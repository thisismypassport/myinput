#pragma once
// macOS counterpart of src/WinUtils.h: the timer and deferred-call primitives the engine uses,
// built on libdispatch's main queue. The engine is single-threaded: on Windows it owns a "dll thread",
// here everything runs on the app's main thread.
#include "UtilsBase.h"
#include "UtilsStr.h"
#include "UtilsPath.h"
#include <Windows.h>
#include <dispatch/dispatch.h>
#include <sys/stat.h>

struct UserTimers {
    unordered_map<UINT_PTR, void *> Map;
    UINT_PTR NextId = 0;

    template <class T>
    T *From(UINT_PTR id) {
        auto iter = Map.find(id);
        return iter == Map.end() ? nullptr : (T *)iter->second;
    }
} GTimerData;

// Same contract as SetTimer/KillTimer: periodic until ended, and starting again changes the period.
class UserTimer {
    struct Context {
        UINT_PTR Id;
        TIMERPROC Proc;
    };

    UINT_PTR Value = 0;
    dispatch_source_t Source = nullptr;
    Context *Ctx = nullptr;

    static void Fire(void *param) {
        Context ctx = *(Context *)param; // the callback may end the timer
        if (GTimerData.Map.count(ctx.Id)) {
            ctx.Proc(nullptr, WM_TIMER, ctx.Id, GetTickCount());
        }
    }

    static void Finalize(void *param) { delete (Context *)param; }

public:
    UserTimer() = default;
    UserTimer(const UserTimer &) {} // mappings are copied while being loaded, before any timer runs
    UserTimer &operator=(const UserTimer &) = delete;

    bool IsSet() const { return Value != 0; }

    // self must be constant for the lifetime of a UserTimer
    void StartMs(DWORD timeMs, TIMERPROC timerCb, void *self) {
        if (!Source) {
            Value = ++GTimerData.NextId;
            GTimerData.Map[Value] = self;
            Ctx = new Context{Value, timerCb};
            Source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
            dispatch_set_context(Source, Ctx);
            dispatch_set_finalizer_f(Source, Finalize);
            dispatch_source_set_event_handler_f(Source, Fire);
        }

        Ctx->Proc = timerCb;
        uint64_t interval = (uint64_t)max(timeMs, (DWORD)1) * NSEC_PER_MSEC;
        dispatch_source_set_timer(Source, dispatch_time(DISPATCH_TIME_NOW, interval), interval, 0);
        if (!Resumed) {
            Resumed = true;
            dispatch_resume(Source);
        }
    }

    void StartS(double time, TIMERPROC timerCb, void *self) {
        StartMs((DWORD)ceil(time * 1000), timerCb, self);
    }

    void End() {
        if (Source) {
            GTimerData.Map.erase(Value);
            dispatch_source_cancel(Source);
            dispatch_release(Source);
            Source = nullptr;
            Ctx = nullptr;
            Resumed = false;
            Value = 0;
        }
    }

    ~UserTimer() {
        End();
    }

private:
    bool Resumed = false;
};

// On Windows this is a worker thread that keeps injected input out of the hook's call stack.
// Deferring to the next main-queue turn gives the same guarantee here.
class ReusableThread {
    struct Action {
        LPTHREAD_START_ROUTINE Routine;
        void *Param;
    };

    static void Run(void *param) {
        Action *action = (Action *)param;
        action->Routine(action->Param);
        delete action;
    }

public:
    ReusableThread(int priority = THREAD_PRIORITY_NORMAL) {}

    void CreateThread(LPTHREAD_START_ROUTINE routine, void *param) {
        dispatch_async_f(dispatch_get_main_queue(), new Action{routine, param}, Run);
    }
};

bool IsFileExists(const wchar_t *path) {
    struct stat info;
    return stat(PathToStr(path), &info) == 0;
}

uint64_t GetFileLastWriteTime(const wchar_t *path) {
    struct stat info;
    if (stat(PathToStr(path), &info) != 0) {
        return 0;
    }
    return (uint64_t)info.st_mtimespec.tv_sec * 1000000000ull + (uint64_t)info.st_mtimespec.tv_nsec;
}
