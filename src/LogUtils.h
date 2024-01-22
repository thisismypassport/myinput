#pragma once
#include "UtilsBase.h"
#include <Windows.h>

enum class LogLevel {
    // Will add more if needed...
    Default,
    Error,
};

void Log(LogLevel level, const char *str, size_t size);

template <class LogFunc>
void Log(LogLevel level, LogFunc &&func) {
    stringstream stream;
    func(stream);
    auto str = stream.str();
    Log(level, str.data(), str.size());
}

#define LOG_OF(level) Log (level, [&] (std::ostream& LogTarget) { LogTarget
#define END    \
    std::endl; \
    })

#define LOG LOG_OF(LogLevel::Default)
#define LOG_ERR LOG_OF(LogLevel::Error)

int Fatal(const char *str) {
    LOG_ERR << "FATAL: " << str << END;
    return 0;
}

#define ASSERT(cond, str) ((cond) || Fatal(str))

#ifdef _DEBUG
#define DBG_ASSERT(cond, str) ASSERT(cond, str)
#else
#define DBG_ASSERT(cond, str)
#endif

class UniqueLog {
    WeakAtomic<bool> mValue = false;

public:
    explicit operator bool() {
        return !mValue.exchange(true);
    }
};

template <int MaxEstimate>
class LimitedLog {
    atomic<int> mValue = 0;

public:
    explicit operator bool() {
        if (mValue >= MaxEstimate) { // may exceed the estimate
            return false;
        }

        mValue++;
        return true;
    }
};
