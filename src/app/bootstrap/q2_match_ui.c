/* Port of src/app/bootstrap/q2-match-ui.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_match_ui.h"
#include "qa/network/common/js_number.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/ui/common/layout.h"
typedef struct name_entry { qa_actor_id actor; char *name; } name_entry;
/* One entry of the live menu: TS { label, command: readonly string[] | null }. */
typedef struct menu_entry {
  char *label;
  char **command; size_t command_count; /* owned; NULL = TS null */
} menu_entry;
typedef struct menu_entry_ctx { struct qa_app_q2_match_ui *ui; size_t index; } menu_entry_ctx;
typedef struct setting_entry { char *key; bool is_boolean; bool bool_value; char *text_value; } setting_entry;
typedef struct setting_ctx { struct qa_app_q2_match_ui *ui; size_t index; } setting_ctx;
struct qa_app_q2_match_ui {
  qa_actor_id actor;
  qa_native_ui_controller *controller;
  qa_app_q2_match_command_fn command;
  void *command_ctx;
  qa_app_q2_match_print_fn print;
  void *print_ctx;
  name_entry *names; size_t name_count, name_capacity;
  qa_ui_hud_prompt *prompts; size_t prompt_count;
  char **prompt_strings; size_t prompt_string_count;
  int menu_handle; /* -1 none */
  /* The registered menu's own data (TS closes over title/entries/settings per registration). */
  bool admin;
  char *menu_title;
  menu_entry *entries; size_t entry_count;
  setting_entry *settings; size_t setting_count;
  /* The last factory result: kept until the next factory call or dispose. */
  qa_ui_control *controls; size_t control_count;
  menu_entry_ctx *entry_ctxs;
  setting_ctx *setting_ctxs;
  char **factory_strings; size_t factory_string_count;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
qa_app_q2_match_ui *qa_app_q2_match_ui_create(qa_actor_id actor, qa_native_ui_controller *controller,
    qa_app_q2_match_command_fn command, void *command_ctx,
    qa_app_q2_match_print_fn print, void *print_ctx) {
  qa_app_q2_match_ui *ui = calloc(1, sizeof *ui);
  if (ui == NULL) qa_range_error("Out of memory");
  ui->actor = actor; ui->controller = controller;
  ui->command = command; ui->command_ctx = command_ctx;
  ui->print = print; ui->print_ctx = print_ctx;
  ui->menu_handle = -1;
  return ui;
}
static void dispose_menu(qa_app_q2_match_ui *ui);
void qa_app_q2_match_ui_destroy(qa_app_q2_match_ui *ui) {
  if (ui == NULL) return;
  dispose_menu(ui);
  for (size_t i = 0; i < ui->name_count; i++) free(ui->names[i].name);
  free(ui->names);
  free(ui->prompts);
  for (size_t i = 0; i < ui->prompt_string_count; i++) free(ui->prompt_strings[i]);
  free(ui->prompt_strings);
  free(ui);
}
void qa_app_q2_match_ui_name(qa_app_q2_match_ui *ui, qa_actor_id actor, const char *name) {
  for (size_t i = 0; i < ui->name_count; i++) {
    if (qa_same_actor(ui->names[i].actor, actor)) {
      free(ui->names[i].name);
      ui->names[i].name = dup_text(name);
      return;
    }
  }
  if (ui->name_count == ui->name_capacity) {
    size_t cap = ui->name_capacity == 0 ? 8 : ui->name_capacity * 2;
    name_entry *next = realloc(ui->names, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    ui->names = next; ui->name_capacity = cap;
  }
  ui->names[ui->name_count].actor = actor;
  ui->names[ui->name_count].name = dup_text(name);
  ui->name_count++;
}
static const char *name_of(qa_app_q2_match_ui *ui, qa_actor_id actor) {
  for (size_t i = 0; i < ui->name_count; i++)
    if (qa_same_actor(ui->names[i].actor, actor)) return ui->names[i].name;
  return NULL;
}
const qa_ui_hud_prompt *qa_app_q2_match_ui_prompts(const qa_app_q2_match_ui *ui, size_t *out_count) {
  *out_count = ui->prompt_count;
  return ui->prompts;
}
static void free_factory(qa_app_q2_match_ui *ui) {
  for (size_t i = 0; i < ui->factory_string_count; i++) free(ui->factory_strings[i]);
  free(ui->factory_strings); ui->factory_strings = NULL; ui->factory_string_count = 0;
  free(ui->controls); ui->controls = NULL; ui->control_count = 0;
  free(ui->entry_ctxs); ui->entry_ctxs = NULL;
  free(ui->setting_ctxs); ui->setting_ctxs = NULL;
}
static void free_entries(menu_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(entries[i].label);
    for (size_t c = 0; c < entries[i].command_count; c++) free(entries[i].command[c]);
    free(entries[i].command);
  }
  free(entries);
}
static void free_settings(setting_entry *settings, size_t count) {
  for (size_t i = 0; i < count; i++) { free(settings[i].key); free(settings[i].text_value); }
  free(settings);
}
static void free_menu_data(qa_app_q2_match_ui *ui) {
  free(ui->menu_title); ui->menu_title = NULL;
  free_entries(ui->entries, ui->entry_count); ui->entries = NULL; ui->entry_count = 0;
  free_settings(ui->settings, ui->setting_count); ui->settings = NULL; ui->setting_count = 0;
}
/* TS this.disposeMenu?.(): the registration and everything its factory closed over. */
static void dispose_menu(qa_app_q2_match_ui *ui) {
  if (ui->menu_handle >= 0) {
    qa_native_ui_controller_unregister(ui->controller, ui->menu_handle);
    ui->menu_handle = -1;
  }
  free_factory(ui);
  free_menu_data(ui);
}
/* TS `[name, ...args] = entry.command ?? []`; the strings are copied first, since the command may
 * replace this menu (and free its entries) while it runs. */
static void free_argv(void *v) {
  char **argv = v;
  for (size_t i = 0; argv[i] != NULL; i++) free(argv[i]);
  free(argv);
}
static void run_command(qa_app_q2_match_ui *ui, const char *name, const char *const *args, size_t arg_count) {
  char **argv = calloc(arg_count + 2, sizeof *argv);
  if (argv == NULL) qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, free_argv, argv);
  argv[0] = dup_text(name);
  for (size_t i = 0; i < arg_count; i++) argv[i + 1] = dup_text(args[i]);
  ui->command(ui->command_ctx, argv[0], arg_count > 0 ? (const char *const *)(argv + 1) : NULL, arg_count);
  qa_cleanup_pop(&node);
  free_argv(argv);
}
static void entry_activate(void *v, qa_seat_id seat) {
  (void)seat;
  menu_entry_ctx *ctx = v;
  qa_app_q2_match_ui *ui = ctx->ui;
  size_t index = ctx->index;
  qa_native_ui_controller_close_all(ui->controller);
  if (index >= ui->entry_count || ui->entries[index].command == NULL || ui->entries[index].command_count == 0) return;
  const menu_entry *entry = &ui->entries[index];
  run_command(ui, entry->command[0], (const char *const *)(entry->command + 1), entry->command_count - 1);
}
static void setting_change_checked(void *v, qa_seat_id seat, bool checked) {
  (void)seat;
  setting_ctx *ctx = v;
  const char *args[] = {ctx->ui->settings[ctx->index].key, checked ? "true" : "false"};
  run_command(ctx->ui, "ctf-settings", args, 2);
}
static void setting_change_text(void *v, qa_seat_id seat, const char *text) {
  (void)v; (void)seat; (void)text;
}
static void setting_submit_text(void *v, qa_seat_id seat, const char *text) {
  (void)seat;
  setting_ctx *ctx = v;
  const char *args[] = {ctx->ui->settings[ctx->index].key, text};
  run_command(ctx->ui, "ctf-settings", args, 2);
}
static void close_activate(void *v, qa_seat_id seat) {
  (void)seat;
  qa_native_ui_controller_close_all(((qa_app_q2_match_ui *)v)->controller);
}
static void noop_open(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; }
static char *factory_string(qa_app_q2_match_ui *ui, char *text) {
  char **next = realloc(ui->factory_strings, (ui->factory_string_count + 1) * sizeof *next);
  if (next == NULL) { free(text); qa_range_error("Out of memory"); }
  ui->factory_strings = next;
  ui->factory_strings[ui->factory_string_count++] = text;
  return text;
}
static char *joined(const char *a, const char *b) {
  size_t size = strlen(a) + strlen(b) + 1;
  char *out = malloc(size);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, size, "%s%s", a, b);
  return out;
}
/* key.replace(/([A-Z])/gu, " $1"), then label.charAt(0).toUpperCase() + label.slice(1). */
static char *setting_label(const char *key) {
  size_t length = strlen(key), capitals = 0;
  for (size_t i = 0; i < length; i++) if (key[i] >= 'A' && key[i] <= 'Z') capitals++;
  char *label = malloc(length + capitals + 1);
  if (label == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < length; i++) {
    if (key[i] >= 'A' && key[i] <= 'Z') label[at++] = ' ';
    label[at++] = key[i];
  }
  label[at] = '\0';
  if (at == 0) return label;
  /* charAt(0): one UTF-16 unit; a supplementary character's lone high surrogate is unchanged by toUpperCase. */
  unsigned char lead = (unsigned char)label[0];
  if (lead >= 0xf0) return label;
  size_t width = lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
  char first[5] = {0};
  memcpy(first, label, width);
  char *upper = qa_js_to_upper_case(first);
  if (upper == NULL) { free(label); qa_range_error("Out of memory"); }
  char *out = joined(upper, label + width);
  free(upper);
  free(label);
  return out;
}
static qa_ui_menu menu_factory(void *v) {
  qa_app_q2_match_ui *ui = v;
  free_factory(ui);
  size_t count = ui->admin ? ui->setting_count + 1 : ui->entry_count;
  ui->controls = calloc(count == 0 ? 1 : count, sizeof *ui->controls);
  if (ui->controls == NULL) qa_range_error("Out of memory");
  ui->control_count = count;
  qa_ui_menu_row_options none = {false, false, false, false, 0, 0, 0, 0};
  if (ui->admin) {
    ui->setting_ctxs = calloc(ui->setting_count == 0 ? 1 : ui->setting_count, sizeof *ui->setting_ctxs);
    if (ui->setting_ctxs == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < ui->setting_count; i++) {
      const setting_entry *setting = &ui->settings[i];
      setting_ctx *ctx = &ui->setting_ctxs[i];
      ctx->ui = ui; ctx->index = i;
      qa_ui_control *control = &ui->controls[i];
      control->id = factory_string(ui, joined("ui:match:", setting->key));
      control->label = factory_string(ui, setting_label(setting->key));
      control->rect = qa_ui_menu_row((double)(i + 1), none);
      control->enabled = true; control->visible = true;
      control->ctx = ctx;
      if (setting->is_boolean) {
        control->kind = "toggle"; control->checked = setting->bool_value;
        control->change_checked = setting_change_checked;
      } else {
        control->kind = "text-entry"; control->text = setting->text_value;
        control->maximum_length = 8;
        control->change_text = setting_change_text;
        control->submit_text = setting_submit_text;
      }
    }
    qa_ui_control *close_control = &ui->controls[ui->setting_count];
    close_control->id = "ui:match:close"; close_control->label = "Close";
    close_control->rect = qa_ui_menu_row(10, none);
    close_control->enabled = true; close_control->visible = true;
    close_control->kind = "button"; close_control->ctx = ui;
    close_control->activate_seat = close_activate;
  } else {
    ui->entry_ctxs = calloc(count == 0 ? 1 : count, sizeof *ui->entry_ctxs);
    if (ui->entry_ctxs == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) {
      menu_entry_ctx *ctx = &ui->entry_ctxs[i];
      ctx->ui = ui; ctx->index = i;
      qa_ui_control *control = &ui->controls[i];
      char id[64];
      snprintf(id, sizeof id, "ui:match:%zu", i);
      control->id = factory_string(ui, dup_text(id));
      control->kind = "button";
      control->label = ui->entries[i].label;
      control->rect = qa_ui_menu_row((double)(i + 1), none);
      control->enabled = ui->entries[i].command != NULL;
      control->visible = true;
      control->ctx = ctx;
      control->activate_seat = entry_activate;
    }
  }
  qa_ui_menu menu = {0};
  menu.id = "menu:application:match";
  menu.title = ui->menu_title;
  menu.full_screen = true;
  menu.controls = ui->controls;
  menu.control_count = count;
  menu.ctx = ui;
  menu.open = noop_open;
  menu.close = noop_open;
  return menu;
}
/* TS menu(title, entries) and the admin-settings branch: closeAll, dispose the previous menu, register,
 * open. The new title/entries/settings were built by the caller and are taken here. */
static void show_menu(qa_app_q2_match_ui *ui, char *title, menu_entry *entries, size_t entry_count,
    setting_entry *settings, size_t setting_count, bool admin) {
  qa_cleanup title_node;
  qa_cleanup_push(&title_node, free, title);
  qa_native_ui_controller_close_all(ui->controller);
  qa_cleanup_pop(&title_node);
  dispose_menu(ui);
  ui->menu_title = title;
  ui->entries = entries; ui->entry_count = entry_count;
  ui->settings = settings; ui->setting_count = setting_count;
  ui->admin = admin;
  ui->menu_handle = qa_native_ui_controller_register(ui->controller, "menu:application:match", menu_factory, ui);
  if (ui->menu_handle < 0) qa_range_error("Out of memory");
  qa_native_ui_controller_open_menu(ui->controller, "menu:application:match");
}
/* entry.command.split(/\s+/u): JS String.prototype.split, so empty fields at either end are kept. */
static char **split_command(const char *command, size_t *out_count) {
  size_t capacity = 4, count = 0;
  char **parts = calloc(capacity, sizeof *parts);
  if (parts == NULL) qa_range_error("Out of memory");
  const char *start = command, *p = command;
  for (;;) {
    size_t space = *p != '\0' ? qa_js_white_space_length(p) : 0;
    if (*p != '\0' && space == 0) { p++; continue; }
    size_t len = (size_t)(p - start);
    char *part = malloc(len + 1);
    if (part == NULL) qa_range_error("Out of memory");
    memcpy(part, start, len); part[len] = '\0';
    if (count == capacity) {
      capacity *= 2;
      char **next = realloc(parts, capacity * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      parts = next;
    }
    parts[count++] = part;
    if (*p == '\0') break;
    while (*p != '\0' && (space = qa_js_white_space_length(p)) != 0) p += space;
    start = p;
  }
  *out_count = count;
  return parts;
}
static char **command_of(const char *const *words, size_t count) {
  char **command = calloc(count, sizeof *command);
  if (command == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) command[i] = dup_text(words[i]);
  return command;
}
static void free_pending_entries(void *v) {
  struct { menu_entry *entries; size_t count; } *pending = v;
  free_entries(pending->entries, pending->count);
}
static char *score_text(const char *format, const char *a, double b, double c) {
  char first[64], second[64];
  snprintf(first, sizeof first, "%s", QA_JS_NUMBER(b));
  snprintf(second, sizeof second, "%s", QA_JS_NUMBER(c));
  size_t size = strlen(format) + strlen(a) + strlen(first) + strlen(second) + 1;
  char *out = malloc(size);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, size, format, a, first, second);
  return out;
}
typedef struct pending_entries { menu_entry *entries; size_t count; } pending_entries;
static void push_entry(pending_entries *pending, char *label, char **command, size_t command_count) {
  pending->entries[pending->count].label = label;
  pending->entries[pending->count].command = command;
  pending->entries[pending->count].command_count = command_count;
  pending->count++;
}
void qa_app_q2_match_ui_receive(qa_app_q2_match_ui *ui, qa_app_q2_match_event event) {
  if (event.has_actor && !qa_same_actor(event.actor, ui->actor)) return;
  if (is(event.kind, "grapple-cable")) return;
  if (is(event.kind, "match-status")) { ui->print(ui->print_ctx, event.text); return; }
  qa_cleanup node;
  if (is(event.kind, "score-log")) {
    char amount[64];
    snprintf(amount, sizeof amount, "%s", QA_JS_NUMBER(event.amount));
    size_t size = strlen(event.name) + strlen(amount) + 5;
    char *line = malloc(size);
    if (line == NULL) qa_range_error("Out of memory");
    snprintf(line, size, "%s: %s%s\n", event.name, event.amount > 0 ? "+" : "", amount);
    qa_cleanup_push(&node, free, line);
    ui->print(ui->print_ctx, line);
    qa_cleanup_pop(&node);
    free(line);
    return;
  }
  if (is(event.kind, "hud")) {
    char *built[8] = {0};
    const char *values[8];
    size_t count = 0;
    values[count++] = event.team == 1 ? "Red" : event.team == 2 ? "Blue" : "Spectator";
    if (event.has_captures) {
      char number[64];
      snprintf(number, sizeof number, "%s", QA_JS_NUMBER(event.captures[0]));
      built[0] = malloc(strlen(number) + strlen(event.flag_states[0]) + 8);
      if (built[0] == NULL) qa_range_error("Out of memory");
      sprintf(built[0], "Red %s (%s)", number, event.flag_states[0]);
      values[count++] = built[0];
      snprintf(number, sizeof number, "%s", QA_JS_NUMBER(event.captures[1]));
      built[1] = malloc(strlen(number) + strlen(event.flag_states[1]) + 9);
      if (built[1] == NULL) qa_range_error("Out of memory");
      sprintf(built[1], "Blue %s (%s)", number, event.flag_states[1]);
      values[count++] = built[1];
      if (event.tech != NULL) {
        values[count++] = is(event.tech, "item_tech1") ? "Disruptor Shield"
            : is(event.tech, "item_tech2") ? "Power Amplifier"
            : is(event.tech, "item_tech3") ? "Time Accel" : "AutoDoc";
      }
      if (event.match != NULL && event.match[0] != '\0') values[count++] = event.match;
      if (event.has_id_target) {
        const char *name = name_of(ui, event.id_target);
        if (name != NULL) values[count++] = name;
      }
      if (event.has_blink_team) values[count++] = event.blink_team == 1 ? "Red captured the flag" : "Blue captured the flag";
    } else if (event.rune != NULL) {
      values[count++] = built[0] = joined(event.rune, " artifact");
    }
    if (event.carried_flag) values[count++] = "Carrying flag";
    qa_ui_hud_prompt *prompts = calloc(count, sizeof *prompts);
    char **strings = calloc(count, sizeof *strings);
    if (prompts == NULL || strings == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) {
      strings[i] = dup_text(values[i]);
      prompts[i].action = strings[i];
      prompts[i].binding = "";
      prompts[i].icon = NULL;
    }
    free(built[0]); free(built[1]);
    for (size_t i = 0; i < ui->prompt_string_count; i++) free(ui->prompt_strings[i]);
    free(ui->prompt_strings);
    free(ui->prompts);
    ui->prompts = prompts; ui->prompt_strings = strings;
    ui->prompt_count = count; ui->prompt_string_count = count;
    return;
  }
  if (is(event.kind, "scoreboard")) {
    size_t total = event.has_rows ? event.row_count
        : event.red_count + event.blue_count + event.spectator_count;
    pending_entries pending = {calloc(total + 1, sizeof(menu_entry)), 0};
    if (pending.entries == NULL) qa_range_error("Out of memory");
    qa_cleanup_push(&node, free_pending_entries, &pending);
    /* "rows" in event ? event.rows : [...red, ...blue, ...spectators] */
    const qa_app_q2_match_score_row *groups[4] = {event.rows, event.red, event.blue, event.spectators};
    size_t group_counts[4] = {event.has_rows ? event.row_count : 0, event.has_rows ? 0 : event.red_count,
        event.has_rows ? 0 : event.blue_count, event.has_rows ? 0 : event.spectator_count};
    for (size_t g = 0; g < 4; g++)
      for (size_t i = 0; i < group_counts[g]; i++)
        push_entry(&pending, score_text("%s   %s   %s ms", groups[g][i].name, groups[g][i].score, groups[g][i].ping), NULL, 0);
    static const char *const close_command[] = {"score"};
    push_entry(&pending, dup_text("Close"), command_of(close_command, 1), 1);
    char *title;
    if (event.has_captures) title = score_text("Red %s%s - Blue %s", "", event.captures[0], event.captures[1]);
    else title = dup_text("Scores");
    qa_cleanup_pop(&node);
    show_menu(ui, title, pending.entries, pending.count, NULL, 0, false);
    return;
  }
  if (is(event.kind, "menu")) {
    pending_entries pending = {calloc(event.entry_count == 0 ? 1 : event.entry_count, sizeof(menu_entry)), 0};
    if (pending.entries == NULL) qa_range_error("Out of memory");
    qa_cleanup_push(&node, free_pending_entries, &pending);
    for (size_t i = 0; i < event.entry_count; i++) {
      const qa_app_q2_match_menu_entry *entry = &event.entries[i];
      char *label = dup_text(entry->label);
      char **command = NULL;
      size_t command_count = 0;
      /* "action" in entry ? (action === null ? null : ["ctf-menu", action]) : command === null ? null : command.split(/\s+/u) */
      if (entry->has_action) {
        if (entry->action != NULL) {
          const char *words[] = {"ctf-menu", entry->action};
          command = command_of(words, 2);
          command_count = 2;
        }
      } else if (entry->command != NULL) command = split_command(entry->command, &command_count);
      push_entry(&pending, label, command, command_count);
    }
    char *title = dup_text(event.title);
    qa_cleanup_pop(&node);
    show_menu(ui, title, pending.entries, pending.count, NULL, 0, false);
    return;
  }
  if (is(event.kind, "admin-settings")) {
    /* Object.entries({ ...event.settings }): copied, since the factory reads them after this event. */
    setting_entry *settings = calloc(event.setting_count == 0 ? 1 : event.setting_count, sizeof *settings);
    if (settings == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < event.setting_count; i++) {
      settings[i].key = dup_text(event.settings[i].key);
      settings[i].is_boolean = event.settings[i].is_boolean;
      settings[i].bool_value = event.settings[i].bool_value;
      settings[i].text_value = event.settings[i].text_value != NULL ? dup_text(event.settings[i].text_value) : NULL;
    }
    show_menu(ui, dup_text("Match settings"), NULL, 0, settings, event.setting_count, true);
    return;
  }
  qa_raise("Error", 0, 0, "Unknown match UI event: %s", event.kind);
}
void qa_app_q2_match_ui_close(qa_app_q2_match_ui *ui) { dispose_menu(ui); }
