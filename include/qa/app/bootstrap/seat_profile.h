/*
 * Port of src/app/bootstrap/seat-profile.ts: the first seat's saved profile
 * with the tuning shared by every game laid over it, as the front end does
 * when it opens a profile. `user_content_root` NULL is the TS undefined.
 * GPL-2.0-or-later.
 */
#ifndef QA_APP_BOOTSTRAP_SEAT_PROFILE_H
#define QA_APP_BOOTSTRAP_SEAT_PROFILE_H

#include <stdbool.h>

#include "qa/settings/config.h"

/* TS globalTuningStore. False only on OOM. */
bool qa_app_global_tuning_store(const char *user_content_root, qa_config_store *out);
/* TS loadSeatProfile: false is the TS null. On true the caller owns `out`
 * (qa_seat_settings_free). */
bool qa_app_load_seat_profile(const qa_config_store *settings, const char *user_content_root, double index,
                              qa_seat_settings *out);
/* TS saveSharedTuning. Raises Error when the document cannot be written, as the
 * TS promise rejects. */
void qa_app_save_shared_tuning(const char *user_content_root, const qa_seat_settings *seat);

#endif
