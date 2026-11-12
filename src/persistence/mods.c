/* Port of src/persistence/mods.ts. GPL-2.0-or-later. */
#include "qa/persistence/mods.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/declaration.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/shared.h"

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
static const char *field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static void *xmalloc(const qa_save_reader *reader, size_t size) {
  void *memory = malloc(size);
  if (memory == NULL) qa_save_reader_fail(reader, "out of memory");
  return memory;
}

/* TS wraps modSelectionKey in try/catch and converts any throw to
 * selection.fail("invalid mod selection"). The C contracts helper
 * aborts instead of throwing, so the two anchored patterns from
 * contracts/mods.ts are checked here first. */
static bool is_alnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
static bool valid_selection(qa_mod_selection selection) {
  const char *product = selection.product, *id = selection.id;
  if (product[0] == '\0' || !is_alnum(product[0])) return false;
  for (size_t i = 1; product[i] != '\0'; i++) {
    char c = product[i];
    if (!is_alnum(c) && c != '.' && c != '_' && c != '+' && c != '-') return false;
  }
  if (id[0] == '\0' || !is_alnum(id[0])) return false;
  for (size_t i = 1; id[i] != '\0'; i++) {
    char c = id[i];
    if (!is_alnum(c) && c != '.' && c != '_' && c != '+' && c != ':' && c != '/' &&
        c != '-')
      return false;
  }
  return true;
}

/* Reads {product, id} from a selection reader and applies the TS
 * try/catch around modSelectionKey. */
static qa_mod_selection read_selection(const qa_save_reader *selection_reader) {
  qa_mod_selection selected;
  selected.product = field_string(selection_reader, "product");
  selected.id = field_string(selection_reader, "id");
  if (!valid_selection(selected))
    qa_save_reader_fail(selection_reader, "invalid mod selection");
  return selected;
}

static qa_provider_checkpoint_ref read_provider_ref(const qa_save_reader *reader) {
  qa_provider_checkpoint_ref ref;
  qa_save_reader provider = field(reader, "provider");
  ref.provider = qa_namespaced(&provider);
  qa_save_reader_destroy(&provider);
  qa_save_reader schema = field(reader, "schema");
  ref.schema = qa_namespaced(&schema);
  qa_save_reader_destroy(&schema);
  qa_save_reader version = field(reader, "version");
  ref.version = qa_save_reader_integer_min(&version, 0);
  qa_save_reader_destroy(&version);
  return ref;
}

/* TS dependency: readModSelection(`${product}/${id}`). The joined
 * key is scratch; the returned selection owns its strings. */
static qa_mod_selection read_dependency(const qa_save_reader *reader) {
  const char *product = field_string(reader, "product");
  const char *id = field_string(reader, "id");
  size_t length = strlen(product) + 1 + strlen(id) + 1;
  char *joined = xmalloc(reader, length);
  size_t product_length = strlen(product);
  memcpy(joined, product, product_length);
  joined[product_length] = '/';
  memcpy(joined + product_length + 1, id, strlen(id) + 1);
  qa_mod_selection selection;
  qa_cleanup joined_node; /* an invalid selection raises (TS readModSelection's Error) */
  qa_cleanup_push(&joined_node, free, joined);
  bool read = qa_read_mod_selection(joined, &selection);
  qa_cleanup_pop(&joined_node);
  free(joined);
  if (!read) qa_save_reader_fail(reader, "out of memory");
  return selection;
}
/* Fills *out / *out_count as it reads, so the caller's unwind releases a partial list. */
static void read_dependencies(const qa_save_reader *reader, const char *name,
                              const qa_mod_selection **out, size_t *out_count) {
  qa_save_reader list = field(reader, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_mod_selection *values =
      count > 0 ? (qa_mod_selection *)xmalloc(&list, count * sizeof *values) : NULL;
  *out = values;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    values[i] = read_dependency(&entry);
    *out_count = i + 1;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
}

void qa_resolved_gameplay_mod_release(qa_resolved_gameplay_mod *mod) {
  qa_mod_selection *requires = (qa_mod_selection *)mod->requires;
  for (size_t i = 0; i < mod->requires_count; i++) qa_mod_selection_free(&requires[i]);
  free(requires);
  qa_mod_selection *conflicts = (qa_mod_selection *)mod->conflicts;
  for (size_t i = 0; i < mod->conflicts_count; i++) qa_mod_selection_free(&conflicts[i]);
  free(conflicts);
  qa_mod_declaration_free(&mod->declaration);
  memset(mod, 0, sizeof *mod);
}
static void gameplay_mod_unwind(void *arg) { qa_resolved_gameplay_mod_release((qa_resolved_gameplay_mod *)arg); }

qa_resolved_gameplay_mod qa_read_gameplay_mod(const qa_save_reader *reader) {
  qa_resolved_gameplay_mod mod;
  memset(&mod, 0, sizeof mod);
  /* A malformed mod raises part way: the dependency lists read so far go with it. */
  qa_cleanup node;
  qa_cleanup_push(&node, gameplay_mod_unwind, &mod);
  qa_save_reader selection = field(reader, "selection");
  mod.selection = read_selection(&selection);
  qa_save_reader_destroy(&selection);
  qa_save_reader source = field(reader, "source");
  mod.source = qa_read_provider(&source);
  qa_save_reader_destroy(&source);
  mod.title = field_string(reader, "title");
  mod.source_title = field_string(reader, "sourceTitle");
  read_dependencies(reader, "requires", &mod.requires, &mod.requires_count);
  read_dependencies(reader, "conflicts", &mod.conflicts, &mod.conflicts_count);
  qa_save_reader declaration = field(reader, "declaration");
  mod.declaration = qa_read_gameplay_mod_declaration(&declaration);
  qa_save_reader_destroy(&declaration);
  qa_save_reader digest = field(reader, "declarationDigest");
  mod.declaration_digest = qa_read_digest(&digest);
  qa_save_reader_destroy(&digest);
  mod.read_order = true;
  qa_cleanup_pop(&node);
  return mod;
}

/* Fills `identity` (zeroed, owned by the caller's unwind): each list is attached before it is
 * read, so a raise part way leaves only what qa_mod_identity_release frees. */
static void read_mod_identity_into(const qa_save_reader *reader, qa_mod_identity *identity) {
  qa_save_reader selection = field(reader, "selection");
  identity->selection = read_selection(&selection);
  qa_save_reader_destroy(&selection);
  qa_save_reader source = field(reader, "source");
  identity->source = qa_read_provider(&source);
  qa_save_reader_destroy(&source);
  qa_save_reader digest = field(reader, "declarationDigest");
  identity->declaration_digest = qa_read_digest(&digest);
  qa_save_reader_destroy(&digest);
  {
    qa_save_reader modules = field(reader, "modules");
    size_t count = qa_save_reader_list_count(&modules);
    qa_module_identity *values =
        count > 0 ? (qa_module_identity *)xmalloc(&modules, count * sizeof *values) : NULL;
    identity->modules = values;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&modules, i);
      values[i] = qa_read_module(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&modules);
    identity->module_count = count;
  }
  {
    qa_save_reader providers = field(reader, "providers");
    size_t count = qa_save_reader_list_count(&providers);
    qa_provider_checkpoint_ref *values = count > 0
        ? (qa_provider_checkpoint_ref *)xmalloc(&providers, count * sizeof *values) : NULL;
    identity->providers = values;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&providers, i);
      values[i] = read_provider_ref(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&providers);
    identity->provider_count = count;
  }
}
void qa_mod_identity_release(qa_mod_identity *identity) {
  free((void *)identity->modules);
  free((void *)identity->providers);
  identity->modules = NULL;
  identity->providers = NULL;
  identity->module_count = identity->provider_count = 0;
}
static void identity_unwind(void *arg) { qa_mod_identity_release((qa_mod_identity *)arg); }

qa_mod_identity qa_read_mod_identity(const qa_save_reader *reader) {
  qa_mod_identity identity;
  memset(&identity, 0, sizeof identity);
  qa_cleanup node;
  qa_cleanup_push(&node, identity_unwind, &identity);
  read_mod_identity_into(reader, &identity);
  qa_cleanup_pop(&node);
  return identity;
}

void qa_mod_session_checkpoint_release(struct qa_mod_session_checkpoint *session) {
  qa_mod_checkpoint *mods = (qa_mod_checkpoint *)session->mods;
  for (size_t i = 0; i < session->mod_count; i++) {
    qa_mod_identity_release(&mods[i].identity);
    qa_guest_checkpoint *guests = (qa_guest_checkpoint *)mods[i].state.guests;
    for (size_t g = 0; g < mods[i].state.guest_count; g++) qa_guest_checkpoint_release(&guests[g]);
    free(guests);
    free((void *)mods[i].state.providers);
  }
  free(mods);
  session->mods = NULL;
  session->mod_count = 0;
}
typedef struct session_build {
  struct qa_mod_session_checkpoint *session;
  char **keys;
  size_t key_count;
} session_build;
static void session_unwind(void *arg) {
  session_build *build = (session_build *)arg;
  for (size_t k = 0; k < build->key_count; k++) free(build->keys[k]);
  free(build->keys);
  qa_mod_session_checkpoint_release(build->session);
}

struct qa_mod_session_checkpoint qa_read_mod_session(const qa_save_reader *reader) {
  struct qa_mod_session_checkpoint session;
  memset(&session, 0, sizeof session);
  qa_save_reader version = field(reader, "version");
  session.version = (int64_t)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader mods = field(reader, "mods");
  size_t count = qa_save_reader_list_count(&mods);
  /* A malformed entry raises part way: the session read so far and the keys are released. */
  session_build build = {&session, NULL, 0};
  qa_cleanup node;
  qa_cleanup_push(&node, session_unwind, &build);
  qa_mod_checkpoint *values =
      count > 0 ? (qa_mod_checkpoint *)calloc(count, sizeof *values) : NULL;
  if (count > 0 && values == NULL) qa_save_reader_fail(&mods, "out of memory");
  session.mods = values;
  build.keys = count > 0 ? (char **)xmalloc(&mods, count * sizeof *build.keys) : NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&mods, i);
    qa_save_reader identity = field(&entry, "identity");
    qa_save_reader selection = field(&identity, "selection");
    qa_mod_selection selected = read_selection(&selection);
    char *key = qa_mod_selection_key(selected);
    if (key == NULL) qa_save_reader_fail(&selection, "out of memory");
    for (size_t k = 0; k < build.key_count; k++) {
      if (strcmp(build.keys[k], key) == 0) {
        free(key);
        qa_save_reader_fail(&selection, "duplicate saved mod selection");
      }
    }
    build.keys[build.key_count++] = key;
    qa_save_reader_destroy(&selection);
    qa_mod_checkpoint *checkpoint = &values[i];
    session.mod_count = i + 1;
    read_mod_identity_into(&identity, &checkpoint->identity);
    qa_save_reader_destroy(&identity);
    qa_save_reader state = field(&entry, "state");
    {
      qa_save_reader guests = field(&state, "guests");
      size_t guest_count = qa_save_reader_list_count(&guests);
      qa_guest_checkpoint *guest_values = guest_count > 0
          ? (qa_guest_checkpoint *)xmalloc(&guests, guest_count * sizeof *guest_values)
          : NULL;
      checkpoint->state.guests = guest_values;
      for (size_t g = 0; g < guest_count; g++) {
        qa_save_reader guest = item(&guests, g);
        guest_values[g] = qa_read_guest(&guest);
        checkpoint->state.guest_count = g + 1;
        qa_save_reader_destroy(&guest);
      }
      qa_save_reader_destroy(&guests);
    }
    {
      qa_save_reader providers = field(&state, "providers");
      size_t provider_count = qa_save_reader_list_count(&providers);
      qa_provider_checkpoint *provider_values = provider_count > 0
          ? (qa_provider_checkpoint *)xmalloc(&providers,
                                              provider_count * sizeof *provider_values)
          : NULL;
      checkpoint->state.providers = provider_values;
      for (size_t p = 0; p < provider_count; p++) {
        qa_save_reader provider = item(&providers, p);
        qa_provider_checkpoint_ref ref = read_provider_ref(&provider);
        provider_values[p].provider = ref.provider;
        provider_values[p].schema = ref.schema;
        provider_values[p].version = ref.version;
        qa_save_reader bytes = field(&provider, "bytes");
        provider_values[p].bytes.bytes = qa_save_reader_bytes(&bytes, &provider_values[p].bytes.length);
        qa_save_reader_destroy(&bytes);
        qa_save_reader_destroy(&provider);
      }
      qa_save_reader_destroy(&providers);
      checkpoint->state.provider_count = provider_count;
    }
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&mods);
  qa_cleanup_pop(&node);
  for (size_t k = 0; k < build.key_count; k++) free(build.keys[k]);
  free(build.keys);
  return session;
}
