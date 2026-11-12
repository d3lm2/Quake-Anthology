/*
 * Port of src/persistence/q2-containers.ts. Decoded strings and
 * arrays are malloc'd (qa_*_free helpers release a whole record);
 * encoders return malloc'd bytes with *out_length set (NULL only on
 * OOM). Timestamps (TS bigint) are uint64_t. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_CONTAINERS_H
#define QA_PERSISTENCE_Q2_CONTAINERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_saved_cvar { char *name; char *value; } qa_saved_cvar;
typedef struct qa_q2_classic_server_save {
  char *comment; char *map_command;
  qa_saved_cvar *cvars; size_t cvar_count;
} qa_q2_classic_server_save;
typedef struct qa_q2_rerelease_server_save {
  char *comment; char *map_command;
  qa_saved_cvar *cvars; size_t cvar_count;
  uint64_t timestamp; int kind; /* 0 | 1 | 2 */
} qa_q2_rerelease_server_save;
typedef struct qa_q2_configstring { int64_t index; char *value; } qa_q2_configstring;
/* portal_bytes: classic, the raw `portalopen` array; rerelease, one byte per area portal. */
typedef struct qa_q2_level_metadata {
  qa_q2_configstring *configstrings; size_t configstring_count;
  uint8_t *portal_bytes; size_t portal_length;
} qa_q2_level_metadata;

bool qa_decode_q2_classic_server(const uint8_t *bytes, size_t length,
                                 qa_q2_classic_server_save *out);
uint8_t *qa_encode_q2_classic_server(const qa_q2_classic_server_save *save,
                                     size_t *out_length);
/* MAX_CONFIGSTRINGS * MAX_QPATH of Quake II 3.20. */
#define QA_Q2_CLASSIC_CONFIGSTRINGS 2080
#define QA_Q2_CLASSIC_CONFIGSTRING_WIDTH 64
/* MAX_CONFIGSTRINGS and the 96 byte slot of the API 2023 engine. */
#define QA_Q2_RERELEASE_CONFIGSTRINGS 12448
#define QA_Q2_RERELEASE_CONFIGSTRING_WIDTH 96
/* MAX_MAP_AREAPORTALS of Quake II 3.20; every entry is a 32 bit qboolean. */
#define QA_Q2_CLASSIC_AREA_PORTALS 1024

/* TS `{ portal, open }`. */
typedef struct qa_q2_portal { double portal; bool open; } qa_q2_portal;

/* TS packConfigstrings: the flat configstring array (count * width bytes, malloc'd; NULL only on OOM). A string
 * longer than its slot continues through the following slots. */
uint8_t *qa_pack_configstrings(const qa_q2_configstring *entries, size_t entry_count, int64_t count, int64_t width,
                               size_t *out_length);
/* TS unpackConfigstrings: malloc'd entries (free each value, then the array); false only on OOM. */
bool qa_unpack_configstrings(const uint8_t *memory, size_t memory_length, int64_t count, int64_t width,
                             qa_q2_configstring **out, size_t *out_count);
/* TS classicPortalBytes: the `portalopen` array, 1024 little endian ints. */
uint8_t *qa_q2_classic_portal_bytes(const qa_q2_portal *portals, size_t portal_count, size_t *out_length);
/* TS classicPortalsFrom: the open portals (malloc'd list; false only on OOM). */
bool qa_q2_classic_portals_from(const uint8_t *bytes, size_t length, qa_q2_portal **out, size_t *out_count);
/* TS rereleasePortalBytes: one byte per portal of the map. */
uint8_t *qa_q2_rerelease_portal_bytes(const qa_q2_portal *portals, size_t portal_count, double count,
                                      size_t *out_length);
bool qa_q2_rerelease_portals_from(const uint8_t *bytes, size_t length, qa_q2_portal **out, size_t *out_count);

/* Classic `MAP.sv2`: the configstring array, then the raw `portalopen` array. */
bool qa_decode_q2_classic_level_metadata(const uint8_t *bytes, size_t length, qa_q2_level_metadata *out);
uint8_t *qa_encode_q2_classic_level_metadata(const qa_q2_level_metadata *save, size_t *out_length);
/* Rerelease `server.ssv`: "EVAS", a kind byte, a length-prefixed comment, a Unix timestamp, the map command and
 * the latched cvar pairs. */
bool qa_decode_q2_rerelease_server(const uint8_t *bytes, size_t length,
                                   qa_q2_rerelease_server_save *out);
uint8_t *qa_encode_q2_rerelease_server(const qa_q2_rerelease_server_save *save,
                                       size_t *out_length);
/* Rerelease level metadata: `index u32, length u8, text` per non-empty slot, an index of -1, then the portal
 * count (u16) and one byte per portal. */
bool qa_decode_q2_rerelease_level_metadata(const uint8_t *bytes, size_t length, qa_q2_level_metadata *out);
uint8_t *qa_encode_q2_rerelease_level_metadata(const qa_q2_level_metadata *save, size_t *out_length);

void qa_q2_classic_server_save_free(qa_q2_classic_server_save *save);
void qa_q2_rerelease_server_save_free(qa_q2_rerelease_server_save *save);
void qa_q2_level_metadata_free(qa_q2_level_metadata *save);

#endif
