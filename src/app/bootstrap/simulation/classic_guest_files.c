/* Port of src/app/bootstrap/simulation/classic-guest-files.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/classic_guest_files.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/foundation/support.h"
#include "qa/content/archive/types.h"
#include "qa/platform/files/contained.h"

static bool safe_integer(double value) { return value == trunc(value) && fabs(value) <= 9007199254740991.0; }
void qa_app_sim_classic_guest_file_position(double offset, double length) {
  if (!safe_integer(offset) || offset < 0 || !safe_integer(length) || length < 0 || !safe_integer(offset + length))
    qa_range_error("Invalid native file range");
}
char *qa_app_sim_classic_guest_file_name(const char *path) {
  size_t n = strlen(path); char *out = malloc(n + 1); if (out == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < n; i++) out[i] = path[i] == '\\' ? '/' : path[i];
  out[n] = '\0';
  char *start = out; while (strncmp(start, "./", 2) == 0) start += 2;
  if (start != out) memmove(out, start, strlen(start) + 1);
  /* containedFileParts(name) refuses a path outside the root; the normalized name is released with the refusal. */
  qa_cleanup node; qa_cleanup_push(&node, free, out);
  size_t count = 0; char **parts = qa_contained_file_parts(out, &count);
  qa_cleanup_pop(&node);
  qa_contained_file_parts_free(parts, count);
  return out;
}
bool qa_app_sim_classic_guest_file_allowed(qa_app_sim_classic_guest_file_options options) {
  if (!(options.creation == trunc(options.creation)) || options.creation < 1 || options.creation > 5)
    qa_range_error("Invalid native file creation disposition");
  return options.write || options.creation == 3;
}

/* Files a DLL could use to run code or console commands in a later session, or to replace other content. */
static bool protected_write(const char *name) {
  static const char *const suffixes[] = {"cfg", "rc", "dll", "so", "dylib", "exe", "bat", "sh", "pak", "pk3", "qvm", "bsp"};
  size_t length = strlen(name);
  for (size_t i = 0; i < sizeof suffixes / sizeof *suffixes; i++) {
    size_t n = strlen(suffixes[i]);
    if (length > n && name[length - n - 1] == '.' && strcasecmp(name + length - n, suffixes[i]) == 0) return true;
  }
  return false;
}

typedef struct native_file {
  qa_windows_file iface;
  struct qa_app_sim_classic_guest_files *owner;
  bool mounted;
  /* mounted resource */
  const uint8_t *bytes; size_t length; bool closed;
  /* descriptor-backed */
  int fd; bool owned; bool read, write;
  struct native_file *next;
} native_file;
struct qa_app_sim_classic_guest_files {
  const char *root;
  const qa_app_sim_classic_guest_resource *resources; size_t resource_count;
  native_file *files;
};

static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static int fd_of(native_file *f) { if (!f->owned) fail("Native file is closed"); return f->fd; }
static size_t file_read(void *ctx, size_t offset, uint8_t *out, size_t length) {
  native_file *f = ctx;
  if (!f->read) fail("Native file is not readable");
  qa_app_sim_classic_guest_file_position((double)offset, (double)length);
  int fd = fd_of(f);
  ssize_t count = pread(fd, out, length, (off_t)offset);
  if (count < 0) qa_content_fs_error(errno, "read", NULL);
  return (size_t)count;
}
static int64_t file_write(void *ctx, size_t offset, const uint8_t *bytes, size_t length) {
  native_file *f = ctx;
  if (!f->write) fail("Native file is not writable");
  qa_app_sim_classic_guest_file_position((double)offset, (double)length);
  int fd = fd_of(f);
  ssize_t written = pwrite(fd, bytes, length, (off_t)offset);
  if (written < 0) qa_content_fs_error(errno, "write", NULL);
  return written;
}
static int64_t file_size(void *ctx) {
  native_file *f = ctx; struct stat st; int fd = fd_of(f);
  if (fstat(fd, &st) != 0) qa_content_fs_error(errno, "fstat", NULL);
  return (int64_t)st.st_size;
}
static void file_truncate(void *ctx, size_t length) {
  native_file *f = ctx;
  if (!f->write) fail("Native file is not writable");
  qa_app_sim_classic_guest_file_position((double)length, 0);
  int fd = fd_of(f);
  if (ftruncate(fd, (off_t)length) != 0) qa_content_fs_error(errno, "ftruncate", NULL);
}
static void file_flush(void *ctx) { native_file *f = ctx; int fd = fd_of(f); if (fsync(fd) != 0) qa_content_fs_error(errno, "fsync", NULL); }
static void file_close(void *ctx) { native_file *f = ctx; if (f->owned) { int current = f->fd; f->owned = false; f->fd = -1; close(current); } }

static void check_open(native_file *f) { if (f->closed) fail("Native file is closed"); }
static size_t mounted_read(void *ctx, size_t offset, uint8_t *out, size_t length) {
  native_file *f = ctx; check_open(f); qa_app_sim_classic_guest_file_position((double)offset, (double)length);
  if (offset >= f->length) return 0;
  size_t count = f->length - offset < length ? f->length - offset : length;
  memcpy(out, f->bytes + offset, count); return count;
}
static int64_t mounted_write(void *ctx, size_t offset, const uint8_t *bytes, size_t length) { (void)ctx; (void)offset; (void)bytes; (void)length; fail("Native mounted file is read-only"); }
static int64_t mounted_size(void *ctx) { native_file *f = ctx; check_open(f); return (int64_t)f->length; }
static void mounted_truncate(void *ctx, size_t length) { (void)ctx; (void)length; fail("Native mounted file is read-only"); }
static void mounted_flush(void *ctx) { check_open(ctx); }
static void mounted_close(void *ctx) { ((native_file *)ctx)->closed = true; }

qa_app_sim_classic_guest_files *qa_app_sim_classic_guest_files_create(const char *root, const qa_app_sim_classic_guest_resource *resources, size_t resource_count) {
  qa_app_sim_classic_guest_files *files = calloc(1, sizeof *files); if (files == NULL) return NULL;
  files->root = root; files->resources = resources; files->resource_count = resource_count;
  return files;
}
void qa_app_sim_classic_guest_files_destroy(qa_app_sim_classic_guest_files *files) {
  if (files == NULL) return;
  for (native_file *f = files->files; f != NULL;) { native_file *next = f->next; if (!f->mounted) file_close(f); free(f); f = next; }
  free(files);
}
static native_file *add_file(qa_app_sim_classic_guest_files *files) {
  native_file *f = calloc(1, sizeof *f); if (f == NULL) qa_range_error("Out of memory");
  f->owner = files; f->fd = -1; f->iface.ctx = f; f->next = files->files; files->files = f; return f;
}

typedef struct open_parent { const char *root, *name; bool create; int descriptor; char *leaf; } open_parent;
static void run_open_parent(void *ctx) { open_parent *p = ctx; p->descriptor = qa_open_contained_parent(p->root, p->name, p->create, &p->leaf); }
/* error.code is ENOENT, EEXIST, EACCES, ELOOP or ENOTDIR. The contained-file helpers and this file's own open and
 * fstat raise the Node text ("ENOTDIR: not a directory, open '...'"); an unlisted code is strerror text. */
static bool recoverable_message(const char *message) {
  static const int codes[] = {ENOENT, EEXIST, EACCES, ELOOP, ENOTDIR};
  static const char *const names[] = {"ENOENT: ", "EEXIST: ", "EACCES: ", "ELOOP: ", "ENOTDIR: "};
  for (size_t i = 0; i < sizeof codes / sizeof *codes; i++)
    if (strcmp(message, strerror(codes[i])) == 0 || strncmp(message, names[i], strlen(names[i])) == 0) return true;
  return false;
}
static qa_windows_file *mounted_or_null(qa_app_sim_classic_guest_files *files, const char *name, bool write, int creation) {
  if (write || creation != 3) return NULL;
  for (size_t i = 0; i < files->resource_count; i++) {
    if (strcmp(files->resources[i].name, name) != 0) continue;
    native_file *f = add_file(files); f->mounted = true; f->bytes = files->resources[i].bytes; f->length = files->resources[i].length;
    f->iface.read = mounted_read; f->iface.write = mounted_write; f->iface.size = mounted_size;
    f->iface.truncate = mounted_truncate; f->iface.flush = mounted_flush; f->iface.close = mounted_close;
    return &f->iface;
  }
  return NULL;
}
qa_windows_file *qa_app_sim_classic_guest_files_open(void *ctx, const char *path, bool read, bool write, int creation) {
  qa_app_sim_classic_guest_files *files = ctx;
  char *name = qa_app_sim_classic_guest_file_name(path);
  qa_app_sim_classic_guest_file_options options = {(double)creation, read, write};
  qa_cleanup name_node; qa_cleanup_push(&name_node, free, name);
  bool allowed = qa_app_sim_classic_guest_file_allowed(options);
  qa_cleanup_pop(&name_node);
  if (!allowed) { free(name); return NULL; }
  /* A refused open reads as a missing file to the guest. */
  if (write && protected_write(name)) { free(name); return NULL; }
  open_parent parent = {files->root, name, write && creation != 3 && creation != 5, -1, NULL};
  qa_caught_error caught;
  if (!qa_try(run_open_parent, &parent, &caught)) {
    if (!recoverable_message(caught.message)) { free(name); qa_rethrow(&caught); }
    qa_windows_file *mounted = mounted_or_null(files, name, write, creation);
    free(name); return mounted;
  }
  int flags = write ? (read ? O_RDWR : O_WRONLY) : O_RDONLY;
  if (creation == 1) flags |= O_CREAT | O_EXCL;
  if (creation == 2) flags |= O_CREAT | O_TRUNC;
  if (creation == 4) flags |= O_CREAT;
  if (creation == 5) flags |= O_TRUNC;
  /* TS openSync(`/proc/self/fd/${parent.descriptor}/${parent.leaf}`): Node names that path in its error. */
  int descriptor = openat(parent.descriptor, parent.leaf, flags | O_NOFOLLOW | O_NONBLOCK, 0600);
  int code = errno;
  const char *syscall = "open";
  struct stat st;
  if (descriptor >= 0 && fstat(descriptor, &st) != 0) { code = errno; syscall = "fstat"; close(descriptor); descriptor = -2; }
  if (descriptor < 0) {
    static const int codes[] = {ENOENT, EEXIST, EACCES, ELOOP, ENOTDIR};
    bool recoverable = false;
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) recoverable = recoverable || code == codes[i];
    if (!recoverable) {
      /* fstatSync(fd) names no path. */
      char *path_text = NULL;
      if (descriptor == -1) {
        size_t length = strlen(parent.leaf) + 40;
        path_text = malloc(length);
        if (path_text != NULL) (void)snprintf(path_text, length, "/proc/self/fd/%d/%s", parent.descriptor, parent.leaf);
      }
      close(parent.descriptor); free(parent.leaf); free(name);
      qa_cleanup node;
      if (path_text != NULL) qa_cleanup_push(&node, free, path_text);
      qa_content_fs_error(code, syscall, descriptor == -1 ? (path_text != NULL ? path_text : "") : NULL);
    }
    close(parent.descriptor); free(parent.leaf);
    qa_windows_file *mounted = mounted_or_null(files, name, write, creation);
    free(name); return mounted;
  }
  close(parent.descriptor); free(parent.leaf);
  free(name);
  if (!S_ISREG(st.st_mode)) { close(descriptor); return NULL; }
  native_file *f = add_file(files);
  f->fd = descriptor; f->owned = true; f->read = read; f->write = write;
  f->iface.read = file_read; f->iface.write = file_write; f->iface.size = file_size;
  f->iface.truncate = file_truncate; f->iface.flush = file_flush; f->iface.close = file_close;
  return &f->iface;
}
