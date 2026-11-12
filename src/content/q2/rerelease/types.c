/* Port of src/content/q2/rerelease/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/types.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"

/* TS `throw new Error(message)`: raised through qa_raise so a TS catch is a qa_try;
 * with no catch active it prints "Error: <message>" and aborts. */
noreturn void qa_q2rr_error(const char *format, ...) {
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
  va_list args; va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}

char *qa_q2rr_dup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}

bool qa_q2rr_ieq(const char *a, const char *b) {
  if (!a || !b) return a == b;
  /* TS toLowerCase() === over entity strings (Latin-1 byte strings) and ASCII names. */
  return qa_js_latin1_equals_ignore_case(a, b);
}
bool qa_q2rr_contains(const char *s, const char *needle) { return s && needle && strstr(s, needle) != NULL; }
bool qa_q2rr_starts_with(const char *s, const char *prefix) {
  return s && prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}

qa_q2_fog_state qa_q2_create_fog(void) { qa_q2_fog_state s = {0}; return s; }

const char *qa_q2_coop_respawn_state_name(qa_q2_coop_respawn_state s) {
  switch (s) {
    case QA_Q2_COOP_IN_COMBAT: return "in-combat";
    case QA_Q2_COOP_BAD_AREA: return "bad-area";
    case QA_Q2_COOP_BLOCKED: return "blocked";
    case QA_Q2_COOP_WAITING: return "waiting";
    case QA_Q2_COOP_NO_LIVES: return "no-lives";
    default: return "none";
  }
}

static char *dup_or_abort(const char *src) {
  char *out = qa_q2rr_dup(src ? src : "");
  if (!out) qa_range_error("Out of memory");
  return out;
}
void qa_q2rr_set_text(char **field, const char *value) {
  char *next = dup_or_abort(value); /* value may alias *field */
  free(*field);
  *field = next;
}

void qa_q2rr_player_extra_state_init(qa_q2rr_player_extra_state *s, double seat, const char *social_id) {
  memset(s, 0, sizeof *s);
  s->seat = seat;
  s->social_id = dup_or_abort(social_id);
  s->auto_shield = -1;
  s->dogtag = dup_or_abort("");
  s->help_image = dup_or_abort("friend");
  s->fog = qa_q2_create_fog();
  s->wanted_fog = qa_q2_create_fog();
}
void qa_q2rr_player_extra_state_destroy(qa_q2rr_player_extra_state *s) {
  free(s->help_points); s->help_points = NULL; s->help_point_count = s->help_point_cap = 0;
  free(s->social_id); free(s->dogtag); free(s->help_image);
  s->social_id = s->dogtag = s->help_image = NULL;
}
bool qa_q2rr_player_extra_state_clone(qa_q2rr_player_extra_state *dst, const qa_q2rr_player_extra_state *src) {
  qa_vec3 *pts = NULL;
  if (src->help_point_count) {
    pts = malloc(src->help_point_count * sizeof *pts);
    if (!pts) return false;
    memcpy(pts, src->help_points, src->help_point_count * sizeof *pts);
  }
  char *social_id = qa_q2rr_dup(src->social_id ? src->social_id : "");
  char *dogtag = qa_q2rr_dup(src->dogtag ? src->dogtag : "");
  char *help_image = qa_q2rr_dup(src->help_image ? src->help_image : "");
  if (!social_id || !dogtag || !help_image) { free(pts); free(social_id); free(dogtag); free(help_image); return false; }
  *dst = *src;
  dst->help_points = pts;
  dst->help_point_cap = src->help_point_count;
  dst->social_id = social_id; dst->dogtag = dogtag; dst->help_image = help_image;
  return true;
}

qa_q2_rerelease_options qa_q2_create_rerelease_options(const qa_q2_rerelease_options_changes *c) {
  qa_q2_rerelease_options o = {0};
  o.coop_squad_respawn = true; o.coop_instanced_items = true; o.coop_lives = false; o.coop_num_lives = 2;
  o.deathmatch_spawn_farthest = true; o.auto_save_minimum_time = 60;
  if (c) {
    if (c->has_coop_squad_respawn) o.coop_squad_respawn = c->v.coop_squad_respawn;
    if (c->has_coop_instanced_items) o.coop_instanced_items = c->v.coop_instanced_items;
    if (c->has_coop_lives) o.coop_lives = c->v.coop_lives;
    if (c->has_coop_num_lives) o.coop_num_lives = c->v.coop_num_lives;
    if (c->has_deathmatch_force_respawn) o.deathmatch_force_respawn = c->v.deathmatch_force_respawn;
    if (c->has_deathmatch_no_fall_damage) o.deathmatch_no_fall_damage = c->v.deathmatch_no_fall_damage;
    if (c->has_deathmatch_spawn_farthest) o.deathmatch_spawn_farthest = c->v.deathmatch_spawn_farthest;
    if (c->has_deathmatch_force_respawn_time) o.deathmatch_force_respawn_time = c->v.deathmatch_force_respawn_time;
    if (c->has_deathmatch_allow_exit) o.deathmatch_allow_exit = c->v.deathmatch_allow_exit;
    if (c->has_coop_player_collision) o.coop_player_collision = c->v.coop_player_collision;
    if (c->has_auto_save_minimum_time) o.auto_save_minimum_time = c->v.auto_save_minimum_time;
  }
  return o;
}

bool qa_q2_is_n64(const qa_q2_game_options *o) { return o && o->map_name && strncmp(o->map_name, "q64/", 4) == 0; }
bool qa_q2_uses_instanced_items(qa_q2_rerelease_options o) { return o.coop_instanced_items || o.coop_squad_respawn; }
