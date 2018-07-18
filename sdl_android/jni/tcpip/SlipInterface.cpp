//
// SlipInterface.cpp
// UIE MultiAccess
//
// Created by Rakuto Furutani on 01/28/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#include <unistd.h>
#include <lwip/tcpip.h>
#include <lwip/netifapi.h>
#include <netif/slipif.h>
#include <cassert>
#include <pthread.h>
#include <time.h>
#include "JNIUtil.h"
#include "LwipUtil.h"
#include "logger.h"
#include "SlipInterface.h"

#define CLASS_NAME "com/smartdevicelink/xevo/slip/SlipInterface"

// Unix signal to break blocking operations on USB file descriptor
#define INTERRUPT_SIGNAL                SIGHUP
#define INTERRUPT_SIGNAL_INTERVAL_MSEC  (100)

#define WRITE_THREAD_DEFER_STOP_MSEC    (500)

// For workaround to detect write() blocking. We poll the status every second, and if stuck is detected
// for stuckThreshold_ times then we treat it as an error.
#define WRITE_THREAD_POLLING_MSEC       (1000)

#ifndef NUMBER_OF
#define NUMBER_OF(array) (sizeof(array) / sizeof(array[0]))
#endif

#define MIN(a,b) (a>b) ? b : a
#define MAX(a,b) (a<b) ? b : a

#define FD_READ_BUFFER_SIZE (8192)
#define ERROR_FAILURE (-1)

jfieldID SlipInterface::instanceFID_ = 0;
int SlipInterface::ERROR_USB_WRITE = -1;
int SlipInterface::ERROR_USB_STUCK = -2;
bool SlipInterface::signalHandlerSet_ = false;

static JNINativeMethod METHOD_TABLE[] = {
    {
        "nativeAttachWithFD",
        "(Ljava/net/InetAddress;Ljava/net/InetAddress;Ljava/io/FileDescriptor;ILcom/smartdevicelink/xevo/slip/SlipInterface$NativeErrorListener;Lcom/smartdevicelink/xevo/slip/SlipInterface$NativeWriteBufferListener;)Z",
        (void *)SlipInterface::AttachWithFD
    },
    {
        "nativeAttach",
        "(Ljava/net/InetAddress;Ljava/net/InetAddress;Ljava/nio/ByteBuffer;Lcom/smartdevicelink/xevo/slip/SlipInterface$OutputPacketListener;Lcom/smartdevicelink/xevo/slip/SlipInterface$NativeErrorListener;)Z",
        (void *)SlipInterface::Attach
    },
    {
        "setStopOnUsbWriteError",
        "(Z)Z",
        (void *)SlipInterface::SetStopOnUsbWriteError
    },
    {
        "detach",
        "()Z",
        (void *)SlipInterface::Detach
    },
    {
        "input",
        "(Ljava/nio/ByteBuffer;II)Z",
        (void *)SlipInterface::Input
    },
    {
        "stopReading",
        "()Z",
        (void *)SlipInterface::StopReading
    },
    {
        "readStopped",
        "()Z",
        (void *)SlipInterface::ReadStopped
    },
    {
        "requestWriteBufferEmptyEvent",
        "()I",
        (void *)SlipInterface::RequestWriteBufferEmptyEvent
    }
};

bool SlipInterface::SetUp() {
    JNIEnv *env = JNIUtil::GetEnv();
    if (!env)
        return ERROR_FAILURE;

    ScopedLocalRef<jclass> holder(env, env->FindClass(CLASS_NAME));
    if (!holder.get()) {
        LOGE("cannot find class %s", CLASS_NAME);
        return ERROR_FAILURE;
    }

    // Obtain Java field IDs
    jclass klass = holder.get();
    instanceFID_ = env->GetFieldID(klass, "mNativeInstance", "J");

    // Register native methods
    env->RegisterNatives(klass, METHOD_TABLE, NUMBER_OF(METHOD_TABLE));

    // Initialize TCP/IP thread only once.
    // Attach tcpip_thread to Java VM so that when we call a Java method from native side
    // we don't need to run AttachCurrentThread() and DetachCurrentThread() each time.
    tcpip_init([](void *arg) {
        JavaVM *jvm = JNIUtil::GetJavaVM();
        JNIEnv *env = nullptr;
        jint ret = jvm->AttachCurrentThread(&env, nullptr);
        if (ret < 0) {
            LOGW("Failed to attach tcpip thread to JVM.");
        }
    }, nullptr);

    struct sigaction act;
    act.sa_handler = [](int _) { /* nothing to do */ };
    act.sa_flags = 0; // don't specify SA_RESTART
    sigemptyset(&act.sa_mask);

    int ret = sigaction(INTERRUPT_SIGNAL, &act, nullptr);
    if (ret == 0) {
        signalHandlerSet_ = true;
    } else {
        LOGE("Failed to set up signal handler");
    }

    return true;
}

bool SlipInterface::TearDown() {
    JNIEnv *env = JNIUtil::GetEnv();
    if (!env)
        return ERROR_FAILURE;

    LwipUtil::runOnTcpipThread([](void* /* not used */) {
        JavaVM *jvm = JNIUtil::GetJavaVM();
        jvm->DetachCurrentThread();
    }, nullptr);

    ScopedLocalRef<jclass> holder(env, env->FindClass(CLASS_NAME));
    if (!holder.get()) {
        return ERROR_FAILURE;
    }

    jclass klass = holder.get();
    env->UnregisterNatives(klass);

    return true;
}

SlipInterface::SlipInterface() {
    rcvbuf_.obj      = nullptr;
    rcvbuf_.data     = nullptr;

    readThread_.thiz_ = this;
    readThread_.func_ = &SlipInterface::readThreadLoop;
    writeThread_.thiz_ = this;
    writeThread_.func_ = &SlipInterface::writeThreadLoop;
}

SlipInterface::~SlipInterface() {
    detach();

    if (outputListener_)
        JNIUtil::DeleteGlobalRef(outputListener_);

    if (errorListener_)
        JNIUtil::DeleteGlobalRef(errorListener_);

    if (nativeWriteBufferListener_)
        JNIUtil::DeleteGlobalRef(nativeWriteBufferListener_);

    if (rcvbuf_.obj)
        JNIUtil::DeleteGlobalRef(rcvbuf_.obj);
}

jboolean SlipInterface::AttachWithFD(JNIEnv *env, jobject obj, jobject address, jobject netmask, jobject fileDescriptor,
                                     jint writeStuckTimeoutSec, jobject errorListener, jobject nativeWriteBufferListener) {
    // Check the parameter
    if (!address || !netmask || !fileDescriptor) {
        JNIUtil::ThrowInvalidParameterException("parameters must not be null");
        return (jboolean)false;
    }

    auto slipif = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (slipif) {
        LOGW("Attach failed, SLIP is already initialized");
        return (jboolean)false;
    }

    struct ip_addr addr;
    struct ip_addr mask;
    addr.addr = JNIUtil::GetIPv4Address(address);
    mask.addr = JNIUtil::GetIPv4Address(netmask);

    // Obtain a SlipInterface instance
    int fd = JNIUtil::GetNativeFileDescriptor(fileDescriptor);
    slipif = new SlipInterface();
    if (errorListener) {
        // set the error listener before write thread starts
        slipif->setErrorListener(env, errorListener);
    }
    if (nativeWriteBufferListener) {
        slipif->setNativeWriteBufferListener(env, nativeWriteBufferListener);
    }
    slipif->setWriteStuckTimeout(writeStuckTimeoutSec);
    slipif->attach(env, addr, mask, fd);

    // Keep instance reference on the Java class property.
    env->SetLongField(obj, instanceFID_, reinterpret_cast<jlong>(slipif));

    return (jboolean)true;
}

jboolean SlipInterface::Attach(JNIEnv *env, jobject obj, jobject address, jobject netmask, jobject rcvbuf, jobject outputListener, jobject errorListener) {
    // Check the parameter
    if (!address || !netmask) {
        JNIUtil::ThrowInvalidParameterException("parameters must not be null");
        return (jboolean)false;
    }

    auto thiz = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (thiz) {
        LOGE("Attach failed, SLIP is already initialized");
        return (jboolean)false;
    }

    struct ip_addr addr;
    struct ip_addr mask;

    addr.addr = JNIUtil::GetIPv4Address(address);
    mask.addr = JNIUtil::GetIPv4Address(netmask);

    // Obtain a SlipInterface instance
    thiz = new SlipInterface();
    thiz->attach(env, addr, mask);

    // Set receive buffer
    if (rcvbuf) {
        thiz->setRecvBuffer(env, rcvbuf);
    }

    // Set output listener
    if (outputListener) {
        thiz->setOutputListener(env, outputListener);
    }

    if (errorListener) {
      thiz->setErrorListener(env, errorListener);
    }

    // Update the Java field
    env->SetLongField(obj, instanceFID_, reinterpret_cast<jlong>(thiz));

    return (jboolean)true;
}

jboolean SlipInterface::Detach(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (!thiz) {
        LOGE("Detach failed, SLIP is already initialized");
        return (jboolean)false;
    }
    env->SetLongField(obj, instanceFID_, 0);

    jboolean result = (jboolean)thiz->detach();
    delete thiz;

    return result;
}

jboolean SlipInterface::SetStopOnUsbWriteError(JNIEnv *env, jobject obj, jboolean flag) {
    auto thiz = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (!thiz) {
        return JNI_FALSE;
    }

    LOGD("SlipInterface::SetStopOnUsbWriteError(%s)", flag ? "TRUE" : "FALSE");
    thiz->usbStopOnWriteError_ = flag;
    return JNI_TRUE;
}

jboolean SlipInterface::Input(JNIEnv *env, jobject obj, jobject buffer, jint offset, jint length) {
    // Check the parameter
    if (!buffer) {
        JNIUtil::ThrowInvalidParameterException("buffer must not be null");
        return (jboolean)false;
    }

    // Obtain an instance
    auto thiz = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (thiz) {
        return (jint)thiz->input(env->GetDirectBufferAddress(buffer), offset, length);
    }
    return (jboolean)false;
}

jboolean SlipInterface::StopReading(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (!thiz) {
        return JNI_FALSE;
    }

    LOGI("SlipInterface::StopReading() called");
    thiz->readThread_.stopFlag_ = true;
    return JNI_TRUE;
}

jboolean SlipInterface::ReadStopped(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (!thiz) {
        return JNI_FALSE;
    }

    LOGD("SlipInterface::ReadStopped() status=%s", thiz->readThread_.isStopped_ ? "TRUE" : "FALSE");
    return thiz->readThread_.isStopped_ ? JNI_TRUE : JNI_FALSE;
}

jint SlipInterface::RequestWriteBufferEmptyEvent(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<SlipInterface *>(env->GetLongField(obj, instanceFID_));
    if (!thiz) {
        return -1;
    }
    if (thiz->writeThread_.isStopped_) {
        return -1;
    }

    jint id = thiz->wbufEmptyEventsId_;
    thiz->wbufEmptyEventsId_++;
    if (thiz->wbufEmptyEventsId_ < 0) {
        thiz->wbufEmptyEventsId_ = 0;
    }

    thiz->wbufEmptyEventRequests_.fetch_add(1);
    thiz->wakeupWriteThread();

    return id;
}

// this is called on tcpip_thread
void SlipInterface::TcpipTimerFunc(void *arg) {
    auto thiz = reinterpret_cast<SlipInterface *>(arg);
    thiz->pollWriteThreadStatus();

    // reschedule the timer
    sys_timeout(WRITE_THREAD_POLLING_MSEC, TcpipTimerFunc, arg);
}

// -----------------------------------------------------------
// private stuff
// -----------------------------------------------------------

void SlipInterface::notifyNativeError(int error) {
    if (!errorListener_) return;

    auto env = JNIUtil::GetEnv();
    if (!env) {
        LOGE("Failed to obtain JNI env.");
        return;
    }
    env->CallVoidMethod(errorListener_, onNativeErrorMID_, error);
}

bool SlipInterface::onUsbRead(uint8_t *data, size_t len) {
    // This logic must be run on critical section because netif could be deallocated on detach.
    Lock lk(mutex_);
    if (netif_) {
        // Input received data to SLIP I/F
        int err = slipif_input(netif_, data, len);
        if (err) {
            LOGE("slipif_input failed: err %d", err);
            return false;
        }
    } else {
        // I/F already detached.
        LOGW("USB netif_ is already destroyed");
        return false;
    }

    return true;
}

void SlipInterface::setRecvBuffer(JNIEnv *env, jobject buffer) {
    if (rcvbuf_.obj) {
        env->DeleteGlobalRef(rcvbuf_.obj);
    }
    rcvbuf_.obj      = env->NewGlobalRef(buffer);
    rcvbuf_.data     = env->GetDirectBufferAddress(buffer);
    rcvbuf_.capacity = env->GetDirectBufferCapacity(buffer);
    rcvbuf_.length   = 0;
    rcvbuf_.offset   = 0;
}

void SlipInterface::setOutputListener(JNIEnv *env, jobject listener) {
    assert(env);
    assert(listener);

    // Obtain an instance
    ScopedLocalRef<jclass> holder(env, env->GetObjectClass(listener));
    outputMID_ = env->GetMethodID(holder.get(), "onSlipPacketReady", "(Ljava/nio/ByteBuffer;II)V");

    if (outputListener_) {
        env->DeleteGlobalRef(outputListener_);
        outputListener_ = nullptr;
    }
    outputListener_ = env->NewGlobalRef(listener);
}

void SlipInterface::setErrorListener(JNIEnv *env, jobject listener) {
    assert(env);
    assert(listener);

    // Obtain an instance
    ScopedLocalRef<jclass> holder(env, env->GetObjectClass(listener));
    onNativeErrorMID_ = env->GetMethodID(holder.get(), "onNativeError", "(I)V");

    if (errorListener_) {
        env->DeleteGlobalRef(errorListener_);
        errorListener_ = nullptr;
    }
    errorListener_ = env->NewGlobalRef(listener);
}

void SlipInterface::setNativeWriteBufferListener(JNIEnv *env, jobject listener) {
    assert(env);
    assert(listener);

    ScopedLocalRef<jclass> holder(env, env->GetObjectClass(listener));
    onWriteBufferEmptyMID_ = env->GetMethodID(holder.get(), "onBufferEmpty", "(I)V");

    if (nativeWriteBufferListener_) {
        env->DeleteGlobalRef(nativeWriteBufferListener_);
        nativeWriteBufferListener_ = nullptr;
    }
    nativeWriteBufferListener_ = env->NewGlobalRef(listener);
}

void SlipInterface::setWriteStuckTimeout(int seconds) {
    if (seconds > 0) {
        stuckThreshold_ = seconds * 1000 / WRITE_THREAD_POLLING_MSEC;
    } else {
        stuckThreshold_ = 0;
    }
}

bool SlipInterface::input(const void *buffer, size_t offset, size_t length) {
    assert(buffer);

    if (!netif_)
        return false;

    err_t err = slipif_input(netif_, (char *)(buffer) + offset, length);
    if (err != ERR_OK) {
        LOGE("slipif_input failed with error: %d", err);
        return false;
    }
    return true;
}

jstring SlipInterface::getPcapFilePath(JNIEnv *env) {
    assert(env);

    const char *pcap_class_name = "com/smartdevicelink/xevo/util/Pcap";
    ScopedLocalRef<jclass> klass(env, env->FindClass(pcap_class_name));
    if (klass.get() == NULL) {
        LOGE("Cannot find class %s", pcap_class_name);
        return NULL;
    }

    jmethodID methodID = env->GetStaticMethodID(klass.get(), "getPcapFilePath", "()Ljava/lang/String;");
    if (methodID == NULL) {
        LOGE("Failed to get method ID.");
        return NULL;
    }

    return (jstring)env->CallStaticObjectMethod(klass.get(), methodID);
}

bool SlipInterface::attach(JNIEnv *env, const ip_addr_t &address, const ip_addr_t &netmask, int fd) {
    if (netif_) {
        LOGE("SLIP I/F is already attached");
        return false;
    }

    // Initialie SLIP I/F
    struct netif *slipif = (struct netif *)calloc(1, sizeof(struct netif));
    if (!slipif) {
        LOGE("cannot allocate network interface");
        return false;
    }

    LOGD("Attach SLIP I/F (addr=0x%x, netmask=0x%x, fd=%d)", ntohl(address.addr), ntohl(netmask.addr), fd);

    const char* pcap_file_path = NULL;
#if PCAP_LOG_OUTPUT

    ScopedLocalRef<jstring> pcap_file_path_jstring(env, getPcapFilePath(env));
    if (pcap_file_path_jstring.get() == NULL) {
        return false;
    }

    pcap_file_path = env->GetStringUTFChars(pcap_file_path_jstring.get(), 0);
    if (pcap_file_path == NULL) {
        LOGE("Cannot get a pcap file path");
        return false;
    }

#endif

    slipif->num = 0;
    slipif->data = this;
    slipif_init(slipif, pcap_file_path, [](struct netif *netif, void *data, size_t len) {
        assert(netif->data);
        auto self = reinterpret_cast<decltype(this)>(netif->data);
        self->slipOutput(data, len);
    });

#if PCAP_LOG_OUTPUT

    env->ReleaseStringUTFChars(pcap_file_path_jstring.get(), pcap_file_path);

#endif

    struct ip_addr gateway = {0};
    // call netif_add() on tcpip_thread
    err_t ret = netifapi_netif_add(slipif, const_cast<ip_addr_t *>(&address), const_cast<ip_addr_t *>(&netmask), &gateway, slipif->state,
        [](struct netif *netif) -> err_t {
            return ERR_OK;
        }, tcpip_input);
    if (ret != ERR_OK) {
        LOGE("netif_add failed.");
        free(slipif);
        return false;
    }

    // call netif_set_up() on tcpip_thread
    ret = netifapi_netif_set_up(slipif);
    if (ret != ERR_OK) {
        LOGE("netif_set_up failed.");
        netifapi_netif_remove(slipif);
        free(slipif);
        return false;
    }

    netif_ = slipif;

    // If specified, set the file descriptor in which read and write data.
    // Note that this class does not open and close file descriptor.
    // Upper layer must manage I/O life cycle.
    if (fd >= 0) {
        fd_ = fd;
        createThread(&readThread_);

        if (stuckThreshold_ > 0) {
            // create a timer on tcpip_thread
            LwipUtil::runOnTcpipThread([](void *arg) {
                auto thiz = reinterpret_cast<SlipInterface *>(arg);
                thiz->previousWritingId_ = -1;
                thiz->stuckCount_ = 0;
                sys_timeout(WRITE_THREAD_POLLING_MSEC, TcpipTimerFunc, arg);
            }, this);
        }

        createThread(&writeThread_);
    }

    return true;
}

bool SlipInterface::detach() {
    LOGI("Detach SLIP I/F");
    // if read/write threads are created, wait until they are terminated
    terminateThread(&readThread_);

    // unregister polling timer on tcpip_thread
    if (stuckThreshold_ > 0) {
        err_t ret = tcpip_untimeout(TcpipTimerFunc, this);
        if (ret != ERR_OK) {
            LOGW("failed to stop polling timer");
        }
    }

    writeThread_.stopFlag_ = true;
    wakeupWriteThread();    // if sendQueue_.front() is blocked, release it
    // We should use the signal only when write() call is blocked for a long time. (Interrupting read()
    // or write() call will leave the kernel driver in a bad state and USB may not work until the
    // cable is reconnected.) So wait for some time and see if the thread stops without a signal.
    terminateThread(&writeThread_, WRITE_THREAD_DEFER_STOP_MSEC);

    // This logic must be run on critical section because netif could be deallocated on detach.
    Lock lk(mutex_);
    if (netif_) {
        fd_ = -1;
        // call netif_set_down() and netif_remove() on tcpip_thread
        netifapi_netif_set_down(netif_);
        netifapi_netif_remove(netif_);
        free(netif_);
        netif_ = nullptr;
    }

    while (sendQueue_.size() > 0) {
        Chunk *chunk = sendQueue_.front();
        sendQueue_.pop();
        delete chunk;
    }

    return true; // always return true
}

bool SlipInterface::createThread(ThreadInfo *threadInfo) {
    if (!threadInfo) {
        return false;
    }
    if (threadInfo->isValid_) {
        LOGE("%s is already created.", threadInfo->name_);
        return false;
    }

    threadInfo->isStopped_ = false;

    pthread_t th;
    int ret = pthread_create(&th, nullptr, [](void *arg) -> void * {
        auto info = reinterpret_cast<ThreadInfo*>(arg);
        auto thiz = info->thiz_;
        auto func = info->func_;

        (thiz->*func)();
        return nullptr;
    }, threadInfo);
    if (ret != 0) {
        LOGE("Failed to create %s, ret=%d", threadInfo->name_, ret);
        threadInfo->isStopped_ = true;
        return false;
    }

    threadInfo->id_ = th;
    threadInfo->isValid_ = true;
    return true;
}

bool SlipInterface::terminateThread(ThreadInfo *threadInfo) {
    return terminateThread(threadInfo, 0);
}

bool SlipInterface::terminateThread(ThreadInfo *threadInfo, long deferMsec) {
    if (!threadInfo) {
        return false;
    }
    if (!threadInfo->isValid_) {
        return false;
    }
    // don't call this method from the thread itself
    if (threadInfo->id_ == pthread_self()) {
        LOGW("terminateThread() called from %s itself", threadInfo->name_);
        return false;
    }

    // if deferMsec is specified, poll isStopped_ flag for max. of deferMsec time before employing
    // the signal
    if (deferMsec > 0) {
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        now = start;

        LOGI("terminateThread() first wait for %ld msec", deferMsec);
        while (!threadInfo->isStopped_ && deltaMsec(start, now) < deferMsec) {
            struct timespec interval = {0, 10 * 1000 * 1000};   // 10 msec
            nanosleep(&interval, nullptr);
            clock_gettime(CLOCK_MONOTONIC, &now);
        }
    }

    int ret;

    /*
     * Using pthread_kill() is tricky. Here are the issues and solutions.
     * (1) We cannot guarantee that the thread is blocked by I/O operation (such as read()) at the time
     * when we call pthread_kill(). i.e. the thread may start read() after we issue pthread_kill().
     * Solution: call pthread_kill() periodically until we confirm that the thread gets out of
     * the loop.
     *
     * (2) We should not issue pthread_kill() to a thread which is already terminated. On some
     * Linux environments, it causes a crash (not sure for Android).
     * Solution: Ensure that the thread keeps running until we finish calling pthread_kill(). This
     * is achieved by using condition_variable.wait() in the thread.
     * DO NOT implement "check thread alive then call pthread_kill()" routine. The thread may
     * terminate just after its liveness is checked and before pthread_kill() is called!
     */
    while (!threadInfo->isStopped_) {
        LOGW("Try to stop %s", threadInfo->name_);
        if (!signalHandlerSet_) {
            LOGW("signal cannot be used");
            return false;
        }
        ret = pthread_kill(threadInfo->id_, INTERRUPT_SIGNAL);
        if (ret != 0) {
            LOGW("Failed to stop %s", threadInfo->name_);
            return false;
        }

        struct timespec interval = {INTERRUPT_SIGNAL_INTERVAL_MSEC / 1000,
                                    (INTERRUPT_SIGNAL_INTERVAL_MSEC % 1000) * 1000 * 1000};
        nanosleep(&interval, nullptr);
    }

    threadInfo->terminateSem_.signal();

    ret = pthread_join(threadInfo->id_, nullptr);
    if (ret != 0) {
        LOGW("Failed to terminate %s, ret=%d", threadInfo->name_, ret);
        // don't return false because the thread should be terminated
    }
    threadInfo->isValid_ = false;
    LOGI("%s terminated.", threadInfo->name_);

    return true;
}

void SlipInterface::readThreadLoop() {
    uint8_t buf[FD_READ_BUFFER_SIZE];
    LOGI("Starting native read thread");

    while (!readThread_.stopFlag_ && fd_ >= 0) {
        // USB file descriptor (/dev/usb_accessory) supports only blocking I/O, damn!
        ssize_t ret = read(fd_, buf, sizeof(buf));
        if (ret == -1) {
            if (errno == EINTR) {
                LOGI("USB read thread interrupted");
            } else {
                LOGW("USB read failed: %s", strerror(errno));
            }
            break;
        } else if (ret == 0) {
            LOGI("USB reading reached EOS");
            break;
        }

        if (!onUsbRead(buf, static_cast<size_t>(ret))) {
            break;
        }
    }
    if (readThread_.stopFlag_) {
        LOGI("readThreadLoop(): stop reading flag set, don't read data any more");
    }
    readThread_.isStopped_ = true;

    // see the comment in terminateReadThread()
    readThread_.terminateSem_.wait();

    LOGI("native USB read thread ends");
}

bool SlipInterface::slipOutput(const void *data, size_t length) {
    if (fd_ >= 0) {
        Chunk *chunk = new Chunk(data, length);
        sendQueue_.push(chunk);
        return true;
    } else if (outputListener_) {
        assert(rcvbuf_.capacity >= length);
        memcpy(rcvbuf_.data, data, length);

        JNIEnv *env = JNIUtil::GetEnv();
        if (!env) {
            LOGE("Failed to get JNI env.");
            return false;
        }
        env->CallVoidMethod(outputListener_, outputMID_, rcvbuf_.obj, 0, length);
        return true;
    }

    return false;
}

void SlipInterface::writeThreadLoop() {
    LOGI("Starting native write thread");

    // Attach this thread to Java VM so that we do not need to call AttachCurrentThread() / DetachCurrentThread()
    // each time when calling a Java method from native code.
    JavaVM *jvm = JNIUtil::GetJavaVM();
    JNIEnv *env = nullptr;
    jint err = jvm->AttachCurrentThread(&env, nullptr);
    if (err < 0) {
        LOGW("Failed to attach USB write thread to JVM.");
    }

    int wbufEmptyEventsId = 0;
    writingId_ = -1;

    while (fd_ >= 0) {
        Chunk *chunk = sendQueue_.front();
        sendQueue_.pop();
        size_t offset = 0;
        writingId_ = wbufEmptyEventsId; // set writingId_ during write() operation
        while (offset < chunk->length_) {
            if (writeThread_.stopFlag_) {
                break;
            }

            // Again, USB file descriptor (/dev/usb_accessory) supports only blocking I/O !
            int ret = write(fd_, chunk->buf_ + offset, chunk->length_ - offset);
            if (ret < 0) {
                if (errno == EINTR) {
                    LOGI("USB write thread interrupted");
                } else {
                    LOGW("USB write failed: %s", strerror(errno));
                    if (usbStopOnWriteError_) {
                        notifyNativeError(SlipInterface::ERROR_USB_WRITE);
                        fd_ = -1;
                    }
                    // discard this chunk
                    break;
                }
            } else {
                offset += ret;
            }
        }
        delete chunk;
        writingId_ = -1;

        if (wbufEmptyEventRequests_ > 0 && sendQueue_.size() == 0) {
            while (wbufEmptyEventRequests_ > 0) {
                wbufEmptyEventRequests_.fetch_sub(1);
                notifyWriteBufferEmpty(wbufEmptyEventsId);

                wbufEmptyEventsId++;
                if (wbufEmptyEventsId < 0) {
                    wbufEmptyEventsId = 0;
                }
            }
        }

        if (writeThread_.stopFlag_) {
            break;
        }
    }

    writeThread_.isStopped_ = true;

    // if there are buffer empty requests, notify before stopping the thread
    while (wbufEmptyEventRequests_ > 0) {
        wbufEmptyEventRequests_.fetch_sub(1);
        notifyWriteBufferEmpty(wbufEmptyEventsId);

        wbufEmptyEventsId++;
        if (wbufEmptyEventsId < 0) {
            wbufEmptyEventsId = 0;
        }
    }

    writeThread_.terminateSem_.wait();

    jvm->DetachCurrentThread();
    LOGI("native USB write thread ends");
}

void SlipInterface::wakeupWriteThread() {
    Chunk *dummy = new Chunk(nullptr, 0);
    sendQueue_.push(dummy);
}

void SlipInterface::pollWriteThreadStatus() {
    // if write() is still running since previous poll, increment the count
    if (writingId_ >= 0 && writingId_ == previousWritingId_) {
        stuckCount_++;
    } else {
        previousWritingId_ = writingId_;
        stuckCount_ = 0;
    }

    if (stuckCount_ == stuckThreshold_) { // '==' instead of '>=' so that the error is notified only once
        notifyNativeError(SlipInterface::ERROR_USB_STUCK);
    }
}

void SlipInterface::notifyWriteBufferEmpty(int id) {
    if (!nativeWriteBufferListener_) {
        return;
    }

    auto env = JNIUtil::GetEnv();
    if (!env) {
        LOGE("Failed to obtain JNI env.");
        return;
    }
    env->CallVoidMethod(nativeWriteBufferListener_, onWriteBufferEmptyMID_, id);
}

// static
long SlipInterface::deltaMsec(const struct timespec& t1, const struct timespec& t2)
{
    return (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_nsec - t1.tv_nsec) / 1000 / 1000;
}
