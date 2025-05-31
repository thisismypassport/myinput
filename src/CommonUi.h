#pragma once
#include "UtilsUi.h"
#include "ConfigEdit.h"

StockIcon gDelIcon{SIID_DELETE};
StockIcon gFolderIcon{SIID_FOLDER};
StockIcon gAppIcon{SIID_APPLICATION};

Path GetPath(const wchar_t *folder, const wchar_t *name, const wchar_t *ext) {
    Path rootPath = PathGetDirName(PathGetDirName(PathGetModulePath(nullptr)));
    Path extName = PathCombineExt(name, ext);
    return PathCombine(PathCombine(rootPath, folder), extName);
}

vector<Path> GetPathFiles(const wchar_t *folder, const wchar_t *ext) {
    vector<Path> files;
    Path path = GetPath(folder, L"*", ext);
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(path, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(PathGetBaseNameWithoutExt(data.cFileName));
            }
        } while (FindNextFileW(handle, &data));
        FindClose(handle);
    }
    std::stable_sort(files.begin(), files.end(),
                     [](const wchar_t *left, const wchar_t *right) { return tstricmp(left, right) < 0; });
    return files;
}

void OpenInShell(const Path &path, const Path &params = nullptr, const Path &workDir = nullptr) {
    ShellExecuteW(nullptr, nullptr, path, params, workDir, SW_SHOWNORMAL);
}

constexpr const wchar_t *ConfigsPath = L"Configs";
constexpr const wchar_t *ConfigsExt = L"ini";
constexpr const wchar_t *ConfigsDefault = L"_default";
constexpr const wchar_t *ConfigsByName = L"<by name>";
constexpr const wchar_t *LogsPath = L"Logs";
constexpr const wchar_t *LogsExt = L"log";

constexpr const wchar_t *ExecutablesFilter = L"Executables\0*.EXE\0";
constexpr const wchar_t *ConfigsFilter = L"MyInput Configs\0*.INI\0";

class ConfigNamesClass {
    vector<Path> mNames; // sorted
    int mSeq = 1;

public:
    int GetSeq() { return mSeq; }

    const vector<Path> &GetNames() {
        if (mNames.empty()) {
            mNames = GetPathFiles(ConfigsPath, ConfigsExt);
        }
        return mNames;
    }

    void Invalidate() {
        mSeq++;
        mNames.clear();
    }

    bool TryCreate(const wchar_t *name, const wchar_t *fromPath) {
        Path path = GetPath(ConfigsPath, name, ConfigsExt);

        if (!fromPath || !CopyFileW(fromPath, path, true)) {
            HANDLE handle = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                return false;
            }

            CloseHandle(handle);
        }

        Invalidate();
        return true;
    }

    bool TryDelete(const wchar_t *name) {
        if (!DeleteFileW(GetPath(ConfigsPath, name, ConfigsExt))) {
            return false;
        }

        Invalidate();
        return true;
    }

} ConfigNames;

Path NewConfigWindowCreate(const wchar_t *initialName, const wchar_t *initialSrcCfg);

#define CONFIG_CHOICE_ALLOW_NONE 0x0
#define CONFIG_CHOICE_ALLOW_NEW 0x1
#define CONFIG_CHOICE_ALLOW_DELETE 0x2
#define CONFIG_CHOICE_ALLOW_BY_NAME 0x4
#define CONFIG_CHOICE_ALLOW_RESIZE 0x8

class ConfigChoicePanel : public Panel {
    int mFlags = 0;
    int mConfigsSeq = 0;
    DropDownList *mDropDown = nullptr;
    Button *mNewButton = nullptr;
    Button *mDeleteButton = nullptr;

    function<void(Path &&)> mChanged;
    function<Path()> mGetDefaultNewName;

    void UpdateDropDown(const wchar_t *initial = nullptr, bool action = true) {
        mConfigsSeq = ConfigNames.GetSeq();

        Path oldSel = mDropDown->GetSelectedStr();
        mDropDown->Clear();

        if (mFlags & CONFIG_CHOICE_ALLOW_BY_NAME) {
            mDropDown->Add(ConfigsByName);
        }

        for (auto &name : ConfigNames.GetNames()) {
            mDropDown->Add(name);
        }

        if (initial) {
            mDropDown->SetSelectedStr(initial, action);
        } else {
            mDropDown->SetSelectedStr(oldSel, action);
        }
    }

public:
    ConfigChoicePanel(HWND parent, intptr_t id, int flags, function<void(Path &&)> changed = {}) : Panel(parent, id), mFlags(flags), mChanged(move(changed)) {}

    void SetDefaultNewName(function<Path()> &&getter) { mGetDefaultNewName = move(getter); }

    void SetOnOpen(function<void(bool)> &&open) { mDropDown->SetOnOpen(move(open)); }

    Control *OnCreate() override {
        mDropDown = New<DropDownList>([this](int idx) {
            if (mChanged) {
                mChanged(mDropDown->Get(idx));
            }

            if (mFlags & CONFIG_CHOICE_ALLOW_DELETE) {
                mDeleteButton->Enable(idx >= 0);
            }
        });

        auto layout = New<Layout>();

        if (mFlags & CONFIG_CHOICE_ALLOW_NEW) {
            mNewButton = New<Button>(L"New", [this] {
                Path initName = mGetDefaultNewName ? mGetDefaultNewName() : Path();
                Path currName = mDropDown->GetSelectedStr();
                Path newName = NewConfigWindowCreate(initName, currName);
                if (newName) {
                    UpdateDropDown(newName);
                }
            });
        }

        if (mFlags & CONFIG_CHOICE_ALLOW_DELETE) {
            mDeleteButton = New<Button>(L"Delete", &gDelIcon, [this] {
                Path currName = mDropDown->GetSelectedStr();
                if (currName && *currName &&
                    Confirm(L"Are you sure you want to permanently delete config '%ws'?", currName.Get()) &&
                    ConfigNames.TryDelete(currName)) {
                    UpdateDropDown();
                }
            });
        }

        if (mFlags & CONFIG_CHOICE_ALLOW_DELETE) {
            layout->AddRightMiddle(mDeleteButton);
        }
        if (mFlags & CONFIG_CHOICE_ALLOW_NEW) {
            layout->AddRightMiddle(mNewButton);
        }

        layout->AddLeftMiddle(mDropDown, (mFlags & CONFIG_CHOICE_ALLOW_RESIZE) ? Layout::Proportion(1.0) : 200);

        return layout;
    }

    bool HasSelected() {
        UpdateIfNeeded();
        return mDropDown->GetSelected() >= 0;
    }
    Path GetSelected() {
        UpdateIfNeeded();
        return mDropDown->GetSelectedStr();
    }
    void SetSelected(const wchar_t *sel, bool action = true) {
        UpdateIfNeeded();
        mDropDown->SetSelectedStr(sel, action);
    }

    void UpdateIfNeeded() {
        if (mConfigsSeq != ConfigNames.GetSeq()) {
            UpdateDropDown(nullptr, false);
        }
    }

    void OnActivate(bool activate, void *root, void *prev) override {
        if (activate) {
            UpdateIfNeeded();
        }
    }
};

class NewConfigWindow : public ModalDialog {
    enum class SourceType {
        Empty,
        Copy,
        Import
    };

    const wchar_t *mInitialName = nullptr;
    const wchar_t *mInitialSrcCfg = nullptr;
    EditLine *mNameEdit = nullptr;
    RadioGroup<SourceType> *mSourceRadio = nullptr;
    RadioBox *mEmptyRadio = nullptr;
    ConfigChoicePanel *mCopyConfig = nullptr;
    RadioBox *mCopyRadio = nullptr;
    Button *mImportBrowse = nullptr;
    EditLine *mImportEdit = nullptr;
    RadioBox *mImportRadio = nullptr;

    SourceType mSourceType = {};
    Path mResult;

public:
    Control *OnCreate() override {
        mNameEdit = New<EditLine>(mInitialName);

        mSourceRadio = New<RadioGroup<SourceType>>([this](SourceType choice) {
            mSourceType = choice;
            mCopyConfig->Enable(choice == SourceType::Copy);
            mImportBrowse->Enable(choice == SourceType::Import);
            mImportEdit->Enable(choice == SourceType::Import);
        });

        mEmptyRadio = New<RadioBox>(L"Create empty", mSourceRadio, SourceType::Empty);
        mCopyRadio = New<RadioBox>(L"Copy from:", mSourceRadio, SourceType::Copy);
        mImportRadio = New<RadioBox>(L"Import from:", mSourceRadio, SourceType::Import);

        mCopyConfig = New<ConfigChoicePanel>(CONFIG_CHOICE_ALLOW_NONE);
        mCopyConfig->SetSelected(mInitialSrcCfg);

        mImportBrowse = New<Button>(L"Select File...", [this] {
            Path path = SelectFileForOpen(ConfigsFilter, L"Import MyInput Config");
            if (path) {
                mImportEdit->Set(path);
            }
        });

        mImportEdit = New<EditLine>();

        Button *ok = New<Button>(L"OK", [this]() {
            Path newConfig = mNameEdit->Get();
            if (!newConfig || !*newConfig) {
                return Alert(L"Config name is empty");
            }

            Path fromPath = nullptr;
            switch (mSourceType) {
            case SourceType::Copy:
                fromPath = GetPath(ConfigsPath, mCopyConfig->GetSelected(), ConfigsExt);
                break;

            case SourceType::Import:
                fromPath = mImportEdit->Get();
                break;
            }

            if (!ConfigNames.TryCreate(newConfig, fromPath)) {
                return Alert(L"Config %ws already exists", newConfig.Get());
            }

            mResult = move(newConfig);
            Close();
        });
        Button *cancel = New<Button>(L"Cancel", [this]() { Close(); });

        mCopyRadio->Click();

        auto shortcuts = GetShortcuts();
        shortcuts->Add(ok, VK_RETURN);
        shortcuts->Add(cancel, VK_ESCAPE);

        auto copyLay = New<Layout>();
        copyLay->AddLeftMiddle(mCopyRadio);
        copyLay->AddLeftMiddle(mCopyConfig);

        auto importLay = New<Layout>();
        importLay->AddLeftMiddle(mImportRadio);
        importLay->AddLeftMiddle(mImportBrowse);
        importLay->AddRemaining(mImportEdit);

        auto btns = New<Layout>();
        btns->AddRight(cancel);
        btns->AddRight(ok);

        auto layout = New<Layout>(true);
        layout->AddTopLeft(New<Label>(L"New Config Name:"));
        layout->AddTop(mNameEdit);
        layout->AddTop(New<Separator>());
        layout->AddTopLeft(mEmptyRadio);
        layout->AddTop(copyLay);
        layout->AddTop(importLay);
        layout->AddBottom(btns);
        layout->AddBottom(New<Separator>());
        return layout;
    }

    Control *InitialFocus() override { return mNameEdit; }

    Path Create(const wchar_t *initialName, const wchar_t *initialSrcCfg) {
        mInitialName = initialName;
        mInitialSrcCfg = initialSrcCfg;
        ModalDialog::Create(L"New Config");
        return move(mResult);
    }
};

Path NewConfigWindowCreate(const wchar_t *initialName, const wchar_t *initialSrcCfg) {
    NewConfigWindow win;
    return win.Create(initialName, initialSrcCfg);
}

void AddPluginNodesToTree(TreeView *tree) {
    for (auto &plugin : GPlugins.Plugins) {
        auto node = tree->Add(nullptr, (L"Plugin '" + ToStdWStr(plugin.Name) + L"' (" + plugin.Description + L")").c_str());
        plugin.TempUserData = node;
    }
}

struct BaseUiIntf {
    virtual void SwitchToConfigs() = 0;
};

struct ExeUiIntf {
    virtual Path GetConfig() = 0;
    virtual void ResetSelection() = 0;
};

struct ConfigUiIntf {
    virtual Path GetConfig() = 0;
    virtual void SetConfig(const Path &path) = 0;
};

class ConfigPanel;
struct ConfigEditEntry *GetConfigEntry(ConfigPanel *panel);
template <typename TOp>
void ChangeConfigEntry(ConfigPanel *panel, TOp &&op);
