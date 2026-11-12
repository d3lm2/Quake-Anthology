/* Port of src/compat/q2/rerelease/semantics.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_SEMANTICS_H
#define QA_COMPAT_Q2_RERELEASE_SEMANTICS_H
#include <stddef.h>
#include "qa/compat/q2/rerelease/source_state.h"

/* TS rereleaseInventoryItems: a new frozen array per call. The rows are malloc'd; the caller frees them. */
qa_compat_q2_rerelease_inventory_item *qa_compat_q2_rerelease_inventory_items_owned(
    qa_compat_q2_rerelease_guest_module *module, void *string_ctx,
    qa_guest_address (*string)(void *ctx, const char *value), size_t *out_count);
/* The same rows in one process-wide scratch list that the next call rewrites: two holders alias. */
const qa_compat_q2_rerelease_inventory_item *qa_compat_q2_rerelease_inventory_items(
    qa_compat_q2_rerelease_guest_module *module, void *string_ctx,
    qa_guest_address (*string)(void *ctx, const char *value), size_t *out_count);
#endif
