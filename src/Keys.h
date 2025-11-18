#pragma once
#include "UtilsBase.h"
#include <Windows.h>

using key_t = int;

// (Special custom vk definitions. should avoid rearranging)
enum {
    MY_VK_UNKNOWN = 0,
    MY_VK_NUMPAD_RETURN = VK_CANCEL, // something unused, but within public range
    MY_VK_LAST_REAL = 0x100,

    MY_VK_FIRST_MOUSE_AXIS = 0x1200,
    MY_VK_MOUSE_UP,
    MY_VK_MOUSE_DOWN,
    MY_VK_MOUSE_LEFT,
    MY_VK_MOUSE_RIGHT,
    MY_VK_WHEEL_UP,
    MY_VK_WHEEL_DOWN,
    MY_VK_WHEEL_LEFT,
    MY_VK_WHEEL_RIGHT,
    MY_VK_LAST_MOUSE_AXIS,

    MY_VK_FIRST_PAD = 0x1500,
    MY_VK_PAD_A,
    MY_VK_PAD_B,
    MY_VK_PAD_X,
    MY_VK_PAD_Y,
    MY_VK_PAD_START,
    MY_VK_PAD_BACK,
    MY_VK_PAD_GUIDE,
    MY_VK_PAD_EXTRA,
    MY_VK_PAD_DPAD_LEFT,
    MY_VK_PAD_DPAD_RIGHT,
    MY_VK_PAD_DPAD_UP,
    MY_VK_PAD_DPAD_DOWN,
    MY_VK_PAD_LTHUMB_LEFT,
    MY_VK_PAD_LTHUMB_RIGHT,
    MY_VK_PAD_LTHUMB_UP,
    MY_VK_PAD_LTHUMB_DOWN,
    MY_VK_PAD_RTHUMB_LEFT,
    MY_VK_PAD_RTHUMB_RIGHT,
    MY_VK_PAD_RTHUMB_UP,
    MY_VK_PAD_RTHUMB_DOWN,
    MY_VK_PAD_LSHOULDER,
    MY_VK_PAD_RSHOULDER,
    MY_VK_PAD_LTRIGGER,
    MY_VK_PAD_RTRIGGER,
    MY_VK_PAD_LTHUMB_PRESS,
    MY_VK_PAD_RTHUMB_PRESS,
    MY_VK_PAD_MOTION_LEFT,
    MY_VK_PAD_MOTION_RIGHT,
    MY_VK_PAD_MOTION_UP,
    MY_VK_PAD_MOTION_DOWN,
    MY_VK_PAD_MOTION_NEAR,
    MY_VK_PAD_MOTION_FAR,
    MY_VK_PAD_MOTION_ROT_LEFT,
    MY_VK_PAD_MOTION_ROT_RIGHT,
    MY_VK_PAD_MOTION_ROT_UP,
    MY_VK_PAD_MOTION_ROT_DOWN,
    MY_VK_PAD_MOTION_ROT_CCW,
    MY_VK_PAD_MOTION_ROT_CW,
    MY_VK_LAST_PAD,

    MY_VK_FIRST_KEY_OR_PAIR = 0x2100,
    MY_VK_WIN,
    MY_VK_OEM_3_8,
    MY_VK_OEM_5_102,
    MY_VK_LAST_KEY_OR_PAIR,

    MY_VK_FIRST_KEY_AND_PAIR = 0x2180,
    MY_VK_UP_LEFT,
    MY_VK_UP_RIGHT,
    MY_VK_DOWN_LEFT,
    MY_VK_DOWN_RIGHT,
    MY_VK_LAST_KEY_AND_PAIR,

    MY_VK_FIRST_MOUSE_AXIS_AND_PAIR = 0x2280,
    MY_VK_MOUSE_UP_LEFT,
    MY_VK_MOUSE_UP_RIGHT,
    MY_VK_MOUSE_DOWN_LEFT,
    MY_VK_MOUSE_DOWN_RIGHT,
    MY_VK_LAST_MOUSE_AXIS_AND_PAIR,

    MY_VK_FIRST_PAD_AND_PAIR = 0x2580,
    MY_VK_PAD_DPAD_UP_LEFT,
    MY_VK_PAD_DPAD_UP_RIGHT,
    MY_VK_PAD_DPAD_DOWN_LEFT,
    MY_VK_PAD_DPAD_DOWN_RIGHT,
    MY_VK_PAD_LTHUMB_UP_LEFT,
    MY_VK_PAD_LTHUMB_UP_RIGHT,
    MY_VK_PAD_LTHUMB_DOWN_LEFT,
    MY_VK_PAD_LTHUMB_DOWN_RIGHT,
    MY_VK_PAD_RTHUMB_UP_LEFT,
    MY_VK_PAD_RTHUMB_UP_RIGHT,
    MY_VK_PAD_RTHUMB_DOWN_LEFT,
    MY_VK_PAD_RTHUMB_DOWN_RIGHT,
    MY_VK_LAST_PAD_AND_PAIR,

    MY_VK_FIRST_PAD_MODIFIER = 0x8500,
    MY_VK_PAD_LTHUMB_HORZ_MODIFIER,
    MY_VK_PAD_LTHUMB_VERT_MODIFIER,
    MY_VK_PAD_RTHUMB_HORZ_MODIFIER,
    MY_VK_PAD_RTHUMB_VERT_MODIFIER,
    MY_VK_PAD_LTRIGGER_MODIFIER,
    MY_VK_PAD_RTRIGGER_MODIFIER,
    MY_VK_PAD_LTHUMB_UP_ROTATOR,
    MY_VK_PAD_LTHUMB_DOWN_ROTATOR,
    MY_VK_PAD_LTHUMB_RIGHT_ROTATOR,
    MY_VK_PAD_LTHUMB_LEFT_ROTATOR,
    MY_VK_PAD_LTHUMB_UP_LEFT_ROTATOR,
    MY_VK_PAD_LTHUMB_DOWN_LEFT_ROTATOR,
    MY_VK_PAD_LTHUMB_UP_RIGHT_ROTATOR,
    MY_VK_PAD_LTHUMB_DOWN_RIGHT_ROTATOR,
    MY_VK_PAD_RTHUMB_UP_ROTATOR,
    MY_VK_PAD_RTHUMB_DOWN_ROTATOR,
    MY_VK_PAD_RTHUMB_RIGHT_ROTATOR,
    MY_VK_PAD_RTHUMB_LEFT_ROTATOR,
    MY_VK_PAD_RTHUMB_UP_LEFT_ROTATOR,
    MY_VK_PAD_RTHUMB_DOWN_LEFT_ROTATOR,
    MY_VK_PAD_RTHUMB_UP_RIGHT_ROTATOR,
    MY_VK_PAD_RTHUMB_DOWN_RIGHT_ROTATOR,
    MY_VK_PAD_LTHUMB_ROTATOR_MODIFIER,
    MY_VK_PAD_RTHUMB_ROTATOR_MODIFIER,
    MY_VK_LAST_PAD_MODIFIER,

    MY_VK_FIRST_PAD_MODIFIER_AND_PAIR = 0x9580,
    MY_VK_PAD_LTHUMB_MODIFIER,
    MY_VK_PAD_RTHUMB_MODIFIER,
    MY_VK_PAD_TRIGGER_MODIFIER,
    MY_VK_LAST_PAD_MODIFIER_AND_PAIR,

    MY_VK_FIRST_CMD = 0xc000,
    MY_VK_NONE,
    MY_VK_RELOAD,
    MY_VK_TOGGLE_DISABLE,
    MY_VK_TOGGLE_HIDE_CURSOR,
    MY_VK_TOGGLE_ALWAYS,
    MY_VK_TOGGLE_SPARE_FOR_DEBUG,
    MY_VK_LOAD_CONFIG,
    MY_VK_TOGGLE_BOUND_CURSOR,
    MY_VK_LAST_CMD,

    MY_VK_FIRST_USER_CMD = 0xc500,
    MY_VK_SET_ACTIVE_USER,
    MY_VK_HOLD_ACTIVE_USER,
    MY_VK_TOGGLE_CONNECTED,
    MY_VK_LAST_USER_CMD,

    MY_VK_FIRST_PAD_OUTPUT = 0xd500, // currently not covered by MyVkType/etc
    MY_VK_PAD_RUMBLE_LOW,
    MY_VK_PAD_RUMBLE_HIGH,
    MY_VK_LAST_PAD_OUTPUT,

    MY_VK_FIRST_META = 0xf000,
    MY_VK_META_COND_AND,
    MY_VK_META_COND_OR,
    MY_VK_LAST_META,

    MY_VK_CUSTOM_START = 0x10000000,
};

enum class MyVkGroup {
    None,
    Gamepad,
    GamepadDiagonal,
    GamepadModifier,
    GamepadRotator,
    GamepadMotion,
    Mouse,
    Keyboard,
    KeyboardChar,
    KeyboardNumpad,
    KeyboardRare,
    KeyboardMedia,
    Command,
    CommandDebug,
};

// (in ui order, keys with same MyVkGroup must be consectuive)
#define ENUMERATE_KEYS_IMPL(e, e_simple)                                                                                                                  \
    /* Gamepad group */                                                                                                                                   \
    e(MY_VK_PAD_A, "%A", L"Gamepad A", Gamepad, "%a");                                                                                                    \
    e(MY_VK_PAD_B, "%B", L"Gamepad B", Gamepad, "%b");                                                                                                    \
    e(MY_VK_PAD_X, "%X", L"Gamepad X", Gamepad, "%x");                                                                                                    \
    e(MY_VK_PAD_Y, "%Y", L"Gamepad Y", Gamepad, "%y");                                                                                                    \
    e(MY_VK_PAD_START, "%Start", L"Gamepad Start", Gamepad, "%start");                                                                                    \
    e(MY_VK_PAD_BACK, "%Back", L"Gamepad Back", Gamepad, "%back");                                                                                        \
    e(MY_VK_PAD_GUIDE, "%Guide", L"Gamepad Guide", Gamepad, "%guide");                                                                                    \
    e(MY_VK_PAD_EXTRA, "%Extra", L"Gamepad Extra", Gamepad, "%extra");                                                                                    \
    e(MY_VK_PAD_LSHOULDER, "%LB", L"Gamepad Left Shoulder", Gamepad, "%lb");                                                                              \
    e(MY_VK_PAD_RSHOULDER, "%RB", L"Gamepad Right Shoulder", Gamepad, "%rb");                                                                             \
    e(MY_VK_PAD_LTRIGGER, "%LT", L"Gamepad Left Trigger", Gamepad, "%lt");                                                                                \
    e(MY_VK_PAD_RTRIGGER, "%RT", L"Gamepad Right Trigger", Gamepad, "%rt");                                                                               \
    e(MY_VK_PAD_LTHUMB_PRESS, "%L", L"Gamepad L.Stick Press", Gamepad, "%l");                                                                             \
    e(MY_VK_PAD_RTHUMB_PRESS, "%R", L"Gamepad R.Stick Press", Gamepad, "%r");                                                                             \
    e(MY_VK_PAD_DPAD_UP, "%D.Up", L"Gamepad DPad Up", Gamepad, "%dup");                                                                                   \
    e(MY_VK_PAD_DPAD_DOWN, "%D.Down", L"Gamepad DPad Down", Gamepad, "%ddown");                                                                           \
    e(MY_VK_PAD_DPAD_LEFT, "%D.Left", L"Gamepad DPad Left", Gamepad, "%dleft");                                                                           \
    e(MY_VK_PAD_DPAD_RIGHT, "%D.Right", L"Gamepad DPad Right", Gamepad, "%dright");                                                                       \
    e(MY_VK_PAD_LTHUMB_UP, "%L.Up", L"Gamepad L.Stick Up", Gamepad, "%lup");                                                                              \
    e(MY_VK_PAD_LTHUMB_DOWN, "%L.Down", L"Gamepad L.Stick Down", Gamepad, "%ldown");                                                                      \
    e(MY_VK_PAD_LTHUMB_LEFT, "%L.Left", L"Gamepad L.Stick Left", Gamepad, "%lleft");                                                                      \
    e(MY_VK_PAD_LTHUMB_RIGHT, "%L.Right", L"Gamepad L.Stick Right", Gamepad, "%lright");                                                                  \
    e(MY_VK_PAD_RTHUMB_UP, "%R.Up", L"Gamepad R.Stick Up", Gamepad, "%rup");                                                                              \
    e(MY_VK_PAD_RTHUMB_DOWN, "%R.Down", L"Gamepad R.Stick Down", Gamepad, "%rdown");                                                                      \
    e(MY_VK_PAD_RTHUMB_LEFT, "%R.Left", L"Gamepad R.Stick Left", Gamepad, "%rleft");                                                                      \
    e(MY_VK_PAD_RTHUMB_RIGHT, "%R.Right", L"Gamepad R.Stick Right", Gamepad, "%rright");                                                                  \
    /* Gamepad Diagonal group */                                                                                                                          \
    e(MY_VK_PAD_DPAD_UP_LEFT, "%D.UpLeft", L"Gamepad DPad Up+Left", GamepadDiagonal, "%dupleft", "%dleftup");                                             \
    e(MY_VK_PAD_DPAD_UP_RIGHT, "%D.UpRight", L"Gamepad DPad Up+Right", GamepadDiagonal, "%dupright", "%drightup");                                        \
    e(MY_VK_PAD_DPAD_DOWN_LEFT, "%D.DownLeft", L"Gamepad DPad Down+Left", GamepadDiagonal, "%ddownleft", "%dleftdown");                                   \
    e(MY_VK_PAD_DPAD_DOWN_RIGHT, "%D.DownRight", L"Gamepad DPad Down+Right", GamepadDiagonal, "%ddownright", "%drightdown");                              \
    e(MY_VK_PAD_LTHUMB_UP_LEFT, "%L.UpLeft", L"Gamepad L.Stick Up+Left", GamepadDiagonal, "%lupleft", "%lleftup");                                        \
    e(MY_VK_PAD_LTHUMB_UP_RIGHT, "%L.UpRight", L"Gamepad L.Stick Up+Right", GamepadDiagonal, "%lupright", "%lrightup");                                   \
    e(MY_VK_PAD_LTHUMB_DOWN_LEFT, "%L.DownLeft", L"Gamepad L.Stick Down+Left", GamepadDiagonal, "%ldownleft", "%lleftdown");                              \
    e(MY_VK_PAD_LTHUMB_DOWN_RIGHT, "%L.DownRight", L"Gamepad L.Stick Down+Right", GamepadDiagonal, "%ldownright", "%lrightdown");                         \
    e(MY_VK_PAD_RTHUMB_UP_LEFT, "%R.UpLeft", L"Gamepad R.Stick Up+Left", GamepadDiagonal, "%rupleft", "%rleftup");                                        \
    e(MY_VK_PAD_RTHUMB_UP_RIGHT, "%R.UpRight", L"Gamepad R.Stick Up+Right", GamepadDiagonal, "%rupright", "%rrightup");                                   \
    e(MY_VK_PAD_RTHUMB_DOWN_LEFT, "%R.DownLeft", L"Gamepad R.Stick Down+Left", GamepadDiagonal, "%rdownleft", "%rleftdown");                              \
    e(MY_VK_PAD_RTHUMB_DOWN_RIGHT, "%R.DownRight", L"Gamepad R.Stick Down+Right", GamepadDiagonal, "%rdownright", "%rrightdown");                         \
    /* Gamepad Modifier group */                                                                                                                          \
    e(MY_VK_PAD_LTHUMB_MODIFIER, "%Mod.L", L"Gamepad L.Stick Modifier", GamepadModifier, "%modl");                                                        \
    e(MY_VK_PAD_LTHUMB_HORZ_MODIFIER, "%Mod.LX", L"Gamepad L.Stick X-Axis Modifier", GamepadModifier, "%modlx");                                          \
    e(MY_VK_PAD_LTHUMB_VERT_MODIFIER, "%Mod.LY", L"Gamepad L.Stick Y-Axis Modifier", GamepadModifier, "%modly");                                          \
    e(MY_VK_PAD_RTHUMB_MODIFIER, "%Mod.R", L"Gamepad R.Stick Modifier", GamepadModifier, "%modr");                                                        \
    e(MY_VK_PAD_RTHUMB_HORZ_MODIFIER, "%Mod.RX", L"Gamepad R.Stick X-Axis Modifier", GamepadModifier, "%modrx");                                          \
    e(MY_VK_PAD_RTHUMB_VERT_MODIFIER, "%Mod.RY", L"Gamepad R.Stick Y-Axis Modifier", GamepadModifier, "%modry");                                          \
    e(MY_VK_PAD_TRIGGER_MODIFIER, "%Mod.T", L"Gamepad Trigger Modifier", GamepadModifier, "%modt");                                                       \
    e(MY_VK_PAD_LTRIGGER_MODIFIER, "%Mod.LT", L"Gamepad Left Trigger Modifier", GamepadModifier, "%modlt");                                               \
    e(MY_VK_PAD_RTRIGGER_MODIFIER, "%Mod.RT", L"Gamepad Right Trigger Modifier", GamepadModifier, "%modrt");                                              \
    /* Gamepad Rotator group */                                                                                                                           \
    e(MY_VK_PAD_LTHUMB_UP_ROTATOR, "%Rot.L.Up", L"Gamepad L.Stick Rotate Up", GamepadRotator, "%rotlup");                                                 \
    e(MY_VK_PAD_LTHUMB_DOWN_ROTATOR, "%Rot.L.Down", L"Gamepad L.Stick Rotate Down", GamepadRotator, "%rotldown");                                         \
    e(MY_VK_PAD_LTHUMB_LEFT_ROTATOR, "%Rot.L.Left", L"Gamepad L.Stick Rotate Left", GamepadRotator, "%rotlleft");                                         \
    e(MY_VK_PAD_LTHUMB_RIGHT_ROTATOR, "%Rot.L.Right", L"Gamepad L.Stick Rotate Right", GamepadRotator, "%rotlright");                                     \
    e(MY_VK_PAD_LTHUMB_UP_LEFT_ROTATOR, "%Rot.L.UpLeft", L"Gamepad L.Stick Rotate Up+Left", GamepadRotator, "%rotlupleft", "%rotlleftup");                \
    e(MY_VK_PAD_LTHUMB_UP_RIGHT_ROTATOR, "%Rot.L.UpRight", L"Gamepad L.Stick Rotate Up+Right", GamepadRotator, "%rotlupright", "%rotlrightup");           \
    e(MY_VK_PAD_LTHUMB_DOWN_LEFT_ROTATOR, "%Rot.L.DownLeft", L"Gamepad L.Stick Rotate Down+Left", GamepadRotator, "%rotldownleft", "%rotlleftdown");      \
    e(MY_VK_PAD_LTHUMB_DOWN_RIGHT_ROTATOR, "%Rot.L.DownRight", L"Gamepad L.Stick Rotate Down+Right", GamepadRotator, "%rotldownright", "%rotlrightdown"); \
    e(MY_VK_PAD_RTHUMB_UP_ROTATOR, "%Rot.R.Up", L"Gamepad R.Stick Rotate Up", GamepadRotator, "%rotrup");                                                 \
    e(MY_VK_PAD_RTHUMB_DOWN_ROTATOR, "%Rot.R.Down", L"Gamepad R.Stick Rotate Down", GamepadRotator, "%rotrdown");                                         \
    e(MY_VK_PAD_RTHUMB_LEFT_ROTATOR, "%Rot.R.Left", L"Gamepad R.Stick Rotate Left", GamepadRotator, "%rotrleft");                                         \
    e(MY_VK_PAD_RTHUMB_RIGHT_ROTATOR, "%Rot.R.Right", L"Gamepad R.Stick Rotate Right", GamepadRotator, "%rotrright");                                     \
    e(MY_VK_PAD_RTHUMB_UP_LEFT_ROTATOR, "%Rot.R.UpLeft", L"Gamepad R.Stick Rotate Up+Left", GamepadRotator, "%rotrupleft", "%rotrleftup");                \
    e(MY_VK_PAD_RTHUMB_UP_RIGHT_ROTATOR, "%Rot.R.UpRight", L"Gamepad R.Stick Rotate Up+Right", GamepadRotator, "%rotrupright", "%rotrrightup");           \
    e(MY_VK_PAD_RTHUMB_DOWN_LEFT_ROTATOR, "%Rot.R.DownLeft", L"Gamepad R.Stick Rotate Down+Left", GamepadRotator, "%rotrdownleft", "%rotrleftdown");      \
    e(MY_VK_PAD_RTHUMB_DOWN_RIGHT_ROTATOR, "%Rot.R.DownRight", L"Gamepad R.Stick Rotate Down+Right", GamepadRotator, "%rotrdownright", "%rotrrightdown"); \
    e(MY_VK_PAD_LTHUMB_ROTATOR_MODIFIER, "%Rot.Mod.L", L"Gamepad L.Stick Rotate Modifier", GamepadRotator, "%rotmodl", "%modrotl");                       \
    e(MY_VK_PAD_RTHUMB_ROTATOR_MODIFIER, "%Rot.Mod.R", L"Gamepad R.Stick Rotate Modifier", GamepadRotator, "%rotmodr", "%modrotr");                       \
    /* Gamepad Motion group */                                                                                                                            \
    e(MY_VK_PAD_MOTION_UP, "%Motion.Up", L"Gamepad Motion Up", GamepadMotion, "%motionup");                                                               \
    e(MY_VK_PAD_MOTION_DOWN, "%Motion.Down", L"Gamepad Motion Down", GamepadMotion, "%motiondown");                                                       \
    e(MY_VK_PAD_MOTION_LEFT, "%Motion.Left", L"Gamepad Motion Left", GamepadMotion, "%motionleft");                                                       \
    e(MY_VK_PAD_MOTION_RIGHT, "%Motion.Right", L"Gamepad Motion Right", GamepadMotion, "%motionright");                                                   \
    e(MY_VK_PAD_MOTION_NEAR, "%Motion.Near", L"Gamepad Motion Near", GamepadMotion, "%motionnear");                                                       \
    e(MY_VK_PAD_MOTION_FAR, "%Motion.Far", L"Gamepad Motion Far", GamepadMotion, "%motionfar");                                                           \
    e(MY_VK_PAD_MOTION_ROT_UP, "%Motion.Rot.Up", L"Gamepad Rotate Up", GamepadMotion, "%motionrotup");                                                    \
    e(MY_VK_PAD_MOTION_ROT_DOWN, "%Motion.Rot.Down", L"Gamepad Rotate Down", GamepadMotion, "%motionrotdown");                                            \
    e(MY_VK_PAD_MOTION_ROT_LEFT, "%Motion.Rot.Left", L"Gamepad Rotate Left", GamepadMotion, "%motionrotleft");                                            \
    e(MY_VK_PAD_MOTION_ROT_RIGHT, "%Motion.Rot.Right", L"Gamepad Rotate Right", GamepadMotion, "%motionrotright");                                        \
    e(MY_VK_PAD_MOTION_ROT_CCW, "%Motion.Rot.CCW", L"Gamepad Rotate Counter-clockwise", GamepadMotion, "%motionrotccw");                                  \
    e(MY_VK_PAD_MOTION_ROT_CW, "%Motion.Rot.CW", L"Gamepad Rotate Clockwise", GamepadMotion, "%motionrotcw");                                             \
    /* Mouse group */                                                                                                                                     \
    e(VK_LBUTTON, "LButton", L"Left Mouse Button", Mouse, "lbutton");                                                                                     \
    e(VK_RBUTTON, "RButton", L"Right Mouse Button", Mouse, "rbutton");                                                                                    \
    e(VK_MBUTTON, "MButton", L"Middle Mouse Button", Mouse, "mbutton");                                                                                   \
    e(VK_XBUTTON1, "XButton1", L"'Back' Mouse Button", Mouse, "xbutton1");                                                                                \
    e(VK_XBUTTON2, "XButton2", L"'Forward' Mouse Button", Mouse, "xbutton2");                                                                             \
    e(MY_VK_WHEEL_UP, "Wheel.Up", L"Mouse Wheel Up", Mouse, "wheelup");                                                                                   \
    e(MY_VK_WHEEL_DOWN, "Wheel.Down", L"Mouse Wheel Down", Mouse, "wheeldown");                                                                           \
    e(MY_VK_WHEEL_LEFT, "Wheel.Left", L"Mouse Wheel Left", Mouse, "wheelleft");                                                                           \
    e(MY_VK_WHEEL_RIGHT, "Wheel.Right", L"Mouse Wheel Right", Mouse, "wheelright");                                                                       \
    e(MY_VK_MOUSE_UP, "Mouse.Up", L"Mouse Up", Mouse, "mouseup");                                                                                         \
    e(MY_VK_MOUSE_DOWN, "Mouse.Down", L"Mouse Down", Mouse, "mousedown");                                                                                 \
    e(MY_VK_MOUSE_LEFT, "Mouse.Left", L"Mouse Left", Mouse, "mouseleft");                                                                                 \
    e(MY_VK_MOUSE_RIGHT, "Mouse.Right", L"Mouse Right", Mouse, "mouseright");                                                                             \
    e(MY_VK_MOUSE_UP_LEFT, "Mouse.UpLeft", L"Mouse Up+Left", Mouse, "mouseupleft", "mouseleftup");                                                        \
    e(MY_VK_MOUSE_UP_RIGHT, "Mouse.UpRight", L"Mouse Up+Right", Mouse, "mouseupright", "mouserightup");                                                   \
    e(MY_VK_MOUSE_DOWN_LEFT, "Mouse.DownLeft", L"Mouse Down+Left", Mouse, "mousedownleft", "mouseleftdown");                                              \
    e(MY_VK_MOUSE_DOWN_RIGHT, "Mouse.DownRight", L"Mouse Down+Right", Mouse, "mousedownright", "mouserightdown");                                         \
    /* Keyboard group */                                                                                                                                  \
    e(VK_BACK, "Backspace", L"Backspace", Keyboard, "backspace");                                                                                         \
    e(VK_TAB, "Tab", L"Tab", Keyboard, "tab");                                                                                                            \
    e(VK_RETURN, "Enter", L"Enter", Keyboard, "enter", "return");                                                                                         \
    e(VK_ESCAPE, "Escape", L"Escape", Keyboard, "escape");                                                                                                \
    e(VK_SPACE, "Space", L"Space", Keyboard, "space");                                                                                                    \
    e(VK_UP, "Up", L"Up", Keyboard, "up");                                                                                                                \
    e(VK_DOWN, "Down", L"Down", Keyboard, "down");                                                                                                        \
    e(VK_LEFT, "Left", L"Left", Keyboard, "left");                                                                                                        \
    e(VK_RIGHT, "Right", L"Right", Keyboard, "right");                                                                                                    \
    e(MY_VK_UP_LEFT, "UpLeft", L"Up+Left", Keyboard, "upleft", "leftup");                                                                                 \
    e(MY_VK_UP_RIGHT, "UpRight", L"Up+Right", Keyboard, "upright", "rightup");                                                                            \
    e(MY_VK_DOWN_LEFT, "DownLeft", L"Down+Left", Keyboard, "downleft", "leftdown");                                                                       \
    e(MY_VK_DOWN_RIGHT, "DownRight", L"Down+Right", Keyboard, "downright", "rightdown");                                                                  \
    e(VK_PRIOR, "PageUp", L"Page Up", Keyboard, "pageup", "pgup");                                                                                        \
    e(VK_NEXT, "PageDown", L"Page Down", Keyboard, "pagedown", "pgdn");                                                                                   \
    e(VK_HOME, "Home", L"Home", Keyboard, "home");                                                                                                        \
    e(VK_END, "End", L"End", Keyboard, "end");                                                                                                            \
    e(VK_INSERT, "Insert", L"Insert", Keyboard, "insert", "ins");                                                                                         \
    e(VK_DELETE, "Delete", L"Delete", Keyboard, "delete", "del");                                                                                         \
    e(VK_SNAPSHOT, "PrintScreen", L"Print Screen", Keyboard, "printscreen");                                                                              \
    e(VK_LWIN, "LWindows", L"Left Windows Key", Keyboard, "lwindows", "lwin");                                                                            \
    e(VK_RWIN, "RWindows", L"Right Windows Key", Keyboard, "rwindows", "rwin");                                                                           \
    e(MY_VK_WIN, "Windows", L"Windows Key (either)", Keyboard, "windows", "win");                                                                         \
    e(VK_APPS, "ContextMenu", L"Context Menu", Keyboard, "contextmenu");                                                                                  \
    e(VK_F1, "F1", L"F1", Keyboard, "f1");                                                                                                                \
    e(VK_F2, "F2", L"F2", Keyboard, "f2");                                                                                                                \
    e(VK_F3, "F3", L"F3", Keyboard, "f3");                                                                                                                \
    e(VK_F4, "F4", L"F4", Keyboard, "f4");                                                                                                                \
    e(VK_F5, "F5", L"F5", Keyboard, "f5");                                                                                                                \
    e(VK_F6, "F6", L"F6", Keyboard, "f6");                                                                                                                \
    e(VK_F7, "F7", L"F7", Keyboard, "f7");                                                                                                                \
    e(VK_F8, "F8", L"F8", Keyboard, "f8");                                                                                                                \
    e(VK_F9, "F9", L"F9", Keyboard, "f9");                                                                                                                \
    e(VK_F10, "F10", L"F10", Keyboard, "f10");                                                                                                            \
    e(VK_F11, "F11", L"F11", Keyboard, "f11");                                                                                                            \
    e(VK_F12, "F12", L"F12", Keyboard, "f12");                                                                                                            \
    e(VK_PAUSE, "Pause", L"Pause", Keyboard, "pause");                                                                                                    \
    e(VK_CAPITAL, "CapsLock", L"Caps Lock", Keyboard, "capslock", "caps");                                                                                \
    e(VK_NUMLOCK, "NumLock", L"Num Lock", Keyboard, "numlock");                                                                                           \
    e(VK_SCROLL, "ScrollLock", L"Scroll Lock", Keyboard, "scrolllock");                                                                                   \
    e(VK_LSHIFT, "LShift", L"Left Shift", Keyboard, "lshift");                                                                                            \
    e(VK_RSHIFT, "RShift", L"Right Shift", Keyboard, "rshift");                                                                                           \
    e(VK_SHIFT, "Shift", L"Shift (either)", Keyboard, "shift");                                                                                           \
    e(VK_LCONTROL, "LControl", L"Left Control", Keyboard, "lcontrol", "lctrl");                                                                           \
    e(VK_RCONTROL, "RControl", L"Right Control", Keyboard, "rcontrol", "rctrl");                                                                          \
    e(VK_CONTROL, "Control", L"Control (either)", Keyboard, "control", "ctrl");                                                                           \
    e(VK_LMENU, "LAlt", L"Left Alt", Keyboard, "lalt");                                                                                                   \
    e(VK_RMENU, "RAlt", L"Right Alt", Keyboard, "ralt");                                                                                                  \
    e(VK_MENU, "Alt", L"Alt (either)", Keyboard, "alt");                                                                                                  \
    /* Keyboard Char group */                                                                                                                             \
    e(VK_OEM_1, "Semicolon", L"Semicolon (;:)", KeyboardChar, "semicolon", "colon");                                                                      \
    e(VK_OEM_2, "Slash", L"Slash (/?)", KeyboardChar, "slash", "question");                                                                               \
    e(VK_OEM_7, "Quote", L"Quote ('\")", KeyboardChar, "quote", "quotes");                                                                                \
    e(VK_OEM_4, "OpenBracket", L"Open Bracket ([{)", KeyboardChar, "openbracket", "openbrace");                                                           \
    e(VK_OEM_6, "CloseBracket", L"Close Bracket (]})", KeyboardChar, "closebracket", "closebrace");                                                       \
    e(VK_OEM_PLUS, "Plus", L"Plus (+=)", KeyboardChar, "plus", "equals");                                                                                 \
    e(VK_OEM_MINUS, "Minus", L"Minus (-_)", KeyboardChar, "minus", "underscore");                                                                         \
    e(VK_OEM_PERIOD, "Period", L"Period (.>)", KeyboardChar, "period");                                                                                   \
    e(VK_OEM_COMMA, "Comma", L"Comma (,<)", KeyboardChar, "comma");                                                                                       \
    e(VK_OEM_5, "RBackslash", L"Backslash (\\|) [right]", KeyboardChar, "rbackslash", "rbar");                                                            \
    e(VK_OEM_3, "Backquote1", L"Backquote (\\~) [normal]", KeyboardChar, "backquote1", "tilde");                                                          \
    e_simple('0', "0", L"0", KeyboardChar, "0");                                                                                                          \
    e_simple('1', "1", L"1", KeyboardChar, "1");                                                                                                          \
    e_simple('2', "2", L"2", KeyboardChar, "2");                                                                                                          \
    e_simple('3', "3", L"3", KeyboardChar, "3");                                                                                                          \
    e_simple('4', "4", L"4", KeyboardChar, "4");                                                                                                          \
    e_simple('5', "5", L"5", KeyboardChar, "5");                                                                                                          \
    e_simple('6', "6", L"6", KeyboardChar, "6");                                                                                                          \
    e_simple('7', "7", L"7", KeyboardChar, "7");                                                                                                          \
    e_simple('8', "8", L"8", KeyboardChar, "8");                                                                                                          \
    e_simple('9', "9", L"9", KeyboardChar, "9");                                                                                                          \
    e_simple('A', "A", L"A", KeyboardChar, "a");                                                                                                          \
    e_simple('B', "B", L"B", KeyboardChar, "b");                                                                                                          \
    e_simple('C', "C", L"C", KeyboardChar, "c");                                                                                                          \
    e_simple('D', "D", L"D", KeyboardChar, "d");                                                                                                          \
    e_simple('E', "E", L"E", KeyboardChar, "e");                                                                                                          \
    e_simple('F', "F", L"F", KeyboardChar, "f");                                                                                                          \
    e_simple('G', "G", L"G", KeyboardChar, "g");                                                                                                          \
    e_simple('H', "H", L"H", KeyboardChar, "h");                                                                                                          \
    e_simple('I', "I", L"I", KeyboardChar, "i");                                                                                                          \
    e_simple('J', "J", L"J", KeyboardChar, "j");                                                                                                          \
    e_simple('K', "K", L"K", KeyboardChar, "k");                                                                                                          \
    e_simple('L', "L", L"L", KeyboardChar, "l");                                                                                                          \
    e_simple('M', "M", L"M", KeyboardChar, "m");                                                                                                          \
    e_simple('N', "N", L"N", KeyboardChar, "n");                                                                                                          \
    e_simple('O', "O", L"O", KeyboardChar, "o");                                                                                                          \
    e_simple('P', "P", L"P", KeyboardChar, "p");                                                                                                          \
    e_simple('Q', "Q", L"Q", KeyboardChar, "q");                                                                                                          \
    e_simple('R', "R", L"R", KeyboardChar, "r");                                                                                                          \
    e_simple('S', "S", L"S", KeyboardChar, "s");                                                                                                          \
    e_simple('T', "T", L"T", KeyboardChar, "t");                                                                                                          \
    e_simple('U', "U", L"U", KeyboardChar, "u");                                                                                                          \
    e_simple('V', "V", L"V", KeyboardChar, "v");                                                                                                          \
    e_simple('W', "W", L"W", KeyboardChar, "w");                                                                                                          \
    e_simple('X', "X", L"X", KeyboardChar, "x");                                                                                                          \
    e_simple('Y', "Y", L"Y", KeyboardChar, "y");                                                                                                          \
    e_simple('Z', "Z", L"Z", KeyboardChar, "z");                                                                                                          \
    /* Keyboard Numpad group */                                                                                                                           \
    e(VK_NUMPAD0, "Numpad0", L"Numpad 0", KeyboardNumpad, "numpad0");                                                                                     \
    e(VK_NUMPAD1, "Numpad1", L"Numpad 1", KeyboardNumpad, "numpad1");                                                                                     \
    e(VK_NUMPAD2, "Numpad2", L"Numpad 2", KeyboardNumpad, "numpad2");                                                                                     \
    e(VK_NUMPAD3, "Numpad3", L"Numpad 3", KeyboardNumpad, "numpad3");                                                                                     \
    e(VK_NUMPAD4, "Numpad4", L"Numpad 4", KeyboardNumpad, "numpad4");                                                                                     \
    e(VK_NUMPAD5, "Numpad5", L"Numpad 5", KeyboardNumpad, "numpad5");                                                                                     \
    e(VK_NUMPAD6, "Numpad6", L"Numpad 6", KeyboardNumpad, "numpad6");                                                                                     \
    e(VK_NUMPAD7, "Numpad7", L"Numpad 7", KeyboardNumpad, "numpad7");                                                                                     \
    e(VK_NUMPAD8, "Numpad8", L"Numpad 8", KeyboardNumpad, "numpad8");                                                                                     \
    e(VK_NUMPAD9, "Numpad9", L"Numpad 9", KeyboardNumpad, "numpad9");                                                                                     \
    e(VK_ADD, "NumpadPlus", L"Numpad Plus (+)", KeyboardNumpad, "numpadplus", "numpadadd");                                                               \
    e(VK_SUBTRACT, "NumpadMinus", L"Numpad Minus (-)", KeyboardNumpad, "numpadminus", "numpadsubtract");                                                  \
    e(VK_DECIMAL, "NumpadPeriod", L"Numpad Period (.)", KeyboardNumpad, "numpadperiod", "numpaddecimal");                                                 \
    e(VK_MULTIPLY, "NumpadAsterisk", L"Numpad Asterisk (*)", KeyboardNumpad, "numpadasterisk", "numpadmultiply");                                         \
    e(VK_DIVIDE, "NumpadSlash", L"Numpad Slash (/)", KeyboardNumpad, "numpadslash", "numpaddivide");                                                      \
    e(MY_VK_NUMPAD_RETURN, "NumpadEnter", L"Numpad Enter", KeyboardNumpad, "numpadenter", "numpadreturn");                                                \
    /* Keyboard Rare group */                                                                                                                             \
    e(VK_OEM_102, "LBackslash", L"Backslash (\\|) [left]", KeyboardRare, "lbackslash", "lbar");                                                           \
    e(MY_VK_OEM_5_102, "Backslash", L"Backslash (\\|) [either]", KeyboardRare, "backslash", "bar");                                                       \
    e(VK_OEM_8, "Backquote2", L"Backquote (\\) [rare]", KeyboardRare, "backqoute2", "not");                                                               \
    e(MY_VK_OEM_3_8, "Backquote", L"Backquote (\\) [either]", KeyboardRare, "backquote");                                                                 \
    e(VK_F13, "F13", L"F13", KeyboardRare, "f13");                                                                                                        \
    e(VK_F14, "F14", L"F14", KeyboardRare, "f14");                                                                                                        \
    e(VK_F15, "F15", L"F15", KeyboardRare, "f15");                                                                                                        \
    e(VK_F16, "F16", L"F16", KeyboardRare, "f16");                                                                                                        \
    e(VK_F17, "F17", L"F17", KeyboardRare, "f17");                                                                                                        \
    e(VK_F18, "F18", L"F18", KeyboardRare, "f18");                                                                                                        \
    e(VK_F19, "F19", L"F19", KeyboardRare, "f19");                                                                                                        \
    e(VK_F20, "F20", L"F20", KeyboardRare, "f20");                                                                                                        \
    e(VK_F21, "F21", L"F21", KeyboardRare, "f21");                                                                                                        \
    e(VK_F22, "F22", L"F22", KeyboardRare, "f22");                                                                                                        \
    e(VK_F23, "F23", L"F23", KeyboardRare, "f23");                                                                                                        \
    e(VK_F24, "F24", L"F24", KeyboardRare, "f24");                                                                                                        \
    e(VK_CLEAR, "Clear", L"Clear", KeyboardRare, "clear");                                                                                                \
    e(VK_KANA, "Kana", L"Kana", KeyboardRare, "kana");                                                                                                    \
    e(VK_KANJI, "Kanji", L"Kanji", KeyboardRare, "kanji");                                                                                                \
    e(VK_CONVERT, "Convert", L"Convert", KeyboardRare, "convert");                                                                                        \
    e(VK_NONCONVERT, "NoConvert", L"No Convert", KeyboardRare, "noconvert");                                                                              \
    e(VK_SLEEP, "Sleep", L"Sleep", KeyboardRare, "sleep");                                                                                                \
    e(VK_HELP, "Help", L"Help", KeyboardRare, "help");                                                                                                    \
    /* Keyboard Media group */                                                                                                                            \
    e(VK_BROWSER_BACK, "Browser.Back", L"Browser - Back", KeyboardMedia, "browserback");                                                                  \
    e(VK_BROWSER_FORWARD, "Browser.Forward", L"Browser - Forward", KeyboardMedia, "browserforward");                                                      \
    e(VK_BROWSER_HOME, "Browser.Home", L"Browser - Home", KeyboardMedia, "browserhome");                                                                  \
    e(VK_BROWSER_REFRESH, "Browser.Refresh", L"Browser - Refresh", KeyboardMedia, "browserrefresh");                                                      \
    e(VK_BROWSER_SEARCH, "Browser.Search", L"Browser - Search", KeyboardMedia, "browsersearch");                                                          \
    e(VK_BROWSER_STOP, "Browser.Stop", L"Browser - Stop", KeyboardMedia, "browserstop");                                                                  \
    e(VK_BROWSER_FAVORITES, "Browser.Favorites", L"Browser - Favorites", KeyboardMedia, "browserfavorites");                                              \
    e(VK_LAUNCH_MAIL, "Launch.Mail", L"Launch Mail", KeyboardMedia, "launchmail");                                                                        \
    e(VK_LAUNCH_MEDIA_SELECT, "Launch.Media", L"Launch Media", KeyboardMedia, "launchmedia");                                                             \
    e(VK_LAUNCH_APP1, "Launch.App1", L"Launch App #1", KeyboardMedia, "launchapp1");                                                                      \
    e(VK_LAUNCH_APP2, "Launch.App2", L"Launch App #2", KeyboardMedia, "launchapp2");                                                                      \
    e(VK_MEDIA_PLAY_PAUSE, "Media.Play", L"Media Play", KeyboardMedia, "mediaplay");                                                                      \
    e(VK_MEDIA_STOP, "Media.Stop", L"Media Stop", KeyboardMedia, "mediastop");                                                                            \
    e(VK_MEDIA_NEXT_TRACK, "Media.Next", L"Media Next", KeyboardMedia, "medianext");                                                                      \
    e(VK_MEDIA_PREV_TRACK, "Media.Prev", L"Media Prev", KeyboardMedia, "mediaprev");                                                                      \
    e(VK_VOLUME_UP, "Volume.Up", L"Volume Up", KeyboardMedia, "volumeup");                                                                                \
    e(VK_VOLUME_DOWN, "Volume.Down", L"Volume Down", KeyboardMedia, "volumedown");                                                                        \
    e(VK_VOLUME_MUTE, "Volume.Mute", L"Volume Mute", KeyboardMedia, "volumemute");                                                                        \
    /* Command group */                                                                                                                                   \
    e(MY_VK_NONE, "None", L"No Effect", Command, "none");                                                                                                 \
    e(MY_VK_RELOAD, "Reload", L"Reload Config", Command, "reload");                                                                                       \
    e(MY_VK_LOAD_CONFIG, "LoadConfig", L"Load Another Config", Command, "loadconfig");                                                                    \
    e(MY_VK_TOGGLE_DISABLE, "ToggleDisable", L"Toggle Disable Mappings", Command, "toggledisable");                                                       \
    e(MY_VK_SET_ACTIVE_USER, "SetActive", L"Set Active Gamepad", Command, "setactive");                                                                   \
    e(MY_VK_HOLD_ACTIVE_USER, "HoldActive", L"Set Active Gamepad while Held", Command, "holdactive");                                                     \
    e(MY_VK_TOGGLE_CONNECTED, "ToggleConnected", L"Toggle Gamepad Connected", Command, "toggleconnected");                                                \
    e(MY_VK_TOGGLE_ALWAYS, "ToggleAlways", L"Toggle Mapping in Background", Command, "togglealways");                                                     \
    e(MY_VK_TOGGLE_HIDE_CURSOR, "ToggleHideCursor", L"Toggle Hide Cursor", Command, "togglehidecursor");                                                  \
    e(MY_VK_TOGGLE_BOUND_CURSOR, "ToggleBoundCursor", L"Toggle Bound Cursor", Command, "toggleboundcursor");                                              \
    /* Command Debug group */                                                                                                                             \
    e(MY_VK_TOGGLE_SPARE_FOR_DEBUG, "ToggleSpareForDebug", L"(Debug) No Effect Toggle", CommandDebug, "togglesparefordebug");                             \
    //

#define ENUMERATE_KEYS_WITH_SIMPLE(e) ENUMERATE_KEYS_IMPL(e, e)
#define IGNORE_SIMPLE_KEYS(...)
#define ENUMERATE_KEYS_WITHOUT_SIMPLE(e) ENUMERATE_KEYS_IMPL(e, IGNORE_SIMPLE_KEYS)

enum class MyVkSource : byte {
    Unknown = 0,
    Keyboard,
    Mouse,
    Pad,
    Command = 0xff,
};

struct MyVkType {
    MyVkSource Source = MyVkSource::Unknown;
    bool OrPair : 1 = false;
    bool AndPair : 1 = false;
    bool OfUser : 1 = false;
    bool Modifier : 1 = false;
    bool Repeatable : 1 = false;
    bool Relative : 1 = false;

    bool IsOutputOnly() {
        return Source == MyVkSource::Command ||
               Source == MyVkSource::Pad || // for now(?)
               Modifier;
    }

    MyVkType() = default;
    MyVkType(MyVkSource source) : Source(source) {}
    MyVkType &SetOrPair() {
        OrPair = true;
        return *this;
    }
    MyVkType &SetAndPair() {
        AndPair = true;
        return *this;
    }
    MyVkType &SetOfUser() {
        OfUser = true;
        return *this;
    }
    MyVkType &SetModifier() {
        Modifier = true;
        return *this;
    }
    MyVkType &SetRepeatable() {
        Repeatable = true;
        return *this;
    }
    MyVkType &SetRelative() {
        Relative = true;
        return *this;
    }
};

bool IsVkMouseButton(int vk) // supports normal vks only, not my_vks
{
    switch (vk) {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    default:
        return false;
    }
}

bool IsVkPairButton(int vk) // supports normal vks only, not my_vks
{
    switch (vk) {
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
        return true;
    default:
        return false;
    }
}

MyVkType GetKeyType(key_t key) {
    if (key > 0 && key < MY_VK_LAST_REAL) {
        if (IsVkMouseButton(key)) {
            return MyVkType(MyVkSource::Mouse);
        } else if (IsVkPairButton(key)) {
            return MyVkType(MyVkSource::Keyboard).SetRepeatable().SetOrPair();
        } else {
            return MyVkType(MyVkSource::Keyboard).SetRepeatable();
        }
    }

    if (key >= MY_VK_FIRST_MOUSE_AXIS && key < MY_VK_LAST_MOUSE_AXIS) {
        return MyVkType(MyVkSource::Mouse).SetRelative();
    }
    if (key >= MY_VK_FIRST_MOUSE_AXIS_AND_PAIR && key < MY_VK_LAST_MOUSE_AXIS_AND_PAIR) {
        return MyVkType(MyVkSource::Mouse).SetRelative().SetAndPair();
    } else if (key >= MY_VK_FIRST_KEY_OR_PAIR && key < MY_VK_LAST_KEY_OR_PAIR) {
        return MyVkType(MyVkSource::Keyboard).SetRepeatable().SetOrPair();
    } else if (key >= MY_VK_FIRST_KEY_AND_PAIR && key < MY_VK_LAST_KEY_AND_PAIR) {
        return MyVkType(MyVkSource::Keyboard).SetRepeatable().SetAndPair();
    } else if (key >= MY_VK_FIRST_PAD && key < MY_VK_LAST_PAD) {
        return MyVkType(MyVkSource::Pad).SetOfUser();
    } else if (key >= MY_VK_FIRST_PAD_AND_PAIR && key < MY_VK_LAST_PAD_AND_PAIR) {
        return MyVkType(MyVkSource::Pad).SetOfUser().SetAndPair();
    } else if (key >= MY_VK_FIRST_PAD_MODIFIER && key < MY_VK_LAST_PAD_MODIFIER) {
        return MyVkType(MyVkSource::Pad).SetOfUser().SetModifier();
    } else if (key >= MY_VK_FIRST_PAD_MODIFIER_AND_PAIR && key < MY_VK_LAST_PAD_MODIFIER_AND_PAIR) {
        return MyVkType(MyVkSource::Pad).SetOfUser().SetModifier().SetAndPair();
    } else if (key >= MY_VK_FIRST_CMD && key < MY_VK_LAST_CMD) {
        return MyVkType(MyVkSource::Command);
    } else if (key >= MY_VK_FIRST_USER_CMD && key < MY_VK_LAST_USER_CMD) {
        return MyVkType(MyVkSource::Command).SetOfUser();
    }

    return MyVkType();
}

tuple<key_t, key_t> GetKeyPair(key_t key) {
    switch (key) {
    case VK_SHIFT:
        return make_tuple(VK_LSHIFT, VK_RSHIFT);
    case VK_CONTROL:
        return make_tuple(VK_LCONTROL, VK_RCONTROL);
    case VK_MENU:
        return make_tuple(VK_LMENU, VK_RMENU);
    case MY_VK_WIN:
        return make_tuple(VK_LWIN, VK_RWIN);
    case MY_VK_OEM_3_8:
        return make_tuple(VK_OEM_3, VK_OEM_8);
    case MY_VK_OEM_5_102:
        return make_tuple(VK_OEM_5, VK_OEM_102);
    case MY_VK_UP_LEFT:
        return make_tuple(VK_UP, VK_LEFT);
    case MY_VK_UP_RIGHT:
        return make_tuple(VK_UP, VK_RIGHT);
    case MY_VK_DOWN_LEFT:
        return make_tuple(VK_DOWN, VK_LEFT);
    case MY_VK_DOWN_RIGHT:
        return make_tuple(VK_DOWN, VK_RIGHT);
    case MY_VK_MOUSE_UP_LEFT:
        return make_tuple(MY_VK_MOUSE_UP, MY_VK_MOUSE_LEFT);
    case MY_VK_MOUSE_UP_RIGHT:
        return make_tuple(MY_VK_MOUSE_UP, MY_VK_MOUSE_RIGHT);
    case MY_VK_MOUSE_DOWN_LEFT:
        return make_tuple(MY_VK_MOUSE_DOWN, MY_VK_MOUSE_LEFT);
    case MY_VK_MOUSE_DOWN_RIGHT:
        return make_tuple(MY_VK_MOUSE_DOWN, MY_VK_MOUSE_RIGHT);
    case MY_VK_PAD_DPAD_UP_LEFT:
        return make_tuple(MY_VK_PAD_DPAD_UP, MY_VK_PAD_DPAD_LEFT);
    case MY_VK_PAD_DPAD_UP_RIGHT:
        return make_tuple(MY_VK_PAD_DPAD_UP, MY_VK_PAD_DPAD_RIGHT);
    case MY_VK_PAD_DPAD_DOWN_LEFT:
        return make_tuple(MY_VK_PAD_DPAD_DOWN, MY_VK_PAD_DPAD_LEFT);
    case MY_VK_PAD_DPAD_DOWN_RIGHT:
        return make_tuple(MY_VK_PAD_DPAD_DOWN, MY_VK_PAD_DPAD_RIGHT);
    case MY_VK_PAD_LTHUMB_UP_LEFT:
        return make_tuple(MY_VK_PAD_LTHUMB_UP, MY_VK_PAD_LTHUMB_LEFT);
    case MY_VK_PAD_LTHUMB_UP_RIGHT:
        return make_tuple(MY_VK_PAD_LTHUMB_UP, MY_VK_PAD_LTHUMB_RIGHT);
    case MY_VK_PAD_LTHUMB_DOWN_LEFT:
        return make_tuple(MY_VK_PAD_LTHUMB_DOWN, MY_VK_PAD_LTHUMB_LEFT);
    case MY_VK_PAD_LTHUMB_DOWN_RIGHT:
        return make_tuple(MY_VK_PAD_LTHUMB_DOWN, MY_VK_PAD_LTHUMB_RIGHT);
    case MY_VK_PAD_RTHUMB_UP_LEFT:
        return make_tuple(MY_VK_PAD_RTHUMB_UP, MY_VK_PAD_RTHUMB_LEFT);
    case MY_VK_PAD_RTHUMB_UP_RIGHT:
        return make_tuple(MY_VK_PAD_RTHUMB_UP, MY_VK_PAD_RTHUMB_RIGHT);
    case MY_VK_PAD_RTHUMB_DOWN_LEFT:
        return make_tuple(MY_VK_PAD_RTHUMB_DOWN, MY_VK_PAD_RTHUMB_LEFT);
    case MY_VK_PAD_RTHUMB_DOWN_RIGHT:
        return make_tuple(MY_VK_PAD_RTHUMB_DOWN, MY_VK_PAD_RTHUMB_RIGHT);
    case MY_VK_PAD_LTHUMB_MODIFIER:
        return make_tuple(MY_VK_PAD_LTHUMB_HORZ_MODIFIER, MY_VK_PAD_LTHUMB_VERT_MODIFIER);
    case MY_VK_PAD_RTHUMB_MODIFIER:
        return make_tuple(MY_VK_PAD_RTHUMB_HORZ_MODIFIER, MY_VK_PAD_RTHUMB_VERT_MODIFIER);
    case MY_VK_PAD_TRIGGER_MODIFIER:
        return make_tuple(MY_VK_PAD_LTRIGGER_MODIFIER, MY_VK_PAD_RTRIGGER_MODIFIER);
    default:
        return make_tuple(0, 0);
    }
}
