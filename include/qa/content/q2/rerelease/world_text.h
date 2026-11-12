/* Port of src/content/q2/rerelease/world-text.ts. GPL-2.0-or-later.
 * The TS iterates UTF-16 units and masks charCodeAt & 255.
 * qa_q2_world_text_utf16 takes explicit UTF-16 units; the char* form
 * takes a byte string whose bytes are the TS characters (BSP entity
 * text and guest memory are read one byte per character). The result
 * text is UTF-8: up to 127 characters U+0000..U+00FF, so out_text
 * needs 255 bytes to hold any text whole; returned
 * qa_world_text.text points at it. angles NULL = billboard. */
#ifndef QA_CONTENT_Q2_RERELEASE_WORLD_TEXT_H
#define QA_CONTENT_Q2_RERELEASE_WORLD_TEXT_H
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/text/world.h"
qa_world_text qa_q2_world_text_utf16(qa_vec3 origin, const qa_vec3 *angles,
                                     const uint16_t *text, size_t text_length,
                                     qa_vec4 color, double size, bool depth_test,
                                     char *out_text, size_t out_cap);
qa_world_text qa_q2_world_text(qa_vec3 origin, const qa_vec3 *angles, const char *text,
                               qa_vec4 color, double size, bool depth_test,
                               char *out_text, size_t out_cap);
#endif
