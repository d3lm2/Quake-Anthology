/* Port of src/app/bootstrap/base-arena-select-menu.ts. UI control
 * construction (six controls) follows the TS factory; each factory call
 * rebuilds them, and they stay valid until the next call.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_BASE_ARENA_SELECT_MENU_H
#define QA_APP_BOOTSTRAP_BASE_ARENA_SELECT_MENU_H
#include <stdbool.h>
#include "qa/app/bootstrap/base_arena_selection.h"
#include "qa/ui/common/controller.h"
typedef struct qa_app_arena_selection_menu qa_app_arena_selection_menu;
typedef struct qa_app_arena_selection_menu_service {
  void *ctx;
  const qa_app_arena_selection *(*read)(void *ctx); /* NULL = TS null */
  void (*choose)(void *ctx, const char *map);
} qa_app_arena_selection_menu_service;
/* TS registerArenaSelectionMenu: registers with the controller; the
 * returned object is the disposer (destroy unregisters). */
qa_app_arena_selection_menu *qa_app_register_arena_selection_menu(qa_native_ui_controller *controller,
    qa_ui_menu_id id, qa_app_arena_selection_menu_service service);
void qa_app_arena_selection_menu_destroy(qa_app_arena_selection_menu *menu);
/* Reconciled view state for tests: builds the menu (a factory call) and reports it.
 * Every pointer is borrowed until the next factory call or state change. */
typedef struct qa_app_arena_selection_view {
  const char *tier, *selected;
  const qa_app_arena_selection_row *current; /* NULL when none */
  const char *opponents, *limits;
} qa_app_arena_selection_view;
qa_app_arena_selection_view qa_app_arena_selection_menu_view(qa_app_arena_selection_menu *menu);
void qa_app_arena_selection_menu_select_tier(qa_app_arena_selection_menu *menu, const char *tier);
void qa_app_arena_selection_menu_select(qa_app_arena_selection_menu *menu, const char *map);
bool qa_app_arena_selection_menu_activate(qa_app_arena_selection_menu *menu, const char *map);
bool qa_app_arena_selection_menu_choose_current(qa_app_arena_selection_menu *menu);
void qa_app_arena_selection_menu_open(qa_app_arena_selection_menu *menu);
#endif
