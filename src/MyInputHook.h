#pragma once
#include <cstdint>

#ifndef MYINPUT_HOOK_DLL_DECLSPEC
#define MYINPUT_HOOK_DLL_DECLSPEC __declspec(dllimport)
#endif

struct MyInputHook_KeyInfo {
    int Flags;          // MyInputHook_KeyFlags
    bool Down;          // always set
    unsigned long Time; // always set
    double Strength;    // always set
    const char *Data;   // only set if .._Has_Data
};

enum MyInputHook_KeyFlags {
    MyInputHook_KeyFlag_Has_Data = 0x1,
};

enum MyInputHook_InStateType {
    MyInputHook_InState_Basic_Type = 1,
    MyInputHook_InState_Motion_Type = 101,
};

struct MyInputHook_InState_Basic {
    uint8_t A, B, X, Y;
    uint8_t DL, DR, DU, DD;       // DPad
    uint8_t LB, RB, LT, RT, L, R; // bumper/trigger/stick
    uint8_t Start, Back, Guide, Extra;
    uint8_t Reserved[14];

    double LX, LY, RX, RY; // left/right sticks (positive = up/right)
    double LTStr, RTStr;   // LT/RT strength
    double Reserved2[2];
};

struct MyInputHook_InState_Motion {
    struct Axis {
        double Pos, Speed, Accel;
    };
    Axis X, Y, Z;    // displacement, in metres (positive = up/right/near)
    Axis RX, RY, RZ; // rotation, in radians (positive = down/right/ccw)
};

enum MyInputHook_OutStateType {
    MyInputHook_OutState_Rumble_Type = 1,
};

struct MyInputHook_OutState_Rumble {
    double Low, High;
};

extern "C" {
// Log the string at 'data', of size 'size'.
// If 'size' is negative, string is assumed to be null-terminated
// Can be called from any thread.
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_Log(const char *data, intptr_t size, char level);

// For internal purposes, set a callback to be called when logging.
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_SetLogCallback(void (*cb)(const char *str, size_t size, char level, void *data), void *data);

// Can be called from any thread, queues 'cb' to be called in the dll thread
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_PostInDllThread(void (*cb)(void *data), void *data);

// Asserts that the calling thread is the dll thread or dllmain
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_AssertInDllThread();

// Must be called from dllmain of plugin
// Registers a custom key named 'name' (as it'd appear in the config file: <plugin>/<name> : <...> and/or <...> : <plugin>/<name>)
// 'cb' is called from dll thread whenever the key's state changes
// Returns an opaque identifier of the custom key
MYINPUT_HOOK_DLL_DECLSPEC void *MyInputHook_RegisterCustomKey(const char *name, void (*cb)(const MyInputHook_KeyInfo *info, void *data), void *data);

// Must be called from dll thread
// Updates the state of a custom key, identified by 'customKeyObj' (return value of MyInputHook_RegisterCustomKey)
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_UpdateCustomKey(void *customKeyObj, const MyInputHook_KeyInfo *info);

// Must be called from dllmain of plugin
// Registers a custom variable named 'name' (as it's appear in the config file: !<plugin>/<name> = <value>)
// 'cb' is called from dllmain or dll thread when the variable is encountered in the config, with its value as the parameter
// Returns an opaque identifier of the custom variable
MYINPUT_HOOK_DLL_DECLSPEC void *MyInputHook_RegisterCustomVar(const char *name, void (*cb)(const char *value, void *data), void *data);

// Must be called from dllmain of plugin
// Registers a custom device named 'name' (as it'd appear in the config file: !Device<i> <plugin>/<name>)
// 'cb' is called from dllmain or dll thread whenever the device is added to or removed from a user
// Returns an opaque identifier of the custom device
MYINPUT_HOOK_DLL_DECLSPEC void *MyInputHook_RegisterCustomDevice(const char *name, void (*cb)(int userIdx, bool added, void *data), void *data);

// Must be called from dllmain or dll thread
// Registers a callback called from dll thread whenever the virtual gamepad at index 'userIdx' is updated
// Returns an opaque identifier of the callback
MYINPUT_HOOK_DLL_DECLSPEC void *MyInputHook_RegisterCallback(int userIdx, void (*cb)(int userIdx, void *data), void *data);

// Must be called from dllmain or dll thread
// Unregisters a callback registered via 'MyInputHook_RegisterCallback'
// Returns nullptr
MYINPUT_HOOK_DLL_DECLSPEC void *MyInputHook_UnregisterCallback(int userIdx, void *cbObj);

// Must be called from dllmain or dll thread
// Registers a callback called from dll thread before & after the config is loaded.
// Returns an opaque identifier of the callback
MYINPUT_HOOK_DLL_DECLSPEC void *MyInputHook_RegisterConfigCallback(void (*cb)(bool after, void *data), void *data);

// Must be called from dll thread
// Gets the state of gamepad index 'userIdx'
// type is one of MyInputHook_InStateType
// state is corresponding MyInputHook_InState_*, and size is its sizeof
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_GetInState(int userIdx, int type, void *state, int size);

// Must be called from dll thread
// Gets the state of gamepad index 'userIdx'
// type is one of MyInputHook_OutStateType
// state is corresponding MyInputHook_OutState_*, and size is its sizeof
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_SetOutState(int userIdx, int type, const void *state, int size);

// Must be called directly from a MyInputHook_PostInDllThread callback (not just from any callback in the dll thread)
// Loads the given config.
// NULL will reload the current config
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_LoadConfig(const wchar_t *name);

// Wait until initialization finishes (if loading manually)
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_WaitInit();

// Disallow injecting into children, overriding any and all configs
MYINPUT_HOOK_DLL_DECLSPEC void MyInputHook_DisallowInjectChildren(bool disallow);

// For internal testing purposes, will be removed or changed
MYINPUT_HOOK_DLL_DECLSPEC int MyInputHook_InternalGetNumVirtual(char type);
MYINPUT_HOOK_DLL_DECLSPEC bool MyInputHook_InternalIsVirtual(const wchar_t *path);
}
