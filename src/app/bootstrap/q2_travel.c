/* Port of src/app/bootstrap/q2-travel.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_travel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static char *dup_range(const char *text, size_t length) {
  char *out = malloc(length + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, length); out[length] = '\0';
  return out;
}
static bool name_ok(const char *name) {
  size_t len = strlen(name);
  if (len == 0 || name[0] == '/') return false;
  if (strstr(name, "//") != NULL) return false;
  const char *dot = NULL;
  for (size_t i = 0; i < len; i++) if (name[i] == '.') dot = name + i;
  size_t base = dot != NULL ? (size_t)(dot - name) : len;
  if (base == 0) return false;
  for (size_t i = 0; i < base; i++) {
    char ch = name[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '-'))
      return false;
  }
  if (dot != NULL && strcmp(dot, ".cin") != 0 && strcmp(dot, ".pcx") != 0 && strcmp(dot, ".dm2") != 0) return false;
  return true;
}
static bool spawn_ok(const char *text) {
  for (const char *p = text; *p != '\0'; p++) {
    char ch = *p;
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
  }
  return true;
}
static bool ends_with(const char *text, const char *suffix) {
  size_t a = strlen(text), b = strlen(suffix);
  return a >= b && strcmp(text + a - b, suffix) == 0;
}
/* A parse's working storage: a TS throw leaves it to the GC. */
typedef struct travel_parse {
  const char **starts; size_t *lengths;
  char *name, *spawn;
  qa_app_q2_travel_target *next;
} travel_parse;
static void travel_parse_free(void *v) {
  travel_parse *parse = v;
  free(parse->starts); free(parse->lengths); free(parse->name); free(parse->spawn);
  qa_app_q2_travel_target_destroy(parse->next);
}
qa_app_q2_travel_target *qa_app_parse_q2_travel(const char *expression) {
  /* Split on '+', then fold from last to first (TS order). */
  size_t parts = 1;
  for (const char *p = expression; *p != '\0'; p++) if (*p == '+') parts++;
  travel_parse parse = {malloc(parts * sizeof *parse.starts), malloc(parts * sizeof *parse.lengths), NULL, NULL, NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, travel_parse_free, &parse);
  if (parse.starts == NULL || parse.lengths == NULL) qa_range_error("Out of memory");
  size_t at = 0; const char *start = expression;
  for (const char *p = expression;; p++) {
    if (*p == '+' || *p == '\0') { parse.starts[at] = start; parse.lengths[at] = (size_t)(p - start); at++; start = p + 1; }
    if (*p == '\0') break;
  }
  for (size_t idx = parts; idx-- > 0;) {
    if (parse.lengths[idx] == 0) qa_raise("Error", 0, 0, "Q2 travel has an empty destination");
    char *part = dup_range(parse.starts[idx], parse.lengths[idx]);
    char *dollar = strchr(part, '$');
    parse.spawn = dup_range(dollar != NULL ? dollar + 1 : "", dollar != NULL ? strlen(dollar + 1) : 0);
    if (dollar != NULL) *dollar = '\0';
    bool new_unit = part[0] == '*';
    parse.name = dup_range(new_unit ? part + 1 : part, strlen(new_unit ? part + 1 : part));
    free(part);
    if (!name_ok(parse.name)) qa_raise("Error", 0, 0, "Invalid Q2 travel destination: %s", parse.name);
    if (!spawn_ok(parse.spawn)) qa_raise("Error", 0, 0, "Invalid Q2 spawn point: %s", parse.spawn);
    qa_app_q2_travel_target *node = malloc(sizeof *node);
    if (node == NULL) qa_range_error("Out of memory");
    node->kind = ends_with(parse.name, ".cin") ? "cinematic" : ends_with(parse.name, ".pcx") ? "picture"
        : ends_with(parse.name, ".dm2") ? "demo" : "map";
    node->name = parse.name; node->spawn_point = parse.spawn; node->new_unit = new_unit; node->next = parse.next;
    parse.name = parse.spawn = NULL;
    parse.next = node;
  }
  if (parse.next == NULL) qa_raise("Error", 0, 0, "Q2 travel has no destination");
  qa_cleanup_pop(&guard);
  free(parse.starts); free(parse.lengths);
  return parse.next;
}
void qa_app_q2_travel_target_destroy(qa_app_q2_travel_target *target) {
  while (target != NULL) {
    qa_app_q2_travel_target *next = target->next;
    free(target->name); free(target->spawn_point); free(target);
    target = next;
  }
}
char *qa_app_q2_next_server_command(const qa_app_q2_travel_target *target) {
  if (target->next == NULL) return dup_range("", 0);
  size_t size = 16;
  for (const qa_app_q2_travel_target *n = target->next; n != NULL; n = n->next)
    size += strlen(n->name) + strlen(n->spawn_point) + 3;
  char *expression = malloc(size);
  if (expression == NULL) qa_range_error("Out of memory");
  size_t at = 0; bool first = true;
  for (const qa_app_q2_travel_target *n = target->next; n != NULL; n = n->next) {
    int w = snprintf(expression + at, size - at, "%s%s%s%s%s", first ? "" : "+",
        n->new_unit ? "*" : "", n->name, n->spawn_point[0] != '\0' ? "$" : "", n->spawn_point);
    if (w > 0) at += (size_t)w;
    first = false;
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, expression);
  qa_app_q2_travel_target *check = qa_app_parse_q2_travel(expression);
  qa_cleanup_pop(&guard);
  qa_app_q2_travel_target_destroy(check);
  char *out = malloc(at + 16);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, at + 16, "gamemap \"%s\"", expression);
  free(expression);
  return out;
}
