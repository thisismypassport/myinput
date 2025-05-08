#include "UtilsPath.h"
#include "UtilsStr.h"
#include "UtilsUiBase.h"
#include "Registry.h"
#include "Link.h"
#include <Windows.h>

DEFINE_ALERT_ON_ERROR()

int CmdLineRegisterUnregister(RegIfeoKey &ifeoKey, int numArgs, LPWSTR *args) {
    bool reg = false, unreg = false, byName = false;
    Path nameOrPath, config;

    for (int argI = 1; argI < numArgs; argI++) {
        if (tstreq(args[argI], L"-r")) {
            reg = true;
        } else if (tstreq(args[argI], L"-u")) {
            unreg = true;
        } else if (tstreq(args[argI], L"-n")) {
            byName = true;
        } else if (tstreq(args[argI], L"-c") && argI + 1 < numArgs) {
            config = args[++argI];
        } else if (!nameOrPath && args[argI][0] != L'-') {
            nameOrPath = args[argI];
        } else {
            Alert(L"Unrecognized option: %ws", args[argI]);
        }
    }

    if (!nameOrPath) {
        nameOrPath = SelectFileForOpen(L"Executables\0*.EXE\0", L"Choose Executable", false);
        if (!nameOrPath) {
            return 2;
        }
    }

    Path exeName, fullPath;
    if (byName || PathGetDirName(nameOrPath)[0] == L'\0') {
        exeName = PathGetBaseName(nameOrPath);
    } else {
        fullPath = PathGetFullPath(nameOrPath);
        exeName = PathGetBaseName(fullPath);

        if (fullPath && tstrieq(PathGetExtPtr(fullPath), L"lnk") &&
            ResolveLink(fullPath, &fullPath)) {
            exeName = PathGetBaseName(fullPath);
        }
    }

    if (!reg && !unreg) {
        RegisteredExeInfo info;
        if (!ifeoKey.GetRegisteredExe(exeName, fullPath, &info, true)) {
            reg = Question(L"Register %ws?", exeName.Get());
        } else if (!info.Exact) {
            reg = Question(L"%ws is registered to %ws\nRe-register it?", exeName.Get(), info.InjectExe.Get());
        } else {
            unreg = Question(L"%ws is already registered\nUnregister it?", exeName.Get());
        }

        if (!config && info.Config) {
            config = move(info.Config);
        }
    }

    bool ok;
    if (reg) {
        ok = ifeoKey.RegisterExe(exeName, fullPath, config);
    } else if (unreg) {
        ok = ifeoKey.UnregisterExe(exeName, fullPath);
    } else {
        return 2;
    }

    return ok ? 0 : 1;
}

int PipeRegisterUnregister(RegIfeoKey &ifeoKey, const wchar_t *name) {
    TurnOffFeedbackCursor();

    HANDLE handle = RegIfeoKeyDelegatedBase::OpenPipe(name);
    if (!handle) {
        Alert(L"Failed opening pipe");
        return 1;
    }

    RegIfeoKeyDelegatedProcessor processor(ifeoKey, handle);
    if (!processor.Run()) {
        Alert(L"Unexpected error while reading pipe");
        return 1;
    }

    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    RegIfeoKey ifeoKey(RegMode::CreateOrEdit);
    if (!ifeoKey) {
        Alert(L"Failed opening HKEY_LOCAL_MACHINE\\%ws for writing - no admin access?", IFEO_KEY);
        return 1;
    }

    int numArgs;
    LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &numArgs);

    if (numArgs == 3 && tstreq(args[1], L"-pipe")) {
        return PipeRegisterUnregister(ifeoKey, args[2]);
    } else {
        return CmdLineRegisterUnregister(ifeoKey, numArgs, args);
    }
}
