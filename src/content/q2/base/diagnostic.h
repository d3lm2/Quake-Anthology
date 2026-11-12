/* Private helper for the Q2 base slice: game.host.diagnostic(`...${text}...`) with
 * authored text of any length (TS template strings have no limit). GPL-2.0-or-later. */
#ifndef QA_Q2_BASE_DIAGNOSTIC_H
#define QA_Q2_BASE_DIAGNOSTIC_H
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "qa/content/q2/foundation/host.h"
#include "qa/foundation/support.h"

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static inline void q2b_diagnosticf(qa_q2_game_services *game, const char *format, ...) {
  va_list args, copy;
  va_start(args, format);
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  char *message = length < 0 ? NULL : malloc((size_t)length + 1);
  if (!message) { va_end(args); qa_range_error("Out of memory"); }
  (void)vsnprintf(message, (size_t)length + 1, format, args);
  va_end(args);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, message);
  game->host->diagnostic(game->host->ctx, message);
  qa_cleanup_pop(&guard);
  free(message);
}
#endif
