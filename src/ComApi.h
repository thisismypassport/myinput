#pragma once
#include "Hook.h"
#include "Header.h"
#include "ComUtils.h"

LimitedLog<50> gUniqLogComEscape;

template <class TActual, class... TIntfWraps>
class ComWrapperBase {
protected:
    tuple<TIntfWraps...> mIntfs;
    atomic<ULONG> mRefCount = 0;

public:
    ComWrapperBase() {
        apply([&](auto &...intfs) {
            (intfs.ComInit((TActual *)this), ...);
        },
              mIntfs);
    }

    void AddRef() { mRefCount++; }
    void Release() {
        ULONG refCount = --mRefCount;
        if (refCount == 0) {
            delete (TActual *)this;
        }
    }

    void *WrapInterface(REFIID intf, void *input) {
        void *result = nullptr;
        apply([&](auto &...intfs) {
            (intfs.TryWrapInterface(intf, input, &result) || ...);
        },
              mIntfs);

        if (!result && gUniqLogComEscape) {
            LOG_ERR << "COM: Unknown intf " << intf << " of " << typeid(TActual).name() << END;
        }
        return result;
    }

    template <class TIntf>
    TIntf *WrapInterface(TIntf *input) {
        return (TIntf *)WrapInterface(__uuidof(TIntf), input);
    }

    void UndoWrapInterface(void *intf) // removes the refcount that WrapInterface adds
    {
        apply([&](auto &...intfs) {
            (intfs.TryUndoWrapInterface(intf) || ...);
        },
              mIntfs);
    }

    void *UnwrapInterface(void *input, IID *outIntf) {
        void *result = nullptr;
        apply([&](auto &...intfs) {
            (intfs.TryUnwrapInterface(input, &result, outIntf) || ...);
        },
              mIntfs);
        return result;
    }
};

template <class TIntf, class TRoot>
class ComIntfWrapper : public TIntf {
protected:
    TRoot *mRoot = nullptr;
    WeakAtomic<TIntf *> mInner = nullptr;
    atomic<ULONG> mRefCount = 0;

    void OnAddRef() {
        mRefCount++;
        mRoot->AddRef();
    }
    void OnRelease() {
        mRefCount--;
        mRoot->Release(); // may delete this
    }

    bool TrySetInterface(void *inObj, void **outObj) {
        TIntf *expected = nullptr;
        TIntf *obj = (TIntf *)inObj;
        if (!mInner.compare_exchange(expected, obj) && expected != obj) {
            if (mRefCount != 0) {
                return false;
            }

            mInner = obj;
        }

        *outObj = (TIntf *)this;
        OnAddRef();
        return true;
    }

public:
    void ComInit(TRoot *root) { mRoot = root; }

    STDMETHODIMP_(ULONG)
    AddRef() override {
        ULONG result = mInner->AddRef();
        OnAddRef();
        return result;
    }
    STDMETHODIMP_(ULONG)
    Release() override {
        ULONG result = mInner->Release();
        OnRelease(); // may delete this
        return result;
    }

    STDMETHODIMP QueryInterface(REFIID intf, void **ptrObj) override {
        HRESULT res = mInner->QueryInterface(intf, ptrObj);
        if (res == S_OK && ptrObj) {
            void *result = mRoot->WrapInterface(intf, *ptrObj);
            if (result) {
                *ptrObj = result;
            }
        }
        return res;
    }

    bool TryWrapInterface(REFIID intf, void *inObj, void **outObj) {
        if (intf != __uuidof(TIntf)) {
            return false;
        }

        if (!this->TrySetInterface(inObj, outObj)) {
            // In this case, we need ComIntfMultiWrapper
            if (gUniqLogComEscape) {
                LOG_ERR << "COM: Intf " << __uuidof(TIntf) << " has multiple ptrs for " << typeid(TRoot).name() << END;
            }
            return false;
        }

        return true;
    }

    bool TryUndoWrapInterface(void *inObj) {
        if (inObj != (TIntf *)this) {
            return false;
        }

        OnRelease(); // may delete this
        return true;
    }

    bool TryUnwrapInterface(void *inObj, void **outObj, IID *outIntf) {
        if (inObj != (TIntf *)this) {
            return false;
        }

        *outObj = mInner.get();
        if (outIntf) {
            *outIntf = __uuidof(TIntf);
        }
        return true;
    }
};

template <class TIntf, class TRoot, class... TSupers>
class ComIntfWrapperWithSupers : public ComIntfWrapper<TIntf, TRoot> {
public:
    bool TryWrapInterface(REFIID intf, void *inObj, void **outObj) {
        if (((intf == __uuidof(TSupers)) || ...)) {
            return ComIntfWrapper<TIntf, TRoot>::TryWrapInterface(__uuidof(TIntf), inObj, outObj);
        } else {
            return ComIntfWrapper<TIntf, TRoot>::TryWrapInterface(intf, inObj, outObj);
        }
    }
};

template <class TActual, class TIntf, class TRoot>
class ComIntfMultiWrapper : public ComIntfWrapper<TIntf, TRoot> {
protected:
    UniquePtr<TActual> mNext;

public:
    bool TryWrapInterface(REFIID intf, void *inObj, void **outObj) {
        if (intf != __uuidof(TIntf)) {
            return false;
        }

        if (!this->TrySetInterface(inObj, outObj)) {
            if (!mNext) {
                mNext = UniquePtr<TActual>::New();
                mNext->ComInit(this->mRoot);
            }

            return mNext->TryWrapInterface(intf, inObj, outObj);
        }

        return true;
    }

    bool TryUndoWrapInterface(void *inObj) {
        if (ComIntfWrapper<TIntf, TRoot>::TryUndoWrapInterface(inObj)) {
            return true;
        }

        return mNext ? mNext->TryUndoWrapInterface(inObj) : false;
    }

    bool TryUnwrapInterface(void *inObj, void **outObj, IID *outIntf) {
        if (ComIntfWrapper<TIntf, TRoot>::TryUnwrapInterface(inObj, outObj, outIntf)) {
            return true;
        }

        return mNext ? mNext->TryUnwrapInterface(inObj, outObj, outIntf) : false;
    }
};

template <class TRoot>
using ComUnknownIntf = ComIntfWrapper<IUnknown, TRoot>;

template <class TCls>
void *WrapNewComInstance(REFIID intf, void *obj) {
    TCls *inst = new TCls();
    void *result = inst->WrapInterface(intf, obj);
    if (result) {
        return result;
    }

    delete inst;
    return obj;
}

template <class TCls>
void WrapNewComInstanceMultiQi(DWORD count, MULTI_QI *queries) {
    TCls *inst = new TCls();

    bool done = false;
    for (DWORD i = 0; i < count; i++) {
        MULTI_QI *query = &queries[i];
        if (query->hr == S_OK) {
            void *result = inst->WrapInterface(*query->pIID, query->pItf);
            if (result) {
                query->pItf = (IUnknown *)result;
                done = true;
            }
        }
    }

    if (!done) {
        delete inst;
    }
}

template <class TInst>
class ComClassFactory;

template <class TInst>
class ComClassFactoryIntf : public ComIntfWrapper<IClassFactory, ComClassFactory<TInst>> {
public:
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID intf, void **ptrObj) override {
        HRESULT res = this->mInner->CreateInstance(pUnkOuter, intf, ptrObj);
        if (res == S_OK && ptrObj) {
            *ptrObj = WrapNewComInstance<TInst>(intf, *ptrObj);
        }
        return res;
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        return this->mInner->LockServer(fLock);
    }
};

template <class TInst>
class ComClassFactory : public ComWrapperBase<ComClassFactory<TInst>, ComClassFactoryIntf<TInst>, ComUnknownIntf<ComClassFactory<TInst>>> {};

template <class TRoot>
IUnknown *ComUnwrapProxy(TRoot *root, void *proxy, IID *outIntf = nullptr) {
    auto unwrapped = (IUnknown *)root->UnwrapInterface(proxy, outIntf);
    return unwrapped ? unwrapped : (IUnknown *)proxy; // (nullptr and invalid ptrs (like proxy) behave differently)
}

// (observed to need Multi on EnumWbemClassObject, thus making proxy-ish intfs all multi, just in case)
template <class TRoot>
class ComClientSecurityIntf : public ComIntfMultiWrapper<ComClientSecurityIntf<TRoot>, IClientSecurity, TRoot> {
public:
    STDMETHODIMP QueryBlanket(IUnknown *pProxy, DWORD *pAuthnSvc, DWORD *pAuthzSvc, OLECHAR **pServerPrincName,
                              DWORD *pAuthnLevel, DWORD *pImpLevel, void **pAuthInfo, DWORD *pCapabilities) override {
        IUnknown *innerProxy = ComUnwrapProxy(this->mRoot, pProxy);
        return this->mInner->QueryBlanket(innerProxy, pAuthnSvc, pAuthzSvc, pServerPrincName, pAuthnLevel, pImpLevel, pAuthInfo, pCapabilities);
    }

    STDMETHODIMP SetBlanket(IUnknown *pProxy, DWORD dwAuthnSvc, DWORD dwAuthzSvc, OLECHAR *pServerPrincName,
                            DWORD dwAuthnLevel, DWORD dwImpLevel, void *pAuthInfo, DWORD dwCapabilities) override {
        IUnknown *innerProxy = ComUnwrapProxy(this->mRoot, pProxy);
        return this->mInner->SetBlanket(innerProxy, dwAuthnSvc, dwAuthzSvc, pServerPrincName, dwAuthnLevel, dwImpLevel, pAuthInfo, dwCapabilities);
    }

    STDMETHODIMP CopyProxy(IUnknown *pProxy, IUnknown **ppCopy) override {
        IID intf = {};
        IUnknown *innerProxy = ComUnwrapProxy(this->mRoot, pProxy, &intf);
        HRESULT res = this->mInner->CopyProxy(innerProxy, ppCopy);
        if (res == S_OK && ppCopy) {
            *ppCopy = (IUnknown *)this->mRoot->WrapInterface(intf, *ppCopy);
        }
        return res;
    }
};

template <class TRoot>
class ComRpcOptionsIntf : public ComIntfMultiWrapper<ComRpcOptionsIntf<TRoot>, IRpcOptions, TRoot> {
public:
    STDMETHODIMP Set(IUnknown *pProxy, RPCOPT_PROPERTIES dwProperty, ULONG_PTR dwValue) override {
        IUnknown *innerProxy = ComUnwrapProxy(this->mRoot, pProxy);
        return this->mInner->Set(innerProxy, dwProperty, dwValue);
    }

    STDMETHODIMP Query(IUnknown *pProxy, RPCOPT_PROPERTIES dwProperty, ULONG_PTR *pdwValue) override {
        IUnknown *innerProxy = ComUnwrapProxy(this->mRoot, pProxy);
        return this->mInner->Query(innerProxy, dwProperty, pdwValue);
    }
};

// TODO: needs more work
template <class TRoot>
class ComMarshalIntf : public ComIntfMultiWrapper<ComMarshalIntf<TRoot>, IMarshal, TRoot> {
public:
    STDMETHODIMP GetUnmarshalClass(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags, CLSID *pCid) override {
        ComRef<IMarshal> marshal;
        if (dwDestContext == MSHCTX_INPROC &&
            SUCCEEDED(CoGetStandardMarshal(riid, (IUnknown *)this, dwDestContext, pvDestContext, mshlflags, &marshal))) {
            return marshal->GetUnmarshalClass(riid, pv, dwDestContext, pvDestContext, mshlflags, pCid);
        }

        pv = pv ? ComUnwrapProxy(this->mRoot, pv) : pv;
        return this->mInner->GetUnmarshalClass(riid, pv, dwDestContext, pvDestContext, mshlflags, pCid);
    }
    STDMETHODIMP GetMarshalSizeMax(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags, DWORD *pSize) override {
        ComRef<IMarshal> marshal;
        if (dwDestContext == MSHCTX_INPROC &&
            SUCCEEDED(CoGetStandardMarshal(riid, (IUnknown *)this, dwDestContext, pvDestContext, mshlflags, &marshal))) {
            return marshal->GetMarshalSizeMax(riid, pv, dwDestContext, pvDestContext, mshlflags, pSize);
        }

        pv = pv ? ComUnwrapProxy(this->mRoot, pv) : pv;
        return this->mInner->GetMarshalSizeMax(riid, pv, dwDestContext, pvDestContext, mshlflags, pSize);
    }
    STDMETHODIMP MarshalInterface(IStream *pStm, REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags) override {
        ComRef<IMarshal> marshal;
        if (dwDestContext == MSHCTX_INPROC &&
            SUCCEEDED(CoGetStandardMarshal(riid, (IUnknown *)this, dwDestContext, pvDestContext, mshlflags, &marshal))) {
            return marshal->MarshalInterface(pStm, riid, pv, dwDestContext, pvDestContext, mshlflags);
        }

        pv = pv ? ComUnwrapProxy(this->mRoot, pv) : pv;
        return this->mInner->MarshalInterface(pStm, riid, pv, dwDestContext, pvDestContext, mshlflags);
    }
    STDMETHODIMP UnmarshalInterface(IStream *pStm, REFIID riid, void **ppv) override {
        return this->mInner->UnmarshalInterface(pStm, riid, ppv);
    }
    STDMETHODIMP ReleaseMarshalData(IStream *pStm) override {
        return this->mInner->ReleaseMarshalData(pStm);
    }
    STDMETHODIMP DisconnectObject(DWORD dwReserved) override {
        return this->mInner->DisconnectObject(dwReserved);
    }
};

#define COM_RPC_INTERFACES(TRoot) ComRpcOptionsIntf<TRoot>, ComClientSecurityIntf<TRoot>, ComMarshalIntf<TRoot>, ComUnknownIntf<TRoot>

void WrapClassIfNeeded(REFCLSID rclsid, REFIID riid, LPVOID *ppv);
void WrapInstanceIfNeeded(REFCLSID rclsid, REFIID riid, LPVOID *ppv);
void WrapInstancesIfNeeded(REFCLSID rclsid, DWORD dwCount, MULTI_QI *pResults);

HRESULT WINAPI CoCreateInstance_Hook(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsCtx, REFIID riid, LPVOID *ppv) {
    HRESULT res = CoCreateInstance_Real(rclsid, pUnkOuter, dwClsCtx, riid, ppv);
    if (res == S_OK && ppv) {
        WrapInstanceIfNeeded(rclsid, riid, ppv);
    }
    return res;
}
HRESULT WINAPI CoCreateInstanceEx_Hook(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsCtx, COSERVERINFO *pServerInfo, DWORD dwCount, MULTI_QI *pResults) {
    HRESULT res = CoCreateInstanceEx_Real(rclsid, pUnkOuter, dwClsCtx, pServerInfo, dwCount, pResults);
    if (res == S_OK) {
        WrapInstancesIfNeeded(rclsid, dwCount, pResults);
    }
    return res;
}
HRESULT WINAPI CoCreateInstanceFromApp_Hook(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsCtx, PVOID reserved, DWORD dwCount, MULTI_QI *pResults) {
    HRESULT res = CoCreateInstanceFromApp_Real(rclsid, pUnkOuter, dwClsCtx, reserved, dwCount, pResults);
    if (res == S_OK) {
        WrapInstancesIfNeeded(rclsid, dwCount, pResults);
    }
    return res;
}
HRESULT WINAPI CoGetClassObject_Hook(REFCLSID rclsid, DWORD dwClsCtx, LPVOID reserved, REFIID riid, LPVOID *ppv) {
    HRESULT res = CoGetClassObject_Real(rclsid, dwClsCtx, reserved, riid, ppv);
    if (res == S_OK && ppv) {
        WrapClassIfNeeded(rclsid, riid, ppv);
    }
    return res;
}

void HookCom() {
    ADD_GLOBAL_HOOK(CoCreateInstance);
    ADD_GLOBAL_HOOK(CoCreateInstanceEx);
    ADD_GLOBAL_HOOK(CoCreateInstanceFromApp);
    ADD_GLOBAL_HOOK(CoGetClassObject);
}
