#pragma once
#include "State.h"
#include "Devices.h"
#include <fstream>

int ConfigReadKey(const string &str, bool output = false) {
#define CONFIG_ON_KEY(vk, name, ...) \
    if (strLow == name)              \
        return vk;                   \
    __VA_OPT__(if (strLow == __VA_ARGS__) return vk;)

    string strLow = StrLowerCase(str);
    StrReplaceAll(strLow, ".", "");

    if (StrStartsWith(strLow, '%') && output) {
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
    if (StrToValue(str, &value) && value >= 1 && value <= 4) {
        return value - 1;
    }

    LOG << "ERROR: Invalid user (must be between 1 and 4): " << str << END;
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

double ConfigReadRange(const string &str, bool turbo) {
    if (!str.empty()) {
        double value;
        if (StrToValue(str, &value) && value > 0) {
            return value;
        }

        const char *what = turbo ? "time" : "range";
        LOG << "ERROR: Invalid " << what << " (must be number greater than 0) : " << str << END;
    }
    return turbo ? 0.02 : 50;
}

void ConfigLoadInputLine(const string &line) {
    string inputStr = StrTrimmed(StrBeforeFirst(line, ':'));
    string rest = StrTrimmed(StrAfterFirst(line, ':'));

    bool replace = false;
    bool forward = false;
    bool turbo = false;
    bool toggle = false;
    bool snap = false;
    while (StrContains(rest, '!')) {
        string optStr = StrLowerCase(StrTrimmed(StrAfterLast(rest, '!')));
        rest = StrTrimmed(StrBeforeLast(rest, '!'));

        if (optStr == "replace") {
            replace = true;
        } else if (optStr == "forward") {
            forward = true;
        } else if (optStr == "turbo") {
            turbo = true;
        } else if (optStr == "toggle") {
            toggle = true;
        } else if (optStr == "snap") {
            snap = true;
        } else {
            LOG << "ERROR: Invalid option: " << optStr << END;
        }
    }

    ImplInputCond *rootCond = nullptr;
    while (StrContains(rest, '?')) {
        string condStr = StrTrimmed(StrAfterLast(rest, '?'));
        rest = StrTrimmed(StrBeforeLast(rest, '?'));

        auto cond = new ImplInputCond();
        if (StrStartsWith(condStr, '~')) {
            condStr = StrTrimmed(StrAfterFirst(condStr, '~'));
        } else {
            cond->State = true;
        }

        cond->Key = ConfigReadKey(condStr);

        cond->Next = rootCond;
        rootCond = cond;
    }

    string userStr = StrTrimmed(StrAfterFirst(rest, '@'));
    rest = StrTrimmed(StrBeforeFirst(rest, '@'));

    string rangeStr = StrTrimmed(StrAfterFirst(rest, '^'));
    rest = StrTrimmed(StrBeforeFirst(rest, '^'));

    string outputStr = StrTrimmed(StrBeforeFirst(rest, '~'));
    string strengthStr = StrTrimmed(StrAfterFirst(rest, '~'));

    int input = ConfigReadKey(inputStr);
    int output = ConfigReadKey(outputStr, true);
    double strength = ConfigReadStrength(strengthStr);
    double range = ConfigReadRange(rangeStr, turbo);
    int user = ConfigReadUser(userStr);

    int nextInput = 0;
    int inputFlags = GetKeyType(input);
    if (inputFlags & MY_TYPE_PAIR) {
        tie(input, nextInput) = GetKeyPair(input);
        inputFlags &= ~MY_TYPE_PAIR;
    }

    int userIndex = user >= 0 ? user : 0;
    LOG << "Mapping " << inputStr << " to " << outputStr << " of player " << (userIndex + 1) << " (strength " << strength << ")" << END;

    while (input && output) {
        int outputFlags = GetKeyType(output);
        if (outputFlags & MY_TYPE_PAIR) {
            output = ImplChooseBestKeyInPair(output);
            outputFlags &= ~MY_TYPE_PAIR;
        }

        ImplInput *mapping = ImplGetInput(input);
        if (mapping) {
            if (replace) {
                mapping->Reset();
            } else if (mapping->IsValid()) {
                while (mapping->Next) {
                    mapping = mapping->Next;
                }

                mapping->Next = new ImplInput();
                mapping = mapping->Next;
            }

            mapping->Key = output;
            mapping->KeyType = GetKeyType(output);
            mapping->UserIndex = user;
            mapping->Strength = strength;
            mapping->Range = range;
            mapping->Conds = rootCond;
            mapping->Forward = forward;
            mapping->Turbo = turbo;
            mapping->Toggle = toggle;
            mapping->Snap = snap;

            int inputType = inputFlags & ~MY_TYPE_FLAGS;
            if (inputType == MY_TYPE_KEYBOARD) {
                G.Keyboard.IsMapped = true;
            } else if (inputType == MY_TYPE_MOUSE) {
                G.Mouse.IsMapped = true;
            }

            int outputType = outputFlags & ~MY_TYPE_FLAGS;
            if (outputType == MY_TYPE_PAD) {
                G.Users[userIndex].Connected = true;
            }
        }

        input = nextInput;
        nextInput = 0;
    }
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
    Path dllPath = PathFromStr(StrTrimmed(StrBeforeFirst(val, '|')).c_str());
    Path dllWait = PathFromStr(StrTrimmed(StrAfterFirst(val, '|')).c_str());

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

void ConfigLoadVarLine(const string &line) {
    string key = StrTrimmed(StrBeforeFirst(line, '=')).substr(1);
    string keyLow = StrLowerCase(key);
    string val = StrTrimmed(StrAfterFirst(line, '='));

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
    } else if (keyLow == "debugdupinputs") {
        G.DebugDupInputs = ConfigReadBoolVar(val);
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
    } else if (StrStartsWith(keyLow, "device")) {
        ConfigLoadDevice(keyLow, val);
    } else if (keyLow == "extrahook") {
        ConfigLoadExtraHook(val);
    } else {
        LOG << "ERROR: Invalid variable: " << key << END;
    }
}

void ConfigLoadFrom(istream &file) {
    string line;
    bool active = true;
    while (getline(file, line)) {
        if (StrIsBlank(line)) {
            continue;
        }

        if (StrStartsWith(line, '#')) {
            if (StrStartsWith(line, "#[")) {
                active = false;
            } else if (StrStartsWith(line, "#]")) {
                active = true;
            }
            continue;
        }

        if (active) {
            if (StrStartsWith(line, '!')) {
                ConfigLoadVarLine(line);
            } else {
                ConfigLoadInputLine(line);
            }
        }
    }
}

void ConfigReset() {
    G.Trace = G.Debug = G.ApiTrace = G.ApiDebug = G.WaitDebugger = G.DebugDupInputs = false;
    G.Forward = G.Always = G.Disable = G.InjectChildren = G.RumbleWindow = false;
    G.Keyboard.IsMapped = G.Mouse.IsMapped = false;

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

void ConfigDebugDupInputs() {
    for (int key = 0; key < MY_VK_LAST_MOUSE; key++) {
        auto input = ImplGetInput(key);
        if (input && !input->IsValid()) {
            input->Key = key;
            input->KeyType = GetKeyType(key);
            input->Forward = true;
        }

        if (key == MY_VK_LAST_REAL) {
            key = MY_VK_FIRST_MOUSE;
        }
    }

    G.Keyboard.IsMapped = G.Mouse.IsMapped = true;
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

    if (G.DebugDupInputs) {
        ConfigDebugDupInputs();
    }

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
