/*
 * Port of src/main.ts: command dispatch through the options parser
 * (--version, --help, --list-content, weapon-behavior, --splash-child),
 * the crash log, the splash, the LLM settings, then openApplication for
 * a dedicated run or StartupApplication.open for a graphical one, run()
 * and close(). A raise (TS throw) reaches main's catch: crash.log, the
 * failure on stderr, exit status 1.
 * GPL-2.0-or-later.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/splash.h"
#include "qa/app/bootstrap/startup_application.h"
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/app/bootstrap/weapon_behavior_tool.h"
#include "qa/main_redaction.h"
#include "qa/content/archive.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/content/user_data.h"
#include "qa/foundation/bounded_queue.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/formats/images/png.h"
#include "qa/llm/settings.h"
#include "qa/platform/sdl.h"
#include "qa/version.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

/* ---- small helpers ---- */

static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy != NULL) memcpy(copy, text, length + 1);
  return copy;
}

/* Malloc'd "a/b"; either side may be empty. */
static char *join_path(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  bool slash = la > 0 && a[la - 1] != '/';
  char *out = malloc(la + (slash ? 1 : 0) + lb + 1);
  if (out == NULL) return NULL;
  memcpy(out, a, la);
  size_t at = la;
  if (slash) out[at++] = '/';
  memcpy(out + at, b, lb + 1);
  return out;
}

/* Malloc'd dirname (node:path dirname, trailing slashes ignored). */
static char *dir_of(const char *path) {
  size_t length = strlen(path);
  while (length > 1 && path[length - 1] == '/') length--;
  size_t slash = length;
  while (slash > 0 && path[slash - 1] != '/') slash--;
  if (slash == 0) return dup_text(".");
  if (slash == 1) return dup_text("/");
  char *out = malloc(slash);
  if (out == NULL) return NULL;
  memcpy(out, path, slash - 1);
  out[slash - 1] = '\0';
  return out;
}

static bool mkdir_p(const char *path) {
  char *copy = dup_text(path);
  if (copy == NULL) return false;
  for (char *p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      (void)mkdir(copy, 0777);
      *p = '/';
    }
  }
  bool ok = mkdir(copy, 0777) == 0 || errno == EEXIST;
  free(copy);
  return ok;
}

/* Malloc'd directory of the running executable (best effort). */
static char *exe_directory(void) {
  char buffer[4096];
  ssize_t length = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
  if (length <= 0) return dup_text(".");
  buffer[length] = '\0';
  return dir_of(buffer);
}

/* ---- recent output + crash log (TS writeCrashLog) ---- */

static qa_bounded_queue recent_output; /* char* chunks, capacity 256 */

static void output_push(const char *text) {
  char *copy = dup_text(text);
  if (copy == NULL) return;
  if (recent_output.count == recent_output.capacity) {
    const char *const *oldest = qa_bounded_queue_peek_oldest(&recent_output);
    if (oldest != NULL) free((void *)*oldest);
  }
  qa_bounded_queue_push(&recent_output, &copy);
}

/* TS host.print: application output, kept for the crash log. */
static void print_text(const char *text) {
  output_push(text);
  fputs(text, stdout);
  fflush(stdout);
}

static void append_json_string(FILE *file, const char *text) {
  fputc('"', file);
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    switch (*p) {
      case '"': fputs("\\\"", file); break;
      case '\\': fputs("\\\\", file); break;
      case '\n': fputs("\\n", file); break;
      case '\r': fputs("\\r", file); break;
      case '\t': fputs("\\t", file); break;
      default:
        if (*p < 0x20) fprintf(file, "\\u%04x", *p);
        else fputc(*p, file);
    }
  }
  fputc('"', file);
}

/* CRASH_LOG_LIMIT_BYTES: past it the log becomes crash.log.1 so a crash loop cannot fill the disk. */
#define CRASH_LOG_LIMIT_BYTES (1024 * 1024)
static void write_crash_log(const char *message, int argc, char **argv) {
  char *user_root = qa_default_user_content_root();
  char *directory = user_root != NULL ? dir_of(user_root) : NULL;
  if (directory == NULL) { free(user_root); return; }
  (void)mkdir_p(directory);
  char *path = join_path(directory, "crash.log");
  /* Tail of the bounded queue, last 32768 chars like TS, then redactOutput. */
  size_t count = recent_output.count, total = 0;
  for (size_t i = 0; i < count; i++) {
    const char *const *chunk = qa_bounded_queue_at(&recent_output, i);
    if (chunk != NULL) total += strlen(*chunk);
  }
  size_t skip = total > 32768 ? total - 32768 : 0;
  char *tail = malloc(total - skip + 1);
  if (tail != NULL) {
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
      const char *const *chunk = qa_bounded_queue_at(&recent_output, i);
      if (chunk == NULL) continue;
      size_t length = strlen(*chunk);
      if (skip >= length) { skip -= length; continue; }
      memcpy(tail + used, *chunk + skip, length - skip);
      used += length - skip;
      skip = 0;
    }
    tail[used] = 0;
  }
  char *redacted_tail = tail != NULL ? qa_redact_output(tail) : NULL;
  const char **arguments = qa_redact_arguments(argc > 0 ? (size_t)argc : 0, (const char *const *)argv);
  struct stat info;
  if (path != NULL && stat(path, &info) == 0 && info.st_size > CRASH_LOG_LIMIT_BYTES) {
    size_t length = strlen(path);
    char *previous = malloc(length + 3);
    if (previous != NULL) {
      memcpy(previous, path, length);
      memcpy(previous + length, ".1", 3);
      (void)rename(path, previous);
      free(previous);
    }
  }
  int descriptor = path != NULL && redacted_tail != NULL && arguments != NULL ? open(path, O_WRONLY | O_CREAT | O_APPEND, 0600) : -1;
  FILE *file = descriptor >= 0 ? fdopen(descriptor, "a") : NULL;
  if (file == NULL && descriptor >= 0) (void)close(descriptor);
  if (file != NULL) {
    struct timespec now;
    (void)clock_gettime(CLOCK_REALTIME, &now);
    struct tm utc;
    char stamp[40];
    if (gmtime_r(&now.tv_sec, &utc) != NULL) {
      size_t used = strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &utc);
      (void)snprintf(stamp + used, sizeof stamp - used, ".%03ldZ",
                     now.tv_nsec / 1000000L);
    } else stamp[0] = '\0';
    char entry[4096];
    ssize_t entry_length = readlink("/proc/self/exe", entry, sizeof entry - 1);
    entry[entry_length > 0 ? entry_length : 0] = '\0';
    fprintf(file, "\n=== Quake Anthology %s crash %s entry=%s argv=[", qa_version, stamp, entry);
    for (int i = 0; i < argc; i++) {
      if (i > 0) fputc(',', file);
      append_json_string(file, arguments[i]);
    }
    fprintf(file, "]\n%s\n--- recent output ---\n%s\n", message, redacted_tail);
    (void)fchmod(fileno(file), 0600);
    (void)fclose(file);
  }
  free(arguments);
  free(redacted_tail);
  free(tail);
  free(path);
  free(directory);
  free(user_root);
}

/* TS llmSettingsDirectory: dirname(defaultUserContentRoot())/settings. */
static char *llm_settings_directory(void) {
  char *user_root = qa_default_user_content_root();
  if (user_root == NULL) return NULL;
  char *parent = dir_of(user_root);
  free(user_root);
  if (parent == NULL) return NULL;
  char *out = join_path(parent, "settings");
  free(parent);
  return out;
}

/* copyFileSync(source, destination, COPYFILE_EXCL); false is the TS throw. */
static bool copy_file_exclusive(const char *from, const char *to) {
  int source = open(from, O_RDONLY);
  if (source < 0) return false;
  int target = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (target < 0) { (void)close(source); return false; }
  bool ok = true;
  char buffer[8192];
  for (;;) {
    ssize_t read_count = read(source, buffer, sizeof buffer);
    if (read_count < 0) ok = false;
    if (read_count <= 0) break;
    ssize_t written = 0;
    while (written < read_count) {
      ssize_t step = write(target, buffer + written,
                           (size_t)(read_count - written));
      if (step <= 0) { ok = false; break; }
      written += step;
    }
    if (!ok) break;
  }
  (void)close(source);
  if (close(target) != 0) ok = false;
  return ok;
}

/* Settings once lived beside the executable. They are imported a single time, and never from the working
 * directory. TS takes the legacy folder from the compiled executable only; this program is always compiled. */
static void migrate_legacy_llm_settings(const char *legacy, const char *llm_directory) {
  if (legacy == NULL || strcmp(legacy, llm_directory) == 0) return;
  char *marker = join_path(llm_directory, ".legacy-migrated");
  if (marker == NULL) return;
  if (access(marker, F_OK) == 0 || !mkdir_p(llm_directory)) { free(marker); return; }
  static const char *const names[] = {"llm.json", "chatgpt.key", "other.key",
                                      "other.service"};
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    char *from = join_path(legacy, names[i]);
    char *to = join_path(llm_directory, names[i]);
    /* Missing legacy file, already migrated, or a read-only folder: the next name is tried. */
    if (from != NULL && to != NULL && copy_file_exclusive(from, to) && chmod(to, 0600) == 0)
      (void)unlink(from);
    free(from);
    free(to);
  }
  int descriptor = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (descriptor >= 0) (void)close(descriptor);
  free(marker);
}

/* ---- splash host (SDL window owned by this process) ---- */

typedef struct splash_window {
  qa_sdl_window window;
  bool open;
  uint8_t *frame;
  size_t frame_length;
  bool stdin_closed;
} splash_window;

static bool splash_load_title(void *ctx, qa_png_image *out) {
  (void)ctx;
  char *exe_dir = exe_directory();
  char *candidates[3];
  candidates[0] = exe_dir != NULL ? join_path(exe_dir, "assets/ui/title.png") : NULL;
  candidates[1] = dup_text("assets/ui/title.png");
  candidates[2] = dup_text("../quake-anthology-ts/assets/ui/title.png");
  bool loaded = false;
  for (int i = 0; i < 3 && !loaded; i++) {
    if (candidates[i] == NULL) continue;
    FILE *file = fopen(candidates[i], "rb");
    if (file == NULL) continue;
    (void)fseek(file, 0, SEEK_END);
    long size = ftell(file);
    (void)fseek(file, 0, SEEK_SET);
    if (size > 0) {
      uint8_t *bytes = malloc((size_t)size);
      if (bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size)
        loaded = qa_decode_png(bytes, (size_t)size, "assets/ui/title.png", out) != 0;
      free(bytes);
    }
    (void)fclose(file);
  }
  for (int i = 0; i < 3; i++) free(candidates[i]);
  free(exe_dir);
  return loaded;
}

static bool splash_open_window(void *ctx, double width, double height,
                               const uint8_t *rgba, size_t length) {
  splash_window *splash = ctx;
  qa_sdl_window_options options = {0};
  options.title = "Quake Anthology";
  options.width = width;
  options.height = height;
  options.backend = QA_SDL_BACKEND_CPU;
  if (!qa_sdl_window_open(&options, &splash->window)) return false;
  splash->open = true;
  free(splash->frame);
  splash->frame = NULL;
  splash->frame_length = 0;
  if (rgba != NULL && length > 0) {
    splash->frame = malloc(length);
    if (splash->frame != NULL) {
      memcpy(splash->frame, rgba, length);
      splash->frame_length = length;
    }
  }
  return true;
}

static bool splash_poll_quit(void *ctx) {
  splash_window *splash = ctx;
  if (splash->stdin_closed) return true;
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
  if (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
    char buffer[256];
    ssize_t got = read(STDIN_FILENO, buffer, sizeof buffer);
    if (got <= 0) splash->stdin_closed = true;
  }
  if (splash->stdin_closed) return true;
  size_t count = 0;
  qa_sdl_event *events = qa_sdl_window_poll_events(&splash->window, &count);
  bool quit = false;
  for (size_t i = 0; i < count; i++)
    if (events[i].kind == QA_SDL_EVENT_QUIT) quit = true;
  free(events);
  return quit || qa_sdl_window_closed(&splash->window);
}

static void splash_present(void *ctx) {
  splash_window *splash = ctx;
  if (splash->frame != NULL)
    qa_sdl_window_present(&splash->window, splash->frame, splash->frame_length);
}

static void splash_close_window(void *ctx) {
  splash_window *splash = ctx;
  if (!splash->open) return;
  splash->open = false;
  qa_sdl_window_close(&splash->window);
  free(splash->frame);
  splash->frame = NULL;
  splash->frame_length = 0;
}

static void splash_sleep(void *ctx, double ms) {
  (void)ctx;
  if (ms <= 0) return;
  struct timespec delay = {(time_t)(ms / 1000.0),
                           (long)((ms - (double)(time_t)(ms / 1000.0) * 1000.0) * 1000000.0)};
  (void)nanosleep(&delay, NULL);
}

static void splash_write_stdout(void *ctx, const char *text) {
  (void)ctx;
  fputs(text, stdout);
  fflush(stdout);
}

static double splash_now_ms(void *ctx) {
  (void)ctx;
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static qa_app_splash_host splash_host_for(splash_window *window) {
  qa_app_splash_host host = {0};
  host.ctx = window;
  host.open_window = splash_open_window;
  host.poll_quit = splash_poll_quit;
  host.present = splash_present;
  host.close_window = splash_close_window;
  host.sleep_ms = splash_sleep;
  host.write_stdout = splash_write_stdout;
  host.now_ms = splash_now_ms;
  host.load_title_image = splash_load_title;
  return host;
}

/* Parent-side splash: fork/exec self as --splash-child (TS Bun.spawn). */

typedef struct splash_child {
  pid_t pid;
  int stdin_fd;
} splash_child;

static bool splash_child_open(void *ctx, double width, double height,
                              const uint8_t *rgba, size_t length) {
  (void)rgba; (void)length;
  splash_child *child = ctx;
  int fds[2];
  if (pipe(fds) != 0) return false;
  pid_t pid = fork();
  if (pid < 0) { (void)close(fds[0]); (void)close(fds[1]); return false; }
  if (pid == 0) {
    (void)dup2(fds[0], STDIN_FILENO);
    (void)close(fds[0]);
    (void)close(fds[1]);
    (void)setenv("QTS_SPLASH_CHILD", "1", 1);
    char w[32], h[32];
    (void)snprintf(w, sizeof w, "%s", QA_JS_NUMBER(width));
    (void)snprintf(h, sizeof h, "%s", QA_JS_NUMBER(height));
    execl("/proc/self/exe", "quake-anthology", "--splash-child", w, h,
          (char *)NULL);
    _exit(127);
  }
  (void)close(fds[0]);
  child->pid = pid;
  child->stdin_fd = fds[1];
  return true;
}

static void splash_child_close(void *ctx) {
  splash_child *child = ctx;
  if (child->stdin_fd >= 0) (void)close(child->stdin_fd);
  child->stdin_fd = -1;
  if (child->pid > 0) (void)waitpid(child->pid, NULL, 0);
  child->pid = -1;
}

static bool splash_child_true(void *ctx) { (void)ctx; return true; }
static void splash_child_void(void *ctx) { (void)ctx; }
static void splash_child_sleep(void *ctx, double ms) { splash_sleep(ctx, ms); }
static bool splash_child_no_image(void *ctx, qa_png_image *out) {
  (void)ctx; (void)out; return false;
}

/* ---- options host ---- */

static void weapon_behavior_box_destroy(void *command) {
  qa_app_weapon_behavior_tool_command_free(
      (qa_app_weapon_behavior_tool_command *)command);
  free(command);
}

static void *parse_weapon_behavior(void *ctx, const char *const *argv,
                                   size_t argc, void (**destroy)(void *)) {
  (void)ctx;
  qa_app_weapon_behavior_tool_command *boxed = malloc(sizeof *boxed);
  if (boxed == NULL) return NULL;
  *boxed = qa_app_parse_weapon_behavior_tool(argv, argc);
  *destroy = weapon_behavior_box_destroy;
  return boxed;
}

/* ---- list-content ---- */

static const char *availability_name(qa_catalog_availability_kind kind) {
  switch (kind) {
    case QA_CATALOG_INSTALLED: return "installed";
    case QA_CATALOG_MISSING: return "missing";
    case QA_CATALOG_UNRESOLVED: return "unresolved";
  }
  return "unresolved";
}

static int run_list_content(const char *corpus_root) {
  char *user_root = qa_default_user_content_root();
  qa_nightdive_addon_root *addons = NULL;
  size_t addon_count = 0;
  (void)qa_default_addon_content_roots(NULL, &addons, &addon_count);
  qa_discover_content_options options = {0};
  options.corpus_root = corpus_root;
  options.user_content_root = user_root;
  options.addon_content_roots =
      (const qa_catalog_nightdive_addon_root *)addons;
  options.addon_content_root_count = addon_count;
  options.discover_mods = true;
  options.inspector = qa_app_default_archive_inspector();
  qa_content_catalog catalog = {0};
  int result = 1;
  if (qa_discover_installed_content(&options, &catalog)) {
    for (size_t i = 0; i < catalog.product_count; i++) {
      const qa_catalog_discovered_product *product = &catalog.products[i];
      printf("%s\t%s\t%s\n", product->expectation.id,
             availability_name(product->availability.kind),
             product->expectation.title);
    }
    result = 0;
  }
  qa_content_catalog_destroy(&catalog);
  qa_nightdive_addon_roots_free(addons, addon_count);
  free(user_root);
  return result;
}

/* Malloc'd lowercase hex of the text bytes (TS Buffer hex). */
static char *hex_encode(const char *text) {
  static const char digits[] = "0123456789abcdef";
  size_t length = strlen(text);
  char *out = malloc(length * 2 + 1);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = digits[((unsigned char)text[i] >> 4) & 0xf];
    out[i * 2 + 1] = digits[(unsigned char)text[i] & 0xf];
  }
  out[length * 2] = '\0';
  return out;
}

/* ---- SIGINT/SIGTERM -> requestQuit (TS main.ts stop handler) ---- */

static qa_application *signal_application;
static qa_startup_application *signal_startup;
static void signal_handler(int signum) {
  (void)signum;
  if (signal_application != NULL) qa_application_request_quit(signal_application);
  if (signal_startup != NULL) qa_startup_application_request_quit(signal_startup);
}
static void install_signals(struct sigaction *old_int, struct sigaction *old_term) {
  struct sigaction action = {0};
  action.sa_handler = signal_handler;
  (void)sigemptyset(&action.sa_mask);
  (void)sigaction(SIGINT, &action, old_int);
  (void)sigaction(SIGTERM, &action, old_term);
}
static void restore_signals(const struct sigaction *old_int, const struct sigaction *old_term) {
  (void)sigaction(SIGINT, old_int, NULL);
  (void)sigaction(SIGTERM, old_term, NULL);
  signal_application = NULL;
  signal_startup = NULL;
}

/* ---- run/menu (TS main.ts: openApplication or StartupApplication.open, run, close) ---- */

/* TS describeFailure: the first line of error.stack is "<name>: <message>"; C has no stack, aggregate or cause. */
static char *describe_failure(const qa_caught_error *error) {
  const char *name = error->name != NULL ? error->name : "Error";
  size_t length = strlen(name) + 2 + strlen(error->message) + 1;
  char *text = malloc(length);
  if (text != NULL) (void)snprintf(text, length, "%s: %s", name, error->message);
  return text;
}

typedef struct main_state {
  int argc;
  char **argv;
  qa_app_application_command command;
  bool parsed;
  char *exe_dir;
  splash_child child;
  qa_app_splash_screen *splash;
  qa_llm_settings_service *llm; /* NULL is TS undefined */
  qa_llm_command_requester requester;
  qa_application *application;      /* dedicated */
  qa_startup_application *startup;  /* graphical */
  int result;
} main_state;

/* TS host.print: the recent-output queue and stdout. */
static void host_print(void *ctx, const char *text) {
  (void)ctx;
  print_text(text);
}
/* TS host.llm as the LlmCommandRequester the console reads. */
static char *llm_request(void *ctx, const qa_llm_request_input *input, char **out_error) {
  qa_llm_error error;
  memset(&error, 0, sizeof error);
  char *text = NULL;
  if (qa_llm_settings_request(ctx, input, &text, &error)) return text;
  *out_error = dup_text(error.message);
  return NULL;
}
static void llm_prime(void *ctx, const char *instructions) {
  qa_llm_error error;
  memset(&error, 0, sizeof error);
  (void)qa_llm_settings_prime(ctx, instructions, &error);
}

static void application_run_body(void *ctx) {
  main_state *state = ctx;
  if (state->application != NULL) qa_application_run(state->application);
  else qa_startup_application_run(state->startup);
}
static void application_close_body(void *ctx) {
  main_state *state = ctx;
  if (state->application != NULL) qa_application_close(state->application);
  else qa_startup_application_close(state->startup);
}

/* The TS block inside `try { ... } finally { await llm?.close(); }`. */
static void application_body(void *ctx) {
  main_state *state = ctx;
  const qa_app_options *options = &state->command.options;
  qa_application_host host;
  memset(&host, 0, sizeof host);
  host.print = host_print;
  if (state->llm != NULL) {
    state->requester.ctx = state->llm;
    state->requester.request = llm_request;
    state->requester.prime = llm_prime;
    host.llm_settings = state->llm;
    host.llm_requester = &state->requester;
  }
  if (options->dedicated) {
    state->application = qa_application_open_application(*options, host, NULL, NULL);
  } else {
    qa_startup_application_host startup_host;
    memset(&startup_host, 0, sizeof startup_host);
    startup_host.base = host;
    state->startup = qa_startup_application_open(options, startup_host, NULL, state->command.kind);
  }
  qa_app_splash_screen_close(state->splash);
  state->splash = NULL;
  struct sigaction old_int, old_term;
  signal_application = state->application;
  signal_startup = state->startup;
  install_signals(&old_int, &old_term);
  qa_caught_error run_error, close_error;
  bool failed = !qa_try(application_run_body, state, &run_error);
  restore_signals(&old_int, &old_term);
  /* A close failure after a fatal error is reported without replacing that error (APP-006). */
  bool closed = qa_try(application_close_body, state, &close_error);
  if (!closed && failed) {
    char *text = describe_failure(&close_error);
    fprintf(stderr, "Shutdown after failure also failed: %s\n", text != NULL ? text : close_error.message);
    free(text);
  }
  /* TS garbage collection of the closed application. */
  if (closed) {
    if (state->application != NULL) qa_application_destroy(state->application);
    else qa_startup_application_destroy(state->startup);
  }
  state->application = NULL;
  state->startup = NULL;
  if (failed) qa_rethrow(&run_error);
  if (!closed) qa_rethrow(&close_error);
}

/* ---- weapon-behavior tool host (TS runWeaponBehaviorTool) ---- */

typedef struct weapon_host_state {
  qa_content_catalog catalog;
  bool catalog_open;
  qa_catalog_mount_list mounts;
  bool mounts_open;
  qa_mount_id *order;
  char *plan_id;
  qa_open_mount_options mount_options;
  qa_content_mounted *mounted;
} weapon_host_state;

static void weapon_print(void *ctx, const char *text) {
  (void)ctx;
  fputs(text, stdout);
  fflush(stdout);
}

/* discoverInstalledContent + catalog.require + mountsFor +
 * openMountPlan("weapon-authoring", hex(product.id)). require
 * aborts with the exact TS message, as the landed tool does. */
static const void *weapon_open_product(
    void *ctx, const qa_app_weapon_behavior_tool_command *command,
    const char **out_family, const char **out_edition,
    const char **out_expectation_id, const char **out_content_directory,
    const char **out_product_id) {
  weapon_host_state *state = ctx;
  qa_discover_content_options options = {0};
  options.corpus_root = command->corpus_root;
  options.user_content_root = command->user_content_root;
  options.discover_mods = true;
  options.inspector = qa_app_default_archive_inspector();
  if (!qa_discover_installed_content(&options, &state->catalog))
    qa_range_error("Out of memory");
  state->catalog_open = true;
  const qa_catalog_discovered_product *product =
      qa_content_catalog_require(&state->catalog, command->product);
  /* TS runWeaponBehaviorTool refuses a classic Quake II product before it mounts anything. */
  if (product->expectation.family == QA_GAME_FAMILY_Q2 &&
      (product->expectation.edition == NULL || strcmp(product->expectation.edition, "rerelease") != 0))
    qa_raise("Error", 0, 0, "%s",
             "Native weapon declarations currently require the Q2 rerelease API2023 Windows x64 adapter");
  if (!qa_content_catalog_mounts_for(&state->catalog, product->id, &state->mounts))
    qa_range_error("Out of memory");
  state->mounts_open = true;
  state->order = calloc(state->mounts.count > 0 ? state->mounts.count : 1,
                         sizeof *state->order);
  if (state->order == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < state->mounts.count; i++) {
    const qa_content_mount *mount = &state->mounts.mounts[i];
    state->order[i] = mount->kind == QA_CONTENT_MOUNT_ARCHIVE
                          ? mount->as.archive.identity.id
                          : mount->as.loose.identity.id;
  }
  char *hex = hex_encode(product->id);
  if (hex == NULL) qa_range_error("Out of memory");
  state->plan_id = qa_create_mount_plan_id("weapon-authoring", hex);
  free(hex);
  if (state->plan_id == NULL) qa_range_error("Out of memory");
  qa_resolved_mount_plan plan = {0};
  plan.id = state->plan_id;
  plan.mounts = state->mounts.mounts;
  plan.mount_count = state->mounts.count;
  plan.default_order = state->order;
  plan.default_order_count = state->mounts.count;
  state->mounted = qa_open_mount_plan(&plan, &state->mount_options);
  if (state->mounted == NULL) qa_range_error("Out of memory");
  *out_family = qa_game_family_name(product->expectation.family);
  *out_edition = product->expectation.edition;
  *out_expectation_id = product->expectation.id;
  *out_content_directory = product->expectation.content_directory;
  *out_product_id = product->id;
  return state->mounted;
}

static void weapon_close_product(void *ctx, const void *content) {
  (void)content;
  weapon_host_state *state = ctx;
  if (state->mounted != NULL) {
    qa_content_mounted_destroy(state->mounted);
    state->mounted = NULL;
  }
  free(state->order);
  state->order = NULL;
  free(state->plan_id);
  state->plan_id = NULL;
  if (state->mounts_open) {
    state->mounts_open = false;
    qa_content_catalog_mount_list_free(&state->mounts);
  }
  if (state->catalog_open) {
    state->catalog_open = false;
    qa_content_catalog_destroy(&state->catalog);
  }
}

/* The QC/QVM/native bodies live in weapon_behavior_tool.c and
 * call the landed content/compat/guest services directly over
 * the qa_content_mounted* from weapon_open_product; the host
 * run_* fields stay NULL (weapon_behavior_tool.h contract). */

/* ---- main ---- */

static bool parse_positive_integer(const char *text, double *out) {
  if (text == NULL || *text == '\0') return false;
  double value = qa_js_string_to_number(text); /* Number(argv[i]), then Number.isInteger and > 0 */
  if (!isfinite(value) || value != trunc(value) || value <= 0) return false;
  *out = value;
  return true;
}

static bool open_browser_url(void *ctx, const char *url,
                             qa_llm_abort_signal *signal,
                             qa_llm_error *error) {
  (void)ctx;
  (void)error;
  if (signal != NULL && signal->aborted) return true;
  qa_open_sdl_url(url);
  return true;
}

static char *resolve_path(void *ctx, const char *path) {
  (void)ctx;
  return qa_node_path_resolve1(path); /* node:path resolve(value) */
}

static void parse_body(void *ctx) {
  main_state *state = ctx;
  qa_content_root_search search = {0};
  search.own_directory = state->exe_dir;
  search.home_directory = getenv("HOME");
  qa_app_options_host options_host = {0};
  options_host.parse_weapon_behavior_tool = parse_weapon_behavior;
  options_host.resolve_path = resolve_path;
  state->command = qa_app_parse_application_command(
      (const char *const *)(state->argv + 1), state->argc > 0 ? (size_t)(state->argc - 1) : 0,
      search, &options_host);
  state->parsed = true;
}

/* TS applicationHelp for the compiled executable: it has no `bun run`, so name the program the user started. */
static void print_help(void) {
  static const char source_name[] = "bun run src/main.ts";
  const char *help = qa_app_application_help;
  const char *at = strstr(help, source_name);
  char path[4096];
  ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1);
  if (at == NULL || length <= 0) {
    fputs(help, stdout);
    return;
  }
  path[length] = '\0';
  const char *base = strrchr(path, '/');
  base = base != NULL ? base + 1 : path;
  (void)fwrite(help, 1, (size_t)(at - help), stdout);
  fputs(base, stdout);
  fputs(at + sizeof source_name - 1, stdout);
}

/* The TS `try { ... }` body after the command line parsed; a raise reaches main's catch. */
static void command_body(void *ctx) {
  main_state *state = ctx;
  qa_app_application_command *command = &state->command;
  if (strcmp(command->kind, "version") == 0) {
    printf("Quake Anthology %s\n", qa_version);
    return;
  }
  if (strcmp(command->kind, "help") == 0) {
    print_help();
    return;
  }
  if (strcmp(command->kind, "weapon-behavior") == 0) {
    weapon_host_state tool = {0};
    qa_app_weapon_behavior_tool_host host = {0};
    host.ctx = &tool;
    host.open_product = weapon_open_product;
    host.close_product = weapon_close_product;
    /* run_qc/run_qvm/run_native stay NULL: tool bodies are direct. */
    qa_app_run_weapon_behavior_tool(
        host, *(qa_app_weapon_behavior_tool_command *)command->weapon_behavior_command,
        weapon_print, NULL);
    return;
  }
  if (strcmp(command->kind, "list-content") == 0) {
    if (run_list_content(command->corpus_root) != 0) qa_range_error("Out of memory");
    return;
  }
  /* run | menu */
  if (!command->options.dedicated && !command->options.hidden) {
    qa_app_splash_host host = {0};
    host.ctx = &state->child;
    host.open_window = splash_child_open;
    host.poll_quit = splash_child_true;
    host.present = splash_child_void;
    host.close_window = splash_child_close;
    host.sleep_ms = splash_child_sleep;
    host.write_stdout = splash_write_stdout;
    host.now_ms = splash_now_ms;
    host.load_title_image = splash_child_no_image;
    state->splash = qa_app_open_splash(host, command->options.width, command->options.height);
  }
  qa_app_startup_stamp("splash");
  /* LLM credentials are user settings, not game data: they live with the other shared settings. */
  char *llm_directory = llm_settings_directory();
  if (llm_directory == NULL) qa_range_error("Out of memory");
  qa_cleanup directory_cleanup;
  qa_cleanup_push(&directory_cleanup, free, llm_directory);
  migrate_legacy_llm_settings(state->exe_dir, llm_directory);
  if (!(strcmp(command->kind, "menu") != 0 && command->options.dedicated)) {
    qa_llm_settings_options llm_options = {0};
    llm_options.base_directory = llm_directory;
    llm_options.auth.open_browser = open_browser_url;
    state->llm = qa_llm_settings_open(&llm_options);
  }
  qa_app_startup_stamp("llm-settings");
  qa_caught_error caught;
  bool ok = qa_try(application_body, state, &caught);
  if (state->llm != NULL) { /* finally: await llm?.close() */
    qa_llm_settings_close(state->llm);
    qa_llm_settings_destroy(state->llm);
    state->llm = NULL;
  }
  qa_cleanup_pop(&directory_cleanup);
  free(llm_directory);
  if (!ok) qa_rethrow(&caught);
}

int main(int argc, char **argv) {
  (void)qa_bounded_queue_init(&recent_output, 256, sizeof(char *));
  if (argc > 1 && strcmp(argv[1], "--splash-child") == 0) {
    double width = 0, height = 0;
    if (argc < 4 || !parse_positive_integer(argv[2], &width) ||
        !parse_positive_integer(argv[3], &height))
      return 1;
    splash_window window = {0};
    return qa_app_run_splash_child(splash_host_for(&window), width, height);
  }
  /* A closed pipe (`| head`, a stopped log reader) is not a crash, so further output is dropped. */
  (void)signal(SIGPIPE, SIG_IGN);

  main_state state;
  memset(&state, 0, sizeof state);
  state.argc = argc;
  state.argv = argv;
  state.child.pid = -1;
  state.child.stdin_fd = -1;
  state.exe_dir = exe_directory();
  qa_caught_error caught;
  /* A command-line typo is a usage error, not a crash, so it leaves no crash log entry. */
  if (!qa_try(parse_body, &state, &caught)) {
    fprintf(stderr, "%s\n", caught.message);
    free(state.exe_dir);
    return 1;
  }
  int result = 0;
  if (!qa_try(command_body, &state, &caught)) {
    qa_app_splash_screen_close(state.splash);
    state.splash = NULL;
    char *text = describe_failure(&caught);
    write_crash_log(text != NULL ? text : caught.message, argc - 1, argv + 1);
    fprintf(stderr, "%s\n", text != NULL ? text : caught.message);
    free(text);
    result = 1;
  }
  qa_app_splash_screen_close(state.splash);
  qa_app_application_command_destroy(&state.command);
  free(state.exe_dir);
  return result;
}
