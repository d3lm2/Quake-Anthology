/*
 * Port of src/settings/layers.ts: settings layering. The high level is the
 * engine's own configuration and the default for every game: bindings and the
 * shared settings, stored under the canonical cvar names declared in
 * core/cvars/setting_declarations.h. Each game's native file is a lower level
 * that receives the full effective set. Precedence, highest first:
 *   1. the game's local overrides (hand-edits to its native file, `bind -local`,
 *      `unbind -local`, `set -local`);
 *   2. the high level;
 *   3. the game's own default.cfg and cvar defaults, which sit beneath both.
 * C accommodations:
 *  - Maps and Sets are qa_layer_map (settings/setting_spellings.h). A
 *    Map<string, LayerBind> entry is id = map key, name = LayerBind.key,
 *    value = command (NULL unbinds). A Map<string, LayerCvar> entry is id = map
 *    key, name = LayerCvar.name, value. A Map<string, string> entry is id, value.
 *    EffectiveSet's bind arrays are maps too (their ids are the folded keys the
 *    TS builds them from), read in entry order.
 *  - async methods are synchronous. LayerStore/GameLayers I/O failures raise,
 *    as the TS promise rejects.
 *  - resetValue is fn + ctx returning a malloc'd string or NULL (undefined).
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_LAYERS_H
#define QA_SETTINGS_LAYERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/settings/config.h"
#include "qa/settings/setting_spellings.h"

/* TS LayerSnapshot: what the engine knows of a game's native file, in the engine's own spelling of keys and commands. */
typedef struct qa_layer_snapshot {
  qa_layer_map binds;
  qa_layer_map cvars;
} qa_layer_snapshot;
/* TS HighLevel: settings is canonical cvar name to canonical value; a NULL command unbinds in every game. */
typedef struct qa_high_level {
  qa_layer_map settings;
  qa_layer_map binds;
} qa_high_level;
/* TS GameOverrides. */
typedef struct qa_game_overrides {
  qa_layer_map binds;
  qa_layer_map cvars;
} qa_game_overrides;
/* TS EffectiveSet: bound keys; keys a layer unbinds; the canonical settings this game has. */
typedef struct qa_effective_set {
  qa_layer_map binds;
  qa_layer_map unbound;
  qa_layer_map cvars;
} qa_effective_set;
/* TS WrittenRecord: what the engine last wrote to a game's native file. */
typedef struct qa_written_record {
  qa_layer_map binds;
  qa_layer_map cvars;
} qa_written_record;
/* One `[key, command]` pair of the TS binding lists. */
typedef struct qa_layer_pair {
  const char *key;
  const char *command;
} qa_layer_pair;

void qa_layer_snapshot_init(qa_layer_snapshot *snapshot);
void qa_layer_snapshot_destroy(qa_layer_snapshot *snapshot);
/* TS emptyHigh / emptyOverrides. */
void qa_high_level_init(qa_high_level *high);
void qa_high_level_destroy(qa_high_level *high);
void qa_game_overrides_init(qa_game_overrides *overrides);
void qa_game_overrides_destroy(qa_game_overrides *overrides);
void qa_effective_set_init(qa_effective_set *set);
void qa_effective_set_destroy(qa_effective_set *set);
void qa_written_record_init(qa_written_record *record);
void qa_written_record_destroy(qa_written_record *record);

/* TS effectiveSet: the high level mapped, then the game's real overrides. `out` is initialized by the call. */
void qa_effective_set_build(qa_layer_format format, const qa_high_level *high, const qa_game_overrides *overrides,
                            qa_effective_set *out);
/* TS sameValue: numerically equal when both are numbers. */
bool qa_layer_same_value(const char *a, const char *b);
/* TS detectHandEdits: folds the differences between a game's native file and
 * what the engine last wrote into the game's overrides; true when they changed. */
bool qa_detect_hand_edits(qa_layer_format format, const qa_layer_snapshot *file, const qa_written_record *record,
                          const qa_high_level *high, qa_game_overrides *overrides);
/* TS seedHigh: `default_binds` NULL is the empty default. `out` is initialized by the call. */
void qa_seed_high(qa_layer_format format, const qa_layer_snapshot *file, const qa_layer_map *default_binds,
                  qa_high_level *out);

/* TS AbsorbContext. */
typedef char *(*qa_layer_reset_value_fn)(void *ctx, const char *name);
typedef struct qa_absorb_context {
  const qa_layer_map *default_binds;
  qa_layer_reset_value_fn reset_value;
  void *reset_value_ctx;
  const qa_layer_map *local_binds; /* Set of folded keys */
  const qa_layer_map *local_cvars; /* Set of canonical names */
} qa_absorb_context;
typedef struct qa_absorb_touched {
  bool high;
  bool overrides;
} qa_absorb_touched;
/* TS absorbChanges: `binds` are the current `[key, command]` pairs, `cvars` the current canonical settings. */
qa_absorb_touched qa_absorb_changes(qa_layer_format format, const qa_effective_set *loaded, const qa_layer_pair *binds,
                                    size_t bind_count, const qa_layer_map *cvars, const qa_absorb_context *context,
                                    qa_high_level *high, qa_game_overrides *overrides);

/* TS LayerStore: the high level and each game's layer state persist as JSON beside the engine's other settings. */
typedef struct qa_layer_store {
  qa_config_store store;
} qa_layer_store;
void qa_layer_store_init(qa_layer_store *store, const char *root);
void qa_layer_store_destroy(qa_layer_store *store);
/* False is the TS null (no game has ever written a high level). `out` is initialized by the call. */
bool qa_layer_store_load_high(const qa_layer_store *store, qa_high_level *out);
void qa_layer_store_save_high(const qa_layer_store *store, const qa_high_level *high);
/* `out_overrides` and `out_record` are initialized by the call; *out_has_record is false for the TS null record. */
void qa_layer_store_load_game(const qa_layer_store *store, const char *id, qa_game_overrides *out_overrides,
                              qa_written_record *out_record, bool *out_has_record);
/* `record` NULL writes `"written": null`. */
void qa_layer_store_save_game(const qa_layer_store *store, const char *id, const qa_game_overrides *overrides,
                              const qa_layer_snapshot *record);

/* TS GameLayers: one game's view of the layers. The store is borrowed and must outlive it. */
typedef struct qa_game_layers {
  const qa_layer_store *store;
  qa_layer_format format;
  char *game_id;
  bool has_high;
  qa_high_level high;
  qa_game_overrides overrides;
  bool has_record;
  qa_written_record record;
  qa_effective_set current_loaded;
  qa_layer_map default_binds;
  qa_layer_map local_binds;
  qa_layer_map local_cvars;
  bool ready;
} qa_game_layers;
void qa_game_layers_init(qa_game_layers *layers, const qa_layer_store *store, qa_layer_format format,
                         const char *game_id);
void qa_game_layers_destroy(qa_game_layers *layers);
/* TS loaded: the effective set the game started from (borrowed). */
const qa_effective_set *qa_game_layers_loaded(const qa_game_layers *layers);
/* TS highLevel: borrowed; an empty high level before the layers are read. */
const qa_high_level *qa_game_layers_high_level(const qa_game_layers *layers);
/* TS gameOverrides (borrowed). */
const qa_game_overrides *qa_game_layers_overrides(const qa_game_layers *layers);
/* TS noteDefaultBinds: the bindings in force once the game's default.cfg ran. */
void qa_game_layers_note_default_binds(qa_game_layers *layers, const qa_layer_pair *binds, size_t count);
void qa_game_layers_note_local_bind(qa_game_layers *layers, const char *key);
void qa_game_layers_note_local_cvar(qa_game_layers *layers, const char *name);
/* TS resolve: answers the effective set (borrowed; valid until the next resolve or absorb). */
const qa_effective_set *qa_game_layers_resolve(qa_game_layers *layers, const qa_layer_snapshot *file);
/* TS absorb: raises Error "Settings layers were not read before they were written" before resolve. */
const qa_effective_set *qa_game_layers_absorb(qa_game_layers *layers, const qa_layer_pair *binds, size_t bind_count,
                                              const qa_layer_map *cvars, qa_layer_reset_value_fn reset_value,
                                              void *reset_value_ctx);
/* TS recordWritten. */
void qa_game_layers_record_written(qa_game_layers *layers, const qa_layer_snapshot *file);
bool qa_game_layers_is_ready(const qa_game_layers *layers);

#endif
