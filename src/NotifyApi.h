#pragma once
#include "Hook.h"
#include "Device.h"
#include "CfgMgr.h"
#include "Header.h"
#include "XUsbApi.h"
#include <Dbt.h>

struct ImplThreadPoolNotification {
    ImplGlobalCb CbIter;
    ImplThreadPoolNotificationCb Cb;
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

    void Register(void *handle, ImplThreadPoolNotificationCb &&cb) {
        lock_guard<mutex> lock(mMutex);

        auto &notify = mNotifications[handle];
        if (!notify) {
            notify = SharedPtr<ImplThreadPoolNotification>::New();
        }
        notify->Cb = move(cb);

        notify->CbIter = G.GlobalCallbacks.Add([notify](ImplUser *user, bool added, bool onInit) {
            DeviceIntf *device = user->Device; // user->Device may change (currently never freed)
            if (device && !onInit) {
                QueueUserWorkItem([](LPVOID param) -> DWORD {
                    auto cb = (function<void()> *)param;
                    (*cb)();
                    delete cb;
                    return 0;
                },
                                  new function<void()>([notify, device, added] {
                                      if (device->HasHid()) {
                                          notify->Cb(device, added);
                                      }
                                      if (device->HasXUsb()) {
                                          notify->Cb(&device->XUsbNode, added);
                                      }
                                  }),
                                  0);
            }
            return true;
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

void *ThreadPoolNotificationAllocate() { return GThreadPoolNotifications.Allocate(); }
void ThreadPoolNotificationRegister(void *handle, ImplThreadPoolNotificationCb &&cb) { return GThreadPoolNotifications.Register(handle, move(cb)); }
bool ThreadPoolNotificationUnregister(void *handle, bool wait) { return GThreadPoolNotifications.Unregister(handle, wait); }

// A and W share same exact implementation, though just to be safe - I've kept each calling the right real function
HDEVNOTIFY WINAPI RegisterDeviceNotification_Hook(HANDLE hRecepient, LPVOID NotificationFilter, DWORD Flags,
                                                  decltype(RegisterDeviceNotificationA) RegisterDeviceNotification_Real) {
    if (G.ApiDebug) {
        LOG << "RegisterDeviceNotification " << NotificationFilter << " " << Flags << END;
    }

    if (Flags & DEVICE_NOTIFY_SERVICE_HANDLE) {
        LOG_ERR << "Unsupported service device notification" << END;
    } else if (NotificationFilter) {
        HWND window = (HWND)hRecepient;
        DEV_BROADCAST_HDR *header = (DEV_BROADCAST_HDR *)NotificationFilter;

        switch (header->dbch_devicetype) {
        case DBT_DEVTYP_DEVICEINTERFACE: {
            GUID *guid = &((DEV_BROADCAST_DEVICEINTERFACE_W *)header)->dbcc_classguid; // _W is same as _A for input

            FilterResult filter;
            if ((Flags & DEVICE_NOTIFY_ALL_INTERFACE_CLASSES) ||
                DeviceInterfaceListFilterMatches<wchar_t>(guid, nullptr, &filter)) {
                HDEVNOTIFY notify = RegisterDeviceNotification_Real(hRecepient, NotificationFilter, Flags);
                if (notify) {
                    ThreadPoolNotificationRegister(notify, [window, filter](DeviceNode *node, bool added) {
                        if (filter.Matches(node)) {
                            bool unicode = IsWindowUnicode(window);
                            size_t size = unicode ? // the DEV_BROADCAST_DEVICEINTERFACE_* sizeof includes the null char
                                              sizeof(DEV_BROADCAST_DEVICEINTERFACE_W) + sizeof(wchar_t) * wcslen(node->DevicePathW)
                                                  : sizeof(DEV_BROADCAST_DEVICEINTERFACE_A) + strlen(node->DevicePathA);

                            DEV_BROADCAST_DEVICEINTERFACE_W *broadcast = (DEV_BROADCAST_DEVICEINTERFACE_W *)new byte[size];
                            ZeroMemory(broadcast, sizeof(*broadcast));
                            broadcast->dbcc_size = (int)size;
                            broadcast->dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
                            broadcast->dbcc_classguid = node->DeviceIntfGuid();

                            WPARAM event = added ? DBT_DEVICEARRIVAL : DBT_DEVICEREMOVECOMPLETE;
                            if (unicode) {
                                wcscpy(broadcast->dbcc_name, node->DevicePathW);
                                SendMessageW(window, WM_DEVICECHANGE, event, (LPARAM)broadcast);
                            } else {
                                strcpy(((DEV_BROADCAST_DEVICEINTERFACE_A *)broadcast)->dbcc_name, node->DevicePathA);
                                SendMessageA(window, WM_DEVICECHANGE, event, (LPARAM)broadcast);
                            }

                            delete[] broadcast;
                        }
                    });
                }
                return notify;
            }
            break;
        }

        case DBT_DEVTYP_HANDLE: {
            HANDLE handle = ((DEV_BROADCAST_HANDLE *)header)->dbch_handle;
            DeviceNode *handleNode = GetDeviceNodeByHandle(handle);

            if (handleNode) {
                HDEVNOTIFY notify = ThreadPoolNotificationAllocate();
                ThreadPoolNotificationRegister(notify, [handleNode, handle, notify, window](DeviceNode *node, bool added) {
                    if (node == handleNode && !added) {
                        DEV_BROADCAST_HANDLE broadcast;
                        ZeroMemory(&broadcast, sizeof(broadcast));
                        broadcast.dbch_size = sizeof(broadcast);
                        broadcast.dbch_devicetype = DBT_DEVTYP_HANDLE;
                        broadcast.dbch_handle = handle;
                        broadcast.dbch_hdevnotify = notify;
                        broadcast.dbch_nameoffset = -1;

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
            LOG_ERR << "Unknown device notification type: " << header->dbch_devicetype << END;
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

    if (ThreadPoolNotificationUnregister(Handle, false)) {
        return TRUE;
    } else {
        return UnregisterDeviceNotification_Real(Handle);
    }
}

void RegisterGlobalNotify() {
    HDEVNOTIFY notify = ThreadPoolNotificationAllocate();
    ThreadPoolNotificationRegister(notify, [](DeviceNode *node, bool added) {
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
    ADD_GLOBAL_HOOK(RegisterDeviceNotificationA);
    ADD_GLOBAL_HOOK(RegisterDeviceNotificationW);
    ADD_GLOBAL_HOOK(UnregisterDeviceNotification);
}
