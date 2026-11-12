/* JavaScript case mapping and localeCompare; see include/qa/foundation/js_case.h.
 * GPL-2.0-or-later. */
#include "qa/foundation/js_case.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

typedef struct js_case_pair {
  uint32_t from, to;
} js_case_pair;
typedef struct js_case_multi {
  uint32_t from;
  uint32_t count;
  uint32_t to[3];
} js_case_multi;

#include "js_case_tables.inc"

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const js_case_pair *find_pair(const js_case_pair *table, size_t count, uint32_t cp) {
  size_t low = 0, high = count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (table[mid].from == cp) return &table[mid];
    if (table[mid].from < cp) low = mid + 1;
    else high = mid;
  }
  return NULL;
}

static const js_case_multi *find_multi(const js_case_multi *table, size_t count, uint32_t cp) {
  for (size_t i = 0; i < count; i++) {
    if (table[i].from == cp) return &table[i];
  }
  return NULL;
}

static bool in_ranges(const js_case_pair *ranges, size_t count, uint32_t cp) {
  size_t low = 0, high = count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (cp < ranges[mid].from) high = mid;
    else if (cp > ranges[mid].to) low = mid + 1;
    else return true;
  }
  return false;
}

/* One decoded unit: a code point, or a byte that is not valid UTF-8 (raw). */
typedef struct unit {
  uint32_t cp;
  bool raw;
} unit;

static size_t decode_one(const unsigned char *s, unit *out) {
  unsigned char c = s[0];
  out->raw = false;
  if (c < 0x80) { out->cp = c; return 1; }
  size_t need;
  uint32_t cp, min;
  if (c >= 0xc2 && c <= 0xdf) { need = 1; cp = c & 0x1f; min = 0x80; }
  else if (c >= 0xe0 && c <= 0xef) { need = 2; cp = c & 0x0f; min = 0x800; }
  else if (c >= 0xf0 && c <= 0xf4) { need = 3; cp = c & 0x07; min = 0x10000; }
  else goto raw;
  for (size_t i = 1; i <= need; i++) {
    if ((s[i] & 0xc0) != 0x80) goto raw;
    cp = (cp << 6) | (s[i] & 0x3f);
  }
  if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) goto raw;
  out->cp = cp;
  return need + 1;
raw:
  out->cp = c;
  out->raw = true;
  return 1;
}

static unit *decode_all(const char *text, size_t *count) {
  size_t length = strlen(text);
  unit *units = malloc((length + 1) * sizeof *units);
  if (units == NULL) return NULL;
  size_t n = 0;
  for (size_t at = 0; at < length;) at += decode_one((const unsigned char *)text + at, &units[n++]);
  *count = n;
  return units;
}

static size_t encode(uint32_t cp, char *out) {
  if (cp < 0x80) { out[0] = (char)cp; return 1; }
  if (cp < 0x800) { out[0] = (char)(0xc0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3f)); return 2; }
  if (cp < 0x10000) {
    out[0] = (char)(0xe0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
  }
  out[0] = (char)(0xf0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[3] = (char)(0x80 | (cp & 0x3f));
  return 4;
}

static bool cased(const unit *u) { return !u->raw && in_ranges(js_cased_ranges, COUNT(js_cased_ranges), u->cp); }
static bool ignorable(const unit *u) {
  return !u->raw && in_ranges(js_case_ignorable_ranges, COUNT(js_case_ignorable_ranges), u->cp);
}

/* Unicode Final_Sigma, checking Case_Ignorable before Cased as ICU does. */
static bool final_sigma(const unit *units, size_t count, size_t at) {
  bool before = false;
  for (size_t i = at; i-- > 0;) {
    if (ignorable(&units[i])) continue;
    before = cased(&units[i]);
    break;
  }
  if (!before) return false;
  for (size_t i = at + 1; i < count; i++) {
    if (ignorable(&units[i])) continue;
    return !cased(&units[i]);
  }
  return true;
}

static char *map_case(const char *text, bool upper) {
  size_t count;
  unit *units = decode_all(text, &count);
  if (units == NULL) return NULL;
  char *out = malloc(strlen(text) * 3 + 1);
  if (out == NULL) { free(units); return NULL; }
  size_t length = 0;
  for (size_t i = 0; i < count; i++) {
    const unit *u = &units[i];
    if (u->raw) { out[length++] = (char)u->cp; continue; }
    uint32_t cp = u->cp;
    if (!upper && cp == 0x3a3) {
      length += encode(final_sigma(units, count, i) ? 0x3c2 : 0x3c3, out + length);
      continue;
    }
    const js_case_multi *multi = upper ? find_multi(js_upper_multi, COUNT(js_upper_multi), cp)
                                       : find_multi(js_lower_multi, COUNT(js_lower_multi), cp);
    if (multi != NULL) {
      for (uint32_t k = 0; k < multi->count; k++) length += encode(multi->to[k], out + length);
      continue;
    }
    const js_case_pair *pair = upper ? find_pair(js_upper_pairs, COUNT(js_upper_pairs), cp)
                                     : find_pair(js_lower_pairs, COUNT(js_lower_pairs), cp);
    length += encode(pair != NULL ? pair->to : cp, out + length);
  }
  out[length] = '\0';
  free(units);
  return out;
}

char *qa_js_to_lower_case(const char *text) { return map_case(text, false); }
char *qa_js_to_upper_case(const char *text) { return map_case(text, true); }

static char *lower_or_abort(const char *text) {
  char *lowered = qa_js_to_lower_case(text);
  if (lowered == NULL) qa_range_error("Out of memory lowering text");
  return lowered;
}

bool qa_js_equals_ignore_case(const char *a, const char *b) {
  char *x = lower_or_abort(a), *y = lower_or_abort(b);
  bool equal = strcmp(x, y) == 0;
  free(x);
  free(y);
  return equal;
}

bool qa_js_ends_with_ignore_case(const char *text, const char *suffix) {
  char *x = lower_or_abort(text), *y = lower_or_abort(suffix);
  size_t a = strlen(x), b = strlen(y);
  bool match = b <= a && memcmp(x + a - b, y, b) == 0;
  free(x);
  free(y);
  return match;
}

bool qa_js_starts_with_ignore_case(const char *text, const char *prefix) {
  char *x = lower_or_abort(text), *y = lower_or_abort(prefix);
  bool match = strncmp(x, y, strlen(y)) == 0;
  free(x);
  free(y);
  return match;
}

char *qa_js_latin1_to_lower_case(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out == NULL) return NULL;
  for (size_t i = 0; i <= length; i++) out[i] = (char)qa_js_latin1_lower((unsigned char)text[i]);
  return out;
}

bool qa_js_latin1_equals_ignore_case(const char *a, const char *b) {
  const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
  for (;; x++, y++) {
    if (qa_js_latin1_lower(*x) != qa_js_latin1_lower(*y)) return false;
    if (*x == '\0') return true;
  }
}

/* ---- localeCompare -------------------------------------------------------- */

/* bun's default collator (ICU root through JavaScriptCore) per code point, generated by
 * `bun tools/js_collation_tables.js`: for the code points of js_collation_ranges, the
 * dense rank of the character's class at base, accent and variant sensitivity (0 where
 * it is ignorable at that level), and the expansions that collate as a sequence. A
 * string compares by its primary weights, then its secondary, then its tertiary. */
typedef struct js_collation_range { uint32_t first, last, offset; } js_collation_range;
typedef struct js_collation_weight { uint16_t primary, secondary, tertiary; } js_collation_weight;
typedef struct js_collation_expansion { uint32_t cp; uint8_t count; js_collation_weight parts[4]; } js_collation_expansion;
#include "js_collation_tables.inc"

typedef struct element {
  uint32_t primary; /* 0: ignorable at this level */
  uint32_t secondary;
  uint32_t tertiary;
  const char *digits; /* numeric run (leading zeros stripped), or NULL */
  size_t digit_count;
} element;

static const js_collation_weight *table_weight(uint32_t cp) {
  for (size_t i = 0; i < COUNT(js_collation_ranges); i++) {
    if (cp >= js_collation_ranges[i].first && cp <= js_collation_ranges[i].last)
      return &js_collation_weights[js_collation_ranges[i].offset + (cp - js_collation_ranges[i].first)];
  }
  return NULL;
}

static const js_collation_expansion *table_expansion(uint32_t cp) {
  size_t lo = 0, hi = COUNT(js_collation_expansions);
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (js_collation_expansions[mid].cp < cp) lo = mid + 1;
    else hi = mid;
  }
  return lo < COUNT(js_collation_expansions) && js_collation_expansions[lo].cp == cp ? &js_collation_expansions[lo] : NULL;
}

/* A decimal digit numeric collation reads as part of a number (ASCII and fullwidth). */
static int digit_value(const unsigned char *s, size_t *width) {
  if (s[0] >= '0' && s[0] <= '9') { *width = 1; return s[0] - '0'; }
  if (s[0] == 0xef && s[1] == 0xbc && s[2] >= 0x90 && s[2] <= 0x99) { *width = 3; return s[2] - 0x90; } /* U+FF10-U+FF19 */
  return -1;
}

static element *collation_elements(const char *text, bool numeric, size_t *count) {
  size_t length = strlen(text);
  /* The elements, then the digit values of numeric runs (one char per digit). */
  element *out = malloc((length * 4 + 1) * sizeof *out + length + 1);
  if (out == NULL) qa_range_error("Out of memory comparing text");
  char *digit_store = (char *)(out + length * 4 + 1);
  size_t n = 0;
  const unsigned char *s = (const unsigned char *)text;
  const js_collation_weight *zero = table_weight('0');
  for (size_t at = 0; at < length;) {
    unit u;
    size_t used = decode_one(s + at, &u);
    /* C0 80 is U+0000 (decoded text); a byte that is not UTF-8 is its Latin-1 code point
     * (a Quake byte string), which decode_one already reports as cp. */
    if (s[at] == 0xc0 && s[at + 1] == 0x80) { u.cp = 0; u.raw = false; used = 2; }
    size_t width = 0;
    if (numeric && digit_value(s + at, &width) >= 0) {
      /* The run's value: its digits with leading zeros dropped (all zeros: no digits). */
      char *digits = digit_store;
      size_t digit_count = 0;
      int value;
      while ((value = digit_value(s + at, &width)) >= 0) {
        if (digit_count > 0 || value != 0) digits[digit_count++] = (char)('0' + value);
        at += width;
      }
      digit_store += digit_count;
      element e = {zero->primary, zero->secondary, zero->tertiary, digits, digit_count};
      out[n++] = e;
      continue;
    }
    at += used;
    const js_collation_expansion *expansion = table_expansion(u.cp);
    if (expansion != NULL) {
      for (uint8_t i = 0; i < expansion->count; i++) {
        element e = {expansion->parts[i].primary, expansion->parts[i].secondary, expansion->parts[i].tertiary, NULL, 0};
        out[n++] = e;
      }
      continue;
    }
    const js_collation_weight *weight = table_weight(u.cp);
    /* Outside the table: after every table weight, by code point (approximate). */
    element e = weight != NULL ? (element){weight->primary, weight->secondary, weight->tertiary, NULL, 0}
                               : (element){JS_COLLATION_PRIMARY_LIMIT + u.cp, 0, 0, NULL, 0};
    if (e.primary != 0 || e.secondary != 0 || e.tertiary != 0) out[n++] = e;
  }
  *count = n;
  return out;
}

static int compare_primary(const element *a, const element *b) {
  if (a->digits != NULL && b->digits != NULL) {
    if (a->digit_count != b->digit_count) return a->digit_count < b->digit_count ? -1 : 1;
    int order = memcmp(a->digits, b->digits, a->digit_count);
    return order < 0 ? -1 : order > 0;
  }
  /* A numeric run's primaries come before every ordinary digit weight (bun: "8" < "½"
   * numerically, ½ expanding to ordinary 1, fraction slash, 2), after what sorts below. */
  if (a->digits != NULL) return b->primary >= a->primary ? -1 : 1;
  if (b->digits != NULL) return a->primary >= b->primary ? 1 : -1;
  if (a->primary != b->primary) return a->primary < b->primary ? -1 : 1;
  return 0;
}

static uint32_t level_weight(const element *e, int level) {
  return level == 0 ? e->primary : level == 1 ? e->secondary : e->tertiary;
}

/* The non-zero weights of one level, in order, then their count. */
static int compare_level(const element *x, size_t nx, const element *y, size_t ny, int level) {
  size_t i = 0, j = 0;
  for (;;) {
    while (i < nx && level_weight(&x[i], level) == 0) i++;
    while (j < ny && level_weight(&y[j], level) == 0) j++;
    if (i == nx || j == ny) return i == nx && j == ny ? 0 : i == nx ? -1 : 1;
    int order = level == 0 ? compare_primary(&x[i], &y[j]) : 0;
    if (order == 0 && level != 0 && level_weight(&x[i], level) != level_weight(&y[j], level))
      order = level_weight(&x[i], level) < level_weight(&y[j], level) ? -1 : 1;
    if (order != 0) return order;
    i++;
    j++;
  }
}

static int collate(const char *a, const char *b, bool tertiary, bool numeric) {
  size_t na, nb;
  element *x = collation_elements(a, numeric, &na), *y = collation_elements(b, numeric, &nb);
  int result = compare_level(x, na, y, nb, 0);
  if (result == 0 && tertiary) result = compare_level(x, na, y, nb, 1);
  if (result == 0 && tertiary) result = compare_level(x, na, y, nb, 2);
  free(x);
  free(y);
  return result;
}

int qa_js_locale_compare(const char *a, const char *b) { return collate(a, b, true, false); }
int qa_js_locale_compare_base(const char *a, const char *b) { return collate(a, b, false, false); }
int qa_js_locale_compare_numeric(const char *a, const char *b) { return collate(a, b, true, true); }

/* The UTF-16 code units of one step of UTF-8 text: C0 80 is U+0000 (qa_text_decoder_decode), a surrogate held as three
 * bytes (ED A0..BF xx) is that one unit, a code point above U+FFFF is a surrogate pair, and a byte that does not start
 * valid UTF-8 is one unit of its value. Returns the bytes consumed. */
static size_t code_units_step(const unsigned char *s, uint16_t units[2], size_t *unit_count) {
  if (s[0] == 0xc0 && s[1] == 0x80) { units[0] = 0; *unit_count = 1; return 2; }
  if (s[0] == 0xed && s[1] >= 0xa0 && s[1] <= 0xbf && (s[2] & 0xc0) == 0x80) {
    units[0] = (uint16_t)(0xd000 | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f));
    *unit_count = 1;
    return 3;
  }
  unit u;
  size_t used = decode_one(s, &u);
  if (u.cp >= 0x10000) {
    uint32_t v = u.cp - 0x10000;
    units[0] = (uint16_t)(0xd800 | (v >> 10));
    units[1] = (uint16_t)(0xdc00 | (v & 0x3ff));
    *unit_count = 2;
  } else {
    units[0] = (uint16_t)u.cp;
    *unit_count = 1;
  }
  return used;
}

int qa_js_compare_code_units(const char *a, const char *b) {
  const unsigned char *pa = (const unsigned char *)a, *pb = (const unsigned char *)b;
  uint16_t ua[2], ub[2];
  size_t na = 0, nb = 0, ia = 0, ib = 0;
  for (;;) {
    if (ia == na) {
      if (*pa == 0) { na = ia = 0; }
      else { pa += code_units_step(pa, ua, &na); ia = 0; }
    }
    if (ib == nb) {
      if (*pb == 0) { nb = ib = 0; }
      else { pb += code_units_step(pb, ub, &nb); ib = 0; }
    }
    bool end_a = ia == na, end_b = ib == nb;
    if (end_a || end_b) return end_a && end_b ? 0 : end_a ? -1 : 1;
    if (ua[ia] != ub[ib]) return ua[ia] < ub[ib] ? -1 : 1;
    ia++;
    ib++;
  }
}
