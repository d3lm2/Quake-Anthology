/* Port of src/content/q1/missionpacks/world/common.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/common.h"
#include "../missionpacks_internal.h"

void qa_q1mp_world_number(qa_q1_actor *entity, const char *key, double value) {
  qa_q1mp_field_number(entity, key, qa_q1mp_fround(value));
}
void qa_q1mp_world_vector(qa_q1_actor *entity, const char *key, qa_vec3 value) {
  char buf[128];
  /* TS template interpolates Math.fround(x) as JS numbers. */
  (void)snprintf(buf, sizeof buf, "%s %s %s", QA_JS_NUMBER(qa_q1mp_fround(value.x)),
                 QA_JS_NUMBER(qa_q1mp_fround(value.y)), QA_JS_NUMBER(qa_q1mp_fround(value.z)));
  qa_q1mp_field_text(entity, key, buf);
}
void qa_q1mp_world_later(qa_q1_services *game, qa_q1_actor *entity, double delay, const char *name) {
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, name));
}
static void zero_angles(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = QA_Q1_ZERO;
  qa_q1_set_body(game, entity, &patch);
}
void qa_q1mp_world_trigger(qa_q1_services *game, qa_q1_actor *entity) {
  entity->movedir = qa_q1_move_direction(qa_q1_get_body(game, entity).angles, game);
  entity->solid = "trigger"; qa_q1mp_set_string(&entity->model, "");
  zero_angles(game, entity);
}
void qa_q1mp_world_brush(qa_q1_services *game, qa_q1_actor *entity) {
  entity->mangle = qa_q1_get_body(game, entity).angles;
  entity->solid = "bsp"; entity->movement = "push";
  zero_angles(game, entity);
}
/* TS `finally { entity.target = prior; entity.message = priorMessage }`: also runs when a target raises. */
typedef struct target_event_restore { qa_q1_actor *entity; char *prior, *prior_message; } target_event_restore;
static void restore_target_event(void *arg) {
  const target_event_restore *restore = arg;
  free(restore->entity->target); free(restore->entity->message);
  restore->entity->target = restore->prior; restore->entity->message = restore->prior_message;
}
/* message NULL = TS default "". The prior strings are restored as TS finally does. */
void qa_q1mp_world_target_event(qa_q1_services *game, qa_q1_actor *entity, const char *target, const char *message) {
  /* The event strings are copied first: they may alias the fields replaced below. */
  char *event_target = NULL, *event_message = NULL;
  qa_q1mp_set_string(&event_target, target);
  qa_q1mp_set_string(&event_message, message != NULL ? message : "");
  target_event_restore restore = {entity, entity->target, entity->message};
  entity->target = event_target; entity->message = event_message;
  qa_cleanup node;
  qa_cleanup_push(&node, restore_target_event, &restore);
  const qa_actor_id activator = entity->activator;
  qa_q1_use_targets(game, entity, entity->has_activator ? &activator : NULL);
  qa_cleanup_pop(&node);
  restore_target_event(&restore);
}
