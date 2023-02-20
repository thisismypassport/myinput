#pragma once
#include "Device.h"
#include "Header.h"

static void ImplSetRumble(ImplUser *user, double lowFreq, double highFreq);

struct NoDeviceIntf : public DeviceIntf {
    int CopyInputTo(uint8_t *dest) override { return 0; }

    NoDeviceIntf(int userIdx) {
        SerialString = ManufacturerString = ProductString = L"";
        VendorId = ProductId = VersionNum = 0;
        IsHid = false;

        Init(userIdx);
    }
};

#pragma pack(push, 1)
struct XHidReport {
    uint8_t ReportId;
    uint16_t X, Y, RX, RY;
    uint16_t Triggers;
    uint16_t Btns, Pad;

    void SetFrom(const ImplUser *user) {
        auto &state = user->State;
        lock_guard<mutex> lock(state.Mutex);

        ReportId = 0;
        X = state.LA.X.Value16() + 0x8000;
        Y = 0x8000 - state.LA.Y.Value16();
        RX = state.RA.X.Value16() + 0x8000;
        RY = 0x8000 - state.RA.Y.Value16();
        // this mess is needed for 'clever' xinput correlation code in sdl
        Triggers = (int16_t)nearbyint((state.LT.Value - state.RT.Value) * 0x7fff) + 0x8000;

        Btns = 0;
        Btns |= state.A.State ? 0x1 : 0;
        Btns |= state.B.State ? 0x2 : 0;
        Btns |= state.X.State ? 0x4 : 0;
        Btns |= state.Y.State ? 0x8 : 0;
        Btns |= state.LB.State ? 0x10 : 0;
        Btns |= state.RB.State ? 0x20 : 0;
        Btns |= state.Start.State ? 0x40 : 0;
        Btns |= state.Back.State ? 0x80 : 0;
        Btns |= state.L.State ? 0x100 : 0;
        Btns |= state.R.State ? 0x200 : 0;

        Btns |= CreateHatValue(state.DU.State, state.DD.State, state.DL.State, state.DR.State, true) << 10;
    }
};
#pragma pack(pop)

// Driver-exported, not real
struct XHidPreparsedData {
    PreparsedHeader Header = {7, sizeof(XHidReport), 0, 0, 0, 0, 3}; // Update if adding/removing below

    PreparsedCap X = PreparsedCap::Axis(1, 0, 16, HID_USAGE_GENERIC_PAIR(X), 0, 1);
    PreparsedCap Y = PreparsedCap::Axis(3, 0, 16, HID_USAGE_GENERIC_PAIR(Y), 1, 1);
    PreparsedCap RX = PreparsedCap::Axis(5, 0, 16, HID_USAGE_GENERIC_PAIR(RX), 2, 2);
    PreparsedCap RY = PreparsedCap::Axis(7, 0, 16, HID_USAGE_GENERIC_PAIR(RY), 3, 2);
    PreparsedCap Trig = PreparsedCap::Axis(9, 0, 16, HID_USAGE_GENERIC_PAIR(Z), 4);
    PreparsedCap Btns = PreparsedCap::Buttons(11, 0, 10, HID_USAGE_PAGE_BUTTON, 1, 5);
    PreparsedCap DPad = PreparsedCap::Hat(12, 2, true, HID_USAGE_GENERIC_PAIR(HATSWITCH), 15);

    PreparsedNode Root = PreparsedNode(HID_USAGE_GENERIC_PAIR(GAMEPAD), 0, 1, 0, 1, 2);
    PreparsedNode XY = PreparsedNode(HID_USAGE_PAGE_GENERIC, 0, 0, 0, 2);
    PreparsedNode RXY = PreparsedNode(HID_USAGE_PAGE_GENERIC, 0, 0, 0, 0);

    XHidPreparsedData() { Header.Init(); }
};

struct XDeviceIntf : public DeviceIntf {
    XHidPreparsedData PreparsedData;

    int CopyInputTo(uint8_t *dest) override {
        ((XHidReport *)dest)->SetFrom(&G.Users[UserIdx]);
        return sizeof(XHidReport);
    }

    XDeviceIntf(int userIdx) {
        ProductString = L"Controller (XBOX 360 Controller for Windows)"; // ???
        ManufacturerString = L"Microsoft Corporation";                   // ???
        SerialString = L"1234567";                                       // ???
        VendorId = 0x45e;
        ProductId = 0x28e;
        VersionNum = 0x100;
        IsXUsb = true;
        Preparsed = &PreparsedData.Header;
        PreparsedSize = sizeof(XHidPreparsedData);

        Init(userIdx);
    }
};

#pragma pack(push, 1)
struct DS4HidReport {
    uint8_t ReportId;
    uint8_t X, Y, RX, RY;
    uint8_t Btns[3], LT, RT;
    uint16_t Time;
    uint8_t Battery;
    int16_t GX, GY, GZ;
    int16_t AX, AY, AZ;
    uint8_t Unk2[5];
    uint8_t PowerOptions;
    uint8_t Unk3[4];
    uint8_t Touch1[4];
    uint8_t Touch2[4];
    uint8_t Unk4[21];

    void SetFrom(const ImplUser *user) {
        ZeroMemory(this, sizeof(DS4HidReport));

        auto &state = user->State;
        lock_guard<mutex> lock(state.Mutex);

        ReportId = 1;
        X = state.LA.X.Value8() + 0x80;
        Y = 0x80 - state.LA.Y.Value8();
        RX = state.RA.X.Value8() + 0x80;
        RY = 0x80 - state.RA.Y.Value8();
        LT = state.LT.Value8();
        RT = state.RT.Value8();

        Btns[0] |= CreateHatValue(state.DU.State, state.DD.State, state.DL.State, state.DR.State, false);

        Btns[0] |= state.X.State ? 0x10 : 0;
        Btns[0] |= state.A.State ? 0x20 : 0;
        Btns[0] |= state.B.State ? 0x40 : 0;
        Btns[0] |= state.Y.State ? 0x80 : 0;
        Btns[1] |= state.LB.State ? 0x1 : 0;
        Btns[1] |= state.RB.State ? 0x2 : 0;
        Btns[1] |= state.LT.State ? 0x4 : 0;
        Btns[1] |= state.RT.State ? 0x8 : 0;
        Btns[1] |= state.Back.State ? 0x10 : 0;
        Btns[1] |= state.Start.State ? 0x20 : 0;
        Btns[1] |= state.L.State ? 0x40 : 0;
        Btns[1] |= state.R.State ? 0x80 : 0;
        Btns[2] |= state.Guide.State ? 0x1 : 0;
        Btns[2] |= state.Extra.State ? 0x2 : 0;
        Btns[2] |= state.Version << 2;

        constexpr double gScale = 1.0 / 0x2000;
        constexpr double rotScale = DegreesToRadians / 16;
        auto &motion = state.Motion;
        AX = ClampToInt<int16_t>(motion.X.FinalAccel / gScale);
        AY = ClampToInt<int16_t>(motion.Y.FinalAccel / gScale);
        AZ = ClampToInt<int16_t>(motion.Z.FinalAccel / gScale);
        GX = ClampToInt<int16_t>(motion.RX.Speed / rotScale);
        GY = ClampToInt<int16_t>(motion.RY.Speed / rotScale);
        GZ = ClampToInt<int16_t>(motion.RZ.Speed / rotScale);

        Time = (uint16_t)((uint64_t)state.Time * 1000 * 3 / 16);

        Battery = 0xff; // scale?
        PowerOptions = 0x1b;
        // TODO: touch?
        Touch1[0] = Touch2[0] = 0x80;
    }
};
#pragma pack(pop)

struct DS4HidPreparsedData {
    PreparsedHeader Header = {8, sizeof(DS4HidReport), 0, 0x20, 0, 0x40, 1}; // Update if adding/removing below

    PreparsedCap X = PreparsedCap::Axis(1, 0, 8, HID_USAGE_GENERIC_PAIR(X), 0);
    PreparsedCap Y = PreparsedCap::Axis(2, 0, 8, HID_USAGE_GENERIC_PAIR(Y), 1);
    PreparsedCap RX = PreparsedCap::Axis(3, 0, 8, HID_USAGE_GENERIC_PAIR(Z), 2);
    PreparsedCap RY = PreparsedCap::Axis(4, 0, 8, HID_USAGE_GENERIC_PAIR(RZ), 3);
    PreparsedCap DPad = PreparsedCap::Hat(5, 0, false, HID_USAGE_GENERIC_PAIR(HATSWITCH), 4);
    PreparsedCap Btns = PreparsedCap::Buttons(5, 4, 14, HID_USAGE_PAGE_BUTTON, 1, 5);
    PreparsedCap LT = PreparsedCap::Axis(8, 0, 8, HID_USAGE_GENERIC_PAIR(RX), 19);
    PreparsedCap RT = PreparsedCap::Axis(9, 0, 8, HID_USAGE_GENERIC_PAIR(RY), 20);

    PreparsedNode Root = PreparsedNode(HID_USAGE_GENERIC_PAIR(GAMEPAD), 0, 1);

    DS4HidPreparsedData() { Header.Init(1); }
};

struct Ds4DeviceIntf : public DeviceIntf {
    DS4HidPreparsedData PreparsedData;

    int CopyInputTo(uint8_t *dest) override {
        ((DS4HidReport *)dest)->SetFrom(&G.Users[UserIdx]);
        return sizeof(DS4HidReport);
    }

    bool ProcessOutput(const uint8_t *src, int size, int id) override {
        if (id == 5 && size >= 6) {
            ImplSetRumble(&G.Users[UserIdx], (double)src[5] / 0xff, (double)src[4] / 0xff);
            return true;
        }

        return DeviceIntf::ProcessOutput(src, size, id);
    }

    int ProcessFeature(const uint8_t *src, uint8_t *dest, int size, int id) override {
        if (id == 18 && size >= 0x10) // serial? more?
        {
            if (dest) {
                FillMemory(dest + 1, 0xf, 0xff);
            }
            return 0x10;
        } else if (id == 2) // calibration
        {
            return 1; // TODO: much more...
        }

        return DeviceIntf::ProcessFeature(src, dest, size, id);
    }

    Ds4DeviceIntf(int userIdx) {
        ProductString = L"PS4 Controller"; // ????
        ManufacturerString = L"Sony Computer Entertainment";
        SerialString = L"1234567"; // ???
        VendorId = 0x54c;
        ProductId = 0x5c4;
        VersionNum = 0x100;
        Preparsed = &PreparsedData.Header;
        PreparsedSize = sizeof(DS4HidPreparsedData);

        Init(userIdx);
    }
};
