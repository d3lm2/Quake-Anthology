/* Port of src/app/bootstrap/player-progress.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/player_progress.h"
#include "qa/network/common/js_number.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdnoreturn.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/formats/text_decoder.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
/* Events are allocated one by one so the views list() and all() hand out stay
 * valid while later records grow the table. */
typedef struct owned_event {
  qa_app_player_progress_event view;
  char *kind, *source, *participant, *event, *award, *map, *rules;
} owned_event;
struct qa_app_player_progress_store {
  char *file;
  owned_event **events;
  size_t count, capacity;
  const qa_app_player_progress_event **all; size_t all_capacity;
};
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
/* TS isProviderId: a string containing ":". */
static bool is_provider_id(const char *text) { return text != NULL && strchr(text, ':') != NULL; }
/* TS decode(): NULL when valid, else the TS Error message. The result keeps only
 * the members of the decoded kind. */
static const char *decode(qa_app_player_progress_event e, bool has_won, qa_app_player_progress_event *out) {
  if (!(is(e.source, "q1") || is(e.source, "q2") || is(e.source, "q3"))
      || e.participant == NULL || e.participant[0] == '\0' || e.event == NULL || e.event[0] == '\0')
    return "Invalid player progress identity";
  qa_app_player_progress_event d;
  memset(&d, 0, sizeof d);
  d.source = e.source; d.participant = e.participant; d.event = e.event;
  if (is(e.kind, "achievement") && !is(e.source, "q3") && e.award != NULL && e.award[0] != '\0') {
    d.kind = "achievement"; d.award = e.award;
    *out = d;
    return NULL;
  }
  if (e.map == NULL || e.map[0] == '\0') return "Invalid player progress map";
  d.map = e.map;
  if (is(e.kind, "level-completed") && !is(e.source, "q3")) { d.kind = "level-completed"; *out = d; return NULL; }
  if (is(e.kind, "match-completed") && isfinite(e.score)) { d.kind = "match-completed"; d.score = e.score; *out = d; return NULL; }
  if (is(e.kind, "ranked-match") && isfinite(e.score) && has_won && is_provider_id(e.rules)) {
    d.kind = "ranked-match"; d.score = e.score; d.won = e.won; d.rules = e.rules;
    *out = d;
    return NULL;
  }
  return "Invalid player progress event";
}
static const char *str_field(const qa_save_value *object, const char *key) {
  const qa_save_value *value = qa_save_value_get(object, key);
  return value != NULL && value->kind == QA_SAVE_STRING ? value->text : NULL;
}
static owned_event *own_event(qa_app_player_progress_event e) {
  owned_event *owned = calloc(1, sizeof *owned);
  if (owned == NULL) qa_range_error("Out of memory");
  owned->kind = dup_text(e.kind); owned->source = dup_text(e.source);
  owned->participant = dup_text(e.participant); owned->event = dup_text(e.event);
  owned->award = dup_text(e.award); owned->map = dup_text(e.map); owned->rules = dup_text(e.rules);
  owned->view.kind = owned->kind; owned->view.source = owned->source;
  owned->view.participant = owned->participant; owned->view.event = owned->event;
  owned->view.award = owned->award; owned->view.map = owned->map; owned->view.score = e.score;
  owned->view.won = e.won; owned->view.rules = owned->rules;
  return owned;
}
static void free_owned(owned_event *e) {
  if (e == NULL) return;
  free(e->kind); free(e->source); free(e->participant); free(e->event); free(e->award); free(e->map); free(e->rules);
  free(e);
}
static void free_owned_node(void *arg) { free_owned(arg); }
static bool same_identity(const qa_app_player_progress_event *a, const qa_app_player_progress_event *b) {
  return strcmp(a->source, b->source) == 0 && strcmp(a->participant, b->participant) == 0
      && strcmp(a->event, b->event) == 0;
}
static void push(qa_app_player_progress_store *store, owned_event *event) {
  if (store->count == store->capacity) {
    size_t cap = store->capacity == 0 ? 8 : store->capacity * 2;
    owned_event **next = realloc(store->events, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    store->events = next; store->capacity = cap;
  }
  store->events[store->count++] = event;
}
static void clear_events(qa_app_player_progress_store *store) {
  for (size_t i = 0; i < store->count; i++) free_owned(store->events[i]);
  store->count = 0;
}
static void append_json_string(FILE *out, const char *text) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    switch (*p) {
      case '"': fputs("\\\"", out); break;
      case '\\': fputs("\\\\", out); break;
      case '\n': fputs("\\n", out); break;
      case '\r': fputs("\\r", out); break;
      case '\t': fputs("\\t", out); break;
      case '\b': fputs("\\b", out); break;
      case '\f': fputs("\\f", out); break;
      default:
        if (*p < 0x20) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
        break;
    }
  }
  fputc('"', out);
}
/* JSON.stringify({ version: 2, events: pending }) + "\n": each event in its
 * decoded key order. */
static void write_events(FILE *out, qa_app_player_progress_store *store, const owned_event *extra) {
  fputs("{\"version\":2,\"events\":[", out);
  size_t total = store->count + (extra != NULL ? 1 : 0);
  for (size_t i = 0; i < total; i++) {
    const qa_app_player_progress_event *e = i < store->count ? &store->events[i]->view : &extra->view;
    if (i != 0) fputc(',', out);
    fputs("{\"kind\":", out); append_json_string(out, e->kind);
    fputs(",\"source\":", out); append_json_string(out, e->source);
    fputs(",\"participant\":", out); append_json_string(out, e->participant);
    fputs(",\"event\":", out); append_json_string(out, e->event);
    if (is(e->kind, "achievement")) { fputs(",\"award\":", out); append_json_string(out, e->award); }
    else {
      fputs(",\"map\":", out); append_json_string(out, e->map);
      if (is(e->kind, "match-completed") || is(e->kind, "ranked-match")) fprintf(out, ",\"score\":%s", QA_JS_NUMBER(e->score));
      if (is(e->kind, "ranked-match")) {
        fputs(e->won ? ",\"won\":true" : ",\"won\":false", out);
        fputs(",\"rules\":", out); append_json_string(out, e->rules);
      }
    }
    fputc('}', out);
  }
  fputs("]}\n", out);
}
/* The Node fs error text "<CODE>: <description>, <syscall> '<path>'". */
static void node_errno_text(int error, const char **code, const char **description) {
  switch (error) {
    case EACCES: *code = "EACCES"; *description = "permission denied"; return;
    case EPERM: *code = "EPERM"; *description = "operation not permitted"; return;
    case ENOENT: *code = "ENOENT"; *description = "no such file or directory"; return;
    case ENOTDIR: *code = "ENOTDIR"; *description = "not a directory"; return;
    case EISDIR: *code = "EISDIR"; *description = "illegal operation on a directory"; return;
    case EEXIST: *code = "EEXIST"; *description = "file already exists"; return;
    case EROFS: *code = "EROFS"; *description = "read-only file system"; return;
    case ENOSPC: *code = "ENOSPC"; *description = "no space left on device"; return;
    case ENAMETOOLONG: *code = "ENAMETOOLONG"; *description = "name too long"; return;
    case ELOOP: *code = "ELOOP"; *description = "too many symbolic links encountered"; return;
    default: *code = "EIO"; *description = strerror(error); return;
  }
}
static noreturn void raise_fs(int error, const char *syscall, const char *path) {
  const char *code, *description;
  node_errno_text(error, &code, &description);
  qa_raise("Error", 0, 0, "%s: %s, %s '%s'", code, description, syscall, path);
}
/* mkdir(dirname(file), { recursive: true }) */
static void mkdir_parent(const char *file) {
  const char *slash = strrchr(file, '/');
  if (slash == NULL || slash == file) return;
  size_t length = (size_t)(slash - file);
  char *dir = malloc(length + 1);
  if (dir == NULL) qa_range_error("Out of memory");
  memcpy(dir, file, length);
  dir[length] = '\0';
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, dir);
  char *copy = malloc(length + 1);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, dir, length + 1);
  qa_cleanup copy_guard;
  qa_cleanup_push(&copy_guard, free, copy);
  for (size_t i = 1; i <= length; i++) {
    if (copy[i] != '/' && copy[i] != '\0') continue;
    char saved = copy[i];
    copy[i] = '\0';
    struct stat info;
    if (stat(copy, &info) != 0) {
      if (mkdir(copy, 0777) != 0 && errno != EEXIST) raise_fs(errno, "mkdir", dir);
    } else if (!S_ISDIR(info.st_mode)) {
      raise_fs(saved == '\0' ? EEXIST : ENOTDIR, "mkdir", dir);
    }
    copy[i] = saved;
  }
  qa_cleanup_pop(&copy_guard);
  free(copy);
  qa_cleanup_pop(&guard);
  free(dir);
}
/* The TS try block over a parsed document: false (store left empty) on any
 * shape error, as the TS catch clears the events. */
static bool load_events(qa_app_player_progress_store *store, const qa_save_value *value) {
  const qa_save_value *version = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "version") : NULL;
  const qa_save_value *events = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "events") : NULL;
  if (version == NULL || version->kind != QA_SAVE_NUMBER || (version->number != 1 && version->number != 2)
      || events == NULL || events->kind != QA_SAVE_ARRAY)
    return false;
  for (size_t i = 0; i < events->item_count; i++) {
    const qa_save_value *item = events->items[i];
    if (item->kind != QA_SAVE_OBJECT) return false;
    const qa_save_value *score = qa_save_value_get(item, "score");
    const qa_save_value *won = qa_save_value_get(item, "won");
    qa_app_player_progress_event e;
    memset(&e, 0, sizeof e);
    e.kind = str_field(item, "kind"); e.source = str_field(item, "source");
    e.participant = str_field(item, "participant"); e.event = str_field(item, "event");
    e.award = str_field(item, "award"); e.map = str_field(item, "map");
    e.score = score != NULL && score->kind == QA_SAVE_NUMBER ? score->number : NAN;
    bool has_won = won != NULL && won->kind == QA_SAVE_BOOLEAN;
    e.won = has_won && won->boolean;
    e.rules = str_field(item, "rules");
    qa_app_player_progress_event decoded;
    if (decode(e, has_won, &decoded) != NULL) return false;
    for (size_t j = 0; j < store->count; j++)
      if (same_identity(&store->events[j]->view, &decoded)) return false;
    push(store, own_event(decoded));
  }
  return true;
}
qa_app_player_progress_store *qa_app_player_progress_store_open(const char *file) {
  qa_app_player_progress_store *store = calloc(1, sizeof *store);
  if (store == NULL) qa_range_error("Out of memory");
  store->file = dup_text(file);
  /* readFile(file, "utf8"): ENOENT opens empty; any other failure (a directory reads EISDIR) rejects. */
  int fd = open(file, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    int code = errno;
    if (code == ENOENT) return store;
    qa_app_player_progress_store_destroy(store);
    raise_fs(code, "open", file);
  }
  size_t capacity = 4096, length = 0;
  uint8_t *bytes = malloc(capacity);
  if (bytes == NULL) qa_range_error("Out of memory");
  int read_error = 0;
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *next = realloc(bytes, capacity);
      if (next == NULL) qa_range_error("Out of memory");
      bytes = next;
    }
    ssize_t got = read(fd, bytes + length, capacity - length);
    if (got < 0) {
      if (errno == EINTR) continue;
      read_error = errno;
      break;
    }
    if (got == 0) break;
    length += (size_t)got;
  }
  close(fd);
  if (read_error != 0) {
    free(bytes);
    qa_app_player_progress_store_destroy(store);
    if (read_error == EISDIR) qa_raise("Error", 0, 0, "EISDIR: illegal operation on a directory, read");
    raise_fs(read_error, "read", file);
  }
  /* The "utf8" decoding keeps a byte-order mark as U+FEFF, which JSON.parse rejects (torn or
   * foreign text opens as empty progress); invalid sequences become U+FFFD. */
  if (length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
    free(bytes);
    return store;
  }
  size_t text_length = 0;
  char *text = qa_text_decoder_decode(bytes, length, &text_length);
  free(bytes);
  if (text == NULL) qa_range_error("Out of memory");
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, text_length, true);
  free(text);
  if (value == NULL) return store; /* torn or foreign text opens as empty progress */
  if (!load_events(store, value)) clear_events(store);
  qa_save_value_free(value);
  return store;
}
void qa_app_player_progress_store_destroy(qa_app_player_progress_store *store) {
  if (store == NULL) return;
  clear_events(store);
  free(store->events); free(store->all); free(store->file); free(store);
}
size_t qa_app_player_progress_store_list(qa_app_player_progress_store *store,
    const char *participant, const qa_app_player_progress_event ***out) {
  size_t count = 0;
  for (size_t i = 0; i < store->count; i++)
    if (strcmp(store->events[i]->view.participant, participant) == 0) count++;
  if (count == 0) { *out = NULL; return 0; }
  const qa_app_player_progress_event **items = malloc(count * sizeof *items);
  if (items == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < store->count; i++)
    if (strcmp(store->events[i]->view.participant, participant) == 0) items[at++] = &store->events[i]->view;
  *out = items;
  return count;
}
const qa_app_player_progress_event *const *qa_app_player_progress_store_all(qa_app_player_progress_store *store,
    size_t *out_count) {
  if (store->count > store->all_capacity) {
    const qa_app_player_progress_event **next = realloc(store->all, store->count * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    store->all = next; store->all_capacity = store->count;
  }
  for (size_t i = 0; i < store->count; i++) store->all[i] = &store->events[i]->view;
  *out_count = store->count;
  return store->all;
}
bool qa_app_player_progress_store_record(qa_app_player_progress_store *store, qa_app_player_progress_event input) {
  qa_app_player_progress_event event;
  const char *invalid = decode(input, true, &event);
  if (invalid != NULL) qa_raise("Error", 0, 0, "%s", invalid);
  for (size_t i = 0; i < store->count; i++)
    if (same_identity(&store->events[i]->view, &event)) return false;
  owned_event *owned = own_event(event);
  qa_cleanup owned_guard;
  qa_cleanup_push(&owned_guard, free_owned_node, owned);
  mkdir_parent(store->file);
  size_t tlen = strlen(store->file) + strlen(".pending") + 1;
  char *temporary = malloc(tlen);
  if (temporary == NULL) qa_range_error("Out of memory");
  snprintf(temporary, tlen, "%s.pending", store->file);
  qa_cleanup temporary_guard;
  qa_cleanup_push(&temporary_guard, free, temporary);
  /* writeFile(temporary, text, { mode: 0o600 }): the mode applies (under the umask) when the file is created. */
  int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) raise_fs(errno, "open", temporary);
  FILE *out = fdopen(fd, "wb");
  if (out == NULL) {
    int code = errno;
    close(fd);
    raise_fs(code, "write", temporary);
  }
  write_events(out, store, owned);
  if (ferror(out)) {
    int code = errno;
    fclose(out);
    raise_fs(code, "write", temporary);
  }
  if (fclose(out) != 0) raise_fs(errno, "write", temporary);
  if (rename(temporary, store->file) != 0) {
    const char *code, *description;
    node_errno_text(errno, &code, &description);
    qa_raise("Error", 0, 0, "%s: %s, rename '%s' -> '%s'", code, description, temporary, store->file);
  }
  qa_cleanup_pop(&temporary_guard);
  free(temporary);
  qa_cleanup_pop(&owned_guard);
  push(store, owned);
  return true;
}
void qa_app_player_progress_store_flush(qa_app_player_progress_store *store) { (void)store; }
char *qa_app_player_progress_identity(qa_app_player_progress_event event) {
  /* JSON.stringify([a,b,c]) with the same escaping as the writer. */
  char *buffer = NULL;
  size_t size = 0;
  FILE *mem = open_memstream(&buffer, &size);
  if (mem == NULL) qa_range_error("Out of memory");
  fputc('[', mem);
  append_json_string(mem, event.source); fputc(',', mem);
  append_json_string(mem, event.participant); fputc(',', mem);
  append_json_string(mem, event.event);
  fputc(']', mem);
  fclose(mem);
  return buffer;
}
