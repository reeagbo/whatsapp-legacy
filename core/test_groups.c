/* Reuse the synthetic peer helpers, not their direct-message test main. */
#define main direct_message_tests_main
#include "test_messaging.c"
#undef main
static wa_node *group_fixture(wa_tree *t,wa_node *parent,const char *subject) {
 wa_node *g=wa_node_add(t,parent,"group");
 assert(g&&wa_node_attr(g,"id","120363000000001")&&wa_node_attr(g,"subject",subject)&&wa_node_attr(g,"addressing_mode","lid"));
 wa_node *u=wa_node_add(t,g,"participant");assert(u&&wa_node_attr(u,"jid","11111111@lid")&&wa_node_attr(u,"type","superadmin"));
 u=wa_node_add(t,g,"participant");assert(u&&wa_node_attr(u,"jid","22222222@lid"));return g;
}
int main(void) {
 char dir[]="/tmp/wa-groups-XXXXXX";assert(mkdtemp(dir));char pa[256],pbdir[256];
 snprintf(pa,sizeof pa,"%s/a",dir);snprintf(pbdir,sizeof pbdir,"%s/b",dir);
 wa_storage a,b;assert(wa_storage_posix(&a,pa)&&wa_storage_posix(&b,pbdir));
 wa_identity ai,bi;assert(wa_identity_generate(&ai)&&wa_identity_generate(&bi));
 strcpy(ai.jid,"34111111111:1@s.whatsapp.net");ai.account_len=1;ai.account[0]=1;
 capture c={0};wa_messenger *m=wa_messenger_create(&ai,&a,"11111111@lid",capture_node,&c);assert(m);m->ready=1;
 wa_signal *bob=wa_signal_create(&bi,&b);assert(bob);
 assert(is_group("120363000000001@g.us")&&is_group("34111111-123@g.us")&&!is_group("1@g.us.evil")&&!is_group("1:0@g.us"));
 assert(!wa_messenger_group(m,1,"","","22222222@lid"));
 assert(wa_messenger_group(m,1,"","Prueba café","22222222@lid"));
 wa_tree *t=captured(&c);assert(!strcmp(wa_node_get(t->root,"to"),"@g.us")&&!strcmp(wa_node_get(t->root,"xmlns"),"w:g2"));
 assert(wa_node_child(t->root,"create"));wa_tree_free(t);
 char query[128];strcpy(query,m->group_query);t=iq("result",query,NULL);group_fixture(t,t->root,"Prueba café");assert(wa_messenger_node(m,t->root));wa_tree_free(t);
 assert(!strcmp(wa_messenger_event(m)->kind,"group")&&!strcmp(wa_messenger_event(m)->id,"created"));clear_events(m);
 /* A group send must query current membership before device discovery. */
 const char *gid="120363000000001@g.us",*id="3EB0ABCDEF012345";
 assert(wa_messenger_send(m,gid,id,(const uint8_t*)"Hola grupo",10));
 t=captured(&c);assert(!strcmp(wa_node_get(t->root,"to"),gid)&&wa_node_child(t->root,"query"));wa_tree_free(t);
 strcpy(query,m->query);t=iq("result",query,NULL);group_fixture(t,t->root,"Prueba café");assert(wa_messenger_node(m,t->root));wa_tree_free(t);
 assert(m->phase==1&&m->member_count==2);
 strcpy(query,m->query);t=iq("result",query,NULL);wa_node *us=wa_node_add(t,t->root,"usync"),*list=wa_node_add(t,us,"list");
 user(t,list,"11111111@lid","11111111@lid",0);user(t,list,"22222222@lid","22222222@lid",0);
 /* Exclude our primary from this fixture, leaving one remote device. */
 wa_node *own=wa_node_child(list,"user");own->child=wa_node_child(own,"lid");own->child->next=NULL;
 assert(wa_messenger_node(m,t->root));wa_tree_free(t);assert(m->count==1&&m->phase==2);
 strcpy(query,m->query);t=iq("result",query,NULL);list=wa_node_add(t,t->root,"list");bundle(t,list,"22222222:0@lid",&bi,bob);
 assert(wa_messenger_node(m,t->root));wa_tree_free(t);
 t=captured(&c);assert(!strcmp(t->root->tag,"message")&&!strcmp(wa_node_get(t->root,"to"),gid));
 wa_node *parts=wa_node_child(t->root,"participants"),*to=wa_node_child(parts,"to"),*enc=wa_node_child(to,"enc"),*skenc=wa_node_child(t->root,"enc");assert(enc&&skenc&&!strcmp(wa_node_get(skenc,"type"),"skmsg"));
 uint8_t plain[8192];size_t n=0;
 assert(wa_signal_decrypt(bob,"11111111:1@lid",!strcmp(wa_node_get(enc,"type"),"pkmsg"),enc->data,enc->data_len,plain,sizeof plain,&n)>=0);
 wa_slice skdm,key;assert(wa_pb_bytes_field(plain,n-plain[n-1],2,&skdm)&&wa_pb_bytes_field(skdm.ptr,skdm.len,2,&key));
 assert(wa_signal_group_process(bob,gid,"11111111:1@lid",key.ptr,key.len)>=0);
 assert(wa_signal_group_decrypt(bob,gid,"11111111:1@lid",skenc->data,skenc->data_len,plain,sizeof plain,&n,NULL,NULL)>=0);
 wa_slice text;assert(wa_pb_bytes_field(plain,n-plain[n-1],1,&text)&&text.len==10&&!memcmp(text.ptr,"Hola grupo",10));wa_tree_free(t);
 /* Incoming sender distribution + skmsg: author and group survive inbox replay. */
 uint8_t dist[512],entry[700],body[800],ct[1600],gct[1600];size_t dn=0,en=0,bn=0,cn=0,gn=0;int pk;
 assert(wa_signal_group_distribution(bob,gid,"22222222:0@lid",dist,sizeof dist,&dn)>=0);
 assert(pb(1,(const uint8_t*)gid,strlen(gid),entry,sizeof entry,&en)&&pb(2,dist,dn,entry,sizeof entry,&en)&&pb(2,entry,en,body,sizeof body,&bn));body[bn++]=1;
 assert(wa_signal_encrypt(bob,"11111111:1@lid",body,bn,ct,sizeof ct,&cn,&pk)>=0);
 bn=0;assert(pb(1,(const uint8_t*)"Respuesta",9,body,sizeof body,&bn));body[bn++]=1;
 assert(wa_signal_group_encrypt(bob,gid,"22222222:0@lid",body,bn,gct,sizeof gct,&gn)>=0);
 clear_events(m);t=wa_tree_create();wa_node *r=wa_node_add(t,NULL,"message");assert(wa_node_attr(r,"from",gid)&&wa_node_attr(r,"participant","22222222:0@lid")&&wa_node_attr(r,"id","group-reply"));
 /* Deliberately order skmsg before its distribution. */
 enc=data(t,r,"enc",gct,gn);assert(wa_node_attr(enc,"type","skmsg"));enc=data(t,r,"enc",ct,cn);assert(wa_node_attr(enc,"type",pk?"pkmsg":"msg"));
 assert(wa_messenger_node(m,r));wa_tree_free(t);
 const wa_message_event *found=NULL;for(size_t i=0;i<m->event_count;i++)if(!strcmp(m->events[i].kind,"message"))found=&m->events[i];
 assert(found&&!strcmp(found->jid,gid)&&!strcmp(found->author,"22222222:0@lid")&&!strcmp(found->text,"Respuesta"));
 wa_inbox *in=wa_inbox_open(&a);assert(in&&wa_inbox_count(in)==1&&!strcmp(wa_inbox_at(in,0)->author,"22222222:0@lid"));wa_inbox_free(in);
 /* Tampering fails; callback rejection must leave the ratchet retryable. */
 assert(wa_signal_group_encrypt(bob,gid,"22222222:0@lid",body,bn,gct,sizeof gct,&gn)>=0);gct[gn-1]^=1;
 assert(wa_signal_group_decrypt(m->signal,gid,"22222222:0@lid",gct,gn,plain,sizeof plain,&n,NULL,NULL)<0);gct[gn-1]^=1;
 int called=0;assert(wa_signal_group_decrypt(m->signal,gid,"22222222:0@lid",gct,gn,plain,sizeof plain,&n,reject_plaintext,&called)==WA_INBOX_STORAGE_ERROR&&called==1);
 assert(wa_signal_group_decrypt(m->signal,gid,"22222222:0@lid",gct,gn,plain,sizeof plain,&n,NULL,NULL)>=0);
 assert(wa_signal_group_decrypt(m->signal,gid,"22222222:0@lid",gct,gn,plain,sizeof plain,&n,NULL,NULL)<0);
 m->pending=0;assert(wa_messenger_group(m,2,gid,"",""));strcpy(query,m->group_query);t=iq("result",query,NULL);assert(wa_messenger_node(m,t->root));wa_tree_free(t);
 wa_signal_free(bob);wa_messenger_free(m);wa_storage_posix_close(&a);wa_storage_posix_close(&b);cleanup(pa);cleanup(pbdir);rmdir(dir);
 puts("PASS: group create/metadata/leave IQs, current membership discovery, pairwise key distribution, skmsg roundtrip, author inbox persistence, tamper/duplicate rejection and pre-commit storage failure retry");return 0;
}
