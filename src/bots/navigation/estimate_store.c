/* Port of src/bots/navigation/estimate-store.ts. GPL-2.0-or-later. See header. */
#include "qa/bots/navigation/estimate_store.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_estimate_value_release(qa_estimate_value *value) {
  if (value == NULL)
    return;
  if (--value->refs == 0)
    value->destroy(value);
}

static void *oom(void *p) {
  if (p == NULL)
    qa_range_error("Out of memory in the navigation estimate store");
  return p;
}

void qa_estimate_store_init(qa_estimate_store *store) { memset(store, 0, sizeof *store); }

void qa_estimate_store_destroy(qa_estimate_store *store) {
  for (size_t slot = 0; slot < store->slot_count; slot++)
    if (store->live[slot])
      qa_estimate_value_release(store->values[slot]);
  for (size_t i = 0; i < store->scope_count; i++)
    free(store->scopes[i]);
  free(store->scopes);
  free(store->scope_ids);
  free(store->tags);
  free(store->keys_a);
  free(store->keys_b);
  free(store->costs);
  free(store->stamps);
  free(store->live);
  free(store->values);
  free(store->chain);
  free(store->buckets);
  free(store->free_slots);
  memset(store, 0, sizeof *store);
}

/* TS StringTable.find: 0 when the scope was never interned. */
static uint32_t scope_find(const qa_estimate_store *store, const char *scope) {
  for (size_t i = 0; i < store->scope_count; i++)
    if (strcmp(store->scopes[i], scope) == 0)
      return (uint32_t)(i + 1);
  return 0;
}
static uint32_t scope_intern(qa_estimate_store *store, const char *scope) {
  uint32_t id = scope_find(store, scope);
  if (id != 0)
    return id;
  if (store->scope_count == store->scope_cap) {
    size_t cap = store->scope_cap ? store->scope_cap * 2 : 4;
    store->scopes = oom(realloc(store->scopes, cap * sizeof *store->scopes));
    store->scope_cap = cap;
  }
  size_t length = strlen(scope);
  char *copy = oom(malloc(length + 1));
  memcpy(copy, scope, length + 1);
  store->scopes[store->scope_count++] = copy;
  return (uint32_t)store->scope_count;
}

static size_t bucket_of(const qa_estimate_store *store, uint32_t a, uint32_t b) {
  uint32_t hash = (a * 0x9e3779b1u) ^ (b * 0x85ebca6bu);
  return hash & (store->bucket_count - 1);
}
static void rehash(qa_estimate_store *store) {
  for (size_t i = 0; i < store->bucket_count; i++)
    store->buckets[i] = -1;
  for (size_t slot = 0; slot < store->slot_count; slot++) {
    if (!store->live[slot])
      continue;
    size_t bucket = bucket_of(store, store->keys_a[slot], store->keys_b[slot]);
    store->chain[slot] = store->buckets[bucket];
    store->buckets[bucket] = (int64_t)slot;
  }
}
/* TS grow(): capacity max(64, length * 2); the bucket table follows it. */
static void grow(qa_estimate_store *store) {
  size_t capacity = store->capacity * 2 > 64 ? store->capacity * 2 : 64;
  store->scope_ids = oom(realloc(store->scope_ids, capacity * sizeof *store->scope_ids));
  store->tags = oom(realloc(store->tags, capacity * sizeof *store->tags));
  store->keys_a = oom(realloc(store->keys_a, capacity * sizeof *store->keys_a));
  store->keys_b = oom(realloc(store->keys_b, capacity * sizeof *store->keys_b));
  store->costs = oom(realloc(store->costs, capacity * sizeof *store->costs));
  store->stamps = oom(realloc(store->stamps, capacity * sizeof *store->stamps));
  store->live = oom(realloc(store->live, capacity * sizeof *store->live));
  store->values = oom(realloc(store->values, capacity * sizeof *store->values));
  store->chain = oom(realloc(store->chain, capacity * sizeof *store->chain));
  store->free_slots = oom(realloc(store->free_slots, capacity * sizeof *store->free_slots));
  for (size_t i = store->capacity; i < capacity; i++) {
    store->live[i] = 0;
    store->values[i] = NULL;
  }
  store->capacity = capacity;
  store->buckets = oom(realloc(store->buckets, capacity * sizeof *store->buckets));
  store->bucket_count = capacity;
  rehash(store);
}

qa_estimate_value *qa_estimate_store_find(qa_estimate_store *store, const char *scope, double a,
                                          double b, double tag) {
  uint32_t scope_id = scope_find(store, scope);
  if (scope_id == 0 || store->bucket_count == 0)
    return NULL;
  uint32_t ka = qa_js_to_uint32(a), kb = qa_js_to_uint32(b), kt = qa_js_to_uint32(tag);
  for (int64_t slot = store->buckets[bucket_of(store, ka, kb)]; slot >= 0; slot = store->chain[slot]) {
    if (store->keys_a[slot] == ka && store->keys_b[slot] == kb && store->scope_ids[slot] == scope_id &&
        store->tags[slot] == kt) {
      store->stamps[slot] = ++store->tick;
      return store->values[slot];
    }
  }
  return NULL;
}

void qa_estimate_store_put(qa_estimate_store *store, const char *scope, double a, double b,
                           double tag, qa_estimate_value *value, double cost) {
  uint32_t scope_id = scope_intern(store, scope);
  size_t slot;
  if (store->free_count > 0)
    slot = (size_t)store->free_slots[--store->free_count];
  else {
    if (store->slot_count >= store->capacity)
      grow(store);
    slot = store->slot_count++;
  }
  store->scope_ids[slot] = scope_id;
  store->tags[slot] = qa_js_to_uint32(tag);
  store->keys_a[slot] = qa_js_to_uint32(a);
  store->keys_b[slot] = qa_js_to_uint32(b);
  store->costs[slot] = cost;
  store->stamps[slot] = ++store->tick;
  store->live[slot] = 1;
  store->values[slot] = qa_estimate_value_retain(value);
  size_t bucket = bucket_of(store, store->keys_a[slot], store->keys_b[slot]);
  store->chain[slot] = store->buckets[bucket];
  store->buckets[bucket] = (int64_t)slot;
  store->live_count++;
  store->used += cost;
}

static void unlink_slot(qa_estimate_store *store, size_t slot) {
  int64_t *link = &store->buckets[bucket_of(store, store->keys_a[slot], store->keys_b[slot])];
  while (*link >= 0 && *link != (int64_t)slot)
    link = &store->chain[*link];
  if (*link == (int64_t)slot)
    *link = store->chain[slot];
}

/* Drops the least recently used entry; nothing when empty. */
void qa_estimate_store_evict_oldest(qa_estimate_store *store) {
  int64_t oldest = -1;
  for (size_t slot = 0; slot < store->slot_count; slot++)
    if (store->live[slot] && (oldest < 0 || store->stamps[slot] < store->stamps[oldest]))
      oldest = (int64_t)slot;
  if (oldest < 0)
    return;
  unlink_slot(store, (size_t)oldest);
  store->used -= store->costs[oldest];
  qa_estimate_value *value = store->values[oldest];
  store->values[oldest] = NULL;
  store->live[oldest] = 0;
  store->free_slots[store->free_count++] = oldest;
  store->live_count--;
  qa_estimate_value_release(value);
}

void qa_estimate_store_clear(qa_estimate_store *store) {
  for (size_t slot = 0; slot < store->slot_count; slot++) {
    if (!store->live[slot])
      continue;
    qa_estimate_value *value = store->values[slot];
    store->values[slot] = NULL;
    store->live[slot] = 0;
    qa_estimate_value_release(value);
  }
  for (size_t i = 0; i < store->bucket_count; i++)
    store->buckets[i] = -1;
  store->free_count = 0;
  store->slot_count = 0;
  store->live_count = 0;
  store->used = 0;
}

/* ------------------------------ GraphEstimateIndex ------------------------------ */
static qa_graph_estimate_index *registry = NULL;

static uint64_t mix(uint64_t hash, uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
  return hash;
}
static uint64_t bits_of(double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}
/* Every field the index, the estimators and the eligibility callbacks read through an edge. */
static uint64_t graph_hash(const qa_navigation_graph *graph) {
  uint64_t hash = mix(0, graph->node_count);
  for (size_t i = 0; i < graph->node_count; i++)
    hash = mix(hash, (uint64_t)graph->nodes[i].id);
  hash = mix(hash, graph->edge_count);
  for (size_t i = 0; i < graph->edge_count; i++) {
    const qa_navigation_edge *edge = &graph->edges[i];
    hash = mix(hash, (uint64_t)edge->id);
    hash = mix(hash, (uint64_t)edge->from);
    hash = mix(hash, (uint64_t)edge->to);
    hash = mix(hash, (uint64_t)edge->mode);
    hash = mix(hash, bits_of(edge->travel_seconds));
    hash = mix(hash, bits_of(edge->start.x));
    hash = mix(hash, bits_of(edge->start.y));
    hash = mix(hash, bits_of(edge->start.z));
    hash = mix(hash, bits_of(edge->end.x));
    hash = mix(hash, bits_of(edge->end.y));
    hash = mix(hash, bits_of(edge->end.z));
    hash = mix(hash, (uint64_t)edge->source_travel_type);
    hash = mix(hash, (uint64_t)edge->source_flags);
    hash = mix(hash, (uint64_t)edge->source.kind);
    hash = mix(hash, (uint64_t)edge->source.primary);
    hash = mix(hash, (uint64_t)edge->has_entity);
  }
  return hash;
}

static void index_free(qa_graph_estimate_index *index) {
  if (index == NULL)
    return;
  if (index->incoming != NULL)
    for (size_t i = 0; i < index->node_count; i++)
      free((void *)index->incoming[i]);
  free((void *)index->incoming);
  free(index->incoming_counts);
  free(index->node_ids);
  free(index->sorted_ids);
  free(index->sorted_index);
  free((void *)index->edges_by_id);
  qa_estimate_store_destroy(&index->tails);
  qa_estimate_store_destroy(&index->aas);
  free(index);
}
static void index_free_cleanup(void *arg) { index_free(arg); }

int64_t qa_graph_estimate_index_node(const qa_graph_estimate_index *index, int64_t id) {
  size_t low = 0, high = index->node_count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (index->sorted_ids[mid] < id)
      low = mid + 1;
    else
      high = mid;
  }
  return low < index->node_count && index->sorted_ids[low] == id ? (int64_t)index->sorted_index[low] : -1;
}
const qa_navigation_edge *qa_graph_estimate_index_edge(const qa_graph_estimate_index *index, int64_t id) {
  size_t low = 0, high = index->edges_by_id_count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (index->edges_by_id[mid]->id < id)
      low = mid + 1;
    else
      high = mid;
  }
  return low < index->edges_by_id_count && index->edges_by_id[low]->id == id ? index->edges_by_id[low]
                                                                            : NULL;
}

static int compare_pair(const void *left, const void *right) {
  const int64_t *a = left, *b = right;
  if (a[0] != b[0])
    return a[0] < b[0] ? -1 : 1;
  return a[1] < b[1] ? -1 : a[1] > b[1];
}

static qa_graph_estimate_index *index_build(const qa_navigation_graph *graph, uint64_t hash) {
  qa_graph_estimate_index *index = oom(calloc(1, sizeof *index));
  qa_cleanup guard;
  qa_cleanup_push(&guard, index_free_cleanup, index);
  index->refs = 1;
  index->edges_key = graph->edges;
  index->edge_count_key = graph->edge_count;
  index->node_count_key = graph->node_count;
  index->hash = hash;
  qa_estimate_store_init(&index->tails);
  qa_estimate_store_init(&index->aas);
  size_t n = graph->node_count;
  /* (id, position) pairs sorted by id then position: the first occurrence is the Map key. */
  int64_t *pairs = oom(malloc((n ? n : 1) * 2 * sizeof *pairs));
  for (size_t i = 0; i < n; i++) {
    pairs[2 * i] = graph->nodes[i].id;
    pairs[2 * i + 1] = (int64_t)i;
  }
  qsort(pairs, n, 2 * sizeof *pairs, compare_pair);
  bool *first = oom(calloc(n ? n : 1, sizeof *first));
  size_t unique = 0;
  for (size_t i = 0; i < n; i++)
    if (i == 0 || pairs[2 * i] != pairs[2 * (i - 1)]) {
      first[pairs[2 * i + 1]] = true;
      unique++;
    }
  index->node_ids = malloc((unique ? unique : 1) * sizeof *index->node_ids);
  index->sorted_ids = malloc((unique ? unique : 1) * sizeof *index->sorted_ids);
  index->sorted_index = malloc((unique ? unique : 1) * sizeof *index->sorted_index);
  index->incoming = calloc(unique ? unique : 1, sizeof *index->incoming);
  index->incoming_counts = calloc(unique ? unique : 1, sizeof *index->incoming_counts);
  if (!index->node_ids || !index->sorted_ids || !index->sorted_index || !index->incoming ||
      !index->incoming_counts) {
    free(pairs);
    free(first);
    oom(NULL);
  }
  /* Canonical order is graph order of first occurrences (Map insertion order). */
  size_t *canonical = oom(malloc((n ? n : 1) * sizeof *canonical));
  for (size_t i = 0, next = 0; i < n; i++)
    if (first[i]) {
      canonical[i] = next;
      index->node_ids[next++] = graph->nodes[i].id;
    }
  index->node_count = unique;
  for (size_t i = 0, next = 0; i < n; i++)
    if (i == 0 || pairs[2 * i] != pairs[2 * (i - 1)]) {
      index->sorted_ids[next] = pairs[2 * i];
      index->sorted_index[next++] = canonical[pairs[2 * i + 1]];
    }
  free(canonical);
  free(first);
  free(pairs);
  size_t *caps = oom(calloc(unique ? unique : 1, sizeof *caps));
  for (size_t i = 0; i < graph->edge_count; i++) {
    const qa_navigation_edge *edge = &graph->edges[i];
    int64_t to = qa_graph_estimate_index_node(index, edge->to);
    if (to < 0) {
      free(caps);
      qa_range_error("Estimate edge has no destination node");
    }
    if (!isfinite(edge->travel_seconds) || edge->travel_seconds < 0) {
      free(caps);
      qa_range_error("Invalid estimated edge duration");
    }
    if (index->incoming_counts[to] == caps[to]) {
      size_t cap = caps[to] ? caps[to] * 2 : 4;
      const qa_navigation_edge **grown = realloc((void *)index->incoming[to], cap * sizeof *grown);
      if (grown == NULL) {
        free(caps);
        oom(NULL);
      }
      index->incoming[to] = grown;
      caps[to] = cap;
    }
    index->incoming[to][index->incoming_counts[to]++] = edge;
  }
  free(caps);
  /* edgesById: (id, position) sorted, the last position of each id wins. */
  size_t m = graph->edge_count;
  int64_t *edge_pairs = oom(malloc((m ? m : 1) * 2 * sizeof *edge_pairs));
  for (size_t i = 0; i < m; i++) {
    edge_pairs[2 * i] = graph->edges[i].id;
    edge_pairs[2 * i + 1] = (int64_t)i;
  }
  qsort(edge_pairs, m, 2 * sizeof *edge_pairs, compare_pair);
  index->edges_by_id = malloc((m ? m : 1) * sizeof *index->edges_by_id);
  if (index->edges_by_id == NULL) {
    free(edge_pairs);
    oom(NULL);
  }
  for (size_t i = 0; i < m; i++)
    if (i + 1 == m || edge_pairs[2 * (i + 1)] != edge_pairs[2 * i])
      index->edges_by_id[index->edges_by_id_count++] = &graph->edges[edge_pairs[2 * i + 1]];
  free(edge_pairs);
  qa_cleanup_pop(&guard);
  return index;
}

qa_graph_estimate_index *qa_graph_estimate_index_acquire(const qa_navigation_graph *graph) {
  uint64_t hash = graph_hash(graph);
  /* An empty edge list has no identity to share (every TS [] is its own array). */
  if (graph->edges != NULL && graph->edge_count > 0)
    for (qa_graph_estimate_index *index = registry; index != NULL; index = index->next)
      if (index->edges_key == graph->edges && index->edge_count_key == graph->edge_count &&
          index->node_count_key == graph->node_count && index->hash == hash) {
        index->refs++;
        return index;
      }
  qa_graph_estimate_index *index = index_build(graph, hash);
  if (graph->edges != NULL && graph->edge_count > 0) {
    index->next = registry;
    registry = index;
  }
  return index;
}

void qa_graph_estimate_index_release(qa_graph_estimate_index *index) {
  if (index == NULL || --index->refs > 0)
    return;
  for (qa_graph_estimate_index **link = &registry; *link != NULL; link = &(*link)->next)
    if (*link == index) {
      *link = index->next;
      break;
    }
  index_free(index);
}
