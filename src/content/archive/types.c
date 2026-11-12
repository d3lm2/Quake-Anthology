/* Port of src/content/archive/types.ts. GPL-2.0-or-later. */
#include "qa/content/archive/types.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS ArchiveError(source, offset, message): `${source}:${offset}: ${message}`,
 * raised so TS catch sites (mod discovery, mount fallbacks) recover it. */
noreturn void qa_archive_error(const char *source, int64_t offset,
                               const char *format, ...) {
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("ArchiveError", 0, (long long)offset, "%s:%lld: %s",
           source ? source : "", (long long)offset, message);
}

noreturn void qa_content_fs_error(int error_number, const char *syscall,
                                  const char *path) {
  /* libuv's names and descriptions, which Bun and Node print. */
  static const struct { int code; const char *name, *text; } known[] = {
      {ENOENT, "ENOENT", "no such file or directory"},
      {ENOTDIR, "ENOTDIR", "not a directory"},
      {EACCES, "EACCES", "permission denied"},
      {EPERM, "EPERM", "operation not permitted"},
      {EISDIR, "EISDIR", "illegal operation on a directory"},
      {ELOOP, "ELOOP", "too many symbolic links encountered"},
      {ENAMETOOLONG, "ENAMETOOLONG", "name too long"},
      {EEXIST, "EEXIST", "file already exists"},
      {EMFILE, "EMFILE", "too many open files"},
      {ENFILE, "ENFILE", "file table overflow"},
      {EIO, "EIO", "i/o error"},
      {ENOSPC, "ENOSPC", "no space left on device"},
      {EROFS, "EROFS", "read-only file system"},
      {EBUSY, "EBUSY", "resource busy or locked"},
      {EINVAL, "EINVAL", "invalid argument"},
      {ENOTEMPTY, "ENOTEMPTY", "directory not empty"},
      {EXDEV, "EXDEV", "cross-device link not permitted"},
      {ENOMEM, "ENOMEM", "not enough memory"},
      {EBADF, "EBADF", "bad file descriptor"},
      {EFBIG, "EFBIG", "file too large"},
      {ENXIO, "ENXIO", "no such device or address"},
      {ETXTBSY, "ETXTBSY", "text file is busy"},
      {EOVERFLOW, "EOVERFLOW", "value too large for defined data type"},
      {ENODEV, "ENODEV", "no such device"},
      {EAGAIN, "EAGAIN", "resource temporarily unavailable"},
      {EINTR, "EINTR", "interrupted system call"},
  };
  const char *name = "UNKNOWN", *text = strerror(error_number);
  for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
    if (known[i].code == error_number) {
      name = known[i].name;
      text = known[i].text;
      break;
    }
  if (path == NULL) qa_raise("Error", error_number, 0, "%s: %s, %s", name, text, syscall);
  qa_raise("Error", error_number, 0, "%s: %s, %s '%s'", name, text, syscall, path);
}

void qa_archive_check_range(const char *source, int64_t size, int64_t offset,
                            int64_t length) {
  /* int64_t inputs are already integers; the TS safe-integer bounds
   * still apply where observable. */
  if (offset < -QA_MAX_SAFE_INTEGER || offset > QA_MAX_SAFE_INTEGER ||
      length < -QA_MAX_SAFE_INTEGER || length > QA_MAX_SAFE_INTEGER ||
      offset < 0 || length < 0 || offset > size - length) {
    qa_archive_error(source, offset,
                     "range of %lld bytes exceeds %lld-byte source",
                     (long long)length, (long long)size);
  }
}

/* JSON.stringify of a byte string, for the RangeError message. */
static char *json_stringify(const char *value) {
  size_t length = strlen(value);
  char *out = (char *)malloc(length * 6 + 3);
  if (out == NULL) return NULL;
  size_t at = 0;
  out[at++] = '"';
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)value[i];
    switch (c) {
      case '"': out[at++] = '\\'; out[at++] = '"'; break;
      case '\\': out[at++] = '\\'; out[at++] = '\\'; break;
      case '\b': out[at++] = '\\'; out[at++] = 'b'; break;
      case '\f': out[at++] = '\\'; out[at++] = 'f'; break;
      case '\n': out[at++] = '\\'; out[at++] = 'n'; break;
      case '\r': out[at++] = '\\'; out[at++] = 'r'; break;
      case '\t': out[at++] = '\\'; out[at++] = 't'; break;
      default:
        if (c < 0x20) {
          at += (size_t)snprintf(out + at, 7, "\\u%04x", c);
        } else {
          out[at++] = (char)c;
        }
        break;
    }
  }
  out[at++] = '"';
  out[at] = '\0';
  return out;
}

static noreturn void unsafe_path(const char *path) {
  char *json = json_stringify(path != NULL ? path : "");
  qa_cleanup node;
  qa_cleanup_push(&node, free, json);
  qa_range_error("Unsafe archive member path: %s", json != NULL ? json : "\"\"");
}

char *qa_try_normalize_entry_path(const char *path, bool *out_unsafe) {
  *out_unsafe = false;
  if (path == NULL) path = "";
  size_t length = strlen(path);
  /* A NUL byte cannot occur in a C string; callers holding raw bytes test
   * for it before calling (TS includes("\0") returns null). */
  for (size_t i = 0; i < length; i++)
    if (path[i] == ':') {
      *out_unsafe = true;
      return NULL;
    }
  bool trailing = length > 0 && (path[length - 1] == '/' || path[length - 1] == '\\');
  /* Parts index into path; "" and "." segments collapse, ".." pops. */
  size_t part_capacity = length / 2 + 2;
  size_t *starts = (size_t *)malloc(part_capacity * sizeof *starts);
  size_t *lengths = (size_t *)malloc(part_capacity * sizeof *lengths);
  if (starts == NULL || lengths == NULL) {
    free(starts);
    free(lengths);
    return NULL;
  }
  size_t part_count = 0;
  size_t start = 0;
  bool bad = false;
  for (size_t i = 0; i <= length; i++) {
    if (i != length && path[i] != '/' && path[i] != '\\') continue;
    size_t part_length = i - start;
    const char *part = path + start;
    size_t part_start = start;
    start = i + 1;
    if (part_length == 0 || (part_length == 1 && part[0] == '.')) continue;
    if (part_length == 2 && part[0] == '.' && part[1] == '.') {
      if (part_count == 0) {
        bad = true;
        break;
      }
      part_count--;
    } else {
      starts[part_count] = part_start;
      lengths[part_count] = part_length;
      part_count++;
    }
  }
  if (bad || part_count == 0) {
    free(starts);
    free(lengths);
    *out_unsafe = true;
    return NULL;
  }
  size_t out_length = trailing ? 1 : 0;
  for (size_t i = 0; i < part_count; i++) out_length += lengths[i] + (i > 0 ? 1 : 0);
  char *out = (char *)malloc(out_length + 1);
  if (out == NULL) {
    free(starts);
    free(lengths);
    return NULL;
  }
  size_t at = 0;
  for (size_t i = 0; i < part_count; i++) {
    if (i > 0) out[at++] = '/';
    memcpy(out + at, path + starts[i], lengths[i]);
    at += lengths[i];
  }
  if (trailing) out[at++] = '/';
  out[at] = '\0';
  free(starts);
  free(lengths);
  return out;
}

char *qa_normalize_entry_path(const char *path) {
  bool unsafe = false;
  char *normalized = qa_try_normalize_entry_path(path, &unsafe);
  if (unsafe) unsafe_path(path);
  return normalized;
}

char *qa_compare_entry_path(const char *path,
                            qa_archive_path_comparison comparison) {
  char *normalized = qa_normalize_entry_path(path);
  if (normalized == NULL) return NULL;
  if (comparison == QA_ARCHIVE_PATH_EXACT) return normalized;
  for (size_t i = 0; normalized[i] != '\0'; i++) {
    unsigned char c = (unsigned char)normalized[i];
    if (c >= 'A' && c <= 'Z') {
      normalized[i] = (char)(c - 'A' + 'a');
    } else if (comparison == QA_ARCHIVE_PATH_CASE_INSENSITIVE &&
               c >= 0xc0 && c <= 0xde && c != 0xd7) {
      normalized[i] = (char)(c + 0x20); /* Latin-1 toLowerCase */
    }
  }
  return normalized;
}
