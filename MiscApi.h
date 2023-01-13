#pragma once
#include "Hook.h"
#include "State.h"

typedef BOOL(WINAPI *CreateProcessInternalW_Type)(
    HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
    BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);

typedef HMODULE(WINAPI *LoadLibraryExW_Type)(LPCWSTR, HANDLE, DWORD);

CreateProcessInternalW_Type CreateProcessInternalW_Real;
LoadLibraryExW_Type LoadLibraryExW_Real;

bool InjectDirect(HANDLE process) {
    void *loadLibAddr = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");

    Path dllPath = PathGetModulePath(G.HInstance);
    size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

    LPVOID dllAddr = VirtualAllocEx(process, NULL, dllPathSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (dllAddr == NULL) {
        LOG << "Couldn't allocate memory in process: " << GetLastError() << END;
        return false;
    }

    auto dllAddrDtor = Destructor([process, dllAddr]() {
        VirtualFreeEx(process, dllAddr, 0, MEM_RELEASE);
    });

    if (!WriteProcessMemory(process, dllAddr, dllPath, dllPathSize, NULL)) {
        LOG << "Couldn't write memory to process: " << GetLastError() << END;
        return false;
    }

    HANDLE hRemote = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibAddr, dllAddr, NULL, NULL);
    if (hRemote == NULL) {
        LOG << "Couldn't inject thread in process: " << GetLastError() << END;
        return false;
    }

    WaitForSingleObject(hRemote, INFINITE);
    CloseHandle(hRemote);
    return true;
}

BOOL WINAPI CreateProcessInternalW_Hook(
    HANDLE token, LPCWSTR appName, LPWSTR cmdLine, LPSECURITY_ATTRIBUTES processSec, LPSECURITY_ATTRIBUTES threadSec,
    BOOL inherit, DWORD flags, LPVOID env, LPCWSTR curDir, LPSTARTUPINFOW startupInfo, LPPROCESS_INFORMATION procInfo, PHANDLE newToken) {
    // DEBUG needed to avoid reinject, if registered
    DWORD actualFlags = flags | CREATE_SUSPENDED | DEBUG_PROCESS;
    if (!(flags & DEBUG_PROCESS)) {
        actualFlags |= DEBUG_ONLY_THIS_PROCESS;
    }

    BOOL success = CreateProcessInternalW_Real(token, appName, cmdLine, processSec, threadSec, inherit,
                                               actualFlags, env, curDir, startupInfo, procInfo, newToken);

    if (success) {
        LOG << "Injecting into new process " << (cmdLine ? cmdLine : appName) << END;

        if (!(flags & DEBUG_PROCESS)) {
            DebugActiveProcessStop(procInfo->dwProcessId);
        }

        BOOL meWow, themWow;
        if (IsWow64Process(GetCurrentProcess(), &meWow) &&
            IsWow64Process(procInfo->hProcess, &themWow) &&
            meWow == themWow) {
            InjectDirect(procInfo->hProcess);
        } else {
            // TODO: use myinput_inject 32bit?
            LOG << "Cannot inject into new process - wrong bitness (TODO)" << END;
        }

        if (!(flags & CREATE_SUSPENDED)) {
            ResumeThread(procInfo->hThread);
        }
    }

    return success;
}

void LoadExtraHook(const Path &hook) {
    if (G.Debug) {
        LOG << "Loading extra hook: " << hook << END;
    }

    if (!LoadLibraryExW_Real(hook, nullptr, 0)) {
        LOG << "Failed loading extra hook: " << hook << " due to: " << GetLastError() << END;
    }
}

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    HMODULE module = LoadLibraryExW_Real(lpLibFileName, hFile, dwFlags);

    if (module) {
        // trigger extra hooks
        for (auto &delayed : G.ExtraDelayedHooks) {
            if (!delayed.Loaded && lpLibFileName && _wcsistr(lpLibFileName, delayed.WaitDll)) {
                Path baseName = PathGetBaseNameWithoutExt(lpLibFileName);
                if (_wcsicmp(baseName, delayed.WaitDll) == 0) {
                    for (auto &hook : delayed.Hooks) {
                        LoadExtraHook(hook);
                    }

                    delayed.Loaded = true;
                }
            }
        }
    }

    return module;
}

void HookMisc() {
    LoadLibraryExW_Real = (LoadLibraryExW_Type)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "LoadLibraryExW");
    if (!LoadLibraryExW_Real) {
        LoadLibraryExW_Real = LoadLibraryExW;
    }

    AddGlobalHook(&LoadLibraryExW_Real, LoadLibraryExW_Hook);

    if (G.InjectChildren) {
        CreateProcessInternalW_Real = (CreateProcessInternalW_Type)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "CreateProcessInternalW");
        if (!CreateProcessInternalW_Real) {
            CreateProcessInternalW_Real = (CreateProcessInternalW_Type)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateProcessInternalW");
        }

        if (CreateProcessInternalW_Real) {
            AddGlobalHook(&CreateProcessInternalW_Real, CreateProcessInternalW_Hook);
        } else {
            LOG << "Couldn't find CreateProcessInternalW - won't be injecting children" << END;
        }
    }
}
