/*
 * Port of src/formats/images/limits.ts: decode limits shared by the image
 * decoders. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_LIMITS_H
#define QA_FORMATS_IMAGES_LIMITS_H

#include <stdbool.h>

/* Decoded RGBA bytes a multi-frame image may retain across all its frames. */
#define QA_MAX_ANIMATION_BYTES (256.0 * 1024.0 * 1024.0)
#define QA_MAX_ANIMATION_FRAMES 4096

/* TS imageSizeAllowed: safe positive integers whose decoded RGBA bytes stay
 * addressable by a signed 32-bit index (doubles, so any TS number can be passed). */
bool qa_image_size_allowed(double width, double height);

#endif
