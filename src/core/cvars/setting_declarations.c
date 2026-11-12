/* Port of src/core/cvars/setting-declarations.ts. GPL-2.0-or-later. */
#include "qa/core/cvars/setting_declarations.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

const char *qa_layer_format_name(qa_layer_format format) {
  switch (format) {
    case QA_LAYER_FORMAT_QUAKE: return "quake";
    case QA_LAYER_FORMAT_QUAKE2: return "quake2";
    case QA_LAYER_FORMAT_QUAKE3: return "quake3";
    case QA_LAYER_FORMAT_KEX_QUAKE: return "kex-quake";
    default: return "kex-quake2";
  }
}

const qa_cvar_alias_conversion qa_identity_conversion = {NULL, true, NULL, NULL};

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  memcpy(copy, text, length + 1);
  return copy;
}
/* String(value) as a malloc'd string. */
static char *number_text(double value) {
  char buffer[40];
  qa_net_js_number(value, buffer, sizeof buffer);
  return dup_text(buffer);
}

/* Length of the valid UTF-8 sequence at p, or 0. */
static size_t utf8_sequence(const unsigned char *p) {
  size_t need;
  uint32_t cp, min;
  if (p[0] >= 0xc2 && p[0] <= 0xdf) { need = 1; cp = p[0] & 0x1f; min = 0x80; }
  else if (p[0] >= 0xe0 && p[0] <= 0xef) { need = 2; cp = p[0] & 0x0f; min = 0x800; }
  else if (p[0] >= 0xf0 && p[0] <= 0xf4) { need = 3; cp = p[0] & 0x07; min = 0x10000; }
  else return 0;
  for (size_t i = 1; i <= need; i++) {
    if ((p[i] & 0xc0) != 0x80) return 0;
    cp = (cp << 6) | (p[i] & 0x3f);
  }
  return cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ? 0 : need + 1;
}
/* Number(value) / value.trim() of a setting value. The value reaches here in two forms: settings
 * text held as UTF-8, and console cvar text, a byte string (one byte per TS character). A byte
 * that is not part of a UTF-8 sequence can only be the latter, so it is widened to the
 * character it stands for: byte 0xa0 is U+00A0, white space to Number() and trim(). */
static char *widened(const char *value) {
  size_t length = strlen(value);
  char *wide = (char *)malloc(length * 2 + 1);
  if (wide == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  size_t at = 0;
  for (const unsigned char *p = (const unsigned char *)value; *p != 0;) {
    size_t run = *p < 0x80 ? 1 : utf8_sequence(p);
    if (run != 0) {
      memcpy(wide + at, p, run);
      at += run;
      p += run;
      continue;
    }
    wide[at++] = (char)(0xc0 | (*p >> 6));
    wide[at++] = (char)(0x80 | (*p & 0x3f));
    p++;
  }
  wide[at] = '\0';
  return wide;
}
static double byte_number(const char *value) {
  char *wide = widened(value);
  double number = qa_js_string_to_number(wide);
  free(wide);
  return number;
}

bool qa_finite_number(const char *value, double *out) {
  /* value.trim() === "": only white space, which Number() would read as 0. */
  char *wide = widened(value);
  const char *at = wide;
  for (size_t space; (space = qa_js_white_space_length(at)) > 0;) at += space;
  bool blank = *at == '\0';
  double number = blank ? NAN : qa_js_string_to_number(wide);
  free(wide);
  if (blank || !isfinite(number)) return false;
  if (out != NULL) *out = number;
  return true;
}

/* Quake and Quake II brightness: lower values brighten, so gamma = 1 / r_gamma. */
static char *gamma_read(void *ctx, const char *value) {
  (void)ctx;
  return number_text(1.0 / byte_number(value));
}
static bool gamma_write(void *ctx, const char *value, char **out_value, char **out_message) {
  (void)ctx;
  double number = 0;
  if (!qa_finite_number(value, &number) || number < 1.0 / 3.0 || number > 2) {
    *out_message = dup_text("Gamma must be between 1/3 and 2");
    return false;
  }
  *out_value = number_text(1.0 / number);
  return true;
}

/* Quake II's gl_mode table, by width; its heights follow at 3:4. Widths outside the table are the custom mode, -1. */
static const double q2_mode_widths[] = {320, 400, 512, 640, 800, 960, 1024, 1152, 1280, 1600, 2048};
#define Q2_MODE_COUNT (sizeof q2_mode_widths / sizeof q2_mode_widths[0])
static char *q2_mode_read(void *ctx, const char *value) {
  (void)ctx;
  /* Q2_MODE_WIDTHS.indexOf(Number(value)): strict equality, so NaN is never found. */
  double width = byte_number(value);
  double index = -1;
  for (size_t i = 0; i < Q2_MODE_COUNT; i++) {
    if (q2_mode_widths[i] != width) continue;
    index = (double)i;
    break;
  }
  return number_text(index);
}
static bool q2_mode_write(void *ctx, const char *value, char **out_value, char **out_message) {
  (void)ctx;
  /* Q2_MODE_WIDTHS[Number(value)]: an element only for an integer index in range (-0 names element 0). */
  double index = byte_number(value);
  if (!(index >= 0 && index < (double)Q2_MODE_COUNT) || index != floor(index)) {
    *out_message = dup_text("gl_mode must name a mode from 0 to 10");
    return false;
  }
  *out_value = number_text(q2_mode_widths[(size_t)index]);
  return true;
}

#define IDENTITY {NULL, true, NULL, NULL}
#define GAMMA {NULL, false, gamma_read, gamma_write}
#define Q2_MODE {NULL, false, q2_mode_read, q2_mode_write}
#define F(format) QA_LAYER_FORMAT_BIT(QA_LAYER_FORMAT_##format)
/* TS spelling(): the default summary and usage of a plain spelling. */
#define ANOTHER(target) \
  "Another spelling of " target ", as written by the game that uses this name. Changes the same setting."

static const qa_setting_spelling volume_spellings[] = {
  {"s_volume", F(QUAKE2) | F(QUAKE3), IDENTITY,
   "Alias of volume. Changes the same effects gain and audio menu preference.", "s_volume <0..1>", "s_volume 0.7"},
  {"snd_volume", F(KEX_QUAKE) | F(KEX_QUAKE2), IDENTITY, ANOTHER("volume"), "snd_volume <value>", "snd_volume 0.7"},
};
static const qa_setting_spelling bgmvolume_spellings[] = {
  {"s_musicvolume", F(QUAKE3), IDENTITY,
   "Alias of bgmvolume. Changes the same music gain and audio menu preference.", "s_musicvolume <0..1>",
   "s_musicvolume 0.5"},
  {"ogg_volume", 0, IDENTITY, "Quake II music volume. Alias of bgmvolume and the shared music gain.",
   "ogg_volume <0..1>", "ogg_volume 0.5"},
  {"snd_musicvolume", F(KEX_QUAKE), IDENTITY, ANOTHER("bgmvolume"), "snd_musicvolume <value>", "snd_musicvolume 0.5"},
  {"s_bgvolume", F(KEX_QUAKE2), IDENTITY, ANOTHER("bgmvolume"), "s_bgvolume <value>", "s_bgvolume 0.5"},
};
static const qa_setting_spelling gamma_spellings[] = {
  {"gamma", F(QUAKE) | F(KEX_QUAKE), GAMMA,
   "Quake brightness convention: lower values brighten. Alias of r_gamma with gamma = 1 / r_gamma.",
   "gamma <1/3..2>", "gamma 0.5"},
  {"vid_gamma", F(QUAKE2), GAMMA,
   "Quake II brightness convention: lower values brighten. Alias of r_gamma with vid_gamma = 1 / r_gamma.",
   "vid_gamma <1/3..2>", "vid_gamma 0.5"},
};
static const qa_setting_spelling fov_spellings[] = {
  {"cg_fov", F(QUAKE3), IDENTITY, ANOTHER("fov"), "cg_fov <value>", "cg_fov 100"},
  {"r_fov", F(KEX_QUAKE), IDENTITY, ANOTHER("fov"), "r_fov <value>", "r_fov 100"},
  {"fov_0", F(KEX_QUAKE2), IDENTITY, ANOTHER("fov"), "fov_0 <value>", "fov_0 100"},
};
static const qa_setting_spelling fullscreen_spellings[] = {
  {"vid_fullscreen", F(QUAKE2), IDENTITY, ANOTHER("r_fullscreen"), "vid_fullscreen <value>", "vid_fullscreen 1"},
};
static const qa_setting_spelling width_spellings[] = {
  {"vid_config_x", F(QUAKE), IDENTITY, ANOTHER("r_customwidth"), "vid_config_x <value>", "vid_config_x 1280"},
  {"v_width", F(KEX_QUAKE) | F(KEX_QUAKE2), IDENTITY, ANOTHER("r_customwidth"), "v_width <value>", "v_width 1280"},
  {"gl_mode", F(QUAKE2), Q2_MODE,
   "Quake II video mode index. Changes the custom width to the mode's width; -1 reads back for widths outside the table.",
   "gl_mode <0..10>", "gl_mode 8"},
};
static const qa_setting_companion width_companions[] = {{QA_LAYER_FORMAT_QUAKE3, "r_mode", "-1"}};
static const qa_setting_spelling height_spellings[] = {
  {"vid_config_y", F(QUAKE), IDENTITY, ANOTHER("r_customheight"), "vid_config_y <value>", "vid_config_y 720"},
  {"v_height", F(KEX_QUAKE) | F(KEX_QUAKE2), IDENTITY, ANOTHER("r_customheight"), "v_height <value>", "v_height 720"},
};
static const qa_setting_spelling sensitivity_spellings[] = {
  {"cl_sensitivity", F(KEX_QUAKE), IDENTITY, ANOTHER("sensitivity"), "cl_sensitivity <value>", "cl_sensitivity 4.5"},
};

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
const qa_engine_setting qa_engine_settings[] = {
  {"volume", QA_ENGINE_SETTING_SHARED, F(QUAKE), volume_spellings, COUNT(volume_spellings), NULL, 0},
  {"bgmvolume", QA_ENGINE_SETTING_SHARED, F(QUAKE), bgmvolume_spellings, COUNT(bgmvolume_spellings), NULL, 0},
  {"r_gamma", QA_ENGINE_SETTING_SHARED, F(QUAKE3), gamma_spellings, COUNT(gamma_spellings), NULL, 0},
  {"fov", QA_ENGINE_SETTING_SHARED, F(QUAKE) | F(QUAKE2), fov_spellings, COUNT(fov_spellings), NULL, 0},
  {"r_fullscreen", QA_ENGINE_SETTING_SHARED, F(QUAKE3), fullscreen_spellings, COUNT(fullscreen_spellings), NULL, 0},
  {"r_customwidth", QA_ENGINE_SETTING_SHARED, F(QUAKE3), width_spellings, COUNT(width_spellings), width_companions,
   COUNT(width_companions)},
  {"r_customheight", QA_ENGINE_SETTING_SHARED, F(QUAKE3), height_spellings, COUNT(height_spellings), NULL, 0},
  {"sensitivity", QA_ENGINE_SETTING_INPUT, F(QUAKE) | F(QUAKE2) | F(QUAKE3), sensitivity_spellings,
   COUNT(sensitivity_spellings), NULL, 0},
  /* The rerelease Quake II keeps m_pitch at 0.01325 with a matching sensitivity scale, so those two stay with that game. */
  {"m_pitch", QA_ENGINE_SETTING_INPUT, F(QUAKE) | F(QUAKE2) | F(QUAKE3) | F(KEX_QUAKE), NULL, 0, NULL, 0},
};
const size_t qa_engine_setting_count = COUNT(qa_engine_settings);

static bool registered(qa_cvar_registry *cvars, const char *name) {
  qa_cvar_read found;
  if (!qa_cvar_registry_find(cvars, name, &found)) return false;
  qa_cvar_read_free(&found);
  return true;
}

void qa_register_setting_aliases(qa_cvar_registry *cvars, qa_engine_setting_registry registry) {
  for (size_t i = 0; i < qa_engine_setting_count; i++) {
    const qa_engine_setting *setting = &qa_engine_settings[i];
    if (setting->registry != registry || !registered(cvars, setting->canonical)) continue;
    for (size_t j = 0; j < setting->spelling_count; j++) {
      const qa_setting_spelling *entry = &setting->spellings[j];
      if (registered(cvars, entry->name)) continue;
      qa_cvar_alias alias;
      memset(&alias, 0, sizeof alias);
      alias.name = entry->name;
      alias.target = setting->canonical;
      alias.conversion = entry->conversion;
      alias.documentation.summary = entry->summary;
      alias.documentation.usage = entry->usage;
      /* The declaration table is static, so the one-element examples list is the example field itself. */
      alias.documentation.examples = &entry->example;
      alias.documentation.example_count = 1;
      qa_cvar_registry_register_alias(cvars, alias);
    }
  }
}
