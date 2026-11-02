#define _POSIX_C_SOURCE 200809L
#include "identity.h"
#include "noise.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <signal_protocol.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int random_func(uint8_t *out, size_t n, void *unused) {
  (void)unused;

  return n <= INT_MAX && RAND_bytes(out, (int)n) == 1 ? 0 : SG_ERR_UNKNOWN;
}
static int mac_init(void **out, const uint8_t *key, size_t n, void *unused) {
  (void)unused;
  if (n > INT_MAX)
    return SG_ERR_INVAL;
  HMAC_CTX *c = HMAC_CTX_new();
  if (!c || HMAC_Init_ex(c, key, (int)n, EVP_sha256(), NULL) != 1) {
    HMAC_CTX_free(c);
    return SG_ERR_UNKNOWN;
  }
  *out = c;
  return 0;
}
static int mac_update(void *c, const uint8_t *data, size_t n, void *unused) {
  (void)unused;
  return HMAC_Update(c, data, n) == 1 ? 0 : SG_ERR_UNKNOWN;
}
static int mac_final(void *c, signal_buffer **out, void *unused) {
  (void)unused;
  uint8_t digest[32];
  unsigned n = 0;
  if (HMAC_Final(c, digest, &n) != 1 || n != 32)
    return SG_ERR_UNKNOWN;
  *out = signal_buffer_create(digest, 32);
  OPENSSL_cleanse(digest, 32);
  return *out ? 0 : SG_ERR_NOMEM;
}
static void mac_free(void *c, void *unused) {
  (void)unused;
  HMAC_CTX_free(c);
}
int wa_keypair(uint8_t sk[32], uint8_t pk[32]) {
  if (RAND_bytes(sk, 32) != 1)
    return 0;

  sk[0] &= 248;
  sk[31] &= 127;
  sk[31] |= 64;

  EVP_PKEY *p = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, sk, 32);
  size_t n = 32;

  int ok = p && EVP_PKEY_get_raw_public_key(p, pk, &n) == 1 && n == 32;
  EVP_PKEY_free(p);

  return ok;
}
int wa_sign(const uint8_t sk[32], const uint8_t *data, size_t n,
            uint8_t sig[64]) {
  signal_context *ctx = NULL;
  ec_private_key *key = NULL;
  signal_buffer *s = NULL;

  signal_crypto_provider provider = {0};
  provider.random_func = random_func;
  provider.hmac_sha256_init_func = mac_init;
  provider.hmac_sha256_update_func = mac_update;
  provider.hmac_sha256_final_func = mac_final;
  provider.hmac_sha256_cleanup_func = mac_free;

  int ok = signal_context_create(&ctx, NULL) == 0 &&
           signal_context_set_crypto_provider(ctx, &provider) == 0 &&
           curve_decode_private_point(&key, sk, 32, ctx) == 0 &&
           curve_calculate_signature(ctx, &s, key, data, n) == 0 &&
           signal_buffer_len(s) == 64;

  if (ok)
    memcpy(sig, signal_buffer_data(s), 64);

  signal_buffer_bzero_free(s);

  if (key)
    SIGNAL_UNREF(key);

  if (ctx)
    signal_context_destroy(ctx);

  return ok;
}
int wa_verify(const uint8_t pk[32], const uint8_t *data, size_t n,
              const uint8_t sig[64]) {
  signal_context *ctx = NULL;
  ec_public_key *key = NULL;

  uint8_t typed[33] = {5};
  memcpy(typed + 1, pk, 32);

  int ok = signal_context_create(&ctx, NULL) == 0 &&
           curve_decode_point(&key, typed, 33, ctx) == 0 &&
           curve_verify_signature(key, data, n, sig, 64) == 1;

  if (key)
    SIGNAL_UNREF(key);

  if (ctx)
    signal_context_destroy(ctx);

  return ok;
}
static int matches(const uint8_t sk[32], const uint8_t pk[32]) {
  uint8_t actual[32];
  size_t n = 32;
  EVP_PKEY *p = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, sk, 32);

  int ok = p && EVP_PKEY_get_raw_public_key(p, actual, &n) == 1 && n == 32 &&
           !CRYPTO_memcmp(actual, pk, 32);
  EVP_PKEY_free(p);

  return ok;
}
int wa_identity_validate(const wa_identity *id) {
  uint8_t typed[33] = {5};
  memcpy(typed + 1, id->prekey_public, 32);

  return id->registration_id > 0 && id->registration_id < 16384 &&
         id->account_len <= sizeof id->account &&
         memchr(id->jid, 0, sizeof id->jid) &&
         matches(id->noise_private, id->noise_public) &&
         matches(id->identity_private, id->identity_public) &&
         matches(id->prekey_private, id->prekey_public) &&
         wa_verify(id->identity_public, typed, 33, id->prekey_signature);
}
int wa_identity_generate(wa_identity *id) {
  memset(id, 0, sizeof *id);

  uint8_t reg[2], typed[33] = {5};

  if (!wa_keypair(id->noise_private, id->noise_public) ||
      !wa_keypair(id->identity_private, id->identity_public) ||
      !wa_keypair(id->prekey_private, id->prekey_public) ||
      RAND_bytes(id->adv_secret, 32) != 1 || RAND_bytes(reg, 2) != 1)
    return 0;

  id->registration_id = (((unsigned)reg[0] << 8) | reg[1]) % 16383 + 1;
  memcpy(typed + 1, id->prekey_public, 32);

  return wa_sign(id->identity_private, typed, 33, id->prekey_signature) &&
         wa_identity_validate(id);
}
/* Explicit fixed-width file encoding, independent of ABI padding and
 * endianness. */
#define STORE_SIZE (8 + 192 + 64 + 32 + 4 + 128 + 2 + 2048 + 32)
static void encode(const wa_identity *id, uint8_t out[STORE_SIZE]) {
  memset(out, 0, STORE_SIZE);
  memcpy(out, "WAKEYS01", 8);
  size_t k = 8;

  const uint8_t *parts[] = {id->noise_private,    id->noise_public,
                            id->identity_private, id->identity_public,
                            id->prekey_private,   id->prekey_public,
                            id->prekey_signature, id->adv_secret};

  for (size_t j = 0; j < 8; j++) {
    size_t n = j == 6 ? 64 : 32;
    memcpy(out + k, parts[j], n);
    k += n;
  }
  for (size_t j = 0; j < 4; j++)
    out[k++] = (uint8_t)(id->registration_id >> (24 - j * 8));

  memcpy(out + k, id->jid, 128);
  k += 128;
  out[k++] = (uint8_t)(id->account_len >> 8);
  out[k++] = (uint8_t)id->account_len;

  memcpy(out + k, id->account, id->account_len);
  SHA256(out, STORE_SIZE - 32, out + STORE_SIZE - 32);
}
static int decode(wa_identity *id, const uint8_t in[STORE_SIZE]) {
  uint8_t digest[32];
  SHA256(in, STORE_SIZE - 32, digest);

  if (memcmp(in, "WAKEYS01", 8) ||
      CRYPTO_memcmp(digest, in + STORE_SIZE - 32, 32))
    return 0;

  memset(id, 0, sizeof *id);
  size_t k = 8;

  uint8_t *parts[] = {id->noise_private,    id->noise_public,
                      id->identity_private, id->identity_public,
                      id->prekey_private,   id->prekey_public,
                      id->prekey_signature, id->adv_secret};

  for (size_t j = 0; j < 8; j++) {
    size_t n = j == 6 ? 64 : 32;
    memcpy(parts[j], in + k, n);
    k += n;
  }
  for (int j = 0; j < 4; j++)
    id->registration_id = (id->registration_id << 8) | in[k++];

  memcpy(id->jid, in + k, 128);
  k += 128;
  id->account_len = ((size_t)in[k] << 8) | in[k + 1];
  k += 2;

  if (id->account_len > sizeof id->account)
    return 0;

  memcpy(id->account, in + k, id->account_len);

  return wa_identity_validate(id);
}
int wa_identity_save(const char *path, const wa_identity *id) {
  if (!path || !wa_identity_validate(id))
    return 0;

  char tmp[1024];
  int k = snprintf(tmp, sizeof tmp, "%s.XXXXXX", path);

  if (k < 0 || (size_t)k >= sizeof tmp)
    return 0;

  uint8_t out[STORE_SIZE];
  encode(id, out);
  int fd = mkstemp(tmp);
  int ok = fd >= 0;

  if (ok)
    ok = fchmod(fd, 0600) == 0;

  for (size_t p = 0; ok && p < sizeof out;) {
    ssize_t n = write(fd, out + p, sizeof out - p);

    if (n < 0 && errno == EINTR)
      continue;

    if (n <= 0)
      ok = 0;
    else
      p += (size_t)n;
  }
  if (ok)
    ok = fsync(fd) == 0;

  if (fd >= 0 && close(fd) != 0)
    ok = 0;

  if (ok)
    ok = rename(tmp, path) == 0;

  if (!ok)
    unlink(tmp);

  OPENSSL_cleanse(out, sizeof out);

  return ok;
}
int wa_identity_open(const char *path, wa_identity *id) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

  if (fd < 0)
    return errno == ENOENT && wa_identity_generate(id) &&
           wa_identity_save(path, id);

  struct stat st;
  int ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_uid == getuid() && (st.st_mode & 077) == 0 &&
           st.st_size == STORE_SIZE;

  uint8_t in[STORE_SIZE];
  for (size_t p = 0; ok && p < sizeof in;) {
    ssize_t n = read(fd, in + p, sizeof in - p);

    if (n < 0 && errno == EINTR)
      continue;

    if (n <= 0)
      ok = 0;
    else
      p += (size_t)n;
  }
  close(fd);

  if (ok)
    ok = decode(id, in);
  OPENSSL_cleanse(in, sizeof in);

  if (!ok)
    OPENSSL_cleanse(id, sizeof *id);

  return ok;
}
int wa_hkdf(const uint8_t *data, size_t n, const uint8_t *salt, size_t sn,
            const char *info, uint8_t out[32]) {
  uint8_t zero[32] = {0}, prk[32], msg[256];
  unsigned len = 0;
  size_t z = strlen(info);

  if (z > 254 || sn > INT_MAX)
    return 0;

  if (!salt) {
    salt = zero;
    sn = 32;
  }
  int ok = HMAC(EVP_sha256(), salt, (int)sn, data, n, prk, &len) && len == 32;

  memcpy(msg, info, z);
  msg[z] = 1;

  if (ok)
    ok = HMAC(EVP_sha256(), prk, 32, msg, z + 1, out, &len) && len == 32;

  OPENSSL_cleanse(prk, sizeof prk);

  return ok;
}
static int ctr(const uint8_t key[32], const uint8_t iv[16], const uint8_t *in,
               uint8_t *out) {
  EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
  int n = 0, m = 0;

  int ok = c && EVP_EncryptInit_ex(c, EVP_aes_256_ctr(), NULL, key, iv) == 1 &&
           EVP_EncryptUpdate(c, out, &n, in, 32) == 1 && n == 32 &&
           EVP_EncryptFinal_ex(c, out + n, &m) == 1 && m == 0;

  EVP_CIPHER_CTX_free(c);

  return ok;
}
static int pair_key(const char code[9], const uint8_t salt[32],
                    uint8_t key[32]) {
  return strlen(code) == 8 && PKCS5_PBKDF2_HMAC(code, 8, salt, 32, 131072,
                                                EVP_sha256(), 32, key) == 1;
}
int wa_pairing_wrap(const char code[9], const uint8_t pk[32], uint8_t out[80]) {
  uint8_t key[32];
  int ok = RAND_bytes(out, 48) == 1 && pair_key(code, out, key) &&
           ctr(key, out + 32, pk, out + 48);
  OPENSSL_cleanse(key, 32);

  return ok;
}
int wa_pairing_unwrap(const char code[9], const uint8_t in[80],
                      uint8_t pk[32]) {
  uint8_t key[32];
  int ok = pair_key(code, in, key) && ctr(key, in + 32, in + 48, pk);
  OPENSSL_cleanse(key, 32);

  return ok;
}
int wa_pairing_finish(const char code[9], const uint8_t eph_sk[32],
                      const wa_identity *id, const uint8_t wrapped[80],
                      const uint8_t primary_pk[32], uint8_t bundle[156],
                      uint8_t adv[32]) {
  uint8_t pk[32], shared[32], identity_shared[32], key[32], random[32],
      plain[96], secrets[96];
  int n = 0, m = 0;

  EVP_CIPHER_CTX *c = NULL;
  int ok = wa_pairing_unwrap(code, wrapped, pk) &&
           wa_noise_dh(eph_sk, pk, shared) &&
           wa_noise_dh(id->identity_private, primary_pk, identity_shared) &&
           RAND_bytes(random, 32) == 1 && RAND_bytes(bundle, 44) == 1 &&
           wa_hkdf(shared, 32, bundle, 32,
                   "link_code_pairing_key_bundle_encryption_key", key);

  if (ok) {
    memcpy(plain, id->identity_public, 32);
    memcpy(plain + 32, primary_pk, 32);
    memcpy(plain + 64, random, 32);
    c = EVP_CIPHER_CTX_new();

    ok =
        c &&
        EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), NULL, key, bundle + 32) == 1 &&
        EVP_EncryptUpdate(c, bundle + 44, &n, plain, 96) == 1 && n == 96 &&
        EVP_EncryptFinal_ex(c, bundle + 44 + n, &m) == 1 && m == 0 &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, bundle + 140) == 1;
  }
  if (ok) {
    memcpy(secrets, shared, 32);
    memcpy(secrets + 32, identity_shared, 32);
    memcpy(secrets + 64, random, 32);
    ok = wa_hkdf(secrets, 96, NULL, 0, "adv_secret", adv);
  }
  EVP_CIPHER_CTX_free(c);
  OPENSSL_cleanse(shared, 32);
  OPENSSL_cleanse(identity_shared, 32);
  OPENSSL_cleanse(key, 32);
  OPENSSL_cleanse(plain, 96);
  OPENSSL_cleanse(secrets, 96);
  OPENSSL_cleanse(random, 32);

  return ok;
}
