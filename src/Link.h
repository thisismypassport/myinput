#pragma once
#include "UtilsPath.h"
#include "ComUtils.h"
#include <shobjidl.h>
#include <shlguid.h>

template <class TGetter>
Path GetWithoutTruncate(TGetter getter) {
    DWORD bufferSize = MAX_PATH;
    while (true) {
        Path dest(bufferSize);
        if (!SUCCEEDED(getter(dest, bufferSize))) {
            return nullptr;
        }
        if (wcslen(dest) < bufferSize - 1) {
            return dest;
        }

        bufferSize *= 2;
    }
}

bool ResolveLink(const wchar_t *linkPath, Path *outPath, Path *outArgs = nullptr, Path *outWorkDir = nullptr) {
    ComRef<IShellLinkW> link;
    ComRef<IPersistFile> persist;
    if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)) &&
        SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID *)&link)) &&
        SUCCEEDED(link->QueryInterface(IID_IPersistFile, (LPVOID *)&persist)) &&
        SUCCEEDED(persist->Load(linkPath, STGM_READ)) &&
        SUCCEEDED(link->Resolve(nullptr, SLR_NO_UI))) {
        *outPath = GetWithoutTruncate([&](wchar_t *buf, DWORD bufSize) {
            return link->GetPath(buf, bufSize, nullptr, 0);
        });
        if (outArgs) {
            *outArgs = GetWithoutTruncate([&](wchar_t *buf, DWORD bufSize) {
                return link->GetArguments(buf, bufSize);
            });
        }
        if (outWorkDir) {
            *outWorkDir = GetWithoutTruncate([&](wchar_t *buf, DWORD bufSize) {
                return link->GetWorkingDirectory(buf, bufSize);
            });
        }
        return true;
    }
    return false;
}
