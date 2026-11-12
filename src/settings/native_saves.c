/* Port of src/settings/native-saves.ts. GPL-2.0-or-later. */
#include "qa/settings/native_saves.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/persistence/q2_save_folder.h"

static void *checked(void *pointer) {
  if (pointer == NULL) qa_raise("Error", 0, 0, "%s", "out of memory");
  return pointer;
}
static bool ends_with_fold(const char *text, const char *suffix) {
  size_t length = strlen(text), suffix_length = strlen(suffix);
  if (length < suffix_length) return false;
  for (size_t i = 0; i < suffix_length; i++) {
    char c = text[length - suffix_length + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}
/* `${text}${suffix}`, malloc'd. */
static char *with_suffix(const char *text, const char *suffix) {
  size_t length = strlen(text) + strlen(suffix) + 1;
  char *out = (char *)checked(malloc(length));
  (void)snprintf(out, length, "%s%s", text, suffix);
  return out;
}
/* TS withExtension. */
static char *with_extension(const char *name) { return with_suffix(name, ends_with_fold(name, ".sav") ? "" : ".sav"); }

const char *qa_native_save_original_name(qa_native_save_original original) {
  switch (original) {
    case QA_NATIVE_SAVE_V5: return "v5";
    case QA_NATIVE_SAVE_V6: return "v6";
    case QA_NATIVE_SAVE_Q2_FOLDER: return "q2-folder";
    case QA_NATIVE_SAVE_Q2_PAK: return "q2-pak";
    default: return NULL;
  }
}

void qa_native_save_layout_destroy(qa_native_save_layout *layout) {
  free(layout->root);
  for (size_t i = 0; i < layout->scan_count; i++) free(layout->scan[i]);
  free(layout->scan);
  memset(layout, 0, sizeof *layout);
}

void qa_native_save_layout_build(const qa_native_config_layout *layout, const qa_native_save_product *product,
                                 qa_native_save_layout *out) {
  memset(out, 0, sizeof *out);
  char *game_directory = qa_node_path_basename(product->content_directory);
  /* folders: the layout's directory, then its fallbacks. */
  size_t folder_count = 1 + layout->fallback_count;
  out->scan = (char **)checked(calloc(folder_count, sizeof *out->scan));
  out->scan_count = folder_count;
  bool q1 = product->family == QA_GAME_FAMILY_Q1 && strcmp(product->edition, "quakeworld") != 0;
  bool q2_rerelease = product->family == QA_GAME_FAMILY_Q2 && strcmp(product->edition, "rerelease") == 0;
  for (size_t i = 0; i < folder_count; i++) {
    const char *folder = i == 0 ? layout->directory : layout->fallbacks[i - 1];
    if (q1) out->scan[i] = (char *)checked(strdup(folder));
    else if (q2_rerelease) out->scan[i] = qa_node_path_join3(folder, game_directory, "save");
    else out->scan[i] = qa_node_path_join2(folder, "save");
  }
  free(game_directory);
  /* describe(root, ...): the root is the writes-first scan folder (for Quake, the layout's directory as given). */
  out->root = (char *)checked(strdup(q1 ? layout->directory : out->scan[0]));
  out->depth = 0;
  out->arrangement = QA_NATIVE_SAVE_FILES;
  if (q1) {
    out->original = strcmp(product->edition, "rerelease") == 0 ? QA_NATIVE_SAVE_V6 : QA_NATIVE_SAVE_V5;
  } else if (q2_rerelease) {
    out->original = QA_NATIVE_SAVE_Q2_PAK;
  } else if (product->family == QA_GAME_FAMILY_Q2) {
    out->original = QA_NATIVE_SAVE_Q2_FOLDER;
    out->depth = 1;
    out->arrangement = QA_NATIVE_SAVE_FOLDERS;
  } else {
    out->original = QA_NATIVE_SAVE_ENGINE_ONLY;
  }
}

/* TS paths(root).locate(name). */
static char *locate_in(const qa_native_save_layout *layout, const char *root, const char *name) {
  if (layout->original == QA_NATIVE_SAVE_Q2_PAK) {
    char *slot = qa_q2_save_slot_name(name);
    char *file = with_extension(slot);
    free(slot);
    char *path = qa_node_path_join2(root, file);
    free(file);
    return path;
  }
  if (layout->original == QA_NATIVE_SAVE_Q2_FOLDER) {
    char *slot = qa_q2_save_slot_name(name);
    char *path = qa_node_path_join3(root, slot, "qts.sav");
    free(slot);
    return path;
  }
  char *file = with_extension(name);
  char *path = qa_node_path_join2(root, file);
  free(file);
  return path;
}
/* TS paths(root).originalPath(name). */
static char *original_in(const qa_native_save_layout *layout, const char *root, const char *name) {
  switch (layout->original) {
    case QA_NATIVE_SAVE_ENGINE_ONLY: return NULL;
    case QA_NATIVE_SAVE_Q2_PAK: {
      char *slot = qa_q2_save_slot_name(name);
      char *file = with_suffix(slot, ".pak.gz");
      free(slot);
      char *path = qa_node_path_join2(root, file);
      free(file);
      return path;
    }
    case QA_NATIVE_SAVE_Q2_FOLDER: {
      char *slot = qa_q2_save_slot_name(name);
      char *path = qa_node_path_join3(root, slot, "server.ssv");
      free(slot);
      return path;
    }
    default: {
      char *file = with_extension(name);
      char *path = qa_node_path_join2(root, file);
      free(file);
      return path;
    }
  }
}

char *qa_native_save_layout_locate(const qa_native_save_layout *layout, const char *name) {
  return locate_in(layout, layout->root, name);
}
char *qa_native_save_layout_original_path(const qa_native_save_layout *layout, const char *name) {
  return original_in(layout, layout->root, name);
}
char **qa_native_save_layout_candidates(const qa_native_save_layout *layout, const char *name, size_t *out_count) {
  char **list = (char **)checked(calloc(layout->scan_count * 2 + 1, sizeof *list));
  size_t count = 0;
  for (size_t i = 0; i < layout->scan_count; i++) {
    char *original = original_in(layout, layout->scan[i], name);
    if (original != NULL) list[count++] = original;
    list[count++] = locate_in(layout, layout->scan[i], name);
  }
  *out_count = count;
  return list;
}

char *qa_native_save_layout_slot(const qa_native_save_layout *layout, const char *path) {
  char *directory = qa_node_path_dirname(path);
  char *base = qa_node_path_basename(path);
  char *slot = NULL;
  if (layout->original == QA_NATIVE_SAVE_Q2_FOLDER) {
    char *parent = qa_node_path_dirname(directory);
    /* /^(?:qts\.sav|server\.ssv)$/i */
    bool file = (strlen(base) == 7 && ends_with_fold(base, "qts.sav")) || (strlen(base) == 10 && ends_with_fold(base, "server.ssv"));
    if (strcmp(parent, layout->root) == 0 && file) slot = qa_node_path_basename(directory);
    free(parent);
  } else if (strcmp(directory, layout->root) == 0) {
    size_t length = strlen(base);
    if (ends_with_fold(base, ".sav")) length -= 4;
    else if (layout->original == QA_NATIVE_SAVE_Q2_PAK && ends_with_fold(base, ".pak.gz")) length -= 7;
    slot = (char *)checked(malloc(length + 1));
    memcpy(slot, base, length);
    slot[length] = '\0';
  }
  free(directory);
  free(base);
  return slot;
}

bool qa_inside_native_saves(const qa_native_save_layout *layout, const char *path) {
  char *root = qa_node_path_resolve1(layout->root);
  char *full = qa_node_path_resolve1(path);
  char *name = qa_node_path_relative(root, full);
  free(root);
  free(full);
  bool inside = name[0] != '\0' && name[0] != '/' && strcmp(name, "..") != 0 && strncmp(name, "../", 3) != 0;
  free(name);
  return inside;
}
