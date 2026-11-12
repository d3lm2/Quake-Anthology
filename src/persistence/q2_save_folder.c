/* Port of src/persistence/q2-save-folder.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_save_folder.h"

#include <math.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

/* CS_NAME: the level's display name, which the original puts in the save slot's comment. */
#define CS_NAME 0
/* SV_WriteServerFile's comment buffer is 32 bytes, NUL included. */
#define COMMENT_BYTES 32

static noreturn void out_of_memory(void) { qa_raise("Error", 0, 0, "%s", "out of memory"); }
static void *checked(void *pointer) {
  if (pointer == NULL) out_of_memory();
  return pointer;
}
static char *dup_range(const char *text, size_t length) {
  char *copy = (char *)checked(malloc(length + 1));
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}
static char *dup_text(const char *text) { return dup_range(text, strlen(text)); }
static uint8_t *dup_bytes(const uint8_t *bytes, size_t length) {
  uint8_t *copy = (uint8_t *)checked(malloc(length == 0 ? 1 : length));
  if (length > 0) memcpy(copy, bytes, length);
  return copy;
}
static bool ends_with_fold(const char *text, const char *suffix) {
  size_t length = strlen(text), suffix_length = strlen(suffix);
  if (length < suffix_length) return false;
  for (size_t i = 0; i < suffix_length; i++) {
    char c = text[length - suffix_length + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}
/* NAME_PATTERN: /^[A-Za-z0-9_][A-Za-z0-9_-]*$/ */
static bool name_pattern(const char *name) {
  if (name[0] == '\0' || name[0] == '-') return false;
  for (const char *at = name; *at != '\0'; at++) {
    char c = *at;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

void qa_q2_save_files_init(qa_q2_save_files *files) { memset(files, 0, sizeof *files); }
void qa_q2_save_files_free(qa_q2_save_files *files) {
  for (size_t i = 0; i < files->count; i++) {
    free(files->files[i].name);
    free(files->files[i].bytes);
  }
  free(files->files);
  memset(files, 0, sizeof *files);
}
const qa_q2_save_file *qa_q2_save_files_get(const qa_q2_save_files *files, const char *name) {
  for (size_t i = 0; i < files->count; i++)
    if (strcmp(files->files[i].name, name) == 0) return &files->files[i];
  return NULL;
}
void qa_q2_save_files_set(qa_q2_save_files *files, const char *name, const uint8_t *bytes, size_t length) {
  uint8_t *copy = dup_bytes(bytes, length);
  for (size_t i = 0; i < files->count; i++) {
    if (strcmp(files->files[i].name, name) != 0) continue;
    free(files->files[i].bytes);
    files->files[i].bytes = copy;
    files->files[i].length = length;
    return;
  }
  if (files->count == files->capacity) {
    size_t capacity = files->capacity == 0 ? 8 : files->capacity * 2;
    files->files = (qa_q2_save_file *)checked(realloc(files->files, capacity * sizeof *files->files));
    files->capacity = capacity;
  }
  files->files[files->count].name = dup_text(name);
  files->files[files->count].bytes = copy;
  files->files[files->count].length = length;
  files->count++;
}

char *qa_q2_level_name(const char *map) {
  /* map.replace(/^maps\//, "").replace(/\.bsp$/i, "") */
  const char *start = strncmp(map, "maps/", 5) == 0 ? map + 5 : map;
  size_t length = strlen(start);
  if (ends_with_fold(start, ".bsp")) length -= 4;
  char *name = dup_range(start, length);
  if (!name_pattern(name)) {
    free(name);
    qa_save_format_error("q2.save", "map %s cannot be named in a save folder", map);
  }
  return name;
}

/* TS string length of a slot name. Slot names reach this from file listings and save paths,
 * UTF-8 text TS decodes; text that is not UTF-8 is a byte string, one character per byte. */
static size_t text_units(const char *text, size_t length) {
  size_t units = 0;
  for (size_t i = 0; i < length;) {
    unsigned char c = (unsigned char)text[i];
    size_t extra = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 && c >= 0xc2 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 && c <= 0xf4 ? 3 : 4;
    if (extra == 4 || i + extra >= length + (extra == 0)) return length;
    for (size_t k = 1; k <= extra; k++)
      if (((unsigned char)text[i + k] & 0xc0) != 0x80) return length;
    units += extra == 3 ? 2 : 1;
    i += extra + 1;
  }
  return units;
}
char *qa_q2_save_slot_name(const char *name) {
  size_t length = strlen(name);
  if (ends_with_fold(name, ".sav")) length -= 4;
  char *slot = dup_range(name, length);
  /* /\.\.|[\\/:*?"<>|\0]/ (a C string carries no NUL); length in UTF-16 units */
  size_t units = text_units(slot, length);
  bool bad = length == 0 || units > 64 || strstr(slot, "..") != NULL || strpbrk(slot, "\\/:*?\"<>|") != NULL;
  if (bad) {
    free(slot);
    qa_save_format_error("q2.save", "Bad savedir.");
  }
  if (qa_js_latin1_equals_ignore_case(slot, "current")) {
    free(slot);
    qa_save_format_error("q2.save", "Can't save to 'current'");
  }
  return slot;
}

void qa_parse_q2_map_command(const char *command, char **out_map, char **out_spawn_point) {
  const char *text = command[0] == '*' ? command + 1 : command;
  const char *dollar = strchr(text, '$');
  char *map = dollar == NULL ? dup_text(text) : dup_range(text, (size_t)(dollar - text));
  if (strchr(map, '+') != NULL || !name_pattern(map)) {
    free(map);
    qa_save_format_error("q2.save", "save names an unsupported map command: %s", command);
  }
  size_t length = strlen(map);
  char *path = (char *)checked(malloc(length + 10));
  (void)snprintf(path, length + 10, "maps/%s.bsp", map);
  free(map);
  *out_map = path;
  *out_spawn_point = dup_text(dollar == NULL ? "" : dollar + 1);
}
char *qa_q2_map_command(const char *map, const char *spawn_point) {
  char *level = qa_q2_level_name(map);
  if (spawn_point[0] == '\0') return level;
  size_t length = strlen(level) + 1 + strlen(spawn_point) + 1;
  char *command = (char *)checked(malloc(length));
  (void)snprintf(command, length, "%s$%s", level, spawn_point);
  free(level);
  return command;
}

void qa_q2_saved_cvars_free(qa_saved_cvar *cvars, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(cvars[i].name);
    free(cvars[i].value);
  }
  free(cvars);
}
typedef struct latched_scope {
  const uint8_t *registry;
  size_t registry_length;
  qa_cvar_registry *scratch;
  qa_saved_cvar *cvars;
  size_t count;
} latched_scope;
static void free_value(void *value) { qa_save_value_free((qa_save_value *)value); }
static void latched_body(void *raw) {
  latched_scope *scope = (latched_scope *)raw;
  qa_save_value *state = qa_decode_checkpoint_value(scope->registry, scope->registry_length);
  if (state == NULL) out_of_memory();
  qa_cleanup node;
  qa_cleanup_push(&node, free_value, state);
  qa_cvar_registry_restore_save_state(scope->scratch, state);
  qa_cleanup_pop(&node);
  qa_save_value_free(state);
  size_t count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_canonical_snapshots(scope->scratch, QA_Q2_CVAR_FLAG_LATCH, &count);
  scope->cvars = (qa_saved_cvar *)checked(calloc(count == 0 ? 1 : count, sizeof *scope->cvars));
  for (size_t i = 0; i < count; i++) {
    scope->cvars[i].name = dup_text(snapshots[i].name);
    scope->cvars[i].value = dup_text(snapshots[i].value);
    scope->count++;
    qa_cvar_read_free(&snapshots[i]);
  }
  free(snapshots);
}
qa_saved_cvar *qa_q2_latched_cvars(const uint8_t *registry, size_t registry_length, qa_command_dialect dialect,
                                   size_t *out_count) {
  /* TS scratchRegistry: a registry of its own under a fresh session identity. */
  qa_identity_owner owner;
  if (!qa_identity_owner_create(&owner, "q2-save-cvars")) out_of_memory();
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.session = owner.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  latched_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.registry = registry;
  scope.registry_length = registry_length;
  scope.scratch = qa_cvar_registry_create(&options);
  if (scope.scratch == NULL) {
    qa_identity_owner_destroy(&owner);
    out_of_memory();
  }
  qa_caught_error caught;
  bool completed = qa_try(latched_body, &scope, &caught);
  qa_cvar_registry_destroy(scope.scratch);
  qa_identity_owner_destroy(&owner);
  if (!completed) {
    qa_q2_saved_cvars_free(scope.cvars, scope.count);
    qa_rethrow(&caught);
  }
  *out_count = scope.count;
  return scope.cvars;
}

qa_q2_save_clock qa_q2_save_clock_now(void) {
  time_t now = time(NULL);
  struct tm local;
  memset(&local, 0, sizeof local);
  (void)localtime_r(&now, &local);
  qa_q2_save_clock clock = {local.tm_hour, local.tm_min, local.tm_mon + 1, local.tm_mday};
  return clock;
}
char *qa_q2_save_comment(const char *level_name, bool autosave, qa_q2_save_clock now) {
  size_t capacity = strlen(level_name) + 64;
  char *text = (char *)checked(malloc(capacity));
  if (autosave) (void)snprintf(text, capacity, "ENTERING %s", level_name);
  else
    (void)snprintf(text, capacity, "%2d:%d%d %2d/%2d  %s", now.hours, now.minutes / 10, now.minutes % 10, now.month, now.date,
                   level_name);
  /* text.slice(0, COMMENT_BYTES - 1) */
  if (strlen(text) > COMMENT_BYTES - 1) text[COMMENT_BYTES - 1] = '\0';
  return text;
}

/* The checkpoint level state as the container types: an index that is not an integer is outside the source range. */
static qa_q2_configstring *container_configstrings(const qa_q2_classic_level_state *state) {
  qa_q2_configstring *list = (qa_q2_configstring *)checked(calloc(state->configstring_count == 0 ? 1 : state->configstring_count, sizeof *list));
  for (size_t i = 0; i < state->configstring_count; i++) {
    double index = state->configstrings[i].index;
    if (!(index >= 0 && index < 9007199254740992.0) || index != floor(index)) {
      free(list);
      qa_save_format_error("q2.configstring", "configstring index outside the source range");
    }
    list[i].index = (int64_t)index;
    list[i].value = (char *)state->configstrings[i].value; /* borrowed for the encode */
  }
  return list;
}
static void level_files(qa_q2_save_files *files, const char *map, const uint8_t *level, size_t level_length,
                        const qa_q2_classic_level_state *state) {
  char *name = qa_q2_level_name(map);
  size_t name_length = strlen(name) + 5;
  char *file = (char *)checked(malloc(name_length));
  (void)snprintf(file, name_length, "%s.sav", name);
  qa_q2_save_files_set(files, file, level, level_length);
  (void)snprintf(file, name_length, "%s.sv2", name);
  free(name);
  qa_cleanup file_node;
  qa_cleanup_push(&file_node, free, file);
  qa_q2_level_metadata metadata;
  memset(&metadata, 0, sizeof metadata);
  metadata.configstrings = container_configstrings(state);
  metadata.configstring_count = state->configstring_count;
  qa_cleanup configstrings_node;
  qa_cleanup_push(&configstrings_node, free, metadata.configstrings);
  qa_q2_portal *portals = (qa_q2_portal *)checked(calloc(state->portal_count == 0 ? 1 : state->portal_count, sizeof *portals));
  for (size_t i = 0; i < state->portal_count; i++) {
    portals[i].portal = state->portals[i].portal;
    portals[i].open = state->portals[i].open;
  }
  qa_cleanup portals_node;
  qa_cleanup_push(&portals_node, free, portals);
  metadata.portal_bytes = qa_q2_classic_portal_bytes(portals, state->portal_count, &metadata.portal_length);
  if (metadata.portal_bytes == NULL) out_of_memory();
  qa_cleanup portal_bytes_node;
  qa_cleanup_push(&portal_bytes_node, free, metadata.portal_bytes);
  size_t encoded_length = 0;
  uint8_t *encoded = qa_encode_q2_classic_level_metadata(&metadata, &encoded_length);
  qa_cleanup_pop(&portal_bytes_node);
  qa_cleanup_pop(&portals_node);
  qa_cleanup_pop(&configstrings_node);
  qa_cleanup_pop(&file_node);
  free(metadata.portal_bytes);
  free(portals);
  free(metadata.configstrings);
  if (encoded == NULL) {
    free(file);
    out_of_memory();
  }
  qa_q2_save_files_set(files, file, encoded, encoded_length);
  free(encoded);
  free(file);
}

typedef struct latched_list {
  qa_saved_cvar *cvars;
  size_t count;
} latched_list;
static void free_latched(void *raw) {
  latched_list *list = (latched_list *)raw;
  qa_q2_saved_cvars_free(list->cvars, list->count);
}
typedef struct folder_scope {
  const qa_q2_classic_original_save *save;
  const qa_q2_classic_folder_context *context;
  qa_q2_save_files *out;
} folder_scope;
static void folder_body(void *raw) {
  folder_scope *scope = (folder_scope *)raw;
  const qa_q2_classic_original_save *save = scope->save;
  qa_q2_save_files *files = scope->out;
  const char *level_name = "";
  for (size_t i = 0; i < save->server.state.configstring_count; i++) {
    if (save->server.state.configstrings[i].index != CS_NAME) continue;
    level_name = save->server.state.configstrings[i].value;
    break;
  }
  qa_q2_classic_server_save server;
  memset(&server, 0, sizeof server);
  server.comment = qa_q2_save_comment(level_name, save->autosave, scope->context->now);
  qa_cleanup comment_node;
  qa_cleanup_push(&comment_node, free, server.comment);
  size_t latched_count = 0;
  qa_saved_cvar *latched = qa_q2_latched_cvars(save->server.cvars.bytes, save->server.cvars.length,
                                               QA_COMMAND_DIALECT_Q2_CLASSIC, &latched_count);
  /* SV_WriteServerFile prints `Cvar too long` and skips a pair that does not fit its 128 byte fields. */
  qa_saved_cvar *fitting = (qa_saved_cvar *)checked(calloc(latched_count == 0 ? 1 : latched_count, sizeof *fitting));
  for (size_t i = 0; i < latched_count; i++)
    /* cvar.name.length < 127 && cvar.value.length < 127 count UTF-16 units, not bytes. */
    if (text_units(latched[i].name, strlen(latched[i].name)) < 127 && text_units(latched[i].value, strlen(latched[i].value)) < 127)
      fitting[server.cvar_count++] = latched[i];
  server.cvars = fitting;
  server.map_command = (char *)scope->context->map_command;
  latched_list owned = {latched, latched_count};
  qa_cleanup fitting_node, latched_node;
  qa_cleanup_push(&fitting_node, free, fitting);
  qa_cleanup_push(&latched_node, free_latched, &owned);
  size_t encoded_length = 0;
  uint8_t *encoded = qa_encode_q2_classic_server(&server, &encoded_length);
  qa_cleanup_pop(&latched_node);
  qa_cleanup_pop(&fitting_node);
  qa_cleanup_pop(&comment_node);
  free(fitting);
  qa_q2_saved_cvars_free(latched, latched_count);
  free(server.comment);
  if (encoded == NULL) out_of_memory();
  qa_q2_save_files_set(files, "server.ssv", encoded, encoded_length);
  free(encoded);
  qa_q2_save_files_set(files, "game.ssv", save->game.bytes, save->game.length);
  if (!save->level_absent) level_files(files, save->map, save->level.bytes, save->level.length, &save->server.state);
  for (size_t i = 0; i < save->visited_level_count; i++) {
    const qa_q2_classic_visited_level *visited = &save->visited_levels[i];
    level_files(files, visited->map, visited->level.bytes, visited->level.length, &visited->state);
  }
}
void qa_q2_classic_folder(const qa_q2_classic_original_save *save, const qa_q2_classic_folder_context *context,
                          qa_q2_save_files *out) {
  qa_q2_save_files_init(out);
  folder_scope scope = {save, context, out};
  qa_caught_error caught;
  if (qa_try(folder_body, &scope, &caught)) return;
  qa_q2_save_files_free(out);
  qa_rethrow(&caught);
}

/* TS fileKey: the name in the folder that equals `wanted` without case. */
static const qa_q2_save_file *file_key(const qa_q2_save_files *files, const char *wanted) {
  for (size_t i = 0; i < files->count; i++)
    if (qa_js_equals_ignore_case(files->files[i].name, wanted)) return &files->files[i];
  return NULL;
}
/* Copies container configstrings and portal bytes into an owned checkpoint level state. */
static void import_level_state(const qa_q2_level_metadata *metadata, qa_q2_classic_level_state *out) {
  memset(out, 0, sizeof *out);
  out->configstrings =
      (qa_q2_classic_configstring_entry *)checked(calloc(metadata->configstring_count == 0 ? 1 : metadata->configstring_count, sizeof *out->configstrings));
  for (size_t i = 0; i < metadata->configstring_count; i++) {
    out->configstrings[i].index = (double)metadata->configstrings[i].index;
    out->configstrings[i].value = dup_text(metadata->configstrings[i].value);
    out->configstring_count++;
  }
  qa_q2_portal *portals = NULL;
  size_t portal_count = 0;
  if (!qa_q2_classic_portals_from(metadata->portal_bytes, metadata->portal_length, &portals, &portal_count)) out_of_memory();
  out->portals = (qa_q2_classic_portal_entry *)checked(calloc(portal_count == 0 ? 1 : portal_count, sizeof *out->portals));
  for (size_t i = 0; i < portal_count; i++) {
    out->portals[i].portal = portals[i].portal;
    out->portals[i].open = portals[i].open;
  }
  out->portal_count = portal_count;
  free(portals);
}

typedef struct folder_level {
  char *key;  /* q2LevelName(stem).toLowerCase() */
  char *name; /* the stem as the file spells it */
  const qa_q2_save_file *level;
  qa_q2_level_metadata metadata;
} folder_level;
typedef struct read_scope {
  const qa_q2_save_files *files;
  qa_q2_classic_original_import *out;
  qa_q2_classic_server_save server;
  folder_level *levels;
  size_t level_count;
} read_scope;
static int compare_level_names(const void *left, const void *right, void *ctx) {
  (void)ctx;
  return qa_js_locale_compare(((const folder_level *)left)->name, ((const folder_level *)right)->name);
}
static void read_body(void *raw) {
  read_scope *scope = (read_scope *)raw;
  const qa_q2_save_files *files = scope->files;
  qa_q2_classic_original_import *out = scope->out;
  const qa_q2_save_file *server_file = file_key(files, "server.ssv");
  const qa_q2_save_file *game = file_key(files, "game.ssv");
  if (server_file == NULL) qa_save_format_error("q2.save", "save folder has no server.ssv");
  if (game == NULL || game->length == 0) qa_save_format_error("q2.save", "save folder has no game.ssv");
  if (!qa_decode_q2_classic_server(server_file->bytes, server_file->length, &scope->server)) out_of_memory();
  qa_parse_q2_map_command(scope->server.map_command, &out->map, &out->spawn_point);
  scope->levels = (folder_level *)checked(calloc(files->count == 0 ? 1 : files->count, sizeof *scope->levels));
  for (size_t i = 0; i < files->count; i++) {
    const qa_q2_save_file *file = &files->files[i];
    if (!ends_with_fold(file->name, ".sav")) continue;
    char *stem = dup_range(file->name, strlen(file->name) - 4);
    qa_cleanup stem_node;
    qa_cleanup_push(&stem_node, free, stem);
    size_t metadata_name_length = strlen(stem) + 5;
    char *metadata_name = (char *)checked(malloc(metadata_name_length));
    (void)snprintf(metadata_name, metadata_name_length, "%s.sv2", stem);
    const qa_q2_save_file *metadata = file_key(files, metadata_name);
    free(metadata_name);
    if (metadata == NULL) qa_save_format_error("q2.save", "%s has no %s.sv2", file->name, stem);
    if (file->length == 0) qa_save_format_error("q2.save", "%s is empty", file->name);
    char *level_name = qa_q2_level_name(stem);
    char *key = qa_js_to_lower_case(level_name);
    free(level_name);
    if (key == NULL) out_of_memory();
    qa_cleanup key_node;
    qa_cleanup_push(&key_node, free, key);
    qa_q2_level_metadata decoded;
    if (!qa_decode_q2_classic_level_metadata(metadata->bytes, metadata->length, &decoded)) out_of_memory();
    qa_cleanup_pop(&key_node);
    qa_cleanup_pop(&stem_node);
    /* levels.set(key, ...): a repeated key keeps its place and takes the later file. */
    folder_level *slot = NULL;
    for (size_t at = 0; at < scope->level_count && slot == NULL; at++)
      if (strcmp(scope->levels[at].key, key) == 0) slot = &scope->levels[at];
    if (slot != NULL) {
      free(slot->key);
      free(slot->name);
      qa_q2_level_metadata_free(&slot->metadata);
    } else {
      slot = &scope->levels[scope->level_count++];
    }
    slot->key = key;
    slot->name = stem;
    slot->level = file;
    slot->metadata = decoded;
  }
  /* resume.map.replace(/^maps\//, "").replace(/\.bsp$/, "").toLowerCase() */
  size_t resume_length = strlen(out->map) - 9;
  char *resume_name = dup_range(out->map + 5, resume_length);
  char *resume_key = qa_js_to_lower_case(resume_name);
  free(resume_name);
  if (resume_key == NULL) out_of_memory();
  folder_level *current = NULL;
  for (size_t at = 0; at < scope->level_count && current == NULL; at++)
    if (strcmp(scope->levels[at].key, resume_key) == 0) current = &scope->levels[at];
  free(resume_key);
  /* The original records the autosave as a flag inside game.ssv; the comment is how its menu tells the two apart. */
  out->autosave = strncmp(scope->server.comment, "ENTERING ", 9) == 0;
  out->latched = (qa_q2_classic_latched_cvar *)checked(calloc(scope->server.cvar_count == 0 ? 1 : scope->server.cvar_count, sizeof *out->latched));
  for (size_t i = 0; i < scope->server.cvar_count; i++) {
    out->latched[i].name = dup_text(scope->server.cvars[i].name);
    out->latched[i].value = dup_text(scope->server.cvars[i].value);
    out->latched_count++;
  }
  out->game = dup_bytes(game->bytes, game->length);
  out->game_length = game->length;
  if (current != NULL) {
    import_level_state(&current->metadata, &out->server);
    out->level = dup_bytes(current->level->bytes, current->level->length);
    out->level_length = current->level->length;
    /* levels.delete(resume): the current level is not a visited one. */
    free(current->key);
    free(current->name);
    qa_q2_level_metadata_free(&current->metadata);
    size_t index = (size_t)(current - scope->levels);
    memmove(current, current + 1, (scope->level_count - index - 1) * sizeof *current);
    scope->level_count--;
  } else {
    out->level_absent = true;
    out->server.configstrings = (qa_q2_classic_configstring_entry *)checked(calloc(1, sizeof *out->server.configstrings));
    out->server.portals = (qa_q2_classic_portal_entry *)checked(calloc(1, sizeof *out->server.portals));
  }
  qa_js_stable_sort(scope->levels, scope->level_count, sizeof *scope->levels, compare_level_names, NULL);
  out->visited_levels =
      (qa_q2_classic_visited_level *)checked(calloc(scope->level_count == 0 ? 1 : scope->level_count, sizeof *out->visited_levels));
  for (size_t i = 0; i < scope->level_count; i++) {
    const folder_level *entry = &scope->levels[i];
    qa_q2_classic_visited_level *visited = &out->visited_levels[out->visited_level_count++];
    visited->version = 1;
    size_t map_length = strlen(entry->name) + 10;
    char *map = (char *)checked(malloc(map_length));
    (void)snprintf(map, map_length, "maps/%s.bsp", entry->name);
    visited->map = map;
    visited->level.bytes = dup_bytes(entry->level->bytes, entry->level->length);
    visited->level.length = entry->level->length;
    import_level_state(&entry->metadata, &visited->state);
  }
}
void qa_read_q2_classic_folder(const qa_q2_save_files *files, qa_q2_classic_original_import *out) {
  memset(out, 0, sizeof *out);
  read_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.files = files;
  scope.out = out;
  qa_caught_error caught;
  bool completed = qa_try(read_body, &scope, &caught);
  for (size_t i = 0; i < scope.level_count; i++) {
    free(scope.levels[i].key);
    free(scope.levels[i].name);
    qa_q2_level_metadata_free(&scope.levels[i].metadata);
  }
  free(scope.levels);
  qa_q2_classic_server_save_free(&scope.server);
  if (completed) return;
  qa_q2_classic_original_import_free(out);
  qa_rethrow(&caught);
}

void qa_q2_classic_folder_header_free(qa_q2_classic_folder_header *header) {
  free(header->comment);
  free(header->map);
  free(header->spawn_point);
  memset(header, 0, sizeof *header);
}
typedef struct header_scope {
  qa_q2_classic_server_save server;
  qa_q2_classic_folder_header *out;
} header_scope;
static void header_body(void *raw) {
  header_scope *scope = (header_scope *)raw;
  qa_parse_q2_map_command(scope->server.map_command, &scope->out->map, &scope->out->spawn_point);
}
void qa_q2_classic_folder_header_read(const uint8_t *server_bytes, size_t length, qa_q2_classic_folder_header *out) {
  memset(out, 0, sizeof *out);
  header_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.out = out;
  if (!qa_decode_q2_classic_server(server_bytes, length, &scope.server)) out_of_memory();
  qa_caught_error caught;
  bool completed = qa_try(header_body, &scope, &caught);
  if (completed) {
    out->comment = scope.server.comment;
    scope.server.comment = NULL;
    out->autosave = strncmp(out->comment, "ENTERING ", 9) == 0;
  }
  qa_q2_classic_server_save_free(&scope.server);
  if (!completed) qa_rethrow(&caught);
}

typedef struct digest_name {
  char *lowered;
  const qa_q2_save_file *file;
} digest_name;
static int compare_digest_names(const void *left, const void *right, void *ctx) {
  (void)ctx;
  return qa_js_locale_compare(((const digest_name *)left)->lowered, ((const digest_name *)right)->lowered);
}
void qa_q2_files_digest(const qa_q2_save_files *files, uint8_t out[QA_SAVE_DIGEST_BYTES]) {
  digest_name *names = (digest_name *)checked(calloc(files->count == 0 ? 1 : files->count, sizeof *names));
  for (size_t i = 0; i < files->count; i++) {
    names[i].lowered = (char *)checked(qa_js_to_lower_case(files->files[i].name));
    names[i].file = &files->files[i];
  }
  qa_js_stable_sort(names, files->count, sizeof *names, compare_digest_names, NULL);
  EVP_MD_CTX *hasher = EVP_MD_CTX_new();
  if (hasher == NULL || EVP_DigestInit_ex(hasher, EVP_sha256(), NULL) != 1) out_of_memory();
  for (size_t i = 0; i < files->count; i++) {
    const qa_q2_save_file *file = names[i].file;
    const uint8_t zero = 0;
    uint8_t header[4] = {(uint8_t)(file->length & 0xff), (uint8_t)((file->length >> 8) & 0xff),
                         (uint8_t)((file->length >> 16) & 0xff), (uint8_t)((file->length >> 24) & 0xff)};
    (void)EVP_DigestUpdate(hasher, names[i].lowered, strlen(names[i].lowered));
    (void)EVP_DigestUpdate(hasher, &zero, 1);
    (void)EVP_DigestUpdate(hasher, header, sizeof header);
    (void)EVP_DigestUpdate(hasher, file->bytes, file->length);
    free(names[i].lowered);
  }
  free(names);
  unsigned int digest_length = 0;
  (void)EVP_DigestFinal_ex(hasher, out, &digest_length);
  EVP_MD_CTX_free(hasher);
}
