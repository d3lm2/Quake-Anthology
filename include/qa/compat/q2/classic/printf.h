/* Port of src/compat/q2/classic/printf.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_CLASSIC_PRINTF_H
#define QA_COMPAT_Q2_CLASSIC_PRINTF_H
#include <stddef.h>
#include "qa/compat/q2/classic/layout.h"

/* TS classicPrintfLayouts: fills out (pass NULL/0 to count), returns total. */
size_t qa_compat_q2_classic_printf_layouts(const char *format, qa_guest_value_layout *out, size_t capacity);
/* Malloc'd result; caller frees. NULL only on OOM. */
char *qa_compat_q2_classic_printf(const qa_compat_q2_memory *memory, const char *format,
                                  const qa_guest_call_value *arguments, size_t count);
/* The same text with its length: %c of 0 is a character of the result (TS "\0"), which the terminator of the C
 * string above cannot show. The result is still terminated after *out_length bytes. */
char *qa_compat_q2_classic_printf_n(const qa_compat_q2_memory *memory, const char *format,
                                    const qa_guest_call_value *arguments, size_t count, size_t *out_length);
#endif
