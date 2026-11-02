#include <jni.h>
#include <string.h>
#include <stdio.h>
#include <openssl/crypto.h>
#include "noise.h"
#include "register.h"
#include "wire.h"

/* Local self-check with synthetic registration data, never sent over the network. */
JNIEXPORT jstring JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeDiagnostics(JNIEnv *env, jobject self) {
    (void)self;
    wa_noise n;
    int noise_ok = wa_noise_init(&n);
    uint8_t identity[32]={1},prekey[32]={2},signature[64]={3},out[4096];
    size_t len=0;
    wa_registration r={12345,27,identity,prekey,signature,"Android Legacy"};
    int payload_ok=wa_registration_payload(&r,out,sizeof(out),&len);
    wa_slice pair;
    int parse_ok=payload_ok && wa_pb_bytes_field(out,len,19,&pair);
    char report[512];
    snprintf(report,sizeof report,
        "Noise / claves efímeras: %s\nClientPayload Protobuf: %s (%zu bytes)\nDecodificación del registro: %s\n\nPruebas locales con datos sintéticos. No se ha contactado con WhatsApp ni vinculado ninguna cuenta.",
        noise_ok?"OK":"ERROR",payload_ok?"OK":"ERROR",len,parse_ok?"OK":"ERROR");
    OPENSSL_cleanse(&n,sizeof n);
    return (*env)->NewStringUTF(env,report);
}
