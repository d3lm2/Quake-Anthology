/* Port of src/content/user-data.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/content/user_data.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/content/mounts/paths.h"
#include "qa/content/roots.h"
#include "qa/foundation/node_path.h"

/* resolve(homedir(), suffix) */
static char *resolve_home(const char *suffix) {
  return qa_node_path_resolve2(qa_content_home_directory(), suffix);
}
char *qa_default_user_content_root(void) {
  return resolve_home(".local/share/quake-typescript/content");
}

bool qa_default_addon_content_roots(const char *home,
                                    qa_nightdive_addon_root **out,
                                    size_t *out_count) {
  const char *resolved = home;
  char *owned_home = NULL;
  if (resolved == NULL) resolved = qa_content_home_directory();
  bool ok = qa_nightdive_addon_directories(resolved, out, out_count);
  free(owned_home);
  return ok;
}

char *qa_user_product_directory(const char *user_content_root,
                                const char *content_directory) {
  return qa_path_within_root(user_content_root, content_directory);
}

char *qa_user_data_directory(const char *kind, const char *user_content_root) {
  if (user_content_root == NULL) {
    char suffix[64];
    (void)snprintf(suffix, sizeof suffix, ".local/share/quake-typescript/%s", kind);
    return resolve_home(suffix);
  }
  /* join(resolve(userContentRoot), kind) */
  char *root = qa_node_path_resolve1(user_content_root);
  char *out = qa_node_path_join2(root, kind);
  free(root);
  return out;
}
