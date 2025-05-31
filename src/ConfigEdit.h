#pragma once
#include "ConfigRead.h"
#include <fstream>

#define CONFIG_UNKNOWN_PNAME "TBD"

struct ConfigEditVar {
    ConfigVar Var = ConfigVar::Unknown;
    bool BoolValue = false;
    user_t UserIdx = -1;
    uintptr_t MiscValue = 0;
    string StrValue;
};

struct ConfigEditEntry;

struct ConfigEditGroup {
    ConfigEditEntry *Start = nullptr;
    string Name;
    UniquePtr<ConfigEditEntry> FirstChild;
    UniquePtr<ConfigEditEntry> End;
    bool GetEnabled();
};

struct ConfigEditEntry {
    string RawText;
    bool Enabled : 1 = true;
    bool ParentsEnabled : 1 = true;
    ConfigAuxInfo Info;
    UniquePtr<ConfigEditVar> Variable;
    SharedPtr<ImplMapping> Mapping;
    UniquePtr<ConfigEditGroup> Group;
    ConfigEditGroup *Parent = nullptr;
    ConfigEditEntry *Prev = nullptr;
    UniquePtr<ConfigEditEntry> Next;
    void *UserData = nullptr;

    static void CopyConds(SharedPtr<ImplCond> &ref) {
        if (ref) {
            ref = SharedPtr<ImplCond>::New(*ref);
            CopyConds(ref->Child);
            CopyConds(ref->Next);
        }
    }

    void CopyStateTo(ConfigEditEntry *dest) {
        dest->RawText = RawText;
        dest->Info = Info;
        dest->Variable = Variable ? UniquePtr<ConfigEditVar>::New(*Variable) : nullptr;
        dest->Mapping = Mapping ? SharedPtr<ImplMapping>::New(*Mapping) : nullptr;
        if (dest->Mapping) {
            CopyConds(dest->Mapping->Conds);
        }
        dest->Group = Group ? UniquePtr<ConfigEditGroup>::New(dest, Group->Name) : nullptr;
    }

    void SwapStateWith(ConfigEditEntry *dest) {
        swap(dest->RawText, RawText);
        swap(dest->Info, Info);
        swap(dest->Variable, Variable);
        swap(dest->Mapping, Mapping);
        if (Group && dest->Group) {
            swap(dest->Group->Name, Group->Name);
        }
    }

    static ConfigEditEntry *Insert(ConfigEditGroup *parent, ConfigEditEntry *prev, UniquePtr<ConfigEditEntry> &&entry) {
        ConfigEditEntry *entryPtr = entry;
        entry->Parent = parent;
        entry->Prev = prev;
        if (prev) {
            entry->Next = move(prev->Next);
            if (entry->Next) {
                entry->Next->Prev = entry;
            }
            prev->Next = move(entry);
        } else {
            entry->Next = move(parent->FirstChild);
            if (entry->Next) {
                entry->Next->Prev = entry;
            }
            parent->FirstChild = move(entry);
        }
        return entryPtr;
    }

    UniquePtr<ConfigEditEntry> Remove() {
        UniquePtr<ConfigEditEntry> self;
        if (Next) {
            Next->Prev = Prev;
        }
        if (Prev) {
            self = move(Prev->Next);
            Prev->Next = move(Next);
        } else {
            self = move(Parent->FirstChild);
            Parent->FirstChild = move(Next);
        }

        Parent = nullptr;
        Prev = nullptr;
        return self;
    }
};

bool ConfigEditGroup::GetEnabled() {
    return Start ? (Start->ParentsEnabled & Start->Enabled) : true;
}

struct ConfigEditPlugin {
    string Name;
    void *TempUserData = nullptr;
    wstring Description;
};

struct ConfigEditPluginDef {
    string OrigName;
    int PluginIdx = -1;
    wstring Description;
};

struct ConfigEditPluginKey : public ConfigEditPluginDef {
    bool IsInput = false;
    bool IsOutput = false;
    bool HasStrength = false;
    bool HasData = false;
};

struct ConfigEditPluginVar : public ConfigEditPluginDef {
    bool IsBool = false;
    bool DefaultBool = false;
    string DefaultStr;
};

struct ConfigEditPluginDevice : public ConfigEditPluginDef {};

struct ConfigEditPlugins {
    ConfigCustom Custom;
    vector<ConfigEditPlugin> Plugins;
    vector<ConfigEditPluginKey> Keys;
    vector<ConfigEditPluginVar> Vars;
    vector<ConfigEditPluginDevice> Devices;
} GPlugins;

template <typename ConfigEditPluginT>
ConfigEditPluginT &CongigEditAddPluginObject(vector<ConfigEditPluginT> &vecRef, unordered_map<string, int> &mapRef,
                                             int pluginIdx, const Path &origName) {
    int idx = (int)vecRef.size();
    vecRef.push_back(ConfigEditPluginT{});

    auto &ref = vecRef[idx];
    ref.OrigName = ToStdStr(origName);
    ref.PluginIdx = pluginIdx;

    string nameLow = ConfigNormStr(ref.OrigName);
    mapRef[nameLow] = idx;
    return ref;
}

void ConfigEditReadPluginDef(ConfigPlugin *plugin, const string &name, std::wifstream &file) {
    int pluginIdx = (int)GPlugins.Plugins.size();
    GPlugins.Plugins.push_back(ConfigEditPlugin{name});

    wstring line;
    while (getline(file, line)) {
        int numArgs;
        LPWSTR *args = CommandLineToArgvW(line.c_str(), &numArgs);

        if (numArgs >= 2) {
            if (tstreq(args[0], L"Key")) {
                auto &key = CongigEditAddPluginObject(GPlugins.Keys, plugin->Keys, pluginIdx, args[1]);

                for (int argI = 2; argI < numArgs; argI++) {
                    if (tstreq(args[argI], L"-i")) {
                        key.IsInput = true;
                    } else if (tstreq(args[argI], L"-o")) {
                        key.IsOutput = true;
                    } else if (tstreq(args[argI], L"-s")) {
                        key.HasStrength = true;
                    } else if (tstreq(args[argI], L"-d")) {
                        key.HasData = true;
                    } else if (tstreq(args[argI], L"-D") && argI + 1 < numArgs) {
                        key.Description = args[++argI];
                    }
                }
            } else if (tstreq(args[0], L"Var")) {
                auto &var = CongigEditAddPluginObject(GPlugins.Vars, plugin->Vars, pluginIdx, args[1]);

                for (int argI = 2; argI < numArgs; argI++) {
                    if (tstreq(args[argI], L"-b")) {
                        var.IsBool = true;
                    } else if (tstreq(args[argI], L"-d") && argI + 1 < numArgs) {
                        var.DefaultStr = ToStdStr(args[++argI]);
                    } else if (tstreq(args[argI], L"-D") && argI + 1 < numArgs) {
                        var.Description = args[++argI];
                    }
                }

                if (var.IsBool && !var.DefaultStr.empty()) {
                    var.DefaultBool = ConfigReadBoolVar(var.DefaultStr, nullptr);
                }
            } else if (tstreq(args[0], L"Device")) {
                auto &dev = CongigEditAddPluginObject(GPlugins.Devices, plugin->Devices, pluginIdx, args[1]);

                for (int argI = 2; argI < numArgs; argI++) {
                    if (tstreq(args[argI], L"-D") && argI + 1 < numArgs) {
                        dev.Description = args[++argI];
                    }
                }
            } else if (tstreq(args[0], L"Plugin")) {
                auto &plugin = GPlugins.Plugins[pluginIdx];

                for (int argI = 1; argI < numArgs; argI++) {
                    if (tstreq(args[argI], L"-D") && argI + 1 < numArgs) {
                        plugin.Description = args[++argI];
                    }
                }
            }
        }

        LocalFree(args);
    }
}

bool ConfigLoadPlugin(ConfigPlugin *plugin, const string &name) {
    Path path = ConfigReadPlugin(name, PathGetDirName(PathGetModulePath(nullptr)));
    if (!path) {
        return false;
    }

    Path defPath = PathCombine(path, PathFromStr((name + ".def").c_str()));
    std::wifstream file(defPath);
    if (file.fail()) {
        LOG << "Plugin " << name << " wasn't found or has no .def file" << END;
        return false;
    }

    ConfigEditReadPluginDef(plugin, name, file);
    return true;
}

void ConfigEditLoadAllPlugins() {
    Path path = ConfigReadPlugin("*", PathGetDirName(PathGetModulePath(nullptr)));

    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(path, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                auto pluginName = ToStdStr(data.cFileName);
                ConfigLoadPlugin(GPlugins.Custom, pluginName);
            }
        } while (FindNextFileW(handle, &data));
        FindClose(handle);
    }
}

UniquePtr<ConfigEditVar> ConfigEditReadVarLine(const string &line, intptr_t idx, ConfigAuxInfo *auxInfo) {
    auto variable = UniquePtr<ConfigEditVar>::New();
    ConfigReadVarLine(
        GPlugins.Custom, line, idx, [&](ConfigVar var, bool value, bool *) {
        variable->Var = var;
        variable->BoolValue = value; }, [&](ConfigVar var, const string &val, user_t userIdx) {
        variable->Var = var;
        variable->StrValue = val;
        variable->UserIdx = userIdx;

        switch (var)
        {
        case ConfigVar::Device:
            variable->MiscValue = (uintptr_t) ConfigReadDeviceName (GPlugins.Custom, val, auxInfo);
            break;

        case ConfigVar::StickShape:
            variable->MiscValue = (uintptr_t) ConfigReadStickShape (val, auxInfo);
            break;

        default:
            if (var >= ConfigVar::CustomStart)
            {
                auto& custVar = GPlugins.Vars[var - ConfigVar::CustomStart];
                if (custVar.IsBool){
                    variable->BoolValue = ConfigReadBoolVar (variable->StrValue, auxInfo);
}
            }
            break;
        } }, auxInfo);
    return variable;
}

UniquePtr<ConfigEditGroup> ConfigEditLoad(std::istream &in) {
    auto root = UniquePtr<ConfigEditGroup>::New();

    ConfigEditGroup *parent = root;
    ConfigEditEntry *sibling = nullptr;
    bool parentsEnabled = true;
    string line;
    while (getline(in, line)) {
        auto node = UniquePtr<ConfigEditEntry>::New(line);
        node->ParentsEnabled = parentsEnabled;

        intptr_t startIdx = 0;
        intptr_t idx = startIdx;
        string token = ConfigReadToken(line, &idx);

        ConfigEditGroup *nextParent = parent;
        ConfigEditEntry *nextSibling = node;

        if (token == "#") {
            startIdx = idx;
            token = ConfigReadToken(line, &idx, true);
            node->Enabled = false;
        }

        if (token.empty() || token == "#") {
            // empty/comment
        } else if (token == "[") {
            ConfigSeekToken(line, &idx);
            node->Group = UniquePtr<ConfigEditGroup>::New(node.get(), line.substr(idx));
            nextParent = node->Group;
            nextSibling = nullptr;
            parentsEnabled &= node->Enabled;
        } else if (token == "]") {
            if (parent->Start) {
                parent->End = move(node);
                nextParent = parent->Start->Parent;
                nextSibling = parent->Start;
                parent = nullptr;
                parentsEnabled = nextParent->GetEnabled();
            } else {
                LOG_W << "ERROR: Unmatched ']' in config" << END;
            }
        } else if (token == "!") {
            node->Variable = ConfigEditReadVarLine(line, idx, &node->Info);
        } else {
            node->Mapping = ConfigReadInputLine(GPlugins.Custom, line, startIdx, &node->Info);
        }

        if (parent) {
            ConfigEditEntry::Insert(parent, sibling, move(node));
        }
        parent = nextParent;
        sibling = nextSibling;
    }

    if (parent->Start) {
        LOG_W << "ERROR: Unmatched '[' in config" << END;
    }

    return root;
}

UniquePtr<ConfigEditGroup> ConfigEditLoad(const wchar_t *path) {
    std::ifstream file(path);
    if (file.fail()) {
        return nullptr;
    }

    return ConfigEditLoad(file);
}

template <typename TPluginData>
string ConfigEditPrefixWithPlugin(TPluginData &data) {
    return GPlugins.Plugins[data.PluginIdx].Name + "/" + data.OrigName;
}

void ConfigEditWriteUser(std::ostringstream &out, int user, bool weld = false) {
    if (user >= 0) {
        out << (weld ? "@" : " @") << (user + 1);
    }
}

void ConfigEditWriteDevice(std::ostringstream &out, ConfigDevice devType) {
    switch (devType) {
#define CONFIG_ON_DEVICE(cv, pname, desc, name, ...) \
    case cv:                                         \
        out << pname;                                \
        break;
        ENUMERATE_CONFIG_DEVICES(CONFIG_ON_DEVICE);
#undef CONFIG_ON_DEVICE

    default:
        if (devType >= ConfigDevice::CustomStart) {
            auto &pluginDev = GPlugins.Devices[devType - ConfigDevice::CustomStart];
            out << ConfigEditPrefixWithPlugin(pluginDev);
        } else {
            out << CONFIG_UNKNOWN_PNAME;
        }
        break;
    }
}

void ConfigEditWriteStickShape(std::ostringstream &out, ImplStickShape shape) {
    switch (shape) {
#define CONFIG_ON_SHAPE(cv, pname, desc, name, ...) \
    case cv:                                        \
        out << pname;                               \
        break;
        ENUMERATE_STICK_SHAPES(CONFIG_ON_SHAPE);
#undef CONFIG_ON_SHAPE

    default:
        out << "default";
        break;
    }
}

void ConfigEditWriteVar(std::ostringstream &out, ConfigEditVar *var) {
    int varFlags = 0;
    switch (var->Var) {
#define CONFIG_ON_VAR(cv, pname, desc, name, flags, ptr) \
    case cv:                                             \
        varFlags = flags;                                \
        out << "!" pname;                                \
        break;

        ENUMERATE_CONFIG_VARS(CONFIG_ON_VAR);
#undef CONFIG_ON_VAR

    default:
        if (var->Var >= ConfigVar::CustomStart) {
            auto &pluginVar = GPlugins.Vars[var->Var - ConfigVar::CustomStart];
            out << "!" << ConfigEditPrefixWithPlugin(pluginVar);
            if (pluginVar.IsBool) {
                varFlags |= CONFIG_VAR_BOOL;
            } else {
                varFlags |= CONFIG_VAR_STR;
            }
            break;
        }

        out << "!" CONFIG_UNKNOWN_PNAME;
        break;
    }

    ConfigEditWriteUser(out, var->UserIdx);

    if (varFlags & CONFIG_VAR_NO_EQUAL) {
        out << " ";
    } else {
        out << " = ";
    }

    switch (var->Var) {
    case ConfigVar::Device:
        ConfigEditWriteDevice(out, (ConfigDevice)var->MiscValue);
        break;

    case ConfigVar::StickShape:
        ConfigEditWriteStickShape(out, (ImplStickShape)var->MiscValue);
        break;

    default:
        if (varFlags & CONFIG_VAR_BOOL) {
            out << (var->BoolValue ? "True" : "False");
        } else {
            out << var->StrValue;
        }
        break;
    }
}

void ConfigEditWriteKey(std::ostringstream &out, key_t key) {
    switch (key) {
#define CONFIG_ON_KEY(vk, pname, desc, group, name, ...) \
    case vk:                                             \
        out << pname;                                    \
        break;
        ENUMERATE_KEYS_WITHOUT_SIMPLE(CONFIG_ON_KEY);
#undef CONFIG_ON_KEY

    default:
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
            out << (char)key;
        } else if (key >= MY_VK_CUSTOM_START && key < MY_VK_CUSTOM_START + (int)GPlugins.Keys.size()) {
            out << ConfigEditPrefixWithPlugin(GPlugins.Keys[key - MY_VK_CUSTOM_START]);
        } else {
            out << CONFIG_UNKNOWN_PNAME;
        }
        break;
    }
}

void ConfigEditWriteCond(std::ostringstream &out, ImplCond *cond, bool nested = false, bool recursive = true) {
    if (cond->Toggle) {
        out << "^";
    }
    if (!cond->State) {
        out << "~";
    }

    if (cond->Key == MY_VK_META_COND_AND || cond->Key == MY_VK_META_COND_OR) {
        auto writeMeta = [&] {
            if (cond->Key == MY_VK_META_COND_AND) {
                out << " & ";
            } else if (cond->Key == MY_VK_META_COND_OR) {
                out << " | ";
            }
        };

        out << "(";
        if (recursive) {
            ImplCond *child = cond->Child;
            while (child) {
                ConfigEditWriteCond(out, child, true);
                child = child->Next;

                if (child) {
                    writeMeta();
                }
            }
        } else {
            writeMeta();
        }
        out << ")";
    } else {
        ConfigEditWriteKey(out, cond->Key);
        ConfigEditWriteUser(out, cond->User, !nested);
    }
}

void ConfigEditWriteInput(std::ostringstream &out, ImplMapping *input, ConfigAuxInfo *info) {
    ConfigEditWriteKey(out, input->SrcKey);
    ConfigEditWriteUser(out, input->SrcUser);

    out << " : ";

    ConfigEditWriteKey(out, input->DestKey);
    ConfigEditWriteUser(out, input->DestUser);

    if (info->HasStrength) {
        out << " ~ " << input->Strength;
    }
    if (info->HasRate) {
        out << " ^ " << input->Rate;
    }

    auto cond = input->Conds;
    while (cond) {
        out << " ?";
        ConfigEditWriteCond(out, cond);
        cond = cond->Next;
    }

    if (input->Turbo) {
        out << " !Turbo";
    }
    if (input->Toggle) {
        out << " !Toggle";
    }
    if (input->Add) {
        out << " !Add";
    }
    if (input->Reset) {
        out << " !Reset";
    }
    if (input->Forward) {
        out << " !Forward";
    }
    if (input->Replace) {
        out << " !Replace";
    }

    if (input->Data) {
        out << " = " << *input->Data; // must be last
    }
}

void ConfigEditUpdate(ConfigEditEntry *node) {
    std::ostringstream out;

    if (!node->Enabled) {
        out << "#";
    }

    if (node->Group) {
        out << "[ " << node->Group->Name;
    } else if (node->Variable) {
        ConfigEditWriteVar(out, node->Variable);
    } else if (node->Mapping) {
        ConfigEditWriteInput(out, node->Mapping, &node->Info);
    }

    node->RawText = move(out).str();
}

void ConfigEditUpdateEnabled(ConfigEditEntry *node) {
    if (!node->Enabled && !node->RawText.starts_with("#")) {
        node->RawText.insert(0, "#");
    } else if (node->Enabled && node->RawText.starts_with("#")) {
        node->RawText.erase(0, 1);
    }
}

void ConfigEditSave(std::ostream &out, ConfigEditGroup *root) {
    auto saveGroupRec = [&](ConfigEditGroup *group, auto &saveEntryRecF) -> void {
        ConfigEditEntry *child = group->FirstChild;
        while (child) {
            saveEntryRecF(child, saveEntryRecF);
            child = child->Next;
        }

        if (group->End) {
            saveEntryRecF(group->End, saveEntryRecF);
        }
    };

    auto saveEntryRec = [&](ConfigEditEntry *node, auto &saveEntryRecF) -> void {
        out << node->RawText << "\n";

        if (node->Group) {
            saveGroupRec(node->Group, saveEntryRecF);
        }
    };

    saveGroupRec(root, saveEntryRec);
}

bool ConfigEditSave(const wchar_t *path, ConfigEditGroup *root) {
    std::ofstream file(path);
    ConfigEditSave(file, root);
    return !file.fail();
}
