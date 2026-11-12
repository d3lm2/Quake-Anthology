/* Port of src/app/bootstrap/simulation/runtime.ts orderedActorTurns (the module generator the
 * SharedSimulation's frame loops iterate). Declared in runtime.h. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/runtime.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

typedef struct pending_entry { qa_owned_actor actor; double position[2]; } pending_entry;
struct qa_app_sim_ordered_actor_turns {
  qa_session_actor_registry *actors;
  void (*position_of)(void *ctx, qa_owned_actor actor, double out[2]); void *position_ctx;
  int64_t revision;
  bool has_cursor; double cursor[2];
  pending_entry *pending; size_t pending_count, index;
};
/* visited: Set<OwnedActor>; the registry hands out one OwnedActor per id, owner and token. */
static bool in_visited(const qa_app_sim_actor_visited *v, qa_owned_actor actor) {
  for (size_t i = 0; i < v->count; i++)
    if (qa_same_actor(v->actors[i].id, actor.id) && v->actors[i].owner != NULL && actor.owner != NULL
        && strcmp(v->actors[i].owner, actor.owner) == 0 && v->actors[i].token == actor.token) return true;
  return false;
}
static void add_visited(qa_app_sim_actor_visited *v, qa_owned_actor actor) {
  if (v->count >= v->capacity) {
    size_t capacity = v->capacity ? v->capacity * 2 : 16;
    qa_owned_actor *grown = realloc(v->actors, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    v->actors = grown; v->capacity = capacity;
  }
  v->actors[v->count++] = actor;
}
/* left.position[0] - right.position[0] || left.position[1] - right.position[1]; positions are integral. */
static int pending_cmp(const void *a, const void *b) {
  const pending_entry *l = a, *r = b;
  if (l->position[0] != r->position[0]) return l->position[0] < r->position[0] ? -1 : 1;
  if (l->position[1] != r->position[1]) return l->position[1] < r->position[1] ? -1 : 1;
  return 0;
}
qa_app_sim_ordered_actor_turns *qa_app_sim_ordered_actor_turns_create(qa_session_actor_registry *actors,
    void (*position_of)(void *ctx, qa_owned_actor actor, double out[2]), void *position_ctx) {
  qa_app_sim_ordered_actor_turns *t = calloc(1, sizeof *t);
  if (!t) qa_range_error("Out of memory");
  t->actors = actors; t->position_of = position_of; t->position_ctx = position_ctx; t->revision = -1;
  return t;
}
void qa_app_sim_ordered_actor_turns_destroy(qa_app_sim_ordered_actor_turns *turns) {
  if (!turns) return;
  free(turns->pending); free(turns);
}
bool qa_app_sim_ordered_actor_turns_next(qa_app_sim_ordered_actor_turns *t, qa_app_sim_actor_visited *visited, qa_owned_actor *out) {
  for (;;) {
    if (t->revision != (int64_t)qa_session_actor_registry_revision(t->actors)) {
      t->pending_count = 0;
      size_t count = 0;
      const qa_actor_observation *observations = qa_session_actor_registry_observations(t->actors, &count);
      free(t->pending);
      t->pending = count ? malloc(count * sizeof *t->pending) : NULL;
      if (count && !t->pending) qa_range_error("Out of memory");
      for (size_t i = 0; i < count; i++) {
        qa_owned_actor actor;
        if (!qa_session_actor_registry_resolve_owned(t->actors, observations[i].id, &actor)) continue;
        if (in_visited(visited, actor)) continue;
        t->pending[t->pending_count].actor = actor;
        t->position_of(t->position_ctx, actor, t->pending[t->pending_count].position);
        t->pending_count++;
      }
      qa_js_sort(t->pending, t->pending_count, sizeof t->pending[0], pending_cmp);
      t->revision = (int64_t)qa_session_actor_registry_revision(t->actors);
      t->index = 0;
    }
    if (t->index >= t->pending_count) return false;
    pending_entry next = t->pending[t->index++];
    if (in_visited(visited, next.actor)) continue;
    if (t->has_cursor && (next.position[0] < t->cursor[0]
        || (next.position[0] == t->cursor[0] && next.position[1] <= t->cursor[1]))) continue;
    t->has_cursor = true; t->cursor[0] = next.position[0]; t->cursor[1] = next.position[1];
    add_visited(visited, next.actor);
    *out = next.actor;
    return true;
  }
}
