#pragma once
#include "State.h"
#include "Devices.h"
#include <fstream>

string ConfigReadToken(const string &str, intptr_t *startPtr) {
    intptr_t start = *startPtr;
    while ((size_t)start < str.size() && isspace(str[start])) {
        start++;
    }

    intptr_t end = start;
    while ((size_t)end < str.size() && !isspace(str[end])) {
        char ch = str[end++];
        if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '%') {
            break;
        }
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
    if (StrToValue(str, &value) && value > 0 && value <= 1) {
        return value;
    }

    LOG << "ERROR: Invalid strength (must be number between 0 and 1): " << str << END;
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

struct ConfigInputLine {
    bool Replace = false;
    bool Forward = false;
    bool Turbo = false;
    bool Toggle = false;
    bool Snap = false;
    int Input = 0;
    int Output = 0;
    int User = 0;
    double Strength = 0;
    double Rate = 0;
    ImplInputCond *Cond = nullptr;

    void Reset() {
        if (Cond) {
            Cond->Reset();
            delete Cond;
        }
    }
};

bool ConfigReadInputLine(const string &line, ConfigInputLine &cfg) {
    intptr_t idx = 0;
    string inputStr = ConfigReadToken(line, &idx);
    string colon = ConfigReadToken(line, &idx);
    string outputStr = ConfigReadToken(line, &idx);

    if (colon != ":") {
        LOG << "ERROR: ':' expected after key name in: " << line << END;
        return false;
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
                cfg.Replace = true;
            } else if (optStr == "forward") {
                cfg.Forward = true;
            } else if (optStr == "turbo") {
                cfg.Turbo = true;
            } else if (optStr == "toggle") {
                cfg.Toggle = true;
            } else if (optStr == "snap") {
                cfg.Snap = true;
            } else {
                LOG << "ERROR: Invalid option: " << optStr << END;
            }
        } else if (token == "?") {
            string condStr = ConfigReadToken(line, &idx);
            auto cond = new ImplInputCond();
            cond->State = true;

            while (true) {
                if (condStr == "^") {
                    cond->Toggle = true;
                } else if (condStr == "~") {
                    cond->State = false;
                } else {
                    break;
                }

                condStr = ConfigReadToken(line, &idx);
            }

            cond->Key = ConfigReadKey(condStr);

            cond->Next = cfg.Cond;
            cfg.Cond = cond;
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

    cfg.Input = ConfigReadKey(inputStr);
    cfg.Output = ConfigReadKey(outputStr, true);
    cfg.Strength = ConfigReadStrength(strengthStr);
    cfg.Rate = ConfigReadRate(rateStr);
    cfg.User = ConfigReadUser(userStr);

    LOG << "Mapping " << inputStr << " to " << outputStr << " of player " << (cfg.User + 1) << " (strength " << cfg.Strength << ")" << END;

    return true;
}

bool ConfigLoadInputLine(const string &line) {
    ConfigInputLine cfg;
    if (!ConfigReadInputLine(line, cfg)) {
        return false;
    }

    int nextInput = 0;
    MyVkType inputType = GetKeyType(cfg.Input);
    if (inputType.Pair) {
        tie(cfg.Input, nextInput) = GetKeyPair(cfg.Input);
        inputType.Pair = false;
    }

    int userIndex = cfg.User >= 0 ? cfg.User : 0;

    if (inputType.Relative && (cfg.Toggle || cfg.Turbo)) {
        LOG << "ERROR: toggle & turbo aren't supported for relative input" << END;
        cfg.Toggle = cfg.Turbo = false;
    }

    while (cfg.Input && cfg.Output) {
        MyVkType outputType = GetKeyType(cfg.Output);
        if (outputType.Pair) {
            cfg.Output = ImplChooseBestKeyInPair(cfg.Output);
            outputType.Pair = false;
        }

        ImplInput *mapping = ImplGetInput(cfg.Input);
        if (mapping) {
            if (cfg.Replace) {
                mapping->Reset();
            } else if (mapping->IsValid()) {
                while (mapping->Next) {
                    mapping = mapping->Next;
                }

                mapping->Next = new ImplInput();
                mapping = mapping->Next;
            }

            mapping->DestKey = cfg.Output;
            mapping->SrcType = inputType;
            mapping->DestType = GetKeyType(cfg.Output);
            mapping->User = cfg.User;
            mapping->Strength = cfg.Strength;
            mapping->Rate = cfg.Rate;
            mapping->Conds = cfg.Cond;
            mapping->Forward = cfg.Forward;
            mapping->Turbo = cfg.Turbo;
            mapping->Toggle = cfg.Toggle;
            mapping->Snap = cfg.Snap;

            if (inputType.Source == MyVkSource::Keyboard) {
                G.Keyboard.IsMapped = true;
            } else if (inputType.Source == MyVkSource::Mouse) {
                G.Mouse.IsMapped = true;
            }

            if (outputType.OfUser) {
                G.Users[userIndex].Connected = true;
            }

            if (IsMouseMotionKey(cfg.Input)) {
                G.Mouse.AnyMotionInput = true;
            }
            if (IsMouseMotionKey(cfg.Output)) {
                G.Mouse.AnyMotionOutput = true;
            }
        }

        cfg.Input = nextInput;
        nextInput = 0;
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

void ConfigLoadExtraHook(const string &val) {
    intptr_t idx = 0;
    Path dllPath = PathFromStr(ConfigReadToken(val, &idx).c_str());
    Path dllWait = PathFromStr(ConfigReadToken(val, &idx).c_str());

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
    string eq = ConfigReadToken(line, &idx);
    string val = ConfigReadToken(line, &idx);

    if (eq != "=") {
        LOG << "ERROR: '=' expected after variable name in: " << line << END;
        return false;
    }

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
    } else if (keyLow == "injectchildren") {
        G.InjectChildren = ConfigReadBoolVar(val);
    } else if (keyLow == "rumblewindow") {
        G.RumbleWindow = ConfigReadBoolVar(val);
    } else if (keyLow.starts_with("device")) {
        ConfigLoadDevice(keyLow, val);
    } else if (keyLow == "extrahook") {
        ConfigLoadExtraHook(val);
    } else {
        LOG << "ERROR: Invalid variable: " << key << END;
    }

    return true;
}

void ConfigLoadFrom(istream &file) {
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
}

void ConfigReset() {
    G.Trace = G.Debug = G.ApiTrace = G.ApiDebug = G.WaitDebugger = false;
    G.Forward = G.Always = G.Disable = G.InjectChildren = G.RumbleWindow = false;
    G.Keyboard.IsMapped = G.Mouse.IsMapped = false;
    G.Mouse.AnyMotionInput = G.Mouse.AnyMotionOutput = false;

    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        G.Users[i].Connected = G.Users[i].DeviceSpecified = false;
    }

    G.Keyboard.Reset();
    G.Mouse.Reset();
    G.ActiveUser = 0;
}

void ConfigSendGlobalEvents(bool added) {
    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        if (G.Users[i].Connected) {
            G.SendGlobalEvent(&G.Users[i], added);
        }
    }
}

static bool ConfigReloadNoUpdate() {
    ConfigReset();

    LOG << "Loading config from: " << G.ConfigPath << END;
    std::ifstream file(G.ConfigPath);
    if (file.fail()) {
        LOG << "ERROR: Failed to open: " << G.ConfigPath << END;
        return false;
    }
    ConfigLoadFrom(file);

    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        if (G.Users[i].Connected && !G.Users[i].DeviceSpecified) {
            G.Users[i].Device = new XDeviceIntf(i);
        }
    }
    return true;
}

bool ConfigLoad(Path &&path) {
    G.ConfigPath = move(path);
    return ConfigReloadNoUpdate();
}

void ConfigReload() {
    ConfigSendGlobalEvents(false);
    ConfigReloadNoUpdate();
    ConfigSendGlobalEvents(true);
    UpdateAll();
}
