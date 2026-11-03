#include "pairing.h"
#include "handshake.h"
#include "identity.h"
#include "node.h"
#include "noise.h"
#include "register.h"
#include "wire.h"
#include "messaging.h"
#include "storage_posix.h"
#include <limits.h>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_FRAME (1024u * 1024u)
struct wa_pair_session {
  wa_identity identity;
  wa_noise noise;

  char path[1024], phone[16], code[9], status[512];

  uint8_t ephemeral_private[32], ephemeral_public[32];

  int state, proof, code_ready, hello_sent, finish_sent;

  uint8_t *input;
  size_t input_len;

  struct {
    uint8_t *data;
    size_t len;
  } output[128];
  size_t output_count;

  wa_messenger *messenger;
  wa_storage message_storage;

  unsigned counter;
  char debug_log[32768];
  size_t debug_used;
  unsigned debug_sequence;
};

/* Deliberately logs protocol metadata only: never payloads, IDs or key bytes. */
static void trace(wa_pair_session *s, const char *format, ...) {
  if (!s || s->debug_used >= sizeof s->debug_log - 64) return;
  char message[768];
  va_list args; va_start(args, format);
  vsnprintf(message, sizeof message, format, args); va_end(args);
  size_t room = sizeof s->debug_log - s->debug_used;
  int n = snprintf(s->debug_log + s->debug_used, room,
                   "C%04u state=%d %s\n", ++s->debug_sequence, s->state, message);
  if (n < 0) return;
  if ((size_t)n >= room - 64) {
    const char end[] = "[native trace truncated; export the current log]\n";
    size_t at = sizeof s->debug_log - sizeof end;
    memcpy(s->debug_log + at, end, sizeof end);
    s->debug_used = sizeof s->debug_log - 1;
  } else s->debug_used += (size_t)n;
}
const char *wa_pair_debug_log(const wa_pair_session *s) { return s ? s->debug_log : ""; }
void wa_pair_debug_clear(wa_pair_session *s) {
  if (s) { OPENSSL_cleanse(s->debug_log, sizeof s->debug_log); s->debug_used = 0; }
}
static const char *safe_tag(const char *tag) {
  static const char *const names[] = {"iq", "pair-device", "pair-success", "device-identity",
    "device", "platform", "biz", "ref", "pair-device-sign", "link_code_companion_reg",
    "link_code_pairing_ref", "link_code_pairing_wrapped_key_bundle", "companion_identity_public",
    "notification", "stream:error", "failure", "success", "error", "ack", "ib", "ping",
    "link_code_pairing_wrapped_companion_ephemeral_pub", "companion_server_auth_key_pub",
    "companion_platform_id", "companion_platform_display", "link_code_pairing_nonce",
    "primary_identity_pub", "link_code_pairing_wrapped_primary_ephemeral_pub", "edge_routing",
    "routing_info", "xmlstreamend", "message", "receipt", "enc", "participants", "to", "usync", "list", "count", "registration", "key", "active"};
  if (tag) for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(tag, names[i])) return names[i];
  return "<other>";
}
static const char *safe_type(const char *type) {
  if (!type) return "-";
  const char *const types[] = {"get", "set", "result", "error", "link_code_companion_reg", "text", "pkmsg", "msg", "read", "delivery", "retry"};
  for (size_t i=0;i<sizeof types/sizeof types[0];i++) if(!strcmp(type,types[i]))return types[i];
  return "<other>";
}
static const char *safe_code(const char *code) {
  return code && strlen(code) > 0 && strlen(code) <= 4 &&
    strspn(code,"0123456789") == strlen(code) ? code : "<absent-or-redacted>";
}
static void trace_node(wa_pair_session *s, const char *direction, const wa_node *n) {
  if (!n) return;
  const char *id = wa_node_get(n,"id");
  const char *kind = !id ? "none" : !strcmp(id,"legacy-pair-1") ? "pair-request" :
    !strcmp(id,"legacy-finish-1") ? "pair-finish" : !strncmp(id,"legacy-ping-",12) ? "ping" : "other";
  trace(s,"%s CORRELATION id_kind=%s",direction,kind);
  const char *code = wa_node_get(n,"code"); if(!code)code=wa_node_get(n,"reason"); if(!code)code=wa_node_get(n,"error");
  trace(s,"%s NODE tag=%s type=%s code=%s attrs=%zu data_bytes=%zu",direction,
        safe_tag(n->tag),safe_type(wa_node_get(n,"type")),safe_code(code),n->attr_count,n->data_len);
  unsigned count=0;
  for(const wa_node *child=n->child;child&&count<16;child=child->next,count++)
    trace(s,"%s CHILD tag=%s code=%s data_bytes=%zu",direction,safe_tag(child->tag),
          safe_code(wa_node_get(child,"code")),child->data_len);
}

static int fail(wa_pair_session *s, const char *message) {
  trace(s, "FAIL %s", message);
  s->state = -1;
  snprintf(s->status, sizeof s->status, "%s", message);

  return 0;
}
static int pb_number(const uint8_t *in, size_t len, unsigned field,
                     uint64_t *out) {
  size_t p = 0;
  while (p < len) {
    uint64_t tag, v;

    if (!wa_pb_varint(in, len, &p, &tag) || !(tag >> 3))
      return 0;

    if ((tag & 7) == 0) {
      if (!wa_pb_varint(in, len, &p, &v))
        return 0;

      if ((tag >> 3) == field) {
        *out = v;

        return 1;
      }
    } else if ((tag & 7) == 2) {
      if (!wa_pb_varint(in, len, &p, &v) || v > len - p)
        return 0;
      p += (size_t)v;
    } else if ((tag & 7) == 1) {
      if (len - p < 8)
        return 0;
      p += 8;
    } else if ((tag & 7) == 5) {
      if (len - p < 4)
        return 0;
      p += 4;
    } else
      return 0;
  }
  return 0;
}
static int append_bytes(unsigned field, const uint8_t *in, size_t n,
                        uint8_t *out, size_t cap, size_t *used) {
  size_t z = 0;

  if (*used > cap ||
      !wa_pb_write_bytes(field, in, n, out + *used, cap - *used, &z))
    return 0;
  *used += z;

  return 1;
}
static int append_number(unsigned field, uint64_t value, uint8_t *out,
                         size_t cap, size_t *used) {
  uint64_t tag = (uint64_t)field << 3;
  do {
    if (*used >= cap)
      return 0;

    uint8_t v = (uint8_t)(tag & 127);
    tag >>= 7;
    out[(*used)++] = v | (tag ? 128 : 0);
  } while (tag);

  do {
    if (*used >= cap)
      return 0;

    uint8_t v = (uint8_t)(value & 127);
    value >>= 7;
    out[(*used)++] = v | (value ? 128 : 0);
  } while (value);

  return 1;
}
wa_pair_session *wa_pair_create(const char *path, const char *phone) {
  if (!path || !phone || strlen(path) >= 1024 || strlen(phone) > 15)
    return NULL;

  if (*phone && (strlen(phone) < 8 || phone[0] == '0' ||
                 strspn(phone, "0123456789") != strlen(phone)))
    return NULL;

  wa_pair_session *s = calloc(1, sizeof *s);

  if (!s)
    return NULL;

  s->state = 1;
  trace(s,"SESSION_CREATE mode=%s", *phone ? "pair-code" : "check-session");
  strcpy(s->path, path);
  strcpy(s->phone, phone);
  s->input = malloc(MAX_FRAME + 3);

  if (!s->input || !wa_identity_open(path, &s->identity) ||
      !wa_noise_init(&s->noise)) {
    fail(s, "No se pudieron crear o leer las claves privadas. No se han "
            "reemplazado claves existentes.");

    return s;
  }
  /* Explicit code requests retry a rejected pairing, preserving long-term
   * keys. Do not overwrite the existing disk identity on mere creation. */
  if (*phone && s->identity.jid[0]) {
    OPENSSL_cleanse(s->identity.jid, sizeof s->identity.jid);
    OPENSSL_cleanse(s->identity.account, sizeof s->identity.account);
    s->identity.account_len = 0;
    trace(s,"NEW_PAIRING_REQUEST previous_account_cleared_in_memory=yes keys_preserved=yes");
  }
  trace(s,"IDENTITY_READY stored_account=%s; NOISE_INITIALIZED",s->identity.jid[0]?"yes":"no");
  snprintf(s->status, sizeof s->status,
           "Claves Signal comprobadas y guardadas. Conectando por TLS...");

  return s;
}
void wa_pair_free(wa_pair_session *s) {
  if (!s)
    return;
  wa_messenger_free(s->messenger);
  wa_storage_posix_close(&s->message_storage);
  free(s->input);
  for (size_t j = 0; j < s->output_count; j++)
    free(s->output[j].data);
  OPENSSL_cleanse(s, sizeof *s);
  free(s);
}
static int queue_raw(wa_pair_session *s, const uint8_t *data, size_t n,
                     int intro) {
  size_t queued=0;
  for(size_t j=0;j<s->output_count;j++)queued+=s->output[j].len;
  if (s->output_count >= sizeof s->output/sizeof *s->output || n > MAX_FRAME || queued > 2*MAX_FRAME-n-7)
    return fail(s, "Limite de salida excedido.");

  uint8_t *out = malloc(n + 7);
  size_t written = 0;

  if (!out)
    return fail(s, "Memoria insuficiente.");

  if (!wa_noise_frame(data, n, intro, out, n + 7, &written)) {
    free(out);

    return fail(s, "Error de formato WA.");
  }
  s->output[s->output_count].data = out;
  s->output[s->output_count++].len = written;
  trace(s,"TX_FRAME_QUEUED bytes=%zu intro=%d pending=%zu",written,intro,s->output_count);

  return 1;
}
int wa_pair_start(wa_pair_session *s) {
  if (!s || s->state != 1 || s->hello_sent)
    return 0;

  uint8_t hello[80];
  size_t len = 0;

  if (!wa_make_client_hello(s->noise.ephemeral_public, hello, sizeof hello,
                            &len) ||
      !queue_raw(s, hello, len, 1))
    return fail(s, "No se pudo construir ClientHello.");

  s->hello_sent = 1;
  trace(s,"CLIENT_HELLO_READY");
  snprintf(s->status, sizeof s->status,
           "TLS conectado. Comprobando el certificado Noise del servidor...");

  return 1;
}
const uint8_t *wa_pair_output(wa_pair_session *s, size_t *n) {
  *n = 0;

  if (!s || !s->output_count)
    return NULL;
  *n = s->output[0].len;

  return s->output[0].data;
}
void wa_pair_output_done(wa_pair_session *s) {
  if (!s || !s->output_count)
    return;
  free(s->output[0].data);
  trace(s,"TX_FRAME_DEQUEUED bytes=%zu",s->output[0].len);
  s->output_count--;
  memmove(s->output, s->output + 1, s->output_count * sizeof s->output[0]);
}
int wa_pair_state(const wa_pair_session *s) { return s ? s->state : -1; }
const char *wa_pair_status(const wa_pair_session *s) {
  return s ? s->status : "Sesion no disponible.";
}
const char *wa_pair_code(const wa_pair_session *s) {
  return s && s->state == 3 && s->code_ready ? s->code : "";
}
static int send_node(wa_pair_session *s, wa_tree *t) {
  if (!t)
    return fail(s, "Memoria insuficiente.");

  trace_node(s,"TX",t->root);
  uint8_t *out = malloc(MAX_FRAME), *ct = malloc(MAX_FRAME + 16);
  size_t n = 0, z = 0;

  int ok = out && ct && wa_node_encode(t->root, out, MAX_FRAME, &n) &&
           wa_noise_encrypt(&s->noise, out, n, ct, &z) &&
           queue_raw(s, ct, z, 0);

  free(out);
  free(ct);

  if (!ok)
    return fail(s, "No se pudo cifrar la respuesta.");

  return 1;
}
static int messenger_send(void *user, wa_tree *tree) { return send_node(user, tree); }
int wa_pair_send_text(wa_pair_session *s,const char *jid,const char *id,const uint8_t *text,size_t n) {
  if(!s||s->state!=5||!s->messenger)return 0;
  int ok=wa_messenger_send(s->messenger,jid,id,text,n);
  trace(s,"MESSAGE_SEND_REQUEST accepted=%d text_bytes=%zu",ok,n);
  return ok;
}
int wa_pair_stored(wa_pair_session *s,const char *id,const char *jid,int outgoing) {
  int ok=s&&wa_messenger_stored(s->messenger,id,jid,outgoing);
  if(s)trace(s,"INBOX_DATABASE_COMMIT confirmed=%d",ok);
  return ok;
}
int wa_pair_resolve(wa_pair_session *s,const char *jid,char out[128]) {
  return s&&wa_messenger_resolve(s->messenger,jid,out);
}
void wa_pair_tick(wa_pair_session *s,uint64_t now) { if(s&&s->messenger)wa_messenger_tick(s->messenger,now); }
const wa_message_event *wa_pair_event(const wa_pair_session *s) { return s?wa_messenger_event(s->messenger):NULL; }
void wa_pair_event_done(wa_pair_session *s) {
  if(s) { const wa_message_event *e=wa_messenger_event(s->messenger);
    if(e)trace(s,"MESSAGING_EVENT kind=%s state=%d",e->kind,e->state);
    wa_messenger_event_done(s->messenger);
  }
}

static wa_tree *iq(const char *type, const char *id, const char *xmlns) {
  wa_tree *t = wa_tree_create();

  if (!t)
    return NULL;
  wa_node *n = wa_node_add(t, NULL, "iq");

  if (!wa_node_attr(n, "to", "@s.whatsapp.net") ||
      !wa_node_attr(n, "type", type) || !wa_node_attr(n, "id", id) ||
      (xmlns && !wa_node_attr(n, "xmlns", xmlns))) {
    wa_tree_free(t);

    return NULL;
  }
  return t;
}
static int answer(wa_pair_session *s, const wa_node *n) {
  const char *id = wa_node_get(n, "id");

  if (!id)
    return fail(s, "Respuesta sin identificador.");
  wa_tree *t = iq("result", id, NULL);
  int ok = send_node(s, t);
  wa_tree_free(t);

  return ok;
}
static wa_node *data_node(wa_tree *t, wa_node *p, const char *tag,
                          const uint8_t *data, size_t len) {
  wa_node *n = wa_node_add(t, p, tag);

  if (n) {
    n->data = data;
    n->data_len = len;
  }
  return n;
}
static wa_node *text_node(wa_tree *t, wa_node *parent, const char *tag, const char *value) {
  wa_node *n=data_node(t,parent,tag,(const uint8_t*)value,strlen(value));
  if(n)n->data_is_text=1;
  return n;
}
static int request_code(wa_pair_session *s) {
  trace(s,"PAIR_CODE_REQUEST_BEGIN");
  const char *alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

  uint8_t random[5], wrapped[80];

  if (RAND_bytes(random, 5) != 1 ||
      !wa_keypair(s->ephemeral_private, s->ephemeral_public))
    return fail(s, "No se pudo generar la clave de vinculacion.");

  uint64_t bits = 0;
  for (int j = 0; j < 5; j++)
    bits = (bits << 8) | random[j];
  for (int j = 0; j < 8; j++)
    s->code[j] = alphabet[(bits >> (35 - j * 5)) & 31];
  s->code[8] = 0;

  if (!wa_pairing_wrap(s->code, s->ephemeral_public, wrapped))
    return fail(s, "Error al proteger la clave de vinculacion.");

  char jid[64];
  snprintf(jid, sizeof jid, "%s@s.whatsapp.net", s->phone);

  wa_tree *t = iq("set", "legacy-pair-1", "md");

  if (!t)
    return fail(s, "Memoria insuficiente.");
  wa_node *reg = wa_node_add(t, t->root, "link_code_companion_reg");

  int ok =
      wa_node_attr(reg, "jid", jid) &&
      wa_node_attr(reg, "stage", "companion_hello") &&
      wa_node_attr(reg, "should_show_push_notification", "true") &&
      data_node(t, reg, "link_code_pairing_wrapped_companion_ephemeral_pub",
                wrapped, 80) &&
      data_node(t, reg, "companion_server_auth_key_pub",
                s->identity.noise_public, 32) &&
      text_node(t, reg, "companion_platform_id", "1") &&
      text_node(t, reg, "companion_platform_display", "Chrome (Linux)") &&
      text_node(t, reg, "link_code_pairing_nonce", "0") &&
      send_node(s, t);

  wa_tree_free(t);

  if (!ok)
    return fail(s, "No se pudo solicitar el codigo.");

  /* The code is generated locally. The protocol sends companion_hello and
   * returns the code without awaiting an IQ result (Baileys requestPairingCode).
   * Kotlin exposes it only after WebSocket.send has accepted the queued frame. */
  s->code_ready = 1;
  s->state = 3;
  trace(s,"PAIR_CODE_PREPARED waiting_for_primary_device; iq_result_not_required=yes");
  snprintf(s->status, sizeof s->status,
           "Codigo preparado. Introducelo en WhatsApp principal > Dispositivos vinculados > Vincular con numero. Aun no se confirma vinculacion.");

  return 1;
}
static int handle_link(wa_pair_session *s, const wa_node *n) {
  trace(s,"PRIMARY_LINK_NOTIFICATION_RECEIVED");
  wa_node *r = wa_node_child(n, "link_code_companion_reg"),
          *ref = wa_node_child(r, "link_code_pairing_ref"),
          *pk = wa_node_child(r, "primary_identity_pub"),
          *wrapped = wa_node_child(
              r, "link_code_pairing_wrapped_primary_ephemeral_pub");

  if (s->state != 3 || !s->code_ready || !ref || !ref->data || !ref->data_len ||
      ref->data_len > 4096 || !pk || pk->data_len != 32 || !wrapped ||
      wrapped->data_len != 80)
    return fail(s, "Respuesta de vinculacion incompleta o inesperada.");

  uint8_t bundle[156], adv[32];

  if (!wa_pairing_finish(s->code, s->ephemeral_private, &s->identity,
                         wrapped->data, pk->data, bundle, adv))
    return fail(s, "Error en el acuerdo de claves de vinculacion.");

  char jid[64];
  snprintf(jid, sizeof jid, "%s@s.whatsapp.net", s->phone);
  wa_tree *t = iq("set", "legacy-finish-1", "md");

  if (!t)
    return fail(s, "Memoria insuficiente.");

  wa_node *reg = wa_node_add(t, t->root, "link_code_companion_reg");
  int ok = wa_node_attr(reg, "jid", jid) &&
           wa_node_attr(reg, "stage", "companion_finish") &&
           data_node(t, reg, "link_code_pairing_wrapped_key_bundle", bundle,
                     sizeof bundle) &&
           data_node(t, reg, "companion_identity_public",
                     s->identity.identity_public, 32) &&
           data_node(t, reg, "link_code_pairing_ref", ref->data, ref->data_len);

  if (ok) {
    memcpy(s->identity.adv_secret, adv, 32);
    ok = wa_identity_save(s->path, &s->identity) && send_node(s, t);
  }
  OPENSSL_cleanse(adv, 32);
  OPENSSL_cleanse(s->ephemeral_private, 32);
  wa_tree_free(t);

  if (!ok)
    return fail(s, "No se pudo guardar o enviar la prueba de vinculacion.");

  s->state = 2;
  trace(s,"PAIR_KEY_BUNDLE_QUEUED adv_secret_saved=yes");
  snprintf(s->status, sizeof s->status,
           "Codigo recibido por el movil principal. Verificando la identidad "
           "de la cuenta...");

  return 1;
}
static int notification_ack(wa_pair_session *s, const wa_node *n) {
  const char *from = wa_node_get(n, "from"), *id = wa_node_get(n, "id"),
             *type = wa_node_get(n, "type");

  if (!from || !id)
    return fail(s, "Notificacion sin remitente o identificador.");

  wa_tree *t = wa_tree_create();

  if (!t)
    return fail(s, "Memoria insuficiente.");
  wa_node *a = wa_node_add(t, NULL, "ack");

  int ok = wa_node_attr(a, "to", from) && wa_node_attr(a, "id", id) &&
           wa_node_attr(a, "class", "notification") &&
           (!type || wa_node_attr(a, "type", type)) && send_node(s, t);
  wa_tree_free(t);

  return ok;
}
static int pair_success(wa_pair_session *s, const wa_node *n) {
  trace(s,"PAIR_SUCCESS_VERIFY_BEGIN");
  wa_node *pair = wa_node_child(n, "pair-success"),
          *di = wa_node_child(pair, "device-identity"),
          *device = wa_node_child(pair, "device");
  const char *jid = wa_node_get(device, "jid"), *id = wa_node_get(n, "id");

  if (!di || !device || !di->data || !id || !jid ||
      strlen(jid) >= sizeof s->identity.jid || !strchr(jid, '@') ||
      strcmp(strchr(jid, '@'), "@s.whatsapp.net"))
    return fail(s, "Identidad del dispositivo incompleta.");

  wa_slice details, mac, dd, account_pk, account_sig;
  uint64_t kind = 0, device_type = 0, index;

  if (!wa_pb_bytes_field(di->data, di->data_len, 1, &details) ||
      details.len > 1800 ||
      !wa_pb_bytes_field(di->data, di->data_len, 2, &mac) || mac.len != 32)
    return fail(s, "Formato de firma de la cuenta incorrecto.");

  if (pb_number(di->data, di->data_len, 3, &kind) && kind != 0)
    return fail(s, "Las cuentas hosted no estan implementadas.");

  uint8_t expected[32];
  unsigned sz = 0;

  if (!HMAC(EVP_sha256(), s->identity.adv_secret, 32, details.ptr, details.len,
            expected, &sz) ||
      sz != 32 || CRYPTO_memcmp(expected, mac.ptr, 32))
    return fail(s, "Firma HMAC de la cuenta no valida.");

  if (!wa_pb_bytes_field(details.ptr, details.len, 1, &dd) || dd.len > 1024 ||
      !wa_pb_bytes_field(details.ptr, details.len, 2, &account_pk) ||
      account_pk.len != 32 ||
      !wa_pb_bytes_field(details.ptr, details.len, 3, &account_sig) ||
      account_sig.len != 64 || !pb_number(dd.ptr, dd.len, 3, &index) ||
      index > UINT_MAX)
    return fail(s, "Certificado de vinculacion incompleto.");

  if (pb_number(dd.ptr, dd.len, 5, &device_type) && device_type != 0)
    return fail(s, "Tipo de dispositivo no compatible.");

  trace(s,"ACCOUNT_HMAC_VERIFIED");
  uint8_t message[1090] = {6, 0}, sig[64];
  memcpy(message + 2, dd.ptr, dd.len);
  memcpy(message + 2 + dd.len, s->identity.identity_public, 32);

  if (!wa_verify(account_pk.ptr, message, dd.len + 34, account_sig.ptr))
    return fail(s, "Firma de identidad del movil principal no valida.");

  trace(s,"ACCOUNT_SIGNATURE_VERIFIED");
  message[1] = 1;
  memcpy(message + 34 + dd.len, account_pk.ptr, 32);

  if (!wa_sign(s->identity.identity_private, message, dd.len + 66, sig))
    return fail(s, "No se pudo firmar el nuevo dispositivo.");

  uint8_t encoded[2048];
  size_t used = 0;

  if (!append_bytes(1, dd.ptr, dd.len, encoded, sizeof encoded, &used) ||
      !append_bytes(3, account_sig.ptr, 64, encoded, sizeof encoded, &used) ||
      !append_bytes(4, sig, 64, encoded, sizeof encoded, &used))
    return fail(s, "Certificado demasiado grande.");

  size_t full = 0;

  if (!append_bytes(1, dd.ptr, dd.len, s->identity.account,
                    sizeof s->identity.account, &full) ||
      !append_bytes(2, account_pk.ptr, 32, s->identity.account,
                    sizeof s->identity.account, &full) ||
      !append_bytes(3, account_sig.ptr, 64, s->identity.account,
                    sizeof s->identity.account, &full) ||
      !append_bytes(4, sig, 64, s->identity.account, sizeof s->identity.account,
                    &full))
    return fail(s, "Certificado demasiado grande.");

  strcpy(s->identity.jid, jid);
  s->identity.account_len = full;

  if (!wa_identity_save(s->path, &s->identity))
    return fail(s, "No se pudo guardar la identidad verificada.");

  wa_tree *t = iq("result", id, NULL);

  if (!t)
    return fail(s, "Memoria insuficiente.");
  wa_node *ps = wa_node_add(t, t->root, "pair-device-sign"),
          *data = data_node(t, ps, "device-identity", encoded, used);
  char keyindex[32];
  snprintf(keyindex, sizeof keyindex, "%u", (unsigned)index);

  int ok = wa_node_attr(data, "key-index", keyindex) && send_node(s, t);
  wa_tree_free(t);

  if (!ok)
    return fail(s, "No se pudo enviar la firma del dispositivo.");

  s->proof = 1;
  trace(s,"DEVICE_SIGNATURE_QUEUED verified_identity_saved=yes");
  snprintf(s->status, sizeof s->status,
           "Identidad verificada y guardada. Esperando la confirmacion final "
           "del servidor...");

  return 1;
}
static int handle_node(wa_pair_session *s, const wa_node *n) {
  trace_node(s,"RX",n);
  const char *type = wa_node_get(n, "type"), *id = wa_node_get(n, "id");

  if (!strcmp(n->tag, "iq") && wa_node_child(n, "pair-device")) {
    if (s->state != 1 || s->identity.jid[0])
      return fail(s, "Solicitud de vinculacion inesperada.");

    if (!answer(s, n))
      return 0;

    if (!s->phone[0]) {
      s->state = 6;
      trace(s,"ANONYMOUS_CHANNEL_ACCEPTED");
      snprintf(s->status, sizeof s->status,
               "Canal TLS/Noise y registro inicial aceptados por el servidor. "
               "Sin cuenta vinculada.");

      return 1;
    }
    return request_code(s);
  }
  if (!strcmp(n->tag, "iq") && wa_node_child(n, "pair-success"))
    return pair_success(s, n);

  if (!strcmp(n->tag, "iq") && id && !strcmp(id, "legacy-pair-1")) {
    if (!type || strcmp(type, "result"))
      return fail(s, "WhatsApp ha rechazado la solicitud de codigo.");

    /* A result is optional: never regress a later pairing phase if it arrives late. */
    trace(s,"PAIR_CODE_IQ_RESULT_RECEIVED optional_ack=yes");

    return 1;
  }
  if (s->state==5 && s->messenger && !strcmp(n->tag,"iq")) {
    int ok=wa_messenger_node(s->messenger,n);
    trace(s,"MESSAGING_IQ_HANDLED ok=%d",ok);
    /* An application-level error consumes the authenticated transport frame. */
    return ok<0?fail(s,"Recepcion detenida para conservar mensajes. Revisa el almacenamiento y reconecta."):1;
  }
  if (!strcmp(n->tag, "iq") && type && !strcmp(type, "error")) {
    const wa_node *e = wa_node_child(n, "error");
    snprintf(s->status, sizeof s->status,
             "Solicitud rechazada por WhatsApp (codigo %s).",
             wa_node_get(e, "code") ? wa_node_get(e, "code") : "desconocido");

    s->state = -1;

    return 0;
  }
  if (!strcmp(n->tag, "notification")) {
    if (wa_node_child(n, "link_code_companion_reg") && !handle_link(s, n))
      return 0;

    return notification_ack(s, n);
  }
  if (!strcmp(n->tag, "stream:error") || !strcmp(n->tag, "failure")) {
    const char *code = wa_node_get(n, "code");

    if (!code)
      code = wa_node_get(n, "reason");

    if (code && !strcmp(code, "515") && s->proof) {
      s->state = 4;
      trace(s,"PAIRING_RESTART_REQUIRED server_restart=515");
      snprintf(s->status, sizeof s->status,
               "Identidad verificada y guardada. Es necesario reconectar "
               "para completar la vinculacion; aun no se confirma la sesion.");

      return 1;
    }
    s->state = -1;
    snprintf(
        s->status, sizeof s->status,
        "Servidor cerro la conexion (codigo %s). No se confirma vinculacion.",
        code ? code : "desconocido");

    return 0;
  }
  if (!strcmp(n->tag, "success")) {
    if (!s->identity.jid[0])
      return fail(s, "Inicio de sesion sin identidad guardada.");

    s->state = 5;
    trace(s,"STORED_SESSION_AUTHENTICATED");
    char directory[1100];
    snprintf(directory,sizeof directory,"%s.signal",s->path);
    if(!wa_storage_posix(&s->message_storage,directory) ||
       !(s->messenger=wa_messenger_create(&s->identity,&s->message_storage,
          wa_node_get(n,"lid"),messenger_send,s)) || !wa_messenger_start(s->messenger))
      return fail(s,"Sesion autenticada, pero no se pudo preparar el almacenamiento Signal.");
    snprintf(s->status, sizeof s->status,"Sesion autenticada. Preparando mensajeria cifrada...");
    return 1;
  }
  if(s->state==5 && s->messenger) {
    int ok=wa_messenger_node(s->messenger,n);
    trace(s,"MESSAGING_NODE_HANDLED ok=%d",ok);
    return ok<0?fail(s,"Recepcion detenida para conservar mensajes. Revisa el almacenamiento y reconecta."):1;
  }
  return 1;
}
static int login_payload(wa_pair_session *s, uint8_t *out, size_t cap,
                         size_t *written) {
  uint8_t reg[4096];
  size_t len = 0;
  wa_registration r = {s->identity.registration_id,  1,
                       s->identity.identity_public,  s->identity.prekey_public,
                       s->identity.prekey_signature, "Linux"};

  if (!wa_registration_payload(&r, reg, sizeof reg, &len))
    return 0;

  wa_slice ua, web;

  if (!wa_pb_bytes_field(reg, len, 5, &ua) ||
      !wa_pb_bytes_field(reg, len, 6, &web))
    return 0;

  const char *colon = strchr(s->identity.jid, ':'),
             *at = strchr(s->identity.jid, '@');

  if (!colon || !at || colon >= at)
    return 0;

  char *end = NULL;
  unsigned long long user = strtoull(s->identity.jid, &end, 10);

  if (end != colon)
    return 0;
  unsigned long device = strtoul(colon + 1, &end, 10);

  if (end != at || device > UINT_MAX)
    return 0;

  *written = 0;

  return append_number(1, (uint64_t)user, out, cap, written) &&
         append_number(3, 1, out, cap, written) &&
         append_bytes(5, ua.ptr, ua.len, out, cap, written) &&
         append_bytes(6, web.ptr, web.len, out, cap, written) &&
         append_number(12, 1, out, cap, written) &&
         append_number(13, 1, out, cap, written) &&
         append_number(18, device, out, cap, written) &&
         append_number(33, 1, out, cap, written) &&
         append_number(41, 0, out, cap, written);
}
static int frame(wa_pair_session *s, const uint8_t *in, size_t n) {
  if (!s->noise.transport) {
    wa_server_hello hello;

    uint8_t enc[48], payload[4096], finish[5000];
    size_t len = 0, z = 0;

    trace(s,"SERVER_HELLO_FRAME bytes=%zu",n);
    if (!wa_parse_server_hello(in, n, &hello)) return fail(s,"ServerHello Protobuf no valido.");
    trace(s,"SERVER_HELLO_PARSED ephemeral_bytes=%zu static_bytes=%zu certificate_bytes=%zu",
          hello.ephemeral_len,hello.static_len,hello.payload_len);
    const char *failed_stage = NULL;
    if (!wa_process_server_hello_ex(&s->noise, &hello, s->identity.noise_private,
                                   s->identity.noise_public, enc, &failed_stage)) {
      trace(s,"NOISE_HANDSHAKE_FAILED stage=%s",failed_stage?failed_stage:"unknown");
      return fail(s,"Certificado o negociacion Noise no validos. No se ha enviado el registro.");
    }
    trace(s,"NOISE_CERTIFICATE_VERIFIED");

    wa_registration r = {
        s->identity.registration_id,  1,
        s->identity.identity_public,  s->identity.prekey_public,
        s->identity.prekey_signature, "Linux"};

    int ok = s->identity.jid[0]
                 ? login_payload(s, payload, sizeof payload, &len)
                 : wa_registration_payload(&r, payload, sizeof payload, &len);

    trace(s,"CLIENT_PAYLOAD_BUILT mode=%s bytes=%zu ok=%d",s->identity.jid[0]?"login":"registration",len,ok);
    if (!ok ||
        !wa_finish_noise(&s->noise, enc, payload, len, finish, sizeof finish,
                         &z) ||
        !queue_raw(s, finish, z, 0))
      return fail(s, "No se pudo completar ClientFinish.");

    s->finish_sent = 1;
    trace(s,"CLIENT_FINISH_QUEUED TRANSPORT_KEYS_ACTIVE");
    snprintf(s->status, sizeof s->status,
             "Canal Noise autenticado. Registro inicial enviado; esperando "
             "respuesta...");

    return 1;
  }
  uint8_t *plain = malloc(n);
  size_t len = 0;

  if (!plain)
    return fail(s, "Memoria insuficiente.");

  if (!wa_noise_decrypt(&s->noise, in, n, plain, &len)) {
    free(plain);

    return fail(s, "Respuesta cifrada no valida.");
  }
  trace(s,"TRANSPORT_DECRYPT_OK plaintext_bytes=%zu",len);
  wa_tree *t = wa_node_decode(plain, len);
  free(plain);

  if (!t)
    return fail(s, "Respuesta binaria no compatible o malformada.");
  int ok = handle_node(s, t->root);
  wa_tree_free(t);

  return ok;
}
int wa_pair_feed(wa_pair_session *s, const uint8_t *bytes, size_t n) {
  if (!s || s->state < 0 || !s->hello_sent || !bytes)
    return 0;

  trace(s,"RX_WEBSOCKET_BINARY bytes=%zu buffered=%zu",n,s->input_len);
  /* Only a complete, otherwise unframed short message's two public header
   * bytes are exposed. Never dump a ciphertext, protobuf or node payload. */
  if(s->noise.transport && !s->input_len && n>0 && n<=2)
    trace(s,"RX_SHORT_WS_HEADER bytes=%zu prefix=%02x%02x",n,bytes[0],n==2?bytes[1]:0);
  while (n) {
    size_t room = MAX_FRAME + 3 - s->input_len, z = n < room ? n : room;

    if (!z)
      return fail(s, "Respuesta demasiado grande.");
    memcpy(s->input + s->input_len, bytes, z);
    s->input_len += z;
    bytes += z;
    n -= z;

    while (s->input_len >= 3) {
      size_t len = ((size_t)s->input[0] << 16) | ((size_t)s->input[1] << 8) |
                   s->input[2];

      if (len > MAX_FRAME)
        return fail(s, "Frame demasiado grande.");

      if (s->input_len < len + 3)
        break;

      trace(s,"RX_WA_FRAME bytes=%zu transport=%d",len,s->noise.transport);
      if (!frame(s, s->input + 3, len))
        return 0;

      s->input_len -= len + 3;
      memmove(s->input, s->input + len + 3, s->input_len);
    }
  }
  return 1;
}
int wa_pair_ping(wa_pair_session *s) {
  if (!s || s->state < 0 || !s->noise.transport || (s->state == 4 || s->state == 6))
    return 0;

  char id[40];
  snprintf(id, sizeof id, "legacy-ping-%u", ++s->counter);

  wa_tree *t = iq("get", id, "w:p");

  if (!t)
    return fail(s, "Memoria insuficiente.");
  int ok = wa_node_add(t, t->root, "ping") && send_node(s, t);
  wa_tree_free(t);

  return ok;
}
