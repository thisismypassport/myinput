#pragma once
#include "UtilsBase.h"
#include <Windows.h>

class Path {
    wchar_t *Buffer;

public:
    Path() : Buffer(nullptr) {}

    Path(size_t size) {
        if (size) {
            Buffer = new wchar_t[size];
            Buffer[0] = L'\0';
        } else {
            Buffer = nullptr;
        }
    }

    Path(const wchar_t *source, size_t count) : Path(count + 1) {
        wcsncpy(Buffer, source, count);
        Buffer[count] = L'\0';
    }

    Path(const wchar_t *source, const wchar_t *end) : Path(source, end - source) {}
    Path(const wchar_t *source) : Path(source, wcslen(source)) {}

    Path(const Path &other) = delete;

    Path(Path &&other) : Buffer(other.Buffer) {
        other.Buffer = nullptr;
    }

    ~Path() {
        delete[] Buffer;
    }

    void operator=(const Path &other) = delete;

    void operator=(Path &&other) {
        delete[] Buffer;
        Buffer = other.Buffer;
        other.Buffer = nullptr;
    }

    const wchar_t *Get() const { return Buffer; }
    wchar_t *Get() { return Buffer; }

    wchar_t *Take() {
        wchar_t *taken = Buffer;
        Buffer = nullptr;
        return taken;
    }

    operator const wchar_t *() const { return Buffer; }
    operator wchar_t *() { return Buffer; }
};

Path PathFromStr(const char *str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);

    Path dest(size);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, dest, size);
    return dest;
}

char *PathToStrTake(const wchar_t *path) {
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);

    char *dest = new char[size];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, dest, size, nullptr, nullptr);
    return dest;
}

Path PathGetFullPath(const wchar_t *path) {
    DWORD size = GetFullPathNameW(path, 0, nullptr, nullptr);

    Path dest(size);
    GetFullPathNameW(path, size, dest, nullptr);
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

Path PathGetFinalPath(HANDLE handle, DWORD flags) {
    DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);

    Path dest(size);
    GetFinalPathNameByHandleW(handle, dest, size, flags);
    return dest;
}

Path PathGetBaseName(const wchar_t *path) {
    const wchar_t *path_sep = wcsrchr(path, L'\\');
    const wchar_t *path_sep2 = wcsrchr(path_sep ? path_sep : path, L'/');
    const wchar_t *start = path_sep2 ? path_sep2 + 1 : path_sep ? path_sep + 1
                                                                : path;

    return Path(start);
}

Path PathGetDirName(const wchar_t *path) {
    const wchar_t *path_sep = wcsrchr(path, L'\\');
    const wchar_t *path_sep2 = wcsrchr(path_sep ? path_sep : path, L'/');
    const wchar_t *end = path_sep2 ? path_sep2 : path_sep ? path_sep
                                                          : path;

    return Path(path, end);
}

Path PathGetExt(const wchar_t *path) {
    const wchar_t *path_sep = wcsrchr(path, L'\\');
    const wchar_t *path_sep2 = wcsrchr(path_sep ? path_sep : path, L'/');
    const wchar_t *start = path_sep2 ? path_sep2 + 1 : path_sep ? path_sep + 1
                                                                : path;

    const wchar_t *ext = wcsrchr(start, L'.');
    if (ext) {
        return Path(ext + 1);
    } else {
        return Path();
    }
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

ostream &operator<<(ostream &o, const wchar_t *wstr) {
    char *str = PathToStrTake(wstr);
    o << str;
    delete[] str;
    return o;
}
