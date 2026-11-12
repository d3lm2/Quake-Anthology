/* Port of src/bots/navigation/estimates.ts. Generic Dijkstra tails cached in an EstimateStore;
 * AAS graphs delegate to estimate_aas.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_ESTIMATES_H
#define QA_BOTS_NAVIGATION_ESTIMATES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/bots/navigation/estimate_store.h"
#include "qa/bots/navigation/types.h"
/* flags: TS `undefined` is has_flags false. */
typedef bool (*qa_nav_estimate_eligibility)(void *ctx, const qa_navigation_edge *edge, bool has_flags, int64_t flags);
/* TS scope(): borrowed text, valid until the owner's next scope change. */
typedef const char *(*qa_nav_estimate_scope)(void *ctx);
typedef struct qa_nav_estimate_tail { bool has; double seconds; const qa_navigation_edge *first_edge; } qa_nav_estimate_tail;
/* TS ReadonlyMap<number, EstimateTail>: one cell per index node, plus the count of reached ids the
 * index has no node for (TS keeps them in the Map, so they count toward its size). */
typedef struct qa_nav_estimate_tails {
  qa_estimate_value header;
  qa_nav_estimate_tail *tails;
  size_t size;
} qa_nav_estimate_tails;
struct qa_aas_navigation_estimates;
typedef struct qa_navigation_estimates {
  struct qa_aas_navigation_estimates *aas; /* owned, NULL unless AAS graph */
  const qa_navigation_graph *graph; /* borrowed */
  void *ctx; qa_nav_estimate_eligibility allowed;
  qa_nav_estimate_scope scope; /* NULL: private store, invalidate clears it */
  qa_graph_estimate_index *index; /* shared, one reference */
  qa_estimate_store own; /* the private store when scope is NULL */
} qa_navigation_estimates;
/* False only on OOM; invalid edges raise the TS RangeError text. Private cache (TS scope null). */
bool qa_navigation_estimates_init(qa_navigation_estimates *e, const qa_navigation_graph *graph,
                                  void *ctx, qa_nav_estimate_eligibility allowed);
/* TS constructor with a scope: estimators over one edge list with equal scope text share tails. */
bool qa_navigation_estimates_init_scoped(qa_navigation_estimates *e, const qa_navigation_graph *graph,
                                         void *ctx, qa_nav_estimate_eligibility allowed,
                                         qa_nav_estimate_scope scope);
void qa_navigation_estimates_destroy(qa_navigation_estimates *e);
void qa_navigation_estimates_invalidate(qa_navigation_estimates *e);
qa_navigation_estimate_result qa_navigation_estimates_estimate(qa_navigation_estimates *e,
                                                               qa_navigation_estimate_query query);
#endif
