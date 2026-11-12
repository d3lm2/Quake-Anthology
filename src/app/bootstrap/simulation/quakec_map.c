/* Port of src/app/bootstrap/simulation/quakec-map.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/types.h"
#include "qa/foundation/support.h"
static char *xstrdup(const char *s) { size_t n = strlen(s); char *o = malloc(n + 1); if (!o) qa_range_error("Out of memory"); memcpy(o, s, n + 1); return o; }
static const char *world_entities(qa_decoded_world w) {
  switch (w.kind) {
    case QA_DECODED_WORLD_Q1: return w.as.q1->entities;
    case QA_DECODED_WORLD_Q2: return w.as.q2->entities;
    case QA_DECODED_WORLD_Q3: return w.as.q3->entities;
  }
  return "";
}
char *qa_app_sim_quakec_map_entities(qa_decoded_world world, const char *mode) {
  const char *text = world_entities(world);
  if (world.kind == QA_DECODED_WORLD_Q1) return xstrdup(text);
  size_t count = 0;
  qa_q1_entity *entities = qa_q1_parse_entities(text, &count);
  bool deathmatch = mode != NULL && strcmp(mode, "deathmatch") == 0;
  const char *wanted = deathmatch ? "info_player_deathmatch" : "info_player_start";
  bool has = false;
  for (size_t i = 0; i < count; i++) {
    const char *c = qa_q1_entity_value(&entities[i], "classname");
    if (c && strcmp(c, wanted) == 0) has = true;
  }
  /* Extra entities appended by the TS are collected separately (no limit: one per matching candidate). */
  size_t *extras = calloc(count ? count : 1, sizeof *extras); size_t extra_count = 0;
  if (!extras) { qa_q1_entities_destroy(entities, count); qa_range_error("Out of memory"); }
  if (!has) {
    static const char *const dm[] = {"info_player_start", "info_player_coop", "team_CTF_redplayer", "team_CTF_blueplayer", "team_CTF_redspawn", "team_CTF_bluespawn"};
    static const char *const sp[] = {"info_player_deathmatch", "team_CTF_redplayer", "team_CTF_blueplayer"};
    const char *const *candidates = deathmatch ? dm : sp; size_t cand_count = deathmatch ? 6 : 3;
    for (size_t i = 0; i < count; i++) {
      const char *c = qa_q1_entity_value(&entities[i], "classname");
      bool match = false;
      for (size_t k = 0; k < cand_count; k++) if (strcmp(c ? c : "", candidates[k]) == 0) match = true;
      if (match) extras[extra_count++] = i;
    }
  }
  size_t total = count + extra_count;
  size_t cap = 1024, len = 0; char *out = malloc(cap);
  if (!out) { free(extras); qa_q1_entities_destroy(entities, count); qa_range_error("Out of memory"); }
  for (size_t e = 0; e < total; e++) {
    size_t src = e < count ? e : extras[e - count];
    bool replaced = e >= count;
    qa_q1_entity *entity = &entities[src];
    /* Build "{\n" + quoted fields + "\n}\n"; quote() refuses a '"' in any key or value. */
    size_t need = 4;
    for (size_t p = 0; p < entity->property_count; p++) {
      const char *key = entity->properties[p].key;
      const char *value = (replaced && strcmp(key, "classname") == 0) ? wanted : entity->properties[p].value;
      if (strchr(key, '"') || strchr(value, '"')) {
        free(out); free(extras); qa_q1_entities_destroy(entities, count);
        qa_raise("Error", 0, 0, "QuakeC entity field cannot be quoted");
      }
      need += strlen(key) + strlen(value) + 6;
    }
    if (len + need + 1 > cap) {
      while (len + need + 1 > cap) cap *= 2;
      char *grown = realloc(out, cap);
      if (!grown) { free(out); free(extras); qa_q1_entities_destroy(entities, count); qa_range_error("Out of memory"); }
      out = grown;
    }
    len += (size_t)sprintf(out + len, "{\n");
    for (size_t p = 0; p < entity->property_count; p++) {
      const char *key = entity->properties[p].key;
      const char *value = (replaced && strcmp(key, "classname") == 0) ? wanted : entity->properties[p].value;
      len += (size_t)sprintf(out + len, "%s\"%s\" \"%s\"", p ? "\n" : "", key, value);
    }
    len += (size_t)sprintf(out + len, "\n}\n");
  }
  out[len] = '\0';
  free(extras);
  qa_q1_entities_destroy(entities, count);
  return out;
}
