/* Port of src/network/q2/codecs/codec.ts (types only; the vtable lives
 * in the header). GPL-2.0-or-later. */
#include "qa/network/q2/codecs/codec.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_q2_clc_batch_move_free(qa_q2_clc_batch_move *move) {
  if (move->frames != NULL) {
    for (size_t i = 0; i < move->frame_count; i++) free(move->frames[i].cmds);
    free(move->frames);
  }
  move->frames = NULL;
  move->frame_count = 0;
}

void qa_q2_server_data_result_clear(qa_q2_server_data_result *result) {
  free(result->gamedir);
  free(result->levelname);
  result->gamedir = NULL;
  result->levelname = NULL;
}
static char *copy_text(const char *text) {
  if (text == NULL) text = "";
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
void qa_q2_server_data_result_set_strings(qa_q2_server_data_result *result, const char *gamedir,
                                          const char *levelname) {
  char *gamedir_copy = copy_text(gamedir);
  char *levelname_copy = copy_text(levelname);
  if (gamedir_copy == NULL || levelname_copy == NULL) {
    free(gamedir_copy);
    free(levelname_copy);
    qa_range_error("Out of memory");
  }
  free(result->gamedir);
  free(result->levelname);
  result->gamedir = gamedir_copy;
  result->levelname = levelname_copy;
}
void qa_q2_server_data_result_copy(qa_q2_server_data_result *out, const qa_q2_server_data_result *in) {
  qa_q2_server_data_result copy = *in;
  copy.gamedir = NULL;
  copy.levelname = NULL;
  qa_q2_server_data_result_set_strings(&copy, in->gamedir, in->levelname);
  *out = copy;
}
