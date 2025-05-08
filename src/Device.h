#pragma once
#include <initguid.h>
#include <devpropdef.h>
#include <devguid.h>
#include <hidclass.h>
#include <hidsdi.h>
#include "State.h"
#include "LogUtils.h"

struct PreparsedCap {
    USHORT Page = HID_USAGE_PAGE_GENERIC;
    UCHAR Report = 0, StartBit = 0;
    USHORT Bits, NumReports;
    USHORT StartByte, TotalBits;
    ULONG Opts = 2; // not array
    USHORT EndByte, LinkColl;
    USHORT LinkPage = 0, LinkUsage = 0;
    ULONG Flags = 0, Pad[8] = {};
    USHORT MinUsage = 0, MaxUsage = 0;
    USHORT MinString = 0, MaxString = 0;
    USHORT MinDesig = 0, MaxDesig = 0;
    USHORT MinData = 0, MaxData = 0;
    USHORT Null = 0, Unk = 0;
    LONG LogMin = 0, LogMax = 0;
    LONG PhyMin = 0, PhyMax = 0;
    LONG Units = 0, UnitsExp = 0;

    PreparsedCap(USHORT startByte, UCHAR startBit, USHORT bitSize, USHORT count, USHORT page, USHORT usage, USHORT data, USHORT col) : StartByte(startByte), StartBit(startBit), Bits(bitSize), TotalBits(bitSize * count),
                                                                                                                                       NumReports(count), EndByte(startByte + DivRoundUp(startBit + bitSize * count, 8)) {
        LinkColl = col;
        Page = page;
        MinUsage = usage;
        MaxUsage = usage + count - 1;
        MinData = data;
        MaxData = data + count - 1;
    }

    static PreparsedCap Axis(USHORT startByte, UCHAR startBit, USHORT bits, USHORT page, USHORT usage, USHORT data, USHORT col = 0) {
        PreparsedCap cap(startByte, startBit, bits, 1, page, usage, data, col);
        cap.Flags = 0x8;
        cap.LogMax = cap.PhyMax = (1 << bits) - 1; // ???
        return cap;
    }

    static PreparsedCap Buttons(USHORT startByte, UCHAR startBit, USHORT bits, USHORT page, USHORT usage, USHORT data, USHORT col = 0) {
        PreparsedCap cap(startByte, startBit, 1, bits, page, usage, data, col);
        cap.Flags = 0x1c; // not 4?
        cap.LogMax = cap.PhyMax = 1;
        return cap;
    }

    static PreparsedCap Hat(USHORT startByte, UCHAR startBit, bool null0, USHORT page, USHORT usage, USHORT data, USHORT col = 0) {
        PreparsedCap cap(startByte, startBit, 4, 1, page, usage, data, col);
        cap.Opts = 0x42;
        cap.Flags = 0x8;
        cap.Null = null0 ? 0 : 8;
        cap.LogMin = null0 ? 1 : 0;
        cap.LogMax = null0 ? 8 : 7;
        cap.PhyMax = 315;
        cap.Units = 0x14;
        return cap;
    }
};

struct PreparsedCapSet {
    USHORT Start, Count, End, Bytes;
    PreparsedCapSet(USHORT start, USHORT count, USHORT bytes) : Start(start), Count(count), End(start + count), Bytes(bytes) {}
};

struct PreparsedNode {
    USHORT Usage, Page;
    USHORT Parent = 0, NumChildren = 0;
    USHORT NextChild = 0, FirstChild = 0;
    ULONG Type = 0;

    PreparsedNode(USHORT page, USHORT usage, USHORT parent, ULONG type, USHORT next = 0, USHORT first = 0, USHORT count = 0)
        : Page(page), Usage(usage), Parent(parent), Type(type), NextChild(next), FirstChild(first), NumChildren(count) {}
};

struct PreparsedHeader {
    char Header[8] = {'H', 'i', 'd', 'P', ' ', 'K', 'D', 'R'};
    USHORT Usage = HID_USAGE_GENERIC_GAMEPAD;
    USHORT Page = HID_USAGE_PAGE_GENERIC;
    USHORT Unk[2] = {};

    PreparsedCapSet Input;
    PreparsedCapSet Output;
    PreparsedCapSet Feature;

    USHORT CapsSize;
    USHORT NumNodes;

    PreparsedHeader(int numInputs, int numInputsBytes, int numOutputs, int numOutputsBytes,
                    int numFeatures, int numFeaturesBytes, int numNodes) : Input(0, numInputs, numInputsBytes), Output(numInputs, numOutputs, numOutputsBytes),
                                                                           Feature(numInputs + numOutputs, numFeatures, numFeaturesBytes),
                                                                           CapsSize((USHORT)sizeof(PreparsedCap) * (numInputs + numOutputs + numFeatures)),
                                                                           NumNodes((USHORT)numNodes) {}

    void Init(int reportId = 0) {
        PreparsedCap *caps = (PreparsedCap *)(this + 1);
        PreparsedNode *nodes = (PreparsedNode *)((PBYTE)caps + CapsSize);

        int numCaps = CapsSize / sizeof(PreparsedCap);
        for (int i = 0; i < numCaps; i++) {
            PreparsedNode *node = &nodes[caps[i].LinkColl];
            caps[i].LinkPage = node->Page;
            caps[i].LinkUsage = node->Usage;
            caps[i].Report = reportId;
        }
    }
};

#define HID_USAGE_GENERIC_PAIR(x) HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_##x

#define AW_SELECTOR(name)                        \
    template <class tchar>                       \
    const tchar *name();                         \
    template <>                                  \
    const char *name<char>() { return name##A; } \
    template <>                                  \
    const wchar_t *name<wchar_t>() { return name##W; }

#define DEVICE_NODE_TYPE_HID (1 << 0)
#define DEVICE_NODE_TYPE_XUSB (1 << 1)
#define DEVICE_NODE_TYPE_COUNT 2

const GUID GUID_DEVCLASS_XUSB = {0xd61ca365, 0x5af4, 0x4486, {0x99, 0x8b, 0x9d, 0xb4, 0x73, 0x4c, 0x6c, 0xa3}};
const GUID GUID_DEVINTERFACE_XUSB = {0xEC87F1E3L, 0xC13B, 0x4100, {0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB}};

#define GUID_DEVCLASS_XUSB_STR "{d61ca365-5af4-4486-998b-9db4734c6ca3}"
#define GUID_DEVCLASS_HIDCLASS_STR "{745a17a0-74d3-11d0-b6fe-00a0c90f57da}"

struct DeviceNode {
    int UserIdx = 0;
    int NodeType = 0;
    PathA DevicePathA = nullptr;
    Path DevicePathW;
    PathA DeviceInstNameA = nullptr;
    Path DeviceInstNameW;
    PathA DeviceBaseNameA = nullptr;
    Path DeviceBaseNameW;

    AW_SELECTOR(DevicePath);
    AW_SELECTOR(DeviceInstName);
    AW_SELECTOR(DeviceBaseName);

    bool IsXUsb() { return NodeType == DEVICE_NODE_TYPE_XUSB; }

    GUID DeviceClassGuid() { return IsXUsb() ? GUID_DEVCLASS_XUSB : GUID_DEVCLASS_HIDCLASS; }
    GUID DeviceIntfGuid() { return IsXUsb() ? GUID_DEVINTERFACE_XUSB : GUID_DEVINTERFACE_HID; }

    template <class tchar>
    const tchar *DeviceDescription() {
        return IsXUsb() ? TSTR("Xbox 360 Controller for Windows") : TSTR("HID-compliant game controller");
    }
    template <class tchar>
    const tchar *DeviceManufacturer() {
        return IsXUsb() ? TSTR("Microsoft") : TSTR("(Standard system devices)");
    }
    template <class tchar>
    const tchar *DeviceClass() {
        return IsXUsb() ? TSTR("XnaComposite") : TSTR("HIDClass");
    }
    template <class tchar>
    const tchar *DeviceClassGuidStr() {
        return IsXUsb() ? TSTR(GUID_DEVCLASS_XUSB_STR) : TSTR(GUID_DEVCLASS_HIDCLASS_STR);
    }
    template <class tchar>
    const tchar *DeviceClassDriver() {
        return IsXUsb() ? TSTR(GUID_DEVCLASS_XUSB_STR "\\0001") : TSTR(GUID_DEVCLASS_HIDCLASS_STR "\\0006"); // ?
    }
    template <class tchar>
    vector<const tchar *> DeviceHardwareIDs() {
        return IsXUsb() ? vector<const tchar *>{DeviceBaseName<tchar>()} : // TODO: one more, with &REV_ at the beginning...
                   vector<const tchar *>{DeviceBaseName<tchar>(), TSTR("HID_DEVICE_SYSTEM_GAME"), TSTR("HID_DEVICE_UP:0001_U:0005"), TSTR("HID_DEVICE")};
    }
};

ostream &operator<<(ostream &o, DeviceNode *node) {
    return o << node->UserIdx << ":" << node->NodeType;
}

struct DeviceIntf : public DeviceNode {
    int Types = DEVICE_NODE_TYPE_HID;
    int CustomIdx = -1;

    Path FinalPipePrefix;
    int FinalPipePrefixLen;

    DeviceNode XUsbNode;

    const wchar_t *ProductString = nullptr;
    const wchar_t *ManufacturerString = nullptr;
    const wchar_t *SerialString = nullptr;
    int VendorId = 0;
    int ProductId = 0;
    int VersionNum = 0;
    const PreparsedHeader *Preparsed = nullptr;
    unsigned PreparsedSize = 0;

    bool HasHid() { return Types & DEVICE_NODE_TYPE_HID; }
    bool HasXUsb() { return Types & DEVICE_NODE_TYPE_XUSB; }

    virtual int CopyInputTo(byte *dest) = 0;
    int CopyInputTo(byte *dest, int size) {
        if (size < Preparsed->Input.Bytes) {
            LOG << "Requested input report with not enough bytes" << END;
            return -1;
        }
        return CopyInputTo(dest);
    }

    virtual bool ProcessOutput(const byte *src, int size, int id) {
        LOG << "Received invalid output report: " << id << END;
        return false;
    }
    bool ProcessOutput(const byte *src, int size) {
        int id = size ? src[0] : -1;
        if (G.ApiDebug) {
            LOG << "Received output report: " << id << END;
        }
        return ProcessOutput(src, size, id);
    }

    virtual int ProcessFeature(const byte *src, byte *dest, int size, int id) {
        LOG << "Requested invalid feature report: " << id << END;
        return -1;
    }
    int ProcessFeature(const byte *src, byte *dest, int size) {
        int id = size ? src[0] : -1;
        if (G.ApiDebug) {
            LOG << (dest ? "Requested " : "Received ") << " feature report: " << id << END;
        }
        return ProcessFeature(src, dest, size, id);
    }

    virtual const wchar_t *GetIndexedString(ULONG index) {
        switch (index) {
        case 1:
            return ManufacturerString;
        case 2:
            return ProductString;
        case 3:
            return SerialString;
        default:
            LOG << "Requested invalid indexed string: " << index << END;
            return nullptr;
        }
    }

    void Init(int userIdx) {
        // TODO: need to vary devices, even if they end up in the same userIdx...

        UserIdx = userIdx;

        FinalPipePrefix = Path(MAX_PATH);
        FinalPipePrefixLen = wsprintfW(FinalPipePrefix, LR"(\Device\NamedPipe\MyInputHook_%d.%d.)", GetCurrentProcessId(), userIdx);

        if (HasHid()) {
            const wchar_t *igSuffix = HasXUsb() ? L"&IG_00" : L"";
            const wchar_t *uidSuffix = L"6&20f390fc&0&00";

            NodeType = DEVICE_NODE_TYPE_HID;

            DeviceBaseNameW = Path(MAX_PATH);
            wsprintfW(DeviceBaseNameW, LR"(HID\VID_%04X&PID_%04X%s)", VendorId, ProductId, igSuffix);
            DeviceBaseNameA = PathToStr(DeviceBaseNameW);

            DeviceInstNameW = Path(MAX_PATH);
            wsprintfW(DeviceInstNameW, LR"(%s\%s%02x)", DeviceBaseNameW.Get(), uidSuffix, userIdx);
            DeviceInstNameA = PathToStr(DeviceInstNameW);

            DevicePathW = Path(MAX_PATH);
            wsprintfW(DevicePathW, LR"(\\?\HID#VID_%04X&PID_%04X%s#%s%02x#{4d1e55b2-f16f-11cf-88cb-001111000030})",
                      VendorId, ProductId, igSuffix, uidSuffix, userIdx);
            DevicePathA = PathToStr(DevicePathW);
        }

        if (HasXUsb()) {
            const wchar_t *uidSuffix = L"C3C831";

            auto &xusb = XUsbNode;
            xusb.NodeType = DEVICE_NODE_TYPE_XUSB;
            xusb.UserIdx = UserIdx;

            xusb.DeviceBaseNameW = Path(MAX_PATH);
            wsprintfW(xusb.DeviceBaseNameW, LR"(HID\VID_%04X&PID_%04X)", VendorId, ProductId);
            xusb.DeviceBaseNameA = PathToStr(xusb.DeviceBaseNameW);

            xusb.DeviceInstNameW = Path(MAX_PATH);
            wsprintfW(xusb.DeviceInstNameW, LR"(%s\%s%02x)", xusb.DeviceBaseNameW.Get(), uidSuffix, userIdx);
            xusb.DeviceInstNameA = PathToStr(xusb.DeviceInstNameW);

            xusb.DevicePathW = Path(MAX_PATH);
            wsprintfW(xusb.DevicePathW, LR"(\\?\HID#VID_%04X&PID_%04X#%s%02x#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb})",
                      VendorId, ProductId, uidSuffix, userIdx);
            xusb.DevicePathA = PathToStr(xusb.DevicePathW);
        }
    }
};

int CreateHatValue(int up, int down, int left, int right, bool null0) {
    int hat = right ? 0x3 : left ? 0x7
                                 : 0;

    if (down) {
        hat = right ? 0x4 : left ? 0x6
                                 : 0x5;
    } else if (up) {
        hat = right ? 0x2 : left ? 0x8
                                 : 0x1;
    }

    if (!null0) {
        hat = (hat == 0) ? 8 : hat - 1;
    }

    return hat;
}
