//
// JNIUtil.cpp
// UIE MultiAccess
//
// Created by Rakuto Furutani on 1/28/2014
// Copyright 2014 UIEVolution Inc. All Rights Reserved.
//

#include <cassert>
#include <linux/socket.h>
#include "JNIUtil.h"
#include "ScopedLocalRef.h"

// static fields
JavaVM* JNIUtil::jvm_;
jclass JNIUtil::fileDescriptorClass;
jclass JNIUtil::inetSocketAddressClass;
jmethodID JNIUtil::inetSocketAddressInitMethod;
jmethodID JNIUtil::fileDescriptorInitMethod;
jfieldID JNIUtil::fileDescriptorDescriptorField;

#define JCLASS_SAFE_RELEASE(kls) do { if (kls) env->DeleteGlobalRef(kls); } while(0)

void JNIUtil::SetUp(JavaVM *jvm) {
    JNIUtil::jvm_ = jvm;

    auto env = JNIUtil::GetEnv();
    assert(env);

    // Java classes
    fileDescriptorClass     = findClass("java/io/FileDescriptor");
    inetSocketAddressClass  = findClass("java/net/InetSocketAddress");

    // Java methods
    inetSocketAddressInitMethod = env->GetMethodID(inetSocketAddressClass, "<init>", "(Ljava/lang/String;I)V");
    fileDescriptorInitMethod = env->GetMethodID(fileDescriptorClass, "<init>", "()V");

    // Java Fields
    fileDescriptorDescriptorField = env->GetFieldID(fileDescriptorClass, "descriptor", "I");
}

void JNIUtil::TearDown() {
    auto env = JNIUtil::GetEnv();
    if (env) {
        JCLASS_SAFE_RELEASE(fileDescriptorClass);
    }
}

JNIEnv* JNIUtil::GetEnv() {
    JNIEnv *env = nullptr;
    assert(jvm_);

    if (jvm_->GetEnv((void **)&env, JNI_VERSION_1_6) < 0) {
        return nullptr;
    }
    return env;
}

JNIEnv* JNIUtil::GetAttachedEnv(bool *needDetach) {
    assert(needDetach);
    JNIEnv *env = JNIUtil::GetEnv();
    if (env) {
        *needDetach = false;
        return env;
    }
    int rc = jvm_->AttachCurrentThread(&env, nullptr);
    if (rc < 0) {
        return nullptr;
    }
    *needDetach = true;

    return env;
}

void JNIUtil::DetachEnv() {
    jvm_->DetachCurrentThread();
}

jobject JNIUtil::NewGlobalRef(jobject klass) {
    JNIEnv *env = JNIUtil::GetEnv();
    if (env) {
        return env->NewGlobalRef(klass);
    }
    return nullptr;
}

void JNIUtil::DeleteGlobalRef(jobject klass) {
    JNIEnv *env = JNIUtil::GetEnv();
    if (env)
        return env->DeleteGlobalRef(klass);
}

std::string JNIUtil::GetString(jstring str, JNIEnv *env) {
    assert(str);
    env = env ?: JNIUtil::GetEnv();
    std::string ret("");
    if (env) {
        auto sz_str = env->GetStringUTFChars(str, 0);
        if (sz_str) {
            ret = std::string(sz_str);
            env->ReleaseStringUTFChars(str, sz_str);
        }
    }
    return ret;
}

unsigned int JNIUtil::GetIPv4Address(jobject inetAddress) {
    unsigned int addr = 0;

    JNIEnv *env = JNIUtil::GetEnv();
    if (!env)
        return addr;

    ScopedLocalRef<jclass> klassHolder(env, env->GetObjectClass(inetAddress));
/*
 *    static jfieldID familyFid = env->GetFieldID(klassHolder.get(), "family", "I");
 *    int addressFamily = env->GetIntField(inetAddress, familyFid);
 *    if (addressFamily != AF_INET) {
 *        JNIUtil::ThrowInvalidParameterException("address family must be AF_INET");
 *        return addr;
 *    }
 *
 */
    // Get the bytes array that contains IP address bytes in the InetAddress
    static jmethodID methodId = env->GetMethodID(klassHolder.get(), "getAddress", "()[B");
    ScopedLocalRef<jbyteArray> addressBytes(env, reinterpret_cast<jbyteArray>(env->CallObjectMethod(inetAddress, methodId)));
    if (!addressBytes.get()) {
        JNIUtil::ThrowInvalidParameterException("address family must be AF_INET");
        return addr;
    }

    env->GetByteArrayRegion(addressBytes.get(), 0, 4, reinterpret_cast<jbyte *>(&addr));
    return addr;
}

int JNIUtil::GetNativeFileDescriptor(jobject fileDescriptor, JNIEnv *env) {
    int fd = -1;
    env = env ?: JNIUtil::GetEnv();
    if (env) {
        fd = env->GetIntField(fileDescriptor, fileDescriptorDescriptorField);
    }
    return fd;
}

jobject JNIUtil::CreateFileDescriptor(int fd, JNIEnv *env) {
    jobject filedesc = nullptr;
    env = env ?: JNIUtil::GetEnv();
    if (env) {
        filedesc = env->NewObject(fileDescriptorClass, fileDescriptorInitMethod);
        env->SetIntField(filedesc, fileDescriptorDescriptorField, fd);
    }
    return filedesc;
}

jclass JNIUtil::FindClass(const char *className) {
    JNIEnv *env = JNIUtil::GetEnv();
    if (!env)
        return nullptr;

    ScopedLocalRef<jclass> holder(env, env->FindClass(className));
    if (!holder.get())
        return nullptr;

    return reinterpret_cast<jclass>(env->NewGlobalRef(holder.get()));
}

jmethodID JNIUtil::GetMethodID(jclass clazz, const char *methodName, const char *signature, bool isStatic) {
    JNIEnv *env = JNIUtil::GetEnv();
    if (!env)
        return nullptr;

    jmethodID methodId;
    if (isStatic) {
        methodId = env->GetStaticMethodID(clazz, methodName, signature);
    } else {
        methodId = env->GetMethodID(clazz, methodName, signature);
    }
    if (!methodId) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return nullptr;
        }
    }
    return methodId;
}

void JNIUtil::ThrowException(const char *className, const char *message) {
    JNIEnv *env = JNIUtil::GetEnv();
    if (env) {
        ScopedLocalRef<jclass> exc(env, env->FindClass(className));
        if (exc.get()) {
            env->ExceptionClear();
            env->ThrowNew(exc.get(), message);
        }
    }
}

void JNIUtil::ThrowNullPointerException(const char *message) {
    JNIUtil::ThrowException("java/lang/NullPointerException", message);
}

void JNIUtil::ThrowOutOfMemoryException(const char *message) {
    JNIUtil::ThrowException("java/lang/OutOfMemoryError", message);
}

void JNIUtil::ThrowInvalidParameterException(const char *message) {
    JNIUtil::ThrowException("java/security/InvalidParameterException", message);
}

void JNIUtil::ThrowRuntimeException(const char *message) {
    JNIUtil::ThrowException("java/lang/RuntimeException", message);
}

void JNIUtil::ThrowIOException(const char *message) {
    JNIUtil::ThrowException("java/io/IOException", message);
}

jclass JNIUtil::findClass(const char *className) {
    auto env = JNIUtil::GetEnv();
    if (!env) {
        return nullptr;
    }

    ScopedLocalRef<jclass> holder(env, env->FindClass(className));
    if (auto klass = holder.get()) {
        return reinterpret_cast<jclass>(env->NewGlobalRef(klass));
    } else {
        LOGE("cannot find class: %s", className);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return nullptr;
    }
}

jfieldID JNIUtil::getFieldID(jclass klass, const char *name, const char *signature) {
    auto env = JNIUtil::GetEnv();
    if (!env) {
        return nullptr;
    }
    jfieldID fid = env->GetFieldID(klass, name, signature);
    if (!fid) {
        LOGE("cannot find Java field ID: %s %s", name, signature);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }
    return fid;
}
