/* Port of src/app/bootstrap/network/unified-native-consumer.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_native_consumer.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct entry {
  qa_active_mod_client_presentation active;
  const char *edition; bool view;
  qa_app_unified_native_identity identity;
  bool has_hud; qa_app_unified_native_hud_state hud;
  bool has_configs; qa_native_q2_config_string *configs; size_t config_count;
  bool has_frame; qa_actor_id frame_viewer;
  qa_mod_client_presentation_frame frame;
  qa_native_q2_hud_frame native_frame; /* storage behind frame */
  struct qa_app_unified_native_consumers *owner;
  /* Owned copies (the decoded update or frame is freed once applied; TS entries hold the
   * objects): active from the activation, identity/hud/configs from the latest update. A frame
   * read before an update keeps the hud and configs it was read with (frame_* blocks). */
  void *active_block, *identity_block, *hud_block, *configs_block;
  void *frame_hud_block, *frame_configs_block; bool frame_uses_current;
} entry;
struct qa_app_unified_native_consumers {
  qa_app_unified_native_consumer_options options;
  entry **entries; size_t count;
  double revision; bool legacy, closed;
  qa_active_mod_client_presentation *sources; size_t sources_cap;
};
struct qa_app_unified_native_commit {
  qa_app_unified_native_consumers *consumers;
  int action; /* 0 noop-clear, 1 update, 2 frames, 3 legacy */
  entry **next; size_t next_count;
  double revision;
  struct { entry *entry; const qa_app_unified_native_state *metadata;
           bool has_configs; const qa_native_q2_config_string *configs; size_t config_count; }
      *pending_updates; size_t pending_update_count;
  struct { entry *entry; qa_actor_id viewer; qa_mod_client_presentation_frame value; }
      *pending_frames; size_t pending_frame_count;
  entry *legacy_entry;
  /* Entries this commit created; freed with it unless running it adopted them. */
  entry **created; size_t created_count; bool adopted;
};
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static bool actor_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token
      && a.session.token == b.session.token;
}
static void current(qa_app_unified_native_consumers *c) {
  c->options.assert_current(c->options.ctx);
  if (c->closed) qa_raise("Error", 0, 0, "Remote native component collection is retired");
}
static entry *find_entry(qa_app_unified_native_consumers *c, const char *provider) {
  for (size_t i = 0; i < c->count; i++)
    if (strcmp(c->entries[i]->active.owner.provider, provider) == 0) return c->entries[i];
  return NULL;
}
static void source_assert(void *ctx) {
  entry *e = ctx;
  current(e->owner);
  if (find_entry(e->owner, e->active.owner.provider) != e)
    qa_raise("Error", 0, 0, "Remote native component is retired");
}
static bool source_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) {
  entry *e = ctx;
  source_assert(ctx);
  qa_actor_id viewer;
  bool has_viewer = e->owner->options.viewer(e->owner->options.ctx, &viewer);
  if (!has_viewer || !actor_equal(viewer, actor) || !e->has_frame
      || !actor_equal(e->frame_viewer, actor)) return false;
  *out = e->frame;
  return true;
}
static void entry_free(entry *e) {
  free(e->active_block);
  free(e->identity_block);
  free(e->hud_block);
  free(e->configs_block);
  free(e->frame_hud_block);
  free(e->frame_configs_block);
  free(e);
}
static qa_app_unified_native_identity identity_clone(const qa_app_unified_native_identity *source,
    void **block) {
  qa_app_unified_component_identity wide;
  memset(&wide, 0, sizeof wide);
  wide.owner = source->owner;
  wide.identity = source->identity;
  wide.generation = source->generation;
  wide = qa_app_unified_component_identity_clone(&wide, block);
  qa_app_unified_native_identity out;
  memset(&out, 0, sizeof out);
  out.owner = wide.owner;
  out.identity = wide.identity;
  out.generation = source->generation;
  return out;
}
static size_t text_size(const char *text) { return text ? strlen(text) + 1 : 0; }
static const char *text_put(char **cursor, const char *text) {
  if (!text) return NULL;
  size_t n = strlen(text) + 1;
  char *out = *cursor;
  memcpy(out, text, n);
  *cursor += n;
  return out;
}
/* configstring entries then their values, in one block. */
static qa_native_q2_config_string *configs_clone(const qa_native_q2_config_string *configs,
    size_t count, void **block) {
  size_t size = count * sizeof *configs;
  for (size_t i = 0; i < count; i++) size += text_size(configs[i].value);
  qa_native_q2_config_string *out = xmalloc(size);
  char *cursor = (char *)(out + count);
  for (size_t i = 0; i < count; i++) {
    out[i].index = configs[i].index;
    out[i].value = text_put(&cursor, configs[i].value);
  }
  *block = out;
  return out;
}
/* inventory, configstring entries, then every string, in one block. */
static qa_app_unified_native_hud_state hud_clone(const qa_app_unified_native_hud_state *source,
    void **block) {
  qa_app_unified_native_hud_state out = *source;
  size_t inventory = source->inventory_count * sizeof *source->inventory;
  size_t configs = source->configstring_count * sizeof *source->configstrings;
  size_t size = inventory + configs + text_size(source->mode) + text_size(source->protocol.kind)
      + text_size(source->layout);
  for (size_t i = 0; i < source->configstring_count; i++) size += text_size(source->configstrings[i].value);
  char *base = xmalloc(size);
  out.inventory = (double *)base;
  if (inventory) memcpy(out.inventory, source->inventory, inventory);
  out.configstrings = (qa_native_q2_config_string *)(base + inventory);
  char *cursor = base + inventory + configs;
  for (size_t i = 0; i < source->configstring_count; i++) {
    out.configstrings[i].index = source->configstrings[i].index;
    out.configstrings[i].value = text_put(&cursor, source->configstrings[i].value);
  }
  out.mode = text_put(&cursor, source->mode);
  out.protocol.kind = text_put(&cursor, source->protocol.kind);
  out.layout = text_put(&cursor, source->layout);
  *block = base;
  return out;
}
static void commit_track(qa_app_unified_native_commit *commit, entry *e) {
  entry **grown = realloc(commit->created, (commit->created_count + 1) * sizeof *grown);
  if (!grown) { entry_free(e); qa_range_error("out of memory"); }
  commit->created = grown;
  commit->created[commit->created_count++] = e;
}
static void cleanup_commit(void *arg) { qa_app_unified_native_commit_free(arg); }
static void entry_unwind(void *arg) { entry_free(arg); }
static entry *create_entry(qa_app_unified_native_consumers *c,
    const qa_app_unified_native_state *metadata, bool camera_only) {
  /* TS preparedMods.find: the first qualified module. */
  const qa_prepared_mod *prepared = NULL;
  for (size_t i = 0; i < c->options.prepared_mod_count && !prepared; i++)
    if (qa_same_mod_identity(&c->options.prepared_mods[i].identity,
            &metadata->identity.identity)) prepared = &c->options.prepared_mods[i];
  char *instance = qa_mod_instance_provider(metadata->identity.identity.selection);
  bool qualified = prepared && prepared->has_client_presentation && instance
      && strcmp(instance, metadata->identity.owner.provider) == 0;
  free(instance);
  if (!qualified)
    qa_raise("Error", 0, 0, "Remote native presentation differs from its locally qualified component");
  const char *admission_hud = prepared->client_presentation.hud;
  const char *mode = strcmp(admission_hud, "none") == 0 ? NULL
      : strcmp(admission_hud, "replace") == 0 ? "replace-status" : "layout-overlay";
  const char *metadata_mode = metadata->has_hud ? metadata->hud.mode : NULL;
  if (!camera_only && ((mode == NULL) != (metadata_mode == NULL)
          || (mode && strcmp(mode, metadata_mode) != 0)))
    qa_raise("Error", 0, 0, "Remote native HUD differs from its admitted mode");
  const char *family = NULL, *edition = NULL;
  c->options.product_expectation(c->options.ctx,
      metadata->identity.identity.source.content, &family, &edition);
  if (strcmp(family, "q2") != 0
      || (strcmp(edition, "classic") != 0 && strcmp(edition, "rerelease") != 0))
    qa_raise("Error", 0, 0, "Remote native presentation has no Q2 source edition");
  entry *e = calloc(1, sizeof *e);
  if (!e) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, entry_unwind, e);
  e->owner = c;
  e->edition = edition;
  e->view = prepared->client_presentation.view;
  e->identity = identity_clone(&metadata->identity, &e->identity_block);
  e->has_hud = metadata->has_hud;
  if (metadata->has_hud) e->hud = hud_clone(&metadata->hud, &e->hud_block);
  qa_app_unified_native_identity active = identity_clone(&metadata->identity, &e->active_block);
  qa_cleanup_pop(&guard);
  e->active.owner = active.owner;
  e->active.identity = active.identity;
  e->active.source.ctx = e;
  e->active.source.generation = metadata->identity.generation;
  e->active.source.frame = source_frame;
  e->active.source.assert_current = source_assert;
  return e;
}
qa_app_unified_native_consumers *qa_app_unified_native_consumers_create(
    qa_app_unified_native_consumer_options options) {
  qa_app_unified_native_consumers *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->options = options;
  return c;
}
void qa_app_unified_native_consumers_destroy(qa_app_unified_native_consumers *consumers) {
  if (!consumers) return;
  for (size_t i = 0; i < consumers->count; i++) entry_free(consumers->entries[i]);
  free(consumers->entries);
  free(consumers->sources);
  free(consumers);
}
qa_app_unified_native_commit *qa_app_unified_native_consumers_prepare_update(
    qa_app_unified_native_consumers *c, const qa_app_unified_component_update *update) {
  current(c);
  if (update->revision != c->revision + 1)
    qa_raise("Error", 0, 0, "Remote native reliable revision is not consecutive");
  qa_app_unified_native_commit *commit = calloc(1, sizeof *commit);
  if (!commit) qa_range_error("out of memory");
  commit->consumers = c;
  commit->action = 1;
  commit->revision = update->revision;
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_commit, commit);
  size_t count = update->native_count;
  commit->next = xmalloc(count * sizeof *commit->next);
  commit->next_count = count;
  commit->pending_updates = xmalloc(count * sizeof *commit->pending_updates);
  commit->pending_update_count = count;
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_native_state *metadata = &update->native[i];
    entry *previous = find_entry(c, metadata->identity.owner.provider);
    bool same = previous
        && qa_same_presentation_owner(&previous->active.owner, metadata->identity.owner)
        && previous->active.source.generation == metadata->identity.generation;
    entry *e = previous;
    if (!same) {
      e = create_entry(c, metadata, false);
      commit_track(commit, e);
    }
    const char *old_mode = e->has_hud ? e->hud.mode : NULL;
    const char *new_mode = metadata->has_hud ? metadata->hud.mode : NULL;
    if (!qa_same_mod_identity(&e->active.identity, &metadata->identity.identity)
        || ((old_mode == NULL) != (new_mode == NULL)
            || (old_mode && strcmp(old_mode, new_mode) != 0)))
      qa_raise("Error", 0, 0, "Remote native activation changed its admitted identity or HUD");
    bool has_configs = false;
    const qa_native_q2_config_string *configs = NULL;
    size_t config_count = 0;
    if (metadata->has_hud) {
      if (metadata->hud.has_configstrings) {
        has_configs = true;
        configs = metadata->hud.configstrings;
        config_count = metadata->hud.configstring_count;
      } else {
        has_configs = e->has_configs;
        configs = e->configs;
        config_count = e->config_count;
      }
    }
    if (metadata->has_hud && (!has_configs
            || strcmp(metadata->hud.protocol.kind,
                strcmp(e->edition, "classic") == 0 ? "q2-classic" : "q2-rerelease") != 0))
      qa_raise("Error", 0, 0, "Remote native HUD has no source configstrings or differs from its source edition");
    commit->next[i] = e;
    commit->pending_updates[i].entry = e;
    commit->pending_updates[i].metadata = metadata;
    commit->pending_updates[i].has_configs = has_configs;
    commit->pending_updates[i].configs = configs;
    commit->pending_updates[i].config_count = config_count;
  }
  qa_cleanup_pop(&guard);
  return commit;
}
static void read_frame(entry *e, const qa_app_unified_native_frame *frame,
    qa_actor_id viewer, qa_mod_client_presentation_frame *out) {
  if (!qa_same_presentation_owner(&e->active.owner, frame->owner)
      || e->active.source.generation != frame->generation
      || !actor_equal(frame->viewer, viewer))
    qa_raise("Error", 0, 0, "Remote native frame differs from its source or recipient admission");
  if ((frame->has_view) != e->view
      || (frame->has_view && strcmp(frame->view.edition, e->edition) != 0))
    qa_raise("Error", 0, 0, "Remote native camera differs from its admitted source");
  memset(out, 0, sizeof *out);
  out->kind = QA_MOD_CLIENT_PRESENTATION_NATIVE;
  out->has_native_view = frame->has_view;
  if (frame->has_view) out->native_view = frame->view;
  if ((!e->has_hud) != (!frame->has_hud))
    qa_raise("Error", 0, 0, "Remote native frame differs from its admitted HUD");
  if (!e->has_hud) { out->has_native_hud = false; return; }
  if (!e->has_configs
      || frame->hud.stat_count != (strcmp(e->edition, "classic") == 0 ? 32u : 64u))
    qa_raise("Error", 0, 0, "Remote native HUD differs from its source ABI");
  out->has_native_hud = true;
  out->native_hud_mode = e->hud.mode;
  qa_native_q2_hud_frame *native = &e->native_frame;
  memset(native, 0, sizeof *native);
  native->protocol = e->hud.protocol;
  native->configstrings = e->configs;
  native->configstring_count = e->config_count;
  native->stats = frame->hud.stats;
  native->stat_count = frame->hud.stat_count;
  native->server_frame = frame->hud.server_frame;
  native->time_milliseconds = frame->hud.time_milliseconds;
  native->has_frame_time_milliseconds = frame->hud.has_frame_time;
  native->frame_time_milliseconds = frame->hud.frame_time_milliseconds;
  native->layout = e->hud.layout;
  native->inventory = e->hud.inventory;
  native->inventory_count = e->hud.inventory_count;
  native->player_number = e->hud.player_number;
  out->native_hud_frame = *native;
}
qa_app_unified_native_commit *qa_app_unified_native_consumers_prepare(
    qa_app_unified_native_consumers *c, const qa_app_unified_component_frames *frames,
    qa_actor_id viewer, const qa_app_unified_native_camera *legacy) {
  current(c);
  if (frames->revision < c->revision) return NULL;
  if (frames->revision != c->revision)
    qa_raise("Error", 0, 0, "Remote native frame lacks reliable admission");
  if (legacy && !frames->has_native) {
    qa_app_unified_native_state metadata;
    memset(&metadata, 0, sizeof metadata);
    metadata.identity.owner = *(const qa_presentation_owner *)legacy->owner;
    metadata.identity.identity = *(const qa_mod_identity *)legacy->identity;
    metadata.identity.generation = legacy->generation;
    entry *previous = find_entry(c, metadata.identity.owner.provider);
    bool reuse = previous
        && qa_same_presentation_owner(&previous->active.owner, metadata.identity.owner)
        && previous->active.source.generation == metadata.identity.generation;
    qa_app_unified_native_commit *commit = calloc(1, sizeof *commit);
    if (!commit) qa_range_error("out of memory");
    commit->consumers = c;
    commit->action = 3;
    qa_cleanup legacy_guard;
    qa_cleanup_push(&legacy_guard, cleanup_commit, commit);
    entry *e = previous;
    if (!reuse) {
      e = create_entry(c, &metadata, true);
      commit_track(commit, e);
    }
    if (!qa_same_mod_identity(&e->active.identity, &metadata.identity.identity))
      qa_raise("Error", 0, 0, "Remote native activation changed identity");
    qa_app_unified_native_frame frame;
    memset(&frame, 0, sizeof frame);
    frame.owner = metadata.identity.owner;
    frame.generation = legacy->generation;
    frame.viewer = viewer;
    frame.has_view = true;
    frame.view = *(const qa_native_mod_camera_view *)legacy->view;
    qa_mod_client_presentation_frame value;
    memset(&value, 0, sizeof value);
    read_frame(e, &frame, viewer, &value);
    commit->legacy_entry = e;
    commit->pending_frames = xmalloc(sizeof *commit->pending_frames);
    commit->pending_frame_count = 1;
    commit->pending_frames[0].entry = e;
    commit->pending_frames[0].viewer = viewer;
    commit->pending_frames[0].value = value;
    qa_cleanup_pop(&legacy_guard);
    return commit;
  }
  qa_app_unified_native_commit *commit = calloc(1, sizeof *commit);
  if (!commit) qa_range_error("out of memory");
  commit->consumers = c;
  if (c->legacy && !frames->has_native) {
    commit->action = 0;
    return commit;
  }
  size_t count = frames->has_native ? frames->native_count : 0;
  if (count != c->count)
    qa_raise("Error", 0, 0, "Remote native frame differs from admitted owners");
  commit->action = 2;
  qa_cleanup frames_guard;
  qa_cleanup_push(&frames_guard, cleanup_commit, commit);
  commit->pending_frames = xmalloc(count * sizeof *commit->pending_frames);
  commit->pending_frame_count = count;
  size_t views = 0, replaces = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_native_frame *frame = &frames->native[i];
    entry *e = find_entry(c, frame->owner.provider);
    if (!e) qa_raise("Error", 0, 0, "Remote native frame has an unadmitted owner");
    qa_mod_client_presentation_frame value;
    memset(&value, 0, sizeof value);
    read_frame(e, frame, viewer, &value);
    if (value.has_native_view) views++;
    if (value.has_native_hud && value.native_hud_mode
        && strcmp(value.native_hud_mode, "replace-status") == 0) replaces++;
    commit->pending_frames[i].entry = e;
    commit->pending_frames[i].viewer = frame->viewer;
    commit->pending_frames[i].value = value;
  }
  if (views > 1 || replaces > 1)
    qa_raise("Error", 0, 0, "Remote native presentation has conflicting exclusive owners");
  qa_cleanup_pop(&frames_guard);
  return commit;
}
typedef struct update_blocks { void *identity, *hud, *configs; } update_blocks;
static void update_blocks_free(void *arg) {
  update_blocks *blocks = arg;
  free(blocks->identity);
  free(blocks->hud);
  free(blocks->configs);
}
/* A committed frame was read with the entry's current hud and configs. */
static void frame_adopt(entry *e) {
  free(e->frame_hud_block);
  free(e->frame_configs_block);
  e->frame_hud_block = NULL;
  e->frame_configs_block = NULL;
  e->frame_uses_current = true;
}
void qa_app_unified_native_commit_run(qa_app_unified_native_commit *commit) {
  qa_app_unified_native_consumers *c = commit->consumers;
  if (commit->action == 0) {
    for (size_t i = 0; i < c->count; i++) entry_free(c->entries[i]);
    c->count = 0;
    c->legacy = false;
    return;
  }
  if (commit->action == 3) {
    commit->adopted = true;
    entry *e = commit->legacy_entry;
    entry **next = xmalloc(sizeof *next);
    next[0] = e;
    for (size_t i = 0; i < c->count; i++)
      if (c->entries[i] != e) entry_free(c->entries[i]);
    free(c->entries);
    c->entries = next;
    c->count = 1;
    c->legacy = true;
    e->has_frame = true;
    e->frame_viewer = commit->pending_frames[0].viewer;
    e->frame = commit->pending_frames[0].value;
    frame_adopt(e);
    return;
  }
  if (commit->action == 1) {
    current(c);
    commit->adopted = true;
    for (size_t i = 0; i < c->count; i++) {
      entry *old = c->entries[i];
      entry *replacement = NULL;
      for (size_t j = 0; j < commit->next_count; j++)
        if (strcmp(commit->next[j]->active.owner.provider, old->active.owner.provider) == 0)
          replacement = commit->next[j];
      if (replacement != old
          && (!replacement
              || !qa_same_presentation_owner(&replacement->active.owner, old->active.owner))) {
        c->options.retire_replicated_owner(c->options.ctx, old->active.owner);
      }
      if (replacement != old) entry_free(old);
    }
    free(c->entries);
    c->entries = commit->next;
    commit->next = NULL;
    c->count = commit->next_count;
    c->revision = commit->revision;
    c->legacy = false;
    for (size_t i = 0; i < commit->pending_update_count; i++) {
      entry *e = commit->pending_updates[i].entry;
      const qa_app_unified_native_state *metadata = commit->pending_updates[i].metadata;
      /* Copy first: the pending configs may be this entry's current block. */
      update_blocks blocks = {NULL, NULL, NULL};
      qa_cleanup guard;
      qa_cleanup_push(&guard, update_blocks_free, &blocks);
      qa_app_unified_native_identity identity = identity_clone(&metadata->identity, &blocks.identity);
      qa_app_unified_native_hud_state hud;
      memset(&hud, 0, sizeof hud);
      if (metadata->has_hud) hud = hud_clone(&metadata->hud, &blocks.hud);
      qa_native_q2_config_string *configs = commit->pending_updates[i].has_configs
          ? configs_clone(commit->pending_updates[i].configs, commit->pending_updates[i].config_count,
              &blocks.configs)
          : NULL;
      qa_cleanup_pop(&guard);
      free(e->identity_block);
      e->identity_block = blocks.identity;
      e->identity = identity;
      if (e->has_frame && e->frame_uses_current) {
        /* The frame was read with the current hud and configs: it keeps them. */
        free(e->frame_hud_block);
        free(e->frame_configs_block);
        e->frame_hud_block = e->hud_block;
        e->frame_configs_block = e->configs_block;
        e->frame_uses_current = false;
      } else {
        free(e->hud_block);
        free(e->configs_block);
      }
      e->has_hud = metadata->has_hud;
      e->hud = hud;
      e->hud_block = blocks.hud;
      e->has_configs = commit->pending_updates[i].has_configs;
      e->config_count = configs ? commit->pending_updates[i].config_count : 0;
      e->configs = configs;
      e->configs_block = blocks.configs;
      c->options.admit_replicated_owner(c->options.ctx, metadata->identity.owner,
          metadata->identity.identity.source.content);
    }
    return;
  }
  for (size_t i = 0; i < commit->pending_frame_count; i++) {
    entry *e = commit->pending_frames[i].entry;
    e->has_frame = true;
    e->frame_viewer = commit->pending_frames[i].viewer;
    e->frame = commit->pending_frames[i].value;
    frame_adopt(e);
  }
}
void qa_app_unified_native_commit_free(qa_app_unified_native_commit *commit) {
  if (!commit) return;
  if (!commit->adopted)
    for (size_t i = 0; i < commit->created_count; i++) entry_free(commit->created[i]);
  free(commit->created);
  free(commit->next);
  free(commit->pending_updates);

  free(commit->pending_frames);
  free(commit);
}
void qa_app_unified_native_consumers_retire(qa_app_unified_native_consumers *consumers,
    qa_presentation_owner owner) {
  entry *e = find_entry(consumers, owner.provider);
  if (e && qa_same_presentation_owner(&e->active.owner, owner)) {
    entry **next = xmalloc(consumers->count ? consumers->count * sizeof *next : sizeof *next);
    size_t count = 0;
    for (size_t i = 0; i < consumers->count; i++)
      if (consumers->entries[i] != e) next[count++] = consumers->entries[i];
    free(consumers->entries);
    consumers->entries = next;
    consumers->count = count;
    entry_free(e);
  }
}
const qa_active_mod_client_presentation *qa_app_unified_native_consumers_sources(
    qa_app_unified_native_consumers *consumers, size_t *out_count) {
  current(consumers);
  if (consumers->sources_cap < consumers->count) {
    consumers->sources = realloc(consumers->sources,
        consumers->count * sizeof *consumers->sources);
    if (!consumers->sources) qa_range_error("out of memory");
    consumers->sources_cap = consumers->count;
  }
  size_t count = 0;
  for (size_t i = 0; i < consumers->count; i++)
    if (consumers->entries[i]->has_frame) consumers->sources[count++] = consumers->entries[i]->active;
  *out_count = count;
  return consumers->sources;
}
void qa_app_unified_native_consumers_close(qa_app_unified_native_consumers *consumers) {
  consumers->closed = true;
  for (size_t i = 0; i < consumers->count; i++) entry_free(consumers->entries[i]);
  consumers->count = 0;
}
