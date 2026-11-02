#ifndef WA_NOISE_H
#define WA_NOISE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
 uint8_t hash[32],salt[32],key[32],read_key[32],write_key[32];
 uint32_t counter,rcounter,wcounter;
 int transport;
 uint8_t ephemeral_private[32],ephemeral_public[32];
} wa_noise;
int wa_noise_init(wa_noise *n);
int wa_noise_auth(wa_noise *n,const uint8_t *data,size_t len);
int wa_noise_mix(wa_noise *n,const uint8_t *data,size_t len);
int wa_noise_dh(const uint8_t priv[32],const uint8_t pub[32],uint8_t out[32]);
int wa_noise_decrypt(wa_noise *n,const uint8_t *cipher,size_t cipher_len,uint8_t *plain,size_t *plain_len);
int wa_noise_encrypt(wa_noise *n,const uint8_t *plain,size_t plain_len,uint8_t *cipher,size_t *cipher_len);
int wa_noise_transport(wa_noise *n);
int wa_noise_frame(const uint8_t *data,size_t len,int intro,uint8_t *out,size_t cap,size_t *outlen);
int wa_noise_unframe(const uint8_t *input,size_t len,const uint8_t **frame,size_t *frame_len,size_t *consumed);
#endif
