#include "UtilsBase.h"
#include "WinHooks.h"
#include "WinInput.h"
#include "WinFeatures.h"
#include "MiscApi.h"
#include "DeviceApi.h"
#include "RawInput.h"
#include "RawRegister.h"
#include "CfgMgr.h"
#include "NotifyApi.h"
#include <Windows.h>

void UpdateAll() {
    WinHooksUpdateKeyboard();
    RawInputUpdateKeyboard();
    WinHooksUpdateMouse();
    RawInputUpdateMouse();
    UpdateHideCursor();
}

void PostAppCallback(AppCallback cb, void *data) {
    PostThreadMessageW(G.DllThread, WM_APP, (WPARAM)data, (LPARAM)cb);
}

static DWORD WINAPI DllThread(LPVOID param) {
    LOG << "Initializing thread..." << END;

    while (G.WaitDebugger && !IsDebuggerPresent()) {
        Sleep(1);
    }

    G.DllThread = GetCurrentThreadId();

    RegisterGlobalNotify();
    WinHooksInitOnThread();
    RawInputInitDllWindow();
    UpdateAll();

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
DLLEXPORT void MyInputHook_Log(const char *data, size_t size) {
    Log(data, size >= 0 ? size : strlen(data));
}

DLLEXPORT int MyInputHook_GetUserCount() {
    UINT mask;
    return ImplGetUsers(&mask);
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInstance);

        // Prevent unload since we have a thread and various hooks
        HMODULE dummy;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)hInstance, &dummy);
        G.HInstance = hInstance;

        Path rootDir = PathGetDirName(PathGetDirName(PathGetModulePath(G.HInstance)));
        Path baseName = PathGetBaseNameWithoutExt(PathGetModulePath(nullptr));

        LogInit(PathCombine(PathCombine(rootDir, L"Logs"), PathCombineExt(baseName, L"log")));
        LOG << "Initializing..." << END;

        RawInputPreregisterEarly();

        ConfigLoad(PathCombine(rootDir, L"Configs"), PathCombineExt(baseName, L"ini"));

        HookRawInput();
        HookDeviceApi();
        HookNotifyApi();
        HookCfgMgr();
        HookWinHooks();
        HookWinInput();
        HookMisc();
        SetGlobalHooks();

        for (auto &hook : G.ExtraHooks) {
            LoadExtraHook(hook);
        }

        CloseHandle(CreateThread(nullptr, 0, DllThread, NULL, 0, nullptr)); // do the rest on the thread, as it's not dllmain-safe
        LOG << "Initialized!" << END;
    }
    return TRUE;
}
}
