#pragma once
#include "State.h"
#include "Devices.h"
#include <fstream>

struct ConfigState {
    list<Path> LoadedFiles;
    unordered_set<wstring_view> LoadedFilesSet;
    Path MainFile;
    Path Directory;
} GConfig;

bool ConfigLoadFrom(const wchar_t *filename);

string ConfigReadToken(const string &str, intptr_t *startPtr) {
    intptr_t start = *startPtr;
    while ((size_t)start < str.size() && isspace(str[start])) {
        start++;
    }

    intptr_t end = start;
    while ((size_t)end < str.size() && !isspace(str[end])) {
        char ch = str[end];
        if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '%') {
            if (start == end) {
                end++;
            }
            break;
        }
        end++;
    }

    *startPtr = end;
    return str.substr(start, end - start);
}

int ConfigReadKey(const string &str, bool output = false) {
#define CONFIG_ON_KEY(vk, name, ...) \
    if (strLow == name)              \
        return vk;                   \
    __VA_OPT__(if (strLow == __VA_ARGS__) return vk;)

    string strLow = StrLowerCase(str);
    StrReplaceAll(strLow, ".", "");

    if (strLow.starts_with('%') && output) {
        ENUMERATE_PAD_VKS(CONFIG_ON_KEY);
    } else {
        if (strLow.size() == 1 && ((strLow[0] >= 'a' && strLow[0] <= 'z') || (strLow[0] >= '0' && strLow[0] <= '9'))) {
            return toupper(strLow[0]);
        }

        ENUMERATE_MISC_VKS(CONFIG_ON_KEY);
    }

    if (output) {
        ENUMERATE_CMD_VKS(CONFIG_ON_KEY);
    }

    LOG << "ERROR: Invalid key: " << str << END;
    return 0;
}

int ConfigReadUser(const string &str) {
    if (str.empty()) {
        return -1;
    }

    int value;
    if (StrToValue(str, &value) && value >= 1 && value <= IMPL_MAX_USERS) {
        return value - 1;
    }

    LOG << "ERROR: Invalid user (must be between 1 and " << IMPL_MAX_USERS << "): " << str << END;
    return -1;
}

double ConfigReadStrength(const string &str) {
    if (str.empty()) {
        return 1;
    }

    double value;
    if (StrToValue(str, &value) && value > 0) {
        return value;
    }

    LOG << "ERROR: Invalid strength (must be number larger than 0): " << str << END;
    return 1;
}

double ConfigReadRate(const string &str) {
    if (!str.empty()) {
        double value;
        if (StrToValue(str, &value) && value > 0) {
            return value;
        }

        LOG << "ERROR: Invalid rate (must be number greater than 0) : " << str << END;
    }
    return 0.02;
}

SharedPtr<ImplCond> ConfigReadCond(const string &line, intptr_t *idxPtr) {
    string condStr = ConfigReadToken(line, idxPtr);
    auto cond = SharedPtr<ImplCond>::New();
    cond->State = true;

    while (true) {
        if (condStr == "^") {
            cond->Toggle = true;
        } else if (condStr == "~") {
            cond->State = false;
        } else {
            break;
        }

        condStr = ConfigReadToken(line, idxPtr);
    }

    if (condStr == "(") {
        int condType = 0;
        while (true) {
            auto subcond = ConfigReadCond(line, idxPtr);
            subcond->Next = cond->Child;
            cond->Child = subcond;

            string token = ConfigReadToken(line, idxPtr);
            int nextType = 0;
            if (token == "&") {
                nextType = MY_VK_META_COND_AND;
            } else if (token == "|") {
                nextType = MY_VK_META_COND_OR;
            } else if (token == ")") {
                break;
            }

            if (nextType == 0) {
                LOG << "ERROR: Ignoring unknown token in condition: " << token << END;
                while ((size_t)*idxPtr < line.size() && ConfigReadToken(line, idxPtr) != ")") {
                }
                break;
            } else {
                if (condType == 0) {
                    condType = nextType;
                } else if (condType != nextType) {
                    LOG << "ERROR: Mixing & and | without parentheses in condition" << END;
                }
            }
        }

        cond->Key = condType ? condType : MY_VK_META_COND_AND;
    } else {
        cond->Key = ConfigReadKey(condStr);

        if (GetKeyType(cond->Key).Pair) {
            auto cond1 = SharedPtr<ImplCond>::New(*cond);
            auto cond2 = SharedPtr<ImplCond>::New(*cond);
            tie(cond1->Key, cond2->Key) = GetKeyPair(cond->Key);
            cond1->State = cond2->State = true; // State used by cond

            cond->Key = MY_VK_META_COND_OR;
            cond->Child = cond1;
            cond1->Next = cond2;
        }
    }

    return cond;
}

SharedPtr<ImplMapping> ConfigReadInputLine(const string &line) {
    auto cfg = SharedPtr<ImplMapping>::New();

    intptr_t idx = 0;
    string inputStr = ConfigReadToken(line, &idx);
    string colon = ConfigReadToken(line, &idx);
    string outputStr = ConfigReadToken(line, &idx);

    if (colon != ":") {
        LOG << "ERROR: ':' expected after key name in: " << line << END;
        return nullptr;
    }

    string userStr, strengthStr, rateStr;
    while (true) {
        string token = ConfigReadToken(line, &idx);
        if (token.empty()) {
            break;
        }

        if (token == "!") {
            string optStr = StrLowerCase(ConfigReadToken(line, &idx));
            if (optStr == "replace") {
                cfg->Replace = true;
            } else if (optStr == "forward") {
                cfg->Forward = true;
            } else if (optStr == "turbo") {
                cfg->Turbo = true;
            } else if (optStr == "toggle") {
                cfg->Toggle = true;
            } else if (optStr == "add") {
                cfg->Add = true;
            } else if (optStr == "persistent") {
                cfg->Persistent = true;
            } else {
                LOG << "ERROR: Invalid option: " << optStr << END;
            }
        } else if (token == "?") {
            auto cond = ConfigReadCond(line, &idx);

            cond->Next = cfg->Conds;
            cfg->Conds = cond;
        } else if (token == "@") {
            userStr = ConfigReadToken(line, &idx);
        } else if (token == "^") {
            rateStr = ConfigReadToken(line, &idx);
        } else if (token == "~") {
            strengthStr = ConfigReadToken(line, &idx);
        } else {
            LOG << "ERROR: Ignoring unknown token in mapping line: " << token << END;
        }
    }

    cfg->SrcKey = ConfigReadKey(inputStr);
    cfg->DestKey = ConfigReadKey(outputStr, true);
    cfg->Strength = ConfigReadStrength(strengthStr);
    cfg->Rate = ConfigReadRate(rateStr);
    cfg->User = ConfigReadUser(userStr);

    LOG << "Mapping " << inputStr << " to " << outputStr << " of player " << (cfg->User + 1) << " (strength " << cfg->Strength << ")" << END;

    return cfg;
}

void ConfigCreateResets(const SharedPtr<ImplMapping> &mapping, ImplCond *cond) {
    do {
        ImplInput *input = ImplGetInput(cond->Key);
        if (input) {
            auto reset = SharedPtr<ImplReset>::New();
            reset->Mapping = mapping;
            reset->Next = input->Resets;
            input->Resets = reset;
        }

        if (cond->Child) {
            ConfigCreateResets(mapping, cond->Child);
        }

        cond = cond->Next;
    } while (cond);
}

bool ConfigLoadInputLine(const string &line) {
    auto cfg = ConfigReadInputLine(line);
    if (!cfg) {
        return false;
    }

    int nextSrcKey = 0;
    cfg->SrcType = GetKeyType(cfg->SrcKey);
    if (cfg->SrcType.Pair) {
        tie(cfg->SrcKey, nextSrcKey) = GetKeyPair(cfg->SrcKey);
        cfg->SrcType.Pair = false;
    }

    int userIndex = cfg->User >= 0 ? cfg->User : 0;

    if (cfg->SrcType.Relative && (cfg->Toggle || cfg->Turbo)) {
        LOG << "ERROR: toggle & turbo aren't supported for relative input" << END;
        cfg->Toggle = cfg->Turbo = false;
    }

    while (cfg && cfg->SrcKey && cfg->DestKey) {
        cfg->DestType = GetKeyType(cfg->DestKey);
        if (cfg->DestType.Pair) {
            cfg->DestKey = ImplChooseBestKeyInPair(cfg->DestKey);
            cfg->DestType.Pair = false;
        }

        SharedPtr<ImplMapping> nextCfg;
        if (nextSrcKey) {
            nextCfg = SharedPtr<ImplMapping>::New(*cfg);
        }

        if (cfg->Conds && !cfg->Persistent) {
            ConfigCreateResets(cfg, cfg->Conds);
        }

        ImplInput *input = ImplGetInput(cfg->SrcKey);
        if (input) {
            if (!cfg->Replace) {
                cfg->Next = input->Mappings;
            }
            input->Mappings = cfg;

            if (cfg->SrcType.Source == MyVkSource::Keyboard) {
                G.Keyboard.IsMapped = true;
            } else if (cfg->SrcType.Source == MyVkSource::Mouse) {
                G.Mouse.IsMapped = true;
            }

            if (cfg->DestType.OfUser) {
                G.Users[userIndex].Connected = true;
            }
        }

        cfg = nextCfg;
        nextSrcKey = 0;
    }

    return true;
}

void ConfigLoadDevice(const string &key, const string &type) {
    string userStr = key.substr(6);
    int userIdx = ConfigReadUser(userStr);

    ImplUser *user = ImplGetUser(userIdx, true);
    if (user) {
        DeviceIntf *device = nullptr;
        string typeLow = StrLowerCase(type);
        if (typeLow == "x360" || typeLow == "xbox360") {
            device = new XDeviceIntf(userIdx);
        } else if (typeLow == "ds4" || typeLow == "ps4") {
            device = new Ds4DeviceIntf(userIdx);
        } else {
            LOG << "ERROR: Invalid device: " << type << END;
        }

        if (device) {
            user->Device = device;
            user->DeviceSpecified = true;
        }
        user->Connected = true;
    } else {
        LOG << "ERROR: Invalid user number following Device: " << userStr << END;
    }
}

void ConfigLoadInclude(const string &val) {
    Path filename;
    if (val.ends_with(".ini")) {
        filename = PathFromStr(val.c_str());
    } else {
        filename = PathCombineExt(PathFromStr(val.c_str()), L"ini");
    }

    ConfigLoadFrom(filename);
}

void ConfigLoadExtraHook(const string &line, intptr_t idx) {
    Path dllPath = PathFromStr(ConfigReadToken(line, &idx).c_str());
    Path dllWait = PathFromStr(ConfigReadToken(line, &idx).c_str());

    if (*dllWait) {
        dllWait = PathGetBaseNameWithoutExt(dllWait); // won't support non-dlls

        for (auto &delayedHook : G.ExtraDelayedHooks) {
            if (delayedHook.WaitDll == dllWait) {
                delayedHook.Hooks.push_back(move(dllPath));
                return;
            }
        }

        G.ExtraDelayedHooks.emplace_back();
        auto &delayedHook = G.ExtraDelayedHooks.back();
        delayedHook.WaitDll = move(dllWait);
        delayedHook.Hooks.push_back(move(dllPath));
    } else {
        G.ExtraHooks.push_back(move(dllPath));
    }
}

bool ConfigReadBoolVar(const string &val) {
    string valLow = StrLowerCase(val);

    if (valLow == "true") {
        return true;
    } else if (valLow == "false") {
        return false;
    }

    LOG << "ERROR: Invalid boolean (should be true/false): " << val << END;
    return false;
}

bool ConfigLoadVarLine(const string &line) {
    intptr_t idx = 1; // skipping initial '!'
    string key = ConfigReadToken(line, &idx);
    string keyLow = StrLowerCase(key);

    intptr_t valIdx = idx;
    if (ConfigReadToken(line, &idx) == "=") { // optional '='
        valIdx = idx;
    } else {
        idx = valIdx;
    }
    string val = ConfigReadToken(line, &idx);

    if (keyLow == "trace") {
        G.Trace = ConfigReadBoolVar(val);
    } else if (keyLow == "debug") {
        G.Debug = ConfigReadBoolVar(val);
    } else if (keyLow == "apitrace") {
        G.ApiTrace = ConfigReadBoolVar(val);
    } else if (keyLow == "apidebug") {
        G.ApiDebug = ConfigReadBoolVar(val);
    } else if (keyLow == "waitdebugger") {
        G.WaitDebugger = ConfigReadBoolVar(val);
    } else if (keyLow == "forward") {
        G.Forward = ConfigReadBoolVar(val);
    } else if (keyLow == "always") {
        G.Always = ConfigReadBoolVar(val);
    } else if (keyLow == "disable") {
        G.Disable = ConfigReadBoolVar(val);
    } else if (keyLow == "hidecursor") {
        G.HideCursor = ConfigReadBoolVar(val);
    } else if (keyLow == "injectchildren") {
        G.InjectChildren = ConfigReadBoolVar(val);
    } else if (keyLow == "rumblewindow") {
        G.RumbleWindow = ConfigReadBoolVar(val);
    } else if (keyLow.starts_with("device")) {
        ConfigLoadDevice(keyLow, val);
    } else if (keyLow == "include") {
        ConfigLoadInclude(val);
    } else if (keyLow == "extrahook") {
        ConfigLoadExtraHook(line, valIdx);
    } else {
        LOG << "ERROR: Invalid variable: " << key << END;
    }

    return true;
}

bool ConfigLoadFrom(const wchar_t *filename) {
    if (GConfig.LoadedFilesSet.contains(filename)) {
        LOG << "ERROR: Duplicate loading of config " << filename << END;
        return false;
    }

    GConfig.LoadedFiles.push_back(filename);
    GConfig.LoadedFilesSet.insert(GConfig.LoadedFiles.back().Get());

    Path path = PathCombine(GConfig.Directory, filename);
    LOG << "Loading config from: " << path << END;
    std::ifstream file(path);
    if (file.fail()) {
        LOG << "ERROR: Failed to open: " << path << END;
        return false;
    }

    string line;
    bool active = true;
    while (getline(file, line)) {
        line = StrTrimmed(line);

        if (line.empty()) {
            continue;
        }

        if (line.starts_with('#')) {
            if (line.starts_with("#[")) {
                active = false;
            } else if (line.starts_with("#]")) {
                active = true;
            }
            continue;
        }

        if (active) {
            if (line.starts_with('!')) {
                ConfigLoadVarLine(line);
            } else {
                ConfigLoadInputLine(line);
            }
        }
    }
    return true;
}

void ConfigReset() {
    G.Trace = G.Debug = G.ApiTrace = G.ApiDebug = G.WaitDebugger = false;
    G.Forward = G.Always = G.Disable = G.HideCursor = false;
    G.InjectChildren = G.RumbleWindow = false;
    G.Keyboard.IsMapped = G.Mouse.IsMapped = false;

    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        G.Users[i].Connected = G.Users[i].DeviceSpecified = false;
    }

    G.Keyboard.Reset();
    G.Mouse.Reset();
    G.ActiveUser = 0;

    GConfig.LoadedFilesSet.clear();
    GConfig.LoadedFiles.clear();
}

static void ConfigSendGlobalEvents(bool added) {
    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        if (G.Users[i].Connected) {
            G.GlobalCallbacks.Call(&G.Users[i], added);
        }
    }
}

static bool ConfigReloadNoUpdate() {
    ConfigReset();

    if (!ConfigLoadFrom(GConfig.MainFile)) {
        ConfigLoadFrom(L"_default.ini");
    }

    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        if (G.Users[i].Connected && !G.Users[i].DeviceSpecified) {
            G.Users[i].Device = new XDeviceIntf(i);
        }
    }

    return true;
}

bool ConfigLoad(Path &&path, Path &&name) {
    GConfig.Directory = move(path);
    GConfig.MainFile = move(name);
    return ConfigReloadNoUpdate();
}

void ConfigReload() {
    ImplPreMappingsChanged();
    ConfigSendGlobalEvents(false);
    ConfigReloadNoUpdate();
    ConfigSendGlobalEvents(true);
    UpdateAll();
}
