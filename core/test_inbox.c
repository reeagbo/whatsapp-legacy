#include "inbox.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint8_t data[150000];size_t n;int fail_read,fail_write;} memory_store;
static int get(void *user,const char *key,uint8_t *out,size_t cap,size_t *n) {
 memory_store *s=user;assert(!strcmp(key,"incoming-inbox"));if(s->fail_read)return -1;
 if(!s->n){*n=0;return 0;}if(s->n>cap)return -1;memcpy(out,s->data,s->n);*n=s->n;return 1;
}
static int put(void *user,const char *key,const uint8_t *p,size_t n) {
 memory_store *s=user;assert(!strcmp(key,"incoming-inbox"));if(s->fail_write)return 0;
 assert(n<=sizeof s->data);memcpy(s->data,p,n);s->n=n;return 1;
}
int main(void) {
 memory_store *data=calloc(1,sizeof *data);assert(data);wa_storage io={data,get,put};
 wa_inbox *in=wa_inbox_open(&io);assert(in&&!wa_inbox_count(in));
 wa_message_event e={0};strcpy(e.kind,"message");strcpy(e.id,"synthetic-id");strcpy(e.jid,"12345678@s.whatsapp.net");strcpy(e.text,"Texto conservado: café \xf0\x9f\x8c\x8d");
 data->fail_write=1;assert(!wa_inbox_add(in,&e)&&!wa_inbox_count(in)&&!data->n);
 data->fail_write=0;assert(wa_inbox_add(in,&e)&&wa_inbox_add(in,&e)&&wa_inbox_count(in)==1);
 wa_inbox_free(in);in=wa_inbox_open(&io);assert(in&&wa_inbox_count(in)==1&&!strcmp(wa_inbox_at(in,0)->text,e.text));
 wa_message_event outgoing=e;strcpy(outgoing.kind,"sent-copy");outgoing.state=2;
 assert(wa_inbox_add(in,&outgoing)&&wa_inbox_count(in)==2);
 assert(wa_inbox_confirm(in,e.id,e.jid,1)&&wa_inbox_count(in)==1);
 data->fail_write=1;assert(!wa_inbox_confirm(in,e.id,e.jid,0)&&wa_inbox_count(in)==1);
 data->fail_write=0;assert(wa_inbox_confirm(in,e.id,e.jid,0)&&wa_inbox_confirm(in,e.id,e.jid,0));
 wa_inbox_free(in);in=wa_inbox_open(&io);assert(in&&!wa_inbox_count(in));
 for(unsigned i=0;i<WA_INBOX_LIMIT;i++){snprintf(e.id,sizeof e.id,"pending-%u",i);assert(wa_inbox_add(in,&e));}
 strcpy(e.id,"over-limit");assert(!wa_inbox_add(in,&e)&&wa_inbox_count(in)==WA_INBOX_LIMIT);
 wa_inbox_free(in);data->fail_read=1;assert(!wa_inbox_open(&io));data->fail_read=0;
 data->data[0]^=1;size_t original=data->n;assert(!wa_inbox_open(&io)&&data->n==original);
 free(data);puts("PASS: durable inbox reopen, idempotent replay/commit, write/read failure, bounded queue and corruption preservation");return 0;
}
