#pragma once
#include "Log.h"
#include <Windows.h>

#ifdef _WIN64

#pragma pack(push, 1)
struct Thunk {
    byte mov_imm_to_r9[2] = {0x49, 0xb9};
    void *data_ptr;
    byte mov_imm_to_rax[2] = {0x48, 0xb8};
    void *code_ptr;
    byte jmp_to_rax[3] = {0x48, 0xff, 0xe0};
    byte padding = 0xcc;

    Thunk(void *code, void *data) : code_ptr(code), data_ptr(data) {}
};
#pragma pack(pop)

#define AUGHOOKPROC_CALLCONV CALLBACK
#define AUGHOOKPROC_PARAMS(a, b, c, d) a, b, c, d

#else

#pragma pack(push, 1)
struct Thunk {
    byte mov_imm_to_ecx = 0xb9;
    void *data_ptr = nullptr;
    byte mov_imm_to_eax = 0xb8;
    void *code_ptr = nullptr;
    byte jmp_to_eax[2] = {0xff, 0xe0};
    byte padding[4] = {0xcc, 0xcc, 0xcc, 0xcc};

    Thunk(void *code, void *data) : code_ptr(code), data_ptr(data) {}
};
#pragma pack(pop)

#define AUGHOOKPROC_CALLCONV __fastcall
#define AUGHOOKPROC_PARAMS(a, b, c, d) d, void *, a, b, c

#endif

typedef LRESULT(AUGHOOKPROC_CALLCONV *AugmentedHookProc)(AUGHOOKPROC_PARAMS(int code, WPARAM wParam, LPARAM lParam, void *data));

class ThunkAlloc {
    mutex mMutex;
    byte *mNext = nullptr;
    byte *mEnd = nullptr;

public:
    Thunk *Alloc() {
        lock_guard<mutex> lock(mMutex);
        if (!mNext || mEnd - mNext < sizeof(Thunk)) {
            SYSTEM_INFO info;
            GetSystemInfo(&info);
            mNext = (byte *)_aligned_malloc(info.dwPageSize, info.dwPageSize);
            ASSERT(mNext, "Cannot allocate aligned");
            mEnd = mNext + info.dwPageSize;

            DWORD old;
            ASSERT(VirtualProtect(mNext, info.dwPageSize, PAGE_EXECUTE_READWRITE, &old), "Cannot unprotect thunk page");
        }

        Thunk *thunk = (Thunk *)mNext;
        mNext += sizeof(Thunk);
        return thunk;
    }

} GThunkAlloc;

HOOKPROC AllocHookProcThunk(AugmentedHookProc code, void *data) {
    Thunk *thunk = GThunkAlloc.Alloc();
    *thunk = Thunk(code, data);
    FlushInstructionCache(GetCurrentProcess(), thunk, sizeof(Thunk));
    return (HOOKPROC)thunk;
}
