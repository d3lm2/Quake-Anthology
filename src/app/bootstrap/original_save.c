/* Port of src/app/bootstrap/original-save.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/original_save.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "application_internal.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/q2_localization.h"
#include "qa/app/bootstrap/simulation/native_q2_rerelease_pak.h"
#include "qa/app/bootstrap/simulation/navigation.h"
#include "qa/foundation/node_path.h"
#include "qa/guest/runtime/windows/contracts.h"
#include "qa/persistence/q2_save_folder.h"
#include "qa/persistence/q2_save_store.h"
#include "qa/persistence/q2_selection.h"
#include "qa/text/localization_resources.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/persistence/kex_q2_save.h"
#include "qa/compat/qc/q1_save.h"
#include "qa/persistence/q1_selection.h"
#include "qa/persistence/saved_game.h"
#include "simulation/shared_simulation_internal.h"

/* What keeps the prepared image alive (TS GC): the decoded shared save, or the closed content and simulation whose
 * recipe the checkpoint borrows. */
typedef struct save_backing {
  bool has_saved; qa_saved_game saved;
  /* A Quake II original save: the sidecar's image, or the import the throwaway world starts from (borrowed by the
   * simulation, so it outlives it) with the headless guest's services. */
  bool has_sidecar; qa_save_image_result sidecar;
  bool has_archive; qa_kex_q2_save_archive archive;
  bool has_classic_import; qa_q2_classic_original_import classic_import;
  bool has_rerelease_import; qa_app_sim_q2_rerelease_native_import rerelease_import;
  qa_app_sim_native_q2_import native_import;
  qa_windows_capabilities capabilities;
  bool has_localization; qa_localization_table localization;
  qa_app_sim_rerelease_localize localize;
  qa_app_sim_rerelease_clipboard clipboard;
  qa_app_shared_sim_next_frame_fn next_frame; void *next_frame_ctx;
  qa_app_loaded_content *content;
  bool has_identity; qa_identity_owner identity;
  qa_app_shared_simulation *simulation;
  bool has_checkpoint; qa_app_shared_sim_save checkpoint;
} save_backing;

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = checked(malloc(len + 1));
  memcpy(out, text, len + 1);
  return out;
}
static void set_owned(char **slot, const char *text) { free(*slot); *slot = dup_text(text); }

/* Deep copy of the owned fields of qa_app_options (TS object spread). */
static qa_app_options clone_options(const qa_app_options *source) {
  qa_app_options o = *source;
  o.network_transport_relay = dup_text(source->network_transport_relay);
  o.startup_commands = NULL; o.startup_command_count = 0;
  if (source->startup_command_count > 0) {
    o.startup_commands = checked(calloc(source->startup_command_count, sizeof *o.startup_commands));
    for (size_t i = 0; i < source->startup_command_count; i++) o.startup_commands[i] = dup_text(source->startup_commands[i]);
    o.startup_command_count = source->startup_command_count;
  }
  o.remote_content.directory = dup_text(source->remote_content.directory);
  o.server_profile_path = dup_text(source->server_profile_path);
  o.corpus_root = dup_text(source->corpus_root);
  o.user_content_root = dup_text(source->user_content_root);
  o.native_config_root = dup_text(source->native_config_root);
  o.addon_content_roots = NULL; o.addon_content_root_count = 0;
  if (source->addon_content_root_count > 0) {
    o.addon_content_roots = checked(calloc(source->addon_content_root_count, sizeof *o.addon_content_roots));
    for (size_t i = 0; i < source->addon_content_root_count; i++) {
      o.addon_content_roots[i] = source->addon_content_roots[i];
      o.addon_content_roots[i].root = dup_text(source->addon_content_roots[i].root);
    }
    o.addon_content_root_count = source->addon_content_root_count;
  }
  o.product = dup_text(source->product);
  o.map_product = dup_text(source->map_product);
  o.map = dup_text(source->map);
  o.quakec_program = dup_text(source->quakec_program);
  o.q2_game_library = dup_text(source->q2_game_library);
  o.weapon_behavior_product = dup_text(source->weapon_behavior_product);
  o.weapon_behavior_id = dup_text(source->weapon_behavior_id);
  o.mods = NULL; o.mod_count = 0;
  if (source->mod_count > 0) {
    o.mods = checked(calloc(source->mod_count, sizeof *o.mods));
    for (size_t i = 0; i < source->mod_count; i++) {
      o.mods[i].product = dup_text(source->mods[i].product);
      o.mods[i].id = dup_text(source->mods[i].id);
    }
    o.mod_count = source->mod_count;
  }
  o.movement_product = dup_text(source->movement_product);
  o.character_model = dup_text(source->character_model);
  o.character_skin = dup_text(source->character_skin);
  o.network_host = dup_text(source->network_host);
  o.network_remote = dup_text(source->network_remote);
  return o;
}
/* TS rest destructuring: drop movementProduct, mapProduct, q2GameLibrary, weaponBehavior, teamArenaSkirmish,
 * q3MapLaunch, q3Product, botSkill, serverProfile, serverProfilePath, startupCommands, explicitRules. (The C
 * explicitRules is the three explicit_* flags the parser records.) */
static void strip_source_options(qa_app_options *o) {
  free(o->movement_product); o->movement_product = NULL;
  free(o->map_product); o->map_product = NULL;
  free(o->q2_game_library); o->q2_game_library = NULL;
  o->has_weapon_behavior = false;
  free(o->weapon_behavior_product); o->weapon_behavior_product = NULL;
  free(o->weapon_behavior_id); o->weapon_behavior_id = NULL;
  o->team_arena_skirmish = NULL;
  o->q3_map_launch = NULL;
  o->has_q3_product = false;
  o->has_bot_skill = false;
  o->server_profile = NULL;
  free(o->server_profile_path); o->server_profile_path = NULL;
  for (size_t i = 0; i < o->startup_command_count; i++) free(o->startup_commands[i]);
  free(o->startup_commands); o->startup_commands = NULL; o->startup_command_count = 0;
  o->explicit_skill = false; o->explicit_mode = false; o->explicit_capacity = false;
}
/* TS network: { kind: "offline" }: the network fields of the other kinds go with it. */
static void set_offline(qa_app_options *o) {
  o->network_kind = "offline";
  free(o->network_host); o->network_host = NULL;
  free(o->network_remote); o->network_remote = NULL;
  o->network_port = 0;
}

static void backing_free(save_backing *backing) {
  if (backing == NULL) return;
  if (backing->has_checkpoint) qa_app_shared_sim_save_free(&backing->checkpoint);
  if (backing->simulation != NULL) qa_app_shared_simulation_destroy(backing->simulation);
  if (backing->has_identity) qa_identity_owner_destroy(&backing->identity);
  if (backing->content != NULL) qa_app_loaded_content_destroy(backing->content);
  if (backing->has_saved) qa_saved_game_free(&backing->saved);
  if (backing->has_sidecar) qa_save_image_result_free(&backing->sidecar);
  if (backing->has_localization) qa_localization_table_destroy(&backing->localization);
  if (backing->has_classic_import) qa_q2_classic_original_import_free(&backing->classic_import);
  if (backing->has_rerelease_import) qa_app_sim_q2_rerelease_native_import_free(&backing->rerelease_import);
  if (backing->has_archive) qa_kex_q2_save_archive_free(&backing->archive);
  free(backing);
}
static void backing_cleanup(void *backing) { backing_free(backing); }
static void options_cleanup(void *options) { qa_app_options_destroy(options); }
/* A raise inside the try closes the simulation, then the content (TS finally blocks, innermost first). */
static void simulation_close_cleanup(void *backing) {
  save_backing *b = backing;
  if (b->simulation != NULL) qa_app_shared_sim_close(b->simulation);
}
static void content_close_cleanup(void *backing) {
  save_backing *b = backing;
  if (b->content != NULL) qa_app_loaded_content_close(b->content);
}

/* Bun.file(path).bytes() rejection: the Node errno message. */
static void raise_read_error(int error, const char *path) {
  if (error == ENOENT) qa_raise("Error", 0, 0, "ENOENT: no such file or directory, open '%s'", path);
  if (error == EACCES) qa_raise("Error", 0, 0, "EACCES: permission denied, open '%s'", path);
  if (error == EISDIR) qa_raise("Error", 0, 0, "EISDIR: illegal operation on a directory, read");
  qa_raise("Error", 0, 0, "%s, open '%s'", error != 0 ? strerror(error) : "read failed", path);
}

/* The simulation of both import paths: createSimulation, admitPlayer(identity.client(0, 0)), the original restore
 * (save NULL for KEX), checkpoint. TS closes the simulation and then the content in finally blocks. */
static bool kex_player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  (void)ctx; (void)client; /* TS playerIdentity: () => ({ seat: 0, socialId: "" }) */
  *out_seat = 0;
  *out_social_id = "";
  return true;
}
static void import_world(save_backing *backing, qa_app_options *restored, const qa_content_catalog *catalog,
    const char *identity_name, const qa_q1_save_data *original, qa_app_prepared_application_save *out) {
  /* loadApplicationContent(restored, undefined, undefined, catalog): the loader takes over its options. */
  backing->content = qa_app_load_application_content_native(clone_options(restored), NULL, false,
      NULL, (qa_content_catalog *)catalog, NULL);
  if (backing->content == NULL) qa_range_error("Out of memory");
  qa_cleanup close_content;
  qa_cleanup_push(&close_content, content_close_cleanup, backing);
  const struct qa_app_sim_prepared_quakec_source *prepared = qa_app_loaded_content_prepared_quakec(backing->content);
  if (original != NULL && prepared == NULL) qa_raise("Error", 0, 0, "Original save requires its native QuakeC program");
  if (!qa_identity_owner_create(&backing->identity, identity_name)) qa_range_error("Out of memory");
  backing->has_identity = true;
  qa_app_application_world world = qa_app_loaded_content_world(backing->content);
  qa_app_simulation_options options;
  memset(&options, 0, sizeof options);
  options.identity = backing->identity;
  options.recipe = *qa_app_loaded_content_recipe(backing->content);
  options.world = &world;
  options.mounts = qa_app_loaded_content_mounts(backing->content);
  if (original != NULL) {
    options.prepared_quakec = prepared;
    options.original_save_candidate = true;
    options.has_initial_source_milliseconds = true;
    options.initial_source_milliseconds = original->time * 1000;
  } else {
    options.identity_ctx = backing;
    options.player_identity = kex_player_identity;
  }
  options.dedicated = true;
  options.skill = restored->skill;
  options.mode = restored->mode;
  options.seed = restored->seed;
  options.max_clients = 1;
  backing->simulation = qa_app_shared_simulation_create(options);
  qa_cleanup close_simulation;
  qa_cleanup_push(&close_simulation, simulation_close_cleanup, backing);
  (void)qa_app_shared_sim_admit_player(backing->simulation, qa_identity_owner_client(&backing->identity, 0, 0));
  if (original != NULL) {
    qa_q1_unknown_save_fields unknown;
    memset(&unknown, 0, sizeof unknown);
    qa_app_shared_sim_restore_original_save(backing->simulation, original, &unknown);
    qa_q1_unknown_save_fields_free(&unknown); /* TS discards restoreQ1SourceSave's result */
  }
  qa_app_shared_sim_checkpoint(backing->simulation, &backing->checkpoint);
  backing->has_checkpoint = true;
  qa_cleanup_pop(&close_simulation);
  qa_app_shared_sim_close(backing->simulation);
  qa_cleanup_pop(&close_content);
  qa_app_loaded_content_close(backing->content);
  out->image = backing->checkpoint.image;
}

/* ---- Quake II original saves ---- */
/* What the engine writes beside the original files; the original's own wipe and copy never touch these:
 * /^qts\.(?:sav|qtsave)$/i. */
static bool engine_file(const char *name) {
  return qa_js_equals_ignore_case(name, "qts.sav") || qa_js_equals_ignore_case(name, "qts" QA_SIDECAR_EXTENSION);
}
#define SIDECAR_NAME "qts" QA_SIDECAR_EXTENSION

/* TS importGuest: the headless world has no console, files or clipboard of its own; the module's output is discarded. */
static bool import_now(void *ctx, double *out) { (void)ctx; *out = trunc(qa_application_performance_now()); return true; }
static bool import_counter(void *ctx, int64_t *out) {
  (void)ctx;
  *out = (int64_t)trunc(qa_application_performance_now() * 1000);
  return true;
}
static qa_windows_file *import_open_file(void *ctx, const char *path, bool read, bool write, int creation) {
  (void)ctx; (void)path; (void)read; (void)write; (void)creation;
  return NULL;
}
static void import_standard_output(void *ctx, qa_windows_stream stream, const uint8_t *bytes, size_t length) {
  (void)ctx; (void)stream; (void)bytes; (void)length;
}
static void import_discard_text(void *ctx, const char *text) { (void)ctx; (void)text; }
static void import_discard_graph(void *ctx, double value, double color) { (void)ctx; (void)value; (void)color; }
static uint8_t *import_localization_read(void *ctx, const char *path, size_t *out_length) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  return opened.bytes;
}
static char *import_localize(void *ctx, const char *key, const char *const *arguments, size_t count) {
  save_backing *backing = ctx;
  return checked(qa_app_q2_localized_text(&backing->localization, key, arguments, count));
}
static void import_guest(save_backing *backing, qa_app_simulation_options *options) {
  const char *edition = NULL;
  const void *prepared = qa_app_loaded_content_prepared_q2_game(backing->content, &edition);
  if (prepared == NULL) qa_raise("Error", 0, 0, "This save needs the original game module, which the game does not provide.");
  backing->capabilities.ctx = backing;
  backing->capabilities.now_milliseconds = import_now;
  backing->capabilities.performance_counter = import_counter;
  backing->capabilities.has_performance_frequency = true;
  backing->capabilities.performance_frequency = 1000000;
  backing->capabilities.open_file = import_open_file;
  backing->capabilities.standard_output = import_standard_output;
  options->has_q2_guest = true;
  memset(&options->q2_guest, 0, sizeof options->q2_guest);
  options->q2_guest.capabilities = &backing->capabilities;
  options->q2_guest.print_ctx = backing; options->q2_guest.print = import_discard_text;
  options->q2_guest.cmd_ctx = backing; options->q2_guest.add_command = import_discard_text;
  options->q2_guest.graph_ctx = backing; options->q2_guest.debug_graph = import_discard_graph;
  options->q2_guest.edition = edition; options->q2_guest.prepared = prepared;
  if (strcmp(edition, "classic") == 0) return;
  const qa_app_sim_prepared_rerelease_guest *rerelease = prepared;
  qa_content_mounted *mounts = (qa_content_mounted *)qa_app_loaded_content_for_content(backing->content, rerelease->execution.owner.content);
  qa_load_server_localization_resources("english", import_localization_read, mounts, QA_LOC_Q2_RERELEASE, &backing->localization);
  backing->has_localization = true;
  backing->localize.ctx = backing; backing->localize.localize = import_localize;
  backing->clipboard.client = false; /* clipboard: { kind: "dedicated" } */
  options->q2_guest.localize = &backing->localize;
  options->q2_guest.clipboard = &backing->clipboard;
}
/* prepareRereleaseNavigation: simulation => createApplicationBotNavigation({ content, simulation }). */
static const void *import_rerelease_navigation(void *ctx, qa_app_shared_simulation *simulation) {
  save_backing *backing = ctx;
  return checked(qa_app_sim_create_application_bot_navigation(qa_application_navigation_content(backing->content),
      qa_app_shared_sim_navigation_simulation(simulation, qa_app_loaded_content_recipe(backing->content))));
}
static void import_no_frame(void *ctx) { (void)ctx; }

/* TS importNativeQ2: starts the product's original game module on the recorded files in a throwaway world and
 * takes its checkpoint. The application then restores that checkpoint like any other save, so a load from original
 * files and a load from the engine's own image run the same restore. backing->native_import is set by the caller. */
static void import_native_q2(const qa_content_catalog *catalog, const qa_app_options *load_options, save_backing *backing,
    const char *product, const char *library, const char *imported_map, const char *spawn_point,
    qa_q2_saved_settings settings, qa_app_prepared_application_save *out) {
  qa_app_options restored = clone_options(load_options);
  qa_cleanup restored_cleanup;
  qa_cleanup_push(&restored_cleanup, options_cleanup, &restored);
  strip_source_options(&restored);
  set_owned(&restored.product, product);
  set_owned(&restored.q2_game_library, library);
  /* map: imported.map.endsWith(".bsp") ? imported.map : `maps/${imported.map}.bsp` */
  size_t map_length = strlen(imported_map);
  if (map_length >= 4 && strcmp(imported_map + map_length - 4, ".bsp") == 0) set_owned(&restored.map, imported_map);
  else {
    char *map_path = checked(malloc(map_length + 10));
    snprintf(map_path, map_length + 10, "maps/%s.bsp", imported_map);
    free(restored.map);
    restored.map = map_path;
  }
  restored.skill = settings.skill;
  restored.mode = settings.mode == QA_Q2_SAVED_COOP ? "coop" : "singleplayer";
  restored.movement = QA_GAME_FAMILY_Q2;
  restored.character = QA_GAME_FAMILY_Q2;
  set_owned(&restored.character_model, "male");
  restored.seats = 1;
  restored.rules = "standard";
  set_offline(&restored);
  backing->content = qa_app_load_application_content_native(clone_options(&restored), NULL, false,
      NULL, (qa_content_catalog *)catalog, NULL);
  if (backing->content == NULL) qa_range_error("Out of memory");
  qa_cleanup close_content;
  qa_cleanup_push(&close_content, content_close_cleanup, backing);
  qa_app_simulation_options options;
  memset(&options, 0, sizeof options);
  import_guest(backing, &options);
  if (!qa_identity_owner_create(&backing->identity, "q2-original-save-import")) qa_range_error("Out of memory");
  backing->has_identity = true;
  qa_app_application_world world = qa_app_loaded_content_world(backing->content);
  options.identity = backing->identity;
  options.recipe = *qa_app_loaded_content_recipe(backing->content);
  options.world = &world;
  options.mounts = qa_app_loaded_content_mounts(backing->content);
  options.dedicated = true;
  options.nav_ctx = backing; options.prepare_rerelease_navigation = import_rerelease_navigation;
  options.native_import = &backing->native_import;
  options.initial_spawn_point = spawn_point;
  options.skill = restored.skill;
  options.mode = restored.mode;
  options.seed = restored.seed;
  options.max_clients = settings.max_clients;
  options.identity_ctx = backing;
  options.player_identity = kex_player_identity;
  qa_app_shared_sim_next_frame_fn next_frame = backing->next_frame != NULL ? backing->next_frame : import_no_frame;
  backing->simulation = qa_app_shared_simulation_load(options, next_frame, backing->next_frame_ctx);
  qa_cleanup close_simulation;
  qa_cleanup_push(&close_simulation, simulation_close_cleanup, backing);
  (void)qa_app_shared_sim_admit_player(backing->simulation, qa_identity_owner_client(&backing->identity, 0, 0));
  qa_app_shared_sim_checkpoint_loading(backing->simulation, next_frame, backing->next_frame_ctx, &backing->checkpoint);
  backing->has_checkpoint = true;
  qa_cleanup_pop(&close_simulation);
  qa_app_shared_sim_close(backing->simulation);
  qa_cleanup_pop(&close_content);
  qa_app_loaded_content_close(backing->content);
  out->image = backing->checkpoint.image;
  qa_cleanup_pop(&restored_cleanup);
  out->options = restored;
}

/* Bun.file(path).bytes(): malloc'd, raises the Node error. */
static uint8_t *read_file_bytes(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) raise_read_error(errno, path);
  uint8_t *bytes = NULL;
  size_t length = 0, capacity = 0;
  for (;;) {
    if (length == capacity) {
      capacity = capacity == 0 ? 65536 : capacity * 2;
      bytes = checked(realloc(bytes, capacity));
    }
    size_t got = fread(bytes + length, 1, capacity - length, file);
    if (got == 0) break;
    length += got;
  }
  if (ferror(file)) { int error = errno; fclose(file); free(bytes); raise_read_error(error, path); }
  fclose(file);
  *out_length = length;
  return bytes;
}
static void files_cleanup(void *files) { qa_q2_save_files_free(files); }
static void store_cleanup(void *store) { qa_q2_save_store_destroy(store); }

/* TS prepareClassicFolder: a Quake II save folder written by the original game or by this engine. The engine's
 * sidecar carries the state the original files cannot (every seat, engine-only state) and is used while it still
 * agrees with the files. Otherwise the original files alone start the original game module on the recorded level,
 * exactly as `load` does in id's engine. */
static void prepare_classic_folder(const qa_content_catalog *catalog, const char *folder, const qa_app_options *load_options,
    const char *source_product, save_backing *backing, qa_app_prepared_application_save *out) {
  char *root = checked(qa_node_path_dirname(folder));
  char *slot = checked(qa_node_path_basename(folder));
  qa_q2_save_store store;
  qa_q2_save_store_init(&store, root);
  free(root);
  qa_cleanup store_guard;
  qa_cleanup_push(&store_guard, store_cleanup, &store);
  qa_cleanup slot_guard;
  qa_cleanup_push(&slot_guard, free, slot);
  qa_q2_save_files all;
  qa_q2_save_store_read(&store, slot, &all);
  qa_cleanup all_guard;
  qa_cleanup_push(&all_guard, files_cleanup, &all);
  /* files = new Map([...all].filter(([name]) => !ENGINE_FILES.test(name))) */
  qa_q2_save_files files;
  qa_q2_save_files_init(&files);
  qa_cleanup files_guard;
  qa_cleanup_push(&files_guard, files_cleanup, &files);
  const qa_q2_save_file *sidecar = NULL;
  for (size_t i = 0; i < all.count; i++) {
    if (!engine_file(all.files[i].name)) qa_q2_save_files_set(&files, all.files[i].name, all.files[i].bytes, all.files[i].length);
    if (sidecar == NULL && qa_js_equals_ignore_case(all.files[i].name, SIDECAR_NAME)) sidecar = &all.files[i];
  }
  bool agreed = false;
  if (sidecar != NULL) {
    uint8_t digest[QA_SAVE_DIGEST_BYTES];
    qa_q2_files_digest(&files, digest);
    agreed = qa_decode_save_sidecar(sidecar->bytes, sidecar->length, digest, &backing->sidecar);
    backing->has_sidecar = agreed;
  }
  if (agreed) {
    out->image = backing->sidecar.image;
    out->options = clone_options(load_options);
  } else {
    qa_read_q2_classic_folder(&files, &backing->classic_import);
    backing->has_classic_import = true;
    const qa_q2_classic_original_import *imported = &backing->classic_import;
    /* basename(dirname(dirname(folder))) */
    char *save_folder = checked(qa_node_path_dirname(folder));
    char *game_folder = checked(qa_node_path_dirname(save_folder));
    char *game_directory = checked(qa_node_path_basename(game_folder));
    free(save_folder); free(game_folder);
    qa_cleanup directory_guard;
    qa_cleanup_push(&directory_guard, free, game_directory);
    const qa_catalog_discovered_product *product =
        qa_select_q2_save_product(catalog, "classic", game_directory, source_product, load_options->product);
    qa_cleanup_pop(&directory_guard);
    free(game_directory);
    const char **names = checked(calloc(imported->latched_count + 1, sizeof *names));
    const char **values = checked(calloc(imported->latched_count + 1, sizeof *values));
    for (size_t i = 0; i < imported->latched_count; i++) { names[i] = imported->latched[i].name; values[i] = imported->latched[i].value; }
    qa_cleanup names_guard, values_guard;
    qa_cleanup_push(&names_guard, free, (void *)names);
    qa_cleanup_push(&values_guard, free, (void *)values);
    qa_q2_saved_settings settings = qa_q2_saved_settings_from(names, values, imported->latched_count);
    qa_cleanup_pop(&values_guard);
    qa_cleanup_pop(&names_guard);
    free((void *)names); free((void *)values);
    backing->native_import = (qa_app_sim_native_q2_import){false, imported, NULL};
    import_native_q2(catalog, load_options, backing, product->expectation.id, "gamex86.dll", imported->map,
        imported->spawn_point, settings, out);
  }
  /* folder: files */
  qa_cleanup_pop(&files_guard);
  out->folder = files;
  out->has_folder = true;
  qa_cleanup_pop(&all_guard);
  qa_q2_save_files_free(&all);
  qa_cleanup_pop(&slot_guard);
  free(slot);
  qa_cleanup_pop(&store_guard);
  qa_q2_save_store_destroy(&store);
}

/* TS prepareRereleasePak: a rerelease `saveN.pak.gz`. A pak records `gamename`; its sidecar `saveN.qtsave` holds
 * engine-only state. */
static void prepare_rerelease_pak(const qa_content_catalog *catalog, const char *path, const qa_app_options *load_options,
    const char *source_product, save_backing *backing, qa_app_prepared_application_save *out) {
  size_t compressed_length = 0;
  uint8_t *compressed = read_file_bytes(path, &compressed_length);
  qa_cleanup compressed_guard;
  qa_cleanup_push(&compressed_guard, free, compressed);
  if (!qa_parse_kex_q2_save_archive(compressed, compressed_length, &backing->archive)) qa_range_error("Out of memory");
  backing->has_archive = true;
  qa_cleanup_pop(&compressed_guard);
  free(compressed);
  const qa_kex_q2_save_archive *archive = &backing->archive;
  /* path.replace(/\.pak\.gz$/i, SIDECAR_EXTENSION) */
  size_t stem = strlen(path) - strlen(".pak.gz");
  char *sidecar_path = checked(malloc(stem + strlen(QA_SIDECAR_EXTENSION) + 1));
  memcpy(sidecar_path, path, stem);
  strcpy(sidecar_path + stem, QA_SIDECAR_EXTENSION);
  struct stat status;
  bool sidecar_exists = stat(sidecar_path, &status) == 0 && !S_ISDIR(status.st_mode); /* Bun.file().exists() */
  if (sidecar_exists) {
    qa_cleanup path_guard;
    qa_cleanup_push(&path_guard, free, sidecar_path);
    size_t sidecar_length = 0;
    uint8_t *sidecar = read_file_bytes(sidecar_path, &sidecar_length);
    qa_cleanup sidecar_guard;
    qa_cleanup_push(&sidecar_guard, free, sidecar);
    qa_q2_save_files entries;
    qa_q2_save_files_init(&entries);
    qa_cleanup entries_guard;
    qa_cleanup_push(&entries_guard, files_cleanup, &entries);
    for (size_t i = 0; i < archive->entry_count; i++)
      qa_q2_save_files_set(&entries, archive->entries[i].name, archive->entries[i].bytes, archive->entries[i].length);
    uint8_t digest[QA_SAVE_DIGEST_BYTES];
    qa_q2_files_digest(&entries, digest);
    qa_cleanup_pop(&entries_guard);
    qa_q2_save_files_free(&entries);
    bool agreed = qa_decode_save_sidecar(sidecar, sidecar_length, digest, &backing->sidecar);
    qa_cleanup_pop(&sidecar_guard);
    free(sidecar);
    qa_cleanup_pop(&path_guard);
    if (agreed) {
      free(sidecar_path);
      backing->has_sidecar = true;
      out->image = backing->sidecar.image;
      out->options = clone_options(load_options);
      return;
    }
  }
  free(sidecar_path);
  qa_app_sim_read_q2_rerelease_pak(archive, &backing->rerelease_import);
  backing->has_rerelease_import = true;
  const qa_app_sim_q2_rerelease_native_import *imported = &backing->rerelease_import;
  /* archive.info.gameDirectory ?? basename(dirname(dirname(path))) */
  char *fallback_directory = NULL;
  if (archive->info.game_directory == NULL) {
    char *save_folder = checked(qa_node_path_dirname(path));
    char *game_folder = checked(qa_node_path_dirname(save_folder));
    fallback_directory = checked(qa_node_path_basename(game_folder));
    free(save_folder); free(game_folder);
  }
  qa_cleanup directory_guard;
  qa_cleanup_push(&directory_guard, free, fallback_directory);
  const qa_catalog_discovered_product *product = qa_select_q2_save_product(catalog, "rerelease",
      archive->info.game_directory != NULL ? archive->info.game_directory : fallback_directory, source_product, load_options->product);
  qa_cleanup_pop(&directory_guard);
  free(fallback_directory);
  const char **names = checked(calloc(imported->latched_count + 1, sizeof *names));
  const char **values = checked(calloc(imported->latched_count + 1, sizeof *values));
  for (size_t i = 0; i < imported->latched_count; i++) { names[i] = imported->latched[i].name; values[i] = imported->latched[i].value; }
  qa_cleanup names_guard, values_guard;
  qa_cleanup_push(&names_guard, free, (void *)names);
  qa_cleanup_push(&values_guard, free, (void *)values);
  qa_q2_saved_settings settings = qa_q2_saved_settings_from(names, values, imported->latched_count);
  qa_cleanup_pop(&values_guard);
  qa_cleanup_pop(&names_guard);
  free((void *)names); free((void *)values);
  backing->native_import = (qa_app_sim_native_q2_import){true, NULL, imported};
  import_native_q2(catalog, load_options, backing, product->expectation.id, "game_x64.dll", imported->map,
      imported->spawn_point, settings, out);
}

void qa_app_prepare_application_save_native(const qa_app_options *options, const qa_content_catalog *catalog, const char *path,
    const char *source_product, qa_app_prepared_application_save *out, void **out_backing) {
  qa_app_prepare_application_save_loading(options, catalog, path, source_product, NULL, NULL, out, out_backing);
}
static void options_destroy_cleanup(void *options) { qa_app_options_destroy(options); }
static void out_folder_cleanup(void *raw) {
  qa_app_prepared_application_save *out = raw;
  if (out->has_folder) qa_q2_save_files_free(&out->folder);
  out->has_folder = false;
}
void qa_app_prepare_application_save_loading(const qa_app_options *options, const qa_content_catalog *catalog, const char *path,
    const char *source_product, void (*next_frame)(void *ctx), void *next_frame_ctx,
    qa_app_prepared_application_save *out, void **out_backing) {
  memset(out, 0, sizeof *out);
  *out_backing = NULL;
  save_backing *backing = checked(calloc(1, sizeof *backing));
  backing->next_frame = next_frame; backing->next_frame_ctx = next_frame_ctx;
  qa_cleanup backing_node;
  qa_cleanup_push(&backing_node, backing_cleanup, backing);
  /* const { authoredCampaignStart, ...loadOptions } = options */
  qa_app_options load_options = clone_options(options);
  load_options.authored_campaign_start = false;
  qa_cleanup load_cleanup;
  qa_cleanup_push(&load_cleanup, options_cleanup, &load_options);
  /* basename(path).toLowerCase() === "server.ssv" || (=== "qts.sav" && the folder holds a server.ssv) */
  char *base = checked(qa_node_path_basename(path));
  char *folder = checked(qa_node_path_dirname(path));
  bool classic_folder = qa_js_equals_ignore_case(base, "server.ssv");
  if (!classic_folder && qa_js_equals_ignore_case(base, "qts.sav")) {
    char *server = checked(qa_node_path_join2(folder, "server.ssv"));
    struct stat status;
    classic_folder = stat(server, &status) == 0 && !S_ISDIR(status.st_mode); /* Bun.file().exists() */
    free(server);
  }
  free(base);
  qa_cleanup folder_guard;
  qa_cleanup_push(&folder_guard, free, folder);
  if (qa_js_ends_with_ignore_case(path, ".pak.gz")) { /* path.toLowerCase().endsWith(".pak.gz") */
    qa_cleanup out_options;
    qa_cleanup_push(&out_options, options_destroy_cleanup, &out->options);
    prepare_rerelease_pak(catalog, path, &load_options, source_product, backing, out);
    qa_cleanup_pop(&out_options);
    qa_cleanup_pop(&folder_guard);
    free(folder);
    qa_cleanup_pop(&load_cleanup);
    qa_app_options_destroy(&load_options);
  } else if (classic_folder) {
    qa_cleanup out_options, out_folder;
    qa_cleanup_push(&out_options, options_destroy_cleanup, &out->options);
    qa_cleanup_push(&out_folder, out_folder_cleanup, out);
    prepare_classic_folder(catalog, folder, &load_options, source_product, backing, out);
    qa_cleanup_pop(&out_folder);
    qa_cleanup_pop(&out_options);
    qa_cleanup_pop(&folder_guard);
    free(folder);
    qa_cleanup_pop(&load_cleanup);
    qa_app_options_destroy(&load_options);
  } else {
    qa_cleanup_pop(&folder_guard);
    free(folder);
    errno = 0;
    if (!qa_read_saved_game(path, &backing->saved)) raise_read_error(errno, path);
    backing->has_saved = true;
    if (backing->saved.kind == QA_SAVED_GAME_SHARED) {
      out->image = backing->saved.image;
      qa_cleanup_pop(&load_cleanup);
      out->options = load_options;
    } else {
      const qa_q1_save_data *data = &backing->saved.data;
      const qa_catalog_discovered_product *product = qa_select_q1_save_product_in_catalog(catalog, data, path, source_product);
      double skill = data->skill;
      if (skill != 0 && skill != 1 && skill != 2 && skill != 3) qa_raise("Error", 0, 0, "Original save skill must be 0..3");
      qa_app_options restored = clone_options(&load_options);
      qa_cleanup restored_cleanup;
      qa_cleanup_push(&restored_cleanup, options_cleanup, &restored);
      strip_source_options(&restored);
      set_owned(&restored.product, product->expectation.id);
      const char *map = data->map != NULL ? data->map : "";
      size_t map_path_length = strlen(map) + 10;
      char *map_path = checked(malloc(map_path_length));
      snprintf(map_path, map_path_length, "maps/%s.bsp", map);
      free(restored.map);
      restored.map = map_path;
      restored.skill = skill;
      restored.mode = "singleplayer";
      restored.movement = QA_GAME_FAMILY_Q1;
      restored.character = QA_GAME_FAMILY_Q1;
      set_owned(&restored.character_model, "player");
      restored.seats = 1;
      restored.rules = "standard";
      set_owned(&restored.quakec_program, "progs.dat");
      set_offline(&restored);
      import_world(backing, &restored, catalog, "original-save-import", data, out);
      qa_cleanup_pop(&restored_cleanup);
      out->options = restored;
      qa_cleanup_pop(&load_cleanup);
      qa_app_options_destroy(&load_options);
    }
  }
  qa_cleanup_pop(&backing_node);
  *out_backing = backing;
}

void qa_app_prepared_application_save_free(qa_app_prepared_application_save *save, void *backing) {
  qa_app_options_destroy(&save->options);
  if (save->has_folder) qa_q2_save_files_free(&save->folder);
  backing_free(backing);
  memset(save, 0, sizeof *save);
}
