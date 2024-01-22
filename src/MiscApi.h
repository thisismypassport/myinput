#pragma once
#include "Hook.h"
#include "State.h"
#include "Inject.h"

typedef BOOL(WINAPI *CreateProcessInternalW_Type)(
    HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
    BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);

CreateProcessInternalW_Type CreateProcessInternalW_Real;

bool InjectDirect(HANDLE process) {
    Path dllPath = PathGetModulePath(G.HInstance);
    return DoInject(process, dllPath);
}

void InjectIndirect(HANDLE process) {
    Path ownDir = PathGetDirName(PathGetModulePath(G.HInstance));

    wstring dirName(PathGetBaseName(ownDir));
    bool replaced = ReplaceWithOtherBitness(&dirName);

    HANDLE inhHandle;
    if (!replaced) {
        LOG_ERR << "Cannot inject into new process - can't find myinput_inject of other bitness" << END;
    } else if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &inhHandle,
                                0, TRUE, DUPLICATE_SAME_ACCESS)) {
        LOG_ERR << "Can't duplicate process handle " << GetLastError() << END;
    } else {
        Path otherExePath = PathCombine(PathCombine(PathGetDirName(ownDir), dirName.c_str()), L"myinput_inject.exe");
        Path cmdArgs = (L"myinput_inject.exe -h " + StrFromValue<wchar_t>((ULONG_PTR)inhHandle)).c_str();

        STARTUPINFOW injSi;
        ZeroMemory(&injSi, sizeof(injSi));
        injSi.cb = sizeof(injSi);

        PROCESS_INFORMATION injPi;
        if (!CreateProcessInternalW_Real(nullptr, otherExePath, cmdArgs, nullptr, nullptr, TRUE,
                                         0, nullptr, nullptr, &injSi, &injPi, nullptr)) {
            LOG_ERR << "Can't create inject process " << GetLastError() << END;
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

    bool inject = true;
repeat:

    // DEBUG needed to avoid reinject, if registered
    DWORD actualFlags = flags | CREATE_SUSPENDED | DEBUG_PROCESS;
    if (!(flags & DEBUG_PROCESS)) {
        actualFlags |= DEBUG_ONLY_THIS_PROCESS;
    }

    BOOL success = CreateProcessInternalW_Real(token, appName, cmdLine, processSec, threadSec, inherit,
                                               actualFlags, env, curDir, startupInfo, procInfo, newToken);

    if (!success && !(flags & DEBUG_PROCESS) && GetLastError() == ERROR_NOT_SUPPORTED) {
        // can't debug differently-WOW processes in some cases (any workarounds?)

        actualFlags &= ~(DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS);
        success = CreateProcessInternalW_Real(token, appName, cmdLine, processSec, threadSec, inherit,
                                              actualFlags, env, curDir, startupInfo, procInfo, newToken);

        if (success) {
            LOG << "Couldn't launch following process with debug flag, it may not work if it was registered..." << END;
        }
    }

    if (success) {
        if ((actualFlags & DEBUG_PROCESS) && !(flags & DEBUG_PROCESS)) {
            DebugActiveProcessStop(procInfo->dwProcessId);
        }

        if (inject) {
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
        }

        if (WaitForSingleObject(procInfo->hProcess, 0) != WAIT_TIMEOUT) // process died? (e.g. due to process restrictions)
        {
            LOG << "Injection crashed process " << (cmdLine ? cmdLine : appName) << " (retrying without it)" << END;
            CloseHandle(procInfo->hProcess);
            CloseHandle(procInfo->hThread);

            // (note - we still want to do the DEBUG_PROCESS thing to avoid injection due to registration)
            inject = false;
            goto repeat;
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
        ADD_GLOBAL_HOOK(CreateProcessInternalW);
    } else {
        LOG << "Couldn't find CreateProcessInternalW - won't be injecting children" << END;
    }
}
