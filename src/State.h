#pragma once
#include <Windows.h>
#include "UtilsStr.h"
#include "UtilsPath.h"
#include "Keys.h"

#define IMPL_MAX_USERS 8

struct ImplRawState {
    bool Pressed = false;
    bool ModifierPressed = false;
    bool PrevPressedForRotate = false;
    double Strength = 0;
    double Modifier = 1;
};

struct ImplButtonState : public ImplRawState {
    bool State = false;
};

struct ImplTriggerState : public ImplButtonState {
    double Value = false;

    uint8_t Value8() const { return (uint8_t)round(Value * 0xff); }
};

struct ImplAxisState {
    ImplRawState Pos, Neg; // must be first (see union below)
    int State = 0;         // -1/0/1
    double Value = 0;
    double Extent = 0;
    double RotateModifier = 1;
    bool RotateFakePressed = false;

    int8_t Value8() const { return (int8_t)round(Value * 0x7f); }
    int16_t Value16() const { return (int16_t)round(Value * 0x7fff); }
};

struct ImplAxesState {
    union {
        struct {
            ImplRawState R, L;
        };
        ImplAxisState X = {};
    };
    union {
        struct {
            ImplRawState U, D;
        };
        ImplAxisState Y = {};
    };
};

struct ImplState {
    mutable mutex Mutex;
    ImplButtonState A, B, X, Y, LB, RB, L, R, DL, DR, DU, DD, Start, Back, Guide, Extra;
    ImplTriggerState LT, RT;
    ImplAxesState LA, RA;
    int Version;

    void IncrementVersion() {
        Version++;
        if (Version == 0) {
            Version++;
        }
    }
};

struct ImplUser;
using ImplUserCb = list<function<void(ImplUser *)>>::iterator;
struct DeviceIntf;

struct ImplUser {
    ImplState State;
    bool Connected = false;
    bool DeviceSpecified = false;
    DeviceIntf *Device = nullptr;

    mutex CbMutex;
    list<function<void(ImplUser *)>> Callbacks;

    ImplUserCb AddCallback(function<void(ImplUser *)> cb);
    void RemoveCallback(const ImplUserCb &cbIter);

    void SendEvent() {
        lock_guard<mutex> lock(CbMutex);
        for (auto &cb : Callbacks) {
            cb(this);
        }
    }
};

struct ImplInputCond {
    int Key = 0;
    bool State = false;
    bool Toggle = false;
    ImplInputCond *Next = nullptr;

    void Reset() {
        if (Next) {
            Next->Reset();
            delete Next;
        }
        *this = ImplInputCond();
    }
};

struct ImplInput {
    // (it's not safe to use this as UINT_PTR, since it may get deleted)
    static unordered_map<UINT_PTR, ImplInput *> sTimers;

    static ImplInput *FromTimer(UINT_PTR id) {
        auto iter = sTimers.find(id);
        return iter == sTimers.end() ? nullptr : iter->second;
    }

    int DestKey = 0;
    MyVkType SrcType = {};
    MyVkType DestType = {};
    int8_t User = 0; // -1 to use active

    bool Forward : 1 = false;
    bool Turbo : 1 = false;
    bool Toggle : 1 = false;
    bool Snap : 1 = false;
    bool PassedCond : 1 = false;
    bool TurboValue : 1 = false;
    bool ToggleValue : 1 = false;

    // set even if cond/etc fails (shouldn't really be here...)
    bool AsyncDown : 1 = false;
    bool AsyncToggle : 1 = false;

    double Rate = 0;
    double Strength = 0;
    ImplInput *Next = nullptr;
    ImplInputCond *Conds = nullptr;
    UINT_PTR TimerValue = 0;

    bool IsValid() const { return DestKey != 0; }
    bool HasTimer() const { return TimerValue != 0; }

    void StartTimer(int timeMs, TIMERPROC timerCb) {
        if (TimerValue) {
            EndTimer();
        }

        TimerValue = SetTimer(nullptr, 0, timeMs, timerCb);
        sTimers[TimerValue] = this;
    }

    void EndTimer() {
        if (TimerValue) {
            sTimers.erase(TimerValue);
            KillTimer(nullptr, TimerValue);
            TimerValue = 0;
        }
    }

    void Reset() {
        EndTimer();
        if (Next) {
            Next->Reset();
            delete Next;
        }
        if (Conds) {
            Conds->Reset();
            delete Conds;
        }

        bool oldDown = AsyncDown;
        bool oldToggle = AsyncToggle;
        *this = ImplInput();
        AsyncDown = oldDown;
        AsyncToggle = oldToggle;
    }
};

unordered_map<UINT_PTR, ImplInput *> ImplInput::sTimers;

struct ImplDeviceBase {
    HHOOK HLowHook = nullptr;
    bool IsMapped = false;
};

struct ImplKeyboard : public ImplDeviceBase {
    enum { Count = MY_VK_LAST_REAL };

    ImplInput Keys[Count] = {};

    ImplInput *Get(int input) {
        if (input >= 0 && input < Count) {
            return &Keys[input];
        } else {
            return nullptr;
        }
    }

    void Reset() {
        for (int i = 0; i < Count; i++) {
            Keys[i].Reset();
        }
    }
};

struct ImplThreadHook {
    DWORD Thread = 0;
    HHOOK HHook = nullptr;
    bool Started = false;
    bool Finished = false;
};

struct ImplMouseAxis {
    ImplInput Forward;
    ImplInput Backward;

    void Reset() {
        Forward.Reset();
        Backward.Reset();
    }
};

struct ImplMouseMotion {
    ImplMouseAxis Horz, Vert;

    void Reset() {
        Horz.Reset();
        Vert.Reset();
    }
};

struct ImplMouse : public ImplDeviceBase {
    ImplMouseMotion Motion;
    ImplMouseAxis Wheel, HWheel;

    bool AnyMotionInput = false;
    bool AnyMotionOutput = false;

    DWORD ActiveThread = 0;
    POINT OldPos = {};
    WeakAtomic<DWORD> PrevThread{0};
    WeakAtomic<HWND> PrevWindow{nullptr};
    MOUSEINPUT MotionChange = {};

    mutex ThreadHooksMutex;
    vector<ImplThreadHook> ThreadHooks;

    void Reset() {
        Motion.Reset();
        Wheel.Reset();
        HWheel.Reset();
    }
};

struct ImplDelayedHooks {
    Path WaitDll;
    vector<Path> Hooks;
    WeakAtomic<bool> Loaded = false;
};

using ImplGlobalCb = list<function<void(ImplUser *, bool)>>::iterator;

struct ImplG {
    ImplUser Users[IMPL_MAX_USERS];
    ImplKeyboard Keyboard; // also includes mouse buttons, though...
    ImplMouse Mouse;
    int8_t ActiveUser = 0;
    int8_t DefaultActiveUser = 0;

    bool Trace = false;
    bool Debug = false;
    bool ApiTrace = false;
    bool ApiDebug = false;
    bool WaitDebugger = false;
    bool Forward = false;
    bool Always = false;
    bool Disable = false;
    bool InjectChildren = false;
    bool RumbleWindow = false;

    HINSTANCE HInstance = nullptr;
    DWORD DllThread = 0;
    HWND DllWindow = nullptr;
    Path ConfigPath;
    vector<Path> ExtraHooks;
    vector<ImplDelayedHooks> ExtraDelayedHooks;

    mutex CbMutex;
    list<function<void(ImplUser *, bool)>> GlobalCallbacks;

    ImplGlobalCb AddGlobalCallback(function<void(ImplUser *, bool)> cb) {
        lock_guard<mutex> lock(CbMutex);
        GlobalCallbacks.push_back(cb);
        return std::prev(GlobalCallbacks.end());
    }

    void RemoveGlobalCallback(const ImplGlobalCb &cbIter) {
        lock_guard<mutex> lock(CbMutex);
        GlobalCallbacks.erase(cbIter);
    }

    void SendGlobalEvent(ImplUser *user, bool added) {
        lock_guard<mutex> lock(CbMutex);
        for (auto &cb : GlobalCallbacks) {
            cb(user, added);
        }
    }
} G;

ImplUserCb ImplUser::AddCallback(function<void(ImplUser *)> cb) {
    lock_guard<mutex> lock(CbMutex);
    Callbacks.push_back(cb);
    return std::prev(Callbacks.end());
}

void ImplUser::RemoveCallback(const ImplUserCb &cbIter) {
    lock_guard<mutex> lock(CbMutex);
    Callbacks.erase(cbIter);
}

static ImplUser *ImplGetUser(DWORD user, bool force = false) {
    if (user < IMPL_MAX_USERS && (force || G.Users[user].Connected)) {
        return &G.Users[user];
    } else {
        return nullptr;
    }
}

static DeviceIntf *ImplGetDevice(DWORD user) {
    ImplUser *data = ImplGetUser(user);
    return data ? data->Device : nullptr;
}

static int ImplGetUsers(UINT *outMask, int minUser = 0) {
    UINT mask = 0;
    int count = 0;
    for (int i = minUser; i < IMPL_MAX_USERS; i++) {
        if (G.Users[i].Connected) {
            mask |= (1 << i);
            count++;
        }
    }

    *outMask = mask;
    return count;
}

static int ImplNextUser(UINT *refMask) {
    DWORD userIdx = 0;
    BitScanForward(&userIdx, *refMask);
    *refMask &= ~(1 << userIdx);
    return userIdx;
}

ImplInput *ImplGetInput(int key) {
    ImplInput *input = G.Keyboard.Get(key);
    if (input) {
        return input;
    }

    switch (key) {
    case MY_VK_WHEEL_UP:
        return &G.Mouse.Wheel.Forward;
    case MY_VK_WHEEL_DOWN:
        return &G.Mouse.Wheel.Backward;
    case MY_VK_WHEEL_RIGHT:
        return &G.Mouse.HWheel.Forward;
    case MY_VK_WHEEL_LEFT:
        return &G.Mouse.HWheel.Backward;
    case MY_VK_MOUSE_UP:
        return &G.Mouse.Motion.Vert.Forward;
    case MY_VK_MOUSE_DOWN:
        return &G.Mouse.Motion.Vert.Backward;
    case MY_VK_MOUSE_RIGHT:
        return &G.Mouse.Motion.Horz.Forward;
    case MY_VK_MOUSE_LEFT:
        return &G.Mouse.Motion.Horz.Backward;
    }
    return nullptr;
}

int ImplChooseBestKeyInPair(int key) {
    int key1, key2;
    tie(key1, key2) = GetKeyPair(key);
    int scan1 = MapVirtualKeyW(key1, MAPVK_VK_TO_VSC);
    int scan2 = MapVirtualKeyW(key2, MAPVK_VK_TO_VSC);
    return (!scan1 && scan2) ? key2 : key1;
}

#define DBG_ASSERT_DLL_THREAD() \
    DBG_ASSERT(GetCurrentThreadId() == G.DllThread, "wrong thread in call")
