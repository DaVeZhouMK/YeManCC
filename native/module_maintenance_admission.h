#pragma once
#include <windows.h>
#include <string_view>
namespace ymcc::moduleMaintenance {
#ifndef YMCC_MODULE_MAINTENANCE_MUTEX_NAME
#define YMCC_MODULE_MAINTENANCE_MUTEX_NAME L"Global\\YeManCC.ModuleMaintenance"
#endif
// Advisory preflight only. The actual SDK/file consumer acquires its own lease;
// never retain this mutex while starting a child that must acquire the same gate.
inline bool available() noexcept {
    HANDLE gate=CreateMutexW(nullptr,FALSE,YMCC_MODULE_MAINTENANCE_MUTEX_NAME);
    if(!gate)return false;
    const DWORD result=WaitForSingleObject(gate,0);
    const bool admitted=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;
    if(admitted)ReleaseMutex(gate);
    CloseHandle(gate);return admitted;
}
inline bool transientPrepareRejection(std::string_view status,std::string_view detail) noexcept {
    return status=="prepare-rejected"&&detail=="usbip-version-maintenance-in-progress";
}
}
