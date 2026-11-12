/* Shared call-shape helpers for the rerelease slice. Internal. */
#ifndef QA_Q2RR_INTERNAL_H
#define QA_Q2RR_INTERNAL_H
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline double rr_health(qa_q2_game_services *g, qa_actor_id a) {
  qa_combat_state_result r = qa_gameplay_authority_read(g->host->combat, a);
  return r.has_value ? r.value.health : 0;
}
static inline bool rr_can_take_damage(qa_q2_game_services *g, qa_actor_id a) {
  qa_combat_state_result r = qa_gameplay_authority_read(g->host->combat, a);
  return r.has_value && r.value.can_take_damage;
}
static inline qa_q2_entity *rr_ent(qa_q2_game_services *g, const qa_actor_id *a) {
  return a ? g->entity(g->ctx, a) : NULL;
}
static inline qa_body_state rr_body_of(qa_q2_game_services *g, qa_q2_entity *e) {
  return g->body(g->ctx, e->actor);
}
static inline double rr_now(qa_q2_game_services *g) { return g->host->now(g->host->ctx); }
static inline double rr_frame(qa_q2_game_services *g) { return g->host->frame_seconds(g->host->ctx); }
static inline void rr_diag(qa_q2_game_services *g, const char *m) { g->host->diagnostic(g->host->ctx, m); }
static inline void rr_sound_ent(qa_q2_game_services *g, qa_q2_entity *e, const char *path, double ch, double vol, double att) {
  g->sound(g->ctx, e->actor, path, ch, vol, att);
}
static inline void rr_emit_host(qa_q2_game_services *g, qa_q2_presentation_event ev) { g->host->emit(g->host->ctx, ev); }
static inline void rr_emit_rr(qa_q2_rerelease_hooks *h, qa_q2_rerelease_event ev) { h->emit(h->ctx, ev); }
static inline qa_vec3 rr_v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
/* TS int32 bitwise on doubles: ToInt32, never a C cast (a value at or above
 * 2^31, such as a contents mask with bit 31, or NaN is undefined to cast). */
static inline long rr_i32(double v) { return (long)qa_js_to_int32(v); }
static inline double rr_band(double a, double b) { return (double)(qa_js_to_int32(a) & qa_js_to_int32(b)); }
static inline double rr_bor(double a, double b) { return (double)(qa_js_to_int32(a) | qa_js_to_int32(b)); }
static inline double rr_bxor(double a, double b) { return (double)(qa_js_to_int32(a) ^ qa_js_to_int32(b)); }
static inline double rr_bnot_and(double a, double b) { return (double)(qa_js_to_int32(a) & ~qa_js_to_int32(b)); }
/* TS `value || fallback` on a number: 0, -0 and NaN take the fallback. */
static inline double rr_or(double value, double fallback) { return value == 0 || isnan(value) ? fallback : value; }
/* game.host.diagnostic(`...${text}...`) with authored text of any length. */
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static inline void rr_diagf(qa_q2_game_services *g, const char *format, ...) {
  va_list args, copy;
  va_start(args, format);
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  char *message = length < 0 ? NULL : malloc((size_t)length + 1);
  if (!message) { va_end(args); qa_range_error("Out of memory"); }
  (void)vsnprintf(message, (size_t)length + 1, format, args);
  va_end(args);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, message);
  g->host->diagnostic(g->host->ctx, message);
  qa_cleanup_pop(&guard);
  free(message);
}
#endif
