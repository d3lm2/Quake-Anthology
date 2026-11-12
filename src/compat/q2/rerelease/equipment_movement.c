/* Port of src/compat/q2/rerelease/equipment-movement.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/equipment_movement.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_throw.h"
#include "qa/compat/q2/rerelease/api.h"
#include "qa/foundation/js.h"

static qa_compat_q2_rerelease_guest_module *active_module = NULL;
static void *active_scope = NULL;
static void set_vector(uint8_t *state, const char *name, qa_vec3 value) {
  size_t off = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, name);
  qa_compat_q2_view_set_f32(state, off, value.x); qa_compat_q2_view_set_f32(state, off + 4, value.y); qa_compat_q2_view_set_f32(state, off + 8, value.z);
}
void qa_compat_q2_prepare_rerelease_equipment_movement(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                                       const qa_equipment_movement *equipment) {
  qa_compat_q2_rerelease_layouts_ensure();
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  uint8_t *state = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_pmove_layout.byte_length);
  size_t flags = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_flags");
  size_t gravity = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.gravity");
  if (equipment->velocity) set_vector(state, "s.velocity", *equipment->velocity);
  qa_compat_q2_view_set_i16(state, gravity, qa_js_to_int32(trunc(qa_compat_q2_view_i16(state, gravity) * equipment->gravity_scale)));
  qa_compat_q2_view_set_u16(state, flags, equipment->prediction_suppressed ? qa_compat_q2_view_u16(state, flags) | 64 : qa_compat_q2_view_u16(state, flags) & ~64u);
}
typedef struct load_ctx { qa_compat_q2_rerelease_guest_module *module; void *scope; double multiplier, reg; } load_ctx_t;
static void load_observe(void *vctx) {
  load_ctx_t *l = vctx;
  if (active_module != l->module || active_scope != l->scope) return;
  const qa_compat_q2_rerelease_module_options *opts = qa_compat_q2_rerelease_guest_module_get_options(l->module);
  uint8_t *xmm = opts->runner->simd_xmm; size_t off = (size_t)l->reg * 16;
  if (!xmm || off + 4 > opts->runner->simd_xmm_len) return;
  qa_compat_q2_view_set_f32(xmm, off, (float)(qa_compat_q2_view_f32(xmm, off) * l->multiplier));
}
typedef struct movement_call {
  qa_compat_q2_rerelease_guest_module *module; qa_compat_q2_memory *memory; uint8_t *state;
  const qa_equipment_movement *equipment; const qa_fixed_movement_pose *pose; double multiplier;
  size_t flags, type; int32_t original_type;
  qa_compat_q2_rerelease_guest_module *previous; void *previous_scope; void *scope;
  load_ctx_t *loads; uint64_t *handles; size_t handle_count; qa_compat_q2_runner *runner;
  void *execute_ctx; void (*execute)(void *);
} movement_call;
/* The TS try block. */
static void movement_body(void *v) {
  movement_call *c = v; qa_compat_q2_rerelease_guest_module *module = c->module; qa_compat_q2_memory *memory = c->memory; uint8_t *state = c->state;
  double multiplier = c->multiplier;
  if (!isfinite(multiplier) || multiplier <= 0) qa_range_error("Equipment movement speed must be finite and positive");
  if (multiplier != 1) {
    const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
    const qa_compat_q2_rerelease_module_options *opts = qa_compat_q2_rerelease_guest_module_get_options(module);
    qa_guest_address image = qa_compat_q2_mem_offset(memory, opts->get_game_api, -(int64_t)profile->movement.game_api);
    qa_guest_address game = qa_compat_q2_rerelease_guest_module_bind_game(module);
    qa_guest_address movement; bool has = qa_compat_q2_mem_read_pointer(memory,
        qa_compat_q2_mem_offset(memory, game, (int64_t)qa_compat_q2_rerelease_field_offset(qa_compat_q2_rerelease_get_game_export_layout(), "Pmove")), &movement);
    if (!has || movement.byte_offset != qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.pmove).byte_offset)
      qa_raise("Error", 0, 0, "Native movement entry differs from its equipment profile");
    size_t n = profile->movement.speed_load_count;
    c->loads = calloc(n ? n : 1, sizeof *c->loads); c->handles = calloc(n ? n : 1, sizeof *c->handles);
    if (!c->loads || !c->handles) qa_range_error("Out of memory");
    c->runner = opts->runner;
    for (size_t i = 0; i < n; i++) {
      qa_guest_address next = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.speed_loads[i].next);
      memory->check(memory->ctx, next, 1, "execute");
      c->loads[i] = (load_ctx_t){module, c->scope, multiplier, profile->movement.speed_loads[i].reg};
      uint64_t handle = opts->runner->observe_entry(opts->runner->ctx, next, &c->loads[i], load_observe);
      c->handles[c->handle_count++] = handle;
    }
  }
  if (c->pose) { qa_compat_q2_view_set_i32(state, c->type, 6); set_vector(state, "s.velocity", (qa_vec3){0, 0, 0}); } /* KexPmTypeT.PM_FREEZE */
  c->execute(c->execute_ctx);
  if (c->pose) {
    size_t height = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.viewheight");
    state[height] = qa_js_to_uint8(c->pose->view_height); /* setInt8: low 8 bits */
    set_vector(state, "mins", c->pose->bounds.min); set_vector(state, "maxs", c->pose->bounds.max);
    qa_compat_q2_view_set_u16(state, c->flags, c->pose->crouched ? qa_compat_q2_view_u16(state, c->flags) | 1 : qa_compat_q2_view_u16(state, c->flags) & ~1u);
  }
}
/* finally { restore pm_type; removals.runReverse(); the module's previous scope }: also when the original Pmove
 * raises. Without it the speed observers stayed bound to this call's storage and the scope stayed set. */
static void movement_cleanup(void *v) {
  movement_call *c = v;
  if (c->pose) qa_compat_q2_view_set_i32(c->state, c->type, c->original_type);
  size_t count = c->handle_count; c->handle_count = 0;
  for (size_t i = count; i > 0; i--) c->runner->remove_entry(c->runner->ctx, c->handles[i - 1]);
  free(c->loads); free(c->handles); c->loads = NULL; c->handles = NULL;
  active_module = c->previous;
  active_scope = c->previous_scope;
}
void qa_compat_q2_with_rerelease_equipment_movement(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                                    const qa_equipment_movement *equipment, void *execute_ctx, void (*execute)(void *)) {
  qa_compat_q2_rerelease_layouts_ensure();
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  movement_call c; memset(&c, 0, sizeof c);
  c.module = module; c.memory = memory; c.equipment = equipment; c.execute_ctx = execute_ctx; c.execute = execute;
  c.state = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_pmove_layout.byte_length);
  c.flags = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_flags");
  c.type = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_type");
  c.original_type = qa_compat_q2_view_i32(c.state, c.type);
  c.pose = equipment ? equipment->pose : NULL;
  c.multiplier = equipment && equipment->has_speed_multiplier ? equipment->speed_multiplier : 1;
  c.previous = active_module; c.previous_scope = active_scope;
  /* The scope's identity: its own block, so a later call can never be handed the same address while an observer
   * of this one is still bound. */
  c.scope = malloc(1); if (!c.scope) qa_range_error("Out of memory");
  active_module = module;
  active_scope = c.scope;
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(movement_body, &c, &thrown);
  void *scope = c.scope;
  qa_compat_q2_native_thrown cleanup_thrown;
  bool cleaned = qa_compat_q2_native_try(movement_cleanup, &c, &cleanup_thrown);
  free(scope);
  if (!cleaned) qa_compat_q2_native_rethrow(&cleanup_thrown);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
