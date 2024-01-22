#pragma once
#include "UtilsStr.h"
#include "Registry.h"
#include <fstream>

struct ExeEntry {
    Path ExeName;
    Path FullPath;
    bool UsePath = false;

    Path Config;

    // not stored in file
    bool InjectEnabled = false;
    bool InjectExact = false;
};

vector<ExeEntry> ExeConfigLoad(const wchar_t *path, const RegIfeoKey *ifeo) {
    vector<ExeEntry> entries;

    std::ifstream file(path);
    if (!file.fail()) {
        string line;
        while (getline(file, line)) {
            line = StrTrimmed(line);
            if (line.empty()) {
                continue;
            }

            int numArgs = 0;
            LPWSTR *args = CommandLineToArgvW(PathFromStr(line.c_str()), &numArgs);

            if (numArgs > 0) {
                ExeEntry entry;
                entry.ExeName = args[0];

                for (int argI = 1; argI < numArgs; argI++) {
                    if (tstreq(args[argI], L"-p") && argI + 1 < numArgs) {
                        entry.FullPath = args[++argI];
                    } else if (tstreq(args[argI], L"-c") && argI + 1 < numArgs) {
                        entry.Config = args[++argI];
                    } else if (tstreq(args[argI], L"-u")) {
                        entry.UsePath = true;
                    } else {
                        LOG << "Warning: unrecognized args in the global config line: " << line << END;
                    }
                }

                entries.push_back(move(entry));
            }

            LocalFree(args);
        }
    }

    if (ifeo && *ifeo) {
        unordered_map<wstring_view, int> entriesByName;
        unordered_map<wstring_view, int> entriesByPath;
        for (int i = 0; i < entries.size(); i++) {
            if (entries[i].UsePath && entries[i].FullPath) {
                entriesByPath[entries[i].FullPath.Get()] = i;
            } else {
                entriesByName[entries[i].ExeName.Get()] = i;
            }
        }

        for (auto &exe : ifeo->GetRegisteredExes()) {
            auto iter = exe.FullPath ? entriesByPath.find(exe.FullPath.Get()) : entriesByName.find(exe.Name.Get());

            ExeEntry *entry;
            if (iter != (exe.FullPath ? entriesByPath.end() : entriesByName.end())) {
                entry = &entries[iter->second];
            } else {
                bool usesPath = exe.FullPath;
                entries.push_back(ExeEntry{move(exe.Name), move(exe.FullPath), usesPath});
                entry = &entries.back();
            }

            entry->Config = move(exe.Config);
            entry->InjectEnabled = true;
            entry->InjectExact = exe.Exact;
        }
    }

    return entries;
}

bool ExeConfigSave(const wchar_t *path, const vector<ExeEntry> &entries) {
    std::ofstream file(path);

    for (auto &entry : entries) {
        // escaping isn't needed since these are all paths or filenames

        file << "\"" << entry.ExeName << "\"";

        if (entry.FullPath) {
            file << " -p \"" << entry.FullPath << "\"";
        }
        if (entry.Config) {
            file << " -c \"" << entry.Config << "\"";
        }
        if (entry.UsePath) {
            file << " -u";
        }

        file << "\n";
    }

    return !file.fail();
}

class RegIfeoKeyDelegatedViaNamedPipe : public RegIfeoKeyDelegated {
protected:
    HANDLE CreateDelegation() override {
        Path pipeName;
        HANDLE handle = CreatePipe(&pipeName);
        if (!handle) {
            Alert(L"Failed creating pipe");
            return nullptr;
        }

        Path regPath = PathCombine(PathGetDirName(PathGetModulePath(nullptr)), L"myinput_register.exe");
        Path regArgs = PathConcatRaw(L"-pipe ", pipeName);

        SHELLEXECUTEINFOW exec = {};
        exec.cbSize = sizeof(exec);
        exec.lpVerb = L"runas";
        exec.lpFile = regPath;
        exec.lpParameters = regArgs;
        exec.nShow = SW_HIDE;
        if (!ShellExecuteExW(&exec)) {
            DestroyDelegation(handle);
            Alert(L"Failed creating an elevated myinput_register.exe");
            return nullptr;
        }

        if (!ConnectNamedPipe(handle, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            DestroyDelegation(handle);
            Alert(L"Failed waiting for connection from myinput_register.exe");
            return nullptr;
        }

        return handle;
    }

    void DestroyDelegation(HANDLE handle) override {
        CloseHandle(handle);
    }

    void ShowDelegationError() override {
        Alert(L"Failed executing action");
    }

public:
    ~RegIfeoKeyDelegatedViaNamedPipe() { End(); }

    using RegIfeoKeyDelegated::SetHandle;
};

RegIfeoKeyDelegatedViaNamedPipe *GetDelegatedIfeo() {
    static RegIfeoKeyDelegatedViaNamedPipe gDelegatedIfeo;
    return &gDelegatedIfeo;
}

void UpdateExeEntryInIfeo(ExeEntry *entry) {
    const wchar_t *fullPath = entry->UsePath ? entry->FullPath.Get() : nullptr;

    auto ifeo = GetDelegatedIfeo();
    if (entry->InjectEnabled) {
        ifeo->RegisterExe(entry->ExeName, fullPath, entry->Config);
        entry->InjectExact = true; /// TODO - ...
    } else {
        ifeo->UnregisterExe(entry->ExeName, fullPath);
    }
}
