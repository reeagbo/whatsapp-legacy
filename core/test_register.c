#include "register.h"
#include "wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 uint8_t ip[32]={1},sp[32]={2},sig[64]={3},out[4096];size_t n=0;
 wa_registration r={12345,27,ip,sp,sig,"Mac OS"};
 assert(wa_registration_payload(&r,out,sizeof out,&n));
 wa_slice pair,agent,dev,raw;
 assert(wa_pb_bytes_field(out,n,19,&pair));
 assert(wa_pb_bytes_field(pair.ptr,pair.len,1,&raw) && raw.len==4 && raw.ptr[2]==0x30 && raw.ptr[3]==0x39);
 assert(wa_pb_bytes_field(pair.ptr,pair.len,5,&raw) && raw.len==32 && !memcmp(raw.ptr,sp,32));
 assert(wa_pb_bytes_field(pair.ptr,pair.len,6,&raw) && raw.len==64);
 assert(wa_pb_bytes_field(pair.ptr,pair.len,8,&dev));
 assert(wa_pb_bytes_field(dev.ptr,dev.len,1,&raw) && raw.len==6 && !memcmp(raw.ptr,"Mac OS",6));
 assert(wa_pb_bytes_field(out,n,5,&agent));
 assert(!wa_registration_payload(&r,out,12,&n));
 puts("PASS: ClientPayload registration fields and sizes validated");
}
