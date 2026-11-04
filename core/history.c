#include "history.h"
#include "wire.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
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
static int address(const char *s){const char *a=strchr(s,'@');if(!a||a==s||a-s>40)return 0;for(const char *p=s;p<a;p++)if(*p<'0'||*p>'9')return 0;return !strcmp(a,"@lid")||!strcmp(a,"@s.whatsapp.net");}
static void key(unsigned slot,char out[32]){snprintf(out,32,"history-task-%u",slot);}
int wa_history_task(const wa_storage *io,unsigned slot,uint8_t *out,size_t cap,size_t *n){if(!io||slot>=WA_HISTORY_TASKS)return -1;char k[32];key(slot,k);return io->get(io->user,k,out,cap,n);}
int wa_history_save(const wa_storage *io,const uint8_t *p,size_t n){
 if(!io||!p||!n||n>WA_HISTORY_NOTIFICATION_MAX||!valid((wa_slice){p,n}))return 0;
 uint8_t *b=malloc(WA_HISTORY_NOTIFICATION_MAX);if(!b)return 0;unsigned free_slot=WA_HISTORY_TASKS;int ok=0;
 for(unsigned i=0;i<WA_HISTORY_TASKS;i++){size_t z=0;int r=wa_history_task(io,i,b,WA_HISTORY_NOTIFICATION_MAX,&z);if(r<0)goto end;if(r==0){if(free_slot==WA_HISTORY_TASKS)free_slot=i;}else if(z==n&&!CRYPTO_memcmp(p,b,n)){ok=1;goto end;}}
 if(free_slot<WA_HISTORY_TASKS){char k[32];key(free_slot,k);ok=io->put(io->user,k,p,n);}
 end:OPENSSL_cleanse(b,WA_HISTORY_NOTIFICATION_MAX);free(b);return ok;
}
int wa_history_done(const wa_storage *io,unsigned slot,const uint8_t *p,size_t n){uint8_t *b=malloc(WA_HISTORY_NOTIFICATION_MAX);if(!b)return 0;size_t z=0;int r=wa_history_task(io,slot,b,WA_HISTORY_NOTIFICATION_MAX,&z);int ok=r==0;if(r==1&&z==n&&!CRYPTO_memcmp(p,b,n)){char k[32];key(slot,k);ok=io->put(io->user,k,NULL,0);}OPENSSL_cleanse(b,WA_HISTORY_NOTIFICATION_MAX);free(b);return ok;}
int wa_history_path(const uint8_t *p,size_t n,char *out,size_t cap){wa_slice s={p,n},inline_data;if((bytes(s,11,&inline_data)&&inline_data.len)||(number(s,101)>=2&&!bytes(s,5,&inline_data))){if(cap)out[0]=0;return 1;}/* directPath is a CDN-relative path, not necessarily the upload /mms/ route.
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
uint64_t wa_state_version(const wa_storage *io) {
 uint8_t bytes8[8];size_t n=0;uint64_t v=0;
 if(!io||io->get(io->user,STATE_VERSION_KEY,bytes8,sizeof bytes8,&n)!=1||n!=8)return 0;
 for(unsigned i=0;i<8;i++)v=(v<<8)|bytes8[i];
 return v;
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
 wa_slice keydata,path,plain_hash,encrypted_hash;
 return bytes(ref,1,&keydata)&&bytes(ref,2,&path)&&bytes(ref,5,&plain_hash)&&bytes(ref,6,&encrypted_hash)
  &&put_bytes(1,plain_hash.ptr,plain_hash.len,task,cap,used)
  &&put_number(2,number(ref,4),task,cap,used)
  &&put_bytes(3,keydata.ptr,keydata.len,task,cap,used)
  &&put_bytes(4,encrypted_hash.ptr,encrypted_hash.len,task,cap,used)
  &&put_bytes(5,path.ptr,path.len,task,cap,used);
}
static int state_task(const wa_storage *io,const wa_node *node,int snapshot,uint64_t fallback) {
 if(!node->data||!node->data_len||node->data_len>WA_HISTORY_NOTIFICATION_MAX-4096)return 0;
 size_t cap=node->data_len+4096,n=0;uint8_t *task=malloc(cap);if(!task)return 0;
 wa_slice raw={node->data,node->data_len},external;
 int ok=put_number(101,snapshot?1:2,task,cap,&n)&&put_number(108,fallback,task,cap,&n);
 if(snapshot)ok=ok&&blob_ref(raw,task,cap,&n);
 else {
  ok=ok&&put_bytes(103,raw.ptr,raw.len,task,cap,&n);
  if(bytes(raw,3,&external))ok=ok&&blob_ref(external,task,cap,&n);
 }
 if(ok)ok=wa_history_save(io,task,n);
 OPENSSL_cleanse(task,cap);free(task);return ok;
}
int wa_state_collect(const wa_storage *io,const wa_node *result) {
 wa_node *sync=wa_node_child(result,"sync");
 for(wa_node *collection=sync?sync->child:NULL;collection;collection=collection->next) {
  const char *name=wa_node_get(collection,"name");
  if(strcmp(collection->tag,"collection")||!name||strcmp(name,STATE_COLLECTION))continue;
  const char *version=wa_node_get(collection,"version");
  uint64_t fallback=version?strtoull(version,NULL,10):0;
  wa_node *snapshot=wa_node_child(collection,"snapshot");
  if(snapshot&&!state_task(io,snapshot,1,0))return 0;
  wa_node *patches=wa_node_child(collection,"patches");
  for(wa_node *p=patches?patches->child:collection->child;p;p=p->next)
   if(!strcmp(p->tag,"patch")&&!state_task(io,p,0,++fallback))return 0;
 }
 return 1;
}
wa_tree *wa_state_query(const wa_storage *io,const char *id) {
 wa_tree *t=wa_tree_create();if(!t)return NULL;
 wa_node *root=wa_node_add(t,NULL,"iq"),*sync=root?wa_node_add(t,root,"sync"):NULL;
 wa_node *collection=sync?wa_node_add(t,sync,"collection"):NULL;
 uint64_t v=wa_state_version(io);char version[32];snprintf(version,sizeof version,"%llu",(unsigned long long)v);
 int ok=collection&&wa_node_attr(root,"to","@s.whatsapp.net")&&wa_node_attr(root,"id",id)
  &&wa_node_attr(root,"type","set")&&wa_node_attr(root,"xmlns","w:sync:app:state")
  &&wa_node_attr(collection,"name",STATE_COLLECTION)&&wa_node_attr(collection,"version",version)
  &&wa_node_attr(collection,"return_snapshot",v?"false":"true");
 if(!ok){wa_tree_free(t);return NULL;}return t;
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
 int state_job;
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
 if(!strcmp(parts[0],"deleteMessageForMe")&&bytes(action,18,&operation)) {
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
 HMAC_CTX *patch,uint8_t hash[128],char missing[129]) {
 wa_slice index,value,id;
 if(!bytes(record,1,&index)||!bytes(index,1,&index)||index.len!=32
   ||!bytes(record,2,&value)||!bytes(value,1,&value)||value.len<64
   ||value.len>65536||(value.len-48)%16||!bytes(record,3,&id)||!bytes(id,1,&id))return 0;
 uint8_t keys[160],mac[64],last[8]={0};unsigned mn=0;int ok=0;
 uint8_t *plain=NULL;size_t size=value.len-32;
 if(!mutation_keys(io,id,keys,missing))goto done;
 uint8_t marker=(uint8_t)(op+1);last[7]=(uint8_t)(id.len+1);
 HMAC_CTX *hm=HMAC_CTX_new();
 ok=hm&&HMAC_Init_ex(hm,keys+64,32,EVP_sha512(),NULL)
  &&HMAC_Update(hm,&marker,1)&&HMAC_Update(hm,id.ptr,id.len)
  &&HMAC_Update(hm,value.ptr,size)&&HMAC_Update(hm,last,8)
  &&HMAC_Final(hm,mac,&mn)&&!CRYPTO_memcmp(mac,value.ptr+size,32);
 HMAC_CTX_free(hm);
 if(!ok)goto done;
 if(patch&&!HMAC_Update(patch,value.ptr+size,32)){ok=0;goto done;}
 if(hash){
  uint8_t contribution[128];
  if(!expand(value.ptr+size,32,"WhatsApp Patch Integrity",contribution,128)){ok=0;goto done;}
  for(unsigned i=0;i<128;i+=2){
   unsigned v=hash[i]+(hash[i+1]<<8)+contribution[i]+(contribution[i+1]<<8);
   hash[i]=(uint8_t)v;hash[i+1]=(uint8_t)(v>>8);
  }
 }
 plain=malloc(size);if(!plain){ok=0;goto done;}
 EVP_CIPHER_CTX *aes=EVP_CIPHER_CTX_new();int a=0,b=0;
 ok=aes&&EVP_DecryptInit_ex(aes,EVP_aes_256_cbc(),NULL,keys+32,value.ptr)>0
  &&EVP_DecryptUpdate(aes,plain,&a,value.ptr+16,(int)(size-16))>0
  &&EVP_DecryptFinal_ex(aes,plain+a,&b)>0;
 EVP_CIPHER_CTX_free(aes);
 if(ok){
  wa_slice data={plain,(size_t)(a+b)},idx,action;
  ok=valid(data)&&bytes(data,1,&idx)&&bytes(data,2,&action)&&valid(action)
    &&HMAC(EVP_sha256(),keys,32,idx.ptr,idx.len,mac,&mn)&&!CRYPTO_memcmp(mac,index.ptr,32);
  /* REMOVE drops sync metadata. Only authenticated SET applies a deletion. */
  if(ok&&!op)ok=add_action(h,idx,action);
 }
 done:
 if(plain){OPENSSL_cleanse(plain,size);free(plain);}
 OPENSSL_cleanse(keys,sizeof keys);return ok;
}
static int state_records(wa_history *h,const wa_storage *io,wa_slice data,unsigned field,
 int snapshot,HMAC_CTX *patch,uint8_t hash[128],char missing[129]) {
 reader r={data.ptr,data.len,0};unsigned f,w;wa_slice record;uint64_t value;int k;
 while((k=next(&r,&f,&w,&record,&value))>0) {
  if(f!=field||w!=2)continue;
  uint64_t op=0;
  if(!snapshot){op=number(record,1);if(op>1||!bytes(record,2,&record))return 0;}
  if(!verify_record(h,io,record,(unsigned)op,patch,hash,missing))return 0;
 }
 return k==0;
}
static int state_open(wa_history *h,const wa_storage *io,wa_slice task,wa_slice external,char missing[129]) {
 uint64_t kind=number(task,101);
 h->state_job=(int)kind;
 if(kind==3){
  h->plain=malloc(task.len);if(!h->plain)return 0;
  memcpy(h->plain,task.ptr,task.len);h->size=task.len;return 1;
 }
 if(!io||kind<1||kind>2)return 0;
 int snapshot=kind==1;
 wa_slice raw=external,version,keyid,expected,snapshot_mac;
 if(!snapshot&&!bytes(task,103,&raw))return 0;
 if(!valid(raw))return 0;
 uint64_t v=bytes(raw,1,&version)?number(version,1):number(task,108);
 uint64_t old=wa_state_version(io);
 if(!v||v>UINT32_MAX)return 0;
 if(v<=old){h->state_version=old;return 1;}
 if(!snapshot&&v!=old+1)return 0;
 if(!bytes(raw,snapshot?4:6,&keyid)||!bytes(keyid,1,&keyid)
   ||!bytes(raw,snapshot?3:5,&expected)||expected.len!=32)return 0;
 uint8_t keys[160],hash[128]={0},stamp[8],actual[32];unsigned n=0;
 int ok=mutation_keys(io,keyid,keys,missing);
 HMAC_CTX *hm=HMAC_CTX_new();big64(stamp,v);
 if(snapshot){
  ok=ok&&hm&&state_records(h,io,raw,2,1,NULL,hash,missing)
   &&HMAC_Init_ex(hm,keys+96,32,EVP_sha256(),NULL)&&HMAC_Update(hm,hash,sizeof hash);
 }else{
  /* Authenticate the complete ordered mutation list and contiguous version. */
  ok=ok&&hm&&bytes(raw,4,&snapshot_mac)&&snapshot_mac.len==32
   &&HMAC_Init_ex(hm,keys+128,32,EVP_sha256(),NULL)&&HMAC_Update(hm,snapshot_mac.ptr,32)
   &&state_records(h,io,raw,2,0,hm,NULL,missing)
   &&(!external.len||state_records(h,io,external,1,0,hm,NULL,missing));
 }
 ok=ok&&HMAC_Update(hm,stamp,sizeof stamp)
  &&HMAC_Update(hm,(const uint8_t*)STATE_COLLECTION,strlen(STATE_COLLECTION))
  &&HMAC_Final(hm,actual,&n)&&!CRYPTO_memcmp(actual,expected.ptr,32);
 HMAC_CTX_free(hm);OPENSSL_cleanse(keys,sizeof keys);
 if(ok)h->state_version=v;
 return ok;
}
static int inflate_data(const uint8_t *p,size_t n,uint8_t **out,size_t *size){
 z_stream z={0};if(inflateInit(&z)!=Z_OK)return 0;size_t cap=65536;uint8_t *b=malloc(cap);if(!b){inflateEnd(&z);return 0;}z.next_in=(Bytef*)p;z.avail_in=(uInt)n;int r=Z_OK;
 while(r==Z_OK){if(z.total_out==cap){if(cap==WA_HISTORY_PLAIN_MAX){r=Z_MEM_ERROR;break;}cap*=2;uint8_t *grown=realloc(b,cap);if(!grown){r=Z_MEM_ERROR;break;}b=grown;}
 z.next_out=b+z.total_out;z.avail_out=(uInt)(cap-z.total_out);r=inflate(&z,Z_NO_FLUSH);}
 int ok=r==Z_STREAM_END&&z.avail_in==0&&z.total_out>0;*size=(size_t)z.total_out;inflateEnd(&z);if(!ok){OPENSSL_cleanse(b,cap);free(b);return 0;}*out=b;return 1;
}
wa_history *wa_history_open_sync(const wa_storage *io,const uint8_t *notification,size_t n,const uint8_t *enc,size_t en,char missing[129]){
 if(!notification||!n||n>WA_HISTORY_NOTIFICATION_MAX||!valid((wa_slice){notification,n}))return NULL;
 wa_slice s={notification,n},v;uint8_t *compressed=NULL;size_t cn=0;int allocated=0;
 uint64_t kind=number(s,101);
 if(missing)missing[0]=0;
 if(kind==3||(kind==2&&!bytes(s,5,&v))){
  wa_history *h=calloc(1,sizeof *h);
  if(!h||!state_open(h,io,s,(wa_slice){NULL,0},missing)){wa_history_free(h);return NULL;}
  return h;
 }
 if(!kind&&bytes(s,11,&v)&&v.len){compressed=(uint8_t*)v.ptr;cn=v.len;}
 else {
  wa_slice mk,eh,ph;uint8_t hash[32],expanded[112],mac[32];size_t expanded_n=sizeof expanded;unsigned mn=0;
  if(!enc||en<26||en>WA_HISTORY_DOWNLOAD_MAX||(en-10)%16||!bytes(s,3,&mk)||mk.len!=32||!bytes(s,4,&eh)||eh.len!=32||!bytes(s,1,&ph)||ph.len!=32||!SHA256(enc,en,hash)||CRYPTO_memcmp(hash,eh.ptr,32))return NULL;
  EVP_PKEY_CTX *k=EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF,NULL);uint8_t salt[32]={0};const char *info=kind?"WhatsApp App State Keys":"WhatsApp History Keys";
  int ok=k&&EVP_PKEY_derive_init(k)>0&&EVP_PKEY_CTX_set_hkdf_md(k,EVP_sha256())>0&&EVP_PKEY_CTX_set1_hkdf_salt(k,salt,32)>0&&EVP_PKEY_CTX_set1_hkdf_key(k,mk.ptr,32)>0&&EVP_PKEY_CTX_add1_hkdf_info(k,(const uint8_t*)info,(int)strlen(info))>0&&EVP_PKEY_derive(k,expanded,&expanded_n)>0;EVP_PKEY_CTX_free(k);
  HMAC_CTX *hm=HMAC_CTX_new();ok=ok&&hm&&HMAC_Init_ex(hm,expanded+48,32,EVP_sha256(),NULL)&&HMAC_Update(hm,expanded,16)&&HMAC_Update(hm,enc,en-10)&&HMAC_Final(hm,mac,&mn)&&!CRYPTO_memcmp(mac,enc+en-10,10);HMAC_CTX_free(hm);
  if(!ok){OPENSSL_cleanse(expanded,sizeof expanded);return NULL;}
  compressed=malloc(en);if(!compressed){OPENSSL_cleanse(expanded,sizeof expanded);return NULL;}allocated=1;
  EVP_CIPHER_CTX *aes=EVP_CIPHER_CTX_new();int a=0,b=0;ok=aes&&EVP_DecryptInit_ex(aes,EVP_aes_256_cbc(),NULL,expanded+16,expanded)>0&&EVP_DecryptUpdate(aes,compressed,&a,enc,(int)(en-10))>0&&EVP_DecryptFinal_ex(aes,compressed+a,&b)>0;EVP_CIPHER_CTX_free(aes);OPENSSL_cleanse(expanded,sizeof expanded);cn=(size_t)(a+b);
  uint64_t length=number(s,2);ok=ok&&(!length||length==cn)&&SHA256(compressed,cn,hash)&&!CRYPTO_memcmp(hash,ph.ptr,32);if(!ok){OPENSSL_cleanse(compressed,en);free(compressed);return NULL;}
 }
 wa_history *h=calloc(1,sizeof *h);
 int ok=h&&(kind?state_open(h,io,s,(wa_slice){compressed,cn},missing):inflate_data(compressed,cn,&h->plain,&h->size));
 if(allocated){OPENSSL_cleanse(compressed,en);free(compressed);}
 if(!ok||(!kind&&!valid((wa_slice){h->plain,h->size}))){wa_history_free(h);return NULL;}
 h->root=(reader){h->plain,h->size,0};return h;
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
 int kind=wa_history_job_kind(p,n);
 if((kind==1||kind==2)&&version>wa_state_version(io)){
  uint8_t *stored=malloc(n);if(!stored)return 0;
  size_t size=0;int matches=wa_history_task(io,slot,stored,n,&size)==1&&size==n&&!CRYPTO_memcmp(p,stored,n);
  OPENSSL_cleanse(stored,n);free(stored);if(!matches)return 0;
  uint8_t data[8];big64(data,version);
  if(!io->put(io->user,STATE_VERSION_KEY,data,sizeof data))return 0;
 }
 return wa_history_done(io,slot,p,n);
}
static int alias(wa_slice s,unsigned lf,unsigned pf,wa_message_event *e){char lid[128],pn[128];if(!str(s,lf,lid,sizeof lid)||!str(s,pf,pn,sizeof pn)||!address(lid)||!address(pn)||!strstr(lid,"@lid")||!strstr(pn,"@s.whatsapp.net"))return 0;strcpy(e->kind,"alias");strcpy(e->id,lid);strcpy(e->jid,pn);return 1;}
static int message(wa_slice record,const char *chat,wa_slice meta,wa_message_event *e,uint64_t *time){wa_slice web,key,msg,inner;if(!bytes(record,1,&web)||!valid(web)||!bytes(web,1,&key)||!valid(key)||!bytes(web,2,&msg)||!valid(msg))return 0;
 char remote[128];if(!str(key,1,remote,sizeof remote)||!str(key,3,e->id,sizeof e->id))return 0;
 if(strcmp(remote,chat)){char alternate[128];int matches=(str(meta,39,alternate,sizeof alternate)&&!strcmp(remote,alternate))||(str(meta,42,alternate,sizeof alternate)&&!strcmp(remote,alternate));if(!matches)return 0;}
 wa_slice t;if(!bytes(msg,1,&t)&&(!bytes(msg,6,&inner)||!bytes(inner,1,&t)))return 0;
 if(!text(t,e->text,sizeof e->text))return 0;uint64_t sec=number(web,3);if(!sec||sec>253402300799ULL)return 0;*time=sec*1000;
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
