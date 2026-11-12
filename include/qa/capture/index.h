/*
 * Port of src/capture/index.ts: Q3 screenshot and levelshot capture
 * over owned renderer readbacks. GPL-2.0-or-later.
 * C accommodations:
 *  - Plain TS `Error` throws are qa_raise("Error", ...) with the TS
 *    message (catchable with qa_try; uncaught they print and abort).
 *  - Encoded results are malloc'd byte buffers (caller frees), NULL
 *    only on allocation failure, with *out_length set.
 *  - The TS `quality = 90` default has no C form; callers pass 90
 *    for the default. Quality is int64_t because encodeJpeg requires
 *    an integer (RangeError otherwise).
 *  - qa_make_levelshot's gamma is NULL or a 256-entry table; the
 *    explicit gamma_length reproduces the TS length check. The
 *    result's pixels are malloc'd (caller frees
 *    (uint8_t *)out->pixels.bytes).
 *  - FrameCapture is synchronous: the TS promise write chain
 *    degenerates to direct calls in call order, and async readRgba
 *    to the synchronous callback above. Capture functions return
 *    false only on allocation failure. A file-system failure raises
 *    the Error the TS promise rejects with (writeAtomic's Node fs
 *    Error through qa_settings_raise_write_failure, or the exclusive
 *    open/write/close error of an unnumbered shot), and the TS
 *    "No free screenshot filename..." Error raises like this module's
 *    other plain Errors. qa_frame_capture_init
 *    copies root; release with qa_frame_capture_destroy. Screenshot
 *    options defaults (format "png", quality 90) have no C form;
 *    callers pass QA_SCREENSHOT_PNG / 90, and NULL name for the
 *    auto-numbered form. Levelshot gamma follows
 *    qa_make_levelshot (NULL or 256 entries).
 *  - The TS constructor's `naming = Q3_SCREENSHOTS` default is
 *    qa_frame_capture_init; qa_frame_capture_init_naming takes the
 *    naming and copies its strings.
 */
#ifndef QA_CAPTURE_INDEX_H
#define QA_CAPTURE_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"

/* TS ScreenshotFormat: "tga" | "png" | "jpg". */
typedef enum qa_screenshot_format {
  QA_SCREENSHOT_TGA,
  QA_SCREENSHOT_PNG,
  QA_SCREENSHOT_JPG
} qa_screenshot_format;

/* TS ScreenshotNaming: where an unnamed screenshot goes and what it is
 * called: the original game's folder, prefix and digits. */
typedef struct qa_screenshot_naming {
  const char *directory;
  const char *prefix;
  int digits;
} qa_screenshot_naming;

/* TS FrameReadback: readRgba may be async in TS; the C callback is
 * synchronous and returns a level whose pixels stay owned by the
 * callback's owner (borrowed for the duration of the capture call). */
typedef struct qa_frame_readback {
  void *ctx;
  qa_image_level (*read_rgba)(void *ctx);
} qa_frame_readback;

/* TS CaptureResult. path is an owned string when produced. */
typedef struct qa_capture_result {
  char *path;
  double width;
  double height;
  double byte_length;
} qa_capture_result;

/* TS encodeScreenshot. Malloc'd bytes, caller frees; NULL on OOM
 * (or for an out-of-enum format, where the TS switch falls through). */
uint8_t *qa_encode_screenshot(const qa_image_level *image,
                              qa_screenshot_format format, int64_t quality,
                              size_t *out_length);

/* TS makeLevelshot: 512x384 sampling grid averaged into 128x128.
 * False only on OOM. */
bool qa_make_levelshot(const qa_image_level *image, const uint8_t *gamma,
                       size_t gamma_length, qa_image_level *out);

/* TS FrameCapture. Owns a copy of root (TS readonly root); readback
 * is borrowed (its ctx stays owned by the caller). */
typedef struct qa_frame_capture {
  char *root;
  qa_frame_readback readback;
  /* TS nextShot: first unnamed screenshot index to try, per format. */
  double next_shot[3];
  /* TS naming; the strings are owned copies. */
  qa_screenshot_naming naming;
} qa_frame_capture;
/* False only on OOM. Uses the Quake III naming (screenshots/shotNNNN). */
bool qa_frame_capture_init(qa_frame_capture *capture, const char *root,
                           qa_frame_readback readback);
bool qa_frame_capture_init_naming(qa_frame_capture *capture, const char *root,
                                  qa_frame_readback readback, qa_screenshot_naming naming);
void qa_frame_capture_destroy(qa_frame_capture *capture);

/* TS FrameCapture.screenshot: named writes go through writeAtomic;
 * unnamed writes take the first free numbered name exclusively (the
 * original Quake 3 stops at shot9999; the Quake and Quake II names only
 * pad to two digits and keep counting).
 * On success *out owns its path (qa_capture_result_free); false only
 * on allocation failure; file-system failures raise (see above). */
bool qa_frame_capture_screenshot(qa_frame_capture *capture,
                                 qa_screenshot_format format, const char *name,
                                 int64_t quality, qa_capture_result *out);
/* TS FrameCapture.levelshot: strips a leading "maps/" and trailing
 * ".bsp" from map_name, writes levelshots/<map>.tga atomically. */
bool qa_frame_capture_levelshot(qa_frame_capture *capture, const char *map_name,
                                const uint8_t *gamma, size_t gamma_length,
                                qa_capture_result *out);
void qa_capture_result_free(qa_capture_result *result);

#endif
