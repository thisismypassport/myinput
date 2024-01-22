#pragma once
#include "LogUtils.h"
#include <intrin.h>
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

// Used to avoid issues with A/W/Ex/etc variants redirecting to each other.
// (There are even cases where a variant redirects to another variant conditionally!)
// This is probably the worst solution, aside from all other solutions
// (e.g: using a threadlocal variable would not be able to recognize re-entrancy and would need to handle exceptions.)
// (well, maybe that's still more practical, though... reconsider if/when below gives trouble)
class AddrRange {
    void *Start = nullptr;
    void *End = nullptr;
    AddrRange *More = nullptr; // (probably not needed)

public:
    void Add(void *addr) {
        if (!addr) {
            return;
        }

        if (!Start) {
            MEMORY_BASIC_INFORMATION info;
            if (ASSERT(VirtualQuery(addr, &info, sizeof(info)), "Invalid addr")) {
                Start = info.AllocationBase;

                while (info.AllocationBase == Start) {
                    End = (byte *)info.BaseAddress + info.RegionSize;

                    if (!VirtualQuery(End, &info, sizeof(info))) {
                        break;
                    }
                }
            }
        } else if (addr < Start || addr >= End) {
            if (!More) {
                More = new AddrRange();
            }

            More->Add(addr);
        }
    }

    bool Contains(void *addr) const {
        if (addr >= Start && addr < End) {
            return true;
        } else if (More) {
            return More->Contains(addr);
        } else {
            return false;
        }
    }
};

template <class TFunc>
void AddGlobalHook(TFunc *pReal, TFunc hook) {
    GHooks.push_back(Hook{pReal, hook});
}

template <class TFunc>
void AddGlobalHook(TFunc *pReal, TFunc hook, AddrRange *range) {
    // We prefer to add individual func addrs instead of module addrs because - in theory - they may come from another module
    range->Add(*pReal);
    AddGlobalHook(pReal, hook);
}

#define ADD_GLOBAL_HOOK(func, ...) AddGlobalHook(&func##_Real, func##_Hook __VA_OPT__(, ) __VA_ARGS__)

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
