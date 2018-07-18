//
// tcpip_jni.cpp
// UIE MultiAccess
//
// Created by Rakuto Furutani on 11/28/2014
// Copyright 2014 UIEVolution Inc. All Rights Reserved.
//

#include <jni.h>
#include "JNIUtil.h"
#include "SlipInterface.h"
#include "NetconnSocket.h"
#include "PingSender.h"

#define EXTERN_C_DECL extern "C"

// ------------------------------------------------------------------------------
// JNI hooks
// ------------------------------------------------------------------------------

EXTERN_C_DECL JNIEXPORT jint JNI_OnLoad(JavaVM *jvm, void *reserved) {
    JNIUtil::SetUp(jvm);

    SlipInterface::SetUp();
    NetconnSocket::SetUp();
    PingSender::SetUp();

    return JNI_VERSION_1_6;
}

EXTERN_C_DECL JNIEXPORT void JNI_OnUnload(JavaVM *jvm, void *reserved) {
    SlipInterface::TearDown();
    NetconnSocket::TearDown();
    PingSender::TearDown();

    JNIUtil::TearDown();
}
