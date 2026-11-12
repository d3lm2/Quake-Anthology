/*
 * Port of src/persistence/value.ts: SaveFormatError, the checkpoint
 * value envelope (encode/decodeCheckpointValue), SaveReader, and
 * namespaced(). C accommodations:
 *  - TS `unknown` checkpoint data becomes an owned qa_save_value tree
 *    built with the qa_save_value_* constructors (NULL return is OOM).
 *    A TS plain object is an ordered key/value list; duplicate keys
 *    are kept, lookups return the first, mirroring JS object access
 *    closely enough for checkpoint data.
 *  - bigint values carry their canonical decimal text; the int64
 *    accessor parses it (contract bigint fields are int64_t).
 *  - SaveFormatError aborts printing "SaveFormatError: <path>:
 *    <message>", the TS Error string form.
 *  - SaveReader borrows the tree; its path string is owned and freed
 *    by qa_save_reader_destroy. Readers of lists/objects return child
 *    readers the caller destroys. Generic list<T> becomes an item
 *    count plus per-index readers (callers loop).
 *  - JSON parse/stringify follow JSON.parse/JSON.stringify for the
 *    packed envelope; number formatting is qa_net_js_number.
 *  - Nesting: the parser, the unpack/pack walks and the free are
 *    iterative. TS's unpack (and the walks its consumers make over a
 *    plain JSON.parse result) recurse on the JS stack and throw
 *    RangeError "Maximum call stack size exceeded." at a depth that
 *    depends on the stack and JIT state (bun 1.3.14: objects nested
 *    17500 deep failed in some runs, arrays decoded to 25000); C raises
 *    that RangeError for a node deeper than QA_SAVE_VALUE_MAX_DEPTH, the
 *    lowest depth measured to fail, after the whole text parsed.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_VALUE_H
#define QA_PERSISTENCE_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

noreturn void qa_save_format_error(const char *path, const char *format, ...);

/* See the nesting note above. */
#define QA_SAVE_VALUE_MAX_DEPTH 17500

typedef enum qa_save_value_kind {
  QA_SAVE_UNDEFINED,
  QA_SAVE_NULL,
  QA_SAVE_BOOLEAN,
  QA_SAVE_NUMBER,
  QA_SAVE_BIGINT,
  QA_SAVE_STRING,
  QA_SAVE_BYTES,
  QA_SAVE_ARRAY,
  QA_SAVE_OBJECT
} qa_save_value_kind;

typedef struct qa_save_value qa_save_value;
typedef struct qa_save_entry {
  char *key; /* owned */
  qa_save_value *value; /* owned */
  /* Bytes of `key` when it holds a NUL (a JSON "\u0000" escape), else 0 (strlen). Such a key
   * never matches a field name, as in TS. */
  size_t key_length;
} qa_save_entry;

struct qa_save_value {
  qa_save_value_kind kind;
  bool boolean;
  double number;
  char *text; /* owned: string value, or bigint decimal text */
  uint8_t *bytes; /* owned */
  size_t byte_length;
  qa_save_value **items; /* owned array of owned values */
  size_t item_count;
  qa_save_entry *entries; /* owned */
  size_t entry_count;
  /* Not data: a live handle a decoder attached to this node (TS trees hold objects such
   * as session-scoped identities). Owned; never encoded, and no reader or comparison
   * sees it. */
  void *attachment;
  size_t attachment_length;
  /* Bytes of a string `text` when it holds a NUL (a JSON "\u0000" escape, which a TS string
   * keeps), else 0 (strlen). The encoder writes the whole text; C-string readers see the
   * part before the first NUL; qa_save_reader_string_length reports the whole length. */
  size_t text_length;
  /* Not data: the whole paths of the readers opened under this node whose path outgrew the
   * reader's inline text (qa_save_reader.long_path); released with the node. */
  void *reader_paths;
};

/* Copies `length` bytes as the node's attachment, replacing any; false on allocation failure. */
bool qa_save_value_attach(qa_save_value *value, const void *data, size_t length);
const void *qa_save_value_attachment(const qa_save_value *value, size_t length);
/* Constructors; NULL only on allocation failure. */
qa_save_value *qa_save_value_undefined(void);
qa_save_value *qa_save_value_null(void);
qa_save_value *qa_save_value_boolean(bool value);
qa_save_value *qa_save_value_number(double value);
qa_save_value *qa_save_value_bigint_text(const char *decimal);
qa_save_value *qa_save_value_bigint_i64(int64_t value);
qa_save_value *qa_save_value_bigint_u64(uint64_t value);
qa_save_value *qa_save_value_string(const char *value);
qa_save_value *qa_save_value_bytes(const uint8_t *data, size_t length);
qa_save_value *qa_save_value_array(void);
qa_save_value *qa_save_value_object(void);
/* Take ownership of `child` on success; false only on OOM. */
bool qa_save_value_push(qa_save_value *array, qa_save_value *child);
bool qa_save_value_set(qa_save_value *object, const char *key, qa_save_value *child);
void qa_save_value_free(qa_save_value *value);

/* First matching entry, or NULL when absent / not an object. */
const qa_save_value *qa_save_value_get(const qa_save_value *object, const char *key);

/* Malloc'd envelope bytes; *out_length set. NULL only on OOM. */
uint8_t *qa_encode_checkpoint_value(const qa_save_value *value, size_t *out_length);
/* Owned decoded tree; NULL only on OOM. Malformed input aborts as the
 * TS throws (SaveFormatError, or the JSON/TextDecoder runtime error). */
qa_save_value *qa_decode_checkpoint_value(const uint8_t *bytes, size_t length);
/* Plain JSON.parse into a checkpoint tree (no $qts unpacking), for
 * the TS sites that bind a parsed root as unknown. The text is
 * decoded leniently (bytes kept as-is; no fatal UTF-8 check).
 * Returns NULL on OOM or parse failure instead of aborting when
 * `quiet` is set, for TS try/catch sites. */
qa_save_value *qa_parse_json_value(const uint8_t *bytes, size_t length, bool quiet);

typedef struct qa_save_reader {
  const qa_save_value *value; /* borrowed; NULL means TS undefined */
  /* Held inline so a reader abandoned by a thrown decode error owns nothing. Only error
   * text reads it. A path that does not fit keeps its first 255 bytes here, and the whole
   * path in long_path, a node chain owned by the value tree the reader walks (released with
   * that tree); qa_save_reader_path builds the whole text. */
  char path[256];
  const struct qa_save_path_node *long_path;
} qa_save_reader;

/* `path` NULL means "save". Returns false only on OOM. */
bool qa_save_reader_init(qa_save_reader *reader, const qa_save_value *value, const char *path);
void qa_save_reader_destroy(qa_save_reader *reader);
/* Fills `out` (caller destroys); false only on OOM. */
bool qa_save_reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out);
bool qa_save_reader_item(const qa_save_reader *reader, size_t index, qa_save_reader *out);
size_t qa_save_reader_list_count(const qa_save_reader *reader);
bool qa_save_reader_is_undefined(const qa_save_reader *reader);
bool qa_save_reader_is_null(const qa_save_reader *reader);

const char *qa_save_reader_string(const qa_save_reader *reader);
/* qa_save_reader_string with the whole byte length (TS string.length in UTF-8 bytes), which
 * exceeds strlen() when the text holds a NUL. */
const char *qa_save_reader_string_length(const qa_save_reader *reader, size_t *out_length);
bool qa_save_reader_boolean(const qa_save_reader *reader);
double qa_save_reader_number(const qa_save_reader *reader);
double qa_save_reader_finite(const qa_save_reader *reader);
/* TS integer(minimum): safe-integer check on the double. */
double qa_save_reader_integer(const qa_save_reader *reader);
double qa_save_reader_integer_min(const qa_save_reader *reader, double minimum);
int64_t qa_save_reader_bigint(const qa_save_reader *reader);
/* The bigint as an unsigned 64-bit value; false (out 0) when it is negative or above
 * 2^64 - 1 (TS compares the whole bigint). */
bool qa_save_reader_bigint_u64(const qa_save_reader *reader, uint64_t *out);
const char *qa_save_reader_bigint_text(const qa_save_reader *reader);
/* Borrowed bytes; *out_length set (may be NULL). */
const uint8_t *qa_save_reader_bytes(const qa_save_reader *reader, size_t *out_length);
const char *qa_save_reader_literal_string(const qa_save_reader *reader, const char *expected);
double qa_save_reader_literal_number(const qa_save_reader *reader, double expected);
bool qa_save_reader_literal_boolean(const qa_save_reader *reader, bool expected);
const char *qa_save_reader_choice_string(const qa_save_reader *reader,
                                         const char *const *choices, size_t count);
double qa_save_reader_choice_number(const qa_save_reader *reader,
                                    const double *choices, size_t count);
noreturn void qa_save_reader_fail(const qa_save_reader *reader, const char *format, ...);
/* The reader's whole path, malloc'd (NULL on OOM); for callers that raise their own
 * SaveFormatError with a reader's path. */
char *qa_save_reader_path(const qa_save_reader *reader);

/* Borrowed string value when it is a namespaced identity. */
const char *qa_namespaced(const qa_save_reader *reader);

#endif
