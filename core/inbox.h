#ifndef WA_INBOX_H
#define WA_INBOX_H
#include "messaging.h"
/* Durable hand-off between Signal and an application message database.
 * Serialized with explicit byte lengths, independent of C ABI or platform. */
#define WA_INBOX_LIMIT 32
#define WA_INBOX_STORAGE_ERROR (-20001)
typedef struct wa_inbox wa_inbox;
wa_inbox *wa_inbox_open(const wa_storage *);
void wa_inbox_free(wa_inbox *);
size_t wa_inbox_count(const wa_inbox *);
const wa_message_event *wa_inbox_at(const wa_inbox *,size_t);
/* Store before committing Signal ratchet state. Identical repeats are harmless. */
int wa_inbox_add(wa_inbox *,const wa_message_event *);
/* Only after the application database has committed this message. */
int wa_inbox_confirm(wa_inbox *,const char *id,const char *jid,int outgoing);
#endif
