/* Port of src/content/q2/base/player/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

qa_q2_base_player_rules qa_q2_create_player_rules(const qa_q2_base_player_rules *changes, uint32_t present_mask) {
  qa_q2_base_player_rules rules = {0};
  rules.password = ""; rules.spectator_password = "";
  rules.max_spectators = 4; rules.cheats = false;
  rules.time_limit_minutes = 0; rules.frag_limit = 0;
  rules.map_list = NULL; rules.map_list_count = 0; rules.map_list_shuffle = false;
  rules.next_map = ""; rules.spawn_point = "";
  rules.flood_messages = 4; rules.flood_seconds = 4; rules.flood_wait_seconds = 10;
  rules.roll_speed = 200; rules.roll_angle = 2;
  rules.run_pitch = 0.002; rules.run_roll = 0.005;
  rules.bob_up = 0.005; rules.bob_pitch = 0.002; rules.bob_roll = 0.002;
  rules.gun_offset = (qa_vec3){0, 0, 0};
  if (!changes) return rules;
  if (present_mask & QA_Q2_RULE_PASSWORD) rules.password = changes->password;
  if (present_mask & QA_Q2_RULE_SPECTATOR_PASSWORD) rules.spectator_password = changes->spectator_password;
  if (present_mask & QA_Q2_RULE_MAX_SPECTATORS) rules.max_spectators = changes->max_spectators;
  if (present_mask & QA_Q2_RULE_CHEATS) rules.cheats = changes->cheats;
  if (present_mask & QA_Q2_RULE_TIME_LIMIT) rules.time_limit_minutes = changes->time_limit_minutes;
  if (present_mask & QA_Q2_RULE_FRAG_LIMIT) rules.frag_limit = changes->frag_limit;
  if (present_mask & QA_Q2_RULE_MAP_LIST) { rules.map_list = changes->map_list; rules.map_list_count = changes->map_list_count; }
  if (present_mask & QA_Q2_RULE_MAP_LIST_SHUFFLE) rules.map_list_shuffle = changes->map_list_shuffle;
  if (present_mask & QA_Q2_RULE_NEXT_MAP) rules.next_map = changes->next_map;
  if (present_mask & QA_Q2_RULE_SPAWN_POINT) rules.spawn_point = changes->spawn_point;
  if (present_mask & QA_Q2_RULE_FLOOD_MESSAGES) rules.flood_messages = changes->flood_messages;
  if (present_mask & QA_Q2_RULE_FLOOD_SECONDS) rules.flood_seconds = changes->flood_seconds;
  if (present_mask & QA_Q2_RULE_FLOOD_WAIT) rules.flood_wait_seconds = changes->flood_wait_seconds;
  if (present_mask & QA_Q2_RULE_ROLL_SPEED) rules.roll_speed = changes->roll_speed;
  if (present_mask & QA_Q2_RULE_ROLL_ANGLE) rules.roll_angle = changes->roll_angle;
  if (present_mask & QA_Q2_RULE_RUN_PITCH) rules.run_pitch = changes->run_pitch;
  if (present_mask & QA_Q2_RULE_RUN_ROLL) rules.run_roll = changes->run_roll;
  if (present_mask & QA_Q2_RULE_BOB_UP) rules.bob_up = changes->bob_up;
  if (present_mask & QA_Q2_RULE_BOB_PITCH) rules.bob_pitch = changes->bob_pitch;
  if (present_mask & QA_Q2_RULE_BOB_ROLL) rules.bob_roll = changes->bob_roll;
  if (present_mask & QA_Q2_RULE_GUN_OFFSET) rules.gun_offset = changes->gun_offset;
  return rules;
}

static qa_inventory_entry *copy_entries(const qa_inventory_entry *entries, size_t count) {
  if (count == 0) return NULL;
  qa_inventory_entry *copy = malloc(count * sizeof *copy);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, entries, count * sizeof *copy);
  return copy;
}

void qa_q2_base_player_carry_set(qa_q2_base_player_carry *carry, qa_q2_base_player_carry value) {
  qa_inventory_entry *copy = copy_entries(value.inventory, value.inventory_count);
  free(carry->inventory);
  *carry = value;
  carry->inventory = copy;
}
void qa_q2_base_player_carry_clear(qa_q2_base_player_carry *carry) {
  free(carry->inventory);
  carry->inventory = NULL;
  carry->inventory_count = 0;
}

void qa_q2_base_player_state_init(qa_q2_base_player_state *state, double slot, double entered_at) {
  *state = (qa_q2_base_player_state){0};
  state->slot = slot;
  state->entered_at = entered_at;
  state->use_q2_weapons = true;
  state->use_q2_inventory = true;
  qa_q2_base_player_state_set_name(state, "");
  qa_q2_base_player_state_set_userinfo(state, "");
  qa_q2_base_player_state_set_skin(state, "male/grunt", 10);
  state->gender = "male";
  state->fov = 90;
  state->hand = "right";
  state->connected = true;
  state->drown_damage = 2;
  state->event = "";
  state->animation_end = 39;
  state->loop_sound = "";
  state->selected_item = "q2:weapon_blaster";
}

void qa_q2_base_player_state_destroy(qa_q2_base_player_state *state) {
  free(state->spawn_inventory);
  state->spawn_inventory = NULL;
  state->spawn_inventory_count = 0;
  qa_q2_base_player_carry_clear(&state->coop_respawn);
  free(state->flood_times);
  state->flood_times = NULL;
  state->flood_time_count = 0;
  state->flood_time_cap = 0;
  free(state->userinfo);
  state->userinfo = NULL;
  free(state->skin);
  state->skin = NULL;
  free(state->name);
  state->name = NULL;
}

static char *copy_bytes(const char *text, size_t length) {
  char *copy = malloc(length + 1);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}
void qa_q2_base_player_state_set_userinfo(qa_q2_base_player_state *state, const char *text) {
  char *copy = copy_bytes(text, strlen(text));
  free(state->userinfo);
  state->userinfo = copy;
}
void qa_q2_base_player_state_set_skin(qa_q2_base_player_state *state, const char *text, size_t length) {
  char *copy = copy_bytes(text, length);
  free(state->skin);
  state->skin = copy;
}
void qa_q2_base_player_state_set_name(qa_q2_base_player_state *state, const char *text) {
  char *copy = copy_bytes(text, strlen(text));
  free(state->name);
  state->name = copy;
}

char *qa_q2_text_slice_units(const char *text, size_t length, size_t units) {
  const unsigned char *bytes = (const unsigned char *)text;
  char *out = malloc(length + 1);
  if (!out) qa_range_error("Out of memory");
  size_t at = 0, used = 0, written = 0;
  while (at < length && used < units) {
    unsigned char lead = bytes[at];
    size_t size = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
    if (size > length - at) size = length - at;
    if (size == 4) {
      if (used + 2 > units) {
        uint32_t code = ((uint32_t)(lead & 0x07) << 18) | ((uint32_t)(bytes[at + 1] & 0x3F) << 12)
            | ((uint32_t)(bytes[at + 2] & 0x3F) << 6) | (uint32_t)(bytes[at + 3] & 0x3F);
        uint32_t high = 0xD800 + ((code - 0x10000) >> 10);
        out[written++] = (char)(0xE0 | (high >> 12));
        out[written++] = (char)(0x80 | ((high >> 6) & 0x3F));
        out[written++] = (char)(0x80 | (high & 0x3F));
        break;
      }
      used += 2;
    } else used++;
    memcpy(out + written, bytes + at, size);
    written += size; at += size;
  }
  out[written] = '\0';
  return out;
}
