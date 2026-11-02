#include "node.h"
#include "tokens.inc"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
wa_tree *wa_tree_create(void) {
  wa_tree *t = calloc(1, sizeof *t);

  if (t) {
    t->arena = malloc(WA_NODE_BYTES);

    if (!t->arena) {
      free(t);
      t = NULL;
    }
  }
  return t;
}
void wa_tree_free(wa_tree *t) {
  if (t) {
    free(t->arena);
    free(t);
  }
}
wa_node *wa_node_add(wa_tree *t, wa_node *p, const char *tag) {
  if (!t || t->count == WA_NODE_LIMIT || !tag || !*tag)
    return NULL;

  wa_node *n = &t->nodes[t->count++];
  n->tag = tag;

  if (p) {
    wa_node **s = &p->child;
    while (*s)
      s = &(*s)->next;
    *s = n;
  } else if (!t->root)
    t->root = n;
  else
    return NULL;

  return n;
}
int wa_node_attr(wa_node *n, const char *k, const char *v) {
  if (!n || !k || !v || n->attr_count == WA_ATTR_LIMIT)
    return 0;
  for (size_t j = 0; j < n->attr_count; j++)
    if (!strcmp(n->attrs[j].key, k))
      return 0;
  n->attrs[n->attr_count++] = (wa_attr){k, v};

  return 1;
}
const char *wa_node_get(const wa_node *n, const char *k) {
  if (n)
    for (size_t j = 0; j < n->attr_count; j++)
      if (!strcmp(n->attrs[j].key, k))
        return n->attrs[j].value;

  return NULL;
}
wa_node *wa_node_child(const wa_node *n, const char *tag) {
  if (n)
    for (wa_node *c = n->child; c; c = c->next)
      if (!strcmp(c->tag, tag))
        return c;

  return NULL;
}
static void *copy(wa_tree *t, const uint8_t *b, size_t n) {
  if (n >= WA_NODE_BYTES - t->used)
    return NULL;

  uint8_t *p = t->arena + t->used;
  memcpy(p, b, n);
  p[n] = 0;
  t->used += n + 1;

  return p;
}
typedef struct {
  const uint8_t *b;
  size_t n, p;
  wa_tree *t;
  int ok;
} reader;

static unsigned byte(reader *r) {
  if (r->p >= r->n) {
    r->ok = 0;

    return 0;
  }
  return r->b[r->p++];
}
static size_t integer(reader *r, unsigned n) {
  size_t v = 0;
  for (unsigned j = 0; j < n; j++)
    v = (v << 8) | byte(r);

  return v;
}
static size_t list(reader *r, unsigned tag) {
  if (tag == 0)
    return 0;

  if (tag == 248)
    return byte(r);

  if (tag == 249)
    return integer(r, 2);

  r->ok = 0;

  return 0;
}
static const char *string(reader *r, unsigned tag, unsigned depth) {
  if (!r->ok || depth > 8) {
    r->ok = 0;

    return NULL;
  }
  if (tag > 0 && tag < sizeof single_tokens / sizeof single_tokens[0])
    return single_tokens[tag];

  if (tag >= 236 && tag <= 239)
    return double_tokens[tag - 236][byte(r)];

  if (tag == 0)
    return "";

  if (tag >= 252 && tag <= 254) {
    size_t n = tag == 252   ? byte(r)
               : tag == 253 ? integer(r, 3) & 0xfffff
                            : integer(r, 4);

    if (!r->ok || n > r->n - r->p || n > 65536) {
      r->ok = 0;

      return NULL;
    }
    const char *p = copy(r->t, r->b + r->p, n);
    r->p += n;

    if (!p || memchr(p, 0, n))
      r->ok = 0;

    return p;
  }
  if (tag == 251 || tag == 255) {
    unsigned count = byte(r);
    size_t n = (count & 127) * 2;
    char out[255];

    if ((count & 128) && !n) {
      r->ok = 0;

      return NULL;
    }
    for (size_t j = 0; j < n; j += 2) {
      unsigned v = byte(r), hi = v >> 4, lo = v & 15;

      const char *table = tag == 251 ? "0123456789ABCDEF" : "0123456789-.???\0";

      if (tag == 255 &&
          (hi > 11 || (lo > 11 && !(j + 2 == n && (count & 128) && lo == 15))))
        r->ok = 0;

      out[j] = table[hi];
      out[j + 1] = table[lo];
    }
    if (count & 128)
      n--;

    return copy(r->t, (uint8_t *)out, n);
  }
  if (tag == 250) {
    const char *u = string(r, byte(r), depth + 1),
               *server = string(r, byte(r), depth + 1);
    char out[2048];

    if (!r->ok || !u || !server || !*server) {
      r->ok = 0;

      return NULL;
    }
    int n = snprintf(out, sizeof out, "%s@%s", u, server);

    if (n < 0 || (size_t)n >= sizeof out) {
      r->ok = 0;

      return NULL;
    }
    return copy(r->t, (uint8_t *)out, (size_t)n);
  }
  if (tag == 247 || tag == 246) {
    unsigned domain = 0, device;
    const char *u, *server;

    if (tag == 247) {
      domain = byte(r);
      device = byte(r);
      u = string(r, byte(r), depth + 1);
      server = domain == 1     ? "lid"
               : domain == 128 ? "hosted"
               : domain == 129 ? "hosted.lid"
                               : "s.whatsapp.net";
    } else {
      u = string(r, byte(r), depth + 1);
      device = (unsigned)integer(r, 2);
      server = string(r, byte(r), depth + 1);
    }
    char out[2048];

    if (!r->ok || !u || !server) {
      r->ok = 0;

      return NULL;
    }
    int n = snprintf(out, sizeof out, "%s:%u@%s", u, device, server);

    if (n < 0 || (size_t)n >= sizeof out) {
      r->ok = 0;

      return NULL;
    }
    return copy(r->t, (uint8_t *)out, (size_t)n);
  }
  r->ok = 0;

  return NULL;
}
static wa_node *node(reader *r, wa_node *parent, unsigned depth) {
  if (depth > 16) {
    r->ok = 0;

    return NULL;
  }
  size_t count = list(r, byte(r));
  const char *tag = string(r, byte(r), 0);

  if (!r->ok || !count || count > 2 * WA_ATTR_LIMIT + 2) {
    r->ok = 0;

    return NULL;
  }
  wa_node *n = wa_node_add(r->t, parent, tag);

  if (!n) {
    r->ok = 0;

    return NULL;
  }
  for (size_t j = 0; j < (count - 1) / 2; j++) {
    const char *k = string(r, byte(r), 0), *v = string(r, byte(r), 0);

    if (!r->ok || !wa_node_attr(n, k, v)) {
      r->ok = 0;

      return NULL;
    }
  }
  if (!(count & 1)) {
    unsigned token = byte(r);

    if (token == 0 || token == 248 || token == 249) {
      size_t num = list(r, token);

      if (num > WA_NODE_LIMIT) {
        r->ok = 0;

        return NULL;
      }
      for (size_t j = 0; r->ok && j < num; j++)
        node(r, n, depth + 1);
    } else if (token >= 252 && token <= 254) {
      size_t len = token == 252   ? byte(r)
                   : token == 253 ? integer(r, 3) & 0xfffff
                                  : integer(r, 4);

      if (!r->ok || len > r->n - r->p) {
        r->ok = 0;

        return NULL;
      }
      n->data = copy(r->t, r->b + r->p, len);
      n->data_len = len;
      r->p += len;

      if (!n->data)
        r->ok = 0;

    } else {
      const char *v = string(r, token, 0);

      if (!r->ok || !v) {
        r->ok = 0;

        return NULL;
      }
      n->data = (const uint8_t *)v;
      n->data_len = strlen(v);
    }
  }
  return n;
}
wa_tree *wa_node_decode(const uint8_t *in, size_t len) {
  if (!in || len < 2 || len > WA_NODE_BYTES || (in[0] != 0 && in[0] != 2))
    return NULL;

  uint8_t *plain = NULL;
  size_t n = len - 1;
  const uint8_t *data = in + 1;

  if (in[0] == 2) {
    plain = malloc(WA_NODE_BYTES);
    uLongf sz = WA_NODE_BYTES;

    if (!plain || uncompress(plain, &sz, in + 1, (uLong)len - 1) != Z_OK) {
      free(plain);

      return NULL;
    }
    data = plain;
    n = (size_t)sz;
  }
  wa_tree *t = wa_tree_create();

  if (!t) {
    free(plain);

    return NULL;
  }
  reader r = {data, n, 0, t, 1};
  node(&r, NULL, 0);
  free(plain);

  if (!r.ok || r.p != r.n) {
    wa_tree_free(t);

    return NULL;
  }
  return t;
}
typedef struct {
  uint8_t *b;
  size_t cap, n;
  int ok;
} writer;

static void put(writer *w, unsigned v) {
  if (w->n == w->cap) {
    w->ok = 0;
    return;
  }
  w->b[w->n++] = (uint8_t)v;
}
static void blob(writer *w, const uint8_t *v, size_t n) {
  if (!v || n > 0xfffff || n > w->cap - w->n) {
    w->ok = 0;
    return;
  }
  if (n < 256) {
    put(w, 252);
    put(w, (unsigned)n);
  } else {
    put(w, 253);
    put(w, (unsigned)(n >> 16));
    put(w, (unsigned)(n >> 8));
    put(w, (unsigned)n);
  }
  if (n > w->cap - w->n) {
    w->ok = 0;
    return;
  }
  memcpy(w->b + w->n, v, n);
  w->n += n;
}
static void text(writer *w, const char *v) {
  if (!v) {
    w->ok = 0;
    return;
  }
  for (unsigned j = 1; j < sizeof single_tokens / sizeof single_tokens[0]; j++)
    if (!strcmp(v, single_tokens[j])) {
      put(w, j);
      return;
    }
  blob(w, (const uint8_t *)v, strlen(v));
}
static void start(writer *w, size_t n) {
  if (!n)
    put(w, 0);
  else if (n < 256) {
    put(w, 248);
    put(w, (unsigned)n);
  } else {
    put(w, 249);
    put(w, (unsigned)(n >> 8));
    put(w, (unsigned)n);
  }
}
static void encode_node(writer *w, const wa_node *n, unsigned depth) {
  if (!n || depth > 16 || n->attr_count > WA_ATTR_LIMIT ||
      (n->data && n->child)) {
    w->ok = 0;
    return;
  }
  start(w, n->attr_count * 2 + 1 + ((n->data || n->child) ? 1 : 0));
  text(w, n->tag);

  for (size_t j = 0; j < n->attr_count; j++) {
    text(w, n->attrs[j].key);
    text(w, n->attrs[j].value);
  }
  if (n->data)
    blob(w, n->data, n->data_len);

  else if (n->child) {
    size_t count = 0;
    for (const wa_node *c = n->child; c; c = c->next) {
      if (++count > WA_NODE_LIMIT) {
        w->ok = 0;
        return;
      }
    }
    start(w, count);
    for (const wa_node *c = n->child; w->ok && c; c = c->next)
      encode_node(w, c, depth + 1);
  }
}
int wa_node_encode(const wa_node *root, uint8_t *out, size_t cap,
                   size_t *written) {
  if (!out || !written || !root)
    return 0;
  writer w = {out, cap, 0, 1};
  put(&w, 0);
  encode_node(&w, root, 0);

  if (w.ok)
    *written = w.n;

  return w.ok;
}
