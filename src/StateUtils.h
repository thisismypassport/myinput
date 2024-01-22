#pragma once
#include "State.h"
#include "Device.h"

static ImplUser *ImplGetUser(DWORD user, bool force = false) {
    if (user < IMPL_MAX_USERS && (force || G.Users[user].Connected)) {
        return &G.Users[user];
    } else {
        return nullptr;
    }
}

static DeviceIntf *ImplGetDevice(DWORD user) {
    ImplUser *data = ImplGetUser(user);
    return data ? data->Device : nullptr;
}

static DeviceNode *ImplGetDeviceNode(DWORD user, int type, DeviceIntf **outDevice = nullptr) {
    DeviceIntf *device = ImplGetDevice(user);
    if (!device || !(device->Types & type)) {
        return nullptr;
    }

    if (outDevice) {
        *outDevice = device;
    }
    return type == DEVICE_NODE_TYPE_HID ? device : type == DEVICE_NODE_TYPE_XUSB ? &device->XUsbNode
                                                                                 : nullptr;
}

static int ImplGetUsers(UINT *outMask, int devType = ~0, int minUser = 0) {
    UINT mask = 0;
    int count = 0;
    for (int i = minUser; i < IMPL_MAX_USERS; i++) {
        auto &user = G.Users[i];
        if (user.Connected && user.Device && (user.Device->Types & devType)) {
            mask |= (1 << i);
            count++;
        }
    }

    *outMask = mask;
    return count;
}

static int ImplNextUser(UINT *refMask) {
    DWORD userIdx = 0;
    BitScanForward(&userIdx, *refMask);
    *refMask &= ~(1 << userIdx);
    return userIdx;
}

// might not be ideal in the future, if some types are exclusive...
#define IMPL_MAX_DEVNODES (IMPL_MAX_USERS * DEVICE_NODE_TYPE_COUNT)

static DeviceNode *ImplGetDeviceNode(int devNodeIdx, DeviceIntf **outDevice = nullptr) {
    int user = devNodeIdx % IMPL_MAX_USERS;
    int typeIdx = devNodeIdx / IMPL_MAX_USERS;
    return ImplGetDeviceNode(user, 1 << typeIdx, outDevice);
}

static DeviceNode *ImplNextDeviceNode(int devNodeIdx, int *outDevNodeIdx) {
    int user = devNodeIdx % IMPL_MAX_USERS;
    int typeIdx = devNodeIdx / IMPL_MAX_USERS;

    DeviceNode *node = nullptr;
    while (typeIdx < DEVICE_NODE_TYPE_COUNT) {
        int type = 1 << typeIdx;
        while (user < IMPL_MAX_USERS) {
            node = ImplGetDeviceNode(user, type);
            if (node) {
                goto breakboth;
            }

            user++;
        }

        typeIdx++;
        user = 0;
    }

breakboth:
    *outDevNodeIdx = user + (typeIdx * IMPL_MAX_USERS);
    return node;
}

ImplInput *ImplGetInput(int key, int userIndex) {
    ImplInput *input = G.Keyboard.Get(key);
    if (input) {
        return input;
    }

    switch (key) {
    case MY_VK_WHEEL_UP:
        return &G.Mouse.Wheel.Forward;
    case MY_VK_WHEEL_DOWN:
        return &G.Mouse.Wheel.Backward;
    case MY_VK_WHEEL_RIGHT:
        return &G.Mouse.HWheel.Forward;
    case MY_VK_WHEEL_LEFT:
        return &G.Mouse.HWheel.Backward;
    case MY_VK_MOUSE_UP:
        return &G.Mouse.Motion.Vert.Forward;
    case MY_VK_MOUSE_DOWN:
        return &G.Mouse.Motion.Vert.Backward;
    case MY_VK_MOUSE_RIGHT:
        return &G.Mouse.Motion.Horz.Forward;
    case MY_VK_MOUSE_LEFT:
        return &G.Mouse.Motion.Horz.Backward;

    case MY_VK_CUSTOM:
        if ((size_t)userIndex < G.CustomKeys.size()) {
            return &G.CustomKeys[userIndex]->Key;
        }
        break;
    }
    return nullptr;
}

int ImplChooseBestKeyInPair(int key) {
    auto [key1, key2] = GetKeyPair(key);
    int scan1 = MapVirtualKeyW(key1, MAPVK_VK_TO_VSC);
    int scan2 = MapVirtualKeyW(key2, MAPVK_VK_TO_VSC);
    return (!scan1 && scan2) ? key2 : key1;
}
