/* Port of src/compat/q2/native-mod-pickups.ts: rules travel with the current
 * resource binding; this object owns only source execution and inventory
 * delegates. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_pickups.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_throw.h"
#include "qa/contracts/native_mod_items.h"

static const char *channel_name(qa_protection_channel channel) { return channel == QA_PROTECTION_POWERED ? "powered" : "regular"; }
static const char *call_returns(const void *call) { return ((const qa_native_mod_source_call *)call)->returns; }
void qa_compat_q2_validate_native_mod_pickups(const qa_native_mod_declaration *d) {
  for (size_t i = 0; i < d->pickup_count; i++) {
    const qa_mod_pickup_rule *rule = &d->pickups[i].rule;
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) if (strcmp(d->pickups[j].rule.id, rule->id) == 0) duplicate = true;
    if (d->clients == NULL || duplicate || rule->id[0] == 0 || rule->offered_count == 0) qa_raise("Error", 0, 0, "Native pickups require unique rules and source client admission");
    for (size_t k = 0; k < rule->offered_count; k++) {
      for (size_t j = 0; j < i; j++) for (size_t m = 0; m < d->pickups[j].rule.offered_count; m++)
        if (strcmp(d->pickups[j].rule.offered[m], rule->offered[k]) == 0) qa_raise("Error", 0, 0, "Ambiguous native original pickup item");
      for (size_t m = 0; m < k; m++) if (strcmp(rule->offered[m], rule->offered[k]) == 0) qa_raise("Error", 0, 0, "Ambiguous native original pickup item");
    }
    for (size_t w = 0; w < rule->writes.count; w++) {
      const qa_pickup_write *resource = &rule->writes.values[w];
      if (resource->kind == QA_PICKUP_RESOURCE_PROTECTION) {
        bool found = false;
        for (size_t p = 0; p < d->protection_count; p++) if (strcmp(d->protection[p].channel, channel_name(resource->channel)) == 0) found = true;
        if (!found) qa_raise("Error", 0, 0, "Native pickup has no protection owner");
      } else if (strcmp(resource->fields, "capacity") != 0) {
        bool found = false;
        if (d->items != NULL) for (size_t n = 0; n < d->items->definition_count; n++) if (strcmp(d->items->definitions[n].item, resource->item) == 0) found = true;
        for (size_t r = 0; r < d->actor_record_count && !found; r++) for (size_t f = 0; f < d->actor_records[r].field_count; f++) {
          const qa_native_mod_actor_field *field = &d->actor_records[r].fields[f];
          if (strcmp(field->binding, "inventory") == 0 && strcmp(field->item, resource->item) == 0) found = true;
        }
        if (!found) qa_raise("Error", 0, 0, "Native pickup has no declared inventory storage");
      }
      if (resource->kind == QA_PICKUP_RESOURCE_INVENTORY && strcmp(resource->fields, "count") != 0) {
        bool found = false;
        if (d->items != NULL) for (size_t n = 0; n < d->items->storage_count; n++) {
          const qa_native_item_storage *storage = &d->items->storage[n];
          if (strcmp(storage->kind, "counter") == 0 && strcmp(storage->item, resource->item) == 0 && strcmp(storage->capacity.kind, "field") == 0) found = true;
        }
        for (size_t r = 0; r < d->actor_record_count && !found; r++) for (size_t f = 0; f < d->actor_records[r].field_count; f++) {
          const qa_native_mod_actor_field *field = &d->actor_records[r].fields[f];
          if (strcmp(field->binding, "inventory-capacity") == 0 && strcmp(field->item, resource->item) == 0) found = true;
        }
        if (!found) qa_raise("Error", 0, 0, "Native pickup has no declared inventory capacity storage");
      }
    }
    const qa_original_pickup_operation *operation = &rule->operation;
    if ((!operation->is_gate_then_grant && strcmp(call_returns(operation->grant), "void") == 0)
        || (operation->is_gate_then_grant && (strcmp(call_returns(operation->gate), "void") == 0
            || (strcmp(operation->grant_accepts, "nonzero") == 0 && strcmp(call_returns(operation->grant), "void") == 0))))
      qa_raise("Error", 0, 0, "Native pickup requires its declared source decision");
  }
}
typedef struct qa_compat_q2_native_mod_pickups pickups;
typedef struct { pickups *s; qa_actor_id actor; const qa_native_mod_pickup *definition; } rule_ctx;
/* protection: the actor's rules that write each channel (regular, powered), kept with the actor's rules: TS returns
 * a new array per call, and a caller may hold both channels' lists (or two actors' lists) at once. */
typedef struct { qa_actor_id actor; qa_original_pickup_rule *rules; rule_ctx *contexts; qa_original_pickup_rule *protection[2]; size_t protection_count[2]; } actor_rules;
typedef struct { qa_actor_id actor; qa_owned_actor owned; bool bound; int64_t binding; qa_original_pickup_rule *rules; } delegate_entry;
struct qa_compat_q2_native_mod_pickups {
  const qa_native_mod_pickup *definitions; size_t definition_count;
  const qa_mod_host_services *services; qa_provider_id owner;
  qa_compat_q2_native_mod_pickup_operations operations;
  bool active; int depth;
  actor_rules *rules; size_t rule_count, rule_capacity;
  delegate_entry *delegates; size_t delegate_count, delegate_capacity;
};
static bool write_is_inventory(const qa_original_pickup_rule *rule) {
  for (size_t i = 0; i < rule->writes.count; i++) if (rule->writes.values[i].kind == QA_PICKUP_RESOURCE_INVENTORY) return true;
  return false;
}
typedef struct {
  pickups *s; qa_actor_id actor; const qa_native_mod_pickup *definition; qa_original_pickup_offer offer; qa_original_pickup_execution observer;
  qa_compat_q2_native_pickup_inputs inputs;
  qa_original_pickup_decision decision;
} take_scope;
static bool take_current(const take_scope *t) {
  const pickups *s = t->s;
  return t->observer.current(t->observer.current_ctx) && s->active && qa_session_actor_registry_is_live(s->services->actors, t->actor)
    && s->operations.eligible(s->operations.ctx, t->actor) && qa_session_actor_registry_is_live(s->services->actors, t->offer.pickup);
}
static void take_operation(void *v) {
  take_scope *t = v; pickups *s = t->s; const qa_original_pickup_operation *operation = &t->definition->rule.operation;
  t->decision = QA_ORIGINAL_PICKUP_REFUSED;
  double value;
  if (operation->is_gate_then_grant) {
    bool has = s->operations.invoke(s->operations.ctx, operation->gate, t->inputs, &value);
    if (!has || value == 0 || !take_current(t)) return;
  }
  bool has = s->operations.invoke(s->operations.ctx, operation->grant, t->inputs, &value);
  if (!has || !take_current(t)) return;
  bool always = operation->is_gate_then_grant && strcmp(operation->grant_accepts, "always") == 0;
  t->decision = always || value != 0 ? QA_ORIGINAL_PICKUP_ACCEPTED : QA_ORIGINAL_PICKUP_REFUSED;
}
static void take_observe(void *v) {
  take_scope *t = v;
  t->s->operations.observe(t->s->operations.ctx, t->actor, &t->observer, t, take_operation);
}
static void take_context(void *v) {
  take_scope *t = v;
  t->s->operations.context(t->s->operations.ctx, t->definition, &t->offer, t->inputs, t, take_observe);
}
static qa_mod_runtime_value float_input(double value) { qa_mod_runtime_value out; memset(&out, 0, sizeof out); out.kind = "float"; out.number_value = value; return out; }
static qa_original_pickup_decision take(void *v, qa_original_pickup_offer offer, qa_original_pickup_execution observer) {
  rule_ctx *rule = v; pickups *s = rule->s;
  s->operations.current(s->operations.ctx);
  take_scope scope = {s, rule->actor, rule->definition, offer, observer, {NULL, 0}, QA_ORIGINAL_PICKUP_REFUSED};
  bool offered = false;
  for (size_t i = 0; i < rule->definition->rule.offered_count; i++) if (strcmp(rule->definition->rule.offered[i], offer.item) == 0) offered = true;
  if (!qa_same_actor(offer.recipient, rule->actor) || !offered || !take_current(&scope)) qa_raise("Error", 0, 0, "Native pickup rule is no longer current");
  qa_mod_runtime_value self, other, item;
  memset(&self, 0, sizeof self); memset(&other, 0, sizeof other); memset(&item, 0, sizeof item);
  self.kind = "actor"; self.actor_value = &rule->actor;
  other.kind = "actor"; other.actor_value = &offer.pickup;
  item.kind = "string"; item.string_value = offer.item;
  qa_compat_q2_native_pickup_input rows[7] = {
    {"self", self}, {"other", other}, {"item", item},
    {"time", float_input(offer.time.kind == QA_SOURCE_TIME_SECONDS ? offer.time.value : offer.time.value / 1000)},
    {"pickup-count", float_input(offer.count.is_override ? offer.count.amount : 0)},
    {"pickup-has-count", float_input(offer.count.is_override ? 1 : 0)},
    {"pickup-dropped", float_input(offer.dropped ? 1 : 0)},
  };
  scope.inputs.rows = rows; scope.inputs.count = 7;
  s->depth++;
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(take_context, &scope, &thrown);
  s->depth--;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return scope.decision;
}
static actor_rules *find_rules(const pickups *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->rule_count; i++) if (qa_same_actor(s->rules[i].actor, actor)) return &s->rules[i];
  return NULL;
}
static actor_rules *rules_for(pickups *s, qa_actor_id actor) {
  actor_rules *found = find_rules(s, actor);
  if (found != NULL) return found;
  if (s->rule_count == s->rule_capacity) {
    size_t capacity = s->rule_capacity ? s->rule_capacity * 2 : 4;
    actor_rules *grown = realloc(s->rules, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native pickups out of memory");
    s->rules = grown; s->rule_capacity = capacity;
  }
  actor_rules *row = &s->rules[s->rule_count++];
  row->actor = actor;
  row->rules = calloc(s->definition_count ? s->definition_count : 1, sizeof *row->rules);
  row->contexts = calloc(s->definition_count ? s->definition_count : 1, sizeof *row->contexts);
  if (row->rules == NULL || row->contexts == NULL) qa_range_error("Native pickups out of memory");
  for (size_t i = 0; i < s->definition_count; i++) {
    const qa_native_mod_pickup *definition = &s->definitions[i];
    row->contexts[i] = (rule_ctx){s, actor, definition};
    row->rules[i] = (qa_original_pickup_rule){&row->contexts[i], definition->rule.id, definition->rule.offered, definition->rule.offered_count, definition->rule.writes, take};
  }
  for (int c = 0; c < 2; c++) {
    qa_protection_channel channel = c == 0 ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED;
    row->protection[c] = calloc(s->definition_count ? s->definition_count : 1, sizeof *row->protection[c]);
    if (row->protection[c] == NULL) qa_range_error("Native pickups out of memory");
    row->protection_count[c] = 0;
    for (size_t i = 0; i < s->definition_count; i++) for (size_t w = 0; w < row->rules[i].writes.count; w++) {
      const qa_pickup_write *write = &row->rules[i].writes.values[w];
      if (write->kind == QA_PICKUP_RESOURCE_PROTECTION && write->channel == channel) { row->protection[c][row->protection_count[c]++] = row->rules[i]; break; }
    }
  }
  return row;
}
static void forget_rules(pickups *s, qa_actor_id actor) {
  actor_rules *found = find_rules(s, actor);
  if (found == NULL) return;
  free(found->rules); free(found->contexts); free(found->protection[0]); free(found->protection[1]);
  *found = s->rules[--s->rule_count];
}
qa_compat_q2_native_mod_pickups *qa_compat_q2_native_mod_pickups_create(const qa_native_mod_pickup *definitions, size_t definition_count, const qa_mod_host_services *services, qa_provider_id owner, qa_compat_q2_native_mod_pickup_operations operations) {
  pickups *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Native pickups out of memory");
  s->definitions = definitions; s->definition_count = definition_count; s->services = services; s->owner = owner; s->operations = operations;
  return s;
}
const qa_original_pickup_rule *qa_compat_q2_native_mod_pickups_protection(pickups *s, qa_actor_id actor, qa_protection_channel channel, size_t *out_count) {
  const actor_rules *row = rules_for(s, actor);
  int c = channel == QA_PROTECTION_POWERED ? 1 : 0;
  *out_count = row->protection_count[c];
  return row->protection[c];
}
void qa_compat_q2_native_mod_pickups_activate(pickups *s) {
  s->operations.current(s->operations.ctx);
  s->active = true;
  if (s->services->clients == NULL) return;
  size_t count = 0;
  const qa_mod_client_identity *clients = s->services->clients->clients(s->services->clients->ctx, &count);
  for (size_t i = 0; i < count; i++) qa_compat_q2_native_mod_pickups_bind_actor(s, clients[i].actor);
}
static delegate_entry *find_delegate(const pickups *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->delegate_count; i++) if (qa_same_actor(s->delegates[i].actor, actor)) return &s->delegates[i];
  return NULL;
}
void qa_compat_q2_native_mod_pickups_bind_actor(pickups *s, qa_actor_id actor) {
  if (!s->active || !s->operations.eligible(s->operations.ctx, actor) || find_delegate(s, actor) != NULL) return;
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(s->services->actors, actor, &owned)) qa_raise("Error", 0, 0, "Native pickup recipient is retired");
  const actor_rules *row = rules_for(s, actor);
  qa_original_pickup_rule *rules = calloc(s->definition_count ? s->definition_count : 1, sizeof *rules);
  if (rules == NULL) qa_range_error("Native pickups out of memory");
  size_t count = 0;
  for (size_t i = 0; i < s->definition_count; i++) if (write_is_inventory(&row->rules[i])) rules[count++] = row->rules[i];
  delegate_entry entry = {actor, owned, false, 0, rules};
  if (count != 0) {
    qa_cleanup node; qa_cleanup_push(&node, free, rules); /* a refused binding raises */
    entry.binding = qa_shared_inventory_table_bind_pickup(s->services->inventory, owned, (qa_inventory_pickup_binding){s->owner, rules, count});
    qa_cleanup_pop(&node);
    entry.bound = true;
  }
  if (s->delegate_count == s->delegate_capacity) {
    size_t capacity = s->delegate_capacity ? s->delegate_capacity * 2 : 4;
    delegate_entry *grown = realloc(s->delegates, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native pickups out of memory");
    s->delegates = grown; s->delegate_capacity = capacity;
  }
  s->delegates[s->delegate_count++] = entry;
}
void qa_compat_q2_native_mod_pickups_assert_idle(const pickups *s) {
  if (s->depth != 0) qa_raise("Error", 0, 0, "Cannot save or restore during Native original pickup execution");
}
typedef struct { pickups *s; qa_owned_actor owned; int64_t binding; } unbind_call;
static void unbind_body(void *v) { unbind_call *c = v; qa_shared_inventory_table_unbind_pickup(c->s->services->inventory, c->owned, c->binding); }
/* TS release(): the actor is forgotten first, then every remover runs in its own try and the failures leave as one
 * AggregateError. */
void qa_compat_q2_native_mod_pickups_release(pickups *s, qa_actor_id actor) {
  delegate_entry *found = find_delegate(s, actor);
  delegate_entry entry = {0};
  bool had = found != NULL;
  if (had) { entry = *found; *found = s->delegates[--s->delegate_count]; }
  forget_rules(s, actor);
  size_t errors = 0;
  if (had) {
    if (entry.bound) { unbind_call call = {s, entry.owned, entry.binding}; qa_caught_error caught; if (!qa_try(unbind_body, &call, &caught)) errors++; }
    free(entry.rules);
  }
  if (errors != 0) qa_raise("AggregateError", 0, 0, "Native pickup delegate cleanup failed");
}
typedef struct { pickups *s; qa_actor_id actor; } release_call;
static void release_body(void *v) { release_call *c = v; qa_compat_q2_native_mod_pickups_release(c->s, c->actor); }
/* TS close(): every delegate is released in its own try; the failures leave as one AggregateError. */
void qa_compat_q2_native_mod_pickups_close(pickups *s) {
  s->active = false;
  while (s->rule_count != 0) forget_rules(s, s->rules[0].actor);
  size_t errors = 0;
  while (s->delegate_count != 0) { release_call call = {s, s->delegates[0].actor}; qa_caught_error caught; if (!qa_try(release_body, &call, &caught)) errors++; }
  if (errors != 0) qa_raise("AggregateError", 0, 0, "Native pickup cleanup failed");
}
void qa_compat_q2_native_mod_pickups_destroy(pickups *s) {
  if (s == NULL) return;
  qa_compat_q2_native_mod_pickups_close(s);
  free(s->rules); free(s->delegates); free(s);
}
