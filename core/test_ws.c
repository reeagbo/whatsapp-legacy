#include "ws.h"
#include "wire.h"
#include "noise.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 uint8_t nonce[16]="the sample nonce",mask[4]={1,2,3,4},out[256],ep[32],hello[80];char req[1024],accept[29];size_t n=0,p=0;
 assert(wa_ws_upgrade_request(nonce,req,sizeof req,accept));
 char response[512];snprintf(response,sizeof response,"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);
 assert(wa_ws_upgrade_validate(response,strlen(response),accept));
 response[strlen(response)-4]='X';assert(!wa_ws_upgrade_validate(response,strlen(response),accept));
 assert(wa_ws_encode(2,(uint8_t*)"hello",5,mask,out,sizeof out,&n));
 assert(n==11 && out[1]==0x85 && out[6]==('h'^1));
 uint8_t server[]={0x82,0x05,'h','e','l','l','o'};uint8_t receive[8]={0};wa_ws_message msg={receive,0,sizeof receive,0,0};
 assert(wa_ws_decode(server,3,&msg,&p)==0);
 assert(wa_ws_decode(server,sizeof server,&msg,&p)==1&&p==7&&msg.used==5&&!memcmp(receive,"hello",5));
 server[1]|=0x80;assert(wa_ws_decode(server,sizeof server,&msg,&p)==-1);
 for(int i=0;i<32;i++)ep[i]=(uint8_t)i;
 assert(wa_make_client_hello(ep,hello,sizeof hello,&n));
 wa_slice outer,inner;assert(wa_pb_bytes_field(hello,n,2,&outer));assert(wa_pb_bytes_field(outer.ptr,outer.len,1,&inner));assert(inner.len==32&&!memcmp(ep,inner.ptr,32));
 // Decode a synthetic ServerHello using actual Protobuf field numbers.
 uint8_t stat[48]={0},payload[16]={0},nested[128],server_hello[150];size_t a=0,b=0,c=0,slen=0;
 assert(wa_pb_write_bytes(1,ep,32,nested,sizeof nested,&a));
 assert(wa_pb_write_bytes(2,stat,48,nested+a,sizeof nested-a,&b));a+=b;
 assert(wa_pb_write_bytes(3,payload,16,nested+a,sizeof nested-a,&c));a+=c;
 assert(wa_pb_write_bytes(3,nested,a,server_hello,sizeof server_hello,&slen));
 wa_server_hello parsed;assert(wa_parse_server_hello(server_hello,slen,&parsed));
 assert(!memcmp(parsed.ephemeral,ep,32)&&parsed.static_len==48&&parsed.payload_len==16);
 server_hello[slen-1]=0; // parser checks lengths and presence, not the certificate contents
 wa_noise noise;assert(wa_noise_init(&noise));
 uint8_t frame[128];size_t fn=0;assert(wa_noise_frame(hello,n,1,frame,sizeof frame,&fn));assert(fn==n+7&&memcmp(frame,"WA\x06\x03",4)==0);
 puts("PASS: WebSocket handshake RFC6455 / masking / framing / Protobuf ClientHello / ServerHello parsing / WA framing");return 0;
}
