// Compiles MyInput's mapping engine for macOS and exposes it through Bridge.h.
// The engine headers are the Windows ones, unmodified: mac/compat holds symlinks to them,
// next to stand-ins for the Windows-only headers they include.
#include "Impl.h"
#include "Log.h"
#include "Bridge.h"
#include <mach/mach_time.h>
#include <pwd.h>

using namespace MyInputMac;

static Host GHost;
static bool GKeyDown[ImplKeyboard::Count];
static bool GSyncPadsPending;

// ---- Win32 functions the engine calls ----

DWORD GetTickCount() {
    static mach_timebase_info_data_t timebase;
    if (!timebase.denom) {
        mach_timebase_info(&timebase);
    }
    return (DWORD)(mach_absolute_time() * timebase.numer / timebase.denom / NSEC_PER_MSEC);
}

SHORT GetAsyncKeyState_Real(int vk) {
    return vk > 0 && vk < ImplKeyboard::Count && GKeyDown[vk] ? (SHORT)0x8000 : 0;
}

SHORT GetAsyncKeyState(int vk) {
    switch (vk) {
    case VK_SHIFT:
        return GetAsyncKeyState_Real(VK_LSHIFT) | GetAsyncKeyState_Real(VK_RSHIFT);
    case VK_CONTROL:
        return GetAsyncKeyState_Real(VK_LCONTROL) | GetAsyncKeyState_Real(VK_RCONTROL);
    case VK_MENU:
        return GetAsyncKeyState_Real(VK_LMENU) | GetAsyncKeyState_Real(VK_RMENU);
    default:
        return GetAsyncKeyState_Real(vk);
    }
}

SHORT GetKeyState(int vk) {
    // Mac keyboards have no Num Lock: the numpad always types digits.
    return vk == VK_NUMLOCK ? 1 : GetAsyncKeyState(vk);
}

UINT MapVirtualKeyW(UINT code, UINT mapType) {
    // Only used to ask "does this key exist on the keyboard?" (non-zero scan code).
    return mapType == MAPVK_VK_TO_VSC ? 1 : 0;
}

bool SystemParametersInfoW(UINT action, UINT param, void *data, UINT flags) {
    switch (action) {
    case SPI_GETKEYBOARDDELAY:
        *(int *)data = 1; // 500ms before a generated key starts repeating
        return true;
    case SPI_GETKEYBOARDSPEED:
        *(int *)data = 31; // then every 28ms
        return true;
    default:
        return false;
    }
}

DWORD GetLastError() { return 0; }

bool LoadLibraryExW(const wchar_t *path, HANDLE file, DWORD flags) {
    LOG_W << "ERROR: Plugins aren't supported on macOS yet" << END;
    return false;
}

UINT SendInput_Real(UINT count, INPUT *inputs, int size) {
    for (UINT i = 0; i < count; i++) {
        const INPUT &input = inputs[i];
        Output output = {};

        if (input.type == INPUT_KEYBOARD) {
            output.Kind = OutputKind::Key;
            output.Vk = ImplUnextend(input.ki.wVk, (input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0);
            output.Down = !(input.ki.dwFlags & KEYEVENTF_KEYUP);
            GHost.SendOutput(output);
            continue;
        }

        DWORD flags = input.mi.dwFlags;
        auto button = [&](DWORD downFlag, DWORD upFlag, int vk) {
            if (flags & (downFlag | upFlag)) {
                output.Kind = OutputKind::MouseButton;
                output.Vk = vk;
                output.Down = (flags & downFlag) != 0;
                GHost.SendOutput(output);
            }
        };
        button(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, VK_LBUTTON);
        button(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, VK_RBUTTON);
        button(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, VK_MBUTTON);
        button(MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, input.mi.mouseData == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1);

        if (flags & (MOUSEEVENTF_WHEEL | MOUSEEVENTF_HWHEEL)) {
            output.Kind = (flags & MOUSEEVENTF_WHEEL) ? OutputKind::Wheel : OutputKind::HWheel;
            output.Delta = (int)input.mi.mouseData;
            GHost.SendOutput(output);
        }
        if (flags & MOUSEEVENTF_MOVE) {
            output.Kind = OutputKind::MouseMove;
            output.DX = input.mi.dx;
            output.DY = input.mi.dy;
            GHost.SendOutput(output);
        }
    }
    return count;
}

// ---- Services the engine expects from its host (MyInputHook.cpp on Windows) ----

struct PostedCallback {
    AppCallback Callback;
    void *Data;
};

void PostAppCallback(AppCallback cb, void *data) {
    dispatch_async_f(dispatch_get_main_queue(), new PostedCallback{cb, data}, [](void *param) {
        auto posted = (PostedCallback *)param;
        posted->Callback(posted->Data);
        delete posted;
    });
}

void PostAppCallback(VoidCallback cb) {
    PostAppCallback([](void *data) { ((VoidCallback)data)(); }, (void *)cb);
}

void UpdateCursor() {
    bool wantCapture = (G.HideCursor || G.BoundCursor) && !G.Disable && G.InForeground;
    GHost.SetCursorCapture(wantCapture && G.HideCursor, wantCapture && !G.HideCursor);
}

void UpdateAll() {
    G.Paused = !G.IsActive();
    UpdateCursor();
    ImplUpdateAsyncState();
}

void ShowWindowRumble(double lowFreq, double highFreq) {
    static UniqueLog once;
    if (once) {
        LOG_W << "ERROR: RumbleWindow isn't supported on macOS yet" << END;
    }
}

// ---- Virtual gamepads ----

static void PushPadState(int index) {
    ImplUser &user = G.Users[index];
    PadState out = {};
    {
        lock_guard<mutex> lock(user.State.Mutex);
        ImplState &in = user.State;
        out.A = in.A.State, out.B = in.B.State, out.X = in.X.State, out.Y = in.Y.State;
        out.LB = in.LB.State, out.RB = in.RB.State, out.L = in.L.State, out.R = in.R.State;
        out.DL = in.DL.State, out.DR = in.DR.State, out.DU = in.DU.State, out.DD = in.DD.State;
        out.Start = in.Start.State, out.Back = in.Back.State;
        out.Guide = in.Guide.State, out.Extra = in.Extra.State;
        out.LT = in.LT.Value, out.RT = in.RT.Value;
        out.LX = in.LA.X.Value, out.LY = in.LA.Y.Value;
        out.RX = in.RA.X.Value, out.RY = in.RA.Y.Value;
    }

    if (G.Trace) {
        LOG << "Gamepad " << index + 1 << ": L=" << out.LX << "," << out.LY << " R=" << out.RX << "," << out.RY
            << " LT=" << out.LT << " RT=" << out.RT << " buttons="
            << (out.A ? "A " : "") << (out.B ? "B " : "") << (out.X ? "X " : "") << (out.Y ? "Y " : "")
            << (out.LB ? "LB " : "") << (out.RB ? "RB " : "") << (out.L ? "L " : "") << (out.R ? "R " : "")
            << (out.DL ? "DL " : "") << (out.DR ? "DR " : "") << (out.DU ? "DU " : "") << (out.DD ? "DD " : "")
            << (out.Start ? "Start " : "") << (out.Back ? "Back " : "")
            << (out.Guide ? "Guide " : "") << (out.Extra ? "Extra " : "") << END;
    }

    GHost.SetPadState(index, out);
}

// Config reloads report every gamepad as removed and added again. Reconciling afterwards,
// instead of forwarding each event, keeps the app from seeing a controller reconnect.
static void SyncPads() {
    GSyncPadsPending = false;
    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        ImplUser &user = G.Users[i];
        DeviceIntf *device = user.Device;
        PadKind kind = PadKind::None;
        if (user.Connected && device) {
            kind = device->Kind == MacDeviceKind::XBox ? PadKind::XBox : device->Kind == MacDeviceKind::Ds4 ? PadKind::Ds4
                                                                                                             : PadKind::None;
        }

        GHost.SetPad(i, kind);
        if (kind != PadKind::None) {
            PushPadState(i);
        }
    }
}

static void RequestSyncPads() {
    if (!GSyncPadsPending) {
        GSyncPadsPending = true;
        PostAppCallback(SyncPads);
    }
}

// ---- Bridge ----

static string GetHomeDir() {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    return home;
}

void MyInputMac::Init(const Host &host) {
    GHost = host;
    G.HInstance = (HINSTANCE)&GHost; // any address inside this image identifies it

    string home = GetHomeDir();
    Path baseName = PathGetBaseNameWithoutExt(PathGetModulePath(nullptr));

    Path logDir = PathFromStr((home + "/Library/Logs/MyInput").c_str());
    LogInit(PathCombine(logDir, PathCombineExt(baseName, L"log")));
    LOG << "Initializing..." << END;

    G.GlobalCallbacks.Add([](ImplUser *, bool, bool) {
        RequestSyncPads();
        return true;
    });
    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        G.Users[i].Callbacks.Add([i](ImplUser *) {
            PushPadState(i);
            return true;
        });
    }

    Path configDir = PathGetEnvVar(L"MYINPUT_HOOK_CONFIG_DIR");
    if (!configDir) {
        configDir = PathFromStr((home + "/Library/Application Support/MyInput/Configs").c_str());
    }

    Path config = PathGetEnvVar(L"MYINPUT_HOOK_CONFIG");
    ConfigInit(move(configDir), PathCombineExt(config ? config : baseName, L"ini"));

    UpdateAll();
    RequestSyncPads();
    LOG << "Initialized!" << END;
}

void MyInputMac::SetForeground(bool foreground) {
    if (foreground != G.InForeground) {
        if (foreground && G.AutoReload) {
            ConfigReloadIfNeeded();
        }
        if (!foreground) {
            memset(GKeyDown, 0, sizeof(GKeyDown)); // releases won't reach us while in the background
        }

        ImplToggleForeground(true);
        if (G.Debug) {
            LOG << "App " << (foreground ? "entered" : "left") << " foreground" << END;
        }
    }

    UpdateCursor();
}

bool MyInputMac::OnKey(int vk, bool down) {
    if (vk <= 0 || vk >= ImplKeyboard::Count) {
        return false;
    }
    GKeyDown[vk] = down;

    // The engine takes keys the way a Windows keyboard hook reports them, where the "extended" flag
    // tells apart the main Enter from the numpad one, and navigation keys from the numpad without Num Lock.
    bool extended = vk != VK_RETURN;
    if (vk == VkNumpadEnter) {
        vk = VK_RETURN;
    }
    return ImplKeyboardHook(vk, down, extended, GetTickCount());
}

bool MyInputMac::OnMouseButton(int vk, bool down) {
    GKeyDown[vk] = down;
    ChangedMask changes;
    return ImplProcessMouseButton(vk, down, GetTickCount(), &changes);
}

bool MyInputMac::OnMouseMove(int dx, int dy) {
    ChangedMask changes;
    return ImplMouseMotionHook(dx, dy, GetTickCount(), &changes);
}

bool MyInputMac::OnMouseWheel(bool horizontal, int delta) {
    ChangedMask changes;
    return ImplMouseWheelHook(horizontal, delta, GetTickCount(), &changes);
}

void MyInputMac::OnRumble(int user, double low, double high) {
    ImplUser *data = ImplGetUser(user);
    if (data) {
        ImplSetRumble(data, low, high);
    }
}

bool MyInputMac::IsKeyMapped(int vk) {
    ImplInput *input = G.Keyboard.Get(vk);
    return input && input->Mappings;
}

bool MyInputMac::IsDebug() { return G.Debug; }

void MyInputMac::LogLine(const char *text) {
    LOG << text << END;
}
