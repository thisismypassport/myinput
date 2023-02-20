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

void InjectIndirect(HANDLE process) {
    Path ownDir = PathGetDirName(PathGetModulePath(G.HInstance));

    wstring dirName(PathGetBaseName(ownDir));
    bool replaced = false;
    if (StrContains(dirName, L"Win32")) {
        replaced = StrReplaceFirst(dirName, L"Win32", L"x64");
    } else if (StrContains(dirName, L"x64")) {
        replaced = StrReplaceFirst(dirName, L"x64", L"Win32");
    }

    HANDLE inhHandle;
    if (!replaced) {
        LOG << "Cannot inject into new process - can't find myinput_inject of other bitness" << END;
    } else if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &inhHandle,
                                0, TRUE, DUPLICATE_SAME_ACCESS)) {
        LOG << "Can't duplicate process handle " << GetLastError() << END;
    } else {
        Path otherExePath = PathCombine(PathCombine(PathGetDirName(ownDir), dirName.c_str()), L"myinput_inject.exe");
        Path cmdArgs = (L"myinput_inject.exe -h " + StrFromValue<wchar_t>((ULONG_PTR)inhHandle)).c_str();

        STARTUPINFOW injSi;
        ZeroMemory(&injSi, sizeof(injSi));
        injSi.cb = sizeof(injSi);

        PROCESS_INFORMATION injPi;
        if (!CreateProcessInternalW_Real(nullptr, otherExePath, cmdArgs, nullptr, nullptr, TRUE,
                                         0, nullptr, nullptr, &injSi, &injPi, nullptr)) {
            LOG << "Can't create inject process " << GetLastError() << END;
        } else {
            WaitForSingleObject(injPi.hThread, INFINITE);
            CloseHandle(injPi.hThread);
            CloseHandle(injPi.hProcess);
        }

        CloseHandle(inhHandle);
    }
}

BOOL WINAPI CreateProcessInternalW_Hook(
    HANDLE token, LPCWSTR appName, LPWSTR cmdLine, LPSECURITY_ATTRIBUTES processSec, LPSECURITY_ATTRIBUTES threadSec,
    BOOL inherit, DWORD flags, LPVOID env, LPCWSTR curDir, LPSTARTUPINFOW startupInfo, LPPROCESS_INFORMATION procInfo, PHANDLE newToken) {
    if (!G.InjectChildren) {
        return CreateProcessInternalW_Real(token, appName, cmdLine, processSec, threadSec, inherit,
                                           flags, env, curDir, startupInfo, procInfo, newToken);
    }

    // DEBUG needed to avoid reinject, if registered
    DWORD actualFlags = flags | CREATE_SUSPENDED | DEBUG_PROCESS;
    if (!(flags & DEBUG_PROCESS)) {
        actualFlags |= DEBUG_ONLY_THIS_PROCESS;
    }

    BOOL success = CreateProcessInternalW_Real(token, appName, cmdLine, processSec, threadSec, inherit,
                                               actualFlags, env, curDir, startupInfo, procInfo, newToken);

    if (!success && !(flags & DEBUG_PROCESS) && GetLastError() == ERROR_NOT_SUPPORTED) {
        // can't debug differently-WOW processes in some cases

        actualFlags &= ~(DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS);
        success = CreateProcessInternalW_Real(token, appName, cmdLine, processSec, threadSec, inherit,
                                              actualFlags, env, curDir, startupInfo, procInfo, newToken);
    }

    if (success) {
        if ((actualFlags & DEBUG_PROCESS) && !(flags & DEBUG_PROCESS)) {
            DebugActiveProcessStop(procInfo->dwProcessId);
        }

        BOOL meWow, themWow;
        if (IsWow64Process(GetCurrentProcess(), &meWow) &&
            IsWow64Process(procInfo->hProcess, &themWow) &&
            meWow == themWow) {
            LOG << "Injecting into new process of same bitness " << (cmdLine ? cmdLine : appName) << END;
            InjectDirect(procInfo->hProcess);
        } else {
            LOG << "Injecting into new process of different bitness " << (cmdLine ? cmdLine : appName) << END;
            InjectIndirect(procInfo->hProcess);
        }

        if (!(flags & CREATE_SUSPENDED)) {
            ResumeThread(procInfo->hThread);
        }
    }

    return success;
}

void HookMisc() {
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
