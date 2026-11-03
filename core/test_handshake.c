#include "handshake.h"
#include "wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 wa_noise state;assert(wa_noise_init(&state));
 uint8_t ephemeral[32]={1},fake_static[48]={0},fake_payload[32]={0},local_pr[32]={0},local_pub[32]={0},out[48];
 wa_server_hello invalid={ephemeral,32,fake_static,48,fake_payload,sizeof fake_payload};
 assert(!wa_process_server_hello(&state,&invalid,local_pr,local_pub,out));
 const char *failed_stage=NULL;
 assert(!wa_process_server_hello_ex(NULL,&invalid,local_pr,local_pub,out,&failed_stage));
 assert(failed_stage&&!strcmp(failed_stage,"validate_input"));
 assert(wa_noise_init(&state));
 assert(!wa_process_server_hello_ex(&state,&invalid,local_pr,local_pub,out,&failed_stage));
 assert(failed_stage&&!strcmp(failed_stage,"ephemeral_ecdh"));
 wa_noise peer;assert(wa_noise_init(&peer)&&wa_noise_init(&state));
 invalid.ephemeral=peer.ephemeral_public;
 assert(!wa_process_server_hello_ex(&state,&invalid,local_pr,local_pub,out,&failed_stage));
 assert(failed_stage&&!strcmp(failed_stage,"decrypt_server_static"));
 uint8_t finish[256],encrypted[48]={0};size_t n;
 assert(wa_make_client_finish(encrypted,fake_payload,sizeof fake_payload,finish,sizeof finish,&n));
 wa_slice outer,s,p;assert(wa_pb_bytes_field(finish,n,4,&outer));
 assert(wa_pb_bytes_field(outer.ptr,outer.len,1,&s)&&s.len==48);
 assert(wa_pb_bytes_field(outer.ptr,outer.len,2,&p)&&p.len==sizeof fake_payload);
 puts("PASS: invalid ServerHello rejected; ClientFinish Protobuf encoded");return 0;
}
