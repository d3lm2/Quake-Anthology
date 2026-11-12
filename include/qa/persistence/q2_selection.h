/*
 * Port of src/persistence/q2-selection.ts: the product a Quake II save belongs
 * to, and the launch options the server settings a save recorded stand for.
 * Plain TS Errors raise Error with the same message. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_SELECTION_H
#define QA_PERSISTENCE_Q2_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/index.h"

/* TS selectQ2SaveProduct. A save folder is named by its game directory (`baseq2/save/NAME`), and a retail pak
 * records it in the `gamename` cvar. Several products can share one directory, so the selection, then the
 * running product, then the directory's own stock game decide. `edition` is "classic" or "rerelease";
 * `game_directory` NULL is the TS null; `selected` and `running` NULL are undefined. The product is borrowed
 * from the catalog. */
const qa_catalog_discovered_product *qa_select_q2_save_product(const qa_content_catalog *catalog, const char *edition,
                                                               const char *game_directory, const char *selected,
                                                               const char *running);

/* TS Q2SavedSettings. */
typedef enum qa_q2_saved_mode {
  QA_Q2_SAVED_SINGLEPLAYER,
  QA_Q2_SAVED_COOP
} qa_q2_saved_mode;
typedef struct qa_q2_saved_settings {
  int skill; /* 0 | 1 | 2 | 3 */
  qa_q2_saved_mode mode;
  double max_clients;
} qa_q2_saved_settings;
/* TS q2SavedSettings: `names[i]`/`values[i]` are the cvars a save recorded (a later repeat of a name wins, as in
 * the TS Map). Deathmatch games cannot be saved or loaded (Error). */
qa_q2_saved_settings qa_q2_saved_settings_from(const char *const *names, const char *const *values, size_t count);

#endif
