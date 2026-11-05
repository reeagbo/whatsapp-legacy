#ifndef WA_MESSAGING_H
#define WA_MESSAGING_H
#include "identity.h"
#include "storage.h"
#include "node.h"
#define WA_TEXT_MAX 4000
/* UTF-8 application events, never part of diagnostic logs. */
typedef struct {char kind[16],id[96],jid[128],text[WA_TEXT_MAX+1];int state;uint64_t timestamp;} wa_message_event;
typedef struct wa_messenger wa_messenger;
typedef int (*wa_send_node)(void *,wa_tree *);
wa_messenger *wa_messenger_create(const wa_identity *,const wa_storage *,const char *own_lid,wa_send_node,void *);
void wa_messenger_free(wa_messenger *);
int wa_messenger_start(wa_messenger *);
int wa_messenger_node(wa_messenger *,const wa_node *);
int wa_messenger_send(wa_messenger *,const char *jid,const char *id,const uint8_t *utf8,size_t);
/* Resolve a stored LID to its known phone address; unknown LIDs stay LIDs. */
int wa_messenger_resolve(wa_messenger *,const char *jid,char out[128]);
int wa_messenger_history(wa_messenger *,const char *jid,const char *oldest_id,int from_me,uint64_t oldest_ms,const char *request_id);
int wa_messenger_sync_key(wa_messenger *,const char *,const char *);
struct wa_history;
int wa_messenger_delete_chat(wa_messenger *,const char *,const char *,int,uint64_t,uint64_t,int);
int wa_messenger_write_pending(const wa_messenger *,const char *);
void wa_messenger_delete_snapshot(wa_messenger *,const struct wa_history *);
void wa_messenger_recover_state(wa_messenger *,int low);
void wa_messenger_tick(wa_messenger *,uint64_t now_ms);
const wa_message_event *wa_messenger_event(const wa_messenger *);
void wa_messenger_event_done(wa_messenger *);
/* App calls only after its message database transaction has committed. */
int wa_messenger_stored(wa_messenger *,const char *id,const char *jid,int outgoing);
#endif
