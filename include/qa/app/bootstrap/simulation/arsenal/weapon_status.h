/* Port of src/app/bootstrap/simulation/arsenal/weapon-status.ts.
 * q1WeaponDisplayName re-export is landed qa_q1_weapon_display_name
 * (caller buffer). Labels needing displayName() synthesis are written
 * into caller buffers. NULL status returns via out flag.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_WEAPON_STATUS_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_WEAPON_STATUS_H
#include "qa/app/bootstrap/simulation/arsenal/selected.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/foundation/weapon_names.h"
#include "qa/content/q2/foundation/weapons/types.h"
/* TS re-export of q1WeaponDisplayName (caller buffer). */
void qa_app_q1_weapon_display_name(qa_q1_weapon weapon, char *out, size_t out_size);
typedef double (*qa_app_item_count_fn)(void *ctx, qa_item_id item);
/* The part of Q1EntityServices read here, passed as callbacks: arsenal/q1.c
 * and shared_simulation_arsenal.c bind them to the game's services. */
typedef struct qa_app_q1_status_host {
  void *ctx;
  qa_item_id (*weapon_item)(void *ctx, qa_q1_weapon weapon);
  qa_item_id (*weapon_ammo)(void *ctx, qa_q1_weapon weapon);
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  bool (*weapon_available)(void *ctx, qa_q1_player_state *player, qa_q1_weapon weapon, const char *purpose);
} qa_app_q1_status_host;
qa_weapon_hud_status qa_app_q1_weapon_status(qa_app_q1_status_host game, qa_q1_player_state *player,
    qa_provider_reference source, char *label, size_t label_size);
/* false return = TS null. */
bool qa_app_q2_weapon_status(const qa_q2w_definition *definition, qa_app_item_count_fn count, void *count_ctx,
    qa_provider_reference source, char *label, size_t label_size, qa_weapon_hud_status *out);
/* product "baseq3"|"missionpack". */
bool qa_app_q3_weapon_status(qa_item_id item, const char *product, qa_app_item_count_fn count, void *count_ctx,
    qa_provider_reference source, char *label, size_t label_size, qa_weapon_hud_status *out);
/* The whole size (with the terminator) of the label each status function writes for the same arguments: a caller
 * passes a label buffer of this size (TS labels have no length bound). */
size_t qa_app_q1_weapon_label_size(qa_q1_weapon weapon);
size_t qa_app_q2_weapon_label_size(const qa_q2w_definition *definition);
size_t qa_app_q3_weapon_label_size(qa_item_id item, const char *product);
qa_arsenal_ammo_warning qa_app_q3_arsenal_warning(const char *product, qa_app_item_count_fn count, void *count_ctx);
#endif
