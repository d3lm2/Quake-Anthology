/* Port of src/content/q3/base/game/save-level.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_level.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (!d) qa_range_error("out of memory"); memcpy(d, v, n + 1); return d; }
qa_q3_level_capture qa_q3_capture_level(qa_q3_game_level *level) {
  qa_q3_level_capture c; c.values = qa_q3_capture_level_values(level);
  c.team_score_count = level->team_scores.length; c.team_scores = malloc((c.team_score_count ? c.team_score_count : 1) * sizeof *c.team_scores);
  for (size_t i = 0; i < c.team_score_count; i++) c.team_scores[i] = qa_q3_base_player_state_slots_get(&level->team_scores, (double)i);
  c.num_team_voting_clients[0] = level->base.num_team_voting_clients[0]; c.num_team_voting_clients[1] = level->base.num_team_voting_clients[1];
  c.sorted_count = QA_Q3_MAX_CLIENTS; c.sorted_clients = malloc(c.sorted_count * sizeof *c.sorted_clients);
  memcpy(c.sorted_clients, level->base.sorted_clients, c.sorted_count * sizeof *c.sorted_clients);
  c.vote = level->base.vote; c.vote.string = dupstr(level->base.vote.string); c.vote.display_string = dupstr(level->base.vote.display_string);
  c.team_votes[0] = level->base.team_votes[0]; c.team_votes[0].string = dupstr(level->base.team_votes[0].string);
  c.team_votes[1] = level->base.team_votes[1]; c.team_votes[1].string = dupstr(level->base.team_votes[1].string); return c;
}
void qa_q3_level_capture_destroy(qa_q3_level_capture *c) {
  qa_q3_level_values_destroy(&c->values); free(c->team_scores); free(c->sorted_clients);
  free(c->vote.string); free(c->vote.display_string); free(c->team_votes[0].string); free(c->team_votes[1].string);
}
static double field_number(const qa_save_reader *r, const char *name) {
  qa_save_reader f; qa_save_reader_field(r, name, &f); double v = qa_save_reader_number(&f); qa_save_reader_destroy(&f); return v;
}
static void assign_field_string(const qa_save_reader *r, const char *name, char **out) {
  qa_save_reader f; qa_save_reader_field(r, name, &f); char *v = dupstr(qa_save_reader_string(&f)); qa_save_reader_destroy(&f);
  free(*out); *out = v;
}
/* Lists and vote texts read before they are applied; a raise caught by qa_try releases them (TS: the GC). */
typedef struct level_scratch { double *scores, *voting, *sorted; char *strings[2]; } level_scratch;
static void level_scratch_free(void *arg) {
  level_scratch *s = arg; free(s->scores); free(s->voting); free(s->sorted); free(s->strings[0]); free(s->strings[1]);
  memset(s, 0, sizeof *s);
}
/* *out owns the array before its items are read. */
static void read_numbers(const qa_save_reader *r, const char *name, double **out, size_t *out_n) {
  qa_save_reader f; qa_save_reader_field(r, name, &f); size_t n = qa_save_reader_list_count(&f);
  double *d = malloc((n ? n : 1) * sizeof *d); if (!d) qa_range_error("out of memory");
  *out = d; *out_n = n;
  for (size_t i = 0; i < n; i++) { qa_save_reader item; qa_save_reader_item(&f, i, &item); d[i] = qa_save_reader_number(&item); qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&f);
}
void qa_q3_restore_level(qa_q3_game_level *level, const qa_save_value *value) {
  qa_save_reader reader; qa_save_reader_init(&reader, value, "q3.level"); qa_save_reader f;
  qa_save_reader_field(&reader, "values", &f); qa_q3_level_values vals = qa_q3_read_level_values(&f); qa_save_reader_destroy(&f);
  qa_q3_restore_level_values(level, &vals); qa_q3_level_values_destroy(&vals);
  level_scratch scratch; memset(&scratch, 0, sizeof scratch);
  qa_cleanup guard; qa_cleanup_push(&guard, level_scratch_free, &scratch);
  size_t ns, nv, nsort; read_numbers(&reader, "teamScores", &scratch.scores, &ns);
  read_numbers(&reader, "numTeamVotingClients", &scratch.voting, &nv);
  read_numbers(&reader, "sortedClients", &scratch.sorted, &nsort);
  /* TS reads every teamVotes entry (so a malformed one fails here, before the length check and before
     the level is touched) and applies the first two at the end. */
  qa_save_reader_field(&reader, "teamVotes", &f); size_t nt = qa_save_reader_list_count(&f);
  struct { double time, yes, no; } votes[2] = {{0, 0, 0}, {0, 0, 0}};
  for (size_t i = 0; i < nt; i++) {
    qa_save_reader vf0; qa_save_reader_item(&f, i, &vf0);
    double time = field_number(&vf0, "time"); double yes = field_number(&vf0, "yes"); double no = field_number(&vf0, "no");
    char *string = NULL; assign_field_string(&vf0, "string", &string);
    if (i < 2) { votes[i].time = time; votes[i].yes = yes; votes[i].no = no; scratch.strings[i] = string; } else free(string);
    qa_save_reader_destroy(&vf0);
  }
  qa_save_reader_destroy(&f);
  if (ns != level->team_scores.length || nv != 2 || nsort != QA_Q3_MAX_CLIENTS || nt != 2)
    qa_save_reader_fail(&reader, "Q3 level table length mismatch");
  for (size_t i = 0; i < ns; i++) qa_q3_base_player_state_slots_set(&level->team_scores, i, scratch.scores[i]);
  level->base.num_team_voting_clients[0] = scratch.voting[0]; level->base.num_team_voting_clients[1] = scratch.voting[1];
  memcpy(level->base.sorted_clients, scratch.sorted, nsort * sizeof *scratch.sorted);
  qa_save_reader_field(&reader, "vote", &f);
  level->base.vote.time = field_number(&f, "time"); level->base.vote.yes = field_number(&f, "yes"); level->base.vote.no = field_number(&f, "no");
  assign_field_string(&f, "string", &level->base.vote.string);
  assign_field_string(&f, "displayString", &level->base.vote.display_string);
  level->base.vote.execute_time = field_number(&f, "executeTime");
  qa_save_reader_destroy(&f);
  for (size_t i = 0; i < 2; i++) {
    qa_q3_ta_team_vote_state *team = &level->base.team_votes[i];
    team->time = votes[i].time; team->yes = votes[i].yes; team->no = votes[i].no;
    free(team->string); team->string = scratch.strings[i]; scratch.strings[i] = NULL;
  }
  qa_cleanup_pop(&guard);
  level_scratch_free(&scratch);
  qa_save_reader_destroy(&reader);
}
