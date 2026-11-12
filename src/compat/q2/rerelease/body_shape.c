/* Port of src/compat/q2/rerelease/body-shape.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/body_shape.h"
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct frame {
  qa_compat_q2_rerelease_guest_module *module; qa_guest_address address;
  const qa_movement_body_shape *body; uint8_t *view; qa_guest_address trace;
  qa_bounds accepted; uint16_t previous_duck; int8_t previous_height; size_t flags_off, height_off;
  bool refused; /* the last expansion query refused its bounds: movementBounds then returns the previous ones */
  struct frame *next;
} frame_t;
static frame_t *active_frames = NULL; /* per-process stack (TS WeakMap by module) */
static qa_vec3 read_vec(frame_t *f, const char *name) {
  size_t at = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, name);
  return (qa_vec3){qa_compat_q2_view_f32(f->view, at), qa_compat_q2_view_f32(f->view, at + 4), qa_compat_q2_view_f32(f->view, at + 8)};
}
static void write_vec(frame_t *f, const char *name, qa_vec3 v) {
  size_t at = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, name);
  qa_compat_q2_view_set_f32(f->view, at, v.x); qa_compat_q2_view_set_f32(f->view, at + 4, v.y); qa_compat_q2_view_set_f32(f->view, at + 8, v.z);
}
typedef struct fits_call { frame_t *f; qa_bounds bounds; qa_guest_address scratch; bool clear; } fits_call;
static void fits_body(void *v) {
  fits_call *c = v; frame_t *f = c->f;
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(f->module);
  qa_vec3 vecs[3] = {read_vec(f, "s.origin"), c->bounds.min, c->bounds.max};
  qa_guest_address targets[3];
  for (int i = 0; i < 3; i++) {
    targets[i] = qa_compat_q2_mem_offset(memory, c->scratch, i * 12);
    uint8_t *data = memory->borrow(memory->ctx, targets[i], 12);
    qa_compat_q2_view_set_f32(data, 0, vecs[i].x); qa_compat_q2_view_set_f32(data, 4, vecs[i].y); qa_compat_q2_view_set_f32(data, 8, vecs[i].z);
  }
  static const qa_guest_value_layout params[] = {{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},
    {false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"uint32",{NULL,0,0,0,NULL,0}}};
  qa_guest_value_layout trace_result; memset(&trace_result, 0, sizeof trace_result);
  trace_result.is_aggregate = true; trace_result.layout = qa_compat_q2_rerelease_trace_layout;
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, targets[0]), qa_compat_q2_call_pointer(true, targets[1]),
                                qa_compat_q2_call_pointer(true, targets[2]), qa_compat_q2_call_pointer(true, targets[0]), qa_compat_q2_call_uint32(0)};
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(f->module, f->trace,
      qa_compat_q2_rerelease_signature(params, 5, false, trace_result), args, 5, NULL, NULL);
  if (f->body->current_actor) f->body->current_actor(f->body->ctx);
  if (result.kind != QA_GUEST_VALUE_AGGREGATE || result.bytes.length != (size_t)qa_compat_q2_rerelease_trace_layout.byte_length)
    qa_raise("Error", 0, 0, "Original Pmove expansion returned an invalid trace");
  c->clear = result.bytes.bytes[0] == 0;
}
static void fits_unmap(void *v) {
  fits_call *c = v; qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(c->f->module);
  memory->unmap(memory->ctx, c->scratch, 36);
}
static bool fits_cb(void *vctx, qa_bounds bounds) {
  frame_t *f = vctx;
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(f->module);
  fits_call c = {f, bounds, memory->allocate(memory->ctx, 36, true, 4, "original Pmove body expansion"), false};
  /* try { trace } finally { memory.unmap(scratch, 36); }: the scratch does not stay mapped when the guest's trace,
   * or the actor check after it, raises. */
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(fits_body, &c, &thrown);
  fits_unmap(&c);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  f->refused = !c.clear;
  return c.clear;
}
static void apply_frame(frame_t *f) {
  qa_vec3 min2;
  qa_vec3 max2;
  if (f->body->current_actor) f->body->current_actor(f->body->ctx);
  qa_bounds desired = f->body->has_requested ? f->body->requested : (min2 = read_vec(f, "mins"), max2 = read_vec(f, "maxs"), (qa_bounds){min2, max2});
  /* TS `next !== desired` is identity: movementBounds hands back the previous bounds only when the expansion
   * query refused the requested ones (a value comparison took a NaN bound for a refusal). */
  f->refused = false;
  qa_bounds next = qa_movement_bounds(f->accepted, desired, fits_cb, f);
  if (f->refused) {
    qa_compat_q2_view_set_u16(f->view, f->flags_off, (qa_compat_q2_view_u16(f->view, f->flags_off) & ~1u) | f->previous_duck);
    f->view[f->height_off] = (uint8_t)f->previous_height;
  }
  f->accepted = next; write_vec(f, "mins", next.min); write_vec(f, "maxs", next.max);
  f->previous_duck = qa_compat_q2_view_u16(f->view, f->flags_off) & 1;
  f->previous_height = (int8_t)f->view[f->height_off];
}
/* TS bindNativeModEntry(host, dimensions, id, signature, (values, original) => ...): the wrapper replaces the
 * entry except for the exact frame original() re-enters, so calling the original runs the guest's own function. */
typedef struct dim_ctx { qa_compat_q2_rerelease_guest_module *module; qa_guest_address global; } dim_ctx_t;
static qa_guest_call_result dimensions_original(void *ctx, qa_guest_address address, qa_compat_q2_guest_call_signature signature,
                                                const qa_guest_call_value *values, size_t count) {
  return qa_compat_q2_rerelease_guest_module_invoke(ctx, address, signature, values, count, NULL, NULL);
}
static qa_guest_call_result dimensions_execute(void *vctx, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  dim_ctx_t *d = vctx;
  qa_guest_call_result result = qa_compat_q2_native_entry_call_original(binding, values, count);
  frame_t *current = NULL; /* frames.at(-1) of this module */
  for (frame_t *f = active_frames; f; f = f->next) if (f->module == d->module) { current = f; break; }
  if (current) {
    qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(d->module);
    qa_guest_address global_value;
    if (qa_compat_q2_mem_read_pointer(memory, d->global, &global_value) && global_value.byte_offset == current->address.byte_offset)
      apply_frame(current);
  }
  return result;
}
/* TS finally { binding?.close(); frames.pop(); }: also on a raise out of run(). */
typedef struct shape_scope { frame_t *frame; qa_compat_q2_native_entry *binding; } shape_scope;
static void shape_scope_close(void *arg) {
  shape_scope *scope = arg;
  if (scope->binding != NULL) qa_compat_q2_native_entry_close(scope->binding);
  scope->binding = NULL;
  if (scope->frame != NULL) {
    for (frame_t **at = &active_frames; *at != NULL; at = &(*at)->next)
      if (*at == scope->frame) { *at = scope->frame->next; break; }
    free(scope->frame);
    scope->frame = NULL;
  }
}
void qa_compat_q2_with_rerelease_body_shape(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                            const qa_movement_body_shape *body, void *run_ctx, void (*run)(void *)) {
  if (!body) { run(run_ctx); return; }
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
  if (!profile->movement.has_body) {
    if (body->has_requested) qa_raise("Error", 0, 0, "Native body shape requires an admitted original dimensions and trace boundary");
    run(run_ctx); return;
  }
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  const qa_compat_q2_rerelease_module_options *opts = qa_compat_q2_rerelease_guest_module_get_options(module);
  qa_guest_address image = qa_compat_q2_mem_offset(memory, opts->get_game_api, -(int64_t)profile->movement.game_api);
  qa_guest_address dimensions = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.body.dimensions);
  qa_guest_address trace = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.body.trace);
  qa_guest_address global = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.body.movement_global);
  qa_compat_q2_rerelease_layouts_ensure();
  uint8_t *view = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_pmove_layout.byte_length);
  frame_t *frame = calloc(1, sizeof *frame); if (!frame) qa_range_error("Out of memory");
  frame->module = module; frame->address = address; frame->body = body; frame->view = view; frame->trace = trace;
  frame->accepted = body->current;
  frame->flags_off = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_flags");
  frame->height_off = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.viewheight");
  frame->previous_duck = qa_compat_q2_view_u16(view, frame->flags_off) & 1;
  frame->previous_height = (int8_t)view[frame->height_off];
  bool outermost = true;
  for (frame_t *f = active_frames; f; f = f->next) if (f->module == module) outermost = false;
  frame->next = active_frames; active_frames = frame;
  shape_scope scope = {frame, NULL};
  qa_cleanup scope_node;
  qa_cleanup_push(&scope_node, shape_scope_close, &scope);
  dim_ctx_t d = {module, global};
  if (outermost) {
    /* `${memory.module.id}:client-body-dimensions` with no length limit; the binding copies the id. */
    const char *module_id = memory->module.id ? memory->module.id : ""; size_t need = strlen(module_id) + sizeof ":client-body-dimensions";
    char *id = malloc(need); if (!id) qa_range_error("Out of memory");
    (void)snprintf(id, need, "%s:client-body-dimensions", module_id);
    qa_cleanup id_owned; qa_cleanup_push(&id_owned, free, id);
    scope.binding = qa_compat_q2_native_entry_bind(memory, opts->runner, module, dimensions_original, dimensions, id,
        qa_compat_q2_rerelease_signature(NULL, 0, true, qa_compat_q2_rr_int_layout), &d, dimensions_execute, NULL, NULL);
    qa_cleanup_pop(&id_owned); free(id);
  }
  if (body->current_actor) body->current_actor(body->ctx);
  run(run_ctx);
  qa_cleanup_pop(&scope_node);
  shape_scope_close(&scope);
}
