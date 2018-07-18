//
// NetconnSocket.h
// UIE MultiAccess
//
// Created by Rakuto Furutani on 1/8/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#pragma once

#include <jni.h>
#include <netinet/in.h>
#include <lwip/api.h>
#include <lwip/sockets.h>
#include <mutex>
#include <memory>
#include <string>
#include <atomic>
#include <condition_variable>
#include <openssl/ssl.h>
#include <openssl/pkcs12.h>
#include <openssl/err.h>
#include <semaphore.hpp>
#include "NetbufWrapper.hpp"
#include "SSLStateMachine.h"

// See NetconnSocket::Protocol in NetconnSocket.java
static const int PROTO_TCP = 0;
static const int PROTO_UDP = 1;

// See NetconnSocket::SSLSide
static const int SSL_SIDE_SERVER = 0;
static const int SSL_SIDE_CLIENT = 1;

class NetconnSocket {
public:
    static int SetUp();
    static int TearDown();

    // SSL I/O error code mapping
    static const int ERR_SSL_NONE             = 0;
    static const int ERR_SSL_SSL              = -101;
    static const int ERR_SSL_WANT_READ        = -102;
    static const int ERR_SSL_WANT_WRITE       = -103;
    static const int ERR_SSL_WANT_X509_LOOKUP = -104;
    static const int ERR_SSL_SYSCALL          = -105;
    static const int ERR_SSL_ZERO_RETURN      = -106;
    static const int ERR_SSL_WANT_CONNECT     = -107;
    static const int ERR_SSL_WANT_ACCEPT      = -108;

    // JNI methods
    JNIEXPORT static void JNICALL SetOwner(JNIEnv*, jobject);
    JNIEXPORT static jint JNICALL New(JNIEnv*, jobject, jint);
    JNIEXPORT static jint JNICALL Release(JNIEnv*, jobject);
    JNIEXPORT static jint JNICALL Bind(JNIEnv*, jobject, jobject, jint);
    JNIEXPORT static jint JNICALL Listen(JNIEnv*, jobject, jint);
    JNIEXPORT static jlong JNICALL Accept(JNIEnv*, jobject, jint, jint);
    JNIEXPORT static jint JNICALL Send(JNIEnv*, jobject, jobject, jint, jint);
    JNIEXPORT static jint JNICALL Recv(JNIEnv*, jobject, jobject, jint);
    JNIEXPORT static jint JNICALL Connect(JNIEnv*, jobject, jobject, jint, jint);
    JNIEXPORT static jint JNICALL Close(JNIEnv*, jobject);
    JNIEXPORT static jobject JNICALL GetLocalSocketAddress(JNIEnv*, jobject);
    JNIEXPORT static jobject JNICALL GetRemoteSocketAddress(JNIEnv*, jobject);
    JNIEXPORT static void JNICALL SetReuseAddress(JNIEnv*, jobject, jboolean);
    JNIEXPORT static void JNICALL ResetStack(JNIEnv*);

    // TLS
    JNIEXPORT static jint JNICALL SetCertificate(JNIEnv *, jobject, jobject, jint, jstring, jboolean);

    // obsolute
    JNIEXPORT static jint JNICALL SetNonblocking(JNIEnv*, jobject, jboolean);
    JNIEXPORT static jint JNICALL SetSendBufferSize(JNIEnv*, jobject, jint);
    JNIEXPORT static jint JNICALL SetRecvBufferSize(JNIEnv*, jobject, jint);
    JNIEXPORT static jint JNICALL GetRecvBufferSize(JNIEnv*, jobject);

private:
    using NetconnHandle = std::unique_ptr<struct netconn, void(*)(struct netconn*)>;
    using Lock          = std::lock_guard<std::mutex>;
    using SSLStateMachineHandle = std::unique_ptr<SSLStateMachine>;

    std::mutex recv_event_mutex_;
    std::mutex conn_mutex_;
    std::mutex ssl_mutex_;
    std::mutex send_mutex_; // only for send()
    std::condition_variable recv_cond_;
    volatile int recvevent_; // Seems that std::atomic_int implementation seems to be broken
    bool is_server_; // Indicates server connection or not

    JavaObjectHolder owner_;
    NetconnHandle conn_;
    enum netconn_type conntype_;
    NetbufWrapper recvbuf_;
    std::atomic_bool closing_;
    std::atomic_bool recv_closed_;
    std::atomic_bool error_occurred_;

    SSLStateMachineHandle ssl_;

    NetconnSocket();
    NetconnSocket(struct netconn*);
    NetconnSocket(jobject, jint);
    ~NetconnSocket();
    int InitSsl(SSL_CTX *ctx);

    // Java Methods
    void setOwner(JNIEnv*, jobject);
    int bind(const struct ip_addr&, u16_t);
    int listen(int);
    NetconnSocket* accept(int, int);
    int connect(const struct ip_addr&, u16_t, int);
    int send(const void *, size_t);
    int recv(void *, size_t, int);
    int close();

    int setCertificate(const void*, size_t, const char *, bool);

    // TLS
    inline bool isSSL() { return ssl_.get(); }
    int sslRead(void *, size_t, int);
    int drainDecryptedDataLocked(void *, size_t);
    int sslWrite(const void *, size_t);
    bool performHandshake(int);

    NetconnHandle createNetconn(enum netconn_type);
    void onNetconnEvent(enum netconn_evt, u16_t);

    static int calcDeltaTimeMsec(struct timespec *now, struct timespec *base);

    inline jint getSendTimeout() {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            return netconn_get_sendtimeout(conn);
        }
        return ERR_CLSD;
    }

    inline jint setSendTimeout(jint timeout) {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            netconn_set_sendtimeout(conn, timeout);
            return 0;
        }
        return ERR_CLSD;
    }

    inline jint getRecvTimeout() {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            return netconn_get_recvtimeout(conn);
        }
        return ERR_CLSD;
    }

    inline jint setRecvTimeout(jint timeout) {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            netconn_set_recvtimeout(conn, timeout);
            return 0;
        }
        return ERR_CLSD;
    }

    inline jint setNonblocking(jboolean on) {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            netconn_set_nonblocking(conn, on);
            return 0;
        }
        return ERR_CLSD;
    }

    inline jint getRecvBufSize() {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            return netconn_get_recvbufsize(conn);
        }
        return ERR_CLSD;
    }

    inline jint setRecvBufSize(jint size) {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            netconn_set_recvbufsize(conn, size);
            return 0;
        }
        return ERR_CLSD;
    }

    inline int getLocalSocketAddress(ip_addr_t *addr, u16_t *port) {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            netconn_addr(conn, addr, port);
            return 0;
        }
        return ERR_CLSD;
    }
    inline int getRemoteSocketAddress(ip_addr_t *addr, u16_t *port) {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            netconn_peer(conn, addr, port);
            return 0;
        }
        return ERR_CLSD;
    }
};
