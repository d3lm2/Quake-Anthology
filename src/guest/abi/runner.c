/* Port of src/guest/abi/runner.ts. GPL-2.0-or-later. */
#include "qa/guest/abi/runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qa/foundation/support.h"

/* TS `throw new Error(...)` and `throw new TypeError(...)`. */
static void plain_error(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static void type_error(const char *message) { qa_raise("TypeError", 0, 0, "%s", message); }

void qa_capture_abi_processor_state(qa_abi_processor_snapshot *out, qa_guest_processor_state *state) {
  out->architecture = state->architecture;
  out->registers_len = qa_integer_register_file_checkpoint(&state->registers, out->registers, qa_register_file_size(state->architecture));
  out->instruction_pointer = state->instruction_pointer;
  out->flags = qa_processor_flags_value(&state->flags);
  memcpy(out->segments, state->segments, sizeof out->segments);
  out->x87 = state->x87;
  out->simd = state->simd;
}
void qa_restore_abi_processor_state(qa_guest_processor_state *state, const qa_abi_processor_snapshot *snapshot) {
  if (state->architecture != snapshot->architecture)
    type_error("ABI processor snapshot architecture differs");
  qa_integer_register_file_restore(&state->registers, snapshot->registers, snapshot->registers_len);
  state->instruction_pointer = snapshot->instruction_pointer;
  qa_processor_flags_set_value(&state->flags, snapshot->flags);
  memcpy(state->segments, snapshot->segments, sizeof state->segments);
  state->x87 = snapshot->x87;
  state->simd = snapshot->simd;
}

typedef struct active_call {
  qa_guest_core_call_context context;
  int64_t remaining;
  /* Context of this frame's host callback in progress, for unwind. */
  const qa_guest_core_call_context *dispatch_context;
  struct active_call *next; /* enclosing call */
} active_call;

typedef struct inline_region {
  qa_guest_core_address entry;
  qa_guest_core_address join;
  void (*intercept)(void *ctx, qa_guest_inline_continuation *continuation);
  void *intercept_ctx;
  struct { active_call *call; uint64_t stack; } *executing;
  size_t executing_count, executing_cap;
  qa_guest_call_runner *runner;
  qa_guest_entry_handle remove; /* callback-table bind_entry handle */
  void *accepts_ctx_holder; /* owned accepts closure state */
  /* The callback the table entry points at lives as long as the entry does (TS keeps the object). */
  qa_guest_host_callback callback;
  char callback_id[40];
  struct inline_region *next;
} inline_region;

struct qa_guest_inline_region_handle { inline_region *region; };

struct qa_guest_call_runner {
  qa_guest_call_runner_options options;
  active_call *top;
  size_t depth;
  inline_region *regions;
  const qa_guest_core_call_context *callback_context;
  uint64_t instructions_executed;
  bool loading_suspended;
  double maximum_loading_slice_ms;
  qa_guest_execution_stop last_stop;
  bool has_last_stop;
  /* Loading yield hook. */
  void (*next_frame)(void *ctx);
  void *next_frame_ctx;
  bool loading;
  struct timespec slice_start;
  bool slice_timing;
  /* TS #entryState: the processor at the outermost call's entry. */
  bool has_entry_state;
  qa_abi_processor_snapshot entry_state;
};

bool qa_guest_call_runner_init(qa_guest_call_runner *runner, qa_guest_call_runner_options options) {
  memset(runner, 0, sizeof *runner);
  if (&options.callbacks->memory->base != options.cpu->memory)
    type_error("Callback table and CPU use different guest memory");
  runner->options = options;
  qa_memory_check(options.cpu->memory, options.return_address, 1, QA_ACCESS_EXECUTE);
  return true;
}
void qa_guest_call_runner_destroy(qa_guest_call_runner *runner) {
  inline_region *region = runner->regions;
  while (region != NULL) {
    inline_region *next = region->next;
    free(region->executing);
    free(region->accepts_ctx_holder);
    free(region);
    region = next;
  }
  memset(runner, 0, sizeof *runner);
}
qa_guest_call_runner_options qa_guest_call_runner_options_of(const qa_guest_call_runner *runner) { return runner->options; }
int64_t qa_guest_call_runner_depth(const qa_guest_call_runner *runner) { return (int64_t)runner->depth; }
const qa_guest_core_call_context *qa_guest_call_runner_current_context(const qa_guest_call_runner *runner) {
  if (runner->callback_context != NULL) return runner->callback_context;
  return runner->top != NULL ? &runner->top->context : NULL;
}
double qa_guest_call_runner_maximum_loading_slice_ms(const qa_guest_call_runner *runner) {
  return runner->maximum_loading_slice_ms;
}
uint64_t qa_guest_call_runner_instructions_executed(const qa_guest_call_runner *runner) {
  return runner->instructions_executed;
}
const qa_guest_execution_stop *qa_guest_call_runner_last_stop(const qa_guest_call_runner *runner) {
  return runner->has_last_stop ? &runner->last_stop : NULL;
}

static double now_ms(void) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
static void stamp_now(struct timespec *ts) { timespec_get(ts, TIME_UTC); }

/* TS throw new GuestCallStopped(stop, context). */
static void stopped(qa_guest_call_runner *runner, qa_guest_execution_stop stop) {
  runner->last_stop = stop;
  runner->has_last_stop = true;
  qa_raise("GuestCallStopped", 0, 0, "Guest call stopped: %s", qa_guest_execution_stop_kind_name(stop.kind));
}
/* invokeSteps' `finally { this.#active.pop(); }` when a raise leaves the call. */
typedef struct active_pop {
  qa_guest_call_runner *runner;
  active_call *active, *enclosing;
  bool linked;                 /* pushed on the runner's call stack */
  qa_abi_value_layout *layouts; /* inferred layouts, until the plan is built */
} active_pop;
static void pop_active(void *arg) {
  active_pop *pop = arg;
  free(pop->layouts);
  if (!pop->linked) { free(pop->active); return; } /* raised before the call was entered */
  if (pop->runner->top != pop->active) return;      /* already unwound (and freed) by the host */
  pop->runner->top = pop->enclosing; pop->runner->depth--;
  free(pop->active);
}

static inline_region *find_region(qa_guest_call_runner *runner, uint64_t entry) {
  for (inline_region *region = runner->regions; region != NULL; region = region->next)
    if (region->entry.byte_offset == entry) return region;
  return NULL;
}

static void run_steps(qa_guest_call_runner *runner, active_call *active,
                      qa_guest_core_address return_address, int64_t slice);
static void intercept_region(qa_guest_call_runner *runner, inline_region *region, active_call *active);

typedef struct dispatch_scope {
  qa_guest_call_runner *runner;
  active_call *active;
  const qa_guest_core_call_context *previous;
  bool installed;
  qa_call_value *arguments; size_t argument_count; /* the fixed arguments */
  qa_call_value *full; size_t full_count;          /* fixed and variadic, when there are variadic ones */
  qa_abi_value_layout *extra;
  qa_call_result result;
  bool has_result;
} dispatch_scope;
static void dispatch_release(void *arg) {
  dispatch_scope *dispatch = arg;
  if (dispatch->installed) {
    /* The frame that raised may already be popped and freed; touch it only while it is still the top. */
    if (dispatch->runner->top == dispatch->active) dispatch->active->dispatch_context = NULL;
    dispatch->runner->callback_context = dispatch->previous;
  }
  for (size_t i = 0; i < dispatch->argument_count; i++) qa_call_value_free(&dispatch->arguments[i]);
  free(dispatch->arguments);
  for (size_t i = 0; i < dispatch->full_count; i++) qa_call_value_free(&dispatch->full[i]);
  free(dispatch->full);
  free(dispatch->extra);
  if (dispatch->has_result) qa_call_value_free(&dispatch->result);
  memset(dispatch, 0, sizeof *dispatch);
}

static void run_steps(qa_guest_call_runner *runner, active_call *active,
                      qa_guest_core_address return_address, int64_t slice) {
  qa_guest_cpu *cpu = runner->options.cpu;
  qa_guest_callback_table *callbacks = runner->options.callbacks;
  int64_t slice_remaining = slice;
  for (;;) {
    if (slice_remaining <= 0 && active->remaining > 0) {
      /* TS yield: only reachable during a loading call. */
      if (runner->loading && runner->next_frame != NULL) {
        if (runner->slice_timing) {
          double elapsed = now_ms() - ((double)runner->slice_start.tv_sec * 1000.0
                                       + (double)runner->slice_start.tv_nsec / 1000000.0);
          if (elapsed > runner->maximum_loading_slice_ms) runner->maximum_loading_slice_ms = elapsed;
        }
        runner->loading_suspended = true;
        runner->next_frame(runner->next_frame_ctx);
        runner->loading_suspended = false;
        stamp_now(&runner->slice_start);
      }
      slice_remaining = slice;
    }
    int64_t budget = active->remaining < slice_remaining ? active->remaining : slice_remaining;
    qa_guest_execution_stop raw_stop = cpu->run(cpu->run_ctx, budget, true, return_address);
    qa_guest_execution_stop stop = raw_stop;
    if (raw_stop.kind == QA_STOP_BUDGET && cpu->state->instruction_pointer == return_address.byte_offset) {
      memset(&stop, 0, sizeof stop);
      stop.kind = QA_STOP_RETURN; stop.instructions = raw_stop.instructions; stop.address = return_address;
    }
    if (stop.instructions < 0 || stop.instructions > active->remaining)
      plain_error("CPU returned an invalid instruction count");
    runner->instructions_executed += (uint64_t)stop.instructions;
    slice_remaining -= stop.instructions;
    for (active_call *frame = runner->top; frame != NULL; frame = frame->next) {
      frame->remaining -= stop.instructions;
      if (frame->remaining < 0) frame->remaining = 0;
    }
    if (stop.kind == QA_STOP_RETURN) return;
    if (stop.kind == QA_STOP_BUDGET && active->remaining > 0 && slice_remaining <= 0) continue;
    if (stop.kind != QA_STOP_HOST_CALL) stopped(runner, stop);
    inline_region *region = find_region(runner, stop.address.byte_offset);
    if (region != NULL) {
      if (active->remaining == 0) {
        qa_guest_execution_stop budget_stop; memset(&budget_stop, 0, sizeof budget_stop);
        budget_stop.kind = QA_STOP_BUDGET; budget_stop.instructions = 0;
        stopped(runner, budget_stop);
      }
      for (active_call *frame = runner->top; frame != NULL; frame = frame->next) frame->remaining--;
      slice_remaining--;
      intercept_region(runner, region, active);
      continue;
    }
    const qa_guest_host_callback *callback = qa_guest_callback_table_resolve(callbacks, stop.address);
    if (callback == NULL)
      qa_raise("Error", 0, 0, "Unknown guest callback at 0x%llx", (unsigned long long)stop.address.byte_offset);
    qa_x86_abi_adapter callback_adapter;
    qa_x86_abi_adapter_init(&callback_adapter, callback->signature.abi);
    qa_guest_core_call_context callback_context = active->context;
    callback_context.parent = &active->context;
    callback_context.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST;
    callback_context.callback_module = cpu->module;
    callback_context.callback_address = stop.address;
    callback_context.callback_abi = callback->signature.abi;
    /* The dispatch owns its argument values, and restores the runner's callback context however it ends
     * (TS: collector, and `finally { this.#callbackContext = previousContext; }`). */
    dispatch_scope dispatch;
    memset(&dispatch, 0, sizeof dispatch);
    dispatch.runner = runner;
    dispatch.active = active;
    dispatch.previous = runner->callback_context;
    qa_cleanup dispatch_node;
    qa_cleanup_push(&dispatch_node, dispatch_release, &dispatch);
    if (!qa_x86_abi_adapter_arguments(&callback_adapter, cpu, &callback->signature, NULL, 0, &dispatch.arguments,
                                      &dispatch.argument_count))
      qa_range_error("ABI argument allocation failed");
    if (callback->signature.variadic && runner->options.variadic_layouts == NULL)
      type_error("Variadic host callback requires a layout resolver");
    if (callback->signature.variadic) {
      size_t extra_count = 0;
      if (!runner->options.variadic_layouts(runner->options.variadic_ctx, callback, dispatch.arguments,
                                             dispatch.argument_count, &callback_context, &dispatch.extra, &extra_count))
        qa_range_error("ABI argument allocation failed");
      if (extra_count > 0) {
        if (!qa_x86_abi_adapter_arguments(&callback_adapter, cpu, &callback->signature,
                                          dispatch.extra, extra_count, &dispatch.full, &dispatch.full_count))
          qa_range_error("ABI argument allocation failed");
      }
      free(dispatch.extra);
      dispatch.extra = NULL;
    }
    const qa_call_value *arguments_ = dispatch.full != NULL ? dispatch.full : dispatch.arguments;
    const size_t argument_count = dispatch.full != NULL ? dispatch.full_count : dispatch.argument_count;
    slice_remaining--;
    for (active_call *frame = runner->top; frame != NULL; frame = frame->next) {
      frame->remaining -= 1;
      if (frame->remaining < 0) frame->remaining = 0;
    }
    runner->callback_context = &callback_context;
    active->dispatch_context = &callback_context;
    dispatch.installed = true;
    dispatch.result = qa_guest_callback_table_invoke(callbacks, stop.address, &callback_context,
                                                     arguments_, argument_count);
    dispatch.has_result = true;
    active->dispatch_context = NULL;
    runner->callback_context = dispatch.previous;
    dispatch.installed = false;
    qa_x86_abi_adapter_leave(&callback_adapter, cpu, &callback->signature, &dispatch.result);
    qa_cleanup_pop(&dispatch_node);
    dispatch_release(&dispatch);
  }
}

static void release_result(void *arg) { qa_call_value_free((qa_call_result *)arg); }

static qa_call_result invoke_steps(qa_guest_call_runner *runner, const qa_guest_call_request *request,
                                   int64_t slice) {
  if (request->instruction_budget <= 0) qa_range_error("Guest instruction budget must be positive");
  qa_guest_cpu *cpu = runner->options.cpu;
  qa_guest_core_address return_address = runner->options.return_address;
  const qa_guest_module_identity *memory_module = &cpu->module;
  if (strcmp(request->context.module.id != NULL ? request->context.module.id : "",
             memory_module->id != NULL ? memory_module->id : "") != 0
      || strcmp(request->context.module.digest != NULL ? request->context.module.digest : "",
                memory_module->digest != NULL ? memory_module->digest : "") != 0)
    type_error("Call context belongs to a different guest module");
  active_call *enclosing = runner->top;
  active_call *active = (active_call *)malloc(sizeof(active_call));
  if (active == NULL) qa_range_error("ABI argument allocation failed");
  active->context = request->context;
  if (enclosing != NULL) active->context.parent = qa_guest_call_runner_current_context(runner);
  active->remaining = enclosing == NULL ? request->instruction_budget
      : (request->instruction_budget < enclosing->remaining ? request->instruction_budget : enclosing->remaining);
  active->dispatch_context = NULL;
  active->next = enclosing;
  active_pop pop = {runner, active, enclosing, false, NULL};
  qa_cleanup pop_node; qa_cleanup_push(&pop_node, pop_active, &pop);
  uint64_t caller_instruction = cpu->state->instruction_pointer;
  bool have_saved = enclosing != NULL;
  qa_abi_processor_snapshot saved; memset(&saved, 0, sizeof saved);
  if (have_saved) qa_capture_abi_processor_state(&saved, cpu->state);
  qa_x86_abi_adapter adapter;
  qa_x86_abi_adapter_init(&adapter, request->signature.abi);
  int width = cpu->memory->pointer_bytes == 4 ? 32 : 64;
  uint64_t caller_stack = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, width, false);
  if (enclosing == NULL) { qa_capture_abi_processor_state(&runner->entry_state, cpu->state); runner->has_entry_state = true; }
  qa_x86_abi_adapter_enter(&adapter, cpu, request->target, &request->signature,
                           request->arguments, request->argument_count, return_address);
  uint64_t entry_stack = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, width, false);
  qa_abi_value_layout *layouts_storage = NULL;
  const qa_abi_value_layout *layouts = request->signature.parameters;
  size_t layout_count = request->signature.parameter_count;
  if (request->argument_count != request->signature.parameter_count) {
    layouts_storage = (qa_abi_value_layout *)malloc(sizeof(qa_abi_value_layout) * (request->argument_count == 0 ? 1 : request->argument_count));
    if (layouts_storage == NULL) qa_range_error("ABI argument allocation failed");
    pop.layouts = layouts_storage;
    for (size_t i = 0; i < request->argument_count; i++)
      layouts_storage[i] = i < request->signature.parameter_count ? request->signature.parameters[i]
          : qa_inferred_layout(&request->arguments[i], request->signature.variadic);
    layouts = layouts_storage; layout_count = request->argument_count;
  }
  qa_abi_call_plan plan;
  if (!qa_plan_guest_call(&plan, &request->signature, layouts, layout_count))
    qa_range_error("ABI argument allocation failed");
  /* Only the callee's stack cleanup is needed after the call: the plan does not outlive a stopped call. */
  const int64_t callee_pop_bytes = plan.callee_pop_bytes;
  qa_abi_call_plan_free(&plan);
  pop.layouts = NULL;
  free(layouts_storage);
  runner->top = active; runner->depth++;
  pop.linked = true;
  /* TS `try { ... } finally { this.#active.pop(); }`: the frame is popped however the call ends, the result
   * read and the processor restore included. */
  run_steps(runner, active, return_address, slice);
  uint64_t final_stack = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, width, false);
  if (final_stack != entry_stack + (uint64_t)(cpu->memory->pointer_bytes + callee_pop_bytes))
    plain_error("Guest returned with incorrect ABI stack cleanup");
  qa_call_result result = qa_x86_abi_adapter_return_value(&adapter, cpu, &request->signature);
  qa_cleanup result_node;
  qa_cleanup_push(&result_node, release_result, &result);
  if (have_saved) qa_restore_abi_processor_state(cpu->state, &saved);
  else {
    qa_integer_register_file_write(&cpu->state->registers, QA_REG_RSP, width, caller_stack, false);
    cpu->state->instruction_pointer = caller_instruction;
    runner->has_entry_state = false;
  }
  qa_cleanup_pop(&result_node);
  qa_cleanup_pop(&pop_node);
  runner->top = enclosing; runner->depth--;
  free(active);
  return result;
}

qa_call_result qa_guest_call_runner_invoke(qa_guest_call_runner *runner, const qa_guest_call_request *request) {
  if (runner->loading_suspended) plain_error("Guest loading call is suspended");
  return invoke_steps(runner, request, QA_MAX_SAFE_INTEGER);
}

/* invokeLoading's `finally`: a raised loading call leaves the runner idle. */
static void end_loading(void *arg) {
  qa_guest_call_runner *runner = arg;
  runner->loading = false; runner->next_frame = NULL; runner->slice_timing = false; runner->loading_suspended = false;
}
qa_call_result qa_guest_call_runner_invoke_loading(qa_guest_call_runner *runner,
                                                   const qa_guest_call_request *request,
                                                   void (*next_frame)(void *ctx), void *next_frame_ctx) {
  if (runner->depth != 0) plain_error("Guest loading requires an idle call runner");
  runner->loading = true;
  runner->next_frame = next_frame; runner->next_frame_ctx = next_frame_ctx;
  runner->slice_timing = true;
  stamp_now(&runner->slice_start);
  qa_cleanup loading_node; qa_cleanup_push(&loading_node, end_loading, runner);
  qa_call_result result = invoke_steps(runner, request, 16384);
  qa_cleanup_pop(&loading_node);
  double elapsed = now_ms() - ((double)runner->slice_start.tv_sec * 1000.0
                               + (double)runner->slice_start.tv_nsec / 1000000.0);
  if (elapsed > runner->maximum_loading_slice_ms) runner->maximum_loading_slice_ms = elapsed;
  runner->loading = false; runner->next_frame = NULL; runner->slice_timing = false;
  return result;
}

typedef struct accepts_state {
  qa_guest_call_runner *runner;
  inline_region *region;
  bool (*accepts)(void *ctx);
  void *accepts_ctx;
} accepts_state;
static bool region_accepts(void *ctx) {
  accepts_state *state = (accepts_state *)ctx;
  qa_guest_cpu *cpu = state->runner->options.cpu;
  int width = cpu->memory->pointer_bytes == 4 ? 32 : 64;
  uint64_t stack = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, width, false);
  if (state->accepts != NULL && !state->accepts(state->accepts_ctx)) return false;
  for (size_t i = 0; i < state->region->executing_count; i++)
    if (state->region->executing[i].call == state->runner->top
        && state->region->executing[i].stack == stack) return false;
  return true;
}
static qa_call_result inline_invoke(void *ctx, const qa_guest_core_call_context *context,
                                    const qa_call_value *arguments, size_t argument_count) {
  (void)ctx; (void)context; (void)arguments; (void)argument_count;
  plain_error("Inline region cannot be invoked as an ABI callback");
  return qa_call_void(); /* unreachable: plain_error aborts */
}

static void free_pending(void *arg) {
  void **pending = arg;
  for (int i = 0; i < 3; i++) free(pending[i]);
}

qa_guest_inline_region_handle *qa_guest_call_runner_bind_inline_region(
    qa_guest_call_runner *runner, qa_guest_core_address entry, qa_guest_core_address join,
    qa_guest_native_abi abi,
    void (*intercept)(void *ctx, qa_guest_inline_continuation *continuation), void *intercept_ctx,
    bool (*accepts)(void *ctx), void *accepts_ctx) {
  qa_guest_cpu *cpu = runner->options.cpu;
  qa_memory_check(cpu->memory, entry, 1, QA_ACCESS_EXECUTE);
  qa_memory_check(cpu->memory, join, 1, QA_ACCESS_EXECUTE);
  if (entry.byte_offset == join.byte_offset || find_region(runner, entry.byte_offset) != NULL)
    plain_error("Invalid or occupied inline region");
  inline_region *region = (inline_region *)calloc(1, sizeof(inline_region));
  accepts_state *state = (accepts_state *)malloc(sizeof(accepts_state));
  qa_guest_inline_region_handle *handle = (qa_guest_inline_region_handle *)malloc(sizeof *handle);
  if (region == NULL || state == NULL || handle == NULL) {
    free(region); free(state); free(handle);
    qa_range_error("ABI argument allocation failed");
  }
  region->entry = entry; region->join = join;
  region->intercept = intercept; region->intercept_ctx = intercept_ctx;
  region->runner = runner; region->accepts_ctx_holder = state;
  state->runner = runner; state->region = region; state->accepts = accepts; state->accepts_ctx = accepts_ctx;
  /* The table entry keeps the callback and its id for as long as it exists, so both live in the region. */
  (void)snprintf(region->callback_id, sizeof region->callback_id, "inline:%llu", (unsigned long long)entry.byte_offset);
  region->callback.id = region->callback_id;
  region->callback.signature.abi = abi;
  region->callback.signature.result_is_void = true;
  region->callback.invoke = inline_invoke;
  void *pending[3] = {region, state, handle};
  qa_cleanup pending_node;
  qa_cleanup_push(&pending_node, free_pending, pending); /* an occupied entry raises */
  region->remove = qa_guest_callback_table_bind_entry(runner->options.callbacks, entry, &region->callback,
                                                      region_accepts, state);
  qa_cleanup_pop(&pending_node);
  region->next = runner->regions; runner->regions = region;
  handle->region = region;
  return handle;
}
void qa_guest_inline_region_unbind(qa_guest_inline_region_handle *handle) {
  if (handle == NULL || handle->region == NULL) return;
  inline_region *region = handle->region;
  qa_guest_call_runner *runner = region->runner;
  if (find_region(runner, region->entry.byte_offset) == region) {
    inline_region **link = &runner->regions;
    while (*link != NULL && *link != region) link = &(*link)->next;
    if (*link == region) *link = region->next;
    if (region->remove != 0) qa_guest_callback_table_unbind_entry(runner->options.callbacks, region->remove);
    free(region->executing);
    free(region->accepts_ctx_holder);
    free(region);
  }
  handle->region = NULL;
}
void qa_guest_inline_region_handle_free(qa_guest_inline_region_handle *handle) { free(handle); }

size_t qa_guest_call_runner_size(void) { return sizeof(qa_guest_call_runner); }

/* TS invokeSteps/runSteps/interceptRegion `finally` blocks for frames a
 * host-side unwind abandoned: pop calls deeper than depth (TS
 * #active.pop), drop their inline-region executing entries
 * (executing.pop) and restore the callback context the surviving
 * frame's dispatch installed (#callbackContext = previousContext). */
void qa_guest_call_runner_recover(qa_guest_call_runner *runner) {
  if (runner->depth != 0 || !runner->has_entry_state) return;
  qa_restore_abi_processor_state(runner->options.cpu->state, &runner->entry_state);
  runner->has_entry_state = false;
}
void qa_guest_call_runner_unwind(qa_guest_call_runner *runner, int64_t depth) {
  if (depth < 0) depth = 0;
  while ((int64_t)runner->depth > depth && runner->top != NULL) {
    active_call *popped = runner->top;
    for (inline_region *region = runner->regions; region != NULL; region = region->next)
      while (region->executing_count > 0 && region->executing[region->executing_count - 1].call == popped)
        region->executing_count--;
    runner->top = popped->next; runner->depth--;
    free(popped);
  }
  runner->callback_context = runner->top != NULL ? runner->top->dispatch_context : NULL;
  if (runner->depth == 0) { runner->loading_suspended = false; }
}

typedef struct continuation_state {
  qa_guest_call_runner *runner;
  inline_region *region;
  active_call *active;
  uint64_t stack;
  int width;
  bool open, consumed;
  bool execute;            /* the use in progress */
  bool failed;             /* TS `failed`/`failure`: a use that threw, even if the interceptor swallowed it */
  qa_caught_error failure;
} continuation_state;

/* TS `finally { region.executing.pop(); }`. */
static void executing_pop(void *arg) {
  inline_region *region = arg;
  if (region->executing_count > 0) region->executing_count--;
}

static void continuation_run(void *arg) {
  continuation_state *state = arg;
  const bool execute = state->execute;
  qa_guest_call_runner *runner = state->runner;
  qa_guest_cpu *cpu = runner->options.cpu;
  uint64_t stack = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, state->width, false);
  if (!state->open || state->consumed || runner->top != state->active || stack != state->stack
      || cpu->state->instruction_pointer != state->region->entry.byte_offset)
    plain_error("Inline continuation is not at its active source frame");
  state->consumed = true;
  if (execute) {
    inline_region *region = state->region;
    if (region->executing_count == region->executing_cap) {
      size_t cap = region->executing_cap == 0 ? 4 : region->executing_cap * 2;
      void *next = realloc(region->executing, cap * sizeof *region->executing);
      if (next == NULL) qa_range_error("ABI argument allocation failed");
      region->executing = next; region->executing_cap = cap;
    }
    region->executing[region->executing_count].call = state->active;
    region->executing[region->executing_count].stack = state->stack;
    region->executing_count++;
    qa_cleanup executing_node;
    qa_cleanup_push(&executing_node, executing_pop, region);
    run_steps(runner, state->active, region->join, QA_MAX_SAFE_INTEGER);
    qa_cleanup_pop(&executing_node);
    region->executing_count--;
    uint64_t after = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, state->width, false);
    if (runner->top != state->active || after != state->stack)
      plain_error("Inline region changed its source stack");
  } else {
    cpu->state->instruction_pointer = state->region->join.byte_offset;
  }
}
/* TS `use`: `try { ... } catch (error) { failed = true; failure = error; throw error; }`. */
static void continuation_use(continuation_state *state, bool execute) {
  state->execute = execute;
  qa_caught_error caught;
  if (qa_try(continuation_run, state, &caught)) return;
  state->failed = true;
  state->failure = caught;
  qa_rethrow(&caught);
}
static void continuation_execute(void *ctx) { continuation_use((continuation_state *)ctx, true); }
static void continuation_skip(void *ctx) { continuation_use((continuation_state *)ctx, false); }

static void continuation_close(void *arg) { ((continuation_state *)arg)->open = false; }

static void intercept_region(qa_guest_call_runner *runner, inline_region *region, active_call *active) {
  qa_guest_cpu *cpu = runner->options.cpu;
  int width = cpu->memory->pointer_bytes == 4 ? 32 : 64;
  continuation_state state; memset(&state, 0, sizeof state);
  state.runner = runner; state.region = region; state.active = active; state.width = width;
  state.stack = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, width, false);
  state.open = true;
  qa_guest_inline_continuation continuation;
  continuation.ctx = &state; continuation.execute = continuation_execute; continuation.skip = continuation_skip;
  qa_cleanup open_node;
  qa_cleanup_push(&open_node, continuation_close, &state); /* TS `finally { open = false; }` */
  region->intercept(region->intercept_ctx, &continuation);
  qa_cleanup_pop(&open_node);
  state.open = false;
  if (state.failed) qa_rethrow(&state.failure); /* the interceptor swallowed a failed use */
  uint64_t stack = qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, width, false);
  if (!state.consumed || runner->top != active || stack != state.stack
      || cpu->state->instruction_pointer != region->join.byte_offset)
    plain_error("Inline interceptor did not join its source frame");
}
