/* Port of src/content/q3/foundation/assets.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/assets.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

static void *xmalloc(size_t n){ void*p=malloc(n?n:1); if(!p) qa_range_error("out of memory"); return p; }
static char *byte_text(const uint8_t *bytes,size_t n){
  char *t=xmalloc(n+1); for(size_t i=0;i<n;i++) t[i]=(char)bytes[i]; t[n]='\0'; return t;
}
static void component(const char *value,const char *name,bool star){
  const char *plain=(star&&value[0]=='*')?value+1:value;
  if(plain[0]=='\0'||strcmp(plain,".")==0||strcmp(plain,"..")==0||strchr(plain,'/')||strchr(plain,'\\'))
    qa_range_error("Invalid Q3 %s: %s",name,value);
}
typedef struct paths { char **values; size_t count, cap; } paths;
static void push_trunc(paths*p,const char*text,size_t limit){
  char *copy=xmalloc(strlen(text)+1); strcpy(copy,text);
  if(strlen(copy)>limit) copy[limit]='\0'; /* TS slice(0, limit) */
  if(p->count==p->cap){ p->cap=p->cap?p->cap*2:8; p->values=realloc(p->values,p->cap*sizeof *p->values); if(!p->values) qa_range_error("out of memory"); }
  p->values[p->count++]=copy;
}
static void free_paths(paths*p){ for(size_t i=0;i<p->count;i++) free(p->values[i]); free(p->values); }
static bool first_open(qa_q3_character_resources r,paths*p,qa_q3_character_opened_resource*out){
  for(size_t i=0;i<p->count;i++){
    if(r.open(r.ctx,p->values[i],out)){
      if(out->byte_length!=0) return true;
      free(out->bytes); out->bytes=NULL;
    }
  }
  return false;
}
/* TS starts `count` first() lookups together (Promise.all over async functions that await one
   resources.open per path). Each open settles in turn, so the lookups advance one path per round:
   round one opens every lookup's first path in list order, round two the second paths of the lookups
   still searching, and so on. A lookup with fewer paths therefore gives up before an earlier one with
   more. Promise.all does not cancel, so every lookup runs to its end. Returns the lookup that ran out
   of paths first (-1 when all were found); found[i] says whether *out[i] was filled. */
enum { TOGETHER_MAX=4 };
static int open_together(qa_q3_character_resources r,paths*p,qa_q3_character_opened_resource**out,bool*found,int count){
  size_t next[TOGETHER_MAX]={0}; bool done[TOGETHER_MAX]={false}; int first_failed=-1, remaining=count;
  for(int i=0;i<count;i++) found[i]=false;
  while(remaining>0) for(int i=0;i<count;i++){
    if(done[i]) continue;
    if(next[i]>=p[i].count){ done[i]=true; remaining--; if(first_failed<0) first_failed=i; continue; }
    if(r.open(r.ctx,p[i].values[next[i]++],out[i])){
      if(out[i]->byte_length!=0){ found[i]=true; done[i]=true; remaining--; }
      else { free(out[i]->bytes); out[i]->bytes=NULL; }
    }
  }
  return first_failed;
}
/* TS template literals have no length limit; format into owned storage. */
static char *fmt(const char *format,...){
  va_list args; va_start(args,format); int n=vsnprintf(NULL,0,format,args); va_end(args);
  if(n<0) qa_range_error("out of memory");
  char *text=xmalloc((size_t)n+1);
  va_start(args,format); vsnprintf(text,(size_t)n+1,format,args); va_end(args);
  return text;
}
static void push_owned(paths*p,char*text,size_t limit){ push_trunc(p,text,limit); free(text); }
static paths body_files(const qa_q3_character_selection*s,const char*base,const char*team_name){
  paths p={0}; const char *team=s->team?s->team:"default";
  static const char *folders[2]={"","characters/"};
  for(int f=0;f<2;f++) for(int pi=0;pi<(team_name[0]?2:1);pi++){
    const char *prefix=(team_name[0]&&pi==0)?team_name:"";
    push_owned(&p,fmt("models/players/%s%s/%s%s_%s_%s.skin",folders[f],s->model,prefix,base,s->skin,team),63);
    push_owned(&p,fmt("models/players/%s%s/%s%s_%s.skin",folders[f],s->model,prefix,base,s->team?s->team:s->skin),63);
  }
  return p;
}
static paths head_files(const qa_q3_character_selection*s,const char*base,const char*ext,const char*team_name){
  const char *model=s->head_model[0]?s->head_model:s->model;
  const char *name=model[0]=='*'?model+1:model;
  paths p={0}; const char *team=s->team?s->team:"default";
  size_t limit=strcmp(base,"head")==0?63:127;
  int folders_n=model[0]=='*'?1:2;
  for(int f=0;f<folders_n;f++){
    const char *folder=model[0]=='*'?"heads/":(f==0?"":"heads/");
    for(int pi=0;pi<(team_name[0]?2:1);pi++){
      const char *prefix=(team_name[0]&&pi==0)?team_name:"";
      push_owned(&p,fmt("models/players/%s%s/%s/%s%s_%s.%s",folder,name,s->head_skin,prefix,base,team,ext),limit);
      push_owned(&p,fmt("models/players/%s%s/%s%s_%s.%s",folder,name,prefix,base,s->team?s->team:s->head_skin,ext),limit);
    }
  }
  return p;
}
static paths two_paths(char*a,char*b){ paths p={0}; push_owned(&p,a,SIZE_MAX); if(b) push_owned(&p,b,SIZE_MAX); return p; }
static void destroy_part(qa_q3_character_part *p);
static void make_part(qa_q3_character_part *part,qa_q3_character_opened_resource*mesh,qa_q3_character_opened_resource*skin){
  part->resource=mesh->reference; part->skin_resource=skin->reference;
  qa_parse_md3(mesh->bytes,mesh->byte_length,mesh->reference.requested_path,&part->decoded);
  qa_to_scene_md3(&part->decoded.model,&part->scene);
  part->model=part->scene.model;
  char *text=byte_text(skin->bytes,skin->byte_length);
  part->surfaces=qa_parse_skin(text,&part->surface_count); free(text);
  free(mesh->bytes); mesh->bytes=NULL; free(skin->bytes); skin->bytes=NULL;
}
/* Everything the loader owns while a raise can still unwind it (TS: garbage collected). */
typedef struct load_state {
  qa_q3_character_opened_resource lower, upper, face, animation, skins[3];
  paths pending[4]; char *text, *team_names[2], *icon_team; qa_q3_character_assets *out; bool out_live;
} load_state;
static void load_unwind(void *arg){
  load_state *st=arg;
  free(st->lower.bytes); free(st->upper.bytes); free(st->face.bytes); free(st->animation.bytes);
  for(int i=0;i<3;i++) free(st->skins[i].bytes);
  for(int i=0;i<4;i++){ free_paths(&st->pending[i]); st->pending[i]=(paths){0}; }
  free(st->text); st->text=NULL; free(st->team_names[0]); free(st->team_names[1]); free(st->icon_team);
  st->team_names[0]=st->team_names[1]=st->icon_team=NULL;
  if(st->out_live){
    destroy_part(&st->out->lower); destroy_part(&st->out->upper); destroy_part(&st->out->head);
    qa_q3_player_animation_config_destroy(&st->out->animation); st->out_live=false;
  }
}
void qa_q3_load_character(qa_q3_character_resources resources, qa_q3_character_selection selection,
    qa_q3_character_assets *out){
  component(selection.model,"model",false); component(selection.skin,"skin",false);
  if(selection.head_model[0]) component(selection.head_model,"head model",true);
  component(selection.head_skin,"head skin",false);
  if(selection.team_name[0]) component(selection.team_name,"team name",false);
  const char *model=selection.model, *head=selection.head_model[0]?selection.head_model:model;
  const char *head_name=head[0]=='*'?head+1:head;
  load_state st={0}; qa_cleanup node; qa_cleanup_push(&node,load_unwind,&st);
  /* TS: Promise.all of four required() lookups; the first one to run out of paths is the error thrown. */
  st.pending[0]=two_paths(fmt("models/players/%s/lower.md3",model),fmt("models/players/characters/%s/lower.md3",model));
  st.pending[1]=two_paths(fmt("models/players/%s/upper.md3",model),fmt("models/players/characters/%s/upper.md3",model));
  st.pending[2]=head[0]=='*'?two_paths(fmt("models/players/heads/%s/%s.md3",head_name,head_name),NULL)
      :two_paths(fmt("models/players/%s/head.md3",head),fmt("models/players/heads/%s/%s.md3",head_name,head_name));
  st.pending[3]=two_paths(fmt("models/players/%s/animation.cfg",model),fmt("models/players/characters/%s/animation.cfg",model));
  { qa_q3_character_opened_resource *targets[4]={&st.lower,&st.upper,&st.face,&st.animation}; bool found[4];
    int missing=open_together(resources,st.pending,targets,found,4);
    if(missing>=0){
      paths *p=&st.pending[missing];
      size_t total=1; for(size_t i=0;i<p->count;i++) total+=strlen(p->values[i])+2;
      st.text=xmalloc(total); st.text[0]='\0';
      for(size_t i=0;i<p->count;i++){ if(i) strcat(st.text,", "); strcat(st.text,p->values[i]); }
      qa_raise("Error",0,0,"Q3 character resource missing: %s",st.text);
    }
    for(int i=0;i<4;i++){ free_paths(&st.pending[i]); st.pending[i]=(paths){0}; }
  }
  int team_name_count=1;
  if(selection.team_name[0]){
    st.team_names[0]=fmt("%s/",selection.team_name);
    st.team_names[1]=fmt("%s",selection.team&&strcmp(selection.team,"blue")==0?"Pagans/":"Stroggs/");
    team_name_count=2;
  } else st.team_names[0]=fmt("%s","");
  bool have_skins=false;
  for(int t=0;t<team_name_count&&!have_skins;t++){
    st.pending[0]=body_files(&selection,"lower",st.team_names[t]);
    st.pending[1]=body_files(&selection,"upper",st.team_names[t]);
    st.pending[2]=head_files(&selection,"head","skin",st.team_names[t]);
    /* TS: Promise.all of three first() lookups. */
    qa_q3_character_opened_resource *targets[3]={&st.skins[0],&st.skins[1],&st.skins[2]}; bool found[3];
    (void)open_together(resources,st.pending,targets,found,3);
    for(int i=0;i<3;i++){ free_paths(&st.pending[i]); st.pending[i]=(paths){0}; }
    if(found[0]&&found[1]&&found[2]) have_skins=true;
    else for(int i=0;i<3;i++){ free(st.skins[i].bytes); st.skins[i]=(qa_q3_character_opened_resource){0}; }
  }
  free(st.team_names[0]); free(st.team_names[1]); st.team_names[0]=st.team_names[1]=NULL;
  if(!have_skins) qa_raise("Error",0,0,"Q3 character skin missing: %s/%s, %s/%s",model,selection.skin,head,selection.head_skin);
  /* TS awaits the icon lookup before it parses any part. */
  st.icon_team=selection.team_name[0]?fmt("%s/",selection.team_name):fmt("%s","");
  st.pending[0]=head_files(&selection,"icon","skin",st.icon_team);
  st.pending[1]=head_files(&selection,"icon","tga",st.icon_team);
  free(st.icon_team); st.icon_team=NULL;
  for(size_t i=0;i<st.pending[1].count;i++) push_trunc(&st.pending[0],st.pending[1].values[i],SIZE_MAX);
  free_paths(&st.pending[1]); st.pending[1]=(paths){0};
  qa_q3_character_opened_resource icon={0};
  bool has_icon=first_open(resources,&st.pending[0],&icon);
  if(has_icon){ free(icon.bytes); icon.bytes=NULL; }
  free_paths(&st.pending[0]); st.pending[0]=(paths){0};
  *out=(qa_q3_character_assets){0}; out->selection=selection; st.out=out; st.out_live=true;
  make_part(&out->lower,&st.lower,&st.skins[0]); make_part(&out->upper,&st.upper,&st.skins[1]); make_part(&out->head,&st.face,&st.skins[2]);
  out->animation_resource=st.animation.reference;
  st.text=byte_text(st.animation.bytes,st.animation.byte_length);
  qa_q3_parse_player_animation_config(st.text,st.animation.reference.requested_path,NULL,&out->animation);
  free(st.text); st.text=NULL; free(st.animation.bytes); st.animation.bytes=NULL;
  if(has_icon){ out->has_icon=true; out->icon=icon.reference; }
  qa_cleanup_pop(&node);
}
static void destroy_part(qa_q3_character_part *p){
  qa_decoded_scene_md3_destroy(&p->scene); qa_decoded_md3_model_destroy(&p->decoded);
  qa_skin_surfaces_destroy(p->surfaces,p->surface_count); p->surfaces=NULL; p->surface_count=0;
}
void qa_q3_character_assets_destroy(qa_q3_character_assets *a){
  if(!a) return;
  destroy_part(&a->lower); destroy_part(&a->upper); destroy_part(&a->head);
  qa_q3_player_animation_config_destroy(&a->animation);
}
