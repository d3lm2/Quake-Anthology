/*
 * Port of src/app/bootstrap/native-migration.ts: moves what earlier versions
 * kept for stock games in the user store (demos, saves) into the folders the
 * original games use. Reads keep falling back to the user store, so anything
 * left behind stays usable. Screenshots there were shared by every game and
 * cannot be attributed to one, so they stay. Runs once per process (per user
 * root and write redirect) and is safe to interrupt and rerun.
 * `user_content_root` and `native_config_root` NULL are the TS undefined.
 * GPL-2.0-or-later.
 */
#ifndef QA_APP_BOOTSTRAP_NATIVE_MIGRATION_H
#define QA_APP_BOOTSTRAP_NATIVE_MIGRATION_H

#include "qa/content/catalog/index.h"

void qa_app_migrate_native_data(const qa_content_catalog *catalog, const char *user_content_root,
                                const char *native_config_root);

#endif
