/* Port of src/bots/navigation/estimate-store.ts. GPL-2.0-or-later.
 *
 * EstimateStore<V>: an LRU cache keyed by (scope, a, b, tag); the scope text is interned once and
 * `used` is the running cost the owner budgets against. Values are reference counted
 * (qa_estimate_value): the store holds one reference per live entry and drops it on eviction or
 * clear, and a caller that keeps a value across a later store operation retains it, the role the
 * TS garbage collector plays when an evicted table is still being read.
 *
 * GraphEstimateIndex: everything derived from one edge list, shared by every estimator over it. TS
 * keys a WeakMap by the edge array's identity; C keys a registry by the edges pointer, guarded by a
 * hash of the edge and node fields so storage reused for a different graph never matches, and
 * reference counts the index (it is freed when its last estimator is destroyed). */
#ifndef QA_BOTS_NAVIGATION_ESTIMATE_STORE_H
#define QA_BOTS_NAVIGATION_ESTIMATE_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/types.h"

typedef struct qa_estimate_value {
  size_t refs;
  void (*destroy)(struct qa_estimate_value *value); /* frees the value itself */
} qa_estimate_value;
static inline qa_estimate_value *qa_estimate_value_retain(qa_estimate_value *value) {
  if (value != NULL)
    value->refs++;
  return value;
}
void qa_estimate_value_release(qa_estimate_value *value);

typedef struct qa_estimate_store {
  double used;
  char **scopes; /* interned scope text; id = index + 1 */
  size_t scope_count, scope_cap;
  uint32_t *scope_ids, *tags, *keys_a, *keys_b;
  double *costs, *stamps;
  uint8_t *live;
  qa_estimate_value **values;
  int64_t *chain; /* next slot in the same bucket, -1 ends */
  int64_t *buckets; /* bucket_count heads, -1 empty */
  size_t bucket_count;
  int64_t *free_slots;
  size_t free_count;
  size_t capacity, slot_count, live_count;
  double tick;
} qa_estimate_store;
void qa_estimate_store_init(qa_estimate_store *store);
void qa_estimate_store_destroy(qa_estimate_store *store);
static inline size_t qa_estimate_store_size(const qa_estimate_store *store) { return store->live_count; }
/* Borrowed (retain to keep it past the next put/evict/clear); marks it most recently used. */
qa_estimate_value *qa_estimate_store_find(qa_estimate_store *store, const char *scope, double a,
                                          double b, double tag);
/* Takes a new reference to value. Raises RangeError on allocation failure. */
void qa_estimate_store_put(qa_estimate_store *store, const char *scope, double a, double b,
                           double tag, qa_estimate_value *value, double cost);
void qa_estimate_store_evict_oldest(qa_estimate_store *store);
void qa_estimate_store_clear(qa_estimate_store *store);

typedef struct qa_graph_estimate_index {
  size_t refs;
  const qa_navigation_edge *edges_key;
  size_t edge_count_key, node_count_key;
  uint64_t hash;
  struct qa_graph_estimate_index *next;
  /* Map<nodeId, NavigationEdge[]> of the graph that created the index, by first occurrence. */
  int64_t *node_ids; /* canonical node ids, Map insertion order */
  size_t node_count;
  int64_t *sorted_ids; /* ascending, for lookup */
  size_t *sorted_index; /* canonical index of sorted_ids[i] */
  const qa_navigation_edge ***incoming;
  size_t *incoming_counts;
  /* edgesById (Map.set: the last edge with an id wins), sorted by id. */
  const qa_navigation_edge **edges_by_id;
  size_t edges_by_id_count;
  qa_estimate_store tails, aas;
} qa_graph_estimate_index;
/* TS graphEstimateIndex(graph): the shared index for graph's edge list, with a new reference.
 * Raises the TS RangeError for an edge without a destination node or a bad duration. */
qa_graph_estimate_index *qa_graph_estimate_index_acquire(const qa_navigation_graph *graph);
void qa_graph_estimate_index_release(qa_graph_estimate_index *index);
/* Canonical node index of id, or -1 (TS incoming.has). */
int64_t qa_graph_estimate_index_node(const qa_graph_estimate_index *index, int64_t id);
/* TS edgesById.get(id) ?? null. */
const qa_navigation_edge *qa_graph_estimate_index_edge(const qa_graph_estimate_index *index, int64_t id);
#endif
