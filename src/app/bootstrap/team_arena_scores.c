/* Port of src/app/bootstrap/team-arena-scores.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/team_arena_scores.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/game_numeric.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js.h"
static double arg_at(const char *const *args, size_t count, size_t index) {
  const char *s = index < count ? args[index] : "";
  /* TS: split("\0",1)[0].slice(0,1023) - C string already NUL-cut. */
  char buf[1024];
  size_t n = strlen(s); if (n > 1023) n = 1023;
  memcpy(buf, s, n); buf[n] = '\0';
  return qa_game_atoi(buf);
}
qa_app_team_arena_postgame qa_app_parse_team_arena_postgame(const char *const *args, size_t count) {
  qa_app_team_arena_postgame r;
  r.accuracy = arg_at(args, count, 2); r.impressives = arg_at(args, count, 3);
  r.excellents = arg_at(args, count, 4); r.defends = arg_at(args, count, 5);
  r.assists = arg_at(args, count, 6); r.gauntlets = arg_at(args, count, 7);
  r.base_score = arg_at(args, count, 8); r.perfects = arg_at(args, count, 9);
  r.red_score = arg_at(args, count, 10); r.blue_score = arg_at(args, count, 11);
  r.end_time = arg_at(args, count, 12); r.captures = arg_at(args, count, 13);
  return r;
}
/* Math.imul: the low 32 bits of the product, wrapping (C signed overflow is undefined). */
static int32_t imul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
/* Bytes of the first `units` UTF-16 units of UTF-8 text (a surrogate pair cut in half is dropped whole). */
static size_t utf16_prefix(const char *text, size_t units) {
  const unsigned char *p = (const unsigned char *)text;
  size_t used = 0, at = 0;
  while (p[at] != '\0') {
    size_t width = p[at] >= 0xf0 ? 4 : p[at] >= 0xe0 ? 3 : p[at] >= 0xc0 ? 2 : 1;
    size_t cost = width == 4 ? 2 : 1;
    if (used + cost > units) break;
    for (size_t i = 1; i < width; i++) if (p[at + i] == '\0') { width = i; break; }
    used += cost; at += width;
  }
  return at;
}
qa_app_team_arena_score_result qa_app_calculate_team_arena_score(qa_app_team_arena_score_input input, qa_app_team_arena_score previous) {
  /* Math.fround staging: fround(fround(fround(end)-fround(start))/1000) */
  float diff = (float)((float)input.stats.end_time - (float)input.match_start_time);
  double time = qa_qvm_float_to_int((float)(diff / 1000.0f));
  double time_bonus = time < input.time_to_beat ? (double)imul(qa_js_to_int32(input.time_to_beat - time), 10) : 0;
  bool won = input.stats.red_score > input.stats.blue_score;
  double shutout = won && input.stats.blue_score <= 0 ? 100 : 0;
  double sb = qa_qvm_float_to_int(input.skill);
  double skill_bonus = sb > 1 ? sb : 1;
  qa_app_team_arena_score s;
  s.accuracy = input.stats.accuracy; s.impressives = input.stats.impressives;
  s.excellents = input.stats.excellents; s.defends = input.stats.defends;
  s.assists = input.stats.assists; s.gauntlets = input.stats.gauntlets;
  s.base_score = input.stats.base_score; s.perfects = input.stats.perfects;
  s.red_score = input.stats.red_score; s.blue_score = input.stats.blue_score;
  s.captures = input.stats.captures;
  s.time = time; s.time_bonus = time_bonus; s.shutout_bonus = shutout; s.skill_bonus = skill_bonus;
  s.score = (double)imul(qa_js_to_int32(s.base_score + shutout + time_bonus), qa_js_to_int32(skill_bonus));
  qa_app_team_arena_score_result r = {s, previous, won && s.score > previous.score, time < previous.time, won};
  return r;
}
char *qa_app_team_arena_score_path(const char *map, double game_type) {
  /* `games/${map.slice(0, 63)}_${gameType | 0}.game`.slice(0, 63), both slices in UTF-16 units. */
  char buf[512];
  char m[256]; size_t n = utf16_prefix(map, 63); memcpy(m, map, n); m[n] = '\0';
  snprintf(buf, sizeof buf, "games/%s_%d.game", m, (int)qa_int32(game_type));
  buf[utf16_prefix(buf, 63)] = '\0';
  char *r = qa_input_dup(buf);
  if (!r) qa_range_error("Out of memory");
  return r;
}
static void put32(uint8_t *out, size_t off, int32_t v) {
  out[off] = (uint8_t)v; out[off + 1] = (uint8_t)(v >> 8); out[off + 2] = (uint8_t)(v >> 16); out[off + 3] = (uint8_t)(v >> 24);
}
static int32_t get32(const uint8_t *b, size_t off) {
  return (int32_t)((uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24));
}
void qa_app_encode_team_arena_score(qa_app_team_arena_score info, uint8_t out[68]) {
  memset(out, 0, 68); put32(out, 0, 64);
  double f[16] = {info.score, info.red_score, info.blue_score, info.perfects, info.accuracy, info.impressives,
      info.excellents, info.defends, info.assists, info.gauntlets, info.captures, info.time,
      info.time_bonus, info.shutout_bonus, info.skill_bonus, info.base_score};
  for (size_t i = 0; i < 16; i++) put32(out, 4 + i * 4, qa_int32(f[i]));
}
qa_app_team_arena_score qa_app_decode_team_arena_score(const uint8_t *bytes, size_t length) {
  uint8_t padded[68]; memset(padded, 0, sizeof padded);
  if (bytes) memcpy(padded, bytes, length > 68 ? 68 : length);
  if (get32(padded, 0) != 64) memset(padded, 0, sizeof padded);
  qa_app_team_arena_score s;
  s.score = get32(padded, 4); s.red_score = get32(padded, 8); s.blue_score = get32(padded, 12);
  s.perfects = get32(padded, 16); s.accuracy = get32(padded, 20); s.impressives = get32(padded, 24);
  s.excellents = get32(padded, 28); s.defends = get32(padded, 32); s.assists = get32(padded, 36);
  s.gauntlets = get32(padded, 40); s.captures = get32(padded, 44); s.time = get32(padded, 48);
  s.time_bonus = get32(padded, 52); s.shutout_bonus = get32(padded, 56); s.skill_bonus = get32(padded, 60);
  s.base_score = get32(padded, 64);
  return s;
}
qa_app_team_arena_score_result qa_app_record_team_arena_score(qa_app_team_arena_score_input input, const char *map, double game_type, qa_app_team_arena_score_files files) {
  char *path = qa_app_team_arena_score_path(map, game_type);
  qa_cleanup node;
  qa_cleanup_push(&node, free, path);
  size_t len = 0; uint8_t *bytes = files.read_file(files.ctx, path, &len);
  qa_app_team_arena_score_result r = qa_app_calculate_team_arena_score(input, qa_app_decode_team_arena_score(bytes, len));
  free(bytes);
  if (r.new_high_score) { uint8_t enc[68]; qa_app_encode_team_arena_score(r.score, enc); files.write_file(files.ctx, path, enc, 68); }
  qa_cleanup_pop(&node);
  free(path);
  return r;
}
