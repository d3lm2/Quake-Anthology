/* Port of src/app/bootstrap/simulation/network-q2-guest-core.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q2_guest_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (!out) qa_range_error("Out of memory");
  memcpy(out, text, length + 1);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

typedef struct core_player { bool used; qa_app_q2_player player; } core_player;
struct qa_app_sim_q2_guest_host_core {
  qa_app_sim_q2_guest_host_core_options core;
  char *map;
  core_player *players; size_t player_count; /* Map<client slot, Q2ApplicationPlayer> */
  qa_app_sim_q2_guest_discovery discovery;
  qa_wire_admission support;
  qa_q2_status_player *status_players; size_t status_player_count; char **status_names;
  char *server_info, *host_name;
};

static core_player *player_slot(qa_app_sim_q2_guest_host_core *c, double slot) {
  if (slot < 0) qa_range_error("Invalid Q2 client slot");
  if ((size_t)slot >= c->player_count) {
    size_t count = (size_t)slot + 1;
    core_player *grown = realloc(c->players, count * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    memset(grown + c->player_count, 0, (count - c->player_count) * sizeof *grown);
    c->players = grown; c->player_count = count;
  }
  return &c->players[(size_t)slot];
}
void qa_app_sim_q2_guest_host_core_set_player(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player) {
  core_player *entry = player_slot(c, (double)player.client.slot);
  entry->used = true; entry->player = player;
}
void qa_app_sim_q2_guest_host_core_require_player(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player) {
  core_player *current = player.client.slot >= 0 && (size_t)player.client.slot < c->player_count ? &c->players[player.client.slot] : NULL;
  if (current == NULL || !current->used || !qa_same_client(current->player.client, player.client)
      || !qa_same_actor(current->player.actor, player.actor))
    qa_raise("Error", 0, 0, "Q2 guest network player is retired");
}
static void free_indirect(void *arg) { free(*(void **)arg); }
static const qa_bsp_node *world_nodes(const qa_decoded_world *world, size_t *out_count) {
  switch (world->kind) {
    case QA_DECODED_WORLD_Q1: *out_count = world->as.q1->node_count; return world->as.q1->nodes;
    case QA_DECODED_WORLD_Q2: *out_count = world->as.q2->node_count; return world->as.q2->nodes;
    case QA_DECODED_WORLD_Q3: *out_count = world->as.q3->node_count; return world->as.q3->nodes;
  }
  *out_count = 0;
  return NULL;
}
bool qa_app_sim_q2_guest_host_core_headnode_visible(qa_app_sim_q2_guest_host_core *c, double headnode, const double *clusters,
    size_t cluster_count, const char *kind) {
  qa_shared_scene_queries *scene = c->core.options.simulation->scene;
  size_t node_count = 0;
  const qa_bsp_node *nodes = world_nodes(&c->core.options.content->world, &node_count);
  qa_bsp_child *pending = xcalloc(1, sizeof *pending);
  size_t count = 1, capacity = 1;
  /* A scene query that refuses a guest leaf releases the pending list (it moves when it grows). */
  qa_cleanup pending_node; qa_cleanup_push(&pending_node, free_indirect, &pending);
  pending[0].is_node = !(headnode < 0);
  pending[0].index = headnode < 0 ? -1 - headnode : headnode;
  bool visible = false;
  while (count != 0 && !visible) {
    qa_bsp_child child = pending[--count];
    if (!child.is_node) {
      /* clusters.some(cluster => scene.clusterVisible(cluster, scene.leafCluster(child.index), kind)): the leaf is
       * looked up per cluster, so an empty cluster list never reads it. */
      for (size_t i = 0; i < cluster_count && !visible; i++)
        if (qa_shared_cluster_visible(scene, clusters[i], qa_shared_leaf_cluster(scene, child.index), kind)) visible = true;
      continue;
    }
    if (child.index < 0 || child.index != trunc(child.index) || child.index >= (double)node_count) {
      qa_raise("Error", 0, 0, "Guest visibility headnode is outside the shared BSP");
    }
    const qa_bsp_node *node = &nodes[(size_t)child.index];
    if (count + 2 > capacity) {
      capacity = (count + 2) * 2;
      qa_bsp_child *grown = realloc(pending, capacity * sizeof *grown);
      if (!grown) qa_range_error("Out of memory");
      pending = grown;
    }
    pending[count++] = node->children[0];
    pending[count++] = node->children[1];
  }
  qa_cleanup_pop(&pending_node);
  free(pending);
  return visible;
}
bool qa_app_sim_q2_guest_host_core_audience_includes(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player,
    const qa_app_sim_q2_guest_message *audience) {
  qa_app_sim_q2_guest_host_core_require_player(c, player);
  if (audience->unicast) return audience->slot == player.source_entity;
  if (is(audience->scope, "all")) return true;
  qa_shared_scene_queries *scene = c->core.options.simulation->scene;
  const qa_app_sim_q2_guest_world *w = c->core.world;
  qa_q2_entity_state entity = w->entity_state(w->ctx, player.source_entity);
  double from = qa_shared_point_leaf(scene, audience->origin), to = qa_shared_point_leaf(scene, entity.origin);
  return qa_shared_areas_connected(scene, qa_shared_leaf_area(scene, from), qa_shared_leaf_area(scene, to))
      && qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, from), qa_shared_leaf_cluster(scene, to), audience->scope);
}
bool qa_app_sim_q2_guest_host_core_free_slot(qa_app_sim_q2_guest_host_core *c, double *out_slot) {
  const qa_app_sim_q2_guest_world *w = c->core.world;
  size_t client_count = 0;
  const qa_app_sim_q2_guest_client *clients = w->clients(w->ctx, &client_count);
  double slot = 0;
  for (;;) {
    bool occupied = false;
    for (size_t i = 0; i < client_count && !occupied; i++) occupied = clients[i].slot - 1 == slot;
    if (!(slot < w->max_clients && occupied)) break;
    slot++;
  }
  if (slot == w->max_clients) return false;
  *out_slot = slot;
  return true;
}
void qa_app_sim_q2_guest_host_core_register_server_variables(qa_app_sim_q2_guest_host_core *c, const char *protocol) {
  qa_cvar_registry *cvars = c->core.world->cvars;
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(cvars, "hostname", "noname", QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_ARCHIVE, &snapshot); qa_cvar_read_free(&snapshot);
  const char *names[2] = {"protocol", "mapname"}, *values[2] = {protocol, c->map};
  for (int i = 0; i < 2; i++) {
    qa_cvar_registry_register(cvars, names[i], values[i], QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_NO_SET, &snapshot); qa_cvar_read_free(&snapshot);
    qa_cvar_registry_set(cvars, names[i], values[i], true, &snapshot); qa_cvar_read_free(&snapshot);
  }
}

typedef struct command_scope { qa_app_sim_q2_guest_host_core *c; qa_app_q2_player player; const char *text; } command_scope;
static void command_body(void *ctx) {
  command_scope *s = ctx;
  qa_app_sim_q2_guest_host_core_require_player(s->c, s->player);
  qa_command_tokens tokens;
  if (!qa_tokenize_command(s->text, s->c->core.dialect, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_command_tokens_destroy, &tokens);
  const qa_app_sim_q2_guest_world *w = s->c->core.world;
  if (tokens.argc != 0) w->command(w->ctx, s->player.source_entity, (const char *const *)tokens.argv, tokens.argc, tokens.args_text);
  qa_cleanup_pop(&guard);
  qa_command_tokens_destroy(&tokens);
}
void qa_app_sim_q2_guest_host_core_command_text(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player, const char *text) {
  command_scope scope = {c, player, text};
  qa_app_q2_game_callback(command_body, &scope);
}
/* command(player, name, args) = command(player, [name, ...args].join(" ")) */
void qa_app_sim_q2_guest_host_core_command(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player, const char *name,
    const char *const *args, size_t count) {
  size_t length = strlen(name) + 1;
  for (size_t i = 0; i < count; i++) length += strlen(args[i]) + 1;
  char *text = xcalloc(length, 1);
  strcpy(text, name);
  for (size_t i = 0; i < count; i++) { strcat(text, " "); strcat(text, args[i]); }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, text);
  qa_app_sim_q2_guest_host_core_command_text(c, player, text);
  qa_cleanup_pop(&guard);
  free(text);
}

typedef struct player_scope {
  qa_app_sim_q2_guest_host_core *c; qa_app_q2_player player; const char *value;
  void (*extra)(void *ctx); void *extra_ctx;
} player_scope;
static void retire_body(void *ctx) {
  player_scope *p = ctx;
  const qa_app_sim_q2_guest_world *w = p->c->core.world;
  const qa_app_sim_network_simulation *s = p->c->core.options.simulation;
  if (!w->is_retired(w->ctx)) s->disconnect_player(s->ctx, p->player.actor);
}
static void extra_body(void *ctx) { player_scope *p = ctx; p->extra(p->extra_ctx); }
static void forget_body(void *ctx) {
  player_scope *p = ctx;
  core_player *entry = player_slot(p->c, (double)p->player.client.slot);
  entry->used = false;
  if (p->c->core.forgotten) p->c->core.forgotten(p->c->core.forgotten_ctx, (double)p->player.client.slot);
}
static void close_body(void *ctx) { player_scope *p = ctx; qa_engine_session_close_client(p->c->core.options.session, p->player.client); }
static void disconnect_body(void *ctx) {
  player_scope *p = ctx;
  qa_app_sim_q2_guest_host_core_require_player(p->c, p->player);
  size_t failures = 0;
  qa_caught_error error;
  if (!qa_try(retire_body, p, &error)) failures++;
  if (p->extra != NULL && !qa_try(extra_body, p, &error)) failures++;
  if (!qa_try(forget_body, p, &error)) failures++;
  if (!qa_try(close_body, p, &error)) failures++;
  if (failures != 0) qa_raise("AggregateError", 0, 0, "Q2 guest client cleanup failed");
}
void qa_app_sim_q2_guest_host_core_disconnect(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player, void (*extra)(void *ctx),
    void *extra_ctx) {
  player_scope scope = {c, player, NULL, extra, extra_ctx};
  qa_app_q2_game_callback(disconnect_body, &scope);
}

const void *qa_app_sim_q2_guest_host_core_supports_source_wire(qa_app_sim_q2_guest_host_core *c) {
  const qa_app_sim_network_simulation *s = c->core.options.simulation;
  qa_wire_admission_destroy(&c->support);
  const char *reasons[3]; size_t count = 0;
  if (c->core.options.content->world.kind != QA_DECODED_WORLD_Q2)
    reasons[count++] = "Original Quake II peers require Quake II BSP geometry; use local or unified presentation for foreign maps";
  if (strncmp(s->recipe->movement.provider, "q2:", 3) != 0) reasons[count++] = "Guest native wire requires Q2 movement";
  if (strncmp(s->recipe->character.definition.provider, "q2:", 3) != 0) reasons[count++] = "Guest native wire requires Q2 character state";
  c->support.supported = count == 0;
  c->support.reasons = count ? xcalloc(count, sizeof *c->support.reasons) : NULL;
  for (size_t i = 0; i < count; i++) c->support.reasons[i] = xstrdup(reasons[i]);
  c->support.reason_count = count;
  return &c->support;
}
static qa_q2_status discovery_status(void *ctx) {
  qa_app_sim_q2_guest_host_core *c = ctx;
  const qa_app_sim_q2_guest_world *w = c->core.world;
  for (size_t i = 0; i < c->status_player_count; i++) free(c->status_names[i]);
  free(c->status_names); free(c->status_players); free(c->server_info);
  c->status_names = NULL; c->status_players = NULL; c->status_player_count = 0;
  c->server_info = qa_cvar_registry_info_string(w->cvars, QA_Q2_CVAR_FLAG_SERVER_INFO, -1);
  size_t count = 0;
  const qa_app_sim_q2_guest_client *clients = w->clients(w->ctx, &count);
  c->status_players = count ? xcalloc(count, sizeof *c->status_players) : NULL;
  c->status_names = count ? xcalloc(count, sizeof *c->status_names) : NULL;
  c->status_player_count = count;
  for (size_t i = 0; i < count; i++) {
    /* world.playerState(client.slot).stats[14] ?? 0: the world fills the state of its own edition. */
    qa_q2_player_state classic; qa_q2_rerelease_player_state rerelease;
    memset(&classic, 0, sizeof classic); memset(&rerelease, 0, sizeof rerelease);
    bool is_classic = c->core.dialect == QA_COMMAND_DIALECT_Q2_CLASSIC;
    w->player_state(w->ctx, clients[i].slot, is_classic ? &classic : NULL, is_classic ? NULL : &rerelease);
    double score = is_classic ? (classic.view.stats.count > 14 ? classic.view.stats.values[14] : 0)
        : (rerelease.view.stats.count > 14 ? rerelease.view.stats.values[14] : 0);
    double ping = w->player_ping(w->ctx, clients[i].slot);
    qa_app_sim_info info; qa_app_sim_info_parse_q2(&info, clients[i].userinfo);
    const char *name = qa_app_sim_info_get(&info, "name");
    c->status_names[i] = xstrdup(name ? name : "");
    qa_app_sim_info_clear(&info);
    c->status_players[i].score = score; c->status_players[i].ping = ping; c->status_players[i].name = c->status_names[i];
  }
  qa_q2_status status = {c->server_info, c->status_players, count};
  return status;
}
static qa_q2_host_info discovery_info(void *ctx) {
  qa_app_sim_q2_guest_host_core *c = ctx;
  const qa_app_sim_q2_guest_world *w = c->core.world;
  free(c->host_name);
  c->host_name = qa_cvar_registry_variable_string(w->cvars, "hostname");
  size_t count = 0;
  w->clients(w->ctx, &count);
  qa_q2_host_info info = {c->host_name, c->map, (double)count, w->max_clients};
  return info;
}
const qa_app_sim_q2_guest_discovery *qa_app_sim_q2_guest_host_core_discovery(qa_app_sim_q2_guest_host_core *c) { return &c->discovery; }
qa_app_q2_player qa_app_sim_q2_guest_host_core_carried_player(qa_app_sim_q2_guest_host_core *c, qa_client_id client) {
  const qa_app_sim_network_simulation *s = c->core.options.simulation;
  const qa_client_id *clients = NULL; const qa_actor_id *actors = NULL;
  size_t count = s->q2_native_players(s->ctx, &clients, &actors);
  size_t found = count;
  for (size_t i = 0; i < count && found == count; i++) if (qa_same_client(clients[i], client)) found = i;
  if (found == count) qa_raise("Error", 0, 0, "Q2 guest carried client is not owned by this world");
  qa_app_q2_player player = {client, actors[found], (double)client.slot + 1};
  qa_app_sim_q2_guest_host_core_set_player(c, player);
  return player;
}
static void begin_body(void *ctx) {
  player_scope *p = ctx;
  qa_app_sim_q2_guest_host_core_require_player(p->c, p->player);
  const qa_app_sim_q2_guest_world *w = p->c->core.world;
  w->begin(w->ctx, p->player.source_entity);
  const qa_app_sim_network_simulation *s = p->c->core.options.simulation;
  s->notify_client_event(s->ctx, "admitted", p->player.actor);
}
void qa_app_sim_q2_guest_host_core_begin(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player) {
  player_scope scope = {c, player, NULL, NULL, NULL};
  qa_app_q2_game_callback(begin_body, &scope);
}
static void userinfo_body(void *ctx) {
  player_scope *p = ctx;
  qa_app_sim_q2_guest_host_core_require_player(p->c, p->player);
  const qa_app_sim_q2_guest_world *w = p->c->core.world;
  w->userinfo(w->ctx, p->player.source_entity, p->value);
  const qa_app_sim_network_simulation *s = p->c->core.options.simulation;
  s->notify_client_event(s->ctx, "userinfo", p->player.actor);
}
void qa_app_sim_q2_guest_host_core_userinfo(qa_app_sim_q2_guest_host_core *c, qa_app_q2_player player, const char *value) {
  player_scope scope = {c, player, value, NULL, NULL};
  qa_app_q2_game_callback(userinfo_body, &scope);
}

qa_app_sim_q2_guest_host_core *qa_app_sim_create_q2_guest_host_core(qa_app_sim_q2_guest_host_core_options options) {
  qa_app_sim_q2_guest_host_core *c = xcalloc(1, sizeof *c);
  c->core = options;
  c->map = xstrdup(options.map);
  c->core.map = c->map;
  c->discovery.ctx = c; c->discovery.status = discovery_status; c->discovery.info = discovery_info;
  return c;
}
const char *qa_app_sim_q2_guest_host_core_map(const qa_app_sim_q2_guest_host_core *c) { return c->map; }
void qa_app_sim_q2_guest_host_core_destroy(qa_app_sim_q2_guest_host_core *c) {
  if (c == NULL) return;
  free(c->players);
  for (size_t i = 0; i < c->status_player_count; i++) free(c->status_names[i]);
  free(c->status_names); free(c->status_players); free(c->server_info); free(c->host_name);
  qa_wire_admission_destroy(&c->support);
  free(c->map);
  free(c);
}
