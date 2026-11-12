/* Port of src/app/bootstrap/q2-console.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_console.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/base/player/index.h"
#include "qa/formats/text_decoder.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
struct qa_app_q2_console { qa_app_q2_console_options options; };
typedef struct forward_ctx { qa_app_q2_console *console; const char *name; } forward_ctx;
struct qa_app_q2_console_binding {
  qa_command_buffer *commands;
  qa_app_q2_console *console;
  const char *names[5];
  qa_command_handler_fn handlers[5];
  void *ctxs[5];
  size_t count;
  /* Each bind's forwarding closures capture its own console (TS closes over `options`). */
  forward_ctx forwards[3];
};
const char *qa_app_q2_operator_player_name(const char *userinfo, const char *name,
    char *scratch, size_t scratch_size) {
  const char *value = qa_q2_userinfo_get(userinfo, "name", scratch, scratch_size);
  return value != NULL ? value : name;
}
qa_app_q2_console *qa_app_q2_console_create(qa_app_q2_console_options options) {
  if (!options.has_q2_source(options.ctx)) qa_raise("Error", 0, 0, "Q2 console requires a Q2 source runtime");
  qa_app_q2_console *console = calloc(1, sizeof *console);
  if (console == NULL) qa_range_error("Out of memory");
  console->options = options;
  return console;
}
void qa_app_q2_console_destroy(qa_app_q2_console *console) { free(console); }
qa_cvar_registry *qa_app_q2_console_cvars(qa_app_q2_console *console) {
  qa_cvar_registry *cvars = console->options.q2_server_cvars(console->options.ctx);
  if (cvars == NULL) qa_raise("Error", 0, 0, "Q2 console requires the simulation's source registry");
  return cvars;
}
static void forward_handler(void *v, qa_command_invocation *invocation) {
  forward_ctx *ctx = v;
  size_t count = invocation->argc > 0 ? invocation->argc - 1 : 0;
  ctx->console->options.execute(ctx->console->options.ctx, ctx->name,
      count > 0 ? (const char *const *)(invocation->argv + 1) : NULL, count, invocation->source);
}
static void print_text(qa_app_q2_console *console, const char *text) {
  console->options.print(console->options.ctx, text);
}
/* `selection ${axis}: ${outcome} (${requested}) ${detail}\n` */
static void print_selection_entry(void *v, const char *axis, const char *outcome, const char *requested, const char *detail) {
  qa_app_q2_console *console = v;
  size_t length = strlen(axis) + strlen(outcome) + strlen(requested) + strlen(detail) + 32;
  char *line = malloc(length);
  if (line == NULL) qa_range_error("Out of memory");
  snprintf(line, length, "selection %s: %s (%s) %s\n", axis, outcome, requested, detail);
  print_text(console, line);
  free(line);
}
/* q2OperatorPlayerName, whole (a malloc'd copy). */
static char *operator_name(const qa_app_q2_console_player *player) {
  size_t length = 0;
  const char *value = qa_q2_userinfo_find(player->userinfo, "name", &length);
  if (value == NULL) { value = player->name; length = strlen(value); }
  char *out = malloc(length + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, value, length);
  out[length] = '\0';
  return out;
}
static char *format_line(const char *format, const char *a, const char *b, const char *c, const char *d) {
  size_t size = strlen(format) + strlen(a) + strlen(b) + strlen(c) + strlen(d) + 1;
  char *out = malloc(size);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, size, format, a, b, c, d);
  return out;
}
/* UTF-16 units of UTF-8 text (String.prototype.length). */
static size_t utf16_units(const char *text, size_t bytes) {
  size_t units = 0;
  for (size_t i = 0; i < bytes; i++) {
    unsigned char byte = (unsigned char)text[i];
    if ((byte & 0xc0) != 0x80) units += byte >= 0xf0 ? 2 : 1;
  }
  return units;
}
static void status_handler(void *v, qa_command_invocation *invocation) {
  (void)invocation;
  qa_app_q2_console *console = v;
  if (!console->options.has_q2_source(console->options.ctx)) { print_text(console, "No server running.\n"); return; }
  const char *map_name = console->options.map_name(console->options.ctx);
  char *header = format_line("map              : %s\nnum score ping name\n", map_name, "", "", "");
  qa_cleanup node;
  qa_cleanup_push(&node, free, header);
  print_text(console, header);
  qa_cleanup_pop(&node);
  free(header);
  const qa_app_q2_console_player *players = NULL;
  size_t count = console->options.players(console->options.ctx, &players);
  for (size_t i = 0; i < count; i++) {
    if (!players[i].connected) continue;
    char *name = operator_name(&players[i]);
    char slot[32], score[32], ping[32];
    snprintf(slot, sizeof slot, "%s", QA_JS_NUMBER(players[i].slot));
    snprintf(score, sizeof score, "%s", QA_JS_NUMBER(players[i].score));
    snprintf(ping, sizeof ping, "%s", QA_JS_NUMBER(players[i].ping));
    char *line = format_line("%s %s %s %s\n", slot, score, ping, name);
    free(name);
    qa_cleanup_push(&node, free, line);
    print_text(console, line);
    qa_cleanup_pop(&node);
    free(line);
  }
  if (console->options.selection_consumption != NULL)
    console->options.selection_consumption(console->options.ctx, print_selection_entry, console);
}
static bool all_digits(const char *text) {
  if (text[0] == '\0') return false;
  for (const char *p = text; *p != '\0'; p++) if (!isdigit((unsigned char)*p)) return false;
  return true;
}
static void dumpuser_handler(void *v, qa_command_invocation *invocation) {
  qa_app_q2_console *console = v;
  size_t arg_count = invocation->argc > 0 ? invocation->argc - 1 : 0;
  const char *target = arg_count > 0 ? invocation->argv[1] : NULL;
  if (target == NULL || arg_count != 1) { print_text(console, "Usage: dumpuser <player name|slot>\n"); return; }
  const qa_app_q2_console_player *players = NULL;
  size_t count = console->options.has_q2_source(console->options.ctx)
      ? console->options.players(console->options.ctx, &players) : 0;
  /* [...players].find(...): the first match. */
  const qa_app_q2_console_player *player = NULL;
  bool digits = all_digits(target);
  double slot = digits ? strtod(target, NULL) : 0;
  for (size_t i = 0; i < count && player == NULL; i++) {
    if (!players[i].connected) continue;
    bool match;
    if (digits) match = players[i].slot == slot;
    else {
      char *name = operator_name(&players[i]);
      match = strcmp(name, target) == 0;
      free(name);
    }
    if (match) player = &players[i];
  }
  qa_cleanup node;
  if (player == NULL) {
    char *line = format_line("Player %s is not on the server\n", target, "", "", "");
    qa_cleanup_push(&node, free, line);
    print_text(console, line);
    qa_cleanup_pop(&node);
    free(line);
    return;
  }
  print_text(console, "userinfo\n--------\n");
  /* q2Userinfo: \key\value pairs in source order, the first of a repeated key kept (a Map that skips
   * keys it has), a trailing unpaired token dropped; printed as `${key.padEnd(20)}${value}\n`. */
  const char *source = player->userinfo;
  const char *p = source[0] == '\\' ? source + 1 : source;
  while (*p != '\0') {
    const char *key_end = strchr(p, '\\');
    if (key_end == NULL) break;
    const char *value = key_end + 1;
    const char *value_end = strchr(value, '\\');
    size_t key_len = (size_t)(key_end - p);
    size_t value_len = value_end != NULL ? (size_t)(value_end - value) : strlen(value);
    const char *next = value_end != NULL ? value_end + 1 : value + value_len;
    bool repeated = false;
    for (const char *q = source[0] == '\\' ? source + 1 : source; q < p && !repeated;) {
      const char *q_key_end = strchr(q, '\\');
      const char *q_value_end = strchr(q_key_end + 1, '\\');
      if ((size_t)(q_key_end - q) == key_len && memcmp(q, p, key_len) == 0) repeated = true;
      q = q_value_end + 1;
    }
    p = next;
    if (repeated) continue;
    size_t units = utf16_units(key_end - key_len, key_len);
    size_t pad = units < 20 ? 20 - units : 0;
    char *line = malloc(key_len + pad + value_len + 2);
    if (line == NULL) qa_range_error("Out of memory");
    memcpy(line, key_end - key_len, key_len);
    memset(line + key_len, ' ', pad);
    memcpy(line + key_len + pad, value, value_len);
    line[key_len + pad + value_len] = '\n';
    line[key_len + pad + value_len + 1] = '\0';
    qa_cleanup_push(&node, free, line);
    print_text(console, line);
    qa_cleanup_pop(&node);
    free(line);
  }
}
qa_app_q2_console_binding *qa_app_q2_console_bind(qa_app_q2_console *console, qa_command_buffer *commands) {
  qa_app_q2_console_binding *binding = calloc(1, sizeof *binding);
  if (binding == NULL) qa_range_error("Out of memory");
  binding->commands = commands;
  binding->console = console;
  forward_ctx *forwards = binding->forwards;
  static const char *const forward_names[] = {"quit", "map", "say"};
  for (size_t i = 0; i < 3; i++) {
    forwards[i].console = console; forwards[i].name = forward_names[i];
    if (qa_command_buffer_register(commands, forward_names[i], forward_handler, &forwards[i], NULL, QA_COMMAND_WORLD)) {
      binding->names[binding->count] = forward_names[i];
      binding->handlers[binding->count] = forward_handler;
      binding->ctxs[binding->count] = &forwards[i];
      binding->count++;
    }
  }
  if (qa_command_buffer_register(commands, "status", status_handler, console, NULL, QA_COMMAND_WORLD)) {
    binding->names[binding->count] = "status"; binding->handlers[binding->count] = status_handler;
    binding->ctxs[binding->count] = console; binding->count++;
  }
  if (qa_command_buffer_register(commands, "dumpuser", dumpuser_handler, console, NULL, QA_COMMAND_WORLD)) {
    binding->names[binding->count] = "dumpuser"; binding->handlers[binding->count] = dumpuser_handler;
    binding->ctxs[binding->count] = console; binding->count++;
  }
  return binding;
}
void qa_app_q2_console_binding_release(qa_app_q2_console_binding *binding) {
  if (binding == NULL) return;
  for (size_t i = 0; i < binding->count; i++)
    qa_command_buffer_unregister(binding->commands, binding->names[i], binding->handlers[i], binding->ctxs[i], true);
  free(binding);
}
const char *const *qa_app_q2_console_shared_names(qa_app_q2_console *console, size_t *out_count) {
  static const char *base_names[] = {"dmflags", "timelimit", "fraglimit", "capturelimit"};
  static const char *all_names[64];
  if (!console->options.is_lmctf(console->options.ctx)) { *out_count = 4; return base_names; }
  size_t at = 0;
  for (size_t i = 0; i < 4; i++) all_names[at++] = base_names[i];
  for (size_t i = 0; i < qa_lmctf_console_name_count && at < 64; i++) all_names[at++] = qa_lmctf_console_names[i];
  *out_count = at;
  return all_names;
}
void qa_app_q2_console_initialize(qa_app_q2_console *console) { qa_app_q2_console_bind_current(console); }
void qa_app_q2_console_bind_current(qa_app_q2_console *console) {
  if (!console->options.is_lmctf(console->options.ctx)) return;
  qa_cvar_registry *cvars = qa_app_q2_console_cvars(console);
  qa_lmctf_rules *rules = console->options.lmctf_rules(console->options.ctx);
  console->options.bind_lmctf_rules(console->options.ctx, cvars, rules);
  char *maplist_file = qa_cvar_registry_variable_string(cvars, "maplist_file");
  size_t length = 0;
  qa_cleanup node;
  qa_cleanup_push(&node, free, maplist_file);
  uint8_t *raw = maplist_file != NULL
      ? console->options.open_match_file(console->options.ctx, maplist_file, &length) : NULL;
  qa_cleanup_pop(&node);
  free(maplist_file);
  if (raw == NULL) raw = console->options.open_match_file(console->options.ctx, "maplist.txt", &length);
  if (raw == NULL) return;
  /* new TextDecoder().decode(file.bytes): a leading BOM dropped, invalid UTF-8 as U+FFFD. */
  uint8_t *bytes = (uint8_t *)qa_text_decoder_decode(raw, length, &length);
  free(raw);
  if (bytes == NULL) qa_range_error("Out of memory");
  /* Split lines, trim, first token lowercased. */
  size_t capacity = 0, count = 0;
  const char **names = NULL;
  size_t at = 0;
  while (at < length) {
    size_t end = at;
    while (end < length && bytes[end] != '\n') end++;
    size_t line_end = end;
    if (line_end > at && bytes[line_end - 1] == '\r') line_end--;
    /* TS line.trim().split(/\s+/)[0].toLowerCase(): JS white space and Unicode lowering. */
    char *line = malloc(line_end - at + 1);
    if (line == NULL) qa_range_error("Out of memory");
    memcpy(line, bytes + at, line_end - at);
    line[line_end - at] = '\0';
    size_t start = 0, space;
    while ((space = qa_js_white_space_length(line + start)) != 0) start += space;
    size_t stop = start;
    while (line[stop] != '\0' && qa_js_white_space_length(line + stop) == 0) stop++;
    line[stop] = '\0';
    char *name = stop > start ? qa_js_to_lower_case(line + start) : NULL;
    free(line);
    if (stop > start && name == NULL) qa_range_error("Out of memory");
    if (name != NULL) {
      if (count == capacity) {
        capacity = capacity == 0 ? 8 : capacity * 2;
        const char **next = realloc(names, capacity * sizeof *next);
        if (next == NULL) qa_range_error("Out of memory");
        names = next;
      }
      names[count++] = name;
    }
    at = end + 1;
  }
  free(bytes);
  for (size_t i = 0; i < rules->map_count; i++) free((void *)rules->map_list[i]);
  free((void *)rules->map_list);
  rules->map_list = names;
  rules->map_count = count;
}
