/* Port of src/formats/images/wad.ts. GPL-2.0-or-later. */
#include "qa/formats/images/wad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

/* A BinaryError caught by the caller (a missing or broken WAD) must not leak the
 * readers, the lumps read so far or the name of the lump being read. */
typedef struct wad_guard {
  qa_binary_reader *reader, *directory;
  qa_wad_archive *archive;
  char **lump_name;
} wad_guard;
static void wad_guard_release(void *arg) {
  wad_guard *guard = (wad_guard *)arg;
  if (guard->lump_name != NULL) free(*guard->lump_name);
  if (guard->archive != NULL) qa_wad_archive_destroy(guard->archive);
  if (guard->directory != NULL) qa_binary_reader_destroy(guard->directory);
  qa_binary_reader_destroy(guard->reader);
}
typedef struct wad_image_guard {
  qa_binary_reader *reader;
  qa_wad_image *image;
} wad_image_guard;
static void wad_image_guard_release(void *arg) {
  wad_image_guard *guard = (wad_image_guard *)arg;
  qa_wad_image_destroy(guard->image);
  qa_binary_reader_destroy(guard->reader);
}

void qa_wad_archive_destroy(qa_wad_archive *archive) {
  for (size_t i = 0; i < archive->lump_count; i++) {
    free(archive->lumps[i].name);
    free(archive->lumps[i].bytes);
  }
  free(archive->lumps);
  archive->lumps = NULL;
  archive->lump_count = 0;
}

void qa_wad_image_destroy(qa_wad_image *image) {
  switch (image->kind) {
    case QA_WAD_IMAGE_QPIC: qa_indexed_image_destroy(&image->image); break;
    case QA_WAD_IMAGE_MIPTEX:
      qa_q1_miptex_destroy(&image->texture);
      free(image->palette);
      image->palette = NULL;
      image->palette_length = 0;
      break;
    case QA_WAD_IMAGE_PALETTE:
      free(image->colors);
      image->colors = NULL;
      image->colors_length = 0;
      break;
    case QA_WAD_IMAGE_RAW: break;
  }
}

/* JS toLowerCase over the fixed byte string (Latin-1 code units). */
static void lower_name(char *name) {
  for (unsigned char *p = (unsigned char *)name; *p != 0; p++) {
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 0xc0 && *p <= 0xde && *p != 0xd7)) {
      *p = (unsigned char)(*p + 32);
    }
  }
}

int qa_decode_wad(const uint8_t *bytes, size_t length, const char *source,
                  qa_wad_archive *out) {
  const char *name = source != NULL ? source : "<wad>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  wad_guard guard_state = {&reader, NULL, NULL, NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, wad_guard_release, &guard_state);
  char *kind = qa_binary_reader_fixed_byte_string(&reader, 4);
  if (kind == NULL) { qa_cleanup_pop(&guard); qa_binary_reader_destroy(&reader); return 0; }
  int is_wad2 = strcmp(kind, "WAD2") == 0;
  int is_wad3 = strcmp(kind, "WAD3") == 0;
  free(kind);
  if (!is_wad2 && !is_wad3) qa_binary_error(name, 0, "Expected WAD2 or WAD3");
  int64_t count = qa_binary_reader_i32(&reader);
  int64_t offset = qa_binary_reader_i32(&reader);
  if (count < 0) qa_binary_error(name, 4, "Negative WAD lump count");
  qa_binary_reader directory;
  if (!qa_binary_reader_section(&reader, offset, count * 32, &directory)) {
    qa_cleanup_pop(&guard);
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  guard_state.directory = &directory;
  qa_wad_lump *lumps =
      count == 0 ? NULL : (qa_wad_lump *)calloc((size_t)count, sizeof *lumps);
  if (count != 0 && lumps == NULL) {
    qa_cleanup_pop(&guard);
    qa_binary_reader_destroy(&directory);
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  out->kind = is_wad3 ? "WAD3" : "WAD2";
  out->lumps = lumps;
  out->lump_count = 0;
  guard_state.archive = out;
  char *lump_name = NULL;
  guard_state.lump_name = &lump_name;
  for (int64_t index = 0; index < count; index++) {
    int64_t start = qa_binary_reader_i32(&directory);
    int64_t disk_size = qa_binary_reader_i32(&directory);
    int64_t byte_length = qa_binary_reader_i32(&directory);
    int64_t type = qa_binary_reader_u8(&directory);
    int64_t compression = qa_binary_reader_u8(&directory);
    qa_binary_reader_skip(&directory, 2);
    lump_name = qa_binary_reader_fixed_byte_string(&directory, 16);
    if (lump_name == NULL) {
      qa_cleanup_pop(&guard);
      qa_binary_reader_destroy(&directory);
      qa_binary_reader_destroy(&reader);
      qa_wad_archive_destroy(out);
      return 0;
    }
    lower_name(lump_name);
    if (byte_length < 0) {
      qa_binary_error(name, offset + index * 32 + 8, "Negative WAD decoded size");
    }
    qa_binary_reader lump_section;
    if (!qa_binary_reader_section(&reader, start, disk_size, &lump_section)) {
      qa_cleanup_pop(&guard);
      free(lump_name);
      qa_binary_reader_destroy(&directory);
      qa_binary_reader_destroy(&reader);
      qa_wad_archive_destroy(out);
      return 0;
    }
    uint8_t *lump_bytes = qa_binary_reader_bytes(&lump_section, disk_size);
    qa_binary_reader_destroy(&lump_section);
    if (lump_bytes == NULL && disk_size != 0) {
      qa_cleanup_pop(&guard);
      free(lump_name);
      qa_binary_reader_destroy(&directory);
      qa_binary_reader_destroy(&reader);
      qa_wad_archive_destroy(out);
      return 0;
    }
    qa_wad_lump *lump = &lumps[out->lump_count++];
    lump->name = lump_name;
    lump->type = type;
    lump->compression = compression;
    lump->offset = start;
    lump->disk_size = disk_size;
    lump->byte_length = byte_length;
    lump->bytes = lump_bytes;
    lump->bytes_length = (size_t)disk_size;
    lump_name = NULL;
  }
  qa_cleanup_pop(&guard);
  qa_binary_reader_destroy(&directory);
  qa_binary_reader_destroy(&reader);
  return 1;
}

/* A Quake byte string is Latin-1 text in TS (fixedByteString): a byte past
 * 0x7f is one code unit, two UTF-8 bytes in a message. Caller frees. */
static char *latin1_text(const char *bytes) {
  size_t length = 0;
  for (const unsigned char *p = (const unsigned char *)bytes; *p != 0; p++) length += *p >= 0x80 ? 2 : 1;
  char *text = malloc(length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (const unsigned char *p = (const unsigned char *)bytes; *p != 0; p++) {
    if (*p < 0x80) {
      text[at++] = (char)*p;
    } else {
      text[at++] = (char)(0xc0 | (*p >> 6));
      text[at++] = (char)(0x80 | (*p & 0x3f));
    }
  }
  text[at] = '\0';
  return text;
}

/* TS `${source}:${lump.name}`: the BinaryError source of the nested decoder. */
static char *lump_source(const char *source, const char *lump_name) {
  char *name = latin1_text(lump_name);
  size_t length = strlen(source) + 1 + strlen(name) + 1;
  char *joined = (char *)malloc(length);
  if (joined != NULL) snprintf(joined, length, "%s:%s", source, name);
  free(name);
  return joined;
}

int qa_decode_wad_image(const qa_wad_archive *archive, const qa_wad_lump *lump,
                        const char *source, qa_wad_image *out) {
  const char *name = source != NULL ? source : "<wad>";
  memset(out, 0, sizeof *out);
  if (lump->compression != 0) {
    qa_binary_error(name, lump->offset, "WAD compression %lld is unsupported",
                    (long long)lump->compression);
  }
  if (lump->disk_size != lump->byte_length) {
    qa_binary_error(name, lump->offset, "Uncompressed WAD lump size mismatch");
  }
  int is_wad3 = strcmp(archive->kind, "WAD3") == 0;
  if (lump->type == 66) {
    char *joined = lump_source(name, lump->name);
    if (joined == NULL) return 0;
    /* The nested decoder raises a BinaryError on a bad lump: the source text must not leak. */
    qa_cleanup joined_guard;
    qa_cleanup_push(&joined_guard, free, joined);
    int ok = qa_decode_qpic(lump->bytes, lump->bytes_length, joined, &out->image);
    qa_cleanup_pop(&joined_guard);
    free(joined);
    if (!ok) return 0;
    out->kind = QA_WAD_IMAGE_QPIC;
    return 1;
  }
  if (lump->type == 68 || (is_wad3 && lump->type == 67)) {
    char *joined = lump_source(name, lump->name);
    if (joined == NULL) return 0;
    qa_cleanup joined_guard;
    qa_cleanup_push(&joined_guard, free, joined);
    int ok = qa_decode_q1_mip_texture(lump->bytes, lump->bytes_length, joined,
                                      &out->texture);
    qa_cleanup_pop(&joined_guard);
    free(joined);
    if (!ok) return 0;
    out->kind = QA_WAD_IMAGE_MIPTEX;
    if (is_wad3 && out->texture.kind == QA_Q1_MIP_EMBEDDED) {
      qa_binary_reader reader;
      if (!qa_binary_reader_init(&reader, lump->bytes, lump->bytes_length, name)) {
        qa_wad_image_destroy(out);
        return 0;
      }
      wad_image_guard guard_state = {&reader, out};
      qa_cleanup guard;
      qa_cleanup_push(&guard, wad_image_guard_release, &guard_state);
      qa_binary_reader_seek(&reader, 36);
      int64_t end = (int64_t)qa_binary_reader_u32(&reader) +
                    (int64_t)out->texture.levels[3].pixels.length;
      qa_binary_reader_seek(&reader, end);
      int64_t palette_count = qa_binary_reader_u16(&reader);
      if (palette_count != 256) {
        qa_binary_error(name, end, "WAD3 miptex requires 256 palette colors");
      }
      out->palette = qa_binary_reader_bytes(&reader, palette_count * 3);
      out->palette_length = (size_t)(palette_count * 3);
      qa_cleanup_pop(&guard);
      qa_binary_reader_destroy(&reader);
      if (out->palette == NULL) {
        qa_wad_image_destroy(out);
        return 0;
      }
    }
    return 1;
  }
  if (lump->type == 64 && lump->bytes_length == 768) {
    out->colors = (uint8_t *)malloc(768);
    if (out->colors == NULL) return 0;
    memcpy(out->colors, lump->bytes, 768);
    out->colors_length = 768;
    out->kind = QA_WAD_IMAGE_PALETTE;
    return 1;
  }
  out->kind = QA_WAD_IMAGE_RAW;
  out->lump = lump;
  return 1;
}
