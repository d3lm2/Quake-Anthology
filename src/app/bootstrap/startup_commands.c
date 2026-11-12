/* Port of src/app/bootstrap/startup-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_commands.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static void append(char **buf, const char *add) {
  size_t a = strlen(*buf), b = strlen(add);
  char *n = realloc(*buf, a + b + 1);
  if (!n) qa_range_error("Out of memory");
  memcpy(n + a, add, b + 1); *buf = n;
}
static void text_slot_free(void *raw) { free(*(char **)raw); }
/* sourceCommandText over an argument: the arguments are UTF-8 (TS decodes them to strings); the command text holds one
 * byte per code unit, and a character above U+00FF raises TS's RangeError "Command text requires source bytes". */
static char *source_text(const char *argument) {
  char *bytes = NULL;
  if (!qa_command_text_from_utf8(argument, &bytes)) qa_range_error("Command text requires source bytes");
  return bytes;
}
static char *first_token_folded(const char *line, qa_command_dialect d, char **second) {
  qa_command_tokens t; memset(&t, 0, sizeof t);
  if (!qa_tokenize_command(line, d, QA_COMMAND_TEXT_SOURCE, &t)) qa_range_error("Out of memory");
  char *r = t.argc ? qa_ascii_fold(t.argv[0]) : qa_input_dup("");
  char *s2 = NULL;
  if (second) s2 = t.argc > 1 ? qa_ascii_fold(t.argv[1]) : qa_input_dup("");
  qa_command_tokens_destroy(&t);
  if (!r || (second && !s2)) { free(r); free(s2); qa_range_error("Out of memory"); }
  if (second) *second = s2;
  return r;
}
qa_app_startup_command qa_app_read_startup_command(const char *const *argv, size_t argc, size_t index) {
  const char *first = index < argc ? argv[index] : NULL;
  if (first == NULL || first[0] != '+' || first[1] == '\0') qa_raise("Error", 0, 0, "Expected +command");
  /* `rest` holds the operands, each ` "value"`; text = first.slice(1) + rest. */
  char *rest = xdup(""), *text = NULL;
  qa_cleanup rest_guard, text_guard;
  qa_cleanup_push(&rest_guard, text_slot_free, &rest);
  qa_cleanup_push(&text_guard, text_slot_free, &text);
  size_t length = 0, end = index;
  while (end + 1 < argc) {
    const char *next = argv[end + 1];
    if (next == NULL || next[0] == '+' || strncmp(next, "--", 2) == 0) break;
    /* operand(next): sourceCommandText(value), then /["\r\n\0]/ (a C string holds no NUL). */
    char *value = source_text(next);
    if (strchr(value, '"') != NULL || strchr(value, '\r') != NULL || strchr(value, '\n') != NULL) {
      free(value);
      qa_raise("Error", 0, 0, "Startup argument cannot contain quotes or line breaks; use a quoted +command batch or exec a cfg file.");
    }
    size_t value_length = strlen(value);
    char *grown = realloc(rest, length + value_length + 4);
    if (grown == NULL) { free(value); qa_range_error("Out of memory"); }
    rest = grown;
    rest[length++] = ' ';
    rest[length++] = '"';
    memcpy(rest + length, value, value_length);
    length += value_length;
    rest[length++] = '"';
    rest[length] = '\0';
    free(value);
    end++;
  }
  /* sourceCommandText(text): the operands are checked; the first argument's own characters remain. */
  char *head = source_text(first + 1);
  size_t head_length = strlen(head);
  text = malloc(head_length + length + 1);
  if (text == NULL) { free(head); qa_range_error("Out of memory"); }
  memcpy(text, head, head_length);
  memcpy(text + head_length, rest, length + 1);
  free(head);
  if (strchr(text, '\r') != NULL || strchr(text, '\n') != NULL || qa_command_separator_offset(text, QA_COMMAND_DIALECT_Q3) < strlen(text))
    qa_raise("Error", 0, 0, "Use a separate +command for each startup command.");
  qa_command_tokens t; memset(&t, 0, sizeof t);
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &t)) qa_range_error("Out of memory");
  if (t.argc == 0) { qa_command_tokens_destroy(&t); qa_raise("Error", 0, 0, "Expected +command"); }
  char *tok0 = qa_ascii_fold(t.argv[0]);
  char *tok1 = qa_ascii_fold(t.argc > 1 ? t.argv[1] : "");
  qa_command_tokens_destroy(&t);
  if (tok0 == NULL || tok1 == NULL) { free(tok0); free(tok1); qa_range_error("Out of memory"); }
  bool connect = strcmp(tok0, "connect") == 0, filesystem = false;
  if (strcmp(tok0, "set") == 0) {
    static const char *const bad[] = {"game", "fs_game", "basedir", "cddir", "fs_basepath", "fs_homepath", "fs_cdpath"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
      if (strcmp(tok1, bad[i]) == 0) filesystem = true;
  }
  free(tok0); free(tok1);
  if (connect)
    qa_raise("Error", 0, 0, "Ordered +connect startup is unsupported; use --connect-q1, --connect-qw, --connect-q2 or --connect-q3.");
  if (filesystem)
    qa_raise("Error", 0, 0, "Filesystem +set selection is unsupported; use --game with an installed catalog product and --content-root or --user-content-root.");
  qa_cleanup_pop(&text_guard);
  qa_cleanup_pop(&rest_guard);
  free(rest);
  qa_app_startup_command r = {text, end};
  return r;
}
bool qa_app_startup_requests_world(const char *const *lines, size_t count) {
  static const char *const cmds[] = {"map", "devmap", "spmap", "spdevmap"};
  for (size_t i = 0; i < count; i++) {
    /* TS uses toLowerCase (unicode); asciiFold matches for these ASCII names. */
    char *t = first_token_folded(lines[i], QA_COMMAND_DIALECT_Q3, NULL);
    bool hit = false;
    for (size_t j = 0; j < 4; j++) if (strcmp(t, cmds[j]) == 0) hit = true;
    free(t);
    if (hit) return true;
  }
  return false;
}
static char *join_lines(const char *const *lines, size_t count) {
  if (count == 0) return xdup("");
  char *r = xdup("");
  for (size_t i = 0; i < count; i++) { if (i) append(&r, "\n"); append(&r, lines[i]); }
  append(&r, "\n");
  return r;
}
qa_app_startup_phases qa_app_startup_command_phases(const char *const *lines, size_t count, qa_command_dialect dialect) {
  long safe_index = -1;
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    for (size_t i = 0; i < count; i++) {
      char *t = first_token_folded(lines[i], dialect, NULL);
      bool hit = strcmp(t, "safe") == 0 || strcmp(t, "cvar_restart") == 0;
      free(t);
      if (hit) { safe_index = (long)i; break; }
    }
  }
  const char **early = calloc(count ? count : 1, sizeof *early);
  const char **late = calloc(count ? count : 1, sizeof *late);
  if (!early || !late) qa_range_error("Out of memory");
  size_t ne = 0, nl = 0;
  qa_app_startup_phases out; memset(&out, 0, sizeof out);
  out.variables = calloc(count ? count : 1, sizeof *out.variables);
  if (!out.variables) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    if ((long)i == safe_index) continue;
    qa_command_tokens t; memset(&t, 0, sizeof t);
    if (!qa_tokenize_command(lines[i], dialect, QA_COMMAND_TEXT_SOURCE, &t)) qa_range_error("Out of memory");
    bool set = t.argc > 0 && strcmp(t.argv[0], "set") == 0;
    if (dialect == QA_COMMAND_DIALECT_Q3 && set) {
      out.variables[out.variable_count].name = xdup(t.argc > 1 ? t.argv[1] : "");
      out.variables[out.variable_count].value = xdup(t.argc > 2 ? t.argv[2] : "");
      out.variable_count++;
    }
    qa_command_tokens_destroy(&t);
    bool q2 = dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE;
    bool q1 = dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
    if (q2 && set) early[ne++] = lines[i];
    if (dialect == QA_COMMAND_DIALECT_Q3 || !set || q1) late[nl++] = lines[i];
  }
  out.early = join_lines(early, ne);
  out.late = join_lines(late, nl);
  out.stuffed = join_lines(lines, count);
  out.safe = safe_index != -1;
  free(early); free(late);
  return out;
}
void qa_app_startup_phases_free(qa_app_startup_phases *p) {
  free(p->early); free(p->late); free(p->stuffed);
  for (size_t i = 0; i < p->variable_count; i++) { free(p->variables[i].name); free(p->variables[i].value); }
  free(p->variables); memset(p, 0, sizeof *p);
}
