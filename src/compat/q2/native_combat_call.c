/* Port of src/compat/q2/native-combat-call.ts. readNativeCombatField and
 * lowerNativeCombatArguments need a live GuestCpu/MappedGuestMemory, so they
 * live beside it: the guest runner's read_combat_field and
 * qa_compat_q2_lower_native_combat_arguments (classic/combat_binding.c).
 * GPL-2.0-or-later. */
#include "qa/compat/q2/native_combat_call.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/persistence/execution.h"
static const qa_guest_field_layout mod_fields[]={{"id",0,"uint8",1},{"friendly_fire",1,"uint8",1},{"no_point_loss",2,"uint8",1}};
const qa_guest_layout qa_compat_q2_native_rerelease_mod_layout={"q2-rerelease:mod_t",3,1,8,mod_fields,3};
static const qa_compat_q2_native_combat_field dmg[]={QA_Q2CF_TARGET,QA_Q2CF_INFLICTOR,QA_Q2CF_ATTACKER,QA_Q2CF_DIRECTION,QA_Q2CF_POINT,QA_Q2CF_NORMAL,QA_Q2CF_AMOUNT,QA_Q2CF_KNOCKBACK,QA_Q2CF_FLAGS,QA_Q2CF_CAUSE};
static const qa_compat_q2_native_combat_field reg[]={QA_Q2CF_TARGET,QA_Q2CF_POINT,QA_Q2CF_NORMAL,QA_Q2CF_AMOUNT,QA_Q2CF_SPARKS,QA_Q2CF_FLAGS};
static const qa_compat_q2_native_combat_field pow_[]={QA_Q2CF_TARGET,QA_Q2CF_POINT,QA_Q2CF_NORMAL,QA_Q2CF_AMOUNT,QA_Q2CF_FLAGS};
static const qa_compat_q2_native_combat_field pain4[]={QA_Q2CF_TARGET,QA_Q2CF_ATTACKER,QA_Q2CF_KICK,QA_Q2CF_AMOUNT};
static const qa_compat_q2_native_combat_field pain8[]={QA_Q2CF_TARGET,QA_Q2CF_ATTACKER,QA_Q2CF_KICK,QA_Q2CF_AMOUNT,QA_Q2CF_CAUSE};
static const qa_compat_q2_native_combat_field death4[]={QA_Q2CF_TARGET,QA_Q2CF_INFLICTOR,QA_Q2CF_ATTACKER,QA_Q2CF_AMOUNT,QA_Q2CF_POINT};
static const qa_compat_q2_native_combat_field death8[]={QA_Q2CF_TARGET,QA_Q2CF_INFLICTOR,QA_Q2CF_ATTACKER,QA_Q2CF_AMOUNT,QA_Q2CF_POINT,QA_Q2CF_CAUSE};
static const qa_compat_q2_native_combat_field def[]={QA_Q2CF_TARGET};
const qa_compat_q2_native_combat_field *qa_compat_q2_native_combat_fields(qa_compat_q2_native_combat_operation op, int64_t pb, size_t *count) {
  switch (op) {
    case QA_Q2CO_DAMAGE: *count=10; return dmg;
    case QA_Q2CO_REGULAR_ARMOR: *count=6; return reg;
    case QA_Q2CO_POWER_ARMOR: *count=5; return pow_;
    case QA_Q2CO_PAIN: if (pb==4) {*count=4; return pain4;} *count=5; return pain8;
    case QA_Q2CO_DEATH: if (pb==4) {*count=5; return death4;} *count=6; return death8;
    default: *count=1; return def;
  }
}
qa_compat_q2_native_combat_call qa_compat_q2_stock_native_combat_call(qa_compat_q2_native_combat_operation op, bool has_abi, qa_native_call_abi abi, qa_compat_q2_native_combat_argument *storage) {
  size_t n; const qa_compat_q2_native_combat_field *f=qa_compat_q2_native_combat_fields(op, has_abi?abi.pointer_bytes:4,&n);
  for (size_t i=0;i<n;i++) { memset(&storage[i],0,sizeof storage[i]); storage[i].kind=QA_Q2CA_FIELD; storage[i].field=f[i]; }
  qa_compat_q2_native_combat_call c={(has_abi&&abi.pointer_bytes==8)?"microsoft-x64":"cdecl",storage,n}; return c;
}
static qa_guest_value_layout lay(const qa_compat_q2_native_combat_argument *a, qa_native_call_abi abi) {
  qa_guest_value_layout v; memset(&v,0,sizeof v);
  if (a->kind==QA_Q2CA_ADDRESS) { v.storage="pointer"; return v; }
  if (a->kind==QA_Q2CA_VALUE) return a->layout;
  switch (a->field) {
    case QA_Q2CF_TARGET: case QA_Q2CF_INFLICTOR: case QA_Q2CF_ATTACKER: case QA_Q2CF_DIRECTION: case QA_Q2CF_POINT: case QA_Q2CF_NORMAL: v.storage="pointer"; return v;
    case QA_Q2CF_AMOUNT: case QA_Q2CF_KNOCKBACK: case QA_Q2CF_FLAGS: case QA_Q2CF_SPARKS: v.storage="int32"; return v;
    case QA_Q2CF_KICK: v.storage="float32"; return v;
    default: if (abi.pointer_bytes==4) {v.storage="int32"; return v;} v.is_aggregate=true; v.layout=qa_compat_q2_native_rerelease_mod_layout; return v;
  }
}
qa_compat_q2_guest_call_signature qa_compat_q2_native_combat_signature(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi, qa_guest_value_layout *storage) {
  qa_native_call_abi src;
  if (strcmp(abi.kind,"windows-i386")==0 && strcmp(call->convention,"microsoft-x64")!=0) { src=abi; src.call=call->convention; }
  else if (strcmp(abi.kind,"windows-x86-64")==0 && strcmp(call->convention,"microsoft-x64")==0) src=abi;
  else qa_raise("Error", 0, 0, "Native combat convention does not match its source architecture");
  for (size_t i=0;i<call->argument_count;i++) storage[i]=lay(&call->arguments[i],src);
  qa_compat_q2_guest_call_signature s; memset(&s,0,sizeof s); s.abi=src; s.parameters=storage; s.parameter_count=call->argument_count;
  s.result_is_void=!(op==QA_Q2CO_REGULAR_ARMOR||op==QA_Q2CO_POWER_ARMOR); s.result.storage="int32"; s.variadic=false; return s;
}
static int64_t vbytes(const qa_guest_value_layout *l, int64_t pb) {
  if (l->is_aggregate) return l->layout.byte_length >= 0 && l->layout.byte_length < 9.0e18 ? (int64_t)l->layout.byte_length : -1;
  const char *s=l->storage;
  if (!strcmp(s,"pointer")) return pb;
  if (!strcmp(s,"int8")||!strcmp(s,"uint8")) return 1;
  if (!strcmp(s,"int16")||!strcmp(s,"uint16")) return 2;
  if (!strcmp(s,"int32")||!strcmp(s,"uint32")||!strcmp(s,"float32")) return 4;
  return 8;
}
static int64_t storage_bytes(const char *s, int64_t pb) {
  if (!strcmp(s,"pointer")) return pb;
  if (!strcmp(s,"int8")||!strcmp(s,"uint8")) return 1;
  if (!strcmp(s,"int16")||!strcmp(s,"uint16")) return 2;
  if (!strcmp(s,"int32")||!strcmp(s,"uint32")||!strcmp(s,"float32")) return 4;
  return 8;
}
static bool safe_integer(double v) { return v == trunc(v) && fabs(v) <= 9007199254740991.0; }
/* TS validateValueLayout (guest/abi/values.ts); byteOrder is always little-endian in the C layout. */
static void validate_value_layout(const qa_guest_value_layout *l, int64_t pb) {
  if (!l->is_aggregate) return;
  const qa_guest_layout *record = &l->layout;
  bool power_of_two = safe_integer(record->alignment) && record->alignment > 0 && record->alignment <= 4096 &&
                      (((int64_t)record->alignment) & ((int64_t)record->alignment - 1)) == 0;
  if (record->pointer_bytes != pb || !safe_integer(record->byte_length) || record->byte_length <= 0 || !power_of_two)
    qa_raise("RangeError", 0, 0, "Invalid aggregate ABI layout");
  for (size_t i = 0; i < record->field_count; i++) {
    const qa_guest_field_layout *field = &record->fields[i];
    if (!safe_integer(field->byte_offset) || field->byte_offset < 0 || !safe_integer(field->count) || field->count < 0 ||
        field->byte_offset + field->count * (double)storage_bytes(field->storage, pb) > record->byte_length)
      qa_raise("RangeError", 0, 0, "Aggregate field %s exceeds its record", field->name);
  }
}
/* bad_bytes (NULL for none): per argument, a declared default byte above 255, which the byte array cannot hold. */
static void validate_call(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi, const bool *bad_bytes) {
  /* planGuestCall(nativeCombatSignature(...)): the parts a Windows signature can fail (buildCallPlan in
   * guest/abi/classify.ts): each parameter layout is validated, then thiscall wants its this pointer first. */
  { qa_guest_value_layout *st=calloc(call->argument_count?call->argument_count:1,sizeof *st); if(!st) qa_range_error("Out of memory");
    qa_cleanup node; qa_cleanup_push(&node,free,st);
    qa_compat_q2_guest_call_signature sig=qa_compat_q2_native_combat_signature(call,op,abi,st);
    for (size_t i=0;i<sig.parameter_count;i++) validate_value_layout(&st[i],sig.abi.pointer_bytes);
    if (!strcmp(sig.abi.kind,"windows-i386") && sig.abi.call && !strcmp(sig.abi.call,"thiscall") && sig.parameter_count>0 &&
        (st[0].is_aggregate || strcmp(st[0].storage,"pointer")!=0))
      qa_raise("TypeError", 0, 0, "thiscall's first explicit argument must be the this pointer");
    qa_cleanup_pop(&node); free(st); }
  size_t n; const qa_compat_q2_native_combat_field *req=qa_compat_q2_native_combat_fields(op,abi.pointer_bytes,&n);
  bool seen[12]={0}; size_t seen_n=0;
  for (size_t i=0;i<call->argument_count;i++) { const qa_compat_q2_native_combat_argument *a=&call->arguments[i];
    if (a->kind==QA_Q2CA_FIELD) { bool ok=false; for (size_t j=0;j<n;j++) if (req[j]==a->field) ok=true;
      if (!ok||seen[a->field]) qa_raise("Error", 0, 0, "Native combat fields must occur exactly once");
      seen[a->field]=true; seen_n++; }
    else if (a->kind==QA_Q2CA_ADDRESS) { if (a->address) {
      /* [rva, ...indirections], with no limit on the number of indirections */
      for (size_t j=0;j<=a->address->indirections.count;j++) { double value=j==0?a->address->rva:a->address->indirections.values[j-1];
        if (!safe_integer(value)||value<0||value>4294967295.0) qa_raise("Error", 0, 0, "Native combat address exceeds its image declaration"); } } }
    else { validate_value_layout(&a->layout,abi.pointer_bytes);
      bool ptr = !a->layout.is_aggregate ? (a->layout.storage&&!strcmp(a->layout.storage,"pointer")) : false;
      if (a->layout.is_aggregate) for (size_t j=0;j<a->layout.layout.field_count;j++) if (!strcmp(a->layout.layout.fields[j].storage,"pointer")) ptr=true;
      if (ptr) qa_raise("Error", 0, 0, "Native combat pointer defaults require an image-relative address");
      if ((int64_t)a->byte_count!=vbytes(&a->layout,abi.pointer_bytes) || (bad_bytes && bad_bytes[i])) qa_raise("Error", 0, 0, "Native combat default bytes do not match their source argument"); }
  }
  if (seen_n!=n) qa_raise("Error", 0, 0, "Native combat declaration omits a required source field");
}
void qa_compat_q2_validate_native_combat_call(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi) {
  validate_call(call, op, abi, NULL);
}
static void release_partial(void *call) { qa_compat_q2_native_combat_call_free(call); }
static const qa_save_reader *fld(const qa_save_reader *r,const char *nm,qa_save_reader *o){qa_save_reader_field(r,nm,o);return o;}
bool qa_compat_q2_read_native_combat_call(const qa_save_reader *reader, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi, qa_compat_q2_native_combat_call *out) {
  static const char *const conv[]={"cdecl","stdcall","fastcall","thiscall","microsoft-x64"};
  static const char *const kinds[]={"field","value","address"};
  static const char *const fnames[]={"target","inflictor","attacker","direction","point","normal","amount","knockback","flags","cause","sparks","kick"};
  qa_save_reader cr,lr; out->convention=qa_save_reader_choice_string(fld(reader,"convention",&cr),conv,5); qa_save_reader_destroy(&cr);
  fld(reader,"arguments",&lr); size_t n=qa_save_reader_list_count(&lr);
  qa_compat_q2_native_combat_argument *args=calloc(n?n:1,sizeof *args); if(!args){qa_save_reader_destroy(&lr);return false;}
  /* A reader or validation failure raises: what was read so far is released (the TS result is garbage then). */
  out->arguments=args; out->argument_count=n;
  qa_cleanup partial; qa_cleanup_push(&partial,release_partial,out);
  bool *bad_bytes=calloc(n?n:1,sizeof *bad_bytes); if(!bad_bytes) qa_range_error("Out of memory");
  qa_cleanup bad; qa_cleanup_push(&bad,free,bad_bytes);
  for (size_t i=0;i<n;i++){ qa_save_reader it,kr; qa_save_reader_item(&lr,i,&it);
    const char *kind=qa_save_reader_choice_string(fld(&it,"kind",&kr),kinds,3); qa_save_reader_destroy(&kr);
    if(!strcmp(kind,"field")){qa_save_reader fr; const char *fn=qa_save_reader_choice_string(fld(&it,"field",&fr),fnames,12); qa_save_reader_destroy(&fr);
      args[i].kind=QA_Q2CA_FIELD; for(int j=0;j<12;j++) if(!strcmp(fn,fnames[j])) args[i].field=(qa_compat_q2_native_combat_field)j;}
    else if(!strcmp(kind,"address")){qa_save_reader ar; fld(&it,"address",&ar); args[i].kind=QA_Q2CA_ADDRESS;
      if(!qa_save_reader_is_null(&ar)){qa_native_mod_address *ad=calloc(1,sizeof *ad); if(!ad) qa_range_error("Out of memory");
        args[i].address=ad; /* owned by the call from here: a field below can raise */
        qa_save_reader rv,iv; ad->rva=qa_compat_q2_native_offset(fld(&ar,"rva",&rv)); qa_save_reader_destroy(&rv);
        fld(&ar,"indirections",&iv); size_t m=qa_save_reader_list_count(&iv); double *arr=m?calloc(m,sizeof *arr):NULL;
        if(m&&!arr) qa_range_error("Out of memory");
        ad->indirections.values=arr; ad->indirections.count=m;
        for(size_t j=0;j<m;j++){qa_save_reader x; qa_save_reader_item(&iv,j,&x); arr[j]=qa_compat_q2_native_offset(&x); qa_save_reader_destroy(&x);}
        qa_save_reader_destroy(&iv);}
      qa_save_reader_destroy(&ar);}
    else {qa_save_reader sr,kr2,br; fld(&it,"layout",&sr); static const char *const lk[]={"scalar","aggregate"};
      const char *k2=qa_save_reader_choice_string(fld(&sr,"kind",&kr2),lk,2); qa_save_reader_destroy(&kr2); args[i].kind=QA_Q2CA_VALUE;
      if(!strcmp(k2,"scalar")){qa_save_reader st; args[i].layout.storage=qa_compat_q2_native_scalar(fld(&sr,"storage",&st)); qa_save_reader_destroy(&st);}
      else { qa_save_reader ly; args[i].layout.is_aggregate=true; args[i].layout.layout=qa_read_layout(fld(&sr,"layout",&ly)); qa_save_reader_destroy(&ly); }
      qa_save_reader_destroy(&sr);
      fld(&it,"bytes",&br); size_t m=qa_save_reader_list_count(&br); uint8_t *b=m?malloc(m):NULL;
      args[i].bytes=b; args[i].byte_count=m;
      /* byte.integer(0) accepts any non-negative integer; validation refuses one above 255. */
      for(size_t j=0;j<m;j++){qa_save_reader x; qa_save_reader_item(&br,j,&x); double value=qa_save_reader_integer_min(&x,0); qa_save_reader_destroy(&x);
        b[j]=qa_js_to_uint8(value);
        if(value>255) bad_bytes[i]=true;}
      qa_save_reader_destroy(&br);}
    qa_save_reader_destroy(&it);}
  qa_save_reader_destroy(&lr);
  validate_call(out,op,abi,bad_bytes);
  qa_cleanup_pop(&bad); free(bad_bytes);
  qa_cleanup_pop(&partial); return true;
}
void qa_compat_q2_native_combat_call_free(qa_compat_q2_native_combat_call *call) {
  for (size_t i=0;i<call->argument_count;i++){ free((void*)call->arguments[i].bytes);
    if(call->arguments[i].kind==QA_Q2CA_VALUE&&call->arguments[i].layout.is_aggregate) free((void*)call->arguments[i].layout.layout.fields);
    if(call->arguments[i].address){free((void*)call->arguments[i].address->indirections.values); free((void*)call->arguments[i].address);} }
  free((void*)call->arguments); call->arguments=NULL; call->argument_count=0;
}
size_t qa_compat_q2_read_native_combat_arguments(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, const qa_guest_call_value *values, size_t count, int64_t pb, qa_guest_call_value *out) {
  if (count!=call->argument_count) qa_raise("Error", 0, 0, "Native combat invocation differs from its declared signature");
  size_t n; const qa_compat_q2_native_combat_field *f=qa_compat_q2_native_combat_fields(op,pb,&n);
  for (size_t i=0;i<n;i++){ size_t idx=(size_t)-1; for(size_t j=0;j<call->argument_count;j++) if(call->arguments[j].kind==QA_Q2CA_FIELD&&call->arguments[j].field==f[i]) { idx=j; break; } /* findIndex: the first */
    if(idx==(size_t)-1) qa_raise("Error", 0, 0, "Native combat invocation lacks a declared field");
    out[i]=values[idx]; }
  return n;
}
