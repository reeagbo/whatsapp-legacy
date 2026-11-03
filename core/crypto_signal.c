#include <signal_protocol.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <limits.h>
static int rnd(uint8_t *p,size_t n,void *u){(void)u;return n<=INT_MAX&&RAND_bytes(p,(int)n)==1?0:SG_ERR_UNKNOWN;}
static int hm_init(void **out,const uint8_t *key,size_t n,void *u){(void)u;if(n>INT_MAX)return SG_ERR_INVAL;HMAC_CTX *c=HMAC_CTX_new();if(!c||HMAC_Init_ex(c,key,(int)n,EVP_sha256(),NULL)!=1){HMAC_CTX_free(c);return SG_ERR_UNKNOWN;}*out=c;return 0;}
static int hm_update(void *c,const uint8_t *p,size_t n,void *u){(void)u;return HMAC_Update(c,p,n)==1?0:SG_ERR_UNKNOWN;}
static int hm_final(void *c,signal_buffer **out,void *u){(void)u;uint8_t d[32];unsigned n=0;if(HMAC_Final(c,d,&n)!=1||n!=32)return SG_ERR_UNKNOWN;*out=signal_buffer_create(d,32);OPENSSL_cleanse(d,sizeof d);return *out?0:SG_ERR_NOMEM;}
static void hm_free(void *c,void *u){(void)u;HMAC_CTX_free(c);}
static int sh_init(void **out,void *u){(void)u;EVP_MD_CTX *c=EVP_MD_CTX_new();if(!c||EVP_DigestInit_ex(c,EVP_sha512(),NULL)!=1){EVP_MD_CTX_free(c);return SG_ERR_UNKNOWN;}*out=c;return 0;}
static int sh_update(void *c,const uint8_t *p,size_t n,void *u){(void)u;return EVP_DigestUpdate(c,p,n)==1?0:SG_ERR_UNKNOWN;}
static int sh_final(void *c,signal_buffer **out,void *u){(void)u;uint8_t d[64];unsigned n=0;if(EVP_DigestFinal_ex(c,d,&n)!=1||n!=64||EVP_DigestInit_ex(c,EVP_sha512(),NULL)!=1)return SG_ERR_UNKNOWN;*out=signal_buffer_create(d,64);OPENSSL_cleanse(d,sizeof d);return *out?0:SG_ERR_NOMEM;}
static void sh_free(void *c,void *u){(void)u;EVP_MD_CTX_free(c);}
static int aes(int enc,signal_buffer **out,int mode,const uint8_t *key,size_t kn,const uint8_t *iv,size_t ivn,const uint8_t *p,size_t n){
 if(ivn!=16||n>INT_MAX-32||(kn!=16&&kn!=32))return SG_ERR_INVAL;
 const EVP_CIPHER *alg=mode==SG_CIPHER_AES_CBC_PKCS5?(kn==32?EVP_aes_256_cbc():EVP_aes_128_cbc()):mode==SG_CIPHER_AES_CTR_NOPADDING?(kn==32?EVP_aes_256_ctr():EVP_aes_128_ctr()):NULL;
 if(!alg)return SG_ERR_INVAL;
 EVP_CIPHER_CTX *c=EVP_CIPHER_CTX_new();signal_buffer *b=signal_buffer_alloc(n+32);int a=0,z=0;
 int ok=c&&b&&EVP_CipherInit_ex(c,alg,NULL,key,iv,enc)==1&&EVP_CipherUpdate(c,signal_buffer_data(b),&a,p,(int)n)==1&&EVP_CipherFinal_ex(c,signal_buffer_data(b)+a,&z)==1;
 if(ok){*out=signal_buffer_create(signal_buffer_data(b),(size_t)(a+z));ok=*out!=NULL;}
 signal_buffer_bzero_free(b);EVP_CIPHER_CTX_free(c);return ok?0:SG_ERR_INVALID_MESSAGE;
}
static int en(signal_buffer **o,int c,const uint8_t *k,size_t kn,const uint8_t *iv,size_t ivn,const uint8_t *p,size_t n,void *u){(void)u;return aes(1,o,c,k,kn,iv,ivn,p,n);}
static int de(signal_buffer **o,int c,const uint8_t *k,size_t kn,const uint8_t *iv,size_t ivn,const uint8_t *p,size_t n,void *u){(void)u;return aes(0,o,c,k,kn,iv,ivn,p,n);}
int wa_crypto_signal_context(signal_context **out){signal_crypto_provider p={rnd,hm_init,hm_update,hm_final,hm_free,sh_init,sh_update,sh_final,sh_free,en,de,NULL};int r=signal_context_create(out,NULL);if(r<0)return r;r=signal_context_set_crypto_provider(*out,&p);if(r<0){signal_context_destroy(*out);*out=NULL;}return r;}
