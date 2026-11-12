/* Port of src/settings/layers.ts. GPL-2.0-or-later. */
#include "qa/settings/layers.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

#define LAYER_VERSION 2

static noreturn void out_of_memory(void) { qa_raise("Error", 0, 0, "%s", "out of memory"); }
static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) out_of_memory();
  memcpy(copy, text, length + 1);
  return copy;
}
/* TS fold: key.toLowerCase(). */
static char *fold(const char *key) {
  char *lower = qa_js_to_lower_case(key);
  if (lower == NULL) out_of_memory();
  return lower;
}
/* `a === b` for `string | null` operands. */
static bool same_nullable(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

void qa_layer_snapshot_init(qa_layer_snapshot *snapshot) {
  qa_layer_map_init(&snapshot->binds);
  qa_layer_map_init(&snapshot->cvars);
}
void qa_layer_snapshot_destroy(qa_layer_snapshot *snapshot) {
  qa_layer_map_destroy(&snapshot->binds);
  qa_layer_map_destroy(&snapshot->cvars);
}
void qa_high_level_init(qa_high_level *high) {
  qa_layer_map_init(&high->settings);
  qa_layer_map_init(&high->binds);
}
void qa_high_level_destroy(qa_high_level *high) {
  qa_layer_map_destroy(&high->settings);
  qa_layer_map_destroy(&high->binds);
}
void qa_game_overrides_init(qa_game_overrides *overrides) {
  qa_layer_map_init(&overrides->binds);
  qa_layer_map_init(&overrides->cvars);
}
void qa_game_overrides_destroy(qa_game_overrides *overrides) {
  qa_layer_map_destroy(&overrides->binds);
  qa_layer_map_destroy(&overrides->cvars);
}
void qa_effective_set_init(qa_effective_set *set) {
  qa_layer_map_init(&set->binds);
  qa_layer_map_init(&set->unbound);
  qa_layer_map_init(&set->cvars);
}
void qa_effective_set_destroy(qa_effective_set *set) {
  qa_layer_map_destroy(&set->binds);
  qa_layer_map_destroy(&set->unbound);
  qa_layer_map_destroy(&set->cvars);
}
void qa_written_record_init(qa_written_record *record) {
  qa_layer_map_init(&record->binds);
  qa_layer_map_init(&record->cvars);
}
void qa_written_record_destroy(qa_written_record *record) {
  qa_layer_map_destroy(&record->binds);
  qa_layer_map_destroy(&record->cvars);
}

void qa_effective_set_build(qa_layer_format format, const qa_high_level *high, const qa_game_overrides *overrides,
                            qa_effective_set *out) {
  qa_effective_set_init(out);
  qa_layer_map binds;
  qa_layer_map_init(&binds);
  for (size_t i = 0; i < high->binds.count; i++)
    qa_layer_map_set(&binds, high->binds.entries[i].id, high->binds.entries[i].name, high->binds.entries[i].value);
  for (size_t i = 0; i < overrides->binds.count; i++)
    qa_layer_map_set(&binds, overrides->binds.entries[i].id, overrides->binds.entries[i].name,
                     overrides->binds.entries[i].value);
  for (size_t i = 0; i < high->settings.count; i++) {
    const qa_layer_entry *entry = &high->settings.entries[i];
    if (qa_spelling_in(format, entry->id, NULL)) qa_layer_map_set(&out->cvars, entry->id, entry->id, entry->value);
  }
  for (size_t i = 0; i < overrides->cvars.count; i++) {
    const qa_layer_entry *entry = &overrides->cvars.entries[i];
    if (qa_spelling_in(format, entry->id, NULL)) qa_layer_map_set(&out->cvars, entry->id, entry->name, entry->value);
  }
  for (size_t i = 0; i < binds.count; i++) {
    const qa_layer_entry *entry = &binds.entries[i];
    qa_layer_map_set(entry->value != NULL ? &out->binds : &out->unbound, entry->id, entry->name, entry->value);
  }
  qa_layer_map_destroy(&binds);
}

bool qa_layer_same_value(const char *a, const char *b) {
  if (strcmp(a, b) == 0) return true;
  double x = 0, y = 0;
  if (!qa_finite_number(a, &x) || !qa_finite_number(b, &y)) return false;
  return fabs(x - y) <= 1e-6 * qa_js_max(qa_js_max(1, fabs(x)), fabs(y));
}

bool qa_detect_hand_edits(qa_layer_format format, const qa_layer_snapshot *file, const qa_written_record *record,
                          const qa_high_level *high, qa_game_overrides *overrides) {
  bool changed = false;
  qa_game_overrides none;
  qa_game_overrides_init(&none);
  qa_effective_set base;
  qa_effective_set_build(format, high, &none, &base);
  qa_game_overrides_destroy(&none);
  /* baseBinds: fold(bind.key) -> bind, over the bound keys. */
  qa_layer_map base_binds;
  qa_layer_map_init(&base_binds);
  for (size_t i = 0; i < base.binds.count; i++) {
    char *folded = fold(base.binds.entries[i].name);
    qa_layer_map_set(&base_binds, folded, base.binds.entries[i].name, base.binds.entries[i].value);
    free(folded);
  }
  /* new Set([...file.binds.keys(), ...record.binds.keys()]) */
  qa_layer_map keys;
  qa_layer_map_init(&keys);
  for (size_t i = 0; i < file->binds.count; i++) qa_layer_map_set(&keys, file->binds.entries[i].id, NULL, NULL);
  for (size_t i = 0; i < record->binds.count; i++) qa_layer_map_set(&keys, record->binds.entries[i].id, NULL, NULL);
  for (size_t i = 0; i < keys.count; i++) {
    const char *key = keys.entries[i].id;
    const qa_layer_entry *in_file = qa_layer_map_get(&file->binds, key);
    const char *now = in_file != NULL ? in_file->value : NULL;
    const qa_layer_entry *recorded = qa_layer_map_get(&record->binds, key);
    if (same_nullable(now, recorded != NULL ? recorded->value : NULL)) continue;
    const qa_layer_entry *beneath = qa_layer_map_get(&base_binds, key);
    const char *below = beneath != NULL ? beneath->value : NULL;
    if (same_nullable(now, below)) {
      changed = qa_layer_map_delete(&overrides->binds, key) || changed;
    } else {
      qa_layer_map_set(&overrides->binds, key, in_file != NULL ? in_file->name : key, now);
      changed = true;
    }
  }
  qa_layer_map_destroy(&keys);
  qa_layer_map_destroy(&base_binds);
  for (size_t i = 0; i < file->cvars.count; i++) {
    const qa_layer_entry *entry = &file->cvars.entries[i];
    const char *name = entry->id;
    if (!qa_spelling_in(format, name, NULL)) continue;
    const qa_layer_entry *written = qa_layer_map_get(&record->cvars, name);
    if (written != NULL && qa_layer_same_value(written->value, entry->value)) continue;
    const qa_layer_entry *below = qa_layer_map_get(&base.cvars, name);
    if (below != NULL && qa_layer_same_value(below->value, entry->value)) {
      changed = qa_layer_map_delete(&overrides->cvars, name) || changed;
    } else {
      qa_layer_map_set(&overrides->cvars, name, entry->name, entry->value);
      changed = true;
    }
  }
  qa_effective_set_destroy(&base);
  return changed;
}

void qa_seed_high(qa_layer_format format, const qa_layer_snapshot *file, const qa_layer_map *default_binds,
                  qa_high_level *out) {
  qa_high_level_init(out);
  for (size_t i = 0; i < file->binds.count; i++) {
    const qa_layer_entry *bind = &file->binds.entries[i];
    if (bind->value == NULL) continue;
    /* defaultBinds.get(key)?.command !== bind.command: an absent default (undefined) never equals a command. */
    const qa_layer_entry *given = default_binds != NULL ? qa_layer_map_get(default_binds, bind->id) : NULL;
    if (given != NULL && given->value != NULL && strcmp(given->value, bind->value) == 0) continue;
    qa_layer_map_set(&out->binds, bind->id, bind->name, bind->value);
  }
  for (size_t i = 0; i < file->cvars.count; i++) {
    const qa_layer_entry *entry = &file->cvars.entries[i];
    if (qa_spelling_in(format, entry->id, NULL)) qa_layer_map_set(&out->settings, entry->id, NULL, entry->value);
  }
}

qa_absorb_touched qa_absorb_changes(qa_layer_format format, const qa_effective_set *loaded, const qa_layer_pair *binds,
                                    size_t bind_count, const qa_layer_map *cvars, const qa_absorb_context *context,
                                    qa_high_level *high, qa_game_overrides *overrides) {
  qa_absorb_touched touched = {false, false};
  qa_layer_map spelled, start_binds, now_binds, keys;
  qa_layer_map_init(&spelled);
  qa_layer_map_init(&start_binds);
  qa_layer_map_init(&now_binds);
  qa_layer_map_init(&keys);
  const qa_layer_map *defaults = context->default_binds;
  for (size_t i = 0; defaults != NULL && i < defaults->count; i++)
    qa_layer_map_set(&spelled, defaults->entries[i].id, NULL, defaults->entries[i].name);
  for (size_t i = 0; defaults != NULL && i < defaults->count; i++)
    if (defaults->entries[i].value != NULL)
      qa_layer_map_set(&start_binds, defaults->entries[i].id, NULL, defaults->entries[i].value);
  for (size_t i = 0; i < loaded->unbound.count; i++) {
    const qa_layer_entry *bind = &loaded->unbound.entries[i];
    char *folded = fold(bind->name);
    (void)qa_layer_map_delete(&start_binds, folded);
    qa_layer_map_set(&spelled, folded, NULL, bind->name);
    free(folded);
  }
  for (size_t i = 0; i < loaded->binds.count; i++) {
    const qa_layer_entry *bind = &loaded->binds.entries[i];
    char *folded = fold(bind->name);
    if (bind->value != NULL) qa_layer_map_set(&start_binds, folded, NULL, bind->value);
    qa_layer_map_set(&spelled, folded, NULL, bind->name);
    free(folded);
  }
  for (size_t i = 0; i < bind_count; i++) {
    char *folded = fold(binds[i].key);
    qa_layer_map_set(&now_binds, folded, binds[i].key, binds[i].command);
    free(folded);
  }
  for (size_t i = 0; i < start_binds.count; i++) qa_layer_map_set(&keys, start_binds.entries[i].id, NULL, NULL);
  for (size_t i = 0; i < now_binds.count; i++) qa_layer_map_set(&keys, now_binds.entries[i].id, NULL, NULL);
  for (size_t i = 0; i < keys.count; i++) {
    const char *key = keys.entries[i].id;
    const qa_layer_entry *before = qa_layer_map_get(&start_binds, key);
    const qa_layer_entry *after = qa_layer_map_get(&now_binds, key);
    /* before === (after?.command ?? undefined) */
    if (same_nullable(before != NULL ? before->value : NULL, after != NULL ? after->value : NULL)) continue;
    const qa_layer_entry *spelling = qa_layer_map_get(&spelled, key);
    const char *bind_key = after != NULL ? after->name : spelling != NULL ? spelling->value : key;
    const char *bind_command = after != NULL ? after->value : NULL;
    if (context->local_binds != NULL && qa_layer_map_has(context->local_binds, key)) {
      qa_layer_map_set(&overrides->binds, key, bind_key, bind_command);
      touched.overrides = true;
    } else {
      qa_layer_map_set(&high->binds, key, bind_key, bind_command);
      touched.high = true;
      touched.overrides = qa_layer_map_delete(&overrides->binds, key) || touched.overrides;
    }
  }
  qa_layer_map_destroy(&keys);
  qa_layer_map_destroy(&now_binds);
  qa_layer_map_destroy(&start_binds);
  qa_layer_map_destroy(&spelled);
  for (size_t i = 0; i < cvars->count; i++) {
    const qa_layer_entry *entry = &cvars->entries[i];
    const char *name = entry->id;
    if (!qa_spelling_in(format, name, NULL)) continue;
    const qa_layer_entry *started = qa_layer_map_get(&loaded->cvars, name);
    char *reset = started == NULL && context->reset_value != NULL ? context->reset_value(context->reset_value_ctx, name) : NULL;
    const char *before = started != NULL ? started->value : reset;
    bool unchanged = before != NULL && qa_layer_same_value(before, entry->value);
    free(reset);
    if (unchanged) continue;
    if (context->local_cvars != NULL && qa_layer_map_has(context->local_cvars, name)) {
      qa_layer_map_set(&overrides->cvars, name, entry->name, entry->value);
      touched.overrides = true;
      continue;
    }
    qa_layer_map_set(&high->settings, name, NULL, entry->value);
    touched.high = true;
    touched.overrides = qa_layer_map_delete(&overrides->cvars, name) || touched.overrides;
  }
  return touched;
}

/* --- JSON documents --- */

/* A property key JS treats as an array index ("0", "17"; no sign, no leading zero, below 2^32 - 1): such keys
 * enumerate first, in ascending order, whatever order they were added in. */
static bool array_index(const char *key, uint32_t *out) {
  if (key[0] == '\0' || (key[0] == '0' && key[1] != '\0')) return false;
  uint64_t value = 0;
  for (const char *at = key; *at != '\0'; at++) {
    if (*at < '0' || *at > '9') return false;
    value = value * 10 + (uint64_t)(*at - '0');
    if (value > 4294967294ull) return false;
  }
  *out = (uint32_t)value;
  return true;
}
/* The positions of `count` keys in JS own-property order: array indices ascending, then the rest as given. */
static size_t *property_order(const char *const *keys, size_t count) {
  size_t *order = (size_t *)malloc((count == 0 ? 1 : count) * sizeof *order);
  if (order == NULL) out_of_memory();
  size_t used = 0;
  for (size_t i = 0; i < count; i++) {
    uint32_t index = 0;
    if (!array_index(keys[i], &index)) continue;
    size_t at = used++;
    for (; at > 0; at--) {
      uint32_t previous = 0;
      (void)array_index(keys[order[at - 1]], &previous);
      if (previous <= index) break;
      order[at] = order[at - 1];
    }
    order[at] = i;
  }
  for (size_t i = 0; i < count; i++) {
    uint32_t index = 0;
    if (!array_index(keys[i], &index)) order[used++] = i;
  }
  return order;
}

typedef struct text_buffer {
  char *data;
  size_t length;
  size_t capacity;
} text_buffer;
static void text_add_length(text_buffer *buffer, const char *text, size_t length) {
  if (buffer->length + length + 1 > buffer->capacity) {
    size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
    while (buffer->length + length + 1 > capacity) capacity *= 2;
    char *grown = (char *)realloc(buffer->data, capacity);
    if (grown == NULL) out_of_memory();
    buffer->data = grown;
    buffer->capacity = capacity;
  }
  memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
}
static void text_add(text_buffer *buffer, const char *text) { text_add_length(buffer, text, strlen(text)); }
static void text_indent(text_buffer *buffer, int depth) {
  for (int i = 0; i < depth; i++) text_add(buffer, "  ");
}
/* JSON.stringify of a string (UTF-8 passes through; C0 80 is U+0000, see nul_as_c080). */
static void text_add_json_string(text_buffer *buffer, const char *text) {
  text_add(buffer, "\"");
  for (const unsigned char *at = (const unsigned char *)text; *at != '\0'; at++) {
    char escape[8];
    if (at[0] == 0xc0 && at[1] == 0x80) {
      text_add(buffer, "\\u0000");
      at++;
      continue;
    }
    switch (*at) {
      case '"': text_add(buffer, "\\\""); break;
      case '\\': text_add(buffer, "\\\\"); break;
      case '\b': text_add(buffer, "\\b"); break;
      case '\f': text_add(buffer, "\\f"); break;
      case '\n': text_add(buffer, "\\n"); break;
      case '\r': text_add(buffer, "\\r"); break;
      case '\t': text_add(buffer, "\\t"); break;
      default:
        if (*at < 0x20) {
          (void)snprintf(escape, sizeof escape, "\\u%04x", (unsigned)*at);
          text_add(buffer, escape);
        } else {
          text_add_length(buffer, (const char *)at, 1);
        }
    }
  }
  text_add(buffer, "\"");
}
typedef enum map_shape { SHAPE_TEXT, SHAPE_BIND, SHAPE_CVAR } map_shape;
/* JSON.stringify(Object.fromEntries(map), null, 2) at `depth`: a Map<string, string>, or one of LayerBind
 * (`key`, `command`) or LayerCvar (`name`, `value`) values. */
static void text_add_map(text_buffer *buffer, const qa_layer_map *map, map_shape shape, int depth) {
  if (map->count == 0) {
    text_add(buffer, "{}");
    return;
  }
  const char **keys = (const char **)calloc(map->count, sizeof *keys);
  if (keys == NULL) out_of_memory();
  for (size_t i = 0; i < map->count; i++) keys[i] = map->entries[i].id;
  size_t *order = property_order(keys, map->count);
  free(keys);
  text_add(buffer, "{\n");
  for (size_t i = 0; i < map->count; i++) {
    const qa_layer_entry *entry = &map->entries[order[i]];
    text_indent(buffer, depth + 1);
    text_add_json_string(buffer, entry->id);
    text_add(buffer, ": ");
    if (shape == SHAPE_TEXT) {
      text_add_json_string(buffer, entry->value != NULL ? entry->value : "");
    } else {
      text_add(buffer, "{\n");
      text_indent(buffer, depth + 2);
      text_add(buffer, shape == SHAPE_BIND ? "\"key\": " : "\"name\": ");
      text_add_json_string(buffer, entry->name != NULL ? entry->name : entry->id);
      text_add(buffer, ",\n");
      text_indent(buffer, depth + 2);
      text_add(buffer, shape == SHAPE_BIND ? "\"command\": " : "\"value\": ");
      if (entry->value == NULL) text_add(buffer, "null");
      else text_add_json_string(buffer, entry->value);
      text_add(buffer, "\n");
      text_indent(buffer, depth + 1);
      text_add(buffer, "}");
    }
    text_add(buffer, i + 1 < map->count ? ",\n" : "\n");
  }
  free(order);
  text_indent(buffer, depth);
  text_add(buffer, "}");
}

static bool is_record(const qa_save_value *value) { return value != NULL && value->kind == QA_SAVE_OBJECT; }
/* object[key] of a parsed JSON object: a repeated key keeps its last value. */
static const qa_save_value *field(const qa_save_value *object, const char *key) {
  const qa_save_value *found = NULL;
  for (size_t i = 0; i < object->entry_count; i++)
    if (strcmp(object->entries[i].key, key) == 0) found = object->entries[i].value;
  return found;
}
typedef void (*record_entry_fn)(void *ctx, const char *key, const qa_save_value *value);
/* Object.entries(value): array-index keys first, then first-insertion order, each key once with its last value. */
static void each_entry(const qa_save_value *object, record_entry_fn fn, void *ctx) {
  size_t count = 0;
  const char **keys = (const char **)calloc(object->entry_count == 0 ? 1 : object->entry_count, sizeof *keys);
  const qa_save_value **values =
      (const qa_save_value **)calloc(object->entry_count == 0 ? 1 : object->entry_count, sizeof *values);
  if (keys == NULL || values == NULL) out_of_memory();
  for (size_t i = 0; i < object->entry_count; i++) {
    size_t at = 0;
    while (at < count && strcmp(keys[at], object->entries[i].key) != 0) at++;
    if (at == count) keys[count++] = object->entries[i].key;
    values[at] = object->entries[i].value;
  }
  size_t *order = property_order(keys, count);
  for (size_t i = 0; i < count; i++) fn(ctx, keys[order[i]], values[order[i]]);
  free(order);
  free(keys);
  free(values);
}
static const char *string_of(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_STRING ? value->text : NULL;
}

/* TS LayerStore.binds. */
static void bind_entry(void *ctx, const char *key, const qa_save_value *entry) {
  qa_layer_map *out = (qa_layer_map *)ctx;
  if (!is_record(entry)) return;
  const char *bound = string_of(field(entry, "key"));
  const qa_save_value *command = field(entry, "command");
  if (bound == NULL || command == NULL) return;
  if (command->kind == QA_SAVE_STRING) qa_layer_map_set(out, key, bound, command->text);
  else if (command->kind == QA_SAVE_NULL) qa_layer_map_set(out, key, bound, NULL);
}
static void read_binds(const qa_save_value *value, qa_layer_map *out) {
  if (is_record(value)) each_entry(value, bind_entry, out);
}
/* TS LayerStore.cvars: version 1 cvars were kept in the game's own spelling and values; convert them. */
typedef struct cvar_scope {
  qa_layer_map *out;
  double version;
} cvar_scope;
static void cvar_entry(void *ctx, const char *key, const qa_save_value *entry) {
  cvar_scope *scope = (cvar_scope *)ctx;
  (void)key;
  if (!is_record(entry)) return;
  const char *name = string_of(field(entry, "name"));
  const char *text = string_of(field(entry, "value"));
  if (name == NULL || text == NULL) return;
  if (scope->version >= LAYER_VERSION) {
    qa_layer_map_set(scope->out, name, name, text);
    return;
  }
  const char *canonical = NULL;
  char *value = NULL;
  if (!qa_setting_to_canonical(name, text, &canonical, &value)) return;
  qa_layer_map_set(scope->out, canonical, canonical, value);
  free(value);
}
static void read_cvars(const qa_save_value *value, double version, qa_layer_map *out) {
  cvar_scope scope = {out, version};
  if (is_record(value)) each_entry(value, cvar_entry, &scope);
}

void qa_layer_store_init(qa_layer_store *store, const char *root) {
  if (!qa_config_store_init(&store->store, root)) out_of_memory();
}
void qa_layer_store_destroy(qa_layer_store *store) { qa_config_store_destroy(&store->store); }

/* A U+0000 in a TS string is held as C0 80 in these UTF-8 texts (as native-config reads a NUL byte), so a
 * value keeps what follows it. Rewrites the parsed tree's strings and keys that hold a NUL. */
static char *c080_text(const char *text, size_t length) {
  size_t extra = 0;
  for (size_t i = 0; i < length; i++) extra += text[i] == '\0';
  char *out = (char *)malloc(length + extra + 1);
  if (out == NULL) out_of_memory();
  size_t at = 0;
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '\0') {
      out[at++] = (char)0xc0;
      out[at++] = (char)0x80;
    } else {
      out[at++] = text[i];
    }
  }
  out[at] = '\0';
  return out;
}
static void nul_as_c080(qa_save_value *value) {
  if (value == NULL) return;
  if (value->kind == QA_SAVE_STRING && value->text_length > strlen(value->text)) {
    char *text = c080_text(value->text, value->text_length);
    free(value->text);
    value->text = text;
    value->text_length = 0;
  }
  for (size_t i = 0; i < value->item_count; i++) nul_as_c080(value->items[i]);
  for (size_t i = 0; i < value->entry_count; i++) {
    qa_save_entry *entry = &value->entries[i];
    if (entry->key_length > strlen(entry->key)) {
      char *key = c080_text(entry->key, entry->key_length);
      free(entry->key);
      entry->key = key;
      entry->key_length = 0;
    }
    nul_as_c080(entry->value);
  }
}
/* TS LayerStore.read: NULL for an absent document; a damaged layer file is the absent one. The caller frees the tree. */
static qa_save_value *read_document(const qa_layer_store *store, const char *name, double *out_version) {
  size_t length = 0;
  char *text = qa_config_store_load_text(&store->store, name, &length);
  if (text == NULL) return NULL;
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, true);
  free(text);
  if (value == NULL) return NULL;
  if (!is_record(value)) {
    qa_save_value_free(value);
    return NULL;
  }
  nul_as_c080(value);
  const qa_save_value *version = field(value, "version");
  *out_version = version != NULL && version->kind == QA_SAVE_NUMBER && version->number == LAYER_VERSION ? LAYER_VERSION : 1;
  return value;
}
static void dump(const qa_layer_store *store, const char *name, text_buffer *buffer) {
  bool ok = qa_config_store_dump(&store->store, name, buffer->data != NULL ? buffer->data : "");
  free(buffer->data);
  if (!ok) qa_settings_raise_write_failure(); /* TS: store.dump rejects with the write's Node fs error */
}

/* Version 1 named the high level's settings by a vocabulary of its own; version 2 uses the canonical cvar names. */
static const char *v1_setting(const char *name) {
  static const struct {
    const char *name;
    const char *canonical;
  } table[] = {
    {"sensitivity", "sensitivity"}, {"volume", "volume"},         {"musicVolume", "bgmvolume"},
    {"gamma", "r_gamma"},           {"fullscreen", "r_fullscreen"}, {"width", "r_customwidth"},
    {"height", "r_customheight"},   {"fov", "fov"},
  };
  for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
    if (strcmp(table[i].name, name) == 0) return table[i].canonical;
  return NULL;
}
typedef struct high_scope {
  qa_high_level *high;
  double version;
} high_scope;
static void high_setting(void *ctx, const char *name, const qa_save_value *value) {
  high_scope *scope = (high_scope *)ctx;
  const char *text = string_of(value);
  if (text == NULL) return;
  if (scope->version >= LAYER_VERSION) {
    qa_layer_map_set(&scope->high->settings, name, NULL, text);
  } else if (strcmp(name, "invertPitch") == 0) {
    qa_layer_map_set(&scope->high->settings, "m_pitch", NULL, strcmp(text, "1") == 0 ? "-0.022" : "0.022");
  } else {
    const char *canonical = v1_setting(name);
    if (canonical != NULL) qa_layer_map_set(&scope->high->settings, canonical, NULL, text);
  }
}

bool qa_layer_store_load_high(const qa_layer_store *store, qa_high_level *out) {
  qa_high_level_init(out);
  double version = 1;
  qa_save_value *document = read_document(store, "layers/high.json", &version);
  if (document == NULL) return false;
  const qa_save_value *settings = field(document, "settings");
  high_scope scope = {out, version};
  if (is_record(settings)) each_entry(settings, high_setting, &scope);
  read_binds(field(document, "binds"), &out->binds);
  qa_save_value_free(document);
  if (version < LAYER_VERSION) qa_layer_store_save_high(store, out);
  return true;
}
void qa_layer_store_save_high(const qa_layer_store *store, const qa_high_level *high) {
  text_buffer buffer = {NULL, 0, 0};
  text_add(&buffer, "{\n  \"version\": 2,\n  \"settings\": ");
  text_add_map(&buffer, &high->settings, SHAPE_TEXT, 1);
  text_add(&buffer, ",\n  \"binds\": ");
  text_add_map(&buffer, &high->binds, SHAPE_BIND, 1);
  text_add(&buffer, "\n}\n");
  dump(store, "layers/high.json", &buffer);
}

/* `layers/games/${encodeURIComponent(id)}.json`, malloc'd. */
static char *game_document(const char *id) {
  text_buffer buffer = {NULL, 0, 0};
  text_add(&buffer, "layers/games/");
  for (const unsigned char *at = (const unsigned char *)id; *at != '\0'; at++) {
    unsigned char c = *at;
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                 c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
    if (plain) {
      text_add_length(&buffer, (const char *)at, 1);
    } else {
      char escape[4];
      (void)snprintf(escape, sizeof escape, "%%%02X", (unsigned)c);
      text_add(&buffer, escape);
    }
  }
  text_add(&buffer, ".json");
  return buffer.data;
}
/* TS snapshotOf. */
static void snapshot_of(const qa_written_record *record, qa_layer_snapshot *out) {
  qa_layer_snapshot_init(out);
  for (size_t i = 0; i < record->binds.count; i++)
    qa_layer_map_set(&out->binds, record->binds.entries[i].id, record->binds.entries[i].id, record->binds.entries[i].value);
  for (size_t i = 0; i < record->cvars.count; i++)
    qa_layer_map_set(&out->cvars, record->cvars.entries[i].id, record->cvars.entries[i].id, record->cvars.entries[i].value);
}
static void save_game_record(const qa_layer_store *store, const char *id, const qa_game_overrides *overrides,
                             const qa_written_record *record) {
  if (record == NULL) {
    qa_layer_store_save_game(store, id, overrides, NULL);
    return;
  }
  qa_layer_snapshot snapshot;
  snapshot_of(record, &snapshot);
  qa_layer_store_save_game(store, id, overrides, &snapshot);
  qa_layer_snapshot_destroy(&snapshot);
}

void qa_layer_store_load_game(const qa_layer_store *store, const char *id, qa_game_overrides *out_overrides,
                              qa_written_record *out_record, bool *out_has_record) {
  qa_game_overrides_init(out_overrides);
  qa_written_record_init(out_record);
  *out_has_record = false;
  char *name = game_document(id);
  double version = 1;
  qa_save_value *document = read_document(store, name, &version);
  free(name);
  if (document == NULL) return;
  const qa_save_value *overrides = field(document, "overrides");
  const qa_save_value *written = field(document, "written");
  if (is_record(written)) {
    qa_layer_map binds, cvars;
    qa_layer_map_init(&binds);
    qa_layer_map_init(&cvars);
    read_binds(field(written, "binds"), &binds);
    read_cvars(field(written, "cvars"), version, &cvars);
    for (size_t i = 0; i < binds.count; i++)
      qa_layer_map_set(&out_record->binds, binds.entries[i].id, NULL, binds.entries[i].value != NULL ? binds.entries[i].value : "");
    for (size_t i = 0; i < cvars.count; i++) qa_layer_map_set(&out_record->cvars, cvars.entries[i].id, NULL, cvars.entries[i].value);
    qa_layer_map_destroy(&binds);
    qa_layer_map_destroy(&cvars);
    *out_has_record = true;
  }
  if (is_record(overrides)) {
    read_binds(field(overrides, "binds"), &out_overrides->binds);
    read_cvars(field(overrides, "cvars"), version, &out_overrides->cvars);
  }
  qa_save_value_free(document);
  if (version < LAYER_VERSION) save_game_record(store, id, out_overrides, *out_has_record ? out_record : NULL);
}
void qa_layer_store_save_game(const qa_layer_store *store, const char *id, const qa_game_overrides *overrides,
                              const qa_layer_snapshot *record) {
  text_buffer buffer = {NULL, 0, 0};
  text_add(&buffer, "{\n  \"version\": 2,\n  \"overrides\": {\n    \"binds\": ");
  text_add_map(&buffer, &overrides->binds, SHAPE_BIND, 2);
  text_add(&buffer, ",\n    \"cvars\": ");
  text_add_map(&buffer, &overrides->cvars, SHAPE_CVAR, 2);
  text_add(&buffer, "\n  },\n  \"written\": ");
  if (record == NULL) {
    text_add(&buffer, "null");
  } else {
    text_add(&buffer, "{\n    \"binds\": ");
    text_add_map(&buffer, &record->binds, SHAPE_BIND, 2);
    text_add(&buffer, ",\n    \"cvars\": ");
    text_add_map(&buffer, &record->cvars, SHAPE_CVAR, 2);
    text_add(&buffer, "\n  }");
  }
  text_add(&buffer, "\n}\n");
  char *name = game_document(id);
  qa_cleanup node;
  qa_cleanup_push(&node, free, name);
  dump(store, name, &buffer);
  qa_cleanup_pop(&node);
  free(name);
}

/* --- GameLayers --- */
void qa_game_layers_init(qa_game_layers *layers, const qa_layer_store *store, qa_layer_format format,
                         const char *game_id) {
  memset(layers, 0, sizeof *layers);
  layers->store = store;
  layers->format = format;
  layers->game_id = dup_text(game_id);
  qa_high_level_init(&layers->high);
  qa_game_overrides_init(&layers->overrides);
  qa_written_record_init(&layers->record);
  qa_effective_set_init(&layers->current_loaded);
  qa_layer_map_init(&layers->default_binds);
  qa_layer_map_init(&layers->local_binds);
  qa_layer_map_init(&layers->local_cvars);
}
void qa_game_layers_destroy(qa_game_layers *layers) {
  free(layers->game_id);
  qa_high_level_destroy(&layers->high);
  qa_game_overrides_destroy(&layers->overrides);
  qa_written_record_destroy(&layers->record);
  qa_effective_set_destroy(&layers->current_loaded);
  qa_layer_map_destroy(&layers->default_binds);
  qa_layer_map_destroy(&layers->local_binds);
  qa_layer_map_destroy(&layers->local_cvars);
  memset(layers, 0, sizeof *layers);
}
const qa_effective_set *qa_game_layers_loaded(const qa_game_layers *layers) { return &layers->current_loaded; }
const qa_high_level *qa_game_layers_high_level(const qa_game_layers *layers) { return &layers->high; }
const qa_game_overrides *qa_game_layers_overrides(const qa_game_layers *layers) { return &layers->overrides; }
bool qa_game_layers_is_ready(const qa_game_layers *layers) { return layers->ready; }

void qa_game_layers_note_default_binds(qa_game_layers *layers, const qa_layer_pair *binds, size_t count) {
  qa_layer_map_clear(&layers->default_binds);
  for (size_t i = 0; i < count; i++) {
    char *folded = fold(binds[i].key);
    qa_layer_map_set(&layers->default_binds, folded, binds[i].key, binds[i].command);
    free(folded);
  }
}
void qa_game_layers_note_local_bind(qa_game_layers *layers, const char *key) {
  char *folded = fold(key);
  qa_layer_map_set(&layers->local_binds, folded, NULL, NULL);
  free(folded);
}
void qa_game_layers_note_local_cvar(qa_game_layers *layers, const char *name) {
  qa_setting_match match;
  if (qa_setting_for(name, &match)) {
    qa_layer_map_set(&layers->local_cvars, match.setting->canonical, NULL, NULL);
    return;
  }
  char *folded = fold(name);
  qa_layer_map_set(&layers->local_cvars, folded, NULL, NULL);
  free(folded);
}

/* What resolve has read but not yet handed to the layers; a raise (a migration's failed save) drops it. */
typedef struct resolve_scratch {
  qa_game_overrides overrides;
  qa_written_record record;
  qa_high_level high;
} resolve_scratch;
static void resolve_scratch_release(void *arg) {
  resolve_scratch *scratch = (resolve_scratch *)arg;
  qa_game_overrides_destroy(&scratch->overrides);
  qa_written_record_destroy(&scratch->record);
  qa_high_level_destroy(&scratch->high);
}
const qa_effective_set *qa_game_layers_resolve(qa_game_layers *layers, const qa_layer_snapshot *file) {
  resolve_scratch scratch;
  qa_game_overrides_init(&scratch.overrides);
  qa_written_record_init(&scratch.record);
  qa_high_level_init(&scratch.high);
  qa_cleanup scratch_node;
  qa_cleanup_push(&scratch_node, resolve_scratch_release, &scratch);
  bool has_record = false;
  qa_layer_store_load_game(layers->store, layers->game_id, &scratch.overrides, &scratch.record, &has_record);
  qa_game_overrides_destroy(&layers->overrides);
  qa_written_record_destroy(&layers->record);
  layers->overrides = scratch.overrides;
  layers->record = scratch.record;
  layers->has_record = has_record;
  qa_game_overrides_init(&scratch.overrides);
  qa_written_record_init(&scratch.record);
  bool dirty_high = false, dirty_game = false;
  qa_high_level *high = &scratch.high;
  if (!qa_layer_store_load_high(layers->store, high)) {
    qa_high_level_destroy(high);
    if (!has_record) qa_seed_high(layers->format, file, &layers->default_binds, high);
    else qa_high_level_init(high);
    /* Nothing to keep leaves the high level unwritten, so the next game with a file can still seed it. */
    dirty_high = high->binds.count > 0 || high->settings.count > 0;
  } else if (!has_record) {
    /* A native file the engine has not written yet is the player's own: where it differs from the high level it is
     * this game's override. */
    qa_written_record none;
    qa_written_record_init(&none);
    dirty_game = qa_detect_hand_edits(layers->format, file, &none, high, &layers->overrides);
    qa_written_record_destroy(&none);
  }
  if (has_record) dirty_game = qa_detect_hand_edits(layers->format, file, &layers->record, high, &layers->overrides);
  qa_cleanup_pop(&scratch_node);
  qa_high_level_destroy(&layers->high);
  layers->high = scratch.high; /* the layers own it from here */
  layers->has_high = true;
  if (dirty_high) qa_layer_store_save_high(layers->store, &layers->high);
  if (dirty_game) save_game_record(layers->store, layers->game_id, &layers->overrides, has_record ? &layers->record : NULL);
  qa_effective_set_destroy(&layers->current_loaded);
  qa_effective_set_build(layers->format, &layers->high, &layers->overrides, &layers->current_loaded);
  layers->ready = true;
  return &layers->current_loaded;
}

const qa_effective_set *qa_game_layers_absorb(qa_game_layers *layers, const qa_layer_pair *binds, size_t bind_count,
                                              const qa_layer_map *cvars, qa_layer_reset_value_fn reset_value,
                                              void *reset_value_ctx) {
  if (!layers->ready || !layers->has_high) qa_raise("Error", 0, 0, "%s", "Settings layers were not read before they were written");
  qa_absorb_context context = {&layers->default_binds, reset_value, reset_value_ctx, &layers->local_binds, &layers->local_cvars};
  qa_absorb_touched touched =
      qa_absorb_changes(layers->format, &layers->current_loaded, binds, bind_count, cvars, &context, &layers->high, &layers->overrides);
  if (touched.high) qa_layer_store_save_high(layers->store, &layers->high);
  qa_effective_set next;
  qa_effective_set_build(layers->format, &layers->high, &layers->overrides, &next);
  qa_effective_set_destroy(&layers->current_loaded);
  layers->current_loaded = next;
  qa_layer_map_clear(&layers->local_binds);
  qa_layer_map_clear(&layers->local_cvars);
  if (touched.overrides)
    save_game_record(layers->store, layers->game_id, &layers->overrides, layers->has_record ? &layers->record : NULL);
  return &layers->current_loaded;
}

void qa_game_layers_record_written(qa_game_layers *layers, const qa_layer_snapshot *file) {
  qa_written_record record;
  qa_written_record_init(&record);
  for (size_t i = 0; i < file->binds.count; i++)
    qa_layer_map_set(&record.binds, file->binds.entries[i].id, NULL, file->binds.entries[i].value != NULL ? file->binds.entries[i].value : "");
  for (size_t i = 0; i < file->cvars.count; i++) qa_layer_map_set(&record.cvars, file->cvars.entries[i].id, NULL, file->cvars.entries[i].value);
  qa_written_record_destroy(&layers->record);
  layers->record = record;
  layers->has_record = true;
  qa_layer_store_save_game(layers->store, layers->game_id, &layers->overrides, file);
}
