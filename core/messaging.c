#include "messaging.h"
#include "signal_engine.h"
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
 wa_identity me;wa_storage io;wa_signal *signal;wa_send_node send;void *user;
 char own[128],lid[128];
 int ready,boot,pending,phase;uint64_t now,deadline;
 char peer[128],route[128],id[96],query[128];uint8_t text[WA_TEXT_MAX+1];size_t text_len;
 struct {char jid[128];int own;} devices[DEVICES];size_t count;
 wa_message_event events[EVENTS];size_t event_count;
};
static int copy(char *out,size_t cap,const char *s){if(!s||strlen(s)>=cap)return 0;memcpy(out,s,strlen(s)+1);return 1;}
static int base(const char *jid,char out[128]){
 if(!jid)return 0;const char *at=strchr(jid,'@'),*colon=strchr(jid,':');if(!at||at-jid<1||at-jid>40)return 0;
 size_t n=(size_t)((colon&&colon<at?colon:at)-jid);for(size_t i=0;i<n;i++)if(jid[i]<'0'||jid[i]>'9')return 0;
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
static int mapping(wa_messenger *m,const char *lid,const char *pn){char l[128],p[128],key[80];if(!base(lid,l)||!base(pn,p))return 0;mapkey(l,key);if(!m->io.put(m->io.user,key,(uint8_t*)p,strlen(p)))return 0;mapkey(p,key);return m->io.put(m->io.user,key,(uint8_t*)l,strlen(l));}
static int mapped(wa_messenger *m,const char *jid,char out[128]){char b[128],key[80];if(!base(jid,b))return 0;mapkey(b,key);size_t n=0;int r=m->io.get(m->io.user,key,(uint8_t*)out,127,&n);if(r!=1||n>127)return 0;out[n]=0;char valid[128];return base(out,valid);}
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
 if(!base(id->jid,m->own)||(lid&&*lid&&!base(lid,m->lid))||!(m->signal=wa_signal_create(id,io))){wa_messenger_free(m);return NULL;}
 if(m->lid[0]&&!mapping(m,m->lid,m->own)){wa_messenger_free(m);return NULL;}return m;
}
void wa_messenger_free(wa_messenger *m){if(!m)return;wa_signal_free(m->signal);OPENSSL_cleanse(m,sizeof *m);free(m);}
int wa_messenger_start(wa_messenger *m){if(!m)return 0;m->boot=1;m->deadline=m->now+45000;wa_tree *t=iq("get","legacy-prekeys-count","encrypt");if(!t||!wa_node_add(t,t->root,"count")){wa_tree_free(t);return 0;}return send(m,t);}
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
 memcpy(m->text,text,n);m->text[n]=0;m->text_len=n;m->count=0;m->pending=1;copy(m->route,sizeof m->route,m->peer);
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
static int transmit(wa_messenger *m){
 uint8_t plain[WA_TEXT_MAX+64],sync[WA_TEXT_MAX+512],outer[WA_TEXT_MAX+600];size_t n=0,sn=0,on=0;
 if(!pb(1,m->text,m->text_len,plain,sizeof plain,&n)||!pb(1,(uint8_t*)m->peer,strlen(m->peer),sync,sizeof sync,&sn)||!pb(2,plain,n,sync,sizeof sync,&sn)||!pb(31,sync,sn,outer,sizeof outer,&on))return error(m,"Texto demasiado largo.");
 wa_tree *t=wa_tree_create();if(!t)return error(m,"Memoria insuficiente.");wa_node *root=wa_node_add(t,NULL,"message");int ok=root&&wa_node_attr(root,"id",m->id)&&wa_node_attr(root,"to",m->route)&&wa_node_attr(root,"type","text");wa_node *participants=wa_node_add(t,root,"participants");ok=ok&&participants;int needs_identity=0;
 for(size_t i=0;i<m->count&&ok;i++){
  uint8_t input[WA_TEXT_MAX+640],ct[WA_TEXT_MAX+2048],random;size_t size=m->devices[i].own?on:n,cn=0;memcpy(input,m->devices[i].own?outer:plain,size);
  if(RAND_bytes(&random,1)!=1){ok=0;break;}unsigned pad=(random&15)+1;memset(input+size,pad,pad);size+=pad;int ispk=0;
  int r=wa_signal_encrypt(m->signal,m->devices[i].jid,input,size,ct,sizeof ct,&cn,&ispk);OPENSSL_cleanse(input,sizeof input);
  if(r<0){ok=0;break;}
  wa_node *to=wa_node_add(t,participants,"to");wa_node *enc=data(t,to,"enc",ct,cn);ok=to&&enc&&wa_node_attr(to,"jid",m->devices[i].jid)&&wa_node_attr(enc,"v","2")&&wa_node_attr(enc,"type",ispk?"pkmsg":"msg");if(ispk)needs_identity=1;
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
static int incoming(wa_messenger *m,const wa_node *node){
 const char *from=wa_node_get(node,"from"),*id=wa_node_get(node,"id");char frombase[128];
 if(!id||!base(from,frombase))return ack(m,node,"message");
 const char *sender=wa_node_get(node,"participant");if(!sender)sender=from;
 const char *alt=wa_node_get(node,"sender_pn");if(alt&&strstr(from,"@lid"))mapping(m,from,alt);
 char crypto[128];copy(crypto,sizeof crypto,sender);
 if(strstr(sender,"@s.whatsapp.net")){char lid[128];if(mapped(m,sender,lid)&&strstr(lid,"@lid"))device(lid,crypto,device_id(sender));}
 int got=0;for(wa_node *e=node->child;e;e=e->next){const char *type=wa_node_get(e,"type");if(strcmp(e->tag,"enc")||!type||!e->data||(strcmp(type,"pkmsg")&&strcmp(type,"msg")))continue;
  uint8_t plaintext[16384];size_t n=0;int r=wa_signal_decrypt(m->signal,crypto,!strcmp(type,"pkmsg"),e->data,e->data_len,plaintext,sizeof plaintext,&n);
  if(r<0){event(m,"notice",id,frombase,"No se pudo descifrar un mensaje (Signal).",r);continue;}
  unsigned pad=n?plaintext[n-1]:0;if(!pad||pad>16||pad>n){OPENSSL_cleanse(plaintext,sizeof plaintext);continue;}int valid=1;for(unsigned j=0;j<pad;j++)if(plaintext[n-j-1]!=pad)valid=0;
  char text[WA_TEXT_MAX+1],dest[128]="",peer[128];int own=0;
  if(valid&&decode_text(plaintext,n-pad,text,dest,&own,0)){
   if(own){if(!base(dest,peer)){OPENSSL_cleanse(plaintext,sizeof plaintext);continue;}}
   else if(!mapped(m,frombase,peer)||!strstr(peer,"@s.whatsapp.net"))copy(peer,sizeof peer,frombase);
   event(m,own?"sent-copy":"message",id,peer,text,own?2:0);got=1;
  }
  OPENSSL_cleanse(plaintext,sizeof plaintext);
 }
 /* Unsupported payloads are acknowledged without claiming decryption/delivery. */
 int ok=ack(m,node,"message");
 if(got){wa_tree *t=wa_tree_create();if(!t)return 0;wa_node *r=wa_node_add(t,NULL,"receipt");ok=ok&&r&&wa_node_attr(r,"to",from)&&wa_node_attr(r,"id",id);if(ok)ok=send(m,t);else wa_tree_free(t);}
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
  const char *code=wa_node_get(node,"error");event(m,"status",id,m->peer,code?"WhatsApp rechazó el mensaje":"Aceptado por el servidor",code?-1:2);if(m->pending&&!strcmp(id,m->id)){m->pending=0;m->phase=0;}return 1;
 }
 if(!strcmp(node->tag,"receipt")){int state=type&&(!strcmp(type,"read")||!strcmp(type,"read-self"))?4:!type||!strcmp(type,"delivery")?3:0;
  if(state&&id){event(m,"status",id,"","",state);wa_node *list=wa_node_child(node,"list");for(wa_node *item=list?list->child:NULL;item;item=item->next){const char *other=wa_node_get(item,"id");if(other)event(m,"status",other,"","",state);}}
  if(type&&!strcmp(type,"retry")&&id)event(m,"status",id,"","El destinatario pidió repetir el cifrado. Reintento automático pendiente.",-1);
  return ack(m,node,"receipt");
 }
 if(!strcmp(node->tag,"message"))return incoming(m,node);
 return 1;
 boot_error:m->boot=0;event(m,"notice","","","No se pudieron preparar las claves de mensajería. Vuelve a conectar.",-1);return 0;
}
void wa_messenger_tick(wa_messenger *m,uint64_t now){if(!m)return;uint64_t delta=m->now?now-m->now:0;m->now=now;
 /* The first tick establishes the external monotonic clock origin. */
 if(!delta&&m->deadline&&m->deadline<now)m->deadline=now+45000;
 if(m->deadline&&now>=m->deadline){m->deadline=0;if(m->pending){event(m,"status",m->id,m->peer,m->phase==3?"Sin confirmación; el envío podría haberse aceptado":"La consulta agotó el tiempo",m->phase==3?-2:-1);m->pending=0;m->phase=0;}if(m->boot){m->boot=0;event(m,"notice","","","Tiempo agotado al preparar la mensajería.",-1);}}
}
const wa_message_event *wa_messenger_event(const wa_messenger *m){return m&&m->event_count?m->events:NULL;}
void wa_messenger_event_done(wa_messenger *m){if(!m||!m->event_count)return;OPENSSL_cleanse(m->events,sizeof *m->events);m->event_count--;memmove(m->events,m->events+1,m->event_count*sizeof *m->events);}
