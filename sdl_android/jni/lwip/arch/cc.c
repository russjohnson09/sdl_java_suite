#include "arch/cc.h"
#include <android/log.h>

void lwip_print_log(char *fmt, ...) {
    va_list al;

    va_start(al, fmt);
    __android_log_vprint(ANDROID_LOG_VERBOSE, ANDROID_LWIP_LOGTAG, fmt, al);
    va_end(al);
}
