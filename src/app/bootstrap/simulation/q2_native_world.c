/* Port of src/app/bootstrap/simulation/q2-native-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q2_native_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/guest/pe/format.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
void *qa_app_sim_prepare_q2_guest_mounted(const qa_resolved_execution_module *execution, qa_content_mounted *mounts,
    const char **out_edition) {
  if (!is(execution->role, "server-game")) qa_raise("Error", 0, 0, "Q2 guest preparation requires a server-game artifact");
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, execution->artifact, &length);
  /* An empty artifact reads as no buffer: parsePe sees zero bytes and refuses it with its own error. */
  static const uint8_t empty[1];
  qa_cleanup bytes_node; qa_cleanup_push(&bytes_node, free, bytes); /* parsePe may refuse a bad artifact */
  qa_pe_file pe; qa_parse_pe(&pe, bytes != NULL ? bytes : empty, bytes != NULL ? length : 0);
  qa_cleanup_pop(&bytes_node);
  qa_guest_native_abi abi = pe.abi;
  qa_pe_file_free(&pe);
  free(bytes);
  qa_resolved_execution_module corrected = *execution;
  corrected.profile = (qa_native_abi){abi.kind, abi.image, abi.pointer_bytes, abi.call};
  corrected.api.kind = QA_GAME_API_Q2_GAME;
  if (is(abi.kind, "windows-x86-64")) {
    corrected.api.as.q2_game.kind = QA_Q2_GAME_API_RERELEASE; corrected.api.as.q2_game.version = 2023;
    qa_app_sim_prepared_rerelease_guest *prepared = calloc(1, sizeof *prepared);
    if (prepared == NULL) qa_range_error("Out of memory");
    qa_cleanup prepared_node; qa_cleanup_push(&prepared_node, free, prepared);
    qa_app_sim_prepare_rerelease_guest(&corrected, mounts, prepared);
    qa_cleanup_pop(&prepared_node);
    *out_edition = "rerelease";
    return prepared;
  }
  corrected.api.as.q2_game.kind = QA_Q2_GAME_API_CLASSIC; corrected.api.as.q2_game.version = 3;
  qa_app_sim_prepared_classic_guest *prepared = calloc(1, sizeof *prepared);
  if (prepared == NULL) qa_range_error("Out of memory");
  qa_cleanup prepared_node; qa_cleanup_push(&prepared_node, free, prepared);
  qa_app_sim_prepare_classic_guest(&corrected, mounts, prepared);
  qa_cleanup_pop(&prepared_node);
  *out_edition = "classic";
  return prepared;
}
void qa_app_sim_prepared_q2_guest_free(void *prepared, const char *edition) {
  if (prepared == NULL) return;
  if (is(edition, "rerelease")) qa_app_sim_prepared_rerelease_guest_free(prepared);
  else qa_app_sim_prepared_classic_guest_free(prepared);
  free(prepared);
}
qa_module_identity qa_app_sim_native_module_identity(qa_app_sim_prepared_q2_native_guest prepared, char **owned_revision) {
  const qa_resolved_resource_reference *artifact = prepared.execution.artifact;
  qa_module_identity out;
  out.id = prepared.execution.owner.provider;
  out.artifact_path = artifact->requested_path;
  out.digest = artifact->digest;
  *owned_revision = NULL;
  if (!prepared.has_primary) out.revision = artifact->digest;
  else {
    /* `${digest}/${primary.declaration.requestedPath}/${primary.declaration.digest}`, whole. */
    const char *digest = artifact->digest ? artifact->digest : "";
    const char *path = prepared.primary_requested_path ? prepared.primary_requested_path : "";
    const char *primary = prepared.primary_digest ? prepared.primary_digest : "";
    size_t length = strlen(digest) + strlen(path) + strlen(primary) + 2;
    char *revision = malloc(length + 1);
    if (revision == NULL) qa_range_error("Out of memory");
    snprintf(revision, length + 1, "%s/%s/%s", digest, path, primary);
    out.revision = *owned_revision = revision;
  }
  return out;
}
/* TS preparedNativePrimary lives with the SharedSimulation: qa_app_shared_sim_native_primary
 * (shared_simulation_arsenal.c) looks up the builtin profile per prepared guest. */
