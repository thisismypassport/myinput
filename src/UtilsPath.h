#pragma once
#include "UtilsBase.h"
#include "UtilsStr.h"
#include <Windows.h>

template <class tchar>
class BasicPath {
    tchar *Buffer;

public:
    BasicPath() : Buffer(nullptr) {}

    BasicPath(size_t size) {
        if (size) {
            Buffer = new tchar[size];
            Buffer[0] = L'\0';
        } else {
            Buffer = nullptr;
        }
    }

    BasicPath(const tchar *source, size_t count) : BasicPath(source ? count + 1 : 0) {
        if (source) {
            tstrncpy(Buffer, source, count);
            Buffer[count] = L'\0';
        }
    }

    BasicPath(const tchar *source, const tchar *end) : BasicPath(source, end - source) {}
    BasicPath(const tchar *source) : BasicPath(source, source ? tstrlen(source) : 0) {}

    BasicPath(const BasicPath<tchar> &other) = delete;

    BasicPath(BasicPath<tchar> &&other) : Buffer(other.Buffer) {
        other.Buffer = nullptr;
    }

    ~BasicPath() {
        delete[] Buffer;
    }

    void operator=(const BasicPath<tchar> &other) = delete;

    void operator=(BasicPath<tchar> &&other) {
        delete[] Buffer;
        Buffer = other.Buffer;
        other.Buffer = nullptr;
    }

    const tchar *Get() const { return Buffer; }
    tchar *Get() { return Buffer; }

    BasicPath<tchar> Copy() const { return BasicPath<tchar>(Get()); }

    tchar *Take() {
        tchar *taken = Buffer;
        Buffer = nullptr;
        return taken;
    }

    operator const tchar *() const { return Buffer; }
    operator tchar *() { return Buffer; }

    operator std::basic_string_view<tchar>() const { return std::basic_string_view<tchar>(Buffer); }
};

using PathA = BasicPath<char>;
using Path = BasicPath<wchar_t>;

Path PathFromStr(const char *str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);

    Path dest(size);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, dest, size);
    return dest;
}

PathA PathToStr(const wchar_t *path) {
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);

    PathA dest(size);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, dest, size, nullptr, nullptr);
    return dest;
}

Path PathGetFullPath(const wchar_t *path) {
    DWORD size = GetFullPathNameW(path, 0, nullptr, nullptr);

    Path dest(size);
    GetFullPathNameW(path, size, dest, nullptr);
    return dest;
}

Path PathGetCurrentDirectory() {
    DWORD size = GetCurrentDirectoryW(0, nullptr);

    Path dest(size);
    GetCurrentDirectoryW(size, dest);
    return dest;
}

Path PathGetEnvVar(const wchar_t *key) {
    DWORD size = GetEnvironmentVariableW(key, nullptr, 0);

    Path dest(size);
    GetEnvironmentVariableW(key, dest, size);
    return dest;
}

Path PathGetModulePath(HMODULE module) {
    DWORD bufferSize = MAX_PATH;
    while (true) {
        Path dest(bufferSize);
        if (GetModuleFileNameW(module, dest, bufferSize) < bufferSize) {
            return dest;
        }

        bufferSize *= 2;
    }
}

const wchar_t *PathGetBaseNamePtr(const wchar_t *path) {
    const wchar_t *path_sep = wcsrchr(path, L'\\');
    const wchar_t *path_sep2 = wcsrchr(path_sep ? path_sep : path, L'/');
    return path_sep2 ? path_sep2 + 1 : path_sep ? path_sep + 1
                                                : path;
}

Path PathGetBaseName(const wchar_t *path) {
    return Path(PathGetBaseNamePtr(path));
}

Path PathGetDirName(const wchar_t *path) {
    const wchar_t *path_sep = wcsrchr(path, L'\\');
    const wchar_t *path_sep2 = wcsrchr(path_sep ? path_sep : path, L'/');
    const wchar_t *end = path_sep2 ? path_sep2 : path_sep ? path_sep
                                                          : path;

    return Path(path, end);
}

const wchar_t *PathGetExtPtr(const wchar_t *path) {
    const wchar_t *path_sep = wcsrchr(path, L'\\');
    const wchar_t *path_sep2 = wcsrchr(path_sep ? path_sep : path, L'/');
    const wchar_t *start = path_sep2 ? path_sep2 + 1 : path_sep ? path_sep + 1
                                                                : path;

    const wchar_t *ext = wcsrchr(start, L'.');
    return ext ? ext + 1 : L"";
}

Path PathGetExt(const wchar_t *path) {
    return Path(PathGetExtPtr(path));
}

Path PathGetBaseNameWithoutExt(const wchar_t *path) {
    const wchar_t *path_sep = wcsrchr(path, L'\\');
    const wchar_t *path_sep2 = wcsrchr(path_sep ? path_sep : path, L'/');
    const wchar_t *start = path_sep2 ? path_sep2 + 1 : path_sep ? path_sep + 1
                                                                : path;

    const wchar_t *end = wcsrchr(start, L'.');
    if (end) {
        return Path(start, end);
    } else {
        return Path(start);
    }
}

Path PathCombine(const wchar_t *path1, const wchar_t *path2) // assumes path2 is relative!
{
    size_t path1len = wcslen(path1);
    size_t path2len = wcslen(path2);

    Path path(path1len + path2len + 2);
    wcscpy(path, path1);

    wchar_t last_ch = path1[path1len - 1];
    if (last_ch == L'\\' || last_ch == L'/') {
        wcscpy(path + path1len, path2);
        path[path1len + path2len] = L'\0';
    } else {
        path[path1len] = L'\\';
        wcscpy(path + path1len + 1, path2);
        path[path1len + path2len + 1] = L'\0';
    }
    return path;
}

Path PathCombineExt(const wchar_t *base, const wchar_t *ext) {
    size_t baselen = wcslen(base);
    size_t extlen = wcslen(ext);

    Path path(baselen + extlen + 2);
    wcscpy(path, base);
    path[baselen] = L'.';
    wcscpy(path + baselen + 1, ext);
    path[baselen + extlen + 1] = L'\0';

    return path;
}

Path PathConcatRaw(const wchar_t *path1, const wchar_t *path2) {
    size_t len1 = wcslen(path1);
    size_t len2 = wcslen(path2);

    Path path(len1 + len2 + 1);
    wcscpy(path, path1);
    wcscpy(path + len1, path2);
    path[len1 + len2] = L'\0';

    return path;
}

ostream &operator<<(ostream &o, const wchar_t *wstr) {
    PathA str = PathToStr(wstr);
    o << str;
    return o;
}
