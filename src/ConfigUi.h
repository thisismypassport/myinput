#pragma once
#include "CommonUi.h"
#include "ConfigVarUi.h"
#include "ConfigKeyUi.h"

// #define CONFIG_NO_SAVE

struct ConfigChg {
    enum ChangeType { None,
                      Add,
                      Remove,
                      Change,
                      Toggle,
                      Swap };

    ChangeType Type = ChangeType::None;
    ConfigEditEntry *EntryPtr = nullptr;
    UniquePtr<ConfigEditEntry> EntryStore;
    ConfigEditGroup *Parent = nullptr;
    ConfigEditEntry *Prev = nullptr;
    bool Locked = false;
};

class ConfigPanel : public Panel, public ConfigUiIntf {
    ExeUiIntf *mExeUiIntf;
    Timer *mAutoSaveTimer = nullptr;
    Button *mUndoButton = nullptr;
    Button *mRedoButton = nullptr;
    ConfigChoicePanel *mConfigChoice = nullptr;
    TreeView *mConfigTree = nullptr;
    Dynamic *mConfigDynamic = nullptr;
    SplitButton *mNewMappingBtn = nullptr;
    Button *mNewVariableBtn = nullptr;
    Button *mNewGroupBtn = nullptr;
    PopupMenu *mNewPopupMenu = nullptr;
    Button *mDeleteBtn = nullptr;
    UpDownButtons *mUpDownBtn = nullptr;
    Button *mEditTextBtn = nullptr;
    PopupMenu *mEntryPopup = nullptr;

    ConfigErrorUiPanel *mErrorUiPanel = nullptr;
    ConfigParentUiPanel *mParentUiPanel = nullptr;
    ConfigVariableUiPanel *mVariableUiPanel = nullptr;
    ConfigMappingUiPanel *mMappingUiPanel = nullptr;

    Path mCurrConfig;
    UniquePtr<ConfigEdit> mCurrEdit;
    ConfigEditEntry *mCurrEntry = nullptr;
    std::deque<ConfigChg> mChanges;
    int mRedoDepth = 0;
    bool mFromActivate = false;
    bool mSaveNeeded = false;
    uint64_t mLastChangeTimestamp = 0;

    uint64_t GetTimestamp() {
        if (!mCurrConfig) {
            return 0;
        }

        Path path = GetPath(ConfigsPath, mCurrConfig, ConfigsExt);
        return GetFileLastWriteTime(path);
    }

    void SaveIfNeeded() {
        if (mSaveNeeded && mCurrConfig) {
#ifndef CONFIG_NO_SAVE
            ConfigEditSave(GetPath(ConfigsPath, mCurrConfig, ConfigsExt), mCurrEdit);
#endif
            mSaveNeeded = false;
        }
    }

    void UpdateUndoRedoButtons() {
        mUndoButton->Enable(mChanges.size() - mRedoDepth > 0);
        mRedoButton->Enable(mRedoDepth > 0);
    }

    void AddChange(ConfigChg::ChangeType type, ConfigEditEntry *entryPtr, UniquePtr<ConfigEditEntry> &&entryStore = nullptr,
                   ConfigEditGroup *parent = nullptr, ConfigEditEntry *prev = nullptr) {
        while (mRedoDepth > 0) {
            mChanges.pop_back();
            mRedoDepth--;
        }

        if (mChanges.size() >= 100) {
            mChanges.pop_front();
        }

        mChanges.emplace_back(type, entryPtr, move(entryStore), parent, prev);
        UpdateUndoRedoButtons();
        mSaveNeeded = true;
    }

    void RepeatChange(ConfigChg &chg) {
        ConfigEditEntry *entry = chg.EntryPtr;
        chg.Locked = true;

        switch (chg.Type) {
        case ConfigChg::Add:
            entry = InsertEntry(move(chg.EntryStore), chg.Parent, chg.Prev);
            chg.EntryPtr = entry;
            chg.Parent = nullptr;
            chg.Type = ConfigChg::Remove;
            break;

        case ConfigChg::Remove:
            chg.EntryStore = RemoveEntry(entry, &chg.Parent, &chg.Prev);
            chg.Type = ConfigChg::Add;
            entry = chg.Prev ? chg.Prev : chg.Parent->Start;
            break;

        case ConfigChg::Change:
            chg.EntryStore->SwapStateWith(entry);
            mConfigTree->Set(GetNode(entry), GetEntryText(entry));
            mConfigTree->SetSelected(GetNode(entry));
            break;

        case ConfigChg::Toggle:
            ToggleEntryEnabled(entry);
            mConfigTree->SetPartialChecked(GetNode(entry), GetEntryChecked(entry), false);
            break;

        case ConfigChg::Swap:
            SwapEntry(entry, &chg.Parent, &chg.Prev);
            break;
        }

        mConfigTree->EnsureVisible(GetNode(entry));
    }

    ConfigEditEntry *GetEntry(TreeNode node) {
        return (ConfigEditEntry *)mConfigTree->GetData(node);
    }
    TreeNode GetNode(ConfigEditEntry *entry) {
        return entry ? (TreeNode)entry->UserData : nullptr;
    }
    ConfigEditEntry *GetPrev(ConfigEditEntry *entry) {
        return GetEntry(mConfigTree->GetPrevSibling(GetNode(entry)));
    }

    Path GetEntryText(ConfigEditEntry *entry) {
        return PathFromStr(entry->RawText.c_str() + (entry->Enabled ? 0 : 1)); // skip '#'
    }

    MaybeBool GetEntryChecked(ConfigEditEntry *entry) {
        return entry->Enabled ? (entry->ParentsEnabled ? MaybeBool::True : MaybeBool::Partial) : MaybeBool::False;
    }

    void UpdateEntryGroupParentsEnabled(ConfigEditGroup *group, bool parentsEnabled) {
        ConfigEditEntry *child = group->FirstChild;
        while (child) {
            if (child->ParentsEnabled != parentsEnabled) {
                child->ParentsEnabled = parentsEnabled;
                if (child->Enabled) {
                    mConfigTree->SetPartialChecked(GetNode(child), GetEntryChecked(child), false);

                    if (child->Group) {
                        UpdateEntryGroupParentsEnabled(child->Group, child->ParentsEnabled && child->Enabled);
                    }
                }
            }
            child = child->Next;
        }
    }

    void ToggleEntryEnabled(ConfigEditEntry *entry) {
        entry->Enabled = !entry->Enabled;
        ConfigEditUpdateEnabled(entry); // (node text is untouched)

        if (entry->Group) {
            if (entry->Group->End) {
                entry->Group->End->Enabled = entry->Enabled;
                ConfigEditUpdateEnabled(entry->Group->End);
            }

            UpdateEntryGroupParentsEnabled(entry->Group, entry->ParentsEnabled && entry->Enabled);
        }
    }

    ConfigChg *GetUnlockedChange() {
        intptr_t chgIdx = mChanges.size() - mRedoDepth - 1;
        if (chgIdx < 0) {
            return nullptr;
        }

        auto ptr = &mChanges[chgIdx];
        return ptr->Locked ? nullptr : ptr;
    }

    void FinalizeUnlockedChange(ConfigChg *prevChg, bool canDelete) {
        mSaveNeeded = true;
        if (mRedoDepth) {
            return;
        }

        if (canDelete) {
            mChanges.pop_back();
            UpdateUndoRedoButtons();
        } else {
            prevChg->Locked = true;
        }
    }

    void CleanEmptyEntryChange() {
        ConfigChg *prevChg = GetUnlockedChange();
        if (prevChg && prevChg->Type == ConfigChg::Change &&
            mCurrEntry && prevChg->EntryPtr == mCurrEntry) {
            FinalizeUnlockedChange(prevChg, mCurrEntry->RawText == prevChg->EntryStore->RawText);
        }
    }

    template <typename TOp>
    void ChangeEntry(ConfigEditEntry *entry, TOp &&op) {
        ConfigChg *prevChg = GetUnlockedChange();
        if (!prevChg || prevChg->Type != ConfigChg::Change || prevChg->EntryPtr != entry) {
            auto entryBak = UniquePtr<ConfigEditEntry>::New();
            entry->CopyStateTo(entryBak);
            AddChange(ConfigChg::Change, entry, move(entryBak));
        }

        op();
        ConfigEditUpdate(entry);
        mConfigTree->Set(GetNode(entry), GetEntryText(entry));
    }

    void InsertGroup(TreeNode destNode, ConfigEditGroup *dest) {
        auto loadGroupRec = [&](TreeNode node, ConfigEditGroup *group, auto &loadEntryRecF) -> void {
            ConfigEditEntry *child = group->FirstChild;
            while (child) {
                loadEntryRecF(node, child, loadEntryRecF);
                child = child->Next;
            }

            mConfigTree->ForceHasChildren(node);
            mConfigTree->SetExpanded(node);
        };

        auto loadEntryRec = [&](TreeNode parent, ConfigEditEntry *entry, auto &loadEntryRecF) -> void {
            if (!entry->Group && !entry->Mapping && !entry->Variable && !entry->Info.Error) {
                return;
            }

            TreeNode node = mConfigTree->Add(parent, GetEntryText(entry), entry);
            entry->UserData = node;

            mConfigTree->SetPartialChecked(node, GetEntryChecked(entry), false);

            if (entry->Group) {
                loadGroupRec(node, entry->Group, loadEntryRecF);
            }
        };

        loadGroupRec(destNode, dest, loadEntryRec);
    }

    ConfigEditEntry *InsertEntry(UniquePtr<ConfigEditEntry> &&entryStore, ConfigEditGroup *parent, ConfigEditEntry *prev) {
        PreventRedraw prevent(mConfigTree, entryStore->Group != nullptr);

        auto entry = ConfigEditEntry::Insert(parent, prev, move(entryStore));

        TreeNode parentNode = GetNode(parent->Start);
        auto node = mConfigTree->Insert(parentNode, GetNode(prev), GetEntryText(entry), entry);
        entry->UserData = node;

        mConfigTree->SetPartialChecked(node, GetEntryChecked(entry), false);

        if (entry->Group) {
            InsertGroup(node, entry->Group);
        }

        mConfigTree->SetSelected(node);
        return entry;
    }

    template <typename TCtor>
    void CreateEntry(TCtor &&ctor) {
        auto refEntry = GetEntry(mConfigTree->GetSelected());
        ConfigEditGroup *parent = refEntry ? (refEntry->Group ? refEntry->Group : refEntry->Parent) : mCurrEdit->Root;
        ConfigEditEntry *prev = refEntry ? (refEntry->Group ? nullptr : refEntry) : nullptr;

        auto newEntry = UniquePtr<ConfigEditEntry>::New();
        newEntry->ParentsEnabled = parent->GetEnabled();
        ctor(newEntry);
        ConfigEditUpdate(newEntry);

        auto entry = InsertEntry(move(newEntry), parent, prev);

        AddChange(ConfigChg::Remove, entry);
    }

    UniquePtr<ConfigEditEntry> RemoveEntry(ConfigEditEntry *entry, ConfigEditGroup **outParent, ConfigEditEntry **outPrev) {
        PreventRedraw prevent(mConfigTree, entry->Group != nullptr);

        *outParent = entry->Parent;
        *outPrev = GetPrev(entry);

        if (mCurrEntry == entry) {
            CleanEmptyEntryChange();
        } else {
            mConfigTree->SetSelected(GetNode(entry), false); // to get default redirect
        }

        mCurrEntry = nullptr;
        mConfigTree->Remove(GetNode(entry));
        return entry->Remove();
    }

    void SwapEntry(ConfigEditEntry *entry, ConfigEditGroup **ptrParent, ConfigEditEntry **ptrPrev) {
        PreventRedraw prevent(mConfigTree, entry->Group != nullptr);

        ConfigEditGroup *newParent = *ptrParent;
        ConfigEditEntry *newPrev = *ptrPrev;
        auto entryStore = RemoveEntry(entry, ptrParent, ptrPrev);
        InsertEntry(move(entryStore), newParent, newPrev);
    }

    Control *GetEntryUi(ConfigEditEntry *entry) {
        if (!entry) {
            return nullptr;
        }

        if (entry->Info.Error) {
            if (!mErrorUiPanel) {
                mErrorUiPanel = New<ConfigErrorUiPanel>();
            }
            return mErrorUiPanel;
        } else if (entry->Group) {
            if (!mParentUiPanel) {
                mParentUiPanel = New<ConfigParentUiPanel>(this);
            }

            mParentUiPanel->Update(entry);
            return mParentUiPanel;
        } else if (entry->Variable) {
            if (!mVariableUiPanel) {
                mVariableUiPanel = New<ConfigVariableUiPanel>(this);
            }

            mVariableUiPanel->Update(entry);
            return mVariableUiPanel;
        } else if (entry->Mapping) {
            if (!mMappingUiPanel) {
                mMappingUiPanel = New<ConfigMappingUiPanel>(this);
            }

            mMappingUiPanel->Update(entry);
            return mMappingUiPanel;
        }
        return nullptr;
    }

    void ReloadConfig() {
        PreventRedraw prevent(mConfigTree);
        mConfigTree->Clear();

        mChanges.clear();
        mRedoDepth = 0;
        mCurrEntry = nullptr;
        mCurrEdit = mCurrConfig ? ConfigEditLoad(GetPath(ConfigsPath, mCurrConfig, ConfigsExt)) : nullptr;

        if (mCurrEdit) {
            InsertGroup(nullptr, mCurrEdit->Root);
        }

        mNewMappingBtn->Enable(mCurrEdit != nullptr);
        mEditTextBtn->Enable(mCurrEdit != nullptr);

        UpdateUndoRedoButtons();
    }

    Control *OnCreate() override {
        mAutoSaveTimer = New<Timer>(30, false, [this] {
            SaveIfNeeded();
        });

        mUndoButton = New<Button>(L"\x27F2Undo", [this] {
            intptr_t idx = mChanges.size() - mRedoDepth - 1;
            if (idx >= 0) {
                RepeatChange(mChanges[idx]);
                mRedoDepth++;
                UpdateUndoRedoButtons();
                mSaveNeeded = true;
            }
        });

        mRedoButton = New<Button>(L"\x27F3Redo", [this] {
            intptr_t idx = mChanges.size() - mRedoDepth;
            if (idx < (intptr_t)mChanges.size()) {
                RepeatChange(mChanges[idx]);
                mRedoDepth--;
                UpdateUndoRedoButtons();
                mSaveNeeded = true;
            }
        });

        mConfigChoice = New<ConfigChoicePanel>(CONFIG_CHOICE_ALLOW_NEW | CONFIG_CHOICE_ALLOW_DELETE,
                                               [this](Path &&config) {
                                                   if (config && mCurrConfig && tstreq(config, mCurrConfig)) {
                                                       return;
                                                   }

                                                   SaveIfNeeded();
                                                   mCurrConfig = move(config);
                                                   ReloadConfig();

                                                   if (!mFromActivate) {
                                                       mExeUiIntf->ResetSelection();
                                                   }
                                               });

        mConfigTree = New<TreeView>([this](TreeNode node) {
            CleanEmptyEntryChange();
            auto entry = GetEntry(node);
            mCurrEntry = entry;
            mConfigDynamic->SetChild(GetEntryUi(entry));

            bool hasEntry = entry != nullptr;
            mDeleteBtn->Enable(hasEntry);
            mUpDownBtn->Enable(hasEntry);
        });

        mConfigTree->SetOnPartialCheck([this](TreeNode node, MaybeBool value) {
            auto entry = GetEntry (node);
            {
                if (value == MaybeBool::False){
                    value = entry->ParentsEnabled ? MaybeBool::True : MaybeBool::Partial;
}
                else{
                    value = MaybeBool::False;
}
            }
            return value; }, [this](TreeNode node, MaybeBool value) {
            auto entry = GetEntry (node);
            if (entry && entry->Enabled != (value != MaybeBool::False))
            {
                ToggleEntryEnabled (entry);
                AddChange (ConfigChg::Toggle, entry);
            } }, true);

        mConfigTree->SetOnClick([this](TreeNode node, bool right, bool dblclk) {
            if (right) {
                mEntryPopup->Create();
            }
        });

        mConfigTree->SetOnKey([this](int key) {
            if (key == VK_DELETE) {
                mDeleteBtn->Click();
            }
        });

        mConfigDynamic = New<Dynamic>();

        mNewMappingBtn = New<SplitButton>(L"New Mapping", [this] {
            CreateEntry([this](ConfigEditEntry *entry) {
                entry->Mapping = SharedPtr<ImplMapping>::New();
                entry->Mapping->Strength = 0.5;
                entry->Mapping->Rate = 0.02;
            });
        });
        mNewVariableBtn = New<Button>(L"New Option", [this] {
            CreateEntry([this](ConfigEditEntry *entry) {
                entry->Variable = UniquePtr<ConfigEditVar>::New();
            });
        });
        mNewGroupBtn = New<Button>(L"New Group", [this] {
            CreateEntry([this](ConfigEditEntry *entry) {
                entry->Group = UniquePtr<ConfigEditGroup>::New(entry);
                entry->Group->End = UniquePtr<ConfigEditEntry>::New("]");
            });
        });

        mNewPopupMenu = New<PopupMenu>([this](PopupMenu *pop) {
            pop->Add(mNewVariableBtn);
            pop->Add(mNewGroupBtn);
        });
        mNewMappingBtn->SetPopup(mNewPopupMenu);

        mDeleteBtn = New<Button>(L"Delete", &gDelIcon, [this] {
            auto entry = GetEntry(mConfigTree->GetSelected());
            if (entry) {
                ConfigEditGroup *parent;
                ConfigEditEntry *prev;
                auto entryStore = RemoveEntry(entry, &parent, &prev);
                AddChange(ConfigChg::Add, nullptr, move(entryStore), parent, prev);
            }
        });

        mUpDownBtn = New<UpDownButtons>([this](bool down) {
            auto entry = GetEntry(mConfigTree->GetSelected());
            if (entry) {
                auto [ok, parentNode, prevNode] = mConfigTree->GetAdjacentInsertPos(GetNode(entry), down);

                if (ok) {
                    AddChange(ConfigChg::Swap, entry, nullptr, entry->Parent, GetPrev(entry));

                    ConfigEditGroup *parent = parentNode ? GetEntry(parentNode)->Group : mCurrEdit->Root;
                    ConfigEditEntry *prev = GetEntry(prevNode);
                    SwapEntry(entry, &parent, &prev);
                }
            }
        });

        mEditTextBtn = New<Button>(L"Edit Raw Text", [this] {
            OpenInShell(GetPath(ConfigsPath, mCurrConfig, ConfigsExt));
        });

        mEntryPopup = New<PopupMenu>([this](PopupMenu *pop) {
            pop->Add(mNewMappingBtn);
            pop->Add(mNewVariableBtn);
            pop->Add(mNewGroupBtn);
            pop->AddSep();
            pop->Add(mDeleteBtn);
        });

        mConfigTree->SetSelected(nullptr);

        auto shortcuts = GetShortcuts();
        shortcuts->Add(mUndoButton, 'Z', true);
        shortcuts->Add(mRedoButton, 'Z', true, true);
        shortcuts->Add(mRedoButton, 'Y', true);

        auto topLayout = New<Layout>();
        topLayout->AddLeft(mUndoButton);
        topLayout->AddLeft(mRedoButton);
#ifdef CONFIG_NO_SAVE
        topLayout->AddRight(New<StyledLabel>(L"Saving disabled", RGB(0xff, 0, 0)));
#else
        topLayout->AddRight(New<Label>(L"Auto-save enabled"));
#endif

        auto btnLayout = New<Layout>();
        btnLayout->AddLeft(mNewMappingBtn);
        btnLayout->AddLeft(mDeleteBtn);
        btnLayout->AddLeft(mUpDownBtn);
        btnLayout->AddRight(mEditTextBtn);

        auto cfgLayout = New<Layout>();
        cfgLayout->AddLeft(mConfigTree, Layout::Proportion(0.5));
        cfgLayout->AddRemaining(mConfigDynamic);

        auto layout = New<Layout>(true);
        layout->AddTopMiddle(mConfigChoice);
        layout->AddTop(New<Separator>());
        layout->AddTop(topLayout);
        layout->AddBottom(btnLayout);
        layout->AddRemaining(cfgLayout);
        return layout;
    }

    void OnActivate(bool activate, void *root, void *prev) override {
        if (activate) {
            mFromActivate = true;

            bool changedSince = GetTimestamp() > mLastChangeTimestamp;
            if (changedSince) {
                mSaveNeeded = false; // just in case
            }

            Path reqConfig = mExeUiIntf->GetConfig();
            if (reqConfig) {
                mConfigChoice->SetSelected(reqConfig);
            } else if (!mConfigChoice->HasSelected()) {
                mConfigChoice->SetSelected(ConfigsDefault);
            } else if (changedSince) {
                ReloadConfig();
            }

            mAutoSaveTimer->Start();
            mFromActivate = false;
        } else {
            mAutoSaveTimer->End();
            CleanEmptyEntryChange();
            SaveIfNeeded();
            mLastChangeTimestamp = GetTimestamp();
        }
    }

    friend ConfigEditEntry *GetConfigEntry(ConfigPanel *panel) {
        return panel->mCurrEntry;
    }

public:
    ConfigPanel(HWND parent, intptr_t id, ExeUiIntf *exeUi) : Panel(parent, id), mExeUiIntf(exeUi) {}

    void OnDestroy() {
        ASSERT(!mSaveNeeded, "window didn't deactivate?");
    }

    Control *InitialFocus() override { return mConfigTree; }

    template <typename TOp>
    friend void ChangeConfigEntry(ConfigPanel *panel, TOp &&op) {
        panel->ChangeEntry(panel->mCurrEntry, [&] { op(panel->mCurrEntry); });
    }

    Path GetConfig() override {
        Path config = mExeUiIntf->GetConfig();
        if (!config) {
            config = mConfigChoice->GetSelected();
        }
        if (!config) {
            config = ConfigsDefault;
        }
        return config;
    }

    void SetConfig(const Path &path) override {
        mExeUiIntf->ResetSelection();
        mConfigChoice->SetSelected(path);
    }
};
