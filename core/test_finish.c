#include "handshake.h"
#include "wire.h"
#include "ws.h"
#include <openssl/crypto.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Cryptographic completion test with deterministic local state; not a WhatsApp
 * registration test. Uses a mirror receive state to validate transport keys. */
int main(void) {
 wa_noise client;
 assert(wa_noise_init(&client));
 const uint8_t message[]={0x0a,0x03,'p','o','c'};
 uint8_t enc_static[48]={0};
 uint8_t out[512];size_t used=0;
 wa_noise before=client;
 assert(wa_finish_noise(&client,enc_static,message,sizeof message,out,sizeof out,&used));
 assert(client.transport && used>64);
 /* Outer protocol: WA length prefix inside an RFC6455 masked binary frame. */
 uint8_t wa[1024], ws[1200], unpacked[1200];
 uint8_t mask[4]={9,8,7,6};size_t wa_size=0,ws_size=0;
 assert(wa_noise_frame(out,used,0,wa,sizeof wa,&wa_size));
 assert(wa_ws_encode(2,wa,wa_size,mask,ws,sizeof ws,&ws_size));
 assert(ws[0]==0x82 && (ws[1]&0x80));
 /* Test-side unmask: the client sends masked frames, unlike server frames. */
 size_t hdr=(ws[1]&127)==126?4:2;
 for(size_t j=0;j<wa_size;j++)unpacked[j]=ws[hdr+4+j]^mask[j&3];
 assert(memcmp(unpacked,wa,wa_size)==0);
 wa_slice finish,st,p;
 assert(wa_pb_bytes_field(out,used,4,&finish));
 assert(wa_pb_bytes_field(finish.ptr,finish.len,1,&st)&&st.len==48);
 assert(wa_pb_bytes_field(finish.ptr,finish.len,2,&p)&&p.len==sizeof(message)+16);
 uint8_t plaintext[128];size_t plain_len=0;
 assert(wa_noise_decrypt(&before,p.ptr,p.len,plaintext,&plain_len));
 assert(plain_len==sizeof message && memcmp(message,plaintext,plain_len)==0);
 assert(!wa_finish_noise(&client,enc_static,message,sizeof message,out,sizeof out,&used));
 /* Independently derive the counterpart's read key and check the transport frame. */
 wa_noise peer=before;
 assert(wa_noise_transport(&peer));
 memcpy(peer.read_key,client.write_key,32);
 uint8_t ct[256],pt[256];size_t cn=0,pn=0;
 const uint8_t msg[]={ 'c','h','a','t' };
 assert(wa_noise_encrypt(&client,msg,sizeof msg,ct,&cn));
 assert(wa_noise_decrypt(&peer,ct,cn,pt,&pn));
 assert(pn==sizeof msg && memcmp(pt,msg,pn)==0);
 ct[cn-1]^=1;
 assert(!wa_noise_decrypt(&peer,ct,cn,pt,&pn));
 puts("PASS: ClientFinish payload encrypted; transport keys active; tamper rejected");
 return 0;
}
