//
// BlockingQueue.h
// UIE MultiAccess
//
// Created by Sho Amano on 1/12/17.
// Copyright (c) 2017 Xevo Inc. All rights reserved.
//

#pragma once

#include <queue>
#include <condition_variable>
#include <mutex>
#include <type_traits>

#define DISALLOW_COPY_AND_ASSIGN(T)         \
    T(const T &other) = delete;             \
    T& operator=(const T &other) = delete

/* Simple blocking queue implementation */

template <typename T>
class BlockingQueue {
public:
    BlockingQueue() {}
    ~BlockingQueue() {}

    static_assert(std::is_copy_constructible<T>::value == true, "Type T is not copyable");

    void push(const T& data) {
        std::lock_guard<std::mutex> lock(mutex_);

        bool wasEmpty = queue_.empty();
        queue_.push(data);
        if (wasEmpty) {
            cond_.notify_all();
        }
    }

    // Get the first element. If the queue is empty, block until an element is available.
    const T& front() const {
        std::unique_lock<std::mutex> lock(mutex_);
        while (queue_.empty()) {
            cond_.wait(lock);
        }

        return queue_.front();
    }

    // Remove the first element. If the queue is empty then do nothing.
    void pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!queue_.empty()) {
            queue_.pop();
        }
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cond_;

    DISALLOW_COPY_AND_ASSIGN(BlockingQueue);
};
