// Integration test for the macOS hook. This is a stand-in for a real game or streaming client:
// an SDL app with an AppKit event loop that reads game controllers and nothing else.
// test/run.sh starts it with the hook injected, the same way mac/inject does for a real app.
// The probe then posts keyboard and mouse events to itself and checks what comes out of the
// virtual gamepad the hook created, i.e. the whole path: AppKit event -> hook -> mapping engine -> SDL.
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <functional>
#include <string>
#include <vector>

#ifdef PROBE_SDL3
#include <SDL3/SDL.h>
using Pad = SDL_Gamepad;
#define BTN(name) SDL_GAMEPAD_BUTTON_##name
#define AXIS(name) SDL_GAMEPAD_AXIS_##name
#define BTN_A SDL_GAMEPAD_BUTTON_SOUTH
#define BTN_B SDL_GAMEPAD_BUTTON_EAST
#define BTN_X SDL_GAMEPAD_BUTTON_WEST
#define BTN_Y SDL_GAMEPAD_BUTTON_NORTH
#define BTN_UP SDL_GAMEPAD_BUTTON_DPAD_UP
#define BTN_TOUCHPAD SDL_GAMEPAD_BUTTON_TOUCHPAD
#define AXIS_LT SDL_GAMEPAD_AXIS_LEFT_TRIGGER
#define AXIS_RT SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
static bool Button(Pad *pad, int button) { return SDL_GetGamepadButton(pad, (SDL_GamepadButton)button); }
static int Axis(Pad *pad, int axis) { return SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)axis); }
#else
#include <SDL2/SDL.h>
using Pad = SDL_GameController;
#define AXIS(name) SDL_CONTROLLER_AXIS_##name
#define BTN_A SDL_CONTROLLER_BUTTON_A
#define BTN_B SDL_CONTROLLER_BUTTON_B
#define BTN_X SDL_CONTROLLER_BUTTON_X
#define BTN_Y SDL_CONTROLLER_BUTTON_Y
#define BTN_UP SDL_CONTROLLER_BUTTON_DPAD_UP
#define BTN_TOUCHPAD SDL_CONTROLLER_BUTTON_TOUCHPAD
#define AXIS_LT SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define AXIS_RT SDL_CONTROLLER_AXIS_TRIGGERRIGHT
static bool Button(Pad *pad, int button) { return SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)button); }
static int Axis(Pad *pad, int axis) { return SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)axis); }
#endif

static Pad *GPad;
static std::string GPadName;
static std::vector<int> GKeysSeen; // key codes of the key-down events that reached the app
static int GRightClicksSeen, GScrollDownsSeen;
static int GFailures;
static int GTurboPresses;

static void Check(bool ok, const char *what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        GFailures++;
    }
}

static bool SawKey(int keyCode) {
    return std::find(GKeysSeen.begin(), GKeysSeen.end(), keyCode) != GKeysSeen.end();
}

static bool Near(int value, int expected) { return abs(value - expected) <= 2; }

// ---- Synthetic input, posted the way the window server delivers the real thing ----

static void Post(CGEventRef cgEvent) {
    [NSApp postEvent:[NSEvent eventWithCGEvent:cgEvent] atStart:NO];
    CFRelease(cgEvent);
}

static void Key(int keyCode, bool down) {
    CGEventRef cgEvent = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)keyCode, down);
    // New events pick up the modifiers held on the real keyboard at that moment.
    // Clear them, so that someone typing Cmd+C elsewhere while this runs can't change the outcome.
    CGEventSetFlags(cgEvent, (CGEventFlags)0);
    Post(cgEvent);
}

static void LeftShift(bool down) {
    CGEventRef cgEvent = CGEventCreateKeyboardEvent(nullptr, kVK_Shift, down);
    CGEventSetType(cgEvent, kCGEventFlagsChanged);
    CGEventSetFlags(cgEvent, down ? (CGEventFlags)(kCGEventFlagMaskShift | 0x2) : (CGEventFlags)0);
    Post(cgEvent);
}

static void MouseButton(bool left, bool down) {
    CGEventType type = left ? (down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp)
                            : (down ? kCGEventRightMouseDown : kCGEventRightMouseUp);
    Post(CGEventCreateMouseEvent(nullptr, type, CGPointMake(100, 100), left ? kCGMouseButtonLeft : kCGMouseButtonRight));
}

static void MouseMove(int dx, int dy) {
    CGEventRef cgEvent = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, CGPointMake(100, 100), kCGMouseButtonLeft);
    CGEventSetIntegerValueField(cgEvent, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(cgEvent, kCGMouseEventDeltaY, dy);
    Post(cgEvent);
}

static void Wheel(int lines) {
    Post(CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, lines));
}

// ---- Script ----

struct Step {
    std::function<void()> Act;
    int WaitMs; // time to let the events, the engine's timers and SDL settle before the next step
};

static std::vector<Step> BuildScript() {
    return {
        {[] {
             Check(GPad != nullptr, "a virtual gamepad showed up");
             printf("      (%s)\n", GPadName.c_str());
             Check(Axis(GPad, AXIS_LT) == 0 && Axis(GPad, AXIS_RT) == 0, "triggers rest at zero");
         },
         0},

        // Keys to buttons and sticks.
        {[] { Key(kVK_ANSI_W, true); }, 80},
        {[] {
             Check(Near(Axis(GPad, AXIS(LEFTY)), -32767), "W pushes the left stick up");
             Check(!SawKey(kVK_ANSI_W), "W is hidden from the app");
             Key(kVK_ANSI_A, true);
         },
         80},
        {[] {
             // The default stick shape is a circle, so a diagonal is ~0.707 on each axis.
             Check(Near(Axis(GPad, AXIS(LEFTX)), -23170) && Near(Axis(GPad, AXIS(LEFTY)), -23170), "W+A is a diagonal");
             Key(kVK_ANSI_W, false);
             Key(kVK_ANSI_A, false);
         },
         80},
        {[] {
             Check(Axis(GPad, AXIS(LEFTX)) == 0 && Axis(GPad, AXIS(LEFTY)) == 0, "releasing recenters the stick");
             Key(kVK_Space, true);
             LeftShift(true);
         },
         80},
        {[] {
             Check(Button(GPad, BTN_A), "Space presses A");
             Check(Button(GPad, BTN_B), "Left Shift presses B");
             Key(kVK_Space, false);
             LeftShift(false);
             Key(kVK_ANSI_F, true);
         },
         80},
        {[] {
             Check(!Button(GPad, BTN_A) && !Button(GPad, BTN_B), "A and B release");
             Check(Button(GPad, BTN_TOUCHPAD), "%Extra is the touchpad click");
             Key(kVK_ANSI_F, false);
         },
         80},

        // Mouse buttons to triggers.
        {[] {
             MouseButton(true, true);
             MouseButton(false, true);
         },
         80},
        {[] {
             Check(Near(Axis(GPad, AXIS_RT), 32767), "left click pulls the right trigger");
             Check(Near(Axis(GPad, AXIS_LT), 16384), "right click half-pulls the left trigger (~0.5)");
             MouseButton(true, false);
             MouseButton(false, false);
         },
         80},
        {[] { Check(Axis(GPad, AXIS_RT) == 0 && Axis(GPad, AXIS_LT) == 0, "triggers release"); }, 0},

        // Mouse movement to the right stick: 10px at 0.05 per pixel.
        {[] { MouseMove(10, 0); }, 40},
        {[] {
             Check(Near(Axis(GPad, AXIS(RIGHTX)), 16384), "moving the mouse right deflects the right stick");
             MouseMove(0, -10);
         },
         40},
        {[] { Check(Near(Axis(GPad, AXIS(RIGHTY)), -16384), "moving the mouse up deflects it up"); }, 200},
        {[] {
             Check(Axis(GPad, AXIS(RIGHTX)) == 0 && Axis(GPad, AXIS(RIGHTY)) == 0, "the stick recenters when the mouse stops");
             Wheel(1);
         },
         30},
        {[] { Check(Button(GPad, BTN_UP), "wheel up taps D-pad up"); }, 200},
        {[] { Check(!Button(GPad, BTN_UP), "and lets go of it"); }, 0},

        // Turbo, which runs on the engine's timers.
        {[] {
             GTurboPresses = 0;
             Key(kVK_ANSI_T, true);
         },
         400},
        {[] {
             Check(GTurboPresses >= 3, "holding T turbo-presses X");
             Key(kVK_ANSI_T, false);
         },
         150},
        {[] { Check(!Button(GPad, BTN_X), "turbo stops on release"); }, 0},

        // Key to key, and keys that aren't mapped.
        {[] {
             GKeysSeen.clear();
             Key(kVK_ANSI_Q, true);
             Key(kVK_ANSI_Q, false);
             Key(kVK_ANSI_Z, true);
             Key(kVK_ANSI_Z, false);
         },
         120},
        {[] {
             Check(SawKey(kVK_Tab) && !SawKey(kVK_ANSI_Q), "Q reaches the app as Tab");
             Check(!Button(GPad, BTN_Y), "a generated Tab isn't mapped again (Tab itself is mapped to Y)");
             Check(SawKey(kVK_ANSI_Z), "unmapped keys pass through");
             Key(kVK_ANSI_G, true);
             Key(kVK_ANSI_H, true);
         },
         120},
        {[] {
             Check(GRightClicksSeen == 1, "G reaches the app as a right click");
             Check(GScrollDownsSeen >= 1, "H reaches the app as wheel-down scrolling");
             Key(kVK_ANSI_G, false);
             Key(kVK_ANSI_H, false);
         },
         80},

        // ToggleDisable turns every mapping off, then back on.
        {[] {
             Key(kVK_F8, true);
             Key(kVK_F8, false);
         },
         120},
        {[] {
             GKeysSeen.clear();
             Key(kVK_Space, true);
         },
         80},
        {[] {
             Check(!Button(GPad, BTN_A) && SawKey(kVK_Space), "after ToggleDisable, Space goes to the app instead");
             Key(kVK_Space, false);
             Key(kVK_F8, true);
             Key(kVK_F8, false);
         },
         120},
        {[] { Key(kVK_Space, true); }, 80},
        {[] {
             Check(Button(GPad, BTN_A), "toggling again restores the mappings");
             Key(kVK_Space, false);
         },
         80},
    };
}

static bool GPrintEvents; // PROBE_IDLE mode: print what the gamepad does

static void PumpSdl() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (GPrintEvents) {
#ifdef PROBE_SDL3
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
                printf("button %s %s\n", SDL_GetGamepadStringForButton((SDL_GamepadButton)event.gbutton.button), event.gbutton.down ? "down" : "up");
            } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                printf("axis %s %d\n", SDL_GetGamepadStringForAxis((SDL_GamepadAxis)event.gaxis.axis), event.gaxis.value);
            }
#else
            if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
                printf("button %s %s\n", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)event.cbutton.button), event.cbutton.state ? "down" : "up");
            } else if (event.type == SDL_CONTROLLERAXISMOTION) {
                printf("axis %s %d\n", SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)event.caxis.axis), event.caxis.value);
            }
#endif
        }
#ifdef PROBE_SDL3
        if (event.type == SDL_EVENT_GAMEPAD_ADDED && !GPad) {
            GPad = SDL_OpenGamepad(event.gdevice.which);
            GPadName = SDL_GetGamepadName(GPad) ?: "";
        } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button == BTN_X) {
            GTurboPresses++;
        }
#else
        if (event.type == SDL_CONTROLLERDEVICEADDED && !GPad) {
            GPad = SDL_GameControllerOpen(event.cdevice.which);
            GPadName = SDL_GameControllerName(GPad) ?: "";
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == BTN_X) {
            GTurboPresses++;
        }
#endif
    }
}

static void PumpAppKit(int ms) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:ms / 1000.0];
    while (true) {
        @autoreleasepool {
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event) {
                if (event.type == NSEventTypeKeyDown) {
                    GKeysSeen.push_back(event.keyCode);
                } else if (event.type == NSEventTypeRightMouseDown) {
                    GRightClicksSeen++;
                } else if (event.type == NSEventTypeScrollWheel && event.scrollingDeltaY < 0) {
                    GScrollDownsSeen++;
                }
                [NSApp sendEvent:event];
            }
        }
        PumpSdl();
        if (deadline.timeIntervalSinceNow <= 0) {
            break;
        }
    }
}

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited]; // never takes focus or shows in the Dock

#ifdef PROBE_SDL3
        if (!SDL_Init(SDL_INIT_GAMEPAD)) {
#else
        if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
#endif
            printf("FAIL  SDL_Init: %s\n", SDL_GetError());
            return 2;
        }

        // The hook starts on the first turn of the run loop and creates its gamepad right after.
        for (int i = 0; i < 100 && !GPad; i++) {
            PumpAppKit(50);
        }

        // PROBE_IDLE=<seconds>: skip the script and just report gamepad events, for trying out a config
        // by hand (with real keys if the config has "!Always = true", or with events posted to this pid).
        if (const char *idle = getenv("PROBE_IDLE")) {
            printf("pid %d\n", getpid());
            GPrintEvents = true;
            PumpAppKit(atoi(idle) * 1000);
            return 0;
        }

        for (const Step &step : BuildScript()) {
            if (!GPad) {
                Check(false, "a virtual gamepad showed up");
                break;
            }
            step.Act();
            PumpAppKit(step.WaitMs);
        }
    }

    printf("%s\n", GFailures ? "FAILED" : "PASSED");
    return GFailures ? 1 : 0;
}
