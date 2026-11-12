/* Port of src/settings/server/library.ts. GPL-2.0-or-later. */
#define _DEFAULT_SOURCE 1
#include "qa/settings/server/library.h"

#include "qa/foundation/support.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/platform/files/contained.h"

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}

/* /^[a-zA-Z0-9][a-zA-Z0-9_-]{0,31}\.json$/ */
static bool profile_file_name(const char *name) {
  size_t length = strlen(name);
  if (length < 6 || strcmp(name + length - 5, ".json") != 0) return false;
  size_t stem = length - 5;
  if (stem > 32) return false;
  if (!isalnum((unsigned char)name[0])) return false;
  for (size_t i = 1; i < stem; i++)
    if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-')
      return false;
  return true;
}

static int entry_compare(const void *left_raw, const void *right_raw) {
  const qa_server_profile_entry *left = (const qa_server_profile_entry *)left_raw;
  const qa_server_profile_entry *right = (const qa_server_profile_entry *)right_raw;
  return qa_js_locale_compare(left->name, right->name); /* TS localeCompare */
}

void qa_server_profile_entries_free(qa_server_profile_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(entries[i].name);
    free(entries[i].path);
  }
  free(entries);
}

qa_server_profile_entry *qa_list_server_profiles(const qa_config_store *store,
                                                 size_t *out_count) {
  *out_count = 0;
  char *directory = qa_settings_path(store->root, "servers");
  if (directory == NULL) return NULL;
  DIR *dir = opendir(directory);
  if (dir == NULL) {
    int failure = errno;
    if (failure == ENOENT) {
      free(directory);
      return (qa_server_profile_entry *)calloc(1, sizeof(qa_server_profile_entry));
    }
    /* TS rethrows the readdir error: Node's "<CODE>: <description>, scandir '<path>'". */
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, directory);
    qa_node_fs_error(failure, "scandir", directory);
  }
  qa_server_profile_entry *entries = NULL;
  size_t count = 0;
  size_t capacity = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!profile_file_name(entry->d_name)) continue;
    /* entry.isFile(): the entry itself; a link to a file is not one. */
    bool is_file = entry->d_type == DT_REG;
    if (entry->d_type == DT_UNKNOWN) {
      size_t path_length = strlen(directory) + 1 + strlen(entry->d_name) + 1;
      char *full = (char *)malloc(path_length);
      if (full == NULL) continue;
      snprintf(full, path_length, "%s/%s", directory, entry->d_name);
      struct stat info;
      is_file = lstat(full, &info) == 0 && S_ISREG(info.st_mode);
      free(full);
    }
    if (!is_file) continue;
    if (count == capacity) {
      capacity = capacity == 0 ? 8 : capacity * 2;
      qa_server_profile_entry *grown =
          (qa_server_profile_entry *)realloc(entries, capacity * sizeof *entries);
      if (grown == NULL) {
        closedir(dir);
        free(directory);
        qa_server_profile_entries_free(entries, count);
        return NULL;
      }
      entries = grown;
    }
    size_t name_length = strlen(entry->d_name);
    entries[count].name = dup_range(entry->d_name, name_length - 5);
    size_t listed_length = strlen("servers/") + name_length + 1;
    entries[count].path = (char *)malloc(listed_length);
    if (entries[count].path != NULL)
      snprintf(entries[count].path, listed_length, "servers/%s", entry->d_name);
    count++;
  }
  closedir(dir);
  free(directory);
  if (count > 1) qa_js_sort(entries, count, sizeof *entries, entry_compare);
  *out_count = count;
  if (entries == NULL)
    entries = (qa_server_profile_entry *)calloc(1, sizeof(qa_server_profile_entry));
  return entries;
}
