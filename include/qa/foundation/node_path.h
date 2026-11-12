/*
 * node:path (POSIX) operations the TypeScript calls on file paths: join,
 * normalize, resolve, relative, dirname, basename. There is no corresponding
 * .ts file: these are the Node built-ins, lexical only (no file system access
 * except resolve's working directory). Every result is malloc'd; out of memory
 * raises. GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_NODE_PATH_H
#define QA_FOUNDATION_NODE_PATH_H

#include <stddef.h>

/* path.normalize(path): "." for an empty path; a trailing slash is kept. */
char *qa_node_path_normalize(const char *path);
/* path.join(...parts): empty parts are skipped; "." when nothing remains. */
char *qa_node_path_join(const char *const *parts, size_t count);
char *qa_node_path_join2(const char *a, const char *b);
char *qa_node_path_join3(const char *a, const char *b, const char *c);
/* path.resolve(...parts): absolute, against the working directory when no part is absolute. */
char *qa_node_path_resolve(const char *const *parts, size_t count);
char *qa_node_path_resolve1(const char *path);
char *qa_node_path_resolve2(const char *a, const char *b);
/* path.relative(from, to). */
char *qa_node_path_relative(const char *from, const char *to);
/* path.dirname(path). */
char *qa_node_path_dirname(const char *path);
/* path.basename(path). */
char *qa_node_path_basename(const char *path);
/* path.extname(path). */
char *qa_node_path_extname(const char *path);

#endif
