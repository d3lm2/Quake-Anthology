/* Port of src/persistence/save-policy.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/persistence/save_policy.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/platform/files/contained.h"

const char *qa_save_unavailable(const qa_save_eligibility *state,
                                qa_save_purpose purpose) {
  if (!state->active) return "No active world to save.";
  if (purpose == QA_SAVE_PURPOSE_TRANSITION)
    return state->authority == QA_SAVE_AUTHORITY_REMOTE
               ? "Only the authoritative world can retain transition state."
               : NULL;
  if (state->authority != QA_SAVE_AUTHORITY_OFFLINE)
    return "Save/load unavailable while hosting a network game.";
  if (state->family != QA_GAME_FAMILY_Q3 &&
      state->mode == QA_SAVE_MODE_DEATHMATCH && !state->match_save)
    return "Cannot save a deathmatch game.";
  if (state->family != QA_GAME_FAMILY_Q3 && state->intermission &&
      !state->match_save)
    return "Cannot save during intermission.";
  /* Server-administration saves persist world state even with no players in it. */
  if (state->player_health_count == 0 && !state->match_save)
    return "No active player to save.";
  if (state->family != QA_GAME_FAMILY_Q3 && !state->match_save) {
    for (size_t i = 0; i < state->player_health_count; i++)
      if (!isfinite(state->player_health[i]) || state->player_health[i] <= 0)
        return "Cannot save with a dead player.";
  }
  return NULL;
}

/* --- node:path (POSIX) resolve/relative/extname for byte strings --- */

static char *dup_range(const char *start, size_t length) {
  char *out = malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, start, length);
  out[length] = '\0';
  return out;
}

/* node:path resolve(directory, name) (name NULL: resolve(directory)), from foundation/node_path
 * (checked against bun's node:path). */
static char *path_resolve(const char *directory, const char *name) {
  if (name == NULL) return qa_node_path_resolve1(directory != NULL ? directory : "");
  return qa_node_path_resolve2(directory != NULL ? directory : "", name);
}

/* extname(name) !== "" (node:path). */
static bool has_extension(const char *name) {
  char *extension = qa_node_path_extname(name);
  bool has = extension[0] != '\0';
  free(extension);
  return has;
}

char *qa_save_command_path(const char *directory, const char *name) {
  if (name[0] == '\0') qa_range_error("Enter a save name or file path");
  if (has_extension(name)) return path_resolve(directory, name);
  size_t nl = strlen(name);
  char *with_ext = malloc(nl + sizeof ".sav");
  if (with_ext == NULL) return NULL;
  memcpy(with_ext, name, nl);
  memcpy(with_ext + nl, ".sav", sizeof ".sav");
  char *out = path_resolve(directory, with_ext);
  free(with_ext);
  return out;
}

/* TS containedFileParts (platform/files/contained.ts): splits on "/"
 * and rejects empty/"."/".." parts, "\\", NUL and ":". Returns a
 * malloc'd copy split in place; *out_parts indexes into it. */
static char *contained_file_parts(const char *name, char ***out_parts,
                                  size_t *out_count) {
  size_t length = strlen(name);
  bool invalid = length == 0 || strchr(name, '\\') != NULL ||
                 strchr(name, ':') != NULL;
  char *work = dup_range(name, length);
  if (work == NULL) return NULL;
  size_t capacity = length / 2 + 2;
  char **parts = malloc(capacity * sizeof *parts);
  if (parts == NULL) { free(work); return NULL; }
  size_t count = 0;
  char *cursor = work;
  for (;;) {
    char *slash = strchr(cursor, '/');
    if (slash != NULL) *slash = '\0';
    if (cursor[0] == '\0' || strcmp(cursor, ".") == 0 ||
        strcmp(cursor, "..") == 0)
      invalid = true;
    parts[count++] = cursor;
    if (slash == NULL) break;
    cursor = slash + 1;
  }
  if (invalid) {
    free(parts);
    free(work);
    qa_range_error(
        "File path must be relative and stay inside its storage directory");
  }
  *out_parts = parts;
  *out_count = count;
  return work;
}

static bool ascii_equal_ignore_case(const char *left, const char *right,
                                    size_t length) {
  if (strlen(right) != length) return false;
  for (size_t i = 0; i < length; i++)
    if (tolower((unsigned char)left[i]) != tolower((unsigned char)right[i]))
      return false;
  return true;
}

/* name.toLowerCase().endsWith(extension), for an ASCII extension. */
static bool ends_with_extension(const char *name, const char *extension) {
  size_t length = strlen(name), extension_length = strlen(extension);
  if (length < extension_length) return false;
  for (size_t i = 0; i < extension_length; i++)
    if (tolower((unsigned char)name[length - extension_length + i]) != (unsigned char)extension[i]) return false;
  return true;
}

/* relative(resolve(directory), resolve(path)) when the path is the directory or inside it; otherwise the TS
 * RangeError. `allow_empty` false also refuses the directory itself (writeContainedNativeFile). */
static char *contained_relative(const char *directory, const char *path, bool allow_empty) {
  char *root = path_resolve(directory, NULL);
  char *full = path_resolve(path, NULL);
  if (root == NULL || full == NULL) { free(root); free(full); return NULL; }
  char *name = NULL;
  size_t root_length = strlen(root);
  if (strcmp(root, full) == 0) {
    if (allow_empty) name = dup_range("", 0);
  } else if (strncmp(full, root, root_length) == 0 &&
             (root_length == 1 || full[root_length] == '/')) {
    const char *rest = full + root_length;
    if (*rest == '/') rest++;
    name = dup_range(rest, strlen(rest));
  }
  free(full);
  if (name == NULL) {
    qa_cleanup root_node;
    qa_cleanup_push(&root_node, free, root);
    qa_range_error("Save path is outside the save directory: %s", root);
  }
  free(root);
  return name;
}

char *qa_contained_save_name(const char *directory, const char *path) {
  return qa_contained_save_name_extension(directory, path, ".sav");
}
char *qa_contained_save_name_extension(const char *directory, const char *path, const char *extension) {
  char *root = path_resolve(directory, NULL);
  char *full = path_resolve(path, NULL);
  if (root == NULL || full == NULL) { free(root); free(full); return NULL; }
  /* node:path relative(root, full) for two absolute paths. */
  char *name = NULL;
  size_t root_length = strlen(root);
  if (strcmp(root, full) == 0) {
    name = dup_range("", 0);
  } else if (strncmp(full, root, root_length) == 0 &&
             (root_length == 1 || full[root_length] == '/')) {
    const char *rest = full + root_length;
    if (*rest == '/') rest++;
    name = dup_range(rest, strlen(rest));
  }
  free(full);
  qa_cleanup root_node;
  qa_cleanup_push(&root_node, free, root);
  if (name == NULL) {
    /* Relative form would be absolute-ish or start with "..". */
    qa_range_error("Save path is outside the save directory: %s", root);
  }
  qa_cleanup name_node;
  qa_cleanup_push(&name_node, free, name);
  char **parts = NULL;
  size_t part_count = 0;
  char *work = contained_file_parts(name, &parts, &part_count);
  qa_cleanup_pop(&name_node);
  qa_cleanup_pop(&root_node);
  if (work == NULL) { free(root); free(name); return NULL; }
  for (size_t i = 0; i < part_count; i++) {
    size_t part_length = strlen(parts[i]);
    size_t bare = part_length;
    if (bare >= 4 && ascii_equal_ignore_case(parts[i] + bare - 4, ".sav", 4))
      bare -= 4;
    if (ascii_equal_ignore_case(parts[i], "current", bare) && bare == 7) {
      free(parts); free(work); free(root); free(name);
      qa_range_error("The current slot is reserved for transition state");
    }
  }
  free(parts);
  free(work);
  free(root);
  if (!ends_with_extension(name, extension)) {
    free(name);
    qa_range_error("Save path must end in %s", extension);
  }
  return name;
}

/* TS mkdir(path, { recursive: true }) as Bun runs it: the whole path first; on ENOENT the
 * missing ancestors from the deepest existing one down. A failure raises Bun's error with the
 * path being created then (an existing non-directory is EEXIST). `absolute` is resolved. */
static void free_text(void *arg) { free(arg); }
static void mkdir_recursive(const char *absolute) {
  if (mkdir(absolute, 0777) == 0) return;
  int code = errno;
  struct stat status;
  if (code == EEXIST && stat(absolute, &status) == 0 && S_ISDIR(status.st_mode)) return;
  if (code != ENOENT) qa_node_fs_error(code, "mkdir", absolute);
  size_t length = strlen(absolute);
  char *path = dup_range(absolute, length);
  if (path == NULL) qa_range_error("Out of memory");
  qa_cleanup path_node;
  qa_cleanup_push(&path_node, free_text, path);
  /* Up: cut at each separator until one ancestor exists or is made. */
  size_t cut = length;
  for (;;) {
    while (cut > 0 && path[cut - 1] != '/') cut--;
    if (cut <= 1) { cut = 1; break; } /* reached the root: create from the first component */
    path[cut - 1] = '\0';
    bool made = mkdir(path, 0777) == 0;
    code = errno;
    if (!made && code != EEXIST && code != ENOENT) qa_node_fs_error(code, "mkdir", path);
    path[cut - 1] = '/';
    if (made || code == EEXIST) break;
    cut--;
  }
  /* Down: every component after the one made or found, then the whole path. */
  for (size_t at = cut; at < length; at++) {
    if (path[at] != '/') continue;
    path[at] = '\0';
    if (mkdir(path, 0777) != 0 && errno != EEXIST) qa_node_fs_error(errno, "mkdir", path);
    path[at] = '/';
  }
  if (mkdir(path, 0777) != 0) {
    code = errno;
    if (!(code == EEXIST && stat(path, &status) == 0 && S_ISDIR(status.st_mode))) qa_node_fs_error(code, "mkdir", path);
  }
  qa_cleanup_pop(&path_node);
  free(path);
}

static bool commit_contained(const char *directory, char *name, const uint8_t *bytes, size_t length);

bool qa_write_contained_save(const char *directory, const char *path,
                             const uint8_t *bytes, size_t length) {
  return qa_write_contained_save_extension(directory, path, bytes, length, ".sav");
}
bool qa_write_contained_save_extension(const char *directory, const char *path, const uint8_t *bytes, size_t length,
                                       const char *extension) {
  char *name = qa_contained_save_name_extension(directory, path, extension);
  if (name == NULL) return false;
  return commit_contained(directory, name, bytes, length);
}

/* TS writeContainedNativeFile: a file of a native save folder, which has the original's own names (`server.ssv`,
 * `MAP.sv2`, `current/`). It is contained like any save, but it has no fixed extension and may sit in the
 * `current` working folder. */
bool qa_write_contained_native_file(const char *directory, const char *path, const uint8_t *bytes, size_t length) {
  char *name = contained_relative(directory, path, false);
  if (name == NULL) return false;
  /* containedFileParts(name), for its validation. */
  char **parts = NULL;
  size_t part_count = 0;
  qa_cleanup name_node;
  qa_cleanup_push(&name_node, free, name);
  char *work = contained_file_parts(name, &parts, &part_count);
  qa_cleanup_pop(&name_node);
  if (work == NULL) { free(name); return false; }
  free(parts);
  free(work);
  return commit_contained(directory, name, bytes, length);
}

/* The TS finally of commitContained: close the temporary's descriptor, rm the temporary
 * (force), close the parent. Runs on success and when a step raises. */
typedef struct commit_scope {
  int parent, file;
  char *leaf;
  char temporary[64 + 256 + 32];
} commit_scope;
static void commit_scope_finish(void *arg) {
  commit_scope *scope = arg;
  if (scope->file >= 0) close(scope->file);
  scope->file = -1;
  if (scope->parent >= 0) {
    if (scope->temporary[0] != '\0') (void)unlinkat(scope->parent, scope->temporary, 0);
    close(scope->parent);
  }
  scope->parent = -1;
  free(scope->leaf);
  scope->leaf = NULL;
}
/* "/proc/self/fd/<parent>/<name>": the path TS's calls name. */
static void fd_path(char *out, size_t capacity, int parent, const char *name) {
  (void)snprintf(out, capacity, "/proc/self/fd/%d/%s", parent, name);
}

/* TS commitContained: commits through a held parent descriptor so directory replacement cannot redirect a save.
 * Takes ownership of `name`. Each failing step raises its Node error (mkdir, open, write, fsync, rename); false
 * only on OOM. */
static bool commit_contained(const char *directory, char *name, const uint8_t *bytes, size_t length) {
  qa_cleanup name_node;
  qa_cleanup_push(&name_node, free_text, name);
  /* await mkdir(resolve(directory, dirname(name)), { recursive: true }) */
  char *folder = qa_node_path_dirname(name);
  char *target = folder != NULL ? path_resolve(directory, folder) : NULL;
  free(folder);
  if (target == NULL) { qa_cleanup_pop(&name_node); free(name); return false; }
  qa_cleanup target_node;
  qa_cleanup_push(&target_node, free_text, target);
  mkdir_recursive(target);
  qa_cleanup_pop(&target_node);
  free(target);
  commit_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.file = -1;
  scope.parent = qa_open_contained_parent(directory, name, true, &scope.leaf);
  qa_cleanup_pop(&name_node);
  free(name);
  if (scope.parent < 0) return false;
  qa_cleanup scope_node;
  qa_cleanup_push(&scope_node, commit_scope_finish, &scope);
  /* TS: `.${leaf}.${crypto.randomUUID()}.tmp` (a leaf is at most NAME_MAX bytes). */
  static unsigned long counter = 0;
  (void)snprintf(scope.temporary, sizeof scope.temporary, ".%s.%ld-%lu.tmp", scope.leaf, (long)getpid(), ++counter);
  char path[sizeof scope.temporary + 64], destination[sizeof scope.temporary + 64];
  fd_path(path, sizeof path, scope.parent, scope.temporary);
  scope.file = openat(scope.parent, scope.temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (scope.file < 0) qa_node_fs_error(errno, "open", path);
  size_t position = 0;
  while (position < length) {
    ssize_t count = write(scope.file, bytes + position, length - position);
    if (count < 0) qa_node_fs_error(errno, "write", NULL);
    if (count == 0) qa_raise("Error", 0, 0, "Save write made no progress");
    position += (size_t)count;
  }
  if (fsync(scope.file) != 0) qa_node_fs_error(errno, "fsync", NULL);
  int file = scope.file;
  scope.file = -1;
  if (close(file) != 0) qa_node_fs_error(errno, "close", NULL);
  fd_path(destination, sizeof destination, scope.parent, scope.leaf);
  if (renameat(scope.parent, scope.temporary, scope.parent, scope.leaf) != 0)
    qa_node_fs_error_paths(errno, "rename", path, destination);
  if (fsync(scope.parent) != 0) qa_node_fs_error(errno, "fsync", NULL);
  qa_cleanup_pop(&scope_node);
  commit_scope_finish(&scope);
  return true;
}

/* TS assertLoadPathAllowed (added for startup.ts routeCommand). */
static bool load_path_inside(const char *root_input, const char *full) {
  char *root = path_resolve(root_input, NULL);
  if (root == NULL) qa_range_error("Out of memory");
  size_t root_length = strlen(root);
  bool inside = strcmp(root, full) == 0 || (strncmp(full, root, root_length) == 0 &&
      (root_length == 1 || full[root_length] == '/'));
  free(root);
  return inside;
}
void qa_assert_load_path_allowed(const char *directory, const char *path, const qa_command_context *source,
                                 bool allow_any_path, const char *const *trusted_directories, size_t trusted_count) {
  if (allow_any_path) return;
  char *full = path_resolve(path, NULL);
  if (full == NULL) qa_range_error("Out of memory");
  bool inside = load_path_inside(directory, full);
  for (size_t i = 0; !inside && i < trusted_count; i++) inside = load_path_inside(trusted_directories[i], full);
  if (inside || source == NULL || (source->producer == NULL && (source->origin.kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE
      || source->origin.kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE || source->origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT))) {
    free(full);
    return;
  }
  qa_cleanup full_node;
  qa_cleanup_push(&full_node, free, full);
  qa_range_error("load of a path outside the save directory is only accepted from the local console; start with "
      "--allow-load-paths to allow it from scripts, game modules and remote clients: %s", full);
}
