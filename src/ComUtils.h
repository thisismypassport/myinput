#pragma once
#include <Windows.h>

template <class TIntf>
class ComRef {
    TIntf *mIntf;

public:
    ComRef() : mIntf(nullptr) {}
    ComRef(TIntf *intf) : mIntf(intf) {
        if (mIntf) {
            mIntf->AddRef();
        }
    }
    ~ComRef() {
        if (mIntf) {
            mIntf->Release();
        }
    }

    operator TIntf *() { return mIntf; }
    TIntf **operator&() { return &mIntf; }
    TIntf *operator->() { return mIntf; }
};

class Bstr {
    BSTR mValue;

public:
    Bstr(const wchar_t *input) { mValue = SysAllocString(input); }
    ~Bstr() { SysFreeString(mValue); }

    operator BSTR() { return mValue; }
};

class Variant {
    VARIANT mVariant;

public:
    Variant() { VariantInit(&mVariant); }
    Variant(int32_t value) : Variant() {
        mVariant.vt = VT_I4;
        mVariant.intVal = value;
    }
    Variant(bool value) : Variant() {
        mVariant.vt = VT_BOOL;
        mVariant.boolVal = value;
    }
    Variant(const wchar_t *input) : Variant() {
        mVariant.vt = VT_BSTR;
        mVariant.bstrVal = SysAllocString(input);
    }
    Variant(const vector<const wchar_t *> &inputs) {
        mVariant.vt = VT_ARRAY | VT_BSTR;
        SAFEARRAYBOUND bound = {(ULONG)inputs.size(), 0};
        mVariant.parray = SafeArrayCreate(VT_BSTR, 1, &bound);

        LONG idx = 0;
        for (auto &input : inputs) {
            SafeArrayPutElement(mVariant.parray, &idx, SysAllocString(input));
            idx++;
        }
    }
    ~Variant() { VariantClear(&mVariant); }

    operator VARIANT *() { return &mVariant; }
};
