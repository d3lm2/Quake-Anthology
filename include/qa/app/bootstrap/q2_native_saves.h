/*
 * Port of src/app/bootstrap/q2-native-saves.ts: writing a Quake II save in the
 * original game's layout beside the engine's own image.
 * C accommodations:
 *  - `now: Date` is its getTime() milliseconds; the classic folder's comment
 *    takes the local clock fields of that instant.
 *  - `portalCount` (async in TS) is a synchronous callback.
 *  - async functions are synchronous; file system failures raise Error.
 * GPL-2.0-or-later.
 */
#ifndef QA_APP_BOOTSTRAP_Q2_NATIVE_SAVES_H
#define QA_APP_BOOTSTRAP_Q2_NATIVE_SAVES_H

#include <stdbool.h>

#include "qa/contracts/session.h"
#include "qa/settings/native_saves.h"

/* TS Q2NativeSaveRequest. */
typedef struct qa_app_q2_native_save_request {
  const qa_native_save_layout *native;
  const char *slot;
  qa_save_image image;
  double now_milliseconds;
  /* The area portal count of a map; the rerelease level file records it and the engine checks it on load. */
  void *portal_ctx;
  double (*portal_count)(void *ctx, const char *map);
} qa_app_q2_native_save_request;

/* TS writeQ2NativeSave: writes the original-format save for the image's native game module: a `save/SLOT/`
 * folder through id's `current` workflow, or the rerelease's `SLOT.pak.gz`. Returns false when the image holds
 * no original state to write, which is the case when the engine's own game logic ran the game. */
bool qa_app_write_q2_native_save(const qa_app_q2_native_save_request *request);

/* TS clearQ2OriginalFiles: after an engine-format save takes a slot, the original files that were there would be
 * loaded in its place. */
void qa_app_clear_q2_original_files(const qa_native_save_layout *native, const char *slot);

#endif
