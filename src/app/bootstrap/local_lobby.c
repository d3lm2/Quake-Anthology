/* Port of src/app/bootstrap/local-lobby.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/local_lobby.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
/* A Lobby value as TS holds it: an immutable snapshot, independent of the service's storage
 * (which reallocates on create and moves rows on leave). */
static void snapshot_free(qa_lobby *lobby) {
  if (lobby == NULL) return;
  free(lobby->id);
  free(lobby->owner);
  free(lobby->name);
  for (size_t i = 0; i < lobby->member_count; i++) qa_online_account_destroy(&lobby->members[i].account);
  free(lobby->members);
  if (lobby->has_endpoint) qa_net_address_destroy(&lobby->endpoint);
  free(lobby);
}
static qa_lobby *snapshot(const qa_lobby *source) {
  qa_lobby *copy = calloc(1, sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  *copy = *source;
  copy->id = dup_text(source->id);
  copy->owner = dup_text(source->owner);
  copy->name = dup_text(source->name);
  copy->members = calloc(source->member_count ? source->member_count : 1, sizeof *copy->members);
  if (copy->members == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < source->member_count; i++) {
    copy->members[i] = source->members[i];
    copy->members[i].account.id = dup_text(source->members[i].account.id);
    copy->members[i].account.name = dup_text(source->members[i].account.name);
  }
  if (source->has_endpoint && qa_net_address_clone(&source->endpoint, &copy->endpoint) != 0) {
    memset(&copy->endpoint, 0, sizeof copy->endpoint);
    copy->has_endpoint = false;
  }
  return copy;
}
static void set_last(qa_app_application_local_lobby *lobby, const qa_lobby *value) {
  qa_lobby *copy = value != NULL ? snapshot(value) : NULL;
  snapshot_free((qa_lobby *)lobby->last_lobby);
  lobby->last_lobby = copy;
}
void qa_app_application_local_lobby_init(qa_app_application_local_lobby *lobby,
    qa_local_lobby_service *service, const qa_online_account *account,
    qa_app_local_lobby_transitions transitions) {
  memset(lobby, 0, sizeof *lobby);
  lobby->service = service;
  lobby->account = account;
  lobby->transitions = transitions;
}
void qa_app_application_local_lobby_destroy(qa_app_application_local_lobby *lobby) {
  free(lobby->membership);
  lobby->membership = NULL;
  set_last(lobby, NULL);
  free(lobby->error);
  lobby->error = NULL;
}
size_t qa_app_application_local_lobby_list(qa_app_application_local_lobby *lobby, const qa_lobby ***out) {
  size_t count = qa_local_lobby_service_count(lobby->service);
  const qa_lobby **items = malloc((count == 0 ? 1 : count) * sizeof *items);
  if (items == NULL) { *out = NULL; return 0; }
  for (size_t i = 0; i < count; i++) items[i] = qa_local_lobby_service_at(lobby->service, i);
  *out = items;
  return count;
}
const qa_lobby *qa_app_application_local_lobby_current(qa_app_application_local_lobby *lobby) {
  if (lobby->membership == NULL) return NULL;
  size_t count = qa_local_lobby_service_count(lobby->service);
  for (size_t i = 0; i < count; i++) {
    const qa_lobby *entry = qa_local_lobby_service_at(lobby->service, i);
    if (entry != NULL && strcmp(entry->id, lobby->membership) == 0) return entry;
  }
  return NULL;
}
/* TS require(): throws Error. */
static const qa_lobby *require_lobby(qa_app_application_local_lobby *lobby) {
  const qa_lobby *current = qa_app_application_local_lobby_current(lobby);
  bool member = false;
  for (size_t i = 0; current != NULL && i < current->member_count; i++)
    if (strcmp(current->members[i].account.id, lobby->account->id) == 0) member = true;
  if (!member) qa_raise("Error", 0, 0, "No current local lobby membership");
  return current;
}
/* A transition's rejection: a returned message or a raise both reject the operation. */
static void reject(const char *error) {
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
/* TS operation(): a rejection settles the returned promise with its error; here the error's message
 * is returned (owned by the lobby until the next operation) and nothing propagates. */
typedef void (*operation_fn)(qa_app_application_local_lobby *lobby, void *arg);
typedef struct operation_call { qa_app_application_local_lobby *lobby; operation_fn fn; void *arg; } operation_call;
static void operation_run(void *ctx) {
  operation_call *call = ctx;
  call->fn(call->lobby, call->arg);
}
static const char *operation(qa_app_application_local_lobby *lobby, operation_fn fn, void *arg) {
  if (lobby->closed) return "Local lobby session is closed";
  operation_call call = {lobby, fn, arg};
  qa_caught_error caught;
  if (qa_try(operation_run, &call, &caught)) return NULL;
  char *message = dup_text(caught.message);
  free(lobby->error);
  lobby->error = message;
  return lobby->error;
}
static void adopt(qa_app_application_local_lobby *lobby, const qa_lobby *created) {
  char *id = dup_text(created->id);
  free(lobby->membership);
  lobby->membership = id;
  set_last(lobby, created);
  lobby->launched_generation = 0;
  lobby->completed_generation = 0;
}
typedef struct host_args { const char *name; double capacity; qa_app_local_lobby_selection selection; double seats; } host_args;
static void host_op(qa_app_application_local_lobby *lobby, void *arg) {
  host_args *a = arg;
  if (lobby->membership != NULL) qa_raise("Error", 0, 0, "Leave the current lobby before hosting another");
  adopt(lobby, qa_local_lobby_service_create(lobby->service, lobby->account, a->name, a->capacity, a->selection.composition, a->seats));
}
const char *qa_app_application_local_lobby_host(qa_app_application_local_lobby *lobby,
    const char *name, double capacity, qa_app_local_lobby_selection selection, double seats) {
  host_args args = {name, capacity, selection, seats};
  return operation(lobby, host_op, &args);
}
typedef struct join_args { const char *id; double seats; } join_args;
static void join_op(qa_app_application_local_lobby *lobby, void *arg) {
  join_args *a = arg;
  if (lobby->membership != NULL) qa_raise("Error", 0, 0, "Leave the current lobby before joining another");
  adopt(lobby, qa_local_lobby_service_join(lobby->service, a->id, lobby->account, a->seats));
}
const char *qa_app_application_local_lobby_join(qa_app_application_local_lobby *lobby, const char *id, double seats) {
  join_args args = {id, seats};
  return operation(lobby, join_op, &args);
}
static void ready_op(qa_app_application_local_lobby *lobby, void *arg) {
  bool value = *(bool *)arg;
  qa_local_lobby_service_ready(lobby->service, require_lobby(lobby)->id, lobby->account->id, value);
}
const char *qa_app_application_local_lobby_ready(qa_app_application_local_lobby *lobby, bool value) {
  return operation(lobby, ready_op, &value);
}
/* One step of start()'s catch block: try { step } catch (cleanup) { failures.push(cleanup) }. */
typedef struct start_state {
  qa_app_application_local_lobby *lobby;
  qa_lobby *next;
  qa_net_address endpoint;
  qa_wire_selection wire;
} start_state;
static void start_host(void *ctx) {
  start_state *state = ctx;
  reject(state->lobby->transitions.host(state->lobby->transitions.ctx, state->next, &state->endpoint, &state->wire));
}
static void start_publish(void *ctx) {
  start_state *state = ctx;
  qa_app_application_local_lobby *lobby = state->lobby;
  set_last(lobby, qa_local_lobby_service_publish(lobby->service, state->next->id, lobby->account->id,
                                                 state->next->match_generation, &state->endpoint, &state->wire));
  lobby->launched_generation = state->next->match_generation;
}
static void start_complete(void *ctx) {
  start_state *state = ctx;
  qa_app_application_local_lobby *lobby = state->lobby;
  if (qa_app_application_local_lobby_current(lobby) != NULL)
    qa_local_lobby_service_complete(lobby->service, state->next->id, lobby->account->id, state->next->match_generation);
}
static void start_leave(void *ctx) {
  start_state *state = ctx;
  reject(state->lobby->transitions.leave(state->lobby->transitions.ctx, state->next));
}
static void free_snapshot_node(void *arg) { snapshot_free(arg); }
static void start_op(qa_app_application_local_lobby *lobby, void *arg) {
  (void)arg;
  const qa_lobby *current = require_lobby(lobby);
  if (strcmp(current->owner, lobby->account->id) != 0) qa_raise("Error", 0, 0, "Only the local lobby host can start a match");
  start_state state;
  memset(&state, 0, sizeof state);
  state.lobby = lobby;
  state.next = snapshot(qa_local_lobby_service_start(lobby->service, current->id, lobby->account->id));
  qa_cleanup next_node;
  qa_cleanup_push(&next_node, free_snapshot_node, state.next);
  bool bound_host = false;
  qa_caught_error error, cleanup;
  bool ok = qa_try(start_host, &state, &error);
  if (ok) {
    bound_host = true;
    ok = qa_try(start_publish, &state, &error);
  }
  if (!ok) {
    size_t failures = 1;
    if (!qa_try(start_complete, &state, &cleanup)) failures++;
    if (bound_host && !qa_try(start_leave, &state, &cleanup)) failures++;
    if (failures > 1) qa_raise("AggregateError", 0, 0, "Lobby publication and host cleanup failed");
    qa_rethrow(&error);
  }
  qa_cleanup_pop(&next_node);
  snapshot_free(state.next);
}
const char *qa_app_application_local_lobby_start(qa_app_application_local_lobby *lobby) {
  return operation(lobby, start_op, NULL);
}
static void launch(qa_app_application_local_lobby *lobby, const qa_lobby *playing) {
  if (playing->match_generation <= lobby->launched_generation) return;
  double generation = playing->match_generation;
  reject(lobby->transitions.join(lobby->transitions.ctx, playing));
  lobby->launched_generation = generation;
}
static void poll_op(qa_app_application_local_lobby *lobby, void *arg) {
  (void)arg;
  if (lobby->membership == NULL) return;
  const qa_lobby *current = qa_app_application_local_lobby_current(lobby);
  if (current == NULL) {
    qa_lobby *previous = (qa_lobby *)lobby->last_lobby;
    free(lobby->membership);
    lobby->membership = NULL;
    lobby->last_lobby = NULL;
    if (previous == NULL) return;
    qa_cleanup node;
    qa_cleanup_push(&node, free_snapshot_node, previous);
    reject(lobby->transitions.leave(lobby->transitions.ctx, previous));
    qa_cleanup_pop(&node);
    snapshot_free(previous);
    return;
  }
  set_last(lobby, current);
  if (strcmp(current->phase, "playing") == 0) launch(lobby, current);
  else if (strcmp(current->phase, "open") == 0 && lobby->launched_generation > lobby->completed_generation) {
    reject(lobby->transitions.completed(lobby->transitions.ctx, current));
    lobby->completed_generation = lobby->launched_generation;
  }
}
const char *qa_app_application_local_lobby_poll(qa_app_application_local_lobby *lobby) {
  return operation(lobby, poll_op, NULL);
}
static void complete_op(qa_app_application_local_lobby *lobby, void *arg) {
  (void)arg;
  const qa_lobby *current = require_lobby(lobby);
  if (strcmp(current->owner, lobby->account->id) != 0) qa_raise("Error", 0, 0, "Only the local lobby host can complete the match");
  const qa_lobby *completed = qa_local_lobby_service_complete(lobby->service, current->id, lobby->account->id, lobby->launched_generation);
  if (lobby->launched_generation > lobby->completed_generation) {
    reject(lobby->transitions.completed(lobby->transitions.ctx, completed));
    lobby->completed_generation = lobby->launched_generation;
  }
}
const char *qa_app_application_local_lobby_complete(qa_app_application_local_lobby *lobby) {
  return operation(lobby, complete_op, NULL);
}
/* TS leaveCurrent(). */
static void leave_current(qa_app_application_local_lobby *lobby) {
  const qa_lobby *current = qa_app_application_local_lobby_current(lobby);
  qa_lobby *leaving = current != NULL ? snapshot(current) : (qa_lobby *)lobby->last_lobby;
  if (current != NULL) set_last(lobby, NULL);
  free(lobby->membership);
  lobby->membership = NULL;
  lobby->last_lobby = NULL;
  if (leaving == NULL) return;
  qa_cleanup node;
  qa_cleanup_push(&node, free_snapshot_node, leaving);
  size_t count = qa_local_lobby_service_count(lobby->service);
  bool listed = false;
  for (size_t i = 0; i < count; i++) {
    const qa_lobby *entry = qa_local_lobby_service_at(lobby->service, i);
    if (entry != NULL && strcmp(entry->id, leaving->id) == 0) listed = true;
  }
  if (listed) qa_local_lobby_service_leave(lobby->service, leaving->id, lobby->account->id);
  reject(lobby->transitions.leave(lobby->transitions.ctx, leaving));
  qa_cleanup_pop(&node);
  snapshot_free(leaving);
}
static void leave_op(qa_app_application_local_lobby *lobby, void *arg) {
  (void)arg;
  leave_current(lobby);
}
const char *qa_app_application_local_lobby_leave(qa_app_application_local_lobby *lobby) {
  return operation(lobby, leave_op, NULL);
}
const char *qa_app_application_local_lobby_close(qa_app_application_local_lobby *lobby) {
  if (lobby->closed) return NULL;
  /* TS close(): closed first, then `await this.leaveCurrent()` (its rejection is close()'s). */
  operation_call call = {lobby, leave_op, NULL};
  lobby->closed = true;
  qa_caught_error caught;
  if (qa_try(operation_run, &call, &caught)) return NULL;
  char *message = dup_text(caught.message);
  free(lobby->error);
  lobby->error = message;
  return lobby->error;
}
