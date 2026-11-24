#pragma once
// The slice of <Windows.h> that MyInput's mapping engine (src/Impl*.h, src/Config*.h, ...) relies on.
// Types and constants keep their Win32 values, since the engine uses virtual-key codes as its key ids.
// Functions are implemented in mac/hook/Engine.cpp, or inline below when they are pure.
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <strings.h>

typedef unsigned char byte;
typedef unsigned char BYTE;
typedef unsigned char UCHAR;
typedef uint16_t WORD;
typedef uint16_t USHORT;
typedef int16_t SHORT;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uintptr_t UINT_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef void *HANDLE;
typedef void *HWND;
typedef void *HHOOK;
typedef void *HINSTANCE;
typedef void *HMODULE;

#define CALLBACK
#define WINAPI

typedef void (*TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *);

#define WM_TIMER 0x0113
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_TIME_CRITICAL 15

enum {
    VK_LBUTTON = 0x01,
    VK_RBUTTON = 0x02,
    VK_CANCEL = 0x03,
    VK_MBUTTON = 0x04,
    VK_XBUTTON1 = 0x05,
    VK_XBUTTON2 = 0x06,
    VK_BACK = 0x08,
    VK_TAB = 0x09,
    VK_CLEAR = 0x0C,
    VK_RETURN = 0x0D,
    VK_SHIFT = 0x10,
    VK_CONTROL = 0x11,
    VK_MENU = 0x12,
    VK_PAUSE = 0x13,
    VK_CAPITAL = 0x14,
    VK_KANA = 0x15,
    VK_KANJI = 0x19,
    VK_ESCAPE = 0x1B,
    VK_CONVERT = 0x1C,
    VK_NONCONVERT = 0x1D,
    VK_SPACE = 0x20,
    VK_PRIOR = 0x21,
    VK_NEXT = 0x22,
    VK_END = 0x23,
    VK_HOME = 0x24,
    VK_LEFT = 0x25,
    VK_UP = 0x26,
    VK_RIGHT = 0x27,
    VK_DOWN = 0x28,
    VK_SNAPSHOT = 0x2C,
    VK_INSERT = 0x2D,
    VK_DELETE = 0x2E,
    VK_HELP = 0x2F,
    VK_LWIN = 0x5B,
    VK_RWIN = 0x5C,
    VK_APPS = 0x5D,
    VK_SLEEP = 0x5F,
    VK_NUMPAD0 = 0x60,
    VK_NUMPAD1,
    VK_NUMPAD2,
    VK_NUMPAD3,
    VK_NUMPAD4,
    VK_NUMPAD5,
    VK_NUMPAD6,
    VK_NUMPAD7,
    VK_NUMPAD8,
    VK_NUMPAD9,
    VK_MULTIPLY = 0x6A,
    VK_ADD = 0x6B,
    VK_SEPARATOR = 0x6C,
    VK_SUBTRACT = 0x6D,
    VK_DECIMAL = 0x6E,
    VK_DIVIDE = 0x6F,
    VK_F1 = 0x70,
    VK_F2,
    VK_F3,
    VK_F4,
    VK_F5,
    VK_F6,
    VK_F7,
    VK_F8,
    VK_F9,
    VK_F10,
    VK_F11,
    VK_F12,
    VK_F13,
    VK_F14,
    VK_F15,
    VK_F16,
    VK_F17,
    VK_F18,
    VK_F19,
    VK_F20,
    VK_F21,
    VK_F22,
    VK_F23,
    VK_F24,
    VK_NUMLOCK = 0x90,
    VK_SCROLL = 0x91,
    VK_LSHIFT = 0xA0,
    VK_RSHIFT = 0xA1,
    VK_LCONTROL = 0xA2,
    VK_RCONTROL = 0xA3,
    VK_LMENU = 0xA4,
    VK_RMENU = 0xA5,
    VK_BROWSER_BACK = 0xA6,
    VK_BROWSER_FORWARD = 0xA7,
    VK_BROWSER_REFRESH = 0xA8,
    VK_BROWSER_STOP = 0xA9,
    VK_BROWSER_SEARCH = 0xAA,
    VK_BROWSER_FAVORITES = 0xAB,
    VK_BROWSER_HOME = 0xAC,
    VK_VOLUME_MUTE = 0xAD,
    VK_VOLUME_DOWN = 0xAE,
    VK_VOLUME_UP = 0xAF,
    VK_MEDIA_NEXT_TRACK = 0xB0,
    VK_MEDIA_PREV_TRACK = 0xB1,
    VK_MEDIA_STOP = 0xB2,
    VK_MEDIA_PLAY_PAUSE = 0xB3,
    VK_LAUNCH_MAIL = 0xB4,
    VK_LAUNCH_MEDIA_SELECT = 0xB5,
    VK_LAUNCH_APP1 = 0xB6,
    VK_LAUNCH_APP2 = 0xB7,
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
    VK_OEM_8 = 0xDF,
    VK_OEM_102 = 0xE2,
};

#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK_EX 3

#define INPUT_MOUSE 0
#define INPUT_KEYBOARD 1

#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_LEFTDOWN 0x0002
#define MOUSEEVENTF_LEFTUP 0x0004
#define MOUSEEVENTF_RIGHTDOWN 0x0008
#define MOUSEEVENTF_RIGHTUP 0x0010
#define MOUSEEVENTF_MIDDLEDOWN 0x0020
#define MOUSEEVENTF_MIDDLEUP 0x0040
#define MOUSEEVENTF_XDOWN 0x0080
#define MOUSEEVENTF_XUP 0x0100
#define MOUSEEVENTF_WHEEL 0x0800
#define MOUSEEVENTF_HWHEEL 0x1000

#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002

#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#define WHEEL_DELTA 120

#define SPI_GETKEYBOARDSPEED 0x000A
#define SPI_GETKEYBOARDDELAY 0x0016

struct MOUSEINPUT {
    LONG dx, dy;
    DWORD mouseData, dwFlags, time;
    uintptr_t dwExtraInfo;
};

struct KEYBDINPUT {
    WORD wVk, wScan;
    DWORD dwFlags, time;
    uintptr_t dwExtraInfo;
};

struct INPUT {
    DWORD type;
    union {
        MOUSEINPUT mi;
        KEYBDINPUT ki;
    };
};

DWORD GetTickCount();
SHORT GetAsyncKeyState(int vk);
SHORT GetAsyncKeyState_Real(int vk);
SHORT GetKeyState(int vk);
UINT MapVirtualKeyW(UINT code, UINT mapType);
bool SystemParametersInfoW(UINT action, UINT param, void *data, UINT flags);
UINT SendInput_Real(UINT count, INPUT *inputs, int size);
bool LoadLibraryExW(const wchar_t *path, HANDLE file, DWORD flags);
DWORD GetLastError();

inline unsigned char BitScanForward(DWORD *index, DWORD mask) {
    if (!mask) {
        return 0;
    }
    *index = (DWORD)__builtin_ctz(mask);
    return 1;
}

inline int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
inline int _strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }
inline int _wcsicmp(const wchar_t *a, const wchar_t *b) { return wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n) { return wcsncasecmp(a, b, n); }

// UTF-8 <-> wchar_t (UTF-32 on macOS), with the Win32 calling convention the engine expects:
// a size of -1 means "NUL-terminated, and count the terminator"; a zero destination size asks for the needed size.
#define CP_UTF8 65001

inline int MultiByteToWideChar(UINT, DWORD, const char *src, int srcSize, wchar_t *dest, int destSize) {
    size_t srcLen = srcSize < 0 ? strlen(src) + 1 : (size_t)srcSize;
    int count = 0;
    for (size_t i = 0; i < srcLen;) {
        unsigned char ch = (unsigned char)src[i];
        uint32_t cp;
        int extra;
        if (ch < 0x80) {
            cp = ch, extra = 0;
        } else if ((ch & 0xE0) == 0xC0) {
            cp = ch & 0x1F, extra = 1;
        } else if ((ch & 0xF0) == 0xE0) {
            cp = ch & 0x0F, extra = 2;
        } else if ((ch & 0xF8) == 0xF0) {
            cp = ch & 0x07, extra = 3;
        } else {
            cp = 0xFFFD, extra = 0;
        }
        i++;
        for (; extra > 0 && i < srcLen && ((unsigned char)src[i] & 0xC0) == 0x80; extra--, i++) {
            cp = (cp << 6) | ((unsigned char)src[i] & 0x3F);
        }
        if (extra > 0) {
            cp = 0xFFFD;
        }
        if (destSize > 0) {
            if (count >= destSize) {
                return 0;
            }
            dest[count] = (wchar_t)cp;
        }
        count++;
    }
    return count;
}

inline int WideCharToMultiByte(UINT, DWORD, const wchar_t *src, int srcSize, char *dest, int destSize, const char *, void *) {
    size_t srcLen = srcSize < 0 ? wcslen(src) + 1 : (size_t)srcSize;
    int count = 0;
    for (size_t i = 0; i < srcLen; i++) {
        uint32_t cp = (uint32_t)src[i];
        char buf[4];
        int len;
        if (cp < 0x80) {
            buf[0] = (char)cp, len = 1;
        } else if (cp < 0x800) {
            buf[0] = (char)(0xC0 | (cp >> 6)), buf[1] = (char)(0x80 | (cp & 0x3F)), len = 2;
        } else if (cp < 0x10000) {
            buf[0] = (char)(0xE0 | (cp >> 12)), buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (cp & 0x3F)), len = 3;
        } else {
            buf[0] = (char)(0xF0 | ((cp >> 18) & 0x07)), buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F)), buf[3] = (char)(0x80 | (cp & 0x3F)), len = 4;
        }
        if (destSize > 0) {
            if (count + len > destSize) {
                return 0;
            }
            memcpy(dest + count, buf, len);
        }
        count += len;
    }
    return count;
}
