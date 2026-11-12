/*
 * Port of src/persistence/q2-save-folder.ts: a Quake II save as the original
 * game keeps it: `save/NAME/` holding `server.ssv`, `game.ssv` and, for every
 * level of the unit, `MAP.sav` (the game module's WriteLevel output) and
 * `MAP.sv2` (configstrings and portal state). Follows id Quake II 3.20
 * sv_ccmds.c: SV_WriteServerFile, SV_WriteLevelFile, SV_ReadServerFile,
 * SV_ReadLevelFile.
 * C accommodations:
 *  - Q2SaveFiles (a ReadonlyMap<string, Uint8Array>) is qa_q2_save_files: an
 *    insertion-ordered list of owned names and bytes.
 *  - The `now: Date` of a save comment is the local wall-clock fields the
 *    original prints (qa_q2_save_clock); qa_q2_save_clock_now reads them.
 *  - SaveFormatError raises through qa_save_format_error; returned strings and
 *    lists are malloc'd.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_SAVE_FOLDER_H
#define QA_PERSISTENCE_Q2_SAVE_FOLDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/common.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/q2_containers.h"
#include "qa/persistence/saved_game.h"

/* TS Q2SaveFiles: a save folder's files by exact name. */
typedef struct qa_q2_save_file {
  char *name;
  uint8_t *bytes;
  size_t length;
} qa_q2_save_file;
typedef struct qa_q2_save_files {
  qa_q2_save_file *files;
  size_t count;
  size_t capacity;
} qa_q2_save_files;
void qa_q2_save_files_init(qa_q2_save_files *files);
void qa_q2_save_files_free(qa_q2_save_files *files);
/* Map.set: copies the name and bytes; an existing exact name keeps its place. */
void qa_q2_save_files_set(qa_q2_save_files *files, const char *name, const uint8_t *bytes, size_t length);
/* Map.get by exact name, or NULL. */
const qa_q2_save_file *qa_q2_save_files_get(const qa_q2_save_files *files, const char *name);

/* TS q2LevelName: the folder and file names the original builds from a map path. Malloc'd. */
char *qa_q2_level_name(const char *map);
/* TS q2SaveSlotName: a save slot name the original accepts: `Bad savedir.` for "..", "/" and "\", and `current`
 * is the working folder. Malloc'd. */
char *qa_q2_save_slot_name(const char *name);
/* TS parseQ2MapCommand: `mapname`, `*mapname` or `mapname$spawnpoint` as `svs.mapcmd` holds them. Both strings
 * are malloc'd. */
void qa_parse_q2_map_command(const char *command, char **out_map, char **out_spawn_point);
/* TS q2MapCommand. Malloc'd. */
char *qa_q2_map_command(const char *map, const char *spawn_point);

/* TS q2LatchedCvars: the cvars SV_WriteServerFile records: every CVAR_LATCH variable, newest registration
 * first, which is the order of the original's cvar list. `registry` is a registry's save-state checkpoint.
 * Malloc'd list of owned pairs (qa_q2_saved_cvars_free). */
qa_saved_cvar *qa_q2_latched_cvars(const uint8_t *registry, size_t registry_length, qa_command_dialect dialect,
                                   size_t *out_count);
void qa_q2_saved_cvars_free(qa_saved_cvar *cvars, size_t count);

/* The local time fields of a TS Date that q2SaveComment prints. */
typedef struct qa_q2_save_clock {
  int hours;   /* getHours() */
  int minutes; /* getMinutes() */
  int month;   /* getMonth() + 1 */
  int date;    /* getDate() */
} qa_q2_save_clock;
qa_q2_save_clock qa_q2_save_clock_now(void);
/* TS q2SaveComment: `"%2i:%i%i %2i/%2i  "` and the level name; an autosave reads `ENTERING name`. Malloc'd. */
char *qa_q2_save_comment(const char *level_name, bool autosave, qa_q2_save_clock now);

/* TS Q2ClassicFolderContext. */
typedef struct qa_q2_classic_folder_context {
  /* The map command to resume from: the current map and its spawn point. */
  const char *map_command;
  qa_q2_save_clock now;
} qa_q2_classic_folder_context;
/* TS q2ClassicFolder: SV_WriteServerFile plus the per-level files of `current/`. An autosave taken on entering
 * a level carries no file for that level, because the original has not written one yet. `out` is initialized by
 * the call. */
void qa_q2_classic_folder(const qa_q2_classic_original_save *save, const qa_q2_classic_folder_context *context,
                          qa_q2_save_files *out);
/* TS readQ2ClassicFolder: everything a loaded folder holds, bound to the map its `server.ssv` resumes. The
 * level of that map is absent for an autosave taken on entering it; every other `.sav` is a visited level whose
 * `.sv2` must accompany it. `out` is owned by the caller (qa_q2_classic_original_import_free). */
void qa_read_q2_classic_folder(const qa_q2_save_files *files, qa_q2_classic_original_import *out);

/* TS q2ClassicFolderHeader: the folder's header for a list: the slot comment and the map it resumes. */
typedef struct qa_q2_classic_folder_header {
  char *comment;
  char *map;
  char *spawn_point;
  bool autosave;
} qa_q2_classic_folder_header;
void qa_q2_classic_folder_header_read(const uint8_t *server_bytes, size_t length, qa_q2_classic_folder_header *out);
void qa_q2_classic_folder_header_free(qa_q2_classic_folder_header *header);

/* TS q2FilesDigest: identity of a set of original files, so an engine-format sidecar can say which files it was
 * written beside. File names are compared without case, as the original's file system does. */
void qa_q2_files_digest(const qa_q2_save_files *files, uint8_t out[QA_SAVE_DIGEST_BYTES]);

#endif
