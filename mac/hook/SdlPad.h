#pragma once
#include "Bridge.h"

// Virtual gamepads, created inside the host app through its own copy of SDL.
// This is the macOS counterpart of the HID/XInput emulation on Windows: no driver, nothing system-wide.
namespace SdlPad {

void SetPad(int user, MyInputMac::PadKind kind);
void SetState(int user, const MyInputMac::PadState &state);

// Attaches and detaches gamepads as needed. SDL may be loaded or initialized
// long after the hook starts, so this is called on a timer.
void Poll();

} // namespace SdlPad
