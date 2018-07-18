//
// PingSender.h
//   Sends ICMP echo message and receive reply using LWIP Socket API.
//
// Copyright 2015 UIEvolution Inc. All Rights Reserved.
//

#pragma once

#include <lwip/sockets.h>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <cstdint>

class PingSender {
public:
    static const size_t DEFAULT_DATA_SIZE = 32;

    // called from tcpip_jni.cpp
    static int SetUp();
    static int TearDown();

    PingSender();
    bool Configure(const char *dst_addr, unsigned int interval_msec, jobject listener = nullptr, size_t data_size = DEFAULT_DATA_SIZE);
    ~PingSender();

    bool Start();
    bool Stop(int timeout_msec);
    // Returns true if we receive any response. The result is sticky, i.e. will be reset only when
    // Configure() is called.
    bool CheckReply();

private:
    static const uint16_t PING_ID = 0x0001; // any number

    jobject pingListener_;
    jmethodID onReplyMID_;
    jmethodID onTimeoutMID_;

    bool openSocket();
    void closeSocket();
    bool allocateMessage();
    void freeMessage();
    void thread_loop();
    bool send();
    bool receive(int timeout_msec);
    void onReply();
    void onTimeout();
    static int getDeltaMsec(struct timespec& to, struct timespec& from);

    std::thread thread_;
    std::atomic_bool stop_flag_;
    bool thread_running_;
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
    int sock_;
    struct lwip_sockaddr_in saddr_;
    unsigned int interval_msec_;
    unsigned char *msg_buf_;
    size_t data_size_;
    uint16_t seq_no_;
    bool got_reply_;

    PingSender(const PingSender&) = delete;
    PingSender& operator=(const PingSender&) = delete;
};
