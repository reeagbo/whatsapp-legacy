#ifndef WA_MESSAGING_H
#define WA_MESSAGING_H
#include "identity.h"
#include "storage.h"
#include "node.h"
#define WA_TEXT_MAX 4000
/* UTF-8 application events, never part of diagnostic logs. */
typedef struct {char kind[16],id[96],jid[128],text[WA_TEXT_MAX+1];int state;} wa_message_event;
typedef struct wa_messenger wa_messenger;
typedef int (*wa_send_node)(void *,wa_tree *);
wa_messenger *wa_messenger_create(const wa_identity *,const wa_storage *,const char *own_lid,wa_send_node,void *);
void wa_messenger_free(wa_messenger *);
int wa_messenger_start(wa_messenger *);
int wa_messenger_node(wa_messenger *,const wa_node *);
int wa_messenger_send(wa_messenger *,const char *jid,const char *id,const uint8_t *utf8,size_t);
void wa_messenger_tick(wa_messenger *,uint64_t now_ms);
const wa_message_event *wa_messenger_event(const wa_messenger *);
void wa_messenger_event_done(wa_messenger *);
#endif
