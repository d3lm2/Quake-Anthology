/* Port of src/persistence/kex-q2-save.ts. GPL-2.0-or-later. */
#include "qa/persistence/kex_q2_save.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/persistence/source_bytes.h"

void qa_kex_q2_save_info_free(qa_kex_q2_save_info *info) {
  free(info->comment);
  free(info->map_command);
  free(info->game_directory);
  for (size_t i = 0; i < info->cvar_count; i++) {
    free(info->cvars[i].name);
    free(info->cvars[i].value);
  }
  free(info->cvars);
  memset(info, 0, sizeof *info);
}
void qa_kex_q2_save_archive_free(qa_kex_q2_save_archive *archive) {
  qa_kex_q2_save_info_free(&archive->info);
  qa_q2_rerelease_server_save_free(&archive->server);
  for (size_t i = 0; i < archive->file_count; i++) free(archive->files[i]);
  free(archive->files);
  for (size_t i = 0; i < archive->entry_count; i++) {
    free(archive->entries[i].name);
    free(archive->entries[i].bytes);
  }
  free(archive->entries);
  qa_save_value_free(archive->game);
  memset(archive, 0, sizeof *archive);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

/* TS serverInfo: false only on OOM. */
static bool server_info(const qa_q2_rerelease_server_save *server, qa_kex_q2_save_info *out) {
  memset(out, 0, sizeof *out);
  out->comment = dup_text(server->comment);
  out->map_command = dup_text(server->map_command);
  out->timestamp = server->timestamp;
  out->kind = server->kind;
  if (out->comment == NULL || out->map_command == NULL) goto oom;
  for (size_t index = 0; index < server->cvar_count; index++) {
    char *value = dup_text(server->cvars[index].value);
    if (value == NULL) goto oom;
    /* TS Map: a repeated name keeps its place and takes the later value. */
    bool replaced = false;
    for (size_t i = 0; i < out->cvar_count && !replaced; i++) {
      if (strcmp(out->cvars[i].name, server->cvars[index].name) != 0) continue;
      free(out->cvars[i].value);
      out->cvars[i].value = value;
      replaced = true;
    }
    if (replaced) continue;
    char *name = dup_text(server->cvars[index].name);
    qa_kex_cvar *grown = name == NULL ? NULL : (qa_kex_cvar *)realloc(out->cvars, (out->cvar_count + 1) * sizeof *grown);
    if (grown == NULL) {
      free(name);
      free(value);
      goto oom;
    }
    out->cvars = grown;
    out->cvars[out->cvar_count].name = name;
    out->cvars[out->cvar_count].value = value;
    out->cvar_count++;
  }
  for (size_t i = 0; i < out->cvar_count; i++) {
    if (strcmp(out->cvars[i].name, "gamename") != 0) continue;
    out->game_directory = dup_text(out->cvars[i].value);
    if (out->game_directory == NULL) goto oom;
  }
  return true;
oom:
  qa_kex_q2_save_info_free(out);
  return false;
}

/* KEX Quake II `server.ssv`: an `EVAS` header, a kind byte, a length-prefixed comment, a Unix timestamp, the map
 * command and the latched cvar pairs terminated by an empty name. */
bool qa_decode_kex_q2_server_save(const uint8_t *bytes, size_t length,
                                  qa_kex_q2_save_info *out) {
  memset(out, 0, sizeof *out);
  qa_q2_rerelease_server_save server;
  if (!qa_decode_q2_rerelease_server(bytes, length, &server)) return false;
  bool ok = server_info(&server, out);
  qa_q2_rerelease_server_save_free(&server);
  return ok;
}

/* Retail saves are a few MB; the bound stops a gzip bomb dropped into a
 * scanned save directory (TS MAX_KEX_SAVE_BYTES, gunzipSync maxOutputLength). */
#define MAX_KEX_SAVE_BYTES ((size_t)256 * 1024 * 1024)

#define PAK_NAME_BYTES 56
#define PAK_ENTRY_BYTES 64

/* gunzipSync(compressed, { maxOutputLength }): malloc'd bytes, NULL only on OOM. Takes no ownership.
 * A zlib message longer than the local buffer is cut (zlib's texts are short). */
static uint8_t *gunzip(const uint8_t *compressed, size_t length, size_t *out_length) {
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
    qa_save_format_error("q2-kex", "cannot decompress save archive: %s",
                         stream.msg != NULL ? stream.msg : "zlib error");
  stream.next_in = (Bytef *)(uintptr_t)compressed;
  stream.avail_in = (uInt)length;
  size_t out_capacity = 65536;
  size_t produced = 0;
  uint8_t *out = (uint8_t *)malloc(out_capacity);
  if (out == NULL) { inflateEnd(&stream); return NULL; }
  for (;;) {
    if (produced == out_capacity) {
      if (out_capacity >= MAX_KEX_SAVE_BYTES) {
        inflateEnd(&stream);
        free(out);
        qa_save_format_error("q2-kex", "cannot decompress save archive: %s",
                             "Cannot create a Buffer larger than 268435456 bytes");
      }
      out_capacity *= 2;
      if (out_capacity > MAX_KEX_SAVE_BYTES) out_capacity = MAX_KEX_SAVE_BYTES;
      uint8_t *grown = (uint8_t *)realloc(out, out_capacity);
      if (grown == NULL) {
        inflateEnd(&stream);
        free(out);
        return NULL;
      }
      out = grown;
    }
    stream.next_out = out + produced;
    stream.avail_out = (uInt)(out_capacity - produced);
    int status = inflate(&stream, Z_NO_FLUSH);
    produced = (size_t)(stream.next_out - out);
    if (status == Z_STREAM_END) {
      /* Node's gunzip (Bun's too) reads every member of a multi-member file: bytes left after a member
       * start another one, unless the next is zero padding, which ends the input. */
      if (stream.avail_in > 0 && stream.next_in[0] != 0) {
        if (inflateReset(&stream) != Z_OK) {
          inflateEnd(&stream);
          free(out);
          qa_save_format_error("q2-kex", "cannot decompress save archive: %s", "zlib error");
        }
        continue;
      }
      break;
    }
    if (status != Z_OK && status != Z_BUF_ERROR) {
      char message[256];
      (void)snprintf(message, sizeof message, "%s", stream.msg != NULL ? stream.msg : "zlib error");
      inflateEnd(&stream);
      free(out);
      qa_save_format_error("q2-kex", "cannot decompress save archive: %s", message);
    }
    if (status == Z_BUF_ERROR && stream.avail_in == 0) {
      inflateEnd(&stream);
      free(out);
      qa_save_format_error("q2-kex", "cannot decompress save archive: %s",
                           "unexpected end of file");
    }
  }
  inflateEnd(&stream);
  *out_length = produced;
  return out;
}

/* Synchronous replacement for the TS async `Bun.file(path).bytes()`. NULL with errno set when the file cannot
 * be read or memory runs out. */
static uint8_t *read_file(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  /* TS Bun.file(path).bytes() rejects with the Node file error ("ENOENT: no such file or
   * directory, open '<path>'"), not a save format error: callers raise it from errno. */
  if (file == NULL) return NULL;
  size_t capacity = 65536;
  size_t length = 0;
  uint8_t *bytes = (uint8_t *)malloc(capacity);
  if (bytes == NULL) { fclose(file); return NULL; }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(bytes, capacity);
      if (grown == NULL) { free(bytes); fclose(file); return NULL; }
      bytes = grown;
    }
    size_t read = fread(bytes + length, 1, capacity - length, file);
    length += read;
    if (read == 0) break;
  }
  fclose(file);
  *out_length = length;
  return bytes;
}

typedef struct parse_scope {
  const uint8_t *pak;
  size_t pak_length;
  qa_kex_q2_save_archive *out;
  qa_binary_reader reader;
  bool reader_open;
  bool oom;
} parse_scope;
static const qa_kex_q2_save_entry *find_entry(const qa_kex_q2_save_archive *archive, const char *name) {
  for (size_t i = 0; i < archive->entry_count; i++)
    if (strcmp(archive->entries[i].name, name) == 0) return &archive->entries[i];
  return NULL;
}
const qa_kex_q2_save_entry *qa_kex_q2_save_archive_entry(const qa_kex_q2_save_archive *archive, const char *name) {
  return find_entry(archive, name);
}
static void parse_body(void *raw) {
  parse_scope *scope = (parse_scope *)raw;
  qa_kex_q2_save_archive *out = scope->out;
  const uint8_t *pak = scope->pak;
  size_t pak_length = scope->pak_length;
  if (!qa_binary_reader_init(&scope->reader, (uint8_t *)(uintptr_t)pak, pak_length, "q2-kex/save.pak")) {
    scope->oom = true;
    return;
  }
  scope->reader_open = true;
  qa_binary_reader *reader = &scope->reader;
  char *signature = qa_read_fixed(reader, 4);
  bool matches = signature != NULL && strcmp(signature, "PACK") == 0;
  free(signature);
  if (!matches) qa_save_format_error("q2-kex", "save archive is not a PAK");
  uint32_t directory_offset = qa_binary_reader_u32(reader);
  uint32_t directory_length = qa_binary_reader_u32(reader);
  if (directory_length % PAK_ENTRY_BYTES != 0 ||
      (uint64_t)directory_offset + (uint64_t)directory_length > (uint64_t)pak_length)
    qa_save_format_error("q2-kex", "save archive directory is out of bounds");
  qa_binary_reader_seek(reader, (int64_t)directory_offset);
  size_t entry_count = directory_length / PAK_ENTRY_BYTES;
  out->files = (char **)calloc(entry_count == 0 ? 1 : entry_count, sizeof *out->files);
  out->entries = (qa_kex_q2_save_entry *)calloc(entry_count == 0 ? 1 : entry_count, sizeof *out->entries);
  if (out->files == NULL || out->entries == NULL) { scope->oom = true; return; }
  for (size_t i = 0; i < entry_count; i++) {
    char *name = qa_read_fixed(reader, PAK_NAME_BYTES);
    if (name == NULL) { scope->oom = true; return; }
    for (size_t c = 0; name[c] != '\0'; c++) name[c] = (char)qa_js_latin1_lower((unsigned char)name[c]);
    qa_cleanup name_node;
    qa_cleanup_push(&name_node, free, name);
    uint32_t offset = qa_binary_reader_u32(reader);
    uint32_t entry_length = qa_binary_reader_u32(reader);
    if ((uint64_t)offset + (uint64_t)entry_length > (uint64_t)pak_length)
      qa_save_format_error("q2-kex", "save entry %s is out of bounds", name);
    if (find_entry(out, name) != NULL) qa_save_format_error("q2-kex", "save archive repeats %s", name);
    qa_cleanup_pop(&name_node);
    out->files[out->file_count] = dup_text(name);
    if (out->files[out->file_count] == NULL) { free(name); scope->oom = true; return; }
    out->file_count++;
    qa_kex_q2_save_entry *entry = &out->entries[out->entry_count++];
    entry->name = name;
    entry->length = entry_length;
    entry->bytes = (uint8_t *)malloc(entry_length == 0 ? 1 : entry_length);
    if (entry->bytes == NULL) { scope->oom = true; return; }
    memcpy(entry->bytes, pak + offset, entry_length);
  }
  const qa_kex_q2_save_entry *server = find_entry(out, "server.ssv");
  if (server == NULL) qa_save_format_error("q2-kex", "save archive has no server.ssv");
  const qa_kex_q2_save_entry *game = find_entry(out, "game.ssv");
  if (game != NULL) {
    /* new TextDecoder().decode drops one leading byte-order mark before JSON.parse. */
    size_t skip = game->length >= 3 && game->bytes[0] == 0xef && game->bytes[1] == 0xbb && game->bytes[2] == 0xbf ? 3 : 0;
    out->game = qa_parse_json_value(game->bytes + skip, game->length - skip, true);
    if (out->game == NULL) qa_save_format_error("q2-kex", "game.ssv is not JSON");
  }
  if (!qa_decode_q2_rerelease_server(server->bytes, server->length, &out->server) || !server_info(&out->server, &out->info))
    scope->oom = true;
}
bool qa_parse_kex_q2_save_archive(const uint8_t *compressed, size_t length, qa_kex_q2_save_archive *out) {
  memset(out, 0, sizeof *out);
  size_t pak_length = 0;
  uint8_t *pak = gunzip(compressed, length, &pak_length);
  if (pak == NULL) return false;
  parse_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.pak = pak;
  scope.pak_length = pak_length;
  scope.out = out;
  /* A malformed archive raises; the partial record is released first. */
  qa_caught_error caught;
  bool completed = qa_try(parse_body, &scope, &caught);
  if (scope.reader_open) qa_binary_reader_destroy(&scope.reader);
  free(pak);
  if (!completed) {
    qa_kex_q2_save_archive_free(out);
    qa_rethrow(&caught);
  }
  if (scope.oom) {
    qa_kex_q2_save_archive_free(out);
    return false;
  }
  return true;
}
/* Read the whole retail KEX Quake II save archive (server metadata, JSON game state and the per-level files). */
bool qa_read_kex_q2_save_archive(const char *path, qa_kex_q2_save_archive *out) {
  memset(out, 0, sizeof *out);
  size_t length = 0;
  uint8_t *compressed = read_file(path, &length);
  if (compressed == NULL) return false;
  qa_cleanup node;
  qa_cleanup_push(&node, free, compressed);
  bool ok = qa_parse_kex_q2_save_archive(compressed, length, out);
  qa_cleanup_pop(&node);
  free(compressed);
  return ok;
}
bool qa_read_kex_q2_save_info(const char *path, qa_kex_q2_save_info *out) {
  qa_kex_q2_save_archive archive;
  if (!qa_read_kex_q2_save_archive(path, &archive)) return false;
  *out = archive.info;
  memset(&archive.info, 0, sizeof archive.info);
  qa_kex_q2_save_archive_free(&archive);
  return true;
}

/* The archive layout the engine writes: `PACK`, the directory offset and length, the directory first and the file
 * data after it in directory order, gzip compressed. Re-packing the entries of a retail save reproduces its pak. */
uint8_t *qa_pack_kex_q2_save_pak(const qa_kex_q2_pack_entry *entries, size_t count, size_t *out_length) {
  for (size_t i = 0; i < count; i++) {
    const char *name = entries[i].name;
    size_t length = strlen(name);
    if (length == 0 || length >= PAK_NAME_BYTES || strchr(name, '\\') != NULL || strchr(name, '/') != NULL)
      qa_save_format_error("q2-kex", "invalid save entry name %s", name);
    for (size_t j = 0; j < i; j++)
      if (qa_js_latin1_equals_ignore_case(entries[j].name, name)) qa_save_format_error("q2-kex", "save archive repeats %s", name);
  }
  size_t directory_length = count * PAK_ENTRY_BYTES, data_start = 12 + directory_length, total = data_start;
  for (size_t i = 0; i < count; i++) total += entries[i].length;
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)total)) return NULL;
  qa_binary_writer_bytes(&writer, (const uint8_t *)"PACK", 4);
  qa_binary_writer_u32(&writer, 12);
  qa_binary_writer_u32(&writer, (int64_t)directory_length);
  size_t offset = data_start;
  for (size_t i = 0; i < count; i++) {
    uint8_t field[PAK_NAME_BYTES] = {0};
    memcpy(field, entries[i].name, strlen(entries[i].name));
    qa_binary_writer_bytes(&writer, field, sizeof field);
    qa_binary_writer_u32(&writer, (int64_t)offset);
    qa_binary_writer_u32(&writer, (int64_t)entries[i].length);
    offset += entries[i].length;
  }
  for (size_t i = 0; i < count; i++) qa_binary_writer_bytes(&writer, entries[i].bytes, entries[i].length);
  uint8_t *pak = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return pak;
}
uint8_t *qa_pack_kex_q2_save_archive(const qa_kex_q2_pack_entry *entries, size_t count, size_t *out_length) {
  size_t pak_length = 0;
  uint8_t *pak = qa_pack_kex_q2_save_pak(entries, count, &pak_length);
  if (pak == NULL) return NULL;
  /* gzipSync(pak): the zlib default level, a 15 bit window and memory level 8, with an empty gzip header. */
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    free(pak);
    return NULL;
  }
  uLong bound = deflateBound(&stream, (uLong)pak_length);
  uint8_t *out = (uint8_t *)malloc(bound == 0 ? 1 : bound);
  if (out == NULL) {
    deflateEnd(&stream);
    free(pak);
    return NULL;
  }
  stream.next_in = pak;
  stream.avail_in = (uInt)pak_length;
  stream.next_out = out;
  stream.avail_out = (uInt)bound;
  int status = deflate(&stream, Z_FINISH);
  size_t produced = (size_t)stream.total_out;
  deflateEnd(&stream);
  free(pak);
  if (status != Z_STREAM_END) {
    free(out);
    return NULL;
  }
  *out_length = produced;
  return out;
}

