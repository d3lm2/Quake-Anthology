/* Port of src/content/archive/zip.ts. GPL-2.0-or-later. */
#include "qa/content/archive/zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

static uint16_t load_u16(const uint8_t *bytes, size_t offset) {
  return (uint16_t)(bytes[offset] | ((uint16_t)bytes[offset + 1] << 8));
}
static uint32_t load_u32(const uint8_t *bytes, size_t offset) {
  return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
         ((uint32_t)bytes[offset + 2] << 16) |
         ((uint32_t)bytes[offset + 3] << 24);
}

static void reader_or_oom(qa_binary_reader *reader, const uint8_t *bytes,
                          size_t length, const char *source) {
  if (!qa_binary_reader_init(reader, (uint8_t *)(const void *)bytes, length,
                             source))
    qa_range_error("out of memory");
}

qa_zip_directory qa_read_zip_tail(const uint8_t *tail, size_t tail_length,
                                  const char *source, int64_t file_size) {
  /* The TS bounds-checks the whole tail through BinaryReader.dataView. */
  qa_binary_reader check;
  reader_or_oom(&check, tail, tail_length, source);
  (void)qa_binary_reader_data_view(&check, 0, (int64_t)tail_length);
  qa_binary_reader_destroy(&check);
  for (int64_t offset = (int64_t)tail_length - 22; offset >= 0; offset--) {
    const uint8_t *view = tail + offset;
    if (load_u32(view, 0) != 0x06054b50u) continue;
    if (offset + 22 + load_u16(view, 20) != (int64_t)tail_length) continue;
    uint16_t disk = load_u16(view, 4);
    uint16_t central_disk = load_u16(view, 6);
    uint16_t disk_entries = load_u16(view, 8);
    uint16_t entry_count = load_u16(view, 10);
    uint32_t byte_length = load_u32(view, 12);
    uint32_t relative_offset = load_u32(view, 16);
    int64_t end_offset = file_size - (int64_t)tail_length + offset;
    if (disk != 0 || central_disk != 0 || disk_entries != entry_count)
      qa_archive_error(source, end_offset, "multi-disk ZIP is unsupported");
    if (entry_count == 0xffff || byte_length == 0xffffffffu ||
        relative_offset == 0xffffffffu)
      qa_archive_error(source, end_offset, "ZIP64 is unsupported");
    qa_archive_check_range(source, end_offset, relative_offset, byte_length);
    int64_t prefix_length = end_offset - relative_offset - byte_length;
    qa_zip_directory directory = {relative_offset + prefix_length,
                                  byte_length, entry_count, prefix_length};
    return directory;
  }
  qa_archive_error(source, file_size - (int64_t)tail_length,
                   "ZIP end-of-central-directory record not found");
}

/* Entries built so far, released when a malformed record raises. */
typedef struct partial_entries {
  qa_archive_entry *entries;
  size_t count;
  qa_binary_reader *reader;
  char *raw_path;
} partial_entries;
static void partial_entries_release(void *arg) {
  partial_entries *partial = (partial_entries *)arg;
  for (size_t i = 0; i < partial->count; i++) {
    free(partial->entries[i].path);
    free(partial->entries[i].raw_path);
  }
  free(partial->entries);
  free(partial->raw_path);
  qa_binary_reader_destroy(partial->reader);
}

/* TS byteString: one char per byte. */
static char *byte_string(const uint8_t *bytes, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  if (length > 0) memcpy(out, bytes, length);
  out[length] = '\0';
  return out;
}

bool qa_read_zip_directory(const uint8_t *bytes, size_t length,
                           const char *source, qa_zip_directory directory,
                           qa_archive_format format,
                           qa_archive_entry **out_entries, size_t *out_count) {
  qa_binary_reader reader;
  reader_or_oom(&reader, bytes, length, source);
  qa_archive_entry *entries = NULL;
  size_t count = 0, capacity = 0;
  partial_entries partial = {NULL, 0, &reader, NULL};
  qa_cleanup partial_node;
  qa_cleanup_push(&partial_node, partial_entries_release, &partial);
  for (int64_t index = 0; index < directory.entry_count; index++) {
    partial.entries = entries;
    partial.count = count;
    partial.raw_path = NULL;
    size_t offset = qa_binary_reader_offset(&reader);
    const uint8_t *header =
        qa_binary_reader_data_view(&reader, (int64_t)offset, 46);
    if (load_u32(header, 0) != 0x02014b50u)
      qa_archive_error(source, directory.offset + (int64_t)offset,
                       "invalid central directory signature");
    uint16_t flags = load_u16(header, 8);
    uint16_t method = load_u16(header, 10);
    uint32_t crc = load_u32(header, 16);
    uint32_t compressed = load_u32(header, 20);
    uint32_t byte_length = load_u32(header, 24);
    uint16_t name_length = load_u16(header, 28);
    uint16_t extra_length = load_u16(header, 30);
    uint16_t comment_length = load_u16(header, 32);
    uint16_t disk = load_u16(header, 34);
    uint32_t local_offset = load_u32(header, 42);
    if (compressed == 0xffffffffu || byte_length == 0xffffffffu ||
        local_offset == 0xffffffffu)
      qa_archive_error(source, directory.offset + (int64_t)offset,
                       "ZIP64 entry is unsupported");
    if (disk != 0)
      qa_archive_error(source, directory.offset + (int64_t)offset,
                       "entry starts on another disk");
    if ((flags & 1) != 0)
      qa_archive_error(source, directory.offset + (int64_t)offset,
                       "encrypted entry is unsupported");
    if (method != 0 && method != 8)
      qa_archive_error(source, directory.offset + (int64_t)offset,
                       "unsupported compression method %u", method);
    if (method == 0 && compressed != byte_length)
      qa_archive_error(source, directory.offset + (int64_t)offset,
                       "stored entry sizes disagree");
    int64_t local_header_offset = local_offset + directory.prefix_length;
    qa_archive_check_range(source, directory.offset, local_header_offset, 30);
    qa_binary_reader_skip(&reader, 46);
    uint8_t *name_bytes = qa_binary_reader_bytes(&reader, name_length);
    /* TS tryNormalizeEntryPath rejects a NUL anywhere in the byte string. */
    bool has_nul = name_length > 0 && memchr(name_bytes, 0, name_length) != NULL;
    char *raw_path = byte_string(name_bytes, name_length);
    free(name_bytes);
    if (raw_path == NULL) goto oom;
    partial.raw_path = raw_path;
    qa_binary_reader_skip(&reader, (int64_t)extra_length + comment_length);
    partial.raw_path = NULL;
    bool unsafe = has_nul;
    char *path = has_nul ? NULL : qa_try_normalize_entry_path(raw_path, &unsafe);
    if (path == NULL && !unsafe) { free(raw_path); goto oom; }
    /* An unaddressable member is skipped instead of rejecting the archive. */
    if (path == NULL) { free(raw_path); continue; }
    if (count == capacity) {
      size_t next = capacity == 0 ? 16 : capacity * 2;
      qa_archive_entry *grown =
          (qa_archive_entry *)realloc(entries, next * sizeof *grown);
      if (grown == NULL) { free(raw_path); free(path); goto oom; }
      entries = grown;
      capacity = next;
    }
    qa_archive_entry *entry = &entries[count];
    memset(entry, 0, sizeof *entry);
    entry->format = format;
    entry->ordinal = (int64_t)count; /* TS entries.length */
    entry->raw_path = raw_path;
    entry->path = path;
    entry->byte_length = byte_length;
    entry->compressed_size = compressed;
    entry->compression_method = method;
    entry->flags = flags;
    entry->crc32 = crc;
    entry->local_header_offset = local_header_offset;
    size_t path_length = strlen(path);
    entry->is_directory = path_length > 0 && path[path_length - 1] == '/';
    count++;
  }
  partial.entries = entries;
  partial.count = count;
  if (qa_binary_reader_remaining(&reader) != 0)
    qa_archive_error(source,
                     directory.offset + (int64_t)qa_binary_reader_offset(&reader),
                     "central directory size disagrees with entry count");
  qa_cleanup_pop(&partial_node);
  qa_binary_reader_destroy(&reader);
  *out_entries = entries;
  *out_count = count;
  return true;
oom:
  qa_cleanup_pop(&partial_node);
  for (size_t i = 0; i < count; i++) {
    free(entries[i].path);
    free(entries[i].raw_path);
  }
  free(entries);
  qa_binary_reader_destroy(&reader);
  return false;
}

qa_zip_local_header qa_read_zip_local_header(const uint8_t *bytes,
                                             size_t length, const char *source,
                                             const qa_archive_entry *entry) {
  qa_binary_reader check;
  reader_or_oom(&check, bytes, length, source);
  (void)qa_binary_reader_data_view(&check, 0, 30);
  qa_binary_reader_destroy(&check);
  const uint8_t *view = bytes;
  if (load_u32(view, 0) != 0x04034b50u)
    qa_archive_error(source, entry->local_header_offset,
                     "invalid local file header signature");
  uint16_t flags = load_u16(view, 6);
  uint16_t method = load_u16(view, 8);
  uint32_t crc = load_u32(view, 14);
  uint32_t compressed = load_u32(view, 18);
  uint32_t byte_length = load_u32(view, 22);
  /* APPNOTE 4.4.4 reserves bit 15; as in the TS. */
  if ((((uint32_t)flags ^ (uint32_t)entry->flags) & 0x7fffu) != 0 ||
      (int64_t)method != entry->compression_method)
    qa_archive_error(source, entry->local_header_offset,
                     "local header disagrees with central directory");
  bool uses_descriptor = (flags & 8) != 0;
  if (((!uses_descriptor || crc != 0) && crc != entry->crc32) ||
      ((!uses_descriptor || compressed != 0) &&
       (int64_t)compressed != entry->compressed_size) ||
      ((!uses_descriptor || byte_length != 0) &&
       (int64_t)byte_length != entry->byte_length)) {
    qa_archive_error(source, entry->local_header_offset,
                     "local sizes or CRC disagree with central directory");
  }
  int64_t name_length = load_u16(view, 26);
  int64_t extra_length = load_u16(view, 28);
  qa_zip_local_header header = {name_length, name_length + extra_length};
  return header;
}

static bool bytes_match_string(const uint8_t *bytes, size_t length,
                               const char *text) {
  return strlen(text) == length &&
         (length == 0 || memcmp(bytes, text, length) == 0);
}

int64_t qa_zip_data_offset(const uint8_t *variable, size_t variable_length,
                           int64_t name_length, const char *source,
                           int64_t central_offset,
                           const qa_archive_entry *entry) {
  size_t name = name_length > 0 ? (size_t)name_length : 0;
  if (name > variable_length) name = variable_length;
  if (!bytes_match_string(variable, name, entry->raw_path))
    qa_archive_error(source, entry->local_header_offset,
                     "local entry name disagrees with central directory");
  int64_t data_offset =
      entry->local_header_offset + 30 + (int64_t)variable_length;
  qa_archive_check_range(source, central_offset, data_offset,
                         entry->compressed_size);
  return data_offset;
}

/* node:zlib inflateRawSync(compressed, { maxOutputLength }) as Bun runs it
 * (lib/zlib.js processChunkSync over ZlibContext): inflate with Z_FINISH into
 * 16 KiB output chunks. After each call a data error throws the zlib message;
 * Z_OK or Z_BUF_ERROR with output space left is "unexpected end of file"; then
 * the bytes read so far are counted, and more than maxOutputLength throws
 * RangeError "Cannot create a Buffer larger than N bytes". A full chunk asks
 * for another call. The TS catch turns each of those into ArchiveError
 * "DEFLATE failed: <message>". */
#define QA_ZLIB_CHUNK 16384u
static uint8_t *inflate_raw(const uint8_t *compressed, size_t compressed_length,
                            size_t max_output, const char *source,
                            int64_t data_offset, size_t *out_length) {
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  int status = inflateInit2(&stream, -MAX_WBITS);
  if (status != Z_OK)
    qa_archive_error(source, data_offset, "DEFLATE failed: %s",
                     stream.msg != NULL ? stream.msg : "Zlib error");
  uint8_t *output = NULL;
  size_t length = 0, capacity = 0;
  size_t in_offset = 0;
  for (;;) {
    if (capacity - length < QA_ZLIB_CHUNK) {
      size_t next = capacity == 0 ? QA_ZLIB_CHUNK : capacity * 2;
      while (next - length < QA_ZLIB_CHUNK) next *= 2;
      uint8_t *grown = (uint8_t *)realloc(output, next);
      if (grown == NULL) {
        free(output);
        (void)inflateEnd(&stream);
        qa_range_error("out of memory");
      }
      output = grown;
      capacity = next;
    }
    /* zlib counts input in uInt; feed what is left, at most UINT_MAX. */
    size_t in_left = compressed_length - in_offset;
    uInt in_now = in_left > (size_t)0xffffffffu ? 0xffffffffu : (uInt)in_left;
    stream.next_in = (Bytef *)(const void *)(compressed + in_offset);
    stream.avail_in = in_now;
    stream.next_out = output + length;
    stream.avail_out = QA_ZLIB_CHUNK;
    status = inflate(&stream, Z_FINISH);
    in_offset += in_now - stream.avail_in;
    const char *failure = NULL;
    if (status == Z_OK || status == Z_BUF_ERROR) {
      if (stream.avail_out != 0) failure = stream.msg != NULL ? stream.msg : "unexpected end of file";
    } else if (status == Z_NEED_DICT) {
      failure = stream.msg != NULL ? stream.msg : "Missing dictionary";
    } else if (status != Z_STREAM_END) {
      failure = stream.msg != NULL ? stream.msg : "Zlib error";
    }
    if (failure != NULL) {
      char message[256];
      (void)snprintf(message, sizeof message, "%s", failure);
      (void)inflateEnd(&stream);
      free(output);
      qa_archive_error(source, data_offset, "DEFLATE failed: %s", message);
    }
    length += QA_ZLIB_CHUNK - stream.avail_out;
    if (length > max_output) {
      (void)inflateEnd(&stream);
      free(output);
      qa_archive_error(source, data_offset,
                       "DEFLATE failed: Cannot create a Buffer larger than %zu bytes",
                       max_output);
    }
    if (stream.avail_out != 0) break;
  }
  (void)inflateEnd(&stream);
  *out_length = length;
  return output;
}

uint8_t *qa_decode_zip_entry(const uint8_t *compressed, size_t compressed_length,
                             const char *source, int64_t data_offset,
                             const qa_archive_entry *entry, size_t *out_length) {
  uint8_t *output = NULL;
  size_t output_length = 0;
  if (entry->compression_method == 0) {
    output_length = compressed_length;
    if (output_length > 0) {
      output = (uint8_t *)malloc(output_length);
      if (output == NULL) return NULL;
      memcpy(output, compressed, output_length);
    }
  } else {
    /* TS inflateRawSync with maxOutputLength max(1, byteLength). */
    size_t max_output = entry->byte_length > 1 ? (size_t)entry->byte_length : 1;
    output = inflate_raw(compressed, compressed_length, max_output, source,
                         data_offset, &output_length);
  }
  if ((int64_t)output_length != entry->byte_length) {
    free(output);
    qa_archive_error(source, data_offset,
                     "decoded size %zu disagrees with %lld", output_length,
                     (long long)entry->byte_length);
  }
  uLong crc = crc32(0L, Z_NULL, 0);
  /* crc32 takes a uInt length; entries are below 4 GiB. */
  if (output_length > 0) crc = crc32(crc, output, (uInt)output_length);
  if ((uint32_t)crc != entry->crc32) {
    free(output);
    qa_archive_error(source, data_offset, "entry CRC32 mismatch");
  }
  if (output_length == 0) {
    free(output);
    output = NULL;
  }
  if (out_length != NULL) *out_length = output_length;
  return output;
}
