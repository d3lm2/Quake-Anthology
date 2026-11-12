/* Port of src/compat/q2/rerelease/foreign-actors.ts. GPL-2.0-or-later.
 * RemovedNativeDamage is a native throw on the port's one unwind mechanism
 * (qa/compat/q2/native_throw.h): its token is the damage frame, the request's
 * identity, and the damageNative that owns the frame catches it. */
#include "qa/compat/q2/rerelease/foreign_actors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/combat_binding.h" /* qa_compat_q2_lower_native_combat_arguments */
#include "qa/compat/q2/native_damage.h"
#include "qa/compat/q2/native_mod_host_api.h" /* isDeepStrictEqual over armor states */
#include "qa/compat/q2/native_throw.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/compat/q2/rerelease/source_state.h"
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/world/actors/body.h"
#include "qa/foundation/js.h"

/* A projection stays allocated until the owner is destroyed: TS holds entries by reference across guest calls
 * that can take them out of both maps. */
typedef struct projection projection_t;
struct projection {
  qa_owned_actor actor; qa_raw_entity_view view; double generation;
  bool releasing, syncing;
  projection_t *next_owned;
};
/* Each frame is its own block (TS: an object): a pointer to one, and the actor ids its request points at, stay
 * valid while deeper frames are pushed. */
typedef struct {
  qa_damage_request request; qa_actor_id attacker, inflictor;
  bool entered, has_stack, observing; uint64_t stack;
} frame_t;
typedef struct {
  frame_t *frame; qa_actor_id actor;
  void *ctx; bool (*committed)(void *ctx);
} publication_t;
/* TS #armorBindings value: an object the damage in progress and the stage removals keep by reference after it
 * left the map. `holds` counts those references. */
typedef struct {
  qa_owned_actor actor;
  qa_armor_stage_intercept_fn regular, powered; void *regular_ctx, *powered_ctx;
  void *armor_ctx; qa_armor_state (*armor)(void *ctx);
  bool listed; size_t holds;
} armor_binding_t;
typedef struct stage_ctx {
  qa_compat_q2_rerelease_foreign_actors *f; qa_raw_entity_view view;
  void *armor_ctx; qa_armor_state (*armor)(void *ctx); qa_protection_channel channel;
  struct stage_ctx *next;
} stage_ctx_t;
struct qa_compat_q2_rerelease_foreign_actors {
  qa_compat_q2_rerelease_q2_guest_host *host;
  qa_compat_q2_rerelease_native_entries entries;
  qa_compat_q2_rerelease_foreign_damage_services services;
  qa_compat_q2_rerelease_deferred_damage *deferred;
  /* TS #actors (key: actor id) and #slots (key: view.slot), each in insertion order. The two maps are separate:
   * an entry can be in one and not the other, and Map.set replaces the value under a key. */
  projection_t **actors; size_t actor_count, actor_cap;
  projection_t **slots; size_t slot_count, slot_cap;
  projection_t *owned_projections;
  frame_t **frames; size_t frame_count, frame_cap;
  publication_t *publications; size_t publication_count, publication_cap;
  armor_binding_t **bindings; size_t binding_count, binding_cap;
  stage_ctx_t *stages;
  uint64_t hook_handles[3]; bool hook_is_inline[3]; size_t hook_count;
  int power_bypass; /* 0 null, 1 pending, 2 stack */ uint64_t bypass_stack;
  qa_guest_address image;
  uint64_t remove_entry, remove_continuation; bool has_remove_entry, has_remove_continuation;
  qa_compat_q2_guest_call_signature damage_signature, power_signature;
  qa_guest_value_layout *sig_storage[2]; /* one entry per declared argument: a declared call has no limit */
  char *damage_id, *power_id;
  bool restoring; qa_compat_q2_rerelease_projection_save *restoring_saved; size_t restoring_count;
  char restoring_domain[32];
  bool closed;
  qa_compat_q2_rerelease_projection_save *save_scratch; size_t save_cap;
};
static qa_compat_q2_rerelease_guest_module *famod(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_q2_guest_host_get_module(f->host);
}
static qa_compat_q2_memory *fmem(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_guest_module_get_memory(famod(f));
}
static qa_q2_foundation_host *fengine(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_q2_guest_host_get_options(f->host)->engine;
}
static qa_compat_q2_runner *frunner(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_guest_module_get_options(famod(f))->runner;
}
static uint64_t rsp_of(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_runner *r = frunner(f);
  return r->read_rsp(r->ctx, 64);
}
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Q2 rerelease foreign actors out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Q2 rerelease foreign actors out of memory"); return q; }
/* TS try { body } finally { cleanup }, for every error class. */
static void run_finally(void (*body)(void *), void *body_ctx, void (*cleanup)(void *), void *cleanup_ctx) {
  qa_compat_q2_native_thrown thrown; bool completed = qa_compat_q2_native_try(body, body_ctx, &thrown);
  cleanup(cleanup_ctx);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
static qa_compat_q2_rerelease_source_edict *xsource(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view) {
  qa_compat_q2_rerelease_source_edict *e = qa_compat_q2_rerelease_source_edict_create(view, famod(f));
  if (!e) qa_range_error("Q2 rerelease foreign actors out of memory");
  return e;
}
static void source_release(void *v) { qa_compat_q2_rerelease_source_edict_destroy(v); }
/* new RereleaseSourceEdict(view, module).generation(): the scratch object goes when the guest read raises too. */
static double generation_of(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view) {
  qa_compat_q2_rerelease_source_edict *source = xsource(f, view);
  qa_cleanup owned; qa_cleanup_push(&owned, source_release, source);
  double generation = qa_compat_q2_rerelease_source_edict_generation(source);
  qa_cleanup_pop(&owned); qa_compat_q2_rerelease_source_edict_destroy(source);
  return generation;
}
static bool inuse_of(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view) {
  qa_compat_q2_rerelease_source_edict *source = xsource(f, view);
  qa_cleanup owned; qa_cleanup_push(&owned, source_release, source);
  bool inuse = qa_compat_q2_mem_read_u8(fmem(f), qa_compat_q2_rerelease_source_edict_at(source, "shared.inuse")) != 0;
  qa_cleanup_pop(&owned); qa_compat_q2_rerelease_source_edict_destroy(source);
  return inuse;
}
static bool field_pointer(qa_compat_q2_rerelease_foreign_actors *f, const qa_compat_q2_native_combat_call *call,
                          qa_compat_q2_guest_call_signature signature, qa_guest_address *out) {
  qa_compat_q2_runner *r = frunner(f);
  qa_guest_call_value value = r->read_combat_field(r->ctx, call, signature, "target");
  return qa_compat_q2_guest_pointer(&value, 1, 0, out);
}
static qa_vec3 vector_at(qa_compat_q2_rerelease_foreign_actors *f, qa_guest_address address) {
  qa_compat_q2_memory *m = fmem(f);
  double x = qa_compat_q2_mem_read_f32(m, address);
  double y = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 4));
  double z = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 8));
  return (qa_vec3){x, y, z};
}
static qa_actor_id actor_at(qa_compat_q2_rerelease_foreign_actors *f, qa_guest_address address) {
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, table.from_pointer(table.ctx, address), &actor))
    qa_raise("Error", 0, 0, "Native damage references a stale edict");
  return actor.id;
}
/* --- the two maps --- */
static bool same_slot(double a, double b) { return a == b || (a != a && b != b); } /* Map keys: SameValueZero */
static projection_t *actors_get(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id id) {
  for (size_t i = 0; i < f->actor_count; i++) if (qa_same_actor(f->actors[i]->actor.id, id)) return f->actors[i];
  return NULL;
}
static projection_t *slots_get(qa_compat_q2_rerelease_foreign_actors *f, double slot) {
  for (size_t i = 0; i < f->slot_count; i++) if (same_slot(f->slots[i]->view.slot, slot)) return f->slots[i];
  return NULL;
}
/* Map.set: a key that is present keeps its place and takes the new value. */
static void actors_set(qa_compat_q2_rerelease_foreign_actors *f, projection_t *entry) {
  for (size_t i = 0; i < f->actor_count; i++) if (qa_same_actor(f->actors[i]->actor.id, entry->actor.id)) { f->actors[i] = entry; return; }
  if (f->actor_count == f->actor_cap) { f->actor_cap = f->actor_cap ? f->actor_cap * 2 : 8; f->actors = xrealloc(f->actors, f->actor_cap * sizeof *f->actors); }
  f->actors[f->actor_count++] = entry;
}
static void slots_set(qa_compat_q2_rerelease_foreign_actors *f, projection_t *entry) {
  for (size_t i = 0; i < f->slot_count; i++) if (same_slot(f->slots[i]->view.slot, entry->view.slot)) { f->slots[i] = entry; return; }
  if (f->slot_count == f->slot_cap) { f->slot_cap = f->slot_cap ? f->slot_cap * 2 : 8; f->slots = xrealloc(f->slots, f->slot_cap * sizeof *f->slots); }
  f->slots[f->slot_count++] = entry;
}
static void actors_delete(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id id) {
  for (size_t i = 0; i < f->actor_count; i++) if (qa_same_actor(f->actors[i]->actor.id, id)) {
    memmove(&f->actors[i], &f->actors[i + 1], (f->actor_count - i - 1) * sizeof *f->actors); f->actor_count--; return;
  }
}
static void slots_delete(qa_compat_q2_rerelease_foreign_actors *f, double slot) {
  for (size_t i = 0; i < f->slot_count; i++) if (same_slot(f->slots[i]->view.slot, slot)) {
    memmove(&f->slots[i], &f->slots[i + 1], (f->slot_count - i - 1) * sizeof *f->slots); f->slot_count--; return;
  }
}
static projection_t *new_projection(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor, qa_raw_entity_view view, double generation) {
  projection_t *entry = xmalloc(sizeof *entry);
  memset(entry, 0, sizeof *entry);
  entry->actor = actor; entry->view = view; entry->generation = generation;
  entry->next_owned = f->owned_projections; f->owned_projections = entry;
  return entry;
}
/* [...map.values()]: the entries as they stand, for a walk that changes the map. */
static projection_t **snapshot(projection_t *const *entries, size_t count) {
  projection_t **copy = xmalloc((count ? count : 1) * sizeof *copy);
  if (count) memcpy(copy, entries, count * sizeof *copy);
  return copy;
}
/* --- armor bindings --- */
static armor_binding_t *find_binding(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor) {
  for (size_t i = 0; i < f->binding_count; i++) if (qa_same_owned_actor(f->bindings[i]->actor, actor)) return f->bindings[i];
  return NULL;
}
static void binding_drop(armor_binding_t *binding) { if (!binding->listed && binding->holds == 0) free(binding); }
static void binding_unhold(armor_binding_t *binding) { if (binding->holds > 0) binding->holds--; binding_drop(binding); }
/* #armorBindings.delete(actor): whatever binding is listed under the actor. */
static void bindings_delete(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor) {
  for (size_t i = 0; i < f->binding_count; i++) if (qa_same_owned_actor(f->bindings[i]->actor, actor)) {
    armor_binding_t *binding = f->bindings[i];
    memmove(&f->bindings[i], &f->bindings[i + 1], (f->binding_count - i - 1) * sizeof *f->bindings); f->binding_count--;
    binding->listed = false; binding_drop(binding); return;
  }
}
static frame_t *last_frame(qa_compat_q2_rerelease_foreign_actors *f) {
  return f->frame_count ? f->frames[f->frame_count - 1] : NULL;
}
static bool publishes_inventory(qa_compat_q2_rerelease_foreign_actors *f, frame_t *frame) {
  if (last_frame(f) != frame) return true;
  for (size_t i = f->publication_count; i > 0; i--) {
    publication_t *scope = &f->publications[i - 1];
    if (scope->frame == frame && qa_same_actor(scope->actor, frame->request.target))
      return !scope->committed || scope->committed(scope->ctx);
  }
  return true;
}
static bool intercept_native_damage(qa_compat_q2_rerelease_foreign_actors *f) {
  frame_t *frame = last_frame(f);
  if (frame && frame->has_stack && frame->stack == rsp_of(f)) return false;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address address;
  if (!field_pointer(f, &profile->calls.damage, f->damage_signature, &address)) return false;
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_raw_entity_view view = table.from_pointer(table.ctx, address);
  if (slots_get(f, view.slot)) return false;
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, view, &actor)) return false;
  qa_q2_foundation_host *engine = fengine(f);
  return qa_mod_operation_active(qa_gameplay_authority_damage_operation(engine->combat)) || find_binding(f, actor) != NULL;
}
typedef struct { qa_compat_q2_rerelease_foreign_actors *f; void *ctx; void *(*operation)(void *ctx); void *result; } publication_call_t;
static void publication_body(void *v) { publication_call_t *c = v; c->result = c->operation(c->ctx); }
static void publication_pop(void *v) { publication_call_t *c = v; if (c->f->publication_count > 0) c->f->publication_count--; }
void *qa_compat_q2_rerelease_foreign_actors_with_inventory_publication(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor,
                                                                        void *ctx, bool (*committed)(void *ctx),
                                                                        void *(*operation)(void *ctx)) {
  if (f->publication_count == f->publication_cap) {
    f->publication_cap = f->publication_cap ? f->publication_cap * 2 : 8;
    f->publications = xrealloc(f->publications, f->publication_cap * sizeof *f->publications);
  }
  f->publications[f->publication_count++] = (publication_t){last_frame(f), actor, ctx, committed};
  /* try { return operation(); } finally { this.#inventoryPublications.pop(); } */
  publication_call_t call = {f, ctx, operation, NULL};
  run_finally(publication_body, &call, publication_pop, &call);
  return call.result;
}
bool qa_compat_q2_rerelease_foreign_actors_observing_damage(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor) {
  frame_t *frame = last_frame(f);
  return frame && frame->observing && qa_same_actor(frame->request.target, actor);
}
/* TS engaged: true when the slot's actor is owned by a projection or a restore is rebinding slots, so views must go through lookup. */
bool qa_compat_q2_rerelease_foreign_actors_engaged(qa_compat_q2_rerelease_foreign_actors *f, double slot) {
  return f->restoring || slots_get(f, slot) != NULL;
}
qa_compat_q2_rerelease_lookup qa_compat_q2_rerelease_foreign_actors_lookup(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view,
                                                                           qa_owned_actor *out) {
  projection_t *entry = slots_get(f, view.slot);
  const qa_saved_actor_id *saved = NULL;
  /* new Map(saved.map(...)): a slot saved twice keeps its last actor. */
  if (f->restoring) for (size_t i = 0; i < f->restoring_count; i++) if (same_slot(f->restoring_saved[i].slot, view.slot)) saved = &f->restoring_saved[i].actor;
  if (!entry && saved) {
    qa_session_actor_registry *actors = fengine(f)->actors;
    qa_actor_id id = qa_session_actor_registry_reference_saved(actors, *saved, f->restoring_domain);
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_owned(actors, id, &actor))
      qa_raise("Error", 0, 0, "Saved foreign projection actor is no longer live");
    if (!inuse_of(f, view)) return QA_COMPAT_Q2_RR_LOOKUP_NULL;
    entry = new_projection(f, actor, view, generation_of(f, view));
    actors_set(f, entry);
    slots_set(f, entry);
  }
  if (!entry) return QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED;
  if (entry->generation != generation_of(f, view) || entry->view.address.byte_offset != view.address.byte_offset) {
    slots_delete(f, view.slot);
    actors_delete(f, entry->actor.id);
    return QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED;
  }
  if (entry->releasing || !qa_session_actor_registry_is_live(fengine(f)->actors, entry->actor.id))
    return QA_COMPAT_Q2_RR_LOOKUP_NULL;
  if (out) *out = entry->actor;
  return QA_COMPAT_Q2_RR_LOOKUP_ACTOR;
}
typedef struct { qa_compat_q2_rerelease_foreign_actors *f; projection_t *entry; } projection_call_t;
static void release_body(void *v) {
  projection_call_t *c = v; qa_compat_q2_rerelease_foreign_actors *f = c->f; projection_t *entry = c->entry;
  if (generation_of(f, entry->view) != entry->generation) return;
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_pointer(true, entry->view.address);
  qa_raw_entity_view view = entry->view;
  (void)qa_compat_q2_rerelease_guest_module_invoke(famod(f), f->entries.freed, qa_compat_q2_rerelease_get_free_signature(), &arg, 1, &view, NULL);
}
/* finally: each map gives the entry up only where it still holds this very entry. */
static void release_forget(void *v) {
  projection_call_t *c = v; qa_compat_q2_rerelease_foreign_actors *f = c->f; projection_t *entry = c->entry;
  if (actors_get(f, entry->actor.id) == entry) actors_delete(f, entry->actor.id);
  if (slots_get(f, entry->view.slot) == entry) slots_delete(f, entry->view.slot);
}
static void release_projection(qa_compat_q2_rerelease_foreign_actors *f, projection_t *entry) {
  if (entry->releasing) return;
  entry->releasing = true;
  projection_call_t call = {f, entry};
  run_finally(release_body, &call, release_forget, &call);
}
void qa_compat_q2_rerelease_foreign_actors_released(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor) {
  qa_compat_q2_rerelease_deferred_damage_release(f->deferred, actor);
  projection_t *entry = actors_get(f, actor.id);
  if (entry) release_projection(f, entry);
}
static bool sync_actor(void *v, qa_guest_address address, qa_actor_id *out) {
  *out = actor_at(v, address);
  return true;
}
static qa_guest_address sync_address(void *v, qa_actor_id actor) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  return qa_compat_q2_rerelease_q2_guest_host_address_for_actor(f->host, actor);
}
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f; projection_t *entry;
  qa_body_state body; qa_combat_state_result combat; qa_compat_q2_rerelease_source_edict *source;
} sync_call_t;
static void sync_body(void *v) {
  sync_call_t *c = v; qa_compat_q2_rerelease_foreign_actors *f = c->f; projection_t *entry = c->entry;
  qa_q2_foundation_host *engine = fengine(f); qa_compat_q2_memory *memory = fmem(f);
  qa_compat_q2_rerelease_source_edict *source = c->source; qa_combat_state_result combat = c->combat;
  qa_compat_q2_rerelease_actor_addresses addresses = {f, sync_actor, sync_address};
  qa_body_state_binding binding = qa_compat_q2_rerelease_source_edict_body(source, addresses);
  binding.write(binding.ctx, c->body);
  qa_compat_q2_rerelease_source_edict_set_health(source, combat.has_value ? combat.value.health : 0);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_rerelease_source_edict_at(source, "mass"),
                             combat.has_value ? qa_js_to_int32(combat.value.mass) : 0);
  qa_compat_q2_mem_write_u8(memory, qa_compat_q2_rerelease_source_edict_at(source, "takedamage"),
                            combat.has_value && combat.value.can_take_damage ? 1 : 0);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_rerelease_source_edict_at(source, "movetype"), 0);
  qa_compat_q2_mem_write_u32(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.svflags"), 1);
  qa_guest_address null_address; memset(&null_address, 0, sizeof null_address);
  qa_compat_q2_mem_write_pointer(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.client"), false, null_address);
  qa_guest_address classname = qa_compat_q2_rerelease_source_edict_at(source, "classname");
  qa_guest_address text = qa_compat_q2_rerelease_core_imports_string(qa_compat_q2_rerelease_q2_guest_host_get_core(f->host), "foreign_actor");
  qa_compat_q2_mem_write_pointer(memory, classname, true, text);
  qa_compat_q2_mem_write_u8(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.solid"), 2);
  qa_linked_body_result linked = qa_shared_body_table_linked(engine->bodies, entry->actor.id);
  qa_vec3 absmin = linked.has_value ? linked.value.absolute_bounds.min
                                     : (qa_vec3){c->body.origin.x + c->body.bounds.min.x,
                                                 c->body.origin.y + c->body.bounds.min.y,
                                                 c->body.origin.z + c->body.bounds.min.z};
  qa_vec3 absmax = linked.has_value ? linked.value.absolute_bounds.max
                                     : (qa_vec3){c->body.origin.x + c->body.bounds.max.x,
                                                 c->body.origin.y + c->body.bounds.max.y,
                                                 c->body.origin.z + c->body.bounds.max.z};
  qa_compat_q2_rerelease_source_edict_write_vector(source, "shared.absmin", absmin);
  qa_compat_q2_rerelease_source_edict_write_vector(source, "shared.absmax", absmax);
  qa_compat_q2_mem_write_u8(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.linked"), linked.has_value ? 1 : 0);
}
static void sync_done(void *v) { sync_call_t *c = v; c->entry->syncing = false; }
static void sync_projection(qa_compat_q2_rerelease_foreign_actors *f, projection_t *entry) {
  if (entry->releasing || entry->syncing) return;
  qa_q2_foundation_host *engine = fengine(f);
  qa_body_state_result body = qa_shared_body_table_read(engine->bodies, entry->actor.id);
  qa_combat_state_result combat = qa_gameplay_authority_read(engine->combat, entry->actor.id);
  if (!body.has_value) qa_raise("Error", 0, 0, "Foreign native projection requires an existing body binding");
  /* The body's ground id points into the table's record; keep it by value across the writes. */
  qa_actor_id ground; qa_body_state state = body.value;
  if (state.ground) { ground = *state.ground; state.ground = &ground; }
  qa_compat_q2_rerelease_source_edict *source = xsource(f, entry->view);
  qa_cleanup owned; qa_cleanup_push(&owned, source_release, source);
  if (qa_compat_q2_rerelease_source_edict_generation(source) != entry->generation) {
    qa_cleanup_pop(&owned); qa_compat_q2_rerelease_source_edict_destroy(source);
    actors_delete(f, entry->actor.id);
    slots_delete(f, entry->view.slot);
    (void)qa_compat_q2_rerelease_foreign_actors_address(f, entry->actor.id);
    return;
  }
  entry->syncing = true;
  /* try { the writes } finally { entry.syncing = false; }: a write the guest memory refuses must not leave the
   * projection unsynchronized for good. */
  sync_call_t call = {f, entry, state, combat, source};
  run_finally(sync_body, &call, sync_done, &call);
  qa_cleanup_pop(&owned); qa_compat_q2_rerelease_source_edict_destroy(source);
}
static void address_sync_body(void *v) { projection_call_t *c = v; sync_projection(c->f, c->entry); }
qa_guest_address qa_compat_q2_rerelease_foreign_actors_address(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor) {
  if (f->closed) qa_raise("Error", 0, 0, "Foreign native projections are closed");
  qa_session_actor_registry *actors = fengine(f)->actors;
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(actors, actor, &owned))
    qa_raise("Error", 0, 0, "Cannot project a stale foreign actor");
  projection_t *entry = actors_get(f, actor);
  if (entry && generation_of(f, entry->view) != entry->generation) {
    actors_delete(f, actor);
    slots_delete(f, entry->view.slot);
    entry = NULL;
  }
  if (!entry) {
    qa_guest_call_result spawned = qa_compat_q2_rerelease_guest_module_invoke(
        famod(f), f->entries.spawn, qa_compat_q2_rerelease_get_spawn_signature(), NULL, 0, NULL, NULL);
    qa_guest_address address;
    if (!qa_compat_q2_rerelease_result_pointer(spawned, &address)) qa_raise("Error", 0, 0, "Native G_Spawn returned null");
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
    qa_raw_entity_view view = table.from_pointer(table.ctx, address);
    entry = new_projection(f, owned, view, generation_of(f, view));
    actors_set(f, entry);
    slots_set(f, entry);
    /* Guest code running inside G_Spawn can make the host bind this edict as a native actor before the
     * projection exists. That phantom duplicates the projection's edict; retire it so the slot has one owner. */
    qa_owned_actor phantom;
    if (qa_session_actor_registry_at_source(actors, fmem(f)->module.id, (int64_t)view.slot, &phantom) &&
        !qa_same_owned_actor(phantom, owned))
      qa_session_actor_registry_release(actors, phantom);
  }
  /* try { this.#sync(entry); } catch (error) { this.#release(entry); throw error; } */
  projection_call_t call = {f, entry};
  qa_compat_q2_native_thrown thrown;
  if (!qa_compat_q2_native_try(address_sync_body, &call, &thrown)) {
    release_projection(f, entry);
    qa_compat_q2_native_rethrow(&thrown);
  }
  return entry->view.address; /* TS returns this entry's address whatever the sync did to the maps */
}
void qa_compat_q2_rerelease_foreign_actors_synchronize(qa_compat_q2_rerelease_foreign_actors *f) {
  /* for (const entry of [...this.#actors.values()]) this.#sync(entry): every entry of the snapshot, whether or
   * not an earlier sync took it out of the maps. */
  size_t count = f->actor_count;
  projection_t **copy = snapshot(f->actors, count);
  qa_cleanup owned; qa_cleanup_push(&owned, free, copy);
  for (size_t i = 0; i < count; i++) sync_projection(f, copy[i]);
  qa_cleanup_pop(&owned); free(copy);
}
/* --- armor stages --- */
/* CleanupStack.runAll(): the list is emptied first, then run in registration order. */
static void remove_armor_hooks(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_runner *r = frunner(f);
  uint64_t handles[3]; bool is_inline[3]; size_t count = f->hook_count;
  for (size_t i = 0; i < count; i++) { handles[i] = f->hook_handles[i]; is_inline[i] = f->hook_is_inline[i]; }
  f->hook_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (is_inline[i]) r->remove_inline_region(r->ctx, handles[i]);
    else r->remove_entry(r->ctx, handles[i]);
  }
}
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f; qa_owned_actor actor; armor_binding_t *binding;
  qa_protection_channel channel; qa_armor_stage_intercept_fn fn; void *fn_ctx;
} stage_removal_t;
static void stage_remove(void *v) {
  stage_removal_t *r = v;
  qa_compat_q2_rerelease_foreign_actors *f = r->f;
  armor_binding_t *binding = r->binding; /* the closure's own binding object */
  if (r->channel == QA_PROTECTION_REGULAR && binding->regular == r->fn && binding->regular_ctx == r->fn_ctx) { binding->regular = NULL; binding->regular_ctx = NULL; }
  if (r->channel == QA_PROTECTION_POWERED && binding->powered == r->fn && binding->powered_ctx == r->fn_ctx) { binding->powered = NULL; binding->powered_ctx = NULL; }
  if (!binding->regular && !binding->powered) bindings_delete(f, r->actor);
  binding_unhold(binding);
  free(r);
  if (f->binding_count == 0) remove_armor_hooks(f);
}
static void install_armor_hooks(qa_compat_q2_rerelease_foreign_actors *f);
static void install_body(void *v) { install_armor_hooks(v); }
static qa_armor_stage_removal stage_bind(void *v, qa_armor_stage_intercept_fn intercept, void *intercept_ctx) {
  stage_ctx_t *s = v;
  qa_compat_q2_rerelease_foreign_actors *f = s->f;
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, s->view, &actor))
    qa_raise("Error", 0, 0, "Native armor stage has no live actor");
  armor_binding_t *binding = find_binding(f, actor);
  if (binding) {
    if (s->channel == QA_PROTECTION_REGULAR ? binding->regular != NULL : binding->powered != NULL)
      qa_raise("Error", 0, 0, "Native armor stage already has an owner");
  } else {
    binding = xmalloc(sizeof *binding);
    memset(binding, 0, sizeof *binding);
    binding->actor = actor; binding->armor_ctx = s->armor_ctx; binding->armor = s->armor;
    if (f->binding_count == f->binding_cap) {
      size_t cap = f->binding_cap ? f->binding_cap * 2 : 8; armor_binding_t **grown = realloc(f->bindings, cap * sizeof *grown);
      if (!grown) { free(binding); qa_range_error("Q2 rerelease foreign actors out of memory"); }
      f->bindings = grown; f->binding_cap = cap;
    }
    f->bindings[f->binding_count++] = binding; binding->listed = true;
  }
  if (s->channel == QA_PROTECTION_REGULAR) { binding->regular = intercept; binding->regular_ctx = intercept_ctx; }
  else { binding->powered = intercept; binding->powered_ctx = intercept_ctx; }
  /* try { install the hooks } catch (error) { the stage is unbound again, a binding with no stage leaves the map
   * and the hooks installed so far are removed }. */
  if (f->hook_count == 0) {
    qa_compat_q2_native_thrown thrown;
    if (!qa_compat_q2_native_try(install_body, f, &thrown)) {
      if (s->channel == QA_PROTECTION_REGULAR) { binding->regular = NULL; binding->regular_ctx = NULL; }
      else { binding->powered = NULL; binding->powered_ctx = NULL; }
      if (!binding->regular && !binding->powered) bindings_delete(f, actor);
      remove_armor_hooks(f);
      qa_compat_q2_native_rethrow(&thrown);
    }
  }
  stage_removal_t *r = xmalloc(sizeof *r);
  *r = (stage_removal_t){f, actor, binding, s->channel, intercept, intercept_ctx};
  binding->holds++;
  return (qa_armor_stage_removal){r, stage_remove};
}
qa_source_armor_stage qa_compat_q2_rerelease_foreign_actors_armor_stage(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view,
                                                                        void *armor_ctx, qa_armor_state (*armor)(void *ctx),
                                                                        qa_protection_channel channel) {
  stage_ctx_t *s = xmalloc(sizeof *s);
  memset(s, 0, sizeof *s);
  s->f = f; s->view = view; s->armor_ctx = armor_ctx; s->armor = armor; s->channel = channel;
  s->next = f->stages; f->stages = s;
  return (qa_source_armor_stage){s, stage_bind};
}
static void power_observe(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  if (f->power_bypass == 1) { f->power_bypass = 2; f->bypass_stack = rsp_of(f); }
}
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f;
  const qa_guest_call_value *args; size_t count; qa_raw_entity_view view;
  int previous; uint64_t previous_stack; double value;
} power_original_t;
static void power_original_body(void *v) {
  power_original_t *o = v; qa_compat_q2_rerelease_foreign_actors *f = o->f;
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(
      famod(f), f->entries.power_armor, f->power_signature, o->args, o->count, &o->view, NULL);
  if (result.kind != QA_GUEST_VALUE_INT32) qa_raise("Error", 0, 0, "Native power stage returned a non-integer result");
  o->value = result.number_value;
}
static void power_original_restore(void *v) { power_original_t *o = v; o->f->power_bypass = o->previous; o->f->bypass_stack = o->previous_stack; }
static double power_original(void *v) {
  power_original_t *o = v;
  qa_compat_q2_rerelease_foreign_actors *f = o->f;
  o->previous = f->power_bypass; o->previous_stack = f->bypass_stack;
  f->power_bypass = 1;
  /* try { ... } finally { this.#powerBypass = previous; } */
  run_finally(power_original_body, o, power_original_restore, o);
  return o->value;
}
static qa_guest_call_result power_invoke(void *v, const qa_guest_call_context *context,
                                         const qa_guest_call_value *source_args, size_t source_count) {
  (void)context;
  qa_compat_q2_rerelease_foreign_actors *f = v;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_call_value args[16]; /* the operation's semantic fields: at most ten */
  size_t count = qa_compat_q2_read_native_combat_arguments(&profile->calls.power_armor, QA_Q2CO_POWER_ARMOR, source_args, source_count, 8, args);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_raw_entity_view view = table.from_pointer(table.ctx, qa_compat_q2_guest_required_pointer(args, count, 0));
  qa_owned_actor actor;
  bool has_actor = qa_compat_q2_rerelease_q2_guest_host_actor(f->host, view, &actor);
  frame_t *frame = last_frame(f);
  armor_binding_t *binding = has_actor ? find_binding(f, actor) : NULL;
  qa_armor_stage_intercept_fn intercept = binding ? binding->powered : NULL;
  if (!has_actor || !intercept || !frame || !qa_same_actor(frame->request.target, actor.id))
    qa_raise("Error", 0, 0, "Native power stage has no active source damage request");
  void *intercept_ctx = binding->powered_ctx;
  power_original_t original = {f, source_args, source_count, view, 0, 0, 0};
  qa_armor_stage_input input; memset(&input, 0, sizeof input);
  input.request = frame->request;
  input.amount = (double)qa_compat_q2_guest_integer(args, count, 3);
  input.direction = frame->request.direction;
  input.point = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 1));
  input.normal = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 2));
  input.flags = qa_compat_q2_native_armor_flags((double)qa_compat_q2_guest_integer(args, count, 4));
  double saved = intercept(intercept_ctx, input, power_original, &original);
  /* throw new RemovedNativeDamage(frame.request): the guest's T_Damage is abandoned and the damageNative that
   * owns this request catches it (the frame is the request's identity). */
  if (!qa_session_actor_registry_is_live(fengine(f)->actors, actor.id))
    qa_compat_q2_native_throw(frame, QA_COMPAT_Q2_REMOVED_NATIVE_DAMAGE_MESSAGE);
  return qa_compat_q2_rerelease_guest_int(trunc(saved));
}
static bool power_accepts(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  if (f->power_bypass == 2 && f->bypass_stack == rsp_of(f)) return false;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address address;
  if (!field_pointer(f, &profile->calls.power_armor, f->power_signature, &address)) return false;
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, table.from_pointer(table.ctx, address), &actor)) return false;
  armor_binding_t *binding = find_binding(f, actor);
  return binding && binding->powered != NULL;
}
/* guest/core/registers.h cannot share a TU with contracts/execution.h
 * (both define qa_guest_address), so the register file is reached
 * through a layout mirror: qa_guest_processor_state begins with the
 * architecture word followed by qa_integer_register_file
 * {architecture, bytes[128]}; register indices are RAX..R15 = 0..15. */
extern uint64_t qa_integer_register_file_read(const void *regs, int reg, int width, bool high_byte);
extern void qa_integer_register_file_write(void *regs, int reg, int width, uint64_t value, bool high_byte);
static void *register_file_of(qa_compat_q2_runner *r) {
  return (char *)r->cpu_state + sizeof(int);
}
static int register_of(const char *name) {
  static const char *const names[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                      "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
  for (int i = 0; i < 16; i++) if (name && strcmp(names[i], name) == 0) return i;
  qa_range_error("Native regular stage has an unknown register");
}
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f;
  qa_guest_address source_stack;
  qa_compat_q2_inline_continuation *continuation;
  const qa_compat_q2_rerelease_world_location *result_location;
  bool executed;
} regular_ctx_t;
/* The TS bigint, as its low 64 bits. */
static int64_t read_location(regular_ctx_t *c, qa_compat_q2_rerelease_world_location location) {
  qa_compat_q2_memory *memory = fmem(c->f);
  if (location.is_register) {
    qa_compat_q2_runner *r = frunner(c->f);
    uint64_t value = qa_integer_register_file_read(register_file_of(r), register_of(location.reg),
                                                   strcmp(location.storage, "pointer") == 0 ? 64 : 32, false);
    return strcmp(location.storage, "int32") == 0 ? (int64_t)(int32_t)(uint32_t)value : (int64_t)value;
  }
  qa_guest_address address = qa_compat_q2_mem_offset(memory, c->source_stack, (int64_t)location.offset);
  if (strcmp(location.storage, "pointer") == 0) return (int64_t)qa_compat_q2_mem_read_u64(memory, address);
  return strcmp(location.storage, "int32") == 0 ? qa_compat_q2_mem_read_i32(memory, address)
                                                 : (int64_t)qa_compat_q2_mem_read_u32(memory, address);
}
/* Number(bigint) for a value read with read_location: a pointer-width value is unsigned. */
static double location_number(qa_compat_q2_rerelease_world_location location, int64_t value) {
  return strcmp(location.storage, "pointer") == 0 ? (double)(uint64_t)value : (double)value;
}
/* BigInt(value) for an integral number, as the two's-complement low 64 bits a register or memory store keeps.
 * A number that is not an integer is JavaScriptCore's RangeError. */
static int64_t bigint_bits(double value) {
  if (!isfinite(value) || value != trunc(value)) qa_range_error("Not an integer");
  uint64_t magnitude = (uint64_t)fmod(fabs(value), 18446744073709551616.0);
  uint64_t bits = value < 0 ? (uint64_t)0 - magnitude : magnitude;
  int64_t out; memcpy(&out, &bits, sizeof out); return out;
}
static void write_location(regular_ctx_t *c, qa_compat_q2_rerelease_world_location location, int64_t value) {
  qa_compat_q2_memory *memory = fmem(c->f);
  if (location.is_register) {
    qa_compat_q2_runner *r = frunner(c->f);
    qa_integer_register_file_write(register_file_of(r), register_of(location.reg),
                                   strcmp(location.storage, "pointer") == 0 ? 64 : 32, (uint64_t)value, false);
    return;
  }
  qa_guest_address address = qa_compat_q2_mem_offset(memory, c->source_stack, (int64_t)location.offset);
  /* BigInt.asIntN(32, value) / BigInt.asUintN(32, value): the exact low 32 bits (a detour through a double lost
   * them above 2^53). */
  if (strcmp(location.storage, "pointer") == 0) qa_compat_q2_mem_write_u64(memory, address, (uint64_t)value);
  else if (strcmp(location.storage, "int32") == 0) qa_compat_q2_mem_write_i32(memory, address, (int32_t)(uint32_t)(uint64_t)value);
  else qa_compat_q2_mem_write_u32(memory, address, (uint32_t)(uint64_t)value);
}
static double regular_original(void *v) {
  regular_ctx_t *c = v;
  c->continuation->execute(c->continuation->ctx);
  c->executed = true;
  return location_number(*c->result_location, read_location(c, *c->result_location));
}
static void regular_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  qa_compat_q2_memory *memory = fmem(f);
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address_result source_stack = qa_compat_q2_mem_pointer(memory, (int64_t)rsp_of(f));
  if (!source_stack.has_value) qa_raise("Error", 0, 0, "Native regular stage has no source stack");
  regular_ctx_t ctx = {f, source_stack.value, continuation, &profile->regular_armor.result, false};
  qa_guest_address_result address = qa_compat_q2_mem_pointer(memory, read_location(&ctx, profile->regular_armor.target));
  qa_owned_actor actor;
  bool has_actor = false;
  if (address.has_value) {
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
    has_actor = qa_compat_q2_rerelease_q2_guest_host_actor(f->host, table.from_pointer(table.ctx, address.value), &actor);
  }
  armor_binding_t *binding = has_actor ? find_binding(f, actor) : NULL;
  qa_armor_stage_intercept_fn intercept = binding ? binding->regular : NULL;
  if (!intercept) { continuation->execute(continuation->ctx); return; }
  void *intercept_ctx = binding->regular_ctx;
  frame_t *frame = last_frame(f);
  if (!has_actor || !frame || !qa_same_actor(frame->request.target, actor.id))
    qa_raise("Error", 0, 0, "Native regular stage has no active source damage request");
  qa_guest_address_result point = qa_compat_q2_mem_pointer(memory, read_location(&ctx, profile->regular_armor.point));
  if (!point.has_value) qa_raise("Error", 0, 0, "Native regular stage has no source arguments");
  qa_guest_address_result normal = qa_compat_q2_mem_pointer(memory, read_location(&ctx, profile->regular_armor.normal));
  if (!normal.has_value) qa_raise("Error", 0, 0, "Native regular stage has no source normal");
  size_t repair_count = profile->regular_armor.repair_count;
  int64_t *repair_values = xmalloc(repair_count * sizeof *repair_values);
  qa_cleanup repair_owned; qa_cleanup_push(&repair_owned, free, repair_values);
  for (size_t i = 0; i < repair_count; i++) repair_values[i] = read_location(&ctx, profile->regular_armor.repair[i].source);
  qa_armor_stage_input input; memset(&input, 0, sizeof input);
  input.request = frame->request;
  input.amount = location_number(profile->regular_armor.amount, read_location(&ctx, profile->regular_armor.amount));
  input.direction = frame->request.direction;
  input.point = vector_at(f, point.value);
  input.normal = vector_at(f, normal.value);
  input.flags = qa_compat_q2_native_armor_flags(location_number(profile->regular_armor.flags, read_location(&ctx, profile->regular_armor.flags)));
  double saved = intercept(intercept_ctx, input, regular_original, &ctx);
  /* throw new RemovedNativeDamage(frame.request), before the region is skipped or its result written. */
  if (!qa_session_actor_registry_is_live(fengine(f)->actors, actor.id))
    qa_compat_q2_native_throw(frame, QA_COMPAT_Q2_REMOVED_NATIVE_DAMAGE_MESSAGE);
  if (!ctx.executed) {
    continuation->skip(continuation->ctx);
    for (size_t i = 0; i < repair_count; i++) write_location(&ctx, profile->regular_armor.repair[i].target, repair_values[i]);
  }
  qa_cleanup_pop(&repair_owned); free(repair_values);
  write_location(&ctx, profile->regular_armor.result, bigint_bits(trunc(saved)));
}
/* `${module.id}${suffix}` with no length limit. */
static char *module_id(qa_compat_q2_rerelease_foreign_actors *f, const char *suffix) {
  const char *id = fmem(f)->module.id ? fmem(f)->module.id : "";
  size_t need = strlen(id) + strlen(suffix) + 1; char *text = xmalloc(need);
  (void)snprintf(text, need, "%s%s", id, suffix);
  return text;
}
static void install_armor_hooks(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_runner *r = frunner(f);
  f->hook_handles[f->hook_count] = r->observe_entry(r->ctx, f->entries.power_armor, f, power_observe);
  f->hook_is_inline[f->hook_count++] = false;
  if (!f->power_id) f->power_id = module_id(f, ":powered-armor");
  f->hook_handles[f->hook_count] = r->bind_entry(r->ctx, f->entries.power_armor, f->power_id, f->power_signature, f, power_invoke, f, power_accepts);
  f->hook_is_inline[f->hook_count++] = false;
  f->hook_handles[f->hook_count] = r->bind_inline_region(r->ctx, f->entries.regular_armor.entry, f->entries.regular_armor.join,
                                                         qa_compat_q2_rerelease_abi, f, regular_region);
  f->hook_is_inline[f->hook_count++] = true;
}
/* --- damage --- */
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f;
  const qa_guest_call_value *source_args; size_t source_count;
} incoming_ctx_t;
static qa_damage_outcome incoming_source_damage(void *v, qa_damage_request request) {
  incoming_ctx_t *c = v;
  return qa_compat_q2_rerelease_foreign_actors_damage_native(c->f, request, c->source_args, c->source_count);
}
static void incoming_damage(qa_compat_q2_rerelease_foreign_actors *f, const qa_guest_call_value *source_args, size_t source_count) {
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_call_value args[16]; /* the operation's semantic fields: at most ten */
  size_t count = qa_compat_q2_read_native_combat_arguments(&profile->calls.damage, QA_Q2CO_DAMAGE, source_args, source_count, 8, args);
  qa_actor_id target = actor_at(f, qa_compat_q2_guest_required_pointer(args, count, 0));
  qa_actor_id inflictor = actor_at(f, qa_compat_q2_guest_required_pointer(args, count, 1));
  qa_actor_id attacker = actor_at(f, qa_compat_q2_guest_required_pointer(args, count, 2));
  const qa_guest_call_value *mod = count > 9 ? &args[9] : NULL;
  if (!mod || mod->kind != QA_GUEST_VALUE_AGGREGATE || mod->bytes.length != 3)
    qa_raise("Error", 0, 0, "Native damage requires the API2023 mod_t ABI");
  qa_q2_native_cause native; memset(&native, 0, sizeof native);
  native.edition = "rerelease"; native.id = mod->bytes.bytes[0];
  native.friendly_fire = mod->bytes.bytes[1] != 0;
  native.no_point_loss = mod->bytes.bytes[2] != 0;
  double canonical;
  if (!qa_q2mp_canonical_cause_from_native(native, &canonical)) qa_raise("Error", 0, 0, "Unclassified native damage cause");
  double damage_flags = (double)qa_compat_q2_guest_integer(args, count, 8);
  qa_damage_request request; memset(&request, 0, sizeof request);
  request.target = target;
  request.amount = (double)qa_compat_q2_guest_integer(args, count, 6);
  request.knockback = (double)qa_compat_q2_guest_integer(args, count, 7);
  request.direction = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 3));
  request.point = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 4));
  request.normal = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 5));
  request.delivery = (qa_js_to_int32(damage_flags) & 1) != 0 ? "radius" : "direct";
  request.attack = f->services.provenance(f->services.ctx, attacker, inflictor, target);
  /* The ids live in this frame; apply() captures the request synchronously. */
  request.attack.attacker = &attacker;
  request.attack.inflictor = &inflictor;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q2;
  request.attack.cause.means_of_death = canonical;
  request.attack.cause.damage_flags = damage_flags;
  request.attack.cause.has_native = true;
  request.attack.cause.native = native;
  qa_gameplay_authority *combat = fengine(f)->combat;
  if (actors_get(f, target)) {
    (void)qa_gameplay_authority_apply(combat, request, NULL, NULL);
  } else {
    incoming_ctx_t ctx = {f, source_args, source_count};
    (void)qa_gameplay_authority_apply(combat, request, incoming_source_damage, &ctx);
  }
  projection_t *entry = actors_get(f, target);
  if (entry && !entry->releasing) sync_projection(f, entry);
}
typedef struct exec exec_t;
typedef struct { exec_t *ex; int kind; } write_obs_t;
typedef struct {
  exec_t *ex; const char *reaction;
  qa_compat_q2_native_combat_call call;
  qa_compat_q2_guest_call_signature signature;
  qa_guest_value_layout *storage; /* one entry per declared argument */
  qa_raw_entity_view view;
} reaction_obs_t;
struct exec {
  qa_compat_q2_rerelease_foreign_actors *f;
  const qa_guest_call_value *source_args; size_t source_count;
  qa_raw_entity_view view;
  qa_compat_q2_rerelease_source_edict *source;
  qa_compat_q2_rerelease_source_client *client;
  frame_t *frame;
  qa_source_damage_observer observer;
  bool has_result; qa_source_damage_result result;
  double health, applied_damage; qa_vec3 velocity;
  bool has_armor; qa_armor_state armor;
  armor_binding_t *power;
  qa_damage_request request;
  bool monster; qa_guest_address attacker, inflictor;
  qa_q2_native_cause native; double damage_flags;
  write_obs_t wobs[3]; uint64_t write_handles[3]; size_t write_count;
  reaction_obs_t robs[2]; uint64_t entry_handles[2]; size_t entry_count;
  bool has_vectors; qa_guest_address vectors;
  void *saved_processor;
  qa_guest_call_value *lowered; size_t lowered_count;
  uint8_t mod_bytes[3];
};
static bool read_armor(exec_t *ex, qa_armor_state *out) {
  if (ex->power && ex->power->armor) { *out = ex->power->armor(ex->power->armor_ctx); return true; }
  qa_combat_state_result state = qa_gameplay_authority_read(fengine(ex->f)->combat, ex->request.target);
  if (!state.has_value) return false;
  *out = state.value.armor;
  return true;
}
static bool exec_current(exec_t *ex) { return last_frame(ex->f) == ex->frame && !ex->has_result; }
/* stopWrites(): the observing flag drops, then removeWrites.runAll(). */
static void stop_writes(exec_t *ex) {
  ex->frame->observing = false;
  qa_compat_q2_memory *memory = fmem(ex->f);
  uint64_t handles[3]; size_t count = ex->write_count;
  for (size_t i = 0; i < count; i++) handles[i] = ex->write_handles[i];
  ex->write_count = 0;
  if (memory->unobserve_writes)
    for (size_t i = 0; i < count; i++) memory->unobserve_writes(memory->ctx, handles[i]);
}
static void write_observed(void *v) {
  write_obs_t *obs = v;
  exec_t *ex = obs->ex;
  if (obs->kind == 0) {
    double after = qa_compat_q2_rerelease_source_edict_get_health(ex->source);
    double before = ex->health; ex->health = after;
    if (!exec_current(ex) || after == before) return;
    ex->applied_damage += before - after;
    qa_damage_mutation m; memset(&m, 0, sizeof m);
    m.kind = QA_DAMAGE_MUTATION_HEALTH; m.health_before = before; m.health_after = after;
    ex->observer.stored(ex->observer.ctx, m);
  } else if (obs->kind == 1) {
    qa_vec3 after = qa_compat_q2_rerelease_source_edict_vector(ex->source, "velocity");
    qa_vec3 before = ex->velocity; ex->velocity = after;
    if (!exec_current(ex)) return;
    qa_damage_mutation m; memset(&m, 0, sizeof m);
    m.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY; m.velocity_before = before; m.velocity_after = after;
    m.movement_provider = ex->request.attack.movement_provider;
    ex->observer.stored(ex->observer.ctx, m);
  } else {
    if (!publishes_inventory(ex->f, ex->frame)) return;
    qa_armor_state after; bool has_after = read_armor(ex, &after);
    bool has_before = ex->has_armor; qa_armor_state before = ex->armor;
    ex->has_armor = has_after; if (has_after) ex->armor = after;
    if (!exec_current(ex)) return;
    if (!has_after || !has_before) qa_raise("Error", 0, 0, "Native armor binding disappeared during damage");
    qa_combat_state_result effective = qa_gameplay_authority_read(fengine(ex->f)->combat, ex->request.target);
    if (!effective.has_value) qa_raise("Error", 0, 0, "Native armor binding disappeared during damage");
    qa_armor_state prior;
    prior.regular = (!ex->power || !ex->power->regular) ? before.regular : effective.value.armor.regular;
    prior.powered = (!ex->power || !ex->power->powered) ? before.powered : effective.value.armor.powered;
    /* isDeepStrictEqual(prior, effective): every member of either armor kind, numbers as Object.is. */
    if (qa_compat_q2_regular_armor_equal(prior.regular, effective.value.armor.regular) &&
        qa_compat_q2_powered_protection_equal(prior.powered, effective.value.armor.powered)) return;
    qa_damage_mutation m; memset(&m, 0, sizeof m);
    m.kind = QA_DAMAGE_MUTATION_ARMOR; m.armor_before = prior; m.armor_after = effective.value.armor;
    ex->observer.stored(ex->observer.ctx, m);
  }
}
static void reaction_observed(void *v) {
  reaction_obs_t *obs = v;
  exec_t *ex = obs->ex;
  if (!exec_current(ex)) return;
  qa_compat_q2_runner *r = frunner(ex->f);
  qa_guest_call_value target = r->read_combat_field(r->ctx, &obs->call, obs->signature, "target");
  qa_guest_address address;
  if (!qa_compat_q2_guest_pointer(&target, 1, 0, &address) || address.byte_offset != obs->view.address.byte_offset) return;
  qa_guest_call_value amount = r->read_combat_field(r->ctx, &obs->call, obs->signature, "amount");
  ex->result = (qa_source_damage_result){(double)qa_compat_q2_guest_integer(&amount, 1, 0), obs->reaction};
  ex->has_result = true;
  stop_writes(ex);
  ex->observer.before_reaction(ex->observer.ctx, ex->result);
}
static void damage_invoke_body(void *v) {
  exec_t *ex = v; qa_compat_q2_rerelease_foreign_actors *f = ex->f;
  (void)qa_compat_q2_rerelease_guest_module_invoke(famod(f), f->entries.damage, f->damage_signature, ex->lowered, ex->lowered_count, &ex->view, NULL);
}
/* The TS try block of damageNative. */
static void damage_body(void *v) {
  exec_t *ex = v;
  qa_compat_q2_rerelease_foreign_actors *f = ex->f;
  qa_compat_q2_memory *memory = fmem(f);
  frame_t *frame = ex->frame; qa_raw_entity_view view = ex->view; qa_damage_request request = ex->request;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  frame->observing = true;
  if (memory->observe_writes) {
    for (int k = 0; k < 3; k++) { ex->wobs[k].ex = ex; ex->wobs[k].kind = k; }
    uint64_t handle = memory->observe_writes(memory->ctx, qa_compat_q2_rerelease_source_edict_at(ex->source, "health"), 4, &ex->wobs[0], write_observed);
    ex->write_handles[ex->write_count++] = handle;
    handle = memory->observe_writes(memory->ctx, qa_compat_q2_rerelease_source_edict_at(ex->source, "velocity"), 12, &ex->wobs[1], write_observed);
    ex->write_handles[ex->write_count++] = handle;
    qa_guest_address client_address;
    if (qa_compat_q2_rerelease_source_edict_client(ex->source, &client_address)) {
      ex->client = qa_compat_q2_rerelease_source_client_create(client_address, famod(f), &profile->client);
      if (!ex->client) qa_range_error("Q2 rerelease foreign actors out of memory");
      handle = memory->observe_writes(memory->ctx, qa_compat_q2_rerelease_source_client_at(ex->client, "pers.inventory"),
                                      profile->client.inventory_count * 4, &ex->wobs[2], write_observed);
      ex->write_handles[ex->write_count++] = handle;
    }
  }
  if (!ex->monster) {
    const char *names[2] = {"pain", "death"};
    const char *fields[2] = {"pain.value", "die.value"};
    qa_compat_q2_runner *r = frunner(f);
    for (int i = 0; i < 2; i++) {
      qa_guest_address callback;
      if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_rerelease_source_edict_at(ex->source, fields[i]), &callback)) continue;
      reaction_obs_t *rob = &ex->robs[i];
      rob->ex = ex; rob->reaction = names[i]; rob->view = view;
      rob->call = i == 0 ? profile->calls.pain : profile->calls.death;
      rob->storage = calloc(rob->call.argument_count ? rob->call.argument_count : 1, sizeof *rob->storage);
      if (!rob->storage) qa_range_error("Q2 rerelease foreign actors out of memory");
      rob->signature = qa_compat_q2_native_combat_signature(&rob->call, i == 0 ? QA_Q2CO_PAIN : QA_Q2CO_DEATH, qa_compat_q2_rerelease_abi, rob->storage);
      uint64_t handle = r->observe_entry(r->ctx, callback, rob, reaction_observed);
      ex->entry_handles[ex->entry_count++] = handle;
    }
  }
  ex->vectors = memory->allocate(memory->ctx, 36, true, 4, "Q2 damage vectors"); ex->has_vectors = true;
  const qa_vec3 values[3] = {request.direction, request.point, request.normal};
  for (int i = 0; i < 3; i++) {
    qa_guest_address base = qa_compat_q2_mem_offset(memory, ex->vectors, i * 12);
    qa_compat_q2_mem_write_f32(memory, base, values[i].x);
    qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, base, 4), values[i].y);
    qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, base, 8), values[i].z);
  }
  qa_compat_q2_runner *runner = frunner(f);
  ex->saved_processor = runner->capture_processor ? runner->capture_processor(runner->ctx) : NULL;
  double depth = qa_compat_q2_native_depth(runner);
  ex->mod_bytes[0] = qa_js_to_uint8(ex->native.id); /* Uint8Array store */
  ex->mod_bytes[1] = ex->native.friendly_fire ? 1 : 0;
  ex->mod_bytes[2] = ex->native.no_point_loss ? 1 : 0;
  qa_guest_call_value aggregate; memset(&aggregate, 0, sizeof aggregate);
  aggregate.kind = QA_GUEST_VALUE_AGGREGATE;
  aggregate.layout = qa_compat_q2_rerelease_mod_layout;
  aggregate.bytes.bytes = ex->mod_bytes; aggregate.bytes.length = 3;
  qa_guest_call_value semantic[10] = {
      qa_compat_q2_rerelease_guest_pointer(true, view.address),
      qa_compat_q2_rerelease_guest_pointer(true, ex->inflictor),
      qa_compat_q2_rerelease_guest_pointer(true, ex->attacker),
      qa_compat_q2_rerelease_guest_pointer(true, ex->vectors),
      qa_compat_q2_rerelease_guest_pointer(true, qa_compat_q2_mem_offset(memory, ex->vectors, 12)),
      qa_compat_q2_rerelease_guest_pointer(true, qa_compat_q2_mem_offset(memory, ex->vectors, 24)),
      qa_compat_q2_rerelease_guest_int(trunc(request.amount)),
      qa_compat_q2_rerelease_guest_int(trunc(request.knockback)),
      qa_compat_q2_rerelease_guest_int(ex->damage_flags),
      aggregate};
  /* A declared call has no argument limit: one lowered value per declared argument. */
  ex->lowered = calloc(profile->calls.damage.argument_count ? profile->calls.damage.argument_count : 1, sizeof *ex->lowered);
  if (!ex->lowered) qa_range_error("Q2 rerelease foreign actors out of memory");
  ex->lowered_count = qa_compat_q2_lower_native_combat_arguments(
      &profile->calls.damage, QA_Q2CO_DAMAGE, semantic, 10, memory, true, f->image, ex->source_args, ex->source_count, ex->lowered);
  qa_compat_q2_native_thrown thrown;
  if (!qa_compat_q2_native_try(damage_invoke_body, ex, &thrown)) {
    /* catch: only RemovedNativeDamage for this request ends here, with the processor put back. */
    if (thrown.token != frame) qa_compat_q2_native_rethrow(&thrown);
    qa_compat_q2_native_unwind(runner, depth);
    if (ex->saved_processor && runner->restore_processor) runner->restore_processor(runner->ctx, ex->saved_processor);
  }
  if (!ex->has_result) ex->result = (qa_source_damage_result){ex->applied_damage, "none"};
}
/* What the execution holds on the host side, released however it ends. */
static void exec_release(void *v) {
  exec_t *ex = v; qa_compat_q2_rerelease_foreign_actors *f = ex->f;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  if (ex->lowered && !ex->source_args)
    for (size_t i = 0; i < profile->calls.damage.argument_count; i++) {
      const qa_compat_q2_native_combat_argument *a = &profile->calls.damage.arguments[i];
      if (a->kind == QA_Q2CA_VALUE && a->layout.is_aggregate) free((void *)ex->lowered[i].bytes.bytes); /* decodeValue's copy */
    }
  free(ex->lowered); ex->lowered = NULL;
  qa_compat_q2_runner *runner = frunner(f);
  if (ex->saved_processor && runner->free_processor) runner->free_processor(runner->ctx, ex->saved_processor);
  ex->saved_processor = NULL;
  for (int i = 0; i < 2; i++) { free(ex->robs[i].storage); ex->robs[i].storage = NULL; }
  if (ex->client) { qa_compat_q2_rerelease_source_client_destroy(ex->client); ex->client = NULL; }
  if (ex->source) { qa_compat_q2_rerelease_source_edict_destroy(ex->source); ex->source = NULL; }
  if (ex->power) { binding_unhold(ex->power); ex->power = NULL; }
}
static qa_source_damage_result damage_execute(void *v, qa_source_damage_observer observer, qa_damage_request request) {
  exec_t outer; exec_t *ex = &outer; memset(ex, 0, sizeof *ex);
  { const exec_t *seed = v; ex->f = seed->f; ex->source_args = seed->source_args; ex->source_count = seed->source_count; }
  qa_compat_q2_rerelease_foreign_actors *f = ex->f;
  qa_q2_foundation_host *engine = fengine(f);
  qa_compat_q2_memory *memory = fmem(f);
  ex->observer = observer; ex->request = request;
  qa_cleanup held; qa_cleanup_push(&held, exec_release, ex);
  qa_actor_source_result target = qa_session_actor_registry_source_of(engine->actors, request.target);
  if (!target.has_value || !target.provider || !memory->module.id || strcmp(target.provider, memory->module.id) != 0)
    qa_raise("Error", 0, 0, "Native damage execution requires a native target");
  qa_q2mp_native_cause_profile cause_profile = {true, NULL, false};
  qa_compat_q2_native_damage_arguments_result cause = qa_compat_q2_native_damage_arguments(request, cause_profile);
  ex->native = cause.native; ex->damage_flags = cause.damage_flags;
  if (!ex->native.edition || strcmp(ex->native.edition, "rerelease") != 0)
    qa_raise("Error", 0, 0, "Damage cause has no native representation");
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  ex->view = table.at_slot(table.ctx, (double)target.slot);
  ex->source = xsource(f, ex->view);
  ex->monster = (qa_compat_q2_mem_read_u32(memory, qa_compat_q2_rerelease_source_edict_at(ex->source, "shared.svflags")) & 4) != 0;
  qa_compat_q2_rerelease_deferred_damage_track(f->deferred, ex->view);
  ex->attacker = qa_compat_q2_rerelease_q2_guest_host_address_for_actor(
      f->host, request.attack.attacker ? *request.attack.attacker : engine->world_actor(engine->ctx));
  ex->inflictor = qa_compat_q2_rerelease_q2_guest_host_address_for_actor(
      f->host, request.attack.inflictor ? *request.attack.inflictor : engine->world_actor(engine->ctx));
  frame_t *frame = xmalloc(sizeof *frame);
  memset(frame, 0, sizeof *frame);
  qa_cleanup frame_owned; qa_cleanup_push(&frame_owned, free, frame); /* until it is pushed */
  frame->request = qa_capture_request(request);
  if (request.attack.attacker) { frame->attacker = *request.attack.attacker; frame->request.attack.attacker = &frame->attacker; }
  if (request.attack.inflictor) { frame->inflictor = *request.attack.inflictor; frame->request.attack.inflictor = &frame->inflictor; }
  qa_owned_actor target_owner;
  if (qa_session_actor_registry_resolve_owned(engine->actors, request.target, &target_owner)) {
    ex->power = find_binding(f, target_owner);
    if (ex->power) ex->power->holds++; /* TS keeps the binding object for the whole damage */
  }
  ex->health = qa_compat_q2_rerelease_source_edict_get_health(ex->source);
  ex->velocity = qa_compat_q2_rerelease_source_edict_vector(ex->source, "velocity");
  ex->has_armor = read_armor(ex, &ex->armor);
  ex->applied_damage = 0;
  if (!ex->has_armor) qa_raise("Error", 0, 0, "Native target lacks a combat binding");
  if (f->frame_count == f->frame_cap) {
    f->frame_cap = f->frame_cap ? f->frame_cap * 2 : 8;
    f->frames = xrealloc(f->frames, f->frame_cap * sizeof *f->frames);
  }
  qa_cleanup_pop(&frame_owned);
  f->frames[f->frame_count++] = frame;
  ex->frame = frame;
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(damage_body, ex, &thrown);
  /* finally { stopWrites(); removeEntries.runAll(); unmap the vectors; frames.pop(); host.reconcile(); } */
  stop_writes(ex);
  qa_compat_q2_runner *runner = frunner(f);
  size_t entry_count = ex->entry_count; ex->entry_count = 0;
  for (size_t i = 0; i < entry_count; i++) runner->remove_entry(runner->ctx, ex->entry_handles[i]);
  if (ex->has_vectors) memory->unmap(memory->ctx, ex->vectors, 36);
  if (f->frame_count > 0) free(f->frames[--f->frame_count]);
  ex->frame = NULL;
  qa_compat_q2_rerelease_q2_guest_host_reconcile(f->host);
  qa_source_damage_result result = ex->result;
  qa_cleanup_pop(&held); exec_release(ex);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return result;
}
qa_damage_outcome qa_compat_q2_rerelease_foreign_actors_damage_native(qa_compat_q2_rerelease_foreign_actors *f, qa_damage_request input,
                                                                      const qa_guest_call_value *source_args, size_t source_count) {
  exec_t seed; memset(&seed, 0, sizeof seed);
  seed.f = f; seed.source_args = source_args; seed.source_count = source_count;
  return qa_gameplay_authority_run_source_damage(fengine(f)->combat, input, damage_execute, &seed);
}
/* --- lifecycle --- */
static void clear_release_body(void *v) { projection_call_t *c = v; release_projection(c->f, c->entry); }
void qa_compat_q2_rerelease_foreign_actors_clear(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_rerelease_deferred_damage_clear(f->deferred);
  /* for (const entry of [...this.#actors.values()]) try { this.#release(entry) } catch (error) { errors.push }:
   * every entry of the snapshot is released once, and the failures leave as one AggregateError. (A loop over the
   * live list never ended when an entry was already releasing.) */
  size_t count = f->actor_count, errors = 0;
  projection_t **copy = snapshot(f->actors, count);
  qa_caught_error caught;
  for (size_t i = 0; i < count; i++) { projection_call_t call = {f, copy[i]}; if (!qa_try(clear_release_body, &call, &caught)) errors++; }
  free(copy);
  if (errors > 0) qa_raise("AggregateError", 0, 0, "Foreign native projection cleanup failed");
}
const qa_compat_q2_rerelease_projection_save *qa_compat_q2_rerelease_foreign_actors_save_projections(
    qa_compat_q2_rerelease_foreign_actors *f, size_t *out_count) {
  size_t count = f->actor_count;
  projection_t **copy = snapshot(f->actors, count);
  qa_cleanup owned; qa_cleanup_push(&owned, free, copy);
  if (count > f->save_cap) { f->save_scratch = xrealloc(f->save_scratch, count * sizeof *f->save_scratch); f->save_cap = count; }
  size_t written = 0;
  for (size_t i = 0; i < count; i++) {
    qa_owned_actor found;
    if (qa_compat_q2_rerelease_foreign_actors_lookup(f, copy[i]->view, &found) != QA_COMPAT_Q2_RR_LOOKUP_ACTOR) continue;
    if (!qa_same_owned_actor(found, copy[i]->actor)) continue;
    f->save_scratch[written].slot = copy[i]->view.slot;
    f->save_scratch[written].actor.slot = (double)copy[i]->actor.id.slot;
    f->save_scratch[written].actor.generation = (double)copy[i]->actor.id.generation;
    written++;
  }
  qa_cleanup_pop(&owned); free(copy);
  *out_count = written;
  return f->save_scratch;
}
void qa_compat_q2_rerelease_foreign_actors_begin_restore(qa_compat_q2_rerelease_foreign_actors *f,
                                                         const qa_compat_q2_rerelease_projection_save *saved, size_t count, const char *domain) {
  if (f->restoring) qa_raise("Error", 0, 0, "Native projection restore is already active");
  f->restoring_saved = xmalloc(count * sizeof *f->restoring_saved);
  if (count) memcpy(f->restoring_saved, saved, count * sizeof *saved);
  f->restoring_count = count;
  snprintf(f->restoring_domain, sizeof f->restoring_domain, "%s", domain ? domain : "current");
  f->restoring = true;
}
void qa_compat_q2_rerelease_foreign_actors_end_restore(qa_compat_q2_rerelease_foreign_actors *f) {
  f->restoring = false;
  free(f->restoring_saved); f->restoring_saved = NULL; f->restoring_count = 0;
}
void qa_compat_q2_rerelease_foreign_actors_close(qa_compat_q2_rerelease_foreign_actors *f) {
  remove_armor_hooks(f);
  /* this.#armorBindings.clear() */
  while (f->binding_count > 0) { armor_binding_t *binding = f->bindings[--f->binding_count]; binding->listed = false; binding_drop(binding); }
  if (f->closed) return;
  f->closed = true;
  qa_compat_q2_runner *r = frunner(f);
  if (f->has_remove_entry) { f->has_remove_entry = false; r->remove_entry(r->ctx, f->remove_entry); }
  if (f->has_remove_continuation) { f->has_remove_continuation = false; r->remove_entry(r->ctx, f->remove_continuation); }
  qa_compat_q2_rerelease_deferred_damage_close(f->deferred);
  qa_compat_q2_rerelease_foreign_actors_clear(f);
}
static void continuation_observed(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  frame_t *frame = last_frame(f);
  if (!frame || frame->has_stack) return;
  frame->stack = rsp_of(f);
  frame->has_stack = true;
}
static bool deferred_current_request(void *v, qa_actor_id actor, qa_damage_request *out) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  frame_t *frame = last_frame(f);
  if (!frame || frame->entered || !qa_same_actor(frame->request.target, actor)) return false;
  frame->entered = true;
  *out = frame->request;
  return true;
}
static bool deferred_intercepted(void *v) { return intercept_native_damage(v); }
static qa_guest_call_result damage_invoke(void *v, const qa_guest_call_context *context,
                                          const qa_guest_call_value *args, size_t count) {
  (void)context;
  incoming_damage(v, args, count);
  qa_guest_call_result out; memset(&out, 0, sizeof out);
  out.kind = QA_GUEST_VALUE_VOID;
  return out;
}
static bool damage_accepts(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address address;
  if (!field_pointer(f, &profile->calls.damage, f->damage_signature, &address)) return false;
  /* [...this.#slots.values()].find(...): the first projection at that address, in slot insertion order. */
  projection_t *entry = NULL;
  for (size_t i = 0; i < f->slot_count && !entry; i++)
    if (f->slots[i]->view.address.byte_offset == address.byte_offset) entry = f->slots[i];
  if (!entry) return intercept_native_damage(f);
  qa_owned_actor found;
  if (qa_compat_q2_rerelease_foreign_actors_lookup(f, entry->view, &found) == QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED)
    return intercept_native_damage(f);
  /* !entry.releasing && isLive(entry.actor.id) && inuse: the edict is read only when the first two hold. */
  return !entry->releasing && qa_session_actor_registry_is_live(fengine(f)->actors, entry->actor.id) && inuse_of(f, entry->view);
}
/* A constructor that throws before its first observer is bound leaves nothing behind. */
static void create_abandon(void *v) { qa_compat_q2_rerelease_foreign_actors *f = v; free(f->sig_storage[0]); free(f->sig_storage[1]); free(f); }
qa_compat_q2_rerelease_foreign_actors *qa_compat_q2_rerelease_foreign_actors_create(
    qa_compat_q2_rerelease_q2_guest_host *host, qa_compat_q2_rerelease_native_entries entries,
    qa_compat_q2_rerelease_foreign_damage_services services) {
  qa_compat_q2_rerelease_foreign_actors *f = calloc(1, sizeof *f);
  if (!f) return NULL;
  qa_cleanup abandon; qa_cleanup_push(&abandon, create_abandon, f);
  f->host = host; f->entries = entries; f->services = services;
  qa_compat_q2_rerelease_guest_module *module = famod(f);
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
  const qa_compat_q2_native_combat_call *calls[2] = {&profile->calls.damage, &profile->calls.power_armor};
  for (int i = 0; i < 2; i++) {
    f->sig_storage[i] = calloc(calls[i]->argument_count ? calls[i]->argument_count : 1, sizeof *f->sig_storage[i]);
    if (!f->sig_storage[i]) qa_range_error("Q2 rerelease foreign actors out of memory");
  }
  f->damage_signature = qa_compat_q2_native_combat_signature(&profile->calls.damage, QA_Q2CO_DAMAGE,
                                                             qa_compat_q2_rerelease_abi, f->sig_storage[0]);
  f->power_signature = qa_compat_q2_native_combat_signature(&profile->calls.power_armor, QA_Q2CO_POWER_ARMOR,
                                                            qa_compat_q2_rerelease_abi, f->sig_storage[1]);
  f->image = qa_compat_q2_mem_offset(fmem(f), entries.damage, -(int64_t)profile->entries.damage);
  qa_compat_q2_runner *r = frunner(f);
  qa_cleanup_pop(&abandon);
  /* From the first observer on the object is reachable from the runner, as the TS closures keep it: TS has no
   * catch here, so a later member that throws leaves what was bound, and the object stays allocated behind it. */
  f->remove_continuation = r->observe_entry(r->ctx, entries.damage, f, continuation_observed); f->has_remove_continuation = true;
  f->deferred = qa_compat_q2_rerelease_deferred_damage_create(host, services, f, deferred_current_request, deferred_intercepted);
  if (!f->deferred) qa_range_error("Q2 rerelease foreign actors out of memory");
  f->damage_id = module_id(f, ":foreign-damage");
  f->remove_entry = r->bind_entry(r->ctx, entries.damage, f->damage_id, f->damage_signature, f, damage_invoke, f, damage_accepts); f->has_remove_entry = true;
  return f;
}
static void destroy_close_body(void *v) { qa_compat_q2_rerelease_foreign_actors_close(v); }
void qa_compat_q2_rerelease_foreign_actors_destroy(qa_compat_q2_rerelease_foreign_actors *f) {
  if (!f) return;
  /* C only: the owner has run TS close() and read its error; a failure here has no reader. */
  qa_caught_error caught; (void)qa_try(destroy_close_body, f, &caught);
  qa_compat_q2_rerelease_deferred_damage_destroy(f->deferred);
  while (f->owned_projections) { projection_t *next = f->owned_projections->next_owned; free(f->owned_projections); f->owned_projections = next; }
  for (size_t i = 0; i < f->frame_count; i++) free(f->frames[i]);
  for (size_t i = 0; i < f->binding_count; i++) free(f->bindings[i]);
  free(f->actors); free(f->slots); free(f->frames); free(f->publications); free(f->bindings);
  free(f->restoring_saved); free(f->save_scratch); free(f->sig_storage[0]); free(f->sig_storage[1]);
  free(f->damage_id); free(f->power_id);
  while (f->stages) { stage_ctx_t *next = f->stages->next; free(f->stages); f->stages = next; }
  free(f);
}
qa_compat_q2_rerelease_q2_guest_host *qa_compat_q2_rerelease_foreign_actors_get_host(const qa_compat_q2_rerelease_foreign_actors *f) {
  return f->host;
}
qa_compat_q2_rerelease_native_entries qa_compat_q2_rerelease_foreign_actors_get_entries(const qa_compat_q2_rerelease_foreign_actors *f) {
  return f->entries;
}
qa_compat_q2_rerelease_deferred_damage *qa_compat_q2_rerelease_foreign_actors_get_deferred(qa_compat_q2_rerelease_foreign_actors *f) {
  return f->deferred;
}
