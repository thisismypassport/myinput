#pragma once
#include "UtilsBase.h"

extern "C" {
// Log the string at 'data', of size 'size'.
// If 'size' is negative, string is assumed to be null-terminated
// Can be called from any thread.
DLLIMPORT void MyInputHook_Log(const char *data, intptr_t size);

// For internal testing purposes, will be removed or changed
DLLIMPORT int MyInputHook_InternalForTest();

// Can be called from any thread, queues 'cb' to be called in the dll thread
DLLIMPORT void MyInputHook_PostInDllThread(void (*cb)(void *data), void *data);

// Asserts that the calling thread is the dll thread or dllmain
DLLIMPORT void MyInputHook_AssertInDllThread();

// Must be called from dllmain
// Registers a custom key named 'name' (as it'd appear in the config file: <name> : <...> and/or <...> : <name>)
// 'cb' is called from dll thread whenever the key's state changes
// Returns an opaque identifier of the custom key
DLLIMPORT void *MyInputHook_RegisterCustomKey(const char *name, void (*cb)(bool down, double strength, unsigned long time, void *data), void *data);

// Must be called from dll thread
// Updates the state of a custom key, identifier by 'customKeyObj' (return value of MyInputHook_RegisterCustomKey)
DLLIMPORT void MyInputHook_UpdateCustomKey(void *customKeyObj, bool down, double strength, unsigned long time);

// Must be called from dllmain
// Registers a custom variable named 'name' (as it's appear in the config file: !<name> = <value>)
// 'cb' is called from dllmain or dll thread when the variable is encountered in the config, with its value as the parameter
// Returns an opaque identifier of the custom variable
DLLIMPORT void *MyInputHook_RegisterCustomVar(const char *name, void (*cb)(const char *value, void *data), void *data);

// Must be called from dllmain
// Registers a custom device named 'name' (as it'd appear in the config file: !Device<i> <name>)
// 'cb' is called from dllmain or dll thread whenever the device is added to or removed from a user
// Returns an opaque identifier of the custom device
DLLIMPORT void *MyInputHook_RegisterCustomDevice(const char *name, void (*cb)(int userIdx, bool added, void *data), void *data);

// Must be called from dllmain or dll thread
// Registers a callback called from dll thread whenever the virtual gamepad at index 'userIdx' is updated
// Returns an opaque identifier of the callback
DLLIMPORT void *MyInputHook_RegisterCallback(int userIdx, void (*cb)(int userIdx, void *data), void *data);

// Must be called from dllmain or dll thread
// Unregisters a callback registered via 'MyInputHook_RegisterCallback'
DLLIMPORT void MyInputHook_UnregisterCallback(int userIdx, void *cbObj);

// Must be called from dll thread
// Gets the state of key 'key' of gamepad index 'userIdx'
// Keys are from Keys.h, between MY_VK_FIRST/LAST_PAD
// if type is 'b', 'dest' is bool* (for buttons)
// if type is 'd', 'dest' is double* (for axes, triggers, and motion)
// if type is 'V' or 'A', 'dest' is double* (for motion - velocity and accelration respectively)
// Returns true if key recognized
DLLIMPORT bool MyInputHook_GetState(int userIdx, int key, char type, void *dest);

// Must be called from dll thread
// Sets the state of key 'key' of gamepad index 'userIdx'
// Keys are from Keys.h, between MY_VK_FIRST/LAST_PAD_OUTPUT
// if type is 'd', 'src' is const double* (for rumble)
// Returns true if key recognized
DLLIMPORT bool MyInputHook_SetState(int userIdx, int key, char type, const void *src);
}
