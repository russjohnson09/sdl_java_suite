//
//  NetbufWrapper.hpp
//  UIEMultiAccess
//
//  Created by Rakuto Furutani on 6/23/15.
//  Copyright (c) 2015 UIEvolution Inc. All rights reserved.
//

#pragma once

#include <lwip/netbuf.h>
#include <cassert>
#include <list>
#include <memory>
#include <JNIUtil.h>

/**
 * Simple lwIP's netbuf wrapper that provides some useful methods to access it.
 *
 * @note Thread safe, all methods simultaneously can be accessedd from multiple threads.
 */
class NetbufWrapper {
    using NetbufHolder = std::unique_ptr<struct netbuf, void(*)(struct netbuf *)>;
    using Lock = std::lock_guard<std::mutex>;

 public:
    NetbufWrapper(): head_(nullptr, nullptr), offset_(0), index_(0) {
    }

    virtual ~NetbufWrapper() {
    }

    inline void set(struct netbuf *buf) {
        assert(buf);

        Lock lk(mutex_);
        if (auto head = head_.get()) {
            netbuf_chain(head, buf);

            // Because netbuf_chain() moves the current data pointer to head of chained buffer,
            // call netbuf_next to move data pointer.
            for (u16_t i = 0; i < index_; i++) {
                netbuf_next(head);
            }
        } else {
            head_ = NetbufHolder(buf, [](struct netbuf *buf) {
                netbuf_delete(buf);
            });
        }
    }

    inline size_t size() {
        Lock lk(mutex_);
        return (head_)? netbuf_len(head_.get()) : 0;
    }

    size_t take(void *buffer, size_t length) {
        assert(buffer != nullptr);

        size_t retval = 0;
        Lock lk(mutex_);
        if (auto buf = head_.get()) {
            u16_t bufsz;
            uint8_t *pbuf;
            auto p = reinterpret_cast<uint8_t *>(buffer);
            while (length > 0) {
                netbuf_data(buf, reinterpret_cast<void **>(&pbuf), &bufsz);
                auto ncopy = bufsz - offset_;
                if (length >= ncopy) {
                    memcpy(p, pbuf + offset_, ncopy);
                    p += ncopy;
                    length -= ncopy;
                    offset_ = 0;
                    retval += ncopy;
                    if (netbuf_next(buf) < 0) {
                        head_ = nullptr;
                        break;
                    }
                } else {
                    memcpy(p, pbuf + offset_, length);
                    p += length;
                    retval += length;
                    offset_ += length;
                    length = 0;
                    break;
                }
            }
        }
        return retval;
    }

 private:
    std::mutex mutex_;
    NetbufHolder head_;
    size_t offset_;
    u16_t index_;
};
