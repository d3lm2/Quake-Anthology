/* Port of src/app/bootstrap/simulation/arsenal/q3.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/app/bootstrap/simulation/arsenal_intent.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/persistence/save_image.h"

typedef struct player_arsenal {
  qa_owned_actor actor;
  qa_arsenal_state arsenal;
  qa_q3_arsenal_runtime_state runtime;
  double torso_animation;
  bool has_last_fire;
  double last_fire_milliseconds;
  qa_item_id pending_use;
  bool arsenal_read_order; /* the restored arsenal's key order (qa_app_q3_selected_arsenal_checkpoint) */
} player_arsenal;
struct qa_app_q3_selected_arsenal {
  qa_app_q3_selected_arsenal_options options;
  player_arsenal **players; /* TS players Map: one allocation per player, so a step's player stays put */
  size_t player_count, player_capacity;
  /* Players removed while a step runs (that step still holds one, as TS keeps the object):
   * freed when the outermost step ends. */
  player_arsenal **retired; size_t retired_count;
  size_t step_depth;
  char **strings; size_t string_count; /* interned pending holdable item ids */
  /* ui()'s weapon status labels, whole, one per distinct weapon item and kept until destroy (a nested ui() read
   * cannot release a label an outer read holds). */
  char **label_items, **labels; size_t label_count;
  struct handoff_ctx **handoffs; size_t handoff_count; /* handoff() closure state, one record per actor slot */
};

static bool item_eq(qa_item_id a, qa_item_id b) { return a && b && strcmp(a, b) == 0; }
static bool missionpack(qa_app_q3_selected_arsenal *arsenal) {
  return arsenal->options.product && strcmp(arsenal->options.product, "missionpack") == 0;
}
static qa_q3_base_product product_of(const char *product) {
  return product && strcmp(product, "missionpack") == 0 ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}
static player_arsenal *find_player(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  for (size_t i = 0; i < arsenal->player_count; i++) if (qa_same_actor(arsenal->players[i]->actor.id, actor)) return arsenal->players[i];
  return NULL;
}
static player_arsenal *require_player(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  player_arsenal *player = find_player(arsenal, actor);
  if (!player) qa_raise("Error", 0, 0, "Actor has no selected Q3 arsenal");
  return player;
}
/* players.set(actor, ...) for a new actor: a zeroed record. */
static player_arsenal *add_player(qa_app_q3_selected_arsenal *arsenal) {
  if (arsenal->player_count == arsenal->player_capacity) {
    size_t capacity = arsenal->player_capacity ? arsenal->player_capacity * 2 : 16;
    player_arsenal **grown = realloc(arsenal->players, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    arsenal->players = grown; arsenal->player_capacity = capacity;
  }
  player_arsenal *player = calloc(1, sizeof *player);
  if (!player) qa_range_error("Out of memory");
  arsenal->players[arsenal->player_count++] = player;
  return player;
}
/* A held item id kept by the arsenal (TS strings need no owner): equal texts share one copy. */
static qa_item_id intern(qa_app_q3_selected_arsenal *arsenal, const char *text) {
  if (text == NULL) return NULL;
  for (size_t i = 0; i < arsenal->string_count; i++) if (strcmp(arsenal->strings[i], text) == 0) return arsenal->strings[i];
  char **grown = realloc(arsenal->strings, (arsenal->string_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  arsenal->strings = grown;
  char *copy = strdup(text);
  if (!copy) qa_range_error("Out of memory");
  arsenal->strings[arsenal->string_count++] = copy;
  return copy;
}
static bool in_inventory_items(qa_app_q3_selected_arsenal *arsenal, qa_item_id item) {
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
    if (!missionpack(arsenal) && qa_q3_weapon_items[i].weapon > 10) continue;
    if (item_eq(qa_q3_weapon_items[i].item, item)) return true;
    if (qa_q3_weapon_items[i].ammo && item_eq(qa_q3_weapon_items[i].ammo, item)) return true;
  }
  return false;
}
static const qa_q3_weapon_item *find_weapon(qa_item_id item) {
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) if (item_eq(qa_q3_weapon_items[i].item, item)) return &qa_q3_weapon_items[i];
  return NULL;
}
static qa_q3_arsenal_controls resolve_controls(qa_app_q3_selected_arsenal *arsenal, qa_arsenal_state state,
    const qa_arsenal_intent *intent, qa_user_command command) {
  if (arsenal->options.resolve_controls)
    return arsenal->options.resolve_controls(arsenal->options.ctx, state, intent, command, arsenal->options.product);
  return qa_app_sim_resolve_q3_arsenal_controls(state, intent, command, arsenal->options.product);
}

qa_app_q3_selected_arsenal *qa_app_q3_selected_arsenal_create(qa_app_q3_selected_arsenal_options options) {
  qa_app_q3_selected_arsenal *arsenal = calloc(1, sizeof *arsenal);
  if (!arsenal) return NULL;
  arsenal->options = options;
  return arsenal;
}
void qa_app_q3_selected_arsenal_destroy(qa_app_q3_selected_arsenal *arsenal) {
  if (!arsenal) return;
  for (size_t i = 0; i < arsenal->player_count; i++) free(arsenal->players[i]);
  for (size_t i = 0; i < arsenal->retired_count; i++) free(arsenal->retired[i]);
  for (size_t i = 0; i < arsenal->string_count; i++) free(arsenal->strings[i]);
  for (size_t i = 0; i < arsenal->handoff_count; i++) free(arsenal->handoffs[i]);
  free(arsenal->players); free(arsenal->retired); free(arsenal->strings); free(arsenal->handoffs);
  for (size_t i = 0; i < arsenal->label_count; i++) { free(arsenal->label_items[i]); free(arsenal->labels[i]); }
  free(arsenal->label_items); free(arsenal->labels); free(arsenal);
}
size_t qa_app_q3_selected_arsenal_catalog(qa_app_q3_selected_arsenal *arsenal, qa_selected_pickup_weapon *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && count < capacity; i++) {
    if (!missionpack(arsenal) && qa_q3_weapon_items[i].weapon > 10) continue;
    out[count].item = qa_q3_weapon_items[i].item; out[count].ammo = qa_q3_weapon_items[i].ammo;
    out[count].drop = (item_eq(qa_q3_weapon_items[i].item, "q3:weapon/gauntlet") || item_eq(qa_q3_weapon_items[i].item, "q3:weapon/grapple")) ? "none" : "supply";
    count++;
  }
  return count;
}
bool qa_app_q3_selected_arsenal_has(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) { return find_player(arsenal, actor) != NULL; }

qa_arsenal_state qa_app_q3_selected_arsenal_read(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  player_arsenal *player = require_player(arsenal, actor);
  qa_arsenal_state state = player->arsenal;
  size_t entry_count = 0;
  const qa_inventory_entry *entries = arsenal->options.inventory.entries(arsenal->options.inventory.ctx, actor, &entry_count);
  qa_inventory_entry *ammo = malloc((entry_count ? entry_count : 1) * sizeof *ammo);
  if (!ammo) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < entry_count; i++) if (in_inventory_items(arsenal, entries[i].item)) ammo[count++] = entries[i];
  state.ammo = ammo; state.ammo_count = count;
  return state;
}
qa_arsenal_state qa_app_q3_selected_arsenal_admit(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    double max_health, bool team_deathmatch) {
  if (find_player(arsenal, actor.id)) qa_raise("Error", 0, 0, "Selected Q3 arsenal already admitted");
  qa_arsenal_state defaults = arsenal->options.has_supply ? arsenal->options.supply_loadout
      : qa_q3_spawn_loadout(arsenal->options.provider, arsenal->options.product, team_deathmatch);
  qa_arsenal_state state = arsenal->options.loadout ? arsenal->options.loadout(arsenal->options.ctx, actor, defaults) : defaults;
  if (!state.provider || !arsenal->options.provider || strcmp(state.provider, arsenal->options.provider) != 0
      || !state.state.kind || strcmp(state.state.kind, "q3") != 0)
    qa_raise("Error", 0, 0, "Selected Q3 starter belongs to a different arsenal");
  size_t entry_count = 0;
  const qa_inventory_entry *entries = arsenal->options.inventory.entries(arsenal->options.inventory.ctx, actor.id, &entry_count);
  for (size_t i = 0; i < entry_count; i++) {
    for (size_t j = 0; j < arsenal->options.supply_replaced_item_count; j++) {
      if (item_eq(entries[i].item, arsenal->options.supply_replaced_items[j])) {
        qa_inventory_entry cleared = entries[i]; cleared.count = 0;
        arsenal->options.inventory.configure(arsenal->options.inventory.ctx, actor, cleared);
      }
    }
  }
  for (size_t i = 0; i < state.ammo_count; i++) arsenal->options.inventory.configure(arsenal->options.inventory.ctx, actor, state.ammo[i]);
  player_arsenal *player = add_player(arsenal);
  /* The player keeps no ammo array (read() rebuilds it from the inventory); the spawn loadout this
   * admission built is released once configured. */
  if (!arsenal->options.has_supply) free((void *)defaults.ammo);
  player->actor = actor; player->arsenal = state; player->arsenal.ammo = NULL; player->arsenal.ammo_count = 0;
  player->runtime = qa_q3_spawn_arsenal_runtime(arsenal->options.product, max_health, 0);
  player->torso_animation = qa_q3_spawn_animation().torso;
  return qa_app_q3_selected_arsenal_read(arsenal, actor.id);
}
/* setSpawnWeapon (arsenal/q3.ts 94-99). */
void qa_app_q3_selected_arsenal_set_spawn_weapon(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor, double weapon) {
  player_arsenal *player = require_player(arsenal, actor);
  const qa_q3_weapon_item *entry = NULL;
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && entry == NULL; i++) if (qa_q3_weapon_items[i].weapon == weapon) entry = &qa_q3_weapon_items[i];
  if (entry == NULL || player->arsenal.state.kind == NULL || strcmp(player->arsenal.state.kind, "q3") != 0)
    qa_raise("Error", 0, 0, "Spawn weapon is not a Q3 weapon");
  player->arsenal.active_weapon = entry->item;
  player->arsenal.state.source_weapon = weapon;
  player->runtime.has_requested_weapon = false;
}
bool qa_app_q3_selected_arsenal_select(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item) {
  player_arsenal *player = require_player(arsenal, actor);
  const qa_q3_weapon_item *weapon = find_weapon(item);
  if (!weapon || (!missionpack(arsenal) && weapon->weapon > 10)) return false;
  if (arsenal->options.inventory.count(arsenal->options.inventory.ctx, actor, item) <= 0) return false;
  player->runtime = qa_q3_request_weapon(player->runtime, weapon->weapon);
  return true;
}
static double held_item(qa_app_q3_selected_arsenal *arsenal, player_arsenal *player) {
  return arsenal->options.has_equipment ? arsenal->options.equipment.read(arsenal->options.equipment.ctx, player->actor).holdable_item
      : player->runtime.holdable_item;
}
bool qa_app_q3_selected_arsenal_owns_holdable_input(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  player_arsenal *player = require_player(arsenal, actor);
  bool owns = !(arsenal->options.has_equipment && arsenal->options.equipment.has_owns_holdables && !arsenal->options.equipment.owns_holdables);
  return owns && (player->runtime.use_item_held || held_item(arsenal, player) != 0);
}
/* `q3:${item.classname}` for a held holdable, interned (a pending use keeps it across frames). */
static qa_item_id holdable_item_id(qa_app_q3_selected_arsenal *arsenal, double held) {
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(product_of(arsenal->options.product), held);
  if (!item || item->type != QA_Q3_BASE_IT_HOLDABLE) return NULL;
  const char *name = item->class_name ? item->class_name : "";
  size_t length = strlen(name) + 4;
  char *text = malloc(length);
  if (!text) qa_range_error("Out of memory");
  snprintf(text, length, "q3:%s", name);
  qa_item_id kept = intern(arsenal, text);
  free(text);
  return kept;
}
void qa_app_q3_selected_arsenal_observe_holdable_input(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor,
    qa_user_command command, const qa_arsenal_intent *intent) {
  if (arsenal->options.has_equipment && arsenal->options.equipment.has_owns_holdables && !arsenal->options.equipment.owns_holdables) return;
  player_arsenal *player = require_player(arsenal, actor);
  qa_arsenal_state state = qa_app_q3_selected_arsenal_read(arsenal, actor);
  qa_q3_arsenal_controls controls = resolve_controls(arsenal, state, intent, command);
  free((void *)state.ammo);
  if (!controls.use_holdable) {
    player->runtime.use_item_held = false;
    player->runtime.respawned = controls.attack && player->runtime.respawned;
    return;
  }
  if (player->runtime.use_item_held) return;
  qa_item_id item = holdable_item_id(arsenal, held_item(arsenal, player));
  if (item) player->pending_use = item;
}
bool qa_app_q3_selected_arsenal_use_item(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item) {
  if (arsenal->options.has_equipment && arsenal->options.equipment.has_owns_holdables && !arsenal->options.equipment.owns_holdables) return false;
  player_arsenal *player = require_player(arsenal, actor);
  qa_item_id held = holdable_item_id(arsenal, held_item(arsenal, player));
  if (!held || !item_eq(held, item)) return false;
  player->pending_use = held; /* the arsenal's copy of the caller's equal item id */
  return true;
}
qa_item_id qa_app_q3_selected_arsenal_pending_weapon(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  player_arsenal *player = require_player(arsenal, actor);
  if (!player->runtime.has_requested_weapon) return NULL;
  const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item(player->runtime.requested_weapon);
  return weapon ? weapon->item : NULL;
}

typedef struct handoff_ctx { qa_app_q3_selected_arsenal *arsenal; qa_actor_id actor; } handoff_ctx;
static bool in_product(qa_app_q3_selected_arsenal *arsenal, qa_item_id item) {
  const qa_q3_weapon_item *weapon = find_weapon(item);
  return weapon && (missionpack(arsenal) || weapon->weapon <= 10);
}
static bool handoff_accepts(void *v, qa_item_id item) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  return in_product(ctx->arsenal, item)
      && ctx->arsenal->options.inventory.count(ctx->arsenal->options.inventory.ctx, ctx->actor, item) > 0;
}
static bool handoff_select(void *v, qa_item_id item) {
  handoff_ctx *ctx = (handoff_ctx *)v; return qa_app_q3_selected_arsenal_select(ctx->arsenal, ctx->actor, item);
}
static void handoff_holster(void *v) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  player_arsenal *player = require_player(ctx->arsenal, ctx->actor);
  player->runtime = qa_q3_request_weapon_holster(player->runtime);
}
static bool handoff_holstered(void *v) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  return require_player(ctx->arsenal, ctx->actor)->runtime.external_slot == QA_Q3_WEAPON_SLOT_HOLSTERED;
}
static bool handoff_resume(void *v, qa_item_id item) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  player_arsenal *player = require_player(ctx->arsenal, ctx->actor);
  if (item != NULL && !in_product(ctx->arsenal, item)) qa_raise("Error", 0, 0, "Resume item is not a Q3 primary weapon");
  qa_q3_arsenal_runtime_state runtime = qa_q3_request_weapon_resume(player->runtime);
  const qa_q3_weapon_item *requested = find_weapon(item ? item : player->arsenal.active_weapon);
  player->runtime = requested == NULL ? runtime : qa_q3_request_weapon(runtime, requested->weapon);
  return item == NULL || (requested != NULL
      && ctx->arsenal->options.inventory.count(ctx->arsenal->options.inventory.ctx, ctx->actor, item) > 0);
}
qa_app_primary_weapon_handoff qa_app_q3_selected_arsenal_handoff(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  (void)require_player(arsenal, actor);
  /* The TS closures capture (this, actor) only: one record per actor serves every handoff of it. */
  /* A record whose actor slot now holds a newer generation belonged to a released actor; the
   * release destroyed the weapon slot that held its handoff, so the record is reused: the records
   * stay one per live actor slot (TS closures are collected with their holder). */
  handoff_ctx *ctx = NULL;
  for (size_t i = 0; i < arsenal->handoff_count && !ctx; i++)
    if (qa_same_actor(arsenal->handoffs[i]->actor, actor)) ctx = arsenal->handoffs[i];
  for (size_t i = 0; i < arsenal->handoff_count && !ctx; i++)
    if (arsenal->handoffs[i]->actor.token == actor.token && arsenal->handoffs[i]->actor.slot == actor.slot) {
      ctx = arsenal->handoffs[i];
      ctx->actor = actor;
    }
  if (!ctx) {
    handoff_ctx **grown = realloc(arsenal->handoffs, (arsenal->handoff_count + 1) * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    arsenal->handoffs = grown;
    ctx = calloc(1, sizeof *ctx);
    if (!ctx) qa_range_error("Out of memory");
    arsenal->handoffs[arsenal->handoff_count++] = ctx;
    ctx->arsenal = arsenal; ctx->actor = actor;
  }
  qa_app_primary_weapon_handoff handoff; memset(&handoff, 0, sizeof handoff);
  handoff.ctx = ctx; handoff.kind = "immediate"; handoff.provider = arsenal->options.provider;
  handoff.accepts = handoff_accepts; handoff.select = handoff_select; handoff.holster = handoff_holster;
  handoff.is_holstered = handoff_holstered; handoff.resume = handoff_resume;
  return handoff;
}
static const qa_q3_weapon_item *best_weapon(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor,
    const qa_pickup_ammo_receipt *before, size_t before_count) {
  for (size_t index = QA_Q3_WEAPON_ITEM_COUNT; index-- > 0;) {
    const qa_q3_weapon_item *weapon = &qa_q3_weapon_items[index];
    if (!missionpack(arsenal) && weapon->weapon > 10) continue;
    if (arsenal->options.inventory.count(arsenal->options.inventory.ctx, actor, weapon->item) <= 0) continue;
    if (weapon->ammo) {
      double amount = arsenal->options.inventory.count(arsenal->options.inventory.ctx, actor, weapon->ammo);
      for (size_t i = 0; i < before_count; i++) /* before.find(...): the first grant of this ammo */
        if (item_eq(before[i].item, weapon->ammo)) { amount = before[i].before; break; }
      if (amount <= 0) continue;
    }
    return weapon;
  }
  return NULL;
}
void qa_app_q3_selected_arsenal_pickup_ammo(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch) {
  player_arsenal *player = require_player(arsenal, actor.id);
  if (auto_switch) {
    const qa_q3_weapon_item *best = best_weapon(arsenal, actor.id, grants, grant_count);
    if (best && item_eq(best->item, player->arsenal.active_weapon)) {
      const qa_q3_weapon_item *next = best_weapon(arsenal, actor.id, NULL, 0);
      if (next) qa_app_q3_selected_arsenal_select(arsenal, actor.id, next->item);
    }
  }
}
void qa_app_q3_selected_arsenal_pickup_weapons(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_item_id *weapons, size_t weapon_count, qa_pickup_selection selection) {
  player_arsenal *player = require_player(arsenal, actor.id);
  const qa_q3_weapon_item *current = find_weapon(player->arsenal.active_weapon);
  const qa_q3_weapon_item *next = NULL;
  for (size_t index = QA_Q3_WEAPON_ITEM_COUNT; index-- > 0 && !next;) {
    for (size_t i = 0; i < weapon_count; i++)
      if (item_eq(qa_q3_weapon_items[index].item, weapons[i])) next = &qa_q3_weapon_items[index];
  }
  if (selection != QA_PICKUP_SELECTION_NEVER && next
      && (selection == QA_PICKUP_SELECTION_ALWAYS || !current || next->weapon > current->weapon))
    qa_app_q3_selected_arsenal_select(arsenal, actor.id, next->item);
}

typedef struct delay_ctx { qa_app_q3_selected_arsenal *arsenal; qa_owned_actor actor; } delay_ctx;
static double delay_cb(void *v, double milliseconds) {
  delay_ctx *ctx = (delay_ctx *)v;
  return ctx->arsenal->options.firing_delay(ctx->arsenal->options.ctx, ctx->actor, milliseconds);
}
static void release_retired(qa_app_q3_selected_arsenal *arsenal) {
  for (size_t i = 0; i < arsenal->retired_count; i++) free(arsenal->retired[i]);
  arsenal->retired_count = 0;
}
static void step_leave(void *v) {
  qa_app_q3_selected_arsenal *arsenal = v;
  if (--arsenal->step_depth == 0) release_retired(arsenal);
}
static qa_weapon_step_result step_body(qa_app_q3_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent);
qa_weapon_step_result qa_app_q3_selected_arsenal_step(qa_app_q3_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  arsenal->step_depth++;
  qa_cleanup guard;
  qa_cleanup_push(&guard, step_leave, arsenal);
  qa_weapon_step_result result = step_body(arsenal, input, intent);
  qa_cleanup_pop(&guard);
  step_leave(arsenal);
  return result;
}
static qa_weapon_step_result step_body(qa_app_q3_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  player_arsenal *player = require_player(arsenal, input.actor.id);
  qa_arsenal_state state = qa_app_q3_selected_arsenal_read(arsenal, input.actor.id);
  if (arsenal->options.has_equipment) {
    qa_app_q3_equipment_state equipment = arsenal->options.equipment.read(arsenal->options.equipment.ctx, player->actor);
    player->runtime.max_health = equipment.max_health;
    player->runtime.persistent_powerup_tag = equipment.persistent_powerup_tag;
    player->runtime.holdable_item = equipment.holdable_item;
    player->runtime.holdable_tag = equipment.holdable_tag;
  }
  double holdable = player->runtime.holdable_item;
  if (intent && (!intent->provider || !arsenal->options.provider || strcmp(intent->provider, arsenal->options.provider) != 0)) {
    free((void *)state.ammo);
    qa_raise("Error", 0, 0, "Arsenal intent belongs to a different provider");
  }
  if (intent && intent->weapon) {
    const qa_q3_weapon_item *requested = find_weapon(intent->weapon);
    /* Peers choose this string; a weapon outside the product is no request. */
    if (requested && (missionpack(arsenal) || requested->weapon <= 10)) qa_app_q3_selected_arsenal_select(arsenal, input.actor.id, intent->weapon);
  }
  qa_actor_animation_state weapon_animation; memset(&weapon_animation, 0, sizeof weapon_animation);
  weapon_animation.provider = arsenal->options.provider;
  weapon_animation.state = qa_q3_spawn_animation();
  weapon_animation.state.torso = player->torso_animation;
  qa_q3_arsenal_controls controls = resolve_controls(arsenal, state, intent, input.command);
  qa_item_id pending_use = player->pending_use; player->pending_use = NULL;
  char expected[128] = {0};
  const qa_q3_base_item_definition *held_definition = qa_q3_base_item_at(product_of(arsenal->options.product), holdable);
  if (held_definition && held_definition->class_name) snprintf(expected, sizeof expected, "q3:%s", held_definition->class_name);
  bool use = pending_use != NULL && strcmp(pending_use, expected) == 0;
  bool owns_holdables = !(arsenal->options.has_equipment && arsenal->options.equipment.has_owns_holdables && !arsenal->options.equipment.owns_holdables);
  bool controls_use_holdable = controls.use_holdable;
  if (!owns_holdables) controls.use_holdable = false;
  else controls.use_holdable = controls.use_holdable || use;
  qa_weapon_step_input stepped_input = input;
  stepped_input.arsenal = state;
  stepped_input.animation = (input.animation.state.kind && strcmp(input.animation.state.kind, "q3") == 0) ? input.animation : weapon_animation;
  delay_ctx dctx = {arsenal, player->actor};
  qa_q3_arsenal_step stepped = qa_q3_step_arsenal(stepped_input, player->runtime, controls,
      arsenal->options.firing_delay ? delay_cb : NULL, &dctx);
  qa_weapon_step_result result = stepped.base;
  free((void *)stepped.torso_animations); /* nothing reads stepQ3Arsenal's torsoAnimations here */
  double elapsed = input.frame.elapsed.kind == QA_SOURCE_TIME_MILLISECONDS ? input.frame.elapsed.value : input.frame.elapsed.value * 1000;
  double milliseconds = trunc(elapsed + player->runtime.fractional_milliseconds);
  if (arsenal->options.has_equipment && arsenal->options.equipment.advance)
    arsenal->options.equipment.advance(arsenal->options.equipment.ctx, player->actor, milliseconds);
  /* The player keeps no ammo array: read() rebuilds ammo from the inventory each time. */
  player->arsenal = result.arsenal; player->arsenal.ammo = NULL; player->arsenal.ammo_count = 0;
  player->runtime = stepped.runtime;
  if (use && !controls_use_holdable) player->runtime.use_item_held = false;
  if (holdable != 0 && stepped.runtime.holdable_item == 0 && arsenal->options.has_equipment)
    arsenal->options.equipment.consume(arsenal->options.equipment.ctx, player->actor, holdable);
  if (result.animation.state.kind && strcmp(result.animation.state.kind, "q3") == 0) player->torso_animation = result.animation.state.torso;
  for (size_t i = 0; i < result.arsenal.ammo_count; i++)
    arsenal->options.inventory.configure(arsenal->options.inventory.ctx, player->actor, result.arsenal.ammo[i]);
  if (!result.arsenal.state.kind || strcmp(result.arsenal.state.kind, "q3") != 0)
    qa_raise("Error", 0, 0, "Selected Q3 step returned a foreign arsenal");
  for (size_t i = 0; i < result.effect_count; i++) {
    if (result.effects[i].kind != QA_MOVEMENT_EFFECT_EVENT) continue;
    double event = result.effects[i].value.event;
    if (event == QA_Q3_EV_FIRE_WEAPON) {
      player->has_last_fire = true;
      player->last_fire_milliseconds = input.frame.time.kind == QA_SOURCE_TIME_MILLISECONDS ? input.frame.time.value : input.frame.time.value * 1000;
      arsenal->options.fire(arsenal->options.ctx, player->actor, result.arsenal.state.source_weapon, input);
    } else if (event >= QA_Q3_EV_USE_ITEM0 && event <= QA_Q3_EV_USE_ITEM15) {
      arsenal->options.use_holdable(arsenal->options.ctx, player->actor, event, input);
    }
  }
  bool present = find_player(arsenal, player->actor.id) != NULL;
  if (present && arsenal->options.has_equipment && arsenal->options.equipment.end_command)
    arsenal->options.equipment.end_command(arsenal->options.equipment.ctx, player->actor, milliseconds);
  if (present) {
    free((void *)result.arsenal.ammo); /* stepQ3Arsenal's ammo list, configured into the inventory above */
    result.arsenal = qa_app_q3_selected_arsenal_read(arsenal, player->actor.id);
  }
  result.animation = (input.animation.state.kind && strcmp(input.animation.state.kind, "q3") == 0) ? result.animation : input.animation;
  free((void *)state.ammo);
  return result;
}
void qa_app_q3_selected_arsenal_remove(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  for (size_t i = 0; i < arsenal->player_count; i++) {
    if (qa_same_actor(arsenal->players[i]->actor.id, actor)) {
      if (arsenal->step_depth == 0) free(arsenal->players[i]);
      else {
        player_arsenal **grown = realloc(arsenal->retired, (arsenal->retired_count + 1) * sizeof *grown);
        if (!grown) qa_range_error("Out of memory");
        arsenal->retired = grown;
        arsenal->retired[arsenal->retired_count++] = arsenal->players[i];
      }
      memmove(arsenal->players + i, arsenal->players + i + 1, (arsenal->player_count - i - 1) * sizeof *arsenal->players);
      arsenal->player_count--; return;
    }
  }
}
qa_app_q3_selected_arsenal_checkpoint qa_app_q3_selected_arsenal_capture(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  player_arsenal *player = require_player(arsenal, actor);
  qa_app_q3_selected_arsenal_checkpoint checkpoint; memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.supply_profile = arsenal->options.has_supply ? arsenal->options.supply_profile : NULL;
  checkpoint.arsenal = qa_app_q3_selected_arsenal_read(arsenal, actor);
  checkpoint.runtime = player->runtime;
  if (arsenal->options.has_equipment) {
    qa_app_q3_equipment_state equipment = arsenal->options.equipment.read(arsenal->options.equipment.ctx, player->actor);
    checkpoint.runtime.max_health = equipment.max_health;
    checkpoint.runtime.persistent_powerup_tag = equipment.persistent_powerup_tag;
    checkpoint.runtime.holdable_item = equipment.holdable_item;
    checkpoint.runtime.holdable_tag = equipment.holdable_tag;
  }
  checkpoint.torso_animation = player->torso_animation;
  checkpoint.has_last_fire_milliseconds = player->has_last_fire;
  checkpoint.last_fire_milliseconds = player->last_fire_milliseconds;
  checkpoint.pending_use = player->pending_use;
  checkpoint.arsenal_read_order = player->arsenal_read_order;
  return checkpoint;
}
void qa_app_q3_selected_arsenal_restore(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    qa_app_q3_selected_arsenal_checkpoint checkpoint) {
  qa_item_id expected_profile = arsenal->options.has_supply ? arsenal->options.supply_profile : NULL;
  if ((checkpoint.supply_profile == NULL) != (expected_profile == NULL)
      || (checkpoint.supply_profile && !item_eq(checkpoint.supply_profile, expected_profile)))
    qa_raise("Error", 0, 0, "Saved pickup supply differs from selected arsenal composition");
  if (!checkpoint.arsenal.provider || !arsenal->options.provider || strcmp(checkpoint.arsenal.provider, arsenal->options.provider) != 0
      || !checkpoint.arsenal.state.kind || strcmp(checkpoint.arsenal.state.kind, "q3") != 0
      || !checkpoint.runtime.product || strcmp(checkpoint.runtime.product, arsenal->options.product) != 0)
    qa_raise("Error", 0, 0, "Saved arsenal differs from selected Q3 provider");
  for (size_t i = 0; i < checkpoint.arsenal.ammo_count; i++)
    if (in_inventory_items(arsenal, checkpoint.arsenal.ammo[i].item))
      arsenal->options.inventory.configure(arsenal->options.inventory.ctx, actor, checkpoint.arsenal.ammo[i]);
  if (arsenal->options.has_equipment && arsenal->options.equipment.restore) {
    qa_app_q3_equipment_state equipment = {checkpoint.runtime.max_health, checkpoint.runtime.persistent_powerup_tag,
        checkpoint.runtime.holdable_item, checkpoint.runtime.holdable_tag};
    arsenal->options.equipment.restore(arsenal->options.equipment.ctx, actor, equipment);
  }
  player_arsenal *player = find_player(arsenal, actor.id);
  if (!player) player = add_player(arsenal);
  memset(player, 0, sizeof *player);
  player->actor = actor; player->arsenal = checkpoint.arsenal; player->runtime = checkpoint.runtime;
  player->arsenal.ammo = NULL; player->arsenal.ammo_count = 0; /* the checkpoint's list stays its owner's */
  player->torso_animation = checkpoint.torso_animation;
  player->has_last_fire = checkpoint.has_last_fire_milliseconds;
  player->last_fire_milliseconds = checkpoint.last_fire_milliseconds;
  player->pending_use = intern(arsenal, checkpoint.pending_use);
  player->arsenal_read_order = checkpoint.arsenal_read_order;
}
static double ui_count(void *v, qa_item_id item) {
  struct { qa_app_inventory_access inventory; qa_actor_id actor; } *ctx = v;
  return ctx->inventory.count(ctx->inventory.ctx, ctx->actor, item);
}
static double no_count(void *ctx, qa_item_id item) { (void)ctx; (void)item; return -1; }
/* The whole weapon status label for item, interned until destroy; on a raise frees pending (the read's ammo). */
static const char *status_label(qa_app_q3_selected_arsenal *arsenal, qa_item_id item, void *pending) {
  for (size_t i = 0; i < arsenal->label_count; i++) if (strcmp(arsenal->label_items[i], item) == 0) return arsenal->labels[i];
  size_t size = qa_app_q3_weapon_label_size(item, arsenal->options.product);
  char *label = calloc(size, 1), *copy = malloc(strlen(item) + 1);
  char **items = realloc(arsenal->label_items, (arsenal->label_count + 1) * sizeof *items);
  if (items != NULL) arsenal->label_items = items;
  char **labels = items == NULL ? NULL : realloc(arsenal->labels, (arsenal->label_count + 1) * sizeof *labels);
  if (labels != NULL) arsenal->labels = labels;
  if (label == NULL || copy == NULL || labels == NULL) { free(label); free(copy); free(pending); qa_range_error("Out of memory"); }
  qa_weapon_hud_status ignored;
  qa_provider_reference none;
  memset(&none, 0, sizeof none);
  (void)qa_app_q3_weapon_status(item, arsenal->options.product, no_count, NULL, none, label, size, &ignored);
  memcpy(copy, item, strlen(item) + 1);
  arsenal->label_items[arsenal->label_count] = copy;
  arsenal->labels[arsenal->label_count++] = label;
  return label;
}
qa_app_arsenal_ui qa_app_q3_selected_arsenal_ui(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor,
    qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity, char (*labels)[64], size_t label_capacity) {
  qa_arsenal_state state = qa_app_q3_selected_arsenal_read(arsenal, actor);
  const qa_q3_weapon_item *weapon = state.state.kind && strcmp(state.state.kind, "q3") == 0
      ? qa_q3_get_weapon_item(state.state.source_weapon) : NULL;
  qa_app_arsenal_ui ui; memset(&ui, 0, sizeof ui);
  struct { qa_app_inventory_access inventory; qa_actor_id actor; } ctx = {arsenal->options.inventory, actor};
  ui.has_weapon_status = qa_app_q3_weapon_status(state.active_weapon, arsenal->options.product, ui_count, &ctx, source, NULL, 0, &ui.weapon_status);
  ui.weapon_status.label = ui.has_weapon_status ? status_label(arsenal, state.active_weapon, (void *)state.ammo) : NULL;
  (void)labels; (void)label_capacity; /* item labels are the static pickup names */
  ui.arsenal_warning = qa_app_q3_arsenal_warning(arsenal->options.product, ui_count, &ctx);
  ui.active_weapon = state.active_weapon;
  if (weapon && weapon->ammo) {
    ui.has_ammo = true; ui.ammo_item = weapon->ammo;
    ui.ammo_count = arsenal->options.inventory.count(arsenal->options.inventory.ctx, actor, weapon->ammo);
  }
  ui.items = items; ui.item_capacity = capacity;
  size_t item_count = 0;
  const qa_q3_base_item_definition *definitions = qa_q3_base_item_list(product_of(arsenal->options.product), &item_count);
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && ui.item_count < capacity; i++) {
    const qa_q3_weapon_item *entry_weapon = &qa_q3_weapon_items[i];
    if (!missionpack(arsenal) && entry_weapon->weapon > 10) continue;
    const char *pickup = NULL;
    for (size_t j = 0; j < item_count && pickup == NULL; j++) /* itemList(product).find(...): the first match */
      if (definitions[j].type == QA_Q3_BASE_IT_WEAPON && definitions[j].tag == entry_weapon->weapon) pickup = definitions[j].pickup_name;
    if (!pickup) { free((void *)state.ammo); qa_raise("Error", 0, 0, "Selected Q3 weapon lacks its original item label"); }
    qa_app_player_ui_item *entry = &items[ui.item_count];
    memset(entry, 0, sizeof *entry);
    entry->id = entry_weapon->item; entry->kind = "weapon"; entry->source_ordinal = entry_weapon->weapon;
    entry->label = pickup;
    entry->owned = arsenal->options.inventory.count(arsenal->options.inventory.ctx, actor, entry_weapon->item) > 0;
    entry->has_count = entry_weapon->ammo != NULL;
    entry->count = entry_weapon->ammo ? arsenal->options.inventory.count(arsenal->options.inventory.ctx, actor, entry_weapon->ammo) : 0;
    entry->has_ammo = !entry->has_count || entry->count > 0;
    entry->warning_count = 0;
    ui.item_count++;
  }
  free((void *)state.ammo);
  return ui;
}
void qa_app_q3_selected_arsenal_view_state(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor,
    double *out_torso_animation, bool *out_has_last_fire, double *out_last_fire) {
  player_arsenal *player = require_player(arsenal, actor);
  *out_torso_animation = player->torso_animation;
  *out_has_last_fire = player->has_last_fire;
  *out_last_fire = player->last_fire_milliseconds;
}
qa_app_arsenal_view qa_app_q3_selected_arsenal_view(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_arsenal_state state = qa_app_q3_selected_arsenal_read(arsenal, actor);
  qa_app_arsenal_view view; memset(&view, 0, sizeof view);
  if (!state.state.kind || strcmp(state.state.kind, "q3") != 0) {
    free((void *)state.ammo);
    qa_raise("Error", 0, 0, "Selected Q3 player has foreign arsenal state");
  }
  size_t count = 0;
  const qa_q3_base_item_definition *definitions = qa_q3_base_item_list(product_of(arsenal->options.product), &count);
  for (size_t i = 0; i < count; i++) {
    if (definitions[i].type == QA_Q3_BASE_IT_WEAPON && definitions[i].tag == state.state.source_weapon) {
      if (definitions[i].world_models[0]) { view.has_value = true; view.path = definitions[i].world_models[0]; view.frame = 0; }
      break;
    }
  }
  free((void *)state.ammo);
  return view;
}

static const char *nullable_namespaced(const qa_save_reader *reader) {
  return qa_save_reader_is_null(reader) ? NULL : qa_namespaced(reader);
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
qa_app_q3_selected_arsenal_checkpoint qa_app_read_q3_selected_arsenal_checkpoint(const qa_save_reader *reader) {
  qa_save_reader supply = field(reader, "supplyProfile");
  qa_save_reader arsenal = field(reader, "arsenal");
  qa_save_reader state = field(&arsenal, "state");
  qa_save_reader runtime = field(reader, "runtime");
  qa_app_q3_selected_arsenal_checkpoint checkpoint; memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.supply_profile = qa_save_reader_is_undefined(&supply) ? NULL : nullable_namespaced(&supply);
  qa_save_reader provider = field(&arsenal, "provider");
  checkpoint.arsenal.provider = qa_namespaced(&provider);
  qa_save_reader active = field(&arsenal, "activeWeapon");
  checkpoint.arsenal.active_weapon = nullable_namespaced(&active);
  qa_save_reader ammo = field(&arsenal, "ammo");
  size_t ammo_count = qa_save_reader_list_count(&ammo);
  qa_inventory_entry *entries = malloc((ammo_count ? ammo_count : 1) * sizeof *entries);
  if (!entries) qa_range_error("Out of memory");
  for (size_t i = 0; i < ammo_count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&ammo, i, &item)) qa_range_error("Out of memory");
    entries[i] = qa_read_inventory_entry(&item);
    qa_save_reader_destroy(&item);
  }
  checkpoint.arsenal.ammo = entries; checkpoint.arsenal.ammo_count = ammo_count;
  qa_save_reader kind = field(&state, "kind");
  checkpoint.arsenal.state.kind = qa_save_reader_literal_string(&kind, "q3");
  qa_save_reader r;
  r = field(&state, "sourceWeapon"); checkpoint.arsenal.state.source_weapon = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = field(&state, "state"); checkpoint.arsenal.state.state = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = field(&state, "timeMilliseconds"); checkpoint.arsenal.state.time_milliseconds = qa_save_reader_number(&r); qa_save_reader_destroy(&r);
  const char *products[2] = {"baseq3", "missionpack"};
  r = field(&runtime, "product"); checkpoint.runtime.product = qa_save_reader_choice_string(&r, products, 2); qa_save_reader_destroy(&r);
  r = field(&runtime, "maxHealth"); checkpoint.runtime.max_health = qa_save_reader_number(&r); qa_save_reader_destroy(&r);
  r = field(&runtime, "spectator"); checkpoint.runtime.spectator = qa_save_reader_boolean(&r); qa_save_reader_destroy(&r);
  r = field(&runtime, "persistentPowerupTag"); checkpoint.runtime.persistent_powerup_tag = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = field(&runtime, "holdableItem"); checkpoint.runtime.holdable_item = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = field(&runtime, "holdableTag"); checkpoint.runtime.holdable_tag = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = field(&runtime, "respawned"); checkpoint.runtime.respawned = qa_save_reader_boolean(&r); qa_save_reader_destroy(&r);
  r = field(&runtime, "useItemHeld"); checkpoint.runtime.use_item_held = qa_save_reader_boolean(&r); qa_save_reader_destroy(&r);
  r = field(&runtime, "eventSequence"); checkpoint.runtime.event_sequence = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = field(&runtime, "fractionalMilliseconds"); checkpoint.runtime.fractional_milliseconds = qa_save_reader_number(&r); qa_save_reader_destroy(&r);
  r = field(&runtime, "externalSlot");
  if (!qa_save_reader_is_undefined(&r)) {
    const char *slots[5] = {"active", "holster-requested", "dropping", "holstered", "resume-requested"};
    const char *slot = qa_save_reader_choice_string(&r, slots, 5);
    checkpoint.runtime.external_slot = strcmp(slot, "holster-requested") == 0 ? QA_Q3_WEAPON_SLOT_HOLSTER_REQUESTED
        : strcmp(slot, "dropping") == 0 ? QA_Q3_WEAPON_SLOT_DROPPING
        : strcmp(slot, "holstered") == 0 ? QA_Q3_WEAPON_SLOT_HOLSTERED
        : strcmp(slot, "resume-requested") == 0 ? QA_Q3_WEAPON_SLOT_RESUME_REQUESTED : QA_Q3_WEAPON_SLOT_ACTIVE;
  }
  qa_save_reader_destroy(&r);
  r = field(&runtime, "requestedWeapon");
  if (!qa_save_reader_is_null(&r)) { checkpoint.runtime.has_requested_weapon = true; checkpoint.runtime.requested_weapon = qa_save_reader_integer_min(&r, 0); }
  qa_save_reader_destroy(&r);
  r = field(reader, "torsoAnimation"); checkpoint.torso_animation = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = field(reader, "lastFireMilliseconds");
  if (!qa_save_reader_is_null(&r)) { checkpoint.has_last_fire_milliseconds = true; checkpoint.last_fire_milliseconds = qa_save_reader_finite(&r); }
  qa_save_reader_destroy(&r);
  r = field(reader, "pendingUse");
  checkpoint.pending_use = qa_save_reader_is_undefined(&r) ? NULL : nullable_namespaced(&r);
  qa_save_reader_destroy(&r);
  qa_save_reader_destroy(&supply); qa_save_reader_destroy(&provider); qa_save_reader_destroy(&active);
  qa_save_reader_destroy(&ammo); qa_save_reader_destroy(&kind); qa_save_reader_destroy(&state);
  qa_save_reader_destroy(&arsenal); qa_save_reader_destroy(&runtime);
  checkpoint.arsenal_read_order = true;
  return checkpoint;
}

static size_t iface_catalog(void *v, qa_selected_pickup_weapon *out, size_t capacity) { return qa_app_q3_selected_arsenal_catalog((qa_app_q3_selected_arsenal *)v, out, capacity); }
static bool iface_has(void *v, qa_actor_id actor) { return qa_app_q3_selected_arsenal_has((qa_app_q3_selected_arsenal *)v, actor); }
static qa_arsenal_state iface_admit(void *v, qa_owned_actor actor, double max_health, bool team_deathmatch) {
  return qa_app_q3_selected_arsenal_admit((qa_app_q3_selected_arsenal *)v, actor, max_health, team_deathmatch);
}
static qa_arsenal_state iface_read(void *v, qa_actor_id actor) { return qa_app_q3_selected_arsenal_read((qa_app_q3_selected_arsenal *)v, actor); }
static bool iface_select(void *v, qa_actor_id actor, qa_item_id item) { return qa_app_q3_selected_arsenal_select((qa_app_q3_selected_arsenal *)v, actor, item); }
static void iface_pickup_ammo(void *v, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  qa_app_q3_selected_arsenal_pickup_ammo((qa_app_q3_selected_arsenal *)v, actor, grants, count, auto_switch);
}
static void iface_pickup_weapons(void *v, qa_owned_actor actor, const qa_item_id *items, size_t count, qa_pickup_selection selection) {
  qa_app_q3_selected_arsenal_pickup_weapons((qa_app_q3_selected_arsenal *)v, actor, items, count, selection);
}
static qa_item_id iface_pending(void *v, qa_actor_id actor) { return qa_app_q3_selected_arsenal_pending_weapon((qa_app_q3_selected_arsenal *)v, actor); }
static qa_app_primary_weapon_handoff iface_handoff(void *v, qa_actor_id actor) { return qa_app_q3_selected_arsenal_handoff((qa_app_q3_selected_arsenal *)v, actor); }
static qa_weapon_step_result iface_step(void *v, qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  return qa_app_q3_selected_arsenal_step((qa_app_q3_selected_arsenal *)v, input, intent);
}
static void iface_remove(void *v, qa_actor_id actor) { qa_app_q3_selected_arsenal_remove((qa_app_q3_selected_arsenal *)v, actor); }
static qa_app_arsenal_ui iface_ui(void *v, qa_actor_id actor, qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity) {
  return qa_app_q3_selected_arsenal_ui((qa_app_q3_selected_arsenal *)v, actor, source, items, capacity, NULL, 0);
}
static qa_app_arsenal_view iface_view(void *v, qa_actor_id actor) { return qa_app_q3_selected_arsenal_view((qa_app_q3_selected_arsenal *)v, actor); }
qa_app_selected_arsenal qa_app_q3_selected_arsenal_interface(qa_app_q3_selected_arsenal *arsenal) {
  qa_app_selected_arsenal iface; memset(&iface, 0, sizeof iface);
  iface.ctx = arsenal; iface.family = "q3"; iface.provider = arsenal->options.provider;
  iface.catalog = iface_catalog; iface.has = iface_has; iface.admit = iface_admit; iface.read = iface_read;
  iface.select = iface_select; iface.pickup_ammo = iface_pickup_ammo; iface.pickup_weapons = iface_pickup_weapons;
  iface.pending_weapon = iface_pending; iface.handoff = iface_handoff; iface.step = iface_step;
  iface.remove = iface_remove; iface.ui = iface_ui; iface.view = iface_view;
  return iface;
}
