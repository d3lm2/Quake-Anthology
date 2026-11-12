/* Port of src/content/mods/catalog.ts. GPL-2.0-or-later. */
#include "qa/content/mods/catalog.h"
#include "qa/content/json_text.h"

#include <ctype.h>
#include <stddef.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/declaration.h"
#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

/* TS throws plain Error for duplicate components. */
static noreturn void qa_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *fstr(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *v = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return v;
}

static char *format_string(const char *format, const char *a) {
  size_t length = strlen(format) + (a != NULL ? strlen(a) : 0) + 1;
  char *out = (char *)malloc(length);
  if (out == NULL) qa_error("out of memory");
  (void)snprintf(out, length, format, a);
  return out;
}

/* The rows are published to the caller as they are read, so a selection that
 * throws leaves the ones before it with the attempt, which frees them. */
static void read_selections(const qa_save_reader *r, const char *name,
                            qa_mod_selection **out_rows, size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_mod_selection *rows =
      count > 0 ? (qa_mod_selection *)calloc(count, sizeof *rows) : NULL;
  if (count > 0 && rows == NULL) qa_save_reader_fail(&list, "out of memory");
  *out_rows = rows;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    const char *text = qa_save_reader_string(&entry);
    if (!qa_read_mod_selection(text, &rows[i]))
      qa_save_reader_fail(&list, "out of memory");
    *out_count = i + 1;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
}

static const qa_mod_program *declaration_program(qa_mod_declaration declaration) {
  switch (declaration.runtime) {
    case QA_MOD_RUNTIME_QUAKEC:
      return declaration.as.quakec != NULL ? &declaration.as.quakec->program : NULL;
    case QA_MOD_RUNTIME_QVM:
      return declaration.as.qvm != NULL ? &declaration.as.qvm->program : NULL;
    case QA_MOD_RUNTIME_NATIVE:
      return declaration.as.native != NULL ? &declaration.as.native->program : NULL;
  }
  return NULL;
}

static void free_selections(qa_mod_selection *rows, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free((char *)rows[i].product);
    free((char *)rows[i].id);
  }
  free(rows);
}

/* The returned array sits in a block that also owns what its entries share:
 * the parsed gameplay-mods.json their titles and ids borrow, the source
 * title and the provider. */
typedef struct discovery_block {
  qa_save_value *value;
  char *source_title;
  char *provider;
  qa_discovered_gameplay_mod items[];
} discovery_block;

static void free_entry(qa_discovered_gameplay_mod *mod);
void qa_discovered_gameplay_mods_free(qa_discovered_gameplay_mod *mods, size_t count) {
  if (mods == NULL) return;
  discovery_block *block = (discovery_block *)(void *)((char *)mods - offsetof(discovery_block, items));
  for (size_t i = 0; i < count; i++) free_entry(&mods[i]);
  qa_save_value_free(block->value);
  free(block->source_title);
  free(block->provider);
  free(block);
}

/* Body of the TS per-component try block. Owned results are published to
 * the attempt as they are made, so the catch side frees what a throw left. */
typedef struct component_attempt {
  const qa_save_reader *reader;
  const qa_mod_mounted_content *mounted;
  char *path;
  qa_mod_selection *requires, *conflicts;
  size_t requires_count, conflicts_count;
  qa_mod_catalog_file file;
  qa_mod_declaration declaration;
} component_attempt;

static void component_body(void *ctx) {
  component_attempt *attempt = (component_attempt *)ctx;
  attempt->path = qa_normalize_resource_path(fstr(attempt->reader, "callbacks"));
  if (attempt->path == NULL) qa_error("out of memory");
  read_selections(attempt->reader, "requires", &attempt->requires, &attempt->requires_count);
  read_selections(attempt->reader, "conflicts", &attempt->conflicts, &attempt->conflicts_count);
  if (!attempt->mounted->open(attempt->mounted->ctx, attempt->path, NULL, &attempt->file))
    qa_raise("Error", 0, 0, "Missing callback declaration %s", attempt->path);
  attempt->declaration = qa_parse_gameplay_mod_declaration(attempt->file.bytes, attempt->file.length);
  const qa_mod_program *program = declaration_program(attempt->declaration);
  qa_mod_catalog_file program_file;
  bool program_opened = program != NULL &&
      attempt->mounted->open(attempt->mounted->ctx, program->path, NULL, &program_file);
  if (!program_opened || strcmp(program_file.digest, program->digest) != 0)
    qa_raise("Error", 0, 0, "Executable differs from its callback declaration");
}

static void free_entry(qa_discovered_gameplay_mod *mod) {
  free((char *)mod->description.unavailable_reason);
  free_selections((qa_mod_selection *)mod->description.requires, mod->description.requires_count);
  free_selections((qa_mod_selection *)mod->description.conflicts, mod->description.conflicts_count);
  if (mod->is_available) qa_mod_declaration_free(&mod->mod.declaration);
}

/* What discovery holds while the document can still throw (a malformed
 * gameplay-mods.json, a duplicate component): the TypeScript leaves it to the
 * collector, a raise here releases it. */
typedef struct discovery_work {
  qa_save_value *value;
  void *declared;
  char *source_title, *provider, *key;
  char **seen;
  size_t seen_count;
  qa_discovered_gameplay_mod *result;
  size_t result_count;
} discovery_work;
static void discovery_work_cleanup(void *raw) {
  discovery_work *work = (discovery_work *)raw;
  for (size_t i = 0; i < work->result_count; i++) free_entry(&work->result[i]);
  free(work->result);
  for (size_t i = 0; i < work->seen_count; i++) free(work->seen[i]);
  free(work->seen);
  free(work->key);
  free(work->source_title);
  free(work->provider);
  free(work->declared);
  qa_save_value_free(work->value);
}

qa_discovered_gameplay_mod *qa_discover_gameplay_mods(
    const qa_mod_catalog_product *product,
    const qa_mod_mounted_content *mounted, size_t *out_count) {
  static const char *const purposes[] = {"addition", "game-type"};
  *out_count = 0;
  qa_mod_catalog_file document;
  if (!mounted->open(mounted->ctx, "gameplay-mods.json", product->id, &document))
    return NULL;
  qa_save_value *value = qa_content_parse_json_fatal(document.bytes, document.length);
  if (value == NULL) return NULL;
  discovery_work work;
  memset(&work, 0, sizeof work);
  work.value = value;
  qa_cleanup work_node;
  qa_cleanup_push(&work_node, discovery_work_cleanup, &work);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) qa_error("out of memory");
  {
    qa_save_reader version = field(&reader, "version");
    (void)qa_save_reader_literal_number(&version, 1);
    qa_save_reader_destroy(&version);
  }
  qa_save_reader components = field(&reader, "components");
  size_t component_count = qa_save_reader_list_count(&components);
  struct declared {
    const char *id;
    const char *title;
    const char *purpose;
    qa_save_reader reader;
  };
  struct declared *declared = component_count > 0
      ? (struct declared *)calloc(component_count, sizeof *declared) : NULL;
  if (component_count > 0 && declared == NULL)
    qa_save_reader_fail(&components, "out of memory");
  work.declared = declared;
  for (size_t i = 0; i < component_count; i++) {
    declared[i].reader = item(&components, i);
    declared[i].id = fstr(&declared[i].reader, "id");
    declared[i].title = fstr(&declared[i].reader, "title");
    qa_save_reader purpose = field(&declared[i].reader, "purpose");
    declared[i].purpose = qa_save_reader_choice_string(&purpose, purposes, 2);
    qa_save_reader_destroy(&purpose);
  }
  /* Source title: `${title} (${FAMILY}${edition === "rerelease" ?
   * " rerelease" : ""})` with FAMILY upper-cased. */
  char family_upper[64];
  size_t family_length = strlen(product->expectation.family);
  if (family_length >= sizeof family_upper) qa_error("out of memory");
  for (size_t i = 0; i <= family_length; i++)
    family_upper[i] = (char)toupper((unsigned char)product->expectation.family[i]);
  size_t source_title_length = strlen(product->expectation.title) + family_length + 16;
  char *source_title = (char *)malloc(source_title_length);
  if (source_title == NULL) qa_error("out of memory");
  work.source_title = source_title;
  (void)snprintf(source_title, source_title_length, "%s (%s%s)",
                 product->expectation.title, family_upper,
                 strcmp(product->expectation.edition, "rerelease") == 0
                     ? " rerelease"
                     : "");
  char *provider = format_string("%s:official", product->expectation.family);
  work.provider = provider;

  qa_discovered_gameplay_mod *result = NULL;
  size_t result_count = 0;
  char **seen = NULL;
  size_t seen_count = 0;
  for (size_t i = 0; i < component_count; i++) {
    qa_mod_selection selection = {product->expectation.id, declared[i].id};
    char *key = qa_mod_selection_key(selection);
    if (key == NULL) qa_error("out of memory");
    work.key = key;
    for (size_t j = 0; j < seen_count; j++) {
      if (strcmp(seen[j], key) == 0) qa_raise("Error", 0, 0, "Duplicate mod component: %s", key);
    }
    char **grown_seen = (char **)realloc(seen, (seen_count + 1) * sizeof *seen);
    if (grown_seen == NULL) qa_error("out of memory");
    seen = grown_seen;
    seen[seen_count++] = key;
    work.key = NULL;
    work.seen = seen;
    work.seen_count = seen_count;
    if (strcmp(declared[i].purpose, "game-type") == 0) continue;
    qa_mod_description description;
    memset(&description, 0, sizeof description);
    description.selection = selection;
    description.title = declared[i].title;
    description.source_title = source_title;
    description.source.provider = provider;
    description.source.content = product->id;
    description.purpose = "addition";

    qa_discovered_gameplay_mod *grown_result = (qa_discovered_gameplay_mod *)realloc(
        result, (result_count + 1) * sizeof *result);
    if (grown_result == NULL) qa_error("out of memory");
    result = grown_result;
    qa_discovered_gameplay_mod *slot = &result[result_count++];
    memset(slot, 0, sizeof *slot);
    work.result = result;
    work.result_count = result_count;

    /* TS try { ... } catch (error) { unavailable(error.message) }: every
     * throw inside, including malformed declarations, settles this
     * component as unavailable instead of failing discovery. */
    component_attempt attempt;
    memset(&attempt, 0, sizeof attempt);
    attempt.reader = &declared[i].reader;
    attempt.mounted = mounted;
    qa_caught_error caught;
    if (!qa_try(component_body, &attempt, &caught)) {
      free(attempt.path);
      free_selections(attempt.requires, attempt.requires_count);
      free_selections(attempt.conflicts, attempt.conflicts_count);
      qa_mod_declaration_free(&attempt.declaration);
      slot->description = description;
      slot->description.is_available = false;
      slot->description.unavailable_reason = format_string("%s", caught.message);
      continue;
    }
    qa_mod_selection *requires = attempt.requires, *conflicts = attempt.conflicts;
    size_t requires_count = attempt.requires_count, conflicts_count = attempt.conflicts_count;
    qa_mod_declaration declaration = attempt.declaration;
    qa_mod_catalog_file file = attempt.file;
    free(attempt.path);
    slot->is_available = true;
    slot->description = description;
    slot->description.is_available = true;
    slot->description.requires = requires;
    slot->description.requires_count = requires_count;
    slot->description.conflicts = conflicts;
    slot->description.conflicts_count = conflicts_count;
    slot->mod.selection = selection;
    slot->mod.source = description.source;
    slot->mod.title = declared[i].title;
    slot->mod.source_title = source_title;
    slot->mod.requires = requires;
    slot->mod.requires_count = requires_count;
    slot->mod.conflicts = conflicts;
    slot->mod.conflicts_count = conflicts_count;
    slot->mod.declaration = declaration;
    slot->mod.declaration_digest = file.digest;
  }
  discovery_block *block = result_count == 0 ? NULL : malloc(sizeof *block + result_count * sizeof *result);
  if (result_count != 0 && block == NULL) qa_error("out of memory");
  qa_cleanup_pop(&work_node);
  for (size_t i = 0; i < component_count; i++)
    qa_save_reader_destroy(&declared[i].reader);
  free(declared);
  for (size_t i = 0; i < seen_count; i++) free(seen[i]);
  free(seen);
  qa_save_reader_destroy(&components);
  qa_save_reader_destroy(&reader);
  *out_count = result_count;
  if (result_count == 0) {
    free(result);
    qa_save_value_free(value);
    free(source_title);
    free(provider);
    return NULL;
  }
  block->value = value;
  block->source_title = source_title;
  block->provider = provider;
  memcpy(block->items, result, result_count * sizeof *result);
  free(result);
  return block->items;
}
