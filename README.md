# myinput
Allows mapping Keyboard/Mouse keys to a Virtual Controller on a per-app basis, without having to install any drivers.

Can also remap Keyboard/Mouse keys to other Keyboard/Mouse keys.

Currently not user-friendly, but functional.

[You can find the latest release (MyInput_Windows.zip) here.](https://github.com/thisismypassport/myinput/releases/) 

If you find this useful, and have any requests or suggestions, let me know via opening an issue or starting a disussion. (And if you're having trouble getting it to work on your machine or to behave as you'd like, please open an issue)

# Usage

## One-time Run

You can always drag an executable to Release-x64\myinput_inject.exe to run it with myinput just this time.

## Register to Always Run

You can drag an executable to Release-x64\myinput_register.exe to register it so that myinput will always run with it.

You can drag it again to unregister it.

You can also run Release-x64\myinput_register.exe directly to see what is registered and unregister it (also contains a few more shortcut buttons to edit configs, etc.)

Using myinput_register.exe requires admin permissions (as it uses Image File Execution Options to change how the executable runs).

## Mapping Configuration

Edit Configs/_default.ini to change the default key mapping.

By default, the file maps keyboard keys to a XBox360 controller.

You can see various mapping examples in Config/myinput_test.ini and Config/myinput_test_subconf.ini (do not copy the \#\[ and \#\] lines)

You can see a reference of the configuration file format in Config/_default.ini

## Per-Executable Mapping

If you have an executable named Hello.exe, you can create a Configs/Hello.ini file with the configuration for that executable. (Used instead of the default config).

# Building from Source

Prerequisite: https://github.com/microsoft/Detours - copy to 3rdparty/detours folder

Then just build with MSVC 2022 or above.

# Implementation Details

It hooks:
- The RawInput API
- The CfgMgr API
- The hid device IOCTLs (used by DirectInput)
- The xusb device IOCTLs (used by XInput)
- low-level keyboard & mouse events
- Misc. APIs to improve compatibility.
