#define _POSIX_C_SOURCE 200809L
/* Cryptographic fixtures generated independently with Python cryptography. */
#include "history.c"
#include "storage_posix.h"
#include <assert.h>
#include <unistd.h>
static const uint8_t inline_task[]={90,130,1,120,156,19,234,102,228,226,53,52,50,54,49,53,51,183,176,116,200,201,76,217,197,36,101,108,130,16,41,214,43,207,72,44,41,78,44,40,208,203,75,45,217,196,196,239,151,159,155,84,148,170,144,95,148,153,158,153,151,152,35,228,205,229,201,37,139,102,134,0,163,20,151,135,103,112,136,127,80,164,174,167,139,144,2,151,156,111,106,94,113,98,86,170,66,70,102,113,201,225,205,69,153,201,249,86,10,201,137,105,135,87,42,124,152,223,211,43,209,240,232,252,42,54,5,22,0,1,96,43,209};
static const uint8_t media_task[]={10,32,168,128,53,40,121,145,150,139,153,232,153,177,50,99,49,178,62,119,131,194,184,255,93,65,121,230,38,171,203,252,174,25,16,130,1,26,32,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,34,32,33,95,150,171,152,179,205,115,125,16,251,39,226,13,162,106,47,45,223,32,91,115,180,55,128,10,61,113,167,141,75,88,42,27,47,109,109,115,47,109,100,45,97,112,112,45,115,116,97,116,101,47,115,121,110,116,104,101,116,105,99};
static const uint8_t media[]={57,106,7,142,40,210,16,232,57,205,176,239,87,106,60,145,239,21,251,145,241,185,10,153,216,231,2,67,178,9,60,89,183,71,242,64,67,35,49,143,29,99,111,72,190,42,239,14,183,117,170,139,10,100,153,98,51,163,110,29,70,218,230,40,125,159,254,6,117,22,62,1,228,93,143,37,30,23,199,103,183,229,238,20,5,202,173,3,173,10,106,198,249,61,65,82,23,161,112,134,223,5,70,113,251,206,77,208,228,41,103,182,99,148,12,24,0,30,77,44,249,37,244,14,19,175,19,145,206,118,57,146,251,45,217,213,192,160,153,91,122,13,144,152,109,76,134,223,222,155,216,198,35,39};
static void check_path(const char *path,int expected) {
 uint8_t task[1024];size_t n=0;char out[1024];
 assert(wa_pb_write_bytes(5,(const uint8_t*)path,strlen(path),task,sizeof task,&n));
 assert(wa_history_path(task,n,out,sizeof out)==expected);
}
int main(void) {
 check_path("/v/t62.71111-24/synthetic.enc?token=test",1);
 check_path("/mms/md-app-state/synthetic",1);
 check_path("//different-host.invalid/path",0);
 check_path("https://different-host.invalid/path",0);
 check_path("/v/invalid#fragment",0);
 check_path("/v/invalid\\path",0);
 check_path("/v/invalid\npath",0);

 const uint8_t rejection[]={0x62,9,0x8a,1,6,0x1a,4,0x32,2,0x10,4};
 assert(wa_history_response(rejection,sizeof rejection)==4);

 wa_history *h=wa_history_open(inline_task,sizeof inline_task,NULL,0);assert(h);wa_message_event e;uint64_t time=0;
 assert(wa_history_next(h,&e,&time)==1&&!strcmp(e.kind,"alias")&&!strcmp(e.jid,"34123456789@s.whatsapp.net"));
 assert(wa_history_next(h,&e,&time)==1&&!strcmp(e.kind,"chat")&&!strcmp(e.text,"Nombre original"));
 assert(wa_history_next(h,&e,&time)==1&&!strcmp(e.kind,"sent-copy")&&!strcmp(e.id,"HISTORY-ID")&&e.state==4&&time==1700000000000ULL);
 assert(wa_history_next(h,&e,&time)==0);wa_history_free(h);
 h=wa_history_open(media_task,sizeof media_task,media,sizeof media);assert(h);wa_history_free(h);
 uint8_t broken[sizeof media];memcpy(broken,media,sizeof media);broken[0]^=1;assert(!wa_history_open(media_task,sizeof media_task,broken,sizeof broken));
 uint8_t task[sizeof media_task];memcpy(task,media_task,sizeof task);broken[sizeof broken-1]^=1;uint8_t digest[32];SHA256(broken,sizeof broken,digest);wa_slice hash;assert(bytes((wa_slice){task,sizeof task},4,&hash));memcpy((uint8_t*)hash.ptr,digest,32);assert(!wa_history_open(task,sizeof task,broken,sizeof broken));
 char folder[]="/tmp/wa-history-XXXXXX";assert(mkdtemp(folder));wa_storage io;assert(wa_storage_posix(&io,folder));
 assert(wa_history_save(&io,inline_task,sizeof inline_task)&&wa_history_save(&io,inline_task,sizeof inline_task));uint8_t stored[8192];size_t n=0;
 assert(wa_history_task(&io,0,stored,sizeof stored,&n)==1&&n==sizeof inline_task);assert(wa_history_task(&io,1,stored,sizeof stored,&n)==0);
 assert(!wa_history_done(&io,0,media_task,sizeof media_task));assert(wa_history_done(&io,0,inline_task,sizeof inline_task));assert(wa_history_task(&io,0,stored,sizeof stored,&n)==0);
 /* Encode an outgoing delete from a verified, empty version-one snapshot,
  * then validate the encrypted mutation through the incoming patch decoder. */
 uint8_t raw_key[32]={7},key_id[]={10,1,9},version[]={8,1},derived[160],input[160]={0},snapshot_mac[32];
 unsigned mac_len=0;big64(input+128,1);memcpy(input+136,STATE_COLLECTION,strlen(STATE_COLLECTION));
 char filename[80];assert(key_file((wa_slice){key_id+2,1},filename));
 assert(io.put(io.user,filename,raw_key,sizeof raw_key));
 assert(expand(raw_key,sizeof raw_key,"WhatsApp Mutation Keys",derived,sizeof derived));
 assert(HMAC(EVP_sha256(),derived+96,32,input,136+strlen(STATE_COLLECTION),snapshot_mac,&mac_len));
 uint8_t snapshot[128],descriptor[64],stored_version[8];size_t sn=0,dn=0;
 assert(put_bytes(1,version,sizeof version,snapshot,sizeof snapshot,&sn));
 assert(put_bytes(3,snapshot_mac,32,snapshot,sizeof snapshot,&sn));
 assert(put_bytes(4,key_id,sizeof key_id,snapshot,sizeof snapshot,&sn));
 assert(put_number(101,1,descriptor,sizeof descriptor,&dn));
 assert(put_bytes(109,(const uint8_t*)"request",7,descriptor,sizeof descriptor,&dn));
 big64(stored_version,1);assert(io.put(io.user,STATE_VERSION_KEY,stored_version,8));
 h=calloc(1,sizeof *h);assert(h&&state_open(h,&io,(wa_slice){descriptor,dn},(wa_slice){snapshot,sn},NULL));
 wa_tree *tree=wa_history_delete_patch(h,&io,"outgoing-test","123456789@lid","last",0,1700000000000ULL,1700000002000ULL);
 assert(tree);wa_history_free(h);
 wa_node *collection=wa_node_child(wa_node_child(tree->root,"sync"),"collection");
 assert(collection&&!strcmp(wa_node_get(collection,"version"),"1"));
 wa_node *patch_node=wa_node_child(collection,"patch");assert(patch_node&&patch_node->data_len);
 uint8_t patch_task[1200];size_t ptn=0;
 assert(put_number(101,2,patch_task,sizeof patch_task,&ptn));
 assert(put_number(108,2,patch_task,sizeof patch_task,&ptn));
 assert(put_bytes(103,patch_node->data,patch_node->data_len,patch_task,sizeof patch_task,&ptn));
 h=wa_history_open_sync(&io,patch_task,ptn,NULL,0,NULL);
 assert(h&&wa_history_next(h,&e,&time)==1&&!strcmp(e.kind,"delete-chat")&&!strcmp(e.jid,"123456789@lid"));
 wa_history_free(h);wa_tree_free(tree);
 assert(wa_state_version(&io)==1&&io.put(io.user,STATE_VERSION_KEY,NULL,0)&&io.put(io.user,filename,NULL,0));
 wa_storage_posix_close(&io);assert(!rmdir(folder));
 /* A remote chat removal carries the last included message in Unix seconds. */
 const uint8_t index[]="[\"deleteChat\",\"123456789@lid\",\"1\"]";
 uint8_t action[64],operation[32],range[16];size_t an=0,on=0,rn=0;
 assert(put_number(1,1700000000ULL,range,sizeof range,&rn));
 assert(put_bytes(1,range,rn,operation,sizeof operation,&on));
 assert(put_number(1,1700000001000ULL,action,sizeof action,&an));
 assert(put_bytes(22,operation,on,action,sizeof action,&an));
 h=calloc(1,sizeof *h);assert(h);h->state_job=2;
 assert(add_action(h,(wa_slice){index,sizeof index-1},(wa_slice){action,an}));
 assert(wa_history_next(h,&e,&time)==1&&!strcmp(e.kind,"delete-chat")&&
        !strcmp(e.jid,"123456789@lid")&&!strcmp(e.text,"1700000000999")&&time==1700000001000ULL);
 assert(wa_history_next(h,&e,&time)==0);wa_history_free(h);
 puts("PASS: independent history media fixture, hashes/HMAC, inline decompression, original dates/status, names, aliases and durable replay/acknowledgement");return 0;
}
