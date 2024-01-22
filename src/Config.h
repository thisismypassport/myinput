#pragma once
#include "StateUtils.h"
#include "Devices.h"
#include <fstream>

struct ConfigCustomMap {
    unordered_map<string, int> Keys;
    unordered_map<string, int> Vars;
};

struct ConfigState {
    list<Path> LoadedFiles;
    unordered_set<wstring_view> LoadedFilesSet;
    Path MainFile;
    Path Directory;
    ConfigCustomMap CustomMap;
    vector<function<void(const string &)>> CustomVarCbs;
} GConfig;

bool ConfigLoadFrom(const wchar_t *filename);

string ConfigReadToken(const string &str, intptr_t *startPtr, bool welded = false) {
    intptr_t start = *startPtr;
    while ((size_t)start < str.size() && isspace(str[start]) && !welded) {
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

int ConfigReadKey(const ConfigCustomMap &custom, const string &str, int *outUser, bool output = false) {
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

    auto customKeyIter = custom.Keys.find(strLow);
    if (customKeyIter != custom.Keys.end()) {
        *outUser = customKeyIter->second;
        return MY_VK_CUSTOM;
    }

    LOG << "ERROR: Invalid key: " << str << END;
    return 0;

#undef CONFIG_ON_KEY
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

SharedPtr<ImplCond> ConfigReadCond(const ConfigCustomMap &custom, const string &line, intptr_t *idxPtr) {
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
            auto subcond = ConfigReadCond(custom, line, idxPtr);
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
        cond->Key = ConfigReadKey(custom, condStr, &cond->User);

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

SharedPtr<ImplMapping> ConfigReadInputLine(const ConfigCustomMap &custom, const string &line, intptr_t idx = 0) {
    auto cfg = SharedPtr<ImplMapping>::New();

    string inputStr = ConfigReadToken(line, &idx);

    string inUserStr;
    while (true) {
        string token = ConfigReadToken(line, &idx);
        if (token.empty()) {
            LOG << "ERROR: ':' expected after key name in: " << line << END;
            return nullptr;
        }

        if (token == "@") {
            inUserStr = ConfigReadToken(line, &idx);
        } else if (token == ":") {
            break;
        } else {
            LOG << "ERROR: Ignoring unknown token before ':' in mapping line: " << token << END;
        }
    }

    string outputStr = ConfigReadToken(line, &idx);

    string outUserStr, strengthStr, rateStr;
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
            } else if (optStr == "reset") {
                cfg->Reset = true;
            } else {
                LOG << "ERROR: Invalid option: " << optStr << END;
            }
        } else if (token == "?") {
            auto cond = ConfigReadCond(custom, line, &idx);

            cond->Next = cfg->Conds;
            cfg->Conds = cond;
        } else if (token == "@") {
            outUserStr = ConfigReadToken(line, &idx);
        } else if (token == "^") {
            rateStr = ConfigReadToken(line, &idx);
        } else if (token == "~") {
            strengthStr = ConfigReadToken(line, &idx);
        } else {
            LOG << "ERROR: Ignoring unknown token in mapping line: " << token << END;
        }
    }

    cfg->SrcKey = ConfigReadKey(custom, inputStr, &cfg->SrcUser);
    cfg->DestKey = ConfigReadKey(custom, outputStr, &cfg->DestUser, true);
    cfg->Strength = ConfigReadStrength(strengthStr);
    cfg->Rate = ConfigReadRate(rateStr);
    if (cfg->SrcKey != MY_VK_CUSTOM) {
        cfg->SrcUser = ConfigReadUser(inUserStr);
    }
    if (cfg->DestKey != MY_VK_CUSTOM) {
        cfg->DestUser = ConfigReadUser(outUserStr);
    }

    LOG << "Mapping " << inputStr << " to " << outputStr << END;

    return cfg;
}

void ConfigCreateResets(const SharedPtr<ImplMapping> &mapping, ImplCond *cond) {
    do {
        ImplInput *input = ImplGetInput(cond->Key, cond->User);
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
    auto cfg = ConfigReadInputLine(GConfig.CustomMap, line);
    if (!cfg) {
        return false;
    }

    int nextSrcKey = 0;
    cfg->SrcType = GetKeyType(cfg->SrcKey);
    if (cfg->SrcType.Pair) {
        tie(cfg->SrcKey, nextSrcKey) = GetKeyPair(cfg->SrcKey);
        cfg->SrcType.Pair = false;
    }

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
            nextCfg->SrcKey = nextSrcKey;
        }

        if (cfg->Conds && (!cfg->Add || cfg->Reset)) {
            ConfigCreateResets(cfg, cfg->Conds);
        }

        ImplInput *input = ImplGetInput(cfg->SrcKey, cfg->SrcUser);
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

            int destUserIdx = cfg->DestUser >= 0 ? cfg->DestUser : 0;

            if (cfg->DestType.OfUser) {
                G.Users[destUserIdx].Connected = true;
            }
        }

        cfg = nextCfg;
        nextSrcKey = 0;
    }

    return true;
}

int ConfigReadDeviceIdx(const string &key) {
    return ConfigReadUser(key.substr(6));
}

void ConfigLoadDevice(const string &key, const string &type) {
    int userIdx = ConfigReadDeviceIdx(key);

    ImplUser *user = ImplGetUser(userIdx, true);
    if (user) {
        DeviceIntf *device = nullptr;
        string typeLow = StrLowerCase(type);
        if (typeLow == "x360" || typeLow == "xbox360") {
            device = new XDeviceIntf(userIdx);
        } else if (typeLow == "ds4" || typeLow == "ps4") {
            device = new Ds4DeviceIntf(userIdx);
        } else if (typeLow == "none") {
            device = new NoDeviceIntf(userIdx);
        } else {
            LOG << "ERROR: Invalid device: " << type << END;
        }

        if (device) {
            user->Device = device;
            user->DeviceSpecified = true;
        }
        user->Connected = true;
    } else {
        LOG << "ERROR: Invalid user number following Device: " << userIdx << END;
    }
}

Path ConfigReadInclude(const string &val) {
    if (val.ends_with(".ini")) {
        return PathFromStr(val.c_str());
    } else {
        return PathCombineExt(PathFromStr(val.c_str()), L"ini");
    }
}

void ConfigLoadInclude(const string &val) {
    ConfigLoadFrom(ConfigReadInclude(val));
}

string ConfigReadPlugin(const string &val, const Path &pathUnderRoot, Path *outPath) {
    intptr_t idx = 0;
    string name = ConfigReadToken(val, &idx);
    string rest = StrTrimmed(val.substr(idx));

    *outPath = rest.empty() ? PathCombine(PathCombine(PathGetDirName(pathUnderRoot), L"plugins"), PathFromStr(name.c_str())) : PathFromStr(rest.c_str());

    return name;
}

void ConfigLoadPlugin(const string &val) {
    Path path;
    string name = ConfigReadPlugin(val, GConfig.Directory, &path);

#ifdef _WIN64
    path = PathCombine(path, L"x64");
#else
    path = PathCombine(path, L"Win32");
#endif

    path = PathCombine(path, PathFromStr((name + "_hook.dll").c_str()));

    if (G.Debug) {
        LOG << "Loading extra hook: " << path << END;
    }

    // Load it immediately, to allow registering config extensions
    // (note: this means calling dll load from dllmain, which is not legal but seems-ok)
    // (only alternative I see is a complicated second injection, to avoid config loading from dllmain)
    if (!LoadLibraryExW(path, nullptr, 0)) {
        LOG << "Failed loading extra hook: " << path << " due to: " << GetLastError() << END;
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

enum class ConfigVar {
    Unknown,
    Trace,
    Debug,
    ApiTrace,
    ApiDebug,
    WaitDebugger,
    SpareForDebug,
    Forward,
    Always,
    Disable,
    HideCursor,
    InjectChildren,
    RumbleWindow,
    Device,
    Include,
    Plugin,
    Custom,
};

#define ENUMERATE_CONFIG_BOOL_VARS(e)                                  \
    e(ConfigVar::Trace, "trace", &G.Trace);                            \
    e(ConfigVar::Debug, "debug", &G.Debug);                            \
    e(ConfigVar::ApiTrace, "apitrace", &G.ApiTrace);                   \
    e(ConfigVar::ApiDebug, "apidebug", &G.ApiDebug);                   \
    e(ConfigVar::WaitDebugger, "waitdebugger", &G.WaitDebugger);       \
    e(ConfigVar::SpareForDebug, "sparefordebug", &G.SpareForDebug);    \
    e(ConfigVar::Forward, "forward", &G.Forward);                      \
    e(ConfigVar::Always, "always", &G.Always);                         \
    e(ConfigVar::Disable, "disable", &G.Disable);                      \
    e(ConfigVar::HideCursor, "hidecursor", &G.HideCursor);             \
    e(ConfigVar::InjectChildren, "injectchildren", &G.InjectChildren); \
    e(ConfigVar::RumbleWindow, "rumblewindow", &G.RumbleWindow);       \
    //

template <class TBoolHandler, class TCustomHandler>
void ConfigReadVarLine(const ConfigCustomMap &custom, const string &line, intptr_t idx, TBoolHandler &&boolHandler, TCustomHandler &&customHandler) {
#define CONFIG_ON_BOOL_VAR(cv, name, ptr) \
    if (keyLow == name)                   \
        return boolHandler(cv, ConfigReadBoolVar(val), ptr);

    string key = ConfigReadToken(line, &idx);
    string keyLow = StrLowerCase(key);

    intptr_t validx = idx;
    string val = ConfigReadToken(line, &idx);
    if (val == "=") // optional '='
    {
        validx = idx;
        val = ConfigReadToken(line, &idx);
    }

    ENUMERATE_CONFIG_BOOL_VARS(CONFIG_ON_BOOL_VAR);

    string rest = StrTrimmed(line.substr(validx));

    if (keyLow == "include") {
        return customHandler(ConfigVar::Include, keyLow, rest);
    } else if (keyLow == "plugin") {
        return customHandler(ConfigVar::Plugin, keyLow, rest);
    } else if (keyLow.starts_with("device")) {
        return customHandler(ConfigVar::Device, keyLow, rest);
    } else if (custom.Vars.contains(keyLow)) {
        return customHandler(ConfigVar::Custom, keyLow, rest);
    }

    LOG << "ERROR: Invalid variable: " << keyLow << END;
    return customHandler(ConfigVar::Unknown, keyLow, rest);

#undef CONFIG_ON_BOOL_VAR
}

void ConfigLoadVarLine(const string &line, intptr_t idx) {
    ConfigReadVarLine(
        GConfig.CustomMap, line, idx, [](auto, bool value, bool *ptr) { *ptr = value; }, [&line](ConfigVar var, const string &keyLow, const string &rest) {
        switch (var)
        {
        case ConfigVar::Include:
            ConfigLoadInclude (rest);
            break;

        case ConfigVar::Plugin:
            ConfigLoadPlugin (rest);
            break;

        case ConfigVar::Device:
            ConfigLoadDevice (keyLow, rest);
            break;

        case ConfigVar::Custom:
            GConfig.CustomVarCbs[GConfig.CustomMap.Vars[keyLow]] (rest);
            break;
        } });
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
    int inactiveDepth = 0;
    while (getline(file, line)) {
        intptr_t idx = 0;
        string token = ConfigReadToken(line, &idx);

        if (token.empty()) {
            continue;
        } else if (token == "#") {
            string subtoken = ConfigReadToken(line, &idx, true);
            if (subtoken == "[") {
                inactiveDepth++;
            } else if (subtoken == "]" && inactiveDepth > 0) {
                inactiveDepth--;
            }
            continue;
        } else if (token == "[") {
            if (inactiveDepth > 0) {
                inactiveDepth++;
            }
            continue;
        } else if (token == "]") {
            if (inactiveDepth > 0) {
                inactiveDepth--;
            }
            continue;
        } else if (inactiveDepth == 0) {
            if (token == "!") {
                ConfigLoadVarLine(line, idx);
            } else {
                ConfigLoadInputLine(line);
            }
        }
    }
    return true;
}

void ConfigReset() {
    G.Reset();

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

bool ConfigInit(Path &&path, Path &&name) {
    GConfig.Directory = move(path);
    GConfig.MainFile = move(name);
    return ConfigReloadNoUpdate();
}

void ConfigReload() {
    ImplAbortMappings();
    ConfigSendGlobalEvents(false);
    ConfigReloadNoUpdate();
    ConfigSendGlobalEvents(true);
    UpdateAll();
}

void ConfigRegisterCustomKey(ConfigCustomMap &custom, const string &name, int index) {
    string nameLow = name;
    StrToLowerCase(nameLow);
    StrReplaceAll(nameLow, ".", "");
    custom.Keys[nameLow] = index;
}

int ConfigRegisterCustomKey(const char *name, function<void(const InputValue &)> &&cb) {
    int index = (int)G.CustomKeys.size();

    auto custKey = UniquePtr<ImplCustomKey>::New();
    custKey->Callback = move(cb);
    G.CustomKeys.push_back(move(custKey));

    ConfigRegisterCustomKey(GConfig.CustomMap, name, index);
    return index;
}

void ConfigRegisterCustomVar(ConfigCustomMap &custom, const char *name, int index) {
    string nameLow = name;
    StrToLowerCase(nameLow);
    custom.Vars[nameLow] = index;
}

void ConfigRegisterCustomVar(const char *name, function<void(const string &)> &&cb) {
    int index = (int)GConfig.CustomVarCbs.size();
    GConfig.CustomVarCbs.push_back(move(cb));

    ConfigRegisterCustomVar(GConfig.CustomMap, name, index);
}
