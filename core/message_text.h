#ifndef WA_MESSAGE_TEXT_H
#define WA_MESSAGE_TEXT_H
#include "wire.h"
#include "messaging.h"

/* A text reply carries ExtendedTextMessage.contextInfo.quotedMessage.
 * Unwrap own-device copies, then read only the directly quoted text. */
static inline int wa_quoted_text(const uint8_t *data,size_t size,wa_slice *quote){
 wa_slice wrapper,inner,extended,context,message;
 for(unsigned depth=0;depth<3&&wa_pb_bytes_field(data,size,31,&wrapper);depth++){
  if(!wa_pb_bytes_field(wrapper.ptr,wrapper.len,2,&inner))return 0;
  data=inner.ptr;size=inner.len;
 }
 if(!wa_pb_bytes_field(data,size,6,&extended)||
    !wa_pb_bytes_field(extended.ptr,extended.len,17,&context)||
    !wa_pb_bytes_field(context.ptr,context.len,3,&message))return 0;
 if(!wa_pb_bytes_field(message.ptr,message.len,1,quote)&&
    !(wa_pb_bytes_field(message.ptr,message.len,6,&extended)&&wa_pb_bytes_field(extended.ptr,extended.len,1,quote)))return 0;
 if(quote->len>WA_QUOTE_MAX){
  quote->len=WA_QUOTE_MAX;
  while(quote->len&&(quote->ptr[quote->len]&0xc0)==0x80)quote->len--;
 }
 return quote->len!=0;
}
#endif
