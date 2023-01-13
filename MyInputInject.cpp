#define NOMINMAX
#include <Windows.h>
#include "UtilsPath.h"
#include "UtilsStr.h"
#include "UtilsUiBase.h"

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

    LPWSTR userArg = nullptr;
    if (!(numArgs > 1)) {
        userArg = SelectFileForOpen(L"Executables\0*.EXE\0", L"Execute").Take();
        if (!userArg) {
            return 2;
        }

        numArgs = 1;
        args = &userArg;
        cmdLine = userArg;
    }

    Path ownDir = PathGetDirName(PathGetModulePath(nullptr));

    bool registered = (numArgs > 2 && wcscmp(args[1], L"-r") == 0);
    int firstArg = registered ? 2 : userArg ? 0
                                            : 1;

    bool redirecting = false;
    bool success = false;
    wchar_t *processCmdLine = SkipCommandLineArgs(cmdLine, firstArg);

    STARTUPINFOW si;
    GetStartupInfoW(&si);

    bool created = true;
    PROCESS_INFORMATION pi;
    // debug flags prevent debugger (which might be us again!) from interfering
    if (!CreateProcessW(nullptr, processCmdLine, nullptr, nullptr, registered,
                        CREATE_SUSPENDED | DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS,
                        nullptr, registered ? Path() : PathGetDirName(args[firstArg]),
                        &si, &pi)) {
        if (GetLastError() == ERROR_NOT_SUPPORTED) {
            // attempt to debug wrong bitness exe
            created = false;
        } else {
            Alert(L"Failed starting %ws. File not found?", args[firstArg]);
            return -1;
        }
    }

    BOOL meWow, themWow;
    if (!created ||
        !IsWow64Process(GetCurrentProcess(), &meWow) ||
        !IsWow64Process(pi.hProcess, &themWow) ||
        meWow != themWow) {
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
                success = true;
            } else {
                Alert(L"Failed redirecting to %ws. File not found?", otherExePath.Get());
            }
        } else {
            Alert(L"Process has wrong bitness and cannot find redirection target - cannot inject");
        }

        created = false;
    }

    if (created) {
        DebugActiveProcessStop(pi.dwProcessId); // was needed just at creation time

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

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

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
