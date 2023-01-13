#pragma once
#include "UtilsBase.h"
#include <sstream>

size_t StrGetSize(const char *str) { return strlen(str); }
size_t StrGetSize(const wchar_t *str) { return wcslen(str); }

template <class TChar>
bool StrStartsWith(const std::basic_string<TChar> &str, TChar ch) {
    return str.size() && str[0] == ch;
}

template <class TChar>
bool StrStartsWith(const std::basic_string<TChar> &str, const TChar *other) {
    size_t otherSize = StrGetSize(other);
    return str.size() >= otherSize && str.compare(0, otherSize, other) == 0;
}

template <class TChar>
bool StrContains(const std::basic_string<TChar> &str, TChar ch) {
    return str.find(ch) != std::basic_string<TChar>::npos;
}

template <class TChar>
bool StrContains(const std::basic_string<TChar> &str, const TChar *other) {
    return str.find(other) != std::basic_string<TChar>::npos;
}

template <class TChar>
bool StrReplaceFirst(std::basic_string<TChar> &str, const TChar *other, const TChar *replacement) {
    auto pos = str.find(other);
    if (pos == std::basic_string<TChar>::npos) {
        return false;
    }

    str.replace(pos, StrGetSize(other), replacement);
    return true;
}

template <class TChar>
int StrReplaceAll(std::basic_string<TChar> &str, const TChar *other, const TChar *replacement) {
    int count = 0;
    while (true) {
        auto pos = str.find(other);
        if (pos == std::basic_string<TChar>::npos) {
            return count;
        }

        str.replace(pos, StrGetSize(other), replacement);
        count++;
    }
}

template <class TChar>
std::basic_string<TChar> StrBeforeFirst(const std::basic_string<TChar> &str, TChar ch) {
    intptr_t idx = str.find(ch);
    if (idx >= 0) {
        return str.substr(0, idx);
    } else {
        return str;
    }
}

template <class TChar>
std::basic_string<TChar> StrAfterFirst(const std::basic_string<TChar> &str, TChar ch) {
    intptr_t idx = str.find(ch);
    if (idx >= 0) {
        return str.substr(idx + 1);
    } else {
        return std::basic_string<TChar>();
    }
}

template <class TChar>
std::basic_string<TChar> StrBeforeLast(const std::basic_string<TChar> &str, TChar ch) {
    intptr_t idx = str.rfind(ch);
    if (idx >= 0) {
        return str.substr(0, idx);
    } else {
        return std::basic_string<TChar>();
    }
}

template <class TChar>
std::basic_string<TChar> StrAfterLast(const std::basic_string<TChar> &str, TChar ch) {
    intptr_t idx = str.rfind(ch);
    if (idx >= 0) {
        return str.substr(idx + 1);
    } else {
        return str;
    }
}

template <class TChar>
std::basic_string<TChar> StrTrimmed(const std::basic_string<TChar> &str) {
    intptr_t start = 0;
    while ((size_t)start < str.size() && isspace(str[start])) {
        start++;
    }

    intptr_t end = str.size();
    while (end > 0 && isspace(str[end - 1])) {
        end--;
    }

    return str.substr(start, end - start);
}

template <class TChar>
bool StrIsBlank(const std::basic_string<TChar> &str) {
    intptr_t i = 0;
    while ((size_t)i < str.size() && isspace(str[i])) {
        i++;
    }

    return i == str.size();
}

template <class TChar, class T>
bool StrToValue(const std::basic_string<TChar> &str, T *pValue) {
    std::basic_stringstream<TChar> ss(str);
    ss >> *pValue;
    return !ss.fail();
}

template <class TChar, class T>
std::basic_string<TChar> StrFromValue(T value) {
    std::basic_stringstream<TChar> ss;
    ss << value;
    return ss.str();
}

template <class TChar>
std::basic_string<TChar> StrLowerCase(const std::basic_string<TChar> &str) {
    std::basic_string<TChar> dest;
    dest.resize(str.size());
    transform(str.begin(), str.end(), dest.begin(), tolower);
    return dest;
}

const wchar_t *_wcsistr(const wchar_t *str, const wchar_t *subStr) {
    if (!*subStr) {
        return str;
    }

    wchar_t l = towlower(*subStr);
    wchar_t u = towupper(*subStr);

    for (; *str; ++str) {
        if (*str == l || *str == u) {
            const wchar_t *s1 = str + 1;
            const wchar_t *s2 = subStr + 1;

            while (*s1 && *s2 && towlower(*s1) == towlower(*s2)) {
                ++s1, ++s2;
            }

            if (!*s2) {
                return str;
            }
        }
    }

    return nullptr;
}
