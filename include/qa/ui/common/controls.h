/*
 * Port of src/ui/common/controls.ts: the plain activating button and the
 * greedy word wrap shared by the native menus. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_CONTROLS_H
#define QA_UI_COMMON_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"

/** TS buttonControl(id, label, rect, activate, enabled): kind "button",
 * visible; activate (fn + ctx) is the control's activate_seat. */
qa_ui_control qa_ui_button_control(qa_ui_control_id id, const char *label, qa_rect rect,
                                   void (*activate)(void *ctx, qa_seat_id seat), void *ctx,
                                   bool enabled);

/** TS wrapWords(text, width): greedy word wrap at `width` UTF-16 units; a
 * word longer than the width is split rather than overflowing. Words are
 * text.split(/\s+/), so leading or trailing whitespace yields an empty
 * word. Returns malloc'd lines (NULL when there are none); free with
 * qa_ui_wrapped_lines_free. */
char **qa_ui_wrap_words(const char *text, size_t width, size_t *out_count);
void qa_ui_wrapped_lines_free(char **lines, size_t count);
/* text.trim() over UTF-8: JS white space (TAB LF VT FF CR SP, U+00A0, U+1680,
 * U+2000-U+200A, U+2028, U+2029, U+202F, U+205F, U+3000, U+FEFF) is removed by
 * code point, never by byte (0xA0 alone is a UTF-8 continuation byte, as in
 * "à" or "Р"). Malloc'd; NULL only on OOM. */
char *qa_ui_js_trim(const char *text);

#endif
