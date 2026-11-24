#pragma once
// macOS counterpart of src/Header.h. The Windows one declares every hooked Win32 API;
// the engine only needs the handful of declarations below.
#include "UtilsBase.h"
#include <Windows.h>

#define MYINPUT_HOOK_DLL_DECLSPEC __attribute__((visibility("default")))
#include "MyInputHook.h"

enum : ULONG {
    ExtraInfoOurInject = 0xe9341f00,
};

constexpr int InputThreadPriority = THREAD_PRIORITY_TIME_CRITICAL;

using AppCallback = void (*)(void *);
void PostAppCallback(AppCallback cb, void *data);
using VoidCallback = void (*)();
void PostAppCallback(VoidCallback cb);

void UpdateAll();
void UpdateCursor();
static void ImplAbortMappings();
static void ImplToggleForeground(bool allowUpdateAll = true);
