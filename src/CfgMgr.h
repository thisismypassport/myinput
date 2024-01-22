#pragma once
#include "Hook.h"
#include "StateUtils.h"
#include "DeviceApi.h"
#include "Header.h"
#include <cfgmgr32.h>
#include <devpkey.h>

DEVINST gRootDevInst;
UniqueLog gUniqLogCfgMgrFind;
AddrRange gCfgMgrAddrs;

enum {
    CustomDevInstStart = 0x71feffff, // DEVINST are indices to an array, so a large value seems safe
};

DeviceNode *GetCustomDevInstNode(DEVINST handle, int *outIdx = nullptr) {
    uintptr_t value = (uintptr_t)handle;
    if (value >= CustomDevInstStart && value < CustomDevInstStart + IMPL_MAX_DEVNODES) {
        DWORD idx = (DWORD)(value - CustomDevInstStart);
        if (outIdx) {
            *outIdx = idx;
        }
        return ImplGetDeviceNode(idx);
    }
    return nullptr;
}

template <class tchar>
static bool TStrCopy(tchar *buffer, ULONG bufferLen, const tchar *src) {
    ULONG srcLen = (ULONG)tstrlen(src) + 1;
    CopyMemory(buffer, src, min(srcLen, bufferLen) * sizeof(tchar));
    return srcLen <= bufferLen;
}

template <class tchar>
static void ZZTStrMoveToEnd(tchar *&buffer, ULONG &bufferLen) {
    while (bufferLen && *buffer) {
        ULONG size = (ULONG)tstrlen(buffer) + 1;
        buffer += size;
        bufferLen -= size;
    }
}

template <class tchar>
static bool ZZTStrAppend(tchar *&buffer, ULONG &bufferLen, const tchar *src) {
    ULONG srcLen = (ULONG)tstrlen(src) + 1;
    if (srcLen < bufferLen) // (1 for null-of-nulls)
    {
        ULONG size = min(srcLen, bufferLen);
        CopyMemory(buffer, src, size * sizeof(tchar));
        buffer += size;
        bufferLen -= size;
        *buffer = TCHAR('\0');
        return true;
    }
    return false;
}

template <class TOrigCall>
static CONFIGRET CM_Get_Parent_GenHook(PDEVINST pdnDevInst, DEVINST dnDevInst, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && pdnDevInst) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Parent " << node << END;
        }
        *pdnDevInst = gRootDevInst;
        return CR_SUCCESS;
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_Parent_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) {
    return CM_Get_Parent_GenHook(pdnDevInst, dnDevInst, _ReturnAddress(),
                                 [&] { return CM_Get_Parent_Real(pdnDevInst, dnDevInst, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Parent_Ex_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Parent_GenHook(pdnDevInst, dnDevInst, _ReturnAddress(),
                                 [&] { return CM_Get_Parent_Ex_Real(pdnDevInst, dnDevInst, ulFlags, hMachine); });
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_Sibling_GenHook(PDEVINST pdnDevInst, DEVINST dnDevInst, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    int devNodeIdx;
    DeviceNode *node = GetCustomDevInstNode(dnDevInst, &devNodeIdx);
    if (node && pdnDevInst) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Sibling " << node << END;
        }

        int nextIdx;
        if (ImplNextDeviceNode(devNodeIdx + 1, &nextIdx)) {
            *pdnDevInst = CustomDevInstStart + nextIdx;
            return CR_SUCCESS;
        }

        *pdnDevInst = 0;
        return CR_NO_SUCH_DEVNODE;
    }

    CONFIGRET ret = origCall();

    DEVINST parent;
    int firstIdx;
    if (ret == CR_NO_SUCH_DEVNODE &&
        CM_Get_Parent_Real(&parent, dnDevInst, 0) == CR_SUCCESS &&
        parent == gRootDevInst &&
        ImplNextDeviceNode(0, &firstIdx)) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Sibling (pre)" << END;
        }
        *pdnDevInst = CustomDevInstStart + firstIdx;
        ret = CR_SUCCESS;
    }
    return ret;
}

CONFIGRET WINAPI CM_Get_Sibling_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) {
    return CM_Get_Sibling_GenHook(pdnDevInst, dnDevInst, _ReturnAddress(),
                                  [&] { return CM_Get_Sibling_Real(pdnDevInst, dnDevInst, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Sibling_Ex_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Sibling_GenHook(pdnDevInst, dnDevInst, _ReturnAddress(),
                                  [&] { return CM_Get_Sibling_Ex_Real(pdnDevInst, dnDevInst, ulFlags, hMachine); });
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_Child_GenHook(PDEVINST pdnDevInst, DEVINST dnDevInst, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && pdnDevInst) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Child " << node << END;
        }
        *pdnDevInst = 0;
        return CR_NO_SUCH_DEVNODE;
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_Child_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) {
    return CM_Get_Child_GenHook(pdnDevInst, dnDevInst, _ReturnAddress(),
                                [&] { return CM_Get_Child_Real(pdnDevInst, dnDevInst, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Child_Ex_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Child_GenHook(pdnDevInst, dnDevInst, _ReturnAddress(),
                                [&] { return CM_Get_Child_Ex_Real(pdnDevInst, dnDevInst, ulFlags, hMachine); });
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_Depth_GenHook(PULONG pulDepth, DEVINST dnDevInst, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && pulDepth) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Depth " << node << END;
        }
        *pulDepth = 1;
        return CR_SUCCESS;
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_Depth_Hook(PULONG pulDepth, DEVINST dnDevInst, ULONG ulFlags) {
    return CM_Get_Depth_GenHook(pulDepth, dnDevInst, _ReturnAddress(),
                                [&] { return CM_Get_Depth_Real(pulDepth, dnDevInst, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Depth_Ex_Hook(PULONG pulDepth, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Depth_GenHook(pulDepth, dnDevInst, _ReturnAddress(),
                                [&] { return CM_Get_Depth_Ex_Real(pulDepth, dnDevInst, ulFlags, hMachine); });
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_ID_Size_GenHook(PULONG pulLen, DEVINST dnDevInst, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && pulLen) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_ID_Size " << node << END;
        }

        *pulLen = (ULONG)wcslen(node->DeviceInstNameW.Get()); // not including null
        return CR_SUCCESS;
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_Device_ID_Size_Hook(PULONG pulLen, DEVINST dnDevInst, ULONG ulFlags) {
    return CM_Get_Device_ID_Size_GenHook(pulLen, dnDevInst, _ReturnAddress(),
                                         [&] { return CM_Get_Device_ID_Size_Real(pulLen, dnDevInst, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_ID_Size_Ex_Hook(PULONG pulLen, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_ID_Size_GenHook(pulLen, dnDevInst, _ReturnAddress(),
                                         [&] { return CM_Get_Device_ID_Size_Ex_Real(pulLen, dnDevInst, ulFlags, hMachine); });
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_ID_GenHook(DEVINST dnDevInst, tchar *buffer, ULONG bufferLen, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && buffer) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_ID " << node << END;
        }

        const tchar *src = node->DeviceInstName<tchar>();
        return TStrCopy(buffer, bufferLen, src) ? CR_SUCCESS : CR_BUFFER_SMALL;
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_Device_IDA_Hook(DEVINST dnDevInst, PSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    return CM_Get_Device_ID_GenHook(dnDevInst, buffer, bufferLen, _ReturnAddress(),
                                    [&] { return CM_Get_Device_IDA_Real(dnDevInst, buffer, bufferLen, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_ID_ExA_Hook(DEVINST dnDevInst, PSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_ID_GenHook(dnDevInst, buffer, bufferLen, _ReturnAddress(),
                                    [&] { return CM_Get_Device_ID_ExA_Real(dnDevInst, buffer, bufferLen, ulFlags, hMachine); });
}
CONFIGRET WINAPI CM_Get_Device_IDW_Hook(DEVINST dnDevInst, PWSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    return CM_Get_Device_ID_GenHook(dnDevInst, buffer, bufferLen, _ReturnAddress(),
                                    [&] { return CM_Get_Device_IDW_Real(dnDevInst, buffer, bufferLen, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_ID_ExW_Hook(DEVINST dnDevInst, PWSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_ID_GenHook(dnDevInst, buffer, bufferLen, _ReturnAddress(),
                                    [&] { return CM_Get_Device_ID_ExW_Real(dnDevInst, buffer, bufferLen, ulFlags, hMachine); });
}

template <class tchar>
static bool DeviceIDListFilterMatches(const tchar *pszFilter, ULONG ulFlags) {
    ulFlags &= ~CM_GETIDLIST_FILTER_PRESENT;

    if (ulFlags == CM_GETIDLIST_FILTER_NONE) {
        return true;
    }

    if (ulFlags == CM_GETIDLIST_FILTER_ENUMERATOR && pszFilter && tstreq(pszFilter, TSTR("ROOT"))) {
        return true;
    }

    // TODO: CM_GETIDLIST_FILTER_CLASS ?
    return false;
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_ID_List_Size_GenHook(PULONG pulLen, const tchar *pszFilter, ULONG ulFlags, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    if (G.ApiDebug) {
        LOG << "CM_Get_Device_ID_List_Size " << (pszFilter ? pszFilter : TSTR("")) << ", " << ulFlags << END;
    }

    CONFIGRET ret = origCall();

    if (ret == CR_SUCCESS && pulLen && DeviceIDListFilterMatches(pszFilter, ulFlags)) {
        for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
            DeviceNode *node = ImplGetDeviceNode(i);
            if (node) {
                *pulLen += (ULONG)tstrlen(node->DeviceInstName<tchar>()) + 1;
            }
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_ID_List_SizeA_Hook(PULONG pulLen, PCSTR pszFilter, ULONG ulFlags) {
    return CM_Get_Device_ID_List_Size_GenHook(pulLen, pszFilter, ulFlags, _ReturnAddress(),
                                              [&] { return CM_Get_Device_ID_List_SizeA_Real(pulLen, pszFilter, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_ID_List_Size_ExA_Hook(PULONG pulLen, PCSTR pszFilter, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_ID_List_Size_GenHook(pulLen, pszFilter, ulFlags, _ReturnAddress(),
                                              [&] { return CM_Get_Device_ID_List_Size_ExA_Real(pulLen, pszFilter, ulFlags, hMachine); });
}
CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW_Hook(PULONG pulLen, PCWSTR pszFilter, ULONG ulFlags) {
    return CM_Get_Device_ID_List_Size_GenHook(pulLen, pszFilter, ulFlags, _ReturnAddress(),
                                              [&] { return CM_Get_Device_ID_List_SizeW_Real(pulLen, pszFilter, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_ID_List_Size_ExW_Hook(PULONG pulLen, PCWSTR pszFilter, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_ID_List_Size_GenHook(pulLen, pszFilter, ulFlags, _ReturnAddress(),
                                              [&] { return CM_Get_Device_ID_List_Size_ExW_Real(pulLen, pszFilter, ulFlags, hMachine); });
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_ID_List_GenHook(const tchar *pszFilter, tchar *buffer, ULONG bufferLen, ULONG ulFlags, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    if (G.ApiDebug) {
        LOG << "CM_Get_Device_ID_List " << (pszFilter ? pszFilter : TSTR("")) << ", " << ulFlags << END;
    }

    CONFIGRET ret = origCall();

    if (ret == CR_SUCCESS && buffer && DeviceIDListFilterMatches(pszFilter, ulFlags)) {
        ZZTStrMoveToEnd(buffer, bufferLen);

        bool ok = true;
        for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
            DeviceNode *node = ImplGetDeviceNode(i);
            if (ok && node) {
                ok = ZZTStrAppend(buffer, bufferLen, node->DeviceInstName<tchar>());
            }
        }

        ret = ok ? CR_SUCCESS : CR_BUFFER_SMALL;
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_ID_ListA_Hook(PCSTR pszFilter, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    return CM_Get_Device_ID_List_GenHook(pszFilter, buffer, bufferLen, ulFlags, _ReturnAddress(),
                                         [&] { return CM_Get_Device_ID_ListA_Real(pszFilter, buffer, bufferLen, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_ID_List_ExA_Hook(PCSTR pszFilter, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_ID_List_GenHook(pszFilter, buffer, bufferLen, ulFlags, _ReturnAddress(),
                                         [&] { return CM_Get_Device_ID_List_ExA_Real(pszFilter, buffer, bufferLen, ulFlags, hMachine); });
}
CONFIGRET WINAPI CM_Get_Device_ID_ListW_Hook(PCWSTR pszFilter, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    return CM_Get_Device_ID_List_GenHook(pszFilter, buffer, bufferLen, ulFlags, _ReturnAddress(),
                                         [&] { return CM_Get_Device_ID_ListW_Real(pszFilter, buffer, bufferLen, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_ID_List_ExW_Hook(PCWSTR pszFilter, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_ID_List_GenHook(pszFilter, buffer, bufferLen, ulFlags, _ReturnAddress(),
                                         [&] { return CM_Get_Device_ID_List_ExW_Real(pszFilter, buffer, bufferLen, ulFlags, hMachine); });
}

template <class tchar>
static DEVINST LocateCustomDevNode(tchar *pDeviceID) {
    for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
        DeviceNode *node = ImplGetDeviceNode(i);
        if (node && tstreq(pDeviceID, node->DeviceInstName<tchar>())) {
            if (G.ApiDebug) {
                LOG << "CM_Locate_DevNode " << pDeviceID << END;
            }

            return CustomDevInstStart + i;
        }
    }
    return 0;
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Locate_DevNode_GenHook(PDEVINST pdnDevInst, tchar *pDeviceID, ULONG ulFlags, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    // Without validation, CM_Locate_DevNode*_Real would create a dummy device node, which is unwanted
    // The question then is whether validation is faster or slower than LocateCustomDevNode...
    if ((ulFlags & CM_LOCATE_DEVNODE_NOVALIDATION) && pDeviceID && pdnDevInst) {
        DEVINST dev = LocateCustomDevNode(pDeviceID);
        if (dev) {
            *pdnDevInst = dev;
            return CR_SUCCESS;
        }
    }

    CONFIGRET ret = origCall();

    if (ret == CR_NO_SUCH_DEVNODE && pDeviceID && pdnDevInst) {
        DEVINST dev = LocateCustomDevNode(pDeviceID);
        if (dev) {
            *pdnDevInst = dev;
            return CR_SUCCESS;
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Locate_DevNodeA_Hook(PDEVINST pdnDevInst, DEVINSTID_A pDeviceID, ULONG ulFlags) {
    return CM_Locate_DevNode_GenHook(pdnDevInst, pDeviceID, ulFlags, _ReturnAddress(),
                                     [&] { return CM_Locate_DevNodeA_Real(pdnDevInst, pDeviceID, ulFlags); });
}
CONFIGRET WINAPI CM_Locate_DevNode_ExA_Hook(PDEVINST pdnDevInst, DEVINSTID_A pDeviceID, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Locate_DevNode_GenHook(pdnDevInst, pDeviceID, ulFlags, _ReturnAddress(),
                                     [&] { return CM_Locate_DevNode_ExA_Real(pdnDevInst, pDeviceID, ulFlags, hMachine); });
}
CONFIGRET WINAPI CM_Locate_DevNodeW_Hook(PDEVINST pdnDevInst, DEVINSTID_W pDeviceID, ULONG ulFlags) {
    return CM_Locate_DevNode_GenHook(pdnDevInst, pDeviceID, ulFlags, _ReturnAddress(),
                                     [&] { return CM_Locate_DevNodeW_Real(pdnDevInst, pDeviceID, ulFlags); });
}
CONFIGRET WINAPI CM_Locate_DevNode_ExW_Hook(PDEVINST pdnDevInst, DEVINSTID_W pDeviceID, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Locate_DevNode_GenHook(pdnDevInst, pDeviceID, ulFlags, _ReturnAddress(),
                                     [&] { return CM_Locate_DevNode_ExW_Real(pdnDevInst, pDeviceID, ulFlags, hMachine); });
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_DevNode_Status_GenHook(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && pulStatus && pulProblemNumber) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_Status " << node << END;
        }

        *pulStatus = DN_DRIVER_LOADED | DN_STARTED | DN_NT_ENUMERATOR | DN_NT_DRIVER | DN_ROOT_ENUMERATED; // ???
        *pulProblemNumber = 0;
        return CR_SUCCESS;
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_DevNode_Status_Hook(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, ULONG ulFlags) {
    return CM_Get_DevNode_Status_GenHook(pulStatus, pulProblemNumber, dnDevInst, _ReturnAddress(),
                                         [&] { return CM_Get_DevNode_Status_Real(pulStatus, pulProblemNumber, dnDevInst, ulFlags); });
}
CONFIGRET WINAPI CM_Get_DevNode_Status_Ex_Hook(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_DevNode_Status_GenHook(pulStatus, pulProblemNumber, dnDevInst, _ReturnAddress(),
                                         [&] { return CM_Get_DevNode_Status_Ex_Real(pulStatus, pulProblemNumber, dnDevInst, ulFlags, hMachine); });
}

static int CMGetPropertyKeys(DEVPROPKEY *propKeys, PULONG propKeyCount, initializer_list<DEVPROPKEY> keys) {
    if (!propKeys || *propKeyCount < keys.size()) {
        *propKeyCount = (ULONG)keys.size();
        return CR_BUFFER_SMALL;
    }

    int i = 0;
    for (auto &key : keys) {
        propKeys[i++] = key;
    }

    *propKeyCount = (ULONG)keys.size();
    return CR_SUCCESS;
}

static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, const void *src, ULONG srcSize, DEVPROPTYPE type,
                              void (*customMemCpy)(PVOID dest, PCVOID src, ULONG size) = nullptr) {
    *propType = type;

    if (!propBuf || *propSize < srcSize) {
        *propSize = srcSize;
        return CR_BUFFER_SMALL;
    }

    if (customMemCpy) {
        customMemCpy(propBuf, src, srcSize);
    } else {
        CopyMemory(propBuf, src, srcSize);
    }
    *propSize = srcSize;
    return CR_SUCCESS;
}

static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, DEVPROP_BOOLEAN src) {
    return CMGetPropertyValue(propType, propBuf, propSize, &src, sizeof(DEVPROP_BOOLEAN), DEVPROP_TYPE_BOOLEAN);
}
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, const GUID &src) {
    return CMGetPropertyValue(propType, propBuf, propSize, &src, sizeof(GUID), DEVPROP_TYPE_GUID);
}
template <class tchar>
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, const tchar *src) {
    return CMGetPropertyValue(propType, propBuf, propSize, src, (ULONG)(tstrlen(src) + 1) * sizeof(tchar), DEVPROP_TYPE_STRING);
}
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, uint16_t src) {
    return CMGetPropertyValue(propType, propBuf, propSize, &src, sizeof(uint16_t), DEVPROP_TYPE_UINT16);
}
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, uint32_t src) {
    return CMGetPropertyValue(propType, propBuf, propSize, &src, sizeof(uint32_t), DEVPROP_TYPE_UINT32);
}

template <class tchar>
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, const vector<const tchar *> &src) {
    ULONG count = 1;
    for (auto &str : src) {
        count += (ULONG)tstrlen(str) + 1;
    }

    return CMGetPropertyValue(propType, propBuf, propSize, &src, count * sizeof(tchar), DEVPROP_TYPE_STRING_LIST, [](PVOID dest, PCVOID src, ULONG size) {
        tchar *buffer = (tchar *)dest;
        *buffer = L'\0';

        for (auto &str : *(vector<const tchar *> *)src) {
            ZZTStrAppend(buffer, size, str);
        }
    });
}

ostream &operator<<(ostream &o, const DEVPROPKEY *propKey) {
    wchar_t buffer[FORMAT_GUID_BUFSIZE];
    FormatGuid(buffer, propKey->fmtid);
    o << buffer << ":" << std::hex << propKey->pid;
    return o;
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_DevNode_Property_Keys_GenHook(DEVINST dnDevInst, DEVPROPKEY *propKeys, PULONG propKeyCount, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && propKeyCount) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_Property_Keys " << node << END;
        }

        // TODO: more!
        return CMGetPropertyKeys(propKeys, propKeyCount, {
                                                             DEVPKEY_Device_DeviceDesc,
                                                             DEVPKEY_Device_HardwareIds,
                                                             DEVPKEY_Device_CompatibleIds,
                                                             DEVPKEY_Device_Class,
                                                             DEVPKEY_Device_ClassGuid,
                                                             DEVPKEY_Device_Driver,
                                                             DEVPKEY_Device_ConfigFlags,
                                                             DEVPKEY_Device_Manufacturer,
                                                             DEVPKEY_Device_PDOName,
                                                             DEVPKEY_Device_Capabilities,
                                                             DEVPKEY_Device_InstanceId,
                                                             DEVPKEY_NAME,
                                                         });
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_DevNode_Property_Keys_Hook(DEVINST dnDevInst, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags) {
    return CM_Get_DevNode_Property_Keys_GenHook(dnDevInst, propKeys, propKeyCount, _ReturnAddress(),
                                                [&] { return CM_Get_DevNode_Property_Keys_Real(dnDevInst, propKeys, propKeyCount, ulFlags); });
}
CONFIGRET WINAPI CM_Get_DevNode_Property_Keys_Ex_Hook(DEVINST dnDevInst, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_DevNode_Property_Keys_GenHook(dnDevInst, propKeys, propKeyCount, _ReturnAddress(),
                                                [&] { return CM_Get_DevNode_Property_Keys_Ex_Real(dnDevInst, propKeys, propKeyCount, ulFlags, hMachine); });
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Get_DevNode_Property_GenHook(DEVINST dnDevInst, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && propKey && propType && propSize) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_Property " << node->UserIdx << ", " << propKey << END;
        }

        // TODO: more!
        if (*propKey == DEVPKEY_Device_InstanceId) {
            return CMGetPropertyValue(propType, propBuf, propSize, node->DeviceInstName<tchar>());
        } else if (*propKey == DEVPKEY_Device_DeviceDesc || *propKey == DEVPKEY_NAME) {
            return CMGetPropertyValue(propType, propBuf, propSize, node->DeviceDescription<tchar>());
        } else if (*propKey == DEVPKEY_Device_HardwareIds) {
            return CMGetPropertyValue(propType, propBuf, propSize, node->DeviceHardwareIDs<tchar>());
        } else if (*propKey == DEVPKEY_Device_CompatibleIds) {
            return CMGetPropertyValue<tchar>(propType, propBuf, propSize, vector<const tchar *>());
        } else if (*propKey == DEVPKEY_Device_Class) {
            return CMGetPropertyValue(propType, propBuf, propSize, node->DeviceClass<tchar>());
        } else if (*propKey == DEVPKEY_Device_ClassGuid) {
            return CMGetPropertyValue(propType, propBuf, propSize, node->DeviceClassGuid());
        } else if (*propKey == DEVPKEY_Device_Driver) {
            return CMGetPropertyValue(propType, propBuf, propSize, node->DeviceClassDriver<tchar>());
        } else if (*propKey == DEVPKEY_Device_ConfigFlags) {
            return CMGetPropertyValue(propType, propBuf, propSize, (uint32_t)0);
        } else if (*propKey == DEVPKEY_Device_Manufacturer) {
            return CMGetPropertyValue(propType, propBuf, propSize, node->DeviceManufacturer<tchar>());
        } else if (*propKey == DEVPKEY_Device_PDOName) {
            return CMGetPropertyValue(propType, propBuf, propSize, TSTR(R"(\Device\ffffffff)")); // dummy
        } else if (*propKey == DEVPKEY_Device_Capabilities) {
            return CMGetPropertyValue(propType, propBuf, propSize, (uint32_t)0xe0);
        } else {
            return CR_NO_SUCH_VALUE;
        }
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_DevNode_PropertyW_Hook(DEVINST dnDevInst, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags) {
    return CM_Get_DevNode_Property_GenHook<wchar_t>(dnDevInst, propKey, propType, propBuf, propSize, _ReturnAddress(),
                                                    [&] { return CM_Get_DevNode_PropertyW_Real(dnDevInst, propKey, propType, propBuf, propSize, ulFlags); });
}
CONFIGRET WINAPI CM_Get_DevNode_Property_ExW_Hook(DEVINST dnDevInst, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_DevNode_Property_GenHook<wchar_t>(dnDevInst, propKey, propType, propBuf, propSize, _ReturnAddress(),
                                                    [&] { return CM_Get_DevNode_Property_ExW_Real(dnDevInst, propKey, propType, propBuf, propSize, ulFlags, hMachine); });
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Get_DevNode_Registry_Property_GenHook(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    DeviceNode *node = GetCustomDevInstNode(dnDevInst);
    if (node && pulLength) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_Registry_Property " << node << ", " << prop << END;
        }

        DEVPROPKEY key = DEVPKEY_Device_DeviceDesc;
        key.pid = prop + 1;
        DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
        ULONG origLength = *pulLength;
        CONFIGRET ret = CM_Get_DevNode_Property_GenHook<tchar>(dnDevInst, &key, &type, (PBYTE)buffer, pulLength, nullptr, [] { return CR_NO_SUCH_VALUE; });

        if (ret != CR_NO_SUCH_VALUE) {
            ULONG regType = REG_NONE;
            switch (type) {
            case DEVPROP_TYPE_STRING:
                regType = REG_SZ;
                break;
            case DEVPROP_TYPE_STRING_LIST:
                regType = REG_MULTI_SZ;
                break;
            case DEVPROP_TYPE_BINARY:
                regType = REG_BINARY;
                break;
            case DEVPROP_TYPE_UINT32:
                regType = REG_DWORD;
                break;
            case DEVPROP_TYPE_UINT64:
                regType = REG_QWORD;
                break;

            case DEVPROP_TYPE_GUID:
                regType = REG_SZ;
                *pulLength = FORMAT_GUID_BUFSIZE * sizeof(tchar);
                if (ret == CR_SUCCESS && origLength >= FORMAT_GUID_BUFSIZE * sizeof(tchar)) {
                    FormatGuid((tchar *)buffer, *(GUID *)buffer);
                } else {
                    ret = CR_BUFFER_SMALL;
                }
                break;

            default:
                LOG_ERR << "Unexpected prop type: " << type << END;
                break;
            }

            if (propType) {
                *propType = regType;
            }
        }
        return ret;
    }

    return origCall();
}

CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyA_Hook(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags) {
    return CM_Get_DevNode_Registry_Property_GenHook<char>(dnDevInst, prop, propType, buffer, pulLength, _ReturnAddress(),
                                                          [&] { return CM_Get_DevNode_Registry_PropertyA_Real(dnDevInst, prop, propType, buffer, pulLength, ulFlags); });
}
CONFIGRET WINAPI CM_Get_DevNode_Registry_Property_ExA_Hook(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_DevNode_Registry_Property_GenHook<char>(dnDevInst, prop, propType, buffer, pulLength, _ReturnAddress(),
                                                          [&] { return CM_Get_DevNode_Registry_Property_ExA_Real(dnDevInst, prop, propType, buffer, pulLength, ulFlags, hMachine); });
}
CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW_Hook(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags) {
    return CM_Get_DevNode_Registry_Property_GenHook<wchar_t>(dnDevInst, prop, propType, buffer, pulLength, _ReturnAddress(),
                                                             [&] { return CM_Get_DevNode_Registry_PropertyW_Real(dnDevInst, prop, propType, buffer, pulLength, ulFlags); });
}
CONFIGRET WINAPI CM_Get_DevNode_Registry_Property_ExW_Hook(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_DevNode_Registry_Property_GenHook<wchar_t>(dnDevInst, prop, propType, buffer, pulLength, _ReturnAddress(),
                                                             [&] { return CM_Get_DevNode_Registry_Property_ExW_Real(dnDevInst, prop, propType, buffer, pulLength, ulFlags, hMachine); });
}

struct FilterResult {
    int User = -1;
    int Types = ~0;

    bool Matches(DeviceNode *node) const {
        return (User < 0 || User == node->UserIdx) &&
               (node->NodeType & Types) != 0;
    }
};

template <class tchar>
static bool DeviceInterfaceListFilterMatches(LPGUID clsGuid, const tchar *pDeviceID, FilterResult *outFilter) {
    FilterResult filter;
    if (clsGuid) {
        if (*clsGuid == GUID_DEVINTERFACE_XUSB) {
            filter.Types = DEVICE_NODE_TYPE_XUSB;
        } else if (*clsGuid == GUID_DEVINTERFACE_HID) {
            filter.Types = DEVICE_NODE_TYPE_HID;
        } else {
            return false;
        }
    }

    if (pDeviceID && *pDeviceID) {
        for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
            DeviceNode *node = ImplGetDeviceNode(i);
            if (node && tstreq(pDeviceID, node->DeviceInstName<tchar>()) &&
                filter.Matches(node)) {
                filter.User = i;
                *outFilter = filter;
                return true;
            }
        }

        return false;
    }

    *outFilter = filter;
    return true;
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_Interface_List_Size_GenHook(PULONG pulLen, LPGUID clsGuid, tchar *pDeviceID, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    CONFIGRET ret = origCall();

    FilterResult filter;
    if (ret == CR_SUCCESS && pulLen && DeviceInterfaceListFilterMatches(clsGuid, pDeviceID, &filter)) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_List_Size " << (pDeviceID ? pDeviceID : TSTR("")) << END;
        }

        for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
            DeviceNode *node = ImplGetDeviceNode(i);
            if (node && filter.Matches(node)) {
                *pulLen += (ULONG)tstrlen(node->DevicePath<tchar>()) + 1;
            }
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeA_Hook(PULONG pulLen, LPGUID clsGuid, DEVINSTID_A pDeviceID, ULONG ulFlags) {
    return CM_Get_Device_Interface_List_Size_GenHook(pulLen, clsGuid, pDeviceID, _ReturnAddress(),
                                                     [&] { return CM_Get_Device_Interface_List_SizeA_Real(pulLen, clsGuid, pDeviceID, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_List_Size_ExA_Hook(PULONG pulLen, LPGUID clsGuid, DEVINSTID_A pDeviceID, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_Interface_List_Size_GenHook(pulLen, clsGuid, pDeviceID, _ReturnAddress(),
                                                     [&] { return CM_Get_Device_Interface_List_Size_ExA_Real(pulLen, clsGuid, pDeviceID, ulFlags, hMachine); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW_Hook(PULONG pulLen, LPGUID clsGuid, DEVINSTID_W pDeviceID, ULONG ulFlags) {
    return CM_Get_Device_Interface_List_Size_GenHook(pulLen, clsGuid, pDeviceID, _ReturnAddress(),
                                                     [&] { return CM_Get_Device_Interface_List_SizeW_Real(pulLen, clsGuid, pDeviceID, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_List_Size_ExW_Hook(PULONG pulLen, LPGUID clsGuid, DEVINSTID_W pDeviceID, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_Interface_List_Size_GenHook(pulLen, clsGuid, pDeviceID, _ReturnAddress(),
                                                     [&] { return CM_Get_Device_Interface_List_Size_ExW_Real(pulLen, clsGuid, pDeviceID, ulFlags, hMachine); });
}

template <class tchar, class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_Interface_List_GenHook(LPGUID clsGuid, tchar *pDeviceID, tchar *buffer, ULONG bufferLen, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    CONFIGRET ret = origCall();

    FilterResult filter;
    if (ret == CR_SUCCESS && buffer && DeviceInterfaceListFilterMatches(clsGuid, pDeviceID, &filter)) {
        if (gUniqLogCfgMgrFind) {
            LOG << "Found device via CfgMgr API" << END;
        }
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_List " << (pDeviceID ? pDeviceID : TSTR("")) << END;
        }

        ZZTStrMoveToEnd(buffer, bufferLen);

        bool ok = true;
        for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
            DeviceNode *node = ImplGetDeviceNode(i);
            if (node && filter.Matches(node)) {
                ok = ZZTStrAppend(buffer, bufferLen, node->DevicePath<tchar>());
            }
        }

        ret = ok ? CR_SUCCESS : CR_BUFFER_SMALL;
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_Interface_ListA_Hook(LPGUID clsGuid, DEVINSTID_A pDeviceID, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    return CM_Get_Device_Interface_List_GenHook(clsGuid, pDeviceID, buffer, bufferLen, _ReturnAddress(),
                                                [&] { return CM_Get_Device_Interface_ListA_Real(clsGuid, pDeviceID, buffer, bufferLen, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_List_ExA_Hook(LPGUID clsGuid, DEVINSTID_A pDeviceID, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_Interface_List_GenHook(clsGuid, pDeviceID, buffer, bufferLen, _ReturnAddress(),
                                                [&] { return CM_Get_Device_Interface_List_ExA_Real(clsGuid, pDeviceID, buffer, bufferLen, ulFlags, hMachine); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_ListW_Hook(LPGUID clsGuid, DEVINSTID_W pDeviceID, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    return CM_Get_Device_Interface_List_GenHook(clsGuid, pDeviceID, buffer, bufferLen, _ReturnAddress(),
                                                [&] { return CM_Get_Device_Interface_ListW_Real(clsGuid, pDeviceID, buffer, bufferLen, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_List_ExW_Hook(LPGUID clsGuid, DEVINSTID_W pDeviceID, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_Interface_List_GenHook(clsGuid, pDeviceID, buffer, bufferLen, _ReturnAddress(),
                                                [&] { return CM_Get_Device_Interface_List_ExW_Real(clsGuid, pDeviceID, buffer, bufferLen, ulFlags, hMachine); });
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_Interface_Property_Keys_GenHook(LPCWSTR pszIntf, DEVPROPKEY *propKeys, PULONG propKeyCount, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    ULONG oldKeyCount = propKeyCount ? *propKeyCount : 0;
    CONFIGRET ret = origCall();

    if (ret == CR_NO_SUCH_DEVICE_INTERFACE && pszIntf && propKeyCount) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_Property_Keys " << pszIntf << END;
        }

        *propKeyCount = oldKeyCount;
        for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
            DeviceNode *node = ImplGetDeviceNode(i);
            if (node && tstreq(pszIntf, node->DevicePathW)) {
                ret = CMGetPropertyKeys(propKeys, propKeyCount, {
                                                                    DEVPKEY_DeviceInterface_ClassGuid,
                                                                    DEVPKEY_DeviceInterface_Enabled,
                                                                    DEVPKEY_Device_InstanceId,
                                                                    DEVPKEY_DeviceInterface_HID_UsagePage,
                                                                    DEVPKEY_DeviceInterface_HID_UsageId,
                                                                    DEVPKEY_DeviceInterface_HID_IsReadOnly,
                                                                    DEVPKEY_DeviceInterface_HID_VendorId,
                                                                    DEVPKEY_DeviceInterface_HID_ProductId,
                                                                    DEVPKEY_DeviceInterface_HID_VersionNumber,
                                                                });
                break;
            }
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_Interface_Property_KeysW_Hook(LPCWSTR pszIntf, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags) {
    return CM_Get_Device_Interface_Property_Keys_GenHook(pszIntf, propKeys, propKeyCount, _ReturnAddress(),
                                                         [&] { return CM_Get_Device_Interface_Property_KeysW_Real(pszIntf, propKeys, propKeyCount, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_Property_Keys_ExW_Hook(LPCWSTR pszIntf, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_Interface_Property_Keys_GenHook(pszIntf, propKeys, propKeyCount, _ReturnAddress(),
                                                         [&] { return CM_Get_Device_Interface_Property_Keys_ExW_Real(pszIntf, propKeys, propKeyCount, ulFlags, hMachine); });
}

template <class TOrigCall>
CONFIGRET WINAPI CM_Get_Device_Interface_Property_GenHook(LPCWSTR pszIntf, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, void *caller, TOrigCall origCall) {
    if (gCfgMgrAddrs.Contains(caller)) {
        return origCall();
    }

    ULONG oldPropSize = propSize ? *propSize : 0;
    CONFIGRET ret = origCall();

    if (ret == CR_NO_SUCH_DEVICE_INTERFACE && pszIntf && propKey && propType && propSize) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_Property " << pszIntf << ", " << propKey << END;
        }

        *propSize = oldPropSize;
        for (int i = 0; i < IMPL_MAX_DEVNODES; i++) {
            DeviceIntf *device = nullptr;
            DeviceNode *node = ImplGetDeviceNode(i, &device);
            if (node && tstreq(pszIntf, node->DevicePathW)) {
                if (*propKey == DEVPKEY_DeviceInterface_ClassGuid) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, node->DeviceIntfGuid());
                } else if (*propKey == DEVPKEY_DeviceInterface_Enabled) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, DEVPROP_TRUE);
                } else if (*propKey == DEVPKEY_Device_InstanceId) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, node->DeviceInstNameW.Get());
                } else if (*propKey == DEVPKEY_DeviceInterface_HID_UsagePage) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, (uint16_t)HID_USAGE_PAGE_GENERIC);
                } else if (*propKey == DEVPKEY_DeviceInterface_HID_UsageId) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, (uint16_t)HID_USAGE_GENERIC_GAMEPAD);
                } else if (*propKey == DEVPKEY_DeviceInterface_HID_IsReadOnly) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, DEVPROP_FALSE);
                } else if (*propKey == DEVPKEY_DeviceInterface_HID_VendorId) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, (uint16_t)device->VendorId);
                } else if (*propKey == DEVPKEY_DeviceInterface_HID_ProductId) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, (uint16_t)device->ProductId);
                } else if (*propKey == DEVPKEY_DeviceInterface_HID_VersionNumber) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, (uint16_t)device->VersionNum);
                } else {
                    ret = CR_NO_SUCH_VALUE;
                }
                break;
            }
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_Interface_PropertyW_Hook(LPCWSTR pszIntf, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags) {
    return CM_Get_Device_Interface_Property_GenHook(pszIntf, propKey, propType, propBuf, propSize, _ReturnAddress(),
                                                    [&] { return CM_Get_Device_Interface_PropertyW_Real(pszIntf, propKey, propType, propBuf, propSize, ulFlags); });
}
CONFIGRET WINAPI CM_Get_Device_Interface_Property_ExW_Hook(LPCWSTR pszIntf, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags, HMACHINE hMachine) {
    return CM_Get_Device_Interface_Property_GenHook(pszIntf, propKey, propType, propBuf, propSize, _ReturnAddress(),
                                                    [&] { return CM_Get_Device_Interface_Property_ExW_Real(pszIntf, propKey, propType, propBuf, propSize, ulFlags, hMachine); });
}

using ImplThreadPoolNotificationCb = function<void(DeviceNode *node, bool added)>;
void *ThreadPoolNotificationAllocate();
void ThreadPoolNotificationRegister(void *handle, ImplThreadPoolNotificationCb &&cb);
bool ThreadPoolNotificationUnregister(void *handle, bool wait);

CONFIGRET WINAPI CM_Register_Notification_Hook(PCM_NOTIFY_FILTER pFilter, PVOID pContext, PCM_NOTIFY_CALLBACK pCallback, PHCMNOTIFICATION pNotifyContext) {
    if (pFilter && pNotifyContext) {
        FilterResult filter;
        DeviceNode *handleNode;
        switch (pFilter->FilterType) {
        case CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE:
            if ((pFilter->Flags & CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES) ||
                DeviceInterfaceListFilterMatches<wchar_t>(&pFilter->u.DeviceInterface.ClassGuid, nullptr, &filter)) {
                CONFIGRET ret = CM_Register_Notification_Real(pFilter, pContext, pCallback, pNotifyContext);
                if (ret == CR_SUCCESS) {
                    HCMNOTIFICATION notify = *pNotifyContext;
                    ThreadPoolNotificationRegister(notify, [pContext, pCallback, notify, filter](DeviceNode *node, bool added) {
                        if (filter.Matches(node)) {
                            CM_NOTIFY_ACTION action = added ? CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL : CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL;

                            size_t size = sizeof(CM_NOTIFY_EVENT_DATA) + wcslen(node->DevicePathW) * sizeof(wchar_t);
                            CM_NOTIFY_EVENT_DATA *event = (CM_NOTIFY_EVENT_DATA *)new byte[size];
                            ZeroMemory(event, sizeof(*event));
                            event->FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
                            event->u.DeviceInterface.ClassGuid = node->DeviceIntfGuid();
                            wcscpy(event->u.DeviceInterface.SymbolicLink, node->DevicePathW);

                            pCallback(notify, pContext, action, event, (int)size);
                            delete[] event;
                        }
                    });
                }
                return ret;
            }
            break;

        case CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE:
            if ((pFilter->Flags & CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES) ||
                DeviceInterfaceListFilterMatches(nullptr, pFilter->u.DeviceInstance.InstanceId, &filter)) {
                CONFIGRET ret = CR_SUCCESS;
                if (filter.User >= 0) {
                    *pNotifyContext = (HCMNOTIFICATION)ThreadPoolNotificationAllocate();
                } else {
                    ret = CM_Register_Notification_Real(pFilter, pContext, pCallback, pNotifyContext);
                }

                if (ret == CR_SUCCESS) {
                    HCMNOTIFICATION notify = *pNotifyContext;
                    ThreadPoolNotificationRegister(notify, [pContext, pCallback, notify, filter](DeviceNode *node, bool added) {
                        if (filter.Matches(node)) {
                            CM_NOTIFY_ACTION action = added ? CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED : CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED;

                            size_t size = sizeof(CM_NOTIFY_EVENT_DATA) + wcslen(node->DeviceInstNameW) * sizeof(wchar_t);
                            CM_NOTIFY_EVENT_DATA *event = (CM_NOTIFY_EVENT_DATA *)new byte[size];
                            ZeroMemory(event, sizeof(*event));
                            event->FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;
                            wcscpy(event->u.DeviceInstance.InstanceId, node->DeviceInstNameW);

                            pCallback(notify, pContext, action, event, (int)size);
                            if (added) {
                                pCallback(notify, pContext, CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED, event, (int)size);
                            }

                            delete[] event;
                        }
                    });
                }
                return ret;
            }
            break;

        case CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE:
            handleNode = GetDeviceNodeByHandle(pFilter->u.DeviceHandle.hTarget);
            if (handleNode) {
                HCMNOTIFICATION notify = (HCMNOTIFICATION)ThreadPoolNotificationAllocate();
                ThreadPoolNotificationRegister(notify, [pContext, pCallback, notify, handleNode](DeviceNode *node, bool added) {
                    if (node == handleNode && !added) {
                        CM_NOTIFY_EVENT_DATA event;
                        ZeroMemory(&event, sizeof(event));
                        event.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;

                        pCallback(notify, pContext, CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE, &event, sizeof(event));
                    }
                });
                *pNotifyContext = notify;
                return CR_SUCCESS;
            }
            break;

        default:
            LOG_ERR << "Unknown cfgmgr notification type: " << pFilter->FilterType << END;
            break;
        }
    }

    return CM_Register_Notification_Real(pFilter, pContext, pCallback, pNotifyContext);
}

CONFIGRET WINAPI CM_Unregister_Notification_Hook(HCMNOTIFICATION NotifyContext) {
    if (ThreadPoolNotificationUnregister(NotifyContext, true)) {
        return CR_SUCCESS;
    } else {
        return CM_Unregister_Notification_Real(NotifyContext);
    }
}

void HookCfgMgr() {
    CM_Locate_DevNodeW(&gRootDevInst, NULL, 0);

    ADD_GLOBAL_HOOK(CM_Get_Parent, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Parent_Ex, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Child, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Child_Ex, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Sibling, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Sibling_Ex, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Depth, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Depth_Ex, &gCfgMgrAddrs);

    ADD_GLOBAL_HOOK(CM_Get_Device_ID_Size, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_Size_Ex, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_IDA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_ExA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_IDW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_ExW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_List_SizeA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_List_Size_ExA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_List_SizeW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_List_Size_ExW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_ListA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_List_ExA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_ListW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_ID_List_ExW, &gCfgMgrAddrs);

    ADD_GLOBAL_HOOK(CM_Locate_DevNodeA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Locate_DevNode_ExA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Locate_DevNodeW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Locate_DevNode_ExW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Status, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Status_Ex, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Property_Keys, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Property_Keys_Ex, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_PropertyW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Property_ExW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Registry_PropertyA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Registry_Property_ExA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Registry_PropertyW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_DevNode_Registry_Property_ExW, &gCfgMgrAddrs);

    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_List_SizeA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_List_Size_ExA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_List_SizeW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_List_Size_ExW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_ListA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_List_ExA, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_ListW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_List_ExW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_Property_KeysW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_Property_Keys_ExW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_PropertyW, &gCfgMgrAddrs);
    ADD_GLOBAL_HOOK(CM_Get_Device_Interface_Property_ExW, &gCfgMgrAddrs);

    ADD_GLOBAL_HOOK(CM_Register_Notification);
    ADD_GLOBAL_HOOK(CM_Unregister_Notification);

    gCfgMgrAddrs.Add(G.HInstance); // for tailcalls & Real-calls
}
