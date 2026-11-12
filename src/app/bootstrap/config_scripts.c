/* Port of src/app/bootstrap/config-scripts.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/config_scripts.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/content/mounts/paths.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/platform/files/contained.h"
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static char *join_path(const char *a, const char *b) {
  size_t cap = strlen(a) + strlen(b) + 2;
  char *path = malloc(cap);
  if (path) snprintf(path, cap, "%s/%s", a, b);
  return path;
}
char *qa_app_console_config_root(const char *user_content_root) {
  char *root = user_content_root ? dup(user_content_root) : qa_default_user_content_root();
  char *path = join_path(root, "console");
  free(root);
  return path;
}
bool qa_app_seat_console_config(const char *root, qa_seat_id seat, qa_config_store *out) {
  char seat_part[64];
  snprintf(seat_part, sizeof seat_part, "settings/seat-%lld", (long long)seat.index);
  char *path = join_path(root, seat_part);
  bool ok = qa_config_store_init(out, path);
  free(path);
  return ok;
}
static const qa_command_origin *caller_origin(const qa_command_origin *origin) {
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller) origin = origin->caller;
  return origin;
}
/* TS (await readFile(path)).toString("latin1"): the file's bytes, one per code unit (a NUL ends the C text). A
 * read that fails raises the Node error, as readFile rejects. */
static char *read_file_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) qa_node_fs_error(errno, "open", path);
  struct stat status;
  if (fstat(fileno(file), &status) != 0 || S_ISDIR(status.st_mode)) {
    fclose(file);
    qa_raise("Error", 0, 0, "EISDIR: illegal operation on a directory, read");
  }
  size_t capacity = 4096, length = 0;
  char *text = malloc(capacity);
  if (text == NULL) { fclose(file); qa_range_error("Out of memory"); }
  for (;;) {
    if (length + 1 >= capacity) {
      char *grown = realloc(text, capacity * 2);
      if (grown == NULL) { free(text); fclose(file); qa_range_error("Out of memory"); }
      text = grown;
      capacity *= 2;
    }
    size_t read = fread(text + length, 1, capacity - length - 1, file);
    length += read;
    if (read == 0) break;
  }
  bool failed = ferror(file) != 0;
  fclose(file);
  if (failed) { free(text); qa_raise("Error", 0, 0, "EIO: i/o error, read '%s'", path); }
  text[length] = '\0';
  return text;
}
/* TS configNames: readdir withFileTypes, so a symbolic link is neither a file nor a directory. */
static void config_names(const char *root, const char *prefix, char ***names, size_t *count, size_t *cap) {
  char *full = NULL;
  if (asprintf(&full, "%s%s%s", root, prefix[0] ? "/" : "", prefix) < 0) qa_range_error("Out of memory");
  DIR *dir = opendir(full);
  if (!dir) {
    int code = errno;
    if (code == ENOENT) { free(full); return; }
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, full);
    qa_node_fs_error(code, "scandir", full);
  }
  free(full);
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char *name = NULL, *disk = NULL;
    if (asprintf(&name, "%s%s%s", prefix, prefix[0] ? "/" : "", entry->d_name) < 0) { closedir(dir); qa_range_error("Out of memory"); }
    if (asprintf(&disk, "%s/%s", root, name) < 0) { free(name); closedir(dir); qa_range_error("Out of memory"); }
    struct stat st;
    bool known = lstat(disk, &st) == 0;
    free(disk);
    if (known && S_ISDIR(st.st_mode)) config_names(root, name, names, count, cap);
    else if (known && S_ISREG(st.st_mode)) {
      /* name.toLowerCase().endsWith(".cfg") */
      size_t len = strlen(name);
      bool cfg = len >= 4 && qa_js_equals_ignore_case(name + len - 4, ".cfg");
      if (cfg) {
        if (*count == *cap) {
          *cap = *cap ? *cap * 2 : 16;
          char **grown = realloc(*names, *cap * sizeof **names);
          if (grown == NULL) { free(name); closedir(dir); qa_range_error("Out of memory"); }
          *names = grown;
        }
        (*names)[(*count)++] = name;
        continue;
      }
    }
    free(name);
  }
  closedir(dir);
}
static char *legacy_config_path(qa_app_legacy_console_config_sources sources) {
  char *shared = NULL;
  if (qa_find_content_path(sources.shared_root, "config.cfg", QA_CONTENT_PATH_CASE_INSENSITIVE, &shared)) {
    struct stat st;
    if (stat(shared, &st) == 0 && S_ISREG(st.st_mode)) return shared;
    free(shared);
  }
  char *newest = NULL;
  double modified = -1;
  for (size_t i = 0; i < sources.game_root_count; i++) {
    char *path = NULL;
    if (!qa_find_content_path(sources.game_roots[i], "config.cfg", QA_CONTENT_PATH_CASE_INSENSITIVE, &path)) continue;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
      double mtime = (double)st.st_mtime * 1000.0 + (double)st.st_mtim.tv_nsec / 1e6;
      if (!newest || mtime > modified) { free(newest); newest = path; modified = mtime; continue; }
    }
    free(path);
  }
  return newest;
}
static void free_text_slot(void *raw) { free(*(char **)raw); }
/* readFile(path).toString("latin1") of a path a lookup found; the path is freed either way. */
static char *read_found(char *path) {
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, path);
  char *text = read_file_text(path);
  qa_cleanup_pop(&guard);
  free(path);
  return text;
}
char *qa_app_read_console_script(qa_app_read_console_script_options options) {
  char *name = qa_normalize_resource_path(options.name);
  if (name == NULL) qa_range_error("Out of memory");
  qa_cleanup name_guard;
  qa_cleanup_push(&name_guard, free_text_slot, &name);
  const qa_command_origin *origin = caller_origin(&options.source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT)
    qa_raise("Error", 0, 0, "%s", "Remote clients cannot read local configuration scripts");
  char *result = NULL;
  /* name.toLowerCase() === options.native.layout.script */
  if (options.native != NULL && qa_js_equals_ignore_case(name, options.native->layout->script))
    result = qa_native_config_read_script(options.native);
  if (result == NULL && origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    char *relative = NULL;
    if (asprintf(&relative, "settings/seat-%lld/%s", (long long)origin->seat.index, name) < 0) qa_range_error("Out of memory");
    char *path = NULL;
    bool found = qa_find_content_path(options.console_root, relative, QA_CONTENT_PATH_CASE_INSENSITIVE, &path);
    free(relative);
    if (found) result = read_found(path);
  }
  /* name.toLowerCase() === "config.cfg" */
  if (result == NULL && options.has_legacy_config && qa_js_equals_ignore_case(name, "config.cfg")) {
    char *legacy = legacy_config_path(options.legacy_config);
    if (legacy != NULL) result = read_found(legacy);
  }
  if (result == NULL) {
    char *path = NULL;
    if (qa_find_content_path(options.settings->root, name, QA_CONTENT_PATH_CASE_INSENSITIVE, &path)) result = read_found(path);
  }
  if (result == NULL && options.native != NULL) {
    /* Scripts the game itself writes into its folder (listip.cfg) are read back from there. */
    char *written = NULL;
    if (qa_find_content_path(options.native->layout->directory, name, QA_CONTENT_PATH_CASE_INSENSITIVE, &written)) {
      struct stat status;
      if (stat(written, &status) == 0 && S_ISREG(status.st_mode)) result = read_found(written);
      else free(written);
    }
  }
  qa_app_source_script_reader reader = options.has_mounted_script ? options.mounted_script : options.mounted;
  bool has_reader = options.has_mounted_script || options.has_mounted;
  if (result == NULL && has_reader) {
    uint8_t *bytes = NULL; size_t count = 0;
    if (reader.open(reader.ctx, name, &bytes, &count)) {
      result = malloc(count + 1);
      if (result == NULL) { free(bytes); qa_range_error("Out of memory"); }
      memcpy(result, bytes, count);
      result[count] = '\0';
      free(bytes);
    }
  }
  qa_cleanup_pop(&name_guard);
  free(name);
  return result;
}
struct qa_app_console_script_files {
  qa_app_console_script_files_options options;
  long reads;
  bool retiring, closed;
};
qa_app_console_script_files *qa_app_console_script_files_create(qa_app_console_script_files_options options) {
  qa_app_console_script_files *files = calloc(1, sizeof *files);
  if (!files) return NULL;
  files->options = options;
  return files;
}
void qa_app_console_script_files_destroy(qa_app_console_script_files *files) {
  if (files == NULL) return;
  if (files->options.release != NULL) files->options.release(files->options.release_ctx);
  free(files);
}
const char *qa_app_console_script_files_root(const qa_app_console_script_files *files) { return files->options.settings->root; }
qa_native_config *qa_app_console_script_files_native(const qa_app_console_script_files *files) { return files->options.native; }
qa_game_layers *qa_app_console_script_files_layers(const qa_app_console_script_files *files) {
  return files->options.native != NULL ? files->options.native->layers : files->options.mixed_layers;
}
static void acquire(qa_app_console_script_files *files) {
  if (files->retiring) qa_raise("Error", 0, 0, "%s", "Configuration reader is retired");
  files->reads++;
}
static void release_read(qa_app_console_script_files *files) { files->reads--; }
/* TS `try { ... } finally { this.releaseRead(); }` around a read that can raise. */
static void release_read_cleanup(void *files) { release_read(files); }
static int by_entry_name(const void *a, const void *b) {
  return strcmp(((const qa_app_console_script_entry *)a)->name, ((const qa_app_console_script_entry *)b)->name);
}
/* TS entries.set(name.toLowerCase(), { name, kind }): a Unicode case-insensitive key keeps its first position and
 * takes the latest entry (list_take_names). */
/* TS list's try/finally (releaseRead) and C storage on a raise. */
typedef struct list_state {
  qa_app_console_script_files *files;
  qa_app_console_script_entry *entries; size_t count;
  char **names; size_t name_count, name_cap;
} list_state;
static void list_state_release(void *raw) {
  list_state *state = raw;
  for (size_t i = 0; i < state->count; i++) free(state->entries[i].name);
  free(state->entries);
  for (size_t i = 0; i < state->name_count; i++) free(state->names[i]);
  free(state->names);
  release_read(state->files);
}
static void list_take_names(list_state *state, const char *root, const char *kind) {
  config_names(root, "", &state->names, &state->name_count, &state->name_cap);
  for (size_t i = 0; i < state->name_count; i++) {
    char *name = state->names[i];
    state->names[i] = NULL;
    bool replaced = false;
    for (size_t j = 0; j < state->count && !replaced; j++) {
      if (!qa_js_equals_ignore_case(state->entries[j].name, name)) continue;
      free(state->entries[j].name);
      state->entries[j] = (qa_app_console_script_entry){name, kind};
      replaced = true;
    }
    if (replaced) continue;
    qa_app_console_script_entry *grown = realloc(state->entries, (state->count + 1) * sizeof *grown);
    if (grown == NULL) { free(name); qa_range_error("Out of memory"); }
    state->entries = grown;
    state->entries[state->count++] = (qa_app_console_script_entry){name, kind};
  }
  free(state->names);
  state->names = NULL; state->name_count = state->name_cap = 0;
}
qa_app_console_script_entry *qa_app_console_script_files_list(qa_app_console_script_files *files, qa_command_context source, size_t *out_count) {
  acquire(files);
  list_state state = {files, NULL, 0, NULL, 0, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, list_state_release, &state);
  const qa_command_origin *origin = caller_origin(&source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT)
    qa_raise("Error", 0, 0, "%s", "Remote clients cannot list local configuration scripts");
  list_take_names(&state, files->options.settings->root, "product");
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    qa_config_store seat_store;
    if (!qa_app_seat_console_config(files->options.console_root, origin->seat, &seat_store)) qa_range_error("Out of memory");
    qa_cleanup store_guard;
    qa_cleanup_push(&store_guard, (void (*)(void *))qa_config_store_destroy, &seat_store);
    list_take_names(&state, seat_store.root, "seat");
    qa_cleanup_pop(&store_guard);
    qa_config_store_destroy(&seat_store);
  }
  qa_js_sort(state.entries, state.count, sizeof *state.entries, by_entry_name);
  qa_cleanup_pop(&guard);
  release_read(files);
  *out_count = state.count;
  return state.entries;
}
void qa_app_console_script_entries_free(qa_app_console_script_entry *entries, size_t count) {
  if (!entries) return;
  for (size_t i = 0; i < count; i++) free(entries[i].name);
  free(entries);
}
char *qa_app_console_script_files_read(qa_app_console_script_files *files, const char *name, qa_command_context source) {
  acquire(files);
  qa_app_read_console_script_options options = {0};
  options.name = name;
  options.source = source;
  options.console_root = files->options.console_root;
  options.settings = files->options.settings;
  options.mounted = files->options.mounted; options.has_mounted = files->options.has_mounted;
  options.mounted_script = files->options.mounted_script; options.has_mounted_script = files->options.has_mounted_script;
  options.has_legacy_config = files->options.has_legacy_config; options.legacy_config = files->options.legacy_config;
  options.native = files->options.native;
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_read_cleanup, files);
  char *text = qa_app_read_console_script(options);
  qa_cleanup_pop(&guard);
  release_read(files);
  return text;
}
bool qa_app_console_script_files_read_mounted_script(qa_app_console_script_files *files, const char *name, uint8_t **out_bytes, size_t *out_count) {
  acquire(files);
  qa_app_source_script_reader reader = files->options.has_mounted_script ? files->options.mounted_script : files->options.mounted;
  bool has = files->options.has_mounted_script || files->options.has_mounted;
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_read_cleanup, files);
  bool found = has && reader.open(reader.ctx, name, out_bytes, out_count);
  qa_cleanup_pop(&guard);
  release_read(files);
  return found;
}
bool qa_app_console_script_files_read_mounted(qa_app_console_script_files *files, const char *name, uint8_t **out_bytes, size_t *out_count) {
  acquire(files);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_read_cleanup, files);
  bool found = files->options.has_mounted && files->options.mounted.open(files->options.mounted.ctx, name, out_bytes, out_count);
  qa_cleanup_pop(&guard);
  release_read(files);
  return found;
}
char **qa_app_console_script_files_list_mounted(qa_app_console_script_files *files, const char *directory, const char *extension, qa_command_context source, size_t *out_count) {
  acquire(files);
  const qa_command_origin *origin = caller_origin(&source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    release_read(files);
    qa_raise("Error", 0, 0, "%s", "Remote clients cannot list local mounted files");
  }
  *out_count = 0;
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_read_cleanup, files);
  char **names = files->options.mounted_files ? files->options.mounted_files(files->options.mounted_ctx, directory, extension, out_count) : NULL;
  qa_cleanup_pop(&guard);
  release_read(files);
  return names;
}
bool qa_app_console_script_files_open_mounted(qa_app_console_script_files *files, const char *path, qa_command_context source, uint8_t **out_bytes, size_t *out_count) {
  acquire(files);
  const qa_command_origin *origin = caller_origin(&source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    release_read(files);
    qa_raise("Error", 0, 0, "%s", "Remote clients cannot open local mounted files");
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_read_cleanup, files);
  bool found = files->options.mounted_resource && files->options.mounted_resource(files->options.mounted_ctx, path, out_bytes, out_count);
  qa_cleanup_pop(&guard);
  release_read(files);
  return found;
}
void qa_app_console_script_files_close(qa_app_console_script_files *files) {
  if (files->closed) return;
  files->closed = true;
  files->retiring = true;
  /* Synchronous port: no reads can be in flight across calls. */
  if (files->options.retire_mounted) files->options.retire_mounted(files->options.mounted.ctx);
}
const char *qa_app_console_script_files_write(qa_app_console_script_files *files, const char *(*operation)(void *ctx), void *ctx) {
  if (files->retiring) return "Configuration reader is retired";
  return operation(ctx);
}

/* ---- TS sourceScriptReader ---- */
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
typedef struct source_script_reader {
  qa_mounted_content_reader *reader;
  char *plan_id; /* the reader's plan borrows it */
  char **owners; size_t owner_count;
} source_script_reader;
static bool source_script_owned(void *ctx, const qa_content_mount *mount) {
  const source_script_reader *state = ctx;
  const qa_mount_identity *identity = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive.identity : &mount->as.loose.identity;
  for (size_t i = 0; i < state->owner_count; i++) if (strcmp(state->owners[i], identity->content) == 0) return true;
  return false;
}
static bool source_script_open(void *ctx, const char *name, uint8_t **out_bytes, size_t *out_count) {
  source_script_reader *state = ctx;
  qa_content_opened_resource opened;
  if (!qa_mounted_content_reader_open(state->reader, name, source_script_owned, state, &opened)) return false;
  *out_bytes = opened.bytes;
  *out_count = opened.byte_length;
  return true;
}
static const qa_mount_identity *plan_mount_identity(const qa_resolved_mount_plan *plan, const char *id) {
  for (size_t i = 0; i < plan->mount_count; i++) {
    const qa_content_mount *mount = &plan->mounts[i];
    const qa_mount_identity *identity = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive.identity : &mount->as.loose.identity;
    if (strcmp(identity->id, id) == 0) return identity;
  }
  return NULL;
}
qa_app_source_script_reader qa_app_source_script_reader_create(const qa_content_catalog *catalog,
    qa_content_mounted *mounts, const char *source) {
  source_script_reader *state = calloc(1, sizeof *state);
  if (state == NULL) qa_range_error("Out of memory");
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, source);
  for (;;) {
    for (size_t i = 0; i < state->owner_count; i++)
      if (strcmp(state->owners[i], product->id) == 0) {
        for (size_t j = 0; j < state->owner_count; j++) free(state->owners[j]);
        free(state->owners); free(state);
        qa_raise("Error", 0, 0, "Cyclic configuration source dependency: %s", source);
      }
    char **owners = realloc(state->owners, (state->owner_count + 1) * sizeof *owners);
    if (owners == NULL) qa_range_error("Out of memory");
    state->owners = owners;
    state->owners[state->owner_count] = strdup(product->id);
    if (state->owners[state->owner_count] == NULL) qa_range_error("Out of memory");
    state->owner_count++;
    if (product->expectation.base_product == NULL) break;
    product = qa_content_catalog_product(catalog, product->expectation.base_product);
  }
  const qa_resolved_mount_plan *plan = qa_content_mounted_plan(mounts);
  qa_mount_id *order = calloc(plan->default_order_count + 1, sizeof *order);
  if (order == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  /* owners.flatMap(owner => defaultOrder.filter(id => byMount.get(id)?.identity.content === owner)) */
  for (size_t o = 0; o < state->owner_count; o++)
    for (size_t i = 0; i < plan->default_order_count; i++) {
      const qa_mount_identity *identity = plan_mount_identity(plan, plan->default_order[i]);
      if (identity != NULL && strcmp(identity->content, state->owners[o]) == 0) order[count++] = plan->default_order[i];
    }
  size_t primary = count;
  for (size_t i = 0; i < plan->default_order_count; i++) {
    bool ordered = false;
    for (size_t j = 0; j < primary; j++) if (strcmp(order[j], plan->default_order[i]) == 0) { ordered = true; break; }
    if (!ordered) order[count++] = plan->default_order[i];
  }
  /* createMountPlanId("configuration", Buffer.from(`${plan.id}/${source}`).toString("hex")) */
  size_t text_length = strlen(plan->id) + 1 + strlen(source);
  char *hex = malloc(text_length * 2 + 1);
  if (hex == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (const char *p = plan->id; *p != '\0'; p++, at += 2) (void)snprintf(hex + at, 3, "%02x", (unsigned char)*p);
  (void)snprintf(hex + at, 3, "%02x", (unsigned char)'/'); at += 2;
  for (const char *p = source; *p != '\0'; p++, at += 2) (void)snprintf(hex + at, 3, "%02x", (unsigned char)*p);
  hex[at] = '\0';
  char *id = qa_create_mount_plan_id("configuration", hex);
  free(hex);
  if (id == NULL) qa_range_error("Out of memory");
  state->plan_id = id;
  state->reader = qa_content_mounted_borrow_ordered_reader(mounts, id, order, count, NULL, 0);
  free((void *)order);
  if (state->reader == NULL) qa_range_error("Out of memory");
  qa_app_source_script_reader reader = { state, source_script_open };
  return reader;
}
void qa_app_source_script_reader_destroy(qa_app_source_script_reader reader) {
  source_script_reader *state = reader.ctx;
  if (state == NULL) return;
  qa_mounted_content_reader_destroy(state->reader);
  free(state->plan_id);
  for (size_t i = 0; i < state->owner_count; i++) free(state->owners[i]);
  free(state->owners);
  free(state);
}
