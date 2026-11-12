/* Port of src/app/bootstrap/simulation/q2-native-world.ts. Prepared
 * guests are the sibling slices' opaque sources; the PE ABI decides
 * the edition through the caller-supplied prepare callbacks.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q2_NATIVE_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q2_NATIVE_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
typedef struct qa_app_sim_prepared_q2_native_guest {
  const char *edition; /* "classic"|"rerelease" */
  bool has_primary;
  const void *primary_profile;       /* NativePrimaryProfile */
  const char *primary_requested_path;
  const char *primary_digest;
  qa_execution_module execution;
  const void *source; /* PreparedClassicGuest|PreparedRereleaseGuest */
} qa_app_sim_prepared_q2_native_guest;
/* TS prepareQ2Guest over mounted content: the PE header of the artifact
 * decides the edition, and the prepared execution carries the corrected
 * api/profile. Returns an owned qa_app_sim_prepared_classic_guest or
 * qa_app_sim_prepared_rerelease_guest (*out_edition "classic"|"rerelease");
 * release it with qa_app_sim_prepared_q2_guest_free. Errors abort as the TS throws. */
struct qa_content_mounted;
void *qa_app_sim_prepare_q2_guest_mounted(const qa_resolved_execution_module *execution, struct qa_content_mounted *mounts,
    const char **out_edition);
void qa_app_sim_prepared_q2_guest_free(void *prepared, const char *edition);
/* TS nativeModuleIdentity(prepared). With a primary declaration the revision is a whole
 * malloc'd string stored in *owned_revision (the caller frees it); otherwise it is the
 * artifact digest and *owned_revision is NULL. */
qa_module_identity qa_app_sim_native_module_identity(qa_app_sim_prepared_q2_native_guest prepared, char **owned_revision);
#endif
