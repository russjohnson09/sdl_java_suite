NDK_TOOLCHAIN_VERSION := clang
APP_STL := c++_static
#LIBCXX_FORCE_REBUILD := true
#APP_OPTIM := debug
#APP_OPTIM := release
ifeq ($(APP_OPTIM),debug)
APP_CFLAGS := -O0 -g -DNDK_DEBUG=1 -DANDROID -DANDROID_DEBUG_LOG -DLWIP_DEBUG -DPCAP_LOG=1 -Wall -Wno-deprecated-register -Wno-macro-redefined
else
APP_CFLAGS := -O3 -DANDROID -DLWIP_NOASSERT -Wall -Wno-deprecated-register -Wno-macro-redefined
endif
APP_CPPFLAGS := -std=c++1y -fexceptions -fpermissive -D__GXX_EXPERIMENTAL_CXX0X__
APP_USE_CPP0X := true
APP_PLATFORM := android-21
#APP_ABI := armeabi armeabi-v7a arm64-v8a x86
APP_ABI := armeabi armeabi-v7a x86
