#pragma once
#include "Hook.h"
#include "Device.h"
#include "Header.h"
#include "XUsbApi.h"
#include "UtilsBuffer.h"

bool gUniqLogDeviceOpen;

class ImplProcessPipeThread {
    DeviceIntf *Device;
    Path FinalName;
    HANDLE Pipe;
    HANDLE ReadEvent, WriteEvent, SendEvent, CancelEvent;
    deque<Buffer> Reports;
    mutex ReportsMutex;
    BufferList *ReportBuffers;
    BufferList *OutputBuffers;
    ImplUserCb CbIter;

    WeakAtomic<ULONG> MaxBuffers = 0x20;
    WeakAtomic<ULONG> PollFreq = 0x10;
    WeakAtomic<bool> Immediate = false;
    WeakAtomic<ULONG> EffectiveMaxBuffers;

    ImplUser *User() { return &G.Users[Device->UserIdx]; }

    Buffer CreateReport() {
        Buffer buffer(ReportBuffers);
        buffer.SetSize(Device->CopyInputTo((uint8_t *)buffer.Ptr()));
        return buffer;
    }

    void SendReport(Buffer &&buffer) {
        lock_guard<mutex> local_lock(ReportsMutex);
        if (Reports.size() >= EffectiveMaxBuffers) {
            Reports.pop_front();
        }
        Reports.push_back(move(buffer));
        if (Reports.size() == 1) {
            SetEvent(SendEvent);
        }
    }

    bool GetNextReport(Buffer *report) {
        if (Immediate) {
            // latest report
            *report = CreateReport();
            return true;
        }

        lock_guard<mutex> local_lock(ReportsMutex);
        if (Reports.empty()) {
            return false;
        }

        *report = move(Reports.front());
        Reports.pop_front();
        return true;
    }

    void ProcessThread() {
        enum {
            CanRead = WAIT_OBJECT_0,
            CanWrite = WAIT_OBJECT_0 + 1,
            CanSend = WAIT_OBJECT_0 + 2,
            CanCancel = WAIT_OBJECT_0 + 3,
            NumWaitObjs = 4,
        };

        Buffer readBuffer(OutputBuffers);
        Buffer writeBuffer;
        OVERLAPPED ReadOverlapped, WriteOverlapped;
        bool didRead = false;
        bool didWrite = false;
        while (true) {
            HANDLE objects[] = {ReadEvent, WriteEvent, SendEvent, CancelEvent};
            int which = WaitForMultipleObjects(NumWaitObjs, objects, false, INFINITE);
            if (which == CanRead) {
                if (didRead) {
                    DWORD numRead = 0;
                    if (!GetOverlappedResult(Pipe, &ReadOverlapped, &numRead, false) && GetLastError() == ERROR_BROKEN_PIPE) {
                        break;
                    }

                    Device->ProcessOutput((const uint8_t *)readBuffer.Ptr(), numRead);
                }

                ZeroMemory(&ReadOverlapped, sizeof(OVERLAPPED));
                ReadOverlapped.hEvent = ReadEvent;

                if (!ReadFile(Pipe, readBuffer.Ptr(), (int)readBuffer.Size(), nullptr, &ReadOverlapped) && GetLastError() == ERROR_BROKEN_PIPE) {
                    break;
                }
                didRead = true;
            } else {
                if (which == CanCancel && didWrite) {
                    CancelIoEx(Pipe, &WriteOverlapped);
                }

                if (which == CanWrite) {
                    DWORD numWrite = 0;
                    if (!GetOverlappedResult(Pipe, &WriteOverlapped, &numWrite, false) && GetLastError() == ERROR_BROKEN_PIPE) {
                        continue; // wait until read part breaks
                    }

                    if (G.ApiTrace) {
                        LOG << "Wrote hid event to pipe " << Pipe << END;
                    }
                    didWrite = false;
                }

                if (!didWrite && GetNextReport(&writeBuffer)) {
                    ZeroMemory(&WriteOverlapped, sizeof(OVERLAPPED));
                    WriteOverlapped.hEvent = WriteEvent;

                    if (!WriteFile(Pipe, writeBuffer.Ptr(), (int)writeBuffer.Size(), nullptr, &WriteOverlapped) && GetLastError() == ERROR_BROKEN_PIPE) {
                        continue; // wait until read part breaks
                    }

                    didWrite = true;
                }
            }
        }

        // Wait for write to finish first
        DWORD unused;
        if (didWrite) {
            GetOverlappedResult(Pipe, &WriteOverlapped, &unused, true);
        }

        Stop();
    }

    static DWORD WINAPI ProcessThread(LPVOID param) {
        ((ImplProcessPipeThread *)param)->ProcessThread();
        return 0;
    }

    void OnEnded();

    void UpdateMaxBuffers() {
        EffectiveMaxBuffers = Immediate ? 0 : MaxBuffers.get();

        lock_guard<mutex> local_lock(ReportsMutex);
        while (Reports.size() > EffectiveMaxBuffers) {
            Reports.pop_front();
        }
    }

public:
    ImplProcessPipeThread(HANDLE pipe, DeviceIntf *device, Path &&finalName) : Pipe(pipe), Device(device), FinalName(move(finalName)) {
        ReadEvent = CreateEventW(nullptr, false, true, nullptr);
        WriteEvent = CreateEventW(nullptr, false, false, nullptr);
        SendEvent = CreateEventW(nullptr, false, false, nullptr);
        CancelEvent = CreateEventW(nullptr, false, false, nullptr);
        UpdateMaxBuffers();
    }

    ~ImplProcessPipeThread() {
        if (G.ApiDebug) {
            LOG << "Pipe finished " << Pipe << END;
        }
        CloseHandle(ReadEvent);
        CloseHandle(WriteEvent);
        CloseHandle(SendEvent);
        CloseHandle(CancelEvent);
        CloseHandle(Pipe);
    }

    bool Matches(const wchar_t *path) {
        return wcscmp(path, FinalName) == 0;
    }

    ULONG GetPollFreq() { return PollFreq; }
    ULONG GetMaxBuffers() { return MaxBuffers; }

    void SetPollFreq(ULONG value) {
        PollFreq = Clamp<ULONG>(value, 0, 10000);

        bool immediate = value == 0;
        bool oldImmediate = Immediate.exchange(immediate);
        UpdateMaxBuffers();
        if (immediate && !oldImmediate) {
            SetEvent(SendEvent);
        }
    }

    void SetMaxBuffers(ULONG value) {
        MaxBuffers = Clamp<ULONG>(value, 2, 0x200);
        UpdateMaxBuffers();
    }

    void FlushReports() {
        lock_guard<mutex> local_lock(ReportsMutex);
        Reports.clear();
        SetEvent(CancelEvent);
    }

    void Stop() {
        User()->RemoveCallback(CbIter);
        OnEnded();
        delete this;
    }

    void Start() {
        ReportBuffers = GBufferLists.Get(Device->Preparsed->Input.Bytes);
        OutputBuffers = GBufferLists.Get(Device->Preparsed->Output.Bytes);

        CbIter = User()->AddCallback([this](ImplUser *user) {
            if (Immediate) {
                FlushReports();
            } else {
                SendReport(CreateReport());
            }
        });
        CloseHandle(CreateThread(nullptr, 0, ImplProcessPipeThread::ProcessThread, this, 0, nullptr));
    }
};

class ImplProcessPipeThreads {
    atomic<UINT> mSequence;
    vector<ImplProcessPipeThread *> mThreads[IMPL_MAX_USERS];
    mutex mMutex;

public:
    HANDLE CreatePipeHandle(DeviceIntf *device, DWORD flags) {
        UINT seq = ++mSequence;

        // See also device's FinalPipePrefix
        wchar_t pipeName[MAX_PATH];
        wsprintfW(pipeName, LR"(\\.\Pipe\MyInputHook_%d.%d.%d)", GetCurrentProcessId(), device->UserIdx, seq);

        Path finalName(MAX_PATH);
        wsprintfW(finalName, L"%s%d", device->FinalPipePrefix.Get(), seq);

        HANDLE pipe = CreateNamedPipeW(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
                                       1, 0, 0, 0, nullptr); // 0 buffer helps with immediate mode, at least

        HANDLE client = CreateFileW_Real(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL | (flags & FILE_FLAG_OVERLAPPED), nullptr);

        if (pipe == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE) {
            LOG << "failed creating pipe" << END;
        } else {
            if (!gUniqLogDeviceOpen) {
                gUniqLogDeviceOpen = true;
                LOG << "Opening device via file API" << END;
            }
            if (G.ApiDebug) {
                LOG << "Created pipe " << pipe << END;
            }

            DWORD readMode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(client, &readMode, nullptr, nullptr); // (doesn't help that much - still reads partials...)

            // must create after client exists as this starts reading
            lock_guard<mutex> lock(mMutex);
            auto thread = new ImplProcessPipeThread(pipe, device, move(finalName));
            mThreads[device->UserIdx].push_back(thread);
            thread->Start();
        }

        return client;
    }

    void OnPipeThreadEnded(ImplProcessPipeThread *thread, int userIdx) {
        lock_guard<mutex> lock(mMutex);
        Erase(mThreads[userIdx], thread);
    }

    template <class TFunc>
    BOOL WithPipeThread(DeviceIntf *device, const wchar_t *finalName, TFunc &&func) {
        lock_guard<mutex> lock(mMutex);
        auto &threads = mThreads[device->UserIdx];
        for (int i = 0; i < (int)threads.size(); i++) {
            if (threads[i]->Matches(finalName)) {
                func(threads[i]);
                return TRUE;
            }
        }

        LOG << "Couldn't find pipe thread?" << END;
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

} GPipeThreads;

void ImplProcessPipeThread::OnEnded() {
    GPipeThreads.OnPipeThreadEnded(this, Device->UserIdx);
}

DeviceIntf *GetDeviceByHandle(HANDLE handle, Path *outPath = nullptr) {
    if (GetFileType(handle) == FILE_TYPE_PIPE) {
        Path finalPath = PathGetFinalPath(handle, FILE_NAME_OPENED | VOLUME_NAME_NT);

        for (int i = 0; i < IMPL_MAX_USERS; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device && wcsncmp(finalPath, device->FinalPipePrefix, device->FinalPipePrefixLen) == 0) {
                if (outPath) {
                    *outPath = move(finalPath);
                }
                return device;
            }
        }
    }

    return nullptr;
}

HANDLE WINAPI CreateFileA_Hook(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                               DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (lpFileName && _strnicmp(lpFileName, DEVICE_NAME_PREFIX, DEVICE_NAME_PREFIX_LEN) == 0) {
        for (int i = 0; i < IMPL_MAX_USERS; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device && _stricmp(lpFileName, device->DevicePathA) == 0) {
                if (G.ApiDebug) {
                    LOG << "CreateFileA (" << lpFileName << "," << dwDesiredAccess << "," << dwFlagsAndAttributes << ")" << END;
                }
                return GPipeThreads.CreatePipeHandle(device, dwFlagsAndAttributes);
            } else if (device && device->IsXInput && _stricmp(lpFileName, device->XDevicePathA) == 0) {
                if (G.ApiDebug) {
                    LOG << "CreateFileA (" << lpFileName << "," << dwDesiredAccess << "," << dwFlagsAndAttributes << ")" << END;
                }
                return XUsbCreateFile(device, dwFlagsAndAttributes);
            }
        }
    }

    return CreateFileA_Real(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

HANDLE WINAPI CreateFileW_Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                               DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (lpFileName && _wcsnicmp(lpFileName, DEVICE_NAME_PREFIX_W, DEVICE_NAME_PREFIX_LEN) == 0) {
        for (int i = 0; i < IMPL_MAX_USERS; i++) {
            DeviceIntf *device = ImplGetDevice(i);
            if (device && _wcsicmp(lpFileName, device->DevicePathW) == 0) {
                if (G.ApiDebug) {
                    LOG << "CreateFileW (" << lpFileName << "," << dwDesiredAccess << "," << dwFlagsAndAttributes << ")" << END;
                }
                return GPipeThreads.CreatePipeHandle(device, dwFlagsAndAttributes);
            } else if (device && device->IsXInput && _wcsicmp(lpFileName, device->XDevicePathW) == 0) {
                if (G.ApiDebug) {
                    LOG << "CreateFileW (" << lpFileName << "," << dwDesiredAccess << "," << dwFlagsAndAttributes << ")" << END;
                }
                return XUsbCreateFile(device, dwFlagsAndAttributes);
            }
        }
    }

    return CreateFileW_Real(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static BOOL ImplDoDeviceIoControl(const wchar_t *finalPath, DeviceIntf *device, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                  LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned) {
    int size;
    switch (dwIoControlCode) {
    case IOCTL_HID_GET_COLLECTION_INFORMATION:
        return ProcessDeviceIoControlOutput<HID_COLLECTION_INFORMATION>(
            lpOutBuffer, nOutBufferSize, lpBytesReturned, [device](HID_COLLECTION_INFORMATION *info) {
                ZeroMemory(info, sizeof(HID_COLLECTION_INFORMATION));
                info->DescriptorSize = device->PreparsedSize;
                info->Polled = TRUE;
                info->VendorID = device->VendorId;
                info->ProductID = device->ProductId;
                info->VersionNumber = device->VersionNum;
                return TRUE;
            });

    case IOCTL_HID_GET_COLLECTION_DESCRIPTOR:
        return ProcessDeviceIoControlOutput<uint8_t>(
            lpOutBuffer, nOutBufferSize, lpBytesReturned, [device](uint8_t *dest) {
                CopyMemory(dest, device->Preparsed, device->PreparsedSize);
                return TRUE;
            },
            device->PreparsedSize);

    case IOCTL_HID_GET_PRODUCT_STRING:
        return ProcessDeviceIoControlOutputString(device->ProductString, lpOutBuffer, nOutBufferSize, lpBytesReturned);

    case IOCTL_HID_GET_MANUFACTURER_STRING:
        return ProcessDeviceIoControlOutputString(device->ManufacturerString, lpOutBuffer, nOutBufferSize, lpBytesReturned);

    case IOCTL_HID_GET_SERIALNUMBER_STRING:
        return ProcessDeviceIoControlOutputString(device->SerialString, lpOutBuffer, nOutBufferSize, lpBytesReturned);

    case IOCTL_HID_GET_INDEXED_STRING:
        return ProcessDeviceIoControlInput<ULONG>(
            lpInBuffer, nInBufferSize, [device, lpOutBuffer, nOutBufferSize, lpBytesReturned](ULONG *ptr) {
                auto str = device->GetIndexedString(*ptr);
                if (str) {
                    return ProcessDeviceIoControlOutputString(str, lpOutBuffer, nOutBufferSize, lpBytesReturned);
                }

                SetLastError(ERROR_GEN_FAILURE);
                return FALSE;
            });

    case IOCTL_HID_SET_POLL_FREQUENCY_MSEC:
        return ProcessDeviceIoControlInput<ULONG>(lpInBuffer, nInBufferSize, [device, finalPath](ULONG *ptr) {
            return GPipeThreads.WithPipeThread(device, finalPath, [ptr](ImplProcessPipeThread *thread) {
                thread->SetPollFreq(*ptr);
            });
        });

    case IOCTL_HID_GET_POLL_FREQUENCY_MSEC:
        return ProcessDeviceIoControlOutput<ULONG>(lpOutBuffer, nOutBufferSize, lpBytesReturned, [device, finalPath](ULONG *ptr) {
            return GPipeThreads.WithPipeThread(device, finalPath, [ptr](ImplProcessPipeThread *thread) {
                *ptr = thread->GetPollFreq();
            });
        });

    case IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS:
        return ProcessDeviceIoControlInput<ULONG>(lpInBuffer, nInBufferSize, [device, finalPath](ULONG *ptr) {
            return GPipeThreads.WithPipeThread(device, finalPath, [ptr](ImplProcessPipeThread *thread) {
                thread->SetMaxBuffers(*ptr);
            });
        });

    case IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS:
        return ProcessDeviceIoControlOutput<ULONG>(lpOutBuffer, nOutBufferSize, lpBytesReturned, [device, finalPath](ULONG *ptr) {
            return GPipeThreads.WithPipeThread(device, finalPath, [ptr](ImplProcessPipeThread *thread) {
                *ptr = thread->GetMaxBuffers();
            });
        });

    case IOCTL_HID_FLUSH_QUEUE:
        return GPipeThreads.WithPipeThread(device, finalPath, [](ImplProcessPipeThread *thread) {
            thread->FlushReports();
        });

    case IOCTL_HID_GET_FEATURE:
        if (lpOutBuffer && (size = device->ProcessFeature((const uint8_t *)lpOutBuffer, (uint8_t *)lpOutBuffer, nOutBufferSize)) >= 0) {
            *lpBytesReturned = size;
            return TRUE;
        }

        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;

    case IOCTL_HID_SET_FEATURE:
        if (lpInBuffer && (size = device->ProcessFeature((const uint8_t *)lpInBuffer, nullptr, nInBufferSize)) >= 0) {
            return TRUE;
        }

        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;

    case IOCTL_HID_GET_INPUT_REPORT:
        if (lpOutBuffer && (size = device->CopyInputTo((uint8_t *)lpOutBuffer, nOutBufferSize)) >= 0) {
            *lpBytesReturned = size;
            return TRUE;
        }

        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;

    case IOCTL_HID_SET_OUTPUT_REPORT:
        if (lpInBuffer && device->ProcessOutput((const uint8_t *)lpInBuffer, nInBufferSize)) {
            return TRUE;
        }

        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;

    case IOCTL_GET_PHYSICAL_DESCRIPTOR:
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;

    default:
        LOG << "Unknown device io control: " << dwIoControlCode << END;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
}

BOOL WINAPI DeviceIoControl_Hook(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                 LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped) {
    int deviceType = DEVICE_TYPE_FROM_CTL_CODE(dwIoControlCode);

    if ((deviceType == FILE_DEVICE_KEYBOARD || deviceType == IOCTL_XINPUT_DEVICE_TYPE) &&
        GetFileType(hDevice) == FILE_TYPE_PIPE) {
        Path finalPath;
        DeviceIntf *device = GetDeviceByHandle(hDevice, &finalPath);

        if (device) {
            if (G.ApiDebug) {
                LOG << "DeviceIoControl (" << dwIoControlCode << ", " << (lpOverlapped ? "overlapped" : "normal") << ")" << END;
            }

            DWORD bytesReturnedBuf = 0;
            lpBytesReturned = lpBytesReturned ? lpBytesReturned : &bytesReturnedBuf;
            BOOL result = deviceType == FILE_DEVICE_KEYBOARD ? ImplDoDeviceIoControl(finalPath, device, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned) : XUsbDeviceIoControl(device, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned);

            if (lpOverlapped) {
                lpOverlapped->Internal = result ? 0 : GetLastError();
                lpOverlapped->InternalHigh = *lpBytesReturned;

                if (lpOverlapped->hEvent) {
                    SetEvent(lpOverlapped->hEvent);
                }
            }

            return result;
        }
    }

    return DeviceIoControl_Real(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped);
}

void HookDeviceApi() {
    AddGlobalHook(&CreateFileA_Real, CreateFileA_Hook);
    AddGlobalHook(&CreateFileW_Real, CreateFileW_Hook);
    AddGlobalHook(&DeviceIoControl_Real, DeviceIoControl_Hook);
}
