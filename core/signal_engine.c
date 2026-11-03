#include "signal_engine.h"
#include <signal_protocol.h>
#include <session_builder.h>
#include <session_cipher.h>
#include <session_pre_key.h>
#include <protocol.h>
#include <curve.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#define RECORD_MAX 65536
struct wa_signal {wa_identity identity;wa_storage io;signal_context *ctx;signal_protocol_store_context *store;};
extern int wa_crypto_signal_context(signal_context **);
static int address(const char *jid,signal_protocol_address *a,char name[96]){
 if(!jid)return 0;
 const char *at=strchr(jid,'@'),*colon=strchr(jid,':');if(!at||at-jid<1||at-jid>64)return 0;
 size_t n=(size_t)((colon&&colon<at?colon:at)-jid);if(!n||n>40)return 0;
 for(size_t j=0;j<n;j++)if(jid[j]<'0'||jid[j]>'9')return 0;
 int domain=!strcmp(at+1,"s.whatsapp.net")?0:!strcmp(at+1,"lid")?1:-1;if(domain<0)return 0;
 memcpy(name,jid,n);name[n]=0;if(domain)strcat(name,"_1");
 unsigned long d=0;if(colon&&colon<at){char *end;d=strtoul(colon+1,&end,10);if(end==colon+1||end!=at||d>255)return 0;}
 *a=(signal_protocol_address){name,strlen(name),(int32_t)d};return 1;
}
static void key_address(char out[80],const char *kind,const signal_protocol_address *a){
 uint8_t digest[32];char b[160];int n=snprintf(b,sizeof b,"%.*s:%d",(int)a->name_len,a->name,a->device_id);SHA256((uint8_t*)b,(size_t)n,digest);
 size_t k=(size_t)snprintf(out,80,"%s-",kind);for(size_t j=0;j<32;j++)snprintf(out+k+j*2,3,"%02x",digest[j]);
}
static void key_id(char out[80],const char *kind,uint32_t id){snprintf(out,80,"%s-%u",kind,id);}
static int load(wa_signal *s,const char *key,signal_buffer **out){
 uint8_t *b=malloc(RECORD_MAX);if(!b)return SG_ERR_NOMEM;size_t n=0;int r=s->io.get(s->io.user,key,b,RECORD_MAX,&n);
 if(r==1){*out=signal_buffer_create(b,n);if(!*out)r=-1;}
 OPENSSL_cleanse(b,RECORD_MAX);free(b);return r<0?SG_ERR_UNKNOWN:r;
}
static int session_load(signal_buffer **out,signal_buffer **user,const signal_protocol_address *a,void *u){(void)user;char key[80];key_address(key,"session",a);return load(u,key,out);}
static int session_store(const signal_protocol_address *a,uint8_t *p,size_t n,uint8_t *ur,size_t un,void *u){(void)ur;(void)un;wa_signal *s=u;char key[80];key_address(key,"session",a);return n<=RECORD_MAX&&s->io.put(s->io.user,key,p,n)?0:SG_ERR_UNKNOWN;}
static int session_has(const signal_protocol_address *a,void *u){signal_buffer *b=NULL;int r=session_load(&b,NULL,a,u);signal_buffer_bzero_free(b);return r;}
static int session_delete(const signal_protocol_address *a,void *u){wa_signal *s=u;char key[80];key_address(key,"session",a);return s->io.put(s->io.user,key,NULL,0)?1:SG_ERR_UNKNOWN;}
static int session_all(const char *n,size_t z,void *u){(void)n;(void)z;(void)u;return SG_ERR_INVAL;}
static int session_devices(signal_int_list **out,const char *n,size_t z,void *u){(void)n;(void)z;(void)u;*out=signal_int_list_alloc();return *out?0:SG_ERR_NOMEM;}
static int pk_load(signal_buffer **out,uint32_t id,void *u){char key[80];key_id(key,"prekey",id);int r=load(u,key,out);return r==1?0:r==0?SG_ERR_INVALID_KEY_ID:r;}
static int pk_store(uint32_t id,uint8_t *p,size_t n,void *u){wa_signal *s=u;char key[80];key_id(key,"prekey",id);return s->io.put(s->io.user,key,p,n)?0:SG_ERR_UNKNOWN;}
static int pk_has(uint32_t id,void *u){signal_buffer *b=NULL;int r=pk_load(&b,id,u);signal_buffer_bzero_free(b);return r==0?1:r==SG_ERR_INVALID_KEY_ID?0:r;}
static int pk_remove(uint32_t id,void *u){wa_signal *s=u;char key[80];key_id(key,"prekey",id);return s->io.put(s->io.user,key,NULL,0)?0:SG_ERR_UNKNOWN;}
static int sp_load(signal_buffer **out,uint32_t id,void *u){char key[80];key_id(key,"signed",id);int r=load(u,key,out);return r==1?0:r==0?SG_ERR_INVALID_KEY_ID:r;}
static int sp_store(uint32_t id,uint8_t *p,size_t n,void *u){wa_signal *s=u;char key[80];key_id(key,"signed",id);return s->io.put(s->io.user,key,p,n)?0:SG_ERR_UNKNOWN;}
static int sp_has(uint32_t id,void *u){signal_buffer *b=NULL;int r=sp_load(&b,id,u);signal_buffer_bzero_free(b);return r==0?1:r==SG_ERR_INVALID_KEY_ID?0:r;}
static int sp_remove(uint32_t id,void *u){(void)id;(void)u;return SG_ERR_INVAL;}
static int own(signal_buffer **pk,signal_buffer **sk,void *u){wa_signal *s=u;uint8_t b[33]={5};memcpy(b+1,s->identity.identity_public,32);*pk=signal_buffer_create(b,33);*sk=signal_buffer_create(s->identity.identity_private,32);return *pk&&*sk?0:SG_ERR_NOMEM;}
static int reg(void *u,uint32_t *id){*id=((wa_signal*)u)->identity.registration_id;return 0;}
static int trusted(const signal_protocol_address *a,uint8_t *p,size_t n,void *u){wa_signal *s=u;uint8_t b[33];size_t z=0;char key[80];key_address(key,"identity",a);int r=s->io.get(s->io.user,key,b,sizeof b,&z);return r<0?SG_ERR_UNKNOWN:r==0?1:z==n&&!CRYPTO_memcmp(b,p,n);}
static int save_identity(const signal_protocol_address *a,uint8_t *p,size_t n,void *u){wa_signal *s=u;if(!p||n!=33)return SG_ERR_INVAL;int r=trusted(a,p,n,u);if(r!=1)return r<0?r:SG_ERR_UNTRUSTED_IDENTITY;char key[80];key_address(key,"identity",a);return s->io.put(s->io.user,key,p,n)?0:SG_ERR_UNKNOWN;}
static int pub(wa_signal *s,const uint8_t p[32],ec_public_key **out){uint8_t b[33]={5};memcpy(b+1,p,32);return curve_decode_point(out,b,33,s->ctx);}
static int pair(wa_signal *s,const uint8_t sk[32],const uint8_t pk[32],ec_key_pair **out){ec_private_key *priv=NULL;ec_public_key *publ=NULL;int r=curve_decode_private_point(&priv,sk,32,s->ctx);if(r>=0)r=pub(s,pk,&publ);if(r>=0)r=ec_key_pair_create(out,publ,priv);if(priv)SIGNAL_UNREF(priv);if(publ)SIGNAL_UNREF(publ);return r;}
wa_signal *wa_signal_create(const wa_identity *id,const wa_storage *io){
 if(!id||!io||!io->get||!io->put)return NULL;
 wa_signal *s=calloc(1,sizeof *s);if(!s)return NULL;s->identity=*id;s->io=*io;
 if(wa_crypto_signal_context(&s->ctx)<0||signal_protocol_store_context_create(&s->store,s->ctx)<0)goto bad;
 signal_protocol_session_store sessions={session_load,session_devices,session_store,session_has,session_delete,session_all,NULL,s};
 signal_protocol_pre_key_store pre={pk_load,pk_store,pk_has,pk_remove,NULL,s};
 signal_protocol_signed_pre_key_store signed_pre={sp_load,sp_store,sp_has,sp_remove,NULL,s};
 signal_protocol_identity_key_store ids={own,reg,save_identity,trusted,NULL,s};
 if(signal_protocol_store_context_set_session_store(s->store,&sessions)<0||signal_protocol_store_context_set_pre_key_store(s->store,&pre)<0||signal_protocol_store_context_set_signed_pre_key_store(s->store,&signed_pre)<0||signal_protocol_store_context_set_identity_key_store(s->store,&ids)<0)goto bad;
 ec_key_pair *kp=NULL;session_signed_pre_key *sp=NULL;int r=pair(s,id->prekey_private,id->prekey_public,&kp);
 if(r>=0)r=session_signed_pre_key_create(&sp,1,0,kp,id->prekey_signature,64);
 if(r>=0)r=signal_protocol_signed_pre_key_store_key(s->store,sp);
 if(kp)SIGNAL_UNREF(kp);if(sp)SIGNAL_UNREF(sp);if(r<0)goto bad;
 return s;
 bad:wa_signal_free(s);return NULL;
}
void wa_signal_free(wa_signal *s){if(!s)return;if(s->store)signal_protocol_store_context_destroy(s->store);if(s->ctx)signal_context_destroy(s->ctx);OPENSSL_cleanse(s,sizeof *s);free(s);}
int wa_signal_prekey(wa_signal *s,uint32_t id,uint8_t pk[32]){
 if(!s||!id||id>0xffffff)return 0;session_pre_key *record=NULL;int r=signal_protocol_pre_key_load_key(s->store,&record,id);
 if(r==SG_ERR_INVALID_KEY_ID){ec_key_pair *kp=NULL;r=curve_generate_key_pair(s->ctx,&kp);if(r>=0)r=session_pre_key_create(&record,id,kp);if(r>=0)r=signal_protocol_pre_key_store_key(s->store,record);if(kp)SIGNAL_UNREF(kp);}
 if(r>=0){ec_public_key *p=ec_key_pair_get_public(session_pre_key_get_key_pair(record));signal_buffer *b=NULL;r=ec_public_key_serialize(&b,p);if(r>=0&&signal_buffer_len(b)==33)memcpy(pk,signal_buffer_data(b)+1,32);else r=SG_ERR_INVALID_KEY;signal_buffer_free(b);}
 if(record)SIGNAL_UNREF(record);return r>=0;
}
int wa_signal_has(wa_signal *s,const char *jid){signal_protocol_address a;char name[96];return s&&address(jid,&a,name)?signal_protocol_session_contains_session(s->store,&a):0;}
int wa_signal_bundle(wa_signal *s,const char *jid,uint32_t registration,const uint8_t identity[32],uint32_t sid,const uint8_t sk[32],const uint8_t sig[64],uint32_t pid,const uint8_t *pk){
 signal_protocol_address a;char name[96];if(!s||!address(jid,&a,name))return SG_ERR_INVAL;
 ec_public_key *i=NULL,*skey=NULL,*pkey=NULL;session_pre_key_bundle *b=NULL;session_builder *builder=NULL;
 int r=pub(s,identity,&i);if(r>=0)r=pub(s,sk,&skey);if(r>=0&&pk)r=pub(s,pk,&pkey);
 if(r>=0)r=session_pre_key_bundle_create(&b,registration,a.device_id,pid,pkey,sid,skey,sig,64,i);
 if(r>=0)r=session_builder_create(&builder,s->store,&a,s->ctx);if(r>=0)r=session_builder_process_pre_key_bundle(builder,b);
 session_builder_free(builder);if(b)SIGNAL_UNREF(b);if(i)SIGNAL_UNREF(i);if(skey)SIGNAL_UNREF(skey);if(pkey)SIGNAL_UNREF(pkey);return r;
}
int wa_signal_encrypt(wa_signal *s,const char *jid,const uint8_t *p,size_t n,uint8_t *out,size_t cap,size_t *len,int *pk){
 signal_protocol_address a;char name[96];if(!s||!address(jid,&a,name))return SG_ERR_INVAL;session_cipher *c=NULL;ciphertext_message *m=NULL;
 int r=session_cipher_create(&c,s->store,&a,s->ctx);if(r>=0)r=session_cipher_encrypt(c,p,n,&m);
 if(r>=0){signal_buffer *b=ciphertext_message_get_serialized(m);*len=signal_buffer_len(b);if(*len>cap)r=SG_ERR_NOMEM;else{memcpy(out,signal_buffer_data(b),*len);*pk=ciphertext_message_get_type(m)==CIPHERTEXT_PREKEY_TYPE;}}
 if(m)SIGNAL_UNREF(m);session_cipher_free(c);return r;
}
int wa_signal_decrypt(wa_signal *s,const char *jid,int pk,const uint8_t *p,size_t n,uint8_t *out,size_t cap,size_t *len){
 signal_protocol_address a;char name[96];if(!s||!address(jid,&a,name))return SG_ERR_INVAL;session_cipher *c=NULL;signal_buffer *plain=NULL;
 int r=session_cipher_create(&c,s->store,&a,s->ctx);
 if(r>=0&&pk){pre_key_signal_message *m=NULL;r=pre_key_signal_message_deserialize(&m,p,n,s->ctx);if(r>=0)r=session_cipher_decrypt_pre_key_signal_message(c,m,NULL,&plain);if(m)SIGNAL_UNREF(m);}
 else if(r>=0){signal_message *m=NULL;r=signal_message_deserialize(&m,p,n,s->ctx);if(r>=0)r=session_cipher_decrypt_signal_message(c,m,NULL,&plain);if(m)SIGNAL_UNREF(m);}
 if(r>=0){*len=signal_buffer_len(plain);if(*len>cap)r=SG_ERR_NOMEM;else memcpy(out,signal_buffer_data(plain),*len);}
 signal_buffer_bzero_free(plain);session_cipher_free(c);return r;
}
