# MyInput
Allows mapping Keyboard/Mouse keys to a Virtual Controller (Gamepad) on a per-game basis, without having to install any drivers.

Can also remap Keyboard/Mouse keys to other Keyboard/Mouse keys.

[You can find the latest release (MyInput_Windows.zip) here.](https://github.com/thisismypassport/myinput/releases/) 

On macOS, MyInput is built from source and run from the terminal: see [macOS](#macos) below. The rest of this page describes the Windows app.

If you find this useful, and have any requests or suggestions, let me know via opening an issue or starting a disussion. (And if you're having trouble getting it to work on your machine or to behave as you'd like, please open an issue)

# Usage

Run myinput.exe - this will open a window, allowing you to run games under MyInput and editing the configuration for each game.

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

You can test the configuration without launching the game by switching to the "Test Config" tab.

Also, if the game is running, switching back to it after changing the configuration will automatically reapply it.

Note that by default, newly added games use the same default configuration (called "_default") - so modifying it will affect all games.

To have a game use a different configuration, select the game in the list and use the "Use Config:" drop down at the bottom of the window (clicking "New" to create a new configuration)

### Default Configuration File ("_default")

By default, it maps keyboard keys to a XBox360 controller.

You can switch to the "Configs" tab in MyInput to see exactly what keys it maps to what gamepad buttons - clicking on a mapping will show you more info on it (including a user-friendly description of the button) and allow you to edit it.

For example:
* It maps the "C" key to the "A" button. (And so on for other buttons)
* It maps the "Up" key to the left analog stick up direction. (And so on for other directions)
* It maps the left shift to modify the range of the left analog stick movement by halving it.
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

# macOS

MyInput for macOS maps the keyboard and mouse to a virtual gamepad inside one app, using the same config files and mapping engine as on Windows. No driver, no system extension, and no Accessibility or Input Monitoring permission: it only sees the input of the app it runs in.

It works with apps that read game controllers through SDL2 or SDL3, such as [chiaki-ng](https://github.com/streetpea/chiaki-ng) (PS4/PS5 Remote Play). Apps that read controllers another way, such as through Apple's GameController framework, aren't supported.

## Build and run

You need the Xcode command line tools (`xcode-select --install`). The build targets macOS 12 and later, and has been tested on macOS 26 (Apple silicon).

```
mac/build.sh
build/mac/myinput chiaki-ng
```

`myinput <app>` takes an app name from /Applications, a path to an `.app`, or a path to an executable. Arguments after it go to the app. `myinput --help` lists the options.

macOS only lets a library into an app that opted in, and most downloaded apps haven't. For those, `myinput` makes a copy of the app in `~/Library/Application Support/MyInput/Apps`, re-signs that copy locally so it accepts the hook, and runs the copy. Your original app is never modified. The copy is a clone, so it takes almost no disk space, and it is rebuilt when the original is updated. Because it carries a different signature, macOS asks again for permissions the app needs, such as local network access for chiaki-ng. It shares the original's settings.

## Configs

Configs live in `~/Library/Application Support/MyInput/Configs`, with the same syntax as on Windows (described at the top of `_default.ini`). An app uses the config named after its executable, or `_default.ini` if there is none, or the one given with `--config <name>`. Edit them in any text editor: a changed config is reloaded when you switch back to the app.

Each app writes a log to `~/Library/Logs/MyInput`, which lists the mappings it loaded and any line it couldn't read.

Keys keep their Windows names:

* `LWindows` / `RWindows` / `Windows` are the Command keys, and `LAlt` / `RAlt` / `Alt` the Option keys.
* Mac keyboards have no `Pause`, `ScrollLock` or `PrintScreen`. `_default.ini` toggles the mappings with `Pause`: pick another key there.
* While Command is held, keys go to the app untouched, so its shortcuts (Cmd+Q, Cmd+F, ...) keep working. Mapping a Command key turns this off.
* `%Extra` is the touchpad click of a PlayStation controller.
* `!Device1 = ps4` makes the first virtual gamepad identify as a DualShock 4 instead of an Xbox pad.

## Mouse look

Mappings like `Mouse.Left : %R.Left` work as on Windows, but they treat mouse movement as a held direction, which is too coarse for aiming. macOS has a mode made for it, where the faster the mouse moves, the further the stick is pushed:

```
!mac/MouseLook = R
!mac/MouseLook.Sensitivity = 1.0
!mac/MouseLook.Deadzone = 0.10
!mac/MouseLook.Curve = 1.0
!mac/MouseLook.Vertical = 1.0
!mac/MouseLook.Smoothing = 0.03
```

* `MouseLook` is the stick to drive: `R`, `L` or `Off`. The other lines are optional, and show the defaults.
* `Sensitivity` is how far the stick goes for a given mouse speed. At 1, about 1000 pixels per second pushes it all the way.
* A mouse is read raw, without the system's pointer acceleration, as on Windows. A trackpad has no raw movement to read, so it goes through pointer acceleration.
* `Deadzone` is the part of the stick the game ignores. The slightest movement starts from its edge, which is what lets small movements through. Match it to the game: too low and slow movements do nothing, too high and the camera lurches.
* `Curve` shapes the response: 1 is linear, below 1 gives slow movements more effect.
* `Vertical` gives up-and-down movement more (or less) stick than sideways movement, for games that turn slower vertically.
* `Smoothing` is the time, in seconds, the mouse speed is averaged over.
* A stick pushed all the way is as fast as the game turns. When the mouse is faster than that, the rest of the movement is played back right after (up to 0.15 seconds of it), so a quick flick ends where the same movement made slowly would.

In the game, look for these settings: the smallest stick deadzone, a linear response curve, and no ramp-up time for the camera. Check what the ramp-up setting means, since games name it differently: in GTA 6 it is "acceleration", and the highest value is the one that reaches full speed at once. The camera still can't turn faster than the game lets a stick turn it.

## chiaki-ng (GTA 6 and other PS5 games with mouse and keyboard)

`chiaki.ini` is installed on first run with a layout for GTA 6's default control scheme: WASD to move, mouse to look, left and right click to shoot and aim, Shift to run, Space to jump. The file itself lists every binding.

A controller uses the same buttons for different things on foot and in a vehicle, so the layout has two modes. `F` enters or exits a vehicle and switches mode, which makes `W` and `S` accelerate and brake. `E` is the same button without the switch, for doors and items. `Caps Lock` switches mode by hand when it gets out of step with the game.

1. Run `build/mac/myinput chiaki-ng` and connect to your console as usual.
2. Once the game is on screen, press **Right Option**. This turns the mappings on and captures the mouse.
3. Press Right Option again to get the cursor back, for example to use chiaki-ng's menus.

The preset uses [mouse look](#mouse-look) on the right stick. Raise the game's camera and aim sensitivity, set its stick deadzone to the minimum and its response curve to linear, then tune the `!mac/MouseLook` lines in `chiaki.ini`.

## Not on macOS yet

* The configuration UI and registering an app to always run with MyInput. Edit the `.ini` files, and start apps with `myinput`.
* `!RumbleWindow`, `%Motion.*` outputs, plugins, and injecting into child processes.
* Generated input (`Q : Tab`, `G : RButton`, ...) reaches the hooked app only, not other apps. Generated mouse movement (`... : Mouse.Left`) is untested.

# Building from Source

For macOS, see [macOS](#macos) above. For Windows:

Prerequisite: https://github.com/microsoft/Detours - copy to 3rdparty/detours folder, then build (via Detours.sln in the vc subfolder) for both x64 and x86.

Then just build (via src/myinput.sln) for both x64 and x86, using MSVC 2022 or above.

# Implementation Details

It hooks:
- The RawInput API
- The CfgMgr API
- The hid device IOCTLs (used by DirectInput & etc.)
- The xusb device IOCTLs (used by XInput & WGI)
- low-level keyboard & mouse events
- The wbem interface (currently partial, to match existing common uses)
- Misc. APIs to improve compatibility.

On macOS (see `mac/`), the hook is a dylib loaded into the app with `DYLD_INSERT_LIBRARIES`. It takes the app's own keyboard and mouse events as AppKit dequeues and dispatches them, runs them through the same mapping engine (compiled unmodified from `src/`, with `mac/compat` standing in for the Windows headers), and creates the virtual gamepad through the app's own copy of SDL. `mac/test/run.sh` runs an end-to-end test of that path against SDL2 and SDL3.

# Attributions

Icon from https://icon-icons.com/icon/gamepad-console/73549
