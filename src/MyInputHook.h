#pragma once
#include "UtilsBase.h"

extern "C" {
DLLIMPORT void MyInputHook_Log(const char *data, size_t size);
DLLIMPORT int MyInputHook_GetUserCount(); // will be removed or changed
DLLIMPORT void MyInputHook_PostInDllThread(void (*cb)(void *data), void *data);
DLLIMPORT void MyInputHook_AssertInDllThread();

// must be called from dll main:
DLLIMPORT void *MyInputHook_RegisterCustomKey(const char *name, void (*cb)(bool down, double strength, unsigned long time, void *data), void *data);
DLLIMPORT void MyInputHook_RegisterCustomVar(const char *name, void (*cb)(const char *value, void *data), void *data);

// must be called from dll thread:
DLLIMPORT void *MyInputHook_RegisterCallback(int userIdx, void (*cb)(int userIdx, void *data), void *data);
DLLIMPORT void MyInputHook_UnregisterCallback(int userIdx, void *cbObj);
DLLIMPORT bool MyInputHook_GetState(int userIdx, int key, char type, void *dest);
DLLIMPORT bool MyInputHook_SetState(int userIdx, int key, char type, const void *src);
DLLIMPORT void MyInputHook_UpdateCustomKey(void *customKeyObj, bool down, double strength, unsigned long time);
}
