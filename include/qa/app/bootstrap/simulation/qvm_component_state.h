/* Port of src/app/bootstrap/simulation/qvm-component-state.ts: the host state every QVM component source (weapon
 * behavior, grapple) keeps for its borrowed qagame image, and the spatial imports both share.
 * C accommodations:
 *  - The two Map<number, string> members are insertion-ordered lists (qa_app_sim_qvm_component_texts).
 *  - capture() returns a record the callers spread into their own; here it adds its fields, in the TS key order,
 *    to the caller's record.
 *  - qvmComponentSpatial's options are fn+ctx; the returned operations call back through the spatial object, which
 *    must stay at its address for as long as the services use it.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QVM_COMPONENT_STATE_H
#define QA_APP_BOOTSTRAP_SIMULATION_QVM_COMPONENT_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/contracts/identity.h"
#include "qa/core/common_parse.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/index.h"

/* TS qvmWeaponInitializationEntities: only source initialization and client spawn points are needed by a
 * trajectory component. Malloc'd entity text; caller frees. */
char *qa_app_sim_qvm_weapon_initialization_entities(const char *text);

/* One entry of a Map<number, string>, in insertion order. */
typedef struct qa_app_sim_qvm_component_text { double key; char *value; } qa_app_sim_qvm_component_text;
typedef struct qa_app_sim_qvm_component_texts {
  qa_app_sim_qvm_component_text *entries; size_t count, capacity;
} qa_app_sim_qvm_component_texts;
/* Map.get: the value, or NULL for undefined. */
const char *qa_app_sim_qvm_component_texts_get(const qa_app_sim_qvm_component_texts *texts, double key);
/* Map.set: copies the value; an existing key keeps its place. */
void qa_app_sim_qvm_component_texts_set(qa_app_sim_qvm_component_texts *texts, double key, const char *value);
void qa_app_sim_qvm_component_texts_delete(qa_app_sim_qvm_component_texts *texts, double key);
void qa_app_sim_qvm_component_texts_clear(qa_app_sim_qvm_component_texts *texts);

/* TS QvmComponentState. cvars is borrowed. */
typedef struct qa_app_sim_qvm_component_state {
  qa_app_sim_qvm_component_texts configstrings; /* index -> value */
  qa_app_sim_qvm_component_texts userinfo;      /* client slot -> userinfo */
  qa_common_parse_state parser;
  qa_common_parse_cursor cursor;
  bool has_cursor;
  qa_cvar_registry *cvars;
} qa_app_sim_qvm_component_state;
/* new QvmComponentState(entityText, cvars): the cursor reads qvmWeaponInitializationEntities(entityText). */
void qa_app_sim_qvm_component_state_init(qa_app_sim_qvm_component_state *state, const char *entity_text, qa_cvar_registry *cvars);
void qa_app_sim_qvm_component_state_destroy(qa_app_sim_qvm_component_state *state);
/* capture(data, files): the source-neutral part of the host checkpoint (data, cvars, configstrings, userinfo, files,
 * entityText, cursor, parser) added to `record`; callers add their own scalar fields before it. */
void qa_app_sim_qvm_component_state_capture(const qa_app_sim_qvm_component_state *state, const qa_qvm_game_data *data,
                                            qa_qvm_files *files, qa_save_value *record);
/* restore(reader, data, files, label): raises `Invalid saved ${label} client` / `... configstring` as the TS throws. */
void qa_app_sim_qvm_component_state_restore(qa_app_sim_qvm_component_state *state, const qa_save_reader *reader,
                                            qa_qvm_game_data *data, qa_qvm_files *files, const char *label);

/* TS QvmComponentSpatialOptions. */
typedef struct qa_app_sim_qvm_component_spatial_options {
  qa_shared_scene_queries *scene;
  qa_cvar_registry *cvars;
  qa_qvm_game_data *data;
  const char *label; /* static text */
  void *ctx;
  /* actorForSlot(slot): false is the TS null. */
  bool (*actor_for_slot)(void *ctx, double slot, qa_actor_id *out);
  double (*slot_of)(void *ctx, qa_actor_id actor);
  /* Actors the component owns itself and must not see in its own area queries. NULL is the TS undefined. */
  bool (*hidden)(void *ctx, qa_actor_id actor);
  /* reject(operation): "perform undeclared brush contact" | "bind a primary brush" | "change world portals". The TS
   * callback returns never: this one must raise. */
  void (*reject)(void *ctx, const char *operation);
} qa_app_sim_qvm_component_spatial_options;
/* TS qvmComponentSpatial: the spatial imports shared by the QVM component sources: collision, contents, PVS and entity
 * linking all come from shared scene queries. */
typedef struct qa_app_sim_qvm_component_spatial {
  qa_app_sim_qvm_component_spatial_options options;
  qa_qvm_server_spatial_operations operations;
} qa_app_sim_qvm_component_spatial;
void qa_app_sim_qvm_component_spatial_init(qa_app_sim_qvm_component_spatial *spatial, qa_app_sim_qvm_component_spatial_options options);
#endif
