/* Port of src/bots/navigation/estimate-aas.ts. TS WeakMap topology cache
 * becomes per-instance topology (same observable results); the estimate caches live in an
 * EstimateStore shared through the graph's estimate index when a scope is given.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_ESTIMATE_AAS_H
#define QA_BOTS_NAVIGATION_ESTIMATE_AAS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/estimates.h"
#include "qa/bots/navigation/types.h"

int64_t qa_aas_estimate_area_time(int64_t presence, int64_t flags, qa_vec3 start, qa_vec3 end);

typedef struct qa_aas_estimate_incoming { int64_t area, reach; } qa_aas_estimate_incoming;
typedef struct qa_aas_estimate_incoming_list { qa_aas_estimate_incoming *v; size_t count; } qa_aas_estimate_incoming_list;
/* TS AasEstimateCache, reference counted (qa_estimate_value). */
typedef struct qa_aas_estimate_cache {
  qa_estimate_value header;
  uint16_t *times; uint8_t *reaches; size_t count;
} qa_aas_estimate_cache;
typedef struct qa_aas_navigation_estimates {
  const qa_navigation_graph *graph;
  const qa_aas_asset *asset;
  void *ctx; qa_nav_estimate_eligibility allowed;
  qa_nav_estimate_scope scope; /* NULL: private store, invalidate clears it */
  qa_graph_estimate_index *index; /* shared, one reference: edgesById and the shared store */
  qa_estimate_store own; /* the private store when scope is NULL */
  qa_aas_estimate_incoming_list *incoming; /* per area */
  uint16_t ***crossings; /* [area][reach offset] -> row over incoming[area] */
  size_t *crossing_rows; /* per area */
  int64_t *portal_maxima; /* per portal */
} qa_aas_navigation_estimates;
/* False only on OOM; missing metadata raises the TS RangeError text. Takes a reference to index. */
bool qa_aas_navigation_estimates_init_scoped(qa_aas_navigation_estimates *e, const qa_navigation_graph *graph,
                                             const qa_aas_asset *asset, void *ctx,
                                             qa_nav_estimate_eligibility allowed, qa_nav_estimate_scope scope,
                                             qa_graph_estimate_index *index);
void qa_aas_navigation_estimates_destroy(qa_aas_navigation_estimates *e);
void qa_aas_navigation_estimates_invalidate(qa_aas_navigation_estimates *e);
qa_navigation_estimate_result qa_aas_navigation_estimates_estimate(qa_aas_navigation_estimates *e,
                                                                   qa_navigation_estimate_query query);
#endif
