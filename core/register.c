#include "register.h"
#include "wire.h"
#include <openssl/md5.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

typedef struct { uint8_t *p;size_t cap,n; } pb;
static int varint(pb *b,uint64_t v){do{if(b->n>=b->cap)return 0;uint8_t byte=v&127;v>>=7;b->p[b->n++]=byte|(v?128:0);}while(v);return 1;}
static int number(pb *b,unsigned field,uint64_t value){return varint(b,((uint64_t)field<<3))&&varint(b,value);}
static int bytes(pb *b,unsigned field,const uint8_t *ptr,size_t n){
 if(!ptr||n>b->cap-b->n)return 0;
 return varint(b,((uint64_t)field<<3)|2)&&varint(b,n)&&n<=b->cap-b->n&&(memcpy(b->p+b->n,ptr,n),b->n+=n,1);
}
static int string(pb *b,unsigned field,const char *s){return bytes(b,field,(const uint8_t*)s,strlen(s));}
static int nested(pb *b,unsigned field,const pb *src){return bytes(b,field,src->p,src->n);}
static void be(uint32_t v,uint8_t *out,size_t n){for(size_t j=0;j<n;j++)out[n-1-j]=(uint8_t)(v>>(j*8));}
int wa_registration_payload(const wa_registration *in,uint8_t *out,size_t cap,size_t *written){
 if(!in||!out||!written||!in->identity_public||!in->prekey_public||!in->prekey_signature||
  !in->registration_id||!in->signed_prekey_id||in->signed_prekey_id>0xffffff||
  !in->operating_system||strlen(in->operating_system)>64)return 0;
 pb b={out,cap,0};
 uint8_t a[256],ua[512],web[64],device[1024],dver[64],sync[128],pair[2048];
 pb v={a,sizeof a,0},u={ua,sizeof ua,0},w={web,sizeof web,0},d={device,sizeof device,0};
 pb dv={dver,sizeof dver,0},sy={sync,sizeof sync,0},pr={pair,sizeof pair,0};
 /* Default version in Baileys' Defaults/index.ts at audit time. */
 if(!number(&v,1,2)||!number(&v,2,3000)||!number(&v,3,1043857760U))return 0;
 if(!number(&u,1,14) /* WEB */||!nested(&u,2,&v)||
  !string(&u,3,"000")||!string(&u,4,"000")||!string(&u,5,"0.1")||
  !string(&u,7,"Desktop")||!string(&u,8,"0.1")||!number(&u,10,0)||
  !string(&u,11,"en")||!string(&u,12,"US"))return 0;
 if(!number(&w,4,0))return 0;
 if(!number(&dv,1,10)||!number(&dv,2,15)||!number(&dv,3,7))return 0;
 if(!number(&sy,3,10240)||!number(&sy,4,1)||!number(&sy,7,1)||
  !number(&sy,8,1)||!number(&sy,9,1)||!number(&sy,10,1)||
  !number(&sy,11,1)||!number(&sy,12,1)||!number(&sy,14,1))return 0;
 if(!string(&d,1,in->operating_system)||!nested(&d,2,&dv)||
  !number(&d,3,1) /* CHROME */||!number(&d,4,0) /* no full sync */||!nested(&d,5,&sy))return 0;
 uint8_t reg[4],id[3],typ[1]={5},digest[MD5_DIGEST_LENGTH],vers[64];
 be(in->registration_id,reg,4);be(in->signed_prekey_id,id,3);
 int k=snprintf((char*)vers,sizeof vers,"2.3000.1043857760");
 if(k<=0||(size_t)k>=sizeof vers||!MD5(vers,(size_t)k,digest))return 0;
 if(!bytes(&pr,1,reg,4)||!bytes(&pr,2,typ,1)||
  !bytes(&pr,3,in->identity_public,32)||!bytes(&pr,4,id,3)||
  !bytes(&pr,5,in->prekey_public,32)||!bytes(&pr,6,in->prekey_signature,64)||
  !bytes(&pr,7,digest,16)||!nested(&pr,8,&d))return 0;
 if(!number(&b,3,0) /* passive=false */||!nested(&b,5,&u)||!nested(&b,6,&w)||
  !number(&b,12,1) /* WIFI_UNKNOWN */||!number(&b,13,1) /* USER_ACTIVATED */||
  !nested(&b,19,&pr)||!number(&b,33,0) /* pull=false */)return 0;
 *written=b.n;return 1;
}
