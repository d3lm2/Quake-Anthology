/* Port of src/app/bootstrap/console-log.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/console_log.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"

static bool q1_dialect(qa_command_dialect dialect) {
  return dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
}
static bool q2_dialect(qa_command_dialect dialect) {
  return dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE;
}

void qa_app_register_console_log_cvar(qa_cvar_registry *cvars) {
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  if (q1_dialect(dialect)) return;
  qa_cvar_read found;
  if (qa_cvar_registry_find(cvars, "logfile", &found)) {
    qa_cvar_read_free(&found);
    return;
  }
  qa_cvar_snapshot registered;
  if (!qa_cvar_registry_register(cvars, "logfile", "0", q2_dialect(dialect) ? 0 : QA_CVAR_FLAG_TEMPORARY, &registered))
    qa_range_error("Out of memory");
  qa_cvar_read_free(&registered);
}

double qa_app_console_log_mode(qa_cvar_registry *cvars, bool console_debug) {
  qa_cvar_read variable;
  if (cvars != NULL && qa_cvar_registry_find(cvars, "logfile", &variable)) {
    double mode = variable.integer_value;
    qa_cvar_read_free(&variable);
    return mode;
  }
  return console_debug && (cvars == NULL || q1_dialect(qa_cvar_registry_dialect(cvars))) ? 2 : 0;
}

bool qa_app_append_log_write(qa_app_append_log *log, const char *root, const char *name, bool sync, const char *text,
                             qa_writable_print_fn report, void *report_ctx) {
  if (!log->open || strcmp(log->name, name) != 0 || log->sync != sync) {
    qa_app_append_log_close(log);
    qa_user_file_store store;
    if (!qa_user_file_store_init(&store, root)) qa_range_error("Out of memory");
    bool opened = qa_user_file_store_open(&store, name, sync ? QA_WRITABLE_APPEND_SYNC : QA_WRITABLE_APPEND, report,
                                          report_ctx, &log->file);
    qa_user_file_store_destroy(&store);
    if (!opened) return false;
    log->name = strdup(name);
    if (log->name == NULL) qa_range_error("Out of memory");
    log->sync = sync;
    log->open = true;
  }
  /* new TextEncoder().encode(text): the console text is written as UTF-8 (core/commands/text.h). */
  char *utf8 = qa_command_text_to_utf8(text);
  (void)qa_writable_binary_file_write(&log->file, (const uint8_t *)utf8, strlen(utf8));
  free(utf8);
  return true;
}

void qa_app_append_log_close(qa_app_append_log *log) {
  if (log->open) {
    qa_writable_binary_file_close(&log->file);
    free(log->name);
  }
  log->name = NULL;
  log->open = false;
}

void qa_app_console_log_init(qa_app_console_log *log, double (*mode)(void *ctx), char *(*root)(void *ctx), void *ctx) {
  memset(log, 0, sizeof *log);
  log->mode = mode;
  log->root = root;
  log->ctx = ctx;
}

void qa_app_console_log_write(qa_app_console_log *log, const char *text) {
  double mode = log->mode(log->ctx);
  if (mode == 0) {
    qa_app_append_log_close(&log->log);
    return;
  }
  char *root = log->root(log->ctx);
  (void)qa_app_append_log_write(&log->log, root, "qconsole.log", mode >= 2, text, NULL, NULL);
  free(root);
}

void qa_app_console_log_close(qa_app_console_log *log) { qa_app_append_log_close(&log->log); }
