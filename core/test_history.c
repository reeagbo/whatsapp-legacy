#define _POSIX_C_SOURCE 200809L
/* Cryptographic fixtures generated independently with Python cryptography. */
#include "history.c"
#include "storage_posix.h"
#include "test_state_tombstones.h"
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
/* Force caller stack reuse between constructing and serializing a query. */
static void overwrite_query_stack(void) {
 volatile uint8_t scratch[8192];
 for(size_t i=0;i<sizeof scratch;i++)scratch[i]=0xa5;
}
static void (*volatile reuse_query_stack)(void)=overwrite_query_stack;
int main(void) {
 /* App-state queries go to the bare server domain (token), not an empty-user
  * JID. Other IQ services accepting that JID does not establish sync routing. */
 wa_tree *query=wa_state_query(NULL,"routing-test",0,1);assert(query);
 reuse_query_stack();
 wa_node *before_collection=wa_node_child(wa_node_child(query->root,"sync"),"collection");
 assert(before_collection&&!strcmp(wa_node_get(before_collection,"version"),"0"));
 assert(!strcmp(wa_node_get(query->root,"to"),"s.whatsapp.net"));
 uint8_t query_wire[1024];size_t query_size=0;
 assert(wa_node_encode(query->root,query_wire,sizeof query_wire,&query_size));
 wa_tree *decoded_query=wa_node_decode(query_wire,query_size);assert(decoded_query);
 assert(!strcmp(wa_node_get(decoded_query->root,"to"),"s.whatsapp.net"));
 wa_node *query_collection=wa_node_child(wa_node_child(decoded_query->root,"sync"),"collection");
 assert(query_collection&&!strcmp(wa_node_get(query_collection,"name"),"regular_low"));
 assert(!strcmp(wa_node_get(query_collection,"version"),"0"));
 wa_tree_free(decoded_query);wa_tree_free(query);

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
 /* A real REMOVE may omit value. Its MAC/index still must authenticate; SET
  * without value and malformed supplied values must remain rejected. */
 const char *tombstone_stage;h=calloc(1,sizeof *h);assert(h);h->state_job=2;
 assert(verify_record(h,&io,(wa_slice){remove_index_only,sizeof remove_index_only},1,NULL,NULL,NULL,&tombstone_stage));
 assert(wa_history_next(h,&e,&time)==0);
 assert(!verify_record(h,&io,(wa_slice){set_missing_action,sizeof set_missing_action},0,NULL,NULL,NULL,&tombstone_stage));
 assert(!strcmp(tombstone_stage,"record-action-missing"));
 assert(!verify_record(h,&io,(wa_slice){remove_malformed_action,sizeof remove_malformed_action},1,NULL,NULL,NULL,&tombstone_stage));
 assert(!strcmp(tombstone_stage,"record-action-format"));
 uint8_t bad_tombstone[sizeof remove_index_only];memcpy(bad_tombstone,remove_index_only,sizeof bad_tombstone);
 bad_tombstone[60]^=1;
 assert(!verify_record(h,&io,(wa_slice){bad_tombstone,sizeof bad_tombstone},1,NULL,NULL,NULL,&tombstone_stage));
 assert(!strcmp(tombstone_stage,"record-value-mac"));wa_history_free(h);
 /* Oversized snapshot records must reach authentication; the outer download
  * limit is the bound. Unauthenticated large values must still be rejected. */
 size_t large_size=65552,large_used=0,envelope_used=0;
 uint8_t *large_value=calloc(1,large_size),*large_blob=malloc(large_size+16),*large_record=malloc(large_size+128);
 assert(large_value&&large_blob&&large_record);
 assert(put_bytes(1,large_value,large_size,large_blob,large_size+16,&large_used));
 const uint8_t *fixture_record=remove_index_only;wa_slice fixture_index,fixture_key;
 assert(bytes((wa_slice){fixture_record,sizeof remove_index_only},1,&fixture_index));
 assert(bytes((wa_slice){fixture_record,sizeof remove_index_only},3,&fixture_key));
 assert(put_bytes(1,fixture_index.ptr,fixture_index.len,large_record,large_size+128,&envelope_used));
 assert(put_bytes(2,large_blob,large_used,large_record,large_size+128,&envelope_used));
 assert(put_bytes(3,fixture_key.ptr,fixture_key.len,large_record,large_size+128,&envelope_used));
 h=calloc(1,sizeof *h);assert(h);
 assert(!verify_record(h,&io,(wa_slice){large_record,envelope_used},1,NULL,NULL,NULL,&tombstone_stage));
 assert(!strcmp(tombstone_stage,"record-value-mac"));
 wa_history_free(h);free(large_value);free(large_blob);free(large_record);


 assert(expand(raw_key,sizeof raw_key,"WhatsApp Mutation Keys",derived,sizeof derived));
 assert(HMAC(EVP_sha256(),derived+96,32,input,136+strlen(STATE_COLLECTION),snapshot_mac,&mac_len));
 uint8_t snapshot[128],descriptor[64],stored_version[8];size_t sn=0,dn=0;
 assert(put_bytes(1,version,sizeof version,snapshot,sizeof snapshot,&sn));
 assert(put_bytes(3,snapshot_mac,32,snapshot,sizeof snapshot,&sn));
 assert(put_bytes(4,key_id,sizeof key_id,snapshot,sizeof snapshot,&sn));
 assert(put_number(101,1,descriptor,sizeof descriptor,&dn));
 assert(put_bytes(109,(const uint8_t*)"request",7,descriptor,sizeof descriptor,&dn));
 big64(stored_version,1);assert(io.put(io.user,STATE_VERSION_KEY,stored_version,8));
 const char *stage;
 h=calloc(1,sizeof *h);assert(h&&state_open_checked(h,&io,(wa_slice){descriptor,dn},(wa_slice){snapshot,sn},NULL,&stage));
 wa_tree *tree=wa_history_delete_patch(h,&io,"outgoing-test","123456789@lid","last",0,1700000000000ULL,1700000002000ULL,0);
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
 wa_history_free(h);
 /* The server returns an old snapshot plus newer deltas for an outgoing
  * operation. Reconstruct version two before generating another write. */
 uint8_t server_patch[1400],delta_version[]={8,2};size_t server_n=0;
 assert(put_bytes(1,delta_version,sizeof delta_version,server_patch,sizeof server_patch,&server_n));
 memcpy(server_patch+server_n,patch_node->data,patch_node->data_len);server_n+=patch_node->data_len;
 uint8_t write_task[1700];size_t write_n=0;
 memcpy(write_task,descriptor,dn);write_n=dn;
 assert(put_bytes(111,server_patch,server_n,write_task,sizeof write_task,&write_n));
 big64(stored_version,2);assert(io.put(io.user,STATE_VERSION_KEY,stored_version,8));
 h=calloc(1,sizeof *h);assert(h);
 assert(state_open_checked(h,&io,(wa_slice){write_task,write_n},(wa_slice){snapshot,sn},NULL,&stage));
 assert(h->state_version==1);
 assert(!wa_history_delete_patch(h,&io,"stale","123456789@lid","last",0,1700000000000ULL,1700000003000ULL,0));
 assert(replay_write_snapshot(h,&io,(wa_slice){write_task,write_n},NULL,&stage));
 assert(h->state_version==2&&h->action_count==0);
 wa_tree *next_write=wa_history_delete_patch(h,&io,"fresh","123456789@lid","last",0,1700000000000ULL,1700000003000ULL,0);
 assert(next_write);wa_tree_free(next_write);wa_history_free(h);
 /* Replay acknowledgements must not skip unapplied incoming changes. */
 assert(wa_history_save(&io,write_task,write_n));
 assert(wa_history_commit(&io,0,write_task,write_n,3));assert(wa_state_version(&io,0)==2);
 h=calloc(1,sizeof *h);assert(h);
 assert(state_open_checked(h,&io,(wa_slice){write_task,write_n},(wa_slice){snapshot,sn},NULL,&stage));
 write_task[write_n-8]^=1;
 assert(!replay_write_snapshot(h,&io,(wa_slice){write_task,write_n},NULL,&stage));wa_history_free(h);
 big64(stored_version,1);assert(io.put(io.user,STATE_VERSION_KEY,stored_version,8));
 wa_tree_free(tree);
 /* Exercise the same reconstruction for regular_low: archive, then generate
  * and decrypt the next unarchive mutation from the reconstructed base. */
 {
  uint8_t low_snapshot[128],low_descriptor[128],low_input[160]={0},low_mac[32];size_t ln=0,ldn=0;
  big64(low_input+128,1);memcpy(low_input+136,"regular_low",11);
  assert(HMAC(EVP_sha256(),derived+96,32,low_input,147,low_mac,&mac_len));
  assert(put_bytes(1,version,sizeof version,low_snapshot,sizeof low_snapshot,&ln));
  assert(put_bytes(3,low_mac,32,low_snapshot,sizeof low_snapshot,&ln));
  assert(put_bytes(4,key_id,sizeof key_id,low_snapshot,sizeof low_snapshot,&ln));
  memcpy(low_descriptor,descriptor,dn);ldn=dn;
  assert(put_number(110,1,low_descriptor,sizeof low_descriptor,&ldn));
  h=calloc(1,sizeof *h);assert(h&&state_open_checked(h,&io,(wa_slice){low_descriptor,ldn},(wa_slice){low_snapshot,ln},NULL,&stage));
  wa_tree *archive=wa_history_delete_patch(h,&io,"archive","123456789@lid","last",0,1700000000000ULL,1700000003000ULL,1);
  assert(archive);wa_history_free(h);
  wa_node *ap=wa_node_child(wa_node_child(wa_node_child(archive->root,"sync"),"collection"),"patch");assert(ap);
  server_n=0;assert(put_bytes(1,delta_version,sizeof delta_version,server_patch,sizeof server_patch,&server_n));
  memcpy(server_patch+server_n,ap->data,ap->data_len);server_n+=ap->data_len;
  memcpy(write_task,low_descriptor,ldn);write_n=ldn;
  assert(put_bytes(111,server_patch,server_n,write_task,sizeof write_task,&write_n));
  big64(stored_version,2);assert(io.put(io.user,version_key(1),stored_version,8));
  h=calloc(1,sizeof *h);assert(h&&state_open_checked(h,&io,(wa_slice){write_task,write_n},(wa_slice){low_snapshot,ln},NULL,&stage));
  assert(replay_write_snapshot(h,&io,(wa_slice){write_task,write_n},NULL,&stage)&&h->state_version==2);
  wa_tree *unarchive=wa_history_delete_patch(h,&io,"unarchive","123456789@lid","last",0,1700000000000ULL,1700000004000ULL,0);
  assert(unarchive);wa_history_free(h);
  ap=wa_node_child(wa_node_child(wa_node_child(unarchive->root,"sync"),"collection"),"patch");assert(ap);
  ptn=0;assert(put_number(101,2,patch_task,sizeof patch_task,&ptn));
  assert(put_number(110,1,patch_task,sizeof patch_task,&ptn));assert(put_number(108,3,patch_task,sizeof patch_task,&ptn));
  assert(put_bytes(103,ap->data,ap->data_len,patch_task,sizeof patch_task,&ptn));
  h=wa_history_open_sync(&io,patch_task,ptn,NULL,0,NULL);
  assert(h&&wa_history_next(h,&e,&time)==1&&!strcmp(e.kind,"archive")&&e.state==0);
  wa_history_free(h);wa_tree_free(archive);wa_tree_free(unarchive);
  assert(io.put(io.user,version_key(1),NULL,0));
 }
 assert(wa_state_version(&io,0)==1&&io.put(io.user,STATE_VERSION_KEY,NULL,0)&&io.put(io.user,filename,NULL,0));
 /* A full delta queue must still be able to store a recovery snapshot. */
 for(unsigned slot=0;slot<WA_HISTORY_PATCH_TASKS;slot++){
  char name[32];key(slot,name);
  assert(io.put(io.user,name,inline_task,sizeof inline_task));
 }
 const uint8_t external_ref[]={10,1,7,18,5,'/','t','e','s','t'};
 wa_node snapshot_node={0};snapshot_node.data=external_ref;snapshot_node.data_len=sizeof external_ref;
 assert(state_task(&io,&snapshot_node,1,0,NULL,1));
 assert(wa_history_task(&io,WA_HISTORY_PATCH_TASKS+1,stored,sizeof stored,&n)==1);
 assert(wa_history_job_kind(stored,n)==1);
 assert(wa_history_done(&io,WA_HISTORY_PATCH_TASKS+1,stored,n));
 for(unsigned slot=0;slot<WA_HISTORY_PATCH_TASKS;slot++)
  assert(wa_history_done(&io,slot,inline_task,sizeof inline_task));
 /* Pages larger than the eight-slot queue resume from the committed cursor
  * instead of forcing a new snapshot and replaying the same oldest deltas. */
 wa_tree *page=wa_tree_create();assert(page);
 wa_node *page_root=wa_node_add(page,NULL,"iq"),*page_sync=wa_node_add(page,page_root,"sync");
 wa_node *page_collection=wa_node_add(page,page_sync,"collection");
 assert(page_collection&&wa_node_attr(page_collection,"name","regular_low"));
 uint8_t page_data[10][4];
 for(unsigned i=0;i<10;i++){
  page_data[i][0]=10;page_data[i][1]=2;page_data[i][2]=8;page_data[i][3]=(uint8_t)(i+1);
  wa_node *item=wa_node_add(page,page_collection,"patch");assert(item);
  item->data=page_data[i];item->data_len=4;
 }
 assert(wa_state_collect(&io,page_root,NULL,1)==2);
 for(unsigned slot=0;slot<WA_HISTORY_PATCH_TASKS;slot++){
  assert(wa_history_task(&io,slot,stored,sizeof stored,&n)==1);
  assert(wa_history_job_order(stored,n)==slot+1);
  assert(wa_history_done(&io,slot,stored,n));
 }
 big64(stored_version,8);assert(io.put(io.user,version_key(1),stored_version,8));
 assert(wa_state_collect(&io,page_root,NULL,1)==1);
 for(unsigned slot=0;slot<2;slot++){
  assert(wa_history_task(&io,slot,stored,sizeof stored,&n)==1);
  assert(wa_history_job_order(stored,n)==slot+9);
  assert(wa_history_done(&io,slot,stored,n));
 }
 assert(wa_history_task(&io,2,stored,sizeof stored,&n)==0);
 assert(io.put(io.user,version_key(1),NULL,0));wa_tree_free(page);
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
 const uint8_t archive_index[]="[\"archive\",\"123456789@lid\"]";
 for(int archived=0;archived<=1;archived++){
  an=0;on=0;
  assert(put_number(1,(uint64_t)archived,operation,sizeof operation,&on));
  assert(put_number(1,1700000001000ULL,action,sizeof action,&an));
  assert(put_bytes(17,operation,on,action,sizeof action,&an));
  h=calloc(1,sizeof *h);assert(h);h->state_job=2;h->low=1;
  assert(add_action(h,(wa_slice){archive_index,sizeof archive_index-1},(wa_slice){action,an}));
  assert(wa_history_next(h,&e,&time)==1&&!strcmp(e.kind,"archive")&&e.state==archived);
  wa_history_free(h);
 }
 uint8_t ref[80],mapped_ref[100];size_t refn=0,mappedn=0;
 assert(put_bytes(1,raw_key,sizeof raw_key,ref,sizeof ref,&refn));
 assert(put_bytes(2,(const uint8_t*)"/test",5,ref,sizeof ref,&refn));
 assert(blob_ref((wa_slice){ref,refn},mapped_ref,sizeof mapped_ref,&mappedn));
 check_path("/test",1);
 puts("PASS: independent history media fixture, hashes/HMAC, inline decompression, original dates/status, names, aliases and durable replay/acknowledgement");return 0;
}
