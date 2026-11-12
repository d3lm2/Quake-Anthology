/* Port of src/app/bootstrap/native-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/native_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/config_scripts.h"
#include "qa/foundation/support.h"
#include "qa/input/bindings.h"

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
static char *dup_text(const char *text) { return checked(strdup(text)); }

qa_app_stock_native *qa_app_stock_native_config(const qa_content_catalog *catalog, const char *engine_behavior,
                                                const char *const *components, size_t component_count,
                                                const char *redirect, bool with_layers, const char *user_content_root) {
  /* const chain = new Set<ContentId>(): the engine-behavior product and the products it extends. */
  const char **chain = NULL;
  size_t chain_count = 0;
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, engine_behavior);
  for (;;) {
    for (size_t i = 0; i < chain_count; i++)
      if (strcmp(chain[i], product->id) == 0) {
        free((void *)chain);
        return NULL;
      }
    chain = checked(realloc((void *)chain, (chain_count + 1) * sizeof *chain));
    chain[chain_count++] = product->id;
    if (product->expectation.base_product == NULL) break;
    product = qa_content_catalog_product(catalog, product->expectation.base_product);
  }
  for (size_t c = 0; c < component_count; c++) {
    bool found = false;
    for (size_t i = 0; i < chain_count && !found; i++) found = strcmp(chain[i], components[c]) == 0;
    if (!found) {
      free((void *)chain);
      return NULL;
    }
  }
  free((void *)chain);
  const qa_catalog_discovered_product *selected = qa_content_catalog_product(catalog, engine_behavior);
  qa_native_config_product native_product = {
    .family = selected->expectation.family,
    .edition = selected->expectation.edition,
    .content_directory = selected->expectation.content_directory,
    .loose_root = selected->loose_root,
  };
  qa_app_stock_native *native = checked(calloc(1, sizeof *native));
  char *write_redirect = qa_game_write_redirect(redirect, catalog->corpus_root);
  bool placed = qa_native_config_layout_build(&native_product, NULL, write_redirect, redirect != NULL, &native->layout);
  free(write_redirect);
  if (!placed) {
    free(native);
    return NULL;
  }
  if (!with_layers) {
    qa_native_config_init(&native->config, &native->layout, NULL);
    return native;
  }
  char *root = qa_app_console_config_root(user_content_root);
  qa_layer_store_init(&native->store, root);
  free(root);
  /* `${family}/${edition}/${contentDirectory}` */
  const char *family = qa_game_family_name(selected->expectation.family);
  size_t length = strlen(family) + 1 + strlen(selected->expectation.edition) + 1 +
                  strlen(selected->expectation.content_directory) + 1;
  char *id = checked(malloc(length));
  (void)snprintf(id, length, "%s/%s/%s", family, selected->expectation.edition, selected->expectation.content_directory);
  qa_game_layers_init(&native->layers, &native->store, native->layout.format, id);
  free(id);
  native->has_layers = true;
  qa_native_config_init(&native->config, &native->layout, &native->layers);
  return native;
}

void qa_app_stock_native_destroy(qa_app_stock_native *native) {
  if (native == NULL) return;
  qa_native_config_destroy(&native->config);
  if (native->has_layers) {
    qa_game_layers_destroy(&native->layers);
    qa_layer_store_destroy(&native->store);
  }
  qa_native_config_layout_destroy(&native->layout);
  free(native);
}

qa_app_mixed_layers *qa_app_mixed_game_layers(const qa_content_catalog *catalog, const char *reference,
                                              const char *user_content_root) {
  const qa_catalog_product_expectation *expectation = &qa_content_catalog_product(catalog, reference)->expectation;
  bool rerelease = strcmp(expectation->edition, "rerelease") == 0;
  qa_layer_format format = expectation->family == QA_GAME_FAMILY_Q3   ? QA_LAYER_FORMAT_QUAKE3
                           : expectation->family == QA_GAME_FAMILY_Q1 ? (rerelease ? QA_LAYER_FORMAT_KEX_QUAKE : QA_LAYER_FORMAT_QUAKE)
                           : rerelease                                ? QA_LAYER_FORMAT_KEX_QUAKE2
                                                                      : QA_LAYER_FORMAT_QUAKE2;
  qa_app_mixed_layers *mixed = checked(calloc(1, sizeof *mixed));
  char *root = qa_app_console_config_root(user_content_root);
  qa_layer_store_init(&mixed->store, root);
  free(root);
  size_t length = strlen("mixed/") + strlen(reference) + 1;
  char *id = checked(malloc(length));
  (void)snprintf(id, length, "mixed/%s", reference);
  qa_game_layers_init(&mixed->layers, &mixed->store, format, id);
  free(id);
  return mixed;
}

void qa_app_mixed_layers_destroy(qa_app_mixed_layers *mixed) {
  if (mixed == NULL) return;
  qa_game_layers_destroy(&mixed->layers);
  qa_layer_store_destroy(&mixed->store);
  free(mixed);
}

void qa_app_note_local_form(qa_game_layers *layers, const qa_local_form *form) {
  if (layers == NULL || form->arg_count == 0) return;
  const char *name = form->args[0];
  if (form->command == QA_LOCAL_FORM_SET) {
    qa_game_layers_note_local_cvar(layers, name);
    return;
  }
  qa_physical_input input;
  if (!qa_named_physical_input(name, 0, &input)) {
    qa_game_layers_note_local_bind(layers, name);
    return;
  }
  char *physical = checked(qa_physical_input_name(&input));
  qa_game_layers_note_local_bind(layers, physical);
  free(physical);
}

void qa_app_note_local_form_fn(void *layers, const qa_local_form *form) { qa_app_note_local_form(layers, form); }

void qa_app_recipe_components(const qa_executable_recipe *recipe, const char *out[QA_APP_RECIPE_COMPONENT_COUNT]) {
  out[0] = recipe->map.entities.content;
  out[1] = recipe->engine_behavior.content;
  out[2] = recipe->match.content;
  out[3] = recipe->combat.content;
  out[4] = recipe->movement.content;
}

qa_app_stock_native *qa_app_stock_game_folders(const qa_content_catalog *catalog, const qa_executable_recipe *recipe,
                                               const char *native_config_root) {
  const char *components[QA_APP_RECIPE_COMPONENT_COUNT];
  qa_app_recipe_components(recipe, components);
  return qa_app_stock_native_config(catalog, recipe->engine_behavior.content, components, QA_APP_RECIPE_COMPONENT_COUNT,
                                    native_config_root, false, NULL);
}

char *qa_app_stock_game_directory(const qa_content_catalog *catalog, const qa_executable_recipe *recipe,
                                  const char *native_config_root, const char *fallback) {
  qa_app_stock_native *folders = qa_app_stock_game_folders(catalog, recipe, native_config_root);
  char *directory = dup_text(folders != NULL ? folders->layout.directory : fallback);
  qa_app_stock_native_destroy(folders);
  return directory;
}

char **qa_app_native_folders(const qa_native_config_layout *layout, size_t *out_count) {
  *out_count = 0;
  if (layout == NULL) return NULL;
  size_t count = 1 + layout->fallback_count;
  char **folders = checked(calloc(count, sizeof *folders));
  folders[0] = dup_text(layout->directory);
  for (size_t i = 0; i < layout->fallback_count; i++) folders[1 + i] = dup_text(layout->fallbacks[i]);
  *out_count = count;
  return folders;
}

void qa_app_native_folders_free(char **folders, size_t count) {
  if (folders == NULL) return;
  for (size_t i = 0; i < count; i++) free(folders[i]);
  free((void *)folders);
}

qa_native_save_product qa_app_native_save_product(const qa_catalog_product_expectation *expectation) {
  return (qa_native_save_product){
    .family = expectation->family, .edition = expectation->edition, .content_directory = expectation->content_directory};
}

qa_app_native_save_folder *qa_app_native_save_folders(const qa_content_catalog *catalog, const char *redirect,
                                                      size_t *out_count) {
  qa_app_native_save_folder *folders = NULL;
  size_t count = 0;
  for (size_t p = 0; p < catalog->product_count; p++) {
    const qa_catalog_discovered_product *product = &catalog->products[p];
    const char *components[1] = {product->id};
    qa_app_stock_native *native = qa_app_stock_native_config(catalog, product->id, components, 1, redirect, false, NULL);
    if (native == NULL) continue;
    qa_native_save_product save_product = qa_app_native_save_product(&product->expectation);
    qa_native_save_layout layout;
    qa_native_save_layout_build(&native->layout, &save_product, &layout);
    for (size_t s = 0; s < layout.scan_count; s++) {
      /* folders.set(root, { depth: Math.max(folders.get(root)?.depth ?? 0, layout.depth), arrangement }) */
      size_t at = 0;
      while (at < count && strcmp(folders[at].root, layout.scan[s]) != 0) at++;
      if (at == count) {
        folders = checked(realloc(folders, (count + 1) * sizeof *folders));
        folders[count++] = (qa_app_native_save_folder){dup_text(layout.scan[s]), 0, layout.arrangement};
      }
      if (layout.depth > folders[at].depth) folders[at].depth = layout.depth;
      folders[at].arrangement = layout.arrangement;
    }
    qa_native_save_layout_destroy(&layout);
    qa_app_stock_native_destroy(native);
  }
  *out_count = count;
  return folders;
}

void qa_app_native_save_folders_free(qa_app_native_save_folder *folders, size_t count) {
  if (folders == NULL) return;
  for (size_t i = 0; i < count; i++) free(folders[i].root);
  free(folders);
}
