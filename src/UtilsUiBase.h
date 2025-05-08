#pragma once
#include "UtilsBase.h"
#include "UtilsPath.h"
#include <Windows.h>
#include <stdio.h>

int MsgBox(int opts, const wchar_t *title, const wchar_t *format, ...) {
    va_list va;
    va_start(va, format);

    wchar_t buffer[1024];
    vswprintf(buffer, sizeof buffer, format, va);
    int result = MessageBoxW(GetActiveWindow(), buffer, title, opts);

    va_end(va);
    return result;
}

#define Alert(...) (MsgBox(MB_OK | MB_ICONERROR, L"Error", __VA_ARGS__), (void)0)
#define Question(...) (MsgBox(MB_YESNO | MB_ICONQUESTION, L"Question", __VA_ARGS__) == IDYES)
#define Confirm(...) (MsgBox(MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2, L"Confirmation", __VA_ARGS__) == IDYES)

Path SelectFile(bool isOpen, const wchar_t *filters, const wchar_t *title, bool extraBool) {
    Path result(MAX_PATH);

    OPENFILENAMEW open = {};
    open.hwndOwner = GetActiveWindow();
    open.lStructSize = sizeof(open);
    open.lpstrFilter = filters;
    open.lpstrFile = result;
    open.lpstrTitle = title;
    open.nMaxFile = MAX_PATH;
    open.Flags = OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (isOpen && extraBool) {
        open.Flags |= OFN_FILEMUSTEXIST;
    } else if (!isOpen && extraBool) {
        open.Flags |= OFN_OVERWRITEPROMPT;
    }

    bool status = isOpen ? GetOpenFileNameW(&open) : GetSaveFileNameW(&open);
    if (!status) {
        result = Path();
    }
    return result;
}

Path SelectFileForOpen(const wchar_t *filters = nullptr, const wchar_t *title = nullptr, bool mustExist = true) {
    return SelectFile(true, filters, title, mustExist);
}
Path SelectFileForSave(const wchar_t *filters = nullptr, const wchar_t *title = nullptr, bool overwritePrompt = true) {
    return SelectFile(false, filters, title, overwritePrompt);
}

#define DEFINE_ALERT_ON_ERROR_COND(cond)                     \
    void Log(LogLevel level, const char *str, size_t size) { \
        if (level == LogLevel::Error && (cond))              \
            Alert(L"%hs", str);                              \
    }

#define DEFINE_ALERT_ON_ERROR() DEFINE_ALERT_ON_ERROR_COND(true)

void TurnOffFeedbackCursor() {
    MSG msg;
    PostMessageW(NULL, WM_NULL, 0, 0);
    GetMessageW(&msg, NULL, 0, 0);
}
