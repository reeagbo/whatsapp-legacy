#include "noise.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) return 0; } while(0)
static const char mode[] = "Noise_XX_25519_AESGCM_SHA256\0\0\0\0";
static void iv(uint32_t n,uint8_t out[12]) { memset(out,0,12);out[8]=(uint8_t)(n>>24);out[9]=(uint8_t)(n>>16);out[10]=(uint8_t)(n>>8);out[11]=(uint8_t)n; }
static int hkdf64(const uint8_t salt[32],const uint8_t *ikm,size_t ikmlen,uint8_t out[64]) {
 uint8_t prk[32],t[32],msg[33];unsigned sz=0;
 CHECK(HMAC(EVP_sha256(),salt,32,ikm,ikmlen,prk,&sz) && sz==32);
 msg[0]=1;CHECK(HMAC(EVP_sha256(),prk,32,msg,1,t,&sz)&&sz==32);memcpy(out,t,32);
 memcpy(msg,t,32);msg[32]=2;CHECK(HMAC(EVP_sha256(),prk,32,msg,33,t,&sz)&&sz==32);memcpy(out+32,t,32);
 OPENSSL_cleanse(prk,sizeof prk);OPENSSL_cleanse(t,sizeof t);return 1;
}
static int gcm(int enc,const uint8_t key[32],uint32_t ctr,const uint8_t *aad,size_t aadlen,const uint8_t *src,size_t src_len,uint8_t *dst,size_t *dstlen) {
 uint8_t nonce[12];iv(ctr,nonce);EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();if(!ctx)return 0;
 int l=0,total=0,ok=0;
 if(enc){
  if(!EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,key,nonce))goto done;
  if(aadlen&&!EVP_EncryptUpdate(ctx,NULL,&l,aad,(int)aadlen))goto done;
  if(src_len&&!EVP_EncryptUpdate(ctx,dst,&l,src,(int)src_len))goto done;
  total=l;if(!EVP_EncryptFinal_ex(ctx,dst+total,&l))goto done;total+=l;
  if(!EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,16,dst+total))goto done;
  total+=16;
 }else{
  if(src_len<16)goto done;
  size_t ct=src_len-16;
  if(!EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,key,nonce))goto done;
  if(aadlen&&!EVP_DecryptUpdate(ctx,NULL,&l,aad,(int)aadlen))goto done;
  if(ct&&!EVP_DecryptUpdate(ctx,dst,&l,src,(int)ct))goto done;
  total=l;if(!EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,16,(void*)(src+ct)))goto done;
  if(EVP_DecryptFinal_ex(ctx,dst+total,&l)<=0)goto done;
  total+=l;
 }
 *dstlen=(size_t)total;ok=1;
 done:EVP_CIPHER_CTX_free(ctx);return ok;
}
int wa_noise_dh(const uint8_t priv[32],const uint8_t pub[32],uint8_t out[32]){
 int ok=0;size_t n=32;EVP_PKEY *a=EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519,NULL,priv,32),*b=EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519,NULL,pub,32);EVP_PKEY_CTX *ctx=NULL;
 if(a&&b&&(ctx=EVP_PKEY_CTX_new(a,NULL))&&EVP_PKEY_derive_init(ctx)>0&&EVP_PKEY_derive_set_peer(ctx,b)>0&&EVP_PKEY_derive(ctx,out,&n)>0&&n==32)ok=1;
 EVP_PKEY_CTX_free(ctx);EVP_PKEY_free(a);EVP_PKEY_free(b);return ok;
}
int wa_noise_init(wa_noise *n){
 memset(n,0,sizeof *n);SHA256((const unsigned char*)mode,sizeof mode-1,n->hash);
 memcpy(n->salt,n->hash,32);memcpy(n->key,n->hash,32);
 CHECK(RAND_bytes(n->ephemeral_private,32)==1);
 EVP_PKEY *p=EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519,NULL,n->ephemeral_private,32);if(!p)return 0;
 size_t len=32;int ok=EVP_PKEY_get_raw_public_key(p,n->ephemeral_public,&len)==1&&len==32;EVP_PKEY_free(p);
 if(!ok)return 0;
 // Baileys authenticates the WA header and the client's ephemeral public key.
 const uint8_t header[]={87,65,6,3};return wa_noise_auth(n,header,sizeof header)&&wa_noise_auth(n,n->ephemeral_public,32);
}
int wa_noise_auth(wa_noise *n,const uint8_t *data,size_t len){
 if(n->transport)return 0;
 SHA256_CTX s;SHA256_Init(&s);SHA256_Update(&s,n->hash,32);SHA256_Update(&s,data,len);SHA256_Final(n->hash,&s);return 1;
}
int wa_noise_mix(wa_noise *n,const uint8_t *data,size_t len){
 uint8_t k[64];CHECK(!n->transport&&hkdf64(n->salt,data,len,k));memcpy(n->salt,k,32);memcpy(n->key,k+32,32);n->counter=0;OPENSSL_cleanse(k,sizeof k);return 1;
}
int wa_noise_encrypt(wa_noise *n,const uint8_t *plain,size_t plen,uint8_t *cipher,size_t *clen){
 const uint8_t *key=n->transport?n->write_key:n->key;uint32_t ctr=n->transport?n->wcounter:n->counter;
 const uint8_t *aad=n->transport?NULL:n->hash;size_t aadlen=n->transport?0:32;
 CHECK(gcm(1,key,ctr,aad,aadlen,plain,plen,cipher,clen));
 if(n->transport)n->wcounter++;else{n->counter++;CHECK(wa_noise_auth(n,cipher,*clen));}return 1;
}
int wa_noise_decrypt(wa_noise *n,const uint8_t *cipher,size_t clen,uint8_t *plain,size_t *plen){
 const uint8_t *key=n->transport?n->read_key:n->key;uint32_t ctr=n->transport?n->rcounter:n->counter;
 const uint8_t *aad=n->transport?NULL:n->hash;size_t aadlen=n->transport?0:32;
 CHECK(gcm(0,key,ctr,aad,aadlen,cipher,clen,plain,plen));
 if(n->transport)n->rcounter++;else{n->counter++;CHECK(wa_noise_auth(n,cipher,clen));}return 1;
}
int wa_noise_transport(wa_noise *n){uint8_t keys[64];CHECK(!n->transport&&hkdf64(n->salt,NULL,0,keys));memcpy(n->write_key,keys,32);memcpy(n->read_key,keys+32,32);n->transport=1;n->rcounter=n->wcounter=0;OPENSSL_cleanse(keys,sizeof keys);return 1;}
int wa_noise_frame(const uint8_t *data,size_t len,int intro,uint8_t *out,size_t cap,size_t *outlen){
 static const uint8_t header[]={87,65,6,3};size_t offset=intro?4:0;
 if(len>0xffffff||cap<len+3+offset)return 0;
 if(intro)memcpy(out,header,4);
 out[offset]=(uint8_t)(len>>16);out[offset+1]=(uint8_t)(len>>8);out[offset+2]=(uint8_t)len;
 if(len)memcpy(out+offset+3,data,len);
 *outlen=offset+3+len;return 1;
}
int wa_noise_unframe(const uint8_t *in,size_t len,const uint8_t **frame,size_t *flen,size_t *used){
 if(len<3)return 0;
 size_t n=((size_t)in[0]<<16)|((size_t)in[1]<<8)|in[2];if(len<n+3)return 0;
 *frame=in+3;*flen=n;*used=n+3;return 1;
}
