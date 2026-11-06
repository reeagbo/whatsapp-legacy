/* Private messenger implementation. All network/crypto ownership remains C99. */
static int group_metadata(wa_messenger *m,const wa_node *g,int sending) {
 const char *raw=wa_node_get(g,"id"),*subject=wa_node_get(g,"subject");char jid[128];
 if(!raw||strlen(raw)>100)return 0;
 snprintf(jid,sizeof jid,strchr(raw,'@')?"%s":"%s@g.us",raw);
 if(!is_group(jid)||!subject||strlen(subject)>256||!utf8((const uint8_t*)subject,strlen(subject)))return 0;
 if(sending&&strcmp(jid,m->peer))return 0;
 char members[WA_TEXT_MAX+1]="";size_t used=0,count=0;int self=0,admin=0;
 if(sending){m->member_count=0;const char *mode=wa_node_get(g,"addressing_mode");m->group_lid=mode&&!strcmp(mode,"lid");}
 for(const wa_node *p=g->child;p;p=p->next){
  if(strcmp(p->tag,"participant"))continue;char peer[128];
  if(!base(wa_node_get(p,"jid"),peer)||is_group(peer)||count==GROUP_MEMBERS)return 0;
  const char *pn=wa_node_get(p,"phone_number"),*lid=wa_node_get(p,"lid");
  if(pn&&strstr(peer,"@lid")&&mapping(m,peer,pn)!=1)return 0;
  if(lid&&strstr(peer,"@s.whatsapp.net")&&mapping(m,lid,peer)!=1)return 0;
  const char *role=wa_node_get(p,"type");int isadmin=role&&(!strcmp(role,"admin")||!strcmp(role,"superadmin"));
  int own=!strcmp(peer,m->own)||(m->lid[0]&&!strcmp(peer,m->lid));
  if(own){self=1;admin=isadmin;}
  int written=snprintf(members+used,sizeof members-used,"%s\t%d\n",peer,isadmin);
  if(written<0||(size_t)written>=sizeof members-used)return 0;used+=(size_t)written;count++;
  if(sending)copy(m->members[m->member_count++],128,peer);
 }
 if(m->event_count>EVENTS-3)return 0;
 event(m,"group",!sending&&m->group_query[0]&&m->group_action==1?"created":"",jid,subject,self?1:0);
 event(m,"group-members","",jid,members,self?1:0);
 if(sending){
  if(!self||!count)return 0;
  if(wa_node_child(g,"announcement")&&!admin)return 0;
  if(wa_node_child(g,"parent"))return 0; /* Community containers are not chat groups. */
  if(m->group_lid&&!m->lid[0])return 0;
 }
 return 1;
}
int wa_messenger_group(wa_messenger *m,int action,const char *jid,const char *subject,const char *members) {
 if(!m||!m->ready||m->group_query[0]||action<0||action>3)return 0;
 if((action==2||action==3)&&!is_group(jid))return 0;
 if(action==1&&(!subject||!strlen(subject)||strlen(subject)>256||!utf8((const uint8_t*)subject,strlen(subject))||!members))return 0;
 snprintf(m->group_query,sizeof m->group_query,"legacy-group-%u",++m->group_sequence);
 copy(m->group_target,sizeof m->group_target,jid?jid:"");m->group_action=action;
 wa_tree *t=group_iq(action==1||action==2?"set":"get",m->group_query,action==3?m->group_target:"@g.us");
 wa_node *q=t?wa_node_add(t,t->root,action==0?"participating":action==1?"create":action==2?"leave":"query"):NULL;
 int ok=q!=NULL;
 if(action==0)ok=ok&&wa_node_add(t,q,"participants")&&wa_node_add(t,q,"description");
 if(action==3)ok=ok&&wa_node_attr(q,"request","interactive");
 if(action==2){wa_node *g=wa_node_add(t,q,"group");ok=ok&&g&&wa_node_attr(g,"id",m->group_target);}
 if(action==1){
  ok=ok&&wa_node_attr(q,"subject",subject)&&wa_node_attr(q,"key",m->group_query);
  const char *p=members;char peers[GROUP_MEMBERS][128];size_t count=0;
  while(ok&&*p){const char *end=strchr(p,'\n');size_t n=end?(size_t)(end-p):strlen(p);char peer[128];
   if(!n||n>=sizeof peer||count==GROUP_MEMBERS){ok=0;break;}
   memcpy(peer,p,n);peer[n]=0;char canonical[128];
   if(!base(peer,canonical)||is_group(canonical)){ok=0;break;}
   int duplicate=0;for(size_t i=0;i<count;i++)if(!strcmp(peers[i],canonical))duplicate=1;
   if(!duplicate){copy(peers[count++],128,canonical);wa_node *u=wa_node_add(t,q,"participant");
    /* Node attributes borrow storage: copy the address into the tree arena. */
    size_t z=strlen(canonical)+1;if(z>WA_NODE_BYTES-t->used){ok=0;break;}
    char *owned=(char*)t->arena+t->used;memcpy(owned,canonical,z);t->used+=z;ok=u&&wa_node_attr(u,"jid",owned);
   }
   if(!end)break;p=end+1;
  }
  ok=ok&&count>0;
 }
 if(!ok){wa_tree_free(t);m->group_query[0]=0;return 0;}
 m->group_deadline=m->now+45000;
 if(send(m,t))return 1;m->group_query[0]=0;m->group_deadline=0;return 0;
}
static int groups_node(wa_messenger *m,const wa_node *node) {
 const char *type=wa_node_get(node,"type"),*id=wa_node_get(node,"id");
 if(!strcmp(node->tag,"notification")&&type&&!strcmp(type,"w:gp2")){
  const char *from=wa_node_get(node,"from");
  if(is_group(from)){
   const wa_node *remove=wa_node_child(node,"remove");
   for(const wa_node *p=remove?remove->child:NULL;p;p=p->next){char peer[128];if(base(wa_node_get(p,"jid"),peer)&&(!strcmp(peer,m->own)||!strcmp(peer,m->lid)))event(m,"group-left","",from,"",1);}
  }
  m->groups_due=1;return ack(m,node,"notification")?1:-1;
 }
 if(strcmp(node->tag,"iq")||!id||!m->group_query[0]||strcmp(id,m->group_query))return 0;
 int ok=type&&!strcmp(type,"result")&&!wa_node_child(node,"error");
 if(ok&&m->group_action==0){const wa_node *groups=wa_node_child(node,"groups");ok=groups!=NULL;
  for(const wa_node *g=groups?groups->child:NULL;g;g=g->next)if(!strcmp(g->tag,"group")&&!group_metadata(m,g,0))ok=0;
 }else if(ok&&m->group_action==2)event(m,"group-left","",m->group_target,"",1);
 else if(ok)ok=group_metadata(m,wa_node_child(node,"group"),0);
 event(m,"group-result","",m->group_target,ok?"Operación de grupo completada":"WhatsApp rechazó la operación o devolvió datos no compatibles",ok?1:-1);
 m->group_query[0]=0;m->group_deadline=0;return 1;
}
static void groups_tick(wa_messenger *m,uint64_t now) {
 if(m->group_query[0]&&now>=m->group_deadline){event(m,"group-result","",m->group_target,"La operación de grupo agotó el tiempo",-2);m->group_query[0]=0;}
 if(m->ready&&m->groups_due&&!m->group_query[0]&&!m->pending){m->groups_due=0;if(!wa_messenger_group(m,0,"","",""))m->groups_due=1;}
}
static int transmit_group(wa_messenger *m) {
 uint8_t plain[WA_TEXT_MAX+32],sk[512],distribution[720],body[900],ciphertext[WA_TEXT_MAX+1024];
 size_t pn=0,sn=0,dn=0,bn=0,cn=0;
 char author[128];if(!device(m->group_lid?m->lid:m->own,author,device_id(m->me.jid)))return error(m,"Identidad de grupo no válida.");
 /* Fresh key for each send avoids retaining keys across membership removals.
  * Every current device receives its distribution; no partial-recipient send. */
 if(!wa_signal_group_reset(m->signal,m->peer,author)||wa_signal_group_distribution(m->signal,m->peer,author,sk,sizeof sk,&sn)<0||
  !pb(1,m->text,m->text_len,plain,sizeof plain,&pn))return error(m,"No se pudo preparar el cifrado del grupo.");
 plain[pn++]=1;
 if(wa_signal_group_encrypt(m->signal,m->peer,author,plain,pn,ciphertext,sizeof ciphertext,&cn)<0)return error(m,"No se pudo cifrar el mensaje de grupo.");
 if(!pb(1,(const uint8_t*)m->peer,strlen(m->peer),distribution,sizeof distribution,&dn)||!pb(2,sk,sn,distribution,sizeof distribution,&dn)||!pb(2,distribution,dn,body,sizeof body,&bn))return error(m,"Clave de grupo demasiado grande.");
 body[bn++]=1;
 wa_tree *t=wa_tree_create();wa_node *root=t?wa_node_add(t,NULL,"message"):NULL;
 wa_node *participants=root?wa_node_add(t,root,"participants"):NULL;
 int ok=root&&participants&&wa_node_attr(root,"to",m->peer)&&wa_node_attr(root,"id",m->id)&&wa_node_attr(root,"type","text")&&wa_node_attr(root,"addressing_mode",m->group_lid?"lid":"pn");int needs_identity=0;
 for(size_t i=0;ok&&i<m->count;i++){
  uint8_t ct[1600];size_t n=0;int pk=0;
  ok=wa_signal_encrypt(m->signal,m->devices[i].jid,body,bn,ct,sizeof ct,&n,&pk)>=0;
  wa_node *to=ok?wa_node_add(t,participants,"to"):NULL,*enc=to?data(t,to,"enc",ct,n):NULL;
  ok=ok&&enc&&wa_node_attr(to,"jid",m->devices[i].jid)&&wa_node_attr(enc,"v","2")&&wa_node_attr(enc,"type",pk?"pkmsg":"msg");if(pk)needs_identity=1;
 }
 wa_node *enc=ok?data(t,root,"enc",ciphertext,cn):NULL;
 ok=ok&&enc&&wa_node_attr(enc,"v","2")&&wa_node_attr(enc,"type","skmsg");
 if(ok&&needs_identity)ok=data(t,root,"device-identity",m->me.account,m->me.account_len)!=NULL;
 OPENSSL_cleanse(plain,sizeof plain);OPENSSL_cleanse(sk,sizeof sk);OPENSSL_cleanse(body,sizeof body);
 if(!ok){wa_tree_free(t);return error(m,"No se pudo cifrar para todos los dispositivos del grupo.");}
 if(!send(m,t))return error(m,"No se pudo enviar al grupo.");
 m->phase=3;m->deadline=m->now+45000;event(m,"status",m->id,m->peer,"Grupo cifrado; esperando confirmación",1);OPENSSL_cleanse(m->text,sizeof m->text);return 1;
}
