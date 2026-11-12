/* Port of src/app/bootstrap/campaign-unit.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/campaign_unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/provider_ownership.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"
/* Real save-image codec, installed when the caller passes a NULL
 * host codec (the TS CampaignUnit calls encode/decodeSaveImage
 * directly). Decode keeps the parsed image in retained scratch,
 * matching the header's borrowed-until-next-decode contract. */
static bool real_encode(void *ctx, const qa_save_image *image,
                        uint8_t **out_bytes, size_t *out_count) {
  (void)ctx;
  return qa_encode_save_image(image, out_bytes, out_count);
}
static const qa_save_image *real_decode(void *ctx, const uint8_t *bytes,
                                        size_t count) {
  static qa_save_image_result scratch;
  (void)ctx;
  qa_save_image_result_free(&scratch);
  if (!qa_decode_save_image(bytes, count, &scratch))
    qa_range_error("Out of memory");
  return &scratch.image;
}
typedef struct { char *key; uint8_t *bytes; size_t length; } world_row;
typedef struct { world_row *rows; size_t count, cap; } worlds_t;
struct qa_app_campaign_unit {
  qa_app_campaign_codec codec;
  bool has_current;
  char *current_content, *current_path;
  worlds_t worlds;
  double revision;
};
struct qa_app_campaign_unit_visit {
  qa_app_campaign_unit *unit;
  double revision;
  char *destination_content, *destination_path;
  worlds_t worlds;
  const qa_save_image *restore;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static qa_app_resource_request location_of(const qa_save_image *image) {
  return (qa_app_resource_request){image->recipe.map.geometry_content, image->recipe.map.geometry.requested_path};
}
static char *map_key(const char *content, const char *path) {
  const char *stem = path;
  if (strncmp(stem, "maps/", 5) == 0) stem += 5;
  size_t len = strlen(stem);
  if (len >= 4 && strcmp(stem + len - 4, ".bsp") == 0) len -= 4;
  size_t cap = strlen(content) + len + 2;
  char *key = malloc(cap);
  snprintf(key, cap, "%s/%.*s", content, (int)len, stem);
  return key;
}
static bool keys_equal(const char *c1, const char *p1, const char *c2, const char *p2) {
  char *a = map_key(c1, p1), *b = map_key(c2, p2);
  bool equal = strcmp(a, b) == 0;
  free(a); free(b);
  return equal;
}
static size_t campaign_entries(const qa_save_image *image, const qa_provider_checkpoint **out_first) {
  size_t count = 0;
  *out_first = NULL;
  for (size_t i = 0; i < image->provider_count; i++) {
    const qa_provider_checkpoint *entry = &image->providers[i];
    bool match = entry->provider && qa_campaign_unit_checkpoint.provider &&
                     strcmp(entry->provider, qa_campaign_unit_checkpoint.provider) == 0;
    if (!match && entry->schema && qa_campaign_unit_checkpoint.schema && strcmp(entry->schema, qa_campaign_unit_checkpoint.schema) == 0) match = true;
    if (match) { if (!*out_first) *out_first = entry; count++; }
  }
  if (count > 1) qa_raise("Error", 0, 0, "Duplicate campaign unit checkpoint");
  if (*out_first) qa_validate_save_provider_owner(**out_first, image->recipe.map.entities.provider);
  return count;
}
static void worlds_free(worlds_t *worlds) {
  for (size_t i = 0; i < worlds->count; i++) { free(worlds->rows[i].key); free(worlds->rows[i].bytes); }
  free(worlds->rows);
  *worlds = (worlds_t){0};
}
static void worlds_set(worlds_t *worlds, char *key, uint8_t *bytes, size_t length) {
  for (size_t i = 0; i < worlds->count; i++)
    if (strcmp(worlds->rows[i].key, key) == 0) { free(worlds->rows[i].bytes); worlds->rows[i].bytes = bytes; worlds->rows[i].length = length; free(key); return; }
  if (worlds->count == worlds->cap) { worlds->cap = worlds->cap ? worlds->cap * 2 : 8; worlds->rows = realloc(worlds->rows, worlds->cap * sizeof *worlds->rows); }
  worlds->rows[worlds->count++] = (world_row){key, bytes, length};
}
static void worlds_free_node(void *arg) { worlds_free(arg); }
static void free_row_node(void *arg) {
  world_row *row = arg;
  free(row->key);
  free(row->bytes);
}
static world_row *worlds_take(worlds_t *worlds, const char *key, world_row *out) {
  for (size_t i = 0; i < worlds->count; i++)
    if (strcmp(worlds->rows[i].key, key) == 0) {
      *out = worlds->rows[i];
      memmove(&worlds->rows[i], &worlds->rows[i + 1], (worlds->count - i - 1) * sizeof *worlds->rows);
      worlds->count--;
      return out;
    }
  return NULL;
}
static worlds_t worlds_copy(const worlds_t *source) {
  worlds_t copy = {0};
  for (size_t i = 0; i < source->count; i++) {
    uint8_t *bytes = malloc(source->rows[i].length ? source->rows[i].length : 1);
    memcpy(bytes, source->rows[i].bytes, source->rows[i].length);
    worlds_set(&copy, dup(source->rows[i].key), bytes, source->rows[i].length);
  }
  return copy;
}
/* worldOnly: validate entries (TS campaignEntries inside
 * worldOnly). */
static void validate_world_only(const qa_save_image *image) {
  const qa_provider_checkpoint *first;
  campaign_entries(image, &first);
}
/* TS worldOnly: shallow copy without the campaign-unit provider
 * (filtered by schema). providers_out is malloc'd scratch the
 * caller frees after encoding; entries stay borrowed. */
static qa_save_image world_only_image(const qa_save_image *image,
                                      qa_provider_checkpoint **providers_out) {
  validate_world_only(image);
  qa_save_image copy = *image;
  qa_provider_checkpoint *providers =
      malloc((image->provider_count ? image->provider_count : 1) *
             sizeof *providers);
  size_t count = 0;
  for (size_t i = 0; i < image->provider_count; i++) {
    const qa_provider_checkpoint *entry = &image->providers[i];
    if (entry->schema && qa_campaign_unit_checkpoint.schema &&
        strcmp(entry->schema, qa_campaign_unit_checkpoint.schema) == 0)
      continue;
    providers[count++] = *entry;
  }
  copy.providers = providers;
  copy.provider_count = count;
  *providers_out = providers;
  return copy;
}
qa_app_campaign_unit *qa_app_campaign_unit_create(qa_app_campaign_codec codec) {
  qa_app_campaign_unit *unit = calloc(1, sizeof *unit);
  if (!unit) return NULL;
  if (codec.encode == NULL && codec.decode == NULL) {
    codec.encode = real_encode;
    codec.decode = real_decode;
  }
  unit->codec = codec;
  return unit;
}
void qa_app_campaign_unit_destroy(qa_app_campaign_unit *unit) {
  if (!unit) return;
  free(unit->current_content);
  free(unit->current_path);
  worlds_free(&unit->worlds);
  free(unit);
}
qa_app_campaign_unit_visit *qa_app_campaign_unit_stage(qa_app_campaign_unit *unit, qa_app_resource_request destination,
                                                       bool new_unit, const qa_save_image *departure) {
  if (departure && unit->has_current) {
    qa_app_resource_request from = location_of(departure);
    if (!keys_equal(from.content, from.path, unit->current_content, unit->current_path))
      qa_raise("Error", 0, 0, "Campaign departure does not match the active map");
  }
  bool reset = new_unit || (unit->has_current && strcmp(unit->current_content, destination.content) != 0);
  worlds_t worlds = reset ? (worlds_t){0} : worlds_copy(&unit->worlds);
  qa_cleanup worlds_cleanup;
  qa_cleanup_push(&worlds_cleanup, worlds_free_node, &worlds);
  if (!reset && departure) {
    qa_provider_checkpoint *filtered = NULL;
    qa_save_image world = world_only_image(departure, &filtered);
    qa_app_resource_request from = location_of(departure);
    uint8_t *bytes = NULL; size_t length = 0;
    qa_cleanup filtered_cleanup;
    qa_cleanup_push(&filtered_cleanup, free, filtered);
    unit->codec.encode(unit->codec.ctx, &world, &bytes, &length);
    qa_cleanup_pop(&filtered_cleanup);
    free(filtered);
    worlds_set(&worlds, map_key(from.content, from.path), bytes, length);
  }
  char *dest_key = map_key(destination.content, destination.path);
  world_row saved;
  const qa_save_image *restore = NULL;
  if (worlds_take(&worlds, dest_key, &saved)) {
    free(dest_key);
    dest_key = NULL;
    qa_cleanup saved_cleanup;
    qa_cleanup_push(&saved_cleanup, free_row_node, &saved);
    restore = unit->codec.decode(unit->codec.ctx, saved.bytes, saved.length);
    qa_cleanup_pop(&saved_cleanup);
    free(saved.key);
    free(saved.bytes);
  }
  free(dest_key);
  qa_cleanup_pop(&worlds_cleanup);
  qa_app_campaign_unit_visit *visit = calloc(1, sizeof *visit);
  visit->unit = unit;
  visit->revision = unit->revision;
  visit->destination_content = dup(destination.content);
  visit->destination_path = dup(destination.path);
  visit->worlds = worlds;
  visit->restore = restore;
  return visit;
}
const qa_save_image *qa_app_campaign_unit_visit_restore(const qa_app_campaign_unit_visit *visit) { return visit->restore; }
void qa_app_campaign_unit_visit_destroy(qa_app_campaign_unit_visit *visit) {
  if (!visit) return;
  free(visit->destination_content);
  free(visit->destination_path);
  worlds_free(&visit->worlds);
  free(visit);
}
void qa_app_campaign_unit_visit_commit(qa_app_campaign_unit_visit *visit) {
  qa_app_campaign_unit *unit = visit->unit;
  if (visit->revision != unit->revision) qa_raise("Error", 0, 0, "Campaign visit was superseded");
  free(unit->current_content);
  free(unit->current_path);
  unit->current_content = dup(visit->destination_content);
  unit->current_path = dup(visit->destination_path);
  unit->has_current = true;
  worlds_free(&unit->worlds);
  unit->worlds = visit->worlds;
  visit->worlds = (worlds_t){0};
  unit->revision++;
  qa_app_campaign_unit_visit_destroy(visit);
}
qa_provider_checkpoint qa_app_campaign_unit_checkpoint(qa_app_campaign_unit *unit, bool has_current,
                                                       qa_app_resource_request current, uint8_t **out_bytes, size_t *out_length) {
  qa_save_value *root = qa_save_value_object();
  if (!has_current && !unit->has_current) qa_save_value_set(root, "current", qa_save_value_null());
  else {
    const char *content = has_current ? current.content : unit->current_content;
    const char *path = has_current ? current.path : unit->current_path;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value_set(entry, "content", qa_save_value_string(content));
    qa_save_value_set(entry, "path", qa_save_value_string(path));
    qa_save_value_set(root, "current", entry);
  }
  qa_save_value *list = qa_save_value_array();
  for (size_t i = 0; i < unit->worlds.count; i++) {
    qa_save_value *entry = qa_save_value_object();
    qa_save_value_set(entry, "map", qa_save_value_string(unit->worlds.rows[i].key));
    qa_save_value_set(entry, "bytes", qa_save_value_bytes(unit->worlds.rows[i].bytes, unit->worlds.rows[i].length));
    qa_save_value_push(list, entry);
  }
  qa_save_value_set(root, "worlds", list);
  *out_bytes = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  qa_provider_checkpoint checkpoint;
  checkpoint.provider = qa_campaign_unit_checkpoint.provider;
  checkpoint.schema = qa_campaign_unit_checkpoint.schema;
  checkpoint.version = qa_campaign_unit_checkpoint.version;
  checkpoint.bytes.bytes = *out_bytes;
  checkpoint.bytes.length = *out_length;
  return checkpoint;
}
qa_provider_checkpoint qa_app_campaign_unit_attach(qa_app_campaign_unit *unit, const qa_save_image *image,
                                                   uint8_t **out_bytes, size_t *out_length) {
  qa_app_resource_request current = location_of(image);
  validate_world_only(image);
  if (unit->has_current && !keys_equal(unit->current_content, unit->current_path, current.content, current.path))
    qa_raise("Error", 0, 0, "Campaign save does not match the active map");
  return qa_app_campaign_unit_checkpoint(unit, true, current, out_bytes, out_length);
}
/* Everything restore() owns while it can raise (a save file is untrusted input). */
typedef struct restore_state {
  qa_save_value *value;
  char *current_content, *current_path;
  char **maps;
  uint8_t **bytes;
  size_t *lengths;
  size_t entry_count;
  worlds_t worlds;
} restore_state;
static void restore_state_free(void *arg) {
  restore_state *state = arg;
  if (state->value != NULL) qa_save_value_free(state->value);
  free(state->current_content);
  free(state->current_path);
  for (size_t i = 0; i < state->entry_count; i++) {
    free(state->maps[i]);
    free(state->bytes[i]);
  }
  free(state->maps);
  free(state->bytes);
  free(state->lengths);
  worlds_free(&state->worlds);
  *state = (restore_state){0};
}
static char *checked_dup(const char *text) {
  char *copy = dup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
void qa_app_campaign_unit_restore(qa_app_campaign_unit *unit, const qa_save_image *image) {
  const qa_provider_checkpoint *checkpoint;
  campaign_entries(image, &checkpoint);
  qa_app_resource_request here = location_of(image);
  restore_state state = {0};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, restore_state_free, &state);
  state.current_content = checked_dup(here.content);
  state.current_path = checked_dup(here.path);
  if (checkpoint) {
    state.value = qa_decode_checkpoint_value(checkpoint->bytes.bytes, checkpoint->bytes.length);
    qa_save_reader reader, current_reader;
    qa_save_reader_init(&reader, state.value, "campaign-unit");
    qa_save_reader_field(&reader, "current", &current_reader);
    bool is_null = qa_save_reader_is_null(&current_reader);
    free(state.current_content);
    free(state.current_path);
    state.current_content = NULL;
    state.current_path = NULL;
    if (!is_null) {
      qa_save_reader content_reader, path_reader;
      qa_save_reader_field(&current_reader, "content", &content_reader);
      state.current_content = checked_dup(qa_save_reader_string(&content_reader));
      qa_save_reader_destroy(&content_reader);
      if (!qa_is_content_id(state.current_content)) qa_raise("Error", 0, 0, "Invalid campaign content identity");
      qa_save_reader_field(&current_reader, "path", &path_reader);
      state.current_path = checked_dup(qa_save_reader_string(&path_reader));
      qa_save_reader_destroy(&path_reader);
    }
    qa_save_reader_destroy(&current_reader);
    if (state.current_content == NULL || !keys_equal(state.current_content, state.current_path, here.content, here.path))
      qa_raise("Error", 0, 0, "Campaign checkpoint active map does not match its world");
    /* reader.field("worlds").list(...) reads every entry before any world is decoded. */
    qa_save_reader list_reader;
    qa_save_reader_field(&reader, "worlds", &list_reader);
    size_t count = qa_save_reader_list_count(&list_reader);
    state.maps = calloc(count ? count : 1, sizeof *state.maps);
    state.bytes = calloc(count ? count : 1, sizeof *state.bytes);
    state.lengths = calloc(count ? count : 1, sizeof *state.lengths);
    if (!state.maps || !state.bytes || !state.lengths) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item, map_reader, bytes_reader;
      qa_save_reader_item(&list_reader, i, &item);
      qa_save_reader_field(&item, "map", &map_reader);
      state.maps[i] = checked_dup(qa_save_reader_string(&map_reader));
      state.entry_count = i + 1;
      qa_save_reader_destroy(&map_reader);
      qa_save_reader_field(&item, "bytes", &bytes_reader);
      size_t length = 0;
      const uint8_t *data = qa_save_reader_bytes(&bytes_reader, &length);
      state.bytes[i] = malloc(length ? length : 1);
      if (state.bytes[i] == NULL) qa_range_error("Out of memory");
      memcpy(state.bytes[i], data, length);
      state.lengths[i] = length;
      qa_save_reader_destroy(&bytes_reader);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&list_reader);
    qa_save_reader_destroy(&reader);
    for (size_t i = 0; i < count; i++) {
      const char *map = state.maps[i];
      const qa_save_image *saved = unit->codec.decode(unit->codec.ctx, state.bytes[i], state.lengths[i]);
      qa_app_resource_request saved_location = location_of(saved);
      char *saved_key = map_key(saved_location.content, saved_location.path);
      char *current_key = map_key(state.current_content, state.current_path);
      bool duplicate = false;
      for (size_t j = 0; j < state.worlds.count; j++) if (strcmp(state.worlds.rows[j].key, map) == 0) duplicate = true;
      bool invalid = strcmp(map, saved_key) != 0 || strcmp(saved_location.content, state.current_content) != 0 ||
          strcmp(map, current_key) == 0 || duplicate;
      free(saved_key);
      free(current_key);
      /* `|| campaignEntries(saved).length !== 0`: evaluated (and able to throw) only when the rest held. */
      if (!invalid) {
        const qa_provider_checkpoint *nested;
        invalid = campaign_entries(saved, &nested) != 0;
      }
      if (invalid) qa_raise("Error", 0, 0, "Invalid campaign unit world checkpoint");
      /* worlds.set(entry.map, entry.bytes.slice()): the row takes this entry's copies. */
      worlds_set(&state.worlds, state.maps[i], state.bytes[i], state.lengths[i]);
      state.maps[i] = NULL;
      state.bytes[i] = NULL;
    }
  }
  qa_cleanup_pop(&cleanup);
  free(unit->current_content);
  free(unit->current_path);
  unit->current_content = state.current_content;
  unit->current_path = state.current_path;
  unit->has_current = true;
  worlds_free(&unit->worlds);
  unit->worlds = state.worlds;
  unit->revision++;
  state.current_content = NULL;
  state.current_path = NULL;
  state.worlds = (worlds_t){0};
  restore_state_free(&state);
}
