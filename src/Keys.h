#pragma once
#include "UtilsBase.h"
#include <Windows.h>

// (Special custom vk definitions. should avoid rearranging)
enum {
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

    MY_VK_FIRST_KEY_PAIR = 0x2100,
    MY_VK_WIN,
    MY_VK_OEM_3_8,
    MY_VK_OEM_5_102,
    MY_VK_LAST_KEY_PAIR,

    MY_VK_FIRST_PAD_MODIFIER = 0xb500,
    MY_VK_PAD_LTHUMB_MODIFIER,
    MY_VK_PAD_LTHUMB_HORZ_MODIFIER,
    MY_VK_PAD_LTHUMB_VERT_MODIFIER,
    MY_VK_PAD_RTHUMB_MODIFIER,
    MY_VK_PAD_RTHUMB_HORZ_MODIFIER,
    MY_VK_PAD_RTHUMB_VERT_MODIFIER,
    MY_VK_PAD_TRIGGER_MODIFIER,
    MY_VK_PAD_LTRIGGER_MODIFIER,
    MY_VK_PAD_RTRIGGER_MODIFIER,
    MY_VK_PAD_LTHUMB_UP_ROTATOR,
    MY_VK_PAD_LTHUMB_DOWN_ROTATOR,
    MY_VK_PAD_LTHUMB_RIGHT_ROTATOR,
    MY_VK_PAD_LTHUMB_LEFT_ROTATOR,
    MY_VK_PAD_RTHUMB_UP_ROTATOR,
    MY_VK_PAD_RTHUMB_DOWN_ROTATOR,
    MY_VK_PAD_RTHUMB_RIGHT_ROTATOR,
    MY_VK_PAD_RTHUMB_LEFT_ROTATOR,
    MY_VK_PAD_LTHUMB_RESETTER,
    MY_VK_PAD_RTHUMB_RESETTER,
    MY_VK_LAST_PAD_MODIFIER,

    MY_VK_FIRST_CMD = 0xc000,
    MY_VK_NONE,
    MY_VK_RELOAD,
    MY_VK_TOGGLE_DISABLE,
    MY_VK_TOGGLE_HIDE_CURSOR,
    MY_VK_TOGGLE_ALWAYS,
    MY_VK_CUSTOM,
    MY_VK_TOGGLE_SPARE_FOR_DEBUG,
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
};

#define ENUMERATE_LIT_VKS(e) \
    e('A', "a");             \
    e('B', "b");             \
    e('C', "c");             \
    e('D', "d");             \
    e('E', "e");             \
    e('F', "f");             \
    e('G', "g");             \
    e('H', "h");             \
    e('I', "i");             \
    e('J', "j");             \
    e('K', "k");             \
    e('L', "l");             \
    e('M', "m");             \
    e('N', "n");             \
    e('O', "o");             \
    e('P', "p");             \
    e('Q', "q");             \
    e('R', "r");             \
    e('S', "s");             \
    e('T', "t");             \
    e('U', "u");             \
    e('V', "v");             \
    e('W', "w");             \
    e('X', "x");             \
    e('Y', "y");             \
    e('Z', "z");             \
    e('0', "0");             \
    e('1', "1");             \
    e('2', "2");             \
    e('3', "3");             \
    e('4', "4");             \
    e('5', "5");             \
    e('6', "6");             \
    e('7', "7");             \
    e('8', "8");             \
    e('9', "9");             \
    //

#define ENUMERATE_MISC_VKS(e)                              \
    e(VK_LBUTTON, "lbutton");                              \
    e(VK_RBUTTON, "rbutton");                              \
    e(VK_MBUTTON, "mbutton");                              \
    e(VK_XBUTTON1, "xbutton1");                            \
    e(VK_XBUTTON2, "xbutton2");                            \
    e(MY_VK_WHEEL_UP, "wheelup");                          \
    e(MY_VK_WHEEL_DOWN, "wheeldown");                      \
    e(MY_VK_WHEEL_LEFT, "wheelleft");                      \
    e(MY_VK_WHEEL_RIGHT, "wheelright");                    \
    e(MY_VK_MOUSE_UP, "mouseup");                          \
    e(MY_VK_MOUSE_DOWN, "mousedown");                      \
    e(MY_VK_MOUSE_LEFT, "mouseleft");                      \
    e(MY_VK_MOUSE_RIGHT, "mouseright");                    \
    e(VK_BACK, "backspace");                               \
    e(VK_TAB, "tab");                                      \
    e(VK_CLEAR, "clear");                                  \
    e(VK_RETURN, "enter", "return");                       \
    e(VK_PAUSE, "pause");                                  \
    e(VK_CAPITAL, "capslock", "caps");                     \
    e(VK_ESCAPE, "escape");                                \
    e(VK_SPACE, "space");                                  \
    e(VK_PRIOR, "pageup", "pgup");                         \
    e(VK_NEXT, "pagedown", "pgdn");                        \
    e(VK_END, "end");                                      \
    e(VK_HOME, "home");                                    \
    e(VK_LEFT, "left");                                    \
    e(VK_RIGHT, "right");                                  \
    e(VK_UP, "up");                                        \
    e(VK_DOWN, "down");                                    \
    e(VK_SNAPSHOT, "printscreen");                         \
    e(VK_INSERT, "insert", "ins");                         \
    e(VK_DELETE, "delete", "del");                         \
    e(VK_LWIN, "lwindows", "lwin");                        \
    e(VK_RWIN, "rwindows", "rwin");                        \
    e(MY_VK_WIN, "windows", "win");                        \
    e(VK_APPS, "contextmenu");                             \
    e(VK_NUMPAD0, "numpad0");                              \
    e(VK_NUMPAD1, "numpad1");                              \
    e(VK_NUMPAD2, "numpad2");                              \
    e(VK_NUMPAD3, "numpad3");                              \
    e(VK_NUMPAD4, "numpad4");                              \
    e(VK_NUMPAD5, "numpad5");                              \
    e(VK_NUMPAD6, "numpad6");                              \
    e(VK_NUMPAD7, "numpad7");                              \
    e(VK_NUMPAD8, "numpad8");                              \
    e(VK_NUMPAD9, "numpad9");                              \
    e(VK_ADD, "numpadplus", "numpadadd");                  \
    e(VK_SUBTRACT, "numpadminus", "numpadsubtract");       \
    e(VK_DECIMAL, "numpadperiod", "numpaddecimal");        \
    e(VK_MULTIPLY, "numpadasterisk", "numpadmultiply");    \
    e(VK_DIVIDE, "numpadslash", "numpaddivide");           \
    e(MY_VK_NUMPAD_RETURN, "numpadenter", "numpadreturn"); \
    e(VK_F1, "f1");                                        \
    e(VK_F2, "f2");                                        \
    e(VK_F3, "f3");                                        \
    e(VK_F4, "f4");                                        \
    e(VK_F5, "f5");                                        \
    e(VK_F6, "f6");                                        \
    e(VK_F7, "f7");                                        \
    e(VK_F8, "f8");                                        \
    e(VK_F9, "f9");                                        \
    e(VK_F10, "f10");                                      \
    e(VK_F11, "f11");                                      \
    e(VK_F12, "f12");                                      \
    e(VK_NUMLOCK, "numlock");                              \
    e(VK_SCROLL, "scrolllock");                            \
    e(VK_LSHIFT, "lshift");                                \
    e(VK_RSHIFT, "rshift");                                \
    e(VK_SHIFT, "shift");                                  \
    e(VK_LCONTROL, "lcontrol", "lctrl");                   \
    e(VK_RCONTROL, "rcontrol", "rctrl");                   \
    e(VK_CONTROL, "control", "ctrl");                      \
    e(VK_LMENU, "lalt");                                   \
    e(VK_RMENU, "ralt");                                   \
    e(VK_MENU, "alt");                                     \
    e(VK_OEM_1, "semicolon", "colon");                     \
    e(VK_OEM_2, "slash", "question");                      \
    e(VK_OEM_3, "backquote1", "tilde");                    \
    e(VK_OEM_4, "openbracket", "openbrace");               \
    e(VK_OEM_5, "rbackslash", "rbar");                     \
    e(VK_OEM_6, "closebracket", "closebrace");             \
    e(VK_OEM_7, "quote", "quotes");                        \
    e(VK_OEM_8, "backqoute2", "not");                      \
    e(MY_VK_OEM_3_8, "backquote");                         \
    e(VK_OEM_102, "lbackslash", "lbar");                   \
    e(MY_VK_OEM_5_102, "backslash", "bar");                \
    e(VK_OEM_PLUS, "plus", "equals");                      \
    e(VK_OEM_MINUS, "minus", "underscore");                \
    e(VK_OEM_PERIOD, "period");                            \
    e(VK_OEM_COMMA, "comma");                              \
    e(VK_F13, "f13");                                      \
    e(VK_F14, "f14");                                      \
    e(VK_F15, "f15");                                      \
    e(VK_F16, "f16");                                      \
    e(VK_F17, "f17");                                      \
    e(VK_F18, "f18");                                      \
    e(VK_F19, "f19");                                      \
    e(VK_F20, "f20");                                      \
    e(VK_F21, "f21");                                      \
    e(VK_F22, "f22");                                      \
    e(VK_F23, "f23");                                      \
    e(VK_F24, "f24");                                      \
    e(VK_KANA, "kana");                                    \
    e(VK_KANJI, "kanji");                                  \
    e(VK_CONVERT, "convert");                              \
    e(VK_NONCONVERT, "noconvert");                         \
    e(VK_SLEEP, "sleep");                                  \
    e(VK_HELP, "help");                                    \
    e(VK_BROWSER_BACK, "browserback");                     \
    e(VK_BROWSER_FORWARD, "browserforward");               \
    e(VK_BROWSER_HOME, "browserhome");                     \
    e(VK_BROWSER_REFRESH, "browserrefresh");               \
    e(VK_BROWSER_SEARCH, "browsersearch");                 \
    e(VK_BROWSER_STOP, "browserstop");                     \
    e(VK_BROWSER_FAVORITES, "browserfavorites");           \
    e(VK_LAUNCH_MAIL, "launchmail");                       \
    e(VK_LAUNCH_MEDIA_SELECT, "launchmedia");              \
    e(VK_LAUNCH_APP1, "launchapp1");                       \
    e(VK_LAUNCH_APP2, "launchapp2");                       \
    e(VK_MEDIA_PLAY_PAUSE, "mediaplay");                   \
    e(VK_MEDIA_STOP, "mediastop");                         \
    e(VK_MEDIA_NEXT_TRACK, "medianext");                   \
    e(VK_MEDIA_PREV_TRACK, "mediaprev");                   \
    e(VK_VOLUME_UP, "volumeup");                           \
    e(VK_VOLUME_DOWN, "volumedown");                       \
    e(VK_VOLUME_MUTE, "volumemute");                       \
    //

#define ENUMERATE_PAD_VKS(e)                          \
    e(MY_VK_PAD_A, "%a");                             \
    e(MY_VK_PAD_B, "%b");                             \
    e(MY_VK_PAD_X, "%x");                             \
    e(MY_VK_PAD_Y, "%y");                             \
    e(MY_VK_PAD_START, "%start");                     \
    e(MY_VK_PAD_BACK, "%back");                       \
    e(MY_VK_PAD_GUIDE, "%guide");                     \
    e(MY_VK_PAD_EXTRA, "%extra");                     \
    e(MY_VK_PAD_DPAD_LEFT, "%dleft");                 \
    e(MY_VK_PAD_DPAD_RIGHT, "%dright");               \
    e(MY_VK_PAD_DPAD_UP, "%dup");                     \
    e(MY_VK_PAD_DPAD_DOWN, "%ddown");                 \
    e(MY_VK_PAD_LTHUMB_LEFT, "%lleft");               \
    e(MY_VK_PAD_LTHUMB_RIGHT, "%lright");             \
    e(MY_VK_PAD_LTHUMB_UP, "%lup");                   \
    e(MY_VK_PAD_LTHUMB_DOWN, "%ldown");               \
    e(MY_VK_PAD_RTHUMB_LEFT, "%rleft");               \
    e(MY_VK_PAD_RTHUMB_RIGHT, "%rright");             \
    e(MY_VK_PAD_RTHUMB_UP, "%rup");                   \
    e(MY_VK_PAD_RTHUMB_DOWN, "%rdown");               \
    e(MY_VK_PAD_LSHOULDER, "%lb");                    \
    e(MY_VK_PAD_RSHOULDER, "%rb");                    \
    e(MY_VK_PAD_LTRIGGER, "%lt");                     \
    e(MY_VK_PAD_RTRIGGER, "%rt");                     \
    e(MY_VK_PAD_LTHUMB_PRESS, "%l");                  \
    e(MY_VK_PAD_RTHUMB_PRESS, "%r");                  \
    e(MY_VK_PAD_MOTION_UP, "%motionup");              \
    e(MY_VK_PAD_MOTION_DOWN, "%motiondown");          \
    e(MY_VK_PAD_MOTION_LEFT, "%motionleft");          \
    e(MY_VK_PAD_MOTION_RIGHT, "%motionright");        \
    e(MY_VK_PAD_MOTION_NEAR, "%motionnear");          \
    e(MY_VK_PAD_MOTION_FAR, "%motionfar");            \
    e(MY_VK_PAD_MOTION_ROT_UP, "%motionrotup");       \
    e(MY_VK_PAD_MOTION_ROT_DOWN, "%motionrotdown");   \
    e(MY_VK_PAD_MOTION_ROT_LEFT, "%motionrotleft");   \
    e(MY_VK_PAD_MOTION_ROT_RIGHT, "%motionrotright"); \
    e(MY_VK_PAD_MOTION_ROT_CCW, "%motionrotccw");     \
    e(MY_VK_PAD_MOTION_ROT_CW, "%motionrotcw");       \
    e(MY_VK_PAD_LTHUMB_MODIFIER, "%modl");            \
    e(MY_VK_PAD_LTHUMB_HORZ_MODIFIER, "%modlx");      \
    e(MY_VK_PAD_LTHUMB_VERT_MODIFIER, "%modly");      \
    e(MY_VK_PAD_RTHUMB_MODIFIER, "%modr");            \
    e(MY_VK_PAD_RTHUMB_HORZ_MODIFIER, "%modrx");      \
    e(MY_VK_PAD_RTHUMB_VERT_MODIFIER, "%modry");      \
    e(MY_VK_PAD_TRIGGER_MODIFIER, "%modt");           \
    e(MY_VK_PAD_LTRIGGER_MODIFIER, "%modlt");         \
    e(MY_VK_PAD_RTRIGGER_MODIFIER, "%modrt");         \
    e(MY_VK_PAD_LTHUMB_UP_ROTATOR, "%rotlup");        \
    e(MY_VK_PAD_LTHUMB_DOWN_ROTATOR, "%rotldown");    \
    e(MY_VK_PAD_LTHUMB_RIGHT_ROTATOR, "%rotlright");  \
    e(MY_VK_PAD_LTHUMB_LEFT_ROTATOR, "%rotlleft");    \
    e(MY_VK_PAD_RTHUMB_UP_ROTATOR, "%rotrup");        \
    e(MY_VK_PAD_RTHUMB_DOWN_ROTATOR, "%rotrdown");    \
    e(MY_VK_PAD_RTHUMB_RIGHT_ROTATOR, "%rotrright");  \
    e(MY_VK_PAD_RTHUMB_LEFT_ROTATOR, "%rotrleft");    \
    //

#define ENUMERATE_CMD_VKS(e)                                \
    e(MY_VK_NONE, "none");                                  \
    e(MY_VK_RELOAD, "reload");                              \
    e(MY_VK_TOGGLE_DISABLE, "toggledisable");               \
    e(MY_VK_TOGGLE_HIDE_CURSOR, "togglehidecursor");        \
    e(MY_VK_TOGGLE_ALWAYS, "togglealways");                 \
    e(MY_VK_TOGGLE_SPARE_FOR_DEBUG, "togglesparefordebug"); \
    e(MY_VK_SET_ACTIVE_USER, "setactive");                  \
    e(MY_VK_HOLD_ACTIVE_USER, "holdactive");                \
    e(MY_VK_TOGGLE_CONNECTED, "toggleconnected");           \
    //

enum class MyVkSource : byte {
    Unknown = 0,
    Keyboard,
    Mouse,
    Pad,
    Command = 0xff,
};

struct MyVkType {
    MyVkSource Source = MyVkSource::Unknown;
    bool Pair : 1 = false;
    bool OfUser : 1 = false;
    bool Modifier : 1 = false;
    bool Repeatable : 1 = false;
    bool Relative : 1 = false;

    MyVkType() = default;
    MyVkType(MyVkSource source) : Source(source) {}
    MyVkType &SetPair() {
        Pair = true;
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

MyVkType GetKeyType(int key) {
    if (key > 0 && key < MY_VK_LAST_REAL) {
        if (IsVkMouseButton(key)) {
            return MyVkType(MyVkSource::Mouse);
        } else if (IsVkPairButton(key)) {
            return MyVkType(MyVkSource::Keyboard).SetRepeatable().SetPair();
        } else {
            return MyVkType(MyVkSource::Keyboard).SetRepeatable();
        }
    }

    if (key >= MY_VK_FIRST_MOUSE_AXIS && key < MY_VK_LAST_MOUSE_AXIS) {
        return MyVkType(MyVkSource::Mouse).SetRelative();
    } else if (key >= MY_VK_FIRST_KEY_PAIR && key < MY_VK_LAST_KEY_PAIR) {
        return MyVkType(MyVkSource::Keyboard).SetRepeatable().SetPair();
    } else if (key >= MY_VK_FIRST_PAD && key < MY_VK_LAST_PAD) {
        return MyVkType(MyVkSource::Pad).SetOfUser();
    } else if (key >= MY_VK_FIRST_PAD_MODIFIER && key < MY_VK_LAST_PAD_MODIFIER) {
        return MyVkType(MyVkSource::Pad).SetOfUser().SetModifier();
    } else if (key >= MY_VK_FIRST_CMD && key < MY_VK_LAST_CMD) {
        return MyVkType(MyVkSource::Command);
    } else if (key >= MY_VK_FIRST_USER_CMD && key < MY_VK_LAST_USER_CMD) {
        return MyVkType(MyVkSource::Command).SetOfUser();
    }

    return MyVkType();
}

tuple<int, int> GetKeyPair(int key) {
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
    default:
        return make_tuple(0, 0);
    }
}
