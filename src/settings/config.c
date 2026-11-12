/* Port of src/settings/config.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/settings/config.h"

#include "qa/platform/files/contained.h"
#include "qa/network/common/js_number.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/core/commands/index.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"

/* Plain TS Errors abort with the identical message (see
 * src/core/cvars/index.c for the same local helper). */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static void *xalloc(void *pointer) {
  if (pointer == NULL) qa_error("Settings allocation failed");
  return pointer;
}

/* TS String(number): plain decimal in [1e-6, 1e21), exponent form
 * outside it. Finds the shortest round-tripping decimal in the
 * matching notation. */
static void js_number_text(double value, char *out, size_t size) {
  qa_net_js_number(value, out, size);
}

typedef struct text_buffer {
  char *data;
  size_t length;
  size_t capacity;
} text_buffer;
static bool text_add(text_buffer *buffer, const char *text) {
  size_t length = strlen(text);
  if (buffer->length + length + 1 > buffer->capacity) {
    size_t cap = buffer->capacity == 0 ? 256 : buffer->capacity;
    while (cap < buffer->length + length + 1) cap *= 2;
    char *data = (char *)realloc(buffer->data, cap);
    if (data == NULL) return false;
    buffer->data = data;
    buffer->capacity = cap;
  }
  memcpy(buffer->data + buffer->length, text, length + 1);
  buffer->length += length;
  return true;
}
static bool text_add_number(text_buffer *buffer, double value) {
  char text[40];
  js_number_text(value, text, sizeof text);
  return text_add(buffer, text);
}
/* JSON string literal. Bytes pass through unescaped except the JSON
 * structural escapes; TS strings are UTF-16, so lone surrogates in a
 * TS document would have been \udXXX escapes, which NUL-terminated
 * byte strings cannot represent. */
static bool text_add_json_string(text_buffer *buffer, const char *text) {
  if (!text_add(buffer, "\"")) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    char scratch[8];
    const char *piece = scratch;
    switch (*p) {
      case '"': piece = "\\\""; break;
      case '\\': piece = "\\\\"; break;
      case '\b': piece = "\\b"; break;
      case '\f': piece = "\\f"; break;
      case '\n': piece = "\\n"; break;
      case '\r': piece = "\\r"; break;
      case '\t': piece = "\\t"; break;
      default:
        if (*p < 0x20) snprintf(scratch, sizeof scratch, "\\u%04x", *p);
        else {
          scratch[0] = (char)*p;
          scratch[1] = '\0';
        }
        break;
    }
    if (!text_add(buffer, piece)) return false;
  }
  return text_add(buffer, "\"");
}

/* --- TS unknown-value readers (record/number/boolean/...) --- */
static bool is_undefined(const qa_save_value *value) {
  return value == NULL || value->kind == QA_SAVE_UNDEFINED;
}
static const qa_save_value *settings_record(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_OBJECT)
    qa_error("Expected a settings object");
  return value;
}
static const qa_save_value *field(const qa_save_value *object, const char *key) {
  return qa_save_value_get(object, key);
}
static double settings_number(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_NUMBER || !isfinite(value->number))
    qa_error("Expected a finite settings number");
  return value->number;
}
static double settings_rumble_strength(const qa_save_value *value) {
  double result = is_undefined(value) ? 1 : settings_number(value);
  if (result < 0 || result > 1)
    qa_error("Vibration strength must be between zero and one");
  return result;
}
static double settings_natural(const qa_save_value *value) {
  double result = settings_number(value);
  if (!(result == trunc(result) && fabs(result) <= 9007199254740991.0) || result < 0)
    qa_error("Expected a nonnegative settings integer");
  return result;
}
static bool settings_boolean(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_BOOLEAN)
    qa_error("Expected a settings boolean");
  return value->boolean;
}
static const char *settings_string(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_STRING || value->text == NULL)
    qa_error("Expected settings text");
  return value->text;
}
/* Console text (one byte per code unit: a bound command) crosses the JSON boundary as
 * the UTF-8 TS writes and reads (core/commands/text.h). A stored string with a character above U+00FF stays as its
 * UTF-8 bytes, as the cvar archives keep it. */
static char *console_text_of(const char *utf8) {
  char *bytes = NULL;
  if (qa_command_text_from_utf8(utf8, &bytes)) return bytes;
  return (char *)xalloc(dup_text(utf8));
}

static qa_stick_curve parse_curve(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  double deadzone = settings_number(field(input, "deadzone"));
  double exponent = settings_number(field(input, "exponent"));
  const qa_save_value *kind = field(input, "kind");
  qa_stick_curve curve;
  memset(&curve, 0, sizeof curve);
  curve.deadzone = deadzone;
  curve.exponent = exponent;
  curve.parsed_order = true;
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "radial") == 0) {
    curve.kind = QA_STICK_CURVE_RADIAL;
    curve.outer_threshold = settings_number(field(input, "outerThreshold"));
    return curve;
  }
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "axial") == 0) {
    curve.kind = QA_STICK_CURVE_AXIAL;
    return curve;
  }
  qa_error("Unknown stick curve");
}

/* TS yawAxis "y" | "z" -> qa_gyro_tuning yaw_axis_z. */
static bool parse_yaw_axis_z(const qa_save_value *value) {
  if (value != NULL && value->kind == QA_SAVE_STRING && value->text != NULL) {
    if (strcmp(value->text, "y") == 0) return false;
    if (strcmp(value->text, "z") == 0) return true;
  }
  qa_error("Unknown gyro yaw axis");
}

static qa_gamepad_tuning parse_gamepad(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *gyro = settings_record(field(input, "gyro"));
  bool yaw_axis_z = parse_yaw_axis_z(field(gyro, "yawAxis"));
  qa_gamepad_tuning tuning;
  memset(&tuning, 0, sizeof tuning);
  tuning.move = parse_curve(field(input, "move"));
  tuning.look = parse_curve(field(input, "look"));
  tuning.swap_sticks = settings_boolean(field(input, "swapSticks"));
  tuning.yaw_degrees_per_second = settings_number(field(input, "yawDegreesPerSecond"));
  tuning.pitch_degrees_per_second =
      settings_number(field(input, "pitchDegreesPerSecond"));
  tuning.invert_pitch = settings_boolean(field(input, "invertPitch"));
  tuning.forward_sensitivity = settings_number(field(input, "forwardSensitivity"));
  tuning.side_sensitivity = settings_number(field(input, "sideSensitivity"));
  tuning.trigger_threshold = settings_number(field(input, "triggerThreshold"));
  tuning.gyro.enabled = settings_boolean(field(gyro, "enabled"));
  tuning.gyro.yaw_axis_z = yaw_axis_z;
  tuning.gyro.yaw_sensitivity = settings_number(field(gyro, "yawSensitivity"));
  tuning.gyro.pitch_sensitivity = settings_number(field(gyro, "pitchSensitivity"));
  tuning.gyro.parsed_order = true;
  qa_validate_gamepad_tuning(&tuning);
  return tuning;
}

static qa_mouse_tuning parse_mouse(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  qa_mouse_tuning mouse;
  memset(&mouse, 0, sizeof mouse);
  mouse.sensitivity = settings_number(field(input, "sensitivity"));
  mouse.acceleration = settings_number(field(input, "acceleration"));
  mouse.filter = settings_boolean(field(input, "filter"));
  mouse.yaw = settings_number(field(input, "yaw"));
  mouse.pitch = settings_number(field(input, "pitch"));
  mouse.side = settings_number(field(input, "side"));
  mouse.forward = settings_number(field(input, "forward"));
  mouse.look_spring =
      is_undefined(field(input, "lookSpring")) ? false : settings_boolean(field(input, "lookSpring"));
  mouse.look_strafe =
      is_undefined(field(input, "lookStrafe")) ? false : settings_boolean(field(input, "lookStrafe"));
  mouse.free_look = settings_boolean(field(input, "freeLook"));
  mouse.invert_pitch = settings_boolean(field(input, "invertPitch"));
  mouse.key_order = QA_MOUSE_KEYS_PARSED;
  return mouse;
}

static qa_controller_axis parse_axis(const qa_save_value *value) {
  static const char *const names[] = {"left-x", "left-y", "right-x",
                                      "right-y", "left-trigger", "right-trigger"};
  if (value != NULL && value->kind == QA_SAVE_STRING && value->text != NULL)
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
      if (strcmp(value->text, names[i]) == 0) return names[i];
  qa_error("Unknown controller axis");
}

static qa_physical_input parse_physical(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *kind = field(input, "kind");
  qa_physical_input physical;
  memset(&physical, 0, sizeof physical);
  const char *name =
      (kind != NULL && kind->kind == QA_SAVE_STRING) ? kind->text : NULL;
  if (name != NULL && strcmp(name, "key") == 0) {
    physical.kind = QA_PHYSICAL_INPUT_KEY;
    physical.code = settings_natural(field(input, "code"));
    return physical;
  }
  if (name != NULL && strcmp(name, "mouse-button") == 0) {
    physical.kind = QA_PHYSICAL_INPUT_MOUSE_BUTTON;
    physical.button = settings_natural(field(input, "button"));
    return physical;
  }
  if (name != NULL && strcmp(name, "controller-button") == 0) {
    physical.kind = QA_PHYSICAL_INPUT_CONTROLLER_BUTTON;
    physical.device = settings_natural(field(input, "device"));
    physical.button = settings_natural(field(input, "button"));
    return physical;
  }
  if (name != NULL && strcmp(name, "controller-axis") == 0) {
    const qa_save_value *direction = field(input, "direction");
    const char *text = NULL;
    if (direction != NULL && direction->kind == QA_SAVE_STRING && direction->text != NULL) {
      if (strcmp(direction->text, "positive") == 0) text = "positive";
      if (strcmp(direction->text, "negative") == 0) text = "negative";
    }
    if (text == NULL) qa_error("Unknown axis direction");
    physical.kind = QA_PHYSICAL_INPUT_CONTROLLER_AXIS;
    physical.device = settings_natural(field(input, "device"));
    physical.axis = parse_axis(field(input, "axis"));
    physical.direction = text;
    return physical;
  }
  qa_error("Unknown binding input");
}

static qa_input_action parse_action(const qa_save_value *value) {
  static const char *const names[] = {"attack", "jump", "forward", "back",
                                      "move-left", "move-right", "move-up",
                                      "move-down", "use", "crouch", "walk",
                                      "scores", "next-weapon", "previous-weapon",
                                      "menu"};
  if (value != NULL && value->kind == QA_SAVE_STRING && value->text != NULL)
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
      if (strcmp(value->text, names[i]) == 0) return names[i];
  qa_error("Unknown input action");
}

static qa_input_binding_target parse_target(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *kind = field(input, "kind");
  qa_input_binding_target target;
  memset(&target, 0, sizeof target);
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "action") == 0) {
    target.is_action = true;
    target.action = parse_action(field(input, "action"));
    return target;
  }
  if (kind != NULL && kind->kind == QA_SAVE_STRING && kind->text != NULL &&
      strcmp(kind->text, "command") == 0) {
    target.is_action = false;
    target.text = console_text_of(settings_string(field(input, "text")));
    return target;
  }
  qa_error("Unknown binding target");
}

static bool is_hex_guid(const char *text, bool case_insensitive) {
  if (strlen(text) != 32) return false;
  for (int i = 0; i < 32; i++) {
    char c = text[i];
    bool digit = c >= '0' && c <= '9';
    bool lower = c >= 'a' && c <= 'f';
    bool upper = c >= 'A' && c <= 'F';
    if (!digit && !lower && !(case_insensitive && upper)) return false;
  }
  return true;
}

static qa_settings_controller_selection parse_controller(const qa_save_value *value) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *kind = field(input, "kind");
  qa_settings_controller_selection selection;
  memset(&selection, 0, sizeof selection);
  const char *name =
      (kind != NULL && kind->kind == QA_SAVE_STRING) ? kind->text : NULL;
  if (name != NULL && strcmp(name, "automatic") == 0) {
    selection.kind = QA_CONTROLLER_AUTOMATIC;
    return selection;
  }
  if (name != NULL && strcmp(name, "none") == 0) {
    selection.kind = QA_CONTROLLER_NONE;
    return selection;
  }
  const char *guid = settings_string(field(input, "guid"));
  if (!is_hex_guid(guid, true))
    qa_error("Controller GUID requires 32 hexadecimal digits");
  memcpy(selection.guid, guid, 33);
  if (name != NULL && strcmp(name, "device") == 0) {
    selection.kind = QA_CONTROLLER_DEVICE;
    selection.ordinal = settings_natural(field(input, "ordinal"));
    return selection;
  }
  if (name != NULL && strcmp(name, "serial") == 0) {
    selection.kind = QA_CONTROLLER_SERIAL;
    selection.serial = (char *)xalloc(dup_text(settings_string(field(input, "serial"))));
    return selection;
  }
  qa_error("Unknown controller selection");
}

static bool version_is_one(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_NUMBER && value->number == 1;
}

void qa_seat_settings_free(qa_seat_settings *settings) {
  /* NULL-safe arrays: a parse that raised part way leaves counts ahead of
   * the allocations. */
  if (settings->bindings != NULL)
    for (size_t i = 0; i < settings->binding_count; i++)
      free((void *)settings->bindings[i].target.text);
  free(settings->bindings);
  if (settings->history != NULL)
    for (size_t i = 0; i < settings->history_count; i++) free(settings->history[i]);
  free(settings->history);
  free(settings->controller.serial);
  memset(settings, 0, sizeof *settings);
}

void qa_parse_seat_settings(const qa_save_value *value, qa_seat_settings *out) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *bindings = field(input, "bindings");
  if (!version_is_one(field(input, "version")) || bindings == NULL ||
      bindings->kind != QA_SAVE_ARRAY)
    qa_error("Unsupported seat settings document");
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->binding_count = bindings->item_count;
  out->bindings = (qa_input_binding *)xalloc(
      calloc(out->binding_count == 0 ? 1 : out->binding_count, sizeof *out->bindings));
  for (size_t i = 0; i < out->binding_count; i++) {
    const qa_save_value *binding = settings_record(bindings->items[i]);
    out->bindings[i].input = parse_physical(field(binding, "input"));
    out->bindings[i].target = parse_target(field(binding, "target"));
  }
  out->has_always_run = !is_undefined(field(input, "alwaysRun"));
  out->always_run =
      out->has_always_run ? settings_boolean(field(input, "alwaysRun")) : false;
  out->gamepad = parse_gamepad(field(input, "gamepad"));
  out->mouse = parse_mouse(field(input, "mouse"));
  const qa_save_value *history = field(input, "history");
  if (history == NULL || history->kind != QA_SAVE_ARRAY)
    qa_error("Expected a settings list");
  out->history_count = history->item_count;
  out->history = (char **)xalloc(
      calloc(out->history_count == 0 ? 1 : out->history_count, sizeof *out->history));
  for (size_t i = 0; i < out->history_count; i++)
    out->history[i] = (char *)xalloc(dup_text(settings_string(history->items[i])));
  out->rumble = settings_boolean(field(input, "rumble"));
  out->rumble_strength = settings_rumble_strength(field(input, "rumbleStrength"));
  out->controller = parse_controller(field(input, "controller"));
}

void qa_parse_global_input_tuning(const qa_save_value *value,
                                  qa_global_input_tuning *out) {
  const qa_save_value *input = settings_record(value);
  if (!version_is_one(field(input, "version")))
    qa_error("Unsupported global input tuning document");
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->mouse = parse_mouse(field(input, "mouse"));
  out->has_always_run = !is_undefined(field(input, "alwaysRun"));
  out->always_run =
      out->has_always_run ? settings_boolean(field(input, "alwaysRun")) : false;
  out->rumble = settings_boolean(field(input, "rumble"));
  out->rumble_strength = settings_rumble_strength(field(input, "rumbleStrength"));
}

void qa_gyro_profile_free(qa_gyro_profile *profile) {
  free(profile->serial);
  memset(profile, 0, sizeof *profile);
}

void qa_parse_gyro_profile(const qa_save_value *value, qa_gyro_profile *out) {
  const qa_save_value *input = settings_record(value);
  const qa_save_value *identity = settings_record(field(input, "identity"));
  const qa_save_value *tuning = settings_record(field(input, "tuning"));
  if (!version_is_one(field(input, "version")))
    qa_error("Unsupported gyro settings version");
  memset(out, 0, sizeof *out);
  out->version = 1;
  const qa_save_value *kind = field(identity, "kind");
  const char *name =
      (kind != NULL && kind->kind == QA_SAVE_STRING) ? kind->text : NULL;
  if (name != NULL && strcmp(name, "seat") == 0) {
    out->identity_is_device = false;
  } else if (name != NULL && strcmp(name, "device") == 0) {
    const char *guid = settings_string(field(identity, "guid"));
    const char *serial = settings_string(field(identity, "serial"));
    /* TS serial.length counts UTF-16 units: one per UTF-8 lead byte, two for a 4-byte sequence. */
    size_t units = 0;
    for (const unsigned char *p = (const unsigned char *)serial; *p != '\0'; p++)
      if ((*p & 0xc0) != 0x80) units += *p >= 0xf0 ? 2 : 1;
    if (!is_hex_guid(guid, false) || serial[0] == '\0' || units > 256)
      qa_error("Invalid gyro device identity");
    out->identity_is_device = true;
    memcpy(out->guid, guid, 33);
    out->serial = (char *)xalloc(dup_text(serial));
  } else {
    qa_error("Unknown gyro profile identity");
  }
  out->tuning.yaw_axis_z = parse_yaw_axis_z(field(tuning, "yawAxis"));
  out->tuning.enabled = settings_boolean(field(tuning, "enabled"));
  out->tuning.yaw_sensitivity = settings_number(field(tuning, "yawSensitivity"));
  out->tuning.pitch_sensitivity = settings_number(field(tuning, "pitchSensitivity"));
}

/* --- settingsPath (node:path resolve/relative, POSIX lexical form) --- */
static char *normalize_absolute(const char *path) {
  /* Split on '/', dropping "" and ".", resolving ".." lexically. */
  size_t length = strlen(path);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, path, length + 1);
  char **parts = (char **)calloc(length / 2 + 2, sizeof *parts);
  char *out = (char *)malloc(length + 2);
  if (parts == NULL || out == NULL) {
    free(copy);
    free(parts);
    free(out);
    return NULL;
  }
  size_t count = 0;
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    if (*cursor == '/') *cursor++ = '\0';
    if (strcmp(start, ".") == 0) continue;
    if (strcmp(start, "..") == 0) {
      if (count > 0) count--;
      continue;
    }
    parts[count++] = start;
  }
  size_t used = 0;
  out[used++] = '/';
  for (size_t i = 0; i < count; i++) {
    size_t part_length = strlen(parts[i]);
    memcpy(out + used, parts[i], part_length);
    used += part_length;
    if (i + 1 < count) out[used++] = '/';
  }
  out[used] = '\0';
  free(copy);
  free(parts);
  return out;
}

static char *absolute_of(const char *path) {
  if (path[0] == '/') return normalize_absolute(path);
  char cwd[4096];
  if (getcwd(cwd, sizeof cwd) == NULL) return NULL;
  size_t length = strlen(cwd) + 1 + strlen(path) + 1;
  char *joined = (char *)malloc(length);
  if (joined == NULL) return NULL;
  snprintf(joined, length, "%s/%s", cwd, path);
  char *normalized = normalize_absolute(joined);
  free(joined);
  return normalized;
}

char *qa_settings_path(const char *root, const char *name) {
  char *resolved_root = absolute_of(root);
  if (resolved_root == NULL) return NULL;
  char *path = NULL;
  if (name[0] == '/') {
    path = normalize_absolute(name);
  } else {
    size_t length = strlen(resolved_root) + 1 + strlen(name) + 1;
    char *joined = (char *)malloc(length);
    if (joined != NULL) {
      snprintf(joined, length, "%s/%s", resolved_root, name);
      path = normalize_absolute(joined);
      free(joined);
    }
  }
  if (path == NULL) {
    free(resolved_root);
    return NULL;
  }
  size_t root_length = strlen(resolved_root);
  bool inside = strncmp(path, resolved_root, root_length) == 0 &&
                (root_length == 1 || path[root_length] == '/') &&
                path[root_length] != '\0';
  free(resolved_root);
  if (!inside) {
    free(path);
    qa_error("Settings path escapes the writable directory");
  }
  return path;
}

/* --- writeAtomic --- */
/* The Node fs error a failed writeAtomic step rejects with, kept for qa_settings_raise_write_failure. */
typedef struct write_failure {
  bool set;
  int code;
  const char *syscall; /* static */
  char *path, *dest;   /* owned; NULL when the TS error names none */
} write_failure;
static _Thread_local write_failure last_write_failure;
static void write_failure_clear(void) {
  free(last_write_failure.path);
  free(last_write_failure.dest);
  memset(&last_write_failure, 0, sizeof last_write_failure);
}
static void write_failure_note(int code, const char *syscall, const char *path, const char *dest) {
  write_failure_clear();
  last_write_failure.set = true;
  last_write_failure.code = code;
  last_write_failure.syscall = syscall;
  last_write_failure.path = path != NULL ? dup_text(path) : NULL;
  last_write_failure.dest = dest != NULL ? dup_text(dest) : NULL;
}
noreturn void qa_settings_raise_write_failure(void) {
  if (!last_write_failure.set) qa_range_error("Out of memory");
  write_failure failure = last_write_failure;
  memset(&last_write_failure, 0, sizeof last_write_failure);
  qa_cleanup path_guard, dest_guard;
  qa_cleanup_push(&path_guard, free, failure.path);
  qa_cleanup_push(&dest_guard, free, failure.dest);
  qa_node_fs_error_paths(failure.code, failure.syscall, failure.path, failure.dest);
}

/* node:path dirname(path) for the absolute or relative paths settingsPath gives. Malloc'd; NULL on OOM. */
static char *path_dirname(const char *path) {
  size_t length = strlen(path);
  while (length > 1 && path[length - 1] == '/') length--;
  size_t end = length;
  while (end > 0 && path[end - 1] != '/') end--;
  if (end == 0) return dup_text(".");
  while (end > 1 && path[end - 1] == '/') end--;
  char *out = (char *)malloc(end + 1);
  if (out == NULL) return NULL;
  memcpy(out, path, end);
  out[end] = '\0';
  return out;
}

/* Bun mkdir(path, { recursive: true }): an existing directory is success; a missing parent is created first and its
 * failure names the parent; any other failure names `path` itself. False with the failure noted. */
static bool make_directories(const char *path) {
  if (mkdir(path, 0777) == 0) return true;
  int code = errno;
  struct stat status;
  if (code == EEXIST) {
    if (stat(path, &status) == 0 && S_ISDIR(status.st_mode)) return true;
    write_failure_note(EEXIST, "mkdir", path, NULL);
    return false;
  }
  if (code == ENOENT) {
    char *parent = path_dirname(path);
    if (parent == NULL) return false;
    bool made = strcmp(parent, path) != 0 && make_directories(parent);
    bool root = strcmp(parent, path) == 0;
    free(parent);
    if (root) {
      write_failure_note(code, "mkdir", path, NULL);
      return false;
    }
    if (!made) return false;
    if (mkdir(path, 0777) == 0) return true;
    code = errno;
    if (code == EEXIST && stat(path, &status) == 0 && S_ISDIR(status.st_mode)) return true;
  }
  write_failure_note(code, "mkdir", path, NULL);
  return false;
}

/* crypto.randomUUID(): a version 4 UUID in lower-case hex. */
static void random_uuid(char out[37]) {
  unsigned char bytes[16];
  size_t got = 0;
  FILE *source = fopen("/dev/urandom", "rb");
  if (source != NULL) {
    got = fread(bytes, 1, sizeof bytes, source);
    fclose(source);
  }
  for (size_t i = got; i < sizeof bytes; i++) bytes[i] = (unsigned char)rand();
  bytes[6] = (unsigned char)((bytes[6] & 0x0f) | 0x40);
  bytes[8] = (unsigned char)((bytes[8] & 0x3f) | 0x80);
  static const char hex[] = "0123456789abcdef";
  size_t at = 0;
  for (size_t i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out[at++] = '-';
    out[at++] = hex[bytes[i] >> 4];
    out[at++] = hex[bytes[i] & 15];
  }
  out[at] = '\0';
}

/* TS writeAtomic: mkdir -p dirname(path); open `${path}.${randomUUID()}.tmp` "wx" 0600; write, sync, close; rename over
 * path; open and sync the directory; finally rm(temporary, { force: true }). False on any failure, which is noted for
 * qa_settings_raise_write_failure (none for an allocation failure). */
bool qa_write_atomic(const char *path, const void *contents, size_t length) {
  write_failure_clear();
  char *directory = path_dirname(path);
  if (directory == NULL) return false;
  if (!make_directories(directory)) {
    free(directory);
    return false;
  }
  size_t temporary_length = strlen(path) + 48;
  char *temporary = (char *)malloc(temporary_length);
  if (temporary == NULL) {
    free(directory);
    return false;
  }
  char uuid[37];
  random_uuid(uuid);
  snprintf(temporary, temporary_length, "%s.%s.tmp", path, uuid);
  bool ok = true;
  int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    write_failure_note(errno, "open", temporary, NULL);
    ok = false;
  }
  if (ok) {
    const uint8_t *bytes = (const uint8_t *)contents;
    size_t written = 0;
    while (ok && written < length) {
      ssize_t chunk = write(fd, bytes + written, length - written);
      if (chunk < 0 && errno == EINTR) continue;
      if (chunk <= 0) {
        write_failure_note(chunk < 0 ? errno : EIO, "write", NULL, NULL);
        ok = false;
        break;
      }
      written += (size_t)chunk;
    }
    if (ok && fsync(fd) != 0) { /* TS file.sync() */
      write_failure_note(errno, "fsync", NULL, NULL);
      ok = false;
    }
    if (close(fd) != 0 && ok) {
      write_failure_note(errno, "close", NULL, NULL);
      ok = false;
    }
  }
  if (ok && rename(temporary, path) != 0) {
    write_failure_note(errno, "rename", temporary, path);
    ok = false;
  }
  if (ok) {
    /* TS: open(dirname(path), "r") and directory.sync(), so the rename is durable. */
    int directory_fd = open(directory, O_RDONLY | O_CLOEXEC);
    if (directory_fd < 0) {
      write_failure_note(errno, "open", directory, NULL);
      ok = false;
    } else {
      if (fsync(directory_fd) != 0) {
        write_failure_note(errno, "fsync", NULL, NULL);
        ok = false;
      }
      close(directory_fd);
    }
  }
  unlink(temporary); /* the TS rm(force) finally; ENOENT after rename */
  free(temporary);
  free(directory);
  return ok;
}

bool qa_write_atomic_text(const char *path, const char *contents) {
  return qa_write_atomic(path, contents, strlen(contents));
}

/* --- JSON writers (JSON.stringify shapes; key order follows the TS
 * object literals in the parsers above) --- */
static bool add_curve(text_buffer *b, const qa_stick_curve *curve, bool pretty,
                      int depth);
static bool add_indent(text_buffer *b, bool pretty, int depth) {
  if (!pretty) return true;
  if (!text_add(b, "\n")) return false;
  for (int i = 0; i < depth * 2; i++)
    if (!text_add(b, " ")) return false;
  return true;
}
static bool add_key(text_buffer *b, const char *key, bool pretty, int depth,
                    bool first) {
  if (!first && !text_add(b, ",")) return false;
  if (!add_indent(b, pretty, depth)) return false;
  if (!text_add_json_string(b, key)) return false;
  return text_add(b, pretty ? ": " : ":");
}
static bool add_close(text_buffer *b, bool pretty, int depth, const char *close) {
  if (pretty && depth > 0 && !add_indent(b, pretty, depth - 1)) return false;
  return text_add(b, close);
}
static bool add_bool(text_buffer *b, bool value) {
  return text_add(b, value ? "true" : "false");
}
static bool add_curve(text_buffer *b, const qa_stick_curve *curve, bool pretty,
                      int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  bool radial = curve->kind == QA_STICK_CURVE_RADIAL;
  if (!text_add_json_string(b, radial ? "radial" : "axial")) return false;
  if (!add_key(b, "deadzone", pretty, depth, false)) return false;
  if (!text_add_number(b, curve->deadzone)) return false;
  /* defaultGamepadTuning's literal puts outerThreshold before exponent; the parser after. */
  if (radial && !curve->parsed_order) {
    if (!add_key(b, "outerThreshold", pretty, depth, false)) return false;
    if (!text_add_number(b, curve->outer_threshold)) return false;
  }
  if (!add_key(b, "exponent", pretty, depth, false)) return false;
  if (!text_add_number(b, curve->exponent)) return false;
  if (radial && curve->parsed_order) {
    if (!add_key(b, "outerThreshold", pretty, depth, false)) return false;
    if (!text_add_number(b, curve->outer_threshold)) return false;
  }
  return add_close(b, pretty, depth, "}");
}
/* `parsed`: the parser's order whatever the struct's provenance (a document saveGyro re-validates). */
static bool add_gyro(text_buffer *b, const qa_gyro_tuning *gyro, bool pretty,
                     int depth, bool parsed) {
  bool literal = !parsed && !gyro->parsed_order; /* defaultGamepadTuning.gyro: yawAxis last */
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "enabled", pretty, depth, true)) return false;
  if (!add_bool(b, gyro->enabled)) return false;
  if (!literal) {
    if (!add_key(b, "yawAxis", pretty, depth, false)) return false;
    if (!text_add_json_string(b, gyro->yaw_axis_z ? "z" : "y")) return false;
  }
  if (!add_key(b, "yawSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, gyro->yaw_sensitivity)) return false;
  if (!add_key(b, "pitchSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, gyro->pitch_sensitivity)) return false;
  if (literal) {
    if (!add_key(b, "yawAxis", pretty, depth, false)) return false;
    if (!text_add_json_string(b, gyro->yaw_axis_z ? "z" : "y")) return false;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_gamepad(text_buffer *b, const qa_gamepad_tuning *pad, bool pretty,
                        int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "move", pretty, depth, true)) return false;
  if (!add_curve(b, &pad->move, pretty, depth + 1)) return false;
  if (!add_key(b, "look", pretty, depth, false)) return false;
  if (!add_curve(b, &pad->look, pretty, depth + 1)) return false;
  if (!add_key(b, "swapSticks", pretty, depth, false)) return false;
  if (!add_bool(b, pad->swap_sticks)) return false;
  if (!add_key(b, "yawDegreesPerSecond", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->yaw_degrees_per_second)) return false;
  if (!add_key(b, "pitchDegreesPerSecond", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->pitch_degrees_per_second)) return false;
  if (!add_key(b, "invertPitch", pretty, depth, false)) return false;
  if (!add_bool(b, pad->invert_pitch)) return false;
  if (!add_key(b, "forwardSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->forward_sensitivity)) return false;
  if (!add_key(b, "sideSensitivity", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->side_sensitivity)) return false;
  if (!add_key(b, "triggerThreshold", pretty, depth, false)) return false;
  if (!text_add_number(b, pad->trigger_threshold)) return false;
  if (!add_key(b, "gyro", pretty, depth, false)) return false;
  if (!add_gyro(b, &pad->gyro, pretty, depth + 1, false)) return false;
  return add_close(b, pretty, depth, "}");
}
/* `order` is the object's key order (QA_MOUSE_KEYS_*). */
static bool add_mouse(text_buffer *b, const qa_mouse_tuning *mouse, bool pretty,
                      int depth, uint8_t order) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "sensitivity", pretty, depth, true)) return false;
  if (!text_add_number(b, mouse->sensitivity)) return false;
  if (!add_key(b, "acceleration", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->acceleration)) return false;
  if (!add_key(b, "filter", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->filter)) return false;
  if (!add_key(b, "yaw", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->yaw)) return false;
  if (!add_key(b, "pitch", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->pitch)) return false;
  if (order == QA_MOUSE_KEYS_CVARS) {
    if (!add_key(b, "invertPitch", pretty, depth, false)) return false;
    if (!add_bool(b, mouse->invert_pitch)) return false;
  }
  if (!add_key(b, "side", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->side)) return false;
  if (!add_key(b, "forward", pretty, depth, false)) return false;
  if (!text_add_number(b, mouse->forward)) return false;
  if (order != QA_MOUSE_KEYS_PARSED) {
    if (!add_key(b, "freeLook", pretty, depth, false)) return false;
    if (!add_bool(b, mouse->free_look)) return false;
  }
  if (!add_key(b, "lookSpring", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->look_spring)) return false;
  if (!add_key(b, "lookStrafe", pretty, depth, false)) return false;
  if (!add_bool(b, mouse->look_strafe)) return false;
  if (order == QA_MOUSE_KEYS_PARSED) {
    if (!add_key(b, "freeLook", pretty, depth, false)) return false;
    if (!add_bool(b, mouse->free_look)) return false;
  }
  if (order != QA_MOUSE_KEYS_CVARS) {
    if (!add_key(b, "invertPitch", pretty, depth, false)) return false;
    if (!add_bool(b, mouse->invert_pitch)) return false;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_physical(text_buffer *b, const qa_physical_input *input,
                         bool pretty, int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  switch (input->kind) {
    case QA_PHYSICAL_INPUT_KEY:
      if (!text_add_json_string(b, "key")) return false;
      if (!add_key(b, "code", pretty, depth, false)) return false;
      if (!text_add_number(b, input->code)) return false;
      break;
    case QA_PHYSICAL_INPUT_MOUSE_BUTTON:
      if (!text_add_json_string(b, "mouse-button")) return false;
      if (!add_key(b, "button", pretty, depth, false)) return false;
      if (!text_add_number(b, input->button)) return false;
      break;
    case QA_PHYSICAL_INPUT_CONTROLLER_BUTTON:
      if (!text_add_json_string(b, "controller-button")) return false;
      if (!add_key(b, "device", pretty, depth, false)) return false;
      if (!text_add_number(b, input->device)) return false;
      if (!add_key(b, "button", pretty, depth, false)) return false;
      if (!text_add_number(b, input->button)) return false;
      break;
    case QA_PHYSICAL_INPUT_CONTROLLER_AXIS:
      if (!text_add_json_string(b, "controller-axis")) return false;
      if (!add_key(b, "device", pretty, depth, false)) return false;
      if (!text_add_number(b, input->device)) return false;
      if (!add_key(b, "axis", pretty, depth, false)) return false;
      if (!text_add_json_string(b, input->axis)) return false;
      if (!add_key(b, "direction", pretty, depth, false)) return false;
      if (!text_add_json_string(b, input->direction)) return false;
      break;
  }
  return add_close(b, pretty, depth, "}");
}
static bool text_add_console_json_string(text_buffer *buffer, const char *console) {
  char *utf8 = qa_command_text_to_utf8(console);
  bool ok = text_add_json_string(buffer, utf8);
  free(utf8);
  return ok;
}
static bool add_binding_target(text_buffer *b, const qa_input_binding_target *target,
                               bool pretty, int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  if (target->is_action) {
    if (!text_add_json_string(b, "action")) return false;
    if (!add_key(b, "action", pretty, depth, false)) return false;
    if (!text_add_json_string(b, target->action)) return false;
  } else {
    if (!text_add_json_string(b, "command")) return false;
    if (!add_key(b, "text", pretty, depth, false)) return false;
    if (!text_add_console_json_string(b, target->text != NULL ? target->text : "")) return false;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_controller(text_buffer *b, const qa_settings_controller_selection *selection,
                           bool pretty, int depth) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", pretty, depth, true)) return false;
  switch (selection->kind) {
    case QA_CONTROLLER_AUTOMATIC:
      if (!text_add_json_string(b, "automatic")) return false;
      break;
    case QA_CONTROLLER_NONE:
      if (!text_add_json_string(b, "none")) return false;
      break;
    case QA_CONTROLLER_DEVICE:
      if (!text_add_json_string(b, "device")) return false;
      if (!add_key(b, "guid", pretty, depth, false)) return false;
      if (!text_add_json_string(b, selection->guid)) return false;
      if (!add_key(b, "ordinal", pretty, depth, false)) return false;
      if (!text_add_number(b, selection->ordinal)) return false;
      break;
    case QA_CONTROLLER_SERIAL:
      if (!text_add_json_string(b, "serial")) return false;
      if (!add_key(b, "guid", pretty, depth, false)) return false;
      if (!text_add_json_string(b, selection->guid)) return false;
      if (!add_key(b, "serial", pretty, depth, false)) return false;
      if (!text_add_json_string(b, selection->serial != NULL ? selection->serial : ""))
        return false;
      break;
  }
  return add_close(b, pretty, depth, "}");
}
static bool add_bindings_json(text_buffer *b, const qa_input_binding *bindings, size_t count, bool pretty, int depth) {
  if (!text_add(b, "[")) return false;
  for (size_t i = 0; i < count; i++) {
    if (i > 0 && !text_add(b, ",")) return false;
    if (!add_indent(b, pretty, depth)) return false;
    if (!text_add(b, "{")) return false;
    if (!add_key(b, "input", pretty, depth + 1, true)) return false;
    if (!add_physical(b, &bindings[i].input, pretty, depth + 2)) return false;
    if (!add_key(b, "target", pretty, depth + 1, false)) return false;
    if (!add_binding_target(b, &bindings[i].target, pretty, depth + 2)) return false;
    if (!add_close(b, pretty, depth + 1, "}")) return false;
  }
  if (count > 0 && !add_close(b, pretty, depth, "]")) return false;
  if (count == 0 && !text_add(b, "]")) return false;
  return true;
}
static bool add_seat_json_with(text_buffer *b, const qa_seat_settings *settings, bool pretty);
static bool add_seat_json(text_buffer *b, const qa_seat_settings *settings) { return add_seat_json_with(b, settings, true); }
/* The parser's key order (parseSeatSettings' object literal). */
static const uint8_t parsed_seat_keys[QA_SEAT_KEY_COUNT] = {
    QA_SEAT_KEY_VERSION, QA_SEAT_KEY_BINDINGS, QA_SEAT_KEY_ALWAYS_RUN, QA_SEAT_KEY_GAMEPAD, QA_SEAT_KEY_MOUSE,
    QA_SEAT_KEY_HISTORY, QA_SEAT_KEY_RUMBLE, QA_SEAT_KEY_RUMBLE_STRENGTH, QA_SEAT_KEY_CONTROLLER};
static bool seat_key_present(const qa_seat_settings *settings, uint8_t key) {
  if (key == QA_SEAT_KEY_ALWAYS_RUN) return settings->has_always_run;
  if (key == QA_SEAT_KEY_RUMBLE_STRENGTH) return !settings->rumble_strength_absent;
  return key < QA_SEAT_KEY_COUNT;
}
void qa_seat_settings_note_key(qa_seat_settings *settings, qa_seat_settings_key key) {
  if (settings->key_count == 0) {
    for (size_t i = 0; i < QA_SEAT_KEY_COUNT; i++)
      if (seat_key_present(settings, parsed_seat_keys[i])) settings->key_order[settings->key_count++] = parsed_seat_keys[i];
  }
  for (size_t i = 0; i < settings->key_count; i++)
    if (settings->key_order[i] == key) return;
  if (settings->key_count < QA_SEAT_KEY_COUNT) settings->key_order[settings->key_count++] = (uint8_t)key;
}
static bool add_seat_json_with(text_buffer *b, const qa_seat_settings *settings, bool pretty) {
  if (!text_add(b, "{")) return false;
  const uint8_t *order = settings->key_count == 0 ? parsed_seat_keys : settings->key_order;
  size_t count = settings->key_count == 0 ? QA_SEAT_KEY_COUNT : settings->key_count;
  bool first = true;
  for (size_t k = 0; k < count; k++) {
    uint8_t key = order[k];
    if (!seat_key_present(settings, key)) continue;
    switch (key) {
      case QA_SEAT_KEY_VERSION:
        if (!add_key(b, "version", pretty, 1, first) || !text_add_number(b, settings->version)) return false;
        break;
      case QA_SEAT_KEY_BINDINGS:
        if (!add_key(b, "bindings", pretty, 1, first)) return false;
        if (!add_bindings_json(b, settings->bindings, settings->binding_count, pretty, 2)) return false;
        break;
      case QA_SEAT_KEY_ALWAYS_RUN:
        if (!add_key(b, "alwaysRun", pretty, 1, first) || !add_bool(b, settings->always_run)) return false;
        break;
      case QA_SEAT_KEY_GAMEPAD:
        if (!add_key(b, "gamepad", pretty, 1, first) || !add_gamepad(b, &settings->gamepad, pretty, 2)) return false;
        break;
      case QA_SEAT_KEY_MOUSE:
        if (!add_key(b, "mouse", pretty, 1, first) || !add_mouse(b, &settings->mouse, pretty, 2, settings->mouse.key_order)) return false;
        break;
      case QA_SEAT_KEY_HISTORY:
        if (!add_key(b, "history", pretty, 1, first)) return false;
        if (!text_add(b, "[")) return false;
        for (size_t i = 0; i < settings->history_count; i++) {
          if (i > 0 && !text_add(b, ",")) return false;
          if (!add_indent(b, pretty, 2)) return false;
          if (!text_add_json_string(b, settings->history[i])) return false;
        }
        if (settings->history_count > 0 && !add_close(b, pretty, 2, "]")) return false;
        if (settings->history_count == 0 && !text_add(b, "]")) return false;
        break;
      case QA_SEAT_KEY_RUMBLE:
        if (!add_key(b, "rumble", pretty, 1, first) || !add_bool(b, settings->rumble)) return false;
        break;
      case QA_SEAT_KEY_RUMBLE_STRENGTH:
        if (!add_key(b, "rumbleStrength", pretty, 1, first) || !text_add_number(b, settings->rumble_strength)) return false;
        break;
      case QA_SEAT_KEY_CONTROLLER:
        if (!add_key(b, "controller", pretty, 1, first) || !add_controller(b, &settings->controller, pretty, 2)) return false;
        break;
      default:
        continue;
    }
    first = false;
  }
  if (first) return text_add(b, "}");
  return add_close(b, pretty, 1, "}");
}
/* Compact JSON.stringify texts (key order follows the parsers). */
char *qa_seat_settings_json(const qa_seat_settings *settings) {
  text_buffer buffer = {NULL, 0, 0};
  if (!add_seat_json_with(&buffer, settings, false)) { free(buffer.data); return NULL; }
  return buffer.data;
}
char *qa_input_bindings_json(const qa_input_binding *bindings, size_t count) {
  text_buffer buffer = {NULL, 0, 0};
  if (!add_bindings_json(&buffer, bindings, count, false, 0)) { free(buffer.data); return NULL; }
  return buffer.data;
}
char *qa_gamepad_tuning_json(const qa_gamepad_tuning *tuning) {
  text_buffer buffer = {NULL, 0, 0};
  if (!add_gamepad(&buffer, tuning, false, 0)) { free(buffer.data); return NULL; }
  return buffer.data;
}
static bool add_global_json(text_buffer *b, const qa_global_input_tuning *tuning) {
  const bool pretty = true;
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "version", pretty, 1, true)) return false;
  if (!text_add_number(b, tuning->version)) return false;
  if (!add_key(b, "mouse", pretty, 1, false)) return false;
  if (!add_mouse(b, &tuning->mouse, pretty, 2, QA_MOUSE_KEYS_PARSED)) return false; /* parseGlobalInputTuning re-validates */
  if (tuning->has_always_run) {
    if (!add_key(b, "alwaysRun", pretty, 1, false)) return false;
    if (!add_bool(b, tuning->always_run)) return false;
  }
  if (!add_key(b, "rumble", pretty, 1, false)) return false;
  if (!add_bool(b, tuning->rumble)) return false;
  if (!add_key(b, "rumbleStrength", pretty, 1, false)) return false;
  if (!text_add_number(b, tuning->rumble_strength)) return false;
  return add_close(b, pretty, 1, "}");
}
static bool add_gyro_json(text_buffer *b, const qa_gyro_profile *profile) {
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "version", false, 1, true)) return false;
  if (!text_add_number(b, profile->version)) return false;
  if (!add_key(b, "identity", false, 1, false)) return false;
  if (!text_add(b, "{")) return false;
  if (!add_key(b, "kind", false, 2, true)) return false;
  if (profile->identity_is_device) {
    if (!text_add_json_string(b, "device")) return false;
    if (!add_key(b, "guid", false, 2, false)) return false;
    if (!text_add_json_string(b, profile->guid)) return false;
    if (!add_key(b, "serial", false, 2, false)) return false;
    if (!text_add_json_string(b, profile->serial != NULL ? profile->serial : ""))
      return false;
  } else {
    if (!text_add_json_string(b, "seat")) return false;
  }
  if (!add_close(b, false, 2, "}")) return false;
  if (!add_key(b, "tuning", false, 1, false)) return false;
  if (!add_gyro(b, &profile->tuning, false, 2, true)) return false; /* parseGyroProfile re-validates */
  return add_close(b, false, 1, "}");
}

/* --- ConfigStore --- */
bool qa_config_store_init(qa_config_store *store, const char *root) {
  return qa_config_store_init_legacy(store, root, NULL);
}
bool qa_config_store_init_legacy(qa_config_store *store, const char *root,
                                 const qa_config_store *legacy) {
  store->root = dup_text(root);
  store->legacy = legacy;
  return store->root != NULL;
}
void qa_config_store_destroy(qa_config_store *store) {
  free(store->root);
  memset(store, 0, sizeof *store);
}

/* Bun.file(path).text() when it exists, else the TS null. Bytes are
 * kept as-is (Bun decodes UTF-8 lossily; qa_parse_json_value is
 * equally lenient, and malformed bytes still fail JSON parsing). */
/* TS (await Bun.file(path).exists()) ? file.text() : absent. exists() is false for a missing path and for a directory
 * (NULL here); a file that exists but cannot be opened rejects with the Node error. */
static char *read_file_bytes(const char *path, size_t *out_length) {
  struct stat status;
  if (stat(path, &status) != 0 || S_ISDIR(status.st_mode)) return NULL;
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    if (errno == ENOENT) return NULL;
    qa_node_fs_error(errno, "open", path);
  }
  size_t capacity = 4096;
  size_t length = 0;
  char *data = (char *)malloc(capacity);
  if (data == NULL) {
    fclose(file);
    return NULL;
  }
  for (;;) {
    if (length + 2048 + 1 > capacity) {
      capacity *= 2;
      char *grown = (char *)realloc(data, capacity);
      if (grown == NULL) {
        free(data);
        fclose(file);
        return NULL;
      }
      data = grown;
    }
    size_t read = fread(data + length, 1, 2048, file);
    length += read;
    if (read < 2048) break;
  }
  fclose(file);
  data[length] = '\0';
  /* Bun's file.text() decodes as TextDecoder does: one leading byte-order mark is dropped. */
  if (length >= 3 && (unsigned char)data[0] == 0xef && (unsigned char)data[1] == 0xbb &&
      (unsigned char)data[2] == 0xbf) {
    memmove(data, data + 3, length - 2);
    length -= 3;
  }
  if (out_length != NULL) *out_length = length;
  return data;
}

char *qa_config_store_load_text(const qa_config_store *store, const char *name,
                                size_t *out_length) {
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return NULL;
  qa_cleanup path_guard;
  qa_cleanup_push(&path_guard, free, path);
  char *text = read_file_bytes(path, out_length);
  qa_cleanup_pop(&path_guard);
  free(path);
  /* TS: (await file.exists()) ? file.text() : (this.legacy?.loadText(name) ?? null). */
  if (text == NULL && store->legacy != NULL) return qa_config_store_load_text(store->legacy, name, out_length);
  return text;
}

bool qa_config_store_dump(const qa_config_store *store, const char *name,
                          const char *contents) {
  write_failure_clear();
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return false;
  bool ok = qa_write_atomic_text(path, contents);
  free(path);
  return ok;
}

/* ConfigStore.parsed: a document that is not JSON at all behaves
 * like the absent one; parsed values still face validation. Returns
 * NULL for the TS null (undecodable or JSON null). */
static qa_save_value *parsed_document(const char *text, size_t length) {
  qa_save_value *value =
      qa_parse_json_value((const uint8_t *)text, length, true);
  if (value != NULL && value->kind == QA_SAVE_NULL) {
    qa_save_value_free(value);
    return NULL;
  }
  return value;
}

/* TS ConfigStore.decoded: decode and validate one stored document. One that
 * cannot be read is renamed to `.bad` so the next save does not erase it,
 * and loads as absent (false). parse fills *out (zeroed first); free_out
 * releases a partial *out when parse raises. Takes ownership of text. */
typedef struct decode_job {
  const qa_save_value *value;
  void (*parse)(const qa_save_value *value, void *out);
  void *out;
  void (*free_out)(void *out);
} decode_job;
static void decode_run(void *ctx) {
  decode_job *job = (decode_job *)ctx;
  qa_cleanup node;
  if (job->free_out != NULL) qa_cleanup_push(&node, job->free_out, job->out);
  job->parse(job->value, job->out);
  if (job->free_out != NULL) qa_cleanup_pop(&node);
}
static bool config_decoded(const qa_config_store *store, const char *name,
                           char *text, size_t length,
                           void (*parse)(const qa_save_value *, void *),
                           void *out, size_t out_size,
                           void (*free_out)(void *)) {
  qa_save_value *value = parsed_document(text, length);
  free(text);
  if (value != NULL) {
    memset(out, 0, out_size);
    decode_job job = {value, parse, out, free_out};
    qa_caught_error caught;
    const bool parsed = qa_try(decode_run, &job, &caught);
    qa_save_value_free(value);
    if (parsed) return true;
    /* quarantined below like a document that is not JSON */
    memset(out, 0, out_size);
  }
  char *path = qa_settings_path(store->root, name);
  if (path != NULL) {
    size_t path_length = strlen(path);
    char *bad = (char *)malloc(path_length + 5);
    if (bad != NULL) {
      memcpy(bad, path, path_length);
      memcpy(bad + path_length, ".bad", 5);
      /* the document may already be gone or read-only; defaults still apply */
      (void)rename(path, bad);
      free(bad);
    }
    free(path);
  }
  return false;
}
static void parse_gyro_job(const qa_save_value *value, void *out) {
  qa_parse_gyro_profile(value, (qa_gyro_profile *)out);
}
static void free_gyro_job(void *out) { qa_gyro_profile_free((qa_gyro_profile *)out); }
static void parse_seat_job(const qa_save_value *value, void *out) {
  qa_parse_seat_settings(value, (qa_seat_settings *)out);
}
static void free_seat_job(void *out) { qa_seat_settings_free((qa_seat_settings *)out); }
static void parse_tuning_job(const qa_save_value *value, void *out) {
  qa_parse_global_input_tuning(value, (qa_global_input_tuning *)out);
}
typedef struct input_routing_result {
  double seat;
  bool has_seat;
} input_routing_result;
static void parse_input_routing_job(const qa_save_value *value, void *out) {
  input_routing_result *result = (input_routing_result *)out;
  const qa_save_value *data = settings_record(value);
  if (!version_is_one(field(data, "version")))
    qa_error("Unsupported input routing document");
  const qa_save_value *seat = field(data, "keyboardSeat");
  if (seat != NULL && seat->kind == QA_SAVE_NULL) {
    result->has_seat = false;
    result->seat = 0;
  } else {
    result->seat = settings_natural(seat);
    result->has_seat = true;
  }
}

bool qa_config_store_load_gyro(const qa_config_store *store, const char *name,
                               qa_gyro_profile *out) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL || length > 8192) {
    free(text);
    return false;
  }
  return config_decoded(store, name, text, length, parse_gyro_job, out,
                        sizeof *out, free_gyro_job);
}

bool qa_config_store_save_gyro(const qa_config_store *store, const char *name,
                               const qa_gyro_profile *profile) {
  write_failure_clear();
  /* TS saveGyro re-validates via parseGyroProfile(profile). */
  if (profile->version != 1) qa_error("Unsupported gyro settings version");
  if (profile->identity_is_device) {
    const char *serial = profile->serial != NULL ? profile->serial : "";
    if (!is_hex_guid(profile->guid, false) || serial[0] == '\0' ||
        strlen(serial) > 256)
      qa_error("Invalid gyro device identity");
  }
  /* yaw_axis_z is a bool: the TS "Unknown gyro yaw axis" case is
   * unrepresentable here. */
  if (!isfinite(profile->tuning.yaw_sensitivity) ||
      !isfinite(profile->tuning.pitch_sensitivity))
    qa_error("Expected a finite settings number");
  text_buffer buffer = {NULL, 0, 0};
  if (!add_gyro_json(&buffer, profile) || !text_add(&buffer, "\n")) {
    free(buffer.data);
    return false;
  }
  bool ok = qa_config_store_dump(store, name, buffer.data);
  free(buffer.data);
  return ok;
}

bool qa_config_store_save_input_routing(const qa_config_store *store,
                                        const char *name, double keyboard_seat,
                                        bool has_keyboard_seat) {
  write_failure_clear();
  if (has_keyboard_seat &&
      (!(keyboard_seat == trunc(keyboard_seat) &&
         fabs(keyboard_seat) <= 9007199254740991.0) ||
       keyboard_seat < 0 || !isfinite(keyboard_seat)))
    qa_error("Expected a nonnegative settings integer");
  text_buffer buffer = {NULL, 0, 0};
  bool built = text_add(&buffer, "{\"version\":1,\"keyboardSeat\":");
  if (built && has_keyboard_seat) built = text_add_number(&buffer, keyboard_seat);
  if (built && !has_keyboard_seat) built = text_add(&buffer, "null");
  if (built) built = text_add(&buffer, "}\n");
  if (!built) {
    free(buffer.data);
    return false;
  }
  bool ok = qa_config_store_dump(store, name, buffer.data);
  free(buffer.data);
  return ok;
}

bool qa_config_store_load_input_routing(const qa_config_store *store,
                                        const char *name, double *out_seat,
                                        bool *out_has_seat) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL) return false;
  input_routing_result result;
  if (!config_decoded(store, name, text, length, parse_input_routing_job,
                      &result, sizeof result, NULL))
    return false;
  *out_seat = result.seat;
  *out_has_seat = result.has_seat;
  return true;
}

bool qa_config_store_save_seat(const qa_config_store *store, const char *name,
                               const qa_seat_settings *settings) {
  write_failure_clear();
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return false;
  text_buffer buffer = {NULL, 0, 0};
  bool ok = add_seat_json(&buffer, settings) && text_add(&buffer, "\n") &&
            qa_write_atomic_text(path, buffer.data);
  free(buffer.data);
  free(path);
  return ok;
}

bool qa_config_store_load_seat(const qa_config_store *store, const char *name,
                               qa_seat_settings *out) {
  size_t length = 0;
  /* TS loadSeat reads this root only; an absent document is the legacy
   * store's loadSeat, which decodes (and quarantines) in its own root. */
  char *path = qa_settings_path(store->root, name);
  char *text = NULL;
  if (path != NULL) {
    qa_cleanup path_guard;
    qa_cleanup_push(&path_guard, free, path);
    text = read_file_bytes(path, &length);
    qa_cleanup_pop(&path_guard);
  }
  free(path);
  if (text == NULL) return store->legacy != NULL && qa_config_store_load_seat(store->legacy, name, out);
  return config_decoded(store, name, text, length, parse_seat_job, out,
                        sizeof *out, free_seat_job);
}

bool qa_config_store_load_global_tuning(const qa_config_store *store,
                                        const char *name,
                                        qa_global_input_tuning *out) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, name, &length);
  if (text == NULL) return false;
  return config_decoded(store, name, text, length, parse_tuning_job, out,
                        sizeof *out, NULL);
}

bool qa_config_store_save_global_tuning(const qa_config_store *store,
                                        const char *name,
                                        const qa_global_input_tuning *tuning) {
  write_failure_clear();
  /* TS saveGlobalTuning re-validates via parseGlobalInputTuning. */
  if (tuning->version != 1) qa_error("Unsupported global input tuning document");
  const qa_mouse_tuning *mouse = &tuning->mouse;
  const double numbers[] = {mouse->sensitivity, mouse->acceleration, mouse->yaw,
                            mouse->pitch, mouse->side, mouse->forward};
  for (size_t i = 0; i < sizeof numbers / sizeof *numbers; i++)
    if (!isfinite(numbers[i])) qa_error("Expected a finite settings number");
  if (!isfinite(tuning->rumble_strength))
    qa_error("Expected a finite settings number");
  if (tuning->rumble_strength < 0 || tuning->rumble_strength > 1)
    qa_error("Vibration strength must be between zero and one");
  text_buffer buffer = {NULL, 0, 0};
  if (!add_global_json(&buffer, tuning) || !text_add(&buffer, "\n")) {
    free(buffer.data);
    return false;
  }
  bool ok = qa_config_store_dump(store, name, buffer.data);
  free(buffer.data);
  return ok;
}

bool qa_config_store_save_cvars(const qa_config_store *store, const char *name,
                                qa_cvar_registry *cvars,
                                const char *const *bindings,
                                size_t binding_count) {
  write_failure_clear();
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) return false;
  size_t command_count = 0;
  char **commands =
      qa_cvar_registry_archive_commands(cvars, NULL, NULL, &command_count);
  text_buffer buffer = {NULL, 0, 0};
  bool ok = text_add(&buffer, "// Generated by quake-typescript\n");
  bool first = true;
  for (size_t i = 0; ok && i < binding_count; i++) {
    if (!first) ok = text_add(&buffer, "\n");
    if (ok) ok = text_add(&buffer, bindings[i]);
    first = false;
  }
  for (size_t i = 0; ok && i < command_count; i++) {
    if (!first) ok = text_add(&buffer, "\n");
    if (ok) ok = text_add(&buffer, commands[i]);
    first = false;
  }
  if (ok) ok = text_add(&buffer, "\n");
  if (ok) ok = qa_write_atomic_text(path, buffer.data);
  free(buffer.data);
  for (size_t i = 0; i < command_count; i++) free(commands[i]);
  free(commands);
  free(path);
  return ok;
}

void qa_config_store_execute(const qa_config_store *store, const char *name,
                             qa_command_buffer *commands,
                             qa_command_context context) {
  char *path = qa_settings_path(store->root, name);
  if (path == NULL) qa_error("Settings script path could not be allocated");
  /* TS readFile(path, "utf8"): the bytes as they are (a byte-order mark stays), and a missing or
   * unreadable file rejects with the Node fs error. */
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    int code = errno;
    qa_cleanup path_node;
    qa_cleanup_push(&path_node, free, path);
    qa_node_fs_error(code, "open", path);
  }
  size_t length = 0, capacity = 4096;
  char *text = (char *)malloc(capacity);
  while (text != NULL) {
    if (length + 1 >= capacity) {
      char *grown = (char *)realloc(text, capacity * 2);
      if (grown == NULL) { free(text); text = NULL; break; }
      text = grown;
      capacity *= 2;
    }
    size_t read = fread(text + length, 1, capacity - length - 1, file);
    length += read;
    if (read == 0) break;
  }
  fclose(file);
  free(path);
  if (text == NULL) qa_error("Settings allocation failed");
  text[length] = '\0';
  text_buffer buffer = {NULL, 0, 0};
  if (!text_add(&buffer, text) || !text_add(&buffer, "\n")) {
    free(text);
    free(buffer.data);
    qa_error("Settings allocation failed");
  }
  free(text);
  qa_command_origin origin;
  memset(&origin, 0, sizeof origin);
  origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  origin.name = name;
  origin.caller = &context.origin;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = context.session;
  source.origin = origin;
  qa_command_buffer_append(commands, buffer.data, &source, -1);
  free(buffer.data);
}
