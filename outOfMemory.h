#include "jni.h"
#include "jvmti.h"

static void JNICALL resourceExhausted(
    jvmtiEnv *jvmti, JNIEnv* jni, jint flags, const void* reserved, const char* description);

void setParameters(char *options);

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved);

