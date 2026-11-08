#include "history.h"
#include "wire.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Strict protobuf iterator; unlike field lookup it rejects trailing corruption. */
typedef struct {const uint8_t *p;size_t n,pos;} reader;
static int next(reader *r,unsigned *field,unsigned *wire,wa_slice *bytes,uint64_t *value){
 if(r->pos==r->n)return 0;uint64_t key,len;if(!wa_pb_varint(r->p,r->n,&r->pos,&key)||!(key>>3)||key>>3>0x1fffffff)return -1;
 *field=(unsigned)(key>>3);*wire=(unsigned)(key&7);bytes->ptr=NULL;bytes->len=0;*value=0;
 if(*wire==0)return wa_pb_varint(r->p,r->n,&r->pos,value)?1:-1;
 if(*wire==2){if(!wa_pb_varint(r->p,r->n,&r->pos,&len)||len>r->n-r->pos)return -1;bytes->ptr=r->p+r->pos;bytes->len=(size_t)len;r->pos+=(size_t)len;return 1;}
 size_t n=*wire==1?8:*wire==5?4:0;if(!n||n>r->n-r->pos)return -1;r->pos+=n;return 1;
}
static int valid(wa_slice s){reader r={s.ptr,s.len,0};unsigned f,w;wa_slice b;uint64_t v;int k;while((k=next(&r,&f,&w,&b,&v))>0){}return k==0;}
static int bytes(wa_slice s,unsigned f,wa_slice *b){return wa_pb_bytes_field(s.ptr,s.len,f,b);}
static uint64_t number(wa_slice s,unsigned wanted){reader r={s.ptr,s.len,0};unsigned f,w;wa_slice b;uint64_t v;while(next(&r,&f,&w,&b,&v)>0)if(f==wanted&&w==0)return v;return 0;}
static int text(wa_slice b,char *out,size_t cap){if(!b.len||b.len>=cap||memchr(b.ptr,0,b.len))return 0;
 /* Validate UTF-8, including overlongs and surrogates. */
 for(size_t i=0;i<b.len;){unsigned c=b.ptr[i++],z,min;uint32_t v;if(c<128)continue;if(c>=0xc2&&c<=0xdf){z=1;v=c&31;min=128;}else if(c>=0xe0&&c<=0xef){z=2;v=c&15;min=2048;}else if(c>=0xf0&&c<=0xf4){z=3;v=c&7;min=65536;}else return 0;if(z>b.len-i)return 0;while(z--){c=b.ptr[i++];if((c&0xc0)!=0x80)return 0;v=(v<<6)|(c&63);}if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0;}
 memcpy(out,b.ptr,b.len);out[b.len]=0;return 1;}
static int str(wa_slice s,unsigned f,char *out,size_t cap){wa_slice b;return bytes(s,f,&b)&&text(b,out,cap);}
static int address(const char *s){const char *a=strchr(s,'@');if(!a||a==s||a-s>40)return 0;for(const char *p=s;p<a;p++)if((*p<'0'||*p>'9')&&(*p!='-'||strcmp(a,"@g.us")))return 0;return !strcmp(a,"@lid")||!strcmp(a,"@s.whatsapp.net")||!strcmp(a,"@g.us");}
static void key(unsigned slot,char out[32]){snprintf(out,32,"history-task-%u",slot);}
int wa_history_task(const wa_storage *io,unsigned slot,uint8_t *out,size_t cap,size_t *n){if(!io||slot>=WA_HISTORY_TASKS)return -1;char k[32];key(slot,k);return io->get(io->user,k,out,cap,n);}
int wa_history_save(const wa_storage *io,const uint8_t *p,size_t n){
 if(!io||!p||!n||n>WA_HISTORY_NOTIFICATION_MAX||!valid((wa_slice){p,n}))return 0;
 uint8_t *b=malloc(WA_HISTORY_NOTIFICATION_MAX);if(!b)return 0;unsigned free_slot=WA_HISTORY_PATCH_TASKS;int ok=0;
 for(unsigned i=0;i<WA_HISTORY_PATCH_TASKS;i++){size_t z=0;int r=wa_history_task(io,i,b,WA_HISTORY_NOTIFICATION_MAX,&z);if(r<0)goto end;if(r==0){if(free_slot==WA_HISTORY_PATCH_TASKS)free_slot=i;}else if(z==n&&!CRYPTO_memcmp(p,b,n)){ok=1;goto end;}}
 if(free_slot<WA_HISTORY_PATCH_TASKS){char k[32];key(free_slot,k);ok=io->put(io->user,k,p,n);}
 end:OPENSSL_cleanse(b,WA_HISTORY_NOTIFICATION_MAX);free(b);return ok;
}
int wa_history_done(const wa_storage *io,unsigned slot,const uint8_t *p,size_t n){uint8_t *b=malloc(WA_HISTORY_NOTIFICATION_MAX);if(!b)return 0;size_t z=0;int r=wa_history_task(io,slot,b,WA_HISTORY_NOTIFICATION_MAX,&z);int ok=r==0;if(r==1&&z==n&&!CRYPTO_memcmp(p,b,n)){char k[32];key(slot,k);ok=io->put(io->user,k,NULL,0);}OPENSSL_cleanse(b,WA_HISTORY_NOTIFICATION_MAX);free(b);return ok;}
int wa_history_path(const uint8_t *p,size_t n,char *out,size_t cap){wa_slice s={p,n},inline_data;if((bytes(s,113,&inline_data)&&inline_data.len)||(bytes(s,11,&inline_data)&&inline_data.len)||(number(s,101)>=2&&!bytes(s,5,&inline_data))){if(cap)out[0]=0;return 1;}/* directPath is a CDN-relative path, not necessarily the upload /mms/ route.
 * The adapter fixes the HTTPS host and refuses redirects. */
 if(!str(s,5,out,cap)||out[0]!='/'||!out[1]||out[1]=='/')return 0;for(const char *c=out;*c;c++)if((unsigned char)*c<=32||(unsigned char)*c>=127||*c=='\\'||*c=='#')return 0;return 1;}
int wa_history_response(const uint8_t *p,size_t n) {
 wa_slice s={p,n},protocol,response,result,full;
 if(!bytes(s,12,&protocol)||!bytes(protocol,17,&response)||!bytes(response,3,&result)||!bytes(result,6,&full))return -1;
 return (int)number(full,2);
}
/* Authenticated individual-chat state from the regular_high collection. */
#define STATE_COLLECTION "regular_high"
#define STATE_VERSION_KEY "app-state-regular-high"
static const char *collection_name(int low){return low?"regular_low":STATE_COLLECTION;}
static const char *version_key(int low){return low?"app-state-regular-low":STATE_VERSION_KEY;}
#define STATE_EVENTS_MAX 20000
typedef struct {
 char kind[16],id[96],jid[128],text[32];
 uint64_t time;
 int outgoing;
} state_action;
static int put_bytes(unsigned f,const uint8_t *data,size_t n,uint8_t *out,size_t cap,size_t *used) {
 size_t written=0;
 if(*used>cap||!wa_pb_write_bytes(f,data,n,out+*used,cap-*used,&written))return 0;
 *used+=written;return 1;
}
static int put_number(unsigned f,uint64_t value,uint8_t *out,size_t cap,size_t *used) {
 uint64_t values[]={(uint64_t)f<<3,value};
 for(unsigned j=0;j<2;j++)do {
  if(*used==cap)return 0;
  uint8_t c=values[j]&127;values[j]>>=7;
  out[(*used)++]=c|(values[j]?128:0);
 }while(values[j]);
 return 1;
}
static void big64(uint8_t out[8],uint64_t value) {
 for(unsigned i=0;i<8;i++)out[7-i]=(uint8_t)(value>>(i*8));
}
uint64_t wa_state_version(const wa_storage *io,int low) {
 uint8_t bytes8[8];size_t n=0;uint64_t v=0;
 if(!io||io->get(io->user,version_key(low),bytes8,sizeof bytes8,&n)!=1||n!=8)return 0;
 for(unsigned i=0;i<8;i++)v=(v<<8)|bytes8[i];
 return v;
}
int wa_state_rebuild_needed(const wa_storage *io) {
 uint8_t marker=0;size_t n=0;
 return !io||io->get(io->user,"state-low-projection-v1",&marker,1,&n)!=1||n!=1||marker!=1;
}
static int inspect_request(const char *request){return request&&!strcmp(request,WA_STATE_INSPECT_REQUEST);}
static int rebuild_request(const char *request){return request&&(!strcmp(request,WA_STATE_REBUILD_REQUEST)||inspect_request(request));}
static const char *write_base_key(const char *request,int low){
 return inspect_request(request)?"inspect-base-low":rebuild_request(request)?"rebuild-base-low":low?"write-base-low":"write-base-high";
}
static int key_file(wa_slice id,char out[80]) {
 uint8_t digest[32];
 if(!id.len||id.len>64||!SHA256(id.ptr,id.len,digest))return 0;
 memcpy(out,"state-key-",10);
 for(size_t i=0;i<32;i++)snprintf(out+10+i*2,3,"%02x",digest[i]);
 return 1;
}
int wa_state_save_keys(const wa_storage *io,const uint8_t *p,size_t n) {
 reader r={p,n,0};unsigned f,w;wa_slice record,id,data,keydata;uint64_t v;int k;
 while((k=next(&r,&f,&w,&record,&v))>0) {
  if(f!=1||w!=2)continue;
  char name[80];uint8_t existing[32];size_t length=0;
  if(!bytes(record,1,&id)||!bytes(id,1,&id)||!key_file(id,name)
    ||!bytes(record,2,&data)||!bytes(data,1,&keydata)||keydata.len!=32)return 0;
  int found=io->get(io->user,name,existing,sizeof existing,&length);
  int ok=found==0?io->put(io->user,name,keydata.ptr,32):
    found==1&&length==32&&!CRYPTO_memcmp(existing,keydata.ptr,32);
  OPENSSL_cleanse(existing,sizeof existing);
  if(!ok)return 0;
 }
 return k==0;
}
static int expand(const uint8_t *key,size_t n,const char *info,uint8_t *out,size_t wanted) {
 EVP_PKEY_CTX *k=EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF,NULL);
 uint8_t salt[32]={0};size_t size=wanted;
 int ok=k&&EVP_PKEY_derive_init(k)>0&&EVP_PKEY_CTX_set_hkdf_md(k,EVP_sha256())>0
  &&EVP_PKEY_CTX_set1_hkdf_salt(k,salt,32)>0&&EVP_PKEY_CTX_set1_hkdf_key(k,key,(int)n)>0
  &&EVP_PKEY_CTX_add1_hkdf_info(k,(const uint8_t*)info,(int)strlen(info))>0
  &&EVP_PKEY_derive(k,out,&size)>0&&size==wanted;
 EVP_PKEY_CTX_free(k);return ok;
}
static int mutation_keys(const wa_storage *io,wa_slice id,uint8_t keys[160],char missing[129]) {
 char file[80];uint8_t raw[32];size_t n=0;
 if(!key_file(id,file))return 0;
 int found=io->get(io->user,file,raw,sizeof raw,&n);
 if(found==0&&missing)for(size_t i=0;i<id.len;i++)snprintf(missing+2*i,3,"%02x",id.ptr[i]);
 int ok=found==1&&n==32&&expand(raw,32,"WhatsApp Mutation Keys",keys,160);
 OPENSSL_cleanse(raw,sizeof raw);return ok;
}
static int blob_ref(wa_slice ref,uint8_t *task,size_t cap,size_t *used) {
 wa_slice keydata,path,plain_hash={0},encrypted_hash={0};
 bytes(ref,5,&plain_hash);bytes(ref,6,&encrypted_hash);
 return bytes(ref,1,&keydata)&&bytes(ref,2,&path)
  &&(!plain_hash.ptr||put_bytes(1,plain_hash.ptr,plain_hash.len,task,cap,used))
  &&put_number(2,number(ref,4),task,cap,used)
  &&put_bytes(3,keydata.ptr,keydata.len,task,cap,used)
  &&(!encrypted_hash.ptr||put_bytes(4,encrypted_hash.ptr,encrypted_hash.len,task,cap,used))
  &&put_bytes(5,path.ptr,path.len,task,cap,used);
}
static int state_task(const wa_storage *io,const wa_node *node,int snapshot,uint64_t fallback,const char *request,int low) {
 if(!node->data||!node->data_len||node->data_len>WA_HISTORY_NOTIFICATION_MAX-4096)return 0;
 size_t cap=node->data_len+4096,n=0;uint8_t *task=malloc(cap);if(!task)return 0;
 wa_slice raw={node->data,node->data_len},external;
 int ok=put_number(101,snapshot?1:2,task,cap,&n)&&put_number(108,fallback,task,cap,&n)
  &&(!low||put_number(110,1,task,cap,&n));
 if(snapshot) {
  ok=ok&&blob_ref(raw,task,cap,&n);
  if(request)ok=ok&&put_bytes(109,(const uint8_t*)request,strlen(request),task,cap,&n);
 }
 else {
  ok=ok&&put_bytes(103,raw.ptr,raw.len,task,cap,&n);
  if(bytes(raw,3,&external))ok=ok&&blob_ref(external,task,cap,&n);
 }
 if(ok&&snapshot){
  /* Reserve one recovery slot per collection. Old patches cannot block it. */
  char name[32];key(WA_HISTORY_PATCH_TASKS+(unsigned)low,name);
  ok=io->put(io->user,name,task,n);
 }else if(ok)ok=wa_history_save(io,task,n);
 OPENSSL_cleanse(task,cap);free(task);return ok;
}
static int write_state_task(const wa_storage *io,const wa_node *snapshot,const wa_node *collection,const char *request,int low,int more){
 uint8_t *task=malloc(WA_HISTORY_NOTIFICATION_MAX);if(!task)return 0;size_t n=0;
 int ok=put_number(101,1,task,WA_HISTORY_NOTIFICATION_MAX,&n)
  &&put_number(110,low!=0,task,WA_HISTORY_NOTIFICATION_MAX,&n)
  &&put_number(112,more!=0,task,WA_HISTORY_NOTIFICATION_MAX,&n)
  &&put_bytes(109,(const uint8_t*)request,strlen(request),task,WA_HISTORY_NOTIFICATION_MAX,&n)
  ;
 if(ok&&snapshot)ok=blob_ref((wa_slice){snapshot->data,snapshot->data_len},task,WA_HISTORY_NOTIFICATION_MAX,&n);
 else if(ok){
  uint8_t *base=malloc(WA_HISTORY_NOTIFICATION_MAX);size_t size=0;
  ok=base&&io->get(io->user,write_base_key(request,low),base,WA_HISTORY_NOTIFICATION_MAX,&size)==1
   &&put_bytes(113,base,size,task,WA_HISTORY_NOTIFICATION_MAX,&n);
  if(base){OPENSSL_cleanse(base,WA_HISTORY_NOTIFICATION_MAX);free(base);}
 }
 const wa_node *patches=wa_node_child(collection,"patches");
 for(const wa_node *p=patches?patches->child:collection->child;p&&ok;p=p->next)
  if(!strcmp(p->tag,"patch"))ok=put_bytes(111,p->data,p->data_len,task,WA_HISTORY_NOTIFICATION_MAX,&n);
 if(ok){char name[32];key(rebuild_request(request)?WA_HISTORY_TASKS-1:WA_HISTORY_PATCH_TASKS+2+(unsigned)low,name);ok=io->put(io->user,name,task,n);}
 OPENSSL_cleanse(task,WA_HISTORY_NOTIFICATION_MAX);free(task);return ok;
}
int wa_state_collect(const wa_storage *io,const wa_node *result,const char *request,int low) {
 int more=0,found=0;
 wa_node *sync=wa_node_child(result,"sync");
 for(wa_node *collection=sync?sync->child:NULL;collection;collection=collection->next) {
  const char *name=wa_node_get(collection,"name");
  if(strcmp(collection->tag,"collection")||!name||strcmp(name,collection_name(low)))continue;
  const char *collection_type=wa_node_get(collection,"type");
  if(wa_node_child(collection,"error")||(collection_type&&!strcmp(collection_type,"error")))return 0;
  found=1;
  const char *has_more=wa_node_get(collection,"has_more_patches");
  if(has_more&&!strcmp(has_more,"true"))more=1;
  const char *version=wa_node_get(collection,"version");
  uint64_t fallback=version?strtoull(version,NULL,10):0;
  wa_node *snapshot=wa_node_child(collection,"snapshot");
  if(request)return write_state_task(io,snapshot,collection,request,low,more)?1:0;
  if(snapshot&&!state_task(io,snapshot,1,0,request,low))return 0;
  wa_node *patches=wa_node_child(collection,"patches");
  unsigned queued=0;
  for(wa_node *p=patches?patches->child:collection->child;p;p=p->next){
   if(strcmp(p->tag,"patch"))continue;
   ++fallback;
   wa_slice raw={p->data,p->data_len},version_field;
   uint64_t patch_version=bytes(raw,1,&version_field)?number(version_field,1):fallback;
   /* Recovery snapshots may be older than the durable receive cursor. Do not
    * refill the bounded queue with already committed deltas from that page. */
   if(patch_version&&patch_version<=wa_state_version(io,low))continue;
   /* A partial page is backpressure, not a broken state requiring a snapshot.
    * Once these deltas commit, the messenger asks for the remaining versions. */
   if(!state_task(io,p,0,fallback,NULL,low))return snapshot||queued?2:0;
   queued++;
  }
 }
 return found?1+more:0;
}
static wa_tree *state_query(uint64_t v,const char *id,int snapshot,int low) {
 wa_tree *t=wa_tree_create();if(!t)return NULL;
 wa_node *root=wa_node_add(t,NULL,"iq"),*sync=root?wa_node_add(t,root,"sync"):NULL;
 wa_node *collection=sync?wa_node_add(t,sync,"collection"):NULL;
 /* Nodes borrow attribute strings; the returned tree must own this number.
  * A stack buffer here expires before the adapter serializes the query. */
 char *version=(char*)t->arena+t->used;
 size_t available=WA_NODE_BYTES-t->used;
 int length=snprintf(version,available,"%llu",(unsigned long long)v);
 if(length<0||(size_t)length>=available){wa_tree_free(t);return NULL;}
 t->used+=(size_t)length+1;
 int ok=collection&&wa_node_attr(root,"to","s.whatsapp.net")&&wa_node_attr(root,"id",id)
  &&wa_node_attr(root,"type","set")&&wa_node_attr(root,"xmlns","w:sync:app:state")
  &&wa_node_attr(collection,"name",collection_name(low))&&wa_node_attr(collection,"version",version)
  &&wa_node_attr(collection,"return_snapshot",snapshot?"true":"false");
 if(!ok){wa_tree_free(t);return NULL;}return t;
}
wa_tree *wa_state_query_after(uint64_t version,const char *id,int low){return state_query(version,id,0,low);}
wa_tree *wa_state_query(const wa_storage *io,const char *id,int force_snapshot,int low) {
 uint64_t v=force_snapshot?0:wa_state_version(io,low);
 return state_query(v,id,force_snapshot||!v,low);
}
int wa_history_save_mutation(const wa_storage *io,const char *kind,const char *peer,const char *target,
 int outgoing,uint64_t time,const char *body) {
 uint8_t task[WA_TEXT_MAX+512];size_t n=0;
 int ok=put_number(101,3,task,sizeof task,&n)
  &&put_bytes(102,(const uint8_t*)peer,strlen(peer),task,sizeof task,&n)
  &&put_bytes(103,(const uint8_t*)target,strlen(target),task,sizeof task,&n)
  &&put_bytes(104,(const uint8_t*)body,strlen(body),task,sizeof task,&n)
  &&put_number(105,outgoing!=0,task,sizeof task,&n)&&put_number(106,time,task,sizeof task,&n)
  &&put_number(107,!strcmp(kind,"delete")?1:2,task,sizeof task,&n)
  &&wa_history_save(io,task,n);
 OPENSSL_cleanse(task,sizeof task);return ok;
}
struct wa_history {
 uint8_t *plain;size_t size;reader root,conversations;wa_slice chat;char jid[128];unsigned chat_stage;
 state_action *actions;size_t action_count,action_capacity,action_position;
 uint64_t state_version;
 int state_job,low,write_more;
 char delete_request[48];
 uint8_t snapshot_hash[128];
};
static int index_parts(wa_slice index,char parts[4][128]) {
 /* Supported indices use unescaped ASCII strings. Other records are still
  * authenticated and included in the integrity calculation. */
 size_t p=0;
 if(!index.len||index.ptr[p++]!='[')return 0;
 for(unsigned i=0;i<4;i++) {
  if(p==index.len||index.ptr[p++]!='"')return i>=2;
  size_t n=0;
  while(p<index.len&&index.ptr[p]!='"') {
   unsigned c=index.ptr[p++];
   if(c<=32||c>=127||c=='\\'||n>=127)return 0;
   parts[i][n++]=(char)c;
  }
  if(p==index.len)return 0;
  parts[i][n]=0;p++;
  if(p<index.len&&index.ptr[p]==']')return i>=1;
  if(p==index.len||index.ptr[p++]!=',')return 0;
 }
 return 1;
}
static int add_action(wa_history *h,wa_slice index,wa_slice action) {
 char parts[4][128]={{0}};wa_slice operation,range;
 if(!index_parts(index,parts)||!address(parts[1]))return 1;
 uint64_t time=number(action,1);
 if(!time||time>253402300799000ULL)return 1;
 state_action record={0};record.time=time;
 strcpy(record.jid,parts[1]);
 if(h->low&&!strcmp(parts[0],"archive")&&bytes(action,17,&operation)) {
  strcpy(record.kind,"archive");record.outgoing=number(operation,1)!=0;
 }else if(h->low&&!strcmp(parts[0],"pin_v1")&&bytes(action,5,&operation)) {
  strcpy(record.kind,"pin");record.outgoing=number(operation,1)!=0;
 }else if(h->low)return 1;
 else if(!strcmp(parts[0],"deleteMessageForMe")&&bytes(action,18,&operation)) {
  if(!parts[2][0]||strlen(parts[2])>=sizeof record.id||(strcmp(parts[3],"0")&&strcmp(parts[3],"1")))return 1;
  strcpy(record.kind,"delete");strcpy(record.id,parts[2]);record.outgoing=!strcmp(parts[3],"1");
 }else if((!strcmp(parts[0],"deleteChat")&&bytes(action,22,&operation))
       ||(!strcmp(parts[0],"clearChat")&&!strcmp(parts[2],"1")&&bytes(action,21,&operation))) {
  uint64_t cutoff=bytes(operation,1,&range)?number(range,1):0;
  if(cutoff>253402300799ULL)return 1;
  strcpy(record.kind,!strcmp(parts[0],"deleteChat")?"delete-chat":"clear-chat");
  snprintf(record.text,sizeof record.text,"%llu",(unsigned long long)(cutoff?cutoff*1000+999:time));
 }else return 1;
 if(h->action_count==h->action_capacity) {
  size_t cap=h->action_capacity?h->action_capacity*2:32;
  if(cap>STATE_EVENTS_MAX)cap=STATE_EVENTS_MAX;
  if(cap<=h->action_count)return 0;
  state_action *actions=realloc(h->actions,cap*sizeof *actions);
  if(!actions)return 0;
  h->actions=actions;h->action_capacity=cap;
 }
 h->actions[h->action_count++]=record;return 1;
}
static int verify_record(wa_history *h,const wa_storage *io,wa_slice record,unsigned op,
 HMAC_CTX *patch,uint8_t hash[128],char missing[129],const char **stage) {
 *stage="record-envelope-format";
 wa_slice index,value,id;
 if(!valid(record))return 0;
 *stage="record-index-format";
 if(!bytes(record,1,&index)||!bytes(index,1,&index))return 0;
 *stage="record-index-size";
 if(index.len!=32)return 0;
 *stage="record-value-format";
 if(!bytes(record,2,&value)||!bytes(value,1,&value))return 0;
 *stage="record-value-size";
 /* The enclosing download is already bounded. Real snapshot records need
  * not fit the former arbitrary 64 KiB per-record cap. */
 if(value.len<64||value.len>WA_HISTORY_DOWNLOAD_MAX)return 0;
 *stage="record-value-alignment";
 if((value.len-48)%16)return 0;
 *stage="record-keyid-format";
 if(!bytes(record,3,&id)||!bytes(id,1,&id))return 0;
 uint8_t keys[160],mac[64],last[8]={0};unsigned mn=0;int ok=0;
 uint8_t *plain=NULL;size_t size=value.len-32;
 *stage="record-key";
 if(!mutation_keys(io,id,keys,missing))goto done;
 *stage="record-value-mac";
 uint8_t marker=(uint8_t)(op+1);last[7]=(uint8_t)(id.len+1);
 HMAC_CTX *hm=HMAC_CTX_new();
 ok=hm&&HMAC_Init_ex(hm,keys+64,32,EVP_sha512(),NULL)
  &&HMAC_Update(hm,&marker,1)&&HMAC_Update(hm,id.ptr,id.len)
  &&HMAC_Update(hm,value.ptr,size)&&HMAC_Update(hm,last,8)
  &&HMAC_Final(hm,mac,&mn)&&!CRYPTO_memcmp(mac,value.ptr+size,32);
 HMAC_CTX_free(hm);
 if(!ok)goto done;
 *stage="record-integrity";
 if(patch&&!HMAC_Update(patch,value.ptr+size,32)){ok=0;goto done;}
 if(hash){
  uint8_t contribution[128];
  if(!expand(value.ptr+size,32,"WhatsApp Patch Integrity",contribution,128)){ok=0;goto done;}
  for(unsigned i=0;i<128;i+=2){
   unsigned v=hash[i]+(hash[i+1]<<8)+contribution[i]+(contribution[i+1]<<8);
   hash[i]=(uint8_t)v;hash[i+1]=(uint8_t)(v>>8);
  }
 }
 *stage="record-memory";
 plain=malloc(size);if(!plain){ok=0;goto done;}
 *stage="record-decrypt";
 EVP_CIPHER_CTX *aes=EVP_CIPHER_CTX_new();int a=0,b=0;
 ok=aes&&EVP_DecryptInit_ex(aes,EVP_aes_256_cbc(),NULL,keys+32,value.ptr)>0
  &&EVP_DecryptUpdate(aes,plain,&a,value.ptr+16,(int)(size-16))>0
  &&EVP_DecryptFinal_ex(aes,plain+a,&b)>0;
 EVP_CIPHER_CTX_free(aes);
 if(ok){
  *stage="record-data-format";
  wa_slice data={plain,(size_t)(a+b)},idx,action={0};
  ok=valid(data);
  if(ok){*stage="record-index-missing";ok=bytes(data,1,&idx);}
  /* SyncActionData.value is optional on REMOVE tombstones. Authenticate the
   * index as usual, but never turn a metadata removal into a chat deletion. */
  if(ok){
   int present=bytes(data,2,&action);
   *stage=present?"record-action-format":"record-action-missing";
   ok=present?valid(action):op==1;
  }
  if(ok){
   *stage="record-index-mac";
   ok=HMAC(EVP_sha256(),keys,32,idx.ptr,idx.len,mac,&mn)&&!CRYPTO_memcmp(mac,index.ptr,32);
  }
  /* REMOVE drops sync metadata. Only authenticated SET applies a deletion. */
  if(ok&&!op){*stage="record-action-store";ok=add_action(h,idx,action);}
 }
 done:
 if(plain){OPENSSL_cleanse(plain,size);free(plain);}
 OPENSSL_cleanse(keys,sizeof keys);return ok;
}
static int state_records(wa_history *h,const wa_storage *io,wa_slice data,unsigned field,
 int snapshot,HMAC_CTX *patch,uint8_t hash[128],char missing[129],const char **stage) {
 reader r={data.ptr,data.len,0};unsigned f,w;wa_slice record;uint64_t value;int k;
 while((k=next(&r,&f,&w,&record,&value))>0) {
  if(f!=field||w!=2)continue;
  *stage="mutation-format";
  uint64_t op=0;
  if(!snapshot){op=number(record,1);if(op>1||!bytes(record,2,&record))return 0;}
  if(!verify_record(h,io,record,(unsigned)op,patch,hash,missing,stage))return 0;
 }
 if(k<0)*stage="mutation-list-format";
 return k==0;
}
static int state_open_checked(wa_history *h,const wa_storage *io,wa_slice task,wa_slice external,char missing[129],const char **stage) {
 *stage="state-format";
 uint64_t kind=number(task,101);
 h->state_job=(int)kind;
 if(kind==3){
  h->plain=malloc(task.len);if(!h->plain)return 0;
  memcpy(h->plain,task.ptr,task.len);h->size=task.len;return 1;
 }
 if(!io||kind<1||kind>2)return 0;
 int snapshot=kind==1;
 h->low=number(task,110)==1;
 const char *collection=collection_name(h->low);
 wa_slice raw=external,version,keyid,expected,snapshot_mac;
 *stage="state-payload-format";
 if(!snapshot&&!bytes(task,103,&raw))return 0;
 if(!valid(raw))return 0;
 *stage="state-version";
 uint64_t v=bytes(raw,1,&version)?number(version,1):number(task,108);
 uint64_t old=wa_state_version(io,h->low);
 if(!v||v>UINT32_MAX)return 0;
 int requested=snapshot&&str(task,109,h->delete_request,sizeof h->delete_request);
 if(v<=old&&!requested){h->state_version=old;return 1;}
 if(!snapshot&&v!=old+1){*stage=h->low?"version-gap-low":"version-gap-high";return 0;}
 *stage="state-auth-format";
 if(!bytes(raw,snapshot?4:6,&keyid)||!bytes(keyid,1,&keyid)
   ||!bytes(raw,snapshot?3:5,&expected)||expected.len!=32)return 0;
 *stage="state-key";
 uint8_t keys[160],hash[128]={0},stamp[8],actual[32];unsigned n=0;
 int ok=mutation_keys(io,keyid,keys,missing);
 HMAC_CTX *hm=HMAC_CTX_new();big64(stamp,v);
 if(snapshot){
  ok=ok&&hm&&state_records(h,io,raw,2,1,NULL,hash,missing,stage)
   &&HMAC_Init_ex(hm,keys+96,32,EVP_sha256(),NULL)&&HMAC_Update(hm,hash,sizeof hash);
 }else{
  /* Authenticate the complete ordered mutation list and contiguous version. */
  if(ok)*stage="patch-auth-format";
  ok=ok&&hm&&bytes(raw,4,&snapshot_mac)&&snapshot_mac.len==32
   &&HMAC_Init_ex(hm,keys+128,32,EVP_sha256(),NULL)&&HMAC_Update(hm,snapshot_mac.ptr,32)
   &&state_records(h,io,raw,2,0,hm,NULL,missing,stage)
   &&(!external.len||state_records(h,io,external,1,0,hm,NULL,missing,stage));
 }
 if(ok)*stage=snapshot?"snapshot-mac":"patch-mac";
 ok=ok&&HMAC_Update(hm,stamp,sizeof stamp)
  &&HMAC_Update(hm,(const uint8_t*)collection,strlen(collection))
  &&HMAC_Final(hm,actual,&n)&&!CRYPTO_memcmp(actual,expected.ptr,32);
 HMAC_CTX_free(hm);OPENSSL_cleanse(keys,sizeof keys);
 if(ok&&requested){
  *stage="state-memory";
  h->plain=malloc(raw.len);if(!h->plain)return 0;
  memcpy(h->plain,raw.ptr,raw.len);h->size=raw.len;
  memcpy(h->snapshot_hash,hash,sizeof hash);
  if(v<=old)h->action_count=0;
 }
 if(ok)h->state_version=v;
 return ok;
}
/* A delete is built only from a freshly authenticated snapshot. Never advance
 * the receive cursor on a send; the server's next sync supplies that version. */
const char *wa_history_delete_request(const wa_history *h){return h?h->delete_request:"";}
static int hash_mac(uint8_t hash[128],const uint8_t value[32],int subtract){
 uint8_t contribution[128];
 if(!expand(value,32,"WhatsApp Patch Integrity",contribution,sizeof contribution))return 0;
 for(unsigned i=0;i<128;i+=2){
  unsigned before=hash[i]+(hash[i+1]<<8),delta=contribution[i]+(contribution[i+1]<<8);
  unsigned result=subtract?before-delta:before+delta;
  hash[i]=(uint8_t)result;hash[i+1]=(uint8_t)(result>>8);
 }
 return 1;
}
/* Reconstruct a write base from the server snapshot plus all authenticated
 * deltas returned with it. The receive cursor is never moved by this replay. */

/* Structural metadata only: never serialize values, key IDs or payload bytes. */
void wa_history_write_diagnostic(const uint8_t *p,size_t n,char *out,size_t cap){
 reader r={p,n,0};unsigned f,w;uint64_t v;wa_slice patch;int k;size_t count=0;
 if(cap)out[0]=0;
 while((k=next(&r,&f,&w,&patch,&v))>0){
  if(f!=111||w!=2)continue;count++;
  wa_slice id={0},key={0},sm={0},pm={0},ext={0},ver={0};
  int format=valid(patch),has_id=bytes(patch,6,&id),has_key=has_id&&bytes(id,1,&key);
  int has_sm=bytes(patch,4,&sm),has_pm=bytes(patch,5,&pm),has_ext=bytes(patch,3,&ext),has_ver=bytes(patch,1,&ver);
  if(!format||(has_ext&&ext.len)||!has_key||!has_sm||sm.len!=32||!has_pm||pm.len!=32){
   snprintf(out,cap,"patch_index=%zu patch_bytes=%zu protobuf=%d key_wrapper=%d key_value=%d key_bytes=%zu snapshot_mac_present=%d snapshot_mac_bytes=%zu patch_mac_present=%d patch_mac_bytes=%zu external_present=%d external_bytes=%zu version_present=%d",
    count,patch.len,format,has_id,has_key,key.len,has_sm,sm.len,has_pm,pm.len,has_ext,ext.len,has_ver);return;
  }
 }
 snprintf(out,cap,"patches=%zu structure=%s",count,k<0?"invalid-task":"valid");
}

static int replay_write_snapshot(wa_history *h,const wa_storage *io,wa_slice task,char missing[129],const char **stage){
 h->write_more=number(task,112)!=0;
 wa_slice *records=calloc(STATE_EVENTS_MAX,sizeof *records);if(!records){*stage="write-memory";return 0;}
 wa_slice snapshot={h->plain,h->size},keyid,version_blob;
 size_t count=0;reader initial={snapshot.ptr,snapshot.len,0};unsigned f,w;uint64_t value;wa_slice record;int k,ok=0;
 if(!bytes(snapshot,4,&keyid))goto done;
 while((k=next(&initial,&f,&w,&record,&value))>0)if(f==2&&w==2){
  if(count==STATE_EVENTS_MAX)goto done;records[count++]=record;
 }
 if(k<0)goto done;
 uint8_t hash[128];memcpy(hash,h->snapshot_hash,sizeof hash);
 reader patches={task.ptr,task.len,0};wa_slice patch;
 while((k=next(&patches,&f,&w,&patch,&value))>0){
  if(f!=111||w!=2)continue;
  wa_slice patch_key,key,snapshot_mac,expected,external;
  *stage="write-patch-protobuf";if(!valid(patch))goto done;
  *stage="write-external-mutations";if(bytes(patch,3,&external)&&external.len)goto done;
  *stage="write-keyid-wrapper";if(!bytes(patch,6,&patch_key))goto done;
  *stage="write-keyid-value";if(!bytes(patch_key,1,&key))goto done;
  *stage="write-snapshot-mac-missing";if(!bytes(patch,4,&snapshot_mac))goto done;
  *stage="write-snapshot-mac-size";if(snapshot_mac.len!=32)goto done;
  *stage="write-patch-mac-missing";if(!bytes(patch,5,&expected))goto done;
  *stage="write-patch-mac-size";if(expected.len!=32)goto done;
  uint64_t version=bytes(patch,1,&version_blob)?number(version_blob,1):h->state_version+1;
  if(version!=h->state_version+1){*stage="write-version-gap";goto done;}
  uint8_t keys[160],actual[32],stamp[8];unsigned mn=0;big64(stamp,version);
  *stage="write-patch-key";
  if(!mutation_keys(io,key,keys,missing))goto done;
  HMAC_CTX *hm=HMAC_CTX_new();wa_history *checked=calloc(1,sizeof *checked);
  int verified=hm&&checked&&HMAC_Init_ex(hm,keys+128,32,EVP_sha256(),NULL)
   &&HMAC_Update(hm,snapshot_mac.ptr,32)
   &&state_records(checked,io,patch,2,0,hm,NULL,missing,stage)
   &&HMAC_Update(hm,stamp,8)
   &&HMAC_Update(hm,(const uint8_t*)collection_name(h->low),strlen(collection_name(h->low)))
   &&HMAC_Final(hm,actual,&mn)&&!CRYPTO_memcmp(actual,expected.ptr,32);
  HMAC_CTX_free(hm);wa_history_free(checked);
  if(!verified){OPENSSL_cleanse(keys,sizeof keys);goto done;}
  reader mutations={patch.ptr,patch.len,0};wa_slice mutation;
  *stage="write-map";
  while((k=next(&mutations,&f,&w,&mutation,&value))>0){
   if(f!=2||w!=2)continue;
   wa_slice incoming,index,old_index;
   unsigned op=(unsigned)number(mutation,1);
   if(!bytes(mutation,2,&incoming)||!bytes(incoming,1,&index)||!bytes(index,1,&index))break;
   size_t pos=0;
   for(;pos<count;pos++)if(bytes(records[pos],1,&old_index)&&bytes(old_index,1,&old_index)
     &&old_index.len==index.len&&!CRYPTO_memcmp(old_index.ptr,index.ptr,index.len))break;
   if(op){if(pos<count)records[pos]=records[--count];}
   else if(pos<count)records[pos]=incoming;
   else if(count<STATE_EVENTS_MAX)records[count++]=incoming;
   else break;
  }
  if(k!=0){OPENSSL_cleanse(keys,sizeof keys);goto done;}
  memset(hash,0,sizeof hash);
  for(size_t i=0;i<count;i++){
   wa_slice encrypted;
   if(!bytes(records[i],2,&encrypted)||!bytes(encrypted,1,&encrypted)||encrypted.len<32
     ||!hash_mac(hash,encrypted.ptr+encrypted.len-32,0)){k=-1;break;}
  }
  *stage="write-snapshot-mac";
  hm=HMAC_CTX_new();verified=k==0&&hm&&HMAC_Init_ex(hm,keys+96,32,EVP_sha256(),NULL)
   &&HMAC_Update(hm,hash,sizeof hash)&&HMAC_Update(hm,stamp,8)
   &&HMAC_Update(hm,(const uint8_t*)collection_name(h->low),strlen(collection_name(h->low)))
   &&HMAC_Final(hm,actual,&mn)&&!CRYPTO_memcmp(actual,snapshot_mac.ptr,32);
  HMAC_CTX_free(hm);OPENSSL_cleanse(keys,sizeof keys);if(!verified)goto done;
  h->state_version=version;keyid=patch_key;
 }
 if(k<0)goto done;
 /* Materialize the reconstructed record map for replacement-MAC calculation. */
 size_t capacity=256;
 for(size_t i=0;i<count;i++){if(capacity>WA_HISTORY_PLAIN_MAX-16||records[i].len>WA_HISTORY_PLAIN_MAX-capacity-16)goto done;capacity+=records[i].len+16;}
 uint8_t *plain=malloc(capacity),version[16];size_t n=0,vn=0;
 if(!plain)goto done;
 int built=put_number(1,h->state_version,version,sizeof version,&vn)
  &&put_bytes(1,version,vn,plain,capacity,&n);
 for(size_t i=0;i<count&&built;i++)built=put_bytes(2,records[i].ptr,records[i].len,plain,capacity,&n);
 wa_slice id;uint8_t seal_keys[160],seal[32],stamp[8];unsigned seal_n=0;
 big64(stamp,h->state_version);HMAC_CTX *seal_ctx=HMAC_CTX_new();
 built=built&&bytes(keyid,1,&id)&&mutation_keys(io,id,seal_keys,missing)&&seal_ctx
  &&HMAC_Init_ex(seal_ctx,seal_keys+96,32,EVP_sha256(),NULL)
  &&HMAC_Update(seal_ctx,hash,sizeof hash)&&HMAC_Update(seal_ctx,stamp,8)
  &&HMAC_Update(seal_ctx,(const uint8_t*)collection_name(h->low),strlen(collection_name(h->low)))
  &&HMAC_Final(seal_ctx,seal,&seal_n)
  &&put_bytes(3,seal,32,plain,capacity,&n)&&put_bytes(4,keyid.ptr,keyid.len,plain,capacity,&n);
 HMAC_CTX_free(seal_ctx);OPENSSL_cleanse(seal_keys,sizeof seal_keys);
 if(!built){free(plain);goto done;}
 OPENSSL_cleanse(h->plain,h->size);free(h->plain);h->plain=plain;h->size=n;
 memcpy(h->snapshot_hash,hash,sizeof hash);h->action_count=0;h->action_position=0;ok=1;
 if(rebuild_request(h->delete_request)&&!h->write_more){
  *stage="rebuild-stale-version";
  if(h->state_version<wa_state_version(io,1)){ok=0;goto done;}
  // Import only the final authenticated record map, never intermediate pages.
  *stage="rebuild-records";
  ok=state_records(h,io,(wa_slice){h->plain,h->size},2,1,NULL,NULL,missing,stage);
 }
 done:free(records);return ok;
}
int wa_history_write_more(const wa_history *h){return h&&h->write_more;}
int wa_history_save_write_base(const wa_history *h,const wa_storage *io){
 return h&&h->delete_request[0]&&h->size<=WA_HISTORY_NOTIFICATION_MAX
  &&io->put(io->user,write_base_key(h->delete_request,h->low),h->plain,h->size);
}
static int replace_snapshot_value(wa_slice snapshot,const uint8_t index_mac[32],uint8_t hash[128]){
 reader r={snapshot.ptr,snapshot.len,0};unsigned f,w;uint64_t v;wa_slice record,index,value;int k;
 while((k=next(&r,&f,&w,&record,&v))>0){
  if(f!=2||w!=2||!bytes(record,1,&index)||!bytes(index,1,&index)||index.len!=32||
     CRYPTO_memcmp(index.ptr,index_mac,32))continue;
  return bytes(record,2,&value)&&bytes(value,1,&value)&&value.len>=32&&
   hash_mac(hash,value.ptr+value.len-32,1);
 }
 return k==0;
}
wa_tree *wa_history_delete_patch(const wa_history *h,const wa_storage *io,const char *iq_id,
 const char *peer,const char *anchor,int own,uint64_t time,uint64_t changed,int archive){
 int pin=archive>=3;
 if(!h||!h->delete_request[0]||!address(peer)||(!pin&&(!anchor||!*anchor||strlen(anchor)>=96||!time))||
    !changed||h->state_version<wa_state_version(io,h->low)||h->state_version>=UINT32_MAX)return NULL;
 wa_slice snapshot={h->plain,h->size},key_id,key;
 if(!bytes(snapshot,4,&key_id)||!bytes(key_id,1,&key))return NULL;
 const char *collection_name_value=collection_name(h->low);
 uint8_t keys[160],message_key[256],message[300],range[340],operation[360],action[400],plain[580];
 size_t kn=0,mn=0,rn=0,on=0,an=0,pn=0;
 char index[170];int il=snprintf(index,sizeof index,pin?"[\"pin_v1\",\"%s\"]":h->low?"[\"archive\",\"%s\"]":"[\"deleteChat\",\"%s\",\"1\"]",peer);
 int ok=il>0&&(size_t)il<sizeof index&&
  (pin|| (put_bytes(1,(const uint8_t*)peer,strlen(peer),message_key,sizeof message_key,&kn)&&
  put_number(2,own!=0,message_key,sizeof message_key,&kn)&&
  put_bytes(3,(const uint8_t*)anchor,strlen(anchor),message_key,sizeof message_key,&kn)&&
  put_bytes(1,message_key,kn,message,sizeof message,&mn)&&put_number(2,time/1000,message,sizeof message,&mn)&&
  put_number(1,time/1000,range,sizeof range,&rn)&&put_bytes(3,message,mn,range,sizeof range,&rn)&&
  (!h->low||put_number(1,archive!=0,operation,sizeof operation,&on))&&
  put_bytes(h->low?2:1,range,rn,operation,sizeof operation,&on)))&&
  (!pin||put_number(1,archive==3,operation,sizeof operation,&on))&&put_number(1,changed,action,sizeof action,&an)&&
  put_bytes(pin?5:h->low?17:22,operation,on,action,sizeof action,&an)&&
  put_bytes(1,(const uint8_t*)index,(size_t)il,plain,sizeof plain,&pn)&&
  put_bytes(2,action,an,plain,sizeof plain,&pn)&&put_number(4,pin?5:h->low?3:6,plain,sizeof plain,&pn)&&
  mutation_keys(io,key,keys,NULL);
 uint8_t value[sizeof plain+64],mac_input[sizeof value+73],mac[64],index_mac[32];
 unsigned mac_len=0;int a=0,b=0;
 EVP_CIPHER_CTX *aes=EVP_CIPHER_CTX_new();
 ok=ok&&aes&&RAND_bytes(value,16)==1&&EVP_EncryptInit_ex(aes,EVP_aes_256_cbc(),NULL,keys+32,value)>0&&
  EVP_EncryptUpdate(aes,value+16,&a,plain,(int)pn)>0&&EVP_EncryptFinal_ex(aes,value+16+a,&b)>0;
 EVP_CIPHER_CTX_free(aes);
 wa_tree *tree=NULL;
 if(ok){
  size_t vn=16+(size_t)(a+b),input=0;
  mac_input[input++]=1;memcpy(mac_input+input,key.ptr,key.len);input+=key.len;
  memcpy(mac_input+input,value,vn);input+=vn;big64(mac_input+input,key.len+1);input+=8;
  uint8_t hash[128],snapshot_input[160],snapshot_mac[32],patch_input[96],patch_mac[32],stamp[8];
  size_t name_len=strlen(collection_name_value);
  memcpy(hash,h->snapshot_hash,sizeof hash);big64(stamp,h->state_version+1);
  ok=HMAC(EVP_sha512(),keys+64,32,mac_input,input,mac,&mac_len)&&
   HMAC(EVP_sha256(),keys,32,(const uint8_t*)index,(size_t)il,index_mac,&mac_len)&&
   replace_snapshot_value(snapshot,index_mac,hash)&&hash_mac(hash,mac,0);
  if(ok){
   memcpy(value+vn,mac,32);vn+=32;
   memcpy(snapshot_input,hash,128);memcpy(snapshot_input+128,stamp,8);
   memcpy(snapshot_input+136,collection_name_value,name_len);
   ok=HMAC(EVP_sha256(),keys+96,32,snapshot_input,136+name_len,snapshot_mac,&mac_len)!=NULL;
   if(ok){
    memcpy(patch_input,snapshot_mac,32);memcpy(patch_input+32,mac,32);memcpy(patch_input+64,stamp,8);
    memcpy(patch_input+72,collection_name_value,name_len);
    ok=HMAC(EVP_sha256(),keys+128,32,patch_input,72+name_len,patch_mac,&mac_len)!=NULL;
   }
  }
  uint8_t ix[40],val[sizeof value+8],record[900],mutation[920],patch[1100];
  size_t ixn=0,valn=0,recn=0,mun=0,patchn=0;
  ok=ok&&put_bytes(1,index_mac,32,ix,sizeof ix,&ixn)&&put_bytes(1,value,vn,val,sizeof val,&valn)&&
   put_bytes(1,ix,ixn,record,sizeof record,&recn)&&put_bytes(2,val,valn,record,sizeof record,&recn)&&
   put_bytes(3,key_id.ptr,key_id.len,record,sizeof record,&recn)&&put_number(1,0,mutation,sizeof mutation,&mun)&&
   put_bytes(2,record,recn,mutation,sizeof mutation,&mun)&&put_bytes(2,mutation,mun,patch,sizeof patch,&patchn)&&
   put_bytes(4,snapshot_mac,32,patch,sizeof patch,&patchn)&&put_bytes(5,patch_mac,32,patch,sizeof patch,&patchn)&&
   put_bytes(6,key_id.ptr,key_id.len,patch,sizeof patch,&patchn);
  if(ok){
   tree=state_query(h->state_version,iq_id,0,h->low);
   wa_node *collection=tree?wa_node_child(wa_node_child(tree->root,"sync"),"collection"):NULL;
   wa_node *node=collection?wa_node_add(tree,collection,"patch"):NULL;
   if(node&&patchn<=WA_NODE_BYTES-tree->used){
    node->data=tree->arena+tree->used;node->data_len=patchn;
    memcpy(tree->arena+tree->used,patch,patchn);tree->used+=patchn;
   }else{wa_tree_free(tree);tree=NULL;}
  }
 }
 OPENSSL_cleanse(keys,sizeof keys);OPENSSL_cleanse(plain,sizeof plain);return tree;
}
static int inflate_data(const uint8_t *p,size_t n,uint8_t **out,size_t *size){
 z_stream z={0};if(inflateInit(&z)!=Z_OK)return 0;size_t cap=65536;uint8_t *b=malloc(cap);if(!b){inflateEnd(&z);return 0;}z.next_in=(Bytef*)p;z.avail_in=(uInt)n;int r=Z_OK;
 while(r==Z_OK){if(z.total_out==cap){if(cap==WA_HISTORY_PLAIN_MAX){r=Z_MEM_ERROR;break;}cap*=2;uint8_t *grown=realloc(b,cap);if(!grown){r=Z_MEM_ERROR;break;}b=grown;}
 z.next_out=b+z.total_out;z.avail_out=(uInt)(cap-z.total_out);r=inflate(&z,Z_NO_FLUSH);}
 int ok=r==Z_STREAM_END&&z.avail_in==0&&z.total_out>0;*size=(size_t)z.total_out;inflateEnd(&z);if(!ok){OPENSSL_cleanse(b,cap);free(b);return 0;}*out=b;return 1;
}
/* Shared authenticated media decoder for snapshots and external mutations. */
static uint8_t *decode_media(wa_slice s,const uint8_t *enc,size_t en,int kind,size_t *size,const char **stage){
 uint8_t *compressed=NULL;size_t cn=0;
 *stage="media-format";
  wa_slice mk,eh={0},ph={0};uint8_t hash[32],expanded[112],mac[32];size_t expanded_n=sizeof expanded;unsigned mn=0;
  bytes(s,4,&eh);bytes(s,1,&ph);
  if(!enc||en<26||en>WA_HISTORY_DOWNLOAD_MAX||(en-10)%16||!bytes(s,3,&mk)||mk.len!=32||
     (!kind&&(!eh.ptr||!ph.ptr))||(ph.ptr&&ph.len!=32)||
     (eh.ptr&&(eh.len!=32||!SHA256(enc,en,hash)||CRYPTO_memcmp(hash,eh.ptr,32))))return NULL;
  EVP_PKEY_CTX *k=EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF,NULL);uint8_t salt[32]={0};const char *info=kind?"WhatsApp App State Keys":"WhatsApp History Keys";
  int ok=k&&EVP_PKEY_derive_init(k)>0&&EVP_PKEY_CTX_set_hkdf_md(k,EVP_sha256())>0&&EVP_PKEY_CTX_set1_hkdf_salt(k,salt,32)>0&&EVP_PKEY_CTX_set1_hkdf_key(k,mk.ptr,32)>0&&EVP_PKEY_CTX_add1_hkdf_info(k,(const uint8_t*)info,(int)strlen(info))>0&&EVP_PKEY_derive(k,expanded,&expanded_n)>0;EVP_PKEY_CTX_free(k);
  *stage="media-mac";
  HMAC_CTX *hm=HMAC_CTX_new();ok=ok&&hm&&HMAC_Init_ex(hm,expanded+48,32,EVP_sha256(),NULL)&&HMAC_Update(hm,expanded,16)&&HMAC_Update(hm,enc,en-10)&&HMAC_Final(hm,mac,&mn)&&!CRYPTO_memcmp(mac,enc+en-10,10);HMAC_CTX_free(hm);
  if(!ok){OPENSSL_cleanse(expanded,sizeof expanded);return NULL;}
  *stage="media-decode";
  compressed=malloc(en);if(!compressed){OPENSSL_cleanse(expanded,sizeof expanded);return NULL;}
  EVP_CIPHER_CTX *aes=EVP_CIPHER_CTX_new();int a=0,b=0;ok=aes&&EVP_DecryptInit_ex(aes,EVP_aes_256_cbc(),NULL,expanded+16,expanded)>0&&EVP_DecryptUpdate(aes,compressed,&a,enc,(int)(en-10))>0&&EVP_DecryptFinal_ex(aes,compressed+a,&b)>0;EVP_CIPHER_CTX_free(aes);OPENSSL_cleanse(expanded,sizeof expanded);cn=(size_t)(a+b);
  uint64_t length=number(s,2);ok=ok&&(!length||length==cn)&&(!ph.ptr||(SHA256(compressed,cn,hash)&&!CRYPTO_memcmp(hash,ph.ptr,32)));if(!ok){OPENSSL_cleanse(compressed,en);free(compressed);return NULL;}
 *size=cn;return compressed;
}

/* Return the first external mutation reference in a write replay.
 * The adapter downloads bytes; all decryption and authentication stays here. */
int wa_history_external_task(const uint8_t *p,size_t n,uint8_t *out,size_t cap,size_t *used){
 wa_slice task={p,n},patch,ref;reader r={p,n,0};unsigned f,w;uint64_t v;int k;
 *used=0;
 if(!p||!n||n>WA_HISTORY_NOTIFICATION_MAX||!valid(task))return -1;
 while((k=next(&r,&f,&w,&patch,&v))>0){
  if(f!=111||w!=2)continue;
  if(!valid(patch))return -1;
  if(bytes(patch,3,&ref)&&ref.len){
   char path[4096];
   if(!blob_ref(ref,out,cap,used)||!wa_history_path(out,*used,path,sizeof path)||!path[0])return -1;
   return 1;
  }
 }
 return k<0?-1:0;
}
/* Inline one authenticated SyncdMutations list into its original patch.
 * The original task remains durable until the complete replay is verified.
 * Patch/value/snapshot MACs are checked by the normal replay afterwards. */
uint8_t *wa_history_hydrate(const uint8_t *p,size_t n,const uint8_t *enc,size_t en,size_t *out_n){
 uint8_t descriptor[4096];size_t dn=0,plain_n=0;const char *stage;
 *out_n=0;
 if(wa_history_external_task(p,n,descriptor,sizeof descriptor,&dn)!=1)return NULL;
 uint8_t *plain=decode_media((wa_slice){descriptor,dn},enc,en,1,&plain_n,&stage);
 if(!plain)return NULL;
 uint8_t *out=malloc(WA_HISTORY_NOTIFICATION_MAX),*expanded=malloc(WA_HISTORY_NOTIFICATION_MAX);
 int ok=out&&expanded&&valid((wa_slice){plain,plain_n}),replaced=0;
 reader r={p,n,0};unsigned f,w;uint64_t v;wa_slice patch,ref;int k=0;size_t previous_field=0;
 while(ok&&(k=next(&r,&f,&w,&patch,&v))>0){
  if(!replaced&&f==111&&w==2&&bytes(patch,3,&ref)&&ref.len){
   size_t pn=0;reader pr={patch.ptr,patch.len,0};unsigned pf,pw;uint64_t pv;wa_slice pb;int pk=0;size_t previous=0;
   while(ok&&(pk=next(&pr,&pf,&pw,&pb,&pv))>0){
    if(pf!=3) {
     size_t len=pr.pos-previous;
     if(len>WA_HISTORY_NOTIFICATION_MAX-pn)ok=0;
     else {memcpy(expanded+pn,patch.ptr+previous,len);pn+=len;}
    }
    previous=pr.pos;
   }
   if(pk<0)ok=0;
   reader mr={plain,plain_n,0};unsigned mf,mw;uint64_t mv;wa_slice mutation;int mk=0;
   while(ok&&(mk=next(&mr,&mf,&mw,&mutation,&mv))>0){
    if(mf==1){if(mw!=2||!valid(mutation))ok=0;
     else ok=put_bytes(2,mutation.ptr,mutation.len,expanded,WA_HISTORY_NOTIFICATION_MAX,&pn);}
   }
   if(mk<0)ok=0;
   ok=ok&&put_bytes(111,expanded,pn,out,WA_HISTORY_NOTIFICATION_MAX,out_n);replaced=1;
  }else{
   size_t len=r.pos-previous_field;
   if(len>WA_HISTORY_NOTIFICATION_MAX-*out_n)ok=0;
   else {memcpy(out+*out_n,p+previous_field,len);*out_n+=len;}
  }
  previous_field=r.pos;
 }
 ok=ok&&k==0&&replaced;
 OPENSSL_cleanse(plain,plain_n);free(plain);
 if(expanded){OPENSSL_cleanse(expanded,WA_HISTORY_NOTIFICATION_MAX);free(expanded);}
 if(!ok){if(out){OPENSSL_cleanse(out,WA_HISTORY_NOTIFICATION_MAX);free(out);}return NULL;}
 return out;
}
wa_history *wa_history_open_diagnostic(const wa_storage *io,const uint8_t *notification,size_t n,const uint8_t *enc,size_t en,char missing[129],const char **stage){
 *stage="task-format";
 if(!notification||!n||n>WA_HISTORY_NOTIFICATION_MAX||!valid((wa_slice){notification,n}))return NULL;
 wa_slice s={notification,n},v;uint8_t *compressed=NULL;size_t cn=0;int allocated=0;
 uint64_t kind=number(s,101);
 if(missing)missing[0]=0;
 if(kind==1&&bytes(s,113,&v)){
  wa_history *h=calloc(1,sizeof *h);
  if(!h||!state_open_checked(h,io,s,v,missing,stage)||!replay_write_snapshot(h,io,s,missing,stage)){wa_history_free(h);return NULL;}
  return h;
 }
 if(kind==3||(kind==2&&!bytes(s,5,&v))){
  wa_history *h=calloc(1,sizeof *h);
  if(!h||!state_open_checked(h,io,s,(wa_slice){NULL,0},missing,stage)){wa_history_free(h);return NULL;}
  return h;
 }
 if(!kind&&bytes(s,11,&v)&&v.len){compressed=(uint8_t*)v.ptr;cn=v.len;}
 else {
  *stage="media-format";
  compressed=decode_media(s,enc,en,(int)kind,&cn,stage);
  if(!compressed)return NULL;allocated=1;
 }
 wa_history *h=calloc(1,sizeof *h);
 if(!kind)*stage="history-zlib";
 int ok=h&&(kind?state_open_checked(h,io,s,(wa_slice){compressed,cn},missing,stage):inflate_data(compressed,cn,&h->plain,&h->size));
 if(ok&&h->delete_request[0])ok=replay_write_snapshot(h,io,s,missing,stage);
 if(allocated){OPENSSL_cleanse(compressed,en);free(compressed);}
 if(!ok||(!kind&&!valid((wa_slice){h->plain,h->size}))){wa_history_free(h);return NULL;}
 h->root=(reader){h->plain,h->size,0};return h;
}
wa_history *wa_history_open_sync(const wa_storage *io,const uint8_t *p,size_t n,const uint8_t *enc,size_t en,char missing[129]){
 const char *stage;
 return wa_history_open_diagnostic(io,p,n,enc,en,missing,&stage);
}
wa_history *wa_history_open(const uint8_t *p,size_t n,const uint8_t *enc,size_t en) {
 return wa_history_open_sync(NULL,p,n,enc,en,NULL);
}
uint64_t wa_history_version(const wa_history *h){return h?h->state_version:0;}
int wa_history_job_kind(const uint8_t *p,size_t n){return (int)number((wa_slice){p,n},101);}
uint64_t wa_history_job_order(const uint8_t *p,size_t n){
 wa_slice task={p,n},patch,version;
 if(bytes(task,103,&patch)&&bytes(patch,1,&version))return number(version,1);
 return number(task,108);
}
int wa_history_commit(const wa_storage *io,unsigned slot,const uint8_t *p,size_t n,uint64_t version){
 int kind=wa_history_job_kind(p,n),low=number((wa_slice){p,n},110)==1;
 wa_slice write_request;
 int write_base=bytes((wa_slice){p,n},109,&write_request);
 int rebuild=low&&write_base&&write_request.len==strlen(WA_STATE_REBUILD_REQUEST)&&!memcmp(write_request.ptr,WA_STATE_REBUILD_REQUEST,write_request.len);
 int completed=rebuild&&!number((wa_slice){p,n},112);
 if(completed&&version<wa_state_version(io,1))return 0;
 /* A write-base replay authenticates metadata, but does not import actions
  * into the database. Only normal incoming sync may advance its cursor. */
 if((!write_base||completed)&&(kind==1||kind==2)&&(version>wa_state_version(io,low)||completed)){
  uint8_t *stored=malloc(n);if(!stored)return 0;
  size_t size=0;int matches=wa_history_task(io,slot,stored,n,&size)==1&&size==n&&!CRYPTO_memcmp(p,stored,n);
  OPENSSL_cleanse(stored,n);free(stored);if(!matches)return 0;
  uint8_t data[8];big64(data,version);
  if(!io->put(io->user,version_key(low),data,sizeof data))return 0;
  if(completed){uint8_t marker=1;if(!io->put(io->user,"state-low-projection-v1",&marker,1))return 0;}
 }
 return wa_history_done(io,slot,p,n);
}
static int alias(wa_slice s,unsigned lf,unsigned pf,wa_message_event *e){char lid[128],pn[128];if(!str(s,lf,lid,sizeof lid)||!str(s,pf,pn,sizeof pn)||!address(lid)||!address(pn)||!strstr(lid,"@lid")||!strstr(pn,"@s.whatsapp.net"))return 0;strcpy(e->kind,"alias");strcpy(e->id,lid);strcpy(e->jid,pn);return 1;}
static int message(wa_slice record,const char *chat,wa_slice meta,wa_message_event *e,uint64_t *time){wa_slice web,key,msg,inner;if(!bytes(record,1,&web)||!valid(web)||!bytes(web,1,&key)||!valid(key)||!bytes(web,2,&msg)||!valid(msg))return 0;
 char remote[128];if(!str(key,1,remote,sizeof remote)||!str(key,3,e->id,sizeof e->id))return 0;
 if(strcmp(remote,chat)){char alternate[128];int matches=(str(meta,39,alternate,sizeof alternate)&&!strcmp(remote,alternate))||(str(meta,42,alternate,sizeof alternate)&&!strcmp(remote,alternate));if(!matches)return 0;}
 wa_slice t;if(!bytes(msg,1,&t)&&(!bytes(msg,6,&inner)||!bytes(inner,1,&t)))return 0;
 if(!text(t,e->text,sizeof e->text))return 0;uint64_t sec=number(web,3);if(!sec||sec>253402300799ULL)return 0;*time=sec*1000;
 if(strstr(chat,"@g.us"))str(key,4,e->author,sizeof e->author);
 int own=number(key,2)!=0;strcpy(e->kind,own?"sent-copy":"message");strcpy(e->jid,chat);uint64_t status=number(web,4);e->state=own?(status>=2&&status<=5?(status>4?4:(int)status):-2):0;return 1;
}
int wa_history_next(wa_history *h,wa_message_event *e,uint64_t *time){if(!h||!e||!time)return -1;
 if(h->state_job){
  memset(e,0,sizeof *e);
  if(h->state_job==3){
   if(h->action_position++)return 0;
   wa_slice task={h->plain,h->size};
   if(!str(task,102,e->jid,sizeof e->jid)||!address(e->jid)||!str(task,103,e->id,sizeof e->id))return -1;
   uint64_t kind=number(task,107);
   if(kind==1)strcpy(e->kind,"delete");
   else if(kind==2&&str(task,104,e->text,sizeof e->text))strcpy(e->kind,"edit");
   else return -1;
   e->state=number(task,105)!=0;*time=number(task,106);return 1;
  }
  if(h->action_position==h->action_count)return 0;
  const state_action *a=&h->actions[h->action_position++];
  strcpy(e->kind,a->kind);strcpy(e->id,a->id);strcpy(e->jid,a->jid);strcpy(e->text,a->text);
  *time=a->time;e->state=a->outgoing;return 1;
 }
 for(;;){memset(e,0,sizeof *e);*time=0;
  if(h->chat.ptr){
   if(h->chat_stage==0){h->chat_stage++;
    char pn[128],lid[128];if(strstr(h->jid,"@lid")&&str(h->chat,39,pn,sizeof pn)&&address(pn)&&strstr(pn,"@s.whatsapp.net")){strcpy(e->kind,"alias");strcpy(e->id,h->jid);strcpy(e->jid,pn);return 1;}
    if(strstr(h->jid,"@s.whatsapp.net")&&str(h->chat,42,lid,sizeof lid)&&address(lid)&&strstr(lid,"@lid")){strcpy(e->kind,"alias");strcpy(e->id,lid);strcpy(e->jid,h->jid);return 1;}
   }
   if(h->chat_stage==1){h->chat_stage++;strcpy(e->kind,"chat");strcpy(e->jid,h->jid);if(!str(h->chat,38,e->text,sizeof e->text))str(h->chat,13,e->text,sizeof e->text);uint64_t sec=number(h->chat,12);if(!sec)sec=number(h->chat,5);if(sec<=253402300799ULL)*time=sec*1000;return 1;}
   if(h->chat_stage==2){h->chat_stage++;
    unsigned f,w;wa_slice b;uint64_t v;reader fields={h->chat.ptr,h->chat.len,0};int k;
    while((k=next(&fields,&f,&w,&b,&v))>0)if(f==16&&w==0){strcpy(e->kind,"history-archive");strcpy(e->jid,h->jid);e->state=v!=0;return 1;}
    if(k<0)return -1;
   }
   if(h->chat_stage==3){h->chat_stage++;
    unsigned f,w;wa_slice b;uint64_t v;reader fields={h->chat.ptr,h->chat.len,0};int k;
    while((k=next(&fields,&f,&w,&b,&v))>0)if(f==24&&w==0){strcpy(e->kind,"history-pin");strcpy(e->jid,h->jid);snprintf(e->text,sizeof e->text,"%llu",(unsigned long long)v);return 1;}
    if(k<0)return -1;
   }
   unsigned f,w;wa_slice b;uint64_t v;int k;
   while((k=next(&h->conversations,&f,&w,&b,&v))>0)if(f==2&&w==2&&message(b,h->jid,h->chat,e,time))return 1;
   if(k<0)return -1;h->chat.ptr=NULL;
  }
  unsigned f,w;wa_slice b;uint64_t v;int k=next(&h->root,&f,&w,&b,&v);if(k<=0)return k;
  if(w!=2)continue;
  if(f==15){if(!valid(b))return -1;if(alias(b,2,1,e))return 1;}
  else if(f==7){if(!valid(b))return -1;if(str(b,1,e->jid,sizeof e->jid)&&address(e->jid)&&str(b,2,e->text,sizeof e->text)){strcpy(e->kind,"name");return 1;}}
  else if(f==2){if(!valid(b))return -1;if(str(b,1,h->jid,sizeof h->jid)&&address(h->jid)){h->chat=b;h->chat_stage=0;h->conversations=(reader){b.ptr,b.len,0};}}
 }
}
void wa_history_free(wa_history *h){if(h){if(h->plain){OPENSSL_cleanse(h->plain,h->size);free(h->plain);}if(h->actions){OPENSSL_cleanse(h->actions,h->action_capacity*sizeof *h->actions);free(h->actions);}OPENSSL_cleanse(h,sizeof *h);free(h);}}
