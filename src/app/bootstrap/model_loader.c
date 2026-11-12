/* Port of src/app/bootstrap/model-loader.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/model_loader.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/formats/q3_model/replacements.h"
#include "qa/foundation/support.h"
struct qa_app_model_variants {
  qa_resolved_resource_reference resource;
  qa_decoded_model native_model;
  qa_model_replacement current;
  bool has_current;
  qa_md5_model replacement_model; /* owned backing for current */
};
struct qa_app_model_replacement_commit {
  qa_app_model_variants *variants;
  qa_model_replacement replacement;
  qa_md5_model model;
  bool has_replacement;
};
static bool rank_of(const qa_resolved_resource_reference *resource, double *out) {
  if (resource->resolution.kind == QA_RESOURCE_RESOLUTION_LINK) return false;
  *out = (double)resource->resolution.rank;
  return true;
}
static bool ends_with_ci(const char *text, const char *suffix) {
  size_t a = strlen(text), b = strlen(suffix);
  if (a < b) return false;
  for (size_t i = 0; i < b; i++)
    if (tolower((unsigned char)text[a - b + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
qa_app_model_variants *qa_app_model_variants_create(qa_resolved_resource_reference resource,
    qa_decoded_model native_model) {
  qa_app_model_variants *variants = calloc(1, sizeof *variants);
  if (variants == NULL) qa_range_error("Out of memory");
  variants->resource = resource;
  variants->native_model = native_model;
  return variants;
}
void qa_app_model_variants_destroy(qa_app_model_variants *variants) { free(variants); }
qa_decoded_model qa_app_model_variants_model(const qa_app_model_variants *variants) {
  qa_decoded_model model = variants->native_model;
  model.replacement = variants->has_current ? &variants->current : NULL;
  return model;
}
/* What prepare_pair owns while the host, the mounts or a texture load can raise. */
typedef struct pair_scope {
  qa_md5_paths paths;
  uint8_t *mesh, *animation, *scale, *skin;
  char *skin_path;
} pair_scope;
static void pair_scope_free(void *arg) {
  pair_scope *scope = arg;
  free(scope->mesh);
  free(scope->animation);
  free(scope->scale);
  free(scope->skin);
  free(scope->skin_path);
  qa_md5_paths_destroy(&scope->paths);
  memset(scope, 0, sizeof *scope);
}
static bool prepare_pair(qa_app_model_variants *variants, const qa_app_model_provider *provider,
    qa_app_model_replacement_commit *commit) {
  qa_game_family family = variants->native_model.kind == QA_DECODED_MODEL_Q1_ALIAS
      ? QA_GAME_FAMILY_Q1 : QA_GAME_FAMILY_Q2;
  pair_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, pair_scope_free, &scope);
  qa_md5_paths_for(variants->resource.requested_path, qa_game_family_name(family), &scope.paths);
  qa_content_opened_resource mesh, animation, scale;
  bool ok = false;
  if (!qa_content_mounted_open(provider->mounts, scope.paths.mesh_path, NULL, NULL, &mesh)) goto done;
  scope.mesh = mesh.bytes;
  {
    double resource_rank = 0, mesh_rank = 0;
    bool has_resource = rank_of(&variants->resource, &resource_rank);
    bool has_mesh = rank_of(mesh.reference, &mesh_rank);
    if (!qa_md5_replacement_allowed(has_resource, resource_rank, has_mesh, mesh_rank)) goto done;
  }
  if (!qa_content_mounted_open(provider->mounts, scope.paths.anim_path, NULL, NULL, &animation)) goto done;
  scope.animation = animation.bytes;
  bool has_scale = family == QA_GAME_FAMILY_Q2
      && qa_content_mounted_open(provider->mounts, scope.paths.scale_path, NULL, NULL, &scale);
  if (has_scale) scope.scale = scale.bytes;
  const char *const *skins = NULL;
  size_t skin_count = 0;
  bool sidecars = false;
  qa_md5_model model = {0};
  bool assembled = provider->md5_replacement(provider->ctx, family,
      mesh.bytes, mesh.byte_length, scope.paths.mesh_path,
      animation.bytes, animation.byte_length, scope.paths.anim_path,
      has_scale ? scale.bytes : NULL, has_scale ? scale.byte_length : 0,
      has_scale ? scope.paths.scale_path : NULL, &variants->native_model, &model,
      &skins, &skin_count, &sidecars);
  if (assembled && family == QA_GAME_FAMILY_Q1) {
    /* TS: skinSelection kind is asserted by the host assembly;
     * each donor skin sidecar must exist and decode. */
    for (size_t i = 0; assembled && i < skin_count; i++) {
      size_t length = strlen(skins[i]);
      scope.skin_path = malloc(length + sizeof ".lmp");
      if (scope.skin_path == NULL) qa_range_error("Out of memory");
      memcpy(scope.skin_path, skins[i], length);
      memcpy(scope.skin_path + length, ".lmp", sizeof ".lmp");
      qa_content_opened_resource skin;
      if (!qa_content_mounted_open(provider->mounts, scope.skin_path, NULL, NULL, &skin)) { assembled = false; break; }
      scope.skin = skin.bytes;
      bool valid = provider->validate_qpic(provider->ctx, skin.bytes, skin.byte_length, scope.skin_path);
      free(scope.skin);
      scope.skin = NULL;
      if (!valid) { assembled = false; break; }
      provider->load_texture(provider->ctx, scope.skin_path, QA_GAME_FAMILY_Q1);
      free(scope.skin_path);
      scope.skin_path = NULL;
    }
  }
  if (assembled && family == QA_GAME_FAMILY_Q2 && !sidecars) {
    for (size_t i = 0; i < skin_count; i++)
      provider->load_texture(provider->ctx, skins[i], QA_GAME_FAMILY_Q2);
  }
  if (assembled) {
    commit->model = model;
    commit->replacement.resource = *mesh.reference;
    commit->replacement.model = model;
    commit->has_replacement = true;
    ok = true;
  }
done:
  qa_cleanup_pop(&guard);
  pair_scope_free(&scope);
  return ok;
}
qa_app_model_replacement_commit *qa_app_model_variants_prepare_replacement(
    qa_app_model_variants *variants, const qa_app_model_provider *provider, bool enabled) {
  qa_app_model_replacement_commit *commit = calloc(1, sizeof *commit);
  if (commit == NULL) qa_range_error("Out of memory");
  commit->variants = variants;
  if (enabled) prepare_pair(variants, provider, commit);
  return commit;
}
void qa_app_model_replacement_commit_apply(qa_app_model_replacement_commit *commit) {
  commit->variants->replacement_model = commit->model;
  commit->variants->current = commit->replacement;
  commit->variants->current.model = commit->variants->replacement_model;
  commit->variants->has_current = commit->has_replacement;
}
void qa_app_model_replacement_commit_destroy(qa_app_model_replacement_commit *commit) { free(commit); }
qa_app_loaded_model qa_app_load_application_model(const qa_app_model_provider *provider,
    const qa_content_opened_resource *asset, bool enhanced_models) {
  const char *path = asset->reference->requested_path;
  (void)ends_with_ci; /* host decode_model owns the .md3 branch */
  const qa_decoded_model *decoded = provider->decode_model(provider->ctx, asset->bytes, asset->byte_length, path);
  qa_app_loaded_model out = {0};
  out.resource = *asset->reference;
  bool alias = (provider->family == QA_GAME_FAMILY_Q1 && decoded->kind == QA_DECODED_MODEL_Q1_ALIAS)
      || (provider->family == QA_GAME_FAMILY_Q2 && decoded->kind == QA_DECODED_MODEL_Q2_ALIAS);
  if (alias) {
    qa_app_model_variants *variants = qa_app_model_variants_create(*asset->reference, *decoded);
    qa_app_model_replacement_commit *commit =
        qa_app_model_variants_prepare_replacement(variants, provider, enhanced_models);
    qa_app_model_replacement_commit_apply(commit);
    qa_app_model_replacement_commit_destroy(commit);
    out.model = qa_app_model_variants_model(variants);
    out.variants = variants;
    return out;
  }
  out.model = *decoded;
  return out;
}
