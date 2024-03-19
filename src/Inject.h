#pragma once
#include "LogUtils.h"
#include "UtilsStr.h"
#include "UtilsPath.h"
#include <Windows.h>

#if ZERO
// the function defined in assembly below
DWORD InjectedFunc() {
    HMODULE module = LoadLibraryW(<myinput_hook.dll path>);
    if (!module) {
        return 0;
    }

    auto func = (void (*)())GetProcAddress(module, "MyInputHookInternal_WaitInit");
    if (!func) {
        return 0;
    }

    func();
    return 1;
}
#endif

#ifdef _WIN64

#pragma pack(push, 1)
struct InjectedFunc {
    byte sub_rsp_x28[4] = {0x48, 0x83, 0xec, 0x28};
    byte move_rax_imm[2] = {0x48, 0xb8};
    void *loadlib_func;
    byte move_rcx_imm[2] = {0x48, 0xb9};
    void *dllpath_ptr;
    byte call_rax[2] = {0xff, 0xd0};

    byte cmp_rax_0[4] = {0x48, 0x83, 0xf8, 0x00};
    byte je_bad = 0x74;
    byte bad_off = offsetof(InjectedFunc, xor_rax) - offsetof(InjectedFunc, bad_off) - 1;
    byte mov_rcx_rax[3] = {0x48, 0x89, 0xc1};
    byte move_rax_imm_n2[2] = {0x48, 0xb8};
    void *getprocaddr_func;
    byte move_rdx_imm[2] = {0x48, 0xba};
    void *procname_ptr;
    byte call_rax_n2[2] = {0xff, 0xd0};

    byte cmp_rax_0_n2[4] = {0x48, 0x83, 0xf8, 0x00};
    byte je_bad_n2 = 0x74;
    byte bad_off_n2 = offsetof(InjectedFunc, xor_rax) - offsetof(InjectedFunc, bad_off_n2) - 1;
    byte call_rax_n3[2] = {0xff, 0xd0};

    byte mov_rax_1[5] = {0xb8, 0x1, 0, 0, 0};
    byte add_rsp_x28[4] = {0x48, 0x83, 0xc4, 0x28};
    byte ret = 0xc3;

    byte xor_rax[2] = {0x31, 0xc0};
    byte add_rsp_x28_n2[4] = {0x48, 0x83, 0xc4, 0x28};
    byte ret_n2 = 0xc3;
};
#pragma pack(pop)

#else

#pragma pack(push, 1)
struct InjectedFunc {
    byte move_eax_imm = 0xb8;
    void *loadlib_func;
    byte push_imm = 0x68;
    void *dllpath_ptr;
    byte call_eax[2] = {0xff, 0xd0};

    byte cmp_eax_0[3] = {0x83, 0xf8, 0x00};
    byte je_bad = 0x74;
    byte bad_off = offsetof(InjectedFunc, xor_eax) - offsetof(InjectedFunc, bad_off) - 1;
    byte push_imm_n2 = 0x68;
    void *procname_ptr;
    byte push_eax = 0x50;
    byte move_eax_imm_n2 = 0xb8;
    void *getprocaddr_func;
    byte call_eax_n2[2] = {0xff, 0xd0};

    byte cmp_eax_0_n2[3] = {0x83, 0xf8, 0x00};
    byte je_bad_n2 = 0x74;
    byte bad_off_n2 = offsetof(InjectedFunc, xor_eax) - offsetof(InjectedFunc, bad_off_n2) - 1;
    byte call_eax_n3[2] = {0xff, 0xd0};

    byte mov_eax_1[5] = {0xb8, 0x1, 0, 0, 0};
    byte ret = 0xc3;

    byte xor_eax[2] = {0x31, 0xc0};
    byte ret_n2 = 0xc3;
};
#pragma pack(pop)

#endif

struct InjectedData {
    InjectedFunc func;
    char procname[32] = "MyInputHookInternal_WaitInit";

    InjectedData(void *baseAddr) {
        auto base = (InjectedData *)baseAddr;
        HMODULE kernel = GetModuleHandleA("kernel32.dll");

        func.loadlib_func = GetProcAddress(kernel, "LoadLibraryW");
        func.dllpath_ptr = base + 1; // placed right after us
        func.getprocaddr_func = GetProcAddress(kernel, "GetProcAddress");
        func.procname_ptr = &base->procname;
    }
};

bool DoInject(HANDLE process, const Path &dllPath) {
    size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    size_t injectSize = sizeof(InjectedData) + dllPathSize;

    LPVOID injectAddr = VirtualAllocEx(process, NULL, injectSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (injectAddr == NULL) {
        LOG_ERR << "Couldn't allocate memory in process: " << GetLastError() << END;
        return false;
    }

    auto injectAddrDtor = Destructor([process, injectAddr] {
        VirtualFreeEx(process, injectAddr, 0, MEM_RELEASE);
    });

    InjectedData injectData(injectAddr);

    if (!WriteProcessMemory(process, injectAddr, &injectData, sizeof(injectData), NULL) ||
        !WriteProcessMemory(process, injectData.func.dllpath_ptr, dllPath, dllPathSize, NULL)) {
        LOG_ERR << "Couldn't write memory to process: " << GetLastError() << END;
        return false;
    }

    DWORD oldProtect;
    if (!VirtualProtectEx(process, injectAddr, injectSize, PAGE_EXECUTE_READ, &oldProtect)) {
        LOG_ERR << "Couldn't protect memory in process: " << GetLastError() << END;
        return false;
    }

    HANDLE hRemote = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)injectAddr, NULL, 0, NULL);
    if (hRemote == NULL) {
        LOG_ERR << "Couldn't inject thread in process: " << GetLastError() << END;
        return false;
    }

    auto hRemoteDtor = Destructor([hRemote] {
        CloseHandle(hRemote);
    });

    WaitForSingleObject(hRemote, INFINITE);
    DWORD code = -1;
    if (!GetExitCodeThread(hRemote, &code) || code != 1) {
        LOG_ERR << "Injected func returned with exit code " << code << END;
        if (code != 0) {
            // exit code is unrecognized, so the thread died and the process is likely soon to follow
            // we kill it (just in case) & wait for it to die so that others know the process is dead
            TerminateProcess(process, code);
            WaitForSingleObject(process, INFINITE);
        }
        return false;
    }

    return true;
}

bool ReplaceWithOtherBitness(wstring *dirName) {
    bool replaced = false;
    if (StrContains(*dirName, L"Win32")) {
        replaced = StrReplaceFirst(*dirName, L"Win32", L"x64");
    } else if (StrContains(*dirName, L"x64")) {
        replaced = StrReplaceFirst(*dirName, L"x64", L"Win32");
    }
    return replaced;
}
