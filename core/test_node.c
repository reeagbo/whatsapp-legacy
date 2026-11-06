#include "node.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "fixtures/pair_request.inc"
static void reference_encoding(void) {
  wa_tree *t=wa_tree_create();assert(t);
  wa_node *iq=wa_node_add(t,NULL,"iq");
  assert(wa_node_attr(iq,"to","@s.whatsapp.net")&&wa_node_attr(iq,"type","set")&&
         wa_node_attr(iq,"id","legacy-pair-1")&&wa_node_attr(iq,"xmlns","md"));
  wa_node *r=wa_node_add(t,iq,"link_code_companion_reg");assert(r);
  assert(wa_node_attr(r,"jid","34123456789@s.whatsapp.net")&&wa_node_attr(r,"stage","companion_hello")&&
         wa_node_attr(r,"should_show_push_notification","true"));
  uint8_t wrapped[80],key[32];memset(wrapped,0x11,sizeof wrapped);memset(key,0x22,sizeof key);
  wa_node *a=wa_node_add(t,r,"link_code_pairing_wrapped_companion_ephemeral_pub");assert(a);a->data=wrapped;a->data_len=sizeof wrapped;
  a=wa_node_add(t,r,"companion_server_auth_key_pub");assert(a);a->data=key;a->data_len=sizeof key;
  const char *tags[]={"companion_platform_id","companion_platform_display","link_code_pairing_nonce"};
  const char *values[]={"1","Chrome (Linux)","0"};
  for(size_t j=0;j<3;j++){a=wa_node_add(t,r,tags[j]);assert(a);a->data=(const uint8_t*)values[j];a->data_len=strlen(values[j]);a->data_is_text=1;}
  uint8_t out[2048];size_t n=0;
  assert(wa_node_encode(iq,out,sizeof out,&n)&&n==sizeof pair_request_fixture&&!memcmp(out,pair_request_fixture,n));
  wa_tree *decoded=wa_node_decode(pair_request_fixture,sizeof pair_request_fixture);assert(decoded);
  assert(!strcmp(wa_node_get(decoded->root,"to"),"@s.whatsapp.net"));
  assert(!strcmp(wa_node_get(wa_node_child(decoded->root,"link_code_companion_reg"),"jid"),"34123456789@s.whatsapp.net"));
  wa_tree_free(decoded);wa_tree_free(t);
  t=wa_tree_create();assert(t);iq=wa_node_add(t,NULL,"iq");
  assert(wa_node_attr(iq,"to","34123456789:2@s.whatsapp.net")&&wa_node_attr(iq,"type","get")&&wa_node_attr(iq,"id","A1B2"));
  assert(wa_node_encode(iq,out,sizeof out,&n)&&n==sizeof device_jid_fixture&&!memcmp(out,device_jid_fixture,n));
  decoded=wa_node_decode(device_jid_fixture,sizeof device_jid_fixture);assert(decoded);
  assert(!strcmp(wa_node_get(decoded->root,"to"),"34123456789:2@s.whatsapp.net"));
  wa_tree_free(decoded);wa_tree_free(t);
}
static void large_group_nodes(void) {
 wa_tree *t=wa_tree_create();assert(t&&!t->nodes[0]);
 wa_node *root=wa_node_add(t,NULL,"iq"),*groups=wa_node_add(t,root,"groups"),*g=wa_node_add(t,groups,"group");
 char keys[20][16];
 for(unsigned i=0;i<20;i++){snprintf(keys[i],sizeof keys[i],"field%u",i);assert(wa_node_attr(g,keys[i],"value"));}
 for(unsigned i=0;i<320;i++){wa_node *p=wa_node_add(t,g,"participant");assert(p&&wa_node_attr(p,"jid","12345678@lid"));}
 assert(t->count==323&&g->child&&g->child->next);
 uint8_t out[32768];size_t n=0;assert(wa_node_encode(root,out,sizeof out,&n));
 wa_tree *d=wa_node_decode(out,n);assert(d&&d->count==323);
 assert(wa_node_child(wa_node_child(d->root,"groups"),"group")->attr_count==20);
 wa_tree_free(d);wa_tree_free(t);
 puts("PASS: 320-member group reply and 20 attributes cross chunk boundaries without pointer invalidation");
}
int main(void) {
  large_group_nodes();
  reference_encoding();
  wa_tree *t = wa_tree_create();
  assert(t);
  wa_node *iq = wa_node_add(t, NULL, "iq"),
          *pair = wa_node_add(t, iq, "pair-device"),
          *ref = wa_node_add(t, pair, "ref");
  assert(iq && pair && ref && wa_node_attr(iq, "id", "test-1") &&
         wa_node_attr(iq, "type", "set"));
  const uint8_t data[] = {0, 1, 2, 0, 255};
  ref->data = data;
  ref->data_len = sizeof data;
  uint8_t out[4096];
  size_t n = 0;
  assert(wa_node_encode(iq, out, sizeof out, &n));
  wa_tree *d = wa_node_decode(out, n);
  assert(d && !strcmp(d->root->tag, "iq") &&
         !strcmp(wa_node_get(d->root, "id"), "test-1"));
  wa_node *decoded =
      wa_node_child(wa_node_child(d->root, "pair-device"), "ref");
  assert(decoded && decoded->data_len == sizeof data &&
         !memcmp(decoded->data, data, sizeof data));
  wa_tree_free(d);
  for (size_t j = 0; j < n; j++)
    assert(!wa_node_decode(out, j));
  uint8_t compressed[4096];
  compressed[0] = 2;
  uLongf cn = sizeof compressed - 1;
  assert(compress(compressed + 1, &cn, out + 1, (uLong)n - 1) == Z_OK);
  d = wa_node_decode(compressed, (size_t)cn + 1);
  assert(d);
  wa_tree_free(d);
  const uint8_t fixture[] = {0, 248, 3,   252, 2,   'i',  'q', 252,
                             2, 'i', 'd', 255, 130, 0x12, 0x3f};
  d = wa_node_decode(fixture, sizeof fixture);
  assert(d && !strcmp(wa_node_get(d->root, "id"), "123"));
  wa_tree_free(d);
  const uint8_t bad[] = {0,   248, 3,   252, 2,   'i', 'q',
                         252, 2,   'i', 'd', 255, 128};
  assert(!wa_node_decode(bad, sizeof bad));
  uint8_t small[1];
  assert(!wa_node_encode(iq, small, sizeof small, &n));
  unsigned seed = 7;
  for (unsigned j = 0; j < 2000; j++) {
    uint8_t fuzz[128];
    for (size_t k = 0; k < sizeof fuzz; k++) {
      seed = seed * 1664525u + 1013904223u;
      fuzz[k] = (uint8_t)(seed >> 24);
    }
    fuzz[0] = 0;
    d = wa_node_decode(fuzz, sizeof fuzz);
    wa_tree_free(d);
  }
  wa_tree_free(t);
  puts("PASS: binary nodes, dictionaries, compression, nibble fixture, "
       "truncated and malformed packets");
  return 0;
}
