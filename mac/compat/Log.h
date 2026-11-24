#pragma once
// macOS counterpart of src/Log.h: one log file per hooked app.
#include "UtilsBase.h"
#include "UtilsPath.h"
#include "LogUtils.h"
#include <cstdio>
#include <sys/stat.h>

FILE *GLogFile = nullptr;
bool GLogToStderr = false;

void LogInit(const Path &inputPath) {
    PathA path = PathToStr(inputPath);
    PathA dir = PathToStr(PathGetDirName(inputPath));
    mkdir(dir, 0755);
    GLogFile = fopen(path, "w");
    GLogToStderr = getenv("MYINPUT_HOOK_STDERR") != nullptr;
}

void Log(LogLevel level, const char *str, size_t size) {
    if (GLogFile) {
        fwrite(str, 1, size, GLogFile);
        fflush(GLogFile);
    }
    if (GLogToStderr) {
        fputs("[myinput] ", stderr);
        fwrite(str, 1, size, stderr);
    }
}
