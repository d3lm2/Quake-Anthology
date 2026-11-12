/* Port of src/ui/settings/bindings.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/bindings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/bindings.h"
#include "qa/input/support.h"
#include "qa/ui/common/layout.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

static void oom(void) { qa_range_error("Out of memory"); }
static char *xstrdup(const char *text) {
  char *copy = qa_input_dup(text);
  if (copy == NULL) oom();
  return copy;
}

char *qa_ui_physical_input_label(const qa_physical_input *input) {
  return qa_physical_input_name(input);
}
static bool same_target(qa_input_binding_target a,
                        qa_input_binding_target b) {
  if (a.is_action && b.is_action) return strcmp(a.action, b.action) == 0;
  if (!a.is_action && !b.is_action) return strcmp(a.text, b.text) == 0;
  return false;
}
bool qa_ui_binding_matches_action(qa_input_binding_target target,
                                  const qa_ui_binding_action *action) {
  return same_target(target, action->target)
         || (action->matches != NULL
             && action->matches(action->ctx, target) == true);
}

/* Internal row key for a physical input (TS JSON.stringify). */
static void input_key(const qa_physical_input *input, char *out, size_t size) {
  char *name = qa_physical_input_name(input);
  if (name == NULL) oom();
  snprintf(out, size, "%s", name);
  free(name);
}
static void target_key(qa_input_binding_target target, char *out, size_t size) {
  if (target.is_action) snprintf(out, size, "action:%s", target.action);
  else snprintf(out, size, "command:%s", target.text);
}

typedef struct bind_state bind_state;
typedef struct bctx { bind_state *state; int action; size_t row; } bctx;
typedef struct brow {
  char *id;
  const qa_ui_binding_action *action;
  bool has_physical;
  qa_physical_input physical;
  qa_input_binding_target target;
  bctx x_ctx; /* the row's "X" action; rows are not moved until the next build */
} brow;
/* An owned copy of a row and its action, kept while a capture or a conflict is
 * outstanding: a factory rebuild replaces the row and action storage, and a
 * bind replaces the previous target's text. */
typedef struct row_snapshot {
  brow row;
  qa_ui_binding_action action;
  char *id, *label, *action_text, *target_text;
} row_snapshot;
struct bind_state {
  qa_native_ui_controller *controller;
  qa_seat_input *input;
  const qa_ui_binding_action *source;
  size_t source_count;
  const qa_ui_binding_action *(*get_source)(void *, size_t *);
  void *source_ctx;
  bool has_reset;
  qa_ui_binding_reset reset;
  bool has_scope;
  qa_ui_binding_scope scope;
  char *query;
  char *selected;
  bool has_pending;
  qa_physical_input pending_physical;
  row_snapshot pending;
  qa_input_binding_target pending_previous;
  char *pending_previous_text;
  row_snapshot capture;
  /* Rebuilt available actions (source + customs). */
  qa_ui_binding_action *available;
  size_t available_count, available_capacity;
  qa_ui_binding_action *customs;
  size_t custom_count, custom_capacity;
  char **strings;
  size_t string_count, string_capacity;
  brow *rows;
  size_t row_count, row_capacity;
  qa_ui_control controls[12];
  qa_ui_list_row *list_rows;
  const char **cells;
  qa_ui_list_row_action *row_actions;
  bctx ctxs[16];
  size_t ctx_used;
  int handles[3];
  size_t handle_count;
  double column_widths[2];
  char title[512];
};

enum { B_NONE, B_SEARCH, B_LIST_SELECT, B_LIST_ACTIVATE, B_ADD, B_REMOVE,
       B_CLEAR, B_RESET_OPEN, B_KEEP, B_RESTORE, B_CANCEL, B_REPLACE,
       B_BACK, B_ROW_X };

static char *pool(bind_state *s, const char *text) {
  if (s->string_count == s->string_capacity) {
    size_t cap = s->string_capacity == 0 ? 32 : s->string_capacity * 2;
    char **next = realloc(s->strings, cap * sizeof *next);
    if (next == NULL) oom();
    s->strings = next;
    s->string_capacity = cap;
  }
  char *copy = xstrdup(text);
  s->strings[s->string_count++] = copy;
  return copy;
}
static void pool_clear(bind_state *s) {
  for (size_t i = 0; i < s->string_count; i++) free(s->strings[i]);
  s->string_count = 0;
  for (size_t i = 0; i < s->row_count; i++) free(s->rows[i].id);
  s->row_count = 0;
  s->ctx_used = 0;
}
static bctx *next_ctx(bind_state *s, int action, size_t row) {
  if (s->ctx_used >= sizeof s->ctxs / sizeof s->ctxs[0]) oom(); /* one per control */
  bctx *c = &s->ctxs[s->ctx_used++];
  c->state = s;
  c->action = action;
  c->row = row;
  return c;
}
static const qa_ui_binding_action *available_actions(bind_state *s,
                                                     size_t *out_count) {
  size_t source_count = 0;
  const qa_ui_binding_action *source =
      s->get_source != NULL ? s->get_source(s->source_ctx, &source_count)
                            : s->source;
  if (s->get_source == NULL) source_count = s->source_count;
  size_t early_bindings = 0;
  (void)qa_seat_input_bindings(s->input, &early_bindings);
  size_t wanted = source_count + early_bindings + 1;
  if (s->available_capacity < wanted) {
    qa_ui_binding_action *next =
        realloc(s->available, wanted * sizeof *next);
    if (next == NULL) oom();
    s->available = next;
    s->available_capacity = wanted;
  }
  memcpy(s->available, source, source_count * sizeof *source);
  size_t count = source_count;
  size_t binding_count = 0;
  const qa_input_binding *bindings =
      qa_seat_input_bindings(s->input, &binding_count);
  /* Customs live in their own stable storage (ids/labels pooled per
   * rebuild would dangle in captured rows; keep them here). */
  if (s->custom_capacity < binding_count + 1) {
    qa_ui_binding_action *next =
        realloc(s->customs, (binding_count + 1) * sizeof *next);
    if (next == NULL) oom();
    s->customs = next;
    s->custom_capacity = binding_count + 1;
  }
  s->custom_count = 0;
  for (size_t i = 0; i < binding_count; i++) {
    qa_input_binding_target target = bindings[i].target;
    bool known = false;
    for (size_t j = 0; j < source_count; j++)
      if (qa_ui_binding_matches_action(target, &source[j])) known = true;
    if (known) continue;
    char key[512], label[1024];
    target_key(target, key, sizeof key);
    bool duplicate = false;
    for (size_t j = 0; j < s->custom_count; j++)
      if (strcmp(s->customs[j].id + 7, key) == 0) duplicate = true;
    if (duplicate) continue;
    qa_ui_binding_action *custom = &s->customs[s->custom_count++];
    memset(custom, 0, sizeof *custom);
    char id[544];
    snprintf(id, sizeof id, "custom:%s", key);
    custom->id = pool(s, id);
    if (target.is_action)
      snprintf(label, sizeof label, "Action: %s", target.action);
    else
      snprintf(label, sizeof label, "Command: %s", target.text);
    custom->label = pool(s, label);
    custom->target = target;
    if (!target.is_action) custom->target.text = pool(s, target.text);
    else custom->target.action = pool(s, target.action);
    if (count < s->available_capacity) s->available[count++] = *custom;
  }
  s->available_count = count;
  *out_count = count;
  return s->available;
}
static const char *target_label(bind_state *s, qa_input_binding_target target) {
  size_t count = 0;
  const qa_ui_binding_action *actions = available_actions(s, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_ui_binding_matches_action(target, &actions[i]))
      return actions[i].label;
  return target.is_action ? target.action : target.text;
}
static void set_selected(bind_state *s, const char *value) {
  free(s->selected);
  s->selected = value == NULL ? NULL : xstrdup(value);
}
/* scope?.local() === true */
static bool scope_local(const bind_state *s) {
  return s->has_scope && s->scope.local(s->scope.ctx);
}
static void note_local(const bind_state *s, const qa_physical_input *physical) {
  if (scope_local(s) && s->scope.note_local != NULL) s->scope.note_local(s->scope.ctx, physical);
}
/* TS unbindKey: a local edit tells the scope of each key it touches. */
static void unbind_key(bind_state *s, const qa_physical_input *physical) {
  note_local(s, physical);
  qa_seat_input_unbind(s->input, physical);
}
static void assign_row(bind_state *s, const brow *row,
                       qa_physical_input physical) {
  if (row->has_physical) unbind_key(s, &row->physical);
  note_local(s, &physical);
  qa_input_binding binding = { physical, row->target };
  if (!qa_seat_input_bind(s->input, &binding)) oom();
  char key[512], selected[1024];
  input_key(&physical, key, sizeof key);
  snprintf(selected, sizeof selected, "%s:%s", row->action->id, key);
  set_selected(s, selected);
}
static void snapshot_free(row_snapshot *snapshot) {
  free(snapshot->id);
  free(snapshot->label);
  free(snapshot->action_text);
  free(snapshot->target_text);
  memset(snapshot, 0, sizeof *snapshot);
}
/* Copies first, then replaces: `row` may live in the snapshot being replaced. */
static void snapshot_row(row_snapshot *out, const brow *row) {
  const char *at = row->action->target.is_action ? row->action->target.action
                                                 : row->action->target.text;
  const char *tt = row->target.is_action ? row->target.action : row->target.text;
  row_snapshot next;
  memset(&next, 0, sizeof next);
  next.id = xstrdup(row->action->id);
  next.label = xstrdup(row->action->label);
  next.action_text = xstrdup(at != NULL ? at : "");
  next.target_text = xstrdup(tt != NULL ? tt : "");
  next.action = *row->action;
  next.row = *row;
  snapshot_free(out);
  *out = next;
  out->action.id = out->id;
  out->action.label = out->label;
  if (out->action.target.is_action) out->action.target.action = out->action_text;
  else out->action.target.text = out->action_text;
  out->row.id = NULL;
  out->row.action = &out->action;
  if (out->row.target.is_action) out->row.target.action = out->target_text;
  else out->row.target.text = out->target_text;
}
static void capture_accept(void *v, qa_physical_input physical) {
  bind_state *s = v;
  const qa_input_binding_target *previous =
      qa_seat_input_binding(s->input, &physical);
  if (previous == NULL
      || qa_ui_binding_matches_action(*previous, s->capture.row.action)) {
    assign_row(s, &s->capture.row, physical);
  } else {
    s->has_pending = true;
    s->pending_physical = physical;
    snapshot_row(&s->pending, &s->capture.row);
    /* The conflict menu reads the previous target after a replace has bound
       the key anew, so keep its text. */
    char *text = xstrdup(previous->is_action ? previous->action : previous->text);
    free(s->pending_previous_text);
    s->pending_previous_text = text;
    s->pending_previous = *previous;
    if (s->pending_previous.is_action) s->pending_previous.action = text;
    else s->pending_previous.text = text;
    qa_native_ui_controller_open_menu(s->controller,
                                      "menu:bindings:conflict");
  }
}
static void capture_cancel(void *v) { (void)v; }
static void start_capture(bind_state *s, const brow *row) {
  snapshot_row(&s->capture, row);
  qa_native_ui_controller_capture_binding(s->controller, capture_accept,
                                          capture_cancel, s);
}
/* query.toLocaleLowerCase().trim().split(/\s+/u), every term a substring of the
 * searchable text; a blank query is [""] and matches everything. JS white space
 * over UTF-8 (a lone 0xA0 byte is a continuation byte, not NBSP). */
static bool terms_match(const char *searchable_lower, const char *query) {
  char *lowered = qa_js_to_lower_case(query);
  if (lowered == NULL) oom();
  bool ok = true;
  const char *p = lowered;
  while (*p != '\0') {
    size_t space;
    while ((space = qa_js_white_space_length(p)) > 0) p += space;
    if (*p == '\0') break;
    const char *start = p;
    while (*p != '\0' && qa_js_white_space_length(p) == 0) p++;
    size_t length = (size_t)(p - start);
    bool found = false;
    for (const char *h = searchable_lower; *h != '\0'; h++)
      if (strncmp(h, start, length) == 0) { found = true; break; }
    if (!found) ok = false;
  }
  free(lowered);
  return ok;
}
static void build_rows(bind_state *s) {
  size_t action_count = 0;
  const qa_ui_binding_action *actions = available_actions(s, &action_count);
  size_t binding_count = 0;
  const qa_input_binding *bindings =
      qa_seat_input_bindings(s->input, &binding_count);
  for (size_t a = 0; a < action_count; a++) {
    const qa_ui_binding_action *action = &actions[a];
    /* Searchable: label + physical labels of matching bindings. */
    /* `${label} ${names.join(" ")}` */
    size_t length = strlen(action->label) + 2;
    char *searchable = malloc(length);
    if (searchable == NULL) oom();
    snprintf(searchable, length, "%s ", action->label);
    size_t matches = 0;
    for (size_t i = 0; i < binding_count; i++) {
      if (!qa_ui_binding_matches_action(bindings[i].target, action))
        continue;
      char *name = qa_physical_input_name(&bindings[i].input);
      if (name == NULL) oom();
      length += strlen(name) + 1;
      char *grown = realloc(searchable, length);
      if (grown == NULL) oom();
      searchable = grown;
      if (matches > 0) strcat(searchable, " ");
      strcat(searchable, name);
      free(name);
      matches++;
    }
    char *lowered = qa_js_to_lower_case(searchable);
    free(searchable);
    if (lowered == NULL) oom();
    bool keep = terms_match(lowered, s->query != NULL ? s->query : "");
    free(lowered);
    if (!keep) continue;
    if (s->row_capacity < s->row_count + matches + 1) {
      size_t cap = s->row_capacity == 0 ? 32 : s->row_capacity * 2;
      while (cap < s->row_count + matches + 1) cap *= 2;
      brow *next = realloc(s->rows, cap * sizeof *next);
      if (next == NULL) oom();
      s->rows = next;
      s->row_capacity = cap;
    }
    if (matches == 0) {
      brow *row = &s->rows[s->row_count++];
      memset(row, 0, sizeof *row);
      row->id = xstrdup(action->id);
      row->action = action;
      row->target = action->target;
    } else {
      for (size_t i = 0; i < binding_count; i++) {
        if (!qa_ui_binding_matches_action(bindings[i].target, action))
          continue;
        char key[512], id[1024];
        input_key(&bindings[i].input, key, sizeof key);
        snprintf(id, sizeof id, "%s:%s", action->id, key);
        brow *row = &s->rows[s->row_count++];
        memset(row, 0, sizeof *row);
        row->id = xstrdup(id);
        row->action = action;
        row->has_physical = true;
        row->physical = bindings[i].input;
        row->target = bindings[i].target;
      }
    }
  }
}
static const brow *current_row(bind_state *s) {
  for (size_t i = 0; i < s->row_count; i++)
    if (s->selected != NULL && strcmp(s->rows[i].id, s->selected) == 0)
      return &s->rows[i];
  return s->row_count > 0 ? &s->rows[0] : NULL;
}

static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  bctx *c = v;
  bind_state *s = c->state;
  switch (c->action) {
    case B_BACK:
    case B_CANCEL:
    case B_KEEP:
      qa_native_ui_controller_close_menu(s->controller);
      break;
    case B_REPLACE:
      assign_row(s, &s->pending.row, s->pending_physical);
      qa_native_ui_controller_close_menu(s->controller);
      break;
    case B_RESTORE:
      s->reset.reset(s->reset.ctx);
      set_selected(s, NULL);
      qa_native_ui_controller_close_menu(s->controller);
      break;
    case B_RESET_OPEN:
      qa_native_ui_controller_open_menu(s->controller,
                                        "menu:bindings:reset");
      break;
    case B_ADD: {
      const brow *current = current_row(s);
      if (current != NULL) {
        brow row = *current;
        row.has_physical = false;
        row.target = current->action->target;
        start_capture(s, &row);
      }
      break;
    }
    case B_REMOVE: {
      const brow *current = current_row(s);
      if (current != NULL && current->has_physical)
        unbind_key(s, &current->physical);
      break;
    }
    case B_CLEAR: {
      const brow *current = current_row(s);
      if (current != NULL) {
        /* Copy inputs first: unbind mutates the borrowed array. */
        size_t count = 0;
        const qa_input_binding *bindings =
            qa_seat_input_bindings(s->input, &count);
        qa_physical_input *copy = malloc((count + 1) * sizeof *copy);
        if (copy == NULL) oom();
        size_t kept = 0;
        for (size_t i = 0; i < count; i++)
          if (qa_ui_binding_matches_action(bindings[i].target,
                                           current->action))
            copy[kept++] = bindings[i].input;
        for (size_t i = 0; i < kept; i++)
          unbind_key(s, &copy[i]);
        free(copy);
      }
      break;
    }
    default: break;
  }
}
static void on_row_x(void *v, qa_seat_id seat) {
  (void)seat;
  bctx *c = v;
  bind_state *s = c->state;
  if (c->row < s->row_count && s->rows[c->row].has_physical)
    unbind_key(s, &s->rows[c->row].physical);
}
static void on_change_local(void *v, qa_seat_id seat, bool checked) {
  (void)seat;
  bctx *c = v;
  bind_state *s = c->state;
  s->scope.set_local(s->scope.ctx, checked);
}
static void on_change_search(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  bctx *c = v;
  char *query = xstrdup(value);
  free(c->state->query);
  c->state->query = query;
  set_selected(c->state, NULL);
}
static void on_submit_noop(void *v, qa_seat_id seat, const char *value) {
  (void)v; (void)seat; (void)value;
}
static void on_select_row(void *v, qa_seat_id seat, const char *row) {
  (void)seat;
  bctx *c = v;
  set_selected(c->state, row);
}
static void on_activate_row(void *v, qa_seat_id seat, const char *row_id) {
  (void)seat;
  bctx *c = v;
  bind_state *s = c->state;
  for (size_t i = 0; i < s->row_count; i++)
    if (strcmp(s->rows[i].id, row_id) == 0) {
      start_capture(s, &s->rows[i]);
      return;
    }
}
static qa_ui_control button(bind_state *s, const char *id, const char *label,
                            qa_rect rect, int action, bool enabled) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "button";
  control.label = label;
  control.rect = rect;
  control.enabled = enabled;
  control.visible = true;
  control.ctx = next_ctx(s, action, 0);
  control.activate_seat = on_activate;
  return control;
}

static void conflict_close(void *v, qa_seat_id seat) {
  (void)seat;
  bind_state *s = v;
  s->has_pending = false;
}
static qa_ui_menu conflict_factory(void *v) {
  bind_state *s = v;
  if (!s->has_pending) qa_raise("Error", 0, 0, "No pending binding conflict");
  pool_clear(s);
  const char *label = target_label(s, s->pending_previous);
  char *previous = malloc(strlen(label) + 12);
  if (previous == NULL) oom();
  sprintf(previous, "Currently: %s", label);
  char *next_label = malloc(strlen(s->pending.row.action->label) + 15);
  if (next_label == NULL) oom();
  sprintf(next_label, "Replace with: %s", s->pending.row.action->label);
  char *name = qa_physical_input_name(&s->pending_physical);
  if (name == NULL) oom();
  snprintf(s->title, sizeof s->title, "%s is already bound", name);
  free(name);
  qa_ui_control *controls = s->controls;
  controls[0] = button(s, "ui:bindings:old", pool(s, previous),
                       qa_ui_menu_row_default(1), B_NONE, false);
  controls[1] = button(s, "ui:bindings:new", pool(s, next_label),
                       qa_ui_menu_row_default(2), B_NONE, false);
  free(previous);
  free(next_label);
  controls[2] = button(s, "ui:bindings:cancel", "Cancel",
                       qa_ui_menu_row_default(4), B_CANCEL, true);
  controls[3] = button(s, "ui:bindings:replace", "Replace binding",
                       qa_ui_menu_row_default(5), B_REPLACE, true);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:bindings:conflict";
  menu.title = s->title;
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = 4;
  menu.ctx = s;
  menu.close = conflict_close;
  return menu;
}
static qa_ui_menu reset_factory(void *v) {
  bind_state *s = v;
  pool_clear(s);
  qa_ui_control *controls = s->controls;
  controls[0] = button(s, "ui:bindings:keep", "Keep bindings",
                       qa_ui_menu_row_default(3), B_KEEP, true);
  controls[1] = button(s, "ui:bindings:restore", "Restore defaults",
                       qa_ui_menu_row_default(4), B_RESTORE,
                       s->reset.available(s->reset.ctx));
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:bindings:reset";
  menu.title = "Restore this player's default bindings?";
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = 2;
  return menu;
}
static qa_ui_menu root_factory(void *v) {
  bind_state *s = v;
  pool_clear(s);
  build_rows(s);
  const brow *current = current_row(s);
  set_selected(s, current != NULL ? current->id : NULL);
  qa_ui_control *controls = s->controls;
  size_t at = 0;
  qa_ui_control search;
  memset(&search, 0, sizeof search);
  search.id = "ui:bindings:search";
  search.kind = "text-entry";
  search.label = "Search actions";
  search.rect = (qa_rect){ 48, 80, 544, 28 };
  search.enabled = true;
  search.visible = true;
  search.text = pool(s, s->query != NULL ? s->query : "");
  search.maximum_length = 80;
  search.ctx = next_ctx(s, B_SEARCH, 0);
  search.change_text = on_change_search;
  search.submit_text = on_submit_noop;
  controls[at++] = search;
  controls[at++] = button(s, "ui:bindings:head", "Action",
                          (qa_rect){ 48, 112, 316, 24 }, B_NONE, false);
  controls[at++] = button(s, "ui:bindings:key-head", "Key",
                          (qa_rect){ 364, 112, 228, 24 }, B_NONE, false);
  /* List rows. */
  size_t row_total = s->row_count == 0 ? 1 : s->row_count;
  free(s->list_rows);
  free(s->cells);
  free(s->row_actions);
  s->list_rows = calloc(row_total, sizeof *s->list_rows);
  s->cells = calloc(row_total * 2, sizeof *s->cells);
  s->row_actions = calloc(row_total, sizeof *s->row_actions);
  if (s->list_rows == NULL || s->cells == NULL || s->row_actions == NULL)
    oom();
  if (s->row_count == 0) {
    s->cells[0] = "No matching actions";
    s->list_rows[0].id = "empty";
    s->list_rows[0].cells = (qa_string_view_list){ s->cells, 1 };
    s->list_rows[0].enabled = false;
  } else {
    for (size_t i = 0; i < s->row_count; i++) {
      const brow *row = &s->rows[i];
      s->cells[i * 2] = row->action->label;
      if (row->has_physical) {
        char *name = qa_physical_input_name(&row->physical);
        if (name == NULL) oom();
        s->cells[i * 2 + 1] = pool(s, name);
        free(name);
        s->row_actions[i].label = "X";
        s->rows[i].x_ctx = (bctx){ s, B_ROW_X, i };
        s->row_actions[i].ctx = &s->rows[i].x_ctx;
        s->row_actions[i].activate = on_row_x;
        s->list_rows[i].action = &s->row_actions[i];
      } else {
        s->cells[i * 2 + 1] = "Unbound";
      }
      s->list_rows[i].id = row->id;
      s->list_rows[i].cells =
          (qa_string_view_list){ s->cells + i * 2, 2 };
      s->list_rows[i].enabled = true;
    }
  }
  qa_ui_control list;
  memset(&list, 0, sizeof list);
  list.id = "ui:bindings:list";
  list.kind = "list";
  list.label = "Bindings";
  list.rect = (qa_rect){ 48, 136, 544, 240 };
  list.enabled = s->row_count > 0;
  list.visible = true;
  list.selected = s->selected;
  list.has_row_height = true;
  list.row_height = 24;
  s->column_widths[0] = 316;
  s->column_widths[1] = 208;
  list.column_widths = (qa_double_view){ s->column_widths, 2 };
  list.rows = s->list_rows;
  list.row_count = row_total;
  list.ctx = next_ctx(s, B_LIST_SELECT, 0);
  list.select_row = on_select_row;
  list.activate_row = on_activate_row;
  controls[at++] = list;
  bool have_current = current != NULL;
  controls[at++] = button(s, "ui:bindings:add", "Add binding",
                          (qa_rect){ 48, 380, 176, 28 }, B_ADD, have_current);
  controls[at++] = button(s, "ui:bindings:remove", "Remove key",
                          (qa_rect){ 232, 380, 176, 28 }, B_REMOVE,
                          have_current && current->has_physical);
  controls[at++] = button(s, "ui:bindings:clear", "Clear action",
                          (qa_rect){ 416, 380, 176, 28 }, B_CLEAR,
                          have_current && current->has_physical);
  if (s->has_scope && s->scope.available(s->scope.ctx)) {
    qa_ui_control local;
    memset(&local, 0, sizeof local);
    local.id = "ui:bindings:local";
    local.kind = "toggle";
    local.label = "Keep bindings to this game";
    local.rect = (qa_rect){ 156, 412, 252, 28 };
    local.enabled = true;
    local.visible = true;
    local.checked = s->scope.local(s->scope.ctx);
    local.ctx = next_ctx(s, B_NONE, 0);
    local.change_checked = on_change_local;
    controls[at++] = local;
  }
  if (s->has_reset)
    controls[at++] = button(s, "ui:bindings:reset", "Reset bindings",
                            (qa_rect){ 416, 412, 176, 28 }, B_RESET_OPEN,
                            s->reset.available(s->reset.ctx));
  controls[at++] = button(s, "ui:bindings:back", "Back",
                          (qa_rect){ 48, 412, 100, 28 }, B_BACK, true);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:bindings:0";
  menu.title = "Bindings";
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = at;
  return menu;
}

static void bindings_dispose(qa_ui_settings_menus *menus) {
  bind_state *s = menus->state;
  if (s == NULL) return;
  for (size_t i = s->handle_count; i-- > 0;)
    qa_native_ui_controller_unregister(s->controller, s->handles[i]);
  pool_clear(s);
  free(s->strings);
  free(s->rows);
  free(s->available);
  free(s->customs);
  free(s->list_rows);
  free(s->cells);
  free(s->row_actions);
  free(s->selected);
  free(s->query);
  free(s->pending_previous_text);
  snapshot_free(&s->capture);
  snapshot_free(&s->pending);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_binding_menus(
    qa_native_ui_controller *controller, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count),
    void *source_ctx, const qa_ui_binding_reset *reset) {
  return qa_ui_register_binding_menus_scoped(controller, input, source, source_count, get_source, source_ctx,
                                             reset, NULL);
}
qa_ui_settings_menus qa_ui_register_binding_menus_scoped(
    qa_native_ui_controller *controller, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count),
    void *source_ctx, const qa_ui_binding_reset *reset, const qa_ui_binding_scope *scope) {
  if (!qa_same_seat(qa_native_ui_controller_get_seat(controller),
                    qa_seat_input_seat(input)))
    qa_raise("Error", 0, 0, "Binding menu belongs to another input seat");
  bind_state *s = calloc(1, sizeof *s);
  if (s == NULL) oom();
  s->controller = controller;
  s->input = input;
  s->source = source;
  s->source_count = source_count;
  s->get_source = get_source;
  s->source_ctx = source_ctx;
  if (reset != NULL) {
    s->has_reset = true;
    s->reset = *reset;
  }
  if (scope != NULL) {
    s->has_scope = true;
    s->scope = *scope;
  }
  int handle = qa_native_ui_controller_register(
      controller, "menu:bindings:conflict", conflict_factory, s);
  if (handle < 0) oom();
  s->handles[s->handle_count++] = handle;
  if (reset != NULL) {
    handle = qa_native_ui_controller_register(controller,
                                              "menu:bindings:reset",
                                              reset_factory, s);
    if (handle < 0) oom();
    s->handles[s->handle_count++] = handle;
  }
  handle = qa_native_ui_controller_register(controller, "menu:bindings:0",
                                            root_factory, s);
  if (handle < 0) oom();
  s->handles[s->handle_count++] = handle;
  qa_ui_settings_menus menus = { "menu:bindings:0", s, bindings_dispose };
  return menus;
}
