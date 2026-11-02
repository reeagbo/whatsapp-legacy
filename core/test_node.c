#include "node.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
int main(void) {
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
