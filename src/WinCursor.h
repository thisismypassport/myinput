#pragma once
#include "Header.h"
#include "Hook.h"
#include "State.h"

struct MouseCapture {
    struct ThreadHook {
        DWORD Thread = 0;
        HHOOK HHook = nullptr;
        int ShowCursorValue = 0;
        bool Started = false;
        bool NeedFinish = false;
    };

    DWORD CaptureThread = 0;
    POINT OldPos = {0, 0};
    WeakAtomic<DWORD> PrevThread{0};
    WeakAtomic<HWND> PrevWindow{nullptr};

    mutex HooksMutex;
    vector<ThreadHook> Hooks; // size 0 or 1 most of the time
    RECT ClipCursorRect = {};
    bool ClipCursorSet = false;
    bool ClipCursorOverride = false;
} GCapture;

static RECT GetMaxCursorRect() {
    RECT rect;
    rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rect;
}

static void MouseCaptureStart(MouseCapture::ThreadHook &info, DWORD threadId) {
    // affect thread state
    info.Started = true;

    info.ShowCursorValue = ShowCursor_Real(false) + 1;
    for (int i = 0; i < info.ShowCursorValue; i++) {
        ShowCursor_Real(false);
    }

    if (!info.NeedFinish) // affect global state, unless someone else took over already
    {
        GCapture.PrevThread = threadId;

        GetCursorPos(&GCapture.OldPos);

        GCapture.ClipCursorOverride = GetClipCursor_Real(&GCapture.ClipCursorRect);
        if (GCapture.ClipCursorOverride) {
            auto maxRect = GetMaxCursorRect();
            GCapture.ClipCursorSet =
                GCapture.ClipCursorRect.left > maxRect.left ||
                GCapture.ClipCursorRect.top > maxRect.top ||
                GCapture.ClipCursorRect.right < maxRect.right ||
                GCapture.ClipCursorRect.bottom < maxRect.bottom;
        }
    }
}

static void MouseCaptureStop(const MouseCapture::ThreadHook &info) {
    UnhookWindowsHookEx_Real(info.HHook);

    if (info.Started) {
        // affect thread state

        int newValue = ShowCursor_Real(info.ShowCursorValue >= 0);
        for (int i = newValue; i < info.ShowCursorValue; i++) {
            ShowCursor_Real(true);
        }
        for (int i = newValue; i > info.ShowCursorValue; i--) {
            ShowCursor_Real(false);
        }

        if (GCapture.PrevThread == 0) // affect global state, unless someone else took it already
        {
            if (GCapture.ClipCursorOverride) {
                ClipCursor_Real(GCapture.ClipCursorSet ? &GCapture.ClipCursorRect : nullptr);
            }

            SetCursorPos(GCapture.OldPos.x, GCapture.OldPos.y);
        }
    }
}

static void MouseCaptureUpdate(HWND activeWindow) {
    RECT r = {};
    GetWindowRect(activeWindow, &r);
    ClipCursor_Real(&r);
    if (G.Debug) {
        LOG << "set cursor rect to " << r.left << ".." << r.right << ", " << r.top << ".." << r.bottom << END;
    }
    GetClipCursor_Real(&r);

    POINT pos = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    SetCursorPos(pos.x, pos.y);
}

static LRESULT CALLBACK MouseCaptureHook(int nCode, WPARAM wParam, LPARAM lParam) // called in thread
{
    if (nCode >= 0) {
        MSG *msg = (MSG *)lParam;

        bool start = false;
        bool finish = false;
        int threadId = GetCurrentThreadId();
        HWND activeWindow = GetActiveWindow();
        bool changed = false;

        if (GCapture.PrevThread != threadId) {
            lock_guard<mutex> lock(GCapture.HooksMutex);
            auto &threadHooks = GCapture.Hooks;
            for (int i = (int)threadHooks.size() - 1; i >= 0; i--) {
                auto &info = threadHooks[i];
                if (info.Thread == threadId) {
                    if (info.NeedFinish) {
                        finish = true;
                        MouseCaptureStop(info);
                        threadHooks.erase(threadHooks.begin() + i);
                    } else if (!info.Started && activeWindow) {
                        start = true;
                        MouseCaptureStart(info, threadId);
                    }
                    break;
                }
            }
        }

        if (!finish) {
            if (activeWindow && activeWindow != GCapture.PrevWindow) {
                changed = true;
                GCapture.PrevWindow = activeWindow;
            }

            if (start || changed ||
                (activeWindow && msg->hwnd == activeWindow &&
                 (msg->message == WM_SIZE || msg->message == WM_MOVE || msg->message == WM_MOUSEMOVE))) {
                MouseCaptureUpdate(activeWindow);
            }
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void WinCursorDetachThread(DWORD threadId) {
    lock_guard<mutex> lock(GCapture.HooksMutex);
    auto &threadHooks = GCapture.Hooks;
    for (int i = (int)threadHooks.size() - 1; i >= 0; i--) {
        auto &info = threadHooks[i];
        if (info.Thread == threadId) {
            MouseCaptureStop(info);
            threadHooks.erase(threadHooks.begin() + i);
            break;
        }
    }
}

static void UpdateHideCursor() {
    DBG_ASSERT_DLL_THREAD();

    bool wantCapture = G.HideCursor && !G.Disable;
    DWORD captureThread = wantCapture ? GetWindowThreadInOurProcess(GetForegroundWindow()) : 0;

    DWORD oldCaptureThread = GCapture.CaptureThread;
    if (captureThread != oldCaptureThread) {
        if (oldCaptureThread) {
            lock_guard<mutex> lock(GCapture.HooksMutex);
            for (auto &hook : GCapture.Hooks) {
                hook.NeedFinish = true;
            }
            GCapture.PrevThread = 0;
            GCapture.PrevWindow = nullptr;
        }

        GCapture.CaptureThread = captureThread;

        if (G.Debug) {
            LOG << "mouse thread changed to: " << captureThread << END;
        }

        if (captureThread) {
            lock_guard<mutex> lock(GCapture.HooksMutex);
            HHOOK hook = SetWindowsHookExW_Real(WH_GETMESSAGE, MouseCaptureHook, nullptr, captureThread);
            GCapture.Hooks.push_back({captureThread, hook});
        }
    }
}

int WINAPI ShowCursor_Hook(BOOL show) {
    DWORD thread = GetCurrentThreadId();

    {
        lock_guard<mutex> lock(GCapture.HooksMutex);
        for (auto &hook : GCapture.Hooks) {
            if (hook.Thread == thread && hook.Started) {
                if (show) {
                    hook.ShowCursorValue++;
                } else {
                    hook.ShowCursorValue--;
                }

                return hook.ShowCursorValue;
            }
        }
    }

    return ShowCursor_Real(show);
}

BOOL WINAPI GetCursorInfo_Hook(PCURSORINFO pci) {
    BOOL success = GetCursorInfo_Real(pci);
    if (success && pci) {
        // allow other threads to see reality
        DWORD thread = GetCurrentThreadId();
        POINT cursorPos;
        HWND cursorWin;

        lock_guard<mutex> lock(GCapture.HooksMutex);
        for (auto &hook : GCapture.Hooks) {
            if (hook.Thread == thread && hook.Started &&
                !(pci->flags & CURSOR_SHOWING) &&
                GetCursorPos(&cursorPos) &&
                (cursorWin = WindowFromPoint(cursorPos)) != nullptr &&
                GetWindowThreadProcessId(cursorWin, nullptr) == thread) {
                pci->flags |= CURSOR_SHOWING;
                break;
            }
        }
    }
    return success;
}

BOOL WINAPI ClipCursor_Hook(const RECT *lpRect) {
    {
        lock_guard<mutex> lock(GCapture.HooksMutex);
        if (GCapture.ClipCursorOverride) {
            GCapture.ClipCursorSet = lpRect != nullptr;
            if (lpRect) {
                GCapture.ClipCursorRect = *lpRect;
            }
            return TRUE;
        }
    }
    return ClipCursor_Real(lpRect);
}

BOOL WINAPI GetClipCursor_Hook(LPRECT lpRect) {
    BOOL success = GetClipCursor_Real(lpRect);
    if (success && lpRect) {
        lock_guard<mutex> lock(GCapture.HooksMutex);
        if (GCapture.ClipCursorOverride) {
            *lpRect = GCapture.ClipCursorSet ? GCapture.ClipCursorRect : GetMaxCursorRect();
        }
    }
    return success;
}

void HookWinCursor() {
    AddGlobalHook(&ShowCursor_Real, ShowCursor_Hook);
    AddGlobalHook(&GetCursorInfo_Real, GetCursorInfo_Hook);
    AddGlobalHook(&ClipCursor_Real, ClipCursor_Hook);
    AddGlobalHook(&GetClipCursor_Real, GetClipCursor_Hook);
}
