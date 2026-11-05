#ifndef WA_HISTORY_H
#define WA_HISTORY_H
#include "storage.h"
#include "messaging.h"
#define WA_HISTORY_PATCH_TASKS 8
#define WA_HISTORY_TASKS (WA_HISTORY_PATCH_TASKS + 4)
#define WA_HISTORY_NOTIFICATION_MAX (1024u*1024u)
#define WA_HISTORY_DOWNLOAD_MAX (8u*1024u*1024u)
#define WA_HISTORY_PLAIN_MAX (32u*1024u*1024u)
struct wa_history;
/* Notification bytes are saved before Signal commits. No network/platform here. */
int wa_history_save(const wa_storage *,const uint8_t *,size_t);
int wa_history_task(const wa_storage *,unsigned,uint8_t *,size_t,size_t *);
int wa_state_save_keys(const wa_storage *,const uint8_t *,size_t);
wa_tree *wa_state_query(const wa_storage *,const char *id,int force_snapshot,int low);
int wa_state_collect(const wa_storage *,const wa_node *,const char *snapshot_request,int low);
wa_tree *wa_state_query_after(uint64_t,const char *,int);
int wa_history_write_more(const struct wa_history *);
int wa_history_save_write_base(const struct wa_history *,const wa_storage *);
uint64_t wa_state_version(const wa_storage *,int low);
int wa_history_save_mutation(const wa_storage *,const char *,const char *,const char *,int,uint64_t,const char *);
int wa_history_done(const wa_storage *,unsigned,const uint8_t *,size_t);
/* Only direct HTTPS paths under WhatsApp's media host are exposed to the adapter. */
int wa_history_response(const uint8_t *,size_t);
int wa_history_path(const uint8_t *,size_t,char *,size_t);
typedef struct wa_history wa_history;
void wa_history_write_diagnostic(const uint8_t *,size_t,char *,size_t);
wa_history *wa_history_open_diagnostic(const wa_storage *,const uint8_t *,size_t,const uint8_t *,size_t,char [129],const char **);
wa_history *wa_history_open_sync(const wa_storage *,const uint8_t *,size_t,const uint8_t *,size_t,char missing[129]);
const char *wa_history_delete_request(const wa_history *);
wa_tree *wa_history_delete_patch(const wa_history *,const wa_storage *,const char *,const char *,const char *,int,uint64_t,uint64_t,int archive);
uint64_t wa_history_version(const wa_history *);
uint64_t wa_history_job_order(const uint8_t *,size_t);
int wa_history_job_kind(const uint8_t *,size_t);
int wa_history_commit(const wa_storage *,unsigned,const uint8_t *,size_t,uint64_t);
wa_history *wa_history_open(const uint8_t *notification,size_t,const uint8_t *encrypted,size_t);
/* 1 record; 0 end; -1 invalid. kind=alias/chat/name/message/sent-copy.
 * id=lid for aliases, jid=pn. Dates are original Unix milliseconds. */
int wa_history_next(wa_history *,wa_message_event *,uint64_t *timestamp);
void wa_history_free(wa_history *);
#endif
