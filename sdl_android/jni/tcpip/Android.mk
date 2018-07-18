LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := tcpip
LOCAL_SRC_FILES := \
	$(wildcard $(LOCAL_PATH)/*.cpp) \
	$(wildcard $(LOCAL_PATH)/../openssl/*.cpp)

# remove leading $(LOCAL_PATH) part from the file list
# (files in LOCAL_SRC_FILES should be relative to LOCAL_PATH)
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES:$(LOCAL_PATH)/%=%)

LOCAL_STATIC_LIBRARIES := libssl libcrypto lwip

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../include \
	$(LOCAL_PATH)/../lwip/include \
	$(LOCAL_PATH)/../lwip/include/ipv4 \
	$(LOCAL_PATH)/../openssl/include/ \

LOCAL_LDLIBS := \
	-ldl  \
	-llog \
	-latomic

include $(BUILD_SHARED_LIBRARY)
