/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_processwarp_android_Worker */

#ifndef _Included_org_processwarp_android_Worker
#define _Included_org_processwarp_android_Worker
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_processwarp_android_Worker
 * Method:    workerInitialize
 * Signature: (Lorg/processwarp/android/Worker;Ljava/lang/String;Ljava/lang/String;JJLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_processwarp_android_Worker_workerInitialize
  (JNIEnv *, jobject, jobject, jstring, jstring, jlong, jlong, jstring);

/*
 * Class:     org_processwarp_android_Worker
 * Method:    workerRelayCommand
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_processwarp_android_Worker_workerRelayCommand
  (JNIEnv *, jobject, jstring, jstring, jstring, jint, jstring);

/*
 * Class:     org_processwarp_android_Worker
 * Method:    workerExecute
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_processwarp_android_Worker_workerExecute
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_processwarp_android_Worker
 * Method:    workerQuit
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_processwarp_android_Worker_workerQuit
  (JNIEnv *, jobject, jstring);

#ifdef __cplusplus
}
#endif
#endif
/* Header for class org_processwarp_android_Worker_Delegate */

#ifndef _Included_org_processwarp_android_Worker_Delegate
#define _Included_org_processwarp_android_Worker_Delegate
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __cplusplus
}
#endif
#endif