#pragma once
#include "UtilsBase.h"

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
    while (true) {
        auto pos = str.find(other);
        if (pos == std::basic_string<TChar>::npos) {
            return count;
        }

        str.replace(pos, tstrlen(other), replacement);
        count++;
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
void StrToLowerCase(std::basic_string<TChar> &str) {
    transform(str.begin(), str.end(), str.begin(), tolower);
}

template <class TChar>
std::basic_string<TChar> StrLowerCase(const std::basic_string<TChar> &str) {
    std::basic_string<TChar> dest;
    dest.resize(str.size());
    transform(str.begin(), str.end(), dest.begin(), tolower);
    return dest;
}

bool tstreq(const char *str1, const char *str2) { return strcmp(str1, str2) == 0; }
bool tstrieq(const char *str1, const char *str2) { return _stricmp(str1, str2) == 0; }
bool tstrneq(const char *str1, const char *str2, size_t count) { return strncmp(str1, str2, count) == 0; }
bool tstrnieq(const char *str1, const char *str2, size_t count) { return _strnicmp(str1, str2, count) == 0; }

bool tstreq(const wchar_t *str1, const wchar_t *str2) { return wcscmp(str1, str2) == 0; }
bool tstrieq(const wchar_t *str1, const wchar_t *str2) { return _wcsicmp(str1, str2) == 0; }
bool tstrneq(const wchar_t *str1, const wchar_t *str2, size_t count) { return wcsncmp(str1, str2, count) == 0; }
bool tstrnieq(const wchar_t *str1, const wchar_t *str2, size_t count) { return _wcsnicmp(str1, str2, count) == 0; }
