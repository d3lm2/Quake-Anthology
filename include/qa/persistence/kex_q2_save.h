/*
 * Port of src/persistence/kex-q2-save.ts. C accommodations: the
 * async file readers are synchronous (comment at the definitions);
 * gunzip uses zlib; the parsed game.ssv root (TS unknown) is a
 * qa_save_value tree from qa_parse_json_value (NULL when absent).
 * Strings and arrays are malloc'd; qa_*_free releases a record.
 * Timestamps (TS bigint) are uint64_t. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_KEX_Q2_SAVE_H
#define QA_PERSISTENCE_KEX_Q2_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/q2_containers.h"
#include "qa/persistence/value.h"

typedef struct qa_kex_cvar { char *name; char *value; } qa_kex_cvar;
typedef struct qa_kex_q2_save_info {
  char *comment;
  char *map_command;
  uint64_t timestamp;
  int kind; /* 0 manual, 1 autosave, 2 the third kind the engine defines */
  char *game_directory; /* NULL when absent */
  qa_kex_cvar *cvars; /* insertion order; later duplicates win on
                           lookup, as in the TS Map */
  size_t cvar_count;
} qa_kex_q2_save_info;
/* One entry of the TS `entries` Map: lowercase name and an owned copy of its bytes. */
typedef struct qa_kex_q2_save_entry {
  char *name;
  uint8_t *bytes;
  size_t length;
} qa_kex_q2_save_entry;
typedef struct qa_kex_q2_save_archive {
  qa_kex_q2_save_info info;
  qa_q2_rerelease_server_save server;
  char **files; /* lowercase names in archive order */
  size_t file_count;
  qa_kex_q2_save_entry *entries; /* every entry by lowercase name, in archive order */
  size_t entry_count;
  qa_save_value *game; /* owned; NULL when game.ssv is absent */
} qa_kex_q2_save_archive;

bool qa_decode_kex_q2_server_save(const uint8_t *bytes, size_t length,
                                  qa_kex_q2_save_info *out);
/* TS async readKexQ2SaveArchive/readKexQ2SaveInfo: synchronous here. */
bool qa_read_kex_q2_save_archive(const char *path, qa_kex_q2_save_archive *out);
/* TS parseKexQ2SaveArchive: the archive from its gzip bytes. False only on OOM; a malformed archive raises
 * SaveFormatError. */
bool qa_parse_kex_q2_save_archive(const uint8_t *compressed, size_t length, qa_kex_q2_save_archive *out);
/* entries.get(name): the entry with that lowercase name, or NULL. */
const qa_kex_q2_save_entry *qa_kex_q2_save_archive_entry(const qa_kex_q2_save_archive *archive, const char *name);
bool qa_read_kex_q2_save_info(const char *path, qa_kex_q2_save_info *out);

/* TS packKexQ2SaveArchive: `PACK`, the directory offset and length, the directory first and the file data after
 * it in directory order, gzip compressed. Malloc'd; NULL only on OOM. */
typedef struct qa_kex_q2_pack_entry {
  const char *name;
  const uint8_t *bytes;
  size_t length;
} qa_kex_q2_pack_entry;
uint8_t *qa_pack_kex_q2_save_archive(const qa_kex_q2_pack_entry *entries, size_t count, size_t *out_length);
/* The pak before compression (C only: zlib builds differ in the compressed bytes they emit, so byte-exact
 * checks compare this form). */
uint8_t *qa_pack_kex_q2_save_pak(const qa_kex_q2_pack_entry *entries, size_t count, size_t *out_length);

void qa_kex_q2_save_info_free(qa_kex_q2_save_info *info);
void qa_kex_q2_save_archive_free(qa_kex_q2_save_archive *archive);

#endif
