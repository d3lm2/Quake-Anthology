/* Port of src/app/bootstrap/simulation/native-q2-rerelease-pak.ts: a rerelease Quake II native save as the
 * Nightdive engine keeps it: `saveN.pak.gz`, a gzip PACK holding `game.ssv` (the game module's WriteGameJson),
 * `server.ssv` ("EVAS" binary), and for every level of the unit `MAP.sav` (WriteLevelJson) with `MAP.sv2`
 * (configstrings and area portal state). The engine builds the pak from the same `save/current/` working folder
 * id's engine used; its strings name `save/{}/{}.sav`, `save/{}/{}.sv2` and `{}/save/{}.pak.gz`.
 * C accommodations:
 *  - `now: Date` is its getTime() milliseconds; `portalCount` (number | Promise<number>) is a synchronous
 *    callback, so the TS async functions are synchronous here.
 *  - The `[string, Uint8Array][]` entry list is qa_app_sim_q2_rerelease_pak_entries: owned names and bytes in
 *    order (a repeated name stays repeated, as in the TS array).
 *  - SaveFormatError and the other TS throws raise through qa_raise. A raise leaves what was already built in
 *    `out`, which was initialized by the call: a caller that catches releases it with the matching _free.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_RERELEASE_PAK_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_RERELEASE_PAK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include "qa/persistence/kex_q2_save.h"

/* TS Q2RereleasePakContext. */
typedef struct qa_app_sim_q2_rerelease_pak_context {
  const char *spawn_point;
  double now_milliseconds; /* now.getTime() */
  /* The area portal count of a map, which the engine compares with its own when it reads the level file. */
  void *ctx;
  double (*portal_count)(void *ctx, const char *map);
} qa_app_sim_q2_rerelease_pak_context;

/* TS rereleaseAutosaveName: the slot that the engine writes an autosave to; a cooperative game keeps its own.
 * Static text. */
const char *qa_app_sim_rerelease_autosave_name(bool coop);
/* TS rereleaseSlotName: q2SaveSlotName of the name without a `.pak.gz` suffix (any case). Malloc'd. */
char *qa_app_sim_rerelease_slot_name(const char *name);

typedef struct qa_app_sim_q2_rerelease_pak_entry { char *name; uint8_t *bytes; size_t length; } qa_app_sim_q2_rerelease_pak_entry;
typedef struct qa_app_sim_q2_rerelease_pak_entries {
  qa_app_sim_q2_rerelease_pak_entry *entries; size_t count, capacity;
} qa_app_sim_q2_rerelease_pak_entries;
void qa_app_sim_q2_rerelease_pak_entries_free(qa_app_sim_q2_rerelease_pak_entries *entries);

/* TS q2RereleasePakEntries: the archive's entries before packing, which is also what the sidecar's digest covers. */
void qa_app_sim_q2_rerelease_pak_entries_of(const qa_app_sim_q2_rerelease_native_save *save,
                                            const qa_app_sim_q2_rerelease_pak_context *context,
                                            qa_app_sim_q2_rerelease_pak_entries *out);
/* TS q2RereleasePak: the pak a rerelease save is stored as (packKexQ2SaveArchive of the entries). Malloc'd. */
uint8_t *qa_app_sim_q2_rerelease_pak(const qa_app_sim_q2_rerelease_native_save *save,
                                     const qa_app_sim_q2_rerelease_pak_context *context, size_t *out_length);
/* TS readQ2RereleasePak: everything a retail pak holds. Entries beginning with a dot are the engine's map-restart
 * backup and are not part of the saved game; any other entry that is not a `.sav` and `.sv2` pair, game.ssv or
 * server.ssv is refused. `out` owns everything (qa_app_sim_q2_rerelease_native_import_free). */
void qa_app_sim_read_q2_rerelease_pak(const qa_kex_q2_save_archive *archive, qa_app_sim_q2_rerelease_native_import *out);
#endif
