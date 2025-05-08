#pragma once
#include "UtilsStr.h"
#include "UtilsPath.h"
#include "Keys.h"
#include "LogUtils.h"
#include "WinUtils.h"
#include <Windows.h>

#define IMPL_MAX_USERS 8
#define IMPL_MAX_SLOTS 31 // last bit in ImplBoolOutput.Slots

using user_t = int8_t;
using slot_t = uint8_t;

#pragma pack(push, 1) // just because it's often followed by more uint8_t's
struct ImplBoolOutput {
    unsigned Slots = 0;
    slot_t NumSlots = 0;

    slot_t AllocSlot() {
        if (NumSlots > IMPL_MAX_SLOTS) {
            return IMPL_MAX_SLOTS; // better than failing
        }

        return NumSlots++;
    }

    void UndoAllocSlot() { NumSlots--; }

    void Reset() {
        NumSlots = Slots = 0;
    }

    bool Get() { return Slots != 0; }
    slot_t NumActive() { return popcount(Slots); }
};
#pragma pack(pop)

struct ImplButtonState {
    ImplBoolOutput Pressed;
    bool State = false;

    void Reset() { Pressed.Reset(); }
};

template <double Default>
struct ImplDoubleOutput {
    double *Slots = nullptr;
    double Combined = Default;
    double Get() { return Combined; }

    bool InitIfNeeded(ImplBoolOutput &boolOutput, slot_t slot) {
        if (slot >= boolOutput.NumSlots) {
            return false;
        }

        if (!Slots) {
            int numDoubles = boolOutput.NumSlots;
            Slots = new double[numDoubles];
            for (int i = 0; i < numDoubles; i++) {
                Slots[i] = Default;
            }
        }
        return true;
    }

    void Reset() {
        Combined = Default;
        delete[] Slots;
        Slots = nullptr;
    }
};

using ImplStrengthOutput = ImplDoubleOutput<0.0>;
using ImplModifierOutput = ImplDoubleOutput<1.0>;

struct ImplTriggerState : public ImplButtonState {
    ImplStrengthOutput PressedStrength;
    ImplBoolOutput Modifier;
    ImplModifierOutput ModifierStrength;
    double Value = 0; // 0 .. 1
    double Threshold = 0.12;

    uint8_t Value8() const { return (uint8_t)nearbyint(Value * 0xff); }

    void Reset() {
        ImplButtonState::Reset();
        PressedStrength.Reset();
        Modifier.Reset();
        ModifierStrength.Reset();
    }
};

struct ImplAxisDirState {
    ImplBoolOutput Pressed;
    bool PrevPressedForRotate = false;
    ImplStrengthOutput PressedStrength;

    void Reset() {
        Pressed.Reset();
        PressedStrength.Reset();
    }
};

struct ImplAxisState {
    ImplAxisDirState Pos, Neg; // must be first (see union below)
    ImplBoolOutput Modifier;
    bool RotateFakePressed = false;
    uint8_t Weight = 0;
    ImplModifierOutput ModifierStrength;
    double RotateMultiplier = 1;
    double Value = 0; // -1 .. 1
    double Extent = 0;

    int8_t Value8() const { return (int8_t)nearbyint(Value * 0x7f); }
    int16_t Value16() const { return (int16_t)nearbyint(Value * 0x7fff); }

    void Reset() {
        Pos.Reset();
        Neg.Reset();
        Modifier.Reset();
        ModifierStrength.Reset();
    }

    tuple<double, uint8_t> GetStrengthAndWeight() {
        double strength = Pos.PressedStrength.Get();
        if (strength) {
            return make_tuple(strength, Pos.Pressed.NumActive());
        }
        strength = Neg.PressedStrength.Get();
        if (strength) {
            return make_tuple(-strength, Neg.Pressed.NumActive());
        }
        return make_tuple(0.0, 0);
    }
};

struct ImplAxesState {
    union {
        struct {
            ImplAxisDirState R, L;
        };
        ImplAxisState X = {};
    };
    union {
        struct {
            ImplAxisDirState U, D;
        };
        ImplAxisState Y = {};
    };
    ImplBoolOutput RotateModifier;
    ImplModifierOutput RotateModifierStrength;

    void Reset() {
        X.Reset();
        Y.Reset();
        RotateModifier.Reset();
        RotateModifierStrength.Reset();
    }
};

struct ImplMotionDimState {
    double NewValue = 0;
    double OldValue = 0;
    double Speed = 0;
    double Accel = 0;
    double GAccel = 0; // value in mult. of g
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

    ImplMotionState() { Y.GAccel = -1; } // gravity
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

    void Reset() {
        A.Reset();
        B.Reset();
        X.Reset();
        Y.Reset();
        LB.Reset();
        RB.Reset();
        L.Reset();
        R.Reset();
        DL.Reset();
        DR.Reset();
        DU.Reset();
        DD.Reset();
        Start.Reset();
        Back.Reset();
        Guide.Reset();
        Extra.Reset();
        LT.Reset();
        RT.Reset();
        LA.Reset();
        RA.Reset();
    }
};

struct DeviceIntf;

struct ImplUser {
    ImplState State;
    bool Connected = false;
    bool DeviceSpecified = false;
    DeviceIntf *Device = nullptr;
    CallbackList<bool(ImplUser *)> Callbacks;

    void Reset() {
        Connected = DeviceSpecified = false;
        State.Reset();
    }
};

using ImplUserCb = decltype(ImplUser::Callbacks)::CbIter;

struct ImplCond {
    int Key = 0;
    user_t User = -1;
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
    user_t SrcUser = -1;
    user_t DestUser = -1;
    slot_t DestSlot = 0;

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
    ImplBoolOutput Output;

    bool AsyncDown : 1 = false;
    bool AsyncToggle : 1 = false;
    bool ObservedPress : 1 = false;
    bool ObservedPressForCheck : 1 = false;

    void Reset() {
        Mappings = nullptr;
        Resets = nullptr;
        AsyncToggle = false;
        Output.Reset();
    }
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

struct ImplMouseMotionTotal {
    double X = 0, Y = 0;
    int Time = 0;
};

struct ImplMouse : public ImplDeviceBase {
    ImplMouseMotion Motion;
    ImplMouseAxis Wheel, HWheel;

    ImplMouseMotionTotal MotionTotal;

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
    ImplStrengthOutput Strength;
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

    // reset by ResetVars
    bool Trace, Debug, ApiTrace, ApiDebug, WaitDebugger, SpareForDebug;
    bool Forward, Always, Disable, HideCursor, RumbleWindow;
    bool InjectChildren;

    HINSTANCE HInstance = nullptr;
    HANDLE InitEvent = nullptr;
    DWORD DllThread = 0;
    HWND DllWindow = nullptr;

    CallbackList<bool(ImplUser *, bool, bool)> GlobalCallbacks;

    bool IsActive() { return InForeground || Always; }

    void Reset() {
        ResetVars();
        Keyboard.IsMapped = Mouse.IsMapped = false;

        for (int i = 0; i < IMPL_MAX_USERS; i++) {
            Users[i].Reset();
        }

        Keyboard.Reset();
        Mouse.Reset();
        ActiveUser = 0;

        for (auto &custom : CustomKeys) {
            custom->Key.Reset();
        }
    }

    ImplG() { ResetVars(); }

private:
    void ResetVars() {
        Trace = Debug = ApiTrace = ApiDebug = WaitDebugger = SpareForDebug = false;
        Forward = Always = Disable = HideCursor = RumbleWindow = false;
        InjectChildren = true;
    }
} G;

using ImplGlobalCb = decltype(ImplG::GlobalCallbacks)::CbIter;

#define CUSTOM_ASSERT_DLL_THREAD(assert) \
    assert(GetCurrentThreadId() == G.DllThread, "wrong thread in call")

#define DBG_ASSERT_DLL_THREAD() \
    CUSTOM_ASSERT_DLL_THREAD(DBG_ASSERT)
