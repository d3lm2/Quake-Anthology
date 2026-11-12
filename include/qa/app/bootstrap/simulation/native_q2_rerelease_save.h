/* Port of src/app/bootstrap/simulation/native-q2-rerelease-save.ts.
 * Ownership: a decoded save keeps its checkpoint tree in `root` and every
 * string, byte view and list borrows it; lists are malloc'd. An encode
 * input is borrowed. Release decoded saves with
 * qa_app_sim_q2_rerelease_native_save_free. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_REREELASE_SAVE_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_REREELASE_SAVE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/q2/rerelease/deferred_damage.h"
#include "qa/compat/q2/rerelease/foreign_actors.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/session.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/value.h"

typedef struct qa_app_sim_q2_rerelease_native_identity { qa_module_identity module; const char *map; } qa_app_sim_q2_rerelease_native_identity;
typedef struct qa_app_sim_q2_rerelease_configstring { double index; const char *value; } qa_app_sim_q2_rerelease_configstring;
typedef struct qa_app_sim_q2_rerelease_portal { double portal; bool open; } qa_app_sim_q2_rerelease_portal;
typedef struct qa_app_sim_q2_rerelease_level_state {
  qa_app_sim_q2_rerelease_configstring *configstrings; size_t configstring_count;
  qa_app_sim_q2_rerelease_portal *portals; size_t portal_count;
} qa_app_sim_q2_rerelease_level_state;
typedef struct qa_app_sim_q2_rerelease_visited_level {
  qa_app_sim_q2_rerelease_level_state state;
  double version; /* TS literal 1 */
  const char *map;
  qa_compat_q2_rerelease_source_save level;
  /* The key order the record encodes with. A level travel recorded (and a pak import) is the TS object literal
   * { version, map, level, configstrings, portals }; one decoded from a save is { version, ...readLevel, map, level },
   * i.e. { version, configstrings, portals, map, level }, which a later save writes back in that order. */
  bool decoded_order;
} qa_app_sim_q2_rerelease_visited_level;
/* api {kind "q2-rerelease-game", version 2023} and abi "windows-x86-64" are constants of the format. */
typedef struct qa_app_sim_q2_rerelease_native_save {
  qa_module_identity module; const char *map;
  bool autosave;
  qa_app_sim_q2_rerelease_level_state server; qa_byte_view server_cvars;
  /* level_absent is the TS `level: null`: the current map's level state was never written (an autosave taken on
   * entering the map). `level` is then all zero. */
  qa_compat_q2_rerelease_source_save game, level; bool level_absent;
  qa_app_sim_q2_rerelease_visited_level *visited_levels; size_t visited_level_count;
  qa_save_value *root; /* owned when decoded */
} qa_app_sim_q2_rerelease_native_save;

/* TS Q2RereleaseNativeImport: the files of a rerelease save archive read back, before they are bound to a launched
 * module. Everything is owned by the import (qa_app_sim_q2_rerelease_native_import_free): `owned` lists every
 * malloc'd block the fields point into. */
typedef struct qa_app_sim_q2_rerelease_latched_cvar { const char *name, *value; } qa_app_sim_q2_rerelease_latched_cvar;
typedef struct qa_app_sim_q2_rerelease_native_import {
  const char *map, *spawn_point;
  bool autosave;
  qa_app_sim_q2_rerelease_level_state server;
  qa_app_sim_q2_rerelease_latched_cvar *latched; size_t latched_count;
  qa_compat_q2_rerelease_source_save game, level; bool level_absent;
  qa_app_sim_q2_rerelease_visited_level *visited_levels; size_t visited_level_count;
  void **owned; size_t owned_count, owned_capacity;
} qa_app_sim_q2_rerelease_native_import;
void qa_app_sim_q2_rerelease_native_import_free(qa_app_sim_q2_rerelease_native_import *import);

/* TS NativeQ2Import (source-strategy.ts): the files of an original save folder or archive, read back and not yet
 * bound to a module. `rerelease` is the TS isRereleaseImport test (the game is a RereleaseSourceSave, not bytes)
 * and selects the member in use. Declared here because SimulationOptions.nativeImport (types.h) carries it. Both
 * imports are borrowed and must outlive the simulation that starts from them: its visited levels stay referenced. */
typedef struct qa_app_sim_native_q2_import {
  bool rerelease;
  const qa_q2_classic_original_import *classic;
  const qa_app_sim_q2_rerelease_native_import *rerelease_import;
} qa_app_sim_native_q2_import;

/* TS readRereleaseSourceSave: lists malloc'd (free with _source_save_free), bytes borrow the reader's tree. */
qa_compat_q2_rerelease_source_save qa_app_sim_read_rerelease_source_save(const qa_save_reader *reader);
void qa_app_sim_rerelease_source_save_free(qa_compat_q2_rerelease_source_save *save);
/* RereleaseSourceSave { native, deferredDamage, projections } as its checkpoint tree (owned). */
qa_save_value *qa_app_sim_rerelease_source_save_value(const qa_compat_q2_rerelease_source_save *save);
/* TS decodeQ2RereleaseNativeSave; record borrows its bytes. Raises as the TS throws, releasing what it decoded. */
void qa_app_sim_decode_q2_rerelease_native_save(const qa_provider_checkpoint *record, qa_app_sim_q2_rerelease_native_identity expected,
                                                qa_app_sim_q2_rerelease_native_save *out);
/* TS encodeQ2RereleaseNativeSave: out->bytes is malloc'd (caller frees); provider borrows save->module.id. */
void qa_app_sim_encode_q2_rerelease_native_save(const qa_app_sim_q2_rerelease_native_save *save, qa_provider_checkpoint *out);
void qa_app_sim_q2_rerelease_native_save_free(qa_app_sim_q2_rerelease_native_save *save);
#endif
