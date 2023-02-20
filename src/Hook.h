#pragma once
#include "LogUtils.h"
#include <detours.h>

void CheckHookError(LONG error) {
    ASSERT(error == NO_ERROR, "Failed hooking");
}

class Hook {
public:
    void *pReal;
    void *hook;

    void Attach() { CheckHookError(DetourAttach((void **)pReal, hook)); }
    void Detach() { CheckHookError(DetourDetach((void **)pReal, hook)); }
};

vector<Hook> GHooks;

void AddGlobalHook(void *pReal, void *hook) {
    GHooks.push_back(Hook{pReal, hook});
}

void SetGlobalHooks() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    for (auto &hook : GHooks) {
        hook.Attach();
    }
    CheckHookError(DetourTransactionCommit());
}

void ClearGlobalHooks() // (NOT safe like this, must go over threads)
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    for (auto &hook : GHooks) {
        hook.Detach();
    }
    CheckHookError(DetourTransactionCommit());
}
