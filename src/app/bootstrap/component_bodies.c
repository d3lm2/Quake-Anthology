/* Port of src/app/bootstrap/component-bodies.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_bodies.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mods/declaration_writer.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
static qa_save_value *checked(qa_save_value *value) {
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static void push(qa_save_value *array, qa_save_value *item) {
  if (!qa_save_value_push(array, checked(item))) qa_range_error("Out of memory");
}
static void push_number(qa_save_value *array, double value) { push(array, qa_save_value_number(value)); }
/* TS materialKey: JSON.stringify of the dedup fields (strings quoted, NaN and infinities as null; the skin's
 * surfaces only without a shader). Malloc'd. */
static char *material_key(const qa_app_body_material *pass) {
  qa_save_value *key = checked(qa_save_value_array());
  push(key, pass->has_custom_shader && pass->custom_shader_name ? qa_save_value_string(pass->custom_shader_name)
                                                                 : qa_save_value_null());
  push_number(key, pass->shader_rgba.x);
  push_number(key, pass->shader_rgba.y);
  push_number(key, pass->shader_rgba.z);
  push_number(key, pass->shader_rgba.w);
  push_number(key, pass->shader_tex_coord.x);
  push_number(key, pass->shader_tex_coord.y);
  push_number(key, pass->shader_time);
  push_number(key, pass->render_flags);
  push_number(key, pass->lighting_origin.x);
  push_number(key, pass->lighting_origin.y);
  push_number(key, pass->lighting_origin.z);
  push_number(key, pass->shadow_plane);
  push(key, qa_save_value_boolean(pass->non_normalized_axes));
  if (!pass->has_custom_shader && pass->custom_skin != NULL) {
    /* customSkin.surfaces: [{ name, shader }, ...] */
    qa_save_value *surfaces = checked(qa_save_value_array());
    for (size_t i = 0; i < pass->custom_skin->surface_count; i++) {
      qa_save_value *surface = checked(qa_save_value_object());
      if (!qa_save_value_set(surface, "name", checked(qa_save_value_string(pass->custom_skin->surfaces[i].name))) ||
          !qa_save_value_set(surface, "shader", checked(qa_save_value_string(pass->custom_skin->surfaces[i].shader))))
        qa_range_error("Out of memory");
      push(surfaces, surface);
    }
    push(key, surfaces);
  } else if (!pass->has_custom_shader && pass->has_custom_skin && pass->custom_skin_surfaces) {
    push(key, qa_save_value_string(pass->custom_skin_surfaces)); /* producers without the skin record */
  } else {
    push(key, qa_save_value_null());
  }
  char *text = qa_json_stringify_value(key, NULL);
  qa_save_value_free(key);
  return text;
}
typedef struct { char *key; size_t count; } occurrence_row;
size_t qa_app_body_materials(const qa_app_component_body *body, const char *part,
                             const qa_app_body_material **out, size_t capacity) {
  /* seen: `${key}/${ordinal}` as (key, ordinal) pairs. */
  occurrence_row *seen = NULL;
  size_t seen_count = 0, result = 0;
  bool want_body = strcmp(part, "body") == 0;
  for (size_t s = 0; s < body->part_count; s++) {
    const qa_app_component_body_part *source = &body->parts[s];
    if (!want_body && strcmp(source->part, "body") != 0 && strcmp(source->part, part) != 0) continue;
    /* Per-source occurrence ordinals, as in TS. */
    occurrence_row *occurrences = calloc(source->pass_count ? source->pass_count : 1, sizeof *occurrences);
    if (occurrences == NULL) qa_range_error("Out of memory");
    size_t distinct = 0;
    for (size_t i = 0; i < source->pass_count; i++) {
      char *key = material_key(&source->passes[i]);
      size_t ordinal = 0, at = distinct;
      for (size_t j = 0; j < distinct; j++) if (strcmp(occurrences[j].key, key) == 0) { at = j; break; }
      if (at == distinct) {
        occurrences[distinct++] = (occurrence_row){key, 1};
      } else {
        ordinal = occurrences[at].count++;
        free(key);
        key = occurrences[at].key;
      }
      bool known = false;
      for (size_t j = 0; j < seen_count; j++) if (seen[j].count == ordinal && strcmp(seen[j].key, key) == 0) known = true;
      if (!known) {
        occurrence_row *grown = realloc(seen, (seen_count + 1) * sizeof *seen);
        if (grown == NULL) qa_range_error("Out of memory");
        seen = grown;
        size_t length = strlen(key) + 1;
        char *copy = malloc(length);
        if (copy == NULL) qa_range_error("Out of memory");
        memcpy(copy, key, length);
        seen[seen_count++] = (occurrence_row){copy, ordinal};
        if (result < capacity) out[result] = &source->passes[i];
        result++;
      }
    }
    for (size_t j = 0; j < distinct; j++) free(occurrences[j].key);
    free(occurrences);
  }
  for (size_t j = 0; j < seen_count; j++) free(seen[j].key);
  free(seen);
  return result;
}
bool qa_app_body_base_visible(const qa_app_component_body *bodies, size_t body_count, qa_actor_id actor, const char *part) {
  for (size_t i = 0; i < body_count; i++) {
    if (!qa_same_actor(bodies[i].actor, actor)) continue;
    if (bodies[i].part_count == 0) return false;
    for (size_t j = 0; j < bodies[i].part_count; j++) {
      const qa_app_component_body_part *source = &bodies[i].parts[j];
      if (strcmp(part, "body") != 0 && strcmp(source->part, "body") != 0 && strcmp(source->part, part) != 0) continue;
      if (!source->base) return false;
    }
  }
  return true;
}
