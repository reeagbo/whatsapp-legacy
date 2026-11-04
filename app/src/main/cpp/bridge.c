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
 jobjectArray out=(*env)->NewObjectArray(env,6,cls,NULL);(*env)->DeleteLocalRef(env,cls);if(!out)return NULL;
 char state[32],time[32];snprintf(time,sizeof time,"%llu",(unsigned long long)e->timestamp);snprintf(state,sizeof state,"%d",e->state);const char *fields[]={e->kind,e->id,e->jid,e->text,state,time};
 for(int i=0;i<6;i++){jstring value=utf8_string(env,fields[i]);if(!value)return NULL;(*env)->SetObjectArrayElement(env,out,i,value);(*env)->DeleteLocalRef(env,value);}
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

#include "history.h"
#include <stdlib.h>
JNIEXPORT jboolean JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryRequest(JNIEnv *env,jobject self,jlong h,jstring jid,jstring anchor,jboolean own,jlong time,jstring id) {
 (void)self;if(!h||!jid||!anchor||!id||time<0)return JNI_FALSE;
 const char *j=(*env)->GetStringUTFChars(env,jid,NULL);if(!j)return JNI_FALSE;
 const char *a=(*env)->GetStringUTFChars(env,anchor,NULL);if(!a){(*env)->ReleaseStringUTFChars(env,jid,j);return JNI_FALSE;}
 const char *i=(*env)->GetStringUTFChars(env,id,NULL);int ok=i&&wa_pair_history(session(h),j,a,own==JNI_TRUE,(uint64_t)time,i);
 if(i)(*env)->ReleaseStringUTFChars(env,id,i);(*env)->ReleaseStringUTFChars(env,anchor,a);(*env)->ReleaseStringUTFChars(env,jid,j);return ok?JNI_TRUE:JNI_FALSE;
}
JNIEXPORT jbyteArray JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryTask(JNIEnv *env,jobject self,jlong h,jint slot) {
 (void)self;if(!h||slot<0||slot>=WA_HISTORY_TASKS)return NULL;uint8_t *buf=malloc(WA_HISTORY_NOTIFICATION_MAX);if(!buf)return NULL;
 size_t n=0;int r=wa_pair_history_task(session(h),(unsigned)slot,buf,WA_HISTORY_NOTIFICATION_MAX,&n);jbyteArray out=NULL;
 if(r==1){out=(*env)->NewByteArray(env,(jsize)n);if(out)(*env)->SetByteArrayRegion(env,out,0,(jsize)n,(jbyte*)buf);}
 OPENSSL_cleanse(buf,WA_HISTORY_NOTIFICATION_MAX);free(buf);return out;
}
JNIEXPORT jboolean JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryDone(JNIEnv *env,jobject self,jlong h,jint slot,jbyteArray task,jlong version) {
 (void)self;if(!h||!task||slot<0||slot>=WA_HISTORY_TASKS)return JNI_FALSE;jsize n=(*env)->GetArrayLength(env,task);if(n<=0||n>WA_HISTORY_NOTIFICATION_MAX)return JNI_FALSE;
 jbyte *p=(*env)->GetByteArrayElements(env,task,NULL);if(!p)return JNI_FALSE;int ok=wa_pair_history_done(session(h),(unsigned)slot,(uint8_t*)p,(size_t)n,(uint64_t)version);(*env)->ReleaseByteArrayElements(env,task,p,JNI_ABORT);return ok?JNI_TRUE:JNI_FALSE;
}
JNIEXPORT jstring JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryPath(JNIEnv *env,jobject self,jbyteArray task) {
 (void)self;if(!task)return NULL;jsize n=(*env)->GetArrayLength(env,task);if(n<=0||n>WA_HISTORY_NOTIFICATION_MAX)return NULL;jbyte *p=(*env)->GetByteArrayElements(env,task,NULL);if(!p)return NULL;
 char path[4096];int ok=wa_history_path((uint8_t*)p,(size_t)n,path,sizeof path);(*env)->ReleaseByteArrayElements(env,task,p,JNI_ABORT);return ok?(*env)->NewStringUTF(env,path):NULL;
}
JNIEXPORT jlong JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryOpen(JNIEnv *env,jobject self,jlong h,jbyteArray task,jbyteArray encrypted,jobjectArray missing) {
 (void)self;if(!h||!task||!encrypted||!missing)return 0;jsize n=(*env)->GetArrayLength(env,task),en=(*env)->GetArrayLength(env,encrypted);if(n<=0||n>WA_HISTORY_NOTIFICATION_MAX||en>WA_HISTORY_DOWNLOAD_MAX)return 0;
 jbyte *p=(*env)->GetByteArrayElements(env,task,NULL);if(!p)return 0;jbyte *b=en?(*env)->GetByteArrayElements(env,encrypted,NULL):NULL;if(en&&!b){(*env)->ReleaseByteArrayElements(env,task,p,JNI_ABORT);return 0;}
 char key[129]={0};
 wa_history *hist=wa_pair_history_open(session(h),(uint8_t*)p,(size_t)n,(uint8_t*)b,(size_t)en,key);
 if(key[0]){jstring value=(*env)->NewStringUTF(env,key);if(value){(*env)->SetObjectArrayElement(env,missing,0,value);(*env)->DeleteLocalRef(env,value);}}
 OPENSSL_cleanse(key,sizeof key);if(b)(*env)->ReleaseByteArrayElements(env,encrypted,b,JNI_ABORT);(*env)->ReleaseByteArrayElements(env,task,p,JNI_ABORT);return (jlong)(intptr_t)hist;
}
JNIEXPORT jstring JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeIdentityPhone(JNIEnv *env,jobject self,jstring path){
 (void)self;if(!path)return NULL;
 const char *p=(*env)->GetStringUTFChars(env,path,NULL);if(!p)return NULL;
 wa_identity identity={0};jstring out=NULL;
 if(wa_identity_open(p,&identity)){
  const char *domain=strchr(identity.jid,'@');
  size_t n=strspn(identity.jid,"0123456789");
  if(domain&&!strcmp(domain,"@s.whatsapp.net")&&n>=8&&n<=15&&(identity.jid+n==domain||identity.jid[n]==':')){
   identity.jid[n]=0;out=(*env)->NewStringUTF(env,identity.jid);
  }
 }
 OPENSSL_cleanse(&identity,sizeof identity);(*env)->ReleaseStringUTFChars(env,path,p);return out;
}
JNIEXPORT jlong JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryVersion(JNIEnv *env,jobject self,jlong h){
 (void)env;(void)self;return (jlong)wa_history_version((wa_history*)(intptr_t)h);
}
JNIEXPORT jlong JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryOrder(JNIEnv *env,jobject self,jbyteArray task){
 (void)self;jsize n=(*env)->GetArrayLength(env,task);jbyte *p=(*env)->GetByteArrayElements(env,task,NULL);if(!p)return 0;
 int kind=wa_history_job_kind((uint8_t*)p,(size_t)n);
 uint64_t order=kind==3?0:kind==1?1:kind==2?2+wa_history_job_order((uint8_t*)p,(size_t)n):INT64_MAX;
 (*env)->ReleaseByteArrayElements(env,task,p,JNI_ABORT);return (jlong)order;
}
JNIEXPORT jboolean JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeSyncKey(JNIEnv *env,jobject self,jlong h,jstring key,jstring id){
 (void)self;if(!h||!key||!id)return JNI_FALSE;
 const char *k=(*env)->GetStringUTFChars(env,key,NULL);if(!k)return JNI_FALSE;
 const char *i=(*env)->GetStringUTFChars(env,id,NULL);int ok=i&&wa_pair_sync_key(session(h),k,i);
 if(i)(*env)->ReleaseStringUTFChars(env,id,i);
 (*env)->ReleaseStringUTFChars(env,key,k);return ok?JNI_TRUE:JNI_FALSE;
}
JNIEXPORT jobjectArray JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryNext(JNIEnv *env,jobject self,jlong h) {
 (void)self;if(!h)return NULL;wa_message_event e;uint64_t time=0;int r=wa_history_next((wa_history*)(intptr_t)h,&e,&time);
 if(r<0){jclass cls=(*env)->FindClass(env,"java/io/IOException");if(cls)(*env)->ThrowNew(env,cls,"Invalid history record");return NULL;}if(!r)return NULL;
 char status[32],timestamp[32];snprintf(status,sizeof status,"%d",e.state);snprintf(timestamp,sizeof timestamp,"%llu",(unsigned long long)time);
 const char *fields[]={e.kind,e.id,e.jid,e.text,status,timestamp};jclass cls=(*env)->FindClass(env,"java/lang/String");if(!cls)return NULL;jobjectArray out=(*env)->NewObjectArray(env,6,cls,NULL);(*env)->DeleteLocalRef(env,cls);if(!out)return NULL;
 for(int i=0;i<6;i++){jstring value=utf8_string(env,fields[i]);if(!value)return NULL;(*env)->SetObjectArrayElement(env,out,i,value);(*env)->DeleteLocalRef(env,value);}return out;
}
JNIEXPORT void JNICALL Java_org_reeagbo_whatsapplegacy_MainActivity_nativeHistoryClose(JNIEnv *env,jobject self,jlong h){(void)env;(void)self;wa_history_free((wa_history*)(intptr_t)h);}
