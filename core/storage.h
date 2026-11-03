#ifndef WA_STORAGE_H
#define WA_STORAGE_H
#include <stddef.h>
#include <stdint.h>
/* Platform-owned durable key/value storage. get: 1 found, 0 missing, -1 error.
 * put must atomically replace and durably save; NULL removes a key. */
typedef struct {
  void *user;
  int (*get)(void *, const char *, uint8_t *, size_t, size_t *);
  int (*put)(void *, const char *, const uint8_t *, size_t);
} wa_storage;
#endif
