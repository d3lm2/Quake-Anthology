/* Port of src/content/archive/pak.ts. GPL-2.0-or-later. */
#include "qa/content/archive/pak.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

static void reader_or_oom(qa_binary_reader *reader, const uint8_t *bytes,
                          size_t length, const char *source) {
  /* BinaryReader borrows mutable storage; it never writes through it. */
  if (!qa_binary_reader_init(reader, (uint8_t *)(const void *)bytes, length,
                             source))
    qa_range_error("out of memory");
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

qa_pak_header qa_read_pak_header(const uint8_t *bytes, size_t length,
                                 const char *source, int64_t file_size) {
  qa_binary_reader reader;
  reader_or_oom(&reader, bytes, length, source);
  qa_binary_reader_expect_magic(&reader, "PACK");
  int64_t offset = qa_binary_reader_i32(&reader);
  int64_t byte_length = qa_binary_reader_i32(&reader);
  qa_binary_reader_destroy(&reader);
  qa_archive_check_range(source, file_size, offset, byte_length);
  if (offset < 12 || byte_length % 64 != 0)
    qa_archive_error(source, offset, "invalid PACK directory range");
  qa_pak_header header = {offset, byte_length};
  return header;
}

bool qa_read_pak_directory(const uint8_t *bytes, size_t length,
                           const char *source, int64_t file_size,
                           qa_archive_entry **out_entries, size_t *out_count) {
  qa_binary_reader reader;
  reader_or_oom(&reader, bytes, length, source);
  qa_archive_entry *entries = NULL;
  size_t count = 0, capacity = 0;
  partial_entries partial = {NULL, 0, &reader, NULL};
  qa_cleanup partial_node;
  qa_cleanup_push(&partial_node, partial_entries_release, &partial);
  while (qa_binary_reader_remaining(&reader) > 0) {
    partial.entries = entries;
    partial.count = count;
    partial.raw_path = NULL;
    char *raw_path = qa_binary_reader_fixed_byte_string(&reader, 56);
    partial.raw_path = raw_path;
    int64_t data_offset = qa_binary_reader_i32(&reader);
    int64_t byte_length = qa_binary_reader_i32(&reader);
    if (raw_path == NULL) goto oom;
    qa_archive_check_range(source, file_size, data_offset, byte_length);
    partial.raw_path = NULL;
    bool unsafe = false;
    char *path = qa_try_normalize_entry_path(raw_path, &unsafe);
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
    entry->format = QA_ARCHIVE_FORMAT_PAK;
    entry->ordinal = (int64_t)count;
    entry->path = path;
    entry->raw_path = raw_path;
    entry->byte_length = byte_length;
    entry->compressed_size = byte_length;
    entry->compression_method = 0;
    entry->data_offset = data_offset;
    size_t path_length = strlen(path);
    entry->is_directory = path_length > 0 && path[path_length - 1] == '/';
    count++;
  }
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
