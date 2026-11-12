/* Port of src/app/bootstrap/startup-input-profile.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_input_profile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/native_settings.h"
#include "qa/foundation/support.h"
#include "qa/input/bindings.h"
#include "qa/input/gamepad.h"
#include "qa/input/mouse.h"
#include "qa/input/support.h"
/** The front end edits the same first-seat profile that gameplay loads. */
struct qa_app_startup_input_profile {
  qa_config_store settings;
  qa_seat_input *input;
  char *baseline;
  char *gamepad_baseline;
  qa_app_stock_native *native; /* owned; NULL = TS null */
  bool bind_local;
};
static char *checked(char *text) {
  if (text == NULL) qa_range_error("Out of memory");
  return text;
}
/* JSON.stringify(this.input.bindings) / JSON.stringify(this.input.gamepad.tuning) */
static char *bindings_text(qa_seat_input *input) {
  size_t count = 0;
  const qa_input_binding *bindings = qa_seat_input_bindings(input, &count);
  return checked(qa_input_bindings_json(bindings, count));
}
static char *gamepad_text(qa_seat_input *input) { return checked(qa_gamepad_tuning_json(&qa_seat_input_gamepad(input)->tuning)); }
static qa_app_startup_input_profile *make_profile(const qa_config_store *settings, qa_seat_input *input) {
  qa_app_startup_input_profile *p = calloc(1, sizeof *p);
  if (p == NULL) qa_range_error("Out of memory");
  if (!qa_config_store_init(&p->settings, settings->root)) qa_range_error("Out of memory");
  p->input = input;
  return p;
}
/* archivedBindingPairs over a binding list, as a borrowed-pair view. The caller frees both. */
static qa_layer_pair *archived_pairs(const qa_input_binding *bindings, size_t count, qa_binding_pair **out_pairs, size_t *out_count) {
  *out_pairs = qa_archived_binding_pairs(bindings, count, false, out_count);
  if (*out_pairs == NULL && *out_count > 0) qa_range_error("Out of memory");
  qa_layer_pair *view = calloc(*out_count + 1, sizeof *view);
  if (view == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < *out_count; i++) view[i] = (qa_layer_pair){(*out_pairs)[i].key, (*out_pairs)[i].command};
  return view;
}
/* native?.layers?.noteDefaultBinds(archivedBindingPairs({ bindings: defaults })) */
static void note_defaults(qa_app_stock_native *native, const qa_input_binding *defaults, size_t count) {
  if (native == NULL || native->config.layers == NULL) return;
  qa_binding_pair *pairs = NULL;
  size_t pair_count = 0;
  qa_layer_pair *view = archived_pairs(defaults, count, &pairs, &pair_count);
  qa_game_layers_note_default_binds(native->config.layers, view, pair_count);
  free(view);
  qa_binding_pairs_free(pairs, pair_count);
}
qa_app_startup_input_profile *qa_app_startup_input_profile_retained(const qa_config_store *settings, qa_seat_input *input) {
  return qa_app_startup_input_profile_retained_native(settings, input, NULL, NULL, 0);
}
bool qa_app_startup_input_profile_bind_local(const qa_app_startup_input_profile *p) { return p->bind_local; }
void qa_app_startup_input_profile_set_bind_local(qa_app_startup_input_profile *p, bool local) { p->bind_local = local; }
bool qa_app_startup_input_profile_layered(const qa_app_startup_input_profile *p) {
  return p->native != NULL && p->native->config.layers != NULL;
}
qa_app_startup_input_profile *qa_app_startup_input_profile_retained_native(const qa_config_store *settings, qa_seat_input *input,
    qa_app_stock_native *native, const qa_input_binding *defaults, size_t default_count) {
  qa_app_startup_input_profile *p = make_profile(settings, input);
  p->native = native;
  note_defaults(native, defaults, default_count);
  p->baseline = bindings_text(input);
  p->gamepad_baseline = gamepad_text(input);
  return p;
}
static void seat_settings_cleanup(void *arg) { qa_seat_settings_free(arg); }
static void profile_cleanup(void *arg) { qa_app_startup_input_profile_destroy(arg); }
qa_app_startup_input_profile *qa_app_startup_input_profile_open(const qa_config_store *settings, qa_seat_input *input, qa_command_dialect dialect,
                                                                const qa_weapon_binding_item *items, size_t item_count) {
  return qa_app_startup_input_profile_open_native(settings, input, dialect, items, item_count, NULL);
}
static void bindings_cleanup(void *arg) { qa_native_bindings_free(arg); }
qa_app_startup_input_profile *qa_app_startup_input_profile_open_native(const qa_config_store *settings, qa_seat_input *input,
    qa_command_dialect dialect, const qa_weapon_binding_item *items, size_t item_count, qa_app_stock_native *native) {
  qa_app_startup_input_profile *p = make_profile(settings, input);
  p->native = native;
  qa_cleanup profile_guard;
  qa_cleanup_push(&profile_guard, profile_cleanup, p);
  size_t default_count = 0;
  qa_input_binding *defaults = qa_default_bindings(0, dialect, items, item_count, &default_count);
  if (defaults == NULL) qa_range_error("Out of memory");
  note_defaults(native, defaults, default_count);
  qa_seat_settings saved;
  memset(&saved, 0, sizeof saved);
  bool has_saved = qa_config_store_load_seat(settings, "input/seat-1.json", &saved);
  qa_cleanup saved_guard;
  qa_cleanup_push(&saved_guard, seat_settings_cleanup, &saved);
  qa_native_bindings own = {NULL, 0};
  bool has_own = native != NULL && qa_native_config_read_bindings(&native->config, &own);
  qa_cleanup own_guard;
  qa_cleanup_push(&own_guard, bindings_cleanup, &own);
  qa_seat_input_unbind_all(input);
  if (has_own) {
    /* A stock game's own file outranks the user store. */
    for (size_t i = 0; i < default_count; i++) qa_seat_input_bind(input, &defaults[i]);
    for (size_t i = 0; i < own.count; i++) {
      qa_input_binding binding;
      memset(&binding, 0, sizeof binding);
      if (!qa_named_physical_input(own.pairs[i].key, 0, &binding.input)) continue;
      binding.target.is_action = false;
      binding.target.text = own.pairs[i].command;
      qa_seat_input_bind(input, &binding);
    }
  } else if (has_saved) {
    for (size_t i = 0; i < saved.binding_count; i++) qa_seat_input_bind(input, &saved.bindings[i]);
  } else {
    for (size_t i = 0; i < default_count; i++) qa_seat_input_bind(input, &defaults[i]);
  }
  if (has_saved) qa_seat_input_gamepad(input)->tuning = saved.gamepad;
  qa_cleanup_pop(&own_guard);
  qa_native_bindings_free(&own);
  for (size_t i = 0; i < default_count; i++) qa_input_binding_dispose(&defaults[i]);
  free(defaults);
  qa_cleanup_pop(&saved_guard);
  qa_seat_settings_free(&saved);
  p->baseline = bindings_text(input);
  p->gamepad_baseline = gamepad_text(input);
  qa_cleanup_pop(&profile_guard);
  return p;
}
void qa_app_startup_input_profile_destroy(qa_app_startup_input_profile *p) {
  if (p == NULL) return;
  qa_config_store_destroy(&p->settings);
  qa_app_stock_native_destroy(p->native);
  free(p->baseline);
  free(p->gamepad_baseline);
  free(p);
}
qa_seat_input *qa_app_startup_input_profile_input(const qa_app_startup_input_profile *p) { return p->input; }
/* Object.keys(values).length */
static size_t override_count(const qa_app_frontend_preference_overrides *v) {
  if (v == NULL) return 0;
  const bool present[] = {v->has_effects_volume, v->has_music_volume, v->has_controller_vibration, v->has_controller_vibration_strength,
                          v->has_sensitivity, v->has_pitch, v->has_yaw, v->has_invert_mouse, v->has_acceleration, v->has_filter,
                          v->has_look_spring, v->has_look_strafe, v->has_free_look, v->has_always_run};
  size_t count = 0;
  for (size_t i = 0; i < sizeof present / sizeof present[0]; i++) count += present[i];
  return count;
}
typedef struct save_scope {
  qa_input_binding *bindings; size_t binding_count;
  char *current, *gamepad_current, *selected_text, *baseline_text;
  qa_seat_settings saved; bool has_saved;
} save_scope;
static void save_scope_free(void *arg) {
  save_scope *scope = arg;
  for (size_t i = 0; i < scope->binding_count; i++) qa_input_binding_dispose(&scope->bindings[i]);
  free(scope->bindings);
  scope->bindings = NULL;
  scope->binding_count = 0;
  free(scope->current); free(scope->gamepad_current); free(scope->selected_text); free(scope->baseline_text);
  scope->current = scope->gamepad_current = scope->selected_text = scope->baseline_text = NULL;
  if (scope->has_saved) qa_seat_settings_free(&scope->saved);
  scope->has_saved = false;
}
void qa_app_startup_input_profile_save(qa_app_startup_input_profile *p, const qa_app_frontend_preference_overrides *values,
                                       const char *const *history, size_t history_count, bool has_history) {
  save_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, save_scope_free, &scope);
  /* Controller bindings normalize to device 0 in the saved copy. */
  size_t count = 0;
  const qa_input_binding *current_bindings = qa_seat_input_bindings(p->input, &count);
  scope.bindings = calloc(count != 0 ? count : 1, sizeof *scope.bindings);
  if (scope.bindings == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    if (!qa_input_binding_copy(&scope.bindings[i], &current_bindings[i])) qa_range_error("Out of memory");
    scope.binding_count = i + 1;
    if (scope.bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON || scope.bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS)
      scope.bindings[i].input.device = 0;
  }
  scope.current = bindings_text(p->input);
  bool bindings_changed = strcmp(scope.current, p->baseline) != 0;
  scope.gamepad_current = gamepad_text(p->input);
  bool gamepad_changed = strcmp(scope.gamepad_current, p->gamepad_baseline) != 0;
  if (!bindings_changed && !gamepad_changed && override_count(values) == 0 && !has_history) {
    qa_cleanup_pop(&guard);
    save_scope_free(&scope);
    return;
  }
  scope.has_saved = qa_config_store_load_seat(&p->settings, "input/seat-1.json", &scope.saved);
  /* baseline = saved ?? { version: 1, gamepad, mouse: { ...defaultMouseTuning }, history: [], rumble: true,
   * controller: { kind: "automatic" }, bindings } (fields borrowed from saved or the scope). */
  qa_seat_settings baseline;
  if (scope.has_saved) baseline = scope.saved;
  else {
    memset(&baseline, 0, sizeof baseline);
    baseline.version = 1;
    baseline.gamepad = qa_seat_input_gamepad(p->input)->tuning;
    baseline.mouse = qa_default_mouse_tuning;
    baseline.rumble = true;
    baseline.rumble_strength = 1;
    baseline.rumble_strength_absent = true; /* the literal has no rumbleStrength; JSON.stringify leaves it out */
    baseline.controller.kind = QA_CONTROLLER_AUTOMATIC;
    baseline.bindings = scope.bindings;
    baseline.binding_count = scope.binding_count;
    /* The literal's key order: version, gamepad, mouse, history, rumble, controller, bindings. */
    static const uint8_t literal_keys[] = {QA_SEAT_KEY_VERSION, QA_SEAT_KEY_GAMEPAD, QA_SEAT_KEY_MOUSE, QA_SEAT_KEY_HISTORY,
                                           QA_SEAT_KEY_RUMBLE, QA_SEAT_KEY_CONTROLLER, QA_SEAT_KEY_BINDINGS};
    memcpy(baseline.key_order, literal_keys, sizeof literal_keys);
    baseline.key_count = (uint8_t)sizeof literal_keys;
  }
  qa_seat_settings merged = baseline;
  if (bindings_changed) { merged.bindings = scope.bindings; merged.binding_count = scope.binding_count; }
  if (gamepad_changed) merged.gamepad = qa_seat_input_gamepad(p->input)->tuning;
  qa_app_frontend_preference_overrides none;
  memset(&none, 0, sizeof none);
  qa_seat_settings selected = qa_app_frontend_seat_settings(values != NULL ? *values : none, &merged);
  if (has_history) {
    selected.history = (char **)history;
    selected.history_count = history_count;
  }
  bool differs = false;
  if (!bindings_changed && !gamepad_changed) {
    scope.selected_text = checked(qa_seat_settings_json(&selected));
    scope.baseline_text = checked(qa_seat_settings_json(&baseline));
    differs = strcmp(scope.selected_text, scope.baseline_text) != 0;
  }
  if ((bindings_changed || gamepad_changed || differs) && !qa_config_store_save_seat(&p->settings, "input/seat-1.json", &selected))
    qa_settings_raise_write_failure();
  if (bindings_changed && p->native != NULL) {
    /* await this.native.writeBindings(archivedBindingPairs(this.input), { local: this.bindLocal }) */
    qa_binding_pair *pairs = NULL;
    size_t pair_count = 0;
    qa_layer_pair *view = archived_pairs(current_bindings, count, &pairs, &pair_count);
    qa_native_config_write_bindings(&p->native->config, view, pair_count, NULL, p->bind_local);
    free(view);
    qa_binding_pairs_free(pairs, pair_count);
  }
  free(p->baseline);
  p->baseline = scope.current;
  scope.current = NULL;
  free(p->gamepad_baseline);
  p->gamepad_baseline = scope.gamepad_current;
  scope.gamepad_current = NULL;
  qa_cleanup_pop(&guard);
  save_scope_free(&scope);
}
