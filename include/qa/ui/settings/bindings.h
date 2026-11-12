/* Port of src/ui/settings/bindings.ts.
 * C accommodations:
 *  - Row/custom-action ids use a stable key rendering of the
 *    physical input / target in place of TS JSON.stringify
 *    (rendered with qa_physical_input_name and the target text);
 *    ids are internal to the menu.
 * GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_BINDINGS_H
#define QA_UI_SETTINGS_BINDINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/ui.h"
#include "qa/input/seat.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

/** TS BindingReset. */
typedef struct qa_ui_binding_reset {
  void *ctx;
  bool (*available)(void *ctx);
  void (*reset)(void *ctx);
} qa_ui_binding_reset;

/** TS BindingScope: whether binding edits stay with the running game instead of reaching the engine's high
 * level and so every game. note_local (NULL when absent) is told of each key a local edit touches. */
typedef struct qa_ui_binding_scope {
  void *ctx;
  bool (*available)(void *ctx);
  bool (*local)(void *ctx);
  void (*set_local)(void *ctx, bool local);
  void (*note_local)(void *ctx, const qa_physical_input *input);
} qa_ui_binding_scope;

/** TS BindingAction. matches NULL when absent. */
typedef struct qa_ui_binding_action {
  const char *id;
  const char *label;
  qa_input_binding_target target;
  void *ctx;
  bool (*matches)(void *ctx, qa_input_binding_target target);
} qa_ui_binding_action;

/* Malloc'd physicalInputName; NULL only on OOM. */
char *qa_ui_physical_input_label(const qa_physical_input *input);
bool qa_ui_binding_matches_action(qa_input_binding_target target,
                                  const qa_ui_binding_action *action);

/** source may be a fixed array (get_source NULL) or a callback.
 * reset NULL omits the reset menu. */
qa_ui_settings_menus qa_ui_register_binding_menus(
    qa_native_ui_controller *controller, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count),
    void *source_ctx, const qa_ui_binding_reset *reset);
/** registerBindingMenus(controller, input, source, reset, scope): scope NULL omits the
 * "Keep bindings to this game" toggle and reports no local edits. */
qa_ui_settings_menus qa_ui_register_binding_menus_scoped(
    qa_native_ui_controller *controller, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count),
    void *source_ctx, const qa_ui_binding_reset *reset, const qa_ui_binding_scope *scope);

#endif
