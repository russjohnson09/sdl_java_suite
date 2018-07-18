//
// semaphore.hpp
// UIE MultiAccess
//
// Created by Rakuto Furutani on 5/7/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#pragma once

#include <mutex>
#include <condition_variable>

class Semaphore {
 public:
    explicit Semaphore(int init_count = count_max)
      : count_(init_count) {}

    void wait() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [=]{ return 0 < count_; });
        --count_;
    }
    bool try_wait() {
        std::lock_guard<std::mutex> lk(m_);
        if (0 < count_) {
            --count_;
            return true;
        } else {
            return false;
        }
    }
    void signal() {
        std::lock_guard<std::mutex> lk(m_);
        if (count_ < count_max) {
            ++count_;
            cv_.notify_one();
        }
    }

    // Lockable requirements
    void lock() { wait(); }
    bool try_lock() { return try_wait(); }
    void unlock() { signal(); }

 private:
    static const int count_max = 1;
    int count_;
    std::mutex m_;
    std::condition_variable cv_;
};
