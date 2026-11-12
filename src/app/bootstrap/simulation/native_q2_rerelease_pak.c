/* Port of src/app/bootstrap/simulation/native-q2-rerelease-pak.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_q2_rerelease_pak.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/q2_containers.h"
#include "qa/persistence/q2_save_folder.h"
#include "qa/persistence/value.h"

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = checked(malloc(length + 1));
  memcpy(copy, text, length + 1);
  return copy;
}
/* Rerelease configstrings are the guest's text through TextDecoder (readGuestString), so the C services keep them as
 * the UTF-8 of the TS string, and the checkpoint JSON carries that text unchanged. The pak's `.sv2` and server.ssv
 * hold bytes: byteText reads one character per byte (U+0000..U+00FF, written here as its UTF-8) and textBytes writes
 * one byte per character, refusing a character above U+00FF. */
static char *byte_text(const char *bytes) {
  size_t length = strlen(bytes), extra = 0;
  for (size_t i = 0; i < length; i++) extra += (unsigned char)bytes[i] >= 0x80;
  char *out = checked(malloc(length + extra + 1));
  size_t o = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)bytes[i];
    if (c < 0x80) out[o++] = (char)c;
    else { out[o++] = (char)(0xc0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3f)); }
  }
  out[o] = '\0';
  return out;
}
static char *text_bytes(const char *text) {
  size_t length = strlen(text);
  char *out = checked(malloc(length + 1));
  size_t o = 0;
  for (size_t i = 0; i < length;) {
    unsigned char c = (unsigned char)text[i];
    if (c < 0x80) { out[o++] = (char)c; i++; continue; }
    if ((c == 0xc2 || c == 0xc3) && i + 1 < length && ((unsigned char)text[i + 1] & 0xc0) == 0x80) {
      out[o++] = (char)(((c & 0x03) << 6) | ((unsigned char)text[i + 1] & 0x3f)); i += 2; continue;
    }
    free(out);
    qa_save_format_error("text", "source save requires byte characters");
  }
  out[o] = '\0';
  return out;
}
typedef struct owned_strings { char **values; size_t count; } owned_strings;
static void owned_strings_free(void *arg) { owned_strings *o = arg; for (size_t i = 0; i < o->count; i++) free(o->values[i]); free(o->values); o->values = NULL; o->count = 0; }
static uint8_t *dup_bytes(const uint8_t *bytes, size_t length) {
  uint8_t *copy = checked(malloc(length == 0 ? 1 : length));
  if (length != 0) memcpy(copy, bytes, length);
  return copy;
}
static char *join(const char *a, const char *b, const char *c) {
  size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
  char *out = checked(malloc(la + lb + lc + 1));
  memcpy(out, a, la); memcpy(out + la, b, lb); memcpy(out + la + lb, c, lc + 1);
  return out;
}

const char *qa_app_sim_rerelease_autosave_name(bool coop) { return coop ? "coop_save0" : "save0"; }
char *qa_app_sim_rerelease_slot_name(const char *name) {
  /* name.replace(/\.pak\.gz$/i, "") */
  size_t length = strlen(name);
  char *stem = dup_text(name);
  if (qa_js_ends_with_ignore_case(name, ".pak.gz")) stem[length - 7] = '\0';
  qa_cleanup node; qa_cleanup_push(&node, free, stem);
  char *slot = qa_q2_save_slot_name(stem);
  qa_cleanup_pop(&node);
  free(stem);
  return slot;
}

void qa_app_sim_q2_rerelease_pak_entries_free(qa_app_sim_q2_rerelease_pak_entries *entries) {
  if (entries == NULL) return;
  for (size_t i = 0; i < entries->count; i++) { free(entries->entries[i].name); free(entries->entries[i].bytes); }
  free(entries->entries);
  memset(entries, 0, sizeof *entries);
}
/* Takes ownership of name and bytes. */
static void push_entry(qa_app_sim_q2_rerelease_pak_entries *entries, char *name, uint8_t *bytes, size_t length) {
  if (entries->count == entries->capacity) {
    size_t capacity = entries->capacity == 0 ? 8 : entries->capacity * 2;
    entries->entries = checked(realloc(entries->entries, capacity * sizeof *entries->entries));
    entries->capacity = capacity;
  }
  entries->entries[entries->count++] = (qa_app_sim_q2_rerelease_pak_entry){name, bytes, length};
}

/* TS levelEntries: `NAME.sav` (the level's JSON) and `NAME.sv2` (its configstrings and one byte per area portal). */
static void level_entries(qa_app_sim_q2_rerelease_pak_entries *out, const char *map, const qa_compat_q2_rerelease_source_save *level,
    const qa_app_sim_q2_rerelease_level_state *state, const qa_app_sim_q2_rerelease_pak_context *context) {
  char *name = qa_q2_level_name(map);
  qa_cleanup name_node; qa_cleanup_push(&name_node, free, name);
  qa_q2_level_metadata metadata; memset(&metadata, 0, sizeof metadata);
  metadata.configstrings = checked(calloc(state->configstring_count == 0 ? 1 : state->configstring_count, sizeof *metadata.configstrings));
  qa_cleanup strings_node; qa_cleanup_push(&strings_node, free, metadata.configstrings);
  metadata.configstring_count = state->configstring_count;
  owned_strings bytes_text; bytes_text.count = 0; bytes_text.values = checked(calloc(state->configstring_count == 0 ? 1 : state->configstring_count, sizeof *bytes_text.values));
  qa_cleanup text_node; qa_cleanup_push(&text_node, owned_strings_free, &bytes_text);
  for (size_t i = 0; i < state->configstring_count; i++) {
    metadata.configstrings[i].index = (int64_t)state->configstrings[i].index;
    /* textBytes(entry.value) inside packConfigstrings */
    metadata.configstrings[i].value = bytes_text.values[bytes_text.count++] = text_bytes(state->configstrings[i].value);
  }
  qa_q2_portal *portals = checked(calloc(state->portal_count == 0 ? 1 : state->portal_count, sizeof *portals));
  qa_cleanup portals_node; qa_cleanup_push(&portals_node, free, portals);
  for (size_t i = 0; i < state->portal_count; i++) portals[i] = (qa_q2_portal){state->portals[i].portal, state->portals[i].open};
  double count = context->portal_count(context->ctx, map);
  metadata.portal_bytes = checked(qa_q2_rerelease_portal_bytes(portals, state->portal_count, count, &metadata.portal_length));
  qa_cleanup bytes_node; qa_cleanup_push(&bytes_node, free, metadata.portal_bytes);
  size_t encoded_length = 0;
  uint8_t *encoded = checked(qa_encode_q2_rerelease_level_metadata(&metadata, &encoded_length));
  qa_cleanup_pop(&bytes_node); qa_cleanup_pop(&portals_node); qa_cleanup_pop(&text_node); qa_cleanup_pop(&strings_node); qa_cleanup_pop(&name_node);
  owned_strings_free(&bytes_text);
  push_entry(out, join(name, ".sav", ""), dup_bytes(level->native, level->native_length), level->native_length);
  push_entry(out, join(name, ".sv2", ""), encoded, encoded_length);
  free(metadata.portal_bytes); free(portals); free(metadata.configstrings); free(name);
}

static void entries_cleanup(void *entries) { qa_app_sim_q2_rerelease_pak_entries_free(entries); }
/* The comment and map command this file built, and the latched cvars; released when the encode refuses. */
static void server_cleanup(void *arg) {
  qa_q2_rerelease_server_save *server = arg;
  free(server->comment); free(server->map_command); qa_q2_saved_cvars_free(server->cvars, server->cvar_count);
  server->comment = server->map_command = NULL; server->cvars = NULL; server->cvar_count = 0;
}
void qa_app_sim_q2_rerelease_pak_entries_of(const qa_app_sim_q2_rerelease_native_save *save,
    const qa_app_sim_q2_rerelease_pak_context *context, qa_app_sim_q2_rerelease_pak_entries *out) {
  memset(out, 0, sizeof *out);
  qa_cleanup out_node; qa_cleanup_push(&out_node, entries_cleanup, out); /* a refused entry keeps none */
  char *level_name = qa_q2_level_name(save->map);
  qa_cleanup name_node; qa_cleanup_push(&name_node, free, level_name);
  qa_q2_rerelease_server_save server; memset(&server, 0, sizeof server);
  server.cvars = qa_q2_latched_cvars(save->server_cvars.bytes, save->server_cvars.length, QA_COMMAND_DIALECT_Q2_RERELEASE, &server.cvar_count);
  qa_cleanup server_node; qa_cleanup_push(&server_node, server_cleanup, &server);
  /* save.server.configstrings.find(entry => entry.index === 0)?.value ?? levelName */
  const char *comment = level_name;
  for (size_t i = 0; i < save->server.configstring_count; i++)
    if (save->server.configstrings[i].index == 0) { comment = save->server.configstrings[i].value; break; }
  /* BigInt(Math.floor(context.now.getTime() / 1000)) */
  double seconds = floor(context->now_milliseconds / 1000);
  if (!isfinite(seconds))
    qa_raise("RangeError", 0, 0, "The number %s cannot be converted to a BigInt because it is not an integer", QA_JS_NUMBER(seconds));
  server.timestamp = seconds < 0 ? (uint64_t)(int64_t)seconds : (uint64_t)seconds;
  server.kind = save->autosave ? 1 : 0;
  /* writeLengthString(comment): textBytes of the configstring text (the level name is already bytes). */
  server.comment = comment == level_name ? dup_text(comment) : text_bytes(comment);
  /* The map command and the latched cvars are command-text strings: TS keeps them as byte characters end to end (the
   * cvar registry and command text refuse anything above U+00FF), which C holds as the raw bytes, so textBytes and
   * byteText are the identity for them here (unlike the configstrings above, which are TextDecoder text). */
  server.map_command = qa_q2_map_command(save->map, context->spawn_point);
  size_t server_length = 0;
  uint8_t *server_bytes = checked(qa_encode_q2_rerelease_server(&server, &server_length));
  qa_cleanup_pop(&server_node);
  server_cleanup(&server);
  qa_cleanup_pop(&name_node);
  free(level_name);
  push_entry(out, dup_text("game.ssv"), dup_bytes(save->game.native, save->game.native_length), save->game.native_length);
  push_entry(out, dup_text("server.ssv"), server_bytes, server_length);
  if (!save->level_absent) level_entries(out, save->map, &save->level, &save->server, context);
  for (size_t i = 0; i < save->visited_level_count; i++) {
    const qa_app_sim_q2_rerelease_visited_level *visited = &save->visited_levels[i];
    level_entries(out, visited->map, &visited->level, &visited->state, context);
  }
  qa_cleanup_pop(&out_node);
}

uint8_t *qa_app_sim_q2_rerelease_pak(const qa_app_sim_q2_rerelease_native_save *save,
    const qa_app_sim_q2_rerelease_pak_context *context, size_t *out_length) {
  qa_app_sim_q2_rerelease_pak_entries entries;
  qa_app_sim_q2_rerelease_pak_entries_of(save, context, &entries);
  qa_kex_q2_pack_entry *pack = checked(calloc(entries.count == 0 ? 1 : entries.count, sizeof *pack));
  for (size_t i = 0; i < entries.count; i++)
    pack[i] = (qa_kex_q2_pack_entry){entries.entries[i].name, entries.entries[i].bytes, entries.entries[i].length};
  uint8_t *pak = checked(qa_pack_kex_q2_save_archive(pack, entries.count, out_length));
  free(pack);
  qa_app_sim_q2_rerelease_pak_entries_free(&entries);
  return pak;
}

/* ---- readQ2RereleasePak ---- */
static void *own(qa_app_sim_q2_rerelease_native_import *import, void *pointer) {
  checked(pointer);
  if (import->owned_count == import->owned_capacity) {
    size_t capacity = import->owned_capacity == 0 ? 16 : import->owned_capacity * 2;
    void **grown = realloc(import->owned, capacity * sizeof *grown);
    if (grown == NULL) { free(pointer); qa_range_error("Out of memory"); }
    import->owned = grown; import->owned_capacity = capacity;
  }
  import->owned[import->owned_count++] = pointer;
  return pointer;
}
typedef struct level_pair { const qa_kex_q2_save_entry *level; qa_q2_level_metadata metadata; } level_pair;
/* TS pair(archive, name): the level's `.sav` and its decoded `.sv2`. The metadata is owned by the caller. */
static level_pair pair(const qa_kex_q2_save_archive *archive, const char *name) {
  char *lower = checked(qa_js_to_lower_case(name));
  char *sav = join(lower, ".sav", ""), *sv2 = join(lower, ".sv2", "");
  free(lower);
  const qa_kex_q2_save_entry *level = qa_kex_q2_save_archive_entry(archive, sav);
  const qa_kex_q2_save_entry *metadata = qa_kex_q2_save_archive_entry(archive, sv2);
  free(sav); free(sv2);
  if (level == NULL || metadata == NULL) qa_save_format_error("q2-kex", "level %s needs both its .sav and .sv2 entries", name);
  level_pair out; memset(&out, 0, sizeof out);
  out.level = level;
  if (!qa_decode_q2_rerelease_level_metadata(metadata->bytes, metadata->length, &out.metadata)) qa_range_error("Out of memory");
  return out;
}
/* TS source(bytes): readRereleaseSourceSave over { native: bytes, deferredDamage: [], projections: [] }. The native
 * bytes are copied into the import. */
static qa_compat_q2_rerelease_source_save source(qa_app_sim_q2_rerelease_native_import *import, const uint8_t *bytes, size_t length) {
  qa_save_value *value = checked(qa_save_value_object());
  qa_cleanup node; qa_cleanup_push(&node, (void (*)(void *))qa_save_value_free, value);
  if (!qa_save_value_set(value, "native", checked(qa_save_value_bytes(bytes, length)))
      || !qa_save_value_set(value, "deferredDamage", checked(qa_save_value_array()))
      || !qa_save_value_set(value, "projections", checked(qa_save_value_array()))) qa_range_error("Out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q2-kex.json")) qa_range_error("Out of memory");
  qa_compat_q2_rerelease_source_save read = qa_app_sim_read_rerelease_source_save(&reader);
  qa_save_reader_destroy(&reader);
  qa_app_sim_rerelease_source_save_free(&read); /* both lists are empty */
  qa_cleanup_pop(&node);
  qa_save_value_free(value);
  qa_compat_q2_rerelease_source_save out; memset(&out, 0, sizeof out);
  out.native = own(import, dup_bytes(bytes, length)); out.native_length = length;
  return out;
}
/* The configstrings and open portals of a decoded `.sv2`, copied into the import. */
static qa_app_sim_q2_rerelease_level_state level_state(qa_app_sim_q2_rerelease_native_import *import, const qa_q2_level_metadata *metadata) {
  qa_app_sim_q2_rerelease_level_state state; memset(&state, 0, sizeof state);
  state.configstrings = own(import, calloc(metadata->configstring_count == 0 ? 1 : metadata->configstring_count, sizeof *state.configstrings));
  state.configstring_count = metadata->configstring_count;
  for (size_t i = 0; i < metadata->configstring_count; i++) {
    state.configstrings[i].index = (double)metadata->configstrings[i].index;
    state.configstrings[i].value = own(import, byte_text(metadata->configstrings[i].value)); /* byteText */
  }
  qa_q2_portal *portals = NULL; size_t portal_count = 0;
  if (!qa_q2_rerelease_portals_from(metadata->portal_bytes, metadata->portal_length, &portals, &portal_count)) qa_range_error("Out of memory");
  state.portals = own(import, calloc(portal_count == 0 ? 1 : portal_count, sizeof *state.portals));
  state.portal_count = portal_count;
  for (size_t i = 0; i < portal_count; i++) state.portals[i] = (qa_app_sim_q2_rerelease_portal){portals[i].portal, portals[i].open};
  free(portals);
  return state;
}
static int compare_stems(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static bool ends_with(const char *text, size_t length, const char *suffix) {
  size_t n = strlen(suffix);
  return length >= n && memcmp(text + length - n, suffix, n) == 0;
}
/* /^(.+)\.(sav|sv2)$/: the stem's length, or 0 when the name does not match. `.` matches no line terminator. */
static size_t level_stem(const char *name) {
  size_t length = strlen(name);
  if (length < 5 || !(ends_with(name, length, ".sav") || ends_with(name, length, ".sv2"))) return 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)name[i];
    if (c == '\n' || c == '\r') return 0;
    if (c == 0xe2 && i + 2 < length && (unsigned char)name[i + 1] == 0x80
        && ((unsigned char)name[i + 2] == 0xa8 || (unsigned char)name[i + 2] == 0xa9)) return 0;
  }
  return length - 4;
}

void qa_app_sim_read_q2_rerelease_pak(const qa_kex_q2_save_archive *archive, qa_app_sim_q2_rerelease_native_import *out) {
  memset(out, 0, sizeof *out);
  const qa_kex_q2_save_entry *game_bytes = qa_kex_q2_save_archive_entry(archive, "game.ssv");
  if (game_bytes == NULL) qa_save_format_error("q2-kex", "save archive has no game.ssv");
  char *map = NULL, *spawn_point = NULL;
  /* parseQ2MapCommand(archive.server.mapCommand): byteText, one character per byte, which C keeps as the raw bytes. */
  qa_parse_q2_map_command(archive->server.map_command, &map, &spawn_point);
  out->map = own(out, map); out->spawn_point = own(out, spawn_point);
  /* resume.map.replace(/^maps\//, "").replace(/\.bsp$/, "").toLowerCase() */
  const char *start = strncmp(map, "maps/", 5) == 0 ? map + 5 : map;
  size_t resume_length = strlen(start);
  if (ends_with(start, resume_length, ".bsp")) resume_length -= 4;
  char *resume_name = own(out, checked(malloc(resume_length + 1)));
  memcpy(resume_name, start, resume_length); resume_name[resume_length] = '\0';
  char *resume_lower = own(out, qa_js_to_lower_case(resume_name));
  /* The stems in first-seen order (a Set), then sorted. */
  char **stems = own(out, calloc(archive->file_count == 0 ? 1 : archive->file_count, sizeof *stems));
  size_t stem_count = 0;
  for (size_t i = 0; i < archive->file_count; i++) {
    const char *name = archive->files[i];
    if (strcmp(name, "game.ssv") == 0 || strcmp(name, "server.ssv") == 0 || name[0] == '.') continue;
    size_t length = level_stem(name);
    if (length == 0) qa_save_format_error("q2-kex", "save archive holds an unknown entry %s", name);
    bool seen = false;
    for (size_t j = 0; j < stem_count && !seen; j++) seen = strlen(stems[j]) == length && memcmp(stems[j], name, length) == 0;
    if (seen) continue;
    char *stem = own(out, checked(malloc(length + 1)));
    memcpy(stem, name, length); stem[length] = '\0';
    stems[stem_count++] = stem;
  }
  qa_js_sort(stems, stem_count, sizeof *stems, compare_stems);
  out->visited_levels = own(out, calloc(stem_count == 0 ? 1 : stem_count, sizeof *out->visited_levels));
  level_pair current; memset(&current, 0, sizeof current); bool has_current = false;
  for (size_t i = 0; i < stem_count; i++) {
    level_pair entry = pair(archive, stems[i]);
    if (strcmp(stems[i], resume_lower) == 0) {
      if (has_current) qa_q2_level_metadata_free(&current.metadata);
      current = entry; has_current = true;
      continue;
    }
    qa_cleanup node; qa_cleanup_push(&node, (void (*)(void *))qa_q2_level_metadata_free, &entry.metadata);
    qa_app_sim_q2_rerelease_visited_level *visited = &out->visited_levels[out->visited_level_count];
    visited->version = 1;
    visited->map = own(out, join("maps/", stems[i], ".bsp"));
    visited->level = source(out, entry.level->bytes, entry.level->length);
    visited->state = level_state(out, &entry.metadata);
    out->visited_level_count++;
    qa_cleanup_pop(&node);
    qa_q2_level_metadata_free(&entry.metadata);
  }
  qa_cleanup current_node; qa_cleanup_push(&current_node, (void (*)(void *))qa_q2_level_metadata_free, &current.metadata);
  out->autosave = archive->server.kind == 1;
  /* latched: archive.server.cvars (byteText: byte characters, the raw bytes here, as the cvar registry holds them) */
  out->latched = own(out, calloc(archive->server.cvar_count == 0 ? 1 : archive->server.cvar_count, sizeof *out->latched));
  out->latched_count = archive->server.cvar_count;
  for (size_t i = 0; i < archive->server.cvar_count; i++) {
    out->latched[i].name = own(out, dup_text(archive->server.cvars[i].name));
    out->latched[i].value = own(out, dup_text(archive->server.cvars[i].value));
  }
  if (has_current) out->server = level_state(out, &current.metadata);
  else {
    out->server.configstrings = own(out, calloc(1, sizeof *out->server.configstrings));
    out->server.portals = own(out, calloc(1, sizeof *out->server.portals));
  }
  out->game = source(out, game_bytes->bytes, game_bytes->length);
  if (has_current) out->level = source(out, current.level->bytes, current.level->length);
  else out->level_absent = true;
  qa_cleanup_pop(&current_node);
  if (has_current) qa_q2_level_metadata_free(&current.metadata);
}
