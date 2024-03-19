#pragma once
#include "UtilsUiBase.h"
#include "LogUtils.h"
#include <inttypes.h>

void AssertEquals(const char *name, int64_t value, int64_t expected) {
    if (value != expected) {
        Alert(L"%hs: %" PRId64 " != %" PRId64 "!", name, value, expected);
    }
}

void AssertNotEquals(const char *name, int64_t value, int64_t unexpected) {
    if (value == unexpected) {
        Alert(L"%hs: %" PRId64 " == %" PRId64 "!", name, value, unexpected);
    }
}

void AssertEquals(const char *name, const char *value, const char *expected) {
    if (strcmp(value, expected) != 0) {
        Alert(L"%hs: %hs != %hs!", name, value, expected);
    }
}

void AssertEquals(const char *name, const wchar_t *value, const wchar_t *expected) {
    if (wcscmp(value, expected) != 0) {
        Alert(L"%hs: %ws != %ws!", name, value, expected);
    }
}

void AssertEquals(const char *name, void *value, void *expected, int size) {
    if (memcmp(value, expected, size) != 0) {
        Alert(L"%hs: *%p != *%p!", name, value, expected);
    }
}

void AssertTrue(const char *name, bool cond) {
    if (!cond) {
        Alert(L"%hs: false!", name);
    }
}

DEFINE_ALERT_ON_LOG(LogLevel::Error)

template <class TFunc>
DWORD CreateThread(TFunc threadFunc) {
    DWORD threadId;
    CloseHandle(CreateThread(
        nullptr, 0, [](PVOID param) -> DWORD {
            (*((function<void()> *)param))();
            return 0;
        },
        new function<void()>(threadFunc), 0, &threadId));
    return threadId;
}

uint64_t GetPerfCounter() {
    return GetOutput(QueryPerformanceCounter).QuadPart;
}

double GetPerfDelay(uint64_t prev, uint64_t *nowPtr = nullptr) {
    static uint64_t freq = GetOutput(QueryPerformanceFrequency).QuadPart;

    uint64_t now = GetPerfCounter();
    if (nowPtr) {
        *nowPtr = now;
    }
    return (double)(now - prev) / freq;
}
