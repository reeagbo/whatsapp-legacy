#ifndef WA_NODE_H
#define WA_NODE_H
#include <stddef.h>
#include <stdint.h>
#define WA_NODE_LIMIT 256
#define WA_ATTR_LIMIT 16
#define WA_NODE_BYTES (1024u * 1024u)
typedef struct {
  const char *key, *value;
} wa_attr;
typedef struct wa_node {
  const char *tag;
  wa_attr attrs[WA_ATTR_LIMIT];
  size_t attr_count;
  const uint8_t *data;
  size_t data_len;
  int data_is_text; /* token/string content rather than opaque binary bytes */
  struct wa_node *child, *next;
} wa_node;
typedef struct {
  wa_node nodes[WA_NODE_LIMIT];
  size_t count;
  uint8_t *arena;
  size_t used;
  wa_node *root;
} wa_tree;
wa_tree *wa_tree_create(void);
void wa_tree_free(wa_tree *tree);
wa_node *wa_node_add(wa_tree *tree, wa_node *parent, const char *tag);
int wa_node_attr(wa_node *node, const char *key, const char *value);
const char *wa_node_get(const wa_node *node, const char *key);
wa_node *wa_node_child(const wa_node *node, const char *tag);
int wa_node_encode(const wa_node *root, uint8_t *out, size_t cap,
                   size_t *written);
wa_tree *wa_node_decode(const uint8_t *in, size_t len);
#endif
