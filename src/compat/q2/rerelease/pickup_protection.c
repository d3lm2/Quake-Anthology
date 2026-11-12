/* Port of src/compat/q2/rerelease/pickup-protection.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/pickup_protection.h"
#include <string.h>
#include "qa/compat/q2/native_throw.h"
#include "qa/compat/q2/rerelease/source_state.h"

typedef void (*consume_fn_t)(void *consume_ctx, void *execute_ctx, void (*execute)(void *));
typedef struct {
  qa_compat_q2_rerelease_q2_guest_host *host;
  qa_raw_entity_view recipient;
  qa_protection_channel channel;
  void *ctx;
  void (*current)(void *ctx);
  void *(*operation)(void *ctx, void *consume_ctx, consume_fn_t consume);
  bool (*committed)(void *ctx);
  qa_owned_actor actor;
} state_t;
static bool is_committed(state_t *s) { return !s->committed || s->committed(s->ctx); }
static void primary_current(state_t *s) {
  s->current(s->ctx);
  qa_q2_foundation_host *engine = qa_compat_q2_rerelease_q2_guest_host_get_options(s->host)->engine;
  qa_provider_id owner = NULL;
  if (qa_gameplay_authority_protection_owner(engine->combat, s->actor, s->channel, &owner))
    qa_raise("Error", 0, 0, "Native primary protection ownership changed during its operation");
}
static void publish(void *v) {
  state_t *s = v;
  primary_current(s);
  if (!is_committed(s)) return;
  qa_compat_q2_rerelease_foreign_actors *foreign = qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(s->host);
  if (foreign && qa_compat_q2_rerelease_foreign_actors_observing_damage(foreign, s->actor.id)) return;
  qa_q2_foundation_host *engine = qa_compat_q2_rerelease_q2_guest_host_get_options(s->host)->engine;
  qa_combat_state_result state = qa_gameplay_authority_read(engine->combat, s->actor.id);
  if (!state.has_value) qa_raise("Error", 0, 0, "Native protection operation lost its combat binding");
  if (s->channel == QA_PROTECTION_POWERED)
    qa_gameplay_authority_set_powered_protection(engine->combat, s->actor, state.value.armor.powered);
  else
    qa_gameplay_authority_set_regular_armor(engine->combat, s->actor, state.value.armor.regular);
}
/* Owned branch: consume runs current() and does not run execute. */
static void consume_current(void *v, void *execute_ctx, void (*execute)(void *)) {
  state_t *s = v; (void)execute_ctx; (void)execute;
  s->current(s->ctx);
}
static void consume_primary(void *v, void *execute_ctx, void (*execute)(void *)) {
  state_t *s = v;
  primary_current(s); execute(execute_ctx); primary_current(s);
}
typedef struct { state_t *s; void *(*run)(state_t *s); } observe_ctx_t;
static bool observe_committed(void *v) { return is_committed(((observe_ctx_t *)v)->s); }
static void *observe_run(void *v) { observe_ctx_t *c = v; return c->run(c->s); }
static void *observe(state_t *s, void *(*run)(state_t *)) {
  qa_compat_q2_rerelease_foreign_actors *foreign = qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(s->host);
  if (!foreign) return run(s);
  observe_ctx_t ctx = {s, run};
  return qa_compat_q2_rerelease_foreign_actors_with_inventory_publication(foreign, s->actor.id, &ctx, observe_committed, observe_run);
}
static void *run_owned(state_t *s) { return s->operation(s->ctx, s, consume_current); }
static void *run_primary(state_t *s) {
  void *result = s->operation(s->ctx, s, consume_primary);
  primary_current(s);
  return result;
}
/* The non-owned branch: the write observers, and the scratch objects their addresses came from. */
typedef struct {
  state_t *s; qa_compat_q2_memory *memory;
  qa_compat_q2_rerelease_source_edict *source; qa_compat_q2_rerelease_source_client *client;
  uint64_t handles[2]; size_t handle_count; void *result;
} primary_t;
static void primary_body(void *v) {
  primary_t *p = v; state_t *s = p->s; qa_compat_q2_memory *memory = p->memory;
  qa_compat_q2_rerelease_guest_module *module = qa_compat_q2_rerelease_q2_guest_host_get_module(s->host);
  if (memory->observe_writes) {
    uint64_t handle = memory->observe_writes(memory->ctx, qa_compat_q2_rerelease_source_edict_at(p->source, "flags"), 8, s, publish);
    p->handles[p->handle_count++] = handle;
  }
  qa_guest_address client_address;
  if (qa_compat_q2_rerelease_source_edict_client(p->source, &client_address)) {
    const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
    p->client = qa_compat_q2_rerelease_source_client_create(client_address, module, &profile->client);
    if (!p->client) qa_range_error("Q2 rerelease pickup protection out of memory");
    if (memory->observe_writes) {
      uint64_t handle = memory->observe_writes(memory->ctx, qa_compat_q2_rerelease_source_client_at(p->client, "pers.inventory"),
                                               profile->client.inventory_count * 4, s, publish);
      p->handles[p->handle_count++] = handle;
    }
  }
  p->result = observe(s, run_primary);
}
/* finally { for (const close of remove.reverse()) close(); }: the observers point at this call's state, so they
 * leave on every path (TS leaves the first observer bound when the second fails to bind; its closure stays valid,
 * a C stack frame does not). */
static void primary_cleanup(void *v) {
  primary_t *p = v; qa_compat_q2_memory *memory = p->memory;
  size_t count = p->handle_count; p->handle_count = 0;
  if (memory->unobserve_writes) for (size_t i = count; i > 0; i--) memory->unobserve_writes(memory->ctx, p->handles[i - 1]);
  if (p->client) { qa_compat_q2_rerelease_source_client_destroy(p->client); p->client = NULL; }
  if (p->source) { qa_compat_q2_rerelease_source_edict_destroy(p->source); p->source = NULL; }
}
void *qa_compat_q2_with_rerelease_primary_protection(
    qa_compat_q2_rerelease_q2_guest_host *host, qa_raw_entity_view recipient, qa_protection_channel channel,
    void *ctx, void (*current)(void *ctx),
    void *(*operation)(void *ctx, void *consume_ctx, void (*consume)(void *consume_ctx, void *execute_ctx, void (*execute)(void *))),
    bool (*committed)(void *ctx)) {
  state_t s;
  s.host = host; s.recipient = recipient; s.channel = channel; s.ctx = ctx;
  s.current = current; s.operation = operation; s.committed = committed;
  current(ctx);
  const qa_compat_q2_rerelease_q2_host_options *options = qa_compat_q2_rerelease_q2_guest_host_get_options(host);
  qa_q2_foundation_host *engine = options->engine;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(host, recipient, &s.actor))
    qa_raise("Error", 0, 0, "Native protection operation has no live recipient");
  qa_provider_id owner = NULL;
  if (qa_gameplay_authority_protection_owner(engine->combat, s.actor, channel, &owner))
    return observe(&s, run_owned);
  qa_compat_q2_rerelease_guest_module *module = qa_compat_q2_rerelease_q2_guest_host_get_module(host);
  primary_t p; memset(&p, 0, sizeof p);
  p.s = &s; p.memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  p.source = qa_compat_q2_rerelease_source_edict_create(recipient, module);
  if (!p.source) qa_range_error("Q2 rerelease pickup protection out of memory");
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(primary_body, &p, &thrown);
  primary_cleanup(&p);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return p.result;
}
