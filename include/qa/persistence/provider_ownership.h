/*
 * Port of src/persistence/provider-ownership.ts. Plain TS Errors
 * abort with the identical message. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_PROVIDER_OWNERSHIP_H
#define QA_PERSISTENCE_PROVIDER_OWNERSHIP_H

#include <stdbool.h>

#include "qa/contracts/session.h"

extern const qa_provider_checkpoint_ref qa_campaign_unit_checkpoint;

/* TS NATIVE_Q2_ORIGINAL_VERSION: version 2 of the native Quake II original-save providers allows the current
 * level to be absent. Version 1 records always carried it and are migrated by their decoders, so both versions
 * are accepted when a save is read. */
#define QA_NATIVE_Q2_ORIGINAL_VERSION 2
/* TS saveProviderVersionSupported: whether a saved record's version is the current one or one its decoder migrates. */
bool qa_save_provider_version_supported(const char *schema, double version, double current);

qa_provider_checkpoint_ref qa_save_provider_contract(const char *schema, qa_provider_id source);
void qa_validate_save_provider_owner(qa_provider_checkpoint record, qa_provider_id source);

#endif
