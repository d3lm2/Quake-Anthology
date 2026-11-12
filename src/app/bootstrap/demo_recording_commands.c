/* Port of src/app/bootstrap/demo-recording-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/demo_recording_commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct sink_ctx { struct qa_app_client_demo_recording *owner; qa_app_demo_recording *recording; } sink_ctx;
typedef struct {
  qa_app_demo_recording *recording;
  qa_app_recording_feed feed;
  const void *token;
  bool reconnect;
  sink_ctx sink; /* the TS sink closure; lives as long as the attachment */
} current_t;
struct qa_app_client_demo_recording {
  qa_app_demo_recording_host host;
  current_t *current, *server;
  bool busy;
  char *error; /* the last returned rejection's text (valid until the next call) */
};
/* A rejection's text, copied: the recording that owns the original may be destroyed before the caller reads it. */
static const char *keep_error(qa_app_client_demo_recording *owner, const char *error) {
  if (error == NULL) return NULL;
  size_t length = strlen(error) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, error, length);
  free(owner->error);
  owner->error = copy;
  return copy;
}
static void print_text(qa_app_client_demo_recording *owner, const char *prefix, const char *text, const char *suffix) {
  size_t length = strlen(prefix) + strlen(text) + strlen(suffix) + 1;
  char *line = malloc(length);
  if (line == NULL) qa_range_error("Out of memory");
  snprintf(line, length, "%s%s%s", prefix, text, suffix);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, line);
  owner->host.print(owner->host.ctx, line);
  qa_cleanup_pop(&guard);
  free(line);
}
static void current_detach(qa_app_client_demo_recording *owner, current_t *current) {
  if (current->reconnect) owner->host.reconnect_detach(owner->host.ctx, current->token);
  else current->feed.detach(current->feed.ctx, current->token);
}
/* TS `finally { this.busy = false; }` for a raise out of a seed, attach or open. */
static void busy_release(void *owner) { ((qa_app_client_demo_recording *)owner)->busy = false; }
static void current_drop(void *raw) {
  current_t *current = raw;
  if (current == NULL) return;
  qa_app_demo_recording_destroy(current->recording);
  free(current);
}
static const char *sink_append(void *ctx, qa_app_demo_recording_packet packet) {
  sink_ctx *sink = ctx;
  const char *error = qa_app_demo_recording_append(sink->recording, packet);
  if (!error) return NULL;
  qa_app_client_demo_recording *owner = sink->owner;
  if (!owner->current || owner->current->recording != sink->recording) return NULL;
  current_t *current = owner->current;
  owner->current = NULL;
  /* The detached feed never calls this sink again: the current entry (and the sink in it) goes after the print. */
  qa_cleanup guard;
  qa_cleanup_push(&guard, current_drop, current);
  current_detach(owner, current);
  /* try { await recording.abort(); } finally { print(`Demo recording failed: ${message}`) } */
  qa_app_demo_recording_abort(current->recording);
  print_text(owner, "Demo recording failed: ", error, "\n");
  qa_cleanup_pop(&guard);
  current_drop(current);
  return NULL;
}
static current_t *current_create(qa_app_client_demo_recording *owner, qa_app_demo_recording *recording) {
  current_t *current = calloc(1, sizeof *current);
  if (current == NULL) qa_range_error("Out of memory");
  current->recording = recording;
  current->sink = (sink_ctx){owner, recording};
  return current;
}
static qa_app_demo_recording_sink current_sink(current_t *current) {
  return (qa_app_demo_recording_sink){&current->sink, sink_append};
}
/* TS finish(): detach, await stop() (a rejection propagates before the print), then "Completed demo <path>". */
static const char *finish_current(qa_app_client_demo_recording *owner) {
  current_t *current = owner->current;
  if (!current) return NULL;
  owner->current = NULL;
  qa_cleanup guard;
  qa_cleanup_push(&guard, current_drop, current);
  current_detach(owner, current);
  const char *error = keep_error(owner, qa_app_demo_recording_stop(current->recording));
  if (error == NULL) print_text(owner, "Completed demo ", qa_app_demo_recording_path(current->recording), "\n");
  qa_cleanup_pop(&guard);
  current_drop(current);
  return error;
}
static qa_app_demo_recording *open_recording(qa_app_client_demo_recording *owner, const char *name,
                                             qa_app_demo_recording_seed seed, const char **out_error) {
  if (name) return qa_app_demo_recording_open(owner->host.root(owner->host.ctx), name, seed, true, out_error);
  for (int index = 0; index < 10000; index++) {
    char generated[16];
    snprintf(generated, sizeof generated, "demo%04d", index);
    qa_app_demo_recording *recording = qa_app_demo_recording_open(owner->host.root(owner->host.ctx), generated, seed, false, out_error);
    if (recording) return recording;
    /* TS rethrows unless error.code === "EEXIST" (Node's message starts with the code). */
    if (!*out_error || strncmp(*out_error, "EEXIST: ", 8) != 0) return NULL;
  }
  *out_error = "No unused demo0000–demo9999 recording name remains";
  return NULL;
}
/* start/startMvd: seed, then finish the current recording, open the new one and attach its sink. */
static const char *start_feed(qa_app_client_demo_recording *owner, const char *name, qa_command_context source,
                              qa_app_recording_feed feed, bool mvd) {
  owner->busy = true;
  qa_cleanup busy_guard;
  qa_cleanup_push(&busy_guard, busy_release, owner);
  qa_app_demo_recording_seed seed = {0};
  const char *error = feed.seed(feed.ctx, source, &seed);
  if (!error && mvd && seed.identity.kind != QA_APP_RECORDING_MVD) error = "Multiview source supplied a different recording format";
  if (!error && !mvd && !name && seed.identity.kind != QA_APP_RECORDING_Q3) error = "Usage: record <name>";
  error = keep_error(owner, error);
  if (!error) error = finish_current(owner);
  qa_app_demo_recording *recording = NULL;
  if (!error) error = keep_error(owner, (recording = open_recording(owner, name, seed, &error)) == NULL ? error : NULL);
  if (!error) {
    current_t *current = current_create(owner, recording);
    /* try { attach } catch { await recording.abort(); throw } */
    qa_cleanup attach_guard;
    qa_cleanup_push(&attach_guard, current_drop, current);
    current->feed = feed;
    current->token = feed.attach(feed.ctx, current_sink(current));
    qa_cleanup_pop(&attach_guard);
    owner->current = current;
    print_text(owner, "Recording ", qa_app_demo_recording_path(recording), "\n");
  }
  qa_cleanup_pop(&busy_guard);
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_start(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  return start_feed(owner, name, source, owner->host.main_feed, false);
}
const char *qa_app_client_demo_recording_start_mvd(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  if (!owner->host.has_mvd_recording) return "mvdrecord requires a hosted Quake II multiview source";
  return start_feed(owner, name, source, owner->host.mvd_recording, true);
}
const char *qa_app_client_demo_recording_start_server(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  if (owner->server) return "Already doing a serverrecord";
  if (!owner->host.has_server_recording) return "serverrecord requires a hosted classic Quake II source";
  owner->busy = true;
  qa_cleanup busy_guard;
  qa_cleanup_push(&busy_guard, busy_release, owner);
  qa_app_demo_recording_seed seed = {0};
  const char *error = owner->host.server_recording.seed(owner->host.server_recording.ctx, source, &seed);
  if (!error && seed.identity.kind != QA_APP_RECORDING_Q2_SERVER) error = "Server source supplied a different recording format";
  error = keep_error(owner, error);
  qa_app_demo_recording *recording = NULL;
  if (!error) error = keep_error(owner, (recording = open_recording(owner, name, seed, &error)) == NULL ? error : NULL);
  if (!error) {
    current_t *current = current_create(owner, recording);
    qa_cleanup attach_guard;
    qa_cleanup_push(&attach_guard, current_drop, current);
    current->feed = owner->host.server_recording;
    /* TS { append: packet => recording.append(packet) } */
    current->token = owner->host.server_recording.attach(owner->host.server_recording.ctx, qa_app_demo_recording_sink_of(recording));
    qa_cleanup_pop(&attach_guard);
    owner->server = current;
    print_text(owner, "Recording server footage ", qa_app_demo_recording_path(recording), "\n");
  }
  qa_cleanup_pop(&busy_guard);
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_stop_server(qa_app_client_demo_recording *owner) {
  if (owner->busy) return "Recording operation is already in progress";
  owner->busy = true;
  qa_cleanup busy_guard;
  qa_cleanup_push(&busy_guard, busy_release, owner);
  current_t *current = owner->server;
  const char *error = NULL;
  if (!current) owner->host.print(owner->host.ctx, "Not doing a serverrecord.\n");
  else {
    owner->server = NULL;
    qa_cleanup guard;
    qa_cleanup_push(&guard, current_drop, current);
    current->feed.detach(current->feed.ctx, current->token);
    error = keep_error(owner, qa_app_demo_recording_stop(current->recording));
    if (error == NULL) print_text(owner, "Completed server footage ", qa_app_demo_recording_path(current->recording), "\n");
    qa_cleanup_pop(&guard);
    current_drop(current);
  }
  qa_cleanup_pop(&busy_guard);
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_stop(qa_app_client_demo_recording *owner) {
  if (owner->busy) return "Recording operation is already in progress";
  owner->busy = true;
  qa_cleanup busy_guard;
  qa_cleanup_push(&busy_guard, busy_release, owner);
  const char *error = finish_current(owner);
  qa_cleanup_pop(&busy_guard);
  owner->busy = false;
  return error;
}
/* TS try { await this.stop(); } finally { if (this.currentServer !== null) await this.stopServer(); }: a rejection of
 * the finally block replaces the first. */
const char *qa_app_client_demo_recording_stop_all(qa_app_client_demo_recording *owner) {
  const char *error = qa_app_client_demo_recording_stop(owner);
  if (owner->server) {
    char *first = error != NULL ? strdup(error) : NULL;
    if (error != NULL && first == NULL) qa_range_error("Out of memory");
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, first);
    const char *server_error = qa_app_client_demo_recording_stop_server(owner);
    qa_cleanup_pop(&guard);
    error = server_error != NULL ? server_error : keep_error(owner, first);
    free(first);
  }
  return error;
}
const char *qa_app_client_demo_recording_rerecord(qa_app_client_demo_recording *owner, const char *name, qa_command_context source) {
  if (owner->busy) return "Recording operation is already in progress";
  owner->busy = true;
  qa_cleanup busy_guard;
  qa_cleanup_push(&busy_guard, busy_release, owner);
  qa_app_demo_recording_seed seed = {0};
  const char *error = owner->host.main_feed.seed(owner->host.main_feed.ctx, source, &seed);
  if (!error && seed.identity.kind != QA_APP_RECORDING_QW) error = "rerecord requires a QuakeWorld server";
  error = keep_error(owner, error);
  const char *root = NULL;
  if (!error) {
    root = owner->host.root(owner->host.ctx);
    error = finish_current(owner);
  }
  qa_app_demo_recording *recording = NULL;
  if (!error) {
    qa_app_demo_recording_packet packet = {0};
    packet.kind = QA_APP_RECORDING_QW;
    packet.record.kind = "sequences";
    qa_app_demo_recording_seed fresh = {seed.identity, &packet, 1};
    recording = qa_app_demo_recording_open(root, name, fresh, true, &error);
    error = keep_error(owner, recording == NULL ? error : NULL);
  }
  if (!error) {
    current_t *current = current_create(owner, recording);
    qa_cleanup attach_guard;
    qa_cleanup_push(&attach_guard, current_drop, current);
    const char *attach_error = NULL;
    const void *token = owner->host.reconnect_recording(owner->host.ctx, source, current_sink(current), &attach_error);
    qa_cleanup_pop(&attach_guard);
    if (!attach_error) {
      current->token = token;
      current->reconnect = true;
      owner->current = current;
      print_text(owner, "Recording ", qa_app_demo_recording_path(recording), "\n");
    } else {
      /* catch (error) { await recording.abort(); throw error; } */
      error = keep_error(owner, attach_error);
      qa_app_demo_recording_abort(recording);
      current_drop(current);
    }
  }
  qa_cleanup_pop(&busy_guard);
  owner->busy = false;
  return error;
}
const char *qa_app_client_demo_recording_stop_mvd(qa_app_client_demo_recording *owner) {
  if (!owner->current || qa_app_demo_recording_identity_of(owner->current->recording).kind != QA_APP_RECORDING_MVD) {
    owner->host.print(owner->host.ctx, "Not recording a multiview demo.\n");
    return NULL;
  }
  return qa_app_client_demo_recording_stop(owner);
}
typedef struct { qa_app_client_demo_recording *owner; const char *name; } handler_ctx;
typedef struct { qa_command_buffer *buffer; const char *names[8]; handler_ctx *ctxs[8]; size_t count; } cmd_binding;
static void record_handler(void *ctx, qa_command_invocation *command) {
  handler_ctx *handler = ctx;
  qa_app_client_demo_recording *owner = handler->owner;
  const char *name = handler->name;
  const qa_command_origin *origin = &command->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller) origin = origin->caller;
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    char text[256];
    snprintf(text, sizeof text, "%s is a local recording command.\n", name);
    owner->host.print(owner->host.ctx, text);
    return;
  }
  size_t count = command->argc > 0 ? command->argc - 1 : 0;
  const char *arg0 = count > 0 ? command->argv[1] : NULL;
  if (strcmp(name, "record") == 0) {
    if (count > 1) { owner->host.print(owner->host.ctx, "Usage: record [name]\n"); return; }
    owner->host.stage(owner->host.ctx, (qa_app_demo_recording_intent){"record", arg0, command->source});
  } else if (strcmp(name, "rerecord") == 0 || strcmp(name, "mvdrecord") == 0 || strcmp(name, "serverrecord") == 0) {
    if (!arg0 || count != 1) {
      char text[128];
      snprintf(text, sizeof text, "Usage: %s <name>\n", name);
      owner->host.print(owner->host.ctx, text);
      return;
    }
    owner->host.stage(owner->host.ctx, (qa_app_demo_recording_intent){name, arg0, command->source});
  } else if (count != 0) {
    char text[128];
    snprintf(text, sizeof text, "Usage: %s\n", name);
    owner->host.print(owner->host.ctx, text);
  } else {
    const char *kind = strcmp(name, "serverstop") == 0 ? "serverstop" : strcmp(name, "mvdstop") == 0 ? "mvdstop" : "stop";
    owner->host.stage(owner->host.ctx, (qa_app_demo_recording_intent){kind, NULL, command->source});
  }
}
void *qa_app_client_demo_recording_attach(qa_app_client_demo_recording *owner, qa_command_buffer *commands) {
  cmd_binding *bind = calloc(1, sizeof *bind);
  bind->buffer = commands;
  /* The buffer borrows documentation: static texts and example arrays (TS usage: record [name] | <name> <name> | name;
   * examples: `${name} session1` for the starting commands, else the name). */
  static const char *const finish = "Finish the current demo recording.";
  static const char *const record_examples[] = {"record session1"}, *const rerecord_examples[] = {"rerecord session1"},
      *const stop_examples[] = {"stop"}, *const stoprecord_examples[] = {"stoprecord"}, *const mvdrecord_examples[] = {"mvdrecord session1"},
      *const mvdstop_examples[] = {"mvdstop"}, *const serverrecord_examples[] = {"serverrecord session1"}, *const serverstop_examples[] = {"serverstop"};
  static const struct { const char *name, *summary, *usage; const char *const *examples; } rows[] = {
      {"record", "Record the current session to a demo.", "record [name]", record_examples},
      {"rerecord", "Reconnect to the QuakeWorld server and record its signon.", "rerecord <name>", rerecord_examples},
      {"stop", NULL, "stop", stop_examples},
      {"stoprecord", NULL, "stoprecord", stoprecord_examples},
      {"mvdrecord", "Record the hosted Quake II world as a native multiview demo.", "mvdrecord <name>", mvdrecord_examples},
      {"mvdstop", NULL, "mvdstop", mvdstop_examples},
      {"serverrecord", "Record native Quake II server entity footage.", "serverrecord <name>", serverrecord_examples},
      {"serverstop", NULL, "serverstop", serverstop_examples}};
  for (size_t i = 0; i < 8; i++) {
    const char *name = rows[i].name;
    handler_ctx *ctx = malloc(sizeof *ctx);
    *ctx = (handler_ctx){owner, name};
    qa_command_documentation doc = {rows[i].summary != NULL ? rows[i].summary : finish, rows[i].usage, rows[i].examples, 1, NULL, 0};
    if (qa_command_buffer_register(commands, name, record_handler, ctx, &doc, QA_COMMAND_WORLD)) {
      bind->names[bind->count] = name;
      bind->ctxs[bind->count++] = ctx;
    } else {
      char text[256];
      snprintf(text, sizeof text, "Recording command %s is already owned by another command.\n", name);
      owner->host.print(owner->host.ctx, text);
      free(ctx);
    }
  }
  return bind;
}
void qa_app_client_demo_recording_detach_commands(qa_app_client_demo_recording *owner, void *handle) {
  (void)owner;
  cmd_binding *bind = handle;
  for (size_t i = 0; i < bind->count; i++) {
    qa_command_buffer_unregister(bind->buffer, bind->names[i], record_handler, bind->ctxs[i], true);
    free(bind->ctxs[i]);
  }
  free(bind);
}
qa_app_client_demo_recording *qa_app_client_demo_recording_create(qa_app_demo_recording_host host) {
  qa_app_client_demo_recording *owner = calloc(1, sizeof *owner);
  if (!owner) return NULL;
  owner->host = host;
  return owner;
}
void qa_app_client_demo_recording_destroy(qa_app_client_demo_recording *owner) {
  if (!owner) return;
  current_drop(owner->current);
  current_drop(owner->server);
  free(owner->error);
  free(owner);
}
const char *qa_app_client_demo_recording_path(const qa_app_client_demo_recording *owner) {
  return owner->current ? qa_app_demo_recording_path(owner->current->recording) : NULL;
}
const char *qa_app_client_demo_recording_server_path(const qa_app_client_demo_recording *owner) {
  return owner->server ? qa_app_demo_recording_path(owner->server->recording) : NULL;
}
