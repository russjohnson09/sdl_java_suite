//
// LwipUtil.cpp
// UIE MultiAccess
//
// Created by Sho Amano on 06/06/2016
// Copyright 2016 UIEVolution Inc. All Rights Reserved.
//

#include <lwip/tcpip.h>
#include <mutex>
#include <condition_variable>
#include "LwipUtil.h"

// run the function on tcpip_thread and wait for its execution completes
void LwipUtil::runOnTcpipThread(void (*func)(void *), void *arg) {
    if (!func) {
        return;
    }

    std::mutex mutex;
    std::condition_variable cond;
    bool finished = false;

    struct _params {
        void (*func_)(void *);
        void *arg_;
        std::mutex& mutex_;
        std::condition_variable& cond_;
        bool& completed_;
    } p = { func, arg, mutex, cond, finished };

    tcpip_callback_with_block([](void *ctx) {
        auto params = reinterpret_cast<struct _params *>(ctx);

        params->func_(params->arg_);

        {
            std::lock_guard<std::mutex> lock(params->mutex_);
            params->completed_ = true;
        }
        params->cond_.notify_all();
    }, &p, 1);

    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [&finished]{ return finished; });
    }
}
