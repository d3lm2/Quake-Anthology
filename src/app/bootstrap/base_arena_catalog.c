/* Port of src/app/bootstrap/base-arena-catalog.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_catalog.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
typedef struct { char *key, *value; } field;
typedef struct { field *fields; size_t count; } info;
typedef struct { info *rows; size_t count, cap; } infos;
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static const char *get(const info *row, const char *key) {
  for (size_t i = 0; i < row->count; i++) if (strcmp(row->fields[i].key, key) == 0) return row->fields[i].value;
  return NULL;
}
static void free_infos(infos *all) {
  for (size_t i = 0; i < all->count; i++) {
    for (size_t j = 0; j < all->rows[i].count; j++) { free(all->rows[i].fields[j].key); free(all->rows[i].fields[j].value); }
    free(all->rows[i].fields);
  }
  free(all->rows);
}
/* One file's arenaInfosFromText: its rows are kept apart until the whole text parsed, so a
 * file that throws contributes none (TS pushes its result only after the call returns). */
typedef struct {
  infos out;
  info row;
  qa_common_parse_cursor cursor;
  bool cursor_live;
  char *key; /* a field name read before its value */
} parse_job;
static void free_row(info *row) {
  for (size_t j = 0; j < row->count; j++) { free(row->fields[j].key); free(row->fields[j].value); }
  free(row->fields);
  *row = (info){NULL, 0};
}
static void parse_job_free(void *arg) {
  parse_job *job = arg;
  free_row(&job->row);
  free(job->key);
  job->key = NULL;
  free_infos(&job->out);
  job->out = (infos){0};
  if (job->cursor_live) qa_common_parse_cursor_destroy(&job->cursor);
  job->cursor_live = false;
}
/* fields.set(key, value): Map.set overwrites an existing key in place. */
static void row_set(info *row, char *key, char *value) {
  for (size_t i = 0; i < row->count; i++)
    if (strcmp(row->fields[i].key, key) == 0) {
      free(key);
      free(row->fields[i].value);
      row->fields[i].value = value;
      return;
    }
  row->fields = realloc(row->fields, (row->count + 1) * sizeof *row->fields);
  row->fields[row->count++] = (field){key, value};
}
/* arenaInfosFromText into job->out. Raises Error for malformed text and the parser's
 * RangeErrors; job's storage is released by the caller's cleanup on a raise. */
static void infos_from_text(parse_job *job, const char *text) {
  qa_common_parse_state parser; qa_common_parse_state_init(&parser);
  if (!qa_common_parse_cursor_init(&job->cursor, text, strlen(text), false)) qa_range_error("out of memory");
  job->cursor_live = true;
  for (;;) {
    const char *token = qa_common_parse_state_parse(&parser, &job->cursor, true);
    if (!token || token[0] == '\0') break;
    if (strcmp(token, "{") != 0) qa_raise("Error", 0, 0, "Malformed base arena definition");
    for (;;) {
      const char *key = qa_common_parse_state_parse(&parser, &job->cursor, true);
      if (key && strcmp(key, "}") == 0) break;
      if (!key || key[0] == '\0') qa_raise("Error", 0, 0, "Unterminated base arena definition");
      job->key = dup(key);
      const char *v = qa_common_parse_state_parse(&parser, &job->cursor, false);
      char *k = job->key;
      job->key = NULL;
      row_set(&job->row, k, dup(v && v[0] ? v : "<NULL>"));
    }
    infos *all = &job->out;
    if (all->count == all->cap) { all->cap = all->cap ? all->cap * 2 : 8; all->rows = realloc(all->rows, all->cap * sizeof *all->rows); }
    all->rows[all->count++] = job->row;
    job->row = (info){NULL, 0};
  }
  qa_common_parse_cursor_destroy(&job->cursor);
  job->cursor_live = false;
}
/* infos.push(...rows): moves every row of `from` onto `to`. */
static void infos_append(infos *to, infos *from) {
  for (size_t i = 0; i < from->count; i++) {
    if (to->count == to->cap) { to->cap = to->cap ? to->cap * 2 : 8; to->rows = realloc(to->rows, to->cap * sizeof *to->rows); }
    to->rows[to->count++] = from->rows[i];
  }
  free(from->rows);
  *from = (infos){0};
}
static void free_infos_node(void *arg) { free_infos(arg); }
static bool valid_map_name(const char *name) {
  if (!name || !name[0]) return false;
  for (const char *p = name; *p; p++)
    if (!isalnum((unsigned char)*p) && *p != '_' && *p != '/' && *p != '-') return false;
  return true;
}
/* Number.parseInt(text ?? "0", 10) || 0 */
static double parse_int10(const char *text) {
  double value = qa_js_parse_int(text ? text : "0", 10);
  return isnan(value) || value == 0 ? 0 : value;
}
static qa_app_base_arena_catalog catalog_from_infos(infos *all) {
  /* singles: type includes "single" and a usable map name. */
  size_t regular = 0;
  for (size_t i = 0; i < all->count; i++) {
    const char *type = get(&all->rows[i], "type");
    if (type && strstr(type, "single") && valid_map_name(get(&all->rows[i], "map")) && !get(&all->rows[i], "special")) regular++;
  }
  double regular_count = (double)((regular / 4) * 4);
  qa_app_base_arena_catalog catalog = {0};
  catalog.regular_count = regular_count;
  catalog.tier_count = regular_count / 4;
  double single = 0, special_number = regular_count;
  for (size_t i = 0; i < all->count; i++) {
    const info *row = &all->rows[i];
    const char *type = get(row, "type");
    if (!type || !strstr(type, "single") || !valid_map_name(get(row, "map"))) continue;
    const char *special = get(row, "special");
    special = special ? special : "";
    const char *name = get(row, "map");
    double number = special[0] ? special_number++ : single++;
    double frag = parse_int10(get(row, "fraglimit")), time = parse_int10(get(row, "timelimit"));
    qa_app_base_arena arena = {0};
    arena.number = number;
    size_t name_length = strlen(name);
    arena.map = malloc(name_length + 10);
    memcpy(arena.map, "maps/", 5);
    memcpy(arena.map + 5, name, name_length);
    memcpy(arena.map + 5 + name_length, ".bsp", 5);
    arena.title = dup(get(row, "longname") ? get(row, "longname") : name);
    arena.special = dup(special);
    char lower[256]; size_t li = 0;
    for (; special[li] && li < sizeof lower - 1; li++) lower[li] = (char)tolower((unsigned char)special[li]);
    lower[li] = '\0';
    arena.selection = strcmp(lower, "training") == 0 ? -4 : strcmp(lower, "final") == 0 ? regular_count : number;
    arena.frag_limit = frag == 0 && time == 0 ? 10 : frag;
    arena.time_limit = time;
    const char *bots = get(row, "bots");
    bots = bots ? bots : "";
    char *copy = dup(bots), *save = NULL;
    /* split(/\s+/) over Latin-1 text: JS \s includes U+00A0. */
    static const char spaces[] = " \t\n\r\f\v\xA0";
    for (char *tok = strtok_r(copy, spaces, &save); tok; tok = strtok_r(NULL, spaces, &save)) {
      arena.bots = realloc(arena.bots, (arena.bot_count + 1) * sizeof *arena.bots);
      arena.bots[arena.bot_count++] = dup(tok);
    }
    free(copy);
    catalog.arenas = realloc(catalog.arenas, (catalog.arena_count + 1) * sizeof *catalog.arenas);
    catalog.arenas[catalog.arena_count++] = arena;
  }
  return catalog;
}
qa_app_base_arena_catalog qa_app_parse_base_arena_catalog(const char *const *texts, size_t text_count) {
  infos all = {0};
  qa_cleanup all_node;
  qa_cleanup_push(&all_node, free_infos_node, &all);
  for (size_t i = 0; i < text_count; i++) {
    parse_job job = {0};
    qa_cleanup job_node;
    qa_cleanup_push(&job_node, parse_job_free, &job);
    infos_from_text(&job, texts[i]);
    qa_cleanup_pop(&job_node);
    infos_append(&all, &job.out);
  }
  qa_cleanup_pop(&all_node);
  qa_app_base_arena_catalog catalog = catalog_from_infos(&all);
  free_infos(&all);
  return catalog;
}
typedef struct {
  parse_job job;
  const char *text;
} read_job;
static void read_job_run(void *ctx) {
  read_job *read = ctx;
  qa_cleanup node;
  qa_cleanup_push(&node, parse_job_free, &read->job);
  infos_from_text(&read->job, read->text);
  qa_cleanup_pop(&node);
}
typedef struct {
  char **names;
  size_t count;
  char *text;
  infos all;
} read_state;
static void read_state_free(void *arg) {
  read_state *state = arg;
  for (size_t i = 0; i < state->count; i++) free(state->names[i]);
  free(state->names);
  free(state->text);
  free_infos(&state->all);
}
/* The try { infos.push(...arenaInfosFromText(text)) } catch { continue } of one file. */
static void read_one(read_state *state) {
  read_job read = {.text = state->text};
  qa_caught_error caught;
  if (qa_try(read_job_run, &read, &caught)) infos_append(&state->all, &read.job.out);
  free(state->text);
  state->text = NULL;
}
qa_app_base_arena_catalog qa_app_read_base_arena_catalog(qa_app_arena_mounts mounts) {
  read_state state = {0};
  qa_cleanup node;
  qa_cleanup_push(&node, read_state_free, &state);
  state.names = mounts.list_files(mounts.ctx, "scripts", ".arena", &state.count);
  /* files = arenas.txt then scripts/<name> for each .arena */
  state.text = mounts.open_text(mounts.ctx, "scripts/arenas.txt");
  if (state.text) read_one(&state);
  for (size_t i = 0; i < state.count; i++) {
    size_t length = strlen(state.names[i]);
    char *path = malloc(length + 9);
    if (path == NULL) qa_range_error("out of memory");
    memcpy(path, "scripts/", 8);
    memcpy(path + 8, state.names[i], length + 1);
    qa_cleanup path_node;
    qa_cleanup_push(&path_node, free, path);
    state.text = mounts.open_text(mounts.ctx, path);
    qa_cleanup_pop(&path_node);
    free(path);
    if (state.text) read_one(&state);
  }
  qa_cleanup_pop(&node);
  qa_app_base_arena_catalog catalog = catalog_from_infos(&state.all);
  read_state_free(&state);
  return catalog;
}
void qa_app_base_arena_catalog_destroy(qa_app_base_arena_catalog *catalog) {
  if (!catalog) return;
  for (size_t i = 0; i < catalog->arena_count; i++) {
    free(catalog->arenas[i].map); free(catalog->arenas[i].title); free(catalog->arenas[i].special);
    for (size_t j = 0; j < catalog->arenas[i].bot_count; j++) free(catalog->arenas[i].bots[j]);
    free(catalog->arenas[i].bots);
  }
  free(catalog->arenas);
  *catalog = (qa_app_base_arena_catalog){0};
}
const qa_app_base_arena *qa_app_base_arena_for_map(const qa_app_base_arena_catalog *catalog, const char *map) {
  /* TS map.toLowerCase(): Unicode lowering (U+212A KELVIN SIGN becomes 'k'). Catalog
   * maps are ASCII (arenaInfos keeps /^[a-z0-9_/-]+$/i names only). */
  char *lowered = qa_js_to_lower_case(map);
  if (lowered == NULL) qa_range_error("Out of memory");
  const char *stem = strncmp(lowered, "maps/", 5) == 0 ? lowered + 5 : lowered;
  size_t slen = strlen(stem);
  if (slen >= 4 && strcmp(stem + slen - 4, ".bsp") == 0) slen -= 4;
  /* row.map.toLowerCase() === `maps/${path}.bsp`, compared in place (no length cap). */
  for (size_t i = 0; i < catalog->arena_count; i++) {
    const char *a = catalog->arenas[i].map;
    if (strncmp(a, "maps/", 5) != 0) continue;
    a += 5;
    size_t j = 0;
    while (j < slen && a[j] && tolower((unsigned char)a[j]) == (unsigned char)stem[j]) j++;
    if (j == slen && strcmp(a + j, ".bsp") == 0) {
      free(lowered);
      return &catalog->arenas[i];
    }
  }
  free(lowered);
  qa_raise("Error", 0, 0, "No authored single-player arena for %s", map);
}
