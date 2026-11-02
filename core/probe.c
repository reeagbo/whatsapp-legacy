/* Desktop-only transport adapter: establishes TLS + WebSocket and sends a fresh ClientHello.
 * Does not authenticate a WhatsApp account or persist credentials.
 */
#define _POSIX_C_SOURCE 200809L
#include "noise.h"
#include "wire.h"
#include "ws.h"
#include "handshake.h"
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int connect_tcp(const char *host,const char *port){
 struct addrinfo hint={0},*head=NULL,*a;hint.ai_family=AF_UNSPEC;hint.ai_socktype=SOCK_STREAM;
 if(getaddrinfo(host,port,&hint,&head))return -1;
 int fd=-1;for(a=head;a;a=a->ai_next){fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(fd<0)continue;
 struct timeval tv={8,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
 if(connect(fd,a->ai_addr,a->ai_addrlen)==0)break;
 close(fd);fd=-1;}
 freeaddrinfo(head);return fd;
}
static int writeall(SSL *s,const uint8_t *data,size_t n){while(n){int k=SSL_write(s,data,(int)(n>16384?16384:n));if(k<=0)return 0;data+=k;n-=(size_t)k;}return 1;}
static int read_exact(SSL *ssl,uint8_t *buffer,size_t n) {
 size_t pos=0;while(pos<n){int r=SSL_read(ssl,buffer+pos,(int)(n-pos));if(r<=0)return 0;pos+=(size_t)r;}return 1;
}
static int read_server_frame(SSL *ssl,uint8_t *data,size_t cap,size_t *size) {
 uint8_t h[10];if(!read_exact(ssl,h,2))return 0;
 if((h[0]&0xf0)!=0x80||(h[0]&0x0f)!=2||(h[1]&0x80))return 0;
 size_t n=(size_t)(h[1]&127);
 if(n==126){if(!read_exact(ssl,h+2,2))return 0;n=((size_t)h[2]<<8)|h[3];}
 else if(n==127){if(!read_exact(ssl,h+2,8))return 0;n=0;for(int i=2;i<10;i++)n=(n<<8)|h[i];}
 if(n>cap)return 0;
 *size=n;
 return read_exact(ssl,data,n);
}
static int readheaders(SSL *s,char *out,size_t cap,size_t *n){*n=0;while(*n<cap){int c=SSL_read(s,out+*n,1);if(c!=1)return 0;(*n)++;if(*n>=4&&!memcmp(out+*n-4,"\r\n\r\n",4))return 1;}return 0;}
#include "pairing.h"
int main(void){
 const char *host="web.whatsapp.com";int fd=connect_tcp(host,"443");
 if(fd<0){fprintf(stderr,"No network/DNS access to server.\n");return 2;}
 SSL_CTX *ctx=SSL_CTX_new(TLS_client_method());SSL *ssl=NULL;wa_pair_session *session=NULL;int rc=1;
 char dir[]="/tmp/wa-online-XXXXXX",path[256]={0};
 if(!ctx||!mkdtemp(dir))goto done;
 snprintf(path,sizeof path,"%s/identity",dir);
 SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,NULL);if(SSL_CTX_set_default_verify_paths(ctx)!=1)goto done;
 ssl=SSL_new(ctx);
 if(!ssl||SSL_set_tlsext_host_name(ssl,host)!=1||SSL_set1_host(ssl,host)!=1||SSL_set_fd(ssl,fd)!=1||SSL_connect(ssl)!=1||SSL_get_verify_result(ssl)!=X509_V_OK)goto done;
 uint8_t nonce[16];char request[1024],expected[29],response[8192];size_t hlen;
 if(RAND_bytes(nonce,16)!=1||!wa_ws_upgrade_request(nonce,request,sizeof request,expected)||!writeall(ssl,(uint8_t*)request,strlen(request))||!readheaders(ssl,response,sizeof response,&hlen)||!wa_ws_upgrade_validate(response,hlen,expected))goto done;
 session=wa_pair_create(path,"");if(!session||!wa_pair_start(session))goto done;
 for(int count=0;count<12;count++){
  size_t len=0;const uint8_t *out;
  while((out=wa_pair_output(session,&len))!=NULL){
   uint8_t mask[4];uint8_t *frame=malloc(len+16);size_t written=0;
   int ok=frame&&RAND_bytes(mask,4)==1&&wa_ws_encode(2,out,len,mask,frame,len+16,&written)&&writeall(ssl,frame,written);
   free(frame);if(!ok)goto done;wa_pair_output_done(session);
  }
  if(wa_pair_state(session)==6){puts("PASS: authenticated TLS + Noise + accepted registration envelope, no phone/account used");rc=0;goto done;}
  if(wa_pair_state(session)<0){fprintf(stderr,"%s\n",wa_pair_status(session));goto done;}
  uint8_t *data=malloc(1024*1024);size_t received=0;
  int ok=data&&read_server_frame(ssl,data,1024*1024,&received)&&wa_pair_feed(session,data,received);free(data);
  if(!ok){fprintf(stderr,"%s\n",wa_pair_status(session));goto done;}
 }
 done:
 wa_pair_free(session);if(path[0])unlink(path);if(path[0])rmdir(dir);
 if(ssl){SSL_shutdown(ssl);SSL_free(ssl);}if(ctx)SSL_CTX_free(ctx);close(fd);
 if(rc)fprintf(stderr,"Live connection not verified. No account was used.\n");
 return rc;
}
