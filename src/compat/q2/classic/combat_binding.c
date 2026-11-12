/* Port of src/compat/q2/classic/combat-binding.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/combat_binding.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_damage.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/core/info_string.h"
#include "qa/world/actors/body.h"
#include "qa/world/gameplay/authority.h"
#include "qa/foundation/js.h"

static qa_guest_call_value decode_value(qa_guest_value_layout layout, const uint8_t *bytes, size_t len, const qa_compat_q2_memory *memory) {
  qa_guest_call_value v; memset(&v, 0, sizeof v);
  if (layout.is_aggregate) {
    v.kind = QA_GUEST_VALUE_AGGREGATE; v.layout = layout.layout;
    uint8_t *copy = malloc(len ? len : 1); memcpy(copy, bytes, len); v.bytes.bytes = copy; v.bytes.length = len; return v;
  }
  const char *s = layout.storage;
  uint64_t raw = 0; for (size_t i = len; i-- > 0;) raw = (raw << 8) | bytes[i];
  if (!strcmp(s, "pointer")) { (void)memory; v.kind = QA_GUEST_VALUE_POINTER; v.pointer_value = qa_compat_q2_mem_pointer(memory, (int64_t)raw); return v; }
  if (!strcmp(s, "float32")) { uint32_t u = (uint32_t)raw; float f; memcpy(&f, &u, 4); v.kind = QA_GUEST_VALUE_FLOAT32; v.number_value = f; return v; }
  if (!strcmp(s, "float64")) { double d; memcpy(&d, &raw, 8); v.kind = QA_GUEST_VALUE_FLOAT64; v.number_value = d; return v; }
  if (!strcmp(s, "int64")) { v.kind = QA_GUEST_VALUE_INT64; v.int64_value = (int64_t)raw; return v; }
  if (!strcmp(s, "uint64")) { v.kind = QA_GUEST_VALUE_UINT64; v.uint64_value = raw; return v; }
  if (s[0] == 'u') { v.kind = QA_GUEST_VALUE_UINT32; v.number_value = (double)(uint32_t)raw; return v; }
  v.kind = QA_GUEST_VALUE_INT32; v.number_value = (double)(int32_t)raw; return v;
}
size_t qa_compat_q2_lower_native_combat_arguments(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation operation,
                                                  const qa_guest_call_value *values, size_t count, const qa_compat_q2_memory *memory,
                                                  bool has_image, qa_guest_address image, const qa_guest_call_value *original, size_t original_count,
                                                  qa_guest_call_value *out) {
  size_t semantic_count; const qa_compat_q2_native_combat_field *semantic = qa_compat_q2_native_combat_fields(operation, memory->pointer_bytes, &semantic_count);
  if (count != semantic_count || (original && original_count != call->argument_count))
    qa_raise("Error", 0, 0, "Native combat continuation changed its argument extent");
  for (size_t i = 0; i < call->argument_count; i++) {
    const qa_compat_q2_native_combat_argument *a = &call->arguments[i];
    if (a->kind == QA_Q2CA_FIELD) {
      size_t idx = (size_t)-1; for (size_t j = 0; j < semantic_count; j++) if (semantic[j] == a->field) { idx = j; break; }
      if (idx == (size_t)-1) qa_raise("Error", 0, 0, "Missing native combat field");
      out[i] = values[idx]; continue;
    }
    if (original) { out[i] = original[i]; continue; }
    if (a->kind == QA_Q2CA_VALUE) { out[i] = decode_value(a->layout, a->bytes, a->byte_count, memory); continue; }
    if (a->address && !has_image) qa_raise("Error", 0, 0, "Native combat address default requires its source image base");
    bool has = false; qa_guest_address address; memset(&address, 0, sizeof address);
    if (a->address && has_image) { has = true; address = qa_compat_q2_mem_offset(memory, image, (int64_t)a->address->rva); }
    if (a->address) for (size_t j = 0; j < a->address->indirections.count; j++) {
      if (!has) qa_raise("Error", 0, 0, "Native combat default address dereferences null");
      has = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)a->address->indirections.values[j]), &address);
    }
    out[i] = qa_compat_q2_call_pointer(has, address);
  }
  return call->argument_count;
}

typedef struct frame { qa_damage_request request; qa_raw_entity_view view; bool has_stack; uint64_t stack; } frame_t;
typedef struct armor_entry { qa_owned_actor actor; qa_armor_stage_intercept_fn regular, powered; void *regular_ctx, *powered_ctx; } armor_entry_t;
struct hook_ctx;
struct qa_compat_q2_classic_combat_bindings {
  qa_compat_q2_classic_guest_host *host; qa_guest_address image; qa_compat_q2_classic_combat_profile profile;
  /* TS frames (an array with no limit): each frame is its own block, so a pointer to one stays valid while deeper
   * damage pushes more. The frame's address is also the identity of its request (TS `error.request !== request`). */
  frame_t **frames; size_t frame_count, frame_cap;
  /* TS armorBindings (a Map with no limit). */
  armor_entry_t *armor; size_t armor_count, armor_cap;
  uint64_t hooks[4]; size_t hook_count; struct hook_ctx *hook_ctx[2]; uint64_t damage_hooks[2]; size_t damage_hook_count;
  bool closed, has_bypass; qa_protection_channel bypass_channel; bool bypass_pending; uint64_t bypass_stack;
  qa_compat_q2_guest_call_signature damage_sig, regular_sig, power_sig;
  qa_guest_value_layout *sig_storage[3];
  /* Per-actor binding and armor-stage contexts (the TS closures), owned until destroy. */
  void **owned; size_t owned_count, owned_cap; struct bctx **contexts; size_t context_count, context_cap;
  /* TS damageProxies: one inuse stand-in edict per foreign damage source, kept for the actor's life so hits do not
   * consume G_Spawn slots (insertion order). */
  struct { qa_actor_id actor; qa_raw_entity_view view; } *proxies; size_t proxy_count, proxy_cap;
  size_t reload_handle; bool reload_observed; /* TS reloadObserver */
};
static void own_block(void ***items, size_t *count, size_t *cap, void *block) {
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 16; void **grown = realloc(*items, *cap * sizeof *grown); if (!grown) qa_range_error("Out of memory"); *items = grown; }
  (*items)[(*count)++] = block;
}
static qa_compat_q2_memory *mem_of(qa_compat_q2_classic_combat_bindings *b) { return qa_compat_q2_classic_guest_host_get_memory(b->host); }
static qa_guest_address at_view(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view, double off) { return qa_compat_q2_mem_offset(mem_of(b), view.address, (int64_t)off); }
static qa_guest_address entry_of(qa_compat_q2_classic_combat_bindings *b, double off) { return qa_compat_q2_mem_offset(mem_of(b), b->image, (int64_t)off); }
static bool client_of(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view, qa_guest_address *out) {
  return qa_compat_q2_mem_read_pointer(mem_of(b), at_view(b, view, 84), out);
}
static double count_of(qa_compat_q2_classic_combat_bindings *b, qa_guest_address client, double index) {
  return qa_compat_q2_mem_read_i32(mem_of(b), qa_compat_q2_mem_offset(mem_of(b), client, (int64_t)(b->profile.client.inventory + index * 4)));
}
/* TS item ids (`q2:${name}`) are strings a combat state keeps, and the authority copies armor states shallowly, so
 * each distinct id is interned for the life of the process (the table stays reachable from this static). */
static const char *intern_item(const char *name) {
  static char **items; static size_t count, capacity;
  for (size_t i = 0; i < count; i++) if (strcmp(items[i] + 3, name) == 0) return items[i];
  if (count == capacity) {
    size_t grown_capacity = capacity ? capacity * 2 : 16; char **grown = realloc(items, grown_capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    items = grown; capacity = grown_capacity;
  }
  size_t need = strlen(name) + 4; char *item = malloc(need); if (!item) qa_range_error("Out of memory");
  snprintf(item, need, "q2:%s", name);
  return items[count++] = item;
}
static frame_t *top_frame(const qa_compat_q2_classic_combat_bindings *b) { return b->frame_count ? b->frames[b->frame_count - 1] : NULL; }
/* isDeepStrictEqual compares numbers with Object.is. */
static bool same_value(double a, double c) { return a != a ? c != c : (a == c && signbit(a) == signbit(c)); }
typedef struct { const char *item; double normal_protection, energy_protection; } armor_def_t;
static armor_def_t armor_definition(qa_compat_q2_classic_combat_bindings *b, double index) {
  qa_compat_q2_memory *memory = mem_of(b);
  qa_guest_address record = entry_of(b, b->profile.globals.item_list + index * b->profile.globals.item_bytes);
  qa_guest_address name_p, info;
  bool hn = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record, (int64_t)b->profile.item_fields.class_name), &name_p);
  char *name = qa_compat_q2_read_classic_string(memory, hn, name_p, 65536);
  bool hi = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record, (int64_t)b->profile.item_fields.armor_info), &info);
  if (name[0] == 0 || !hi) { free(name); qa_raise("Error", 0, 0, "Original native armor item lacks its name or protection information"); }
  const char *item = intern_item(name); free(name);
  double normal_protection2 = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, info, (int64_t)b->profile.armor_info.normal_protection));
  double energy_protection2 = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, info, (int64_t)b->profile.armor_info.energy_protection));
  return (armor_def_t){item, normal_protection2, energy_protection2};
}
static qa_armor_state armor_of(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view) {
  qa_armor_state none; memset(&none, 0, sizeof none); none.regular.kind = QA_REGULAR_ARMOR_NONE; none.powered.kind = "none";
  qa_guest_address client;
  if (!client_of(b, view, &client)) return none;
  double index = -1;
  for (size_t i = 0; i < b->profile.armor.regular_count; i++) if (count_of(b, client, b->profile.armor.regular[i]) > 0) { index = b->profile.armor.regular[i]; break; }
  bool powered = (qa_compat_q2_mem_read_i32(mem_of(b), at_view(b, view, b->profile.fields.flags)) & qa_js_to_int32(b->profile.flags.power_armor)) != 0;
  const char *power = !powered ? NULL : count_of(b, client, b->profile.items.shield) > 0 ? "shield" : count_of(b, client, b->profile.items.screen) > 0 ? "screen" : NULL;
  if (index < 0 && !power) return none;
  qa_armor_state state = none;
  if (index >= 0) {
    armor_def_t def = armor_definition(b, index);
    state.regular.kind = QA_REGULAR_ARMOR_Q2; state.regular.item = def.item;
    state.regular.normal_protection = def.normal_protection; state.regular.energy_protection = def.energy_protection;
    state.regular.points = count_of(b, client, index);
  }
  if (power) { state.powered.kind = power; state.powered.cells = count_of(b, client, b->profile.items.cells); }
  return state;
}
/* isDeepStrictEqual(prior, effective) over the ArmorState union: every member of the kind, numbers by Object.is. */
static bool item_eq(qa_item_id a, qa_item_id c) { return (a == NULL) == (c == NULL) && (a == NULL || strcmp(a, c) == 0); }
static bool armor_eq(qa_armor_state a, qa_armor_state c) {
  if (a.regular.kind != c.regular.kind) return false;
  switch (a.regular.kind) {
    case QA_REGULAR_ARMOR_NONE: break;
    case QA_REGULAR_ARMOR_Q1:
      if (!same_value(a.regular.points, c.regular.points) || !same_value(a.regular.absorption, c.regular.absorption) || !item_eq(a.regular.item, c.regular.item)) return false;
      break;
    case QA_REGULAR_ARMOR_Q2:
      if (!same_value(a.regular.points, c.regular.points) || !same_value(a.regular.normal_protection, c.regular.normal_protection) ||
          !same_value(a.regular.energy_protection, c.regular.energy_protection) || !item_eq(a.regular.item, c.regular.item)) return false;
      break;
    case QA_REGULAR_ARMOR_Q3:
      if (!same_value(a.regular.points, c.regular.points) || !same_value(a.regular.protection, c.regular.protection)) return false;
      break;
    case QA_REGULAR_ARMOR_SOURCE:
      if (!same_value(a.regular.points, c.regular.points) || !item_eq(a.regular.item, c.regular.item)) return false;
      break;
  }
  if (strcmp(a.powered.kind, c.powered.kind) != 0) return false;
  if (strcmp(a.powered.kind, "none") != 0 && !same_value(a.powered.cells, c.powered.cells)) return false;
  return true;
}
/* Forward: damage implementation used by the binding. */
static qa_damage_outcome do_damage(qa_compat_q2_classic_combat_bindings *b, qa_damage_request input, qa_raw_entity_view view,
                                   const qa_guest_call_value *source_args, size_t source_count);
/* Binding ctx */
typedef struct bctx { qa_compat_q2_classic_combat_bindings *b; qa_raw_entity_view view; qa_owned_actor actor; char *team; } bctx_t;
/* TS emptyRegularArmor reads the guest's item record on every call. */
static qa_regular_armor_state empty_regular(bctx_t *c, double points) {
  armor_def_t def = armor_definition(c->b, c->b->profile.armor.empty);
  qa_regular_armor_state r; memset(&r, 0, sizeof r);
  r.kind = QA_REGULAR_ARMOR_Q2; r.item = def.item; r.normal_protection = def.normal_protection; r.energy_protection = def.energy_protection;
  r.points = points; return r;
}
static void validate_armor(bctx_t *c, qa_armor_state armor) {
  if (armor.regular.kind != QA_REGULAR_ARMOR_NONE && armor.regular.kind != QA_REGULAR_ARMOR_Q2)
    qa_raise("Error", 0, 0, "Source armor requires Q2 armor values");
  if (armor.regular.kind == QA_REGULAR_ARMOR_Q2) {
    bool ok = false; /* Array.prototype.some stops at the first match */
    for (size_t i = 0; i < c->b->profile.armor.regular_count && !ok; i++) {
      armor_def_t def = armor_definition(c->b, c->b->profile.armor.regular[i]);
      if (armor.regular.item && strcmp(def.item, armor.regular.item) == 0) ok = true;
    }
    if (!ok) qa_raise("Error", 0, 0, "Source armor item is outside its declared native inventory");
  }
  if (strcmp(armor.powered.kind, armor_of(c->b, c->view).powered.kind) != 0)
    qa_raise("Error", 0, 0, "Native power activation requires its original source equipment operation");
}
static qa_combat_state bind_read(void *vctx) {
  double left;
  double right;
  bctx_t *c = vctx; qa_compat_q2_classic_combat_bindings *b = c->b; qa_compat_q2_memory *memory = mem_of(b);
  qa_guest_address client; bool has_client = client_of(b, c->view, &client);
  int32_t flags = qa_compat_q2_mem_read_i32(memory, at_view(b, c->view, b->profile.fields.flags));
  qa_cvar_registry *cvars = qa_compat_q2_classic_guest_host_get_options(b->host)->services.cvars;
  char *skin = NULL;
  free(c->team); c->team = NULL;
  if (has_client) {
    char *userinfo = qa_compat_q2_read_classic_string(memory, true, qa_compat_q2_mem_offset(memory, client, (int64_t)b->profile.client.userinfo), b->profile.client.userinfo_bytes);
    skin = qa_info_value_for_key_bounded(userinfo, "skin", (int64_t)b->profile.client.userinfo_bytes);
    free(userinfo);
    int32_t rules = qa_js_to_int32(qa_cvar_registry_variable_value(cvars, "dmflags")); /* rules & mask: ToInt32 */
    if (qa_cvar_registry_variable_value(cvars, "coop") != 0) c->team = strdup("q2:coop");
    else if ((rules & qa_js_to_int32(b->profile.teams.model)) != 0) {
      const char *slash = strchr(skin, '/'); size_t n = slash ? (size_t)(slash - skin) : strlen(skin);
      c->team = malloc(n + 10); snprintf(c->team, n + 10, "q2:model:%.*s", (int)n, skin);
    } else if ((rules & qa_js_to_int32(b->profile.teams.skin)) != 0) {
      /* skin.split("/")[1] ?? skin */
      const char *slash = strchr(skin, '/'); const char *part = skin; size_t n = strlen(skin);
      if (slash) { const char *next = strchr(slash + 1, '/'); part = slash + 1; n = next ? (size_t)(next - part) : strlen(part); }
      c->team = malloc(n + 9); memcpy(c->team, "q2:skin:", 8); memcpy(c->team + 8, part, n); c->team[8 + n] = '\0';
    }
  }
  qa_combat_state state; memset(&state, 0, sizeof state);
  state.health = qa_compat_q2_mem_read_i32(memory, at_view(b, c->view, b->profile.fields.health));
  state.can_take_damage = qa_compat_q2_mem_read_i32(memory, at_view(b, c->view, b->profile.fields.damageable)) != 0;
  state.mass = qa_compat_q2_mem_read_i32(memory, at_view(b, c->view, b->profile.fields.mass));
  state.armor = armor_of(b, c->view); state.team = c->team;
  state.has_no_knockback = true; state.no_knockback = (flags & qa_js_to_int32(b->profile.flags.no_knockback)) != 0;
  state.invulnerable = (flags & qa_js_to_int32(b->profile.flags.invulnerable)) != 0 ||
    (has_client && (left = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, client, (int64_t)b->profile.client.invincible_frame)), right = qa_compat_q2_mem_read_i32(memory, entry_of(b, b->profile.globals.level_frame)), left >
                   right));
  free(skin);
  return state;
}
static void bind_write_health(void *vctx, double health) {
  bctx_t *c = vctx; qa_compat_q2_mem_write_i32(mem_of(c->b), at_view(c->b, c->view, c->b->profile.fields.health), qa_js_to_int32(health));
}
static void bind_write_armor(void *vctx, qa_armor_state armor) {
  bctx_t *c = vctx; qa_compat_q2_classic_combat_bindings *b = c->b; qa_compat_q2_memory *memory = mem_of(b);
  validate_armor(c, armor);
  qa_guest_address client;
  if (!client_of(b, c->view, &client)) {
    if (armor.regular.kind != QA_REGULAR_ARMOR_NONE || strcmp(armor.powered.kind, "none") != 0) qa_raise("Error", 0, 0, "Source non-client has no inventory armor");
    return;
  }
  qa_regular_armor_state current = armor_of(b, c->view).regular;
  for (size_t i = 0; i < b->profile.armor.regular_count; i++) {
    double index = b->profile.armor.regular[i];
    armor_def_t def = armor_definition(b, index);
    bool skip = armor.regular.kind == QA_REGULAR_ARMOR_Q2 && current.kind == QA_REGULAR_ARMOR_Q2 &&
                armor.regular.item && current.item && strcmp(armor.regular.item, current.item) == 0 && strcmp(def.item, current.item) != 0;
    if (!skip) {
      qa_guest_address address = qa_compat_q2_mem_offset(memory, client, (int64_t)(b->profile.client.inventory + index * 4));
      double points = (armor.regular.kind == QA_REGULAR_ARMOR_Q2 && armor.regular.item && strcmp(armor.regular.item, def.item) == 0) ? armor.regular.points : 0;
      if ((double)qa_compat_q2_mem_read_i32(memory, address) != points) qa_compat_q2_mem_write_i32(memory, address, qa_js_to_int32(points));
    }
  }
  if (strcmp(armor.powered.kind, "none") != 0)
    qa_compat_q2_mem_write_i32(memory, qa_compat_q2_mem_offset(memory, client, (int64_t)(b->profile.client.inventory + b->profile.items.cells * 4)), qa_js_to_int32(armor.powered.cells));
}
static qa_regular_armor_state bind_empty(void *vctx, double points) { return empty_regular(vctx, points); }
static void bind_validate(void *vctx, qa_armor_state armor) { validate_armor(vctx, armor); }
static qa_armor_state bind_normalize(void *vctx, qa_armor_state armor) {
  bctx_t *c = vctx; return qa_normalize_legacy_power_only_armor(armor, armor_of(c->b, c->view), "q2:none");
}
static qa_damage_outcome bind_source_damage(void *vctx, qa_damage_request request) {
  bctx_t *c = vctx; return do_damage(c->b, request, c->view, NULL, 0);
}
/* Armor stage */
typedef struct stage_ctx { qa_compat_q2_classic_combat_bindings *b; qa_owned_actor actor; qa_protection_channel channel; } stage_ctx_t;
typedef struct removal_ctx { qa_compat_q2_classic_combat_bindings *b; qa_owned_actor actor; qa_protection_channel channel; qa_armor_stage_intercept_fn fn; void *fn_ctx; } removal_ctx_t;
static armor_entry_t *find_armor(qa_compat_q2_classic_combat_bindings *b, qa_owned_actor actor) {
  for (size_t i = 0; i < b->armor_count; i++) if (qa_same_owned_actor(b->armor[i].actor, actor)) return &b->armor[i];
  return NULL;
}
/* TS armorHooks.runAll(): the stack is emptied first, then each removal runs in registration order. */
static void remove_armor_hooks(qa_compat_q2_classic_combat_bindings *b) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  uint64_t hooks[4]; size_t count = b->hook_count; memcpy(hooks, b->hooks, sizeof hooks); b->hook_count = 0;
  for (size_t i = 0; i < count; i++) opts->runner->remove_entry(opts->runner->ctx, hooks[i]);
  for (int i = 0; i < 2; i++) { free(b->hook_ctx[i]); b->hook_ctx[i] = NULL; }
}
/* `delete entry[channel]` when it still holds this intercept, then the actor's entry when both channels are empty. */
static void clear_stage(qa_compat_q2_classic_combat_bindings *b, qa_owned_actor actor, qa_protection_channel channel,
                        qa_armor_stage_intercept_fn fn, void *fn_ctx) {
  armor_entry_t *entry = find_armor(b, actor);
  if (!entry) return;
  if (channel == QA_PROTECTION_REGULAR && entry->regular == fn && entry->regular_ctx == fn_ctx) { entry->regular = NULL; entry->regular_ctx = NULL; }
  if (channel == QA_PROTECTION_POWERED && entry->powered == fn && entry->powered_ctx == fn_ctx) { entry->powered = NULL; entry->powered_ctx = NULL; }
  if (!entry->regular && !entry->powered) *entry = b->armor[--b->armor_count];
}
static void stage_remove(void *vctx) {
  removal_ctx_t *r = vctx; qa_compat_q2_classic_combat_bindings *b = r->b;
  clear_stage(b, r->actor, r->channel, r->fn, r->fn_ctx);
  free(r);
  if (b->armor_count == 0) remove_armor_hooks(b);
}
static void install_armor_hooks(qa_compat_q2_classic_combat_bindings *b);
static void install_armor_hooks_body(void *b) { install_armor_hooks(b); }
static qa_armor_stage_removal stage_bind(void *vctx, qa_armor_stage_intercept_fn intercept, void *intercept_ctx) {
  stage_ctx_t *s = vctx; qa_compat_q2_classic_combat_bindings *b = s->b;
  armor_entry_t *entry = find_armor(b, s->actor);
  if (entry && (s->channel == QA_PROTECTION_REGULAR ? entry->regular : entry->powered)) qa_raise("Error", 0, 0, "Classic source armor stage already has an owner");
  if (!entry) {
    if (b->armor_count == b->armor_cap) {
      size_t cap = b->armor_cap ? b->armor_cap * 2 : 16; armor_entry_t *grown = realloc(b->armor, cap * sizeof *grown);
      if (!grown) qa_range_error("Out of memory");
      b->armor = grown; b->armor_cap = cap;
    }
    entry = &b->armor[b->armor_count++]; memset(entry, 0, sizeof *entry); entry->actor = s->actor;
  }
  if (s->channel == QA_PROTECTION_REGULAR) { entry->regular = intercept; entry->regular_ctx = intercept_ctx; }
  else { entry->powered = intercept; entry->powered_ctx = intercept_ctx; }
  removal_ctx_t *r = malloc(sizeof *r); if (!r) qa_range_error("Out of memory");
  *r = (removal_ctx_t){b, s->actor, s->channel, intercept, intercept_ctx};
  if (b->hook_count == 0) {
    qa_caught_error caught;
    if (!qa_try(install_armor_hooks_body, b, &caught)) {
      /* TS catch: the stage is unbound again and the hooks installed so far are removed. */
      free(r);
      clear_stage(b, s->actor, s->channel, intercept, intercept_ctx);
      remove_armor_hooks(b);
      qa_rethrow(&caught);
    }
  }
  return (qa_armor_stage_removal){r, stage_remove};
}
static qa_source_armor_stage armor_stage(qa_compat_q2_classic_combat_bindings *b, qa_owned_actor actor, qa_protection_channel channel) {
  stage_ctx_t *s = malloc(sizeof *s); if (!s) qa_range_error("Out of memory"); *s = (stage_ctx_t){b, actor, channel};
  own_block(&b->owned, &b->owned_count, &b->owned_cap, s);
  return (qa_source_armor_stage){s, stage_bind};
}
void qa_compat_q2_classic_combat_bindings_bind(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view, qa_owned_actor actor) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  if ((int64_t)view.stride_bytes != (int64_t)b->profile.entity_bytes) qa_raise("Error", 0, 0, "Native combat declaration differs from the source edict stride");
  bctx_t *c = calloc(1, sizeof *c); if (!c) qa_range_error("Out of memory"); c->b = b; c->view = view; c->actor = actor;
  own_block((void ***)&b->contexts, &b->context_count, &b->context_cap, c);
  qa_combat_state_binding binding; memset(&binding, 0, sizeof binding);
  binding.ctx = c; binding.validate_armor = bind_validate; binding.empty_regular_armor = bind_empty;
  binding.normalize_legacy_armor = bind_normalize; binding.source_damage = bind_source_damage;
  binding.read = bind_read; binding.write_health = bind_write_health; binding.write_armor = bind_write_armor;
  binding.protection_regular.declared = true; binding.protection_regular.owner = mem_of(b)->module.id;
  binding.protection_powered.declared = true; binding.protection_powered.owner = mem_of(b)->module.id;
  if (opts->services.damage_provenance) {
    binding.protection_regular.has_stage = true; binding.protection_regular.stage = armor_stage(b, actor, QA_PROTECTION_REGULAR);
    binding.protection_powered.has_stage = true; binding.protection_powered.stage = armor_stage(b, actor, QA_PROTECTION_POWERED);
  }
  qa_gameplay_authority_bind(opts->services.engine->combat, actor, binding);
}
bool qa_compat_q2_classic_combat_bindings_notarget(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view) {
  return (qa_compat_q2_mem_read_u32(mem_of(b), at_view(b, view, b->profile.fields.flags)) & (uint32_t)b->profile.flags.notarget) != 0;
}
qa_raw_entity_view qa_compat_q2_classic_combat_bindings_spawn_proxy_edict(qa_compat_q2_classic_combat_bindings *b) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_guest_call_result allocated = qa_compat_q2_classic_guest_host_invoke(
      b->host, entry_of(b, b->profile.entries.spawn), qa_compat_q2_classic_signature(NULL, 0, false, qa_compat_q2_q2_pointer, false),
      NULL, 0, NULL, opts->instruction_budget);
  if (allocated.kind != QA_GUEST_VALUE_POINTER || !allocated.pointer_value.has_value) qa_raise("Error", 0, 0, "Source G_Spawn returned no monster projection");
  return qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(b->host), allocated.pointer_value.value);
}
void qa_compat_q2_classic_combat_bindings_free_proxy_edict(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view) {
  static const qa_guest_value_layout pp[] = {{false,"pointer",{0}}};
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_guest_call_value v = qa_compat_q2_call_pointer(true, view.address);
  (void)qa_compat_q2_classic_guest_host_invoke(b->host, entry_of(b, b->profile.entries.free),
      qa_compat_q2_classic_signature(pp, 1, true, qa_compat_q2_q2_int, false), &v, 1, NULL, opts->instruction_budget);
  qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(b->host));
}
void qa_compat_q2_classic_combat_bindings_sync_proxy_edict(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view,
                                                           qa_compat_q2_proxy_body body, const qa_compat_q2_proxy_combat *combat) {
  qa_compat_q2_memory *memory = mem_of(b);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, view.address, 4), body.origin);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, view.address, 16), body.angles);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, view.address, 188), body.bounds.min);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, view.address, 200), body.bounds.max);
  qa_compat_q2_write_classic_vector(memory, at_view(b, view, b->profile.fields.velocity), body.velocity);
  qa_compat_q2_mem_write_i32(memory, at_view(b, view, b->profile.fields.health), combat ? qa_js_to_int32(combat->health) : 0);
  qa_compat_q2_mem_write_i32(memory, at_view(b, view, b->profile.fields.damageable), combat && combat->can_take_damage ? 1 : 0);
  qa_compat_q2_mem_write_i32(memory, at_view(b, view, b->profile.fields.mass), combat ? qa_js_to_int32(combat->mass) : 100);
}
double qa_compat_q2_classic_combat_bindings_proxy_health(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view) {
  return qa_compat_q2_mem_read_i32(mem_of(b), at_view(b, view, b->profile.fields.health));
}
qa_vec3 qa_compat_q2_classic_combat_bindings_proxy_velocity(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view) {
  return qa_compat_q2_read_classic_vector(mem_of(b), at_view(b, view, b->profile.fields.velocity));
}
/* Hooks */
static uint64_t rsp_of(qa_compat_q2_classic_combat_bindings *b) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  return opts->runner->read_rsp(opts->runner->ctx, 32);
}
static bool target_at_entry(qa_compat_q2_classic_combat_bindings *b, const qa_compat_q2_native_combat_call *call,
                            qa_compat_q2_guest_call_signature signature, qa_owned_actor *out) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_guest_call_value field = opts->runner->read_combat_field(opts->runner->ctx, call, signature, "target");
  qa_guest_address a;
  if (!qa_compat_q2_classic_pointer(&field, 1, 0, &a)) return false;
  return qa_compat_q2_classic_edicts_observe(qa_compat_q2_classic_guest_host_get_edicts(b->host), a, out);
}
typedef struct hook_ctx { qa_compat_q2_classic_combat_bindings *b; qa_protection_channel channel; } hook_ctx_t;
static void damage_observe(void *vctx) {
  qa_compat_q2_classic_combat_bindings *b = vctx; frame_t *frame = top_frame(b);
  if (frame && !frame->has_stack) { frame->stack = rsp_of(b); frame->has_stack = true; }
}
static void incoming_damage(qa_compat_q2_classic_combat_bindings *b, const qa_guest_call_value *source_args, size_t source_count);
static qa_guest_call_result damage_invoke(void *vctx, const qa_guest_call_context *context, const qa_guest_call_value *args, size_t count) {
  (void)context; qa_compat_q2_classic_combat_bindings *b = vctx;
  incoming_damage(b, args, count);
  return qa_compat_q2_call_void();
}
/* TS standIn(call, signature): the shared actor behind the call's target edict, when it is a stand-in. */
static bool stand_in_at_entry(qa_compat_q2_classic_combat_bindings *b, const qa_compat_q2_native_combat_call *call,
                              qa_compat_q2_guest_call_signature signature) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  if (!opts->services.stand_in_actor) return false;
  qa_guest_call_value field = opts->runner->read_combat_field(opts->runner->ctx, call, signature, "target");
  qa_guest_address a;
  if (!qa_compat_q2_classic_pointer(&field, 1, 0, &a)) return false;
  qa_actor_id actor;
  return opts->services.stand_in_actor(opts->services.stand_in_ctx,
      qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(b->host), a), &actor);
}
static bool damage_accepts(void *vctx) {
  qa_compat_q2_classic_combat_bindings *b = vctx;
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  if (b->closed) return false;
  frame_t *frame = top_frame(b);
  if (frame && frame->has_stack && frame->stack == rsp_of(b)) return false;
  if (stand_in_at_entry(b, &b->profile.calls.damage, b->damage_sig)) return true;
  bool composed = qa_mod_operation_active(qa_gameplay_authority_damage_operation(opts->services.engine->combat));
  if (!composed && b->armor_count == 0) return false;
  qa_owned_actor actor;
  if (!target_at_entry(b, &b->profile.calls.damage, b->damage_sig, &actor)) return false;
  return composed || find_armor(b, actor) != NULL;
}
/* `${module.id}:${suffix}` with no length limit; the runner copies the id it is given. */
static char *callback_name(qa_compat_q2_classic_combat_bindings *b, const char *suffix) {
  const char *module = mem_of(b)->module.id ? mem_of(b)->module.id : "";
  size_t need = strlen(module) + strlen(suffix) + 2; char *id = malloc(need); if (!id) qa_range_error("Out of memory");
  snprintf(id, need, "%s:%s", module, suffix);
  return id;
}
static void install_damage_hooks(qa_compat_q2_classic_combat_bindings *b) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_guest_address damage = entry_of(b, b->profile.entries.damage);
  uint64_t handle = opts->runner->observe_entry(opts->runner->ctx, damage, b, damage_observe);
  b->damage_hooks[b->damage_hook_count++] = handle;
  char *id = callback_name(b, "source-damage"); qa_cleanup node; qa_cleanup_push(&node, free, id);
  handle = opts->runner->bind_entry(opts->runner->ctx, damage, id, b->damage_sig, b, damage_invoke, b, damage_accepts);
  qa_cleanup_pop(&node); free(id);
  b->damage_hooks[b->damage_hook_count++] = handle;
}
static void armor_observe(void *vctx) {
  hook_ctx_t *h = vctx;
  if (h->b->has_bypass && h->b->bypass_channel == h->channel && h->b->bypass_pending) { h->b->bypass_stack = rsp_of(h->b); h->b->bypass_pending = false; }
}
typedef struct orig_ctx { qa_compat_q2_classic_combat_bindings *b; qa_protection_channel channel; qa_guest_address entry;
  qa_compat_q2_guest_call_signature signature; const qa_guest_call_value *args; size_t count; qa_raw_entity_view view; qa_guest_call_result result; } orig_ctx_t;
static void armor_original_body(void *vctx) {
  orig_ctx_t *o = vctx; const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(o->b->host);
  o->result = qa_compat_q2_classic_guest_host_invoke(o->b->host, o->entry, o->signature, o->args, o->count, &o->view, opts->instruction_budget);
  if (o->result.kind != QA_GUEST_VALUE_INT32) qa_raise("Error", 0, 0, "Classic armor stage returned a non-integer result");
}
static double armor_original(void *vctx) {
  orig_ctx_t *o = vctx; qa_compat_q2_classic_combat_bindings *b = o->b;
  bool prev_has = b->has_bypass; qa_protection_channel prev_ch = b->bypass_channel; bool prev_pending = b->bypass_pending; uint64_t prev_stack = b->bypass_stack;
  b->has_bypass = true; b->bypass_channel = o->channel; b->bypass_pending = true; b->bypass_stack = 0;
  qa_caught_error caught; bool completed = qa_try(armor_original_body, o, &caught);
  /* TS finally: the bypass of the enclosing original call is restored whatever the guest did. */
  b->has_bypass = prev_has; b->bypass_channel = prev_ch; b->bypass_pending = prev_pending; b->bypass_stack = prev_stack;
  if (!completed) qa_rethrow(&caught);
  return o->result.number_value;
}
static qa_guest_call_result armor_invoke(void *vctx, const qa_guest_call_context *context, const qa_guest_call_value *source_args, size_t source_count) {
  (void)context; hook_ctx_t *h = vctx; qa_compat_q2_classic_combat_bindings *b = h->b; const qa_protection_channel channel = h->channel;
  const qa_compat_q2_native_combat_call *call = channel == QA_PROTECTION_REGULAR ? &b->profile.calls.regular_armor : &b->profile.calls.power_armor;
  qa_compat_q2_native_combat_operation op = channel == QA_PROTECTION_REGULAR ? QA_Q2CO_REGULAR_ARMOR : QA_Q2CO_POWER_ARMOR;
  qa_guest_call_value args[16]; size_t n = qa_compat_q2_read_native_combat_arguments(call, op, source_args, source_count, 4, args);
  frame_t *frame = top_frame(b);
  qa_owned_actor actor; bool has_actor = qa_compat_q2_classic_edicts_observe(qa_compat_q2_classic_guest_host_get_edicts(b->host),
      qa_compat_q2_classic_required_pointer(args, n, 0), &actor);
  armor_entry_t *entry = has_actor ? find_armor(b, actor) : NULL;
  qa_armor_stage_intercept_fn intercept = entry ? (channel == QA_PROTECTION_REGULAR ? entry->regular : entry->powered) : NULL;
  void *intercept_ctx = entry ? (channel == QA_PROTECTION_REGULAR ? entry->regular_ctx : entry->powered_ctx) : NULL;
  if (!frame || !has_actor || !intercept || !qa_same_actor(frame->request.target, actor.id))
    qa_raise("Error", 0, 0, "Classic armor stage has no active source damage request");
  qa_guest_address entry_addr = entry_of(b, channel == QA_PROTECTION_REGULAR ? b->profile.entries.regular_armor : b->profile.entries.power_armor);
  orig_ctx_t o = {b, channel, entry_addr, channel == QA_PROTECTION_REGULAR ? b->regular_sig : b->power_sig, source_args, source_count, frame->view, {0}};
  qa_armor_stage_input input; memset(&input, 0, sizeof input);
  input.request = frame->request; input.amount = qa_compat_q2_classic_number(args, n, 3);
  input.direction = frame->request.direction;
  input.point = qa_compat_q2_read_classic_vector(mem_of(b), qa_compat_q2_classic_required_pointer(args, n, 1));
  input.normal = qa_compat_q2_read_classic_vector(mem_of(b), qa_compat_q2_classic_required_pointer(args, n, 2));
  input.flags = qa_compat_q2_native_armor_flags(qa_compat_q2_classic_number(args, n, channel == QA_PROTECTION_REGULAR ? 5 : 4));
  double saved = intercept(intercept_ctx, input, armor_original, &o);
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  /* TS `throw new RemovedNativeDamage(frame.request)`: the guest's T_Damage is abandoned and the damage() that owns
   * this request catches it (the frame is the request's identity). */
  if (!qa_session_actor_registry_is_live(opts->services.engine->actors, actor.id))
    qa_compat_q2_native_throw(frame, QA_COMPAT_Q2_REMOVED_NATIVE_DAMAGE_MESSAGE);
  return qa_compat_q2_call_int32(trunc(saved)); /* integer(): Math.trunc */
}
static bool armor_accepts(void *vctx) {
  hook_ctx_t *h = vctx; qa_compat_q2_classic_combat_bindings *b = h->b;
  if (b->has_bypass && b->bypass_channel == h->channel && !b->bypass_pending && b->bypass_stack == rsp_of(b)) return false;
  const qa_compat_q2_native_combat_call *call = h->channel == QA_PROTECTION_REGULAR ? &b->profile.calls.regular_armor : &b->profile.calls.power_armor;
  qa_compat_q2_guest_call_signature sig = h->channel == QA_PROTECTION_REGULAR ? b->regular_sig : b->power_sig;
  qa_owned_actor actor;
  if (!target_at_entry(b, call, sig, &actor)) return false;
  armor_entry_t *e = find_armor(b, actor);
  return e && (h->channel == QA_PROTECTION_REGULAR ? e->regular : e->powered) != NULL;
}
static void install_armor_hooks(qa_compat_q2_classic_combat_bindings *b) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  for (int ch = 0; ch < 2; ch++) {
    qa_protection_channel channel = ch == 0 ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED;
    qa_guest_address entry = entry_of(b, channel == QA_PROTECTION_REGULAR ? b->profile.entries.regular_armor : b->profile.entries.power_armor);
    hook_ctx_t *h = malloc(sizeof *h); if (!h) qa_range_error("Out of memory");
    *h = (hook_ctx_t){b, channel}; b->hook_ctx[ch] = h;
    uint64_t handle = opts->runner->observe_entry(opts->runner->ctx, entry, h, armor_observe);
    b->hooks[b->hook_count++] = handle;
    char *id = callback_name(b, channel == QA_PROTECTION_REGULAR ? "regular-armor" : "powered-armor");
    qa_cleanup node; qa_cleanup_push(&node, free, id);
    handle = opts->runner->bind_entry(opts->runner->ctx, entry, id, channel == QA_PROTECTION_REGULAR ? b->regular_sig : b->power_sig, h, armor_invoke, h, armor_accepts);
    qa_cleanup_pop(&node); free(id);
    b->hooks[b->hook_count++] = handle;
  }
}
static qa_damage_outcome incoming_source_damage(void *ctx, qa_damage_request request);
typedef struct incoming_ctx { qa_compat_q2_classic_combat_bindings *b; qa_raw_entity_view view; const qa_guest_call_value *args; size_t count; } incoming_ctx_t;
static void incoming_damage(qa_compat_q2_classic_combat_bindings *b, const qa_guest_call_value *source_args, size_t source_count) {
  qa_guest_call_value args[16]; size_t n = qa_compat_q2_read_native_combat_arguments(&b->profile.calls.damage, QA_Q2CO_DAMAGE, source_args, source_count, 4, args);
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  if (!opts->services.damage_provenance) qa_raise("Error", 0, 0, "Classic source damage requires captured provenance");
  qa_actor_id ids[3];
  bool stand_in = false;
  for (int i = 0; i < 3; i++) {
    if (opts->services.stand_in_actor) {
      qa_actor_id standing;
      if (opts->services.stand_in_actor(opts->services.stand_in_ctx, qa_compat_q2_classic_edicts_from_pointer(
              qa_compat_q2_classic_guest_host_get_edicts(b->host), qa_compat_q2_classic_required_pointer(args, n, (size_t)i)), &standing)) {
        ids[i] = standing;
        if (i == 0) stand_in = true;
        continue;
      }
    }
    qa_owned_actor owned;
    if (!qa_compat_q2_classic_edicts_observe(qa_compat_q2_classic_guest_host_get_edicts(b->host),
            qa_compat_q2_classic_required_pointer(args, n, (size_t)i), &owned))
      qa_raise("Error", 0, 0, "Classic damage references a retired source actor");
    ids[i] = owned.id;
  }
  double flags = qa_compat_q2_classic_number(args, n, 8);
  qa_q2_native_cause native; memset(&native, 0, sizeof native);
  native.edition = "classic"; native.game = b->profile.game; native.value = qa_compat_q2_classic_number(args, n, 9);
  double means; if (!qa_q2mp_canonical_cause_from_native(native, &means)) qa_raise("Error", 0, 0, "Unclassified classic damage cause");
  qa_damage_request req; memset(&req, 0, sizeof req);
  req.target = ids[0]; req.amount = qa_compat_q2_classic_number(args, n, 6); req.knockback = qa_compat_q2_classic_number(args, n, 7);
  req.direction = qa_compat_q2_read_classic_vector(mem_of(b), qa_compat_q2_classic_required_pointer(args, n, 3));
  req.point = qa_compat_q2_read_classic_vector(mem_of(b), qa_compat_q2_classic_required_pointer(args, n, 4));
  req.normal = qa_compat_q2_read_classic_vector(mem_of(b), qa_compat_q2_classic_required_pointer(args, n, 5));
  req.delivery = (qa_js_to_int32(flags) & 1) != 0 ? "radius" : "direct";
  qa_attack_provenance prov = opts->services.damage_provenance->provenance(opts->services.damage_provenance->ctx, ids[2], ids[1], ids[0]);
  /* The attacker and inflictor of this call live in this frame: a guest T_Damage that runs inside this one (a death
   * that damages something else) has its own, where one static pair was overwritten under the outer request. */
  qa_actor_id attacker = ids[2], inflictor = ids[1];
  prov.attacker = &attacker; prov.inflictor = &inflictor;
  prov.cause.kind = QA_ATTACK_CAUSE_Q2; prov.cause.means_of_death = means; prov.cause.damage_flags = flags;
  prov.cause.has_native = true; prov.cause.native = native;
  req.attack = prov;
  incoming_ctx_t source = {b, qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(b->host),
      qa_compat_q2_classic_required_pointer(args, n, 0)), source_args, source_count};
  /* A stand-in's health belongs to the shared monster: shared combat applies the hit and the guest's own T_Damage does not run on it. */
  (void)qa_gameplay_authority_apply(opts->services.engine->combat, req, stand_in ? NULL : incoming_source_damage, stand_in ? NULL : &source);
}
/* TS releaseRetiredProxies: stand-ins of actors that are no longer live are freed, if the guest still holds them. */
static void release_retired_proxies(qa_compat_q2_classic_combat_bindings *b) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_compat_q2_memory *memory = mem_of(b);
  static const qa_guest_value_layout pp[] = {{false,"pointer",{0}}};
  for (size_t i = 0; i < b->proxy_count;) {
    if (qa_session_actor_registry_is_live(opts->services.engine->actors, b->proxies[i].actor)) { i++; continue; }
    const qa_raw_entity_view view = b->proxies[i].view;
    memmove(&b->proxies[i], &b->proxies[i + 1], (b->proxy_count - i - 1) * sizeof *b->proxies);
    b->proxy_count--;
    if (qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, view.address, 88)) != 0) {
      qa_guest_call_value v = qa_compat_q2_call_pointer(true, view.address);
      (void)qa_compat_q2_classic_guest_host_invoke(b->host, entry_of(b, b->profile.entries.free),
          qa_compat_q2_classic_signature(pp, 1, true, qa_compat_q2_q2_int, false), &v, 1, NULL, opts->instruction_budget);
    }
  }
}
typedef struct exec_ctx exec_ctx_t;
typedef struct rob { exec_ctx_t *ex; const char *reaction; qa_compat_q2_native_combat_call call; qa_compat_q2_guest_call_signature sig; qa_guest_value_layout *storage; } rob_t;
struct exec_ctx {
  qa_compat_q2_classic_combat_bindings *b; qa_raw_entity_view view; const qa_guest_call_value *source_args; size_t source_count;
  qa_source_damage_observer observer; qa_damage_request request;
  qa_source_damage_result result; bool reacting; double health; qa_vec3 velocity; qa_armor_state armor; frame_t *frame;
  uint64_t writes[3]; size_t write_count;                      /* TS writes (CleanupStack) */
  uint64_t reactions[2]; size_t reaction_count; rob_t robs[2]; /* TS reactions (CleanupStack) */
  qa_guest_address vectors; qa_guest_call_value *lowered; size_t lowered_count; void *saved_processor;
};
static bool exec_current(exec_ctx_t *e) { return top_frame(e->b) == e->frame && !e->reacting; }
/* TS stopWrites(): writes.runAll(). */
static void stop_writes(exec_ctx_t *ex) {
  qa_compat_q2_memory *memory = mem_of(ex->b); size_t count = ex->write_count; ex->write_count = 0;
  for (size_t i = 0; i < count; i++) memory->unobserve_writes(memory->ctx, ex->writes[i]);
}
static void obs_health(void *vctx) {
  exec_ctx_t *ex = vctx; qa_compat_q2_classic_combat_bindings *b = ex->b;
  double before = ex->health; ex->health = qa_compat_q2_mem_read_i32(mem_of(b), at_view(b, ex->view, b->profile.fields.health));
  if (exec_current(ex) && before != ex->health) {
    qa_damage_mutation m; memset(&m, 0, sizeof m); m.kind = QA_DAMAGE_MUTATION_HEALTH; m.health_before = before; m.health_after = ex->health;
    ex->observer.stored(ex->observer.ctx, m); ex->result.applied_damage = ex->result.applied_damage + before - ex->health;
  }
}
static void obs_velocity(void *vctx) {
  exec_ctx_t *ex = vctx; qa_compat_q2_classic_combat_bindings *b = ex->b;
  qa_vec3 before = ex->velocity; ex->velocity = qa_compat_q2_read_classic_vector(mem_of(b), at_view(b, ex->view, b->profile.fields.velocity));
  if (exec_current(ex)) {
    qa_damage_mutation m; memset(&m, 0, sizeof m); m.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY; m.velocity_before = before; m.velocity_after = ex->velocity;
    m.movement_provider = ex->request.attack.movement_provider;
    ex->observer.stored(ex->observer.ctx, m);
  }
}
static void obs_armor(void *vctx) {
  exec_ctx_t *ex = vctx; qa_compat_q2_classic_combat_bindings *b = ex->b;
  qa_armor_state before = ex->armor; ex->armor = armor_of(b, ex->view);
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_owned_actor target; bool resolved = qa_session_actor_registry_resolve_owned(opts->services.engine->actors, ex->request.target, &target);
  if (!exec_current(ex) || !resolved) return;
  armor_entry_t *stages = find_armor(b, target);
  bool regular_stage = stages && stages->regular, powered_stage = stages && stages->powered;
  qa_combat_state_result effective = qa_gameplay_authority_read(opts->services.engine->combat, ex->request.target);
  if (!effective.has_value) return;
  qa_armor_state prior = {!regular_stage ? before.regular : effective.value.armor.regular,
                          !powered_stage ? before.powered : effective.value.armor.powered};
  if (armor_eq(prior, effective.value.armor)) return;
  qa_damage_mutation m; memset(&m, 0, sizeof m); m.kind = QA_DAMAGE_MUTATION_ARMOR; m.armor_before = prior; m.armor_after = effective.value.armor;
  ex->observer.stored(ex->observer.ctx, m);
}
static void reaction_observe(void *vctx) {
  rob_t *r = vctx; exec_ctx_t *ex = r->ex; qa_compat_q2_classic_combat_bindings *b = ex->b;
  if (!exec_current(ex)) return;
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_guest_call_value target = opts->runner->read_combat_field(opts->runner->ctx, &r->call, r->sig, "target");
  qa_guest_address a; if (!qa_compat_q2_classic_pointer(&target, 1, 0, &a) || a.byte_offset != ex->view.address.byte_offset) return;
  ex->reacting = true;
  qa_guest_call_value amount = opts->runner->read_combat_field(opts->runner->ctx, &r->call, r->sig, "amount");
  ex->result = (qa_source_damage_result){qa_compat_q2_classic_number(&amount, 1, 0), r->reaction};
  stop_writes(ex);
  ex->observer.before_reaction(ex->observer.ctx, ex->result);
}
/* TS address(actor): the edict a damage argument names; a foreign actor gets its kept stand-in edict. */
static qa_guest_address damage_address(exec_ctx_t *ex, const qa_actor_id *actor) {
  qa_compat_q2_classic_combat_bindings *b = ex->b; qa_compat_q2_memory *memory = mem_of(b);
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_q2_foundation_host *engine = opts->services.engine;
  if (!actor) return qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(b->host), 0).address;
  qa_actor_source_result src = qa_session_actor_registry_source_of(engine->actors, *actor);
  if (src.has_value && src.provider && memory->module.id && strcmp(src.provider, memory->module.id) == 0)
    return qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(b->host), (double)src.slot).address;
  qa_body_state_result body = qa_shared_body_table_read(engine->bodies, *actor);
  if (!body.has_value) qa_raise("Error", 0, 0, "Foreign damage actor has no body to project");
  size_t at = b->proxy_count;
  for (size_t k = 0; k < b->proxy_count; k++) if (qa_same_actor(b->proxies[k].actor, *actor)) { at = k; break; }
  if (at == b->proxy_count || qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, b->proxies[at].view.address, 88)) == 0) {
    qa_guest_call_result allocated = qa_compat_q2_classic_guest_host_invoke(b->host, entry_of(b, b->profile.entries.spawn),
        qa_compat_q2_classic_signature(NULL, 0, false, qa_compat_q2_q2_pointer, false), NULL, 0, NULL, opts->instruction_budget);
    if (allocated.kind != QA_GUEST_VALUE_POINTER || !allocated.pointer_value.has_value) qa_raise("Error", 0, 0, "Source G_Spawn returned no damage projection");
    qa_raw_entity_view proxy = qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(b->host), allocated.pointer_value.value);
    /* damageProxies.set(actor, proxy): the guest call may have changed the table, so the actor is looked up again. */
    at = b->proxy_count;
    for (size_t k = 0; k < b->proxy_count; k++) if (qa_same_actor(b->proxies[k].actor, *actor)) { at = k; break; }
    if (at == b->proxy_count) {
      if (b->proxy_count == b->proxy_cap) {
        size_t cap = b->proxy_cap ? b->proxy_cap * 2 : 8; void *grown = realloc(b->proxies, cap * sizeof *b->proxies);
        if (!grown) qa_range_error("Out of memory");
        b->proxies = grown; b->proxy_cap = cap;
      }
      b->proxy_count++;
      b->proxies[at].actor = *actor;
    }
    b->proxies[at].view = proxy;
  }
  qa_guest_address value = b->proxies[at].view.address;
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, value, 4), body.value.origin);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, value, 16), body.value.angles);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, value, 188), body.value.bounds.min);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, value, 200), body.value.bounds.max);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, value, (int64_t)b->profile.fields.velocity), body.value.velocity);
  return value;
}
static void damage_invoke_body(void *vctx) {
  exec_ctx_t *ex = vctx; qa_compat_q2_classic_combat_bindings *b = ex->b;
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  (void)qa_compat_q2_classic_guest_host_invoke(b->host, entry_of(b, b->profile.entries.damage), b->damage_sig, ex->lowered, ex->lowered_count, &ex->view, opts->instruction_budget);
}
/* The TS try block of damage(). */
static void damage_body(void *vctx) {
  exec_ctx_t *ex = vctx; qa_compat_q2_classic_combat_bindings *b = ex->b; qa_raw_entity_view view = ex->view; qa_damage_request request = ex->request;
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_compat_q2_memory *memory = mem_of(b); uint64_t handle;
  /* Write observers when the memory mirror supports them. */
  if (memory->observe_writes) {
    handle = memory->observe_writes(memory->ctx, at_view(b, view, b->profile.fields.health), 4, ex, obs_health); ex->writes[ex->write_count++] = handle;
    handle = memory->observe_writes(memory->ctx, at_view(b, view, b->profile.fields.velocity), 12, ex, obs_velocity); ex->writes[ex->write_count++] = handle;
    qa_guest_address client;
    if (client_of(b, view, &client)) {
      handle = memory->observe_writes(memory->ctx, qa_compat_q2_mem_offset(memory, client, (int64_t)b->profile.client.inventory), b->profile.client.inventory_count * 4, ex, obs_armor);
      ex->writes[ex->write_count++] = handle;
    }
  }
  for (int r = 0; r < 2; r++) {
    qa_guest_address callback;
    if (!qa_compat_q2_mem_read_pointer(memory, at_view(b, view, r == 0 ? b->profile.fields.pain : b->profile.fields.die), &callback)) continue;
    rob_t *rob = &ex->robs[r]; rob->ex = ex; rob->reaction = r == 0 ? "pain" : "death";
    rob->call = r == 0 ? b->profile.calls.pain : b->profile.calls.death;
    rob->storage = calloc(rob->call.argument_count ? rob->call.argument_count : 1, sizeof *rob->storage); if (!rob->storage) qa_range_error("Out of memory");
    rob->sig = qa_compat_q2_native_combat_signature(&rob->call, r == 0 ? QA_Q2CO_PAIN : QA_Q2CO_DEATH, qa_compat_q2_classic_abi, rob->storage);
    handle = opts->runner->observe_entry(opts->runner->ctx, callback, rob, reaction_observe); ex->reactions[ex->reaction_count++] = handle;
  }
  const qa_vec3 vecs[] = {request.direction, request.point, request.normal};
  for (int i = 0; i < 3; i++) qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, ex->vectors, i * 12), vecs[i]);
  qa_q2mp_native_cause_profile prof = {false, b->profile.game, false};
  qa_compat_q2_native_damage_arguments_result lowered = qa_compat_q2_native_damage_arguments(request, prof);
  if (!lowered.native.edition || strcmp(lowered.native.edition, "classic") != 0) qa_raise("Error", 0, 0, "Missing declared classic damage cause");
  qa_guest_address attacker = damage_address(ex, request.attack.attacker);
  qa_guest_address inflictor = request.attack.inflictor && request.attack.attacker && qa_same_actor(*request.attack.inflictor, *request.attack.attacker)
      ? attacker : damage_address(ex, request.attack.inflictor);
  ex->saved_processor = opts->runner->capture_processor ? opts->runner->capture_processor(opts->runner->ctx) : NULL;
  double depth = qa_compat_q2_native_depth(opts->runner);
  /* integer(): Math.trunc */
  qa_guest_call_value semantic[10] = {
    qa_compat_q2_call_pointer(true, view.address), qa_compat_q2_call_pointer(true, inflictor), qa_compat_q2_call_pointer(true, attacker),
    qa_compat_q2_call_pointer(true, ex->vectors), qa_compat_q2_call_pointer(true, qa_compat_q2_mem_offset(memory, ex->vectors, 12)),
    qa_compat_q2_call_pointer(true, qa_compat_q2_mem_offset(memory, ex->vectors, 24)),
    qa_compat_q2_call_int32(trunc(request.amount)), qa_compat_q2_call_int32(trunc(request.knockback)),
    qa_compat_q2_call_int32(trunc(lowered.damage_flags)), qa_compat_q2_call_int32(trunc(lowered.native.value))};
  size_t capacity = b->profile.calls.damage.argument_count ? b->profile.calls.damage.argument_count : 1;
  ex->lowered = calloc(capacity, sizeof *ex->lowered); if (!ex->lowered) qa_range_error("Out of memory");
  ex->lowered_count = qa_compat_q2_lower_native_combat_arguments(&b->profile.calls.damage, QA_Q2CO_DAMAGE, semantic, 10, memory,
                                                                 true, b->image, ex->source_args, ex->source_count, ex->lowered);
  qa_compat_q2_native_thrown thrown;
  if (!qa_compat_q2_native_try(damage_invoke_body, ex, &thrown)) {
    /* TS catch: only RemovedNativeDamage for this request ends here, with the processor put back. */
    if (thrown.token != ex->frame) qa_compat_q2_native_rethrow(&thrown);
    qa_compat_q2_native_unwind(opts->runner, depth);
    if (ex->saved_processor && opts->runner->restore_processor) opts->runner->restore_processor(opts->runner->ctx, ex->saved_processor);
  }
  /* Without write observers, diff health/velocity after the call. */
  if (!memory->observe_writes && exec_current(ex)) {
    double after_h = qa_compat_q2_mem_read_i32(memory, at_view(b, view, b->profile.fields.health));
    if (after_h != ex->health) { qa_damage_mutation m; memset(&m, 0, sizeof m); m.kind = QA_DAMAGE_MUTATION_HEALTH; m.health_before = ex->health; m.health_after = after_h;
      ex->observer.stored(ex->observer.ctx, m); ex->result.applied_damage += ex->health - after_h; ex->health = after_h; }
    qa_vec3 after_v = qa_compat_q2_read_classic_vector(memory, at_view(b, view, b->profile.fields.velocity));
    qa_damage_mutation m; memset(&m, 0, sizeof m); m.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY; m.velocity_before = ex->velocity; m.velocity_after = after_v; m.movement_provider = request.attack.movement_provider;
    ex->observer.stored(ex->observer.ctx, m); ex->velocity = after_v;
  }
}
static qa_source_damage_result damage_execute(void *vctx, qa_source_damage_observer observer, qa_damage_request request) {
  exec_ctx_t *ex = vctx; qa_compat_q2_classic_combat_bindings *b = ex->b; qa_raw_entity_view view = ex->view;
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  qa_compat_q2_memory *memory = mem_of(b);
  frame_t *frame = malloc(sizeof *frame); if (!frame) qa_range_error("Out of memory");
  *frame = (frame_t){request, view, false, 0};
  if (b->frame_count == b->frame_cap) {
    size_t cap = b->frame_cap ? b->frame_cap * 2 : 8; frame_t **grown = realloc(b->frames, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    b->frames = grown; b->frame_cap = cap;
  }
  b->frames[b->frame_count++] = frame;
  ex->frame = frame; ex->observer = observer; ex->request = request;
  ex->write_count = 0; ex->reaction_count = 0; ex->lowered = NULL; ex->lowered_count = 0; ex->saved_processor = NULL;
  memset(ex->robs, 0, sizeof ex->robs);
  /* As in TS, a raise before the try leaves the frame pushed. */
  ex->health = qa_compat_q2_mem_read_i32(memory, at_view(b, view, b->profile.fields.health));
  ex->armor = armor_of(b, view);
  ex->velocity = qa_compat_q2_read_classic_vector(memory, at_view(b, view, b->profile.fields.velocity));
  ex->result = (qa_source_damage_result){0, "none"}; ex->reacting = false;
  release_retired_proxies(b);
  ex->vectors = memory->allocate(memory->ctx, 36, true, 4, "Q2 classic damage arguments");
  qa_caught_error caught; bool completed = qa_try(damage_body, ex, &caught);
  /* TS finally. Host storage first: nothing below needs it. */
  if (ex->lowered && !ex->source_args)
    for (size_t i = 0; i < b->profile.calls.damage.argument_count; i++) {
      const qa_compat_q2_native_combat_argument *a = &b->profile.calls.damage.arguments[i];
      if (a->kind == QA_Q2CA_VALUE && a->layout.is_aggregate) free((void *)ex->lowered[i].bytes.bytes); /* decodeValue's copy */
    }
  free(ex->lowered); ex->lowered = NULL;
  if (ex->saved_processor && opts->runner->free_processor) opts->runner->free_processor(opts->runner->ctx, ex->saved_processor);
  ex->saved_processor = NULL;
  stop_writes(ex);
  size_t reaction_count = ex->reaction_count; ex->reaction_count = 0;
  for (size_t i = 0; i < reaction_count; i++) opts->runner->remove_entry(opts->runner->ctx, ex->reactions[i]);
  for (int r = 0; r < 2; r++) { free(ex->robs[r].storage); ex->robs[r].storage = NULL; }
  memory->unmap(memory->ctx, ex->vectors, 36);
  if (b->frame_count > 0) free(b->frames[--b->frame_count]); /* frames.pop() */
  qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(b->host));
  if (!completed) qa_rethrow(&caught);
  return ex->result;
}
static qa_damage_outcome do_damage(qa_compat_q2_classic_combat_bindings *b, qa_damage_request input, qa_raw_entity_view view,
                                   const qa_guest_call_value *source_args, size_t source_count) {
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  exec_ctx_t ex; memset(&ex, 0, sizeof ex); ex.b = b; ex.view = view; ex.source_args = source_args; ex.source_count = source_count;
  return qa_gameplay_authority_run_source_damage(opts->services.engine->combat, input, damage_execute, &ex);
}
static qa_damage_outcome incoming_source_damage(void *ctx, qa_damage_request request) {
  incoming_ctx_t *source = ctx; return do_damage(source->b, request, source->view, source->args, source->count);
}
static void proxies_reloaded(void *ctx) { ((qa_compat_q2_classic_combat_bindings *)ctx)->proxy_count = 0; }
static qa_guest_value_layout *signature_storage(const qa_compat_q2_native_combat_call *call) {
  qa_guest_value_layout *storage = calloc(call->argument_count ? call->argument_count : 1, sizeof *storage);
  if (!storage) qa_range_error("Out of memory");
  return storage;
}
static void create_body(void *vctx) {
  qa_compat_q2_classic_combat_bindings *b = vctx;
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  b->sig_storage[0] = signature_storage(&b->profile.calls.damage);
  b->damage_sig = qa_compat_q2_native_combat_signature(&b->profile.calls.damage, QA_Q2CO_DAMAGE, qa_compat_q2_classic_abi, b->sig_storage[0]);
  b->sig_storage[1] = signature_storage(&b->profile.calls.regular_armor);
  b->regular_sig = qa_compat_q2_native_combat_signature(&b->profile.calls.regular_armor, QA_Q2CO_REGULAR_ARMOR, qa_compat_q2_classic_abi, b->sig_storage[1]);
  b->sig_storage[2] = signature_storage(&b->profile.calls.power_armor);
  b->power_sig = qa_compat_q2_native_combat_signature(&b->profile.calls.power_armor, QA_Q2CO_POWER_ARMOR, qa_compat_q2_classic_abi, b->sig_storage[2]);
  b->reload_handle = qa_compat_q2_classic_guest_host_observe_reload(b->host, proxies_reloaded, b); b->reload_observed = true;
  if (opts->services.damage_provenance) install_damage_hooks(b);
}
qa_compat_q2_classic_combat_bindings *qa_compat_q2_classic_combat_bindings_create(
    qa_compat_q2_classic_guest_host *host, qa_guest_address image,
    const qa_compat_q2_classic_combat_profile *declared, bool has_declared) {
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(host);
  const qa_compat_q2_classic_combat_profile *profile = has_declared || declared ? declared : qa_compat_q2_classic_get_combat_profile(memory->module.digest);
  if (!profile) return NULL;
  if (!profile->digest || !memory->module.digest || strcmp(profile->digest, memory->module.digest) != 0)
    qa_raise("Error", 0, 0, "Classic combat profile belongs to another original artifact");
  qa_compat_q2_validate_classic_combat_profile(profile);
  const double entries[] = {profile->entries.damage, profile->entries.power_armor, profile->entries.regular_armor, profile->entries.spawn, profile->entries.free};
  for (size_t i = 0; i < 5; i++) memory->check(memory->ctx, qa_compat_q2_mem_offset(memory, image, (int64_t)entries[i]), 1, "execute");
  qa_compat_q2_classic_combat_bindings *b = calloc(1, sizeof *b); if (!b) qa_range_error("Out of memory");
  b->host = host; b->image = image; b->profile = *profile;
  /* TS: a constructor that throws leaves nothing behind, and a failed installDamageHooks closes the binding. */
  qa_caught_error caught;
  if (!qa_try(create_body, b, &caught)) { qa_compat_q2_classic_combat_bindings_destroy(b); qa_rethrow(&caught); }
  return b;
}
void qa_compat_q2_classic_combat_bindings_close(qa_compat_q2_classic_combat_bindings *b) {
  if (!b || b->closed) return;
  b->closed = true;
  if (b->reload_observed) qa_compat_q2_classic_guest_host_unobserve_reload(b->host, b->reload_handle);
  b->proxy_count = 0;
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(b->host);
  size_t damage_hook_count = b->damage_hook_count; b->damage_hook_count = 0;
  for (size_t i = 0; i < damage_hook_count; i++) opts->runner->remove_entry(opts->runner->ctx, b->damage_hooks[i]);
  remove_armor_hooks(b);
  b->armor_count = 0;
}
void qa_compat_q2_classic_combat_bindings_destroy(qa_compat_q2_classic_combat_bindings *b) {
  if (!b) return;
  qa_compat_q2_classic_combat_bindings_close(b);
  for (size_t i = 0; i < b->context_count; i++) { free(b->contexts[i]->team); free(b->contexts[i]); }
  for (size_t i = 0; i < b->owned_count; i++) free(b->owned[i]);
  for (size_t i = 0; i < b->frame_count; i++) free(b->frames[i]);
  for (int i = 0; i < 3; i++) free(b->sig_storage[i]);
  free(b->frames); free(b->armor);
  free(b->contexts); free(b->owned); free(b->proxies); free(b);
}
