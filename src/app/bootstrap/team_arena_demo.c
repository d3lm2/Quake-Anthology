/* Port of src/app/bootstrap/team-arena-demo.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/team_arena_demo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"
void qa_app_team_arena_demo_free(qa_app_team_arena_demo *d) { free(d->name); free(d->path); d->name = NULL; d->path = NULL; }
static bool map_ok(const char *map) {
  if (map[0] == '\0') return false;
  for (const char *p = map; *p; p++) {
    char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '-')) return false;
  }
  /* no empty or ".." path part */
  const char *part = map;
  for (const char *p = map;; p++) {
    if (*p == '/' || *p == '\0') {
      size_t len = (size_t)(p - part);
      if (len == 0 || (len == 2 && part[0] == '.' && part[1] == '.')) return false;
      if (*p == '\0') break;
      part = p + 1;
    }
  }
  return true;
}
bool qa_app_team_arena_demo_lookup(const char *map, double game_type, double protocol,
                                   bool (*exists)(void *ctx, const char *path), void *ctx,
                                   qa_app_team_arena_demo *out) {
  /* Number.isInteger: finite and integral. */
  if (!map_ok(map) || !isfinite(game_type) || game_type != trunc(game_type) || game_type < 0
      || !isfinite(protocol) || protocol != trunc(protocol) || protocol < 0)
    qa_raise("Error", 0, 0, "Invalid Team Arena demo selection");
  char game_type_text[32], protocol_text[32];
  snprintf(game_type_text, sizeof game_type_text, "%s", QA_JS_NUMBER(game_type));
  snprintf(protocol_text, sizeof protocol_text, "%s", QA_JS_NUMBER(protocol));
  size_t name_size = strlen(map) + strlen(game_type_text) + 2;
  char *name = malloc(name_size);
  if (name == NULL) qa_range_error("Out of memory");
  snprintf(name, name_size, "%s_%s", map, game_type_text);
  size_t path_size = name_size + strlen(protocol_text) + 16;
  char *path = malloc(path_size);
  if (path == NULL) { free(name); qa_range_error("Out of memory"); }
  snprintf(path, path_size, "demos/%s.dm_%s", name, protocol_text);
  qa_cleanup name_guard, path_guard;
  qa_cleanup_push(&name_guard, free, name);
  qa_cleanup_push(&path_guard, free, path);
  bool found = exists(ctx, path);
  qa_cleanup_pop(&path_guard);
  qa_cleanup_pop(&name_guard);
  if (!found) { free(name); free(path); return false; }
  out->name = name;
  out->path = path;
  return true;
}
