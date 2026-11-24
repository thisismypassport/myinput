#pragma once
// macOS counterpart of src/UtilsPath.h: same Path type and helpers, POSIX underneath.
// Paths stay wchar_t-based because the shared engine passes them around as const wchar_t*.
#include "UtilsBase.h"
#include "UtilsStr.h"
#include <Windows.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <unistd.h>

struct TakeTag {};

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
    BasicPath(tchar *source, TakeTag) : Buffer(source) {}
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

    // Lets a wide path open a std::ifstream, which only MSVC accepts directly.
    operator string() const
        requires(sizeof(tchar) != sizeof(char))
    {
        return Buffer ? ToStdStr(wstring_view(Buffer)) : string();
    }
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
    char resolved[PATH_MAX];
    PathA narrow = PathToStr(path);
    return PathFromStr(realpath(narrow, resolved) ? resolved : narrow.Get());
}

Path PathGetCurrentDirectory() {
    char buffer[PATH_MAX];
    return PathFromStr(getcwd(buffer, sizeof(buffer)) ? buffer : ".");
}

Path PathGetEnvVar(const wchar_t *key) {
    const char *value = getenv(PathToStr(key));
    return value && *value ? PathFromStr(value) : Path();
}

// nullptr means the main executable, anything else is an address inside the wanted image.
Path PathGetModulePath(HMODULE module) {
    char buffer[PATH_MAX];
    if (module) {
        Dl_info info;
        if (dladdr(module, &info) && info.dli_fname) {
            return PathFromStr(info.dli_fname);
        }
    }
    uint32_t size = sizeof(buffer);
    return PathFromStr(_NSGetExecutablePath(buffer, &size) == 0 ? buffer : "");
}

const wchar_t *PathGetBaseNamePtr(const wchar_t *path) {
    const wchar_t *sep = wcsrchr(path, L'/');
    return sep ? sep + 1 : path;
}

Path PathGetBaseName(const wchar_t *path) {
    return Path(PathGetBaseNamePtr(path));
}

Path PathGetDirName(const wchar_t *path) {
    const wchar_t *sep = wcsrchr(path, L'/');
    return Path(path, sep ? sep : path);
}

const wchar_t *PathGetExtPtr(const wchar_t *path) {
    const wchar_t *ext = wcsrchr(PathGetBaseNamePtr(path), L'.');
    return ext ? ext + 1 : L"";
}

Path PathGetExt(const wchar_t *path) {
    return Path(PathGetExtPtr(path));
}

Path PathGetBaseNameWithoutExt(const wchar_t *path) {
    const wchar_t *start = PathGetBaseNamePtr(path);
    const wchar_t *end = wcsrchr(start, L'.');
    return end ? Path(start, end) : Path(start);
}

Path PathCombine(const wchar_t *path1, const wchar_t *path2) // assumes path2 is relative!
{
    wstring combined = path1;
    if (!combined.empty() && combined.back() != L'/') {
        combined += L'/';
    }
    combined += path2;
    return Path(combined.c_str());
}

Path PathCombineExt(const wchar_t *base, const wchar_t *ext) {
    wstring combined = base;
    combined += L'.';
    combined += ext;
    return Path(combined.c_str());
}

Path PathConcatRaw(const wchar_t *path1, const wchar_t *path2) {
    wstring combined = path1;
    combined += path2;
    return Path(combined.c_str());
}

ostream &operator<<(ostream &o, const wchar_t *wstr) {
    PathA str = PathToStr(wstr);
    o << str.Get();
    return o;
}
