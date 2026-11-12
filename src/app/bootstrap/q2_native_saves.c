/* Port of src/app/bootstrap/q2-native-saves.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_native_saves.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "qa/app/bootstrap/simulation/native_q2_rerelease_pak.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/persistence/kex_q2_save.h"
#include "qa/persistence/q2_save_folder.h"
#include "qa/persistence/q2_save_store.h"
#include "qa/persistence/save_policy.h"
#include "qa/persistence/saved_game.h"
#include "qa/platform/files/contained.h"

/* The engine-format file that sits beside original files, and is never copied or wiped by the original's own
 * save logic. */
#define SIDECAR_NAME "qts" QA_SIDECAR_EXTENSION

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}

/* rm(path, { force: true }) */
static void remove_forced(const char *path) {
  if (unlink(path) == 0 || errno == ENOENT) return;
  qa_node_fs_error(errno, "rm", path);
}

static void write_contained(const char *directory, const char *path, const uint8_t *bytes, size_t length) {
  if (!qa_write_contained_native_file(directory, path, bytes, length))
    qa_raise("Error", errno, 0, "%s, open '%s'", errno != 0 ? strerror(errno) : "write failed", path);
}

static void settings_free(qa_app_sim_saved_settings *settings) {
  free(settings->client_slots);
  qa_save_value_free(settings->value);
}
static void settings_cleanup(void *settings) { settings_free(settings); }
static void classic_cleanup(void *save) { qa_q2_classic_original_save_free(save); }
static void files_cleanup(void *files) { qa_q2_save_files_free(files); }
static void store_cleanup(void *store) { qa_q2_save_store_destroy(store); }
static void rerelease_cleanup(void *save) { qa_app_sim_q2_rerelease_native_save_free(save); }
static void entries_cleanup(void *entries) { qa_app_sim_q2_rerelease_pak_entries_free(entries); }

/* The local clock fields of a TS Date (getHours, getMinutes, getMonth() + 1, getDate). */
static qa_q2_save_clock clock_of(double milliseconds) {
  time_t seconds = (time_t)floor(milliseconds / 1000);
  struct tm local;
  memset(&local, 0, sizeof local);
  (void)localtime_r(&seconds, &local);
  qa_q2_save_clock clock = {local.tm_hour, local.tm_min, local.tm_mon + 1, local.tm_mday};
  return clock;
}

static bool write_folder(const qa_app_q2_native_save_request *request) {
  const qa_native_save_layout *native = request->native;
  qa_q2_classic_original_save original;
  memset(&original, 0, sizeof original);
  if (!qa_app_sim_native_q2_original_save(request->image, NULL, &original)) return false;
  qa_cleanup original_guard;
  qa_cleanup_push(&original_guard, classic_cleanup, &original);
  qa_app_sim_saved_settings settings = qa_app_sim_saved_simulation_settings(request->image);
  qa_cleanup settings_guard;
  qa_cleanup_push(&settings_guard, settings_cleanup, &settings);
  char *map_command = checked(qa_q2_map_command(original.map, settings.initial_spawn_point));
  qa_cleanup command_guard;
  qa_cleanup_push(&command_guard, free, map_command);
  qa_q2_classic_folder_context context = {map_command, clock_of(request->now_milliseconds)};
  qa_q2_save_files files;
  qa_q2_classic_folder(&original, &context, &files);
  qa_cleanup files_guard;
  qa_cleanup_push(&files_guard, files_cleanup, &files);
  qa_q2_save_store store;
  qa_q2_save_store_init(&store, native->root);
  qa_cleanup store_guard;
  qa_cleanup_push(&store_guard, store_cleanup, &store);
  qa_q2_save_store_save(&store, request->slot, &files);
  char *folder = checked(qa_q2_save_store_folder(&store, request->slot));
  qa_cleanup folder_guard;
  qa_cleanup_push(&folder_guard, free, folder);
  char *engine = checked(qa_node_path_join2(folder, "qts.sav"));
  qa_cleanup engine_guard;
  qa_cleanup_push(&engine_guard, free, engine);
  remove_forced(engine);
  qa_cleanup_pop(&engine_guard);
  free(engine);
  uint8_t digest[QA_SAVE_DIGEST_BYTES];
  qa_q2_files_digest(&files, digest);
  size_t sidecar_length = 0;
  uint8_t *sidecar = checked(qa_encode_save_sidecar(digest, &request->image, &sidecar_length));
  qa_cleanup sidecar_guard;
  qa_cleanup_push(&sidecar_guard, free, sidecar);
  char *sidecar_path = checked(qa_node_path_join2(folder, SIDECAR_NAME));
  qa_cleanup path_guard;
  qa_cleanup_push(&path_guard, free, sidecar_path);
  write_contained(native->root, sidecar_path, sidecar, sidecar_length);
  qa_cleanup_pop(&path_guard);
  free(sidecar_path);
  qa_cleanup_pop(&sidecar_guard);
  free(sidecar);
  qa_cleanup_pop(&folder_guard);
  free(folder);
  qa_cleanup_pop(&store_guard);
  qa_q2_save_store_destroy(&store);
  qa_cleanup_pop(&files_guard);
  qa_q2_save_files_free(&files);
  qa_cleanup_pop(&command_guard);
  free(map_command);
  qa_cleanup_pop(&settings_guard);
  settings_free(&settings);
  qa_cleanup_pop(&original_guard);
  qa_q2_classic_original_save_free(&original);
  return true;
}

static char *slot_path(const char *root, const char *slot, const char *extension) {
  size_t length = strlen(slot) + strlen(extension) + 1;
  char *name = checked(malloc(length));
  (void)snprintf(name, length, "%s%s", slot, extension);
  char *path = checked(qa_node_path_join2(root, name));
  free(name);
  return path;
}

static bool write_pak(const qa_app_q2_native_save_request *request) {
  const qa_native_save_layout *native = request->native;
  qa_app_sim_q2_rerelease_native_save original;
  memset(&original, 0, sizeof original);
  if (!qa_app_sim_native_q2_rerelease_save_decoded(request->image, NULL, &original)) return false;
  qa_cleanup original_guard;
  qa_cleanup_push(&original_guard, rerelease_cleanup, &original);
  qa_app_sim_saved_settings settings = qa_app_sim_saved_simulation_settings(request->image);
  qa_cleanup settings_guard;
  qa_cleanup_push(&settings_guard, settings_cleanup, &settings);
  qa_app_sim_q2_rerelease_pak_context context = {settings.initial_spawn_point, request->now_milliseconds, request->portal_ctx,
                                                 request->portal_count};
  qa_app_sim_q2_rerelease_pak_entries entries;
  memset(&entries, 0, sizeof entries);
  qa_cleanup entries_guard;
  qa_cleanup_push(&entries_guard, entries_cleanup, &entries);
  qa_app_sim_q2_rerelease_pak_entries_of(&original, &context, &entries);
  /* packKexQ2SaveArchive(entries) */
  qa_kex_q2_pack_entry *pack = checked(calloc(entries.count + 1, sizeof *pack));
  for (size_t i = 0; i < entries.count; i++)
    pack[i] = (qa_kex_q2_pack_entry){entries.entries[i].name, entries.entries[i].bytes, entries.entries[i].length};
  size_t archive_length = 0;
  uint8_t *archive = qa_pack_kex_q2_save_archive(pack, entries.count, &archive_length);
  free(pack);
  checked(archive);
  qa_cleanup archive_guard;
  qa_cleanup_push(&archive_guard, free, archive);
  char *pak = slot_path(native->root, request->slot, ".pak.gz");
  qa_cleanup pak_guard;
  qa_cleanup_push(&pak_guard, free, pak);
  write_contained(native->root, pak, archive, archive_length);
  qa_cleanup_pop(&pak_guard);
  free(pak);
  qa_cleanup_pop(&archive_guard);
  free(archive);
  /* encodeSaveSidecar(q2FilesDigest(new Map(entries)), image): a repeated name keeps its last bytes. */
  qa_q2_save_files files;
  qa_q2_save_files_init(&files);
  qa_cleanup files_guard;
  qa_cleanup_push(&files_guard, files_cleanup, &files);
  for (size_t i = 0; i < entries.count; i++)
    qa_q2_save_files_set(&files, entries.entries[i].name, entries.entries[i].bytes, entries.entries[i].length);
  uint8_t digest[QA_SAVE_DIGEST_BYTES];
  qa_q2_files_digest(&files, digest);
  qa_cleanup_pop(&files_guard);
  qa_q2_save_files_free(&files);
  size_t sidecar_length = 0;
  uint8_t *sidecar = checked(qa_encode_save_sidecar(digest, &request->image, &sidecar_length));
  qa_cleanup sidecar_guard;
  qa_cleanup_push(&sidecar_guard, free, sidecar);
  char *sidecar_path = slot_path(native->root, request->slot, QA_SIDECAR_EXTENSION);
  qa_cleanup path_guard;
  qa_cleanup_push(&path_guard, free, sidecar_path);
  write_contained(native->root, sidecar_path, sidecar, sidecar_length);
  qa_cleanup_pop(&path_guard);
  free(sidecar_path);
  qa_cleanup_pop(&sidecar_guard);
  free(sidecar);
  char *engine = slot_path(native->root, request->slot, ".sav");
  qa_cleanup engine_guard;
  qa_cleanup_push(&engine_guard, free, engine);
  remove_forced(engine);
  qa_cleanup_pop(&engine_guard);
  free(engine);
  qa_cleanup_pop(&entries_guard);
  qa_app_sim_q2_rerelease_pak_entries_free(&entries);
  qa_cleanup_pop(&settings_guard);
  settings_free(&settings);
  qa_cleanup_pop(&original_guard);
  qa_app_sim_q2_rerelease_native_save_free(&original);
  return true;
}

bool qa_app_write_q2_native_save(const qa_app_q2_native_save_request *request) {
  if (request->native->original == QA_NATIVE_SAVE_Q2_FOLDER) return write_folder(request);
  if (request->native->original == QA_NATIVE_SAVE_Q2_PAK) return write_pak(request);
  return false;
}

void qa_app_clear_q2_original_files(const qa_native_save_layout *native, const char *slot) {
  if (native->original == QA_NATIVE_SAVE_Q2_FOLDER) {
    qa_q2_save_store store;
    qa_q2_save_store_init(&store, native->root);
    qa_cleanup store_guard;
    qa_cleanup_push(&store_guard, store_cleanup, &store);
    qa_q2_save_files files;
    qa_q2_save_store_read(&store, slot, &files);
    qa_cleanup files_guard;
    qa_cleanup_push(&files_guard, files_cleanup, &files);
    char *folder = checked(qa_q2_save_store_folder(&store, slot));
    qa_cleanup folder_guard;
    qa_cleanup_push(&folder_guard, free, folder);
    for (size_t i = 0; i < files.count; i++) {
      const char *file = files.files[i].name;
      /* /^(?:server\.ssv|game\.ssv|.+\.sv2)$/i, or /\.sav$/i other than qts.sav */
      bool original = qa_js_equals_ignore_case(file, "server.ssv") || qa_js_equals_ignore_case(file, "game.ssv") ||
                      (strlen(file) > 4 && qa_js_ends_with_ignore_case(file, ".sv2")) ||
                      (qa_js_ends_with_ignore_case(file, ".sav") && !qa_js_equals_ignore_case(file, "qts.sav"));
      if (!original) continue;
      char *path = checked(qa_node_path_join2(folder, file));
      qa_cleanup path_guard;
      qa_cleanup_push(&path_guard, free, path);
      remove_forced(path);
      qa_cleanup_pop(&path_guard);
      free(path);
    }
    char *sidecar = checked(qa_node_path_join2(folder, SIDECAR_NAME));
    qa_cleanup sidecar_guard;
    qa_cleanup_push(&sidecar_guard, free, sidecar);
    remove_forced(sidecar);
    qa_cleanup_pop(&sidecar_guard);
    free(sidecar);
    qa_cleanup_pop(&folder_guard);
    free(folder);
    qa_cleanup_pop(&files_guard);
    qa_q2_save_files_free(&files);
    qa_cleanup_pop(&store_guard);
    qa_q2_save_store_destroy(&store);
  } else if (native->original == QA_NATIVE_SAVE_Q2_PAK) {
    char *pak = slot_path(native->root, slot, ".pak.gz");
    qa_cleanup pak_guard;
    qa_cleanup_push(&pak_guard, free, pak);
    remove_forced(pak);
    qa_cleanup_pop(&pak_guard);
    free(pak);
    char *sidecar = slot_path(native->root, slot, QA_SIDECAR_EXTENSION);
    qa_cleanup sidecar_guard;
    qa_cleanup_push(&sidecar_guard, free, sidecar);
    remove_forced(sidecar);
    qa_cleanup_pop(&sidecar_guard);
    free(sidecar);
  }
}
