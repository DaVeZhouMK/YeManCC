#pragma once
#include <windows.h>
#include <winrt/Windows.Foundation.h>
namespace ymcc::sensors {
// .get()/wait_for cannot be used on this STA. Poll the documented operation
// status with a bounded message pump and call GetResults only after completion.
template<class Operation,class Cancel>
auto getStaResults(Operation operation,ULONGLONG deadline,Cancel cancelled) -> decltype(operation.GetResults()) {
    using winrt::Windows::Foundation::AsyncStatus;
    while(operation.Status()==AsyncStatus::Started) {
        if(cancelled()){operation.Cancel();throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));}
        if(GetTickCount64()>=deadline){operation.Cancel();throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));}
        const DWORD remaining=static_cast<DWORD>(deadline-GetTickCount64());
        MsgWaitForMultipleObjectsEx(0,nullptr,(remaining<10?remaining:10),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        for(MSG message{};PeekMessageW(&message,nullptr,0,0,PM_REMOVE);){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(cancelled())throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    return operation.GetResults();
}
} // namespace ymcc::sensors
