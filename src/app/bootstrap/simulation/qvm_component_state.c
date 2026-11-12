/* Port of src/app/bootstrap/simulation/qvm-component-state.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/qvm_component_state.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/types.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

#define CLIENTS 64
#define CONFIGSTRINGS 1024

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---- qvmWeaponInitializationEntities ---- */
static bool init_class(const char *classname) {
  static const char *const classes[] = {"worldspawn", "info_player_start", "info_player_deathmatch", "info_player_intermission",
    "team_CTF_redplayer", "team_CTF_blueplayer", "team_CTF_redspawn", "team_CTF_bluespawn"};
  for (size_t k = 0; k < 8; k++) if (is(classname ? classname : "", classes[k])) return true;
  return false;
}
typedef struct entities_scratch { qa_q1_entity *entities; size_t count; char *out; } entities_scratch;
static void entities_scratch_free(void *arg) { entities_scratch *e = arg; qa_q1_entities_destroy(e->entities, e->count); free(e->out); }
char *qa_app_sim_qvm_weapon_initialization_entities(const char *text) {
  entities_scratch scratch; memset(&scratch, 0, sizeof scratch);
  scratch.entities = qa_q1_parse_entities(text, &scratch.count);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, entities_scratch_free, &scratch);
  size_t worlds = 0;
  for (size_t i = 0; i < scratch.count; i++) if (is(qa_q1_entity_value(&scratch.entities[i], "classname"), "worldspawn")) worlds++;
  if (worlds != 1) qa_raise("Error", 0, 0, "QVM component requires one authored worldspawn");
  size_t length = 0, capacity = 64; scratch.out = checked(malloc(capacity));
#define APPEND(text_) do { const char *t_ = (text_); size_t n_ = strlen(t_); \
    while (length + n_ + 1 > capacity) { capacity *= 2; scratch.out = checked(realloc(scratch.out, capacity)); } \
    memcpy(scratch.out + length, t_, n_); length += n_; } while (0)
  for (size_t i = 0; i < scratch.count; i++) {
    const qa_q1_entity *entity = &scratch.entities[i];
    if (!init_class(qa_q1_entity_value(entity, "classname"))) continue;
    APPEND("{\n");
    for (size_t p = 0; p < entity->property_count; p++) {
      const char *key = entity->properties[p].key, *value = entity->properties[p].value;
      if (p) APPEND("\n");
      /* quote(value): a C string holds no NUL, so only the quote can make a field unquotable. */
      if (strchr(key, '"')) qa_raise("Error", 0, 0, "Unquotable QVM spawn field");
      APPEND("\""); APPEND(key); APPEND("\" ");
      if (strchr(value, '"')) qa_raise("Error", 0, 0, "Unquotable QVM spawn field");
      APPEND("\""); APPEND(value); APPEND("\"");
    }
    APPEND("\n}\n");
  }
#undef APPEND
  scratch.out[length] = '\0';
  char *out = scratch.out; scratch.out = NULL;
  qa_cleanup_pop(&cleanup); entities_scratch_free(&scratch);
  return out;
}

/* ---- Map<number, string> ---- */
static qa_app_sim_qvm_component_text *text_entry(const qa_app_sim_qvm_component_texts *texts, double key) {
  for (size_t i = 0; i < texts->count; i++) if (texts->entries[i].key == key) return &texts->entries[i];
  return NULL;
}
const char *qa_app_sim_qvm_component_texts_get(const qa_app_sim_qvm_component_texts *texts, double key) {
  const qa_app_sim_qvm_component_text *entry = text_entry(texts, key);
  return entry == NULL ? NULL : entry->value;
}
void qa_app_sim_qvm_component_texts_set(qa_app_sim_qvm_component_texts *texts, double key, const char *value) {
  size_t length = strlen(value);
  char *copy = checked(malloc(length + 1));
  memcpy(copy, value, length + 1);
  qa_app_sim_qvm_component_text *existing = text_entry(texts, key);
  if (existing != NULL) { free(existing->value); existing->value = copy; return; }
  if (texts->count == texts->capacity) {
    size_t capacity = texts->capacity == 0 ? 8 : texts->capacity * 2;
    qa_app_sim_qvm_component_text *grown = realloc(texts->entries, capacity * sizeof *grown);
    if (grown == NULL) { free(copy); qa_range_error("Out of memory"); }
    texts->entries = grown; texts->capacity = capacity;
  }
  texts->entries[texts->count++] = (qa_app_sim_qvm_component_text){key, copy};
}
void qa_app_sim_qvm_component_texts_delete(qa_app_sim_qvm_component_texts *texts, double key) {
  qa_app_sim_qvm_component_text *entry = text_entry(texts, key);
  if (entry == NULL) return;
  free(entry->value);
  size_t index = (size_t)(entry - texts->entries);
  memmove(&texts->entries[index], &texts->entries[index + 1], (texts->count - index - 1) * sizeof *texts->entries);
  texts->count--;
}
void qa_app_sim_qvm_component_texts_clear(qa_app_sim_qvm_component_texts *texts) {
  for (size_t i = 0; i < texts->count; i++) free(texts->entries[i].value);
  texts->count = 0;
}

/* ---- QvmComponentState ---- */
void qa_app_sim_qvm_component_state_init(qa_app_sim_qvm_component_state *state, const char *entity_text, qa_cvar_registry *cvars) {
  memset(state, 0, sizeof *state);
  state->cvars = cvars;
  char *entities = qa_app_sim_qvm_weapon_initialization_entities(entity_text);
  bool cursor_ok = qa_common_parse_cursor_init(&state->cursor, entities, strlen(entities), false);
  free(entities);
  if (!cursor_ok) qa_range_error("Out of memory");
  state->has_cursor = true;
  qa_common_parse_state_init(&state->parser);
}
void qa_app_sim_qvm_component_state_destroy(qa_app_sim_qvm_component_state *state) {
  if (state->has_cursor) qa_common_parse_cursor_destroy(&state->cursor);
  state->has_cursor = false;
  qa_app_sim_qvm_component_texts_clear(&state->configstrings);
  qa_app_sim_qvm_component_texts_clear(&state->userinfo);
  free(state->configstrings.entries); free(state->userinfo.entries);
  state->configstrings = (qa_app_sim_qvm_component_texts){0};
  state->userinfo = (qa_app_sim_qvm_component_texts){0};
}

static void put(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("Out of memory");
}
static void push(qa_save_value *array, qa_save_value *child) {
  if (child == NULL || !qa_save_value_push(array, child)) qa_range_error("Out of memory");
}
static qa_save_value *object(void) { return checked(qa_save_value_object()); }
static qa_save_value *array(void) { return checked(qa_save_value_array()); }
/* TS QvmFiles.captureCheckpoint value form (see compat/qvm/mod_provider.c). */
static qa_save_value *mount_value(const qa_mount_identity *identity, bool archive, qa_archive_format format, const char *path,
    qa_content_digest digest) {
  qa_save_value *value = object(), *id = object();
  put(id, "id", qa_save_value_string(identity->id)); put(id, "content", qa_save_value_string(identity->content));
  put(id, "generation", qa_save_value_number((double)identity->generation)); put(value, "identity", id);
  put(value, "kind", qa_save_value_string(archive ? "archive" : "loose"));
  if (archive) {
    static const char *const formats[] = {"pak", "pk3", "kpf", "zip"};
    put(value, "format", qa_save_value_string(formats[format])); put(value, "archivePath", qa_save_value_string(path));
    put(value, "archiveDigest", qa_save_value_string(digest));
  } else put(value, "rootPath", qa_save_value_string(path));
  return value;
}
static qa_save_value *resource_value(const qa_resolved_resource_reference *resource) {
  qa_save_value *value = object(), *provenance = object(), *resolution = object();
  put(value, "id", qa_save_value_string(resource->id)); put(value, "requestedPath", qa_save_value_string(resource->requested_path));
  put(provenance, "memberPath", qa_save_value_string(resource->provenance.member_path));
  if (resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    const qa_archive_mount *mount = resource->provenance.archive_mount;
    put(provenance, "kind", qa_save_value_string("archive"));
    put(provenance, "mount", mount_value(&mount->identity, true, mount->format, mount->archive_path, mount->archive_digest));
    put(provenance, "memberIndex", qa_save_value_number((double)resource->provenance.member_index));
  } else {
    const qa_loose_mount *mount = resource->provenance.loose_mount;
    put(provenance, "kind", qa_save_value_string("loose"));
    put(provenance, "mount", mount_value(&mount->identity, false, QA_ARCHIVE_FORMAT_PAK, mount->root_path, NULL));
  }
  put(value, "provenance", provenance); put(value, "digest", qa_save_value_string(resource->digest));
  put(value, "byteLength", qa_save_value_number((double)resource->byte_length));
  put(resolution, "plan", qa_save_value_string(resource->resolution.plan));
  switch (resource->resolution.kind) {
    case QA_RESOURCE_RESOLUTION_DEFAULT_ORDER:
      put(resolution, "kind", qa_save_value_string("default-order")); put(resolution, "rank", qa_save_value_number((double)resource->resolution.rank));
      break;
    case QA_RESOURCE_RESOLUTION_PREFIX_ORDER:
      put(resolution, "kind", qa_save_value_string("prefix-order")); put(resolution, "rank", qa_save_value_number((double)resource->resolution.rank));
      put(resolution, "prefix", qa_save_value_string(resource->resolution.prefix));
      break;
    case QA_RESOURCE_RESOLUTION_LINK:
      put(resolution, "kind", qa_save_value_string("link")); put(resolution, "sourcePrefix", qa_save_value_string(resource->resolution.source_prefix));
      put(resolution, "targetPath", qa_save_value_string(resource->resolution.target_path));
      break;
  }
  put(value, "resolution", resolution);
  return value;
}
static qa_save_value *files_value(qa_qvm_files *files) {
  qa_qvm_files_checkpoint checkpoint;
  if (!qa_qvm_files_capture_checkpoint(files, &checkpoint)) qa_range_error("Out of memory");
  qa_save_value *value = object(), *handles = array();
  for (size_t i = 0; i < checkpoint.handle_count; i++) {
    const qa_qvm_file_handle_checkpoint *entry = &checkpoint.handles[i];
    qa_save_value *handle = object();
    put(handle, "slot", qa_save_value_number(entry->slot)); put(handle, "kind", qa_save_value_string(entry->is_write ? "write" : "read"));
    if (entry->is_write) {
      qa_save_value *file = object();
      put(file, "path", qa_save_value_string(entry->file.path));
      put(file, "mode", qa_save_value_string(entry->file.mode == QA_WRITABLE_WRITE ? "write"
          : entry->file.mode == QA_WRITABLE_APPEND ? "append" : "append-sync"));
      put(file, "position", qa_save_value_number((double)entry->file.position)); put(handle, "file", file);
    } else {
      qa_save_value *resource = object();
      put(resource, "reference", resource_value(&entry->reference)); put(resource, "bytes", qa_save_value_bytes(entry->bytes, entry->byte_length));
      put(handle, "resource", resource); put(handle, "position", qa_save_value_number(entry->position));
    }
    push(handles, handle);
  }
  put(value, "handles", handles);
  qa_qvm_files_checkpoint_destroy(&checkpoint);
  return value;
}

void qa_app_sim_qvm_component_state_capture(const qa_app_sim_qvm_component_state *state, const qa_qvm_game_data *data,
    qa_qvm_files *files, qa_save_value *record) {
  qa_qvm_game_data_state layout = qa_qvm_game_data_checkpoint(data);
  qa_save_value *data_value = object();
  put(data_value, "entitiesWord", qa_save_value_number(layout.entities_word)); put(data_value, "numEntities", qa_save_value_number(layout.num_entities));
  put(data_value, "entityStride", qa_save_value_number(layout.entity_stride)); put(data_value, "clientsWord", qa_save_value_number(layout.clients_word));
  put(data_value, "clientStride", qa_save_value_number(layout.client_stride));
  put(record, "data", data_value);
  put(record, "cvars", qa_cvar_registry_capture_save_state(state->cvars));
  qa_save_value *configstrings = array();
  for (size_t i = 0; i < state->configstrings.count; i++) {
    qa_save_value *row = object();
    put(row, "index", qa_save_value_number(state->configstrings.entries[i].key));
    put(row, "value", qa_save_value_string(state->configstrings.entries[i].value));
    push(configstrings, row);
  }
  put(record, "configstrings", configstrings);
  qa_save_value *userinfo = array();
  for (size_t i = 0; i < state->userinfo.count; i++) {
    qa_save_value *row = object();
    put(row, "slot", qa_save_value_number(state->userinfo.entries[i].key));
    put(row, "value", qa_save_value_string(state->userinfo.entries[i].value));
    push(userinfo, row);
  }
  put(record, "userinfo", userinfo);
  put(record, "files", files_value(files));
  put(record, "entityText", qa_save_value_string(state->cursor.source));
  put(record, "cursor", state->cursor.has_offset ? qa_save_value_number((double)state->cursor.offset) : qa_save_value_null());
  put(record, "parser", qa_common_parse_state_capture(&state->parser));
}

static qa_save_reader fld(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static double int_field(const qa_save_reader *reader, const char *name, bool minimum) {
  qa_save_reader f = fld(reader, name);
  double value = minimum ? qa_save_reader_integer_min(&f, 0) : qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
void qa_app_sim_qvm_component_state_restore(qa_app_sim_qvm_component_state *state, const qa_save_reader *reader,
    qa_qvm_game_data *data, qa_qvm_files *files, const char *label) {
  qa_save_reader f = fld(reader, "entityText");
  (void)qa_save_reader_literal_string(&f, state->cursor.source);
  qa_save_reader_destroy(&f);
  qa_save_reader saved = fld(reader, "data");
  qa_qvm_game_data_state layout;
  layout.entities_word = int_field(&saved, "entitiesWord", false);
  layout.num_entities = int_field(&saved, "numEntities", true);
  layout.entity_stride = int_field(&saved, "entityStride", true);
  layout.clients_word = int_field(&saved, "clientsWord", false);
  layout.client_stride = int_field(&saved, "clientStride", true);
  qa_save_reader_destroy(&saved);
  qa_qvm_game_data_restore(data, layout);
  f = fld(reader, "cvars"); qa_cvar_registry_restore_save_state(state->cvars, f.value); qa_save_reader_destroy(&f);
  qa_app_sim_qvm_component_texts_clear(&state->configstrings);
  qa_app_sim_qvm_component_texts_clear(&state->userinfo);
  /* TS reads each list in full before it checks the entries. */
  qa_save_reader list = fld(reader, "userinfo");
  size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell; qa_save_reader_item(&list, i, &cell);
    (void)int_field(&cell, "slot", true);
    qa_save_reader value = fld(&cell, "value"); (void)qa_save_reader_string(&value);
    qa_save_reader_destroy(&value); qa_save_reader_destroy(&cell);
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell; qa_save_reader_item(&list, i, &cell);
    double slot = int_field(&cell, "slot", true);
    qa_save_reader value = fld(&cell, "value"); const char *text = qa_save_reader_string(&value);
    if (slot >= CLIENTS || qa_app_sim_qvm_component_texts_get(&state->userinfo, slot) != NULL)
      qa_raise("Error", 0, 0, "Invalid saved %s client", label);
    qa_app_sim_qvm_component_texts_set(&state->userinfo, slot, text);
    qa_save_reader_destroy(&value); qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  list = fld(reader, "configstrings");
  count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell; qa_save_reader_item(&list, i, &cell);
    (void)int_field(&cell, "index", true);
    qa_save_reader value = fld(&cell, "value"); (void)qa_save_reader_string(&value);
    qa_save_reader_destroy(&value); qa_save_reader_destroy(&cell);
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell; qa_save_reader_item(&list, i, &cell);
    double index = int_field(&cell, "index", true);
    qa_save_reader value = fld(&cell, "value"); const char *text = qa_save_reader_string(&value);
    if (index >= CONFIGSTRINGS || qa_app_sim_qvm_component_texts_get(&state->configstrings, index) != NULL)
      qa_raise("Error", 0, 0, "Invalid saved %s configstring", label);
    qa_app_sim_qvm_component_texts_set(&state->configstrings, index, text);
    qa_save_reader_destroy(&value); qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  /* reader.field("cursor").nullable(cell => cell.integer(0)) */
  f = fld(reader, "cursor");
  if (qa_save_reader_is_null(&f)) qa_common_parse_cursor_set_null(&state->cursor);
  else qa_common_parse_cursor_set_offset(&state->cursor, (size_t)qa_save_reader_integer_min(&f, 0));
  qa_save_reader_destroy(&f);
  f = fld(reader, "parser"); qa_common_parse_state_restore(&state->parser, f.value); qa_save_reader_destroy(&f);
  f = fld(reader, "files"); qa_qvm_files_restore_checkpoint(files, f.value); qa_save_reader_destroy(&f);
}

/* ---- qvmComponentSpatial ---- */
static qa_qvm_trace_record spatial_trace(void *ctx, const qa_qvm_server_trace_query *input) {
  qa_app_sim_qvm_component_spatial *spatial = ctx;
  const qa_app_sim_qvm_component_spatial_options *o = &spatial->options;
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = input->start; query.end = input->end; query.shape.kind = input->shape_kind; query.shape.bounds = input->shape_bounds;
  query.target.is_model = false; query.numeric = qa_q3_binary32_profile;
  qa_actor_id pass;
  query.pass_actor = o->actor_for_slot(o->ctx, input->pass_entity_num, &pass) ? &pass : NULL;
  query.policy.kind = "q3"; query.policy.contents_mask = input->mask;
  query.policy.curves = qa_cvar_registry_variable_value(o->cvars, "cm_noCurves") == 0;
  query.policy.player_curve_clip = qa_cvar_registry_variable_value(o->cvars, "cm_playerCurveClip") != 0;
  qa_trace_result trace = qa_shared_trace(o->scene, query);
  if (!is(trace.kind, "q3")) qa_raise("Error", 0, 0, "%s trace lost its source policy", o->label);
  qa_qvm_trace_record out; memset(&out, 0, sizeof out);
  out.result.fraction = trace.fraction; out.result.end = trace.end; out.result.all_solid = trace.all_solid; out.result.start_solid = trace.start_solid;
  out.result.contents = trace.contents; out.result.surface_flags = trace.surface_flags;
  out.result.plane.plane = trace.source_plane.plane; out.result.plane.type = trace.source_plane.type; out.result.plane.signbits = trace.source_plane.signbits;
  out.entity_num = trace.hit.kind == QA_TRACE_HIT_ACTOR ? (int32_t)o->slot_of(o->ctx, trace.hit.actor) : trace.fraction == 1 ? 1023 : 1022;
  return out;
}
static int32_t spatial_point_contents(void *ctx, qa_vec3 point, int32_t pass_entity) {
  qa_app_sim_qvm_component_spatial *spatial = ctx;
  const qa_app_sim_qvm_component_spatial_options *o = &spatial->options;
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = point; query.target.is_model = false;
  qa_actor_id pass;
  query.pass_actor = o->actor_for_slot(o->ctx, pass_entity, &pass) ? &pass : NULL;
  query.numeric = qa_q3_binary32_profile;
  query.policy.kind = "q3"; query.policy.contents_mask = -1; query.policy.curves = true; query.policy.player_curve_clip = true;
  qa_point_contents_result sample = qa_shared_point_contents(o->scene, query);
  if (!is(sample.kind, "q3")) qa_raise("Error", 0, 0, "%s contents lost its source policy", o->label);
  return qa_js_to_int32(sample.contents);
}
static size_t spatial_area_entities(void *ctx, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity) {
  qa_app_sim_qvm_component_spatial *spatial = ctx;
  const qa_app_sim_qvm_component_spatial_options *o = &spatial->options;
  size_t found = 0;
  const qa_spatial_actor *queried = qa_shared_query_actors(o->scene, bounds, NULL, &found);
  /* The query result is the scene's scratch: copy the actors before any callback can query again. */
  qa_actor_id *actors = checked(malloc((found ? found : 1) * sizeof *actors));
  for (size_t i = 0; i < found; i++) actors[i] = queried[i].body.actor;
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, actors);
  size_t kept = 0;
  for (size_t i = 0; i < found; i++) if (o->hidden == NULL || !o->hidden(o->ctx, actors[i])) actors[kept++] = actors[i];
  size_t count = maximum < 0 ? kept : kept < (size_t)maximum ? kept : (size_t)maximum;
  for (size_t i = 0; i < count; i++) {
    int32_t slot = (int32_t)o->slot_of(o->ctx, actors[i]);
    if (i < capacity) out[i] = slot;
  }
  qa_cleanup_pop(&cleanup); free(actors);
  return count;
}
/* options.reject(operation) returns never. */
static noreturn void reject(qa_app_sim_qvm_component_spatial *spatial, const char *operation) {
  spatial->options.reject(spatial->options.ctx, operation);
  qa_raise("Error", 0, 0, "%s source cannot %s", spatial->options.label, operation);
}
static bool spatial_entity_contact(void *ctx, qa_bounds bounds, int32_t slot, bool capsule) {
  (void)bounds; (void)slot; (void)capsule;
  reject(ctx, "perform undeclared brush contact");
}
static void spatial_set_brush_model(void *ctx, int32_t slot, const char *name) {
  (void)slot; (void)name;
  reject(ctx, "bind a primary brush");
}
static void spatial_adjust_area_portal_state(void *ctx, int32_t slot, bool open) {
  (void)slot; (void)open;
  reject(ctx, "change world portals");
}
static bool spatial_in_pvs(void *ctx, qa_vec3 first, qa_vec3 second, bool ignore) {
  qa_app_sim_qvm_component_spatial *spatial = ctx;
  qa_shared_scene_queries *scene = spatial->options.scene;
  double a = qa_shared_point_leaf(scene, first), b = qa_shared_point_leaf(scene, second);
  return qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, a), qa_shared_leaf_cluster(scene, b), "pvs")
      && (ignore || qa_shared_areas_connected(scene, qa_shared_leaf_area(scene, a), qa_shared_leaf_area(scene, b)));
}
static bool spatial_areas_connected(void *ctx, int32_t first, int32_t second) {
  qa_app_sim_qvm_component_spatial *spatial = ctx;
  return qa_shared_areas_connected(spatial->options.scene, first, second);
}
static void spatial_link(void *ctx, int32_t slot) {
  qa_app_sim_qvm_component_spatial *spatial = ctx;
  qa_qvm_shared_entity *r = qa_qvm_game_data_entity(spatial->options.data, slot);
  qa_qvm_shared_entity_set_linked(r, true);
  qa_vec3 origin = qa_qvm_shared_entity_get_current_origin(r), mins = qa_qvm_shared_entity_get_mins(r), maxs = qa_qvm_shared_entity_get_maxs(r);
  qa_qvm_shared_entity_set_absmin(r, (qa_vec3){origin.x + mins.x - 1, origin.y + mins.y - 1, origin.z + mins.z - 1});
  qa_qvm_shared_entity_set_absmax(r, (qa_vec3){origin.x + maxs.x + 1, origin.y + maxs.y + 1, origin.z + maxs.z + 1});
}
static void spatial_unlink(void *ctx, int32_t slot) {
  qa_app_sim_qvm_component_spatial *spatial = ctx;
  qa_qvm_shared_entity_set_linked(qa_qvm_game_data_entity(spatial->options.data, slot), false);
}
void qa_app_sim_qvm_component_spatial_init(qa_app_sim_qvm_component_spatial *spatial, qa_app_sim_qvm_component_spatial_options options) {
  spatial->options = options;
  spatial->operations = (qa_qvm_server_spatial_operations){spatial, spatial_trace, spatial_point_contents, spatial_area_entities,
      spatial_entity_contact, spatial_set_brush_model, spatial_adjust_area_portal_state, spatial_in_pvs, spatial_areas_connected,
      spatial_link, spatial_unlink};
}
