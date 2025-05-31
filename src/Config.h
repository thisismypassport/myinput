#pragma once
#include "ConfigRead.h"
#include "StateUtils.h"
#include "Devices.h"
#include <fstream>

struct ConfigState {
    list<Path> LoadedFiles;
    unordered_set<wstring_view> LoadedFilesSet;
    Path MainFile;
    Path Directory;
    ConfigCustom Custom;
    ConfigPlugin *CurrPlugin = nullptr;
    vector<function<void(const string &)>> CustomVarCbs;
    vector<function<void(int, bool)>> CustomDeviceCbs;
} GConfig;

bool ConfigLoadFrom(const wchar_t *filename);

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
    auto cfg = ConfigReadInputLine(GConfig.Custom, line);
    if (!cfg) {
        return false;
    }

    SharedPtr<ImplMapping> nextSrcCfg;
    if (cfg->SrcType.Relative && (cfg->Toggle || cfg->Turbo)) {
        LOG_W << "ERROR: toggle & turbo aren't supported for relative input" << END;
        cfg->Toggle = cfg->Turbo = false;
    }

    if (cfg->SrcType.AndPair || cfg->SrcType.OrPair) {
        bool isAnd = cfg->SrcType.AndPair;
        cfg->SrcType.AndPair = cfg->SrcType.OrPair = false;
        nextSrcCfg = SharedPtr<ImplMapping>::New(*cfg);
        tie(cfg->SrcKey, nextSrcCfg->SrcKey) = GetKeyPair(cfg->SrcKey);

        if (isAnd) {
            auto andCond = SharedPtr<ImplCond>::New();
            andCond->State = true;
            andCond->Key = nextSrcCfg->SrcKey;
            andCond->Next = cfg->Conds;
            cfg->Conds = andCond;

            auto nextAndCond = SharedPtr<ImplCond>::New();
            nextAndCond->State = true;
            nextAndCond->Key = cfg->SrcKey;
            nextAndCond->Next = nextSrcCfg->Conds;
            nextSrcCfg->Conds = nextAndCond;
        }
    }

    while (cfg && cfg->SrcKey && cfg->DestKey) {
        SharedPtr<ImplMapping> nextDestCfg;

        if (cfg->DestType.OrPair) {
            cfg->DestType.OrPair = false;
            cfg->DestKey = ImplChooseBestKeyInPair(cfg->DestKey);
        } else if (cfg->DestType.AndPair) {
            cfg->DestType.AndPair = false;
            nextDestCfg = SharedPtr<ImplMapping>::New(*cfg);
            tie(cfg->DestKey, nextDestCfg->DestKey) = GetKeyPair(cfg->DestKey);
        }

        while (cfg && cfg->DestKey) {
            if (cfg->Conds && (!cfg->Add || cfg->Reset)) {
                ConfigCreateResets(cfg, cfg->Conds);
            }

            ImplInput *input = ImplGetInput(cfg->SrcKey, cfg->SrcUser);
            if (input) {
                cfg->DestSlot = ImplAllocSlot(cfg->DestKey, cfg->DestUser);

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

            cfg = nextDestCfg;
            nextDestCfg = nullptr;
        }

        cfg = nextSrcCfg;
        nextSrcCfg = nullptr;
    }

    return true;
}

void ConfigLoadDevice(ConfigCustom &custom, user_t userIdx, const string &type) {
    ImplUser *user = ImplGetUser(userIdx, true);
    if (user) {
        ConfigDevice typeId = ConfigReadDeviceName(custom, type);

        DeviceIntf *device = nullptr;
        switch (typeId) {
        case ConfigDevice::XBox:
            device = new XDeviceIntf(userIdx);
            break;
        case ConfigDevice::Ds4:
            device = new Ds4DeviceIntf(userIdx);
            break;
        case ConfigDevice::None:
            device = new NoDeviceIntf(userIdx);
            break;
        default:
            if (typeId >= ConfigDevice::CustomStart) {
                device = new CustomDeviceIntf(userIdx, typeId - ConfigDevice::CustomStart);
            } else {
                LOG_W << "ERROR: Invalid device: " << type << END;
            }
        }

        if (device) {
            user->Device = device;
            user->DeviceSpecified = true;
        }
        user->Connected = true;
    } else {
        LOG_W << "ERROR: User must be specified for Device" << END;
    }
}

void ConfigSetDeviceStickShape(user_t userIdx, const string &value) // TODO - generalize this all..
{
    auto shape = ConfigReadStickShape(value);

    ImplUser *user = ImplGetUser(userIdx, true);
    if (user) {
        user->StickShape = shape;
    } else {
        LOG_W << "ERROR: User must be specified for Device info" << END;
    }
}

void ConfigLoadInclude(const string &val) {
    ConfigLoadFrom(ConfigReadInclude(val));
}

bool ConfigLoadPlugin(ConfigPlugin *plugin, const string &name) {
    Path path = ConfigReadPlugin(name, GConfig.Directory);
    if (!path) {
        return false;
    }

    path = PathCombine(path, PathGetBaseNamePtr(PathGetDirName(PathGetModulePath(G.HInstance))));

    path = PathCombine(path, PathFromStr((name + "_hook.dll").c_str()));

    GConfig.CurrPlugin = plugin;

    if (G.Debug) {
        LOG << "Loading plugin: " << path << END;
    }

    // Load it immediately, to allow registering config extensions
    // (note: this means calling dll load from dllmain, which is not legal but seems-ok)
    // (only alternative I see is a complicated second injection, to avoid config loading from dllmain)
    bool ok = LoadLibraryExW(path, nullptr, 0);
    if (!ok) {
        LOG_W << "ERROR: Failed loading plugin: " << path << " due to: " << GetLastError() << END;
    }

    GConfig.CurrPlugin = nullptr;
    return ok;
}

void ConfigLoadVarLine(const string &line, intptr_t idx) {
    ConfigReadVarLine(
        GConfig.Custom, line, idx, [](auto, bool value, bool *ptr) { *ptr = value; }, [&line](ConfigVar var, const string &rest, user_t user) {
        switch (var)
        {
        case ConfigVar::Include:
            ConfigLoadInclude (rest);
            break;

        case ConfigVar::Device:
            ConfigLoadDevice (GConfig.Custom, user, rest);
            break;

        case ConfigVar::StickShape:
            ConfigSetDeviceStickShape (user, rest);
            break;

        case ConfigVar::Comment:
            break;

        default:
            if (var >= ConfigVar::CustomStart){
                GConfig.CustomVarCbs[var - ConfigVar::CustomStart] (rest);
}
            break;
        } });
}

bool ConfigLoadFrom(const wchar_t *filename) {
    if (GConfig.LoadedFilesSet.contains(filename)) {
        LOG_W << "ERROR: Duplicate loading of config " << filename << END;
        return false;
    }

    GConfig.LoadedFiles.push_back(filename);
    GConfig.LoadedFilesSet.insert(GConfig.LoadedFiles.back().Get());

    Path path = PathCombine(GConfig.Directory, filename);
    LOG << "Loading config from: " << path << END;
    std::ifstream file(path);
    if (file.fail()) {
        LOG_W << "ERROR: Failed to open: " << path << END;
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

static void ConfigSendGlobalEvents(bool added, bool onInit = false) {
    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        if (G.Users[i].Connected) {
            G.GlobalCallbacks.Call(&G.Users[i], added, onInit);
        }
    }
}

constexpr const wchar_t *ConfigEmpty = L"<empty>.ini";
constexpr const wchar_t *ConfigDefault = L"_default.ini";

static void ConfigFinalizeUser(int idx, ImplUser *user) {
    if (user->Connected) {
        if (!user->DeviceSpecified) {
            user->Device = new XDeviceIntf(idx);
        }

        if (user->StickShape == ImplStickShape::Default && user->Device) {
            user->StickShape = user->Device->DefaultStickShape();
        }
    }
}

static void ConfigReloadNoUpdate() {
    ConfigReset();

    if (!tstreq(GConfig.MainFile, ConfigEmpty)) {
        if (!ConfigLoadFrom(GConfig.MainFile)) {
            ConfigLoadFrom(ConfigDefault);
        }
    }

    // for now, we leak any old Device (this is relied upon by e.g. ThreadPoolNotificationRegister, could refcount?)
    // (note: the device is accessed from other threads for short durations as well...)
    for (int i = 0; i < IMPL_MAX_USERS; i++) {
        ConfigFinalizeUser(i, &G.Users[i]);
    }
}

void ConfigInit(Path &&path, Path &&name) {
    GConfig.Directory = move(path);
    GConfig.MainFile = move(name);
    ConfigReloadNoUpdate();
    ConfigSendGlobalEvents(true, true);
}

void ConfigReload() {
    ImplAbortMappings();
    ConfigSendGlobalEvents(false);
    ConfigReloadNoUpdate();
    ConfigSendGlobalEvents(true);
    UpdateAll();
}

void ConfigLoad(const wchar_t *name) {
    GConfig.MainFile = PathCombineExt(name, L"ini");
    ConfigReload();
}

bool ConfigCheckCurrPlugin() {
    if (GConfig.CurrPlugin) {
        return true;
    }

    Fatal("can only be called in plugin dllmain");
    return false;
}

int ConfigRegisterCustomKey(const char *name, function<void(const InputValue &, const ImplMapping *)> &&cb) {
    if (!ConfigCheckCurrPlugin()) {
        return -1;
    }

    int index = (int)G.CustomKeys.size();

    auto custKey = UniquePtr<ImplCustomKey>::New();
    custKey->Callback = move(cb);
    G.CustomKeys.push_back(move(custKey));

    string nameLow = ConfigNormStr(name);
    GConfig.CurrPlugin->Keys[nameLow] = index;

    return index;
}

int ConfigRegisterCustomVar(const char *name, function<void(const string &)> &&cb) {
    if (!ConfigCheckCurrPlugin()) {
        return -1;
    }

    int index = (int)GConfig.CustomVarCbs.size();
    GConfig.CustomVarCbs.push_back(move(cb));

    string nameLow = name;
    StrToLowerCase(nameLow);
    GConfig.CurrPlugin->Vars[nameLow] = index;

    return index;
}

int ConfigRegisterCustomDevice(const char *name, function<void(int, bool)> &&cb) {
    if (!ConfigCheckCurrPlugin()) {
        return -1;
    }

    int index = (int)GConfig.CustomDeviceCbs.size();
    GConfig.CustomDeviceCbs.push_back(move(cb));

    string nameLow = name;
    StrToLowerCase(nameLow);
    GConfig.CurrPlugin->Devices[nameLow] = index;

    if (index == 0) // first time
    {
        G.GlobalCallbacks.Add([](ImplUser *user, bool added, bool onInit) {
            DeviceIntf *device = user->Device;
            if (device && device->CustomIdx >= 0) {
                GConfig.CustomDeviceCbs[device->CustomIdx](device->UserIdx, added);
            }
            return true;
        });
    }

    return index;
}
