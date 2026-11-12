/* Port of src/guest/runtime/common/format.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/common/format.h"

#include <ctype.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"
#include "qa/foundation/js.h"

/* TS `throw new GuestFormatFortifyFailure(message)` (error.name). */
void qa_guest_format_fortify_failure(const char *message) { qa_raise("GuestFormatFortifyFailure", 0, 0, "%s", message); }

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Guest runtime out of memory");
  return next;
}

/* Byte strings (may contain NULs from %c). */
typedef struct bstr { char *data; size_t len, cap; } bstr;
static void bstr_init(bstr *s) { s->data = NULL; s->len = 0; s->cap = 0; }
static void bstr_reserve(bstr *s, size_t want) {
  if (want <= s->cap) return;
  size_t cap = s->cap == 0 ? 32 : s->cap;
  while (cap < want) cap *= 2;
  s->data = xrealloc(s->data, cap);
  s->cap = cap;
}
static void bstr_append(bstr *s, const char *bytes, size_t len) {
  bstr_reserve(s, s->len + len + 1);
  memcpy(s->data + s->len, bytes, len);
  s->len += len;
  s->data[s->len] = '\0';
}
static void bstr_append_str(bstr *s, const char *text) { bstr_append(s, text, strlen(text)); }
static void bstr_free(bstr *s) { free(s->data); bstr_init(s); }

/* Internal FormatFailure / BufferFull throws. */
typedef struct format_throw {
  jmp_buf buf;
  int kind; /* 1 failure, 2 full */
  int errno_value;
} format_throw;
static format_throw *g_throw;
static void format_failure(int errno_value) {
  if (g_throw == NULL) qa_range_error("Invalid guest printf conversion");
  g_throw->kind = 1;
  g_throw->errno_value = errno_value;
  longjmp(g_throw->buf, 1);
}
static void buffer_full(void) {
  if (g_throw == NULL) qa_range_error("Guest printf buffer full");
  g_throw->kind = 2;
  longjmp(g_throw->buf, 1);
}

typedef struct reference { int index; qa_format_argument_type type; } reference;
typedef struct amount { bool is_argument; int64_t value; reference ref; } amount;
typedef struct conversion {
  char code;
  char length[4];
  char flags[16];
  amount width;
  bool has_precision;
  amount precision;
  bool has_argument;
  reference argument;
} conversion;
typedef struct token {
  bool is_literal;
  bstr text;
  conversion conv;
} token;
typedef struct parsed_format {
  token *tokens; size_t token_count, token_cap;
  bool positional;
  reference *references; size_t reference_count, reference_cap;
} parsed_format;

static int integer_bits(const char *length, const qa_guest_format_request *request) {
  if (strcmp(length, "hh") == 0) return 8;
  if (strcmp(length, "h") == 0) return 16;
  if (strcmp(length, "ll") == 0 || strcmp(length, "j") == 0 || strcmp(length, "I64") == 0 || strcmp(length, "q") == 0) return 64;
  if (strcmp(length, "L") == 0) return request->dialect == QA_FORMAT_SYSTEM_V ? 64 : 32;
  if (strcmp(length, "l") == 0) return request->dialect == QA_FORMAT_WINDOWS ? 32 : request->memory->pointer_bytes * 8;
  if (strcmp(length, "z") == 0 || strcmp(length, "t") == 0 || strcmp(length, "I") == 0)
    return request->memory->pointer_bytes * 8;
  return 32;
}

typedef struct parser {
  const char *text;
  size_t length, at;
  parsed_format *parsed;
  const qa_guest_format_request *request;
  int sequence;
  bool positional, sequential;
} parser;

static void push_reference(parser *p, reference ref) {
  parsed_format *parsed = p->parsed;
  if (parsed->reference_count == parsed->reference_cap) {
    parsed->reference_cap = parsed->reference_cap == 0 ? 8 : parsed->reference_cap * 2;
    parsed->references = xrealloc(parsed->references, parsed->reference_cap * sizeof *parsed->references);
  }
  parsed->references[parsed->reference_count++] = ref;
}
static void push_token(parser *p, token tok) {
  parsed_format *parsed = p->parsed;
  if (parsed->token_count == parsed->token_cap) {
    parsed->token_cap = parsed->token_cap == 0 ? 8 : parsed->token_cap * 2;
    parsed->tokens = xrealloc(parsed->tokens, parsed->token_cap * sizeof *parsed->tokens);
  }
  parsed->tokens[parsed->token_count++] = tok;
}
static int64_t parse_number(parser *p) {
  int64_t value = 0;
  while (p->at < p->length && isdigit((unsigned char)p->text[p->at])) {
    value = value * 10 + (p->text[p->at] - '0');
    p->at++;
    if (value > 0x7fffffffLL)
      format_failure(p->request->dialect == QA_FORMAT_WINDOWS ? 132 : 75);
  }
  return value;
}
static bool parse_position(parser *p, int *out) {
  size_t start = p->at;
  int64_t value = parse_number(p);
  if (p->at >= p->length || p->text[p->at] != '$') { p->at = start; return false; }
  if (p->request->dialect == QA_FORMAT_WINDOWS || value < 1 || value > 4096) format_failure(22);
  p->at++;
  *out = (int)value - 1;
  return true;
}
static reference make_reference(parser *p, qa_format_argument_type type, bool has_index, int index) {
  if (!has_index) p->sequential = true; else p->positional = true;
  if (p->positional && p->sequential) format_failure(22);
  reference ref = {has_index ? index : p->sequence++, type};
  push_reference(p, ref);
  return ref;
}
static amount parse_amount(parser *p) {
  amount result; memset(&result, 0, sizeof result);
  if (p->at >= p->length || p->text[p->at] != '*') {
    result.value = parse_number(p);
    return result;
  }
  p->at++;
  int index = 0;
  bool has_index = parse_position(p, &index);
  result.is_argument = true;
  result.ref = make_reference(p, QA_FORMAT_ARG_INT, has_index, index);
  return result;
}

static void parse_format(parsed_format *parsed, const char *text, const qa_guest_format_request *request) {
  memset(parsed, 0, sizeof *parsed);
  parser p; memset(&p, 0, sizeof p);
  p.text = text; p.length = strlen(text); p.parsed = parsed; p.request = request;
  static const char *const candidates[] = {"I64", "I32", "hh", "ll", "h", "l", "L", "j", "z", "t", "I", "w", "q"};
  while (p.at < p.length) {
    size_t start = p.at;
    while (p.at < p.length && p.text[p.at] != '%') p.at++;
    if (p.at > start) {
      token tok; memset(&tok, 0, sizeof tok);
      tok.is_literal = true;
      bstr_init(&tok.text);
      bstr_append(&tok.text, p.text + start, p.at - start);
      push_token(&p, tok);
    }
    if (p.at == p.length) break;
    p.at++;
    if (p.at < p.length && p.text[p.at] == '%') {
      token tok; memset(&tok, 0, sizeof tok);
      tok.is_literal = true;
      bstr_init(&tok.text);
      bstr_append(&tok.text, "%", 1);
      push_token(&p, tok);
      p.at++;
      continue;
    }
    int position = 0;
    bool has_position = parse_position(&p, &position);
    conversion conv; memset(&conv, 0, sizeof conv);
    size_t flag_len = 0;
    while (p.at < p.length && strchr("-+ #0'", p.text[p.at]) != NULL) {
      conv.flags[flag_len] = '\0';
      if (strchr(conv.flags, p.text[p.at]) == NULL) conv.flags[flag_len++] = p.text[p.at]; /* six distinct flags at most */
      p.at++;
    }
    conv.flags[flag_len] = '\0';
    conv.width = parse_amount(&p);
    if (p.at < p.length && p.text[p.at] == '.') {
      p.at++;
      conv.has_precision = true;
      conv.precision = parse_amount(&p);
    }
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
      size_t len = strlen(candidates[i]);
      if (p.at + len <= p.length && strncmp(p.text + p.at, candidates[i], len) == 0) {
        memcpy(conv.length, candidates[i], len + 1);
        p.at += len;
        break;
      }
    }
    char code = p.at < p.length ? p.text[p.at++] : '\0';
    if (code == '\0' || strchr("diouxXfFeEgGaAcCsSpn", code) == NULL) format_failure(22);
    if (request->dialect == QA_FORMAT_SYSTEM_V
        && (strcmp(conv.length, "I") == 0 || strcmp(conv.length, "I32") == 0
            || strcmp(conv.length, "I64") == 0 || strcmp(conv.length, "w") == 0))
      format_failure(22);
    qa_format_argument_type type;
    if (strchr("fFeEgGaA", code) != NULL)
      type = strcmp(conv.length, "L") == 0 ? QA_FORMAT_ARG_LONG_DOUBLE : QA_FORMAT_ARG_DOUBLE;
    else if (strchr("sSpn", code) != NULL) type = QA_FORMAT_ARG_POINTER;
    else if (strchr("cC", code) != NULL) type = QA_FORMAT_ARG_INT;
    else type = integer_bits(conv.length, request) == 64 ? QA_FORMAT_ARG_INT64 : QA_FORMAT_ARG_INT;
    conv.code = code;
    conv.has_argument = true;
    conv.argument = make_reference(&p, type, has_position, position);
    token tok; memset(&tok, 0, sizeof tok);
    tok.conv = conv;
    push_token(&p, tok);
  }
  parsed->positional = p.positional;
}
static void parsed_format_free(parsed_format *parsed) {
  for (size_t i = 0; i < parsed->token_count; i++) bstr_free(&parsed->tokens[i].text);
  free(parsed->tokens);
  free(parsed->references);
  memset(parsed, 0, sizeof *parsed);
}

/* TS retainedFormat: per dialect and pointer width, FIFO over 256 sources of at
 * most 4096 bytes. Entries live for the process, as the TS module-level maps do. */
#define RETAINED_FORMAT_CAPACITY 256u
#define RETAINED_FORMAT_LIMIT 4096u
typedef struct retained_format_table {
  char *keys[RETAINED_FORMAT_CAPACITY];
  uint32_t hashes[RETAINED_FORMAT_CAPACITY];
  parsed_format values[RETAINED_FORMAT_CAPACITY];
  size_t count, oldest;
} retained_format_table;
static retained_format_table *retained_formats[2][2];

static uint32_t format_text_hash(const char *text) {
  uint32_t hash = 2166136261u;
  for (const unsigned char *at = (const unsigned char *)text; *at != 0; at++) hash = (hash ^ *at) * 16777619u;
  return hash;
}

/* Returns the parse to use. *scratch receives a source too long to retain, and
 * *scratch_used tells the caller to release it. */
static const parsed_format *retained_format(const char *text, const qa_guest_format_request *request,
                                            parsed_format *scratch, bool *scratch_used) {
  *scratch_used = false;
  if (strlen(text) > RETAINED_FORMAT_LIMIT) {
    *scratch_used = true; /* before the parse: a conversion that fails part way is released with the run */
    parse_format(scratch, text, request);
    return scratch;
  }
  retained_format_table **slot =
      &retained_formats[request->dialect == QA_FORMAT_WINDOWS ? 1 : 0][request->memory->pointer_bytes == 8 ? 1 : 0];
  if (*slot == NULL) {
    *slot = (retained_format_table *)calloc(1, sizeof **slot);
    if (*slot == NULL) qa_range_error("Out of memory");
  }
  retained_format_table *table = *slot;
  uint32_t hash = format_text_hash(text);
  for (size_t i = 0; i < table->count; i++)
    if (table->hashes[i] == hash && strcmp(table->keys[i], text) == 0) return &table->values[i];
  *scratch_used = true; /* the parse is the run's until the table takes it */
  parse_format(scratch, text, request);
  const parsed_format parsed = *scratch;
  size_t length = strlen(text);
  char *key = (char *)malloc(length + 1);
  if (key == NULL) qa_range_error("Out of memory");
  memcpy(key, text, length + 1);
  memset(scratch, 0, sizeof *scratch);
  *scratch_used = false;
  size_t at;
  if (table->count == RETAINED_FORMAT_CAPACITY) {
    at = table->oldest;
    table->oldest = (table->oldest + 1) % RETAINED_FORMAT_CAPACITY;
    free(table->keys[at]);
    parsed_format_free(&table->values[at]);
  } else {
    at = table->count++;
  }
  table->keys[at] = key;
  table->hashes[at] = hash;
  table->values[at] = parsed;
  return &table->values[at];
}

typedef struct output {
  const qa_guest_format_request *request;
  int64_t total, used;
} output;

static void output_check_total(output *out) {
  if (out->total > 0x7fffffffLL)
    format_failure(out->request->dialect == QA_FORMAT_WINDOWS ? 132 : 75);
}
static void output_append(output *out, const char *text, size_t length) {
  if (length == 0) return;
  const qa_guest_format_request *r = out->request;
  uint64_t available = !r->has_buffer ? 0 : (r->capacity > (uint64_t)out->used ? r->capacity - (uint64_t)out->used : 0);
  size_t copied = available < length ? (size_t)available : length;
  if (r->has_buffer && copied > 0) {
    qa_memory_write(r->memory, qa_memory_offset(r->memory, r->buffer, out->used),
                    (const uint8_t *)text, copied);
    out->used += (int64_t)copied;
  }
  if (r->has_buffer && r->termination != QA_TERMINATION_C99 && !r->continue_count && copied < length)
    buffer_full();
  out->total += (int64_t)length;
  output_check_total(out);
}
static void output_repeat(output *out, char character, int64_t count) {
  if (count <= 0) return;
  const qa_guest_format_request *r = out->request;
  char chunk[4096];
  memset(chunk, character, sizeof chunk);
  if (r->termination == QA_TERMINATION_C99 || r->continue_count || !r->has_buffer) {
    uint64_t available = !r->has_buffer || r->capacity <= (uint64_t)out->used ? 0 : r->capacity - (uint64_t)out->used;
    int64_t copied = (uint64_t)count < available ? count : (int64_t)available;
    for (int64_t at = 0; at < copied; at += 4096) {
      int64_t piece = copied - at < 4096 ? copied - at : 4096;
      output_append(out, chunk, (size_t)piece);
    }
    out->total += count - copied;
    output_check_total(out);
  } else {
    for (int64_t at = 0; at < count; at += 4096) {
      int64_t piece = count - at < 4096 ? count - at : 4096;
      output_append(out, chunk, (size_t)piece);
    }
  }
}
static qa_guest_format_result output_finish(output *out, bool has_error, int error, bool full) {
  const qa_guest_format_request *r = out->request;
  qa_guest_format_result result;
  result.has_errno = has_error;
  result.errno_value = error;
  if (!r->has_buffer) {
    result.result = !has_error ? (int)out->total : -1;
    return result;
  }
  const uint64_t capacity = r->capacity, used = (uint64_t)out->used;
  if (r->termination == QA_TERMINATION_UCRT_LEGACY) {
    if (used < capacity)
      qa_memory_write_u8(r->memory, qa_memory_offset(r->memory, r->buffer, out->used), 0);
    result.result = (has_error || full || (uint64_t)out->total > capacity) ? -1 : (int)out->total;
    return result;
  }
  if (capacity > 0) {
    int64_t at = (has_error && r->dialect == QA_FORMAT_WINDOWS && r->termination == QA_TERMINATION_C99) ? 0
        : used >= capacity ? (int64_t)(capacity - 1) : out->used;
    qa_memory_write_u8(r->memory, qa_memory_offset(r->memory, r->buffer, at), 0);
  }
  if (r->termination == QA_TERMINATION_UCRT && (capacity == 0 || used == capacity)) {
    result.result = capacity == 0 ? -1 : -2;
    return result;
  }
  result.result = !has_error ? (int)out->total : -1;
  return result;
}

static uint64_t argument_integer(const qa_format_argument *value) {
  if (value->is_float) format_failure(22);
  return value->integer;
}
static int64_t as_int_n(uint64_t value, int bits) {
  if (bits >= 64) return (int64_t)value;
  uint64_t sign = 1ULL << (bits - 1);
  return (int64_t)(((value & ((sign << 1) - 1)) ^ sign) - sign);
}
static uint64_t as_uint_n(uint64_t value, int bits) {
  return bits >= 64 ? value : value & ((1ULL << bits) - 1);
}

static int cmp_mapping(const void *a, const void *b) {
  uint64_t x = ((const qa_guest_mapping *)a)->base, y = ((const qa_guest_mapping *)b)->base;
  return x < y ? -1 : x > y ? 1 : 0;
}
static bool readonly_format(const qa_guest_format_request *request, size_t length) {
  if (!request->has_format) return false;
  size_t count = 0;
  const qa_guest_mapping *live = qa_gmem_mappings(request->memory_ext, &count);
  if (live == NULL) return false;
  qa_guest_mapping *sorted = xmalloc(count * sizeof *sorted);
  memcpy(sorted, live, count * sizeof *sorted);
  qa_js_sort(sorted, count, sizeof *sorted, cmp_mapping);
  uint64_t cursor = request->format.byte_offset;
  uint64_t end = cursor + length + 1;
  bool result = false;
  for (size_t i = 0; i < count; i++) {
    uint64_t stop = sorted[i].base + sorted[i].byte_length;
    if (stop <= cursor) continue;
    if (sorted[i].base > cursor || qa_guest_permissions_has_write(sorted[i].permissions)) break;
    cursor = stop;
    if (cursor >= end) { result = true; break; }
  }
  free(sorted);
  return result;
}

static void string_argument(bstr *out_text, const qa_guest_format_request *request,
                            bool has_address, qa_guest_core_address address,
                            bool wide, bool has_precision, int64_t precision) {
  bstr_init(out_text);
  if (!has_address) {
    if (has_precision && request->dialect == QA_FORMAT_SYSTEM_V && precision < 6) return;
    size_t take = has_precision ? (size_t)precision : 6;
    if (take > 6) take = 6;
    bstr_append(out_text, "(null)", take);
    return;
  }
  qa_mapped_guest_memory *m = request->memory;
  int width = wide ? (request->dialect == QA_FORMAT_WINDOWS ? 2 : 4) : 1;
  if (!wide && m->managed_sparse && request->memory_ext != NULL && request->memory_ext->find_zero != NULL) {
    if (has_precision && precision == 0) return;
    size_t maximum = has_precision ? (size_t)precision : 1048576;
    int64_t length = request->memory_ext->find_zero(request->memory_ext->ctx, address, maximum);
    if (length < 0 && !has_precision) qa_range_error("Guest printf string exceeds runtime string limit");
    size_t take = length < 0 ? (has_precision ? (size_t)precision : 0) : (size_t)length;
    bstr_reserve(out_text, take + 1);
    out_text->data[0] = '\0';
    if (take > 0) {
      qa_memory_copy(m, address, (uint8_t *)out_text->data, take);
      out_text->len = take;
      out_text->data[take] = '\0';
    }
    return;
  }
  for (int64_t index = 0; !has_precision || (int64_t)out_text->len < precision; index++) {
    if (index >= 1048576) qa_range_error("Guest printf string exceeds runtime string limit");
    qa_guest_core_address at = qa_memory_offset(m, address, index * width);
    uint32_t code = width == 4 ? qa_memory_read_u32(m, at) : width == 2 ? qa_memory_read_u16(m, at)
                                                                        : qa_memory_read_u8(m, at);
    if (code == 0) break;
    if (wide && code > (uint32_t)(request->dialect == QA_FORMAT_WINDOWS ? 255 : 127))
      format_failure(request->dialect == QA_FORMAT_WINDOWS ? 42 : 84);
    char ch = (char)code;
    bstr_append(out_text, &ch, 1);
  }
}

static void uint_to_radix(bstr *out_text, uint64_t value, int radix, bool upper) {
  char reversed[64];
  size_t n = 0;
  if (value == 0) reversed[n++] = '0';
  while (value != 0) {
    unsigned digit = (unsigned)(value % (uint64_t)radix);
    reversed[n++] = (char)(digit < 10 ? '0' + (int)digit : (upper ? 'A' : 'a') + (int)digit - 10);
    value /= (uint64_t)radix;
  }
  bstr_init(out_text);
  for (size_t i = n; i-- > 0;) bstr_append(out_text, &reversed[i], 1);
}

/* Everything one run owns. It is released when the run ends, by the printf failure exits (a local longjmp)
 * and by a raise (a guest fault while a string or the va_list is read). */
typedef struct format_scope {
  char *text;
  parsed_format long_format;
  bool long_format_used;
  qa_format_argument *values;
  size_t value_count;
  qa_format_argument_type *types;
  bool *seen;
  bstr body, prefix, padded;
  qa_format_argument value;
  bool value_owned;
  char *formatted;
  format_throw *previous_throw;
} format_scope;
static void format_scope_release(void *arg) {
  format_scope *scope = arg;
  free(scope->text);
  if (scope->long_format_used) parsed_format_free(&scope->long_format);
  for (size_t i = 0; i < scope->value_count; i++) qa_format_argument_free(&scope->values[i]);
  free(scope->values);
  free(scope->types);
  free(scope->seen);
  bstr_free(&scope->body);
  bstr_free(&scope->prefix);
  bstr_free(&scope->padded);
  if (scope->value_owned) qa_format_argument_free(&scope->value);
  free(scope->formatted);
  g_throw = scope->previous_throw;
  memset(scope, 0, sizeof *scope);
}

typedef struct format_run {
  const qa_guest_format_request *request;
  qa_format_arguments *reader;
  bool positional;
  qa_format_argument *values;
  size_t value_count;
} format_run;

static qa_format_argument get_argument(format_run *run, reference ref) {
  if (!run->positional) {
    if (run->reader == NULL) qa_raise("Error", 0, 0, "Missing printf argument reader");
    return qa_format_arguments_next(run->reader, ref.type);
  }
  if (ref.index < 0 || (size_t)ref.index >= run->value_count) format_failure(22);
  qa_format_argument value = run->values[ref.index];
  /* Borrow: clear ownership in the stored copy so frees stay single. The
   * caller of get_argument does not free positional values. */
  return value;
}
static int64_t amount_value(format_run *run, amount value) {
  if (!value.is_argument) return value.value;
  qa_format_argument argument = get_argument(run, value.ref);
  int64_t result = as_int_n(argument_integer(&argument), 32);
  if (!run->positional) qa_format_argument_free(&argument);
  return result;
}

static qa_guest_format_result format_run_body(const qa_guest_format_request *request, output *out, format_scope *scope) {
  char *text = scope->text = qa_read_string(request->memory, request->memory_ext, request->format, 1048576);
  const parsed_format *retained = retained_format(text, request, &scope->long_format, &scope->long_format_used);
  const parsed_format parsed = *retained;
  qa_format_arguments reader;
  bool has_reader = parsed.reference_count > 0;
  if (has_reader)
    qa_format_arguments_init(&reader, request->memory, request->dialect,
                             request->has_arguments, request->arguments);
  format_run run; memset(&run, 0, sizeof run);
  run.request = request;
  run.reader = has_reader ? &reader : NULL;
  run.positional = parsed.positional;
  if (parsed.positional) {
    /* Collect types by index; require contiguity from 0. */
    size_t max_index = 0;
    for (size_t i = 0; i < parsed.reference_count; i++)
      if (parsed.references[i].index + 1 > (int)max_index) max_index = (size_t)parsed.references[i].index + 1;
    qa_format_argument_type *types = scope->types = xmalloc(max_index * sizeof *types);
    bool *seen = scope->seen = xmalloc(max_index * sizeof *seen);
    memset(seen, 0, max_index * sizeof *seen);
    for (size_t i = 0; i < parsed.reference_count; i++) {
      reference ref = parsed.references[i];
      if (seen[ref.index] && types[ref.index] != ref.type) format_failure(22);
      seen[ref.index] = true;
      types[ref.index] = ref.type;
    }
    size_t type_count = 0;
    for (size_t i = 0; i < max_index; i++) if (seen[i]) type_count++;
    run.values = scope->values = xmalloc(type_count * sizeof *run.values);
    run.value_count = type_count;
    for (size_t index = 0; index < type_count; index++) {
      if (index >= max_index || !seen[index] || !has_reader) format_failure(22);
      run.values[index] = qa_format_arguments_next(&reader, types[index]);
      scope->value_count = index + 1;
    }
    scope->types = NULL; scope->seen = NULL;
    free(types); free(seen);
  }

  qa_guest_format_result final;
  for (size_t ti = 0; ti < parsed.token_count; ti++) {
    token *tok = &parsed.tokens[ti];
    if (tok->is_literal) { output_append(out, tok->text.data, tok->text.len); continue; }
    conversion *conv = &tok->conv;
    int64_t width = amount_value(&run, conv->width);
    bool has_precision = conv->has_precision;
    int64_t precision = has_precision ? amount_value(&run, conv->precision) : 0;
    bool left = strchr(conv->flags, '-') != NULL || width < 0;
    if (width < 0) width = -width;
    if (has_precision && precision < 0) has_precision = false;
    if (has_precision && precision > 1048576)
      qa_range_error("Guest printf precision exceeds runtime conversion limit");
    bool alternate = strchr(conv->flags, '#') != NULL;
    bool plus = strchr(conv->flags, '+') != NULL;
    bool blank = strchr(conv->flags, ' ') != NULL;
    if (!conv->has_argument) qa_raise("Error", 0, 0, "Missing printf conversion argument");
    scope->value = get_argument(&run, conv->argument);
    scope->value_owned = !run.positional;
#define value scope->value
#define body scope->body
#define prefix scope->prefix
    char code = conv->code;
    bstr_init(&body); bstr_init(&prefix);
    bool zero = strchr(conv->flags, '0') != NULL && !left;
    bool skip_output = false;
    if (strchr("diouxX", code) != NULL) {
      bool signed_ = code == 'd' || code == 'i';
      int bits = integer_bits(conv->length, request);
      int64_t number = signed_ ? as_int_n(argument_integer(&value), bits) : 0;
      uint64_t magnitude = signed_ ? (number < 0 ? (uint64_t)(-(number + 1)) + 1 : (uint64_t)number)
                                   : as_uint_n(argument_integer(&value), bits);
      int radix = code == 'o' ? 8 : (code == 'x' || code == 'X') ? 16 : 10;
      if (!(has_precision && precision == 0 && magnitude == 0))
        uint_to_radix(&body, magnitude, radix, code == 'X');
      int64_t wanted = has_precision ? precision : 1;
      if ((int64_t)body.len < wanted) {
        bstr padded; bstr_init(&padded);
        for (int64_t i = 0; i < wanted - (int64_t)body.len; i++) bstr_append(&padded, "0", 1);
        bstr_append(&padded, body.data, body.len);
        bstr_free(&body);
        body = padded;
      }
      if (signed_) {
        if (number < 0) bstr_append_str(&prefix, "-");
        else if (plus) bstr_append_str(&prefix, "+");
        else if (blank) bstr_append_str(&prefix, " ");
      } else if (alternate && radix == 16 && magnitude != 0) {
        bstr_append_str(&prefix, code == 'X' ? "0X" : "0x");
      } else if (alternate && radix == 8 && !(body.len > 0 && body.data[0] == '0')) {
        bstr_append_str(&prefix, "0");
      }
      if (has_precision) zero = false;
    } else if (strchr("fFeEgGaA", code) != NULL) {
      if (!value.is_float) format_failure(22);
      char *formatted = scope->formatted = qa_format_float(&value.float_value, code, has_precision, (int)precision,
                                        alternate, request->rounding,
                                        request->dialect == QA_FORMAT_WINDOWS,
                                        strcmp(conv->length, "L") == 0 && request->dialect == QA_FORMAT_SYSTEM_V,
                                        request->exponent_digits);
      if (formatted == NULL) qa_range_error("Guest runtime out of memory");
      bstr_append_str(&body, formatted);
      scope->formatted = NULL;
      free(formatted);
      if (value.float_value.sign == 1) bstr_append_str(&prefix, "-");
      else if (plus) bstr_append_str(&prefix, "+");
      else if (blank) bstr_append_str(&prefix, " ");
      if (value.float_value.kind != QA_BINARY_FINITE) zero = false;
      if (body.len >= 2 && body.data[0] == '0' && (body.data[1] == 'x' || body.data[1] == 'X')) {
        bstr_append(&prefix, body.data, 2);
        memmove(body.data, body.data + 2, body.len - 2);
        body.len -= 2;
        body.data[body.len] = '\0';
      }
    } else if (code == 'p') {
      uint64_t pointer = as_uint_n(argument_integer(&value), request->memory->pointer_bytes * 8);
      if (request->dialect == QA_FORMAT_WINDOWS) {
        uint_to_radix(&body, pointer, 16, true);
        int64_t wanted = request->memory->pointer_bytes * 2;
        if ((int64_t)body.len < wanted) {
          bstr padded; bstr_init(&padded);
          for (int64_t i = 0; i < wanted - (int64_t)body.len; i++) bstr_append(&padded, "0", 1);
          bstr_append(&padded, body.data, body.len);
          bstr_free(&body); body = padded;
        }
      } else if (pointer == 0) {
        bstr_append_str(&body, "(nil)");
        zero = false;
      } else {
        bstr_append_str(&prefix, "0x");
        uint_to_radix(&body, pointer, 16, false);
        int64_t wanted = has_precision ? precision : 1;
        if ((int64_t)body.len < wanted) {
          bstr padded; bstr_init(&padded);
          for (int64_t i = 0; i < wanted - (int64_t)body.len; i++) bstr_append(&padded, "0", 1);
          bstr_append(&padded, body.data, body.len);
          bstr_free(&body); body = padded;
        }
      }
    } else if (code == 's' || code == 'S') {
      bool wide = strcmp(conv->length, "l") == 0 || strcmp(conv->length, "w") == 0
          || (code == 'S' && strcmp(conv->length, "h") != 0);
      qa_guest_core_address address = {0};
      uint64_t raw = argument_integer(&value);
      bool has_address = qa_memory_pointer(request->memory, raw, &address);
      bstr_free(&body);
      string_argument(&body, request, has_address, address, wide, has_precision, precision);
      zero = false;
    } else if (code == 'c' || code == 'C') {
      bool wide = strcmp(conv->length, "l") == 0 || strcmp(conv->length, "w") == 0
          || (code == 'C' && strcmp(conv->length, "h") != 0);
      int bits = wide ? (request->dialect == QA_FORMAT_WINDOWS ? 16 : 32) : 8;
      uint64_t number = as_uint_n(argument_integer(&value), bits);
      if (wide && number > (uint64_t)(request->dialect == QA_FORMAT_WINDOWS ? 255 : 127))
        format_failure(request->dialect == QA_FORMAT_WINDOWS ? 42 : 84);
      char ch = (char)number;
      bstr_append(&body, &ch, 1);
      zero = false;
    } else if (code == 'n') {
      if (request->dialect == QA_FORMAT_WINDOWS) format_failure(22);
      if (request->fortify && !readonly_format(request, strlen(text)))
        qa_guest_format_fortify_failure("glibc %n in writable format storage");
      qa_guest_core_address address = {0};
      if (!qa_memory_pointer(request->memory, argument_integer(&value), &address))
        qa_raise("TypeError", 0, 0, "Null printf count pointer");
      qa_write_unsigned(request->memory, address, integer_bits(conv->length, request) / 8,
                        (uint64_t)out->total);
      skip_output = true;
    }
    if (scope->value_owned) qa_format_argument_free(&value);
    scope->value_owned = false;
    if (skip_output) { bstr_free(&body); bstr_free(&prefix); continue; }
    int64_t padding = width - (int64_t)prefix.len - (int64_t)body.len;
    if (padding < 0) padding = 0;
    if (!left && !zero) output_repeat(out, ' ', padding);
    output_append(out, prefix.data, prefix.len);
    if (zero) output_repeat(out, '0', padding);
    output_append(out, body.data, body.len);
    if (left) output_repeat(out, ' ', padding);
    bstr_free(&body);
    bstr_free(&prefix);
#undef value
#undef body
#undef prefix
  }
  final = output_finish(out, false, 0, false);
  return final;
}

qa_guest_format_result qa_format_guest_buffer(const qa_guest_format_request *request) {
  /* capacity is the guest's size_t: any 64-bit value on a 64-bit guest (sprintf passes SIZE_MAX). */
  if (request->memory->pointer_bytes == 4 && request->capacity > 0xffffffffull)
    qa_range_error("Guest printf buffer count is not size_t");
  if (!request->has_format || (!request->has_buffer && request->capacity != 0)) {
    qa_guest_format_result result = {-1, true, 22};
    return result;
  }
  output out; memset(&out, 0, sizeof out);
  out.request = request;
  format_throw catcher; memset(&catcher, 0, sizeof catcher);
  format_scope scope; memset(&scope, 0, sizeof scope);
  scope.previous_throw = g_throw;
  qa_cleanup scope_node;
  qa_cleanup_push(&scope_node, format_scope_release, &scope);
  g_throw = &catcher;
  if (setjmp(catcher.buf) != 0) {
    /* FormatFailure / BufferFull: nothing was pushed between the setjmp and the jump. */
    qa_cleanup_pop(&scope_node);
    format_scope_release(&scope);
    if (catcher.kind == 2) return output_finish(&out, false, 0, true);
    return output_finish(&out, true, catcher.errno_value, false);
  }
  qa_guest_format_result result = format_run_body(request, &out, &scope);
  qa_cleanup_pop(&scope_node);
  format_scope_release(&scope);
  return result;
}
