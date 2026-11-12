/* Port of src/app/bootstrap/base-arena-menu.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_menu.h"
#include "qa/network/common/js_number.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/base_arena_select_menu.h"
#include "qa/foundation/support.h"
const char qa_app_arena_progress_root[] = "menu:application:arena-progress";
static const struct { double skill; const char *label; } difficulties[] = {
    {1, "I Can Win"}, {2, "Bring It On"}, {3, "Hurt Me Plenty"}, {4, "Hardcore"}, {5, "Nightmare"}};
const char *qa_app_arena_difficulty_label(double skill) {
  for (size_t i = 0; i < 5; i++) if (difficulties[i].skill == skill) return difficulties[i].label;
  return "";
}
enum {
  ACT_NONE, ACT_SKILL, ACT_SKILL_BACK, ACT_OPEN_SELECTION, ACT_OPEN_RESET, ACT_CLOSE, ACT_CONFIRM_RESET,
  ACT_RETRY, ACT_NEXT, ACT_OPEN_ROOT, ACT_QUIT
};
typedef struct button_ctx {
  qa_app_base_arena_menus *menus;
  int action;
  double skill;
} button_ctx;
/* One factory's controls and their text, valid until that factory runs again. */
typedef struct menu_build {
  qa_ui_control *controls;
  button_ctx *ctxs;
  size_t count;
  char **pool;
  size_t pool_count;
  qa_ui_list_row *rows;
  const char **cells;
} menu_build;
enum { BUILD_SKILL, BUILD_PROGRESS, BUILD_RESET, BUILD_RESULT, BUILD_COUNT };
struct qa_app_base_arena_menus {
  qa_native_ui_controller *controller;
  qa_app_base_arena_menu_service service;
  qa_app_arena_selection_menu *selection_menu;
  int registrations[4];
  menu_build builds[BUILD_COUNT];
  char *shown;
  char *selected_map;
  bool closed;
};
static char *dup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p == NULL) qa_range_error("Out of memory");
  memcpy(p, s, n);
  return p;
}
static qa_ui_menu_id menu_id(const char *text) { return text; }
static const qa_app_arena_selection *select_read(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  return menus->service.selection(menus->service.ctx);
}
static void select_choose(void *ctx, const char *map) {
  qa_app_base_arena_menus *menus = ctx;
  char *copy = dup(map);
  free(menus->selected_map);
  menus->selected_map = copy;
  qa_native_ui_controller_open_menu(menus->controller, menu_id("menu:application:arena-skill"));
}
static void release_build(menu_build *build) {
  for (size_t i = 0; i < build->pool_count; i++) free(build->pool[i]);
  free(build->pool);
  free(build->controls);
  free(build->ctxs);
  free(build->rows);
  free(build->cells);
  *build = (menu_build){0};
}
/* Starts a build of `count` controls and `strings` pooled texts. */
static menu_build *begin_build(qa_app_base_arena_menus *menus, int which, size_t count, size_t strings) {
  menu_build *build = &menus->builds[which];
  release_build(build);
  build->controls = calloc(count ? count : 1, sizeof *build->controls);
  build->ctxs = calloc(count ? count : 1, sizeof *build->ctxs);
  build->pool = calloc(strings ? strings : 1, sizeof *build->pool);
  if (!build->controls || !build->ctxs || !build->pool) qa_range_error("Out of memory");
  return build;
}
static const char *pooled(menu_build *build, char *owned) {
  if (owned == NULL) qa_range_error("Out of memory");
  build->pool[build->pool_count++] = owned;
  return owned;
}
static char *format(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  char *text = length < 0 ? NULL : malloc((size_t)length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  vsnprintf(text, (size_t)length + 1, fmt, args);
  va_end(args);
  return text;
}
static void on_activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  button_ctx *c = ctx;
  qa_app_base_arena_menus *menus = c->menus;
  switch (c->action) {
    case ACT_SKILL:
      if (menus->selected_map != NULL) {
        char *map = dup(menus->selected_map);
        menus->service.play(menus->service.ctx, map, c->skill);
        free(map);
      }
      break;
    case ACT_SKILL_BACK:
    case ACT_CLOSE: qa_native_ui_controller_close_menu(menus->controller); break;
    case ACT_OPEN_SELECTION: qa_native_ui_controller_open_menu(menus->controller, menu_id("menu:application:arena-selection")); break;
    case ACT_OPEN_RESET: qa_native_ui_controller_open_menu(menus->controller, menu_id("menu:application:arena-reset")); break;
    case ACT_CONFIRM_RESET:
      menus->service.reset(menus->service.ctx);
      qa_native_ui_controller_close_menu(menus->controller);
      break;
    case ACT_RETRY: menus->service.retry(menus->service.ctx); break;
    case ACT_NEXT: menus->service.next(menus->service.ctx); break;
    case ACT_OPEN_ROOT: qa_native_ui_controller_open_menu(menus->controller, menu_id(qa_app_arena_progress_root)); break;
    case ACT_QUIT: menus->service.quit(menus->service.ctx); break;
    default: break;
  }
}
/* button(id, label, y, activate, enabled = true) */
static qa_ui_control *button(qa_app_base_arena_menus *menus, menu_build *build, const char *id, const char *label, double y,
                             int action, bool enabled) {
  size_t at = build->count++;
  build->ctxs[at] = (button_ctx){menus, action, 0};
  qa_ui_control *control = &build->controls[at];
  control->id = id;
  control->kind = "button";
  control->label = label;
  control->rect = (qa_rect){64, y, 512, 32};
  control->enabled = enabled;
  control->visible = true;
  control->ctx = &build->ctxs[at];
  control->activate_seat = on_activate;
  return control;
}
static qa_ui_menu finish(menu_build *build, const char *id, const char *title) {
  qa_ui_menu out = {0};
  out.id = menu_id(id);
  out.title = title;
  out.full_screen = true;
  out.controls = build->controls;
  out.control_count = build->count;
  return out;
}
static qa_ui_menu difficulty_menu(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  menu_build *build = begin_build(menus, BUILD_SKILL, 6, 10);
  for (size_t index = 0; index < 5; index++) {
    /* `${entry.skill === service.skill() ? "> " : ""}${entry.label}`: one skill() read per entry. */
    bool chosen = difficulties[index].skill == menus->service.skill(menus->service.ctx);
    const char *id = pooled(build, format("ui:arena:skill:%s", QA_JS_NUMBER(difficulties[index].skill)));
    const char *label = pooled(build, format("%s%s", chosen ? "> " : "", difficulties[index].label));
    qa_ui_control *control = button(menus, build, id, label, 108 + (double)index * 44, ACT_SKILL, true);
    ((button_ctx *)control->ctx)->skill = difficulties[index].skill;
  }
  button(menus, build, "ui:arena:skill-back", "Back", 420, ACT_SKILL_BACK, true);
  return finish(build, "menu:application:arena-skill", "Difficulty");
}
static void select_nothing(void *ctx, qa_seat_id seat, const char *row) {
  (void)ctx;
  (void)seat;
  (void)row;
}
static qa_ui_menu progress_menu(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  size_t row_count = 0;
  const qa_app_arena_progress_row *progress = menus->service.progress(menus->service.ctx, &row_count);
  menu_build *build = begin_build(menus, BUILD_PROGRESS, 4, row_count * 3);
  build->rows = calloc(row_count ? row_count : 1, sizeof *build->rows);
  build->cells = calloc(row_count ? row_count * 2 : 1, sizeof *build->cells);
  if (!build->rows || !build->cells) qa_range_error("Out of memory");
  for (size_t index = 0; index < row_count; index++) {
    build->cells[index * 2] = pooled(build, dup(progress[index].label));
    build->cells[index * 2 + 1] = pooled(build, dup(progress[index].value));
    const char *id = pooled(build, format("%s", QA_JS_NUMBER((double)index)));
    build->rows[index] = (qa_ui_list_row){NULL, id, {&build->cells[index * 2], 2}, NULL, true};
  }
  size_t at = build->count++;
  build->ctxs[at] = (button_ctx){menus, ACT_NONE, 0};
  qa_ui_control *list = &build->controls[at];
  list->id = "ui:arena:progress";
  list->kind = "list";
  list->label = "Progress and awards";
  list->rect = (qa_rect){48, 96, 544, 231};
  list->has_row_height = true;
  list->row_height = 33;
  list->rows = build->rows;
  list->row_count = row_count;
  list->selected = NULL;
  list->ctx = &build->ctxs[at];
  list->select_row = select_nothing;
  list->enabled = true;
  list->visible = true;
  button(menus, build, "ui:arena:select", "Choose an arena", 336, ACT_OPEN_SELECTION, true);
  button(menus, build, "ui:arena:reset", "Reset progress...", 376, ACT_OPEN_RESET, true);
  button(menus, build, "ui:arena:back", "Back", 424, ACT_CLOSE, true);
  return finish(build, qa_app_arena_progress_root, "Arena progress");
}
static qa_ui_menu reset_menu(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  menu_build *build = begin_build(menus, BUILD_RESET, 2, 0);
  button(menus, build, "ui:arena:cancel", "Keep progress", 180, ACT_CLOSE, true);
  button(menus, build, "ui:arena:confirm-reset", "Reset progress", 230, ACT_CONFIRM_RESET, true);
  return finish(build, "menu:application:arena-reset", "Reset arena progress and awards?");
}
static qa_ui_menu result_menu(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  const qa_app_arena_postgame_presentation *result = NULL;
  bool has = menus->service.result(menus->service.ctx, &result);
  size_t podium = has ? result->podium_count : 0;
  menu_build *build = begin_build(menus, BUILD_RESULT, podium + 5, podium * 2);
  const char *title = has && result->result.rank == 1 ? "Victory" : "Match complete";
  for (size_t index = 0; index < podium; index++) {
    const qa_app_arena_podium_player *player = &result->podium[index];
    const char *id = pooled(build, format("ui:arena:podium:%s", QA_JS_NUMBER((double)index)));
    const char *name = menus->service.player_name(menus->service.ctx, player->client);
    const char *label = pooled(build, format("%s. %s   %s", QA_JS_NUMBER(player->rank), name, QA_JS_NUMBER(player->score)));
    button(menus, build, id, label, 96 + (double)index * 38, ACT_NONE, false);
  }
  button(menus, build, "ui:arena:retry", "Retry", 258, ACT_RETRY, true);
  button(menus, build, "ui:arena:next", "Next match", 300, ACT_NEXT, has && result->result.next_level >= 0);
  button(menus, build, "ui:arena:arenas", "Choose an arena", 342, ACT_OPEN_SELECTION, true);
  button(menus, build, "ui:arena:awards", "Progress and awards", 384, ACT_OPEN_ROOT, true);
  button(menus, build, "ui:arena:main", "Main menu", 426, ACT_QUIT, true);
  return finish(build, "menu:application:arena-result", title);
}
qa_app_base_arena_menus *qa_app_base_arena_menus_create(qa_native_ui_controller *controller, qa_app_base_arena_menu_service service) {
  qa_app_base_arena_menus *menus = calloc(1, sizeof *menus);
  if (!menus) return NULL;
  menus->controller = controller;
  menus->service = service;
  qa_app_arena_selection_menu_service select_service = {menus, select_read, select_choose};
  menus->selection_menu = qa_app_register_arena_selection_menu(controller, menu_id("menu:application:arena-selection"), select_service);
  menus->registrations[0] = qa_native_ui_controller_register(controller, menu_id("menu:application:arena-skill"), difficulty_menu, menus);
  menus->registrations[1] = qa_native_ui_controller_register(controller, menu_id(qa_app_arena_progress_root), progress_menu, menus);
  menus->registrations[2] = qa_native_ui_controller_register(controller, menu_id("menu:application:arena-reset"), reset_menu, menus);
  menus->registrations[3] = qa_native_ui_controller_register(controller, menu_id("menu:application:arena-result"), result_menu, menus);
  return menus;
}
void qa_app_base_arena_menus_destroy(qa_app_base_arena_menus *menus) {
  if (!menus) return;
  qa_app_base_arena_menus_close(menus);
  for (int i = 0; i < BUILD_COUNT; i++) release_build(&menus->builds[i]);
  free(menus->shown);
  free(menus->selected_map);
  free(menus);
}
const char *qa_app_base_arena_menus_root(const qa_app_base_arena_menus *menus) {
  (void)menus;
  return qa_app_arena_progress_root;
}
void qa_app_base_arena_menus_update(qa_app_base_arena_menus *menus) {
  const qa_app_arena_postgame_presentation *result = NULL;
  if (!menus->service.result(menus->service.ctx, &result)) {
    free(menus->shown);
    menus->shown = NULL;
    return;
  }
  /* TS identity = JSON.stringify([result.result, result.podium]). */
  char identity[4096];
  size_t len = (size_t)snprintf(identity, sizeof identity, "[{\"rank\":%s,\"completedTier\":%s,\"unlockedMovie\":%s,\"nextLevel\":%s,\"awards\":[",
      QA_JS_NUMBER(result->result.rank), QA_JS_NUMBER(result->result.completed_tier),
      result->result.unlocked_movie < 0 ? "null" : "?", QA_JS_NUMBER(result->result.next_level));
  if (result->result.unlocked_movie >= 0) {
    /* Rebuild the movie field text exactly once. */
    char movie[40];
    snprintf(movie, sizeof movie, "%s", QA_JS_NUMBER(result->result.unlocked_movie));
    char *marker = strstr(identity, "\"unlockedMovie\":?");
    if (marker) {
      char tail[4096];
      snprintf(tail, sizeof tail, "%s", marker + 17);
      snprintf(marker, sizeof identity - (size_t)(marker - identity), "\"unlockedMovie\":%s", movie);
      strncat(identity, tail, sizeof identity - strlen(identity) - 1);
      len = strlen(identity);
    }
  }
  for (size_t i = 0; i < result->result.award_count; i++)
    len += (size_t)snprintf(identity + len, sizeof identity - len, "%s{\"medal\":%s,\"amount\":%s}",
        i ? "," : "", QA_JS_NUMBER(result->result.awards[i].medal), QA_JS_NUMBER(result->result.awards[i].amount));
  len += (size_t)snprintf(identity + len, sizeof identity - len, "]},[");
  for (size_t i = 0; i < result->podium_count; i++)
    len += (size_t)snprintf(identity + len, sizeof identity - len, "%s{\"client\":%s,\"rank\":%s,\"score\":%s}",
        i ? "," : "", QA_JS_NUMBER(result->podium[i].client), QA_JS_NUMBER(result->podium[i].rank), QA_JS_NUMBER(result->podium[i].score));
  snprintf(identity + len, sizeof identity - len, "]]");
  if (menus->shown && strcmp(menus->shown, identity) == 0) return;
  free(menus->shown);
  menus->shown = dup(identity);
  qa_native_ui_controller_close_all(menus->controller);
  qa_native_ui_controller_open_menu(menus->controller, menu_id("menu:application:arena-result"));
}
void qa_app_base_arena_menus_close(qa_app_base_arena_menus *menus) {
  if (!menus || menus->closed) return;
  menus->closed = true;
  qa_app_arena_selection_menu_destroy(menus->selection_menu);
  menus->selection_menu = NULL;
  for (size_t i = 0; i < 4; i++) qa_native_ui_controller_unregister(menus->controller, menus->registrations[i]);
}
