/*
 * `new TextDecoder().decode(bytes)` for the text formats the TypeScript reads
 * that way (shader scripts, MD5 mesh/anim/scale text). There is no
 * corresponding .ts file: this is the WHATWG UTF-8 decoder itself. A leading
 * byte order mark is dropped and every maximal invalid subpart becomes U+FFFD,
 * so the result is UTF-8 (docs/PORTING.md, Strings). A decoded U+0000 is
 * written as the two bytes C0 80 (as Java's modified UTF-8 does), so the C
 * string never ends early: TS keeps the text after it (the shader tokenizer
 * reads it as white space, the MD5 grammar as a token character). Consumers
 * that count or print UTF-16 units read C0 80 as U+0000
 * (qa_tokenizer_init_text, materials, formats/q3_model/text.c).
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_TEXT_DECODER_H
#define QA_FORMATS_TEXT_DECODER_H

#include <stddef.h>
#include <stdint.h>

/* Malloc'd, NUL-terminated; *out_length (may be NULL) excludes the terminator.
 * NULL only on allocation failure. */
char *qa_text_decoder_decode(const uint8_t *bytes, size_t length, size_t *out_length);

#endif
