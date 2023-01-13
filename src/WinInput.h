#pragma once
#include "UtilsBase.h"
#include "Header.h"
#include "Keys.h"
#include "Hook.h"

struct ManualAsyncKeyState {
    enum { Count = 0x100 };
    WeakAtomic<bool> State[Count];
    WeakAtomic<bool> Sticky[Count];
    WeakAtomic<bool> Enabled = false;
} GManualAsync;

SHORT WINAPI GetAsyncKeyState_Hook(int vKey) {
    if (GManualAsync.Enabled &&
        vKey >= 0 && vKey < ManualAsyncKeyState::Count &&
        !IsVkMouseButton(vKey)) {
        if (G.ApiTrace) {
            LOG << "GetAsyncKeyState (manual)" << END;
        }

        bool state = GManualAsync.State[vKey];
        bool sticky = GManualAsync.Sticky[vKey].exchange(false);
        return (int)sticky | (((int)state) << 15);
    }

    return GetAsyncKeyState_Real(vKey);
}

void SetManualAsyncKeyState(bool enable) {
    if (GManualAsync.Enabled != enable) {
        if (enable) {
            for (int i = 0; i < ManualAsyncKeyState::Count; i++) {
                SHORT value = GetAsyncKeyState_Real(i);
                GManualAsync.State[i] = value < 0;
                GManualAsync.Sticky[i] = value & 1;
            }
        }

        GManualAsync.Enabled = enable;
    }
}

void UpdateManualAsyncKeyState(int key, int otherKey, bool down) {
    if (down) {
        GManualAsync.State[key] = GManualAsync.Sticky[key] = true;
    } else {
        GManualAsync.State[key] = false;
    }

    if (key != otherKey) {
        UpdateManualAsyncKeyState(otherKey, otherKey, down);
    }
}

class AppExtraInfo {
    mutex mMutex;
    WeakAtomic<WeakAtomic<ULONG_PTR> *> mExtraInfos = nullptr; // more than good enough?
    int mExtraInfoIdx = 0;

public:
    ULONG_PTR GetOurs(ULONG_PTR appInfo) {
        if (appInfo == 0) {
            return ExtraInfoAppDefault;
        }

        lock_guard<mutex> lock(mMutex);
        if (!mExtraInfos) {
            mExtraInfos = new WeakAtomic<ULONG_PTR>[ExtraInfoAppCustomCount] {};
        }

        int idx = mExtraInfoIdx++;
        if (mExtraInfoIdx == ExtraInfoAppCustomCount) {
            mExtraInfoIdx = 0;
        }
        mExtraInfos[idx] = appInfo;
        return ExtraInfoAppCustomStart + idx;
    }

    ULONG_PTR GetOrig(ULONG_PTR ourInfo) {
        if (ourInfo >= ExtraInfoAppCustomStart && ourInfo < ExtraInfoAppCustomStart + ExtraInfoAppCustomCount && mExtraInfos) {
            return mExtraInfos[ourInfo - ExtraInfoAppCustomStart];
        } else {
            return 0;
        }
    }

} GAppExtraInfo;

void WINAPI KeybdEvent_Hook(BYTE bVk, BYTE bScan, DWORD dwFlags, ULONG_PTR dwExtraInfo) {
    KeybdEvent_Real(bVk, bScan, dwFlags, GAppExtraInfo.GetOurs(dwExtraInfo));
}

void WINAPI MouseEvent_Hook(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo) {
    MouseEvent_Real(dwFlags, dx, dy, dwData, GAppExtraInfo.GetOurs(dwExtraInfo));
}

UINT WINAPI SendInput_Hook(UINT count, LPINPUT inputs, int cbSize) {
    INPUT *copy = nullptr;
    if (count && inputs && cbSize == sizeof(INPUT)) {
        copy = new INPUT[count];
        CopyMemory(copy, inputs, count * cbSize);

        for (UINT i = 0; i < count; i++) {
            switch (copy[i].type) {
            case INPUT_KEYBOARD:
                copy[i].ki.dwExtraInfo = GAppExtraInfo.GetOurs(copy[i].ki.dwExtraInfo);
                break;
            case INPUT_MOUSE:
                copy[i].mi.dwExtraInfo = GAppExtraInfo.GetOurs(copy[i].mi.dwExtraInfo);
                break;
            }
        }
    }

    INPUT *realInputs = copy ? copy : inputs;
    UINT result = SendInput_Real(count, realInputs, cbSize);

    if (copy) {
        delete[] copy;
    }

    return result;
}

bool IsExtraInfoLocal(ULONG_PTR extraInfo) {
    return (extraInfo & ExtraInfoLocalPageMask) == ExtraInfoLocalPage;
}

LPARAM WINAPI GetMessageExtraInfo_Hook() {
    LPARAM result = GetMessageExtraInfo_Real();

    if (IsExtraInfoLocal(result)) {
        result = GAppExtraInfo.GetOrig(result);
    }

    return result;
}

static void UpdateCurrentInputMessageSource(INPUT_MESSAGE_SOURCE *msg) {
    if (msg->originId == IMO_INJECTED &&
        GetMessageExtraInfo_Real() == ExtraInfoOurInject) {
        msg->originId = IMO_HARDWARE;
    }
}

BOOL WINAPI GetCurrentInputMessageSource_Hook(INPUT_MESSAGE_SOURCE *msg) {
    BOOL success = GetCurrentInputMessageSource_Real(msg);
    if (success && msg) {
        UpdateCurrentInputMessageSource(msg);
    }
    return success;
}

BOOL WINAPI GetCIMSSM_Hook(INPUT_MESSAGE_SOURCE *msg) {
    BOOL success = GetCIMSSM_Real(msg);
    if (success && msg) {
        UpdateCurrentInputMessageSource(msg);
    }
    return success;
}

void HookWinInput() {
    AddGlobalHook(&GetAsyncKeyState_Real, GetAsyncKeyState_Hook);
    AddGlobalHook(&KeybdEvent_Real, KeybdEvent_Hook);
    AddGlobalHook(&MouseEvent_Real, MouseEvent_Hook);
    AddGlobalHook(&SendInput_Real, SendInput_Hook);
    AddGlobalHook(&GetMessageExtraInfo_Real, GetMessageExtraInfo_Hook);
    AddGlobalHook(&GetCurrentInputMessageSource_Real, GetCurrentInputMessageSource_Hook);
    AddGlobalHook(&GetCIMSSM_Real, GetCIMSSM_Hook);
}
