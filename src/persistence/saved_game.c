/* Port of src/persistence/saved-game.ts. GPL-2.0-or-later. */
#include "qa/persistence/saved_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/sha.h>

#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/save_policy.h"
#include "qa/persistence/value.h"

/* TS /^[A-Za-z0-9_/-]+$/ plus the split("/") part check. The Q1 map
 * is a byte string in both implementations, so UTF-16 vs byte
 * lengths cannot differ on this ASCII-only alphabet. */
static bool valid_source_map(const char *map) {
  if (map == NULL || map[0] == '\0') return false;
  for (const char *cursor = map; *cursor != '\0'; cursor++) {
    char ch = *cursor;
    bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '-';
    if (!ok) return false;
  }
  const char *part = map;
  for (;;) {
    const char *slash = strchr(part, '/');
    size_t part_length = slash != NULL ? (size_t)(slash - part) : strlen(part);
    if (part_length == 0) return false;
    if (part_length == 1 && part[0] == '.') return false;
    if (part_length == 2 && part[0] == '.' && part[1] == '.') return false;
    if (slash == NULL) break;
    part = slash + 1;
  }
  return true;
}

bool qa_decode_saved_game(const uint8_t *bytes, size_t length, qa_saved_game *out) {
  memset(out, 0, sizeof *out);
  /* TS indexes bytes[0]/[1]; a short buffer reads as undefined there
   * and misses the shared signature, as the length guard does here. */
  if (length >= 2 && bytes[0] == 81 && bytes[1] == 84) {
    qa_save_image_result result;
    if (!qa_decode_save_image(bytes, length, &result)) return false;
    out->kind = QA_SAVED_GAME_SHARED;
    out->image = result.image;
    out->image_root = result.root;
    return true;
  }
  if (!qa_decode_q1_save(bytes, length, &out->data)) return false;
  if (!valid_source_map(out->data.map))
    qa_save_format_error("q1.map", "invalid source map name");
  if (out->data.time < 0 || out->data.skill < 0 || out->data.skill > 3 ||
      out->data.entity_count < 2 || out->data.entities[0].pair_count == 0)
    qa_save_format_error("q1", "save has no valid singleplayer world");
  out->kind = QA_SAVED_GAME_Q1_SOURCE;
  return true;
}

/* A save in the original format cannot hold everything this engine keeps (mixed or engine-only state), so the
 * engine writes that state beside it: a sidecar holding the shared container behind a signature and the SHA-256
 * of the original-format file or files it belongs to. Loading the original save restores from the sidecar while
 * the two still match, and from the original files alone when they do not. */
static const uint8_t sidecar_signature[8] = {'Q', 'T', 'S', 'I', 'D', 'E', '1', '\n'};

/* path.replace(/\.(?:sav|pak\.gz)$/i, SIDECAR_EXTENSION) */
char *qa_sidecar_path(const char *path) {
  size_t length = strlen(path);
  size_t stem = length;
  static const char *const endings[] = {".sav", ".pak.gz"};
  for (size_t i = 0; i < 2 && stem == length; i++) {
    size_t ending = strlen(endings[i]);
    if (length < ending) continue;
    bool matches = true;
    for (size_t at = 0; at < ending && matches; at++) {
      char c = path[length - ending + at];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      matches = c == endings[i][at];
    }
    if (matches) stem = length - ending;
  }
  bool replaced = stem != length;
  size_t extension = replaced ? strlen(QA_SIDECAR_EXTENSION) : 0;
  char *out = (char *)malloc(stem + extension + 1);
  if (out == NULL) return NULL;
  memcpy(out, path, stem);
  if (replaced) memcpy(out + stem, QA_SIDECAR_EXTENSION, extension);
  out[stem + extension] = '\0';
  return out;
}
void qa_save_digest_bytes(const uint8_t *bytes, size_t length, uint8_t out[QA_SAVE_DIGEST_BYTES]) {
  SHA256(bytes, length, out);
}
/* `digest` identifies the original-format content the image was captured with. */
uint8_t *qa_encode_save_sidecar(const uint8_t digest[QA_SAVE_DIGEST_BYTES], const qa_save_image *image,
                                size_t *out_length) {
  size_t body_length = 0;
  uint8_t *body = NULL;
  if (!qa_encode_save_image(image, &body, &body_length)) return NULL;
  const size_t header = sizeof sidecar_signature + QA_SAVE_DIGEST_BYTES;
  uint8_t *out = (uint8_t *)malloc(header + body_length);
  if (out == NULL) {
    free(body);
    return NULL;
  }
  memcpy(out, sidecar_signature, sizeof sidecar_signature);
  memcpy(out + sizeof sidecar_signature, digest, QA_SAVE_DIGEST_BYTES);
  memcpy(out + header, body, body_length);
  free(body);
  *out_length = header + body_length;
  return out;
}
/* The image when the sidecar was written beside exactly the original content `digest` names; false when it was not. */
bool qa_decode_save_sidecar(const uint8_t *bytes, size_t length, const uint8_t digest[QA_SAVE_DIGEST_BYTES],
                            qa_save_image_result *out) {
  const size_t header = sizeof sidecar_signature + QA_SAVE_DIGEST_BYTES;
  if (length <= header || memcmp(bytes, sidecar_signature, sizeof sidecar_signature) != 0) return false;
  if (memcmp(bytes + sizeof sidecar_signature, digest, QA_SAVE_DIGEST_BYTES) != 0) return false;
  if (!qa_decode_save_image(bytes + header, length - header, out)) qa_raise("Error", 0, 0, "%s", "out of memory");
  return true;
}

/* Bun.file(path).bytes(): malloc'd, NULL on I/O failure or OOM (errno set by the failing call). */
static uint8_t *read_file(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  size_t capacity = 65536;
  size_t length = 0;
  uint8_t *bytes = (uint8_t *)malloc(capacity);
  if (bytes == NULL) {
    fclose(file);
    return NULL;
  }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(bytes, capacity);
      if (grown == NULL) {
        free(bytes);
        fclose(file);
        return NULL;
      }
      bytes = grown;
    }
    size_t count = fread(bytes + length, 1, capacity - length, file);
    length += count;
    if (count == 0) break;
  }
  bool failed = ferror(file) != 0;
  fclose(file);
  if (failed) {
    free(bytes);
    return NULL;
  }
  *out_length = length;
  return bytes;
}

bool qa_read_saved_game(const char *path, qa_saved_game *out) {
  memset(out, 0, sizeof *out);
  size_t length = 0;
  uint8_t *bytes = read_file(path, &length);
  if (bytes == NULL) return false;
  if (!(length >= 2 && bytes[0] == 81 && bytes[1] == 84)) {
    /* An original-format save: the engine's sidecar beside it answers while it still matches these bytes. */
    char *sidecar = qa_sidecar_path(path);
    if (sidecar == NULL) {
      free(bytes);
      return false;
    }
    size_t sidecar_length = 0;
    uint8_t *sidecar_bytes = read_file(sidecar, &sidecar_length);
    free(sidecar);
    if (sidecar_bytes != NULL) {
      uint8_t digest[QA_SAVE_DIGEST_BYTES];
      qa_save_digest_bytes(bytes, length, digest);
      qa_cleanup bytes_node, sidecar_node;
      qa_cleanup_push(&bytes_node, free, bytes);
      qa_cleanup_push(&sidecar_node, free, sidecar_bytes);
      qa_save_image_result result;
      bool found = qa_decode_save_sidecar(sidecar_bytes, sidecar_length, digest, &result);
      qa_cleanup_pop(&sidecar_node);
      qa_cleanup_pop(&bytes_node);
      free(sidecar_bytes);
      if (found) {
        free(bytes);
        out->kind = QA_SAVED_GAME_SHARED;
        out->image = result.image;
        out->image_root = result.root;
        return true;
      }
    }
  }
  qa_cleanup node;
  qa_cleanup_push(&node, free, bytes);
  bool ok = qa_decode_saved_game(bytes, length, out);
  qa_cleanup_pop(&node);
  free(bytes);
  return ok;
}

bool qa_write_saved_game(const char *directory, const char *path,
                         const qa_saved_game *save) {
  return qa_write_saved_game_sidecar(directory, path, save, NULL);
}
bool qa_write_saved_game_sidecar(const char *directory, const char *path, const qa_saved_game *save,
                                 const qa_save_image *sidecar) {
  size_t length = 0;
  uint8_t *bytes = NULL;
  if (save->kind == QA_SAVED_GAME_SHARED) {
    if (!qa_encode_save_image(&save->image, &bytes, &length)) return false;
  } else {
    bytes = qa_encode_q1_save(&save->data, &length);
    if (bytes == NULL) return false;
  }
  qa_cleanup node;
  qa_cleanup_push(&node, free, bytes);
  bool ok = qa_write_contained_save(directory, path, bytes, length);
  if (ok && sidecar != NULL && save->kind == QA_SAVED_GAME_Q1_SOURCE) {
    uint8_t digest[QA_SAVE_DIGEST_BYTES];
    qa_save_digest_bytes(bytes, length, digest);
    size_t sidecar_length = 0;
    uint8_t *sidecar_bytes = qa_encode_save_sidecar(digest, sidecar, &sidecar_length);
    char *sidecar_file = qa_sidecar_path(path);
    ok = sidecar_bytes != NULL && sidecar_file != NULL &&
         qa_write_contained_save_extension(directory, sidecar_file, sidecar_bytes, sidecar_length, QA_SIDECAR_EXTENSION);
    free(sidecar_bytes);
    free(sidecar_file);
  }
  qa_cleanup_pop(&node);
  free(bytes);
  return ok;
}

void qa_saved_game_free(qa_saved_game *save) {
  if (save == NULL) return;
  if (save->kind == QA_SAVED_GAME_SHARED && save->image_root != NULL) {
    qa_save_image_result result = {save->image, save->image_root};
    qa_save_image_result_free(&result);
  }
  qa_q1_save_data_free(&save->data);
  memset(save, 0, sizeof *save);
}
