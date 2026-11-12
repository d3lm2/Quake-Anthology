/* Port of src/app/bootstrap/native-migration.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/native_migration.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/app/bootstrap/native_settings.h"
#include "qa/content/mounts/paths.h"
#include "qa/content/user_data.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/persistence/saved_game.h"
#include "qa/platform/files/contained.h"
#include "qa/settings/native_saves.h"

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}

/* const done = new Set<string>(): `${userRoot}\0${redirect ?? ""}` keys, for the life of the process. */
static struct done_key { char *user_root, *redirect; } *done;
static size_t done_count;

/* Node's fs error text: "<CODE>: <description>, <syscall> '<path>'". */
static void raise_errno(int error, const char *call, const char *path) { qa_node_fs_error(error, call, path); }

/* /\.(dem|qwd|dm2|mvd|dm_\d+)$/i */
static bool demo_file(const char *name) {
  const char *dot = strrchr(name, '.');
  if (dot == NULL) return false;
  const char *extension = dot + 1;
  if (qa_js_equals_ignore_case(extension, "dem") || qa_js_equals_ignore_case(extension, "qwd") ||
      qa_js_equals_ignore_case(extension, "dm2") || qa_js_equals_ignore_case(extension, "mvd"))
    return true;
  if (strlen(extension) < 4 || tolower((unsigned char)extension[0]) != 'd' || tolower((unsigned char)extension[1]) != 'm' ||
      extension[2] != '_')
    return false;
  for (const char *digit = extension + 3; *digit != '\0'; digit++)
    if (!isdigit((unsigned char)*digit)) return false;
  return true;
}

/* mkdir(path, { recursive: true }) */
static void make_directories(const char *directory) {
  char *path = checked(strdup(directory));
  for (char *cursor = path + 1;; cursor++) {
    bool end = *cursor == '\0';
    if (*cursor == '/' || end) {
      char saved = *cursor;
      *cursor = '\0';
      if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        int failure = errno;
        free(path);
        raise_errno(failure, "mkdir", directory);
      }
      *cursor = saved;
    }
    if (end) break;
  }
  free(path);
}

/* copyFile(source, temporary) */
static void copy_file(const char *source, const char *destination) {
  int in = open(source, O_RDONLY);
  if (in < 0) raise_errno(errno, "copyfile", source);
  int out = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (out < 0) {
    int failure = errno;
    close(in);
    raise_errno(failure, "copyfile", destination);
  }
  char buffer[65536];
  int failure = 0;
  for (;;) {
    ssize_t got = read(in, buffer, sizeof buffer);
    if (got < 0) { failure = errno; break; }
    if (got == 0) break;
    ssize_t at = 0;
    while (at < got) {
      ssize_t wrote = write(out, buffer + at, (size_t)(got - at));
      if (wrote < 0) { failure = errno; break; }
      at += wrote;
    }
    if (failure != 0) break;
  }
  close(in);
  if (close(out) != 0 && failure == 0) failure = errno;
  if (failure != 0) raise_errno(failure, "copyfile", destination);
}

typedef struct move_job {
  const char *source, *destination;
  char *temporary;
} move_job;
static void move_copy(void *ctx) {
  move_job *job = ctx;
  copy_file(job->source, job->temporary);
  if (rename(job->temporary, job->destination) != 0) raise_errno(errno, "rename", job->temporary);
}

/* TS moveInto: move one file without ever losing it: copy beside the destination, rename into place, then drop
 * the source. An interrupted run leaves the source (and at worst a temporary copy), and the next run finishes the
 * job. A destination that already holds a different file is never overwritten; the source stays where it is. */
static bool move_into(const char *source, const char *destination) {
  struct stat existing, original;
  bool has_existing = stat(destination, &existing) == 0;
  if (!has_existing && !qa_is_missing_file(errno)) raise_errno(errno, "stat", destination);
  if (stat(source, &original) != 0) raise_errno(errno, "stat", source);
  if (has_existing) {
    if (existing.st_size != original.st_size) return false;
    if (unlink(source) != 0) raise_errno(errno, "rm", source);
    return true;
  }
  char *directory = checked(qa_node_path_dirname(destination));
  qa_cleanup directory_guard;
  qa_cleanup_push(&directory_guard, free, directory);
  make_directories(directory);
  /* join(dirname(destination), `.${basename(destination)}.${crypto.randomUUID()}.tmp`) */
  char *base = checked(qa_node_path_basename(destination));
  unsigned char random[16];
  FILE *urandom = fopen("/dev/urandom", "rb");
  if (urandom == NULL || fread(random, 1, sizeof random, urandom) != sizeof random) {
    for (size_t i = 0; i < sizeof random; i++) random[i] = (unsigned char)rand();
  }
  if (urandom != NULL) fclose(urandom);
  random[6] = (unsigned char)((random[6] & 0x0f) | 0x40);
  random[8] = (unsigned char)((random[8] & 0x3f) | 0x80);
  size_t length = strlen(base) + 64;
  char *name = checked(malloc(length));
  (void)snprintf(name, length, ".%s.%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x.tmp", base, random[0],
                 random[1], random[2], random[3], random[4], random[5], random[6], random[7], random[8], random[9], random[10],
                 random[11], random[12], random[13], random[14], random[15]);
  free(base);
  move_job job = {source, destination, checked(qa_node_path_join2(directory, name))};
  free(name);
  qa_cleanup_pop(&directory_guard);
  free(directory);
  qa_caught_error caught;
  bool copied = qa_try(move_copy, &job, &caught);
  /* finally { await rm(temporary, { force: true }) } */
  (void)unlink(job.temporary);
  free(job.temporary);
  if (!copied) qa_rethrow(&caught);
  if (unlink(source) != 0) raise_errno(errno, "rm", source);
  return true;
}

typedef struct name_list { char **names; size_t count; } name_list;
static void names_free(name_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->names[i]);
  free((void *)list->names);
  list->names = NULL;
  list->count = 0;
}
static void names_cleanup(void *list) { names_free(list); }
/* TS files(directory): the regular files of a directory; a missing directory has none. */
static name_list files_of(const char *directory) {
  name_list list = {NULL, 0};
  DIR *dir = opendir(directory);
  if (dir == NULL) {
    if (qa_is_missing_file(errno)) return list;
    raise_errno(errno, "scandir", directory);
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char *path = checked(qa_node_path_join2(directory, entry->d_name));
    struct stat status;
    bool regular = lstat(path, &status) == 0 && S_ISREG(status.st_mode); /* Dirent.isFile() does not follow links */
    free(path);
    if (!regular) continue;
    list.names = checked(realloc((void *)list.names, (list.count + 1) * sizeof *list.names));
    list.names[list.count++] = checked(strdup(entry->d_name));
  }
  closedir(dir);
  return list;
}

typedef struct locate_job { const qa_native_save_layout *layout; const char *name; char *target; } locate_job;
static void locate_run(void *ctx) {
  locate_job *job = ctx;
  job->target = qa_native_save_layout_locate(job->layout, job->name);
}
typedef struct header_job { const char *path; const char *product; bool matches; } header_job;
static void header_run(void *ctx) {
  header_job *job = ctx;
  qa_save_header_result header;
  memset(&header, 0, sizeof header);
  if (!qa_read_save_header(job->path, &header)) return;
  job->matches = header.present && strcmp(header.header.content, job->product) == 0;
  qa_save_header_result_free(&header);
}

typedef struct product_job {
  const qa_catalog_discovered_product *product;
  const qa_app_stock_native *native;
  const char *store, *saves;
  const name_list *top_level;
} product_job;
static void layout_cleanup(void *layout) { qa_native_save_layout_destroy(layout); }
static void move_candidate(const product_job *job, const qa_native_save_layout *layout, const char *path) {
  char *name = checked(qa_node_path_basename(path));
  qa_cleanup name_guard;
  qa_cleanup_push(&name_guard, free, name);
  if (qa_js_ends_with_ignore_case(name, ".sav") && !qa_js_equals_ignore_case(name, "autosave.sav")) {
    /* Only engine-format saves carry the product they belong to; original-format files stay where they are. */
    header_job header = {path, job->product->id, false};
    qa_caught_error caught;
    if (qa_try(header_run, &header, &caught) && header.matches) {
      /* A slot name the original refuses (`current`, one with a path separator) stays where it is. */
      locate_job locate = {layout, name, NULL};
      if (qa_try(locate_run, &locate, &caught)) {
        qa_cleanup target_guard;
        qa_cleanup_push(&target_guard, free, locate.target);
        (void)move_into(path, locate.target);
        qa_cleanup_pop(&target_guard);
        free(locate.target);
      }
    }
  }
  qa_cleanup_pop(&name_guard);
  free(name);
}
static void product_run(void *ctx) {
  product_job *job = ctx;
  static const char *const directories[] = {"", "demos"};
  for (size_t d = 0; d < 2; d++) {
    char *from = checked(qa_node_path_join2(job->store, directories[d]));
    qa_cleanup from_guard;
    qa_cleanup_push(&from_guard, free, from);
    name_list names = files_of(from);
    qa_cleanup names_guard;
    qa_cleanup_push(&names_guard, names_cleanup, &names);
    for (size_t i = 0; i < names.count; i++) {
      if (!demo_file(names.names[i])) continue;
      char *source = checked(qa_node_path_join3(job->store, directories[d], names.names[i]));
      char *destination = checked(qa_node_path_join3(job->native->layout.directory, directories[d], names.names[i]));
      qa_cleanup source_guard, destination_guard;
      qa_cleanup_push(&source_guard, free, source);
      qa_cleanup_push(&destination_guard, free, destination);
      (void)move_into(source, destination);
      qa_cleanup_pop(&destination_guard);
      qa_cleanup_pop(&source_guard);
      free(source);
      free(destination);
    }
    qa_cleanup_pop(&names_guard);
    names_free(&names);
    qa_cleanup_pop(&from_guard);
    free(from);
  }
  qa_native_save_product save_product = qa_app_native_save_product(&job->product->expectation);
  qa_native_save_layout layout;
  qa_native_save_layout_build(&job->native->layout, &save_product, &layout);
  qa_cleanup layout_guard;
  qa_cleanup_push(&layout_guard, layout_cleanup, &layout);
  /* candidates: the product's own save folder, then the top-level saves */
  char *product_saves = checked(qa_node_path_join2(job->saves, job->product->id));
  qa_cleanup product_guard;
  qa_cleanup_push(&product_guard, free, product_saves);
  name_list own = files_of(product_saves);
  qa_cleanup own_guard;
  qa_cleanup_push(&own_guard, names_cleanup, &own);
  for (size_t pass = 0; pass < 2; pass++) {
    const name_list *list = pass == 0 ? &own : job->top_level;
    const char *root = pass == 0 ? product_saves : job->saves;
    for (size_t i = 0; i < list->count; i++) {
      char *path = checked(qa_node_path_join2(root, list->names[i]));
      qa_cleanup path_guard;
      qa_cleanup_push(&path_guard, free, path);
      move_candidate(job, &layout, path);
      qa_cleanup_pop(&path_guard);
      free(path);
    }
  }
  qa_cleanup_pop(&own_guard);
  names_free(&own);
  qa_cleanup_pop(&product_guard);
  free(product_saves);
  qa_cleanup_pop(&layout_guard);
  qa_native_save_layout_destroy(&layout);
}

static void native_cleanup(void *native) { qa_app_stock_native_destroy(native); }

void qa_app_migrate_native_data(const qa_content_catalog *catalog, const char *user_content_root,
                                const char *native_config_root) {
  char *user_root = user_content_root != NULL ? checked(strdup(user_content_root)) : checked(qa_default_user_content_root());
  char *redirect = qa_game_write_redirect(native_config_root, catalog->corpus_root);
  if (redirect == NULL) redirect = checked(strdup(""));
  for (size_t i = 0; i < done_count; i++)
    if (strcmp(done[i].user_root, user_root) == 0 && strcmp(done[i].redirect, redirect) == 0) {
      free(user_root);
      free(redirect);
      return;
    }
  done = checked(realloc(done, (done_count + 1) * sizeof *done));
  done[done_count].user_root = checked(strdup(user_root));
  done[done_count].redirect = redirect;
  done_count++;
  qa_cleanup root_guard;
  qa_cleanup_push(&root_guard, free, user_root);
  char *saves = checked(qa_user_data_directory("saves", user_content_root));
  qa_cleanup saves_guard;
  qa_cleanup_push(&saves_guard, free, saves);
  name_list top_level = files_of(saves);
  qa_cleanup top_guard;
  qa_cleanup_push(&top_guard, names_cleanup, &top_level);
  for (size_t p = 0; p < catalog->product_count; p++) {
    const qa_catalog_discovered_product *product = &catalog->products[p];
    if (product->availability.kind != QA_CATALOG_INSTALLED) continue;
    const char *components[1] = {product->id};
    qa_app_stock_native *native = qa_app_stock_native_config(catalog, product->id, components, 1, native_config_root, false, NULL);
    if (native == NULL) continue;
    qa_cleanup native_guard;
    qa_cleanup_push(&native_guard, native_cleanup, native);
    char *store = product->user_content != NULL && product->user_content->root != NULL
                      ? checked(strdup(product->user_content->root))
                      : checked(qa_user_product_directory(user_root, product->expectation.content_directory));
    qa_cleanup store_guard;
    qa_cleanup_push(&store_guard, free, store);
    if (strcmp(store, native->layout.directory) != 0) {
      product_job job = {product, native, store, saves, &top_level};
      qa_caught_error caught;
      /* A folder that cannot be written leaves its data in the user store, which is still read. */
      if (!qa_try(product_run, &job, &caught))
        (void)fprintf(stderr, "Could not move %s data to %s: %s\n", product->id, native->layout.directory, caught.message);
    }
    qa_cleanup_pop(&store_guard);
    free(store);
    qa_cleanup_pop(&native_guard);
    qa_app_stock_native_destroy(native);
  }
  qa_cleanup_pop(&top_guard);
  names_free(&top_level);
  qa_cleanup_pop(&saves_guard);
  free(saves);
  qa_cleanup_pop(&root_guard);
  free(user_root);
}
