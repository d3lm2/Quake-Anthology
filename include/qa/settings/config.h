/*
 * Port of src/settings/config.ts: seat settings, global input tuning
 * and gyro profile documents, the settingsPath/writeAtomic helpers,
 * and ConfigStore. C accommodations:
 *  - TS `unknown` document inputs become qa_save_value trees (the
 *    persistence checkpoint tree, built by qa_parse_json_value for
 *    JSON.parse sites). A NULL tree or a QA_SAVE_NULL node is the TS
 *    null the loaders admit as "absent".
 *  - GamepadTuning/StickCurve and MouseTuning come from their TS
 *    owners qa/input/gamepad.h and qa/input/mouse.h (included
 *    below); qa_validate_gamepad_tuning runs at the parse site.
 *    Curve kind and gyro yawAxis convert at the JSON boundary in
 *    config.c (radial/axial, "y"/"z" <-> yaw_axis_z). ControllerSelection
 *    (src/platform/controller.ts) is still mirrored below: the
 *    platform header's borrowed serial differs from the owned copy
 *    SeatSettings persists.
 *  - Bun.file/node:fs/promises become synchronous stdio calls.
 *    Loader/save results that were Promise<T | null> become a bool
 *    (false is the TS null/absent) plus an out struct; saves return
 *    false only on I/O or allocation failure (the TS rejects).
 *  - Plain TS Errors abort with the identical message, as elsewhere
 *    in the port. JSON text written by saves is produced locally;
 *    numbers use shortest round-tripping decimal.
 *  - TS strings are UTF-16; the 8192 gyro length gate and the gyro
 *    serial length gate count bytes here, which can differ for
 *    non-ASCII serials/documents (see docs/PORTING.md).
 * Ownership: parsed settings own their strings/arrays; free with the
 * matching qa_*_free. qa_settings_path / qa_config_store_load_text
 * return malloc'd buffers the caller frees.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_CONFIG_H
#define QA_SETTINGS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/contracts/common.h"
#include "qa/contracts/ui.h"
#include "qa/core/cvars/index.h"
#include "qa/input/gamepad.h"
#include "qa/input/mouse.h"
#include "qa/persistence/value.h"

typedef struct qa_command_buffer qa_command_buffer;

/* TS ControllerSelection (src/platform/controller.ts). guid is the 32
 * hex digits (device/serial); serial is an owned copy (serial kind). */
typedef enum qa_settings_controller_selection_kind {
  QA_CONTROLLER_AUTOMATIC,
  QA_CONTROLLER_NONE,
  QA_CONTROLLER_DEVICE,
  QA_CONTROLLER_SERIAL
} qa_settings_controller_selection_kind;
typedef struct qa_settings_controller_selection {
  qa_settings_controller_selection_kind kind;
  char guid[33];   /* device/serial */
  double ordinal;  /* device */
  char *serial;    /* serial, owned */
} qa_settings_controller_selection;

/* The keys of a TS SeatSettings object, for its JSON.stringify key order. */
typedef enum qa_seat_settings_key {
  QA_SEAT_KEY_VERSION,
  QA_SEAT_KEY_BINDINGS,
  QA_SEAT_KEY_ALWAYS_RUN,
  QA_SEAT_KEY_GAMEPAD,
  QA_SEAT_KEY_MOUSE,
  QA_SEAT_KEY_HISTORY,
  QA_SEAT_KEY_RUMBLE,
  QA_SEAT_KEY_RUMBLE_STRENGTH,
  QA_SEAT_KEY_CONTROLLER,
  QA_SEAT_KEY_COUNT
} qa_seat_settings_key;

/* TS SeatSettings. Binding action/axis/direction strings point at
 * vocabulary literals; command texts and history strings are owned.
 * A bound command's text is console text (one byte per code unit); a
 * history line is UTF-8 as the console field holds it. */
typedef struct qa_seat_settings {
  int32_t version; /* TS literal 1 */
  qa_input_binding *bindings;
  size_t binding_count;
  qa_gamepad_tuning gamepad;
  qa_mouse_tuning mouse;
  bool has_always_run;
  bool always_run;
  char **history;
  size_t history_count;
  bool rumble;
  double rumble_strength; /* parser always resolves (default 1) */
  /* TS rumbleStrength undefined: a document built without the key (JSON.stringify leaves it out). The parser always
   * sets the value, so a parsed or zeroed struct has it. */
  bool rumble_strength_absent;
  qa_settings_controller_selection controller;
  /* The TS object's key order, which JSON.stringify keeps. key_count 0 is the parser's order (version, bindings,
   * alwaysRun, gamepad, mouse, history, rumble, rumbleStrength, controller); otherwise key_order[0..key_count) lists
   * the keys as the object literal and its spreads inserted them. alwaysRun is written only with has_always_run and
   * rumbleStrength only without rumble_strength_absent. */
  uint8_t key_order[QA_SEAT_KEY_COUNT];
  uint8_t key_count;
} qa_seat_settings;
/* A spread that sets `key` ({ ...settings, key: value }): a key the object does not hold yet is appended after the
 * others. Call before marking the key present (has_always_run, rumble_strength_absent). */
void qa_seat_settings_note_key(qa_seat_settings *settings, qa_seat_settings_key key);
void qa_parse_seat_settings(const qa_save_value *value, qa_seat_settings *out);
void qa_seat_settings_free(qa_seat_settings *settings);

/** TS GlobalInputTuning: input tuning shared by every game; bindings
 * stay in per-product seat settings. */
typedef struct qa_global_input_tuning {
  int32_t version; /* TS literal 1 */
  qa_mouse_tuning mouse;
  bool has_always_run;
  bool always_run;
  bool rumble;
  double rumble_strength;
} qa_global_input_tuning;
void qa_parse_global_input_tuning(const qa_save_value *value,
                                  qa_global_input_tuning *out);

/* TS GyroProfileIdentity / GyroProfile. */
typedef struct qa_gyro_profile {
  int32_t version; /* TS literal 1 */
  bool identity_is_device; /* false is "seat" */
  char guid[33];           /* device */
  char *serial;            /* device, owned */
  qa_gyro_tuning tuning;
} qa_gyro_profile;
void qa_parse_gyro_profile(const qa_save_value *value, qa_gyro_profile *out);
void qa_gyro_profile_free(qa_gyro_profile *profile);

/* Malloc'd resolved path under root; aborts (TS Error) when the name
 * escapes the writable directory. NULL only on OOM. */
char *qa_settings_path(const char *root, const char *name);
/* mkdir -p the parent, write a unique sibling temp (0600), rename.
 * False on I/O or allocation failure; the temp is always removed. */
bool qa_write_atomic(const char *path, const void *contents, size_t length);
bool qa_write_atomic_text(const char *path, const char *contents);
/* The error TS writeAtomic (and so every ConfigStore save) rejects with, for the last false return of a save on this
 * thread: the Node fs Error ("EACCES: permission denied, open '<path>.<uuid>.tmp'", "EISDIR: illegal operation on a
 * directory, rename '<temp>' -> '<path>'", ...). With no file-system failure recorded (allocation failure) it raises
 * RangeError "Out of memory". */
noreturn void qa_settings_raise_write_failure(void);

/** TS ConfigStore. Owns a copy of root. `legacy` (borrowed, NULL when absent)
 * is the store a document lived in before it moved here: loadSeat and loadText
 * fall back to it so nothing saved earlier is lost; writes only go to root. */
typedef struct qa_config_store {
  char *root;
  const struct qa_config_store *legacy;
} qa_config_store;
bool qa_config_store_init(qa_config_store *store, const char *root);
/* TS `new ConfigStore(root, legacy)`; the legacy store must outlive this one. */
bool qa_config_store_init_legacy(qa_config_store *store, const char *root,
                                 const qa_config_store *legacy);
void qa_config_store_destroy(qa_config_store *store);

/* Malloc'd file bytes (NUL-terminated for convenience; *out_length
 * excludes the terminator), or NULL when the file is absent. */
char *qa_config_store_load_text(const qa_config_store *store, const char *name,
                                size_t *out_length);
bool qa_config_store_dump(const qa_config_store *store, const char *name,
                          const char *contents);

/* False is the TS null (absent or undecodable document). The loads below
 * follow TS ConfigStore.decoded: a document that is not JSON or fails its
 * schema is renamed to `<name>.bad` and loads as absent. */
bool qa_config_store_load_gyro(const qa_config_store *store, const char *name,
                               qa_gyro_profile *out);
bool qa_config_store_save_gyro(const qa_config_store *store, const char *name,
                               const qa_gyro_profile *profile);
/* keyboard_seat < 0 is the TS null. */
bool qa_config_store_save_input_routing(const qa_config_store *store,
                                        const char *name, double keyboard_seat,
                                        bool has_keyboard_seat);
bool qa_config_store_load_input_routing(const qa_config_store *store,
                                        const char *name, double *out_seat,
                                        bool *out_has_seat);
bool qa_config_store_load_seat(const qa_config_store *store, const char *name,
                               qa_seat_settings *out);
bool qa_config_store_save_seat(const qa_config_store *store, const char *name,
                               const qa_seat_settings *settings);
bool qa_config_store_load_global_tuning(const qa_config_store *store,
                                        const char *name,
                                        qa_global_input_tuning *out);
bool qa_config_store_save_global_tuning(const qa_config_store *store,
                                        const char *name,
                                        const qa_global_input_tuning *tuning);
/* Compact JSON.stringify texts of a SeatSettings, an InputBinding list and a
 * GamepadTuning (key order follows the parsers). Malloc'd; NULL on OOM. */
char *qa_seat_settings_json(const qa_seat_settings *settings);
char *qa_input_bindings_json(const qa_input_binding *bindings, size_t count);
char *qa_gamepad_tuning_json(const qa_gamepad_tuning *tuning);
/* bindings are saved console binding lines (may be NULL/0). */
bool qa_config_store_save_cvars(const qa_config_store *store, const char *name,
                                qa_cvar_registry *cvars,
                                const char *const *bindings,
                                size_t binding_count);
/* Appends the file text plus "\n" as a script chunk. Aborts when the
 * file cannot be read (the TS readFile rejects). */
void qa_config_store_execute(const qa_config_store *store, const char *name,
                             qa_command_buffer *commands,
                             qa_command_context context);

#endif
