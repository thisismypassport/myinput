#include "UtilsPath.h"
#include "UtilsStr.h"
#include "UtilsUiBase.h"
#include "Inject.h"
#include "Link.h"
#include <Windows.h>

static bool gIsProgrammatic;
DEFINE_ALERT_ON_ERROR_COND(!gIsProgrammatic)

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
            if (tstreq(args[argI], L"-r")) {
                registered = true;
            } else if (tstreq(args[argI], L"-p")) {
                byPid = true;
            } else if (tstreq(args[argI], L"-h")) {
                gIsProgrammatic = byHandle = true;
            } else if (tstreq(args[argI], L"-c") && argI + 1 < numArgs) {
                config = args[++argI];
            } else if (args[argI][0] != L'-') {
                break; // followed by args
            } else {
                Alert(L"Unrecognized option: %ws", args[argI]);
            }
        }

        if (argI >= numArgs) {
            Alert(L"Nothing to inject into.\r\n"
                  L"Usage: myinput_inject [-c config] executable [args...]");
            return -1;
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
        if (!StrToValue(args[argI], &value)) {
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
        Path workDir = registered ? Path() : PathGetDirName(args[argI]);
        wstring cmdLineBuf;

    retry:
        // debug flags prevent debugger (which might be us again!) from interfering
        if (CreateProcessW(nullptr, processCmdLine, nullptr, nullptr, registered,
                           CREATE_SUSPENDED | DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS,
                           nullptr, workDir,
                           &si, &pi)) {
            DebugActiveProcessStop(pi.dwProcessId); // was needed just at creation time
            created = true;
            success = true;
        } else if (GetLastError() == ERROR_NOT_SUPPORTED) {
            // attempt to debug wrong bitness exe in some cases
        } else if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
            Path ownPath = PathGetModulePath(nullptr);

            // since runas checks the zone identifier
            Path ownZoneId = PathConcatRaw(ownPath, L":Zone.Identifier");
            DeleteFileW(ownZoneId);

            SHELLEXECUTEINFOW exec = {};
            exec.cbSize = sizeof(exec);
            exec.fMask = SEE_MASK_NOCLOSEPROCESS;
            exec.nShow = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOW;
            exec.lpVerb = L"runas";
            exec.lpFile = ownPath;
            exec.lpParameters = SkipCommandLineArgs(cmdLine, 1);
            if (ShellExecuteExW(&exec) && exec.hProcess) {
                redirected = true;
                pi.hProcess = exec.hProcess;
            } else {
                Alert(L"Failed elevating %ws", args[argI]);
                return -1;
            }
        } else if (GetLastError() == ERROR_BAD_EXE_FORMAT) {
            Path targetExe, targetArgs, targetWorkDir;
            if (ResolveLink(args[argI], &targetExe, &targetArgs, &targetWorkDir) &&
                targetExe) {
                cmdLineBuf = L"\"" + wstring(targetExe) + L"\"";

                if (targetArgs && *targetArgs) {
                    cmdLineBuf += L" " + wstring(targetArgs);
                }

                const wchar_t *extraArgs = SkipCommandLineArgs(processCmdLine, 1);
                if (extraArgs && *extraArgs) {
                    cmdLineBuf += L" " + wstring(extraArgs);
                }

                if (targetWorkDir && *targetWorkDir) {
                    workDir = move(targetWorkDir);
                }

                processCmdLine = cmdLineBuf.data();
                goto retry;
            } else {
                Alert(L"Failed starting %ws - not an executable.", args[argI]);
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
        if (ReplaceWithOtherBitness(&dirName)) {
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
        Path dllPath = PathCombine(ownDir, L"myinput_hook.dll");

        success = DoInject(pi.hProcess, dllPath);
    }

    if (pi.hThread) {
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
    }

    DWORD exitCode = -1;
    if (registered || redirected) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        exitCode = success ? 0 : 1;
    }

    CloseHandle(pi.hProcess);
    return exitCode;
}
