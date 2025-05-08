#pragma once
#include "UtilsBase.h"
#include <charconv>
#include <Windows.h>

// (assumes tchar is a template param)
#define TSTR(lit) (sizeof(tchar) == sizeof(char) ? (const tchar *)lit : (const tchar *)CONCAT(L, lit))
#define TCHAR(lit) (sizeof(tchar) == sizeof(char) ? (tchar)lit : (tchar)CONCAT(L, lit))

#define WSTR(lit) CONCAT(L, lit)

size_t tstrlen(const char *str) { return strlen(str); }
size_t tstrlen(const wchar_t *str) { return wcslen(str); }
char *tstrcpy(char *dest, const char *src) { return strcpy(dest, src); }
wchar_t *tstrcpy(wchar_t *dest, const wchar_t *src) { return wcscpy(dest, src); }
char *tstrncpy(char *dest, const char *src, size_t len) { return strncpy(dest, src, len); }
wchar_t *tstrncpy(wchar_t *dest, const wchar_t *src, size_t len) { return wcsncpy(dest, src, len); }

template <class TChar>
bool StrContains(const std::basic_string<TChar> &str, TChar ch) // removeme once C++23 hits
{
    return str.find(ch) != std::basic_string<TChar>::npos;
}

template <class TChar>
bool StrContains(const std::basic_string<TChar> &str, const TChar *other) // removeme once C++23 hits
{
    return str.find(other) != std::basic_string<TChar>::npos;
}

template <class TChar>
bool StrReplaceFirst(std::basic_string<TChar> &str, const TChar *other, const TChar *replacement) {
    auto pos = str.find(other);
    if (pos == std::basic_string<TChar>::npos) {
        return false;
    }

    str.replace(pos, tstrlen(other), replacement);
    return true;
}

template <class TChar>
int StrReplaceAll(std::basic_string<TChar> &str, const TChar *other, const TChar *replacement) {
    int count = 0;
    size_t startPos = 0;
    while (true) {
        auto pos = str.find(other, startPos);
        if (pos == std::basic_string<TChar>::npos) {
            return count;
        }

        str.replace(pos, tstrlen(other), replacement);
        count++;
        startPos = pos + tstrlen(replacement);
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

wstring ToStdWStr(std::string_view str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);

    wstring dest;
    dest.resize(size); // TODO: resize_and_overwrite in c++23
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), dest.data(), size);
    return dest;
}

string ToStdStr(std::wstring_view str) {
    int size = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0, nullptr, nullptr);

    string dest;
    dest.resize(size); // TODO: resize_and_overwrite in c++23
    WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), dest.data(), size, nullptr, nullptr);
    return dest;
}

template <typename T, typename... TArgs>
bool StrToValue(const std::string_view &str, T *pValue, TArgs... args) {
    auto end = str.data() + str.size();
    auto result = std::from_chars(str.data(), end, *pValue, args...);
    return result.ec == std::errc() && result.ptr == end;
}

template <typename T, typename... TArgs>
bool StrToValue(const std::wstring_view &str, T *pValue, TArgs... args) {
    return StrToValue(ToStdStr(str), pValue, args...);
}

template <class TChar, class T, typename... TArgs>
std::basic_string<TChar> StrFromValue(T value, TArgs... args) {
    char buffer[0x100];
    char *end = buffer + sizeof(buffer);
    auto result = std::to_chars(buffer, end, value, args...);
    if (result.ec != std::errc()) {
        return {}; // should't happen..
    }

    if constexpr (std::is_same_v<TChar, char>) {
        return string(buffer, result.ptr);
    } else {
        return ToStdWStr(string_view(buffer, result.ptr));
    }
}

template <class TChar>
void StrToLowerCase(std::basic_string<TChar> &str) // ascii tolower!
{
    transform(str.begin(), str.end(), str.begin(), tolower);
}

template <class TChar>
std::basic_string<TChar> StrLowerCase(const std::basic_string<TChar> &str) // ascii tolower!
{
    std::basic_string<TChar> dest;
    dest.resize(str.size());
    transform(str.begin(), str.end(), dest.begin(), tolower);
    return dest;
}

int tstrcmp(const char *str1, const char *str2) { return strcmp(str1, str2); }
int tstricmp(const char *str1, const char *str2) { return _stricmp(str1, str2); }
bool tstreq(const char *str1, const char *str2) { return strcmp(str1, str2) == 0; }
bool tstrieq(const char *str1, const char *str2) { return _stricmp(str1, str2) == 0; }
bool tstrneq(const char *str1, const char *str2, size_t count) { return strncmp(str1, str2, count) == 0; }
bool tstrnieq(const char *str1, const char *str2, size_t count) { return _strnicmp(str1, str2, count) == 0; }

int tstrcmp(const wchar_t *str1, const wchar_t *str2) { return wcscmp(str1, str2); }
int tstricmp(const wchar_t *str1, const wchar_t *str2) { return _wcsicmp(str1, str2); }
bool tstreq(const wchar_t *str1, const wchar_t *str2) { return wcscmp(str1, str2) == 0; }
bool tstrieq(const wchar_t *str1, const wchar_t *str2) { return _wcsicmp(str1, str2) == 0; }
bool tstrneq(const wchar_t *str1, const wchar_t *str2, size_t count) { return wcsncmp(str1, str2, count) == 0; }
bool tstrnieq(const wchar_t *str1, const wchar_t *str2, size_t count) { return _wcsnicmp(str1, str2, count) == 0; }

const wchar_t *wcsistr(const wchar_t *str, const wchar_t *subStr) {
    if (!*subStr) {
        return str;
    }

    wchar_t l = towlower(*subStr);
    wchar_t u = towupper(*subStr);

    for (; *str; ++str) {
        if (*str == l || *str == u) {
            const wchar_t *s1 = str + 1;
            const wchar_t *s2 = subStr + 1;

            while (*s1 && *s2 && towupper(*s1) == towupper(*s2)) {
                ++s1, ++s2;
            }

            if (!*s2) {
                return str;
            }
        }
    }

    return nullptr;
}
