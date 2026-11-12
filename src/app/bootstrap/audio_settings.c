/* Port of src/app/bootstrap/audio-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/foundation/caught_message.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
/* TS preferences(unknown) over a parsed JSON value; raises the TS Error. */
static qa_app_audio_preferences preferences(const qa_save_value *value) {
  qa_app_audio_preferences out = {0};
  out.output_format = qa_default_audio_output_format;
  if (!value || value->kind != QA_SAVE_OBJECT) qa_raise("Error", 0, 0, "Invalid audio preferences");
  const qa_save_value *version = qa_save_value_get(value, "version");
  const qa_save_value *device = qa_save_value_get(value, "deviceName");
  const qa_save_value *effects = qa_save_value_get(value, "effectsVolume");
  const qa_save_value *music = qa_save_value_get(value, "musicVolume");
  /* deviceName.length > 0 && !deviceName.includes("\0"): a "\u0000" escape is a NUL inside text_length. */
  bool device_ok = device && (device->kind == QA_SAVE_NULL ||
      (device->kind == QA_SAVE_STRING && device->text[0] != '\0' && device->text_length == 0));
  if (!version || version->kind != QA_SAVE_NUMBER || version->number != 1 || !device_ok ||
      !effects || effects->kind != QA_SAVE_NUMBER || !isfinite(effects->number) || effects->number < 0 || effects->number > 1 ||
      !music || music->kind != QA_SAVE_NUMBER || !isfinite(music->number) || music->number < 0 || music->number > 1)
    qa_raise("Error", 0, 0, "Invalid audio preferences");
  const qa_save_value *shuffle = qa_save_value_get(value, "musicShuffle");
  const qa_save_value *track = qa_save_value_get(value, "menuTrack");
  bool shuffle_value = shuffle && shuffle->kind == QA_SAVE_BOOLEAN ? shuffle->boolean : false;
  const char *track_value = track && track->kind == QA_SAVE_STRING ? track->text : "auto";
  if ((shuffle && shuffle->kind != QA_SAVE_BOOLEAN) || (track && track->kind != QA_SAVE_STRING) || !qa_app_valid_menu_track(track_value))
    qa_raise("Error", 0, 0, "Invalid music preferences");
  const qa_save_value *format = qa_save_value_get(value, "outputFormat");
  if (format) {
    /* TS audioOutputFormat(unknown): its RangeError reaches loadAudioSettings' catch. */
    const qa_save_value *rate = format->kind == QA_SAVE_OBJECT ? qa_save_value_get(format, "sampleRate") : NULL;
    const qa_save_value *channels = format->kind == QA_SAVE_OBJECT ? qa_save_value_get(format, "channels") : NULL;
    const qa_save_value *bits = format->kind == QA_SAVE_OBJECT ? qa_save_value_get(format, "sampleBits") : NULL;
    if (!rate || rate->kind != QA_SAVE_NUMBER || !(fabs(rate->number) <= 9007199254740991.0) || rate->number != trunc(rate->number) ||
        rate->number < 8000 || rate->number > 192000 || !channels || channels->kind != QA_SAVE_NUMBER ||
        (channels->number != 1 && channels->number != 2) || !bits || bits->kind != QA_SAVE_NUMBER ||
        (bits->number != 8 && bits->number != 16))
      qa_raise("RangeError", 0, 0, "Audio output requires 8000\xe2\x80\x93" "192000 Hz, 1 or 2 channels, and 8 or 16 bits");
    out.output_format.sample_rate = (int64_t)rate->number;
    out.output_format.channels = (int64_t)channels->number;
    out.output_format.sample_bits = (int64_t)bits->number;
    out.has_output_format = true;
  } else {
    /* TS: outputFormat: "outputFormat" in value ? audioOutputFormat(value.outputFormat) : defaultAudioOutputFormat. */
    out.output_format = qa_default_audio_output_format;
    out.has_output_format = true;
  }
  if (shuffle) { out.has_music_shuffle = true; out.music_shuffle = shuffle_value; }
  if (track) { out.has_menu_track = true; out.menu_track = dup(track_value); }
  out.device_name = device->kind == QA_SAVE_NULL ? NULL : dup(device->text);
  out.effects_volume = effects->number;
  out.music_volume = music->number;
  return out;
}
typedef struct preferences_call { const qa_save_value *value; qa_app_audio_preferences out; } preferences_call;
static void preferences_body(void *ctx) {
  preferences_call *call = ctx;
  call->out = preferences(call->value);
}
bool qa_app_load_audio_settings(const qa_config_store *store, qa_app_audio_preferences *out) {
  return qa_app_load_audio_settings_warn(store, out, NULL, NULL);
}
bool qa_app_load_audio_settings_warn(const qa_config_store *store, qa_app_audio_preferences *out,
    void (*warn)(void *ctx, const char *text), void *warn_ctx) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, "audio.json", &length);
  if (!text) return false;
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, true);
  free(text);
  if (!value) return false;
  /* A hand-edited or foreign file falls back to defaults instead of stopping the launch
   * (TS warn defaults to a no-op). */
  preferences_call call = {value, {0}};
  qa_caught_error caught;
  bool valid = qa_try(preferences_body, &call, &caught);
  qa_save_value_free(value);
  if (!valid) {
    if (warn != NULL) {
      const char *message = qa_caught_message(&caught);
      size_t size = strlen(message) + 32;
      char *text = malloc(size);
      if (text == NULL) qa_range_error("Out of memory");
      snprintf(text, size, "Ignoring audio.json: %s\n", message);
      qa_cleanup node;
      qa_cleanup_push(&node, free, text);
      warn(warn_ctx, text);
      qa_cleanup_pop(&node);
      free(text);
    }
    return false;
  }
  *out = call.out;
  return true;
}
/* The document under construction: grows with the text (TS JSON.stringify has no size limit). */
typedef struct json_text { char *data; size_t length, capacity; } json_text;
static void json_text_release(void *value) { free(((json_text *)value)->data); }
static void json_append(json_text *out, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list measure;
  va_copy(measure, args);
  int needed = vsnprintf(NULL, 0, format, measure);
  va_end(measure);
  if (needed < 0) { va_end(args); qa_range_error("Out of memory"); }
  if (out->length + (size_t)needed + 1 > out->capacity) {
    size_t capacity = out->capacity == 0 ? 256 : out->capacity;
    while (out->length + (size_t)needed + 1 > capacity) capacity *= 2;
    char *grown = realloc(out->data, capacity);
    if (grown == NULL) { va_end(args); qa_range_error("Out of memory"); }
    out->data = grown; out->capacity = capacity;
  }
  vsnprintf(out->data + out->length, out->capacity - out->length, format, args);
  va_end(args);
  out->length += (size_t)needed;
}
static void append_json_string(json_text *out, const char *text) {
  json_append(out, "\"");
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    unsigned char c = *p;
    if (c == '"' || c == '\\') json_append(out, "\\%c", c);
    else if (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') /* JSON.stringify's short escapes */
      json_append(out, "\\%c", c == '\b' ? 'b' : c == '\f' ? 'f' : c == '\n' ? 'n' : c == '\r' ? 'r' : 't');
    else if (c < 0x20) json_append(out, "\\u%04x", c);
    else json_append(out, "%c", c);
  }
  json_append(out, "\"");
}
static void saved_preferences_release(void *value) {
  qa_app_audio_preferences *saved = value;
  free(saved->menu_track); free(saved->device_name);
  saved->menu_track = NULL; saved->device_name = NULL;
}
void qa_app_save_audio_settings(const qa_config_store *store, qa_app_audio_settings_input audio) {
  bool shuffle = false;
  const char *track = "auto";
  qa_app_audio_preferences saved = {0};
  bool have_saved = false;
  if (audio.has_music_preferences) { shuffle = audio.music_shuffle; track = audio.menu_track; }
  else {
    have_saved = qa_app_load_audio_settings(store, &saved);
    if (have_saved) {
      shuffle = saved.has_music_shuffle ? saved.music_shuffle : false;
      track = saved.has_menu_track ? saved.menu_track : "auto";
    }
  }
  /* The loaded document's strings and the text built below are freed on every path (a validation raise too). */
  qa_cleanup saved_guard, text_guard;
  qa_cleanup_push(&saved_guard, saved_preferences_release, &saved);
  json_text out = {NULL, 0, 0};
  qa_cleanup_push(&text_guard, json_text_release, &out);
  /* preferences({...}) validation, mirrored on the constructed fields. */
  if (!qa_app_valid_menu_track(track)) qa_raise("Error", 0, 0, "Invalid music preferences");
  if ((audio.selected_output && audio.selected_output[0] == '\0') ||
      !isfinite(audio.effects_volume) || audio.effects_volume < 0 || audio.effects_volume > 1 ||
      !isfinite(audio.music_volume) || audio.music_volume < 0 || audio.music_volume > 1)
    qa_raise("Error", 0, 0, "Invalid audio preferences");
  qa_audio_output_format format = audio.has_output_format ? audio.output_format : qa_default_audio_output_format;
  format = qa_audio_output_format_validated(format.sample_rate, format.channels, format.sample_bits);
  json_append(&out, "{\"version\":1,\"musicShuffle\":%s,\"menuTrack\":", shuffle ? "true" : "false");
  append_json_string(&out, track);
  json_append(&out, ",\"outputFormat\":{\"sampleRate\":%lld,\"channels\":%lld,\"sampleBits\":%lld},\"deviceName\":",
      (long long)format.sample_rate, (long long)format.channels, (long long)format.sample_bits);
  if (audio.selected_output) append_json_string(&out, audio.selected_output);
  else json_append(&out, "null");
  json_append(&out, ",\"effectsVolume\":%s,", QA_JS_NUMBER(audio.effects_volume));
  json_append(&out, "\"musicVolume\":%s}\n", QA_JS_NUMBER(audio.music_volume));
  bool written = qa_config_store_dump(store, "audio.json", out.data);
  qa_cleanup_pop(&text_guard);
  qa_cleanup_pop(&saved_guard);
  json_text_release(&out);
  saved_preferences_release(&saved);
  if (!written) qa_settings_raise_write_failure();
}
/* TS saveAudioSettings(store, audio) with an ApplicationAudio: its musicPreferences getter is always
 * defined, so the stored music settings never come from the document. */
void qa_app_save_audio_settings_from(const qa_config_store *store, qa_app_application_audio *audio) {
  qa_app_audio_music_preferences music = qa_app_application_audio_music_preferences(audio);
  qa_app_audio_settings_input input;
  memset(&input, 0, sizeof input);
  input.has_music_preferences = true; input.music_shuffle = music.music_shuffle; input.menu_track = music.menu_track;
  input.has_output_format = true; input.output_format = qa_app_application_audio_output_format(audio);
  input.selected_output = qa_app_application_audio_selected_output(audio);
  input.effects_volume = qa_app_application_audio_effects_volume(audio);
  input.music_volume = qa_app_application_audio_music_volume(audio);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, music.menu_track);
  qa_app_save_audio_settings(store, input);
  qa_cleanup_pop(&cleanup);
  free(music.menu_track);
}
