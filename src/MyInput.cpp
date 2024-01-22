#include "UtilsPath.h"
#include "UtilsStr.h"
#include "UtilsUiBase.h"
#include <Windows.h>

// this exe just redirects to ui of appropriate bitness

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    BOOL wow;
    const wchar_t *dir = IsWow64Process(GetCurrentProcess(), &wow) && wow ? L"x64" : L"Win32";

    Path uiPath = PathCombine(PathCombine(PathGetDirName(PathGetModulePath(nullptr)), dir), L"myinput_ui.exe");

    PROCESS_INFORMATION pi;
    STARTUPINFOW si = {};
    si.cb = sizeof(si);

    if (!CreateProcessW(uiPath, nullptr, nullptr, nullptr, false,
                        0, nullptr, nullptr, &si, &pi)) {
        Alert(L"Couldn't find myinput_ui.exe");
        return 1;
    }

    return 0;
}
