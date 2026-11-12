/* Port of src/app/bootstrap/simulation/network-q2-guest-core.ts: the pieces of the native Q2 guest server host that do
 * not depend on the game edition (API 3 or API 2023): the player table, visibility and message-audience rules, slot
 * allocation and the lifecycle calls common to both guest editions.
 * C accommodations:
 *  - TS Q2GuestWorldSurface is the member table both C guest worlds already share (qa_app_sim_q2_guest_world,
 *    network_q2_guest.h); cvars, scene, geometry and maxClients are read from it and from the binding options.
 *  - `shared` is a set of functions over the core; each edition's host forwards its vtable entries to them.
 *  - `forgotten` (C only) tells the edition's host that a player left the table, so it can release the per-player
 *    storage TypeScript leaves to the garbage collector.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_GUEST_CORE_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_GUEST_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/network_q2_guest.h"
#include "qa/core/commands/index.h"
#include "qa/network/q2/connectionless.h"

typedef struct qa_app_sim_q2_guest_host_core qa_app_sim_q2_guest_host_core;
/* The discovery value the Q2 server network reads from host.discovery. */
typedef struct qa_app_sim_q2_guest_discovery {
  void *ctx;
  qa_q2_status (*status)(void *ctx);
  qa_q2_host_info (*info)(void *ctx);
} qa_app_sim_q2_guest_discovery;
/* TS Q2GuestHostCoreOptions. The world is borrowed; map is copied. */
typedef struct qa_app_sim_q2_guest_host_core_options {
  qa_app_sim_q2_server_binding_options options;
  const qa_app_sim_q2_guest_world *world;
  const char *map;
  qa_command_dialect dialect; /* QA_COMMAND_DIALECT_Q2_CLASSIC | QA_COMMAND_DIALECT_Q2_RERELEASE */
  void *forgotten_ctx;
  void (*forgotten)(void *ctx, double client_slot);
} qa_app_sim_q2_guest_host_core_options;

/* TS createQ2GuestHostCore. */
qa_app_sim_q2_guest_host_core *qa_app_sim_create_q2_guest_host_core(qa_app_sim_q2_guest_host_core_options options);
void qa_app_sim_q2_guest_host_core_destroy(qa_app_sim_q2_guest_host_core *core);
const char *qa_app_sim_q2_guest_host_core_map(const qa_app_sim_q2_guest_host_core *core);

/* players.set(player.client.slot, player) */
void qa_app_sim_q2_guest_host_core_set_player(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player);
/* requirePlayer(player): raises "Q2 guest network player is retired". */
void qa_app_sim_q2_guest_host_core_require_player(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player);
/* headnodeVisible(headnode, clusters, kind): kind is "pvs" | "phs". */
bool qa_app_sim_q2_guest_host_core_headnode_visible(qa_app_sim_q2_guest_host_core *core, double headnode, const double *clusters,
                                                    size_t cluster_count, const char *kind);
/* audienceIncludes(player, audience): whether a guest message reaches this player: unicast by slot, multicast by
 * area connection and the PVS/PHS rule. The audience is the message's (unicast/slot, origin/scope). */
bool qa_app_sim_q2_guest_host_core_audience_includes(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player,
                                                     const qa_app_sim_q2_guest_message *audience);
/* freeSlot(): false is the TS null (the server is full). */
bool qa_app_sim_q2_guest_host_core_free_slot(qa_app_sim_q2_guest_host_core *core, double *out_slot);
/* registerServerVariables(protocol) */
void qa_app_sim_q2_guest_host_core_register_server_variables(qa_app_sim_q2_guest_host_core *core, const char *protocol);
/* disconnect(player, extra?): runs every cleanup step and reports all failures together; `extra` (NULL for undefined)
 * is an edition-specific step. */
void qa_app_sim_q2_guest_host_core_disconnect(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player,
                                              void (*extra)(void *ctx), void *extra_ctx);

/* shared: supportsSourceWire, discovery, carriedPlayer, begin, userinfo, command, commandText. */
const void *qa_app_sim_q2_guest_host_core_supports_source_wire(qa_app_sim_q2_guest_host_core *core);
const qa_app_sim_q2_guest_discovery *qa_app_sim_q2_guest_host_core_discovery(qa_app_sim_q2_guest_host_core *core);
qa_app_q2_player qa_app_sim_q2_guest_host_core_carried_player(qa_app_sim_q2_guest_host_core *core, qa_client_id client);
void qa_app_sim_q2_guest_host_core_begin(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player);
void qa_app_sim_q2_guest_host_core_userinfo(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player, const char *value);
void qa_app_sim_q2_guest_host_core_command(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player, const char *name,
                                           const char *const *args, size_t count);
void qa_app_sim_q2_guest_host_core_command_text(qa_app_sim_q2_guest_host_core *core, qa_app_q2_player player, const char *text);
#endif
