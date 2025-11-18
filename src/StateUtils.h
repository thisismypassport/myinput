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

static int ImplGetUsers(user_mask_t *outMask, int devType = ~0, int minUser = 0) {
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

static int ImplNextUser(user_mask_t *refMask) {
    DWORD userIdx = 0;
    BitScanForward(&userIdx, *refMask);
    *refMask &= ~(1 << userIdx);
    return userIdx;
}

// might not be ideal in the future, if some types are exclusive...
// (e.g. used as local array size, and enumerated over)
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

static DeviceNode *ImplNextDeviceNode(int *refDevNodeIdx) {
    return ImplNextDeviceNode(*refDevNodeIdx + 1, refDevNodeIdx);
}

ImplInput *ImplGetInput(key_t key, user_t userIdx) {
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

    default:
        if (key >= MY_VK_CUSTOM_START && key < MY_VK_CUSTOM_START + (int)G.CustomKeys.size()) {
            return &G.CustomKeys[key - MY_VK_CUSTOM_START]->Key;
        }
        break;
    }
    return nullptr;
}

slot_t ImplAllocSlotForUser(int userIndex, ImplBoolOutput &(*getter)(ImplState *)) {
    if (userIndex >= 0) {
        ImplUser *user = ImplGetUser(userIndex, true);
        if (user) {
            return getter(&user->State).AllocSlot();
        } else {
            return IMPL_MAX_SLOTS;
        }
    } else {
        // Allocate the same slot for all users, for simplicity

        slot_t targetSlot = IMPL_MAX_SLOTS;
        for (int i = 0; i < IMPL_MAX_USERS; i++) {
            auto &output = getter(&G.Users[i].State);

            slot_t userSlot = output.AllocSlot();
            if (userSlot == IMPL_MAX_SLOTS) {
                return IMPL_MAX_SLOTS;
            }

            if (targetSlot == IMPL_MAX_SLOTS) {
                targetSlot = userSlot;
            }

            while (userSlot < targetSlot) {
                userSlot = output.AllocSlot();
                if (userSlot == IMPL_MAX_SLOTS) {
                    return IMPL_MAX_SLOTS;
                }
            }

            if (userSlot > targetSlot) {
                output.UndoAllocSlot();
                targetSlot = userSlot;
                i = -1; // restart from the beginning, using the new target
            }
        }
        return targetSlot;
    }
}

slot_t ImplAllocSlot(key_t key, user_t userIndex) {
    ImplInput *input = ImplGetInput(key, userIndex);
    if (input) {
        return input->Output.AllocSlot();
    }

#define STATE_LAMBDA(value) [](ImplState *state) -> ImplBoolOutput & { return state->value; }

    switch (key) {
    case MY_VK_PAD_A:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(A.Pressed));
    case MY_VK_PAD_B:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(B.Pressed));
    case MY_VK_PAD_X:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(X.Pressed));
    case MY_VK_PAD_Y:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(Y.Pressed));
    case MY_VK_PAD_START:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(Start.Pressed));
    case MY_VK_PAD_BACK:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(Back.Pressed));
    case MY_VK_PAD_GUIDE:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(Guide.Pressed));
    case MY_VK_PAD_EXTRA:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(Extra.Pressed));
    case MY_VK_PAD_DPAD_LEFT:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(DL.Pressed));
    case MY_VK_PAD_DPAD_RIGHT:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(DR.Pressed));
    case MY_VK_PAD_DPAD_UP:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(DU.Pressed));
    case MY_VK_PAD_DPAD_DOWN:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(DD.Pressed));
    case MY_VK_PAD_LTHUMB_LEFT:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LA.L.Pressed));
    case MY_VK_PAD_LTHUMB_RIGHT:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LA.R.Pressed));
    case MY_VK_PAD_LTHUMB_UP:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LA.U.Pressed));
    case MY_VK_PAD_LTHUMB_DOWN:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LA.D.Pressed));
    case MY_VK_PAD_RTHUMB_LEFT:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RA.L.Pressed));
    case MY_VK_PAD_RTHUMB_RIGHT:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RA.R.Pressed));
    case MY_VK_PAD_RTHUMB_UP:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RA.U.Pressed));
    case MY_VK_PAD_RTHUMB_DOWN:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RA.D.Pressed));
    case MY_VK_PAD_LSHOULDER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LB.Pressed));
    case MY_VK_PAD_RSHOULDER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RB.Pressed));
    case MY_VK_PAD_LTRIGGER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LT.Pressed));
    case MY_VK_PAD_RTRIGGER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RT.Pressed));
    case MY_VK_PAD_LTHUMB_PRESS:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(L.Pressed));
    case MY_VK_PAD_RTHUMB_PRESS:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(R.Pressed));
    case MY_VK_PAD_LTHUMB_HORZ_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LA.X.Modifier));
    case MY_VK_PAD_LTHUMB_VERT_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LA.Y.Modifier));
    case MY_VK_PAD_RTHUMB_HORZ_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RA.X.Modifier));
    case MY_VK_PAD_RTHUMB_VERT_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RA.Y.Modifier));
    case MY_VK_PAD_LTRIGGER_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LT.Modifier));
    case MY_VK_PAD_RTRIGGER_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RT.Modifier));
    case MY_VK_PAD_LTHUMB_ROTATOR_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(LA.RotateModifier));
    case MY_VK_PAD_RTHUMB_ROTATOR_MODIFIER:
        return ImplAllocSlotForUser(userIndex, STATE_LAMBDA(RA.RotateModifier));
    default:
        return IMPL_MAX_SLOTS;
    }
}

int ImplChooseBestKeyInPair(key_t key) {
    auto [key1, key2] = GetKeyPair(key);
    int scan1 = MapVirtualKeyW(key1, MAPVK_VK_TO_VSC);
    int scan2 = MapVirtualKeyW(key2, MAPVK_VK_TO_VSC);
    return (!scan1 && scan2) ? key2 : key1;
}
