/*
 * C-only support for the Q2 native bindings (no .ts file): the recoverable
 * TypeScript throws these modules rely on (SourceItemRetired, frame.cancelled,
 * RemovedNativeInput, the ammo stop and supply decline sentinels) unwind a
 * guest call back to the host frame that catches them.
 *
 * These are ordinary TS Error objects compared by identity, so they travel on
 * the port's one unwind mechanism (qa_raise/qa_try, foundation/support.h):
 * qa_compat_q2_native_throw raises an "Error" that carries its token, and
 * qa_compat_q2_native_try is `try { body(ctx) }` over qa_try. It returns true
 * when the body completed and false when anything was thrown: a sentinel
 * (thrown->token is the token it was thrown with) or any other error class
 * (thrown->token matches no caller's token). `finally` code runs after the
 * call, for every kind of throw as in TS; then qa_compat_q2_native_rethrow
 * continues the unwind for whatever this frame does not handle, re-raising a
 * foreign error with its own name, code, detail and message. A throw with no
 * enclosing catch prints "Error: <message>" and aborts, as an uncaught TS
 * Error ends the process.
 *
 * A catch that resumes guest execution must restore the processor and call
 * qa_compat_q2_native_unwind so the runner forgets the abandoned calls.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_Q2_NATIVE_THROW_H
#define QA_COMPAT_Q2_NATIVE_THROW_H
#include <stdbool.h>
#include <stdnoreturn.h>
#include "qa/compat/q2/classic/layout.h"

#include "qa/foundation/support.h"

typedef struct qa_compat_q2_native_thrown {
  const void *token;   /* identity compared, as TS `error === token` */
  const char *message; /* reported when uncaught */
  /* The error as caught; rethrow re-raises it when it is not a sentinel. */
  qa_caught_error caught;
} qa_compat_q2_native_thrown;

bool qa_compat_q2_native_try(void (*body)(void *ctx), void *ctx, qa_compat_q2_native_thrown *thrown);
noreturn void qa_compat_q2_native_throw(const void *token, const char *message);
noreturn void qa_compat_q2_native_rethrow(const qa_compat_q2_native_thrown *thrown);
/* Current runner call depth / abandon deeper calls after a caught throw. */
double qa_compat_q2_native_depth(const qa_compat_q2_runner *runner);
void qa_compat_q2_native_unwind(const qa_compat_q2_runner *runner, double depth);
#endif
