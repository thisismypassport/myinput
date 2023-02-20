#pragma once
#include "UtilsBase.h"
#include <Windows.h>

struct UserTimers {
    // (it's not safe to use 'this' as UINT_PTR, since timers may get called after EndTimer)
    unordered_map<UINT_PTR, void *> Map;

    template <class T>
    T *From(UINT_PTR id) {
        auto iter = Map.find(id);
        return iter == Map.end() ? nullptr : (T *)iter->second;
    }
} GTimerData;

class UserTimer {
    UINT_PTR Value = 0;

public:
    bool IsSet() const { return Value != 0; }

    // self must be constant for the lifetime of a UserTimer
    void StartMs(DWORD timeMs, TIMERPROC timerCb, void *self) {
        if (Value) {
            SetTimer(nullptr, Value, timeMs, timerCb);
        } else {
            Value = SetTimer(nullptr, 0, timeMs, timerCb);
            GTimerData.Map[Value] = self;
        }
    }

    void StartS(double time, TIMERPROC timerCb, void *self) {
        StartMs((DWORD)ceil(time * 1000), timerCb, self);
    }

    void End() {
        if (Value) {
            GTimerData.Map.erase(Value);
            KillTimer(nullptr, Value);
            Value = 0;
        }
    }

    ~UserTimer() {
        End();
    }
};

class InfiniteThreadPool {
    struct ThreadEntry {
        LPTHREAD_START_ROUTINE Routine;
        void *Param;
        HANDLE Event;
        InfiniteThreadPool *Pool;
    };

    mutex Mutex;
    vector<ThreadEntry *> IdleThreads;

    static DWORD WINAPI ProcessThread(LPVOID param) {
        ThreadEntry *entry = (ThreadEntry *)param;

        while (true) {
            entry->Routine(entry->Param);

            {
                lock_guard<mutex> lock(entry->Pool->Mutex);
                entry->Pool->IdleThreads.push_back(entry);
            }
            WaitForSingleObject(entry->Event, INFINITE);
        }
    }

public:
    void CreateThread(LPTHREAD_START_ROUTINE routine, void *param) {
        lock_guard<mutex> lock(Mutex);
        if (!IdleThreads.empty()) {
            ThreadEntry *entry = IdleThreads.back();
            IdleThreads.pop_back();

            entry->Routine = routine;
            entry->Param = param;
            SetEvent(entry->Event);
        } else {
            HANDLE event = CreateEventW(nullptr, false, false, nullptr);
            ThreadEntry *entry = new ThreadEntry{routine, param, event, this};
            CloseHandle(::CreateThread(nullptr, 0, ProcessThread, entry, 0, nullptr));
        }
    }
} GInfiniteThreadPool;

class ReliablePostThreadMessage // regular PostThreadMessage doesn't work early in a thread's lifetime
{
    mutex Mutex;
    vector<tuple<UINT, WPARAM, LPARAM>> InitialMessages;
    WeakAtomic<bool> Initialized = false;

public:
    // thread must be constant for the lifetime of a ReliablePostThreadMessage
    // and must match the id of the thread that calls Initialize
    void Post(DWORD thread, int msg, WPARAM w, LPARAM l) {
        if (!Initialized) {
            lock_guard<mutex> lock(Mutex);
            if (!Initialized) {
                InitialMessages.push_back({msg, w, l});
                return;
            }
        }

        PostThreadMessageW(thread, msg, w, l);
        // (note: may still fail at shutdown time)
    }

    void Initialize() {
        lock_guard<mutex> lock(Mutex);

        // initialize message queue - only then PostThreadMessage will work
        MSG dummy;
        PeekMessageW(&dummy, NULL, WM_USER, WM_USER, PM_NOREMOVE);

        if (!InitialMessages.empty()) {
            DWORD thread = GetCurrentThreadId();
            for (auto [msg, w, l] : InitialMessages) {
                PostThreadMessageW(thread, msg, w, l);
            }

            InitialMessages.clear();
            InitialMessages.shrink_to_fit();
        }
        Initialized = true;
    }
};
