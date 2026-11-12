/* Port of src/app/bootstrap/startup-saves.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_saves.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/foundation/caught_message.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/foundation/node_path.h"
#include "qa/persistence/kex_q2_save.h"
#include "qa/persistence/q2_save_folder.h"
#include "qa/persistence/q1_selection.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/saved_game.h"
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
/* TS name.toLowerCase().endsWith(suffix): Unicode lowering (U+212A is 'k' in ".pak.gz"). */
static bool ends_ci(const char *s, const char *suffix) { return qa_js_ends_with_ignore_case(s, suffix); }
static char *join_path(const char *dir, const char *name) {
  size_t a = strlen(dir), b = strlen(name);
  char *r = malloc(a + 1 + b + 1);
  if (!r) qa_range_error("Out of memory");
  snprintf(r, a + 1 + b + 1, "%s/%s", dir, name);
  return r;
}
/* Bun.file(path).exists(): true for an existing path that is not a directory (stat follows links). */
static bool bun_file_exists(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && !S_ISDIR(info.st_mode);
}
/* Dirent isDirectory(): the entry itself, not a link target. */
static bool entry_is_directory(const char *path) {
  struct stat info;
  return lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
}
char *qa_app_resolve_external_save_path(const char *const *directories, size_t directory_count, const char *name) {
  char *filename;
  if (ends_ci(name, ".sav")) filename = xdup(name);
  else {
    size_t length = strlen(name);
    filename = malloc(length + 5);
    if (filename == NULL) qa_range_error("Out of memory");
    memcpy(filename, name, length);
    memcpy(filename + length, ".sav", 5);
  }
  if (strchr(filename, '/') || strchr(filename, '\\') || strcmp(filename, ".sav") == 0) { free(filename); return NULL; }
  char *found = NULL;
  for (size_t i = 0; i < directory_count && found == NULL; i++) {
    char *direct = join_path(directories[i], filename);
    if (bun_file_exists(direct)) { found = direct; break; }
    free(direct);
    /* readdir(directory, { withFileTypes: true }).catch(() => []): its entries omit "." and "..". */
    DIR *dir = opendir(directories[i]);
    if (dir == NULL) continue;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      char *subdir = join_path(directories[i], entry->d_name);
      if (!entry_is_directory(subdir)) { free(subdir); continue; }
      char *nested = join_path(subdir, filename);
      free(subdir);
      if (bun_file_exists(nested)) { found = nested; break; }
      free(nested);
    }
    closedir(dir);
  }
  free(filename);
  return found;
}
typedef struct cached_row { char *path; double mtime_ms; double size; qa_app_startup_save_row row; } cached_row;
struct qa_app_startup_saves {
  const qa_content_catalog *catalog;
  char *directory;
  char **external; size_t external_count;
  /* Stock games' own save folders, which hold saves the original names and formats; scanned to the given depth. */
  qa_app_native_save_folder *native_folders; size_t native_folder_count;
  qa_app_startup_save_list current;
  char **path_ids; char **paths; size_t path_count;
  cached_row *cache; size_t cache_count, cache_cap;
};
static void free_row(qa_app_startup_save_row *row) {
  free(row->id); free(row->label); free(row->map); free(row->game); free(row->unavailable);
}
static void free_list(qa_app_startup_save_list *list) {
  for (size_t i = 0; i < list->row_count; i++) free_row(&list->rows[i]);
  free(list->rows); free(list->error);
  memset(list, 0, sizeof *list);
}
qa_app_startup_saves *qa_app_startup_saves_create(const qa_content_catalog *catalog, const char *directory,
                                                  const char *const *external_directories, size_t external_count) {
  return qa_app_startup_saves_create_native(catalog, directory, external_directories, external_count, NULL, 0);
}
qa_app_startup_saves *qa_app_startup_saves_create_native(const qa_content_catalog *catalog, const char *directory,
    const char *const *external_directories, size_t external_count,
    const qa_app_native_save_folder *native_folders, size_t native_folder_count) {
  qa_app_startup_saves *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->catalog = catalog;
  s->directory = xdup(directory);
  s->external = calloc(external_count ? external_count : 1, sizeof *s->external);
  if (!s->external) qa_range_error("Out of memory");
  s->external_count = external_count;
  for (size_t i = 0; i < external_count; i++) s->external[i] = xdup(external_directories[i]);
  s->native_folders = calloc(native_folder_count ? native_folder_count : 1, sizeof *s->native_folders);
  if (!s->native_folders) qa_range_error("Out of memory");
  s->native_folder_count = native_folder_count;
  for (size_t i = 0; i < native_folder_count; i++) {
    s->native_folders[i] = native_folders[i];
    s->native_folders[i].root = xdup(native_folders[i].root);
  }
  return s;
}
void qa_app_startup_saves_destroy(qa_app_startup_saves *s) {
  if (!s) return;
  free_list(&s->current);
  for (size_t i = 0; i < s->path_count; i++) { free(s->path_ids[i]); free(s->paths[i]); }
  free(s->path_ids); free(s->paths);
  for (size_t i = 0; i < s->cache_count; i++) { free(s->cache[i].path); free_row(&s->cache[i].row); }
  free(s->cache);
  for (size_t i = 0; i < s->external_count; i++) free(s->external[i]);
  for (size_t i = 0; i < s->native_folder_count; i++) free(s->native_folders[i].root);
  free(s->native_folders);
  free(s->external); free(s->directory); free(s);
}
const char *qa_app_startup_saves_directory(const qa_app_startup_saves *s) { return s->directory; }
const qa_content_catalog *qa_app_startup_saves_catalog(const qa_app_startup_saves *s) { return s->catalog; }
const qa_app_startup_save_list *qa_app_startup_saves_list(const qa_app_startup_saves *s) { return &s->current; }
const char *qa_app_startup_saves_path(qa_app_startup_saves *s, const char *id) {
  const qa_app_startup_save_row *row = NULL;
  const char *path = NULL;
  /* rows.find takes the first row with the id; the paths Map keeps the last path set for it. */
  for (size_t i = 0; i < s->current.row_count && row == NULL; i++) if (strcmp(s->current.rows[i].id, id) == 0) row = &s->current.rows[i];
  for (size_t i = 0; i < s->path_count; i++) if (strcmp(s->path_ids[i], id) == 0) path = s->paths[i];
  if (row == NULL || path == NULL) qa_raise("Error", 0, 0, "This saved game is no longer listed. Refresh the saved games.");
  if (row->unavailable != NULL) qa_raise("Error", 0, 0, "%s", row->unavailable);
  return path;
}
char *qa_app_startup_saves_named_path(qa_app_startup_saves *s, const char *name) {
  return qa_app_startup_saves_named_path_native(s, name, NULL);
}
/* /[\p{L}\p{N}]/u for one code point, from the table bun generates. */
static bool is_letter_or_number(uint32_t code) {
  static const struct {
    uint32_t first, last;
  } ranges[] = {
#include "startup_saves_letters.inc"
  };
  size_t lo = 0, hi = sizeof ranges / sizeof *ranges;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (code < ranges[mid].first) hi = mid;
    else if (code > ranges[mid].last) lo = mid + 1;
    else return true;
  }
  return false;
}
/* One strict UTF-8 code point; false for an invalid sequence (a lone surrogate or malformed
 * bytes, which /[\p{L}\p{N} _-]/u never matches either). */
static bool next_code_point(const unsigned char **cursor, uint32_t *out) {
  const unsigned char *p = *cursor;
  unsigned char lead = p[0];
  size_t extra = lead < 0x80 ? 0 : (lead & 0xe0) == 0xc0 ? 1 : (lead & 0xf0) == 0xe0 ? 2 : (lead & 0xf8) == 0xf0 ? 3 : 4;
  if (extra == 4) return false;
  uint32_t code = extra == 0 ? lead : extra == 1 ? lead & 0x1fu : extra == 2 ? lead & 0x0fu : lead & 0x07u;
  for (size_t i = 1; i <= extra; i++) {
    if ((p[i] & 0xc0) != 0x80) return false;
    code = code << 6 | (p[i] & 0x3fu);
  }
  static const uint32_t minimum[4] = {0, 0x80, 0x800, 0x10000};
  if (code < minimum[extra] || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
  *cursor = p + extra + 1;
  *out = code;
  return true;
}
/* /^[\p{L}\p{N} _-]{1,48}$/u over UTF-8 text: 1 to 48 code points. */
static bool valid_save_label(const char *label) {
  const unsigned char *cursor = (const unsigned char *)label;
  size_t count = 0;
  while (*cursor != '\0') {
    uint32_t code;
    if (!next_code_point(&cursor, &code)) return false;
    if (!(code == ' ' || code == '_' || code == '-' || is_letter_or_number(code))) return false;
    if (++count > 48) return false;
  }
  return count >= 1;
}
char *qa_app_startup_saves_named_path_native(qa_app_startup_saves *s, const char *name, const qa_native_save_layout *native) {
  char *label = qa_input_js_trim(name);
  if (!label) qa_range_error("Out of memory");
  if (!valid_save_label(label)) { free(label); qa_raise("Error", 0, 0, "Use 1-48 letters, numbers, spaces, - or _."); }
  /* const path = native === null ? join(this.directory, `${label}.sav`) : native.locate(label) */
  char *path;
  if (native == NULL) {
    size_t length = strlen(label);
    char *file = malloc(length + 5);
    if (file == NULL) { free(label); qa_range_error("Out of memory"); }
    memcpy(file, label, length);
    memcpy(file + length, ".sav", 5);
    qa_cleanup file_guard[2];
    qa_cleanup_push(&file_guard[0], free, label);
    qa_cleanup_push(&file_guard[1], free, file);
    path = join_path(s->directory, file);
    qa_cleanup_pop(&file_guard[1]);
    qa_cleanup_pop(&file_guard[0]);
    free(file);
  } else {
    qa_cleanup label_guard;
    qa_cleanup_push(&label_guard, free, label);
    path = qa_native_save_layout_locate(native, label);
    qa_cleanup_pop(&label_guard);
  }
  /* mkdir(dirname(path), { recursive: true }): a failure raises the Node error text. */
  char *directory = qa_node_path_dirname(path);
  if (directory == NULL) qa_range_error("Out of memory");
  char *walk = xdup(directory);
  size_t walk_length = strlen(walk);
  for (size_t i = 1; i <= walk_length; i++) {
    if (walk[i] != '/' && walk[i] != '\0') continue;
    char saved = walk[i];
    walk[i] = '\0';
    struct stat info;
    if (stat(walk, &info) != 0 && mkdir(walk, 0777) != 0 && errno != EEXIST) {
      int error = errno;
      qa_cleanup guards[4];
      qa_cleanup_push(&guards[0], free, walk); qa_cleanup_push(&guards[1], free, label);
      qa_cleanup_push(&guards[2], free, path); qa_cleanup_push(&guards[3], free, directory);
      qa_raise("Error", 0, 0, "%s: %s, mkdir '%s'", error == EACCES ? "EACCES" : error == EROFS ? "EROFS" : "EIO",
               error == EACCES ? "permission denied" : error == EROFS ? "read-only file system" : strerror(error), directory);
    }
    walk[i] = saved;
  }
  free(walk);
  free(directory);
  /* for (const taken of native === null ? [path] : [path, native.originalPath(label)]) */
  char *original = native != NULL ? qa_native_save_layout_original_path(native, label) : NULL;
  free(label);
  bool exists = bun_file_exists(path) || (original != NULL && bun_file_exists(original));
  free(original);
  if (exists) { free(path); qa_raise("Error", 0, 0, "Name exists. Select its slot to overwrite."); }
  return path;
}
typedef struct found_file { char *path; char *id; } found_file;
/* The scans' `collect`: seen.add(path); files.push({ path, id: prefix + relative(root, path) }). Takes `path`. */
static void collect_file(char *path, const char *root, const char *prefix,
                         found_file **files, size_t *count, size_t *cap, char ***seen, size_t *seen_count, size_t *seen_cap) {
  if (*seen_count == *seen_cap) { *seen_cap = *seen_cap ? *seen_cap * 2 : 16; *seen = realloc(*seen, *seen_cap * sizeof **seen); if (!*seen) qa_range_error("Out of memory"); }
  (*seen)[(*seen_count)++] = xdup(path);
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 16; *files = realloc(*files, *cap * sizeof **files); if (!*files) qa_range_error("Out of memory"); }
  char *relative = qa_node_path_relative(root, path);
  if (relative == NULL) qa_range_error("Out of memory");
  char *id;
  if (prefix != NULL) {
    size_t need = strlen(prefix) + strlen(relative) + 1;
    id = malloc(need);
    if (!id) qa_range_error("Out of memory");
    snprintf(id, need, "%s%s", prefix, relative);
    free(relative);
  } else id = relative;
  (*files)[*count].path = path;
  (*files)[*count].id = id;
  (*count)++;
}
/* `depth` < 0 is the TS Infinity. */
static void scan_dir(qa_app_startup_saves *s, const char *directory, const char *root, const char *external_prefix, int depth,
                     found_file **files, size_t *count, size_t *cap, char ***seen, size_t *seen_count, size_t *seen_cap, bool *error) {
  DIR *dir = opendir(directory);
  if (dir == NULL) {
    if (errno == ENOENT) return;
    *error = true;
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char *path = join_path(directory, entry->d_name);
    /* Dirent isDirectory()/isFile() do not follow symbolic links. */
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
      if (depth != 0)
        scan_dir(s, path, root, external_prefix, depth < 0 ? depth : depth - 1, files, count, cap, seen, seen_count, seen_cap, error);
      free(path);
      continue;
    }
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || !(ends_ci(entry->d_name, ".sav") || ends_ci(entry->d_name, ".pak.gz"))) {
      free(path);
      continue;
    }
    bool dup = false;
    for (size_t i = 0; i < *seen_count; i++) if (strcmp((*seen)[i], path) == 0) dup = true;
    bool skip_pak = external_prefix == NULL && ends_ci(path, ".pak.gz");
    if (dup || skip_pak) { free(path); continue; }
    collect_file(path, root, external_prefix, files, count, cap, seen, seen_count, seen_cap);
  }
  closedir(dir);
  (void)s;
}
/* TS scanSlotFolders: Quake II keeps one folder per save. Each is listed once: by its `server.ssv` when the original
 * format is there, else by the engine image. */
static void scan_slot_folders(const char *root, const char *prefix,
                              found_file **files, size_t *count, size_t *cap, char ***seen, size_t *seen_count, size_t *seen_cap, bool *error) {
  DIR *dir = opendir(root);
  if (dir == NULL) {
    if (errno != ENOENT) *error = true;
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char *folder = join_path(root, entry->d_name);
    struct stat st;
    if (lstat(folder, &st) != 0 || !S_ISDIR(st.st_mode) || qa_js_equals_ignore_case(entry->d_name, "current")) { free(folder); continue; }
    DIR *slot = opendir(folder);
    if (slot == NULL) {
      /* The readdir of a slot folder throws into the same catch: a missing folder ends the scan quietly. */
      bool missing = errno == ENOENT;
      free(folder);
      closedir(dir);
      if (!missing) *error = true;
      return;
    }
    /* names.find(server.ssv) ?? names.find(qts.sav), each the first in directory order */
    char *server = NULL, *engine = NULL;
    struct dirent *file;
    while ((file = readdir(slot)) != NULL) {
      if (server == NULL && qa_js_equals_ignore_case(file->d_name, "server.ssv")) server = xdup(file->d_name);
      else if (engine == NULL && qa_js_equals_ignore_case(file->d_name, "qts.sav")) engine = xdup(file->d_name);
    }
    closedir(slot);
    const char *handle = server != NULL ? server : engine;
    if (handle != NULL) {
      char *path = join_path(folder, handle);
      bool dup = false;
      for (size_t i = 0; i < *seen_count; i++) if (strcmp((*seen)[i], path) == 0) dup = true;
      if (dup) free(path);
      else collect_file(path, root, prefix, files, count, cap, seen, seen_count, seen_cap);
    }
    free(server); free(engine); free(folder);
  }
  closedir(dir);
}
static const char *base_name(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}
static int row_compare(const void *left, const void *right) {
  const qa_app_startup_save_row *a = left, *b = right;
  if (a->saved_at_milliseconds != b->saved_at_milliseconds) return a->saved_at_milliseconds > b->saved_at_milliseconds ? -1 : 1;
  return qa_js_locale_compare(a->id, b->id);
}

typedef struct save_detail { char *label, *map, *game; double saved_at_milliseconds; char *unavailable; } save_detail;
static void set_text(char **slot, const char *text) { char *next = text != NULL ? xdup(text) : NULL; free(*slot); *slot = next; }
/* The Node fs error message for a failed open/stat. */
static void raise_io(int error, const char *call, const char *path) {
  if (error == ENOENT) qa_raise("Error", 0, 0, "ENOENT: no such file or directory, %s '%s'", call, path);
  if (error == EACCES) qa_raise("Error", 0, 0, "EACCES: permission denied, %s '%s'", call, path);
  if (error == EISDIR) qa_raise("Error", 0, 0, "EISDIR: illegal operation on a directory, read");
  qa_raise("Error", 0, 0, "%s, %s '%s'", error != 0 ? strerror(error) : "read failed", call, path);
}
static double stat_mtime_ms(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) raise_io(errno, "stat", path);
  return (double)st.st_mtime * 1000.0 + (double)st.st_mtim.tv_nsec / 1e6;
}
static char *game_label(const qa_catalog_discovered_product *product) {
  size_t length = strlen(product->expectation.title) + strlen(product->expectation.edition) + 4;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, length, "%s (%s)", product->expectation.title, product->expectation.edition);
  return out;
}
typedef struct inspect_job { const qa_content_catalog *catalog; const char *path; save_detail *detail; } inspect_job;
static void kex_release(void *save) { qa_kex_q2_save_archive_free(save); }
/* TS refresh, the .pak.gz try block. */
static void inspect_pak(void *ctx) {
  inspect_job *job = ctx;
  save_detail *detail = job->detail;
  detail->saved_at_milliseconds = stat_mtime_ms(job->path);
  qa_kex_q2_save_archive save;
  memset(&save, 0, sizeof save);
  errno = 0;
  if (!qa_read_kex_q2_save_archive(job->path, &save)) raise_io(errno, "open", job->path);
  qa_cleanup guard;
  qa_cleanup_push(&guard, kex_release, &save);
  const qa_kex_q2_save_info *info = &save.info;
  if (info->comment != NULL && info->comment[0] != '\0') set_text(&detail->label, info->comment);
  set_text(&detail->map, info->map_command != NULL ? info->map_command : "");
  if (info->timestamp > 0 && info->timestamp < 253402300800ULL) detail->saved_at_milliseconds = (double)info->timestamp * 1000;
  const char *wanted = info->game_directory != NULL ? info->game_directory : "";
  const qa_catalog_discovered_product *product = NULL;
  for (size_t i = 0; i < job->catalog->product_count && product == NULL; i++) {
    const qa_catalog_discovered_product *candidate = &job->catalog->products[i];
    if (candidate->expectation.family != QA_GAME_FAMILY_Q2 || strcmp(candidate->expectation.edition, "rerelease") != 0) continue;
    const char *base = strrchr(candidate->expectation.content_directory, '/');
    base = base != NULL ? base + 1 : candidate->expectation.content_directory;
    if (qa_js_equals_ignore_case(base, wanted)) product = candidate;
  }
  if (product != NULL) { char *game = game_label(product); free(detail->game); detail->game = game; }
  if (product == NULL || product->availability.kind != QA_CATALOG_INSTALLED)
    set_text(&detail->unavailable, "Required game content is not installed.");
  qa_cleanup_pop(&guard);
  qa_kex_q2_save_archive_free(&save);
}
/* map.replace(/^maps\//, "").replace(/\.bsp$/, "") */
static char *short_map(const char *map) {
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  size_t length = strlen(map);
  if (length >= 4 && strcmp(map + length - 4, ".bsp") == 0) length -= 4;
  char *out = malloc(length + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, map, length);
  out[length] = '\0';
  return out;
}
static void header_release(void *header) { qa_save_header_result_free(header); }
static void saved_release(void *save) { qa_saved_game_free(save); }
/* TS refresh, the .sav try block. Returns through detail; header_row is set when readSaveHeader answered. */
typedef struct sav_job { inspect_job base; bool header_row; } sav_job;
static void inspect_sav(void *ctx) {
  sav_job *job = ctx;
  save_detail *detail = job->base.detail;
  const char *path = job->base.path;
  detail->saved_at_milliseconds = stat_mtime_ms(path);
  qa_save_header_result header;
  memset(&header, 0, sizeof header);
  errno = 0;
  if (!qa_read_save_header(path, &header)) raise_io(errno, "open", path);
  qa_cleanup header_guard;
  qa_cleanup_push(&header_guard, header_release, &header);
  if (header.present) {
    const qa_catalog_discovered_product *product = qa_content_catalog_product(job->base.catalog, header.header.content);
    char *map = short_map(header.header.map);
    free(detail->map); detail->map = map;
    char *game = game_label(product);
    free(detail->game); detail->game = game;
    if (product->availability.kind != QA_CATALOG_INSTALLED) set_text(&detail->unavailable, "Required game content is not installed.");
    job->header_row = true;
    qa_cleanup_pop(&header_guard);
    qa_save_header_result_free(&header);
    return;
  }
  qa_cleanup_pop(&header_guard);
  qa_save_header_result_free(&header);
  qa_saved_game save;
  memset(&save, 0, sizeof save);
  errno = 0;
  if (!qa_read_saved_game(path, &save)) raise_io(errno, "open", path);
  qa_cleanup save_guard;
  qa_cleanup_push(&save_guard, saved_release, &save);
  const qa_catalog_discovered_product *product = save.kind == QA_SAVED_GAME_SHARED
      ? qa_content_catalog_product(job->base.catalog, save.image.recipe.map.entities.content)
      : qa_select_q1_save_product_in_catalog(job->base.catalog, &save.data, path, NULL);
  char *map = save.kind == QA_SAVED_GAME_SHARED ? short_map(save.image.recipe.map.geometry.requested_path)
                                                : xdup(save.data.map != NULL ? save.data.map : "");
  free(detail->map); detail->map = map;
  char *game = game_label(product);
  free(detail->game); detail->game = game;
  if (save.kind == QA_SAVED_GAME_SHARED) {
    qa_app_sim_saved_settings settings = qa_app_sim_saved_simulation_settings(save.image);
    free(settings.client_slots);
    qa_save_value_free(settings.value);
  }
  if (product->availability.kind != QA_CATALOG_INSTALLED) set_text(&detail->unavailable, "Required game content is not installed.");
  qa_cleanup_pop(&save_guard);
  qa_saved_game_free(&save);
}
static void folder_header_release(void *header) { qa_q2_classic_folder_header_free(header); }
/* TS refresh, the server.ssv try block. */
static void inspect_server(void *ctx) {
  inspect_job *job = ctx;
  save_detail *detail = job->detail;
  FILE *file = fopen(job->path, "rb");
  if (file == NULL) raise_io(errno, "open", job->path);
  uint8_t *bytes = NULL;
  size_t length = 0, capacity = 0;
  for (;;) {
    if (length == capacity) {
      capacity = capacity == 0 ? 4096 : capacity * 2;
      bytes = realloc(bytes, capacity);
      if (bytes == NULL) qa_range_error("Out of memory");
    }
    size_t got = fread(bytes + length, 1, capacity - length, file);
    if (got == 0) break;
    length += got;
  }
  fclose(file);
  qa_cleanup bytes_guard;
  qa_cleanup_push(&bytes_guard, free, bytes);
  qa_q2_classic_folder_header header;
  memset(&header, 0, sizeof header);
  qa_q2_classic_folder_header_read(bytes, length, &header);
  qa_cleanup_pop(&bytes_guard);
  free(bytes);
  qa_cleanup header_guard;
  qa_cleanup_push(&header_guard, folder_header_release, &header);
  /* label = header.comment.trim() === "" ? slot : header.comment.trim() */
  char *comment = qa_input_js_trim(header.comment != NULL ? header.comment : "");
  if (comment == NULL) qa_range_error("Out of memory");
  if (comment[0] != '\0') { free(detail->label); detail->label = comment; }
  else free(comment);
  char *map = short_map(header.map != NULL ? header.map : "");
  free(detail->map); detail->map = map;
  /* gameDirectory = basename(dirname(dirname(dirname(path)))) */
  char *slot_folder = qa_node_path_dirname(job->path);
  char *save_folder = qa_node_path_dirname(slot_folder);
  char *game_folder = qa_node_path_dirname(save_folder);
  char *game_directory = qa_node_path_basename(game_folder);
  free(slot_folder); free(save_folder); free(game_folder);
  if (game_directory == NULL) qa_range_error("Out of memory");
  const qa_catalog_discovered_product *product = NULL;
  for (size_t i = 0; i < job->catalog->product_count && product == NULL; i++) {
    const qa_catalog_discovered_product *candidate = &job->catalog->products[i];
    if (candidate->expectation.family != QA_GAME_FAMILY_Q2 || strcmp(candidate->expectation.edition, "classic") != 0) continue;
    const char *base = strrchr(candidate->expectation.content_directory, '/');
    base = base != NULL ? base + 1 : candidate->expectation.content_directory;
    if (qa_js_equals_ignore_case(base, game_directory)) product = candidate;
  }
  free(game_directory);
  if (product != NULL) { char *game = game_label(product); free(detail->game); detail->game = game; }
  if (product == NULL || product->availability.kind != QA_CATALOG_INSTALLED)
    set_text(&detail->unavailable, "Required game content is not installed.");
  qa_cleanup_pop(&header_guard);
  qa_q2_classic_folder_header_free(&header);
}
typedef enum inspect_kind { INSPECT_SAV, INSPECT_PAK, INSPECT_SERVER } inspect_kind;
static void inspect(qa_app_startup_saves *s, const char *path, inspect_kind kind, save_detail *detail) {
  sav_job job = {{s->catalog, path, detail}, false};
  qa_caught_error caught;
  if (qa_try(kind == INSPECT_PAK ? inspect_pak : kind == INSPECT_SERVER ? inspect_server : inspect_sav, &job, &caught)) return;
  set_text(&detail->unavailable, qa_caught_message(&caught));
}
void qa_app_startup_saves_refresh(qa_app_startup_saves *s) {
  found_file *files = NULL; size_t count = 0, cap = 0;
  char **seen = NULL; size_t seen_count = 0, seen_cap = 0;
  bool had_error = false;
  scan_dir(s, s->directory, s->directory, NULL, -1, &files, &count, &cap, &seen, &seen_count, &seen_cap, &had_error);
  for (size_t i = 0; i < s->external_count; i++) {
    char prefix[64];
    snprintf(prefix, sizeof prefix, "external/%zu/", i);
    scan_dir(s, s->external[i], s->external[i], prefix, -1, &files, &count, &cap, &seen, &seen_count, &seen_cap, &had_error);
  }
  for (size_t i = 0; i < s->native_folder_count; i++) {
    const qa_app_native_save_folder *folder = &s->native_folders[i];
    char prefix[64];
    snprintf(prefix, sizeof prefix, "native/%zu/", i);
    if (folder->arrangement == QA_NATIVE_SAVE_FOLDERS)
      scan_slot_folders(folder->root, prefix, &files, &count, &cap, &seen, &seen_count, &seen_cap, &had_error);
    else
      scan_dir(s, folder->root, folder->root, prefix, folder->depth, &files, &count, &cap, &seen, &seen_count, &seen_cap, &had_error);
  }
  for (size_t i = 0; i < seen_count; i++) free(seen[i]);
  free(seen);
  qa_app_startup_save_row *rows = calloc(count ? count : 1, sizeof *rows);
  char **path_ids = calloc(count ? count : 1, sizeof *path_ids);
  char **paths = calloc(count ? count : 1, sizeof *paths);
  if (!rows || !path_ids || !paths) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const char *path = files[i].path;
    const char *id = files[i].id;
    path_ids[i] = xdup(id);
    paths[i] = xdup(path);
    struct stat st;
    bool has_identity = stat(path, &st) == 0;
    double mtime_ms = has_identity ? (double)st.st_mtime * 1000.0 + (double)st.st_mtim.tv_nsec / 1e6 : 0;
    double size = has_identity ? (double)st.st_size : 0;
    cached_row *cached = NULL;
    if (has_identity) for (size_t j = 0; j < s->cache_count; j++) if (strcmp(s->cache[j].path, path) == 0) cached = &s->cache[j];
    if (cached != NULL && cached->mtime_ms == mtime_ms && cached->size == size) {
      rows[i] = cached->row;
      rows[i].id = xdup(id);
      rows[i].label = xdup(cached->row.label);
      rows[i].map = xdup(cached->row.map);
      rows[i].game = xdup(cached->row.game);
      rows[i].unavailable = cached->row.unavailable ? xdup(cached->row.unavailable) : NULL;
      continue;
    }
    save_detail detail; memset(&detail, 0, sizeof detail);
    if (ends_ci(path, ".pak.gz")) {
      /* basename(path).replace(/\.pak\.gz$/i, ""): without the u flag only ASCII letters fold, so a
       * name that ends_ci matched through a non-ASCII letter (U+212A KELVIN SIGN) keeps its suffix. */
      const char *base = base_name(path);
      size_t base_len = strlen(base);
      if (base_len >= 7 && strncasecmp(base + base_len - 7, ".pak.gz", 7) == 0) base_len -= 7;
      detail.label = qa_input_dup_range(base, base_len);
      detail.map = xdup("Unknown map");
      detail.game = xdup("Quake II (rerelease)");
      detail.saved_at_milliseconds = 0;
      inspect(s, path, INSPECT_PAK, &detail);
    } else if (qa_js_equals_ignore_case(base_name(path), "server.ssv")) {
      /* slot = basename(dirname(path)) */
      char *folder = qa_node_path_dirname(path);
      detail.label = folder != NULL ? qa_node_path_basename(folder) : NULL;
      free(folder);
      if (detail.label == NULL) qa_range_error("Out of memory");
      detail.map = xdup("Unknown map");
      detail.game = xdup("Quake II");
      detail.saved_at_milliseconds = has_identity ? mtime_ms : 0; /* identity?.mtimeMs ?? 0 */
      inspect(s, path, INSPECT_SERVER, &detail);
    } else {
      const char *base = base_name(path);
      size_t name_len = strlen(base) - 4; /* .sav */
      char *name = qa_input_dup_range(base, name_len);
      if (!name) qa_range_error("Out of memory");
      /* name.toLowerCase() === "autosave": Unicode lowering (U+212A KELVIN SIGN is "k"). */
      if (qa_js_equals_ignore_case(name, "autosave")) detail.label = xdup("Autosave");
      else if (qa_js_equals_ignore_case(name, "quicksave")) detail.label = xdup("Quicksave");
      else {
        detail.label = xdup(name);
        for (char *p = detail.label; *p; p++) if (*p == '_') *p = ' ';
      }
      free(name);
      detail.map = xdup("Unknown map");
      detail.game = xdup("Unknown game");
      detail.saved_at_milliseconds = 0;
      inspect(s, path, INSPECT_SAV, &detail);
    }
    rows[i].id = xdup(id);
    rows[i].label = detail.label;
    rows[i].map = detail.map;
    rows[i].game = detail.game;
    rows[i].saved_at_milliseconds = detail.saved_at_milliseconds;
    rows[i].unavailable = detail.unavailable;
    if (has_identity) {
      cached_row *slot = cached;
      if (slot == NULL) {
        if (s->cache_count == s->cache_cap) { s->cache_cap = s->cache_cap ? s->cache_cap * 2 : 16; s->cache = realloc(s->cache, s->cache_cap * sizeof *s->cache); if (!s->cache) qa_range_error("Out of memory"); }
        slot = &s->cache[s->cache_count++];
        memset(slot, 0, sizeof *slot);
        slot->path = xdup(path);
      } else free_row(&slot->row);
      slot->mtime_ms = mtime_ms;
      slot->size = size;
      slot->row = rows[i];
      slot->row.id = xdup(rows[i].id);
      slot->row.label = xdup(rows[i].label);
      slot->row.map = xdup(rows[i].map);
      slot->row.game = xdup(rows[i].game);
      slot->row.unavailable = rows[i].unavailable ? xdup(rows[i].unavailable) : NULL;
    }
  }
  qa_js_sort(rows, count, sizeof *rows, row_compare);
  /* paths map order need not follow rows; ids are the keys. */
  for (size_t i = 0; i < s->path_count; i++) { free(s->path_ids[i]); free(s->paths[i]); }
  free(s->path_ids); free(s->paths);
  s->path_ids = path_ids; s->paths = paths; s->path_count = count;
  free_list(&s->current);
  s->current.rows = rows;
  s->current.row_count = count;
  s->current.error = had_error ? xdup("Some saved games could not be read. Check access and refresh.") : NULL;
  for (size_t i = 0; i < count; i++) { free(files[i].path); free(files[i].id); }
  free(files);
}
