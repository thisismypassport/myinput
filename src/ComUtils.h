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
    ComRef(const ComRef<TIntf> &other) : mIntf(other.mIntf) {
        if (mIntf) {
            mIntf->AddRef();
        }
    }
    ComRef(ComRef<TIntf> &&other) : mIntf(other.mIntf) { other.mIntf = nullptr; }
    ~ComRef() {
        if (mIntf) {
            mIntf->Release();
        }
    }

    ComRef<TIntf> &operator=(const ComRef<TIntf> &other) {
        if (mIntf == other.mIntf) {
            return *this;
        }
        if (mIntf) {
            mIntf->Release();
        }
        mIntf = other.mIntf;
        if (mIntf) {
            mIntf->AddRef();
        }
        return *this;
    }
    ComRef<TIntf> &operator=(ComRef<TIntf> &&other) {
        if (mIntf) {
            mIntf->Release();
        }
        mIntf = other.mIntf;
        other.mIntf = nullptr;
        return *this;
    }

    TIntf *Take() {
        TIntf *intf = mIntf;
        mIntf = nullptr;
        return intf;
    }
    TIntf *TakeCopy() {
        if (mIntf) {
            mIntf->AddRef();
        }
        return mIntf;
    }

    TIntf *Get() const { return mIntf; }
    operator TIntf *() const { return mIntf; }
    TIntf *operator->() const { return mIntf; }
    TIntf **operator&() { return &mIntf; }
};

class Bstr {
    BSTR mValue;

public:
    Bstr() : mValue(nullptr) {}
    Bstr(const wchar_t *input) : mValue(input ? SysAllocString(input) : nullptr) {}
    Bstr(const Bstr &other) = delete;
    ~Bstr() {
        if (mValue) {
            SysFreeString(mValue);
        }
    }

    Bstr &operator=(const Bstr &other) = delete;

    const BSTR Get() const { return mValue; }
    operator const BSTR() const { return mValue; }
    BSTR *operator&() { return &mValue; }
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
    Variant(const Variant &other) = delete;
    ~Variant() { VariantClear(&mVariant); }

    Variant &operator=(const Variant &other) = delete;

    VARIANT Take() {
        VARIANT var = mVariant;
        VariantInit(&mVariant);
        return var;
    }

    operator VARIANT &() { return mVariant; }
    operator VARIANT *() { return &mVariant; }
    VARIANT *operator&() { return &mVariant; }

    bool IsBstr() { return mVariant.vt == VT_BSTR; }

    const wchar_t *GetBstr() {
        DBG_ASSERT(mVariant.vt == VT_BSTR, "bad type");
        return mVariant.bstrVal;
    }
};

template <class T, class... TSupers>
class ComBase : public T {
    atomic<int> mRefCount = 0;

    template <class TSuper>
    bool QueryInterfaceSuper(REFIID riid, void **ppvObject) {
        if (riid == __uuidof(TSuper)) {
            *ppvObject = (TSuper *)this;
        } else {
            return false;
        }
        return true;
    }

public:
    STDMETHODIMP_(ULONG)
    AddRef(void) override {
        return ++mRefCount;
    }

    STDMETHODIMP_(ULONG)
    Release(void) override {
        int refCount = --mRefCount;
        if (refCount == 0) {
            delete this;
        }
        return refCount;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override {
        *ppvObject = nullptr;
        if (riid == __uuidof(T)) {
            *ppvObject = (T *)this;
        } else if (riid == __uuidof(IUnknown)) {
            *ppvObject = (IUnknown *)this;
        } else if (!(QueryInterfaceSuper<TSupers>(riid, ppvObject) || ...)) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
};
