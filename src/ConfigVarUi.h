#pragma once
#include "CommonUi.h"
#include "ConfigEdit.h"

class ConfigErrorUiPanel : public Panel {
    Control *OnCreate() {
        Label *errorLbl = New<StyledLabel>(L"Line contains errors - cannot edit.", RGB(0xff, 0, 0));

        auto layout = New<Layout>(true);
        layout->AddTopLeft(errorLbl);
        return layout;
    }

public:
    using Panel::Panel;
};

class ConfigParentUiPanel : public Panel {
    ConfigPanel *mConfig;
    EditLine *mNameEdit = nullptr;

    Control *OnCreate() {
        Label *nameLbl = New<Label>(L"Name:");
        mNameEdit = New<EditLine>([this] {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Group->Name = ToStdStr(mNameEdit->Get()); });
        });

        Layout *topLay = New<Layout>();
        topLay->AddLeftMiddle(nameLbl);
        topLay->AddLeftMiddle(mNameEdit, 200);

        auto layout = New<Layout>(true);
        layout->AddTop(topLay);
        return layout;
    }

public:
    ConfigParentUiPanel(HWND parent, intptr_t id, ConfigPanel *panel) : Panel(parent, id), mConfig(panel) {}

    void Update(ConfigEditEntry *entry) {
        mNameEdit->Set(PathFromStr(entry->Group->Name.c_str()));
    }
};

class ConfigVariableUiPanel : public Panel {
    class VarTypePopupWindow : public PopupWindow {
        ConfigVariableUiPanel *mParent;
        TreeView *mTree;

        void Add(TreeNode parent, ConfigVar var) {
            auto node = mTree->Add(parent, mParent->GetVariableDesc(var), (void *)var);

            if (var == mParent->mVarType) {
                mTree->SetSelected(node, false);
            }
        }

        TreeNode AddPluginsAndDebug() {
            for (auto &plugin : GPlugins.Plugins) {
                auto node = mTree->Add(nullptr, PathFromStr(("Plugin '" + plugin.Name + "'").c_str()));
                plugin.TempUserData = node;
            }

            int varIdx = 0;
            for (auto &var : GPlugins.Vars) {
                auto node = (TreeNode)GPlugins.Plugins[var.PluginIdx].TempUserData;
                Add(node, ConfigVar::CustomStart + (varIdx++));
            }

            return mTree->Add(nullptr, L"Logging & Debug");
        }

        void Create(POINT pos) override {
            PopupWindow::Create(pos, SIZE{350, 400});
        }

    public:
        VarTypePopupWindow(HWND parent, intptr_t id, ConfigVariableUiPanel *panel) : PopupWindow(parent, id), mParent(panel) {}

        Control *OnCreate() override {
            mTree = New<TreeView>([this](TreeNode node) {
                void *data = mTree->GetData(node);
                if (data) {
                    auto entry = GetConfigEntry(mParent->mConfig);
                    if (entry) {
                        auto var = (ConfigVar)(uintptr_t)data;
                        mParent->SetVariableDefault(var, entry);
                        mParent->UpdateType(entry, var);
                    }
                    Destroy();
                }
            });

            TreeNode dbgParent = nullptr;

#define CONFIG_ON_VAR(cv, pname, desc, name, flags, ptr)                     \
    if constexpr (flags & CONFIG_VAR_GROUP_DEBUG)                            \
        Add(dbgParent ? dbgParent : (dbgParent = AddPluginsAndDebug()), cv); \
    else                                                                     \
        Add(nullptr, cv);

            ENUMERATE_CONFIG_VARS(CONFIG_ON_VAR);
#undef CONFIG_ON_VAR

            return mTree;
        }
    };

    ConfigPanel *mConfig;
    ConfigVar mVarType;

    DropDownPopup *mTypeDrop = nullptr;
    VarTypePopupWindow *mTypePopup = nullptr;
    Dynamic *mValueDynamic = nullptr;
    CheckBox *mValueCheck = nullptr;
    EditLine *mValueEdit = nullptr;
    Layout *mValueEditLayout = nullptr;
    DropDownEditLine *mValueConfig = nullptr;
    Layout *mValueConfigLayout = nullptr;
    DropDownList *mValueDevice = nullptr;
    EditInt<user_t> *mValueDeviceUser = nullptr;
    Layout *mValueDeviceLayout = nullptr;

    const wchar_t *GetVariableDesc(ConfigVar var) {
        switch (var) {
#define CONFIG_ON_VAR(cv, pname, desc, name, flags, ptr) \
    case cv:                                             \
        return desc;
            ENUMERATE_CONFIG_VARS(CONFIG_ON_VAR);
#undef CONFIG_ON_VAR

        case ConfigVar::Unknown:
            return L"(Choose One)";

        default:
            if (var >= ConfigVar::CustomStart) {
                return GPlugins.Vars[var - ConfigVar::CustomStart].Description.c_str();
            } else {
                return L"???";
            }
        }
    }

    Control *GetVariableUi(ConfigVar var) {
        switch (var) {
#define CONFIG_ON_VAR(cv, pname, desc, name, flags, ptr) \
    case cv:                                             \
        if constexpr (flags & CONFIG_VAR_BOOL)           \
            return mValueCheck;                          \
        if constexpr (flags & CONFIG_VAR_STR)            \
            return mValueEditLayout;                     \
        break;

            ENUMERATE_CONFIG_VARS(CONFIG_ON_VAR);
#undef CONFIG_ON_VAR
        }

        switch (var) {
        case ConfigVar::Include:
            return mValueConfigLayout;
        case ConfigVar::Device:
            return mValueDeviceLayout;
        case ConfigVar::Unknown:
            return nullptr;

        default:
            if (var >= ConfigVar::CustomStart && GPlugins.Vars[var - ConfigVar::CustomStart].IsBool) {
                return mValueCheck;
            } else {
                return mValueEditLayout;
            }
        }
    }

    void SetVariableDefault(ConfigVar var, ConfigEditEntry *entry) {
        entry->Variable->BoolValue = false;
        entry->Variable->StrValue = "";

        switch (var) {
#define CONFIG_ON_VAR(cv, pname, desc, name, flags, ptr) \
    case cv:                                             \
        if constexpr (flags & CONFIG_VAR_BOOL)           \
            entry->Variable->BoolValue = *(bool *)ptr;   \
        break;

            ENUMERATE_CONFIG_VARS(CONFIG_ON_VAR);
#undef CONFIG_ON_VAR

        default:
            if (var >= ConfigVar::CustomStart) {
                auto &pluginVar = GPlugins.Vars[var - ConfigVar::CustomStart];
                if (pluginVar.IsBool) {
                    entry->Variable->BoolValue = pluginVar.DefaultBool;
                } else {
                    entry->Variable->StrValue = pluginVar.DefaultStr;
                }
            }
        }

        switch (var) {
        case ConfigVar::Device:
            entry->Variable->MiscValue = (uintptr_t)ConfigDevice::XBox;
            entry->Variable->UserIdx = 0;
            break;
        }
    }

    Control *OnCreate() {
        mTypePopup = New<VarTypePopupWindow>(this);
        mTypeDrop = New<DropDownPopup>(mTypePopup);

        mValueDynamic = New<Dynamic>();

        mValueCheck = New<CheckBox>(L"On", [this](bool value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Variable->BoolValue = value; });
        });
        mValueEdit = New<EditLine>([this] {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Variable->StrValue = ToStdStr(mValueEdit->Get()); });
        });
        mValueConfig = New<DropDownEditLine>([this] {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Variable->StrValue = ToStdStr(mValueConfig->Get()); });
        });

        mValueDevice = New<DropDownList>([this](int sel) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Variable->MiscValue = (uintptr_t)mValueDevice->GetData(sel); });
        });
        mValueDeviceUser = New<EditInt<user_t>>([this](user_t value) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Variable->UserIdx = value - 1; });
        });
        mValueDeviceUser->SetRange(1, IMPL_MAX_USERS);

        mValueEditLayout = New<Layout>();
        mValueEditLayout->AddLeft(mValueEdit, 250);

        mValueConfigLayout = New<Layout>();
        mValueConfigLayout->AddLeft(mValueConfig, 250);

        mValueDeviceLayout = New<Layout>();
        mValueDeviceLayout->AddLeftMiddle(mValueDevice, 175);
        mValueDeviceLayout->AddLeftMiddle(New<Label>(L"for gamepad #"), -1, 0, 0);
        mValueDeviceLayout->AddLeftMiddle(mValueDeviceUser, 25);

        auto typeLay = New<Layout>();
        typeLay->AddLeftMiddle(New<Label>(L"Option:"));
        typeLay->AddLeftMiddle(mTypeDrop, 250);

        auto valLay = New<Layout>();
        valLay->AddLeftMiddle(New<Label>(L"Value:"));
        valLay->AddRemaining(mValueDynamic);

        auto layout = New<Layout>(true);
        layout->AddTop(typeLay);
        layout->AddTop(nullptr, 5);
        layout->AddTop(valLay);
        return layout;
    }

    void AddDevices(DropDownList *devices, ConfigDevice currDev) {
        auto add = [&](ConfigDevice dev, const wchar_t *desc) {
            int idx = devices->Add(desc, (void *)dev);
            if (dev == currDev) {
                devices->SetSelected(idx, false);
            }
        };

#define CONFIG_ON_DEVICE(cv, pname, desc, name, ...) add(cv, desc);
        ENUMERATE_CONFIG_DEVICES(CONFIG_ON_DEVICE);
#undef CONFIG_ON_DEVICE

        int devIdx = 0;
        for (auto &dev : GPlugins.Devices) {
            add(ConfigDevice::CustomStart + (devIdx++),
                (ToStdWStr("(Plugin '" + GPlugins.Plugins[dev.PluginIdx].Name + "') ") + dev.Description).c_str());
        }
    }

    void UpdateType(ConfigEditEntry *entry, ConfigVar var, bool action = true) {
        mVarType = var;
        mTypeDrop->SetSelected(GetVariableDesc(var));

        Control *varUi = GetVariableUi(var);
        mValueDynamic->SetChild(varUi);

        if (varUi == mValueCheck) {
            mValueCheck->Set(entry->Variable->BoolValue);
        } else if (varUi == mValueEditLayout) {
            mValueEdit->Set(PathFromStr(entry->Variable->StrValue.c_str()));
        } else if (varUi == mValueConfigLayout) {
            mValueConfig->Clear();
            for (auto &config : ConfigNames.GetNames()) {
                mValueConfig->Add(config);
            }

            mValueConfig->Set(PathFromStr(entry->Variable->StrValue.c_str()));
        } else if (varUi == mValueDeviceLayout) {
            mValueDevice->Clear();
            AddDevices(mValueDevice, (ConfigDevice)entry->Variable->MiscValue);

            mValueDeviceUser->Set(entry->Variable->UserIdx + 1);
        }

        if (action) {
            ChangeConfigEntry(mConfig, [&](auto entry) { entry->Variable->Var = var; });
        }
    }

public:
    ConfigVariableUiPanel(HWND parent, intptr_t id, ConfigPanel *panel) : Panel(parent, id), mConfig(panel) {}

    void Update(ConfigEditEntry *entry) {
        UpdateType(entry, entry->Variable->Var, false);

        mTypeDrop->Enable(entry->Variable->Var == ConfigVar::Unknown);
    }
};
