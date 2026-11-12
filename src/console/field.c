/* Port of src/console/field.ts. GPL-2.0-or-later. */
#include "qa/console/field.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

/* KeyCode values from src/input/key-codes.ts (input module pending). */
enum {
  KEY_BACKSPACE = 127,
  KEY_LEFT = 134,
  KEY_RIGHT = 135,
  KEY_CONTROL = 137,
  KEY_SHIFT = 138,
  KEY_INSERT = 139,
  KEY_DELETE = 140,
  KEY_HOME = 143,
  KEY_END = 144,
  KEY_KEYPAD_INSERT = 170
};

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    if (length > 0) memcpy(out, text, length); /* an empty field has no buffer */
    out[length] = '\0';
  }
  return out;
}
/* The TS field is an array of characters (code points) and its text a JS string. The C field holds that text
 * as UTF-8 (always well formed: every entry point decodes its input), so a character above U+00FF stays in the
 * field as TS keeps it; cursor, scroll and maximumLength count code points. The text becomes console text
 * (one byte per character, core/commands/text.h) where TS hands it to a command (SeatConsole.submit). */
static size_t cp_step(const char *text, size_t length, size_t at) {
  unsigned char lead = (unsigned char)text[at];
  size_t step = lead < 0xc0 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
  return at + step <= length ? step : length - at;
}

static uint32_t cp_decode(const char *text, size_t length, size_t at, size_t *step) {
  const unsigned char *bytes = (const unsigned char *)text + at;
  *step = cp_step(text, length, at);
  switch (*step) {
    case 2: return ((uint32_t)(bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);
    case 3: return ((uint32_t)(bytes[0] & 0x0f) << 12) | ((uint32_t)(bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
    case 4:
      return ((uint32_t)(bytes[0] & 0x07) << 18) | ((uint32_t)(bytes[1] & 0x3f) << 12) |
             ((uint32_t)(bytes[2] & 0x3f) << 6) | (bytes[3] & 0x3f);
    default: return bytes[0];
  }
}

static size_t cp_encode(uint32_t cp, char *out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xc0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3f));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xe0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
  }
  out[0] = (char)(0xf0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
  out[3] = (char)(0x80 | (cp & 0x3f));
  return 4;
}

/* One code point of platform text, decoded as TextDecoder does (WHATWG UTF-8: an ill-formed sequence is one
 * U+FFFD over its maximal subpart). */
static size_t input_decode(const unsigned char *bytes, size_t length, size_t at, uint32_t *cp) {
  unsigned char lead = bytes[at];
  if (lead < 0x80) {
    *cp = lead;
    return 1;
  }
  size_t need = 0;
  uint32_t value = 0;
  unsigned char lower = 0x80, upper = 0xbf;
  if (lead >= 0xc2 && lead <= 0xdf) {
    need = 1;
    value = lead & 0x1f;
  } else if (lead >= 0xe0 && lead <= 0xef) {
    need = 2;
    value = lead & 0x0f;
    if (lead == 0xe0) lower = 0xa0;
    if (lead == 0xed) upper = 0x9f;
  } else if (lead >= 0xf0 && lead <= 0xf4) {
    need = 3;
    value = lead & 0x07;
    if (lead == 0xf0) lower = 0x90;
    if (lead == 0xf4) upper = 0x8f;
  } else {
    *cp = 0xfffd;
    return 1;
  }
  size_t used = 1;
  for (size_t i = 0; i < need; i++) {
    if (at + used >= length || bytes[at + used] < lower || bytes[at + used] > upper) {
      *cp = 0xfffd;
      return used;
    }
    value = (value << 6) | (bytes[at + used] & 0x3f);
    used++;
    lower = 0x80;
    upper = 0xbf;
  }
  *cp = value;
  return used;
}

static size_t cp_count(const char *text, size_t length) {
  size_t count = 0;
  for (size_t at = 0; at < length; at += cp_step(text, length, at)) count++;
  return count;
}

/* Byte offset of code point `index`, clamped to [0, count]. */
static size_t cp_offset(const char *text, size_t length, int64_t index) {
  size_t at = 0;
  for (int64_t i = 0; i < index && at < length; i++) at += cp_step(text, length, at);
  return at;
}

static int64_t field_count(const qa_console_field *field) {
  return field->characters == NULL ? 0 : (int64_t)cp_count(field->characters, field->length);
}

/* JS \s: WhiteSpace and LineTerminator code points. */
static bool is_js_space(uint32_t cp) {
  return cp == 0x09 || cp == 0x0a || cp == 0x0b || cp == 0x0c || cp == 0x0d || cp == 0x20 ||
         cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
         cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000 || cp == 0xfeff;
}
static char fold_byte(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static bool folded_starts_with(const char *text, const char *prefix, size_t prefix_length) {
  for (size_t i = 0; i < prefix_length; i++) {
    if (text[i] == '\0' || fold_byte(text[i]) != fold_byte(prefix[i])) return false;
  }
  return true;
}

/* A console-text name (one byte per character) starts with a UTF-8 field prefix, A-Z folded (foldCompletion). */
static bool name_starts_with_field(const char *name, const char *prefix, size_t prefix_length) {
  size_t n = 0;
  for (size_t at = 0; at < prefix_length;) {
    size_t step = 0;
    uint32_t cp = cp_decode(prefix, prefix_length, at, &step);
    at += step;
    uint32_t c = (unsigned char)name[n];
    if (c == 0) return false;
    if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != cp) return false;
    n++;
  }
  return true;
}
/* Console text as UTF-8 (each byte is its code point). */
static char *console_to_field(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length * 2 + 1);
  if (out == NULL) return NULL;
  size_t at = 0;
  for (size_t i = 0; i < length; i++) at += cp_encode((unsigned char)text[i], out + at);
  out[at] = '\0';
  return out;
}

static bool field_grow(qa_console_field *field, size_t needed) {
  if (needed <= field->capacity) return true;
  size_t capacity = field->capacity == 0 ? 64 : field->capacity;
  while (capacity < needed) capacity *= 2;
  char *data = (char *)realloc(field->characters, capacity);
  if (data == NULL) return false;
  field->characters = data;
  field->capacity = capacity;
  return true;
}

static void completion_clear(qa_console_field *field) {
  free(field->completion_text);
  free(field->completion_tail);
  free(field->completion_selected);
  for (size_t i = 0; i < field->completion_match_count; i++) free(field->completion_matches[i]);
  free(field->completion_matches);
  field->completion_text = NULL;
  field->completion_tail = NULL;
  field->completion_selected = NULL;
  field->completion_matches = NULL;
  field->completion_match_count = 0;
  field->completion_cursor = 0;
  field->completion_index = 0;
  field->has_completion = false;
}

bool qa_console_field_init(qa_console_field *field, int64_t maximum_length,
                           int64_t width_in_chars) {
  memset(field, 0, sizeof *field);
  field->maximum_length = maximum_length;
  field->width_in_chars = width_in_chars;
  return true;
}

void qa_console_field_destroy(qa_console_field *field) {
  completion_clear(field);
  free(field->characters);
  memset(field, 0, sizeof *field);
}

char *qa_console_field_text(const qa_console_field *field) {
  return dup_range(field->characters == NULL ? "" : field->characters, field->length);
}

static void keep_visible(qa_console_field *field) {
  int64_t scroll = field->scroll < field->cursor ? field->scroll : field->cursor;
  field->scroll = scroll > 0 ? scroll : 0;
  if (field->cursor >= field->scroll + field->width_in_chars) {
    int64_t width = field->width_in_chars > 1 ? field->width_in_chars : 1;
    field->scroll = field->cursor - width + 1;
  }
}

const char *qa_console_field_selected_completion(const qa_console_field *field) {
  if (!field->has_completion || field->completion_cursor != field->cursor) return NULL;
  size_t length = strlen(field->completion_text);
  if (length != field->length) return NULL;
  if (length > 0 && memcmp(field->completion_text, field->characters, length) != 0) return NULL;
  return field->completion_selected;
}

void qa_console_field_set_text(qa_console_field *field, const char *text) {
  completion_clear(field);
  /* text.replace(/[\x00-\x1f\x7f]/g, "") as code points, sliced to maximumLength. */
  size_t text_length = strlen(text);
  char *clean = (char *)malloc(text_length * 3 + 1); /* U+FFFD for one byte at most */
  if (clean == NULL) return; /* OOM only */
  size_t clean_length = 0;
  for (size_t i = 0; i < text_length;) {
    uint32_t cp = 0;
    i += input_decode((const unsigned char *)text, text_length, i, &cp);
    if (!(cp <= 0x1f || cp == 0x7f)) clean_length += cp_encode(cp, clean + clean_length);
  }
  int64_t maximum = field->maximum_length > 0 ? field->maximum_length : 0;
  size_t keep = cp_offset(clean, clean_length, maximum);
  if (!field_grow(field, keep == 0 ? 1 : keep)) {
    free(clean);
    return; /* OOM only */
  }
  memcpy(field->characters, clean, keep);
  free(clean);
  field->length = keep;
  field->cursor = field_count(field);
  keep_visible(field);
}

void qa_console_field_clear(qa_console_field *field) {
  completion_clear(field);
  field->length = 0;
  field->cursor = 0;
  field->scroll = 0;
}

/* JS Array.splice(start, count) over the code points. */
static void remove_range(qa_console_field *field, int64_t start, int64_t count) {
  int64_t length = field_count(field);
  if (start < 0) start = length + start > 0 ? length + start : 0;
  if (start > length) start = length;
  if (count < 0) count = 0;
  if (start + count > length) count = length - start;
  if (count <= 0) return;
  size_t from = cp_offset(field->characters, field->length, start);
  size_t to = cp_offset(field->characters, field->length, start + count);
  memmove(field->characters + from, field->characters + to, field->length - to);
  field->length -= to - from;
}

void qa_console_field_insert(qa_console_field *field, const char *text) {
  completion_clear(field);
  size_t text_length = strlen(text);
  for (size_t p = 0; p < text_length;) {
    uint32_t cp = 0;
    p += input_decode((const unsigned char *)text, text_length, p, &cp);
    /* character < " " compares the first UTF-16 unit. */
    if (cp < ' ' || cp == 0x7f) continue;
    char character[4];
    size_t step = cp_encode(cp, character);
    int64_t count = field_count(field);
    if (field->overstrike && field->cursor >= 0 && field->cursor < count) {
      size_t from = cp_offset(field->characters, field->length, field->cursor);
      size_t to = cp_offset(field->characters, field->length, field->cursor + 1);
      size_t length = field->length - (to - from) + step;
      if (!field_grow(field, length)) return; /* OOM only */
      memmove(field->characters + from + step, field->characters + to, field->length - to);
      memcpy(field->characters + from, character, step);
      field->length = length;
      field->cursor++;
    } else if (count < field->maximum_length) {
      if (!field_grow(field, field->length + step)) return; /* OOM only */
      int64_t at = field->cursor;
      if (at < 0) at = 0;
      if (at > count) at = count;
      size_t from = cp_offset(field->characters, field->length, at);
      memmove(field->characters + from + step, field->characters + from, field->length - from);
      memcpy(field->characters + from, character, step);
      field->length += step;
      field->cursor++;
    }
  }
  keep_visible(field);
}

/* The code point at index, or -1 for TS "" (outside the array). */
static int64_t field_char(const qa_console_field *field, int64_t index, int64_t count) {
  if (index < 0 || index >= count) return -1;
  size_t at = cp_offset(field->characters, field->length, index), step = 0;
  return cp_decode(field->characters, field->length, at, &step);
}

static int64_t word_position(const qa_console_field *field, int direction) {
  int64_t cursor = field->cursor;
  int64_t length = field_count(field);
  const int64_t offset = direction == -1 ? -1 : 0;
  while (cursor + direction >= 0 && cursor + direction <= length) {
    int64_t c = field_char(field, cursor + offset, length);
    if (c < 0 || !is_js_space((uint32_t)c)) break;
    cursor += direction;
  }
  while (cursor + direction >= 0 && cursor + direction <= length) {
    int64_t c = field_char(field, cursor + offset, length);
    if (c < 0 || is_js_space((uint32_t)c)) break;
    cursor += direction;
  }
  return cursor;
}

static bool navigation(qa_console_field *field, int64_t code, bool control) {
  int64_t count = field_count(field);
  switch (code) {
    case KEY_BACKSPACE:
      if (field->cursor > 0) remove_range(field, --field->cursor, 1);
      break;
    case KEY_DELETE:
      remove_range(field, field->cursor, 1);
      break;
    case KEY_LEFT:
      field->cursor = control ? word_position(field, -1)
                               : (field->cursor - 1 > 0 ? field->cursor - 1 : 0);
      break;
    case KEY_RIGHT: {
      int64_t next = field->cursor + 1;
      field->cursor = control ? word_position(field, 1) : (next < count ? next : count);
      break;
    }
    case KEY_HOME: field->cursor = 0; break;
    case KEY_END: field->cursor = count; break;
    case KEY_INSERT: field->overstrike = !field->overstrike; break;
    default: return false;
  }
  keep_visible(field);
  return true;
}

bool qa_console_field_key(qa_console_field *field, int64_t code, bool control, bool shift,
                          qa_console_clipboard_fn clipboard, void *clipboard_ctx) {
  if (code != KEY_SHIFT && code != KEY_CONTROL) completion_clear(field);
  if ((code == 118 && control) ||
      ((code == KEY_INSERT || code == KEY_KEYPAD_INSERT) && shift)) {
    const char *text = clipboard(clipboard_ctx);
    if (text != NULL) {
      /* text.split(/[\r\n\0]/, 1)[0]; the platform's text is UTF-8. */
      size_t length = strcspn(text, "\r\n");
      char *line = dup_range(text, length);
      if (line != NULL) {
        qa_console_field_insert(field, line);
        free(line);
      }
    }
    return true;
  }
  if (control) {
    switch (code) {
      case 97: field->cursor = 0; break;
      case 101: field->cursor = field_count(field); break;
      case 99:
      case 117: qa_console_field_clear(field); break;
      case 107: remove_range(field, field->cursor, field_count(field)); break;
      case 119: {
        int64_t start = word_position(field, -1);
        remove_range(field, start, field->cursor - start);
        field->cursor = start;
        break;
      }
      default: return navigation(field, code, true);
    }
    keep_visible(field);
    return true;
  }
  return navigation(field, code, false);
}

/* ^([\\/]?)([^\s]*)(.*)$ over a byte range: the field's UTF-8 text, or console text (one byte per character). */
static void parse_line(const char *text, size_t length, bool utf8, size_t *token_start,
                       size_t *token_end) {
  size_t start = length > 0 && (text[0] == '/' || text[0] == '\\') ? 1 : 0;
  size_t end = start;
  while (end < length) {
    size_t step = 1;
    uint32_t cp = utf8 ? cp_decode(text, length, end, &step) : (unsigned char)text[end];
    if (is_js_space(cp)) break;
    end += step;
  }
  *token_start = start;
  *token_end = end;
}

typedef struct match_entry {
  char *folded;      /* owned */
  const char *name;  /* borrowed */
} match_entry;

static int match_compare(const void *left, const void *right) {
  return strcmp(((const match_entry *)left)->folded, ((const match_entry *)right)->folded);
}

void qa_console_field_complete(qa_console_field *field, const char *const *names,
                               size_t name_count, bool reverse) {
  bool fresh = !field->has_completion || field->completion_cursor != field->cursor ||
               strlen(field->completion_text) != field->length ||
               (field->length > 0 &&
                memcmp(field->completion_text, field->characters, field->length) != 0);
  if (fresh) {
    completion_clear(field);
    size_t token_start = 0, token_end = 0;
    parse_line(field->characters == NULL ? "" : field->characters, field->length, true,
               &token_start, &token_end);
    /* tokenStart/tokenEnd and the cursor count code points. */
    const char *chars = field->characters == NULL ? "" : field->characters;
    int64_t start_cp = (int64_t)token_start;
    int64_t end_cp = start_cp + (int64_t)cp_count(chars + token_start, token_end - token_start);
    if (field->cursor < start_cp || field->cursor > end_cp) return;
    size_t prefix_length = cp_offset(chars + token_start, token_end - token_start,
                                     field->cursor - start_cp);
    match_entry *entries = NULL;
    size_t count = 0, capacity = 0;
    for (size_t i = 0; i < name_count; i++) {
      const char *name = names[i];
      if (!name_starts_with_field(name, field->characters + token_start, prefix_length)) continue;
      char *folded = dup_text(name);
      if (folded == NULL) continue; /* OOM only */
      for (char *p = folded; *p != '\0'; p++) *p = fold_byte(*p);
      bool duplicate = false;
      for (size_t j = 0; j < count; j++)
        if (strcmp(entries[j].folded, folded) == 0) duplicate = true;
      if (duplicate) {
        free(folded);
        continue;
      }
      if (count == capacity) {
        size_t next = capacity == 0 ? 8 : capacity * 2;
        match_entry *grown = (match_entry *)realloc(entries, next * sizeof *grown);
        if (grown == NULL) {
          free(folded);
          continue; /* OOM only */
        }
        entries = grown;
        capacity = next;
      }
      entries[count].folded = folded;
      entries[count].name = name;
      count++;
    }
    if (count == 0) {
      free(entries);
      return;
    }
    qa_js_sort(entries, count, sizeof *entries, match_compare);
    field->completion_text = qa_console_field_text(field);
    field->completion_tail =
        dup_range(field->characters + token_end, field->length - token_end);
    field->completion_matches = (char **)calloc(count, sizeof(char *));
    field->completion_selected = dup_text("");
    if (field->completion_text == NULL || field->completion_tail == NULL ||
        field->completion_matches == NULL || field->completion_selected == NULL) {
      for (size_t i = 0; i < count; i++) free(entries[i].folded);
      free(entries);
      completion_clear(field);
      return; /* OOM only */
    }
    for (size_t i = 0; i < count; i++) {
      field->completion_matches[i] = dup_text(entries[i].name);
      free(entries[i].folded);
    }
    free(entries);
    field->completion_match_count = count;
    field->completion_cursor = field->cursor;
    field->completion_index = reverse ? 0 : -1;
    field->has_completion = true;
  }
  int64_t total = (int64_t)field->completion_match_count;
  int64_t index = (field->completion_index + (reverse ? -1 : 1) + total) % total;
  const char *selected = field->completion_matches[index];
  /* The names are console text: each byte is one character, and the field text holds it as UTF-8. */
  char *selected_text = console_to_field(selected);
  if (selected_text == NULL) return; /* OOM only */
  size_t text_length = 1 + strlen(selected_text) + strlen(field->completion_tail);
  int64_t text_points = 1 + (int64_t)strlen(selected) +
                        (int64_t)cp_count(field->completion_tail, strlen(field->completion_tail));
  if (text_points > field->maximum_length) {
    free(selected_text);
    if (fresh) completion_clear(field); /* TS keeps the fresh cycle unassigned. */
    return;
  }
  char *text = (char *)malloc(text_length + 1);
  char *selected_copy = dup_text(selected);
  if (text == NULL || selected_copy == NULL) {
    free(text);
    free(selected_copy);
    free(selected_text);
    return; /* OOM only */
  }
  sprintf(text, "/%s%s", selected_text, field->completion_tail);
  free(selected_text);
  /* setText clears the cycle in TS; steal it across the call. */
  char *tail = field->completion_tail;
  char **matches = field->completion_matches;
  size_t match_count = field->completion_match_count;
  field->completion_tail = NULL;
  field->completion_matches = NULL;
  field->completion_match_count = 0;
  qa_console_field_set_text(field, text);
  field->cursor = 1 + (int64_t)strlen(selected_copy);
  keep_visible(field);
  field->completion_text = text;
  field->completion_tail = tail;
  field->completion_matches = matches;
  field->completion_match_count = match_count;
  field->completion_cursor = field->cursor;
  field->completion_index = index;
  free(field->completion_selected);
  field->completion_selected = selected_copy;
  field->has_completion = true;
}

bool qa_complete_command(const char *text, const char *const *names, size_t name_count,
                         qa_completion_result *out) {
  memset(out, 0, sizeof *out);
  size_t length = strlen(text);
  size_t token_start = 0, token_end = 0;
  parse_line(text, length, false, &token_start, &token_end);
  size_t prefix_length = token_end - token_start;
  if (prefix_length == 0) {
    out->text = dup_text(text);
    return out->text != NULL;
  }
  const char **matches = NULL;
  size_t count = 0, capacity = 0;
  for (size_t i = 0; i < name_count; i++) {
    const char *name = names[i];
    if (!folded_starts_with(name, text + token_start, prefix_length)) continue;
    bool duplicate = false;
    for (size_t j = 0; j < count; j++)
      if (strcmp(matches[j], name) == 0) duplicate = true;
    if (duplicate) continue;
    if (count == capacity) {
      size_t next = capacity == 0 ? 8 : capacity * 2;
      const char **grown = (const char **)realloc(matches, next * sizeof *grown);
      if (grown == NULL) {
        free(matches);
        return false;
      }
      matches = grown;
      capacity = next;
    }
    matches[count++] = name;
  }
  out->matches = matches;
  out->match_count = count;
  if (count == 0) {
    out->text = dup_text(text);
    if (out->text == NULL) {
      qa_completion_result_destroy(out);
      return false;
    }
    return true;
  }
  const char *first = matches[0];
  size_t common = strlen(first);
  for (size_t i = 0; i < count; i++) {
    size_t at = 0;
    while (at < common && fold_byte(first[at]) == fold_byte(matches[i][at])) at++;
    common = at;
  }
  size_t tail_length = length - token_end;
  const char *suffix = tail_length > 0 ? text + token_end : (count == 1 ? " " : "");
  size_t suffix_length = tail_length > 0 ? tail_length : strlen(suffix);
  out->text = (char *)malloc(token_start + common + suffix_length + 1);
  if (out->text == NULL) {
    qa_completion_result_destroy(out);
    return false;
  }
  memcpy(out->text, text, token_start);
  memcpy(out->text + token_start, first, common);
  memcpy(out->text + token_start + common, suffix, suffix_length);
  out->text[token_start + common + suffix_length] = '\0';
  return true;
}

void qa_completion_result_destroy(qa_completion_result *result) {
  free(result->text);
  free(result->matches);
  memset(result, 0, sizeof *result);
}

static void history_free_value(const void *value, size_t index, void *ctx) {
  (void)index;
  (void)ctx;
  free(*(char *const *)value);
}

static void history_push(qa_console_history *history, const char *line) {
  char *copy = dup_text(line);
  if (copy == NULL) return; /* OOM only */
  const void *oldest = NULL;
  if (history->entries.count == history->entries.capacity)
    oldest = qa_bounded_queue_peek_oldest(&history->entries);
  char *evicted = oldest == NULL ? NULL : *(char *const *)oldest;
  qa_bounded_queue_push(&history->entries, &copy);
  free(evicted);
}

bool qa_console_history_init(qa_console_history *history, int64_t capacity) {
  memset(history, 0, sizeof *history);
  history->capacity = capacity;
  history->draft = dup_text("");
  if (history->draft == NULL) return false;
  if (!qa_bounded_queue_init(&history->entries,
                              capacity > 0 ? (size_t)capacity : 0, sizeof(char *))) {
    free(history->draft);
    history->draft = NULL;
    return false;
  }
  return true;
}

void qa_console_history_destroy(qa_console_history *history) {
  qa_bounded_queue_for_each(&history->entries, history_free_value, NULL);
  qa_bounded_queue_destroy(&history->entries);
  free(history->draft);
  memset(history, 0, sizeof *history);
}

size_t qa_console_history_line_count(const qa_console_history *history) {
  return history->entries.count;
}

const char **qa_console_history_lines(const qa_console_history *history, size_t *out_count) {
  size_t count = 0;
  char **values = (char **)qa_bounded_queue_to_array(&history->entries, &count);
  if (out_count != NULL) *out_count = count;
  return (const char **)values;
}

void qa_console_history_replace(qa_console_history *history, const char *const *lines,
                                size_t count) {
  qa_bounded_queue_for_each(&history->entries, history_free_value, NULL);
  qa_bounded_queue_clear(&history->entries);
  for (size_t i = 0; i < count; i++)
    if (lines[i][0] != '\0') history_push(history, lines[i]);
  history->position = (int64_t)history->entries.count;
}

void qa_console_history_add(qa_console_history *history, const char *line) {
  if (line[0] == '\0') return;
  history_push(history, line);
  history->position = (int64_t)history->entries.count;
  free(history->draft);
  history->draft = dup_text("");
}

const char *qa_console_history_previous(qa_console_history *history, const char *current) {
  if (history->position == (int64_t)history->entries.count) {
    char *draft = dup_text(current);
    if (draft != NULL) {
      free(history->draft);
      history->draft = draft;
    }
  }
  history->position = history->position - 1 > 0 ? history->position - 1 : 0;
  const void *value = qa_bounded_queue_at(&history->entries, (size_t)history->position);
  return value == NULL ? current : *(char *const *)value;
}

const char *qa_console_history_next(qa_console_history *history) {
  int64_t count = (int64_t)history->entries.count;
  history->position = history->position + 1 < count ? history->position + 1 : count;
  const void *value = qa_bounded_queue_at(&history->entries, (size_t)history->position);
  return value == NULL ? history->draft : *(char *const *)value;
}
