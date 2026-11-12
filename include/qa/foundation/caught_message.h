/*
 * The whole message of a caught error. qa_caught_error (support.h) keeps the
 * first QA_CAUGHT_MESSAGE_CAPACITY - 1 bytes of a message in the struct, where a
 * TypeScript error message has no length limit (a guest's Com_Error or game
 * error text can be long). The raise also keeps the whole text of the most
 * recent caught error on its thread; qa_rethrow re-raises with it, so a
 * catch-and-rethrow chain loses nothing, and an uncaught raise prints all of it.
 *
 * qa_caught_message returns that whole text when `caught` is the most recent
 * caught error of this thread and its message was cut, and caught->message
 * otherwise. The pointer is valid until the next raise on this thread.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_CAUGHT_MESSAGE_H
#define QA_FOUNDATION_CAUGHT_MESSAGE_H

#include "qa/foundation/support.h"

const char *qa_caught_message(const qa_caught_error *caught);

/* `*to = *from`, keeping the whole message: a copied qa_caught_error (the first of several
 * failures kept for an AggregateError) answers qa_caught_message with all of it. */
void qa_caught_copy(qa_caught_error *to, const qa_caught_error *from);

#endif
