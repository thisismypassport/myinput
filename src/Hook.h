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
// for now, let's have the threadlocal solution available via a #define...

#ifndef MYINPUT_THREAD_LOCAL_DETECTOR
class RedirectDetector {
    void *Start = nullptr;
    void *End = nullptr;
    RedirectDetector *More = nullptr;

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
                More = new RedirectDetector();
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

#define REDIRECT_DETECT(detector, caller, origCall) \
    if (detector.Contains(caller))                  \
        return origCall();

#else
class RedirectDetector {
    DWORD tls;

public:
    // wasting tls slots...
    RedirectDetector() { tls = TlsAlloc(); }
    ~RedirectDetector() { TlsFree(tls); }

    void Add(void *addr) {} // irrelevant in this mode

    class Scope {
        RedirectDetector &Parent;
        bool PrevEntered;

    public:
        Scope(RedirectDetector &parent) : Parent(parent) {
            PrevEntered = (intptr_t)TlsGetValue(parent.tls);
            if (!PrevEntered) {
                TlsSetValue(parent.tls, (void *)(intptr_t)1);
            }
        }
        ~Scope() {
            TlsSetValue(Parent.tls, (void *)(intptr_t)PrevEntered);
        }
        bool WasEntered() { return PrevEntered; }
    };
};

// (if caller is null, this is an internal call that should never be detected as a redirect)
#define REDIRECT_DETECT(detector, caller, origCall) \
    RedirectDetector::Scope scope(detector);        \
    if (caller && scope.WasEntered())               \
        return origCall();

#endif

template <class TFunc>
void AddGlobalHook(TFunc *pReal, TFunc hook) {
    GHooks.push_back(Hook{pReal, hook});
}

template <class TFunc>
void AddGlobalHook(TFunc *pReal, TFunc hook, RedirectDetector *range) {
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
