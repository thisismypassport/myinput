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

static void RegisterMacOptions();

bool LoadLibraryExW(const wchar_t *path, HANDLE file, DWORD flags) {
    // Config options written as "mac/<name>" look like they belong to a plugin called "mac".
    // That is how macOS-only options get into the config format without changing the shared parser:
    // the engine asks for mac_hook.dll, and the options are registered here instead.
    if (tstreq(PathGetBaseNamePtr(path), L"mac_hook.dll")) {
        RegisterMacOptions();
        return true;
    }

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

struct MouseLook;
static void UpdateMouseLook();

void UpdateCursor() {
    bool wantCapture = (G.HideCursor || G.BoundCursor) && !G.Disable && G.InForeground;
    GHost.SetCursorCapture(wantCapture && G.HideCursor, wantCapture && !G.HideCursor);
}

void UpdateAll() {
    G.Paused = !G.IsActive();
    UpdateCursor();
    ImplUpdateAsyncState();
    UpdateMouseLook(); // lets go of the stick if the mappings were just paused or disabled
}

void ShowWindowRumble(double lowFreq, double highFreq) {
    static UniqueLog once;
    if (once) {
        LOG_W << "ERROR: RumbleWindow isn't supported on macOS yet" << END;
    }
}

// ---- Mouse look ----
//
// The engine's own mouse mappings (Mouse.Left : %R.Left) treat movement as a held direction, which is
// fine for menus but poor for aiming. A stick sets how fast the camera turns, so the natural conversion
// is from mouse speed to stick deflection. That is what this does, outside the engine: it measures the
// mouse's speed over a short window and adds the matching deflection to a stick when its state is sent out.

static void PushPadState(int index);

static double NowSeconds() {
    static mach_timebase_info_data_t timebase;
    if (!timebase.denom) {
        mach_timebase_info(&timebase);
    }
    return (double)mach_absolute_time() * timebase.numer / timebase.denom / NSEC_PER_SEC;
}

struct MouseLook {
    // Mouse speed, in pixels per second, that pushes the stick all the way at Sensitivity 1.
    static constexpr double FullSpeed = 1000;
    static constexpr int TickMs = 4;

    // The longest stretch of time one mouse event is taken to describe.
    static constexpr double MaxSpan = 0.02;

    // A mouse event reports the distance covered since the previous one, so it stands for a stretch of time.
    struct Sample {
        double Start, End, X, Y;
    };

    // The longest that movement too fast for the stick is kept and played back afterwards.
    static constexpr double MaxCarry = 0.15;

    bool Enabled, LeftStick;
    double Sensitivity, Deadzone, Curve, Smoothing, Vertical;

    deque<Sample> Samples; // movement within the last Smoothing seconds
    double LastMove = 0, LastUpdate = 0;
    double CarryX = 0, CarryY = 0; // movement the stick was too slow to deliver yet, in pixels
    double OutX = 0, OutY = 0; // what is added to the stick, positive is right/up
    int User = 0;
    UserTimer Timer;

    MouseLook() { ResetSettings(); }

    void ResetSettings() {
        Enabled = LeftStick = false;
        Sensitivity = 1;
        Deadzone = 0.1;
        Curve = 1;
        Smoothing = 0.03;
        Vertical = 1;
    }

    bool IsActive() { return Enabled && !G.Paused && !G.Disable; }

    void Update() {
        double now = NowSeconds();
        double window = max(Smoothing, TickMs / 1000.0);
        while (!Samples.empty() && (!IsActive() || Samples.front().End <= now - window)) {
            Samples.pop_front();
        }

        // Add up the movement that falls inside the window, counting the part of each event's stretch
        // that does. Counting whole events instead would make the result jump every time one enters or
        // leaves the window, which at a mouse's event rate is a visible stutter.
        double x = 0, y = 0;
        for (auto &sample : Samples) {
            double inside = min(sample.End, now) - max(sample.Start, now - window);
            double share = inside / (sample.End - sample.Start);
            x += sample.X * share, y += sample.Y * share;
        }

        // Averaging over the window turns distance into speed, and keeps the total turn the same
        // however the movement is spread: every pixel deflects the stick for exactly that long.
        double vx = x / window, vy = y * Vertical / window;

        // A stick pushed all the way is as fast as the game turns. When the mouse is faster than that,
        // remember what couldn't be delivered and keep turning until it is, so that a quick flick
        // still ends up where the same movement made slowly would have.
        double dt = min(max(now - LastUpdate, 0.001), 0.05);
        LastUpdate = now;
        double fullSpeed = FullSpeed / Sensitivity;
        if (!IsActive()) {
            CarryX = CarryY = 0;
        }
        vx += CarryX / dt, vy += CarryY / dt;
        double speed = sqrt(vx * vx + vy * vy);
        CarryX = CarryY = 0;
        if (speed > fullSpeed) {
            double excess = min((speed - fullSpeed) * dt, fullSpeed * MaxCarry);
            CarryX = vx / speed * excess, CarryY = vy / speed * excess;
        }

        double outX = 0, outY = 0;
        if (speed > 0) {
            // Games ignore the middle of the stick. Starting from the edge of that deadzone
            // is what makes the smallest movement show up.
            double amount = min(speed / fullSpeed, 1.0);
            double deflection = Deadzone + (1 - Deadzone) * pow(amount, Curve);
            outX = vx / speed * deflection;
            outY = -vy / speed * deflection;
        }

        if (outX != OutX || outY != OutY) {
            OutX = outX, OutY = outY;
            PushPadState(User);
        }
        if (Samples.empty() && !CarryX && !CarryY && !OutX && !OutY) {
            Timer.End(); // nothing left to play out, and the stick is back at rest
        }
    }

    static void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD);

    bool OnMove(double dx, double dy) {
        if (!IsActive()) {
            return false;
        }

        if (dx || dy) {
            User = G.ActiveUser;
            double now = NowSeconds();
            double start = min(max(LastMove, now - MaxSpan), now - 0.001);
            Samples.push_back({start, now, dx, dy});
            LastMove = now;
            if (!Timer.IsSet()) {
                LastUpdate = now - TickMs / 1000.0; // coming out of idle: this is the first tick
                Timer.StartMs(TickMs, OnTimer, this);
                Update();
            }
        }
        return !G.Forward;
    }
} GLook;

void CALLBACK MouseLook::OnTimer(HWND, UINT, UINT_PTR, DWORD) { GLook.Update(); }

static void UpdateMouseLook() { GLook.Update(); }

static void SetLookNumber(double *target, const char *name, const string &value, double minValue, double maxValue) {
    char *end = nullptr;
    double number = strtod_l(value.c_str(), &end, nullptr);
    if (end == value.c_str() || *end || number < minValue || number > maxValue) {
        LOG_W << "ERROR: Invalid " << name << " (must be between " << minValue << " and " << maxValue << "): " << value << END;
    } else {
        *target = number;
    }
}

static void RegisterMacOptions() {
    ConfigRegisterCB([](bool after) {
        if (!after) { // about to (re)load the config: back to defaults
            GLook.ResetSettings();
            GLook.Update();
        }
    });

    ConfigRegisterCustomVar("mouselook", [](const string &value) {
        string stick = StrLowerCase(value);
        if (stick == "r" || stick == "right") {
            GLook.Enabled = true, GLook.LeftStick = false;
        } else if (stick == "l" || stick == "left") {
            GLook.Enabled = true, GLook.LeftStick = true;
        } else if (stick == "off") {
            GLook.Enabled = false;
        } else {
            LOG_W << "ERROR: Invalid MouseLook (should be R, L or Off): " << value << END;
        }

        if (GLook.Enabled) {
            G.Users[G.ActiveUser].Connected = true; // like a mapping to a gamepad key would
        }
    });
    ConfigRegisterCustomVar("mouselooksensitivity", [](const string &value) {
        SetLookNumber(&GLook.Sensitivity, "MouseLook.Sensitivity", value, 0.01, 100);
    });
    ConfigRegisterCustomVar("mouselookdeadzone", [](const string &value) {
        SetLookNumber(&GLook.Deadzone, "MouseLook.Deadzone", value, 0, 0.9);
    });
    ConfigRegisterCustomVar("mouselookcurve", [](const string &value) {
        SetLookNumber(&GLook.Curve, "MouseLook.Curve", value, 0.1, 5);
    });
    ConfigRegisterCustomVar("mouselooksmoothing", [](const string &value) {
        SetLookNumber(&GLook.Smoothing, "MouseLook.Smoothing", value, 0, 0.5);
    });
    ConfigRegisterCustomVar("mouselookvertical", [](const string &value) {
        SetLookNumber(&GLook.Vertical, "MouseLook.Vertical", value, 0.1, 10);
    });
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

    if (index == GLook.User && (GLook.OutX || GLook.OutY)) {
        double &x = GLook.LeftStick ? out.LX : out.RX;
        double &y = GLook.LeftStick ? out.LY : out.RY;
        x += GLook.OutX, y += GLook.OutY;
        double length = sqrt(x * x + y * y);
        if (length > 1) {
            x /= length, y /= length;
        }
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

bool MyInputMac::OnMouseLook(double dx, double dy) {
    return GLook.OnMove(dx, dy);
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
