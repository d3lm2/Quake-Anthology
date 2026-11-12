/* Port of src/content/q2/missionpacks/monsters/power-armor.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/power_armor.h"

#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"


/* TS WeakSet<OwnedActor> of bound actors (identity: id + owner + token). A TS
 * OwnedActor object ends with its session; a session token's address is reused
 * by the next session, so the records of a token are retired when its identity
 * owner is destroyed, as content/q2/base/monsters/brain.c does (a retired
 * record is reused, never freed: an authority may still hold it as its cell
 * binding). The closures read the actor, never the entity, which is freed
 * while the binding stays with the authority. */
typedef struct cell_binding {
  qa_owned_actor actor;
  bool retired;
  qa_q2_game_services *game;
  double capacity; /* TS entry.capacity captured at bind time */
  struct cell_binding *next;
} cell_binding;
static cell_binding *bindings;
static bool retire_registered;
static void retire_session(const void *token) {
  for (cell_binding *b = bindings; b; b = b->next)
    if (b->actor.token == token) b->retired = true;
}
static bool same_owned(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_actor(a.id, b.id) && a.token == b.token
      && ((a.owner == NULL && b.owner == NULL) || (a.owner && b.owner && strcmp(a.owner, b.owner) == 0));
}
static double cell_read(void *ctx) {
  cell_binding *cell = ctx;
  return qa_shared_inventory_table_count(cell->game->host->inventory, cell->actor.id, "q2:monster-power");
}
static void cell_write(void *ctx, double count) {
  cell_binding *cell = ctx;
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = count;
  entry.capacity = cell->capacity;
  qa_shared_inventory_table_configure(cell->game->host->inventory, cell->actor, entry, NULL, NULL);
}
/* TS: bindPowerArmorCells, then bindings.add: a bind that raises records nothing. */
static void unbind_record(void *arg) { ((cell_binding *)arg)->retired = true; }

void qa_q2mpm_restore_monster_power_armor(qa_q2m_context *context) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  if (!retire_registered) { qa_identity_owner_on_destroy(retire_session); retire_registered = true; }
  cell_binding *cell = NULL;
  for (cell_binding *seen = bindings; seen; seen = seen->next) {
    if (seen->retired) { if (!cell) cell = seen; continue; }
    if (same_owned(seen->actor, entity->actor)) return;
  }
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host->inventory, entity->actor.id, &count);
  const qa_inventory_entry *found = NULL;
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, "q2:monster-power") == 0) { found = &entries[i]; break; }
  if (!found) qa_raise("Error", 0, 0, "Monster power armor is missing its shared inventory cells");
  if (!cell) {
    cell = malloc(sizeof *cell);
    if (!cell) qa_raise("Error", 0, 0, "Out of memory binding monster power armor cells");
    cell->retired = true;
    cell->next = bindings;
    bindings = cell;
  }
  cell->game = game;
  cell->actor = entity->actor;
  cell->capacity = found->capacity;
  cell->retired = false;
  qa_power_armor_cell_binding binding = {cell, cell_read, cell_write};
  qa_cleanup node;
  qa_cleanup_push(&node, unbind_record, cell);
  qa_gameplay_authority_bind_power_armor_cells(game->host->combat, entity->actor, binding);
  qa_cleanup_pop(&node);
}

void qa_q2mpm_monster_power_armor(qa_q2m_context *context, const char *kind, double cells) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  if (!qa_shared_inventory_table_has(game->host->inventory, entity->actor.id))
    qa_shared_inventory_table_create_entries(game->host->inventory, entity->actor, NULL, 0);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = cells;
  entry.capacity = cells;
  qa_shared_inventory_table_configure(game->host->inventory, entity->actor, entry, NULL, NULL);
  qa_q2mpm_restore_monster_power_armor(context);
  qa_powered_protection_state powered = {kind, cells};
  qa_gameplay_authority_set_powered_protection(game->host->combat, entity->actor, powered);
}

void qa_q2mpm_restore_monster_power_armor_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2mpm_restore_monster_power_armor(context);
}
