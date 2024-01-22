# myinput
Allows mapping Keyboard/Mouse keys to a Virtual Controller on a per-app basis, without having to install any drivers.

Can also remap Keyboard/Mouse keys to other Keyboard/Mouse keys.

Currently not user-friendly, but functional.

[You can find the latest release (MyInput_Windows.zip) here.](https://github.com/thisismypassport/myinput/releases/) 

If you find this useful, and have any requests or suggestions, let me know via opening an issue or starting a disussion. (And if you're having trouble getting it to work on your machine or to behave as you'd like, please open an issue)

# Usage

You can run myinput.exe to open a window, from which you can:

## One-time Run

To run an executable under myinput just once, click on "Launch New (One-Time)" on the bottom right of the myinput window.

Alternatively, in Windows Explorer, just drag the executable you want to run to x64\myinput_inject.exe

## Register to Always Run

To register an executable so that - whenever you launch it - it is launched through my input, click on "Register New" (near the bottom right of the myinput window).

Alternatively, in Windows Explorer, just drag the executable you want to register to x64\myinput_register.exe (you can drag it again to unregister it)

Note: Registering an executable requires admin permissions (as it uses Image File Execution Options to change how the executable runs).

## Mapping Configuration

The configuration file specify which keys map to which keys/buttons.

You can open the global configuration file by clicking "Edit Global Config" (or opening Configs\_default.ini directly).

### Default Configuration File

By default, it maps keyboard keys to a XBox360 controller.

The lines at the top of the file (that start with a '#') are comments that describe the configuration format, whereas the lines below map keys to various buttons.

For example: (these are all parts of the default configuration)
* It maps the "C" key to the "A" button (written as "%A").
* It maps the "Up" key to the left analog stick up direction (written as "%L.Up")
* It maps the left shift ("LShift") to modify the range of the left analog stick movement ("%Mod.L") by a half ("~ 0.5")
* It maps the "Pause" key to disable all other key mappings until pressed again.
* It maps the "F12" key to reload key mappings from the config file.

Note that lines that start with "#" are ignored.

### Advanced Configuration Examples

You can see more advanced examples in Config\myinput_test.ini

(Do not copy the \#\[ and \#\] line - they cause the whole block to be ignored)

For example: (these are NOT parts of the default configuration, just examples of what you can do)
* It maps the "Numpad1" key to rotate the left analog stick up ("%Rot.L.Up")
* It contains lines mapping keys & mouse buttons to other keys & mouse buttons, such as mapping "MButton" (middle mouse button) to "XButton1" (first extra mouse button)
* It contains lines (below "MISC KEY FEATURES") that maps "Q" to turbo-press "Z", maps "A" to toggle "Z", and maps "1" to toggle turbo of "Z". (and below, you can see toggle & turbo also works on controller buttons)
* At the bottom, it defines 2 controllers ("!Device1 = X360" and "!Device2 = PS4"). Much above, it maps "F2" to "SetActive @2" which switches all mappings to use the 2nd controller.

### Per-Executable Configuration

You can specify a configuration for each executable, by either:
* Clicking "Edit Config" and answering "Yes" to create a configuration for that executable.
* Toggling "Use Config", writing the name of the config in the edit box, and clicking "Apply". (This allows you to have multiple executables using the same custom config file)

# Building from Source

Prerequisite: https://github.com/microsoft/Detours - copy to 3rdparty/detours folder

Then just build with MSVC 2022 or above.

# Implementation Details

It hooks:
- The RawInput API
- The CfgMgr API
- The hid device IOCTLs (used by DirectInput & etc.)
- The xusb device IOCTLs (used by XInput & WGI)
- low-level keyboard & mouse events
- The wbem interface (currently partial, to match existing common uses)
- Misc. APIs to improve compatibility.
