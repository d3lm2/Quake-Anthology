/* Port of src/persistence/protection.ts. GPL-2.0-or-later. */
#include "qa/persistence/protection.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"

static const char *const schema_name = "world:primary-protection";

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}

static qa_save_value *actor_value(qa_saved_actor_id actor) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number(actor.slot)) ||
      !set_own(obj, "generation", qa_save_value_number(actor.generation))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
/* Same armor builders as src/persistence/q2_players.c (the TS
 * encodeCheckpointValue of an ArmorState channel). */
static qa_save_value *build_regular_armor(const qa_regular_armor_state *in) {
  static const char *const names[] = {"none", "q1", "q2", "q3", "source"};
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "kind", qa_save_value_string(names[in->kind]));
  if (ok && in->kind == QA_REGULAR_ARMOR_SOURCE)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "item",
                 in->item != NULL ? qa_save_value_string(in->item)
                                  : qa_save_value_null());
  if (ok && in->kind == QA_REGULAR_ARMOR_Q1)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "absorption", qa_save_value_number(in->absorption)) &&
         set_own(obj, "item",
                 qa_save_value_string(in->item != NULL ? in->item : ""));
  if (ok && in->kind == QA_REGULAR_ARMOR_Q3)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "protection", qa_save_value_number(in->protection));
  if (ok && in->kind == QA_REGULAR_ARMOR_Q2)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "normalProtection",
                 qa_save_value_number(in->normal_protection)) &&
         set_own(obj, "energyProtection",
                 qa_save_value_number(in->energy_protection)) &&
         set_own(obj, "item",
                 qa_save_value_string(in->item != NULL ? in->item : ""));
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *build_powered(const qa_powered_protection_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "kind",
                    qa_save_value_string(in->kind != NULL ? in->kind : "none"));
  if (ok && in->kind != NULL && strcmp(in->kind, "none") != 0)
    ok = set_own(obj, "cells", qa_save_value_number(in->cells));
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}

bool qa_capture_primary_protection(const qa_session_actor_registry *actors,
                                   const qa_gameplay_authority *combat,
                                   qa_provider_checkpoint *out) {
  memset(out, 0, sizeof *out);
  combat->assert_idle(combat->ctx);
  qa_save_value *entries = qa_save_value_array();
  if (entries == NULL) return false;
  size_t observation_count = 0;
  const qa_actor_observation *observations =
      actors->observations(actors->ctx, &observation_count);
  for (size_t i = 0; i < observation_count; i++) {
    qa_owned_actor actor;
    if (!actors->resolve_owned(actors->ctx, observations[i].id, &actor))
      continue;
    qa_armor_state primary;
    if (!combat->copied_primary_armor(combat->ctx, actor, &primary))
      continue;
    bool regular =
        combat->protection_owner(combat->ctx, actor, QA_PROTECTION_REGULAR) !=
        NULL;
    bool powered =
        combat->protection_owner(combat->ctx, actor, QA_PROTECTION_POWERED) !=
        NULL;
    size_t ignored_count = 0;
    if (regular)
      (void)combat->protection_inventory_items(combat->ctx, actor,
                                               QA_PROTECTION_REGULAR,
                                               &ignored_count);
    if (powered)
      (void)combat->protection_inventory_items(combat->ctx, actor,
                                               QA_PROTECTION_POWERED,
                                               &ignored_count);
    if (!regular && !powered) continue;
    qa_save_value *entry = qa_save_value_object();
    bool ok = entry != NULL &&
              set_own(entry, "actor",
                      actor_value(qa_saved_actor_id_of(actor.id))) &&
              set_own(entry, "owner", qa_save_value_string(actor.owner));
    if (ok && regular)
      ok = set_own(entry, "regular", build_regular_armor(&primary.regular));
    if (ok && powered)
      ok = set_own(entry, "powered", build_powered(&primary.powered));
    if (!ok) {
      qa_save_value_free(entry);
      qa_save_value_free(entries);
      return false;
    }
    if (!push_own(entries, entry)) { /* push_own frees entry on failure */
      qa_save_value_free(entries);
      return false;
    }
  }
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(entries, &length);
  qa_save_value_free(entries);
  if (bytes == NULL) return false;
  out->provider = "world:gameplay";
  out->schema = schema_name;
  out->version = 1;
  out->bytes.bytes = bytes;
  out->bytes.length = length;
  return true;
}

void qa_primary_protection_free(qa_primary_protection *protection) {
  if (protection == NULL) return;
  free(protection->entries);
  qa_save_value_free(protection->root);
  memset(protection, 0, sizeof *protection);
}

static void free_partial(void *arg) { qa_primary_protection_free(arg); }
static void free_value(void *arg) { qa_save_value_free(arg); }
static bool read_primary_protection(const qa_save_image *save, const qa_session_actor_registry *actors,
                                    qa_primary_protection *out, const qa_save_value *none);
/* A malformed record raises with what was read released (TS discards it). */
bool qa_read_primary_protection(const qa_save_image *save,
                                const qa_session_actor_registry *actors,
                                qa_primary_protection *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *none = qa_save_value_object();
  if (none == NULL || !qa_save_value_set(none, "kind", qa_save_value_string("none"))) {
    qa_save_value_free(none);
    return false;
  }
  qa_cleanup none_guard, guard;
  qa_cleanup_push(&none_guard, free_value, none);
  qa_cleanup_push(&guard, free_partial, out);
  bool ok = read_primary_protection(save, actors, out, none);
  qa_cleanup_pop(&guard);
  qa_cleanup_pop(&none_guard);
  qa_save_value_free(none);
  return ok;
}
static bool read_primary_protection(const qa_save_image *save, const qa_session_actor_registry *actors,
                                    qa_primary_protection *out, const qa_save_value *none) {
  const qa_provider_checkpoint *record = NULL;
  size_t record_count = 0;
  for (size_t i = 0; i < save->provider_count; i++) {
    if (save->providers[i].schema != NULL &&
        strcmp(save->providers[i].schema, schema_name) == 0) {
      record = &save->providers[i];
      record_count++;
    }
  }
  if (record_count > 1)
    qa_save_format_error(schema_name, "duplicate primary protection checkpoint");
  if (record == NULL) return true;
  if (record->provider == NULL || strcmp(record->provider, "world:gameplay") != 0 ||
      record->version != 1)
    qa_save_format_error(schema_name,
                         "unsupported primary protection checkpoint");
  qa_save_value *root =
      qa_decode_checkpoint_value(record->bytes.bytes, record->bytes.length);
  if (root == NULL) return false;
  out->root = root;
  qa_save_reader list;
  if (!qa_save_reader_init(&list, root, NULL)) return false;
  size_t count = qa_save_reader_list_count(&list);
  if (count > 0) {
    out->entries = calloc(count, sizeof *out->entries);
    if (out->entries == NULL) {
      qa_save_reader_destroy(&list);
      return false;
    }
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item(&list, i);
    qa_primary_protection_entry *entry = &out->entries[out->entry_count];
    qa_save_reader actor_field = field(&value, "actor");
    entry->saved = qa_read_saved_actor(&actor_field);
    qa_save_reader_destroy(&actor_field);
    qa_save_reader owner_field = field(&value, "owner");
    entry->owner = qa_namespaced(&owner_field);
    qa_save_reader_destroy(&owner_field);
    bool resolved = actors->resolve_saved(actors->ctx, entry->saved, &entry->actor);
    bool duplicate = false;
    if (resolved) {
      for (size_t j = 0; j < out->entry_count; j++) {
        if (out->entries[j].actor.token == entry->actor.token &&
            qa_same_actor(out->entries[j].actor.id, entry->actor.id))
          duplicate = true;
      }
    }
    if (!resolved || entry->actor.owner == NULL ||
        strcmp(entry->actor.owner, entry->owner) != 0 || duplicate)
      qa_save_reader_fail(&value, "missing or duplicate primary protection owner");
    qa_save_reader regular = field(&value, "regular");
    qa_save_reader powered = field(&value, "powered");
    bool has_regular = !qa_save_reader_is_undefined(&regular);
    bool has_powered = !qa_save_reader_is_undefined(&powered);
    if (!has_regular && !has_powered)
      qa_save_reader_fail(&value, "empty hidden primary armor");
    /* TS: readArmor(new SaveReader({ regular: regular.value ?? none, powered: powered.value ?? none })),
     * a fresh reader rooted at "save" (its errors read "save.regular...", whatever the entry), over
     * the modern branch; null as well as an absent channel reads as { kind: "none" }. */
    qa_armor_state armor;
    memset(&armor, 0, sizeof armor);
    qa_save_reader channel;
    bool regular_given = has_regular && !qa_save_reader_is_null(&regular);
    qa_save_reader_init(&channel, regular_given ? regular.value : none, "save.regular");
    armor.regular = qa_read_regular_armor(&channel);
    {
      static const char *const kinds[] = {"none", "screen", "shield"};
      bool powered_given = has_powered && !qa_save_reader_is_null(&powered);
      qa_save_reader_init(&channel, powered_given ? powered.value : none, "save.powered");
      qa_save_reader kind = field(&channel, "kind");
      armor.powered.kind = qa_save_reader_choice_string(&kind, kinds, 3);
      qa_save_reader_destroy(&kind);
      if (strcmp(armor.powered.kind, "none") != 0) {
        qa_save_reader cells = field(&channel, "cells");
        armor.powered.cells = qa_save_reader_number(&cells);
        qa_save_reader_destroy(&cells);
      }
    }
    if (armor.regular.kind == QA_REGULAR_ARMOR_SOURCE)
      qa_save_reader_fail(&regular,
                          "copied primary armor cannot own a source formula");
    entry->has_regular = has_regular;
    entry->regular = armor.regular;
    entry->has_powered = has_powered;
    entry->powered = armor.powered;
    out->entry_count++;
    qa_save_reader_destroy(&regular);
    qa_save_reader_destroy(&powered);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&list);
  out->recorded = true;
  return true;
}
