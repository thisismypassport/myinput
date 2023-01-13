#pragma once
#include "UtilsBase.h"
#include "UtilsPath.h"
#include <Windows.h>

HANDLE GLogFile;

void LogInit(const Path &inputPath) {
    Path fallbackPath;
    int num = 0;
    while (num < 100) {
        const Path &path = num ? fallbackPath : inputPath;
        GLogFile = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, 0, nullptr);

        if (GLogFile != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) {
            break;
        }

        Path dirName = PathGetDirName(inputPath);
        wstringstream fileNameStream;
        fileNameStream << PathGetBaseNameWithoutExt(inputPath).Get() << L"." << ++num << L"." << PathGetExt(inputPath).Get();
        fallbackPath = PathCombine(dirName, fileNameStream.str().c_str());
    }
}

void Log(const char *str, size_t size) {
    if (GLogFile) {
        OVERLAPPED overlapped = {};
        overlapped.Offset = -1;
        overlapped.OffsetHigh = -1;

        DWORD count;
        WriteFile(GLogFile, str, (DWORD)size, &count, &overlapped);
    }
}

template <class LogFunc>
void Log(LogFunc &&func) {
    if (GLogFile) {
        stringstream stream;
        func(stream);
        auto str = stream.str();
        Log(str.data(), str.size());
    }
}

#define LOG Log ([&] (std::ostream& LogTarget) { LogTarget
#define END    \
    std::endl; \
    })

int Fatal(const char *str) {
    LOG << "FATAL: " << str << END;
    if (IsDebuggerPresent()) {
        DebugBreak();
    }
    return 0;
}

#define ASSERT(cond, str) ((cond) || Fatal(str));

#ifdef _DEBUG
#define DBG_ASSERT(cond, str) ASSERT(cond, str)
#else
#define DBG_ASSERT(cond, str)
#endif
