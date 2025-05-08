#pragma once
#include "CommonUi.h"
#include "Registry.h"
#include "WinUtils.h"
#include "Link.h"

RegIfeoKeyDelegatedViaNamedPipe *GetDelegatedIfeo() {
    static RegIfeoKeyDelegatedViaNamedPipe gDelegatedIfeo;
    return &gDelegatedIfeo;
}

class ExePanel : public Panel, public ExeUiIntf {
    class ConfigChoicePopup : public OverlayWindow {
        ExePanel *mExePanel;
        int mCurrentIdx = -1;

        RECT GetExpectedRect() {
            return mExePanel->mRegList->GetRect(mCurrentIdx, ListConfigColumn);
        }

    public:
        ConfigChoicePopup(HWND parent, intptr_t id, ExePanel *exePanel) : OverlayWindow(parent, id), mExePanel(exePanel) {}

        void Create(int idx) {
            mCurrentIdx = idx;
            OverlayWindow::Create(GetExpectedRect());
        }

        void OnDestroy() {
            mCurrentIdx = -1;
        }

        void Update(int idx) {
            if (idx == mCurrentIdx) {
                SetRect(GetExpectedRect());
            } else {
                Destroy();
            }
        }

        Control *OnCreate() override {
            return mExePanel->CreateConfigChoice(mExePanel, this, true);
        }
    };

    BaseUiIntf *mBaseUiIntf;
    UniquePtr<RegIfeoKey> mIfeoKey;
    UniquePtr<RegDisabledIfeoKey> mDisabledIfeoKey;

    Label *mListLbl = nullptr;
    MultiListView *mRegList = nullptr;
    Label *mRegLbl = nullptr;
    Button *mRegBtn = nullptr;
    Button *mRegDisabledBtn = nullptr;
    Button *mUnregBtn = nullptr;
    Button *mReregBtn = nullptr;
    Button *mEditCfgBtn = nullptr;
    Button *mViewLogsBtn = nullptr;
    Button *mOpenFolderBtn = nullptr;
    Button *mRunBtn = nullptr;
    Label *mConfigLabel = nullptr;
    ConfigChoicePanel *mConfigChoice = nullptr;
    ConfigChoicePopup *mConfigPopup = nullptr;
    PopupMenu *mPopupMenu = nullptr;

    vector<RegisteredExe> mRegisteredExes;
    bool mHasInexact = false;

    enum ListColumn {
        ListNameColumn,
        ListConfigColumn,
    };

    RegIfeoKeyMaybeDelegated *GetIfeoKeyForWrite(bool disabled) {
        if (disabled) {
            return mDisabledIfeoKey;
        } else {
            return GetDelegatedIfeo();
        }
    }

    Path CreateLaunchCmd(const Path &path, const Path &config) {
        wstring cmd;
        if (config) {
            cmd += L"-c \"" + wstring(config) + L"\" ";
        }
        cmd += L"\"" + wstring(path) + L"\"";
        return cmd.c_str();
    }

    Path &ExeIdentifier(RegisteredExe &exe) {
        return exe.FullPath ? exe.FullPath : exe.Name;
    }

    void UpdateList(const vector<Path> *initialPaths = nullptr, bool forPopup = false) {
        vector<Path> prevSelPaths;
        if (!initialPaths) {
            for (auto &idx : mRegList->GetSelected()) {
                prevSelPaths.push_back(move(ExeIdentifier(mRegisteredExes[idx]))); // can move, as we clear below
            }
        }
        if (!initialPaths) {
            initialPaths = &prevSelPaths;
        }

        mRegisteredExes.clear();
        mIfeoKey->GetRegisteredExesInto(mRegisteredExes);
        mDisabledIfeoKey->GetRegisteredExesInto(mRegisteredExes);

        std::stable_sort(mRegisteredExes.begin(), mRegisteredExes.end(), [](const auto &left, const auto &right) {
            int cmp = tstricmp(left.Name, right.Name);
            if (cmp == 0 && left.FullPath && right.FullPath) {
                cmp = tstricmp(left.FullPath, right.FullPath);
            }
            return cmp < 0;
        });

        mRegList->Clear();

        unordered_map<wstring_view, int> nameDupCount;
        for (auto &exe : mRegisteredExes) {
            nameDupCount[exe.Name.Get()]++;
        }

        vector<int> selection;
        for (int i = 0; i < (int)mRegisteredExes.size(); i++) {
            auto &regExe = mRegisteredExes[i];
            wstring label = PathGetBaseNameWithoutExt(regExe.Name).Get();

            if (nameDupCount[regExe.Name.Get()] > 1 && regExe.FullPath) {
                label = label + L" (" + PathGetDirName(regExe.FullPath).Get() + L")";
            }

            if (!regExe.FullPath) {
                label = label + L" (any path)";
            }

            if (!regExe.Exact) {
                mHasInexact = true;
                label += L" [#]";
            }

            mRegList->Add(label.c_str());

            mRegList->Set(i, ListConfigColumn,
                          regExe.Config ? regExe.Config.Get() : ConfigsByName);

            for (auto &initialPath : *initialPaths) {
                if (tstreq(ExeIdentifier(regExe), initialPath)) {
                    selection.push_back(i);
                }
            }

            mRegList->SetChecked(i, !regExe.Disabled, false);
        }

        mRegList->SetSelected(selection);
        if (!selection.empty()) {
            mRegList->EnsureVisible(selection[0]);
        }

        mRegLbl->Show(mHasInexact);

        if (!forPopup) {
            mConfigPopup->Destroy();
        }
    }

    void UpdateList(Path &&initialPath) {
        vector<Path> selectedVec;
        selectedVec.push_back(move(initialPath));
        UpdateList(&selectedVec);
    }

    void OnFilesDrop(const vector<Path> &files) override {
        for (auto &file : files) {
            RegisterNew(file);
        }

        UpdateList(&files);
    }

    void RegisterNew(const Path &path, bool disabled = false) {
        Path exeName = PathGetBaseName(path);
        Path fullPath = PathGetFullPath(path);

        if (fullPath && tstrieq(PathGetExtPtr(fullPath), L"lnk") &&
            ResolveLink(fullPath, &fullPath)) {
            exeName = PathGetBaseName(fullPath);
        }

        if (!IsFileExists(fullPath)) {
            if (tstreq(PathGetExtPtr(exeName), L"")) {
                exeName = PathCombineExt(exeName, L"exe");
            }

            if (!Question(L"Register all executables named %ws?", exeName.Get())) {
                return;
            }
            fullPath = nullptr; // by name only
        }

        RegisteredExeInfo regInfo;
        bool registered = mIfeoKey->GetRegisteredExe(exeName, fullPath, &regInfo, true);
        bool registeredDisabled = mDisabledIfeoKey->GetRegisteredExe(exeName, fullPath, &regInfo, true);
        if (disabled ? registeredDisabled : registered) {
            auto existingName = regInfo.ByPath ? fullPath.Get() : exeName.Get();
            if (regInfo.Exact) {
                return Alert(L"%ws is already in the list.", existingName);
            } else if (!Question(L"%ws is registered to %ws\nRe-register it?", existingName, regInfo.InjectExe.Get())) {
                return;
            }
        } else if (registeredDisabled || registered) {
            GetIfeoKeyForWrite(!disabled)->UnregisterExe(exeName, fullPath);
        }

        if (!regInfo.Config) {
            regInfo.Config = ConfigsDefault; // leaving it empty gives legacy/questionable default behaviour
        }

        if (!GetIfeoKeyForWrite(disabled)->RegisterExe(exeName, fullPath, regInfo.Config)) {
            GetIfeoKeyForWrite(true)->RegisterExe(exeName, fullPath, regInfo.Config);
        }
    }

    static ConfigChoicePanel *CreateConfigChoice(ExePanel *self, PanelBase *parent, bool forPopup) {
        auto panel = parent->New<ConfigChoicePanel>(CONFIG_CHOICE_ALLOW_NEW | CONFIG_CHOICE_ALLOW_BY_NAME |
                                                        (forPopup ? CONFIG_CHOICE_ALLOW_RESIZE : 0),
                                                    [self, forPopup](Path &&config) {
                                                        bool hasConfig = config && !tstreq(config, ConfigsByName);

                                                        auto indices = self->mRegList->GetSelected();
                                                        for (int idx : indices) {
                                                            auto &exe = self->mRegisteredExes[idx];
                                                            exe.Config = hasConfig ? move(config) : Path();
                                                            self->GetIfeoKeyForWrite(exe.Disabled)->RegisterExe(exe.Name, exe.FullPath, exe.Config);
                                                        }

                                                        self->UpdateList(nullptr, forPopup);
                                                        if (forPopup) {
                                                            self->mConfigChoice->UpdateIfNeeded();
                                                        }
                                                    });

        panel->SetDefaultNewName([self]() -> Path {
            auto indices = self->mRegList->GetSelected();
            return indices.empty() ? Path() : PathGetBaseNameWithoutExt(self->mRegisteredExes[indices.front()].Name);
        });

        if (forPopup) {
            auto indices = self->mRegList->GetSelected();
            if (indices.size() == 1) {
                auto &config = self->mRegisteredExes[indices.front()].Config;
                panel->SetSelected(config ? config.Get() : ConfigsByName, false);
            }
        }

        return panel;
    }

    Control *OnCreate() override {
        mIfeoKey = UniquePtr<RegIfeoKey>::New(RegMode::Read);
        mDisabledIfeoKey = UniquePtr<RegDisabledIfeoKey>::New(RegMode::CreateOrEdit);

        mListLbl = New<Label>(L"Registered Executables:");

        mRegList = New<MultiListView>([this](const vector<int> &indices) {
            bool anyExact = false;
            bool mixedConfigs = false;
            const wchar_t *anyConfig = nullptr;
            for (int idx : indices) {
                auto &exe = mRegisteredExes[idx];
                if (exe.Exact) {
                    anyExact = true;
                }

                const wchar_t *exeConfig = exe.Config ? exe.Config.Get() : ConfigsByName;
                if (!anyConfig) {
                    anyConfig = exeConfig;
                } else if (!mixedConfigs && !tstrieq(anyConfig, exeConfig)) {
                    mixedConfigs = true;
                }
            }

            bool anySelected = indices.size() > 0;
            mUnregBtn->Enable(anySelected);
            mReregBtn->Show(anySelected && !anyExact);

            RegisteredExe *oneSelected = indices.size() == 1 ? &mRegisteredExes[indices.front()] : nullptr;
            mEditCfgBtn->Enable(oneSelected);
            mViewLogsBtn->Enable(oneSelected);
            mOpenFolderBtn->Enable(oneSelected);
            mRunBtn->Enable(oneSelected);

            mConfigLabel->Enable(anySelected);
            mConfigChoice->Enable(anySelected);
            mConfigChoice->SetSelected(mixedConfigs ? nullptr : anyConfig, false);
        });

        mRegList->AddColumn(L"Name");
        mRegList->AddColumn(L"Config", 200);

        mRegList->SetOnClick([this](int idx, int colIdx, bool right, bool dblclk) {
            mConfigPopup->Destroy();

            if (colIdx == ListNameColumn && right) {
                mPopupMenu->Create();
            } else if (colIdx == ListNameColumn && !right && dblclk) {
                mEditCfgBtn->Click();
            } else if (colIdx == ListConfigColumn) {
                mConfigPopup->Create(idx);
            }
        });

        mRegList->SetOnKey([this](int key) {
            if (key == VK_RETURN) {
                mEditCfgBtn->Click();
            } else if (key == VK_DELETE) {
                auto indices = mRegList->GetSelected();
                if (indices.size() == 1 ? Confirm(L"Are you sure you want to unregister '%ws'?", mRegisteredExes[indices[0]].Name.Get()) : indices.size() > 1 ? Confirm(L"Are you sure you want to unregister these %d executables?", indices.size())
                                                                                                                                                              : false) {
                    mUnregBtn->Click();
                }
            }
        });

        mRegList->SetOnRectChanged([this]() {
            if (mConfigPopup->IsCreated()) {
                auto selected = mRegList->GetSelected();
                mConfigPopup->Update(selected.size() == 1 ? selected.front() : -1);
            }
        });

        mRegList->SetOnCheck([this](int idx, bool checked) {
            auto &exe = mRegisteredExes[idx];
            if (exe.Disabled != !checked &&
                GetIfeoKeyForWrite(exe.Disabled)->UnregisterExe(exe.Name, exe.FullPath)) {
                exe.Disabled = !exe.Disabled;
                if (!GetIfeoKeyForWrite(exe.Disabled)->RegisterExe(exe.Name, exe.FullPath, exe.Config)) {
                    exe.Disabled = !exe.Disabled;
                    GetIfeoKeyForWrite(exe.Disabled)->RegisterExe(exe.Name, exe.FullPath, exe.Config);
                    mRegList->SetChecked(idx, false, false);
                }
            }
        });

        mRegLbl = New<Label>(L"[#] - Registered to another version of myinput");

        mUnregBtn = New<Button>(L"Unregister", &gDelIcon, [this] {
            auto indices = mRegList->GetSelected();
            for (int idx : indices) {
                auto &exe = mRegisteredExes[idx];
                GetIfeoKeyForWrite(exe.Disabled)->UnregisterExe(exe.Name, exe.FullPath);
            }

            UpdateList();
        });

        mReregBtn = New<Button>(L"Reregister to This Version", [this] {
            auto indices = mRegList->GetSelected();
            for (int idx : indices) {
                auto &exe = mRegisteredExes[idx];
                GetIfeoKeyForWrite(exe.Disabled)->RegisterExe(exe.Name, exe.FullPath, exe.Config);
            }

            UpdateList();
        });

        mRegBtn = New<Button>(L"Register New", [this] {
            Path selected = SelectFileForOpen(L"Executables\0*.EXE\0", L"Register Executable", false);
            if (selected) {
                RegisterNew(selected);
                UpdateList(move(selected));
            }
        });

        mRegDisabledBtn = New<Button>(L"Add without Registering", [this] {
            Path selected = SelectFileForOpen(L"Executables\0*.EXE\0", L"Add Executable (without Registering)", false);
            if (selected) {
                RegisterNew(selected, true);
                UpdateList(move(selected));
            }
        });

        mEditCfgBtn = New<Button>(L"Edit \r\nConfig", [this] {
            if (!mRegList->GetSelected().empty()) {
                mBaseUiIntf->SwitchToConfigs();
            }
        });

        mViewLogsBtn = New<Button>(L"View \r\nLogs", [this] {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                Path baseName = PathGetBaseNameWithoutExt(mRegisteredExes[indices.front()].Name);
                Path log = GetPath(LogsPath, baseName, LogsExt);
                if (!IsFileExists(log)) {
                    Alert(L"%ws doesn't have any logs - wasn't run?", baseName.Get());
                } else {
                    OpenInShell(log);
                }
            }
        });

        mRunBtn = New<Button>(L"Run with \r\nMyInput", &gAppIcon, [this] {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                auto idx = indices.front();
                const Path &path = mRegisteredExes[idx].FullPath;
                if (!path) {
                    Alert(L"%ws has no location defined", mRegisteredExes[idx].Name.Get());
                } else if (!IsFileExists(path)) {
                    Alert(L"%ws doesn't exist", path.Get());
                } else {
                    OpenInShell(mDisabledIfeoKey->GetInjectExePath(), CreateLaunchCmd(path, mRegisteredExes[idx].Config));
                }
            }
        });

        mOpenFolderBtn = New<Button>(L"Open \r\nFolder", &gFolderIcon, [this] {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                auto idx = indices.front();
                const Path &path = mRegisteredExes[idx].FullPath;
                Path folderPath = path ? PathGetDirName(path) : nullptr;
                if (!folderPath) {
                    Alert(L"%ws has no location defined", mRegisteredExes[idx].Name.Get());
                } else if (!IsFileExists(folderPath)) {
                    Alert(L"%ws doesn't exist", folderPath.Get());
                } else {
                    OpenInShell(folderPath);
                }
            }
        });

        mConfigLabel = New<Label>(L"Use Config:");
        mConfigChoice = CreateConfigChoice(this, this, false);
        mConfigPopup = New<ConfigChoicePopup>(this);

        mPopupMenu = New<PopupMenu>([this](PopupMenu *pop) {
            pop->Add(mEditCfgBtn, true);
            pop->Add(mViewLogsBtn);
            pop->AddSep();
            pop->Add(mRunBtn);
            pop->Add(mOpenFolderBtn);
            pop->AddSep();
            pop->Add(mUnregBtn);
            pop->Add(mReregBtn);
        });

        UpdateList();

        EnableFilesDrop();

        auto headerLayout = New<Layout>();
        headerLayout->AddLeft(mListLbl);
        headerLayout->AddRight(mRegLbl);

        auto cfgLayout = New<Layout>();
        cfgLayout->AddLeftMiddle(mConfigLabel);
        cfgLayout->AddLeftMiddle(mConfigChoice);

        auto subLayout = New<Layout>();
        subLayout->AddLeftMiddle(mEditCfgBtn);
        subLayout->AddLeftMiddle(mViewLogsBtn);
        subLayout->AddLeftMiddle(mRunBtn);
        subLayout->AddLeftMiddle(mOpenFolderBtn);
        subLayout->AddRightMiddle(mRegDisabledBtn);

        auto regLayout = New<Layout>();
        regLayout->AddLeftMiddle(mUnregBtn);
        regLayout->AddLeftMiddle(mReregBtn);
        regLayout->AddRightMiddle(mRegBtn);

        auto layout = New<Layout>(true);
        layout->AddTop(headerLayout);
        layout->AddBottom(cfgLayout);
        layout->AddBottom(subLayout);
        layout->AddBottom(regLayout);
        layout->AddRemaining(mRegList);
        return layout;
    }

    void OnActivate(bool activate, void *root, void *prev) override {
        if (!activate && prev != mConfigPopup) {
            mConfigPopup->Destroy();
        }
    }

public:
    ExePanel(HWND parent, intptr_t id, BaseUiIntf *baseUi) : Panel(parent, id), mBaseUiIntf(baseUi) {}

    Path GetConfig() override {
        auto indices = mRegList->GetSelected();
        if (indices.size() == 1) {
            auto idx = indices.front();
            const Path &name = mRegisteredExes[idx].Name;
            const Path &config = mRegisteredExes[idx].Config;

            if (config) {
                return config.Copy();
            }

            Path baseName = PathGetBaseNameWithoutExt(name);
            Path configPath = GetPath(ConfigsPath, baseName, ConfigsExt);
            if (IsFileExists(configPath)) {
                return baseName;
            } else {
                return ConfigsDefault;
            }
        } else {
            if (indices.size() > 1) {
                return ConfigsDefault;
            } else {
                return nullptr;
            }
        }
    }

    Control *InitialFocus() override { return mRegList; }

    void ResetSelection() override {
        mRegList->SetSelected({});
    }
};
