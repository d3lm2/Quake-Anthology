/* Port of src/formats/q12-model/md2.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model/md2.h"
#include "qa/formats/q12_model/animation.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/q12_model/common.h"
#include "qa/formats/q12_model/normals.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}

/* The commands read so far and the one being read, released when a raise
 * abandons the list. */
typedef struct commands_state {
  qa_md2_command *result;
  size_t count;
  qa_md2_command_vertex *pending;
} commands_state;
static void commands_release(void *arg) {
  commands_state *state = (commands_state *)arg;
  free(state->pending);
  qa_md2_commands_destroy(state->result, state->count);
}

static qa_md2_command *read_commands(qa_binary_reader *reader, double vertex_count, size_t *out_count) {
  commands_state state = {NULL, 0, NULL};
  size_t capacity = 0;
  *out_count = 0;
  if (reader->length == 0) return NULL;
  qa_cleanup guard;
  qa_cleanup_push(&guard, commands_release, &state);
  while (qa_binary_reader_remaining(reader) > 0) {
    int32_t command = qa_binary_reader_i32(reader);
    if (command == 0) {
      qa_cleanup_pop(&guard);
      *out_count = state.count;
      return state.result;
    }
    double vertices = fabs((double)command);
    if (vertices < 3) qa_q12_fail(reader, "GL strip/fan must have at least three vertices");
    (void)qa_binary_reader_data_view(reader, (int64_t)reader->cursor, (int64_t)(vertices * 12));
    qa_md2_command entry;
    entry.kind = command > 0 ? "strip" : "fan";
    entry.vertex_count = (size_t)vertices;
    entry.vertices = xmalloc(entry.vertex_count * sizeof *entry.vertices);
    state.pending = entry.vertices;
    for (size_t i = 0; i < entry.vertex_count; i++) {
      entry.vertices[i].tex_coord.x = qa_binary_reader_finite_f32(reader);
      entry.vertices[i].tex_coord.y = qa_binary_reader_finite_f32(reader);
      entry.vertices[i].vertex = qa_q12_index(reader, qa_binary_reader_i32(reader), vertex_count, "GL vertex");
    }
    if (state.count == capacity) {
      capacity = capacity ? capacity * 2 : 8;
      state.result = xrealloc(state.result, capacity * sizeof *state.result);
    }
    state.result[state.count++] = entry;
    state.pending = NULL;
  }
  qa_q12_fail(reader, "GL command list lacks terminator");
}

typedef struct decode_commands_state {
  qa_binary_reader reader;
  uint8_t *data;
} decode_commands_state;
static void decode_commands_release(void *arg) {
  decode_commands_state *state = (decode_commands_state *)arg;
  qa_binary_reader_destroy(&state->reader);
  free(state->data);
}

qa_md2_command *qa_decode_md2_commands(const qa_q2_alias_model *model, size_t *out_count) {
  if (model->frame_count == 0) qa_range_error("MD2 has no frames");
  size_t length = model->gl_command_count * 4;
  uint8_t *data = xmalloc(length);
  for (size_t i = 0; i < model->gl_command_count; i++) {
    uint32_t word = (uint32_t)model->gl_commands[i];
    data[i * 4] = (uint8_t)word;
    data[i * 4 + 1] = (uint8_t)(word >> 8);
    data[i * 4 + 2] = (uint8_t)(word >> 16);
    data[i * 4 + 3] = (uint8_t)(word >> 24);
  }
  decode_commands_state state;
  state.data = data;
  if (!qa_binary_reader_init(&state.reader, data, length, "<md2 GL commands>")) {
    free(data);
    qa_range_error("Out of memory");
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, decode_commands_release, &state);
  qa_md2_command *result = read_commands(&state.reader, (double)model->frames[0].frame.vertex_count, out_count);
  qa_cleanup_pop(&guard);
  decode_commands_release(&state);
  return result;
}

void qa_md2_commands_destroy(qa_md2_command *commands, size_t count) {
  if (commands == NULL) return;
  for (size_t i = 0; i < count; i++) free(commands[i].vertices);
  free(commands);
}

static void md2_section(qa_binary_reader *reader, double offset, double count, double stride, double end,
                        qa_binary_reader *out) {
  double size = count * stride;
  if (size != trunc(size) || size > 9007199254740991.0 || offset < 68 || offset > end - size) {
    qa_binary_error(reader->source, (int64_t)reader->cursor, "MD2 section %s+%s exceeds header/model bounds",
                    QA_JS_NUMBER(offset), QA_JS_NUMBER(size));
  }
  if (!qa_binary_reader_section(reader, (int64_t)offset, (int64_t)size, out)) qa_range_error("Out of memory");
}

/* Readers and partial results of one parse, released when a BinaryError from
 * malformed input unwinds it (a caught raise must not leak the model read so far). */
typedef struct md2_parse_state {
  qa_binary_reader reader;
  qa_binary_reader sections[5];
  int section_count;
  qa_vec3 *points;
  qa_bounds *all;
  qa_q2_alias_model *out;
} md2_parse_state;

static void md2_parse_release(void *arg) {
  md2_parse_state *state = (md2_parse_state *)arg;
  for (int i = 0; i < state->section_count; i++) qa_binary_reader_destroy(&state->sections[i]);
  qa_binary_reader_destroy(&state->reader);
  free(state->points);
  free(state->all);
  qa_q2_alias_model_destroy(state->out);
}

void qa_parse_md2(const uint8_t *data, size_t data_length, const char *source, qa_q2_alias_model *out) {
  memset(out, 0, sizeof *out);
  md2_parse_state state;
  memset(&state, 0, sizeof state);
  state.out = out;
  if (!qa_binary_reader_init(&state.reader, (uint8_t *)data, data_length, source != NULL ? source : "<md2>"))
    qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, md2_parse_release, &state);
  qa_binary_reader *reader = &state.reader;
  qa_binary_reader_expect_magic(reader, "IDP2");
  qa_q12_version(reader, 8);
  out->skin_width = qa_q12_count(reader, "skin width", 1);
  out->skin_height = qa_q12_count(reader, "skin height", 1);
  double frame_size = qa_q12_count(reader, "frame size", 1);
  double skin_count = qa_q12_count(reader, "skins", 0);
  double vertex_count = qa_q12_count(reader, "vertices", 1);
  double coordinate_count = qa_q12_count(reader, "texture coordinates", 1);
  double triangle_count = qa_q12_count(reader, "triangles", 1);
  double command_count = qa_q12_count(reader, "GL command words", 0);
  double frame_count = qa_q12_count(reader, "frames", 1);
  double skin_offset = qa_binary_reader_i32(reader);
  double coordinate_offset = qa_binary_reader_i32(reader);
  double triangle_offset = qa_binary_reader_i32(reader);
  double frame_offset = qa_binary_reader_i32(reader);
  double command_offset = qa_binary_reader_i32(reader);
  double end = qa_binary_reader_i32(reader);
  if (end < 68 || end > (double)reader->length) {
    qa_binary_error(reader->source, (int64_t)reader->cursor, "invalid model end %s", QA_JS_NUMBER(end));
  }
  if (frame_size < 40 + vertex_count * 4) qa_q12_fail(reader, "frame size is smaller than its vertices");
  qa_binary_reader *skin_reader = &state.sections[0], *coordinate_reader = &state.sections[1];
  qa_binary_reader *triangle_reader = &state.sections[2], *frame_reader = &state.sections[3];
  qa_binary_reader *command_reader = &state.sections[4];
  md2_section(reader, skin_offset, skin_count, 64, end, skin_reader);
  state.section_count = 1;
  md2_section(reader, coordinate_offset, coordinate_count, 4, end, coordinate_reader);
  state.section_count = 2;
  md2_section(reader, triangle_offset, triangle_count, 12, end, triangle_reader);
  state.section_count = 3;
  md2_section(reader, frame_offset, frame_count, frame_size, end, frame_reader);
  state.section_count = 4;
  md2_section(reader, command_offset, command_count, 4, end, command_reader);
  state.section_count = 5;
  const char **skins = xmalloc((size_t)skin_count * sizeof *skins);
  out->skins.values = skins;
  for (size_t i = 0; i < (size_t)skin_count; i++) {
    skins[i] = qa_binary_reader_fixed_byte_string(skin_reader, 64);
    out->skins.count = i + 1;
  }
  qa_vec2 *coordinates = xmalloc((size_t)coordinate_count * sizeof *coordinates);
  out->texture_coordinates = coordinates;
  for (size_t i = 0; i < (size_t)coordinate_count; i++) {
    coordinates[i].x = qa_binary_reader_i16(coordinate_reader);
    coordinates[i].y = qa_binary_reader_i16(coordinate_reader);
  }
  out->texture_coordinate_count = (size_t)coordinate_count;
  qa_q2_alias_triangle *triangles = xmalloc((size_t)triangle_count * sizeof *triangles);
  out->triangles = triangles;
  for (size_t i = 0; i < (size_t)triangle_count; i++) {
    for (int j = 0; j < 3; j++)
      triangles[i].vertices[j] = qa_q12_index(triangle_reader, qa_binary_reader_u16(triangle_reader),
                                              vertex_count, "vertex");
    for (int j = 0; j < 3; j++)
      triangles[i].tex_coords[j] = qa_q12_index(triangle_reader, qa_binary_reader_u16(triangle_reader),
                                                coordinate_count, "texture coordinate");
  }
  out->triangle_count = (size_t)triangle_count;
  qa_q2_alias_frame *frames = xmalloc((size_t)frame_count * sizeof *frames);
  out->frames = frames;
  for (size_t i = 0; i < (size_t)frame_count; i++) {
    qa_binary_reader_seek(frame_reader, (int64_t)((double)i * frame_size));
    memset(&frames[i], 0, sizeof frames[i]);
    out->frame_count = i + 1;
    frames[i].scale = qa_q12_vector(frame_reader);
    frames[i].translation = qa_q12_vector(frame_reader);
    frames[i].frame.name = qa_binary_reader_fixed_byte_string(frame_reader, 16);
    size_t count = (size_t)vertex_count;
    qa_model_vertex *vertices = xmalloc(count * sizeof *vertices);
    frames[i].frame.vertices = vertices;
    qa_packed_alias_vertex *compressed = xmalloc(count * sizeof *compressed);
    frames[i].compressed_vertices = compressed;
    qa_vec3 *points = xmalloc(count * sizeof *points);
    state.points = points;
    for (size_t v = 0; v < count; v++) {
      qa_packed_alias_vertex packed = qa_q12_packed_vertex(frame_reader);
      qa_q12_index(frame_reader, packed.normal_index, 162, "normal");
      compressed[v] = packed;
      vertices[v].position = qa_q12_packed_position(packed, frames[i].scale, frames[i].translation);
      vertices[v].normal = qa_decode_alias_normal(packed.normal_index);
      points[v] = vertices[v].position;
    }
    frames[i].frame.vertex_count = count;
    frames[i].compressed_vertex_count = count;
    frames[i].frame.bounds = qa_q12_bounds_from_points(points, count);
    state.points = NULL;
    free(points);
  }
  size_t validated = 0;
  qa_md2_command *validated_commands = read_commands(command_reader, vertex_count, &validated);
  qa_md2_commands_destroy(validated_commands, validated);
  qa_binary_reader_seek(command_reader, 0);
  int32_t *commands = xmalloc((size_t)command_count * sizeof *commands);
  out->gl_commands = commands;
  for (size_t i = 0; i < (size_t)command_count; i++) commands[i] = qa_binary_reader_i32(command_reader);
  out->gl_command_count = (size_t)command_count;
  qa_bounds *all = xmalloc((size_t)frame_count * sizeof *all);
  state.all = all;
  for (size_t i = 0; i < (size_t)frame_count; i++) all[i] = frames[i].frame.bounds;
  out->bounds = qa_q12_union_bounds(all, (size_t)frame_count);
  state.all = NULL;
  free(all);
  qa_cleanup_pop(&guard);
  for (int i = 0; i < state.section_count; i++) qa_binary_reader_destroy(&state.sections[i]);
  qa_binary_reader_destroy(&state.reader);
}

void qa_q2_alias_model_destroy(qa_q2_alias_model *model) {
  if (model == NULL) return;
  for (size_t i = 0; i < model->skins.count; i++) free((void *)model->skins.values[i]);
  free((void *)model->skins.values);
  free((void *)model->texture_coordinates);
  free((void *)model->triangles);
  for (size_t i = 0; i < model->frame_count; i++) {
    /* The interpolation PoseLru keys by frame address (TS WeakMap identity): forget it before the address is freed. */
    qa_alias_interpolation_forget(&model->frames[i].frame);
    free((void *)model->frames[i].frame.name);
    free((void *)model->frames[i].frame.vertices);
    free((void *)model->frames[i].compressed_vertices);
  }
  free((void *)model->frames);
  free((void *)model->gl_commands);
  memset(model, 0, sizeof *model);
}
