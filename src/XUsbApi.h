#pragma once
#include <Xinput.h>
#include "UtilsBase.h"
#include "Device.h"

#define XINPUT_GAMEPAD_GUIDE 0x400 // private value

#define IOCTL_XUSB_DEVICE_TYPE 0x8000

#define IOCTL_XUSB_GET_INFORMATION 0x80006000
#define IOCTL_XUSB_GET_CAPABILITIES 0x8000e004
#define IOCTL_XUSB_GET_LED_STATE 0x8000e008
#define IOCTL_XUSB_GET_GAMEPAD_STATE 0x8000e00c
#define IOCTL_XUSB_GET_BATTERY_INFO 0x8000e018
#define IOCTL_XUSB_GET_AUDIO_DEV_INFO 0x8000e020
#define IOCTL_XUSB_GET_GAMEPAD_STATE_ASYNC 0x8000e3ac
#define IOCTL_XUSB_SET_GAMEPAD_STATE 0x8000a010

atomic<UINT> gXUsbSequence;
UniqueLog gUniqLogXUsbOpen;
UniqueLog gUniqLogXUsbAsync;

static HANDLE XUsbCreateFile(DeviceIntf *device, DWORD flags) {
    while (true) {
        UINT seq = ++gXUsbSequence;

        // See also device's FinalPipePrefix and GetDeviceNodeByHandle
        wchar_t pipeName[MAX_PATH];
        wsprintfW(pipeName, LR"(\\.\Pipe\MyInputHook_%d.%d.xusb.%d)", GetCurrentProcessId(), device->UserIdx, seq);

        HANDLE pipe = CreateNamedPipeW(pipeName, PIPE_ACCESS_INBOUND | (flags & FILE_FLAG_OVERLAPPED),
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
                                       1, 0, 0, 0, nullptr); // 0 buffer for "immediate" mode

        if (pipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) {
                LOG << "xusb pipe name already taken, retrying..." << END;
                continue;
            }

            LOG_ERR << "failed creating xusb pipe" << END;
        } else {
            if (gUniqLogXUsbOpen) {
                LOG << "Opening xusb device" << END;
            }
            if (G.ApiDebug) {
                LOG << "Created xusb pipe " << pipe << END;
            }
        }

        return pipe;
    }
}

template <class TData, class TCopy>
static BOOL ProcessDeviceIoControlInput(LPVOID lpInBuffer, DWORD nInBufferSize, TCopy copyFunc, UINT expectedSize = sizeof(TData)) {
    if (!lpInBuffer || nInBufferSize < expectedSize) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    } else {
        return copyFunc((TData *)lpInBuffer);
    }
}

template <class TData, class TCopy>
static BOOL ProcessDeviceIoControlOutput(LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, TCopy copyFunc, UINT expectedSize = sizeof(TData)) {
    if (!lpOutBuffer || nOutBufferSize < expectedSize) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    } else {
        BOOL status = copyFunc((TData *)lpOutBuffer);
        if (status) {
            *lpBytesReturned = expectedSize;
        }
        return status;
    }
}

static BOOL ProcessDeviceIoControlOutputString(const wchar_t *string, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned) {
    DWORD strSize = (DWORD)(wcslen(string) + 1) * sizeof(wchar_t);
    return ProcessDeviceIoControlOutput<wchar_t>(
        lpOutBuffer, nOutBufferSize, lpBytesReturned, [string, strSize](wchar_t *dest) {
            CopyMemory(dest, string, strSize);
            return TRUE;
        },
        strSize);
}

enum class XUsbVersion : uint16_t {
    None = 0,

    // there is also V0, not supported
    V1 = 0x101,
    V2 = 0x102,
    // there is also V3, not supported yet

    Default = V2,
};

#pragma pack(push, 1)

static_assert(sizeof(XINPUT_GAMEPAD) == 12);

struct XUsbInformation {
    XUsbVersion SupportedVersion;
    uint8_t Count, Unk;
    uint32_t Flags;
    uint16_t VendorID, ProductID;
};

struct XUsbInput {
    XUsbVersion Version;
    uint8_t Index;
};

struct XUsbDeviceInput {
    XUsbVersion Version;
    uint8_t Index;
    uint8_t DevType;
};

struct XUsbBatteryInfo {
    XUsbVersion Version; // guess
    BYTE BatteryType, BatteryLevel;
};

struct XUsbAudioDevInfo {
    XUsbVersion Version; // guess
    uint16_t VendorID, ProductID;
    uint8_t AudioIndex;
};

enum { XUSB_AUDIO_INDEX_NONE = 0xff };

struct XUsbLedState {
    XUsbVersion Version; // guess
    uint8_t State;
};

struct XUsbGamepadState {
    XUsbVersion Version; // guess
    uint8_t Active, Unk, AudioIndex;
    uint32_t PacketNumber;
    uint8_t Unk2, Unk3;
    XINPUT_GAMEPAD Gamepad;
    uint8_t GamepadExt[6];
};

struct XUsbCapabilities {
    XUsbVersion Version; // guess
    uint8_t Type, SubType;
    XINPUT_GAMEPAD Gamepad;
    uint8_t GamepadExt[6];
    uint8_t LowMotor, HighMotor;
};

struct XUsbCapabilities2 {
    XUsbVersion Version; // guess
    uint8_t Type, SubType;
    uint16_t Flags;
    uint16_t VendorID, ProductID; // part of Ex capabilities, not sure what device it's about
    uint16_t Unk1;                // part of Ex capabilities, unknown
    uint32_t Unk2;                // part of Ex capabilities, unknown
    XINPUT_GAMEPAD Gamepad;
    uint8_t GamepadExt[6];
    uint8_t LowMotor, HighMotor;
};

struct XUsbVibration {
    uint8_t Index, Led, LowMotor, HighMotor, Cmd;
};
#pragma pack(pop)

template <class TData, class TCopy> // TInput must have Index & Version e.g. like XUsbInput
static BOOL ProcessDeviceIoControlXUsbInput(LPVOID lpInBuffer, DWORD nInBufferSize, TCopy copyFunc, UINT expectedSize = sizeof(TData)) {
    return ProcessDeviceIoControlInput<TData>(lpInBuffer, nInBufferSize, [&](TData *input) {
        if (input->Index != 0) // we only support & report 1 device per xusb node (hopefully fine...)
        {
            SetLastError(ERROR_INVALID_PARAMETER); // ???
            return FALSE;
        }

        return copyFunc(input, input->Version);
    });
}

BOOL OnXUsbVersionError(XUsbVersion version, const char *funcDesc) {
    LOG_ERR << "Unknown xusb device " << funcDesc << " version: " << (int)version << END;
    SetLastError(ERROR_INVALID_PARAMETER); // ???
    return FALSE;
}

void ReadXUsbState(XUsbGamepadState *xusb, const ImplState &state, XUsbVersion version) {
    lock_guard<mutex> lock(state.Mutex);
    ZeroMemory(xusb, sizeof(XUsbGamepadState));
    xusb->Version = version;
    xusb->Active = true;
    xusb->AudioIndex = XUSB_AUDIO_INDEX_NONE;
    xusb->Unk3 = 0xff; // relied upon by wgi (what is it?)
    xusb->PacketNumber = state.Version;
    xusb->Gamepad.wButtons |= state.A.State ? XINPUT_GAMEPAD_A : 0;
    xusb->Gamepad.wButtons |= state.B.State ? XINPUT_GAMEPAD_B : 0;
    xusb->Gamepad.wButtons |= state.X.State ? XINPUT_GAMEPAD_X : 0;
    xusb->Gamepad.wButtons |= state.Y.State ? XINPUT_GAMEPAD_Y : 0;
    xusb->Gamepad.wButtons |= state.Start.State ? XINPUT_GAMEPAD_START : 0;
    xusb->Gamepad.wButtons |= state.Back.State ? XINPUT_GAMEPAD_BACK : 0;
    xusb->Gamepad.wButtons |= state.Guide.State ? XINPUT_GAMEPAD_GUIDE : 0;
    xusb->Gamepad.wButtons |= state.DU.State ? XINPUT_GAMEPAD_DPAD_UP : 0;
    xusb->Gamepad.wButtons |= state.DD.State ? XINPUT_GAMEPAD_DPAD_DOWN : 0;
    xusb->Gamepad.wButtons |= state.DL.State ? XINPUT_GAMEPAD_DPAD_LEFT : 0;
    xusb->Gamepad.wButtons |= state.DR.State ? XINPUT_GAMEPAD_DPAD_RIGHT : 0;
    xusb->Gamepad.wButtons |= state.LB.State ? XINPUT_GAMEPAD_LEFT_SHOULDER : 0;
    xusb->Gamepad.wButtons |= state.RB.State ? XINPUT_GAMEPAD_RIGHT_SHOULDER : 0;
    xusb->Gamepad.wButtons |= state.L.State ? XINPUT_GAMEPAD_LEFT_THUMB : 0;
    xusb->Gamepad.wButtons |= state.R.State ? XINPUT_GAMEPAD_RIGHT_THUMB : 0;
    xusb->Gamepad.bLeftTrigger = state.LT.Value8();
    xusb->Gamepad.bRightTrigger = state.RT.Value8();
    xusb->Gamepad.sThumbLX = state.LA.X.Value16();
    xusb->Gamepad.sThumbLY = state.LA.Y.Value16();
    xusb->Gamepad.sThumbRX = state.RA.X.Value16();
    xusb->Gamepad.sThumbRY = state.RA.Y.Value16();
}

// Note: assuming version will not change in future async reads...
static bool XUsbSetupAsyncRead(const wchar_t *finalPath, DeviceIntf *device, XUsbVersion version) {
    if (gUniqLogXUsbAsync) {
        LOG << "Using async xusb api (from wgi?)" << END;
    }

    for (int i = 0; i < 3; i++) {
        finalPath = wcschr(finalPath, L'\\');
        finalPath = finalPath ? finalPath + 1 : L"";
    }

    Path pipePath = PathCombine(L"\\\\.\\Pipe\\", finalPath);

    HANDLE client = CreateFileW_Real(pipePath, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (client == INVALID_HANDLE_VALUE) {
        LOG_ERR << "failed creating xusb client" << END;
        return false;
    }

    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    overlapped.hEvent = CreateEventW(nullptr, false, false, nullptr);
    bool inWrite = false;

    G.Users[device->UserIdx].Callbacks.Add([client, overlapped, version,
                                            xusbState = XUsbGamepadState{}, inWrite = false](ImplUser *user) mutable {
        auto onWriteEnd = [&](BOOL status) {
            inWrite = false;
            if (!status) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    inWrite = true;
                }

                if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
                    CloseHandle(overlapped.hEvent);
                    return false;
                }
            }
            return true;
        };

        DWORD written;
        if (inWrite) {
            if (!onWriteEnd(GetOverlappedResult(client, &overlapped, &written, false))) {
                return false;
            }

            if (inWrite) {
                CancelIoEx(client, &overlapped);
            }
        }

        ReadXUsbState(&xusbState, user->State, version);
        return onWriteEnd(WriteFile(client, &xusbState, sizeof(XUsbGamepadState), &written, &overlapped));
    });
    return true;
}

static BOOL XUsbDeviceIoControl(const wchar_t *finalPath, DeviceIntf *device, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned,
                                HANDLE hDevice, LPOVERLAPPED lpAsyncOverlapped, bool *refAsync) {
    switch (dwIoControlCode) {
    case IOCTL_XUSB_GET_INFORMATION:
        return ProcessDeviceIoControlOutput<XUsbInformation>(
            lpOutBuffer, nOutBufferSize, lpBytesReturned, [&](XUsbInformation *info) {
                ZeroMemory(info, sizeof(XUsbInformation));
                info->Count = 1; // could have 1 xusb device manage multiple devices? no real need?
                info->VendorID = device->VendorId;
                info->ProductID = device->ProductId;
                info->SupportedVersion = XUsbVersion::Default;
                return TRUE;
            });

    case IOCTL_XUSB_GET_CAPABILITIES:
        return ProcessDeviceIoControlXUsbInput<XUsbInput>(lpInBuffer, nInBufferSize, [&](XUsbInput *, XUsbVersion version) {
            switch (version) {
            case XUsbVersion::V1:
                return ProcessDeviceIoControlOutput<XUsbCapabilities>(
                    lpOutBuffer, nOutBufferSize, lpBytesReturned, [&](XUsbCapabilities *caps) {
                        ZeroMemory(caps, sizeof(XUsbCapabilities));
                        caps->Version = version;
                        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
                        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
                        memset(&caps->Gamepad, 0xff, sizeof(caps->Gamepad));
                        caps->LowMotor = caps->HighMotor = 0xff;
                        return TRUE;
                    });

            case XUsbVersion::V2:
                return ProcessDeviceIoControlOutput<XUsbCapabilities2>(
                    lpOutBuffer, nOutBufferSize, lpBytesReturned, [&](XUsbCapabilities2 *caps) {
                        ZeroMemory(caps, sizeof(XUsbCapabilities2));
                        caps->Version = version;
                        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
                        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
                        caps->Flags = XINPUT_CAPS_VOICE_SUPPORTED | XINPUT_CAPS_PMD_SUPPORTED; // ???
                        caps->VendorID = device->VendorId;
                        caps->ProductID = device->ProductId;
                        memset(&caps->Gamepad, 0xff, sizeof(caps->Gamepad));
                        caps->LowMotor = caps->HighMotor = 0xff;
                        return TRUE;
                    });
            }
            return OnXUsbVersionError(version, "get caps");
        });

    case IOCTL_XUSB_GET_BATTERY_INFO:
        return ProcessDeviceIoControlXUsbInput<XUsbDeviceInput>(lpInBuffer, nInBufferSize, [&](XUsbDeviceInput *, XUsbVersion version) {
            switch (version) {
            case XUsbVersion::V2:
                return ProcessDeviceIoControlOutput<XUsbBatteryInfo>(
                    lpOutBuffer, nOutBufferSize, lpBytesReturned, [&](XUsbBatteryInfo *state) {
                        ZeroMemory(state, sizeof(XUsbBatteryInfo));
                        state->Version = version;
                        state->BatteryType = BATTERY_TYPE_WIRED;
                        state->BatteryLevel = BATTERY_LEVEL_FULL;
                        return TRUE;
                    });
            }
            return OnXUsbVersionError(version, "get battery");
        });

    case IOCTL_XUSB_GET_AUDIO_DEV_INFO:
        return ProcessDeviceIoControlXUsbInput<XUsbInput>(lpInBuffer, nInBufferSize, [&](XUsbInput *, XUsbVersion version) {
            switch (version) {
            case XUsbVersion::V2:
                return ProcessDeviceIoControlOutput<XUsbAudioDevInfo>(
                    lpOutBuffer, nOutBufferSize, lpBytesReturned, [&](XUsbAudioDevInfo *state) {
                        ZeroMemory(state, sizeof(XUsbAudioDevInfo));
                        state->Version = version;
                        state->VendorID = device->VendorId;
                        state->ProductID = device->ProductId;
                        state->AudioIndex = XUSB_AUDIO_INDEX_NONE;
                        return TRUE;
                    });
            }
            return OnXUsbVersionError(version, "get audio");
        });

    case IOCTL_XUSB_GET_LED_STATE:
        return ProcessDeviceIoControlXUsbInput<XUsbInput>(lpInBuffer, nInBufferSize, [&](XUsbInput *, XUsbVersion version) {
            switch (version) {
            case XUsbVersion::V1:
                return ProcessDeviceIoControlOutput<XUsbLedState>(
                    lpOutBuffer, nOutBufferSize, lpBytesReturned, [&](XUsbLedState *state) {
                        ZeroMemory(state, sizeof(XUsbLedState));
                        state->Version = version;
                        return TRUE;
                    });
            }
            return OnXUsbVersionError(version, "get led");
        });

    case IOCTL_XUSB_GET_GAMEPAD_STATE:
        return ProcessDeviceIoControlXUsbInput<XUsbInput>(lpInBuffer, nInBufferSize, [&](XUsbInput *, XUsbVersion version) {
            switch (version) {
            case XUsbVersion::V1:
            case XUsbVersion::V2: // not observed, but adding just in case
                return ProcessDeviceIoControlOutput<XUsbGamepadState>(
                    lpOutBuffer, nOutBufferSize, lpBytesReturned, [&](XUsbGamepadState *xusb) {
                        auto &state = G.Users[device->UserIdx].State;
                        ReadXUsbState(xusb, state, version);
                        return TRUE;
                    });
            }
            return OnXUsbVersionError(version, "get state");
        });

    case IOCTL_XUSB_GET_GAMEPAD_STATE_ASYNC:
        // (input->DevType is 2 or 3, meaning unknown [I'm just reusing XUsbDeviceInput for now])
        return ProcessDeviceIoControlXUsbInput<XUsbDeviceInput>(lpInBuffer, nInBufferSize, [&](XUsbDeviceInput *, XUsbVersion version) {
            switch (version) {
            case XUsbVersion::V2:
                *refAsync = true;
                if (ReadFile(hDevice, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpAsyncOverlapped)) {
                    return TRUE;
                }

                DWORD err = GetLastError();
                if ((err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_LISTENING) &&
                    XUsbSetupAsyncRead(finalPath, device, version)) {
                    if (ReadFile(hDevice, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpAsyncOverlapped)) {
                        return TRUE;
                    }
                }
                return FALSE;
            }
            return OnXUsbVersionError(version, "get state async");
        });

    case IOCTL_XUSB_SET_GAMEPAD_STATE:
        return ProcessDeviceIoControlInput<XUsbVibration>(lpInBuffer, nInBufferSize, [&](XUsbVibration *ptr) {
            // NOTE: more info (inc. version) is in XUsbVibration (as used by wgi)
            auto user = &G.Users[device->UserIdx];
            switch (ptr->Cmd) {
            // case 1: led
            case 2:
                ImplSetRumble(user, (double)ptr->LowMotor / 0xff, (double)ptr->HighMotor / 0xff);
                break;
            }
            return TRUE;
        });

    default:
        LOG_ERR << "Unknown xusb device io control: " << dwIoControlCode << END;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
}
