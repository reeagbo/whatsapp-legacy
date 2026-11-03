#include "handshake.h"
#include "wire.h"
#include <curve.h>
#include <signal_protocol.h>
#include <openssl/crypto.h>
#include <string.h>

/* WhatsApp long-term root key taken from Baileys src/Defaults/index.ts */
static const uint8_t wa_root[32] = {
 0x14,0x23,0x75,0x57,0x4d,0x0a,0x58,0x71,0x66,0xaa,0xe7,0x1e,0xbe,0x51,0x64,0x37,
 0xc4,0xa2,0x8b,0x73,0xe3,0x69,0x5c,0x6c,0xe1,0xf7,0xf9,0x54,0x5d,0xa8,0xee,0x6b
};

static int pb_uint(const uint8_t *buf,size_t len,unsigned field,uint64_t *out) {
 size_t i=0;
 while(i<len){uint64_t tag,v; if(!wa_pb_varint(buf,len,&i,&tag)||!(tag>>3))return 0;
  unsigned w=(unsigned)(tag&7);
  if(w==0){if(!wa_pb_varint(buf,len,&i,&v))return 0; if((tag>>3)==field){*out=v;return 1;}}
  else if(w==2){if(!wa_pb_varint(buf,len,&i,&v)||v>len-i)return 0;i+=(size_t)v;}
  else if(w==1){if(len-i<8)return 0;i+=8;}
  else if(w==5){if(len-i<4)return 0;i+=4;}
  else return 0;
 }
 return 0;
}
static int verify_sig(const uint8_t *pub,size_t pub_len,wa_slice data,wa_slice sig,signal_context *ctx){
 if(pub_len!=32||sig.len!=64)return 0;
 uint8_t typed[33];typed[0]=5;memcpy(typed+1,pub,32);
 ec_public_key *k=NULL;
 if(curve_decode_point(&k,typed,sizeof typed,ctx)!=0)return 0;
 int ok=curve_verify_signature(k,data.ptr,data.len,sig.ptr,sig.len)==1;
 SIGNAL_UNREF(k);return ok;
}
static int cert_parts(wa_slice cert,wa_slice *details,wa_slice *sig){
 return wa_pb_bytes_field(cert.ptr,cert.len,1,details)
     && wa_pb_bytes_field(cert.ptr,cert.len,2,sig)
     && details->len>0 && sig->len==64;
}
static int check_chain(const uint8_t *buf,size_t len,const uint8_t server_static[32]){
 wa_slice leaf,inter,ld,ls,id,is,ikey,lkey;uint64_t issuer=~(uint64_t)0;
 if(!wa_pb_bytes_field(buf,len,1,&leaf)||!wa_pb_bytes_field(buf,len,2,&inter))return 0;
 if(!cert_parts(leaf,&ld,&ls)||!cert_parts(inter,&id,&is))return 0;
 if(!pb_uint(id.ptr,id.len,2,&issuer)||issuer!=0)return 0;
 if(!wa_pb_bytes_field(id.ptr,id.len,3,&ikey)||ikey.len!=32)return 0;
 if(!wa_pb_bytes_field(ld.ptr,ld.len,3,&lkey)||lkey.len!=32)return 0;
 /* Stronger than Baileys: require the signed leaf key to match handshake server static key. */
 if(CRYPTO_memcmp(lkey.ptr,server_static,32)!=0)return 0;
 signal_context *ctx=NULL;
 if(signal_context_create(&ctx,NULL)!=0)return 0;
 int valid=verify_sig(wa_root,32,id,is,ctx) && verify_sig(ikey.ptr,ikey.len,ld,ls,ctx);
 signal_context_destroy(ctx);
 return valid;
}
int wa_process_server_hello_ex(wa_noise *n,const wa_server_hello *h,
 const uint8_t local_private[32],const uint8_t local_public[32],uint8_t static_out[48],const char **failed_stage){
 if(failed_stage)*failed_stage="validate_input";
 if(!n||!h||!local_private||!local_public||!static_out||n->transport||h->ephemeral_len!=32||h->static_len!=48||h->payload_len<16||h->payload_len>65536)return 0;
 uint8_t dh[32],server_static[32],cert[65536];size_t len=sizeof server_static,cert_len=sizeof cert,e=0;
 int ok=0;
 #define STEP(label,expr) do { if(failed_stage)*failed_stage=label; if(!(expr))goto done; } while(0)
 STEP("authenticate_ephemeral",wa_noise_auth(n,h->ephemeral,32));
 STEP("ephemeral_ecdh",wa_noise_dh(n->ephemeral_private,h->ephemeral,dh));
 STEP("mix_ephemeral",wa_noise_mix(n,dh,32));
 STEP("decrypt_server_static",wa_noise_decrypt(n,h->server_static,h->static_len,server_static,&len));
 STEP("server_static_length",len==32);
 STEP("server_static_ecdh",wa_noise_dh(n->ephemeral_private,server_static,dh));
 STEP("mix_server_static",wa_noise_mix(n,dh,32));
 STEP("decrypt_certificate",wa_noise_decrypt(n,h->payload,h->payload_len,cert,&cert_len));
 STEP("verify_certificate_chain",check_chain(cert,cert_len,server_static));
 STEP("encrypt_client_static",wa_noise_encrypt(n,local_public,32,static_out,&e)&&e==48);
 STEP("client_static_ecdh",wa_noise_dh(local_private,h->ephemeral,dh));
 STEP("mix_client_static",wa_noise_mix(n,dh,32));
 ok=1;
 if(failed_stage)*failed_stage=NULL;
 done:
 OPENSSL_cleanse(dh,sizeof dh);OPENSSL_cleanse(server_static,sizeof server_static);OPENSSL_cleanse(cert,sizeof cert);
 #undef STEP
 return ok;
}
int wa_process_server_hello(wa_noise *n,const wa_server_hello *h,
 const uint8_t local_private[32],const uint8_t local_public[32],uint8_t static_out[48]){
 return wa_process_server_hello_ex(n,h,local_private,local_public,static_out,NULL);
}
int wa_make_client_finish(const uint8_t encrypted_static[48],const uint8_t *payload,size_t plen,uint8_t *out,size_t cap,size_t *written){
 if(!encrypted_static||!payload||!out||!written||plen<16)return 0;
 if(plen>65536||cap<64)return 0;
 uint8_t nested[65700];size_t n=0,m=0,k=0;
 if(!wa_pb_write_bytes(1,encrypted_static,48,nested,sizeof nested,&n))return 0;
 if(!wa_pb_write_bytes(2,payload,plen,nested+n,sizeof nested-n,&m))return 0;
 if(!wa_pb_write_bytes(4,nested,n+m,out,cap,&k))return 0;
 *written=k;return 1;
}

int wa_finish_noise(wa_noise *n, const uint8_t encrypted_static[48],
    const uint8_t *client_payload, size_t client_payload_len,
    uint8_t *out, size_t capacity, size_t *written) {
 if (!n || !encrypted_static || !client_payload || !client_payload_len ||
     !out || !written || n->transport || client_payload_len > 65536) return 0;
 /* GCM appends a 16-byte authentication tag. The outer container is protobuf. */
 uint8_t ciphertext[65552]; size_t ciphertext_len=0;
 int ok = wa_noise_encrypt(n,client_payload,client_payload_len,ciphertext,&ciphertext_len)
  && wa_make_client_finish(encrypted_static,ciphertext,ciphertext_len,out,capacity,written)
  && wa_noise_transport(n);
 OPENSSL_cleanse(ciphertext,sizeof ciphertext);
 return ok;
}
