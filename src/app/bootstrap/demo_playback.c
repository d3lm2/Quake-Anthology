/* Port of src/app/bootstrap/demo-playback.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/demo_playback.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"
static bool ends_ci(const char *text, const char *suffix) {
  size_t t = strlen(text), s = strlen(suffix);
  if (t < s) return false;
  for (size_t i = 0; i < s; i++)
    if (tolower((unsigned char)text[t - s + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
qa_app_demo_family qa_app_demo_family_of(const char *name, qa_app_demo_family fallback) {
  if (ends_ci(name, ".qwd")) return QA_APP_DEMO_QW;
  if (ends_ci(name, ".dem")) return QA_APP_DEMO_Q1;
  if (ends_ci(name, ".dm2") || ends_ci(name, ".mvd")) return QA_APP_DEMO_Q2;
  /* .dm_<digits> */
  const char *dot = strrchr(name, '.');
  if (dot && (dot[1] == 'd' || dot[1] == 'D') && (dot[2] == 'm' || dot[2] == 'M') && dot[3] == '_' && isdigit((unsigned char)dot[4])) {
    bool digits = true;
    for (const char *p = dot + 4; *p; p++) if (!isdigit((unsigned char)*p)) digits = false;
    if (digits) return QA_APP_DEMO_Q3;
  }
  return fallback;
}
static bool has_extension(const char *path) {
  const char *dot = strrchr(path, '.'), *slash = strrchr(path, '/');
  return dot && (!slash || dot > slash);
}
static char *join2(const char *a, const char *b) {
  size_t n = strlen(a) + strlen(b) + 1;
  char *p = malloc(n);
  if (p == NULL) qa_range_error("Out of memory");
  snprintf(p, n, "%s%s", a, b);
  return p;
}
static void free_slot(void *slot) { free(*(char **)slot); }
static char *format_text(const char *format, const char *a, const char *b) {
  int length = snprintf(NULL, 0, format, a, b);
  if (length < 0) qa_range_error("Out of memory");
  char *text = malloc((size_t)length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, (size_t)length + 1, format, a, b);
  return text;
}
qa_app_demo_resource qa_app_open_demo_resource(qa_app_demo_request request, qa_app_demo_reader reader) {
  char *name = qa_normalize_resource_path(request.name);
  if (name == NULL) qa_range_error("Out of memory");
  qa_cleanup name_guard;
  qa_cleanup_push(&name_guard, free_slot, &name);
  qa_app_demo_resource out = {request.family, NULL, NULL, 0, 0};
  if (request.family != QA_APP_DEMO_Q3) {
    const char *extension = request.family == QA_APP_DEMO_Q1 ? ".dem" : request.family == QA_APP_DEMO_QW ? ".qwd" : ".dm2";
    char *path = has_extension(name) ? join2(name, "") : join2(name, extension);
    if (request.family == QA_APP_DEMO_Q2 && strncmp(path, "demos/", 6) != 0) {
      char *prefixed = join2("demos/", path);
      free(path);
      path = prefixed;
    }
    qa_cleanup path_guard;
    qa_cleanup_push(&path_guard, free_slot, &path);
    if (!reader.read(reader.ctx, path, &out.bytes, &out.byte_count)) qa_raise("Error", 0, 0, "Couldn't open demo %s", path);
    qa_cleanup_pop(&path_guard);
    qa_cleanup_pop(&name_guard);
    free(name);
    out.path = path;
    return out;
  }
  const char *filename = strncmp(name, "demos/", 6) == 0 ? name + 6 : name;
  /* suffix .dm_(\d+) */
  double requested = -1; /* Number(suffix[1]); -1 for no suffix */
  size_t stem_len = strlen(filename);
  const char *dot = strrchr(filename, '.');
  if (dot && (dot[1] == 'd' || dot[1] == 'D') && (dot[2] == 'm' || dot[2] == 'M') && dot[3] == '_' && isdigit((unsigned char)dot[4])) {
    bool digits = true;
    for (const char *p = dot + 4; *p; p++) if (!isdigit((unsigned char)*p)) digits = false;
    if (digits) { requested = qa_js_string_to_number(dot + 4); stem_len = (size_t)(dot - filename); }
  }
  bool supported = requested == 66 || requested == 67 || requested == 68;
  char *stem = malloc(stem_len + 1);
  if (stem == NULL) qa_range_error("Out of memory");
  memcpy(stem, filename, stem_len);
  stem[stem_len] = '\0';
  qa_cleanup stem_guard;
  qa_cleanup_push(&stem_guard, free_slot, &stem);
  if (requested >= 0 && !supported) {
    char *message = format_text("Protocol %s not supported for demos\n", QA_JS_NUMBER(requested), "");
    qa_cleanup message_guard;
    qa_cleanup_push(&message_guard, free_slot, &message);
    reader.print(reader.ctx, message);
    qa_cleanup_pop(&message_guard);
    free(message);
  }
  static const long natives[] = {66, 67, 68};
  size_t candidate_count = supported ? 1 : 3;
  for (size_t i = 0; i < candidate_count; i++) {
    long protocol = supported ? (long)requested : natives[i];
    char *path;
    if (supported) path = join2("demos/", filename);
    else {
      char suffix[32];
      snprintf(suffix, sizeof suffix, ".dm_%ld", protocol);
      path = format_text("demos/%s%s", stem, suffix);
    }
    qa_cleanup path_guard;
    qa_cleanup_push(&path_guard, free_slot, &path);
    if (!reader.read(reader.ctx, path, &out.bytes, &out.byte_count)) {
      if (!supported) {
        char *message = format_text("Not found: %s\n", path, "");
        qa_cleanup message_guard;
        qa_cleanup_push(&message_guard, free_slot, &message);
        reader.print(reader.ctx, message);
        qa_cleanup_pop(&message_guard);
        free(message);
      }
      qa_cleanup_pop(&path_guard);
      free(path);
      continue;
    }
    qa_cleanup_pop(&path_guard);
    if (protocol != 66 && protocol != 67 && protocol != 68) {
      free(path); free(out.bytes);
      qa_raise("Error", 0, 0, "Invalid Quake III demo protocol selection");
    }
    out.path = path;
    out.protocol = (double)protocol;
    qa_cleanup_pop(&stem_guard);
    free(stem);
    qa_cleanup_pop(&name_guard);
    free(name);
    return out;
  }
  qa_raise("Error", 0, 0, "Couldn't open demo %s", name);
}
void qa_app_demo_resource_destroy(qa_app_demo_resource *resource) {
  if (!resource) return;
  free(resource->path);
  free(resource->bytes);
  *resource = (qa_app_demo_resource){QA_APP_DEMO_Q1, NULL, NULL, 0, 0};
}
