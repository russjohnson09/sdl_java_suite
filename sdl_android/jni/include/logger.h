/*
 * logger.h
 * UIE MultiAccess
 *
 * Created by Rakuto Furutani on 5/24/14.
 * Copyright (c) 2013 UIEvolution Inc. All rights reserved.
 */
#pragma once

#include <string.h>
#include <stdio.h>
#include <thread>

#ifdef ANDROID
#include <android/log.h>
#endif

#ifdef ANDROID
# define ANDROID_LOG_TAG "XevoSDL/Native"
# define PrintLog(...) do { \
      char buffer[2048]; \
      snprintf(buffer, 2048, __VA_ARGS__); \
      __android_log_print(ANDROID_LOG_DEBUG, ANDROID_LOG_TAG, "%s", buffer); \
} while(0)

# define TRACE_LOG(priority, ...) do { \
      char buffer[2048]; \
      snprintf(buffer, 2048, __VA_ARGS__); \
      __android_log_print(priority, ANDROID_LOG_TAG, "%s", buffer); \
    } while(0)

#  ifdef ANDROID_DEBUG_LOG
#    define LOGE(...) TRACE_LOG(ANDROID_LOG_ERROR, __VA_ARGS__)
#    define LOGW(...) TRACE_LOG(ANDROID_LOG_WARN, __VA_ARGS__)
#    define LOGI(...) TRACE_LOG(ANDROID_LOG_INFO, __VA_ARGS__)
#    define LOGD(...) TRACE_LOG(ANDROID_LOG_DEBUG, __VA_ARGS__)
#    ifdef ANDROID_VERBOSE_LOG
#      define LOGV(...) TRACE_LOG(ANDROID_LOG_VERBOSE, __VA_ARGS__)
#    else // ANDROID_VERBOSE_LOG
#      define LOGV(...)
#    endif

#  else // ANDROID_DEBUG_LOG
#    define PrintLog(...)
#    define LOGE(...) TRACE_LOG(ANDROID_LOG_ERROR, __VA_ARGS__)
#    define LOGW(...) TRACE_LOG(ANDROID_LOG_WARN, __VA_ARGS__)
#    define LOGI(...) TRACE_LOG(ANDROID_LOG_INFO, __VA_ARGS__)
#    define LOGD(...)
#    define LOGV(...)
#  endif
#else
#  define PrintLog(...) do { \
      char buffer[2048]; \
      snprintf(buffer, 2048, __VA_ARGS__); \
      fprintf(stdout, "[%04x] %s\n", (int)std::hash<std::thread::id>()(std::this_thread::get_id()), buffer); \
      fflush(stdout); \
} while(0)

#    define LOGE(...) PrintLog(__VA_ARGS__)
#    define LOGW(...) PrintLog(__VA_ARGS__)
#    define LOGI(...) PrintLog(__VA_ARGS__)
#    define LOGD(...) PrintLog(__VA_ARGS__)
#    define LOGV(...) PrintLog(__VA_ARGS__)

#endif // ANDROID
