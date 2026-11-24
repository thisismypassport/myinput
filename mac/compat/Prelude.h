#pragma once
// Force-included before the engine translation unit on macOS.
// The engine headers in src/ are written for MSVC, which pulls these in implicitly.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <ranges>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <xlocale.h>

#define __stdcall
#define __int64 long long

// libc++ can only parse floating point with std::from_chars on macOS 26 and later.
// The engine uses it for the numbers in config files, so route those through strtod instead.
// (Every standard header is included above, so the rename below only ever touches engine code.)
namespace std {
template <class T, class... TArgs>
from_chars_result myinput_from_chars(const char *first, const char *last, T &value, TArgs... args) {
    if constexpr (is_floating_point_v<T>) {
        if (first == last || isspace((unsigned char)*first) || *first == '+') {
            return {first, errc::invalid_argument};
        }
        string copy(first, last);
        char *end = nullptr;
        double parsed = strtod_l(copy.c_str(), &end, nullptr); // a null locale is the "C" locale
        if (end == copy.c_str()) {
            return {first, errc::invalid_argument};
        }
        value = (T)parsed;
        return {first + (end - copy.c_str()), errc()};
    } else {
        return from_chars(first, last, value, args...);
    }
}
} // namespace std
#define from_chars myinput_from_chars
