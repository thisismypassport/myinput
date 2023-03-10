#include "UtilsPath.h"
#include "UtilsStr.h"
#include "UtilsUiBase.h"
#include <Windows.h>

wchar_t *SkipCommandLineArgs(wchar_t *cmdLine, int count) {
    bool inQuotes = false;
    int numEscapes = 0;
    for (int i = 0; i < count; i++) {
        while (true) {
            wchar_t ch = *cmdLine;
            if (!ch) {
                return cmdLine;
            }

            cmdLine++;
            if (ch == '\\') {
                numEscapes++;
            } else {
                numEscapes = 0;
                if (ch == '"' && (numEscapes % 2) == 0) {
                    inQuotes = !inQuotes;
                } else if (!inQuotes && isspace(ch)) {
                    while (isspace(*cmdLine)) {
                        cmdLine++;
                    }
                    break;
                }
            }
        }
    }
    return cmdLine;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    LPWSTR cmdLine = GetCommandLineW();

    int numArgs;
    LPWSTR *args = CommandLineToArgvW(cmdLine, &numArgs);

    bool registered = false, byPid = false, byHandle = false;
    Path config, userCmdLine;
    int argI = 0;
    if (numArgs > 1) {
        for (argI = 1; argI < numArgs; argI++) {
            if (wcseq(args[argI], L"-r")) {
                registered = true;
            } else if (wcseq(args[argI], L"-p")) {
                byPid = true;
            } else if (wcseq(args[argI], L"-h")) {
                byHandle = true;
            } else if (wcseq(args[argI], L"-c") && argI + 1 < numArgs) {
                config = args[++argI];
            } else if (args[argI][0] != L'-') {
                break; // followed by args
            } else {
                Alert(L"Unrecognized option: %ws", args[argI]);
            }
        }
    } else {
        Path userArg = SelectFileForOpen(L"Executables\0*.EXE\0", L"Execute");
        if (!userArg) {
            return 2;
        }

        wstring ownArg0 = numArgs ? args[0] : L".";
        userCmdLine = (ownArg0 + L" \"" + wstring(userArg) + L"\"").c_str();
        cmdLine = userCmdLine;

        args = CommandLineToArgvW(cmdLine, &numArgs);
        argI = 1;
    }

    if (config) {
        SetEnvironmentVariableW(L"MYINPUT_HOOK_CONFIG", config);
    }

    STARTUPINFOW si = GetOutput(GetStartupInfoW);

    bool success = false;
    bool created = false;
    bool redirected = false;
    PROCESS_INFORMATION pi;
    if (byPid || byHandle) {
        // (As of this writing, injecting into an already running process is NOT properly supported)

        uint64_t value;
        if (!StrToValue(wstring(args[argI]), &value)) {
            Alert(L"Invalid number argument %ws.", args[argI]);
            return -1;
        }

        pi.dwThreadId = 0;
        pi.hThread = nullptr;

        if (byHandle) {
            pi.hProcess = (HANDLE)value;
            pi.dwProcessId = GetProcessId(pi.hProcess);

            if (pi.dwProcessId == 0) {
                Alert(L"Invalid process handle %lld.", value);
                return -1;
            }
        } else {
            pi.dwProcessId = (DWORD)value;
            pi.hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, pi.dwProcessId);

            if (pi.hProcess == nullptr) {
                Alert(L"Can't open process id %lld.", value);
                return -1;
            }
        }

        success = true;
    } else {
        wchar_t *processCmdLine = SkipCommandLineArgs(cmdLine, argI);

        // debug flags prevent debugger (which might be us again!) from interfering
        if (CreateProcessW(nullptr, processCmdLine, nullptr, nullptr, registered,
                           CREATE_SUSPENDED | DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS,
                           nullptr, registered ? Path() : PathGetDirName(args[argI]),
                           &si, &pi)) {
            DebugActiveProcessStop(pi.dwProcessId); // was needed just at creation time
            created = true;
            success = true;
        } else if (GetLastError() == ERROR_NOT_SUPPORTED) {
            // attempt to debug wrong bitness exe in some cases
        } else if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
            Path ownPath = PathGetModulePath(nullptr);

            SHELLEXECUTEINFOW exec = {};
            exec.cbSize = sizeof(exec);
            exec.fMask = SEE_MASK_NOCLOSEPROCESS;
            exec.lpVerb = L"runas";
            exec.lpFile = ownPath;
            exec.lpParameters = SkipCommandLineArgs(cmdLine, 1);
            exec.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&exec) && exec.hProcess) {
                redirected = true;
                pi.hProcess = exec.hProcess;
            } else {
                Alert(L"Failed elevating %ws", args[argI]);
                return -1;
            }
        } else {
            Alert(L"Failed starting %ws. File not found?", args[argI]);
            return -1;
        }
    }

    Path ownDir = PathGetDirName(PathGetModulePath(nullptr));

    BOOL meWow, themWow;
    if (!redirected &&
        (!success ||
         !IsWow64Process(GetCurrentProcess(), &meWow) ||
         !IsWow64Process(pi.hProcess, &themWow) ||
         meWow != themWow)) {
        success = false;

        wstring dirName(PathGetBaseName(ownDir));
        bool replaced = false;
        if (StrContains(dirName, L"Win32")) {
            replaced = StrReplaceFirst(dirName, L"Win32", L"x64");
        } else if (StrContains(dirName, L"x64")) {
            replaced = StrReplaceFirst(dirName, L"x64", L"Win32");
        }

        if (replaced) {
            Path otherExePath = PathCombine(PathCombine(PathGetDirName(ownDir), dirName.c_str()), L"myinput_inject.exe");

            PROCESS_INFORMATION redirectPi;
            if (CreateProcessW(otherExePath, cmdLine, nullptr, nullptr, true,
                               CREATE_SUSPENDED, nullptr, nullptr, &si, &redirectPi)) {
                if (created) {
                    TerminateProcess(pi.hProcess, -1);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }

                pi = redirectPi; // wait for redirect instead;
                redirected = true;
                success = true;
            } else {
                Alert(L"Failed redirecting to %ws. File not found?", otherExePath.Get());
            }
        } else {
            Alert(L"Process has wrong bitness and cannot find myinput_inject of other bitness - cannot inject");
        }
    }

    if (success && !redirected) {
        void *loadLibAddr = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");

        const wchar_t *dllName = L"myinput_hook.dll";
        Path dllPath = PathCombine(ownDir, dllName);

        size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

        LPVOID dllAddr = VirtualAllocEx(pi.hProcess, nullptr, dllPathSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (dllAddr) {
            if (WriteProcessMemory(pi.hProcess, dllAddr, dllPath, dllPathSize, nullptr)) {
                HANDLE hRemote = CreateRemoteThread(pi.hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)loadLibAddr,
                                                    dllAddr, 0, nullptr);
                if (hRemote) {
                    WaitForSingleObject(hRemote, INFINITE);
                    CloseHandle(hRemote);
                    success = true;
                } else {
                    Alert(L"Couldn't inject thread in process (%d)", GetLastError());
                }
            } else {
                Alert(L"Couldn't write memory to process (%d)", GetLastError());
            }

            VirtualFreeEx(pi.hProcess, dllAddr, 0, MEM_RELEASE);
        } else {
            Alert(L"Couldn't allocate memory in process (%d)", GetLastError());
        }
    }

    if (pi.hThread) {
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
    }

    DWORD exitCode = -1;
    if (registered) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        exitCode = success ? 0 : 1;
    }

    CloseHandle(pi.hProcess);
    return exitCode;
}
