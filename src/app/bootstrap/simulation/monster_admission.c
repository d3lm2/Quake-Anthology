/* Port of src/app/bootstrap/simulation/monster-admission.ts (MonsterAdmission, monsterSourceStrategy,
 * monsterExecutionStrategy): moved out of shared_simulation_worlds.c as TS moved it out of runtime.ts (no behavior
 * change). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_admission.h"
#include <string.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q1/foundation/runtime.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---- monsterSourceStrategy(source) ---- */
qa_trace_policy qa_app_sim_monster_source_trace_policy(const qa_app_sim_selected_monster_source *source) {
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  if (is(source->kind, "q2")) { policy.kind = "q2"; policy.contents_mask = 1; policy.leaf_contents = "merged"; }
  else { policy.kind = "q1"; policy.move = "normal"; policy.has_hull = false; }
  return policy;
}
double qa_app_sim_monster_source_medium(qa_app_sim_selected_monster_source *source, qa_vec3 origin) {
  if (is(source->kind, "q2")) {
    qa_q2_game_services *game = qa_q2_entity_services_game(source->q2->game);
    return qa_js_to_int32(game->host->point_contents(game->host->ctx, origin)) & 56;
  }
  qa_q1_contents contents = source->game->host.contents(source->game->host.ctx, origin);
  return is(contents, "water") ? 32 : is(contents, "slime") ? 16 : is(contents, "lava") ? 8 : 0;
}
void qa_app_sim_monster_source_set_route(qa_app_sim_selected_monster_source *source, qa_actor_id actor, const qa_actor_id *goal, double pause_until) {
  if (is(source->kind, "q2")) { qa_q2m_monsters_set_route(&source->q2->monsters, actor, goal, pause_until); return; }
  qa_q1_actor *entity = qa_q1_get_entity(source->game, &actor);
  if (!entity) qa_raise("Error", 0, 0, "Selected Q1 route actor has no source continuation");
  qa_q1_set_monster_route(source->game, entity, goal, pause_until);
}

/* ---- MonsterAdmission ---- */
qa_app_sim_selected_monster_source *qa_app_sim_monster_admission_strategy(const qa_app_sim_monster_admission *admission, qa_monster_definition_reference definition) {
  return admission->services.source_for(admission->services.ctx, definition);
}
qa_app_sim_monster_admission_probe qa_app_sim_monster_admission_probe_entry(const qa_app_sim_monster_admission *admission, qa_authored_monster *entry,
    qa_monster_definition_reference definition) {
  const qa_app_sim_monster_admission_services *services = &admission->services;
  qa_body_state_result body = qa_shared_body_table_read(qa_app_sim_shared_physics_bodies(services->physics), entry->base.actor.id);
  if (!body.has_value) qa_raise("Error", 0, 0, "Started selected monster has no shared body");
  qa_app_sim_selected_monster_source *source = qa_app_sim_monster_admission_strategy(admission, definition);
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = body.value.origin; query.end = body.value.origin; query.shape = (qa_trace_shape){"box", body.value.bounds};
  query.target.is_model = false; query.pass_actor = &entry->base.actor.id;
  query.numeric = qa_app_sim_provider_timing(*services->recipe, definition.source.provider).numeric;
  query.policy = qa_app_sim_monster_source_trace_policy(source);
  qa_trace_result trace = qa_shared_geometry_trace(services->scene, query);
  return (qa_app_sim_monster_admission_probe){body.value, source, trace.start_solid || trace.all_solid};
}
qa_trace_query qa_app_sim_monster_admission_query(const qa_app_sim_monster_admission *admission, qa_authored_monster *entry,
    qa_monster_definition_reference definition, const qa_app_sim_selected_monster_source *source) {
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.target.is_model = false; query.pass_actor = &entry->base.actor.id;
  query.numeric = qa_app_sim_provider_timing(*admission->services.recipe, definition.source.provider).numeric;
  query.policy = qa_app_sim_monster_source_trace_policy(source);
  return query;
}
noreturn void qa_app_sim_monster_admission_obstructed(const qa_app_sim_monster_admission *admission, qa_authored_monster *entry,
    qa_monster_definition_reference definition) {
  qa_raise("Error", 0, 0, "Selected monster placement obstructed in %s: source %s %s -> %s/%s",
    admission->services.recipe->map.geometry.requested_path, QA_JS_NUMBER(entry->source_ordinal), entry->base.classname,
    definition.source.provider, definition.classname);
}

/* ---- monsterExecutionStrategy(actor, entry, isLive): continuation after dormancy and the enemy links ---- */
void qa_app_sim_monster_admission_resume(const qa_app_sim_monster_admission *admission, qa_actor_id actor, const qa_actor_id *activator) {
  const qa_app_sim_monster_admission_services *services = &admission->services;
  qa_app_sim_actor_execution *entry = services->execution(services->ctx, actor);
  if (entry != NULL && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    qa_q1_actor *entity = entry->q1_entity; qa_q1_services *game = entry->q1_services;
    qa_q1_bound_action start = entity->think;
    qa_q1_cancel(game, entity);
    if (start.fn) start.fn(start.ctx, game, entity);
    if (qa_session_actor_registry_is_live(services->actors, actor) && activator && entity->use.fn) entity->use.fn(entity->use.ctx, game, entity, NULL, activator);
    return;
  }
  if (entry != NULL && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) {
    qa_q2_entity *entity = entry->q2_entity; qa_q2_game_services *game = qa_q2_entity_services_game(entry->q2_services);
    qa_q2_think start = entity->think;
    game->cancel(game->ctx, entity);
    if (start.fn) start.fn(start.ctx, entity, game);
    if (qa_session_actor_registry_is_live(services->actors, actor) && activator && entity->use.fn) entity->use.fn(entity->use.ctx, entity, game, NULL, activator);
    return;
  }
  qa_raise("Error", 0, 0, "Activated monster has no creature source continuation");
}
bool qa_app_sim_monster_admission_enemy(const qa_app_sim_monster_admission *admission, qa_actor_id actor, qa_actor_id *out) {
  qa_app_sim_actor_execution *entry = admission->services.execution(admission->services.ctx, actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    if (!entry->q1_entity->monster || !entry->q1_entity->monster->has_enemy) return false;
    *out = entry->q1_entity->monster->enemy; return true;
  }
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && entry->q2_entity->has_enemy) { *out = entry->q2_entity->enemy; return true; }
  return false;
}
bool qa_app_sim_monster_admission_old_enemy(const qa_app_sim_monster_admission *admission, qa_actor_id actor, qa_actor_id *out) {
  qa_app_sim_actor_execution *entry = admission->services.execution(admission->services.ctx, actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    if (!entry->q1_entity->monster || !entry->q1_entity->monster->has_old_enemy) return false;
    *out = entry->q1_entity->monster->old_enemy; return true;
  }
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && entry->read_monster) {
    qa_q2m_state *state = entry->read_monster(entry->ctx, entry->actor.id);
    if (state && state->has_old_enemy) { *out = state->old_enemy; return true; }
  }
  return false;
}
void qa_app_sim_monster_admission_set_route(const qa_app_sim_monster_admission *admission, qa_actor_id actor, const qa_actor_id *goal, double pause_until) {
  qa_monster_definition_reference definition;
  if (!admission->services.definition_of(admission->services.ctx, actor, &definition))
    qa_raise("Error", 0, 0, "Selected route actor has no creature definition");
  qa_app_sim_monster_source_set_route(qa_app_sim_monster_admission_strategy(admission, definition), actor, goal, pause_until);
}
