#include "messaging.h"
#include "signal_engine.h"
#include "inbox.h"
#include "history.h"
#include "wire.h"
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DEVICES 32
#define EVENTS 64
struct wa_messenger {
 wa_identity me;wa_storage io;wa_signal *signal;wa_inbox *inbox;int inbox_replayed;wa_send_node send;void *user;
 char own[128],lid[128];
 int history_mode;
 int ready,boot,pending,phase;uint64_t now,deadline;
 char peer[128],route[128],id[96],query[128];uint8_t text[WA_TEXT_MAX+1];size_t text_len;
 struct {char jid[128];int own;} devices[DEVICES];size_t count;
 wa_message_event events[EVENTS];size_t event_count;
};
static int copy(char *out,size_t cap,const char *s){if(!s||strlen(s)>=cap)return 0;memcpy(out,s,strlen(s)+1);return 1;}
static int base(const char *jid,char out[128]){
 if(!jid)return 0;const char *at=strchr(jid,'@'),*colon=strchr(jid,':');if(!at||at-jid<1||at-jid>40)return 0;
 size_t n=(size_t)((colon&&colon<at?colon:at)-jid);if(!n)return 0;for(size_t i=0;i<n;i++)if(jid[i]<'0'||jid[i]>'9')return 0;
 if(strcmp(at+1,"s.whatsapp.net")&&strcmp(at+1,"lid"))return 0;
 int k=snprintf(out,128,"%.*s@%s",(int)n,jid,at+1);return k>0&&k<128;
}
static int device(const char *jid,char out[128],unsigned d){char b[128];if(!base(jid,b)||d>255)return 0;char *at=strchr(b,'@');*at=0;int n=snprintf(out,128,"%s:%u@%s",b,d,at+1);return n>0&&n<128;}
static unsigned device_id(const char *jid){const char *p=strchr(jid,':');return p?(unsigned)strtoul(p+1,NULL,10):0;}
static int utf8(const uint8_t *p,size_t n){
 for(size_t i=0;i<n;){uint8_t c=p[i++];if(!c)return 0;if(c<128)continue;unsigned z;uint32_t v,min;if(c>=0xc2&&c<=0xdf){z=1;v=c&31;min=128;}else if(c>=0xe0&&c<=0xef){z=2;v=c&15;min=2048;}else if(c>=0xf0&&c<=0xf4){z=3;v=c&7;min=65536;}else return 0;if(z>n-i)return 0;for(unsigned j=0;j<z;j++){c=p[i++];if((c&0xc0)!=0x80)return 0;v=(v<<6)|(c&63);}if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0;}return 1;
}
static void event(wa_messenger *m,const char *kind,const char *id,const char *jid,const char *text,int state){
 if(m->event_count>=EVENTS)return;
 wa_message_event *e=&m->events[m->event_count++];memset(e,0,sizeof *e);
 copy(e->kind,sizeof e->kind,kind);copy(e->id,sizeof e->id,id?id:"");copy(e->jid,sizeof e->jid,jid?jid:"");copy(e->text,sizeof e->text,text?text:"");e->state=state;
}
static int error(wa_messenger *m,const char *text){event(m,"status",m->id,m->peer,text,-1);m->pending=0;m->phase=0;return 0;}
static wa_tree *iq(const char *type,const char *id,const char *xmlns){wa_tree *t=wa_tree_create();if(!t)return NULL;wa_node *n=wa_node_add(t,NULL,"iq");if(!n||!wa_node_attr(n,"to","@s.whatsapp.net")||!wa_node_attr(n,"type",type)||!wa_node_attr(n,"id",id)||(xmlns&&!wa_node_attr(n,"xmlns",xmlns))){wa_tree_free(t);return NULL;}return t;}
static wa_node *data(wa_tree *t,wa_node *p,const char *tag,const uint8_t *b,size_t n){
 if(!t||!b||n>WA_NODE_BYTES-t->used)return NULL;wa_node *v=wa_node_add(t,p,tag);if(!v)return NULL;uint8_t *at=t->arena+t->used;memcpy(at,b,n);t->used+=n;v->data=at;v->data_len=n;return v;
}
static int send(wa_messenger *m,wa_tree *t){int ok=t&&m->send(m->user,t);wa_tree_free(t);return ok;}
static void be(uint32_t v,uint8_t *b,size_t n){for(size_t i=0;i<n;i++)b[n-i-1]=(uint8_t)(v>>(8*i));}
static int number(const wa_node *n,size_t len,uint32_t *out){if(!n||!n->data||n->data_len!=len||len>4)return 0;uint32_t v=0;for(size_t i=0;i<len;i++)v=(v<<8)|n->data[i];*out=v;return 1;}
static wa_node *keynode(wa_tree *t,wa_node *parent,const char *tag,uint32_t id,const uint8_t pk[32],const uint8_t *sig){uint8_t b[3];be(id,b,3);wa_node *n=wa_node_add(t,parent,tag);if(!n||!data(t,n,"id",b,3)||!data(t,n,"value",pk,32)||(sig&&!data(t,n,"signature",sig,64)))return NULL;return n;}
static void mapkey(const char *jid,char key[80]){uint8_t h[32];SHA256((uint8_t*)jid,strlen(jid),h);memcpy(key,"mapping-",8);for(size_t i=0;i<32;i++)snprintf(key+8+i*2,3,"%02x",h[i]);}
static int mapping(wa_messenger *m,const char *lid,const char *pn){char l[128],p[128],key[80];if(!base(lid,l)||!base(pn,p)||!strstr(l,"@lid")||!strstr(p,"@s.whatsapp.net"))return 0;mapkey(l,key);
 char previous[128];size_t length=0;int found=m->io.get(m->io.user,key,(uint8_t*)previous,127,&length);
 if(found<0||length>127)return 0;
 if(found==1){previous[length]=0;if(strcmp(previous,p))return 0;}
 if(!m->io.put(m->io.user,key,(uint8_t*)p,strlen(p)))return 0;mapkey(p,key);return m->io.put(m->io.user,key,(uint8_t*)l,strlen(l));}
static int mapped(wa_messenger *m,const char *jid,char out[128]){char b[128],key[80];if(!base(jid,b))return 0;mapkey(b,key);size_t n=0;int r=m->io.get(m->io.user,key,(uint8_t*)out,127,&n);if(r!=1||n>127)return 0;out[n]=0;char valid[128];return base(out,valid);}
/* Application address only: never rename the Signal session/ratchet key. */
int wa_messenger_resolve(wa_messenger *m,const char *jid,char out[128]) {
 char b[128],pn[128];if(!m||!out||!base(jid,b))return 0;
 if(strstr(b,"@lid")&&mapped(m,b,pn)&&strstr(pn,"@s.whatsapp.net"))return copy(out,128,pn);
 return copy(out,128,b);
}
static int envelope_mapping(wa_messenger *m,const char *jid,const char *alternate) {
 char a[128],b[128];
 if(!alternate||!base(jid,a)||!base(alternate,b))return 1;
 if(strstr(a,"@lid")&&strstr(b,"@s.whatsapp.net"))return mapping(m,a,b);
 if(strstr(b,"@lid")&&strstr(a,"@s.whatsapp.net"))return mapping(m,b,a);
 return 1; /* Same-domain/malformed metadata cannot establish an alias. */
}
static int activate(wa_messenger *m){m->boot=3;wa_tree *t=iq("set","legacy-active","passive");if(!t||!wa_node_add(t,t->root,"active")){wa_tree_free(t);return 0;}return send(m,t);}
static int upload(wa_messenger *m){
 uint8_t b[4];size_t n=0;uint32_t start=1;int r=m->io.get(m->io.user,"next-prekey",b,4,&n);if(r<0||(r==1&&n!=4))return 0;if(r==1){start=0;for(size_t j=0;j<4;j++)start=(start<<8)|b[j];}
 if(!start||start>0xffffff-32)return 0;
 wa_tree *t=iq("set","legacy-prekeys-upload","encrypt");if(!t)return 0;
 uint8_t reg[4],type=5;be(m->me.registration_id,reg,4);
 int ok=data(t,t->root,"registration",reg,4)&&data(t,t->root,"type",&type,1)&&data(t,t->root,"identity",m->me.identity_public,32);
 wa_node *list=wa_node_add(t,t->root,"list");ok=ok&&list;
 for(uint32_t i=0;ok&&i<32;i++){uint8_t pk[32];ok=wa_signal_prekey(m->signal,start+i,pk)&&keynode(t,list,"key",start+i,pk,NULL);}
 ok=ok&&keynode(t,t->root,"skey",1,m->me.prekey_public,m->me.prekey_signature);be(start+32,b,4);
 ok=ok&&m->io.put(m->io.user,"next-prekey",b,4);m->boot=2;
 if(!ok){wa_tree_free(t);return 0;}return send(m,t);
}
wa_messenger *wa_messenger_create(const wa_identity *id,const wa_storage *io,const char *lid,wa_send_node sender,void *u){
 if(!id||!io||!sender||!id->jid[0])return NULL;wa_messenger *m=calloc(1,sizeof *m);if(!m)return NULL;m->me=*id;m->io=*io;m->send=sender;m->user=u;
 if(!base(id->jid,m->own)||(lid&&*lid&&!base(lid,m->lid))||!(m->inbox=wa_inbox_open(io))||!(m->signal=wa_signal_create(id,io))){wa_messenger_free(m);return NULL;}
 if(!m->lid[0]){char known[128];if(mapped(m,m->own,known)&&strstr(known,"@lid"))copy(m->lid,sizeof m->lid,known);}
 if(m->lid[0]&&!mapping(m,m->lid,m->own)){wa_messenger_free(m);return NULL;}return m;
}
void wa_messenger_free(wa_messenger *m){if(!m)return;wa_signal_free(m->signal);wa_inbox_free(m->inbox);OPENSSL_cleanse(m,sizeof *m);free(m);}
int wa_messenger_start(wa_messenger *m){if(!m)return 0;
 if(!m->inbox_replayed){
  for(size_t j=0;j<wa_inbox_count(m->inbox);j++){
   const wa_message_event *e=wa_inbox_at(m->inbox,j);
   event(m,e->kind,e->id,e->jid,e->text,e->state);
  }
  m->inbox_replayed=1;
 }
 m->boot=1;m->deadline=m->now+45000;wa_tree *t=iq("get","legacy-prekeys-count","encrypt");if(!t||!wa_node_add(t,t->root,"count")){wa_tree_free(t);return 0;}return send(m,t);}
static int discover(wa_messenger *m){
 snprintf(m->query,sizeof m->query,"%s-usync",m->id);wa_tree *t=iq("get",m->query,"usync");if(!t)return 0;
 wa_node *u=wa_node_add(t,t->root,"usync");int ok=u&&wa_node_attr(u,"context","message")&&wa_node_attr(u,"mode","query")&&wa_node_attr(u,"sid",m->query)&&wa_node_attr(u,"last","true")&&wa_node_attr(u,"index","0");
 wa_node *q=wa_node_add(t,u,"query"),*d=wa_node_add(t,q,"devices");ok=ok&&q&&d&&wa_node_attr(d,"version","2")&&wa_node_add(t,q,"lid");
 wa_node *list=wa_node_add(t,u,"list"),*p=wa_node_add(t,list,"user");ok=ok&&list&&p&&wa_node_attr(p,"jid",m->peer);
 if(strcmp(m->peer,m->own)){p=wa_node_add(t,list,"user");ok=ok&&p&&wa_node_attr(p,"jid",m->own);}
 m->phase=1;m->deadline=m->now+45000;if(!ok){wa_tree_free(t);return 0;}return send(m,t);
}
int wa_messenger_send(wa_messenger *m,const char *jid,const char *id,const uint8_t *text,size_t n){
 if(!m||!m->ready||m->pending||!text||!n||n>WA_TEXT_MAX||!utf8(text,n)||!id||strlen(id)<8||strlen(id)>80||strspn(id,"0123456789ABCDEF")!=strlen(id))return 0;
 if(!base(jid,m->peer)||!copy(m->id,sizeof m->id,id))return 0;
 m->history_mode=0;memcpy(m->text,text,n);m->text[n]=0;m->text_len=n;m->count=0;m->pending=1;copy(m->route,sizeof m->route,m->peer);
 event(m,"status",id,m->peer,"Buscando dispositivos…",0);if(!discover(m))return error(m,"No se pudo consultar al destinatario.");return 1;
}
static int prepare_devices(wa_messenger *m,const wa_node *root){
 wa_node *list=wa_node_child(wa_node_child(root,"usync"),"list");if(!list)return error(m,"Respuesta de dispositivos incompleta.");
 size_t target_count=0;
 for(wa_node *u=list->child;u;u=u->next){
  const char *jid=wa_node_get(u,"jid");char pn[128];if(strcmp(u->tag,"user")||!base(jid,pn))continue;
  int isown=!strcmp(pn,m->own)||(*m->lid&&!strcmp(pn,m->lid));if(!isown&&strcmp(pn,m->peer))continue;
  const char *lid=wa_node_get(wa_node_child(u,"lid"),"val");char canonical[128];copy(canonical,sizeof canonical,pn);
  if(lid&&base(lid,canonical)){if(!mapping(m,canonical,pn))return error(m,"No se pudo guardar la dirección del contacto.");}
  if(!isown)copy(m->route,sizeof m->route,canonical);
  wa_node *devices=wa_node_child(wa_node_child(u,"devices"),"device-list");
  for(wa_node *d=devices?devices->child:NULL;d;d=d->next){
   const char *di=wa_node_get(d,"id"),*ki=wa_node_get(d,"key-index"),*host=wa_node_get(d,"is_hosted");if(strcmp(d->tag,"device")||!di||!*di||strspn(di,"0123456789")!=strlen(di))continue;
   unsigned long number=strtoul(di,NULL,10);if(number>255||(number&&!ki))continue;
   if(host&&!strcmp(host,"true"))return error(m,"Un dispositivo hosted todavía no está soportado.");
   if(m->history_mode&&(!isown||number!=0))continue;
   if(isown&&number==device_id(m->me.jid))continue;
   if(m->count==DEVICES)return error(m,"Demasiados dispositivos para esta versión.");
   if(!device(canonical,m->devices[m->count].jid,(unsigned)number))return error(m,"Dirección de dispositivo no válida.");
   m->devices[m->count++].own=isown;if(!isown)target_count++;
  }
 }
 if(!m->count||(!target_count&&strcmp(m->peer,m->own)))return error(m,"No se encontraron dispositivos del destinatario.");
 snprintf(m->query,sizeof m->query,"%s-keys",m->id);wa_tree *t=iq("get",m->query,"encrypt");if(!t)return error(m,"Memoria insuficiente.");
 wa_node *k=wa_node_add(t,t->root,"key");int ok=k!=NULL;size_t missing=0;
 for(size_t i=0;i<m->count&&ok;i++){int have=wa_signal_has(m->signal,m->devices[i].jid);if(have<0){ok=0;break;}if(have)continue;missing++;wa_node *u=wa_node_add(t,k,"user");ok=u&&wa_node_attr(u,"jid",m->devices[i].jid);}
 if(!ok){wa_tree_free(t);return error(m,"No se pudieron leer las sesiones cifradas.");}
 m->phase=2;m->deadline=m->now+45000;if(!missing){wa_tree_free(t);return 2;}if(!send(m,t))return error(m,"No se pudieron solicitar las claves.");return 1;
}
static int bundles(wa_messenger *m,const wa_node *root){
 wa_node *list=wa_node_child(root,"list");if(!list)return error(m,"Respuesta de claves incompleta.");
 for(wa_node *u=list->child;u;u=u->next){const char *jid=wa_node_get(u,"jid");if(!jid)continue;int wanted=0;for(size_t i=0;i<m->count;i++)if(!strcmp(jid,m->devices[i].jid))wanted=1;if(!wanted)continue;
  wa_node *identity=wa_node_child(u,"identity"),*signed_key=wa_node_child(u,"skey"),*pk=wa_node_child(u,"key"),*sk=wa_node_child(signed_key,"value"),*sig=wa_node_child(signed_key,"signature"),*pv=wa_node_child(pk,"value");uint32_t reg,sid,pid=0;
  if(wa_node_child(u,"error")||!number(wa_node_child(u,"registration"),4,&reg)||!identity||identity->data_len!=32||!sk||sk->data_len!=32||!sig||sig->data_len!=64||!number(wa_node_child(signed_key,"id"),3,&sid)||(pk&&(!pv||pv->data_len!=32||!number(wa_node_child(pk,"id"),3,&pid))))return error(m,"El servidor no devolvió un paquete de claves válido.");
  int r=wa_signal_bundle(m->signal,jid,reg,identity->data,sid,sk->data,sig->data,pid,pv?pv->data:NULL);
  if(r<0)return error(m,r==-1010?"La identidad del contacto ha cambiado. Envío detenido.":"No se pudo verificar o guardar la sesión Signal.");
 }
 for(size_t i=0;i<m->count;i++)if(wa_signal_has(m->signal,m->devices[i].jid)!=1)return error(m,"Faltan claves para algún dispositivo; no se ha enviado el mensaje.");
 return 1;
}
static int pb(unsigned field,const uint8_t *p,size_t n,uint8_t *out,size_t cap,size_t *used){size_t z=0;if(*used>cap||!wa_pb_write_bytes(field,p,n,out+*used,cap-*used,&z))return 0;*used+=z;return 1;}
static int pbnum(unsigned field,uint64_t value,uint8_t *out,size_t cap,size_t *used) {
 uint8_t encoded[20];size_t n=0;uint64_t key=(uint64_t)field<<3;
 do {uint8_t c=key&127;key>>=7;encoded[n++]=c|(key?128:0);}while(key);
 do {uint8_t c=value&127;value>>=7;encoded[n++]=c|(value?128:0);}while(value);
 if(n>cap-*used)return 0;memcpy(out+*used,encoded,n);*used+=n;return 1;
}
int wa_messenger_history(wa_messenger *m,const char *jid,const char *anchor,int from_me,uint64_t timestamp,const char *id) {
 if(!m||!m->ready||m->pending||!id||strlen(id)<8||strlen(id)>80||strspn(id,"0123456789ABCDEF")!=strlen(id))return 0;
 uint8_t request[1024],pdo[1200],protocol[1400];size_t rn=0,pn=0,n=0;
 int full=!jid||!*jid;char peer[128];
 if(!full&&(!base(jid,peer)||!anchor||!*anchor||strlen(anchor)>95||!timestamp||timestamp>253402300799000ULL))return 0;
 int ok;
 if(full){ /* Primary may decline full sync; it remains a request, not a completion. */
  uint8_t metadata[128],config[128];size_t mn=0,cn=0;
  ok=pb(1,(const uint8_t*)id,strlen(id),metadata,sizeof metadata,&mn)&&pbnum(1,365,config,sizeof config,&cn)&&pbnum(2,8,config,sizeof config,&cn)&&pbnum(3,32,config,sizeof config,&cn)&&pbnum(4,1,config,sizeof config,&cn)&&pbnum(16,1,config,sizeof config,&cn)&&pbnum(18,1,config,sizeof config,&cn)&&pb(1,metadata,mn,request,sizeof request,&rn)&&pb(2,config,cn,request,sizeof request,&rn)&&pbnum(1,6,pdo,sizeof pdo,&pn)&&pb(6,request,rn,pdo,sizeof pdo,&pn);
 }else{
  ok=pb(1,(const uint8_t*)peer,strlen(peer),request,sizeof request,&rn)&&pb(2,(const uint8_t*)anchor,strlen(anchor),request,sizeof request,&rn)&&pbnum(3,from_me!=0,request,sizeof request,&rn)&&pbnum(4,100,request,sizeof request,&rn)&&pbnum(5,timestamp,request,sizeof request,&rn)&&pbnum(1,3,pdo,sizeof pdo,&pn)&&pb(4,request,rn,pdo,sizeof pdo,&pn);
 }
 ok=ok&&pbnum(2,16,protocol,sizeof protocol,&n)&&pb(16,pdo,pn,protocol,sizeof protocol,&n);size_t total=0;
 if(!ok||!pb(12,protocol,n,m->text,sizeof m->text,&total))return 0;
 m->text_len=total;m->history_mode=1;m->pending=1;m->count=0;copy(m->peer,sizeof m->peer,m->own);copy(m->route,sizeof m->route,m->own);copy(m->id,sizeof m->id,id);
 event(m,"history-status",id,"","Solicitando historial al móvil principal…",0);
 if(!discover(m))return error(m,"No se pudo solicitar historial.");return 1;
}
static int transmit(wa_messenger *m){
 uint8_t plain[WA_TEXT_MAX+64],sync[WA_TEXT_MAX+512],outer[WA_TEXT_MAX+600];size_t n=0,sn=0,on=0;
 if(m->history_mode){memcpy(plain,m->text,m->text_len);n=m->text_len;}
 else if(!pb(1,m->text,m->text_len,plain,sizeof plain,&n)||!pb(1,(uint8_t*)m->peer,strlen(m->peer),sync,sizeof sync,&sn)||!pb(2,plain,n,sync,sizeof sync,&sn)||!pb(31,sync,sn,outer,sizeof outer,&on))return error(m,"Texto demasiado largo.");
 wa_tree *t=wa_tree_create();if(!t)return error(m,"Memoria insuficiente.");wa_node *root=wa_node_add(t,NULL,"message");int ok=root&&wa_node_attr(root,"id",m->id)&&wa_node_attr(root,"to",m->history_mode?m->own:m->route)&&wa_node_attr(root,"type","text");wa_node *participants=m->history_mode?root:wa_node_add(t,root,"participants");ok=ok&&participants;int needs_identity=0;
 if(m->history_mode){wa_node *meta=wa_node_add(t,root,"meta");ok=ok&&wa_node_attr(root,"category","peer")&&wa_node_attr(root,"push_priority","high_force")&&meta&&wa_node_attr(meta,"appdata","default");}
 for(size_t i=0;i<m->count&&ok;i++){
  uint8_t input[WA_TEXT_MAX+640],ct[WA_TEXT_MAX+2048],random;size_t size=!m->history_mode&&m->devices[i].own?on:n,cn=0;memcpy(input,!m->history_mode&&m->devices[i].own?outer:plain,size);
  if(RAND_bytes(&random,1)!=1){ok=0;break;}unsigned pad=(random&15)+1;memset(input+size,pad,pad);size+=pad;int ispk=0;
  int r=wa_signal_encrypt(m->signal,m->devices[i].jid,input,size,ct,sizeof ct,&cn,&ispk);OPENSSL_cleanse(input,sizeof input);
  if(r<0){ok=0;break;}
  wa_node *to=m->history_mode?root:wa_node_add(t,participants,"to");wa_node *enc=data(t,to,"enc",ct,cn);ok=to&&enc&&(m->history_mode||wa_node_attr(to,"jid",m->devices[i].jid))&&wa_node_attr(enc,"v","2")&&wa_node_attr(enc,"type",ispk?"pkmsg":"msg");if(ispk)needs_identity=1;
 }
 if(ok&&needs_identity)ok=data(t,root,"device-identity",m->me.account,m->me.account_len)!=NULL;
 OPENSSL_cleanse(plain,sizeof plain);OPENSSL_cleanse(sync,sizeof sync);OPENSSL_cleanse(outer,sizeof outer);
 if(!ok){wa_tree_free(t);return error(m,"No se pudo cifrar para todos los dispositivos. No se envió el mensaje.");}
 if(!send(m,t))return error(m,"No se pudo poner el mensaje en la conexión.");
 m->phase=3;m->deadline=m->now+45000;event(m,"status",m->id,m->peer,"Cifrado y enviado; esperando al servidor",1);OPENSSL_cleanse(m->text,sizeof m->text);return 1;
}
static int ack(wa_messenger *m,const wa_node *node,const char *klass){
 const char *id=wa_node_get(node,"id"),*from=wa_node_get(node,"from"),*participant=wa_node_get(node,"participant"),*type=wa_node_get(node,"type");if(!id||!from)return 0;
 wa_tree *t=wa_tree_create();if(!t)return 0;wa_node *a=wa_node_add(t,NULL,"ack");int ok=a&&wa_node_attr(a,"id",id)&&wa_node_attr(a,"to",from)&&wa_node_attr(a,"class",klass)&&(!participant||wa_node_attr(a,"participant",participant))&&(!type||wa_node_attr(a,"type",type));if(!ok){wa_tree_free(t);return 0;}return send(m,t);
}
static int decode_text(const uint8_t *p,size_t n,char out[WA_TEXT_MAX+1],char dest[128],int *own,int depth){
 if(depth>3)return 0;wa_slice t,v;*own=0;
 if(wa_pb_bytes_field(p,n,31,&v)){wa_slice d,inner;if(wa_pb_bytes_field(v.ptr,v.len,1,&d)&&d.len<128&&wa_pb_bytes_field(v.ptr,v.len,2,&inner)){memcpy(dest,d.ptr,d.len);dest[d.len]=0;int found=decode_text(inner.ptr,inner.len,out,dest,own,depth+1);*own=1;return found;}}
 if(!wa_pb_bytes_field(p,n,1,&t)){if(!wa_pb_bytes_field(p,n,6,&v)||!wa_pb_bytes_field(v.ptr,v.len,1,&t))return 0;}
 if(!t.len||t.len>WA_TEXT_MAX||!utf8(t.ptr,t.len))return 0;memcpy(out,t.ptr,t.len);out[t.len]=0;return 1;
}
typedef struct {
 wa_messenger *m;const char *id,*from;wa_message_event saved;int stored,history;
} incoming_commit;
static int save_before_ratchet(void *user,const uint8_t *plain,size_t n) {
 incoming_commit *c=user;unsigned pad=n?plain[n-1]:0;
 if(!pad||pad>16||pad>n)return 0;
 for(unsigned j=0;j<pad;j++)if(plain[n-j-1]!=pad)return 0;
 char text[WA_TEXT_MAX+1],dest[128]="",peer[128];int own=0;
 wa_slice protocol,notification;
 if((!strcmp(c->from,c->m->own)||(c->m->lid[0]&&!strcmp(c->from,c->m->lid)))&&wa_pb_bytes_field(plain,n-pad,12,&protocol)&&wa_pb_bytes_field(protocol.ptr,protocol.len,6,&notification)) {
  if(!wa_history_save(&c->m->io,notification.ptr,notification.len))return WA_INBOX_STORAGE_ERROR;
  c->history=1;return 0;
 }
 if(!decode_text(plain,n-pad,text,dest,&own,0))return 0;
 if(own){
  /* Only our own authenticated account can supply an outgoing-device copy. */
  if(strcmp(c->from,c->m->own)&&(!c->m->lid[0]||strcmp(c->from,c->m->lid)))return 0;
  if(!wa_messenger_resolve(c->m,dest,peer))return 0;
 }
 else if(!mapped(c->m,c->from,peer)||!strstr(peer,"@s.whatsapp.net"))copy(peer,sizeof peer,c->from);
 wa_message_event *e=&c->saved;memset(e,0,sizeof *e);
 if(!copy(e->kind,sizeof e->kind,own?"sent-copy":"message")||
    !copy(e->id,sizeof e->id,c->id)||!copy(e->jid,sizeof e->jid,peer)||
    !copy(e->text,sizeof e->text,text))return WA_INBOX_STORAGE_ERROR;
 e->state=own?2:0;
 if(!wa_inbox_add(c->m->inbox,e))return WA_INBOX_STORAGE_ERROR;
 c->stored=1;return 0;
}
static int incoming(wa_messenger *m,const wa_node *node){
 /* Do not acknowledge/drop a decrypted text if the app event queue is full.
  * The transport adapter closes and the server may redeliver on reconnect. */
 if(m->event_count>EVENTS-4)return -1;
 const char *from=wa_node_get(node,"from"),*id=wa_node_get(node,"id");char frombase[128];
 if(!id||!base(from,frombase))return ack(m,node,"message");
 const char *sender=wa_node_get(node,"participant");if(!sender)sender=from;
 const char *alt=wa_node_get(node,strstr(frombase,"@lid")?"sender_pn":"sender_lid");
 if(!alt)alt=wa_node_get(node,strstr(frombase,"@lid")?"peer_recipient_pn":"peer_recipient_lid");
 int own_sender=!strcmp(frombase,m->own)||(m->lid[0]&&!strcmp(frombase,m->lid));
 const char *recipient=wa_node_get(node,"recipient");
 const char *recipient_alt=recipient?wa_node_get(node,strstr(recipient,"@lid")?"recipient_pn":"recipient_lid"):NULL;
 if(!envelope_mapping(m,frombase,alt)||(own_sender&&!envelope_mapping(m,recipient,recipient_alt))) {
  event(m,"notice",id,frombase,"No se pudo guardar o verificar la dirección del contacto.",WA_INBOX_STORAGE_ERROR);return -1;
 }
 char crypto[128];copy(crypto,sizeof crypto,sender);
 if(strstr(sender,"@s.whatsapp.net")){char lid[128];if(mapped(m,sender,lid)&&strstr(lid,"@lid"))device(lid,crypto,device_id(sender));}
 int got=0,history=0;for(wa_node *e=node->child;e;e=e->next){const char *type=wa_node_get(e,"type");if(strcmp(e->tag,"enc")||!type||!e->data||(strcmp(type,"pkmsg")&&strcmp(type,"msg")))continue;
  /* Serialized Signal ciphertext includes framing/MAC and optional prekey
   * metadata; its plaintext fits in at most that many bytes. Allocate only for
   * this bounded stanza, rather than capping valid sync payloads at 16 KiB. */
  size_t capacity=e->data_len;
  if(!capacity||capacity>WA_NODE_BYTES){event(m,"notice",id,frombase,"Paquete recibido demasiado grande.",-1);continue;}
  uint8_t *plaintext=malloc(capacity);
  if(!plaintext){event(m,"notice",id,frombase,"Memoria insuficiente para procesar un mensaje recibido. No se confirmó su recepción.",-12);return -1;}
  incoming_commit commit={0};commit.m=m;commit.id=id;commit.from=frombase;
  size_t n=0;int r=wa_signal_decrypt_with_callback(m->signal,crypto,!strcmp(type,"pkmsg"),e->data,e->data_len,plaintext,capacity,&n,save_before_ratchet,&commit);
  if(r<0){
   OPENSSL_cleanse(plaintext,capacity);free(plaintext);
   if(commit.stored){
    wa_message_event *saved=&commit.saved;
    event(m,saved->kind,saved->id,saved->jid,saved->text,saved->state);
    event(m,"notice",id,frombase,"Texto conservado, pero no se pudo guardar la sesión Signal. Vuelve a conectar.",r);
    return -1;
   }
   if(r==WA_INBOX_STORAGE_ERROR){
    event(m,"notice",id,frombase,"No se pudo conservar un mensaje recibido. No se confirmó su recepción.",r);
    return -1;
   }
   /* Signal identifies authenticated retransmissions separately from failures. */
   if(r==-1001){event(m,"duplicate",id,frombase,"",0);continue;}
   event(m,"notice",id,frombase,r==-12?"Memoria insuficiente para procesar un mensaje recibido.":"No se pudo descifrar un mensaje (Signal).",r);continue;
  }
  unsigned pad=n?plaintext[n-1]:0;
  if(!pad||pad>16||pad>n){event(m,"bad-padding",id,frombase,"",0);OPENSSL_cleanse(plaintext,capacity);free(plaintext);continue;}
  int valid=1;for(unsigned j=0;j<pad;j++)if(plaintext[n-j-1]!=pad)valid=0;
  if(valid&&own_sender){int code=wa_history_response(plaintext,n-pad);if(code>=0)event(m,"history-status","","",code==0?"El móvil principal aceptó la solicitud de historial":code==4?"El móvil principal no admite esta solicitud de historial completo. Prueba recuperar mensajes desde un chat.":"El móvil principal rechazó la solicitud de historial",code==0?1:-1);}
  if(valid&&commit.history){history=1;event(m,"history","","","Historial recibido; pendiente de importar",0);}
  else if(valid&&commit.stored){
   wa_message_event *saved=&commit.saved;
   event(m,saved->kind,saved->id,saved->jid,saved->text,saved->state);got=1;
  } else event(m,valid?"ignored-payload":"bad-padding",id,frombase,"",0);
  OPENSSL_cleanse(plaintext,capacity);free(plaintext);
 }
 /* Push names are display metadata, never a phone-number or identity proof.
  * Learn them only after a supported, authenticated incoming text. */
 const char *name=wa_node_get(node,"notify");
 if(got&&strcmp(frombase,m->own)&&(!m->lid[0]||strcmp(frombase,m->lid))&&name) {
  size_t length=strlen(name);char peer[128];
  if(length&&length<=256&&utf8((const uint8_t*)name,length)&&wa_messenger_resolve(m,frombase,peer))
   event(m,"contact","",peer,name,0);
 }
 /* Unsupported payloads are acknowledged without claiming decryption/delivery. */
 int ok=ack(m,node,"message");
 if(got||history){wa_tree *t=wa_tree_create();if(!t)return 0;wa_node *r=wa_node_add(t,NULL,"receipt");ok=ok&&r&&wa_node_attr(r,"to",from)&&wa_node_attr(r,"id",id)&&(!history||wa_node_attr(r,"type","hist_sync"));if(ok)ok=send(m,t);else wa_tree_free(t);}
 return ok;
}
int wa_messenger_node(wa_messenger *m,const wa_node *node){
 if(!m||!node)return 0;const char *id=wa_node_get(node,"id"),*type=wa_node_get(node,"type");
 if(!strcmp(node->tag,"iq")&&id){
  if(!strcmp(id,"legacy-prekeys-count")&&m->boot==1){uint32_t count=0;const char *value=wa_node_get(wa_node_child(node,"count"),"value");if(!type||strcmp(type,"result")||!value||strspn(value,"0123456789")!=strlen(value))goto boot_error;count=(uint32_t)strtoul(value,NULL,10);if(!(count>16?activate(m):upload(m)))goto boot_error;return 1;}
  if(!strcmp(id,"legacy-prekeys-upload")&&m->boot==2){if(!type||strcmp(type,"result")||!activate(m))goto boot_error;return 1;}
  if(!strcmp(id,"legacy-active")&&m->boot==3){if(!type||strcmp(type,"result"))goto boot_error;m->ready=1;m->boot=0;event(m,"ready","","","Conectado",1);return 1;}
  if(m->pending&&!strcmp(id,m->query)){if(!type||strcmp(type,"result"))return error(m,"WhatsApp rechazó la consulta de dispositivos o claves.");if(m->phase==1){int r=prepare_devices(m,node);return r==2?transmit(m):r;}if(m->phase==2)return bundles(m,node)?transmit(m):0;}
 }
 if(!strcmp(node->tag,"ack")&&id&&wa_node_get(node,"class")&&!strcmp(wa_node_get(node,"class"),"message")){
  const char *code=wa_node_get(node,"error");event(m,"status",id,"",code?"WhatsApp rechazó el mensaje":"Aceptado por el servidor",code?-1:2);if(m->pending&&!strcmp(id,m->id)){m->pending=0;m->phase=0;}return 1;
 }
 if(!strcmp(node->tag,"receipt")){int state=type&&!strcmp(type,"read")?4:!type||!strcmp(type,"delivery")?3:0;
  char peer[128],resolved[128];const char *from=wa_node_get(node,"from");
  int known=base(from,peer);
  if(known&&mapped(m,peer,resolved)&&strstr(resolved,"@s.whatsapp.net"))copy(peer,sizeof peer,resolved);
  /* Attribute confirmations to their actual peer, never an own-device copy.
   * read-self records our own reading and is not a recipient read receipt. */
  if(state&&id&&known){event(m,"status",id,peer,"",state);wa_node *list=wa_node_child(node,"list");for(wa_node *item=list?list->child:NULL;item;item=item->next){const char *other=wa_node_get(item,"id");if(other)event(m,"status",other,peer,"",state);}}
  if(type&&!strcmp(type,"retry")&&id)event(m,"status",id,"","El destinatario pidió repetir el cifrado. Reintento automático pendiente.",-1);
  return ack(m,node,"receipt");
 }
 if(!strcmp(node->tag,"message"))return incoming(m,node);
 return 1;
 boot_error:m->boot=0;event(m,"notice","","","No se pudieron preparar las claves de mensajería. Vuelve a conectar.",-1);return 0;
}
void wa_messenger_tick(wa_messenger *m,uint64_t now){if(!m)return;int first=m->now==0;m->now=now;
 /* The first tick establishes the external monotonic clock origin. */
 if(first&&m->deadline)m->deadline+=now;
 if(m->deadline&&now>=m->deadline){m->deadline=0;if(m->pending){event(m,"status",m->id,m->peer,m->phase==3?"Sin confirmación; el envío podría haberse aceptado":"La consulta agotó el tiempo",m->phase==3?-2:-1);m->pending=0;m->phase=0;}if(m->boot){m->boot=0;event(m,"notice","","","Tiempo agotado al preparar la mensajería.",-1);}}
}
const wa_message_event *wa_messenger_event(const wa_messenger *m){return m&&m->event_count?m->events:NULL;}
void wa_messenger_event_done(wa_messenger *m){if(!m||!m->event_count)return;OPENSSL_cleanse(m->events,sizeof *m->events);m->event_count--;memmove(m->events,m->events+1,m->event_count*sizeof *m->events);}

int wa_messenger_stored(wa_messenger *m,const char *id,const char *jid,int outgoing) {
 return m&&wa_inbox_confirm(m->inbox,id,jid,outgoing);
}
