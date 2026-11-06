#define _POSIX_C_SOURCE 200809L
#include "messaging.c"
#include "storage_posix.h"
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
/* Synthetic peers, isolated storage, no network or phone/account used. */
typedef struct {uint8_t bytes[32768];size_t n;int sends;} capture;
static int capture_node(void *u,wa_tree *t){capture *c=u;assert(wa_node_encode(t->root,c->bytes,sizeof c->bytes,&c->n));c->sends++;return 1;}
static wa_tree *captured(capture *c){wa_tree *t=wa_node_decode(c->bytes,c->n);assert(t);return t;}
static void result(wa_messenger *m,const char *id){wa_tree *t=iq("result",id,NULL);assert(t&&wa_messenger_node(m,t->root));wa_tree_free(t);}
static void user(wa_tree *t,wa_node *list,const char *pn,const char *lid,int self){
 wa_node *u=wa_node_add(t,list,"user");assert(u&&wa_node_attr(u,"jid",pn));wa_node *l=wa_node_add(t,u,"lid");assert(l&&wa_node_attr(l,"val",lid));
 wa_node *d=wa_node_add(t,u,"devices"),*dl=wa_node_add(t,d,"device-list"),*a=wa_node_add(t,dl,"device");assert(a&&wa_node_attr(a,"id","0"));
 if(self){a=wa_node_add(t,dl,"device");assert(a&&wa_node_attr(a,"id","1")&&wa_node_attr(a,"key-index","1"));}
}
static void bundle(wa_tree *t,wa_node *list,const char *jid,wa_identity *id,wa_signal *signal){
 wa_node *u=wa_node_add(t,list,"user");assert(u&&wa_node_attr(u,"jid",jid));uint8_t reg[4],pk[32];be(id->registration_id,reg,4);
 assert(data(t,u,"registration",reg,4)&&data(t,u,"identity",id->identity_public,32));assert(keynode(t,u,"skey",1,id->prekey_public,id->prekey_signature));
 assert(wa_signal_prekey(signal,50,pk)&&keynode(t,u,"key",50,pk,NULL));
}
static void clear_events(wa_messenger *m){while(wa_messenger_event(m))wa_messenger_event_done(m);}
static void cleanup(const char *path){DIR *d=opendir(path);assert(d);struct dirent *e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char file[1024];snprintf(file,sizeof file,"%s/%s",path,e->d_name);assert(!unlink(file));}closedir(d);assert(!rmdir(path));}
static int reject_plaintext(void *user,const uint8_t *plain,size_t n) {
 int *called=user;assert(plain&&n);(*called)++;return WA_INBOX_STORAGE_ERROR;
}
int main(void){
 char dir[]="/tmp/wa-messages-XXXXXX";assert(mkdtemp(dir));char pa[256],pbdir[256],pp[256];snprintf(pa,sizeof pa,"%s/a",dir);snprintf(pbdir,sizeof pbdir,"%s/b",dir);snprintf(pp,sizeof pp,"%s/p",dir);
 wa_storage a,b,p;assert(wa_storage_posix(&a,pa)&&wa_storage_posix(&b,pbdir)&&wa_storage_posix(&p,pp));wa_identity ai,bi,pi;assert(wa_identity_generate(&ai)&&wa_identity_generate(&bi)&&wa_identity_generate(&pi));
 strcpy(ai.jid,"34111111111:1@s.whatsapp.net");memcpy(ai.account,"synthetic-certificate",21);ai.account_len=21;
 capture c={0};wa_messenger *m=wa_messenger_create(&ai,&a,"11111111:1@lid",capture_node,&c);wa_signal *bob=wa_signal_create(&bi,&b),*primary=wa_signal_create(&pi,&p);assert(m&&bob&&primary);
 /* An existing receive cursor is not evidence that archive/pin rows exist.
  * Request a one-time full low-state recovery, without resetting that cursor. */
 assert(m->rebuild&&wa_messenger_write_pending(m,WA_STATE_REBUILD_REQUEST));
 m->ready=1;m->sync_due[0]=100000;m->sync_due[1]=0;
 wa_messenger_tick(m,1);
 wa_tree *recovery=captured(&c);wa_node *recovery_collection=wa_node_child(wa_node_child(recovery->root,"sync"),"collection");
 assert(recovery_collection&&!strcmp(wa_node_get(recovery_collection,"name"),"regular_low")&&!strcmp(wa_node_get(recovery_collection,"version"),"0")&&!strcmp(wa_node_get(recovery_collection,"return_snapshot"),"true"));
 wa_tree_free(recovery);m->sync_query[0]=0;m->ready=0;m->now=0;m->sync_due[0]=m->sync_due[1]=0;
 assert(!wa_messenger_write_pending(m,"expired"));
 strcpy(m->delete_id,"write");strcpy(m->delete_snapshot,"snapshot");
 assert(wa_messenger_write_pending(m,"snapshot")&&!wa_messenger_write_pending(m,"other"));
 m->delete_id[0]=0;assert(!wa_messenger_write_pending(m,"snapshot"));m->delete_snapshot[0]=0;
 /* One server notification can update both collections. */
 wa_tree *notice=wa_tree_create();assert(notice);
 wa_node *nr=wa_node_add(notice,NULL,"notification");
 assert(wa_node_attr(nr,"type","server_sync")&&wa_node_attr(nr,"id","notice")&&wa_node_attr(nr,"from","s.whatsapp.net"));
 wa_node *nc=wa_node_add(notice,nr,"collection");assert(wa_node_attr(nc,"name","regular_high"));
 nc=wa_node_add(notice,nr,"collection");assert(wa_node_attr(nc,"name","regular_low"));
 m->now=7;m->sync_due[0]=m->sync_due[1]=1000;
 assert(wa_messenger_node(m,nr)&&m->sync_due[0]==7&&m->sync_due[1]==7);
 m->now=0;m->sync_due[0]=m->sync_due[1]=0;wa_tree_free(notice);
 /* Persistent, strict cross-domain mapping; unknown LIDs remain opaque. */
 char resolved[128];
 assert(wa_messenger_resolve(m,"11111111:1@lid",resolved)&&!strcmp(resolved,"34111111111@s.whatsapp.net"));
 assert(wa_messenger_resolve(m,"99999999@lid",resolved)&&!strcmp(resolved,"99999999@lid"));
 assert(!wa_messenger_resolve(m,"123@evil",resolved));
 assert(mapping(m,"11111111@lid","12345678@lid")==-1);
 assert(envelope_mapping(m,"34333333333:0@s.whatsapp.net","33333333:0@lid"));
 assert(wa_messenger_resolve(m,"33333333@lid",resolved)&&!strcmp(resolved,"34333333333@s.whatsapp.net"));
 assert(envelope_mapping(m,"33333333@lid","34444444444@s.whatsapp.net")==-1);
 assert(wa_messenger_resolve(m,"33333333@lid",resolved)&&!strcmp(resolved,"34333333333@s.whatsapp.net"));
 wa_messenger_tick(m,100000);assert(wa_messenger_start(m));wa_tree *t=captured(&c);assert(!strcmp(wa_node_get(t->root,"id"),"legacy-prekeys-count"));wa_tree_free(t);
 t=iq("result","legacy-prekeys-count",NULL);wa_node *count=wa_node_add(t,t->root,"count");assert(count&&wa_node_attr(count,"value","0"));assert(wa_messenger_node(m,t->root));wa_tree_free(t);
 t=captured(&c);wa_node *list=wa_node_child(t->root,"list");assert(list);size_t prekeys=0;for(wa_node *v=list->child;v;v=v->next)prekeys++;assert(prekeys==32);wa_tree_free(t);
 result(m,"legacy-prekeys-upload");result(m,"legacy-active");assert(m->ready);clear_events(m);
 const uint8_t message[]="Hola, prueba C99: café y \xf0\x9f\x8c\x8d";const char *id="3EB01234567890ABCDEF";
 assert(wa_messenger_send(m,"34222222222@s.whatsapp.net",id,message,sizeof message-1));assert(!wa_messenger_send(m,"34222222222@s.whatsapp.net",id,message,sizeof message-1));
 char qid[128];copy(qid,sizeof qid,m->query);t=iq("result",qid,NULL);wa_node *us=wa_node_add(t,t->root,"usync");list=wa_node_add(t,us,"list");user(t,list,"34222222222@s.whatsapp.net","22222222@lid",0);user(t,list,"34111111111@s.whatsapp.net","11111111@lid",1);assert(wa_messenger_node(m,t->root)&&m->count==2);wa_tree_free(t);
 copy(qid,sizeof qid,m->query);t=iq("result",qid,NULL);list=wa_node_add(t,t->root,"list");bundle(t,list,"22222222:0@lid",&bi,bob);bundle(t,list,"11111111:0@lid",&pi,primary);assert(wa_messenger_node(m,t->root)&&m->phase==3);wa_tree_free(t);
 t=captured(&c);assert(!strcmp(t->root->tag,"message")&&!strcmp(wa_node_get(t->root,"to"),"22222222@lid")&&wa_node_child(t->root,"device-identity"));
 wa_node *participants=wa_node_child(t->root,"participants");size_t decrypted=0;
 for(wa_node *to=participants->child;to;to=to->next){const char *jid=wa_node_get(to,"jid");wa_node *enc=wa_node_child(to,"enc");assert(!strcmp(wa_node_get(enc,"type"),"pkmsg"));uint8_t plain[8192];size_t n=0;
  int isown=strstr(jid,"11111111")!=NULL;
  if(!isown){uint8_t broken[8192];assert(enc->data_len<sizeof broken);memcpy(broken,enc->data,enc->data_len);broken[enc->data_len-1]^=1;assert(wa_signal_decrypt(bob,"11111111:1@lid",1,broken,enc->data_len,plain,sizeof plain,&n)<0);}
  if(!isown){int called=0;assert(wa_signal_decrypt_with_callback(bob,"11111111:1@lid",1,enc->data,enc->data_len,plain,sizeof plain,&n,reject_plaintext,&called)==WA_INBOX_STORAGE_ERROR&&called==1);}
  assert(wa_signal_decrypt(isown?primary:bob,"11111111:1@lid",1,enc->data,enc->data_len,plain,sizeof plain,&n)>=0);
  char text[WA_TEXT_MAX+1],dest[128]="";int own=0;unsigned pad=plain[n-1];assert(pad&&pad<=16);assert(decode_text(plain,n-pad,text,dest,&own,0)&&!strcmp(text,(const char*)message)&&own==isown);if(own)assert(!strcmp(dest,"34222222222@s.whatsapp.net"));decrypted++;
 }assert(decrypted==2);wa_tree_free(t);
 t=wa_tree_create();wa_node *acknode=wa_node_add(t,NULL,"ack");assert(wa_node_attr(acknode,"id",id)&&wa_node_attr(acknode,"class","message"));assert(wa_messenger_node(m,t->root)&&!m->pending);wa_tree_free(t);clear_events(m);
 /* Restart both peers. The reply must use persisted ratchet state, not a new session. */
 wa_messenger_free(m);wa_signal_free(bob);m=wa_messenger_create(&ai,&a,"11111111:1@lid",capture_node,&c);bob=wa_signal_create(&bi,&b);assert(m&&bob);
 uint8_t encoded[128];size_t en=0;const uint8_t reply[]="Respuesta cifrada";assert(pb(1,reply,sizeof reply-1,encoded,sizeof encoded,&en));encoded[en++]=1;
 uint8_t ct[4096];size_t cn=0;int pk=0;assert(wa_signal_encrypt(bob,"11111111:1@lid",encoded,en,ct,sizeof ct,&cn,&pk)>=0&&!pk);
 t=wa_tree_create();wa_node *root=wa_node_add(t,NULL,"message");assert(wa_node_attr(root,"id","incoming-test")&&wa_node_attr(root,"from","22222222:0@lid")&&wa_node_attr(root,"notify","Ana sintética")&&wa_node_attr(root,"sender_pn","34444444444@s.whatsapp.net"));wa_node *enc=data(t,root,"enc",ct,cn);assert(enc&&wa_node_attr(enc,"type","msg"));assert(wa_messenger_node(m,root));
 const wa_message_event *e=wa_messenger_event(m);assert(e&&!strcmp(e->kind,"alias-conflict"));wa_messenger_event_done(m);
 e=wa_messenger_event(m);assert(e&&!strcmp(e->kind,"message")&&!strcmp(e->text,(const char*)reply)&&!strcmp(e->jid,"34222222222@s.whatsapp.net"));
 wa_messenger_event_done(m);e=wa_messenger_event(m);
 assert(e&&!strcmp(e->kind,"contact")&&!strcmp(e->jid,"34222222222@s.whatsapp.net")&&!strcmp(e->text,"Ana sintética"));
 wa_tree_free(t);clear_events(m);
 /* Process death after delivery to JNI but before SQLite commit must replay.
  * The inbox is released only by the application's explicit durable receipt. */
 wa_messenger_free(m);m=wa_messenger_create(&ai,&a,"11111111:1@lid",capture_node,&c);assert(m&&wa_messenger_start(m));
 assert(wa_messenger_resolve(m,"33333333@lid",resolved)&&!strcmp(resolved,"34333333333@s.whatsapp.net"));
 e=wa_messenger_event(m);assert(e&&!strcmp(e->id,"incoming-test")&&!strcmp(e->text,(const char*)reply));clear_events(m);
 assert(wa_messenger_stored(m,"incoming-test","34222222222@s.whatsapp.net",0));
 wa_messenger_free(m);m=wa_messenger_create(&ai,&a,"11111111:1@lid",capture_node,&c);assert(m&&wa_messenger_start(m)&&!wa_messenger_event(m));
 /* Re-delivery of the already committed ciphertext is not an error alert. */
 t=wa_tree_create();root=wa_node_add(t,NULL,"message");assert(wa_node_attr(root,"id","incoming-test")&&wa_node_attr(root,"from","22222222:0@lid")&&wa_node_attr(root,"peer_recipient_pn","34111111111@s.whatsapp.net"));
 enc=data(t,root,"enc",ct,cn);assert(enc&&wa_node_attr(enc,"type","msg"));assert(wa_messenger_node(m,root));
 e=wa_messenger_event(m);assert(e&&!strcmp(e->kind,"duplicate"));wa_tree_free(t);clear_events(m);
 /* Regression for actual 30/61 KiB startup payloads: decrypt a large
  * non-text protobuf without a Signal warning, then receive ordinary text on
  * the same ratchet. No history-sync implementation is implied by ignoring it. */
 for(size_t size=30000;size<=60000;size+=30000){
  uint8_t *payload=malloc(size),*large_plain=malloc(size+32),*large_cipher=malloc(size+1024);
  assert(payload&&large_plain&&large_cipher);memset(payload,'x',size);size_t plain_len=0,cipher_len=0;
  assert(pb(99,payload,size,large_plain,size+32,&plain_len));large_plain[plain_len++]=1;
  assert(wa_signal_encrypt(bob,"11111111:1@lid",large_plain,plain_len,large_cipher,size+1024,&cipher_len,&pk)>=0&&!pk);
  t=wa_tree_create();root=wa_node_add(t,NULL,"message");assert(wa_node_attr(root,"id","large-incoming")&&wa_node_attr(root,"from","22222222:0@lid"));
  enc=data(t,root,"enc",large_cipher,cipher_len);assert(enc&&wa_node_attr(enc,"type","msg"));assert(wa_messenger_node(m,root));
  e=wa_messenger_event(m);assert(e&&!strcmp(e->kind,"ignored-payload"));
  wa_tree *response=captured(&c);assert(!strcmp(response->root->tag,"ack"));wa_tree_free(response);
  assert(m->event_count==1);clear_events(m);wa_tree_free(t);free(payload);free(large_plain);free(large_cipher);
 }
 /* The next small message must still decrypt after both large payloads. */
 assert(wa_signal_encrypt(bob,"11111111:1@lid",encoded,en,ct,sizeof ct,&cn,&pk)>=0);
 t=wa_tree_create();root=wa_node_add(t,NULL,"message");assert(wa_node_attr(root,"id","after-large")&&wa_node_attr(root,"from","22222222:0@lid"));
 enc=data(t,root,"enc",ct,cn);assert(enc&&wa_node_attr(enc,"type",pk?"pkmsg":"msg"));assert(wa_messenger_node(m,root));
 e=wa_messenger_event(m);assert(e&&!strcmp(e->kind,"message")&&!strcmp(e->text,(const char*)reply));wa_tree_free(t);clear_events(m);
 /* Reordered receipts must remain distinguishable. UI persists monotonic state. */
 t=wa_tree_create();root=wa_node_add(t,NULL,"receipt");assert(wa_node_attr(root,"id",id)&&wa_node_attr(root,"from","22222222@lid")&&wa_node_attr(root,"type","read"));assert(wa_messenger_node(m,root));assert(wa_messenger_event(m)->state==4&&!strcmp(wa_messenger_event(m)->jid,"34222222222@s.whatsapp.net"));wa_tree_free(t);clear_events(m);
 t=wa_tree_create();root=wa_node_add(t,NULL,"receipt");assert(wa_node_attr(root,"id",id)&&wa_node_attr(root,"from","11111111@lid")&&wa_node_attr(root,"type","read-self"));
 assert(wa_messenger_node(m,root)&&!wa_messenger_event(m));wa_tree_free(t);
 /* Peer history requests encrypt only to our authenticated primary device. */
 clear_events(m);m->ready=1;
 assert(wa_messenger_history(m,"34222222222@s.whatsapp.net","incoming-test",0,1700000000000ULL,"3EB0AABBCCDDEEFF"));
 copy(qid,sizeof qid,m->query);t=iq("result",qid,NULL);us=wa_node_add(t,t->root,"usync");list=wa_node_add(t,us,"list");user(t,list,"34111111111@s.whatsapp.net","11111111@lid",1);assert(wa_messenger_node(m,t->root));wa_tree_free(t);
 t=captured(&c);assert(!strcmp(t->root->tag,"message")&&!strcmp(wa_node_get(t->root,"category"),"peer")&&!wa_node_child(t->root,"participants"));
 enc=wa_node_child(t->root,"enc");assert(enc);uint8_t request_plain[4096];size_t request_len=0;
 assert(wa_signal_decrypt(primary,"11111111:1@lid",!strcmp(wa_node_get(enc,"type"),"pkmsg"),enc->data,enc->data_len,request_plain,sizeof request_plain,&request_len)>=0);
 wa_slice protocol,pdo,request,chat_id;unsigned request_pad=request_plain[request_len-1];
 assert(wa_pb_bytes_field(request_plain,request_len-request_pad,12,&protocol)&&wa_pb_bytes_field(protocol.ptr,protocol.len,16,&pdo)&&wa_pb_bytes_field(pdo.ptr,pdo.len,4,&request)&&wa_pb_bytes_field(request.ptr,request.len,1,&chat_id));
 assert(chat_id.len==strlen("34222222222@s.whatsapp.net")&&!memcmp(chat_id.ptr,"34222222222@s.whatsapp.net",chat_id.len));wa_tree_free(t);
 m->pending=0;m->phase=0;clear_events(m);
 /* TOFU is pinned; no silent key replacement on a changed identity. */
 uint8_t pre[32];assert(wa_signal_prekey(primary,51,pre));assert(wa_signal_bundle(m->signal,"22222222:0@lid",pi.registration_id,pi.identity_public,1,pi.prekey_public,pi.prekey_signature,51,pre)==-1010);
 m->ready=1;assert(!wa_messenger_send(m,"bad@evil",id,message,sizeof message-1));uint8_t bad[]={0xc0,0x80};assert(!wa_messenger_send(m,"34222222222@s.whatsapp.net",id,bad,sizeof bad));
 wa_messenger_free(m);wa_signal_free(bob);wa_signal_free(primary);wa_storage_posix_close(&a);wa_storage_posix_close(&b);wa_storage_posix_close(&p);cleanup(pa);cleanup(pbdir);cleanup(pp);assert(!rmdir(dir));
 puts("PASS: prekey publication, LID device lookup, all-device Signal encryption, own-device copy, UTF-8 text, bidirectional persistent ratchet, 30/60 KiB payloads and subsequent text, read receipts, identity-change rejection and malformed-input rejection");return 0;
}
