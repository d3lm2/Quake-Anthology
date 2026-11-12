/* Port of src/content/q3/base/game/save-module-values.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_module_values.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
qa_q3_game_entity *qa_q3_read_module_entity(const qa_save_reader *reader, qa_q3_entity_pool *pool) {
  double slot = qa_save_reader_integer_min(reader, 0);
  if (slot > 1023) qa_save_reader_fail(reader, "module source entity slot exceeds table extent");
  qa_q3_game_entity *e = qa_q3_entity_pool_get(pool, slot);
  if (!e) qa_save_reader_fail(reader, "module references an absent source entity slot");
  return e;
}
qa_q3_module_cvar_capture qa_q3_capture_module_cvar(const qa_cvar_snapshot *v) {
  /* flags: the snapshot's number as TS holds it (a restored value is not wrapped). */
  qa_q3_module_cvar_capture c = {v->name, v->value, v->reset_value, v->latched_value, v->flags_value, v->modified, v->modification_count, v->numeric_value, v->integer_value}; return c;
}
/* The tree holds the UTF-8 of the TS string; a vmCvar text is a registry text, one byte per character. */
static char *registry_text(const qa_save_reader *f) {
  char *bytes = NULL;
  if (!qa_command_text_from_utf8(qa_save_reader_string(f), &bytes)) qa_save_reader_fail(f, "cvar text requires byte characters");
  return bytes;
}
static char *rf_str(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); char *v = registry_text(&f); qa_save_reader_destroy(&f); return v; }
static void free_cvar_read(void *arg) { qa_cvar_read_free(arg); }
qa_cvar_snapshot qa_q3_read_module_cvar(const qa_save_reader *reader) {
  /* The texts read so far are released when a later field raises. */
  qa_cvar_snapshot v; memset(&v, 0, sizeof v); qa_save_reader f;
  qa_cleanup guard; qa_cleanup_push(&guard, free_cvar_read, &v);
  v.name = rf_str(reader, "name"); v.value = rf_str(reader, "value"); v.reset_value = rf_str(reader, "resetValue");
  qa_save_reader_field(reader, "latchedValue", &f); v.latched_value = qa_save_reader_is_null(&f) ? NULL : registry_text(&f); qa_save_reader_destroy(&f);
  /* TS keeps the number (flags_value); bit tests see its ToInt32 (flags). */
  qa_save_reader_field(reader, "flags", &f); v.flags_value = qa_save_reader_integer(&f); v.flags = qa_js_to_int32(v.flags_value); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "modified", &f); v.modified = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "modificationCount", &f); v.modification_count = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "numericValue", &f); v.numeric_value = qa_save_reader_number(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "integerValue", &f); v.integer_value = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
  qa_cleanup_pop(&guard);
  return v;
}
