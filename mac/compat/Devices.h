#pragma once
// macOS counterpart of src/Devices.h: the gamepad types a config can ask for with !Device.
#include "Device.h"

struct NoDeviceIntf : public DeviceIntf {
    NoDeviceIntf(int user) {
        UserIdx = XUsbNode.UserIdx = user;
        Types = 0;
    }
};

struct CustomDeviceIntf : public NoDeviceIntf {
    CustomDeviceIntf(int user, int customIdx) : NoDeviceIntf(user) {
        CustomIdx = customIdx;
        Kind = MacDeviceKind::Custom;
    }
};

struct XDeviceIntf : public DeviceIntf {
    XDeviceIntf(int user) {
        UserIdx = XUsbNode.UserIdx = user;
        Types = DEVICE_NODE_TYPE_HID | DEVICE_NODE_TYPE_XUSB;
        Kind = MacDeviceKind::XBox;
    }
};

struct Ds4DeviceIntf : public DeviceIntf {
    Ds4DeviceIntf(int user) {
        UserIdx = XUsbNode.UserIdx = user;
        Kind = MacDeviceKind::Ds4;
    }
};
