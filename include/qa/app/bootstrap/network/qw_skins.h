/* Port of src/app/bootstrap/network/qw-skins.ts. Async read/cache
 * -> synchronous with the same 128-entry clear-on-full cache. The
 * landed PCX decoder aborts on malformed bytes where the TS catch
 * returns null. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_QW_SKINS_H
#define QA_APP_BOOTSTRAP_NETWORK_QW_SKINS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/scene.h"
typedef struct qa_app_qw_skin_options {
  void *ctx;
  /* Malloc'd bytes (caller-owned via *out) or false for TS null. */
  bool (*read)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length);
  double (*noskins)(void *ctx);
  const char *(*baseskin)(void *ctx);
  const char *(*allskins)(void *ctx);
} qa_app_qw_skin_options;
typedef struct qa_app_qw_player_skins qa_app_qw_player_skins;
qa_app_qw_player_skins *qa_app_qw_player_skins_create(qa_app_qw_skin_options options);
void qa_app_qw_player_skins_destroy(qa_app_qw_player_skins *skins);
/* Malloc'd skin name; NULL only on OOM. */
char *qa_app_qw_player_skins_name(qa_app_qw_player_skins *skins, const char *userinfo_skin);
/* Borrowed cached skin or NULL (TS null). Its name and pixels stay valid until
 * qa_app_qw_player_skins_release_retired after the clear that drops it (TS callers keep the
 * resolved object after the cache forgets it), or until destroy. */
const qa_indexed_model_skin *qa_app_qw_player_skins_select(
    qa_app_qw_player_skins *skins, const char *userinfo_skin);
/* TS cache.clear(): forgets every entry; the skins already handed out are retired, not freed. */
void qa_app_qw_player_skins_clear(qa_app_qw_player_skins *skins);
/* Frees the storage of skins dropped by earlier clears; call once no selection holds them. */
void qa_app_qw_player_skins_release_retired(qa_app_qw_player_skins *skins);
#endif
