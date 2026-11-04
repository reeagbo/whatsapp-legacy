#ifndef WA_PAIRING_H
#define WA_PAIRING_H
#include <stddef.h>
#include <stdint.h>
#include "messaging.h"
typedef struct wa_pair_session wa_pair_session;
/* State: -1 error, 1 handshake, 2 awaiting primary identity verification, 3 locally generated code ready,
 * 4 identity verified/restart required (login still pending), 5 existing session authenticated,
 * 6 anonymous channel ready (test probe). */
wa_pair_session *wa_pair_create(const char *credentials_path,
                                const char *phone);
int wa_pair_send_text(wa_pair_session *,const char *jid,const char *id,const uint8_t *utf8,size_t);
int wa_pair_stored(wa_pair_session *,const char *id,const char *jid,int outgoing);
int wa_pair_resolve(wa_pair_session *,const char *jid,char out[128]);
int wa_pair_history(wa_pair_session *,const char *,const char *,int,uint64_t,const char *);
int wa_pair_history_task(wa_pair_session *,unsigned,uint8_t *,size_t,size_t *);
struct wa_history;
struct wa_history *wa_pair_history_open(wa_pair_session *,const uint8_t *,size_t,const uint8_t *,size_t,char [129]);
int wa_pair_history_done(wa_pair_session *,unsigned,const uint8_t *,size_t,uint64_t);
int wa_pair_delete_chat(wa_pair_session *,const char *,const char *,int,uint64_t,uint64_t);
void wa_pair_delete_snapshot(wa_pair_session *,const struct wa_history *);
int wa_pair_sync_key(wa_pair_session *,const char *,const char *);
void wa_pair_tick(wa_pair_session *,uint64_t monotonic_ms);
const wa_message_event *wa_pair_event(const wa_pair_session *);
void wa_pair_event_done(wa_pair_session *);
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
