//
// LwipUtil.h
// UIE MultiAccess
//
// Created by Sho Amano on 06/06/2016
// Copyright 2016 UIEVolution Inc. All Rights Reserved.
//

#pragma once

class LwipUtil {
public:
    static void runOnTcpipThread(void (*func)(void *), void *arg);

private:
    LwipUtil();
    ~LwipUtil();

    LwipUtil(const LwipUtil&) = delete;
    LwipUtil& operator=(const LwipUtil&) = delete;
};
