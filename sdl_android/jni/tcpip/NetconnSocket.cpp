//
// NetconnSocket.cpp
// UIE MultiAccess
//
// Created by Rakuto Furutani on 1/8/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cassert>
#include <lwip/tcp.h>
#include <lwip/tcpip.h>
#include <lwip/inet.h>
#include <lwip/tcp.h>
#include <lwip/inet.h>
#include <lwip/tcp_impl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "logger.h"
#include "JNIUtil.h"
#include "ScopedLocalRef.h"
#include "NetconnSocket.h"

static const char* const CLASS_NAME = "com/smartdevicelink/xevo/transport/NetconnSocket";

#ifndef NUMBER_OF
#define NUMBER_OF(array) (sizeof(array) / sizeof(array[0]))
#endif

#ifndef MIN
#define MIN(a,b) ((a>b)? b : a)
#endif

static jfieldID FID_INSTANCE;

static JNINativeMethod METHOD_TABLE[] = {
    {
        "nativeSetOwner",
        "()V",
        (void *)NetconnSocket::SetOwner
    },
    {
        "nativeNew",
        "(I)I",
        (void *)NetconnSocket::New
    },
    {
        "nativeRelease",
        "()I",
        (void *)NetconnSocket::Release
    },
    {
        "nativeBind",
        "(Ljava/net/InetAddress;I)I",
        (void *)NetconnSocket::Bind
    },
    {
        "nativeListen",
        "(I)I",
        (void *)NetconnSocket::Listen
    },
    {
        "nativeAccept",
        "(II)J",
        (void *)NetconnSocket::Accept
    },
    {
        "nativeSend",
        "(Ljava/nio/ByteBuffer;II)I",
        (void *)NetconnSocket::Send
    },
    {
        "nativeRecv",
        "(Ljava/nio/ByteBuffer;I)I",
        (void *)NetconnSocket::Recv
    },
    {
        "nativeConnect",
        "(Ljava/net/InetAddress;II)I",
        (void *)NetconnSocket::Connect
    },
    {
        "nativeClose",
        "()I",
        (void *)NetconnSocket::Close
    },
    {
        "nativeGetLocalSocketAddress",
        "()Ljava/net/SocketAddress;",
        (void *)NetconnSocket::GetLocalSocketAddress
    },
    {
        "nativeGetRemoteSocketAddress",
        "()Ljava/net/SocketAddress;",
        (void *)NetconnSocket::GetRemoteSocketAddress
    },
    {
        "nativeSetReuseAddress",
        "(Z)V",
        (void *)NetconnSocket::SetReuseAddress
    },
    {
        "nativeSetCertificate",
        "(Ljava/nio/ByteBuffer;ILjava/lang/String;Z)I",
        (void *)NetconnSocket::SetCertificate
    },
    {
        "nativeResetStack",
        "()V",
        (void *)NetconnSocket::ResetStack
    }
#if 0
    {
        "nativeSetSendBufferSize",
        "(I)I",
        (void *)NetconnSocket::SetSendBufferSize
    },
    {
        "nativeSetNonblocking",
        "(Z)I",
        (void *)NetconnSocket::SetNonblocking
    },
    {
        "nativeSetRecvBufferSize",
        "(I)I",
        (void *)NetconnSocket::SetRecvBufferSize
    },
    {
        "nativeGetRecvBufferSize",
        "()I",
        (void *)NetconnSocket::GetRecvBufferSize
    },
#endif
};


int NetconnSocket::SetUp() {
    JNIEnv *env = JNIUtil::GetEnv();
    assert(env);

    ScopedLocalRef<jclass> holder(env, env->FindClass(CLASS_NAME));
    if (auto klass = holder.get()) {
        FID_INSTANCE = env->GetFieldID(klass, "mNativeInstance", "J");
        env->RegisterNatives(klass, METHOD_TABLE, NUMBER_OF(METHOD_TABLE));
    }

    return 0;
}

int NetconnSocket::TearDown() {
    JNIEnv *env = JNIUtil::GetEnv();
    assert(env);

    ScopedLocalRef<jclass> holder(env, env->FindClass(CLASS_NAME));
    if (auto klass = holder.get()) {
        env->UnregisterNatives(klass);
    }

    return 0;
}

void NetconnSocket::SetOwner(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        thiz->setOwner(env, obj);
    }
}

jint NetconnSocket::New(JNIEnv *env, jobject obj, jint protocol) {
    auto thiz = new NetconnSocket(obj, protocol);
    if (!thiz) {
        JNIUtil::ThrowOutOfMemoryException("NetconnSocket");
        return -1;
    }
    env->SetLongField(obj, FID_INSTANCE, reinterpret_cast<jlong>(thiz));
    return 0;
}

jint NetconnSocket::Release(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (!thiz)
        return -1;

    env->SetLongField(obj, FID_INSTANCE, 0);
    delete thiz;

    return 0;
}

jint NetconnSocket::Bind(JNIEnv *env, jobject obj, jobject addr, jint port) {
    auto thiz = env->GetLongField(obj, FID_INSTANCE);
    if (thiz) {
        struct ip_addr address = {JNIUtil::GetIPv4Address(addr)};
        return reinterpret_cast<NetconnSocket *>(thiz)->bind(address, port);
    }
    return -1;
}

jint NetconnSocket::Listen(JNIEnv *env, jobject obj, jint backlog) {
    auto thiz = env->GetLongField(obj, FID_INSTANCE);
    if (thiz) {
        return reinterpret_cast<NetconnSocket *>(thiz)->listen(backlog);
    }
    return -1;
}

jlong NetconnSocket::Accept(JNIEnv *env, jobject obj, jint timeout, jint sslHandshakeTimeout) {
    auto thiz = env->GetLongField(obj, FID_INSTANCE);
    if (thiz) {
        return reinterpret_cast<jlong>(reinterpret_cast<NetconnSocket *>(thiz)->accept(timeout, sslHandshakeTimeout));
    }
    return 0;
}

jint NetconnSocket::Send(JNIEnv *env, jobject obj, jobject buffer, jint offset, jint length) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        auto p = reinterpret_cast<char *>(env->GetDirectBufferAddress(buffer));
        return (thiz->isSSL())? thiz->sslWrite(p + offset, length) : thiz->send(p + offset, length);
    }
    return -1;
}

jint NetconnSocket::Recv(JNIEnv *env, jobject obj, jobject buffer, jint timeout) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        auto bufptr = env->GetDirectBufferAddress(buffer);
        auto maxlen = env->GetDirectBufferCapacity(buffer);
        return (thiz->isSSL())? thiz->sslRead(bufptr, maxlen, timeout) : thiz->recv(bufptr, maxlen, timeout);
    }
    return -1;
}

jint NetconnSocket::Connect(JNIEnv *env, jobject obj, jobject addr, jint port, jint timeoutMs) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        struct ip_addr address = {JNIUtil::GetIPv4Address(addr)};
        return thiz->connect(address, port, timeoutMs);
    }
    return -1;
}

jint NetconnSocket::Close(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        return thiz->close();
    }
    return -1;
}

jint NetconnSocket::SetNonblocking(JNIEnv *env, jobject obj, jboolean nonblock) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        return thiz->setNonblocking(nonblock);
    }
    return -1;
}

jint NetconnSocket::SetSendBufferSize(JNIEnv *env, jobject obj, int size) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        // Netconn has not API for that purpose
        return 0;
    }
    return -1;
}

jint NetconnSocket::SetRecvBufferSize(JNIEnv *env, jobject obj, int size) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        return thiz->setRecvBufSize(size);
    }
    return -1;
}

jint NetconnSocket::GetRecvBufferSize(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        return thiz->getRecvBufSize();
    }
    return -1;
}

jobject NetconnSocket::GetLocalSocketAddress(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        ip_addr_t addr;
        u16_t port;
        if (thiz->getLocalSocketAddress(&addr, &port) < 0) {
            return nullptr;
        }
        struct in_addr inaddr = {addr.addr};
        auto klass = JNIUtil::inetSocketAddressClass;
        auto host = env->NewStringUTF(inet_ntoa(inaddr));
        return env->NewObject(klass, JNIUtil::inetSocketAddressInitMethod, host, port);
    }
    return nullptr;
}

jobject NetconnSocket::GetRemoteSocketAddress(JNIEnv *env, jobject obj) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        ip_addr_t addr;
        u16_t port;
        if (thiz->getRemoteSocketAddress(&addr, &port) < 0) {
            return nullptr;
        }
        ScopedLocalRef<jclass> holder(env, env->FindClass("java/net/InetSocketAddress"));
        if (auto klass = holder.get()) {
            struct in_addr inaddr = {addr.addr};
            auto initMID = env->GetMethodID(holder.get(), "<init>", "(Ljava/lang/String;I)V");
            auto szHost = inet_ntoa(inaddr);
            auto host = env->NewStringUTF(szHost);
            return env->NewObject(klass, initMID, host, port);
        }
    }
    return nullptr;
}

void NetconnSocket::SetReuseAddress(JNIEnv *env, jobject obj, jboolean reuse) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        if (auto conn = thiz->conn_.get()) {
            netconn_set_reuseaddr(conn, reuse);
        }
    }
}

jint NetconnSocket::SetCertificate(JNIEnv *env, jobject obj, jobject pkcs12Buf, jint length, jstring password, jboolean is_server) {
    auto thiz = reinterpret_cast<NetconnSocket *>(env->GetLongField(obj, FID_INSTANCE));
    if (thiz) {
        auto pkcs12 = reinterpret_cast<char *>(env->GetDirectBufferAddress(pkcs12Buf));
        auto passwd = JNIUtil::GetString(password);
        return thiz->setCertificate(pkcs12, length, passwd.c_str(), is_server);
    }
    return ERR_VAL;
}

void NetconnSocket::ResetStack(JNIEnv *env) {
    tcpip_callback([](void *ctx){
        LOGI("Clear out active PCBs");
        err_t err;
        struct tcp_pcb *pcb;
        for (pcb = tcp_active_pcbs; pcb != nullptr; pcb = pcb->next) {
            LOGI("tcp_close(%p)", pcb);
            err = tcp_close(pcb);
            if (err) {
                LOGW("tcp_close failed: %s", lwip_strerr(err));
            }
        }
    }, nullptr);
}

// --------------------------------------------------------------------------
// public methods
// --------------------------------------------------------------------------

NetconnSocket::NetconnSocket()
: recvevent_(0),
  is_server_(false),
  owner_(nullptr),
  conn_(nullptr, nullptr),
  conntype_(NETCONN_INVALID),
  closing_(false),
  recv_closed_(false),
  error_occurred_(false),
  ssl_(nullptr) {
}

NetconnSocket::NetconnSocket(struct netconn *conn)
: NetconnSocket() {
    assert(conn);

    conn->data = this;
    conn_ = NetconnHandle(conn, [](struct netconn *conn) {
        if (conn)
            netconn_delete(conn);
    });
    conntype_ = conn->type;
}

NetconnSocket::NetconnSocket(jobject owner, jint proto)
: NetconnSocket() {
    LOGV("New NetconnSocket(%p)", this);

    // Create netconn instance
    if (proto == PROTO_TCP) {
        conn_ = createNetconn(NETCONN_TCP);
        conntype_ = NETCONN_TCP;
    } else if (proto == PROTO_UDP) {
        conn_ = createNetconn(NETCONN_UDP);
        conntype_ = NETCONN_UDP;
    } else {
        assert("unknown protocol specified");
    }
}

NetconnSocket::~NetconnSocket() {
    if (auto conn = conn_.get()) {
        conn->data = nullptr;
    }
    LOGV("Dealloc NetconnSocket(%p)", this);
    close();
}

int NetconnSocket::InitSsl(SSL_CTX *ctx) {
    assert(ctx);
    ssl_ = SSLStateMachineHandle(new SSLStateMachine(ctx));
    return ssl_->IsValid() ? 0 : ERR_SSL_SSL;
}

NetconnSocket::NetconnHandle NetconnSocket::createNetconn(enum netconn_type type) {
    auto conn = netconn_new_with_callback(type, [](struct netconn *conn, enum netconn_evt evt, u16_t len) {
        LOGV("netconn_callback:conn->data(%p), conn(%p)", conn->data, conn);
        if (conn->data) {
            reinterpret_cast<decltype(this)>(conn->data)->onNetconnEvent(evt, len);
        }
    });
    if (!conn) {
        JNIUtil::ThrowOutOfMemoryException("TCP socket");
        return NetconnHandle(nullptr, nullptr);
    }
    conn->data = this;
    LOGV("createNetconn:conn->data(%p), conn(%p)", conn->data, conn);
    return NetconnHandle(conn, [](struct netconn *conn) {
        if (conn)
            netconn_delete(conn);
    });
}
void NetconnSocket::setOwner(JNIEnv *env, jobject owner) {
    assert(env);
    assert(owner);

    owner_.set(owner);
}

int NetconnSocket::bind(const struct ip_addr &addr, u16_t port) {
    LOGV("NetconnSocket::bind(0x%x:%d)", addr.addr, port);

    Lock lock(conn_mutex_);
    if (auto conn = conn_.get()) {
        err_t err = netconn_bind(conn, const_cast<struct ip_addr *>(&addr), port);
        if (err) {
            LOGE("bind failed: %s", lwip_strerr(err));
            return err;
        }
        return 0;
    }
    return ERR_VAL;
}

int NetconnSocket::listen(int backlog) {
    LOGV("NetconnSocket::listen(%d)", backlog);

    Lock lock(conn_mutex_);
    if (auto conn = conn_.get()) {
        err_t err = netconn_listen_with_backlog(conn, backlog);
        if (err) {
            LOGE("listen failed: %s", lwip_strerr(err));
            return err;
        }
    }
    return 0;
}

NetconnSocket* NetconnSocket::accept(int timeout, int sslHandshakeTimeout) {
    LOGV("NetconnSocket::accept(timeout=%d, hsTimeout=%d)", timeout, sslHandshakeTimeout);

    Lock lk(conn_mutex_);
    if (auto conn = conn_.get()) {
        is_server_ = true;
        struct netconn *new_conn;
        if (timeout > 0) {
            netconn_set_recvtimeout(conn, timeout);
        } else {
            // netconn_accept is not designed to allow interrupt from another thread.
            // To support closing socket from another thread, set timeout and iterate accept loop.
            netconn_set_recvtimeout(conn, 1000);
        }
        if (sslHandshakeTimeout <= 0) {
            sslHandshakeTimeout = 5000;
        }
        while (!closing_) {
            err_t err = netconn_accept(conn, &new_conn);
            if (err) {
                if (err == ERR_TIMEOUT) {
                    if (closing_) {
                        // if close() is called during netconn_accept(), throw IOException instead of
                        // returning nullptr
                        break;
                    } else {
                        if (timeout > 0) {
                            // timeout
                            return nullptr;
                        } else {
                            // retry
                            continue;
                        }
                    }
                } else {
                    LOGE("accept failed: %s", lwip_strerr(err));
                    // throw exception
                    break;
                }
            }

            // after close() is called, don't accept any more connections
            if (closing_) {
                if (new_conn) {
                    LOGV("Discarding a new connection after close");
                    err = netconn_close(new_conn);
                    if (err) {
                        LOGE("close failed: %s", lwip_strerr(err));
                    }
                }
                break;
            }

            if (new_conn) {
                // Perform SSL handshake
                LOGV("NetconnSocket::accept new_conn(%p)", new_conn);
                if (isSSL()) {
                    Lock lk(ssl_mutex_);
                    auto sock = new NetconnSocket(new_conn);
                    if (sock->InitSsl(ssl_->context()) != 0) {
                        LOGE("Initialize SSL failed");
                        delete sock;
                        return nullptr;
                    }
                    if (!sock->performHandshake(sslHandshakeTimeout)) {
                        delete sock;
                        return nullptr;
                    }
                    return sock;
                } else {
                    return new NetconnSocket(new_conn);
                }
            }
        }
    }

    // Socket is closed by another thread
    JNIUtil::ThrowIOException("connection closed");
    return nullptr;
}

int NetconnSocket::connect(const struct ip_addr &addr, u16_t port, int timeoutMs) {
#if defined(ANDROID_DEBUG_LOG) && defined(ANDROID_VERBOSE_LOG)
    {
        struct in_addr address = {addr.addr};
        LOGV("NetconnSocket::connect(%s:%d)", inet_ntoa(address), port);
    }
#endif
    {
        Lock lk(conn_mutex_);
        is_server_ = false;
        if (auto conn = conn_.get()) {
            // TODO: Timeout support
            auto err = netconn_connect(conn, const_cast<ip_addr *>(&addr), port);
            if (err) {
                LOGE("connect failed: %s", lwip_strerr(err));
                return err;
            }
        } else {
            return ERR_CLSD;
        }
    }

    // Perform SSL handshake
    if (isSSL()) {
        LOGI("Perform SSL Handshake");
        int err, ret;
        uint8_t buffer[4096];
        while (!ssl_->IsHandshakeComplete()) {
            if (ssl_->IsWritePending()) {
                ret = ssl_->WriteExtract(buffer, sizeof(buffer));
                if (ret < 0)
                    return ERR_SSL_SSL;

                err = this->send(buffer, ret);
                if (err) {
                    LOGW("send failed during SSL handshake: %s", lwip_strerr(err));
                    return err;
                }
            }

            ret = ssl_->ReadExtract(buffer, sizeof(buffer));
            if (ret == 0) {
                // Need to continue SSL handshake iteration on timeout
                ret = this->recv(buffer, sizeof(buffer), 50);
                if (ret <= 0) {
                    if (ret == ERR_TIMEOUT) {
                        continue;
                    } else if (ret < 0) {
                        LOGW("recv failed during SSL handshake: %s", lwip_strerr(ret));
                    }
                    return err;
                }
                ssl_->ReadInject(buffer, ret);
            }
        }
    }
    return 0;
}

int NetconnSocket::send(const void *data, size_t length) {
    // LOGV("NetconnSocket::send(%zu bytes)", length);

    auto p = reinterpret_cast<const char *>(data);
    err_t err;
    if (conntype_ == NETCONN_TCP) {
        // avoid multiple threads calling send() and data are mixed up
        Lock lock(send_mutex_);

        while (length > 0) {
            if (closing_) {
                LOGW("Stop sending since socket is now closing");
                return ERR_CLSD;
            }

            {
                err = 0;
                Lock lk(conn_mutex_);
                if (auto conn = conn_.get()) {
                    size_t writesz = 0;
                    // Set nonblocking rather than timeout because LwIP checks timeout at 2-second intervals.
                    auto isNonblocking = netconn_is_nonblocking(conn);
                    netconn_set_nonblocking(conn, 1);
                    err = netconn_write_partly(conn, p, length, NETCONN_COPY, &writesz);
                    netconn_set_nonblocking(conn, isNonblocking);
                    if (err) {
                        // Due to a known issue of LwIP, netconn_write_partly() may return ERR_MEM if we
                        // write many chunk in a short time. This is not an out-of-memory error but is
                        // simply an overflow in LwIP's send queue, so we can retry after a while.
                        // reference: http://savannah.nongnu.org/bugs/?46289
                        // I think OOM errors seldom occur in a Android phone. So here we treat all
                        // ERR_MEM errors as send queue overflow.
                        if (err != ERR_WOULDBLOCK && err != ERR_MEM) {
                            LOGE("netconn_write failed: %s (%d)", lwip_strerr(err), err);
                            return err;
                        }
                    } else {
                        length -= writesz;
                        p += writesz;
                    }
                } else {
                    return ERR_CLSD;
                }
            }

            if (err == ERR_WOULDBLOCK || err == ERR_MEM) {
                // Avoid busy loop. Do this outside of conn_mutex_ lock.
                struct timespec ts = {0, 10000000}; // 10 msec
                nanosleep(&ts, nullptr);
            }
        }
        return 0;
    } else if (conntype_ == NETCONN_UDP) {
        Lock lk(conn_mutex_);
        if (auto conn = conn_.get()) {
            auto buf = netbuf_new();
            if (!buf) {
                JNIUtil::ThrowOutOfMemoryException("UDP packet");
                return ERR_MEM;
            }
            err = netbuf_ref(buf, p, length); // only return ERR_MEM or ERR_OK
            if (err) {
                netbuf_delete(buf);
                JNIUtil::ThrowOutOfMemoryException("UDP packet");
                return err;
            }
            err = netconn_send(conn, buf);
            if (err) {
                LOGE("netconn_send failed: %s", lwip_strerr(err));
                netbuf_delete(buf);
                return err;
            }
            netbuf_delete(buf);
            return 0;
        } else {
            return ERR_CLSD;
        }
    } else {
        return ERR_ARG;
    }
}

int NetconnSocket::recv(void *buffer, size_t length, int timeoutMs) {
    LOGV("NetconnSocket::recv(%p, %zu, %d, %d)", buffer, length, timeoutMs, recvevent_);

    // Frist off check that data exists in the RCVBUF.
    if (recvbuf_.size() > 0) {
        return recvbuf_.take(buffer, length);
    }

    // Note that Netconn API family cannot be accessed from another thread at the same time.
    // netconn_recv cannot be interrupted by netconn_close.
    if (auto conn = conn_.get()) {
        // VIPER-1182
        // Need to chekc recv_avail property because this connection possibly receives packet
        // before onNetconnEvent callback is set on constructor.
        if (conn->recv_avail == 0) {
            std::unique_lock<decltype(recv_event_mutex_)> lk(recv_event_mutex_);
            if (recvevent_ == 0) {
                auto pred = [this]{ return (recvevent_ > 0 || closing_ || recv_closed_ || error_occurred_); };
                LOGV("cond_wait(%d ms)", timeoutMs);
                if (timeoutMs <= 0) {
                    recv_cond_.wait(lk, pred);
                } else if (!recv_cond_.wait_for(lk, std::chrono::milliseconds(timeoutMs), pred)) {
                    return ERR_TIMEOUT;
                }

                if (closing_ || recv_closed_ || error_occurred_) {
                    return ERR_CLSD;
                }
            }
        }
    }

    // Note that don't call setXXX or getXXX inline function because it causes unexpected behavior
    // due to locking same non-recursive mutex twice on the same thread.
    Lock lk(conn_mutex_);
    if (auto conn = conn_.get()) {
        int original_timeout = netconn_get_recvtimeout(conn);
        if (timeoutMs > 0) {
            netconn_set_recvtimeout(conn, timeoutMs);
        }
        auto reset_timeout = [conn](int t) {
            netconn_set_recvtimeout(conn,  t);
        };

        struct netbuf *buf = nullptr;
        err_t err = netconn_recv(conn, &buf);
        if (err < 0) {
            if (err != ERR_TIMEOUT) {
                LOGW("NetconnSocket::recv failed: %s, %d", lwip_strerr(err), err);
            }
            if (timeoutMs > 0) {
                reset_timeout(original_timeout);
            }
            return err;
        }
        recvbuf_.set(buf);

        // Note that timeout is only applied on only one recv() call.
        // So we have to revert timeout setting here.
        if (timeoutMs > 0) {
            reset_timeout(original_timeout);
        }
        return recvbuf_.take(buffer, length);
    }
    return ERR_CLSD;
}

int NetconnSocket::close() {
    // Set this flag to allow interrupt accept call.
    closing_ = true;
    // Unblock threads waiting for data receiving.
    recv_cond_.notify_all();

    LOGV("NetconnSocket closing");
    Lock lk(conn_mutex_);
    if (auto conn = conn_.get()) {
        if (conntype_ == NETCONN_TCP) {
            LOGV("NetconnSocket::close(type=%d)", conntype_);

            err_t err = netconn_close(conn);
            if (err) {
                LOGE("close failed: %s", lwip_strerr(err));
            }
            conn_ = nullptr;
            LOGV("NetconnSocket closed (%p)", conn);
            return err;
        } else {
            // Not need to close UDP socket
            conn_ = nullptr;
            return ERR_OK;
        }
    }
    return ERR_CLSD;
}

int NetconnSocket::sslRead(void *buffer, size_t maxlen, int timeoutMs) {
    assert(buffer);
    assert(ssl_);

    if (ssl_) {
        int ret;
        // First, try if SSL layer has some remaining decrypted data.
        {
            Lock lk(ssl_mutex_);
            ret = drainDecryptedDataLocked(buffer, maxlen);
            if (ret != 0) {
                // got some decrypted data or an error
                return ret;
            }
        }

        uint8_t buf[16 * 1024];
        int readsz = this->recv(buf, sizeof(buf), timeoutMs);
        if (readsz <= 0) {
            if (readsz < 0 && readsz != ERR_TIMEOUT) {
                LOGE("recv failed: %s", lwip_strerr(readsz));
            }
            return readsz;
        }

        Lock lk(ssl_mutex_);
        ssl_->ReadInject(buf, readsz);
        ret = drainDecryptedDataLocked(buffer, maxlen);
        if (ret == 0) {
            // received some data from network but we need more to decrypt
            return ERR_TIMEOUT;
        }
        return ret;
    }
    return ERR_VAL;
}

// Read decrypted data from SSL as much as possible.
// Returns >0 if success, =0 if SSL needs more data and <0 on error
int NetconnSocket::drainDecryptedDataLocked(void *buffer, size_t maxlen) {
    assert(buffer);
    assert(ssl_);

    auto p = reinterpret_cast<uint8_t *>(buffer);
    auto pe = p + maxlen;
    int err = 0;

    do {
        int ret = ssl_->ReadExtract(p, maxlen);
        if (ret > 0) {
            p += ret;
            maxlen -= ret;
        } else if (ret == 0) {
            // SSL needs more data
            break;
        } else {
            // error
            err = ret;
            break;
        }
    } while (p < pe);

    int extracted = p - reinterpret_cast<uint8_t *>(buffer);
    // note: if ReadExtract() returned some data then returned an error, we treat it as
    // success (buffer has some data in it)
    if (extracted == 0 && err != 0) {
        return err;
    }
    return extracted;
}

int NetconnSocket::sslWrite(const void *buffer, size_t length) {
    assert(buffer);
    assert(ssl_);

    if (ssl_) {
        uint8_t outbuf[65535];
        Lock lk(ssl_mutex_);
        int ret = ssl_->WriteInject(buffer, length);
        if (ret < 0)
            return ERR_SSL_SSL;
        if (!ssl_->IsWritePending()) {
            return ERR_SSL_SSL;
        }

        while (ssl_->IsWritePending()) {
            ret = ssl_->WriteExtract(outbuf, sizeof(outbuf));
            if (ret < 0)
                return ERR_SSL_SSL;

            int result = this->send(outbuf, ret);
            if (result < 0) {
                return result;
            }
        }
        return 0;
    }
    return ERR_VAL;
}

bool NetconnSocket::performHandshake(int timeoutMs) {
    LOGV("NetconnSocket::performHandshake()");
    struct timespec startTime, now;
    clock_gettime(CLOCK_MONOTONIC, &startTime);
    if (ssl_) {
        // Perform SSL handshake iteration
        int ret, err;
        unsigned char buf[4096];
        Lock lk(ssl_mutex_);
        while (!ssl_->IsHandshakeComplete()) {
            ret = clock_gettime(CLOCK_MONOTONIC, &now);
            if (ret == 0 && timeoutMs >= 0 &&
                    calcDeltaTimeMsec(&now, &startTime) > timeoutMs) {
                // timeout
                LOGW("SSL handshake aborted due to timeout (%d msec)", timeoutMs);
                return false;
            }

            ret = ssl_->ReadExtract(buf, sizeof(buf));
            if (ret == 0) {
                ret = this->recv(buf, sizeof(buf), 10);
                if (ret <= 0) {
                    if (ret != ERR_TIMEOUT) {
                        LOGE("recv failed during SSL handshake: %s", lwip_strerr(ret));
                        return false;
                    }
                } else {
                    ssl_->ReadInject(buf, ret);
                }
            } else if (ret < 0) {
                LOGW("SSL handshake failed");
                return false;
            }
            if (ssl_->IsWritePending()) {
                ret = ssl_->WriteExtract(buf, sizeof(buf));
                err = this->send(buf, ret);
                if (err < 0) {
                    LOGE("send failed during SSL handshake: %s", lwip_strerr(err));
                    return false;
                }
            }
        }
        return true;
    }
    return ERR_VAL;
}

int NetconnSocket::setCertificate(const void *pkcs12, size_t length, const char *password, bool is_server) {
    LOGI("Set PKCS12 certificate %zu bytes", length);
    ssl_ = SSLStateMachineHandle(new SSLStateMachine(pkcs12, length, password, is_server));
    return ssl_->IsValid()? 0 : ERR_SSL_SSL;
}

void NetconnSocket::onNetconnEvent(enum netconn_evt event, u16_t len) {
    // These evets are only handled on client connection.
    if (!is_server_) {
        switch (event) {
            case NETCONN_EVT_RCVPLUS: {
                LOGV("NETCONN_EVT_RCVPLUS: %d", len);
                // Used when new incoming data from a remote peer arrives. The amount of data received is passed in len.
                // If len is 0 then a connection event has occurred: this may be an error, the acceptance of a connection
                // for a listening connection (called for the listening connection), or deletion of the connection.
                Lock lk(recv_event_mutex_);
                if (len > 0) {
                    ++recvevent_;
                } else if (conntype_ == NETCONN_TCP) {
                    // len = 0 means that the peer half-closed TCP connection
                    recv_closed_ = true;
                }
                recv_cond_.notify_all();
                break;
            }
            case NETCONN_EVT_RCVMINUS: {
                // Used when new incoming data from a remote peer has been received and accepted by higher layers.
                // The amount of data accepted is passed in len. If len is 0 then this indicates the acceptance of a connection as a result of a listening port
                // (called for the newly created accepted connection).
                LOGV("NETCONN_EVT_RCVMINUS: %d", len);
                Lock lk(recv_event_mutex_);
                // VIPER-2472:
                // Need to check the recvevent_ because it will be 0 when the netconn_recv() is called
                // before onNetConnEvent is set.
                if (len > 0 && recvevent_ > 0) {
                    --recvevent_;
                }
                break;
            }
            case NETCONN_EVT_SENDPLUS:
                LOGV("NETCONN_EVT_SENDPLUS: %d", len);
                break;
            case NETCONN_EVT_SENDMINUS:
                LOGV("NETCONN_EVT_SENDMINUS: %d", len);
                break;
            case NETCONN_EVT_ERROR:
                // This is only used for TCP connections, and is triggered when an error has occurred or a connection is being forced closed. It is used to signal select().
                LOGV("NETCONN_EVT_ERROR");
                error_occurred_ = true;
                // Unblock threads waiting for data receiving.
                recv_cond_.notify_all();
                break;
            default:
                break;
        }
    }
}

// static
int NetconnSocket::calcDeltaTimeMsec(struct timespec *now, struct timespec *base) {
    time_t sec = now->tv_sec - base->tv_sec;
    long nsec = now->tv_nsec - base->tv_nsec;
    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000;
    }

    return sec * 1000 + nsec / 1000000;
}

// vim: set expandtab ts=4 sw=4 :
