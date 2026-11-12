/* Port of src/content/q3/base/game/save-state.ts. GPL-2.0-or-later.
 * Body, combat and inventory state live in the session tables, so the graph
 * carries exactly the TS Q3GraphState fields. */
#ifndef QA_CONTENT_Q3_BASE_GAME_SAVE_STATE_H
#define QA_CONTENT_Q3_BASE_GAME_SAVE_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/save_values.h"
#include "qa/content/q3/base/records.h"
#include "qa/world/actors/registry.h"
typedef struct qa_q3_saved_actor_ref { bool present; double slot, generation; } qa_q3_saved_actor_ref;
typedef struct qa_q3_game_save_entity_state {
  qa_q3_entity_values values; qa_q3_network_values network;
  qa_q3_base_trajectory pos, apos;
  double sv_flags, single_client, contents, owner_num, model_kind, model_index;
  bool has_client; double client; bool classname_is_client; char *classname; double classname_client;
  bool has_parent, has_next_train, has_prev_train, has_target_ent, has_chain, has_enemy, has_activator, has_teamchain, has_teammaster;
  double parent, next_train, prev_train, target_ent, chain, enemy, activator, teamchain, teammaster;
  bool has_item; double item; double nextthink;
  char *think, *reached, *blocked, *touch, *use, *pain, *die;
  /* TS activation: 0 null, 1 entity slot, 2 actor. */
  int activation_kind; double activation_slot; qa_q3_saved_actor_ref activation_actor;
  /* TS sharedPrivate: previousLink (null when !has_previous_link) and the
   * absMin/absMax overrides (null when !has_*). */
  bool has_previous_link; qa_q3_saved_actor_ref previous_link_actor; double previous_link_count;
  qa_bounds previous_link_absolute_bounds; qa_vec3 previous_link_origin, previous_link_angles, previous_link_velocity;
  qa_bounds previous_link_bounds; qa_q3_saved_actor_ref previous_link_ground;
  bool has_abs_min_override, has_abs_max_override; qa_vec3 abs_min_override, abs_max_override;
} qa_q3_game_save_entity_state;
typedef struct qa_q3_game_save_client_state {
  qa_q3_client_values values; qa_q3_player_values player; qa_q3_persistant_values persistant;
  qa_q3_base_user_command command; qa_q3_team_values team; qa_q3_session_values session;
  double *events, *event_parms, *persistant_slots, *powerups, *ammo_times;
  size_t event_count, event_parm_count, persistant_count, powerup_count, ammo_time_count;
  bool has_hook, has_persistant_powerup; double hook, persistant_powerup;
  /* TS backing (sourceStats/specialAmmo: 16 values each when captured; a read keeps whatever length the
   * save holds and restoreClientBacking rejects any other) and areabits. */
  double *source_stats, *special_ammo; size_t source_stat_count, special_ammo_count;
  bool has_areabits; uint8_t *areabits; size_t areabits_length;
} qa_q3_game_save_client_state;
typedef struct qa_q3_graph_ownership { bool has_actor; qa_q3_saved_actor_ref actor; bool active, borrowed; } qa_q3_graph_ownership;
typedef struct qa_q3_graph_state {
  qa_q3_graph_ownership *ownership; size_t ownership_count;
  qa_q3_game_save_entity_state *entities; size_t entity_count;
  qa_q3_game_save_client_state *clients; size_t client_count;
  double num_entities, max_clients;
} qa_q3_graph_state;
void qa_q3_graph_state_destroy(qa_q3_graph_state *state);
/* TS captureQ3Graph/prepareQ3Graph/restoreQ3Graph parameters. Items index
 * itemList(records.product); records may be NULL only for qa_q3_restore_graph. */
typedef struct qa_q3_graph_services {
  qa_q3_base_entity_records *records; qa_q3_entity_pool *pool; qa_session_actor_registry *actors;
} qa_q3_graph_services;
qa_q3_graph_state qa_q3_capture_q3_graph(qa_q3_graph_services services);
void qa_q3_prepare_q3_graph(qa_q3_graph_services services, const qa_q3_graph_state *state);
void qa_q3_restore_q3_graph(qa_q3_graph_services services, qa_q3_graph_state *state);
/* Plain checkpoint tree of TS captureQ3Graph's return value. NULL never (OOM aborts). */
qa_save_value *qa_q3_graph_state_to_value(const qa_q3_graph_state *state);
/* Same as qa_q3_read_graph (save_reader.h), which reads every TS readQ3Graph field; kept for its callers. */
qa_q3_graph_state qa_q3_read_complete_graph(const qa_save_value *value);
/* C-only: restores without records or an actor registry. Classnames, activations, shared private state,
 * areabits, items and ownership are not touched. No TS counterpart; restoreQ3Graph is qa_q3_restore_q3_graph. */
void qa_q3_restore_graph(qa_q3_entity_pool *pool, qa_q3_graph_state *state);
#endif
