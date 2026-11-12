/*
 * Port of src/app/bootstrap/console-log.ts: the console log shared by every
 * application type, and the append-only log file both it (`qconsole.log`) and
 * the game's log (`games.log`) are instances of.
 * C accommodations:
 *  - hostWithConsoleLog wraps an ApplicationHost's print; in C each
 *    application's print (qa_application_print, the remote application's
 *    print) writes its console log before the host print, in the same order.
 *  - ConsoleLog's mode and root closures are fn + ctx; root returns a malloc'd
 *    path the log frees.
 * GPL-2.0-or-later.
 */
#ifndef QA_APP_BOOTSTRAP_CONSOLE_LOG_H
#define QA_APP_BOOTSTRAP_CONSOLE_LOG_H

#include <stdbool.h>

#include "qa/core/cvars/index.h"
#include "qa/platform/files/writable.h"

/* TS registerConsoleLogCvar: Quake II and Quake III keep the `logfile` variable
 * (Quake III registers it with its server variables already). */
void qa_app_register_console_log_cvar(qa_cvar_registry *cvars);

/* TS consoleLogMode: 0 none, 1 append, 2 append and flush every write. Quake II
 * and Quake III ask through the `logfile` variable; Quake (WinQuake) asks with
 * the `-condebug` launch flag, which flushes each line. `cvars` NULL is the TS
 * null. */
double qa_app_console_log_mode(qa_cvar_registry *cvars, bool console_debug);

/* TS AppendLog: one append-only log file kept open between writes and reopened
 * when its name or sync mode changes. Zero-initialize. */
typedef struct qa_app_append_log {
  bool open;
  char *name; /* owned */
  bool sync;
  qa_writable_binary_file file;
} qa_app_append_log;
/* Appends `text` to `name` in `root`; false when the file could not be opened.
 * `report` NULL is the TS undefined. */
bool qa_app_append_log_write(qa_app_append_log *log, const char *root, const char *name, bool sync, const char *text,
                             qa_writable_print_fn report, void *report_ctx);
void qa_app_append_log_close(qa_app_append_log *log);

/* TS ConsoleLog: the owner supplies its mode and its game folder. */
typedef struct qa_app_console_log {
  qa_app_append_log log;
  double (*mode)(void *ctx);
  char *(*root)(void *ctx); /* malloc'd */
  void *ctx;
} qa_app_console_log;
void qa_app_console_log_init(qa_app_console_log *log, double (*mode)(void *ctx), char *(*root)(void *ctx), void *ctx);
void qa_app_console_log_write(qa_app_console_log *log, const char *text);
void qa_app_console_log_close(qa_app_console_log *log);

#endif
