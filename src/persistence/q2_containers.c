/* Port of src/persistence/q2-containers.ts: engine-written metadata files of the two Quake II save layouts.
 * Classic: id Quake II 3.20 sv_ccmds.c (SV_WriteServerFile, SV_WriteLevelFile) and qcommon/cmodel.c
 * (CM_WritePortalState). Rerelease: the Nightdive engine; its server.ssv starts with "EVAS" and its level files
 * carry no magic. GPL-2.0-or-later. */
#include "qa/persistence/q2_containers.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"
#include "qa/persistence/source_bytes.h"
#include "qa/persistence/value.h"

#define MAX_FIELD 65536u
#define LEVEL_END 0xffffffffu

static bool push_cvar(qa_saved_cvar **cvars, size_t *count, char *name, char *value) {
  qa_saved_cvar *grown =
      (qa_saved_cvar *)realloc(*cvars, (*count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  *cvars = grown;
  (*cvars)[*count].name = name;
  (*cvars)[*count].value = value;
  (*count)++;
  return true;
}
static void free_cvars(qa_saved_cvar *cvars, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(cvars[i].name);
    free(cvars[i].value);
  }
  free(cvars);
}
void qa_q2_classic_server_save_free(qa_q2_classic_server_save *save) {
  free(save->comment);
  free(save->map_command);
  free_cvars(save->cvars, save->cvar_count);
  memset(save, 0, sizeof *save);
}
void qa_q2_rerelease_server_save_free(qa_q2_rerelease_server_save *save) {
  free(save->comment);
  free(save->map_command);
  free_cvars(save->cvars, save->cvar_count);
  memset(save, 0, sizeof *save);
}
void qa_q2_level_metadata_free(qa_q2_level_metadata *save) {
  for (size_t i = 0; i < save->configstring_count; i++)
    free(save->configstrings[i].value);
  free(save->configstrings);
  free(save->portal_bytes);
  memset(save, 0, sizeof *save);
}

static void reader_cleanup(void *arg) { qa_binary_reader_destroy((qa_binary_reader *)arg); }
static void classic_server_cleanup(void *arg) { qa_q2_classic_server_save_free((qa_q2_classic_server_save *)arg); }
bool qa_decode_q2_classic_server(const uint8_t *bytes, size_t length,
                                 qa_q2_classic_server_save *out) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "q2/server.ssv"))
    return false;
  memset(out, 0, sizeof *out);
  /* A truncated file raises BinaryError: the reader and the partial record are released first. */
  qa_cleanup reader_node, out_node;
  qa_cleanup_push(&reader_node, reader_cleanup, &reader);
  qa_cleanup_push(&out_node, classic_server_cleanup, out);
  out->comment = qa_read_fixed(&reader, 32);
  out->map_command = qa_read_fixed(&reader, 128);
  if (out->comment == NULL || out->map_command == NULL) goto oom;
  while (qa_binary_reader_remaining(&reader) > 0) {
    char *name = qa_read_fixed(&reader, 128);
    qa_cleanup name_node;
    qa_cleanup_push(&name_node, free, name);
    char *value = qa_read_fixed(&reader, 128);
    qa_cleanup_pop(&name_node);
    if (name == NULL || value == NULL || !push_cvar(&out->cvars, &out->cvar_count, name, value)) {
      free(name);
      free(value);
      goto oom;
    }
  }
  qa_cleanup_pop(&out_node);
  qa_cleanup_pop(&reader_node);
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_cleanup_pop(&out_node);
  qa_cleanup_pop(&reader_node);
  qa_binary_reader_destroy(&reader);
  qa_q2_classic_server_save_free(out);
  return false;
}
uint8_t *qa_encode_q2_classic_server(const qa_q2_classic_server_save *save,
                                     size_t *out_length) {
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)(160 + save->cvar_count * 256)))
    return NULL;
  qa_write_fixed(&writer, save->comment, strlen(save->comment), 32);
  qa_write_fixed(&writer, save->map_command, strlen(save->map_command), 128);
  for (size_t i = 0; i < save->cvar_count; i++) {
    qa_write_fixed(&writer, save->cvars[i].name, strlen(save->cvars[i].name), 128);
    qa_write_fixed(&writer, save->cvars[i].value, strlen(save->cvars[i].value), 128);
  }
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
static bool push_configstring(qa_q2_configstring **list, size_t *count, int64_t index, char *value) {
  qa_q2_configstring *grown = (qa_q2_configstring *)realloc(*list, (*count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  *list = grown;
  (*list)[*count].index = index;
  (*list)[*count].value = value;
  (*count)++;
  return true;
}
static void free_configstrings(qa_q2_configstring *list, size_t count) {
  for (size_t i = 0; i < count; i++) free(list[i].value);
  free(list);
}

/* The engine keeps configstrings as one flat array of fixed slots. A string longer than its slot (the status bar)
 * continues through the following slots, so the array, not the individual slot, is what a save file holds. */
uint8_t *qa_pack_configstrings(const qa_q2_configstring *entries, size_t entry_count, int64_t count, int64_t width,
                               size_t *out_length) {
  size_t memory_length = (size_t)(count * width);
  uint8_t *memory = (uint8_t *)calloc(memory_length == 0 ? 1 : memory_length, 1);
  size_t *order = (size_t *)malloc((entry_count == 0 ? 1 : entry_count) * sizeof *order);
  if (memory == NULL || order == NULL) {
    free(memory);
    free(order);
    return NULL;
  }
  /* [...entries].sort((left, right) => left.index - right.index): a stable sort by index. */
  for (size_t i = 0; i < entry_count; i++) {
    size_t at = i;
    for (; at > 0 && entries[order[at - 1]].index > entries[i].index; at--) order[at] = order[at - 1];
    order[at] = i;
  }
  for (size_t i = 0; i < entry_count; i++) {
    const qa_q2_configstring *entry = &entries[order[i]];
    if (entry->index < 0 || entry->index >= count) {
      free(memory);
      free(order);
      qa_save_format_error("q2.configstring", "configstring index outside the source range");
    }
    /* A C string carries no interior NUL, so the TS interior-NUL refusal cannot arise. */
    size_t length = strlen(entry->value);
    size_t start = (size_t)(entry->index * width);
    if (start + length >= memory_length) {
      free(memory);
      free(order);
      qa_save_format_error("q2.configstring", "configstring runs past the configstring array");
    }
    size_t cleared = length + 1 > (size_t)width ? length + 1 : (size_t)width;
    size_t end = start + cleared < memory_length ? start + cleared : memory_length;
    memset(memory + start, 0, end - start);
    memcpy(memory + start, entry->value, length);
  }
  free(order);
  *out_length = memory_length;
  return memory;
}
/* Inverse of qa_pack_configstrings: a slot begins a string unless an earlier string's text still covers it. */
bool qa_unpack_configstrings(const uint8_t *memory, size_t memory_length, int64_t count, int64_t width,
                             qa_q2_configstring **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  size_t covered = 0;
  for (int64_t index = 0; index < count; index++) {
    size_t start = (size_t)(index * width);
    /* memory[start] past the end reads undefined in TS, which is not 0: the loop below then adds an empty string. */
    if ((start < memory_length && memory[start] == 0) || start < covered) continue;
    size_t end = start;
    while (end < memory_length && memory[end] != 0) end++;
    char *value = qa_byte_text(start < memory_length ? memory + start : NULL, end - start);
    if (value == NULL || !push_configstring(out, out_count, index, value)) {
      free(value);
      free_configstrings(*out, *out_count);
      *out = NULL;
      *out_count = 0;
      return false;
    }
    covered = end + 1;
  }
  return true;
}

/* The `portalopen` array as the original writes it: 1024 little endian ints. */
uint8_t *qa_q2_classic_portal_bytes(const qa_q2_portal *portals, size_t portal_count, size_t *out_length) {
  uint8_t *bytes = (uint8_t *)calloc(QA_Q2_CLASSIC_AREA_PORTALS * 4, 1);
  if (bytes == NULL) return NULL;
  for (size_t i = 0; i < portal_count; i++) {
    double portal = portals[i].portal;
    if (!(portal >= 0 && portal < QA_Q2_CLASSIC_AREA_PORTALS) || portal != (double)(int64_t)portal) {
      free(bytes);
      qa_save_format_error("q2.portals", "area portal outside MAX_MAP_AREAPORTALS");
    }
    qa_store_u32_le(bytes, QA_Q2_CLASSIC_AREA_PORTALS * 4, (size_t)portal * 4, portals[i].open ? 1 : 0);
  }
  *out_length = QA_Q2_CLASSIC_AREA_PORTALS * 4;
  return bytes;
}
static bool push_portal(qa_q2_portal **list, size_t *count, double portal) {
  qa_q2_portal *grown = (qa_q2_portal *)realloc(*list, (*count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  *list = grown;
  (*list)[*count].portal = portal;
  (*list)[*count].open = true;
  (*count)++;
  return true;
}
/* Open portals of a classic `portalopen` array; a closed portal is the default and is not listed. */
bool qa_q2_classic_portals_from(const uint8_t *bytes, size_t length, qa_q2_portal **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (length != QA_Q2_CLASSIC_AREA_PORTALS * 4)
    qa_save_format_error("q2.portals", "classic portal state must hold MAX_MAP_AREAPORTALS ints");
  for (size_t portal = 0; portal < QA_Q2_CLASSIC_AREA_PORTALS; portal++) {
    if (qa_load_u32_le(bytes, length, portal * 4) == 0) continue;
    if (!push_portal(out, out_count, (double)portal)) {
      free(*out);
      *out = NULL;
      *out_count = 0;
      return false;
    }
  }
  return true;
}
/* One byte per portal of the map; the count must be the map's own, which the engine compares on load. */
uint8_t *qa_q2_rerelease_portal_bytes(const qa_q2_portal *portals, size_t portal_count, double count,
                                      size_t *out_length) {
  if (!(count >= 0 && count <= 65535) || count != (double)(int64_t)count)
    qa_save_format_error("q2.portals", "invalid area portal count");
  uint8_t *bytes = (uint8_t *)calloc(count == 0 ? 1 : (size_t)count, 1);
  if (bytes == NULL) return NULL;
  for (size_t i = 0; i < portal_count; i++) {
    double portal = portals[i].portal;
    if (!(portal >= 0 && portal < count) || portal != (double)(int64_t)portal) {
      free(bytes);
      qa_save_format_error("q2.portals", "area portal outside the map's portal count");
    }
    bytes[(size_t)portal] = portals[i].open ? 1 : 0;
  }
  *out_length = (size_t)count;
  return bytes;
}
bool qa_q2_rerelease_portals_from(const uint8_t *bytes, size_t length, qa_q2_portal **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  for (size_t portal = 0; portal < length; portal++) {
    if (bytes[portal] == 0) continue;
    if (!push_portal(out, out_count, (double)portal)) {
      free(*out);
      *out = NULL;
      *out_count = 0;
      return false;
    }
  }
  return true;
}

bool qa_decode_q2_classic_level_metadata(const uint8_t *bytes, size_t length, qa_q2_level_metadata *out) {
  memset(out, 0, sizeof *out);
  const size_t size = (size_t)QA_Q2_CLASSIC_CONFIGSTRINGS * QA_Q2_CLASSIC_CONFIGSTRING_WIDTH;
  if (length < size) qa_save_format_error("q2.sv2", "classic level metadata is shorter than the configstring array");
  if (!qa_unpack_configstrings(bytes, size, QA_Q2_CLASSIC_CONFIGSTRINGS, QA_Q2_CLASSIC_CONFIGSTRING_WIDTH,
                               &out->configstrings, &out->configstring_count))
    return false;
  out->portal_length = length - size;
  out->portal_bytes = (uint8_t *)malloc(out->portal_length == 0 ? 1 : out->portal_length);
  if (out->portal_bytes == NULL) {
    qa_q2_level_metadata_free(out);
    return false;
  }
  memcpy(out->portal_bytes, bytes + size, out->portal_length);
  return true;
}
uint8_t *qa_encode_q2_classic_level_metadata(const qa_q2_level_metadata *save, size_t *out_length) {
  size_t memory_length = 0;
  uint8_t *memory = qa_pack_configstrings(save->configstrings, save->configstring_count, QA_Q2_CLASSIC_CONFIGSTRINGS,
                                          QA_Q2_CLASSIC_CONFIGSTRING_WIDTH, &memory_length);
  if (memory == NULL) return NULL;
  uint8_t *bytes = (uint8_t *)realloc(memory, memory_length + save->portal_length + 1);
  if (bytes == NULL) {
    free(memory);
    return NULL;
  }
  if (save->portal_length > 0) memcpy(bytes + memory_length, save->portal_bytes, save->portal_length);
  *out_length = memory_length + save->portal_length;
  return bytes;
}

/* TS readLengthString: NULL only on OOM. */
static char *read_length_string(qa_binary_reader *reader) {
  uint32_t length = qa_binary_reader_u32(reader);
  if (length > MAX_FIELD) qa_save_format_error("q2-rerelease", "save string is implausibly long");
  uint8_t *bytes = qa_binary_reader_bytes(reader, (int64_t)length);
  if (length > 0 && bytes == NULL) return NULL;
  char *text = qa_byte_text(bytes, length);
  free(bytes);
  return text;
}
static void write_length_string(qa_binary_writer *writer, const char *value) {
  size_t length = strlen(value);
  qa_binary_writer_u32(writer, (int64_t)length);
  qa_binary_writer_bytes(writer, (const uint8_t *)value, length);
}
typedef struct server_scope {
  const uint8_t *bytes;
  size_t length;
  qa_q2_rerelease_server_save *out;
  qa_binary_reader reader;
  bool reader_open;
  bool oom;
} server_scope;
static void decode_server_body(void *raw) {
  server_scope *scope = (server_scope *)raw;
  qa_q2_rerelease_server_save *out = scope->out;
  qa_binary_reader *reader = &scope->reader;
  char *signature = qa_read_fixed(reader, 4);
  if (signature == NULL) { scope->oom = true; return; }
  bool evas = strcmp(signature, "EVAS") == 0;
  free(signature);
  if (!evas) qa_save_format_error("q2-rerelease", "missing EVAS save signature");
  int kind = qa_binary_reader_u8(reader);
  if (kind != 0 && kind != 1 && kind != 2) qa_save_format_error("q2-rerelease", "unknown save type");
  out->kind = kind;
  out->comment = read_length_string(reader);
  if (out->comment == NULL) { scope->oom = true; return; }
  uint32_t low = qa_binary_reader_u32(reader);
  uint32_t high = qa_binary_reader_u32(reader);
  out->map_command = read_length_string(reader);
  if (out->map_command == NULL) { scope->oom = true; return; }
  /* The latched cvar pairs run to the end of the file; the engine writes no terminator. */
  while (qa_binary_reader_remaining(reader) > 0) {
    char *name = read_length_string(reader);
    if (name == NULL) { scope->oom = true; return; }
    if (name[0] == '\0') {
      free(name);
      break;
    }
    qa_cleanup name_node;
    qa_cleanup_push(&name_node, free, name);
    char *value = read_length_string(reader);
    qa_cleanup_pop(&name_node);
    if (value == NULL || !push_cvar(&out->cvars, &out->cvar_count, name, value)) {
      free(name);
      free(value);
      scope->oom = true;
      return;
    }
  }
  if (qa_binary_reader_remaining(reader) != 0) qa_save_format_error("q2-rerelease", "trailing server metadata");
  out->timestamp = ((uint64_t)high << 32) | (uint64_t)low;
}
bool qa_decode_q2_rerelease_server(const uint8_t *bytes, size_t length, qa_q2_rerelease_server_save *out) {
  server_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.bytes = bytes;
  scope.length = length;
  scope.out = out;
  memset(out, 0, sizeof *out);
  if (!qa_binary_reader_init(&scope.reader, (uint8_t *)bytes, length, "q2-rerelease/server.ssv")) return false;
  /* A malformed file raises; the partial record and the reader are released first. */
  qa_caught_error caught;
  bool completed = qa_try(decode_server_body, &scope, &caught);
  qa_binary_reader_destroy(&scope.reader);
  if (!completed) {
    qa_q2_rerelease_server_save_free(out);
    qa_rethrow(&caught);
  }
  if (scope.oom) {
    qa_q2_rerelease_server_save_free(out);
    return false;
  }
  return true;
}
uint8_t *qa_encode_q2_rerelease_server(const qa_q2_rerelease_server_save *save, size_t *out_length) {
  size_t size = 13 + 8 + strlen(save->comment) + strlen(save->map_command);
  for (size_t i = 0; i < save->cvar_count; i++) size += 8 + strlen(save->cvars[i].name) + strlen(save->cvars[i].value);
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)size)) return NULL;
  qa_binary_writer_bytes(&writer, (const uint8_t *)"EVAS", 4);
  qa_binary_writer_u8(&writer, save->kind);
  write_length_string(&writer, save->comment);
  qa_binary_writer_u32(&writer, (int64_t)(uint32_t)save->timestamp);
  qa_binary_writer_u32(&writer, (int64_t)(uint32_t)(save->timestamp >> 32));
  write_length_string(&writer, save->map_command);
  for (size_t i = 0; i < save->cvar_count; i++) {
    if (save->cvars[i].name[0] == '\0') {
      qa_binary_writer_destroy(&writer);
      qa_save_format_error("q2-rerelease", "a cvar needs a name");
    }
    write_length_string(&writer, save->cvars[i].name);
    write_length_string(&writer, save->cvars[i].value);
  }
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}

typedef struct level_scope {
  qa_binary_reader reader;
  uint8_t *memory;
  qa_q2_level_metadata *out;
  bool oom;
} level_scope;
static void decode_level_body(void *raw) {
  level_scope *scope = (level_scope *)raw;
  qa_binary_reader *reader = &scope->reader;
  const size_t width = QA_Q2_RERELEASE_CONFIGSTRING_WIDTH;
  for (;;) {
    uint32_t index = qa_binary_reader_u32(reader);
    if (index == LEVEL_END) break;
    if (index >= QA_Q2_RERELEASE_CONFIGSTRINGS) qa_save_format_error("q2-rerelease", "configstring outside source range");
    uint8_t length = qa_binary_reader_u8(reader);
    if (length > width) qa_save_format_error("q2-rerelease", "configstring longer than its slot");
    uint8_t *text = qa_binary_reader_bytes(reader, length);
    if (length > 0 && text == NULL) { scope->oom = true; return; }
    if (length > 0) memcpy(scope->memory + (size_t)index * width, text, length);
    free(text);
  }
  scope->out->portal_length = qa_binary_reader_u16(reader);
  scope->out->portal_bytes = qa_binary_reader_bytes(reader, (int64_t)scope->out->portal_length);
  if (scope->out->portal_length > 0 && scope->out->portal_bytes == NULL) { scope->oom = true; return; }
  if (qa_binary_reader_remaining(reader) != 0) qa_save_format_error("q2-rerelease", "trailing level metadata");
}
/* `index u32, length u8, text` per non-empty slot, an index of -1, then the portal count (u16) and one byte per portal. */
bool qa_decode_q2_rerelease_level_metadata(const uint8_t *bytes, size_t length, qa_q2_level_metadata *out) {
  memset(out, 0, sizeof *out);
  const size_t memory_length = (size_t)QA_Q2_RERELEASE_CONFIGSTRINGS * QA_Q2_RERELEASE_CONFIGSTRING_WIDTH;
  level_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.out = out;
  scope.memory = (uint8_t *)calloc(memory_length, 1);
  if (scope.memory == NULL) return false;
  if (!qa_binary_reader_init(&scope.reader, (uint8_t *)bytes, length, "q2-rerelease/level.sv2")) {
    free(scope.memory);
    return false;
  }
  qa_caught_error caught;
  bool completed = qa_try(decode_level_body, &scope, &caught);
  qa_binary_reader_destroy(&scope.reader);
  if (!completed) {
    free(scope.memory);
    qa_q2_level_metadata_free(out);
    qa_rethrow(&caught);
  }
  bool ok = !scope.oom && qa_unpack_configstrings(scope.memory, memory_length, QA_Q2_RERELEASE_CONFIGSTRINGS,
                                                  QA_Q2_RERELEASE_CONFIGSTRING_WIDTH, &out->configstrings,
                                                  &out->configstring_count);
  free(scope.memory);
  if (!ok) qa_q2_level_metadata_free(out);
  return ok;
}
uint8_t *qa_encode_q2_rerelease_level_metadata(const qa_q2_level_metadata *save, size_t *out_length) {
  const size_t width = QA_Q2_RERELEASE_CONFIGSTRING_WIDTH;
  size_t memory_length = 0;
  uint8_t *memory = qa_pack_configstrings(save->configstrings, save->configstring_count, QA_Q2_RERELEASE_CONFIGSTRINGS,
                                          QA_Q2_RERELEASE_CONFIGSTRING_WIDTH, &memory_length);
  if (memory == NULL) return NULL;
  if (save->portal_length > 0xffff) {
    free(memory);
    qa_save_format_error("q2-rerelease", "too many area portals");
  }
  size_t size = 6 + save->portal_length;
  for (size_t index = 0; index < QA_Q2_RERELEASE_CONFIGSTRINGS; index++) {
    size_t start = index * width;
    if (memory[start] == 0) continue;
    size_t end = start;
    while (end < memory_length && memory[end] != 0) end++;
    size += 5 + (end - start < width ? end - start : width);
  }
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)size)) {
    free(memory);
    return NULL;
  }
  for (size_t index = 0; index < QA_Q2_RERELEASE_CONFIGSTRINGS; index++) {
    size_t start = index * width;
    if (memory[start] == 0) continue;
    size_t end = start;
    while (end < memory_length && memory[end] != 0) end++;
    size_t slot_length = end - start < width ? end - start : width;
    qa_binary_writer_u32(&writer, (int64_t)index);
    qa_binary_writer_u8(&writer, (int64_t)slot_length);
    qa_binary_writer_bytes(&writer, memory + start, slot_length);
  }
  free(memory);
  qa_binary_writer_u32(&writer, (int64_t)LEVEL_END);
  qa_binary_writer_u16(&writer, (int64_t)save->portal_length);
  qa_binary_writer_bytes(&writer, save->portal_bytes, save->portal_length);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
