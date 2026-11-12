/* Port of src/app/bootstrap/simulation/monster-admission.ts: the creature-source half of selected-monster
 * admission, shared by every map program that hosts it. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_ADMISSION_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_ADMISSION_H
#include <stdbool.h>
#include <stdnoreturn.h>
#include "qa/app/bootstrap/simulation/actor_execution.h"
#include "qa/app/bootstrap/simulation/monster_runtime.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/content/monsters/authored.h"
#include "qa/contracts/content.h"
#include "qa/contracts/scene.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"

/* MonsterAdmissionServices. execution: NULL = TS undefined; definition_of: false = TS undefined. */
typedef struct qa_app_sim_monster_admission_services {
  const qa_executable_recipe *recipe;
  qa_session_actor_registry *actors;
  qa_app_sim_shared_physics *physics;
  qa_shared_scene_queries *scene;
  void *ctx;
  qa_app_sim_actor_execution *(*execution)(void *ctx, qa_actor_id actor);
  qa_app_sim_selected_monster_source *(*source_for)(void *ctx, qa_monster_definition_reference definition);
  bool (*definition_of)(void *ctx, qa_actor_id actor, qa_monster_definition_reference *out);
} qa_app_sim_monster_admission_services;
/* class MonsterAdmission. */
typedef struct qa_app_sim_monster_admission { qa_app_sim_monster_admission_services services; } qa_app_sim_monster_admission;

/* MonsterSourceStrategy of a creature source (TS monsterSourceStrategy(source)): its trace rules, the liquid class
 * bits of the contents at an origin (Q2 CONTENTS_WATER/SLIME/LAVA mask, or the Q1 equivalent mapped to the same
 * bits) and route control. */
qa_trace_policy qa_app_sim_monster_source_trace_policy(const qa_app_sim_selected_monster_source *source);
double qa_app_sim_monster_source_medium(qa_app_sim_selected_monster_source *source, qa_vec3 origin);
void qa_app_sim_monster_source_set_route(qa_app_sim_selected_monster_source *source, qa_actor_id actor, const qa_actor_id *goal, double pause_until);

/* strategy(definition): the source whose strategy applies. */
qa_app_sim_selected_monster_source *qa_app_sim_monster_admission_strategy(const qa_app_sim_monster_admission *admission, qa_monster_definition_reference definition);
/* probe(entry, definition): the shared body of a started monster and whether it begins embedded in solid world. */
typedef struct qa_app_sim_monster_admission_probe { qa_body_state body; qa_app_sim_selected_monster_source *source; bool solid; } qa_app_sim_monster_admission_probe;
qa_app_sim_monster_admission_probe qa_app_sim_monster_admission_probe_entry(const qa_app_sim_monster_admission *admission, qa_authored_monster *entry,
    qa_monster_definition_reference definition);
/* query(entry, definition, strategy): the query a relocation search runs with (pass_actor borrows the entry's actor). */
qa_trace_query qa_app_sim_monster_admission_query(const qa_app_sim_monster_admission *admission, qa_authored_monster *entry,
    qa_monster_definition_reference definition, const qa_app_sim_selected_monster_source *source);
/* throw admission.obstructed(entry, definition) */
noreturn void qa_app_sim_monster_admission_obstructed(const qa_app_sim_monster_admission *admission, qa_authored_monster *entry,
    qa_monster_definition_reference definition);
void qa_app_sim_monster_admission_resume(const qa_app_sim_monster_admission *admission, qa_actor_id actor, const qa_actor_id *activator);
/* false = TS null. */
bool qa_app_sim_monster_admission_enemy(const qa_app_sim_monster_admission *admission, qa_actor_id actor, qa_actor_id *out);
bool qa_app_sim_monster_admission_old_enemy(const qa_app_sim_monster_admission *admission, qa_actor_id actor, qa_actor_id *out);
void qa_app_sim_monster_admission_set_route(const qa_app_sim_monster_admission *admission, qa_actor_id actor, const qa_actor_id *goal, double pause_until);
#endif
