#include "inbox.h"
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_MAX (17u + 127u + 95u + 1215u + WA_TEXT_MAX)
#define INBOX_BYTES (10u + WA_INBOX_LIMIT * RECORD_MAX + SHA256_DIGEST_LENGTH)
struct wa_inbox {
  wa_storage io;
  size_t count;
  wa_message_event records[WA_INBOX_LIMIT];
};
static void u16(uint8_t *out,size_t n) {out[0]=(uint8_t)(n>>8);out[1]=(uint8_t)n;}
static size_t read16(const uint8_t *p) {return ((size_t)p[0]<<8)|p[1];}
static int is_record(const wa_message_event *e) {
  return e && memchr(e->kind,0,sizeof e->kind)&&memchr(e->id,0,sizeof e->id)&&
    memchr(e->author,0,sizeof e->author)&&memchr(e->jid,0,sizeof e->jid)&&memchr(e->text,0,sizeof e->text)&&(!strcmp(e->kind,"message")||!strcmp(e->kind,"sent-copy")) &&
    e->id[0]&&e->jid[0]&&e->text[0]&&
    strlen(e->id)<sizeof e->id&&strlen(e->jid)<sizeof e->jid&&strlen(e->text)<=WA_TEXT_MAX;
}
static int save(wa_inbox *in,const wa_message_event *extra,size_t omit) {
  uint8_t *out=malloc(INBOX_BYTES);if(!out)return 0;
  memcpy(out,"WAINBOX3",8);size_t count=in->count+(extra?1:0)-(omit<in->count?1:0),used=10;
  u16(out+8,count);
  for(size_t j=0;j<in->count+(extra?1:0);j++) {
    if(j==omit)continue;
    const wa_message_event *e=j<in->count?in->records+j:extra;
    if(!is_record(e)){OPENSSL_cleanse(out,INBOX_BYTES);free(out);return 0;}
    size_t a=strlen(e->id),b=strlen(e->jid),c=strlen(e->text),d=strlen(e->author);
    if(used+17+a+b+c+d>INBOX_BYTES-SHA256_DIGEST_LENGTH){OPENSSL_cleanse(out,INBOX_BYTES);free(out);return 0;}
    out[used]=!strcmp(e->kind,"sent-copy")?1:0;
    u16(out+used+1,a);u16(out+used+3,b);u16(out+used+5,c);used+=7;
    for(unsigned i=0;i<8;i++)out[used++]=(uint8_t)(e->timestamp>>(56-8*i));
    u16(out+used,d);used+=2;memcpy(out+used,e->author,d);used+=d;
    memcpy(out+used,e->id,a);used+=a;memcpy(out+used,e->jid,b);used+=b;memcpy(out+used,e->text,c);used+=c;
  }
  int ok=SHA256(out,used,out+used)!=NULL;
  if(ok)ok=in->io.put(in->io.user,"incoming-inbox",out,used+SHA256_DIGEST_LENGTH);
  OPENSSL_cleanse(out,INBOX_BYTES);free(out);return ok;
}
wa_inbox *wa_inbox_open(const wa_storage *io) {
  if(!io||!io->get||!io->put)return NULL;
  wa_inbox *in=calloc(1,sizeof *in);uint8_t *raw=malloc(INBOX_BYTES);
  if(!in||!raw){free(in);free(raw);return NULL;}in->io=*io;
  size_t n=0;int result=io->get(io->user,"incoming-inbox",raw,INBOX_BYTES,&n);
  int ok=result==0;
  uint8_t digest[SHA256_DIGEST_LENGTH];
  if(result==1&&n>=10+SHA256_DIGEST_LENGTH&&n<=INBOX_BYTES&&
     SHA256(raw,n-SHA256_DIGEST_LENGTH,digest)&&
     !CRYPTO_memcmp(digest,raw+n-SHA256_DIGEST_LENGTH,SHA256_DIGEST_LENGTH)&&
     (!memcmp(raw,"WAINBOX1",8)||!memcmp(raw,"WAINBOX2",8)||!memcmp(raw,"WAINBOX3",8))) {
    n-=SHA256_DIGEST_LENGTH;
    int version2=raw[7]>='2',version3=raw[7]=='3';
    size_t count=read16(raw+8),p=10;ok=count<=WA_INBOX_LIMIT;
    for(size_t j=0;ok&&j<count;j++) {
      if(n-p<(version2?15u:7u)){ok=0;break;}
      unsigned own=raw[p];size_t a=read16(raw+p+1),b=read16(raw+p+3),c=read16(raw+p+5);p+=7;
      uint64_t timestamp=0;
      if(version2)for(unsigned i=0;i<8;i++)timestamp=(timestamp<<8)|raw[p++];
      if(own>1||!a||a>=96||!b||b>=128||!c||c>WA_TEXT_MAX||a+b+c>n-p){ok=0;break;}
      wa_message_event *e=in->records+j;
      if(version3){if(n-p<2){ok=0;break;}size_t d=read16(raw+p);p+=2;if(d>=sizeof e->author||d>n-p){ok=0;break;}memcpy(e->author,raw+p,d);p+=d;if(strlen(e->author)!=d){ok=0;break;}}
      if(a+b+c>n-p){ok=0;break;}
      strcpy(e->kind,own?"sent-copy":"message");e->state=own?2:0;e->timestamp=timestamp;
      memcpy(e->id,raw+p,a);p+=a;memcpy(e->jid,raw+p,b);p+=b;memcpy(e->text,raw+p,c);p+=c;
      if(strlen(e->id)!=a||strlen(e->jid)!=b||strlen(e->text)!=c){ok=0;break;}
      for(size_t k=0;k<j;k++)if(!strcmp(in->records[k].id,e->id)&&!strcmp(in->records[k].jid,e->jid)&&!strcmp(in->records[k].kind,e->kind)){ok=0;break;}
    }
    if(ok&&p==n)in->count=count;else ok=0;
  }
  OPENSSL_cleanse(raw,INBOX_BYTES);free(raw);
  if(!ok){wa_inbox_free(in);return NULL;}return in;
}
void wa_inbox_free(wa_inbox *in) {if(in){OPENSSL_cleanse(in,sizeof *in);free(in);}}
size_t wa_inbox_count(const wa_inbox *in) {return in?in->count:0;}
const wa_message_event *wa_inbox_at(const wa_inbox *in,size_t j) {return in&&j<in->count?in->records+j:NULL;}
int wa_inbox_add(wa_inbox *in,const wa_message_event *e) {
  if(!in||!is_record(e))return 0;
  for(size_t j=0;j<in->count;j++)if(!strcmp(in->records[j].id,e->id)&&!strcmp(in->records[j].jid,e->jid)&&!strcmp(in->records[j].kind,e->kind))
    return !strcmp(in->records[j].text,e->text);
  if(in->count==WA_INBOX_LIMIT||!save(in,e,WA_INBOX_LIMIT))return 0;
  in->records[in->count++]=*e;return 1;
}
int wa_inbox_confirm(wa_inbox *in,const char *id,const char *jid,int outgoing) {
  if(!in||!id||!jid)return 0;
  for(size_t j=0;j<in->count;j++)if(!strcmp(in->records[j].id,id)&&!strcmp(in->records[j].jid,jid)&&!strcmp(in->records[j].kind,outgoing?"sent-copy":"message")) {
    if(!save(in,NULL,j))return 0;
    in->count--;memmove(in->records+j,in->records+j+1,(in->count-j)*sizeof *in->records);
    OPENSSL_cleanse(in->records+in->count,sizeof *in->records);return 1;
  }
  return 1; /* Idempotent acknowledgement of an already committed item. */
}
