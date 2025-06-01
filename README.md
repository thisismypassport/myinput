# MyInput
Allows mapping Keyboard/Mouse keys to a Virtual Controller (Gamepad) on a per-game basis, without having to install any drivers.

Can also remap Keyboard/Mouse keys to other Keyboard/Mouse keys.

[You can find the latest release (MyInput_Windows.zip) here.](https://github.com/thisismypassport/myinput/releases/) 

If you find this useful, and have any requests or suggestions, let me know via opening an issue or starting a disussion. (And if you're having trouble getting it to work on your machine or to behave as you'd like, please open an issue)

# Usage

Run myinput.exe - this will open a window, allowing you to run games under MyInput and editing the configuring for each game.

## Run on Demand

To run a game under MyInput, first add it to the list by clicking on "Add without Registering" in the bottom right of the MyInput window, and selecting the game's executable.

Now that it's on the list, you can right-click on it and select "Run with MyInput" whenever you want to run it with MyInput's virtual controller.

Alternatively, in Windows Explorer, just drag the executable you want to run to x64\myinput_inject.exe

## Register to Always Run

To register a game's executable so that - whenever you launch it - it is launched with MyInput, click on "Register New" in the bottom right of the myinput window, and select the executable.

Note: Registering an executable requires admin permissions (as it uses Image File Execution Options to change how the executable runs).

Once added to the list, you can unregister or re-register a game by simply clicking the checkbox next to it.

Alternatively, in Windows Explorer, just drag the executable you want to register to x64\myinput_register.exe (you can drag it again to unregister it)

## Edit Configuration

You can edit the configuration a game uses by double clicking on it in the list - allowing you to specify what inputs map to what, etc.

You can test the configuration without launching the game by switching to the "Test" tab.

Note that by default, newly added games use the same default configuration (called "_default") - so modifying it will affect all games.

To have a game use a different configuration, select the game in the list and use the "Use Config:" drop down at the bottom of the window (clicking "New" to use a new configuration)

### Default Configuration File ("_default")

By default, it maps keyboard keys to a XBox360 controller.

You can switch to the "Configs" tab in MyInput to see exactly what keys it maps to what gamepad buttons - clicking on a mapping will show you more info on it (including a user-friendly description of the button).

For example:
* It maps the "C" key to the "A" button. (And so on for other buttons)
* It maps the "Up" key to the left analog stick up direction. (And so on for other directions)
* It maps the left shift to modify the range of the left analog stick movement by half.
* It maps the "F12" key to reload mappings from the config file.
* It maps the "Pause" key to disable all other mappings until pressed again.

Additionally, it contains a few more example sections disabled by default - you can enable them by clicking the checkbox next to them:
* A "fluidly move right stick while right control is held" section - it uses the "Add strength" feature to allow fine-grained control of an analog stick via the keyboard.
* A "move left-stick in up to 16 directions" section.
* A "switch between multiple gamepads" section - one approach to handle multiple gamepads, see below for others

### Supported Mapping Options

All keyboard keys & mouse buttons can be mapped to any keyboard key, mouse button, or virtual gamepad button.

By default, the virtual gamepad is a XBox360 one (supported by the most games), but it can be changed via the "Configure a virual gamepad's type" global option.

The "Options..." section allows you to configure various options for each mapping:
* Turbo-pressing a key/button (see first drop-down inside Options) - optionally with a custom rate.
* Toggling a key/button (see same drop-down)
* A custom press strength for analog buttons and sticks.
* Adding press strength while the input is pressed, allowing fine-grainer control over an analog stick.
* Optionally forwaring the input key, so that the game sees both the input key and the output button.

The "Conditions..." section allows you to configure under which conditions a mapping will be active.
* You can enable a mapping when some key is pressed, released, toggled, etc.
* You can create a complex condition, to combine (via and/or/not) multiple sub-conditions.

In addition, you can map keys to one of several actions (in the "Actions" section of the Output drop-down), such as toggling the hiding of the mouse cursor.

### Multiple Gamepads

There are several ways to configure multiple gamepads (controllers):
* Specifying exactly which gamepad each mapping is for - by changing "Active Gamepad" to "Gamepad #..." and selecting the gamepad number to use.
* Mapping a key to "Actions" -> "Set Active Gamepad" : allows you to change the active gamepad by pressing a key
* Mapping a key to "Actions" -> "Set Active Gamepad while Held" : allows you to change the active gamepad while a key is held

### Supported Global Options

These are accessible via adding a "New Option" - see the arrow next to "New Mapping":
* You can shake the window when a gamepad's rumble occurs.
* You can specify what shape a gamepad's thumbstick traces (diagonal movement goes further with a square than with a circle)
* You can hide the cursor while the window is in focus. (Also toggle-able via "Actions")
* You can control whether the mapping continues to work while the game is in the background. (Also toggle-able via "Actions")
* You can include one config inside another.
* (And more...)

(Don't forget to toggle the option after selecting it - it starts in its default state)

# Troubleshooting

For Steam Games:
* Make sure to disable Steam Input (either globally or for that specific game)
* You may have better luck if you register the executable to always run, as Steam games often ask Steam to relaunch them, even if you run them directly.

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
