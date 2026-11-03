#ifndef WA_SIGNAL_ENGINE_H
#define WA_SIGNAL_ENGINE_H
#include "identity.h"
#include "storage.h"
typedef struct wa_signal wa_signal;
wa_signal *wa_signal_create(const wa_identity *,const wa_storage *);
void wa_signal_free(wa_signal *);
int wa_signal_prekey(wa_signal *,uint32_t id,uint8_t public_key[32]);
int wa_signal_has(wa_signal *,const char *jid);
int wa_signal_bundle(wa_signal *,const char *jid,uint32_t registration,
 const uint8_t identity[32],uint32_t signed_id,const uint8_t signed_key[32],
 const uint8_t signature[64],uint32_t prekey_id,const uint8_t *prekey);
int wa_signal_encrypt(wa_signal *,const char *jid,const uint8_t *,size_t,
 uint8_t *,size_t,size_t *,int *prekey_message);
int wa_signal_decrypt(wa_signal *,const char *jid,int prekey_message,const uint8_t *,size_t,
 uint8_t *,size_t,size_t *);
/* Invoked after authentication/decryption and before ratchet state is saved.
 * A negative return aborts the ratchet commit. Calls must be serialized. */
typedef int (*wa_signal_plaintext_callback)(void *,const uint8_t *,size_t);
int wa_signal_decrypt_with_callback(wa_signal *,const char *jid,int prekey_message,
 const uint8_t *,size_t,uint8_t *,size_t,size_t *,wa_signal_plaintext_callback,void *);
#endif
