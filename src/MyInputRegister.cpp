#include "UtilsPath.h"
#include "UtilsStr.h"
#include "UtilsUi.h"
#include <Windows.h>

#define IFEO_KEY LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options)"
#define DEBUGGER_VALUE L"Debugger"
#define MYINPUT_INJECT_EXE L"myinput_inject.exe"

class RegKey {
    HKEY mKey = nullptr;
    bool mClose = false;

public:
    RegKey() {}
    RegKey(HKEY key) : mKey(key) {}

    RegKey(const RegKey &parent, const wchar_t *path, bool create = false) {
        if (create) {
            mClose = RegCreateKeyExW(parent.mKey, path, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &mKey, nullptr) == ERROR_SUCCESS;
        } else {
            mClose = RegOpenKeyExW(parent.mKey, path, 0, KEY_READ | KEY_WRITE, &mKey) == ERROR_SUCCESS;
        }
    }

    ~RegKey() {
        if (mClose) {
            RegCloseKey(mKey);
        }
    }

    bool Delete(const wchar_t *path) {
        return RegDeleteKeyExW(mKey, path, 0, 0) == ERROR_SUCCESS;
    }

    Path GetStringValue(const wchar_t *name) {
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

    bool IsEmpty() {
        DWORD numSubkeys = -1, numValues = -1;
        RegQueryInfoKeyW(mKey, nullptr, nullptr, nullptr, &numSubkeys, nullptr, nullptr, &numValues, nullptr, nullptr, nullptr, nullptr);
        return numSubkeys == 0 && numValues == 0;
    }

    vector<Path> GetChildKeys() {
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

RegKey *gIfeoKey;
Path gInjectExePath;

bool GetRegisteredExe(const wchar_t *exeName, Path *outPath, Path *outConfig) {
    RegKey exeKey(*gIfeoKey, exeName);
    bool isRegistered = false;
    if (exeKey) {
        Path exeDebugger = exeKey.GetStringValue(DEBUGGER_VALUE);
        if (exeDebugger) {
            int numArgs = 0;
            LPWSTR *args = CommandLineToArgvW(exeDebugger, &numArgs);

            if (args && numArgs) {
                *outPath = args[0];
                isRegistered = wcseq(args[0], gInjectExePath);

                for (int argI = 1; argI < numArgs; argI++) {
                    if (wcseq(args[argI], L"-c") && argI + 1 < numArgs) {
                        *outConfig = args[argI + 1], argI++;
                    }
                }
            }

            LocalFree(args);
        }
    }
    return isRegistered;
}

bool RegisterExe(const wchar_t *exeName, const wchar_t *config) {
    RegKey exeKey(*gIfeoKey, exeName, true);

    wstring cmdLine = L"\"" + wstring(gInjectExePath) + L"\" -r";
    if (config) {
        cmdLine += L" -c \"" + wstring(config) + L"\"";
    }

    if (!exeKey.SetStringValue(DEBUGGER_VALUE, cmdLine.c_str())) {
        Alert(L"Failed to set Debugger value in registry");
        return false;
    }

    return true;
}

bool UnregisterExe(const wchar_t *exeName) {
    RegKey exeKey(*gIfeoKey, exeName);
    if (exeKey) {
        if (!exeKey.DeleteValue(DEBUGGER_VALUE)) {
            Alert(L"Failed to delete Debugger value in registry");
            return false;
        }

        if (exeKey.IsEmpty() && !gIfeoKey->Delete(exeName)) {
            Alert(L"Failed to delete empty %ws key in registry", exeName);
            return false;
        }
    }
    return true;
}

struct RegisteredExe {
    Path Name;
    Path Config;
    bool Exact;
};

vector<RegisteredExe> GetRegisteredExes() {
    vector<RegisteredExe> exeNames;
    for (Path &exeName : gIfeoKey->GetChildKeys()) {
        Path regPath, regConfig;
        if (GetRegisteredExe(exeName, &regPath, &regConfig)) {
            exeNames.push_back({move(exeName), move(regConfig), true});
        } else if (regPath && wcsstr(regPath, MYINPUT_INJECT_EXE)) {
            exeNames.push_back({move(exeName), move(regConfig), false});
        }
    }
    return exeNames;
}

Path GetPath(const wchar_t *folder, const wchar_t *name, const wchar_t *ext) {
    Path rootPath = PathGetDirName(PathGetDirName(PathGetModulePath(nullptr)));
    Path extName = PathCombineExt(PathGetBaseNameWithoutExt(name), ext);
    return PathCombine(PathCombine(rootPath, folder), extName);
}

Path GetDefaultConfigPath() {
    return GetPath(L"Configs", L"_default", L"ini");
}

void OpenEditor(const Path &path) {
    ShellExecuteW(nullptr, nullptr, path, nullptr, nullptr, SW_SHOWNORMAL);
}

class RegisterUnregisterWindow : public Window {
    Label *mListLbl = nullptr;
    MultiListBox *mRegList = nullptr;
    Label *mRegLbl = nullptr;
    Button *mRegBtn = nullptr;
    Button *mUnregBtn = nullptr;
    Button *mReregBtn = nullptr;
    CheckBox *mCustomCfgChk = nullptr;
    EditLine *mCustomCfgEdit = nullptr;
    Button *mCustomCfgBtn = nullptr;
    Button *mEditCfgBtn = nullptr;
    Button *mEditDefCfgBtn = nullptr;
    Button *mViewLogsBtn = nullptr;

    vector<RegisteredExe> mRegisteredExes;
    bool mHasInexact = false;

    const wchar_t *InitialTitle() override { return L"Register Apps"; }
    SIZE InitialSize() override { return SIZE{500, 400}; }

    void UpdateList(const wchar_t *initialSel = nullptr) {
        mRegisteredExes = GetRegisteredExes();

        mRegList->Clear();

        vector<int> selection;
        for (int i = 0; i < (int)mRegisteredExes.size(); i++) {
            auto &regExe = mRegisteredExes[i];
            if (regExe.Exact) {
                mRegList->Add(regExe.Name);
            } else {
                mHasInexact = true;
                mRegList->Add((wstring(regExe.Name) + L" (*)").c_str());
            }

            if (initialSel && wcseq(regExe.Name, initialSel)) {
                selection.push_back(i);
            }
        }

        mRegList->SetSelected(selection);
        mRegLbl->Show(mHasInexact);
    }

    void OnCreate() override {
        mListLbl = Add<Label>(L"Registered Executables:");

        mRegList = Add<MultiListBox>([this](const vector<int> &indices) {
            bool anyExact = false;
            for (int idx : indices) {
                if (mRegisteredExes[idx].Exact) {
                    anyExact = true;
                }
            }

            bool anySelected = indices.size() > 0;
            mUnregBtn->Enable(anySelected);
            mReregBtn->Enable(anySelected && !anyExact);

            RegisteredExe *oneSelected = indices.size() == 1 ? &mRegisteredExes[indices.front()] : nullptr;
            mEditCfgBtn->Enable(oneSelected);
            mViewLogsBtn->Enable(oneSelected);
            mCustomCfgChk->Enable(oneSelected);

            bool hasConfig = oneSelected && oneSelected->Config;
            mCustomCfgChk->Set(hasConfig);
            mCustomCfgEdit->Enable(hasConfig);
            mCustomCfgEdit->Set(hasConfig ? oneSelected->Config.Get() : oneSelected ? PathGetBaseNameWithoutExt(oneSelected->Name).Get()
                                                                                    : L"");

            mCustomCfgBtn->Enable(false);
        });

        mRegLbl = Add<Label>(L"(*) - Registered to another instance");

        mRegBtn = Add<Button>(L"Register New", [this]() {
            Path selected = SelectFileForOpen(L"Executables\0*.EXE\0", L"Choose Executable", false);
            if (selected) {
                Path exeName = PathGetBaseName(selected);
                Path regValue, regConfig;
                if (GetRegisteredExe(exeName, &regValue, &regConfig)) {
                    Alert(L"%ws is already registered.", exeName.Get());
                } else if (!regValue || Question(L"%ws is registered to %ws\nRe-register it?", exeName.Get(), regValue.Get())) {
                    RegisterExe(exeName, regConfig);
                }

                UpdateList(exeName);
            }
        });

        mUnregBtn = Add<Button>(L"Unregister", [this]() {
            auto indices = mRegList->GetSelected();
            for (int idx : indices) {
                UnregisterExe(mRegisteredExes[idx].Name);
            }

            UpdateList();
        });

        mReregBtn = Add<Button>(L"Reregister", [this]() {
            auto indices = mRegList->GetSelected();
            for (int idx : indices) {
                RegisterExe(mRegisteredExes[idx].Name, mRegisteredExes[idx].Config);
            }

            UpdateList();
        });

        mCustomCfgChk = Add<CheckBox>(L"Use Config", [this](bool value) {
            mCustomCfgEdit->Enable(value);
            mCustomCfgBtn->Enable(true);
        });

        mCustomCfgEdit = Add<EditLine>([this]() {
            mCustomCfgBtn->Enable(true);
        });

        mCustomCfgBtn = Add<Button>(L"Apply", [this]() {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                int idx = indices.front();
                mRegisteredExes[idx].Config = mCustomCfgChk->Get() ? mCustomCfgEdit->Get() : Path();
                RegisterExe(mRegisteredExes[idx].Name, mRegisteredExes[idx].Config);
                mCustomCfgBtn->Enable(false);
            }
        });

        mEditCfgBtn = Add<Button>(L"Edit Config", [this]() {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                auto idx = indices.front();
                const Path &name = mRegisteredExes[idx].Name;
                const Path &config = mRegisteredExes[idx].Config;

                Path configPath = GetPath(L"Configs", config ? config : name, L"ini");
                if (GetFileAttributesW(configPath) == INVALID_FILE_ATTRIBUTES) {
                    if (!config && !Question(L"%ws currently uses the global config file - change it to use its own config file?", name.Get())) {
                        return;
                    }

                    CopyFileW(GetDefaultConfigPath(), configPath, false);
                }
                OpenEditor(configPath);
            }
        });

        mEditDefCfgBtn = Add<Button>(L"Edit Global Config", [this]() {
            OpenEditor(GetDefaultConfigPath());
        });

        mViewLogsBtn = Add<Button>(L"View Logs", [this]() {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                const Path &name = mRegisteredExes[indices.front()].Name;
                Path log = GetPath(L"Logs", name, L"log");
                if (GetFileAttributesW(log) == INVALID_FILE_ATTRIBUTES) {
                    Alert(L"%ws doesn't have any logs - wasn't run?", name.Get());
                } else {
                    OpenEditor(log);
                }
            }
        });

        UpdateList();
    }

    void OnResize(SIZE size) override {
        Layout layout(size, SIZE{10, 10});
        const int buttonWidth = 100;

        Layout cfgLayout = layout.SubLayout();
        layout.OnBottom(&cfgLayout, 25);
        cfgLayout.OnLeft(mEditCfgBtn, buttonWidth);
        cfgLayout.OnLeft(mViewLogsBtn, buttonWidth);
        cfgLayout.OnRight(mEditDefCfgBtn, buttonWidth);

        Layout custCfgLayout = layout.SubLayout();
        layout.OnBottom(&custCfgLayout, 25);
        custCfgLayout.OnLeft(mCustomCfgChk, 75);
        custCfgLayout.OnLeft(mCustomCfgEdit, 150);
        custCfgLayout.OnLeft(mCustomCfgBtn, buttonWidth);

        Layout regLayout = layout.SubLayout();
        layout.OnBottom(&regLayout, 25);
        regLayout.OnLeft(mUnregBtn, buttonWidth);
        regLayout.OnLeft(mReregBtn, buttonWidth);
        regLayout.OnRight(mRegBtn, buttonWidth);

        layout.OnTop(mListLbl, 15);
        layout.OnBottom(mRegLbl, 15);
        layout.OnRest(mRegList);
    }
};

void WindowRegisterUnregister() {
    RegisterUnregisterWindow window;
    window.Create();
    ProcessWindows();
}

void CmdLineRegisterUnregister(int numArgs, LPWSTR *args) {
    bool reg = false, unreg = false;
    Path exeName, config;

    for (int argI = 1; argI < numArgs; argI++) {
        if (wcseq(args[argI], L"-r")) {
            reg = true;
        } else if (wcseq(args[argI], L"-u")) {
            unreg = true;
        } else if (wcseq(args[argI], L"-c") && argI + 1 < numArgs) {
            config = args[++argI];
        } else if (!exeName && args[argI][0] != L'-') {
            exeName = PathGetBaseName(args[argI]);
        } else {
            Alert(L"Unrecognized option: %ws", args[argI]);
        }
    }

    if (!exeName) {
        Alert(L"No executable supplied with arguments");
        return;
    }

    if (!reg && !unreg) {
        Path regValue;
        if (GetRegisteredExe(exeName, &regValue, &config)) {
            unreg = Question(L"%ws is already registered\nUnregister it?", exeName.Get());
        } else if (regValue) {
            reg = Question(L"%ws is registered to %ws\nRe-register it?", exeName.Get(), regValue.Get());
        } else {
            reg = Question(L"Register %ws?", exeName.Get());
        }
    }

    int code = 2;
    if (reg) {
        code = RegisterExe(exeName, config) ? 0 : 1;
    } else if (unreg) {
        code = UnregisterExe(exeName) ? 0 : 1;
    }

    exit(code);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    gIfeoKey = new RegKey(RegKey(HKEY_LOCAL_MACHINE), IFEO_KEY, true);
    if (!*gIfeoKey) {
        Alert(L"Failed opening HKEY_LOCAL_MACHINE\\%ws for writing - no admin access?", IFEO_KEY);
        return 1;
    }

    gInjectExePath = PathCombine(PathGetDirName(PathGetModulePath(nullptr)), MYINPUT_INJECT_EXE);

    LPWSTR cmdLine = GetCommandLineW();

    int numArgs;
    LPWSTR *args = CommandLineToArgvW(cmdLine, &numArgs);

    if (numArgs > 1) {
        CmdLineRegisterUnregister(numArgs, args);
    } else {
        WindowRegisterUnregister();
    }

    return 0;
}