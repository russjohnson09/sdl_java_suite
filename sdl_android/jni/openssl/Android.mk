# Copyright 2014 UIEvolution Inc. All rights reserved.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libssl
ifeq ($(TARGET_ARCH), x86)
LOCAL_SRC_FILES := ../openssl/libs/x86/libssl.a
else ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
LOCAL_SRC_FILES := ../openssl/libs/arm64-v8a/libssl.a
else
LOCAL_SRC_FILES := ../openssl/libs/arm/libssl.a
endif

include $(PREBUILT_STATIC_LIBRARY)

## OpenSSL: libcrypt.a
include $(CLEAR_VARS)
LOCAL_MODULE    := libcrypto
ifeq ($(TARGET_ARCH), x86)
LOCAL_SRC_FILES := ../openssl/libs/x86/libcrypto.a
else ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
LOCAL_SRC_FILES := ../openssl/libs/arm64-v8a/libcrypto.a
else
LOCAL_SRC_FILES := ../openssl/libs/arm/libcrypto.a
endif

include $(PREBUILT_STATIC_LIBRARY)
