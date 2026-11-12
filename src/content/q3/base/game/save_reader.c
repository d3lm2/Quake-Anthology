/* Port of src/content/q3/base/game/save-reader.ts. GPL-2.0-or-later.
 * Fields are read in the TS order, so the first invalid field reported is the
 * one TS reports. A raise caught by qa_try releases the partly read graph. */
#include "qa/content/q3/base/game/save_reader.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/save_values.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/foundation/support.h"
static char *dupstr(const char *v) { size_t n = strlen(v); char *d = malloc(n + 1); if (!d) qa_range_error("out of memory"); memcpy(d, v, n + 1); return d; }
static qa_save_reader field_of(const qa_save_reader *r, const char *n) { qa_save_reader f; if (!qa_save_reader_field(r, n, &f)) qa_range_error("out of memory"); return f; }
static double rf_int0(const qa_save_reader *r, const char *n) { qa_save_reader f = field_of(r, n); double v = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f); return v; }
static double rf_num(const qa_save_reader *r, const char *n) { qa_save_reader f = field_of(r, n); double v = qa_save_reader_number(&f); qa_save_reader_destroy(&f); return v; }
static qa_vec3 rf_vec(const qa_save_reader *r, const char *n) { qa_save_reader f = field_of(r, n); qa_vec3 v = qa_q3_read_vector(&f); qa_save_reader_destroy(&f); return v; }
qa_q3_saved_actor_ref qa_q3_read_actor(const qa_save_reader *reader) {
  qa_q3_saved_actor_ref a; a.present = true;
  a.slot = rf_int0(reader, "slot"); a.generation = rf_int0(reader, "generation");
  return a;
}
static qa_q3_saved_actor_ref nullable_actor(const qa_save_reader *r, const char *n) {
  qa_q3_saved_actor_ref out = {false, 0, 0}; qa_save_reader f = field_of(r, n);
  if (!qa_save_reader_is_null(&f)) out = qa_q3_read_actor(&f);
  qa_save_reader_destroy(&f); return out;
}
static qa_q3_base_trajectory read_traj(const qa_save_reader *r) {
  qa_q3_base_trajectory t; t.type = rf_num(r, "type"); t.time = rf_num(r, "time"); t.duration = rf_num(r, "duration");
  t.base = rf_vec(r, "base"); t.delta = rf_vec(r, "delta"); return t;
}
static double read_slot(const qa_save_reader *r, double max) {
  double v = qa_save_reader_integer_min(r, 0); if (v >= max) qa_save_reader_fail(r, "Q3 source reference outside retained table"); return v;
}
static bool nullable_slot(const qa_save_reader *r, const char *name, double max, double *out) {
  qa_save_reader f = field_of(r, name);
  if (qa_save_reader_is_null(&f)) { qa_save_reader_destroy(&f); return false; }
  *out = read_slot(&f, max); qa_save_reader_destroy(&f); return true;
}
static char *nullable_str(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field_of(r, name);
  if (qa_save_reader_is_null(&f)) { qa_save_reader_destroy(&f); return NULL; }
  char *v = dupstr(qa_save_reader_string(&f)); qa_save_reader_destroy(&f); return v;
}
static bool nullable_vec(const qa_save_reader *r, const char *name, qa_vec3 *out) {
  qa_save_reader f = field_of(r, name);
  if (qa_save_reader_is_null(&f)) { qa_save_reader_destroy(&f); return false; }
  *out = qa_q3_read_vector(&f); qa_save_reader_destroy(&f); return true;
}
/* TS entity(reader). `s` is zeroed and owned by the graph, so a raise leaves nothing unowned. */
static void read_entity(const qa_save_reader *r, qa_q3_game_save_entity_state *s) {
  static const char *const model_kinds[] = {"inline", "box", "capsule"}, *const name_kinds[] = {"value", "client-name"}, *const activation_kinds[] = {"entity", "actor"};
  qa_save_reader shared = field_of(r, "shared");
  qa_save_reader model = field_of(&shared, "model");
  qa_save_reader f = field_of(&model, "kind");
  const char *kind = qa_save_reader_choice_string(&f, model_kinds, 3); qa_save_reader_destroy(&f);
  qa_save_reader classname = field_of(r, "classname");
  f = field_of(&classname, "kind");
  const char *name_kind = qa_save_reader_choice_string(&f, name_kinds, 2); qa_save_reader_destroy(&f);
  f = field_of(r, "values"); s->values = qa_q3_read_entity_values(&f); qa_save_reader_destroy(&f);
  f = field_of(r, "network"); s->network = qa_q3_read_network_values(&f); qa_save_reader_destroy(&f);
  f = field_of(r, "pos"); s->pos = read_traj(&f); qa_save_reader_destroy(&f);
  f = field_of(r, "apos"); s->apos = read_traj(&f); qa_save_reader_destroy(&f);
  s->sv_flags = rf_num(&shared, "svFlags"); s->single_client = rf_num(&shared, "singleClient");
  s->contents = rf_num(&shared, "contents"); s->owner_num = rf_num(&shared, "ownerNum");
  s->model_kind = strcmp(kind, "inline") == 0 ? QA_Q3_BASE_MODEL_INLINE : strcmp(kind, "capsule") == 0 ? QA_Q3_BASE_MODEL_CAPSULE : QA_Q3_BASE_MODEL_BOX;
  if (s->model_kind == QA_Q3_BASE_MODEL_INLINE) s->model_index = rf_int0(&model, "index");
  qa_save_reader_destroy(&model); qa_save_reader_destroy(&shared);
  /* TS reads reader.field("sharedPrivate") once per member. */
  qa_save_reader priv = field_of(r, "sharedPrivate");
  s->has_abs_min_override = nullable_vec(&priv, "absMinOverride", &s->abs_min_override);
  s->has_abs_max_override = nullable_vec(&priv, "absMaxOverride", &s->abs_max_override);
  qa_save_reader link = field_of(&priv, "previousLink");
  if (!qa_save_reader_is_null(&link)) {
    qa_save_reader state = field_of(&link, "state");
    s->has_previous_link = true;
    f = field_of(&link, "actor"); s->previous_link_actor = qa_q3_read_actor(&f); qa_save_reader_destroy(&f);
    s->previous_link_count = rf_int0(&link, "linkCount");
    f = field_of(&link, "absoluteBounds"); s->previous_link_absolute_bounds.min = rf_vec(&f, "min"); qa_save_reader_destroy(&f);
    f = field_of(&link, "absoluteBounds"); s->previous_link_absolute_bounds.max = rf_vec(&f, "max"); qa_save_reader_destroy(&f);
    s->previous_link_origin = rf_vec(&state, "origin"); s->previous_link_angles = rf_vec(&state, "angles"); s->previous_link_velocity = rf_vec(&state, "velocity");
    f = field_of(&state, "bounds"); s->previous_link_bounds.min = rf_vec(&f, "min"); qa_save_reader_destroy(&f);
    f = field_of(&state, "bounds"); s->previous_link_bounds.max = rf_vec(&f, "max"); qa_save_reader_destroy(&f);
    s->previous_link_ground = nullable_actor(&state, "ground");
    qa_save_reader_destroy(&state);
  }
  qa_save_reader_destroy(&link); qa_save_reader_destroy(&priv);
  s->has_client = nullable_slot(r, "client", 64, &s->client);
  if (strcmp(name_kind, "value") == 0) s->classname = nullable_str(&classname, "value");
  else { f = field_of(&classname, "client"); s->classname_is_client = true; s->classname_client = read_slot(&f, 64); qa_save_reader_destroy(&f); }
  qa_save_reader_destroy(&classname);
  s->has_parent = nullable_slot(r, "parent", 1024, &s->parent); s->has_next_train = nullable_slot(r, "nextTrain", 1024, &s->next_train);
  s->has_prev_train = nullable_slot(r, "prevTrain", 1024, &s->prev_train); s->has_target_ent = nullable_slot(r, "targetEnt", 1024, &s->target_ent);
  s->has_chain = nullable_slot(r, "chain", 1024, &s->chain); s->has_enemy = nullable_slot(r, "enemy", 1024, &s->enemy);
  s->has_activator = nullable_slot(r, "activator", 1024, &s->activator); s->has_teamchain = nullable_slot(r, "teamchain", 1024, &s->teamchain);
  s->has_teammaster = nullable_slot(r, "teammaster", 1024, &s->teammaster);
  qa_save_reader activation = field_of(r, "activation");
  if (!qa_save_reader_is_null(&activation)) {
    f = field_of(&activation, "kind");
    const char *activation_kind = qa_save_reader_choice_string(&f, activation_kinds, 2); qa_save_reader_destroy(&f);
    if (strcmp(activation_kind, "entity") == 0) {
      f = field_of(&activation, "slot"); s->activation_slot = read_slot(&f, 1024); s->activation_kind = 1; qa_save_reader_destroy(&f);
    } else { f = field_of(&activation, "actor"); s->activation_actor = qa_q3_read_actor(&f); s->activation_kind = 2; qa_save_reader_destroy(&f); }
  }
  qa_save_reader_destroy(&activation);
  f = field_of(r, "item");
  if (!qa_save_reader_is_null(&f)) { s->item = qa_save_reader_integer_min(&f, 0); s->has_item = true; }
  qa_save_reader_destroy(&f);
  s->nextthink = rf_num(r, "nextthink");
  s->think = nullable_str(r, "think"); s->reached = nullable_str(r, "reached"); s->blocked = nullable_str(r, "blocked");
  s->touch = nullable_str(r, "touch"); s->use = nullable_str(r, "use"); s->pain = nullable_str(r, "pain"); s->die = nullable_str(r, "die");
}
/* TS numbers(reader). The array is stored before its items are read, so a raise leaves it owned. */
static void read_numbers(const qa_save_reader *r, const char *name, double **out, size_t *count) {
  qa_save_reader f = field_of(r, name);
  size_t n = qa_save_reader_list_count(&f); double *d = malloc((n ? n : 1) * sizeof *d); if (!d) qa_range_error("out of memory");
  *out = d; *count = n;
  for (size_t i = 0; i < n; i++) { qa_save_reader it; if (!qa_save_reader_item(&f, i, &it)) qa_range_error("out of memory"); d[i] = qa_save_reader_number(&it); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&f);
}
/* TS client(reader). */
static void read_client(const qa_save_reader *r, qa_q3_game_save_client_state *s) {
  qa_save_reader command = field_of(r, "command"), backing = field_of(r, "backing"), f;
  f = field_of(r, "values"); s->values = qa_q3_read_client_values(&f); qa_save_reader_destroy(&f);
  f = field_of(r, "player"); s->player = qa_q3_read_player_values(&f); qa_save_reader_destroy(&f);
  f = field_of(r, "persistant"); s->persistant = qa_q3_read_persistant_values(&f); qa_save_reader_destroy(&f);
  s->command.server_time = rf_num(&command, "serverTime"); s->command.angles = rf_vec(&command, "angles");
  s->command.buttons = rf_num(&command, "buttons"); s->command.weapon = rf_num(&command, "weapon");
  s->command.forwardmove = rf_num(&command, "forwardmove"); s->command.rightmove = rf_num(&command, "rightmove"); s->command.upmove = rf_num(&command, "upmove");
  qa_save_reader_destroy(&command);
  f = field_of(r, "team"); s->team = qa_q3_read_team_values(&f); qa_save_reader_destroy(&f);
  f = field_of(r, "session"); s->session = qa_q3_read_session_values(&f); qa_save_reader_destroy(&f);
  read_numbers(r, "events", &s->events, &s->event_count); read_numbers(r, "eventParms", &s->event_parms, &s->event_parm_count);
  read_numbers(r, "persistantSlots", &s->persistant_slots, &s->persistant_count); read_numbers(r, "powerups", &s->powerups, &s->powerup_count);
  read_numbers(r, "ammoTimes", &s->ammo_times, &s->ammo_time_count);
  /* The reader accepts any length; restoreClientBacking (prepareQ3Graph) rejects a count other than 16. */
  read_numbers(&backing, "sourceStats", &s->source_stats, &s->source_stat_count);
  read_numbers(&backing, "specialAmmo", &s->special_ammo, &s->special_ammo_count);
  qa_save_reader_destroy(&backing);
  s->has_hook = nullable_slot(r, "hook", 1024, &s->hook);
  s->has_persistant_powerup = nullable_slot(r, "persistantPowerup", 1024, &s->persistant_powerup);
  f = field_of(r, "areabits");
  if (!qa_save_reader_is_null(&f)) {
    size_t length = 0; const uint8_t *bytes = qa_save_reader_bytes(&f, &length);
    s->areabits = malloc(length ? length : 1); if (!s->areabits) qa_range_error("out of memory");
    if (length) memcpy(s->areabits, bytes, length);
    s->areabits_length = length; s->has_areabits = true;
  }
  qa_save_reader_destroy(&f);
}
static void destroy_graph(void *arg) { qa_q3_graph_state_destroy(arg); }
qa_q3_graph_state qa_q3_read_graph(const qa_save_value *value) {
  qa_save_reader reader; if (!qa_save_reader_init(&reader, value, "q3.graph")) qa_range_error("out of memory");
  qa_q3_graph_state s; memset(&s, 0, sizeof s);
  qa_cleanup guard; qa_cleanup_push(&guard, destroy_graph, &s);
  qa_save_reader f = field_of(&reader, "ownership"); size_t count = qa_save_reader_list_count(&f);
  s.ownership = calloc(count ? count : 1, sizeof *s.ownership); if (!s.ownership) qa_range_error("out of memory");
  s.ownership_count = count;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader it; if (!qa_save_reader_item(&f, i, &it)) qa_range_error("out of memory");
    qa_save_reader af = field_of(&it, "actor"); s.ownership[i].has_actor = !qa_save_reader_is_null(&af);
    if (s.ownership[i].has_actor) s.ownership[i].actor = qa_q3_read_actor(&af);
    qa_save_reader_destroy(&af);
    qa_save_reader bf = field_of(&it, "active"); s.ownership[i].active = qa_save_reader_boolean(&bf); qa_save_reader_destroy(&bf);
    bf = field_of(&it, "borrowed"); s.ownership[i].borrowed = qa_save_reader_boolean(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "entities"); count = qa_save_reader_list_count(&f);
  s.entities = calloc(count ? count : 1, sizeof *s.entities); if (!s.entities) qa_range_error("out of memory");
  s.entity_count = count;
  for (size_t i = 0; i < count; i++) { qa_save_reader it; if (!qa_save_reader_item(&f, i, &it)) qa_range_error("out of memory"); read_entity(&it, &s.entities[i]); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "clients"); count = qa_save_reader_list_count(&f);
  s.clients = calloc(count ? count : 1, sizeof *s.clients); if (!s.clients) qa_range_error("out of memory");
  s.client_count = count;
  for (size_t i = 0; i < count; i++) { qa_save_reader it; if (!qa_save_reader_item(&f, i, &it)) qa_range_error("out of memory"); read_client(&it, &s.clients[i]); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "numEntities"); s.num_entities = qa_save_reader_integer_min(&f, 64); qa_save_reader_destroy(&f);
  f = field_of(&reader, "maxClients"); s.max_clients = qa_save_reader_integer_min(&f, 1); qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&guard);
  return s;
}
