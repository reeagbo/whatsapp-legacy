#define _POSIX_C_SOURCE 200809L
#include "storage_posix.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int path(char *out, size_t cap, const char *dir, const char *key) {
  if (!key || !*key || strspn(key,"abcdefghijklmnopqrstuvwxyz0123456789-") != strlen(key)) return 0;
  int n=snprintf(out,cap,"%s/%s",dir,key); return n>0&&(size_t)n<cap;
}
static int get(void *u,const char *key,uint8_t *out,size_t cap,size_t *len) {
  char p[1400]; if(!path(p,sizeof p,u,key))return -1;
  int fd=open(p,O_RDONLY|O_NOFOLLOW);if(fd<0)return errno==ENOENT?0:-1;
  struct stat st;int ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=0&&(uint64_t)st.st_size<=cap;
  size_t n=0;while(ok&&n<(size_t)st.st_size){ssize_t k=read(fd,out+n,(size_t)st.st_size-n);if(k<0&&errno==EINTR)continue;if(k<=0){ok=0;break;}n+=(size_t)k;}
  close(fd);*len=n;return ok?1:-1;
}
static int put(void *u,const char *key,const uint8_t *data,size_t n) {
  char p[1400],tmp[1440];if(!path(p,sizeof p,u,key))return 0;
  if(!data){if(unlink(p)&&errno!=ENOENT)return 0;}else{
    snprintf(tmp,sizeof tmp,"%s-XXXXXX",p);int fd=mkstemp(tmp);if(fd<0)return 0;
    int ok=fchmod(fd,0600)==0;size_t k=0;
    while(ok&&k<n){ssize_t z=write(fd,data+k,n-k);if(z<0&&errno==EINTR)continue;if(z<=0){ok=0;break;}k+=(size_t)z;}
    if(ok)ok=fsync(fd)==0;
    if(close(fd))ok=0;
    if(ok)ok=rename(tmp,p)==0;
    if(!ok){unlink(tmp);return 0;}
  }
  int d=open(u,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(d<0)return 0;int ok=fsync(d)==0;close(d);return ok;
}
int wa_storage_posix(wa_storage *out,const char *dir){
  if(!out||!dir||strlen(dir)>1024)return 0;
  if(mkdir(dir,0700)&&errno!=EEXIST)return 0;
  struct stat st;if(lstat(dir,&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return 0;
  char *copy=strdup(dir);if(!copy)return 0;
  *out=(wa_storage){copy,get,put};return 1;
}
void wa_storage_posix_close(wa_storage *s){if(s){free(s->user);memset(s,0,sizeof *s);}}
