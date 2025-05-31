#pragma once
#include "CommonUi.h"
#include "ConfigEdit.h"
#define IMPL_KEY_MOUSE_UTILS_ONLY
#include "ImplKeyMouse.h"

#define CONFIG_KEY_UI_INPUT (1 << 0)
#define CONFIG_KEY_UI_OUTPUT (1 << 1)
#define CONFIG_KEY_UI_VERTICAL (1 << 2)

class ConfigKeyPanel : public Panel {
    class KeyPopupWindow : public PopupWindow {
        ConfigKeyPanel *mParent;
        TreeView *mTree;

        void Add(TreeNode parent, key_t key) {
            auto node = mTree->Add(parent, mParent->GetKeyDesc(key), (void *)(uintptr_t)key);

            if (key == mParent->mCurrKey) {
                mTree->SetSelected(node, false);
            }
        }

        void AddPlugins() {
            AddPluginNodesToTree(mTree);

            int keyIdx = 0;
            for (auto &keyData : GPlugins.Keys) {
                key_t key = MY_VK_CUSTOM_START + (keyIdx++);

                if ((mParent->mFlags & CONFIG_KEY_UI_INPUT) && !keyData.IsInput) {
                    continue;
                }
                if ((mParent->mFlags & CONFIG_KEY_UI_OUTPUT) && !keyData.IsOutput) {
                    continue;
                }

                auto node = (TreeNode)GPlugins.Plugins[keyData.PluginIdx].TempUserData;
                Add(node, key);
            }
        }

        TreeNode AddGroup(TreeNode prevGroupNode, MyVkGroup group) {
            switch (group) {
            case MyVkGroup::Gamepad:
                return mTree->Add(prevGroupNode, L"Gamepad");
            case MyVkGroup::GamepadDiagonal:
                mTree->SetExpanded(prevGroupNode);
                return mTree->Add(prevGroupNode, L"Diagonals");
            case MyVkGroup::GamepadModifier:
                return mTree->Add(mTree->GetParent(prevGroupNode), L"Modifiers");
            case MyVkGroup::GamepadRotator:
                return mTree->Add(mTree->GetParent(prevGroupNode), L"Rotators");
            case MyVkGroup::GamepadMotion:
                return mTree->Add(mTree->GetParent(prevGroupNode), L"Motion (PS4 gamepad only)");
            case MyVkGroup::Mouse:
                return mTree->Add(nullptr, L"Mouse");
            case MyVkGroup::Keyboard:
                return mTree->Add(nullptr, L"Keyboard");
            case MyVkGroup::KeyboardChar:
                return mTree->Add(prevGroupNode, L"Characters");
            case MyVkGroup::KeyboardNumpad:
                return mTree->Add(mTree->GetParent(prevGroupNode), L"Numpad");
            case MyVkGroup::KeyboardRare:
                return mTree->Add(mTree->GetParent(prevGroupNode), L"Rare Keys");
            case MyVkGroup::KeyboardMedia:
                return mTree->Add(mTree->GetParent(prevGroupNode), L"Media Keys");
            case MyVkGroup::Command:
                return mTree->Add(nullptr, L"Actions");
            case MyVkGroup::CommandDebug:
                return mTree->Add(prevGroupNode, L"Debug");
            default:
                return Fatal("wrong key group"), nullptr;
            }
        }

        void Create(POINT pos) override {
            PopupWindow::Create(pos, SIZE{350, 400});
        }

    public:
        KeyPopupWindow(HWND parent, intptr_t id, ConfigKeyPanel *panel) : PopupWindow(parent, id), mParent(panel) {}

        Control *OnCreate() override {
            mTree = New<TreeView>([this](TreeNode node) {
                void *data = mTree->GetData(node);
                if (data) {
                    auto key = (int)(uintptr_t)data;
                    mParent->UpdateKey(key);
                    mParent->OnChange();
                    Destroy();
                }
            });

            MyVkGroup currGroup = MyVkGroup::None;
            TreeNode currParent = nullptr;

#define CONFIG_ON_KEY(vk, pname, desc, group, name, ...)                                       \
    if ((mParent->mFlags & CONFIG_KEY_UI_OUTPUT) || !GetKeyType(vk).IsOutputOnly()) {          \
        if (currGroup != MyVkGroup::group)                                                     \
            currParent = AddGroup(currParent, MyVkGroup::group), currGroup = MyVkGroup::group; \
        Add(currParent, vk);                                                                   \
    }

            ENUMERATE_KEYS_WITH_SIMPLE(CONFIG_ON_KEY);
#undef CONFIG_ON_KEY

            AddPlugins();

            return mTree;
        }
    };

    function<void(key_t, MyVkType, user_t, SharedPtr<string>)> mChanged;

    int mFlags = 0;
    key_t mCurrKey = 0;
    MyVkType mCurrKeyType = {};
    user_t mCurrUser = -1;
    SharedPtr<string> mCurrData;
    EditKey *mKeyEdit = nullptr;
    DropDownPopup *mKeyDropDown = nullptr;
    KeyPopupWindow *mKeyPopup = nullptr;
    Dynamic *mDataDynamic = nullptr;
    Layout *mUserLayout = nullptr;
    DropDownList *mUserDropDown = nullptr;
    EditInt<user_t> *mUserEdit = nullptr;
    DropDownEditLine *mConfigEdit = nullptr;

    const wchar_t *GetKeyDesc(key_t key) {
        switch (key) {
#define CONFIG_ON_KEY(vk, pname, desc, group, name, ...) \
    case vk:                                             \
        return desc;
            ENUMERATE_KEYS_WITH_SIMPLE(CONFIG_ON_KEY);
#undef CONFIG_ON_KEY

        case MY_VK_UNKNOWN:
            return L"(Input or Choose)";

        default:
            if (key >= MY_VK_CUSTOM_START && key < MY_VK_CUSTOM_START + (int)GPlugins.Keys.size()) {
                return GPlugins.Keys[key - MY_VK_CUSTOM_START].Description.c_str();
            } else {
                return L"???";
            }
            break;
        }
    }

    Control *GetCurrKeyDataUi() {
        if (mCurrKeyType.OfUser) {
            return mUserLayout;
        } else if (mCurrKey == MY_VK_LOAD_CONFIG) {
            return mConfigEdit;
        }

        if (mCurrKey >= MY_VK_CUSTOM_START && mCurrKey < MY_VK_CUSTOM_START + (int)GPlugins.Keys.size()) {
            auto &pluginKey = GPlugins.Keys[mCurrKey - MY_VK_CUSTOM_START];
            if (pluginKey.HasData) {
                return mConfigEdit;
            }
        }

        return nullptr;
    }

    void OnChange() {
        Control *dataUi = GetCurrKeyDataUi();
        mChanged(mCurrKey, mCurrKeyType,
                 dataUi == mUserLayout ? mCurrUser : -1,
                 dataUi == mConfigEdit ? mCurrData : nullptr);
    }

    void UpdateKey(key_t key) { Set(key, mCurrUser, mCurrData); }
    void UpdateUser(user_t user) { Set(mCurrKey, user, mCurrData); }

public:
    ConfigKeyPanel(HWND p, intptr_t id, int flags, function<void(key_t key, MyVkType type, user_t user, SharedPtr<string> data)> &&changed) : Panel(p, id), mChanged(move(changed)), mFlags(flags) {}

    void Set(key_t key, user_t user, SharedPtr<string> data) {
        mCurrKey = key;
        mCurrKeyType = GetKeyType(key);
        mCurrUser = user;
        mCurrData = data;

        mKeyEdit->Set(GetKeyDesc(key));

        Control *dataUi = GetCurrKeyDataUi();
        mDataDynamic->SetChild(dataUi);

        if (dataUi == mUserLayout) {
            mUserEdit->Show(user >= 0);
            mUserDropDown->SetSelected(user >= 0, false);
            if (user >= 0) {
                mUserEdit->Set(user + 1);
            }
        } else if (dataUi == mConfigEdit) {
            mConfigEdit->Clear();
            for (auto &config : ConfigNames.GetNames()) {
                mConfigEdit->Add(config);
            }

            mConfigEdit->Set(data ? PathFromStr(data->c_str()) : Path(L""));
        }
    }

    Control *OnCreate() override {
        mKeyEdit = New<EditKey>([this](int vk, int scan, int ext, int mods) {
            if (!mods)
            {
                ImplSplitLeftRight (&vk, &ext, KF_EXTENDED, scan);
                UpdateKey (ImplUnextend (vk, ext));
                OnChange ();
            } }, [this](int dx, int dy) {
            key_t key = dy > 0 ? MY_VK_WHEEL_UP : dy < 0 ? MY_VK_WHEEL_DOWN :
                dx > 0 ? MY_VK_WHEEL_RIGHT : dx < 0 ? MY_VK_WHEEL_LEFT : 0;
            if (key != 0)
            {
                UpdateKey (key);
                OnChange ();
            } });

        mKeyPopup = New<KeyPopupWindow>(this);
        mKeyDropDown = New<DropDownPopup>(mKeyPopup);
        mKeyDropDown->SetPopupBase(mKeyEdit);

        mUserDropDown = New<DropDownList>([this](int idx) {
            UpdateUser(idx ? mUserEdit->Get() - 1 : -1);
            OnChange();
        });
        mUserDropDown->Add(L"Active Gamepad");
        mUserDropDown->Add(L"Gamepad #...");

        mUserEdit = New<EditInt<user_t>>([this](user_t value) {
            UpdateUser(value - 1);
            OnChange();
        });
        mUserEdit->SetRange(1, IMPL_MAX_USERS);

        mUserLayout = New<Layout>();
        mUserLayout->AddLeftMiddle(mUserDropDown, -1, 0, 0);
        mUserLayout->AddLeftMiddle(mUserEdit, 25);

        mConfigEdit = New<DropDownEditLine>([this] {
            mCurrData = SharedPtr<string>::New(ToStdStr(mConfigEdit->Get()));
            OnChange();
        });

        auto layout = New<Layout>();
        layout->AddLeftMiddle(mKeyEdit, 150, 0, 0);
        layout->AddLeftMiddle(mKeyDropDown, 15);

        mDataDynamic = New<Dynamic>();

        if (mFlags & CONFIG_KEY_UI_VERTICAL) {
            auto vertLayout = New<Layout>();
            vertLayout->AddTop(layout);
            layout = vertLayout;
        }

        layout->AddLeftMiddle(mDataDynamic);
        return layout;
    }
};

class ConfigCondsPanel : public Panel {
    ConfigPanel *mConfig;
    ImplCond *mCurrCond = nullptr;
    function<void()> mCountChanged;

    TreeView *mCondTree;
    SplitButton *mNewKeyBtn;
    Button *mNewComplexBtn;
    PopupMenu *mNewPopupMenu;
    Button *mDeleteBtn;
    UpDownButtons *mUpDownBtn;

    Dynamic *mCondDynamic;
    Layout *mCondKeyLay;
    ConfigKeyPanel *mCondKey;
    DropDownList *mCondKeyDrop;
    Layout *mCondComplexLay;
    DropDownList *mCondComplexDrop;

    bool IsComplex(ImplCond *cond) {
        return cond->Key == MY_VK_META_COND_AND || cond->Key == MY_VK_META_COND_OR;
    }

    void UpdateCondUi(ImplCond *cond) {
        if (!cond) {
            mCondDynamic->SetChild(nullptr);
        } else {
            bool isComplex = IsComplex(cond);

            mCondDynamic->SetChild(isComplex ? mCondComplexLay : mCondKeyLay);

            if (isComplex) {
                int state = (cond->Key == MY_VK_META_COND_OR ? 1 : 0) |
                            (cond->State ? 0 : 2);

                mCondComplexDrop->SetSelected(state, false);
            } else {
                mCondKey->Set(cond->Key, cond->User, nullptr);

                int state = (cond->State ? 0 : 1) |
                            (cond->Toggle ? 2 : 0);

                mCondKeyDrop->SetSelected(state, false);
            }
        }
    }

    void UpdateTreeUi(ConfigEditEntry *entry, ImplCond *cond) {
        bool hasConds = entry->Mapping->Conds != nullptr;

        mDeleteBtn->Enable(cond != nullptr);
        mUpDownBtn->Enable(cond != nullptr);
        mDeleteBtn->Show(hasConds);
        mUpDownBtn->Show(hasConds);
        mCondTree->Display(hasConds);
    }

    std::wstring GetCondStr(ImplCond *cond) {
        std::ostringstream out;
        ConfigEditWriteCond(out, cond, true, false);
        return ToStdWStr(move(out.str()));
    }

    void UpdateTreeText() {
        auto node = mCondTree->GetSelected();
        auto cond = (ImplCond *)mCondTree->GetData(node);
        if (cond) {
            mCondTree->Set(node, GetCondStr(cond).c_str());
        }
    }

    void PopulateChildren(TreeNode node, ImplCond *cond) {
        if (IsComplex(cond)) {
            PopulateTree(node, cond->Child);
            mCondTree->ForceHasChildren(node);
            mCondTree->SetExpanded(node);
        }
    }

    void PopulateTree(TreeNode parent, ImplCond *conds) {
        while (conds) {
            TreeNode node = mCondTree->Add(parent, GetCondStr(conds).c_str(), conds);
            PopulateChildren(node, conds);
            conds = conds->Next;
        }
    }

    SharedPtr<ImplCond> *GetPrevPtr(ConfigEditEntry *entry, TreeNode parent, TreeNode prev) {
        if (prev) {
            return &((ImplCond *)mCondTree->GetData(prev))->Next;
        } else if (parent) {
            return &((ImplCond *)mCondTree->GetData(parent))->Child;
        } else {
            return &entry->Mapping->Conds;
        }
    }

    void AddCond(int key) {
        auto selNode = mCondTree->GetSelected();
        auto selCond = (ImplCond *)mCondTree->GetData(selNode);
        auto parentNode = selCond && IsComplex(selCond) ? selNode : mCondTree->GetParent(selNode);
        auto prevNode = parentNode == selNode ? nullptr : selNode;

        auto newCond = SharedPtr<ImplCond>::New();
        newCond->State = true;
        newCond->Key = key;

        ChangeConfigEntry(mConfig, [&](auto entry) {
            auto prevPtr = GetPrevPtr(entry, parentNode, prevNode);
            newCond->Next = *prevPtr;
            *prevPtr = newCond;
        });

        auto newNode = mCondTree->Insert(parentNode, prevNode, GetCondStr(newCond).c_str(), newCond);
        PopulateChildren(newNode, newCond);
        mCondTree->SetSelected(newNode);
        mCountChanged();
    }

public:
    ConfigCondsPanel(HWND parent, intptr_t id, ConfigPanel *panel, function<void()> &&countChanged) : Panel(parent, id), mConfig(panel), mCountChanged(move(countChanged)) {}

    Control *OnCreate() {
        mCondTree = New<TreeView>([this](TreeNode node) {
            mCurrCond = (ImplCond *)mCondTree->GetData(node);
            UpdateCondUi(mCurrCond);
            UpdateTreeUi(GetConfigEntry(mConfig), mCurrCond);
        });

        mCondTree->SetOnKey([this](int key) {
            if (key == VK_DELETE) {
                mDeleteBtn->Click();
            } else {
                return false;
            }
            return true;
        });

        mNewKeyBtn = New<SplitButton>(L"New Key", [this] {
            AddCond(MY_VK_UNKNOWN);
        });
        mNewComplexBtn = New<Button>(L"New Complex", [this] {
            AddCond(MY_VK_META_COND_AND);
        });

        mNewPopupMenu = New<PopupMenu>([this](PopupMenu *pop) {
            pop->Add(mNewComplexBtn);
        });
        mNewKeyBtn->SetPopup(mNewPopupMenu);

        mDeleteBtn = New<Button>(L"Delete", &gDelIcon, [this] {
            auto node = mCondTree->GetSelected();
            if (node) {
                PreventRedraw prevent(mCondTree, mCurrCond->Child != nullptr);

                auto parent = mCondTree->GetParent(node);
                auto prev = mCondTree->GetPrevSibling(node);

                ChangeConfigEntry(mConfig, [&](auto entry) {
                    auto prevPtr = GetPrevPtr(entry, parent, prev);
                    *prevPtr = mCurrCond->Next;
                });
                mCurrCond = nullptr;

                mCondTree->Remove(node);
                UpdateTreeUi(GetConfigEntry(mConfig), mCurrCond);
                mCountChanged();
            }
        });

        mUpDownBtn = New<UpDownButtons>([this](bool down) {
            auto node = mCondTree->GetSelected();
            if (node) {
                auto [ok, newParent, newPrev] = mCondTree->GetAdjacentInsertPos(node, down);
                if (ok) {
                    PreventRedraw prevent(mCondTree, mCurrCond->Child != nullptr);

                    auto parent = mCondTree->GetParent(node);
                    auto prev = mCondTree->GetPrevSibling(node);

                    ChangeConfigEntry(mConfig, [&](auto entry) {
                        auto prevPtr = GetPrevPtr(entry, parent, prev);
                        auto currCond = *prevPtr;
                        *prevPtr = currCond->Next;
                        auto newPrevPtr = GetPrevPtr(entry, newParent, newPrev);
                        currCond->Next = *newPrevPtr;
                        *newPrevPtr = currCond;
                    });

                    mCondTree->Remove(node);
                    auto newNode = mCondTree->Insert(newParent, newPrev, GetCondStr(mCurrCond).c_str(), mCurrCond);
                    PopulateChildren(newNode, mCurrCond);
                    mCondTree->SetSelected(newNode);
                }
            }
        });

        mCondDynamic = New<Dynamic>();

        mCondKey = New<ConfigKeyPanel>(CONFIG_KEY_UI_INPUT | CONFIG_KEY_UI_VERTICAL,
                                       [this](key_t key, MyVkType type, user_t user, SharedPtr<string>) {
                                           ChangeConfigEntry(mConfig, [&](auto) {
                                               mCurrCond->Key = key;
                                               mCurrCond->User = user;
                                           });
                                           UpdateTreeText();
                                       });

        mCondKeyDrop = New<DropDownList>([this](int idx) {
            ChangeConfigEntry(mConfig, [&](auto) {
                mCurrCond->State = !(idx & 1);
                mCurrCond->Toggle = (idx & 2);
            });
            UpdateTreeText();
        });
        mCondKeyDrop->Add(L"Is pressed");
        mCondKeyDrop->Add(L"Is not pressed");
        mCondKeyDrop->Add(L"Is toggled on");
        mCondKeyDrop->Add(L"Is toggled off");

        mCondComplexDrop = New<DropDownList>([this](int idx) {
            ChangeConfigEntry(mConfig, [&](auto) {
                mCurrCond->State = !(idx & 2);
                mCurrCond->Key = (idx & 1) ? MY_VK_META_COND_OR : MY_VK_META_COND_AND;
            });
            UpdateTreeText();
        });
        mCondComplexDrop->Add(L"All sub-conditions hold");
        mCondComplexDrop->Add(L"At least one holds");
        mCondComplexDrop->Add(L"At least one doesn't hold");
        mCondComplexDrop->Add(L"No sub-conditions hold");

        mCondKeyLay = New<Layout>();
        mCondKeyLay->AddTopLeft(mCondKey);
        mCondKeyLay->AddLeft(mCondKeyDrop, 150);

        mCondComplexLay = New<Layout>();
        mCondComplexLay->AddLeft(mCondComplexDrop, 150);

        Layout *treeBtnLay = New<Layout>();
        treeBtnLay->AddLeft(mNewKeyBtn);
        treeBtnLay->AddLeft(mDeleteBtn);
        treeBtnLay->AddLeft(mUpDownBtn);

        Layout *layout = New<Layout>();
        layout->AddRight(mCondDynamic, Layout::Proportion(0.5));
        layout->AddBottom(treeBtnLay);
        layout->AddRemaining(mCondTree);
        return layout;
    }

    void Update(ConfigEditEntry *entry) {
        mCurrCond = nullptr;
        UpdateCondUi(nullptr);

        PreventRedraw prevent(mCondTree);
        mCondTree->Clear();
        PopulateTree(nullptr, entry->Mapping->Conds);
        UpdateTreeUi(entry, nullptr);
    }
};

class ConfigMappingUiPanel : public Panel {
    struct KeyUiInfo {
        bool IsModifier = false;
        bool IsValue = false;
        bool HasTopStrength = false;
        bool OptionsNeeded = false;
        bool ConditionsNeeded = false;
    };

    ConfigPanel *mConfig;
    ConfigKeyPanel *mSrcKey;
    ConfigKeyPanel *mDestKey;
    Layout *mTopStrengthLay;
    Label *mTopStrengthLbl;
    EditFloat<double> *mTopStrengthEdit;

    CheckBox *mOptionsChk;
    Layout *mOptionsLay;
    Layout *mOptStrengthLay;
    CheckBox *mOptStrengthChk;
    EditFloat<double> *mOptStrengthEdit;
    CheckBox *mRateChk;
    EditFloat<double> *mRateEdit;
    DropDownList *mActionDrop;
    DropDownList *mAxisActionDrop;
    CheckBox *mReplaceChk;
    CheckBox *mForwardChk;

    CheckBox *mCondChk;
    ConfigCondsPanel *mCondPanel;

    KeyUiInfo GetUiInfo(ConfigEditEntry *entry) {
        KeyUiInfo info;
        info.IsModifier = entry->Mapping->DestType.Modifier;

        auto key = entry->Mapping->DestKey;
        auto custKey = key >= MY_VK_CUSTOM_START && key < MY_VK_CUSTOM_START + (int)GPlugins.Keys.size() ? &GPlugins.Keys[key - MY_VK_CUSTOM_START] : nullptr;

        info.IsValue = custKey && custKey->HasStrength;

        info.HasTopStrength = info.IsModifier || info.IsValue;

        info.OptionsNeeded =
            entry->Mapping->Forward || entry->Mapping->Replace ||
            entry->Mapping->Toggle || entry->Mapping->Turbo ||
            entry->Mapping->Add || entry->Mapping->Reset ||
            entry->Info.HasRate || (entry->Info.HasStrength && !info.HasTopStrength);

        info.ConditionsNeeded = entry->Mapping->Conds != nullptr;
        return info;
    }

    void UpdateEntryOnDestKeyChange(ConfigEditEntry *entry) {
        auto uiInfo = GetUiInfo(entry);
        if (uiInfo.HasTopStrength) {
            entry->Info.HasStrength = true;
            entry->Mapping->Strength = mTopStrengthEdit->Get();
        } else {
            entry->Info.HasStrength = mOptStrengthChk->Get();
            entry->Mapping->Strength = mOptStrengthEdit->Get();
        }
    }

    Control *OnCreate() {
        mSrcKey = New<ConfigKeyPanel>(CONFIG_KEY_UI_INPUT, [this](key_t key, MyVkType type, user_t user, SharedPtr<string>) {
            ChangeConfigEntry(mConfig, [&](auto entry) {
                entry->Mapping->SrcKey = key;
                entry->Mapping->SrcType = type;
                entry->Mapping->SrcUser = user;
            });
            UpdateKeyDepUi(GetConfigEntry(mConfig));
        });

        mDestKey = New<ConfigKeyPanel>(CONFIG_KEY_UI_OUTPUT, [this](key_t key, MyVkType type, user_t user, SharedPtr<string> data) {
            ChangeConfigEntry(mConfig, [&](auto entry) {
                entry->Mapping->DestKey = key;
                entry->Mapping->DestType = type;
                entry->Mapping->DestUser = user;
                entry->Mapping->Data = data;
                UpdateEntryOnDestKeyChange(entry);
            });
            UpdateKeyDepUi(GetConfigEntry(mConfig));
        });

        mTopStrengthLbl = New<Label>();
        mTopStrengthEdit = New<EditFloat<double>>([this](double value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Mapping->Strength = value; });
            UpdateExpandUi(GetConfigEntry(mConfig));
        });
        mTopStrengthEdit->SetRange(0, numeric_limits<double>::infinity(), 0.1);

        mOptionsChk = New<CheckBox>(L"Options...", [this](bool show) {
            mOptionsLay->Display(show);
        });

        mActionDrop = New<DropDownList>([this](int idx) {
            ChangeConfigEntry(mConfig, [&](auto entry) {
                entry->Mapping->Toggle = idx & 1;
                entry->Mapping->Turbo = idx & 2;
            });
            UpdateExpandUi(GetConfigEntry(mConfig));
        });
        mActionDrop->Add(L"Hold output while input is held. (Default)");
        mActionDrop->Add(L"Toggle output when input is pressed.");
        mActionDrop->Add(L"Turbo-press output while input is held.");
        mActionDrop->Add(L"Toggle Turbo-pressing output when input is pressed.");

        mOptStrengthChk = New<CheckBox>(L"Custom Press Strength:", [this](bool value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Info.HasStrength = value; });
            UpdateExpandUi(GetConfigEntry(mConfig));
            mOptStrengthEdit->Enable(value);
        });
        mOptStrengthEdit = New<EditFloat<double>>([this](double value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Mapping->Strength = value; });
            UpdateExpandUi(GetConfigEntry(mConfig));
        });
        mOptStrengthEdit->SetRange(0, numeric_limits<double>::infinity(), 0.1);

        mRateChk = New<CheckBox>(L"Custom Press Rate:", [this](bool value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Info.HasRate = value; });
            UpdateExpandUi(GetConfigEntry(mConfig));
            mRateEdit->Enable(value);
        });
        mRateEdit = New<EditFloat<double>>([this](double value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Mapping->Rate = value; });
            UpdateExpandUi(GetConfigEntry(mConfig));
        });
        mRateEdit->SetRange(0, numeric_limits<double>::infinity(), 0.01);

        mAxisActionDrop = New<DropDownList>([this](int idx) {
            ChangeConfigEntry(mConfig, [&](auto entry) {
                entry->Mapping->Add = idx != 0;
                entry->Mapping->Reset = idx == 2;
            });
            UpdateExpandUi(GetConfigEntry(mConfig));
        });
        mAxisActionDrop->Add(L"Set strength to output. (Default)");
        mAxisActionDrop->Add(L"Add strength to output.");
        mAxisActionDrop->Add(L"Add strength to output; reset on condition end.");

        mForwardChk = New<CheckBox>(L"Forward input in addition to output", [this](bool value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Mapping->Forward = value; });
            UpdateExpandUi(GetConfigEntry(mConfig));
        });
        mReplaceChk = New<CheckBox>(L"Replace previous mappings of input", [this](bool value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Mapping->Replace = value; });
            UpdateExpandUi(GetConfigEntry(mConfig));
        });

        mCondChk = New<CheckBox>(L"Conditions...", [this](bool show) {
            mCondPanel->Display(show);
        });

        mCondPanel = New<ConfigCondsPanel>(mConfig, [this] {
            UpdateExpandUi(GetConfigEntry(mConfig));
        });

        auto srcLayout = New<Layout>();
        srcLayout->AddLeftMiddle(New<Label>(L"Input:"));
        srcLayout->AddLeftMiddle(mSrcKey);

        auto destLayout = New<Layout>();
        destLayout->AddLeftMiddle(New<Label>(L"Output:"));
        destLayout->AddLeftMiddle(mDestKey);

        mTopStrengthLay = New<Layout>();
        mTopStrengthLay->AddLeftMiddle(mTopStrengthLbl);
        mTopStrengthLay->AddLeftMiddle(mTopStrengthEdit);

        mOptStrengthLay = New<Layout>();
        mOptStrengthLay->AddLeftMiddle(mOptStrengthChk);
        mOptStrengthLay->AddLeftMiddle(mOptStrengthEdit);

        auto optRateLay = New<Layout>();
        optRateLay->AddLeftMiddle(mRateChk);
        optRateLay->AddLeftMiddle(mRateEdit);
        optRateLay->AddLeftMiddle(New<Label>(L"Seconds"));

        auto actionLay = New<Layout>();
        actionLay->AddLeft(mActionDrop, 250);
        auto axisActionLay = New<Layout>();
        axisActionLay->AddLeft(mAxisActionDrop, 250);

        mOptionsLay = New<Layout>();
        mOptionsLay->AddTop(actionLay);
        mOptionsLay->AddTop(mOptStrengthLay);
        mOptionsLay->AddTop(optRateLay);
        mOptionsLay->AddTop(axisActionLay);
        mOptionsLay->AddTopLeft(mForwardChk);
        mOptionsLay->AddTopLeft(mReplaceChk);

        auto layout = New<Layout>(true);
        layout->AddTop(srcLayout);
        layout->AddTop(destLayout);
        layout->AddTop(mTopStrengthLay);
        layout->AddTop(New<Separator>());
        layout->AddTopLeft(mOptionsChk);
        layout->AddTop(mOptionsLay, -1, 20);
        layout->AddTopLeft(mCondChk);
        layout->AddTop(mCondPanel, -1, 20);
        return layout;
    }

    void UpdateExpandUi(ConfigEditEntry *entry, bool reset = false) {
        auto uiInfo = GetUiInfo(entry);

        mOptionsChk->Enable(!uiInfo.OptionsNeeded);
        if (uiInfo.OptionsNeeded || reset) {
            mOptionsChk->Set(uiInfo.OptionsNeeded);
            mOptionsLay->Display(uiInfo.OptionsNeeded);
        }

        mCondChk->Enable(!uiInfo.ConditionsNeeded);
        if (uiInfo.ConditionsNeeded || reset) {
            mCondChk->Set(uiInfo.ConditionsNeeded);
            mCondPanel->Display(uiInfo.ConditionsNeeded);
        }
    }

    void UpdateKeyDepUi(ConfigEditEntry *entry, bool reset = false) {
        UpdateExpandUi(entry, reset);

        auto uiInfo = GetUiInfo(entry);

        mTopStrengthLay->Display(uiInfo.HasTopStrength);
        if (uiInfo.HasTopStrength) {
            if (uiInfo.IsModifier) {
                mTopStrengthLbl->SetText(L"Modifier:");
            } else if (uiInfo.IsValue) {
                mTopStrengthLbl->SetText(L"Value:");
            }
        }
        mTopStrengthEdit->Set(entry->Mapping->Strength);

        mOptStrengthLay->Display(!uiInfo.HasTopStrength);
        if (!uiInfo.HasTopStrength) {
            mOptStrengthChk->Set(entry->Info.HasStrength);
            mOptStrengthEdit->Enable(entry->Info.HasStrength);
        }
        mOptStrengthEdit->Set(entry->Mapping->Strength);

        mRateChk->Set(entry->Info.HasRate);
        mRateEdit->Enable(entry->Info.HasRate);
        mRateEdit->Set(entry->Mapping->Rate);

        int action = (entry->Mapping->Toggle ? 1 : 0) | (entry->Mapping->Turbo ? 2 : 0);
        mActionDrop->SetSelected(action, false);

        int axisAction = entry->Mapping->Add ? (entry->Mapping->Reset ? 2 : 1) : 0;
        mAxisActionDrop->SetSelected(axisAction, false);

        mForwardChk->Set(entry->Mapping->Forward);
        mReplaceChk->Set(entry->Mapping->Replace);
    }

public:
    ConfigMappingUiPanel(HWND parent, intptr_t id, ConfigPanel *panel) : Panel(parent, id), mConfig(panel) {}

    void Update(ConfigEditEntry *entry) {
        mSrcKey->Set(entry->Mapping->SrcKey, entry->Mapping->SrcUser, nullptr);
        mDestKey->Set(entry->Mapping->DestKey, entry->Mapping->DestUser, entry->Mapping->Data);
        UpdateKeyDepUi(entry, true);
        mCondPanel->Update(entry);
    }
};
