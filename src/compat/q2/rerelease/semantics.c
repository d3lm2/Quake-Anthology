/* Port of src/compat/q2/rerelease/semantics.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/semantics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

static int compare_rows(const void *a, const void *b) {
  double d = ((const qa_compat_q2_rerelease_inventory_item *)a)->source_index - ((const qa_compat_q2_rerelease_inventory_item *)b)->source_index;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}
typedef struct roster { bool *indices; qa_compat_q2_rerelease_inventory_item *items; } roster;
static void roster_release(void *v) { roster *r = v; free(r->indices); free(r->items); r->indices = NULL; r->items = NULL; }
qa_compat_q2_rerelease_inventory_item *qa_compat_q2_rerelease_inventory_items_owned(
    qa_compat_q2_rerelease_guest_module *module, void *string_ctx,
    qa_guest_address (*string)(void *, const char *), size_t *out_count) {
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
  size_t capacity = (size_t)profile->client.inventory_count;
  roster r = {calloc(capacity ? capacity : 1, sizeof(bool)), calloc(capacity ? capacity : 1, sizeof(qa_compat_q2_rerelease_inventory_item))};
  qa_cleanup owned; qa_cleanup_push(&owned, roster_release, &r); /* a roster that is refused keeps nothing */
  if (!r.indices || !r.items) qa_range_error("Out of memory");
  bool *indices = r.indices; qa_compat_q2_rerelease_inventory_item *items = r.items;
  size_t count = 0;
  const qa_compat_q2_rerelease_inventory_row *remaining = NULL;
  for (size_t i = 0; i < profile->inventory_count; i++) {
    const qa_compat_q2_rerelease_inventory_row *row = &profile->inventory[i];
    if (strcmp(row->source_kind, "remaining") == 0) { if (!remaining) remaining = row; continue; } /* find: the first */
    double value;
    if (strcmp(row->source_kind, "index") == 0) value = row->source_index;
    else {
      qa_guest_call_value arg = qa_compat_q2_call_pointer(true, string(string_ctx, row->source_name));
      qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_call_game(module, "Bot_GetItemID", &arg, 1, NULL, NULL);
      if (result.kind != QA_GUEST_VALUE_INT32) qa_raise("Error", 0, 0, "Native item roster mismatch: %s", row->item);
      value = result.number_value;
    }
    /* The guest names the index: it is used only once it is inside the roster. */
    if (!(value >= 0) || value >= profile->client.inventory_count || value != (double)(size_t)value || indices[(size_t)value] || count >= capacity)
      qa_raise("Error", 0, 0, "Native item roster mismatch: %s", row->item);
    indices[(size_t)value] = true;
    items[count++] = (qa_compat_q2_rerelease_inventory_item){row->item, value, row->capacity_is_ammo, row->capacity_source_index, row->capacity_count};
  }
  if (remaining) {
    size_t found = 0; double unnamed = -1;
    for (size_t i = 0; i < capacity; i++) if (!indices[i]) { if (found == 0) unnamed = (double)i; found++; }
    if (found != 1) qa_raise("Error", 0, 0, "Source inventory declaration requires exactly one remaining slot");
    items[count++] = (qa_compat_q2_rerelease_inventory_item){remaining->item, unnamed, remaining->capacity_is_ammo,
                                                            remaining->capacity_source_index, remaining->capacity_count};
  }
  /* items.find(row => row.item === cells)?.sourceIndex !== cellsIndex: the first row of that item. */
  bool cells_ok = false;
  for (size_t i = 0; i < count; i++) if (strcmp(items[i].item, profile->armor.cells) == 0) { cells_ok = items[i].source_index == profile->armor.cells_index; break; }
  if ((double)count != profile->client.inventory_count || !cells_ok)
    qa_raise("Error", 0, 0, "Native inventory roster differs from the declared source storage");
  qa_js_sort(items, count, sizeof *items, compare_rows);
  qa_cleanup_pop(&owned);
  free(indices);
  if (out_count) *out_count = count;
  return items;
}
/* One process-wide list, rewritten by each call: kept for the caller outside compat/q2 that holds its result
 * (app/bootstrap/simulation/rerelease_guest_services.c). The list is replaced only once a roster is whole. */
const qa_compat_q2_rerelease_inventory_item *qa_compat_q2_rerelease_inventory_items(
    qa_compat_q2_rerelease_guest_module *module, void *string_ctx,
    qa_guest_address (*string)(void *, const char *), size_t *out_count) {
  static qa_compat_q2_rerelease_inventory_item *items = NULL; static size_t items_cap = 0;
  size_t count = 0;
  qa_compat_q2_rerelease_inventory_item *fresh = qa_compat_q2_rerelease_inventory_items_owned(module, string_ctx, string, &count);
  if (count > items_cap) {
    qa_compat_q2_rerelease_inventory_item *grown = realloc(items, count * sizeof *items);
    if (!grown) { free(fresh); qa_range_error("Out of memory"); }
    items = grown; items_cap = count;
  }
  if (count) memcpy(items, fresh, count * sizeof *items);
  free(fresh);
  if (out_count) *out_count = count;
  return items;
}
