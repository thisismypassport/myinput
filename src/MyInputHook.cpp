#include "UtilsBase.h"
#include "WinHooks.h"
#include "WinInput.h"
#include "WinCursor.h"
#include "ImplFeedback.h"
#include "MiscApi.h"
#include "DeviceApi.h"
#include "RawInput.h"
#include "RawRegister.h"
#include "CfgMgr.h"
#include "NotifyApi.h"
#include "Log.h"
#include "WbemApi.h"
#include <Windows.h>

void UpdateAll() {
    WinHooksUpdateKeyboard();
    RawInputUpdateKeyboard();
    WinHooksUpdateMouse();
    RawInputUpdateMouse();
    UpdateHideCursor();
    ImplUpdateAsyncState();
}

ReliablePostThreadMessage GDllThreadMsgQueue;

void PostAppCallback(AppCallback cb, void *data) {
    GDllThreadMsgQueue.Post(WM_APP, (WPARAM)data, (LPARAM)cb);
}

void PostAppCallback(VoidCallback cb) {
    PostAppCallback([](void *data) { ((VoidCallback)data)(); }, cb);
}

static DWORD WINAPI DllThread(LPVOID param) {
    LOG << "Initializing thread..." << END;

    while (G.WaitDebugger && !IsDebuggerPresent()) {
        Sleep(1);
    }

    G.DllThread = GetCurrentThreadId(); // set both here and by CreateThread - needed by both

    RegisterGlobalNotify();
    WinHooksInitOnThread();
    RawInputInitDllWindow();
    UpdateAll();

    GDllThreadMsgQueue.Initialize();

    SetEvent(G.InitEvent);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_APP) {
            ((AppCallback)msg.lParam)((void *)msg.wParam);
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

extern "C" {
DLLEXPORT void MyInputHook_Log(const char *data, intptr_t size) {
    Log(LogLevel::Default, data, size >= 0 ? size : strlen(data));
}

DLLEXPORT int MyInputHook_InternalForTest() {
    UINT mask;
    return ImplGetUsers(&mask, DEVICE_NODE_TYPE_HID);
}

DLLEXPORT void MyInputHook_PostInDllThread(AppCallback cb, void *data) {
    PostAppCallback(cb, data);
}

DLLEXPORT void MyInputHook_AssertInDllThread() {
    CUSTOM_ASSERT_DLL_THREAD(ASSERT);
}

DLLEXPORT void *MyInputHook_RegisterCustomKey(const char *name, void (*cb)(bool, double, unsigned long, void *), void *data) {
    int index = ConfigRegisterCustomKey(name, cb ? [=](auto &info) { cb(info.Down, info.Strength, info.Time, data); } : function<void(const InputValue &)>());
    return (void *)(uintptr_t)(index + 1);
}

DLLEXPORT void MyInputHook_RegisterCustomVar(const char *name, void (*cb)(const char *value, void *data), void *data) {
    ConfigRegisterCustomVar(name, [=](const auto &str) { cb(str.c_str(), data); });
}

DLLEXPORT void *MyInputHook_RegisterCallback(int userIdx, void (*cb)(int, void *), void *data) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx);
    if (!user) {
        return nullptr;
    }

    return new ImplUserCb(user->Callbacks.Add([=](ImplUser *) {
        cb(userIdx, data);
        return true;
    }));
}

DLLEXPORT void MyInputHook_UnregisterCallback(int userIdx, void *cbObj) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx);
    ImplUserCb *cb = (ImplUserCb *)cbObj;
    if (user && cb) {
        user->Callbacks.Remove(*cb);
        delete cb;
    }
}

DLLEXPORT bool MyInputHook_GetState(int userIdx, int key, char type, void *dest) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx);
    return user ? ImplPadGetState(user, key, type, dest) : false;
}

DLLEXPORT bool MyInputHook_SetState(int userIdx, int key, char type, const void *src) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx);
    return user ? ImplPadSetState(user, key, type, src) : false;
}

DLLEXPORT void MyInputHook_UpdateCustomKey(void *customKeyObj, bool down, double strength, unsigned long time) {
    DBG_ASSERT_DLL_THREAD();
    int index = (int)(uintptr_t)customKeyObj - 1;
    ImplOnCustomKey(index, InputValue{down, strength, time});
}

DLLEXPORT void MyInputHookInternal_WaitInit() {
    WaitForSingleObject(G.InitEvent, INFINITE);
}
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // Prevent unload since we have a thread and various hooks
        HMODULE dummy;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)hInstance, &dummy);
        G.HInstance = hInstance;

        G.DllThread = GetCurrentThreadId(); // temporary for asserts, until dll thread spins up
        G.InitEvent = CreateEventW(nullptr, true, false, nullptr);

        Path rootDir = PathGetDirName(PathGetDirName(PathGetModulePath(G.HInstance)));
        Path baseName = PathGetBaseNameWithoutExt(PathGetModulePath(nullptr));

        LogInit(PathCombine(PathCombine(rootDir, L"Logs"), PathCombineExt(baseName, L"log")));
        LOG << "Initializing..." << END;

        RawInputPreregisterEarly();

        HookRawInput();
        HookDeviceApi();
        HookNotifyApi();
        HookCfgMgr();
        HookCom();
        HookWinHooks();
        HookWinInput();
        HookWinCursor();
        HookMisc();
        SetGlobalHooks();

        Path config = PathGetEnvVar(L"MYINPUT_HOOK_CONFIG");
        ConfigInit(PathCombine(rootDir, L"Configs"), PathCombineExt(config ? config : baseName, L"ini"));

        G.DllThread = 0;
        CloseHandle(CreateThread(nullptr, 0, DllThread, NULL, 0, &G.DllThread)); // do the rest on the thread, as it's not dllmain-safe
        LOG << "Initialized!" << END;
    } else if (ul_reason_for_call == DLL_THREAD_DETACH) {
        DWORD threadId = GetCurrentThreadId();
        WinHooksDetachThread(threadId);
        WinCursorDetachThread(threadId);
    }
    return TRUE;
}
