/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/estimates.h"
#include "qa/bots/navigation/estimate_aas.h"
#include "qa/bots/navigation/heap.h"
#include "qa/bots/navigation/nav.h"
#include "qa/foundation/support.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
/* The private store, or the shared one when a scope is given (computed: the owner may move). */
static qa_estimate_store *store_of(qa_navigation_estimates *e) {
  return e->scope == NULL ? &e->own : &e->index->tails;
}
#include "qa/foundation/js.h"

/* About 16 MB of cached tails per store, matching the AAS estimate cache. */
#define MAXIMUM_CACHED_ENTRIES 200000

typedef struct { int64_t node; double seconds; } cost_t;
static double cost_key(const void *value) { return ((const cost_t *)value)->seconds; }

static void tails_destroy(qa_estimate_value *value) {
  qa_nav_estimate_tails *tails = (qa_nav_estimate_tails *)value;
  free(tails->tails);
  free(tails);
}

static void destroy_cleanup(void *arg) {
  qa_navigation_estimates *e = arg;
  free(e->aas); /* its own init cleanup already released what it held */
  e->aas = NULL;
  qa_navigation_estimates_destroy(e);
}
static bool init_common(qa_navigation_estimates *e, const qa_navigation_graph *graph, void *ctx,
                        qa_nav_estimate_eligibility allowed, qa_nav_estimate_scope scope) {
  memset(e, 0, sizeof *e);
  e->graph = graph; e->ctx = ctx; e->allowed = allowed; e->scope = scope;
  qa_estimate_store_init(&e->own);
  e->index = qa_graph_estimate_index_acquire(graph);
  if (graph->aas_asset) {
    e->aas = calloc(1, sizeof *e->aas);
    if (!e->aas) { qa_navigation_estimates_destroy(e); return false; }
    /* A topology raise abandons this estimator: drop its index reference and storage. */
    qa_cleanup guard;
    qa_cleanup_push(&guard, destroy_cleanup, e);
    bool built = qa_aas_navigation_estimates_init_scoped(e->aas, graph, graph->aas_asset, ctx, allowed, scope, e->index);
    qa_cleanup_pop(&guard);
    if (!built) { free(e->aas); e->aas = NULL; qa_navigation_estimates_destroy(e); return false; }
  }
  return true;
}
bool qa_navigation_estimates_init(qa_navigation_estimates *e, const qa_navigation_graph *graph,
                                  void *ctx, qa_nav_estimate_eligibility allowed) {
  return init_common(e, graph, ctx, allowed, NULL);
}
bool qa_navigation_estimates_init_scoped(qa_navigation_estimates *e, const qa_navigation_graph *graph,
                                         void *ctx, qa_nav_estimate_eligibility allowed,
                                         qa_nav_estimate_scope scope) {
  return init_common(e, graph, ctx, allowed, scope);
}
void qa_navigation_estimates_destroy(qa_navigation_estimates *e) {
  if (!e) return;
  if (e->aas) { qa_aas_navigation_estimates_destroy(e->aas); free(e->aas); e->aas = NULL; }
  qa_estimate_store_destroy(&e->own);
  qa_graph_estimate_index_release(e->index);
  memset(e, 0, sizeof *e);
}
void qa_navigation_estimates_invalidate(qa_navigation_estimates *e) {
  if (e->scope != NULL) return;
  if (e->aas) qa_aas_navigation_estimates_invalidate(e->aas);
  qa_estimate_store_clear(store_of(e));
}

typedef struct { int64_t id; double seconds; } extra_t;
typedef struct { extra_t *v; size_t n, cap; } extras_t;
static extra_t *extra_find(extras_t *x, int64_t id) {
  for (size_t i = 0; i < x->n; i++) if (x->v[i].id == id) return &x->v[i];
  return NULL;
}
typedef struct { qa_min_heap queue; extras_t extras; qa_nav_estimate_tails *tails; } search_t;
static void search_cleanup(void *arg) {
  search_t *s = arg;
  qa_min_heap_destroy(&s->queue);
  free(s->extras.v);
  if (s->tails) tails_destroy(&s->tails->header);
}
static void *need(void *p) { if (!p) qa_range_error("Out of memory estimating navigation"); return p; }

/* TS tails(goal, flags): a retained table (the caller releases it). */
static qa_nav_estimate_tails *tails_for(qa_navigation_estimates *e, int64_t goal, bool has_flags, int64_t flags) {
  const qa_graph_estimate_index *index = e->index;
  const char *scope = e->scope ? e->scope(e->ctx) : "";
  double b = has_flags ? (double)qa_js_to_uint32((double)flags) : 0, tag = has_flags ? 0 : 1;
  qa_estimate_value *cached = qa_estimate_store_find(store_of(e), scope, (double)goal, b, tag);
  if (cached != NULL) return (qa_nav_estimate_tails *)qa_estimate_value_retain(cached);
  search_t s; memset(&s, 0, sizeof s);
  qa_min_heap_init(&s.queue, sizeof(cost_t), cost_key);
  qa_cleanup guard;
  qa_cleanup_push(&guard, search_cleanup, &s);
  s.tails = need(calloc(1, sizeof *s.tails));
  s.tails->header.refs = 1; s.tails->header.destroy = tails_destroy;
  s.tails->tails = need(calloc(index->node_count ? index->node_count : 1, sizeof *s.tails->tails));
  int64_t goal_i = qa_graph_estimate_index_node(index, goal);
  s.tails->tails[goal_i] = (qa_nav_estimate_tail){true, 0, NULL};
  s.tails->size = 1;
  cost_t start = {goal, 0};
  if (!qa_min_heap_push(&s.queue, &start)) need(NULL);
  cost_t current;
  while (qa_min_heap_pop(&s.queue, &current)) {
    int64_t ci = qa_graph_estimate_index_node(index, current.node);
    if (ci >= 0) {
      if (!s.tails->tails[ci].has || s.tails->tails[ci].seconds != current.seconds) continue;
    } else {
      /* TS incoming.get(id) ?? []: a reached id without a node has no incoming edges. */
      continue;
    }
    for (size_t i = 0; i < index->incoming_counts[ci]; i++) {
      const qa_navigation_edge *edge = index->incoming[ci][i];
      if (!e->allowed(e->ctx, edge, has_flags, flags)) continue;
      double seconds = current.seconds + edge->travel_seconds;
      int64_t fi = qa_graph_estimate_index_node(index, edge->from);
      if (fi >= 0) {
        if (seconds >= (s.tails->tails[fi].has ? s.tails->tails[fi].seconds : INFINITY)) continue;
        if (!s.tails->tails[fi].has) s.tails->size++;
        s.tails->tails[fi] = (qa_nav_estimate_tail){true, seconds, edge};
      } else {
        extra_t *known = extra_find(&s.extras, edge->from);
        if (seconds >= (known ? known->seconds : INFINITY)) continue;
        if (known) known->seconds = seconds;
        else {
          if (s.extras.n == s.extras.cap) {
            s.extras.cap = s.extras.cap ? s.extras.cap * 2 : 4;
            s.extras.v = need(realloc(s.extras.v, s.extras.cap * sizeof *s.extras.v));
          }
          s.extras.v[s.extras.n++] = (extra_t){edge->from, seconds};
          s.tails->size++;
        }
      }
      cost_t next = {edge->from, seconds};
      if (!qa_min_heap_push(&s.queue, &next)) need(NULL);
    }
  }
  qa_nav_estimate_tails *tails = s.tails;
  qa_estimate_store_put(store_of(e), scope, (double)goal, b, tag, &tails->header, (double)tails->size);
  s.tails = NULL;
  qa_cleanup_pop(&guard);
  search_cleanup(&s);
  /* Each cache holds one entry per reachable node, so the budget counts entries, not tables. */
  while (qa_estimate_store_size(store_of(e)) > 1 &&
         (qa_estimate_store_size(store_of(e)) > 128 || store_of(e)->used > MAXIMUM_CACHED_ENTRIES))
    qa_estimate_store_evict_oldest(store_of(e));
  return tails; /* the creation reference */
}
qa_navigation_estimate_result qa_navigation_estimates_estimate(qa_navigation_estimates *e,
                                                               qa_navigation_estimate_query query) {
  qa_navigation_estimate_result out; memset(&out, 0, sizeof out);
  int64_t start_i = qa_graph_estimate_index_node(e->index, query.start_node);
  /* AAS_AreaRouteToGoalArea answers equal areas before it range-tests them. */
  if (e->aas && query.start_node == query.goal_node && start_i < 0) { out.is_estimate = true; out.travel_time = 1; return out; }
  if (start_i < 0 || qa_graph_estimate_index_node(e->index, query.goal_node) < 0) return out;
  if (query.has_travel_flags && (query.travel_flags < -0x80000000LL || query.travel_flags > 0xffffffffLL))
    qa_range_error("Estimate travel flags must be a 32-bit mask");
  if (!e->aas && query.has_origin && (!isfinite((float)query.origin.x) || !isfinite((float)query.origin.y)
                           || !isfinite((float)query.origin.z)))
    qa_range_error("Estimate origin must contain finite float32 coordinates");
  if (e->aas) return qa_aas_navigation_estimates_estimate(e->aas, query);
  if (query.start_node == query.goal_node) { out.is_estimate = true; out.travel_time = 1; return out; }
  qa_nav_estimate_tails *tails = tails_for(e, query.goal_node, query.has_travel_flags, query.travel_flags);
  qa_nav_estimate_tail tail = tails->tails[start_i];
  qa_estimate_value_release(&tails->header);
  if (!tail.has) return out;
  double seconds = tail.seconds;
  if (query.has_origin && tail.first_edge) {
    qa_vec3 start = tail.first_edge->start, origin = query.origin;
    double heuristic = e->graph->kex_asset ? e->graph->kex_asset->heuristic : 1;
    seconds += qa_js_hypot3(start.x - origin.x, start.y - origin.y, start.z - origin.z) * heuristic / 320;
  }
  out.is_estimate = true;
  /* Math.max(1, Math.trunc(Math.fround(seconds * 100))): NaN stays NaN. */
  out.travel_time = qa_js_max(1, trunc((float)(seconds * 100)));
  if (query.has_origin && tail.first_edge) { out.has_first_edge = true; out.first_edge = *tail.first_edge; }
  return out;
}
