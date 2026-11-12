/* Port of src/app/bootstrap/simulation/foreign-match-rules.ts (ForeignMatchRules): the runtime match rules for worlds
 * whose own source does not run the selected match provider (Tag, LMCTF and flag CTF, DeathBall, Q3 Horde). Each rule
 * is a strategy with one contract: it ticks once per settled frame, releases per-actor state when the actor is
 * released, and says whether it blocks saved games and same-map source restart. The rules' state lives on the
 * simulation (the TS ForeignMatchHost), so the strategy list is the fixed order horde, tag, flags, deathball.
 * Moved out of shared_simulation_{step,core,players,worlds}.c as TS moved it out of runtime.ts. GPL-2.0-or-later. */
#include "foreign_match_host.h"

void qa_app_foreign_match_rules_tick(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_tick_horde_wave(sim);
  qa_app_shared_sim_tick_tag(sim);
  qa_app_shared_sim_tick_lmctf(sim);
  qa_app_shared_sim_tick_deathball(sim);
}
void qa_app_foreign_match_rules_release(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_foreign_horde_release(sim, actor);
  qa_app_foreign_tag_release(sim, actor);
  qa_app_foreign_flags_release(sim, actor);
  qa_app_foreign_deathball_release(sim, actor);
}
/* True when a running mode holds state a saved game or source restart cannot carry. */
bool qa_app_foreign_match_rules_blocks_checkpoint(qa_app_shared_simulation *sim) {
  return qa_app_foreign_horde_blocks_checkpoint(sim) || qa_app_foreign_tag_blocks_checkpoint(sim)
    || qa_app_foreign_flags_blocks_checkpoint(sim) || qa_app_foreign_deathball_blocks_checkpoint(sim);
}
void qa_app_foreign_match_rules_register_combat(qa_app_shared_simulation *sim) {
  qa_app_foreign_tag_register_combat(sim);
  qa_app_foreign_flags_register_combat(sim);
  qa_app_foreign_deathball_register_combat(sim);
}
