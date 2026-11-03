#include "noise.h"
#include "register.h"
#include "wire.h"
#include <jni.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>

/* Local self-check with synthetic registration data, never sent over the
 * network. */
JNIEXPORT jstring JNICALL
Java_org_reeagbo_whatsapplegacy_MainActivity_nativeDiagnostics(JNIEnv *env,
                                                               jobject self) {
  (void)self;
  wa_noise n;
  int noise_ok = wa_noise_init(&n);
  uint8_t identity[32] = {1}, prekey[32] = {2}, signature[64] = {3}, out[4096];
  size_t len = 0;
  wa_registration r = {12345,  27,        identity,
                       prekey, signature, "Android Legacy"};
  int payload_ok = wa_registration_payload(&r, out, sizeof(out), &len);
  wa_slice pair;
  int parse_ok = payload_ok && wa_pb_bytes_field(out, len, 19, &pair);
  char report[512];
  snprintf(report, sizeof report,
           "Noise / claves efímeras: %s\nClientPayload Protobuf: %s (%zu "
           "bytes)\nDecodificación del registro: %s\n\nPruebas locales con "
           "datos sintéticos. No se ha contactado con WhatsApp ni vinculado "
           "ninguna cuenta.",
           noise_ok ? "OK" : "ERROR", payload_ok ? "OK" : "ERROR", len,
           parse_ok ? "OK" : "ERROR");
  OPENSSL_cleanse(&n, sizeof n);
  return (*env)->NewStringUTF(env, report);
}

#include "pairing.h"
#include <stdint.h>
static wa_pair_session *session(jlong handle) {
  return (wa_pair_session *)(intptr_t)handle;
}
JNIEXPORT jlong JNICALL
Java_org_reeagbo_whatsapplegacy_MainActivity_nativeCreate(JNIEnv *env,
                                                          jobject self,
                                                          jstring path,
                                                          jstring phone) {
  (void)self;
  if (!path || !phone)
    return 0;
  const char *p = (*env)->GetStringUTFChars(env, path, NULL);
  if (!p)
    return 0;
  const char *number = (*env)->GetStringUTFChars(env, phone, NULL);
  if (!number) {
    (*env)->ReleaseStringUTFChars(env, path, p);
    return 0;
  }
  wa_pair_session *s = wa_pair_create(p, number);
  (*env)->ReleaseStringUTFChars(env, path, p);
  (*env)->ReleaseStringUTFChars(env, phone, number);
  return (jlong)(intptr_t)s;
}
JNIEXPORT void JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeFree(
    JNIEnv *env, jobject self, jlong handle) {
  (void)env;
  (void)self;
  wa_pair_free(session(handle));
}
JNIEXPORT void JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeStart(
    JNIEnv *env, jobject self, jlong handle) {
  (void)env;
  (void)self;
  wa_pair_start(session(handle));
}
JNIEXPORT void JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeFeed(
    JNIEnv *env, jobject self, jlong handle, jbyteArray bytes) {
  (void)self;
  if (!handle || !bytes)
    return;
  jsize n = (*env)->GetArrayLength(env, bytes);
  if (n > 2 * 1024 * 1024)
    return;
  jbyte *p = (*env)->GetByteArrayElements(env, bytes, NULL);
  if (!p)
    return;
  wa_pair_feed(session(handle), (uint8_t *)p, (size_t)n);
  (*env)->ReleaseByteArrayElements(env, bytes, p, JNI_ABORT);
}
JNIEXPORT jint JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeState(
    JNIEnv *env, jobject self, jlong handle) {
  (void)env;
  (void)self;
  return wa_pair_state(session(handle));
}
JNIEXPORT jstring JNICALL
Java_org_reeagbo_whatsapplegacy_MainActivity_nativeStatus(JNIEnv *env,
                                                          jobject self,
                                                          jlong handle) {
  (void)self;
  return (*env)->NewStringUTF(env, wa_pair_status(session(handle)));
}
JNIEXPORT jstring JNICALL
Java_org_reeagbo_whatsapplegacy_MainActivity_nativeCode(JNIEnv *env,
                                                        jobject self,
                                                        jlong handle) {
  (void)self;
  return (*env)->NewStringUTF(env, wa_pair_code(session(handle)));
}
JNIEXPORT void JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativePing(
    JNIEnv *env, jobject self, jlong handle) {
  (void)env;
  (void)self;
  wa_pair_ping(session(handle));
}
JNIEXPORT jbyteArray JNICALL
Java_org_reeagbo_whatsapplegacy_MainActivity_nativeOutput(JNIEnv *env,
                                                          jobject self,
                                                          jlong handle) {
  (void)self;
  size_t n = 0;
  const uint8_t *p = wa_pair_output(session(handle), &n);
  if (!p || n > INT32_MAX)
    return NULL;
  jbyteArray out = (*env)->NewByteArray(env, (jsize)n);
  if (out) {
    (*env)->SetByteArrayRegion(env, out, 0, (jsize)n, (const jbyte *)p);
    wa_pair_output_done(session(handle));
  }
  return out;
}

JNIEXPORT jstring JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeLog(
    JNIEnv *env, jobject self, jlong handle) {
  (void)self;
  jstring out=(*env)->NewStringUTF(env,wa_pair_debug_log(session(handle)));
  if(out)wa_pair_debug_clear(session(handle));
  return out;
}

static jstring utf8_string(JNIEnv *env,const char *p) {
  size_t n=strlen(p);if(n>INT32_MAX)return NULL;
  jbyteArray bytes=(*env)->NewByteArray(env,(jsize)n);if(!bytes)return NULL;
  (*env)->SetByteArrayRegion(env,bytes,0,(jsize)n,(const jbyte*)p);
  jclass cls=(*env)->FindClass(env,"java/lang/String");
  jmethodID init=cls?(*env)->GetMethodID(env,cls,"<init>","([BLjava/lang/String;)V"):NULL;
  jstring charset=(*env)->NewStringUTF(env,"UTF-8");
  jstring result=init&&charset?(*env)->NewObject(env,cls,init,bytes,charset):NULL;
  (*env)->DeleteLocalRef(env,bytes);if(charset)(*env)->DeleteLocalRef(env,charset);if(cls)(*env)->DeleteLocalRef(env,cls);return result;
}
JNIEXPORT jobjectArray JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeEvent(JNIEnv *env,jobject self,jlong h){
 (void)self;const wa_message_event *e=wa_pair_event(session(h));if(!e)return NULL;
 jclass cls=(*env)->FindClass(env,"java/lang/String");if(!cls)return NULL;
 jobjectArray out=(*env)->NewObjectArray(env,5,cls,NULL);(*env)->DeleteLocalRef(env,cls);if(!out)return NULL;
 char state[32];snprintf(state,sizeof state,"%d",e->state);const char *fields[]={e->kind,e->id,e->jid,e->text,state};
 for(int i=0;i<5;i++){jstring value=utf8_string(env,fields[i]);if(!value)return NULL;(*env)->SetObjectArrayElement(env,out,i,value);(*env)->DeleteLocalRef(env,value);}
 wa_pair_event_done(session(h));return out;
}
JNIEXPORT jboolean JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeSend(JNIEnv *env,jobject self,jlong h,jstring jid,jstring id,jbyteArray text){
 (void)self;if(!jid||!id||!text)return JNI_FALSE;jsize n=(*env)->GetArrayLength(env,text);if(n<1||n>WA_TEXT_MAX)return JNI_FALSE;
 const char *j=(*env)->GetStringUTFChars(env,jid,NULL);if(!j)return JNI_FALSE;
 const char *i=(*env)->GetStringUTFChars(env,id,NULL);if(!i){(*env)->ReleaseStringUTFChars(env,jid,j);return JNI_FALSE;}
 jbyte *p=(*env)->GetByteArrayElements(env,text,NULL);int ok=p&&wa_pair_send_text(session(h),j,i,(uint8_t*)p,(size_t)n);
 if(p)(*env)->ReleaseByteArrayElements(env,text,p,JNI_ABORT);
 (*env)->ReleaseStringUTFChars(env,jid,j);(*env)->ReleaseStringUTFChars(env,id,i);return ok?JNI_TRUE:JNI_FALSE;
}
JNIEXPORT void JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeTick(JNIEnv *env,jobject self,jlong h,jlong now){(void)env;(void)self;if(now>=0)wa_pair_tick(session(h),(uint64_t)now);}

JNIEXPORT jboolean JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeStored(JNIEnv *env,jobject self,jlong h,jstring id,jstring jid,jboolean outgoing) {
 (void)self;if(!h||!id||!jid)return JNI_FALSE;
 const char *i=(*env)->GetStringUTFChars(env,id,NULL);if(!i)return JNI_FALSE;
 const char *j=(*env)->GetStringUTFChars(env,jid,NULL);if(!j){(*env)->ReleaseStringUTFChars(env,id,i);return JNI_FALSE;}
 int ok=wa_pair_stored(session(h),i,j,outgoing==JNI_TRUE);
 (*env)->ReleaseStringUTFChars(env,jid,j);(*env)->ReleaseStringUTFChars(env,id,i);return ok?JNI_TRUE:JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeResolve(JNIEnv *env,jobject self,jlong h,jstring jid) {
 (void)self;if(!h||!jid)return NULL;
 const char *j=(*env)->GetStringUTFChars(env,jid,NULL);if(!j)return NULL;
 char resolved[128];int ok=wa_pair_resolve(session(h),j,resolved);
 (*env)->ReleaseStringUTFChars(env,jid,j);
 return ok?(*env)->NewStringUTF(env,resolved):NULL;
}
