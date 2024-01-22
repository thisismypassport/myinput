#pragma once
#include "Config.h"

struct ConfigEditVar {
    ConfigVar Var = ConfigVar::Unknown;
    string Name;
    bool IsBool : 1 = false;
    bool BoolValue : 1 = false;
    string StrValue;
    int UserIdx = -1;
};

struct ConfigEditEntry {
    ConfigEditEntry *Parent = nullptr;
    string RawText;
    bool Enabled : 1 = true;
    bool FromRaw : 1 = false;
    bool IsRoot : 1 = false;
    SharedPtr<ConfigEditVar> Variable;
    SharedPtr<ImplMapping> Mapping;
    UniquePtr<vector<UniquePtr<ConfigEditEntry>>> Children;
};

struct ConfigEditKeyOpts {
    bool IsInput : 1 = false;
    bool IsOutput : 1 = false;
    bool PrimaryStrength : 1 = false;
};

struct ConfigEditVarOpts {
    bool IsBool : 1 = false;
};

struct ConfigEdit {
    UniquePtr<ConfigEditEntry> Root;
    ConfigCustomMap CustomMap;
    vector<ConfigEditKeyOpts> CustomKeyOpts;
    vector<ConfigEditVarOpts> CustomVarOpts;
};

SharedPtr<ConfigEditVar> ConfigEditReadVarLine(const ConfigEdit &edit, const string &line, intptr_t idx) {
    auto variable = SharedPtr<ConfigEditVar>::New();
    ConfigReadVarLine(
        edit.CustomMap, line, idx, [&](ConfigVar var, bool value, bool *) {
        variable->Var = var;
        variable->IsBool = true;
        variable->BoolValue = value; }, [&](ConfigVar var, const string &keyLow, const string &val) {
        variable->Var = var;
        variable->StrValue = val;

        switch (var)
        {
        case ConfigVar::Device:
            variable->UserIdx = ConfigReadUser (keyLow.substr (6));
            break;

        case ConfigVar::Plugin:
            // TODO: load
            break;

        case ConfigVar::Include:
            // TODO: load plugins
            break;

        case ConfigVar::Custom:
            variable->Name = keyLow;
            {
                auto& varOpts = edit.CustomVarOpts[edit.CustomMap.Vars[keyLow]];
                if (varOpts.IsBool)
                {
                    variable->IsBool = true;
                    variable->BoolValue = ConfigReadBoolVar (variable->StrValue);
                }
            }
            break;

        case ConfigVar::Unknown:
            variable->Name = keyLow;
            break;
        } });
    return variable;
}

UniquePtr<ConfigEdit> ConfigEditLoad(const wchar_t *path) {
    auto edit = UniquePtr<ConfigEdit>::New();

    edit->Root = UniquePtr<ConfigEditEntry>::New();
    edit->Root->IsRoot = true;
    edit->Root->Children = UniquePtr<vector<UniquePtr<ConfigEditEntry>>>::New();

    std::ifstream file(path);
    if (file.fail()) {
        return edit;
    }

    ConfigEditEntry *parent = edit->Root;
    string line;
    while (getline(file, line)) {
        auto node = UniquePtr<ConfigEditEntry>::New(parent, line);
        node->FromRaw = true;

        intptr_t startIdx = 0;
        intptr_t idx = startIdx;
        string token = ConfigReadToken(line, &idx);

        if (token == "#") {
            startIdx = idx;
            token = ConfigReadToken(line, &idx, true);
            node->Enabled = false;
        }

        if (token.empty() || token == "#") {
            // empty/comment
        } else if (token == "[") {
            node->Children = UniquePtr<vector<UniquePtr<ConfigEditEntry>>>::New();
            node->Variable = SharedPtr<ConfigEditVar>::New();
            node->Variable->Name = ConfigReadToken(line, &idx);
            parent = node;
        } else if (token == "]") {
            if (parent->Parent) {
                parent = parent->Parent;
            } else {
                LOG << "ERROR: Unmatched ']' in config" << END;
            }
        } else if (token == "!") {
            node->Variable = ConfigEditReadVarLine(*edit, line, idx);
        } else {
            node->Mapping = ConfigReadInputLine(edit->CustomMap, line, startIdx);
        }

        parent->Children->push_back(move(node));
    }

    if (parent->Parent) {
        LOG << "ERROR: Unmatched '[' in config" << END;
    }

    return edit;
}

void ConfigEditWriteVar(std::ofstream &file, ConfigEditVar *var) {
#define CONFIG_ON_BOOL_VAR(cv, name, ptr)                              \
    case cv:                                                           \
        file << "!" name " = " << (var->BoolValue ? "true" : "false"); \
        break;

    switch (var->Var) {
        ENUMERATE_CONFIG_BOOL_VARS(CONFIG_ON_BOOL_VAR);

    case ConfigVar::Include:
        file << "!include " << var->StrValue;
        break;

    case ConfigVar::Device:
        file << "!device" << var->UserIdx << " " << var->StrValue;
        break;

    case ConfigVar::Plugin:
        file << "!plugin " << var->StrValue;
        break;

    case ConfigVar::Custom: // assuming !IsBool/etc
    case ConfigVar::Unknown:
        file << "!" << var->Name << " = " << var->StrValue;
        break;
    }

#undef CONFIG_ON_BOOL_VAR
}

void ConfigEditWriteInput(std::ofstream &file, ImplMapping *input) {
    // ... TODO
}

bool ConfigEditSave(const wchar_t *path, ConfigEditEntry *root) {
    std::ofstream file(path);

    auto saveRec = [&](ConfigEditEntry *node, auto &saveRecF) -> void {
        if (node->IsRoot) {
            // nothing to do
        } else if (node->FromRaw) {
            file << node->RawText;
        } else {
            if (!node->Enabled) {
                file << "#";
            }

            if (node->Children) {
                file << "[ " << node->Variable->Name;
            } else if (node->Variable) {
                ConfigEditWriteVar(file, node->Variable);
            } else if (node->Mapping) {
                ConfigEditWriteInput(file, node->Mapping);
            } else {
                Fatal("Invalid new config line");
            }
        }

        file << "\n";

        if (node->Children) {
            for (auto &child : *node->Children) {
                saveRecF(child, saveRecF);
            }

            if (!node->IsRoot) {
                if (!node->Enabled) {
                    file << "#";
                }
                file << "]\n";
            }
        }
    };

    saveRec(root, saveRec);

    return !file.fail();
}
