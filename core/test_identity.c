#define _POSIX_C_SOURCE 200809L
#include "identity.h"
#include "noise.h"
#include "pairing.h"
#include <assert.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
  wa_noise noise;
  assert(wa_noise_init(&noise));
  const uint8_t mode[32] = "Noise_XX_25519_AESGCM_SHA256\0\0\0\0";
  assert(!memcmp(
      noise.salt, mode,
      32)); /* Spec: 32-byte protocol name is used directly, not hashed. */
  wa_identity a, b;
  assert(wa_identity_generate(&a));
  assert(wa_identity_validate(&a));
  const uint8_t msg[] = {1, 2, 3, 4};
  uint8_t sig[64];
  assert(wa_sign(a.identity_private, msg, sizeof msg, sig));
  assert(wa_verify(a.identity_public, msg, sizeof msg, sig));
  sig[2] ^= 1;
  assert(!wa_verify(a.identity_public, msg, sizeof msg, sig));
  char dir[] = "/tmp/wa-identity-XXXXXX";
  assert(mkdtemp(dir));
  char file[256];
  snprintf(file, sizeof file, "%s/keys", dir);
  assert(wa_identity_save(file, &a));
  assert(wa_identity_open(file, &b));
  assert(!memcmp(a.identity_private, b.identity_private, 32));
  assert(!memcmp(a.noise_private, b.noise_private, 32));
  assert(a.registration_id == b.registration_id);
  struct stat st;
  assert(stat(file, &st) == 0 && (st.st_mode & 0777) == 0600);
  FILE *f = fopen(file, "r+b");
  assert(f);
  assert(fseek(f, 50, SEEK_SET) == 0);
  int ch = fgetc(f);
  assert(fseek(f, 50, SEEK_SET) == 0);
  assert(fputc(ch ^ 1, f) != EOF);
  assert(fclose(f) == 0);
  assert(!wa_identity_open(file, &b));
  assert(unlink(file) == 0);
  assert(symlink("/etc/passwd", file) == 0);
  assert(!wa_identity_open(file, &b));
  assert(unlink(file) == 0);
  assert(wa_identity_open(file, &b));
  assert(wa_identity_validate(&b));
  wa_pair_session *s = wa_pair_create(file, "34123456789");
  assert(s && wa_pair_state(s) == 1);
  assert(!*wa_pair_code(s));
  assert(wa_pair_start(s));
  assert(!wa_pair_start(s));
  size_t n = 0;
  const uint8_t *out = wa_pair_output(s, &n);
  assert(out && n > 7 && !memcmp(out, "WA\6\3", 4));
  wa_pair_output_done(s);
  uint8_t invalid[] = {0, 0, 1, 255};
  assert(!wa_pair_feed(s, invalid, sizeof invalid));
  assert(wa_pair_state(s) == -1 && !*wa_pair_code(s));
  wa_pair_free(s);
  uint8_t wrapped[80], pk[32];
  assert(wa_pairing_wrap("1234ABCD", a.identity_public, wrapped));
  assert(wa_pairing_unwrap("1234ABCD", wrapped, pk));
  assert(!memcmp(pk, a.identity_public, 32));
  assert(wa_pairing_unwrap("1234ABCE", wrapped, pk));
  assert(memcmp(pk, a.identity_public, 32));
  /* RFC5869 SHA256, test case 1, first 32 bytes. */
  uint8_t ikm[22], salt[13], key[32];
  memset(ikm, 11, 22);
  for (int j = 0; j < 13; j++)
    salt[j] = (uint8_t)j;
  const char info[] = "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9";
  const uint8_t expected[] = {0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
                              0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
                              0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
                              0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf};
  assert(wa_hkdf(ikm, 22, salt, 13, info, key) && !memcmp(key, expected, 32));
  unlink(file);
  rmdir(dir);
  OPENSSL_cleanse(&a, sizeof a);
  OPENSSL_cleanse(&b, sizeof b);
  puts("PASS: Noise specification, Signal signatures, private atomic storage, "
       "corrupt/symlink rejection, pairing wrap and RFC5869 HKDF");
  return 0;
}
