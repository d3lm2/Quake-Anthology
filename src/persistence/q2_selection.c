/* Port of src/persistence/q2-selection.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_selection.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"

static bool candidate(const qa_catalog_discovered_product *product, const char *edition, const char *game_directory) {
  if (product->expectation.family != QA_GAME_FAMILY_Q2 || strcmp(product->expectation.edition, edition) != 0 ||
      product->availability.kind != QA_CATALOG_INSTALLED)
    return false;
  if (game_directory == NULL) return true;
  char *directory = qa_node_path_basename(product->expectation.content_directory);
  bool same = qa_js_equals_ignore_case(directory, game_directory);
  free(directory);
  return same;
}

const qa_catalog_discovered_product *qa_select_q2_save_product(const qa_content_catalog *catalog, const char *edition,
                                                               const char *game_directory, const char *selected,
                                                               const char *running) {
  size_t count = 0;
  const qa_catalog_discovered_product *only = NULL;
  for (size_t i = 0; i < catalog->product_count; i++) {
    if (!candidate(&catalog->products[i], edition, game_directory)) continue;
    if (count++ == 0) only = &catalog->products[i];
  }
  if (selected != NULL) {
    for (size_t i = 0; i < catalog->product_count; i++) {
      const qa_catalog_discovered_product *product = &catalog->products[i];
      if (!candidate(product, edition, game_directory)) continue;
      if (strcmp(product->expectation.id, selected) == 0 || strcmp(product->id, selected) == 0) return product;
    }
    qa_raise("Error", 0, 0, "%s", "Selected source game does not match this save.");
  }
  if (count == 1) return only;
  for (size_t i = 0; running != NULL && i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog->products[i];
    if (candidate(product, edition, game_directory) && strcmp(product->expectation.id, running) == 0) return product;
  }
  if (game_directory != NULL) {
    /* `q2-${edition}-${gameDirectory.toLowerCase()}`: the directory's own stock game. */
    char *lowered = qa_js_to_lower_case(game_directory);
    if (lowered == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
    size_t length = strlen(edition) + strlen(lowered) + 5;
    char *stock_id = (char *)malloc(length);
    if (stock_id == NULL) {
      free(lowered);
      qa_raise("Error", 0, 0, "%s", "out of memory");
    }
    (void)snprintf(stock_id, length, "q2-%s-%s", edition, lowered);
    free(lowered);
    size_t stock_count = 0;
    const qa_catalog_discovered_product *stock = NULL;
    for (size_t i = 0; i < catalog->product_count; i++) {
      const qa_catalog_discovered_product *product = &catalog->products[i];
      if (!candidate(product, edition, game_directory) || strcmp(product->expectation.id, stock_id) != 0) continue;
      if (stock_count++ == 0) stock = product;
    }
    free(stock_id);
    if (stock_count == 1) return stock;
  }
  if (count == 0)
    qa_raise("Error", 0, 0, "Quake II game directory %s is not installed.", game_directory != NULL ? game_directory : "(unknown)");
  qa_raise("Error", 0, 0, "%s", "Select the source game for this save; more than one installed game uses its directory.");
}

/* cvars.get(name) ?? fallback */
static const char *cvar(const char *const *names, const char *const *values, size_t count, const char *name,
                        const char *fallback) {
  const char *found = fallback;
  for (size_t i = 0; i < count; i++)
    if (strcmp(names[i], name) == 0) found = values[i];
  return found;
}
qa_q2_saved_settings qa_q2_saved_settings_from(const char *const *names, const char *const *values, size_t count) {
  /* NaN !== 0 is true, as in the TS. */
  if (!(qa_js_string_to_number(cvar(names, values, count, "deathmatch", "0")) == 0))
    qa_raise("Error", 0, 0, "%s", "A deathmatch game cannot be restored from a save.");
  double skill = qa_js_string_to_number(cvar(names, values, count, "skill", "1"));
  double max_clients = trunc(qa_js_string_to_number(cvar(names, values, count, "maxclients", "1")));
  bool coop = !(qa_js_string_to_number(cvar(names, values, count, "coop", "0")) == 0);
  qa_q2_saved_settings settings;
  settings.skill = skill == 0 || skill == 1 || skill == 2 || skill == 3 ? (int)skill : 1;
  settings.mode = coop ? QA_Q2_SAVED_COOP : QA_Q2_SAVED_SINGLEPLAYER;
  /* Number.isSafeInteger(maxClients) && maxClients >= 1 && maxClients <= 64 */
  settings.max_clients = max_clients >= 1 && max_clients <= 64 ? max_clients : 1;
  return settings;
}
