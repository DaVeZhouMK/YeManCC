#pragma once
#include <windows.h>
#include <objbase.h>
namespace ymcc::mouseMode {
// Balance this thread's COM reference. Existing STA callers remain STA;
// uninitialized IPC workers receive an MTA for the duration of the call.
struct ComApartment {
    const HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComApartment() = default;
    ComApartment(const ComApartment&) = delete;
    ~ComApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
    bool ready() const { return SUCCEEDED(status) || status == RPC_E_CHANGED_MODE; }
};
}
