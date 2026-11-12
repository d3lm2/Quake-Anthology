/* Port of src/content/mounts/paths.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/content/mounts/paths.h"

#include <dirent.h>
#include <errno.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/content/archive/types.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"

/* A TS `throw new Error(...)`: catchable (qa_try), aborting only when
   nothing catches it. */
static noreturn void plain_error(const char *format, const char *argument) {
  qa_raise("Error", 0, 0, format, argument);
}

char *qa_normalize_resource_path(const char *path) {
  if (path == NULL) path = "";
  size_t length = strlen(path);
  char *normalized = (char *)malloc(length + 1);
  if (normalized == NULL) return NULL;
  for (size_t i = 0; i < length; i++)
    normalized[i] = path[i] == '\\' ? '/' : path[i];
  normalized[length] = '\0';
  bool bad = length == 0;
  if (!bad && length >= 2 && normalized[1] == ':') {
    char first = normalized[0];
    if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))
      bad = true;
  }
  size_t start = 0;
  for (size_t i = 0; i <= length && !bad; i++) {
    if (i != length && normalized[i] != '/') continue;
    size_t part_length = i - start;
    if (part_length == 0 || (part_length == 1 && normalized[start] == '.') ||
        (part_length == 2 && normalized[start] == '.' &&
         normalized[start + 1] == '.'))
      bad = true;
    start = i + 1;
  }
  if (bad) {
    free(normalized);
    qa_range_error("Invalid relative resource path: %s", path);
  }
  return normalized;
}

/* TS `isAbsolute(child) || child === ".." || child.startsWith("../")` for
 * child = path.relative(root, path) over two absolute normalized paths: the
 * path is outside unless it is the root or lies beneath it. */
static bool within_root(const char *root, const char *path) {
  size_t root_length = strlen(root);
  if (strcmp(root, "/") == 0) return path[0] == '/';
  return strncmp(path, root, root_length) == 0 &&
         (path[root_length] == '/' || path[root_length] == '\0');
}

char *qa_path_within_root(const char *root, const char *path) {
  char *normalized = qa_normalize_resource_path(path);
  if (normalized == NULL) return NULL;
  qa_cleanup node;
  qa_cleanup_push(&node, free, normalized);
  char *destination = qa_node_path_resolve2(root, normalized);
  qa_cleanup_pop(&node);
  free(normalized);
  qa_cleanup_push(&node, free, destination);
  char *resolved_root = qa_node_path_resolve1(root);
  qa_cleanup_pop(&node);
  bool inside = within_root(resolved_root, destination);
  free(resolved_root);
  if (!inside) {
    free(destination);
    qa_range_error("Resource escapes content root: %s", path);
  }
  return destination;
}

bool qa_is_missing_file(int error_number) {
  return error_number == ENOENT || error_number == ENOTDIR;
}

/* The TS default `directory => readdir(directory)`. */
static bool list_fresh(void *ctx, const char *directory, char ***out_names,
                       size_t *out_count, bool *out_owned) {
  (void)ctx;
  *out_names = NULL;
  *out_count = 0;
  *out_owned = true;
  DIR *handle = opendir(directory);
  if (handle == NULL) {
    int error_number = errno;
    if (qa_is_missing_file(error_number)) return false;
    qa_content_fs_error(error_number, "scandir", directory);
  }
  char **names = NULL;
  size_t count = 0, capacity = 0;
  struct dirent *entry;
  bool oom = false;
  while ((entry = readdir(handle)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (count == capacity) {
      size_t next = capacity == 0 ? 32 : capacity * 2;
      char **grown = (char **)realloc(names, next * sizeof *grown);
      if (grown == NULL) { oom = true; break; }
      names = grown;
      capacity = next;
    }
    size_t length = strlen(entry->d_name) + 1;
    names[count] = (char *)malloc(length);
    if (names[count] == NULL) { oom = true; break; }
    memcpy(names[count], entry->d_name, length);
    count++;
  }
  (void)closedir(handle);
  if (oom) {
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
    qa_range_error("out of memory");
  }
  *out_names = names;
  *out_count = count;
  return true;
}

/* entry.toLowerCase() === part.toLowerCase(), `lowered_part` precomputed. */
static bool name_matches(const char *name, const char *lowered_part) {
  bool ascii = true;
  for (const unsigned char *p = (const unsigned char *)name; *p != '\0'; p++)
    if (*p >= 0x80) { ascii = false; break; }
  if (ascii) {
    size_t i = 0;
    for (; name[i] != '\0'; i++) {
      char c = name[i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != lowered_part[i]) return false; /* also ends at lowered_part's NUL */
    }
    return lowered_part[i] == '\0';
  }
  char *lowered = qa_js_to_lower_case(name);
  if (lowered == NULL) qa_range_error("out of memory");
  bool equal = strcmp(lowered, lowered_part) == 0;
  free(lowered);
  return equal;
}

typedef struct find_state {
  char *normalized;
  char *current;
  char *canonical_root;
  char *part;
  char *lowered_part;
  char **names;
  size_t name_count;
  bool names_owned;
} find_state;
static void find_state_release_names(find_state *state) {
  if (state->names_owned) {
    for (size_t i = 0; i < state->name_count; i++) free(state->names[i]);
    free(state->names);
  }
  state->names = NULL;
  state->name_count = 0;
  state->names_owned = false;
}
static void find_state_release(void *arg) {
  find_state *state = (find_state *)arg;
  find_state_release_names(state);
  free(state->normalized);
  free(state->current);
  free(state->canonical_root);
  free(state->part);
  free(state->lowered_part);
  memset(state, 0, sizeof *state);
}

bool qa_find_content_path_listed(const char *root, const char *path,
                                 qa_content_path_comparison comparison,
                                 qa_content_path_list_fn list, void *list_ctx,
                                 char **out_path) {
  *out_path = NULL;
  if (list == NULL) list = list_fresh;
  find_state state;
  memset(&state, 0, sizeof state);
  state.normalized = qa_normalize_resource_path(path);
  if (state.normalized == NULL) qa_range_error("out of memory");
  /* Everything held here is released when a step below raises. */
  qa_cleanup node;
  qa_cleanup_push(&node, find_state_release, &state);
  state.current = qa_node_path_resolve1(root);
  /* TS: every failure inside the try that isMissingFile accepts is a miss. */
  state.canonical_root = realpath(state.current, NULL);
  if (state.canonical_root == NULL) {
    int error_number = errno;
    if (!qa_is_missing_file(error_number))
      qa_content_fs_error(error_number, "lstat", state.current);
    goto missing;
  }
  size_t start = 0, length = strlen(state.normalized);
  for (size_t i = 0; i <= length; i++) {
    if (i != length && state.normalized[i] != '/') continue;
    size_t part_length = i - start;
    free(state.part);
    state.part = (char *)malloc(part_length + 1);
    if (state.part == NULL) qa_range_error("out of memory");
    memcpy(state.part, state.normalized + start, part_length);
    state.part[part_length] = '\0';
    start = i + 1;
    char *next = NULL;
    if (comparison == QA_CONTENT_PATH_EXACT) {
      next = qa_node_path_resolve2(state.current, state.part);
    } else {
      if (!list(list_ctx, state.current, &state.names, &state.name_count,
                &state.names_owned))
        goto missing;
      free(state.lowered_part);
      state.lowered_part = qa_js_to_lower_case(state.part);
      if (state.lowered_part == NULL) qa_range_error("out of memory");
      const char *exact = NULL, *match = NULL;
      size_t matches = 0;
      for (size_t e = 0; e < state.name_count; e++) {
        const char *name = state.names[e];
        if (exact == NULL && strcmp(name, state.part) == 0) exact = name;
        if (name_matches(name, state.lowered_part)) {
          if (matches == 0) match = name;
          matches++;
        }
      }
      if (exact != NULL) {
        next = qa_node_path_resolve2(state.current, exact);
      } else if (matches > 1) {
        plain_error("Ambiguous content path: %s", path);
      } else if (match == NULL) {
        goto missing;
      } else {
        next = qa_node_path_resolve2(state.current, match);
      }
      find_state_release_names(&state);
    }
    free(state.current);
    state.current = next;
  }
  {
    char *canonical = realpath(state.current, NULL);
    if (canonical == NULL) {
      int error_number = errno;
      if (!qa_is_missing_file(error_number))
        qa_content_fs_error(error_number, "lstat", state.current);
      goto missing;
    }
    bool inside = within_root(state.canonical_root, canonical);
    if (!inside) {
      free(canonical);
      plain_error("Content symlink escapes root: %s", path);
    }
    struct stat info;
    if (stat(canonical, &info) != 0) {
      int error_number = errno;
      if (!qa_is_missing_file(error_number)) {
        qa_cleanup canonical_node;
        qa_cleanup_push(&canonical_node, free, canonical);
        qa_content_fs_error(error_number, "statx", canonical);
      }
      free(canonical);
      goto missing;
    }
    free(canonical);
  }
  qa_cleanup_pop(&node);
  *out_path = state.current;
  state.current = NULL;
  find_state_release(&state);
  return true;
missing:
  qa_cleanup_pop(&node);
  find_state_release(&state);
  return false;
}

bool qa_find_content_path(const char *root, const char *path,
                          qa_content_path_comparison comparison,
                          char **out_path) {
  return qa_find_content_path_listed(root, path, comparison, NULL, NULL,
                                     out_path);
}
