#pragma once
#include <Xinput.h>
#include "UtilsBase.h"

#define XINPUT_GAMEPAD_GUIDE 0x400 // private value

DEFINE_GUID(GUID_DEVINTERFACE_XUSB, 0xEC87F1E3L, 0xC13B, 0x4100, 0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB);

#define IOCTL_XINPUT_DEVICE_TYPE 0x8000

#define IOCTL_XINPUT_GET_INFORMATION 0x80006000
#define IOCTL_XINPUT_GET_CAPABILITIES 0x8000e004
#define IOCTL_XINPUT_GET_LED_STATE 0x8000e008
#define IOCTL_XINPUT_GET_GAMEPAD_STATE 0x8000e00c
#define IOCTL_XINPUT_SET_GAMEPAD_STATE 0x8000a010
// (rest is unimportant or for higher protocol versions)

atomic<UINT> gXUsbSequence;
bool gUniqLogXUsbOpen;

static HANDLE XUsbCreateFile(DeviceIntf *device, DWORD flags) {
    UINT seq = ++gXUsbSequence;

    // See also device's FinalPipePrefix
    wchar_t pipeName[MAX_PATH];
    wsprintfW(pipeName, LR"(\\.\Pipe\MyInputHook_%d.%d.xusb.%d)", GetCurrentProcessId(), device->UserIdx, seq);

    HANDLE pipe = CreateNamedPipeW(pipeName, PIPE_ACCESS_INBOUND | (flags & FILE_FLAG_OVERLAPPED),
                                   0, 1, 0, 0, 0, nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        LOG << "failed creating xusb pipe" << END;
    } else {
        if (!gUniqLogXUsbOpen) {
            gUniqLogXUsbOpen = true;
            LOG << "Opening xusb device" << END;
        }
        if (G.ApiDebug) {
            LOG << "Created xusb pipe " << pipe << END;
        }
    }

    return pipe;
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

enum { LatestProtocol = 0x101 }; // there is also 0x100, 0x102

#pragma pack(push, 1)
struct XUsbInformation {
    uint16_t ProtocolVersion;
    uint8_t Count, Unk;
    uint32_t Flags;
    uint16_t VendorID, ProductID;
};

struct XUsbProtocolInput {
    uint16_t ProtocolVersion;
    uint8_t Index;
};

struct XUsbLedState {
    uint16_t ProtocolVersion;
    uint8_t State;
};

struct XUsbGamepadState // differs for protocol 0x100!
{
    uint16_t ProtocolVersion; // guess
    uint8_t Active, Unk, Audio;
    uint32_t PacketNumber;
    uint16_t Unk2;
    XINPUT_GAMEPAD Gamepad;
    uint8_t GamepadExt[6];
};

struct XUsbCapabilities {
    uint16_t ProtocolVersion; // guess
    uint8_t Type, SubType;
    XINPUT_GAMEPAD Gamepad;
    uint8_t GamepadExt[6];
    uint8_t LowMotor, HighMotor;
};

struct XUsbVibration {
    uint8_t Index, Led, LowMotor, HighMotor, Cmd;
};
#pragma pack(pop)

static BOOL XUsbDeviceIoControl(DeviceIntf *device, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned) {
    switch (dwIoControlCode) {
    case IOCTL_XINPUT_GET_INFORMATION:
        return ProcessDeviceIoControlOutput<XUsbInformation>(
            lpOutBuffer, nOutBufferSize, lpBytesReturned, [device](XUsbInformation *info) {
                ZeroMemory(info, sizeof(XUsbInformation));
                info->Count = 1; // could have 1 xusb device manage multiple devices? no real need?
                info->VendorID = device->VendorId;
                info->ProductID = device->ProductId;
                info->ProtocolVersion = LatestProtocol;
                return TRUE;
            });

    case IOCTL_XINPUT_GET_CAPABILITIES:
        // input is XUsbProtocolInput
        return ProcessDeviceIoControlOutput<XUsbCapabilities>(
            lpOutBuffer, nOutBufferSize, lpBytesReturned, [device](XUsbCapabilities *caps) {
                ZeroMemory(caps, sizeof(XUsbCapabilities));
                caps->ProtocolVersion = LatestProtocol;
                caps->Type = XINPUT_DEVTYPE_GAMEPAD;
                caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
                memset(&caps->Gamepad, 0xff, sizeof(caps->Gamepad));
                caps->LowMotor = caps->HighMotor = 0xff;
                return TRUE;
            });

    case IOCTL_XINPUT_GET_LED_STATE:
        // input is XUsbProtocolInput
        return ProcessDeviceIoControlOutput<XUsbLedState>(
            lpOutBuffer, nOutBufferSize, lpBytesReturned, [device](XUsbLedState *state) {
                ZeroMemory(state, sizeof(XUsbLedState));
                return TRUE;
            });

    case IOCTL_XINPUT_GET_GAMEPAD_STATE:
        // input is XUsbProtocolInput
        return ProcessDeviceIoControlOutput<XUsbGamepadState>(
            lpOutBuffer, nOutBufferSize, lpBytesReturned, [device](XUsbGamepadState *xusb) {
                auto &state = G.Users[device->UserIdx].State;
                lock_guard<mutex> lock(state.Mutex);

                ZeroMemory(xusb, sizeof(XUsbGamepadState));
                xusb->ProtocolVersion = LatestProtocol;
                xusb->Active = true;
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
                return TRUE;
            });

    case IOCTL_XINPUT_SET_GAMEPAD_STATE:
        return ProcessDeviceIoControlInput<XUsbVibration>(lpInBuffer, nInBufferSize, [device](XUsbVibration *ptr) {
            switch (ptr->Cmd) {
            // case 1: led
            case 2:
                SetRumble((double)ptr->LowMotor / 0xff, (double)ptr->HighMotor / 0xff);
                break;
            }
            return TRUE;
        });

    default:
        LOG << "Unknown xusb device io control: " << dwIoControlCode << END;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
}
