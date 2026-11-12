/* Port of src/app/bootstrap/simulation/diagnostics.ts (SimulationDiagnostics): a read-only view of the runtime's
 * match-layer state for tests and operator tooling, plus the two mutations tests need to drive it (a player movement
 * change and a recorded attack). It reads the runtime's own state; nothing here is a second store. The TS file is the
 * interface only; SharedSimulation.diagnostics() (runtime.ts) builds it, here qa_app_shared_sim_diagnostics in
 * diagnostics.c. Members returning bool with an out parameter: false = TS null / undefined. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_DIAGNOSTICS_H
#define QA_APP_BOOTSTRAP_SIMULATION_DIAGNOSTICS_H
#include <stdbool.h>
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/source_match.h"

/* lmctf.flag(team) */
typedef struct qa_app_simulation_diagnostics_flag {
  bool has_actor; qa_owned_actor actor; bool has_carrier; qa_actor_id carrier; qa_vec3 home; bool has_dropped_at; double dropped_at;
} qa_app_simulation_diagnostics_flag;
/* lmctf.rune(kind) */
typedef struct qa_app_simulation_diagnostics_rune { bool has_actor; qa_owned_actor actor; bool has_holder; qa_actor_id holder; } qa_app_simulation_diagnostics_rune;
/* source(): which gameplay source family runs, plus the Q1 program and edition when it is a Q1 source. */
typedef struct qa_app_simulation_diagnostics_source { const char *kind; bool has_q1; const char *q1_program; const char *q1_edition; } qa_app_simulation_diagnostics_source;

typedef struct qa_app_simulation_diagnostics {
  void *ctx;
  /* modes (getters) */
  bool (*mode_tag)(void *ctx);
  bool (*mode_lmctf)(void *ctx);
  bool (*mode_ctf)(void *ctx);
  bool (*mode_deathball)(void *ctx);
  /* tag */
  bool (*tag_token)(void *ctx, qa_owned_actor *out);
  bool (*tag_owner)(void *ctx, qa_actor_id *out);
  double (*tag_count)(void *ctx);
  double (*tag_tracked_health)(void *ctx);
  bool (*tag_tracks_health)(void *ctx, qa_actor_id actor);
  /* lmctf: team is 1 | 2 */
  bool (*lmctf_flag)(void *ctx, double team, qa_app_simulation_diagnostics_flag *out);
  bool (*lmctf_rune)(void *ctx, const char *kind, qa_app_simulation_diagnostics_rune *out);
  bool (*lmctf_team)(void *ctx, qa_actor_id actor, double *out);
  /* deathball */
  bool (*deathball_ball)(void *ctx, qa_owned_actor *out);
  bool (*deathball_team)(void *ctx, qa_actor_id actor, double *out);
  bool (*deathball_goal)(void *ctx, double team, qa_vec3 *out);
  /* horde */
  double (*horde_wave)(void *ctx);
  double (*horde_pending_wave)(void *ctx);
  double (*horde_fragged_count)(void *ctx);
  qa_app_simulation_diagnostics_source (*source)(void *ctx);
  /* The Q1 gameplay services in play: the Q1 source itself, or the selected Q1 arsenal inside a foreign world. NULL = TS null. */
  qa_q1_services *(*q1_services)(void *ctx);
  /* Grants a selected arsenal's weapons or ammunition ("weapons" | "ammo") to a player; false when no arsenal applies. */
  bool (*grant_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*select_arsenal_weapon)(void *ctx, qa_actor_id actor, qa_item_id item);
  qa_source_match_player_result (*match_player)(void *ctx, qa_actor_id actor);
  /* Applies a movement change to a player, as the Q2 teleport and noclip commands do. */
  void (*move_player)(void *ctx, qa_actor_id actor, qa_q2_player_movement_change change);
  /* Records the provenance the next death observation of this actor will attribute its kill to. */
  void (*record_attack)(void *ctx, qa_owned_actor actor, qa_attack_provenance attack);
} qa_app_simulation_diagnostics;

/* SharedSimulation.diagnostics(). TS caches the object; the C view is a value of closures over the simulation. */
qa_app_simulation_diagnostics qa_app_shared_sim_diagnostics(qa_app_shared_simulation *sim);
#endif
