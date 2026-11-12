/* Port of src/app/bootstrap/seat-profile.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/seat_profile.h"

#include <stdio.h>
#include <stdlib.h>

#include "qa/content/user_data.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"

#define GLOBAL_TUNING "input/global-tuning.json"

bool qa_app_global_tuning_store(const char *user_content_root, qa_config_store *out) {
  char *root = qa_user_data_directory("settings", user_content_root);
  if (root == NULL) return false;
  bool ok = qa_config_store_init(out, root);
  free(root);
  return ok;
}

typedef struct load_global {
  const qa_config_store *store;
  qa_global_input_tuning *out;
  bool found;
} load_global;
static void load_global_run(void *ctx) {
  load_global *load = ctx;
  load->found = qa_config_store_load_global_tuning(load->store, GLOBAL_TUNING, load->out);
}

bool qa_app_load_seat_profile(const qa_config_store *settings, const char *user_content_root, double index,
                              qa_seat_settings *out) {
  /* `input/seat-${index + 1}.json` */
  char number[48], name[96];
  qa_net_js_number(index + 1, number, sizeof number);
  (void)snprintf(name, sizeof name, "input/seat-%s.json", number);
  if (!qa_config_store_load_seat(settings, name, out)) return false;
  if (index != 0) return true;
  qa_config_store store;
  if (!qa_app_global_tuning_store(user_content_root, &store)) qa_range_error("Out of memory");
  qa_global_input_tuning global;
  load_global load = {&store, &global, false};
  qa_caught_error caught;
  /* An unreadable shared document leaves the product's own tuning in force. */
  if (!qa_try(load_global_run, &load, &caught)) load.found = false;
  qa_config_store_destroy(&store);
  if (!load.found) return true;
  out->mouse = global.mouse;
  out->rumble = global.rumble;
  out->rumble_strength = global.rumble_strength;
  if (global.has_always_run) {
    out->has_always_run = true;
    out->always_run = global.always_run;
  }
  return true;
}

void qa_app_save_shared_tuning(const char *user_content_root, const qa_seat_settings *seat) {
  qa_config_store store;
  if (!qa_app_global_tuning_store(user_content_root, &store)) qa_range_error("Out of memory");
  qa_global_input_tuning tuning = {
    .version = 1,
    .mouse = seat->mouse,
    .has_always_run = seat->has_always_run,
    .always_run = seat->always_run,
    .rumble = seat->rumble,
    .rumble_strength = seat->rumble_strength_absent ? 1 : seat->rumble_strength, /* parseGlobalInputTuning's default */
  };
  bool saved = qa_config_store_save_global_tuning(&store, GLOBAL_TUNING, &tuning);
  qa_config_store_destroy(&store);
  if (!saved) qa_settings_raise_write_failure();
}
