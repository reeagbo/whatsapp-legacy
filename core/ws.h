#ifndef WA_WS_H
#define WA_WS_H
#include <stddef.h>
#include <stdint.h>
#define WA_WS_MAX_PAYLOAD (1024u*1024u)
typedef struct { uint8_t *payload; size_t used, cap; uint8_t opcode; int fin; } wa_ws_message;
int wa_ws_encode(uint8_t opcode, const uint8_t *data, size_t size, const uint8_t mask[4], uint8_t *out, size_t cap, size_t *written);
int wa_ws_decode(const uint8_t *data, size_t size, wa_ws_message *out, size_t *consumed);
int wa_ws_upgrade_request(const uint8_t nonce[16], char *request, size_t capacity, char expected_accept[29]);
int wa_ws_upgrade_validate(const char *headers, size_t length, const char expected_accept[29]);
typedef struct { const uint8_t *ephemeral; size_t ephemeral_len; const uint8_t *server_static; size_t static_len; const uint8_t *payload; size_t payload_len; } wa_server_hello;
int wa_parse_server_hello(const uint8_t *protobuf,size_t len,wa_server_hello *out);
int wa_make_client_hello(const uint8_t ephemeral[32],uint8_t *out,size_t capacity,size_t *written);
#endif
