#pragma once
#include "State.h"
#include "Keys.h"

struct ConfigPlugin {
    unordered_map<string, int> Keys;
    unordered_map<string, int> Vars;
    unordered_map<string, int> Devices;
};

struct ConfigCustom {
    unordered_map<string, UniquePtr<ConfigPlugin>> Plugins;
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
        if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '%' && ch != '/') {
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

string ConfigReadRest(const string &str, intptr_t *startPtr) {
    string rest = StrTrimmed(str.substr(*startPtr));
    *startPtr = str.size();
    return rest;
}

string ConfigNormStr(const string &str) {
    string strLow = StrLowerCase(str);
    StrReplaceAll(strLow, ".", "");
    return strLow;
}

Path ConfigReadPlugin(const string &name, const Path &pathUnderRoot) {
    if (StrContains(name, "/") || StrContains(name, "\\")) {
        LOG_ERR << "Invalid plugin name: " << name << END;
        return nullptr;
    }

    return PathCombine(PathCombine(PathGetDirName(pathUnderRoot), L"plugins"), PathFromStr(name.c_str()));
}

bool ConfigLoadPlugin(ConfigPlugin *plugin, const string &name);

ConfigPlugin *ConfigLoadPlugin(ConfigCustom &custom, const string &name) {
    string nameLow = name;
    StrToLowerCase(nameLow);

    ConfigPlugin *plugin = (custom.Plugins[nameLow] = UniquePtr<ConfigPlugin>::New());
    if (!ConfigLoadPlugin(plugin, name)) {
        custom.Plugins.erase(nameLow);
        plugin = nullptr;
    }
    return plugin;
}

ConfigPlugin *ConfigTryGetPlugin(ConfigCustom &custom, string *pStrLow) {
    intptr_t pos = pStrLow->find('/');
    if (pos < 0) {
        return nullptr;
    }

    string pluginName = pStrLow->substr(0, pos);
    auto iter = custom.Plugins.find(pluginName);

    ConfigPlugin *plugin;
    if (iter != custom.Plugins.end()) {
        plugin = iter->second.get();
    } else {
        plugin = ConfigLoadPlugin(custom, pluginName);
    }

    if (!plugin) {
        return nullptr;
    }

    *pStrLow = pStrLow->substr(pos + 1);
    return plugin;
}

#define CONFIG_UNKNOWN_NAME "tbd"

int ConfigReadKey(ConfigCustom &custom, const string &str, ConfigAuxInfo *auxInfo) {
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

    auto plugin = ConfigTryGetPlugin(custom, &strLow);
    if (plugin) {
        auto customKeyIter = plugin->Keys.find(strLow);
        if (customKeyIter != plugin->Keys.end()) {
            return MY_VK_CUSTOM_START + customKeyIter->second;
        }
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

SharedPtr<ImplCond> ConfigReadCond(ConfigCustom &custom, const string &line, intptr_t *idxPtr,
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

SharedPtr<ImplMapping> ConfigReadInputLine(ConfigCustom &custom, const string &line, intptr_t idx = 0,
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
        } else if (token == "=") {
            cfg->Data = SharedPtr<string>::New(ConfigReadRest(line, &idx));
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

Path ConfigReadInclude(const string &val) {
    if (val.ends_with(".ini")) {
        return PathFromStr(val.c_str());
    } else {
        return PathCombineExt(PathFromStr(val.c_str()), L"ini");
    }
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
    Comment,
    StickShape,
    CustomStart = 0x10000000,
};

ConfigVar operator+(ConfigVar var, int idx) { return (ConfigVar)((int)var + idx); }
int operator-(ConfigVar l, ConfigVar r) { return (int)l - (int)r; }

#define CONFIG_VAR_BOOL 0x1
#define CONFIG_VAR_STR 0x2
#define CONFIG_VAR_SPECIAL 0x4
#define CONFIG_VAR_NO_EQUAL 0x8
#define CONFIG_VAR_GROUP_DEBUG 0x100

// (in ui order, vars with same CONFIG_VAR_GROUP_* must be consecutive)
#define ENUMERATE_CONFIG_VARS(e)                                                           \
    /* Normal group */                                                                     \
    e(ConfigVar::RumbleWindow, "RumbleWindow", L"Shake the window on gamepad vibration",   \
      "rumblewindow", CONFIG_VAR_BOOL, &G.RumbleWindow);                                   \
    e(ConfigVar::Device, "Device", L"Gamepad type",                                        \
      "device", CONFIG_VAR_SPECIAL, nullptr);                                              \
    e(ConfigVar::StickShape, "StickShape", L"Shape traced by gamepad's thumbsticks",       \
      "stickshape", CONFIG_VAR_SPECIAL, nullptr);                                          \
    e(ConfigVar::HideCursor, "HideCursor", L"Hide the cursor while in focus",              \
      "hidecursor", CONFIG_VAR_BOOL, &G.HideCursor);                                       \
    e(ConfigVar::Include, "Include", L"Include another config",                            \
      "include", CONFIG_VAR_SPECIAL | CONFIG_VAR_NO_EQUAL, nullptr);                       \
    e(ConfigVar::Always, "Always", L"Always process mappings, even in background",         \
      "always", CONFIG_VAR_BOOL, &G.Always);                                               \
    e(ConfigVar::Forward, "Forward", L"Forward all inputs, even if mapped to outputs",     \
      "forward", CONFIG_VAR_BOOL, &G.Forward);                                             \
    e(ConfigVar::Disable, "Disable", L"Disable all mappings by default",                   \
      "disable", CONFIG_VAR_BOOL, &G.Disable);                                             \
    e(ConfigVar::InjectChildren, "InjectChildren", L"Inject into child processes",         \
      "injectchildren", CONFIG_VAR_BOOL, &G.InjectChildren);                               \
    e(ConfigVar::Comment, "Comment", L"Comment (no effect)",                               \
      "comment", CONFIG_VAR_SPECIAL | CONFIG_VAR_NO_EQUAL, nullptr);                       \
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
void ConfigReadVarLine(ConfigCustom &custom, const string &line, intptr_t idx,
                       TBoolHandler &&boolHandler, TCustomHandler &&customHandler, ConfigAuxInfo *auxInfo = nullptr) {
    string key = ConfigReadToken(line, &idx);
    string keyLow = ConfigNormStr(key);

    intptr_t validx = idx;
    user_t user = -1;
    string val = ConfigReadToken(line, &idx);
    if (val == "@") {
        string userStr = ConfigReadToken(line, &idx);
        user = ConfigReadUser(userStr, auxInfo);

        validx = idx;
        val = ConfigReadToken(line, &idx);
    }
    if (val == "=") // optional '='
    {
        validx = idx;
        val = ConfigReadToken(line, &idx);
    }

retryLegacy:
    auto getRest = [&] { return ConfigReadRest(line, &validx); };

#define CONFIG_ON_VAR(cv, pname, desc, name, flags, ptr)                  \
    if (keyLow == name) {                                                 \
        if constexpr ((flags) & CONFIG_VAR_BOOL)                          \
            return boolHandler(cv, ConfigReadBoolVar(val, auxInfo), ptr); \
        else                                                              \
            return customHandler(cv, getRest(), user);                    \
    }

    ENUMERATE_CONFIG_VARS(CONFIG_ON_VAR);
#undef CONFIG_ON_VAR

    auto plugin = ConfigTryGetPlugin(custom, &keyLow);
    if (plugin) {
        auto iter = plugin->Vars.find(keyLow);
        if (iter != plugin->Vars.end()) {
            return customHandler(ConfigVar::CustomStart + iter->second, getRest(), user);
        }
    }

    if (keyLow.starts_with("device") && keyLow.size() > 6) // legacy
    {
        user = ConfigReadUser(keyLow.substr(6), auxInfo);
        keyLow = keyLow.substr(0, 6);
        goto retryLegacy;
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

ConfigDevice ConfigReadDeviceName(ConfigCustom &custom, const string &type,
                                  ConfigAuxInfo *auxInfo = nullptr) {
    string typeLow = ConfigNormStr(type);

#define CONFIG_ON_DEVICE(cv, pname, desc, name, ...) \
    if (typeLow == name)                             \
        return cv;                                   \
    __VA_OPT__(if (typeLow == __VA_ARGS__) return cv;)

    ENUMERATE_CONFIG_DEVICES(CONFIG_ON_DEVICE);
#undef CONFIG_ON_DEVICE

    auto plugin = ConfigTryGetPlugin(custom, &typeLow);
    if (plugin) {
        auto iter = plugin->Devices.find(typeLow);
        if (iter != plugin->Devices.end()) {
            return ConfigDevice::CustomStart + iter->second;
        }
    }

    if (typeLow != CONFIG_UNKNOWN_NAME) {
        ConfigError(auxInfo);
    }
    LOG_W << "ERROR: Invalid device: " << typeLow << END;
    return ConfigDevice::Unknown;
}

// (in ui order)
#define ENUMERATE_STICK_SHAPES(e)                                                  \
    e(ImplStickShape::Default, "Default", L"Use Controller's Default", "default"); \
    e(ImplStickShape::Circle, "Circle", L"Circle", "circle");                      \
    e(ImplStickShape::Square, "Square", L"Square", "square");                      \
    //

ImplStickShape ConfigReadStickShape(const string &type, ConfigAuxInfo *auxInfo = nullptr) {
    string typeLow = ConfigNormStr(type);

#define CONFIG_ON_SHAPE(cv, pname, desc, name) \
    if (typeLow == name)                       \
        return cv;

    ENUMERATE_STICK_SHAPES(CONFIG_ON_SHAPE);
#undef CONFIG_ON_SHAPE

    ConfigError(auxInfo);
    LOG_W << "ERROR: Invalid stick shape: " << typeLow << END;
    return ImplStickShape::Default;
}
