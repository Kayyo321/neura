#define _POSIX_C_SOURCE 200809L
#include <jni.h>
#include <time.h>

JNIEXPORT jlong JNICALL Java_TimedWorkload_processCpuNanos(JNIEnv *env, jclass type) {
    (void)env;
    (void)type;
    struct timespec value;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) return -1;
    return (jlong)value.tv_sec * 1000000000 + value.tv_nsec;
}
