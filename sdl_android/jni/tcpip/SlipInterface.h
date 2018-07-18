//
// SlipInterface.h
// UIE MultiAccess
//
// Created by Rakuto Furutani on 01/28/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#pragma once

#include <mutex>
#include <atomic>
#include <cstring>
#include <jni.h>
#include <pthread.h>
#include <time.h>
#include <lwip/ip_addr.h>
#include "semaphore.hpp"
#include "BlockingQueue.h"

struct netif;

class SlipInterface {
 public:
    static bool SetUp();
    static bool TearDown();

    JNIEXPORT static jboolean JNICALL AttachWithFD(JNIEnv*, jobject, jobject, jobject, jobject, jint, jobject, jobject);
    JNIEXPORT static jboolean JNICALL Attach(JNIEnv*, jobject, jobject, jobject, jobject, jobject, jobject);
    JNIEXPORT static jboolean JNICALL Detach(JNIEnv*, jobject);
    JNIEXPORT static jboolean JNICALL SetStopOnUsbWriteError(JNIEnv*, jobject, jboolean);
    JNIEXPORT static jboolean JNICALL Input(JNIEnv*, jobject, jobject, jint, jint);
    JNIEXPORT static jint JNICALL SetAncillaryFD(JNIEnv*, jobject, jobject);
    JNIEXPORT static jboolean JNICALL StopReading(JNIEnv*, jobject);
    JNIEXPORT static jboolean JNICALL ReadStopped(JNIEnv*, jobject);
    JNIEXPORT static jint JNICALL RequestWriteBufferEmptyEvent(JNIEnv*, jobject);

    // called by LwIP's timer
    static void TcpipTimerFunc(void *arg);

 private:
    using Lock = std::lock_guard<std::mutex>;

    struct ThreadInfo {
        SlipInterface *thiz_ = nullptr; // for convenience
        pthread_t id_; // valid only if isValid_ is true
        bool isValid_ = false;
        void (SlipInterface::*func_)() = nullptr;
        Semaphore terminateSem_{0};
        std::atomic_bool stopFlag_{false};
        std::atomic_bool isStopped_{true};
        const char *name_;

        ThreadInfo(const char *name)
        : name_(name) {}
    };

    struct Chunk {
        Chunk(const void *data, size_t len) {
            if (len > 0) {
                buf_ = new unsigned char[len];
                length_ = len;
                memcpy(buf_, data, len);
            }
        }
        ~Chunk() {
            delete[] buf_;
        }
        unsigned char *buf_ = nullptr;
        size_t length_ = 0;
    private:
        Chunk(const Chunk &other) = delete;
        Chunk& operator=(const Chunk &other) = delete;
    };

    static int ERROR_USB_WRITE;
    static int ERROR_USB_STUCK;

    static jfieldID instanceFID_;
    jmethodID outputMID_ = 0;
    jmethodID onNativeErrorMID_;
    jmethodID onWriteBufferEmptyMID_;

    static bool signalHandlerSet_;

    int fd_ = -1;
    // read/write threads are used only when fd is provided
    ThreadInfo readThread_{"USB read thread"};
    ThreadInfo writeThread_{"USB write thread"};
    jobject outputListener_ = nullptr;
    jobject errorListener_ = nullptr;
    jobject nativeWriteBufferListener_ = nullptr;
    BlockingQueue<Chunk *> sendQueue_;
    std::mutex mutex_;
    std::atomic_bool usbStopOnWriteError_{true};
    std::atomic_uint wbufEmptyEventRequests_{0};
    std::atomic_int wbufEmptyEventsId_{0}; // 0 through INT_MAX
    std::atomic_int writingId_{-1}; // a number (0 through INT_MAX) is set when write() starts, -1 after write() ends
    int previousWritingId_ = -1;
    int stuckThreshold_ = 0;
    int stuckCount_ = 0;

    struct netif *netif_ = nullptr;
    struct {
        jobject obj;
        void *data;
        jint capacity;
        volatile size_t length;
        volatile size_t offset;

        inline const void *bytes() { return reinterpret_cast<char *>(data) + offset; }
        inline size_t remaining() { return length - offset; }
    } rcvbuf_;

    SlipInterface();
    ~SlipInterface();

    void setRecvBuffer(JNIEnv*, jobject);
    void setOutputListener(JNIEnv *, jobject);
    void setErrorListener(JNIEnv *, jobject);
    void setNativeWriteBufferListener(JNIEnv *, jobject);
    void setWriteStuckTimeout(int);
    bool input(const void *, size_t, size_t);
    jstring getPcapFilePath(JNIEnv *env);
    bool attach(JNIEnv *env, const ip_addr_t&, const ip_addr_t&, int fd=-1);
    bool detach();
    bool setStopOnUsbWriteError(bool);
    bool createThread(ThreadInfo *);
    bool terminateThread(ThreadInfo *);
    bool terminateThread(ThreadInfo *, long);
    void readThreadLoop();
    bool slipOutput(const void*, size_t);
    void writeThreadLoop();
    void wakeupWriteThread();
    bool onUsbRead(uint8_t *data, size_t len);
    void pollWriteThreadStatus();
    void notifyNativeError(int);
    void notifyWriteBufferEmpty(int);
    static long deltaMsec(const struct timespec&, const struct timespec&);
};
