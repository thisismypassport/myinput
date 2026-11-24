#pragma once
#include <cstdint>

// The seam between the two halves of the macOS hook:
//  - Engine.cpp compiles MyInput's shared mapping engine (src/Impl*.h, src/Config*.h) as plain C++.
//  - Hook.mm and SdlPad.mm are Objective-C++ and talk to AppKit and to the host app's SDL.
// Keeping them apart avoids clashes between the engine's Win32-style names and Apple's headers.
// Everything here must be called on the main thread.
namespace MyInputMac {

constexpr int MaxPads = 8;

// Keys are identified by Win32 virtual-key codes, as in the config format.
// Numpad Enter has no code of its own on Windows; the engine uses this one for it.
constexpr int VkNumpadEnter = 0x03;

enum class PadKind {
    None,
    XBox,
    Ds4,
};

struct PadState {
    bool A, B, X, Y, LB, RB, L, R, DL, DR, DU, DD, Start, Back, Guide, Extra;
    double LT, RT;         // 0 .. 1
    double LX, LY, RX, RY; // -1 .. 1, positive is right/up
};

// Keyboard and mouse events the engine wants to generate (e.g. for "Q : Tab" mappings).
enum class OutputKind {
    Key,         // Vk, Down
    MouseButton, // Vk (VK_LBUTTON..VK_XBUTTON2), Down
    MouseMove,   // DX, DY in pixels, positive is right/down
    Wheel,       // Delta, 120 per notch, positive is up
    HWheel,      // Delta, 120 per notch, positive is right
};

struct Output {
    OutputKind Kind;
    int Vk;
    bool Down;
    int DX, DY;
    int Delta;
};

struct Host {
    void (*SetPad)(int user, PadKind kind); // PadKind::None disconnects
    void (*SetPadState)(int user, const PadState &state);
    void (*SetCursorCapture)(bool hide, bool bound);
    void (*SendOutput)(const Output &output);
};

void Init(const Host &host);
void SetForeground(bool foreground);

// Each returns true if the event was consumed by a mapping and must not reach the app.
bool OnKey(int vk, bool down);
bool OnMouseButton(int vk, bool down);
bool OnMouseMove(int dx, int dy);                // positive is right/down
bool OnMouseWheel(bool horizontal, int delta);   // 120 per notch, positive is up/right

void OnRumble(int user, double low, double high); // 0 .. 1
bool IsKeyMapped(int vk);
bool IsDebug();
void LogLine(const char *text);

} // namespace MyInputMac
