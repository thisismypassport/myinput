#pragma once
#include "Impl.h"
#include "Hook.h"
#include "Header.h"
#include "WinInput.h"
#include "Thunk.h"

static LRESULT CALLBACK LowKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam) {
    DBG_ASSERT_DLL_THREAD();

    KBDLLHOOKSTRUCT *data = (KBDLLHOOKSTRUCT *)lParam;

    if (G.Trace) {
        LOG << "ll keyboard hook: " << nCode << ", " << data->vkCode << ", " << data->flags << END;
    }

    // we do the real processing here, and send raw keyboard events, since we can't cheaply
    // have both a raw keyboard registration and lowlevel event hook in the same process (for whatever bad reason)

    if (nCode >= 0) {
        bool injected = (data->flags & LLKHF_INJECTED) != 0;
        bool locallyInjected = injected && IsExtraInfoLocal(data->dwExtraInfo);

        if (!locallyInjected && ImplKeyboardHook(data->vkCode, !(data->flags & LLKHF_UP), !!(data->flags & LLKHF_EXTENDED), data->time)) {
            return 1;
        }

        if (ProcessRawKeyboardEvent((int)wParam, data->vkCode, data->scanCode, data->flags, (ULONG)data->dwExtraInfo, injected)) {
            return 1;
        }
    }

    return CallNextHookEx(G.Keyboard.HLowHook, nCode, wParam, lParam);
}

static bool ImplCheckMouse(WPARAM msg, MSLLHOOKSTRUCT *data) {
    switch (msg) {
    case WM_MOUSEMOVE:
        return ImplCheckMouseMotion();
    case WM_MOUSEWHEEL:
        return ImplCheckMouseWheel(false);
    case WM_MOUSEHWHEEL:
        return ImplCheckMouseWheel(true);
    case WM_LBUTTONDOWN:
        return ImplCheckMouseButton(VK_LBUTTON, true);
    case WM_LBUTTONUP:
        return ImplCheckMouseButton(VK_LBUTTON, false);
    case WM_RBUTTONDOWN:
        return ImplCheckMouseButton(VK_RBUTTON, true);
    case WM_RBUTTONUP:
        return ImplCheckMouseButton(VK_RBUTTON, false);
    case WM_MBUTTONDOWN:
        return ImplCheckMouseButton(VK_MBUTTON, true);
    case WM_MBUTTONUP:
        return ImplCheckMouseButton(VK_MBUTTON, false);
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        int xbtn = HIWORD(data->mouseData) == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1;
        return ImplCheckMouseButton(xbtn, msg == WM_XBUTTONDOWN);
    }

    return false;
}

static LRESULT CALLBACK LowMouseHook(int nCode, WPARAM wParam, LPARAM lParam) {
    DBG_ASSERT_DLL_THREAD();

    MSLLHOOKSTRUCT *data = (MSLLHOOKSTRUCT *)lParam;

    if (G.Trace) {
        LOG << "ll mouse hook: " << nCode << ", " << wParam << ", " << data->mouseData << ", " << data->flags << END;
    }

    // we do the real processing in the raw mouse event, here we just block events if needed

    if (nCode >= 0) {
        bool injected = (data->flags & LLMHF_INJECTED) != 0;
        bool locallyInjected = injected && IsExtraInfoLocal(data->dwExtraInfo);

        if (!locallyInjected && ImplCheckMouse(wParam, data)) {
            return 1;
        }
    }

    return CallNextHookEx(G.Mouse.HLowHook, nCode, wParam, lParam);
}

static void UpdateInForeground(bool allowUpdateAll = true) {
    bool inForeground = IsWindowInOurProcess(GetForegroundWindow());
    if (inForeground != G.InForeground) {
        ImplToggleForeground(allowUpdateAll);
        if (G.Debug) {
            LOG << "App " << (inForeground ? "entered" : "left") << " foreground" << END;
        }
    }
}

static unordered_map<DWORD, HHOOK> GForegroundHooks;

static LRESULT CALLBACK ForegroundHook(int nCode, WPARAM wParam, LPARAM lParam) // called in thread
{
    if (nCode >= 0) {
        auto msg = (CWPSTRUCT *)lParam;
        if (msg->message == WM_NCACTIVATE) {
            PostAppCallback([] {
                UpdateInForeground();
                UpdateHideCursor();
            });
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static void AddForegroundHook(HWND window) {
    DWORD thread = GetWindowThreadInOurProcess(window);
    if (thread) {
        auto &hook = GForegroundHooks[thread];
        if (!hook) {
            if (G.Debug) {
                LOG << "Setting foreground hook" << END;
            }
            hook = SetWindowsHookExW_Real(WH_CALLWNDPROC, ForegroundHook, nullptr, thread);
        }
    }
}

static void CALLBACK ObjectFocusHook(HWINEVENTHOOK hook, DWORD event, HWND window, LONG objId, LONG childId, DWORD thread, DWORD time) {
    if (event != EVENT_SYSTEM_FOREGROUND) {
        return;
    }

    // window is less up-to-date and sometimes wrong - ignoring it
    UpdateInForeground();
    UpdateHideCursor();

    // either it changed before we received the msg, or it's bugged and we need a hook to determine when it becomes truly foreground
    if (window != GetForegroundWindow()) {
        AddForegroundHook(window);
    }
}

static void WinHooksUpdateKeyboard(bool force = false) {
    DBG_ASSERT_DLL_THREAD();

    bool keyboard = G.Keyboard.IsMapped && G.IsActive();
    bool oldKeyboard = G.Keyboard.HLowHook != nullptr;
    if (keyboard != oldKeyboard || force) {
        if (oldKeyboard) {
            UnhookWindowsHookEx_Real(G.Keyboard.HLowHook);
        }

        G.Keyboard.HLowHook = nullptr;

        if (keyboard) {
            G.Keyboard.HLowHook = SetWindowsHookExW_Real(WH_KEYBOARD_LL, LowKeyboardHook, nullptr, 0);
        }
    }
}

static void WinHooksUpdateMouse(bool force = false) {
    DBG_ASSERT_DLL_THREAD();

    bool mouse = G.Mouse.IsMapped && G.IsActive();
    bool oldMouse = G.Mouse.HLowHook != nullptr;
    if (mouse != oldMouse || force) {
        if (oldMouse) {
            UnhookWindowsHookEx_Real(G.Mouse.HLowHook);
        }

        G.Mouse.HLowHook = nullptr;

        if (mouse) {
            G.Mouse.HLowHook = SetWindowsHookExW_Real(WH_MOUSE_LL, LowMouseHook, nullptr, 0);
        }
    }
}

struct WinHook {
    int Type;
    HOOKPROC Proc;
    DWORD OwnerThread;
    WeakAtomic<bool> Enabled;
    HOOKPROC Thunk;
};

static LRESULT AUGHOOKPROC_CALLCONV LowHookWrapper(AUGHOOKPROC_PARAMS(int nCode, WPARAM wParam, LPARAM lParam, void *hookData)) {
    WinHook *hook = (WinHook *)hookData;

    if (hook->Enabled) {
        switch (hook->Type) {
        case WH_KEYBOARD_LL: {
            KBDLLHOOKSTRUCT *data = (KBDLLHOOKSTRUCT *)lParam;

            if (G.Trace) {
                LOG << "app ll keyboard hook: " << nCode << ", " << data->vkCode << ", " << data->flags << END;
            }

            bool injected = data->flags & LLKHF_INJECTED;
            if (injected && data->dwExtraInfo == ExtraInfoOurInject) {
                data->flags &= ~LLKHF_INJECTED;
            }

            if (injected && IsExtraInfoLocal(data->dwExtraInfo)) {
                data->dwExtraInfo = GAppExtraInfo.GetOrig(data->dwExtraInfo);
            }
        } break;

        case WH_MOUSE_LL: {
            MSLLHOOKSTRUCT *data = (MSLLHOOKSTRUCT *)lParam;

            if (G.Trace) {
                LOG << "app ll mouse hook: " << nCode << ", " << wParam << ", " << data->mouseData << END;
            }

            bool injected = data->flags & LLMHF_INJECTED;
            if (injected && data->dwExtraInfo == ExtraInfoOurInject) {
                data->flags &= ~LLMHF_INJECTED;
            }

            if (injected && IsExtraInfoLocal(data->dwExtraInfo)) {
                data->dwExtraInfo = GAppExtraInfo.GetOrig(data->dwExtraInfo);
            }
        } break;

        case WH_MOUSE: {
            MOUSEHOOKSTRUCT *data = (MOUSEHOOKSTRUCT *)lParam;

            if (G.Trace) {
                LOG << "app mouse hook: " << nCode << ", " << wParam << END;
            }

            if (IsExtraInfoLocal(data->dwExtraInfo)) {
                data->dwExtraInfo = GAppExtraInfo.GetOrig(data->dwExtraInfo);
            }
        } break;
        }

        return hook->Proc(nCode, wParam, lParam);
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
};

class WinHooks {
    mutex mMutex;
    unordered_map<HHOOK, WinHook *> mByHandle;
    // reusing hooks is dangerous, but it's either that or leaking:
    deque<WinHook *> mFreeLLKeyHooks, mFreeLLMouseHooks, mFreeMouseHooks;

    deque<WinHook *> &GetFreeHooks(int type) {
        switch (type) {
        case WH_KEYBOARD_LL:
            return mFreeLLKeyHooks;
        case WH_MOUSE_LL:
            return mFreeLLMouseHooks;
        case WH_MOUSE:
            return mFreeMouseHooks;
            DEFAULT_UNREACHABLE;
        }
    }

    decltype(mByHandle)::iterator RemoveByIter(decltype(mByHandle)::iterator iter) {
        WinHook *hook = iter->second;
        hook->Enabled = false;
        GetFreeHooks(hook->Type).push_back(hook);

        return mByHandle.erase(iter);
    }

public:
    HHOOK Add(int type, HOOKPROC proc, HINSTANCE hmod, DWORD thread, DWORD ownerThread,
              decltype(SetWindowsHookExA) SetWindowsHookEx_Real, bool enable) {
        lock_guard<mutex> lock(mMutex);

        WinHook *hook;
        auto &freeHooks = GetFreeHooks(type);
        if (!freeHooks.empty()) {
            hook = freeHooks.front();
            freeHooks.pop_front();
        } else {
            hook = new WinHook();
            hook->Thunk = AllocHookProcThunk(LowHookWrapper, hook);
        }

        *hook = WinHook{type, proc, ownerThread, enable, hook->Thunk};

        HHOOK handle = SetWindowsHookEx_Real(type, hook->Thunk, hmod, thread);
        if (handle) {
            mByHandle[handle] = hook;
        } else {
            freeHooks.push_back(hook);
        }
        return handle;
    }

    void Enable(HHOOK handle) {
        lock_guard<mutex> lock(mMutex);
        auto iter = mByHandle.find(handle);
        if (iter != mByHandle.end()) {
            iter->second->Enabled = true;
        }
    }

    void Remove(HHOOK handle) {
        lock_guard<mutex> lock(mMutex);
        auto iter = mByHandle.find(handle);
        if (iter != mByHandle.end()) {
            RemoveByIter(iter);
        }
    }

    void RemoveForThread(DWORD threadId) {
        lock_guard<mutex> lock(mMutex);
        auto iter = mByHandle.begin();
        while (iter != mByHandle.end()) {
            if (iter->second->OwnerThread == threadId) {
                iter = RemoveByIter(iter);
            } else {
                iter++;
            }
        }
    }

    void DebugUnhookAll(int type) {
        lock_guard<mutex> lock(mMutex);
        for (auto &pair : mByHandle) {
            if (pair.second->Type == type) {
                UnhookWindowsHookEx_Real(pair.first);
            }
        }
    }

} GWinHooks;

// Only enable the hook after our main hook is back in the front

static void WinHooksPostKeyboardSet(void *data) {
    WinHooksUpdateKeyboard(true);
    GWinHooks.Enable((HHOOK)data);
}
static void WinHooksPostMouseSet(void *data) {
    WinHooksUpdateMouse(true);
    GWinHooks.Enable((HHOOK)data);
}

HHOOK WINAPI SetWindowsHookEx_Hook(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId,
                                   decltype(SetWindowsHookExA) SetWindowsHookEx_Real) {
    if (G.ApiDebug) {
        LOG << "SetWindowsHookEx " << idHook << " " << dwThreadId << END;
    }

    if ((idHook == WH_KEYBOARD_LL || idHook == WH_MOUSE_LL) ||
        (idHook == WH_MOUSE && dwThreadId)) {
        bool enable = (idHook == WH_MOUSE);
        DWORD ownerThread = GetCurrentThreadId();
        HHOOK handle = GWinHooks.Add(idHook, lpfn, hmod, dwThreadId, ownerThread, SetWindowsHookEx_Real, enable);
        if (handle && !enable) {
            PostAppCallback(idHook == WH_KEYBOARD_LL ? WinHooksPostKeyboardSet : WinHooksPostMouseSet, handle);
        }
        return handle;
    }
    return SetWindowsHookEx_Real(idHook, lpfn, hmod, dwThreadId);
}

HHOOK WINAPI SetWindowsHookExA_Hook(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    return SetWindowsHookEx_Hook(idHook, lpfn, hmod, dwThreadId, SetWindowsHookExA_Real);
}

HHOOK WINAPI SetWindowsHookExW_Hook(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    return SetWindowsHookEx_Hook(idHook, lpfn, hmod, dwThreadId, SetWindowsHookExW_Real);
}

BOOL WINAPI UnhookWindowsHookEx_Hook(HHOOK hhk) {
    GWinHooks.Remove(hhk);
    return UnhookWindowsHookEx_Real(hhk);
}

void WinHooksInitOnThread() {
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                    nullptr, ObjectFocusHook, 0, 0, WINEVENT_OUTOFCONTEXT);
    UpdateInForeground(false);
}

void WinHooksDetachThread(DWORD threadId) {
    GWinHooks.RemoveForThread(threadId);
}

void HookWinHooks() {
    ADD_GLOBAL_HOOK(SetWindowsHookExA);
    ADD_GLOBAL_HOOK(SetWindowsHookExW);
    ADD_GLOBAL_HOOK(UnhookWindowsHookEx);
}

void DebugRemoveLowHooks() // for debugging purposes
{
    UnhookWindowsHookEx_Real(G.Keyboard.HLowHook);
    UnhookWindowsHookEx_Real(G.Mouse.HLowHook);
    GWinHooks.DebugUnhookAll(WH_KEYBOARD_LL);
    GWinHooks.DebugUnhookAll(WH_MOUSE_LL);
}
