/*
 * object.h
 *
 * Created by Rakuto Furutani on 7/1/14.
 * Copyright (c) 2014 UIEvolution Inc. All rights reserved.
 */
#pragma once

#define DISALLOW_COPY_AND_ASSIGN(TypeName)          \
    TypeName(const TypeName&) = delete;             \
    TypeName& operator=(const TypeName&) = delete;  \

// Interface for the reference count object
template <class T>
class Retainable {
public:
    virtual T* Retain() = 0;
    virtual void Release() = 0;
};
