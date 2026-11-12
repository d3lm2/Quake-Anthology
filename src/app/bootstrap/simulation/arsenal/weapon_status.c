/* Port of src/app/bootstrap/simulation/arsenal/weapon-status.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"

static void display_name(const char *value, char *out, size_t out_size) {
  if (!out || out_size == 0) return;
  size_t length = 0;
  bool word_start = true;
  for (const char *p = value; *p && length + 1 < out_size; p++) {
    char c = (*p == '_' || *p == '-') ? ' ' : *p;
    if (word_start && (isalnum((unsigned char)c) || c == '_')) c = (char)toupper((unsigned char)c);
    out[length++] = c;
    word_start = !(isalnum((unsigned char)c) || c == '_');
  }
  out[length] = '\0';
}
static qa_q3_base_product product_of(const char *product) {
  return product && strcmp(product, "missionpack") == 0 ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}

/* Mirror of content/q1/foundation/weapon-names.ts (landed header has
 * no definition yet); table transcribed from the TS source. */
static const struct { const char *weapon, *label; } Q1_NAMES[] = {
  {"axe", "Axe"}, {"shotgun", "Shotgun"}, {"supershotgun", "Double-barrelled Shotgun"}, {"nailgun", "Nailgun"},
  {"supernailgun", "Super Nailgun"}, {"grenadelauncher", "Grenade Launcher"}, {"rocketlauncher", "Rocket Launcher"},
  {"lightning", "Thunderbolt"}, {"hipnotic:laser", "Laser Cannon"}, {"hipnotic:mjolnir", "Mjolnir"},
  {"hipnotic:proximity", "Proximity Gun"},
};
/* The table label, or NULL with *suffix the part displayName() rewrites (weapon.split(":").at(-1)). */
static const char *q1_table_name(qa_q1_weapon weapon, const char **suffix) {
  for (size_t i = 0; i < sizeof Q1_NAMES / sizeof *Q1_NAMES; i++)
    if (weapon && strcmp(weapon, Q1_NAMES[i].weapon) == 0) return Q1_NAMES[i].label;
  const char *name = weapon ? weapon : "";
  const char *colon = strrchr(name, ':');
  *suffix = colon ? colon + 1 : name;
  return NULL;
}
static void q1_display_name(qa_q1_weapon weapon, char *out, size_t out_size) {
  if (!out || out_size == 0) return;
  const char *suffix = NULL, *name = q1_table_name(weapon, &suffix);
  if (name != NULL) { strncpy(out, name, out_size - 1); out[out_size - 1] = '\0'; return; }
  display_name(suffix, out, out_size);
}

void qa_app_q1_weapon_display_name(qa_q1_weapon weapon, char *out, size_t out_size) {
  q1_display_name(weapon, out, out_size);
}
/* displayName() keeps its input's length, so each label's whole size is known before it is written. */
size_t qa_app_q1_weapon_label_size(qa_q1_weapon weapon) {
  const char *suffix = NULL, *name = q1_table_name(weapon, &suffix);
  return strlen(name != NULL ? name : suffix) + 1;
}
size_t qa_app_q2_weapon_label_size(const qa_q2w_definition *definition) {
  if (!definition) return 1;
  const char *name = qa_q2_base_weapon_display_name(definition->item);
  return strlen(name != NULL ? name : definition->name) + 1;
}

qa_weapon_hud_status qa_app_q1_weapon_status(qa_app_q1_status_host game, qa_q1_player_state *player,
    qa_provider_reference source, char *label, size_t label_size) {
  qa_weapon_hud_status status; memset(&status, 0, sizeof status);
  status.source = source;
  status.item = game.weapon_item(game.ctx, player->weapon);
  qa_item_id ammo = game.weapon_ammo(game.ctx, player->weapon);
  q1_display_name(player->weapon, label, label_size);
  status.label = label;
  if (ammo) {
    status.ammo_is_finite = true; status.ammo_item = ammo;
    status.ammo_count = game.inventory_count(game.ctx, player->actor.id, ammo);
    status.has_ammo_to_start = game.weapon_available(game.ctx, player, player->weapon, "fire");
    status.low = false;
  }
  return status;
}

bool qa_app_q2_weapon_status(const qa_q2w_definition *definition, qa_app_item_count_fn count, void *count_ctx,
    qa_provider_reference source, char *label, size_t label_size, qa_weapon_hud_status *out) {
  if (!definition) return false;
  memset(out, 0, sizeof *out);
  out->source = source; out->item = definition->item;
  const char *name = qa_q2_base_weapon_display_name(definition->item);
  if (name) {
    if (label && label_size) { strncpy(label, name, label_size - 1); label[label_size - 1] = '\0'; }
    out->label = label;
  } else {
    display_name(definition->name, label, label_size);
    out->label = label;
  }
  if (definition->ammo) {
    double amount = count(count_ctx, definition->ammo);
    out->ammo_is_finite = true; out->ammo_item = definition->ammo; out->ammo_count = amount;
    out->has_ammo_to_start = amount >= definition->quantity;
    out->low = amount <= definition->warning;
  }
  return true;
}

static const qa_q3_weapon_item *q3_definition(qa_item_id item, const char *product) {
  const qa_q3_weapon_item *definition = NULL;
  bool missionpack = product && strcmp(product, "missionpack") == 0;
  /* Q3_WEAPON_ITEMS.find(...): the first match. */
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && !definition; i++)
    if (qa_q3_weapon_items[i].item && item && strcmp(qa_q3_weapon_items[i].item, item) == 0
        && (missionpack || qa_q3_weapon_items[i].weapon <= 10)) definition = &qa_q3_weapon_items[i];
  return definition;
}
static const char *q3_pickup_name(const qa_q3_weapon_item *definition, const char *product) {
  size_t item_count = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(product_of(product), &item_count);
  for (size_t i = 0; i < item_count; i++) /* itemList(product).find(...): the first match */
    if (items[i].type == QA_Q3_BASE_IT_WEAPON && items[i].tag == definition->weapon) return items[i].pickup_name;
  return NULL;
}
/* definition.item.slice("q3:weapon/".length) */
static const char *q3_item_suffix(const qa_q3_weapon_item *definition) {
  size_t prefix = strlen("q3:weapon/");
  return strlen(definition->item) > prefix ? definition->item + prefix : "";
}
size_t qa_app_q3_weapon_label_size(qa_item_id item, const char *product) {
  const qa_q3_weapon_item *definition = q3_definition(item, product);
  if (!definition) return 1;
  const char *pickup = q3_pickup_name(definition, product);
  return strlen(pickup != NULL ? pickup : q3_item_suffix(definition)) + 1;
}
bool qa_app_q3_weapon_status(qa_item_id item, const char *product, qa_app_item_count_fn count, void *count_ctx,
    qa_provider_reference source, char *label, size_t label_size, qa_weapon_hud_status *out) {
  const qa_q3_weapon_item *definition = q3_definition(item, product);
  if (!definition) return false;
  memset(out, 0, sizeof *out);
  out->source = source; out->item = definition->item;
  const char *pickup = q3_pickup_name(definition, product);
  if (pickup) {
    if (label && label_size) { strncpy(label, pickup, label_size - 1); label[label_size - 1] = '\0'; }
  } else {
    display_name(q3_item_suffix(definition), label, label_size);
  }
  out->label = label;
  if (definition->ammo && count(count_ctx, definition->ammo) != -1) {
    double amount = count(count_ctx, definition->ammo);
    out->ammo_is_finite = true; out->ammo_item = definition->ammo; out->ammo_count = amount;
    out->has_ammo_to_start = amount > 0; out->low = false;
  }
  return true;
}

qa_arsenal_ammo_warning qa_app_q3_arsenal_warning(const char *product, qa_app_item_count_fn count, void *count_ctx) {
  int32_t total = 0;
  bool base = !product || strcmp(product, "baseq3") == 0;
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
    const qa_q3_weapon_item *entry = &qa_q3_weapon_items[i];
    double weapon = entry->weapon;
    if (weapon < QA_Q3_WP_MACHINEGUN || (base && weapon > 10) || count(count_ctx, entry->item) <= 0) continue;
    bool slow = weapon == QA_Q3_WP_ROCKET_LAUNCHER || weapon == QA_Q3_WP_GRENADE_LAUNCHER
        || weapon == QA_Q3_WP_RAILGUN || weapon == QA_Q3_WP_SHOTGUN
        || (!base && weapon == QA_Q3_WP_PROX_LAUNCHER);
    /* total = (total + Math.imul(amount, slow ? 1000 : 200)) | 0: wrapping int32 arithmetic (C int32
     * overflow is undefined, so the product and sum are taken in 64 bits and wrapped). */
    int32_t amount = entry->ammo == NULL ? -1 : qa_js_to_int32(count(count_ctx, entry->ammo));
    int32_t product = (int32_t)(uint32_t)((uint64_t)(int64_t)amount * (uint64_t)(slow ? 1000 : 200));
    total = (int32_t)(uint32_t)((uint64_t)(int64_t)total + (uint64_t)(int64_t)product);
    if (total >= 5000) return "none";
  }
  return total == 0 ? "empty" : "low";
}
