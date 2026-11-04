#include "wire.h"
#include <string.h>
int wa_pb_varint(const uint8_t *b,size_t size,size_t *pos,uint64_t *out){
 uint64_t r=0;int shift=0;while(*pos<size&&shift<70){uint8_t x=b[(*pos)++];if(shift==63&&(x&0xfe))return 0;if(shift>63)return 0;r|=(uint64_t)(x&127)<<shift;if(!(x&128)){*out=r;return 1;}shift+=7;}return 0;
}
static int lookup(const uint8_t *b,size_t n,unsigned field,wa_slice *out,uint64_t *number){
 size_t p=0;while(p<n){uint64_t tag=0,value=0;if(!wa_pb_varint(b,n,&p,&tag)||!(tag>>3))return 0;
 unsigned wire=tag&7;if(wire==2){if(!wa_pb_varint(b,n,&p,&value)||value>n-p)return 0;if(out&&(tag>>3)==field){out->ptr=b+p;out->len=(size_t)value;return 1;}p+=(size_t)value;}
 else if(wire==0){if(!wa_pb_varint(b,n,&p,&value))return 0;if(number&&(tag>>3)==field){*number=value;return 1;}}
 else if(wire==1){if(n-p<8)return 0;p+=8;}
 else if(wire==5){if(n-p<4)return 0;p+=4;}
 else return 0;
 }return 0;
}
int wa_pb_bytes_field(const uint8_t *b,size_t n,unsigned f,wa_slice *out){return lookup(b,n,f,out,NULL);}
int wa_pb_varint_field(const uint8_t *b,size_t n,unsigned f,uint64_t *out){return lookup(b,n,f,NULL,out);}
static size_t putvar(uint64_t n,uint8_t *out){size_t i=0;do{uint8_t x=n&127;n>>=7;out[i++]=x|(n?128:0);}while(n);return i;}
int wa_pb_write_bytes(unsigned f,const uint8_t *data,size_t len,uint8_t *out,size_t cap,size_t *written){
 if(!f||f>0x1fffffff)return 0;
 uint8_t v[20];size_t a=putvar(((uint64_t)f<<3)|2,v),b=putvar(len,v+a);if(cap<a+b||len>cap-a-b)return 0;
 memcpy(out,v,a+b);if(len)memcpy(out+a+b,data,len);*written=a+b+len;return 1;
}
