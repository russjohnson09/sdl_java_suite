//
// JNIUtil.h
// UIE MultiAccess
//
// Created by Rakuto Furutani on 11/28/2014
// Copyright 2014 UIEVolution Inc. All Rights Reserved.
//

#pragma once

#include <jni.h>
#include <string>
#include "logger.h"
#include "ScopedLocalRef.h"

class JNIUtil {
 public:
    // Java classes
    static jclass fileDescriptorClass;
    static jclass inetSocketAddressClass;

    // Java method IDs
    static jmethodID inetSocketAddressInitMethod;
    static jmethodID fileDescriptorInitMethod;

    // Java class fields
    static jfieldID fileDescriptorDescriptorField;

    static void SetUp(JavaVM *jvm);
    static void TearDown();

    inline static JavaVM *GetJavaVM() { return jvm_; }
    static JNIEnv *GetEnv();
    static JNIEnv *GetAttachedEnv(bool *needDetach);
    static void DetachEnv();

    static jclass FindClass(const char *className);
    static jmethodID GetMethodID(jclass klass, const char *methodName, const char *signature, bool isStatic);
    static jobject NewGlobalRef(jobject obj);
    static void DeleteGlobalRef(jobject obj);
    static std::string GetString(jstring str, JNIEnv *env=nullptr);

    static unsigned int GetIPv4Address(jobject inetAddress);
    static int GetNativeFileDescriptor(jobject fileDescriptor, JNIEnv *env=nullptr);
    static jobject CreateFileDescriptor(int fd, JNIEnv *env=nullptr);

    static void ThrowException(const char *className, const char *message);
    static void ThrowNullPointerException(const char *message);
    static void ThrowOutOfMemoryException(const char *message);
    static void ThrowInvalidParameterException(const char *message);
    static void ThrowRuntimeException(const char *message);
    static void ThrowIOException(const char *message);


    template <typename Callable>
    static void AttachCurrentThread(Callable cb) {
        bool attached = false;
        JNIEnv *env = nullptr;
        jint res = jvm_->GetEnv((void **)&env, JNI_VERSION_1_6);
        if (res < 0) {
            res = jvm_->AttachCurrentThread(&env, nullptr);
            if (res < 0) {
                LOGE("Give up attaching current thread onto JVM");
                return; // give up
            }
            attached = true;
        }
        (cb)(env);
        if (attached)
            jvm_->DetachCurrentThread();
    }

 private:
    static JavaVM *jvm_;

    static jclass findClass(const char*);
    static jfieldID getFieldID(jclass, const char *, const char*);
};

class JavaObjectHolder {
 public:
    JavaObjectHolder()
    : object_(nullptr) {
    }

    explicit JavaObjectHolder(jobject obj)
    : object_(nullptr) {
        object_ = JNIUtil::NewGlobalRef(obj);
    }

    inline void set(jobject obj) {
        if (object_)
            JNIUtil::DeleteGlobalRef(object_);
        object_ = JNIUtil::NewGlobalRef(obj);
    }

    ~JavaObjectHolder() {
        if (object_)
            JNIUtil::DeleteGlobalRef(object_);
    }

    JavaObjectHolder& operator=(JavaObjectHolder&& other) {
        if (this != &other) {
            object_ = other.object_;
            other.object_ = nullptr;
        }
        return *this;
    }

 private:
    jobject object_;
};


