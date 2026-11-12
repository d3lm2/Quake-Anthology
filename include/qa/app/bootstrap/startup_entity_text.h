/*
 * The map entity text startup-selection.ts reads to find a map's classnames: the entity lump decoded with
 * `new TextDecoder().decode(bytes)` (and, for the monster roster, `.replace(/\0+$/, "")`), then parsed by
 * parseQ1Entities (formats/q1-map), parseQ2Entities (content/q2/foundation/fields.ts) or parseEntities
 * (formats/q3-map). There is no corresponding .ts file: those parsers run here over the decoded UTF-16 code units, as
 * TS runs them over the decoded string, because the C ports of them read NUL-terminated byte strings (an interior
 * U+0000, which the Q2 and Q3 grammars read past, would end the C text) and count bytes where TS counts units (token
 * limits, error positions). GPL-2.0-or-later.
 */
#ifndef QA_APP_BOOTSTRAP_STARTUP_ENTITY_TEXT_H
#define QA_APP_BOOTSTRAP_STARTUP_ENTITY_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum qa_app_entity_grammar {
  QA_APP_ENTITY_GRAMMAR_Q1,
  QA_APP_ENTITY_GRAMMAR_Q2,
  QA_APP_ENTITY_GRAMMAR_Q3,
} qa_app_entity_grammar;

/*
 * The classname of each entity, in order: q1EntityValue(entity, "classname") ?? "", the Q2 spawn fields' classname
 * (after ED_NewString escapes; `rerelease` selects the 512-unit token limit over 128), or entity.get("classname") ?? ""
 * for Q3 (`name` is the parse source name, NULL for "<entities>"). Malloc'd array of malloc'd UTF-8 strings (U+0000 is
 * C0 80 and a lone surrogate its three-byte form); free with qa_app_entity_classnames_free. A parse error raises the TS
 * error: Error for Q1 and Q2, TextParseError for Q3, with TS's message.
 */
char **qa_app_entity_classnames(const uint8_t *bytes, size_t length, qa_app_entity_grammar grammar, bool rerelease,
                                const char *name, bool strip_trailing_nuls, size_t *out_count);
void qa_app_entity_classnames_free(char **names, size_t count);

#endif
