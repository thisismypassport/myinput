#pragma once
#include "UtilsStr.h"
#include "UtilsPath.h"
#include "Keys.h"
#include "WinUtils.h"
#include <Windows.h>

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
    double Value = 0; // 0 .. 1
    double Threshold = 0.12;

    uint8_t Value8() const { return (uint8_t)nearbyint(Value * 0xff); }
};

struct ImplAxisState {
    ImplRawState Pos, Neg; // must be first (see union below)
    double Value = 0;      // -1 .. 1
    double Extent = 0;
    double RotateModifier = 1;
    bool RotateFakePressed = false;

    int8_t Value8() const { return (int8_t)nearbyint(Value * 0x7f); }
    int16_t Value16() const { return (int16_t)nearbyint(Value * 0x7fff); }
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

struct ImplMotionDimState {
    double NewValue = 0;
    double OldValue = 0;
    double Speed = 0;
    double Accel = 0;
    double FinalAccel = 0; // value in mult. of g
};

static constexpr double DegreesToRadians = std::numbers::pi / 180;

struct Vector3 {
    double X, Y, Z;
};

struct ImplMotionState {
    ImplMotionDimState X, Y, Z;    // value is metres
    ImplMotionDimState RX, RY, RZ; // value is radians
    UserTimer Timer;
    DWORD PrevTime = 0;

    Vector3 XAxis() {
        auto [sy, cy] = SinCos(RY.NewValue);
        auto [sz, cz] = SinCos(RZ.NewValue);
        return {cz * cy, sz * cy, -sy};
    }

    Vector3 YAxis() {
        auto [sx, cx] = SinCos(RX.NewValue);
        auto [sy, cy] = SinCos(RY.NewValue);
        auto [sz, cz] = SinCos(RZ.NewValue);
        auto sxy = sx * sy;
        return {sxy * cz - cx * sz, sxy * sz + cx * cz, sx * cy};
    }

    Vector3 ZAxis() {
        auto [sx, cx] = SinCos(RX.NewValue);
        auto [sy, cy] = SinCos(RY.NewValue);
        auto [sz, cz] = SinCos(RZ.NewValue);
        auto cxsy = cx * sy;
        return {cxsy * cz + sx * sz, cxsy * sz - sx * cz, cx * cy};
    }

    static constexpr double PosScale = 0.01; // cm -> metres
    static constexpr double RotScale = DegreesToRadians;
    static constexpr double GScale = 9.80665;

    ImplMotionState() { Y.FinalAccel = -1; } // gravity
};

struct ImplFeedbackState {
    double LowRumble = 0, HighRumble = 0;
};

struct ImplState {
    mutable mutex Mutex;
    ImplButtonState A, B, X, Y, LB, RB, L, R, DL, DR, DU, DD, Start, Back, Guide, Extra;
    ImplTriggerState LT, RT;
    ImplAxesState LA, RA;
    ImplMotionState Motion;
    ImplFeedbackState Feedback;
    DWORD Time = 0;
    int Version = 0;
};

struct DeviceIntf;

struct ImplUser {
    ImplState State;
    bool Connected = false;
    bool DeviceSpecified = false;
    DeviceIntf *Device = nullptr;
    CallbackList<void(ImplUser *)> Callbacks;
};

using ImplUserCb = decltype(ImplUser::Callbacks)::CbIter;

struct ImplCond {
    int Key = 0;
    int User = 0;
    bool State = false;
    bool Toggle = false;
    SharedPtr<ImplCond> Child;
    SharedPtr<ImplCond> Next;
};

struct ImplMapping {
    int SrcKey = 0;
    int DestKey = 0;
    MyVkType SrcType = {};
    MyVkType DestType = {};
    int SrcUser = 0;
    int DestUser = 0; // -1 to use active

    bool Forward : 1 = false;
    bool Turbo : 1 = false;
    bool Toggle : 1 = false;
    bool Add : 1 = false;
    bool PassedCond : 1 = false;
    bool TurboValue : 1 = false;
    bool ToggleValue : 1 = false;
    bool Replace : 1 = false; // r/w only
    bool Reset : 1 = false;   // r/w only

    double Rate = 0;
    double Strength = 0;
    SharedPtr<ImplMapping> Next;
    SharedPtr<ImplCond> Conds;
    UserTimer Timer;

    bool HasTimer() const { return Timer.IsSet(); }
    void StartTimerMs(DWORD timeMs, TIMERPROC timerCb) { Timer.StartMs(timeMs, timerCb, this); }
    void StartTimerS(double time, TIMERPROC timerCb) { Timer.StartS(time, timerCb, this); }
    void EndTimer() { Timer.End(); }
    static ImplMapping *FromTimer(UINT_PTR id) { return GTimerData.From<ImplMapping>(id); }
};

struct ImplReset {
    SharedPtr<ImplMapping> Mapping;
    SharedPtr<ImplReset> Next;
};

struct ImplInput {
    SharedPtr<ImplMapping> Mappings;
    SharedPtr<ImplReset> Resets;

    bool AsyncDown : 1 = false;
    bool AsyncToggle : 1 = false;

    bool ObservedPress : 1 = false;
    bool ObservedPressForCheck : 1 = false;

    void Reset() { Mappings = nullptr; }
};

struct ImplDeviceBase {
    HHOOK HLowHook = nullptr;
    bool IsMapped = false;
};

struct ImplKeyboard : public ImplDeviceBase {
    enum { Count = MY_VK_LAST_REAL };

    ImplInput Keys[Count] = {};

    ImplInput *Get(int input) {
        if (input > 0 && input < Count) {
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

    MOUSEINPUT MotionChange = {};

    void Reset() {
        Motion.Reset();
        Wheel.Reset();
        HWheel.Reset();
    }
};

struct InputValue {
    bool Down;
    DWORD Time;
    double Strength;

    InputValue(bool down, double strength, DWORD time) : Down(down), Strength(strength), Time(time) {}
};

struct ImplCustomKey {
    ImplInput Key;
    function<void(const InputValue &)> Callback;
};

struct ImplG {
    ImplUser Users[IMPL_MAX_USERS];
    ImplKeyboard Keyboard; // also includes mouse buttons, though...
    ImplMouse Mouse;
    vector<UniquePtr<ImplCustomKey>> CustomKeys;
    int ActiveUser = 0;
    int DefaultActiveUser = 0;
    bool InForeground = false;
    bool Paused = false;

    bool Trace = false;
    bool Debug = false;
    bool ApiTrace = false;
    bool ApiDebug = false;
    bool WaitDebugger = false;
    bool Forward = false;
    bool Always = false;
    bool Disable = false;
    bool HideCursor = false;
    bool InjectChildren = false;
    bool RumbleWindow = false;

    HINSTANCE HInstance = nullptr;
    DWORD DllThread = 0;
    HWND DllWindow = nullptr;

    CallbackList<void(ImplUser *, bool)> GlobalCallbacks;

    bool IsActive() { return InForeground || Always; }

    void Reset() {
        G.Trace = G.Debug = G.ApiTrace = G.ApiDebug = G.WaitDebugger = false;
        G.Forward = G.Always = G.Disable = G.HideCursor = false;
        G.InjectChildren = G.RumbleWindow = false;
        G.Keyboard.IsMapped = G.Mouse.IsMapped = false;

        for (int i = 0; i < IMPL_MAX_USERS; i++) {
            G.Users[i].Connected = G.Users[i].DeviceSpecified = false;
        }

        G.Keyboard.Reset();
        G.Mouse.Reset();
        G.ActiveUser = 0;

        for (auto &custom : CustomKeys) {
            custom->Key.Reset();
        }
    }
} G;

using ImplGlobalCb = decltype(ImplG::GlobalCallbacks)::CbIter;

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

template <class TCond>
static int ImplGetUsers(UINT *outMask, TCond &&cond, int minUser = 0) {
    UINT mask = 0;
    int count = 0;
    for (int i = minUser; i < IMPL_MAX_USERS; i++) {
        if (G.Users[i].Connected && cond(G.Users[i].Device)) {
            mask |= (1 << i);
            count++;
        }
    }

    *outMask = mask;
    return count;
}

static int ImplGetUsers(UINT *outMask, int minUser = 0) {
    return ImplGetUsers(
        outMask, [](DeviceIntf *device) { return true; }, minUser);
}

static int ImplNextUser(UINT *refMask) {
    DWORD userIdx = 0;
    BitScanForward(&userIdx, *refMask);
    *refMask &= ~(1 << userIdx);
    return userIdx;
}

ImplInput *ImplGetInput(int key, int userIndex) {
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

    case MY_VK_CUSTOM:
        if ((size_t)userIndex < G.CustomKeys.size()) {
            return &G.CustomKeys[userIndex]->Key;
        }
        break;
    }
    return nullptr;
}

int ImplChooseBestKeyInPair(int key) {
    auto [key1, key2] = GetKeyPair(key);
    int scan1 = MapVirtualKeyW(key1, MAPVK_VK_TO_VSC);
    int scan2 = MapVirtualKeyW(key2, MAPVK_VK_TO_VSC);
    return (!scan1 && scan2) ? key2 : key1;
}

#define CUSTOM_ASSERT_DLL_THREAD(assert) \
    assert(GetCurrentThreadId() == G.DllThread, "wrong thread in call")

#define DBG_ASSERT_DLL_THREAD() \
    CUSTOM_ASSERT_DLL_THREAD(DBG_ASSERT)
