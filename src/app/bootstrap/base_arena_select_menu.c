/* Port of src/app/bootstrap/base-arena-select-menu.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_select_menu.h"
#include "qa/network/common/js_number.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
/* One list row as the build's `rows` closure holds it (map and availability). */
typedef struct built_row {
  char *map;
  bool available;
} built_row;
typedef struct control_ctx {
  qa_app_arena_selection_menu *menu;
  int action;
} control_ctx;
enum { ACT_TIER, ACT_LIST, ACT_LABEL, ACT_PLAY, ACT_BACK, ACT_COUNT };
struct qa_app_arena_selection_menu {
  qa_native_ui_controller *controller;
  qa_ui_menu_id id;
  qa_app_arena_selection_menu_service service;
  int registration;
  char *tier, *selected;
  /* The last factory's menu, valid until the next factory call (the TS closures). */
  qa_ui_control controls[6];
  control_ctx ctxs[ACT_COUNT];
  qa_ui_choice *choices;
  qa_ui_list_row *rows;
  const char **cells;
  built_row *built;
  size_t built_count;
  char **pool;
  size_t pool_count, pool_capacity;
  char *current_map; /* NULL = current undefined */
  bool current_available;
  char *opponents, *limits;
};
static char *dup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p == NULL) qa_range_error("Out of memory");
  memcpy(p, s, n);
  return p;
}
static char *pooled(qa_app_arena_selection_menu *menu, const char *text) {
  if (menu->pool_count == menu->pool_capacity) {
    size_t capacity = menu->pool_capacity ? menu->pool_capacity * 2 : 32;
    char **grown = realloc(menu->pool, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    menu->pool = grown;
    menu->pool_capacity = capacity;
  }
  char *copy = dup(text);
  menu->pool[menu->pool_count++] = copy;
  return copy;
}
static void release_build(qa_app_arena_selection_menu *menu) {
  for (size_t i = 0; i < menu->pool_count; i++) free(menu->pool[i]);
  menu->pool_count = 0;
  free(menu->choices);
  free(menu->rows);
  free(menu->cells);
  for (size_t i = 0; i < menu->built_count; i++) free(menu->built[i].map);
  free(menu->built);
  free(menu->current_map);
  free(menu->opponents);
  free(menu->limits);
  menu->choices = NULL;
  menu->rows = NULL;
  menu->cells = NULL;
  menu->built = NULL;
  menu->built_count = 0;
  menu->current_map = NULL;
  menu->opponents = NULL;
  menu->limits = NULL;
}
static void reconcile(qa_app_arena_selection_menu *menu, const qa_app_arena_selection *selection) {
  if (selection) {
    bool known = false;
    for (size_t i = 0; i < selection->tier_count; i++) if (menu->tier && strcmp(selection->tiers[i].id, menu->tier) == 0) known = true;
    if (!known) {
      /* selection.rows.find(map === current)?.tier ?? selection.tiers[0]?.id ?? "" */
      const char *tier = NULL;
      for (size_t i = 0; i < selection->row_count; i++)
        if (selection->current && strcmp(selection->rows[i].arena->map, selection->current) == 0) { tier = selection->rows[i].tier; break; }
      if (tier == NULL) tier = selection->tier_count ? selection->tiers[0].id : "";
      char *copy = dup(tier);
      free(menu->tier);
      menu->tier = copy;
    }
  }
  bool selected_known = false;
  const char *fallback = NULL, *first = NULL;
  if (selection && menu->tier) {
    for (size_t i = 0; i < selection->row_count; i++) {
      if (strcmp(selection->rows[i].tier, menu->tier) != 0) continue;
      if (!first) first = selection->rows[i].arena->map;
      if (menu->selected && strcmp(selection->rows[i].arena->map, menu->selected) == 0) selected_known = true;
      /* rows.find: the first row on the current map. */
      if (!fallback && selection->current && strcmp(selection->rows[i].arena->map, selection->current) == 0)
        fallback = selection->rows[i].arena->map;
    }
  }
  if (!selected_known) {
    char *copy = dup(fallback ? fallback : first);
    free(menu->selected);
    menu->selected = copy;
  }
}
/* rows.find(row => row.arena.map === selected) over the tier's rows. */
static const qa_app_arena_selection_row *current_row(const qa_app_arena_selection_menu *menu,
                                                     const qa_app_arena_selection *selection) {
  if (selection == NULL || menu->selected == NULL) return NULL;
  for (size_t i = 0; i < selection->row_count; i++)
    if (strcmp(selection->rows[i].tier, menu->tier) == 0 && strcmp(selection->rows[i].arena->map, menu->selected) == 0)
      return &selection->rows[i];
  return NULL;
}
/* `Opponents: ${bots.join(", ") || "None"}` and the limits line, as owned text. */
static void describe(const qa_app_base_arena *arena, char **opponents, char **limits) {
  size_t length = strlen("Opponents: ") + strlen("None") + 1;
  for (size_t i = 0; i < arena->bot_count; i++) length += strlen(arena->bots[i]) + 2;
  char *text = malloc(length);
  if (text == NULL) qa_range_error("Out of memory");
  strcpy(text, "Opponents: ");
  size_t at = strlen(text);
  for (size_t i = 0; i < arena->bot_count; i++) {
    if (i) { memcpy(text + at, ", ", 2); at += 2; }
    size_t n = strlen(arena->bots[i]);
    memcpy(text + at, arena->bots[i], n);
    at += n;
  }
  text[at] = '\0';
  if (at == strlen("Opponents: ")) strcpy(text + at, "None");
  *opponents = text;
  /* [frag > 0 ? `${frag} frags` : "", time > 0 ? `${time} minutes` : ""].filter(Boolean).join(" · ") */
  char frag[64] = "", time[64] = "";
  if (arena->frag_limit > 0) snprintf(frag, sizeof frag, "%s frags", QA_JS_NUMBER(arena->frag_limit));
  if (arena->time_limit > 0) snprintf(time, sizeof time, "%s minutes", QA_JS_NUMBER(arena->time_limit));
  char *line = malloc(strlen(frag) + strlen(time) + sizeof " \xc2\xb7 ");
  if (line == NULL) qa_range_error("Out of memory");
  strcpy(line, frag);
  if (frag[0] && time[0]) strcat(line, " \xc2\xb7 ");
  strcat(line, time);
  *limits = line;
}
static void on_select_choice(void *ctx, qa_seat_id seat, const char *value) {
  (void)seat;
  control_ctx *c = ctx;
  qa_app_arena_selection_menu_select_tier(c->menu, value);
}
static void on_select_row(void *ctx, qa_seat_id seat, const char *value) {
  (void)seat;
  control_ctx *c = ctx;
  qa_app_arena_selection_menu_select(c->menu, value);
}
static void on_activate_row(void *ctx, qa_seat_id seat, const char *value) {
  (void)seat;
  qa_app_arena_selection_menu *menu = ((control_ctx *)ctx)->menu;
  /* The build's rows.find(row => row.arena.map === value). */
  for (size_t i = 0; i < menu->built_count; i++)
    if (strcmp(menu->built[i].map, value) == 0) {
      if (menu->built[i].available) {
        char *map = dup(value);
        menu->service.choose(menu->service.ctx, map);
        free(map);
      }
      return;
    }
}
static void on_activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  control_ctx *c = ctx;
  qa_app_arena_selection_menu *menu = c->menu;
  if (c->action == ACT_PLAY) {
    /* choose(): current?.available, current captured by the build. */
    if (menu->current_map != NULL && menu->current_available) {
      char *map = dup(menu->current_map);
      menu->service.choose(menu->service.ctx, map);
      free(map);
    }
  } else if (c->action == ACT_BACK) {
    qa_native_ui_controller_close_menu(menu->controller);
  }
}
static void on_open(void *ctx, qa_seat_id seat) {
  (void)seat;
  qa_app_arena_selection_menu_open(ctx);
}
static qa_ui_control label_control(qa_app_arena_selection_menu *menu, const char *id, const char *text, double y) {
  qa_ui_control control = {0};
  control.id = id;
  control.kind = "button";
  control.label = text;
  control.rect = (qa_rect){48, y, 544, 28};
  control.visible = true;
  control.enabled = false;
  control.ctx = &menu->ctxs[ACT_LABEL];
  control.activate_seat = on_activate;
  return control;
}
static qa_ui_menu make_menu(void *ctx) {
  qa_app_arena_selection_menu *menu = ctx;
  const qa_app_arena_selection *selection = menu->service.read(menu->service.ctx);
  reconcile(menu, selection);
  release_build(menu);
  size_t tier_count = selection ? selection->tier_count : 0, row_count = 0;
  if (selection)
    for (size_t i = 0; i < selection->row_count; i++) if (strcmp(selection->rows[i].tier, menu->tier) == 0) row_count++;
  menu->choices = calloc(tier_count ? tier_count : 1, sizeof *menu->choices);
  menu->rows = calloc(row_count ? row_count : 1, sizeof *menu->rows);
  menu->cells = calloc(row_count ? row_count * 2 : 1, sizeof *menu->cells);
  menu->built = calloc(row_count ? row_count : 1, sizeof *menu->built);
  if (!menu->choices || !menu->rows || !menu->cells || !menu->built) qa_range_error("Out of memory");
  for (size_t i = 0; i < tier_count; i++)
    menu->choices[i] = (qa_ui_choice){pooled(menu, selection->tiers[i].id), pooled(menu, selection->tiers[i].label)};
  for (size_t i = 0, at = 0; selection && i < selection->row_count; i++) {
    const qa_app_arena_selection_row *row = &selection->rows[i];
    if (strcmp(row->tier, menu->tier) != 0) continue;
    menu->built[at] = (built_row){dup(row->arena->map), row->available};
    menu->built_count = at + 1;
    menu->cells[at * 2] = pooled(menu, row->arena->title);
    menu->cells[at * 2 + 1] = pooled(menu, row->available ? row->record : "Locked");
    menu->rows[at] = (qa_ui_list_row){NULL, menu->built[at].map, {&menu->cells[at * 2], 2}, NULL, true};
    at++;
  }
  const qa_app_arena_selection_row *current = current_row(menu, selection);
  if (current != NULL) {
    menu->current_map = dup(current->arena->map);
    menu->current_available = current->available;
    describe(current->arena, &menu->opponents, &menu->limits);
  } else {
    menu->opponents = dup("No single-player arenas are available.");
    menu->limits = dup("");
  }
  for (int i = 0; i < ACT_COUNT; i++) menu->ctxs[i] = (control_ctx){menu, i};
  qa_ui_control *tier = &menu->controls[0];
  *tier = (qa_ui_control){0};
  tier->id = "ui:arena-select:tier";
  tier->kind = "choice";
  tier->label = "Tier";
  tier->rect = (qa_rect){48, 88, 544, 30};
  tier->visible = true;
  tier->enabled = selection != NULL;
  tier->choices = menu->choices;
  tier->choice_count = tier_count;
  tier->selected = pooled(menu, menu->tier);
  tier->ctx = &menu->ctxs[ACT_TIER];
  tier->select_choice = on_select_choice;
  qa_ui_control *list = &menu->controls[1];
  *list = (qa_ui_control){0};
  list->id = "ui:arena-select:arenas";
  list->kind = "list";
  list->label = "Arenas";
  list->rect = (qa_rect){48, 124, 544, 156};
  list->has_row_height = true;
  list->row_height = 39;
  list->visible = true;
  list->enabled = row_count > 0;
  list->selected = menu->selected ? pooled(menu, menu->selected) : NULL;
  list->rows = menu->rows;
  list->row_count = row_count;
  list->ctx = &menu->ctxs[ACT_LIST];
  list->select_row = on_select_row;
  list->activate_row = on_activate_row;
  menu->controls[2] = label_control(menu, "ui:arena-select:opponents", menu->opponents, 286);
  menu->controls[3] = label_control(menu, "ui:arena-select:limits", menu->limits, 318);
  qa_ui_control *play = &menu->controls[4];
  *play = (qa_ui_control){0};
  play->id = "ui:arena-select:play";
  play->kind = "button";
  play->label = current != NULL && !current->available ? "Win the preceding tier to unlock" : "Choose difficulty";
  play->rect = (qa_rect){48, 352, 544, 30};
  play->visible = true;
  play->enabled = current != NULL && current->available;
  play->ctx = &menu->ctxs[ACT_PLAY];
  play->activate_seat = on_activate;
  qa_ui_control *back = &menu->controls[5];
  *back = (qa_ui_control){0};
  back->id = "ui:arena-select:back";
  back->kind = "button";
  back->label = "Back";
  back->rect = (qa_rect){48, 390, 544, 30};
  back->visible = true;
  back->enabled = true;
  back->ctx = &menu->ctxs[ACT_BACK];
  back->activate_seat = on_activate;
  qa_ui_menu out = {0};
  out.id = menu->id;
  out.title = "Choose an arena";
  out.full_screen = true;
  out.controls = menu->controls;
  out.control_count = 6;
  out.ctx = menu;
  out.open = on_open;
  return out;
}
qa_app_arena_selection_menu *qa_app_register_arena_selection_menu(qa_native_ui_controller *controller,
    qa_ui_menu_id id, qa_app_arena_selection_menu_service service) {
  qa_app_arena_selection_menu *menu = calloc(1, sizeof *menu);
  if (!menu) return NULL;
  menu->controller = controller;
  menu->id = id;
  menu->service = service;
  menu->tier = dup("");
  menu->registration = qa_native_ui_controller_register(controller, id, make_menu, menu);
  return menu;
}
void qa_app_arena_selection_menu_destroy(qa_app_arena_selection_menu *menu) {
  if (!menu) return;
  qa_native_ui_controller_unregister(menu->controller, menu->registration);
  release_build(menu);
  free(menu->pool);
  free(menu->tier);
  free(menu->selected);
  free(menu);
}
qa_app_arena_selection_view qa_app_arena_selection_menu_view(qa_app_arena_selection_menu *menu) {
  qa_ui_menu built = make_menu(menu);
  qa_app_arena_selection_view view = {0};
  view.tier = menu->tier;
  view.selected = menu->selected;
  view.current = current_row(menu, menu->service.read(menu->service.ctx));
  view.opponents = built.controls[2].label;
  view.limits = built.controls[3].label;
  return view;
}
void qa_app_arena_selection_menu_select_tier(qa_app_arena_selection_menu *menu, const char *tier) {
  char *copy = dup(tier);
  free(menu->tier);
  menu->tier = copy;
  free(menu->selected);
  menu->selected = NULL;
}
void qa_app_arena_selection_menu_select(qa_app_arena_selection_menu *menu, const char *map) {
  char *copy = dup(map);
  free(menu->selected);
  menu->selected = copy;
}
bool qa_app_arena_selection_menu_activate(qa_app_arena_selection_menu *menu, const char *map) {
  make_menu(menu);
  for (size_t i = 0; i < menu->built_count; i++)
    if (strcmp(menu->built[i].map, map) == 0) {
      if (!menu->built[i].available) return false;
      char *copy = dup(map);
      menu->service.choose(menu->service.ctx, copy);
      free(copy);
      return true;
    }
  return false;
}
bool qa_app_arena_selection_menu_choose_current(qa_app_arena_selection_menu *menu) {
  make_menu(menu);
  if (menu->current_map == NULL || !menu->current_available) return false;
  char *map = dup(menu->current_map);
  menu->service.choose(menu->service.ctx, map);
  free(map);
  return true;
}
void qa_app_arena_selection_menu_open(qa_app_arena_selection_menu *menu) {
  char *copy = dup("");
  free(menu->tier);
  menu->tier = copy;
  free(menu->selected);
  menu->selected = NULL;
}
