/* Port of src/app/bootstrap/simulation/rerelease-weapon-behavior.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_weapon_behavior.h"
#include "qa/foundation/caught_message.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/foundation/js.h"
#include "qa/persistence/save_image.h"
#include "qa/foundation/support.h"
void qa_app_sim_retire_rerelease_weapon_actor(qa_actor_id actor, qa_app_sim_rerelease_retire_state state,
    void (*dispose)(void *ctx, double slot, qa_app_sim_rerelease_binding_entry binding), void *dispose_ctx) {
  for (size_t i = 0; i < *state.retired_count; i++)
    if (qa_same_actor(state.retired[i].actor, actor)) {
      memmove(&state.retired[i], &state.retired[i + 1], (*state.retired_count - i - 1) * sizeof state.retired[0]);
      (*state.retired_count)--; break;
    }
  double slot = 0; bool has_slot = false; size_t slot_at = 0;
  for (size_t i = 0; i < *state.slot_count; i++)
    if (qa_same_actor(state.slots[i].actor, actor)) { has_slot = true; slot = state.slots[i].slot; slot_at = i; break; }
  qa_app_sim_rerelease_binding_entry binding; bool has_binding = false; size_t binding_at = 0;
  if (has_slot) for (size_t i = 0; i < *state.binding_count; i++)
    if (state.bindings[i].slot == slot) { has_binding = true; binding = state.bindings[i]; binding_at = i; break; }
  if (!has_slot || !has_binding || !qa_same_actor(binding.actor, actor)) return;
  memmove(&state.slots[slot_at], &state.slots[slot_at + 1], (*state.slot_count - slot_at - 1) * sizeof state.slots[0]);
  (*state.slot_count)--;
  memmove(&state.bindings[binding_at], &state.bindings[binding_at + 1], (*state.binding_count - binding_at - 1) * sizeof state.bindings[0]);
  (*state.binding_count)--;
  dispose(dispose_ctx, slot, binding);
}

/* --- RereleaseWeaponBehaviorSource: native private state executes in its own address space. --- */
/* One per live trajectory instance (the TS closure): close unlinks and frees it, so a source no longer
 * keeps one per projectile fired until destroy; destroy frees the ones never closed. */
typedef struct instance_state { struct qa_app_sim_rerelease_weapon_behavior_source *owner; qa_actor_id actor; double slot; qa_app_sim_rerelease_binding_entry binding; bool closed, retired_only; struct instance_state *previous, *next; } instance_state;
typedef struct capture_state { qa_raw_entity_view shooter; qa_weapon_behavior_launch launch; double *slots; size_t count, capacity; } capture_state;
struct qa_app_sim_rerelease_weapon_behavior_source {
  qa_app_sim_rerelease_weapon_behavior_options options;
  qa_native_weapon_behavior_declaration declaration; bool has_declaration;
  qa_app_sim_rerelease_guest_source *source; qa_app_sim_rerelease_guest_services *services; qa_compat_q2_rerelease_weapon_profile *profile;
  qa_compat_q2_rerelease_semantic_bindings semantics;
  /* TS Maps and Sets: growable arrays (no fixed count). */
  qa_app_sim_rerelease_binding_entry *bindings; size_t binding_count, binding_capacity;
  qa_app_sim_rerelease_slot_entry *slots; size_t slot_count, slot_capacity;
  qa_app_sim_rerelease_retired_entry *retired; size_t retired_count, retired_capacity;
  qa_actor_id *instances; size_t instance_count, instance_capacity;
  instance_state *states;
  const char *const *command_arguments; size_t command_count; const char *command_args;
  capture_state *capture;
  bool has_subscription; int64_t subscription;
  qa_actor_id *pending; size_t pending_count, pending_capacity;
  bool busy, closed; double time;
  char *initialization_entities;
  qa_q2_foundation_host *engine_copy; /* TS { ...engine, emit }: owned, freed by destroy */
};
typedef struct qa_app_sim_rerelease_weapon_behavior_source source_t;
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
/* Room for one more element of a growable array. */
#define GROW(array, count, capacity) do { \
    if ((count) == (capacity)) { \
      size_t grown_capacity = (capacity) ? (capacity) * 2 : 16; \
      void *grown = realloc((array), grown_capacity * sizeof *(array)); \
      if (grown == NULL) qa_range_error("Out of memory"); \
      (array) = grown; (capacity) = grown_capacity; \
    } \
  } while (0)
static bool same(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
static void free_save_value(void *value) { qa_save_value_free(value); }
static qa_compat_q2_rerelease_q2_guest_host *host_of(source_t *s) {
  if (s->closed || s->source == NULL || s->services == NULL || s->profile == NULL) fail("Native weapon component is not initialized");
  return qa_app_sim_rerelease_guest_source_host(s->source);
}
static qa_compat_q2_rerelease_weapon_profile *profile_of(source_t *s) { (void)host_of(s); return s->profile; }
static qa_compat_q2_rerelease_guest_module *module_of(source_t *s) { return qa_compat_q2_rerelease_q2_guest_host_get_module(host_of(s)); }
static qa_raw_entity_view view_of(source_t *s, double slot) { qa_raw_entity_table t = qa_compat_q2_rerelease_guest_module_entities(module_of(s)); return t.at_slot(t.ctx, slot); }
static qa_app_sim_rerelease_binding_entry *binding_at(source_t *s, double slot) { for (size_t i = 0; i < s->binding_count; i++) if (s->bindings[i].slot == slot) return &s->bindings[i]; return NULL; }
static qa_app_sim_rerelease_slot_entry *slot_of(source_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->slot_count; i++) if (same(s->slots[i].actor, actor)) return &s->slots[i]; return NULL; }
static qa_app_sim_rerelease_retired_entry *retired_of(source_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->retired_count; i++) if (same(s->retired[i].actor, actor)) return &s->retired[i]; return NULL; }
static void delete_binding(source_t *s, double slot) { qa_app_sim_rerelease_binding_entry *b = binding_at(s, slot); if (b == NULL) return; size_t i = (size_t)(b - s->bindings); memmove(b, b + 1, (s->binding_count - i - 1) * sizeof *b); s->binding_count--; }
static void delete_slot(source_t *s, qa_actor_id actor) { qa_app_sim_rerelease_slot_entry *e = slot_of(s, actor); if (e == NULL) return; size_t i = (size_t)(e - s->slots); memmove(e, e + 1, (s->slot_count - i - 1) * sizeof *e); s->slot_count--; }
static void delete_retired(source_t *s, qa_actor_id actor) { qa_app_sim_rerelease_retired_entry *e = retired_of(s, actor); if (e == NULL) return; size_t i = (size_t)(e - s->retired); memmove(e, e + 1, (s->retired_count - i - 1) * sizeof *e); s->retired_count--; }
static bool has_instance(source_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->instance_count; i++) if (same(s->instances[i], actor)) return true; return false; }
static void delete_instance(source_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->instance_count; i++) if (same(s->instances[i], actor)) { s->instances[i] = s->instances[--s->instance_count]; return; } }
static void add_instance(source_t *s, qa_actor_id actor) { GROW(s->instances, s->instance_count, s->instance_capacity); s->instances[s->instance_count++] = actor; }
static void set_binding(source_t *s, double slot, qa_owned_actor actor, const char *kind, double generation) {
  qa_app_sim_rerelease_binding_entry *b = binding_at(s, slot);
  if (b == NULL) { GROW(s->bindings, s->binding_count, s->binding_capacity); b = &s->bindings[s->binding_count++]; }
  b->slot = slot; b->actor = actor.id; b->owned = actor; b->kind = kind; b->generation = generation;
}
static void set_slot(source_t *s, qa_actor_id actor, double slot) {
  qa_app_sim_rerelease_slot_entry *e = slot_of(s, actor);
  if (e == NULL) { GROW(s->slots, s->slot_count, s->slot_capacity); e = &s->slots[s->slot_count++]; }
  e->actor = actor; e->slot = slot;
}
static void remember(source_t *s, qa_raw_entity_view record, qa_owned_actor actor, const char *kind) {
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  set_binding(s, record.slot, actor, kind, p->generation(p->ctx, record)); set_slot(s, actor.id, record.slot);
}
static bool live(source_t *s, double slot, qa_app_sim_rerelease_binding_entry binding) {
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s); qa_raw_entity_view view = view_of(s, slot);
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module_of(s));
  return qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, view.address, (int64_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "inuse"))) != 0
    && p->generation(p->ctx, view) == binding.generation;
}
typedef struct client_dispose { source_t *s; double slot; } client_dispose;
static void run_disconnect(void *ctx) { client_dispose *d = ctx; qa_compat_q2_rerelease_q2_guest_host_client_disconnect(host_of(d->s), d->slot); }
static void dispose(void *ctx, double slot, qa_app_sim_rerelease_binding_entry binding) {
  source_t *s = ctx; if (s->closed) return;
  qa_compat_q2_rerelease_q2_guest_host *host = host_of(s); qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  if (strcmp(binding.kind, "client") == 0) {
    client_dispose d = {s, slot}; qa_caught_error caught; bool done = qa_try(run_disconnect, &d, &caught);
    qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(host, slot);
    if (!done) qa_rethrow(&caught);
  } else if (live(s, slot, binding)) p->free_record(p->ctx, view_of(s, slot));
}
static void release_actor(source_t *s, qa_actor_id actor) {
  delete_instance(s, actor);
  qa_app_sim_rerelease_retire_state state = {s->slots, &s->slot_count, s->bindings, &s->binding_count, s->retired, &s->retired_count};
  qa_app_sim_retire_rerelease_weapon_actor(actor, state, dispose, s);
}
/* TS flushReleases: every pending release runs; the first failure is rethrown after all ran (AggregateError in TS). */
typedef struct release_call { source_t *s; qa_actor_id actor; } release_call;
static void run_release(void *ctx) { release_call *c = ctx; release_actor(c->s, c->actor); }
static void flush_releases(source_t *s) {
  bool failed = false; qa_caught_error first;
  while (s->pending_count > 0) {
    release_call c = {s, s->pending[0]}; memmove(&s->pending[0], &s->pending[1], (s->pending_count - 1) * sizeof *s->pending); s->pending_count--;
    qa_caught_error caught; if (!qa_try(run_release, &c, &caught) && !failed) { failed = true; qa_caught_copy(&first, &caught); }
  }
  (void)first; /* TS AggregateError(errors, ...): the class and message are the aggregate's */
  if (failed) qa_raise("AggregateError", 0, 0, "Native component actor retirement failed");
}
static void on_release(void *ctx, qa_owned_actor actor) {
  source_t *s = ctx;
  if (s->busy) {
    for (size_t i = 0; i < s->pending_count; i++) if (same(s->pending[i], actor.id)) return;
    GROW(s->pending, s->pending_count, s->pending_capacity);
    s->pending[s->pending_count++] = actor.id;
  }
  else release_actor(s, actor.id);
}
static void drain(source_t *s) { if (s->services != NULL) { size_t n; (void)qa_app_sim_rerelease_guest_services_drain_messages(s->services, &n); } }
/* TS operation(run): busy around run, then finally { busy = false; flushReleases(); drainMessages() }.
 * A raise in the finally replaces the body's (and skips the drain), as in TS. */
static void run_operation(source_t *s, void (*body)(void *ctx), void *ctx) {
  if (s->busy) fail("Native weapon component operation is already active");
  s->busy = true;
  qa_caught_error caught; bool ok = qa_try(body, ctx, &caught);
  s->busy = false;
  flush_releases(s); drain(s);
  if (!ok) qa_rethrow(&caught);
}
static void set_time(source_t *s, double seconds) {
  if (!isfinite(seconds) || seconds < 0) fail("Native weapon invocation time must be finite and nonnegative");
  s->time = seconds; qa_compat_q2_rerelease_weapon_profile *p = profile_of(s); p->time(p->ctx, (int64_t)qa_js_round(seconds * 1000));
}
static qa_raw_entity_view mirror(source_t *s, qa_actor_id actor) {
  qa_app_sim_rerelease_slot_entry *previous = slot_of(s, actor); if (previous != NULL) return view_of(s, previous->slot);
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s); qa_app_sim_rerelease_weapon_actor target = s->options.actor(s->options.ctx, actor);
  qa_raw_entity_view record = p->allocate(p->ctx); p->project(p->ctx, record, target.shooter.body); remember(s, record, target.actor, "target"); return record;
}
typedef struct provision_call { source_t *s; qa_raw_entity_view record; } provision_call;
static void refresh(void *ctx) { source_t *s = ctx; qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_q2_guest_host_get_core(host_of(s))); }
static void refresh_call(void *ctx) { refresh(((provision_call *)ctx)->s); }
static void *run_command(void *ctx) {
  provision_call *c = ctx; qa_guest_call_value argument = qa_compat_q2_rerelease_guest_pointer(true, c->record.address);
  (void)qa_compat_q2_rerelease_guest_module_call_game(module_of(c->s), "ClientCommand", &argument, 1, &c->record, NULL); return NULL;
}
static void clear_command(void *arg) { source_t *s = arg; s->command_arguments = NULL; s->command_count = 0; s->command_args = ""; }
static void client_command(source_t *s, qa_raw_entity_view record, qa_native_weapon_command command) {
  s->command_arguments = command.arguments.values; s->command_count = command.arguments.count; s->command_args = command.tail != NULL ? command.tail : "";
  qa_cleanup node; qa_cleanup_push(&node, clear_command, s);
  /* refresh and run share one context: the call (run_command read the source as a call). */
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s); provision_call call = {s, record};
  (void)qa_compat_q2_with_rerelease_weapon_provisioning(p->provisioning_cvars, p->provisioning_cvar_count, s->options.services.base.cvars, &call, refresh_call, run_command);
  qa_cleanup_pop(&node); clear_command(s);
}
static qa_raw_entity_view client(source_t *s, qa_actor_id actor) {
  qa_compat_q2_rerelease_q2_guest_host *host = host_of(s); qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  qa_app_sim_rerelease_weapon_actor target = s->options.actor(s->options.ctx, actor);
  qa_app_sim_rerelease_slot_entry *previous = slot_of(s, actor);
  if (previous != NULL) {
    double slot = previous->slot; qa_app_sim_rerelease_binding_entry *binding = binding_at(s, slot);
    if (binding != NULL && strcmp(binding->kind, "client") == 0) { p->project_shooter(p->ctx, view_of(s, slot), target.shooter); return view_of(s, slot); }
    if (binding != NULL) p->free_record(p->ctx, view_of(s, slot));
    delete_binding(s, slot); delete_slot(s, actor);
  }
  double slot = 1; while (slot <= s->options.services.base.max_clients && binding_at(s, slot) != NULL) slot++;
  if (slot > s->options.services.base.max_clients) fail("Native weapon component exhausted its real source client capacity");
  qa_raw_entity_view record = view_of(s, slot);
  remember(s, record, target.actor, "client"); (void)qa_compat_q2_rerelease_q2_guest_host_reserve_client(host, slot);
  qa_compat_q2_rerelease_client_connect connected = qa_compat_q2_rerelease_q2_guest_host_client_connect(host, slot, target.userinfo, "", false);
  free(connected.userinfo);
  if (!connected.accepted) { delete_binding(s, slot); delete_slot(s, actor); qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(host, slot); fail("Native weapon source rejected the shooter userinfo"); }
  qa_compat_q2_rerelease_q2_guest_host_client_begin(host, slot); remember(s, record, target.actor, "client"); p->project_shooter(p->ctx, record, target.shooter);
  for (size_t i = 0; i < p->equipment_count; i++) client_command(s, record, p->equipment[i]);
  p->equip(p->ctx, record); return record;
}

/* --- instances --- */
static qa_weapon_trajectory_update_result none(void) { qa_weapon_trajectory_update_result r; memset(&r, 0, sizeof r); return r; }
static qa_weapon_trajectory_update_result retire_instance(instance_state *st, qa_body_state body) {
  source_t *s = st->owner; qa_weapon_trajectory_update t = {body.origin, body.velocity, body.angles};
  qa_app_sim_rerelease_retired_entry *r = retired_of(s, st->actor);
  if (r == NULL) { GROW(s->retired, s->retired_count, s->retired_capacity); r = &s->retired[s->retired_count++]; r->actor = st->actor; }
  r->trajectory.origin = t.origin; r->trajectory.velocity = t.velocity; r->trajectory.angles = t.angles;
  delete_binding(s, st->slot); delete_slot(s, st->actor); return none();
}
typedef struct step_call { instance_state st; qa_body_state body; double time; qa_weapon_trajectory_update_result result; } step_call;
/* st is the step's copy of the instance (the TS closure's slot, binding and actor). */
static qa_weapon_trajectory_update_result step_body(instance_state *st, qa_body_state body, double time) {
  source_t *s = st->owner;
  if (st->closed || !qa_session_actor_registry_is_live(s->options.services.base.engine->actors, st->actor)) fail("Native trajectory instance is closed");
  set_time(s, time); if (retired_of(s, st->actor) != NULL) return none();
  if (!live(s, st->slot, st->binding)) return retire_instance(st, body);
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s); qa_raw_entity_view record = view_of(s, st->slot); p->project(p->ctx, record, body);
  int64_t next = p->next_think(p->ctx, record); if (next <= 0 || next > (int64_t)qa_js_round(time * 1000)) return none();
  p->think(p->ctx, record);
  if (!live(s, st->slot, st->binding)) return retire_instance(st, body);
  qa_weapon_trajectory_update_result out; out.has_value = true; out.value = p->trajectory(p->ctx, record); return out;
}
static void run_step(void *ctx) { step_call *c = ctx; c->result = step_body(&c->st, c->body, c->time); }
static qa_weapon_trajectory_update_result instance_step(void *ctx, qa_body_state body, double time) {
  instance_state *st = ctx; source_t *s = st->owner;
  if (st->retired_only) { if (st->closed) fail("Native trajectory instance is closed"); return none(); }
  step_call call; memset(&call, 0, sizeof call); call.st = *st; call.body = body; call.time = time;
  run_operation(s, run_step, &call); return call.result;
}
static void unlink_state(instance_state *st) {
  source_t *s = st->owner;
  if (st->previous != NULL) st->previous->next = st->next; else s->states = st->next;
  if (st->next != NULL) st->next->previous = st->previous;
}
static void instance_close(void *ctx) {
  instance_state *st = ctx; if (st->closed) return;
  source_t *s = st->owner; qa_actor_id actor = st->actor; bool retired_only = st->retired_only;
  /* The instance is dead once closed: free it before the release, which can raise. */
  st->closed = true; unlink_state(st); free(st);
  delete_instance(s, actor);
  if (retired_only) delete_retired(s, actor); else release_actor(s, actor);
}
static instance_state *new_state(source_t *s, qa_actor_id actor) {
  instance_state *st = calloc(1, sizeof *st); if (st == NULL) qa_range_error("Out of memory");
  st->owner = s; st->actor = actor; st->next = s->states;
  if (s->states != NULL) s->states->previous = st;
  s->states = st; return st;
}
static qa_weapon_behavior_instance instance(source_t *s, double slot) {
  qa_app_sim_rerelease_binding_entry *binding = binding_at(s, slot); if (binding == NULL) fail("Native projectile binding is missing");
  qa_actor_id actor = binding->actor; qa_app_sim_rerelease_binding_entry kept = *binding;
  if (has_instance(s, actor)) fail("Native projectile already has a trajectory owner");
  add_instance(s, actor);
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  qa_weapon_behavior_instance out; memset(&out, 0, sizeof out);
  out.definition = p->definition; out.initial = p->trajectory(p->ctx, view_of(s, slot)); out.step = instance_step; out.close = instance_close;
  instance_state *st = new_state(s, actor); st->slot = slot; st->binding = kept;
  out.ctx = st;
  return out;
}
static qa_weapon_behavior_instance retired_instance(source_t *s, qa_actor_id actor, qa_weapon_trajectory_update initial) {
  if (has_instance(s, actor)) fail("Native projectile already has a trajectory owner");
  add_instance(s, actor);
  qa_weapon_behavior_instance out; memset(&out, 0, sizeof out);
  out.definition = profile_of(s)->definition; out.initial = initial; out.step = instance_step; out.close = instance_close;
  instance_state *st = new_state(s, actor); st->retired_only = true;
  out.ctx = st;
  return out;
}
qa_weapon_behavior_instance qa_app_sim_rerelease_weapon_behavior_source_resume(source_t *s, qa_actor_id actor) {
  qa_app_sim_rerelease_retired_entry *r = retired_of(s, actor);
  if (r != NULL) { qa_weapon_trajectory_update t = {r->trajectory.origin, r->trajectory.velocity, r->trajectory.angles}; return retired_instance(s, actor, t); }
  qa_app_sim_rerelease_slot_entry *slot = slot_of(s, actor); qa_app_sim_rerelease_binding_entry *b = slot == NULL ? NULL : binding_at(s, slot->slot);
  if (slot == NULL || b == NULL || strcmp(b->kind, "projectile") != 0) fail("Saved native trajectory has no retained source projectile");
  return instance(s, slot->slot);
}
static void free_indirect_slots(void *arg) { capture_state *capture = arg; free(capture->slots); capture->slots = NULL; }
static void free_captured(source_t *s, capture_state *capture) { qa_compat_q2_rerelease_weapon_profile *p = profile_of(s); for (size_t i = 0; i < capture->count; i++) p->free_record(p->ctx, view_of(s, capture->slots[i])); }
/* TS catch: for (slot of capture.slots) { profile.free(view(slot)); bindings.delete(slot) } (one slot at a time). */
static void free_captured_bindings(source_t *s, capture_state *capture) {
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  for (size_t i = 0; i < capture->count; i++) { p->free_record(p->ctx, view_of(s, capture->slots[i])); delete_binding(s, capture->slots[i]); }
}
typedef struct launch_call { source_t *s; qa_raw_entity_view shooter; } launch_call;
static void run_launch(void *ctx) {
  launch_call *c = ctx; qa_compat_q2_rerelease_weapon_profile *p = profile_of(c->s);
  qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_q2_guest_host_get_core(host_of(c->s))); p->launch(p->ctx, c->shooter);
}
typedef struct attach_call { source_t *s; qa_weapon_behavior_launch launch; qa_weapon_behavior_instance_result result; } attach_call;
static qa_weapon_behavior_instance_result attach_body(source_t *s, qa_weapon_behavior_launch launch) {
  qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  if (launch.role != p->definition.role || slot_of(s, launch.projectile.id) != NULL) fail("Incompatible or duplicate native projectile attachment");
  set_time(s, launch.time_seconds); qa_raw_entity_view shooter = client(s, launch.shooter);
  client_command(s, shooter, p->ammunition);
  capture_state capture; memset(&capture, 0, sizeof capture); capture.shooter = shooter; capture.launch = launch; s->capture = &capture;
  qa_cleanup capture_cleanup; /* the captured slot list is released on every path */
  qa_cleanup_push(&capture_cleanup, free_indirect_slots, &capture);
  launch_call call = {s, shooter}; qa_caught_error caught; bool completed = qa_try(run_launch, &call, &caught);
  s->capture = NULL;
  if (!completed) { free_captured_bindings(s, &capture); delete_slot(s, launch.projectile.id); qa_rethrow(&caught); }
  qa_weapon_behavior_instance_result out; memset(&out, 0, sizeof out);
  if (capture.count == 0) { qa_cleanup_pop(&capture_cleanup); free(capture.slots); return out; }
  if (capture.count != 1) {
    free_captured(s, &capture); delete_slot(s, launch.projectile.id); for (size_t i = 0; i < capture.count; i++) delete_binding(s, capture.slots[i]);
    fail("Native launch emitted multiple projectiles; one-to-many trajectory composition is required");
  }
  double captured_slot = capture.slots[0];
  qa_cleanup_pop(&capture_cleanup); free(capture.slots);
  out.has_value = true; out.value = instance(s, captured_slot);
  return out;
}
static void run_attach(void *ctx) { attach_call *c = ctx; c->result = attach_body(c->s, c->launch); }
qa_weapon_behavior_instance_result qa_app_sim_rerelease_weapon_behavior_source_attach(source_t *s, qa_weapon_behavior_launch launch) {
  attach_call call; memset(&call, 0, sizeof call); call.s = s; call.launch = launch;
  run_operation(s, run_attach, &call);
  return call.result;
}
static bool intercept(void *ctx, qa_compat_q2_rerelease_import_call call, qa_compat_q2_rerelease_q2_guest_host *host, qa_guest_call_result *out) {
  source_t *s = ctx; const char *name = call.name; *out = qa_compat_q2_call_void();
  if (strcmp(name, "SetAreaPortalState") == 0) fail("Native trajectory component cannot mutate selected world portals");
  if (strcmp(name, "SendToClipBoard") == 0) fail("Native trajectory component has no clipboard capability");
  if (strcmp(name, "linkentity") == 0 || strcmp(name, "unlinkentity") == 0) {
    qa_compat_q2_rerelease_guest_module *module = qa_compat_q2_rerelease_q2_guest_host_get_module(host); qa_raw_entity_table t = qa_compat_q2_rerelease_guest_module_entities(module);
    qa_raw_entity_view record = t.from_pointer(t.ctx, qa_compat_q2_guest_required_pointer(call.arguments, call.argument_count, 0));
    if (record.slot == 0) return true;
    bool link = strcmp(name, "linkentity") == 0; capture_state *capture = s->capture;
    if (link && capture != NULL && s->profile->matches(s->profile->ctx, record, capture->shooter)) {
      bool seen = false; for (size_t i = 0; i < capture->count; i++) seen = seen || capture->slots[i] == record.slot;
      if (!seen) { GROW(capture->slots, capture->count, capture->capacity); capture->slots[capture->count++] = record.slot; }
      remember(s, record, capture->launch.projectile, "projectile");
    }
    qa_compat_q2_memory *m = qa_compat_q2_rerelease_guest_module_get_memory(module);
#define AT(field) qa_compat_q2_mem_offset(m, record.address, (int64_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, field))
    qa_compat_q2_mem_write_u8(m, AT("linked"), link && qa_compat_q2_mem_read_u8(m, AT("solid")) != 0 ? 1 : 0);
    if (link) {
      for (int64_t axis = 0; axis < 12; axis += 4) {
        double origin = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, AT("s.origin"), axis)), min = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, AT("mins"), axis)), max = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, AT("maxs"), axis));
        qa_compat_q2_mem_write_f32(m, qa_compat_q2_mem_offset(m, AT("absmin"), axis), origin + min - 1);
        qa_compat_q2_mem_write_f32(m, qa_compat_q2_mem_offset(m, AT("absmax"), axis), origin + max + 1);
        qa_compat_q2_mem_write_f32(m, qa_compat_q2_mem_offset(m, AT("size"), axis), max - min);
      }
      qa_compat_q2_mem_write_i32(m, AT("linkcount"), qa_js_to_int32((double)qa_compat_q2_mem_read_i32(m, AT("linkcount")) + 1));
    }
#undef AT
    return true;
  }
  /* Selected weapon presentation stays primary; source inventory/ammo writes stay in private memory. */
  static const char *const silent[] = {"WriteChar", "WriteByte", "WriteShort", "WriteLong", "WriteFloat", "WriteAngle", "WritePosition", "WriteDir", "WriteString", "WriteEntity",
    "unicast", "multicast", "sound", "positioned_sound", "local_sound", "Broadcast_Print", "Client_Print", "Center_Print", "Loc_Print"};
  for (size_t i = 0; i < sizeof silent / sizeof *silent; i++) if (strcmp(name, silent[i]) == 0) return true;
  return false;
}

/* --- services leaves owned by the component --- */
static const char *const *component_command(void *ctx, size_t *out_count, const char **out_args) { source_t *s = ctx; *out_count = s->command_count; *out_args = s->command_args; return s->command_arguments; }
static void component_add_command(void *ctx, const char *text) { (void)ctx; (void)text; fail("Native weapon component cannot execute server commands"); }
static void silent_emit(void *ctx, qa_q2_presentation_event event) { (void)ctx; (void)event; }
static void silent_shapes(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event) { (void)ctx; (void)event; }
static void silent_text(void *ctx, qa_compat_q2_rerelease_world_text_event event) { (void)ctx; (void)event; }
static bool se_project(void *ctx, qa_raw_entity_view view, qa_compat_q2_rerelease_guest_module *module, qa_owned_actor *out) {
  source_t *s = ctx; (void)module; qa_app_sim_rerelease_binding_entry *b = binding_at(s, view.slot); if (b == NULL) return false; *out = b->owned; return true;
}
static qa_compat_q2_rerelease_actor_bindings se_bind(void *ctx, qa_raw_entity_view view, qa_owned_actor actor, qa_compat_q2_rerelease_guest_module *module) {
  (void)ctx; (void)view; (void)actor; (void)module; fail("Native component cannot replace primary actor authorities");
}
static qa_guest_address se_foreign(void *ctx, qa_actor_id actor) { return mirror(ctx, actor).address; }
static qa_compat_q2_rerelease_core_services bind_memory(void *ctx, qa_compat_q2_memory *memory) { source_t *s = ctx; return qa_app_sim_rerelease_guest_services_bind_memory(s->services, memory); }
static qa_weapon_behavior_instance_result as_attach(void *ctx, qa_weapon_behavior_launch launch) { return qa_app_sim_rerelease_weapon_behavior_source_attach(ctx, launch); }
static qa_weapon_behavior_instance as_resume(void *ctx, qa_actor_id actor) { return qa_app_sim_rerelease_weapon_behavior_source_resume(ctx, actor); }

static void create_body(void *ctx);
static void run_component_close(void *ctx) { qa_app_sim_rerelease_weapon_behavior_source_close(ctx); }
source_t *qa_app_sim_rerelease_weapon_behavior_source_create(const qa_app_sim_rerelease_weapon_behavior_options *options) {
  source_t *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->options = *options; s->command_args = "";
  const qa_resolved_execution_module *execution = &options->prepared->execution; const qa_resolved_resource_reference *artifact = execution->artifact;
  qa_module_identity module = {execution->owner.provider, artifact->requested_path, artifact->digest, artifact->digest};
  char *json = qa_compat_q2_serialize_native_weapon_declaration(options->declaration); if (json == NULL) qa_range_error("Out of memory");
  qa_save_value *value = qa_parse_json_value((const uint8_t *)json, strlen(json), false); free(json);
  /* A refused declaration (the reader raises) keeps nothing. */
  qa_cleanup owner_node, value_node; qa_cleanup_push(&owner_node, free, s); qa_cleanup_push(&value_node, free_save_value, value);
  if (!qa_compat_q2_read_native_weapon_declaration(value, &module, &s->declaration)) qa_range_error("Out of memory");
  qa_cleanup_pop(&value_node); qa_cleanup_pop(&owner_node);
  qa_save_value_free(value); s->has_declaration = true; s->options.declaration = &s->declaration;
  /* TS try { ... } catch (error) { owner.close() (AggregateError when that fails too); throw error }. */
  qa_caught_error caught;
  if (!qa_try(create_body, s, &caught)) {
    qa_caught_error cleanup; bool closed = qa_try(run_component_close, s, &cleanup);
    qa_app_sim_rerelease_weapon_behavior_source_destroy(s);
    if (!closed) qa_raise("AggregateError", 0, 0, "Native component initialization and cleanup failed");
    qa_rethrow(&caught);
  }
  return s;
}
static void create_body(void *ctx) {
  source_t *s = ctx; const qa_app_sim_rerelease_weapon_behavior_options *options = &s->options;
  qa_app_sim_rerelease_guest_services_options services = options->services;
  /* TS { ...engine, emit: () => undefined }: a copy of the engine with presentation silenced. */
  qa_q2_foundation_host *engine = malloc(sizeof *engine); if (engine == NULL) qa_range_error("Out of memory");
  *engine = *options->services.base.engine; engine->emit = silent_emit; services.base.engine = engine; s->engine_copy = engine;
  services.base.command = (qa_app_sim_guest_command){s, component_command}; services.base.add_command = (qa_app_sim_guest_add_command){s, component_add_command};
  services.debug_shapes = (qa_app_sim_rerelease_debug_shapes){s, silent_shapes}; services.world_text = (qa_app_sim_rerelease_world_text){s, silent_text};
  s->semantics.ctx = s; s->semantics.project = se_project; s->semantics.bind = se_bind; s->semantics.foreign_address = se_foreign;
  services.semantic_bindings = &s->semantics;
  s->services = qa_app_sim_rerelease_guest_services_create(&services); if (s->services == NULL) qa_range_error("Out of memory");
  s->options.services.base.engine = engine; /* the same actors and authorities as the caller's engine */
  qa_app_sim_rerelease_guest_source_options source; memset(&source, 0, sizeof source);
  qa_app_sim_rerelease_guest_services_host_options(s->services, &source.host);
  source.pickups = source.host.has_pickups ? source.host.pickup_admission : NULL; source.host.has_pickups = false; source.host.pickup_admission = NULL;
  source.host.intercept_ctx = s; source.host.intercept_import = intercept;
  source.clock_ctx = options->clock_ctx; source.now_milliseconds = options->now_milliseconds; source.performance_counter = options->performance_counter; source.performance_frequency = options->performance_frequency;
  source.services_ctx = s; source.services = bind_memory;
  s->source = qa_app_sim_rerelease_guest_source_create(options->prepared, &source);
  qa_compat_q2_rerelease_q2_guest_host *host = qa_app_sim_rerelease_guest_source_host(s->source);
  qa_app_sim_rerelease_guest_services_bind_host(s->services, host);
  s->profile = qa_compat_q2_rerelease_weapon_profile_create(qa_compat_q2_rerelease_q2_guest_host_get_module(host), qa_app_sim_rerelease_guest_source_image_base(s->source), &s->declaration);
  if (s->profile == NULL) qa_range_error("Out of memory");
  s->initialization_entities = qa_compat_q2_rerelease_weapon_initialization_entities(options->map.entities, s->profile->initialization_classes);
  for (size_t i = 0; i < s->profile->initial_cvar_count; i++) { qa_cvar_snapshot set; qa_cvar_registry_set(options->services.base.cvars, s->profile->initial_cvars[i].name, s->profile->initial_cvars[i].value, true, &set); qa_cvar_read_free(&set); }
  qa_app_sim_rerelease_guest_source_init_loading(s->source, options->next_frame, options->next_frame_ctx);
  qa_compat_q2_rerelease_q2_guest_host_spawn_entities_loading(host, options->map.map, s->initialization_entities, options->map.spawn_point != NULL ? options->map.spawn_point : "", options->next_frame, options->next_frame_ctx);
  qa_app_sim_rerelease_guest_services_complete_spawn(s->services); drain(s);
  s->subscription = qa_session_actor_registry_on_release(options->services.base.engine->actors, on_release, s); s->has_subscription = true;
}
void qa_app_sim_rerelease_weapon_behavior_source_destroy(source_t *s) {
  if (s == NULL) return;
  if (s->has_subscription) qa_session_actor_registry_off_release(s->options.services.base.engine->actors, s->subscription);
  if (s->profile != NULL) qa_compat_q2_rerelease_weapon_profile_destroy(s->profile);
  qa_app_sim_rerelease_guest_source_destroy(s->source); qa_app_sim_rerelease_guest_services_destroy(s->services);
  for (instance_state *st = s->states; st != NULL;) { instance_state *next = st->next; free(st); st = next; }
  free(s->engine_copy); free(s->initialization_entities);
  free(s->bindings); free(s->slots); free(s->retired); free(s->instances); free(s->pending);
  if (s->has_declaration) qa_compat_q2_native_weapon_declaration_free(&s->declaration);
  free(s);
}
qa_native_weapon_behavior_declaration qa_app_sim_rerelease_weapon_behavior_source_declaration(const source_t *s) { return s->declaration; }
qa_weapon_behavior_definition qa_app_sim_rerelease_weapon_behavior_source_definition(const source_t *s) { return profile_of((source_t *)s)->definition; }
qa_weapon_behavior_source qa_app_sim_rerelease_weapon_behavior_source_as_source(source_t *s) {
  qa_weapon_behavior_source out; memset(&out, 0, sizeof out); out.ctx = s; out.definition = qa_app_sim_rerelease_weapon_behavior_source_definition(s); out.attach = as_attach; out.resume = as_resume; return out;
}
typedef struct checkpoint_call { source_t *s; void (*next_frame)(void *ctx); void *frame_ctx; qa_app_sim_rerelease_weapon_behavior_checkpoint out; } checkpoint_call;
static void free_partial_checkpoint(void *arg) { qa_app_sim_rerelease_weapon_behavior_checkpoint_free(arg); }
static void checkpoint_body(void *ctx) {
  checkpoint_call *c = ctx; source_t *s = c->s; void (*next_frame)(void *ctx) = c->next_frame; void *frame_ctx = c->frame_ctx;
  qa_compat_q2_rerelease_q2_guest_host *host = host_of(s); qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  qa_app_sim_rerelease_weapon_behavior_checkpoint *out = &c->out;
  qa_cleanup partial; qa_cleanup_push(&partial, free_partial_checkpoint, out); /* the saves read so far */
  out->game = qa_compat_q2_rerelease_q2_guest_host_write_save_loading(host, "game", false, next_frame, frame_ctx);
  out->level = qa_compat_q2_rerelease_q2_guest_host_write_save_loading(host, "level", false, next_frame, frame_ctx);
  if (s->pending_count != 0) fail("Primary actors changed during native component capture");
  out->version = 1; out->declaration = s->declaration; out->definition = p->definition;
  out->map = s->options.map.map; out->entities = s->options.map.entities; out->spawn_point = s->options.map.spawn_point != NULL ? s->options.map.spawn_point : ""; out->time = s->time;
  qa_save_value *cvars = qa_cvar_registry_capture_world_transfer_state(s->options.services.base.cvars);
  out->cvars = qa_encode_checkpoint_value(cvars, &out->cvars_length); qa_save_value_free(cvars);
  size_t count = 0; const qa_app_sim_configstring *strings = qa_app_sim_rerelease_guest_services_configstrings(s->services, &count);
  out->configstrings = calloc(count + 1, sizeof *out->configstrings); out->configstring_count = count;
  for (size_t i = 0; i < count; i++) { out->configstrings[i].index = strings[i].index; out->configstrings[i].value = strings[i].value; }
  out->retired = calloc(s->retired_count + 1, sizeof *out->retired); out->retired_count = s->retired_count;
  for (size_t i = 0; i < s->retired_count; i++) { out->retired[i].actor = qa_saved_actor_id_of(s->retired[i].actor); out->retired[i].trajectory = s->retired[i].trajectory; }
  out->bindings = calloc(s->binding_count + 1, sizeof *out->bindings); out->binding_count = s->binding_count;
  for (size_t i = 0; i < s->binding_count; i++) {
    out->bindings[i].slot = s->bindings[i].slot; out->bindings[i].actor = qa_saved_actor_id_of(s->bindings[i].actor); out->bindings[i].kind = s->bindings[i].kind;
    out->bindings[i].generation = p->generation(p->ctx, view_of(s, s->bindings[i].slot));
  }
  qa_cleanup_pop(&partial);
}
/* TS: busy around the capture, then finally { busy = false; flushReleases() } (a raise there replaces the capture's). */
qa_app_sim_rerelease_weapon_behavior_checkpoint qa_app_sim_rerelease_weapon_behavior_source_checkpoint(source_t *s, void (*next_frame)(void *ctx), void *frame_ctx) {
  if (s->busy) fail("Cannot save an active native weapon call");
  s->busy = true;
  checkpoint_call call; memset(&call, 0, sizeof call); call.s = s; call.next_frame = next_frame; call.frame_ctx = frame_ctx;
  qa_caught_error caught; bool ok = qa_try(checkpoint_body, &call, &caught);
  s->busy = false;
  if (!ok) { flush_releases(s); qa_rethrow(&caught); }
  qa_cleanup node; qa_cleanup_push(&node, free_partial_checkpoint, &call.out);
  flush_releases(s);
  qa_cleanup_pop(&node);
  return call.out;
}
void qa_app_sim_rerelease_weapon_behavior_checkpoint_free(qa_app_sim_rerelease_weapon_behavior_checkpoint *c) {
  free(c->game.native); free(c->level.native);
  qa_compat_q2_rerelease_deferred_damage_free_saves((qa_compat_q2_rerelease_deferred_damage_save *)c->game.deferred_damage, c->game.deferred_damage_count);
  qa_compat_q2_rerelease_deferred_damage_free_saves((qa_compat_q2_rerelease_deferred_damage_save *)c->level.deferred_damage, c->level.deferred_damage_count);
  free((void *)c->cvars); free(c->configstrings); free(c->retired); free(c->bindings); memset(c, 0, sizeof *c);
}
static qa_owned_actor default_resolve(void *ctx, qa_saved_actor_id saved) {
  source_t *s = ctx; qa_owned_actor owned; qa_actor_id id = qa_session_actor_registry_reference_saved(s->options.services.base.engine->actors, saved, "checkpoint");
  if (!qa_session_actor_registry_resolve_owned(s->options.services.base.engine->actors, id, &owned)) fail("Saved native component actor is unavailable");
  return owned;
}
static bool text_eq(const char *a, const char *b) { return strcmp(a != NULL ? a : "", b != NULL ? b : "") == 0; }
typedef struct restore_call {
  source_t *s; const qa_app_sim_rerelease_weapon_behavior_checkpoint *saved;
  void *resolve_ctx; qa_owned_actor (*resolve)(void *ctx, qa_saved_actor_id saved);
  void (*next_frame)(void *ctx); void *frame_ctx;
} restore_call;
static void restore_body(void *ctx);
void qa_app_sim_rerelease_weapon_behavior_source_restore_with(source_t *s, qa_app_sim_rerelease_weapon_behavior_checkpoint saved, void *resolve_ctx,
    qa_owned_actor (*resolve)(void *ctx, qa_saved_actor_id saved), void (*next_frame)(void *ctx), void *frame_ctx) {
  if (resolve == NULL) { resolve = default_resolve; resolve_ctx = s; }
  if (s->busy || s->binding_count != 0 || saved.version != 1 || !qa_same_weapon_behavior(saved.definition, qa_app_sim_rerelease_weapon_behavior_source_definition(s))
      || !qa_compat_q2_same_native_weapon_declaration(&saved.declaration, &s->declaration)
      || !text_eq(saved.map, s->options.map.map) || !text_eq(saved.entities, s->options.map.entities) || !text_eq(saved.spawn_point, s->options.map.spawn_point)
      || !isfinite(saved.time) || saved.time < 0) fail("Incompatible native trajectory checkpoint");
  s->busy = true;
  restore_call call = {s, &saved, resolve_ctx, resolve, next_frame, frame_ctx};
  qa_caught_error caught; bool ok = qa_try(restore_body, &call, &caught);
  /* finally { busy = false; flushReleases() }: a raise there replaces the restore's. */
  s->busy = false; flush_releases(s);
  if (!ok) qa_rethrow(&caught);
}
static void restore_body(void *ctx) {
  restore_call *c = ctx; source_t *s = c->s; const qa_app_sim_rerelease_weapon_behavior_checkpoint *saved = c->saved;
  void *resolve_ctx = c->resolve_ctx; qa_owned_actor (*resolve)(void *ctx, qa_saved_actor_id saved) = c->resolve;
  void (*next_frame)(void *ctx) = c->next_frame; void *frame_ctx = c->frame_ctx;
  qa_compat_q2_rerelease_q2_guest_host *host = host_of(s); qa_compat_q2_rerelease_weapon_profile *p = profile_of(s);
  qa_save_value *cvars = qa_decode_checkpoint_value(saved->cvars, saved->cvars_length);
  qa_cleanup cvars_node; qa_cleanup_push(&cvars_node, free_save_value, cvars);
  qa_cvar_registry_restore_save_state(s->options.services.base.cvars, cvars);
  qa_cleanup_pop(&cvars_node); qa_save_value_free(cvars);
  refresh(s);
  qa_compat_q2_rerelease_q2_guest_host_read_save_loading(host, "game", saved->game, next_frame, frame_ctx, "checkpoint");
  qa_compat_q2_rerelease_q2_guest_host_spawn_entities_loading(host, saved->map, s->initialization_entities, saved->spawn_point, next_frame, frame_ctx);
  qa_app_sim_configstring *strings = calloc(saved->configstring_count + 1, sizeof *strings); if (strings == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < saved->configstring_count; i++) { strings[i].index = saved->configstrings[i].index; strings[i].value = saved->configstrings[i].value; }
  qa_cleanup strings_node; qa_cleanup_push(&strings_node, free, strings);
  qa_app_sim_rerelease_guest_services_restore_configstrings(s->services, strings, saved->configstring_count);
  qa_cleanup_pop(&strings_node); free(strings);
  for (size_t i = 0; i < saved->binding_count; i++) {
    double slot = saved->bindings[i].slot;
    if (slot != floor(slot) || slot < 1 || slot > 9007199254740991.0 || binding_at(s, slot) != NULL) fail("Invalid native component saved slot");
    qa_owned_actor owner = resolve(resolve_ctx, saved->bindings[i].actor); if (slot_of(s, owner.id) != NULL) fail("Duplicate native component saved actor");
    const char *kind = strcmp(saved->bindings[i].kind, "client") == 0 ? "client" : strcmp(saved->bindings[i].kind, "target") == 0 ? "target" : "projectile";
    set_binding(s, slot, owner, kind, saved->bindings[i].generation); set_slot(s, owner.id, slot);
  }
  for (size_t i = 0; i < saved->retired_count; i++) {
    qa_owned_actor owner = resolve(resolve_ctx, saved->retired[i].actor);
    if (slot_of(s, owner.id) != NULL || retired_of(s, owner.id) != NULL) fail("Invalid retired native trajectory actor");
    GROW(s->retired, s->retired_count, s->retired_capacity);
    s->retired[s->retired_count].actor = owner.id; s->retired[s->retired_count++].trajectory = saved->retired[i].trajectory;
  }
  qa_compat_q2_rerelease_q2_guest_host_read_save_loading(host, "level", saved->level, next_frame, frame_ctx, "checkpoint");
  for (size_t i = 0; i < s->binding_count; i++) {
    if (p->generation(p->ctx, view_of(s, s->bindings[i].slot)) != s->bindings[i].generation) fail("Native source save changed projectile generation");
    if (strcmp(s->bindings[i].kind, "client") == 0) (void)qa_compat_q2_rerelease_q2_guest_host_reserve_client(host, s->bindings[i].slot);
  }
  s->time = saved->time; p->time(p->ctx, (int64_t)qa_js_round(saved->time * 1000)); drain(s);
}
void qa_app_sim_rerelease_weapon_behavior_source_restore(source_t *s, qa_app_sim_rerelease_weapon_behavior_checkpoint saved, void (*next_frame)(void *ctx), void *frame_ctx) {
  qa_app_sim_rerelease_weapon_behavior_source_restore_with(s, saved, NULL, NULL, next_frame, frame_ctx);
}
static void run_close(void *ctx) { qa_app_sim_rerelease_guest_source_close(ctx); }
void qa_app_sim_rerelease_weapon_behavior_source_close(source_t *s) {
  if (s->closed) return;
  if (s->busy) fail("Cannot close an active native weapon component");
  s->closed = true;
  if (s->has_subscription) { qa_session_actor_registry_off_release(s->options.services.base.engine->actors, s->subscription); s->has_subscription = false; }
  qa_caught_error caught; bool completed = s->source == NULL || qa_try(run_close, s->source, &caught);
  s->binding_count = 0; s->slot_count = 0; s->instance_count = 0; s->retired_count = 0; s->pending_count = 0;
  if (!completed) qa_rethrow(&caught);
}
