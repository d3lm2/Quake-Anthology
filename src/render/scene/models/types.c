/* Port of src/render/scene/models/types.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/types.h"

#include <stdlib.h>
#include <string.h>

#include "qa/materials/evaluate.h"

void qa_scene_model_group_destroy(qa_scene_model_group *group) {
  qa_mat_draw_batches_destroy(group->batches, group->batch_count);
  free(group->shadow_positions);
  free(group->shadow_indices);
  for (size_t i = 0; i < group->owned_storage_count; i++) free(group->owned_storage[i]);
  free(group->owned_storage);
  memset(group, 0, sizeof *group);
}
qa_scene_group qa_scene_model_shadow_volume_group(const qa_scene_model_group *group) {
  qa_render_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = QA_RENDER_OP_SHADOW_VOLUME;
  operation.positions = group->shadow_positions;
  operation.position_count = group->shadow_position_count;
  operation.indices.values = group->shadow_indices;
  operation.indices.count = group->shadow_index_count;
  operation.mirror = group->shadow_mirror;
  operation.white_image = group->shadow_white_image;
  if (group->kind != NULL && strcmp(group->kind, "source") == 0) {
    qa_source_surface_order order = {group->source_view, group->source_entity,
                                     group->surface, group->fog, group->dlight};
    return qa_shadow_volume_group(group->source_material, &order, operation);
  }
  return qa_shadow_volume_group(group->source_material, NULL, operation);
}
void qa_scene_model_groups_destroy(qa_scene_model_group *groups, size_t count) {
  for (size_t i = 0; i < count; i++) qa_scene_model_group_destroy(&groups[i]);
  free(groups);
}

static qa_model_image_scratch fallback_scratch;

qa_render_image qa_model_image(const qa_model_image_selection *selection,
                               qa_palette palette, const qa_byte_view *translation,
                               qa_model_image_scratch *scratch) {
  if (scratch == NULL) scratch = &fallback_scratch;
  scratch->level = (qa_image_level){selection->width, selection->height,
                                    selection->pixels};
  qa_render_image image = {0};
  image.kind = QA_RENDER_IMAGE_INDEXED8;
  image.levels = &scratch->level;
  image.level_count = 1;
  image.palette = palette;
  image.transparency.kind =
      selection->has_transparent_index ? "index" : "opaque";
  image.transparency.index =
      selection->has_transparent_index ? selection->transparent_index : 0;
  if (selection->fullbright) {
    scratch->fullbright = (qa_fullbright_range){
        224, (selection->has_transparent_index &&
              selection->transparent_index == 255)
                 ? 254
                 : 255};
    image.fullbright = &scratch->fullbright;
  }
  if (translation != NULL) {
    scratch->translation = *translation;
    image.translation = &scratch->translation;
  }
  return image;
}

qa_vec4 qa_byte_color(qa_vec4 color) {
  return (qa_vec4){color.x * 255, color.y * 255, color.z * 255, color.w * 255};
}
