/* Port of src/formats/q1-map/entities.ts. GPL-2.0-or-later. */
#include "qa/formats/q1_map/entities.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}

/* A Quake byte string is Latin-1 text in TS (fixedByteString): a byte past
 * 0x7f is one code unit, two UTF-8 bytes in a message. Caller frees. */
static char *latin1_text(const char *bytes) {
  size_t length = 0;
  for (const unsigned char *p = (const unsigned char *)bytes; *p != 0; p++) length += *p >= 0x80 ? 2 : 1;
  char *text = malloc(length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (const unsigned char *p = (const unsigned char *)bytes; *p != 0; p++) {
    if (*p < 0x80) {
      text[at++] = (char)*p;
    } else {
      text[at++] = (char)(0xc0 | (*p >> 6));
      text[at++] = (char)(0x80 | (*p & 0x3f));
    }
  }
  text[at] = '\0';
  return text;
}

/* Plain TS Error: prints the identical message under the Error name. */
static noreturn void plain_error(const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}

typedef enum { TOKEN_NONE, TOKEN_OPEN, TOKEN_CLOSE, TOKEN_TEXT } token_kind;

typedef struct {
  const char *text;
  size_t length;
  size_t position;
} lexer;

static token_kind next_token(lexer *lex, char **out_value) {
  *out_value = NULL;
  while (lex->position < lex->length) {
    char character = lex->text[lex->position];
    if (character == '\0') return TOKEN_NONE;
    /* TS compares characters: high bytes are above " ", not whitespace. */
    if ((unsigned char)character <= ' ') { lex->position++; continue; }
    if (lex->position + 1 < lex->length && lex->text[lex->position] == '/' &&
        lex->text[lex->position + 1] == '/') {
      size_t end = lex->position + 2;
      while (end < lex->length && lex->text[end] != '\n') end++;
      lex->position = end >= lex->length ? lex->length : end + 1;
      continue;
    }
    break;
  }
  if (lex->position >= lex->length) return TOKEN_NONE;
  char first = lex->text[lex->position++];
  if (first == '{') return TOKEN_OPEN;
  if (first == '}') return TOKEN_CLOSE;
  size_t start;
  if (first == '"') {
    start = lex->position;
    while (lex->position < lex->length && lex->text[lex->position] != '"' &&
           lex->text[lex->position] != '\0')
      lex->position++;
    if (lex->position >= lex->length || lex->text[lex->position] == '\0')
      plain_error("Entity text:%zu: unterminated quote", start);
    size_t length = lex->position - start;
    lex->position++;
    char *value = xmalloc(length + 1);
    memcpy(value, lex->text + start, length);
    value[length] = '\0';
    *out_value = value;
    return TOKEN_TEXT;
  }
  start = lex->position - 1;
  while (lex->position < lex->length && (unsigned char)lex->text[lex->position] > ' ' &&
         lex->text[lex->position] != '{' && lex->text[lex->position] != '}')
    lex->position++;
  size_t length = lex->position - start;
  char *value = xmalloc(length + 1);
  memcpy(value, lex->text + start, length);
  value[length] = '\0';
  *out_value = value;
  return TOKEN_TEXT;
}

/* What a malformed entity lump leaves half built when the parse raises: the
 * entities so far, the one being read and its pending key. */
typedef struct parse_state {
  qa_q1_entity *entities;
  size_t count;
  qa_q1_entity entity;
  char *key;
} parse_state;
static void parse_release(void *arg) {
  parse_state *state = (parse_state *)arg;
  free(state->key);
  qa_q1_entities_destroy(state->entities, state->count);
  for (size_t i = 0; i < state->entity.property_count; i++) {
    free(state->entity.properties[i].key);
    free(state->entity.properties[i].value);
  }
  free(state->entity.properties);
}

qa_q1_entity *qa_q1_parse_entities(const char *text, size_t *out_count) {
  lexer lex = {text, strlen(text), 0};
  parse_state state;
  memset(&state, 0, sizeof state);
  qa_cleanup guard;
  qa_cleanup_push(&guard, parse_release, &state);
#define entities state.entities
#define count state.count
  size_t capacity = 0;
  for (;;) {
    char *token_value = NULL;
    token_kind first = next_token(&lex, &token_value);
    free(token_value);
    if (first == TOKEN_NONE) break;
    if (first != TOKEN_OPEN) plain_error("Entity text:%zu: expected opening brace", lex.position);
    state.entity.properties = NULL;
    state.entity.property_count = 0;
    size_t property_capacity = 0;
    for (;;) {
      token_kind key_kind = next_token(&lex, &state.key);
      if (key_kind == TOKEN_CLOSE) { free(state.key); state.key = NULL; break; }
      if (key_kind != TOKEN_TEXT) {
        plain_error("Entity text:%zu: expected key or closing brace", lex.position);
      }
      char *value = NULL;
      token_kind value_kind = next_token(&lex, &value);
      if (value_kind != TOKEN_TEXT) {
        free(value);
        /* qa_raise directly: the key is file text of any length. */
        char *key = latin1_text(state.key);
        char message[QA_CAUGHT_MESSAGE_CAPACITY];
        snprintf(message, sizeof message, "Entity text:%zu: expected value for %s", lex.position, key);
        free(key);
        qa_raise("Error", 0, 0, "%s", message);
      }
      if (state.entity.property_count == property_capacity) {
        property_capacity = property_capacity ? property_capacity * 2 : 8;
        state.entity.properties =
            xrealloc(state.entity.properties, property_capacity * sizeof *state.entity.properties);
      }
      state.entity.properties[state.entity.property_count].key = state.key;
      state.entity.properties[state.entity.property_count].value = value;
      state.entity.property_count++;
      state.key = NULL;
    }
    if (count == capacity) {
      capacity = capacity ? capacity * 2 : 8;
      entities = xrealloc(entities, capacity * sizeof *entities);
    }
    entities[count++] = state.entity;
    state.entity.properties = NULL;
    state.entity.property_count = 0;
  }
  qa_cleanup_pop(&guard);
  *out_count = count;
  return entities;
#undef entities
#undef count
}

void qa_q1_entities_destroy(qa_q1_entity *entities, size_t count) {
  if (entities == NULL) return;
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < entities[i].property_count; j++) {
      free(entities[i].properties[j].key);
      free(entities[i].properties[j].value);
    }
    free(entities[i].properties);
  }
  free(entities);
}

const char *qa_q1_entity_value(const qa_q1_entity *entity, const char *key) {
  const char *value = NULL;
  for (size_t i = 0; i < entity->property_count; i++) {
    if (strcmp(entity->properties[i].key, key) == 0) value = entity->properties[i].value;
  }
  return value;
}
