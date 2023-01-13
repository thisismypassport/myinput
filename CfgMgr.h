#pragma once
#include "Hook.h"
#include "Device.h"
#include "Header.h"
#include "XUsbApi.h"
#include "NotifyApi.h"
#include <cfgmgr32.h>
#include <devpkey.h>

DEVINST gRootDevInst;
bool gUniqLogCfgMgrFind;

enum {
    CustomDevInstStart = 0x71feffff, // DEVINST are indices to an array, so a large value seems safe
};

DeviceIntf *GetCustomDevInstDevice(DEVINST handle) {
    uintptr_t value = (uintptr_t)handle;
    if (value >= CustomDevInstStart && value < CustomDevInstStart + XUSER_MAX_COUNT) {
        return ImplGetDevice((DWORD)(value - CustomDevInstStart));
    } else {
        return nullptr;
    }
}

static bool WStrCopy(PWSTR buffer, ULONG bufferLen, const wchar_t *src) {
    ULONG srcLen = (ULONG)wcslen(src) + 1;
    CopyMemory(buffer, src, min(srcLen, bufferLen) * sizeof(wchar_t));
    return srcLen <= bufferLen;
}

static void ZZWStrMoveToEnd(PZZWSTR &buffer, ULONG &bufferLen) {
    while (bufferLen && *buffer) {
        ULONG size = (ULONG)wcslen(buffer) + 1;
        buffer += size;
        bufferLen -= size;
    }
}

static bool ZZWStrAppend(PZZWSTR &buffer, ULONG &bufferLen, const wchar_t *src) {
    ULONG srcLen = (ULONG)wcslen(src) + 1;
    if (srcLen < bufferLen) // (1 for null-of-nulls)
    {
        ULONG size = min(srcLen, bufferLen);
        CopyMemory(buffer, src, size * sizeof(wchar_t));
        buffer += size;
        bufferLen -= size;
        *buffer = L'\0';
        return true;
    }
    return false;
}

CONFIGRET WINAPI CM_Get_Parent_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && pdnDevInst) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Parent " << device->UserIdx << END;
        }
        *pdnDevInst = gRootDevInst;
        return CR_SUCCESS;
    }

    return CM_Get_Parent_Real(pdnDevInst, dnDevInst, ulFlags);
}

CONFIGRET WINAPI CM_Get_Sibling_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) {
    UINT userMask;
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && pdnDevInst) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Sibling " << device->UserIdx << END;
        }

        if (ImplGetUsers(&userMask, device->UserIdx + 1) > 0) {
            *pdnDevInst = CustomDevInstStart + ImplNextUser(&userMask);
            return CR_SUCCESS;
        }

        *pdnDevInst = 0;
        return CR_NO_SUCH_DEVNODE;
    }

    CONFIGRET ret = CM_Get_Sibling_Real(pdnDevInst, dnDevInst, ulFlags);
    DEVINST parent;
    if (ret == CR_NO_SUCH_DEVNODE &&
        CM_Get_Parent_Real(&parent, dnDevInst, 0) == CR_SUCCESS &&
        parent == gRootDevInst &&
        ImplGetUsers(&userMask) > 0) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Sibling (pre)" << END;
        }
        *pdnDevInst = CustomDevInstStart + ImplNextUser(&userMask);
        ret = CR_SUCCESS;
    }
    return ret;
}

CONFIGRET WINAPI CM_Get_Child_Hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && pdnDevInst) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Child " << device->UserIdx << END;
        }
        *pdnDevInst = 0;
        return CR_NO_SUCH_DEVNODE;
    }

    return CM_Get_Child_Real(pdnDevInst, dnDevInst, ulFlags);
}

CONFIGRET WINAPI CM_Get_Depth_Hook(PULONG pulDepth, DEVINST dnDevInst, ULONG ulFlags) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && pulDepth) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Depth " << device->UserIdx << END;
        }
        *pulDepth = 1;
        return CR_SUCCESS;
    }

    return CM_Get_Depth_Real(pulDepth, dnDevInst, ulFlags);
}

CONFIGRET WINAPI CM_Get_Device_ID_Size_Ex_Hook(PULONG pulLen, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && pulLen) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_ID_Size_Ex " << device->UserIdx << END;
        }

        const wchar_t *src = device->DeviceInstName.Get();
        *pulLen = (ULONG)wcslen(src); // not including null
        return CR_SUCCESS;
    }

    return CM_Get_Device_ID_Size_Ex_Real(pulLen, dnDevInst, ulFlags, hMachine);
}

CONFIGRET WINAPI CM_Get_Device_ID_ExW_Hook(DEVINST dnDevInst, PWSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && buffer) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_ID_ExW " << device->UserIdx << END;
        }

        const wchar_t *src = device->DeviceInstName.Get();
        return WStrCopy(buffer, bufferLen, src) ? CR_SUCCESS : CR_BUFFER_SMALL;
    }

    return CM_Get_Device_ID_ExW_Real(dnDevInst, buffer, bufferLen, ulFlags, hMachine);
}

static bool DeviceIDListFilterMatches(PCWSTR pszFilter, ULONG ulFlags) {
    ulFlags &= ~CM_GETIDLIST_FILTER_PRESENT;

    if (ulFlags == CM_GETIDLIST_FILTER_NONE) {
        return true;
    }

    if (ulFlags == CM_GETIDLIST_FILTER_ENUMERATOR && pszFilter && wcscmp(pszFilter, L"ROOT") == 0) {
        return true;
    }

    // TODO: CM_GETIDLIST_FILTER_CLASS ?
    return false;
}

CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW_Hook(PULONG pulLen, PCWSTR pszFilter, ULONG ulFlags) {
    if (G.ApiDebug) {
        LOG << "CM_Get_Device_ID_List_SizeW " << (pszFilter ? pszFilter : L"") << ", " << ulFlags << END;
    }

    CONFIGRET ret = CM_Get_Device_ID_List_SizeW_Real(pulLen, pszFilter, ulFlags);

    if (ret == CR_SUCCESS && pulLen && DeviceIDListFilterMatches(pszFilter, ulFlags)) {
        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device) {
                *pulLen += (ULONG)wcslen(device->DeviceInstName) + 1;
            }
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_ID_ListW_Hook(PCWSTR pszFilter, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    if (G.ApiDebug) {
        LOG << "CM_Get_Device_ID_ListW " << (pszFilter ? pszFilter : L"") << ", " << ulFlags << END;
    }

    CONFIGRET ret = CM_Get_Device_ID_ListW_Real(pszFilter, buffer, bufferLen, ulFlags);

    if (ret == CR_SUCCESS && buffer && DeviceIDListFilterMatches(pszFilter, ulFlags)) {
        ZZWStrMoveToEnd(buffer, bufferLen);

        bool ok = true;
        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (ok && device) {
                ok = ZZWStrAppend(buffer, bufferLen, device->DeviceInstName);
            }
        }

        ret = ok ? CR_SUCCESS : CR_BUFFER_SMALL;
    }

    return ret;
}

static DEVINST LocateCustomDevNode(DEVINSTID_W pDeviceID) {
    for (int i = 0; i < XUSER_MAX_COUNT; i++) {
        DeviceIntf *device = ImplGetDevice(i);
        if (device && wcscmp(pDeviceID, device->DeviceInstName) == 0) {
            if (G.ApiDebug) {
                LOG << "CM_Locate_DevNodeW " << pDeviceID << END;
            }

            return CustomDevInstStart + i;
        }
    }
    return 0;
}

CONFIGRET WINAPI CM_Locate_DevNodeW_Hook(PDEVINST pdnDevInst, DEVINSTID_W pDeviceID, ULONG ulFlags) {
    // Without validation, CM_Locate_DevNodeW_Real would create a dummy device node, which is unwanted
    // The question then is whether validation is faster or slower than LocateCustomDevNode...
    if ((ulFlags & CM_LOCATE_DEVNODE_NOVALIDATION) && pDeviceID && pdnDevInst) {
        DEVINST dev = LocateCustomDevNode(pDeviceID);
        if (dev) {
            *pdnDevInst = dev;
            return CR_SUCCESS;
        }
    }

    CONFIGRET ret = CM_Locate_DevNodeW_Real(pdnDevInst, pDeviceID, ulFlags);

    if (ret == CR_NO_SUCH_DEVNODE && pDeviceID && pdnDevInst) {
        DEVINST dev = LocateCustomDevNode(pDeviceID);
        if (dev) {
            *pdnDevInst = dev;
            return CR_SUCCESS;
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_DevNode_Status_Hook(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, ULONG ulFlags) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && pulStatus && pulProblemNumber) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_Status " << device->UserIdx << END;
        }

        *pulStatus = DN_DRIVER_LOADED | DN_STARTED | DN_NT_ENUMERATOR | DN_NT_DRIVER | DN_ROOT_ENUMERATED; // ???
        *pulProblemNumber = 0;
        return CR_SUCCESS;
    }

    return CM_Get_DevNode_Status_Real(pulStatus, pulProblemNumber, dnDevInst, ulFlags);
}

static int CMGetPropertyKeys(DEVPROPKEY *propKeys, PULONG propKeyCount, std::initializer_list<DEVPROPKEY> keys) {
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
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, const wchar_t *src) {
    return CMGetPropertyValue(propType, propBuf, propSize, src, (ULONG)(wcslen(src) + 1) * sizeof(wchar_t), DEVPROP_TYPE_STRING);
}
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, uint16_t src) {
    return CMGetPropertyValue(propType, propBuf, propSize, &src, sizeof(uint16_t), DEVPROP_TYPE_UINT16);
}
static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, uint32_t src) {
    return CMGetPropertyValue(propType, propBuf, propSize, &src, sizeof(uint32_t), DEVPROP_TYPE_UINT32);
}

static int CMGetPropertyValue(DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, std::initializer_list<const wchar_t *> src) {
    ULONG count = 1;
    for (auto &str : src) {
        count += (ULONG)wcslen(str) + 1;
    }

    return CMGetPropertyValue(propType, propBuf, propSize, &src, count * sizeof(wchar_t), DEVPROP_TYPE_STRING_LIST, [](PVOID dest, PCVOID src, ULONG size) {
        wchar_t *buffer = (wchar_t *)dest;
        *buffer = L'\0';

        for (auto &str : *(std::initializer_list<const wchar_t *> *)src) {
            ZZWStrAppend(buffer, size, str);
        }
    });
}

#define FORMAT_GUID_BUFSIZE 39

void FormatGuid(wchar_t dest[FORMAT_GUID_BUFSIZE], GUID guid) {
    wsprintfW(dest, L"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
              (int)guid.Data1, (int)guid.Data2, (int)guid.Data3, (int)guid.Data4[0], (int)guid.Data4[1],
              (int)guid.Data4[2], (int)guid.Data4[3], (int)guid.Data4[4], (int)guid.Data4[5], (int)guid.Data4[6], (int)guid.Data4[7]);
}

ostream &operator<<(ostream &o, const DEVPROPKEY *propKey) {
    wchar_t buffer[FORMAT_GUID_BUFSIZE];
    FormatGuid(buffer, propKey->fmtid);
    o << buffer << ":" << std::hex << propKey->pid;
    return o;
}

CONFIGRET WINAPI CM_Get_DevNode_Property_Keys_Hook(DEVINST dnDevInst, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && propKeyCount) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_Property_Keys " << device->UserIdx << END;
        }

        // TODO: more!
        return CMGetPropertyKeys(propKeys, propKeyCount, {
                                                             DEVPKEY_Device_InstanceId,
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
                                                         });
    }

    return CM_Get_DevNode_Property_Keys_Real(dnDevInst, propKeys, propKeyCount, ulFlags);
}

CONFIGRET WINAPI CM_Get_DevNode_PropertyW_Hook(DEVINST dnDevInst, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && propKey && propType && propSize) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_PropertyW " << device->UserIdx << ", " << propKey << END;
        }

        // TODO: more!
        if (*propKey == DEVPKEY_Device_InstanceId) {
            return CMGetPropertyValue(propType, propBuf, propSize, device->DeviceInstName);
        } else if (*propKey == DEVPKEY_Device_DeviceDesc) {
            return CMGetPropertyValue(propType, propBuf, propSize, L"HID-compliant game controller");
        } else if (*propKey == DEVPKEY_Device_HardwareIds) {
            return CMGetPropertyValue(propType, propBuf, propSize, {device->DeviceBaseName, L"HID_DEVICE_SYSTEM_GAME", L"HID_DEVICE_UP:0001_U:0005", L"HID_DEVICE"});
        } else if (*propKey == DEVPKEY_Device_CompatibleIds) {
            return CMGetPropertyValue(propType, propBuf, propSize, {});
        } else if (*propKey == DEVPKEY_Device_Class) {
            return CMGetPropertyValue(propType, propBuf, propSize, L"HIDClass");
        } else if (*propKey == DEVPKEY_Device_ClassGuid) {
            return CMGetPropertyValue(propType, propBuf, propSize, GUID_DEVCLASS_HIDCLASS);
        } else if (*propKey == DEVPKEY_Device_Driver) {
            return CMGetPropertyValue(propType, propBuf, propSize, L"{745a17a0-74d3-11d0-b6fe-00a0c90f57da}\\0006"); // ?
        } else if (*propKey == DEVPKEY_Device_ConfigFlags) {
            return CMGetPropertyValue(propType, propBuf, propSize, (uint32_t)0);
        } else if (*propKey == DEVPKEY_Device_Manufacturer) {
            return CMGetPropertyValue(propType, propBuf, propSize, L"(Standard system devices)");
        } else if (*propKey == DEVPKEY_Device_PDOName) {
            return CMGetPropertyValue(propType, propBuf, propSize, LR"(\Device\ffffffff)"); // dummy
        } else if (*propKey == DEVPKEY_Device_Capabilities) {
            return CMGetPropertyValue(propType, propBuf, propSize, (uint32_t)0xe0);
        } else {
            return CR_NO_SUCH_VALUE;
        }
    }

    return CM_Get_DevNode_PropertyW_Real(dnDevInst, propKey, propType, propBuf, propSize, ulFlags);
}

CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW_Hook(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags) {
    DeviceIntf *device = GetCustomDevInstDevice(dnDevInst);
    if (device && pulLength) {
        if (G.ApiDebug) {
            LOG << "CM_Get_DevNode_Registry_PropertyW " << device->UserIdx << ", " << prop << END;
        }

        DEVPROPKEY key = DEVPKEY_Device_DeviceDesc;
        key.pid = prop + 1;
        DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
        ULONG origLength = *pulLength;
        CONFIGRET ret = CM_Get_DevNode_PropertyW_Hook(dnDevInst, &key, &type, (PBYTE)buffer, pulLength, ulFlags);

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
                *pulLength = FORMAT_GUID_BUFSIZE * sizeof(wchar_t);
                if (ret == CR_SUCCESS && origLength >= FORMAT_GUID_BUFSIZE * sizeof(wchar_t)) {
                    FormatGuid((wchar_t *)buffer, *(GUID *)buffer);
                } else {
                    ret = CR_BUFFER_SMALL;
                }
                break;

            default:
                LOG << "Unexpected prop type: " << type << END;
                break;
            }

            if (propType) {
                *propType = regType;
            }
        }
        return ret;
    }

    return CM_Get_DevNode_Registry_PropertyW_Real(dnDevInst, prop, propType, buffer, pulLength, ulFlags);
}

struct FilterResult {
    int User = -1;
    int XUsb = -1;

    bool Matches(DeviceIntf *device, int xusb = -1) const {
        return (User < 0 || User == device->UserIdx) &&
               !(XUsb == (int)true && !device->IsXInput) &&
               (xusb < 0 || XUsb < 0 || XUsb == xusb);
    }
};

static bool DeviceInterfaceListFilterMatches(LPGUID clsGuid, DEVINSTID_W pDeviceID, FilterResult *outFilter) {
    FilterResult filter;
    if (clsGuid) {
        if (*clsGuid == GUID_DEVINTERFACE_XUSB) {
            filter.XUsb = true;
        } else if (*clsGuid == GUID_DEVINTERFACE_HID) {
            filter.XUsb = false;
        } else {
            return false;
        }
    }

    if (pDeviceID && *pDeviceID) {
        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device && wcscmp(pDeviceID, device->DeviceInstName) == 0 &&
                filter.Matches(device)) {
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

CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW_Hook(PULONG pulLen, LPGUID clsGuid, DEVINSTID_W pDeviceID, ULONG ulFlags) {
    CONFIGRET ret = CM_Get_Device_Interface_List_SizeW_Real(pulLen, clsGuid, pDeviceID, ulFlags);

    FilterResult filter;
    if (ret == CR_SUCCESS && pulLen && DeviceInterfaceListFilterMatches(clsGuid, pDeviceID, &filter)) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_List_SizeW " << (pDeviceID ? pDeviceID : L"") << END;
        }

        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device) {
                if (filter.Matches(device, false)) {
                    *pulLen += (ULONG)wcslen(device->DevicePathW) + 1;
                }
                if (device->IsXInput && filter.Matches(device, true)) {
                    *pulLen += (ULONG)wcslen(device->XDevicePathW) + 1;
                }
            }
        }
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_Interface_ListW_Hook(LPGUID clsGuid, DEVINSTID_W pDeviceID, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags) {
    CONFIGRET ret = CM_Get_Device_Interface_ListW_Real(clsGuid, pDeviceID, buffer, bufferLen, ulFlags);

    FilterResult filter;
    if (ret == CR_SUCCESS && buffer && DeviceInterfaceListFilterMatches(clsGuid, pDeviceID, &filter)) {
        if (!gUniqLogCfgMgrFind) {
            gUniqLogCfgMgrFind = true;
            LOG << "Found device via CfgMgr API" << END;
        }
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_ListW " << (pDeviceID ? pDeviceID : L"") << END;
        }

        ZZWStrMoveToEnd(buffer, bufferLen);

        bool ok = true;
        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device) {
                if (ok && filter.Matches(device, false)) {
                    ok = ZZWStrAppend(buffer, bufferLen, device->DevicePathW);
                }
                if (ok && device->IsXInput && filter.Matches(device, true)) {
                    ok = ZZWStrAppend(buffer, bufferLen, device->XDevicePathW);
                }
            }
        }

        ret = ok ? CR_SUCCESS : CR_BUFFER_SMALL;
    }

    return ret;
}

CONFIGRET WINAPI CM_Get_Device_Interface_Property_KeysW_Hook(LPCWSTR pszIntf, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags) {
    ULONG oldKeyCount = propKeyCount ? *propKeyCount : 0;
    CONFIGRET ret = CM_Get_Device_Interface_Property_KeysW_Real(pszIntf, propKeys, propKeyCount, ulFlags);

    if (ret == CR_NO_SUCH_DEVICE_INTERFACE && pszIntf && propKeyCount) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_Property_KeysW " << pszIntf << END;
        }

        *propKeyCount = oldKeyCount;
        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device &&
                (wcscmp(pszIntf, device->DevicePathW) == 0 ||
                 (device->IsXInput && wcscmp(pszIntf, device->XDevicePathW) == 0))) {
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

CONFIGRET WINAPI CM_Get_Device_Interface_PropertyW_Hook(LPCWSTR pszIntf, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags) {
    ULONG oldPropSize = propSize ? *propSize : 0;
    CONFIGRET ret = CM_Get_Device_Interface_PropertyW_Real(pszIntf, propKey, propType, propBuf, propSize, ulFlags);

    if (ret == CR_NO_SUCH_DEVICE_INTERFACE && pszIntf && propKey && propType && propSize) {
        if (G.ApiDebug) {
            LOG << "CM_Get_Device_Interface_PropertyW " << pszIntf << ", " << propKey << END;
        }

        *propSize = oldPropSize;
        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            bool isXUsb = false;
            if (device &&
                (wcscmp(pszIntf, device->DevicePathW) == 0 ||
                 (isXUsb = (device->IsXInput && wcscmp(pszIntf, device->XDevicePathW) == 0)))) {
                if (*propKey == DEVPKEY_DeviceInterface_ClassGuid) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, isXUsb ? GUID_DEVINTERFACE_XUSB : GUID_DEVINTERFACE_HID);
                } else if (*propKey == DEVPKEY_DeviceInterface_Enabled) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, DEVPROP_TRUE);
                } else if (*propKey == DEVPKEY_Device_InstanceId) {
                    ret = CMGetPropertyValue(propType, propBuf, propSize, device->DeviceInstName);
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

CONFIGRET WINAPI CM_Register_Notification_Hook(PCM_NOTIFY_FILTER pFilter, PVOID pContext, PCM_NOTIFY_CALLBACK pCallback, PHCMNOTIFICATION pNotifyContext) {
    if (pFilter && pNotifyContext) {
        FilterResult filter;
        DeviceIntf *device;
        switch (pFilter->FilterType) {
        case CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE:
            if ((pFilter->Flags & CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES) ||
                pFilter->u.DeviceInterface.ClassGuid == GUID_DEVINTERFACE_HID) {
                CONFIGRET ret = CM_Register_Notification_Real(pFilter, pContext, pCallback, pNotifyContext);
                if (ret == CR_SUCCESS) {
                    HCMNOTIFICATION notify = *pNotifyContext;
                    GThreadPoolNotifications.Register(notify, [pContext, pCallback, notify](ImplUser *user, bool added) {
                        CM_NOTIFY_ACTION action = added ? CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL : CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL;

                        size_t size = sizeof(CM_NOTIFY_EVENT_DATA) + wcslen(user->Device->DevicePathW) * sizeof(wchar_t);
                        CM_NOTIFY_EVENT_DATA *event = (CM_NOTIFY_EVENT_DATA *)new byte[size];
                        ZeroMemory(event, sizeof(*event));
                        event->FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
                        event->u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_HID;
                        wcscpy(event->u.DeviceInterface.SymbolicLink, user->Device->DevicePathW);

                        pCallback(notify, pContext, action, event, (int)size);
                        delete[] event;
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
                    *pNotifyContext = (HCMNOTIFICATION)GThreadPoolNotifications.Allocate();
                } else {
                    ret = CM_Register_Notification_Real(pFilter, pContext, pCallback, pNotifyContext);
                }

                if (ret == CR_SUCCESS) {
                    HCMNOTIFICATION notify = *pNotifyContext;
                    GThreadPoolNotifications.Register(notify, [pContext, pCallback, notify, filter](ImplUser *user, bool added) {
                        if (filter.Matches(user->Device)) {
                            CM_NOTIFY_ACTION action = added ? CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED : CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED;

                            size_t size = sizeof(CM_NOTIFY_EVENT_DATA) + wcslen(user->Device->DeviceInstName) * sizeof(wchar_t);
                            CM_NOTIFY_EVENT_DATA *event = (CM_NOTIFY_EVENT_DATA *)new byte[size];
                            ZeroMemory(event, sizeof(*event));
                            event->FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;
                            wcscpy(event->u.DeviceInstance.InstanceId, user->Device->DeviceInstName);

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
            device = GetDeviceByHandle(pFilter->u.DeviceHandle.hTarget);
            if (device) {
                HCMNOTIFICATION notify = (HCMNOTIFICATION)GThreadPoolNotifications.Allocate();
                GThreadPoolNotifications.Register(notify, [pContext, pCallback, notify, device](ImplUser *user, bool added) {
                    if (user->Device == device && !added) {
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
            LOG << "Unknown cfgmgr notification type: " << pFilter->FilterType << END;
            break;
        }
    }

    return CM_Register_Notification_Real(pFilter, pContext, pCallback, pNotifyContext);
}

CONFIGRET WINAPI CM_Unregister_Notification_Hook(HCMNOTIFICATION NotifyContext) {
    if (GThreadPoolNotifications.Unregister(NotifyContext, true)) {
        return CR_SUCCESS;
    } else {
        return CM_Unregister_Notification_Real(NotifyContext);
    }
}

void HookCfgMgr() {
    // Only hooking the (currently?) primary functions... (not ideal)

    CM_Locate_DevNodeW(&gRootDevInst, NULL, 0);

    AddGlobalHook(&CM_Get_Parent_Real, CM_Get_Parent_Hook);
    AddGlobalHook(&CM_Get_Child_Real, CM_Get_Child_Hook);
    AddGlobalHook(&CM_Get_Sibling_Real, CM_Get_Sibling_Hook);
    AddGlobalHook(&CM_Get_Depth_Real, CM_Get_Depth_Hook);

    AddGlobalHook(&CM_Get_Device_ID_Size_Ex_Real, CM_Get_Device_ID_Size_Ex_Hook); // Ex is primary here
    AddGlobalHook(&CM_Get_Device_ID_ExW_Real, CM_Get_Device_ID_ExW_Hook);         // Ex is primary here
    AddGlobalHook(&CM_Get_Device_ID_List_SizeW_Real, CM_Get_Device_ID_List_SizeW_Hook);
    AddGlobalHook(&CM_Get_Device_ID_ListW_Real, CM_Get_Device_ID_ListW_Hook);

    AddGlobalHook(&CM_Locate_DevNodeW_Real, CM_Locate_DevNodeW_Hook);
    AddGlobalHook(&CM_Get_DevNode_Status_Real, CM_Get_DevNode_Status_Hook);
    AddGlobalHook(&CM_Get_DevNode_Property_Keys_Real, CM_Get_DevNode_Property_Keys_Hook);
    AddGlobalHook(&CM_Get_DevNode_PropertyW_Real, CM_Get_DevNode_PropertyW_Hook);
    AddGlobalHook(&CM_Get_DevNode_Registry_PropertyW_Real, CM_Get_DevNode_Registry_PropertyW_Hook);

    AddGlobalHook(&CM_Get_Device_Interface_List_SizeW_Real, CM_Get_Device_Interface_List_SizeW_Hook);
    AddGlobalHook(&CM_Get_Device_Interface_ListW_Real, CM_Get_Device_Interface_ListW_Hook);
    AddGlobalHook(&CM_Get_Device_Interface_Property_KeysW_Real, CM_Get_Device_Interface_Property_KeysW_Hook);
    AddGlobalHook(&CM_Get_Device_Interface_PropertyW_Real, CM_Get_Device_Interface_PropertyW_Hook);

    AddGlobalHook(&CM_Register_Notification_Real, CM_Register_Notification_Hook);
    AddGlobalHook(&CM_Unregister_Notification_Real, CM_Unregister_Notification_Hook);
}
