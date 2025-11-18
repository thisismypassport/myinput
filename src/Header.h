#pragma once
#include "UtilsBase.h"
#include <Windows.h>
#include <cfgmgr32.h>

#define MYINPUT_HOOK_DLL_DECLSPEC __declspec(dllexport)
#include "MyInputHook.h"

CONFIGRET(WINAPI *CM_Get_Parent_Real)
(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) = CM_Get_Parent;
CONFIGRET(WINAPI *CM_Get_Parent_Ex_Real)
(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Parent_Ex;
CONFIGRET(WINAPI *CM_Get_Child_Real)
(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) = CM_Get_Child;
CONFIGRET(WINAPI *CM_Get_Child_Ex_Real)
(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Child_Ex;
CONFIGRET(WINAPI *CM_Get_Sibling_Real)
(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) = CM_Get_Sibling;
CONFIGRET(WINAPI *CM_Get_Sibling_Ex_Real)
(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Sibling_Ex;
CONFIGRET(WINAPI *CM_Get_Depth_Real)
(PULONG pulDepth, DEVINST dnDevInst, ULONG ulFlags) = CM_Get_Depth;
CONFIGRET(WINAPI *CM_Get_Depth_Ex_Real)
(PULONG pulDepth, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Depth_Ex;

CONFIGRET(WINAPI *CM_Get_Device_ID_Size_Real)
(PULONG pulLen, DEVINST dnDevInst, ULONG ulFlags) = CM_Get_Device_ID_Size;
CONFIGRET(WINAPI *CM_Get_Device_ID_Size_Ex_Real)
(PULONG pulLen, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_ID_Size_Ex;
CONFIGRET(WINAPI *CM_Get_Device_IDA_Real)
(DEVINST dnDevInst, PSTR buffer, ULONG bufferLen, ULONG ulFlags) = CM_Get_Device_IDA;
CONFIGRET(WINAPI *CM_Get_Device_ID_ExA_Real)
(DEVINST dnDevInst, PSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_ID_ExA;
CONFIGRET(WINAPI *CM_Get_Device_IDW_Real)
(DEVINST dnDevInst, PWSTR buffer, ULONG bufferLen, ULONG ulFlags) = CM_Get_Device_IDW;
CONFIGRET(WINAPI *CM_Get_Device_ID_ExW_Real)
(DEVINST dnDevInst, PWSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_ID_ExW;
CONFIGRET(WINAPI *CM_Get_Device_ID_List_SizeA_Real)
(PULONG pulLen, PCSTR pszFilter, ULONG ulFlags) = CM_Get_Device_ID_List_SizeA;
CONFIGRET(WINAPI *CM_Get_Device_ID_List_Size_ExA_Real)
(PULONG pulLen, PCSTR pszFilter, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_ID_List_Size_ExA;
CONFIGRET(WINAPI *CM_Get_Device_ID_List_SizeW_Real)
(PULONG pulLen, PCWSTR pszFilter, ULONG ulFlags) = CM_Get_Device_ID_List_SizeW;
CONFIGRET(WINAPI *CM_Get_Device_ID_List_Size_ExW_Real)
(PULONG pulLen, PCWSTR pszFilter, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_ID_List_Size_ExW;
CONFIGRET(WINAPI *CM_Get_Device_ID_ListA_Real)
(PCSTR pszFilter, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags) = CM_Get_Device_ID_ListA;
CONFIGRET(WINAPI *CM_Get_Device_ID_List_ExA_Real)
(PCSTR pszFilter, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_ID_List_ExA;
CONFIGRET(WINAPI *CM_Get_Device_ID_ListW_Real)
(PCWSTR pszFilter, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags) = CM_Get_Device_ID_ListW;
CONFIGRET(WINAPI *CM_Get_Device_ID_List_ExW_Real)
(PCWSTR pszFilter, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_ID_List_ExW;

CONFIGRET(WINAPI *CM_Locate_DevNodeA_Real)
(PDEVINST pdnDevInst, DEVINSTID_A pDeviceID, ULONG ulFlags) = CM_Locate_DevNodeA;
CONFIGRET(WINAPI *CM_Locate_DevNode_ExA_Real)
(PDEVINST pdnDevInst, DEVINSTID_A pDeviceID, ULONG ulFlags, HMACHINE hMachine) = CM_Locate_DevNode_ExA;
CONFIGRET(WINAPI *CM_Locate_DevNodeW_Real)
(PDEVINST pdnDevInst, DEVINSTID_W pDeviceID, ULONG ulFlags) = CM_Locate_DevNodeW;
CONFIGRET(WINAPI *CM_Locate_DevNode_ExW_Real)
(PDEVINST pdnDevInst, DEVINSTID_W pDeviceID, ULONG ulFlags, HMACHINE hMachine) = CM_Locate_DevNode_ExW;
CONFIGRET(WINAPI *CM_Get_DevNode_Status_Real)
(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, ULONG ulFlags) = CM_Get_DevNode_Status;
CONFIGRET(WINAPI *CM_Get_DevNode_Status_Ex_Real)
(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, ULONG ulFlags, HMACHINE hMachine) = CM_Get_DevNode_Status_Ex;
CONFIGRET(WINAPI *CM_Get_DevNode_Property_Keys_Real)
(DEVINST dnDevInst, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags) = CM_Get_DevNode_Property_Keys;
CONFIGRET(WINAPI *CM_Get_DevNode_Property_Keys_Ex_Real)
(DEVINST dnDevInst, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags, HMACHINE hMachine) = CM_Get_DevNode_Property_Keys_Ex;
CONFIGRET(WINAPI *CM_Get_DevNode_PropertyW_Real)
(DEVINST dnDevInst, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags) = CM_Get_DevNode_PropertyW;
CONFIGRET(WINAPI *CM_Get_DevNode_Property_ExW_Real)
(DEVINST dnDevInst, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags, HMACHINE hMachine) = CM_Get_DevNode_Property_ExW;
CONFIGRET(WINAPI *CM_Get_DevNode_Registry_PropertyA_Real)
(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags) = CM_Get_DevNode_Registry_PropertyA;
CONFIGRET(WINAPI *CM_Get_DevNode_Registry_Property_ExA_Real)
(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags, HMACHINE hMachine) = CM_Get_DevNode_Registry_Property_ExA;
CONFIGRET(WINAPI *CM_Get_DevNode_Registry_PropertyW_Real)
(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags) = CM_Get_DevNode_Registry_PropertyW;
CONFIGRET(WINAPI *CM_Get_DevNode_Registry_Property_ExW_Real)
(DEVINST dnDevInst, ULONG prop, PULONG propType, PVOID buffer, PULONG pulLength, ULONG ulFlags, HMACHINE hMachine) = CM_Get_DevNode_Registry_Property_ExW;

CONFIGRET(WINAPI *CM_Get_Device_Interface_List_SizeA_Real)
(PULONG pulLen, LPGUID clsGuid, DEVINSTID_A pDeviceID, ULONG ulFlags) = CM_Get_Device_Interface_List_SizeA;
CONFIGRET(WINAPI *CM_Get_Device_Interface_List_Size_ExA_Real)
(PULONG pulLen, LPGUID clsGuid, DEVINSTID_A pDeviceID, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_Interface_List_Size_ExA;
CONFIGRET(WINAPI *CM_Get_Device_Interface_List_SizeW_Real)
(PULONG pulLen, LPGUID clsGuid, DEVINSTID_W pDeviceID, ULONG ulFlags) = CM_Get_Device_Interface_List_SizeW;
CONFIGRET(WINAPI *CM_Get_Device_Interface_List_Size_ExW_Real)
(PULONG pulLen, LPGUID clsGuid, DEVINSTID_W pDeviceID, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_Interface_List_Size_ExW;
CONFIGRET(WINAPI *CM_Get_Device_Interface_ListA_Real)
(LPGUID clsGuid, DEVINSTID_A pDeviceID, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags) = CM_Get_Device_Interface_ListA;
CONFIGRET(WINAPI *CM_Get_Device_Interface_List_ExA_Real)
(LPGUID clsGuid, DEVINSTID_A pDeviceID, PZZSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_Interface_List_ExA;
CONFIGRET(WINAPI *CM_Get_Device_Interface_ListW_Real)
(LPGUID clsGuid, DEVINSTID_W pDeviceID, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags) = CM_Get_Device_Interface_ListW;
CONFIGRET(WINAPI *CM_Get_Device_Interface_List_ExW_Real)
(LPGUID clsGuid, DEVINSTID_W pDeviceID, PZZWSTR buffer, ULONG bufferLen, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_Interface_List_ExW;
CONFIGRET(WINAPI *CM_Get_Device_Interface_Property_KeysW_Real)
(LPCWSTR pszIntf, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags) = CM_Get_Device_Interface_Property_KeysW;
CONFIGRET(WINAPI *CM_Get_Device_Interface_Property_Keys_ExW_Real)
(LPCWSTR pszIntf, DEVPROPKEY *propKeys, PULONG propKeyCount, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_Interface_Property_Keys_ExW;
CONFIGRET(WINAPI *CM_Get_Device_Interface_PropertyW_Real)
(LPCWSTR pszIntf, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags) = CM_Get_Device_Interface_PropertyW;
CONFIGRET(WINAPI *CM_Get_Device_Interface_Property_ExW_Real)
(LPCWSTR pszIntf, const DEVPROPKEY *propKey, DEVPROPTYPE *propType, PBYTE propBuf, PULONG propSize, ULONG ulFlags, HMACHINE hMachine) = CM_Get_Device_Interface_Property_ExW;

CONFIGRET(WINAPI *CM_Register_Notification_Real)
(PCM_NOTIFY_FILTER pFilter, PVOID pContext, PCM_NOTIFY_CALLBACK pCallback, PHCMNOTIFICATION pNotifyContext) = CM_Register_Notification;
CONFIGRET(WINAPI *CM_Unregister_Notification_Real)
(HCMNOTIFICATION NotifyContext) = CM_Unregister_Notification;

HANDLE(WINAPI *CreateFileA_Real)
(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
 DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) = CreateFileA;
HANDLE(WINAPI *CreateFileW_Real)
(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
 DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) = CreateFileW;
BOOL(WINAPI *DeviceIoControl_Real)
(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
 LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped) = DeviceIoControl;

HDEVNOTIFY(WINAPI *RegisterDeviceNotificationA_Real)
(HANDLE hRecepient, LPVOID NotificationFilter, DWORD Flags) = RegisterDeviceNotificationA;
HDEVNOTIFY(WINAPI *RegisterDeviceNotificationW_Real)
(HANDLE hRecepient, LPVOID NotificationFilter, DWORD Flags) = RegisterDeviceNotificationW;
BOOL(WINAPI *UnregisterDeviceNotification_Real)
(HDEVNOTIFY Handle) = UnregisterDeviceNotification;

UINT(WINAPI *GetRawInputDeviceList_Real)
(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize) = GetRawInputDeviceList;
UINT(WINAPI *GetRawInputDeviceInfoA_Real)
(HANDLE hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize) = GetRawInputDeviceInfoA;
UINT(WINAPI *GetRawInputDeviceInfoW_Real)
(HANDLE hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize) = GetRawInputDeviceInfoW;
UINT(WINAPI *GetRawInputBuffer_Real)
(PRAWINPUT pData, PUINT pCbSize, UINT cbSizeHeader) = GetRawInputBuffer;
UINT(WINAPI *GetRawInputData_Real)
(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pCbSize, UINT cbSizeHeader) = GetRawInputData;
BOOL(WINAPI *RegisterRawInputDevices_Real)
(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize) = RegisterRawInputDevices;
UINT(WINAPI *GetRegisteredRawInputDevices_Real)
(PRAWINPUTDEVICE pRawInputDevices, PUINT puiNumDevices, UINT cbSize) = GetRegisteredRawInputDevices;

HRESULT(WINAPI *CoCreateInstance_Real)
(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsCtx, REFIID riid, LPVOID *ppv) = CoCreateInstance;
HRESULT(WINAPI *CoCreateInstanceEx_Real)
(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsCtx, COSERVERINFO *pServerInfo, DWORD dwCount, MULTI_QI *pResults) = CoCreateInstanceEx;
HRESULT(WINAPI *CoCreateInstanceFromApp_Real)
(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsCtx, PVOID reserved, DWORD dwCount, MULTI_QI *pResults) = CoCreateInstanceFromApp;
HRESULT(WINAPI *CoGetClassObject_Real)
(REFCLSID rclsid, DWORD dwClsCtx, LPVOID reserved, REFIID riid, LPVOID *ppv) = CoGetClassObject;

HHOOK(WINAPI *SetWindowsHookExA_Real)
(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) = SetWindowsHookExA;
HHOOK(WINAPI *SetWindowsHookExW_Real)
(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) = SetWindowsHookExW;
BOOL(WINAPI *UnhookWindowsHookEx_Real)
(HHOOK hhk) = UnhookWindowsHookEx;

SHORT(WINAPI *GetAsyncKeyState_Real)
(int vKey) = GetAsyncKeyState;
void(WINAPI *KeybdEvent_Real)(BYTE bVk, BYTE bScan, DWORD dwFlags, ULONG_PTR dwExtraInfo) = keybd_event;
void(WINAPI *MouseEvent_Real)(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo) = mouse_event;
UINT(WINAPI *SendInput_Real)
(UINT cInputs, LPINPUT pInputs, int cbSize) = SendInput;
LPARAM(WINAPI *GetMessageExtraInfo_Real)
(VOID) = GetMessageExtraInfo;
BOOL(WINAPI *GetCurrentInputMessageSource_Real)
(INPUT_MESSAGE_SOURCE *msg) = GetCurrentInputMessageSource;
BOOL(WINAPI *GetCIMSSM_Real)
(INPUT_MESSAGE_SOURCE *msg) = GetCIMSSM;

int(WINAPI *ShowCursor_Real)(BOOL bShow) = ShowCursor;
BOOL(WINAPI *GetCursorInfo_Real)
(PCURSORINFO pci) = GetCursorInfo;
BOOL(WINAPI *ClipCursor_Real)
(const RECT *lpRect) = ClipCursor;
BOOL(WINAPI *GetClipCursor_Real)
(LPRECT lpRect) = GetClipCursor;

enum {
    // (Note: below applies to user32 handles only!)
    OurHandleLow = 0xffff, // the lower 16bits are an index, so this seems safest

    KeyboardInputHandleHighStart = 0x1000,
    MouseInputHandleHighStart = 0x2000,
    GamepadInputHandleHighStart = 0x3000,
    InputHandleHighCount = 0x1000,

    CustomDeviceHandleHighStart = 0x7200,
};

WORD GetOurHandle(HANDLE handle) {
    uintptr_t value = (uintptr_t)handle;

    if ((LONG)value == value &&
        LOWORD(value) == OurHandleLow) {
        return HIWORD(value);
    }

    return 0;
}

HANDLE MakeOurHandle(WORD high) {
    return (HANDLE)(intptr_t)MAKELONG(OurHandleLow, high);
}

enum : ULONG {
    ExtraInfoLocalPageMask = 0xffffff00,
    ExtraInfoLocalPage = 0xe9341f00,
    ExtraInfoOurInject = ExtraInfoLocalPage,
    ExtraInfoAppDefault = ExtraInfoLocalPage + 1,
    ExtraInfoAppCustomStart = ExtraInfoLocalPage + 2,
    ExtraInfoAppCustomCount = 0xfe,
};

constexpr int InputThreadPriority = THREAD_PRIORITY_TIME_CRITICAL; // ok?

using AppCallback = void (*)(void *);
void PostAppCallback(AppCallback cb, void *data);
using VoidCallback = void (*)();
void PostAppCallback(VoidCallback cb);

HANDLE GetCustomDeviceHandle(int user);
void UpdateAll();
void UpdateCursor();
void RehookCursor();
bool ProcessRawKeyboardEvent(int msg, int key, int scan, int flags, ULONG extraInfo, bool injected);
void ImplAbortMappings();
void ImplToggleForeground(bool allowUpdateAll = true);
void DebugRemoveLowHooks();
