/* Port of src/app/bootstrap/network/unified-component-publication.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_component_publication.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
/* The previous publications are kept as copies of what the next projection compares: TS holds
 * the previous arrays by reference, while the host's strings and HUD frames (layout,
 * inventory, configstrings) are rebuilt or released between frames. */
typedef struct retained {
  void **blocks; size_t count, cap;
} retained;
struct qa_app_unified_component_publisher {
  double revision;
  qa_app_unified_native_publication *native; size_t native_count;
  qa_app_unified_component_publication *sources; size_t source_count;
  retained kept;
  struct { char *provider; double sequence; } *sequences;
  size_t sequence_count, sequence_cap;
};
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static void retained_free(retained *kept) {
  for (size_t i = 0; i < kept->count; i++) free(kept->blocks[i]);
  free(kept->blocks);
  memset(kept, 0, sizeof *kept);
}
static void retained_cleanup(void *arg) { retained_free(arg); }
static void *keep_block(retained *kept, const void *data, size_t size) {
  if (kept->count == kept->cap) {
    size_t cap = kept->cap ? kept->cap * 2 : 16;
    void **blocks = realloc(kept->blocks, cap * sizeof *blocks);
    if (!blocks) qa_range_error("out of memory");
    kept->blocks = blocks;
    kept->cap = cap;
  }
  void *copy = xmalloc(size);
  if (size) memcpy(copy, data, size);
  kept->blocks[kept->count++] = copy;
  return copy;
}
static const char *keep_text(retained *kept, const char *text) {
  return text ? keep_block(kept, text, strlen(text) + 1) : NULL;
}
static qa_mod_identity keep_identity(retained *kept, const qa_mod_identity *identity) {
  qa_mod_identity out = *identity;
  out.selection.product = keep_text(kept, identity->selection.product);
  out.selection.id = keep_text(kept, identity->selection.id);
  out.source.provider = keep_text(kept, identity->source.provider);
  out.source.content = keep_text(kept, identity->source.content);
  out.declaration_digest = keep_text(kept, identity->declaration_digest);
  qa_module_identity *modules = keep_block(kept, identity->modules, identity->module_count * sizeof *modules);
  for (size_t i = 0; i < identity->module_count; i++) {
    modules[i].id = keep_text(kept, modules[i].id);
    modules[i].artifact_path = keep_text(kept, modules[i].artifact_path);
    modules[i].digest = keep_text(kept, modules[i].digest);
    modules[i].revision = keep_text(kept, modules[i].revision);
  }
  out.modules = modules;
  qa_provider_checkpoint_ref *providers = keep_block(kept, identity->providers, identity->provider_count * sizeof *providers);
  for (size_t i = 0; i < identity->provider_count; i++) {
    providers[i].provider = keep_text(kept, providers[i].provider);
    providers[i].schema = keep_text(kept, providers[i].schema);
  }
  out.providers = providers;
  return out;
}
/* A component publication as the next projection reads it: owner, generation, identity, ABI,
 * runtime, viewer and configstring revision. */
static qa_app_unified_component_publication keep_source(retained *kept, const qa_app_unified_component_publication *source) {
  qa_app_unified_component_publication out = *source;
  out.identity.owner.provider = keep_text(kept, source->identity.owner.provider);
  out.identity.identity = keep_identity(kept, &source->identity.identity);
  out.identity.identity_wire = NULL;
  out.identity.abi = keep_text(kept, source->identity.abi);
  out.identity.runtime = keep_text(kept, source->identity.runtime);
  out.game_state = NULL;
  out.scene = NULL;
  out.bindings = NULL;
  out.binding_count = 0;
  return out;
}
/* A native publication as projectNativeComponents reads it: owner, generation, identity,
 * viewer and the HUD fields it compares. */
static qa_app_unified_native_publication keep_native(retained *kept, const qa_app_unified_native_publication *source) {
  qa_app_unified_native_publication out = *source;
  out.identity.owner.provider = keep_text(kept, source->identity.owner.provider);
  out.identity.identity = keep_identity(kept, &source->identity.identity);
  out.identity.identity_wire = NULL;
  out.frame.qvm_hud_mode = NULL;
  out.frame.native_hud_mode = keep_text(kept, source->frame.native_hud_mode);
  qa_native_q2_hud_frame *hud = &out.frame.native_hud_frame;
  const qa_native_q2_hud_frame *from = &source->frame.native_hud_frame;
  hud->protocol.kind = keep_text(kept, from->protocol.kind);
  hud->stats = NULL;
  hud->stat_count = 0;
  hud->layout = keep_text(kept, from->layout);
  hud->inventory = keep_block(kept, from->inventory, from->inventory_count * sizeof *hud->inventory);
  qa_native_q2_config_string *configs = keep_block(kept, from->configstrings, from->configstring_count * sizeof *configs);
  for (size_t i = 0; i < from->configstring_count; i++) configs[i].value = keep_text(kept, configs[i].value);
  hud->configstrings = configs;
  return out;
}
static bool actor_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token
      && a.session.token == b.session.token;
}
qa_app_unified_component_publisher *qa_app_unified_component_publisher_create(void) {
  return calloc(1, sizeof(qa_app_unified_component_publisher));
}
void qa_app_unified_component_publisher_destroy(qa_app_unified_component_publisher *publisher) {
  if (!publisher) return;
  free(publisher->native);
  free(publisher->sources);
  retained_free(&publisher->kept);
  for (size_t i = 0; i < publisher->sequence_count; i++) free(publisher->sequences[i].provider);
  free(publisher->sequences);
  free(publisher);
}
void qa_app_unified_component_projection_free(qa_app_unified_component_projection *projection) {
  if (projection->has_update) {
    /* States borrow game-state records and command strings from the
     * publications; free only the shells (not update_free, which
     * would destroy borrowed records). */
    for (size_t i = 0; i < projection->update.source_count; i++)
      free((void *)projection->update.sources[i].commands);
    free(projection->update.sources);
    free(projection->update.native);
  }
  for (size_t i = 0; i < projection->frame.source_count; i++) {
    free(projection->frame.sources[i].bindings);
    free((void *)projection->frame.sources[i].scene.snapshot.entities);
  }
  free(projection->frame.sources);
  /* Projected native frames borrow their stats from the publications (the host's frame rows):
   * only the array is ours (qa_app_unified_native_frames_free is for decoded frames). */
  free(projection->frame.native);
  memset(projection, 0, sizeof *projection);
}
static double *sequence_slot(qa_app_unified_component_publisher *p, const char *provider) {
  for (size_t i = 0; i < p->sequence_count; i++)
    if (strcmp(p->sequences[i].provider, provider) == 0) return &p->sequences[i].sequence;
  return NULL;
}
static void sequence_set(qa_app_unified_component_publisher *p, const char *provider, double value) {
  double *slot = sequence_slot(p, provider);
  if (slot) { *slot = value; return; }
  if (p->sequence_count == p->sequence_cap) {
    size_t cap = p->sequence_cap ? p->sequence_cap * 2 : 8;
    void *grown = realloc(p->sequences, cap * sizeof *p->sequences);
    if (!grown) qa_range_error("out of memory");
    p->sequences = grown;
    p->sequence_cap = cap;
  }
  /* The Map key outlives the host's string. */
  size_t length = strlen(provider) + 1;
  char *copy = xmalloc(length);
  memcpy(copy, provider, length);
  p->sequences[p->sequence_count].provider = copy;
  p->sequences[p->sequence_count].sequence = value;
  p->sequence_count++;
}
static void projection_parts_free(void *arg) {
  qa_app_unified_native_projection *projected = arg;
  free(projected->states);
  free(projected->frames);
}
/* The states built so far, released if a publication check raises midway. */
typedef struct states_owned { qa_app_unified_component_state *states; size_t count; } states_owned;
static void states_owned_free(void *arg) {
  states_owned *owned = arg;
  for (size_t i = 0; i < owned->count; i++) free(owned->states[i].commands);
  free(owned->states);
}
qa_app_unified_component_projection qa_app_unified_component_publisher_project(
    qa_app_unified_component_publisher *publisher,
    const qa_app_unified_component_publication *sources, size_t source_count,
    const qa_app_unified_native_publication *native, size_t native_count) {
  bool changed = source_count != publisher->source_count;
  if (!changed)
    for (size_t i = 0; i < source_count; i++)
      if (!qa_same_presentation_owner(&publisher->sources[i].identity.owner,
              sources[i].identity.owner)) changed = true;
  qa_app_unified_component_state *states = xmalloc(source_count * sizeof *states);
  states_owned owned = {states, 0};
  qa_cleanup states_guard;
  qa_cleanup_push(&states_guard, states_owned_free, &owned);
  for (size_t i = 0; i < source_count; i++) {
    const qa_app_unified_component_publication *source = &sources[i];
    const qa_app_unified_component_publication *old = NULL;
    for (size_t j = 0; j < publisher->source_count; j++)
      if (strcmp(publisher->sources[j].identity.owner.provider,
              source->identity.owner.provider) == 0) old = &publisher->sources[j];
    bool same = old && qa_same_presentation_owner(&source->identity.owner, old->identity.owner)
        && source->identity.generation == old->identity.generation;
    if (same && (!qa_same_mod_identity(&source->identity.identity, &old->identity.identity)
            || strcmp(source->identity.abi, old->identity.abi) != 0
            || strcmp(source->identity.runtime, old->identity.runtime) != 0
            || !actor_equal(source->viewer, old->viewer)))
      qa_raise("Error", 0, 0, "Component activation changed identity or recipient");
    const qa_qvm_scene_context *scene = source->scene;
    double sequence = scene ? (double)scene->snapshot.server_command_sequence : 0;
    double *stored = sequence_slot(publisher, source->identity.owner.provider);
    /* TS same ? (sequences.get(provider) ?? 0) : sequence. */
    double base = same ? (stored ? *stored : 0) : sequence;
    if (sequence < base) qa_raise("Error", 0, 0, "Original component command sequence moved backward");
    /* TS commands.filter(command => command.sequence > base). */
    size_t available = scene ? scene->command_count : 0;
    size_t command_count = 0;
    for (size_t k = 0; k < available; k++)
      if ((double)scene->commands[k].sequence > base) command_count++;
    qa_qvm_cp_scene_command *commands = xmalloc(command_count * sizeof *commands);
    for (size_t k = 0, n = 0; k < available; k++)
      if ((double)scene->commands[k].sequence > base) commands[n++] = scene->commands[k];
    memset(&states[i], 0, sizeof states[i]);
    states[i].commands = commands;
    owned.count = i + 1;
    if (sequence != base && (command_count == 0
            || (double)commands[0].sequence != base + 1
            || (double)commands[command_count - 1].sequence != sequence))
      qa_raise("Error", 0, 0, "Original component reliable commands exceeded their retained window");
    bool game_changed = !same || old->game_state_revision != source->game_state_revision;
    if (same && source->game_state_revision < old->game_state_revision)
      qa_raise("Error", 0, 0, "Original component configstring revision moved backward");
    changed = changed || !same || game_changed || command_count > 0;
    sequence_set(publisher, source->identity.owner.provider, sequence);
    states[i].identity = source->identity;
    states[i].game_state_revision = source->game_state_revision;
    /* TS gameChanged ? context.gameState : null, and context.gameState may itself be null. */
    states[i].has_game_state = game_changed && source->game_state != NULL;
    if (states[i].has_game_state) states[i].game_state = *source->game_state;
    states[i].command_base = base;
    states[i].command_count = command_count;
  }
  size_t kept = 0;
  for (size_t i = 0; i < publisher->sequence_count; i++) {
    bool present = false;
    for (size_t j = 0; j < source_count; j++)
      if (strcmp(sources[j].identity.owner.provider, publisher->sequences[i].provider) == 0)
        present = true;
    if (present) publisher->sequences[kept++] = publisher->sequences[i];
    else free(publisher->sequences[i].provider);
  }
  publisher->sequence_count = kept;
  qa_app_unified_native_projection projected = qa_app_unified_project_native_components(
      publisher->native, publisher->native_count, native, native_count);
  qa_cleanup projected_guard;
  qa_cleanup_push(&projected_guard, projection_parts_free, &projected);
  retained next = {0};
  qa_cleanup kept_guard;
  qa_cleanup_push(&kept_guard, retained_cleanup, &next);
  qa_app_unified_component_publication *kept_sources = xmalloc(source_count * sizeof *kept_sources);
  qa_app_unified_native_publication *kept_native = NULL;
  {
    qa_cleanup sources_guard;
    qa_cleanup_push(&sources_guard, free, kept_sources);
    for (size_t i = 0; i < source_count; i++) kept_sources[i] = keep_source(&next, &sources[i]);
    kept_native = xmalloc(native_count * sizeof *kept_native);
    qa_cleanup native_guard;
    qa_cleanup_push(&native_guard, free, kept_native);
    for (size_t i = 0; i < native_count; i++) kept_native[i] = keep_native(&next, &native[i]);
    qa_cleanup_pop(&native_guard);
    qa_cleanup_pop(&sources_guard);
  }
  qa_cleanup_pop(&kept_guard);
  qa_cleanup_pop(&projected_guard);
  qa_cleanup_pop(&states_guard);
  free(publisher->sources);
  publisher->sources = kept_sources;
  publisher->source_count = source_count;
  free(publisher->native);
  publisher->native = kept_native;
  publisher->native_count = native_count;
  retained_free(&publisher->kept);
  publisher->kept = next;
  qa_app_unified_component_projection out;
  memset(&out, 0, sizeof out);
  if (changed || projected.changed) {
    out.has_update = true;
    out.update.revision = ++publisher->revision;
    out.update.sources = states;
    out.update.source_count = source_count;
    out.update.native = projected.states;
    out.update.native_count = projected.state_count;
  } else {
    for (size_t i = 0; i < source_count; i++) free(states[i].commands);
    free(states);
    free(projected.states);
  }
  out.frame.revision = publisher->revision;
  out.frame.has_native = true;
  out.frame.native = projected.frames;
  out.frame.native_count = projected.frame_count;
  out.frame.sources = xmalloc(source_count * sizeof *out.frame.sources);
  out.frame.source_count = source_count;
  for (size_t i = 0; i < source_count; i++) {
    const qa_app_unified_component_publication *source = &sources[i];
    qa_app_unified_component_frame *frame = &out.frame.sources[i];
    memset(frame, 0, sizeof *frame);
    frame->owner = source->identity.owner;
    frame->generation = source->identity.generation;
    frame->abi = source->identity.abi;
    frame->viewer = source->viewer;
    frame->client_number = source->has_client_number ? source->client_number
        : source->snapshot.player_state.client_number;
    frame->game_state_revision = source->game_state_revision;
    frame->snapshot = source->snapshot;
    frame->weapon_presented = source->has_weapon_presented && source->weapon_presented;
    frame->binding_count = source->binding_count;
    frame->bindings = xmalloc(source->binding_count * sizeof *frame->bindings);
    if (source->binding_count)
      memcpy(frame->bindings, source->bindings, source->binding_count * sizeof *frame->bindings);
    if (source->scene) {
      frame->has_scene = true;
      frame->scene.revision = (double)source->scene->revision;
      frame->scene.snapshot = source->scene->snapshot;
      frame->scene.snapshot.entities = xmalloc(
          source->scene->snapshot.entity_count * sizeof *frame->scene.snapshot.entities);
      if (source->scene->snapshot.entity_count)
        memcpy((void *)frame->scene.snapshot.entities, source->scene->snapshot.entities,
            source->scene->snapshot.entity_count * sizeof *frame->scene.snapshot.entities);
    }
  }
  return out;
}
