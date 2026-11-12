/* Port of src/settings/native-config.ts. GPL-2.0-or-later. */
#include "qa/settings/native_config.h"

#include <errno.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/content/roots.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/node_path.h"
#include "qa/foundation/support.h"
#include "qa/platform/files/contained.h"
#include "qa/settings/config.h"

static noreturn void out_of_memory(void) { qa_raise("Error", 0, 0, "%s", "out of memory"); }
static void *checked(void *pointer) {
  if (pointer == NULL) out_of_memory();
  return pointer;
}
static char *dup_range(const char *text, size_t length) {
  char *copy = (char *)checked(malloc(length + 1));
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}
static char *dup_text(const char *text) { return dup_range(text, strlen(text)); }
static char *lower(const char *text) { return (char *)checked(qa_js_to_lower_case(text)); }
/* A malloc'd printf. */
static char *format_text(const char *format, const char *a, const char *b, const char *c, const char *d) {
  size_t length = strlen(format) + strlen(a) + strlen(b) + strlen(c) + strlen(d) + 1;
  char *text = (char *)checked(malloc(length));
  (void)snprintf(text, length, format, a, b, c, d);
  return text;
}

/* An owned list of strings. */
typedef struct text_list {
  char **items;
  size_t count;
  size_t capacity;
} text_list;
static void list_reserve(text_list *list, size_t extra) {
  if (list->count + extra <= list->capacity) return;
  size_t capacity = list->capacity == 0 ? 16 : list->capacity;
  while (list->count + extra > capacity) capacity *= 2;
  list->items = (char **)checked(realloc(list->items, capacity * sizeof *list->items));
  list->capacity = capacity;
}
/* Takes ownership of `text`. */
static void list_push(text_list *list, char *text) {
  list_reserve(list, 1);
  list->items[list->count++] = text;
}
/* Inserts copies of `other` at `index`. */
static void list_insert_copies(text_list *list, size_t index, const text_list *other) {
  if (other->count == 0) return;
  list_reserve(list, other->count);
  memmove(list->items + index + other->count, list->items + index, (list->count - index) * sizeof *list->items);
  for (size_t i = 0; i < other->count; i++) list->items[index + i] = dup_text(other->items[i]);
  list->count += other->count;
}
static void list_push_copies(text_list *list, const text_list *other) { list_insert_copies(list, list->count, other); }
static void list_free(text_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  free(list->items);
  memset(list, 0, sizeof *list);
}
/* lines.join(separator) + separator (an empty list is the separator alone). */
static char *list_join_terminated(const text_list *list, const char *separator) {
  size_t separator_length = strlen(separator), total = separator_length + 1;
  for (size_t i = 0; i < list->count; i++) total += strlen(list->items[i]) + separator_length;
  char *text = (char *)checked(malloc(total));
  size_t at = 0;
  for (size_t i = 0; i < list->count; i++) {
    size_t length = strlen(list->items[i]);
    memcpy(text + at, list->items[i], length);
    at += length;
    memcpy(text + at, separator, separator_length);
    at += separator_length;
  }
  if (list->count == 0) {
    memcpy(text, separator, separator_length);
    at = separator_length;
  }
  text[at] = '\0';
  return text;
}

/* --- Key and command names of the rerelease engine --- */

/* Key names the rerelease engine writes in place of this engine's, from the key table in its executable. The first
 * name for a key is the one written back. Keypad digits follow the layout of the keypad navigation keys. */
static const char *const kex_key_pairs[][2] = {
  {"mouse_left", "mouse1"},        {"mouse_right", "mouse2"},     {"mouse_middle", "mouse3"},
  {"mouse_misc1", "mouse4"},       {"mouse_misc2", "mouse5"},     {"mouse_wheel_up", "mwheelup"},
  {"mouse_wheel_down", "mwheeldown"}, {"lshift", "shift"},        {"lctrl", "ctrl"},
  {"lalt", "alt"},                 {"up", "uparrow"},             {"down", "downarrow"},
  {"left", "leftarrow"},           {"right", "rightarrow"},       {"pageup", "pgup"},
  {"pagedown", "pgdn"},            {"insert", "ins"},             {"delete", "del"},
  {"caps_lock", "capslock"},       {"kp_0", "kp_ins"},            {"kp_1", "kp_end"},
  {"kp_2", "kp_downarrow"},        {"kp_3", "kp_pgdn"},           {"kp_4", "kp_leftarrow"},
  {"kp_6", "kp_rightarrow"},       {"kp_7", "kp_home"},           {"kp_8", "kp_uparrow"},
  {"kp_9", "kp_pgup"},             {"kp_period", "kp_del"},       {"kp_divide", "kp_slash"},
  {"kp_mul", "kp_star"},
};
/* Names the rerelease also accepts for a key; they are read but never written. */
static const char *const kex_key_aliases[][2] = {
  {"rshift", "shift"}, {"rctrl", "ctrl"}, {"ralt", "alt"},   {"numlock", "kp_numlock"}, {"grave", "`"},
  {"backquote", "`"},  {"minus", "-"},    {"equals", "="},   {"comma", ","},            {"period", "."},
  {"slash", "/"},      {"backslash", "\\"}, {"quote", "'"},
};
/* The rerelease Quake binds actions with a leading star where this engine uses plus commands. */
static const char *const kex_commands[][2] = {
  {"*in_attack", "+attack"},     {"*in_back", "+back"},           {"*in_down", "+movedown"},
  {"*in_forward", "+forward"},   {"*in_jump", "+jump"},           {"*in_left", "+left"},
  {"*in_lookdown", "+lookdown"}, {"*in_lookup", "+lookup"},       {"*in_right", "+right"},
  {"*in_strafe", "+strafe"},     {"*in_moveleft", "+moveleft"},   {"*in_moveright", "+moveright"},
  {"*in_speed", "+speed"},       {"*in_up", "+moveup"},           {"*showscores", "+showscores"},
};
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* `${prefix}${n}` for a canonical decimal n in first..last: the number, or -1. */
static int numbered(const char *text, const char *prefix, int first, int last) {
  size_t prefix_length = strlen(prefix);
  if (strncmp(text, prefix, prefix_length) != 0) return -1;
  const char *digits = text + prefix_length;
  if (digits[0] == '\0' || (digits[0] == '0' && digits[1] != '\0')) return -1;
  int value = 0;
  for (const char *at = digits; *at != '\0'; at++) {
    if (*at < '0' || *at > '9' || value > last) return -1;
    value = value * 10 + (*at - '0');
  }
  return value >= first && value <= last ? value : -1;
}
/* KEX_KEYS.get(key.toLowerCase()) ?? key, malloc'd. */
static char *kex_key_to_ours(const char *key) {
  char *folded = lower(key);
  const char *found = NULL;
  char joystick[16];
  for (size_t i = 0; i < COUNT(kex_key_pairs) && found == NULL; i++)
    if (strcmp(kex_key_pairs[i][0], folded) == 0) found = kex_key_pairs[i][1];
  int index = found == NULL ? numbered(folded, "joy_", 0, 31) : -1;
  if (index >= 0) {
    (void)snprintf(joystick, sizeof joystick, "joy%d", index + 1);
    found = joystick;
  }
  for (size_t i = 0; i < COUNT(kex_key_aliases) && found == NULL; i++)
    if (strcmp(kex_key_aliases[i][0], folded) == 0) found = kex_key_aliases[i][1];
  char *out = dup_text(found != NULL ? found : key);
  free(folded);
  return out;
}
/* TS kexKey: KEX_KEYS_BACK.get(key.toLowerCase()) ?? key.toLowerCase(), malloc'd. */
static char *kex_key(const char *key) {
  char *folded = lower(key);
  for (size_t i = 0; i < COUNT(kex_key_pairs); i++) {
    if (strcmp(kex_key_pairs[i][1], folded) != 0) continue;
    free(folded);
    return dup_text(kex_key_pairs[i][0]);
  }
  int index = numbered(folded, "joy", 1, 32);
  if (index < 0) return folded;
  free(folded);
  char joystick[16];
  (void)snprintf(joystick, sizeof joystick, "joy_%d", index - 1);
  return dup_text(joystick);
}
/* KEX_COMMANDS.get(command.toLowerCase()), static text or NULL. */
static const char *kex_command_to_ours(const char *command) {
  char *folded = lower(command);
  const char *found = NULL;
  for (size_t i = 0; i < COUNT(kex_commands) && found == NULL; i++)
    if (strcmp(kex_commands[i][0], folded) == 0) found = kex_commands[i][1];
  free(folded);
  return found;
}
/* KEX_COMMANDS_BACK.get(command.toLowerCase()), static text or NULL. */
static const char *kex_command_from_ours(const char *command) {
  char *folded = lower(command);
  const char *found = NULL;
  for (size_t i = 0; i < COUNT(kex_commands) && found == NULL; i++)
    if (strcmp(kex_commands[i][1], folded) == 0) found = kex_commands[i][0];
  free(folded);
  return found;
}

/* --- Layout --- */

/* GLQuake writes quakeNN.tga beside the game, Quake II scrnshot/quakeNN, Quake III screenshots/shotNNNN. */
static qa_screenshot_naming screenshot_naming(qa_game_family family) {
  switch (family) {
    case QA_GAME_FAMILY_Q1: return (qa_screenshot_naming){"", "quake", 2};
    case QA_GAME_FAMILY_Q2: return (qa_screenshot_naming){"scrnshot", "quake", 2};
    default: return (qa_screenshot_naming){"screenshots", "shot", 4};
  }
}

/* os.homedir(): $HOME, else the password database entry. */
static const char *home_directory(void) {
  const char *home = getenv("HOME");
  if (home != NULL && home[0] != '\0') return home;
  const struct passwd *entry = getpwuid(getuid());
  return entry != NULL && entry->pw_dir != NULL ? entry->pw_dir : "/";
}

/* TS savedGamesRoot: malloc'd, or NULL for the TS null. */
static char *saved_games_root(qa_game_family family, const char *home) {
  qa_nightdive_addon_root *roots = NULL;
  size_t count = 0;
  if (!qa_nightdive_addon_directories(home, &roots, &count)) out_of_memory();
  char *found = NULL;
  for (size_t i = 0; i < count && found == NULL; i++)
    if (roots[i].family == family) found = dup_text(roots[i].root);
  qa_nightdive_addon_roots_free(roots, count);
  return found;
}

/* TS placed: takes ownership of `real` and `alt`; false when neither exists. */
static bool placed(const char *name, char *real, char *alt, bool binds, bool reads_redirect, qa_native_config_file *out) {
  if (real == NULL && alt == NULL) return false;
  memset(out, 0, sizeof *out);
  out->name = dup_text(name);
  out->path = real != NULL ? real : dup_text(alt);
  out->binds = binds;
  out->redirected = alt;
  out->reads_redirect = alt != NULL && reads_redirect;
  return true;
}

void qa_native_config_layout_destroy(qa_native_config_layout *layout) {
  for (size_t i = 0; i < layout->file_count; i++) {
    free(layout->files[i].name);
    free(layout->files[i].path);
    free(layout->files[i].redirected);
  }
  free(layout->files);
  free(layout->directory);
  for (size_t i = 0; i < layout->fallback_count; i++) free(layout->fallbacks[i]);
  free(layout->fallbacks);
  memset(layout, 0, sizeof *layout);
}

static bool classic_layout(const qa_native_config_product *product, const char *redirect, bool reads_redirect,
                           bool exclusive, const char *name, qa_layer_format format, qa_native_config_layout *out) {
  char *real = product->loose_root == NULL || exclusive ? NULL : qa_node_path_join2(product->loose_root, name);
  char *alt = redirect == NULL ? NULL : qa_node_path_join3(redirect, product->content_directory, name);
  qa_native_config_file file;
  if (!placed(name, real, alt, true, reads_redirect, &file)) return false;
  memset(out, 0, sizeof *out);
  out->format = format;
  out->script = strcmp(name, "q3config.cfg") == 0 ? "q3config.cfg" : "config.cfg";
  out->files = (qa_native_config_file *)checked(malloc(sizeof *out->files));
  out->files[0] = file;
  out->file_count = 1;
  out->directory = qa_node_path_dirname(file.redirected != NULL ? file.redirected : file.path);
  char *path_directory = qa_node_path_dirname(file.path);
  if (strcmp(path_directory, out->directory) == 0) {
    free(path_directory);
  } else {
    out->fallbacks = (char **)checked(malloc(sizeof *out->fallbacks));
    out->fallbacks[0] = path_directory;
    out->fallback_count = 1;
  }
  out->screenshots = screenshot_naming(product->family);
  return true;
}

bool qa_native_config_layout_build(const qa_native_config_product *product, const char *home, const char *redirect,
                                   bool reads_redirect, qa_native_config_layout *out) {
  bool rerelease = strcmp(product->edition, "rerelease") == 0;
  /* A redirect that answers reads too replaces the real location outright. */
  bool exclusive = redirect != NULL && reads_redirect;
  if (product->family == QA_GAME_FAMILY_Q3)
    return classic_layout(product, redirect, reads_redirect, exclusive, "q3config.cfg", QA_LAYER_FORMAT_QUAKE3, out);
  if (!rerelease)
    return classic_layout(product, redirect, reads_redirect, exclusive, "config.cfg",
                          product->family == QA_GAME_FAMILY_Q1 ? QA_LAYER_FORMAT_QUAKE : QA_LAYER_FORMAT_QUAKE2, out);
  bool q1 = product->family == QA_GAME_FAMILY_Q1;
  char *real = exclusive ? NULL : saved_games_root(product->family, home != NULL ? home : home_directory());
  char *alt = NULL;
  if (redirect != NULL) {
    const char *parts[4] = {redirect, "Saved Games", "Nightdive Studios", q1 ? "Quake" : "Quake II"};
    alt = qa_node_path_join(parts, 4);
  }
  if (real == NULL && alt == NULL) return false;
  char *game = qa_node_path_basename(product->content_directory);
  const struct {
    const char *name;
    const char *directory;
    bool binds;
  } q1_files[] = {{"kexengine.cfg", "", true}},
    q2_files[] = {{"kexengine.cfg", "", false}, {"system.cfg", "", false}, {"settings.cfg", game, false},
                  {"keybinds.cfg", game, true}};
  size_t wanted = q1 ? COUNT(q1_files) : COUNT(q2_files);
  memset(out, 0, sizeof *out);
  out->files = (qa_native_config_file *)checked(calloc(wanted, sizeof *out->files));
  for (size_t i = 0; i < wanted; i++) {
    const char *name = q1 ? q1_files[i].name : q2_files[i].name;
    const char *directory = q1 ? q1_files[i].directory : q2_files[i].directory;
    bool binds = q1 ? q1_files[i].binds : q2_files[i].binds;
    char *file_real = real == NULL ? NULL : qa_node_path_join3(real, directory, name);
    char *file_alt = alt == NULL ? NULL : qa_node_path_join3(alt, directory, name);
    if (placed(name, file_real, file_alt, binds, reads_redirect, &out->files[out->file_count])) out->file_count++;
  }
  free(game);
  out->format = q1 ? QA_LAYER_FORMAT_KEX_QUAKE : QA_LAYER_FORMAT_KEX_QUAKE2;
  out->script = "config.cfg";
  if (alt != NULL && real != NULL) {
    out->fallbacks = (char **)checked(malloc(sizeof *out->fallbacks));
    out->fallbacks[0] = real;
    out->fallback_count = 1;
    out->directory = alt;
  } else {
    out->directory = alt != NULL ? alt : real;
  }
  out->screenshots = screenshot_naming(product->family);
  return true;
}

/* --- Lines --- */

/* line.trim(), malloc'd. */
static char *trimmed(const char *line) {
  const char *start = line;
  for (size_t space; (space = qa_js_white_space_length(start)) > 0;) start += space;
  const char *end = start;
  for (const char *at = start; *at != '\0';) {
    size_t space = qa_js_white_space_length(at);
    if (space > 0) {
      at += space;
    } else {
      at++;
      end = at;
    }
  }
  return dup_range(start, (size_t)(end - start));
}
static size_t space_run(const char *text) {
  size_t at = 0;
  for (size_t space; (space = qa_js_white_space_length(text + at)) > 0;) at += space;
  return at;
}
static size_t non_space_run(const char *text) {
  size_t at = 0;
  while (text[at] != '\0' && qa_js_white_space_length(text + at) == 0) at++;
  return at;
}
/* ASCII case-insensitive prefix test (regex flag i over ASCII literals). */
static bool starts_with_fold(const char *text, const char *prefix) {
  for (; *prefix != '\0'; text++, prefix++) {
    char c = *text;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != *prefix) return false;
  }
  return true;
}
static bool equals_fold(const char *text, const char *word) { return starts_with_fold(text, word) && text[strlen(word)] == '\0'; }

typedef struct cvar_line {
  char *keyword;
  char *name;
  char *value;
} cvar_line;
static void cvar_line_free(cvar_line *line) {
  free(line->keyword);
  free(line->name);
  free(line->value);
  memset(line, 0, sizeof *line);
}
/* The tail of CVAR_LINE after the optional keyword: ([^\s";]+)\s+"([^"]*)"$ */
static bool cvar_tail(const char *text, cvar_line *out) {
  size_t name_length = 0;
  while (text[name_length] != '\0' && text[name_length] != '"' && text[name_length] != ';' &&
         qa_js_white_space_length(text + name_length) == 0)
    name_length++;
  if (name_length == 0) return false;
  size_t gap = space_run(text + name_length);
  if (gap == 0) return false;
  const char *open = text + name_length + gap;
  if (*open != '"') return false;
  const char *close = strchr(open + 1, '"');
  if (close == NULL || close[1] != '\0') return false;
  out->name = dup_range(text, name_length);
  out->value = dup_range(open + 1, (size_t)(close - open - 1));
  return true;
}
/* TS cvarLine: /^(?:(seta|setu|sets|set)\s+)?([^\s";]+)\s+"([^"]*)"$/i over the trimmed line. */
static bool parse_cvar_line(const char *line, cvar_line *out) {
  memset(out, 0, sizeof *out);
  char *text = trimmed(line);
  static const char *const keywords[] = {"seta", "setu", "sets", "set"};
  bool matched = false;
  for (size_t i = 0; i < COUNT(keywords) && !matched; i++) {
    size_t length = strlen(keywords[i]);
    if (!starts_with_fold(text, keywords[i])) continue;
    size_t gap = space_run(text + length);
    if (gap == 0 || !cvar_tail(text + length + gap, out)) continue;
    out->keyword = dup_range(text, length);
    matched = true;
  }
  if (!matched && cvar_tail(text, out)) {
    out->keyword = dup_text("");
    matched = true;
    static const char *const commands[] = {"bind", "unbind", "unbindall", "exec", "alias", "echo"};
    for (size_t i = 0; i < COUNT(commands) && matched; i++)
      if (equals_fold(out->name, commands[i])) matched = false;
    if (!matched) cvar_line_free(out);
  }
  free(text);
  return matched;
}

/* TS bindLine: /^bind\s+(?:"([^"]*)"|(\S+))\s+(?:"([^"]*)"|(\S+))\s*$/i over the trimmed line. On true the key and
 * command are malloc'd. */
static bool bind_command_part(const char *text, char **out_command) {
  if (text[0] == '"') {
    const char *close = strchr(text + 1, '"');
    if (close != NULL && close[1] == '\0') {
      *out_command = dup_range(text + 1, (size_t)(close - text - 1));
      return true;
    }
  }
  size_t run = non_space_run(text);
  if (run == 0 || text[run] != '\0') return false;
  *out_command = dup_range(text, run);
  return true;
}
static bool parse_bind_line(const char *line, char **out_key, char **out_command) {
  char *text = trimmed(line);
  bool matched = false;
  size_t gap = starts_with_fold(text, "bind") ? space_run(text + 4) : 0;
  if (gap > 0) {
    const char *key = text + 4 + gap;
    /* The quoted alternative first, then the bare one, as the regex backtracks. */
    if (key[0] == '"') {
      const char *close = strchr(key + 1, '"');
      size_t after = close != NULL ? space_run(close + 1) : 0;
      if (after > 0 && bind_command_part(close + 1 + after, out_command)) {
        *out_key = dup_range(key + 1, (size_t)(close - key - 1));
        matched = true;
      }
    }
    if (!matched) {
      size_t run = non_space_run(key);
      size_t after = run > 0 ? space_run(key + run) : 0;
      if (after > 0 && bind_command_part(key + run + after, out_command)) {
        *out_key = dup_range(key, run);
        matched = true;
      }
    }
  }
  free(text);
  return matched;
}
static bool is_bind_line(const char *line) {
  char *key = NULL, *command = NULL;
  if (!parse_bind_line(line, &key, &command)) return false;
  free(key);
  free(command);
  return true;
}

/* TS readFrom: the file as Latin-1 text, or NULL when it does not exist. Inside this file a TS string is held as
 * UTF-8 (the layers' JSON text is UTF-8 too); the public read and write calls convert at the console boundary, where
 * text is one byte per code unit like every other script (qa_command_text_from_utf8 / _to_utf8). A NUL byte is
 * U+0000 in the TS string, which every line rule treats as an ordinary character; here it is the two-byte C0 80 so
 * the text after it is still read (console text, a C string, ends at it: cut_at_nul). */
static char *read_from(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    if (errno == ENOENT || errno == ENOTDIR) return NULL;
    qa_node_fs_error(errno, "open", path); /* TS rethrows readFile's Node error */
  }
  size_t capacity = 4096, length = 0;
  uint8_t *bytes = (uint8_t *)checked(malloc(capacity));
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      bytes = (uint8_t *)checked(realloc(bytes, capacity));
    }
    size_t count = fread(bytes + length, 1, capacity - length, file);
    length += count;
    if (count == 0) break;
  }
  bool failed = ferror(file) != 0;
  int error = errno;
  (void)fclose(file);
  if (failed) {
    free(bytes);
    /* A folder where the file should be reads as EISDIR in Node, which the TS rethrows (Bun names no path). */
    qa_node_fs_error(error, "read", NULL);
  }
  char *text = (char *)checked(malloc(length * 2 + 1));
  size_t at = 0;
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = bytes[i];
    if (byte == 0) {
      text[at++] = (char)0xc0;
      text[at++] = (char)0x80;
    } else if (byte < 0x80) {
      text[at++] = (char)byte;
    } else {
      text[at++] = (char)(0xc0 | (byte >> 6));
      text[at++] = (char)(0x80 | (byte & 0x3f));
    }
  }
  text[at] = '\0';
  free(bytes);
  return text;
}
/* TS readNative: a redirected file is read from its redirect first, so what a run wrote is what it reads back. */
static char *read_native(const qa_native_config_file *file) {
  char *text = file->redirected == NULL || !file->reads_redirect ? NULL : read_from(file->redirected);
  return text != NULL ? text : read_from(file->path);
}

/* TS lines: text.split(/\r\n|\r|\n/) without a final empty line. */
static void split_lines(const char *text, text_list *out) {
  const char *start = text;
  for (const char *at = text;; at++) {
    if (*at != '\r' && *at != '\n' && *at != '\0') continue;
    bool last = *at == '\0';
    if (!last || at != start) list_push(out, dup_range(start, (size_t)(at - start)));
    if (last) break;
    if (at[0] == '\r' && at[1] == '\n') at++;
    start = at + 1;
  }
}

/* TS writtenKey: the original writes key names bare (`bind TAB "inven"`); anything the tokenizer would split stays quoted. */
static char *written_key(qa_layer_format format, const char *key) {
  bool bare = format != QA_LAYER_FORMAT_QUAKE && format != QA_LAYER_FORMAT_KEX_QUAKE && key[0] != '\0';
  for (const char *at = key; bare && *at != '\0'; at++) {
    char c = *at;
    bare = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("_+,./-=[]`'\\", c) != NULL;
  }
  return bare ? dup_text(key) : format_text("\"%s\"%s%s%s", key, "", "", "");
}

/* TS defaultKeyword: the keyword a variable line starts with when the layers add one the file lacks. */
static const char *default_keyword(qa_layer_format format) {
  switch (format) {
    case QA_LAYER_FORMAT_QUAKE: return "";
    case QA_LAYER_FORMAT_QUAKE2: return "set ";
    default: return "seta ";
  }
}
static bool is_kex(qa_layer_format format) { return format == QA_LAYER_FORMAT_KEX_QUAKE || format == QA_LAYER_FORMAT_KEX_QUAKE2; }

void qa_native_bindings_free(qa_native_bindings *bindings) {
  for (size_t i = 0; i < bindings->count; i++) {
    free((char *)bindings->pairs[i].key);
    free((char *)bindings->pairs[i].command);
  }
  free(bindings->pairs);
  memset(bindings, 0, sizeof *bindings);
}
void qa_native_session_free(qa_native_session *session) {
  qa_native_bindings_free(&session->bindings);
  for (size_t i = 0; i < session->cvar_count; i++) free(session->cvars[i]);
  free(session->cvars);
  memset(session, 0, sizeof *session);
}

/* --- Console input --- */

/* The console's bindings, archive commands and reset values are one byte per code unit; the layers and the files
 * hold UTF-8 (TS writeAtomic encodes the string as UTF-8). The public write calls convert here, once. */
typedef struct utf8_input {
  qa_layer_pair *pairs;
  size_t pair_count;
  char **cvars;
  size_t cvar_count;
  const qa_native_write_context *caller;
  qa_native_write_context context;
  qa_cleanup guard;
} utf8_input;
static char *utf8_reset_value(void *ctx, const char *name) {
  const qa_native_write_context *caller = (const qa_native_write_context *)ctx;
  char *value = caller->reset_value(caller->reset_value_ctx, name);
  if (value == NULL) return NULL;
  char *text = qa_command_text_to_utf8(value);
  free(value);
  return text;
}
static void utf8_input_free(void *raw) {
  utf8_input *input = (utf8_input *)raw;
  for (size_t i = 0; i < input->pair_count; i++) {
    free((char *)input->pairs[i].key);
    free((char *)input->pairs[i].command);
  }
  free(input->pairs);
  for (size_t i = 0; i < input->cvar_count; i++) free(input->cvars[i]);
  free(input->cvars);
  input->pairs = NULL;
  input->cvars = NULL;
  input->pair_count = input->cvar_count = 0;
}
/* Converts and pushes a raise-time guard; utf8_input_end pops and frees it. */
static void utf8_input_begin(utf8_input *input, const qa_layer_pair *bindings, size_t binding_count,
                             const char *const *cvars, size_t cvar_count, const qa_native_write_context *context) {
  memset(input, 0, sizeof *input);
  qa_cleanup_push(&input->guard, utf8_input_free, input);
  input->pairs = (qa_layer_pair *)checked(calloc(binding_count == 0 ? 1 : binding_count, sizeof *input->pairs));
  for (size_t i = 0; i < binding_count; i++) {
    input->pairs[i].key = qa_command_text_to_utf8(bindings[i].key);
    input->pair_count = i + 1;
    input->pairs[i].command = qa_command_text_to_utf8(bindings[i].command);
  }
  input->cvars = (char **)checked(calloc(cvar_count == 0 ? 1 : cvar_count, sizeof *input->cvars));
  for (size_t i = 0; i < cvar_count; i++) {
    input->cvars[i] = qa_command_text_to_utf8(cvars[i]);
    input->cvar_count = i + 1;
  }
  input->caller = context;
  if (context != NULL && context->reset_value != NULL) {
    input->context.reset_value = utf8_reset_value;
    input->context.reset_value_ctx = (void *)context;
  }
}
static const qa_native_write_context *utf8_input_context(const utf8_input *input) {
  return input->caller == NULL ? NULL : &input->context;
}
static void utf8_input_end(utf8_input *input) {
  qa_cleanup_pop(&input->guard);
  utf8_input_free(input);
}

/* --- absorbSession --- */

static void layer_map_release(void *map) { qa_layer_map_destroy((qa_layer_map *)map); }

static void absorb_session(qa_game_layers *layers, qa_layer_format format, const qa_layer_pair *bindings,
                           size_t binding_count, const char *const *cvars, size_t cvar_count, bool has_cvars,
                           const qa_native_write_context *context, qa_native_session *out) {
  memset(out, 0, sizeof *out);
  if (!qa_game_layers_is_ready(layers)) qa_raise("Error", 0, 0, "%s", "Settings layers were not read before they were written");
  qa_layer_reset_value_fn reset_value = context != NULL ? context->reset_value : NULL;
  void *reset_ctx = context != NULL ? context->reset_value_ctx : NULL;
  /* The state is canonical settings: a cvar in any spelling reads as its canonical name and value, and where two
   * spellings of one setting are archived, the one that differs from its reset value is the player's. */
  qa_layer_map current;
  qa_layer_map_init(&current);
  for (size_t i = 0; has_cvars && i < cvar_count; i++) {
    cvar_line parsed;
    if (!parse_cvar_line(cvars[i], &parsed)) continue;
    qa_setting_match match;
    const char *canonical = NULL;
    char *value = NULL;
    bool known = qa_setting_for(parsed.name, &match) && qa_spelling_in(format, match.setting->canonical, NULL) &&
                 qa_setting_to_canonical(parsed.name, parsed.value, &canonical, &value);
    cvar_line_free(&parsed);
    if (!known) continue;
    char *reset = reset_value != NULL ? reset_value(reset_ctx, canonical) : NULL;
    const qa_layer_entry *kept = qa_layer_map_get(&current, canonical);
    if (kept == NULL || (reset != NULL && qa_layer_same_value(kept->value, reset)))
      qa_layer_map_set(&current, canonical, canonical, value);
    free(reset);
    free(value);
  }
  qa_cleanup current_node; /* a failed layer save drops the parsed state (TS: garbage) */
  qa_cleanup_push(&current_node, layer_map_release, &current);
  const qa_effective_set *effective = qa_game_layers_absorb(layers, bindings, binding_count, &current, reset_value, reset_ctx);
  qa_cleanup_pop(&current_node);
  /* Unchanged default.cfg bindings stay in the file; bindings a layer cancels leave it. */
  qa_layer_map cancelled, written;
  qa_layer_map_init(&cancelled);
  qa_layer_map_init(&written);
  for (size_t i = 0; i < effective->unbound.count; i++) {
    char *folded = lower(effective->unbound.entries[i].name);
    qa_layer_map_set(&cancelled, folded, NULL, NULL);
    free(folded);
  }
  for (size_t i = 0; i < binding_count; i++) {
    char *folded = lower(bindings[i].key);
    if (!qa_layer_map_has(&cancelled, folded)) qa_layer_map_set(&written, folded, bindings[i].key, bindings[i].command);
    free(folded);
  }
  for (size_t i = 0; i < effective->binds.count; i++) {
    const qa_layer_entry *bind = &effective->binds.entries[i];
    if (bind->value == NULL) continue;
    char *folded = lower(bind->name);
    qa_layer_map_set(&written, folded, bind->name, bind->value);
    free(folded);
  }
  qa_layer_map_destroy(&cancelled);
  out->bindings.count = written.count;
  out->bindings.pairs = (qa_layer_pair *)checked(calloc(written.count == 0 ? 1 : written.count, sizeof *out->bindings.pairs));
  for (size_t i = 0; i < written.count; i++) {
    out->bindings.pairs[i].key = dup_text(written.entries[i].name);
    out->bindings.pairs[i].command = dup_text(written.entries[i].value);
  }
  qa_layer_map_destroy(&written);
  if (!has_cvars) {
    qa_layer_map_destroy(&current);
    return;
  }
  /* The game's file spells each setting its own way: the archived canonical lines give way to the effective set in
   * that spelling. */
  qa_layer_map values, wanted, present;
  qa_layer_map_init(&values);
  qa_layer_map_init(&wanted);
  qa_layer_map_init(&present);
  for (size_t i = 0; i < current.count; i++) qa_layer_map_set(&values, current.entries[i].id, NULL, current.entries[i].value);
  for (size_t i = 0; i < effective->cvars.count; i++)
    qa_layer_map_set(&values, effective->cvars.entries[i].id, NULL, effective->cvars.entries[i].value);
  qa_layer_map_destroy(&current);
  qa_file_cvars(format, &values, &wanted);
  qa_layer_map_destroy(&values);
  text_list lines = {NULL, 0, 0};
  for (size_t i = 0; i < cvar_count; i++) {
    cvar_line parsed;
    bool is_line = parse_cvar_line(cvars[i], &parsed);
    qa_setting_match match;
    bool mapped = is_line && qa_setting_for(parsed.name, &match) && qa_spelling_in(format, match.setting->canonical, NULL);
    if (!mapped) {
      list_push(&lines, dup_text(cvars[i]));
      if (is_line) {
        char *folded = lower(parsed.name);
        qa_layer_map_set(&present, folded, NULL, NULL);
        free(folded);
      }
    }
    if (is_line) cvar_line_free(&parsed);
  }
  for (size_t i = 0; i < wanted.count; i++) {
    const qa_layer_entry *cvar = &wanted.entries[i];
    if (qa_layer_map_has(&present, cvar->id)) continue;
    list_push(&lines, format_text("%s%s \"%s\"%s", default_keyword(format), cvar->name, cvar->value, ""));
  }
  qa_layer_map_destroy(&wanted);
  qa_layer_map_destroy(&present);
  out->cvars = lines.items;
  out->cvar_count = lines.count;
  out->has_cvars = true;
}

void qa_absorb_session(qa_game_layers *layers, qa_layer_format format, const qa_layer_pair *bindings,
                       size_t binding_count, const char *const *cvars, size_t cvar_count, bool has_cvars,
                       const qa_native_write_context *context, qa_native_session *out) {
  utf8_input input;
  utf8_input_begin(&input, bindings, binding_count, cvars, has_cvars ? cvar_count : 0, context);
  absorb_session(layers, format, input.pairs, input.pair_count, (const char *const *)input.cvars, input.cvar_count,
                 has_cvars, utf8_input_context(&input), out);
  utf8_input_end(&input);
}

/* --- NativeConfig --- */

void qa_native_config_init(qa_native_config *config, const qa_native_config_layout *layout, qa_game_layers *layers) {
  memset(config, 0, sizeof *config);
  config->layout = layout;
  config->layers = layers;
  qa_layer_map_init(&config->names);
}
void qa_native_config_destroy(qa_native_config *config) {
  qa_layer_map_destroy(&config->names);
  memset(config, 0, sizeof *config);
}
bool qa_native_config_loaded(const qa_native_config *config) { return config->loaded_files > 0; }
bool qa_native_config_has_bindings(const qa_native_config *config) { return config->bound; }

/* TS translateBind: takes ownership of key and command; false drops the binding. */
static bool translate_bind(const qa_native_config *config, char **key, char **command) {
  if (config->layout->format != QA_LAYER_FORMAT_KEX_QUAKE) return true;
  if ((*command)[0] == '*') {
    const char *translated = kex_command_to_ours(*command);
    if (translated == NULL) {
      free(*key);
      free(*command);
      *key = *command = NULL;
      return false;
    }
    free(*command);
    *command = dup_text(translated);
  }
  char *ours = kex_key_to_ours(*key);
  free(*key);
  *key = ours;
  return true;
}

static void layer_snapshot_release(void *snapshot) { qa_layer_snapshot_destroy((qa_layer_snapshot *)snapshot); }
void qa_native_config_snapshot(const qa_native_config *config, qa_layer_snapshot *out) {
  qa_layer_snapshot_init(out);
  qa_cleanup out_node; /* a file that cannot be read raises with what was read released (destroy zeroes it) */
  qa_cleanup_push(&out_node, layer_snapshot_release, out);
  for (size_t f = 0; f < config->layout->file_count; f++) {
    char *text = read_native(&config->layout->files[f]);
    if (text == NULL) continue;
    text_list lines = {NULL, 0, 0};
    split_lines(text, &lines);
    free(text);
    for (size_t i = 0; i < lines.count; i++) {
      char *key = NULL, *command = NULL;
      if (parse_bind_line(lines.items[i], &key, &command)) {
        if (translate_bind(config, &key, &command)) {
          char *folded = lower(key);
          qa_layer_map_set(&out->binds, folded, key, command);
          free(folded);
          free(key);
          free(command);
        }
        continue;
      }
      cvar_line cvar;
      if (!parse_cvar_line(lines.items[i], &cvar)) continue;
      const char *canonical = NULL;
      char *value = NULL;
      if (qa_setting_to_canonical(cvar.name, cvar.value, &canonical, &value)) {
        if (qa_spelling_in(config->layout->format, canonical, NULL)) qa_layer_map_set(&out->cvars, canonical, canonical, value);
        free(value);
      }
      cvar_line_free(&cvar);
    }
    list_free(&lines);
  }
  qa_cleanup_pop(&out_node);
}

typedef struct script_variable {
  size_t index;
  cvar_line cvar;
} script_variable;

/* TS layered: the script lines with the file's bindings and mapped variables replaced by the effective set, and the
 * rest of it appended. */
static void layered(const qa_native_config *config, const text_list *out, size_t bind_at, bool has_bind_at,
                    const script_variable *variables, size_t variable_count, const qa_effective_set *effective,
                    text_list *merged) {
  qa_layer_format format = config->layout->format;
  qa_layer_map values, wanted, emitted;
  qa_layer_map_init(&values);
  qa_layer_map_init(&wanted);
  qa_layer_map_init(&emitted);
  for (size_t i = 0; i < effective->cvars.count; i++)
    qa_layer_map_set(&values, effective->cvars.entries[i].id, NULL, effective->cvars.entries[i].value);
  qa_file_cvars(format, &values, &wanted);
  qa_layer_map_destroy(&values);
  char **replaced = (char **)checked(calloc(out->count == 0 ? 1 : out->count, sizeof *replaced));
  for (size_t i = 0; i < variable_count; i++) {
    const cvar_line *cvar = &variables[i].cvar;
    char *folded = lower(cvar->name);
    const qa_layer_entry *replacement = qa_layer_map_get(&wanted, folded);
    if (replacement != NULL) {
      qa_layer_map_set(&emitted, folded, NULL, NULL);
      free(replaced[variables[i].index]);
      replaced[variables[i].index] =
          format_text("%s%s%s \"%s\"", cvar->keyword, cvar->keyword[0] == '\0' ? "" : " ", cvar->name, replacement->value);
    }
    free(folded);
  }
  text_list binds = {NULL, 0, 0};
  for (size_t i = 0; i < effective->unbound.count; i++)
    list_push(&binds, format_text("unbind \"%s\"%s%s%s", effective->unbound.entries[i].name, "", "", ""));
  for (size_t i = 0; i < effective->binds.count; i++) {
    const qa_layer_entry *bind = &effective->binds.entries[i];
    list_push(&binds, format_text("bind \"%s\" \"%s\"%s%s", bind->name, bind->value != NULL ? bind->value : "", "", ""));
  }
  if (!has_bind_at) list_push_copies(merged, &binds);
  for (size_t index = 0; index < out->count; index++) {
    if (has_bind_at && index == bind_at) list_push_copies(merged, &binds);
    if (is_bind_line(out->items[index])) continue;
    list_push(merged, dup_text(replaced[index] != NULL ? replaced[index] : out->items[index]));
  }
  for (size_t i = 0; i < wanted.count; i++) {
    const qa_layer_entry *cvar = &wanted.entries[i];
    if (qa_layer_map_has(&emitted, cvar->id)) continue;
    list_push(merged, format_text("%s%s \"%s\"%s", default_keyword(format), cvar->name, cvar->value, ""));
  }
  for (size_t i = 0; i < out->count; i++) free(replaced[i]);
  free(replaced);
  list_free(&binds);
  qa_layer_map_destroy(&wanted);
  qa_layer_map_destroy(&emitted);
}

/* What readScript holds while it reads the files and the layers; a raise (an unreadable file, a failed layer save)
 * releases it. */
typedef struct script_scratch {
  text_list out, merged;
  script_variable *variables;
  size_t variable_count;
  qa_layer_snapshot snapshot;
} script_scratch;
static void script_scratch_release(void *arg) {
  script_scratch *scratch = (script_scratch *)arg;
  for (size_t i = 0; i < scratch->variable_count; i++) cvar_line_free(&scratch->variables[i].cvar);
  free(scratch->variables);
  list_free(&scratch->out);
  list_free(&scratch->merged);
  qa_layer_snapshot_destroy(&scratch->snapshot);
  memset(scratch, 0, sizeof *scratch);
}
/* TS readScript, as UTF-8 text. */
static char *read_script_text(qa_native_config *config) {
  config->loaded_files = 0;
  qa_layer_map_clear(&config->names);
  config->bound = false;
  script_scratch scratch;
  memset(&scratch, 0, sizeof scratch);
  qa_cleanup scratch_node;
  qa_cleanup_push(&scratch_node, script_scratch_release, &scratch);
  /* Positions the layers rewrite: where the first binding stood, and each variable's keyword. */
  size_t bind_at = 0;
  bool has_bind_at = false;
  size_t variable_capacity = 0;
  for (size_t f = 0; f < config->layout->file_count; f++) {
    char *text = read_native(&config->layout->files[f]);
    if (text == NULL) continue;
    text_list lines = {NULL, 0, 0};
    split_lines(text, &lines);
    free(text);
    bool useful = false;
    for (size_t i = 0; i < lines.count; i++) {
      char *key = NULL, *command = NULL;
      if (parse_bind_line(lines.items[i], &key, &command)) {
        if (!translate_bind(config, &key, &command)) continue;
        useful = true;
        config->bound = true;
        if (!has_bind_at) {
          has_bind_at = true;
          bind_at = scratch.out.count;
        }
        list_push(&scratch.out, format_text("bind \"%s\" \"%s\"%s%s", key, command, "", ""));
        free(key);
        free(command);
        continue;
      }
      cvar_line cvar;
      if (!parse_cvar_line(lines.items[i], &cvar)) continue;
      useful = true;
      char *folded = lower(cvar.name);
      qa_layer_map_set(&config->names, folded, NULL, NULL);
      free(folded);
      if (scratch.variable_count == variable_capacity) {
        variable_capacity = variable_capacity == 0 ? 16 : variable_capacity * 2;
        scratch.variables = (script_variable *)checked(realloc(scratch.variables, variable_capacity * sizeof *scratch.variables));
      }
      scratch.variables[scratch.variable_count].index = scratch.out.count;
      scratch.variables[scratch.variable_count].cvar = cvar;
      scratch.variable_count++;
      list_push(&scratch.out, trimmed(lines.items[i]));
    }
    list_free(&lines);
    if (useful) config->loaded_files++;
  }
  char *script = NULL;
  if (config->layers == NULL) {
    if (config->loaded_files > 0) script = list_join_terminated(&scratch.out, "\n");
  } else {
    qa_native_config_snapshot(config, &scratch.snapshot);
    const qa_effective_set *effective = qa_game_layers_resolve(config->layers, &scratch.snapshot);
    qa_layer_snapshot_destroy(&scratch.snapshot);
    layered(config, &scratch.out, bind_at, has_bind_at, scratch.variables, scratch.variable_count, effective, &scratch.merged);
    if (effective->binds.count > 0) config->bound = true;
    if (scratch.merged.count > 0) script = list_join_terminated(&scratch.merged, "\n");
  }
  qa_cleanup_pop(&scratch_node);
  script_scratch_release(&scratch);
  return script;
}

/* The script as console text: one byte per code unit. A character above U+00FF (only a layers value from JSON can
 * hold one) is refused with the RangeError TS raises when it executes that script (sourceCommandText). */
/* Console text is a C string: a U+0000 read from a file (C0 80 here) ends it. */
static void cut_at_nul(char *text) {
  char *nul = strstr(text, "\xc0\x80");
  if (nul != NULL) *nul = '\0';
}
char *qa_native_config_read_script(qa_native_config *config) {
  char *text = read_script_text(config);
  if (text == NULL) return NULL;
  cut_at_nul(text);
  char *bytes = NULL;
  bool ok = qa_command_text_from_utf8(text, &bytes);
  free(text);
  if (!ok) qa_raise("RangeError", 0, 0, "%s", "Command text requires source bytes");
  return bytes;
}

/* Console text for a binding's key or command; a character above U+00FF stays UTF-8 (TS keeps the string and
 * refuses it only when the binding runs). */
static char *console_text(char *utf8) {
  cut_at_nul(utf8);
  char *bytes = NULL;
  if (!qa_command_text_from_utf8(utf8, &bytes)) return utf8;
  free(utf8);
  return bytes;
}

bool qa_native_config_read_bindings(qa_native_config *config, qa_native_bindings *out) {
  memset(out, 0, sizeof *out);
  char *script = read_script_text(config);
  if (script == NULL || !config->bound) {
    free(script);
    return false;
  }
  text_list lines = {NULL, 0, 0};
  split_lines(script, &lines);
  free(script);
  out->pairs = (qa_layer_pair *)checked(calloc(lines.count == 0 ? 1 : lines.count, sizeof *out->pairs));
  for (size_t i = 0; i < lines.count; i++) {
    char *key = NULL, *command = NULL;
    if (!parse_bind_line(lines.items[i], &key, &command)) continue;
    out->pairs[out->count].key = console_text(key);
    out->pairs[out->count].command = console_text(command);
    out->count++;
  }
  list_free(&lines);
  return true;
}

bool qa_native_config_keeps_archive_entry(const qa_native_config *config, const char *name) {
  if (!qa_native_config_loaded(config)) return true;
  if (!is_kex(config->layout->format)) return false;
  /* The archive entry's name is console text (cvar_archives.c); the names read from the files are UTF-8. */
  char *text = qa_command_text_to_utf8(name);
  char *folded = lower(text);
  free(text);
  bool answered = qa_layer_map_has(&config->names, folded);
  free(folded);
  return !answered;
}

bool qa_native_config_suppresses_profile_bindings(const qa_native_config *config) {
  return is_kex(config->layout->format) ? config->bound : qa_native_config_loaded(config);
}

/* TS writtenBind, malloc'd. */
static char *written_bind(const qa_native_config *config, const char *key, const char *command) {
  if (config->layout->format == QA_LAYER_FORMAT_KEX_QUAKE) {
    const char *star = kex_command_from_ours(command);
    char *name = kex_key(key);
    char *line = format_text("bind %s \"%s\"%s%s", name, star != NULL ? star : command, "", "");
    free(name);
    return line;
  }
  char *name = written_key(config->layout->format, key);
  char *line = format_text("bind %s \"%s\"%s%s", name, command, "", "");
  free(name);
  return line;
}
static void written_binds(const qa_native_config *config, const qa_layer_pair *bindings, size_t count, text_list *out) {
  for (size_t i = 0; i < count; i++) list_push(out, written_bind(config, bindings[i].key, bindings[i].command));
}
static void write_text(const char *path, char *text) {
  bool ok = qa_write_atomic_text(path, text);
  free(text);
  if (!ok) qa_settings_raise_write_failure(); /* TS: writeAtomic rejects with the failing step's Node fs error */
}
static const char *write_path(const qa_native_config_file *file) { return file->redirected != NULL ? file->redirected : file->path; }

/* TS merge: the rerelease files hold many variables this engine does not own: update the lines present, keep the
 * rest, and leave variables they do not name in the user store. `cvars` NULL (has_cvars false) is the TS null. */
static void merge(const qa_native_config *config, const qa_layer_pair *bindings, size_t binding_count,
                  const char *const *cvars, size_t cvar_count, bool has_cvars) {
  qa_layer_format format = config->layout->format;
  /* wanted: lowercase name -> parsed line (name, value); `missing` starts as a copy. */
  qa_layer_map wanted, missing;
  qa_layer_map_init(&wanted);
  qa_layer_map_init(&missing);
  for (size_t i = 0; has_cvars && i < cvar_count; i++) {
    cvar_line parsed;
    if (!parse_cvar_line(cvars[i], &parsed)) continue;
    char *folded = lower(parsed.name);
    qa_layer_map_set(&wanted, folded, parsed.name, parsed.value);
    free(folded);
    cvar_line_free(&parsed);
  }
  qa_layer_map_copy(&missing, &wanted);
  qa_cleanup wanted_node, missing_node; /* an unreadable or unwritable file drops them */
  qa_cleanup_push(&wanted_node, layer_map_release, &wanted);
  qa_cleanup_push(&missing_node, layer_map_release, &missing);
  for (size_t index = 0; index < config->layout->file_count; index++) {
    const qa_native_config_file *file = &config->layout->files[index];
    char *existing = read_native(file);
    if ((existing == NULL && !file->binds && index != 0) || (!has_cvars && !file->binds)) {
      free(existing);
      continue;
    }
    bool crlf = existing != NULL && strstr(existing, "\r\n") != NULL;
    text_list output = {NULL, 0, 0}, bind_lines = {NULL, 0, 0}, lines = {NULL, 0, 0};
    if (file->binds) written_binds(config, bindings, binding_count, &bind_lines);
    bool bound_here = false;
    if (existing != NULL) split_lines(existing, &lines);
    for (size_t i = 0; i < lines.count; i++) {
      const char *line = lines.items[i];
      char *key = NULL, *command = NULL;
      if (file->binds && parse_bind_line(line, &key, &command)) {
        /* Engine-owned actions keep their lines; the player's own bindings are replaced as a block. */
        if (format == QA_LAYER_FORMAT_KEX_QUAKE && command[0] == '*' && kex_command_to_ours(command) == NULL) {
          list_push(&output, dup_text(line));
        } else if (!bound_here) {
          bound_here = true;
          list_push_copies(&output, &bind_lines);
        }
        free(key);
        free(command);
        continue;
      }
      cvar_line cvar;
      bool is_cvar = parse_cvar_line(line, &cvar);
      char *folded = is_cvar ? lower(cvar.name) : NULL;
      const qa_layer_entry *next = is_cvar ? qa_layer_map_get(&wanted, folded) : NULL;
      if (next != NULL) {
        list_push(&output, format_text("%s%s%s \"%s\"", cvar.keyword, cvar.keyword[0] == '\0' ? "" : " ", cvar.name, next->value));
        (void)qa_layer_map_delete(&missing, folded);
      } else {
        list_push(&output, dup_text(line));
      }
      free(folded);
      if (is_cvar) cvar_line_free(&cvar);
    }
    list_free(&lines);
    if (file->binds && !bound_here) list_insert_copies(&output, 0, &bind_lines);
    list_free(&bind_lines);
    /* The layered set is complete: variables the file never named go into the first file. */
    if (index == 0 && config->layers != NULL)
      for (size_t i = 0; i < missing.count; i++)
        if (qa_setting_for(missing.entries[i].name, NULL))
          list_push(&output, format_text("%s%s \"%s\"%s", default_keyword(format), missing.entries[i].name, missing.entries[i].value, ""));
    if (existing == NULL && output.count == 0) {
      list_free(&output);
      continue;
    }
    free(existing);
    char *text = list_join_terminated(&output, crlf ? "\r\n" : "\n");
    list_free(&output);
    write_text(write_path(file), text);
  }
  qa_cleanup_pop(&missing_node);
  qa_cleanup_pop(&wanted_node);
  qa_layer_map_destroy(&wanted);
  qa_layer_map_destroy(&missing);
}

/* TS writeFiles. */
static void write_files(const qa_native_config *config, const qa_layer_pair *bindings, size_t binding_count,
                        const char *const *cvars, size_t cvar_count) {
  qa_layer_format format = config->layout->format;
  if (is_kex(format)) {
    merge(config, bindings, binding_count, cvars, cvar_count, true);
    return;
  }
  text_list lines = {NULL, 0, 0};
  if (format != QA_LAYER_FORMAT_QUAKE) list_push(&lines, dup_text("// generated by quake, do not modify"));
  written_binds(config, bindings, binding_count, &lines);
  for (size_t i = 0; i < cvar_count; i++) list_push(&lines, dup_text(cvars[i]));
  if (config->layout->file_count == 0) {
    list_free(&lines);
    return;
  }
  char *text = list_join_terminated(&lines, "\n");
  list_free(&lines);
  write_text(write_path(&config->layout->files[0]), text);
}

static void record_written(qa_native_config *config) {
  if (config->layers == NULL) return;
  qa_layer_snapshot snapshot;
  qa_native_config_snapshot(config, &snapshot);
  qa_cleanup snapshot_node; /* a failed layer save drops it */
  qa_cleanup_push(&snapshot_node, layer_snapshot_release, &snapshot);
  qa_game_layers_record_written(config->layers, &snapshot);
  qa_cleanup_pop(&snapshot_node);
  qa_layer_snapshot_destroy(&snapshot);
}
/* TS layeredState, when layers are present: reads the layers first when this session has not. */
static void layered_state(qa_native_config *config, const qa_layer_pair *bindings, size_t binding_count,
                          const char *const *cvars, size_t cvar_count, bool has_cvars,
                          const qa_native_write_context *context, qa_native_session *out) {
  if (!qa_game_layers_is_ready(config->layers)) free(read_script_text(config));
  absorb_session(config->layers, config->layout->format, bindings, binding_count, cvars, cvar_count, has_cvars, context, out);
}

static void native_session_release(void *raw) { qa_native_session_free((qa_native_session *)raw); }
void qa_native_config_write(qa_native_config *config, const qa_layer_pair *bindings, size_t binding_count,
                            const char *const *cvars, size_t cvar_count, const qa_native_write_context *context) {
  utf8_input input;
  utf8_input_begin(&input, bindings, binding_count, cvars, cvar_count, context);
  if (config->layers == NULL) {
    write_files(config, input.pairs, input.pair_count, (const char *const *)input.cvars, input.cvar_count);
    utf8_input_end(&input);
    return;
  }
  qa_native_session state;
  memset(&state, 0, sizeof state);
  qa_cleanup state_guard;
  qa_cleanup_push(&state_guard, native_session_release, &state);
  layered_state(config, input.pairs, input.pair_count, (const char *const *)input.cvars, input.cvar_count, true,
                utf8_input_context(&input), &state);
  write_files(config, state.bindings.pairs, state.bindings.count, (const char *const *)state.cvars, state.cvar_count);
  qa_cleanup_pop(&state_guard);
  qa_native_session_free(&state);
  utf8_input_end(&input);
  record_written(config);
}

void qa_native_config_write_bindings(qa_native_config *config, const qa_layer_pair *console_bindings,
                                     size_t console_binding_count, const qa_native_write_context *console_context,
                                     bool local) {
  utf8_input input;
  utf8_input_begin(&input, console_bindings, console_binding_count, NULL, 0, console_context);
  const qa_layer_pair *bindings = input.pairs;
  size_t binding_count = input.pair_count;
  const qa_native_write_context *context = utf8_input_context(&input);
  qa_native_session state;
  memset(&state, 0, sizeof state);
  qa_cleanup state_guard;
  qa_cleanup_push(&state_guard, native_session_release, &state);
  qa_game_layers *layers = config->layers;
  if (layers != NULL) {
    if (!qa_game_layers_is_ready(layers)) free(read_script_text(config));
    if (local) {
      qa_layer_map before, after, keys;
      qa_layer_map_init(&before);
      qa_layer_map_init(&after);
      qa_layer_map_init(&keys);
      const qa_effective_set *loaded = qa_game_layers_loaded(layers);
      for (size_t i = 0; i < loaded->binds.count; i++) {
        char *folded = lower(loaded->binds.entries[i].name);
        qa_layer_map_set(&before, folded, NULL, loaded->binds.entries[i].value);
        free(folded);
      }
      for (size_t i = 0; i < binding_count; i++) {
        char *folded = lower(bindings[i].key);
        qa_layer_map_set(&after, folded, NULL, bindings[i].command);
        free(folded);
      }
      for (size_t i = 0; i < before.count; i++) qa_layer_map_set(&keys, before.entries[i].id, NULL, NULL);
      for (size_t i = 0; i < after.count; i++) qa_layer_map_set(&keys, after.entries[i].id, NULL, NULL);
      for (size_t i = 0; i < keys.count; i++) {
        const qa_layer_entry *was = qa_layer_map_get(&before, keys.entries[i].id);
        const qa_layer_entry *now = qa_layer_map_get(&after, keys.entries[i].id);
        /* before.get(key) !== after.get(key): an absent key is undefined, a cancelled one null. */
        bool same = (was == NULL) == (now == NULL) &&
                    (was == NULL || ((was->value == NULL) == (now->value == NULL) &&
                                     (was->value == NULL || strcmp(was->value, now->value) == 0)));
        if (!same) qa_game_layers_note_local_bind(layers, keys.entries[i].id);
      }
      qa_layer_map_destroy(&before);
      qa_layer_map_destroy(&after);
      qa_layer_map_destroy(&keys);
    }
    layered_state(config, bindings, binding_count, NULL, 0, false, context, &state);
    bindings = state.bindings.pairs;
    binding_count = state.bindings.count;
  }
  qa_layer_format format = config->layout->format;
  if (is_kex(format)) {
    merge(config, bindings, binding_count, NULL, 0, false);
  } else if (config->layout->file_count == 0) {
    qa_cleanup_pop(&state_guard);
    qa_native_session_free(&state);
    utf8_input_end(&input);
    return;
  } else {
    const qa_native_config_file *file = &config->layout->files[0];
    char *existing = read_native(file);
    text_list lines = {NULL, 0, 0}, output = {NULL, 0, 0};
    split_lines(existing != NULL ? existing : "", &lines);
    free(existing);
    if (format != QA_LAYER_FORMAT_QUAKE) list_push(&output, dup_text("// generated by quake, do not modify"));
    written_binds(config, bindings, binding_count, &output);
    for (size_t i = 0; i < lines.count; i++) {
      const char *line = lines.items[i];
      if (is_bind_line(line)) continue;
      /* /^\s*unbindall\s*$/i and /^\s*\/\/ generated by quake/i */
      const char *body = line + space_run(line);
      if (starts_with_fold(body, "unbindall") && body[9 + space_run(body + 9)] == '\0') continue;
      if (starts_with_fold(body, "// generated by quake")) continue;
      list_push(&output, dup_text(line));
    }
    list_free(&lines);
    char *text = list_join_terminated(&output, "\n");
    list_free(&output);
    write_text(write_path(file), text);
  }
  qa_cleanup_pop(&state_guard);
  qa_native_session_free(&state);
  utf8_input_end(&input);
  record_written(config);
}
