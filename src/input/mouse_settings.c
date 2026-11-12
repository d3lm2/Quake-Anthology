/* Port of src/input/mouse-settings.ts. GPL-2.0-or-later. */
#include "qa/input/mouse_settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/cvars/setting_declarations.h"
#include "qa/input/support.h"

static void register_unless_present(qa_cvar_registry *cvars, const char *name,
                                    const char *value, double flags) {
  qa_cvar_read found;
  bool present = qa_cvar_registry_find(cvars, name, &found);
  if (present) qa_cvar_read_free(&found);
  if (!present || qa_cvar_registry_is_console_created(cvars, name)) {
    qa_cvar_snapshot snapshot;
    memset(&snapshot, 0, sizeof snapshot);
    (void)qa_cvar_registry_register(cvars, name, value, flags, &snapshot);
    qa_cvar_read_free(&snapshot);
  }
}

/* The cvar registry borrows documentation (summary, usage, examples), so it
 * lives in static storage: "<name> [value]" usage texts and one example each. */
typedef struct declaration {
  const char *name;
  const char *summary;
  const char *usage;
  const char *const examples[1];
} declaration;

static const declaration DECLARATIONS[] = {
    {"sensitivity", "Mouse sensitivity multiplier for this seat.", "sensitivity [value]", {"sensitivity 4.5"}},
    {"cl_mouseAccel",
     "Mouse acceleration added per count per millisecond. Shared extension "
     "outside Quake III.",
     "cl_mouseAccel [value]", {"cl_mouseAccel 0"}},
    {"m_filter", "Mouse smoothing averages the current and previous mouse sample.", "m_filter [value]",
     {"m_filter 1"}},
    {"m_yaw", "Mouse horizontal turn scale in degrees per scaled count.", "m_yaw [value]", {"m_yaw 0.022"}},
    {"m_pitch",
     "Mouse vertical turn scale. Negative values enable Invert mouse in the "
     "menu.",
     "m_pitch [value]", {"m_pitch -0.022"}},
    {"m_side", "Mouse sideways movement scale while strafing.", "m_side [value]", {"m_side 0.8"}},
    {"m_forward", "Mouse forward movement scale when free look is disabled or strafing.", "m_forward [value]",
     {"m_forward 1"}},
    {"lookspring", "Start grounded pitch centering when mouse look is released.", "lookspring [value]",
     {"lookspring 1"}},
    {"lookstrafe", "Mouse horizontal motion strafes while holding mouse look.", "lookstrafe [value]",
     {"lookstrafe 1"}},
    {"freelook",
     "Mouse vertical motion controls view pitch without holding +mlook. Shared "
     "name across games.",
     "freelook [value]", {"freelook 1"}}};

void qa_mouse_settings_init(qa_mouse_settings *settings, qa_cvar_registry *cvars) {
  settings->cvars = cvars;
  const qa_mouse_tuning *defaults = &qa_default_mouse_tuning;
  static const struct {
    const char *name;
    const char *value;
  } center[] = {{"v_centerspeed", "500"}, {"v_centermove", "0.15"}};
  for (size_t i = 0; i < sizeof center / sizeof center[0]; i++) {
    register_unless_present(cvars, center[i].name, center[i].value, QA_CVAR_FLAG_NONE);
  }
  const double values[sizeof DECLARATIONS / sizeof DECLARATIONS[0]] = {
      defaults->sensitivity, defaults->acceleration, defaults->filter ? 1 : 0, defaults->yaw, defaults->pitch,
      defaults->side,        defaults->forward,      0,                       0,             defaults->free_look ? 1 : 0};
  for (size_t i = 0; i < sizeof DECLARATIONS / sizeof DECLARATIONS[0]; i++) {
    char *text = qa_input_js_number(values[i]);
    register_unless_present(cvars, DECLARATIONS[i].name, text != NULL ? text : "",
                            QA_CVAR_FLAG_ARCHIVE);
    free(text);
    qa_command_documentation documentation;
    memset(&documentation, 0, sizeof documentation);
    documentation.summary = DECLARATIONS[i].summary;
    documentation.usage = DECLARATIONS[i].usage;
    documentation.examples = DECLARATIONS[i].examples;
    documentation.example_count = 1;
    qa_cvar_registry_document(cvars, DECLARATIONS[i].name, documentation);
  }
  qa_register_setting_aliases(cvars, QA_ENGINE_SETTING_INPUT);
}

qa_mouse_tuning qa_mouse_settings_read(const qa_mouse_settings *settings) {
  qa_cvar_registry *cvars = settings->cvars;
  double pitch = qa_cvar_registry_variable_value(cvars, "m_pitch");
  bool filter;
  if (qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3) {
    qa_cvar_read found;
    memset(&found, 0, sizeof found);
    double integer = 0;
    if (qa_cvar_registry_find(cvars, "m_filter", &found)) {
      integer = found.integer_value;
      qa_cvar_read_free(&found);
    }
    filter = integer != 0;
  } else {
    filter = qa_cvar_registry_variable_value(cvars, "m_filter") != 0;
  }
  qa_mouse_tuning tuning;
  tuning.sensitivity = qa_cvar_registry_variable_value(cvars, "sensitivity");
  tuning.acceleration = qa_cvar_registry_variable_value(cvars, "cl_mouseAccel");
  tuning.filter = filter;
  tuning.yaw = qa_cvar_registry_variable_value(cvars, "m_yaw");
  tuning.pitch = fabs(pitch);
  /* pitch < 0 || Object.is(pitch, -0): a NaN with its sign bit set is neither. */
  tuning.invert_pitch = pitch < 0 || (pitch == 0 && signbit(pitch));
  tuning.side = qa_cvar_registry_variable_value(cvars, "m_side");
  tuning.forward = qa_cvar_registry_variable_value(cvars, "m_forward");
  tuning.free_look = qa_cvar_registry_variable_value(cvars, "freelook") != 0;
  tuning.look_spring = qa_cvar_registry_variable_value(cvars, "lookspring") != 0;
  tuning.look_strafe = qa_cvar_registry_variable_value(cvars, "lookstrafe") != 0;
  tuning.key_order = QA_MOUSE_KEYS_CVARS; /* read()'s object literal order */
  return tuning;
}

static void set_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  (void)qa_cvar_registry_set(cvars, name, value, false, &snapshot);
  qa_cvar_read_free(&snapshot);
}

static void set_number(qa_cvar_registry *cvars, const char *name, double value) {
  char *text = qa_input_js_number(value);
  set_cvar(cvars, name, text != NULL ? text : "");
  free(text);
}

static bool is_negative_zero(double value) { return value == 0 && signbit(value); }

void qa_mouse_settings_write(qa_mouse_settings *settings, qa_mouse_tuning value) {
  qa_cvar_registry *cvars = settings->cvars;
  set_number(cvars, "sensitivity", value.sensitivity);
  set_number(cvars, "cl_mouseAccel", value.acceleration);
  set_cvar(cvars, "m_filter", value.filter ? "1" : "0");
  if (is_negative_zero(value.yaw)) set_cvar(cvars, "m_yaw", "-0");
  else set_number(cvars, "m_yaw", value.yaw);
  double pitch = value.pitch * (value.invert_pitch ? -1 : 1);
  if (is_negative_zero(pitch)) set_cvar(cvars, "m_pitch", "-0");
  else set_number(cvars, "m_pitch", pitch);
  set_number(cvars, "m_side", value.side);
  set_number(cvars, "m_forward", value.forward);
  set_cvar(cvars, "lookspring", value.look_spring ? "1" : "0");
  set_cvar(cvars, "lookstrafe", value.look_strafe ? "1" : "0");
  set_cvar(cvars, "freelook", value.free_look ? "1" : "0");
}

static qa_mouse_tuning store_read(void *ctx) {
  return qa_mouse_settings_read((const qa_mouse_settings *)ctx);
}

static void store_write(void *ctx, qa_mouse_tuning value) {
  qa_mouse_settings_write((qa_mouse_settings *)ctx, value);
}

qa_mouse_tuning_store qa_mouse_settings_store(qa_mouse_settings *settings) {
  qa_mouse_tuning_store store = {settings, store_read, store_write};
  return store;
}
