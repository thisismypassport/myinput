#pragma once
// macOS counterpart of src/Device.h. On Windows a device is a full emulated HID/XUSB node,
// because games talk to the driver stack. Here the virtual gamepad is created through the
// host app's own input library (see mac/hook/SdlPad.mm), so a device is only a type tag.
#include "State.h"
#include "LogUtils.h"
#include "Header.h"

#define DEVICE_NODE_TYPE_HID 1
#define DEVICE_NODE_TYPE_XUSB 2
#define DEVICE_NODE_TYPE_COUNT 2

enum class MacDeviceKind {
    None,
    XBox,
    Ds4,
    Custom,
};

struct DeviceNode {
    int UserIdx = 0;
};

struct DeviceIntf : public DeviceNode {
    int Types = DEVICE_NODE_TYPE_HID;
    int CustomIdx = -1;
    MacDeviceKind Kind = MacDeviceKind::None;
    DeviceNode XUsbNode;

    bool HasHid() { return Types & DEVICE_NODE_TYPE_HID; }
    bool HasXUsb() { return Types & DEVICE_NODE_TYPE_XUSB; }

    virtual ImplStickShape DefaultStickShape() { return ImplStickShape::Circle; }
    virtual ~DeviceIntf() {}
};
