#ifndef WA_IDENTITY_H
#define WA_IDENTITY_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
  uint8_t noise_private[32], noise_public[32], identity_private[32],
      identity_public[32];
  uint8_t prekey_private[32], prekey_public[32], prekey_signature[64],
      adv_secret[32];
  uint32_t registration_id;
  char jid[128];
  uint8_t account[2048];
  size_t account_len;
} wa_identity;
int wa_keypair(uint8_t private_key[32], uint8_t public_key[32]);
int wa_sign(const uint8_t private_key[32], const uint8_t *data, size_t n,
            uint8_t signature[64]);
int wa_verify(const uint8_t public_key[32], const uint8_t *data, size_t n,
              const uint8_t signature[64]);
int wa_identity_generate(wa_identity *id);
int wa_identity_validate(const wa_identity *id);
int wa_identity_open(const char *path, wa_identity *id);
int wa_identity_save(const char *path, const wa_identity *id);
int wa_hkdf(const uint8_t *data, size_t n, const uint8_t *salt, size_t salt_len,
            const char *info, uint8_t key[32]);
int wa_pairing_wrap(const char code[9], const uint8_t public_key[32],
                    uint8_t out[80]);
int wa_pairing_unwrap(const char code[9], const uint8_t in[80],
                      uint8_t public_key[32]);
int wa_pairing_finish(const char code[9], const uint8_t ephemeral_private[32],
                      const wa_identity *id, const uint8_t wrapped[80],
                      const uint8_t primary_public[32], uint8_t bundle[156],
                      uint8_t adv_secret[32]);
#endif
