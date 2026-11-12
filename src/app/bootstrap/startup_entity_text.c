/* See include/qa/app/bootstrap/startup_entity_text.h. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_entity_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/text_decoder.h"
#include "qa/foundation/support.h"

/* ---- decoded text as UTF-16 code units ------------------------------------------------------------------- */
typedef struct units {
  uint16_t *data;
  size_t length, capacity;
} units;
static void units_push(units *u, uint16_t unit) {
  if (u->length == u->capacity) {
    size_t capacity = u->capacity != 0 ? u->capacity * 2 : 16;
    uint16_t *grown = realloc(u->data, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    u->data = grown;
    u->capacity = capacity;
  }
  u->data[u->length++] = unit;
}
/* qa_text_decoder_decode's UTF-8 (valid, U+0000 as C0 80) as the code units of the TS string. */
static void units_from_decoded(units *out, const char *text, size_t length) {
  const unsigned char *s = (const unsigned char *)text;
  for (size_t at = 0; at < length;) {
    unsigned char c = s[at];
    uint32_t cp;
    if (c < 0x80) { cp = c; at += 1; }
    else if (c == 0xc0 && at + 1 < length && s[at + 1] == 0x80) { cp = 0; at += 2; }
    else if (c < 0xe0 && at + 1 < length) { cp = ((uint32_t)(c & 0x1f) << 6) | (s[at + 1] & 0x3f); at += 2; }
    else if (c < 0xf0 && at + 2 < length) {
      cp = ((uint32_t)(c & 0x0f) << 12) | ((uint32_t)(s[at + 1] & 0x3f) << 6) | (s[at + 2] & 0x3f);
      at += 3;
    } else if (at + 3 < length) {
      cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[at + 1] & 0x3f) << 12) | ((uint32_t)(s[at + 2] & 0x3f) << 6) |
           (s[at + 3] & 0x3f);
      at += 4;
    } else { cp = 0xfffd; at = length; }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      units_push(out, (uint16_t)(0xd800 | (cp >> 10)));
      units_push(out, (uint16_t)(0xdc00 | (cp & 0x3ff)));
    } else units_push(out, (uint16_t)cp);
  }
}
/* A slice of units as malloc'd UTF-8: a surrogate pair is one code point, a lone surrogate its three-byte form, U+0000
 * the bytes C0 80. */
static char *units_text(const uint16_t *u, size_t length) {
  char *out = malloc(length * 3 + 1);
  if (out == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < length; i++) {
    uint32_t cp = u[i];
    if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < length && u[i + 1] >= 0xdc00 && u[i + 1] <= 0xdfff) {
      cp = 0x10000 + ((cp - 0xd800) << 10) + (u[i + 1] - 0xdc00);
      i++;
    }
    if (cp == 0) { out[at++] = (char)0xc0; out[at++] = (char)0x80; }
    else if (cp < 0x80) out[at++] = (char)cp;
    else if (cp < 0x800) { out[at++] = (char)(0xc0 | (cp >> 6)); out[at++] = (char)(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) {
      out[at++] = (char)(0xe0 | (cp >> 12));
      out[at++] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[at++] = (char)(0x80 | (cp & 0x3f));
    } else {
      out[at++] = (char)(0xf0 | (cp >> 18));
      out[at++] = (char)(0x80 | ((cp >> 12) & 0x3f));
      out[at++] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[at++] = (char)(0x80 | (cp & 0x3f));
    }
  }
  out[at] = '\0';
  return out;
}
static bool units_equal(const uint16_t *u, size_t length, const char *ascii) {
  size_t n = strlen(ascii);
  if (n != length) return false;
  for (size_t i = 0; i < n; i++)
    if (u[i] != (unsigned char)ascii[i]) return false;
  return true;
}

/* ---- owned parse state, released when a parse error unwinds ------------------------------------------------ */
typedef struct parse_state {
  units text;
  units token, key, value; /* the current token, entity key and value */
  units classname; bool has_classname;
  char **names; size_t count, capacity;
  char *scratch; /* a message argument */
} parse_state;
static void state_release(void *raw) {
  parse_state *state = raw;
  free(state->text.data);
  free(state->token.data);
  free(state->key.data);
  free(state->value.data);
  free(state->classname.data);
  free(state->scratch);
  for (size_t i = 0; i < state->count; i++) free(state->names[i]);
  free(state->names);
  memset(state, 0, sizeof *state);
}
static void units_set(units *to, const uint16_t *from, size_t length) {
  to->length = 0;
  for (size_t i = 0; i < length; i++) units_push(to, from[i]);
}
/* Ends one entity: its classname, or "" when it has none. */
static void push_classname(parse_state *state) {
  char *name = state->has_classname ? units_text(state->classname.data, state->classname.length) : NULL;
  if (name == NULL) {
    name = malloc(1);
    if (name == NULL) qa_range_error("Out of memory");
    name[0] = '\0';
  }
  if (state->count == state->capacity) {
    size_t capacity = state->capacity != 0 ? state->capacity * 2 : 16;
    char **grown = realloc(state->names, capacity * sizeof *grown);
    if (grown == NULL) { free(name); qa_range_error("Out of memory"); }
    state->names = grown;
    state->capacity = capacity;
  }
  state->names[state->count++] = name;
  state->has_classname = false;
  state->classname.length = 0;
}
/* Raises Error(prefix + key text + suffix) (the key text is UTF-8 of the units). */
static _Noreturn void raise_with_units(parse_state *state, const char *name, const char *format_before, const units *u,
                                       const char *after) {
  free(state->scratch);
  state->scratch = units_text(u->data, u->length);
  qa_raise(name, 0, 0, "%s%s%s", format_before, state->scratch, after);
}

/* ---- parseQ1Entities (formats/q1-map/entities.ts) ---------------------------------------------------------- */
typedef enum { Q1_EOF, Q1_OPEN, Q1_CLOSE, Q1_TEXT } q1_kind;
static q1_kind q1_token(parse_state *state, size_t *position) {
  const uint16_t *t = state->text.data;
  size_t n = state->text.length, p = *position;
  while (p < n) {
    uint16_t c = t[p];
    if (c == 0) { *position = p; return Q1_EOF; }
    if (c <= ' ') { p++; continue; }
    if (c == '/' && p + 1 < n && t[p + 1] == '/') {
      size_t end = p + 2;
      while (end < n && t[end] != '\n') end++;
      p = end < n ? end + 1 : n;
      continue;
    }
    break;
  }
  if (p >= n) { *position = p; return Q1_EOF; }
  uint16_t first = t[p++];
  if (first == '{') { *position = p; return Q1_OPEN; }
  if (first == '}') { *position = p; return Q1_CLOSE; }
  if (first == '"') {
    size_t start = p;
    while (p < n && t[p] != '"' && t[p] != 0) p++;
    *position = p;
    if (p >= n || t[p] == 0) qa_raise("Error", 0, 0, "Entity text:%zu: unterminated quote", start);
    units_set(&state->token, t + start, p - start);
    *position = p + 1;
    return Q1_TEXT;
  }
  size_t start = p - 1;
  while (p < n && t[p] > ' ' && t[p] != '{' && t[p] != '}') p++;
  units_set(&state->token, t + start, p - start);
  *position = p;
  return Q1_TEXT;
}
static void parse_q1(parse_state *state) {
  size_t position = 0;
  for (q1_kind first = q1_token(state, &position); first != Q1_EOF; first = q1_token(state, &position)) {
    if (first != Q1_OPEN) qa_raise("Error", 0, 0, "Entity text:%zu: expected opening brace", position);
    for (;;) {
      q1_kind key = q1_token(state, &position);
      if (key == Q1_CLOSE) break;
      if (key != Q1_TEXT) qa_raise("Error", 0, 0, "Entity text:%zu: expected key or closing brace", position);
      units_set(&state->key, state->token.data, state->token.length);
      q1_kind value = q1_token(state, &position);
      if (value != Q1_TEXT) {
        char at[96];
        snprintf(at, sizeof at, "Entity text:%zu: expected value for ", position);
        raise_with_units(state, "Error", at, &state->key, "");
      }
      /* q1EntityValue: the last "classname" property wins. */
      if (units_equal(state->key.data, state->key.length, "classname")) {
        units_set(&state->classname, state->token.data, state->token.length);
        state->has_classname = true;
      }
    }
    push_classname(state);
  }
}

/* ---- parseQ2Entities (content/q2/foundation/fields.ts) over parseQ2Token (core/common-parse.ts) ------------- */
static uint16_t char_at(const units *u, size_t index) { return index < u->length ? u->data[index] : 0; }
/* parseQ2Token(state, limit) into state->token. */
static void q2_parse_token(parse_state *state, size_t *index, size_t limit) {
  const units *s = &state->text;
  size_t idx = *index, len = 0;
  state->token.length = 0;
  uint16_t c;
  for (;;) {
    c = char_at(s, idx);
    while (c <= 32) {
      if (c == 0) { *index = idx; return; }
      idx++;
      c = char_at(s, idx);
    }
    if (c == '/' && char_at(s, idx + 1) == '/') {
      while (char_at(s, idx) != 0 && char_at(s, idx) != 10) idx++;
      continue;
    }
    break;
  }
  c = char_at(s, idx);
  if (c == '"') {
    idx++;
    for (;;) {
      c = char_at(s, idx);
      idx++;
      if (c == '"' || c == 0) { *index = idx; return; }
      if (len < limit) { units_push(&state->token, c); len++; }
    }
  }
  do {
    if (len < limit) { units_push(&state->token, c); len++; }
    idx++;
    c = char_at(s, idx);
  } while (c > 32);
  if (len == limit) state->token.length = 0;
  *index = idx;
}
/* entityToken: false is { kind: "eof" }; true leaves the token in state->token. */
static bool q2_entity_token(parse_state *state, size_t *index, size_t limit) {
  const uint16_t *d = state->text.data;
  size_t n = state->text.length, i = *index;
  for (;;) {
    while (i < n && d[i] > 0 && d[i] <= 32) i++;
    if (i >= n || d[i] == 0) return false;
    if (d[i] != '/' || i + 1 >= n || d[i + 1] != '/') break;
    while (i < n && d[i] != 0 && d[i] != '\n') i++;
  }
  q2_parse_token(state, index, limit);
  return true;
}
static void parse_q2(parse_state *state, bool rerelease) {
  size_t index = 0, limit = rerelease ? 512 : 128;
  for (;;) {
    if (!q2_entity_token(state, &index, limit)) break;
    size_t ordinal = state->count;
    if (!units_equal(state->token.data, state->token.length, "{"))
      qa_raise("Error", 0, 0, "Q2 entity %zu: expected opening brace", ordinal);
    for (;;) {
      if (!q2_entity_token(state, &index, limit)) qa_raise("Error", 0, 0, "Q2 entity %zu: EOF without closing brace", ordinal);
      if (units_equal(state->token.data, state->token.length, "}")) break;
      units_set(&state->key, state->token.data, state->token.length);
      bool has_value = q2_entity_token(state, &index, limit);
      if (!has_value || units_equal(state->token.data, state->token.length, "}")) {
        char at[96];
        snprintf(at, sizeof at, "Q2 entity %zu: missing value for ", ordinal);
        raise_with_units(state, "Error", at, &state->key, "");
      }
      if (units_equal(state->key.data, state->key.length, "classname")) {
        /* entityString: /\\(.)/gs, backslash-n to LF and any other escaped unit to one backslash. */
        state->classname.length = 0;
        const uint16_t *v = state->token.data;
        size_t length = state->token.length;
        for (size_t i = 0; i < length; i++) {
          if (v[i] == '\\' && i + 1 < length) {
            units_push(&state->classname, v[i + 1] == 'n' ? '\n' : '\\');
            i++;
          } else units_push(&state->classname, v[i]);
        }
        state->has_classname = true;
      }
    }
    push_classname(state);
  }
}

/* ---- parseEntities (formats/q3-map/entities.ts) ---------------------------------------------------------- */
typedef struct q3_tokenizer {
  const char *name;
  size_t offset, line, column;
} q3_tokenizer;
typedef struct q3_token { size_t line, column; } q3_token;
static bool q3_starts_with(const parse_state *state, const q3_tokenizer *t, const char *prefix) {
  size_t n = strlen(prefix);
  if (t->offset + n > state->text.length) return false;
  for (size_t i = 0; i < n; i++)
    if (state->text.data[t->offset + i] != (unsigned char)prefix[i]) return false;
  return true;
}
static void q3_advance(const parse_state *state, q3_tokenizer *t) {
  if (t->offset >= state->text.length) return;
  uint16_t c = state->text.data[t->offset];
  if (c == '\r') {
    t->offset++;
    if (t->offset < state->text.length && state->text.data[t->offset] == '\n') t->offset++;
    t->line++;
    t->column = 1;
    return;
  }
  t->offset++;
  if (c == '\n') { t->line++; t->column = 1; }
  else t->column++;
}
static _Noreturn void q3_error(const q3_tokenizer *t, size_t line, size_t column, const char *message) {
  qa_raise("TextParseError", 0, 0, "%s:%zu:%zu: %s", t->name, line, column, message);
}
/* Tokenizer.next(allowLineBreaks): false is undefined; true leaves the value in state->token. */
static bool q3_next(parse_state *state, q3_tokenizer *t, bool allow_line_breaks, q3_token *out) {
  const uint16_t *s = state->text.data;
  size_t n = state->text.length;
  bool crossed = false;
  for (;;) {
    while (t->offset < n) {
      uint16_t c = s[t->offset];
      if (c > 32) break;
      if (c == '\n' || c == '\r') crossed = true;
      q3_advance(state, t);
    }
    if (q3_starts_with(state, t, "//")) {
      q3_advance(state, t);
      q3_advance(state, t);
      while (t->offset < n) {
        uint16_t c = s[t->offset];
        if (c == '\n' || c == '\r') break;
        q3_advance(state, t);
      }
      continue;
    }
    if (q3_starts_with(state, t, "/*")) {
      q3_advance(state, t);
      q3_advance(state, t);
      while (t->offset < n && !q3_starts_with(state, t, "*/")) {
        uint16_t c = s[t->offset];
        if (c == '\n' || c == '\r') crossed = true;
        q3_advance(state, t);
      }
      if (q3_starts_with(state, t, "*/")) {
        q3_advance(state, t);
        q3_advance(state, t);
      }
      continue;
    }
    break;
  }
  if (!allow_line_breaks && crossed) return false;
  if (t->offset >= n) return false;
  out->line = t->line;
  out->column = t->column;
  state->token.length = 0;
  if (s[t->offset] == '"') {
    q3_advance(state, t);
    while (t->offset < n) {
      uint16_t c = s[t->offset];
      if (c == '"') break;
      units_push(&state->token, c);
      q3_advance(state, t);
    }
    if (t->offset < n && s[t->offset] == '"') q3_advance(state, t);
  } else {
    while (t->offset < n) {
      uint16_t c = s[t->offset];
      if (c <= 32) break;
      units_push(&state->token, c);
      q3_advance(state, t);
    }
  }
  if (state->token.length >= 1024) q3_error(t, out->line, out->column, "token is limited to 1023 characters");
  return true;
}
static _Noreturn void q3_missing_value(parse_state *state, const q3_tokenizer *t, size_t line, size_t column) {
  free(state->scratch);
  state->scratch = units_text(state->key.data, state->key.length);
  qa_raise("TextParseError", 0, 0, "%s:%zu:%zu: missing value for entity key \"%s\"", t->name, line, column, state->scratch);
}
static void parse_q3(parse_state *state, const char *name) {
  q3_tokenizer t = {name != NULL ? name : "<entities>", 0, 1, 1};
  for (;;) {
    q3_token opening;
    if (!q3_next(state, &t, true, &opening)) return;
    if (!units_equal(state->token.data, state->token.length, "{")) q3_error(&t, opening.line, opening.column, "expected \"{\"");
    for (;;) {
      q3_token key;
      if (!q3_next(state, &t, true, &key)) q3_error(&t, t.line, t.column, "expected key or \"}\"");
      if (units_equal(state->token.data, state->token.length, "}")) {
        push_classname(state);
        break;
      }
      units_set(&state->key, state->token.data, state->token.length);
      q3_token value;
      if (!q3_next(state, &t, false, &value)) q3_missing_value(state, &t, t.line, t.column);
      if (units_equal(state->token.data, state->token.length, "}")) q3_missing_value(state, &t, value.line, value.column);
      /* entity.set(key, value): the Map keeps the last value. */
      if (units_equal(state->key.data, state->key.length, "classname")) {
        units_set(&state->classname, state->token.data, state->token.length);
        state->has_classname = true;
      }
    }
  }
}

char **qa_app_entity_classnames(const uint8_t *bytes, size_t length, qa_app_entity_grammar grammar, bool rerelease,
                                const char *name, bool strip_trailing_nuls, size_t *out_count) {
  parse_state state;
  memset(&state, 0, sizeof state);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, state_release, &state);
  size_t decoded_length = 0;
  char *decoded = qa_text_decoder_decode(bytes, length, &decoded_length);
  if (decoded == NULL) qa_range_error("Out of memory");
  state.scratch = decoded; /* released with the state if the conversion raises */
  units_from_decoded(&state.text, decoded, decoded_length);
  free(decoded);
  state.scratch = NULL;
  if (strip_trailing_nuls)
    while (state.text.length > 0 && state.text.data[state.text.length - 1] == 0) state.text.length--;
  if (grammar == QA_APP_ENTITY_GRAMMAR_Q1) parse_q1(&state);
  else if (grammar == QA_APP_ENTITY_GRAMMAR_Q2) parse_q2(&state, rerelease);
  else parse_q3(&state, name);
  char **names = state.names;
  size_t count = state.count;
  state.names = NULL;
  state.count = 0;
  qa_cleanup_pop(&cleanup);
  state_release(&state);
  if (names == NULL) {
    names = malloc(sizeof *names);
    if (names == NULL) qa_range_error("Out of memory");
  }
  *out_count = count;
  return names;
}

void qa_app_entity_classnames_free(char **names, size_t count) {
  if (names == NULL) return;
  for (size_t i = 0; i < count; i++) free(names[i]);
  free(names);
}
