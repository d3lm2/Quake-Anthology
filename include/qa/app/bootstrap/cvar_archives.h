/* Port of src/app/bootstrap/cvar-archives.ts. Async store I/O is
 * synchronous against the landed qa_config_store. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CVAR_ARCHIVES_H
#define QA_APP_BOOTSTRAP_CVAR_ARCHIVES_H
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
#include "qa/settings/config.h"
/* TS CvarArchiveOwner tuple: owner[0] is source|client|input|movement|fallback. */
/* Malloc'd entries (name/value owned); free with qa_app_cvar_archive_entries_free. */
qa_cvar_archive_entry *qa_app_load_cvar_archive(const qa_config_store *store, const char *const *owner,
                                                size_t owner_count, qa_command_dialect dialect, size_t *out_count);
void qa_app_cvar_archive_entries_free(qa_cvar_archive_entry *entries, size_t count);
/* entries NULL uses registry archive entries (TS default argument). */
void qa_app_save_cvar_archive(const qa_config_store *store, const char *const *owner, size_t owner_count,
                              qa_cvar_registry *registry, const qa_cvar_archive_entry *entries, size_t entry_count);
/* TS `await store.dump(name, contents)`: a failed write raises Error. The directory step (writeAtomic's recursive
 * mkdir) raises Node's message ("ENOTDIR: not a directory, mkdir '<dir>'"); a later failure raises
 * "<strerror>: could not write <name>". */
void qa_app_config_store_dump(const qa_config_store *store, const char *name, const char *contents);
/* JSON.stringify(value) of a cvar name or value as UTF-8, malloc'd with its quotes. The cvar holds console text (one
 * byte per code unit, written as the code points' UTF-8) or, for an archive value the console cannot hold, the
 * archive's UTF-8 (U+0000 as C0 80 written \u0000; a lone surrogate written \udXXX). */
char *qa_app_cvar_json_string(const char *value);
/* Node's mkdir(path, { recursive: true }) as Bun runs it: 0, or the errno with *failed the malloc'd directory the
 * error names (raise it with qa_node_fs_error(code, "mkdir", *failed)). */
int qa_app_mkdir_recursive(const char *path, char **failed);
/* TS archiveDataText(text, isCommand): saved settings are data; only variable and binding
 * lines survive, one command per line. Malloc'd ("" when nothing is kept). */
char *qa_app_archive_data_text(const char *text, bool (*is_command)(void *ctx, const char *name), void *ctx);
#endif
