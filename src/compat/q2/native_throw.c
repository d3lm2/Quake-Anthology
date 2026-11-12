/* Recoverable throw channel for the Q2 native bindings. See the header.
 * GPL-2.0-or-later. */
#include "qa/compat/q2/native_throw.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* qa_caught_error.code of a sentinel; detail is its token. */
#define NATIVE_THROW_CODE 0x51324e54 /* "Q2NT" */
/* The token reported for an error that is not a sentinel: no caller compares equal to it. */
static const char foreign_error = 0;
/* The thrower's own message pointer (callers keep it, and a caught message is a copy in the catch buffer). */
static _Thread_local const void *last_token;
static _Thread_local const char *last_message;

bool qa_compat_q2_native_try(void (*body)(void *ctx), void *ctx, qa_compat_q2_native_thrown *thrown) {
  qa_compat_q2_native_thrown local;
  qa_compat_q2_native_thrown *out = thrown != NULL ? thrown : &local;
  out->token = NULL; out->message = NULL;
  if (qa_try(body, ctx, &out->caught)) return true;
  if (out->caught.code == NATIVE_THROW_CODE && strcmp(out->caught.name, "Error") == 0) {
    out->token = (const void *)(intptr_t)out->caught.detail;
    out->message = last_token == out->token ? last_message : NULL;
  } else {
    out->token = &foreign_error;
    out->message = NULL;
  }
  return false;
}
noreturn void qa_compat_q2_native_throw(const void *token, const char *message) {
  last_token = token; last_message = message;
  qa_raise("Error", NATIVE_THROW_CODE, (long long)(intptr_t)token, "%s", message != NULL ? message : "Uncaught native control throw");
}
noreturn void qa_compat_q2_native_rethrow(const qa_compat_q2_native_thrown *thrown) {
  if (thrown->token == &foreign_error) qa_rethrow(&thrown->caught);
  qa_compat_q2_native_throw(thrown->token, thrown->message);
}
double qa_compat_q2_native_depth(const qa_compat_q2_runner *runner) {
  return runner->depth != NULL ? runner->depth(runner->ctx) : 0;
}
void qa_compat_q2_native_unwind(const qa_compat_q2_runner *runner, double depth) {
  if (runner->unwind != NULL) runner->unwind(runner->ctx, depth);
}
