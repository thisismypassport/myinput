#pragma once
#include "State.h"
#include "Keys.h"

struct ConfigCustomMap {
    unordered_map<string, int> Keys;
    unordered_map<string, int> Vars;
    unordered_map<string, int> Devices;
};

struct ConfigAuxInfo {
    bool Error : 1;
    bool HasStrength : 1;
    bool HasRate : 1;
};

void ConfigError(ConfigAuxInfo *auxInfo) {
    if (auxInfo) {
        auxInfo->Error = true;
    }
}

void ConfigSeekToken(const string &str, intptr_t *startPtr) {
    intptr_t start = *startPtr;
    while ((size_t)start < str.size() && isspace(str[start])) {
        start++;
    }
    *startPtr = start;
}

string ConfigReadToken(const string &str, intptr_t *startPtr, bool welded = false) {
    if (!welded) {
        ConfigSeekToken(str, startPtr);
    }

    intptr_t start = *startPtr;
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

string ConfigNormStr(const string &str) {
    string strLow = StrLowerCase(str);
    StrReplaceAll(strLow, ".", "");
    return strLow;
}

#define CONFIG_UNKNOWN_NAME "tbd"

int ConfigReadKey(const ConfigCustomMap &custom, const string &str, ConfigAuxInfo *auxInfo) {
    string strLow = ConfigNormStr(str);

    if (strLow.size() == 1 && ((strLow[0] >= 'a' && strLow[0] <= 'z') || (strLow[0] >= '0' && strLow[0] <= '9'))) {
        return toupper(strLow[0]);
    }

#define CONFIG_ON_KEY(vk, pname, desc, group, name, ...) \
    if (strLow == name)                                  \
        return vk;                                       \
    __VA_OPT__(if (strLow == __VA_ARGS__) return vk;)

    ENUMERATE_KEYS_WITHOUT_SIMPLE(CONFIG_ON_KEY);
#undef CONFIG_ON_KEY

    auto customKeyIter = custom.Keys.find(strLow);
    if (customKeyIter != custom.Keys.end()) {
        return MY_VK_CUSTOM_START + customKeyIter->second;
    }

    if (strLow != CONFIG_UNKNOWN_NAME) {
        ConfigError(auxInfo);
    }
    LOG_W << "ERROR: Invalid key: " << str << END;
    return MY_VK_UNKNOWN;
}

user_t ConfigReadUser(const string &str, ConfigAuxInfo *auxInfo) {
    if (str.empty()) {
        return -1;
    }

    user_t value;
    if (StrToValue(str, &value) && value >= 1 && value <= IMPL_MAX_USERS) {
        return value - 1;
    }

    ConfigError(auxInfo);
    LOG_W << "ERROR: Invalid user (must be between 1 and " << IMPL_MAX_USERS << "): " << str << END;
    return -1;
}

double ConfigReadStrength(const string &str, ConfigAuxInfo *auxInfo) {
    if (str.empty()) {
        return 1;
    }

    if (auxInfo) {
        auxInfo->HasStrength = true;
    }

    double value;
    if (StrToValue(str, &value) && value >= 0) {
        return value;
    }

    ConfigError(auxInfo);
    LOG_W << "ERROR: Invalid strength (must be non-negative number): " << str << END;
    return 1;
}

double ConfigReadRate(const string &str, bool isTurbo, ConfigAuxInfo *auxInfo) {
    if (!str.empty()) {
        if (auxInfo) {
            auxInfo->HasRate = true;
        }

        double value;
        if (StrToValue(str, &value) && value >= 0) {
            return value;
        }

        ConfigError(auxInfo);
        LOG_W << "ERROR: Invalid rate (must be non-negative number) : " << str << END;
    }
    return isTurbo ? 0.02 : 0.01;
}

SharedPtr<ImplCond> ConfigReadCond(const ConfigCustomMap &custom, const string &line, intptr_t *idxPtr,
                                   ConfigAuxInfo *auxInfo, bool nested = false) {
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
        SharedPtr<ImplCond> *nextPtr = &cond->Child;
        while (true) {
            auto subcond = ConfigReadCond(custom, line, idxPtr, auxInfo, true);
            *nextPtr = subcond;
            nextPtr = &subcond->Next;

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
                ConfigError(auxInfo);
                LOG_W << "ERROR: Ignoring unknown token in condition: " << token << END;
                while ((size_t)*idxPtr < line.size() && ConfigReadToken(line, idxPtr) != ")") {
                }
                break;
            } else {
                if (condType == 0) {
                    condType = nextType;
                } else if (condType != nextType) {
                    LOG_W << "ERROR: Mixing & and | without parentheses in condition" << END, ConfigError(auxInfo);
                }
            }
        }

        cond->Key = condType ? condType : MY_VK_META_COND_AND;
    } else {
        cond->Key = ConfigReadKey(custom, condStr, auxInfo);
        auto keyType = GetKeyType(cond->Key);

        if (keyType.IsOutputOnly()) {
            LOG_W << "ERROR: Output-only key in condition: " << condStr << END, ConfigError(auxInfo);
        }

        string userStr;
        intptr_t suffixIdx = *idxPtr;
        string suffix = ConfigReadToken(line, &suffixIdx, !nested);
        if (suffix == "@") {
            userStr = ConfigReadToken(line, &suffixIdx);
            *idxPtr = suffixIdx;
        }

        cond->User = ConfigReadUser(userStr, auxInfo);

        if (!auxInfo && // preprocessing step, only for when loading for real
            (keyType.OrPair || keyType.AndPair)) {
            auto cond1 = SharedPtr<ImplCond>::New(*cond);
            auto cond2 = SharedPtr<ImplCond>::New(*cond);
            tie(cond1->Key, cond2->Key) = GetKeyPair(cond->Key);
            cond1->State = cond2->State = true; // State used by cond

            cond->Key = keyType.AndPair ? MY_VK_META_COND_AND : MY_VK_META_COND_OR;
            cond->Child = cond1;
            cond1->Next = cond2;
        }
    }

    return cond;
}

SharedPtr<ImplMapping> ConfigReadInputLine(const ConfigCustomMap &custom, const string &line, intptr_t idx = 0,
                                           ConfigAuxInfo *auxInfo = nullptr) {
    auto cfg = SharedPtr<ImplMapping>::New();

    string inputStr = ConfigReadToken(line, &idx);

    string inUserStr;
    while (true) {
        string token = ConfigReadToken(line, &idx);
        if (token.empty()) {
            ConfigError(auxInfo);
            LOG_W << "ERROR: ':' expected after key name in: " << line << END;
            return nullptr;
        }

        if (token == "@") {
            inUserStr = ConfigReadToken(line, &idx);
        } else if (token == ":") {
            break;
        } else {
            LOG_W << "ERROR: Ignoring unknown token before ':' in mapping line: " << token << END, ConfigError(auxInfo);
        }
    }

    string outputStr = ConfigReadToken(line, &idx);

    string outUserStr, strengthStr, rateStr;
    SharedPtr<ImplCond> *nextCondPtr = &cfg->Conds;
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
                LOG_W << "ERROR: Invalid option: " << optStr << END, ConfigError(auxInfo);
            }
        } else if (token == "?") {
            auto cond = ConfigReadCond(custom, line, &idx, auxInfo);
            *nextCondPtr = cond;
            nextCondPtr = &cond->Next;
        } else if (token == "@") {
            outUserStr = ConfigReadToken(line, &idx);
        } else if (token == "^") {
            rateStr = ConfigReadToken(line, &idx);
        } else if (token == "~") {
            strengthStr = ConfigReadToken(line, &idx);
        } else {
            LOG_W << "ERROR: Ignoring unknown token in mapping line: " << token << END, ConfigError(auxInfo);
        }
    }

    cfg->SrcKey = ConfigReadKey(custom, inputStr, auxInfo);
    cfg->SrcType = GetKeyType(cfg->SrcKey);
    cfg->DestKey = ConfigReadKey(custom, outputStr, auxInfo);
    cfg->DestType = GetKeyType(cfg->DestKey);
    cfg->Strength = ConfigReadStrength(strengthStr, auxInfo);
    cfg->Rate = ConfigReadRate(rateStr, cfg->Turbo, auxInfo);
    cfg->SrcUser = ConfigReadUser(inUserStr, auxInfo);
    cfg->DestUser = ConfigReadUser(outUserStr, auxInfo);

    if (cfg->SrcType.IsOutputOnly()) {
        LOG_W << "ERROR: Output-only key used as input: " << inputStr << END, ConfigError(auxInfo);
    }

    LOG << "Mapping " << inputStr << " to " << outputStr << END;

    return cfg;
}

int ConfigReadDeviceIdx(const string &idx, ConfigAuxInfo *auxInfo = nullptr) {
    return ConfigReadUser(idx, auxInfo);
}

Path ConfigReadInclude(const string &val) {
    if (val.ends_with(".ini")) {
        return PathFromStr(val.c_str());
    } else {
        return PathCombineExt(PathFromStr(val.c_str()), L"ini");
    }
}

string ConfigReadPlugin(const string &val, const Path &pathUnderRoot, Path *outPath) {
    intptr_t idx = 0;
    string name = ConfigReadToken(val, &idx);
    string rest = StrTrimmed(val.substr(idx));

    *outPath = rest.empty() ? PathCombine(PathCombine(PathGetDirName(pathUnderRoot), L"plugins"), PathFromStr(name.c_str())) : PathFromStr(rest.c_str());

    return name;
}

bool ConfigReadBoolVar(const string &val, ConfigAuxInfo *auxInfo) {
    string valLow = StrLowerCase(val);

    if (valLow == "true") {
        return true;
    } else if (valLow == "false") {
        return false;
    }

    ConfigError(auxInfo);
    LOG_W << "ERROR: Invalid boolean (should be true/false): " << val << END;
    return false;
}

enum class ConfigVar : uint32_t {
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
    CustomStart = 0x10000000,
};

ConfigVar operator+(ConfigVar var, int idx) { return (ConfigVar)((int)var + idx); }
int operator-(ConfigVar l, ConfigVar r) { return (int)l - (int)r; }

#define CONFIG_VAR_BOOL 0x1
#define CONFIG_VAR_STR 0x2
#define CONFIG_VAR_STR_CFG 0x4
#define CONFIG_VAR_SPECIAL 0x8
#define CONFIG_VAR_GROUP_DEBUG 0x100

// (in ui order, vars with same CONFIG_VAR_GROUP_* must be consecutive)
#define ENUMERATE_CONFIG_VARS(e)                                                           \
    /* Normal group */                                                                     \
    e(ConfigVar::RumbleWindow, "RumbleWindow", L"Shake the window on gamepad vibration",   \
      "rumblewindow", CONFIG_VAR_BOOL, &G.RumbleWindow);                                   \
    e(ConfigVar::Device, "Device", L"Configure a virtual gamepad's type",                  \
      "device", CONFIG_VAR_SPECIAL, nullptr);                                              \
    e(ConfigVar::HideCursor, "HideCursor", L"Hide the cursor while in focus",              \
      "hidecursor", CONFIG_VAR_BOOL, &G.HideCursor);                                       \
    e(ConfigVar::Include, "Include", L"Include another config",                            \
      "include", CONFIG_VAR_STR_CFG, nullptr);                                             \
    e(ConfigVar::Always, "Always", L"Always process mappings, even in background",         \
      "always", CONFIG_VAR_BOOL, &G.Always);                                               \
    e(ConfigVar::Forward, "Forward", L"Forward all inputs, even if mapped to outputs",     \
      "forward", CONFIG_VAR_BOOL, &G.Forward);                                             \
    e(ConfigVar::Disable, "Disable", L"Disable all mappings by default",                   \
      "disable", CONFIG_VAR_BOOL, &G.Disable);                                             \
    e(ConfigVar::InjectChildren, "InjectChildren", L"Inject into child processes",         \
      "injectchildren", CONFIG_VAR_BOOL, &G.InjectChildren);                               \
    e(ConfigVar::Plugin, "Plugin", L"Load a MyInput plugin",                               \
      "plugin", CONFIG_VAR_STR_CFG, nullptr);                                              \
    /* Debug group */                                                                      \
    e(ConfigVar::ApiDebug, "ApiDebug", L"Log debug-level api calls",                       \
      "apidebug", CONFIG_VAR_BOOL | CONFIG_VAR_GROUP_DEBUG, &G.ApiDebug);                  \
    e(ConfigVar::ApiTrace, "ApiTrace", L"Log trace-level api calls",                       \
      "apitrace", CONFIG_VAR_BOOL | CONFIG_VAR_GROUP_DEBUG, &G.ApiTrace);                  \
    e(ConfigVar::Debug, "Debug", L"Log debug-level events",                                \
      "debug", CONFIG_VAR_BOOL | CONFIG_VAR_GROUP_DEBUG, &G.Debug);                        \
    e(ConfigVar::Trace, "Trace", L"Log trace-level events",                                \
      "trace", CONFIG_VAR_BOOL | CONFIG_VAR_GROUP_DEBUG, &G.Trace);                        \
    e(ConfigVar::WaitDebugger, "WaitDebugger", L"(Debug) Hang until debugger is attached", \
      "waitdebugger", CONFIG_VAR_BOOL | CONFIG_VAR_GROUP_DEBUG, &G.WaitDebugger);          \
    e(ConfigVar::SpareForDebug, "SpareForDebug", L"(Debug) No effect",                     \
      "sparefordebug", CONFIG_VAR_BOOL | CONFIG_VAR_GROUP_DEBUG, &G.SpareForDebug);        \
    //

template <class TBoolHandler, class TCustomHandler>
void ConfigReadVarLine(const ConfigCustomMap &custom, const string &line, intptr_t idx,
                       TBoolHandler &&boolHandler, TCustomHandler &&customHandler, ConfigAuxInfo *auxInfo = nullptr) {
    string key = ConfigReadToken(line, &idx);
    string keyLow = ConfigNormStr(key);

    intptr_t validx = idx;
    string val = ConfigReadToken(line, &idx);
    if (val == "=") // optional '='
    {
        validx = idx;
        val = ConfigReadToken(line, &idx);
    }

    auto getRest = [&] { return StrTrimmed(line.substr(validx)); };

#define CONFIG_ON_VAR(cv, pname, desc, name, flags, ptr)                  \
    if (keyLow == name) {                                                 \
        if constexpr (flags & CONFIG_VAR_BOOL)                            \
            return boolHandler(cv, ConfigReadBoolVar(val, auxInfo), ptr); \
        else                                                              \
            return customHandler(cv, getRest(), "");                      \
    }

    ENUMERATE_CONFIG_VARS(CONFIG_ON_VAR);
#undef CONFIG_ON_VAR

    if (keyLow.starts_with("device")) {
        return customHandler(ConfigVar::Device, getRest(), key.substr(6));
    }

    auto iter = custom.Vars.find(keyLow);
    if (iter != custom.Vars.end()) {
        return customHandler(ConfigVar::CustomStart + iter->second, getRest(), "");
    }

    if (keyLow != CONFIG_UNKNOWN_NAME) {
        ConfigError(auxInfo);
    }
    LOG_W << "ERROR: Invalid variable: " << keyLow << END;
}

enum class ConfigDevice : uint32_t {
    Unknown,
    XBox,
    Ds4,
    None,
    CustomStart = 0x10000000,
};

ConfigDevice operator+(ConfigDevice var, int idx) { return (ConfigDevice)((int)var + idx); }
int operator-(ConfigDevice l, ConfigDevice r) { return (int)l - (int)r; }

// (in ui order)
#define ENUMERATE_CONFIG_DEVICES(e)                                                       \
    e(ConfigDevice::None, "None", L"No Controller", "none");                              \
    e(ConfigDevice::XBox, "XBox360", L"XBox360 Controller (Default)", "xbox360", "x360"); \
    e(ConfigDevice::Ds4, "PS4", L"PS4 (Dual Shock 4) Controller", "ps4", "ds4");          \
    //

ConfigDevice ConfigReadDeviceName(const ConfigCustomMap &custom, const string &type,
                                  ConfigAuxInfo *auxInfo = nullptr) {
    string typeLow = ConfigNormStr(type);

#define CONFIG_ON_DEVICE(cv, pname, desc, name, ...) \
    if (typeLow == name)                             \
        return cv;                                   \
    __VA_OPT__(if (typeLow == __VA_ARGS__) return cv;)

    ENUMERATE_CONFIG_DEVICES(CONFIG_ON_DEVICE);
#undef CONFIG_ON_DEVICE

    auto iter = custom.Devices.find(typeLow);
    if (iter != custom.Devices.end()) {
        return ConfigDevice::CustomStart + iter->second;
    }

    if (typeLow != CONFIG_UNKNOWN_NAME) {
        ConfigError(auxInfo);
    }
    LOG_W << "ERROR: Invalid device: " << typeLow << END;
    return ConfigDevice::Unknown;
}
