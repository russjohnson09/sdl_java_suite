//
// LinkedBufferQueue.h
// UIE MultiAccess
//
// Created by Rakuto Furutani on 12/11/14.
// Copyright (c) 2014 UIEvolution Inc. All rights reserved.
//
#pragma once

#include <memory>
#include <atomic>

// A simple single-producer, single-consumer lock free queue.
class LinkedBufferQueue {
public:
    class ChunkBuffer {
    public:
        size_t length;
        void *data;

        ChunkBuffer(const void *data, size_t len)
        : offset_(0), length(len) {
            this->data = new uint8_t[len];
            memcpy(this->data, data, len);
        }

        ~ChunkBuffer() {
            if (data)
                delete [] reinterpret_cast<uint8_t *>(data);
        }

        inline void advanceOffset(size_t off) {
            offset_.fetch_add(off, std::memory_order_relaxed);
        }
        inline size_t offset() {
            return offset_.load();
        }
        inline size_t Length() { return length; }
        inline size_t Remain() { return length - offset_.load(); }
        inline void *Data() { return reinterpret_cast<char *>(data) + offset_.load(); }

    private:
        std::atomic<size_t> offset_;
    };

    LinkedBufferQueue()
    : head_(new Node), tail_(head_.load()) {
    }

    LinkedBufferQueue(const LinkedBufferQueue &other) = delete;
    LinkedBufferQueue& operator=(const LinkedBufferQueue &other) = delete;

    ~LinkedBufferQueue() {
        while (Node *const old_head=head_.load()) {
            head_.store(old_head->next);
            delete old_head;
        }
    }

    inline bool IsEmpty() {
        return (head_.load() == tail_.load());
    }

    inline std::shared_ptr<ChunkBuffer> First() {
        return (IsEmpty())? std::shared_ptr<ChunkBuffer>() : head_.load()->data;
    }

    inline std::shared_ptr<ChunkBuffer> Pop() {
        Node *old_head = pop_head();
        if (!old_head) {
            return std::shared_ptr<ChunkBuffer>();
        }
        std::shared_ptr<ChunkBuffer> result(old_head->data);
        delete old_head;
        /*
         *totalLength_.fetch_sub(result->length, std::memory_order_release);
         */
        return result;
    }

    inline void Push(const void *data, size_t length) {
        auto newBuffer = std::make_shared<ChunkBuffer>(data, length);
        auto p = new Node;
        Node* const old_tail = tail_.load();
        old_tail->data.swap(newBuffer);
        old_tail->next = p;
        tail_.store(p);
        /*
         *totalLength_.fetch_add(length, std::memory_order_relaxed);
         */
    }

private:
    class Node {
    public:
        std::shared_ptr<ChunkBuffer> data;
        Node *next;
        Node()
        : next(nullptr)
        {}
    };

    std::atomic<Node *> head_;
    std::atomic<Node *> tail_;
    std::atomic<size_t> totalLength_;

    Node* pop_head() {
        Node* const old_head = head_.load();
        if (old_head == tail_.load()) {
            return nullptr;
        }
        head_.store(old_head->next);
        return old_head;
    }
};
