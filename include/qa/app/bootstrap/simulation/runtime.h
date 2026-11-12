/* Port of src/app/bootstrap/simulation/runtime.ts: the ordered actor turns
 * the SharedSimulation's step, world and arsenal stages iterate. The
 * SharedSimulation class itself is qa_app_shared_simulation
 * (shared_simulation.h, shared_simulation_*.c).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/world/actors/registry.h"
/* TS orderedActorTurns (shared_simulation_turns.c). */
typedef struct qa_app_sim_ordered_actor_turns qa_app_sim_ordered_actor_turns;
qa_app_sim_ordered_actor_turns *qa_app_sim_ordered_actor_turns_create(qa_session_actor_registry *actors,
    void (*position_of)(void *ctx, qa_owned_actor actor, double out[2]), void *position_ctx);
void qa_app_sim_ordered_actor_turns_destroy(qa_app_sim_ordered_actor_turns *turns);
/* Caller-owned visited set (actor ids compared with qa_same_actor on
 * owned identity: id + owner). */
typedef struct qa_app_sim_actor_visited { qa_owned_actor *actors; size_t count, capacity; } qa_app_sim_actor_visited;
bool qa_app_sim_ordered_actor_turns_next(qa_app_sim_ordered_actor_turns *turns, qa_app_sim_actor_visited *visited, qa_owned_actor *out);
#endif
