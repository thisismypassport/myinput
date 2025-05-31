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

    LOG << "Initialization finished" << END;

    SetThreadPriority(GetCurrentThread(), InputThreadPriority);

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

void MyInputHook_Log(const char *data, intptr_t size, char level) {
    Log((LogLevel)level, data, size >= 0 ? size : strlen(data));
}

void MyInputHook_SetLogCallback(void (*cb)(const char *str, size_t size, char level, void *data), void *data) {
    GLogCb = LogCbType{cb, data};
}

int MyInputHook_InternalForTest() {
    UINT mask;
    return ImplGetUsers(&mask, DEVICE_NODE_TYPE_HID);
}

void MyInputHook_PostInDllThread(AppCallback cb, void *data) {
    PostAppCallback(cb, data);
}

void MyInputHook_AssertInDllThread() {
    CUSTOM_ASSERT_DLL_THREAD(ASSERT);
}

void *MyInputHook_RegisterCustomKey(const char *name, void (*cb)(const MyInputHook_KeyInfo *, void *), void *data) {
    auto keyCb = [=](const InputValue &value, const ImplMapping *mapping) {
        MyInputHook_KeyInfo info = {};
        info.Down = value.Down;
        info.Strength = value.Strength;
        info.Time = value.Time;
        if (mapping->Data) {
            info.Flags |= MyInputHook_KeyFlag_Has_Data;
            info.Data = mapping->Data->c_str();
        }
        cb(&info, data);
    };

    DBG_ASSERT_DLL_THREAD();
    int index = ConfigRegisterCustomKey(name, cb ? keyCb : function<void(const InputValue &, const ImplMapping *)>{});
    return (void *)(uintptr_t)(index + 1);
}

void MyInputHook_UpdateCustomKey(void *customKeyObj, const MyInputHook_KeyInfo *info) {
    DBG_ASSERT_DLL_THREAD();
    int index = (int)(uintptr_t)customKeyObj - 1;
    ImplOnCustomKey(index, InputValue{info->Down, info->Strength, info->Time});
}

void *MyInputHook_RegisterCustomVar(const char *name, void (*cb)(const char *, void *), void *data) {
    DBG_ASSERT_DLL_THREAD();
    int index = ConfigRegisterCustomVar(name, [=](const auto &str) { cb(str.c_str(), data); });
    return (void *)(uintptr_t)(index + 1);
}

void *MyInputHook_RegisterCustomDevice(const char *name, void (*cb)(int, bool, void *), void *data) {
    DBG_ASSERT_DLL_THREAD();
    int index = ConfigRegisterCustomDevice(name, [=](int idx, bool add) { cb(idx, add, data); });
    return (void *)(uintptr_t)(index + 1);
}

void *MyInputHook_RegisterCallback(int userIdx, void (*cb)(int, void *), void *data) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx, true);
    if (!user) {
        return nullptr;
    }

    return new ImplUserCb(user->Callbacks.Add([=](ImplUser *) {
        cb(userIdx, data);
        return true;
    }));
}

void *MyInputHook_UnregisterCallback(int userIdx, void *cbObj) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx, true);
    ImplUserCb *cb = (ImplUserCb *)cbObj;
    if (user && cb) {
        user->Callbacks.Remove(*cb);
        delete cb;
    }
    return nullptr;
}

void MyInputHook_GetInState(int userIdx, int type, void *state, int size) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx);
    if (!user || !ImplPadGetInState(user, type, state, size)) {
        memset(state, 0, size);
    }
}

void MyInputHook_SetOutState(int userIdx, int type, const void *state, int size) {
    DBG_ASSERT_DLL_THREAD();
    auto user = ImplGetUser(userIdx);
    if (user) {
        ImplPadSetOutState(user, type, state, size);
    }
}

void MyInputHook_LoadConfig(const wchar_t *name) {
    DBG_ASSERT_DLL_THREAD();
    if (name) {
        ConfigLoad(name);
    } else {
        ConfigReload();
    }
}

void MyInputHook_WaitInit() {
    WaitForSingleObject(G.InitEvent, INFINITE);
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
