/* Port of src/materials/fog.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/materials/fog.h"
#include "qa/foundation/js.h"

static float fr(double v) { return (float)v; }
static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}

/* TS at(items, index): items[index], RangeError when it is undefined (a
 * fractional, negative, NaN or too large index). */
static size_t checked_at(double index, size_t length) {
  if (!(index >= 0 && index == floor(index) && index < (double)length))
    qa_range_error("fog index %s outside %zu", QA_JS_NUMBER(index), length);
  return (size_t)index;
}
/* TS plane(side): at(planes, at(brushSides, firstSide + side).plane). */
static qa_plane brush_plane(const qa_fog_brush_map *map, qa_fog_brush_entry brush, double side) {
  size_t brush_side = checked_at(brush.first_side + side, map->brush_side_count);
  return map->planes[checked_at(map->brush_sides[brush_side].plane, map->plane_count)];
}

qa_fog_volume qa_prepare_fog_volume(qa_fog_brush_map map, double index,
                                    qa_q3_material_fog parameters,
                                    double identity_light) {
  qa_fog_brush_map_entry fog = map.fogs[checked_at(index, map.fog_count)];
  qa_fog_brush_entry brush = map.brushes[checked_at(fog.brush, map.brush_count)];
  if (brush.side_count < 6)
    qa_raise("Error", 0, 0, "fog brush %s has fewer than six axial sides",
                   QA_JS_NUMBER(fog.brush));
  qa_fog_volume out;
  memset(&out, 0, sizeof out);
  /* The TS object literal reads min x, y, z then max x, y, z: sides 0, 2, 4,
     1, 3, 5, each lazily, so the first missing side names its own index. */
  double min_x = -brush_plane(&map, brush, 0).distance;
  double min_y = -brush_plane(&map, brush, 2).distance;
  double min_z = -brush_plane(&map, brush, 4).distance;
  double max_x = brush_plane(&map, brush, 1).distance;
  double max_y = brush_plane(&map, brush, 3).distance;
  double max_z = brush_plane(&map, brush, 5).distance;
  out.bounds.min = (qa_vec3){min_x, min_y, min_z};
  out.bounds.max = (qa_vec3){max_x, max_y, max_z};
  if (!(fog.visible_side < 0)) {
    qa_plane outside = brush_plane(&map, brush, fog.visible_side);
    out.has_surface = true;
    out.surface.normal = qa_sub3((qa_vec3){0, 0, 0}, outside.normal);
    out.surface.distance = -outside.distance;
  }
  out.color = (qa_vec4){
      (qa_mat_to_int32(fr(fr(parameters.color.x * identity_light) * 255)) & 255) / 255.0,
      (qa_mat_to_int32(fr(fr(parameters.color.y * identity_light) * 255)) & 255) / 255.0,
      (qa_mat_to_int32(fr(fr(parameters.color.z * identity_light) * 255)) & 255) / 255.0, 1};
  /* Math.max(1, depthForOpaque): a NaN depth stays NaN. */
  out.tc_scale = fr(1.0 / fr(qa_js_max(1, parameters.depth_for_opaque) * 8));
  return out;
}

qa_fog_coordinates_state qa_fog_coordinates(qa_fog_volume fog, qa_vec3 origin,
                                            qa_vec3 forward) {
  qa_fog_coordinates_state state;
  state.distance_vector = qa_scale3(forward, fog.tc_scale);
  qa_vec3 neg_origin = qa_sub3((qa_vec3){0, 0, 0}, origin);
  state.offset = fr(fr(qa_dot3(neg_origin, forward) * fog.tc_scale) + 1.0 / 512);
  state.has_surface = fog.has_surface;
  state.surface = fog.surface;
  state.eye_depth = !fog.has_surface ? 1
      : fr(qa_dot3(origin, fog.surface.normal) - fog.surface.distance);
  return state;
}

qa_vec2 qa_fog_coordinates_at(qa_fog_coordinates_state state,
                              qa_vec3 position) {
  double x = fr(qa_dot3(position, state.distance_vector) + state.offset);
  if (!state.has_surface) {
    qa_vec2 out = {x, 31.0 / 32};
    return out;
  }
  double depth = fr(qa_dot3(position, state.surface.normal) -
                    state.surface.distance);
  double y;
  if (state.eye_depth < 0)
    y = depth < 1 ? 1.0 / 32
        : fr(1.0 / 32 + fr(fr(30.0 / 32 * depth) / fr(depth - state.eye_depth)));
  else
    y = depth < 0 ? 1.0 / 32 : 31.0 / 32;
  qa_vec2 out = {x, y};
  return out;
}

double qa_fog_factor(double s, double t) {
  double distance = fr(s - 1.0 / 512);
  if (distance < 0 || t < 1.0 / 32) return 0;
  if (t < 31.0 / 32)
    distance = fr(distance * fr(fr(t - 1.0 / 32) / (30.0 / 32)));
  distance = fr(distance * 8);
  if (distance > 1) distance = 1;
  double index = trunc(fr(distance * 255)); /* NaN s reads FOG_TABLE[NaN]: undefined */
  if (!(index >= 0 && index < 256))
    qa_range_error("fog index %s outside 256", QA_JS_NUMBER(index));
  return fr(sqrt(fr(index / 255.0)));
}

qa_image_level qa_create_fog_texture(uint8_t *pixels, size_t length) {
  if (pixels == NULL) {
    length = 256 * 32 * 4;
    pixels = malloc(length);
    if (pixels == NULL) qa_range_error("fog allocation failed");
  }
  if (length != 256 * 32 * 4)
    qa_range_error("Fog texture requires 256x32 RGBA bytes");
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 256; x++) {
      double density = qa_fog_factor((x + 0.5) / 256, (y + 0.5) / 32);
      uint8_t *p = pixels + (y * 256 + x) * 4;
      p[0] = 255; p[1] = 255; p[2] = 255;
      p[3] = (uint8_t)qa_mat_to_int32(fr(255 * density));
    }
  }
  qa_image_level out = {256, 32, {pixels, length}};
  return out;
}

qa_vec4 qa_attenuate_fog_color(qa_vec4 color, const char *adjustment,
                               qa_vec2 coordinates) {
  if (eq(adjustment, "none")) return color;
  double attenuation = fr(1 - qa_fog_factor(coordinates.x, coordinates.y));
  bool rgb = eq(adjustment, "rgb") || eq(adjustment, "rgba");
  bool alpha = eq(adjustment, "alpha") || eq(adjustment, "rgba");
  qa_vec4 out = color;
  if (rgb) {
    out.x = trunc(fr(qa_js_round(color.x * 255) * attenuation)) / 255;
    out.y = trunc(fr(qa_js_round(color.y * 255) * attenuation)) / 255;
    out.z = trunc(fr(qa_js_round(color.z * 255) * attenuation)) / 255;
  }
  if (alpha) out.w = trunc(fr(qa_js_round(color.w * 255) * attenuation)) / 255;
  return out;
}

qa_render_state qa_fog_pass_state(const char *pass, const char *cull) {
  qa_render_state out = {{"src-alpha", "one-minus-src-alpha"}, pass, false,
                         "none", cull, {0, 1}, NULL};
  return out;
}
