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
int main(void){
 const char *host="web.whatsapp.com";int fd=connect_tcp(host,"443");if(fd<0){fprintf(stderr,"No hay acceso de red/DNS al servidor; prueba online no ejecutada.\n");return 2;}
 SSL_CTX *ctx=SSL_CTX_new(TLS_client_method());SSL *ssl=NULL;int rc=1;
 if(!ctx)goto done;
 SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,NULL);if(SSL_CTX_set_default_verify_paths(ctx)!=1)goto done;
 ssl=SSL_new(ctx);if(!ssl||SSL_set_tlsext_host_name(ssl,host)!=1||SSL_set1_host(ssl,host)!=1||SSL_set_fd(ssl,fd)!=1||SSL_connect(ssl)!=1||SSL_get_verify_result(ssl)!=X509_V_OK)goto done;
 uint8_t nonce[16];char request[1024],expected[29],response[8192];size_t hlen;
 if(RAND_bytes(nonce,sizeof nonce)!=1||!wa_ws_upgrade_request(nonce,request,sizeof request,expected))goto done;
 if(!writeall(ssl,(uint8_t*)request,strlen(request))||!readheaders(ssl,response,sizeof response,&hlen)||!wa_ws_upgrade_validate(response,hlen,expected))goto done;
 puts("TLS y WebSocket: conexión autenticada a web.whatsapp.com correcta.");
 wa_noise noise;if(!wa_noise_init(&noise))goto done;
 uint8_t hello[80],framed[128],ws_frame[256],mask[4];size_t hn,fn,wn;
 if(!wa_make_client_hello(noise.ephemeral_public,hello,sizeof hello,&hn)||!wa_noise_frame(hello,hn,1,framed,sizeof framed,&fn)||RAND_bytes(mask,4)!=1||!wa_ws_encode(2,framed,fn,mask,ws_frame,sizeof ws_frame,&wn))goto done;
 if(!writeall(ssl,ws_frame,wn))goto done;
 puts("ClientHello enviado. Leyendo ServerHello...");
 uint8_t response_frame[70000];size_t bytes=0;
 if(!read_server_frame(ssl,response_frame,sizeof response_frame,&bytes)){
  fprintf(stderr,"No se pudo leer un frame binario ServerHello completo.\n");goto done;
 }
 const uint8_t *inner=NULL;size_t ilen=0,used=0;
 if(!wa_noise_unframe(response_frame,bytes,&inner,&ilen,&used)||used!=bytes){
  fprintf(stderr,"Formato WA framing incorrecto.\n");goto done;
 }
 wa_server_hello server;
 if(!wa_parse_server_hello(inner,ilen,&server)){
  fprintf(stderr,"ServerHello Protobuf no válido.\n");goto done;
 }
 puts("ServerHello recibido: comprobando canal Noise y cadena de certificados...");
 uint8_t spriv[32],spub[32],encrypted_static[48];
 if(RAND_bytes(spriv,32)!=1)goto done;
 EVP_PKEY *kp=EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519,NULL,spriv,32);
 size_t publen=32;
 int generated=kp&&EVP_PKEY_get_raw_public_key(kp,spub,&publen)==1&&publen==32;
 EVP_PKEY_free(kp);
 if(!generated)goto done;
 int verified=wa_process_server_hello(&noise,&server,spriv,spub,encrypted_static);
 OPENSSL_cleanse(spriv,sizeof spriv);
 if(!verified){fprintf(stderr,"ServerHello: autenticación Noise/certificado no verificada.\n");goto done;}
 puts("Certificado Noise autenticado. Registro de cuenta todavía no implementado; no se envía ClientFinish.");
 rc=0;
 done:
 if(rc==1)fprintf(stderr,"Falló la negociación TLS/WebSocket o el envío inicial.\n");
 if(ssl){SSL_shutdown(ssl);SSL_free(ssl);}if(ctx)SSL_CTX_free(ctx);close(fd);return rc;
}
