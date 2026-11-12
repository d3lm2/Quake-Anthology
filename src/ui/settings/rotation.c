/* Port of src/ui/settings/rotation.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/rotation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/settings/server/profile.h"
#include "qa/settings/server/rotation.h"
#include "qa/ui/common/layout.h"
#include "qa/foundation/js.h"

typedef struct rotation_state rotation_state;
typedef struct rctx { rotation_state *state; int action; } rctx;
struct rotation_state {
  qa_native_ui_controller *controller;
  qa_ui_server_bindings_fn bindings;
  void *bindings_ctx;
  long selected;
  char *draft, *error;
  int handle;
  qa_ui_control controls[9];
  rctx ctxs[9];
  qa_ui_choice *choices;
  char **choice_strings;
  size_t choice_count;
  char selected_id[32];
};

enum { R_NONE, R_ADD, R_UP, R_DOWN, R_REMOVE, R_CLEAR, R_BACK, R_TEXT,
       R_SELECT };

static char *dup_str(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static void set_str(char **slot, const char *value) {
  char *copy = dup_str(value);
  if (copy == NULL) qa_range_error("Out of memory");
  free(*slot);
  *slot = copy;
}

static const qa_bound_server_setting *current_binding(rotation_state *s) {
  size_t count = 0;
  const qa_bound_server_setting *bindings = s->bindings(s->bindings_ctx,
                                                        &count);
  for (size_t i = 0; i < count; i++)
    if (bindings[i].definition != NULL && bindings[i].definition->id != NULL
        && strcmp(bindings[i].definition->id, "server:map-rotation") == 0)
      return &bindings[i];
  return NULL;
}

static bool is_sep(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v'
      || ch == '\f' || ch == ',';
}
/* TS maps(): desired.split(/[\s,]+/).filter(Boolean). Owned. */
static char **read_maps(rotation_state *s, size_t *out_count) {
  *out_count = 0;
  const qa_bound_server_setting *binding = current_binding(s);
  if (binding == NULL) return NULL;
  qa_server_setting_status status = qa_read_server_setting(binding);
  const char *text = status.desired != NULL ? status.desired : "";
  char **maps = NULL;
  size_t count = 0, cap = 0;
  const char *p = text;
  while (*p != '\0') {
    while (is_sep(*p)) p++;
    if (*p == '\0') break;
    const char *start = p;
    while (*p != '\0' && !is_sep(*p)) p++;
    if (count == cap) {
      size_t ncap = cap == 0 ? 4 : cap * 2;
      char **grown = realloc(maps, ncap * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      maps = grown;
      cap = ncap;
    }
    size_t length = (size_t)(p - start);
    maps[count] = malloc(length + 1);
    if (maps[count] == NULL) qa_range_error("Out of memory");
    memcpy(maps[count], start, length);
    maps[count][length] = '\0';
    count++;
  }
  qa_server_setting_status_free(&status);
  *out_count = count;
  return maps;
}
static void free_maps(char **maps, size_t count) {
  for (size_t i = 0; i < count; i++) free(maps[i]);
  free(maps);
}
/* TS action(work): any throw inside work (no rotation binding, an invalid map
 * name from rotationMapName, a list the setting's parser refuses) becomes the
 * notice; success clears it. Temporaries live here so a raise cannot leak them. */
typedef struct rotation_work {
  rotation_state *state;
  int action;
  char *name;
  char **maps;
  size_t count;
  char **values;
  const char **moved;
  char *joined;
} rotation_work;

/* TS write(): throws without a binding. */
static void write_maps(rotation_work *w, char *const *values, size_t count) {
  const qa_bound_server_setting *binding = current_binding(w->state);
  if (binding == NULL)
    qa_raise("Error", 0, 0, "This source has no map rotation setting");
  size_t length = 1;
  for (size_t i = 0; i < count; i++) length += strlen(values[i]) + 1;
  free(w->joined);
  w->joined = malloc(length);
  if (w->joined == NULL) qa_range_error("Out of memory");
  w->joined[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i > 0) strcat(w->joined, " ");
    strcat(w->joined, values[i]);
  }
  qa_server_setting_status status = qa_write_server_setting(binding, w->joined);
  qa_server_setting_status_free(&status);
}

static void add_draft(rotation_work *w) {
  rotation_state *s = w->state;
  w->maps = read_maps(s, &w->count);
  w->name = qa_rotation_map_name(s->draft);
  if (w->name == NULL) qa_range_error("Out of memory");
  w->values = malloc((w->count + 1) * sizeof *w->values);
  if (w->values == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < w->count; i++) w->values[i] = w->maps[i];
  w->values[w->count] = w->name;
  write_maps(w, w->values, w->count + 1);
  size_t now = 0;
  char **after = read_maps(s, &now);
  s->selected = (long)now - 1;
  free_maps(after, now);
  set_str(&s->draft, "");
}

static void run_work(void *v) {
  rotation_work *w = v;
  rotation_state *s = w->state;
  switch (w->action) {
    case R_ADD: add_draft(w); break;
    case R_UP:
    case R_DOWN:
      w->maps = read_maps(s, &w->count);
      w->moved = qa_move_rotation_map((const char *const *)w->maps, w->count,
                                      (double)s->selected,
                                      w->action == R_UP ? -1 : 1);
      write_maps(w, w->moved != NULL ? (char *const *)w->moved
                                     : (char *const *)w->maps, w->count);
      s->selected += w->action == R_UP ? -1 : 1;
      break;
    case R_REMOVE: {
      w->maps = read_maps(s, &w->count);
      w->values = calloc(w->count > 0 ? w->count : 1, sizeof *w->values);
      if (w->values == NULL) qa_range_error("Out of memory");
      size_t kept = 0;
      for (size_t i = 0; i < w->count; i++)
        if ((long)i != s->selected) w->values[kept++] = w->maps[i];
      write_maps(w, w->values, kept);
      break;
    }
    case R_CLEAR: write_maps(w, NULL, 0); break;
    default: break;
  }
}

static void action(rotation_state *s, int kind) {
  rotation_work work;
  memset(&work, 0, sizeof work);
  work.state = s;
  work.action = kind;
  qa_caught_error caught;
  bool ok = qa_try(run_work, &work, &caught);
  free(work.joined);
  free((void *)work.moved);
  free(work.values);
  free(work.name);
  free_maps(work.maps, work.count);
  set_str(&s->error, ok ? "" : caught.message);
}

static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  rctx *c = v;
  rotation_state *s = c->state;
  switch (c->action) {
    case R_ADD:
    case R_UP:
    case R_DOWN:
    case R_REMOVE:
    case R_CLEAR: action(s, c->action); return;
    case R_BACK:
      /* TS button(..., () => controller.closeMenu()) also runs through action(). */
      qa_native_ui_controller_close_menu(s->controller);
      set_str(&s->error, "");
      return;
    default: set_str(&s->error, ""); return;
  }
}
static void on_change_text(void *v, qa_seat_id seat, const char *text) {
  (void)seat;
  rctx *c = v;
  set_str(&c->state->draft, text);
}
static void on_submit_text(void *v, qa_seat_id seat, const char *text) {
  (void)seat;
  (void)text;
  rctx *c = v;
  /* TS submit ignores its value and reads the shared draft. */
  action(c->state, R_ADD);
}
static void on_select_choice(void *v, qa_seat_id seat, const char *choice) {
  (void)seat;
  rctx *c = v;
  rotation_state *s = c->state;
  double index = qa_js_string_to_number(choice); /* Number(value) */
  size_t count = 0;
  char **maps = read_maps(s, &count);
  if (isfinite(index)
      && index == trunc(index) && index >= 0 && index < (double)count)
    s->selected = (long)index;
  free_maps(maps, count);
}

static qa_ui_control button(rotation_state *s, size_t slot, const char *id,
                            const char *label, double row, int action,
                            bool enabled) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "button";
  control.label = label;
  control.rect = qa_ui_menu_row_default(row);
  control.enabled = enabled;
  control.visible = true;
  s->ctxs[slot].state = s;
  s->ctxs[slot].action = action;
  control.ctx = &s->ctxs[slot];
  control.activate_seat = on_activate;
  return control;
}

static void no_op_open(void *v, qa_seat_id seat) { (void)v; (void)seat; }

static qa_ui_menu factory(void *v) {
  rotation_state *s = v;
  size_t count = 0;
  char **values = read_maps(s, &count);
  long max_selected = (long)count - 1;
  /* Math.max(0, Math.min(selected, length - 1)): an empty list selects 0. */
  if (s->selected > max_selected) s->selected = max_selected;
  if (s->selected < 0) s->selected = 0;
  bool have = current_binding(s) != NULL;
  /* Rebuild choices: "0"/"Empty: authored exits" when empty. */
  for (size_t i = 0; i < s->choice_count * 2; i++) free(s->choice_strings[i]);
  free(s->choice_strings);
  free(s->choices);
  size_t nchoices = count == 0 ? 1 : count;
  s->choices = calloc(nchoices, sizeof *s->choices);
  s->choice_strings = calloc(nchoices * 2, sizeof *s->choice_strings);
  if (s->choices == NULL || s->choice_strings == NULL)
    qa_range_error("Out of memory");
  s->choice_count = nchoices;
  if (count == 0) {
    s->choice_strings[0] = dup_str("0");
    s->choice_strings[1] = dup_str("Empty: authored exits");
  } else {
    for (size_t i = 0; i < count; i++) {
      char id[32], label[512];
      snprintf(id, sizeof id, "%zu", i);
      snprintf(label, sizeof label, "%zu. %s", i + 1, values[i]);
      s->choice_strings[i * 2] = dup_str(id);
      s->choice_strings[i * 2 + 1] = dup_str(label);
    }
  }
  for (size_t i = 0; i < nchoices * 2; i++)
    if (s->choice_strings[i] == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < nchoices; i++) {
    s->choices[i].id = s->choice_strings[i * 2];
    s->choices[i].label = s->choice_strings[i * 2 + 1];
  }
  snprintf(s->selected_id, sizeof s->selected_id, "%ld", s->selected);

  size_t at = 0;
  qa_ui_control entry;
  memset(&entry, 0, sizeof entry);
  entry.id = "ui:rotation:map";
  entry.kind = "text-entry";
  entry.label = "Add map";
  entry.text = s->draft;
  entry.maximum_length = 127;
  entry.rect = qa_ui_menu_row_default(0);
  entry.enabled = have;
  entry.visible = true;
  s->ctxs[at].state = s;
  s->ctxs[at].action = R_TEXT;
  entry.ctx = &s->ctxs[at];
  entry.change_text = on_change_text;
  entry.submit_text = on_submit_text;
  s->controls[at++] = entry;
  s->controls[at] = button(s, at, "ui:rotation:add", "Add to end", 1, R_ADD,
                           have);
  at++;
  qa_ui_control choice;
  memset(&choice, 0, sizeof choice);
  choice.id = "ui:rotation:selection";
  choice.kind = "choice";
  choice.label = "Rotation entry";
  choice.selected = s->selected_id;
  choice.choices = s->choices;
  choice.choice_count = nchoices;
  choice.rect = qa_ui_menu_row_default(2);
  choice.enabled = count > 0;
  choice.visible = true;
  s->ctxs[at].state = s;
  s->ctxs[at].action = R_SELECT;
  choice.ctx = &s->ctxs[at];
  choice.select_choice = on_select_choice;
  s->controls[at++] = choice;
  s->controls[at] = button(s, at, "ui:rotation:up", "Move earlier", 3, R_UP,
                           s->selected > 0);
  at++;
  s->controls[at] = button(s, at, "ui:rotation:down", "Move later", 4,
                           R_DOWN, s->selected < (long)count - 1);
  at++;
  s->controls[at] = button(s, at, "ui:rotation:remove", "Remove entry", 5,
                           R_REMOVE, count > 0);
  at++;
  s->controls[at] = button(s, at, "ui:rotation:clear", "Use authored exits",
                           6, R_CLEAR, count > 0);
  at++;
  s->controls[at] = button(
      s, at, "ui:rotation:notice",
      s->error[0] != '\0' ? s->error
                          : "Save in Server profiles to reuse this order.",
      8, R_NONE, false);
  at++;
  s->controls[at] = button(s, at, "ui:rotation:back", "Back", 10, R_BACK,
                           true);
  at++;
  free_maps(values, count);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:server:rotation";
  menu.title = "Map rotation";
  menu.full_screen = false;
  menu.controls = s->controls;
  menu.control_count = at;
  menu.ctx = s;
  menu.open = no_op_open;
  menu.close = no_op_open;
  return menu;
}

static void rotation_dispose(qa_ui_settings_menus *menus) {
  rotation_state *s = menus->state;
  if (s == NULL) return;
  qa_native_ui_controller_unregister(s->controller, s->handle);
  for (size_t i = 0; i < s->choice_count * 2; i++) free(s->choice_strings[i]);
  free(s->choice_strings);
  free(s->choices);
  free(s->draft);
  free(s->error);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_map_rotation_menu(
    qa_native_ui_controller *controller, qa_ui_server_bindings_fn bindings,
    void *bindings_ctx) {
  rotation_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->bindings = bindings;
  s->bindings_ctx = bindings_ctx;
  s->selected = 0;
  s->draft = dup_str("");
  s->error = dup_str("");
  if (s->draft == NULL || s->error == NULL) qa_range_error("Out of memory");
  int handle = qa_native_ui_controller_register(
      controller, "menu:server:rotation", factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { "menu:server:rotation", s, rotation_dispose };
  return menus;
}
