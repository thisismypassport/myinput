#pragma once
#include "UtilsBase.h"
#include "UtilsStr.h"
#include "UtilsPath.h"
#include <Windows.h>
#include <cstdarg>

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

class ReusableThread {
    struct Action {
        LPTHREAD_START_ROUTINE Routine;
        void *Param;
    };

    mutex Mutex;
    HANDLE Event = nullptr;
    deque<Action> Actions;

    static DWORD WINAPI ProcessThread(LPVOID param) {
        ReusableThread *self = (ReusableThread *)param;

        while (true) {
            WaitForSingleObject(self->Event, INFINITE);

            Action action;
            {
                lock_guard<mutex> lock(self->Mutex);
                if (self->Actions.empty()) {
                    continue;
                }

                action = self->Actions.front();
                self->Actions.pop_front();
            }

            action.Routine(action.Param);
        }
    }

public:
    void CreateThread(LPTHREAD_START_ROUTINE routine, void *param) {
        lock_guard<mutex> lock(Mutex);
        if (!Event) {
            Event = CreateEventW(nullptr, false, false, nullptr);
            CloseHandle(::CreateThread(nullptr, 0, ProcessThread, this, 0, nullptr));
        }

        if (Actions.empty()) {
            SetEvent(Event);
        }
        Actions.push_back({routine, param});
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
    WeakAtomic<DWORD> Thread = 0;

public:
    void Post(int msg, WPARAM w, LPARAM l) {
        if (!Thread) {
            lock_guard<mutex> lock(Mutex);
            if (!Thread) {
                InitialMessages.push_back({msg, w, l});
                return;
            }
        }

        PostThreadMessageW(Thread, msg, w, l);
        // (note: may still fail at shutdown time)
    }

    void Initialize() {
        lock_guard<mutex> lock(Mutex);

        // initialize message queue - only then PostThreadMessage will work
        MSG dummy;
        PeekMessageW(&dummy, NULL, WM_USER, WM_USER, PM_NOREMOVE);

        DWORD thread = GetCurrentThreadId();
        if (!InitialMessages.empty()) {
            for (auto [msg, w, l] : InitialMessages) {
                PostThreadMessageW(thread, msg, w, l);
            }

            InitialMessages.clear();
            InitialMessages.shrink_to_fit();
        }
        Thread = thread;
    }
};

int wsprintfT(char *dest, const char *src, ...) {
    va_list va;
    va_start(va, src);
    int ret = wvsprintfA(dest, src, va);
    va_end(va);
    return ret;
}
int wsprintfT(wchar_t *dest, const wchar_t *src, ...) {
    va_list va;
    va_start(va, src);
    int ret = wvsprintfW(dest, src, va);
    va_end(va);
    return ret;
}

#define FORMAT_GUID_BUFSIZE 39

template <class tchar>
void FormatGuid(tchar dest[FORMAT_GUID_BUFSIZE], GUID guid) {
    wsprintfT(dest, TSTR("{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}"),
              (int)guid.Data1, (int)guid.Data2, (int)guid.Data3, (int)guid.Data4[0], (int)guid.Data4[1],
              (int)guid.Data4[2], (int)guid.Data4[3], (int)guid.Data4[4], (int)guid.Data4[5], (int)guid.Data4[6], (int)guid.Data4[7]);
}

ostream &operator<<(ostream &o, const GUID &guid) {
    wchar_t buffer[FORMAT_GUID_BUFSIZE];
    FormatGuid(buffer, guid);
    return o << buffer;
}
