#ifndef WA_REGISTER_H
#define WA_REGISTER_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
 uint32_t registration_id;
 uint32_t signed_prekey_id;
 const uint8_t *identity_public;  /* 32-byte raw Curve25519 key */
 const uint8_t *prekey_public;    /* 32-byte raw Curve25519 key */
 const uint8_t *prekey_signature; /* 64-byte signature */
 const char *operating_system;
} wa_registration;
/* Builds a ClientPayload equivalent to Baileys' initial desktop registration.
 * Caller is responsible for generating and storing valid signed key material.
 * Does not modify Signal state; does not contact any server. */
int wa_registration_payload(const wa_registration *input, uint8_t *out,size_t cap,size_t *len);
#endif
