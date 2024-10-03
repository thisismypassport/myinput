#pragma once
#include "UtilsBase.h"
#include "UtilsPath.h"
#include <Windows.h>
#include <stdio.h>

int MsgBox(int opts, const wchar_t *format, ...) {
    va_list va;
    va_start(va, format);

    wchar_t buffer[1024];
    vswprintf(buffer, sizeof buffer, format, va);
    int result = MessageBoxW(nullptr, buffer, L"Error", opts);

    va_end(va);
    return result;
}

#define Alert(...) MsgBox(MB_OK | MB_ICONERROR, __VA_ARGS__)
#define Question(...) (MsgBox(MB_YESNO | MB_ICONQUESTION, __VA_ARGS__) == IDYES)

Path SelectFile(bool isOpen, const wchar_t *filters, const wchar_t *title, bool extraBool) {
    Path result(MAX_PATH);

    OPENFILENAMEW open = {};
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

#define DEFINE_ALERT_ON_LOG_COND(min_level, cond)            \
    void Log(LogLevel level, const char *str, size_t size) { \
        if (level >= min_level && (cond))                    \
            Alert(L"%hs", str);                              \
    }

#define DEFINE_ALERT_ON_LOG(min_level) DEFINE_ALERT_ON_LOG_COND(min_level, true)

void TurnOffFeedbackCursor() {
    MSG msg;
    PostMessageW(NULL, WM_NULL, 0, 0);
    GetMessageW(&msg, NULL, 0, 0);
}
