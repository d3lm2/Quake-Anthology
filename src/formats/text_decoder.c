/* WHATWG UTF-8 decode (TextDecoder, non-fatal). See include/qa/formats/text_decoder.h.
 * GPL-2.0-or-later. */
#include "qa/formats/text_decoder.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *qa_text_decoder_decode(const uint8_t *bytes, size_t length, size_t *out_length) {
  size_t at = 0;
  if (length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) at = 3;
  /* Each input byte yields at most one U+FFFD (3 bytes). */
  if (length > (SIZE_MAX - 1) / 3) return NULL;
  char *out = malloc((length - at) * 3 + 1);
  if (out == NULL) return NULL;
  size_t written = 0;
  static const char replacement[3] = {(char)0xef, (char)0xbf, (char)0xbd};
  while (at < length) {
    uint8_t c = bytes[at];
    if (c == 0) {
      /* U+0000 as C0 80 (see the header): the C string stays whole. */
      out[written++] = (char)0xc0;
      out[written++] = (char)0x80;
      at++;
      continue;
    }
    if (c < 0x80) {
      out[written++] = (char)c;
      at++;
      continue;
    }
    size_t need;
    uint8_t low = 0x80, high = 0xbf;
    if (c >= 0xc2 && c <= 0xdf) need = 1;
    else if (c >= 0xe0 && c <= 0xef) {
      need = 2;
      if (c == 0xe0) low = 0xa0;
      if (c == 0xed) high = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
      need = 3;
      if (c == 0xf0) low = 0x90;
      if (c == 0xf4) high = 0x8f;
    } else {
      memcpy(out + written, replacement, 3);
      written += 3;
      at++;
      continue;
    }
    /* The maximal subpart ends at the first byte outside its bound or at the
       end of input; that byte (not consumed) starts the next sequence. */
    size_t next = at + 1;
    bool complete = true;
    for (size_t k = 1; k <= need; k++, next++) {
      if (next >= length) {
        complete = false;
        break;
      }
      uint8_t byte = bytes[next];
      uint8_t lower = k == 1 ? low : 0x80, upper = k == 1 ? high : 0xbf;
      if (byte < lower || byte > upper) {
        complete = false;
        break;
      }
    }
    if (!complete) {
      memcpy(out + written, replacement, 3);
      written += 3;
      at = next;
      continue;
    }
    memcpy(out + written, bytes + at, next - at);
    written += next - at;
    at = next;
  }
  out[written] = '\0';
  if (out_length != NULL) *out_length = written;
  return out;
}
