# Copyright 2014 UIEvolution Inc. All rights reserved.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

CORE_SRC  := $(LOCAL_PATH)/core
API_SRC   := $(LOCAL_PATH)/api
ARCH_SRC  := $(LOCAL_PATH)/arch
NETIF_SRC := $(LOCAL_PATH)/netif
IPV4_SRC  := $(LOCAL_PATH)/core/ipv4

LOCAL_MODULE := lwip
LOCAL_SRC_FILES :=             \
	$(wildcard $(CORE_SRC)/*.c)  \
	$(wildcard $(API_SRC)/*.c)   \
	$(wildcard $(ARCH_SRC)/*.c)  \
	$(wildcard $(NETIF_SRC)/*.c) \
	$(wildcard $(IPV4_SRC)/*.c)  \

# remove leading $(LOCAL_PATH) part from the file list
# (files in LOCAL_SRC_FILES should be relative to LOCAL_PATH)
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES:$(LOCAL_PATH)/%=%)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../include \
	$(LOCAL_PATH)/include \
	$(LOCAL_PATH)/include/ipv4 \

include $(BUILD_STATIC_LIBRARY)
