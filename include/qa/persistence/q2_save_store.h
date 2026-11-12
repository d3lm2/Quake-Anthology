/*
 * Port of src/persistence/q2-save-store.ts: the `save/` folder of a Quake II
 * game directory and the working folder workflow of id's sv_ccmds.c:
 * SV_WipeSavegame and SV_CopySaveGame move a save between `current/` and
 * `save/NAME/`. The async methods are synchronous; file system failures raise
 * Error with the system message, as the TS promise rejects. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_SAVE_STORE_H
#define QA_PERSISTENCE_Q2_SAVE_STORE_H

#include "qa/persistence/q2_save_folder.h"

/* The working folder the game module writes level files into. */
#define QA_Q2_CURRENT_SAVE "current"

/* TS Q2SaveStore: `root` is the game directory's `save` folder (an owned copy). */
typedef struct qa_q2_save_store {
  char *root;
} qa_q2_save_store;
void qa_q2_save_store_init(qa_q2_save_store *store, const char *root);
void qa_q2_save_store_destroy(qa_q2_save_store *store);
/* TS folder: join(root, name). Malloc'd. */
char *qa_q2_save_store_folder(const qa_q2_save_store *store, const char *name);
/* TS read: every regular file of a slot, by name. A missing slot has no files. `out` is initialized by the
 * call. A file over 256 MiB is a RangeError (`Save file NAME is implausibly large`). */
void qa_q2_save_store_read(const qa_q2_save_store *store, const char *name, qa_q2_save_files *out);
/* TS wipe (SV_WipeSavegame): removes server.ssv, game.ssv, every *.sav and every *.sv2. */
void qa_q2_save_store_wipe(const qa_q2_save_store *store, const char *name);
void qa_q2_save_store_write(const qa_q2_save_store *store, const char *name, const qa_q2_save_files *files);
/* TS copy (SV_CopySaveGame): the destination is wiped, then receives the source's save files. */
void qa_q2_save_store_copy(const qa_q2_save_store *store, const char *from, const char *to);
/* TS save (`save NAME`): the level and server files go to `current/`, which is then copied to the slot. */
void qa_q2_save_store_save(const qa_q2_save_store *store, const char *name, const qa_q2_save_files *files);
/* TS load (`load NAME`): the slot is copied to `current/` and the files are read from there. */
void qa_q2_save_store_load(const qa_q2_save_store *store, const char *name, qa_q2_save_files *out);

#endif
