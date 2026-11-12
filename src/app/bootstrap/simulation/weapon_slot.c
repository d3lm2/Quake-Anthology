/* Port of src/app/bootstrap/simulation/weapon-slot.ts. GPL-2.0-or-later.
 *
 * TS compares slot states and activations by object identity (`this.current !== state`, `activation?.state !==
 * state`, `this.activation !== activation`): every assignment of a state or activation takes a fresh serial here and
 * those checks compare serials. Bindings are a Map in insertion order (a rebound provider goes last); a binding record
 * stays allocated after it is unbound, as the TS object stays reachable from a caller's local, and is freed with the
 * slot. */
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct binding {
  int handle;
  qa_source_weapon_handoff handoff;
  bool (*current)(void *ctx); void *current_ctx;
  qa_source_weapon_presentation (*read)(void *ctx); void *read_ctx; bool has_read;
} binding;
typedef struct activation {
  uint64_t serial, state_serial;
  qa_app_sim_weapon_slot_state state;
  qa_source_weapon_request request; bool cancelling;
} activation;
struct qa_app_sim_weapon_slot {
  qa_source_weapon_handoff primary;
  qa_app_sim_equipment_weapon_handoff equipment; bool has_equipment;
  qa_source_weapon_handoff equipment_handoff;
  qa_app_sim_weapon_slot_state current; uint64_t current_serial; bool restore_pending;
  bool has_activation; activation activation; uint64_t serial;
  binding **bindings; size_t binding_count, binding_capacity; /* the live Map, insertion order */
  binding **records; size_t record_count, record_capacity;    /* every record made, freed with the slot */
  int next_handle;
  bool (*actor_current)(void *ctx); void *actor_current_ctx;
  qa_source_weapon_presentation *presentations; size_t presentation_count, presentation_capacity;
};
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void *grow(void *items, size_t *capacity, size_t need, size_t size) {
  if (need <= *capacity) return items;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  while (next < need) next *= 2;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}
static bool actor_current(qa_app_sim_weapon_slot *s) { return s->actor_current == NULL || s->actor_current(s->actor_current_ctx); }
static void set_current(qa_app_sim_weapon_slot *s, qa_app_sim_weapon_slot_state state) { s->current = state; s->current_serial = ++s->serial; }
/* this.bindings.get(provider) */
static binding *map_get(qa_app_sim_weapon_slot *s, qa_provider_id provider) {
  for (size_t i = 0; i < s->binding_count; i++) if (is(s->bindings[i]->handoff.provider, provider)) return s->bindings[i];
  return NULL;
}
/* binding(provider): the live binding, rechecked after its current() callback. */
static binding *find_binding(qa_app_sim_weapon_slot *s, qa_provider_id provider) {
  binding *b = map_get(s, provider);
  if (b == NULL) return NULL;
  bool live = b->current == NULL || b->current(b->current_ctx);
  return live && map_get(s, provider) == b ? b : NULL;
}
static binding *add_binding(qa_app_sim_weapon_slot *s, qa_source_weapon_handoff h, bool (*cur)(void *), void *cctx,
                            qa_source_weapon_presentation (*read)(void *), void *rctx, bool has_read) {
  s->records = grow(s->records, &s->record_capacity, s->record_count + 1, sizeof *s->records);
  s->bindings = grow(s->bindings, &s->binding_capacity, s->binding_count + 1, sizeof *s->bindings);
  binding *b = malloc(sizeof *b);
  if (b == NULL) qa_range_error("Out of memory");
  *b = (binding){s->next_handle++, h, cur, cctx, read, rctx, has_read};
  s->records[s->record_count++] = b;
  s->bindings[s->binding_count++] = b;
  return b;
}
static void map_delete(qa_app_sim_weapon_slot *s, binding *b) {
  for (size_t i = 0; i < s->binding_count; i++)
    if (s->bindings[i] == b) {
      memmove(&s->bindings[i], &s->bindings[i + 1], (s->binding_count - i - 1) * sizeof *s->bindings);
      s->binding_count--;
      return;
    }
}
static bool equip_accepts(void *ctx, qa_item_id item) { qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; return is(item, s->equipment.weapon.item); }
static bool equip_select(void *ctx, qa_item_id item) { return equip_accepts(ctx, item); }
static void equip_holster(void *ctx) { qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; if (s->equipment.holster) s->equipment.holster(s->equipment.ctx); }
static bool equip_holstered(void *ctx) { qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; return s->equipment.is_holstered && s->equipment.is_holstered(s->equipment.ctx); }
static bool equip_resume(void *ctx, qa_item_id item) { (void)item; qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; if (s->equipment.resume) s->equipment.resume(s->equipment.ctx); return true; }

static qa_app_sim_weapon_slot_state restore_state(qa_app_sim_weapon_slot *s, qa_app_sim_weapon_slot_restore_state state) {
  if (is(state.kind, "active") || is(state.kind, "switching") || is(state.kind, "activating")) return state;
  if (is(state.kind, "primary")) { qa_app_sim_weapon_slot_state o = state; o.kind = "active"; o.provider = s->primary.provider; return o; }
  if (is(state.kind, "equipment")) {
    if (!s->has_equipment) qa_raise("Error", 0, 0, "Saved equipment weapon has no owner");
    qa_app_sim_weapon_slot_state o = state; o.kind = "active"; o.provider = s->equipment.weapon.provider; return o;
  }
  if (is(state.kind, "holstering-primary")) { qa_app_sim_weapon_slot_state o = state; o.kind = "switching"; o.from = s->primary.provider; return o; }
  if (!s->has_equipment) qa_raise("Error", 0, 0, "Saved equipment transition has no owner");
  qa_app_sim_weapon_slot_state o = state; o.kind = "switching"; o.from = s->equipment.weapon.provider; return o;
}
static void slot_free(void *ctx) {
  qa_app_sim_weapon_slot *s = ctx;
  for (size_t i = 0; i < s->record_count; i++) free(s->records[i]);
  free(s->records); free(s->bindings); free(s->presentations); free(s);
}
qa_app_sim_weapon_slot *qa_app_sim_weapon_slot_create(qa_source_weapon_handoff primary,
    const qa_app_sim_equipment_weapon_handoff *equipment,
    const qa_app_sim_weapon_slot_restore_state *restored,
    bool (*actor_current_fn)(void *ctx), void *actor_current_ctx) {
  qa_app_sim_weapon_slot *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  qa_cleanup node; qa_cleanup_push(&node, slot_free, s); /* a refused restore state frees the slot */
  s->primary = primary;
  s->actor_current = actor_current_fn; s->actor_current_ctx = actor_current_ctx;
  add_binding(s, primary, actor_current_fn, actor_current_ctx, NULL, NULL, false);
  if (equipment != NULL) {
    if (is(equipment->weapon.provider, primary.provider)) qa_raise("Error", 0, 0, "Equipment and primary weapon owners overlap");
    s->equipment = *equipment; s->has_equipment = true;
    s->equipment_handoff = (qa_source_weapon_handoff){0};
    s->equipment_handoff.ctx = s;
    s->equipment_handoff.provider = equipment->weapon.provider;
    s->equipment_handoff.accepts = equip_accepts; s->equipment_handoff.select = equip_select;
    s->equipment_handoff.holster = equip_holster; s->equipment_handoff.is_holstered = equip_holstered;
    s->equipment_handoff.is_source_input = false; s->equipment_handoff.resume_immediate = equip_resume;
    add_binding(s, s->equipment_handoff, actor_current_fn, actor_current_ctx, NULL, NULL, false);
  }
  qa_app_sim_weapon_slot_restore_state initial;
  memset(&initial, 0, sizeof initial); initial.kind = "active"; initial.provider = primary.provider;
  set_current(s, restore_state(s, restored != NULL ? *restored : initial));
  s->restore_pending = restored != NULL;
  qa_cleanup_pop(&node);
  return s;
}
void qa_app_sim_weapon_slot_destroy(qa_app_sim_weapon_slot *slot) { if (slot != NULL) slot_free(slot); }
qa_app_sim_weapon_slot_state qa_app_sim_weapon_slot_snapshot(const qa_app_sim_weapon_slot *slot) { return slot->current; }
void qa_app_sim_weapon_slot_validate_restore(qa_app_sim_weapon_slot *slot) {
  if (!slot->restore_pending) return;
  qa_app_sim_weapon_slot_state state = slot->current; uint64_t state_serial = slot->current_serial;
  binding *outgoing = find_binding(slot, is(state.kind, "active") ? state.provider : state.from);
  if (!actor_current(slot) || outgoing == NULL) qa_raise("Error", 0, 0, "Saved weapon source is not bound");
  if (outgoing->has_read) {
    qa_source_weapon_presentation p = outgoing->read(outgoing->read_ctx);
    bool declared = p.active == NULL;
    for (size_t i = 0; !declared && i < p.item_count; i++)
      if (p.items[i].is_weapon && is(p.items[i].item, p.active)) declared = true;
    if (!is(p.source.provider, outgoing->handoff.provider) || !declared || find_binding(slot, outgoing->handoff.provider) != outgoing)
      qa_raise("Error", 0, 0, "Saved active weapon is not declared by its source owner");
  }
  if (!is(state.kind, "active")) {
    binding *incoming = find_binding(slot, state.next.provider);
    if (incoming == NULL || !incoming->handoff.accepts(incoming->handoff.ctx, state.next.item) || find_binding(slot, state.next.provider) != incoming)
      qa_raise("Error", 0, 0, "Saved pending weapon is not admitted to its source owner");
    if (is(state.kind, "activating")) {
      if (!incoming->handoff.is_source_input) qa_raise("Error", 0, 0, "Saved activation requires its original input owner");
      qa_source_weapon_request request = incoming->handoff.restore_request(incoming->handoff.ctx, state.request, state.next.item);
      if (request.id != state.request || slot->current_serial != state_serial || find_binding(slot, state.next.provider) != incoming)
        qa_raise("Error", 0, 0, "Saved weapon activation changed during restoration");
      slot->has_activation = true;
      slot->activation = (activation){++slot->serial, state_serial, state, request, false};
    }
  }
  slot->restore_pending = false;
}
bool qa_app_sim_weapon_slot_selected(qa_app_sim_weapon_slot *slot, qa_provider_id provider) {
  return actor_current(slot) && is(slot->current.kind, "active") && is(slot->current.provider, provider) && find_binding(slot, provider) != NULL;
}
bool qa_app_sim_weapon_slot_presented(qa_app_sim_weapon_slot *slot, qa_provider_id provider) {
  if (!actor_current(slot)) return false;
  qa_provider_id shown = is(slot->current.kind, "active") ? slot->current.provider : slot->current.from;
  return is(shown, provider) && find_binding(slot, provider) != NULL;
}
bool qa_app_sim_weapon_slot_primary_selected(qa_app_sim_weapon_slot *slot) { return qa_app_sim_weapon_slot_selected(slot, slot->primary.provider); }
bool qa_app_sim_weapon_slot_equipment_selected(qa_app_sim_weapon_slot *slot) {
  return slot->has_equipment && qa_app_sim_weapon_slot_selected(slot, slot->equipment.weapon.provider);
}
typedef struct cancel_call { qa_source_weapon_request request; } cancel_call;
static void cancel_request(void *ctx) { cancel_call *c = ctx; if (c->request.cancel) c->request.cancel(c->request.ctx); }
/* cancelActivation(state): the request is cancelled once; finally the activation is dropped if it is still this one. */
static void cancel_activation(qa_app_sim_weapon_slot *s, const qa_app_sim_weapon_slot_state *state, uint64_t state_serial) {
  if (!is(state->kind, "activating") || !s->has_activation || s->activation.state_serial != state_serial || s->activation.cancelling) return;
  uint64_t serial = s->activation.serial;
  s->activation.cancelling = true;
  cancel_call call = {s->activation.request};
  qa_caught_error caught;
  bool ok = qa_try(cancel_request, &call, &caught);
  if (s->has_activation && s->activation.serial == serial) s->has_activation = false;
  if (!ok) qa_rethrow(&caught);
}
static void resume_slot(qa_app_sim_weapon_slot *s, qa_provider_id provider, qa_item_id item, qa_provider_id out_provider, binding *out_binding);
static void fallback_slot(qa_app_sim_weapon_slot *s) { resume_slot(s, s->primary.provider, NULL, NULL, NULL); }
static void resume_slot(qa_app_sim_weapon_slot *s, qa_provider_id provider, qa_item_id item, qa_provider_id out_provider, binding *out_binding) {
  binding *b = find_binding(s, provider);
  if (b == NULL) qa_raise("Error", 0, 0, "Weapon selection has no live source owner");
  qa_app_sim_weapon_slot_state activated; memset(&activated, 0, sizeof activated);
  activated.kind = "active"; activated.provider = provider;
  set_current(s, activated);
  uint64_t activated_serial = s->current_serial;
  bool accepted;
  if (!b->handoff.is_source_input) {
    accepted = b->handoff.resume_immediate(b->handoff.ctx, item);
  } else {
    qa_source_weapon_request request = b->handoff.resume_request(b->handoff.ctx, item);
    if (!actor_current(s) || s->current_serial != activated_serial || find_binding(s, provider) != b) { if (request.cancel) request.cancel(request.ctx); return; }
    const char *status = request.status(request.ctx);
    if (!actor_current(s) || s->current_serial != activated_serial || find_binding(s, provider) != b) { if (request.cancel) request.cancel(request.ctx); return; }
    if (is(status, "pending")) {
      if (item == NULL || out_binding == NULL || find_binding(s, out_provider) != out_binding) {
        if (request.cancel) request.cancel(request.ctx);
        qa_raise("Error", 0, 0, "Deferred source resumption has no unchanged outgoing owner");
      }
      qa_app_sim_weapon_slot_state st; memset(&st, 0, sizeof st);
      st.kind = "activating"; st.from = out_provider; st.next = (qa_weapon_reference){provider, item}; st.request = request.id;
      set_current(s, st);
      s->has_activation = true;
      s->activation = (activation){++s->serial, s->current_serial, st, request, false};
      return;
    }
    accepted = is(status, "accepted");
  }
  if (accepted || !actor_current(s) || s->current_serial != activated_serial || find_binding(s, provider) != b) return;
  if (out_binding != NULL && find_binding(s, out_provider) == out_binding) { resume_slot(s, out_provider, NULL, NULL, NULL); return; }
  qa_raise("Error", 0, 0, "Original source refused weapon resumption");
}
int qa_app_sim_weapon_slot_bind(qa_app_sim_weapon_slot *slot, qa_source_weapon_binding source) {
  if (!actor_current(slot) || (source.current && !source.current(source.ctx)) || map_get(slot, source.handoff.provider) != NULL)
    qa_raise("Error", 0, 0, "Source weapon owner is unavailable or already bound");
  return add_binding(slot, source.handoff, source.current, source.ctx, source.read, source.ctx, source.read != NULL)->handle;
}
void qa_app_sim_weapon_slot_unbind(qa_app_sim_weapon_slot *slot, int handle) {
  binding *b = NULL;
  for (size_t i = 0; i < slot->binding_count; i++) if (slot->bindings[i]->handle == handle) b = slot->bindings[i];
  if (b == NULL) return; /* this.bindings.get(provider) !== binding */
  qa_provider_id provider = b->handoff.provider;
  map_delete(slot, b);
  if (!actor_current(slot) || slot->restore_pending) return;
  qa_app_sim_weapon_slot_state state = slot->current; uint64_t state_serial = slot->current_serial;
  if ((is(state.kind, "active") && is(state.provider, provider)) || (!is(state.kind, "active") && is(state.from, provider))) {
    cancel_activation(slot, &state, state_serial); fallback_slot(slot);
  } else if (!is(state.kind, "active") && is(state.next.provider, provider)) {
    cancel_activation(slot, &state, state_serial);
    if (slot->current_serial == state_serial) resume_slot(slot, state.from, NULL, NULL, NULL);
  }
}
const qa_source_weapon_presentation *qa_app_sim_weapon_slot_presentations(qa_app_sim_weapon_slot *slot, size_t *out_count) {
  slot->presentation_count = 0;
  if (actor_current(slot)) {
    /* for (const [provider, entry] of this.bindings): a Map iterator visits, in insertion order, the entries still
     * present when it reaches them, including ones added during the loop. Handles grow with insertion and the live
     * array keeps insertion order, so the next entry is the first live one with a larger handle. */
    int last = -1;
    for (;;) {
      binding *e = NULL;
      for (size_t i = 0; i < slot->binding_count; i++) if (slot->bindings[i]->handle > last) { e = slot->bindings[i]; break; }
      if (e == NULL) break;
      last = e->handle;
      if (!e->has_read || find_binding(slot, e->handoff.provider) != e) continue;
      qa_source_weapon_presentation p = e->read(e->read_ctx);
      if (!is(p.source.provider, e->handoff.provider)) qa_raise("Error", 0, 0, "Source weapon presentation belongs to another owner");
      if (find_binding(slot, e->handoff.provider) == e) {
        slot->presentations = grow(slot->presentations, &slot->presentation_capacity, slot->presentation_count + 1, sizeof *slot->presentations);
        slot->presentations[slot->presentation_count++] = p;
      }
    }
  }
  if (out_count) *out_count = slot->presentation_count;
  return slot->presentations;
}
static void reconcile_activation(qa_app_sim_weapon_slot *s, const qa_app_sim_weapon_slot_state *state, uint64_t state_serial) {
  /* const activation = this.activation, outgoing = this.binding(state.from), incoming = this.binding(state.next.provider);
   * the binding reads run before the identity check. */
  bool had = s->has_activation;
  activation captured = had ? s->activation : (activation){0};
  binding *outgoing = find_binding(s, state->from);
  binding *incoming = find_binding(s, state->next.provider);
  if (!had || captured.state_serial != state_serial) qa_raise("Error", 0, 0, "Weapon activation lost its source request");
  /* activation.cancelling: the live record while it is still this activation */
  bool cancelling = s->has_activation && s->activation.serial == captured.serial ? s->activation.cancelling : captured.cancelling;
  if (cancelling) return;
  if (outgoing == NULL) { cancel_activation(s, state, state_serial); if (s->current_serial == state_serial) fallback_slot(s); return; }
  if (incoming == NULL) { cancel_activation(s, state, state_serial); if (s->current_serial == state_serial) resume_slot(s, state->from, NULL, NULL, NULL); return; }
  const char *status = captured.request.status(captured.request.ctx);
  if (!actor_current(s) || s->current_serial != state_serial || !s->has_activation || s->activation.serial != captured.serial
      || find_binding(s, state->from) != outgoing || find_binding(s, state->next.provider) != incoming) return;
  if (is(status, "pending")) return;
  s->has_activation = false;
  if (is(status, "accepted")) {
    qa_app_sim_weapon_slot_state o; memset(&o, 0, sizeof o); o.kind = "active"; o.provider = state->next.provider;
    set_current(s, o);
  } else resume_slot(s, state->from, NULL, NULL, NULL);
}
bool qa_app_sim_weapon_slot_request(qa_app_sim_weapon_slot *slot, qa_weapon_reference weapon) {
  qa_app_sim_weapon_slot_validate_restore(slot);
  if (!actor_current(slot)) return false;
  binding *destination = find_binding(slot, weapon.provider);
  if (destination == NULL || !destination->handoff.accepts(destination->handoff.ctx, weapon.item) || find_binding(slot, weapon.provider) != destination) return false;
  if (is(slot->current.kind, "activating")) {
    qa_app_sim_weapon_slot_state current = slot->current;
    reconcile_activation(slot, &current, slot->current_serial);
  }
  qa_app_sim_weapon_slot_state state = slot->current; uint64_t state_serial = slot->current_serial;
  if (is(state.kind, "activating")) {
    if (is(state.next.provider, weapon.provider) && is(state.next.item, weapon.item)) return true;
    cancel_activation(slot, &state, state_serial);
    if (!actor_current(slot) || slot->current_serial != state_serial || find_binding(slot, weapon.provider) != destination) return false;
    if (is(weapon.provider, state.from)) resume_slot(slot, state.from, weapon.item, NULL, NULL);
    else { qa_app_sim_weapon_slot_state o; memset(&o, 0, sizeof o); o.kind = "switching"; o.from = state.from; o.next = weapon; set_current(slot, o); }
    return true;
  }
  if (is(state.kind, "switching")) { qa_app_sim_weapon_slot_state o = state; o.next = weapon; set_current(slot, o); return true; }
  if (is(state.provider, weapon.provider)) {
    bool accepted = destination->handoff.select(destination->handoff.ctx, weapon.item);
    return accepted && actor_current(slot) && find_binding(slot, weapon.provider) == destination;
  }
  binding *outgoing = find_binding(slot, state.provider);
  if (outgoing == NULL) { fallback_slot(slot); return false; }
  qa_app_sim_weapon_slot_state o; memset(&o, 0, sizeof o); o.kind = "switching"; o.from = state.provider; o.next = weapon;
  set_current(slot, o);
  outgoing->handoff.holster(outgoing->handoff.ctx);
  return true;
}
void qa_app_sim_weapon_slot_reconcile(qa_app_sim_weapon_slot *slot) {
  qa_app_sim_weapon_slot_validate_restore(slot);
  if (!actor_current(slot)) return;
  qa_app_sim_weapon_slot_state state = slot->current; uint64_t state_serial = slot->current_serial;
  if (is(state.kind, "activating")) { reconcile_activation(slot, &state, state_serial); return; }
  if (is(state.kind, "active")) { if (find_binding(slot, state.provider) == NULL) fallback_slot(slot); return; }
  binding *outgoing = find_binding(slot, state.from), *incoming = find_binding(slot, state.next.provider);
  if (outgoing == NULL) { fallback_slot(slot); return; }
  bool accepted = incoming != NULL && incoming->handoff.accepts(incoming->handoff.ctx, state.next.item);
  if (!actor_current(slot) || slot->current_serial != state_serial) return;
  if (!accepted || incoming == NULL || find_binding(slot, state.next.provider) != incoming) { resume_slot(slot, state.from, NULL, NULL, NULL); return; }
  if (outgoing->handoff.is_holstered(outgoing->handoff.ctx) && slot->current_serial == state_serial
      && find_binding(slot, state.from) == outgoing && find_binding(slot, state.next.provider) == incoming)
    resume_slot(slot, state.next.provider, state.next.item, state.from, outgoing);
}
