/* node:path (POSIX) operations; see include/qa/foundation/node_path.h. GPL-2.0-or-later. */
#include "qa/foundation/node_path.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/foundation/support.h"

static void *checked(void *pointer) {
  if (pointer == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  return pointer;
}
static char *copy_range(const char *text, size_t length) {
  char *copy = (char *)checked(malloc(length + 1));
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

/* Node's normalizeString: resolves "." and ".." segments. With `allow_above_root`, leading ".." segments of a
 * relative path are kept. The result has no leading or trailing slash. */
static char *normalize_string(const char *path, bool allow_above_root) {
  size_t length = strlen(path);
  char *out = (char *)checked(malloc(length + 4));
  size_t out_length = 0;
  size_t last_segment_length = 0;
  size_t last_slash = (size_t)-1;
  size_t dots = 0;
  for (size_t i = 0; i <= length; i++) {
    char code = i < length ? path[i] : '/';
    if (i < length && code != '/') {
      if (code == '.' && dots != (size_t)-1) dots++;
      else dots = (size_t)-1;
      continue;
    }
    if (last_slash == i - 1 || dots == 1) {
      /* An empty segment or "." adds nothing. */
    } else if (dots == 2) {
      bool handled = false;
      if (out_length < 2 || last_segment_length != 2 || out[out_length - 1] != '.' || out[out_length - 2] != '.') {
        if (out_length > 2) {
          size_t slash = out_length;
          while (slash > 0 && out[slash - 1] != '/') slash--;
          if (slash == 0) {
            out_length = 0;
            last_segment_length = 0;
          } else {
            out_length = slash - 1;
            size_t previous = out_length;
            while (previous > 0 && out[previous - 1] != '/') previous--;
            last_segment_length = out_length - previous;
          }
          handled = true;
        } else if (out_length != 0) {
          out_length = 0;
          last_segment_length = 0;
          handled = true;
        }
      }
      if (!handled && allow_above_root) {
        if (out_length > 0) out[out_length++] = '/';
        out[out_length++] = '.';
        out[out_length++] = '.';
        last_segment_length = 2;
      }
    } else {
      if (out_length > 0) out[out_length++] = '/';
      size_t start = last_slash + 1;
      memcpy(out + out_length, path + start, i - start);
      out_length += i - start;
      last_segment_length = i - last_slash - 1;
    }
    last_slash = i;
    dots = 0;
  }
  out[out_length] = '\0';
  return out;
}

char *qa_node_path_normalize(const char *path) {
  size_t length = strlen(path);
  if (length == 0) return copy_range(".", 1);
  bool absolute = path[0] == '/';
  bool trailing = path[length - 1] == '/';
  char *body = normalize_string(path, !absolute);
  size_t body_length = strlen(body);
  if (body_length == 0) {
    free(body);
    if (absolute) return copy_range("/", 1);
    return trailing ? copy_range("./", 2) : copy_range(".", 1);
  }
  char *out = (char *)checked(malloc(body_length + 3));
  size_t at = 0;
  if (absolute) out[at++] = '/';
  memcpy(out + at, body, body_length);
  at += body_length;
  if (trailing) out[at++] = '/';
  out[at] = '\0';
  free(body);
  return out;
}

char *qa_node_path_join(const char *const *parts, size_t count) {
  size_t total = 1;
  for (size_t i = 0; i < count; i++) total += strlen(parts[i]) + 1;
  char *joined = (char *)checked(malloc(total));
  size_t at = 0;
  for (size_t i = 0; i < count; i++) {
    size_t length = strlen(parts[i]);
    if (length == 0) continue;
    if (at > 0) joined[at++] = '/';
    memcpy(joined + at, parts[i], length);
    at += length;
  }
  joined[at] = '\0';
  if (at == 0) {
    free(joined);
    return copy_range(".", 1);
  }
  char *out = qa_node_path_normalize(joined);
  free(joined);
  return out;
}
char *qa_node_path_join2(const char *a, const char *b) {
  const char *parts[2] = {a, b};
  return qa_node_path_join(parts, 2);
}
char *qa_node_path_join3(const char *a, const char *b, const char *c) {
  const char *parts[3] = {a, b, c};
  return qa_node_path_join(parts, 3);
}

char *qa_node_path_resolve(const char *const *parts, size_t count) {
  /* Right to left: parts are prepended until one is absolute; the working directory is the last resort. */
  size_t first = count;
  bool absolute = false;
  while (first > 0 && !absolute) {
    first--;
    absolute = parts[first][0] == '/';
  }
  char cwd[4096];
  cwd[0] = '\0';
  if (!absolute) {
    first = 0;
    if (getcwd(cwd, sizeof cwd) == NULL) memcpy(cwd, "/", 2);
  }
  size_t total = strlen(cwd) + 2;
  for (size_t i = first; i < count; i++) total += strlen(parts[i]) + 1;
  char *joined = (char *)checked(malloc(total));
  size_t at = 0;
  if (!absolute) {
    memcpy(joined, cwd, strlen(cwd));
    at = strlen(cwd);
  }
  for (size_t i = first; i < count; i++) {
    size_t length = strlen(parts[i]);
    if (length == 0) continue;
    if (at > 0) joined[at++] = '/';
    memcpy(joined + at, parts[i], length);
    at += length;
  }
  joined[at] = '\0';
  char *body = normalize_string(joined, false);
  free(joined);
  size_t body_length = strlen(body);
  char *out = (char *)checked(malloc(body_length + 2));
  out[0] = '/';
  memcpy(out + 1, body, body_length + 1);
  free(body);
  return out;
}
char *qa_node_path_resolve1(const char *path) { return qa_node_path_resolve(&path, 1); }
char *qa_node_path_resolve2(const char *a, const char *b) {
  const char *parts[2] = {a, b};
  return qa_node_path_resolve(parts, 2);
}

char *qa_node_path_relative(const char *from_in, const char *to_in) {
  if (strcmp(from_in, to_in) == 0) return copy_range("", 0);
  char *from = qa_node_path_resolve1(from_in);
  char *to = qa_node_path_resolve1(to_in);
  if (strcmp(from, to) == 0) {
    free(from);
    free(to);
    return copy_range("", 0);
  }
  /* Both are absolute and normalized: compare segment by segment past the root slash. */
  size_t from_length = strlen(from) - 1, to_length = strlen(to) - 1;
  const char *from_body = from + 1, *to_body = to + 1;
  size_t length = from_length < to_length ? from_length : to_length;
  size_t last_common = (size_t)-1;
  size_t i = 0;
  for (; i < length; i++) {
    if (from_body[i] != to_body[i]) break;
    if (from_body[i] == '/') last_common = i;
  }
  if (i == length) {
    if (to_length > length) {
      if (to_body[i] == '/') {
        /* `from` is an ancestor of `to`. */
        char *out = copy_range(to_body + i + 1, to_length - i - 1);
        free(from);
        free(to);
        return out;
      }
      if (i == 0) {
        /* `from` is the root. */
        char *out = copy_range(to_body, to_length);
        free(from);
        free(to);
        return out;
      }
    } else if (from_length > length) {
      if (from_body[i] == '/') last_common = i;
      else if (i == 0) last_common = 0;
    }
  }
  /* One ".." for every segment of `from` after the common prefix. */
  char *out = (char *)checked(malloc(from_length * 3 + to_length + 4));
  size_t at = 0;
  for (size_t index = last_common + 1; index <= from_length; index++) {
    if (index == from_length || from_body[index] == '/') {
      if (at > 0) out[at++] = '/';
      out[at++] = '.';
      out[at++] = '.';
    }
  }
  size_t tail = last_common + 1;
  if (tail < to_length) {
    if (at > 0) out[at++] = '/';
    memcpy(out + at, to_body + tail, to_length - tail);
    at += to_length - tail;
  }
  out[at] = '\0';
  free(from);
  free(to);
  return out;
}

char *qa_node_path_dirname(const char *path) {
  size_t length = strlen(path);
  if (length == 0) return copy_range(".", 1);
  bool has_root = path[0] == '/';
  size_t end = (size_t)-1;
  bool matched_slash = true;
  for (size_t i = length - 1; i >= 1; i--) {
    if (path[i] == '/') {
      if (!matched_slash) {
        end = i;
        break;
      }
    } else {
      matched_slash = false;
    }
  }
  if (end == (size_t)-1) return has_root ? copy_range("/", 1) : copy_range(".", 1);
  if (has_root && end == 1) return copy_range("//", 2);
  return copy_range(path, end);
}

char *qa_node_path_basename(const char *path) {
  size_t length = strlen(path);
  size_t start = 0, end = (size_t)-1;
  bool matched_slash = true;
  for (size_t i = length; i-- > 0;) {
    if (path[i] == '/') {
      if (!matched_slash) {
        start = i + 1;
        break;
      }
    } else if (end == (size_t)-1) {
      matched_slash = false;
      end = i + 1;
    }
  }
  if (end == (size_t)-1) return copy_range("", 0);
  return copy_range(path + start, end - start);
}

char *qa_node_path_extname(const char *path) {
  size_t length = strlen(path);
  size_t start_dot = (size_t)-1, start_part = 0, end = (size_t)-1;
  bool matched_slash = true;
  int pre_dot_state = 0;
  for (size_t i = length; i-- > 0;) {
    char code = path[i];
    if (code == '/') {
      if (!matched_slash) {
        start_part = i + 1;
        break;
      }
      continue;
    }
    if (end == (size_t)-1) {
      matched_slash = false;
      end = i + 1;
    }
    if (code == '.') {
      if (start_dot == (size_t)-1) start_dot = i;
      else if (pre_dot_state != 1) pre_dot_state = 1;
    } else if (start_dot != (size_t)-1) {
      pre_dot_state = -1;
    }
  }
  if (start_dot == (size_t)-1 || end == (size_t)-1 || pre_dot_state == 0 ||
      (pre_dot_state == 1 && start_dot == end - 1 && start_dot == start_part + 1))
    return copy_range("", 0);
  return copy_range(path + start_dot, end - start_dot);
}
