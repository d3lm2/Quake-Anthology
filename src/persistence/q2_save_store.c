/* Port of src/persistence/q2-save-store.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_save_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/platform/files/contained.h"
#include "qa/persistence/save_policy.h"

/* A bound on one save file; a retail Quake II level file is a few MB. */
#define MAX_SAVE_FILE_BYTES ((off_t)256 * 1024 * 1024)

static noreturn void out_of_memory(void) { qa_raise("Error", 0, 0, "%s", "out of memory"); }
/* The Node fs error ("<CODE>: <libuv description>, <syscall> '<path>'"); every code had been
 * spelled EIO with the C library's strerror text. */
static noreturn void system_error(int error, const char *call, const char *path) {
  qa_node_fs_error(error, call, path);
}

void qa_q2_save_store_init(qa_q2_save_store *store, const char *root) {
  store->root = strdup(root);
  if (store->root == NULL) out_of_memory();
}
void qa_q2_save_store_destroy(qa_q2_save_store *store) {
  free(store->root);
  store->root = NULL;
}
char *qa_q2_save_store_folder(const qa_q2_save_store *store, const char *name) {
  return qa_node_path_join2(store->root, name);
}

/* /^(?:server\.ssv|game\.ssv|.+\.sav|.+\.sv2)$/i: the files SV_WipeSavegame removes and SV_CopySaveGame carries. */
static bool save_file_name(const char *name) {
  size_t length = strlen(name);
  char folded[16];
  if (length < sizeof folded) {
    for (size_t i = 0; i <= length; i++) folded[i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] - 'A' + 'a') : name[i];
    if (strcmp(folded, "server.ssv") == 0 || strcmp(folded, "game.ssv") == 0) return true;
  }
  if (length < 5) return false;
  const char *extension = name + length - 4;
  bool matches = extension[0] == '.' && (extension[1] == 's' || extension[1] == 'S') &&
                 (((extension[2] == 'a' || extension[2] == 'A') && (extension[3] == 'v' || extension[3] == 'V')) ||
                  ((extension[2] == 'v' || extension[2] == 'V') && extension[3] == '2'));
  if (!matches) return false;
  /* `.+` does not cross a line terminator. */
  for (size_t i = 0; i + 4 < length; i++) {
    if (name[i] == '\n' || name[i] == '\r') return false;
    if ((unsigned char)name[i] == 0xe2 && (unsigned char)name[i + 1] == 0x80 &&
        ((unsigned char)name[i + 2] == 0xa8 || (unsigned char)name[i + 2] == 0xa9))
      return false;
  }
  return true;
}

typedef struct read_scope {
  const qa_q2_save_store *store;
  const char *name;
  qa_q2_save_files *out;
  char *folder;
  DIR *directory;
  char *path;
  uint8_t *bytes;
  FILE *file;
} read_scope;
static void read_body(void *raw) {
  read_scope *scope = (read_scope *)raw;
  scope->folder = qa_q2_save_store_folder(scope->store, scope->name);
  scope->directory = opendir(scope->folder);
  if (scope->directory == NULL) {
    if (errno == ENOENT) return;
    system_error(errno, "scandir", scope->folder);
  }
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(scope->directory);
    if (entry == NULL) {
      if (errno != 0) system_error(errno, "scandir", scope->folder);
      break;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    free(scope->path);
    scope->path = qa_node_path_join2(scope->folder, entry->d_name);
    /* entry.isFile(): the entry itself, not what a link points at. */
    struct stat status;
    if (entry->d_type == DT_UNKNOWN) {
      if (lstat(scope->path, &status) != 0 || !S_ISREG(status.st_mode)) continue;
    } else if (entry->d_type != DT_REG) {
      continue;
    }
    if (stat(scope->path, &status) != 0) system_error(errno, "open", scope->path);
    if (status.st_size > MAX_SAVE_FILE_BYTES) qa_range_error("Save file %s is implausibly large", entry->d_name);
    scope->file = fopen(scope->path, "rb");
    if (scope->file == NULL) system_error(errno, "open", scope->path);
    size_t length = (size_t)status.st_size, capacity = length + 1, read = 0;
    scope->bytes = (uint8_t *)malloc(capacity);
    if (scope->bytes == NULL) out_of_memory();
    for (;;) {
      if (read == capacity) {
        capacity *= 2;
        uint8_t *grown = (uint8_t *)realloc(scope->bytes, capacity);
        if (grown == NULL) out_of_memory();
        scope->bytes = grown;
      }
      size_t count = fread(scope->bytes + read, 1, capacity - read, scope->file);
      read += count;
      if (count == 0) break;
    }
    if (ferror(scope->file) != 0) system_error(EIO, "read", scope->path);
    (void)fclose(scope->file);
    scope->file = NULL;
    qa_q2_save_files_set(scope->out, entry->d_name, scope->bytes, read);
    free(scope->bytes);
    scope->bytes = NULL;
  }
}
void qa_q2_save_store_read(const qa_q2_save_store *store, const char *name, qa_q2_save_files *out) {
  qa_q2_save_files_init(out);
  read_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.store = store;
  scope.name = name;
  scope.out = out;
  qa_caught_error caught;
  bool completed = qa_try(read_body, &scope, &caught);
  if (scope.file != NULL) (void)fclose(scope.file);
  if (scope.directory != NULL) (void)closedir(scope.directory);
  free(scope.bytes);
  free(scope.path);
  free(scope.folder);
  if (completed) return;
  qa_q2_save_files_free(out);
  qa_rethrow(&caught);
}

void qa_q2_save_store_wipe(const qa_q2_save_store *store, const char *name) {
  qa_q2_save_files files;
  qa_q2_save_store_read(store, name, &files);
  char *folder = qa_q2_save_store_folder(store, name);
  int failed = 0;
  char *failed_path = NULL;
  for (size_t i = 0; i < files.count && failed == 0; i++) {
    if (!save_file_name(files.files[i].name)) continue;
    char *path = qa_node_path_join2(folder, files.files[i].name);
    /* rm(path, { force: true }): a file that is already gone is not an error. */
    if (unlink(path) != 0 && errno != ENOENT) {
      failed = errno;
      failed_path = path;
    } else {
      free(path);
    }
  }
  free(folder);
  qa_q2_save_files_free(&files);
  if (failed == 0) return;
  qa_cleanup node;
  qa_cleanup_push(&node, free, failed_path);
  system_error(failed, "rm", failed_path);
}

void qa_q2_save_store_write(const qa_q2_save_store *store, const char *name, const qa_q2_save_files *files) {
  char *folder = qa_q2_save_store_folder(store, name);
  qa_cleanup folder_node;
  qa_cleanup_push(&folder_node, free, folder);
  for (size_t i = 0; i < files->count; i++) {
    char *path = qa_node_path_join2(folder, files->files[i].name);
    qa_cleanup path_node;
    qa_cleanup_push(&path_node, free, path);
    if (!qa_write_contained_native_file(store->root, path, files->files[i].bytes, files->files[i].length))
      system_error(errno != 0 ? errno : EIO, "open", path);
    qa_cleanup_pop(&path_node);
    free(path);
  }
  qa_cleanup_pop(&folder_node);
  free(folder);
}

static void free_files(void *files) { qa_q2_save_files_free((qa_q2_save_files *)files); }
void qa_q2_save_store_copy(const qa_q2_save_store *store, const char *from, const char *to) {
  qa_q2_save_files source, carried;
  qa_q2_save_store_read(store, from, &source);
  qa_q2_save_files_init(&carried);
  for (size_t i = 0; i < source.count; i++)
    if (save_file_name(source.files[i].name))
      qa_q2_save_files_set(&carried, source.files[i].name, source.files[i].bytes, source.files[i].length);
  qa_q2_save_files_free(&source);
  qa_cleanup node;
  qa_cleanup_push(&node, free_files, &carried);
  qa_q2_save_store_wipe(store, to);
  qa_q2_save_store_write(store, to, &carried);
  qa_cleanup_pop(&node);
  qa_q2_save_files_free(&carried);
}

void qa_q2_save_store_save(const qa_q2_save_store *store, const char *name, const qa_q2_save_files *files) {
  qa_q2_save_store_wipe(store, QA_Q2_CURRENT_SAVE);
  qa_q2_save_store_write(store, QA_Q2_CURRENT_SAVE, files);
  qa_q2_save_store_copy(store, QA_Q2_CURRENT_SAVE, name);
}

void qa_q2_save_store_load(const qa_q2_save_store *store, const char *name, qa_q2_save_files *out) {
  qa_q2_save_files_init(out);
  qa_q2_save_store_copy(store, name, QA_Q2_CURRENT_SAVE);
  qa_q2_save_store_read(store, QA_Q2_CURRENT_SAVE, out);
}
