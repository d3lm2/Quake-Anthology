/* Port of src/persistence/value.ts. GPL-2.0-or-later. */
#include "qa/persistence/value.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/caught_message.h"
#include "qa/foundation/support.h"

/* TS SaveFormatError(path, message): the whole text, whatever its length (messages carry
 * names read from the save). The formatted copy goes with the raise's cleanups. */
static noreturn void save_format_errorv(const char *path, const char *format, va_list args) {
  va_list measure;
  va_copy(measure, args);
  int needed = vsnprintf(NULL, 0, format, measure);
  va_end(measure);
  size_t size = needed < 0 ? 1 : (size_t)needed + 1;
  char *text = (char *)malloc(size);
  if (text == NULL) qa_raise("SaveFormatError", 0, 0, "%s: %s", path, "out of memory");
  vsnprintf(text, size, format, args);
  qa_cleanup node;
  qa_cleanup_push(&node, free, text);
  qa_raise("SaveFormatError", 0, 0, "%s: %s", path, text);
}
noreturn void qa_save_format_error(const char *path, const char *format, ...) {
  va_list args;
  va_start(args, format);
  save_format_errorv(path, format, args);
}

/* JSON.parse / fatal TextDecoder failures raise a SyntaxError / TypeError, matching
 * the TS runtime error shape; quiet parses (TS try/catch sites) catch them. */
static noreturn void syntax_error_message(const char *message) {
  qa_raise("SyntaxError", 0, 0, "JSON Parse error: %s", message);
}

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}
static char *dup_text(const char *text) { return dup_range(text, strlen(text)); }
/* Whole byte length of a string node or key: the recorded length when it holds a NUL. */
static size_t text_bytes(const char *text, size_t recorded) {
  size_t length = strlen(text);
  return recorded > length ? recorded : length;
}

static qa_save_value *value_new(qa_save_value_kind kind) {
  qa_save_value *value = (qa_save_value *)calloc(1, sizeof *value);
  if (value != NULL) value->kind = kind;
  return value;
}
/* JS own-key order (Object.keys, JSON.stringify): array-index keys
 * ("0" or [1-9][0-9]* below 2^32 - 1) ascending, then the other keys in
 * insertion order. */
static bool js_array_index_key_entry(const qa_save_entry *entry, uint32_t *out);
static bool js_array_index_key(const char *key, uint32_t *out) {
  if (key[0] == '\0' || (key[0] == '0' && key[1] != '\0')) return false;
  uint64_t value = 0;
  for (const char *p = key; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + (uint64_t)(*p - '0');
    if (value > 4294967294u) return false;
  }
  *out = (uint32_t)value;
  return true;
}

/* A key holding a NUL is never an array index. */
static bool js_array_index_key_entry(const qa_save_entry *entry, uint32_t *out) {
  return entry->key_length <= strlen(entry->key) && js_array_index_key(entry->key, out);
}
/* The JS own-key order of `count` entries as entry indexes (array-index keys ascending, the
 * rest in insertion order), in O(n log n): a parsed object can hold many keys. NULL on OOM. */
typedef struct index_key { uint32_t value; size_t position; } index_key;
static int index_key_compare(const void *a, const void *b) {
  const index_key *x = a, *y = b;
  if (x->value != y->value) return x->value < y->value ? -1 : 1;
  return (x->position > y->position) - (x->position < y->position);
}
static size_t *js_entry_order(const qa_save_entry *entries, size_t count) {
  size_t *order = (size_t *)malloc((count + 1) * sizeof *order);
  index_key *keys = (index_key *)malloc((count + 1) * sizeof *keys);
  if (order == NULL || keys == NULL) {
    free(order);
    free(keys);
    return NULL;
  }
  size_t index_count = 0, n = 0;
  for (size_t i = 0; i < count; i++) {
    uint32_t value = 0;
    if (js_array_index_key_entry(&entries[i], &value)) keys[index_count++] = (index_key){value, i};
  }
  qsort(keys, index_count, sizeof *keys, index_key_compare);
  for (size_t i = 0; i < index_count; i++) order[n++] = keys[i].position;
  for (size_t i = 0; i < count; i++) {
    uint32_t value = 0;
    if (!js_array_index_key_entry(&entries[i], &value)) order[n++] = i;
  }
  free(keys);
  return order;
}
/* Reorders a parsed object's entries into JSON.parse's result key order. */
static bool js_order_entries(qa_save_entry *entries, size_t count) {
  bool any = false;
  for (size_t i = 0; i < count && !any; i++) {
    uint32_t value = 0;
    any = js_array_index_key_entry(&entries[i], &value);
  }
  if (!any) return true;
  size_t *order = js_entry_order(entries, count);
  qa_save_entry *copy = (qa_save_entry *)malloc((count + 1) * sizeof *copy);
  if (order == NULL || copy == NULL) {
    free(order);
    free(copy);
    return false;
  }
  for (size_t i = 0; i < count; i++) copy[i] = entries[order[i]];
  memcpy(entries, copy, count * sizeof *copy);
  free(copy);
  free(order);
  return true;
}

qa_save_value *qa_save_value_undefined(void) { return value_new(QA_SAVE_UNDEFINED); }
qa_save_value *qa_save_value_null(void) { return value_new(QA_SAVE_NULL); }
qa_save_value *qa_save_value_boolean(bool b) {
  qa_save_value *v = value_new(QA_SAVE_BOOLEAN);
  if (v != NULL) v->boolean = b;
  return v;
}
qa_save_value *qa_save_value_number(double n) {
  qa_save_value *v = value_new(QA_SAVE_NUMBER);
  if (v != NULL) v->number = n;
  return v;
}
qa_save_value *qa_save_value_bigint_text(const char *decimal) {
  qa_save_value *v = value_new(QA_SAVE_BIGINT);
  if (v == NULL) return NULL;
  v->text = dup_text(decimal);
  if (v->text == NULL) { free(v); return NULL; }
  return v;
}
qa_save_value *qa_save_value_bigint_i64(int64_t n) {
  char buffer[32];
  (void)snprintf(buffer, sizeof buffer, "%lld", (long long)n);
  return qa_save_value_bigint_text(buffer);
}
qa_save_value *qa_save_value_bigint_u64(uint64_t n) {
  char buffer[32];
  (void)snprintf(buffer, sizeof buffer, "%llu", (unsigned long long)n);
  return qa_save_value_bigint_text(buffer);
}
qa_save_value *qa_save_value_string(const char *s) {
  qa_save_value *v = value_new(QA_SAVE_STRING);
  if (v == NULL) return NULL;
  v->text = dup_text(s);
  if (v->text == NULL) { free(v); return NULL; }
  return v;
}
qa_save_value *qa_save_value_bytes(const uint8_t *data, size_t length) {
  qa_save_value *v = value_new(QA_SAVE_BYTES);
  if (v == NULL) return NULL;
  if (length > 0) {
    v->bytes = (uint8_t *)malloc(length);
    if (v->bytes == NULL) { free(v); return NULL; }
    memcpy(v->bytes, data, length);
    v->byte_length = length;
  }
  return v;
}
qa_save_value *qa_save_value_array(void) { return value_new(QA_SAVE_ARRAY); }
qa_save_value *qa_save_value_object(void) { return value_new(QA_SAVE_OBJECT); }

bool qa_save_value_push(qa_save_value *array, qa_save_value *child) {
  qa_save_value **items = (qa_save_value **)realloc(
      array->items, (array->item_count + 1) * sizeof *items);
  if (items == NULL) return false;
  array->items = items;
  array->items[array->item_count++] = child;
  return true;
}
bool qa_save_value_set(qa_save_value *object, const char *key, qa_save_value *child) {
  char *copy = dup_text(key);
  if (copy == NULL) return false;
  qa_save_entry *entries = (qa_save_entry *)realloc(
      object->entries, (object->entry_count + 1) * sizeof *entries);
  if (entries == NULL) { free(copy); return false; }
  object->entries = entries;
  object->entries[object->entry_count].key = copy;
  object->entries[object->entry_count].value = child;
  object->entries[object->entry_count].key_length = 0;
  object->entry_count++;
  return true;
}
/* A reader path that outgrew qa_save_reader.path: the whole path is the parent's whole path
 * (or, with no parent, nothing) followed by `text`. Owned by the node the reader was opened
 * under, through qa_save_value.reader_paths. */
struct qa_save_path_node {
  const struct qa_save_path_node *parent;
  struct qa_save_path_node *next; /* the owning node's list */
  size_t length;
  char text[];
};
static void free_reader_paths(qa_save_value *value) {
  struct qa_save_path_node *node = value->reader_paths;
  while (node != NULL) {
    struct qa_save_path_node *next = node->next;
    free(node);
    node = next;
  }
}
static void free_node(qa_save_value *value) {
  free(value->text);
  free(value->bytes);
  free(value->items);
  for (size_t i = 0; i < value->entry_count; i++) free(value->entries[i].key);
  free(value->entries);
  free(value->attachment);
  free_reader_paths(value);
  free(value);
}
static void free_recursive(qa_save_value *value) {
  for (size_t i = 0; i < value->item_count; i++)
    if (value->items[i] != NULL) free_recursive(value->items[i]);
  for (size_t i = 0; i < value->entry_count; i++)
    if (value->entries[i].value != NULL) free_recursive(value->entries[i].value);
  free_node(value);
}
/* Iterative: a parsed tree can nest deeper than the C stack allows recursion. */
void qa_save_value_free(qa_save_value *value) {
  if (value == NULL) return;
  size_t count = 0, capacity = 64;
  qa_save_value **stack = (qa_save_value **)malloc(capacity * sizeof *stack);
  if (stack == NULL) {
    free_recursive(value);
    return;
  }
  stack[count++] = value;
  while (count > 0) {
    qa_save_value *node = stack[--count];
    size_t children = node->item_count + node->entry_count;
    if (count + children > capacity) {
      size_t grown = capacity;
      while (grown < count + children) grown *= 2;
      qa_save_value **next = (qa_save_value **)realloc(stack, grown * sizeof *next);
      if (next == NULL) {
        free_recursive(node);
        continue;
      }
      stack = next;
      capacity = grown;
    }
    for (size_t i = 0; i < node->item_count; i++)
      if (node->items[i] != NULL) stack[count++] = node->items[i];
    for (size_t i = 0; i < node->entry_count; i++)
      if (node->entries[i].value != NULL) stack[count++] = node->entries[i].value;
    free_node(node);
  }
  free(stack);
}
bool qa_save_value_attach(qa_save_value *value, const void *data, size_t length) {
  void *copy = malloc(length ? length : 1);
  if (copy == NULL) return false;
  memcpy(copy, data, length);
  free(value->attachment);
  value->attachment = copy;
  value->attachment_length = length;
  return true;
}
const void *qa_save_value_attachment(const qa_save_value *value, size_t length) {
  return value != NULL && value->attachment_length == length ? value->attachment : NULL;
}
const qa_save_value *qa_save_value_get(const qa_save_value *object, const char *key) {
  if (object == NULL || object->kind != QA_SAVE_OBJECT) return NULL;
  for (size_t i = 0; i < object->entry_count; i++)
    if (strcmp(object->entries[i].key, key) == 0 && object->entries[i].key_length <= strlen(key))
      return object->entries[i].value;
  return NULL;
}

/* --- canonical base64 (TS canonicalBase64) --- */
static bool canonical_base64(const char *value, size_t length) {
  if (length % 4 != 0) return false;
  size_t padding = 0;
  if (length >= 2 && value[length - 1] == '=' && value[length - 2] == '=') padding = 2;
  else if (length >= 1 && value[length - 1] == '=') padding = 1;
  size_t end = length - padding;
  int last_digit = 0;
  for (size_t i = 0; i < end; i++) {
    unsigned char code = (unsigned char)value[i];
    if (code >= 65 && code <= 90) last_digit = code - 65;
    else if (code >= 97 && code <= 122) last_digit = code - 71;
    else if (code >= 48 && code <= 57) last_digit = code + 4;
    else if (code == 43) last_digit = 62;
    else if (code == 47) last_digit = 63;
    else return false;
  }
  if (padding == 2) return (last_digit & 15) == 0;
  if (padding == 1) return (last_digit & 3) == 0;
  return true;
}
static const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int base64_digit(unsigned char code) {
  if (code >= 'A' && code <= 'Z') return code - 'A';
  if (code >= 'a' && code <= 'z') return code - 'a' + 26;
  if (code >= '0' && code <= '9') return code - '0' + 52;
  if (code == '+') return 62;
  return 63;
}
static char *base64_encode(const uint8_t *data, size_t length) {
  size_t out_length = ((length + 2) / 3) * 4;
  char *out = (char *)malloc(out_length + 1);
  if (out == NULL) return NULL;
  size_t o = 0;
  for (size_t i = 0; i < length; i += 3) {
    size_t remaining = length - i;
    unsigned a = data[i];
    unsigned b = remaining > 1 ? data[i + 1] : 0;
    unsigned c = remaining > 2 ? data[i + 2] : 0;
    out[o++] = base64_alphabet[a >> 2];
    out[o++] = base64_alphabet[((a & 3) << 4) | (b >> 4)];
    out[o++] = remaining > 1 ? base64_alphabet[((b & 15) << 2) | (c >> 6)] : '=';
    out[o++] = remaining > 2 ? base64_alphabet[c & 63] : '=';
  }
  out[o] = '\0';
  return out;
}
/* Caller checked canonical_base64. */
static uint8_t *base64_decode(const char *text, size_t length, size_t *out_length) {
  size_t padding = 0;
  if (length >= 2 && text[length - 1] == '=' && text[length - 2] == '=') padding = 2;
  else if (length >= 1 && text[length - 1] == '=') padding = 1;
  size_t total = (length / 4) * 3 - padding;
  uint8_t *out = total > 0 ? (uint8_t *)malloc(total) : NULL;
  if (total > 0 && out == NULL) return NULL;
  size_t o = 0;
  for (size_t i = 0; i < length; i += 4) {
    int a = base64_digit((unsigned char)text[i]);
    int b = base64_digit((unsigned char)text[i + 1]);
    int c = text[i + 2] == '=' ? 0 : base64_digit((unsigned char)text[i + 2]);
    int d = text[i + 3] == '=' ? 0 : base64_digit((unsigned char)text[i + 3]);
    if (o < total) out[o++] = (uint8_t)((a << 2) | (b >> 4));
    if (o < total) out[o++] = (uint8_t)(((b & 15) << 4) | (c >> 2));
    if (o < total) out[o++] = (uint8_t)(((c & 3) << 6) | d);
  }
  *out_length = total;
  return out;
}

/* --- growable text buffer for JSON output --- */
typedef struct text_buffer { char *data; size_t length; size_t capacity; } text_buffer;
static bool text_reserve(text_buffer *b, size_t extra) {
  if (b->length + extra + 1 <= b->capacity) return true;
  size_t cap = b->capacity == 0 ? 256 : b->capacity;
  while (cap < b->length + extra + 1) cap *= 2;
  char *data = (char *)realloc(b->data, cap);
  if (data == NULL) return false;
  b->data = data;
  b->capacity = cap;
  return true;
}
static bool text_add(text_buffer *b, const char *text, size_t length) {
  if (!text_reserve(b, length)) return false;
  memcpy(b->data + b->length, text, length);
  b->length += length;
  b->data[b->length] = '\0';
  return true;
}
static bool text_add_cstr(text_buffer *b, const char *text) {
  return text_add(b, text, strlen(text));
}
static bool text_add_char(text_buffer *b, char c) { return text_add(b, &c, 1); }

/* JS String(finite number) via shortest round-trip decimal. */
static void format_js_number(double value, char *buffer, size_t size) {
  qa_net_js_number(value, buffer, size);
}

/* The WTF-8 surrogate half at p (ED A0..BF xx), or 0. */
static unsigned surrogate_at(const unsigned char *p, const unsigned char *end) {
  if (end - p < 3 || p[0] != 0xed || p[1] < 0xa0 || p[1] > 0xbf || (p[2] & 0xc0) != 0x80) return 0;
  return 0xd000u | ((unsigned)(p[1] & 0x3f) << 6) | (unsigned)(p[2] & 0x3f);
}
/* JSON.stringify of a string: well-formed, so a lone surrogate half (kept as WTF-8 by the
 * parser) is written as a lowercase \udXXX escape and a paired one as the character. */
static bool write_json_string_length(text_buffer *out, const char *text, size_t length) {
  static const char hex_digits[] = "0123456789abcdef";
  if (!text_add_char(out, '"')) return false;
  const unsigned char *end = (const unsigned char *)text + length;
  for (const unsigned char *p = (const unsigned char *)text; p < end; p++) {
    bool ok;
    unsigned unit = surrogate_at(p, end);
    if (unit != 0) {
      unsigned low = unit <= 0xdbff ? surrogate_at(p + 3, end) : 0;
      if (low >= 0xdc00) {
        uint32_t cp = 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u);
        char pair[4] = {(char)(0xf0 | (cp >> 18)), (char)(0x80 | ((cp >> 12) & 0x3f)),
                        (char)(0x80 | ((cp >> 6) & 0x3f)), (char)(0x80 | (cp & 0x3f))};
        ok = text_add(out, pair, 4);
        p += 5;
      } else {
        char escape[7] = {'\\', 'u', hex_digits[unit >> 12], hex_digits[(unit >> 8) & 0xf],
                          hex_digits[(unit >> 4) & 0xf], hex_digits[unit & 0xf], '\0'};
        ok = text_add_cstr(out, escape);
        p += 2;
      }
      if (!ok) return false;
      continue;
    }
    switch (*p) {
      case '"': ok = text_add_cstr(out, "\\\""); break;
      case '\\': ok = text_add_cstr(out, "\\\\"); break;
      case '\b': ok = text_add_cstr(out, "\\b"); break;
      case '\f': ok = text_add_cstr(out, "\\f"); break;
      case '\n': ok = text_add_cstr(out, "\\n"); break;
      case '\r': ok = text_add_cstr(out, "\\r"); break;
      case '\t': ok = text_add_cstr(out, "\\t"); break;
      default:
        if (*p < 0x20) {
          char escape[7] = {'\\', 'u', '0', '0', hex_digits[*p >> 4], hex_digits[*p & 0xf], '\0'};
          ok = text_add_cstr(out, escape);
        } else {
          ok = text_add_char(out, (char)*p);
        }
        break;
    }
    if (!ok) return false;
  }
  return text_add_char(out, '"');
}
static bool write_json_string(text_buffer *out, const char *text) {
  return write_json_string_length(out, text, strlen(text));
}

/* ---- path text of an iterative walk ----
 * A walk keeps one frame per open container; a frame's current child is item `index`, or the
 * entry `index` of the object. The path is built only when an error needs it: TS builds the
 * same text (`${path}[${index}]`, `${path}.${key}`), and keys from a save can make it long. */
typedef struct walk_frame {
  const qa_save_value *value;
  size_t next;   /* children visited so far */
  size_t *order; /* pack: the object's key order; NULL for arrays */
  size_t current; /* the child being visited: item index or entry index */
} walk_frame;
static bool path_append_segment(text_buffer *out, const walk_frame *frame) {
  if (frame->value->kind == QA_SAVE_ARRAY) {
    char index[32];
    (void)snprintf(index, sizeof index, "[%zu]", frame->current);
    return text_add_cstr(out, index);
  }
  const qa_save_entry *entry = &frame->value->entries[frame->current];
  return text_add_char(out, '.') && text_add(out, entry->key, text_bytes(entry->key, entry->key_length));
}
/* "<root>" plus the segments of frames [0, count): malloc'd, NULL on OOM. */
static char *walk_path(const char *root, const walk_frame *frames, size_t count) {
  text_buffer out = {NULL, 0, 0};
  bool ok = text_add_cstr(&out, root);
  for (size_t i = 0; ok && i < count; i++) ok = path_append_segment(&out, &frames[i]);
  if (!ok) {
    free(out.data);
    return NULL;
  }
  return out.data;
}
static noreturn void walk_fail(const char *root, const walk_frame *frames, size_t count, const char *message) {
  char *path = walk_path(root, frames, count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, path);
  qa_save_format_error(path != NULL ? path : root, "%s", message);
}
typedef struct walk_stack {
  walk_frame *frames;
  size_t count, capacity;
} walk_stack;
static void walk_stack_free(void *arg) {
  walk_stack *stack = arg;
  for (size_t i = 0; i < stack->count; i++) free(stack->frames[i].order);
  free(stack->frames);
  stack->frames = NULL;
  stack->count = stack->capacity = 0;
}
static bool walk_push(walk_stack *stack, const qa_save_value *value, size_t *order) {
  if (stack->count == stack->capacity) {
    size_t capacity = stack->capacity != 0 ? stack->capacity * 2 : 32;
    walk_frame *frames = (walk_frame *)realloc(stack->frames, capacity * sizeof *frames);
    if (frames == NULL) return false;
    stack->frames = frames;
    stack->capacity = capacity;
  }
  stack->frames[stack->count++] = (walk_frame){value, 0, order, 0};
  return true;
}

static bool pack_tagged(text_buffer *out, const char *tag, const char *encoded) {
  return text_add_cstr(out, "{\"$qts\":") && write_json_string(out, tag) &&
         text_add_cstr(out, ",\"value\":") && write_json_string(out, encoded) &&
         text_add_char(out, '}');
}
/* One non-container value; false on OOM. */
static bool pack_scalar(const qa_save_value *value, text_buffer *out) {
  switch (value->kind) {
    case QA_SAVE_NULL: return text_add_cstr(out, "null");
    case QA_SAVE_BOOLEAN: return text_add_cstr(out, value->boolean ? "true" : "false");
    case QA_SAVE_STRING: return write_json_string_length(out, value->text, text_bytes(value->text, value->text_length));
    case QA_SAVE_NUMBER: {
      if (isfinite(value->number) && !(value->number == 0.0 && signbit(value->number))) {
        char buffer[64];
        format_js_number(value->number, buffer, sizeof buffer);
        return text_add_cstr(out, buffer);
      }
      const char *encoded = (value->number == 0.0 && signbit(value->number)) ? "-0"
          : isnan(value->number) ? "NaN" : value->number > 0 ? "Infinity" : "-Infinity";
      return pack_tagged(out, "number", encoded);
    }
    case QA_SAVE_BIGINT: return pack_tagged(out, "bigint", value->text);
    case QA_SAVE_BYTES: {
      char *encoded = base64_encode(value->bytes, value->byte_length);
      if (encoded == NULL) return false;
      bool ok = pack_tagged(out, "bytes", encoded);
      free(encoded);
      return ok;
    }
    default: return false;
  }
}
static bool has_reserved_key(const qa_save_value *object) {
  for (size_t i = 0; i < object->entry_count; i++)
    if (strcmp(object->entries[i].key, "$qts") == 0 && object->entries[i].key_length <= 4) return true;
  return false;
}
/* TS pack() then JSON.stringify, written directly and iteratively (TS recurses; a tree deep
 * enough to exhaust the JS stack cannot come from the decoder, which refuses it). Raises the
 * TS SaveFormatErrors with their paths; false on OOM. */
static bool pack_tree(const qa_save_value *root, text_buffer *out) {
  walk_stack stack = {NULL, 0, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, walk_stack_free, &stack);
  bool ok = true;
  const qa_save_value *value = root;
  for (;;) {
    /* Visit `value`, the root or the current child of the top frame. */
    if (value->kind == QA_SAVE_ARRAY || value->kind == QA_SAVE_OBJECT) {
      size_t *order = NULL;
      if (value->kind == QA_SAVE_OBJECT) {
        if (has_reserved_key(value)) walk_fail("checkpoint", stack.frames, stack.count, "$qts is reserved by the save envelope");
        order = js_entry_order(value->entries, value->entry_count);
        if (order == NULL) { ok = false; break; }
      }
      if (!walk_push(&stack, value, order)) { free(order); ok = false; break; }
      if (!text_add_char(out, value->kind == QA_SAVE_ARRAY ? '[' : '{')) { ok = false; break; }
    } else if (value->kind == QA_SAVE_UNDEFINED || value->kind > QA_SAVE_OBJECT) {
      walk_fail("checkpoint", stack.frames, stack.count, "checkpoint contains a non-data value");
    } else if (!pack_scalar(value, out)) {
      ok = false;
      break;
    }
    /* Move to the next child, closing finished containers. */
    value = NULL;
    while (stack.count > 0 && value == NULL) {
      walk_frame *top = &stack.frames[stack.count - 1];
      size_t total = top->value->kind == QA_SAVE_ARRAY ? top->value->item_count : top->value->entry_count;
      if (top->next == total) {
        if (!text_add_char(out, top->value->kind == QA_SAVE_ARRAY ? ']' : '}')) { ok = false; break; }
        free(top->order);
        stack.count--;
        continue;
      }
      if (top->next > 0 && !text_add_char(out, ',')) { ok = false; break; }
      if (top->value->kind == QA_SAVE_ARRAY) {
        top->current = top->next++;
        value = top->value->items[top->current];
      } else {
        top->current = top->order[top->next++];
        const qa_save_entry *entry = &top->value->entries[top->current];
        if (!write_json_string_length(out, entry->key, text_bytes(entry->key, entry->key_length)) ||
            !text_add_char(out, ':')) { ok = false; break; }
        value = entry->value;
      }
    }
    if (!ok || value == NULL) break;
  }
  qa_cleanup_pop(&guard);
  walk_stack_free(&stack);
  return ok;
}

static void free_text_buffer(void *arg) { free(((text_buffer *)arg)->data); }
uint8_t *qa_encode_checkpoint_value(const qa_save_value *value, size_t *out_length) {
  text_buffer out = {NULL, 0, 0};
  qa_cleanup guard; /* a refused value raises with the partial text released */
  qa_cleanup_push(&guard, free_text_buffer, &out);
  bool ok = pack_tree(value, &out);
  qa_cleanup_pop(&guard);
  if (!ok) { free(out.data); return NULL; }
  if (out.data == NULL && !text_reserve(&out, 0)) return NULL;
  *out_length = out.length;
  return (uint8_t *)out.data;
}

/* --- strict JSON parser (JSON.parse over fatal UTF-8) ---
 * Produces a raw tree using only null/boolean/number/string/array/
 * object kinds; unpack() then interprets $qts tags. Strings are
 * stored as UTF-8 bytes; \u escapes (incl. surrogate pairs and lone
 * surrogates as WTF-8) are encoded as UTF-8. */
static bool utf8_valid(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t c = bytes[i];
    if (c < 0x80) { i++; continue; }
    size_t extra; uint32_t cp; uint32_t min;
    if ((c & 0xe0) == 0xc0) { extra = 1; cp = c & 0x1f; min = 0x80; }
    else if ((c & 0xf0) == 0xe0) { extra = 2; cp = c & 0x0f; min = 0x800; }
    else if ((c & 0xf8) == 0xf0) { extra = 3; cp = c & 0x07; min = 0x10000; }
    else return false;
    if (i + extra >= length + 0 && i + extra > length - 1) return false;
    for (size_t j = 1; j <= extra; j++) {
      if (i + j >= length || (bytes[i + j] & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (uint32_t)(bytes[i + j] & 0x3f);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    i += extra + 1;
  }
  return true;
}

/* JavaScriptCore LiteralParser (strict JSON), which is what Bun's JSON.parse
 * runs: the lexer stays one token ahead of the parser, and a failure reports
 * "JSON Parse error: " plus the lexer's message when the current token is a
 * lexer error, else the parser's message, else (trailing tokens) "Unable to
 * parse JSON string". Offsets are UTF-8 bytes; the texts JSC prints per UTF-16
 * unit print the whole UTF-8 character here. */
typedef enum json_token {
  JT_LBRACKET, JT_RBRACKET, JT_LPAREN, JT_RPAREN, JT_LBRACE, JT_RBRACE,
  JT_COMMA, JT_COLON, JT_ASSIGN, JT_SEMI, JT_DOT, JT_STRING, JT_NUMBER,
  JT_TRUE, JT_FALSE, JT_NULL, JT_IDENTIFIER, JT_END, JT_ERROR
} json_token;
typedef struct json_parser {
  const char *text;
  size_t offset;
  size_t length;
  size_t max_depth; /* deepest node of the parsed document (root = 1) */
  json_token token;
  size_t token_start, token_end;
  char *token_string; /* decoded JT_STRING text, owned until taken */
  size_t token_string_length; /* its bytes; more than strlen when it holds a NUL */
  double token_number;
  char lex_error[160];
} json_parser;
static void cleanup_value(void *arg) { qa_save_value_free(arg); }
static void cleanup_text(void *arg) { free(arg); }
static void cleanup_token(void *arg) {
  json_parser *p = arg;
  free(p->token_string);
  p->token_string = NULL;
}
static bool encode_utf8(text_buffer *out, uint32_t cp) {
  char buf[4]; size_t n;
  if (cp < 0x80) { buf[0] = (char)cp; n = 1; }
  else if (cp < 0x800) { buf[0] = (char)(0xc0 | (cp >> 6)); buf[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
  else if (cp < 0x10000) { buf[0] = (char)(0xe0 | (cp >> 12)); buf[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
  else { buf[0] = (char)(0xf0 | (cp >> 18)); buf[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); buf[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
  return text_add(out, buf, n);
}
static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
/* Byte length of the UTF-8 character at offset (1 for a stray byte). */
static size_t json_char_length(const json_parser *p, size_t offset) {
  unsigned char c = (unsigned char)p->text[offset];
  size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
  return offset + n <= p->length ? n : p->length - offset;
}
static json_token lex_fail(json_parser *p, const char *message) {
  snprintf(p->lex_error, sizeof p->lex_error, "%s", message);
  p->token = JT_ERROR;
  return JT_ERROR;
}
static json_token lex_fail_char(json_parser *p, const char *prefix, size_t at, const char *suffix) {
  snprintf(p->lex_error, sizeof p->lex_error, "%s%.*s%s", prefix,
           (int)json_char_length(p, at), p->text + at, suffix);
  p->token = JT_ERROR;
  return JT_ERROR;
}
static bool json_digit(const json_parser *p, size_t at) {
  return at < p->length && p->text[at] >= '0' && p->text[at] <= '9';
}
/* -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)? ; an exponent marker not
 * followed by digits ends the number before it. */
static json_token lex_number(json_parser *p) {
  size_t at = p->offset;
  if (at < p->length && p->text[at] == '-') at++;
  if (at < p->length && p->text[at] == '0') at++;
  else if (at < p->length && p->text[at] >= '1' && p->text[at] <= '9') {
    while (json_digit(p, at)) at++;
  } else {
    return lex_fail(p, "Invalid number");
  }
  if (at < p->length && p->text[at] == '.') {
    at++;
    if (!json_digit(p, at)) return lex_fail(p, "Invalid digits after decimal point");
    while (json_digit(p, at)) at++;
  }
  if (at < p->length && (p->text[at] == 'e' || p->text[at] == 'E')) {
    size_t exponent = at + 1;
    if (exponent < p->length && (p->text[exponent] == '+' || p->text[exponent] == '-')) exponent++;
    if (json_digit(p, exponent)) {
      at = exponent;
      while (json_digit(p, at)) at++;
    }
  }
  char *token = dup_range(p->text + p->offset, at - p->offset);
  if (token == NULL) qa_range_error("Out of memory");
  p->token_number = strtod(token, NULL);
  free(token);
  p->offset = at;
  p->token = JT_NUMBER;
  return JT_NUMBER;
}
/* UTF-16 units in the 4-unit window after "\u", as UTF-8 bytes. */
static size_t escape_window(const json_parser *p, size_t at, size_t *units) {
  size_t bytes = 0;
  *units = 0;
  while (*units < 4 && at + bytes < p->length) {
    size_t n = json_char_length(p, at + bytes);
    *units += n == 4 ? 2 : 1;
    bytes += n;
  }
  return bytes;
}
static json_token lex_string(json_parser *p) {
  size_t at = p->offset + 1;
  text_buffer out = {NULL, 0, 0};
  for (;;) {
    size_t run = at;
    while (at < p->length) {
      unsigned char c = (unsigned char)p->text[at];
      if (c < 0x20 || c == '"' || c == '\\') break;
      at++;
    }
    if (at > run && !text_add(&out, p->text + run, at - run)) qa_range_error("Out of memory");
    if (at >= p->length) { free(out.data); return lex_fail(p, "Unterminated string"); }
    char c = p->text[at];
    if (c == '"') { at++; break; }
    if (c != '\\') { free(out.data); return lex_fail(p, "Unterminated string"); }
    at++;
    if (at >= p->length) { free(out.data); return lex_fail(p, "Unterminated string"); }
    char e = p->text[at];
    char simple = e == '"' ? '"' : e == '\\' ? '\\' : e == '/' ? '/' : e == 'b' ? '\b'
                : e == 'f' ? '\f' : e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : 0;
    if (simple != 0) {
      if (!text_add_char(&out, simple)) qa_range_error("Out of memory");
      at++;
      continue;
    }
    if (e != 'u') {
      free(out.data);
      return lex_fail_char(p, "Invalid escape character ", at, "");
    }
    size_t units;
    size_t window = escape_window(p, at + 1, &units);
    if (units < 4) { free(out.data); return lex_fail(p, "\\u must be followed by 4 hex digits"); }
    uint32_t cp = 0;
    bool hex = window == 4;
    for (size_t i = 0; hex && i < 4; i++) {
      int d = hex_value(p->text[at + 1 + i]);
      if (d < 0) hex = false;
      else cp = (cp << 4) | (uint32_t)d;
    }
    if (!hex) {
      free(out.data);
      snprintf(p->lex_error, sizeof p->lex_error, "\"\\u%.*s\" is not a valid unicode escape",
               (int)window, p->text + at + 1);
      p->token = JT_ERROR;
      return JT_ERROR;
    }
    at += 5;
    /* A high surrogate joins a following \u low surrogate; lone halves stay (WTF-8). */
    if (cp >= 0xd800 && cp <= 0xdbff && at + 6 <= p->length && p->text[at] == '\\' &&
        p->text[at + 1] == 'u') {
      uint32_t low = 0;
      bool ok = true;
      for (size_t i = 0; ok && i < 4; i++) {
        int d = hex_value(p->text[at + 2 + i]);
        if (d < 0) ok = false;
        else low = (low << 4) | (uint32_t)d;
      }
      if (ok && low >= 0xdc00 && low <= 0xdfff) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        at += 6;
      }
    }
    if (!encode_utf8(&out, cp)) qa_range_error("Out of memory");
  }
  p->token_string = out.data != NULL ? out.data : dup_text("");
  if (p->token_string == NULL) qa_range_error("Out of memory");
  p->token_string_length = out.data != NULL ? out.length : 0;
  p->offset = at;
  p->token = JT_STRING;
  return JT_STRING;
}
static bool json_literal(json_parser *p, const char *word, size_t length) {
  return p->length - p->offset >= length && memcmp(p->text + p->offset, word, length) == 0;
}
static json_token json_lex(json_parser *p) {
  free(p->token_string);
  p->token_string = NULL;
  while (p->offset < p->length) {
    char c = p->text[p->offset];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->offset++;
    else break;
  }
  p->token_start = p->offset;
  if (p->offset >= p->length) { p->token = JT_END; return JT_END; }
  char c = p->text[p->offset];
  json_token single = c == '[' ? JT_LBRACKET : c == ']' ? JT_RBRACKET : c == '(' ? JT_LPAREN
                    : c == ')' ? JT_RPAREN : c == '{' ? JT_LBRACE : c == '}' ? JT_RBRACE
                    : c == ',' ? JT_COMMA : c == ':' ? JT_COLON : c == '=' ? JT_ASSIGN
                    : c == ';' ? JT_SEMI : c == '.' ? JT_DOT : JT_ERROR;
  if (single != JT_ERROR) {
    p->offset++;
    p->token = single;
    return single;
  }
  if (c == '"') return lex_string(p);
  if (c == '-' || (c >= '0' && c <= '9')) return lex_number(p);
  if (c == 't' && json_literal(p, "true", 4)) { p->offset += 4; p->token = JT_TRUE; return JT_TRUE; }
  if (c == 'f' && json_literal(p, "false", 5)) { p->offset += 5; p->token = JT_FALSE; return JT_FALSE; }
  if (c == 'n' && json_literal(p, "null", 4)) { p->offset += 4; p->token = JT_NULL; return JT_NULL; }
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$') {
    while (p->offset < p->length) {
      char d = p->text[p->offset];
      if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') || d == '_' || d == '$')
        p->offset++;
      else break;
    }
    p->token_end = p->offset;
    p->token = JT_IDENTIFIER;
    return JT_IDENTIFIER;
  }
  if (c == '\'') return lex_fail(p, "Single quotes (') are not allowed in JSON");
  return lex_fail_char(p, "Unrecognized token '", p->offset, "'");
}
/* LiteralParser::getErrorMessage: the lexer's message wins. */
static noreturn void json_fail(json_parser *p, const char *message) {
  if (p->token == JT_ERROR) syntax_error_message(p->lex_error);
  syntax_error_message(message);
}
static noreturn void json_unexpected(json_parser *p) {
  char message[256];
  char token = 0;
  switch (p->token) {
    case JT_END: json_fail(p, "Unexpected EOF");
    case JT_IDENTIFIER:
      snprintf(message, sizeof message, "Unexpected identifier \"%.*s\"",
               (int)(p->token_end - p->token_start), p->text + p->token_start);
      json_fail(p, message);
    case JT_LBRACKET: token = '['; break;
    case JT_RBRACKET: token = ']'; break;
    case JT_LPAREN: token = '('; break;
    case JT_RPAREN: token = ')'; break;
    case JT_LBRACE: token = '{'; break;
    case JT_RBRACE: token = '}'; break;
    case JT_COMMA: token = ','; break;
    case JT_COLON: token = ':'; break;
    case JT_ASSIGN: token = '='; break;
    case JT_SEMI: token = ';'; break;
    case JT_DOT: token = '.'; break;
    default: json_fail(p, "Unable to parse JSON string");
  }
  snprintf(message, sizeof message, "Unexpected token '%c'", token);
  json_fail(p, message);
}
/* ---- iterative parse (JSC's LiteralParser is iterative: no depth limit of its own) ----
 * One frame per open container. An object frame holds the key whose value is being parsed and
 * a hash index over its keys, so a repeated key replaces its entry (JS keeps the last value in
 * the first position) in constant time. */
typedef struct parse_frame {
  qa_save_value *container;
  char *key; /* owned until the entry is stored */
  size_t key_bytes, key_length;
  size_t *index; /* entry index + 1 per slot; 0 = empty */
  size_t index_capacity;
} parse_frame;
typedef struct parse_stack {
  parse_frame *frames;
  size_t count, capacity;
  qa_save_value *pending; /* a finished value not yet stored in its parent */
} parse_stack;
static void parse_stack_free(void *arg) {
  parse_stack *stack = arg;
  for (size_t i = 0; i < stack->count; i++) {
    qa_save_value_free(stack->frames[i].container);
    free(stack->frames[i].key);
    free(stack->frames[i].index);
  }
  free(stack->frames);
  qa_save_value_free(stack->pending);
  stack->frames = NULL;
  stack->pending = NULL;
  stack->count = stack->capacity = 0;
}
static uint64_t key_hash(const char *key, size_t length) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < length; i++) hash = (hash ^ (unsigned char)key[i]) * 0x100000001b3ull;
  return hash;
}
static bool frame_index_rebuild(parse_frame *frame, size_t capacity) {
  size_t *index = (size_t *)calloc(capacity, sizeof *index);
  if (index == NULL) return false;
  const qa_save_value *v = frame->container;
  for (size_t i = 0; i < v->entry_count; i++) {
    size_t length = text_bytes(v->entries[i].key, v->entries[i].key_length);
    for (size_t at = (size_t)key_hash(v->entries[i].key, length) & (capacity - 1);; at = (at + 1) & (capacity - 1))
      if (index[at] == 0) {
        index[at] = i + 1;
        break;
      }
  }
  free(frame->index);
  frame->index = index;
  frame->index_capacity = capacity;
  return true;
}
/* Stores (frame.key, child) in the frame's object; false on OOM (child then still owned by the caller). */
static bool frame_store(parse_frame *frame, qa_save_value *child) {
  qa_save_value *v = frame->container;
  if ((v->entry_count + 1) * 2 > frame->index_capacity &&
      !frame_index_rebuild(frame, frame->index_capacity != 0 ? frame->index_capacity * 2 : 16))
    return false;
  size_t mask = frame->index_capacity - 1;
  size_t at = (size_t)key_hash(frame->key, frame->key_bytes) & mask;
  for (;; at = (at + 1) & mask) {
    size_t slot = frame->index[at];
    if (slot == 0) break;
    qa_save_entry *entry = &v->entries[slot - 1];
    if (text_bytes(entry->key, entry->key_length) == frame->key_bytes &&
        memcmp(entry->key, frame->key, frame->key_bytes) == 0) {
      qa_save_value_free(entry->value);
      entry->value = child;
      free(frame->key);
      frame->key = NULL;
      return true;
    }
  }
  qa_save_entry *entries = (qa_save_entry *)realloc(v->entries, (v->entry_count + 1) * sizeof *entries);
  if (entries == NULL) return false;
  v->entries = entries;
  v->entries[v->entry_count] = (qa_save_entry){frame->key, child, frame->key_length};
  frame->index[at] = ++v->entry_count;
  frame->key = NULL;
  return true;
}
static bool parse_push(parse_stack *stack, qa_save_value *container) {
  if (stack->count == stack->capacity) {
    size_t capacity = stack->capacity != 0 ? stack->capacity * 2 : 32;
    parse_frame *frames = (parse_frame *)realloc(stack->frames, capacity * sizeof *frames);
    if (frames == NULL) return false;
    stack->frames = frames;
    stack->capacity = capacity;
  }
  memset(&stack->frames[stack->count], 0, sizeof stack->frames[0]);
  stack->frames[stack->count++].container = container;
  return true;
}
/* Takes the current string token as the top frame's key and moves past it and the colon. */
static void parse_key(json_parser *p, parse_frame *frame) {
  frame->key = p->token_string;
  frame->key_bytes = p->token_string_length;
  frame->key_length = frame->key_bytes > strlen(frame->key) ? frame->key_bytes : 0;
  p->token_string = NULL;
  json_lex(p);
  if (p->token != JT_COLON) json_fail(p, "Expected ':' before value in object property definition");
  json_lex(p);
}
static qa_save_value *json_parse_value(json_parser *p) {
  parse_stack stack = {NULL, 0, 0, NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, parse_stack_free, &stack);
  qa_save_value *result = NULL;
  for (;;) {
    /* A value starts at the current token. */
    if (stack.count + 1 > p->max_depth) p->max_depth = stack.count + 1;
    json_token token = p->token;
    qa_save_value *v = NULL;
    if (token == JT_LBRACE || token == JT_LBRACKET) {
      qa_save_value *container = value_new(token == JT_LBRACE ? QA_SAVE_OBJECT : QA_SAVE_ARRAY);
      if (container == NULL) break;
      if (!parse_push(&stack, container)) { qa_save_value_free(container); break; }
      json_lex(p);
      if (token == JT_LBRACE) {
        if (p->token == JT_RBRACE) {
          json_lex(p);
          stack.count--;
          free(stack.frames[stack.count].index);
          v = container;
        } else {
          if (p->token != JT_STRING) json_fail(p, "Expected '}'");
          parse_key(p, &stack.frames[stack.count - 1]);
          continue;
        }
      } else {
        if (p->token == JT_RBRACKET) {
          json_lex(p);
          stack.count--;
          v = container;
        } else continue;
      }
    } else if (token == JT_STRING) {
      v = value_new(QA_SAVE_STRING);
      if (v == NULL) break;
      v->text = p->token_string;
      v->text_length = p->token_string_length > strlen(v->text) ? p->token_string_length : 0;
      p->token_string = NULL;
      stack.pending = v;
      json_lex(p);
    } else {
      if (token == JT_NUMBER) v = qa_save_value_number(p->token_number);
      else if (token == JT_TRUE) v = qa_save_value_boolean(true);
      else if (token == JT_FALSE) v = qa_save_value_boolean(false);
      else if (token == JT_NULL) v = value_new(QA_SAVE_NULL);
      else json_unexpected(p);
      if (v == NULL) break;
      stack.pending = v;
      json_lex(p);
    }
    /* `v` is finished: store it in its parent, closing the containers it completes. */
    stack.pending = v;
    bool next_value = false;
    while (!next_value) {
      if (stack.count == 0) {
        result = v;
        stack.pending = NULL;
        break;
      }
      parse_frame *top = &stack.frames[stack.count - 1];
      qa_save_value *parent = top->container;
      if (parent->kind == QA_SAVE_OBJECT) {
        if (!frame_store(top, v)) goto oom;
        stack.pending = NULL;
        if (p->token == JT_COMMA) {
          json_lex(p);
          if (p->token != JT_STRING) json_fail(p, "Property name must be a string literal");
          parse_key(p, top);
          next_value = true;
        } else if (p->token == JT_RBRACE) {
          json_lex(p);
          if (!js_order_entries(parent->entries, parent->entry_count)) goto oom; /* JSON.parse result key order */
          free(top->index);
          stack.count--;
          v = parent;
          stack.pending = v;
        } else
          json_fail(p, "Expected '}'");
      } else {
        if (!qa_save_value_push(parent, v)) goto oom;
        stack.pending = NULL;
        if (p->token == JT_COMMA) {
          json_lex(p);
          if (p->token == JT_RBRACKET) json_fail(p, "Unexpected comma at the end of array expression");
          next_value = true;
        } else if (p->token == JT_RBRACKET) {
          json_lex(p);
          stack.count--;
          v = parent;
          stack.pending = v;
        } else
          json_fail(p, "Expected ']'");
      }
    }
    if (!next_value) break;
  }
oom:
  qa_cleanup_pop(&guard);
  if (result == NULL) parse_stack_free(&stack);
  else free(stack.frames);
  return result;
}
/* LiteralParser::tryLiteralParse (StrictJSON): one value, then the end. */
static qa_save_value *json_document(json_parser *p) {
  json_lex(p);
  qa_save_value *parsed = json_parse_value(p);
  if (parsed == NULL) return NULL;
  if (p->token != JT_END) {
    qa_cleanup guard;
    qa_cleanup_push(&guard, cleanup_value, parsed);
    json_fail(p, "Unable to parse JSON string");
  }
  return parsed;
}

/* TS unpack(): transforms the raw tree in place where possible. */
static bool valid_bigint_text(const char *text) {
  size_t i = 0;
  if (text[i] == '-') i++;
  if (text[i] == '\0') return false;
  if (text[i] == '0') return text[i + 1] == '\0';
  if (text[i] < '1' || text[i] > '9') return false;
  for (i++; text[i] != '\0'; i++) if (text[i] < '0' || text[i] > '9') return false;
  return true;
}
/* The replacement for a tagged object (TS unpack's $qts branch), or NULL for OOM; raises the TS
 * errors with the object's path. */
static qa_save_value *unpack_tagged(const qa_save_value *value, const walk_frame *frames, size_t count) {
  const qa_save_value *tag = qa_save_value_get(value, "$qts");
  const qa_save_value *encoded = qa_save_value_get(value, "value");
  if (encoded == NULL || encoded->kind != QA_SAVE_STRING || value->entry_count != 2)
    walk_fail("checkpoint", frames, count, "invalid tagged checkpoint value");
  const char *tag_text = tag->kind == QA_SAVE_STRING && tag->text_length <= strlen(tag->text) ? tag->text : NULL;
  const char *enc = encoded->text;
  if (encoded->text_length > strlen(enc)) walk_fail("checkpoint", frames, count, "unknown tagged checkpoint value");
  qa_save_value *result;
  if (tag_text != NULL && strcmp(tag_text, "bigint") == 0 && valid_bigint_text(enc))
    result = qa_save_value_bigint_text(enc);
  else if (tag_text != NULL && strcmp(tag_text, "bytes") == 0 && canonical_base64(enc, strlen(enc))) {
    size_t length = 0;
    uint8_t *data = base64_decode(enc, strlen(enc), &length);
    if (length > 0 && data == NULL) return NULL;
    result = value_new(QA_SAVE_BYTES);
    if (result != NULL) { result->bytes = data; result->byte_length = length; }
    else free(data);
  } else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "-0") == 0)
    result = qa_save_value_number(-0.0);
  else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "NaN") == 0)
    result = qa_save_value_number(NAN);
  else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "Infinity") == 0)
    result = qa_save_value_number(INFINITY);
  else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "-Infinity") == 0)
    result = qa_save_value_number(-INFINITY);
  else
    walk_fail("checkpoint", frames, count, "unknown tagged checkpoint value");
  return result;
}
static noreturn void stack_exhausted(void) { qa_range_error("Maximum call stack size exceeded."); }
/* TS unpack(): a depth-first walk in TS order that replaces tagged objects in place. TS recurses
 * and exhausts the JS stack on a deep document (see QA_SAVE_VALUE_MAX_DEPTH); the walk raises
 * that RangeError on reaching a node deeper than the limit, after the errors of every node TS
 * visits first. False only on OOM (the tree stays whole and owned by the caller). */
static bool unpack_tree(qa_save_value **root) {
  walk_stack stack = {NULL, 0, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, walk_stack_free, &stack);
  bool ok = true;
  qa_save_value **slot = root;
  for (;;) {
    qa_save_value *value = *slot;
    if (stack.count + 1 > QA_SAVE_VALUE_MAX_DEPTH) stack_exhausted();
    if (value->kind == QA_SAVE_OBJECT && qa_save_value_get(value, "$qts") != NULL) {
      qa_save_value *replacement = unpack_tagged(value, stack.frames, stack.count);
      if (replacement == NULL) { ok = false; break; }
      qa_save_value_free(value);
      *slot = replacement;
    } else if (value->kind == QA_SAVE_ARRAY || value->kind == QA_SAVE_OBJECT) {
      if (!walk_push(&stack, value, NULL)) { ok = false; break; }
    }
    slot = NULL;
    while (stack.count > 0 && slot == NULL) {
      walk_frame *top = &stack.frames[stack.count - 1];
      qa_save_value *container = (qa_save_value *)top->value;
      size_t total = container->kind == QA_SAVE_ARRAY ? container->item_count : container->entry_count;
      if (top->next == total) {
        stack.count--;
        continue;
      }
      top->current = top->next++;
      slot = container->kind == QA_SAVE_ARRAY ? &container->items[top->current] : &container->entries[top->current].value;
    }
    if (slot == NULL) break;
  }
  qa_cleanup_pop(&guard);
  walk_stack_free(&stack);
  return ok;
}

static void cleanup_value_slot(void *arg) { qa_save_value_free(*(qa_save_value **)arg); }
typedef struct decode_job {
  const uint8_t *bytes;
  size_t length;
  qa_save_value *parsed;
  bool oom;
} decode_job;

static void decode_parse_run(void *raw) {
  decode_job *job = raw;
  char *text = dup_range((const char *)job->bytes, job->length);
  if (text == NULL) { job->oom = true; return; }
  qa_cleanup text_guard;
  qa_cleanup_push(&text_guard, cleanup_text, text);
  json_parser parser;
  memset(&parser, 0, sizeof parser);
  parser.text = text;
  parser.length = job->length;
  qa_cleanup token_guard;
  qa_cleanup_push(&token_guard, cleanup_token, &parser);
  qa_save_value *parsed = json_document(&parser);
  qa_cleanup_pop(&token_guard);
  cleanup_token(&parser);
  if (parsed == NULL) { qa_cleanup_pop(&text_guard); free(text); job->oom = true; return; }
  qa_cleanup_pop(&text_guard);
  free(text);
  job->parsed = parsed;
}

/* TS decodeCheckpointValue wraps the fatal TextDecoder and JSON.parse
 * failures as SaveFormatError("checkpoint", error.message); both texts are
 * Bun's (the parser above reproduces JavaScriptCore's JSON.parse messages). */
qa_save_value *qa_decode_checkpoint_value(const uint8_t *bytes, size_t length) {
  if (!utf8_valid(bytes, length)) qa_save_format_error("checkpoint", "Invalid byte sequence");
  /* TextDecoder("utf-8") drops one leading byte-order mark before JSON.parse sees the text. */
  if (length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
    bytes += 3;
    length -= 3;
  }
  decode_job job = {bytes, length, NULL, false};
  qa_caught_error caught;
  if (!qa_try(decode_parse_run, &job, &caught)) {
    if (strcmp(caught.name, "SyntaxError") != 0) qa_rethrow(&caught);
    qa_save_format_error("checkpoint", "%s", qa_caught_message(&caught));
  }
  if (job.oom) return NULL;
  /* unpack_tree replaces tagged nodes as it goes; a raise in it leaves the
   * tree as it was then, released by this guard. */
  qa_save_value *parsed = job.parsed;
  qa_cleanup parsed_guard;
  qa_cleanup_push(&parsed_guard, cleanup_value_slot, &parsed);
  bool unpacked = unpack_tree(&parsed);
  qa_cleanup_pop(&parsed_guard);
  if (!unpacked) {
    qa_save_value_free(parsed);
    return NULL;
  }
  return parsed;
}

typedef struct json_parse_scope { const char *text; size_t length; qa_save_value *parsed; } json_parse_scope;
static void json_parse_body(void *ctx) {
  json_parse_scope *scope = ctx;
  json_parser parser;
  memset(&parser, 0, sizeof parser);
  parser.text = scope->text;
  parser.length = scope->length;
  qa_cleanup token_guard;
  qa_cleanup_push(&token_guard, cleanup_token, &parser);
  qa_save_value *parsed = json_document(&parser);
  qa_cleanup_pop(&token_guard);
  cleanup_token(&parser);
  /* JSON.parse itself has no depth limit, but every TS consumer of a parsed document walks it on
   * the JS stack; a document past QA_SAVE_VALUE_MAX_DEPTH fails as their recursion would. */
  if (parsed != NULL && parser.max_depth > QA_SAVE_VALUE_MAX_DEPTH) {
    qa_save_value_free(parsed);
    stack_exhausted();
  }
  scope->parsed = parsed;
}
qa_save_value *qa_parse_json_value(const uint8_t *bytes, size_t length, bool quiet) {
  char *text = dup_range((const char *)bytes, length);
  if (text == NULL) return NULL;
  json_parse_scope scope = {text, length, NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_text, text);
  if (quiet) {
    qa_caught_error caught;
    if (!qa_try(json_parse_body, &scope, &caught)) scope.parsed = NULL;
  } else json_parse_body(&scope);
  qa_cleanup_pop(&guard);
  free(text);
  return scope.parsed;
}

/* --- SaveReader --- */
/* A child reader's path: the parent's path plus `segment`. It is kept inline when it fits;
 * otherwise a node holding the segment (or, under a parent whose path still fits inline, the
 * whole path) is hung on `owner`, the node the reader is opened under, which outlives the
 * reader. A recent node with the same parent and text is reused, so reading one field again
 * and again does not grow the list. */
static void reader_set_path(qa_save_reader *out, const qa_save_reader *parent, const qa_save_value *owner,
                            const char *segment, size_t segment_length) {
  size_t inline_length = strlen(parent->path);
  if (parent->long_path == NULL && inline_length + segment_length < sizeof out->path) {
    memcpy(out->path, parent->path, inline_length);
    memcpy(out->path + inline_length, segment, segment_length);
    out->path[inline_length + segment_length] = '\0';
    out->long_path = NULL;
    return;
  }
  /* The inline text keeps the first bytes of the path. */
  size_t head = inline_length;
  if (head < sizeof out->path - 1) {
    size_t take = segment_length < sizeof out->path - 1 - head ? segment_length : sizeof out->path - 1 - head;
    memmove(out->path, parent->path, head);
    memcpy(out->path + head, segment, take);
    head += take;
  } else
    memmove(out->path, parent->path, head);
  out->path[head] = '\0';
  out->long_path = NULL;
  if (owner == NULL) return;
  const struct qa_save_path_node *base = parent->long_path;
  size_t prefix = base == NULL ? inline_length : 0;
  qa_save_value *holder = (qa_save_value *)owner; /* the path list is not data (see value.h) */
  size_t scanned = 0;
  for (struct qa_save_path_node *node = holder->reader_paths; node != NULL && scanned < 8; node = node->next, scanned++)
    if (node->parent == base && node->length == prefix + segment_length &&
        (prefix == 0 || memcmp(node->text, parent->path, prefix) == 0) &&
        memcmp(node->text + prefix, segment, segment_length) == 0) {
      out->long_path = node;
      return;
    }
  struct qa_save_path_node *node = (struct qa_save_path_node *)malloc(sizeof *node + prefix + segment_length + 1);
  if (node == NULL) return; /* the error text keeps the inline head */
  node->parent = base;
  node->length = prefix + segment_length;
  memcpy(node->text, parent->path, prefix);
  memcpy(node->text + prefix, segment, segment_length);
  node->text[node->length] = '\0';
  node->next = holder->reader_paths;
  holder->reader_paths = node;
  out->long_path = node;
}
bool qa_save_reader_init(qa_save_reader *reader, const qa_save_value *value, const char *path) {
  reader->value = value;
  const char *text = path != NULL ? path : "save";
  qa_save_reader root;
  root.path[0] = '\0';
  root.long_path = NULL;
  root.value = NULL;
  reader_set_path(reader, &root, value, text, strlen(text));
  reader->value = value;
  return true;
}
void qa_save_reader_destroy(qa_save_reader *reader) {
  reader->path[0] = '\0';
  reader->long_path = NULL;
  reader->value = NULL;
}
char *qa_save_reader_path(const qa_save_reader *reader) {
  if (reader->long_path == NULL) return dup_text(reader->path);
  size_t total = 0;
  for (const struct qa_save_path_node *node = reader->long_path; node != NULL; node = node->parent) total += node->length;
  char *text = (char *)malloc(total + 1);
  if (text == NULL) return NULL;
  text[total] = '\0';
  size_t end = total;
  for (const struct qa_save_path_node *node = reader->long_path; node != NULL; node = node->parent) {
    end -= node->length;
    memcpy(text + end, node->text, node->length);
  }
  return text;
}
noreturn void qa_save_reader_fail(const qa_save_reader *reader, const char *format, ...) {
  char *path = reader->long_path != NULL ? qa_save_reader_path(reader) : NULL;
  qa_cleanup guard; /* the raise formats the message before its cleanups run */
  qa_cleanup_push(&guard, free, path);
  va_list args;
  va_start(args, format);
  save_format_errorv(path != NULL ? path : reader->path, format, args);
}
static const qa_save_value *reader_value(const qa_save_reader *reader) { return reader->value; }
bool qa_save_reader_is_undefined(const qa_save_reader *reader) {
  return reader->value == NULL || reader->value->kind == QA_SAVE_UNDEFINED;
}
bool qa_save_reader_is_null(const qa_save_reader *reader) {
  return reader->value != NULL && reader->value->kind == QA_SAVE_NULL;
}
bool qa_save_reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_OBJECT)
    qa_save_reader_fail(reader, "expected a record");
  size_t length = strlen(name);
  char small[128];
  char *segment = length + 1 <= sizeof small ? small : (char *)malloc(length + 1);
  if (segment == NULL) return false;
  segment[0] = '.';
  memcpy(segment + 1, name, length);
  const qa_save_value *child = qa_save_value_get(value, name);
  qa_save_reader parent = *reader;
  reader_set_path(out, &parent, value, segment, length + 1);
  out->value = child;
  if (segment != small) free(segment);
  return true;
}
size_t qa_save_reader_list_count(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_ARRAY)
    qa_save_reader_fail(reader, "expected an array");
  return value->item_count;
}
bool qa_save_reader_item(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_ARRAY)
    qa_save_reader_fail(reader, "expected an array");
  char segment[32];
  int length = snprintf(segment, sizeof segment, "[%zu]", index);
  const qa_save_value *child = index < value->item_count ? value->items[index] : NULL;
  qa_save_reader parent = *reader;
  reader_set_path(out, &parent, value, segment, (size_t)length);
  out->value = child;
  return true;
}
const char *qa_save_reader_string(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_STRING)
    qa_save_reader_fail(reader, "expected a string");
  return value->text;
}
const char *qa_save_reader_string_length(const qa_save_reader *reader, size_t *out_length) {
  const char *text = qa_save_reader_string(reader);
  if (out_length != NULL) *out_length = text_bytes(text, reader->value->text_length);
  return text;
}
bool qa_save_reader_boolean(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BOOLEAN)
    qa_save_reader_fail(reader, "expected a boolean");
  return value->boolean;
}
double qa_save_reader_number(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_NUMBER)
    qa_save_reader_fail(reader, "expected a number");
  return value->number;
}
double qa_save_reader_finite(const qa_save_reader *reader) {
  double value = qa_save_reader_number(reader);
  if (!isfinite(value)) qa_save_reader_fail(reader, "expected a finite number");
  return value;
}
double qa_save_reader_integer_min(const qa_save_reader *reader, double minimum) {
  double value = qa_save_reader_number(reader);
  if (!(value == trunc(value) && fabs(value) <= 9007199254740991.0) || value < minimum)
    qa_save_reader_fail(reader, "expected an integer in range");
  return value;
}
double qa_save_reader_integer(const qa_save_reader *reader) {
  return qa_save_reader_integer_min(reader, -9007199254740991.0);
}
const char *qa_save_reader_bigint_text(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BIGINT)
    qa_save_reader_fail(reader, "expected a bigint");
  return value->text;
}
int64_t qa_save_reader_bigint(const qa_save_reader *reader) {
  return (int64_t)strtoll(qa_save_reader_bigint_text(reader), NULL, 10);
}
bool qa_save_reader_bigint_u64(const qa_save_reader *reader, uint64_t *out) {
  const char *text = qa_save_reader_bigint_text(reader);
  *out = 0;
  if (*text == '-') return false; /* negative (canonical text has no -0) */
  uint64_t value = 0;
  for (; *text >= '0' && *text <= '9'; text++) {
    unsigned digit = (unsigned)(*text - '0');
    if (value > (UINT64_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  *out = value;
  return true;
}
const uint8_t *qa_save_reader_bytes(const qa_save_reader *reader, size_t *out_length) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BYTES)
    qa_save_reader_fail(reader, "expected raw checkpoint bytes");
  if (out_length != NULL) *out_length = value->byte_length;
  return value->bytes;
}
/* A string holding a NUL (text_length beyond strlen) equals no code-given text. */
static bool string_is(const qa_save_value *value, const char *expected) {
  return value->text_length <= strlen(value->text) && strcmp(value->text, expected) == 0;
}
static void free_message(void *arg) { free(((text_buffer *)arg)->data); }
const char *qa_save_reader_literal_string(const qa_save_reader *reader, const char *expected) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_STRING || !string_is(value, expected))
    qa_save_reader_fail(reader, "expected %s", expected);
  return expected;
}
double qa_save_reader_literal_number(const qa_save_reader *reader, double expected) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_NUMBER || value->number != expected) {
    char buffer[64];
    format_js_number(expected, buffer, sizeof buffer);
    qa_save_reader_fail(reader, "expected %s", buffer);
  }
  return expected;
}
bool qa_save_reader_literal_boolean(const qa_save_reader *reader, bool expected) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BOOLEAN || value->boolean != expected)
    qa_save_reader_fail(reader, "expected %s", expected ? "true" : "false");
  return expected;
}
const char *qa_save_reader_choice_string(const qa_save_reader *reader,
                                         const char *const *choices, size_t count) {
  const qa_save_value *value = reader_value(reader);
  if (value != NULL && value->kind == QA_SAVE_STRING)
    for (size_t i = 0; i < count; i++)
      if (string_is(value, choices[i])) return choices[i];
  /* The TS "a or b or c" message, whole; the raise formats it before its cleanups free it. */
  text_buffer message = {NULL, 0, 0};
  (void)text_add_cstr(&message, "expected ");
  for (size_t i = 0; i < count; i++) {
    if (i > 0) (void)text_add_cstr(&message, " or ");
    (void)text_add_cstr(&message, choices[i]);
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_message, &message);
  qa_save_reader_fail(reader, "%s", message.data != NULL ? message.data : "expected");
}
double qa_save_reader_choice_number(const qa_save_reader *reader,
                                    const double *choices, size_t count) {
  const qa_save_value *value = reader_value(reader);
  if (value != NULL && value->kind == QA_SAVE_NUMBER)
    for (size_t i = 0; i < count; i++)
      if (value->number == choices[i]) return choices[i];
  text_buffer message = {NULL, 0, 0};
  (void)text_add_cstr(&message, "expected ");
  for (size_t i = 0; i < count; i++) {
    char buffer[64];
    format_js_number(choices[i], buffer, sizeof buffer);
    if (i > 0) (void)text_add_cstr(&message, " or ");
    (void)text_add_cstr(&message, buffer);
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_message, &message);
  qa_save_reader_fail(reader, "%s", message.data != NULL ? message.data : "expected");
}
/* TS indexOf(":") over the whole string (a NUL is an ordinary character there). */
const char *qa_namespaced(const qa_save_reader *reader) {
  size_t length = 0;
  const char *value = qa_save_reader_string_length(reader, &length);
  const char *colon = (const char *)memchr(value, ':', length);
  if (colon == NULL || colon == value || (size_t)(colon - value) == length - 1)
    qa_save_reader_fail(reader, "expected a namespaced identity");
  return value;
}



