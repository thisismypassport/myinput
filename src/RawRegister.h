#pragma once
#include "Impl.h"
#include "Header.h"

UniqueLog gUniqLogRawInputRegister;

struct RawInputRegInfo {
    bool Registered;
    bool Private;
    HWND Window;
    UINT Flags;
};

class RawInputRegBase {
protected:
    mutex Mutex;
    WeakAtomic<bool> Registered = false;
    WeakAtomic<bool> Private = false;
    HWND Window = 0;
    UINT Flags = 0;

    HWND GetTargetWindow(bool *outForeground = nullptr) {
        HWND window = Window;
        bool foreground = true;
        if (window) {
            if (!IsWindow(window)) {
                window = nullptr;
            }

            if (window) {
                foreground = IsWindowInOurProcess(GetForegroundWindow());

                if (!foreground && (Flags & (RIDEV_INPUTSINK | RIDEV_EXINPUTSINK)) == 0) {
                    window = nullptr;
                }
            }
        } else {
            window = GetForegroundWindow();
            if (window && !IsWindowInOurProcess(window)) {
                window = nullptr;
            }
        }
        if (outForeground) {
            *outForeground = foreground;
        }
        return window;
    }

    template <class TInit>
    void Register(HWND window, UINT flags, TInit init) {
        lock_guard<mutex> lock(Mutex);
        if (!Registered) {
            Window = window;
            Flags = flags;
            Private = RIDEV_EXMODE(Flags) != RIDEV_PAGEONLY;

            init();

            Registered = true;
        }
    }

    template <class TUninit>
    void Unregister(UINT flags, TUninit uninit) {
        lock_guard<mutex> lock(Mutex);
        if (Registered) {
            uninit();
        }

        // update method of unregistration (delete vs exclude) either way
        Window = 0;
        Flags = flags & RIDEV_EXCLUDE;
        Private = Flags == RIDEV_EXCLUDE;
        Registered = false;
    }

    UINT ReadFrom(UINT uiCommand, PVOID dest, PUINT pCbSize, RAWINPUT *src, int srcSize) {
        UINT expectedSize = 0;
        switch (uiCommand) {
        case RID_INPUT:
            expectedSize = srcSize;
            break;
        case RID_HEADER:
            expectedSize = sizeof(RAWINPUTHEADER);
            break;

        default:
            LOG_ERR << "Invalid uiCommand: " << uiCommand << END;
            break;
        }

        if (!dest) {
            *pCbSize = expectedSize;
            return 0;
        } else if (*pCbSize < expectedSize) {
            *pCbSize = expectedSize;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return INVALID_UINT_VALUE;
        } else {
            CopyMemory(dest, src, expectedSize);
            return expectedSize;
        }
    }

public:
    bool IsRegistered() { return Registered; }
    bool IsRegisteredPrivate() { return Registered && Private; }

    RawInputRegInfo GetRegisteredInfo() {
        lock_guard<mutex> lock(Mutex);
        return RawInputRegInfo{Registered, Private, Window, Flags};
    }
};

class RawInputRegFullPage : public RawInputRegBase {
public:
    void Register(HWND window, UINT flags) {
        RawInputRegBase::Register(window, flags, [this] {});
    }

    void Unregister(UINT flags) {
        RawInputRegBase::Unregister(flags, [this] {});
    }

    void UpdateRealRegister(bool force) {}
} GRawInputRegFullPage;

class RawInputReg : public RawInputRegBase {
protected:
    mutex DequeMutex;
    deque<Buffer> *BufDeque = nullptr;
    deque<Buffer> *OldBufDeque = nullptr;
    HWND PrevWindow = nullptr;

    enum { MaxDequeSize = (InputHandleHighCount / 2) - 8 };
    WORD HandleHighBase, HandleHighEnd;
    WORD HandleHighFront, HandleHighBack, OldHandleHighFront = 0;

    RawInputReg(WORD handleHighBase) {
        HandleHighBase = HandleHighFront = HandleHighBack = handleHighBase;
        HandleHighEnd = HandleHighBase + InputHandleHighCount;
    }

    void OnChangeWindow(HWND window) // called locked
    {
        // keep old unsent or potentially-unprocessed handles
        if (PrevWindow && BufDeque && !BufDeque->empty()) {
            auto prevOld = OldBufDeque;
            OldBufDeque = BufDeque;

            BufDeque = prevOld; // will be created later if null
            if (BufDeque) {
                BufDeque->clear();
            }

            OldHandleHighFront = HandleHighFront;
            HandleHighFront = HandleHighBack;
        }
        PrevWindow = window;
    }

    void Enqueue(HWND window, WPARAM wparam, Buffer &&buffer) {
        WORD handleHigh;
        {
            lock_guard<mutex> lock(DequeMutex);

            if (window != PrevWindow) {
                OnChangeWindow(window);
            }

            if (!BufDeque) {
                BufDeque = new deque<Buffer>();
            }

            BufDeque->push_back(move(buffer));

            handleHigh = HandleHighBack++;
            if (HandleHighBack == HandleHighEnd) {
                HandleHighBack = HandleHighBase;
            }

            if (HandleHighBack == OldHandleHighFront && OldBufDeque && !OldBufDeque->empty()) {
                OldBufDeque->clear();
            }

            if (BufDeque->size() >= MaxDequeSize) {
                if (G.ApiTrace) {
                    LOG << "Too many raw input messages to " << window << END;
                }
                BufDeque->pop_front();

                HandleHighFront++;
                if (HandleHighFront == HandleHighEnd) {
                    HandleHighFront = HandleHighBase;
                }

                // we need to keep flooding the window with messages (hopefully it's processing or clearing them - that need not reach Read)
                // as otherwise it can clear all its messages and we won't know to send it another.
                // (alternatively - consider hooking message system? will also help any queue status issues)
            }
        }

        if (G.ApiTrace) {
            LOG << "Pushing Raw input data for " << (HRAWINPUT)MakeOurHandle(handleHigh) << " to " << window << END;
        }
        PostMessageW(window, WM_INPUT, wparam, (LPARAM)MakeOurHandle(handleHigh));
    }

    void EnqueueNotify(HWND window, HANDLE handle, bool added) {
        if (G.ApiTrace) {
            LOG << "Pushing device change notify to " << window << END;
        }
        PostMessageW(window, WM_INPUT_DEVICE_CHANGE, added ? GIDC_ARRIVAL : GIDC_REMOVAL, (LPARAM)handle);
    }

    int GetHandleHighDelta(WORD left, WORD right) {
        int delta = (INT16)(left - right);
        if (delta >= InputHandleHighCount / 2) {
            delta -= InputHandleHighCount;
        } else if (delta < -InputHandleHighCount / 2) {
            delta += InputHandleHighCount;
        }
        return delta;
    }

public:
    bool Read(WORD handleHigh, UINT uiCommand, PVOID dest, PUINT pCbSize, PUINT pCount) {
        if (!(handleHigh >= HandleHighBase && handleHigh < HandleHighEnd)) {
            return false;
        }

        lock_guard<mutex> lock(DequeMutex);
        deque<Buffer> *bufDeque = BufDeque;
        WORD *handleHighFrontPtr = &HandleHighFront;

        int handleDelta = GetHandleHighDelta(handleHigh, HandleHighFront);

        if (handleDelta < 0 && OldBufDeque && !OldBufDeque->empty()) // try old deque
        {
            handleDelta = GetHandleHighDelta(handleHigh, OldHandleHighFront);
            bufDeque = OldBufDeque;
            handleHighFrontPtr = &OldHandleHighFront;
        }

        if (handleDelta > 0) {
            int dequeCount = bufDeque ? (int)bufDeque->size() : 0;
            handleDelta = min(handleDelta, dequeCount);

            for (int i = 0; i < handleDelta && bufDeque && !bufDeque->empty(); i++) {
                bufDeque->pop_front();
            }

            *handleHighFrontPtr += handleDelta;
            if (*handleHighFrontPtr >= HandleHighEnd) {
                *handleHighFrontPtr -= InputHandleHighCount;
            }
        }

        if (!bufDeque || bufDeque->empty() || handleDelta < 0) {
            if (G.Debug) {
                LOG << "Read of old or unknown raw input handle" << END;
            }
            return false;
        }

        Buffer &buffer = bufDeque->front();
        RAWINPUT *src = (RAWINPUT *)buffer.Ptr();
        *pCount = ReadFrom(uiCommand, dest, pCbSize, src, (int)buffer.Size());
        return true;
    }
};

class RawInputRegLegacy : public RawInputReg {
protected:
    WeakAtomic<bool> NoLegacy = false;
    unordered_set<HANDLE> Handles;
    BufferList *BufList = nullptr;
    int BufSize;
    int PrevMapped = -1;

    RawInputRegLegacy(WORD handleHighBase, int bufSize) : RawInputReg(handleHighBase), BufSize(bufSize) {}

    void OnNotifyChange(HANDLE handle, bool added) // (called with Mutex)
    {
        HWND window = GetTargetWindow();
        if (!window) {
            return;
        }

        EnqueueNotify(window, handle, added);
    }

    void UpdateRealRegister(USHORT devUsage, bool devDisallowed, bool mapped, bool force) {
        lock_guard<mutex> lock(Mutex);

        if (PrevMapped != (int)mapped || force) {
            UINT exmode = RIDEV_EXMODE(Flags);
            UINT mappedFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY |
                               (exmode == RIDEV_NOLEGACY ? exmode : 0) |
                               (Flags & (RIDEV_CAPTUREMOUSE | RIDEV_NOHOTKEYS | RIDEV_APPKEYS));

            RAWINPUTDEVICE device;
            device.hwndTarget = mapped ? (devDisallowed ? 0 : G.DllWindow) : Window;
            device.usUsagePage = HID_USAGE_PAGE_GENERIC;
            device.usUsage = devUsage;
            device.dwFlags = mapped ? (devDisallowed ? RIDEV_EXCLUDE : mappedFlags) : (Registered ? Flags : RIDEV_EXCLUDE);

            if (!RegisterRawInputDevices_Real(&device, 1, sizeof(RAWINPUTDEVICE))) {
                LOG_ERR << "Error registering real raw input device" << END;
            }

            PrevMapped = mapped;
        }

        NoLegacy = Registered && RIDEV_EXMODE(Flags) == RIDEV_NOLEGACY;
    }

public:
    void Register(HWND window, UINT flags) {
        RawInputRegBase::Register(window, flags, [this] {
            BufList = GBufferLists.Get(BufSize);

            if (Flags & RIDEV_DEVNOTIFY) {
                for (auto &handle : Handles) {
                    OnNotifyChange(handle, true);
                }
            }
        });
    }

    void Unregister(UINT flags) {
        RawInputRegBase::Unregister(flags, [this] {});
    }

    bool InNoLegacyMode() { return NoLegacy; }

    void OnDeviceAdded(HANDLE handle) {
        lock_guard<mutex> lock(Mutex);
        Handles.insert(handle);

        if (Registered && (Flags & RIDEV_DEVNOTIFY)) {
            OnNotifyChange(handle, true);
        }
    }

    void OnDeviceRemoved(HANDLE handle) {
        lock_guard<mutex> lock(Mutex);
        if (!Handles.erase(handle)) {
            return;
        }

        if (Registered && (Flags & RIDEV_DEVNOTIFY)) {
            OnNotifyChange(handle, false);
        }
    }

    void OnEvent(RAWINPUT *source) {
        lock_guard<mutex> lock(Mutex);
        if (!Registered) {
            return;
        }

        bool foreground;
        HWND window = GetTargetWindow(&foreground);
        if (!window) {
            return;
        }

        Buffer buffer(BufList);
        RAWINPUT *rawInput = (RAWINPUT *)buffer.Ptr();

        CopyMemory(rawInput, source, BufSize);

        rawInput->header.dwSize = BufSize;
        rawInput->header.wParam &= ~GET_RAWINPUT_CODE_WPARAM(~0); // in case there's more stuff there
        rawInput->header.wParam |= foreground ? RIM_INPUT : RIM_INPUTSINK;

        Enqueue(window, rawInput->header.wParam, move(buffer));
    }
};

class RawInputRegKeyboard : public RawInputRegLegacy {
public:
    RawInputRegKeyboard() : RawInputRegLegacy(KeyboardInputHandleHighStart, sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {}

    void UpdateRealRegister(bool force) {
        bool mapped = G.Keyboard.IsMapped && G.IsActive();
        RawInputRegLegacy::UpdateRealRegister(HID_USAGE_GENERIC_KEYBOARD, true, mapped, force);
        SetManualAsyncKeyState(mapped && NoLegacy);
    }
} GRawInputRegKeyboard;

class RawInputRegMouse : public RawInputRegLegacy {
public:
    RawInputRegMouse() : RawInputRegLegacy(MouseInputHandleHighStart, sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) {}

    void UpdateRealRegister(bool force) {
        bool mapped = G.Mouse.IsMapped && G.IsActive();
        RawInputRegLegacy::UpdateRealRegister(HID_USAGE_GENERIC_MOUSE, false, mapped, force);
    }
} GRawInputRegMouse;

class RawInputRegGamepad : public RawInputReg {
    ImplGlobalCb NotifyCbIter;

    struct RegInfo {
        bool Active = false;
        ImplUserCb CbIter;
        BufferList *BufList = nullptr;
    } Regs[IMPL_MAX_USERS] = {};

    void OnMessage(ImplUser *user) {
        bool foreground;
        HWND window = GetTargetWindow(&foreground);
        if (!window) {
            return;
        }

        DeviceIntf *device = user->Device;
        int idx = device->UserIdx;

        Buffer buffer(Regs[idx].BufList);
        RAWINPUT *rawInput = (RAWINPUT *)buffer.Ptr();

        rawInput->header.dwType = RIM_TYPEHID;
        rawInput->header.dwSize = GetRawInputSize(device);
        rawInput->header.hDevice = GetCustomDeviceHandle(idx);
        rawInput->header.wParam = foreground ? RIM_INPUT : RIM_INPUTSINK;
        rawInput->data.hid.dwCount = 1;
        rawInput->data.hid.dwSizeHid = device->CopyInputTo(rawInput->data.hid.bRawData);

        Enqueue(window, rawInput->header.wParam, move(buffer));
    }

    void OnNotifyMessage(ImplUser *user, bool added) {
        HWND window = GetTargetWindow();
        if (!window) {
            return;
        }

        HANDLE handle = GetCustomDeviceHandle(user->Device->UserIdx);

        EnqueueNotify(window, handle, added);
    }

    int GetRawInputSize(DeviceIntf *device) {
        return sizeof(RAWINPUTHEADER) + offsetof(RAWHID, bRawData) + device->Preparsed->Input.Bytes;
    }

public:
    RawInputRegGamepad() : RawInputReg(GamepadInputHandleHighStart) {}

    void Register(HWND window, UINT flags) {
        if (gUniqLogRawInputRegister) {
            LOG << "Listening to device events via RawInput API" << END;
        }
        if (G.ApiDebug) {
            LOG << "RegisterRawInputGamepad - registering for " << window << END;
        }

        RawInputRegBase::Register(window, flags, [this] {
            for (int i = 0; i < IMPL_MAX_USERS; i++) {
                DeviceIntf *device = ImplGetDevice(i);
                if (device && device->HasHid()) {
                    auto &reg = Regs[i];
                    reg.Active = true;
                    reg.BufList = GBufferLists.Get(GetRawInputSize(device));

                    reg.CbIter = G.Users[i].Callbacks.Add([this](ImplUser *user) {
                        OnMessage(user);
                        return true;
                    });

                    if (Flags & RIDEV_DEVNOTIFY) {
                        OnNotifyMessage(&G.Users[i], true);
                    }
                }
            }

            if (Flags & RIDEV_DEVNOTIFY) {
                NotifyCbIter = G.GlobalCallbacks.Add([this](ImplUser *user, bool added, bool onInit) {
                    DBG_ASSERT(!onInit, "too early");
                    OnNotifyMessage(user, added);
                    return true;
                });
            }
        });
    }

    void Unregister(UINT flags) {
        if (G.ApiDebug) {
            LOG << "RegisterRawInputGamepad - unregistering" << END;
        }

        RawInputRegBase::Unregister(flags, [this] {
            for (int i = 0; i < IMPL_MAX_USERS; i++) {
                if (Regs[i].Active) {
                    G.Users[i].Callbacks.Remove(Regs[i].CbIter);
                    Regs[i].Active = false;
                }
            }

            if (Flags & RIDEV_DEVNOTIFY) {
                G.GlobalCallbacks.Remove(NotifyCbIter);
            }
        });
    }

    void UpdateRealRegister(bool force) {}
} GRawInputRegGamepad;

class DeviceFinder {
    DWORD mType;
    HANDLE mDevice;
    bool mRecheck = true;

public:
    DeviceFinder(DWORD type) : mType(type) {}

    HANDLE GetDevice() {
        if (mRecheck) {
            while (true) {
                UINT count = 0;
                GetRawInputDeviceList_Real(nullptr, &count, sizeof(RAWINPUTDEVICELIST));

                auto devices = UniquePtr<RAWINPUTDEVICELIST[]>::New(count);
                UINT realCount = GetRawInputDeviceList_Real(devices.get(), &count, sizeof(RAWINPUTDEVICELIST));
                if (realCount == INVALID_UINT_VALUE && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    continue;
                }

                // can we do something smarter than choosing arbitrarily?
                HANDLE newDevice = nullptr;
                if (realCount != INVALID_UINT_VALUE) {
                    for (UINT i = 0; i < realCount; i++) {
                        if (devices[i].dwType == mType) {
                            newDevice = devices[i].hDevice;
                        }
                    }
                }

                mDevice = newDevice;
                mRecheck = false;
                break;
            }
        }
        return mDevice;
    }

    void Invalidate() {
        mRecheck = true;
    }

    void Suggest(HANDLE handle) {
        if (mRecheck && handle) {
            mDevice = handle;
        }
    }

} GKeyboardDeviceFinder{RIM_TYPEKEYBOARD}, GMouseDeviceFinder{RIM_TYPEMOUSE};

void RawInputUpdateKeyboard() {
    GRawInputRegKeyboard.UpdateRealRegister(false);
}

void RawInputUpdateMouse() {
    GRawInputRegMouse.UpdateRealRegister(false);
}

void ProcessMouseWheel(RAWMOUSE &mouse, int mask, bool horiz, DWORD time, ChangedMask *changes) {
    if ((mouse.usButtonFlags & mask) && ImplMouseWheelHook(horiz, (SHORT)mouse.usButtonData, time, changes)) {
        mouse.usButtonFlags &= ~mask;
    }
}

void ProcessMouseButton(RAWMOUSE &mouse, int mask, int key, bool down, DWORD time, ChangedMask *changes) {
    if ((mouse.usButtonFlags & mask) && ImplProcessMouseButton(key, down, time, changes)) {
        mouse.usButtonFlags &= ~mask;
    }
}

void ProcessRawInput(HRAWINPUT handle, DWORD time) {
    RAWINPUT input;
    UINT inputSize = sizeof(input);
    if ((int)GetRawInputData_Real(handle, RID_INPUT, &input, &inputSize, sizeof(RAWINPUTHEADER)) >= 0 &&
        input.header.dwType == RIM_TYPEMOUSE) {
        auto &mouse = input.data.mouse;

        bool injected = input.header.hDevice == nullptr; // best we can do?
        if (!injected) {
            GMouseDeviceFinder.Suggest(input.header.hDevice);
        }

        if (mouse.ulExtraInformation == ExtraInfoOurInject) {
            input.header.hDevice = GMouseDeviceFinder.GetDevice();
        }

        bool locallyInjected = injected && IsExtraInfoLocal(mouse.ulExtraInformation);
        if (locallyInjected) {
            mouse.ulExtraInformation = (ULONG)GAppExtraInfo.GetOrig(mouse.ulExtraInformation);
        }

        if (G.Trace) {
            LOG << "raw mouse event: " << mouse.lLastX << "," << mouse.lLastY << ", " << mouse.usButtonFlags << ", " << injected << END;
        }

        if (!locallyInjected) {
            ChangedMask changes;

            if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 && // else, trackpad?
                (mouse.lLastX != 0 || mouse.lLastY != 0) &&
                ImplMouseMotionHook(mouse.lLastX, mouse.lLastY, time, &changes)) {
                mouse.lLastX = mouse.lLastY = 0;
            }

            ProcessMouseWheel(mouse, RI_MOUSE_WHEEL, false, time, &changes);
            ProcessMouseWheel(mouse, RI_MOUSE_HWHEEL, true, time, &changes);

            ProcessMouseButton(mouse, RI_MOUSE_LEFT_BUTTON_DOWN, VK_LBUTTON, true, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_LEFT_BUTTON_UP, VK_LBUTTON, false, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_RIGHT_BUTTON_DOWN, VK_RBUTTON, true, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_RIGHT_BUTTON_UP, VK_RBUTTON, false, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_MIDDLE_BUTTON_DOWN, VK_MBUTTON, true, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_MIDDLE_BUTTON_UP, VK_MBUTTON, false, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_BUTTON_4_DOWN, VK_XBUTTON1, true, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_BUTTON_4_UP, VK_XBUTTON1, false, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_BUTTON_5_DOWN, VK_XBUTTON2, true, time, &changes);
            ProcessMouseButton(mouse, RI_MOUSE_BUTTON_5_UP, VK_XBUTTON2, false, time, &changes);

            if (mouse.usButtonFlags == 0 && mouse.lLastX == 0 && mouse.lLastY == 0) {
                return;
            }
        }

        GRawInputRegMouse.OnEvent(&input);
    }
}

bool ProcessRawKeyboardEvent(int msg, int key, int scan, int flags, ULONG extraInfo, bool injected) {
    if (GRawInputRegKeyboard.IsRegistered()) {
        RAWINPUT raw = {};
        raw.header.dwType = RIM_TYPEKEYBOARD;
        if (!injected || extraInfo == ExtraInfoOurInject) {
            raw.header.hDevice = GKeyboardDeviceFinder.GetDevice();
        }

        if (injected && IsExtraInfoLocal(extraInfo)) {
            extraInfo = (ULONG)GAppExtraInfo.GetOrig(extraInfo);
        }

        int origKey = key;
        ImplCombineLeftRight(&key, &flags, LLKHF_EXTENDED);

        raw.data.keyboard.Flags = (flags & LLKHF_UP) ? RI_KEY_BREAK : RI_KEY_MAKE;
        if (flags & LLKHF_EXTENDED) {
            raw.data.keyboard.Flags |= RI_KEY_E0;
        }

        raw.data.keyboard.VKey = key;
        raw.data.keyboard.MakeCode = scan;
        raw.data.keyboard.Message = msg;
        raw.data.keyboard.ExtraInformation = extraInfo;

        GRawInputRegKeyboard.OnEvent(&raw);

        if (GRawInputRegKeyboard.InNoLegacyMode()) {
            UpdateManualAsyncKeyState(origKey, key, !(flags & LLKHF_UP));
            return true;
        }
    }

    return false;
}

void ProcessRawDeviceChange(HANDLE handle, bool added) {
    if (added) {
        RID_DEVICE_INFO info;
        UINT infoSize = sizeof(info);
        info.cbSize = infoSize;
        if ((int)GetRawInputDeviceInfoW_Real(handle, RIDI_DEVICEINFO, &info, &infoSize) >= 0) {
            switch (info.dwType) {
            case RIM_TYPEKEYBOARD:
                GRawInputRegKeyboard.OnDeviceAdded(handle);
                break;
            case RIM_TYPEMOUSE:
                GRawInputRegMouse.OnDeviceAdded(handle);
                break;
            }
        }
    } else {
        // Handle belongs to one of the two...
        GRawInputRegKeyboard.OnDeviceRemoved(handle);
        GRawInputRegMouse.OnDeviceRemoved(handle);
    }
}

LRESULT CALLBACK DllWindowProc(HWND win, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_INPUT:
        ProcessRawInput((HRAWINPUT)l, GetMessageTime());
        break;

    case WM_INPUT_DEVICE_CHANGE:
        ProcessRawDeviceChange((HANDLE)l, w == GIDC_ARRIVAL);
        break;

    case WM_DEVICECHANGE:
        GKeyboardDeviceFinder.Invalidate();
        GMouseDeviceFinder.Invalidate();
        break;
    }

    return DefWindowProcW(win, msg, w, l);
}

void RawInputInitDllWindow() {
    G.DllWindow = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ASSERT(G.DllWindow, "Message window couldn't be created");
    SetWindowLongPtrW(G.DllWindow, GWLP_WNDPROC, (LONG_PTR)DllWindowProc);
}

void RawInputPreregisterEarly() {
    // start with these excluded, to avoid inconsistent behaviour if register is called early
    DBG_ASSERT(!G.Keyboard.IsMapped && !G.Mouse.IsMapped, "wrong call");

    RawInputUpdateKeyboard();
    RawInputUpdateMouse();
}
