/* See include/qa/foundation/support.h. GPL-2.0-or-later. */
#include "qa/foundation/support.h"

#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/caught_message.h"

typedef struct catch_frame {
  jmp_buf jump;
  qa_caught_error *caught;
  qa_cleanup *cleanups;
  struct catch_frame *previous;
} catch_frame;
static _Thread_local catch_frame *catch_top;
static _Thread_local qa_cleanup *cleanup_top;
/* The whole texts of the most recent caught raises on this thread whose message was longer than
 * QA_CAUGHT_MESSAGE_CAPACITY - 1 bytes, keyed by the qa_caught_error they were caught into. Several
 * are kept so a catch that runs cleanup (which may raise and catch on its own) before rethrowing
 * still finds its own whole text. */
#define WHOLE_MESSAGE_SLOTS 8
typedef struct whole_message {
  const qa_caught_error *owner;
  char *text;
} whole_message;
static _Thread_local whole_message whole_messages[WHOLE_MESSAGE_SLOTS];
static _Thread_local unsigned whole_message_next;

const char *qa_caught_message(const qa_caught_error *caught) {
  size_t kept = strlen(caught->message);
  if (kept != sizeof caught->message - 1) return caught->message;
  for (size_t i = 0; i < WHOLE_MESSAGE_SLOTS; i++) {
    const whole_message *slot = &whole_messages[i];
    if (slot->owner == caught && slot->text != NULL && strncmp(slot->text, caught->message, kept) == 0) return slot->text;
  }
  return caught->message;
}

/* Records `text` (malloc'd or NULL) as the whole message of `caught`, replacing what it held. */
static void keep_whole_message(const qa_caught_error *caught, char *text) {
  for (size_t i = 0; i < WHOLE_MESSAGE_SLOTS; i++) {
    whole_message *slot = &whole_messages[i];
    if (slot->owner != caught) continue;
    free(slot->text);
    slot->text = NULL;
    slot->owner = NULL;
  }
  if (text == NULL) return;
  whole_message *slot = &whole_messages[whole_message_next++ % WHOLE_MESSAGE_SLOTS];
  free(slot->text);
  slot->owner = caught;
  slot->text = text;
}

void qa_caught_copy(qa_caught_error *to, const qa_caught_error *from) {
  const char *whole = qa_caught_message(from);
  char *kept = NULL;
  if (whole != from->message) {
    size_t length = strlen(whole);
    kept = malloc(length + 1);
    if (kept != NULL) memcpy(kept, whole, length + 1);
  }
  if (to != from) *to = *from;
  keep_whole_message(to, kept);
}

bool qa_try(void (*fn)(void *ctx), void *ctx, qa_caught_error *caught) {
  catch_frame frame;
  frame.caught = caught;
  frame.cleanups = cleanup_top;
  frame.previous = catch_top;
  catch_top = &frame;
  if (setjmp(frame.jump) != 0) return false; /* the raise restored catch_top and cleanup_top */
  fn(ctx);
  catch_top = frame.previous;
  return true;
}

bool qa_range_error_catch(void (*fn)(void *ctx), void *ctx, char *message, size_t capacity) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return true;
  if (capacity != 0) snprintf(message, capacity, "%s", qa_caught_message(&caught));
  return false;
}

static noreturn void raise_formatted(const char *name, int code, long long detail,
    const char *format, va_list args) {
  if (catch_top != NULL) {
    catch_frame *frame = catch_top;
    qa_caught_error *caught = frame->caught;
    /* Format first: the arguments may be owned by a pending cleanup. */
    char *message = caught->message;
    va_list whole;
    va_copy(whole, args);
    int needed = vsnprintf(message, sizeof caught->message, format, args);
    /* A text that does not fit the caught buffer is kept whole beside it (qa_caught_message): TS error messages
     * have no length limit. The new copy is made before the old one is released, since a rethrow formats from it. */
    char *longer = NULL;
    if (needed >= (int)sizeof caught->message) {
      longer = malloc((size_t)needed + 1);
      if (longer != NULL) vsnprintf(longer, (size_t)needed + 1, format, whole);
    }
    va_end(whole);
    keep_whole_message(caught, longer);
    caught->name = name;
    caught->code = code;
    caught->detail = detail;
    catch_top = frame->previous;
    while (cleanup_top != frame->cleanups) {
      qa_cleanup *node = cleanup_top;
      cleanup_top = node->previous;
      node->fn(node->arg);
    }
    longjmp(frame->jump, 1);
  }
  fprintf(stderr, "%s: ", name);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  abort();
}

noreturn void qa_raise(const char *name, int code, long long detail, const char *format, ...) {
  va_list args;
  va_start(args, format);
  raise_formatted(name, code, detail, format, args);
}

noreturn void qa_rethrow(const qa_caught_error *caught) {
  qa_raise(caught->name, caught->code, caught->detail, "%s", qa_caught_message(caught));
}

noreturn void qa_range_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  raise_formatted("RangeError", 0, 0, format, args);
}

void qa_cleanup_push(qa_cleanup *node, void (*fn)(void *arg), void *arg) {
  node->fn = fn;
  node->arg = arg;
  node->previous = cleanup_top;
  cleanup_top = node;
}

qa_unwind_mark qa_unwind_mark_take(void) {
  qa_unwind_mark mark = {cleanup_top, catch_top};
  return mark;
}

void qa_unwind_to(qa_unwind_mark mark) {
  catch_top = (catch_frame *)mark.catches;
  while (cleanup_top != mark.cleanups) {
    qa_cleanup *node = cleanup_top;
    if (node == NULL) {
      fputs("RangeError: unwind mark is not on the cleanup stack\n", stderr);
      abort();
    }
    cleanup_top = node->previous;
    node->fn(node->arg);
  }
}

void qa_cleanup_pop(qa_cleanup *node) {
  if (cleanup_top != node) {
    fputs("RangeError: cleanup popped out of order\n", stderr);
    abort();
  }
  cleanup_top = node->previous;
}
