/*
 * Port of src/core/cvars/setting-declarations.ts: the one declaration of how
 * each game spells the engine's shared settings. The canonical cvar is the
 * engine's own (r_gamma, volume, bgmvolume, sensitivity, fov, ...); every other
 * spelling is registered as a cvar alias in every dialect. `formats` only
 * chooses which spelling a game's native file is written and read with.
 * C accommodations:
 *  - LayerFormat is an enum; a `formats` list is a bit set of
 *    QA_LAYER_FORMAT_BIT(format).
 *  - CvarAlias["conversion"] is qa_cvar_alias_conversion (is_identity, or
 *    read/write returning malloc'd text).
 *  - finiteNumber's null is a false return.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_CVARS_SETTING_DECLARATIONS_H
#define QA_CORE_CVARS_SETTING_DECLARATIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/core/cvars/index.h"

/* TS LayerFormat. */
typedef enum qa_layer_format {
  QA_LAYER_FORMAT_QUAKE = 0,
  QA_LAYER_FORMAT_QUAKE2,
  QA_LAYER_FORMAT_QUAKE3,
  QA_LAYER_FORMAT_KEX_QUAKE,
  QA_LAYER_FORMAT_KEX_QUAKE2
} qa_layer_format;
#define QA_LAYER_FORMAT_COUNT 5
#define QA_LAYER_FORMAT_BIT(format) (1u << (unsigned)(format))
/* "quake", "quake2", "quake3", "kex-quake", "kex-quake2". */
const char *qa_layer_format_name(qa_layer_format format);

/* TS SettingSpelling. */
typedef struct qa_setting_spelling {
  const char *name;
  /* The native files that spell the setting this way; 0 for a name that is only accepted. */
  uint32_t formats;
  /* From the canonical value to this spelling's, and back. */
  qa_cvar_alias_conversion conversion;
  const char *summary;
  const char *usage;
  const char *example;
} qa_setting_spelling;

/* TS EngineSetting["registry"]: the shared client settings, or each seat's input settings. */
typedef enum qa_engine_setting_registry {
  QA_ENGINE_SETTING_SHARED = 0,
  QA_ENGINE_SETTING_INPUT
} qa_engine_setting_registry;

/* A cvar a game needs at a fixed value for the setting to take effect. */
typedef struct qa_setting_companion {
  qa_layer_format format;
  const char *name;
  const char *value;
} qa_setting_companion;

/* TS EngineSetting. */
typedef struct qa_engine_setting {
  const char *canonical;
  qa_engine_setting_registry registry;
  /* The native files that spell the setting with the canonical name itself. */
  uint32_t formats;
  const qa_setting_spelling *spellings;
  size_t spelling_count;
  const qa_setting_companion *companions; /* NULL when the TS field is absent */
  size_t companion_count;
} qa_engine_setting;

/* TS identityConversion. */
extern const qa_cvar_alias_conversion qa_identity_conversion;
/* TS finiteNumber: a trimmed, finite number; false for the TS null. */
bool qa_finite_number(const char *value, double *out);

/* TS ENGINE_SETTINGS, in declaration order. */
extern const qa_engine_setting qa_engine_settings[];
extern const size_t qa_engine_setting_count;

/* TS registerSettingAliases: registers every spelling of the settings the
 * registry owns as an alias of the canonical cvar. A name the registry already
 * has as a real cvar is left as it is. */
void qa_register_setting_aliases(qa_cvar_registry *cvars, qa_engine_setting_registry registry);

#endif
