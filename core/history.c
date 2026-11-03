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
int wa_history_path(const uint8_t *p,size_t n,char *out,size_t cap){wa_slice s={p,n},inline_data;if(bytes(s,11,&inline_data)&&inline_data.len){if(cap)out[0]=0;return 1;}if(!str(s,5,out,cap)||strncmp(out,"/mms/",5))return 0;for(const char *c=out;*c;c++)if((unsigned char)*c<=32||*c=='\\'||*c=='#')return 0;return 1;}
int wa_history_response(const uint8_t *p,size_t n) {
 wa_slice s={p,n},protocol,response,result,full;
 if(!bytes(s,12,&protocol)||!bytes(protocol,17,&response)||!bytes(response,3,&result)||!bytes(result,6,&full))return -1;
 return (int)number(full,2);
}
struct wa_history {uint8_t *plain;size_t size;reader root,conversations;wa_slice chat;char jid[128];unsigned chat_stage;};
static int inflate_data(const uint8_t *p,size_t n,uint8_t **out,size_t *size){
 z_stream z={0};if(inflateInit(&z)!=Z_OK)return 0;size_t cap=65536;uint8_t *b=malloc(cap);if(!b){inflateEnd(&z);return 0;}z.next_in=(Bytef*)p;z.avail_in=(uInt)n;int r=Z_OK;
 while(r==Z_OK){if(z.total_out==cap){if(cap==WA_HISTORY_PLAIN_MAX){r=Z_MEM_ERROR;break;}cap*=2;uint8_t *grown=realloc(b,cap);if(!grown){r=Z_MEM_ERROR;break;}b=grown;}
 z.next_out=b+z.total_out;z.avail_out=(uInt)(cap-z.total_out);r=inflate(&z,Z_NO_FLUSH);}
 int ok=r==Z_STREAM_END&&z.avail_in==0&&z.total_out>0;*size=(size_t)z.total_out;inflateEnd(&z);if(!ok){OPENSSL_cleanse(b,cap);free(b);return 0;}*out=b;return 1;
}
wa_history *wa_history_open(const uint8_t *notification,size_t n,const uint8_t *enc,size_t en){
 if(!notification||!n||n>WA_HISTORY_NOTIFICATION_MAX||!valid((wa_slice){notification,n}))return NULL;
 wa_slice s={notification,n},v;uint8_t *compressed=NULL;size_t cn=0;int allocated=0;
 if(bytes(s,11,&v)&&v.len){compressed=(uint8_t*)v.ptr;cn=v.len;}
 else {
  wa_slice mk,eh,ph;uint8_t hash[32],expanded[112],mac[32];size_t expanded_n=sizeof expanded;unsigned mn=0;
  if(!enc||en<26||en>WA_HISTORY_DOWNLOAD_MAX||(en-10)%16||!bytes(s,3,&mk)||mk.len!=32||!bytes(s,4,&eh)||eh.len!=32||!bytes(s,1,&ph)||ph.len!=32||!SHA256(enc,en,hash)||CRYPTO_memcmp(hash,eh.ptr,32))return NULL;
  EVP_PKEY_CTX *k=EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF,NULL);uint8_t salt[32]={0};const char info[]="WhatsApp History Keys";
  int ok=k&&EVP_PKEY_derive_init(k)>0&&EVP_PKEY_CTX_set_hkdf_md(k,EVP_sha256())>0&&EVP_PKEY_CTX_set1_hkdf_salt(k,salt,32)>0&&EVP_PKEY_CTX_set1_hkdf_key(k,mk.ptr,32)>0&&EVP_PKEY_CTX_add1_hkdf_info(k,(const uint8_t*)info,sizeof info-1)>0&&EVP_PKEY_derive(k,expanded,&expanded_n)>0;EVP_PKEY_CTX_free(k);
  HMAC_CTX *hm=HMAC_CTX_new();ok=ok&&hm&&HMAC_Init_ex(hm,expanded+48,32,EVP_sha256(),NULL)&&HMAC_Update(hm,expanded,16)&&HMAC_Update(hm,enc,en-10)&&HMAC_Final(hm,mac,&mn)&&!CRYPTO_memcmp(mac,enc+en-10,10);HMAC_CTX_free(hm);
  if(!ok){OPENSSL_cleanse(expanded,sizeof expanded);return NULL;}
  compressed=malloc(en);if(!compressed){OPENSSL_cleanse(expanded,sizeof expanded);return NULL;}allocated=1;
  EVP_CIPHER_CTX *aes=EVP_CIPHER_CTX_new();int a=0,b=0;ok=aes&&EVP_DecryptInit_ex(aes,EVP_aes_256_cbc(),NULL,expanded+16,expanded)>0&&EVP_DecryptUpdate(aes,compressed,&a,enc,(int)(en-10))>0&&EVP_DecryptFinal_ex(aes,compressed+a,&b)>0;EVP_CIPHER_CTX_free(aes);OPENSSL_cleanse(expanded,sizeof expanded);cn=(size_t)(a+b);
  uint64_t length=number(s,2);ok=ok&&(!length||length==cn)&&SHA256(compressed,cn,hash)&&!CRYPTO_memcmp(hash,ph.ptr,32);if(!ok){OPENSSL_cleanse(compressed,en);free(compressed);return NULL;}
 }
 wa_history *h=calloc(1,sizeof *h);int ok=h&&inflate_data(compressed,cn,&h->plain,&h->size);if(allocated){OPENSSL_cleanse(compressed,en);free(compressed);}if(!ok){wa_history_free(h);return NULL;}
 if(!valid((wa_slice){h->plain,h->size})){wa_history_free(h);return NULL;}h->root=(reader){h->plain,h->size,0};return h;
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
void wa_history_free(wa_history *h){if(h){if(h->plain){OPENSSL_cleanse(h->plain,h->size);free(h->plain);}OPENSSL_cleanse(h,sizeof *h);free(h);}}
