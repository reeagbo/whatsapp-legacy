#include "ws.h"
#include "wire.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

int wa_ws_encode(uint8_t opcode,const uint8_t *data,size_t size,const uint8_t mask[4],uint8_t *out,size_t cap,size_t *written){
 if(size>WA_WS_MAX_PAYLOAD || !mask || (size && !data) || !out || !written) return 0;
 if(opcode!=2 && opcode!=8 && opcode!=9 && opcode!=10) return 0;
 if(opcode>=8 && size>125)return 0;
 size_t h=(size<126)?2:((size<=65535)?4:10);
 if(cap<h+4 || size>cap-h-4)return 0;
 out[0]=0x80|opcode;
 if(size<126)out[1]=0x80|(uint8_t)size;
 else if(size<=65535){out[1]=0xfe;out[2]=(uint8_t)(size>>8);out[3]=(uint8_t)size;}
 else {out[1]=0xff;for(int i=0;i<8;i++)out[2+i]=(uint8_t)(size>>(56-8*i));}
 memcpy(out+h,mask,4);
 for(size_t i=0;i<size;i++)out[h+4+i]=data[i]^mask[i&3];
 *written=h+4+size;return 1;
}
// Decode a complete server-to-client frame. Only unmasked server frames allowed; reject fragmentation for initial POC.
int wa_ws_decode(const uint8_t *data,size_t size,wa_ws_message *out,size_t *consumed){
 if(!data||!out||!consumed||size<2)return 0;
 if((data[0]&0x70)||!(data[0]&0x80)||(data[1]&0x80))return -1;
 uint8_t op=data[0]&15;if(op!=2&&op!=8&&op!=9&&op!=10)return -1;
 size_t len=data[1]&127,h=2;
 if(len==126){if(size<4)return 0;len=((size_t)data[2]<<8)|data[3];h=4;if(len<126)return -1;}
 else if(len==127){if(size<10)return 0;if(data[2]&0x80)return -1;len=0;for(int i=2;i<10;i++)len=(len<<8)|data[i];h=10;if(len<=65535)return -1;}
 if(len>WA_WS_MAX_PAYLOAD||(op>=8&&len>125))return -1;
 if(size<h+len)return 0;
 if(len>out->cap)return -1;
 if(len)memcpy(out->payload,data+h,len);
 out->used=len;out->opcode=op;out->fin=1;*consumed=h+len;return 1;
}
static int eqi(const char *a,size_t an,const char *b){size_t n=strlen(b);if(an!=n)return 0;for(size_t i=0;i<n;i++)if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i]))return 0;return 1;}
int wa_ws_upgrade_request(const uint8_t nonce[16],char *request,size_t capacity,char expected_accept[29]){
 char key[25],joined[64];unsigned char digest[20];
 if(!nonce||!request||!expected_accept)return 0;
 if(EVP_EncodeBlock((unsigned char*)key,nonce,16)!=24)return 0;
 int sz=snprintf(joined,sizeof joined,"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
 if(sz!=60)return 0;
 SHA1((const unsigned char*)joined,(size_t)sz,digest);
 if(EVP_EncodeBlock((unsigned char*)expected_accept,digest,20)!=28)return 0;
 int n=snprintf(request,capacity,"GET /ws/chat HTTP/1.1\r\nHost: web.whatsapp.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nOrigin: https://web.whatsapp.com\r\n\r\n",key);
 return n>0&&(size_t)n<capacity;
}
int wa_ws_upgrade_validate(const char *headers,size_t length,const char expected_accept[29]){
 if(!headers||!expected_accept||length>8192||length<16)return 0;
 size_t end=0;while(end+3<length&&memcmp(headers+end,"\r\n\r\n",4))end++;
 if(end+3>=length)return 0;
 size_t first=0;while(first+1<end&&memcmp(headers+first,"\r\n",2))first++;
 if(first>=end||first<12||strncmp(headers,"HTTP/1.1 101",12))return 0;
 int upgrade=0,connection=0,accept=0;
 size_t pos=first+2;
 while(pos<end){size_t line=pos;while(pos+1<end+2&&memcmp(headers+pos,"\r\n",2))pos++;
 if(pos>end)break;
 size_t colon=line;while(colon<pos&&headers[colon]!=':')colon++;
 if(colon<pos){size_t v=colon+1;while(v<pos&&(headers[v]==' '||headers[v]=='\t'))v++;
 if(eqi(headers+line,colon-line,"Upgrade")&&eqi(headers+v,pos-v,"websocket"))upgrade=1;
 if(eqi(headers+line,colon-line,"Connection")){
  // comma-separated Connection tokens
  size_t t=v;while(t<pos){while(t<pos&&(headers[t]==' '||headers[t]=='\t'||headers[t]==','))t++;size_t a=t;while(t<pos&&headers[t]!=',')t++;size_t b=t;while(b>a&&(headers[b-1]==' '||headers[b-1]=='\t'))b--;if(eqi(headers+a,b-a,"Upgrade"))connection=1;}
 }
 if(eqi(headers+line,colon-line,"Sec-WebSocket-Accept")&&(pos-v)==28&&!memcmp(headers+v,expected_accept,28))accept=1;
 }
 pos+=2;
 }
 return upgrade&&connection&&accept;
}
int wa_make_client_hello(const uint8_t ephemeral[32],uint8_t *out,size_t cap,size_t *written){
 uint8_t nested[48];size_t inner=0;
 if(!wa_pb_write_bytes(1,ephemeral,32,nested,sizeof nested,&inner))return 0;
 return wa_pb_write_bytes(2,nested,inner,out,cap,written);
}

int wa_parse_server_hello(const uint8_t *protobuf,size_t len,wa_server_hello *out){
 if(!protobuf||!out)return 0;
 wa_slice outer,e,st,p;
 if(!wa_pb_bytes_field(protobuf,len,3,&outer))return 0;
 if(!wa_pb_bytes_field(outer.ptr,outer.len,1,&e)||e.len!=32)return 0;
 if(!wa_pb_bytes_field(outer.ptr,outer.len,2,&st)||st.len!=48)return 0;
 if(!wa_pb_bytes_field(outer.ptr,outer.len,3,&p)||p.len<16||p.len>65536)return 0;
 out->ephemeral=e.ptr;out->ephemeral_len=e.len;out->server_static=st.ptr;out->static_len=st.len;out->payload=p.ptr;out->payload_len=p.len;
 return 1;
}
