// The macOS hook: a dylib loaded into the target app (see mac/inject), where it
//  - intercepts the app's own keyboard and mouse events and runs them through the mapping engine,
//  - drops the ones a mapping consumed, so the app never sees them,
//  - feeds the resulting gamepad state to virtual gamepads (SdlPad.mm).
// It only ever sees events delivered to this app, so it needs no Accessibility or Input Monitoring permission.
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h> // kVK_* key codes
#import <GameController/GameController.h> // GCMouse, for unaccelerated mouse movement
#import <objc/runtime.h>
#include "Bridge.h"
#include "SdlPad.h"

using namespace MyInputMac;

namespace {

// Win32 virtual-key codes, which is how the engine and the config format name keys.
enum {
    VK_LBUTTON = 0x01,
    VK_RBUTTON = 0x02,
    VK_MBUTTON = 0x04,
    VK_XBUTTON1 = 0x05,
    VK_XBUTTON2 = 0x06,
    VK_BACK = 0x08,
    VK_TAB = 0x09,
    VK_RETURN = 0x0D,
    VK_CAPITAL = 0x14,
    VK_ESCAPE = 0x1B,
    VK_SPACE = 0x20,
    VK_PRIOR = 0x21,
    VK_NEXT = 0x22,
    VK_END = 0x23,
    VK_HOME = 0x24,
    VK_LEFT = 0x25,
    VK_UP = 0x26,
    VK_RIGHT = 0x27,
    VK_DOWN = 0x28,
    VK_INSERT = 0x2D,
    VK_DELETE = 0x2E,
    VK_LWIN = 0x5B,
    VK_RWIN = 0x5C,
    VK_APPS = 0x5D,
    VK_NUMPAD0 = 0x60,
    VK_MULTIPLY = 0x6A,
    VK_ADD = 0x6B,
    VK_SUBTRACT = 0x6D,
    VK_DECIMAL = 0x6E,
    VK_DIVIDE = 0x6F,
    VK_F1 = 0x70,
    VK_NUMLOCK = 0x90,
    VK_LSHIFT = 0xA0,
    VK_RSHIFT = 0xA1,
    VK_LCONTROL = 0xA2,
    VK_RCONTROL = 0xA3,
    VK_LMENU = 0xA4,
    VK_RMENU = 0xA5,
    VK_OEM_1 = 0xBA,
    VK_OEM_PLUS = 0xBB,
    VK_OEM_COMMA = 0xBC,
    VK_OEM_MINUS = 0xBD,
    VK_OEM_PERIOD = 0xBE,
    VK_OEM_2 = 0xBF,
    VK_OEM_3 = 0xC0,
    VK_OEM_4 = 0xDB,
    VK_OEM_5 = 0xDC,
    VK_OEM_6 = 0xDD,
    VK_OEM_7 = 0xDE,
    VK_OEM_102 = 0xE2,
};

struct KeyEntry {
    uint16_t KeyCode;
    int Vk;
};

// Keys are matched by position, like on Windows, not by the character the layout puts on them.
const KeyEntry KeyTable[] = {
    {kVK_ANSI_A, 'A'}, {kVK_ANSI_B, 'B'}, {kVK_ANSI_C, 'C'}, {kVK_ANSI_D, 'D'}, {kVK_ANSI_E, 'E'},
    {kVK_ANSI_F, 'F'}, {kVK_ANSI_G, 'G'}, {kVK_ANSI_H, 'H'}, {kVK_ANSI_I, 'I'}, {kVK_ANSI_J, 'J'},
    {kVK_ANSI_K, 'K'}, {kVK_ANSI_L, 'L'}, {kVK_ANSI_M, 'M'}, {kVK_ANSI_N, 'N'}, {kVK_ANSI_O, 'O'},
    {kVK_ANSI_P, 'P'}, {kVK_ANSI_Q, 'Q'}, {kVK_ANSI_R, 'R'}, {kVK_ANSI_S, 'S'}, {kVK_ANSI_T, 'T'},
    {kVK_ANSI_U, 'U'}, {kVK_ANSI_V, 'V'}, {kVK_ANSI_W, 'W'}, {kVK_ANSI_X, 'X'}, {kVK_ANSI_Y, 'Y'},
    {kVK_ANSI_Z, 'Z'},
    {kVK_ANSI_0, '0'}, {kVK_ANSI_1, '1'}, {kVK_ANSI_2, '2'}, {kVK_ANSI_3, '3'}, {kVK_ANSI_4, '4'},
    {kVK_ANSI_5, '5'}, {kVK_ANSI_6, '6'}, {kVK_ANSI_7, '7'}, {kVK_ANSI_8, '8'}, {kVK_ANSI_9, '9'},

    {kVK_Return, VK_RETURN}, {kVK_Tab, VK_TAB}, {kVK_Space, VK_SPACE}, {kVK_Delete, VK_BACK},
    {kVK_Escape, VK_ESCAPE}, {kVK_ForwardDelete, VK_DELETE}, {kVK_Help, VK_INSERT},
    {kVK_Home, VK_HOME}, {kVK_End, VK_END}, {kVK_PageUp, VK_PRIOR}, {kVK_PageDown, VK_NEXT},
    {kVK_LeftArrow, VK_LEFT}, {kVK_RightArrow, VK_RIGHT}, {kVK_UpArrow, VK_UP}, {kVK_DownArrow, VK_DOWN},
    {kVK_ContextualMenu, VK_APPS},

    {kVK_ANSI_Semicolon, VK_OEM_1}, {kVK_ANSI_Equal, VK_OEM_PLUS}, {kVK_ANSI_Comma, VK_OEM_COMMA},
    {kVK_ANSI_Minus, VK_OEM_MINUS}, {kVK_ANSI_Period, VK_OEM_PERIOD}, {kVK_ANSI_Slash, VK_OEM_2},
    {kVK_ANSI_Grave, VK_OEM_3}, {kVK_ANSI_LeftBracket, VK_OEM_4}, {kVK_ANSI_Backslash, VK_OEM_5},
    {kVK_ANSI_RightBracket, VK_OEM_6}, {kVK_ANSI_Quote, VK_OEM_7}, {kVK_ISO_Section, VK_OEM_102},

    {kVK_ANSI_Keypad0, VK_NUMPAD0 + 0}, {kVK_ANSI_Keypad1, VK_NUMPAD0 + 1}, {kVK_ANSI_Keypad2, VK_NUMPAD0 + 2},
    {kVK_ANSI_Keypad3, VK_NUMPAD0 + 3}, {kVK_ANSI_Keypad4, VK_NUMPAD0 + 4}, {kVK_ANSI_Keypad5, VK_NUMPAD0 + 5},
    {kVK_ANSI_Keypad6, VK_NUMPAD0 + 6}, {kVK_ANSI_Keypad7, VK_NUMPAD0 + 7}, {kVK_ANSI_Keypad8, VK_NUMPAD0 + 8},
    {kVK_ANSI_Keypad9, VK_NUMPAD0 + 9},
    {kVK_ANSI_KeypadMultiply, VK_MULTIPLY}, {kVK_ANSI_KeypadPlus, VK_ADD}, {kVK_ANSI_KeypadMinus, VK_SUBTRACT},
    {kVK_ANSI_KeypadDecimal, VK_DECIMAL}, {kVK_ANSI_KeypadDivide, VK_DIVIDE},
    {kVK_ANSI_KeypadEnter, VkNumpadEnter}, {kVK_ANSI_KeypadClear, VK_NUMLOCK}, // same physical key as Num Lock

    {kVK_F1, VK_F1 + 0}, {kVK_F2, VK_F1 + 1}, {kVK_F3, VK_F1 + 2}, {kVK_F4, VK_F1 + 3},
    {kVK_F5, VK_F1 + 4}, {kVK_F6, VK_F1 + 5}, {kVK_F7, VK_F1 + 6}, {kVK_F8, VK_F1 + 7},
    {kVK_F9, VK_F1 + 8}, {kVK_F10, VK_F1 + 9}, {kVK_F11, VK_F1 + 10}, {kVK_F12, VK_F1 + 11},
    {kVK_F13, VK_F1 + 12}, {kVK_F14, VK_F1 + 13}, {kVK_F15, VK_F1 + 14}, {kVK_F16, VK_F1 + 15},
    {kVK_F17, VK_F1 + 16}, {kVK_F18, VK_F1 + 17}, {kVK_F19, VK_F1 + 18}, {kVK_F20, VK_F1 + 19},
};

struct ModifierEntry {
    uint16_t KeyCode;
    int Vk;
    NSUInteger DeviceMask; // per-side bit in NSEvent.modifierFlags (NX_DEVICE*KEYMASK)
    NSUInteger Flag;
};

// Command stands in for the Windows key, and Option for Alt.
const ModifierEntry ModifierTable[] = {
    {kVK_Shift, VK_LSHIFT, 0x00000002, NSEventModifierFlagShift},
    {kVK_RightShift, VK_RSHIFT, 0x00000004, NSEventModifierFlagShift},
    {kVK_Control, VK_LCONTROL, 0x00000001, NSEventModifierFlagControl},
    {kVK_RightControl, VK_RCONTROL, 0x00002000, NSEventModifierFlagControl},
    {kVK_Option, VK_LMENU, 0x00000020, NSEventModifierFlagOption},
    {kVK_RightOption, VK_RMENU, 0x00000040, NSEventModifierFlagOption},
    {kVK_Command, VK_LWIN, 0x00000008, NSEventModifierFlagCommand},
    {kVK_RightCommand, VK_RWIN, 0x00000010, NSEventModifierFlagCommand},
};

// Marks the events we generate ourselves, so they pass through the monitor untouched.
constexpr int64_t OurEventTag = 0x4d79496e; // 'MyIn'

int GVkFromKeyCode[256];
int GKeyCodeFromVk[256];
bool GCursorHidden, GCursorBound, GSkipNextMove;
double GMoveRemainderX, GMoveRemainderY, GWheelRemainderX, GWheelRemainderY;
NSUInteger GGeneratedFlags;

void InitKeyTables() {
    for (int i = 0; i < 256; i++) {
        GKeyCodeFromVk[i] = -1;
    }
    for (const KeyEntry &entry : KeyTable) {
        GVkFromKeyCode[entry.KeyCode] = entry.Vk;
        GKeyCodeFromVk[entry.Vk] = entry.KeyCode;
    }
    for (const ModifierEntry &entry : ModifierTable) {
        GKeyCodeFromVk[entry.Vk] = entry.KeyCode;
    }
    GKeyCodeFromVk[VK_CAPITAL] = kVK_CapsLock;
}

bool IsOurEvent(NSEvent *event) {
    CGEventRef cgEvent = event.CGEvent;
    return cgEvent && CGEventGetIntegerValueField(cgEvent, kCGEventSourceUserData) == OurEventTag;
}

// ---- Input ----

bool HandleKey(NSEvent *event, bool down) {
    int vk = event.keyCode < 256 ? GVkFromKeyCode[event.keyCode] : 0;
    if (!vk) {
        return false;
    }

    // Leave the app's Command shortcuts (Cmd+Q, Cmd+F, ...) alone, unless Command itself is remapped.
    if (down && (event.modifierFlags & NSEventModifierFlagCommand) &&
        !IsKeyMapped(VK_LWIN) && !IsKeyMapped(VK_RWIN)) {
        return false;
    }

    return OnKey(vk, down);
}

bool HandleFlagsChanged(NSEvent *event) {
    if (event.keyCode == kVK_CapsLock) {
        // Caps Lock reports a state change per press, not a press and a release.
        bool consumed = OnKey(VK_CAPITAL, true);
        consumed |= OnKey(VK_CAPITAL, false);
        return consumed;
    }

    for (const ModifierEntry &entry : ModifierTable) {
        if (entry.KeyCode == event.keyCode) {
            return OnKey(entry.Vk, (event.modifierFlags & entry.DeviceMask) != 0);
        }
    }
    return false;
}

bool IsInTitleBar(NSEvent *event) {
    NSWindow *window = event.window;
    if (!window || (window.styleMask & NSWindowStyleMaskFullScreen)) {
        return false;
    }
    return event.locationInWindow.y > NSMaxY(window.contentLayoutRect);
}

bool HandleMouseButton(NSEvent *event, bool down) {
    int vk;
    switch (event.buttonNumber) {
    case 0:
        vk = VK_LBUTTON;
        break;
    case 1:
        vk = VK_RBUTTON;
        break;
    case 2:
        vk = VK_MBUTTON;
        break;
    case 3:
        vk = VK_XBUTTON1;
        break;
    case 4:
        vk = VK_XBUTTON2;
        break;
    default:
        return false;
    }

    // Keep the window draggable and closable while the cursor is free.
    if (down && !GCursorHidden && IsInTitleBar(event)) {
        return false;
    }

    return OnMouseButton(vk, down);
}

// Moves the cursor without the side effects of CGWarpMouseCursorPosition:
// macOS folds the jump into the next mouse-move delta, and freezes the cursor for a moment afterwards.
void WarpCursor(NSPoint screenPoint) {
    // Quartz puts the origin at the top-left of the primary screen, AppKit at its bottom-left.
    CGFloat primaryHeight = NSMaxY(NSScreen.screens.firstObject.frame);
    CGWarpMouseCursorPosition(CGPointMake(screenPoint.x, primaryHeight - screenPoint.y));
    CGAssociateMouseAndMouseCursorPosition(!GCursorHidden); // cancels the freeze
    GSkipNextMove = true;
}

void KeepCursorInKeyWindow() {
    NSWindow *window = NSApp.keyWindow;
    if (!window) {
        return;
    }

    NSRect frame = [window convertRectToScreen:window.contentLayoutRect];
    NSPoint location = NSEvent.mouseLocation;
    NSPoint clamped = NSMakePoint(fmin(fmax(location.x, NSMinX(frame)), NSMaxX(frame) - 1),
                                  fmin(fmax(location.y, NSMinY(frame) + 1), NSMaxY(frame)));
    if (!NSEqualPoints(location, clamped)) {
        WarpCursor(clamped);
    }
}

// ---- Raw mouse movement ----
//
// The deltas in mouse events have the system's pointer acceleration applied: the same hand movement
// counts for more when it is fast. That is right for a cursor and wrong for aiming, so mouse look takes
// the mouse's own counts instead, straight from the device, like raw input does on Windows.
// Trackpads don't offer that, and keep going through the events.

// A mouse reports several counts for each pixel the pointer would move at slow speed.
// Scaling them down keeps one Sensitivity setting in a similar range for mice and trackpads.
constexpr double RawCountsToPixels = 0.2;

NSTimeInterval GLastRawMove;

void WatchRawMouse(GCMouse *mouse) {
    NSString *name = mouse.vendorName ?: @"a mouse";
    __block bool announced = false;
    mouse.mouseInput.mouseMovedHandler = ^(GCMouseInput *, float deltaX, float deltaY) {
        GLastRawMove = NSProcessInfo.processInfo.systemUptime;
        // GameController's Y axis points up.
        if (OnMouseLook(deltaX * RawCountsToPixels, -deltaY * RawCountsToPixels) && !announced) {
            announced = true;
            LogLine([NSString stringWithFormat:@"Mouse look is using raw input from: %@", name].UTF8String);
        }
    };
}

void WatchRawMice() {
    for (GCMouse *mouse in GCMouse.mice) {
        WatchRawMouse(mouse);
    }
    [NSNotificationCenter.defaultCenter addObserverForName:GCMouseDidConnectNotification
                                                    object:nil
                                                     queue:NSOperationQueue.mainQueue
                                                usingBlock:^(NSNotification *note) {
                                                    WatchRawMouse(note.object);
                                                }];
}

bool HandleMouseMove(NSEvent *event) {
    if (GSkipNextMove) {
        GSkipNextMove = false;
        GMoveRemainderX = GMoveRemainderY = 0;
        return GCursorHidden;
    }

    // An event that echoes movement the mouse has just reported raw must not count twice.
    bool echo = NSProcessInfo.processInfo.systemUptime - GLastRawMove < 0.1;
    if (OnMouseLook(echo ? 0 : event.deltaX, echo ? 0 : event.deltaY)) {
        if (GCursorBound) {
            KeepCursorInKeyWindow();
        }
        return true;
    }

    // Deltas can be fractional. The engine counts whole pixels, so carry the remainder over.
    GMoveRemainderX += event.deltaX;
    GMoveRemainderY += event.deltaY;
    int dx = (int)GMoveRemainderX, dy = (int)GMoveRemainderY;
    GMoveRemainderX -= dx;
    GMoveRemainderY -= dy;

    bool consumed = (dx || dy) && OnMouseMove(dx, dy);
    if (GCursorBound) {
        KeepCursorInKeyWindow();
    }
    return consumed;
}

bool HandleScroll(NSEvent *event) {
    // The engine counts wheel notches of 120 units, positive being the wheel rolled up or right.
    double scale = event.hasPreciseScrollingDeltas ? 12 : 120;
    double dx = event.scrollingDeltaX * scale, dy = event.scrollingDeltaY * scale;
    if (event.isDirectionInvertedFromDevice) { // "natural scrolling" flips what the device reported
        dx = -dx, dy = -dy;
    }

    GWheelRemainderX += -dx; // a positive AppKit delta is a scroll to the left
    GWheelRemainderY += dy;
    int wheelX = (int)GWheelRemainderX, wheelY = (int)GWheelRemainderY;
    GWheelRemainderX -= wheelX;
    GWheelRemainderY -= wheelY;

    bool consumed = false;
    if (wheelY) {
        consumed |= OnMouseWheel(false, wheelY);
    }
    if (wheelX) {
        consumed |= OnMouseWheel(true, wheelX);
    }
    return consumed;
}

bool HandleEvent(NSEvent *event) {
    if (IsOurEvent(event)) {
        return false;
    }

    switch (event.type) {
    case NSEventTypeKeyDown:
        return HandleKey(event, true);
    case NSEventTypeKeyUp:
        return HandleKey(event, false);
    case NSEventTypeFlagsChanged:
        return HandleFlagsChanged(event);
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
    case NSEventTypeOtherMouseDown:
        return HandleMouseButton(event, true);
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseUp:
        return HandleMouseButton(event, false);
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
        return HandleMouseMove(event);
    case NSEventTypeScrollWheel:
        return HandleScroll(event);
    default:
        return false;
    }
}

// ---- Interception ----
//
// Events are taken at the two points every AppKit event passes through, whatever toolkit the app uses:
// where they are dequeued and where they are dispatched. A local NSEvent monitor is not enough, because
// it only runs inside -[NSApplication sendEvent:], after toolkits such as SDL have already read the event.

using NextEventImp = NSEvent *(*)(id, SEL, NSEventMask, NSDate *, NSRunLoopMode, BOOL);
using SendEventImp = void (*)(id, SEL, NSEvent *);

NextEventImp GOrigNextEvent;
SendEventImp GOrigSendEvent;
const char PassedEventKey = 0; // tags an event that already went through the engine at dequeue time

NSEvent *HookedNextEvent(id self, SEL cmd, NSEventMask mask, NSDate *expiration, NSRunLoopMode mode, BOOL dequeue) {
    while (true) {
        NSEvent *event = GOrigNextEvent(self, cmd, mask, expiration, mode, dequeue);
        // Leave peeks alone, as well as modal and tracking loops (menus, window drags and resizes).
        if (!event || !dequeue || ![mode isEqualToString:NSDefaultRunLoopMode]) {
            return event;
        }
        if (!HandleEvent(event)) {
            objc_setAssociatedObject(event, &PassedEventKey, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return event;
        }
        // Consumed: carry on waiting for the next event, as the caller asked.
    }
}

void HookedSendEvent(id self, SEL cmd, NSEvent *event) {
    if (objc_getAssociatedObject(event, &PassedEventKey) || !HandleEvent(event)) {
        GOrigSendEvent(self, cmd, event);
    }
}

bool InstallEventHooks() {
    static bool installed;
    if (installed) {
        return true;
    }
    if (!NSApp) {
        return false; // the app hasn't created its NSApplication yet
    }

    // Hook the app's own NSApplication subclass (Qt and SDL both have one), so we run before its overrides.
    Class appClass = object_getClass(NSApp);

    SEL nextEvent = @selector(nextEventMatchingMask:untilDate:inMode:dequeue:);
    Method nextEventMethod = class_getInstanceMethod(appClass, nextEvent);
    GOrigNextEvent = (NextEventImp)method_getImplementation(nextEventMethod);
    if (!class_addMethod(appClass, nextEvent, (IMP)HookedNextEvent, method_getTypeEncoding(nextEventMethod))) {
        method_setImplementation(nextEventMethod, (IMP)HookedNextEvent);
    }

    SEL sendEvent = @selector(sendEvent:);
    Method sendEventMethod = class_getInstanceMethod(appClass, sendEvent);
    GOrigSendEvent = (SendEventImp)method_getImplementation(sendEventMethod);
    if (!class_addMethod(appClass, sendEvent, (IMP)HookedSendEvent, method_getTypeEncoding(sendEventMethod))) {
        method_setImplementation(sendEventMethod, (IMP)HookedSendEvent);
    }

    installed = true;
    return true;
}

// ---- Cursor ----

void EnsureMouseMovedEvents() {
    // AppKit only reports mouse movement to windows that ask for it.
    for (NSWindow *window in NSApp.windows) {
        if (window.isVisible && !window.acceptsMouseMovedEvents) {
            window.acceptsMouseMovedEvents = YES;
        }
    }
}

void SetCursorCapture(bool hide, bool bound) {
    if (hide || bound) {
        EnsureMouseMovedEvents();
    }

    if (hide != GCursorHidden) {
        GCursorHidden = hide;
        if (hide) {
            // Park the cursor inside the window, then freeze it there: mouse movement keeps
            // arriving as deltas, and it can't wander onto another display or the Dock.
            NSWindow *window = NSApp.keyWindow;
            if (window) {
                NSRect frame = [window convertRectToScreen:window.contentLayoutRect];
                WarpCursor(NSMakePoint(NSMidX(frame), NSMidY(frame)));
            }
            CGAssociateMouseAndMouseCursorPosition(false);
            CGDisplayHideCursor(kCGDirectMainDisplay);
        } else {
            CGAssociateMouseAndMouseCursorPosition(true);
            CGDisplayShowCursor(kCGDirectMainDisplay);
        }
    }

    GCursorBound = bound;
    if (bound) {
        KeepCursorInKeyWindow();
    }
}

// ---- Output (key -> key and key -> mouse mappings) ----

CGPoint CurrentCursorPosition() {
    CGEventRef probe = CGEventCreate(nullptr);
    CGPoint position = CGEventGetLocation(probe);
    CFRelease(probe);
    return position;
}

void PostToApp(CGEventRef cgEvent) {
    CGEventSetIntegerValueField(cgEvent, kCGEventSourceUserData, OurEventTag);
    NSEvent *event = [NSEvent eventWithCGEvent:cgEvent];
    CFRelease(cgEvent);
    if (event) {
        [NSApp postEvent:event atStart:NO];
    }
}

void SendKey(int vk, bool down) {
    if (vk <= 0 || vk >= 256 || GKeyCodeFromVk[vk] < 0) {
        return;
    }

    CGEventRef cgEvent = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)GKeyCodeFromVk[vk], down);
    for (const ModifierEntry &entry : ModifierTable) {
        if (entry.Vk == vk) {
            GGeneratedFlags = down ? (GGeneratedFlags | entry.Flag) : (GGeneratedFlags & ~entry.Flag);
            CGEventSetType(cgEvent, kCGEventFlagsChanged);
        }
    }
    CGEventSetFlags(cgEvent, (CGEventFlags)(NSEvent.modifierFlags | GGeneratedFlags));
    PostToApp(cgEvent);
}

void SendMouseButton(int vk, bool down) {
    CGMouseButton button;
    CGEventType type;
    switch (vk) {
    case VK_LBUTTON:
        button = kCGMouseButtonLeft;
        type = down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
        break;
    case VK_RBUTTON:
        button = kCGMouseButtonRight;
        type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
        break;
    default:
        button = vk == VK_MBUTTON ? kCGMouseButtonCenter : vk == VK_XBUTTON1 ? (CGMouseButton)3 : (CGMouseButton)4;
        type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
        break;
    }

    CGEventRef cgEvent = CGEventCreateMouseEvent(nullptr, type, CurrentCursorPosition(), button);
    NSWindow *window = NSApp.keyWindow;
    if (window) {
        CGEventSetIntegerValueField(cgEvent, kCGMouseEventWindowUnderMousePointer, window.windowNumber);
        CGEventSetIntegerValueField(cgEvent, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, window.windowNumber);
    }
    PostToApp(cgEvent);
}

void SendOutput(const Output &output) {
    switch (output.Kind) {
    case OutputKind::Key:
        SendKey(output.Vk, output.Down);
        break;
    case OutputKind::MouseButton:
        SendMouseButton(output.Vk, output.Down);
        break;
    case OutputKind::MouseMove: {
        CGPoint position = CurrentCursorPosition();
        CGWarpMouseCursorPosition(CGPointMake(position.x + output.DX, position.y + output.DY));
        break;
    }
    case OutputKind::Wheel:
    case OutputKind::HWheel: {
        int lines = output.Delta / 120;
        bool vertical = output.Kind == OutputKind::Wheel;
        PostToApp(CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, vertical ? lines : 0, vertical ? 0 : -lines));
        break;
    }
    }
}

// ---- Startup ----

void Start() {
    InitKeyTables();

    Host host = {};
    host.SetPad = SdlPad::SetPad;
    host.SetPadState = SdlPad::SetState;
    host.SetCursorCapture = SetCursorCapture;
    host.SendOutput = SendOutput;
    Init(host);

    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    [center addObserverForName:NSApplicationDidBecomeActiveNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification *) {
                        EnsureMouseMovedEvents();
                        SetForeground(true);
                    }];
    [center addObserverForName:NSApplicationDidResignActiveNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification *) {
                        SetForeground(false);
                    }];
    // The cursor capture follows the key window (e.g. when the app opens its stream window).
    [center addObserverForName:NSWindowDidBecomeKeyNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification *) {
                        EnsureMouseMovedEvents();
                    }];
    InstallEventHooks();
    WatchRawMice();
    SetForeground(NSApp.isActive);

    static dispatch_source_t pollTimer;
    pollTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(pollTimer, DISPATCH_TIME_NOW, NSEC_PER_SEC / 2, NSEC_PER_SEC / 10);
    dispatch_source_set_event_handler(pollTimer, ^{
        InstallEventHooks();
        SdlPad::Poll();
    });
    dispatch_resume(pollTimer);
}

__attribute__((constructor)) void OnLoad() {
    // Don't follow the app into helper processes it launches.
    unsetenv("DYLD_INSERT_LIBRARIES");

    // Runs once the app's own run loop is up, which is also after its NSApplication exists.
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            Start();
        }
    });
}

} // namespace
