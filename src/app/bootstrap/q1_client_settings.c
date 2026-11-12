/* Port of src/app/bootstrap/q1-client-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_client_settings.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
typedef struct size_ctx { qa_command_buffer *commands; double step; } size_ctx;
struct qa_app_q1_view_commands {
  qa_command_buffer *commands;
  const char *names[4];
  size_t count;
  /* Each registration's closures capture its own command buffer (TS closes over `commands`). */
  size_ctx sizeup, sizedown;
};
static void size_handler(void *v, qa_command_invocation *command) {
  size_ctx *ctx = v;
  qa_cvar_read size;
  if (qa_command_buffer_find_cvar(ctx->commands, "viewsize", &command->source, &size)) {
    char text[64];
    double next = size.numeric_value + ctx->step;
    snprintf(text, sizeof text, "viewsize %s\n", QA_JS_NUMBER(next));
    qa_cvar_read_free(&size);
    qa_command_invocation_insert(command, text);
  }
}
static size_t arg_count(const qa_command_invocation *command) {
  return command->argc > 0 ? command->argc - 1 : 0;
}
static const char *arg_at(const qa_command_invocation *command, size_t index) {
  return index < arg_count(command) ? command->argv[1 + index] : "";
}
static void name_handler(void *v, qa_command_invocation *command) {
  (void)v;
  if (arg_count(command) == 0) { qa_command_invocation_execute_now(command, "_cl_name"); return; }
  const char *source = arg_count(command) == 1 ? arg_at(command, 0) : command->args_text;
  /* .slice(0, 15) counts UTF-16 units (a surrogate pair cut in half is dropped whole), then
   * .replace(/["\n\r]/g, ""). */
  const unsigned char *p = (const unsigned char *)source;
  size_t units = 0, end = 0;
  while (p[end] != '\0') {
    size_t width = p[end] >= 0xf0 ? 4 : p[end] >= 0xe0 ? 3 : p[end] >= 0xc0 ? 2 : 1;
    size_t cost = width == 4 ? 2 : 1;
    if (units + cost > 15) break;
    for (size_t i = 1; i < width; i++) if (p[end + i] == '\0') { width = i; break; }
    units += cost; end += width;
  }
  char *text = malloc(end + 12);
  if (text == NULL) qa_range_error("Out of memory");
  size_t at = (size_t)snprintf(text, end + 12, "_cl_name \"");
  for (size_t i = 0; i < end; i++)
    if (source[i] != '"' && source[i] != '\n' && source[i] != '\r') text[at++] = source[i];
  text[at++] = '"';
  text[at] = '\0';
  qa_cleanup node;
  qa_cleanup_push(&node, free, text);
  qa_command_invocation_execute_now(command, text);
  qa_cleanup_pop(&node);
  free(text);
}
static int color_component(const char *text) {
  int value = qa_native_atoi(text) & 15;
  return value < 13 ? value : 13;
}
static void color_netquake_handler(void *v, qa_command_invocation *command) {
  (void)v;
  if (arg_count(command) == 0) { qa_command_invocation_execute_now(command, "_cl_color"); return; }
  int top = color_component(arg_at(command, 0));
  int bottom = color_component(arg_count(command) > 1 ? arg_at(command, 1) : arg_at(command, 0));
  char text[32];
  snprintf(text, sizeof text, "_cl_color %d", top * 16 + bottom);
  qa_command_invocation_execute_now(command, text);
}
static void color_qw_handler(void *v, qa_command_invocation *command) {
  (void)v;
  if (arg_count(command) == 0) {
    qa_command_invocation_execute_now(command, "topcolor");
    qa_command_invocation_execute_now(command, "bottomcolor");
    return;
  }
  int top = color_component(arg_at(command, 0));
  int bottom = color_component(arg_count(command) > 1 ? arg_at(command, 1) : arg_at(command, 0));
  char text[32];
  snprintf(text, sizeof text, "topcolor %d", top);
  qa_command_invocation_execute_now(command, text);
  snprintf(text, sizeof text, "bottomcolor %d", bottom);
  qa_command_invocation_execute_now(command, text);
}
qa_app_q1_view_commands *qa_app_register_q1_view_commands(qa_command_buffer *commands) {
  qa_app_q1_view_commands *registration = calloc(1, sizeof *registration);
  if (registration == NULL) qa_range_error("Out of memory");
  registration->commands = commands;
  qa_command_dialect dialect = qa_command_buffer_dialect(commands);
  if (dialect != QA_COMMAND_DIALECT_Q1_NETQUAKE && dialect != QA_COMMAND_DIALECT_Q1_QUAKEWORLD) return registration;
  registration->sizeup.commands = commands; registration->sizeup.step = 10;
  registration->sizedown.commands = commands; registration->sizedown.step = -10;
  if (!qa_command_buffer_exists(commands, "sizeup")
      && qa_command_buffer_register(commands, "sizeup", size_handler, &registration->sizeup, NULL, QA_COMMAND_WORLD))
    registration->names[registration->count++] = "sizeup";
  if (!qa_command_buffer_exists(commands, "sizedown")
      && qa_command_buffer_register(commands, "sizedown", size_handler, &registration->sizedown, NULL, QA_COMMAND_WORLD))
    registration->names[registration->count++] = "sizedown";
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    qa_cvar_read probe;
    bool has_name_cvar = qa_command_buffer_find_cvar(commands, "name", NULL, &probe);
    if (has_name_cvar) qa_cvar_read_free(&probe);
    if (!qa_command_buffer_exists(commands, "name") && !has_name_cvar
        && qa_command_buffer_register(commands, "name", name_handler, NULL, NULL, QA_COMMAND_WORLD))
      registration->names[registration->count++] = "name";
    bool has_color_cvar = qa_command_buffer_find_cvar(commands, "color", NULL, &probe);
    if (has_color_cvar) qa_cvar_read_free(&probe);
    if (!qa_command_buffer_exists(commands, "color") && !has_color_cvar
        && qa_command_buffer_register(commands, "color", color_netquake_handler, NULL, NULL, QA_COMMAND_WORLD))
      registration->names[registration->count++] = "color";
  }
  if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD && !qa_command_buffer_exists(commands, "color")
      && qa_command_buffer_register(commands, "color", color_qw_handler, NULL, NULL, QA_COMMAND_WORLD))
    registration->names[registration->count++] = "color";
  return registration;
}
void qa_app_q1_view_commands_release(qa_app_q1_view_commands *registration) {
  if (registration == NULL) return;
  for (size_t i = 0; i < registration->count; i++)
    qa_command_buffer_unregister(registration->commands, registration->names[i], NULL, NULL, false);
  free(registration);
}
static char *validate_number(void *ctx, const char *value) {
  (void)ctx;
  /* value.trim() !== "" && Number.isFinite(Number(value)); strtod missed "0b1" and "0o7". */
  const char *start = value;
  size_t space;
  while ((space = qa_js_white_space_length(start)) != 0) start += space;
  if (*start == '\0') {
    char *message = malloc(26);
    if (message != NULL) memcpy(message, "Expected a finite number", 25);
    return message;
  }
  double number = qa_js_string_to_number(value);
  if (!isfinite(number)) {
    char *message = malloc(26);
    if (message != NULL) memcpy(message, "Expected a finite number", 25);
    return message;
  }
  return NULL;
}
static void changed_noop(void *ctx, const char *value) { (void)ctx; (void)value; }
/* The registry borrows documentation: `examples` is a static array. */
static void register_number(qa_cvar_registry *cvars, const char *name, const char *value,
    double flags, const char *summary, const char *usage, const char *const *examples) {
  qa_cvar_snapshot snap;
  if (qa_cvar_registry_register(cvars, name, value, flags, &snap)) qa_cvar_read_free(&snap);
  qa_cvar_value_binding binding = {NULL, validate_number, changed_noop};
  qa_cvar_registry_bind_value(cvars, name, binding);
  qa_command_documentation documentation = {summary, usage, examples, 1, NULL, 0};
  qa_cvar_registry_document(cvars, name, documentation);
}
static const char *const chase_active_examples[] = {"chase_active 0"};
static const char *const chase_back_examples[] = {"chase_back 100"};
static const char *const chase_up_examples[] = {"chase_up 16"};
static const char *const chase_right_examples[] = {"chase_right 0"};
void qa_app_register_q1_client_settings(qa_cvar_registry *cvars, qa_command_dialect profile, bool profile_all) {
  bool q1 = profile == QA_COMMAND_DIALECT_Q1_NETQUAKE || profile == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  if (!profile_all && !q1) return;
  qa_cvar_snapshot snap;
  if (qa_cvar_registry_register(cvars, "viewsize", "100", QA_CVAR_FLAG_ARCHIVE, &snap)) qa_cvar_read_free(&snap);
  if (profile_all || profile == QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
    if (qa_cvar_registry_register(cvars, "cl_sbar", "0", QA_CVAR_FLAG_ARCHIVE, &snap)) qa_cvar_read_free(&snap);
  }
  qa_cvar_value_binding binding = {NULL, validate_number, changed_noop};
  qa_cvar_registry_bind_value(cvars, "viewsize", binding);
  static const char *const view_examples[] = {"viewsize 100", "viewsize 120"};
  qa_command_documentation view_doc = {
      "Quake view size. 30 through 100 sizes the scene; 110 removes the inventory margin, 120 hides the status display.",
      "viewsize <30..120>", view_examples, 2, NULL, 0};
  qa_cvar_registry_document(cvars, "viewsize", view_doc);
  if (profile_all || profile == QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    register_number(cvars, "chase_active", "0", 0, "Enable the local chase camera without changing player aim.", "chase_active <number>", chase_active_examples);
    register_number(cvars, "chase_back", "100", 0, "Chase distance behind the player, in world units.", "chase_back <number>", chase_back_examples);
    register_number(cvars, "chase_up", "16", 0, "Chase height above the eye, in world units.", "chase_up <number>", chase_up_examples);
    register_number(cvars, "chase_right", "0", 0, "Q1 lateral chase offset: positive moves opposite the view's right vector.", "chase_right <number>", chase_right_examples);
  }
}
bool qa_app_read_q1_view_settings(qa_cvar_registry *cvars, qa_command_dialect profile,
    bool has_profile, qa_app_q1_view_settings *out) {
  if (!has_profile) {
    if (cvars == NULL) return false;
    profile = qa_cvar_registry_dialect(cvars);
  }
  if (profile != QA_COMMAND_DIALECT_Q1_NETQUAKE && profile != QA_COMMAND_DIALECT_Q1_QUAKEWORLD) return false;
  if (cvars == NULL) return false;
  qa_cvar_read current;
  if (!qa_cvar_registry_find(cvars, "viewsize", &current)) return false;
  double numeric = current.numeric_value;
  qa_cvar_read_free(&current);
  double size = qa_js_max(30, qa_js_min(120, numeric));
  if (size != numeric) {
    char text[32];
    snprintf(text, sizeof text, "%s", QA_JS_NUMBER(size));
    qa_cvar_snapshot snap;
    if (qa_cvar_registry_set(cvars, "viewsize", text, false, &snap)) qa_cvar_read_free(&snap);
  }
  memset(out, 0, sizeof *out);
  out->size = size;
  out->overlay_status = profile == QA_COMMAND_DIALECT_Q1_QUAKEWORLD
      && qa_cvar_registry_variable_value(cvars, "cl_sbar") == 0;
  if (profile == QA_COMMAND_DIALECT_Q1_NETQUAKE && qa_cvar_registry_variable_value(cvars, "chase_active") != 0) {
    out->has_chase = true;
    out->chase.back = qa_cvar_registry_variable_value(cvars, "chase_back");
    out->chase.up = qa_cvar_registry_variable_value(cvars, "chase_up");
    out->chase.right = qa_cvar_registry_variable_value(cvars, "chase_right");
  }
  return true;
}
qa_scene_camera qa_app_q1_chase_camera(qa_scene_camera camera, qa_vec3 angles,
    qa_app_q1_chase_settings settings, const qa_scene_queries *scene,
    qa_numeric_profile numeric, qa_actor_id actor) {
  const qa_numeric_operations *math = qa_create_numeric_operations(&numeric);
  qa_vec3 forward = {0, 0, 0}, right = {0, 0, 0};
  qa_donor_angle_vectors(angles, &forward, &right, NULL);
  double desired_x = math->store(math->ctx, math->subtract(math->ctx,
      math->subtract(math->ctx, camera.origin.x, math->multiply(math->ctx, forward.x, settings.back)),
      math->multiply(math->ctx, right.x, settings.right)));
  double desired_y = math->store(math->ctx, math->subtract(math->ctx,
      math->subtract(math->ctx, camera.origin.y, math->multiply(math->ctx, forward.y, settings.back)),
      math->multiply(math->ctx, right.y, settings.right)));
  qa_vec3 desired = {desired_x, desired_y,
      math->store(math->ctx, math->add(math->ctx, camera.origin.z, settings.up))};
  qa_trace_query query = {0};
  query.start = camera.origin;
  query.target.is_model = false;
  query.policy.kind = "q1"; query.policy.move = "normal"; query.policy.has_hull = false;
  query.numeric = numeric;
  query.pass_actor = &actor;
  query.end = desired;
  query.shape.kind = "box";
  query.shape.bounds.min.x = -4; query.shape.bounds.min.y = -4; query.shape.bounds.min.z = -4;
  query.shape.bounds.max.x = 4; query.shape.bounds.max.y = 4; query.shape.bounds.max.z = 4;
  query.policy.derived = true; /* the rear trace alone: policy { ..., derived: true } */
  qa_trace_result rear = scene->trace(scene->ctx, query);
  query.policy.derived = false;
  qa_vec3 origin = rear.start_solid || rear.all_solid ? camera.origin : rear.end;
  qa_vec3 far = {
      math->store(math->ctx, math->add(math->ctx, camera.origin.x, math->multiply(math->ctx, forward.x, 4096))),
      math->store(math->ctx, math->add(math->ctx, camera.origin.y, math->multiply(math->ctx, forward.y, 4096))),
      math->store(math->ctx, math->add(math->ctx, camera.origin.z, math->multiply(math->ctx, forward.z, 4096)))};
  query.end = far;
  query.shape.kind = "point";
  qa_trace_result aim = scene->trace(scene->ctx, query);
  qa_vec3 target = aim.fraction == 1 || aim.start_solid || aim.all_solid ? far : aim.end;
  qa_vec3 delta = {target.x - origin.x, target.y - origin.y, target.z - origin.z};
  double horizontal = qa_js_hypot2(delta.x, delta.y);
  qa_vec3 view_angles = {
      math->store(math->ctx, -atan2(delta.z, horizontal) * 180 / acos(-1.0)),
      horizontal == 0 ? angles.y : math->store(math->ctx, atan2(delta.y, delta.x) * 180 / acos(-1.0)),
      angles.z};
  qa_scene_camera out = camera;
  out.origin = origin;
  out.axis = qa_angles_to_axis(view_angles);
  return out;
}
qa_rect qa_app_q1_view_rectangle(qa_rect area, double viewsize, bool intermission, bool overlay_status) {
  /* Math.min/Math.max propagate NaN. */
  double size = intermission ? 120 : qa_js_max(30, qa_js_min(120, viewsize));
  double lines = size >= 120 ? 0 : size >= 110 ? 24 : 48;
  double available = qa_js_max(1, area.height - (overlay_status && size >= 100 ? 0 : lines));
  double fraction = qa_js_min(size, 100) / 100;
  double width = qa_js_min(area.width, qa_js_max(96, trunc(area.width * fraction)));
  double height = qa_js_max(1, qa_js_min(available, trunc(area.height * fraction)));
  qa_rect out = {area.x + trunc((area.width - width) / 2),
      area.y + (size >= 100 ? 0 : trunc((available - height) / 2)), width, height};
  return out;
}
qa_scene_camera qa_app_q1_view_camera(qa_scene_camera camera, qa_rect area,
    qa_app_q1_view_settings settings, bool intermission) {
  qa_rect viewport = qa_app_q1_view_rectangle(area, settings.size, intermission, settings.overlay_status);
  double ratio = viewport.width / viewport.height / (camera.viewport.width / camera.viewport.height);
  qa_scene_camera out = camera;
  out.viewport = viewport;
  const double *p = camera.projection.values;
  double *q = out.projection.values;
  q[0] = p[0]; q[1] = p[1] * ratio; q[2] = p[2]; q[3] = p[3];
  q[4] = p[4]; q[5] = p[5] * ratio; q[6] = p[6]; q[7] = p[7];
  q[8] = p[8]; q[9] = p[9] * ratio; q[10] = p[10]; q[11] = p[11];
  q[12] = p[12]; q[13] = p[13] * ratio; q[14] = p[14]; q[15] = p[15];
  return out;
}
