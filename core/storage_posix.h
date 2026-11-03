#ifndef WA_STORAGE_POSIX_H
#define WA_STORAGE_POSIX_H
#include "storage.h"
/* Convenience adapter for Android/Linux; replace on other platforms. */
int wa_storage_posix(wa_storage *out, const char *directory);
void wa_storage_posix_close(wa_storage *storage);
#endif
