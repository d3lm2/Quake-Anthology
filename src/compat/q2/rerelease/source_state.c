/* Port of src/compat/q2/rerelease/source-state.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/source_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/combat_binding.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/foundation/js.h"

/* The binding contexts an object handed out, released with it. TS closures have no count limit. */
typedef struct binding_list { void **items; size_t count, capacity; } binding_list;
static void *binding_new(binding_list *list, size_t size) {
  if (list->count == list->capacity) {
    size_t capacity = list->capacity ? list->capacity * 2 : 4; void **grown = realloc(list->items, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    list->items = grown; list->capacity = capacity;
  }
  void *ctx = calloc(1, size); if (!ctx) qa_range_error("Out of memory");
  list->items[list->count++] = ctx; return ctx;
}
static void binding_list_free(binding_list *list) { for (size_t i = 0; i < list->count; i++) free(list->items[i]); free(list->items); list->items = NULL; list->count = list->capacity = 0; }
/* TS try { body } finally { cleanup }. */
static void run_finally(void (*body)(void *), void *body_ctx, void (*cleanup)(void *), void *cleanup_ctx) {
  qa_compat_q2_native_thrown thrown; bool completed = qa_compat_q2_native_try(body, body_ctx, &thrown);
  cleanup(cleanup_ctx);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
struct qa_compat_q2_rerelease_source_client {
  qa_guest_address address; qa_compat_q2_rerelease_guest_module *module;
  qa_compat_q2_rerelease_client_profile profile;
  binding_list bindings; binding_list owned; /* owned: item lists and read scratch of the inventory bindings */
};
qa_compat_q2_rerelease_source_client *qa_compat_q2_rerelease_source_client_create(
    qa_guest_address address, qa_compat_q2_rerelease_guest_module *module, const qa_compat_q2_rerelease_client_profile *profile) {
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  if (profile->authority_is_artifact && (!profile->digest || !memory->module.digest || strcmp(profile->digest, memory->module.digest) != 0))
    qa_raise("Error", 0, 0, "Rerelease private client profile belongs to another artifact");
  memory->check(memory->ctx, address, profile->layout.byte_length, "read");
  qa_compat_q2_rerelease_source_client *c = calloc(1, sizeof *c); if (!c) return NULL;
  c->address = address; c->module = module; c->profile = *profile; return c;
}
void qa_compat_q2_rerelease_source_client_destroy(qa_compat_q2_rerelease_source_client *c) {
  if (!c) return;
  binding_list_free(&c->bindings); binding_list_free(&c->owned); free(c);
}
qa_guest_address qa_compat_q2_rerelease_source_client_get_address(const qa_compat_q2_rerelease_source_client *c) { return c->address; }
qa_guest_address qa_compat_q2_rerelease_source_client_at(const qa_compat_q2_rerelease_source_client *c, const char *name) {
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(c->module);
  return qa_compat_q2_mem_offset(memory, c->address, (int64_t)qa_compat_q2_rerelease_field_offset(&c->profile.layout, name));
}
int64_t qa_compat_q2_rerelease_source_client_invincible_until_milliseconds(const qa_compat_q2_rerelease_source_client *c) {
  const qa_compat_q2_memory * m = qa_compat_q2_rerelease_guest_module_get_memory(c->module);
  qa_guest_address a = qa_compat_q2_rerelease_source_client_at(c, "invincible_time");
  return qa_compat_q2_mem_read_i64(m, a);
}
static qa_guest_address item_at(qa_compat_q2_rerelease_source_client *c, double index) {
  if (index != trunc(index) || fabs(index) > 9007199254740991.0 || index < 0 || index >= c->profile.inventory_count)
    qa_range_error("Rerelease inventory index is outside the selected IT_TOTAL");
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(c->module);
  return qa_compat_q2_mem_offset(memory, qa_compat_q2_rerelease_source_client_at(c, "pers.inventory"), (int64_t)(index * 4));
}
static qa_guest_address ammo_at(qa_compat_q2_rerelease_source_client *c, double index) {
  if (index != trunc(index) || fabs(index) > 9007199254740991.0 || index < 0 || index >= c->profile.ammo_count)
    qa_range_error("Rerelease capacity index is outside the selected AMMO_MAX");
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(c->module);
  return qa_compat_q2_mem_offset(memory, qa_compat_q2_rerelease_source_client_at(c, "pers.max_ammo"), (int64_t)(index * 2));
}
typedef struct inv_ctx { qa_compat_q2_rerelease_source_client *c; qa_compat_q2_rerelease_inventory_item *items; size_t count; qa_inventory_entry *scratch; } inv_ctx_t;
static bool inv_mutable(void *vctx, qa_item_id item) {
  inv_ctx_t *x = vctx;
  for (size_t i = 0; i < x->count; i++) if (strcmp(x->items[i].item, item) == 0 && x->items[i].capacity_is_ammo) return true;
  return false;
}
static const qa_inventory_entry *inv_read(void *vctx, size_t *out_count) {
  inv_ctx_t *x = vctx; qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(x->c->module);
  /* Each binding reads into its own list (TS: a new array per read; one list per client was shared by all its
   * bindings). */
  for (size_t i = 0; i < x->count; i++) {
    memset(&x->scratch[i], 0, sizeof x->scratch[i]);
    x->scratch[i].item = x->items[i].item;
    x->scratch[i].count = qa_compat_q2_mem_read_i32(memory, item_at(x->c, x->items[i].source_index));
    x->scratch[i].has_count_policy = true; x->scratch[i].count_policy = (qa_inventory_count_policy){"source-counter", "int32"};
    x->scratch[i].capacity = x->items[i].capacity_is_ammo ? qa_compat_q2_mem_read_i16(memory, ammo_at(x->c, x->items[i].capacity_source_index)) : x->items[i].capacity_count;
  }
  if (out_count) *out_count = x->count;
  return x->scratch;
}
static void inv_write(void *vctx, qa_inventory_entry entry) {
  inv_ctx_t *x = vctx; qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(x->c->module);
  const qa_compat_q2_rerelease_inventory_item *item = NULL;
  for (size_t i = 0; i < x->count && !item; i++) if (strcmp(x->items[i].item, entry.item) == 0) item = &x->items[i]; /* find: the first */
  if (!item) qa_raise("Error", 0, 0, "Item has no native inventory binding");
  if (entry.count != trunc(entry.count) || fabs(entry.count) > 9007199254740991.0 || entry.count < -2147483648.0 || entry.count > 2147483647.0)
    qa_range_error("Native inventory count exceeds int32");
  if (!item->capacity_is_ammo) { if (entry.capacity != item->capacity_count) qa_raise("Error", 0, 0, "Native item has a fixed source capacity"); }
  else {
    if (entry.capacity != trunc(entry.capacity) || fabs(entry.capacity) > 9007199254740991.0 || entry.capacity < 0 || entry.capacity > 32767)
      qa_range_error("Native ammo capacity exceeds int16");
    qa_compat_q2_mem_write_i16(memory, ammo_at(x->c, item->capacity_source_index), qa_js_to_int32(entry.capacity));
  }
  qa_compat_q2_mem_write_i32(memory, item_at(x->c, item->source_index), qa_js_to_int32(entry.count));
}
qa_inventory_state_binding qa_compat_q2_rerelease_source_client_inventory(qa_compat_q2_rerelease_source_client *c,
                                                                          const qa_compat_q2_rerelease_inventory_item *items, size_t count) {
  for (size_t i = 0; i < count; i++) {
    (void)item_at(c, items[i].source_index);
    for (size_t j = 0; j < i; j++) if (strcmp(items[i].item, items[j].item) == 0 || items[i].source_index == items[j].source_index)
      qa_raise("Error", 0, 0, "Duplicate semantic or source inventory item");
    if (items[i].capacity_is_ammo) (void)ammo_at(c, items[i].capacity_source_index);
  }
  inv_ctx_t *x = binding_new(&c->bindings, sizeof *x); x->c = c; x->count = count;
  x->items = binding_new(&c->owned, (count ? count : 1) * sizeof *x->items); if (count) memcpy(x->items, items, count * sizeof *x->items);
  x->scratch = binding_new(&c->owned, (count ? count : 1) * sizeof *x->scratch);
  qa_inventory_state_binding b; memset(&b, 0, sizeof b);
  b.ctx = x; b.mutable_capacity = inv_mutable; b.read = inv_read; b.write = inv_write;
  return b;
}
static double cells_read(void *ctx) {
  qa_compat_q2_rerelease_source_client *c = ctx;
  double index = qa_compat_q2_rerelease_guest_module_require_world_profile(c->module)->armor.cells_index;
  const qa_compat_q2_memory * m = qa_compat_q2_rerelease_guest_module_get_memory(c->module);
  qa_guest_address a = item_at(c, index);
  return qa_compat_q2_mem_read_i32(m, a);
}
static void cells_write(void *ctx, double count) {
  qa_compat_q2_rerelease_source_client *c = ctx;
  if (count != trunc(count) || fabs(count) > 9007199254740991.0 || count < -2147483648.0 || count > 2147483647.0)
    qa_range_error("Native power armor cell count exceeds int32");
  double index = qa_compat_q2_rerelease_guest_module_require_world_profile(c->module)->armor.cells_index;
  const qa_compat_q2_memory * m = qa_compat_q2_rerelease_guest_module_get_memory(c->module);
  qa_guest_address a = item_at(c, index);
  qa_compat_q2_mem_write_i32(m, a, qa_js_to_int32(count));
}
qa_power_armor_cell_binding qa_compat_q2_rerelease_source_client_power_armor_cells(qa_compat_q2_rerelease_source_client *c) {
  (void)qa_compat_q2_rerelease_guest_module_require_world_profile(c->module); /* TS reads the profile when the binding is made */
  return (qa_power_armor_cell_binding){c, cells_read, cells_write};
}

struct qa_compat_q2_rerelease_source_edict {
  qa_raw_entity_view raw; qa_compat_q2_rerelease_guest_module *module;
  binding_list bindings;
};
qa_compat_q2_rerelease_source_edict *qa_compat_q2_rerelease_source_edict_create(qa_raw_entity_view raw, qa_compat_q2_rerelease_guest_module *module) {
  if (raw.stride_bytes < qa_compat_q2_rerelease_guest_module_require_world_profile(module)->edict.byte_length)
    qa_raise("Error", 0, 0, "Rerelease edict does not contain the selected source-private prefix");
  qa_compat_q2_rerelease_source_edict *e = calloc(1, sizeof *e); if (!e) return NULL;
  e->raw = raw; e->module = module; return e;
}
void qa_compat_q2_rerelease_source_edict_destroy(qa_compat_q2_rerelease_source_edict *e) {
  if (!e) return;
  binding_list_free(&e->bindings); free(e);
}
qa_raw_entity_view qa_compat_q2_rerelease_source_edict_get_raw(const qa_compat_q2_rerelease_source_edict *e) { return e->raw; }
static qa_compat_q2_memory *mem_of(const qa_compat_q2_rerelease_source_edict *e) { return qa_compat_q2_rerelease_guest_module_get_memory(e->module); }
qa_guest_address qa_compat_q2_rerelease_source_edict_at(const qa_compat_q2_rerelease_source_edict *e, const char *name) {
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(e->module);
  const qa_compat_q2_memory * m = mem_of(e);
  qa_guest_address a = e->raw.address;
  int64_t d = (int64_t)qa_compat_q2_rerelease_field_offset(&profile->edict, name);
  return qa_compat_q2_mem_offset(m, a, d);
}
double qa_compat_q2_rerelease_source_edict_generation(const qa_compat_q2_rerelease_source_edict *e) { const qa_compat_q2_memory * m = mem_of(e); qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(e, "spawn_count"); return qa_compat_q2_mem_read_i32(m, a); }
double qa_compat_q2_rerelease_source_edict_get_health(const qa_compat_q2_rerelease_source_edict *e) { const qa_compat_q2_memory * m = mem_of(e); qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(e, "health"); return qa_compat_q2_mem_read_i32(m, a); }
void qa_compat_q2_rerelease_source_edict_set_health(qa_compat_q2_rerelease_source_edict *e, double value) { const qa_compat_q2_memory * m = mem_of(e); qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(e, "health"); qa_compat_q2_mem_write_i32(m, a, qa_js_to_int32(value)); }
bool qa_compat_q2_rerelease_source_edict_damageable(const qa_compat_q2_rerelease_source_edict *e) { const qa_compat_q2_memory * m = mem_of(e); qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(e, "takedamage"); return qa_compat_q2_mem_read_u8(m, a) != 0; }
bool qa_compat_q2_rerelease_source_edict_client(const qa_compat_q2_rerelease_source_edict *e, qa_guest_address *out) {
  const qa_compat_q2_memory * m = mem_of(e);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(e, "shared.client");
  return qa_compat_q2_mem_read_pointer(m, a, out);
}
typedef struct combat_ctx { qa_compat_q2_rerelease_source_edict *e; qa_compat_q2_rerelease_source_protection protection; } combat_ctx_t;
static qa_combat_state combat_read(void *vctx) {
  combat_ctx_t *x = vctx;
  qa_combat_state s; memset(&s, 0, sizeof s);
  s.health = qa_compat_q2_rerelease_source_edict_get_health(x->e);
  const qa_compat_q2_memory * m = mem_of(x->e);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(x->e, "mass");
  s.mass = qa_compat_q2_mem_read_i32(m, a);
  s.can_take_damage = qa_compat_q2_rerelease_source_edict_damageable(x->e);
  s.armor = x->protection.armor(x->protection.ctx);
  qa_compat_q2_rerelease_protection_traits traits = x->protection.traits(x->protection.ctx);
  s.invulnerable = traits.invulnerable; s.team = traits.team; s.has_no_knockback = true; s.no_knockback = traits.no_knockback;
  return s;
}
static void combat_write_health(void *vctx, double health) { qa_compat_q2_rerelease_source_edict_set_health(((combat_ctx_t *)vctx)->e, health); }
static void combat_write_armor(void *vctx, qa_armor_state value) { combat_ctx_t *x = vctx; x->protection.write_armor(x->protection.ctx, value); }
qa_combat_state_binding qa_compat_q2_rerelease_source_edict_combat(qa_compat_q2_rerelease_source_edict *e, qa_compat_q2_rerelease_source_protection protection) {
  combat_ctx_t *x = binding_new(&e->bindings, sizeof *x); x->e = e; x->protection = protection;
  qa_combat_state_binding b; memset(&b, 0, sizeof b);
  b.ctx = x; b.read = combat_read; b.write_health = combat_write_health; b.write_armor = combat_write_armor;
  return b;
}
qa_vec3 qa_compat_q2_rerelease_source_edict_vector(const qa_compat_q2_rerelease_source_edict *e, const char *name) {
  const qa_compat_q2_memory * m = mem_of(e);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(e, name);
  return qa_compat_q2_mem_read_f32_vector(m, a);
}
void qa_compat_q2_rerelease_source_edict_write_vector(const qa_compat_q2_rerelease_source_edict *e, const char *name, qa_vec3 value) {
  qa_guest_address at = qa_compat_q2_rerelease_source_edict_at(e, name);
  qa_compat_q2_mem_write_f32(mem_of(e), at, value.x);
  qa_compat_q2_mem_write_f32(mem_of(e), qa_compat_q2_mem_offset(mem_of(e), at, 4), value.y);
  qa_compat_q2_mem_write_f32(mem_of(e), qa_compat_q2_mem_offset(mem_of(e), at, 8), value.z);
}
typedef struct body_ctx { qa_compat_q2_rerelease_source_edict *e; qa_compat_q2_rerelease_actor_addresses addresses; qa_actor_id ground; } body_ctx_t;
static qa_body_state body_read(void *vctx) {
  body_ctx_t *x = vctx;
  qa_body_state s; memset(&s, 0, sizeof s);
  qa_guest_address ground; const qa_compat_q2_memory * m = mem_of(x->e); qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(x->e, "groundentity"); bool has = qa_compat_q2_mem_read_pointer(m, a, &ground);
  s.origin = qa_compat_q2_rerelease_source_edict_vector(x->e, "shared.s.origin");
  s.angles = qa_compat_q2_rerelease_source_edict_vector(x->e, "shared.s.angles");
  s.velocity = qa_compat_q2_rerelease_source_edict_vector(x->e, "velocity");
  s.bounds.min = qa_compat_q2_rerelease_source_edict_vector(x->e, "shared.mins");
  s.bounds.max = qa_compat_q2_rerelease_source_edict_vector(x->e, "shared.maxs");
  /* The ground id lives with this binding (one process-wide static served every edict's read). The body table
   * copies it as soon as the read returns. */
  qa_actor_id ground_actor;
  if (has && x->addresses.actor(x->addresses.ctx, ground, &ground_actor)) { x->ground = ground_actor; s.ground = &x->ground; }
  return s;
}
static void body_write(void *vctx, qa_body_state state) {
  body_ctx_t *x = vctx;
  qa_compat_q2_rerelease_source_edict_write_vector(x->e, "shared.s.origin", state.origin);
  qa_compat_q2_rerelease_source_edict_write_vector(x->e, "shared.s.angles", state.angles);
  qa_compat_q2_rerelease_source_edict_write_vector(x->e, "velocity", state.velocity);
  qa_compat_q2_rerelease_source_edict_write_vector(x->e, "shared.mins", state.bounds.min);
  qa_compat_q2_rerelease_source_edict_write_vector(x->e, "shared.maxs", state.bounds.max);
  qa_guest_address g; memset(&g, 0, sizeof g);
  bool has = state.ground != NULL;
  if (has) g = x->addresses.address(x->addresses.ctx, *state.ground);
  const qa_compat_q2_memory * m = mem_of(x->e);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(x->e, "groundentity");
  qa_compat_q2_mem_write_pointer(m, a, has, g);
}
qa_body_state_binding qa_compat_q2_rerelease_source_edict_body(qa_compat_q2_rerelease_source_edict *e, qa_compat_q2_rerelease_actor_addresses addresses) {
  body_ctx_t *x = binding_new(&e->bindings, sizeof *x); x->e = e; x->addresses = addresses;
  return (qa_body_state_binding){x, body_read, body_write, NULL};
}
typedef struct cb_ctx { qa_compat_q2_rerelease_source_edict *e; qa_compat_q2_rerelease_actor_addresses addresses;
  void *encode_ctx; qa_compat_q2_rerelease_encode_trace_fn encode; } cb_ctx_t;
static bool callback_present(cb_ctx_t *x, const char *name) {
  char field[64]; snprintf(field, sizeof field, "%s.value", name);
  qa_guest_address ignored; const qa_compat_q2_memory * m = mem_of(x->e); qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(x->e, field); return qa_compat_q2_mem_read_pointer(m, a, &ignored);
}
static bool actor_address(cb_ctx_t *x, const qa_actor_id *actor, qa_guest_address *out) {
  memset(out, 0, sizeof *out);
  if (!actor) return false;
  *out = x->addresses.address(x->addresses.ctx, *actor); return true;
}
/* TS withCause: the mod_t aggregate over caller storage (3 bytes), valid while the call runs. */
static qa_guest_call_value cause_mod(const qa_attack_provenance *attack, uint8_t *bytes) {
  if (attack && attack->cause.kind != QA_ATTACK_CAUSE_Q2) qa_raise("Error", 0, 0, "Native rerelease callback requires a classified Q2 damage cause");
  qa_q2_native_cause native; memset(&native, 0, sizeof native); bool have = false;
  if (attack && attack->cause.kind == QA_ATTACK_CAUSE_Q2 && attack->cause.has_native && attack->cause.native.edition &&
      strcmp(attack->cause.native.edition, "rerelease") == 0) { native = attack->cause.native; have = true; }
  else {
    /* nativeCauseFromCanonical({ edition: "rerelease" }, ...): the rerelease profile, no point loss unset. */
    qa_q2mp_native_cause_profile prof = {true, NULL, false};
    have = qa_q2mp_native_cause_from_canonical(prof, attack && attack->cause.kind == QA_ATTACK_CAUSE_Q2 ? attack->cause.means_of_death : 0, &native);
  }
  if (!have || !native.edition || strcmp(native.edition, "rerelease") != 0) qa_raise("Error", 0, 0, "Damage cause has no rerelease mod_t representation");
  qa_guest_call_value v; memset(&v, 0, sizeof v);
  v.kind = QA_GUEST_VALUE_AGGREGATE; v.layout = qa_compat_q2_native_rerelease_mod_layout;
  bytes[0] = qa_js_to_uint8(native.id); bytes[1] = native.friendly_fire ? 1 : 0; bytes[2] = native.no_point_loss ? 1 : 0;
  v.bytes.bytes = bytes; v.bytes.length = 3; return v;
}
static void cb_think(void *vctx, qa_owned_actor self, qa_frame_context frame) {
  (void)self; (void)frame; cb_ctx_t *x = vctx;
  (void)qa_compat_q2_rerelease_source_edict_call(x->e, "think", NULL, 0, NULL);
}
static void cb_use(void *vctx, qa_owned_actor self, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)self; cb_ctx_t *x = vctx;
  if (!callback_present(x, "use")) return;
  qa_guest_address a, b;
  qa_guest_call_value value = qa_compat_q2_call_pointer(actor_address(x, other, &a), a);
  qa_guest_call_value value2 = qa_compat_q2_call_pointer(actor_address(x, activator, &b), b);
  qa_guest_call_value args[] = {value, value2};
  (void)qa_compat_q2_rerelease_source_edict_call(x->e, "use", args, 2, NULL);
}
typedef struct scratch_call { cb_ctx_t *x; qa_guest_address address; double bytes; const uint8_t *data; size_t data_length; const char *name; qa_guest_call_value *args; size_t count; qa_vec3 point; bool is_point; } scratch_call;
static void scratch_body(void *v) {
  scratch_call *c = v; qa_compat_q2_memory *memory = mem_of(c->x->e);
  /* The writes are inside the TS try: a refused write still unmaps. */
  if (c->is_point) {
    qa_compat_q2_mem_write_f32(memory, c->address, c->point.x);
    qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, c->address, 4), c->point.y);
    qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, c->address, 8), c->point.z);
  } else memory->write(memory->ctx, c->address, c->data, c->data_length);
  (void)qa_compat_q2_rerelease_source_edict_call(c->x->e, c->name, c->args, c->count, NULL);
}
static void scratch_cleanup(void *v) { scratch_call *c = v; qa_compat_q2_memory *memory = mem_of(c->x->e); memory->unmap(memory->ctx, c->address, c->bytes); }
static void cb_touch(void *vctx, qa_touch_contact contact) {
  cb_ctx_t *x = vctx;
  if (!callback_present(x, "touch")) return;
  if (!contact.source_trace) qa_raise("Error", 0, 0, "Native rerelease touch requires its source trace");
  qa_guest_address other = x->addresses.address(x->addresses.ctx, contact.other);
  qa_guest_call_result encoded = x->encode(x->encode_ctx, contact.source_trace->trace);
  if (encoded.kind != QA_GUEST_VALUE_AGGREGATE) qa_raise("Error", 0, 0, "Native rerelease trace encoder must return an aggregate");
  qa_compat_q2_memory *memory = mem_of(x->e);
  qa_guest_address trace = memory->allocate(memory->ctx, (double)encoded.bytes.length, true, 8, "Q2 callback trace");
  qa_guest_call_value v; memset(&v, 0, sizeof v); v.kind = QA_GUEST_VALUE_UINT32; v.number_value = contact.source_trace->inverted ? 1 : 0;
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, other), qa_compat_q2_call_pointer(true, trace), v};
  /* try { write, call } finally { unmap }: the trace scratch does not stay mapped when the guest raises. */
  scratch_call c = {x, trace, (double)encoded.bytes.length, encoded.bytes.bytes, encoded.bytes.length, "touch", args, 3, {0, 0, 0}, false};
  run_finally(scratch_body, &c, scratch_cleanup, &c);
}
static void cb_pain(void *vctx, qa_pain_reaction reaction) {
  cb_ctx_t *x = vctx;
  if (!callback_present(x, "pain")) return;
  qa_guest_address other; bool has = actor_address(x, reaction.attacker, &other);
  qa_guest_call_value kick; memset(&kick, 0, sizeof kick); kick.kind = QA_GUEST_VALUE_FLOAT32; kick.number_value = reaction.kick;
  uint8_t mod_bytes[3]; qa_guest_call_value mod = cause_mod(reaction.attack, mod_bytes);
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(has, other), kick, qa_compat_q2_call_int32(reaction.damage), mod};
  (void)qa_compat_q2_rerelease_source_edict_call(x->e, "pain", args, 4, NULL);
}
static void cb_die(void *vctx, qa_death_reaction reaction) {
  cb_ctx_t *x = vctx;
  if (!callback_present(x, "die")) return;
  qa_guest_address inflictor, attacker;
  bool has_i = actor_address(x, reaction.inflictor, &inflictor), has_a = actor_address(x, reaction.pain.attacker, &attacker);
  uint8_t mod_bytes[3]; qa_guest_call_value mod = cause_mod(reaction.pain.attack, mod_bytes);
  qa_compat_q2_memory *memory = mem_of(x->e);
  qa_guest_address point = memory->allocate(memory->ctx, 12, true, 4, "Q2 callback damage point");
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(has_i, inflictor), qa_compat_q2_call_pointer(has_a, attacker),
                                qa_compat_q2_call_int32(reaction.pain.damage), qa_compat_q2_call_pointer(true, point), mod};
  /* try { write the point, call } finally { unmap }. */
  scratch_call c = {x, point, 12, NULL, 0, "die", args, 5, reaction.point, true};
  run_finally(scratch_body, &c, scratch_cleanup, &c);
}
qa_actor_callbacks qa_compat_q2_rerelease_source_edict_callbacks(qa_compat_q2_rerelease_source_edict *e,
                                                                 qa_compat_q2_rerelease_actor_addresses addresses,
                                                                 void *encode_ctx, qa_compat_q2_rerelease_encode_trace_fn encode_trace) {
  cb_ctx_t *x = binding_new(&e->bindings, sizeof *x); x->e = e; x->addresses = addresses; x->encode_ctx = encode_ctx; x->encode = encode_trace;
  return (qa_actor_callbacks){x, cb_think, cb_touch, cb_use, cb_pain, cb_die};
}
/* The lowered arguments of a declared combat call: the parameter storage, the values, and the copies decodeValue
 * made for aggregate defaults (the lowering was done without captured arguments). */
typedef struct lowered_call { qa_guest_value_layout *storage; qa_guest_call_value *values; const qa_compat_q2_native_combat_call *call; size_t count; } lowered_call;
static void lowered_call_release(void *v) {
  lowered_call *l = v;
  /* The values start zeroed, so an entry the lowering did not reach has nothing to free. */
  if (l->values) for (size_t i = 0; i < l->call->argument_count; i++) {
    const qa_compat_q2_native_combat_argument *a = &l->call->arguments[i];
    if (a->kind == QA_Q2CA_VALUE && a->layout.is_aggregate) free((void *)l->values[i].bytes.bytes);
  }
  free(l->storage); free(l->values); l->storage = NULL; l->values = NULL; l->count = 0;
}
qa_guest_call_result qa_compat_q2_rerelease_source_edict_call(qa_compat_q2_rerelease_source_edict *e, const char *name,
                                                              const qa_guest_call_value *arguments, size_t count, const qa_raw_entity_view *other) {
  char field[64]; snprintf(field, sizeof field, "%s.value", name);
  qa_guest_address address;
  const qa_compat_q2_memory * m = mem_of(e);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(e, field);
  if (!qa_compat_q2_mem_read_pointer(m, a, &address))
    return qa_compat_q2_call_void();
  qa_guest_call_value *all = malloc((count + 1) * sizeof *all); if (!all) qa_range_error("Out of memory");
  qa_cleanup all_owned; qa_cleanup_push(&all_owned, free, all);
  all[0] = qa_compat_q2_call_pointer(true, e->raw.address);
  if (count) memcpy(all + 1, arguments, count * sizeof *all);
  qa_guest_call_result result;
  if (strcmp(name, "pain") == 0 || strcmp(name, "die") == 0) {
    qa_compat_q2_native_combat_operation op = strcmp(name, "pain") == 0 ? QA_Q2CO_PAIN : QA_Q2CO_DEATH;
    const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(e->module);
    const qa_compat_q2_native_combat_call *call = op == QA_Q2CO_PAIN ? &profile->calls.pain : &profile->calls.death;
    /* A declared call has no argument limit: the signature and the lowered values take call->argument_count. */
    lowered_call lowered = {NULL, NULL, call, 0};
    qa_cleanup lowered_owned; qa_cleanup_push(&lowered_owned, lowered_call_release, &lowered);
    size_t capacity = call->argument_count ? call->argument_count : 1;
    lowered.storage = calloc(capacity, sizeof *lowered.storage); lowered.values = calloc(capacity, sizeof *lowered.values);
    if (!lowered.storage || !lowered.values) qa_range_error("Out of memory");
    qa_compat_q2_guest_call_signature signature = qa_compat_q2_native_combat_signature(call, op, qa_compat_q2_rerelease_abi, lowered.storage);
    const qa_compat_q2_rerelease_module_options *opts = qa_compat_q2_rerelease_guest_module_get_options(e->module);
    lowered.count = qa_compat_q2_lower_native_combat_arguments(call, op, all, count + 1, mem_of(e), opts->has_image_base, opts->image_base, NULL, 0, lowered.values);
    result = qa_compat_q2_rerelease_guest_module_invoke(e->module, address, signature, lowered.values, lowered.count, &e->raw, other);
    qa_cleanup_pop(&lowered_owned); lowered_call_release(&lowered);
  } else {
    static const qa_guest_value_layout p3[] = {{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}}};
    static const qa_guest_value_layout p3b[] = {{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"uint8",{NULL,0,0,0,NULL,0}}};
    static const qa_guest_value_layout p1[] = {{false,"pointer",{NULL,0,0,0,NULL,0}}};
    const qa_guest_value_layout *params = strcmp(name, "touch") == 0 ? p3b : strcmp(name, "use") == 0 ? p3 : p1;
    size_t pn = strcmp(name, "touch") == 0 ? 4 : strcmp(name, "use") == 0 ? 3 : 1;
    result = qa_compat_q2_rerelease_guest_module_invoke(e->module, address,
        qa_compat_q2_rerelease_signature(params, pn, true, qa_compat_q2_rr_int_layout), all, count + 1, &e->raw, other);
  }
  qa_cleanup_pop(&all_owned);
  free(all);
  return result;
}
