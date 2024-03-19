#pragma once
#include "ComApi.h"
#include <WbemIdl.h>
#include <WmiUtils.h>

// (best effort basis) (while hopefully not breaking much)

UniqueLog gUniqLogWbemUsed;

const wchar_t *GetComputerNameForWbem() {
    static const wchar_t *sComputerNamePtr = [] {
        static wchar_t sComputerName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        ASSERT(GetComputerNameW(sComputerName, &size), "Cannot get computer name");
        return sComputerName;
    }();
    return sComputerNamePtr;
}

enum class MyWbemNamespaceType {
    None,
    Root,
    Cimv2,
    Other,
};

#define WBEM_NS_ROOT L"ROOT"
#define WBEM_NS_CIMV2 L"CIMV2"
#define WBEM_NS_ROOT_CIMV2 WBEM_NS_ROOT L"\\" WBEM_NS_CIMV2

bool IsWbemServerOurs(const wchar_t *server, size_t len = SIZE_MAX) {
    return true; // TODO: can be ., localhost, GetComputerNameForWbem,
                 // localhost ip4 addr, localhost ip6 addr, ???
                 // best to just ignore?
}

MyWbemNamespaceType ParseWbemNamespace(MyWbemNamespaceType type, const wchar_t *str, bool allowAbs) {
    if (!str || !*str) {
        return type;
    }

    const wchar_t *delims = L"\\/";

    if (allowAbs && wcschr(delims, str[0]) && wcschr(delims, str[1])) {
        const wchar_t *system = str + 2;
        const wchar_t *end = wcspbrk(system, delims);
        if (!end) {
            return MyWbemNamespaceType::Other;
        }

        size_t sysLen = end - system;
        if (!IsWbemServerOurs(system, sysLen)) {
            return MyWbemNamespaceType::Other;
        }

        str = end + 1;
    }

    while (str && *str) {
        const wchar_t *end = wcspbrk(str, delims);
        size_t len = end ? end - str : wcslen(str);

        if (type == MyWbemNamespaceType::None && tstrnieq(str, WBEM_NS_ROOT, len)) {
            type = MyWbemNamespaceType::Root;
        } else if (type == MyWbemNamespaceType::Root && tstrnieq(str, WBEM_NS_CIMV2, len)) {
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

bool IsWbemClassOursOrBase(const wchar_t *clsName) {
    return clsName && (tstrieq(clsName, MY_WBEM_PNP_CLASS) || tstrieq(clsName, MY_WBEM_PNP_CLASS_BASE) ||
                       tstrieq(clsName, MY_WBEM_PNP_CLASS_BASE_2) || tstrieq(clsName, MY_WBEM_PNP_CLASS_BASE_TOP));
}

int ParseWbemObjectName(const wchar_t *name, MyWbemNamespaceType nsType, bool *outIsAbs) {
    *outIsAbs = false;
    ComRef<IWbemPath> path;
    if (SUCCEEDED(CoCreateInstance_Real(__uuidof(WbemDefPath), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWbemPath), (void **)&path)) &&
        SUCCEEDED(path->SetText(WBEMPATH_CREATE_ACCEPT_ALL, name))) {
        wchar_t clsName[32] = {};
        ULONG clsNameSize = (ULONG)size(clsName);
        wchar_t srvName[32] = {};
        ULONG srvNameSize = (ULONG)size(srvName);
        ULONG nsCount;
        wchar_t nsName1[8] = {};
        wchar_t nsName2[8] = {};
        ULONG nsName1Size = (ULONG)size(nsName1);
        ULONG nsName2Size = (ULONG)size(nsName2);
        ComRef<IWbemPathKeyList> keyList;
        ULONG keyCount;
        wchar_t keyName[16] = {};
        ULONG keyNameSize = (ULONG)size(keyName);
        Variant keyValue;
        ULONG keyType;

        if (SUCCEEDED(path->GetClassName(&clsNameSize, clsName)) &&
            tstrieq(clsName, MY_WBEM_PNP_CLASS) &&
            SUCCEEDED(path->GetServer(&srvNameSize, srvName)) &&
            (!*srvName || IsWbemServerOurs(srvName)) &&
            SUCCEEDED(path->GetNamespaceCount(&nsCount)) &&
            ((nsCount == 0 && nsType == MyWbemNamespaceType::Cimv2) ||
             (nsCount == 2 &&
              SUCCEEDED(path->GetNamespaceAt(0, &nsName1Size, nsName1)) &&
              SUCCEEDED(path->GetNamespaceAt(1, &nsName2Size, nsName2)) &&
              tstrieq(nsName1, WBEM_NS_ROOT) && tstrieq(nsName2, WBEM_NS_CIMV2))) &&
            SUCCEEDED(path->GetKeyList(&keyList)) &&
            SUCCEEDED(keyList->GetCount(&keyCount)) && keyCount == 1 &&
            SUCCEEDED(keyList->GetKey2(0, 0, &keyNameSize, keyName, &keyValue, &keyType)) &&
            keyType == CIM_STRING && keyValue.IsBstr() &&
            (!*keyName || tstrieq(keyName, L"DeviceID"))) {
            *outIsAbs = nsCount == 2;

            for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
                DeviceNode *node = ImplGetDeviceNode(i);
                if (node && tstrieq(node->DeviceInstNameW, keyValue.GetBstr())) {
                    return i;
                }
            }
        }
    }
    return -1;
}

class WbemQueryFilter {
    Bstr LangText;
    Bstr QueryText;
    ComRef<IWbemQuery> QueryObj;
    SWbemRpnEncodedQuery *Query = nullptr;
    bool QueryOk = false;
    once_flag QueryOnce;

    bool InitQuery() {
        call_once(QueryOnce, [&] {
            if (SUCCEEDED(CoCreateInstance_Real(__uuidof(WbemQuery), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWbemQuery), (void **)&QueryObj)) &&
                SUCCEEDED(QueryObj->Parse(LangText, QueryText, 0)) &&
                SUCCEEDED(QueryObj->GetAnalysis(WMIQ_ANALYSIS_RPN_SEQUENCE, 0, (void **)&Query))) {
                // TODO: m_pszOptionalFromPath? etc...
                QueryOk = !Query->m_pszOptionalFromPath && Query->m_uFromListSize == 1 &&
                          IsWbemClassOursOrBase(Query->m_ppszFromList[0]);
            }
        });
        return QueryOk;
    }

    bool ProcessWhere(IWbemClassObject *obj) {
        vector<bool> rpn;
        for (ULONG i = 0; i < Query->m_uWhereClauseSize; i++) {
            auto where = Query->m_ppRpnWhereClause[i];
            switch (where->m_uTokenType) {
            case WMIQ_RPN_TOKEN_EXPRESSION:
                switch (where->m_uOperator) {
                case WMIQ_RPN_OP_EQ:
                    break;
                }
                return false; // TODO...
                break;

            case WMIQ_RPN_TOKEN_AND:
                if (rpn.size() < 2 || !ExtractBack(rpn) || !ExtractBack(rpn)) {
                    return false;
                }
                break;

            case WMIQ_RPN_TOKEN_OR:
                if (rpn.size() < 2 || (!ExtractBack(rpn) && !ExtractBack(rpn))) {
                    return false;
                }
                break;

            case WMIQ_RPN_TOKEN_NOT:
                if (rpn.size() < 1 || ExtractBack(rpn)) {
                    return false;
                }
                break;

            default:
                return false;
            }
        }

        return rpn.size() == 1 && ExtractBack(rpn);
    }

public:
    WbemQueryFilter(const wchar_t *lang, const wchar_t *query) : LangText(lang), QueryText(query) {}

    ~WbemQueryFilter() {
        if (Query) {
            QueryObj->FreeMemory(Query);
        }
    }

    bool Matches(IWbemClassObject *obj) {
        if (InitQuery()) {
            if (Query->m_uWhereClauseSize && !ProcessWhere(obj)) {
                return false;
            }

            // TODO: remove by select? how?
            return true;
        }
        return false;
    }
};

class MyWbemClassObject;

class MyWbemClassObjectIntf : public ComIntfWrapperWithSupers<IWbemObjectAccess, MyWbemClassObject, IWbemClassObject> {
    bool GetPrivateProp(LPCWSTR name, VARIANT *val) {
        if (!val || val->vt != VT_NULL) { // also tells us we can copy into *val directly
            return false;
        }

        if (tstreq(name, L"__SERVER")) {
            *val = Variant(GetComputerNameForWbem()).Take();
        } else if (tstreq(name, L"__NAMESPACE")) {
            *val = Variant(WBEM_NS_ROOT_CIMV2).Take();
        } else if (tstreq(name, L"__PATH")) {
            Variant relPath;
            if (!SUCCEEDED(mInner->Get(L"__RELPATH", 0, &relPath, nullptr, nullptr))) {
                return false;
            }
            *val = Variant((wstring(L"\\\\") + GetComputerNameForWbem() + L"\\" WBEM_NS_ROOT_CIMV2 ":" + relPath.GetBstr()).c_str()).Take();
        } else {
            return false;
        }

        return true;
    }

public:
    STDMETHODIMP Get(LPCWSTR wszName, long lFlags, VARIANT *pVal, CIMTYPE *pType, long *plFlavor) override {
        HRESULT res = mInner->Get(wszName, lFlags, pVal, pType, plFlavor);
        if (res == S_OK) {
            GetPrivateProp(wszName, pVal);
        }
        return res;
    }

    STDMETHODIMP Next(long lFlags, BSTR *strName, VARIANT *pVal, CIMTYPE *pType, long *plFlavor) override {
        Bstr localName;
        strName = strName ? strName : &localName;
        HRESULT res = mInner->Next(lFlags, strName, pVal, pType, plFlavor);
        if (res == S_OK) {
            GetPrivateProp(*strName, pVal);
        }
        return res;
    }

    STDMETHODIMP GetPropertyHandle(LPCWSTR wszPropertyName, CIMTYPE *pType, long *plHandle) override {
        return mInner->GetPropertyHandle(wszPropertyName, pType, plHandle);
    }
    STDMETHODIMP WritePropertyValue(long lHandle, long lNumBytes, const byte *aData) override {
        return mInner->WritePropertyValue(lHandle, lNumBytes, aData);
    }
    STDMETHODIMP ReadPropertyValue(long lHandle, long lBufferSize, long *plNumBytes, byte *aData) override {
        return mInner->ReadPropertyValue(lHandle, lBufferSize, plNumBytes, aData);
    }
    STDMETHODIMP ReadDWORD(long lHandle, DWORD *pdw) override {
        return mInner->ReadDWORD(lHandle, pdw);
    }
    STDMETHODIMP WriteDWORD(long lHandle, DWORD dw) override {
        return mInner->WriteDWORD(lHandle, dw);
    }
    STDMETHODIMP ReadQWORD(long lHandle, unsigned __int64 *pqw) override {
        return mInner->ReadQWORD(lHandle, pqw);
    }
    STDMETHODIMP WriteQWORD(long lHandle, unsigned __int64 pw) override {
        return mInner->WriteQWORD(lHandle, pw);
    }
    STDMETHODIMP GetPropertyInfoByHandle(long lHandle, BSTR *pstrName, CIMTYPE *pType) override {
        return mInner->GetPropertyInfoByHandle(lHandle, pstrName, pType);
    }
    STDMETHODIMP Lock(long lFlags) override {
        return mInner->Lock(lFlags);
    }
    STDMETHODIMP Unlock(long lFlags) override {
        return mInner->Unlock(lFlags);
    }

    STDMETHODIMP GetQualifierSet(IWbemQualifierSet **ppQualSet) override {
        return mInner->GetQualifierSet(ppQualSet);
    }
    STDMETHODIMP Put(LPCWSTR wszName, long lFlags, VARIANT *pVal, CIMTYPE Type) override {
        return mInner->Put(wszName, lFlags, pVal, Type);
    }
    STDMETHODIMP Delete(LPCWSTR wszName) override {
        return mInner->Delete(wszName);
    }
    STDMETHODIMP GetNames(LPCWSTR wszQualifierName, long lFlags, VARIANT *pQualifierVal, SAFEARRAY **pNames) override {
        return mInner->GetNames(wszQualifierName, lFlags, pQualifierVal, pNames);
    }
    STDMETHODIMP BeginEnumeration(long lEnumFlags) override {
        return mInner->BeginEnumeration(lEnumFlags);
    }
    STDMETHODIMP EndEnumeration(void) override {
        return mInner->EndEnumeration();
    }
    STDMETHODIMP GetPropertyQualifierSet(LPCWSTR wszProperty, IWbemQualifierSet **ppQualSet) override {
        return mInner->GetPropertyQualifierSet(wszProperty, ppQualSet);
    }
    STDMETHODIMP Clone(IWbemClassObject **ppCopy) override {
        return mInner->Clone(ppCopy);
    }
    STDMETHODIMP GetObjectText(long lFlags, BSTR *pstrObjectText) override {
        return mInner->GetObjectText(lFlags, pstrObjectText);
    }
    STDMETHODIMP SpawnDerivedClass(long lFlags, IWbemClassObject **ppNewClass) override {
        return mInner->SpawnDerivedClass(lFlags, ppNewClass);
    }
    STDMETHODIMP SpawnInstance(long lFlags, IWbemClassObject **ppNewInstance) override {
        return mInner->SpawnInstance(lFlags, ppNewInstance);
    }
    STDMETHODIMP CompareTo(long lFlags, IWbemClassObject *pCompareTo) override {
        return mInner->CompareTo(lFlags, pCompareTo);
    }
    STDMETHODIMP GetPropertyOrigin(LPCWSTR wszName, BSTR *pstrClassName) override {
        return mInner->GetPropertyOrigin(wszName, pstrClassName);
    }
    STDMETHODIMP InheritsFrom(LPCWSTR strAncestor) override {
        return mInner->InheritsFrom(strAncestor);
    }
    STDMETHODIMP GetMethod(LPCWSTR wszName, long lFlags, IWbemClassObject **ppInSignature, IWbemClassObject **ppOutSignature) override {
        return mInner->GetMethod(wszName, lFlags, ppInSignature, ppOutSignature);
    }
    STDMETHODIMP PutMethod(LPCWSTR wszName, long lFlags, IWbemClassObject *pInSignature, IWbemClassObject *pOutSignature) override {
        return mInner->PutMethod(wszName, lFlags, pInSignature, pOutSignature);
    }
    STDMETHODIMP DeleteMethod(LPCWSTR wszName) override {
        return mInner->DeleteMethod(wszName);
    }
    STDMETHODIMP BeginMethodEnumeration(long lEnumFlags) override {
        return mInner->BeginMethodEnumeration(lEnumFlags);
    }
    STDMETHODIMP NextMethod(long lFlags, BSTR *pstrName, IWbemClassObject **ppInSignature, IWbemClassObject **ppOutSignature) override {
        return mInner->NextMethod(lFlags, pstrName, ppInSignature, ppOutSignature);
    }
    STDMETHODIMP EndMethodEnumeration(void) override {
        return mInner->EndMethodEnumeration();
    }
    STDMETHODIMP GetMethodQualifierSet(LPCWSTR wszMethod, IWbemQualifierSet **ppQualSet) override {
        return mInner->GetMethodQualifierSet(wszMethod, ppQualSet);
    }
    STDMETHODIMP GetMethodOrigin(LPCWSTR wszMethodName, BSTR *pstrClassName) override {
        return mInner->GetMethodOrigin(wszMethodName, pstrClassName);
    }
};

class MyWbemClassObject : public ComWrapperBase<MyWbemClassObject, MyWbemClassObjectIntf, ComUnknownIntf<MyWbemClassObject>> {};

ComRef<IWbemClassObject> CreateWbemClassObject(IWbemServices *services, int devNodeIdx, bool fromOtherService = false) {
    DeviceNode *node = ImplGetDeviceNode(devNodeIdx);
    if (!node) {
        return nullptr;
    }

    ComRef<IWbemClassObject> clsObj;
    const wchar_t *clsName = fromOtherService ? L"\\\\.\\" WBEM_NS_ROOT_CIMV2 ":" MY_WBEM_PNP_CLASS : MY_WBEM_PNP_CLASS;
    HRESULT res = services->GetObject(Bstr(clsName), 0, nullptr, &clsObj, nullptr);

    ComRef<IWbemClassObject> obj;
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
    if (res == S_OK) {
        res = obj->Put(L"SystemName", 0, Variant(GetComputerNameForWbem()), 0);
    }

    if (res == S_OK) {
        // since we can't set __-prefixed props:
        return (new MyWbemClassObject())->WrapInterface(obj.Get());
    } else {
        LOG_ERR << "Wbem: failed creating device instance" << END;
        return nullptr;
    }
}

class MyWbemObjectCallResult : public ComBase<IWbemCallResult> {
    ComRef<IWbemClassObject> mObject;

public:
    MyWbemObjectCallResult(IWbemClassObject *object) : mObject(object) {}

    STDMETHODIMP GetResultObject(long lTimeout, IWbemClassObject **ppResultObject) override {
        *ppResultObject = mObject.TakeCopy();
        return S_OK;
    }
    STDMETHODIMP GetResultString(long lTimeout, BSTR *pstrResultString) override {
        *pstrResultString = nullptr;
        return WBEM_E_INVALID_OPERATION;
    }
    STDMETHODIMP GetResultServices(long lTimeout, IWbemServices **ppServices) override {
        *ppServices = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetCallStatus(long lTimeout, long *plStatus) override {
        *plStatus = S_OK;
        return S_OK;
    }
};

template <class TActual, class TRoot>
class MyWbemCallResultIntfBase : public ComIntfMultiWrapper<TActual, IWbemCallResult, TRoot> {
    STDMETHODIMP GetResultObject(long lTimeout, IWbemClassObject **ppResultObject) override {
        return this->mInner->GetResultObject(lTimeout, ppResultObject);
    }
    STDMETHODIMP GetResultString(long lTimeout, BSTR *pstrResultString) override {
        return this->mInner->GetResultString(lTimeout, pstrResultString);
    }
    STDMETHODIMP GetResultServices(long lTimeout, IWbemServices **ppServices) override {
        return this->mInner->GetResultServices(lTimeout, ppServices);
    }
    STDMETHODIMP GetCallStatus(long lTimeout, long *plStatus) override {
        return this->mInner->GetCallStatus(lTimeout, plStatus);
    }
};

class MyWbemObjectSink;

struct MyWbemObjectSinkState {
    ComRef<IWbemServices> Services;
    SharedPtr<WbemQueryFilter> Filter;
};

class MyWbemObjectSinkIntf : public ComIntfMultiWrapper<MyWbemObjectSinkIntf, IWbemObjectSink, MyWbemObjectSink> {
    MyWbemObjectSinkState *State();

    STDMETHODIMP Indicate(long lObjectCount, IWbemClassObject **apObjArray) override {
        return mInner->Indicate(lObjectCount, apObjArray);
    }

    STDMETHODIMP SetStatus(long lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject *pObjParam) override {
        if (lFlags == WBEM_STATUS_COMPLETE && hResult == S_OK) {
            auto m = State();
            IWbemClassObject *objects[IMPL_MAX_DEVNODES];
            long objCount = 0;

            for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
                DeviceNode *node = ImplGetDeviceNode(i);
                if (node) {
                    auto obj = CreateWbemClassObject(m->Services, i);
                    if (!m->Filter || m->Filter->Matches(obj)) {
                        objects[objCount++] = obj.Take();
                    }
                }
            }

            if (objCount) {
                mInner->Indicate(objCount, objects);
            }
        }

        return mInner->SetStatus(lFlags, hResult, strParam, pObjParam);
    }
};

class MyWbemObjectSink : public ComWrapperBase<MyWbemObjectSink, MyWbemObjectSinkIntf, ComUnknownIntf<MyWbemObjectSink>> {
public:
    MyWbemObjectSinkState State;
    MyWbemObjectSink(IWbemServices *services, const SharedPtr<WbemQueryFilter> &filter = nullptr) : State{services, filter} {}
};

MyWbemObjectSinkState *MyWbemObjectSinkIntf::State() { return &mRoot->State; }

struct MyWbemEnumClassObjectState {
    ComRef<IWbemServices> Services;
    SharedPtr<WbemQueryFilter> Filter;
    int Pos;
    recmutex SyncMutex;  // covers mInner calls
    recmutex StateMutex; // doesn't cover mInner calls

    ComRef<IWbemClassObject> Next() {
        while (true) {
            if (!ImplNextDeviceNode(&Pos)) {
                return nullptr;
            }

            auto obj = CreateWbemClassObject(Services, Pos);
            if (!Filter || Filter->Matches(obj)) {
                return obj;
            }
        }
    }
};

class MyEnumWbemObjectSink;

struct MyEnumWbemObjectSinkState {
    ComRef<IEnumWbemClassObject> Enum;
    MyWbemEnumClassObjectState *EnumState;
    int Count;
    int Indicated;
};

class MyEnumWbemObjectSinkIntf : public ComIntfMultiWrapper<MyEnumWbemObjectSinkIntf, IWbemObjectSink, MyEnumWbemObjectSink> {
    MyEnumWbemObjectSinkState *State();

    STDMETHODIMP Indicate(long lObjectCount, IWbemClassObject **apObjArray) override {
        auto m = State();
        m->Indicated += lObjectCount;
        return mInner->Indicate(lObjectCount, apObjArray);
    }

    STDMETHODIMP SetStatus(long lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject *pObjParam) override {
        auto m = State();
        int count = m->Count - m->Indicated;
        if (hResult == S_OK && !m->Indicated) { // weird dummy call *sometimes* received before returning S_FALSE from NextAsync
            return hResult;
        }

        if (lFlags == WBEM_STATUS_COMPLETE && hResult == S_FALSE && count > 0) {
            unique_lock lock(m->EnumState->StateMutex);
            IWbemClassObject *objects[IMPL_MAX_DEVNODES];
            long objCount = 0;

            for (int i = 0; i < count; i++) {
                auto obj = m->EnumState->Next();
                if (!obj) {
                    break;
                }

                objects[objCount++] = obj.Take();
            }

            if (objCount) {
                mInner->Indicate(objCount, objects);
            }
            if (objCount == count) {
                hResult = S_OK;
            }
        }

        return mInner->SetStatus(lFlags, hResult, strParam, pObjParam);
    }
};

class MyEnumWbemObjectSink : public ComWrapperBase<MyEnumWbemObjectSink, MyEnumWbemObjectSinkIntf, ComUnknownIntf<MyEnumWbemObjectSink>> {
public:
    MyEnumWbemObjectSinkState State;
    MyEnumWbemObjectSink(IEnumWbemClassObject *enm, MyWbemEnumClassObjectState *state, int count) : State{enm, state, count} {}
};

MyEnumWbemObjectSinkState *MyEnumWbemObjectSinkIntf::State() { return &mRoot->State; }

class MyWbemEnumClassObject;

class MyWbemEnumClassObjectIntf : public ComIntfMultiWrapper<MyWbemEnumClassObjectIntf, IEnumWbemClassObject, MyWbemEnumClassObject> {
    MyWbemEnumClassObjectState *State();

public:
    STDMETHODIMP Reset(void) override {
        auto m = State();
        unique_lock sync(m->SyncMutex);

        HRESULT res = mInner->Reset();
        if (res == S_OK) {
            unique_lock lock(m->StateMutex);
            m->Pos = -1;
        }
        return res;
    }

    STDMETHODIMP Clone(IEnumWbemClassObject **ppEnum) override;

    STDMETHODIMP Next(long lTimeout, ULONG uCount, IWbemClassObject **apObjects, ULONG *puReturned) override {
        auto m = State();
        unique_lock sync(m->SyncMutex);

        HRESULT res = mInner->Next(lTimeout, uCount, apObjects, puReturned);
        if (res != S_FALSE) {
            return res;
        }

        ULONG count = *puReturned;
        uCount -= count;
        apObjects += count;

        unique_lock lock(m->StateMutex);
        if (m->Pos >= IMPL_MAX_DEVNODES) {
            return S_FALSE;
        }

        for (ULONG i = 0; i < uCount; i++) {
            auto obj = m->Next();
            if (!obj) {
                return S_FALSE;
            }

            (*puReturned)++;
            *(apObjects++) = obj.Take();
        }
        return S_OK;
    }

    STDMETHODIMP NextAsync(ULONG uCount, IWbemObjectSink *pSink) override {
        auto m = State();
        unique_lock sync(m->SyncMutex);

        MyEnumWbemObjectSink *wrapper = nullptr;
        if (pSink) {
            pSink = (wrapper = new MyEnumWbemObjectSink(mInner, m, uCount))->WrapInterface(pSink);
        }
        HRESULT res = mInner->NextAsync(uCount, pSink);
        if (res == S_FALSE && pSink) {
            unique_lock lock(m->StateMutex);
            if (m->Pos < IMPL_MAX_DEVNODES) {
                pSink->SetStatus(WBEM_STATUS_COMPLETE, res, nullptr, nullptr);
                if (m->Pos < IMPL_MAX_DEVNODES) {
                    res = S_OK;
                }
            }
        }
        if (wrapper) {
            wrapper->UndoWrapInterface(pSink);
        }
        return res;
    }

    STDMETHODIMP Skip(long lTimeout, ULONG nCount) override {
        auto m = State();
        unique_lock sync(m->SyncMutex);

        {
            unique_lock lock(m->StateMutex);
            if (m->Pos >= 0) {
                for (ULONG i = 0; i < nCount && m->Pos < IMPL_MAX_DEVNODES; i++) {
                    ImplNextDeviceNode(&m->Pos);
                }
                return m->Pos < IMPL_MAX_DEVNODES ? S_OK : S_FALSE;
            }
        }

        HRESULT res = mInner->Skip(lTimeout, nCount);
        if (res == S_FALSE) {
            // We don't know how many we skipped, so we guess. (TODO, but hopefully nobody can rely on exact skips)
            unique_lock lock(m->StateMutex);
            if (nCount < INT_MIN) {
                ImplNextDeviceNode(&m->Pos);
            } else {
                m->Pos = IMPL_MAX_DEVNODES;
            }
        }
        return res;
    }
};

class MyWbemEnumClassObject : public ComWrapperBase<MyWbemEnumClassObject, MyWbemEnumClassObjectIntf, COM_RPC_INTERFACES(MyWbemEnumClassObject)> {
public:
    MyWbemEnumClassObjectState State;
    MyWbemEnumClassObject(IWbemServices *services, const SharedPtr<WbemQueryFilter> &filter = nullptr, int pos = -1) : State{services, filter, pos} {}
};

MyWbemEnumClassObjectState *MyWbemEnumClassObjectIntf::State() { return &mRoot->State; }

STDMETHODIMP MyWbemEnumClassObjectIntf::Clone(IEnumWbemClassObject **ppEnum) {
    auto m = State();
    unique_lock sync(m->SyncMutex);

    HRESULT res = mInner->Clone(ppEnum);
    if (res == S_OK && ppEnum) {
        unique_lock lock(m->StateMutex);
        *ppEnum = (new MyWbemEnumClassObject(m->Services, m->Filter, m->Pos))->WrapInterface(*ppEnum);
    }
    return res;
}

class MyWbemGetObjectSink;

struct MyWbemGetObjectSinkState {
    ComRef<IWbemServices> Services;
    Bstr Name;
    MyWbemNamespaceType NsType;
};

class MyWbemGetObjectSinkIntf : public ComIntfMultiWrapper<MyWbemGetObjectSinkIntf, IWbemObjectSink, MyWbemGetObjectSink> {
    MyWbemGetObjectSinkState *State();

    STDMETHODIMP Indicate(long lObjectCount, IWbemClassObject **apObjArray) override {
        return mInner->Indicate(lObjectCount, apObjArray);
    }

    STDMETHODIMP SetStatus(long lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject *pObjParam) override {
        if (lFlags == WBEM_STATUS_COMPLETE && hResult == WBEM_E_NOT_FOUND) {
            auto m = State();
            bool absName;
            int devIdx = ParseWbemObjectName(m->Name, m->NsType, &absName);
            if (devIdx >= 0) {
                ComRef<IWbemClassObject> object = CreateWbemClassObject(m->Services, devIdx, absName);
                mInner->Indicate(1, &object);

                strParam = nullptr;
                pObjParam = nullptr;
                hResult = S_OK;
            }
        }

        return mInner->SetStatus(lFlags, hResult, strParam, pObjParam);
    }
};

class MyWbemGetObjectSink : public ComWrapperBase<MyWbemGetObjectSink, MyWbemGetObjectSinkIntf, ComUnknownIntf<MyWbemGetObjectSink>> {
public:
    MyWbemGetObjectSinkState State;
    MyWbemGetObjectSink(IWbemServices *services, const BSTR name, MyWbemNamespaceType nsType) : State{services, name, nsType} {}
};

MyWbemGetObjectSinkState *MyWbemGetObjectSinkIntf::State() { return &mRoot->State; }

class MyWbemGetObjectCallResult;

struct MyWbemGetObjectCallResultState {
    ComRef<IWbemServices> Services;
    Bstr Name;
    MyWbemNamespaceType NsType;
    ComRef<IWbemClassObject> Object;
    once_flag ObjectOnce;
};

class MyWbemGetObjectCallResultIntf : public ComIntfMultiWrapper<MyWbemGetObjectCallResultIntf, IWbemCallResult, MyWbemGetObjectCallResult> {
    MyWbemGetObjectCallResultState *State();

    bool ResolveIfNeeded() {
        auto m = State();
        call_once(m->ObjectOnce, [&] {
            bool absName;
            int devIdx = ParseWbemObjectName(m->Name, m->NsType, &absName);
            if (devIdx >= 0) {
                m->Object = CreateWbemClassObject(m->Services, devIdx, absName);
            }
        });
        return m->Object != nullptr;
    }

    STDMETHODIMP GetResultObject(long lTimeout, IWbemClassObject **ppResultObject) override {
        HRESULT res = this->mInner->GetResultObject(lTimeout, ppResultObject);
        if (res == WBEM_E_NOT_FOUND && ResolveIfNeeded()) {
            if (ppResultObject) {
                *ppResultObject = State()->Object.TakeCopy();
            }
            res = S_OK;
        }
        return res;
    }
    STDMETHODIMP GetResultString(long lTimeout, BSTR *pstrResultString) override {
        HRESULT res = this->mInner->GetResultString(lTimeout, pstrResultString);
        if (res == WBEM_E_NOT_FOUND && ResolveIfNeeded()) {
            res = WBEM_E_INVALID_OPERATION;
        }
        return res;
    }
    STDMETHODIMP GetResultServices(long lTimeout, IWbemServices **ppServices) override {
        HRESULT res = this->mInner->GetResultServices(lTimeout, ppServices);
        if (res == WBEM_E_NOT_FOUND && ResolveIfNeeded()) {
            res = S_OK;
        }
        return res;
    }
    STDMETHODIMP GetCallStatus(long lTimeout, long *plStatus) override {
        HRESULT res = this->mInner->GetCallStatus(lTimeout, plStatus);
        if (res == S_OK && plStatus && *plStatus == WBEM_E_NOT_FOUND && ResolveIfNeeded()) {
            *plStatus = S_OK;
        }
        return res;
    }
};

class MyWbemGetObjectCallResult : public ComWrapperBase<MyWbemGetObjectCallResult, MyWbemGetObjectCallResultIntf, COM_RPC_INTERFACES(MyWbemGetObjectCallResult)> {
public:
    MyWbemGetObjectCallResultState State;
    MyWbemGetObjectCallResult(IWbemServices *services, const BSTR name, MyWbemNamespaceType nsType) : State{services, name, nsType} {}
};

MyWbemGetObjectCallResultState *MyWbemGetObjectCallResultIntf::State() { return &mRoot->State; }

class MyWbemNamespace;

struct MyWbemNamespaceState {
    MyWbemNamespaceType Type;
};

class MyWbemServicesIntf : public ComIntfMultiWrapper<MyWbemServicesIntf, IWbemServices, MyWbemNamespace> {
    MyWbemNamespaceState *State();

    bool ObjectPathMaybeForUs(const wchar_t *path) {
        return path && wcsistr(path, MY_WBEM_PNP_CLASS);
    }

    bool QueryMaybeForUs(const wchar_t *query) // safe?
    {
        return query && (wcsistr(query, MY_WBEM_PNP_CLASS) || wcsistr(query, MY_WBEM_PNP_CLASS_BASE) ||
                         wcsistr(query, MY_WBEM_PNP_CLASS_BASE_2) || wcsistr(query, MY_WBEM_PNP_CLASS_BASE_TOP));
    }

public:
    STDMETHODIMP OpenNamespace(const BSTR strNamespace, long lFlags, IWbemContext *pCtx, IWbemServices **ppWorkingNamespace, IWbemCallResult **ppResult) override;

    STDMETHODIMP CreateInstanceEnum(const BSTR strFilter, long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum) override {
        HRESULT res = mInner->CreateInstanceEnum(strFilter, lFlags, pCtx, ppEnum);
        if (res == S_OK && ppEnum && State()->Type == MyWbemNamespaceType::Cimv2 && IsWbemClassOursOrBase(strFilter)) {
            *ppEnum = (new MyWbemEnumClassObject(mInner))->WrapInterface(*ppEnum);
        }
        return res;
    }
    STDMETHODIMP CreateInstanceEnumAsync(const BSTR strFilter, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        MyWbemObjectSink *wrapper = nullptr;
        if (pResponseHandler && State()->Type == MyWbemNamespaceType::Cimv2 && IsWbemClassOursOrBase(strFilter)) {
            pResponseHandler = (wrapper = new MyWbemObjectSink(mInner))->WrapInterface(pResponseHandler);
        }
        HRESULT res = mInner->CreateInstanceEnumAsync(strFilter, lFlags, pCtx, pResponseHandler);
        if (wrapper) {
            wrapper->UndoWrapInterface(pResponseHandler);
        }
        return res;
    }

    STDMETHODIMP GetObject(const BSTR strObjectPath, long lFlags, IWbemContext *pCtx, IWbemClassObject **ppObject, IWbemCallResult **ppCallResult) override {
        HRESULT res = mInner->GetObject(strObjectPath, lFlags, pCtx, ppObject, ppCallResult);
        if (res == S_OK && ppCallResult && ObjectPathMaybeForUs(strObjectPath)) {
            *ppCallResult = (new MyWbemGetObjectCallResult(mInner, strObjectPath, State()->Type))->WrapInterface(*ppCallResult);
        }
        if (res == WBEM_E_NOT_FOUND && ObjectPathMaybeForUs(strObjectPath)) {
            bool absName;
            int devIdx = ParseWbemObjectName(strObjectPath, State()->Type, &absName);
            if (devIdx >= 0) {
                if (ppObject) {
                    *ppObject = CreateWbemClassObject(mInner, devIdx, absName).Take();
                }
                if (ppCallResult) {
                    *ppCallResult = ComRef(new MyWbemObjectCallResult(CreateWbemClassObject(mInner, devIdx, absName))).Take();
                }
                res = S_OK;
            }
        }
        return res;
    }
    STDMETHODIMP GetObjectAsync(const BSTR strObjectPath, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        // Note: we don't check Type because strObjectPath may be absolute
        MyWbemGetObjectSink *wrapper = nullptr;
        if (pResponseHandler && ObjectPathMaybeForUs(strObjectPath)) {
            pResponseHandler = (wrapper = new MyWbemGetObjectSink(mInner, strObjectPath, State()->Type))->WrapInterface(pResponseHandler);
        }
        HRESULT res = mInner->GetObjectAsync(strObjectPath, lFlags, pCtx, pResponseHandler);
        if (wrapper) {
            wrapper->UndoWrapInterface(pResponseHandler);
        }
        return res;
    }

    STDMETHODIMP ExecQuery(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum) override {
        HRESULT res = mInner->ExecQuery(strQueryLanguage, strQuery, lFlags, pCtx, ppEnum);
        if (res == S_OK && ppEnum && State()->Type == MyWbemNamespaceType::Cimv2 && QueryMaybeForUs(strQuery)) {
            auto queryFilter = SharedPtr<WbemQueryFilter>::New(strQueryLanguage, strQuery);
            *ppEnum = (new MyWbemEnumClassObject(mInner, queryFilter))->WrapInterface(*ppEnum);
        }
        return res;
    }
    STDMETHODIMP ExecQueryAsync(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        MyWbemObjectSink *wrapper = nullptr;
        if (pResponseHandler && State()->Type == MyWbemNamespaceType::Cimv2 && QueryMaybeForUs(strQuery)) {
            auto queryFilter = SharedPtr<WbemQueryFilter>::New(strQueryLanguage, strQuery);
            pResponseHandler = (wrapper = new MyWbemObjectSink(mInner, queryFilter))->WrapInterface(pResponseHandler);
        }
        HRESULT res = mInner->ExecQueryAsync(strQueryLanguage, strQuery, lFlags, pCtx, pResponseHandler);
        if (wrapper) {
            wrapper->UndoWrapInterface(pResponseHandler);
        }
        return res;
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

    STDMETHODIMP ExecNotificationQuery(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum) override {
        return mInner->ExecNotificationQuery(strQueryLanguage, strQuery, lFlags, pCtx, ppEnum);
    }
    STDMETHODIMP ExecNotificationQueryAsync(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler) override {
        return mInner->ExecNotificationQueryAsync(strQueryLanguage, strQuery, lFlags, pCtx, pResponseHandler);
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

class MyWbemNamespace : public ComWrapperBase<MyWbemNamespace, MyWbemServicesIntf, COM_RPC_INTERFACES(MyWbemNamespace)> {
public:
    MyWbemNamespaceState State;
    MyWbemNamespace(MyWbemNamespaceType type) : State{type} {}
};

MyWbemNamespaceState *MyWbemServicesIntf::State() { return &mRoot->State; }

class MyWbemNamespaceCallResult;

class MyWbemNamespaceCallResultIntf : public MyWbemCallResultIntfBase<MyWbemNamespaceCallResultIntf, MyWbemNamespaceCallResult> {
    MyWbemNamespaceState *State();

    STDMETHODIMP GetResultServices(long lTimeout, IWbemServices **ppServices) override {
        HRESULT res = mInner->GetResultServices(lTimeout, ppServices);
        if (res == S_OK && ppServices) {
            *ppServices = (new MyWbemNamespace(State()->Type))->WrapInterface(*ppServices);
        }
        return res;
    }
};

class MyWbemNamespaceCallResult : public ComWrapperBase<MyWbemNamespaceCallResult, MyWbemNamespaceCallResultIntf, COM_RPC_INTERFACES(MyWbemNamespaceCallResult)> {
public:
    MyWbemNamespaceState State;
    MyWbemNamespaceCallResult(MyWbemNamespaceType type) : State{type} {}
};

MyWbemNamespaceState *MyWbemNamespaceCallResultIntf::State() { return &mRoot->State; }

STDMETHODIMP MyWbemServicesIntf::OpenNamespace(const BSTR strNamespace, long lFlags, IWbemContext *pCtx,
                                               IWbemServices **ppWorkingNamespace, IWbemCallResult **ppResult) {
    HRESULT res = mInner->OpenNamespace(strNamespace, lFlags, pCtx, ppWorkingNamespace, ppResult);
    if (res == S_OK && (ppWorkingNamespace || ppResult)) {
        auto type = ParseWbemNamespace(State()->Type, strNamespace, false);
        if (ppWorkingNamespace) {
            *ppWorkingNamespace = (new MyWbemNamespace(type))->WrapInterface(*ppWorkingNamespace);
        }
        if (ppResult) {
            *ppResult = (new MyWbemNamespaceCallResult(type))->WrapInterface(*ppResult);
        }
    }
    return res;
}

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
            auto type = ParseWbemNamespace(MyWbemNamespaceType::None, strNetworkResource, true);
            *ppNamespace = (new MyWbemNamespace(type))->WrapInterface(*ppNamespace);
        }
        return res;
    }
};

class MyWbemLocator : public ComWrapperBase<MyWbemLocator, MyWbemLocatorIntf, ComUnknownIntf<MyWbemLocator>> {};

WeakAtomic<ComClassFactory<MyWbemLocator> *> mWbemLocatorFactory;

void WrapClassIfNeeded(REFCLSID rclsid, REFIID riid, LPVOID *ppv) {
    if (rclsid == __uuidof(WbemLocator)) {
        *ppv = WrapNewComInstance<ComClassFactory<MyWbemLocator>>(riid, *ppv);
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
