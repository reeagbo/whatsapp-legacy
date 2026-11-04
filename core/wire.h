#ifndef WA_WIRE_H
#define WA_WIRE_H
#include <stdint.h>
#include <stddef.h>
typedef struct { const uint8_t *ptr;size_t len;} wa_slice;
// HandshakeMessage { clientHello=2? } field numbers are encapsulation-specific.
// This module provides strictly generic protobuf wire-level parsing and encoding.
int wa_pb_varint(const uint8_t *data,size_t size,size_t *pos,uint64_t *value);
int wa_pb_varint_field(const uint8_t *,size_t,unsigned,uint64_t *);
int wa_pb_bytes_field(const uint8_t *data,size_t len,unsigned field,wa_slice *result);
int wa_pb_write_bytes(unsigned field,const uint8_t *value,size_t n,uint8_t *out,size_t cap,size_t *written);
#endif
