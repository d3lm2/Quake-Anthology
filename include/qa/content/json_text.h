/*
 * JSON.parse(new TextDecoder(...).decode(bytes)) for the content readers.
 * There is no corresponding .ts file: this is the pair of built-ins those
 * readers call. The WHATWG UTF-8 decoder drops a leading byte order mark;
 * with { fatal: true } invalid UTF-8 is Bun's TypeError. A JSON syntax error
 * raises as qa_parse_json_value does. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_JSON_TEXT_H
#define QA_CONTENT_JSON_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

/* WHATWG UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF. */
static inline bool qa_content_utf8_valid(const uint8_t *bytes, size_t length) {
  for (size_t i = 0; i < length;) {
    uint8_t c = bytes[i];
    if (c < 0x80) { i++; continue; }
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
    } else return false;
    if (length - i <= need) return false;
    if (bytes[i + 1] < low || bytes[i + 1] > high) return false;
    for (size_t k = 2; k <= need; k++)
      if ((bytes[i + k] & 0xc0) != 0x80) return false;
    i += need + 1;
  }
  return true;
}

/* JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes)). */
static inline qa_save_value *qa_content_parse_json_fatal(const uint8_t *bytes, size_t length) {
  if (length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
    bytes += 3;
    length -= 3;
  }
  if (!qa_content_utf8_valid(bytes, length))
    qa_raise("TypeError", 0, 0, "%s", "Invalid byte sequence"); /* Bun 1.3's text */
  return qa_parse_json_value(bytes, length, false);
}

/* JSON.parse(new TextDecoder().decode(bytes)): invalid sequences decode to
 * U+FFFD, which only JSON strings can hold; the bytes are kept as they are. */
static inline qa_save_value *qa_content_parse_json_lenient(const uint8_t *bytes, size_t length) {
  if (length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
    bytes += 3;
    length -= 3;
  }
  return qa_parse_json_value(bytes, length, false);
}

#endif
