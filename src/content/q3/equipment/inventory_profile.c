/* Port of src/content/q3/equipment/inventory-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/inventory_profile.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
static const char STOCK[]="sha256:57c52bf22e4f528c064f8af1553a7103723bab0a02276bb11eed944bf829b219";
typedef struct cap { double limit; } cap;
typedef struct cap3 {
  double ordinary, fallback, game_type, special_mode, lithium;
  double *keys, *limits; size_t count;
} cap3;
static double constant_at(const qa_q3_equipment_qvm_artifact *a,int64_t index){
  if(index<0||(size_t)index>=a->image.instruction_count
    ||a->image.instructions[index].opcode!=QA_Q3_EQUIPMENT_OP_CONST)
    qa_raise("Error", 0, 0, "Q3 inventory source constant is not its qualified instruction");
  return a->image.instructions[index].operand;
}
static double opcode_at(const qa_q3_equipment_qvm_artifact *a,int64_t index){
  if(index<0||(size_t)index>=a->image.instruction_count) return -1;
  return a->image.instructions[index].opcode;
}
static double cap_at(const qa_q3_equipment_qvm_artifact *a,int64_t comparison,int64_t store){
  double value=constant_at(a,comparison);
  if(constant_at(a,store)!=value||opcode_at(a,comparison+1)!=QA_Q3_EQUIPMENT_OP_LEI
    ||opcode_at(a,store+1)!=QA_Q3_EQUIPMENT_OP_STORE4||value<0)
    qa_raise("Error", 0, 0, "Q3 inventory capacity comparison and store disagree");
  return value;
}
static int32_t read_i32(qa_q3_equipment_qvm_memory m,double address){
  /* DataView.getInt32 offset: ToIndex truncates, NaN is 0, a negative or out-of-range offset is a RangeError. */
  double index=isnan(address)?0:trunc(address);
  if(index<0||index+4>(double)m.byte_length) qa_range_error("QVM memory word is outside its allocation");
  size_t a=(size_t)index;
  uint32_t u=(uint32_t)m.bytes[a]|((uint32_t)m.bytes[a+1]<<8)|((uint32_t)m.bytes[a+2]<<16)|((uint32_t)m.bytes[a+3]<<24);
  int32_t v; memcpy(&v,&u,sizeof v); return v;
}
static double cap_stock(void*ctx,qa_q3_equipment_qvm_memory m,double weapon){ (void)m;(void)weapon; return ((cap*)ctx)->limit; }
static double cap_threewave(void*ctx,qa_q3_equipment_qvm_memory m,double weapon){
  cap3*c=ctx;
  /* TS `getInt32(...) === specialMode`: a number comparison, no int32 conversion of the constant. */
  bool special=(double)read_i32(m,c->game_type)==c->special_mode||read_i32(m,c->lithium)!=0;
  if(!special) return c->ordinary;
  for(size_t i=0;i<c->count;i++) if(c->keys[i]==weapon) return c->limits[i];
  return c->fallback;
}
static void cap3_release(void*arg){ cap3*c=arg; free(c->keys); free(c->limits); c->keys=c->limits=NULL; c->count=0; }
bool qa_q3_native_inventory_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_inventory_profile *out){
  const char *digest=artifact->module.digest?artifact->module.digest:"";
  if(strcmp(digest,STOCK)!=0&&strcmp(digest,qa_threewave_grapple_digest)!=0) return false;
  if(!artifact->role||strcmp(artifact->role,"qagame")!=0
    ||!artifact->abi_profile||strcmp(artifact->abi_profile,"q3-modern")!=0)
    qa_raise("Error", 0, 0, "Qualified Q3 inventory requires its modern server ABI");
  *out=(qa_q3_equipment_qvm_inventory_profile){0};
  out->module=artifact->module; out->abi_profile=artifact->abi_profile; out->ammo_offset=376;
  if(strcmp(digest,STOCK)==0){
    /* Checks first: a raise from cap_at must not leave the context allocated. */
    double limit=cap_at(artifact,103202,103216);
    cap*c=malloc(sizeof*c); if(!c) qa_range_error("out of memory");
    c->limit=limit; out->weapons_offset=192; out->capacity_ctx=c; out->capacity=cap_stock;
    return true;
  }
  cap3 built={0}; qa_cleanup guard; qa_cleanup_push(&guard,cap3_release,&built);
  built.ordinary=cap_at(artifact,166830,166844); built.fallback=cap_at(artifact,166753,166764);
  built.game_type=constant_at(artifact,166769); built.special_mode=constant_at(artifact,166771); built.lithium=constant_at(artifact,166773);
  double first=constant_at(artifact,166498); double last=constant_at(artifact,166507); double jump_table=constant_at(artifact,166514);
  qa_q3_equipment_qvm_memory data={artifact->image.initialized_data,artifact->image.initialized_data_length};
  size_t capacity=0;
  /* TS `for (let weapon = first; weapon <= last; weapon++)` over numbers. */
  for(double weapon=first;weapon<=last;weapon++){
    int32_t branch=read_i32(data,jump_table+weapon*4);
    double limit=cap_at(artifact,(int64_t)branch+10,(int64_t)branch+21);
    if(built.count==capacity){
      capacity=capacity?capacity*2:16;
      double *keys=realloc(built.keys,capacity*sizeof *keys); if(!keys) qa_range_error("out of memory"); built.keys=keys;
      double *limits=realloc(built.limits,capacity*sizeof *limits); if(!limits) qa_range_error("out of memory"); built.limits=limits;
    }
    built.keys[built.count]=weapon; built.limits[built.count]=limit; built.count++;
  }
  cap3*c=malloc(sizeof*c); if(!c) qa_range_error("out of memory");
  qa_cleanup_pop(&guard);
  *c=built; out->weapons_offset=204; out->capacity_ctx=c; out->capacity=cap_threewave;
  return true;
}
void qa_q3_equipment_qvm_inventory_profile_destroy(qa_q3_equipment_qvm_inventory_profile *p){
  if(!p||!p->capacity_ctx) return;
  if(p->capacity==cap_threewave){ cap3*c=p->capacity_ctx; free(c->keys); free(c->limits); }
  free(p->capacity_ctx); p->capacity_ctx=NULL;
}
