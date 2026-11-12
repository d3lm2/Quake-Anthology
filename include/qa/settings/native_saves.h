/*
 * Port of src/settings/native-saves.ts: where a stock game keeps saves the way
 * the original does, and which format the engine can write there. Quake writes
 * `sN.sav` beside the game (NetQuake v5; the rerelease v6 in its Saved Games
 * folder). Quake II keeps a folder per save, `save/NAME/` with `server.ssv`,
 * `game.ssv` and each level's `MAP.sav` and `MAP.sv2`; the rerelease packs the
 * same files as `save/NAME.pak.gz`. The engine writes those when the original
 * game module runs the game, with its own format beside them for state they
 * cannot carry. Where the engine's own game logic runs, only the engine format
 * is written, at the same place. Quake III has no savegame feature and keeps
 * the engine format under `save/`.
 * C accommodations: the layout's closures (locate, originalPath, candidates,
 * slot) are functions over the layout; returned paths are malloc'd and NULL is
 * the TS null. GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_NATIVE_SAVES_H
#define QA_SETTINGS_NATIVE_SAVES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/settings/native_config.h"

/* TS NativeSaveOriginal | null: the original format a stock game's native save location can be written in. */
typedef enum qa_native_save_original {
  QA_NATIVE_SAVE_ENGINE_ONLY = 0, /* null: only the engine format fits */
  QA_NATIVE_SAVE_V5,              /* "v5" */
  QA_NATIVE_SAVE_V6,              /* "v6" */
  QA_NATIVE_SAVE_Q2_FOLDER,       /* "q2-folder" */
  QA_NATIVE_SAVE_Q2_PAK           /* "q2-pak" */
} qa_native_save_original;
/* "v5", "v6", "q2-folder", "q2-pak", or NULL. */
const char *qa_native_save_original_name(qa_native_save_original original);

/* TS NativeSaveLayout["arrangement"]: how a save slot is laid out below a scanned folder. */
typedef enum qa_native_save_arrangement {
  QA_NATIVE_SAVE_FILES,
  QA_NATIVE_SAVE_FOLDERS
} qa_native_save_arrangement;

/* TS NativeSaveLayout. */
typedef struct qa_native_save_layout {
  /* Folder all native saves are contained in. */
  char *root;
  /* Folders listed for saves, writes first, each scanned to `depth` levels. */
  char **scan;
  size_t scan_count;
  /* Subfolder levels scanned below each `scan` folder. */
  int depth;
  qa_native_save_arrangement arrangement;
  qa_native_save_original original;
} qa_native_save_layout;

/* The product fields nativeSaveLayout reads. */
typedef struct qa_native_save_product {
  qa_game_family family;
  const char *edition;
  const char *content_directory;
} qa_native_save_product;

/* TS nativeSaveLayout. `out` is owned by the caller (qa_native_save_layout_destroy). */
void qa_native_save_layout_build(const qa_native_config_layout *layout, const qa_native_save_product *product,
                                 qa_native_save_layout *out);
void qa_native_save_layout_destroy(qa_native_save_layout *layout);
/* TS locate: the engine-format file of a slot. A Quake II slot name the original refuses raises SaveFormatError. */
char *qa_native_save_layout_locate(const qa_native_save_layout *layout, const char *name);
/* TS originalPath: the file that identifies a slot in the original format (Quake II `server.ssv`, the rerelease
 * pak), or NULL. */
char *qa_native_save_layout_original_path(const qa_native_save_layout *layout, const char *name);
/* TS candidates: every file that could hold a slot, original format first, across the folders a stock game
 * reads. A malloc'd list of malloc'd paths. */
char **qa_native_save_layout_candidates(const qa_native_save_layout *layout, const char *name, size_t *out_count);
/* TS slot: the slot a save file below `root` belongs to, whichever of its files the path names; NULL when it is
 * not one. */
char *qa_native_save_layout_slot(const qa_native_save_layout *layout, const char *path);
/* TS insideNativeSaves: true when `path` is inside the native save root. */
bool qa_inside_native_saves(const qa_native_save_layout *layout, const char *path);

#endif
