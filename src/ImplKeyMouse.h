#pragma once
#include "StateUtils.h"

static int ImplUnextend(int virtKeyCode, bool extended) {
    switch (virtKeyCode) {
    case VK_INSERT:
        return extended ? virtKeyCode : VK_NUMPAD0;
    case VK_END:
        return extended ? virtKeyCode : VK_NUMPAD1;
    case VK_DOWN:
        return extended ? virtKeyCode : VK_NUMPAD2;
    case VK_NEXT:
        return extended ? virtKeyCode : VK_NUMPAD3;
    case VK_LEFT:
        return extended ? virtKeyCode : VK_NUMPAD4;
    case VK_CLEAR:
        return extended ? virtKeyCode : VK_NUMPAD5;
    case VK_RIGHT:
        return extended ? virtKeyCode : VK_NUMPAD6;
    case VK_HOME:
        return extended ? virtKeyCode : VK_NUMPAD7;
    case VK_UP:
        return extended ? virtKeyCode : VK_NUMPAD8;
    case VK_PRIOR:
        return extended ? virtKeyCode : VK_NUMPAD9;
    case VK_DELETE:
        return extended ? virtKeyCode : VK_DECIMAL;
    case VK_RETURN:
        return extended ? MY_VK_NUMPAD_RETURN : virtKeyCode;
    case VK_CANCEL:
        return extended ? VK_PAUSE : VK_SCROLL;
    default:
        return virtKeyCode;
    }
}

// TODO: probably wrong in some scenarios - esp. GetKeyState?
static bool IsControlOn() { return GetAsyncKeyState(VK_CONTROL) < 0; }
static bool IsNumpadOn() { return (GetKeyState(VK_NUMLOCK) & 1) != 0; }

static int ImplReextend(int virtKeyCode, bool *outExtended) {
    *outExtended = false;
    switch (virtKeyCode) {
    case VK_INSERT:
    case VK_END:
    case VK_DOWN:
    case VK_NEXT:
    case VK_LEFT:
    case VK_CLEAR:
    case VK_RIGHT:
    case VK_HOME:
    case VK_UP:
    case VK_PRIOR:
    case VK_DELETE:
    case VK_DIVIDE:
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_SNAPSHOT:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
        // not sure if applies to all of these:
    case VK_MEDIA_NEXT_TRACK:
    case VK_MEDIA_PREV_TRACK:
    case VK_MEDIA_PLAY_PAUSE:
    case VK_MEDIA_STOP:
    case VK_VOLUME_UP:
    case VK_VOLUME_DOWN:
    case VK_VOLUME_MUTE:
    case VK_BROWSER_HOME:
    case VK_BROWSER_BACK:
    case VK_BROWSER_FORWARD:
    case VK_BROWSER_REFRESH:
    case VK_BROWSER_STOP:
    case VK_BROWSER_SEARCH:
    case VK_BROWSER_FAVORITES:
    case VK_LAUNCH_MAIL:
    case VK_LAUNCH_MEDIA_SELECT:
    case VK_LAUNCH_APP1:
    case VK_LAUNCH_APP2:
        *outExtended = true;
        return virtKeyCode;

    case MY_VK_NUMPAD_RETURN:
        *outExtended = true;
        return VK_RETURN;

    case VK_NUMPAD0:
        return IsNumpadOn() ? virtKeyCode : VK_INSERT;
    case VK_NUMPAD1:
        return IsNumpadOn() ? virtKeyCode : VK_END;
    case VK_NUMPAD2:
        return IsNumpadOn() ? virtKeyCode : VK_DOWN;
    case VK_NUMPAD3:
        return IsNumpadOn() ? virtKeyCode : VK_NEXT;
    case VK_NUMPAD4:
        return IsNumpadOn() ? virtKeyCode : VK_LEFT;
    case VK_NUMPAD5:
        return IsNumpadOn() ? virtKeyCode : VK_CLEAR;
    case VK_NUMPAD6:
        return IsNumpadOn() ? virtKeyCode : VK_RIGHT;
    case VK_NUMPAD7:
        return IsNumpadOn() ? virtKeyCode : VK_HOME;
    case VK_NUMPAD8:
        return IsNumpadOn() ? virtKeyCode : VK_UP;
    case VK_NUMPAD9:
        return IsNumpadOn() ? virtKeyCode : VK_PRIOR;
    case VK_DECIMAL:
        return IsNumpadOn() ? virtKeyCode : VK_DELETE;

    case VK_SCROLL:
        return IsControlOn() ? VK_CANCEL : virtKeyCode;
    case VK_PAUSE:
        *outExtended = IsControlOn();
        return *outExtended ? VK_CANCEL : virtKeyCode;

    default:
        return virtKeyCode;
    }
}

static void ImplSplitLeftRight(int *ptrKey, int *ptrFlags, int extFlag, int scanCode) {
    switch (*ptrKey) {
    case VK_SHIFT:
        *ptrKey = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
        if (*ptrKey == VK_RSHIFT) {
            *ptrFlags |= extFlag;
        } else if (*ptrKey != VK_LSHIFT) {
            *ptrKey = VK_LSHIFT; // just in case
        }
        break;

    case VK_CONTROL:
        *ptrKey = (*ptrFlags & extFlag) ? VK_RCONTROL : VK_LCONTROL;
        break;
    case VK_MENU:
        *ptrKey = (*ptrFlags & extFlag) ? VK_RMENU : VK_LMENU;
        break;
    }
}

static void ImplCombineLeftRight(int *ptrKey, int *ptrFlags, int extFlag) {
    switch (*ptrKey) {
    case VK_LSHIFT:
    case VK_RSHIFT:
        *ptrKey = VK_SHIFT;
        *ptrFlags &= ~extFlag;
        break;
    case VK_LCONTROL:
    case VK_RCONTROL:
        *ptrKey = VK_CONTROL;
        break;
    case VK_LMENU:
    case VK_RMENU:
        *ptrKey = VK_MENU;
        break;
    }
}

#ifndef IMPL_KEY_MOUSE_UTILS_ONLY

static int ImplKeyboardDelayTime() {
    int delay = 0;
    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0);
    return (delay + 1) * 250;
}

static int ImplKeyboardRepeatTime() {
    int speed = 0;
    SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0);
    return 400 - speed * 12;
}

static BufferListOfSize<sizeof(INPUT)> GImplInputBuffers;
static ReusableThread GImplInputThread{InputThreadPriority}; // separate from dll thread to avoid recursive hook calls

static DWORD WINAPI ImplSendInputDelayed(void *param) {
    SendInput_Real(1, (INPUT *)param, sizeof(INPUT));
    GImplInputBuffers.Get()->PutBack(param);
    return 0;
}

static void ImplGenerateMouseEventCommon(int flag, DWORD time, int data = 0) {
    INPUT *input = (INPUT *)GImplInputBuffers.Get()->Take();
    input->type = INPUT_MOUSE;
    input->mi = {};
    input->mi.dwFlags = flag;
    input->mi.mouseData = data;
    input->mi.time = time;
    input->mi.dwExtraInfo = ExtraInfoOurInject;

    GImplInputThread.CreateThread(ImplSendInputDelayed, input);
}

static void ImplGenerateKey(int key, bool down, DWORD time) {
    switch (key) {
    case VK_LBUTTON:
        return ImplGenerateMouseEventCommon(down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP, time);
    case VK_RBUTTON:
        return ImplGenerateMouseEventCommon(down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP, time);
    case VK_MBUTTON:
        return ImplGenerateMouseEventCommon(down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, time);
    case VK_XBUTTON1:
        return ImplGenerateMouseEventCommon(down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, time, XBUTTON1);
    case VK_XBUTTON2:
        return ImplGenerateMouseEventCommon(down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, time, XBUTTON2);
    }

    bool extended;
    key = ImplReextend(key, &extended);
    int scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);

    INPUT *input = (INPUT *)GImplInputBuffers.Get()->Take();
    input->type = INPUT_KEYBOARD;
    input->ki = {};
    input->ki.wVk = key;
    input->ki.wScan = scan;
    if (!down) {
        input->ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    if (extended) {
        input->ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    input->ki.time = time;
    input->ki.dwExtraInfo = ExtraInfoOurInject;

    GImplInputThread.CreateThread(ImplSendInputDelayed, input);
}

static void ImplHandleKeyChange(int key, bool down, DWORD time, slot_t slot) {
    auto &output = G.Keyboard.Keys[key].Output;
    ImplProcessBoolOutput(output, down, slot);
    ImplGenerateKey(key, output.Get(), time);
}

static void ImplGenerateMouseWheel(int flag, double strength, DWORD time) {
    int data = (int)strength * WHEEL_DELTA;
    ImplGenerateMouseEventCommon(flag, time, data);
}

static void ImplGenerateMouseMotion(double dx, double dy, DWORD time, ChangedMask *changes) {
    G.Mouse.MotionTotal.X += dx;
    G.Mouse.MotionTotal.Y += dy;
    G.Mouse.MotionTotal.Time = time;
    changes->ChangeMouseMotion();
}

static int RoundAway(double value) { return value > 0 ? (int)ceil(value) : (int)floor(value); }

static void ImplGenerateMouseMotionFinish() {
    INPUT *input = (INPUT *)GImplInputBuffers.Get()->Take();
    input->type = INPUT_MOUSE;
    input->mi = {};
    input->mi.dwFlags = MOUSEEVENTF_MOVE;
    input->mi.dx = RoundAway(G.Mouse.MotionTotal.X);
    input->mi.dy = RoundAway(G.Mouse.MotionTotal.Y);
    input->mi.time = G.Mouse.MotionTotal.Time;
    input->mi.dwExtraInfo = ExtraInfoOurInject;

    GImplInputThread.CreateThread(ImplSendInputDelayed, input);

    G.Mouse.MotionTotal = {};
}

static void ImplUpdateAsyncState() {
    for (int i = 0; i < ImplKeyboard::Count; i++) {
        G.Keyboard.Keys[i].AsyncDown = GetAsyncKeyState_Real(i) < 0;
        // AsyncToggle?
    }
}

#endif
