#pragma once
#include "ComApi.h"
#include <WbemIdl.h>

// (best effort basis) (while hopefully not breaking much)

UniqueLog gUniqLogWbemUsed;

enum class MyWbemNamespaceType {
    None,
    Root,
    Cimv2,
    Other,
};

MyWbemNamespaceType ParseNamespace(MyWbemNamespaceType type, BSTR str, bool allowAbs) {
    if (!str || !*str) {
        return type;
    }

    const wchar_t *delims = L"\\/";

    if (allowAbs && wcschr(delims, str[0]) && wcschr(delims, str[1])) {
        wchar_t *end = wcspbrk(str + 2, delims);
        if (!end) {
            return type;
        }
        str = end + 1;
    }

    while (str && *str) {
        wchar_t *end = wcspbrk(str, delims);
        size_t len = end ? end - str : wcslen(str);

        if (type == MyWbemNamespaceType::None && tstrnieq(str, L"root", len)) {
            type = MyWbemNamespaceType::Root;
        } else if (type == MyWbemNamespaceType::Root && tstrnieq(str, L"cimv2", len)) {
            type = MyWbemNamespaceType::Cimv2;
        } else {
            type = MyWbemNamespaceType::None;
        }

        str = end ? end + 1 : nullptr;
    }

    return type;
}

#define MY_WBEM_PNP_CLASS L"Win32_PnPEntity"
#define MY_WBEM_PNP_CLASS_BASE L"CIM_LogicalDevice"
#define MY_WBEM_PNP_CLASS_BASE_2 L"CIM_LogicalElement"
#define MY_WBEM_PNP_CLASS_BASE_TOP L"CIM_ManagedSystemElement"

IWbemClassObject *CreateDeviceClassObject(IWbemServices *services, int devNodeIdx) {
    DeviceNode *node = ImplGetDeviceNode(devNodeIdx);
    if (!node) {
        return nullptr;
    }

    ComRef<IWbemClassObject> clsObj;
    HRESULT res = services->GetObject(Bstr(MY_WBEM_PNP_CLASS), 0, nullptr, &clsObj, nullptr);

    IWbemClassObject *obj = nullptr;
    if (res == S_OK) {
        res = clsObj->SpawnInstance(0, &obj);
    }
    if (res == S_OK) {
        res = obj->Put(L"Caption", 0, Variant(node->DeviceDescription<wchar_t>()), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"ClassGuid", 0, Variant(node->DeviceClassGuidStr<wchar_t>()), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"ConfigManagerErrorCode", 0, Variant((int32_t)0), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"ConfigManagerUserConfig", 0, Variant((bool)false), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"CreationClassName", 0, Variant(MY_WBEM_PNP_CLASS), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"Description", 0, Variant(node->DeviceDescription<wchar_t>()), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"DeviceID", 0, Variant(node->DeviceInstNameW), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"HardwareID", 0, Variant(node->DeviceHardwareIDs<wchar_t>()), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"Manufacturer", 0, Variant(node->DeviceManufacturer<wchar_t>()), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"Name", 0, Variant(node->DeviceDescription<wchar_t>()), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"PNPClass", 0, Variant(node->DeviceClass<wchar_t>()), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"PNPDeviceID", 0, Variant(node->DeviceInstNameW), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"Present", 0, Variant((bool)true), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"Status", 0, Variant(L"OK"), 0);
    }
    if (res == S_OK) {
        res = obj->Put(L"SystemCreationClassName", 0, Variant(L"Win32_ComputerSystem"), 0);
    }
    // TODO: SystemName, private system props (can't be put...)

    if (res != S_OK) {
        obj->Release();
        LOG_ERR << "Wbem: failed creating device instance" << END;
        return nullptr;
    }
    return obj;
}

class MyWbemEnumClassObject;

class MyWbemEnumClassObjectIntf : public ComIntfMultiWrapper<MyWbemEnumClassObjectIntf, IEnumWbemClassObject, MyWbemEnumClassObject> {
    IWbemServices *mServices = nullptr;
    int mPos = -1;

    void SeekToNextPos() {
        if (mPos >= IMPL_MAX_DEVNODES) {
            return;
        }

        ImplNextDeviceNode(mPos + 1, &mPos);
    }

public:
    void Init(IWbemServices *services, int pos) {
        mServices = services;
        mPos = pos;
    }

    STDMETHODIMP Reset(void) override {
        HRESULT res = mInner->Reset();
        if (res == S_OK) {
            mPos = -1;
        }
        return res;
    }

    STDMETHODIMP Clone(IEnumWbemClassObject **ppEnum) override;

    STDMETHODIMP Next(long lTimeout, ULONG uCount, IWbemClassObject **apObjects, ULONG *puReturned) override {
        ULONG start = 0;
        if (mPos < 0) {
            HRESULT res = mInner->Next(lTimeout, uCount, apObjects, puReturned);
            if (res != S_FALSE) {
                return res;
            }

            ULONG count = *puReturned;
            uCount -= count;
            apObjects += count;
        } else {
            *puReturned = 0;
        }

        for (ULONG i = 0; i < uCount; i++) {
            SeekToNextPos();
            if (mPos >= IMPL_MAX_DEVNODES) {
                for (; i < uCount; i++) {
                    *(apObjects++) = nullptr;
                }
                return S_FALSE;
            }

            (*puReturned)++;
            *(apObjects++) = CreateDeviceClassObject(mServices, mPos);
        }
        return S_OK;
    }

    STDMETHODIMP NextAsync(ULONG uCount, IWbemObjectSink *pSink) override {
        // TODO
        return mInner->NextAsync(uCount, pSink);
    }

    STDMETHODIMP Skip(long lTimeout, ULONG nCount) override {
        if (mPos >= 0) {
            for (ULONG i = 0; i < nCount && mPos < IMPL_MAX_DEVNODES; i++) {
                SeekToNextPos();
            }
            return mPos < IMPL_MAX_DEVNODES ? S_OK : S_FALSE;
        } else {
            HRESULT res = mInner->Skip(lTimeout, nCount);
            if (res == S_FALSE) {
                // We don't know how many we skipped, so we guess. (TODO, but hopefully nobody can rely on exact skips)
                if (nCount < INT_MIN) {
                    SeekToNextPos();
                } else {
                    mPos = IMPL_MAX_DEVNODES;
                }
            }
            return res;
        }
    }
};

class MyWbemEnumClassObject : public ComWrapperBase<MyWbemEnumClassObject, MyWbemEnumClassObjectIntf,
                                                    ComClientSecurityIntf<MyWbemEnumClassObject>, ComUnknownIntf<MyWbemEnumClassObject>> {
public:
    MyWbemEnumClassObject(IWbemServices *services, int pos = -1) { get<MyWbemEnumClassObjectIntf>(mIntfs).Init(services, pos); }
};

STDMETHODIMP MyWbemEnumClassObjectIntf::Clone(IEnumWbemClassObject **ppEnum) {
    HRESULT res = mInner->Clone(ppEnum);
    if (res == S_OK && ppEnum) {
        *ppEnum = (new MyWbemEnumClassObject(mServices, mPos))->WrapInterface(*ppEnum);
    }
    return res;
}

class MyWbemNamespace;

class MyWbemServicesIntf : public ComIntfMultiWrapper<MyWbemServicesIntf, IWbemServices, MyWbemNamespace> {
    MyWbemNamespaceType mType = MyWbemNamespaceType::None;

    bool ClassHasPnpInstances(const BSTR strFilter) {
        return tstrieq(strFilter, MY_WBEM_PNP_CLASS) || tstrieq(strFilter, MY_WBEM_PNP_CLASS_BASE) ||
               tstrieq(strFilter, MY_WBEM_PNP_CLASS_BASE_2) || tstrieq(strFilter, MY_WBEM_PNP_CLASS_BASE_TOP);
    }

public:
    void Init(MyWbemNamespaceType type) { mType = type; }

    STDMETHODIMP OpenNamespace(const BSTR strNamespace, long lFlags, IWbemContext *pCtx, IWbemServices **ppWorkingNamespace, IWbemCallResult **ppResult) override {
        // TODO!
        return mInner->OpenNamespace(strNamespace, lFlags, pCtx, ppWorkingNamespace, ppResult);
    }

    STDMETHODIMP CreateInstanceEnum(const BSTR strFilter, long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum) override {
        HRESULT res = mInner->CreateInstanceEnum(strFilter, lFlags, pCtx, ppEnum);
        if (res == S_OK && ppEnum && mType == MyWbemNamespaceType::Cimv2 && ClassHasPnpInstances(strFilter)) {
            *ppEnum = (new MyWbemEnumClassObject(mInner))->WrapInterface(*ppEnum);
        }
        return res;
    }
    STDMETHODIMP CreateInstanceEnumAsync(const BSTR strFilter, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        // TODO!
        return mInner->CreateInstanceEnumAsync(strFilter, lFlags, pCtx, pResponseHandler);
    }

    STDMETHODIMP GetObject(const BSTR strObjectPath, long lFlags, IWbemContext *pCtx, IWbemClassObject **ppObject, IWbemCallResult **ppCallResult) override {
        // TODO!
        return mInner->GetObject(strObjectPath, lFlags, pCtx, ppObject, ppCallResult);
    }
    STDMETHODIMP GetObjectAsync(const BSTR strObjectPath, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        // TODO!
        return mInner->GetObjectAsync(strObjectPath, lFlags, pCtx, pResponseHandler);
    }

    STDMETHODIMP ExecQuery(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum) override {
        // TODO!
        return mInner->ExecQuery(strQueryLanguage, strQuery, lFlags, pCtx, ppEnum);
    }
    STDMETHODIMP ExecQueryAsync(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        // TODO!
        return mInner->ExecQueryAsync(strQueryLanguage, strQuery, lFlags, pCtx, pResponseHandler);
    }
    STDMETHODIMP ExecNotificationQuery(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum) override {
        // TODO!
        return mInner->ExecNotificationQuery(strQueryLanguage, strQuery, lFlags, pCtx, ppEnum);
    }
    STDMETHODIMP ExecNotificationQueryAsync(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        // TODO!
        return mInner->ExecNotificationQueryAsync(strQueryLanguage, strQuery, lFlags, pCtx, pResponseHandler);
    }

    STDMETHODIMP ExecMethod(const BSTR strObjectPath, const BSTR strMethodName, long lFlags, IWbemContext *pCtx, IWbemClassObject *pInParams,
                            IWbemClassObject **ppOutParams, IWbemCallResult **ppCallResult) override {
        // TODO!
        return mInner->ExecMethod(strObjectPath, strMethodName, lFlags, pCtx, pInParams, ppOutParams, ppCallResult);
    }
    STDMETHODIMP ExecMethodAsync(const BSTR strObjectPath, const BSTR strMethodName, long lFlags, IWbemContext *pCtx, IWbemClassObject *pInParams,
                                 IWbemObjectSink *pResponseHandler) override {
        // TODO!
        return mInner->ExecMethodAsync(strObjectPath, strMethodName, lFlags, pCtx, pInParams, pResponseHandler);
    }

    STDMETHODIMP CancelAsyncCall(IWbemObjectSink *pSink) override {
        return mInner->CancelAsyncCall(pSink);
    }
    STDMETHODIMP QueryObjectSink(long lFlags, IWbemObjectSink **ppResponseHandler) override {
        return mInner->QueryObjectSink(lFlags, ppResponseHandler);
    }

    STDMETHODIMP PutClass(IWbemClassObject *pObject, long lFlags, IWbemContext *pCtx, IWbemCallResult **ppCallResult) override {
        return mInner->PutClass(pObject, lFlags, pCtx, ppCallResult);
    }
    STDMETHODIMP PutClassAsync(IWbemClassObject *pObject, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        return mInner->PutClassAsync(pObject, lFlags, pCtx, pResponseHandler);
    }
    STDMETHODIMP DeleteClass(const BSTR strClass, long lFlags, IWbemContext *pCtx, IWbemCallResult **ppCallResult) override {
        return mInner->DeleteClass(strClass, lFlags, pCtx, ppCallResult);
    }
    STDMETHODIMP DeleteClassAsync(const BSTR strClass, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        return mInner->DeleteClassAsync(strClass, lFlags, pCtx, pResponseHandler);
    }

    STDMETHODIMP CreateClassEnum(const BSTR strSuperclass, long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum) override {
        return mInner->CreateClassEnum(strSuperclass, lFlags, pCtx, ppEnum);
    }
    STDMETHODIMP CreateClassEnumAsync(const BSTR strSuperclass, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        return mInner->CreateClassEnumAsync(strSuperclass, lFlags, pCtx, pResponseHandler);
    }

    STDMETHODIMP PutInstance(IWbemClassObject *pInst, long lFlags, IWbemContext *pCtx, IWbemCallResult **ppCallResult) override {
        return mInner->PutInstance(pInst, lFlags, pCtx, ppCallResult);
    }
    STDMETHODIMP PutInstanceAsync(IWbemClassObject *pInst, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        return mInner->PutInstanceAsync(pInst, lFlags, pCtx, pResponseHandler);
    }
    STDMETHODIMP DeleteInstance(const BSTR strObjectPath, long lFlags, IWbemContext *pCtx, IWbemCallResult **ppCallResult) override {
        return mInner->DeleteInstance(strObjectPath, lFlags, pCtx, ppCallResult);
    }
    STDMETHODIMP DeleteInstanceAsync(const BSTR strObjectPath, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        return mInner->DeleteInstanceAsync(strObjectPath, lFlags, pCtx, pResponseHandler);
    }
};

class MyWbemNamespace : public ComWrapperBase<MyWbemNamespace, MyWbemServicesIntf, ComClientSecurityIntf<MyWbemNamespace>, ComUnknownIntf<MyWbemNamespace>> {
public:
    MyWbemNamespace(MyWbemNamespaceType type) { get<MyWbemServicesIntf>(mIntfs).Init(type); }
};

class MyWbemLocator;

class MyWbemLocatorIntf : public ComIntfWrapper<IWbemLocator, MyWbemLocator> {
public:
    STDMETHODIMP ConnectServer(const BSTR strNetworkResource, const BSTR strUser, const BSTR strPassword, const BSTR strLocale,
                               long lSecurityFlags, const BSTR strAuthority, IWbemContext *pCtx, IWbemServices **ppNamespace) override {
        if (gUniqLogWbemUsed) {
            LOG << "(Wbem used)" << END;
        }

        HRESULT res = mInner->ConnectServer(strNetworkResource, strUser, strPassword, strLocale, lSecurityFlags, strAuthority, pCtx, ppNamespace);
        if (res == S_OK && ppNamespace) {
            auto type = ParseNamespace(MyWbemNamespaceType::None, strNetworkResource, true);
            *ppNamespace = (new MyWbemNamespace(type))->WrapInterface(*ppNamespace);
        }
        return res;
    }
};

class MyWbemLocator : public ComWrapperBase<MyWbemLocator, MyWbemLocatorIntf, ComUnknownIntf<MyWbemLocator>> {};

WeakAtomic<ComClassFactory<MyWbemLocator> *> mWbemLocatorFactory;

void WrapClassIfNeeded(REFCLSID rclsid, REFIID riid, LPVOID *ppv) {
    if (rclsid == __uuidof(WbemLocator)) {
        *ppv = GetComSingleton(mWbemLocatorFactory)->WrapInterface(riid, *ppv);
    }
}

void WrapInstanceIfNeeded(REFCLSID rclsid, REFIID riid, LPVOID *ppv) {
    if (rclsid == __uuidof(WbemLocator)) {
        *ppv = WrapNewComInstance<MyWbemLocator>(riid, *ppv);
    }
}

void WrapInstancesIfNeeded(REFCLSID rclsid, DWORD dwCount, MULTI_QI *pResults) {
    if (rclsid == __uuidof(WbemLocator)) {
        WrapNewComInstanceMultiQi<MyWbemLocator>(dwCount, pResults);
    }
}
