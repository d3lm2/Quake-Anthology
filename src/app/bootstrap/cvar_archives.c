/* Port of src/app/bootstrap/cvar-archives.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/cvar_archives.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "qa/platform/files/contained.h"
#include <sys/stat.h>
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
/* encodeURIComponent for one owner part. */
static char *encode_part(const char *part) {
  size_t cap = strlen(part) * 3 + 1, out = 0;
  char *encoded = malloc(cap);
  for (const unsigned char *p = (const unsigned char *)part; *p; p++) {
    unsigned char c = *p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')')
      encoded[out++] = (char)c;
    else out += (size_t)snprintf(encoded + out, cap - out, "%%%02X", c);
  }
  encoded[out] = '\0';
  return encoded;
}
static char *archive_path(const char *const *owner, size_t count) {
  size_t cap = 64;
  for (size_t i = 0; i < count; i++) cap += strlen(owner[i]) * 3 + 1;
  char *path = malloc(cap);
  size_t out = (size_t)snprintf(path, cap, "cvars/");
  for (size_t i = 0; i < count; i++) {
    char *part = encode_part(owner[i]);
    out += (size_t)snprintf(path + out, cap - out, "%s%s", i ? "/" : "", part);
    free(part);
  }
  snprintf(path + out, cap - out, ".json");
  return path;
}
static const char *dialect_text(qa_command_dialect dialect) {
  switch (dialect) {
    case QA_COMMAND_DIALECT_Q1_NETQUAKE: return "q1-netquake";
    case QA_COMMAND_DIALECT_Q1_QUAKEWORLD: return "q1-quakeworld";
    case QA_COMMAND_DIALECT_Q2_CLASSIC: return "q2-classic";
    case QA_COMMAND_DIALECT_Q2_RERELEASE: return "q2-rerelease";
    case QA_COMMAND_DIALECT_Q3: return "q3";
  }
  return "q3";
}
static char *console_text(const char *utf8) {
  char *bytes = NULL;
  if (qa_command_text_from_utf8(utf8, &bytes)) return bytes;
  char *copy = dup(utf8);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
typedef struct archive_read { const qa_save_value *value; const char *dialect_name; qa_cvar_archive_entry *entries; size_t count; } archive_read;
static void free_partial_entries(void *ctx) {
  archive_read *read = ctx;
  qa_app_cvar_archive_entries_free(read->entries, read->count);
  read->entries = NULL; read->count = 0;
}
static void destroy_reader(void *reader) { qa_save_reader_destroy(reader); }
static void read_archive(void *ctx) {
  archive_read *read = ctx;
  qa_save_reader reader;
  qa_save_reader_init(&reader, read->value, "cvar archive");
  qa_cleanup reader_guard;
  qa_cleanup_push(&reader_guard, destroy_reader, &reader);
  qa_save_reader field_reader;
  qa_save_reader_field(&reader, "version", &field_reader);
  qa_save_reader_literal_number(&field_reader, 1);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&reader, "dialect", &field_reader);
  qa_save_reader_literal_string(&field_reader, read->dialect_name);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&reader, "entries", &field_reader);
  qa_cleanup field_guard;
  qa_cleanup_push(&field_guard, destroy_reader, &field_reader);
  size_t count = qa_save_reader_list_count(&field_reader);
  read->entries = calloc(count ? count : 1, sizeof *read->entries);
  if (read->entries == NULL) qa_range_error("Out of memory");
  qa_cleanup entries_guard;
  qa_cleanup_push(&entries_guard, free_partial_entries, read);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, name_reader, value_reader;
    qa_save_reader_item(&field_reader, i, &item);
    qa_save_reader_field(&item, "name", &name_reader);
    const char *name = qa_save_reader_string(&name_reader);
    qa_save_reader_field(&item, "value", &value_reader);
    const char *text = qa_save_reader_string(&value_reader);
    /* JSON text is UTF-8; a cvar holds console text, one byte per code unit (what TS's string holds when the value
     * came from a script, the console or the native file). A value above U+00FF stays UTF-8. */
    read->entries[i].name = console_text(name);
    read->entries[i].value = console_text(text);
    read->count = i + 1;
    qa_save_reader_destroy(&value_reader);
    qa_save_reader_destroy(&name_reader);
    qa_save_reader_destroy(&item);
  }
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (strcmp(read->entries[i].name, read->entries[j].name) == 0) qa_save_reader_fail(&reader, "duplicate archived cvar");
  qa_cleanup_pop(&entries_guard);
  qa_cleanup_pop(&field_guard);
  qa_save_reader_destroy(&field_reader);
  qa_cleanup_pop(&reader_guard);
  qa_save_reader_destroy(&reader);
}
qa_cvar_archive_entry *qa_app_load_cvar_archive(const qa_config_store *store, const char *const *owner,
                                                size_t owner_count, qa_command_dialect dialect, size_t *out_count) {
  *out_count = 0;
  char *path = archive_path(owner, owner_count);
  size_t length = 0;
  qa_cleanup path_guard; /* loadText rejects for a file it cannot open */
  qa_cleanup_push(&path_guard, free, path);
  char *text = qa_config_store_load_text(store, path, &length);
  qa_cleanup_pop(&path_guard);
  free(path);
  if (!text) return NULL;
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, true);
  free(text);
  if (!value) return NULL; /* non-JSON restores nothing */
  archive_read read = {value, dialect_text(dialect), NULL, 0};
  qa_caught_error caught;
  bool ok = qa_try(read_archive, &read, &caught);
  qa_save_value_free(value);
  /* A malformed archive restores nothing; the registry's own defaults apply. */
  if (!ok) return NULL;
  *out_count = read.count;
  return read.entries;
}
void qa_app_cvar_archive_entries_free(qa_cvar_archive_entry *entries, size_t count) {
  if (!entries) return;
  for (size_t i = 0; i < count; i++) { free(entries[i].name); free(entries[i].value); }
  free(entries);
}
/* A cvar holds console text (one byte per code unit); a JSON archive value the console cannot hold (a character
 * above U+00FF, or U+0000 decoded as C0 80) stays the UTF-8 the archive held (console_text above). That fallback is
 * what qa_command_text_from_utf8 refuses and is well-formed UTF-8 with C0 80 for U+0000 and three-byte surrogates;
 * anything else is console text. */
static bool fallback_utf8(const char *text) {
  bool high = false;
  for (const unsigned char *p = (const unsigned char *)text; *p;) {
    unsigned char c = *p;
    if (c < 0x80) { p++; continue; }
    high = true;
    size_t need;
    if (c == 0xc0 && p[1] == 0x80) { p += 2; continue; }
    if (c >= 0xc2 && c <= 0xdf) need = 1;
    else if (c >= 0xe0 && c <= 0xef) need = 2;
    else if (c >= 0xf0 && c <= 0xf4) need = 3;
    else return false;
    for (size_t k = 1; k <= need; k++) if ((p[k] & 0xc0) != 0x80) return false;
    if (c == 0xe0 && p[1] < 0xa0) return false;
    if (c == 0xf0 && p[1] < 0x90) return false;
    if (c == 0xf4 && p[1] > 0x8f) return false;
    p += need + 1;
  }
  if (!high) return false;
  char *bytes = NULL;
  if (qa_command_text_from_utf8(text, &bytes)) { free(bytes); return false; }
  return true;
}
static unsigned surrogate_unit(const unsigned char *p) {
  if (p[0] != 0xed || p[1] < 0xa0 || p[1] > 0xbf || (p[2] & 0xc0) != 0x80) return 0;
  return 0xd000u | ((unsigned)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
}
char *qa_app_cvar_json_string(const char *value) {
  char *text = fallback_utf8(value) ? NULL : qa_command_text_to_utf8(value);
  const unsigned char *at = (const unsigned char *)(text != NULL ? text : value);
  size_t length = strlen((const char *)at), used = 0;
  char *out = malloc(length * 6 + 3);
  if (out == NULL) { free(text); qa_range_error("Out of memory"); }
  static const char hex[] = "0123456789abcdef";
  out[used++] = '"';
  for (const unsigned char *p = at; *p;) {
    unsigned char c = *p;
    unsigned unit = surrogate_unit(p);
    if (unit != 0) {
      unsigned low = unit <= 0xdbff ? surrogate_unit(p + 3) : 0;
      if (low >= 0xdc00) {
        uint32_t cp = 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u);
        out[used++] = (char)(0xf0 | (cp >> 18)); out[used++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        out[used++] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[used++] = (char)(0x80 | (cp & 0x3f));
        p += 6;
      } else {
        out[used++] = '\\'; out[used++] = 'u';
        out[used++] = hex[unit >> 12]; out[used++] = hex[(unit >> 8) & 15]; out[used++] = hex[(unit >> 4) & 15]; out[used++] = hex[unit & 15];
        p += 3;
      }
      continue;
    }
    if (c == 0xc0 && p[1] == 0x80) { c = 0; p++; } /* the fallback's U+0000 */
    if (c == '"' || c == '\\') { out[used++] = '\\'; out[used++] = (char)c; }
    else if (c == '\n') { out[used++] = '\\'; out[used++] = 'n'; }
    else if (c == '\r') { out[used++] = '\\'; out[used++] = 'r'; }
    else if (c == '\t') { out[used++] = '\\'; out[used++] = 't'; }
    else if (c == '\b') { out[used++] = '\\'; out[used++] = 'b'; }
    else if (c == '\f') { out[used++] = '\\'; out[used++] = 'f'; }
    else if (c < 0x20) { out[used++] = '\\'; out[used++] = 'u'; out[used++] = '0'; out[used++] = '0'; out[used++] = hex[c >> 4]; out[used++] = hex[c & 15]; }
    else out[used++] = (char)c;
    p++;
  }
  out[used++] = '"';
  out[used] = '\0';
  free(text);
  return out;
}
/* JSON.stringify for strings (archive names/values) as TS's store.dump writes them (qa_app_cvar_json_string). */
static void append_json_string(char **buf, size_t *len, size_t *cap, const char *console) {
  char *quoted = qa_app_cvar_json_string(console);
  size_t length = strlen(quoted);
  if (*len + length + 1 > *cap) {
    size_t next = (*len + length + 1) * 2;
    char *grown = realloc(*buf, next);
    if (grown == NULL) { free(quoted); qa_range_error("Out of memory"); }
    *buf = grown; *cap = next;
  }
  memcpy(*buf + *len, quoted, length + 1);
  *len += length;
  free(quoted);
}
static void append_text(char **buf, size_t *len, size_t *cap, const char *text) {
  size_t need = *len + strlen(text) + 1;
  if (need > *cap) {
    char *grown = realloc(*buf, need * 2);
    if (grown == NULL) qa_range_error("Out of memory");
    *cap = need * 2; *buf = grown;
  }
  memcpy(*buf + *len, text, strlen(text) + 1);
  *len += strlen(text);
}
void qa_app_save_cvar_archive(const qa_config_store *store, const char *const *owner, size_t owner_count,
                              qa_cvar_registry *registry, const qa_cvar_archive_entry *entries, size_t entry_count) {
  qa_cvar_archive_entry *owned = NULL;
  if (!entries) {
    owned = qa_cvar_registry_archive_entries(registry, NULL, NULL, &entry_count);
    entries = owned;
  }
  char *buf = malloc(256); size_t len = 0, cap = 256;
  if (buf == NULL) qa_range_error("Out of memory");
  buf[0] = '\0';
  append_text(&buf, &len, &cap, "{\"version\":1,\"dialect\":");
  append_json_string(&buf, &len, &cap, dialect_text(qa_cvar_registry_dialect(registry)));
  append_text(&buf, &len, &cap, ",\"entries\":[");
  for (size_t i = 0; i < entry_count; i++) {
    if (i) append_text(&buf, &len, &cap, ",");
    append_text(&buf, &len, &cap, "{\"name\":");
    append_json_string(&buf, &len, &cap, entries[i].name);
    append_text(&buf, &len, &cap, ",\"value\":");
    append_json_string(&buf, &len, &cap, entries[i].value);
    append_text(&buf, &len, &cap, "}");
  }
  append_text(&buf, &len, &cap, "]}\n");
  if (owned) qa_app_cvar_archive_entries_free(owned, entry_count);
  char *path = archive_path(owner, owner_count);
  qa_cleanup path_guard, buf_guard;
  qa_cleanup_push(&buf_guard, free, buf);
  qa_cleanup_push(&path_guard, free, path);
  /* TS `await store.dump(...)`: a failed write rejects. */
  qa_app_config_store_dump(store, path, buf);
  qa_cleanup_pop(&path_guard);
  qa_cleanup_pop(&buf_guard);
  free(path);
  free(buf);
}
/* Node's mkdir(path, {recursive: true}) as Bun runs it: ENOENT walks to the parent and retries; an existing
 * non-directory is EEXIST; any other failure is reported at the directory where it happened. 0 or the errno, with
 * *failed the malloc'd path the error names. */
int qa_app_mkdir_recursive(const char *path, char **failed) {
  if (mkdir(path, 0777) == 0) return 0;
  int code = errno;
  if (code == ENOENT) {
    const char *slash = strrchr(path, '/');
    if (slash != NULL && slash != path) {
      size_t parent_length = (size_t)(slash - path);
      char *parent = malloc(parent_length + 1);
      if (parent == NULL) qa_range_error("Out of memory");
      memcpy(parent, path, parent_length);
      parent[parent_length] = '\0';
      int parent_code = qa_app_mkdir_recursive(parent, failed);
      free(parent);
      if (parent_code != 0) return parent_code;
      if (mkdir(path, 0777) == 0) return 0;
      code = errno;
    }
  }
  if (code == EEXIST) {
    struct stat info;
    if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) return 0;
  }
  *failed = dup(path);
  if (*failed == NULL) qa_range_error("Out of memory");
  return code;
}
static void free_text_slot(void *slot) { free(*(char **)slot); }
void qa_app_config_store_dump(const qa_config_store *store, const char *name, const char *contents) {
  /* settingsPath raises TS's "Settings path escapes the writable directory" */
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) qa_range_error("Out of memory");
  qa_cleanup path_guard;
  qa_cleanup_push(&path_guard, free_text_slot, &path);
  /* writeAtomic: mkdir(dirname(path), { recursive: true }) rejects with Node's error for the directory. */
  const char *slash = strrchr(path, '/');
  if (slash != NULL && slash != path) {
    path[slash - path] = '\0';
    char *failed = NULL;
    int code = qa_app_mkdir_recursive(path, &failed);
    if (code != 0) {
      free(path);
      path = failed;
      qa_node_fs_error(code, "mkdir", failed);
    }
    path[slash - path] = '/';
  }
  qa_cleanup_pop(&path_guard);
  free(path);
  /* The later steps write a temporary named by a random UUID in TS, so their Node message cannot be reproduced;
   * the failure still rejects with Error. */
  errno = 0;
  if (!qa_config_store_dump(store, name, contents)) {
    int code = errno;
    qa_raise("Error", code, 0, "%s: could not write %s", code != 0 ? strerror(code) : "write failed", name);
  }
}
/* JS String.prototype.trim over [start, end): returns the trimmed bounds. */
static void js_trim_bounds(const char *start, const char *end, const char **out_start, const char **out_end) {
  size_t n;
  while (start < end && (n = qa_js_white_space_length(start)) != 0 && start + n <= end) start += n;
  for (;;) {
    const char *p = start, *last = NULL;
    /* find the last white-space character that ends exactly at `end` */
    while (p < end) {
      size_t w = qa_js_white_space_length(p);
      if (w != 0 && p + w == end) { last = p; break; }
      p += w != 0 ? w : 1;
    }
    if (last == NULL) break;
    end = last;
  }
  *out_start = start; *out_end = end;
}
static bool archive_command(const char *first, size_t length) {
  static const char *const names[] = {"seta", "set", "sets", "setu", "bind", "unbind", "unbindall"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    if (strlen(names[i]) != length) continue;
    bool same = true;
    for (size_t j = 0; j < length && same; j++) same = tolower((unsigned char)first[j]) == names[i][j];
    if (same) return true;
  }
  return false;
}
/* /^[^\s";]+ +"[^"]*"$/ over the trimmed line. */
static bool bare_variable_shape(const char *start, const char *end) {
  const char *p = start;
  while (p < end && *p != '"' && *p != ';' && qa_js_white_space_length(p) == 0) p++;
  if (p == start || p >= end || *p != ' ') return false;
  while (p < end && *p == ' ') p++;
  if (p >= end || *p != '"') return false;
  p++;
  while (p < end && *p != '"') p++;
  return p < end && p + 1 == end;
}
char *qa_app_archive_data_text(const char *text, bool (*is_command)(void *ctx, const char *name), void *ctx) {
  size_t cap = strlen(text) + 2, length = 0;
  char *out = malloc(cap);
  if (out == NULL) qa_range_error("Out of memory");
  out[0] = '\0';
  const char *line = text;
  for (;;) {
    const char *line_end = line;
    while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') line_end++;
    const char *start, *end;
    js_trim_bounds(line, line_end, &start, &end);
    if (start < end && !(end - start >= 2 && start[0] == '/' && start[1] == '/')) {
      bool quoted = false, chained = false;
      for (const char *p = start; p < end; p++) {
        if (*p == '"') quoted = !quoted;
        else if (*p == ';' && !quoted) chained = true;
      }
      const char *first_end = start;
      while (first_end < end && qa_js_white_space_length(first_end) == 0) first_end++;
      size_t first_length = (size_t)(first_end - start);
      bool keep = !quoted && !chained;
      if (keep && !archive_command(start, first_length)) {
        /* Quake I writes a bare `name "value"` line for an archived variable. */
        bool bare = bare_variable_shape(start, end);
        if (bare) {
          char *first = malloc(first_length + 1);
          if (first == NULL) qa_range_error("Out of memory");
          memcpy(first, start, first_length);
          first[first_length] = '\0';
          bare = !is_command(ctx, first);
          free(first);
        }
        keep = bare;
      }
      if (keep) {
        memcpy(out + length, start, (size_t)(end - start));
        length += (size_t)(end - start);
        out[length++] = '\n';
        out[length] = '\0';
      }
    }
    if (*line_end == '\0') break;
    line = line_end + (line_end[0] == '\r' && line_end[1] == '\n' ? 2 : 1);
  }
  return out;
}
