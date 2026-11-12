/* See header. GPL-2.0-or-later. */
#include "qa/main_redaction.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char REDACTED[] = "<redacted>";
static const char *const KEYWORDS[] = {"password", "secret", "token", "apikey", "api_key"};
static const char *const PREFIXES[] = {"rcon_", "mvd_", "spectator_", "sv_", "cl_"};
#define COUNT(a) (sizeof(a) / sizeof *(a))

static unsigned char fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c; }
/* Case-insensitive (ASCII, as the non-unicode `i` flag) literal at `at`. */
static bool literal_at(const char *text, size_t length, size_t at, const char *word) {
  size_t n = strlen(word);
  if (at > length || length - at < n) return false;
  for (size_t i = 0; i < n; i++)
    if (fold((unsigned char)text[at + i]) != (unsigned char)word[i]) return false;
  return true;
}
static bool word_byte(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
/* \b: one side a word character and the other not (text ends count as non-word). */
static bool boundary(const char *text, size_t length, size_t at) {
  bool before = at > 0 && word_byte((unsigned char)text[at - 1]);
  bool after = at < length && word_byte((unsigned char)text[at]);
  return before != after;
}
/* Byte length of the JS \s character at `at` (UTF-8), 0 when it is not one. */
static size_t space_at(const char *text, size_t length, size_t at) {
  if (at >= length) return 0;
  const unsigned char *p = (const unsigned char *)text + at;
  size_t left = length - at;
  if (*p == ' ' || (*p >= '\t' && *p <= '\r')) return 1;
  if (left >= 2 && p[0] == 0xc2 && p[1] == 0xa0) return 2; /* U+00A0 */
  if (left >= 3) {
    if (p[0] == 0xe1 && p[1] == 0x9a && p[2] == 0x80) return 3; /* U+1680 */
    if (p[0] == 0xe2 && p[1] == 0x80 && ((p[2] >= 0x80 && p[2] <= 0x8a) || p[2] == 0xa8 || p[2] == 0xa9 || p[2] == 0xaf))
      return 3; /* U+2000-U+200A, U+2028, U+2029, U+202F */
    if (p[0] == 0xe2 && p[1] == 0x81 && p[2] == 0x9f) return 3; /* U+205F */
    if (p[0] == 0xe3 && p[1] == 0x80 && p[2] == 0x80) return 3; /* U+3000 */
    if (p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) return 3; /* U+FEFF */
  }
  return 0;
}
/* The positions \s* can stop at from `at`, shortest first (malloc'd; NULL on allocation failure). */
static size_t *space_run(const char *text, size_t length, size_t at, size_t *out_count) {
  size_t capacity = 8, n = 0;
  size_t *stops = malloc(capacity * sizeof *stops);
  if (stops == NULL) return NULL;
  stops[n++] = at;
  for (size_t step; (step = space_at(text, length, at)) != 0; at += step) {
    if (n == capacity) {
      size_t *grown = realloc(stops, (capacity *= 2) * sizeof *stops);
      if (grown == NULL) { free(stops); return NULL; }
      stops = grown;
    }
    stops[n++] = at + step;
  }
  *out_count = n;
  return stops;
}
/* ("[^"\n]*"|\S+) at `at`: the match end, or 0 (a match is never empty and never starts at 0 here). */
static size_t final_value(const char *text, size_t length, size_t at) {
  if (at < length && text[at] == '"') {
    size_t q = at + 1;
    while (q < length && text[q] != '"' && text[q] != '\n') q++;
    if (q < length && text[q] == '"') return q + 1;
  }
  size_t q = at;
  while (q < length && space_at(text, length, q) == 0) q++;
  return q > at ? q : 0;
}
/* \s*(value) from `at`, longest whitespace first. -1 on allocation failure. */
static int second_from(const char *text, size_t length, size_t at, size_t *group_end, size_t *match_end) {
  size_t count;
  size_t *stops = space_run(text, length, at, &count);
  if (stops == NULL) return -1;
  int found = 0;
  for (size_t i = count; i-- > 0 && !found;) {
    size_t end = final_value(text, length, stops[i]);
    if (end != 0) { *group_end = stops[i]; *match_end = end; found = 1; }
  }
  free(stops);
  return found;
}
/* (?:is\s*:?|=)?\s*(value) from `p`: "is" (each \s* stop longest first, ":" then none), "=", empty. */
static int middle_from(const char *text, size_t length, size_t p, size_t *group_end, size_t *match_end) {
  int found = 0;
  if (literal_at(text, length, p, "is")) {
    size_t count;
    size_t *stops = space_run(text, length, p + 2, &count);
    if (stops == NULL) return -1;
    for (size_t i = count; i-- > 0 && found == 0;) {
      if (stops[i] < length && text[stops[i]] == ':') found = second_from(text, length, stops[i] + 1, group_end, match_end);
      if (found == 0) found = second_from(text, length, stops[i], group_end, match_end);
    }
    free(stops);
    if (found != 0) return found;
  }
  if (p < length && text[p] == '=') {
    found = second_from(text, length, p + 1, group_end, match_end);
    if (found != 0) return found;
  }
  return second_from(text, length, p, group_end, match_end);
}
/* Everything after the keyword: "?\s*(?:is\s*:?|=)?\s*(value), in the regex's backtracking order. */
static int tail_from(const char *text, size_t length, size_t at, size_t *group_end, size_t *match_end) {
  size_t quote_options[2], quote_count = 0;
  if (at < length && text[at] == '"') quote_options[quote_count++] = at + 1;
  quote_options[quote_count++] = at;
  for (size_t qi = 0; qi < quote_count; qi++) {
    size_t count;
    size_t *stops = space_run(text, length, quote_options[qi], &count);
    if (stops == NULL) return -1;
    int found = 0;
    for (size_t i = count; i-- > 0 && found == 0;) found = middle_from(text, length, stops[i], group_end, match_end);
    free(stops);
    if (found != 0) return found;
  }
  return 0;
}
static int match_at(const char *text, size_t length, size_t at, size_t *group_end, size_t *match_end) {
  if (!boundary(text, length, at)) return 0;
  size_t starts[COUNT(PREFIXES) + 1], start_count = 0;
  for (size_t i = 0; i < COUNT(PREFIXES); i++)
    if (literal_at(text, length, at, PREFIXES[i])) starts[start_count++] = at + strlen(PREFIXES[i]);
  starts[start_count++] = at;
  for (size_t si = 0; si < start_count; si++)
    for (size_t k = 0; k < COUNT(KEYWORDS); k++) {
      if (!literal_at(text, length, starts[si], KEYWORDS[k])) continue;
      size_t after = starts[si] + strlen(KEYWORDS[k]);
      if (!boundary(text, length, after)) continue;
      int found = tail_from(text, length, after, group_end, match_end);
      if (found != 0) return found;
    }
  return 0;
}
const char **qa_redact_arguments(size_t count, const char *const *argv) {
  const char **out = malloc((count ? count : 1) * sizeof *out);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    bool secret = false;
    if (i > 0) {
      const char *previous = argv[i - 1];
      size_t length = strlen(previous);
      for (size_t at = 0; at < length && !secret; at++)
        for (size_t k = 0; k < COUNT(KEYWORDS) && !secret; k++) secret = literal_at(previous, length, at, KEYWORDS[k]);
    }
    out[i] = secret ? REDACTED : argv[i];
  }
  return out;
}

char *qa_redact_output(const char *text) {
  size_t length = strlen(text), capacity = length + 1, used = 0;
  char *out = malloc(capacity);
  if (out == NULL) return NULL;
  size_t copied = 0;
  for (size_t at = 0; at < length;) {
    size_t group_end, match_end;
    int found = match_at(text, length, at, &group_end, &match_end);
    if (found < 0) { free(out); return NULL; }
    if (found == 0) { at++; continue; }
    size_t keep = group_end - copied, need = used + keep + sizeof REDACTED + (length - match_end) + 1;
    if (need > capacity) {
      char *grown = realloc(out, need);
      if (grown == NULL) { free(out); return NULL; }
      out = grown; capacity = need;
    }
    memcpy(out + used, text + copied, keep);
    used += keep;
    memcpy(out + used, REDACTED, sizeof REDACTED - 1);
    used += sizeof REDACTED - 1;
    copied = at = match_end;
  }
  size_t rest = length - copied;
  if (used + rest + 1 > capacity) {
    char *grown = realloc(out, used + rest + 1);
    if (grown == NULL) { free(out); return NULL; }
    out = grown;
  }
  memcpy(out + used, text + copied, rest);
  out[used + rest] = 0;
  return out;
}
