/* Port of src/content/q2/rerelease/world-text.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/world_text.h"
#include <string.h>

qa_world_text qa_q2_world_text_utf16(qa_vec3 origin, const qa_vec3 *angles,
                                     const uint16_t *text, size_t text_length,
                                     qa_vec4 color, double size, bool depth_test,
                                     char *out_text, size_t out_cap) {
  /* String.fromCharCode(charCodeAt(index) & 255) for the first 127 units: each result is a
   * character U+0000..U+00FF, written as UTF-8 (the C text form the renderer and the network
   * codecs read). A character that does not fit whole is not written. */
  size_t n = text_length < 127 ? text_length : 127;
  if (out_cap > 0) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
      unsigned code = text[i] & 255u;
      size_t need = code < 0x80 ? 1 : 2;
      if (w + need > out_cap - 1) break;
      if (code < 0x80) out_text[w++] = (char)code;
      else { out_text[w++] = (char)(0xC0 | (code >> 6)); out_text[w++] = (char)(0x80 | (code & 0x3F)); }
    }
    out_text[w] = '\0';
  }
  qa_world_text r;
  memset(&r, 0, sizeof r);
  r.text = out_text;
  r.origin = origin;
  r.color = color;
  r.cell_size = size * 8;
  r.has_distance_cull_factor = true;
  r.distance_cull_factor = 0.004;
  r.orientation_fixed = angles != NULL;
  if (angles) r.angles = *angles;
  r.depth_test = depth_test;
  r.font_selected = false; /* "classic" */
  return r;
}
qa_world_text qa_q2_world_text(qa_vec3 origin, const qa_vec3 *angles, const char *text,
                               qa_vec4 color, double size, bool depth_test,
                               char *out_text, size_t out_cap) {
  /* text is a byte string (BSP entity text read by fixedByteString, guest memory bytes): each
   * byte is one TS character, as String.fromCharCode(byte) built it. */
  uint16_t units[127];
  size_t len = text ? strlen(text) : 0;
  size_t n = len < 127 ? len : 127;
  for (size_t i = 0; i < n; i++) units[i] = (uint16_t)(unsigned char)text[i];
  return qa_q2_world_text_utf16(origin, angles, units, n, color, size, depth_test, out_text, out_cap);
}
