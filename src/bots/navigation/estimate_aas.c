/* Port of src/bots/navigation/estimate-aas.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/estimate_aas.h"
#include "qa/bots/navigation/graph.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The private store, or the shared one when a scope is given (computed: the owner may move). */
static qa_estimate_store *store_of(qa_aas_navigation_estimates *e) {
  return e->scope == NULL ? &e->own : &e->index->aas;
}

static void at_check(int64_t index, size_t count) {
  if (index < 0 || index >= (int64_t)count)
    qa_range_error("Missing AAS estimate metadata %lld", (long long)index);
}

int64_t qa_aas_estimate_area_time(int64_t presence, int64_t flags, qa_vec3 start, qa_vec3 end) {
  float x = (float)((float)start.x - (float)end.x);
  float y = (float)((float)start.y - (float)end.y);
  float z = (float)((float)start.z - (float)end.z);
  float xx = (float)((double)x * x), yy = (float)((double)y * y), zz = (float)((double)z * z);
  float sum = (float)((float)((double)xx + yy) + (double)zz);
  float distance = (float)sqrt((double)sum);
  float factor = (presence & 2) == 0 ? (float)1.3 : (flags & 4) != 0 ? 1.0f : (float)0.33;
  double converted = trunc((double)(float)((double)distance * factor));
  /* The C float-to-int conversion yields INT_MIN for NaN and out-of-range values, which the later clamp turns into 1. */
  double value = isfinite(converted) && converted <= 0x7fffffff ? converted : -2147483648.0;
  int64_t v = (int64_t)value;
  if (v < 1) v = 1;
  return v & 0xffff;
}

/* --- topology --- */
static bool build_topology(qa_aas_navigation_estimates *e) {
  const qa_aas_asset *asset = e->asset;
  size_t areas = asset->area_count;
  e->incoming = calloc(areas ? areas : 1, sizeof *e->incoming);
  if (!e->incoming) return false;
  size_t *caps = calloc(areas ? areas : 1, sizeof *caps);
  if (!caps) return false;
  qa_cleanup caps_guard;
  qa_cleanup_push(&caps_guard, free, caps);
  bool ok = true;
  for (int64_t area = 1; ok && area < (int64_t)areas; area++) {
    at_check(area, asset->setting_count);
    const qa_aas_setting_rec *settings = &asset->settings[area];
    int64_t limit = settings->reach_count < 128 ? settings->reach_count : 128;
    for (int64_t offset = 0; offset < limit && ok; offset++) {
      int64_t reach = settings->first_reach + offset;
      at_check(reach, asset->reachability_count);
      int64_t target = asset->reachability[reach].area;
      at_check(target, areas);
      qa_aas_estimate_incoming_list *list = &e->incoming[target];
      if (list->count == caps[target]) {
        size_t c = caps[target] ? caps[target] * 2 : 4;
        qa_aas_estimate_incoming *p = realloc(list->v, c * sizeof *p);
        if (!p) { ok = false; break; }
        list->v = p; caps[target] = c;
      }
      list->v[list->count++] = (qa_aas_estimate_incoming){area, reach};
    }
  }
  qa_cleanup_pop(&caps_guard);
  free(caps);
  if (!ok) return false;
  for (size_t i = 0; i < areas; i++) {
    qa_aas_estimate_incoming_list *list = &e->incoming[i];
    for (size_t a = 0, b = list->count ? list->count - 1 : 0; a < b; a++, b--) {
      qa_aas_estimate_incoming tmp = list->v[a]; list->v[a] = list->v[b]; list->v[b] = tmp;
    }
  }
  e->crossings = calloc(asset->setting_count ? asset->setting_count : 1, sizeof *e->crossings);
  e->crossing_rows = calloc(asset->setting_count ? asset->setting_count : 1, sizeof *e->crossing_rows);
  if (!e->crossings || !e->crossing_rows) return false;
  for (size_t area = 0; area < asset->setting_count && ok; area++) {
    const qa_aas_setting_rec *settings = &asset->settings[area];
    size_t rows = settings->reach_count > 0 ? (size_t)settings->reach_count : 0;
    e->crossing_rows[area] = rows;
    e->crossings[area] = calloc(rows ? rows : 1, sizeof *e->crossings[area]);
    if (!e->crossings[area]) return false;
    for (size_t offset = 0; offset < rows && ok; offset++) {
      int64_t ridx = settings->first_reach + (int64_t)offset;
      at_check(ridx, asset->reachability_count);
      qa_vec3 start = asset->reachability[ridx].start;
      /* TS reads at(incoming, area) per row, after the row's reachability. */
      at_check((int64_t)area, areas);
      size_t in_count = e->incoming[area].count;
      uint16_t *row = calloc(in_count ? in_count : 1, sizeof *row);
      if (!row) return false;
      e->crossings[area][offset] = row;
      for (size_t k = 0; k < in_count; k++) {
        int64_t lr = e->incoming[area].v[k].reach;
        at_check(lr, asset->reachability_count);
        row[k] = (uint16_t)qa_aas_estimate_area_time(settings->presence, settings->flags,
                                                     asset->reachability[lr].end, start);
      }
    }
  }
  if (!ok) return false;
  e->portal_maxima = calloc(asset->portal_count ? asset->portal_count : 1, sizeof *e->portal_maxima);
  if (!e->portal_maxima) return false;
  for (size_t p = 0; p < asset->portal_count; p++) {
    int64_t area = asset->portals[p].area;
    at_check(area, asset->setting_count);
    int64_t maximum = 0;
    for (size_t r = 0; r < e->crossing_rows[area]; r++) {
      at_check(area, asset->area_count);
      for (size_t k = 0; k < e->incoming[area].count; k++)
        if (e->crossings[area][r][k] > maximum) maximum = e->crossings[area][r][k];
    }
    e->portal_maxima[p] = maximum;
  }
  return true;
}

static void destroy_cleanup(void *arg) { qa_aas_navigation_estimates_destroy(arg); }
bool qa_aas_navigation_estimates_init_scoped(qa_aas_navigation_estimates *e, const qa_navigation_graph *graph,
                                             const qa_aas_asset *asset, void *ctx,
                                             qa_nav_estimate_eligibility allowed, qa_nav_estimate_scope scope,
                                             qa_graph_estimate_index *index) {
  memset(e, 0, sizeof *e);
  e->graph = graph; e->asset = asset; e->ctx = ctx; e->allowed = allowed; e->scope = scope;
  qa_estimate_store_init(&e->own);
  /* TS: topology(asset) first, then graphEstimateIndex(graph). */
  qa_cleanup guard;
  qa_cleanup_push(&guard, destroy_cleanup, e);
  bool built = build_topology(e);
  qa_cleanup_pop(&guard);
  if (!built) { qa_aas_navigation_estimates_destroy(e); return false; }
  index->refs++;
  e->index = index;
  return true;
}
void qa_aas_navigation_estimates_destroy(qa_aas_navigation_estimates *e) {
  if (!e) return;
  if (e->incoming && e->asset) for (size_t i = 0; i < e->asset->area_count; i++) free(e->incoming[i].v);
  free(e->incoming);
  if (e->crossings && e->asset) {
    for (size_t a = 0; a < e->asset->setting_count; a++) {
      if (e->crossings[a]) for (size_t r = 0; r < e->crossing_rows[a]; r++) free(e->crossings[a][r]);
      free(e->crossings[a]);
    }
  }
  free(e->crossings); free(e->crossing_rows); free(e->portal_maxima);
  qa_estimate_store_destroy(&e->own);
  qa_graph_estimate_index_release(e->index);
  memset(e, 0, sizeof *e);
}
void qa_aas_navigation_estimates_invalidate(qa_aas_navigation_estimates *e) {
  if (e->scope != NULL) return;
  qa_estimate_store_clear(store_of(e));
}

/* --- caches: an EstimateStore budgeted at 16 MiB of table bytes --- */
static void cache_destroy(qa_estimate_value *value) {
  qa_aas_estimate_cache *cache = (qa_aas_estimate_cache *)value;
  free(cache->times); free(cache->reaches); free(cache);
}
static void *need(void *p) { if (!p) qa_range_error("Out of memory estimating AAS navigation"); return p; }
static qa_aas_estimate_cache *cache_new(size_t count) {
  qa_aas_estimate_cache *cache = need(calloc(1, sizeof *cache));
  cache->header.refs = 1; cache->header.destroy = cache_destroy;
  cache->count = count;
  cache->times = calloc(count ? count : 1, sizeof *cache->times);
  cache->reaches = calloc(count ? count : 1, sizeof *cache->reaches);
  if (!cache->times || !cache->reaches) { cache_destroy(&cache->header); need(NULL); }
  return cache;
}
static void release_cleanup(void *arg) { qa_estimate_value_release(arg); }
/* TS remember(): stores the cache, evicts past 16 MiB, and returns it; the caller's creation
   reference becomes the returned (retained) reference. */
static qa_aas_estimate_cache *remember(qa_aas_navigation_estimates *e, const char *scope, double a, double b,
                                       double tag, qa_aas_estimate_cache *cache) {
  qa_estimate_store_put(store_of(e), scope, a, b, tag, &cache->header, (double)(cache->count * 2 + cache->count));
  while (store_of(e)->used > 16 * 1024 * 1024 && qa_estimate_store_size(store_of(e)) > 1)
    qa_estimate_store_evict_oldest(store_of(e));
  return cache;
}
static const qa_navigation_edge *edge_by_id(qa_aas_navigation_estimates *e, int64_t id) {
  return qa_graph_estimate_index_edge(e->index, id);
}
static int64_t cluster_area(qa_aas_navigation_estimates *e, int64_t cluster, int64_t area) {
  at_check(area, e->asset->setting_count);
  const qa_aas_setting_rec *settings = &e->asset->settings[area];
  if (settings->cluster > 0) return settings->cluster_area;
  at_check(-settings->cluster, e->asset->portal_count);
  const qa_aas_portal_rec *portal = &e->asset->portals[-settings->cluster];
  return portal->cluster_areas[portal->front_cluster == cluster ? 0 : 1];
}

typedef struct { int64_t area, cluster, time; const uint16_t *row; size_t row_len; bool queued; } Update;
typedef struct { Update *updates; Update **queue; uint16_t *first_row; qa_aas_estimate_cache *cache; } work_t;
static void work_cleanup(void *arg) {
  work_t *w = arg;
  free(w->updates); free(w->queue); free(w->first_row);
  if (w->cache) qa_estimate_value_release(&w->cache->header);
}
static void queue_push(work_t *w, size_t *qn, size_t *qcap, Update *next) {
  if (*qn == *qcap) { w->queue = need(realloc(w->queue, *qcap * 2 * sizeof *w->queue)); *qcap *= 2; }
  w->queue[(*qn)++] = next;
}

/* TS areaCache: a retained cache (the caller releases it). */
static qa_aas_estimate_cache *area_cache(qa_aas_navigation_estimates *e, int64_t cluster, int64_t goal, int64_t flags) {
  const char *scope = e->scope ? e->scope(e->ctx) : "";
  double tag = (double)cluster * 2;
  qa_estimate_value *cached = qa_estimate_store_find(store_of(e), scope, (double)goal, (double)flags, tag);
  if (cached) return (qa_aas_estimate_cache *)qa_estimate_value_retain(cached);
  at_check(cluster, e->asset->cluster_count);
  int64_t count_i = e->asset->clusters[cluster].reachability_area_count;
  size_t count = count_i > 0 ? (size_t)count_i : 0;
  work_t w; memset(&w, 0, sizeof w);
  qa_cleanup guard;
  qa_cleanup_push(&guard, work_cleanup, &w);
  w.cache = cache_new(count);
  int64_t goal_index = cluster_area(e, cluster, goal);
  if (goal_index >= count_i) {
    qa_aas_estimate_cache *cache = w.cache;
    w.cache = NULL;
    qa_cleanup_pop(&guard);
    return remember(e, scope, (double)goal, (double)flags, tag, cache);
  }
  w.updates = need(calloc(count ? count : 1, sizeof *w.updates));
  /* TS at(updates, goalIndex) comes before at(topology.incoming, goal). */
  at_check(goal_index, count);
  at_check(goal, e->asset->area_count);
  size_t first_len = e->incoming[goal].count;
  w.first_row = need(calloc(first_len ? first_len : 1, sizeof *w.first_row));
  w.queue = need(malloc((count + 1) * sizeof *w.queue));
  Update *first = &w.updates[goal_index];
  first->area = goal; first->time = 1; first->row = w.first_row; first->row_len = first_len;
  w.cache->times[goal_index] = 1;
  size_t qn = 0, qcap = count + 1; w.queue[qn++] = first;
  for (size_t cursor = 0; cursor < qn; cursor++) {
    Update *current = w.queue[cursor]; current->queued = false;
    at_check(current->area, e->asset->area_count);
    const qa_aas_estimate_incoming_list *incoming = &e->incoming[current->area];
    for (size_t ordinal = 0; ordinal < incoming->count; ordinal++) {
      qa_aas_estimate_incoming link = incoming->v[ordinal];
      at_check(link.reach, e->asset->reachability_count);
      const qa_aas_reach_rec *reach = &e->asset->reachability[link.reach];
      const qa_navigation_edge *edge = edge_by_id(e, link.reach);
      if (!edge) continue;
      if ((qa_aas_travel_flag(reach->travel_type) & ~flags) != 0) continue;
      if (!e->allowed(e->ctx, edge, true, flags)) continue;
      at_check(reach->area, e->asset->setting_count);
      const qa_aas_setting_rec *dest = &e->asset->settings[reach->area];
      if ((qa_aas_area_travel_flags(dest->contents, dest->flags) & ~flags) != 0) continue;
      at_check(link.area, e->asset->setting_count);
      const qa_aas_setting_rec *settings = &e->asset->settings[link.area];
      if (settings->cluster > 0 && settings->cluster != cluster) continue;
      int64_t index = cluster_area(e, cluster, link.area);
      if (index >= count_i) continue;
      /* TS reads at(current.row, ordinal) before at(cache.times, index). */
      at_check((int64_t)ordinal, current->row_len);
      at_check(index, count);
      int64_t time = (current->time + current->row[ordinal] + reach->travel_time) & 0xffff;
      int64_t previous = w.cache->times[index];
      if (previous != 0 && previous <= time) continue;
      w.cache->times[index] = (uint16_t)time;
      int64_t offset = link.reach - settings->first_reach;
      w.cache->reaches[index] = (uint8_t)offset;
      Update *next = &w.updates[index];
      next->area = link.area; next->time = time;
      at_check(offset, e->crossing_rows[link.area]);
      next->row = e->crossings[link.area][offset];
      next->row_len = e->incoming[link.area].count;
      if (!next->queued) {
        next->queued = true;
        queue_push(&w, &qn, &qcap, next);
      }
    }
  }
  qa_aas_estimate_cache *cache = w.cache;
  w.cache = NULL;
  qa_cleanup_pop(&guard);
  work_cleanup(&w);
  return remember(e, scope, (double)goal, (double)flags, tag, cache);
}

/* TS portalCache: a retained cache (the caller releases it). */
static qa_aas_estimate_cache *portal_cache(qa_aas_navigation_estimates *e, int64_t cluster, int64_t goal, int64_t flags) {
  const char *scope = e->scope ? e->scope(e->ctx) : "";
  qa_estimate_value *cached = qa_estimate_store_find(store_of(e), scope, (double)goal, (double)flags, 1);
  if (cached) return (qa_aas_estimate_cache *)qa_estimate_value_retain(cached);
  size_t count = e->asset->portal_count;
  work_t w; memset(&w, 0, sizeof w);
  qa_cleanup guard;
  qa_cleanup_push(&guard, work_cleanup, &w);
  w.cache = cache_new(count);
  w.updates = need(calloc(count + 1, sizeof *w.updates));
  w.queue = need(malloc((count + 2) * sizeof *w.queue));
  Update *first = &w.updates[count];
  first->cluster = cluster; first->area = goal; first->time = 1;
  at_check(goal, e->asset->setting_count);
  int64_t goal_cluster = e->asset->settings[goal].cluster;
  /* A Uint16Array store past its length is dropped. */
  if (goal_cluster < 0 && -goal_cluster < (int64_t)count) w.cache->times[-goal_cluster] = 1;
  size_t qn = 0, qcap = count + 2; w.queue[qn++] = first;
  for (size_t cursor = 0; cursor < qn; cursor++) {
    Update *current = w.queue[cursor]; current->queued = false;
    at_check(current->cluster, e->asset->cluster_count);
    const qa_aas_cluster_rec *record = &e->asset->clusters[current->cluster];
    qa_aas_estimate_cache *local = area_cache(e, current->cluster, current->area, flags);
    qa_cleanup local_guard;
    qa_cleanup_push(&local_guard, release_cleanup, &local->header);
    for (int64_t offset = 0; offset < record->portal_count; offset++) {
      int64_t pidx = record->first_portal + offset;
      at_check(pidx, e->asset->portal_index_count);
      int64_t number = e->asset->portal_indexes[pidx];
      at_check(number, e->asset->portal_count);
      const qa_aas_portal_rec *portal = &e->asset->portals[number];
      if (portal->area == current->area) continue;
      int64_t index = cluster_area(e, current->cluster, portal->area);
      if (index >= record->reachability_area_count) continue;
      at_check(index, local->count);
      int64_t local_time = local->times[index];
      if (local_time == 0) continue;
      int64_t time = (local_time + current->time) & 0xffff;
      int64_t previous = w.cache->times[number];
      if (previous != 0 && previous <= time) continue;
      w.cache->times[number] = (uint16_t)time;
      Update *next = &w.updates[number];
      next->cluster = portal->front_cluster == current->cluster ? portal->back_cluster : portal->front_cluster;
      next->area = portal->area;
      next->time = (time + e->portal_maxima[number]) & 0xffff;
      if (!next->queued) {
        next->queued = true;
        queue_push(&w, &qn, &qcap, next);
      }
    }
    qa_cleanup_pop(&local_guard);
    qa_estimate_value_release(&local->header);
  }
  qa_aas_estimate_cache *cache = w.cache;
  w.cache = NULL;
  qa_cleanup_pop(&guard);
  work_cleanup(&w);
  return remember(e, scope, (double)goal, (double)flags, 1, cache);
}

static qa_navigation_estimate_result first_edge_result(qa_navigation_estimate_result out, const qa_navigation_edge *edge) {
  if (edge) { out.has_first_edge = true; out.first_edge = *edge; }
  return out;
}
qa_navigation_estimate_result qa_aas_navigation_estimates_estimate(qa_aas_navigation_estimates *e,
                                                                   qa_navigation_estimate_query query) {
  qa_navigation_estimate_result out; memset(&out, 0, sizeof out);
  int64_t area = query.start_node, goal = query.goal_node;
  if (area <= 0 || area >= (int64_t)e->asset->area_count || goal <= 0 || goal >= (int64_t)e->asset->area_count) return out;
  if (area == goal) { out.is_estimate = true; out.travel_time = 1; return out; }
  at_check(area, e->asset->setting_count); at_check(goal, e->asset->setting_count);
  const qa_aas_setting_rec *start = &e->asset->settings[area];
  const qa_aas_setting_rec *end = &e->asset->settings[goal];
  int64_t flags = qa_js_to_int32(query.has_travel_flags ? (double)query.travel_flags : (double)0x011c0fbe);
  if (((start->contents | end->contents) & 256) != 0) flags |= 0x00800000;
  int64_t cluster = start->cluster, goal_cluster = end->cluster;
  if (cluster < 0 && goal_cluster > 0) {
    at_check(-cluster, e->asset->portal_count);
    const qa_aas_portal_rec *portal = &e->asset->portals[-cluster];
    if (portal->front_cluster == goal_cluster || portal->back_cluster == goal_cluster) cluster = goal_cluster;
  } else if (cluster > 0 && goal_cluster < 0) {
    at_check(-goal_cluster, e->asset->portal_count);
    const qa_aas_portal_rec *portal = &e->asset->portals[-goal_cluster];
    if (portal->front_cluster == cluster || portal->back_cluster == cluster) goal_cluster = cluster;
  }
  if (cluster > 0 && cluster == goal_cluster) {
    qa_aas_estimate_cache *cache = area_cache(e, cluster, goal, flags);
    qa_cleanup guard;
    qa_cleanup_push(&guard, release_cleanup, &cache->header);
    int64_t index = cluster_area(e, cluster, area);
    at_check(cluster, e->asset->cluster_count);
    bool outside = index >= e->asset->clusters[cluster].reachability_area_count;
    int64_t time = 0;
    if (!outside) {
      at_check(index, cache->count);
      time = cache->times[index];
    }
    if (!outside && time != 0) {
      out.is_estimate = true;
      if (!query.has_origin) out.travel_time = (double)time;
      else {
        int64_t reach = start->first_reach + cache->reaches[index];
        at_check(reach, e->asset->reachability_count);
        out.travel_time = (double)(time + qa_aas_estimate_area_time(start->presence, start->flags, query.origin,
                                                                    e->asset->reachability[reach].start));
        out = first_edge_result(out, edge_by_id(e, reach));
      }
    }
    qa_cleanup_pop(&guard);
    qa_estimate_value_release(&cache->header);
    if (outside || time != 0) return out;
  }
  cluster = start->cluster; goal_cluster = end->cluster;
  if (goal_cluster < 0) { at_check(-goal_cluster, e->asset->portal_count); goal_cluster = e->asset->portals[-goal_cluster].front_cluster; }
  /* TS keeps portalCache while the loop below remembers area caches that may evict it. */
  qa_aas_estimate_cache *pcache = portal_cache(e, goal_cluster, goal, flags);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_cleanup, &pcache->header);
  if (cluster < 0) {
    at_check(-cluster, pcache->count);
    out.is_estimate = true;
    out.travel_time = (double)pcache->times[-cluster];
    if (query.has_origin) {
      at_check(-cluster, pcache->count);
      out = first_edge_result(out, edge_by_id(e, start->first_reach + pcache->reaches[-cluster]));
    }
  } else {
    int64_t best_time = 0;
    at_check(cluster, e->asset->cluster_count);
    const qa_aas_cluster_rec *record = &e->asset->clusters[cluster];
    for (int64_t offset = 0; offset < record->portal_count; offset++) {
      int64_t pidx = record->first_portal + offset;
      at_check(pidx, e->asset->portal_index_count);
      int64_t number = e->asset->portal_indexes[pidx];
      at_check(number, pcache->count);
      int64_t portal_time = pcache->times[number];
      if (portal_time == 0) continue;
      at_check(number, e->asset->portal_count);
      const qa_aas_portal_rec *portal = &e->asset->portals[number];
      qa_aas_estimate_cache *local = area_cache(e, cluster, portal->area, flags);
      qa_cleanup local_guard;
      qa_cleanup_push(&local_guard, release_cleanup, &local->header);
      int64_t index = cluster_area(e, cluster, area);
      bool skip = index >= record->reachability_area_count;
      int64_t local_time = 0;
      if (!skip) {
        at_check(index, local->count);
        local_time = local->times[index];
        skip = local_time == 0;
      }
      if (!skip) {
        bool has_reach = query.has_origin;
        int64_t reach = has_reach ? start->first_reach + local->reaches[index] : 0;
        int64_t time = (portal_time + local_time) & 0xffff;
        time = (time + e->portal_maxima[number]) & 0xffff;
        if (has_reach) {
          at_check(reach, e->asset->reachability_count);
          time = (time + qa_aas_estimate_area_time(start->presence, start->flags, query.origin,
                                                   e->asset->reachability[reach].start)) & 0xffff;
        }
        if (best_time == 0 || time < best_time) {
          best_time = time;
          out.is_estimate = true; out.travel_time = (double)time;
          out.has_first_edge = false;
          if (has_reach) out = first_edge_result(out, edge_by_id(e, reach));
        }
      }
      qa_cleanup_pop(&local_guard);
      qa_estimate_value_release(&local->header);
    }
  }
  qa_cleanup_pop(&guard);
  qa_estimate_value_release(&pcache->header);
  return out;
}
