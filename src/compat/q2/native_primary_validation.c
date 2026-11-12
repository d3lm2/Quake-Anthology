/* Port of src/compat/q2/native-primary-validation.ts. profile->world is the
 * edition's world profile (classic/world_profile.h or
 * rerelease/world_profile.h). GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_validation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/world_profile.h"
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/world_profile.h"
static bool safei(double v){return v==trunc(v)&&v>=-9007199254740991.0&&v<=9007199254740991.0;}
static void bound(double off,double bytes,double limit){ if(!safei(off)||!safei(bytes)||off<0||bytes<1||off+bytes>limit) qa_raise("Error", 0, 0, "Native primary field exceeds its declared source record"); }
static qa_compat_q2_pe_file PE; static double ENT, CLI;
static void image(double off,double bytes,bool exec){ bound(off,bytes,PE.image_size); bool ok=false;
  for(size_t i=0;i<PE.section_count;i++){const qa_compat_q2_pe_section *s=&PE.sections[i];
    if(off>=s->rva&&off+bytes<=s->rva+s->mapped_size&&(exec?(s->permissions&&strstr(s->permissions,"execute")):(s->permissions&&strcmp(s->permissions,"none")!=0))) ok=true;}
  if(!ok) qa_raise("Error", 0, 0, "Native primary address has no matching image section"); }
static double width(const char *e){ if(!strcmp(e,"int8")||!strcmp(e,"uint8"))return 1; if(!strcmp(e,"int16")||!strcmp(e,"uint16"))return 2; if(!strcmp(e,"int64")||!strcmp(e,"uint64")||!strcmp(e,"float64"))return 8; return 4; }
static void field(qa_native_item_field f){ double b=width(f.field.encoding);
  if(!strcmp(f.record,"image")) image(f.field.offset,b,false);
  else if(!strcmp(f.record,"entity")) bound(f.field.offset,b,ENT);
  else if(!strcmp(f.record,"client")) bound(f.field.offset,b,CLI);
  else qa_raise("Error", 0, 0, "Native primary field has no declared source record"); }
static void test(const qa_native_item_test *t){ if(!strcmp(t->kind,"scalar")) field(t->field);
  else { if(!strcmp(t->pointer_field.record,"image")) image(t->pointer_field.offset,(double)PE.pointer_bytes,false);
    else if(!strcmp(t->pointer_field.record,"entity")) bound(t->pointer_field.offset,(double)PE.pointer_bytes,ENT);
    else if(!strcmp(t->pointer_field.record,"client")) bound(t->pointer_field.offset,(double)PE.pointer_bytes,CLI);
    else qa_raise("Error", 0, 0, "Native primary field has no declared source record");
    if(t->pointer_value) image(t->pointer_value->rva,(double)PE.pointer_bytes,false);} }
/* TS entries/regions arrays: no limit on how many a declared profile names. */
typedef struct { double *values; size_t count, capacity; } number_list;
static void push(number_list *l, double v) {
  if (l->count == l->capacity) { size_t c = l->capacity ? l->capacity * 2 : 64; double *g = realloc(l->values, c * sizeof *g); if (!g) qa_range_error("Out of memory"); l->values = g; l->capacity = c; }
  l->values[l->count++] = v;
}
typedef struct { number_list entries, regions; /* regions: entry, join pairs */ } working;
static void region(working *w, double entry, double join) { push(&w->regions, entry); push(&w->regions, join); }
static void release_working(void *v) { working *w = v; free(w->entries.values); free(w->regions.values); w->entries.values = w->regions.values = NULL; }
/* world.calls: an image-relative default must lie in a mapped section. */
static void call_addresses(const qa_compat_q2_native_combat_call *call) {
  for (size_t i = 0; i < call->argument_count; i++) {
    const qa_compat_q2_native_combat_argument *a = &call->arguments[i];
    if (a->kind != QA_Q2CA_ADDRESS || a->address == NULL) continue;
    size_t n = a->address->indirections.count;
    image(a->address->rva + (n > 0 ? a->address->indirections.values[0] : 0), n == 0 ? 1 : (double)PE.pointer_bytes, false);
  }
}
void qa_compat_q2_validate_native_primary(const qa_compat_q2_native_primary_profile *profile, qa_compat_q2_pe_file pe, double entity_bytes){
  PE=pe; ENT=entity_bytes; const qa_compat_q2_native_primary_weapon_profile *w=profile->weapons; CLI=w->client_byte_length;
  const qa_compat_q2_native_primary_player_profile *pl=profile->player;
  const qa_compat_q2_native_primary_command_profile *cm=profile->commands;
  const qa_compat_q2_native_primary_inventory_profile *in=profile->inventory;
  const qa_compat_q2_native_primary_drop_profile *dr=profile->drop;
  const qa_compat_q2_native_pickup_profile *pk=profile->pickups;
  const bool classic=!strcmp(profile->edition,"classic");
  const qa_compat_q2_classic_world_profile *cw=classic?profile->world:NULL;
  const qa_compat_q2_rerelease_primary_world_profile *rw=classic?NULL:profile->world;
  /* Object.values(profile.world.calls), in the order the profile declares them. */
  if(cw){ call_addresses(&cw->base.calls.pain); call_addresses(&cw->base.calls.death); call_addresses(&cw->base.calls.damage); call_addresses(&cw->base.calls.regular_armor); call_addresses(&cw->base.calls.power_armor); }
  else if(rw){ call_addresses(&rw->calls.pain); call_addresses(&rw->calls.death); call_addresses(&rw->calls.process_pain); call_addresses(&rw->calls.damage); call_addresses(&rw->calls.power_armor); }
  working work; memset(&work,0,sizeof work); qa_cleanup node; qa_cleanup_push(&node,release_working,&work);
  number_list *entries=&work.entries;
  double base[]={w->spawn_entry,w->attack_animation_entry,pl->spawn,cm->give_entry,cm->give_weapons,cm->give_ammo,cm->drop_entry,in->next_entry,in->next_scan,in->prev_entry,in->prev_scan,in->validate_entry,in->use_entry,in->use_call,in->named_entry,in->named_call,in->named_lookup_call,in->named_lookup_return,dr->named,dr->inv_entry,dr->inv_admitted,dr->find,dr->lookup_return,dr->allocate,dr->free_entry,pk->touch,pk->grant_return,pk->targets_return,pk->ammo_entry};
  for(size_t i=0;i<sizeof base/sizeof *base;i++) push(entries,base[i]);
  if(!w->dispatcher.entry.kind||strcmp(w->dispatcher.entry.kind,"rva")!=0) qa_raise("Error", 0, 0, "Primary source dispatcher requires a declared executable address");
  push(entries,w->dispatcher.entry.rva);
  /* regions, in TS order */
  for(size_t i=0;i<w->decision_count;i++) region(&work,w->decisions[i].entry,w->decisions[i].join);
  for(size_t i=0;i<w->attack_skip_count;i++) region(&work,w->attack_skip[i].entry,w->attack_skip[i].join);
  region(&work,w->delay_entry,w->delay_join);
  region(&work,cm->give_unknown_entry,cm->give_unknown_join);
  for(size_t i=0;i<cm->ammo_grant_count;i++) region(&work,cm->ammo_grants[i].entry,cm->ammo_grants[i].join);
  region(&work,cm->drop_elig_entry,cm->drop_elig_join);
  region(&work,in->next_entry,in->next_join); region(&work,in->prev_entry,in->prev_join);
  region(&work,in->use_entry,in->use_join); region(&work,in->named_entry,in->named_join);
  for(size_t i=0;i<dr->callback_count;i++) region(&work,dr->callbacks[i].entry,dr->callbacks[i].join);
  for(size_t i=0;i<dr->debit_count;i++) region(&work,dr->debits[i].entry,dr->debits[i].join);
  if(in->has_validate_scan) region(&work,in->validate_scan_entry,in->validate_scan_join);
  if(dr->has_consumer) region(&work,dr->consumer_entry,dr->consumer_join);
  if(!strcmp(pl->objectives_kind,"entry")) push(entries,pl->objectives_entry);
  if(!strcmp(w->damage_kind,"source-result")) push(entries,w->damage_entry);
  else { image(w->damage_address,width(w->damage_encoding),false); region(&work,w->damage_region_entry,w->damage_region_join); }
  if(!strcmp(w->delay_evaluate_kind,"source-animation")){ push(entries,w->delay_anim_entry); for(size_t i=0;i<w->delay_write_count;i++) field(w->delay_writes[i]); for(size_t i=0;i<w->delay_projection_count;i++) field(w->delay_projection[i].field); if(w->delay_baseline_ms<=0) qa_raise("Error", 0, 0, "Original weapon animation interval must be positive"); }
  for(size_t i=0;i<pk->grant_count;i++){const qa_compat_q2_native_pickup_grant *g=&pk->grants[i]; push(entries,g->entry); region(&work,g->recipient_entry,g->recipient_join);
    for(size_t j=0;j<g->consumer_count;j++) push(entries,g->consumers[j].entry);
    if(g->has_supply&&!strcmp(g->supply_kind,"ammo")) push(entries,g->supply_entry);
    else if(g->has_supply){push(entries,g->supply_ammo_return);push(entries,g->supply_settle);region(&work,g->supply_auto_entry,g->supply_auto_join);}}
  if(pk->has_ammo_stop) push(entries,pk->ammo_stop);
  for(size_t i=0;i+1<work.regions.count;i+=2){ push(entries,work.regions.values[i]); push(entries,work.regions.values[i+1]);
    if(work.regions.values[i]==work.regions.values[i+1]) qa_raise("Error", 0, 0, "Native source region is empty"); }
  for(size_t i=0;i<entries->count;i++) image(entries->values[i],1,true);
  qa_cleanup_pop(&node); release_working(&work);
  qa_native_item_field fs[]={w->entity_water_level,w->entity_view_height,w->entity_max_health,w->client_buttons,w->client_latched_buttons,w->delay_flag,w->anim_frame,w->anim_end,w->anim_priority,w->anim_duck,w->anim_run};
  for(size_t i=0;i<11;i++) field(fs[i]);
  for(size_t i=0;i<w->decision_count;i++) for(size_t j=0;j<w->decisions[i].field_count;j++){field(w->decisions[i].fields[j].field); double cm2=w->decisions[i].fields[j].clear_mask; if(cm2!=trunc(cm2)||cm2<1||cm2>4294967295.0) qa_raise("Error", 0, 0, "Native input mask is outside uint32");}
  for(size_t i=0;i<w->spawn_accepted_count;i++) test(&w->spawn_accepted[i]);
  for(size_t i=0;i<w->active_count;i++) test(&w->active[i]);
  for(size_t i=0;i<w->committed_input_count;i++) for(size_t j=0;j<w->committed_input[i].count;j++) test(&w->committed_input[i].tests[j]);
  for(size_t i=0;i<w->continuation_count;i++) for(size_t j=0;j<w->continuations[i].count;j++) test(&w->continuations[i].tests[j]);
  if(strcmp(w->entity_max_health.record,"entity")!=0||strcmp(w->entity_max_health.field.encoding,"int32")!=0) qa_raise("Error", 0, 0, "Native source max health requires its declared int32 entity field");
  bound(w->client_view_angles,12,CLI); bound(pl->command_angles,12,CLI);
  if(pl->has_match){bound(pl->match.score,4,CLI); if(fmod(pl->match.score,4)!=0) qa_raise("Error", 0, 0, "Native score field requires int32 alignment");}
  if(pl->has_forward) bound(pl->forward,12,CLI);
  bound(pl->velocity,12,ENT);
  if(in->client!=(classic?84:120)) qa_raise("Error", 0, 0, "Native client pointer differs from the selected public game API");
  bound(in->inventory,in->count*4,CLI); bound(in->cursor,4,CLI);
  for(size_t i=0;i<in->selection_write_count;i++) bound(in->selection_writes[i].offset,in->selection_writes[i].bytes,CLI);
  bound(cm->client_weapon,(double)PE.pointer_bytes,CLI); bound(dr->client_weapon,(double)PE.pointer_bytes,CLI); bound(dr->client_pending,(double)PE.pointer_bytes,CLI);
  if(cm->has_client_ammo_index) bound(cm->client_ammo_index,4,CLI);
  for(size_t i=0;i<pk->ammo_capacity_count;i++) bound(pk->ammo_capacities[i],pk->ammo_capacity_bytes,CLI);
  image(cm->give_argc,(double)PE.pointer_bytes,false); image(cm->give_argv,(double)PE.pointer_bytes,false);
  bound(cm->items_classname,(double)PE.pointer_bytes,cm->items_stride); bound(cm->items_icon,(double)PE.pointer_bytes,cm->items_stride); bound(pk->items_pickup,(double)PE.pointer_bytes,cm->items_stride);
  bound(cm->items_flags,4,cm->items_stride); bound(pk->supply_flags,4,cm->items_stride); bound(pk->ammo_tag,4,cm->items_stride);
  bound(cm->ammo_offset,!strcmp(cm->ammo_kind,"name")?(double)PE.pointer_bytes:4,cm->items_stride);
  if(!strcmp(cm->ammo_kind,"name")) bound(cm->ammo_label,(double)PE.pointer_bytes,cm->items_stride);
  image(cm->items_table,cm->items_count*cm->items_stride,false);
  image(w->time_address,width(w->time_encoding),false); image(pk->time_address,!strcmp(pk->time_storage,"float32-seconds")?4:8,false);
  bound(pk->entity_count,4,ENT); bound(pk->entity_spawnflags,4,ENT); bound(pk->entity_item,(double)PE.pointer_bytes,ENT); bound(pk->entity_inuse,pk->entity_inuse_bytes,ENT);
  if(pk->has_entity_generation) bound(pk->entity_generation,4,ENT);
  if(cm->items_weapon_flag!=pk->supply_weapon_flag||cm->items_classname!=pk->items_classname||cm->items_flags!=pk->supply_flags||in->cursor!=dr->client_cursor||cm->client_weapon!=dr->client_weapon) qa_raise("Error", 0, 0, "Native primary interfaces disagree about original source fields");
  if(cw){
    const qa_compat_q2_classic_combat_profile *b=&cw->base;
    if(b->client.inventory!=in->inventory||b->client.inventory_count!=in->count||b->globals.item_list!=cm->items_table||b->globals.item_bytes!=cm->items_stride||
       cw->inventory_table.count!=cm->items_count||cw->inventory_table.class_name!=cm->items_classname||cw->inventory_table.flags!=cm->items_flags||cw->inventory_table.ammo_flag!=cm->items_ammunition_flag)
      qa_raise("Error", 0, 0, "Classic source world and item interfaces disagree");
    const double world_entries[]={b->entries.damage,b->entries.power_armor,b->entries.regular_armor,b->entries.spawn,b->entries.free};
    for(size_t i=0;i<5;i++) image(world_entries[i],1,true);
    image(b->globals.level_frame,4,false);
    /* Object.entries(world.client) without inventoryCount and userinfoBytes */
    bound(b->client.inventory,4,CLI); bound(b->client.max_grenades,4,CLI); bound(b->client.invincible_frame,4,CLI);
    bound(b->client.userinfo,b->client.userinfo_bytes,CLI); bound(b->client.view_angles,12,CLI);
    for(size_t i=0;i<cw->inventory_table.capacity_count;i++) bound(cw->inventory_table.capacities[i],4,CLI);
  } else if(rw){
    qa_compat_q2_rerelease_layouts_ensure();
    if(rw->client.layout.byte_length!=CLI||rw->client.inventory_count!=in->count||
       qa_compat_q2_rerelease_field_offset(&rw->client.layout,"pers.inventory")!=in->inventory||
       qa_compat_q2_rerelease_field_offset(&rw->client.layout,"pers.weapon")!=cm->client_weapon||
       qa_compat_q2_rerelease_field_offset(&rw->client.layout,"pers.selected_item")!=in->cursor)
      qa_raise("Error", 0, 0, "Rerelease source world and item interfaces disagree");
  }
}
