/* Port of src/settings/setting-spellings.ts. GPL-2.0-or-later. */
#include "qa/settings/setting_spellings.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/js_parse_extra.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  memcpy(copy, text, length + 1);
  return copy;
}

/* --- qa_layer_map --- */
void qa_layer_map_init(qa_layer_map *map) { memset(map, 0, sizeof *map); }
static void entry_free(qa_layer_entry *entry) {
  free(entry->id);
  free(entry->name);
  free(entry->value);
}
static void index_drop(qa_layer_map *map) {
  free(map->index);
  map->index = NULL;
  map->index_capacity = 0;
  map->indexed = 0;
}
void qa_layer_map_clear(qa_layer_map *map) {
  for (size_t i = 0; i < map->count; i++) entry_free(&map->entries[i]);
  map->count = 0;
  index_drop(map);
}
void qa_layer_map_destroy(qa_layer_map *map) {
  qa_layer_map_clear(map);
  free(map->entries);
  memset(map, 0, sizeof *map);
}
static size_t hash_id(const char *id) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (const unsigned char *at = (const unsigned char *)id; *at != '\0'; at++) hash = (hash ^ *at) * 0x100000001b3ULL;
  return (size_t)hash;
}
static void index_insert(qa_layer_map *map, size_t entry) {
  size_t mask = map->index_capacity - 1, slot = hash_id(map->entries[entry].id) & mask;
  while (map->index[slot] != 0) slot = (slot + 1) & mask;
  map->index[slot] = entry + 1;
}
static void index_rebuild(qa_layer_map *map) {
  size_t capacity = 16;
  while (capacity < map->count * 2) capacity *= 2;
  size_t *index = (size_t *)calloc(capacity, sizeof *index);
  if (index == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  free(map->index);
  map->index = index;
  map->index_capacity = capacity;
  for (size_t i = 0; i < map->count; i++) index_insert(map, i);
  map->indexed = map->count;
}
/* Small maps are scanned; a larger one is looked up through its index (built on demand: the index
 * is a cache, so a const map may build it). */
static qa_layer_entry *find_entry(const qa_layer_map *map, const char *id) {
  if (map->count <= 8) {
    for (size_t i = 0; i < map->count; i++)
      if (strcmp(map->entries[i].id, id) == 0) return &map->entries[i];
    return NULL;
  }
  qa_layer_map *cache = (qa_layer_map *)map;
  if (cache->index == NULL || cache->indexed != cache->count || cache->index_capacity < cache->count * 2) index_rebuild(cache);
  size_t mask = cache->index_capacity - 1;
  for (size_t slot = hash_id(id) & mask; cache->index[slot] != 0; slot = (slot + 1) & mask) {
    qa_layer_entry *entry = &cache->entries[cache->index[slot] - 1];
    if (strcmp(entry->id, id) == 0) return entry;
  }
  return NULL;
}
const qa_layer_entry *qa_layer_map_get(const qa_layer_map *map, const char *id) { return find_entry(map, id); }
bool qa_layer_map_has(const qa_layer_map *map, const char *id) { return find_entry(map, id) != NULL; }
void qa_layer_map_set(qa_layer_map *map, const char *id, const char *name, const char *value) {
  /* The arguments may point into the entry being replaced, so copy before freeing. */
  char *name_copy = dup_text(name), *value_copy = dup_text(value);
  qa_layer_entry *entry = find_entry(map, id);
  if (entry != NULL) {
    free(entry->name);
    free(entry->value);
    entry->name = name_copy;
    entry->value = value_copy;
    return;
  }
  if (map->count == map->capacity) {
    size_t capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    qa_layer_entry *grown = (qa_layer_entry *)realloc(map->entries, capacity * sizeof *grown);
    if (grown == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
    map->entries = grown;
    map->capacity = capacity;
  }
  entry = &map->entries[map->count++];
  entry->id = dup_text(id);
  entry->name = name_copy;
  entry->value = value_copy;
  /* An index covering every earlier entry with room to spare takes the new one. */
  if (map->index != NULL && map->indexed == map->count - 1 && map->index_capacity >= map->count * 2) {
    index_insert(map, map->count - 1);
    map->indexed = map->count;
  }
}
bool qa_layer_map_delete(qa_layer_map *map, const char *id) {
  qa_layer_entry *entry = find_entry(map, id);
  if (entry == NULL) return false;
  size_t index = (size_t)(entry - map->entries);
  entry_free(entry);
  memmove(entry, entry + 1, (map->count - index - 1) * sizeof *entry);
  map->count--;
  index_drop(map); /* positions moved */
  return true;
}
void qa_layer_map_copy(qa_layer_map *out, const qa_layer_map *in) {
  qa_layer_map copy;
  qa_layer_map_init(&copy);
  for (size_t i = 0; i < in->count; i++) qa_layer_map_set(&copy, in->entries[i].id, in->entries[i].name, in->entries[i].value);
  qa_layer_map_destroy(out);
  *out = copy;
}

/* --- TS byName: the canonical name as declared, each spelling lowercased --- */
static bool by_name(const char *key, qa_setting_match *out) {
  /* Later Map.set calls replace earlier ones, so the last declaration of a key wins. */
  bool found = false;
  for (size_t i = 0; i < qa_engine_setting_count; i++) {
    const qa_engine_setting *setting = &qa_engine_settings[i];
    if (strcmp(setting->canonical, key) == 0) {
      out->setting = setting;
      out->spelling = NULL;
      found = true;
    }
    for (size_t j = 0; j < setting->spelling_count; j++) {
      char *lower = qa_js_to_lower_case(setting->spellings[j].name);
      if (lower == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
      bool same = strcmp(lower, key) == 0;
      free(lower);
      if (!same) continue;
      out->setting = setting;
      out->spelling = &setting->spellings[j];
      found = true;
    }
  }
  return found;
}

bool qa_setting_for(const char *name, qa_setting_match *out) {
  char *lower = qa_js_to_lower_case(name);
  if (lower == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  qa_setting_match match = {NULL, NULL};
  bool found = by_name(lower, &match);
  free(lower);
  if (found && out != NULL) *out = match;
  return found;
}

bool qa_setting_to_canonical(const char *name, const char *value, const char **out_canonical, char **out_value) {
  qa_setting_match found;
  if (!qa_setting_for(name, &found)) return false;
  const qa_cvar_alias_conversion *conversion = found.spelling != NULL ? &found.spelling->conversion : &qa_identity_conversion;
  if (conversion->is_identity) {
    if (!qa_finite_number(value, NULL)) return false;
    *out_canonical = found.setting->canonical;
    *out_value = dup_text(value);
    return true;
  }
  char *written = NULL, *message = NULL;
  if (!conversion->write(conversion->ctx, value, &written, &message)) {
    free(message);
    return false;
  }
  *out_canonical = found.setting->canonical;
  *out_value = written;
  return true;
}

bool qa_spelling_in(qa_layer_format format, const char *canonical, qa_file_spelling *out) {
  qa_setting_match found;
  if (!by_name(canonical, &found) || found.spelling != NULL) return false;
  if ((found.setting->formats & QA_LAYER_FORMAT_BIT(format)) != 0) {
    if (out != NULL) {
      out->name = found.setting->canonical;
      out->spelling = NULL;
    }
    return true;
  }
  for (size_t i = 0; i < found.setting->spelling_count; i++) {
    const qa_setting_spelling *entry = &found.setting->spellings[i];
    if ((entry->formats & QA_LAYER_FORMAT_BIT(format)) == 0) continue;
    if (out != NULL) {
      out->name = entry->name;
      out->spelling = entry;
    }
    return true;
  }
  return false;
}

char *qa_file_spelling_to_file(const qa_file_spelling *spelling, const char *value) {
  if (spelling->spelling == NULL || spelling->spelling->conversion.is_identity) return dup_text(value);
  const qa_cvar_alias_conversion *conversion = &spelling->spelling->conversion;
  char *read = conversion->read(conversion->ctx, value);
  if (read == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  double converted = qa_js_string_to_number(read);
  if (!isfinite(converted)) return read;
  free(read);
  /* String(Number(converted.toPrecision(7))) */
  char precise[QA_JS_TO_PRECISION_CAPACITY];
  (void)qa_js_to_precision(converted, 7, precise, sizeof precise);
  char text[40];
  qa_net_js_number(qa_js_string_to_number(precise), text, sizeof text);
  return dup_text(text);
}

void qa_file_cvars(qa_layer_format format, const qa_layer_map *canonical, qa_layer_map *out) {
  for (size_t i = 0; i < qa_engine_setting_count; i++) {
    const qa_engine_setting *setting = &qa_engine_settings[i];
    const qa_layer_entry *value = qa_layer_map_get(canonical, setting->canonical);
    qa_file_spelling used;
    if (value == NULL || !qa_spelling_in(format, setting->canonical, &used)) continue;
    char *lower = qa_js_to_lower_case(used.name);
    char *file_value = qa_file_spelling_to_file(&used, value->value);
    if (lower == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
    qa_layer_map_set(out, lower, used.name, file_value);
    free(lower);
    free(file_value);
    for (size_t j = 0; j < setting->companion_count; j++) {
      const qa_setting_companion *companion = &setting->companions[j];
      if (companion->format != format) continue;
      char *companion_lower = qa_js_to_lower_case(companion->name);
      if (companion_lower == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
      qa_layer_map_set(out, companion_lower, companion->name, companion->value);
      free(companion_lower);
    }
  }
}
