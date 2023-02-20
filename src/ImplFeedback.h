#pragma once
#include "Header.h"

struct WindowRumble {
    double LowSpeed;
    double HighSpeed;
    UINT_PTR LowTimer = 0;
    UINT_PTR HighTimer = 0;
    HWND PrevWindow = nullptr;
    int DX = 0, DY = 0;
} *GRumble = nullptr;

static void AdjustWindow(HWND window, int dx, int dy) {
    if (dx || dy) {
        RECT windowRect = {};
        GetWindowRect(window, &windowRect);

        SetWindowPos(window, nullptr, windowRect.left + dx, windowRect.top + dy, 0, 0,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
    }
}

static void RumbleWindow(WindowRumble *rumble, int dx, int dy) {
    HWND window = GetForegroundWindow();
    if (window != rumble->PrevWindow) {
        if (rumble->PrevWindow) {
            AdjustWindow(rumble->PrevWindow, -rumble->DX, -rumble->DY);
            rumble->PrevWindow = nullptr;
            rumble->DX = rumble->DY = 0;
        }

        if (!IsWindowInOurProcess(window)) {
            return;
        }

        rumble->PrevWindow = window;
    }

    if (dx) {
        int oldDx = rumble->DX;
        if (oldDx > 0) {
            dx = -dx;
        }
        rumble->DX = dx;
        if (oldDx) {
            dx *= 2;
        }
    }
    if (dy) {
        int oldDy = rumble->DY;
        if (oldDy > 0) {
            dy = -dy;
        }
        rumble->DY = dy;
        if (oldDy) {
            dy *= 2;
        }
    }

    AdjustWindow(rumble->PrevWindow, dx, dy);
}

void CALLBACK RumbleWindowLowCb(HWND, UINT, UINT_PTR rumble, DWORD time) {
    if (GRumble) {
        RumbleWindow(GRumble, max((int)(GRumble->LowSpeed * 10), 1), 0);
    }
}

void CALLBACK RumbleWindowHighCb(HWND, UINT, UINT_PTR rumble, DWORD time) {
    if (GRumble) {
        RumbleWindow(GRumble, 0, max((int)(GRumble->HighSpeed * 10), 1));
    }
}

void RumbleWindowStartCb(void *param) {
    if (GRumble) {
        if (GRumble->LowTimer) {
            KillTimer(nullptr, GRumble->LowTimer);
        }
        if (GRumble->HighTimer) {
            KillTimer(nullptr, GRumble->HighTimer);
        }

        if (GRumble->PrevWindow) {
            AdjustWindow(GRumble->PrevWindow, -GRumble->DX, -GRumble->DY);
        }

        delete GRumble;
    }

    GRumble = (WindowRumble *)param;

    if (GRumble) {
        if (GRumble->LowSpeed) {
            GRumble->LowTimer = SetTimer(nullptr, 0, 100, RumbleWindowLowCb);
        }
        if (GRumble->HighSpeed) {
            GRumble->HighTimer = SetTimer(nullptr, 0, 25, RumbleWindowHighCb);
        }
    }
}

void ShowWindowRumble(double lowFreq, double highFreq) {
    auto rumble = (lowFreq || highFreq) ? new WindowRumble{lowFreq, highFreq} : nullptr;
    PostAppCallback(RumbleWindowStartCb, rumble);
}
