/*
 * Port of src/settings/setting-spellings.ts: lookups over the engine setting
 * declarations (core/cvars/setting_declarations.h).
 * C accommodations:
 *  - A TS Map<string, ...> whose insertion order is observable becomes
 *    qa_layer_map: entries in insertion order, set() replacing in place,
 *    delete() closing the gap. Each entry has the map key (`id`) and up to two
 *    strings: `name` (a LayerBind's key, a LayerCvar's name, a file spelling)
 *    and `value` (a command or value; NULL is the TS null command). A
 *    Map<string, string> keeps its value in `value` and leaves `name` NULL.
 *    A TS Set<string> is a map whose entries carry only `id`.
 *  - Null results are false returns; returned strings are malloc'd.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SETTING_SPELLINGS_H
#define QA_SETTINGS_SETTING_SPELLINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/core/cvars/setting_declarations.h"

typedef struct qa_layer_entry {
  char *id;    /* the Map key */
  char *name;  /* NULL when the TS value is a plain string */
  char *value; /* NULL is the TS null */
} qa_layer_entry;
typedef struct qa_layer_map {
  qa_layer_entry *entries;
  size_t count;
  size_t capacity;
  /* Private: a hash index over entries[0, indexed) (open addressing, entry index + 1), kept so a
   * lookup is not a scan (TS Map); NULL until a lookup in a larger map builds it. */
  size_t *index;
  size_t index_capacity, indexed;
} qa_layer_map;

void qa_layer_map_init(qa_layer_map *map);
void qa_layer_map_destroy(qa_layer_map *map);
void qa_layer_map_clear(qa_layer_map *map);
/* Map.get: the entry, or NULL for undefined. The pointer is valid until the next set or delete. */
const qa_layer_entry *qa_layer_map_get(const qa_layer_map *map, const char *id);
bool qa_layer_map_has(const qa_layer_map *map, const char *id);
/* Map.set: strings are copied; an existing key keeps its position. */
void qa_layer_map_set(qa_layer_map *map, const char *id, const char *name, const char *value);
/* Map.delete: true when the key was present. */
bool qa_layer_map_delete(qa_layer_map *map, const char *id);
/* Replaces `out` (initialized) with a copy of `in`. */
void qa_layer_map_copy(qa_layer_map *out, const qa_layer_map *in);

/* TS settingFor: the setting a cvar name spells, in any game. `spelling` is
 * NULL when the name is the canonical one. False is the TS null. */
typedef struct qa_setting_match {
  const qa_engine_setting *setting;
  const qa_setting_spelling *spelling;
} qa_setting_match;
bool qa_setting_for(const char *name, qa_setting_match *out);

/* TS toCanonical: a value read in a cvar's own spelling, as the canonical
 * value. *out_canonical is static text, *out_value malloc'd. False is the TS
 * null (not a value of the setting). */
bool qa_setting_to_canonical(const char *name, const char *value, const char **out_canonical, char **out_value);

/* TS spellingIn: the spelling a game's native file uses for a canonical
 * setting. `name` is static text; `spelling` is NULL when the file uses the
 * canonical name itself. False is the TS null (the game does not have it). */
typedef struct qa_file_spelling {
  const char *name;
  const qa_setting_spelling *spelling;
} qa_file_spelling;
bool qa_spelling_in(qa_layer_format format, const char *canonical, qa_file_spelling *out);
/* TS spellingIn(...).toFile(value): malloc'd. */
char *qa_file_spelling_to_file(const qa_file_spelling *spelling, const char *value);

/* TS fileCvars: what a game's file holds for the canonical settings given
 * (`canonical`: id = canonical name, value = canonical value). `out` (an
 * initialized, empty map) receives id = lowercase file name, name = file
 * spelling, value = file value, plus the companions the format needs. */
void qa_file_cvars(qa_layer_format format, const qa_layer_map *canonical, qa_layer_map *out);

#endif
