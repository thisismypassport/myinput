#pragma once
#include "UtilsBase.h"
#include <Windows.h>

void Log(const char *str, size_t size);

template <class LogFunc>
void Log(LogFunc &&func) {
    stringstream stream;
    func(stream);
    auto str = stream.str();
    Log(str.data(), str.size());
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
