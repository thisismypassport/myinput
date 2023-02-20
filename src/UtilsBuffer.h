#pragma once
#include "UtilsBase.h"

// Buffers must be PODs

class BufferList {
    size_t mMaxSize;
    void *mFree = nullptr;
    mutex mMutex;

public:
    BufferList(size_t maxSize) : mMaxSize(maxSize) {}

    size_t MaxSize() { return mMaxSize; }

    void PutBack(void *ptr) {
        lock_guard<mutex> lock(mMutex);
        *(void **)ptr = mFree;
        mFree = ptr;
    }

    void *Take() {
        lock_guard<mutex> lock(mMutex);
        void *ptr;
        if (mFree) {
            ptr = mFree;
            mFree = *(void **)mFree;
        } else {
            ptr = malloc(max(mMaxSize, sizeof(void *)));
        }
        return ptr;
    }
};

class BufferLists {
    unordered_map<size_t, BufferList *> mLists;
    mutex mMutex;

public:
    BufferList *Get(size_t size) {
        lock_guard<mutex> lock(mMutex);
        auto &list = mLists[size];
        if (!list) {
            list = new BufferList(size);
        }
        return list;
    }

} GBufferLists;

template <int Size>
class BufferListOfSize {
    WeakAtomic<BufferList *> mList = nullptr;

public:
    BufferList *Get() {
        if (!mList) {
            mList = GBufferLists.Get(Size);
        }
        return mList;
    }
};

class Buffer {
    void *mPtr;
    size_t mSize;
    BufferList *mList;

    void Dtor() {
        if (mPtr) {
            mList->PutBack(mPtr);
        }
    }

public:
    Buffer() : mPtr(nullptr), mSize(0), mList(nullptr) {}
    Buffer(BufferList *list, size_t size) : mSize(size), mList(list) {
        mPtr = list->Take();
    }
    Buffer(BufferList *list) : Buffer(list, list->MaxSize()) {}

    Buffer(const Buffer &other) = delete;
    Buffer(Buffer &&other) : mPtr(other.mPtr), mSize(other.mSize), mList(other.mList) {
        other.mPtr = nullptr;
        other.mSize = 0;
    }

    ~Buffer() { Dtor(); }

    void operator=(const Buffer &other) = delete;
    void operator=(Buffer &&other) {
        Dtor();
        mPtr = other.mPtr;
        mSize = other.mSize;
        mList = other.mList;
        other.mPtr = nullptr;
        other.mSize = 0;
    }

    void *Ptr() { return mPtr; }
    size_t Size() { return mSize; }
    void SetSize(size_t size) { mSize = size; }

    friend class BufferList;
};
