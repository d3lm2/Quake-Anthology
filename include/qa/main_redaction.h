/*
 * Port of the crash-log redaction in src/main.ts (redactArguments, redactOutput): the
 * crash log must not carry a password, secret, token or API key from the command line or
 * the recent console output. GPL-2.0-or-later.
 */
#ifndef QA_MAIN_REDACTION_H
#define QA_MAIN_REDACTION_H
#include <stddef.h>

/* TS redactArguments: each argument whose predecessor matches
 * /password|secret|token|apikey|api_key/i becomes "<redacted>". Returns a malloc'd array of
 * `count` pointers into `argv` (or to the static "<redacted>"); free the array only. NULL on
 * allocation failure. */
const char **qa_redact_arguments(size_t count, const char *const *argv);

/* TS redactOutput: text.replace(/\b((?:rcon_|mvd_|spectator_|sv_|cl_)?(?:password|secret|token|
 * apikey|api_key)\b"?\s*(?:is\s*:?|=)?\s*)("[^"\n]*"|\S+)/gi, "$1<redacted>") over UTF-8 text
 * (\s includes the Unicode spaces JS matches). Returns malloc'd text, NULL on allocation failure. */
char *qa_redact_output(const char *text);

#endif
