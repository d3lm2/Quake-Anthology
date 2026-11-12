/* Port of src/app/bootstrap/demo-library.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/demo_library.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/content/mounts/paths.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
struct qa_app_demo_library { char *root; qa_app_demo_library_mounts mounts; char **native_roots; size_t native_root_count; };
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_demo_library *qa_app_demo_library_create_native(const char *root, qa_app_demo_library_mounts mounts,
    const char *const *native_roots, size_t native_root_count) {
  qa_app_demo_library *library = calloc(1, sizeof *library);
  if (!library) return NULL;
  library->root = dup(root);
  library->mounts = mounts;
  if (native_root_count > 0) {
    library->native_roots = calloc(native_root_count, sizeof *library->native_roots);
    if (!library->native_roots) { free(library->root); free(library); return NULL; }
    for (size_t i = 0; i < native_root_count; i++) library->native_roots[i] = dup(native_roots[i]);
    library->native_root_count = native_root_count;
  }
  return library;
}
qa_app_demo_library *qa_app_demo_library_create(const char *root, qa_app_demo_library_mounts mounts) {
  return qa_app_demo_library_create_native(root, mounts, NULL, 0);
}
void qa_app_demo_library_destroy(qa_app_demo_library *library) {
  if (!library) return;
  for (size_t i = 0; i < library->native_root_count; i++) free(library->native_roots[i]);
  free((void *)library->native_roots);
  free(library->root);
  free(library);
}
const char *qa_app_demo_library_root(const qa_app_demo_library *library) { return library->root; }
typedef struct { char **names; size_t count, cap; } name_set;
/* /\.(dem|qwd|dm2|mvd|dm_\d+)$/i */
static bool demo_extension(const char *path) {
  const char *dot = strrchr(path, '.');
  if (!dot) return false;
  const char *ext = dot + 1;
  if (qa_js_equals_ignore_case(ext, "dem") || qa_js_equals_ignore_case(ext, "qwd") || qa_js_equals_ignore_case(ext, "dm2") ||
      qa_js_equals_ignore_case(ext, "mvd"))
    return true;
  if (tolower((unsigned char)ext[0]) != 'd' || tolower((unsigned char)ext[1]) != 'm' || ext[2] != '_' || ext[3] == '\0') return false;
  for (size_t i = 3; ext[i]; i++) if (!isdigit((unsigned char)ext[i])) return false;
  return true;
}
static void add_name(name_set *set, const char *path) {
  if (!demo_extension(path)) return;
  char *normalized = qa_normalize_resource_path(path);
  /* TS names.set(normalized.toLowerCase(), normalized): Unicode case-insensitive key. */
  for (size_t i = 0; i < set->count; i++) {
    if (qa_js_equals_ignore_case(set->names[i], normalized)) { free(set->names[i]); set->names[i] = normalized; return; }
  }
  if (set->count == set->cap) { set->cap = set->cap ? set->cap * 2 : 16; set->names = realloc(set->names, set->cap * sizeof *set->names); }
  set->names[set->count++] = normalized;
}
static const char *scan(const char *root, const char *directory, bool shallow, name_set *set) {
  /* Native game folders also hold the game's own files; only demo locations are listed. */
  if (shallow && directory[0] != '\0' && !qa_js_equals_ignore_case(directory, "demos")) return NULL;
  char *full = NULL;
  if (asprintf(&full, "%s%s%s", root, directory[0] ? "/" : "", directory) < 0) return "out of memory";
  DIR *dir = opendir(full);
  free(full);
  if (!dir) return qa_is_missing_file(errno) ? NULL : "could not scan demo directory";
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char *path = NULL, *disk = NULL;
    if (asprintf(&path, "%s%s%s", directory, directory[0] ? "/" : "", entry->d_name) < 0) { closedir(dir); return "out of memory"; }
    if (asprintf(&disk, "%s/%s", root, path) < 0) { free(path); closedir(dir); return "out of memory"; }
    /* readdir withFileTypes: a symbolic link is neither a directory nor a file. */
    struct stat st;
    bool known = lstat(disk, &st) == 0;
    free(disk);
    const char *error = NULL;
    if (known && S_ISDIR(st.st_mode)) error = scan(root, path, shallow, set);
    else if (known && S_ISREG(st.st_mode)) add_name(set, path);
    free(path);
    if (error) { closedir(dir); return error; }
  }
  closedir(dir);
  return NULL;
}
static int by_name(const void *a, const void *b) { return qa_js_locale_compare(*(char *const *)a, *(char *const *)b); }
qa_app_demo_library_entry *qa_app_demo_library_list(qa_app_demo_library *library, size_t *out_count, const char **out_error) {
  *out_count = 0;
  *out_error = NULL;
  name_set set = {0};
  static const char *const queries[][2] = {{"", ".dem"}, {"", ".qwd"}, {"demos", ".dm2"}, {"demos", ".mvd"},
      {"demos", ".dm_66"}, {"demos", ".dm_67"}, {"demos", ".dm_68"}};
  for (size_t q = 0; q < 7; q++) {
    size_t count = 0;
    char **names = library->mounts.list_files(library->mounts.ctx, queries[q][0], queries[q][1], &count);
    for (size_t i = 0; i < count; i++) {
      char *path = NULL;
      if (asprintf(&path, "%s%s%s", queries[q][0], queries[q][0][0] ? "/" : "", names[i]) >= 0) add_name(&set, path);
      free(path);
      free(names[i]);
    }
    free(names);
  }
  const char *error = scan(library->root, "", false, &set);
  for (size_t i = 0; error == NULL && i < library->native_root_count; i++) error = scan(library->native_roots[i], "", true, &set);
  if (error) {
    for (size_t i = 0; i < set.count; i++) free(set.names[i]);
    free(set.names);
    *out_error = error;
    return NULL;
  }
  qa_js_sort(set.names, set.count, sizeof *set.names, by_name); /* localeCompare ~ byte order for ASCII names */
  qa_app_demo_library_entry *entries = calloc(set.count ? set.count : 1, sizeof *entries);
  for (size_t i = 0; i < set.count; i++) {
    entries[i].id = set.names[i];
    entries[i].label = dup(set.names[i]);
    entries[i].family = qa_app_demo_family_of(set.names[i], QA_APP_DEMO_Q1);
  }
  free(set.names);
  *out_count = set.count;
  return entries;
}
void qa_app_demo_library_entries_free(qa_app_demo_library_entry *entries, size_t count) {
  if (!entries) return;
  for (size_t i = 0; i < count; i++) { free(entries[i].id); free(entries[i].label); }
  free(entries);
}
bool qa_app_demo_library_read(qa_app_demo_library *library, const char *path, uint8_t **out_bytes, size_t *out_count, const char **out_error) {
  *out_error = NULL;
  char *normalized = qa_normalize_resource_path(path);
  /* for (const root of [...this.nativeRoots, this.root]) */
  for (size_t r = 0; r <= library->native_root_count; r++) {
    const char *root = r < library->native_root_count ? library->native_roots[r] : library->root;
    char *disk = NULL;
    if (asprintf(&disk, "%s/%s", root, normalized) < 0) { free(normalized); *out_error = "out of memory"; return false; }
    FILE *file = fopen(disk, "rb");
    int error = errno;
    free(disk);
    struct stat st;
    if (file != NULL && (fstat(fileno(file), &st) != 0 || S_ISDIR(st.st_mode))) {
      /* readFile of a folder rejects with EISDIR. */
      fclose(file);
      free(normalized);
      *out_error = "could not read demo file";
      return false;
    }
    if (file) {
      size_t capacity = 65536, read = 0;
      uint8_t *bytes = malloc(capacity);
      for (size_t got = 1; bytes != NULL && got != 0;) {
        if (read == capacity) {
          uint8_t *grown = realloc(bytes, capacity * 2);
          if (grown == NULL) { free(bytes); bytes = NULL; break; }
          bytes = grown;
          capacity *= 2;
        }
        got = fread(bytes + read, 1, capacity - read, file);
        read += got;
      }
      fclose(file);
      free(normalized);
      if (bytes == NULL) { *out_error = "out of memory"; return false; }
      *out_bytes = bytes;
      *out_count = read;
      return true;
    }
    if (!qa_is_missing_file(error)) { free(normalized); *out_error = "could not read demo file"; return false; }
  }
  bool found = library->mounts.read(library->mounts.ctx, normalized, out_bytes, out_count);
  free(normalized);
  return found;
}
