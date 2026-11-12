/*
 * Port of src/content/mounts/paths.ts: resource-path normalization,
 * root confinement, missing-file classification, and case-resolving
 * content lookup. C accommodations:
 *  - node:path resolve/relative are qa/foundation/node_path.h;
 *    findContentPath's async walk is synchronous and returns false for
 *    the TS null, with *out_path malloc'd on true.
 *  - isMissingFile takes an errno value (the TS inspects error.code).
 *  - Nothing here aborts on what the file system holds: a failed fs call
 *    raises the Error Bun reports (qa_content_fs_error), an ambiguous
 *    spelling or a symlink escape raises the TS Error.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MOUNTS_PATHS_H
#define QA_CONTENT_MOUNTS_PATHS_H

#include <stdbool.h>
#include <stddef.h>

/* TS "exact" | "case-insensitive" (findContentPath comparison and
 * OpenMountOptions.looseComparison). */
typedef enum qa_content_path_comparison {
  QA_CONTENT_PATH_CASE_INSENSITIVE,
  QA_CONTENT_PATH_EXACT
} qa_content_path_comparison;

/* Malloc'd normalized path; NULL only on OOM. Invalid paths raise
 * the exact TS RangeError. */
char *qa_normalize_resource_path(const char *path);
/* Malloc'd absolute destination inside root; NULL only on OOM. */
char *qa_path_within_root(const char *root, const char *path);

bool qa_is_missing_file(int error_number);

/* False is the TS null (missing). Ambiguous spellings and symlink
 * escapes raise the exact TS Error messages. */
bool qa_find_content_path(const char *root, const char *path,
                          qa_content_path_comparison comparison,
                          char **out_path);

/* The TS `list` parameter: the entry names of `directory`. Returns false
 * for a directory that is missing (the TS lister throws ENOENT/ENOTDIR,
 * which findContentPath turns into null); raises for any other failure.
 * With *out_owned set the caller frees each name and the array; otherwise
 * the names are borrowed until the next call. */
typedef bool (*qa_content_path_list_fn)(void *ctx, const char *directory,
                                        char ***out_names, size_t *out_count,
                                        bool *out_owned);
/* findContentPath(root, path, comparison, list); a NULL `list` reads each
 * directory fresh, as the TS default does. */
bool qa_find_content_path_listed(const char *root, const char *path,
                                 qa_content_path_comparison comparison,
                                 qa_content_path_list_fn list, void *list_ctx,
                                 char **out_path);

#endif
