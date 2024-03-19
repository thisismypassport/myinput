#pragma once
#include "Hook.h"
#include "Device.h"
#include "Header.h"
#include "UtilsBuffer.h"
#include "RawRegister.h"

UniqueLog gUniqLogRawInputFind;

#ifndef _WIN64
BOOL gWow64;
#endif

HANDLE GetCustomDeviceHandle(int user) {
    return MakeOurHandle(CustomDeviceHandleHighStart + user);
}

DeviceIntf *GetCustomHandleDevice(HANDLE handle) {
    WORD handleHigh = GetOurHandle(handle);

    if (handleHigh >= CustomDeviceHandleHighStart && handleHigh < CustomDeviceHandleHighStart + IMPL_MAX_USERS) {
        return ImplGetDevice(handleHigh - CustomDeviceHandleHighStart);
    } else {
        return nullptr;
    }
}

UINT WINAPI GetRawInputDeviceList_Hook(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize) {
    if (G.ApiDebug) {
        LOG << "GetRawInputDeviceList ()" << END;
    }

    UINT usersMask;
    int usersCount = ImplGetUsers(&usersMask, DEVICE_NODE_TYPE_HID);

    UINT result = GetRawInputDeviceList_Real(pRawInputDeviceList, puiNumDevices, cbSize);
    bool ok = result != INVALID_UINT_VALUE;

    if (ok && pRawInputDeviceList && puiNumDevices) {
        if (cbSize != sizeof(RAWINPUTDEVICELIST)) {
            LOG_ERR << "Unknown input device list size: " << cbSize << END;
        } else if (result + usersCount > *puiNumDevices) {
            *puiNumDevices = result + usersCount;
            result = INVALID_UINT_VALUE;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        } else {
            for (int i = 0; i < usersCount; i++) {
                int userIdx = ImplNextUser(&usersMask);

                auto *device = &pRawInputDeviceList[result++];
                device->dwType = RIM_TYPEHID;
                device->hDevice = GetCustomDeviceHandle(userIdx);
            }
        }
    } else {
        if ((!pRawInputDeviceList || (!ok && GetLastError() == ERROR_INSUFFICIENT_BUFFER)) && puiNumDevices) {
            (*puiNumDevices) += usersCount;
        }
    }

    return result;
}

template <class TData, class TCopy>
static UINT ProcessRawInputDeviceInfo(UINT expectedSize, LPVOID pData, PUINT pcbSize, TCopy copyFunc, bool allowMore = true) {
    if (!pcbSize) {
        SetLastError(ERROR_NOACCESS);
        return -1;
    } else if (!pData) {
        *pcbSize = expectedSize;
        return 0;
    } else if (*pcbSize < expectedSize) {
        *pcbSize = expectedSize;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return -1;
    } else if (*pcbSize > expectedSize && !allowMore) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    } else {
        copyFunc((TData *)pData);
        return expectedSize;
    }
}

template <class tchar>
static UINT GetRawCustomDeviceInfo(UINT uiCommand, LPVOID pData, PUINT pcbSize, DeviceIntf *device) {
    UINT strSize;
    switch (uiCommand) {
    case RIDI_DEVICENAME:
        if (gUniqLogRawInputFind) {
            LOG << "Found device via RawInput API" << END;
        }
        strSize = (UINT)tstrlen(device->DevicePathW) + 1; // yes, not multiplied by sizeof(tchar)
        return ProcessRawInputDeviceInfo<tchar>(strSize, pData, pcbSize, [device](tchar *ptr) {
            tstrcpy(ptr, device->DevicePath<tchar>());
        });

    case RIDI_DEVICEINFO:
        return ProcessRawInputDeviceInfo<RID_DEVICE_INFO>(
            sizeof(RID_DEVICE_INFO), pData, pcbSize, [device](RID_DEVICE_INFO *ptr) {
                ptr->cbSize = sizeof(RID_DEVICE_INFO);
                ptr->dwType = RIM_TYPEHID;
                ptr->hid.dwVendorId = device->VendorId;
                ptr->hid.dwProductId = device->ProductId;
                ptr->hid.dwVersionNumber = device->VersionNum;
                ptr->hid.usUsagePage = HID_USAGE_PAGE_GENERIC;
                ptr->hid.usUsage = HID_USAGE_GENERIC_GAMEPAD;
            },
            false);

    case RIDI_PREPARSEDDATA:
        return ProcessRawInputDeviceInfo<uint8_t>(device->PreparsedSize, pData, pcbSize, [device](uint8_t *ptr) {
            CopyMemory(ptr, device->Preparsed, device->PreparsedSize);
        });

    default:
        LOG_ERR << "Unknown raw device command: " << uiCommand << END;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
}

UINT WINAPI GetRawInputDeviceInfoA_Hook(HANDLE hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize) {
    if (G.ApiDebug) {
        LOG << "GetRawInputDeviceInfoA (" << (uintptr_t)hDevice << ", " << uiCommand << ")" << END;
    }

    DeviceIntf *device = GetCustomHandleDevice(hDevice);
    if (device && device->HasHid()) {
        return GetRawCustomDeviceInfo<char>(uiCommand, pData, pcbSize, device);
    } else {
        return GetRawInputDeviceInfoA_Real(hDevice, uiCommand, pData, pcbSize);
    }
}

UINT WINAPI GetRawInputDeviceInfoW_Hook(HANDLE hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize) {
    if (G.ApiDebug) {
        LOG << "GetRawInputDeviceInfoW (" << (uintptr_t)hDevice << ", " << uiCommand << ")" << END;
    }

    DeviceIntf *device = GetCustomHandleDevice(hDevice);
    if (device && device->HasHid()) {
        return GetRawCustomDeviceInfo<wchar_t>(uiCommand, pData, pcbSize, device);
    } else {
        return GetRawInputDeviceInfoW_Real(hDevice, uiCommand, pData, pcbSize);
    }
}

UINT WINAPI GetRawInputData_Hook(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pCbSize, UINT cbSizeHeader) {
    if (G.ApiTrace) {
        LOG << "GetRawInputData (" << hRawInput << "," << uiCommand << ")" << END;
    }

    WORD handleHigh = GetOurHandle(hRawInput);
    if (cbSizeHeader == sizeof(RAWINPUTHEADER) && handleHigh) {
        UINT count;
        if (GRawInputRegMouse.Read(handleHigh, uiCommand, pData, pCbSize, &count)) {
            if (G.ApiTrace) {
                LOG << "GetRawInputData read mouse data" << END;
            }
            return count;
        } else if (GRawInputRegGamepad.Read(handleHigh, uiCommand, pData, pCbSize, &count)) {
            if (G.ApiTrace) {
                LOG << "GetRawInputData read gamepad data" << END;
            }
            return count;
        } else if (GRawInputRegKeyboard.Read(handleHigh, uiCommand, pData, pCbSize, &count)) {
            if (G.ApiTrace) {
                LOG << "GetRawInputData read keyboard data" << END;
            }
            return count;
        }
    }

    return GetRawInputData_Real(hRawInput, uiCommand, pData, pCbSize, cbSizeHeader);
}

void FixupRawInputBufferData(UINT *pResult, PRAWINPUT pData, UINT *pSize, UINT remaining) {
#ifndef _WIN64 // look at this mess windows made
    if (gWow64) {
        if (*pResult == INVALID_UINT_VALUE || !pData) {
            *pSize += 0x8;
        } else if (*pResult + 0x8 > remaining) {
            *pSize = *pResult + 0x8;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            *pResult = INVALID_UINT_VALUE;
        } else {
            byte *ptr = (byte *)pData;
            MoveMemory(ptr + 0x18, ptr + 0x10, *pResult - 0x10);
            *(UINT *)(ptr + 0x10) = *(UINT *)(ptr + 0xc);
            *(UINT *)(ptr + 0x14) = *(UINT *)(ptr + 0xc) = 0;
            *pResult += 0x8;
            pData->header.dwSize += 0x8;
        }
    }
#endif
}

UINT WINAPI GetRawInputBuffer_Hook(PRAWINPUT pData, PUINT pCbSize, UINT cbSizeHeader) {
    // can't rely on GetRawInputBuffer_Real - must simulate it
    if (cbSizeHeader == sizeof(RAWINPUTHEADER) && pCbSize) {
        if (G.ApiTrace) {
            LOG << "GetRawInputBuffer () custom impl." << END;
        }

        enum { PM_QS_OUR_WM_INPUT = (QS_RAWINPUT | QS_POSTMESSAGE) << 16 };

        UINT count = 0;
        UINT remaining = *pCbSize;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, WM_INPUT, WM_INPUT, PM_NOREMOVE | PM_QS_OUR_WM_INPUT) &&
               msg.message == WM_INPUT) {
            UINT size = remaining;
            UINT result = GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, pData, &size, cbSizeHeader);
            FixupRawInputBufferData(&result, pData, &size, remaining);
            if (result != INVALID_UINT_VALUE) {
                if (!pData) {
                    *pCbSize = size;
                    return 0;
                }

                PRAWINPUT pNext = NEXTRAWINPUTBLOCK(pData);
                UINT skip = (UINT)((uint8_t *)pNext - (uint8_t *)pData);
                pData = pNext;
                count++;

                if (skip < remaining) {
                    remaining -= skip;
                } else {
                    remaining = 0;
                }
            } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                if (count > 0) {
                    break;
                }

                *pCbSize = size;
                return INVALID_UINT_VALUE; // GetRawInputData's SetLastError
            }

            MSG realMsg;
            while (!PeekMessageW(&realMsg, nullptr, WM_INPUT, WM_INPUT, PM_REMOVE | PM_QS_OUR_WM_INPUT) ||
                   realMsg.message != WM_INPUT || realMsg.lParam != msg.lParam) {
                LOG << "Unexpected PeekMessage result: " << msg.message << END;
                // ???
                if (realMsg.message == WM_QUIT) {
                    PostQuitMessage((int)realMsg.wParam);
                }
            }

            if (remaining == 0) {
                break;
            }
        }

        if (!count) {
            *pCbSize = 0; // else, untouched?
        }

        return count;
    }

    return GetRawInputBuffer_Real(pData, pCbSize, cbSizeHeader);
}

template <class TRawInputReg>
static void TryRegisterRawInputDevice(const RAWINPUTDEVICE &instr, int usage, TRawInputReg &rawInputReg) {
    int mode = RIDEV_EXMODE(instr.dwFlags);

    if (instr.usUsagePage == HID_USAGE_PAGE_GENERIC &&
        (instr.usUsage == usage || (mode == RIDEV_PAGEONLY && !rawInputReg.IsRegisteredPrivate()))) {
        if ((instr.dwFlags & RIDEV_REMOVE) || mode == RIDEV_EXCLUDE) {
            rawInputReg.Unregister(instr.dwFlags);

            UINT fullFlags;
            HWND fullWindow;
            if (mode != RIDEV_EXCLUDE && GRawInputRegFullPage.GetRegistered(&fullWindow, &fullFlags)) {
                rawInputReg.Register(fullWindow, fullFlags);
            }
        } else {
            rawInputReg.Unregister(instr.dwFlags);
            rawInputReg.Register(instr.hwndTarget, instr.dwFlags);
        }

        rawInputReg.UpdateRealRegister(true);
    }
}

BOOL WINAPI RegisterRawInputDevices_Hook(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize) {
    const RAWINPUTDEVICE *realDevices = pRawInputDevices;
    UINT realNumDevices = uiNumDevices;
    RAWINPUTDEVICE *copy = nullptr;

    if (cbSize == sizeof(RAWINPUTDEVICE) && pRawInputDevices) {
        UINT copyI = 0;
        auto doCopyIfNeeded = [&](UINT i) {
            if (!copy) {
                copy = new RAWINPUTDEVICE[uiNumDevices];
                CopyMemory(copy, pRawInputDevices, cbSize * uiNumDevices);
                realDevices = copy;
                copyI = i;
            }
        };

        for (UINT i = 0; i < uiNumDevices; i++) {
            auto &instr = pRawInputDevices[i];
            if (G.ApiDebug) {
                LOG << "RegisterRawInputDevices - " << instr.usUsagePage << ", " << instr.usUsage << ", " << instr.dwFlags << END;
            }

            if (instr.usUsagePage == HID_USAGE_PAGE_GENERIC &&
                (instr.usUsage == HID_USAGE_GENERIC_KEYBOARD || instr.usUsage == HID_USAGE_GENERIC_MOUSE)) {
                doCopyIfNeeded(i);
                realNumDevices--;
            } else if (copy) {
                copy[copyI++] = copy[i];
            }
        }

        if (copy) {
            ASSERT(copyI == realNumDevices, "broken raw register logic");
        }
    }

    BOOL success = (uiNumDevices && !realNumDevices) ? TRUE : // real call fails for empty array
                       RegisterRawInputDevices_Real(realDevices, realNumDevices, cbSize);

    if (success && cbSize == sizeof(RAWINPUTDEVICE) && pRawInputDevices) {
        for (UINT i = 0; i < uiNumDevices; i++) {
            TryRegisterRawInputDevice(pRawInputDevices[i], 0, GRawInputRegFullPage);
            TryRegisterRawInputDevice(pRawInputDevices[i], HID_USAGE_GENERIC_GAMEPAD, GRawInputRegGamepad);
            TryRegisterRawInputDevice(pRawInputDevices[i], HID_USAGE_GENERIC_KEYBOARD, GRawInputRegKeyboard);
            TryRegisterRawInputDevice(pRawInputDevices[i], HID_USAGE_GENERIC_MOUSE, GRawInputRegMouse);
        }
    }

    if (copy) {
        delete[] copy;
    }
    return success;
}

UINT WINAPI GetRegisteredRawInputDevices_Hook(PRAWINPUTDEVICE pRawInputDevices, PUINT puiNumDevices, UINT cbSize) {
    if (G.ApiDebug) {
        LOG << "GetRegisteredRawInputDevices" << END;
    }

    if (cbSize == sizeof(RAWINPUTDEVICE) && puiNumDevices) {
        UINT keyFlags, mouseFlags;
        HWND keyWindow, mouseWindow;
        bool keyPrivate, mousePrivate;
        GRawInputRegKeyboard.GetRegistered(&keyWindow, &keyFlags, &keyPrivate);
        GRawInputRegMouse.GetRegistered(&mouseWindow, &mouseFlags, &mousePrivate);

        UINT deltaReal = 0;
        if (!keyPrivate) {
            deltaReal++;
        }
        if (!mousePrivate) {
            deltaReal++;
        }

        UINT count = *puiNumDevices;
        UINT realCount = count + deltaReal;
        RAWINPUTDEVICE *copy = (pRawInputDevices && deltaReal) ? new RAWINPUTDEVICE[realCount] : nullptr;
        RAWINPUTDEVICE *realDevices = copy ? copy : pRawInputDevices;

        UINT realResult = GetRegisteredRawInputDevices_Real(realDevices, &realCount, cbSize);
        if (!pRawInputDevices || realResult == INVALID_UINT_VALUE) {
            *puiNumDevices = realCount >= deltaReal ? realCount - deltaReal : 0;
            if (copy) {
                delete[] copy;
            }
            return realResult;
        }

        UINT outI = 0;
        for (UINT i = 0; i < realResult; i++) {
            if (realDevices[i].usUsagePage == HID_USAGE_PAGE_GENERIC && realDevices[i].usUsage == HID_USAGE_GENERIC_KEYBOARD) {
                if (!keyPrivate) {
                    continue;
                }

                realDevices[i].dwFlags = keyFlags;
                realDevices[i].hwndTarget = keyWindow;
            }

            if (realDevices[i].usUsagePage == HID_USAGE_PAGE_GENERIC && realDevices[i].usUsage == HID_USAGE_GENERIC_MOUSE) {
                if (!mousePrivate) {
                    continue;
                }

                realDevices[i].dwFlags = mouseFlags;
                realDevices[i].hwndTarget = mouseWindow;
            }

            if (copy) {
                pRawInputDevices[outI++] = copy[i];
            }
        }

        UINT result = realResult >= deltaReal ? realResult - deltaReal : 0;

        if (copy) {
            ASSERT(outI == result && outI <= count, "broken raw get registered logic");
            delete[] copy;
        }

        return result;
    }

    return GetRegisteredRawInputDevices_Real(pRawInputDevices, puiNumDevices, cbSize);
}

void HookRawInput() {
#ifndef _WIN64
    IsWow64Process(GetCurrentProcess(), &gWow64);
#endif

    ADD_GLOBAL_HOOK(GetRawInputDeviceList);
    ADD_GLOBAL_HOOK(GetRawInputDeviceInfoA);
    ADD_GLOBAL_HOOK(GetRawInputDeviceInfoW);
    ADD_GLOBAL_HOOK(GetRawInputData);
    ADD_GLOBAL_HOOK(GetRawInputBuffer);
    ADD_GLOBAL_HOOK(RegisterRawInputDevices);
    ADD_GLOBAL_HOOK(GetRegisteredRawInputDevices);
}
