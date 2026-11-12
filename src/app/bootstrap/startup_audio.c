/* Port of src/app/bootstrap/startup-audio.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_audio.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/audio/menu.h"
#include "qa/app/bootstrap/audio/playlist.h"
#include "qa/app/bootstrap/input_devices.h"
#include <math.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/network/common/js_number.h"
typedef struct queued {
  const char *name;
  char **args; size_t arg_count;
  void (*print)(void *ctx, const char *text); void *print_ctx;
} queued;
typedef struct candidate { qa_content_mounted *mounts; qa_app_music_source source; qa_sound_bank *bank; const char *const *names; size_t name_count; } candidate;
struct qa_app_startup_audio {
  qa_unified_audio *engine;
  qa_sound_bank *bank;       /* TS this.bank = new SoundBank(mounts) */
  qa_sound_bank *theme_bank; /* the theme candidate's new SoundBank(theme.mounts) */
  qa_app_application_music *music;
  qa_cvar_registry *output_cvars;
  qa_game_family family;
  qa_seat_id seat;
  void (*print)(void *ctx, const char *text); void *print_ctx;
  candidate candidates[2]; size_t candidate_count;
  char *menu_track;
  char **tracks; size_t track_count;
  queued commands[1024]; size_t command_head, command_count;
  struct { const char *event; const qa_sound_asset *sound; } sounds[8]; size_t sound_count;
  bool closed;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static char *xprintf(const char *format, ...) {
  va_list args, measure;
  va_start(args, format);
  va_copy(measure, args);
  int length = vsnprintf(NULL, 0, format, measure);
  va_end(measure);
  char *text = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (text == NULL) { va_end(args); qa_range_error("Out of memory"); }
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  return text;
}
/* The names and paths one selectMenuTrack candidate tries; released when a raise unwinds. */
typedef struct track_names { char *names[2]; char *paths[2]; } track_names;
static void track_names_clear(void *raw) {
  track_names *t = raw;
  for (size_t i = 0; i < 2; i++) { free(t->names[i]); free(t->paths[i]); t->names[i] = t->paths[i] = NULL; }
}
static void print_owned(qa_app_startup_audio *a, char *text) {
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, text);
  a->print(a->print_ctx, text);
  qa_cleanup_pop(&guard);
  free(text);
}
static void select_menu_track(qa_app_startup_audio *a, const char *value) {
  if (a->closed || (a->menu_track != NULL && strcmp(a->menu_track, value) == 0)) return;
  free(a->menu_track); a->menu_track = xdup(value);
  qa_app_application_music_stop_playback(a->music, "source");
  if (strcmp(value, "0") == 0) return;
  track_names owned = {{NULL, NULL}, {NULL, NULL}};
  qa_cleanup guard;
  qa_cleanup_push(&guard, track_names_clear, &owned);
  for (size_t ci = 0; ci < a->candidate_count; ci++) {
    /* /^\d+$/ (ASCII digits) */
    bool numeric = value[0] != '\0';
    for (const char *p = value; *p; p++) if (*p < '0' || *p > '9') numeric = false;
    char number_buf[64] = "";
    if (numeric) {
      /* String(Number(value)).padStart(2, "0") */
      char text[40];
      qa_net_js_number(qa_js_string_to_number(value), text, sizeof text);
      snprintf(number_buf, sizeof number_buf, "%s%s", strlen(text) < 2 ? "0" : "", text);
    }
    track_names_clear(&owned);
    const char *names[4];
    size_t name_count = 0;
    if (strcmp(value, "auto") == 0) {
      for (size_t i = 0; i < a->candidates[ci].name_count && name_count < 4; i++) names[name_count++] = a->candidates[ci].names[i];
    } else if (numeric) {
      owned.names[0] = xprintf("music/%s", number_buf);
      owned.names[1] = xprintf("music/track%s", number_buf);
      names[name_count++] = owned.names[0];
      names[name_count++] = owned.names[1];
    } else if (strncmp(value, "music/", 6) == 0) names[name_count++] = value;
    else {
      owned.names[0] = xprintf("music/%s", value);
      names[name_count++] = owned.names[0];
    }
    for (size_t ni = 0; ni < name_count; ni++) {
      const char *name = names[ni];
      size_t len = strlen(name);
      /* /\.(?:ogg|wav)$/i folds ASCII only */
      bool has_ext = len >= 4 && (strcasecmp(name + len - 4, ".ogg") == 0 || strcasecmp(name + len - 4, ".wav") == 0);
      const char *path_list[2];
      size_t path_count = 0;
      free(owned.paths[0]); free(owned.paths[1]);
      owned.paths[0] = owned.paths[1] = NULL;
      if (has_ext) path_list[path_count++] = name;
      else {
        owned.paths[0] = xprintf("%s.ogg", name);
        owned.paths[1] = xprintf("%s.wav", name);
        path_list[path_count++] = owned.paths[0];
        path_list[path_count++] = owned.paths[1];
      }
      for (size_t pi = 0; pi < path_count; pi++) {
        const qa_resolved_resource_reference *resolved = NULL;
        bool found = qa_content_mounted_resolve(a->candidates[ci].mounts, path_list[pi], &resolved);
        if (a->closed || a->menu_track == NULL || strcmp(a->menu_track, value) != 0) {
          qa_cleanup_pop(&guard);
          track_names_clear(&owned);
          return;
        }
        if (!found) continue;
        char *cue = qa_app_music_file_cue(path_list[pi]);
        qa_cleanup cue_guard;
        qa_cleanup_push(&cue_guard, free, cue);
        qa_app_application_music_play(a->music, a->candidates[ci].source, a->candidates[ci].bank, cue,
                                      (qa_app_open_music_track){NULL, NULL}, NULL);
        qa_cleanup_pop(&cue_guard);
        free(cue);
        qa_cleanup_pop(&guard);
        track_names_clear(&owned);
        return;
      }
    }
  }
  qa_cleanup_pop(&guard);
  track_names_clear(&owned);
  if (strcmp(value, "auto") != 0) print_owned(a, xprintf("Menu music unavailable: %s\n", value));
}
/* UnifiedAudio milliseconds: () => Math.trunc(performance.now()); random: () => 0. */
static double engine_milliseconds(void *ctx) {
  (void)ctx;
  /* performance.now() counts from the process time origin; the monotonic clock counts from boot. */
  static double origin = -1;
  double now = qa_app_performance_now();
  if (origin < 0) origin = now;
  return trunc(now - origin);
}
static double engine_random(void *ctx) {
  (void)ctx;
  return 0;
}
static void open_cleanup(void *arg) { qa_app_startup_audio_destroy(arg); }
qa_app_startup_audio *qa_app_startup_audio_open(const qa_app_startup_audio_options *options) {
  qa_app_startup_audio *a = calloc(1, sizeof *a);
  if (!a) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, open_cleanup, a);
  /* private constructor(mounts, family, seat, print, preferences, controls) */
  a->family = options->source.family;
  a->seat = options->seat;
  a->print = options->print; a->print_ctx = options->print_ctx;
  qa_unified_audio_options engine;
  memset(&engine, 0, sizeof engine);
  engine.milliseconds = engine_milliseconds;
  engine.random = engine_random;
  engine.has_output_format = options->preferences.has_output_format;
  engine.output_format = options->preferences.output_format;
  a->engine = qa_unified_audio_create(&engine);
  if (a->engine == NULL) qa_range_error("Out of memory");
  a->bank = qa_sound_bank_create(options->mounts);
  if (a->bank == NULL) qa_range_error("Out of memory");
  a->music = qa_app_application_music_create(a->engine, options->print, options->print_ctx,
                                             QA_MUSIC_VOLUME_IMMEDIATE, options->music_controls, NULL, NULL);
  if (a->music == NULL) qa_range_error("Out of memory");
  qa_app_startup_audio_set_volumes(a, options->preferences.has_effects_volume ? options->preferences.effects_volume : 0.7,
                                   options->preferences.has_music_volume ? options->preferences.music_volume : 0.25);
  qa_audio_listener listener;
  memset(&listener, 0, sizeof listener);
  listener.seat = options->seat;
  listener.axis.vectors[0] = (qa_vec3){1, 0, 0};
  listener.axis.vectors[1] = (qa_vec3){0, 1, 0};
  listener.axis.vectors[2] = (qa_vec3){0, 0, 1};
  listener.gain = 1;
  qa_unified_audio_set_listeners(a->engine, &listener, 1);
  /* static open(options) */
  static const char *const events[] = {"open", "close", "move", "change", "reject"};
  qa_sound_family sound_family = a->family == QA_GAME_FAMILY_Q1 ? QA_SOUND_FAMILY_Q1 : a->family == QA_GAME_FAMILY_Q2 ? QA_SOUND_FAMILY_Q2 : QA_SOUND_FAMILY_Q3;
  for (int i = 0; i < 5; i++) {
    const qa_sound_asset *sound = NULL;
    if (!qa_sound_bank_register(a->bank, qa_app_menu_sound_path(a->family, events[i]), sound_family, &sound)) qa_range_error("Out of memory");
    if (sound != NULL) { a->sounds[a->sound_count].event = events[i]; a->sounds[a->sound_count].sound = sound; a->sound_count++; }
  }
  qa_app_application_music_select(a->music, options->source, a->bank, (qa_app_open_music_track){NULL, NULL});
  static const char *const fallback_q1[] = {"music/track02", "music/02"};
  static const char *const fallback_q2[] = {"music/02", "music/track02"};
  static const char *const fallback_q3[] = {"music/sonic5"};
  static const char *const theme_names[] = {"music/track77"};
  const char *const *fallback = fallback_q3; size_t fallback_count = 1;
  if (a->family == QA_GAME_FAMILY_Q1) { fallback = fallback_q1; fallback_count = 2; }
  else if (a->family == QA_GAME_FAMILY_Q2) { fallback = fallback_q2; fallback_count = 2; }
  a->candidates[a->candidate_count++] = (candidate){options->mounts, options->source, a->bank, fallback, fallback_count};
  if (options->has_theme) {
    a->theme_bank = qa_sound_bank_create(options->theme_mounts);
    if (a->theme_bank == NULL) qa_range_error("Out of memory");
    memmove(&a->candidates[1], &a->candidates[0], sizeof(candidate));
    a->candidates[0] = (candidate){options->theme_mounts, options->theme_source, a->theme_bank, theme_names, 1};
    a->candidate_count++;
  }
  /* tracks: sorted unique union over candidates. */
  size_t cap = 0;
  for (size_t ci = 0; ci < a->candidate_count; ci++) {
    size_t count = 0;
    char **list = qa_app_mounted_music_tracks(a->candidates[ci].mounts, &count);
    for (size_t i = 0; i < count; i++) {
      bool dup = false;
      for (size_t j = 0; j < a->track_count; j++) if (strcmp(a->tracks[j], list[i]) == 0) dup = true;
      if (!dup) {
        if (a->track_count == cap) { cap = cap ? cap * 2 : 16; a->tracks = realloc(a->tracks, cap * sizeof *a->tracks); if (!a->tracks) qa_range_error("Out of memory"); }
        a->tracks[a->track_count++] = list[i];
      } else free(list[i]);
    }
    free(list);
  }
  /* [...new Set(...)].sort(): UTF-16 code-unit order (the names are unique). */
  for (size_t i = 0; i < a->track_count; i++)
    for (size_t j = i + 1; j < a->track_count; j++)
      if (qa_js_compare_code_units(a->tracks[j], a->tracks[i]) < 0) { char *tmp = a->tracks[i]; a->tracks[i] = a->tracks[j]; a->tracks[j] = tmp; }
  select_menu_track(a, options->preferences.menu_track ? options->preferences.menu_track : "auto");
  qa_cleanup_pop(&guard);
  return a;
}
void qa_app_startup_audio_destroy(qa_app_startup_audio *a) {
  if (!a) return;
  qa_app_startup_audio_close(a);
  if (a->music != NULL) qa_app_application_music_destroy(a->music);
  for (size_t i = 0; i < a->track_count; i++) free(a->tracks[i]);
  free(a->tracks); free(a->menu_track);
  if (a->engine != NULL) qa_unified_audio_destroy(a->engine);
  if (a->bank != NULL) qa_sound_bank_destroy(a->bank);
  if (a->theme_bank != NULL) qa_sound_bank_destroy(a->theme_bank);
  free(a);
}
qa_unified_audio *qa_app_startup_audio_engine(qa_app_startup_audio *a) { return a->engine; }
qa_audio_output_format qa_app_startup_audio_output_format(qa_app_startup_audio *a) {
  return qa_unified_audio_output_format(a->engine);
}
/* this.engine.selectOutput(name, format, restart): SdlAudioUnavailableError and failures throw. */
static void select_output(qa_unified_audio *engine, const char *name, qa_audio_output_format format, bool restart) {
  qa_sdl_audio_open_result result = qa_unified_audio_select_output(engine, name, &format, restart);
  if (result != QA_SDL_AUDIO_OPEN_OK)
    qa_raise("Error", 0, 0, "%s", result == QA_SDL_AUDIO_OPEN_FAILED ? "Out of memory" : qa_unified_audio_open_error(engine));
}
void qa_app_startup_audio_bind_output_cvars(qa_app_startup_audio *a, qa_cvar_registry *cvars) { a->output_cvars = cvars; }
void qa_app_startup_audio_select_output_format(qa_app_startup_audio *a, qa_audio_output_format format) {
  select_output(a->engine, qa_unified_audio_selected_output(a->engine), format, false);
  if (a->output_cvars != NULL) qa_app_write_audio_output_cvars(a->output_cvars, qa_app_startup_audio_output_format(a));
}
void qa_app_startup_audio_restart_output(qa_app_startup_audio *a) {
  qa_audio_output_format format = a->output_cvars == NULL ? qa_app_startup_audio_output_format(a)
                                                           : qa_app_read_audio_output_cvars(a->output_cvars);
  select_output(a->engine, qa_unified_audio_selected_output(a->engine), format, true);
}
const char *const *qa_app_startup_audio_music_tracks(qa_app_startup_audio *a, size_t *out_count) {
  *out_count = a->track_count;
  return (const char *const *)a->tracks;
}
qa_app_audio_music_preferences qa_app_startup_audio_music_preferences(qa_app_startup_audio *a) {
  if (a->output_cvars == NULL) {
    qa_app_audio_music_preferences prefs = {false, xdup(a->menu_track ? a->menu_track : "auto")};
    return prefs;
  }
  return qa_app_read_music_settings(a->output_cvars);
}
/* The thrown error's message: SdlAudioUnavailableError's SDL text, which the engine keeps for its last failed open. */
static const char *open_error(qa_unified_audio *engine, qa_sdl_audio_open_result result) {
  return result == QA_SDL_AUDIO_OPEN_FAILED ? "Out of memory" : qa_unified_audio_open_error(engine);
}
void qa_app_startup_audio_open_output(qa_app_startup_audio *a, const char *device_name, void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_sdl_audio_open_result result = qa_unified_audio_open_device(a->engine, device_name, 0);
  if (result == QA_SDL_AUDIO_OPEN_OK) return;
  if (device_name == NULL) qa_raise("Error", 0, 0, "%s", open_error(a->engine, result));
  char *text = xprintf("Audio output %s unavailable: %s. Using system default.\n", device_name, open_error(a->engine, result));
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, text);
  print(print_ctx, text);
  qa_cleanup_pop(&guard);
  free(text);
  result = qa_unified_audio_open_device(a->engine, NULL, 0);
  if (result != QA_SDL_AUDIO_OPEN_OK) qa_raise("Error", 0, 0, "%s", open_error(a->engine, result));
}
void qa_app_startup_audio_cd_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (!a->closed) qa_app_application_music_cd_command(a->music, args, count, print, print_ctx);
}
static void queue_command(qa_app_startup_audio *a, const char *name, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (a->closed) return;
  if (a->command_count == 1024) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)1024);
  queued *q = &a->commands[(a->command_head + a->command_count) % 1024];
  q->name = name;
  q->args = calloc(count ? count : 1, sizeof *q->args);
  if (!q->args) qa_range_error("Out of memory");
  q->arg_count = count;
  for (size_t i = 0; i < count; i++) q->args[i] = xdup(args[i]);
  q->print = print; q->print_ctx = print_ctx;
  a->command_count++;
}
void qa_app_startup_audio_queue_cd_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  queue_command(a, "cd", args, count, print, print_ctx);
}
void qa_app_startup_audio_queue_music_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  queue_command(a, "music", args, count, print, print_ctx);
}
static void queued_free(void *raw) {
  queued *command = raw;
  for (size_t i = 0; i < command->arg_count; i++) free(command->args[i]);
  free(command->args);
  command->args = NULL;
  command->arg_count = 0;
}
void qa_app_startup_audio_flush_commands(qa_app_startup_audio *a) {
  if (a->output_cvars != NULL) {
    qa_app_audio_music_preferences prefs = qa_app_read_music_settings(a->output_cvars);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, prefs.menu_track);
    select_menu_track(a, prefs.menu_track);
    qa_cleanup_pop(&guard);
    free(prefs.menu_track);
  }
  while (!a->closed && a->command_count > 0) {
    queued command = a->commands[a->command_head];
    a->command_head = (a->command_head + 1) % 1024;
    a->command_count--;
    qa_cleanup guard; /* the read record is retired; a raising command still releases its copy */
    qa_cleanup_push(&guard, queued_free, &command);
    if (strcmp(command.name, "cd") == 0) qa_app_startup_audio_cd_command(a, (const char *const *)command.args, command.arg_count, command.print, command.print_ctx);
    else qa_app_application_music_music_command(a->music, (const char *const *)command.args, command.arg_count, command.print, command.print_ctx);
    qa_cleanup_pop(&guard);
    queued_free(&command);
  }
}
void qa_app_startup_audio_set_volumes(qa_app_startup_audio *a, double effects, double music) {
  if (a->closed) return;
  qa_unified_audio_set_effects_volume(a->engine, effects);
  qa_app_application_music_set_volume(a->music, music);
}
void qa_app_startup_audio_sound(qa_app_startup_audio *a, qa_ui_sound event) {
  if (a->closed) return;
  for (size_t i = 0; i < a->sound_count; i++) {
    if (strcmp(a->sounds[i].event, event) != 0) continue;
    qa_play_sound request;
    memset(&request, 0, sizeof request);
    request.sound = a->sounds[i].sound;
    request.family = a->family == QA_GAME_FAMILY_Q1 ? QA_SOUND_FAMILY_Q1 : a->family == QA_GAME_FAMILY_Q2 ? QA_SOUND_FAMILY_Q2 : QA_SOUND_FAMILY_Q3;
    request.origin.kind = QA_SOUND_ORIGIN_LOCAL;
    request.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
    request.audience.seat = a->seat;
    request.channel = 0;
    request.volume = 1;
    request.attenuation = 0;
    (void)qa_unified_audio_play(a->engine, &request);
  }
}
void qa_app_startup_audio_pump(qa_app_startup_audio *a) {
  if (a->closed) return;
  qa_unified_audio_update_music(a->engine);
  (void)qa_unified_audio_pump(a->engine, false, 0, 0);
}
void qa_app_startup_audio_close(qa_app_startup_audio *a) {
  if (a->closed) return;
  a->closed = true;
  while (a->command_count > 0) {
    queued command = a->commands[a->command_head];
    a->command_head = (a->command_head + 1) % 1024;
    a->command_count--;
    for (size_t i = 0; i < command.arg_count; i++) free(command.args[i]);
    free(command.args);
  }
  if (a->music != NULL) qa_app_application_music_stop(a->music);
  if (a->engine != NULL) qa_unified_audio_close(a->engine);
  if (a->bank != NULL) qa_sound_bank_clear(a->bank);
  for (size_t i = 0; i < a->candidate_count; i++) qa_sound_bank_clear(a->candidates[i].bank);
  a->sound_count = 0;
}
