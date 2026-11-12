/* Port of src/capture/index.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/capture/index.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/content/archive/types.h"
#include "qa/formats/images/jpeg_encoder.h"
#include "qa/formats/images/png_encoder.h"
#include "qa/formats/images/tga.h"
#include "qa/settings/config.h"
#include "qa/foundation/support.h"

/* TS throws plain Error in this module (a rejection for the async methods):
 * a catchable raise named "Error"; uncaught it prints and aborts as before. */
#define capture_error(...) qa_raise("Error", 0, 0, __VA_ARGS__)

static void check_frame(const qa_image_level *image) {
  if (!isfinite(image->width) || !isfinite(image->height) ||
      image->width != trunc(image->width) ||
      image->height != trunc(image->height) || image->width < 1 ||
      image->height < 1 ||
      (double)image->pixels.length != image->width * image->height * 4) {
    capture_error("Renderer returned invalid RGBA frame dimensions");
  }
}

uint8_t *qa_encode_screenshot(const qa_image_level *image,
                              qa_screenshot_format format, int64_t quality,
                              size_t *out_length) {
  check_frame(image);
  switch (format) {
    case QA_SCREENSHOT_TGA:
      return qa_encode_tga(image, out_length);
    case QA_SCREENSHOT_PNG:
      return qa_encode_png(image->width, image->height, image->pixels.bytes,
                           image->pixels.length, out_length);
    case QA_SCREENSHOT_JPG:
      return qa_encode_jpeg(image, quality, NULL, out_length);
  }
  *out_length = 0;
  return NULL;
}

/** R_LevelShot samples a 512x384 grid, averaging twelve samples into each 128x128 texel. */
bool qa_make_levelshot(const qa_image_level *image, const uint8_t *gamma,
                       size_t gamma_length, qa_image_level *out) {
  check_frame(image);
  if (gamma != NULL && gamma_length != 256) {
    capture_error("Gamma table must contain 256 entries");
  }
  uint8_t *pixels = (uint8_t *)malloc(128 * 128 * 4);
  if (pixels == NULL) return false;
  /* The TS views pixels through a DataView at its byteOffset; the C
   * byte view already points at the first visible byte. */
  const uint8_t *input = image->pixels.bytes;
  /* Math.fround staging: compute in double, cast to float. */
  const float x_scale = (float)(image->width / 512);
  const float y_scale = (float)(image->height / 384);
  const int64_t width = (int64_t)image->width;
  const int64_t height = (int64_t)image->height;
  for (int64_t y = 0; y < 128; y++) {
    for (int64_t x = 0; x < 128; x++) {
      int r = 0, g = 0, b = 0;
      for (int64_t yy = 0; yy < 3; yy++) {
        for (int64_t xx = 0; xx < 4; xx++) {
          int64_t sx = (int64_t)trunc((float)((double)(x * 4 + xx) * (double)x_scale));
          int64_t sy = (int64_t)trunc((float)((double)(y * 3 + yy) * (double)y_scale));
          if (sx > width - 1) sx = width - 1;
          if (sy > height - 1) sy = height - 1;
          const size_t offset = (size_t)(sy * width + sx) * 4;
          r += input[offset];
          g += input[offset + 1];
          b += input[offset + 2];
        }
      }
      const size_t offset = (size_t)(y * 128 + x) * 4;
      pixels[offset] = gamma != NULL ? gamma[r / 12] : (uint8_t)(r / 12);
      pixels[offset + 1] = gamma != NULL ? gamma[g / 12] : (uint8_t)(g / 12);
      pixels[offset + 2] = gamma != NULL ? gamma[b / 12] : (uint8_t)(b / 12);
      pixels[offset + 3] = 255;
    }
  }
  out->width = 128;
  out->height = 128;
  out->pixels.bytes = pixels;
  out->pixels.length = 128 * 128 * 4;
  return true;
}

/* --- FrameCapture (synchronous; the TS write chain is call order) --- */

bool qa_frame_capture_init(qa_frame_capture *capture, const char *root,
                           qa_frame_readback readback) {
  /* TS Q3_SCREENSHOTS. */
  const qa_screenshot_naming q3 = {"screenshots", "shot", 4};
  return qa_frame_capture_init_naming(capture, root, readback, q3);
}

bool qa_frame_capture_init_naming(qa_frame_capture *capture, const char *root,
                                  qa_frame_readback readback, qa_screenshot_naming naming) {
  memset(capture, 0, sizeof *capture);
  capture->root = strdup(root);
  char *directory = strdup(naming.directory);
  char *prefix = strdup(naming.prefix);
  if (capture->root == NULL || directory == NULL || prefix == NULL) {
    free(capture->root);
    free(directory);
    free(prefix);
    capture->root = NULL;
    return false;
  }
  capture->readback = readback;
  capture->naming.directory = directory;
  capture->naming.prefix = prefix;
  capture->naming.digits = naming.digits;
  return true;
}

void qa_frame_capture_destroy(qa_frame_capture *capture) {
  if (capture == NULL) return;
  free(capture->root);
  free((char *)capture->naming.directory);
  free((char *)capture->naming.prefix);
  memset(capture, 0, sizeof *capture);
}

void qa_capture_result_free(qa_capture_result *result) {
  if (result == NULL) return;
  free(result->path);
  memset(result, 0, sizeof *result);
}

static const char *format_extension(qa_screenshot_format format) {
  switch (format) {
    case QA_SCREENSHOT_TGA: return "tga";
    case QA_SCREENSHOT_PNG: return "png";
    case QA_SCREENSHOT_JPG: return "jpg";
  }
  return "";
}

/* TS mkdir(dirname, { recursive: true }) as Bun runs it: the full path first;
 * ENOENT walks up to the parent and retries; any other failure raises Bun's
 * Error naming the path that failed (an existing non-directory is EEXIST). */
static void make_directories(const char *dir) {
  if (mkdir(dir, 0777) == 0) return;
  int error = errno;
  if (error == EEXIST) {
    struct stat status;
    if (stat(dir, &status) == 0 && S_ISDIR(status.st_mode)) return;
    qa_content_fs_error(EEXIST, "mkdir", dir);
  }
  if (error != ENOENT) qa_content_fs_error(error, "mkdir", dir);
  const char *slash = strrchr(dir, '/');
  if (slash == NULL || slash == dir) qa_content_fs_error(error, "mkdir", dir);
  char *parent = strndup(dir, (size_t)(slash - dir));
  if (parent == NULL) capture_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, parent);
  make_directories(parent);
  qa_cleanup_pop(&guard);
  free(parent);
  if (mkdir(dir, 0777) != 0 && errno != EEXIST) qa_content_fs_error(errno, "mkdir", dir);
}

/* settingsPath(root, relative) with a formatted relative name;
 * NULL only on OOM (qa_settings_path aborts on escape, as TS throws). */
static char *capture_path(const qa_frame_capture *capture, const char *relative_format,
                          const char *middle, const char *extension) {
  size_t length = strlen(relative_format) + strlen(middle) + strlen(extension) + 1;
  char *relative = (char *)malloc(length);
  if (relative == NULL) return NULL;
  /* relative_format carries exactly two %s slots. */
  (void)snprintf(relative, length, relative_format, middle, extension);
  char *path = qa_settings_path(capture->root, relative);
  free(relative);
  return path;
}

/* settingsPath(root, joinFolder(naming.directory, `${stem}.${extension}`)). */
static char *naming_path(const qa_frame_capture *capture, const char *stem, const char *extension) {
  const char *directory = capture->naming.directory;
  size_t length = strlen(directory) + 1 + strlen(stem) + 1 + strlen(extension) + 1;
  char *relative = (char *)malloc(length);
  if (relative == NULL) return NULL;
  if (directory[0] == '\0') (void)snprintf(relative, length, "%s.%s", stem, extension);
  else (void)snprintf(relative, length, "%s/%s.%s", directory, stem, extension);
  char *path = qa_settings_path(capture->root, relative);
  free(relative);
  return path;
}

typedef struct shot_scratch {
  uint8_t *bytes;
  char *candidate;
} shot_scratch;

static void shot_scratch_release(void *raw) {
  shot_scratch *scratch = (shot_scratch *)raw;
  free(scratch->candidate);
  free(scratch->bytes);
}

bool qa_frame_capture_screenshot(qa_frame_capture *capture,
                                 qa_screenshot_format format, const char *name,
                                 int64_t quality, qa_capture_result *out) {
  memset(out, 0, sizeof *out);
  const char *extension = format_extension(format);
  qa_image_level image = capture->readback.read_rgba(capture->readback.ctx);
  size_t byte_length = 0;
  uint8_t *bytes = qa_encode_screenshot(&image, format, quality, &byte_length);
  if (bytes == NULL) return false;
  char *path = NULL;
  if (name != NULL) {
    path = naming_path(capture, name, extension);
    if (path == NULL) {
      free(bytes);
      return false;
    }
    if (!qa_write_atomic(path, bytes, byte_length)) {
      /* TS writeAtomic rejects with the Node fs Error, which the caller sees. */
      free(path);
      free(bytes);
      qa_settings_raise_write_failure();
    }
  } else {
    /* TS nextShot: the first unnamed index to try per format; the directory
     * is made once, on that first index. */
    const int start = (int)capture->next_shot[format];
    /* TS limit: the original Quake 3 stops at shot9999; the Quake and Quake II
     * names only pad to two digits and keep counting. */
    int limit = 1000000;
    if (capture->naming.digits >= 4) {
      double power = 1;
      for (int digit = 0; digit < capture->naming.digits; digit++) power *= 10;
      limit = power > 2147483647.0 ? 2147483647 : (int)power;
    }
    for (int index = start; index < limit && path == NULL; index++) {
      /* `${prefix}${String(index).padStart(digits, "0")}` */
      size_t stem_length = strlen(capture->naming.prefix) + 16 +
                           (capture->naming.digits > 0 ? (size_t)capture->naming.digits : 0);
      char *stem = (char *)malloc(stem_length);
      if (stem == NULL) {
        free(bytes);
        return false;
      }
      (void)snprintf(stem, stem_length, "%s%0*d", capture->naming.prefix,
                     capture->naming.digits > 0 ? capture->naming.digits : 1, index);
      char *candidate = naming_path(capture, stem, extension);
      free(stem);
      if (candidate == NULL) {
        free(bytes);
        return false;
      }
      /* The file-system failures reject with Bun's Error; the buffers are
       * released on the way out. */
      shot_scratch scratch = {bytes, candidate};
      qa_cleanup guard;
      qa_cleanup_push(&guard, shot_scratch_release, &scratch);
      if (index == start) {
        const char *slash = strrchr(candidate, '/');
        char *dir = strndup(candidate, (size_t)(slash - candidate));
        if (dir == NULL) capture_error("out of memory");
        qa_cleanup dir_guard;
        qa_cleanup_push(&dir_guard, free, dir);
        make_directories(dir);
        qa_cleanup_pop(&dir_guard);
        free(dir);
      }
      int fd = open(candidate, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
      if (fd < 0) {
        int error = errno;
        if (error != EEXIST) qa_content_fs_error(error, "open", candidate);
        qa_cleanup_pop(&guard);
        free(candidate);
        continue;
      }
      size_t written = 0;
      int write_error = 0;
      while (written < byte_length) {
        ssize_t chunk = write(fd, bytes + written, byte_length - written);
        if (chunk < 0 && errno == EINTR) continue;
        if (chunk <= 0) {
          write_error = chunk < 0 ? errno : EIO;
          break;
        }
        written += (size_t)chunk;
      }
      /* TS try { writeFile } finally { close } */
      int close_error = close(fd) != 0 ? errno : 0;
      if (write_error != 0) qa_content_fs_error(write_error, "write", NULL);
      if (close_error != 0) qa_content_fs_error(close_error, "close", NULL);
      qa_cleanup_pop(&guard);
      path = candidate;
      capture->next_shot[format] = index + 1;
    }
    if (path == NULL) {
      free(bytes);
      char nines[128];
      size_t nine_count = capture->naming.digits > 0 ? (size_t)capture->naming.digits : 0;
      if (nine_count > sizeof nines - 1) nine_count = sizeof nines - 1;
      memset(nines, '9', nine_count);
      nines[nine_count] = '\0';
      capture_error("No free screenshot filename after %s%s", capture->naming.prefix, nines);
    }
  }
  free(bytes);
  out->path = path;
  out->width = image.width;
  out->height = image.height;
  out->byte_length = (double)byte_length;
  return true;
}

bool qa_frame_capture_levelshot(qa_frame_capture *capture, const char *map_name,
                                const uint8_t *gamma, size_t gamma_length,
                                qa_capture_result *out) {
  memset(out, 0, sizeof *out);
  const char *map = map_name;
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  size_t map_length = strlen(map);
  if (map_length >= 4 && strcmp(map + map_length - 4, ".bsp") == 0) map_length -= 4;
  char *stripped = strndup(map, map_length);
  if (stripped == NULL) return false;
  qa_image_level frame = capture->readback.read_rgba(capture->readback.ctx);
  qa_image_level image;
  if (!qa_make_levelshot(&frame, gamma, gamma_length, &image)) {
    free(stripped);
    return false;
  }
  size_t byte_length = 0;
  uint8_t *bytes = qa_encode_tga(&image, &byte_length);
  free((uint8_t *)image.pixels.bytes);
  if (bytes == NULL) {
    free(stripped);
    return false;
  }
  char *path = capture_path(capture, "levelshots/%s.%s", stripped, "tga");
  free(stripped);
  if (path == NULL) {
    free(bytes);
    return false;
  }
  if (!qa_write_atomic(path, bytes, byte_length)) {
    /* TS writeAtomic rejects with the Node fs Error, which the caller sees. */
    free(path);
    free(bytes);
    qa_settings_raise_write_failure();
  }
  free(bytes);
  out->path = path;
  out->width = image.width;
  out->height = image.height;
  out->byte_length = (double)byte_length;
  return true;
}
