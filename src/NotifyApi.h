#pragma once
#include "Hook.h"
#include "Device.h"
#include "Header.h"
#include "XUsbApi.h"
#include <Dbt.h>

struct ImplThreadPoolNotification {
    ImplGlobalCb CbIter;
    function<void(ImplUser *user, bool added)> Cb;
    bool Allocated = false;
};

class ImplThreadPoolNotifications {
    mutex mMutex;
    unordered_map<void *, SharedPtr<ImplThreadPoolNotification>> mNotifications;

public:
    // handle here must be a real pointer, so it's safe to allocate our own
    // (this is indeed the case for HDEVNOTIFY and HCMNOTIFICATION)

    void *Allocate() {
        auto notify = SharedPtr<ImplThreadPoolNotification>::New();
        notify->Allocated = true;

        lock_guard<mutex> lock(mMutex);
        void *handle = notify.get();
        mNotifications[handle] = move(notify);
        return handle;
    }

    void Register(void *handle, function<void(ImplUser *user, bool added)> &&cb) {
        lock_guard<mutex> lock(mMutex);

        auto &notify = mNotifications[handle];
        if (!notify) {
            notify = SharedPtr<ImplThreadPoolNotification>::New();
        }
        notify->Cb = move(cb);

        notify->CbIter = G.GlobalCallbacks.Add([notify](ImplUser *user, bool added) {
            QueueUserWorkItem([](LPVOID param) -> DWORD {
                (*((function<void()> *)param))();
                return 0;
            },
                              new function<void()>([notify, user, added]() {
                                  notify->Cb(user, added);
                              }),
                              0);
        });
    }

    // returns true if an allocated notification was unregistered
    bool Unregister(void *handle, bool wait) {
        unique_lock<mutex> lock(mMutex);

        auto iter = mNotifications.find(handle);
        if (iter == mNotifications.end()) {
            return false;
        }

        auto notify = iter->second;
        G.GlobalCallbacks.Remove(notify->CbIter);

        mNotifications.erase(iter);

        // TODO TODO! if (wait) ....

        return notify->Allocated;
    }

} GThreadPoolNotifications;

// A and W share same exact implementation, though just to be safe - I've kept each calling the right real function
HDEVNOTIFY WINAPI RegisterDeviceNotification_Hook(HANDLE hRecepient, LPVOID NotificationFilter, DWORD Flags,
                                                  decltype(RegisterDeviceNotificationA) RegisterDeviceNotification_Real) {
    if (G.ApiDebug) {
        LOG << "RegisterDeviceNotification " << NotificationFilter << " " << Flags << END;
    }

    if (Flags & DEVICE_NOTIFY_SERVICE_HANDLE) {
        LOG << "Unsupported service device notification" << END;
    } else if (NotificationFilter) {
        HWND window = (HWND)hRecepient;
        DEV_BROADCAST_HDR *header = (DEV_BROADCAST_HDR *)NotificationFilter;

        switch (header->dbch_devicetype) {
        case DBT_DEVTYP_DEVICEINTERFACE:
            if ((Flags & DEVICE_NOTIFY_ALL_INTERFACE_CLASSES) ||
                ((DEV_BROADCAST_DEVICEINTERFACE_W *)header)->dbcc_classguid == GUID_DEVINTERFACE_HID) // _W is same as _A for input
            {
                HDEVNOTIFY notify = RegisterDeviceNotification_Real(hRecepient, NotificationFilter, Flags);
                if (notify) {
                    GThreadPoolNotifications.Register(notify, [window](ImplUser *user, bool added) {
                        if (user->Device->IsHid) {
                            bool unicode = IsWindowUnicode(window);
                            size_t size = unicode ? // the DEV_BROADCAST_DEVICEINTERFACE_* sizeof includes the null char
                                              sizeof(DEV_BROADCAST_DEVICEINTERFACE_W) + sizeof(wchar_t) * wcslen(user->Device->DevicePathW)
                                                  : sizeof(DEV_BROADCAST_DEVICEINTERFACE_A) + strlen(user->Device->DevicePathA);

                            DEV_BROADCAST_DEVICEINTERFACE_W *broadcast = (DEV_BROADCAST_DEVICEINTERFACE_W *)new byte[size];
                            ZeroMemory(broadcast, sizeof(*broadcast));
                            broadcast->dbcc_size = (int)size;
                            broadcast->dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
                            broadcast->dbcc_classguid = GUID_DEVINTERFACE_HID;

                            WPARAM event = added ? DBT_DEVICEARRIVAL : DBT_DEVICEREMOVECOMPLETE;
                            if (unicode) {
                                wcscpy(broadcast->dbcc_name, user->Device->DevicePathW);
                                SendMessageW(window, WM_DEVICECHANGE, event, (LPARAM)broadcast);
                            } else {
                                strcpy(((DEV_BROADCAST_DEVICEINTERFACE_A *)broadcast)->dbcc_name, user->Device->DevicePathA);
                                SendMessageA(window, WM_DEVICECHANGE, event, (LPARAM)broadcast);
                            }

                            delete[] broadcast;
                        }
                    });
                }
                return notify;
            }

        case DBT_DEVTYP_HANDLE: {
            HANDLE handle = ((DEV_BROADCAST_HANDLE *)header)->dbch_handle;
            DeviceIntf *device = GetDeviceByHandle(handle);

            if (device) {
                HDEVNOTIFY notify = GThreadPoolNotifications.Allocate();
                GThreadPoolNotifications.Register(notify, [device, handle, notify, window](ImplUser *user, bool added) {
                    if (user->Device == device && !added) {
                        DEV_BROADCAST_HANDLE broadcast;
                        ZeroMemory(&broadcast, sizeof(broadcast));
                        broadcast.dbch_size = sizeof(broadcast);
                        broadcast.dbch_devicetype = DBT_DEVTYP_HANDLE;
                        broadcast.dbch_handle = handle;
                        broadcast.dbch_hdevnotify = notify;

                        if (IsWindowUnicode(window)) {
                            SendMessageW(window, WM_DEVICECHANGE, DBT_DEVICEREMOVECOMPLETE, (LPARAM)&broadcast);
                        } else {
                            SendMessageA(window, WM_DEVICECHANGE, DBT_DEVICEREMOVECOMPLETE, (LPARAM)&broadcast);
                        }
                    }
                });
                return notify;
            }
            break;
        }

        default:
            LOG << "Unknown device notification type: " << header->dbch_devicetype << END;
            break;
        }
    }

    return RegisterDeviceNotification_Real(hRecepient, NotificationFilter, Flags);
}

HDEVNOTIFY WINAPI RegisterDeviceNotificationA_Hook(HANDLE hRecepient, LPVOID NotificationFilter, DWORD Flags) {
    return RegisterDeviceNotification_Hook(hRecepient, NotificationFilter, Flags, RegisterDeviceNotificationA_Real);
}

HDEVNOTIFY WINAPI RegisterDeviceNotificationW_Hook(HANDLE hRecepient, LPVOID NotificationFilter, DWORD Flags) {
    return RegisterDeviceNotification_Hook(hRecepient, NotificationFilter, Flags, RegisterDeviceNotificationW_Real);
}

BOOL WINAPI UnregisterDeviceNotification_Hook(HDEVNOTIFY Handle) {
    if (G.ApiDebug) {
        LOG << "UnregisterDeviceNotification" << END;
    }

    if (GThreadPoolNotifications.Unregister(Handle, false)) {
        return TRUE;
    } else {
        return UnregisterDeviceNotification_Real(Handle);
    }
}

void RegisterGlobalNotify() {
    HDEVNOTIFY notify = GThreadPoolNotifications.Allocate();
    GThreadPoolNotifications.Register(notify, [](ImplUser *user, bool added) {
        EnumWindows([](HWND window, LPARAM param) -> BOOL {
            if (IsWindowInOurProcess(window)) {
                if (IsWindowUnicode(window)) {
                    SendMessageW(window, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0);
                } else {
                    SendMessageA(window, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0);
                }
            }
            return true;
        },
                    0);
    });
}

void HookNotifyApi() {
    // The below just delegate to CM, but to a private function, so could've implemented that instead...
    AddGlobalHook(&RegisterDeviceNotificationA_Real, RegisterDeviceNotificationA_Hook);
    AddGlobalHook(&RegisterDeviceNotificationW_Real, RegisterDeviceNotificationW_Hook);
    AddGlobalHook(&UnregisterDeviceNotification_Real, UnregisterDeviceNotification_Hook);
}
