#ifndef WA_HISTORY_H
#define WA_HISTORY_H
#include "storage.h"
#include "messaging.h"
#define WA_HISTORY_TASKS 8
#define WA_HISTORY_NOTIFICATION_MAX (1024u*1024u)
#define WA_HISTORY_DOWNLOAD_MAX (8u*1024u*1024u)
#define WA_HISTORY_PLAIN_MAX (32u*1024u*1024u)
/* Notification bytes are saved before Signal commits. No network/platform here. */
int wa_history_save(const wa_storage *,const uint8_t *,size_t);
int wa_history_task(const wa_storage *,unsigned,uint8_t *,size_t,size_t *);
int wa_history_done(const wa_storage *,unsigned,const uint8_t *,size_t);
/* Only direct HTTPS paths under WhatsApp's media host are exposed to the adapter. */
int wa_history_response(const uint8_t *,size_t);
int wa_history_path(const uint8_t *,size_t,char *,size_t);
typedef struct wa_history wa_history;
wa_history *wa_history_open(const uint8_t *notification,size_t,const uint8_t *encrypted,size_t);
/* 1 record; 0 end; -1 invalid. kind=alias/chat/name/message/sent-copy.
 * id=lid for aliases, jid=pn. Dates are original Unix milliseconds. */
int wa_history_next(wa_history *,wa_message_event *,uint64_t *timestamp);
void wa_history_free(wa_history *);
#endif
