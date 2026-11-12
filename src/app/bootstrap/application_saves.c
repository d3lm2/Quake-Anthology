/* Port of src/app/bootstrap/application.ts, region 643-653: autosaveLevel.
 * Separate from application_core.c because it calls saveGame (P5), so the
 * constructor and storage link without it. Layout and status:
 * docs/application_layout.md. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include "qa/app/bootstrap/simulation/native_q2_rerelease_pak.h"
#include "qa/foundation/caught_message.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
/* node:fs/promises mkdir(directory, { recursive: true }); a failure raises
 * the Node error text ("<CODE>: <description>, mkdir '<path>'"). */
static void node_errno_text(int error, const char **code, const char **description) {
  switch (error) {
    case EACCES: *code = "EACCES"; *description = "permission denied"; return;
    case EPERM: *code = "EPERM"; *description = "operation not permitted"; return;
    case ENOTDIR: *code = "ENOTDIR"; *description = "not a directory"; return;
    case EEXIST: *code = "EEXIST"; *description = "file already exists"; return;
    case EROFS: *code = "EROFS"; *description = "read-only file system"; return;
    case ENOSPC: *code = "ENOSPC"; *description = "no space left on device"; return;
    case ENAMETOOLONG: *code = "ENAMETOOLONG"; *description = "name too long"; return;
    case ELOOP: *code = "ELOOP"; *description = "too many symbolic links encountered"; return;
    default: *code = "EIO"; *description = strerror(error); return;
  }
}
static void mkdir_recursive(const char *directory) {
  char *copy = checked(strdup(directory));
  size_t n = strlen(copy);
  for (size_t i = 1; i <= n; i++) {
    if (copy[i] != '/' && copy[i] != '\0') continue;
    char saved = copy[i];
    copy[i] = '\0';
    struct stat info;
    if (stat(copy, &info) != 0) {
      if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
        int error = errno;
        const char *code, *description;
        node_errno_text(error, &code, &description);
        free(copy);
        qa_raise("Error", 0, 0, "%s: %s, mkdir '%s'", code, description, directory);
      }
    } else if (!S_ISDIR(info.st_mode)) {
      free(copy);
      qa_raise("Error", 0, 0, "%s: %s, mkdir '%s'", saved == '\0' ? "EEXIST" : "ENOTDIR",
          saved == '\0' ? "file already exists" : "not a directory", directory);
    }
    copy[i] = saved;
  }
  free(copy);
}

typedef struct autosave_attempt { qa_application *app; const char *directory; } autosave_attempt;
static void native_layout_cleanup(void *layout) { qa_native_save_layout_destroy(layout); }
static void autosave_run(void *ctx) {
  autosave_attempt *attempt = ctx;
  qa_application *app = attempt->app;
  /* The original game module's autosave is the original's `save0` slot, written the way id's engine writes it on
   * entering a level. */
  qa_native_save_layout native;
  bool has_native = qa_application_native_saves(app, &native);
  bool original = has_native && qa_application_native_q2_original(app, &native);
  char *path;
  if (original) {
    qa_cleanup native_guard;
    qa_cleanup_push(&native_guard, native_layout_cleanup, &native);
    path = qa_native_save_layout_locate(&native, qa_app_sim_rerelease_autosave_name(false));
    qa_cleanup_pop(&native_guard);
    qa_native_save_layout_destroy(&native);
  } else {
    if (has_native) qa_native_save_layout_destroy(&native);
    mkdir_recursive(attempt->directory);
    size_t n = strlen(attempt->directory) + sizeof "/autosave.sav";
    char *joined = checked(malloc(n));
    (void)snprintf(joined, n, "%s/autosave.sav", attempt->directory);
    path = qa_application_path_normalize(joined);
    free(joined);
  }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  char *written = qa_application_save_game_at(app, path, "shared", true);
  qa_cleanup_pop(&cleanup);
  free(path);
  qa_cleanup written_cleanup;
  qa_cleanup_push(&written_cleanup, free, written);
  qa_application_printf(app, "Autosaved %s.\n", written);
  qa_cleanup_pop(&written_cleanup);
  free(written);
}
/* TS autosaveAvailable(): the engine's own sources autosave, and so does a stock Quake II game on its original
 * module, which writes the original's `save0`. */
static bool autosave_available(qa_application *app) {
  if (qa_application_level_recovery_available(app, app->world_simulation, &app->launch_options)) return true;
  qa_native_save_layout native;
  if (!qa_application_native_saves(app, &native)) return false;
  bool available = qa_application_native_q2_original(app, &native) && !app->launch_options.dedicated &&
      strcmp(app->launch_options.network_kind, "offline") == 0 &&
      strcmp(qa_application_simulation_mode(app->world_simulation), "singleplayer") == 0;
  qa_native_save_layout_destroy(&native);
  return available;
}
/* TS autosaveLevel(). */
void qa_application_autosave_level(qa_application *app) {
  if (!autosave_available(app) || qa_application_save_unavailable(app, QA_SAVE_PURPOSE_AUTOSAVE) != NULL) return;
  qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
  if (cvars != NULL && qa_cvar_registry_variable_value(cvars, "sv_autosave") == 0) return;
  const qa_catalog_discovered_product *product = qa_content_catalog_product(qa_app_loaded_content_catalog(app->loaded_content),
      qa_app_loaded_content_recipe(app->loaded_content)->map.entities.content);
  const char *root = qa_application_save_directory(app);
  size_t n = strlen(root) + 1 + strlen(product->expectation.id) + 1;
  char *joined = checked(malloc(n));
  (void)snprintf(joined, n, "%s/%s", root, product->expectation.id);
  char *directory = qa_application_path_normalize(joined);
  free(joined);
  autosave_attempt attempt = { app, directory };
  qa_caught_error caught;
  if (!qa_try(autosave_run, &attempt, &caught)) qa_application_printf(app, "Autosave failed: %s\n", qa_caught_message(&caught));
  free(directory);
}
