/* Port of src/app/bootstrap/simulation/qvm-weapon-behavior.ts. GPL-2.0-or-later.
 * TS Map/Set insertion order is kept by the ordered arrays below. */
#include "qa/app/bootstrap/simulation/qvm_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/qvm_component_state.h"
#include <math.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/core/common_parse.h"
#include "qa/core/cvars/index.h"
#include "qa/core/numeric.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/types.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#include "qa/world/gameplay/weapon_behaviors.h"

static noreturn void fail_error(const char *format, ...) {
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
  va_list ap; va_start(ap, format); vsnprintf(message, sizeof message, format, ap); va_end(ap);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *checked(void *pointer) { if (!pointer) qa_range_error("Out of memory"); return pointer; }
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
#define GROW(array, count, capacity) do { if ((count) == (capacity)) { (capacity) = (capacity) ? (capacity) * 2 : 8; \
  (array) = checked(realloc((array), (capacity) * sizeof *(array))); } } while (0)

typedef struct binding { qa_actor_id actor; int32_t pointer; const char *kind; /* "target"|"client"|"projectile" */ } binding;
typedef struct retired_entry { qa_actor_id actor; qa_weapon_trajectory_update trajectory; } retired_entry;
typedef struct actor_set { qa_actor_id *values; size_t count, capacity; } actor_set;
/* An instance is owned by the source until its holder closes it (the attachments close each
 * instance once and then drop it), so close frees it. */
typedef struct qvm_instance {
  qa_app_sim_qvm_weapon_behavior_source *source;
  qa_actor_id actor; double generation; bool closed;
  struct qvm_instance *next, *previous;
} qvm_instance;

struct qa_app_sim_qvm_weapon_behavior_source {
  qa_app_sim_qvm_weapon_behavior_options options;
  qa_qvm_weapon_profile profile; bool has_profile;
  qa_weapon_behavior_definition definition;
  qa_qvm_game game; bool has_game;
  qa_cvar_registry *cvars;
  qa_qvm_files *files;
  binding *bindings; size_t binding_count, binding_capacity;
  retired_entry *retired; size_t retired_count, retired_capacity;
  actor_set instances, activated;
  qa_app_sim_qvm_component_state state; /* TS QvmComponentState: configstrings, userinfo, parser, cursor */
  qa_qvm_server_game_services services;
  qa_app_sim_qvm_component_spatial spatial; /* qvmComponentSpatial(...) */
  qa_qvm_host_state host_state;
  uint8_t *host_bytes; /* last host checkpoint encoding (module copies it) */
  double time;
  bool ready, closed, busy;
  double generation;
  qvm_instance *instance_list;
};
typedef qa_app_sim_qvm_weapon_behavior_source source_t;

/* ---- ordered collections ---- */
static bool aset_has(const actor_set *s, qa_actor_id a) { for (size_t i = 0; i < s->count; i++) if (qa_same_actor(s->values[i], a)) return true; return false; }
static void aset_add(actor_set *s, qa_actor_id a) { if (aset_has(s, a)) return; GROW(s->values, s->count, s->capacity); s->values[s->count++] = a; }
static void aset_delete(actor_set *s, qa_actor_id a) {
  for (size_t i = 0; i < s->count; i++) if (qa_same_actor(s->values[i], a)) { memmove(&s->values[i], &s->values[i + 1], (s->count - i - 1) * sizeof *s->values); s->count--; return; }
}
static binding *binding_get(source_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->binding_count; i++) if (qa_same_actor(s->bindings[i].actor, actor)) return &s->bindings[i]; return NULL; }
static void binding_set(source_t *s, binding value) {
  binding *existing = binding_get(s, value.actor); if (existing) { *existing = value; return; }
  GROW(s->bindings, s->binding_count, s->binding_capacity); s->bindings[s->binding_count++] = value;
}
static void binding_delete(source_t *s, qa_actor_id actor) {
  binding *b = binding_get(s, actor); if (!b) return;
  size_t i = (size_t)(b - s->bindings); memmove(&s->bindings[i], &s->bindings[i + 1], (s->binding_count - i - 1) * sizeof *s->bindings); s->binding_count--;
}
static retired_entry *retired_get(source_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->retired_count; i++) if (qa_same_actor(s->retired[i].actor, actor)) return &s->retired[i]; return NULL; }
static void retired_set(source_t *s, qa_actor_id actor, qa_weapon_trajectory_update trajectory) {
  retired_entry *existing = retired_get(s, actor); if (existing) { existing->trajectory = trajectory; return; }
  GROW(s->retired, s->retired_count, s->retired_capacity); s->retired[s->retired_count++] = (retired_entry){actor, trajectory};
}
static void retired_delete(source_t *s, qa_actor_id actor) {
  retired_entry *r = retired_get(s, actor); if (!r) return;
  size_t i = (size_t)(r - s->retired); memmove(&s->retired[i], &s->retired[i + 1], (s->retired_count - i - 1) * sizeof *s->retired); s->retired_count--;
}
static const char *userinfo_get(source_t *s, double slot) { return qa_app_sim_qvm_component_texts_get(&s->state.userinfo, slot); }
static void userinfo_set(source_t *s, double slot, const char *value) { qa_app_sim_qvm_component_texts_set(&s->state.userinfo, slot, value); }
static void userinfo_delete(source_t *s, double slot) { qa_app_sim_qvm_component_texts_delete(&s->state.userinfo, slot); }
static void userinfo_clear(source_t *s) { qa_app_sim_qvm_component_texts_clear(&s->state.userinfo); }

/* ---- guest record helpers ---- */
static void current(source_t *s) {
  s->options.assert_current(s->options.ctx);
  if (s->closed) fail_error("QVM weapon source is closed");
}
static void validate_layout(source_t *s) {
  if (qa_qvm_game_data_get_entity_stride_bytes(&s->game.data) != (double)s->profile.entity_stride) fail_error("QVM weapon private layout differs from located source data");
}
static int64_t base_offset(source_t *s, qa_qvm_writable_view *view) { (void)s; return view->start; }
static double slot_of(source_t *s, int32_t pointer) {
  double slot = qa_qvm_game_data_number_from_pointer(&s->game.data, pointer);
  if (pointer <= 0) fail_error("Noncanonical QVM entity pointer");
  qa_qvm_writable_view view; qa_qvm_game_data_entity_bytes(&s->game.data, slot, &view);
  if (base_offset(s, &view) != pointer) fail_error("Noncanonical QVM entity pointer");
  return slot;
}
static bool live(source_t *s, int32_t pointer) {
  qa_qvm_writable_view view; qa_qvm_game_data_entity_bytes(&s->game.data, slot_of(s, pointer), &view);
  return qa_qvm_writable_view_get_int32(&view, s->profile.field_inuse, true) != 0;
}
static void project(source_t *s, int32_t pointer, qa_body_state body) {
  qa_qvm_shared_entity *entity = qa_qvm_game_data_entity_from_pointer(&s->game.data, pointer);
  qa_qvm_shared_entity_set_current_origin(entity, body.origin); qa_qvm_shared_entity_set_current_angles(entity, body.angles);
  qa_qvm_shared_entity_set_mins(entity, body.bounds.min); qa_qvm_shared_entity_set_maxs(entity, body.bounds.max);
  qa_qvm_entity_state_set_origin(&entity->s, body.origin); qa_qvm_entity_state_set_angles(&entity->s, body.angles);
  qa_qvm_trajectory pos = qa_qvm_entity_state_get_pos(&entity->s);
  pos.base = body.origin; pos.delta = body.velocity; pos.time = qa_js_round(s->time * 1000);
  qa_qvm_entity_state_set_pos(&entity->s, pos);
}
static qa_weapon_trajectory_update trajectory(source_t *s, int32_t pointer) {
  qa_qvm_shared_entity *entity = qa_qvm_game_data_entity_from_pointer(&s->game.data, pointer);
  qa_weapon_trajectory_update out;
  out.origin = qa_qvm_shared_entity_get_current_origin(entity);
  out.velocity = qa_qvm_entity_state_get_pos(&entity->s).delta;
  out.angles = qa_qvm_shared_entity_get_current_angles(entity);
  return out;
}
static const qa_actor_id *actor_for_slot(source_t *s, double slot, qa_actor_id *storage) {
  for (size_t i = 0; i < s->binding_count; i++) if (slot_of(s, s->bindings[i].pointer) == slot) { *storage = s->bindings[i].actor; return storage; }
  return NULL;
}
static int32_t call(source_t *s, const int32_t *words, size_t count, int64_t instruction) { return qa_qvm_module_call(&s->game.module, words, count, instruction); }

/* ---- mirror and release ---- */
typedef struct connect_call { source_t *s; double slot; } connect_call;
static void run_connect(void *ctx) {
  connect_call *c = ctx;
  char *denial = qa_qvm_game_client_connect(&c->s->game, c->slot, true, false);
  if (denial != NULL) {
    char message[QA_CAUGHT_MESSAGE_CAPACITY]; snprintf(message, sizeof message, "%s", denial); free(denial);
    fail_error("QVM weapon source rejected client: %s", message);
  }
  qa_qvm_game_client_begin(&c->s->game, c->slot);
}
static double team_number(const char *team) {
  if (is(team, "red")) return 1;
  if (is(team, "blue")) return 2;
  if (is(team, "spectator")) return 3;
  return 0;
}
static const qa_app_sim_qvm_weapon_target *find_target(source_t *s, qa_actor_id actor) {
  size_t count = 0; const qa_app_sim_qvm_weapon_target *targets = s->options.targets(s->options.ctx, &count);
  for (size_t i = 0; i < count; i++) if (qa_same_actor(targets[i].actor, actor)) return &targets[i];
  return NULL;
}
static int32_t mirror(source_t *s, qa_actor_id actor, const qa_app_sim_qvm_weapon_target *known) {
  const qa_app_sim_qvm_weapon_target *found = known ? known : find_target(s, actor);
  if (found == NULL) fail_error("QVM weapon query refers to an unavailable shared actor");
  qa_app_sim_qvm_weapon_target target = *found;
  bool player = is(target.kind, "player");
  binding *bound = binding_get(s, actor);
  if (bound == NULL) {
    if (player) {
      double slot = 0; while (slot < 64 && userinfo_get(s, slot)) slot++;
      if (slot == 64) fail_error("QVM weapon source exhausted its admitted client capacity");
      qa_qvm_writable_view record; qa_qvm_game_data_entity_bytes(&s->game.data, slot, &record);
      int32_t pointer = (int32_t)base_offset(s, &record);
      if (pointer == 0) fail_error("QVM client entity cannot use a null pointer");
      binding_set(s, (binding){actor, pointer, "client"}); userinfo_set(s, slot, target.userinfo);
      connect_call c = {s, slot}; qa_caught_error caught;
      if (!qa_try(run_connect, &c, &caught)) { binding_delete(s, actor); userinfo_delete(s, slot); qa_rethrow(&caught); }
    } else {
      int32_t pointer = call(s, NULL, 0, s->profile.allocate); slot_of(s, pointer);
      bool occupied = false; for (size_t i = 0; i < s->binding_count; i++) if (s->bindings[i].pointer == pointer) occupied = true;
      if (!live(s, pointer) || occupied) fail_error("QVM source allocated an occupied or inactive entity");
      binding_set(s, (binding){actor, pointer, "target"});
    }
    bound = binding_get(s, actor);
  }
  binding current_binding = *bound;
  if (player) {
    if (!is(current_binding.kind, "client")) fail_error("QVM mirrored actor changed client ownership");
    double slot = slot_of(s, current_binding.pointer);
    const char *info = userinfo_get(s, slot);
    if (info == NULL || !is(info, target.userinfo)) { userinfo_set(s, slot, target.userinfo); qa_qvm_game_client_userinfo_changed(&s->game, slot); }
    double desired = team_number(target.team);
    if (!is(target.team, "spectator") && (s->options.team_mode ? is(target.team, "free") : !is(target.team, "free")))
      fail_error("QVM player team differs from selected source team mode");
    /* PERS_TEAM is public playerState.persistant[3] in both supported source ABIs. */
    qa_qvm_writable_view ps; qa_qvm_game_data_public_player_bytes(&s->game.data, slot, &ps);
    if (qa_qvm_writable_view_get_int32(&ps, 260, true) != desired) {
      const char *arguments[2] = {"team", target.team};
      qa_qvm_game_client_command(&s->game, slot, arguments, 2);
    }
    qa_qvm_game_data_public_player_bytes(&s->game.data, slot, &ps);
    if (qa_qvm_writable_view_get_int32(&ps, 260, true) != desired) fail_error("QVM source did not admit the requested %s team", target.team);
    const struct { int64_t offset; qa_vec3 vector; } rows[2] = {{20, target.body.origin}, {152, target.body.angles}};
    for (int i = 0; i < 2; i++) {
      qa_qvm_writable_view_set_float32(&ps, rows[i].offset, rows[i].vector.x, true);
      qa_qvm_writable_view_set_float32(&ps, rows[i].offset + 4, rows[i].vector.y, true);
      qa_qvm_writable_view_set_float32(&ps, rows[i].offset + 8, rows[i].vector.z, true);
    }
    qa_qvm_writable_view_set_int32(&ps, 184, qa_js_to_int32(trunc(target.health)), true);
  }
  project(s, current_binding.pointer, target.body);
  qa_qvm_writable_view fields; qa_qvm_game_data_entity_bytes(&s->game.data, slot_of(s, current_binding.pointer), &fields);
  qa_qvm_writable_view_set_int32(&fields, s->profile.field_health, qa_js_to_int32(trunc(target.health)), true);
  return current_binding.pointer;
}
typedef struct disconnect_call { source_t *s; double slot; } disconnect_call;
static void run_disconnect(void *ctx) { disconnect_call *c = ctx; qa_qvm_game_client_disconnect(&c->s->game, c->slot); }
static void release(source_t *s, qa_actor_id actor) {
  retired_delete(s, actor); aset_delete(&s->activated, actor);
  binding *bound = binding_get(s, actor); if (bound == NULL) return;
  binding value = *bound; binding_delete(s, actor);
  if (is(value.kind, "client")) {
    double slot = slot_of(s, value.pointer);
    disconnect_call c = {s, slot}; qa_caught_error caught;
    bool ok = qa_try(run_disconnect, &c, &caught);
    userinfo_delete(s, slot);
    if (!ok) qa_rethrow(&caught);
  } else if (live(s, value.pointer)) { int32_t words[1] = {value.pointer}; call(s, words, 1, s->profile.free_entry); }
}
typedef struct target_copy { qa_app_sim_qvm_weapon_target *values; size_t count; binding *snapshot; } target_copy;
static void target_copy_free(void *arg) { target_copy *c = arg; free(c->values); free(c->snapshot); }
static void refresh_targets(source_t *s, const qa_actor_id *exclude) {
  size_t count = 0; const qa_app_sim_qvm_weapon_target *listed = s->options.targets(s->options.ctx, &count);
  target_copy copy = {checked(malloc((count ? count : 1) * sizeof *copy.values)), count, NULL};
  if (count) memcpy(copy.values, listed, count * sizeof *copy.values);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, target_copy_free, &copy);
  size_t snapshot_count = s->binding_count;
  copy.snapshot = checked(malloc((snapshot_count ? snapshot_count : 1) * sizeof *copy.snapshot));
  if (snapshot_count) memcpy(copy.snapshot, s->bindings, snapshot_count * sizeof *copy.snapshot);
  for (size_t i = 0; i < snapshot_count; i++) {
    if (is(copy.snapshot[i].kind, "projectile")) continue;
    bool present = false; for (size_t t = 0; t < count && !present; t++) present = qa_same_actor(copy.values[t].actor, copy.snapshot[i].actor);
    if (!present) release(s, copy.snapshot[i].actor);
  }
  for (size_t t = 0; t < count; t++) {
    if (exclude && qa_same_actor(copy.values[t].actor, *exclude)) continue;
    binding *bound = binding_get(s, copy.values[t].actor);
    if (bound && is(bound->kind, "projectile")) continue;
    mirror(s, copy.values[t].actor, &copy.values[t]);
  }
  qa_cleanup_pop(&cleanup); target_copy_free(&copy);
}

/* ---- operations ---- */
typedef struct operation_call { source_t *s; void (*run)(void *ctx); void *ctx; } operation_call;
static void operation(source_t *s, void (*run)(void *ctx), void *ctx) {
  current(s); if (!s->ready || s->busy) fail_error("QVM weapon source is unready or already executing");
  s->busy = true; qa_caught_error caught;
  bool ok = qa_try(run, ctx, &caught);
  s->busy = false;
  if (!ok) qa_rethrow(&caught);
}
static void set_time(source_t *s, double time) {
  double milliseconds = qa_js_round(time * 1000);
  if (!isfinite(time) || time < s->time || milliseconds > 0x7fffffff) fail_error("Invalid QVM weapon source time");
  s->time = time;
  qa_qvm_writable_view view; qa_qvm_memory_data_view(s->game.module.memory, s->profile.level_time, 4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32(milliseconds), true);
}
static bool entry_is_function(source_t *s, int32_t entry) {
  return entry > 0 && (size_t)entry < s->options.artifact->image.instruction_count && s->options.artifact->image.instructions[entry].opcode == QA_QVM_OP_ENTER;
}

static qa_weapon_trajectory_update_result instance_step(void *ctx, qa_body_state body, double time);
static void instance_close(void *ctx);
static qa_weapon_behavior_instance make_instance(source_t *s, qa_actor_id actor) {
  if (aset_has(&s->instances, actor)) fail_error("QVM projectile already has an instance");
  binding *bound = binding_get(s, actor); retired_entry *retired = retired_get(s, actor);
  qa_weapon_trajectory_update initial; bool has_initial = true;
  if (retired) initial = retired->trajectory; else if (bound) initial = trajectory(s, bound->pointer); else has_initial = false;
  if (!has_initial || (bound != NULL && !is(bound->kind, "projectile"))) fail_error("No retained QVM trajectory projectile");
  aset_add(&s->instances, actor);
  qvm_instance *instance = checked(calloc(1, sizeof *instance));
  instance->source = s; instance->actor = actor; instance->generation = s->generation; instance->next = s->instance_list;
  if (s->instance_list) s->instance_list->previous = instance;
  s->instance_list = instance;
  qa_weapon_behavior_instance out; memset(&out, 0, sizeof out);
  out.ctx = instance; out.definition = s->definition; out.initial = initial; out.step = instance_step; out.close = instance_close;
  return out;
}
typedef struct step_call { qvm_instance *instance; qa_body_state body; double time; qa_weapon_trajectory_update_result result; } step_call;
static void retire_step(step_call *c) {
  source_t *s = c->instance->source;
  qa_weapon_trajectory_update t = {c->body.origin, c->body.velocity, c->body.angles};
  retired_set(s, c->instance->actor, t); binding_delete(s, c->instance->actor);
}
static void run_step(void *ctx) {
  step_call *c = ctx; qvm_instance *instance = c->instance; source_t *s = instance->source;
  if (instance->closed || instance->generation != s->generation) fail_error("QVM trajectory instance is closed");
  set_time(s, c->time); refresh_targets(s, NULL);
  binding *bound = binding_get(s, instance->actor); if (bound == NULL) return;
  int32_t pointer = bound->pointer;
  if (!live(s, pointer)) { retire_step(c); return; }
  project(s, pointer, c->body);
  qa_qvm_writable_view fields; qa_qvm_game_data_entity_bytes(&s->game.data, slot_of(s, pointer), &fields);
  int32_t next = qa_qvm_writable_view_get_int32(&fields, s->profile.field_nextthink, true);
  if (next <= 0 || next > qa_js_round(c->time * 1000)) return;
  int32_t entry = qa_qvm_writable_view_get_int32(&fields, s->profile.field_think, true);
  if (!entry_is_function(s, entry)) fail_error("Invalid source projectile think callback");
  qa_qvm_writable_view_set_int32(&fields, s->profile.field_nextthink, 0, true);
  int32_t words[1] = {pointer}; call(s, words, 1, entry);
  if (!live(s, pointer)) { retire_step(c); return; }
  if (qa_qvm_entity_state_get_e_type(&qa_qvm_game_data_entity_from_pointer(&s->game.data, pointer)->s) != 3) {
    call(s, words, 1, s->profile.free_entry); retire_step(c); return;
  }
  c->result.has_value = true; c->result.value = trajectory(s, pointer);
}
static qa_weapon_trajectory_update_result instance_step(void *ctx, qa_body_state body, double time) {
  qvm_instance *instance = ctx;
  step_call c; memset(&c, 0, sizeof c); c.instance = instance; c.body = body; c.time = time;
  operation(instance->source, run_step, &c);
  return c.result;
}
static void unlink_instance(void *ctx) {
  qvm_instance *instance = ctx; source_t *s = instance->source;
  if (instance->previous) instance->previous->next = instance->next; else s->instance_list = instance->next;
  if (instance->next) instance->next->previous = instance->previous;
  free(instance);
}
static void instance_close(void *ctx) {
  qvm_instance *instance = ctx; source_t *s = instance->source;
  if (instance->closed) return;
  instance->closed = true;
  if (instance->generation != s->generation) { unlink_instance(instance); return; }
  qa_actor_id actor = instance->actor;
  /* The holder drops the instance even when release raises. */
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, unlink_instance, instance);
  aset_delete(&s->instances, actor); release(s, actor);
  qa_cleanup_pop(&cleanup); unlink_instance(instance);
}

typedef struct attach_call { source_t *s; qa_weapon_behavior_launch launch; qa_weapon_behavior_instance result; } attach_call;
static void run_attach(void *ctx) {
  attach_call *c = ctx; source_t *s = c->s; qa_weapon_behavior_launch launch = c->launch;
  set_time(s, launch.time_seconds); refresh_targets(s, &launch.projectile.id);
  if (!is(launch.role, s->definition.role) || binding_get(s, launch.projectile.id)) fail_error("QVM weapon launch role or ownership differs");
  int32_t shooter = mirror(s, launch.shooter, NULL);
  qa_qvm_shared_entity *entity = qa_qvm_game_data_entity_from_pointer(&s->game.data, shooter);
  if (s->definition.has_activate && !aset_has(&s->activated, launch.shooter)) {
    if (s->definition.activate.kind != QA_WEAPON_CALLBACK_QVM) fail_error("Invalid QVM activation identity");
    int32_t words[1] = {shooter}; call(s, words, 1, (int64_t)s->definition.activate.instruction_index);
    aset_add(&s->activated, launch.shooter);
  }
  qa_vec3 v = launch.body.velocity;
  double length = qa_js_hypot3(v.x, v.y, v.z);
  if (!(length > 0)) fail_error("QVM source firing requires a projectile direction");
  qa_vec3 direction = {v.x / length, v.y / length, v.z / length};
  qa_qvm_shared_entity_set_current_origin(entity, launch.body.origin);
  qa_qvm_trajectory pos = qa_qvm_entity_state_get_pos(&entity->s); pos.delta = direction; qa_qvm_entity_state_set_pos(&entity->s, pos);
  const char *abi = s->options.artifact->has_abi_profile ? s->options.artifact->abi_profile : "q3-modern";
  int32_t start_offset = is(abi, "q3-modern") ? 488 : 476;
  if (s->definition.fire.kind != QA_WEAPON_CALLBACK_QVM) fail_error("Invalid QVM fire identity");
  int32_t words[3] = {shooter, shooter + start_offset, shooter + 36};
  int32_t pointer = call(s, words, 3, (int64_t)s->definition.fire.instruction_index);
  slot_of(s, pointer);
  bool occupied = false; for (size_t i = 0; i < s->binding_count; i++) if (s->bindings[i].pointer == pointer) occupied = true;
  if (!live(s, pointer) || qa_qvm_entity_state_get_e_type(&qa_qvm_game_data_entity_from_pointer(&s->game.data, pointer)->s) != 3 || occupied)
    fail_error("QVM fire did not return a new owned missile");
  binding_set(s, (binding){launch.projectile.id, pointer, "projectile"});
  c->result = make_instance(s, launch.projectile.id);
}
qa_weapon_behavior_instance qa_app_sim_qvm_weapon_behavior_source_attach(source_t *s, qa_weapon_behavior_launch launch) {
  attach_call c; memset(&c, 0, sizeof c); c.s = s; c.launch = launch;
  operation(s, run_attach, &c);
  return c.result;
}
qa_weapon_behavior_instance qa_app_sim_qvm_weapon_behavior_source_resume(source_t *s, qa_actor_id actor) { current(s); return make_instance(s, actor); }

/* ---- host ---- */
static void reject_command(void *ctx, const char *text) { (void)ctx; (void)text; fail_error("QVM weapon callback cannot execute server commands"); }
static void forward_print(void *ctx, const char *text) { source_t *s = ctx; s->options.print(s->options.ctx, text); }
static double host_milliseconds(void *ctx) { source_t *s = ctx; return qa_js_round(s->time * 1000); }
static const char *const *host_arguments(void *ctx, size_t *out_count) { (void)ctx; *out_count = 0; return NULL; }
static double host_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) { source_t *s = ctx; return s->options.real_time(s->options.ctx, has_output, out); }
static qa_qvm_host_result host(void *ctx, const qa_qvm_host_call *call_in) {
  source_t *s = ctx;
  current(s);
  qa_qvm_common_services common; memset(&common, 0, sizeof common);
  common.role = "qagame"; common.cvars = s->cvars; common.ctx = s; common.print = forward_print; common.milliseconds = host_milliseconds;
  common.arguments = host_arguments; common.real_time = host_real_time;
  common.commands.ctx = s; common.commands.execute_now = reject_command; common.commands.append = reject_command; common.commands.insert = reject_command;
  qa_qvm_host_result result = 0;
  if (qa_qvm_common_syscall(call_in, common, &result)) return result;
  if (qa_qvm_file_syscall(call_in, s->files, &result)) return result;
  if (qa_qvm_server_game_syscall(call_in, &s->services, &result)) return result;
  if (is(call_in->kind, "engine") && is(call_in->role, "qagame")
      && (call_in->code == QA_QVM_GAME_IMPORT_BOTLIB_SETUP || call_in->code == QA_QVM_GAME_IMPORT_BOTLIB_AAS_INITIALIZED)) return 0;
  qa_qvm_reject_syscall(call_in);
}
static const char *services_configstring_get(void *ctx, int32_t index) { source_t *s = ctx; const char *value = qa_app_sim_qvm_component_texts_get(&s->state.configstrings, index); return value ? value : ""; }
static void services_configstring_set(void *ctx, int32_t index, const char *value) { source_t *s = ctx; qa_app_sim_qvm_component_texts_set(&s->state.configstrings, index, value); }
static const char *services_get_userinfo(void *ctx, double slot) { const char *value = userinfo_get(ctx, slot); return value ? value : ""; }
static void services_set_userinfo(void *ctx, double slot, const char *value) { userinfo_set(ctx, slot, value); }
static qa_qvmx_wire_user_command services_user_command(void *ctx, double slot) {
  source_t *s = ctx; (void)slot;
  qa_qvmx_wire_user_command out; memset(&out, 0, sizeof out);
  out.server_time = qa_js_round(s->time * 1000); return out;
}
static void services_drop_client(void *ctx, double slot, const char *reason) { (void)ctx; (void)slot; (void)reason; fail_error("QVM trajectory component cannot drop a primary client"); }
static void services_send_server_command(void *ctx, double slot, const char *text) { (void)ctx; (void)slot; (void)text; }
static qa_qvm_entity_token_result services_entity_token(void *ctx) {
  source_t *s = ctx; qa_qvm_entity_token_result out;
  out.token = qa_common_parse_state_parse(&s->state.parser, &s->state.cursor, true); out.ended = !s->state.cursor.has_offset;
  return out;
}
/* qvmComponentSpatial options: actorForSlot, slotOf(actor) = slot(mirror(actor)) and reject(operation) = forbidden(operation). */
static bool component_actor_for_slot(void *ctx, double slot, qa_actor_id *out) {
  qa_actor_id storage; const qa_actor_id *actor = actor_for_slot(ctx, slot, &storage);
  if (actor == NULL) return false;
  *out = *actor; return true;
}
static double component_slot_of(void *ctx, qa_actor_id actor) { source_t *s = ctx; return slot_of(s, mirror(s, actor, NULL)); }
static void component_reject(void *ctx, const char *operation) { (void)ctx; fail_error("QVM trajectory component cannot %s", operation); }

/* ---- host checkpoint (format q3:weapon-host) ---- */
static void put(qa_save_value *object, const char *key, qa_save_value *child) { if (!child || !qa_save_value_set(object, key, child)) qa_range_error("Out of memory"); }
static qa_qvm_host_checkpoint host_checkpoint(void *ctx) {
  source_t *s = ctx;
  qa_save_value *value = checked(qa_save_value_object());
  put(value, "version", qa_save_value_number(1)); put(value, "mode", qa_save_value_string(s->options.mode));
  put(value, "teamMode", qa_save_value_boolean(s->options.team_mode)); put(value, "time", qa_save_value_number(s->time));
  qa_app_sim_qvm_component_state_capture(&s->state, &s->game.data, s->files, value);
  size_t length = 0; uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  if (!bytes) qa_range_error("Out of memory");
  free(s->host_bytes); s->host_bytes = bytes;
  qa_qvm_host_checkpoint out; memset(&out, 0, sizeof out);
  out.state.module = s->definition.module; out.state.format = "q3:weapon-host"; out.state.bytes.bytes = bytes; out.state.bytes.length = length;
  return out;
}
static qa_save_reader fld(const qa_save_reader *r, const char *n) { qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_range_error("Out of memory"); return o; }
typedef struct host_restore_scratch { qa_save_value *value; qa_save_reader reader; bool has_reader; } host_restore_scratch;
static void host_restore_free(void *arg) { host_restore_scratch *h = arg; if (h->has_reader) qa_save_reader_destroy(&h->reader); qa_save_value_free(h->value); }
static void host_restore(void *ctx, const qa_qvm_host_checkpoint *checkpoint) {
  source_t *s = ctx;
  if (!is(checkpoint->state.format, "q3:weapon-host") || checkpoint->random_count != 0 || checkpoint->callback_count != 0) fail_error("Invalid QVM weapon host checkpoint");
  host_restore_scratch h; memset(&h, 0, sizeof h);
  h.value = qa_decode_checkpoint_value(checkpoint->state.bytes.bytes, checkpoint->state.bytes.length);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, host_restore_free, &h);
  if (!qa_save_reader_init(&h.reader, h.value, "qvm.weapon.host")) qa_range_error("Out of memory");
  h.has_reader = true;
  const qa_save_reader *reader = &h.reader; qa_save_reader f;
  f = fld(reader, "version"); qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(reader, "mode"); qa_save_reader_literal_string(&f, s->options.mode); qa_save_reader_destroy(&f);
  f = fld(reader, "teamMode"); qa_save_reader_literal_boolean(&f, s->options.team_mode); qa_save_reader_destroy(&f);
  f = fld(reader, "time"); s->time = qa_save_reader_number(&f);
  if (!isfinite(s->time) || s->time < 0) qa_save_reader_fail(reader, "invalid source time");
  qa_save_reader_destroy(&f);
  qa_app_sim_qvm_component_state_restore(&s->state, reader, &s->game.data, s->files, "component");
  qa_cleanup_pop(&cleanup); host_restore_free(&h);
}

/* ---- construction ---- */
static void files_assert_current(void *ctx) { current(ctx); }
static void source_free(source_t *s) {
  if (s->has_game) qa_qvm_game_destroy(&s->game);
  if (s->files) qa_qvm_files_destroy(s->files);
  if (s->cvars) qa_cvar_registry_destroy(s->cvars);
  qa_app_sim_qvm_component_state_destroy(&s->state);
  if (s->has_profile) qa_qvm_weapon_profile_free(&s->profile);
  for (qvm_instance *i = s->instance_list, *next; i; i = next) { next = i->next; free(i); }
  free(s->bindings); free(s->retired);
  free(s->instances.values); free(s->activated.values); free(s->host_bytes); free(s);
}
static void source_cleanup(void *arg) { source_free(arg); }
/* TS constructor. On a raise the partial source is freed by the cleanup node. */
static source_t *construct(qa_app_sim_qvm_weapon_behavior_options options) {
  source_t *s = checked(calloc(1, sizeof *s));
  s->options = options;
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, source_cleanup, s);
  qa_qvm_validate_weapon_profile(options.profile, options.artifact, &s->profile); s->has_profile = true;
  s->definition = s->profile.definition;
  qa_cvar_registry_options cvar_options; memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3; cvar_options.context = options.context; cvar_options.print = forward_print; cvar_options.print_ctx = s;
  s->cvars = checked(qa_cvar_registry_create(&cvar_options));
  qa_app_sim_qvm_component_state_init(&s->state, options.entity_text, s->cvars);
  const char *const rows[7][2] = {{"g_log", ""}, {"cm_noCurves", "0"}, {"cm_playerCurveClip", "1"}, {"bot_enable", "0"}, {"sv_maxclients", "64"}, {"dedicated", "1"}, {"g_gametype", options.team_mode ? "3" : "0"}};
  for (int i = 0; i < 7; i++) {
    qa_cvar_snapshot registered;
    if (!qa_cvar_registry_register(s->cvars, rows[i][0], rows[i][1], 0, &registered)) qa_range_error("Out of memory");
    qa_cvar_read_free(&registered);
  }
  qa_qvm_file_services files; memset(&files, 0, sizeof files);
  files.mounts = options.mounts; files.writable = NULL; files.ctx = s; files.print = forward_print; files.assert_current = files_assert_current;
  s->files = checked(qa_qvm_files_create(files));
  s->host_state.ctx = s; s->host_state.checkpoint = host_checkpoint; s->host_state.restore = host_restore;
  qa_qvm_module_options module; memset(&module, 0, sizeof module);
  module.artifact = options.artifact; module.host = host; module.host_ctx = s; module.host_state = &s->host_state;
  qa_qvm_game_init(&s->game, &module); s->has_game = true;
  qa_qvm_game_data_set_client_count(&s->game.data, 64);
  qa_app_sim_qvm_component_spatial_options spatial; memset(&spatial, 0, sizeof spatial);
  spatial.scene = options.scene; spatial.cvars = s->cvars; spatial.data = &s->game.data; spatial.label = "QVM weapon";
  spatial.ctx = s; spatial.actor_for_slot = component_actor_for_slot; spatial.slot_of = component_slot_of; spatial.reject = component_reject;
  qa_app_sim_qvm_component_spatial_init(&s->spatial, spatial);
  memset(&s->services, 0, sizeof s->services);
  s->services.data = &s->game.data; s->services.cvars = s->cvars; s->services.max_clients = 64;
  s->services.configstrings_ctx = s; s->services.configstring_get = services_configstring_get; s->services.configstring_set = services_configstring_set;
  s->services.spatial = &s->spatial.operations; s->services.ctx = s;
  s->services.get_userinfo = services_get_userinfo; s->services.set_userinfo = services_set_userinfo; s->services.get_user_command = services_user_command;
  s->services.drop_client = services_drop_client; s->services.send_server_command = services_send_server_command;
  s->services.entity_tokens.ctx = s; s->services.entity_tokens.entity_token = services_entity_token;
  qa_cleanup_pop(&cleanup);
  return s;
}
typedef struct close_call { source_t *s; } close_call;
static void run_close_all(void *ctx) { close_call *c = ctx; qa_qvm_files_close_all(c->s->files); }
void qa_app_sim_qvm_weapon_behavior_source_close(source_t *s) {
  if (s->closed) return;
  s->closed = true; s->instances.count = 0; s->binding_count = 0; s->retired_count = 0; s->activated.count = 0; userinfo_clear(s);
  close_call c = {s}; qa_caught_error caught;
  bool ok = qa_try(run_close_all, &c, &caught);
  qa_qvm_game_retire(&s->game);
  if (!ok) qa_rethrow(&caught);
}
/* TS `catch (error) { try { source.close() } catch (cleanup) { throw AggregateError } throw error }`, then free. */
typedef struct create_call { source_t *s; } create_call;
static void run_initialize(void *ctx) {
  create_call *c = ctx;
  qa_qvm_game_initialize_async(&c->s->game, 0, c->s->options.seed, false);
  validate_layout(c->s); c->s->ready = true;
}
static void run_close(void *ctx) { qa_app_sim_qvm_weapon_behavior_source_close(((create_call *)ctx)->s); }
static noreturn void fail_with_cleanup(source_t *s, const qa_caught_error *error, const char *aggregate) {
  create_call c = {s}; qa_caught_error cleanup;
  bool ok = qa_try(run_close, &c, &cleanup);
  source_free(s);
  if (!ok) qa_raise("AggregateError", 0, 0, "%s", aggregate); /* TS AggregateError([error, cleanup], aggregate) */
  qa_rethrow(error);
}
qa_app_sim_qvm_weapon_behavior_source *qa_app_sim_qvm_weapon_behavior_source_create(qa_app_sim_qvm_weapon_behavior_options options) {
  source_t *s = construct(options);
  create_call c = {s}; qa_caught_error error;
  if (!qa_try(run_initialize, &c, &error)) fail_with_cleanup(s, &error, "QVM weapon initialization and cleanup failed");
  return s;
}
void qa_app_sim_qvm_weapon_behavior_source_destroy(source_t *s) {
  if (!s) return;
  if (!s->closed) qa_app_sim_qvm_weapon_behavior_source_close(s);
  source_free(s);
}
qa_weapon_behavior_definition qa_app_sim_qvm_weapon_behavior_source_definition(const source_t *s) { return s->definition; }
struct qa_qvm_game *qa_app_sim_qvm_weapon_behavior_source_game(source_t *s) { return &s->game; }
static qa_weapon_behavior_instance_result as_attach(void *ctx, qa_weapon_behavior_launch launch) {
  qa_weapon_behavior_instance_result out; out.has_value = true; out.value = qa_app_sim_qvm_weapon_behavior_source_attach(ctx, launch); return out;
}
static qa_weapon_behavior_instance as_resume(void *ctx, qa_actor_id actor) { return qa_app_sim_qvm_weapon_behavior_source_resume(ctx, actor); }
qa_weapon_behavior_source qa_app_sim_qvm_weapon_behavior_source_as_source(source_t *s) {
  qa_weapon_behavior_source out; memset(&out, 0, sizeof out);
  out.ctx = s; out.definition = s->definition; out.attach = as_attach; out.resume = as_resume; return out;
}

/* ---- checkpoints ---- */
static qa_save_value *module_value(qa_module_identity module) {
  qa_save_value *value = checked(qa_save_value_object());
  put(value, "id", qa_save_value_string(module.id)); put(value, "artifactPath", qa_save_value_string(module.artifact_path));
  put(value, "digest", qa_save_value_string(module.digest)); put(value, "revision", qa_save_value_string(module.revision));
  return value;
}
static qa_save_value *callback_value(qa_weapon_behavior_callback callback) {
  qa_save_value *value = checked(qa_save_value_object());
  put(value, "kind", qa_save_value_string("qvm")); put(value, "module", module_value(callback.module));
  put(value, "instructionIndex", qa_save_value_number(callback.instruction_index));
  return value;
}
/* sha256 of encodeCheckpointValue(profile), the validated TS QvmWeaponProfile
 * {definition{id,title,module,role,aspect,fire,activate},entityStride,levelTime,allocate,free,fields,fireAbi}. */
static void profile_digest(source_t *s, char out[65]) {
  const qa_qvm_weapon_profile *p = &s->profile; const qa_weapon_behavior_definition *d = &p->definition;
  qa_save_value *value = checked(qa_save_value_object()), *definition = checked(qa_save_value_object()), *fields = checked(qa_save_value_object());
  put(definition, "id", qa_save_value_string(d->id)); put(definition, "title", qa_save_value_string(d->title));
  put(definition, "module", module_value(d->module)); put(definition, "role", qa_save_value_string(d->role));
  put(definition, "aspect", qa_save_value_string(d->aspect)); put(definition, "fire", callback_value(d->fire));
  put(definition, "activate", d->has_activate ? callback_value(d->activate) : qa_save_value_null());
  put(value, "definition", definition);
  put(value, "entityStride", qa_save_value_number((double)p->entity_stride)); put(value, "levelTime", qa_save_value_number((double)p->level_time));
  put(value, "allocate", qa_save_value_number((double)p->allocate)); put(value, "free", qa_save_value_number((double)p->free_entry));
  put(fields, "inuse", qa_save_value_number((double)p->field_inuse)); put(fields, "nextthink", qa_save_value_number((double)p->field_nextthink));
  put(fields, "think", qa_save_value_number((double)p->field_think)); put(fields, "health", qa_save_value_number((double)p->field_health));
  put(value, "fields", fields); put(value, "fireAbi", qa_save_value_string(p->fire_abi));
  size_t length = 0; uint8_t *bytes = qa_encode_checkpoint_value(value, &length); qa_save_value_free(value);
  if (!bytes) qa_range_error("Out of memory");
  uint8_t digest[SHA256_DIGEST_LENGTH]; SHA256(bytes, length, digest); free(bytes);
  for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
}
void qa_app_sim_qvm_weapon_behavior_checkpoint_free(qa_app_sim_qvm_weapon_behavior_checkpoint *c) {
  if (!c) return;
  if (c->owns_module) { free((void *)c->module.data.bytes); free((void *)c->module.host_state.bytes.bytes); }
  free(c->activated); free(c->bindings); free(c->retired);
  memset(c, 0, sizeof *c);
}
static void run_refresh(void *ctx) { refresh_targets(ctx, NULL); }
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_qvm_weapon_behavior_source_checkpoint(source_t *s) {
  current(s); if (s->busy || !s->ready) fail_error("QVM weapon checkpoint requires an idle initialized source");
  operation(s, run_refresh, s);
  qa_app_sim_qvm_weapon_behavior_checkpoint out; memset(&out, 0, sizeof out);
  out.version = 1; out.definition = s->definition;
  qa_qvm_module_checkpoint(&s->game.module, &out.module); out.owns_module = true;
  profile_digest(s, out.profile_digest);
  out.activated_count = s->activated.count; out.activated = checked(malloc((out.activated_count ? out.activated_count : 1) * sizeof *out.activated));
  for (size_t i = 0; i < s->activated.count; i++) out.activated[i] = (qa_saved_actor_id){(double)s->activated.values[i].slot, (double)s->activated.values[i].generation};
  out.binding_count = s->binding_count; out.bindings = checked(malloc((out.binding_count ? out.binding_count : 1) * sizeof *out.bindings));
  for (size_t i = 0; i < s->binding_count; i++)
    out.bindings[i] = (qa_app_sim_qvm_weapon_binding){{(double)s->bindings[i].actor.slot, (double)s->bindings[i].actor.generation}, s->bindings[i].pointer, s->bindings[i].kind};
  out.retired_count = s->retired_count; out.retired = checked(malloc((out.retired_count ? out.retired_count : 1) * sizeof *out.retired));
  for (size_t i = 0; i < s->retired_count; i++)
    out.retired[i] = (qa_app_sim_qvm_weapon_retired){{(double)s->retired[i].actor.slot, (double)s->retired[i].actor.generation}, s->retired[i].trajectory};
  return out;
}
typedef struct restore_static_call {
  source_t *s; const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint;
  qa_actor_id (*resolve)(void *ctx, qa_saved_actor_id saved); void *resolve_ctx;
} restore_static_call;
static void run_restore_static(void *ctx) {
  restore_static_call *c = ctx; source_t *s = c->s; const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint = c->checkpoint;
  char digest[65]; profile_digest(s, digest);
  if (checkpoint->version != 1 || !qa_same_weapon_behavior(s->definition, checkpoint->definition) || strcmp(checkpoint->profile_digest, digest) != 0)
    fail_error("Saved QVM weapon profile differs from the selected artifact declaration");
  qa_qvm_module_restore(&s->game.module, &checkpoint->module); validate_layout(s);
  for (size_t i = 0; i < checkpoint->binding_count; i++) {
    const qa_app_sim_qvm_weapon_binding *saved = &checkpoint->bindings[i];
    qa_actor_id actor = c->resolve(c->resolve_ctx, saved->actor);
    int32_t pointer = (int32_t)saved->pointer; slot_of(s, pointer);
    bool pointer_used = false; for (size_t b = 0; b < s->binding_count; b++) if (s->bindings[b].pointer == pointer) pointer_used = true;
    if (binding_get(s, actor) || pointer_used || !live(s, pointer)) fail_error("Invalid saved QVM weapon actor binding");
    if (is(saved->kind, "client") != (userinfo_get(s, slot_of(s, pointer)) != NULL)) fail_error("Saved QVM client ownership differs");
    binding_set(s, (binding){actor, pointer, saved->kind});
  }
  size_t clients = 0; for (size_t b = 0; b < s->binding_count; b++) if (is(s->bindings[b].kind, "client")) clients++;
  if (clients != s->state.userinfo.count) fail_error("Unbound saved QVM client");
  for (size_t i = 0; i < checkpoint->retired_count; i++) {
    qa_actor_id actor = c->resolve(c->resolve_ctx, checkpoint->retired[i].actor);
    if (binding_get(s, actor) || retired_get(s, actor)) fail_error("Duplicate saved QVM weapon actor");
    retired_set(s, actor, checkpoint->retired[i].trajectory);
  }
  for (size_t i = 0; i < checkpoint->activated_count; i++) {
    qa_actor_id actor = c->resolve(c->resolve_ctx, checkpoint->activated[i]);
    if (!binding_get(s, actor) || aset_has(&s->activated, actor)) fail_error("Invalid saved weapon activation");
    aset_add(&s->activated, actor);
  }
  s->ready = true;
}
qa_app_sim_qvm_weapon_behavior_source *qa_app_sim_qvm_weapon_behavior_source_create_restored(
    qa_app_sim_qvm_weapon_behavior_options options, const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint,
    qa_actor_id (*resolve_actor)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx) {
  source_t *s = construct(options);
  restore_static_call c = {s, checkpoint, resolve_actor, resolve_ctx}; qa_caught_error error;
  if (!qa_try(run_restore_static, &c, &error)) fail_with_cleanup(s, &error, "QVM weapon restore and cleanup failed");
  return s;
}
typedef struct module_restore_call { source_t *s; const qa_guest_checkpoint *checkpoint; } module_restore_call;
static void run_module_restore(void *ctx) { module_restore_call *c = ctx; qa_qvm_module_restore(&c->s->game.module, c->checkpoint); }
typedef struct restore_call {
  source_t *s; const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint;
  qa_actor_id (*resolve)(void *ctx, qa_saved_actor_id saved); void *resolve_ctx;
} restore_call;
typedef struct staged_scope { source_t *staged; qa_guest_checkpoint previous; bool has_previous; } staged_scope;
static void staged_scope_free(void *arg) {
  staged_scope *scope = arg;
  if (scope->has_previous) { free((void *)scope->previous.data.bytes); free((void *)scope->previous.host_state.bytes.bytes); }
  qa_app_sim_qvm_weapon_behavior_source_destroy(scope->staged);
}
static void run_restore(void *ctx) {
  restore_call *c = ctx; source_t *s = c->s;
  staged_scope scope; memset(&scope, 0, sizeof scope);
  scope.staged = qa_app_sim_qvm_weapon_behavior_source_create_restored(s->options, c->checkpoint, c->resolve, c->resolve_ctx);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, staged_scope_free, &scope);
  qa_qvm_module_checkpoint(&s->game.module, &scope.previous); scope.has_previous = true;
  module_restore_call restore = {s, &c->checkpoint->module}; qa_caught_error error;
  if (!qa_try(run_module_restore, &restore, &error)) {
    module_restore_call rollback = {s, &scope.previous}; qa_caught_error rollback_error;
    if (!qa_try(run_module_restore, &rollback, &rollback_error))
      qa_raise("AggregateError", 0, 0, "QVM weapon restore and rollback failed");
    qa_rethrow(&error);
  }
  source_t *staged = scope.staged;
  s->binding_count = 0; for (size_t i = 0; i < staged->binding_count; i++) binding_set(s, staged->bindings[i]);
  s->retired_count = 0; for (size_t i = 0; i < staged->retired_count; i++) retired_set(s, staged->retired[i].actor, staged->retired[i].trajectory);
  s->activated.count = 0; for (size_t i = 0; i < staged->activated.count; i++) aset_add(&s->activated, staged->activated.values[i]);
  s->instances.count = 0; s->generation++;
  qa_cleanup_pop(&cleanup); staged_scope_free(&scope);
}
void qa_app_sim_qvm_weapon_behavior_source_restore(source_t *s, const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint,
    qa_actor_id (*resolve_actor)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx) {
  restore_call c = {s, checkpoint, resolve_actor, resolve_ctx};
  operation(s, run_restore, &c);
}

/* ---- reader ---- */
static bool hex_digest(const char *text) {
  if (text == NULL || strlen(text) != 64) return false;
  for (const char *c = text; *c; c++) if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'))) return false;
  return true;
}
typedef struct read_scratch { qa_app_sim_qvm_weapon_behavior_checkpoint *out; } read_scratch;
static void read_scratch_free(void *arg) { qa_app_sim_qvm_weapon_behavior_checkpoint_free(((read_scratch *)arg)->out); }
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_read_qvm_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected) {
  static const char *const kinds[] = {"target", "client", "projectile"};
  qa_app_sim_qvm_weapon_behavior_checkpoint out; memset(&out, 0, sizeof out);
  read_scratch scratch = {&out}; qa_cleanup cleanup; qa_cleanup_push(&cleanup, read_scratch_free, &scratch);
  qa_save_reader f = fld(reader, "module"); out.module = qa_read_guest(&f);
  if (out.module.kind != QA_GUEST_CHECKPOINT_QVM) qa_save_reader_fail(&f, "expected QVM checkpoint");
  qa_save_reader_destroy(&f);
  f = fld(reader, "profileDigest"); const char *digest = qa_save_reader_string(&f);
  if (!hex_digest(digest)) qa_save_reader_fail(&f, "invalid profile digest");
  memcpy(out.profile_digest, digest, 65); qa_save_reader_destroy(&f);
  f = fld(reader, "version"); out.version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(reader, "definition"); out.definition = qa_read_weapon_behavior_definition(&f, expected); qa_save_reader_destroy(&f);
  f = fld(reader, "activated"); size_t count = qa_save_reader_list_count(&f);
  out.activated = checked(malloc((count ? count : 1) * sizeof *out.activated));
  for (size_t i = 0; i < count; i++) { qa_save_reader e; qa_save_reader_item(&f, i, &e); out.activated[i] = qa_read_saved_actor(&e); out.activated_count = i + 1; qa_save_reader_destroy(&e); }
  qa_save_reader_destroy(&f);
  f = fld(reader, "bindings"); count = qa_save_reader_list_count(&f);
  out.bindings = checked(malloc((count ? count : 1) * sizeof *out.bindings));
  for (size_t i = 0; i < count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "actor"); out.bindings[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "pointer"); out.bindings[i].pointer = qa_save_reader_integer_min(&bf, 1); qa_save_reader_destroy(&bf);
    bf = fld(&e, "kind"); out.bindings[i].kind = qa_save_reader_choice_string(&bf, kinds, 3); qa_save_reader_destroy(&bf);
    out.binding_count = i + 1; qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "retired"); count = qa_save_reader_list_count(&f);
  out.retired = checked(malloc((count ? count : 1) * sizeof *out.retired));
  for (size_t i = 0; i < count; i++) {
    qa_save_reader e, bf, t, v; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "actor"); out.retired[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    t = fld(&e, "trajectory");
    v = fld(&t, "origin"); out.retired[i].trajectory.origin = qa_read_vector(&v); qa_save_reader_destroy(&v);
    qa_save_reader_destroy(&t); t = fld(&e, "trajectory");
    v = fld(&t, "velocity"); out.retired[i].trajectory.velocity = qa_read_vector(&v); qa_save_reader_destroy(&v);
    qa_save_reader_destroy(&t); t = fld(&e, "trajectory");
    v = fld(&t, "angles"); out.retired[i].trajectory.angles = qa_read_vector(&v); qa_save_reader_destroy(&v);
    qa_save_reader_destroy(&t);
    out.retired_count = i + 1; qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  qa_cleanup_pop(&cleanup);
  return out;
}

/* ---- initialization entities ---- */
