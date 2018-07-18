//
// PingSender.cpp
//   Sends ICMP echo message and receive reply using LWIP Socket API.
//
// Copyright 2015 UIEvolution Inc. All Rights Reserved.
//

#include <lwip/sockets.h>
#include <lwip/ip.h>
#include <lwip/icmp.h>
#include <lwip/inet.h>
#include <lwip/inet_chksum.h>

#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <cassert>
#include <cerrno>
#include <ctime>
#include "JNIUtil.h"
#include "logger.h"
#include "ScopedLocalRef.h"
#include "PingSender.h"

#ifndef NUMBER_OF
#define NUMBER_OF(array) (sizeof(array) / sizeof(array[0]))
#endif

#define BACKUP_STOP_TIMEOUT_MSEC    (1000)

/*
 * I am not sure whether LWIP stack supports operations from multiple threads. So, I think it is
 * better that we use this class exclusively and not with NetconnSocket class.
 */

static const char * const CLASS_NAME = "com/smartdevicelink/xevo/util/PingSender";
static jfieldID FID_INSTANCE;

static void Ping_Sender_New(JNIEnv *env, jobject obj) {
    auto thiz = new PingSender();
    if (!thiz) {
        JNIUtil::ThrowOutOfMemoryException("PingSender");
        return;
    }

    env->SetLongField(obj, FID_INSTANCE, reinterpret_cast<jlong>(thiz));
}

static jboolean Ping_Sender_Configure(JNIEnv *env, jobject obj, jstring destAddr, jint intervalMsec, jobject listener, jint dataSize) {
    if (intervalMsec < 0 || dataSize < 0) {
        return false;
    }

    auto thiz = reinterpret_cast<PingSender *>(env->GetLongField(obj, FID_INSTANCE));
    if (!thiz) {
        return false;
    }

    std::string addr = JNIUtil::GetString(destAddr);

    bool ret = thiz->Configure(addr.c_str(), static_cast<unsigned int>(intervalMsec), listener, static_cast<size_t>(dataSize));
    return ret ? JNI_TRUE : JNI_FALSE;
}

static jboolean Ping_Sender_Release(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<PingSender *>(env->GetLongField(obj, FID_INSTANCE));
    if (!thiz) {
        return false;
    }
    env->SetLongField(obj, FID_INSTANCE, 0);
    delete thiz;

    return true;
}

static jboolean Ping_Sender_Start(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<PingSender *>(env->GetLongField(obj, FID_INSTANCE));
    if (!thiz) {
        return false;
    }

    return thiz->Start() ? JNI_TRUE : JNI_FALSE;
}

static jboolean Ping_Sender_Stop(JNIEnv *env, jobject obj, jint timeoutMsec) {
    auto thiz = reinterpret_cast<PingSender *>(env->GetLongField(obj, FID_INSTANCE));
    if (!thiz) {
        return false;
    }

    return thiz->Stop(timeoutMsec);
}

static jboolean Ping_Sender_CheckReply(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<PingSender *>(env->GetLongField(obj, FID_INSTANCE));
    if (!thiz) {
        return false;
    }

    return thiz->CheckReply() ? JNI_TRUE : JNI_FALSE;
}



static JNINativeMethod METHOD_TABLE[] = {
    { "nativeNew",        "()V",                                                                         (void*)Ping_Sender_New        },
    { "nativeConfigure",  "(Ljava/lang/String;ILcom/smartdevicelink/xevo/util/PingSender$PingSenderListener;I)Z", (void*)Ping_Sender_Configure  },
    { "nativeRelease",    "()Z",                                                                         (void*)Ping_Sender_Release    },
    { "nativeStart",      "()Z",                                                                         (void*)Ping_Sender_Start      },
    { "nativeStop",       "(I)Z",                                                                        (void*)Ping_Sender_Stop       },
    { "nativeCheckReply", "()Z",                                                                         (void*)Ping_Sender_CheckReply },
};

int PingSender::SetUp() {
    JNIEnv *env = JNIUtil::GetEnv();
    assert(env);

    ScopedLocalRef<jclass> holder(env, env->FindClass(CLASS_NAME));
    if (auto klass = holder.get()) {
        FID_INSTANCE = env->GetFieldID(klass, "mNativeInstance", "J");
        env->RegisterNatives(klass, METHOD_TABLE, NUMBER_OF(METHOD_TABLE));
    }

    return 0;
}

int PingSender::TearDown() {
    JNIEnv *env = JNIUtil::GetEnv();
    assert(env);

    ScopedLocalRef<jclass> holder(env, env->FindClass(CLASS_NAME));
    if (auto klass = holder.get()) {
        env->UnregisterNatives(klass);
    }

    return 0;
}

// ---------- class implementation ----------

PingSender::PingSender()
  : pingListener_(nullptr),
    stop_flag_(false),
    thread_running_(false),
    sock_(-1),
    interval_msec_(0),
    msg_buf_(nullptr),
    data_size_(0),
    seq_no_(0),
    got_reply_(false) {
}

bool PingSender::Configure(const char *dst_addr, unsigned int interval_msec, jobject listener, size_t data_size) {
    if (dst_addr == nullptr) {
        LOGW("ICMP invalid dest address");
        return false;
    }

    saddr_.sin_len = sizeof(saddr_);
    saddr_.sin_family = AF_INET;
    int ret = lwip_inet_aton(dst_addr, &saddr_.sin_addr);
    if (ret == 0) {
        LOGW("ICMP invalid dest address: %s", dst_addr);
        return false;
    }

    if (data_size > 65507) { // 65535 - IP header (20 bytes) - ICMP header (8 bytes)
        LOGI("ICMP data size too big, changed to 65507 bytes");
        data_size = 65507;
    }
    data_size_ = data_size;

    if (!allocateMessage()) {
        return false;
    }

    interval_msec_ = interval_msec;
    got_reply_ = false;

    if (listener) {
        JNIEnv *env = JNIUtil::GetEnv();
        assert(env);
        ScopedLocalRef<jclass> holder(env, env->GetObjectClass(listener));
        onReplyMID_ = env->GetMethodID(holder.get(), "onReply", "()V");
        onTimeoutMID_ = env->GetMethodID(holder.get(), "onTimeout", "()V");

        if (pingListener_) {
            env->DeleteGlobalRef(pingListener_);
            pingListener_ = nullptr;
        }
        pingListener_ = env->NewGlobalRef(listener);
    }

    return true;
}

PingSender::~PingSender() {
    LOGD("PingSender destructor");
    Stop(BACKUP_STOP_TIMEOUT_MSEC); // in case Stop() has not been called yet
    freeMessage();

    if (pingListener_) {
        JNIUtil::DeleteGlobalRef(pingListener_);
    }
}

bool PingSender::Start() {
    if (msg_buf_ == nullptr) {
        LOGW("ICMP please call Configure() first.");
        return false;
    }

    if (thread_.joinable()) {
        // thread is already running
        LOGW("PingSender is already started.");
        return false;
    }

    stop_flag_ = false;
    thread_running_ = true;
    thread_ = std::thread([this]() {
        thread_loop();
    });

    return true;
}

bool PingSender::Stop(int timeout_msec) {
    LOGD("PingSender::Stop() called");

    if (thread_.joinable()) {
        stop_flag_ = true;

        {
            std::unique_lock<std::mutex> lk(stop_mutex_);
            while (thread_running_) {
                if (timeout_msec >= 0) {
                    std::cv_status ret = stop_cv_.wait_for(lk, std::chrono::milliseconds(timeout_msec));
                    if (ret == std::cv_status::timeout) {
                        LOGI("PingSender::Stop() timed out (%d msec)", timeout_msec);
                        return false;
                    }
                } else {
                    stop_cv_.wait(lk);
                }
            }
        }

        thread_.join();
    }

    return true;
}

bool PingSender::CheckReply() {
    return got_reply_;
}

bool PingSender::openSocket() {
    if (sock_ >= 0) {
        LOGW("ICMP socket already created");
        return false;
    }

    sock_ = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (sock_ < 0) {
        LOGE("ICMP socket creation failed, errno=%d", errno);
        return false;
    }

    return true;
}

void PingSender::closeSocket() {
    if (sock_ >= 0) {
        lwip_close(sock_);
        sock_ = -1;
    }
}

bool PingSender::allocateMessage() {
    size_t msg_size = sizeof(struct icmp_echo_hdr) + data_size_;

    freeMessage();

    msg_buf_ = new unsigned char[msg_size];
    if (msg_buf_ == nullptr) {
        LOGE("ICMP message allocation failed");
        return false;
    }

    struct icmp_echo_hdr *header = reinterpret_cast<icmp_echo_hdr*>(msg_buf_);

    ICMPH_TYPE_SET(header, ICMP_ECHO);  // type = 8
    ICMPH_CODE_SET(header, 0);          // code = 0
    header->chksum = 0;                 // to be filled later
    header->id = PING_ID;               // ID: any 16-bit number
    header->seqno = 0;                  // will be set later

    // put some values in the data area
    unsigned char *data = msg_buf_ + sizeof(struct icmp_echo_hdr);
    for (unsigned int i = 0; i < data_size_; i++) {
        data[i] = i % 256;
    }

    return true;
}

void PingSender::freeMessage() {
    if (msg_buf_ != nullptr) {
        delete[] msg_buf_;
        msg_buf_ = nullptr;
    }
}

void PingSender::thread_loop() {
    LOGD("ICMP thread started");

    if (!openSocket()) {
        goto end;
    }

    enum {
        STATE_SEND = 0,
        STATE_RECEIVE,
    } state;
    struct timespec sent_time;
    bool ret;

    state = STATE_SEND;
    while (!stop_flag_) {
        if (state == STATE_SEND) {
            ret = send();
            if (!ret) {
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &sent_time);
            state = STATE_RECEIVE;

        } else {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int timeout_msec = getDeltaMsec(now, sent_time);
            if (timeout_msec >= interval_msec_) {
                // time to send next request
                state = STATE_SEND;
                onTimeout();
                continue;
            }

            ret = receive(interval_msec_ - timeout_msec);
            if (!ret) {
                break;
            }
        }
    }

    closeSocket();

end:
    {
        std::lock_guard<std::mutex> lk(stop_mutex_);
        thread_running_ = false;
    }
    stop_cv_.notify_all();

    LOGD("ICMP thread stopped");
}

bool PingSender::send() {
    if (sock_ < 0 || msg_buf_ == nullptr) {
        return false;
    }

    LOGD("ICMP send request");

    struct icmp_echo_hdr *header = reinterpret_cast<icmp_echo_hdr*>(msg_buf_);
    // set sequence number
    header->seqno = htons(seq_no_);
    seq_no_++;

    // calculate checksum
    header->chksum = 0; // clear the value before calculation
    header->chksum = inet_chksum(msg_buf_, sizeof(struct icmp_echo_hdr) + data_size_);

    size_t msg_size = sizeof(struct icmp_echo_hdr) + data_size_;

    int ret = lwip_sendto(sock_, msg_buf_, msg_size, 0,
                           reinterpret_cast<struct lwip_sockaddr *>(&saddr_), sizeof(saddr_));
    if (ret < 0) {
        LOGW("ICMP sendto failed");
        return false;
    } else if (static_cast<size_t>(ret) != msg_size) {
        // I'm not sure if this happens, but let's not make it an error
        LOGI("ICMP sendto imcomplete message");
    }

    return true;
}

bool PingSender::receive(int timeout_msec) {
    if (sock_ < 0) {
        return false;
    }

    LOGD("ICMP receive response with timeout %d msec", timeout_msec);

    if (timeout_msec >= 0) {
        lwip_setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &timeout_msec, sizeof(timeout_msec));
    }

    char recv_buf[sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr) + data_size_];

#ifdef DEBUG_PRINT_ICMP_RESPONSE
    // for debug output
    struct lwip_sockaddr_in addr;
    lwip_socklen_t addrlen;
    int ret = lwip_recvfrom(sock_, recv_buf, sizeof(recv_buf), 0, reinterpret_cast<struct lwip_sockaddr *>(&addr), &addrlen);
#else
    int ret = lwip_recvfrom(sock_, recv_buf, sizeof(recv_buf), 0, nullptr, nullptr);
#endif
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // timeout, so not error
            return true;
        } else {
            LOGW("ICMP recvfrom failed, errno=%d", errno);
            return false;
        }
    }

    got_reply_ = true;
    onReply();

#ifdef DEBUG_PRINT_ICMP_RESPONSE
    if ((size_t)ret >= sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr)) {
        struct ip_hdr *ip_header = reinterpret_cast<struct ip_hdr *>(recv_buf);
        struct icmp_echo_hdr *icmp_header = reinterpret_cast<icmp_echo_hdr *>(recv_buf + IPH_HL(ip_header) * 4);

        LOGW("ICMP got response from %s: ID=%d, seq=%d",
             lwip_inet_ntoa(addr.sin_addr), icmp_header->id, ntohs(icmp_header->seqno));
    }
#else
    /* we don't use received reply, so nothing to do */
#endif

    return true;
}

void PingSender::onReply() {
    if (pingListener_) {
        bool needDetach = false;
        JNIEnv *env = JNIUtil::GetAttachedEnv(&needDetach);
        assert(env);
        env->CallVoidMethod(pingListener_, onReplyMID_);
        if (needDetach) {
            JNIUtil::DetachEnv();
        }
    }
}

void PingSender::onTimeout() {
    if (pingListener_) {
        bool needDetach = false;
        JNIEnv *env = JNIUtil::GetAttachedEnv(&needDetach);
        assert(env);
        env->CallVoidMethod(pingListener_, onTimeoutMID_);
        if (needDetach) {
            JNIUtil::DetachEnv();
        }
    }
}

// static
int PingSender::getDeltaMsec(struct timespec& to, struct timespec& from) {
    time_t sec = to.tv_sec - from.tv_sec;
    long nsec = to.tv_nsec - from.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000;
    }

    return sec * 1000 + nsec / 1000000;
}
