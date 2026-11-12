/* Decoded checkpoint ownership (see include/qa/persistence/decoded.h). GPL-2.0-or-later. */
#include "qa/persistence/decoded.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

void *qa_persist_decoded_array(qa_persist_decoded *owner, size_t count, size_t size) {
  if (owner->block_count == owner->block_capacity) {
    size_t capacity = owner->block_capacity != 0 ? owner->block_capacity * 2 : 16;
    void **blocks = realloc(owner->blocks, capacity * sizeof *blocks);
    if (blocks == NULL) qa_range_error("out of memory");
    owner->blocks = blocks;
    owner->block_capacity = capacity;
  }
  void *block = calloc(count != 0 ? count : 1, size != 0 ? size : 1);
  if (block == NULL) qa_range_error("out of memory");
  owner->blocks[owner->block_count++] = block;
  return block;
}

void qa_persist_decoded_free(qa_persist_decoded *owner) {
  for (size_t i = 0; i < owner->block_count; i++) free(owner->blocks[i]);
  free(owner->blocks);
  qa_save_value_free(owner->root);
  owner->root = NULL;
  owner->blocks = NULL;
  owner->block_count = owner->block_capacity = 0;
}

static void rollback(void *arg) {
  qa_persist_decoded_scope *scope = arg;
  qa_persist_decoded *owner = scope->owner;
  for (size_t i = scope->mark; i < owner->block_count; i++) free(owner->blocks[i]);
  owner->block_count = scope->mark;
  if (scope->mark == 0) {
    free(owner->blocks);
    owner->blocks = NULL;
    owner->block_capacity = 0;
  }
  if (owner->root != scope->root) {
    qa_save_value_free(owner->root);
    owner->root = scope->root;
  }
}
void qa_persist_decoded_enter(qa_persist_decoded_scope *scope, qa_persist_decoded *owner) {
  scope->owner = owner;
  scope->mark = owner->block_count;
  scope->root = owner->root;
  qa_cleanup_push(&scope->node, rollback, scope);
}
void qa_persist_decoded_leave(qa_persist_decoded_scope *scope) { qa_cleanup_pop(&scope->node); }
