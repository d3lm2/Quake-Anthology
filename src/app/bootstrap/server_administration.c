/* Port of src/app/bootstrap/server-administration.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_administration.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/network/q2/handshake.h"
#include "qa/persistence/value.h"
#include "qa/network/common/js_number.h"

static bool cvar_exists_(qa_cvar_registry *r, const char *name) {
  qa_cvar_read out; memset(&out, 0, sizeof out);
  bool found = qa_cvar_registry_find(r, name, &out);
  if (found) qa_cvar_read_free(&out);
  return found;
}
static void cvar_register_(qa_cvar_registry *r, const char *name, const char *def, double flags) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_register(r, name, def, flags, &out)) qa_cvar_read_free(&out);
}

struct qa_app_server_operator_state {
  qa_config_store store; char *scope; /* TS holds its own ConfigStore */
  qa_ip_filter_list filters;
  qa_net_address *masters_qw; size_t masters_qw_count;
  qa_net_address *masters_q2; size_t masters_q2_count;
  qa_net_address *masters_q3; size_t masters_q3_count;
  char *master_names;
  double generation;
  bool closed;
  /* masterFailure: the error a detached Q3 resolution raised, rethrown by the next refresh. */
  bool has_master_failure; qa_caught_error master_failure;
  /* The Q3 resolution still waiting for a lookup: names[next..] left, addresses so far. */
  bool q3_pending;
  char **q3_pending_names; size_t q3_pending_count, q3_pending_next;
  qa_net_address *q3_pending_addresses; size_t q3_pending_address_count;
  char **limited_prefixes; size_t limited_count, limited_cap;
  qa_cvar_registry *rcon_owner; double rcon_revision; char *rcon_text;
  uint32_t rcon_time, rcon_credit, rcon_cap, rcon_cost;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static bool is_q2(qa_command_dialect d) { return d == QA_COMMAND_DIALECT_Q2_CLASSIC || d == QA_COMMAND_DIALECT_Q2_RERELEASE; }
static void filter_text(qa_ipv4_filter f, char *buf, size_t size) {
  snprintf(buf, size, "%u.%u.%u.%u", f.compare & 255, (f.compare >> 8) & 255, (f.compare >> 16) & 255, (f.compare >> 24) & 255);
}
/* Prints a formatted text of any length (TS template strings have no cap). */
static void print_format(void (*print)(void *ctx, const char *text), void *print_ctx, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) { va_end(args); qa_range_error("Out of memory"); }
  char *text = malloc((size_t)length + 1);
  if (!text) { va_end(args); qa_range_error("Out of memory"); }
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, text);
  print(print_ctx, text);
  qa_cleanup_pop(&guard);
  free(text);
}
static void append_str(char **buf, const char *add) {
  size_t a = strlen(*buf), b = strlen(add);
  char *n = realloc(*buf, a + b + 1);
  if (!n) qa_range_error("Out of memory");
  memcpy(n + a, add, b + 1); *buf = n;
}
static void destroy_state_slot(void *arg) { qa_app_server_operator_state_destroy(arg); }
static void free_value_slot(void *arg) { qa_save_value_free(arg); }
qa_app_server_operator_state *qa_app_server_operator_state_open(const qa_config_store *store, const char *scope) {
  qa_app_server_operator_state *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  qa_ip_filter_list_init(&s->filters, true);
  s->rcon_revision = -1;
  qa_cleanup state_guard;
  qa_cleanup_push(&state_guard, destroy_state_slot, s);
  if (!qa_config_store_init_legacy(&s->store, store->root, store->legacy)) qa_range_error("Out of memory");
  s->scope = xdup(scope);
  s->master_names = xdup("");
  size_t len = 0;
  char *text = qa_config_store_load_text(&s->store, scope, &len);
  if (text == NULL) { qa_cleanup_pop(&state_guard); return s; }
  qa_cleanup text_guard;
  qa_cleanup_push(&text_guard, free, text);
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, len, false);
  qa_cleanup_pop(&text_guard);
  free(text);
  if (value == NULL) qa_range_error("Out of memory");
  qa_cleanup value_guard;
  qa_cleanup_push(&value_guard, free_value_slot, value);
  const qa_save_value *version = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "version") : NULL;
  const qa_save_value *filters = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "filters") : NULL;
  if (value->kind != QA_SAVE_OBJECT || version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1
      || filters == NULL || filters->kind != QA_SAVE_ARRAY || filters->item_count > 1024)
    qa_raise("Error", 0, 0, "Invalid server filter profile");
  for (size_t i = 0; i < filters->item_count; i++) {
    qa_ipv4_filter filter;
    if (filters->items[i]->kind != QA_SAVE_STRING || !qa_source_ipv4_filter(filters->items[i]->text, &filter))
      qa_raise("Error", 0, 0, "Invalid saved server filter");
    qa_ip_filter_list_add(&s->filters, filter);
  }
  qa_cleanup_pop(&value_guard);
  qa_save_value_free(value);
  qa_cleanup_pop(&state_guard);
  return s;
}
static void free_masters(qa_net_address *a, size_t n) { for (size_t i = 0; i < n; i++) qa_net_address_destroy(&a[i]); free(a); }
void qa_app_server_operator_state_destroy(qa_app_server_operator_state *s) {
  if (!s) return;
  qa_config_store_destroy(&s->store);
  qa_ip_filter_list_destroy(&s->filters);
  free_masters(s->masters_qw, s->masters_qw_count);
  free_masters(s->masters_q2, s->masters_q2_count);
  free_masters(s->masters_q3, s->masters_q3_count);
  for (size_t i = 0; i < s->q3_pending_count; i++) free(s->q3_pending_names[i]);
  free(s->q3_pending_names);
  free_masters(s->q3_pending_addresses, s->q3_pending_address_count);
  for (size_t i = 0; i < s->limited_count; i++) free(s->limited_prefixes[i]);
  free(s->limited_prefixes); free(s->master_names); free(s->rcon_text); free(s->scope); free(s);
}
/* A malloc'd text a raise must release: the slot is the text pointer itself, which
 * append_str moves. */
static void free_text_slot(void *arg) { char **slot = arg; free(*slot); *slot = NULL; }
static void save_filters(qa_app_server_operator_state *s) {
  size_t n = 0; qa_ipv4_filter *snap = qa_ip_filter_list_snapshot(&s->filters, &n);
  qa_cleanup snap_guard;
  qa_cleanup_push(&snap_guard, free, snap);
  char *text = xdup("{\"version\":1,\"filters\":[");
  qa_cleanup text_guard;
  qa_cleanup_push(&text_guard, free_text_slot, &text);
  for (size_t i = 0; i < n; i++) {
    char buf[64]; filter_text(snap[i], buf, sizeof buf);
    if (i) append_str(&text, ",");
    append_str(&text, "\""); append_str(&text, buf); append_str(&text, "\"");
  }
  append_str(&text, "]}\n");
  /* this.store.dump(...) rejects when the write fails (writeAtomic); the C store
   * reports it as false. */
  errno = 0;
  if (!qa_config_store_dump(&s->store, s->scope, text))
    qa_raise("Error", errno, 0, "%s: could not write %s", errno != 0 ? strerror(errno) : "write failed", s->scope);
  qa_cleanup_pop(&text_guard);
  free(text);
  qa_cleanup_pop(&snap_guard);
  free(snap);
}
qa_app_limited_rcon qa_app_server_operator_limited_rcon(qa_app_server_operator_state *s, qa_cvar_registry *cvars) {
  qa_app_limited_rcon r; memset(&r, 0, sizeof r);
  r.password = qa_cvar_registry_variable_string(cvars, "lrcon_password");
  if (!r.password) qa_range_error("Out of memory");
  r.prefixes = calloc(s->limited_count ? s->limited_count : 1, sizeof *r.prefixes);
  if (!r.prefixes) qa_range_error("Out of memory");
  for (size_t i = 0; i < s->limited_count; i++) r.prefixes[i] = xdup(s->limited_prefixes[i]);
  r.prefix_count = s->limited_count;
  return r;
}
void qa_app_limited_rcon_free(qa_app_limited_rcon *r) {
  free(r->password);
  for (size_t i = 0; i < r->prefix_count; i++) free(r->prefixes[i]);
  free(r->prefixes); memset(r, 0, sizeof *r);
}
bool qa_app_server_operator_limited_rcon_command(qa_app_server_operator_state *s, const char *name, const char *raw_args,
                                                 void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (strcmp(name, "addlrconcmd") != 0 && strcmp(name, "dellrconcmd") != 0 && strcmp(name, "listlrconcmds") != 0) return false;
  char buf[8192];
  if (strcmp(name, "listlrconcmds") == 0) {
    if (s->limited_count == 0) print(print_ctx, "No lrconcmds registered.\n");
    else {
      char *text = xdup("id command\n-- -------\n");
      for (size_t i = 0; i < s->limited_count; i++) {
        /* String(index+1).padEnd(2) */
        snprintf(buf, sizeof buf, "%zu", i + 1);
        append_str(&text, buf);
        if (strlen(buf) < 2) append_str(&text, " ");
        append_str(&text, " "); append_str(&text, s->limited_prefixes[i]); append_str(&text, "\n");
      }
      print(print_ctx, text); free(text);
    }
    return true;
  }
  if (raw_args[0] == '\0') {
    snprintf(buf, sizeof buf, "Usage: %s %s\n", name, strcmp(name, "addlrconcmd") == 0 ? "<command>" : "<id|cmd|all>");
    print(print_ctx, buf);
    return true;
  }
  size_t raw_len = strlen(raw_args);
  char *value = (raw_len >= 2 && raw_args[0] == '"' && raw_args[raw_len - 1] == '"')
                    ? qa_input_dup_range(raw_args + 1, raw_len - 2) : qa_input_dup(raw_args);
  if (!value) qa_range_error("Out of memory");
  if (strcmp(name, "addlrconcmd") == 0) {
    bool exists = false;
    for (size_t i = 0; i < s->limited_count; i++) if (strcmp(s->limited_prefixes[i], value) == 0) exists = true;
    if (exists) {
      qa_cleanup value_guard;
      qa_cleanup_push(&value_guard, free, value);
      print_format(print, print_ctx, "Lrconcmd already exists: %s\n", value);
      qa_cleanup_pop(&value_guard);
      free(value);
    }
    else {
      if (s->limited_count == s->limited_cap) { s->limited_cap = s->limited_cap ? s->limited_cap * 2 : 8; s->limited_prefixes = realloc(s->limited_prefixes, s->limited_cap * sizeof *s->limited_prefixes); if (!s->limited_prefixes) qa_range_error("Out of memory"); }
      s->limited_prefixes[s->limited_count++] = value;
    }
    return true;
  }
  if (s->limited_count == 0) { print(print_ctx, "No lrconcmds registered.\n"); free(value); return true; }
  if (strcmp(value, "all") == 0) {
    for (size_t i = 0; i < s->limited_count; i++) free(s->limited_prefixes[i]);
    s->limited_count = 0; free(value); return true;
  }
  bool numbered = value[0] != '\0';
  for (const char *p = value; *p; p++) if (!isdigit((unsigned char)*p)) numbered = false;
  long index = -1;
  if (numbered) index = (long)qa_native_atoi(value) - 1;
  else for (size_t i = 0; i < s->limited_count; i++) if (strcmp(s->limited_prefixes[i], value) == 0) index = (long)i;
  if (index < 0 || (size_t)index >= s->limited_count) {
    qa_cleanup value_guard;
    qa_cleanup_push(&value_guard, free, value);
    if (numbered) print_format(print, print_ctx, "No such lrconcmd index: %d\n", (int)qa_native_atoi(value));
    else print_format(print, print_ctx, "No such lrconcmd string: %s\n", value);
    qa_cleanup_pop(&value_guard);
  } else {
    free(s->limited_prefixes[index]);
    memmove(&s->limited_prefixes[index], &s->limited_prefixes[index + 1], (s->limited_count - (size_t)index - 1) * sizeof(char *));
    s->limited_count--;
  }
  free(value);
  return true;
}
typedef struct rate_result { uint32_t value; size_t end; } rate_result;
static rate_result rate_unsigned(const char *text, size_t offset) {
  const char *p = text + offset;
  while (*p == '\t' || *p == '\n' || *p == '\v' || *p == '\f' || *p == '\r' || *p == ' ') p++;
  bool negative = false;
  if (*p == '+' || *p == '-') { negative = *p == '-'; p++; }
  if (!isdigit((unsigned char)*p)) { rate_result r = {0, offset}; return r; }
  /* BigInt magnitude capped at 2^64-1, then asUintN(32). */
  __uint128_t magnitude = 0;
  bool over = false;
  while (isdigit((unsigned char)*p)) {
    if (!over) {
      magnitude = magnitude * 10 + (unsigned)(*p - '0');
      if (magnitude > (__uint128_t)0xffffffffffffffffULL) { magnitude = (__uint128_t)0xffffffffffffffffULL; over = true; }
    }
    p++;
  }
  uint64_t mag = (uint64_t)magnitude;
  /* TS: magnitude > 2^64-1 ? 2^64-1 : sign === "-" ? -magnitude : magnitude (the cap keeps no sign). */
  uint32_t value = over ? 0xffffffffu : negative ? (uint32_t)(0ULL - mag) : (uint32_t)mag;
  rate_result r = {value, (size_t)(p - text)};
  return r;
}
static uint32_t rate_credits(double rate) {
  double v = rate > 4294967295.0 / 32000.0 ? trunc(rate / 10000.0) * 32000.0 : trunc(rate * 32000.0 / 10000.0);
  return qa_uint32(v);
}
static void initialize_rcon_rate(qa_app_server_operator_state *s, const char *text, double now,
                                 void (*print)(void *ctx, const char *text), void *print_ctx) {
  char buf[256];
  rate_result limit = rate_unsigned(text, 0);
  size_t cursor = limit.end;
  double period = 1, multiplier = 1;
  if (text[cursor] == '/') {
    rate_result parsed = rate_unsigned(text, cursor + 1);
    period = parsed.value != 0 ? (double)parsed.value : 1;
    cursor = parsed.end;
    int unit = tolower((unsigned char)text[cursor]);
    if (unit == 's' || unit == 'm' || unit == 'h') { multiplier = unit == 'h' ? 3600 : unit == 'm' ? 60 : 1; cursor++; }
  }
  if (limit.value == 0) { s->rcon_time = 0; s->rcon_credit = 0; s->rcon_cap = 0; s->rcon_cost = 0; return; }
  if (period > 4294967295.0 / (10000.0 * multiplier)) {
    char *n = qa_input_js_number(period);
    snprintf(buf, sizeof buf, "Period too large: %s\n", n); free(n);
    print(print_ctx, buf); return;
  }
  double rate = trunc(10000.0 * period * multiplier / (double)limit.value);
  if (rate == 0) {
    char *n = qa_input_js_number(limit.value);
    snprintf(buf, sizeof buf, "Limit too large: %s\n", n); free(n);
    print(print_ctx, buf); return;
  }
  const char *star = strchr(text + cursor, '*');
  double burst = star == NULL ? 5 : (double)rate_unsigned(text, (size_t)(star - text) + 1).value;
  if (burst > 4294967295.0 / rate) {
    char *n = qa_input_js_number(burst);
    snprintf(buf, sizeof buf, "Burst too large: %s\n", n); free(n);
    print(print_ctx, buf); return;
  }
  s->rcon_time = qa_uint32(now);
  s->rcon_credit = rate_credits(rate * burst);
  s->rcon_cap = s->rcon_credit;
  s->rcon_cost = rate_credits(rate);
}
bool qa_app_server_operator_rcon_rate_allowed(qa_app_server_operator_state *s, qa_cvar_registry *cvars, double now,
                                              void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_cvar_read setting; memset(&setting, 0, sizeof setting);
  if (!qa_cvar_registry_find(cvars, "sv_rcon_limit", &setting))
    qa_raise("Error", 0, 0, "Register source administration cvars before accepting rcon");
  if (s->rcon_text == NULL || strcmp(s->rcon_text, setting.value) != 0
      || (s->rcon_owner == cvars && s->rcon_revision != setting.modification_count))
    initialize_rcon_rate(s, setting.value, now, print, print_ctx);
  s->rcon_owner = cvars; s->rcon_revision = setting.modification_count;
  free(s->rcon_text); s->rcon_text = xdup(setting.value);
  qa_cvar_read_free(&setting);
  uint32_t time_u = qa_uint32(now);
  s->rcon_credit = s->rcon_credit + (uint32_t)((time_u - s->rcon_time) * 32u);
  s->rcon_time = time_u;
  if (s->rcon_credit > s->rcon_cap) s->rcon_credit = s->rcon_cap;
  if (s->rcon_credit < s->rcon_cost) return false;
  s->rcon_credit -= s->rcon_cost;
  return true;
}
void qa_app_server_operator_recharge_rcon_rate(qa_app_server_operator_state *s) {
  uint32_t sum = s->rcon_credit + s->rcon_cost;
  s->rcon_credit = sum < s->rcon_cap ? sum : s->rcon_cap;
}
qa_net_address *qa_app_server_operator_masters(qa_app_server_operator_state *s, qa_command_dialect dialect, size_t *out_count) {
  *out_count = 0;
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) return NULL;
  qa_net_address *src = NULL; size_t src_count = 0; bool q2_default = false;
  if (is_q2(dialect)) { src = s->masters_q2; src_count = s->masters_q2_count; q2_default = true; }
  else if (dialect == QA_COMMAND_DIALECT_Q3) { src = s->masters_q3; src_count = s->masters_q3_count; }
  else { src = s->masters_qw; src_count = s->masters_qw_count; }
  size_t total = src_count + (q2_default ? 1 : 0);
  qa_net_address *out = calloc(total ? total : 1, sizeof *out);
  if (!out) qa_range_error("Out of memory");
  size_t at = 0;
  if (q2_default) {
    int64_t host[4] = {192, 246, 40, 37};
    if (!qa_net_make_ipv4(&out[at++], host, 27900, false)) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < src_count; i++) if (!qa_net_address_clone(&src[i], &out[at++])) qa_range_error("Out of memory");
  *out_count = total;
  return out;
}
bool qa_app_server_operator_rejects(qa_app_server_operator_state *s, const qa_net_address *address, double filterban) {
  if (address->kind != QA_NET_IPV4) return false;
  uint32_t incoming = (uint32_t)address->ipv4[0] | ((uint32_t)address->ipv4[1] << 8) | ((uint32_t)address->ipv4[2] << 16) | ((uint32_t)address->ipv4[3] << 24);
  size_t n = 0; qa_ipv4_filter *snap = qa_ip_filter_list_snapshot(&s->filters, &n);
  bool matches = false;
  for (size_t i = 0; i < n; i++) if ((incoming & snap[i].mask) == snap[i].compare) matches = true;
  free(snap);
  return matches ? trunc(filterban) != 0 : filterban == 0;
}
bool qa_app_server_operator_filter_command(qa_app_server_operator_state *s, qa_app_server_operator_host host, const char *name,
                                           const char *const *args, size_t arg_count) {
  if (host.dialect == QA_COMMAND_DIALECT_Q3) return false;
  if (strcmp(name, "sv") == 0 && is_q2(host.dialect)) {
    name = arg_count > 0 ? args[0] : "";
    if (arg_count > 0) { args++; arg_count--; }
  }
  if (strcmp(name, "addip") != 0 && strcmp(name, "removeip") != 0 && strcmp(name, "listip") != 0 && strcmp(name, "writeip") != 0) return false;
  char buf[8192];
  if (strcmp(name, "listip") == 0) {
    size_t n = 0; qa_ipv4_filter *snap = qa_ip_filter_list_snapshot(&s->filters, &n);
    char *text = xdup("Filter list:\n");
    for (size_t i = 0; i < n; i++) { char fb[64]; filter_text(snap[i], fb, sizeof fb); if (i) append_str(&text, "\n"); append_str(&text, fb); }
    append_str(&text, "\n");
    free(snap);
    host.print(host.ctx, text); free(text);
    return true;
  }
  if (strcmp(name, "writeip") == 0) {
    const char *prefix = is_q2(host.dialect) ? "sv " : "";
    snprintf(buf, sizeof buf, "set filterban %s\n", QA_JS_NUMBER(trunc(qa_cvar_registry_variable_value(host.cvars, "filterban"))));
    char *text = xdup(buf);
    qa_cleanup text_guard;
    qa_cleanup_push(&text_guard, free_text_slot, &text);
    size_t n = 0; qa_ipv4_filter *snap = qa_ip_filter_list_snapshot(&s->filters, &n);
    qa_cleanup snap_guard;
    qa_cleanup_push(&snap_guard, free, snap);
    for (size_t i = 0; i < n; i++) { char fb[64]; filter_text(snap[i], fb, sizeof fb); append_str(&text, prefix); append_str(&text, "addip "); append_str(&text, fb); append_str(&text, "\n"); }
    qa_cleanup_pop(&snap_guard);
    free(snap);
    /* await host.writeConfig(...): a rejected write propagates (the host raises) and
     * nothing is printed. */
    host.write_config(host.ctx, "listip.cfg", text);
    qa_cleanup_pop(&text_guard);
    free(text);
    host.print(host.ctx, "Wrote listip.cfg\n");
    return true;
  }
  const char *text = arg_count > 0 ? args[0] : NULL;
  if (text == NULL) { snprintf(buf, sizeof buf, "Usage: %s <ip-mask>\n", name); host.print(host.ctx, buf); return true; }
  qa_ipv4_filter filter;
  if (!qa_source_ipv4_filter(text, &filter)) { print_format(host.print, host.ctx, "Bad filter address: %s\n", text); return true; }
  if (strcmp(name, "removeip") == 0) {
    bool removed = qa_ip_filter_list_remove(&s->filters, filter);
    if (removed) save_filters(s);
    host.print(host.ctx, removed ? "Removed.\n" : "Didn't find.\n");
    return true;
  }
  size_t n = 0; qa_ipv4_filter *snap = qa_ip_filter_list_snapshot(&s->filters, &n); free(snap);
  if (n >= 1024) { host.print(host.ctx, "IP filter list is full\n"); return true; }
  qa_ip_filter_list_add(&s->filters, filter);
  save_filters(s);
  return true;
}
/* The addresses a master resolution has collected so far (released with a raise). */
typedef struct master_list { qa_net_address *addresses; size_t count; } master_list;
static void release_master_list(void *arg) {
  master_list *list = arg;
  free_masters(list->addresses, list->count);
  list->addresses = NULL; list->count = 0;
}
/* host.resolve_address returns its error text; TS resolveAddress throws it. */
static void resolve_master(const qa_app_server_operator_host *host, const char *name, int64_t port, qa_net_address *out) {
  memset(out, 0, sizeof *out);
  const char *error = host->resolve_address(host->ctx, name, port, out);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
typedef struct set_master_call { const qa_app_server_operator_host *host; const char *name; bool qw; master_list *list; } set_master_call;
static void set_master_body(void *ctx) {
  set_master_call *call = ctx;
  const qa_app_server_operator_host *host = call->host;
  qa_net_address address;
  resolve_master(host, call->name, call->qw ? 27000 : 27900, &address);
  call->list->addresses[call->list->count++] = address;
  char *key = qa_net_address_key(&address, true);
  if (key == NULL) qa_range_error("Out of memory");
  qa_cleanup key_guard;
  qa_cleanup_push(&key_guard, free, key);
  print_format(host->print, host->ctx, "Master server at %s\nSending a ping.\n", key);
  qa_cleanup_pop(&key_guard);
  free(key);
  if (call->qw) { uint8_t ping[2] = {107, 0}; host->send(host->ctx, &address, ping, 2); }
  else {
    size_t plen = 0; uint8_t *ping = qa_q2_out_of_band("ping", false, &plen);
    if (ping == NULL) qa_range_error("Out of memory");
    qa_cleanup ping_guard;
    qa_cleanup_push(&ping_guard, free, ping);
    host->send(host->ctx, &address, ping, plen);
    qa_cleanup_pop(&ping_guard);
    free(ping);
  }
}
void qa_app_server_operator_set_masters(qa_app_server_operator_state *s, qa_app_server_operator_host host,
                                        const char *const *names, size_t name_count) {
  if (s->closed) return;
  bool qw = host.dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  if (!qw && !is_q2(host.dialect)) qa_raise("Error", 0, 0, "This source uses master cvars instead of setmaster");
  if (!qw && !host.dedicated) { host.print(host.ctx, "Only dedicated servers use masters.\n"); return; }
  s->generation++;
  size_t limit = qw ? 8 : 7;
  master_list list = {calloc(limit, sizeof *list.addresses), 0};
  if (!list.addresses) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_master_list, &list);
  for (size_t i = 0; i < name_count && i < limit; i++) {
    if (qw && strcmp(names[i], "none") == 0) break;
    /* try { resolve; push; print; send } catch (error) { print(`Bad master address ${name}: ${error.message}`) }:
     * a resolver that raises (resolveAddress's RangeError, a lookup failure) is caught like one that returns its text. */
    set_master_call call = {&host, names[i], qw, &list};
    qa_caught_error caught;
    if (!qa_try(set_master_body, &call, &caught))
      print_format(host.print, host.ctx, "Bad master address %s: %s\n", names[i], caught.message);
  }
  qa_cleanup_pop(&guard);
  if (qw) { free_masters(s->masters_qw, s->masters_qw_count); s->masters_qw = list.addresses; s->masters_qw_count = list.count; }
  else { free_masters(s->masters_q2, s->masters_q2_count); s->masters_q2 = list.addresses; s->masters_q2_count = list.count; }
  host.heartbeat(host.ctx);
}
/* refreshQ3Masters resolves in a detached async function: resolveAddress settles an
 * address literal (or a parse error) within the microtasks that run before the step's
 * first heartbeat, but a host name waits for dns.lookup, which completes on a later
 * event-loop turn, so the masters (and every print after that name) land on a later step
 * at the earliest. The C resolution runs the literal prefix at once and keeps the rest
 * pending until the next refresh, the earliest step TS could apply it. */
static bool ip_literal(const char *host) {
  unsigned char v4[4], v6[16];
  if (inet_pton(AF_INET, host, v4) == 1) return true;
  const char *pct = strchr(host, '%');
  size_t length = pct ? (size_t)(pct - host) : strlen(host);
  char literal[INET6_ADDRSTRLEN + 1];
  if (length >= sizeof literal) return false;
  memcpy(literal, host, length); literal[length] = '\0';
  if (inet_pton(AF_INET6, literal, v6) != 1) return false;
  if (pct) {
    if (pct[1] == '\0') return false;
    for (const char *p = pct + 1; *p; p++) {
      unsigned char c = (unsigned char)*p;
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '.' || c == ':')) return false;
    }
  }
  return true;
}
/* /^\d+$/ then Number(text) against portNumber's 1..65535. */
static bool port_text_valid(const char *text) {
  if (*text == '\0') return false;
  unsigned long value = 0;
  for (const char *p = text; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    if (value <= 65535) value = value * 10 + (unsigned long)(*p - '0');
  }
  return value >= 1 && value <= 65535;
}
/* True when resolveAddress(text, 27950, 4) reaches dns.lookup: the host/port split and
 * the port check pass and the host is not an IP literal (every other outcome settles
 * without I/O). */
static bool needs_lookup(const char *text) {
  const char *host = text, *host_end = text + strlen(text);
  if (text[0] == '[') {
    const char *end = strchr(text, ']');
    if (end == NULL) return false;
    host = text + 1; host_end = end;
    if (end[1] != '\0' && (end[1] != ':' || !port_text_valid(end + 2))) return false;
  } else {
    const char *first = strchr(text, ':'), *last = strrchr(text, ':');
    if (first != NULL && first == last) {
      host_end = first;
      if (!port_text_valid(first + 1)) return false;
    }
  }
  size_t length = (size_t)(host_end - host);
  char *copy = malloc(length + 1);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, host, length); copy[length] = '\0';
  bool literal = ip_literal(copy);
  free(copy);
  return !literal;
}
static void q3_resolution_release(qa_app_server_operator_state *s) {
  for (size_t i = 0; i < s->q3_pending_count; i++) free(s->q3_pending_names[i]);
  free(s->q3_pending_names);
  free_masters(s->q3_pending_addresses, s->q3_pending_address_count);
  s->q3_pending_names = NULL; s->q3_pending_count = s->q3_pending_next = 0;
  s->q3_pending_addresses = NULL; s->q3_pending_address_count = 0;
  s->q3_pending = false;
}
typedef struct q3_resolve_call { qa_app_server_operator_state *s; qa_app_server_operator_host host; bool defer; } q3_resolve_call;
typedef struct q3_master_call { const qa_app_server_operator_host *host; const char *name; qa_app_server_operator_state *s; } q3_master_call;
static void q3_master_body(void *ctx) {
  q3_master_call *call = ctx;
  qa_net_address address;
  resolve_master(call->host, call->name, 27950, &address);
  call->s->q3_pending_addresses[call->s->q3_pending_address_count++] = address;
}
static void q3_resolve_body(void *ctx) {
  q3_resolve_call *call = ctx;
  qa_app_server_operator_state *s = call->s;
  for (; s->q3_pending_next < s->q3_pending_count; s->q3_pending_next++) {
    const char *name = s->q3_pending_names[s->q3_pending_next];
    if (call->defer && needs_lookup(name)) return;
    /* try { addresses.push(await resolveAddress(name, 27950, 4)) } catch (error) { print(`Bad master address ...`) } */
    q3_master_call one = {&call->host, name, s};
    qa_caught_error caught;
    if (!qa_try(q3_master_body, &one, &caught))
      print_format(call->host.print, call->host.ctx, "Bad master address %s: %s\n", name, caught.message);
  }
  /* if (generation === this.generation) this.masterAddresses.set("q3", addresses) */
  free_masters(s->masters_q3, s->masters_q3_count);
  s->masters_q3 = s->q3_pending_addresses; s->masters_q3_count = s->q3_pending_address_count;
  s->q3_pending_addresses = NULL; s->q3_pending_address_count = 0;
  q3_resolution_release(s);
}
/* resolve().catch(error => { if (generation === this.generation) this.masterFailure = { error } }) */
static void q3_resolve(qa_app_server_operator_state *s, qa_app_server_operator_host host, bool defer) {
  q3_resolve_call call = {s, host, defer};
  qa_caught_error caught;
  if (qa_try(q3_resolve_body, &call, &caught)) return;
  q3_resolution_release(s);
  s->master_failure = caught;
  s->has_master_failure = true;
}
typedef struct q3_names_scope { char *names[5]; size_t count; char *key; } q3_names_scope;
static void q3_names_release(void *arg) {
  q3_names_scope *scope = arg;
  for (size_t i = 0; i < scope->count; i++) free(scope->names[i]);
  scope->count = 0;
  free(scope->key); scope->key = NULL;
}
void qa_app_server_operator_refresh_q3_masters(qa_app_server_operator_state *s, qa_app_server_operator_host host, qa_cvar_registry *cvars) {
  if (s->closed) return;
  /* A pending lookup finished between steps: it settles before this step reads anything. */
  if (s->q3_pending) q3_resolve(s, host, false);
  if (s->has_master_failure) {
    qa_caught_error failure = s->master_failure;
    s->has_master_failure = false;
    qa_rethrow(&failure);
  }
  q3_names_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, q3_names_release, &scope);
  scope.key = xdup("");
  for (int index = 1; index <= 5; index++) {
    char var[32]; snprintf(var, sizeof var, "sv_master%d", index);
    char *value = qa_cvar_registry_variable_string(cvars, var);
    if (value == NULL || value[0] == '\0') { free(value); continue; }
    scope.names[scope.count++] = value;
    if (scope.count > 1) append_str(&scope.key, "\n");
    append_str(&scope.key, value);
  }
  if (strcmp(scope.key, s->master_names) == 0) {
    qa_cleanup_pop(&guard);
    q3_names_release(&scope);
    return;
  }
  qa_net_address *addresses = calloc(scope.count ? scope.count : 1, sizeof *addresses);
  char **names = calloc(scope.count ? scope.count : 1, sizeof *names);
  if (addresses == NULL || names == NULL) { free(addresses); free(names); qa_range_error("Out of memory"); }
  free(s->master_names); s->master_names = scope.key; scope.key = NULL;
  s->generation++;
  /* A resolution of the previous names is retired by the generation. */
  q3_resolution_release(s);
  for (size_t i = 0; i < scope.count; i++) names[i] = scope.names[i];
  s->q3_pending_names = names; s->q3_pending_count = scope.count; scope.count = 0;
  s->q3_pending_addresses = addresses;
  s->q3_pending = true;
  qa_cleanup_pop(&guard);
  q3_names_release(&scope);
  q3_resolve(s, host, true);
}
void qa_app_server_operator_state_close(qa_app_server_operator_state *s) {
  s->closed = true; s->generation++; s->has_master_failure = false;
  q3_resolution_release(s);
}
const char *const *qa_app_source_administration_command_names(qa_command_dialect dialect, size_t *out_count) {
  static const char *const qw[] = {"setmaster", "heartbeat", "addip", "removeip", "listip", "writeip"};
  static const char *const q2[] = {"setmaster", "heartbeat", "sv", "addlrconcmd", "dellrconcmd", "listlrconcmds"};
  static const char *const q3[] = {"heartbeat", "addip", "removeip", "listip"};
  switch (dialect) {
    case QA_COMMAND_DIALECT_Q1_NETQUAKE: *out_count = 0; return NULL;
    case QA_COMMAND_DIALECT_Q1_QUAKEWORLD: *out_count = 6; return qw;
    case QA_COMMAND_DIALECT_Q2_CLASSIC:
    case QA_COMMAND_DIALECT_Q2_RERELEASE: *out_count = 6; return q2;
    case QA_COMMAND_DIALECT_Q3: *out_count = 4; return q3;
  }
  *out_count = 0; return NULL;
}
static void register_one(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  if (!cvar_exists_(cvars, name) || qa_cvar_registry_is_console_created(cvars, name))
    cvar_register_(cvars, name, value, flags);
  else qa_cvar_registry_add_flags(cvars, name, flags);
}
void qa_app_register_source_administration_cvars(qa_cvar_registry *cvars) {
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  bool q2 = is_q2(dialect);
  register_one(cvars, dialect == QA_COMMAND_DIALECT_Q3 ? "rconPassword" : "rcon_password", "",
               q2 ? QA_Q2_CVAR_FLAG_PRIVATE : dialect == QA_COMMAND_DIALECT_Q3 ? QA_CVAR_FLAG_TEMPORARY : 0);
  if (dialect != QA_COMMAND_DIALECT_Q3) register_one(cvars, "filterban", "1", 0);
  if (q2) register_one(cvars, "public", "0", QA_Q2_CVAR_FLAG_LATCH);
  if (q2) { register_one(cvars, "lrcon_password", "", QA_Q2_CVAR_FLAG_PRIVATE); register_one(cvars, "sv_rcon_limit", "1", 0); }
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    for (int index = 1; index <= 5; index++) {
      char var[32]; snprintf(var, sizeof var, "sv_master%d", index);
      register_one(cvars, var, index == 1 ? "master.quake3arena.com" : "", index == 1 ? 0 : QA_CVAR_FLAG_ARCHIVE);
    }
  }
}
typedef struct admin_ctx {
  qa_app_source_administration_options options;
  char *password;
  qa_net_address *masters; size_t master_count;
} admin_ctx;
static const char *admin_password(void *ctx) {
  admin_ctx *a = ctx;
  free(a->password);
  const char *name = a->options.dialect == QA_COMMAND_DIALECT_Q3 ? "rconPassword" : "rcon_password";
  a->password = qa_cvar_registry_variable_string(a->options.cvars, name);
  return a->password ? a->password : "";
}
static void admin_execute(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx) {
  admin_ctx *a = ctx;
  a->options.execute(a->options.execute_ctx, command, output, output_ctx);
}
static bool admin_rejects(void *ctx, const qa_net_address *address) {
  admin_ctx *a = ctx;
  if (a->options.dialect == QA_COMMAND_DIALECT_Q3) return false;
  return qa_app_server_operator_rejects(a->options.state, address, qa_cvar_registry_variable_value(a->options.cvars, "filterban"));
}
static const qa_net_address *admin_masters(void *ctx, size_t *out_count) {
  admin_ctx *a = ctx;
  free_masters(a->masters, a->master_count);
  a->masters = NULL; a->master_count = 0;
  bool q2 = is_q2(a->options.dialect);
  if (!a->options.dedicated || (q2 && qa_cvar_registry_variable_value(a->options.cvars, "public") == 0)
      || (a->options.dialect == QA_COMMAND_DIALECT_Q3 && qa_cvar_registry_variable_value(a->options.cvars, "dedicated") != 2)) {
    *out_count = 0; return NULL;
  }
  a->masters = qa_app_server_operator_masters(a->options.state, a->options.dialect, &a->master_count);
  *out_count = a->master_count;
  return a->masters;
}
static void admin_record(void *ctx, const qa_net_address *address, const char *result) {
  admin_ctx *a = ctx;
  a->options.record(a->options.record_ctx, address, result);
}
qa_server_administration qa_app_source_server_administration(const qa_app_source_administration_options *options) {
  qa_app_register_source_administration_cvars(options->cvars);
  admin_ctx *a = calloc(1, sizeof *a);
  if (!a) qa_range_error("Out of memory");
  a->options = *options;
  qa_server_administration admin = {a, admin_password, admin_execute, admin_rejects, admin_masters, admin_record};
  return admin;
}
void qa_app_source_server_administration_destroy(qa_server_administration *administration) {
  if (administration == NULL || administration->ctx == NULL) return;
  admin_ctx *a = administration->ctx;
  free(a->password);
  free_masters(a->masters, a->master_count);
  free(a);
  administration->ctx = NULL;
}
