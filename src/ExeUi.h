#pragma once
#include "ExeConfig.h"
#include "UtilsUi.h"

Path GetPath(const wchar_t *folder, const wchar_t *name, const wchar_t *ext) {
    Path rootPath = PathGetDirName(PathGetDirName(PathGetModulePath(nullptr)));
    Path extName = PathCombineExt(PathGetBaseNameWithoutExt(name), ext);
    return PathCombine(PathCombine(rootPath, folder), extName);
}

Path GetDefaultConfigPath() {
    return GetPath(L"Configs", L"_default", L"ini");
}

void OpenInShell(const Path &path, const Path &params = nullptr) {
    ShellExecuteW(nullptr, nullptr, path, params, nullptr, SW_SHOWNORMAL);
}

class ExePanel : public Panel {
    UniquePtr<RegIfeoKey> mIfeoKey;
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
    Button *mLaunchBtn = nullptr;
    Button *mLaunchNewBtn = nullptr;

    vector<RegisteredExe> mRegisteredExes;
    bool mHasInexact = false;

    void UpdateList(const wchar_t *initialSel = nullptr) {
        mRegisteredExes = mIfeoKey->GetRegisteredExes();

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

            if (!regExe.Exact) {
                mHasInexact = true;
                label += L" [#]";
            }

            mRegList->Add(label.c_str());

            if (initialSel && tstreq(regExe.Name, initialSel)) {
                selection.push_back(i);
            }
        }

        mRegList->SetSelected(selection);
        mRegLbl->Show(mHasInexact);
    }

    IControl *OnCreate() override {
        mIfeoKey = UniquePtr<RegIfeoKey>::New(RegMode::Read);

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
            mLaunchBtn->Enable(oneSelected);
            mCustomCfgChk->Enable(oneSelected);

            bool hasConfig = oneSelected && oneSelected->Config;
            mCustomCfgChk->Set(hasConfig);
            mCustomCfgEdit->Enable(hasConfig);
            mCustomCfgEdit->Set(hasConfig ? oneSelected->Config.Get() : oneSelected ? PathGetBaseNameWithoutExt(oneSelected->Name).Get()
                                                                                    : L"");

            mCustomCfgBtn->Enable(false);
        });

        mRegLbl = Add<Label>(L"[#] - Registered to another version of myinput");

        mRegBtn = Add<Button>(L"Register New", [this] {
            Path selected = SelectFileForOpen(L"Executables\0*.EXE\0", L"Choose Executable", false);
            if (selected) {
                Path exeName = PathGetBaseName(selected);
                Path fullPath = PathGetFullPath(selected);

                RegisteredExeInfo regInfo;
                bool registered = mIfeoKey->GetRegisteredExe(exeName, fullPath, &regInfo, true);
                if (registered) {
                    auto existingName = regInfo.ByPath ? fullPath.Get() : exeName.Get();
                    if (regInfo.Exact) {
                        Alert(L"%ws is already registered.", existingName);
                    } else if (Question(L"%ws is registered to %ws\nRe-register it?", existingName, regInfo.InjectExe.Get())) {
                        registered = false;
                    }
                }

                if (!registered) {
                    GetDelegatedIfeo()->RegisterExe(exeName, fullPath, regInfo.Config);
                }

                UpdateList(exeName);
            }
        });

        mUnregBtn = Add<Button>(L"Unregister", [this] {
            auto indices = mRegList->GetSelected();
            for (int idx : indices) {
                auto &exe = mRegisteredExes[idx];
                GetDelegatedIfeo()->UnregisterExe(exe.Name, exe.FullPath);
            }

            UpdateList();
        });

        mReregBtn = Add<Button>(L"Reregister", [this] {
            auto indices = mRegList->GetSelected();
            for (int idx : indices) {
                auto &exe = mRegisteredExes[idx];
                GetDelegatedIfeo()->RegisterExe(exe.Name, exe.FullPath, exe.Config);
            }

            UpdateList();
        });

        mCustomCfgChk = Add<CheckBox>(L"Use Config", [this](bool value) {
            mCustomCfgEdit->Enable(value);
            mCustomCfgBtn->Enable(true);
        });

        mCustomCfgEdit = Add<EditLine>([this](bool done) {
            mCustomCfgBtn->Enable(true);
        });

        mCustomCfgBtn = Add<Button>(L"Apply", [this] {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                auto &exe = mRegisteredExes[indices.front()];
                exe.Config = mCustomCfgChk->Get() ? mCustomCfgEdit->Get() : Path();
                GetDelegatedIfeo()->RegisterExe(exe.Name, exe.FullPath, exe.Config);
                mCustomCfgBtn->Enable(false);
            }
        });

        mEditCfgBtn = Add<Button>(L"Edit Config", [this] {
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
                OpenInShell(configPath);
            }
        });

        mEditDefCfgBtn = Add<Button>(L"Edit Global Config", [this] {
            OpenInShell(GetDefaultConfigPath());
        });

        mViewLogsBtn = Add<Button>(L"View Logs", [this] {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                const Path &name = mRegisteredExes[indices.front()].Name;
                Path log = GetPath(L"Logs", name, L"log");
                if (GetFileAttributesW(log) == INVALID_FILE_ATTRIBUTES) {
                    Alert(L"%ws doesn't have any logs - wasn't run?", name.Get());
                } else {
                    OpenInShell(log);
                }
            }
        });

        mLaunchBtn = Add<Button>(L"Launch", [this] {
            auto indices = mRegList->GetSelected();
            if (!indices.empty()) {
                auto idx = indices.front();
                const Path &path = mRegisteredExes[idx].FullPath;
                if (!path) {
                    Alert(L"%ws has no location defined", mRegisteredExes[idx].Name.Get());
                } else if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
                    Alert(L"%ws doesn't exist", path.Get());
                } else {
                    OpenInShell(path);
                }
            }
        });

        mLaunchNewBtn = Add<Button>(L"Launch New (One-Time)", [this] {
            Path selected = SelectFileForOpen(L"Executables\0*.EXE\0", L"Execute");
            if (selected) {
                Path injectPath = PathCombine(PathGetDirName(PathGetModulePath(nullptr)), L"myinput_inject.exe");
                wstring launchCmd = L"\"" + wstring(selected.Get()) + L"\"";
                OpenInShell(injectPath, launchCmd.c_str());
            }
        });

        UpdateList();

        auto layout = Add<Layout>(true);

        auto cfgLayout = Add<Layout>();
        layout->OnBottom(cfgLayout);
        cfgLayout->OnLeft(mEditCfgBtn);
        cfgLayout->OnLeft(mViewLogsBtn);
        cfgLayout->OnLeft(mLaunchBtn);
        cfgLayout->OnRight(mLaunchNewBtn);
        cfgLayout->OnRight(mEditDefCfgBtn);

        auto custCfgLayout = Add<Layout>();
        layout->OnBottom(custCfgLayout);
        custCfgLayout->OnLeft(mCustomCfgChk);
        custCfgLayout->OnLeft(mCustomCfgEdit, 150);
        custCfgLayout->OnLeft(mCustomCfgBtn);

        auto regLayout = Add<Layout>();
        layout->OnBottom(regLayout);
        regLayout->OnLeft(mUnregBtn);
        regLayout->OnLeft(mReregBtn);
        regLayout->OnRight(mRegBtn);

        layout->OnTop(mListLbl);
        layout->OnBottom(mRegLbl);
        layout->OnRest(mRegList);
        return layout;
    }

public:
    using Panel::Panel;
};
