/* SharedSimulation.diagnostics() (runtime.ts) over the interface of src/app/bootstrap/simulation/diagnostics.ts:
 * typed read access to match-layer state (Tag, LMCTF, DeathBall, Horde) and the movement/attack mutations tests drive
 * it with. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/diagnostics.h"
#include <string.h>
#include "shared_simulation_internal.h"
#include "foreign_match_host.h"

static bool dg_mode_tag(void *ctx) { return qa_app_shared_sim_foreign_tag(ctx); }
static bool dg_mode_lmctf(void *ctx) { return qa_app_shared_sim_foreign_lmctf(ctx); }
static bool dg_mode_ctf(void *ctx) { return qa_app_shared_sim_foreign_ctf(ctx); }
static bool dg_mode_deathball(void *ctx) { return qa_app_shared_sim_foreign_deathball(ctx); }
static bool dg_tag_token(void *ctx, qa_owned_actor *out) {
  qa_app_shared_simulation *sim = ctx;
  if (!sim->has_tag_token) return false;
  *out = sim->tag_token; return true;
}
static bool dg_tag_owner(void *ctx, qa_actor_id *out) {
  qa_app_shared_simulation *sim = ctx;
  if (!sim->has_tag_owner) return false;
  *out = sim->tag_owner; return true;
}
static double dg_tag_count(void *ctx) { return ((qa_app_shared_simulation *)ctx)->tag_count; }
static double dg_tag_tracked_health(void *ctx) { return (double)((qa_app_shared_simulation *)ctx)->tag_health.count; }
static bool dg_tag_tracks_health(void *ctx, qa_actor_id actor) { return qa_ss_map_has(&((qa_app_shared_simulation *)ctx)->tag_health, &actor); }
static bool dg_lmctf_flag(void *ctx, double team, qa_app_simulation_diagnostics_flag *out) {
  const qa_ss_lmctf_flag *flag = qa_ss_map_get(&((qa_app_shared_simulation *)ctx)->lmctf_flags, &team);
  if (flag == NULL) return false;
  *out = (qa_app_simulation_diagnostics_flag){flag->has_actor, flag->actor, flag->has_carrier, flag->carrier, flag->home,
                                              flag->has_dropped_at, flag->dropped_at};
  return true;
}
static bool dg_lmctf_rune(void *ctx, const char *kind, qa_app_simulation_diagnostics_rune *out) {
  qa_app_shared_simulation *sim = ctx;
  for (size_t i = 0; i < sim->lmctf_runes.count; i++) {
    if (strcmp(qa_ss_map_key_at(&sim->lmctf_runes, i)->string, kind) != 0) continue;
    const qa_ss_lmctf_rune *rune = qa_ss_map_value_at(&sim->lmctf_runes, i);
    *out = (qa_app_simulation_diagnostics_rune){rune->has_actor, rune->actor, rune->has_holder, rune->holder};
    return true;
  }
  return false;
}
static bool dg_team(const qa_ss_map *teams, qa_actor_id actor, double *out) {
  const double *team = qa_ss_map_get(teams, &actor);
  if (team == NULL) return false;
  *out = *team; return true;
}
static bool dg_lmctf_team(void *ctx, qa_actor_id actor, double *out) { return dg_team(&((qa_app_shared_simulation *)ctx)->lmctf_teams, actor, out); }
static bool dg_deathball_ball(void *ctx, qa_owned_actor *out) {
  qa_app_shared_simulation *sim = ctx;
  if (!sim->has_deathball) return false;
  *out = sim->deathball; return true;
}
static bool dg_deathball_team(void *ctx, qa_actor_id actor, double *out) { return dg_team(&((qa_app_shared_simulation *)ctx)->deathball_teams, actor, out); }
/* this.#spots?.goals.get(team) */
static bool dg_deathball_goal(void *ctx, double team, qa_vec3 *out) {
  qa_app_shared_simulation *sim = ctx;
  const qa_vec3 *goal = sim->has_deathball_spots ? qa_ss_map_get(&sim->deathball_goals, &team) : NULL;
  if (goal == NULL) return false;
  *out = *goal; return true;
}
static double dg_horde_wave(void *ctx) { return ((qa_app_shared_simulation *)ctx)->horde_wave; }
static double dg_horde_pending_wave(void *ctx) { return ((qa_app_shared_simulation *)ctx)->horde_pending_wave; }
static double dg_horde_fragged_count(void *ctx) { return (double)((qa_app_shared_simulation *)ctx)->horde_fragged.count; }
static qa_app_simulation_diagnostics_source dg_source(void *ctx) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_simulation_diagnostics_source source = {sim->strategy->kind, false, NULL, NULL};
  if (sim->strategy->as_q1(sim) != NULL) {
    source.has_q1 = true;
    source.q1_program = qa_comp_q1_program_name(sim->source.q1_composition->selection.program);
    source.q1_edition = sim->source.q1_game->options.edition;
  }
  return source;
}
static qa_q1_services *dg_q1_services(void *ctx) { return qa_app_shared_sim_q1_combat_source(ctx); }
static bool dg_grant_arsenal(void *ctx, qa_actor_id actor, const char *category) { return qa_app_shared_sim_grant_selected_arsenal(ctx, actor, category); }
static bool dg_select_arsenal_weapon(void *ctx, qa_actor_id actor, qa_item_id item) {
  qa_app_shared_simulation *sim = ctx;
  return sim->has_selected_arsenal && qa_app_shared_sim_selected_arsenal_select(sim, actor, item);
}
static qa_source_match_player_result dg_match_player(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_primary_match_player(ctx, actor); }
static void dg_move_player(void *ctx, qa_actor_id actor, qa_q2_player_movement_change change) { qa_app_shared_sim_set_player_movement(ctx, actor, change); }
static void dg_record_attack(void *ctx, qa_owned_actor actor, qa_attack_provenance attack) {
  qa_app_shared_sim_record_attack(ctx, actor, &attack);
}

qa_app_simulation_diagnostics qa_app_shared_sim_diagnostics(qa_app_shared_simulation *sim) {
  return (qa_app_simulation_diagnostics){
    sim, dg_mode_tag, dg_mode_lmctf, dg_mode_ctf, dg_mode_deathball,
    dg_tag_token, dg_tag_owner, dg_tag_count, dg_tag_tracked_health, dg_tag_tracks_health,
    dg_lmctf_flag, dg_lmctf_rune, dg_lmctf_team,
    dg_deathball_ball, dg_deathball_team, dg_deathball_goal,
    dg_horde_wave, dg_horde_pending_wave, dg_horde_fragged_count,
    dg_source, dg_q1_services, dg_grant_arsenal, dg_select_arsenal_weapon, dg_match_player, dg_move_player, dg_record_attack};
}
