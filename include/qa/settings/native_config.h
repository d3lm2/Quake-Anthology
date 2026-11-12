/*
 * Port of src/settings/native-config.ts: the configuration files a stock game
 * keeps in its own folders, with the original name, format and location. A
 * stock game is every selected component coming from one product or mod; those
 * files are then the source of truth for bindings and archived variables.
 * C accommodations:
 *  - NativeConfigFormat is qa_layer_format (the same five names).
 *  - NativeBindings (`[key, command]` pairs) are qa_layer_pair arrays; results
 *    are qa_native_bindings (owned strings).
 *  - Files are read as Latin-1, as the TS does (`toString("latin1")`), and held
 *    as UTF-8, which is what the TS string becomes when it is written back.
 *  - async methods are synchronous; I/O failures other than a missing file
 *    raise Error, as the TS promise rejects.
 *  - filterArchive's generic entry list becomes a per-name predicate.
 *  - Null results are false / NULL returns.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_NATIVE_CONFIG_H
#define QA_SETTINGS_NATIVE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/capture/index.h"
#include "qa/contracts/content.h"
#include "qa/settings/layers.h"

/* TS NativeConfigProduct. */
typedef struct qa_native_config_product {
  qa_game_family family;
  const char *edition;
  const char *content_directory;
  const char *loose_root; /* NULL is the TS null */
} qa_native_config_product;

/* TS NativeConfigFile. Strings are owned by the layout. */
typedef struct qa_native_config_file {
  char *name;
  char *path;
  /* Where writes go instead of `path`, when the layout is redirected; NULL when it is not. */
  char *redirected;
  /* Whether reads prefer the redirected copy; shared test roots only receive writes. */
  bool reads_redirect;
  bool binds;
} qa_native_config_file;

/* TS NativeConfigLayout. */
typedef struct qa_native_config_layout {
  qa_layer_format format;
  /* The startup script name whose read these files answer: "config.cfg" or "q3config.cfg" (static). */
  const char *script;
  /* Files in the order the original executes them. */
  qa_native_config_file *files;
  size_t file_count;
  /* The folder the original writes its demos, screenshots, logs and saves into. Writes go to `directory`; reads
   * also look in `fallbacks`. */
  char *directory;
  char **fallbacks;
  size_t fallback_count;
  /* Where the original puts unnamed screenshots, beneath `directory` (static strings). */
  qa_screenshot_naming screenshots;
} qa_native_config_layout;

/* TS nativeConfigLayout. `home` NULL is homedir(); `redirect` NULL is undefined; the TS default for
 * reads_redirect is true. False is the TS null (the product keeps no native files this engine can place).
 * On true `out` is owned by the caller (qa_native_config_layout_destroy). */
bool qa_native_config_layout_build(const qa_native_config_product *product, const char *home, const char *redirect,
                                   bool reads_redirect, qa_native_config_layout *out);
void qa_native_config_layout_destroy(qa_native_config_layout *layout);

/* An owned NativeBindings list. */
typedef struct qa_native_bindings {
  qa_layer_pair *pairs; /* key and command are owned */
  size_t count;
} qa_native_bindings;
void qa_native_bindings_free(qa_native_bindings *bindings);

/* TS NativeWriteContext: the game's reset value for a cvar (NULL fn when absent). */
typedef struct qa_native_write_context {
  qa_layer_reset_value_fn reset_value;
  void *reset_value_ctx;
} qa_native_write_context;

/* TS absorbSession's result: `cvars` is the TS null when has_cvars is false. */
typedef struct qa_native_session {
  qa_native_bindings bindings;
  char **cvars;
  size_t cvar_count;
  bool has_cvars;
} qa_native_session;
void qa_native_session_free(qa_native_session *session);
/* TS absorbSession: takes a running game's state into the layers and answers the bindings and variable lines
 * to write. `cvars` NULL (has_cvars false) is the TS null. Raises Error "Settings layers were not read before
 * they were written" when the layers are not ready. */
void qa_absorb_session(qa_game_layers *layers, qa_layer_format format, const qa_layer_pair *bindings,
                       size_t binding_count, const char *const *cvars, size_t cvar_count, bool has_cvars,
                       const qa_native_write_context *context, qa_native_session *out);

/* TS NativeConfig. The layout and layers are borrowed; `layers` NULL reads and writes the files as they are. */
typedef struct qa_native_config {
  const qa_native_config_layout *layout;
  qa_game_layers *layers;
  size_t loaded_files;
  qa_layer_map names; /* Set of lowercase variable names the last read found */
  bool bound;
} qa_native_config;
void qa_native_config_init(qa_native_config *config, const qa_native_config_layout *layout, qa_game_layers *layers);
void qa_native_config_destroy(qa_native_config *config);
/* TS loaded: true when the last read found a file that held at least one binding or variable. */
bool qa_native_config_loaded(const qa_native_config *config);
/* TS hasBindings: true when the last read found a binding. */
bool qa_native_config_has_bindings(const qa_native_config *config);
/* TS snapshot: what the files hold now, in this engine's spelling of keys and commands. `out` is initialized by the call. */
void qa_native_config_snapshot(const qa_native_config *config, qa_layer_snapshot *out);
/* TS readScript: the malloc'd text the original would execute for its saved configuration, or NULL when no
 * file holds anything. The text is console text, one byte per code unit like every other console script (the
 * files are read as Latin-1); a character above U+00FF, which only a layers value from JSON can hold, raises
 * RangeError "Command text requires source bytes" (TS raises it when the script executes).
 * Console text: the bindings, archive commands and reset values passed to qa_absorb_session,
 * qa_native_config_write and qa_native_config_write_bindings are console text too; the files are written as UTF-8
 * (TS writeAtomic), and the layers hold UTF-8. The session qa_absorb_session answers is UTF-8. */
char *qa_native_config_read_script(qa_native_config *config);
/* TS readBindings: false is the TS null (the files hold no bindings). Keys and commands are console text; one
 * holding a character above U+00FF stays UTF-8. */
bool qa_native_config_read_bindings(qa_native_config *config, qa_native_bindings *out);
/* TS filterArchive, per entry: true when the JSON archive entry `name` (console text, as cvar_archives.c loads it)
 * stays (the native files do not already answer it). */
bool qa_native_config_keeps_archive_entry(const qa_native_config *config, const char *name);
/* TS suppressesProfileBindings. */
bool qa_native_config_suppresses_profile_bindings(const qa_native_config *config);
/* TS write: what the original writes at quit: bindings, then archived variables (archive commands in the
 * dialect's own spelling). `context` NULL is the TS default `{}`. */
void qa_native_config_write(qa_native_config *config, const qa_layer_pair *bindings, size_t binding_count,
                            const char *const *cvars, size_t cvar_count, const qa_native_write_context *context);
/* TS writeBindings: replaces only the bindings, keeping every other line. With `local` the change stays this
 * game's override. */
void qa_native_config_write_bindings(qa_native_config *config, const qa_layer_pair *bindings, size_t binding_count,
                                     const qa_native_write_context *context, bool local);

#endif
