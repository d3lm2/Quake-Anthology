/* Port of src/app/bootstrap/team-arena-skirmish.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/settings.h"
#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js_case.h"
#include <math.h>

static void cvar_set_(qa_cvar_registry *r, const char *name, const char *value, bool force) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_set(r, name, value, force, &out)) qa_cvar_read_free(&out);
}

const qa_app_team_arena_override_row qa_app_team_arena_server_overrides[6] = {
    {"capturelimit", "ui_saveCaptureLimit"}, {"fraglimit", "ui_saveFragLimit"},
    {"g_doWarmup", "ui_doWarmup"}, {"g_warmup", "ui_Warmup"},
    {"sv_pure", "ui_pure"}, {"g_friendlyFire", "ui_friendlyFire"}};
typedef struct camp_map {
  char *title, *name, *opponent; double team_members;
  double *types; size_t type_count; double *times; /* parallel to types */
} camp_map;
typedef struct team_row { char *key; char *members[5]; } team_row;
typedef struct alias_row { char *key; char *value; } alias_row;
struct qa_app_team_arena_campaign {
  double *game_types; size_t game_type_count;
  camp_map *maps; size_t map_count;
  team_row *teams; size_t team_count;
  alias_row *aliases; size_t alias_count;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static char *lower_dup(const char *s) {
  /* TS toLowerCase over latin1-decoded campaign text: Latin-1 folding. */
  char *r = qa_js_latin1_to_lower_case(s);
  if (!r) qa_range_error("Out of memory");
  return r;
}
static bool ci_eq(const char *a, const char *b) {
  return qa_js_latin1_equals_ignore_case(a, b);
}
/* sections(): parse into named row lists. Rows owned. */
typedef struct sec { char *name; char ***rows; size_t *row_lens; size_t row_count; } sec;
typedef struct secs { sec *items; size_t count; } secs;
static char *next_token(qa_common_parse_state *st, qa_common_parse_cursor *cur) {
  const char *t = qa_common_parse_state_parse(st, cur, true);
  return xdup(t ? t : "");
}
static void secs_free(secs *s);
/* One parse's partial state: metadata is untrusted file text, and a TS throw leaves all of it to the GC. */
typedef struct sections_scope {
  secs out;
  sec current; bool has_current;
  char **row; size_t row_n;
  char *token;
  qa_common_parse_cursor *cursor;
} sections_scope;
static void sections_scope_free(void *v) {
  sections_scope *scope = v;
  if (scope->has_current) {
    scope->out.items = realloc(scope->out.items, (scope->out.count + 1) * sizeof *scope->out.items);
    if (scope->out.items != NULL) scope->out.items[scope->out.count++] = scope->current;
  }
  secs_free(&scope->out);
  for (size_t i = 0; i < scope->row_n; i++) free(scope->row[i]);
  free(scope->row);
  free(scope->token);
  if (scope->cursor != NULL) qa_common_parse_cursor_destroy(scope->cursor);
}
static secs parse_sections(const char *text) {
  qa_common_parse_state st; qa_common_parse_state_init(&st);
  qa_common_parse_cursor cur;
  if (!qa_common_parse_cursor_init(&cur, text, strlen(text), false)) qa_range_error("Out of memory");
  sections_scope scope; memset(&scope, 0, sizeof scope);
  scope.cursor = &cur;
  qa_cleanup guard;
  qa_cleanup_push(&guard, sections_scope_free, &scope);
  size_t cap = 0;
  for (;;) {
    scope.token = next_token(&st, &cur);
    if (scope.token[0] == '\0') { free(scope.token); scope.token = NULL; break; }
    char *section = scope.token;
    char *open = next_token(&st, &cur);
    bool opened = strcmp(open, "{") == 0;
    free(open);
    if (!opened) qa_raise("Error", 0, 0, "Invalid Team Arena %s section", section);
    memset(&scope.current, 0, sizeof scope.current);
    scope.current.name = lower_dup(section);
    scope.has_current = true;
    free(section); scope.token = NULL;
    sec *current = &scope.current;
    size_t rcap = 0;
    for (;;) {
      char *tok = next_token(&st, &cur);
      bool close = strcmp(tok, "}") == 0, row_open = strcmp(tok, "{") == 0;
      free(tok);
      if (close) break;
      if (!row_open) qa_raise("Error", 0, 0, "Invalid Team Arena %s row", current->name);
      size_t fcap = 0;
      for (;;) {
        char *field = next_token(&st, &cur);
        if (strcmp(field, "}") == 0) { free(field); break; }
        if (field[0] == '\0' || strcmp(field, "{") == 0) { free(field); qa_raise("Error", 0, 0, "Invalid Team Arena %s field", current->name); }
        if (scope.row_n == fcap) {
          fcap = fcap ? fcap * 2 : 8;
          char **grown = realloc(scope.row, fcap * sizeof *grown);
          if (!grown) { free(field); qa_range_error("Out of memory"); }
          scope.row = grown;
        }
        scope.row[scope.row_n++] = field;
      }
      if (current->row_count == rcap) {
        rcap = rcap ? rcap * 2 : 8;
        char ***rows = realloc(current->rows, rcap * sizeof *rows);
        if (!rows) qa_range_error("Out of memory");
        current->rows = rows;
        size_t *lens = realloc(current->row_lens, rcap * sizeof *lens);
        if (!lens) qa_range_error("Out of memory");
        current->row_lens = lens;
      }
      current->rows[current->row_count] = scope.row; current->row_lens[current->row_count] = scope.row_n; current->row_count++;
      scope.row = NULL; scope.row_n = 0;
    }
    if (scope.out.count == cap) {
      cap = cap ? cap * 2 : 4;
      sec *items = realloc(scope.out.items, cap * sizeof *items);
      if (!items) qa_range_error("Out of memory");
      scope.out.items = items;
    }
    scope.out.items[scope.out.count++] = scope.current;
    scope.has_current = false;
  }
  qa_cleanup_pop(&guard);
  qa_common_parse_cursor_destroy(&cur);
  return scope.out;
}
/* result.set(section.toLowerCase(), rows): a repeated section's last rows win. */
static const sec *find_sec(const secs *s, const char *name) {
  const sec *found = NULL;
  for (size_t i = 0; i < s->count; i++) if (strcmp(s->items[i].name, name) == 0) found = &s->items[i];
  return found;
}
static void secs_free(secs *s) {
  for (size_t i = 0; i < s->count; i++) {
    free(s->items[i].name);
    for (size_t r = 0; r < s->items[i].row_count; r++) {
      for (size_t f = 0; f < s->items[i].row_lens[r]; f++) free(s->items[i].rows[r][f]);
      free(s->items[i].rows[r]);
    }
    free(s->items[i].rows); free(s->items[i].row_lens);
  }
  free(s->items);
}
static const char *field_at(char **row, size_t len, size_t index) {
  if (index >= len) qa_raise("Error", 0, 0, "Incomplete Team Arena metadata row");
  return row[index];
}
static double integer_at(char **row, size_t len, size_t index) {
  const char *v = field_at(row, len, index);
  const char *p = v; if (*p == '-') p++;
  if (*p == '\0') qa_raise("Error", 0, 0, "Invalid Team Arena metadata integer");
  for (; *p; p++) if (!isdigit((unsigned char)*p)) qa_raise("Error", 0, 0, "Invalid Team Arena metadata integer");
  double n = qa_input_js_parse_number(v);
  if (!(isfinite(n) && n == trunc(n)) || n > 9007199254740991.0 || n < -9007199254740991.0)
    qa_raise("Error", 0, 0, "Team Arena metadata integer is out of range");
  return n;
}
typedef struct campaign_scope { secs game, teams; qa_app_team_arena_campaign *campaign; } campaign_scope;
static void campaign_scope_free(void *v) {
  campaign_scope *scope = v;
  secs_free(&scope->game); secs_free(&scope->teams);
  qa_app_team_arena_campaign_destroy(scope->campaign);
}
qa_app_team_arena_campaign *qa_app_parse_team_arena_campaign(const char *game_info, const char *team_info) {
  campaign_scope scope; memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, campaign_scope_free, &scope);
  scope.game = parse_sections(game_info);
  scope.teams = parse_sections(team_info);
  secs game = scope.game, teams = scope.teams;
  qa_app_team_arena_campaign *c = scope.campaign = calloc(1, sizeof *c);
  if (!c) qa_range_error("Out of memory");
  const sec *gt = find_sec(&game, "gametypes");
  if (gt) {
    c->game_types = calloc(gt->row_count ? gt->row_count : 1, sizeof *c->game_types);
    if (!c->game_types) qa_range_error("Out of memory");
    for (size_t i = 0; i < gt->row_count; i++) c->game_types[i] = integer_at(gt->rows[i], gt->row_lens[i], 1);
    c->game_type_count = gt->row_count;
  }
  const sec *ms = find_sec(&game, "maps");
  if (ms) {
    c->maps = calloc(ms->row_count ? ms->row_count : 1, sizeof *c->maps);
    if (!c->maps) qa_range_error("Out of memory");
    c->map_count = ms->row_count;
    for (size_t i = 0; i < ms->row_count; i++) {
      char **row = ms->rows[i]; size_t len = ms->row_lens[i];
      camp_map *m = &c->maps[i];
      size_t pairs = len > 4 ? (len - 4 + 1) / 2 : 0;
      m->types = calloc(pairs ? pairs : 1, sizeof *m->types);
      m->times = calloc(pairs ? pairs : 1, sizeof *m->times);
      if (!m->types || !m->times) qa_range_error("Out of memory");
      for (size_t idx = 4; idx < len; idx += 2) {
        double type = integer_at(row, len, idx);
        /* Set semantics: keep first occurrence order, times map overwrites. */
        size_t at = m->type_count;
        for (size_t j = 0; j < m->type_count; j++) if (m->types[j] == type) at = j;
        if (at == m->type_count) m->types[m->type_count++] = type;
        m->times[at] = integer_at(row, len, idx + 1);
      }
      const char *name = field_at(row, len, 1);
      double members = integer_at(row, len, 2);
      bool bad_name = name[0] == '\0';
      for (const char *p = name; *p; p++) {
        char ch = *p;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '-')) bad_name = true;
      }
      /* split("/").includes("..") */
      const char *part = name;
      for (const char *p = name;; p++) {
        if (*p == '/' || *p == '\0') { if ((size_t)(p - part) == 2 && part[0] == '.' && part[1] == '.') bad_name = true; if (!*p) break; part = p + 1; }
      }
      if (bad_name || members < 1 || members > 5) qa_raise("Error", 0, 0, "Invalid Team Arena campaign map");
      m->title = xdup(field_at(row, len, 0)); m->name = xdup(name);
      m->team_members = members; m->opponent = xdup(field_at(row, len, 3));
    }
  }
  const sec *tm = find_sec(&teams, "teams");
  if (tm) {
    c->teams = calloc(tm->row_count ? tm->row_count : 1, sizeof *c->teams);
    if (!c->teams) qa_range_error("Out of memory");
    /* new Map(entries): a repeated key keeps its first position and takes the last members. */
    size_t kept = 0;
    for (size_t i = 0; i < tm->row_count; i++) {
      char *key = lower_dup(field_at(tm->rows[i], tm->row_lens[i], 0));
      size_t at = kept;
      for (size_t j = 0; j < kept; j++) if (strcmp(c->teams[j].key, key) == 0) { at = j; break; }
      if (at == kept) { c->teams[at].key = key; c->team_count = ++kept; }
      else free(key);
      for (size_t k = 0; k < 5; k++) {
        char *member = xdup(field_at(tm->rows[i], tm->row_lens[i], 2 + k));
        free(c->teams[at].members[k]);
        c->teams[at].members[k] = member;
      }
    }
    c->team_count = kept;
  }
  const sec *al = find_sec(&teams, "aliases");
  if (al) {
    c->aliases = calloc(al->row_count ? al->row_count : 1, sizeof *c->aliases);
    if (!c->aliases) qa_range_error("Out of memory");
    size_t kept = 0;
    for (size_t i = 0; i < al->row_count; i++) {
      char *key = lower_dup(field_at(al->rows[i], al->row_lens[i], 0));
      size_t at = kept;
      for (size_t j = 0; j < kept; j++) if (strcmp(c->aliases[j].key, key) == 0) { at = j; break; }
      if (at == kept) { c->aliases[at].key = key; c->alias_count = ++kept; }
      else free(key);
      char *value = xdup(field_at(al->rows[i], al->row_lens[i], 1));
      free(c->aliases[at].value);
      c->aliases[at].value = value;
    }
  }
  qa_cleanup_pop(&guard);
  secs_free(&game); secs_free(&teams);
  return c;
}
void qa_app_team_arena_campaign_destroy(qa_app_team_arena_campaign *c) {
  if (!c) return;
  free(c->game_types);
  for (size_t i = 0; i < c->map_count; i++) { free(c->maps[i].title); free(c->maps[i].name); free(c->maps[i].opponent); free(c->maps[i].types); free(c->maps[i].times); }
  free(c->maps);
  for (size_t i = 0; i < c->team_count; i++) { free(c->teams[i].key); for (int k = 0; k < 5; k++) free(c->teams[i].members[k]); }
  free(c->teams);
  for (size_t i = 0; i < c->alias_count; i++) { free(c->aliases[i].key); free(c->aliases[i].value); }
  free(c->aliases); free(c);
}
char **qa_app_team_arena_team_choices(const qa_app_team_arena_campaign *c, size_t *out_count) {
  char **r = calloc(c->team_count ? c->team_count : 1, sizeof *r);
  if (!r) qa_range_error("Out of memory");
  for (size_t i = 0; i < c->team_count; i++) r[i] = c->teams[i].key;
  *out_count = c->team_count;
  return r;
}
static bool map_active(const camp_map *m, double game_type) {
  double type = game_type == 2 ? 3 : game_type == 3 ? 0 : game_type;
  bool has2 = false, has = false;
  for (size_t i = 0; i < m->type_count; i++) { if (m->types[i] == 2) has2 = true; if (m->types[i] == type) has = true; }
  return has2 && has;
}
qa_app_team_arena_cursor qa_app_next_team_arena_cursor(const qa_app_team_arena_campaign *c, qa_app_team_arena_cursor current) {
  long gt_i = (long)current.game_type_index, map_i = (long)current.map_index;
  if (gt_i < 0 || (size_t)gt_i >= c->game_type_count || map_i < 0 || (size_t)map_i >= c->map_count)
    qa_raise("Error", 0, 0, "Invalid Team Arena campaign cursor");
  double type = c->game_types[gt_i];
  for (size_t i = (size_t)map_i + 1; i < c->map_count; i++)
    if (map_active(&c->maps[i], type)) { qa_app_team_arena_cursor r = {current.game_type_index, (double)i}; return r; }
  long incremented = gt_i + 1;
  long next_gt = incremented >= (long)c->game_type_count ? 1 : incremented == 2 ? 3 : incremented;
  bool has_gt = next_gt >= 0 && (size_t)next_gt < c->game_type_count;
  double gt = has_gt ? c->game_types[next_gt] : 0;
  long found = -1;
  if (has_gt) for (size_t i = 0; i < c->map_count; i++) if (map_active(&c->maps[i], gt)) { found = (long)i; break; }
  if (found == -1) qa_raise("Error", 0, 0, "Next Team Arena game type has no authored single-player maps");
  qa_app_team_arena_cursor r = {(double)next_gt, (double)found};
  return r;
}
qa_app_team_arena_cursor qa_app_current_team_arena_cursor(const qa_app_team_arena_campaign *c, const char *map, double game_type) {
  long gt_i = -1, map_i = -1;
  for (size_t i = 0; i < c->game_type_count; i++) if (c->game_types[i] == game_type) { gt_i = (long)i; break; }
  for (size_t i = 0; i < c->map_count; i++) {
    size_t size = strlen(c->maps[i].name) + 10;
    char *path = malloc(size);
    if (path == NULL) qa_range_error("Out of memory");
    snprintf(path, size, "maps/%s.bsp", c->maps[i].name);
    bool same = ci_eq(path, map);
    free(path);
    if (same) { map_i = (long)i; break; }
  }
  if (gt_i == -1 || map_i == -1) qa_raise("Error", 0, 0, "Current Team Arena match has no authored campaign entry");
  qa_app_team_arena_cursor r = {(double)gt_i, (double)map_i};
  return r;
}
static char *num_str(double v) { char *r = qa_input_js_number(v); if (!r) qa_range_error("Out of memory"); return r; }
static void push_nv(qa_app_name_value **arr, size_t *n, size_t *cap, const char *name, const char *value) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *arr = realloc(*arr, *cap * sizeof **arr); if (!*arr) qa_range_error("Out of memory"); }
  (*arr)[*n].name = xdup(name); (*arr)[*n].value = xdup(value); (*n)++;
}
void qa_app_name_values_free(qa_app_name_value *values, size_t count) {
  for (size_t i = 0; i < count; i++) { free(values[i].name); free(values[i].value); }
  free(values);
}
void qa_app_team_arena_skirmish_free(qa_app_team_arena_skirmish *s) {
  free(s->map); free(s->title); free(s->player_team); free(s->player_model); free(s->player_head_model);
  qa_app_name_values_free(s->cvars, s->cvar_count);
  qa_app_name_values_free(s->client_cvars, s->client_cvar_count);
  for (size_t i = 0; i < s->bot_count; i++) { free(s->bots[i].ai); free(s->bots[i].name); free(s->bots[i].team); }
  free(s->bots); memset(s, 0, sizeof *s);
}
void qa_app_team_arena_skirmish_init_default(qa_app_team_arena_skirmish *out, const qa_app_team_arena_campaign *c, double skill, qa_app_team_arena_cursor cursor, qa_app_team_arena_teams teams) {
  memset(out, 0, sizeof *out);
  long gt_i = (long)cursor.game_type_index, map_i = (long)cursor.map_index;
  double game_type = (gt_i >= 0 && (size_t)gt_i < c->game_type_count) ? c->game_types[gt_i] : -1;
  const camp_map *map = (map_i >= 0 && (size_t)map_i < c->map_count) ? &c->maps[map_i] : NULL;
  if (game_type != 1 && game_type != 4 && game_type != 5 && game_type != 6 && game_type != 7)
    qa_raise("Error", 0, 0, "Unsupported Team Arena single-player game type");
  if (map == NULL || !map_active(map, game_type)) qa_raise("Error", 0, 0, "Selected Team Arena map is not authored for this skirmish");
  size_t bcap = 0;
  qa_app_team_arena_bot *bots = NULL; size_t nbots = 0;
  if (game_type == 1) {
    bots = calloc(1, sizeof *bots); if (!bots) qa_range_error("Out of memory");
    bots[0].ai = xdup(map->opponent); bots[0].name = xdup(map->opponent); bots[0].team = xdup(""); bots[0].delay_milliseconds = 500;
    nbots = 1;
  } else {
    const char *names[2] = {teams.opponent, teams.player};
    const char *team_names[2] = {"Blue", "Red"};
    double counts[2] = {map->team_members, map->team_members - 1};
    for (int side = 0; side < 2; side++) {
      char *key = lower_dup(names[side]);
      const team_row *row = NULL;
      for (size_t i = 0; i < c->team_count; i++) if (strcmp(c->teams[i].key, key) == 0) row = &c->teams[i];
      free(key);
      if (!row) qa_raise("Error", 0, 0, "Team Arena team is missing: %s", names[side]);
      for (double idx = 0; idx < counts[side]; idx++) {
        if ((size_t)idx >= 5) qa_raise("Error", 0, 0, "Incomplete Team Arena metadata row");
        const char *member = row->members[(size_t)idx];
        char *mkey = lower_dup(member);
        const char *ai = "James";
        for (size_t i = 0; i < c->alias_count; i++) if (strcmp(c->aliases[i].key, mkey) == 0) ai = c->aliases[i].value;
        free(mkey);
        if (nbots == bcap) { bcap = bcap ? bcap * 2 : 8; bots = realloc(bots, bcap * sizeof *bots); if (!bots) qa_range_error("Out of memory"); }
        bots[nbots].ai = xdup(ai); bots[nbots].name = xdup(member); bots[nbots].team = xdup(team_names[side]);
        bots[nbots].delay_milliseconds = ((double)nbots + 1) * 500; nbots++;
      }
    }
  }
  double max_clients = game_type == 1 ? 2 : map->team_members * 2;
  double time_to_beat = 0;
  for (size_t i = 0; i < map->type_count; i++) if (map->types[i] == game_type) time_to_beat = map->times[i];
  size_t active_before = 0;
  for (long i = 0; i < map_i; i++) if (map_active(&c->maps[i], game_type)) active_before++;
  qa_app_name_value *cvars = NULL; size_t nc = 0, ccap = 0;
  char *tmp;
  push_nv(&cvars, &nc, &ccap, "nextmap", "teamarena-results");
  tmp = num_str(time_to_beat); push_nv(&cvars, &nc, &ccap, "ui_teamArenaTimeToBeat", tmp); free(tmp);
  tmp = num_str(game_type); push_nv(&cvars, &nc, &ccap, "g_gametype", tmp); free(tmp);
  push_nv(&cvars, &nc, &ccap, "ui_singlePlayerActive", "1");
  tmp = num_str(skill); push_nv(&cvars, &nc, &ccap, "g_spSkill", tmp); free(tmp);
  tmp = num_str(max_clients); push_nv(&cvars, &nc, &ccap, "sv_maxclients", tmp); free(tmp);
  push_nv(&cvars, &nc, &ccap, "g_doWarmup", "1");
  push_nv(&cvars, &nc, &ccap, "g_warmup", "15");
  push_nv(&cvars, &nc, &ccap, "sv_pure", "0");
  push_nv(&cvars, &nc, &ccap, "g_friendlyFire", "0");
  push_nv(&cvars, &nc, &ccap, "g_redTeam", teams.player);
  push_nv(&cvars, &nc, &ccap, "g_blueTeam", teams.opponent);
  push_nv(&cvars, &nc, &ccap, "capturelimit", game_type == 6 ? "4" : game_type == 7 ? "15" : "5");
  push_nv(&cvars, &nc, &ccap, "fraglimit", "10");
  push_nv(&cvars, &nc, &ccap, "ui_scoreMap", map->title);
  tmp = num_str(cursor.game_type_index); push_nv(&cvars, &nc, &ccap, "ui_gameType", tmp); free(tmp);
  tmp = num_str(cursor.map_index); push_nv(&cvars, &nc, &ccap, "ui_currentMap", tmp); free(tmp);
  tmp = num_str((double)active_before); push_nv(&cvars, &nc, &ccap, "ui_mapIndex", tmp); free(tmp);
  push_nv(&cvars, &nc, &ccap, "ui_teamName", teams.player);
  push_nv(&cvars, &nc, &ccap, "ui_opponentName", teams.opponent);
  qa_app_name_value *client = NULL; size_t ncl = 0, clcap = 0;
  push_nv(&client, &ncl, &clcap, "cg_cameraOrbit", "0");
  push_nv(&client, &ncl, &clcap, "cg_thirdPerson", "0");
  push_nv(&client, &ncl, &clcap, "cg_drawTimer", "1");
  size_t map_size = strlen(map->name) + 10;
  out->map = malloc(map_size);
  if (out->map == NULL) qa_range_error("Out of memory");
  snprintf(out->map, map_size, "maps/%s.bsp", map->name);
  out->title = xdup(map->title);
  out->game_type = game_type; out->max_clients = max_clients; out->time_to_beat = time_to_beat; out->skill = skill;
  out->cvars = cvars; out->cvar_count = nc; out->client_cvars = client; out->client_cvar_count = ncl;
  out->bots = bots; out->bot_count = nbots;
  out->player_team = xdup(game_type == 1 ? "free" : "Red");
  out->player_model = xdup(game_type == 1 ? "sarge" : "james");
  out->player_head_model = xdup(game_type == 1 ? "sarge" : "*james");
  out->cursor = cursor;
}
static const char *find_ci(const qa_app_name_value *base, size_t n, const char *name) {
  for (size_t i = 0; i < n; i++) if (ci_eq(base[i].name, name)) return base[i].value;
  return NULL;
}
qa_app_name_value *qa_app_team_arena_source_cvars(const qa_app_team_arena_skirmish *setup, const qa_app_name_value *baseline, size_t baseline_count, size_t *out_count) {
  size_t def_count = 0;
  const qa_q3_base_cvar_definition *defs = qa_q3_base_game_cvar_definitions(QA_Q3_BASE_PRODUCT_MISSIONPACK, &def_count);
  qa_app_name_value *out = NULL; size_t n = 0, cap = 0;
  const char *mc = find_ci(baseline, baseline_count, "sv_maxclients");
  push_nv(&out, &n, &cap, "ui_maxClients", mc ? mc : "0");
  for (size_t i = 0; i < 6; i++) {
    const char *v = find_ci(baseline, baseline_count, qa_app_team_arena_server_overrides[i].name);
    if (!v && defs) for (size_t j = 0; j < def_count; j++) if (ci_eq(defs[j].name, qa_app_team_arena_server_overrides[i].name)) { v = defs[j].value; break; }
    push_nv(&out, &n, &cap, qa_app_team_arena_server_overrides[i].saved, v ? v : "0");
  }
  for (size_t i = 0; i < setup->cvar_count; i++) push_nv(&out, &n, &cap, setup->cvars[i].name, setup->cvars[i].value);
  *out_count = n; return out;
}
qa_app_name_value *qa_app_team_arena_client_cvars(const qa_app_team_arena_skirmish *setup, const qa_app_name_value *baseline, size_t baseline_count, size_t *out_count) {
  const char *saved = find_ci(baseline, baseline_count, "ui_drawtimer");
  if (!saved) saved = find_ci(baseline, baseline_count, "cg_drawtimer");
  qa_app_name_value *out = NULL; size_t n = 0, cap = 0;
  push_nv(&out, &n, &cap, "ui_drawTimer", saved ? saved : "0");
  for (size_t i = 0; i < setup->client_cvar_count; i++) push_nv(&out, &n, &cap, setup->client_cvars[i].name, setup->client_cvars[i].value);
  *out_count = n; return out;
}
typedef struct read_scope { char *game, *teams; qa_app_team_arena_campaign *campaign; char **files; size_t file_count; } read_scope;
static void read_scope_free(void *v) {
  read_scope *scope = v;
  free(scope->game); free(scope->teams);
  qa_app_team_arena_campaign_destroy(scope->campaign);
  for (size_t i = 0; i < scope->file_count; i++) free(scope->files[i]);
  free(scope->files);
}
qa_app_team_arena_campaign *qa_app_read_team_arena_campaign(qa_app_team_arena_campaign_host host) {
  read_scope scope; memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, read_scope_free, &scope);
  scope.game = host.read_file(host.ctx, host.game_info_path);
  scope.teams = host.read_file(host.ctx, host.team_info_path);
  qa_app_team_arena_campaign *c = scope.campaign = qa_app_parse_team_arena_campaign(scope.game, scope.teams);
  free(scope.game); free(scope.teams); scope.game = scope.teams = NULL;
  if (!host.additional_teams) { qa_cleanup_pop(&guard); return c; }
  size_t count = 0; char **files = scope.files = host.list_team_files(host.ctx, &count);
  scope.file_count = count;
  for (size_t i = 0; i < count; i++) {
    size_t path_size = strlen(files[i]) + 9;
    char *path = malloc(path_size);
    if (path == NULL) qa_range_error("Out of memory");
    snprintf(path, path_size, "scripts/%s", files[i]);
    char *text = host.read_file(host.ctx, path);
    free(path);
    qa_cleanup text_guard;
    qa_cleanup_push(&text_guard, free, text);
    qa_app_team_arena_campaign *extra = qa_app_parse_team_arena_campaign("", text);
    qa_cleanup_pop(&text_guard);
    free(text);
    /* Merge: set/replace by key (Map.set). */
    for (size_t t = 0; t < extra->team_count; t++) {
      size_t at = c->team_count;
      for (size_t j = 0; j < c->team_count; j++) if (strcmp(c->teams[j].key, extra->teams[t].key) == 0) at = j;
      if (at == c->team_count) {
        c->teams = realloc(c->teams, (c->team_count + 1) * sizeof *c->teams);
        if (!c->teams) qa_range_error("Out of memory");
        c->teams[c->team_count].key = xdup(extra->teams[t].key);
        for (int k = 0; k < 5; k++) c->teams[c->team_count].members[k] = xdup(extra->teams[t].members[k]);
        c->team_count++;
      } else for (int k = 0; k < 5; k++) { free(c->teams[at].members[k]); c->teams[at].members[k] = xdup(extra->teams[t].members[k]); }
    }
    for (size_t t = 0; t < extra->alias_count; t++) {
      size_t at = c->alias_count;
      for (size_t j = 0; j < c->alias_count; j++) if (strcmp(c->aliases[j].key, extra->aliases[t].key) == 0) at = j;
      if (at == c->alias_count) {
        c->aliases = realloc(c->aliases, (c->alias_count + 1) * sizeof *c->aliases);
        if (!c->aliases) qa_range_error("Out of memory");
        c->aliases[c->alias_count].key = xdup(extra->aliases[t].key);
        c->aliases[c->alias_count].value = xdup(extra->aliases[t].value);
        c->alias_count++;
      } else { free(c->aliases[at].value); c->aliases[at].value = xdup(extra->aliases[t].value); }
    }
    qa_app_team_arena_campaign_destroy(extra);
    free(files[i]); files[i] = NULL;
  }
  qa_cleanup_pop(&guard);
  free(files);
  return c;
}
qa_app_team_arena_campaign *qa_app_load_team_arena_campaign(qa_app_team_arena_campaign_host host) {
  /* TS prepareQ3ApplicationProduct/catalog mounting is host-resolved here;
   * a NULL game_info_path signals the TS "requires a Q3 product policy". */
  if (host.game_info_path == NULL) qa_raise("Error", 0, 0, "Team Arena requires a Q3 product policy");
  return qa_app_read_team_arena_campaign(host);
}
static void campaign_destroy_cleanup(void *c) { qa_app_team_arena_campaign_destroy(c); }
void qa_app_read_team_arena_skirmish(qa_app_team_arena_skirmish *out, qa_app_team_arena_campaign_host host, double skill,
                                     bool has_previous, const char *prev_map, double prev_game_type, bool advance,
                                     bool has_teams, qa_app_team_arena_teams teams) {
  qa_app_team_arena_campaign *c = qa_app_read_team_arena_campaign(host);
  qa_cleanup guard;
  qa_cleanup_push(&guard, campaign_destroy_cleanup, c);
  qa_app_team_arena_cursor cursor = {3, 0};
  if (has_previous) {
    qa_app_team_arena_cursor cur = qa_app_current_team_arena_cursor(c, prev_map, prev_game_type);
    cursor = advance ? qa_app_next_team_arena_cursor(c, cur) : cur;
  }
  qa_app_team_arena_teams t = has_teams ? teams : (qa_app_team_arena_teams){"Pagans", "Stroggs"};
  qa_app_team_arena_skirmish_init_default(out, c, skill, cursor, t);
  qa_cleanup_pop(&guard);
  qa_app_team_arena_campaign_destroy(c);
}
struct qa_app_team_arena_launch_overrides {
  qa_app_team_arena_skirmish setup; /* borrowed fields copied shallowly; caller retains */
  const qa_app_team_arena_skirmish *setup_ptr;
  bool captured;
  qa_app_name_value *source; size_t source_count;
  qa_seat_id *seat_ids; qa_app_name_value **seat_settings; size_t *seat_counts; size_t seat_count;
};
qa_app_team_arena_launch_overrides *qa_app_team_arena_launch_overrides_create(const qa_app_team_arena_skirmish *setup) {
  qa_app_team_arena_launch_overrides *o = calloc(1, sizeof *o);
  if (!o) return NULL;
  o->setup_ptr = setup;
  return o;
}
void qa_app_team_arena_launch_overrides_destroy(qa_app_team_arena_launch_overrides *o) {
  if (!o) return;
  qa_app_name_values_free(o->source, o->source_count);
  free(o->seat_ids); free(o->seat_counts);
  if (o->seat_settings) { for (size_t i = 0; i < o->seat_count; i++) qa_app_name_values_free(o->seat_settings[i], o->seat_counts[i]); free(o->seat_settings); }
  free(o);
}
static qa_app_name_value *snapshots_as_nv(qa_cvar_registry *cvars, size_t *out_count) {
  size_t n = 0;
  qa_cvar_snapshot *snaps = qa_cvar_registry_snapshots(cvars, 0, &n);
  qa_app_name_value *out = calloc(n ? n : 1, sizeof *out);
  if (!out) qa_range_error("Out of memory");
  for (size_t i = 0; i < n; i++) { out[i].name = snaps[i].name; out[i].value = snaps[i].value; snaps[i].name = NULL; snaps[i].value = NULL; qa_cvar_read_free(&snaps[i]); }
  free(snaps);
  *out_count = n; return out;
}
void qa_app_team_arena_launch_overrides_apply(qa_app_team_arena_launch_overrides *o, qa_cvar_registry *source,
                                              const qa_app_team_arena_launch_seat *seats, size_t seat_count) {
  size_t applied = 0; qa_cvar_snapshot *latched = qa_cvar_registry_apply_latched(source, NULL, &applied);
  for (size_t i = 0; i < applied; i++) qa_cvar_read_free(&latched[i]);
  free(latched);
  if (!o->captured) {
    o->seat_ids = calloc(seat_count ? seat_count : 1, sizeof *o->seat_ids);
    o->seat_settings = calloc(seat_count ? seat_count : 1, sizeof *o->seat_settings);
    o->seat_counts = calloc(seat_count ? seat_count : 1, sizeof *o->seat_counts);
    if (!o->seat_ids || !o->seat_settings || !o->seat_counts) qa_range_error("Out of memory");
    o->seat_count = seat_count;
    for (size_t i = 0; i < seat_count; i++) {
      o->seat_ids[i] = seats[i].id;
      size_t bn = 0; qa_app_name_value *base = snapshots_as_nv(seats[i].cvars, &bn);
      size_t cn = 0; qa_app_name_value *client = qa_app_team_arena_client_cvars(o->setup_ptr, base, bn, &cn);
      qa_app_name_values_free(base, bn);
      size_t cap = cn;
      push_nv(&client, &cn, &cap, "model", o->setup_ptr->player_model);
      push_nv(&client, &cn, &cap, "team_model", o->setup_ptr->player_model);
      push_nv(&client, &cn, &cap, "headmodel", o->setup_ptr->player_head_model);
      push_nv(&client, &cn, &cap, "team_headmodel", o->setup_ptr->player_head_model);
      o->seat_settings[i] = client; o->seat_counts[i] = cn;
    }
    const char *timer = "0";
    if (seat_count) for (size_t i = 0; i < o->seat_counts[0]; i++) if (strcmp(o->seat_settings[0][i].name, "ui_drawTimer") == 0) timer = o->seat_settings[0][i].value;
    size_t bn = 0; qa_app_name_value *base = snapshots_as_nv(source, &bn);
    size_t sn = 0; qa_app_name_value *src = qa_app_team_arena_source_cvars(o->setup_ptr, base, bn, &sn);
    qa_app_name_values_free(base, bn);
    size_t cap = sn; push_nv(&src, &sn, &cap, "ui_drawTimer", timer);
    o->source = src; o->source_count = sn;
    o->captured = true;
  }
  for (size_t i = 0; i < o->source_count; i++) cvar_set_(source, o->source[i].name, o->source[i].value, true);
  for (size_t i = 0; i < seat_count; i++) {
    size_t at = o->seat_count;
    for (size_t j = 0; j < o->seat_count && at == o->seat_count; j++) if (qa_same_seat(o->seat_ids[j], seats[i].id)) at = j;
    if (at == o->seat_count) qa_raise("Error", 0, 0, "Team Arena preparation adopted an unknown seat");
    for (size_t k = 0; k < o->seat_counts[at]; k++) cvar_set_(seats[i].cvars, o->seat_settings[at][k].name, o->seat_settings[at][k].value, true);
  }
}
