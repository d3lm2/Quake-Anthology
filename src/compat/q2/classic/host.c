/* Port of src/compat/q2/classic/host.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/host.h"
#include "qa/core/commands/text.h"
#include "qa/compat/q2/guest_console.h"
#include "qa/compat/q2/guest_strings.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/pickup_profile.h"
#include "qa/compat/q2/classic/printf.h"
#include "qa/world/actors/body.h"
#include "qa/foundation/js.h"

bool qa_compat_q2_classic_pointer(const qa_guest_call_value *args, size_t count, size_t index, qa_guest_address *out) {
  if (index >= count || args[index].kind != QA_GUEST_VALUE_POINTER) qa_raise("TypeError", 0, 0, "API 3 argument %zu must be a pointer", index);
  if (!args[index].pointer_value.has_value) return false;
  if (out) *out = args[index].pointer_value.value;
  return true;
}
qa_guest_address qa_compat_q2_classic_required_pointer(const qa_guest_call_value *args, size_t count, size_t index) {
  qa_guest_address a; memset(&a, 0, sizeof a);
  if (!qa_compat_q2_classic_pointer(args, count, index, &a)) qa_range_error("API 3 argument %zu cannot be null", index);
  return a;
}
double qa_compat_q2_classic_number(const qa_guest_call_value *args, size_t count, size_t index) {
  if (index >= count || (args[index].kind != QA_GUEST_VALUE_INT32 && args[index].kind != QA_GUEST_VALUE_UINT32 &&
                         args[index].kind != QA_GUEST_VALUE_FLOAT32 && args[index].kind != QA_GUEST_VALUE_FLOAT64))
    qa_raise("TypeError", 0, 0, "API 3 argument %zu must be numeric", index);
  return args[index].number_value;
}

typedef struct alloc_rec { int64_t key; qa_guest_address address; double bytes, tag; } alloc_rec_t;
typedef struct surface_rec { char *key; qa_guest_address address; } surface_rec_t;
typedef struct model_rec { double index; char *name; } model_rec_t;
typedef struct cb_rec { char *id; char *name; struct qa_compat_q2_classic_guest_host *host; const qa_compat_q2_classic_import *entry; } cb_rec_t;
struct qa_compat_q2_classic_guest_host {
  qa_compat_q2_classic_guest_host_options options;
  qa_compat_q2_memory *memory; int64_t spawn_instructions; qa_guest_address imports;
  qa_compat_q2_classic_cvars *cvars;
  cb_rec_t *callbacks; size_t callback_count;
  alloc_rec_t *allocations; size_t alloc_count, alloc_cap;
  surface_rec_t *surfaces; size_t surface_count, surface_cap;
  model_rec_t *models; size_t model_count, model_cap;
  qa_compat_q2_classic_edicts *edicts; bool has_exports; qa_guest_address exports;
  bool initialized, suppress_reconcile, has_pickups;
  qa_compat_q2_native_primary_pickups pickups;
  bool has_input; void *input_ctx; qa_compat_q2_classic_input_boundary_fn input_boundary; uint64_t input_handle;
  uint8_t trace_result[56]; /* the trace import's aggregate result, copied out by the runner on return */
  qa_compat_q2_command_guest_strings *command_strings; /* TS #commandStrings */
  struct { size_t handle; void (*observer)(void *ctx); void *ctx; } *reload_observers; /* TS #reloadObservers */
  size_t reload_count, reload_cap, next_reload_handle;
};
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
/* TS: importBoundary(entry.name, arguments_, () => this.importCall(entry.name, context, arguments_)). */
typedef struct import_thunk { qa_compat_q2_classic_guest_host *h; const char *name; const qa_guest_call_context *context; const qa_guest_call_value *args; size_t count; } import_thunk;
static qa_guest_call_result import_thunk_invoke(void *ctx) {
  import_thunk *t = ctx; return qa_compat_q2_classic_guest_host_import_call(t->h, t->name, t->context, t->args, t->count);
}
static qa_guest_call_result import_cb(void *ctx, const qa_guest_call_context *context, const qa_guest_call_value *args, size_t count) {
  cb_rec_t *rec = ctx; qa_compat_q2_classic_guest_host *h = rec->host;
  if (!h->options.import_boundary) return qa_compat_q2_classic_guest_host_import_call(h, rec->name, context, args, count);
  import_thunk thunk = {h, rec->name, context, args, count};
  return h->options.import_boundary(h->options.import_boundary_ctx, rec->name, args, count, import_thunk_invoke, &thunk);
}
qa_compat_q2_classic_guest_host *qa_compat_q2_classic_guest_host_create(const qa_compat_q2_classic_guest_host_options *options) {
  qa_compat_q2_classic_guest_host *h = calloc(1, sizeof *h); if (!h) return NULL;
  h->options = *options;
  h->memory = options->runner->memory;
  if (h->memory->pointer_bytes != 4) qa_raise("TypeError", 0, 0, "Classic Q2 API 3 requires a 32-bit guest");
  h->cvars = qa_compat_q2_classic_cvars_create(h->memory, options->services.cvars);
  h->command_strings = qa_compat_q2_command_guest_strings_create(h->memory, "API 3 command text");
  h->imports = h->memory->allocate(h->memory->ctx, QA_COMPAT_Q2_CLASSIC_IMPORT_BYTES, false, 0, "API 3 game_import_t");
  h->callbacks = calloc(qa_compat_q2_classic_import_count, sizeof(cb_rec_t));
  h->callback_count = qa_compat_q2_classic_import_count;
  for (size_t i = 0; i < qa_compat_q2_classic_import_count; i++) {
    const qa_compat_q2_classic_import *entry = &qa_compat_q2_classic_imports[i];
    const char *provider = options->provider ? options->provider : "";
    size_t id_size = strlen(provider) + strlen(entry->name) + 7; char *id = malloc(id_size);
    if (!id) qa_range_error("API 3 host allocation failed");
    snprintf(id, id_size, "%s:api3.%s", provider, entry->name);
    cb_rec_t *rec = &h->callbacks[i]; rec->id = id; rec->name = (char *)entry->name; rec->host = h; rec->entry = entry;
    qa_guest_address address = options->runner->bind(options->runner->ctx, rec->id, entry->signature, rec, import_cb);
    qa_compat_q2_mem_write_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, h->imports, (int64_t)(i * 4)), true, address);
  }
  return h;
}
void qa_compat_q2_classic_guest_host_destroy(qa_compat_q2_classic_guest_host *h) {
  if (!h) return;
  for (size_t i = 0; i < h->callback_count; i++) free(h->callbacks[i].id);
  for (size_t i = 0; i < h->surface_count; i++) free(h->surfaces[i].key);
  for (size_t i = 0; i < h->model_count; i++) free(h->models[i].name);
  free(h->callbacks); free(h->allocations); free(h->surfaces); free(h->models);
  free(h->reload_observers);
  if (h->command_strings) qa_compat_q2_command_guest_strings_destroy(h->command_strings);
  qa_compat_q2_classic_edicts_destroy(h->edicts);
  qa_compat_q2_classic_cvars_destroy(h->cvars);
  free(h);
}
const qa_compat_q2_classic_guest_host_options *qa_compat_q2_classic_guest_host_get_options(const qa_compat_q2_classic_guest_host *h) { return &h->options; }
qa_compat_q2_memory *qa_compat_q2_classic_guest_host_get_memory(const qa_compat_q2_classic_guest_host *h) { return h->memory; }
qa_compat_q2_classic_cvars *qa_compat_q2_classic_guest_host_get_cvars(const qa_compat_q2_classic_guest_host *h) { return h->cvars; }
qa_guest_address qa_compat_q2_classic_guest_host_get_imports(const qa_compat_q2_classic_guest_host *h) { return h->imports; }
int64_t qa_compat_q2_classic_guest_host_spawn_instructions(const qa_compat_q2_classic_guest_host *h) { return h->spawn_instructions; }
qa_compat_q2_classic_edicts *qa_compat_q2_classic_guest_host_get_edicts(qa_compat_q2_classic_guest_host *h) {
  if (!h->edicts) qa_raise("Error", 0, 0, "GetGameAPI has not returned its export table");
  return h->edicts;
}
qa_guest_address qa_compat_q2_classic_guest_host_get_exports(const qa_compat_q2_classic_guest_host *h) {
  if (!h->has_exports) qa_raise("Error", 0, 0, "GetGameAPI has not returned its export table");
  return h->exports;
}
static qa_guest_call_result pickup_invoke(void *ctx, qa_guest_address target, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  qa_compat_q2_classic_guest_host *h = ctx;
  return qa_compat_q2_classic_guest_host_invoke(h, target, signature, values, count, NULL, h->options.instruction_budget);
}
static qa_raw_entity_view pickup_record(void *ctx, qa_guest_address address) { return qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(ctx), address); }
static bool pickup_current(void *ctx, qa_raw_entity_view record, qa_actor_id *out) {
  qa_owned_actor owned;
  if (!qa_compat_q2_classic_edicts_current(qa_compat_q2_classic_guest_host_get_edicts(ctx), record, &owned)) return false;
  *out = owned.id;
  return true;
}
static const qa_original_pickup_admission *pickup_admission(void *ctx) {
  qa_compat_q2_classic_guest_host *h = ctx;
  if (!h->options.services.pickups) qa_raise("Error", 0, 0, "API3 primary pickup authority disappeared");
  return h->options.services.pickups;
}
void qa_compat_q2_classic_guest_host_bind_pickups(qa_compat_q2_classic_guest_host *h, qa_guest_address image, const qa_compat_q2_native_pickup_profile *declared) {
  if (h->has_pickups) qa_raise("Error", 0, 0, "API3 primary pickups already bound");
  const qa_compat_q2_native_pickup_profile *profile = declared ? declared : qa_compat_q2_classic_get_pickup_profile(h->memory->module.digest);
  if (!profile || !h->options.services.pickups) return;
  if (h->options.services.projection) qa_raise("Error", 0, 0, "Primary pickup callers cannot bind a component projection");
  qa_compat_q2_native_pickup_host pickup_host;
  memset(&pickup_host, 0, sizeof pickup_host);
  pickup_host.memory = h->memory; pickup_host.runner = h->options.runner; pickup_host.ctx = h;
  pickup_host.invoke = pickup_invoke; pickup_host.record = pickup_record; pickup_host.current = pickup_current; pickup_host.admission = pickup_admission;
  qa_compat_q2_native_primary_pickups_init(&h->pickups, pickup_host, image, profile);
  h->has_pickups = true;
}
void qa_compat_q2_classic_guest_host_bind_pickup_supply(qa_compat_q2_classic_guest_host *h, qa_compat_q2_native_pickup_supply owner) {
  if (!h->has_pickups) qa_raise("Error", 0, 0, "This original game has no admitted pickup supply interface");
  qa_compat_q2_native_primary_pickups_bind_supply(&h->pickups, owner);
}
void qa_compat_q2_classic_guest_host_unbind_pickup_supply(qa_compat_q2_classic_guest_host *h, qa_compat_q2_native_pickup_supply owner) {
  qa_compat_q2_native_primary_pickups_unbind_supply(&h->pickups, owner);
}
qa_compat_q2_native_pickup_supply_evaluation qa_compat_q2_classic_guest_host_pickup_supply(qa_compat_q2_classic_guest_host *h, const qa_original_pickup_offer *offer) {
  if (!h->has_pickups) qa_raise("Error", 0, 0, "This original game has no admitted pickup supply interface");
  return qa_compat_q2_native_primary_pickups_evaluate_supply(&h->pickups, offer);
}
bool qa_compat_q2_classic_guest_host_pickup_supply_owns(qa_compat_q2_classic_guest_host *h, qa_actor_id actor, qa_item_id item, qa_actor_id pickup) {
  if (!h->has_pickups) qa_raise("Error", 0, 0, "This original game has no admitted pickup supply interface");
  return h->pickups.has_supply && h->pickups.supply.owns(h->pickups.supply.ctx, actor, item, pickup);
}
uint64_t qa_compat_q2_classic_guest_host_bind_input_movement(qa_compat_q2_classic_guest_host *h, void *ctx, qa_compat_q2_classic_input_boundary_fn boundary) {
  if (h->has_input) qa_raise("Error", 0, 0, "API3 movement already has an input owner");
  h->has_input = true; h->input_ctx = ctx; h->input_boundary = boundary; h->input_handle++;
  return h->input_handle;
}
void qa_compat_q2_classic_guest_host_unbind_input_movement(qa_compat_q2_classic_guest_host *h, uint64_t handle) {
  if (h->has_input && handle == h->input_handle) h->has_input = false;
}
void qa_compat_q2_classic_guest_host_apply_input_movement(qa_compat_q2_classic_guest_host *h, qa_guest_address address,
                                                          void *run_ctx, void (*run)(void *, const qa_movement_body_shape *)) {
  if (!h->has_input) { run(run_ctx, NULL); return; }
  h->input_boundary(h->input_ctx, address, run_ctx, run);
}
size_t qa_compat_q2_classic_guest_host_variadic_layouts(qa_compat_q2_classic_guest_host *h, qa_callback_id id,
                                                        const qa_guest_call_value *fixed, size_t fixed_count, qa_guest_value_layout *out, size_t capacity) {
  const char *name = NULL;
  for (size_t i = 0; i < h->callback_count; i++) if (id && h->callbacks[i].id && strcmp(h->callbacks[i].id, id) == 0) name = h->callbacks[i].name;
  if (!name) qa_raise("Error", 0, 0, "Variadic callback %s is not an API 3 import", id ? id : "");
  size_t index = (strcmp(name, "bprintf") == 0 || strcmp(name, "centerprintf") == 0) ? 1 : strcmp(name, "cprintf") == 0 ? 2 : 0;
  qa_guest_address a = qa_compat_q2_classic_required_pointer(fixed, fixed_count, index);
  char *fmt = qa_compat_q2_read_classic_string(h->memory, true, a, 65536);
  size_t r = qa_compat_q2_classic_printf_layouts(fmt, out, capacity);
  free(fmt); return r;
}
static qa_guest_call_context make_context(qa_compat_q2_classic_guest_host *h, qa_guest_address target,
                                          qa_compat_q2_guest_call_signature signature, const qa_raw_entity_view *self, bool loading) {
  qa_guest_call_context c; memset(&c, 0, sizeof c);
  c.module = h->memory->module;
  c.callback.kind = QA_GUEST_CALLBACK_NATIVE_GUEST; c.callback.module = h->memory->module;
  c.callback.address = target; c.callback.abi = signature.abi;
  c.parent = loading ? NULL : (h->options.runner->current_context ? h->options.runner->current_context(h->options.runner->ctx) : NULL);
  c.self_view = self; c.other = NULL;
  return c;
}
qa_guest_call_result qa_compat_q2_classic_guest_host_invoke(qa_compat_q2_classic_guest_host *h, qa_guest_address target,
                                                            qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *args,
                                                            size_t count, const qa_raw_entity_view *self, double budget) {
  return h->options.runner->invoke(h->options.runner->ctx, target, signature, args, count, make_context(h, target, signature, self, false), budget);
}
qa_guest_address qa_compat_q2_classic_guest_host_get_game_api(qa_compat_q2_classic_guest_host *h, qa_guest_address target) {
  if (h->has_exports) qa_raise("Error", 0, 0, "GetGameAPI already bound");
  static const qa_guest_value_layout pp[] = {{false, "pointer", {0}}};
  qa_guest_call_value arg = qa_compat_q2_call_pointer(true, h->imports);
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_invoke(
      h, target, qa_compat_q2_classic_signature(pp, 1, false, qa_compat_q2_q2_pointer, false), &arg, 1, NULL, h->options.instruction_budget);
  if (result.kind != QA_GUEST_VALUE_POINTER || !result.pointer_value.has_value) qa_raise("Error", 0, 0, "GetGameAPI returned null or a non-pointer result");
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_edicts_create(
      h->memory, result.pointer_value.value, h->options.services.engine->actors, h->options.provider,
      h->options.services.bind_ctx, h->options.services.bind_entity, h->options.services.projection);
  for (size_t i = 0; i < qa_compat_q2_classic_export_count; i++) {
    qa_guest_address a;
    if (!qa_compat_q2_mem_read_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, result.pointer_value.value, (int64_t)qa_compat_q2_classic_exports[i].offset), &a))
      qa_raise("Error", 0, 0, "Null API 3 export at byte %d", (int)qa_compat_q2_classic_exports[i].offset);
    h->memory->check(h->memory->ctx, a, 1, "execute");
  }
  h->has_exports = true; h->exports = result.pointer_value.value; h->edicts = edicts;
  return result.pointer_value.value;
}
qa_guest_call_result qa_compat_q2_classic_guest_host_call(qa_compat_q2_classic_guest_host *h, const char *name,
                                                          const qa_guest_call_value *args, size_t count, const qa_raw_entity_view *self, double budget) {
  if (!h->has_exports) qa_raise("Error", 0, 0, "GetGameAPI must run before lifecycle calls");
  const qa_compat_q2_classic_export *entry = qa_compat_q2_classic_find_export(name);
  if (!entry) qa_raise("Error", 0, 0, "Unknown API 3 export %s", name);
  qa_guest_address target;
  if (!qa_compat_q2_mem_read_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, h->exports, (int64_t)entry->offset), &target))
    qa_raise("Error", 0, 0, "Null API 3 export %s", name);
  qa_compat_q2_classic_cvars_refresh(h->cvars);
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_invoke(h, target, entry->signature, args, count, self, budget);
  if (h->initialized && !h->suppress_reconcile && strcmp(name, "Shutdown") != 0)
    qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
  return result;
}
qa_guest_call_result qa_compat_q2_classic_guest_host_call_loading(qa_compat_q2_classic_guest_host *h, const char *name,
                                                                  const qa_guest_call_value *args, size_t count,
                                                                  void (*next_frame)(void *), void *next_ctx, double budget) {
  if (!h->has_exports) qa_raise("Error", 0, 0, "GetGameAPI must run before lifecycle calls");
  const qa_compat_q2_classic_export *entry = qa_compat_q2_classic_find_export(name);
  if (!entry) qa_raise("Error", 0, 0, "Unknown API 3 export %s", name);
  qa_guest_address target;
  if (!qa_compat_q2_mem_read_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, h->exports, (int64_t)entry->offset), &target))
    qa_raise("Error", 0, 0, "Null API 3 export %s", name);
  qa_compat_q2_classic_cvars_refresh(h->cvars);
  qa_guest_call_result result = h->options.runner->invoke_loading(h->options.runner->ctx, target, entry->signature, args, count,
                                                                  make_context(h, target, entry->signature, NULL, true), budget, next_frame, next_ctx);
  if (h->initialized && !h->suppress_reconcile && strcmp(name, "Shutdown") != 0)
    qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
  return result;
}
void qa_compat_q2_classic_guest_host_init(qa_compat_q2_classic_guest_host *h) {
  if (h->initialized) qa_raise("Error", 0, 0, "API 3 Init already completed");
  (void)qa_compat_q2_classic_guest_host_call(h, "Init", NULL, 0, NULL, h->options.instruction_budget);
  h->initialized = true; qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
}
void qa_compat_q2_classic_guest_host_init_loading(qa_compat_q2_classic_guest_host *h, void (*next_frame)(void *), void *next_ctx) {
  if (h->initialized) qa_raise("Error", 0, 0, "API 3 Init already completed");
  (void)qa_compat_q2_classic_guest_host_call_loading(h, "Init", NULL, 0, next_frame, next_ctx, h->options.instruction_budget);
  h->initialized = true; qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
}
size_t qa_compat_q2_classic_guest_host_observe_reload(qa_compat_q2_classic_guest_host *h, void (*observer)(void *ctx), void *ctx) {
  if (h->reload_count == h->reload_cap) {
    h->reload_cap = h->reload_cap ? h->reload_cap * 2 : 4;
    h->reload_observers = realloc(h->reload_observers, h->reload_cap * sizeof *h->reload_observers);
    if (!h->reload_observers) qa_range_error("API 3 host allocation failed");
  }
  const size_t handle = ++h->next_reload_handle;
  h->reload_observers[h->reload_count].handle = handle;
  h->reload_observers[h->reload_count].observer = observer;
  h->reload_observers[h->reload_count].ctx = ctx;
  h->reload_count++;
  return handle;
}
void qa_compat_q2_classic_guest_host_unobserve_reload(qa_compat_q2_classic_guest_host *h, size_t handle) {
  for (size_t i = 0; i < h->reload_count; i++)
    if (h->reload_observers[i].handle == handle) {
      memmove(&h->reload_observers[i], &h->reload_observers[i + 1], (h->reload_count - i - 1) * sizeof *h->reload_observers);
      h->reload_count--;
      return;
    }
}
/* TS #reloaded: the guest's edict table is replaced. */
static void reloaded(qa_compat_q2_classic_guest_host *h) {
  /* A Set iteration: an observer removed during the walk is skipped and one added during it is visited. Handles grow
   * in insertion order, so the next entry is the first one past the handle just visited. */
  size_t visited = 0;
  for (;;) {
    size_t i = 0;
    while (i < h->reload_count && h->reload_observers[i].handle <= visited) i++;
    if (i == h->reload_count) break;
    visited = h->reload_observers[i].handle;
    h->reload_observers[i].observer(h->reload_observers[i].ctx);
  }
}
/* One UTF-16 code point of the UTF-8 text at *at (a malformed sequence reads as one U+FFFD, as TextDecoder does). */
static uint32_t userinfo_code_point(const unsigned char *b, size_t *length) {
  *length = 1;
  if (b[0] < 0x80) return b[0];
  if ((b[0] & 0xe0) == 0xc0 && b[0] >= 0xc2 && (b[1] & 0xc0) == 0x80) { *length = 2; return ((uint32_t)(b[0] & 0x1f) << 6) | (b[1] & 0x3f); }
  if ((b[0] & 0xf0) == 0xe0 && (b[1] & 0xc0) == 0x80 && (b[2] & 0xc0) == 0x80) {
    uint32_t v = ((uint32_t)(b[0] & 0x0f) << 12) | ((uint32_t)(b[1] & 0x3f) << 6) | (b[2] & 0x3f);
    if (v >= 0x800) { *length = 3; return v; }
  } else if ((b[0] & 0xf8) == 0xf0 && (b[1] & 0xc0) == 0x80 && (b[2] & 0xc0) == 0x80 && (b[3] & 0xc0) == 0x80) {
    uint32_t v = ((uint32_t)(b[0] & 0x07) << 18) | ((uint32_t)(b[1] & 0x3f) << 12) | ((uint32_t)(b[2] & 0x3f) << 6) | (b[3] & 0x3f);
    if (v >= 0x10000 && v <= 0x10ffff) { *length = 4; return v; }
  }
  return 0xfffd;
}
/* TS fitClassicUserinfo(text, capacity) and the byte characters writeClassicString stores: the text is the UTF-8 of
 * the TS string, cut to capacity - 1 UTF-16 units (String.slice(0, -1) for capacity 0), every unit above U+00FF
 * (each half of a surrogate pair too) replaced by "?"; the answer is the guest bytes, one per character. */
char *qa_compat_q2_fit_classic_userinfo(const char *text, size_t capacity) {
  const size_t length = strlen(text);
  size_t total = 0;
  for (size_t i = 0, n = 0; i < length; i += n) total += userinfo_code_point((const unsigned char *)text + i, &n) >= 0x10000 ? 2 : 1;
  const size_t limit = capacity == 0 ? (total == 0 ? 0 : total - 1) : (capacity - 1 < total ? capacity - 1 : total);
  char *fitted = malloc(limit + 1);
  if (!fitted) qa_range_error("API 3 host allocation failed");
  size_t units = 0;
  for (size_t i = 0, n = 0; i < length && units < limit; i += n) {
    const uint32_t cp = userinfo_code_point((const unsigned char *)text + i, &n);
    for (size_t u = 0; u < (cp >= 0x10000 ? 2u : 1u) && units < limit; u++) fitted[units++] = cp <= 0xff ? (char)cp : '?';
  }
  fitted[units] = '\0';
  return fitted;
}
static void release_provider_actors(qa_compat_q2_classic_guest_host *h) {
  if (h->options.services.projection) return;
  qa_session_actor_registry *actors = h->options.services.engine->actors;
  size_t n = 0; const qa_owned_actor *owned = qa_session_actor_registry_owned_by(actors, h->options.provider, &n);
  qa_owned_actor *copy = malloc((n ? n : 1) * sizeof *copy); if (owned && n) memcpy(copy, owned, n * sizeof *copy);
  for (size_t i = 0; i < n; i++) {
    qa_actor_source_result s = qa_session_actor_registry_source_of(actors, copy[i].id);
    if (s.has_value && s.provider && h->options.provider && strcmp(s.provider, h->options.provider) == 0)
      qa_session_actor_registry_release(actors, copy[i]);
  }
  free(copy);
}
/* A lifecycle call under a TS try: guarded_call_fn runs inside qa_try, the caller runs its finally code and rethrows. */
typedef struct guarded_call {
  qa_compat_q2_classic_guest_host *h; const char *name; const qa_guest_call_value *args; size_t count;
  const qa_raw_entity_view *self; double budget; bool loading; void (*next)(void *); void *next_ctx; qa_guest_call_result result;
} guarded_call;
static void guarded_call_fn(void *ctx) {
  guarded_call *c = ctx;
  c->result = c->loading ? qa_compat_q2_classic_guest_host_call_loading(c->h, c->name, c->args, c->count, c->next, c->next_ctx, c->budget)
                         : qa_compat_q2_classic_guest_host_call(c->h, c->name, c->args, c->count, c->self, c->budget);
}
void qa_compat_q2_classic_guest_host_shutdown(qa_compat_q2_classic_guest_host *h) {
  guarded_call call; memset(&call, 0, sizeof call); call.h = h; call.name = "Shutdown"; call.budget = h->options.instruction_budget;
  qa_caught_error caught; const bool completed = qa_try(guarded_call_fn, &call, &caught);
  /* finally */
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_close(&h->pickups);
  h->has_pickups = false; h->initialized = false;
  if (!completed) qa_rethrow(&caught);
  release_provider_actors(h);
}
typedef struct { qa_compat_q2_classic_guest_host *h; const char *name; const qa_guest_call_value *args; size_t n; double budget; } with_strings_t;
typedef struct with_strings_op { void (*op)(void *, const qa_guest_call_value *, size_t); void *op_ctx; const qa_guest_call_value *vals; size_t n; } with_strings_op;
static void with_strings_fn(void *ctx) { with_strings_op *o = ctx; o->op(o->op_ctx, o->vals, o->n); }
static void with_strings(qa_compat_q2_classic_guest_host *h, const char *const *texts, size_t n,
                         void (*op)(void *, const qa_guest_call_value *, size_t), void *op_ctx) {
  qa_guest_address *addrs = calloc(n ? n : 1, sizeof *addrs);
  qa_guest_call_value *vals = calloc(n ? n : 1, sizeof *vals);
  if (!addrs || !vals) qa_range_error("Classic Q2 host out of memory");
  for (size_t i = 0; i < n; i++) { addrs[i] = qa_compat_q2_allocate_classic_string(h->memory, texts[i]); vals[i] = qa_compat_q2_call_pointer(true, addrs[i]); }
  with_strings_op guarded = {op, op_ctx, vals, n};
  qa_caught_error caught; const bool completed = qa_try(with_strings_fn, &guarded, &caught);
  /* finally: the strings are unmapped whether or not the call raised. */
  for (size_t i = 0; i < n; i++) h->memory->unmap(h->memory->ctx, addrs[i], qa_compat_q2_classic_string_allocation_bytes(texts[i]));
  free(addrs); free(vals);
  if (!completed) qa_rethrow(&caught);
}
typedef struct { qa_compat_q2_classic_guest_host *h; const char *name; double budget; void (*next)(void *); void *next_ctx; bool loading; } call_op_t;
static void do_call(void *ctx, const qa_guest_call_value *vals, size_t n) {
  call_op_t *o = ctx;
  if (o->loading) (void)qa_compat_q2_classic_guest_host_call_loading(o->h, o->name, vals, n, o->next, o->next_ctx, o->budget);
  else (void)qa_compat_q2_classic_guest_host_call(o->h, o->name, vals, n, NULL, o->budget);
}
/* Math.min(Number.MAX_SAFE_INTEGER, instructionBudget * 10) */
static double spawn_budget(qa_compat_q2_classic_guest_host *h) { return qa_js_min(9007199254740991.0, h->options.instruction_budget * 10); }
typedef struct spawn_op { qa_compat_q2_classic_guest_host *h; const char *const *texts; call_op_t *call; } spawn_op;
static void spawn_fn(void *ctx) { spawn_op *o = ctx; with_strings(o->h, o->texts, 3, do_call, o->call); }
/* try { withStrings(...) } finally { #spawnInstructions = instructionsExecuted - before } */
static void spawn_guarded(qa_compat_q2_classic_guest_host *h, const char *const *texts, call_op_t *call) {
  int64_t before = h->options.runner->instructions_executed(h->options.runner->ctx);
  spawn_op op = {h, texts, call};
  qa_caught_error caught; const bool completed = qa_try(spawn_fn, &op, &caught);
  h->spawn_instructions = h->options.runner->instructions_executed(h->options.runner->ctx) - before;
  if (!completed) qa_rethrow(&caught);
}
void qa_compat_q2_classic_guest_host_spawn_entities(qa_compat_q2_classic_guest_host *h, const char *map, const char *entities, const char *spawn_point) {
  reloaded(h);
  release_provider_actors(h);
  const char *texts[] = {map, entities, spawn_point};
  call_op_t o = {h, "SpawnEntities", spawn_budget(h), NULL, NULL, false};
  spawn_guarded(h, texts, &o);
}
void qa_compat_q2_classic_guest_host_spawn_entities_loading(qa_compat_q2_classic_guest_host *h, const char *map, const char *entities,
                                                            const char *spawn_point, void (*next_frame)(void *), void *next_ctx) {
  reloaded(h);
  release_provider_actors(h);
  const char *texts[] = {map, entities, spawn_point};
  call_op_t o = {h, "SpawnEntities", spawn_budget(h), next_frame, next_ctx, true};
  spawn_guarded(h, texts, &o);
}
void qa_compat_q2_classic_guest_host_save_loading(qa_compat_q2_classic_guest_host *h, const char *name, const char *filename,
                                                  void (*next_frame)(void *), void *next_ctx) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  reloaded(h);
  release_provider_actors(h);
  const char *texts[] = {filename};
  call_op_t o = {h, name, spawn_budget(h), next_frame, next_ctx, true};
  with_strings(h, texts, 1, do_call, &o); /* allocate; try { callLoading } finally { unmap } */
}
static void models_set(qa_compat_q2_classic_guest_host *h, double index, const char *name);
void qa_compat_q2_classic_guest_host_set_model_name(qa_compat_q2_classic_guest_host *h, double index, const char *name) {
  if (index != trunc(index) || index < 1 || index >= 256) qa_range_error("API 3 model index outside MAX_MODELS");
  for (size_t i = 0; i < h->model_count; i++) if (h->models[i].index == index) {
    if (name[0] == 0) { free(h->models[i].name); h->models[i] = h->models[--h->model_count]; }
    else { free(h->models[i].name); h->models[i].name = xstrdup(name); }
    return;
  }
  if (name[0] == 0) return;
  models_set(h, index, name);
}
/* TS `this.#models.set(index, path)` from the modelindex and setmodel imports: no range check, and an empty path is
 * stored (setModelName is the entry that validates and deletes). */
static void models_set(qa_compat_q2_classic_guest_host *h, double index, const char *name) {
  char *copy = xstrdup(name); if (!copy) qa_range_error("API 3 host allocation failed");
  for (size_t i = 0; i < h->model_count; i++) if (h->models[i].index == index) { free(h->models[i].name); h->models[i].name = copy; return; }
  if (h->model_count == h->model_cap) {
    size_t cap = h->model_cap ? h->model_cap * 2 : 16; model_rec_t *grown = realloc(h->models, cap * sizeof *grown);
    if (!grown) { free(copy); qa_range_error("API 3 host allocation failed"); }
    h->models = grown; h->model_cap = cap;
  }
  h->models[h->model_count++] = (model_rec_t){index, copy};
}
static const char *model_name(qa_compat_q2_classic_guest_host *h, double index) {
  for (size_t i = 0; i < h->model_count; i++) if (h->models[i].index == index) return h->models[i].name;
  return NULL;
}
void qa_compat_q2_classic_guest_host_rebind_world(qa_compat_q2_classic_guest_host *h) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  if (!h->initialized || !h->has_exports || h->options.runner->depth(h->options.runner->ctx) != 0)
    qa_raise("Error", 0, 0, "API 3 world rebind requires an idle initialized module");
  /* new ClassicQ2Edicts(...) over the same exports and services; a raise keeps the previous table. */
  qa_compat_q2_classic_edicts_rebind(h->edicts, h->options.services.engine->actors, h->options.provider,
                                     h->options.services.bind_ctx, h->options.services.bind_entity, h->options.services.projection);
  for (size_t i = 0; i < h->model_count; i++) free(h->models[i].name);
  h->model_count = 0;
  reloaded(h);
}
/* Array.from({ length: maxClients }): ToLength (NaN and negatives give 0, fractions
 * truncate), and an array length beyond 2^32 - 1 is a RangeError. */
static size_t travel_client_count(double max_clients) {
  double length = isnan(max_clients) || max_clients <= 0 ? 0 : trunc(max_clients);
  if (length > 4294967295.0) qa_range_error("Invalid array length");
  return (size_t)length;
}
typedef struct travel_op {
  qa_compat_q2_classic_guest_host *h; const char *filename; bool loading; void (*next)(void *); void *next_ctx;
  qa_raw_entity_view *clients; int32_t *inuse; size_t n;
} travel_op;
static void travel_fn(void *ctx) {
  travel_op *t = ctx; qa_compat_q2_classic_guest_host *h = t->h;
  for (size_t i = 0; i < t->n; i++) qa_compat_q2_view_set_i32((uint8_t *)t->clients[i].bytes.bytes, 88, 0);
  if (!t->loading) { qa_compat_q2_classic_guest_host_save(h, "WriteLevel", t->filename, false); return; }
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  const char *texts[] = {t->filename};
  call_op_t o = {h, "WriteLevel", spawn_budget(h), t->next, t->next_ctx, true};
  with_strings(h, texts, 1, do_call, &o);
}
static void travel_reconcile_fn(void *ctx) { qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(ctx)); }
static void travel_free(void *arg) { travel_op *t = arg; free(t->clients); free(t->inuse); }
/* TS writeTravelLevel / writeTravelLevelLoading: the clients' inuse words and #suppressReconcile are restored in a
 * finally, the edicts are reconciled even after a failed save, and both failures together are one AggregateError. */
static void write_travel(qa_compat_q2_classic_guest_host *h, const char *filename, double max_clients, bool loading,
                         void (*next_frame)(void *), void *next_ctx) {
  if (!h->initialized || h->options.runner->depth(h->options.runner->ctx) != 0 || h->suppress_reconcile)
    qa_raise("Error", 0, 0, "API 3 travel save requires an idle initialized module");
  travel_op t; memset(&t, 0, sizeof t); t.h = h; t.filename = filename; t.loading = loading; t.next = next_frame; t.next_ctx = next_ctx;
  t.n = travel_client_count(max_clients);
  t.clients = malloc((t.n ? t.n : 1) * sizeof *t.clients); t.inuse = malloc((t.n ? t.n : 1) * sizeof *t.inuse);
  if (!t.clients || !t.inuse) { travel_free(&t); qa_range_error("API 3 host allocation failed"); }
  qa_cleanup node; qa_cleanup_push(&node, travel_free, &t);
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_guest_host_get_edicts(h);
  for (size_t i = 0; i < t.n; i++) t.clients[i] = qa_compat_q2_classic_edicts_at(edicts, (double)i + 1);
  for (size_t i = 0; i < t.n; i++) t.inuse[i] = qa_compat_q2_view_i32((const uint8_t *)t.clients[i].bytes.bytes, 88);
  h->suppress_reconcile = true;
  qa_caught_error failure; const bool saved = qa_try(travel_fn, &t, &failure);
  for (size_t i = 0; i < t.n; i++) qa_compat_q2_view_set_i32((uint8_t *)t.clients[i].bytes.bytes, 88, t.inuse[i]);
  h->suppress_reconcile = false;
  qa_cleanup_pop(&node); travel_free(&t);
  qa_caught_error second; const bool reconciled = qa_try(travel_reconcile_fn, h, &second);
  if (!reconciled) {
    if (!saved) qa_raise("AggregateError", 0, 0, "Travel WriteLevel and reconciliation failed");
    qa_rethrow(&second);
  }
  if (!saved) qa_rethrow(&failure);
}
void qa_compat_q2_classic_guest_host_write_travel_level(qa_compat_q2_classic_guest_host *h, const char *filename, double max_clients) {
  write_travel(h, filename, max_clients, false, NULL, NULL);
}
void qa_compat_q2_classic_guest_host_write_travel_level_loading(qa_compat_q2_classic_guest_host *h, const char *filename, double max_clients,
                                                                void (*next_frame)(void *), void *next_ctx) {
  write_travel(h, filename, max_clients, true, next_frame, next_ctx);
}
void qa_compat_q2_classic_guest_host_run_frame(qa_compat_q2_classic_guest_host *h) {
  (void)qa_compat_q2_classic_guest_host_call(h, "RunFrame", NULL, 0, NULL, h->options.instruction_budget);
}
/* The scratch-buffer client calls: allocate; try { ... } finally { unmap }. */
typedef struct client_op {
  qa_compat_q2_classic_guest_host *h; const char *name; double slot; const char *userinfo; qa_guest_address buffer;
  bool allowed; char *text;
} client_op;
static void client_userinfo_fn(void *ctx) {
  client_op *c = ctx; qa_compat_q2_classic_guest_host *h = c->h;
  char *fitted = qa_compat_q2_fit_classic_userinfo(c->userinfo, 512);
  qa_cleanup node; qa_cleanup_push(&node, free, fitted);
  qa_compat_q2_write_classic_string(h->memory, c->buffer, fitted, 512);
  qa_cleanup_pop(&node); free(fitted);
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(h), c->slot);
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, record.address), qa_compat_q2_call_pointer(true, c->buffer)};
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_call(h, c->name, args, 2, &record, h->options.instruction_budget);
  if (strcmp(c->name, "ClientConnect") == 0) {
    if (result.kind != QA_GUEST_VALUE_INT32) qa_raise("Error", 0, 0, "API 3 ClientConnect returned a non-integer result");
    c->allowed = result.number_value != 0;
  }
  /* readClassicString's latin1 text, held as the UTF-8 of that TS string like every other userinfo here. */
  char *bytes = qa_compat_q2_read_classic_string(h->memory, true, c->buffer, 512);
  qa_cleanup bytes_node; qa_cleanup_push(&bytes_node, free, bytes);
  c->text = qa_command_text_to_utf8(bytes);
  qa_cleanup_pop(&bytes_node); free(bytes);
}
static void client_userinfo_call(client_op *c, const char *label) {
  qa_compat_q2_classic_guest_host *h = c->h;
  c->buffer = h->memory->allocate(h->memory->ctx, 516, false, 0, label);
  qa_caught_error caught; const bool completed = qa_try(client_userinfo_fn, c, &caught);
  h->memory->unmap(h->memory->ctx, c->buffer, 516);
  if (!completed) qa_rethrow(&caught);
}
qa_compat_q2_classic_client_connect qa_compat_q2_classic_guest_host_client_connect(qa_compat_q2_classic_guest_host *h, double slot, const char *userinfo) {
  client_op c; memset(&c, 0, sizeof c); c.h = h; c.name = "ClientConnect"; c.slot = slot; c.userinfo = userinfo;
  client_userinfo_call(&c, "API 3 mutable userinfo");
  qa_compat_q2_classic_client_connect out = {c.allowed, c.text};
  return out;
}
void qa_compat_q2_classic_guest_host_client_event(qa_compat_q2_classic_guest_host *h, const char *name, double slot) {
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(h), slot);
  qa_guest_call_value v = qa_compat_q2_call_pointer(true, record.address);
  (void)qa_compat_q2_classic_guest_host_call(h, name, &v, 1, &record, h->options.instruction_budget);
}
char *qa_compat_q2_classic_guest_host_client_userinfo_changed(qa_compat_q2_classic_guest_host *h, double slot, const char *userinfo) {
  client_op c; memset(&c, 0, sizeof c); c.h = h; c.name = "ClientUserinfoChanged"; c.slot = slot; c.userinfo = userinfo;
  client_userinfo_call(&c, "API 3 mutable userinfo change");
  return c.text;
}
static void client_think_fn(void *ctx) {
  client_op *c = ctx; qa_compat_q2_classic_guest_host *h = c->h;
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(h), c->slot);
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, record.address), qa_compat_q2_call_pointer(true, c->buffer)};
  (void)qa_compat_q2_classic_guest_host_call(h, "ClientThink", args, 2, &record, h->options.instruction_budget);
}
void qa_compat_q2_classic_guest_host_client_think(qa_compat_q2_classic_guest_host *h, double slot, const uint8_t *command, size_t length) {
  if (length != 16) qa_range_error("API 3 usercmd_t requires 16 source bytes");
  client_op c; memset(&c, 0, sizeof c); c.h = h; c.slot = slot;
  c.buffer = h->memory->allocate(h->memory->ctx, 16, false, 0, "API 3 usercmd_t");
  h->memory->write(h->memory->ctx, c.buffer, command, 16);
  qa_caught_error caught; const bool completed = qa_try(client_think_fn, &c, &caught);
  h->memory->unmap(h->memory->ctx, c.buffer, 16);
  if (!completed) qa_rethrow(&caught);
}
typedef struct save_op { qa_compat_q2_classic_guest_host *h; const char *name; bool autosave; } save_op;
static void save_call(void *ctx, const qa_guest_call_value *vals, size_t n) {
  save_op *o = ctx; (void)n;
  qa_guest_call_value both[2] = {vals[0], qa_compat_q2_call_int32(o->autosave ? 1 : 0)};
  (void)qa_compat_q2_classic_guest_host_call(o->h, o->name, both, strcmp(o->name, "WriteGame") == 0 ? 2 : 1, NULL, o->h->options.instruction_budget);
}
void qa_compat_q2_classic_guest_host_save(qa_compat_q2_classic_guest_host *h, const char *name, const char *filename, bool autosave) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  if (strcmp(name, "ReadGame") == 0 || strcmp(name, "ReadLevel") == 0) reloaded(h);
  if (strcmp(name, "ReadGame") == 0 || strcmp(name, "ReadLevel") == 0) release_provider_actors(h);
  const char *texts[] = {filename};
  save_op o = {h, name, autosave};
  with_strings(h, texts, 1, save_call, &o); /* allocate; try { call } finally { unmap } */
}
void qa_compat_q2_classic_guest_host_trace_bytes(qa_compat_q2_classic_guest_host *h, qa_trace_result trace, uint8_t out[56]) {
  if (!trace.kind || strcmp(trace.kind, "q2") != 0) qa_raise("TypeError", 0, 0, "API 3 trace requires the shared Q2 trace contract");
  memset(out, 0, 56);
  qa_compat_q2_view_set_i32(out, 0, trace.all_solid ? 1 : 0); qa_compat_q2_view_set_i32(out, 4, trace.start_solid ? 1 : 0);
  qa_compat_q2_view_set_f32(out, 8, trace.fraction);
  qa_compat_q2_view_set_f32(out, 12, trace.end.x); qa_compat_q2_view_set_f32(out, 16, trace.end.y); qa_compat_q2_view_set_f32(out, 20, trace.end.z);
  qa_compat_q2_view_set_f32(out, 24, trace.source_plane.plane.normal.x); qa_compat_q2_view_set_f32(out, 28, trace.source_plane.plane.normal.y);
  qa_compat_q2_view_set_f32(out, 32, trace.source_plane.plane.normal.z); qa_compat_q2_view_set_f32(out, 36, trace.source_plane.plane.distance);
  out[40] = qa_js_to_uint8(trace.source_plane.type); out[41] = qa_js_to_uint8(trace.source_plane.signbits);
  qa_compat_q2_view_set_i32(out, 48, qa_js_to_int32(trace.contents));
  /* cmodel.c's nullsurface: the DLLs dereference trace.surface unconditionally, so it is never NULL. */
  static const qa_q2_surface_info null_surface = {"", 0, 0, NULL};
  if (!trace.surface) trace.surface = &null_surface;
  {
    /* JSON.stringify([name, flags, value]) keys the Map; any unambiguous spelling of the triple will do, at full length. */
    const char *surface_name = trace.surface->name ? trace.surface->name : "";
    size_t key_size = strlen(surface_name) + 96; char *key = malloc(key_size);
    if (!key) qa_range_error("API 3 host allocation failed");
    snprintf(key, key_size, "%s|%s|%s", surface_name, QA_JS_NUMBER(trace.surface->flags), QA_JS_NUMBER(trace.surface->value));
    qa_cleanup key_node; qa_cleanup_push(&key_node, free, key);
    qa_guest_address address; bool found = false; memset(&address, 0, sizeof address);
    for (size_t i = 0; i < h->surface_count && !found; i++) if (strcmp(h->surfaces[i].key, key) == 0) { address = h->surfaces[i].address; found = true; }
    if (!found) {
      address = h->memory->allocate(h->memory->ctx, 24, false, 0, "API 3 csurface_t");
      uint8_t raw[24]; memset(raw, 0, 24);
      const char *nm = trace.surface->name ? trace.surface->name : "";
      for (size_t i = 0; i < 16 && nm[i]; i++) raw[i] = (uint8_t)nm[i];
      qa_compat_q2_view_set_i32(raw, 16, qa_js_to_int32(trace.surface->flags)); qa_compat_q2_view_set_i32(raw, 20, qa_js_to_int32(trace.surface->value));
      h->memory->write(h->memory->ctx, address, raw, 24);
      if (h->surface_count == h->surface_cap) {
        size_t cap = h->surface_cap ? h->surface_cap * 2 : 16; surface_rec_t *grown = realloc(h->surfaces, cap * sizeof *grown);
        if (!grown) qa_range_error("API 3 host allocation failed");
        h->surfaces = grown; h->surface_cap = cap;
      }
      qa_cleanup_pop(&key_node);
      h->surfaces[h->surface_count++] = (surface_rec_t){key, address};
    } else { qa_cleanup_pop(&key_node); free(key); }
    /* view.setUint32(44, Number(address.byteOffset)) */
    uint32_t u = (uint32_t)address.byte_offset;
    out[44] = (uint8_t)u; out[45] = (uint8_t)(u >> 8); out[46] = (uint8_t)(u >> 16); out[47] = (uint8_t)(u >> 24);
  }
  int64_t ent = 0;
  /* SV_Trace reports the world edict when nothing was hit. */
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR) ent = qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(h), 0).address.byte_offset;
  else ent = qa_compat_q2_classic_edicts_pointer(qa_compat_q2_classic_guest_host_get_edicts(h), trace.hit.actor).byte_offset;
  uint32_t eu = (uint32_t)ent; out[52] = (uint8_t)eu; out[53] = (uint8_t)(eu >> 8); out[54] = (uint8_t)(eu >> 16); out[55] = (uint8_t)(eu >> 24);
}
void qa_compat_q2_classic_guest_host_link_entity(qa_compat_q2_classic_guest_host *h, qa_guest_address address) {
  /* TS `this.edicts`: an import reached before GetGameAPI returned raises instead of dereferencing nothing. */
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_guest_host_get_edicts(h);
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(edicts, address);
  qa_owned_actor actor;
  if (!qa_compat_q2_classic_edicts_observe(edicts, address, &actor) || record.slot == 0) return;
  qa_q2_foundation_host *engine = h->options.services.engine;
  qa_shared_body_table_unlink(engine->bodies, actor);
  qa_compat_q2_memory *mem = h->memory;
  qa_vec3 origin = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 4));
  qa_vec3 angles = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 16));
  qa_vec3 mins = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 188));
  qa_vec3 maxs = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 200));
  uint8_t *view = (uint8_t *)record.bytes.bytes;
  int32_t solid = qa_compat_q2_view_i32(view, 248), flags = qa_compat_q2_view_i32(view, 184);
  if (solid < 0 || solid > 3) qa_range_error("Invalid API 3 solid_t");
  /* clamp(value,maximum) = max(1,min(maximum,trunc(value))) */
  int packed = 0;
  if (solid == 2 && (flags & 2) == 0) {
    /* NaN guest bounds give a NaN clamp; `c1 | (c2 << 5)` applies ToInt32 (0). */
    int c1 = qa_js_to_int32(qa_js_max(1, qa_js_min(31, trunc(maxs.x / 8)))), c2 = qa_js_to_int32(qa_js_max(1, qa_js_min(31, trunc(-mins.z / 8)))), c3 = qa_js_to_int32(qa_js_max(1, qa_js_min(63, trunc((maxs.z + 32) / 8))));
    packed = c1 | (c2 << 5) | (c3 << 10);
  } else if (solid == 3) packed = 31;
  qa_compat_q2_view_set_i32(view, 72, packed);
  bool has_radius = solid == 3 && (angles.x != 0 || angles.y != 0 || angles.z != 0);
  double radius = has_radius ? qa_js_max(qa_js_max(fabs(mins.x), fabs(mins.y)), qa_js_max(fabs(mins.z), qa_js_max(fabs(maxs.x), qa_js_max(fabs(maxs.y), fabs(maxs.z))))) : 0;
  qa_bounds bounds;
  bounds.min = (qa_vec3){(float)((float)(origin.x + (has_radius ? -radius : mins.x)) - 1), (float)((float)(origin.y + (has_radius ? -radius : mins.y)) - 1), (float)((float)(origin.z + (has_radius ? -radius : mins.z)) - 1)};
  bounds.max = (qa_vec3){(float)((float)(origin.x + (has_radius ? radius : maxs.x)) + 1), (float)((float)(origin.y + (has_radius ? radius : maxs.y)) + 1), (float)((float)(origin.z + (has_radius ? radius : maxs.z)) + 1)};
  qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 212), bounds.min);
  qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 224), bounds.max);
  qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 236), (qa_vec3){maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z});
  qa_compat_q2_classic_world_link link = h->options.services.world_link(h->options.services.ctx, bounds);
  qa_compat_q2_view_set_i32(view, 104, (link.clusters_null || link.cluster_count > 16) ? -1 : qa_js_to_int32(link.cluster_count));
  qa_compat_q2_view_set_i32(view, 172, qa_js_to_int32(link.headnode));
  qa_compat_q2_view_set_i32(view, 176, qa_js_to_int32(link.areas[0])); qa_compat_q2_view_set_i32(view, 180, qa_js_to_int32(link.areas[1]));
  size_t cn = link.cluster_count < 16 ? link.cluster_count : 16;
  for (size_t i = 0; i < cn; i++) qa_compat_q2_view_set_i32(view, 108 + i * 4, qa_js_to_int32(link.clusters[i]));
  if (qa_compat_q2_view_i32(view, 92) == 0) qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 28), origin);
  /* view.setInt32(92, linkcount + 1): the guest's word wraps instead of overflowing. */
  qa_compat_q2_view_set_i32(view, 92, (int32_t)(qa_compat_q2_view_u32(view, 92) + 1u));
  const char *mn = model_name(h, qa_compat_q2_view_i32(view, 40));
  bool inline_model = mn && mn[0] == '*' && mn[1];
  if (inline_model) { for (const char *p = mn + 1; *p; p++) if (*p < '0' || *p > '9') inline_model = false; }
  if (solid == 3 && !inline_model) qa_raise("Error", 0, 0, "API 3 solid brush has no registered inline model");
  double model = inline_model ? atof(mn + 1) : 0;
  engine->set_solid(engine->ctx, actor, solid == 0 ? "none" : solid == 1 ? "trigger" : solid == 2 ? "box" : "brush", solid == 3 ? &model : NULL);
  if (solid != 0) {
    if (!h->options.services.link_body) qa_shared_body_table_link(engine->bodies, actor, NULL);
    else h->options.services.link_body(h->options.services.link_ctx, record, actor);
  }
}
typedef struct import_strings { char *first, *second; } import_strings;
static void import_strings_free(void *arg) { import_strings *s = arg; free(s->first); free(s->second); s->first = s->second = NULL; }
/* TS string(index): readClassicString(memory, pointer(index)). */
static char *import_string(qa_compat_q2_memory *memory, const qa_guest_call_value *args, size_t count, size_t index) {
  qa_guest_address a; memset(&a, 0, sizeof a);
  bool has = qa_compat_q2_classic_pointer(args, count, index, &a);
  return qa_compat_q2_read_classic_string(memory, has, a, 65536);
}
qa_guest_call_result qa_compat_q2_classic_guest_host_import_call(qa_compat_q2_classic_guest_host *h, const char *name,
                                                                 const qa_guest_call_context *context, const qa_guest_call_value *args, size_t count) {
  void * value4;
  qa_vec3 value5;
  qa_vec3 value6;
  void * value;
  qa_vec3 value2;
  qa_vec3 value3;
  qa_compat_q2_classic_engine_services *services = &h->options.services;
  qa_compat_q2_memory *memory = h->memory;
  qa_guest_call_result void_r = qa_compat_q2_call_void();
  if (!strcmp(name, "bprintf") || !strcmp(name, "dprintf") || !strcmp(name, "cprintf") || !strcmp(name, "centerprintf") || !strcmp(name, "error")) {
    size_t offset = (!strcmp(name, "bprintf") || !strcmp(name, "centerprintf")) ? 1 : !strcmp(name, "cprintf") ? 2 : 0;
    qa_guest_address fa; bool has = qa_compat_q2_classic_pointer(args, count, offset, &fa);
    char *fmt = qa_compat_q2_read_classic_string(memory, has, fa, 65536);
    char *text = qa_compat_q2_classic_printf(memory, fmt, args + offset + 1, count - offset - 1);
    free(fmt);
    if (!strcmp(name, "error")) {
      /* throw new Error(`API 3 game error: ${text}`): the raise formats the whole text before the cleanup frees it. */
      qa_cleanup node; qa_cleanup_push(&node, free, text);
      qa_raise("Error", 0, 0, "API 3 game error: %s", text);
    }
    bool has_ent = false; qa_guest_address ent; memset(&ent, 0, sizeof ent);
    if (!strcmp(name, "cprintf") || !strcmp(name, "centerprintf")) has_ent = qa_compat_q2_classic_pointer(args, count, 0, &ent);
    double level = !strcmp(name, "bprintf") ? qa_compat_q2_classic_number(args, count, 0) : !strcmp(name, "cprintf") ? qa_compat_q2_classic_number(args, count, 1) : 0;
    services->print(services->ctx, !strcmp(name, "dprintf") ? "debug" : !strcmp(name, "bprintf") ? "broadcast" : !strcmp(name, "cprintf") ? "client" : "center", has_ent, ent, level, text);
    free(text); return void_r;
  }
  if (!strcmp(name, "TagMalloc")) {
    double requested = qa_compat_q2_classic_number(args, count, 0), tag = qa_compat_q2_classic_number(args, count, 1);
    if (!isfinite(requested) || requested != trunc(requested) || requested < 0)
      qa_range_error("TagMalloc size must be a non-negative integer");
    double bytes = qa_compat_q2_native_allocation_bytes(requested);
    char label[128]; snprintf(label, sizeof label, "API 3 tag %s", QA_JS_NUMBER(tag));
    qa_guest_address a = qa_compat_q2_allocate_native_memory(memory, requested, label);
    /* this.#allocations.set(address.byteOffset, ...): a key already present keeps its place and takes the new record. */
    for (size_t i = 0; i < h->alloc_count; i++) if (h->allocations[i].key == a.byte_offset) {
      h->allocations[i] = (alloc_rec_t){a.byte_offset, a, bytes, tag};
      return qa_compat_q2_call_pointer(true, a);
    }
    if (h->alloc_count == h->alloc_cap) {
      size_t cap = h->alloc_cap ? h->alloc_cap * 2 : 16; alloc_rec_t *grown = realloc(h->allocations, cap * sizeof *grown);
      if (!grown) qa_range_error("API 3 host allocation failed");
      h->allocations = grown; h->alloc_cap = cap;
    }
    h->allocations[h->alloc_count++] = (alloc_rec_t){a.byte_offset, a, bytes, tag};
    return qa_compat_q2_call_pointer(true, a);
  }
  if (!strcmp(name, "TagFree")) {
    qa_guest_address a = qa_compat_q2_classic_required_pointer(args, count, 0);
    for (size_t i = 0; i < h->alloc_count; i++) if (h->allocations[i].key == a.byte_offset) {
      memory->unmap(memory->ctx, h->allocations[i].address, h->allocations[i].bytes);
      /* Keep the TS Map's insertion order: FreeTags unmaps in that order. */
      memmove(&h->allocations[i], &h->allocations[i + 1], (h->alloc_count - i - 1) * sizeof *h->allocations);
      h->alloc_count--; return void_r;
    }
    qa_raise("Error", 0, 0, "TagFree pointer is not a live API 3 allocation");
  }
  if (!strcmp(name, "FreeTags")) {
    double tag = qa_compat_q2_classic_number(args, count, 0);
    /* TS walks the Map in insertion order; the unmap order shapes later placements. */
    size_t kept = 0;
    for (size_t i = 0; i < h->alloc_count; i++) {
      if (h->allocations[i].tag == tag) memory->unmap(memory->ctx, h->allocations[i].address, h->allocations[i].bytes);
      else h->allocations[kept++] = h->allocations[i];
    }
    h->alloc_count = kept;
    return void_r;
  }
  /* String arguments, read where TS reads them. Both are freed on return and, under a catch, on a raise. */
  import_strings strings = {NULL, NULL}; qa_cleanup strings_node; qa_cleanup_push(&strings_node, import_strings_free, &strings);
#define s0 (strings.first)
#define s1 (strings.second)
  double configstring_index = 0;
  if (!strcmp(name, "configstring")) { configstring_index = qa_compat_q2_classic_number(args, count, 0); s1 = import_string(memory, args, count, 1); }
  else if (!strcmp(name, "cvar")) { s0 = import_string(memory, args, count, 0); s1 = import_string(memory, args, count, 1); }
  else if (!strcmp(name, "cvar_set") || !strcmp(name, "cvar_forceset") || !strcmp(name, "modelindex") || !strcmp(name, "soundindex") ||
           !strcmp(name, "imageindex") || !strcmp(name, "AddCommandString")) s0 = import_string(memory, args, count, 0);
  qa_guest_call_result result = void_r; bool handled = true;
  if (!strcmp(name, "cvar")) { qa_cvar_snapshot out; memset(&out, 0, sizeof out); qa_cvar_registry_register(services->cvars, s0, s1, qa_compat_q2_classic_number(args, count, 2), &out); qa_cvar_read_free(&out);
    qa_guest_address a; result = qa_compat_q2_call_pointer(qa_compat_q2_classic_cvars_pointer(h->cvars, s0, &a), a); }
  else if (!strcmp(name, "cvar_set") || !strcmp(name, "cvar_forceset")) {
    /* A guest may not overwrite the credentials it is not allowed to read. */
    qa_cvar_read existing; bool found = qa_cvar_registry_find(services->cvars, s0, &existing);
    bool writable = !found || (existing.flags & QA_Q2_CVAR_FLAG_PRIVATE) == 0; if (found) qa_cvar_read_free(&existing);
    /* The value is read only when the set runs: a refused private cvar never touches the guest's value pointer. */
    if (writable) { s1 = import_string(memory, args, count, 1); qa_cvar_snapshot out; memset(&out, 0, sizeof out); qa_cvar_registry_set(services->cvars, s0, s1, !strcmp(name, "cvar_forceset"), &out); qa_cvar_read_free(&out); }
    qa_guest_address a; result = qa_compat_q2_call_pointer(qa_compat_q2_classic_cvars_pointer(h->cvars, s0, &a), a); }
  else if (!strcmp(name, "configstring")) services->configstring(services->ctx, configstring_index, s1);
  else if (!strcmp(name, "modelindex") || !strcmp(name, "soundindex") || !strcmp(name, "imageindex")) {
    double index = services->resource_index(services->ctx, !strcmp(name, "modelindex") ? "model" : !strcmp(name, "soundindex") ? "sound" : "image", s0);
    if (!strcmp(name, "modelindex")) models_set(h, index, s0);
    result = qa_compat_q2_call_int32(index);
  }
  else if (!strcmp(name, "sound")) { qa_guest_address e; bool has = qa_compat_q2_classic_pointer(args, count, 0, &e);
    services->sound(services->ctx, NULL, has, e, qa_compat_q2_classic_number(args, count, 1), qa_compat_q2_classic_number(args, count, 2), qa_compat_q2_classic_number(args, count, 3), qa_compat_q2_classic_number(args, count, 4), qa_compat_q2_classic_number(args, count, 5)); }
  else if (!strcmp(name, "positioned_sound")) { qa_guest_address e; bool has = qa_compat_q2_classic_pointer(args, count, 1, &e);
    qa_guest_address o; bool haso = qa_compat_q2_classic_pointer(args, count, 0, &o); qa_vec3 origin = haso ? qa_compat_q2_read_classic_vector(memory, o) : (qa_vec3){0, 0, 0};
    services->sound(services->ctx, haso ? &origin : NULL, has, e, qa_compat_q2_classic_number(args, count, 2), qa_compat_q2_classic_number(args, count, 3), qa_compat_q2_classic_number(args, count, 4), qa_compat_q2_classic_number(args, count, 5), qa_compat_q2_classic_number(args, count, 6)); }
  else if (!strcmp(name, "pointcontents")) result = qa_compat_q2_call_int32(services->engine->point_contents(services->engine->ctx, qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0))));
  else if (!strcmp(name, "inPVS")) result = qa_compat_q2_call_int32((value4 = services->engine->ctx, value5 = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0)), value6 = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 1)), services->engine->in_pvs(value4, value5, value6)) ? 1 : 0);
  else if (!strcmp(name, "inPHS")) result = qa_compat_q2_call_int32((value = services->engine->ctx, value2 = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0)), value3 = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 1)), services->engine->in_phs(value, value2, value3)) ? 1 : 0);
  else if (!strcmp(name, "SetAreaPortalState")) services->engine->set_area_portal(services->engine->ctx, qa_compat_q2_classic_number(args, count, 0), qa_compat_q2_classic_number(args, count, 1) != 0);
  else if (!strcmp(name, "AreasConnected")) result = qa_compat_q2_call_int32(services->areas_connected(services->ctx, qa_compat_q2_classic_number(args, count, 0), qa_compat_q2_classic_number(args, count, 1)) ? 1 : 0);
  else if (!strcmp(name, "setmodel")) {
    qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(h), qa_compat_q2_classic_required_pointer(args, count, 0));
    s0 = import_string(memory, args, count, 1);
    double index = services->resource_index(services->ctx, "model", s0); models_set(h, index, s0);
    qa_compat_q2_view_set_i32((uint8_t *)record.bytes.bytes, 40, qa_js_to_int32(index));
    if (s0[0] == '*') {
      qa_bounds b = services->engine->inline_model_bounds(services->engine->ctx, qa_js_string_to_number(s0 + 1)); /* Number(model.slice(1)) */
      qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, record.address, 188), b.min);
      qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, record.address, 200), b.max);
      qa_compat_q2_classic_guest_host_link_entity(h, record.address);
    }
  }
  else if (!strcmp(name, "linkentity")) qa_compat_q2_classic_guest_host_link_entity(h, qa_compat_q2_classic_required_pointer(args, count, 0));
  else if (!strcmp(name, "unlinkentity")) { qa_owned_actor a; if (qa_compat_q2_classic_edicts_observe(qa_compat_q2_classic_guest_host_get_edicts(h), qa_compat_q2_classic_required_pointer(args, count, 0), &a)) qa_shared_body_table_unlink(services->engine->bodies, a); }
  else if (!strcmp(name, "BoxEdicts")) {
    qa_guest_address a2;
    qa_guest_address v;
    double maximum = qa_compat_q2_classic_number(args, count, 3), area = qa_compat_q2_classic_number(args, count, 4);
    if (maximum < 0 || (area != 1 && area != 2)) qa_range_error("BoxEdicts invalid area type or maximum count");
    qa_vec3 min2 = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0));
    qa_vec3 max2 = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 1));
    qa_bounds b = {min2, max2};
    size_t n = 0; const qa_actor_id *results = services->box_edicts(services->ctx, b, area == 1 ? "solid" : "trigger", &n);
    /* .slice(0, maximum): ToIntegerOrInfinity (NaN is 0). */
    { double limit = isnan(maximum) ? 0 : trunc(maximum); if ((double)n > limit) n = (size_t)limit; }
    /* required(2) is evaluated per result, so an empty result never looks at the list pointer. */
    for (size_t i = 0; i < n; i++)
      (a2 = qa_compat_q2_mem_offset(memory, qa_compat_q2_classic_required_pointer(args, count, 2), (int64_t)(i * 4)), v = qa_compat_q2_classic_edicts_pointer(qa_compat_q2_classic_guest_host_get_edicts(h), results[i]), qa_compat_q2_mem_write_pointer(memory, a2, true, v));
    result = qa_compat_q2_call_int32((double)n);
  }
  else if (!strcmp(name, "trace")) {
    qa_guest_address mins_a, maxs_a, pass_a; bool has_mins = qa_compat_q2_classic_pointer(args, count, 1, &mins_a), has_maxs = qa_compat_q2_classic_pointer(args, count, 2, &maxs_a), has_pass = qa_compat_q2_classic_pointer(args, count, 4, &pass_a);
    qa_owned_actor ignored; bool has_ignored = has_pass && qa_compat_q2_classic_edicts_observe(qa_compat_q2_classic_guest_host_get_edicts(h), pass_a, &ignored);
    qa_bounds b = {{0, 0, 0}, {0, 0, 0}};
    if (has_mins) b.min = qa_compat_q2_read_classic_vector(memory, mins_a);
    if (has_maxs) b.max = qa_compat_q2_read_classic_vector(memory, maxs_a);
    qa_q2_trace_request req; memset(&req, 0, sizeof req);
    req.start = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0));
    req.end = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 3));
    req.bounds = (!has_mins && !has_maxs) ? NULL : &b; req.ignore = has_ignored ? &ignored.id : NULL;
    req.mask = qa_compat_q2_classic_number(args, count, 5);
    qa_trace_result tr = services->engine->trace(services->engine->ctx, req);
    qa_guest_call_value agg; memset(&agg, 0, sizeof agg); agg.kind = QA_GUEST_VALUE_AGGREGATE; agg.layout = qa_compat_q2_classic_trace_layout;
    qa_compat_q2_classic_guest_host_trace_bytes(h, tr, h->trace_result);
    agg.bytes.bytes = h->trace_result; agg.bytes.length = 56; result = agg;
  }
  else if (!strcmp(name, "Pmove")) services->pmove(services->pmove_ctx ? services->pmove_ctx : services->ctx, qa_compat_q2_classic_required_pointer(args, count, 0), h);
  else if (!strcmp(name, "argc")) { size_t n = 0; const char *tail; (void)services->command(services->ctx, &n, &tail); result = qa_compat_q2_call_int32((double)n); }
  else if (!strcmp(name, "argv") || !strcmp(name, "args")) {
    size_t n = 0; const char *tail; const char *const *argv = services->command(services->ctx, &n, &tail);
    /* `${command.arguments.join("\0")}\0\0${command.args}` keys the reusable strings of one command. */
    size_t key_length = 2 + strlen(tail);
    for (size_t i = 0; i < n; i++) key_length += strlen(argv[i]) + (i ? 1 : 0);
    char *key = malloc(key_length + 1);
    if (!key) qa_range_error("API 3 host allocation failed");
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
      if (i) key[at++] = '\0';
      memcpy(key + at, argv[i], strlen(argv[i])); at += strlen(argv[i]);
    }
    key[at++] = '\0'; key[at++] = '\0';
    memcpy(key + at, tail, strlen(tail)); at += strlen(tail);
    key[at] = '\0';
    const char *text = tail;
    if (!strcmp(name, "argv")) {
      double idx = qa_compat_q2_classic_number(args, count, 0);
      text = (idx >= 0 && idx == trunc(idx) && (size_t)idx < n) ? argv[(size_t)idx] : "";
    }
    result = qa_compat_q2_call_pointer(true, qa_compat_q2_command_guest_strings_get(h->command_strings, key, at, text));
    free(key);
  }
  else if (!strcmp(name, "AddCommandString")) {
    qa_compat_q2_guest_console_filter filtered = qa_compat_q2_filter_guest_console(s0);
    if (filtered.refused_count != 0) {
      char *list = qa_compat_q2_guest_console_refused_list(&filtered);
      size_t size = strlen(list) + 64;
      char *message = malloc(size);
      if (!message) qa_range_error("API 3 host allocation failed");
      snprintf(message, size, "game module console command refused: %s\n", list);
      services->print(services->ctx, "debug", false, (qa_guest_address){0}, 0, message);
      free(message); free(list);
    }
    if (filtered.text[0] != '\0') services->add_command(services->ctx, filtered.text);
    qa_compat_q2_guest_console_filter_free(&filtered);
  }
  else if (!strcmp(name, "DebugGraph")) services->debug_graph(services->ctx, qa_compat_q2_classic_number(args, count, 0), qa_compat_q2_classic_number(args, count, 1));
  else if (!strcmp(name, "multicast") || !strcmp(name, "unicast") || !strncmp(name, "Write", 5)) services->message(services->ctx, name, args, count, context);
  else handled = false;
  qa_cleanup_pop(&strings_node); import_strings_free(&strings);
#undef s0
#undef s1
  if (!handled) qa_raise("Error", 0, 0, "Unsupported API 3 engine import %s", name);
  return result;
}
