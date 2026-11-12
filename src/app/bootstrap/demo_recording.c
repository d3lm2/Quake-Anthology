/* Port of src/app/bootstrap/demo-recording.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/demo_recording.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/content/mounts/paths.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/caught_message.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/platform/files/contained.h"
#include "qa/network/q2/demo.h"
#include "qa/network/q2/mvd_recording.h"
#include "qa/network/q3/demo.h"
struct qa_app_demo_recording {
  char *path;    /* TS recording.path as console text (what prints) */
  char *fs_path; /* the same path as the UTF-8 the file system receives */
  qa_app_demo_recording_identity identity;
  int fd;
  bool accepting, finishing;
  char *failure; /* TS this.failure: the first write error's message, kept for every later write */
  char *finish_error; /* stop()'s settled rejection (TS returns the same finishing promise again) */
  qa_vec3 angles;
  double seconds;
};
static void free_slot(void *slot) { free(*(char **)slot); }
static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
/* JS toLowerCase().endsWith(extension) for an ASCII extension: only ASCII letters fold onto ASCII. */
static bool ends_ci(const char *text, const char *suffix) {
  size_t t = strlen(text), s = strlen(suffix);
  if (t < s) return false;
  for (size_t i = 0; i < s; i++) {
    char a = text[t - s + i], b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (a != b) return false;
  }
  return true;
}
static bool starts_demos_ci(const char *text) {
  static const char prefix[] = "demos/";
  for (size_t i = 0; i < 6; i++) {
    char a = text[i];
    if (a == '\0') return false;
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (a != prefix[i]) return false;
  }
  return true;
}
/* TS recordingPath over console text: join(root, prefix, filename) with node:path's normalization. The name is
 * console text (one byte per UTF-16 unit); the result is the UTF-8 path TS hands the file system. */
static char *recording_fs_path(const char *root, const char *name, qa_app_demo_recording_identity identity) {
  char *normalized = qa_normalize_resource_path(name);
  if (normalized == NULL) qa_range_error("Out of memory");
  qa_cleanup normalized_guard;
  qa_cleanup_push(&normalized_guard, free_slot, &normalized);
  const char *extension = identity.kind == QA_APP_RECORDING_MVD ? ".mvd" : identity.kind == QA_APP_RECORDING_Q1 ? ".dem"
      : identity.kind == QA_APP_RECORDING_QW ? ".qwd"
      : identity.kind == QA_APP_RECORDING_Q2 || identity.kind == QA_APP_RECORDING_Q2_SERVER ? ".dm2" : ".dm_68";
  size_t length = strlen(normalized);
  bool has = ends_ci(normalized, extension);
  char *filename = malloc(length + strlen(extension) + 1);
  if (filename == NULL) qa_range_error("Out of memory");
  memcpy(filename, normalized, length + 1);
  if (!has) strcat(filename, extension);
  qa_cleanup_pop(&normalized_guard);
  free(normalized);
  qa_cleanup filename_guard;
  qa_cleanup_push(&filename_guard, free_slot, &filename);
  bool prefix = (identity.kind == QA_APP_RECORDING_MVD || identity.kind == QA_APP_RECORDING_Q2_SERVER ||
      identity.kind == QA_APP_RECORDING_Q2 || identity.kind == QA_APP_RECORDING_Q3) && !starts_demos_ci(filename);
  char *utf8 = qa_command_text_to_utf8(filename);
  qa_cleanup_pop(&filename_guard);
  free(filename);
  qa_cleanup utf8_guard;
  qa_cleanup_push(&utf8_guard, free_slot, &utf8);
  char *path = qa_node_path_join3(root, prefix ? "demos" : "", utf8);
  qa_cleanup_pop(&utf8_guard);
  free(utf8);
  return path;
}
/* The TS string of a UTF-8 path as console text (each character one byte); a character above U+00FF keeps the
 * UTF-8 bytes. */
static char *display_path(const char *fs_path) {
  char *text = NULL;
  if (qa_command_text_from_utf8(fs_path, &text)) return text;
  return dup_text(fs_path);
}
char *qa_app_recording_path(const char *root, const char *name, qa_app_demo_recording_identity identity) {
  char *fs_path = recording_fs_path(root, name, identity);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_slot, &fs_path);
  char *path = display_path(fs_path);
  qa_cleanup_pop(&guard);
  free(fs_path);
  return path;
}
/* The Node/Bun fs Error message ("<CODE>: <description>, <syscall>[ '<path>']") as malloc'd text. */
typedef struct node_error_call { int code; const char *syscall; const char *path; } node_error_call;
static void node_error_body(void *ctx) {
  node_error_call *call = ctx;
  qa_node_fs_error_paths(call->code, call->syscall, call->path, NULL);
}
static char *node_error_text(int code, const char *syscall, const char *path) {
  node_error_call call = {code, syscall, path};
  qa_caught_error caught;
  (void)qa_try(node_error_body, &call, &caught);
  return dup_text(qa_caught_message(&caught));
}
/* Node's mkdir(path, {recursive: true}) as Bun runs it: ENOENT walks to the parent and retries; an existing
 * non-directory is EEXIST; any other failure is reported at the directory where it happened. 0 or the errno, with
 * *failed the malloc'd path the error names. */
static int mkdir_recursive(const char *path, char **failed) {
  if (mkdir(path, 0777) == 0) return 0;
  int code = errno;
  if (code == ENOENT) {
    const char *slash = strrchr(path, '/');
    if (slash != NULL && slash != path) {
      size_t parent_length = (size_t)(slash - path);
      char *parent = malloc(parent_length + 1);
      if (parent == NULL) qa_range_error("Out of memory");
      memcpy(parent, path, parent_length);
      parent[parent_length] = '\0';
      int parent_code = mkdir_recursive(parent, failed);
      free(parent);
      if (parent_code != 0) return parent_code;
      if (mkdir(path, 0777) == 0) return 0;
      code = errno;
    }
  }
  if (code == EEXIST) {
    struct stat info;
    if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) return 0;
  }
  *failed = dup_text(path);
  return code;
}
/* TS FileHandle.write loop: an error rejects with Node's "<CODE>: <description>, write"; zero bytes is
 * "Recording write made no progress". The first failure is kept and every later write rejects with it. */
static const char *write_bytes(qa_app_demo_recording *r, const uint8_t *bytes, size_t length) {
  if (r->failure) return r->failure;
  size_t offset = 0;
  while (offset < length) {
    ssize_t written = write(r->fd, bytes + offset, length - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written < 0) { r->failure = node_error_text(errno, "write", NULL); return r->failure; }
    if (written == 0) { r->failure = dup_text("Recording write made no progress"); return r->failure; }
    offset += (size_t)written;
  }
  return NULL;
}
static _Thread_local char *open_error; /* the last failed open's message (see the header) */
static const char *set_open_error(char *text) {
  free(open_error);
  open_error = text;
  return open_error;
}
qa_app_demo_recording *qa_app_demo_recording_open(const char *root, const char *name, qa_app_demo_recording_seed seed, bool replace,
                                                  const char **out_error) {
  *out_error = NULL;
  if (seed.packet_count == 0) { *out_error = "Recording requires matching initial source state"; return NULL; }
  for (size_t i = 0; i < seed.packet_count; i++)
    if (seed.packets[i].kind != seed.identity.kind) { *out_error = "Recording requires matching initial source state"; return NULL; }
  char *fs_path = recording_fs_path(root, name, seed.identity);
  qa_cleanup path_guard;
  qa_cleanup_push(&path_guard, free_slot, &fs_path);
  char *path = display_path(fs_path);
  qa_cleanup display_guard;
  qa_cleanup_push(&display_guard, free_slot, &path);
  /* mkdir(dirname(path), { recursive: true }) */
  char *directory = qa_node_path_dirname(fs_path);
  char *failed = NULL;
  int code = mkdir_recursive(directory, &failed);
  free(directory);
  if (code != 0) {
    char *shown = display_path(failed);
    free(failed);
    *out_error = set_open_error(node_error_text(code, "mkdir", shown));
    free(shown);
    qa_cleanup_pop(&display_guard);
    qa_cleanup_pop(&path_guard);
    free(path); free(fs_path);
    return NULL;
  }
  /* A name the user typed replaces an existing demo like the original clients; generated names must not collide.
   * TS open(path, "w" | "wx"): O_WRONLY|O_CREAT plus O_TRUNC or O_EXCL, mode 0o666. */
  int fd = open(fs_path, O_WRONLY | O_CREAT | O_CLOEXEC | (replace ? O_TRUNC : O_EXCL), 0666);
  if (fd < 0) {
    *out_error = set_open_error(node_error_text(errno, "open", path));
    qa_cleanup_pop(&display_guard);
    qa_cleanup_pop(&path_guard);
    free(path); free(fs_path);
    return NULL;
  }
  qa_cleanup_pop(&display_guard);
  qa_cleanup_pop(&path_guard);
  qa_app_demo_recording *r = calloc(1, sizeof *r);
  if (r == NULL) { close(fd); free(path); free(fs_path); qa_range_error("Out of memory"); }
  r->path = path;
  r->fs_path = fs_path;
  r->identity = seed.identity;
  r->fd = fd;
  r->accepting = true;
  const char *error = NULL;
  if (seed.identity.kind == QA_APP_RECORDING_MVD) {
    size_t len = 0; uint8_t *magic = qa_q2_mvd_magic(&len);
    error = write_bytes(r, magic, len); free(magic);
  }
  if (!error && seed.identity.kind == QA_APP_RECORDING_Q1) {
    size_t len = 0; uint8_t *header = qa_write_netquake_demo_header(seed.identity.track, &len);
    error = write_bytes(r, header, len); free(header);
  }
  for (size_t i = 0; !error && i < seed.packet_count; i++) error = qa_app_demo_recording_append(r, seed.packets[i]);
  if (error) {
    *out_error = set_open_error(dup_text(error));
    qa_app_demo_recording_abort(r);
    /* A recording that never got its seed is a truncated file, not a demo; the write failure is the error reported. */
    (void)unlink(r->fs_path);
    qa_app_demo_recording_destroy(r);
    return NULL;
  }
  return r;
}
const char *qa_app_demo_recording_path(const qa_app_demo_recording *r) { return r->path; }
qa_app_demo_recording_identity qa_app_demo_recording_identity_of(const qa_app_demo_recording *r) { return r->identity; }
static const char *sink_append(void *ctx, qa_app_demo_recording_packet packet) {
  return qa_app_demo_recording_append(ctx, packet);
}
qa_app_demo_recording_sink qa_app_demo_recording_sink_of(qa_app_demo_recording *r) {
  return (qa_app_demo_recording_sink){r, sink_append};
}
const char *qa_app_demo_recording_append(qa_app_demo_recording *r, qa_app_demo_recording_packet packet) {
  if (!r->accepting) return "Recording is stopped";
  if (packet.kind != r->identity.kind) return "Recording source protocol changed";
  uint8_t *bytes = NULL;
  size_t length = 0;
  switch (packet.kind) {
    case QA_APP_RECORDING_MVD: bytes = qa_q2_frame_mvd_message(packet.message, packet.message_length, &length); break;
    case QA_APP_RECORDING_Q1: {
      r->angles = packet.view_angles;
      qa_netquake_demo_record record = {packet.view_angles, (uint8_t *)packet.message, packet.message_length};
      bytes = qa_write_netquake_demo_record(&record, &length);
      break;
    }
    case QA_APP_RECORDING_QW:
      r->seconds = packet.record.seconds;
      bytes = qa_write_quakeworld_demo_record(&packet.record, &length);
      break;
    case QA_APP_RECORDING_Q2_SERVER:
    case QA_APP_RECORDING_Q2: bytes = qa_q2_write_demo_record(packet.message, packet.message_length, &length); break;
    case QA_APP_RECORDING_Q3: {
      qa_q3_demo_message message = {(int64_t)packet.sequence, packet.message, packet.message_length};
      if (!qa_q3_encode_demo_message(&message, &bytes, &length)) qa_range_error("Out of memory");
      break;
    }
  }
  const char *error = write_bytes(r, bytes, length);
  free(bytes);
  return error;
}
const char *qa_app_demo_recording_stop(qa_app_demo_recording *r) {
  if (r->finishing) return r->finish_error;
  r->finishing = true;
  r->accepting = false;
  uint8_t *footer = NULL;
  size_t length = 0;
  static const uint8_t mvd_footer[2] = {0, 0};
  const uint8_t *bytes = NULL;
  switch (r->identity.kind) {
    case QA_APP_RECORDING_Q2_SERVER: length = 0; break;
    case QA_APP_RECORDING_MVD: bytes = mvd_footer; length = 2; break;
    case QA_APP_RECORDING_Q1: {
      uint8_t end = 2;
      qa_netquake_demo_record record = {r->angles, &end, 1};
      footer = qa_write_netquake_demo_record(&record, &length);
      bytes = footer;
      break;
    }
    case QA_APP_RECORDING_QW: {
      static const uint8_t tail[] = {255, 255, 255, 255, 2, 'E', 'n', 'd', 'O', 'f', 'D', 'e', 'm', 'o', 0};
      qa_quakeworld_demo_record record = {0};
      record.kind = "packet"; record.seconds = r->seconds;
      record.message = (uint8_t *)tail; record.length = sizeof tail;
      footer = qa_write_quakeworld_demo_record(&record, &length);
      bytes = footer;
      break;
    }
    case QA_APP_RECORDING_Q2: footer = qa_q2_finish_demo(&length); bytes = footer; break;
    case QA_APP_RECORDING_Q3: qa_q3_finish_demo(&footer, &length); bytes = footer; break;
  }
  /* try { await this.write(footer); await this.file.sync(); } finally { await this.file.close(); } */
  const char *error = write_bytes(r, bytes, length);
  free(footer);
  if (error != NULL) r->finish_error = dup_text(error);
  else if (r->fd >= 0 && fsync(r->fd) != 0) r->finish_error = node_error_text(errno, "fsync", NULL);
  if (r->fd >= 0) { close(r->fd); r->fd = -1; }
  return r->finish_error;
}
/* TS abort(): awaits the write chain (which never rejects) and closes; it resolves even after a failed write. A
 * recording already finishing answers its stop() result. */
const char *qa_app_demo_recording_abort(qa_app_demo_recording *r) {
  if (r->finishing) return r->finish_error;
  r->finishing = true;
  r->accepting = false;
  if (r->fd >= 0) { close(r->fd); r->fd = -1; }
  return NULL;
}
void qa_app_demo_recording_destroy(qa_app_demo_recording *r) {
  if (!r) return;
  if (r->fd >= 0) close(r->fd);
  free(r->path);
  free(r->fs_path);
  free(r->failure);
  free(r->finish_error);
  free(r);
}
