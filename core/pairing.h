#ifndef WA_PAIRING_H
#define WA_PAIRING_H
#include <stddef.h>
#include <stdint.h>
typedef struct wa_pair_session wa_pair_session;
/* State: -1 error, 1 handshake, 2 awaiting primary identity verification, 3 locally generated code ready,
 * 4 pairing authenticated/restart requested, 5 existing session authenticated,
 * 6 anonymous channel ready (test probe). */
wa_pair_session *wa_pair_create(const char *credentials_path,
                                const char *phone);
void wa_pair_free(wa_pair_session *s);
int wa_pair_start(wa_pair_session *s);
int wa_pair_feed(wa_pair_session *s, const uint8_t *bytes, size_t n);
int wa_pair_ping(wa_pair_session *s);
const uint8_t *wa_pair_output(wa_pair_session *s, size_t *n);
void wa_pair_output_done(wa_pair_session *s);
int wa_pair_state(const wa_pair_session *s);
const char *wa_pair_status(const wa_pair_session *s);
const char *wa_pair_code(const wa_pair_session *s);
/* Safe ASCII diagnostic metadata, bounded to 32 KiB until drained. */
const char *wa_pair_debug_log(const wa_pair_session *s);
void wa_pair_debug_clear(wa_pair_session *s);
#endif
