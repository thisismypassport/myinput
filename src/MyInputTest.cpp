#include "UtilsPath.h"
#include "UtilsStr.h"
#include "UtilsUiBase.h"
#include "WinUtils.h"
#include "Keys.h"
#include "MyInputHook.h"
#include "Inject.h"
#include "ComUtils.h"
#include "TestUtils.h"
#include "UiTest.h"

#include <Windows.h>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>
#include <hidclass.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <Shlwapi.h>
#include <Dbt.h>
#include <Xinput.h>
#include <WbemIdl.h>
#include <roapi.h>
#include <wrl.h>
#include "windows.gaming.input.h"

namespace WF = ABI::Windows::Foundation;
namespace WFC = ABI::Windows::Foundation::Collections;
namespace WS = ABI::Windows::System;
namespace WGI = ABI::Windows::Gaming::Input;
namespace WGIF = ABI::Windows::Gaming::Input::ForceFeedback;
namespace WRL = Microsoft::WRL;
namespace WRLW = Microsoft::WRL::Wrappers;

bool gPrintGamepad;
bool gPrintKeyboard;
bool gPrintMouse;
bool gPrintMouseCursor;
bool gPrintTime;
bool gPrintDeviceChange;
bool gStressDevice;
bool gStressDeviceCommunicate;
bool gRemarshalWmi;
bool gRegRawActive;

WeakAtomic<HWND> gNotifyWin;
WeakAtomic<HWND> gNotifyWinA;
HANDLE gSubProcess;
WNDPROC gTestWindowProcBase;
HANDLE gTestWindow;
BOOL gWow64;

struct LatencyMeasure {
    enum { MaxPrev = 0x80 };
    const char *Name;
    double SumPrev = 0.0;
    int NumPrev = 0;
    uint64_t LastTime = 0;
} gKeyLatency{"key"}, gMouseLatency{"mouse"};

#ifdef _WIN64 // some things are broken/questionable in wow64 mode (in windows, not in myinput)
#define WOW64_BROKEN(x) x
#else
#define WOW64_BROKEN(x)
#endif

// #define CMEX_ENABLE

#ifdef CMEX_ENABLE
#define CMEX(func, ...) func##_Ex(__VA_ARGS__, nullptr)
#define CMEXAW(func, aw, ...) func##_Ex##aw(__VA_ARGS__, nullptr)
#else
#define CMEX(func, ...) func(__VA_ARGS__)
#define CMEXAW(func, aw, ...) func##aw(__VA_ARGS__)
#endif

DWORD CALLBACK DeviceChangeCMCb(HCMNOTIFICATION notify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD size);

class RawDeviceReader {
    struct Input {
        ULONG Prev = 0;
        bool IsValue = false;
        bool Seen = false;
        string Name;
    };

    int mIdx;
    PHIDP_PREPARSED_DATA mPreparsed;
    const char *mSource;
    vector<Input> mInputs;

public:
    RawDeviceReader(int idx, byte *preparsed, const char *source) : mIdx(idx), mPreparsed((PHIDP_PREPARSED_DATA)preparsed), mSource(source) {
        HIDP_CAPS caps;
        AssertEquals("raw.caps", HidP_GetCaps(mPreparsed, &caps), HIDP_STATUS_SUCCESS);

        mInputs.resize(caps.NumberInputDataIndices);

        USHORT nbcaps = caps.NumberInputButtonCaps;
        HIDP_BUTTON_CAPS *bcaps = new HIDP_BUTTON_CAPS[nbcaps];
        AssertEquals("raw.bcaps", HidP_GetButtonCaps(HidP_Input, bcaps, &nbcaps, mPreparsed), HIDP_STATUS_SUCCESS);

        for (int i = 0; i < nbcaps; i++) {
            for (int j = bcaps[i].Range.DataIndexMin; j <= bcaps[i].Range.DataIndexMax; j++) {
                if (bcaps[i].UsagePage == HID_USAGE_PAGE_BUTTON) {
                    mInputs[j].Name = "b" + StrFromValue<char>(bcaps[i].Range.UsageMin + j - bcaps[i].Range.DataIndexMin);
                }
            }
        }

        delete[] bcaps;

        USHORT nvcaps = caps.NumberInputValueCaps;
        HIDP_VALUE_CAPS *vcaps = new HIDP_VALUE_CAPS[nvcaps];
        AssertEquals("raw.vcaps", HidP_GetValueCaps(HidP_Input, vcaps, &nvcaps, mPreparsed), HIDP_STATUS_SUCCESS);

        for (int i = 0; i < nvcaps; i++) {
            for (int j = vcaps[i].Range.DataIndexMin; j <= vcaps[i].Range.DataIndexMax; j++) {
                mInputs[j].IsValue = true;

                if (vcaps[i].UsagePage == HID_USAGE_PAGE_GENERIC) {
                    switch (vcaps[i].Range.UsageMin + j - vcaps[i].Range.DataIndexMin) {
                    case HID_USAGE_GENERIC_X:
                        mInputs[j].Name = "lx";
                        mInputs[j].Prev = 1 << (vcaps[i].BitSize - 1);
                        break;
                    case HID_USAGE_GENERIC_Y:
                        mInputs[j].Name = "ly";
                        mInputs[j].Prev = 1 << (vcaps[i].BitSize - 1);
                        break;
                    case HID_USAGE_GENERIC_RX:
                        mInputs[j].Name = "rx";
                        mInputs[j].Prev = 1 << (vcaps[i].BitSize - 1);
                        break;
                    case HID_USAGE_GENERIC_RY:
                        mInputs[j].Name = "ry";
                        mInputs[j].Prev = 1 << (vcaps[i].BitSize - 1);
                        break;
                    case HID_USAGE_GENERIC_Z:
                        mInputs[j].Name = "lz";
                        break;
                    case HID_USAGE_GENERIC_RZ:
                        mInputs[j].Name = "rz";
                        break;
                    case HID_USAGE_GENERIC_HATSWITCH:
                        mInputs[j].Name = "d";
                        mInputs[j].Prev = vcaps[i].LogicalMin ? 0 : vcaps[i].LogicalMax;
                        break;
                    }
                }
            }
        }

        delete[] vcaps;

        for (size_t i = 0; i < mInputs.size(); i++) {
            if (mInputs[i].Name.empty()) {
                mInputs[i].Name = "#" + StrFromValue<char>(i);
            }
        }
    }

    void Read(void *buffer, DWORD length) {
        HIDP_DATA datas[0x100];
        ULONG ndatas = 0x100;
        ZeroMemory(datas, sizeof(datas)); // allows using RawValue
        AssertEquals("raw.read", HidP_GetData(HidP_Input, datas, &ndatas, mPreparsed, (char *)buffer, length), HIDP_STATUS_SUCCESS);

        if (gPrintGamepad) {
            for (ULONG i = 0; i < ndatas; i++) {
                auto &input = mInputs[datas[i].DataIndex];

                if (input.Prev != datas[i].RawValue) {
                    printf("pad.%d %s -> %ld : %s\n", mIdx, input.Name.c_str(), datas[i].RawValue, mSource);
                    input.Prev = datas[i].RawValue;
                }

                input.Seen = true;
            }

            // handle buttons (so complicated...)
            for (auto &input : mInputs) {
                if (!input.IsValue && !input.Seen && input.Prev) {
                    printf("pad.%d %s -> 0 : %s\n", mIdx, input.Name.c_str(), mSource);
                    input.Prev = 0;
                }

                input.Seen = false;
            }
        }
    }
};

void StressDevice(const wchar_t *path) {
    static bool started = false;
    if (!started) {
        wstring pathCopy = path;
        CreateThread([pathCopy] {
            uint64_t prev = GetPerfCounter();
            int i = 0;
            while (true) {
                HANDLE handle = CreateFileW(pathCopy.c_str(), GENERIC_READ | GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr);
                if (gStressDeviceCommunicate) {
                    ULONG value = 0;
                    DeviceIoControl(handle, IOCTL_HID_SET_POLL_FREQUENCY_MSEC, &value, sizeof(ULONG), nullptr, 0, nullptr, nullptr);
                    byte data[0x1000];
                    DWORD dsize;
                    ReadFile(handle, data, sizeof data, &dsize, nullptr);
                }
                CloseHandle(handle);

                if (i++ == 0x10000) {
                    printf("stress dev create : %lf\n", GetPerfDelay(prev, &prev));
                    i = 0;
                }
            }
        });
        started = true;
    }
}

void ReadDevice(int idx, const wchar_t *path, byte *preparsed, bool immediate) {
    if (gStressDevice) {
        StressDevice(path);
    }

    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
    AssertNotEquals("dev.open", (intptr_t)file, (intptr_t)INVALID_HANDLE_VALUE);

    wchar_t istr[0x4000] = {};
    AssertEquals("dev.gistr", HidD_GetIndexedString(file, 1, istr, sizeof(istr)), TRUE);

    char tbuff[0x2000] = {};
    tbuff[0] = -1;
    AssertEquals("dev.gfeat", HidD_GetFeature(file, tbuff, sizeof(tbuff)), FALSE);
    AssertEquals("dev.sfeat", HidD_SetFeature(file, tbuff, sizeof(tbuff)), FALSE);
    AssertEquals("dev.sout", HidD_SetOutputReport(file, tbuff, sizeof(tbuff)), FALSE);
    AssertEquals("dev.gphy", HidD_GetPhysicalDescriptor(file, tbuff, sizeof(tbuff)), FALSE);
    AssertEquals("dev.gin", HidD_GetInputReport(file, tbuff, sizeof(tbuff)), TRUE);

    AssertEquals("dev.sibuf", HidD_SetNumInputBuffers(file, 4), TRUE);
    ULONG value = 0;
    AssertEquals("dev.gibuf", HidD_GetNumInputBuffers(file, &value), TRUE);
    AssertEquals("dev.ibuf", value, 4);

    ULONG usize, oldValue = 0;
    AssertEquals("dev.gfreq", DeviceIoControl(file, IOCTL_HID_GET_POLL_FREQUENCY_MSEC, nullptr, 0, &oldValue, sizeof(ULONG), &usize, nullptr), TRUE);
    value = 0;
    AssertEquals("dev.sfreq", DeviceIoControl(file, IOCTL_HID_SET_POLL_FREQUENCY_MSEC, &value, sizeof(ULONG), nullptr, 0, nullptr, nullptr), TRUE);
    value = 1;
    AssertEquals("dev.gfreq", DeviceIoControl(file, IOCTL_HID_GET_POLL_FREQUENCY_MSEC, nullptr, 0, &value, sizeof(ULONG), &usize, nullptr), TRUE);
    AssertEquals("dev.freq", value, 0);
    if (!immediate) {
        AssertEquals("dev.sfreq", DeviceIoControl(file, IOCTL_HID_SET_POLL_FREQUENCY_MSEC, &oldValue, sizeof(ULONG), nullptr, 0, nullptr, nullptr), TRUE);
    }

    AssertEquals("dev.flush", HidD_FlushQueue(file), TRUE);

    CreateThread([idx, file, preparsed, immediate] {
#if ZERO
        // breaks things...
        char buff1[0xe];
        OVERLAPPED ovrl1;
        ZeroMemory(&ovrl1, sizeof(ovrl1));
        BOOL result = ReadFile(file, buff1, sizeof(buff1), nullptr, &ovrl1);

        DWORD length1;
        result = GetOverlappedResult(file, &ovrl1, &length1, true);
#endif

        DWORD stdLength = -1;
        RawDeviceReader reader(idx, preparsed, "device");
        while (true) {
            char buff[0x2000];
            OVERLAPPED ovrl;
            ZeroMemory(&ovrl, sizeof(ovrl));
            ReadFile(file, buff, sizeof(buff), nullptr, &ovrl);

            DWORD length;
            GetOverlappedResult(file, &ovrl, &length, true);

            if (stdLength == -1)
                stdLength = length;
            else
                AssertEquals("dev.read.len", length, stdLength);

            reader.Read(buff, length);

            if (immediate)
                Sleep(200);
        }
    });
}

void TestDevice(const char *nameA, const wchar_t *nameW, bool ours, int userIdx, bool read = false, bool readImmediate = false) {
    int junk = 10;
    for (int type = 0; type < 2; type++) {
        HANDLE syncFile = type == 0 ? CreateFileA(nameA, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr) : CreateFileW(nameW, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr);

        if (ours) {
            AssertNotEquals("dev.open.sync", (intptr_t)syncFile, (intptr_t)INVALID_HANDLE_VALUE);
        }

        byte *desc = nullptr;
        if (syncFile != INVALID_HANDLE_VALUE) {
            // collection
            HID_COLLECTION_INFORMATION coll;
            DWORD collSize = ~0;
            AssertEquals("dev.io.coll.bad", DeviceIoControl(syncFile, IOCTL_HID_GET_COLLECTION_INFORMATION, nullptr, 0, &coll, 1, &collSize, nullptr), FALSE);
            AssertEquals("dev.io.coll.errno", GetLastError(), ERROR_INVALID_USER_BUFFER);
            AssertEquals("dev.io.coll.size", collSize, (DWORD)~0);

            AssertEquals("dev.io.coll", DeviceIoControl(syncFile, IOCTL_HID_GET_COLLECTION_INFORMATION, nullptr, 0, &coll, sizeof(coll) + 1, &collSize, nullptr), TRUE);
            AssertEquals("dev.io.coll.size", collSize, sizeof(coll));

            // product string
            DWORD prodSize = ~0;
            AssertEquals("dev.io.pstr.bad", DeviceIoControl(syncFile, IOCTL_HID_GET_PRODUCT_STRING, nullptr, 0, nullptr, 0, &prodSize, nullptr), FALSE);
            AssertEquals("dev.io.pstr.errno", GetLastError(), ERROR_INVALID_USER_BUFFER);
            AssertEquals("dev.io.pstr.size", prodSize, (DWORD)~0);

            DWORD prodLen = 0x200;
            wchar_t *prodStr = new wchar_t[prodLen];
            AssertEquals("dev.io.pstr", DeviceIoControl(syncFile, IOCTL_HID_GET_PRODUCT_STRING, nullptr, 0, prodStr, prodLen * sizeof(wchar_t), &prodSize, nullptr), TRUE);
            AssertEquals("dev.io.pstr.size", prodSize, (wcslen(prodStr) + 1) * sizeof(wchar_t));

            // descriptor
            if (ours) {
                AssertNotEquals("dev.io.desc.has", coll.DescriptorSize, 0);
            }
            if (coll.DescriptorSize) {
                desc = new byte[coll.DescriptorSize + junk];
                DWORD descSize = ~0;
                AssertEquals("dev.io.desc.bad", DeviceIoControl(syncFile, IOCTL_HID_GET_COLLECTION_DESCRIPTOR, nullptr, 0, desc, 1, &descSize, nullptr), FALSE);
                AssertEquals("dev.io.desc.errno", GetLastError(), ERROR_INVALID_USER_BUFFER);
                AssertEquals("dev.io.desc.size", descSize, (DWORD)~0);

                AssertEquals("dev.io.desc", DeviceIoControl(syncFile, IOCTL_HID_GET_COLLECTION_DESCRIPTOR, nullptr, 0, desc, coll.DescriptorSize + junk, &descSize, nullptr), TRUE);
                AssertEquals("dev.io.desc.size", descSize, coll.DescriptorSize);
            }

            // feature
            if (ours) {
                DWORD featSize = 0;
                AssertEquals("dev.io.feat.bad", DeviceIoControl(syncFile, IOCTL_HID_GET_FEATURE, nullptr, 0, nullptr, 0, &featSize, nullptr), FALSE);
                AssertEquals("dev.io.feat.errno", GetLastError(), ERROR_INVALID_FUNCTION);
            }

            CloseHandle(syncFile);
        }

        if (ours && desc && read && type == 1) {
            ReadDevice(userIdx, nameW, desc, readImmediate);
        }
    }
}

struct RawInputData {
    HANDLE device;
    byte *preparsed;
    RawDeviceReader *reader;
};

using RawReadCb = function<void(RAWINPUT *)>;
vector<RawReadCb> gActiveRawReadCbs;

template <class TReadCb>
void ProcessRawInputMsg(TReadCb readCallback, const MSG &msg, bool readBuffer) {
    if (msg.message == WM_INPUT_DEVICE_CHANGE) {
        AssertEquals("ri.chg.w", msg.wParam == GIDC_ARRIVAL || msg.wParam == GIDC_REMOVAL, true);

        if (msg.wParam == GIDC_ARRIVAL) {
            RID_DEVICE_INFO info;
            UINT infosize = sizeof(info);
            AssertEquals("ri.chg.l", GetRawInputDeviceInfoW((HANDLE)msg.lParam, RIDI_DEVICEINFO, &info, &infosize), infosize);

            if (gPrintDeviceChange) {
                printf("event : raw device add: %p\n", (HANDLE)msg.lParam);
            }
        } else {
            if (gPrintDeviceChange) {
                printf("event : raw device remove: %p\n", (HANDLE)msg.lParam);
            }
        }
    }

    if (msg.message == WM_INPUT) {
        UINT size = 0;
        AssertEquals("ri.d.size", GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)), 0);

        const int junk = 10;
        byte *buffer = new byte[size + junk];

        UINT badSize = 1;
        AssertEquals("ri.d.bad", GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, buffer, &badSize, sizeof(RAWINPUTHEADER)), (UINT)-1);
        AssertEquals("ri.d.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
        WOW64_BROKEN(AssertEquals)
        ("ri.d.bad.size", badSize, size);

        UINT readSize = size + junk;
        AssertEquals("ri.d", GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, buffer, &readSize, sizeof(RAWINPUTHEADER)), size);
        AssertEquals("ri.d.size", readSize, size + junk);

        if (!readBuffer) {
            readCallback((RAWINPUT *)buffer);
        }

        delete[] buffer;

        UINT headSize = 0;
        AssertEquals("ri.d.head.size", GetRawInputData((HRAWINPUT)msg.lParam, RID_HEADER, nullptr, &headSize, sizeof(RAWINPUTHEADER)), 0);
        AssertEquals("ri.d.head.eq", headSize, sizeof(RAWINPUTHEADER));

        RAWINPUTHEADER header[2];
        badSize = 1;
        WOW64_BROKEN(AssertEquals)
        ("ri.d.head.bad", GetRawInputData((HRAWINPUT)msg.lParam, RID_HEADER, &header, &badSize, sizeof(RAWINPUTHEADER)), (UINT)-1);
        AssertEquals("ri.d.head.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
        AssertEquals("ri.d.bad.size", badSize, headSize);

        readSize = headSize * 2;
        AssertEquals("ri.d", GetRawInputData((HRAWINPUT)msg.lParam, RID_HEADER, &header, &readSize, sizeof(RAWINPUTHEADER)), headSize);
        WOW64_BROKEN(AssertEquals)
        ("ri.d.size", readSize, headSize * 2);

        if (readBuffer) {
            byte xbuffer[0x4000];
            RAWINPUT *xinput = (PRAWINPUT)xbuffer;
            UINT xbufsize = sizeof(xbuffer);
            UINT count = GetRawInputBuffer(xinput, &xbufsize, sizeof(RAWINPUTHEADER));
            AssertNotEquals("ri.buf.c", count, (UINT)-1);

            for (UINT i = 0; i < count; i++) {
#ifndef _WIN64 // look at this mess windows made
                if (gWow64) {
                    byte *ptr = (byte *)xinput;
                    *(UINT *)(ptr + 0xc) = *(UINT *)(ptr + 0x10);
                    MoveMemory(ptr + 0x10, ptr + 0x18, xinput->header.dwSize - 0x18);
                }
#endif

                readCallback(xinput);
                xinput = NEXTRAWINPUTBLOCK(xinput);
            }
        }
    }
}

void ProcessRawInputMsgs(MSG &msg) {
    ProcessRawInputMsg([](RAWINPUT *rawinput) {
        for (auto &readCb : gActiveRawReadCbs) {
            readCb(rawinput);
        }
    },
                       msg, false);
}

template <class TReadCb>
void ProcessRawInputMsgs(TReadCb readCallback, bool readBuffer, bool readWait) {
    MSG msg;
    while (readBuffer || readWait || GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (readWait) {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_RAWINPUT, MWMO_INPUTAVAILABLE);
            PeekMessageW(&msg, nullptr, 0, 0, (readBuffer ? PM_NOREMOVE : PM_REMOVE) | PM_QS_INPUT);
        } else if (readBuffer) {
            while (!PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
                WaitMessage();
            }
        }

        ProcessRawInputMsg(readCallback, msg, readBuffer);

        if (readBuffer && msg.message != WM_INPUT) {
            GetMessageW(&msg, nullptr, 0, 0);
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void ReadRawInput(RawInputData *data, int count, bool readBuffer, bool readWait, bool readPage) {
    CreateThread([=] {
        for (int i = 0; i < count; i++) {
            data[i].reader = new RawDeviceReader(i, data[i].preparsed, readBuffer ? "rawbuffer" : "raw");
        }

        auto readCb = [data, count](RAWINPUT *ri) {
            if (ri->header.dwType == RIM_TYPEHID) {
                for (int i = 0; i < count; i++) {
                    if (data[i].device == ri->header.hDevice) {
                        for (DWORD j = 0; j < ri->data.hid.dwCount; j++) {
                            data[i].reader->Read(ri->data.hid.bRawData + ri->data.hid.dwSizeHid * j, ri->data.hid.dwSizeHid);
                        }
                        break;
                    }
                }
            }
        };

        HWND window = gRegRawActive ? nullptr : CreateWindowW(L"STATIC", L"MSG", 0, 0, 0, 0, 0, 0, NULL, NULL, NULL);
        int flags = gRegRawActive ? 0 : RIDEV_INPUTSINK;

        RAWINPUTDEVICE rid;
        rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid.usUsage = readPage ? 0 : HID_USAGE_GENERIC_GAMEPAD;
        rid.hwndTarget = window;
        rid.dwFlags = RIDEV_DEVNOTIFY | flags | (readPage ? RIDEV_PAGEONLY : 0);
        AssertEquals("ri.reg", RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE)), TRUE);

        RAWINPUTDEVICE irid[4];
        UINT count = 4;
        AssertEquals("ri.greg", GetRegisteredRawInputDevices(irid, &count, sizeof(RAWINPUTDEVICE)), 1);
        AssertEquals("ri.greg.size", count, 4);
        AssertEquals("ri.greg.up", irid[0].usUsagePage, rid.usUsagePage);
        AssertEquals("ri.greg.u", irid[0].usUsage, rid.usUsage);
        AssertEquals("ri.greg.w", (intptr_t)irid[0].hwndTarget, (intptr_t)rid.hwndTarget);
        AssertEquals("ri.greg.f", irid[0].dwFlags & 0xfff, rid.dwFlags & 0xfff); // bug - some flags are lost?

        if (gRegRawActive) {
            gActiveRawReadCbs.push_back(readCb);
        } else {
            ProcessRawInputMsgs(readCb, readBuffer, readWait);
        }
    });
}

void TestRawInputRegister() {
    RAWINPUTDEVICE irid[10];
    UINT count = 10;
    AssertEquals("ri.greg.0", GetRegisteredRawInputDevices(irid, &count, sizeof(RAWINPUTDEVICE)), 0);

    RAWINPUTDEVICE rids[6] = {};
    rids[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rids[0].usUsage = HID_USAGE_GENERIC_SYSTEM_CTL;
    rids[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rids[1].usUsage = HID_USAGE_GENERIC_PORTABLE_DEVICE_CONTROL;
    rids[2].usUsagePage = HID_USAGE_PAGE_DIGITIZER;
    rids[2].usUsage = 0;
    rids[2].dwFlags = RIDEV_PAGEONLY;
    rids[3].usUsagePage = HID_USAGE_PAGE_DIGITIZER;
    rids[3].usUsage = HID_USAGE_DIGITIZER_PEN;
    rids[4].usUsagePage = HID_USAGE_PAGE_DIGITIZER;
    rids[4].usUsage = HID_USAGE_DIGITIZER_TOUCH;
    rids[4].dwFlags = RIDEV_EXCLUDE;
    rids[5].usUsagePage = HID_USAGE_PAGE_CONSUMER;
    rids[5].usUsage = HID_USAGE_CONSUMERCTRL;
    rids[5].dwFlags = RIDEV_EXCLUDE;

    AssertEquals("ri.reg.gen", RegisterRawInputDevices(rids, 6, sizeof(RAWINPUTDEVICE)), TRUE);
    AssertEquals("ri.greg.gen", GetRegisteredRawInputDevices(irid, &count, sizeof(RAWINPUTDEVICE)), 6);

    UINT badCount = 3;
    AssertEquals("ri.greg.badgen", GetRegisteredRawInputDevices(irid, &badCount, sizeof(RAWINPUTDEVICE)), INVALID_UINT_VALUE);
    AssertEquals("ri.greg.badgen.err", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    AssertEquals("ri.greg.badgen.sz", badCount, 6);

    for (int i = 0; i < 6; i++) {
        rids[i].dwFlags = (rids[i].dwFlags & RIDEV_PAGEONLY) | RIDEV_REMOVE;
    }
    AssertEquals("ri.reg.del", RegisterRawInputDevices(rids, 6, sizeof(RAWINPUTDEVICE)), TRUE);
    AssertEquals("ri.greg.del", GetRegisteredRawInputDevices(irid, &count, sizeof(RAWINPUTDEVICE)), 0);
}

void TestRawInput(int numOurs, bool read, bool readBuffer, bool readWait, bool readPage, bool printDevices) {
    UINT count;
    AssertEquals("ri.list.count", GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)), 0);

    int junk = 10;
    RAWINPUTDEVICELIST *list = new RAWINPUTDEVICELIST[count + junk];

    UINT badSize = count - 1;
    AssertEquals("ri.list.bad", GetRawInputDeviceList(list, &badSize, sizeof(RAWINPUTDEVICELIST)), (UINT)-1);
    AssertEquals("ri.list.bad.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    AssertEquals("ri.list.bad.size", badSize, count);

    UINT size = count + junk;
    AssertEquals("ri.list", GetRawInputDeviceList(list, &size, sizeof(RAWINPUTDEVICELIST)), count);
    AssertEquals("ri.list.size", size, count + junk);

    TestRawInputRegister();

    auto data = new RawInputData[numOurs];

    for (int i = 0; i < (int)count; i++) {
        HANDLE device = list[i].hDevice;

        AssertEquals("ri.get.bad", GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, nullptr, nullptr), (UINT)-1);
        AssertEquals("ri.get.bad.errno", GetLastError(), ERROR_NOACCESS);

        // info

        UINT infoCount = 0;
        AssertEquals("ri.info.count", GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, nullptr, &infoCount), 0);

        RID_DEVICE_INFO *info = new RID_DEVICE_INFO[2];
        UINT infoBadSize = infoCount - 1;
        AssertEquals("ri.info.bad", GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, info, &infoBadSize), (UINT)-1);
        AssertEquals("ri.info.bad.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
        AssertEquals("ri.info.bad.size", infoBadSize, infoCount);

        infoBadSize = infoCount + 1;
        AssertEquals("ri.info.worse", GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, info, &infoBadSize), (UINT)-1);
        AssertEquals("ri.info.worse.errno", GetLastError(), ERROR_INVALID_PARAMETER);
        AssertEquals("ri.info.worse.size", infoBadSize, infoCount + 1);

        UINT infoSize = infoCount;
        AssertEquals("ri.info", GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, info, &infoSize), infoCount);
        AssertEquals("ri.info.size", infoSize, infoCount);

        AssertEquals("ri.info.type", info->dwType, list[i].dwType);
        AssertEquals("ri.info.size", info->cbSize, sizeof(RID_DEVICE_INFO));

        // name

        UINT nameCountW = 0;
        AssertEquals("ri.name.w.count", GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &nameCountW), 0);

        wchar_t *nameW = new wchar_t[nameCountW + junk];
        UINT badNameSizeW = nameCountW - junk;
        AssertEquals("ri.name.w.bad", GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nameW, &badNameSizeW), (UINT)-1);
        AssertEquals("ri.name.w.bad.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
        AssertEquals("ri.name.w.bad.size", badNameSizeW, nameCountW);

        UINT nameSizeW = nameCountW + junk;
        AssertEquals("ri.name.w", GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nameW, &nameSizeW), nameCountW);
        AssertEquals("ri.name.w.size", nameSizeW, nameCountW + junk);

        UINT nameCountA = 0;
        AssertEquals("ri.name.a.count", GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, nullptr, &nameCountA), 0);

        PathA expectedNameA = PathToStr(nameW);
        UINT expectedNameACount = (UINT)strlen(expectedNameA) + 1;

        char *nameA = new char[nameCountA + junk];
        UINT badNameSizeA = expectedNameACount - junk;
        AssertEquals("ri.name.a.bad", GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, nameA, &badNameSizeA), (UINT)-1);
        AssertEquals("ri.name.a.bad.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
        AssertEquals("ri.name.a.bad.size", badNameSizeA, nameCountA);

        UINT nameSizeA = nameCountA + junk;
        AssertEquals("ri.name.a", GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, nameA, &nameSizeA), expectedNameACount);
        AssertEquals("ri.name.a.size", nameSizeA, nameCountA + junk);
        AssertEquals("ri.name.eq", nameA, expectedNameA);

        if (printDevices) {
            printf("%ls (%x)\n", nameW, info->dwType);
        }

        int userIdx = i - count + numOurs;
        bool ours = userIdx >= 0;

        // preparsed

        UINT ppCount = 0;
        AssertEquals("ri.pp.count", GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &ppCount), 0);

        if (ours) {
            AssertNotEquals("ri.pp.count.has", ppCount, 0);
        }
        byte *pp = nullptr;
        if (ppCount) {
            pp = new byte[ppCount + junk];
            UINT ppBadSize = ppCount - 1;
            AssertEquals("ri.pp.bad", GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, pp, &ppBadSize), (UINT)-1);
            AssertEquals("ri.pp.bad.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
            AssertEquals("ri.pp.bad.size", ppBadSize, ppCount);

            UINT ppSize = ppCount + junk;
            AssertEquals("ri.pp", GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, pp, &ppSize), ppCount);
            AssertEquals("ri.pp.size", ppSize, ppCount + junk);
        }

        TestDevice(nameA, nameW, ours, userIdx);

        if (ours && (read || readBuffer)) {
            data[userIdx] = RawInputData{device, pp, nullptr};
        }
    }

    if (read || readBuffer) {
        ReadRawInput(data, numOurs, readBuffer, readWait, readPage);
    }
}

void TestSetupDi(int numOurs, bool readDevice, bool readDeviceImmediate) {
    GUID hidGuid = GetOutput(HidD_GetHidGuid);

    for (int type = 0; type < 2; type++) {
        HDEVINFO devs = type == 0 ? SetupDiGetClassDevsA(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE) : SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
        AssertNotEquals("di.init", (intptr_t)devs, (intptr_t)INVALID_HANDLE_VALUE);

        int totalCount = 0;
        for (int i = 0; true; i++) {
            SP_DEVINFO_DATA data;
            data.cbSize = sizeof(data);

            if (SetupDiEnumDeviceInfo(devs, i, &data)) {
                totalCount++;
            } else {
                break;
            }
        }

        for (int i = 0; true; i++) {
            SP_DEVINFO_DATA data;
            data.cbSize = sizeof(data);

            if (!SetupDiEnumDeviceInfo(devs, i, &data)) {
                AssertEquals("di.end", GetLastError(), ERROR_NO_MORE_ITEMS);
                break;
            }

            int userIdx = i - totalCount + numOurs;
            bool ours = userIdx >= 0;

            for (int j = 0; true; j++) {
                SP_DEVICE_INTERFACE_DATA idata;
                idata.cbSize = sizeof(idata);

                if (!SetupDiEnumDeviceInterfaces(devs, &data, &hidGuid, j, &idata)) {
                    AssertEquals("di.intf.end", GetLastError(), ERROR_NO_MORE_ITEMS);
                    break;
                }

                AssertEquals("di.intf.cls", &idata.InterfaceClassGuid, &hidGuid, sizeof(hidGuid));

                SP_DEVINFO_DATA idataData;
                idataData.cbSize = sizeof(idataData);
                DWORD detailSize;
                AssertEquals("di.detail.a.bad", SetupDiGetDeviceInterfaceDetailA(devs, &idata, nullptr, 0, &detailSize, &idataData), FALSE);
                AssertEquals("di.detail.a.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
                AssertEquals("di.detail.a.data", &data, &idataData, sizeof(data));

                SP_DEVICE_INTERFACE_DETAIL_DATA_A *detailAData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)new byte[detailSize];
                detailAData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
                AssertEquals("di.detail.a.worse", SetupDiGetDeviceInterfaceDetailA(devs, &idata, detailAData, detailSize / 2, &detailSize, nullptr), FALSE);
                AssertEquals("di.detail.a.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);

                AssertEquals("di.detail.a", SetupDiGetDeviceInterfaceDetailA(devs, &idata, detailAData, detailSize, &detailSize, nullptr), TRUE);

                AssertEquals("di.detail.w.bad", SetupDiGetDeviceInterfaceDetailW(devs, &idata, nullptr, 0, &detailSize, &idataData), FALSE);
                AssertEquals("di.detail.w.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);
                AssertEquals("di.detail.w.data", &data, &idataData, sizeof(data));

                SP_DEVICE_INTERFACE_DETAIL_DATA_W *detailWData = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)new byte[detailSize];
                detailWData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
                AssertEquals("di.detail.w.worse", SetupDiGetDeviceInterfaceDetailW(devs, &idata, detailWData, detailSize / 2, &detailSize, nullptr), FALSE);
                AssertEquals("di.detail.w.errno", GetLastError(), ERROR_INSUFFICIENT_BUFFER);

                AssertEquals("di.detail.w", SetupDiGetDeviceInterfaceDetailW(devs, &idata, detailWData, detailSize, &detailSize, nullptr), TRUE);

                TestDevice(detailAData->DevicePath, detailWData->DevicePath, ours, userIdx, readDevice && type == 1, readDeviceImmediate);
            }
        }
    }
}

void TestCfgMgrPrintProp(int depth, DEVPROPKEY *key, DEVPROPTYPE type, byte *value, ULONG size) {
    GUID guid = key->fmtid;
    printf("%*s{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}:%lX = ", depth * 2, "",
           guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7],
           key->pid);

    switch (type) {
    case DEVPROP_TYPE_STRING: {
        PathA str = PathToStr((wchar_t *)value);
        printf("(str) %s\n", str.Get());
        break;
    }
    case DEVPROP_TYPE_STRING_LIST: {
        printf("(strs) ");
        wchar_t *ptr = (wchar_t *)value;
        while (*ptr) {
            PathA str = PathToStr(ptr);
            printf(",\"%s\"", str.Get());
            ptr += wcslen(ptr) + 1;
        }
        printf("\n");
        break;
    }

    case DEVPROP_TYPE_BINARY: {
        printf("(bytes) ");
        for (ULONG i = 0; i < size; i++) {
            printf(",%02x", value[i]);
        }
        printf("\n");
        break;
    }

    case DEVPROP_TYPE_EMPTY:
        printf("(empty)\n");
        break;
    case DEVPROP_TYPE_NULL:
        printf("(null)\n");
        break;
    case DEVPROP_TYPE_BOOLEAN:
        printf("(bool) %s\n", *(DEVPROP_BOOLEAN *)value ? "true" : "false");
        break;

    case DEVPROP_TYPE_BYTE:
        printf("(u8) %" PRIu8 "\n", *(uint8_t *)value);
        break;
    case DEVPROP_TYPE_UINT16:
        printf("(u16) %" PRIu16 "\n", *(uint16_t *)value);
        break;
    case DEVPROP_TYPE_UINT32:
        printf("(u32) %" PRIu32 "\n", *(uint32_t *)value);
        break;
    case DEVPROP_TYPE_UINT64:
        printf("(u64) %" PRIu64 "\n", *(uint64_t *)value);
        break;

    case DEVPROP_TYPE_SBYTE:
        printf("(s8) %" PRId8 "\n", *(int8_t *)value);
        break;
    case DEVPROP_TYPE_INT16:
        printf("(s16) %" PRId16 "\n", *(int16_t *)value);
        break;
    case DEVPROP_TYPE_INT32:
        printf("(s32) %" PRId32 "\n", *(int32_t *)value);
        break;
    case DEVPROP_TYPE_INT64:
        printf("(s64) %" PRId64 "\n", *(int64_t *)value);
        break;

    case DEVPROP_TYPE_FILETIME: {
        char buffer[1000];
        SHFormatDateTimeA((FILETIME *)value, nullptr, buffer, sizeof(buffer));
        printf("(time) %s\n", buffer);
        break;
    }
    case DEVPROP_TYPE_GUID: {
        GUID guid = *(GUID *)value;
        printf("(guid) {%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}\n",
               guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        break;
    }

    default: {
        printf("(type %x)\n", type);
        break;
    }
    }
}

void TestCfgMgrNode(bool printDevices, bool printAll, DEVINST dev, GUID *classes, size_t numClasses) {
    // (assuming no changes in device tree)

    if (!dev) {
        AssertEquals("cm.locate", CMEXAW(CM_Locate_DevNode, A, &dev, NULL, 0), CR_SUCCESS);

        ULONG allLen;
        AssertEquals("cm.all.size", CMEXAW(CM_Get_Device_ID_List_Size, A, &allLen, NULL, 0), CR_SUCCESS);

        char *all = new char[allLen];
        AssertEquals("cm.all.bad", CMEXAW(CM_Get_Device_ID_List, A, NULL, all, allLen - 1, 0), CR_BUFFER_SMALL);
        AssertEquals("cm.all", CMEXAW(CM_Get_Device_ID_List, A, NULL, all, allLen, 0), CR_SUCCESS);
        delete[] all;
    }

    while (dev) {
        ULONG len;
        AssertEquals("cm.did.size", CMEX(CM_Get_Device_ID_Size, &len, dev, 0), CR_SUCCESS);

        ULONG bufLen = len + 1;
        char *did = new char[bufLen];
        AssertEquals("cm.did.bad", CMEXAW(CM_Get_Device_ID, A, dev, did, len, 0), CR_BUFFER_SMALL);
        AssertEquals("cm.did", CMEXAW(CM_Get_Device_ID, A, dev, did, bufLen, 0), CR_SUCCESS);

        ULONG depth = 0;
        AssertEquals("cm.depth", CMEX(CM_Get_Depth, &depth, dev, 0), CR_SUCCESS);

        if (!tstrnieq(did, "SWD\\PRINTENUM\\", 14)) // invalid unrelated devices
        {
            DEVINST mydev;
            AssertEquals("cm.did.locate", CMEXAW(CM_Locate_DevNode, A, &mydev, did, 0), CR_SUCCESS);
            AssertEquals("cm.did.locate.me", mydev, dev);
            AssertEquals("cm.did.locate.nv", CMEXAW(CM_Locate_DevNode, A, &mydev, did, CM_LOCATE_DEVNODE_NOVALIDATION), CR_SUCCESS);
            AssertEquals("cm.did.locate.nv.me", mydev, dev);
        }

        ULONG status, problem;
        AssertEquals("cm.status", CMEX(CM_Get_DevNode_Status, &status, &problem, dev, 0), CR_SUCCESS);

        if (printDevices) {
            printf("%*s%s (%x,%x)\n", depth * 2, "", did, status, problem);
        }

        bool propsPrinted = false;
        for (size_t ci = 0; ci < numClasses; ci++) {
            ULONG ibuflen;
            AssertEquals("cm.ilist.size", CMEXAW(CM_Get_Device_Interface_List_Size, A, &ibuflen, &classes[ci], did, CM_GET_DEVICE_INTERFACE_LIST_PRESENT), CR_SUCCESS);

            // avoid print props for irrelevant devices except under printAll
            if (!propsPrinted && (printAll || ibuflen > 1)) {
                propsPrinted = true;

                // instance notify
                Path wdid = PathFromStr(did);

                CM_NOTIFY_FILTER filter;
                ZeroMemory(&filter, sizeof(filter));
                filter.cbSize = sizeof(filter);
                filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;
                wcscpy(filter.u.DeviceInstance.InstanceId, wdid);
                HCMNOTIFICATION notify;
                AssertEquals("cm.ntf.inst", CM_Register_Notification(&filter, (void *)0x124, DeviceChangeCMCb, &notify), CR_SUCCESS);

                // device props
                ULONG dpcount = 0;
                AssertEquals("cm.prop.keys.size", CMEX(CM_Get_DevNode_Property_Keys, dev, nullptr, &dpcount, 0), CR_BUFFER_SMALL);
                if (dpcount) {
                    DEVPROPKEY *dpkeys = new DEVPROPKEY[dpcount];
                    ULONG dpbad = dpcount - 1;
                    AssertEquals("cm.prop.keys.bad", CMEX(CM_Get_DevNode_Property_Keys, dev, dpkeys, &dpbad, 0), CR_BUFFER_SMALL);
                    AssertEquals("cm.prop.keys.bad.size", dpbad, dpcount);
                    ULONG dpgood = dpcount + 1;
                    AssertEquals("cm.prop.keys", CMEX(CM_Get_DevNode_Property_Keys, dev, dpkeys, &dpgood, 0), CR_SUCCESS);
                    AssertEquals("cm.prop.keys.size", dpgood, dpcount);

                    DEVPROPKEY badkey = {1, 2, 3, 4};
                    DEVPROPTYPE type;
                    ULONG unused = 0;
                    AssertEquals("cm.prop.miss", CMEXAW(CM_Get_DevNode_Property, W, dev, &badkey, &type, nullptr, &unused, 0), CR_NO_SUCH_VALUE);

                    for (ULONG i = 0; i < dpcount; i++) {
                        DEVPROPKEY *key = &dpkeys[i];
                        ULONG dpsize = 0;
                        AssertEquals("cm.prop.size", CMEXAW(CM_Get_DevNode_Property, W, dev, key, &type, nullptr, &dpsize, 0), CR_BUFFER_SMALL);

                        byte *dprop = new byte[dpsize];
                        dpbad = dpsize - 1;
                        AssertEquals("cm.prop.bad", CMEXAW(CM_Get_DevNode_Property, W, dev, key, &type, dprop, &dpbad, 0), CR_BUFFER_SMALL);
                        AssertEquals("cm.prop.bad.size", dpbad, dpsize);
                        AssertEquals("cm.prop", CMEXAW(CM_Get_DevNode_Property, W, dev, key, &type, dprop, &dpsize, 0), CR_SUCCESS);

                        if (printDevices) {
                            TestCfgMgrPrintProp(depth + 2, key, type, dprop, dpsize);
                        }
                        delete[] dprop;
                    }

                    delete[] dpkeys;
                }

                // registry props
                for (int p = CM_DRP_MIN; p <= CM_DRP_MAX; p++) {
                    ULONG plen = 0;
                    int ret = CMEXAW(CM_Get_DevNode_Registry_Property, W, dev, p, nullptr, nullptr, &plen, 0);
                    if (ret == CR_NO_SUCH_VALUE || ret == CR_FAILURE) {
                        continue;
                    }
                    if (printAll && ret != CR_BUFFER_SMALL) { // ?
                        continue;
                    }

                    AssertEquals("cm.rprop.size", ret, CR_BUFFER_SMALL);

                    byte *prop = new byte[plen];
                    ULONG pbad = plen - 1;
                    ULONG regType;
                    AssertEquals("cm.rprop.bad", CMEXAW(CM_Get_DevNode_Registry_Property, W, dev, p, &regType, prop, &pbad, 0), CR_BUFFER_SMALL);
                    AssertEquals("cm.rprop.bad.size", pbad, plen);
                    AssertEquals("cm.rprop", CMEXAW(CM_Get_DevNode_Registry_Property, W, dev, p, &regType, prop, &plen, 0), CR_SUCCESS);

                    if (printDevices) {
                        DEVPROPTYPE realType = DEVPROP_TYPE_NULL;
                        switch (regType) {
                        case REG_SZ:
                            realType = DEVPROP_TYPE_STRING;
                            break;
                        case REG_MULTI_SZ:
                            realType = DEVPROP_TYPE_STRING_LIST;
                            break;
                        case REG_DWORD:
                            realType = DEVPROP_TYPE_UINT32;
                            break;
                        case REG_QWORD:
                            realType = DEVPROP_TYPE_UINT64;
                            break;
                        case REG_BINARY:
                            realType = DEVPROP_TYPE_BINARY;
                            break;
                        }

                        DEVPROPKEY pkey = {};
                        FillMemory(&pkey.fmtid, sizeof(pkey.fmtid), 0xff);
                        pkey.pid = p;
                        TestCfgMgrPrintProp(depth + 2, &pkey, realType, prop, plen);
                    }
                    delete[] prop;
                }
            }

            if (ibuflen > 1) {
                // interfaces
                char *diids = new char[ibuflen];
                int iret = CMEXAW(CM_Get_Device_Interface_List, A, &classes[ci], did, diids, ibuflen - 1, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
                AssertEquals("cm.ilist.bad", iret, CR_BUFFER_SMALL);
                AssertEquals("cm.ilist", CMEXAW(CM_Get_Device_Interface_List, A, &classes[ci], did, diids, ibuflen, CM_GET_DEVICE_INTERFACE_LIST_PRESENT), CR_SUCCESS);

                char *diidp = diids;
                while (*diidp) {
                    if (printDevices) {
                        printf("%*s(i) %s\n", (depth + 1) * 2, "", diidp);
                    }

                    Path diidpw = PathFromStr(diidp);

                    // interface notify
                    HANDLE devh = CreateFileW(diidpw, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr);
                    if (devh != INVALID_HANDLE_VALUE) {
                        DEV_BROADCAST_HANDLE dbc;
                        ZeroMemory(&dbc, sizeof(dbc));
                        dbc.dbch_size = sizeof(dbc);
                        dbc.dbch_devicetype = DBT_DEVTYP_HANDLE;
                        dbc.dbch_handle = devh;
                        AssertNotEquals("dev.ntf.hdl", (intptr_t)RegisterDeviceNotificationA(gNotifyWin, &dbc, 0), NULL);
                    }

                    devh = CreateFileW(diidpw, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr);
                    if (devh != INVALID_HANDLE_VALUE) {
                        CM_NOTIFY_FILTER filter;
                        ZeroMemory(&filter, sizeof(filter));
                        filter.cbSize = sizeof(filter);
                        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
                        filter.u.DeviceHandle.hTarget = devh;
                        HCMNOTIFICATION notify;
                        HANDLE devh2;
                        AssertNotEquals("cm.ntf.hdl.dup", DuplicateHandle(GetCurrentProcess(), devh, GetCurrentProcess(), &devh2, 0, false, PROCESS_DUP_HANDLE), 0);
                        AssertEquals("cm.ntf.hdl", CM_Register_Notification(&filter, (void *)devh2, DeviceChangeCMCb, &notify), CR_SUCCESS);
                        CloseHandle(devh);
                    }

                    // interface props
                    ULONG dpcount = 0;
                    AssertEquals("cm.iprop.keys.size", CMEXAW(CM_Get_Device_Interface_Property_Keys, W, diidpw, nullptr, &dpcount, 0), CR_BUFFER_SMALL);
                    if (dpcount) {
                        DEVPROPKEY *dpkeys = new DEVPROPKEY[dpcount];
                        ULONG dpbad = dpcount - 1;
                        AssertEquals("cm.iprop.keys.bad", CMEXAW(CM_Get_Device_Interface_Property_Keys, W, diidpw, dpkeys, &dpbad, 0), CR_BUFFER_SMALL);
                        AssertEquals("cm.iprop.keys.bad.size", dpbad, dpcount);
                        ULONG dpgood = dpcount + 1;
                        AssertEquals("cm.iprop.keys", CMEXAW(CM_Get_Device_Interface_Property_Keys, W, diidpw, dpkeys, &dpgood, 0), CR_SUCCESS);
                        AssertEquals("cm.iprop.keys.size", dpgood, dpcount);

                        DEVPROPKEY badkey = {1, 2, 3, 4};
                        DEVPROPTYPE type;
                        ULONG unused = 0;
                        AssertEquals("cm.iprop.miss", CMEXAW(CM_Get_Device_Interface_Property, W, diidpw, &badkey, &type, nullptr, &unused, 0), CR_NO_SUCH_VALUE);

                        for (ULONG i = 0; i < dpcount; i++) {
                            DEVPROPKEY *key = &dpkeys[i];
                            ULONG dpsize = 0;
                            AssertEquals("cm.iprop.size", CMEXAW(CM_Get_Device_Interface_Property, W, diidpw, key, &type, nullptr, &dpsize, 0), CR_BUFFER_SMALL);

                            byte *dprop = new byte[dpsize];
                            dpbad = dpsize - 1;
                            AssertEquals("cm.iprop.bad", CMEXAW(CM_Get_Device_Interface_Property, W, diidpw, key, &type, dprop, &dpbad, 0), CR_BUFFER_SMALL);
                            AssertEquals("cm.iprop.bad.size", dpbad, dpsize);
                            AssertEquals("cm.iprop", CMEXAW(CM_Get_Device_Interface_Property, W, diidpw, key, &type, dprop, &dpsize, 0), CR_SUCCESS);

                            if (printDevices) {
                                TestCfgMgrPrintProp(depth + 3, key, type, dprop, dpsize);
                            }
                            delete[] dprop;
                        }

                        delete[] dpkeys;
                    }

                    diidp += strlen(diidp) + 1;
                }

                delete[] diids;
            }
        }

        delete[] did;

        DEVINST child;
        CONFIGRET ret = CMEX(CM_Get_Child, &child, dev, 0);
        AssertEquals("cm.child", ret, child ? CR_SUCCESS : CR_NO_SUCH_DEVNODE);
        if (child) {
            TestCfgMgrNode(printDevices, printAll, child, classes, numClasses);

            DEVINST parent;
            CONFIGRET ret = CMEX(CM_Get_Parent, &parent, child, 0);
            AssertEquals("cm.parent", ret, CR_SUCCESS);
            AssertEquals("cm.parent.eq", parent, dev);
        }

        ret = CMEX(CM_Get_Sibling, &dev, dev, 0);
        AssertEquals("cm.sibling", ret, dev ? CR_SUCCESS : CR_NO_SUCH_DEVNODE);
    }
}

void TestCfgMgr(bool printDevices, bool printAll) {
    if (printAll) {
        vector<GUID> classes;
        ULONG idx = 0;
        while (true) {
            GUID newCls;
            auto result = CMEX(CM_Enumerate_Classes, idx, &newCls, CM_ENUMERATE_CLASSES_INTERFACE);
            if (result == CR_NO_SUCH_VALUE) {
                break;
            }

            if (result == CR_SUCCESS) {
                classes.push_back(newCls);
            }
            idx++;
        }

        TestCfgMgrNode(printDevices, printAll, 0, classes.data(), classes.size());
    } else {
        GUID hidGuid = GetOutput(HidD_GetHidGuid);
        TestCfgMgrNode(printDevices, printAll, 0, &hidGuid, 1);
    }
}

void ReadJoysticks(int count) {
    CreateThread([=] {
        auto oldInfos = new JOYINFOEX[count];
        ZeroMemory(oldInfos, sizeof(JOYINFOEX) * count);

        while (true) {
            for (int joy = 0; joy < count; joy++) // this is a terrible way to do this (stresses device creation), thus we test it
            {
                auto &oldInfo = oldInfos[joy];

                JOYINFOEX info;
                info.dwSize = sizeof(info);
                info.dwFlags = JOY_RETURNALL;
                if (joyGetPosEx(joy, &info) == JOYERR_NOERROR && gPrintGamepad) {
                    if (info.dwXpos != oldInfo.dwXpos) {
                        printf("pad.%d x -> %d : joy\n", joy, info.dwXpos);
                    }
                    if (info.dwYpos != oldInfo.dwYpos) {
                        printf("pad.%d y -> %d : joy\n", joy, info.dwYpos);
                    }
                    if (info.dwZpos != oldInfo.dwZpos) {
                        printf("pad.%d z -> %d : joy\n", joy, info.dwZpos);
                    }
                    if (info.dwRpos != oldInfo.dwRpos) {
                        printf("pad.%d r -> %d : joy\n", joy, info.dwRpos);
                    }
                    if (info.dwUpos != oldInfo.dwUpos) {
                        printf("pad.%d u -> %d : joy\n", joy, info.dwUpos);
                    }
                    if (info.dwVpos != oldInfo.dwVpos) {
                        printf("pad.%d v -> %d : joy\n", joy, info.dwVpos);
                    }

                    if (info.dwButtons != oldInfo.dwButtons) {
                        for (int i = 0; i < 0x20; i++) {
                            if (((info.dwButtons ^ oldInfo.dwButtons) >> i) & 1) {
                                printf("pad.%d b%d -> %d : joy\n", joy, i + 1, (info.dwButtons >> i) & 1);
                            }
                        }
                    }

                    if (info.dwPOV != oldInfo.dwPOV) {
                        printf("pad.%d pov -> %d : joy\n", joy, info.dwPOV);
                    }

                    oldInfo = info;
                }
            }

            Sleep(10);
        }
    });
}

void TestJoystick(bool read) {
    UINT count = joyGetNumDevs();
    for (UINT joy = 0; joy < count; joy++) {
        JOYINFO info;
        if (joyGetPos(joy, &info) == JOYERR_NOERROR) {
            UINT thresh;
            AssertEquals("joy.th.get", joyGetThreshold(joy, &thresh), JOYERR_NOERROR);
            AssertEquals("joy.th.val", thresh, 0);

            AssertEquals("joy.th.set", joySetThreshold(joy, 10), JOYERR_NOERROR);

            JOYCAPSW caps;
            AssertEquals("joy.caps", joyGetDevCapsW(joy, &caps, sizeof(caps)), JOYERR_NOERROR);
        }
    }

    if (read) {
        ReadJoysticks(count);
    }
}

typedef DWORD(WINAPI *XInputGetState_T)(DWORD dwUserIndex, XINPUT_STATE *pState);
typedef DWORD(WINAPI *XInputSetState_T)(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration);
typedef DWORD(WINAPI *XInputGetKeystroke_T)(DWORD dwUserIndex, DWORD dwReserved, XINPUT_KEYSTROKE *pKeystroke);
typedef DWORD(WINAPI *XInputGetCapabilities_T)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities);
typedef DWORD(WINAPI *XInputGetBatteryInformation_T)(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION *pBatteryInformation);
typedef DWORD(WINAPI *XInputGetAudioDeviceIds_T)(DWORD dwUserIndex, LPWSTR pRenderDeviceId, UINT *pRenderCount, LPWSTR pCaptureDeviceId, UINT *pCaptureCount);
XInputGetState_T XInputGetState_F, XInputGetStateEx_F;
XInputSetState_T XInputSetState_F;
XInputGetKeystroke_T XInputGetKeystroke_F;
XInputGetCapabilities_T XInputGetCapabilities_F;
XInputGetBatteryInformation_T XInputGetBatteryInformation_F;
XInputGetAudioDeviceIds_T XInputGetAudioDeviceIds_F;

void ReadXInput(bool ex) {
    XInputGetState_T XInputGetState = (ex && XInputGetStateEx_F) ? XInputGetStateEx_F : XInputGetState_F;

    CreateThread([XInputGetState] {
        XINPUT_STATE oldState[XUSER_MAX_COUNT] = {};
        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            XInputGetState(i, &oldState[i]);
        }

        while (true) {
            Sleep(10);

            for (int i = 0; i < XUSER_MAX_COUNT; i++) {
                XINPUT_STATE state = {};
                if (XInputGetState(i, &state) == ERROR_SUCCESS &&
                    state.dwPacketNumber != oldState[i].dwPacketNumber &&
                    gPrintGamepad) {
                    const char *buttons[] = {"du", "dd", "dl", "dr", "st", "bk", "l", "r", "lb", "rb", "g", "", "a", "b", "x", "y"};

                    for (int mask = 1, b = 0; mask < 0x10000; mask <<= 1, b++) {
                        if ((state.Gamepad.wButtons & mask) != (oldState[i].Gamepad.wButtons & mask)) {
                            printf("pad.%d %s -> %d : xinput\n", i, buttons[b], (state.Gamepad.wButtons & mask) != 0);
                        }
                    }

                    if (state.Gamepad.bLeftTrigger != oldState[i].Gamepad.bLeftTrigger) {
                        printf("pad.%d lt -> %d : xinput\n", i, state.Gamepad.bLeftTrigger);
                    }
                    if (state.Gamepad.bRightTrigger != oldState[i].Gamepad.bRightTrigger) {
                        printf("pad.%d rt -> %d : xinput\n", i, state.Gamepad.bRightTrigger);
                    }

                    if (state.Gamepad.sThumbLX != oldState[i].Gamepad.sThumbLX) {
                        printf("pad.%d lx -> %d : xinput\n", i, state.Gamepad.sThumbLX);
                    }
                    if (state.Gamepad.sThumbLY != oldState[i].Gamepad.sThumbLY) {
                        printf("pad.%d ly -> %d : xinput\n", i, state.Gamepad.sThumbLY);
                    }
                    if (state.Gamepad.sThumbRX != oldState[i].Gamepad.sThumbRX) {
                        printf("pad.%d rx -> %d : xinput\n", i, state.Gamepad.sThumbRX);
                    }
                    if (state.Gamepad.sThumbRY != oldState[i].Gamepad.sThumbRY) {
                        printf("pad.%d ry -> %d : xinput\n", i, state.Gamepad.sThumbRY);
                    }

                    oldState[i] = state;
                }
            }
        }
    });
}

void ReadXInputStroke() {
    CreateThread([] {
        while (true) {
            XINPUT_KEYSTROKE key;
            while (XInputGetKeystroke_F(XUSER_INDEX_ANY, 0, &key) == ERROR_SUCCESS &&
                   gPrintGamepad) {
                char ch = (key.Flags & XINPUT_KEYSTROKE_REPEAT) ? 'r' : (key.Flags & XINPUT_KEYSTROKE_KEYUP) ? 'u'
                                                                                                             : 'd';

                printf("pad.%d %d -> %c : xstroke\n", key.UserIndex, key.VirtualKey, ch);
            }

            Sleep(10);
        }
    });
}

void RumbleXInput() {
    CreateThread([] {
        double left = 0, right = 0;
        while (true) {
            if (left < 1.0) {
                left = Clamp(left + 0.2, 0.0, 1.0);
            } else if (right < 1.0) {
                right = Clamp(right + 0.2, 0.0, 1.0);
            } else {
                left = right = 0;
            }

            Sleep(2000);

            XINPUT_VIBRATION vibe;
            vibe.wLeftMotorSpeed = (WORD)(0xffff * left);
            vibe.wRightMotorSpeed = (WORD)(0xffff * right);
            XInputSetState_F(0, &vibe);
        }
    });
}

LRESULT CALLBACK VisualizeWindowProc(HWND win, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_TIMER) {
        InvalidateRect(win, nullptr, false);
    } else if (msg == WM_PAINT) {
        static HDC dc = CreateCompatibleDC(nullptr);
        static HBITMAP bmp = CreateCompatibleBitmap(GetDC(nullptr), 450, 260);
        SelectObject(dc, bmp);

        RECT whole = {0, 0, 450, 260};
        FillRect(dc, &whole, (HBRUSH)(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, TA_TOP | TA_LEFT);

        XInputGetState_T XInputGetState = XInputGetStateEx_F ? XInputGetStateEx_F : XInputGetState_F;

        XINPUT_STATE state = {};
        if (XInputGetState(0, &state) == ERROR_SUCCESS) {
            const wchar_t *buttons[] = {L"U", L"D", L"L", L"R", L"ST", L"BK", L"LS", L"RS", L"LB", L"RB", L"G", L"", L"A", L"B", L"X", L"Y", L"LT", L"RT"};
            POINT btnPoints[] = {{140, 180}, {140, 220}, {115, 200}, {165, 200}, {260, 120}, {150, 120}, {65, 140}, {280, 200}, {40, 35}, {375, 35}, {210, 120}, {0, 0}, {350, 150}, {390, 120}, {315, 120}, {350, 90}, {80, 5}, {330, 5}};

            for (int mask = 1, b = 0; mask < 0x10000; mask <<= 1, b++) {
                if (state.Gamepad.wButtons & mask) {
                    SetTextColor(dc, RGB(0, 0, 0));
                    TextOutW(dc, btnPoints[b].x, btnPoints[b].y, buttons[b], (int)wcslen(buttons[b]));
                }
            }

            for (int b = 0; b < 2; b++) {
                BYTE value = b ? state.Gamepad.bRightTrigger : state.Gamepad.bLeftTrigger;
                if (value) {
                    SetTextColor(dc, RGB(value ^ 0xff, value ^ 0xff, value ^ 0xff));
                    int bb = 0x10 + b;
                    TextOutW(dc, btnPoints[bb].x, btnPoints[bb].y, buttons[bb], (int)wcslen(buttons[bb]));
                }
            }

            for (int b = 0; b < 2; b++) {
                SHORT x = b ? state.Gamepad.sThumbRX : state.Gamepad.sThumbLX;
                SHORT y = b ? state.Gamepad.sThumbRY : state.Gamepad.sThumbLY;
                if (x || y) {
                    static HPEN pen = CreatePen(PS_SOLID, 2, RGB(0xff, 0x80, 0x80));
                    SelectObject(dc, pen);

                    POINT c = btnPoints[6 + b];
                    MoveToEx(dc, c.x, c.y, nullptr);
                    LineTo(dc, c.x + x * 37 / 0x7fff, c.y - y * 37 / 0x7fff);
                }
            }
        }

        PAINTSTRUCT ps;
        HDC pdc = BeginPaint(win, &ps);
        RECT crect;
        GetClientRect(win, &crect);
        StretchBlt(pdc, 0, 0, crect.right, crect.bottom, dc, 0, 0, 450, 260, SRCCOPY);
        EndPaint(win, &ps);
        return 0;
    }

    return DefWindowProc(win, msg, w, l);
}

void VisualizeXInput() {
    CreateThread([] {
        RECT rect = {0, 0, 450, 260};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
        HWND window = CreateWindowW(L"STATIC", L"Visualize", WS_OVERLAPPEDWINDOW, 400, 0, rect.right - rect.left, rect.bottom - rect.top, 0, NULL, NULL, NULL);
        SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)VisualizeWindowProc);
        SetTimer(window, 1, 15, nullptr);
        ShowWindow(window, SW_SHOW);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (gRegRawActive) {
                ProcessRawInputMsgs(msg);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    });
}

void TestXInput(int version, bool read, bool readEx, bool readStroke, bool rumble, bool visualize) {
    const wchar_t *dll_name = L"";
    switch (version) {
    case 1:
        dll_name = L"xinput1_1.dll";
        break;
    case 2:
        dll_name = L"xinput1_2.dll";
        break;
    case 3:
        dll_name = L"xinput1_3.dll";
        break;
    case 4:
        dll_name = L"xinput1_4.dll";
        break;
    case -9:
        dll_name = L"xinput9_1_0.dll";
        break;
    default:
        Alert(L"wrong xinput dll name");
    }

    HMODULE xdll = LoadLibraryW(dll_name);
    AssertNotEquals("x.load", (intptr_t)xdll, 0);

    XInputGetState_F = (XInputGetState_T)GetProcAddress(xdll, "XInputGetState");
    XInputSetState_F = (XInputSetState_T)GetProcAddress(xdll, "XInputSetState");
    XInputGetKeystroke_F = (XInputGetKeystroke_T)GetProcAddress(xdll, "XInputGetKeystroke");
    XInputGetCapabilities_F = (XInputGetCapabilities_T)GetProcAddress(xdll, "XInputGetCapabilities");
    XInputGetBatteryInformation_F = (XInputGetBatteryInformation_T)GetProcAddress(xdll, "XInputGetBatteryInformation");
    XInputGetAudioDeviceIds_F = (XInputGetAudioDeviceIds_T)GetProcAddress(xdll, "XInputGetAudioDeviceIds");
    XInputGetStateEx_F = (XInputGetState_T)GetProcAddress(xdll, MAKEINTRESOURCEA(100));

    for (int i = 0; i < XUSER_MAX_COUNT; i++) {
        XINPUT_CAPABILITIES caps;
        if (XInputGetCapabilities_F(i, 0, &caps) == ERROR_SUCCESS) {
            XINPUT_STATE state;
            AssertEquals("x.gs", XInputGetState_F(i, &state), ERROR_SUCCESS);

            XINPUT_VIBRATION vibe = {};
            AssertEquals("x.ss", XInputSetState_F(i, &vibe), ERROR_SUCCESS);

            XINPUT_BATTERY_INFORMATION binfo;
            if (XInputGetBatteryInformation_F) {
                AssertEquals("x.gbi", XInputGetBatteryInformation_F(i, BATTERY_DEVTYPE_GAMEPAD, &binfo), ERROR_SUCCESS);
            }

            if (XInputGetAudioDeviceIds_F && i == 0) // bug(?) - i > 0 always end up on first device but with non-0 index
            {
                wchar_t renderAudio[100], captureAudio[100];
                UINT renderAudioSz = (UINT)size(renderAudio), captureAudioSz = (UINT)size(captureAudio);
                AssertEquals("x.gaud", XInputGetAudioDeviceIds_F(i, renderAudio, &renderAudioSz, captureAudio, &captureAudioSz), ERROR_SUCCESS);
            }

            XINPUT_KEYSTROKE stroke;
            DWORD status = XInputGetKeystroke_F ? XInputGetKeystroke_F(i, 0, &stroke) : ERROR_EMPTY;
            AssertEquals("x.gks", status == ERROR_SUCCESS || status == ERROR_EMPTY, true);
        }
    }

    if (read || readEx) {
        ReadXInput(readEx);
    }
    if (readStroke && XInputGetKeystroke_F) {
        ReadXInputStroke();
    }
    if (rumble) {
        RumbleXInput();
    }
    if (visualize) {
        VisualizeXInput();
    }
}

static int sWgiGamepadId = 0; // any better way?
static int sWgiRawCtrlId = 0; // any better way?

void ProcessWgiGamepad(int gamepadId, WGI::IGamepad *gamepad, bool rumble) {
    gamepad->AddRef();
    CreateThread([=] {
        WGI::GamepadReading oldReading = {};

        while (true) {
            Sleep(10);

            WGI::GamepadReading reading;
            AssertEquals("ro.wgig.read", gamepad->GetCurrentReading(&reading), S_OK);

            const char *buttons[] = {"st", "vk", "a", "b", "x", "y", "du", "dd", "dl", "dr", "lb", "rb", "l", "r", "p1", "p2", "p3", "p4"};

            for (int mask = 1, b = 0; mask < 0x10000; mask <<= 1, b++) {
                if ((reading.Buttons & mask) != (oldReading.Buttons & mask)) {
                    printf("pad.%d %s -> %d : wgi\n", gamepadId, buttons[b], (reading.Buttons & mask) != 0);
                }
            }

            if (reading.LeftTrigger != oldReading.LeftTrigger) {
                printf("pad.%d lt -> %lf : wgi\n", gamepadId, reading.LeftTrigger);
            }
            if (reading.RightTrigger != oldReading.RightTrigger) {
                printf("pad.%d rt -> %lf : wgi\n", gamepadId, reading.RightTrigger);
            }

            if (reading.LeftThumbstickX != oldReading.LeftThumbstickX) {
                printf("pad.%d lx -> %lf : wgi\n", gamepadId, reading.LeftThumbstickX);
            }
            if (reading.LeftThumbstickY != oldReading.LeftThumbstickY) {
                printf("pad.%d ly -> %lf : wgi\n", gamepadId, reading.LeftThumbstickY);
            }
            if (reading.RightThumbstickX != oldReading.RightThumbstickX) {
                printf("pad.%d rx -> %lf : wgi\n", gamepadId, reading.RightThumbstickX);
            }
            if (reading.RightThumbstickY != oldReading.RightThumbstickY) {
                printf("pad.%d ry -> %lf : wgi\n", gamepadId, reading.RightThumbstickY);
            }

            oldReading = reading;
        }
    });

    if (rumble) {
        CreateThread([=] {
            double left = 0, right = 0;
            while (true) {
                if (left < 1.0) {
                    left = Clamp(left + 0.2, 0.0, 1.0);
                } else if (right < 1.0) {
                    right = Clamp(right + 0.2, 0.0, 1.0);
                } else {
                    left = right = 0;
                }

                Sleep(2000);

                WGI::GamepadVibration vibration = {};
                vibration.LeftMotor = left;
                vibration.RightMotor = right;
                // TODO: trigger vibration?
                AssertEquals("ro.wgig.vib", gamepad->put_Vibration(vibration), S_OK);
            }
        });
    }
}

void ProcessWgiRawCtrl(int rawctrlId, WGI::IRawGameController *rawctrl) {
    rawctrl->AddRef();
    CreateThread([=] {
        INT32 nButtons;
        AssertEquals("ro.wgir.nbtn", rawctrl->get_ButtonCount(&nButtons), S_OK);
        INT32 nAxes;
        AssertEquals("ro.wgir.naxe", rawctrl->get_AxisCount(&nAxes), S_OK);
        INT32 nSwitches;
        AssertEquals("ro.wgir.nswi", rawctrl->get_SwitchCount(&nSwitches), S_OK);

        auto buttons = new boolean[nButtons];
        auto oldButtons = new boolean[nButtons]{};
        auto switches = new WGI::GameControllerSwitchPosition[nSwitches];
        auto oldSwitches = new WGI::GameControllerSwitchPosition[nSwitches]{};
        auto axes = new DOUBLE[nAxes];
        auto oldAxes = new DOUBLE[nAxes]{};

        while (true) {
            Sleep(10);

            UINT64 timestamp;
            AssertEquals("ro.wgir.read", rawctrl->GetCurrentReading(nButtons, buttons, nSwitches, switches, nAxes, axes, &timestamp), S_OK);

            for (int i = 0; i < nButtons; i++) {
                if (buttons[i] != oldButtons[i]) {
                    printf("pad.%d b%d -> %d : wgiraw\n", rawctrlId, i, buttons[i]);
                    oldButtons[i] = buttons[i];
                }
            }

            for (int i = 0; i < nAxes; i++) {
                if (axes[i] != oldAxes[i]) {
                    printf("pad.%d a%d -> %lf : wgiraw\n", rawctrlId, i, axes[i]);
                    oldAxes[i] = axes[i];
                }
            }

            for (int i = 0; i < nSwitches; i++) {
                if (switches[i] != oldSwitches[i]) {
                    printf("pad.%d s%d -> %d : wgiraw\n", rawctrlId, i, switches[i]);
                    oldSwitches[i] = switches[i];
                }
            }
        }
    });
}

void TestWgi(bool gamepad, bool raw, bool rumble) {
    // TODO: also test add/remove...
    CreateThread([=] {
        AssertEquals("ro.init", RoInitialize(RO_INIT_MULTITHREADED), S_OK);

        if (gamepad) {
            WRL::ComPtr<WGI::IGamepadStatics> gamepadStatics;
            AssertEquals("ro.act.wgig", RoGetActivationFactory(WRLW::HStringReference(L"Windows.Gaming.Input.Gamepad").Get(), __uuidof(WGI::IGamepadStatics), &gamepadStatics), S_OK);

            EventRegistrationToken addedToken;
            AssertEquals("ro.wgig.addev", gamepadStatics->add_GamepadAdded(WRL::Callback<WF::IEventHandler<WGI::Gamepad *>>([=](auto, WGI::IGamepad *gamepad) -> HRESULT {
                                                                               int gamepadId = sWgiGamepadId++;
                                                                               if (gPrintDeviceChange) {
                                                                                   printf("event : wgi device add : %d\n", gamepadId);
                                                                               }

                                                                               ProcessWgiGamepad(gamepadId, gamepad, rumble);
                                                                               return S_OK;
                                                                           }).Get(),
                                                                           &addedToken),
                         S_OK);

            EventRegistrationToken removeToken;
            AssertEquals("ro.wgig.remev", gamepadStatics->add_GamepadRemoved(WRL::Callback<WF::IEventHandler<WGI::Gamepad *>>([=](auto, WGI::IGamepad *gamepad) -> HRESULT {
                                                                                 if (gPrintDeviceChange) {
                                                                                     printf("event : wgi device remove\n"); // id unknown...
                                                                                 }

                                                                                 return S_OK;
                                                                             }).Get(),
                                                                             &removeToken),
                         S_OK);

            WRL::ComPtr<WFC::IVectorView<WGI::Gamepad *>> gamepads;
            AssertEquals("ro.wgig.get", gamepadStatics->get_Gamepads(&gamepads), S_OK);

            uint32_t gamepadCount;
            AssertEquals("ro.wgig.size", gamepads->get_Size(&gamepadCount), S_OK);

            for (uint32_t i = 0; i < gamepadCount; i++) {
                WRL::ComPtr<WGI::IGamepad> gamepad;
                AssertEquals("ro.wgig.at", gamepads->GetAt(i, &gamepad), S_OK);

                ProcessWgiGamepad(sWgiGamepadId++, gamepad.Get(), rumble);
            }
        }

        if (raw) {
            WRL::ComPtr<WGI::IRawGameControllerStatics> rawctrlStatics;
            AssertEquals("ro.act.wgir", RoGetActivationFactory(WRLW::HStringReference(L"Windows.Gaming.Input.RawGameController").Get(), __uuidof(WGI::IRawGameControllerStatics), &rawctrlStatics), S_OK);

            EventRegistrationToken addedToken;
            AssertEquals("ro.wgir.addev", rawctrlStatics->add_RawGameControllerAdded(WRL::Callback<WF::IEventHandler<WGI::RawGameController *>>([=](auto, WGI::IRawGameController *rawctrl) -> HRESULT {
                                                                                         int rawctrlId = sWgiRawCtrlId++;
                                                                                         if (gPrintDeviceChange) {
                                                                                             printf("event : wgi-raw device add : %d\n", rawctrlId);
                                                                                         }

                                                                                         ProcessWgiRawCtrl(rawctrlId, rawctrl);
                                                                                         return S_OK;
                                                                                     }).Get(),
                                                                                     &addedToken),
                         S_OK);

            EventRegistrationToken removeToken;
            AssertEquals("ro.wgir.remev", rawctrlStatics->add_RawGameControllerRemoved(WRL::Callback<WF::IEventHandler<WGI::RawGameController *>>([=](auto, WGI::IRawGameController *rawctrl) -> HRESULT {
                                                                                           if (gPrintDeviceChange) {
                                                                                               printf("event : wgi-raw device remove\n"); // id unknown...
                                                                                           }

                                                                                           return S_OK;
                                                                                       }).Get(),
                                                                                       &removeToken),
                         S_OK);

            WRL::ComPtr<WFC::IVectorView<WGI::RawGameController *>> rawctrls;
            AssertEquals("ro.wgir.get", rawctrlStatics->get_RawGameControllers(&rawctrls), S_OK);

            uint32_t rawctrlCount;
            AssertEquals("ro.wgir.size", rawctrls->get_Size(&rawctrlCount), S_OK);

            for (uint32_t i = 0; i < rawctrlCount; i++) {
                WRL::ComPtr<WGI::IRawGameController> rawctrl;
                AssertEquals("ro.wgir.at", rawctrls->GetAt(i, &rawctrl), S_OK);

                ProcessWgiRawCtrl(sWgiRawCtrlId++, rawctrl.Get());
            }
        }

        while (true) {
            Sleep(1000); // seems needed to avoid callbacks dying...
        }
    });
}

void TestWindowMsg(UINT msg, WPARAM w, LPARAM l, LONG time, bool injected, ULONG_PTR xinfo, const char *source) {
    int printed = -1;
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (gPrintKeyboard) {
            printed = printf("key %d -> %d %s%s%s%s: %s <%d>\n", (int)w,
                             msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN,
                             (HIWORD(l) & (KF_REPEAT | KF_UP)) == KF_REPEAT ? "[R] " : "",
                             HIWORD(l) & KF_EXTENDED ? "[X] " : "",
                             injected ? "[J] " : "",
                             (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) ? "[S] " : "",
                             source, HIWORD(l) & 0xff);
        }
        break;

#define CASE_WM_MOUSE(wm, id, val)                                 \
    case WM_##wm:                                                  \
    case WM_NC##wm:                                                \
        if (gPrintMouse)                                           \
            printed = printf("mouse %s -> %d %s%s: %s\n", id, val, \
                             msg == WM_NC##wm ? "[N] " : "",       \
                             injected ? "[J] " : "",               \
                             source);                              \
        break;

        CASE_WM_MOUSE(LBUTTONUP, "l", 0)
        CASE_WM_MOUSE(LBUTTONDOWN, "l", 1)
        CASE_WM_MOUSE(LBUTTONDBLCLK, "l", 2)
        CASE_WM_MOUSE(RBUTTONUP, "r", 0)
        CASE_WM_MOUSE(RBUTTONDOWN, "r", 1)
        CASE_WM_MOUSE(RBUTTONDBLCLK, "r", 2)
        CASE_WM_MOUSE(MBUTTONUP, "m", 0)
        CASE_WM_MOUSE(MBUTTONDOWN, "m", 1)
        CASE_WM_MOUSE(MBUTTONDBLCLK, "m", 2)
        CASE_WM_MOUSE(XBUTTONUP, HIWORD(w) == XBUTTON1 ? "x1" : "x2", 0)
        CASE_WM_MOUSE(XBUTTONDOWN, HIWORD(w) == XBUTTON1 ? "x1" : "x2", 1)
        CASE_WM_MOUSE(XBUTTONDBLCLK, HIWORD(w) == XBUTTON1 ? "x1" : "x2", 2)

    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE:
        if (gPrintMouseCursor) {
            printed = printf("mouse cursor -> %d,%d %s%s: %s\n", (SHORT)LOWORD(l), (SHORT)HIWORD(l),
                             injected ? "[J] " : "", msg == WM_NCMOUSEMOVE ? "[N] " : "", source);
        }
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (gPrintMouse) {
            printed = printf("mouse %swheel -> %d %s: %s\n", msg == WM_MOUSEHWHEEL ? "h" : "",
                             (SHORT)HIWORD(w), injected ? "[J] " : "", source);
        }
        break;
    }

    if (printed >= 0 && xinfo) {
        printf("(xinfo: %" PRIxPTR " : %s)\n", xinfo, source);
    }
    if (printed >= 0 && time != -1 && gPrintTime) {
        printf("(time: %u : %s)\n", time, source);
    }
}

LRESULT CALLBACK LowKeyHook(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT *data = (KBDLLHOOKSTRUCT *)lParam;

    TestWindowMsg((int)wParam, data->vkCode, (data->flags << 24) | (data->scanCode << 16), data->time,
                  (data->flags & LLKHF_INJECTED), data->dwExtraInfo, nCode >= 0 ? "lowhook" : "D_lowhook");

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK HighKeyHook(int nCode, WPARAM wParam, LPARAM lParam) {
    int msg = (HIWORD(lParam) & KF_UP) ? WM_KEYUP : WM_KEYDOWN;

    TestWindowMsg(msg, wParam, lParam, -1, false, 0, nCode >= 0 ? "hook" : "D_hook"); // no way to get time in high hooks?

    AssertNotEquals("hook.key.high.assum", lParam & 0xc0000000, 0x80000000); // illegal event

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

template <class HookStruct>
void TestWindowMouseHook(int msg, HookStruct *data, LONG time, bool injected, ULONG_PTR xinfo, const char *source) {
    LONG coords = MAKELONG(data->pt.x, data->pt.y);
    UINT mouseData = 0;
    switch (msg) {
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_NCXBUTTONDOWN:
    case WM_NCXBUTTONUP:
    case WM_NCXBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        mouseData = data->mouseData; // fallthrough
    default:
        TestWindowMsg(msg, mouseData, coords, time, injected, xinfo, source);
        break;
    }
}

LRESULT CALLBACK LowMouseHook(int nCode, WPARAM wParam, LPARAM lParam) {
    MSLLHOOKSTRUCT *data = (MSLLHOOKSTRUCT *)lParam;

    TestWindowMouseHook((int)wParam, data, data->time, data->flags & LLMHF_INJECTED,
                        data->dwExtraInfo, nCode >= 0 ? "lowhook" : "D_lowhook");

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK HighMouseHook(int nCode, WPARAM wParam, LPARAM lParam) {
    MOUSEHOOKSTRUCTEX *data = (MOUSEHOOKSTRUCTEX *)lParam;

    TestWindowMouseHook((int)wParam, data, -1, false, data->dwExtraInfo, nCode >= 0 ? "hook" : "D_hook");

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void RegisterHook(bool keyboard, bool mouse, int count, bool low, DWORD thread) {
    CreateThread([=] {
        for (int i = 0; i < count; i++) {
            if (keyboard) {
                HHOOK keyHook = SetWindowsHookExW(low ? WH_KEYBOARD_LL : WH_KEYBOARD, low ? LowKeyHook : HighKeyHook, thread ? nullptr : GetModuleHandle(nullptr), thread);
                AssertNotEquals("hook.k", (intptr_t)keyHook, 0);
            }

            if (mouse) {
                HHOOK mouseHook = SetWindowsHookExW(low ? WH_MOUSE_LL : WH_MOUSE, low ? LowMouseHook : HighMouseHook, thread ? nullptr : GetModuleHandle(nullptr), thread);
                AssertNotEquals("hook.m", (intptr_t)mouseHook, 0);
            }
        }

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    });
}

void RegisterHotKeys() {
    CreateThread([=] {
        for (int i = VK_F1; i < VK_F24; i++) {
            RegisterHotKey(NULL, i, 0, i);
        }

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_HOTKEY) {
                printf("hotkey %d\n", (int)msg.wParam);
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    });
}

void SendInputs() {
    CreateThread([=] {
        // while (true)
        {
            Sleep(3000);

            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RIGHT;
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            input.ki.wScan = MapVirtualKeyW(VK_RIGHT, MAPVK_VK_TO_VSC);
            input.ki.dwExtraInfo = 0x9991;
            SendInput(1, &input, sizeof(INPUT));

            Sleep(3000);

            input.ki.dwFlags |= KEYEVENTF_KEYUP;
            input.ki.dwExtraInfo = 0x9992;
            SendInput(1, &input, sizeof(INPUT));

            Sleep(3000);

            input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_XDOWN;
            input.mi.mouseData = XBUTTON1;
            input.mi.dwExtraInfo = 0x9993;
            SendInput(1, &input, sizeof(INPUT));

            Sleep(3000);

            input.mi.dwFlags = MOUSEEVENTF_XUP;
            SendInput(1, &input, sizeof(INPUT));

            Sleep(3000);

            input.mi = {};
            input.mi.dx = input.mi.dy = 100;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            input.mi.dwExtraInfo = 0x9994;
            SendInput(1, &input, sizeof(INPUT));

            Sleep(3000);

            input.mi.dwFlags |= MOUSEEVENTF_ABSOLUTE;
            input.mi.dwExtraInfo = 0x9995;
            SendInput(1, &input, sizeof(INPUT));

            Sleep(3000);

            input.mi.dwFlags |= MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_WHEEL;
            input.mi.mouseData = 120;
            input.mi.dwExtraInfo = 0x9996;
            SendInput(1, &input, sizeof(INPUT));
        }
    });
}

enum { MeasureLatencyXInfo = 0x12345 };

void MeasureLatency() {
    CreateThread([=] {
        bool first = true;
        while (true) {
            if (GetForegroundWindow() == gTestWindow && gTestWindow) {
                INPUT input;
                input.type = INPUT_KEYBOARD;
                input.ki = {};
                input.ki.wVk = 'M';
                input.ki.wScan = MapVirtualKeyW('M', MAPVK_VK_TO_VSC);
                input.ki.dwFlags = first ? 0 : KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = MeasureLatencyXInfo;
                gKeyLatency.LastTime = GetPerfCounter();
                SendInput(1, &input, sizeof(INPUT));
                Sleep(5);

                input.type = INPUT_MOUSE;
                input.mi = {};
                input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                input.mi.mouseData = WHEEL_DELTA;
                input.mi.dwExtraInfo = MeasureLatencyXInfo + 1;
                gMouseLatency.LastTime = GetPerfCounter();
                SendInput(1, &input, sizeof(INPUT));

                first = !first;
            }
            Sleep(15);
        }
    });
}

void MeasureLatencyInMsg() {
    auto xinfo = GetMessageExtraInfo();
    if (xinfo == MeasureLatencyXInfo || xinfo == MeasureLatencyXInfo + 1) {
        LatencyMeasure *latency = xinfo == MeasureLatencyXInfo ? &gKeyLatency : &gMouseLatency;
        double delay = GetPerfDelay(latency->LastTime);

        latency->SumPrev += delay;
        latency->NumPrev++;

        if (latency->NumPrev == LatencyMeasure::MaxPrev) {
            printf("%s latency: %lf\n", latency->Name, latency->SumPrev / latency->NumPrev);
            latency->SumPrev = 0.0;
            latency->NumPrev = 0;
        }
    }
}

void WasteCpu() {
    SYSTEM_INFO info = GetOutput(GetSystemInfo);
    for (DWORD i = 0; i < info.dwNumberOfProcessors * 2; i++) {
        CreateThread([] {
            while (true) {
            }
        });
    }
}

bool GetCIM() {
    return GetOutput(GetCurrentInputMessageSource).originId == IMO_INJECTED;
}

LRESULT CALLBACK TestWindowProc(HWND win, UINT msg, WPARAM w, LPARAM l) {
    TestWindowMsg(msg, w, l, GetMessageTime(), GetCIM(), GetMessageExtraInfo(), "wndproc");
    return gTestWindowProcBase(win, msg, w, l);
}

template <class intT>
void TestStateMsg(int key, intT value, intT *oldValue, const char *source) {
    if (value != *oldValue) {
        bool isMouse = IsVkMouseButton(key);
        if (isMouse ? gPrintMouse : gPrintKeyboard) {
            printf("%s %d -> %d (%d) : %s\n", isMouse ? "mouse" : "key", key, (std::make_signed_t<intT>)value < 0, value & 1, source);
        }
        *oldValue = value;
    }
}

void ResistCursorHideInit(HWND window) {
    for (int i = 0; i < 5; i++) {
        ShowCursor(true);
    }

#if ZERO
    RECT rect = {};
    GetWindowRect(window, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    HWND topwin = CreateWindowExW(WS_EX_TOPMOST, L"EDIT", L"", WS_BORDER, rect.left + w / 2 - 50, rect.top + h / 2 - 50, 100, 100, 0, NULL, NULL, NULL);
    ShowWindow(topwin, SW_SHOWNOACTIVATE);
#endif
}

void ResistCursorHide(HWND window) {
    int newValue = ShowCursor(true);
    if (newValue > 1000) {
        ShowCursor(false); // for sanity purposes (would other apps be so kind?)
    }
    AssertEquals("resist.sc", newValue > 0, true);

    POINT cursorPos;
    if (GetCursorPos(&cursorPos) &&
        WindowFromPoint(cursorPos) == window) {
        CURSORINFO ci;
        ci.cbSize = sizeof(ci);
        AssertEquals("resist.gci", GetCursorInfo(&ci), TRUE);
        AssertEquals("resist.gci.f", ci.flags, CURSOR_SHOWING);
    }

    AssertEquals("resist.cc", ClipCursor(nullptr), TRUE);
    RECT clip;
    AssertEquals("resist.gcc", GetClipCursor(&clip), TRUE);
    AssertEquals("resist.gcc.x", clip.left, GetSystemMetrics(SM_XVIRTUALSCREEN));
    AssertEquals("resist.gcc.y", clip.top, GetSystemMetrics(SM_YVIRTUALSCREEN));
    AssertEquals("resist.gcc.w", clip.right - clip.left, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    AssertEquals("resist.gcc.h", clip.bottom - clip.top, GetSystemMetrics(SM_CYVIRTUALSCREEN));
}

DWORD ShowTestWindow(int count, bool resistHideCursor) {
    DWORD threadId = 0;
    for (int i = 0; i < count; i++) {
        threadId = CreateThread([=]() mutable {
            short keys[0x100] = {};
            short asynckeys[0x100] = {};
            BYTE fullkeys[0x100] = {};
            GetKeyboardState(fullkeys);
            for (int i = 0; i < 0x100; i++) {
                keys[i] = GetKeyState(i);
                asynckeys[i] = GetAsyncKeyState(i);
            }
            POINT cursorPos = GetOutput(GetCursorPos);

            int width = GetSystemMetrics(SM_CXSCREEN);
            int height = GetSystemMetrics(SM_CYSCREEN);
            int style = ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_OVERLAPPEDWINDOW;

            HWND window = CreateWindowW(L"EDIT", L"TEST", style, width - 500, height - 500, 250, 250, 0, NULL, NULL, NULL);
            gTestWindow = window;
            SetWindowTextW(window, L"");

            gTestWindowProcBase = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
            SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)TestWindowProc);
            SetTimer(window, 1, 100, nullptr);
            ShowWindow(window, SW_SHOW);

            if (resistHideCursor) {
                ResistCursorHideInit(window);
            }

            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (i == 0) {
                    MeasureLatencyInMsg();
                }

                if (resistHideCursor && msg.message == WM_MOUSEMOVE) {
                    ResistCursorHide(msg.hwnd);
                }

                POINT newPos;
                AssertEquals("test.gcp", GetCursorPos(&newPos), TRUE);
                if (newPos.x != cursorPos.x || newPos.y != cursorPos.y) {
                    if (gPrintMouseCursor) {
                        printf("mouse cursor -> %d,%d : state\n", newPos.x, newPos.y);
                    }
                    cursorPos = newPos;
                }

                for (int i = 0; i < 0x100; i++) {
                    TestStateMsg(i, GetKeyState(i), &keys[i], "state");
                    TestStateMsg(i, GetAsyncKeyState(i), &asynckeys[i], "asyncstate");
                }

                BYTE newkeys[0x100] = {};
                AssertEquals("test.gks", GetKeyboardState(newkeys), TRUE);
                for (int i = 0; i < 0x100; i++) {
                    TestStateMsg(i, newkeys[i], &fullkeys[i], "fullstate");
                }

                TestWindowMsg(msg.message, msg.wParam, msg.lParam, GetMessageTime(), GetCIM(), GetMessageExtraInfo(), "msgloop");

                if (gRegRawActive) {
                    ProcessRawInputMsgs(msg);
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        });
    }
    return threadId;
}

void RegisterRawInput(bool keyboard, bool mouse, bool noLegacy, bool noHotkey, bool hotkey, bool captureMouse, bool readBuffer, bool readWait) {
    if (!keyboard && !mouse) {
        return;
    }

    CreateThread([=] {
        auto readCb = [prevX = 0, prevY = 0](RAWINPUT *ri) mutable {
            bool injected = ri->header.hDevice == nullptr;

            if (ri->header.dwType == RIM_TYPEKEYBOARD) {
                WORD flags = ri->data.keyboard.MakeCode;
                if (ri->data.keyboard.Flags & RI_KEY_BREAK) {
                    flags |= KF_UP;
                } else if (ri->data.keyboard.Flags & RI_KEY_MAKE) {
                    flags |= KF_REPEAT;
                }
                if (ri->data.keyboard.Flags & RI_KEY_E0) {
                    flags |= KF_EXTENDED;
                }
                if ((ri->data.keyboard.Flags & RI_KEY_E1) && gPrintKeyboard) {
                    printf("[XXX] ");
                }
                TestWindowMsg(ri->data.keyboard.Message, ri->data.keyboard.VKey, flags << 16, GetMessageTime(),
                              injected, ri->data.keyboard.ExtraInformation, "raw");
            } else if (ri->header.dwType == RIM_TYPEMOUSE) {
                int printed = -1;
                const char *injectedStr = injected ? "[J] " : "";

                if (gPrintMouseCursor) {
                    if ((ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
                        if (ri->data.mouse.lLastX != prevX || ri->data.mouse.lLastY != prevY) {
                            printed = printf("mouse cursor -> %d,%d %s: raw\n", ri->data.mouse.lLastX, ri->data.mouse.lLastY, injectedStr);
                        }
                        prevX = ri->data.mouse.lLastX;
                        prevY = ri->data.mouse.lLastY;
                    } else {
                        if (ri->data.mouse.lLastX != 0 || ri->data.mouse.lLastY != 0) {
                            printed = printf("mouse cursor +-> %d,%d %s: raw\n", ri->data.mouse.lLastX, ri->data.mouse.lLastY, injectedStr);
                        }
                    }
                }

                if (gPrintMouse) {
                    if (ri->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                        printed |= printf("mouse wheel -> %d %s: raw\n", (SHORT)ri->data.mouse.usButtonData, injectedStr);
                    }
                    if (ri->data.mouse.usButtonFlags & RI_MOUSE_HWHEEL) {
                        printed |= printf("mouse hwheel -> %d %s: raw\n", (SHORT)ri->data.mouse.usButtonData, injectedStr);
                    }
                    if (ri->data.mouse.usButtonFlags & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP)) {
                        printed |= printf("mouse l -> %d %s: raw\n", (ri->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0, injectedStr);
                    }
                    if (ri->data.mouse.usButtonFlags & (RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP)) {
                        printed |= printf("mouse r -> %d %s: raw\n", (ri->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0, injectedStr);
                    }
                    if (ri->data.mouse.usButtonFlags & (RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP)) {
                        printed |= printf("mouse m -> %d %s: raw\n", (ri->data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0, injectedStr);
                    }
                    if (ri->data.mouse.usButtonFlags & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP)) {
                        printed |= printf("mouse x1 -> %d %s: raw\n", (ri->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) != 0, injectedStr);
                    }
                    if (ri->data.mouse.usButtonFlags & (RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP)) {
                        printed |= printf("mouse x2 -> %d %s: raw\n", (ri->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) != 0, injectedStr);
                    }
                }

                if (printed >= 0 && ri->data.mouse.ulExtraInformation) {
                    printf("(xinfo: %x : raw)\n", ri->data.mouse.ulExtraInformation);
                }
                if (printed >= 0 && gPrintTime) {
                    printf("(time: %u : raw)\n", GetMessageTime());
                }
            }
        };

        HWND window = gRegRawActive ? nullptr : CreateWindowW(L"STATIC", L"MSG", 0, 0, 0, 0, 0, 0, NULL, NULL, NULL);

        int flags = (noLegacy ? RIDEV_NOLEGACY : 0);
        flags |= (gRegRawActive ? 0 : RIDEV_INPUTSINK);

        RAWINPUTDEVICE rid[2] = {};
        int ridI = 0, kI = -1, mI = -1;
        if (keyboard) {
            kI = ridI;
            rid[ridI].usUsagePage = HID_USAGE_PAGE_GENERIC;
            rid[ridI].usUsage = HID_USAGE_GENERIC_KEYBOARD;
            rid[ridI].hwndTarget = window;
            rid[ridI].dwFlags = RIDEV_DEVNOTIFY | flags | (noHotkey ? RIDEV_NOHOTKEYS : 0) | (hotkey ? RIDEV_APPKEYS : 0);
            ridI++;
        }
        if (mouse) {
            mI = ridI;
            rid[ridI].usUsagePage = HID_USAGE_PAGE_GENERIC;
            rid[ridI].usUsage = HID_USAGE_GENERIC_MOUSE;
            rid[ridI].hwndTarget = window;
            rid[ridI].dwFlags = RIDEV_DEVNOTIFY | flags | (captureMouse ? RIDEV_CAPTUREMOUSE : 0);
            ridI++;
        }

        AssertEquals("ri.reg", RegisterRawInputDevices(rid, ridI, sizeof(RAWINPUTDEVICE)), TRUE);

        RAWINPUTDEVICE irid[4];
        UINT count = 4;
        UINT actualCount = GetRegisteredRawInputDevices(irid, &count, sizeof(RAWINPUTDEVICE));
        AssertNotEquals("ri.greg.bad.errno", actualCount, (UINT)-1);
        UINT found = 0;
        for (UINT i = 0; i < actualCount; i++) {
            int index = -1;
            switch (irid[i].usUsage) {
            case HID_USAGE_GENERIC_KEYBOARD:
                index = kI;
                break;
            case HID_USAGE_GENERIC_MOUSE:
                index = mI;
                break;
            }

            if (index >= 0) {
                AssertEquals("ri.greg.up", irid[i].usUsagePage, rid[index].usUsagePage);
                AssertEquals("ri.greg.u", irid[i].usUsage, rid[index].usUsage);
                AssertEquals("ri.greg.w", (intptr_t)irid[i].hwndTarget, (intptr_t)rid[index].hwndTarget);
                AssertEquals("ri.greg.f", irid[i].dwFlags & 0xfff, rid[index].dwFlags & 0xfff);
                found++;
            }
        }
        AssertEquals("ri.greg.count", found, ridI);

        if (gRegRawActive) {
            gActiveRawReadCbs.push_back(readCb);
        } else {
            ProcessRawInputMsgs(readCb, readBuffer, readWait);
        }
    });
}

void CreateNewProcess(bool isChild, bool otherBitness, bool withIO, const string &args) {
    if (isChild) {
        if (withIO) {
            char buffer[16];
            fread(buffer, 1, 12, stdin);
            AssertEquals("child.in", buffer, "hello stdin");
            AssertEquals("child.out", fwrite("hello stdout", 1, 13, stdout), 13);
            AssertEquals("child.err", fwrite("hello stderr", 1, 13, stderr), 13);
            fflush(stdout);
            fflush(stderr);
        }
        return;
    } else {
        Path ownPath = PathGetModulePath(nullptr);
        if (otherBitness) {
            wstring ownPathWStr(ownPath);
            ReplaceWithOtherBitness(&ownPathWStr);
            ownPath = ownPathWStr.c_str();
        }

        PROCESS_INFORMATION pi;
        Path cmdLine = args.empty() ? PathConcatRaw(GetCommandLineW(), L" --child") : PathConcatRaw(L"myinput_test.exe ", PathFromStr(args.c_str()));

        if (withIO) {
            HANDLE outrd, outwr, errrd, errwr, inrd, inwr;
            AssertEquals("crpr.in.crp", CreatePipe(&inrd, &inwr, nullptr, 0), TRUE);
            AssertEquals("crpr.out.crp", CreatePipe(&outrd, &outwr, nullptr, 0), TRUE);
            AssertEquals("crpr.err.crp", CreatePipe(&errrd, &errwr, nullptr, 0), TRUE);
            AssertEquals("crpr.in.shi", SetHandleInformation(inrd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT), TRUE);
            AssertEquals("crpr.out.shi", SetHandleInformation(outwr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT), TRUE);
            AssertEquals("crpr.err.shi", SetHandleInformation(errwr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT), TRUE);

            STARTUPINFOW si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = inrd;
            si.hStdOutput = outwr;
            si.hStdError = errwr;

            AssertEquals("crpr", CreateProcessW(ownPath, cmdLine, nullptr, nullptr, true, 0, nullptr, nullptr, &si, &pi), TRUE);

            DWORD written;
            AssertEquals("crpr.in.wr", WriteFile(inwr, "hello stdin", 12, &written, nullptr), TRUE);
            AssertEquals("crpr.in.wr.sz", written, 12);

            DWORD read;
            char buffer[16];
            AssertEquals("crpr.out.rd", ReadFile(outrd, buffer, 13, &read, nullptr), TRUE);
            AssertEquals("crpr.out.rd.d", buffer, "hello stdout");
            AssertEquals("crpr.err.rd", ReadFile(errrd, buffer, 13, &read, nullptr), TRUE);
            AssertEquals("crpr.err.rd.d", buffer, "hello stderr");
        } else {
            STARTUPINFOW si = {};
            si.cb = sizeof(si);
            AssertEquals("crpr", CreateProcessW(ownPath, cmdLine, nullptr, nullptr, false, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi), TRUE);
        }

        CloseHandle(pi.hThread);
        gSubProcess = pi.hProcess;
        SetConsoleCtrlHandler([](DWORD type) -> BOOL {
            TerminateProcess(gSubProcess, 1000);
            return TRUE;
        },
                              TRUE);
    }
}

DWORD CALLBACK DeviceChangeCMCb(HCMNOTIFICATION notify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD size) {
    switch (data->FilterType) {
    case CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE: {
        AssertEquals("cm.ntf.intf.ctxt", (intptr_t)context, 0x123);

        GUID hidGuid = GetOutput(HidD_GetHidGuid);

        AssertEquals("cm.ntf.intf.guid", IsEqualGUID(data->u.DeviceInterface.ClassGuid, hidGuid), TRUE);

        if (gPrintDeviceChange) {
            switch (action) {
            case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
                printf("event : cm device intf add : %ls\n", data->u.DeviceInterface.SymbolicLink);
                break;
            case CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL:
                printf("event : cm device intf remove : %ls\n", data->u.DeviceInterface.SymbolicLink);
                break;
            default:
                AssertEquals("cm.ntf.intf.act", true, false);
            }
        }
        break;
    }

    case CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE: {
        if (action == CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE) {
            AssertEquals("cm.ntf.hdl.close", CloseHandle((HANDLE)context), TRUE);
            CreateThread([=] { AssertEquals("cm.ntf.hdl.unreg", CM_Unregister_Notification(notify), CR_SUCCESS); });
        }

        if (gPrintDeviceChange) {
            switch (action) {
            case CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE:
                printf("event : cm device handle remove\n");
                break;
            default:
                printf("event : cm device handle event : %d\n", action);
            }
        }
        break;
    }

    case CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE: {
        AssertEquals("cm.ntf.inst.ctxt", (intptr_t)context, 0x124);

        if (gPrintDeviceChange) {
            switch (action) {
            case CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED:
                printf("event : cm device inst enum : %ls\n", data->u.DeviceInstance.InstanceId);
                break;
            case CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED:
                printf("event : cm device inst start : %ls\n", data->u.DeviceInstance.InstanceId);
                break;
            case CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED:
                printf("event : cm device inst remove : %ls\n", data->u.DeviceInstance.InstanceId);
                break;
            default:
                AssertEquals("cm.ntf.inst.act", true, false);
            }
        }
        break;
    }

    default:
        AssertEquals("cm.ntf.ft", true, false);
    }

    return ERROR_SUCCESS;
}

LRESULT CALLBACK NotifyWindowProc(HWND win, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DEVICECHANGE) {
        DEV_BROADCAST_HDR *header = ((DEV_BROADCAST_HDR *)l);
        bool unicode = IsWindowUnicode(win);
        const char *type = unicode ? "wwin" : "awin";

        const char *act = nullptr;
        switch (w) {
        case DBT_DEVNODES_CHANGED:
            act = "nodes change";
            break;
        case DBT_DEVICEARRIVAL:
            act = "add";
            break;
        case DBT_DEVICEQUERYREMOVE:
            act = "remove query";
            break;
        case DBT_DEVICEQUERYREMOVEFAILED:
            act = "remove query fail";
            break;
        case DBT_DEVICEREMOVEPENDING:
            act = "remove pending";
            break;
        case DBT_DEVICEREMOVECOMPLETE:
            act = "remove";
            break;
        case DBT_DEVICETYPESPECIFIC:
            act = "type event";
            break;
        case DBT_CUSTOMEVENT:
            act = "custom event";
            break;
        default:
            act = "unk";
            break;
        }

        if (w & 0x8000) {
            AssertNotEquals("dev.ntf.l", (intptr_t)header, 0);

            switch (header->dbch_devicetype) {
            case DBT_DEVTYP_DEVICEINTERFACE: {
                GUID hidGuid = GetOutput(HidD_GetHidGuid);

                auto devintf = (DEV_BROADCAST_DEVICEINTERFACE_W *)header;
                AssertEquals("cm.ntf.intf.guid", IsEqualGUID(devintf->dbcc_classguid, hidGuid), TRUE);

                if (gPrintDeviceChange) {
                    if (unicode) {
                        printf("event : %s, device intf %s: %ls\n", type, act, devintf->dbcc_name);
                    } else {
                        printf("event : %s, device intf %s: %s\n", type, act, ((DEV_BROADCAST_DEVICEINTERFACE_A *)header)->dbcc_name);
                    }
                }
            } break;

            case DBT_DEVTYP_HANDLE: {
                auto devhandle = (DEV_BROADCAST_HANDLE *)header;

                if (w == DBT_DEVICEREMOVECOMPLETE) {
                    AssertEquals("cm.ntf.hdl.close", CloseHandle(devhandle->dbch_handle), TRUE);
                    AssertEquals("cm.ntf.hdl.unreg", UnregisterDeviceNotification(devhandle->dbch_hdevnotify), TRUE);
                }

                if (gPrintDeviceChange) {
                    printf("event : %s device hdl %s\n", type, act);
                }
            } break;

            default:
                if (gPrintDeviceChange) {
                    printf("event : %s device unk %s: %d\n", type, act, header->dbch_devicetype);
                }
            }
        } else {
            AssertEquals("dev.ntf.l", l, 0);

            if (gPrintDeviceChange) {
                printf("event : %s device %s\n", type, act);
            }
        }
    }

    return DefWindowProc(win, msg, w, l);
}

void CreateNotifyWindow() {
    HANDLE event = CreateEventW(nullptr, true, false, nullptr);

    CreateThread([event] {
        HWND window = CreateWindowW(L"STATIC", L"NOTIFY", 0, 0, 0, 0, 0, 0, NULL, NULL, NULL);
        SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)NotifyWindowProc);
        AssertEquals("ntf.win.uni", IsWindowUnicode(window), TRUE);

        HWND windowA = CreateWindowA("STATIC", "NOTIFY", 0, 0, 0, 0, 0, 0, NULL, NULL, NULL);
        SetWindowLongPtrA(windowA, GWLP_WNDPROC, (LONG_PTR)NotifyWindowProc);
        AssertEquals("ntf.win.ansi", IsWindowUnicode(windowA), FALSE);

        gNotifyWin = window;
        gNotifyWinA = windowA;
        SetEvent(event);

        GUID hidGuid = GetOutput(HidD_GetHidGuid);

        DEV_BROADCAST_DEVICEINTERFACE_A dbc;
        ZeroMemory(&dbc, sizeof(dbc));
        dbc.dbcc_size = sizeof(dbc);
        dbc.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        dbc.dbcc_classguid = hidGuid;
        AssertNotEquals("dev.ntf.intf", (intptr_t)RegisterDeviceNotificationA(gNotifyWin, &dbc, 0), NULL);
        AssertNotEquals("dev.ntf.intf", (intptr_t)RegisterDeviceNotificationW(gNotifyWinA, &dbc, 0), NULL);

        CM_NOTIFY_FILTER filter;
        ZeroMemory(&filter, sizeof(filter));
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = hidGuid;
        HCMNOTIFICATION notify;
        AssertEquals("cm.ntf.intf", CM_Register_Notification(&filter, (void *)0x123, DeviceChangeCMCb, &notify), CR_SUCCESS);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            AssertNotEquals("dev.chg.not.pushed", msg.message, WM_DEVICECHANGE);

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    });

    WaitForSingleObject(event, INFINITE);
}

void PrintWmiProp(BSTR key, const VARIANT &value, CIMTYPE type) {
    printf("  %ls -> ", key);

    switch (type & ~CIM_FLAG_ARRAY) {
    case CIM_EMPTY:
        printf("(null) ");
        break;
    case CIM_SINT8:
        printf("(int8) ");
        break;
    case CIM_SINT16:
        printf("(int16) ");
        break;
    case CIM_SINT32:
        printf("(int32) ");
        break;
    case CIM_SINT64:
        printf("(int64) ");
        break;
    case CIM_UINT8:
        printf("(uint8) ");
        break;
    case CIM_UINT16:
        printf("(uint16) ");
        break;
    case CIM_UINT32:
        printf("(uint32) ");
        break;
    case CIM_UINT64:
        printf("(uint64) ");
        break;
    case CIM_REAL32:
        printf("(real32) ");
        break;
    case CIM_REAL64:
        printf("(real64) ");
        break;
    case CIM_BOOLEAN:
        printf("(bool) ");
        break;
    case CIM_CHAR16:
        printf("(char) ");
        break;
    case CIM_STRING:
        printf("(str) ");
        break;
    case CIM_DATETIME:
        printf("(time) ");
        break;
    case CIM_REFERENCE:
        printf("(ref) ");
        break;
    case CIM_OBJECT:
        printf("(obj) ");
        break;
    default:
        printf("(??? %d)", type);
        break;
    }

    if (value.vt == VT_NULL) {
        printf("null");
    } else if (type & CIM_FLAG_ARRAY) {
        printf("{");
        byte *ptr = (byte *)value.parray->pvData;
        for (ULONG i = 0; i < value.parray->rgsabound[0].cElements; i++) {
            if (i > 0) {
                printf(", ");
            }

            switch (type & ~CIM_FLAG_ARRAY) {
            case CIM_STRING:
                printf("\"%ls\"", *(wchar_t **)ptr);
                break;
            default:
                printf("???");
                break;
            }

            ptr += value.parray->cbElements;
        }
        printf("}");
    } else {
        switch (type) {
        case CIM_SINT32:
        case CIM_UINT32:
            printf("%d", value.intVal);
            break;
        case CIM_BOOLEAN:
            printf(value.boolVal ? "true" : "false");
            break;
        case CIM_STRING:
            printf("\"%ls\"", value.bstrVal);
            break;
        default:
            printf("???");
            break;
        }
    }

    printf("\n");
}

bool ProcessWmiDevice(IWbemClassObject *obj, bool print = false, bool printAll = false) {
    Variant path;
    AssertEquals("wb.objpath", obj->Get(L"__PATH", 0, &path, nullptr, nullptr), S_OK);
    AssertTrue("wb.objpath.str", path.IsBstr());

    Variant cls;
    AssertEquals("wb.objget", obj->Get(L"PNPClass", 0, &cls, nullptr, nullptr), S_OK);

    bool isHid = cls.IsBstr() && tstreq(cls.GetBstr(), L"HIDClass");
    if (!printAll && !isHid) {
        return false;
    }

    if (print) {
        printf("WMI Device: %ls\n", path.GetBstr());
    }

    AssertEquals("wb.objenum", obj->BeginEnumeration(0), S_OK);
    while (true) {
        Bstr key;
        Variant value;
        CIMTYPE type;
        HRESULT res = obj->Next(0, &key, &value, &type, nullptr);
        if (res == WBEM_S_NO_MORE_DATA) {
            break;
        }

        AssertEquals("wb.objnext", res, S_OK);

        if (print) {
            PrintWmiProp(key, value, type);
        }
    }

#if ZERO
    ComRef<IWbemObjectAccess> access;
    AssertEquals("wb.objacc", obj->QueryInterface(&access), S_OK);

    long phdl = 33;
    AssertEquals("wb.acc.hdl", access->GetPropertyHandle(L"__PATH", nullptr, &phdl), S_OK);

    DWORD dw;
    AssertEquals("wb.acc.bad", access->ReadDWORD(phdl, &dw), WBEM_E_INVALID_PARAMETER);

    wchar_t path2[0x1000];
    long path2Sz;
    AssertEquals("wb.acc.path", access->ReadPropertyValue(phdl, sizeof(path2), &path2Sz, (byte *)path2), S_OK);
    AssertEquals("wb.acc.sz", path2Sz, (wcslen(path.GetBstr()) + 1) * sizeof(wchar_t));
    AssertEquals("wb.acc.val", path2, path.GetBstr());
#endif

    return isHid;
}

class WbemObjectSinkImpl : public ComBase<IWbemObjectSink> {
    function<void(long, IWbemClassObject **)> mIndicate;
    function<void(HRESULT, BSTR, IWbemClassObject *, long)> mSetStatus;
    atomic<long> mTotalIndicated = 0;
    atomic<long> mGrandTotal = 0;

public:
    void Init(function<void(long, IWbemClassObject **)> &&indicate,
              function<void(HRESULT, BSTR, IWbemClassObject *, long)> &&setStatus) {
        mIndicate = indicate;
        mSetStatus = setStatus;
    }

    STDMETHODIMP Indicate(long lObjectCount, IWbemClassObject **apObjArray) override {
        mTotalIndicated += lObjectCount;
        mGrandTotal += lObjectCount;
        mIndicate(lObjectCount, apObjArray);
        return S_OK;
    }

    STDMETHODIMP SetStatus(long lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject *pObjParam) override {
        switch (lFlags) {
        case WBEM_STATUS_COMPLETE:
            mSetStatus(hResult, strParam, pObjParam, mTotalIndicated.exchange(0));
            return S_OK;
        default:
            printf("sink status %lx ; %lx\n", lFlags, hResult);
            return S_OK;
        }
    }

    long GrandTotal() { return mGrandTotal; }
};

template <class TIntf>
ComRef<TIntf> RemarshalIntf(TIntf *intf, bool check = true) {
    if (!gRemarshalWmi) {
        return intf;
    }

    if (check) {
        ComRef<IMarshal> marshal;
        AssertEquals("co.marshal.qry", intf->QueryInterface(&marshal), S_OK);
        AssertNotEquals("co.marshal.val", (uintptr_t)marshal.Get(), 0);
    }

    ComRef<IStream> stream;
    AssertEquals("co.marshal.xth", CoMarshalInterThreadInterfaceInStream(__uuidof(TIntf), intf, &stream), S_OK);

    ComRef<TIntf> outIntf;
    AssertEquals("co.unmarshal.xth", CoGetInterfaceAndReleaseStream(stream, __uuidof(TIntf), (LPVOID *)&outIntf), S_OK);
    return outIntf;
}

void ReadWmi(bool print, bool printAll) {
    constexpr int ENUM_SIZE = 1;
    constexpr int ASYNC_SIZE = 3;

    AssertEquals("co.init", CoInitializeEx(NULL, COINIT_MULTITHREADED), S_OK);

    ComRef<IWbemLocator> wbemLoc;
    AssertEquals("co.create", CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void **)&wbemLoc), S_OK);
    AssertNotEquals("co.created", (intptr_t)wbemLoc.Get(), 0);

    ComRef<IUnknown> wbemLocU;
    AssertEquals("co.createu", CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, (void **)&wbemLocU), S_OK);
    AssertNotEquals("co.createdu", (intptr_t)wbemLocU.Get(), 0);

    MULTI_QI mq[] = {
        {&IID_IUnknown},
        {&IID_IWbemLocator},
    };
    AssertEquals("co.createx", CoCreateInstanceEx(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, nullptr, 2, mq), S_OK);
    AssertEquals("co.createx.0", mq[0].hr, S_OK);
    AssertEquals("co.createx.0", mq[1].hr, S_OK);
    mq[0].pItf->Release();
    mq[1].pItf->Release();

    ComRef<IClassFactory> wbemLocCls;
    AssertEquals("co.cls", CoGetClassObject(CLSID_WbemLocator, CLSCTX_INPROC_SERVER, nullptr, IID_IClassFactory, (void **)&wbemLocCls), S_OK);
    AssertNotEquals("co.cls.got", (intptr_t)wbemLocCls.Get(), 0);

    ComRef<IWbemLocator> wbemLocC;
    AssertEquals("co.cls.create", wbemLocCls->CreateInstance(nullptr, IID_IWbemLocator, (void **)&wbemLocC), S_OK);
    AssertNotEquals("co.cls.created", (intptr_t)wbemLocC.Get(), 0);

    ComRef<IUnknown> wbemLocClsU;
    AssertEquals("co.clsu", CoGetClassObject(CLSID_WbemLocator, CLSCTX_INPROC_SERVER, nullptr, IID_IUnknown, (void **)&wbemLocClsU), S_OK);
    AssertNotEquals("co.clsu.got", (intptr_t)wbemLocClsU.Get(), 0);

    ComRef<IWbemServices> svc1;
    Bstr root(L"\\\\.\\ROOT");
    AssertEquals("wb.conn", wbemLoc->ConnectServer(root, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc1), S_OK);
    AssertNotEquals("wb.conn.val", (intptr_t)svc1.Get(), 0);
    svc1 = RemarshalIntf(svc1.Get());

    AssertEquals("wb.sec", CoSetProxyBlanket(svc1, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE), S_OK);

    ComRef<IWbemServices> svcpcp;
    AssertEquals("wb.seccp", CoCopyProxy(svc1, (IUnknown **)&svcpcp), S_OK);
    AssertNotEquals("wb.seccp.neq", (intptr_t)svcpcp.Get(), (intptr_t)svc1.Get());
    ComRef<IWbemServices> svcpcpq;
    AssertEquals("wb.seccp.q", svcpcp->QueryInterface(&svcpcpq), S_OK);
    AssertEquals("wb.seccp.eq", (intptr_t)svcpcpq.Get(), (intptr_t)svc1.Get());

    ComRef<IRpcOptions> rpco;
    svc1->QueryInterface<IRpcOptions>(&rpco);
    AssertEquals("wb.rpco", rpco->Set(svc1, COMBND_RPCTIMEOUT, RPC_C_BINDING_INFINITE_TIMEOUT), S_OK);

    ComRef<IWbemServices> svc0;
    Bstr cim(L"cimv2");
    AssertEquals("wb.open", svc1->OpenNamespace(cim, 0, nullptr, &svc0, nullptr), S_OK);
    AssertNotEquals("wb.open.done", (intptr_t)svc0.Get(), 0);

    ComRef<IWbemCallResult> svcRes;
    AssertEquals("wb.open.ss", svc1->OpenNamespace(cim, WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, nullptr, &svcRes), S_OK);
    svcRes = RemarshalIntf(svcRes.Get());

    long svcResSt;
    AssertEquals("wb.cr.stat", svcRes->GetCallStatus(WBEM_INFINITE, &svcResSt), S_OK);
    AssertEquals("wb.open.ss.res", svcResSt, S_OK);

    ComRef<IWbemServices> svc;
    AssertEquals("wb.cr.svc", svcRes->GetResultServices(0, &svc), S_OK);
    AssertNotEquals("wb.open.ss.done", (intptr_t)svc.Get(), 0);
    svc = RemarshalIntf(svc.Get());

    AssertEquals("wb.sec", CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE), S_OK);

    ComRef<IWbemClassObject> bad1;
    AssertEquals("wb.cr.bad1", svcRes->GetResultObject(0, &bad1), S_OK);
    AssertEquals("wb.cr.bad1.0", (intptr_t)bad1.Get(), 0);
    Bstr bad2;
    AssertEquals("wb.cr.bad2", svcRes->GetResultString(0, &bad2), WBEM_E_INVALID_OPERATION);

    ComRef<IEnumWbemClassObject> enm;
    Bstr clsn(L"Win32_PNPEntity");
    AssertEquals("wb.enum", svc->CreateInstanceEnum(clsn, 0, nullptr, &enm), S_OK);
    AssertNotEquals("wb.enum.val", (intptr_t)enm.Get(), 0);
    enm = RemarshalIntf(enm.Get());

    long enumTotal = 0;
    long enumHid = 0;
    while (true) {
        IWbemClassObject *objs[ENUM_SIZE] = {};
        ULONG ret;
        HRESULT res = enm->Next(WBEM_INFINITE, ENUM_SIZE, objs, &ret);
        AssertEquals("wb.next", res, ret == ENUM_SIZE ? S_OK : S_FALSE);
        if (!ret) {
            AssertEquals("wb.next.end", enm->Next(WBEM_INFINITE, ENUM_SIZE, objs, &ret), S_FALSE);
            break;
        }

        enumTotal += ret;
        for (ULONG objI = 0; objI < ret; objI++) {
            if (ProcessWmiDevice(objs[objI])) {
                enumHid++;
            }

            Variant deviceId;
            AssertEquals("wb.obj.did", objs[objI]->Get(L"deviceid", 0, &deviceId, nullptr, nullptr), S_OK);

            wstring objName = deviceId.GetBstr();
            StrReplaceAll(objName, L"\\", L"\\\\");
            StrReplaceAll(objName, L"\"", L"\\\"");
            objName = L"win32_pnpentity.deviceid=\"" + objName + L"\"";

            ComRef<IWbemClassObject> objBn;
            ComRef<IWbemCallResult> objBnSr;
            AssertEquals("wb.getobj", svc->GetObject(Bstr(objName.c_str()), 0, nullptr, &objBn, &objBnSr), S_OK);
            AssertNotEquals("wb.getobj.res", (uintptr_t)objBn.Get(), 0);
            objBn = RemarshalIntf(objBn.Get());
            objBnSr = RemarshalIntf(objBnSr.Get());

            ComRef<IWbemClassObject> objBn0;
            AssertEquals("wb.getobj.cr", objBnSr->GetResultObject(0, &objBn0), S_OK);
            AssertNotEquals("wb.getobj.cr.res", (uintptr_t)objBn0.Get(), 0);

            if (!StrContains(objName, L"'")) {
                wstring objName2 = deviceId.GetBstr();
                objName2 = L"win32_pnpentity.deviceid='" + objName2 + L"'";
                ComRef<IWbemCallResult> objBnRes;
                AssertEquals("wb.getobj.ss", svc->GetObject(Bstr(objName2.c_str()), WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, nullptr, &objBnRes), S_OK);
                objBnRes = RemarshalIntf(objBnRes.Get());

                ComRef<IWbemClassObject> objBn2;
                AssertEquals("wb.getobj.ss.cr", objBnRes->GetResultObject(WBEM_INFINITE, &objBn2), S_OK);
                AssertNotEquals("wb.getobj.ss.res", (uintptr_t)objBn2.Get(), 0);

                long objStat;
                AssertEquals("wb.getobj.ss.crs", objBnRes->GetCallStatus(WBEM_INFINITE, &objStat), S_OK);
                AssertEquals("wb.getobj.ss.stat", objStat, S_OK);

                ComRef<IWbemServices> bad1;
                Bstr bad2;
                AssertEquals("wb.getobj.ss.bad1", objBnRes->GetResultServices(WBEM_INFINITE, &bad1), S_OK);
                AssertEquals("wb.getobj.ss.bad1r", (uintptr_t)bad1.Get(), 0);
                AssertEquals("wb.getobj.ss.bad2", objBnRes->GetResultString(WBEM_INFINITE, &bad2), WBEM_E_INVALID_OPERATION);

                wstring objName3 = deviceId.GetBstr();
                objName3 = L"\\\\.\\rOOt\\cimV2:win32_pnpentity='" + objName3 + L"'";
                StrToLowerCase(objName3);

                ComRef<WbemObjectSinkImpl> asyncobj = new WbemObjectSinkImpl();
                asyncobj->Init([=](long count, IWbemClassObject **objs) {
                    AssertEquals ("wb.getobj.a.c", count, 1);
                    AssertNotEquals ("wb.getobj.a.res", (uintptr_t) objs[0], 0); }, [=](HRESULT res, BSTR str, IWbemClassObject *obj, long total) {
                    AssertTrue ("wb.getobj.a.args", !str && !obj);
                    AssertEquals ("wb.getobj.a.res", res, S_OK); });
                AssertEquals("wb.getobj.a", svc1->GetObjectAsync(Bstr(objName3.c_str()), 0, nullptr, asyncobj), S_OK);
            }

            objs[objI]->Release();
        }
    }

    ComRef<IEnumWbemClassObject> enmclone;
    AssertEquals("wb.enum.cl", enm->Clone(&enmclone), S_OK);
    enmclone = RemarshalIntf(enmclone.Get());
    AssertEquals("wb.enum.skend", enmclone->Skip(WBEM_INFINITE, 1), S_FALSE);
    AssertEquals("wb.enum.reset", enmclone->Reset(), S_OK);
    AssertEquals("wb.enum.sk1", enmclone->Skip(WBEM_INFINITE, 1), S_OK);

    AssertEquals("wb.enum.reset", enmclone->Reset(), S_OK);
    ComRef<WbemObjectSinkImpl> enumasync = new WbemObjectSinkImpl();
    enumasync->Init([=](long count, IWbemClassObject **objs) {
        for (long objI = 0; objI < count; objI++){
            ProcessWmiDevice (objs[objI], print, printAll);
} }, [=](HRESULT res, BSTR str, IWbemClassObject *obj, long total) {
        AssertTrue ("wb.enum.nexta.args", !str && !obj);
        AssertTrue ("wb.enum.nexta.res", res == S_OK || (res == S_FALSE && total != ENUM_SIZE));

        if (res == S_OK)
        {
            if (!total){ return; // happens from S_FALSE branch NextAsync call...
}

            CreateThread ([=]
            {
                HRESULT res2 = enmclone->NextAsync (ENUM_SIZE, enumasync);
                AssertTrue ("wb.enum.nexta.more", res2 == S_OK || res2 == S_FALSE);
            });
        }
        else
        {
            CreateThread ([=]
            {
                HRESULT res2 = enmclone->NextAsync (ENUM_SIZE, enumasync);
                AssertEquals ("wb.enum.nexta.nomore", res2, S_FALSE);
                AssertEquals ("wb.enum.nexta.gtotal", enumTotal, enumasync->GrandTotal ());
            });
        } });
    for (int i = 0; i < ASYNC_SIZE; i++) {
        AssertEquals("wb.enum.nexta", enmclone->NextAsync(ENUM_SIZE, enumasync), S_OK);
    }

    ComRef<WbemObjectSinkImpl> asyncenum = new WbemObjectSinkImpl();
    asyncenum->Init([=](long count, IWbemClassObject **objs) {
        for (long objI = 0; objI < count; objI++){
            ProcessWmiDevice (objs[objI]);
} }, [=](HRESULT res, BSTR str, IWbemClassObject *obj, long total) {
        AssertTrue ("wb.aenum.args", !str && !obj);
        AssertEquals ("wb.aenum.res", res, S_OK);
        AssertEquals ("wb.aenum.gtotal", enumTotal, asyncenum->GrandTotal ()); });

    AssertEquals("wb.aenum", svc->CreateInstanceEnumAsync(clsn, 0, nullptr, asyncenum), S_OK);

    long queryTotal = 0;
    ComRef<IEnumWbemClassObject> queryEnum;
    AssertEquals("wb.query", svc->ExecQuery(Bstr(L"WQL"), Bstr(L"select __PATH, PNPClass, ClassGuid from win32_pnpentity"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &queryEnum), S_OK);
    AssertNotEquals("wb.query.val", (intptr_t)queryEnum.Get(), 0);
    queryEnum = RemarshalIntf(queryEnum.Get());

    while (true) {
        IWbemClassObject *objs[ENUM_SIZE] = {};
        ULONG ret;
        HRESULT res = queryEnum->Next(WBEM_INFINITE, ENUM_SIZE, objs, &ret);
        AssertEquals("wb.query.next", res, ret == ENUM_SIZE ? S_OK : S_FALSE);
        if (!ret) {
            AssertEquals("wb.query.next.end", enm->Next(WBEM_INFINITE, ENUM_SIZE, objs, &ret), S_FALSE);
            AssertEquals("wb.query.total", enumTotal, queryTotal);
            break;
        }

        queryTotal += ret;
        for (ULONG objI = 0; objI < ret; objI++) {
            objs[objI] = RemarshalIntf(objs[objI]).Take();
            ProcessWmiDevice(objs[objI]);
            objs[objI]->Release();
        }
    }

    ComRef<WbemObjectSinkImpl> queryAsync = new WbemObjectSinkImpl();
    queryAsync->Init([=](long count, IWbemClassObject **objs) {
        for (long objI = 0; objI < count; objI++){
            ProcessWmiDevice (objs[objI]);
} }, [=](HRESULT res, BSTR str, IWbemClassObject *obj, long total) {
        AssertTrue ("wb.aquery.args", !str && !obj);
        AssertEquals ("wb.aquery.res", res, S_OK);
        AssertEquals ("wb.aquery.gtotal", enumHid, queryAsync->GrandTotal ()); });

    ComRef<IWbemObjectSink> sink = RemarshalIntf((IWbemObjectSink *)queryAsync.Get(), false);
    AssertEquals("wb.aquery", svc->ExecQueryAsync(Bstr(L"WQL"), Bstr(L"select * from win32_pnpentity WHERE __path is not null and pnpclass = 'hidclass'"), 0, nullptr, sink), S_OK);
}

void *gTestKey = nullptr;
void *gTestVar = nullptr;

void OnTestKey(const MyInputHook_KeyInfo *info, void *data) {
    printf("test key %s (%f, %d)\n", info->Down ? "DOWN" : "UP", info->Strength, info->Time);

    // hook the output back into the input, just for testing.
    MyInputHook_UpdateCustomKey(gTestKey, info);
}

void OnTestVar(const char *value, void *data) {
    printf("test var = %s\n", value);
}

class Args {
    struct Arg {
        char mType;
        void *mDest;
    };

    unordered_map<string_view, Arg> mArgDefs;

public:
    void Add(const char *name, char type, void *dest) {
        mArgDefs[name] = {type, dest};
    }

    bool Process(int argc, char **argv) {
        int i = 1;
        while (i < argc) {
            const char *opt = argv[i++];
            auto iter = mArgDefs.find(opt);
            if (iter != mArgDefs.end()) {
                switch (iter->second.mType) {
                case 'b':
                    *(bool *)iter->second.mDest = true;
                    break;

                case 'i':
                    if (i >= argc || !StrToValue(argv[i++], (int *)iter->second.mDest)) {
                        Alert(L"Invalid int for %hs", opt);
                    }
                    break;

                case 's':
                    if (i >= argc) {
                        Alert(L"Invalid int for %hs", opt);
                    } else {
                        *(string *)iter->second.mDest = argv[i++];
                    }
                    break;

                default:
                    Alert(L"Unknown type for opt %hs", opt);
                }
            } else {
                Alert(L"Unknown option: %hs", opt);
            }
        }
        return argc > 1;
    }
};

#define BOOL_ARG(arg, var) \
    bool arg = false;      \
    args.Add("--" var, 'b', &arg);
#define G_BOOL_ARG(arg, var) \
    arg = false;             \
    args.Add("--" var, 'b', &arg);
#define INT_ARG(arg, var, def) \
    int arg = def;             \
    args.Add("--" var, 'i', &arg);
#define STR_ARG(arg, var) \
    string arg;           \
    args.Add("--" var, 's', &arg);

int main(int argc, char **argv) {
    Args args;
    BOOL_ARG(loadHook, "hook");
    STR_ARG(hookConfig, "hook-config");
    BOOL_ARG(testWindow, "test-win");
    INT_ARG(testWindowCount, "test-win-count", 1);
    BOOL_ARG(visualizeWindow, "vis-win");

    BOOL_ARG(readDevice, "read-dev");
    BOOL_ARG(readDeviceImmediate, "read-dev-immed");
    BOOL_ARG(readJoystick, "read-joy");

    BOOL_ARG(readWgi, "read-wgi");
    BOOL_ARG(readWgiRaw, "read-wgi-raw");
    BOOL_ARG(rumbleWgi, "rumble-wgi");

    BOOL_ARG(readRaw, "read-raw");
    BOOL_ARG(readRawBuffer, "read-raw-buf");
    BOOL_ARG(readRawWait, "read-raw-wait");
    BOOL_ARG(readRawFullPage, "read-raw-page");

    BOOL_ARG(readXInput, "read-x");
    BOOL_ARG(readXInputEx, "read-x-ex");
    BOOL_ARG(readXInputStroke, "read-x-stroke");
    BOOL_ARG(rumbleXInput, "rumble-x");
    INT_ARG(xinputVersion, "x-version", 4);

    BOOL_ARG(registerRaw, "reg-raw");
    G_BOOL_ARG(gRegRawActive, "reg-raw-active");
    BOOL_ARG(registerRawNoKeyboard, "reg-raw-nokeyboard");
    BOOL_ARG(registerRawNoMouse, "reg-raw-nomouse");
    BOOL_ARG(registerRawNoLegacy, "reg-raw-nolegacy");
    BOOL_ARG(registerRawNoHotkey, "reg-raw-nohotkey");
    BOOL_ARG(registerRawHotkey, "reg-raw-hotkey");
    BOOL_ARG(registerRawCaptureMouse, "reg-raw-capture");
    BOOL_ARG(registerLowHook, "reg-hook-low");
    BOOL_ARG(registerLocalHook, "reg-hook-local");
    BOOL_ARG(registerGlobalHook, "reg-hook-global");
    BOOL_ARG(registerHookNoKeyboard, "reg-hook-nokeyboard");
    BOOL_ARG(registerHookNoMouse, "reg-hook-nomouse");
    INT_ARG(registerHookCount, "reg-hook-count", 1);
    BOOL_ARG(registerHotkey, "reg-hotkey");

    BOOL_ARG(readWmi, "read-wmi");
    G_BOOL_ARG(gRemarshalWmi, "remarshal-wmi");
    BOOL_ARG(resistHideCursor, "resist-hide-cursor");
    BOOL_ARG(sendInputs, "send-inputs");
    G_BOOL_ARG(gStressDevice, "stress-device");
    G_BOOL_ARG(gStressDeviceCommunicate, "stress-device-comm");
    BOOL_ARG(measureLatency, "measure-latency");
    BOOL_ARG(wasteCpu, "waste-cpu");

    G_BOOL_ARG(gPrintGamepad, "print-pad");
    G_BOOL_ARG(gPrintKeyboard, "print-keys");
    G_BOOL_ARG(gPrintMouse, "print-mouse");
    G_BOOL_ARG(gPrintMouseCursor, "print-cursor");
    G_BOOL_ARG(gPrintTime, "print-time");
    BOOL_ARG(printRawInputDevices, "print-raw-dev");
    BOOL_ARG(printCfgMgrDevices, "print-cfg-dev");
    BOOL_ARG(printAllCfgMgrDevices, "print-cfg-dev-all");
    G_BOOL_ARG(gPrintDeviceChange, "print-dev-chg");
    BOOL_ARG(printWmi, "print-wmi");
    BOOL_ARG(printWmiAll, "print-wmi-all");

    BOOL_ARG(createProcess, "create-process");
    BOOL_ARG(createProcessOther, "create-process-other");
    BOOL_ARG(createProcessWithIo, "create-process-io");
    STR_ARG(processArgs, "process-args");
    BOOL_ARG(isChild, "child");
    BOOL_ARG(printProcessArgs, "print-process-args");
    BOOL_ARG(uiTest, "ui-test");

    IsWow64Process(GetCurrentProcess(), &gWow64);

    if (!args.Process(argc, argv)) {
        testWindow = readDevice = gPrintGamepad = visualizeWindow = true;
    }

    if (uiTest) {
        FreeConsole();
        DoUiTest(); // instead of everything else
        return 0;
    }

    if (printProcessArgs) {
        printf("cmdline: %ls\n", GetCommandLineW());
        printf("workdir: %ls\n", PathGetCurrentDirectory().Get());
    }

    if (loadHook && !isChild) {
        if (!hookConfig.empty()) {
            SetEnvironmentVariableW(L"MYINPUT_HOOK_CONFIG", PathFromStr(hookConfig.c_str()));
        }

        LoadLibraryA("myinput_hook.dll");
    }

    int numOurs = 0;
    HMODULE hookLib = GetModuleHandleA("myinput_hook.dll");
    if (hookLib) {
        numOurs = MyInputHook_InternalForTest();
    } else if (loadHook) {
        Alert(L"Hook not loaded in child!");
    }

#if ZERO // TODO - create dll, or just don't bother
    if (hookTest) {
        MyInputHook_PostInDllThread([](void *) {
            gTestKey = MyInputHook_RegisterCustomKey("Test", OnTestKey, nullptr);
            gTestVar = MyInputHook_RegisterCustomVar("Test", OnTestVar, nullptr);
            MyInputHook_LoadConfig(nullptr);
        },
                                    nullptr);
    }
#endif

    CreateNotifyWindow();

    TestJoystick(readJoystick);
    TestRawInput(numOurs, readRaw, readRawBuffer, readRawWait, readRawFullPage, printRawInputDevices);
    TestSetupDi(numOurs, readDevice, readDeviceImmediate);
    TestCfgMgr(printCfgMgrDevices, printAllCfgMgrDevices);
    TestXInput(xinputVersion, readXInput, readXInputEx, readXInputStroke, rumbleXInput, visualizeWindow);

    if (readWgi || readWgiRaw) {
        TestWgi(readWgi, readWgiRaw, rumbleWgi);
    }

    if (registerRaw) {
        RegisterRawInput(!registerRawNoKeyboard, !registerRawNoMouse,
                         registerRawNoLegacy, registerRawNoHotkey, registerRawHotkey, registerRawCaptureMouse,
                         readRawBuffer, readRawWait);
    }

    if (registerLowHook) {
        RegisterHook(!registerHookNoKeyboard, !registerHookNoMouse, registerHookCount, true, 0);
    }
#if ZERO
    if (registerGlobalHook) { // - nope...
        RegisterHook(!registerHookNoKeyboard, !registerHookNoMouse, registerHookCount, false, 0);
    }
#endif

    DWORD testWindowThread = 0;
    if (testWindow) {
        testWindowThread = ShowTestWindow(testWindowCount, resistHideCursor);
    }

    if (registerLocalHook && testWindowThread) {
        RegisterHook(!registerHookNoKeyboard, !registerHookNoMouse, registerHookCount, false, testWindowThread);
    }

    if (registerHotkey) {
        RegisterHotKeys();
    }

    if (sendInputs) {
        SendInputs();
    }

    if (measureLatency) {
        MeasureLatency();
    }

    if (wasteCpu) {
        WasteCpu();
    }

    if (readWmi) {
        ReadWmi(printWmi, printWmiAll);
    }

    if (createProcess) {
        CreateNewProcess(isChild, createProcessOther, createProcessWithIo, processArgs);
    }

    while (true) {
        Sleep(1000);
    }
}
