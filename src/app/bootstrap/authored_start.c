/* Port of src/app/bootstrap/authored-start.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/authored_start.h"
#include <string.h>
#include "qa/content/q2/foundation/start_items.h"
bool qa_app_authored_campaign_start_for(const qa_authored_start_catalog *catalog, const char *selected_map,
                                    qa_app_authored_campaign_start *out) {
  const char *stem = selected_map;
  if (strncmp(stem, "maps/", 5) == 0) stem += 5;
  size_t len = strlen(stem);
  if (len >= 4 && strcmp(stem + len - 4, ".bsp") == 0) len -= 4;
  if (!catalog) return false;
  /* entry.path === `maps/${stem}.bsp`, compared in place (no fixed buffer: TS has no length cap). */
  const qa_authored_start_map *start = NULL;
  for (size_t i = 0; i < catalog->start_count; i++) {
    const char *path = catalog->starts[i].path;
    if (strncmp(path, "maps/", 5) == 0 && strncmp(path + 5, stem, len) == 0 && strlen(path + 5) >= len &&
        strcmp(path + 5 + len, ".bsp") == 0) {
      start = &catalog->starts[i];
      break;
    }
  }
  if (!start) return false;
  size_t item_count = 0;
  qa_q2_start_item *items = qa_q2_parse_start_items(start->start_items, &item_count);
  qa_q2_start_items_free(items, item_count);
  out->target = qa_app_parse_q2_travel(start->bsp);
  out->start_items = start->start_items;
  return true;
}
