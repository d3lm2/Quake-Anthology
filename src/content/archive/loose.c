/* Port of src/content/archive/loose.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/content/archive/loose.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/content/archive/types.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"

/* TS `within === ".." || within.startsWith("../") || isAbsolute(within)` for
 * path.relative(root, source) over two canonical absolute paths. */
static bool within_root(const char *root, const char *path) {
  size_t root_length = strlen(root);
  if (strcmp(root, "/") == 0) return true;
  return strncmp(path, root, root_length) == 0 &&
         (path[root_length] == '/' || path[root_length] == '\0');
}

bool qa_open_loose_entry(const char *root_path, const char *member_path,
                         qa_loose_entry_handle *out) {
  memset(out, 0, sizeof *out);
  char *path = qa_normalize_entry_path(member_path);
  if (path == NULL) return false;
  size_t path_length = strlen(path);
  if (path_length > 0 && path[path_length - 1] == '/') {
    free(path);
    qa_range_error("Expected a loose file path");
  }
  /* realpathSync failures are plain fs Errors a TS caller may catch. */
  char *root = realpath(root_path, NULL);
  if (root == NULL) {
    int saved = errno;
    free(path);
    qa_content_fs_error(saved, "lstat", root_path);
  }
  char *resolved = qa_node_path_resolve2(root, path);
  char *source = realpath(resolved, NULL);
  if (source == NULL) {
    int saved = errno;
    free(path); free(root);
    qa_cleanup node;
    qa_cleanup_push(&node, free, resolved);
    qa_content_fs_error(saved, "lstat", resolved);
  }
  free(resolved);
  if (!within_root(root, source)) {
    free(path); free(root); free(source);
    qa_range_error("Loose entry escapes root: %s", member_path);
  }
  free(root);
  qa_archive_source storage;
  qa_cleanup path_node, source_node;
  qa_cleanup_push(&path_node, free, path);
  qa_cleanup_push(&source_node, free, source);
  bool opened = qa_file_source_init(&storage, source);
  qa_cleanup_pop(&source_node);
  qa_cleanup_pop(&path_node);
  if (!opened) {
    free(path); free(source);
    return false;
  }
  out->path = path;
  out->source = source;
  out->byte_length = storage.byte_length;
  out->storage = storage;
  return true;
}

uint8_t *qa_loose_entry_read(qa_loose_entry_handle *entry, size_t *out_length) {
  return qa_archive_source_read(&entry->storage, 0, entry->storage.byte_length,
                                out_length);
}

void qa_loose_entry_close(qa_loose_entry_handle *entry) {
  qa_archive_source_close(&entry->storage);
}

void qa_loose_entry_destroy(qa_loose_entry_handle *entry) {
  qa_archive_source_destroy(&entry->storage);
  free(entry->path);
  entry->path = NULL;
  free(entry->source);
  entry->source = NULL;
}

static void destroy_entry(void *arg) {
  qa_loose_entry_destroy((qa_loose_entry_handle *)arg);
}

uint8_t *qa_read_loose_entry(const char *root_path, const char *member_path,
                             size_t *out_length) {
  qa_loose_entry_handle entry;
  if (!qa_open_loose_entry(root_path, member_path, &entry)) return NULL;
  /* TS try/finally: the entry is closed when the read throws. */
  qa_cleanup node;
  qa_cleanup_push(&node, destroy_entry, &entry);
  uint8_t *bytes = qa_loose_entry_read(&entry, out_length);
  qa_cleanup_pop(&node);
  qa_loose_entry_destroy(&entry);
  return bytes;
}
