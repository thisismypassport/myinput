#include "SdlPad.h"
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

using namespace MyInputMac;

// The host's SDL is found at runtime with dlsym, so the hook has no build-time dependency on SDL
// and always talks to the exact copy the app uses. The declarations below mirror SDL's public headers.

namespace {

constexpr uint32_t SdlInitJoystick = 0x200;
constexpr uint16_t SdlJoystickTypeGamepad = 1;

// SDL_GameControllerButton / SDL_GamepadButton. Both versions number these the same.
enum {
    ButtonA,
    ButtonB,
    ButtonX,
    ButtonY,
    ButtonBack,
    ButtonGuide,
    ButtonStart,
    ButtonLeftStick,
    ButtonRightStick,
    ButtonLeftShoulder,
    ButtonRightShoulder,
    ButtonDpadUp,
    ButtonDpadDown,
    ButtonDpadLeft,
    ButtonDpadRight,
    ButtonMisc1,
    ButtonPaddle1,
    ButtonPaddle2,
    ButtonPaddle3,
    ButtonPaddle4,
    ButtonTouchpad,
    ButtonCount,
};

enum {
    AxisLeftX,
    AxisLeftY,
    AxisRightX,
    AxisRightY,
    AxisLeftTrigger,
    AxisRightTrigger,
    AxisCount,
};

// SDL assigns raw button and axis indices in enum order to the bits set in these masks.
// Declaring all of them makes a raw index equal to its enum value.
constexpr uint32_t ButtonMask = (1u << ButtonCount) - 1;
constexpr uint32_t AxisMask = (1u << AxisCount) - 1;

struct Sdl2Desc { // SDL_VirtualJoystickDesc, SDL 2.24+
    uint16_t version; // SDL_VIRTUAL_JOYSTICK_DESC_VERSION
    uint16_t type;
    uint16_t naxes;
    uint16_t nbuttons;
    uint16_t nhats;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t padding;
    uint32_t button_mask;
    uint32_t axis_mask;
    const char *name;
    void *userdata;
    void (*Update)(void *userdata);
    void (*SetPlayerIndex)(void *userdata, int player_index);
    int (*Rumble)(void *userdata, uint16_t low, uint16_t high);
    int (*RumbleTriggers)(void *userdata, uint16_t left, uint16_t right);
    int (*SetLED)(void *userdata, uint8_t red, uint8_t green, uint8_t blue);
    int (*SendEffect)(void *userdata, const void *data, int size);
};

struct Sdl3Desc { // SDL_VirtualJoystickDesc, SDL 3
    uint32_t version; // sizeof(*this)
    uint16_t type;
    uint16_t padding;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t naxes;
    uint16_t nbuttons;
    uint16_t nballs;
    uint16_t nhats;
    uint16_t ntouchpads;
    uint16_t nsensors;
    uint16_t padding2[2];
    uint32_t button_mask;
    uint32_t axis_mask;
    const char *name;
    const void *touchpads;
    const void *sensors;
    void *userdata;
    void (*Update)(void *userdata);
    void (*SetPlayerIndex)(void *userdata, int player_index);
    bool (*Rumble)(void *userdata, uint16_t low, uint16_t high);
    bool (*RumbleTriggers)(void *userdata, uint16_t left, uint16_t right);
    bool (*SetLED)(void *userdata, uint8_t red, uint8_t green, uint8_t blue);
    bool (*SendEffect)(void *userdata, const void *data, int size);
    bool (*SetSensorsEnabled)(void *userdata, bool enabled);
    void (*Cleanup)(void *userdata);
};
static_assert(sizeof(Sdl3Desc) == 136, "must match SDL3's SDL_VirtualJoystickDesc");

struct Sdl2Api {
    uint32_t (*WasInit)(uint32_t flags);
    const char *(*GetError)();
    int (*AttachVirtualEx)(const Sdl2Desc *desc);
    int (*DetachVirtual)(int deviceIndex);
    int (*NumJoysticks)();
    int32_t (*GetDeviceInstanceID)(int deviceIndex);
    void *(*Open)(int deviceIndex);
    void (*Close)(void *joystick);
    int32_t (*InstanceID)(void *joystick);
    int (*SetAxis)(void *joystick, int axis, int16_t value);
    int (*SetButton)(void *joystick, int button, uint8_t value);
    void *(*ControllerFromInstanceID)(int32_t id);
};

struct Sdl3Api {
    uint32_t (*WasInit)(uint32_t flags);
    const char *(*GetError)();
    uint32_t (*AttachVirtual)(const Sdl3Desc *desc);
    bool (*DetachVirtual)(uint32_t id);
    void *(*Open)(uint32_t id);
    void (*Close)(void *joystick);
    bool (*SetAxis)(void *joystick, int axis, int16_t value);
    bool (*SetButton)(void *joystick, int button, bool down);
    void *(*GamepadFromID)(uint32_t id);
};

struct Pad {
    PadKind Wanted = PadKind::None;
    PadKind Attached = PadKind::None;
    void *Joystick = nullptr;
    uint32_t Id = 0;
    PadState State = {};
    bool OpenedByApp = false;
};

Sdl2Api GSdl2;
Sdl3Api GSdl3;
int GSdlVersion = 0; // 0 until the host's SDL is found, then 2 or 3
Pad GPads[MaxPads];

template <class T>
bool Resolve(T &target, const char *name) {
    target = (T)dlsym(RTLD_DEFAULT, name);
    return target != nullptr;
}

void Logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void Logf(const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    LogLine(buffer);
}

bool ResolveSdl() {
    if (GSdlVersion) {
        return true;
    }

    // Prefer the SDL2 API when the app has it: an app built against SDL2 on top of sdl2-compat
    // exports both, and it only knows about joysticks that went through the SDL2 layer.
    if (Resolve(GSdl2.AttachVirtualEx, "SDL_JoystickAttachVirtualEx") &&
        Resolve(GSdl2.WasInit, "SDL_WasInit") &&
        Resolve(GSdl2.GetError, "SDL_GetError") &&
        Resolve(GSdl2.DetachVirtual, "SDL_JoystickDetachVirtual") &&
        Resolve(GSdl2.NumJoysticks, "SDL_NumJoysticks") &&
        Resolve(GSdl2.GetDeviceInstanceID, "SDL_JoystickGetDeviceInstanceID") &&
        Resolve(GSdl2.Open, "SDL_JoystickOpen") &&
        Resolve(GSdl2.Close, "SDL_JoystickClose") &&
        Resolve(GSdl2.InstanceID, "SDL_JoystickInstanceID") &&
        Resolve(GSdl2.SetAxis, "SDL_JoystickSetVirtualAxis") &&
        Resolve(GSdl2.SetButton, "SDL_JoystickSetVirtualButton") &&
        Resolve(GSdl2.ControllerFromInstanceID, "SDL_GameControllerFromInstanceID")) {
        GSdlVersion = 2;
    } else if (Resolve(GSdl3.AttachVirtual, "SDL_AttachVirtualJoystick") &&
               Resolve(GSdl3.WasInit, "SDL_WasInit") &&
               Resolve(GSdl3.GetError, "SDL_GetError") &&
               Resolve(GSdl3.DetachVirtual, "SDL_DetachVirtualJoystick") &&
               Resolve(GSdl3.Open, "SDL_OpenJoystick") &&
               Resolve(GSdl3.Close, "SDL_CloseJoystick") &&
               Resolve(GSdl3.SetAxis, "SDL_SetJoystickVirtualAxis") &&
               Resolve(GSdl3.SetButton, "SDL_SetJoystickVirtualButton") &&
               Resolve(GSdl3.GamepadFromID, "SDL_GetGamepadFromID")) {
        GSdlVersion = 3;
    } else {
        return false;
    }

    Logf("Found SDL%d in the app", GSdlVersion);
    return true;
}

bool IsJoystickSubsystemUp() {
    return (GSdlVersion == 2 ? GSdl2.WasInit(SdlInitJoystick) : GSdl3.WasInit(SdlInitJoystick)) != 0;
}

// SDL calls these on whichever thread the app asks for rumble from.
void OnSdlRumble(void *userdata, uint16_t low, uint16_t high) {
    int user = (int)(intptr_t)userdata;
    dispatch_async(dispatch_get_main_queue(), ^{
        OnRumble(user, low / 65535.0, high / 65535.0);
    });
}

int OnSdl2Rumble(void *userdata, uint16_t low, uint16_t high) {
    OnSdlRumble(userdata, low, high);
    return 0;
}

bool OnSdl3Rumble(void *userdata, uint16_t low, uint16_t high) {
    OnSdlRumble(userdata, low, high);
    return true;
}

void GetIdentity(PadKind kind, uint16_t *vendor, uint16_t *product, const char **name) {
    if (kind == PadKind::Ds4) {
        *vendor = 0x054c, *product = 0x09cc; // DualShock 4 (2nd gen)
        *name = "MyInput Virtual PS4 Controller";
    } else {
        // Xbox One S pad rather than the Xbox 360 one the Windows build emulates:
        // chiaki-ng treats a version-less 045e:028e on macOS as Steam's virtual controller.
        *vendor = 0x045e, *product = 0x02ea;
        *name = "MyInput Virtual Xbox Controller";
    }
}

void SetAxis(Pad &pad, int axis, int16_t value) {
    if (GSdlVersion == 2) {
        GSdl2.SetAxis(pad.Joystick, axis, value);
    } else {
        GSdl3.SetAxis(pad.Joystick, axis, value);
    }
}

void SetButton(Pad &pad, int button, bool down) {
    if (GSdlVersion == 2) {
        GSdl2.SetButton(pad.Joystick, button, down ? 1 : 0);
    } else {
        GSdl3.SetButton(pad.Joystick, button, down);
    }
}

int16_t StickValue(double value) {
    return (int16_t)lround(fmax(-1.0, fmin(1.0, value)) * 32767);
}

int16_t TriggerValue(double value) {
    // Triggers span the whole axis range, resting at the minimum.
    return (int16_t)(lround(fmax(0.0, fmin(1.0, value)) * 65535) - 32768);
}

void PushState(Pad &pad) {
    const PadState &s = pad.State;
    SetAxis(pad, AxisLeftX, StickValue(s.LX));
    SetAxis(pad, AxisLeftY, StickValue(-s.LY)); // SDL's Y axes point down
    SetAxis(pad, AxisRightX, StickValue(s.RX));
    SetAxis(pad, AxisRightY, StickValue(-s.RY));
    SetAxis(pad, AxisLeftTrigger, TriggerValue(s.LT));
    SetAxis(pad, AxisRightTrigger, TriggerValue(s.RT));

    SetButton(pad, ButtonA, s.A);
    SetButton(pad, ButtonB, s.B);
    SetButton(pad, ButtonX, s.X);
    SetButton(pad, ButtonY, s.Y);
    SetButton(pad, ButtonBack, s.Back);
    SetButton(pad, ButtonGuide, s.Guide);
    SetButton(pad, ButtonStart, s.Start);
    SetButton(pad, ButtonLeftStick, s.L);
    SetButton(pad, ButtonRightStick, s.R);
    SetButton(pad, ButtonLeftShoulder, s.LB);
    SetButton(pad, ButtonRightShoulder, s.RB);
    SetButton(pad, ButtonDpadUp, s.DU);
    SetButton(pad, ButtonDpadDown, s.DD);
    SetButton(pad, ButtonDpadLeft, s.DL);
    SetButton(pad, ButtonDpadRight, s.DR);
    // %Extra is the Share button on Xbox pads and the touchpad click on PlayStation ones.
    // SDL has a single slot that plays that role for remote-play apps: the touchpad button.
    SetButton(pad, ButtonTouchpad, s.Extra);
}

bool Attach(int user, Pad &pad) {
    uint16_t vendor, product;
    const char *name;
    GetIdentity(pad.Wanted, &vendor, &product, &name);

    if (GSdlVersion == 2) {
        Sdl2Desc desc = {};
        desc.version = 1;
        desc.type = SdlJoystickTypeGamepad;
        desc.naxes = AxisCount;
        desc.nbuttons = ButtonCount;
        desc.vendor_id = vendor;
        desc.product_id = product;
        desc.button_mask = ButtonMask;
        desc.axis_mask = AxisMask;
        desc.name = name;
        desc.userdata = (void *)(intptr_t)user;
        desc.Rumble = OnSdl2Rumble;

        int deviceIndex = GSdl2.AttachVirtualEx(&desc);
        if (deviceIndex < 0) {
            Logf("ERROR: SDL couldn't create gamepad %d: %s", user + 1, GSdl2.GetError());
            return false;
        }

        pad.Joystick = GSdl2.Open(deviceIndex);
        if (!pad.Joystick) {
            Logf("ERROR: SDL couldn't open gamepad %d: %s", user + 1, GSdl2.GetError());
            GSdl2.DetachVirtual(deviceIndex);
            return false;
        }
        pad.Id = (uint32_t)GSdl2.InstanceID(pad.Joystick);
    } else {
        Sdl3Desc desc = {};
        desc.version = sizeof(desc);
        desc.type = SdlJoystickTypeGamepad;
        desc.naxes = AxisCount;
        desc.nbuttons = ButtonCount;
        desc.vendor_id = vendor;
        desc.product_id = product;
        desc.button_mask = ButtonMask;
        desc.axis_mask = AxisMask;
        desc.name = name;
        desc.userdata = (void *)(intptr_t)user;
        desc.Rumble = OnSdl3Rumble;

        pad.Id = GSdl3.AttachVirtual(&desc);
        if (!pad.Id) {
            Logf("ERROR: SDL couldn't create gamepad %d: %s", user + 1, GSdl3.GetError());
            return false;
        }

        pad.Joystick = GSdl3.Open(pad.Id);
        if (!pad.Joystick) {
            Logf("ERROR: SDL couldn't open gamepad %d: %s", user + 1, GSdl3.GetError());
            GSdl3.DetachVirtual(pad.Id);
            return false;
        }
    }

    pad.Attached = pad.Wanted;
    pad.OpenedByApp = false;
    Logf("Gamepad %d connected: %s", user + 1, name);
    PushState(pad);
    return true;
}

void Detach(int user, Pad &pad) {
    if (GSdlVersion == 2) {
        GSdl2.Close(pad.Joystick);
        for (int i = 0; i < GSdl2.NumJoysticks(); i++) {
            if ((uint32_t)GSdl2.GetDeviceInstanceID(i) == pad.Id) {
                GSdl2.DetachVirtual(i);
                break;
            }
        }
    } else {
        GSdl3.Close(pad.Joystick);
        GSdl3.DetachVirtual(pad.Id);
    }

    pad.Attached = PadKind::None;
    pad.Joystick = nullptr;
    Logf("Gamepad %d disconnected", user + 1);
}

} // namespace

void SdlPad::SetPad(int user, PadKind kind) {
    GPads[user].Wanted = kind;
    Poll();
}

void SdlPad::SetState(int user, const PadState &state) {
    Pad &pad = GPads[user];
    pad.State = state;
    // The app may have shut SDL down since the last poll, taking our joystick with it.
    if (pad.Joystick && IsJoystickSubsystemUp()) {
        PushState(pad);
    }
}

void SdlPad::Poll() {
    if (!ResolveSdl()) {
        return;
    }

    if (!IsJoystickSubsystemUp()) {
        for (Pad &pad : GPads) {
            if (pad.Joystick) {
                pad.Joystick = nullptr; // SDL freed it when the subsystem quit
                pad.Attached = PadKind::None;
            }
        }
        return;
    }

    for (int i = 0; i < MaxPads; i++) {
        Pad &pad = GPads[i];
        if (pad.Attached != pad.Wanted) {
            if (pad.Attached != PadKind::None) {
                Detach(i, pad);
            }
            if (pad.Wanted != PadKind::None) {
                Attach(i, pad);
            }
        }

        if (pad.Joystick && !pad.OpenedByApp) {
            void *opened = GSdlVersion == 2 ? GSdl2.ControllerFromInstanceID((int32_t)pad.Id) : GSdl3.GamepadFromID(pad.Id);
            if (opened) {
                pad.OpenedByApp = true;
                Logf("Gamepad %d is now in use by the app", i + 1);
            }
        }
    }
}
