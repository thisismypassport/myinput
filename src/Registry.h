#pragma once
#include "UtilsStr.h"
#include "UtilsPath.h"
#include "LogUtils.h"
#include <Windows.h>

enum class RegMode {
    Read,
    Edit,
    CreateOrEdit,
};

class RegKey {
    HKEY mKey = nullptr;
    bool mClose = false;

    void Dtor() {
        if (mClose) {
            RegCloseKey(mKey);
        }
        mKey = nullptr;
        mClose = false;
    }

public:
    RegKey() {}
    RegKey(HKEY key) : mKey(key) {}

    RegKey(const RegKey &parent, const wchar_t *path, RegMode mode, bool *outCreated = nullptr) {
        int access = KEY_READ;
        if (mode != RegMode::Read) {
            access |= KEY_WRITE;
        }

        DWORD disp = REG_OPENED_EXISTING_KEY;
        if (mode == RegMode::CreateOrEdit) {
            mClose = RegCreateKeyExW(parent.mKey, path, 0, nullptr, 0, access, nullptr, &mKey, &disp) == ERROR_SUCCESS;
        } else {
            mClose = RegOpenKeyExW(parent.mKey, path, 0, access, &mKey) == ERROR_SUCCESS;
        }

        if (outCreated) {
            *outCreated = disp == REG_CREATED_NEW_KEY;
        }
    }

    RegKey(const RegKey &other) = delete;
    RegKey(RegKey &&other) { *this = move(other); }

    ~RegKey() { Dtor(); }

    void operator=(const RegKey &) = delete;
    void operator=(RegKey &&other) {
        Dtor();
        mKey = other.mKey;
        mClose = other.mClose;
        other.mKey = nullptr;
        other.mClose = false;
    }

    bool HasValue(const wchar_t *name) const {
        return RegGetValueW(mKey, nullptr, name, RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }

    bool GetIntValue(const wchar_t *name, int32_t *pIntValue) const {
        DWORD value = 0, size = sizeof(DWORD);
        RegGetValueW(mKey, nullptr, name, RRF_RT_DWORD, nullptr, &value, &size);
        *pIntValue = value;
        return size == sizeof(DWORD);
    }

    bool GetBoolValue(const wchar_t *name) const {
        int32_t value;
        return GetIntValue(name, &value) && value;
    }

    bool SetIntValue(const wchar_t *name, int32_t intValue) {
        DWORD value = intValue;
        return RegSetValueExW(mKey, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value)) == ERROR_SUCCESS;
    }

    bool SetBoolValue(const wchar_t *name, bool value) {
        return SetIntValue(name, (int32_t)value);
    }

    Path GetStringValue(const wchar_t *name) const {
        DWORD size = 0;
        RegGetValueW(mKey, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size);
        if (size == 0) {
            return Path();
        }

        while (true) {
            Path path(size);
            if (RegGetValueW(mKey, nullptr, name, RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_MORE_DATA) {
                return path;
            } else {
                size *= 2;
            }
        }
    }

    bool SetStringValue(const wchar_t *name, const wchar_t *value) {
        return RegSetValueExW(mKey, name, 0, REG_SZ, (const BYTE *)value, (DWORD)(wcslen(value) + 1) * sizeof(wchar_t)) == ERROR_SUCCESS;
    }

    bool DeleteValue(const wchar_t *name) {
        return RegDeleteValueW(mKey, name) == ERROR_SUCCESS;
    }

    int NumChildren() const {
        DWORD numSubkeys = 0;
        RegQueryInfoKeyW(mKey, nullptr, nullptr, nullptr, &numSubkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        return numSubkeys;
    }

    bool HasChildren() const { return NumChildren() != 0; }

    int NumValues() const {
        DWORD numValues = 0;
        RegQueryInfoKeyW(mKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &numValues, nullptr, nullptr, nullptr, nullptr);
        return numValues;
    }

    bool HasValues() const { return NumValues() != 0; }

    bool IsEmpty() const {
        return !HasChildren() && !HasValues();
    }

    bool DeleteChild(const wchar_t *name) {
        return RegDeleteKeyExW(mKey, name, 0, 0) == ERROR_SUCCESS;
    }

    vector<Path> GetChildren() const {
        DWORD numSubkeys = 0, maxSubkeyLen = 0;
        RegQueryInfoKeyW(mKey, nullptr, nullptr, nullptr, &numSubkeys, &maxSubkeyLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        vector<Path> names;
        for (DWORD i = 0; i < numSubkeys; i++) {
            DWORD nameSize = maxSubkeyLen + 1;
            Path name(nameSize);
            LSTATUS status = RegEnumKeyExW(mKey, i, name, &nameSize, nullptr, nullptr, nullptr, nullptr);

            if (status == ERROR_NO_MORE_ITEMS) {
                break;
            }

            if (status == ERROR_MORE_DATA) {
                maxSubkeyLen *= 2;
                i--;
                continue;
            }

            if (status == ERROR_SUCCESS) {
                names.push_back(move(name));
            }
        }
        return names;
    }

    operator bool() const { return mKey != nullptr; }
};

#define IFEO_KEY LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options)"
#define USE_FILTER_VALUE L"UseFilter"
#define FILTER_PATH_VALUE L"FilterFullPath"
#define DEBUGGER_VALUE L"Debugger"
#define MYINPUT_INJECT_EXE L"myinput_inject.exe"

struct RegisteredExeInfo {
    Path InjectExe;
    Path Config;
    bool Exact = false;
    bool ByPath = false;
};

struct RegisteredExe {
    Path Name;
    Path FullPath;
    Path Config;
    bool Exact = false;
};

class RegIfeoKey {
    RegKey mKey;
    Path mInjectExePath;

    bool GetRegisteredExeFrom(RegKey &targetKey, RegisteredExeInfo *outInfo) const {
        bool isRegistered = false;
        Path exeDebugger = targetKey.GetStringValue(DEBUGGER_VALUE);
        if (exeDebugger) {
            int numArgs = 0;
            LPWSTR *args = CommandLineToArgvW(exeDebugger, &numArgs);

            if (args && numArgs &&
                wcsstr(args[0], MYINPUT_INJECT_EXE)) {
                isRegistered = true;

                outInfo->InjectExe = args[0];
                outInfo->Exact = tstreq(args[0], mInjectExePath);

                for (int argI = 1; argI < numArgs; argI++) {
                    if (tstreq(args[argI], L"-c") && argI + 1 < numArgs) {
                        outInfo->Config = args[++argI];
                    }
                }
            }

            LocalFree(args);
        }
        return isRegistered;
    }

public:
    RegIfeoKey(RegMode mode, HMODULE baseModule = nullptr) : mKey(RegKey(HKEY_LOCAL_MACHINE), IFEO_KEY, mode) {
        mInjectExePath = PathCombine(PathGetDirName(PathGetModulePath(baseModule)), MYINPUT_INJECT_EXE);
    }

    bool GetRegisteredExe(const wchar_t *exeName, const wchar_t *fullPath, RegisteredExeInfo *outInfo, bool exactPath) const {
        bool isRegistered = false;
        RegKey exeKey(mKey, exeName, RegMode::Read);
        RegKey pathKey;
        if (exeKey) {
            if (fullPath && exeKey.GetBoolValue(USE_FILTER_VALUE)) {
                for (Path &filterName : exeKey.GetChildren()) {
                    RegKey childKey(exeKey, filterName, RegMode::Read);
                    Path childPath = childKey.GetStringValue(FILTER_PATH_VALUE);
                    if (childPath && tstrieq(childPath, fullPath) &&
                        childKey.HasValue(DEBUGGER_VALUE)) {
                        pathKey = move(childKey);
                        break;
                    }
                }
            }

            if (!(exactPath && fullPath && !pathKey)) {
                RegKey &targetKey = pathKey ? pathKey : exeKey;
                isRegistered = GetRegisteredExeFrom(targetKey, outInfo);

                if (isRegistered && pathKey) {
                    outInfo->ByPath = true;
                }
            }
        }
        return isRegistered;
    }

    bool RegisterExe(const wchar_t *exeName, const wchar_t *fullPath, const wchar_t *config) {
        RegKey exeKey(mKey, exeName, RegMode::CreateOrEdit);
        RegKey pathKey;

        if (fullPath) {
            if (!exeKey.GetBoolValue(USE_FILTER_VALUE)) {
                exeKey.SetBoolValue(USE_FILTER_VALUE, true);
            }

            // find existing full path filter
            for (Path &filterName : exeKey.GetChildren()) {
                RegKey childKey(exeKey, filterName, RegMode::Edit);
                Path childPath = childKey.GetStringValue(FILTER_PATH_VALUE);
                if (childPath && tstrieq(childPath, fullPath)) {
                    pathKey = move(childKey);
                    break;
                }
            }

            // create new full path filter
            if (!pathKey) {
                wstring filterBase = L"MyInput_";
                filterBase += StrFromValue<wchar_t>(time(nullptr));

                for (int i = 0; i < 100; i++) {
                    wstring filterName = filterBase;
                    if (i) {
                        filterName += L"_" + StrFromValue<wchar_t>(i);
                    }

                    bool created;
                    RegKey childKey(exeKey, filterName.c_str(), RegMode::CreateOrEdit, &created);
                    if (childKey && created &&
                        childKey.SetStringValue(FILTER_PATH_VALUE, fullPath)) {
                        pathKey = move(childKey);
                        break;
                    }
                }
            }

            if (!pathKey) {
                LOG_ERR << "Failed to create path filter in registry" << END;
                return false;
            }
        }

        wstring cmdLine = L"\"" + wstring(mInjectExePath) + L"\" -r";
        if (config) {
            cmdLine += L" -c \"" + wstring(config) + L"\"";
        }

        RegKey &targetKey = pathKey ? pathKey : exeKey;
        if (!targetKey.SetStringValue(DEBUGGER_VALUE, cmdLine.c_str())) {
            LOG_ERR << "Failed to set Debugger value in registry" << END;
            return false;
        }

        return true;
    }

    bool UnregisterExe(const wchar_t *exeName, const wchar_t *fullPath) {
        RegKey exeKey(mKey, exeName, RegMode::Edit);
        RegKey pathKey;
        Path pathFilter;
        if (exeKey) {
            if (fullPath) {
                if (!exeKey.GetBoolValue(USE_FILTER_VALUE)) {
                    return false;
                }

                for (Path &filterName : exeKey.GetChildren()) {
                    RegKey childKey(exeKey, filterName, RegMode::Edit);
                    Path childPath = childKey.GetStringValue(FILTER_PATH_VALUE);
                    if (childPath && tstrieq(childPath, fullPath)) {
                        pathKey = move(childKey);
                        pathFilter = move(filterName);
                        break;
                    }
                }

                if (!pathKey) {
                    return false;
                }
            }

            RegKey &targetKey = pathKey ? pathKey : exeKey;
            if (!targetKey.DeleteValue(DEBUGGER_VALUE)) {
                LOG_ERR << "Failed to delete Debugger value in registry" << END;
                return false;
            }

            if (pathKey && !pathKey.HasChildren() && pathKey.NumValues() <= 1 &&
                pathKey.DeleteValue(FILTER_PATH_VALUE)) {
                if (!exeKey.DeleteChild(pathFilter)) {
                    LOG_ERR << "Failed to delete empty " << pathFilter << " filter key in registry" << END;
                    return false;
                }

                if (!exeKey.HasChildren()) {
                    exeKey.DeleteValue(USE_FILTER_VALUE);
                }
            }

            if (exeKey.IsEmpty() && !mKey.DeleteChild(exeName)) {
                LOG_ERR << "Failed to delete empty " << exeName << " key in registry" << END;
                return false;
            }
        }
        return true;
    }

    vector<RegisteredExe> GetRegisteredExes() const {
        vector<RegisteredExe> exes;
        for (Path &exeName : mKey.GetChildren()) {
            RegKey exeKey(mKey, exeName, RegMode::Read);
            if (exeKey) {
                if (exeKey.GetBoolValue(USE_FILTER_VALUE)) {
                    for (Path &filterName : exeKey.GetChildren()) {
                        RegKey childKey(exeKey, filterName, RegMode::Read);
                        Path childPath = childKey.GetStringValue(FILTER_PATH_VALUE);

                        RegisteredExeInfo exeInfo;
                        if (childPath && GetRegisteredExeFrom(childKey, &exeInfo)) {
                            exes.push_back({Path(exeName.Get()), move(childPath), move(exeInfo.Config), exeInfo.Exact});
                        }
                    }
                }

                RegisteredExeInfo exeInfo;
                if (GetRegisteredExeFrom(exeKey, &exeInfo)) {
                    exes.push_back({move(exeName), nullptr, move(exeInfo.Config), exeInfo.Exact});
                }
            }
        }
        return exes;
    }

    operator bool() const { return (bool)mKey; }
};

class RegIfeoKeyDelegatedBase {
protected:
    HANDLE mHandle = nullptr;
    bool mOk = false;

    void SetHandle(HANDLE handle) {
        if (handle) {
            mHandle = handle;
            mOk = true;
        }
    }

    enum Cmd : int32_t {
        EndCmd,
        RegisterCmd,
        UnregisterCmd,
    };

    static constexpr uint32_t PackageHeader = 0xdeffedd0;

    struct Package {
        uint32_t Header;
        int32_t Cmd;
        int32_t Args[6];
    };

    int32_t WStrSize(const wchar_t *str) {
        return str ? (int)(wcslen(str) * sizeof(wchar_t)) : -1;
    }

    void Write(const void *data, int size) {
        if (data && size > 0) {
            DWORD junk;
            if (!WriteFile(mHandle, data, size, &junk, nullptr)) {
                mOk = false;
            }
        }
    }

    void WriteStatus(bool ok) {
        int32_t status = ok ? 1 : 0;
        Write(&status, sizeof(status));
    }

    bool Read(void *data, int size) {
        if (data && size) {
            DWORD actualSize;
            if (!ReadFile(mHandle, data, size, &actualSize, nullptr) ||
                size != actualSize) {
                mOk = false;
                return false;
            }
        }
        return true;
    }

    Path ReadWStr(int size) {
        Path path;
        if (size >= 0 && size % sizeof(wchar_t) == 0) {
            int chars = size / sizeof(wchar_t);
            path = Path(chars + 1);
            Read(path, size);
            path[chars] = L'\0';
        }
        return path;
    }

    bool ReadStatus() {
        int32_t status;
        return Read(&status, sizeof(status)) && status == TRUE;
    }

public:
    static HANDLE CreatePipe(Path *outName) {
        static int unique = 0;
        wchar_t pipeName[MAX_PATH];
        wsprintfW(pipeName, L"MyInputIfeoPipe_%d.%d.%d", GetCurrentProcessId(), time(nullptr), unique++);
        *outName = pipeName;

        HANDLE handle = CreateNamedPipeW(PathCombine(LR"(\\.\pipe)", pipeName), PIPE_ACCESS_DUPLEX,
                                         PIPE_TYPE_BYTE, 1, 0x1000, 0x1000, 0, nullptr);
        return handle != INVALID_HANDLE_VALUE ? handle : nullptr;
    }

    static HANDLE OpenPipe(const wchar_t *name) {
        HANDLE handle = CreateFileW(PathCombine(LR"(\\.\pipe)", name),
                                    GENERIC_READ | GENERIC_WRITE, 0,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        return handle != INVALID_HANDLE_VALUE ? handle : nullptr;
    }
};

class RegIfeoKeyDelegated : public RegIfeoKeyDelegatedBase {
    template <class TAction>
    bool Wrap(TAction &&action, bool create = true) {
        while (true) {
            if (!mHandle) {
                if (!create) {
                    return false;
                }

                mHandle = CreateDelegation();
                if (!mHandle) {
                    return false;
                }

                mOk = true;
                create = false;
            }

            bool status = action();
            if (mOk) {
                return status;
            }

            DestroyDelegation(mHandle);
            mHandle = nullptr;
        }

        ShowDelegationError();
        return false;
    }

protected:
    virtual HANDLE CreateDelegation() = 0;
    virtual void DestroyDelegation(HANDLE output) = 0;
    virtual void ShowDelegationError() = 0;
    virtual ~RegIfeoKeyDelegated() {}

public:
    bool RegisterExe(const wchar_t *exeName, const wchar_t *fullPath, const wchar_t *config) {
        return Wrap([&] {
            Package pkg = {PackageHeader, RegisterCmd, WStrSize(exeName), WStrSize(fullPath), WStrSize(config)};
            Write(&pkg, sizeof(pkg));
            Write(exeName, pkg.Args[0]);
            Write(fullPath, pkg.Args[1]);
            Write(config, pkg.Args[2]);
            return ReadStatus();
        });
    }

    bool UnregisterExe(const wchar_t *exeName, const wchar_t *fullPath) {
        return Wrap([&] {
            Package pkg = {PackageHeader, UnregisterCmd, WStrSize(exeName), WStrSize(fullPath)};
            Write(&pkg, sizeof(pkg));
            Write(exeName, pkg.Args[0]);
            Write(fullPath, pkg.Args[1]);
            return ReadStatus();
        });
    }

    void End() {
        Wrap([&] {
            Package pkg = {PackageHeader, EndCmd};
            Write(&pkg, sizeof(pkg));
            return true;
        },
             false);

        if (mHandle) {
            DestroyDelegation(mHandle);
        }
    }
};

class RegIfeoKeyDelegatedProcessor : public RegIfeoKeyDelegatedBase {
    RegIfeoKey &mTarget;

public:
    RegIfeoKeyDelegatedProcessor(RegIfeoKey &target, HANDLE handle) : mTarget(target) {
        SetHandle(handle);
    }

    bool Run() {
        Package pkg;
        while (Read(&pkg, sizeof(pkg)) &&
               pkg.Header == PackageHeader) {
            switch (pkg.Cmd) {
            case RegisterCmd: {
                Path exeName = ReadWStr(pkg.Args[0]);
                Path fullPath = ReadWStr(pkg.Args[1]);
                Path config = ReadWStr(pkg.Args[2]);
                WriteStatus(mTarget.RegisterExe(exeName, fullPath, config));
            } break;

            case UnregisterCmd: {
                Path exeName = ReadWStr(pkg.Args[0]);
                Path fullPath = ReadWStr(pkg.Args[1]);
                WriteStatus(mTarget.UnregisterExe(exeName, fullPath));
            } break;

            case EndCmd:
                return true;
            }
        }
        return false;
    }
};
