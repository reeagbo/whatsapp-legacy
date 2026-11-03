#define _POSIX_C_SOURCE 200809L
/* White-box protocol state tests. No test hooks are exported by the library. */
#include "pairing.c"
#include <assert.h>
#include <unistd.h>
static int deliver(wa_pair_session *s, wa_noise *server, wa_tree *t) {
  uint8_t plain[16384], ct[16400], framed[16403];
  size_t n = 0, z = 0, k = 0;
  assert(wa_node_encode(t->root, plain, sizeof plain, &n));
  assert(wa_noise_encrypt(server, plain, n, ct, &z));
  assert(wa_noise_frame(ct, z, 0, framed, sizeof framed, &k));
  int ok = 1;
  for (size_t p = 0; ok && p < k;) {
    size_t part = k - p > 7 ? 7 : k - p;
    ok = wa_pair_feed(s, framed + p, part);
    p += part;
  }
  return ok;
}
static wa_tree *drain(wa_pair_session *s, wa_noise *server) {
  wa_tree *last = NULL;
  size_t n = 0;
  while (wa_pair_output(s, &n)) {
    const uint8_t *data = wa_pair_output(s, &n), *frame = NULL;
    size_t len = 0, used = 0;
    assert(wa_noise_unframe(data, n, &frame, &len, &used) && used == n);
    uint8_t *plain = malloc(len);
    size_t pn = 0;
    assert(plain && wa_noise_decrypt(server, frame, len, plain, &pn));
    wa_tree_free(last);
    last = wa_node_decode(plain, pn);
    assert(last);
    free(plain);
    wa_pair_output_done(s);
  }
  return last;
}
int main(void) {
  char dir[] = "/tmp/wa-pairing-XXXXXX", path[256];
  assert(mkdtemp(dir));
  snprintf(path, sizeof path, "%s/keys", dir);
  wa_pair_session *s = wa_pair_create(path, "34123456789");
  assert(s && s->state == 1);
  assert(wa_noise_transport(&s->noise));
  s->hello_sent = 1;
  wa_noise server = s->noise;
  memcpy(server.read_key, s->noise.write_key, 32);
  memcpy(server.write_key, s->noise.read_key, 32);
  wa_tree *t = wa_tree_create();
  wa_node *root = wa_node_add(t, NULL, "iq");
  assert(wa_node_attr(root, "id", "test-pair"));
  assert(wa_node_attr(root, "type", "set"));
  assert(wa_node_add(t, root, "pair-device"));
  assert(deliver(s, &server, t) && s->state == 3 && strlen(wa_pair_code(s)) == 8);
  wa_tree_free(t);
  t = drain(s, &server);
  wa_node *r = wa_node_child(t->root, "link_code_companion_reg");
  assert(r);
  wa_node *wrapped =
      wa_node_child(r, "link_code_pairing_wrapped_companion_ephemeral_pub");
  uint8_t pub[32];
  assert(wrapped && wrapped->data_len == 80 &&
         wa_pairing_unwrap(s->code, wrapped->data, pub) &&
         !memcmp(pub, s->ephemeral_public, 32));
  wa_tree_free(t);
  /* Regression: do not inject an IQ acknowledgement. A real primary must
   * be able to use the code and respond while that optional result is absent. */
  uint8_t ep[32], epub[32], ip[32], ipub[32], wr[80];
  assert(wa_keypair(ep, epub) && wa_keypair(ip, ipub) &&
         wa_pairing_wrap(s->code, epub, wr));
  t = wa_tree_create();
  root = wa_node_add(t, NULL, "notification");
  assert(wa_node_attr(root, "from", "s.whatsapp.net") &&
         wa_node_attr(root, "id", "note1") &&
         wa_node_attr(root, "type", "link_code_companion_reg"));
  r = wa_node_add(t, root, "link_code_companion_reg");
  assert(data_node(t, r, "link_code_pairing_ref", (const uint8_t *)"test-ref",
                   8) &&
         data_node(t, r, "primary_identity_pub", ipub, 32) &&
         data_node(t, r, "link_code_pairing_wrapped_primary_ephemeral_pub", wr,
                   80));
  assert(deliver(s, &server, t) && s->state == 2);
  wa_tree_free(t);
  /* Optional, late IQ results must not reveal the consumed code or regress state. */
  t=iq("result","legacy-pair-1",NULL);
  assert(deliver(s,&server,t)&&s->state==2&&!*wa_pair_code(s));
  wa_tree_free(t);
  /* Decrypt the outgoing bundle from the primary's side, independently. */
  size_t n = 0;
  const uint8_t *frame = wa_pair_output(s, &n), *ct;
  size_t cn = 0, used = 0;
  assert(frame && wa_noise_unframe(frame, n, &ct, &cn, &used));
  uint8_t plain[8192];
  size_t pn = 0;
  assert(wa_noise_decrypt(&server, ct, cn, plain, &pn));
  t = wa_node_decode(plain, pn);
  assert(t);
  wa_node *bundle =
      wa_node_child(wa_node_child(t->root, "link_code_companion_reg"),
                    "link_code_pairing_wrapped_key_bundle");
  assert(bundle && bundle->data_len == 156);
  uint8_t shared[32], key[32], decrypted[96], sec[96], adv[32];
  assert(wa_noise_dh(ep, s->ephemeral_public, shared) &&
         wa_hkdf(shared, 32, bundle->data, 32,
                 "link_code_pairing_key_bundle_encryption_key", key));
  EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
  int a = 0, b = 0;
  assert(c &&
         EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), NULL, key,
                            bundle->data + 32) == 1 &&
         EVP_DecryptUpdate(c, decrypted, &a, bundle->data + 44, 96) == 1 &&
         a == 96 &&
         EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16,
                             (void *)(bundle->data + 140)) == 1 &&
         EVP_DecryptFinal_ex(c, decrypted + a, &b) == 1 && b == 0);
  EVP_CIPHER_CTX_free(c);
  assert(!memcmp(decrypted, s->identity.identity_public, 32) &&
         !memcmp(decrypted + 32, ipub, 32));
  memcpy(sec, shared, 32);
  assert(wa_noise_dh(ip, s->identity.identity_public, sec + 32));
  memcpy(sec + 64, decrypted + 64, 32);
  assert(wa_hkdf(sec, 96, NULL, 0, "adv_secret", adv) &&
         !memcmp(adv, s->identity.adv_secret, 32));
  wa_tree_free(t);
  wa_pair_output_done(s);
  wa_tree_free(drain(s, &server));
  /* Account identity envelope and account signature, signed by the primary. */
  uint8_t dd[16];
  size_t dn = 0;
  assert(append_number(3, 1, dd, sizeof dd, &dn));
  uint8_t ask[32], apk[32], sig[64], message[1090] = {6, 0};
  assert(wa_keypair(ask, apk));
  memcpy(message + 2, dd, dn);
  memcpy(message + 2 + dn, s->identity.identity_public, 32);
  assert(wa_sign(ask, message, dn + 34, sig));
  uint8_t account[512], envelope[1024], mac[32];
  size_t an = 0, en = 0;
  unsigned mn = 0;
  assert(append_bytes(1, dd, dn, account, sizeof account, &an) &&
         append_bytes(2, apk, 32, account, sizeof account, &an) &&
         append_bytes(3, sig, 64, account, sizeof account, &an));
  assert(HMAC(EVP_sha256(), adv, 32, account, an, mac, &mn) && mn == 32);
  assert(append_bytes(1, account, an, envelope, sizeof envelope, &en) &&
         append_bytes(2, mac, 32, envelope, sizeof envelope, &en));
  t = iq("set", "pair-final", NULL);
  r = wa_node_add(t, t->root, "pair-success");
  wa_node *di = data_node(t, r, "device-identity", envelope, en),
          *dev = wa_node_add(t, r, "device");
  assert(di && dev && wa_node_attr(dev, "jid", "34123456789:1@s.whatsapp.net"));
  wa_pair_session *bad = wa_pair_create(path, "34123456789");
  assert(bad);
  envelope[en - 1] ^= 1;
  assert(!pair_success(bad, t->root) && bad->state == -1 && !bad->proof &&
         !bad->identity.jid[0]);
  envelope[en - 1] ^= 1;
  wa_pair_free(bad);
  assert(deliver(s, &server, t) && s->proof && s->state == 2);
  wa_tree_free(t);
  t = drain(s, &server);
  wa_node *signed_node = wa_node_child(wa_node_child(t->root, "pair-device-sign"), "device-identity");
  wa_slice signed_details, device_signature;
  assert(signed_node && wa_pb_bytes_field(signed_node->data, signed_node->data_len, 1, &signed_details));
  assert(wa_pb_bytes_field(signed_node->data, signed_node->data_len, 4, &device_signature) && device_signature.len == 64);
  assert(!strcmp(wa_node_get(signed_node, "key-index"), "1"));
  message[1] = 1;
  memcpy(message + 2, signed_details.ptr, signed_details.len);
  memcpy(message + 2 + signed_details.len, s->identity.identity_public, 32);
  memcpy(message + 34 + signed_details.len, apk, 32);
  assert(wa_verify(s->identity.identity_public, message, signed_details.len + 66, device_signature.ptr));
  wa_tree_free(t);
  wa_identity stored;
  assert(wa_identity_open(path, &stored) &&
         !strcmp(stored.jid, "34123456789:1@s.whatsapp.net") &&
         stored.account_len > 0);
  /* A restart preserves credentials; retrying a rejected pairing preserves
   * long-term keys and does not destroy disk credentials on cancel. */
  wa_pair_session *login = wa_pair_create(path, "");
  assert(login && !strcmp(login->identity.jid, stored.jid));
  uint8_t login_pb[4096]; size_t login_len = 0; uint64_t user = 0, device_id = 0;
  assert(login_payload(login, login_pb, sizeof login_pb, &login_len));
  assert(pb_number(login_pb, login_len, 1, &user) && user == UINT64_C(34123456789));
  assert(pb_number(login_pb, login_len, 18, &device_id) && device_id == 1);
  wa_pair_free(login);
  wa_pair_session *retry = wa_pair_create(path, "34123456789");
  assert(retry && !*retry->identity.jid && !retry->identity.account_len);
  assert(!memcmp(retry->identity.noise_private, stored.noise_private, 32));
  assert(wa_identity_open(path, &stored) && *stored.jid);
  wa_pair_free(retry);
  t = wa_tree_create();
  root = wa_node_add(t, NULL, "stream:error");
  assert(wa_node_attr(root, "code", "515"));
  assert(deliver(s, &server, t) && s->state == 4 && !*wa_pair_code(s));
  wa_tree_free(t);
  const char *log = wa_pair_debug_log(s);
  assert(strstr(log,"PAIR_CODE_PREPARED") && strstr(log,"PAIRING_RESTART_REQUIRED"));
  assert(!strstr(log,"34123456789") && !strstr(log,s->code) && !strstr(log,path));
  wa_pair_debug_clear(s);
  assert(!*wa_pair_debug_log(s));
  for(unsigned j=0;j<1000;j++)trace(s,"TEST_BOUND %0600u",j);
  assert(strlen(wa_pair_debug_log(s))<32768);
  assert(strstr(wa_pair_debug_log(s),"truncated"));
  wa_pair_debug_clear(s);
  t=wa_tree_create(); root=wa_node_add(t,NULL,"failure");
  assert(wa_node_attr(root,"reason","34123456789"));
  assert(!handle_node(s,root));
  assert(strstr(wa_pair_debug_log(s),"redacted")&&!strstr(wa_pair_debug_log(s),"34123456789"));
  wa_tree_free(t);
  wa_pair_free(s);
  unlink(path);
  rmdir(dir);
  puts("PASS: fragmented encrypted pairing flow, code visibility without IQ acknowledgement, "
       "primary-side bundle decryption, rejected forged account HMAC, signed "
       "identity persistence, restart requirement and rejected-account retry");
  return 0;
}
