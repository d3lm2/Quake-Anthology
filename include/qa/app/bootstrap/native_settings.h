/*
 * Port of src/app/bootstrap/native-settings.ts: a stock game takes every
 * selected component from one product or mod (the engine-behavior product and
 * the products it extends). Such a game reads and writes the configuration
 * files the original keeps in its own folders; any mixed combination keeps to
 * the user store.
 * C accommodations:
 *  - A TS NativeConfig owns its layout and, when layered, a GameLayers over a
 *    LayerStore. qa_app_stock_native is that bundle: one allocation owning the
 *    layout, the store, the layers and the config, released with
 *    qa_app_stock_native_destroy. `config.layers` is NULL when the TS call
 *    leaves `layers` out.
 *  - mixedGameLayers' GameLayers with its LayerStore is qa_app_mixed_layers.
 *  - Null results are NULL returns; returned strings are malloc'd.
 * GPL-2.0-or-later.
 */
#ifndef QA_APP_BOOTSTRAP_NATIVE_SETTINGS_H
#define QA_APP_BOOTSTRAP_NATIVE_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/core/commands/index.h"
#include "qa/settings/layers.h"
#include "qa/settings/native_config.h"
#include "qa/settings/native_saves.h"

/* A TS NativeConfig with what it owns. */
typedef struct qa_app_stock_native {
  qa_native_config_layout layout;
  bool has_layers;
  qa_layer_store store;
  qa_game_layers layers;
  qa_native_config config;
} qa_app_stock_native;

/* TS stockNativeConfig(catalog, engineBehavior, components, redirect, layers).
 * `redirect` NULL is undefined. `with_layers` false is the TS `layers`
 * undefined; with it, `user_content_root` (NULL = undefined) is where the
 * engine-wide layer files live. NULL is the TS null. Raises as catalog.product
 * does for an unknown product. */
qa_app_stock_native *qa_app_stock_native_config(const qa_content_catalog *catalog, const char *engine_behavior,
                                                const char *const *components, size_t component_count,
                                                const char *redirect, bool with_layers, const char *user_content_root);
void qa_app_stock_native_destroy(qa_app_stock_native *native);

/* TS mixedGameLayers: the layers of a mixed combination, which keeps no native
 * file: the same high level and overrides, keyed by the combination's
 * engine-behavior product. */
typedef struct qa_app_mixed_layers {
  qa_layer_store store;
  qa_game_layers layers;
} qa_app_mixed_layers;
qa_app_mixed_layers *qa_app_mixed_game_layers(const qa_content_catalog *catalog, const char *reference,
                                              const char *user_content_root);
void qa_app_mixed_layers_destroy(qa_app_mixed_layers *mixed);

/* TS noteLocalForm: `bind -local`, `unbind -local` and `set -local` keep the
 * entry to this game. `layers` NULL is undefined. */
void qa_app_note_local_form(qa_game_layers *layers, const qa_local_form *form);
/* A qa_local_form_fn whose ctx is a `qa_game_layers *` (NULL allowed). */
void qa_app_note_local_form_fn(void *layers, const qa_local_form *form);

/* TS recipeComponents: map entities, engine behavior, match, combat, movement
 * (borrowed from the recipe). */
#define QA_APP_RECIPE_COMPONENT_COUNT 5
void qa_app_recipe_components(const qa_executable_recipe *recipe, const char *out[QA_APP_RECIPE_COMPONENT_COUNT]);

/* TS stockGameFolders(catalog, recipe, options): the stock game's layout, or
 * NULL (a mixed combination). Owned by the caller (qa_app_stock_native_destroy). */
qa_app_stock_native *qa_app_stock_game_folders(const qa_content_catalog *catalog, const qa_executable_recipe *recipe,
                                               const char *native_config_root);
/* TS stockGameDirectory(content, options, fallback). Malloc'd. */
char *qa_app_stock_game_directory(const qa_content_catalog *catalog, const qa_executable_recipe *recipe,
                                  const char *native_config_root, const char *fallback);

/* TS nativeFolders(native): the folders a stock game's demos and saves are
 * read from, writes first. `layout` is the config's layout (NULL = the TS
 * null config). Malloc'd list of malloc'd paths (NULL and count 0 for null). */
char **qa_app_native_folders(const qa_native_config_layout *layout, size_t *out_count);
void qa_app_native_folders_free(char **folders, size_t count);

/* TS nativeSaveFolders: every installed stock game's native save folders, for
 * the saved-game lists. */
typedef struct qa_app_native_save_folder {
  char *root; /* owned */
  int depth;
  qa_native_save_arrangement arrangement;
} qa_app_native_save_folder;
qa_app_native_save_folder *qa_app_native_save_folders(const qa_content_catalog *catalog, const char *redirect,
                                                      size_t *out_count);
void qa_app_native_save_folders_free(qa_app_native_save_folder *folders, size_t count);

/* The expectation fields nativeSaveLayout reads, as its product argument. */
qa_native_save_product qa_app_native_save_product(const qa_catalog_product_expectation *expectation);

#endif
