#pragma once
#include "msi_key_event_class.h"
#include <wbemidl.h>
#include <wrl/client.h>
#include <utility>
namespace ymcc::msi {
// Read-only class definition lookup. Never deploy MOF/DLLs, change WmiAcpi,
// restart ACPI/PnP, execute WMI methods or subscribe on another machine family.
class WindowsEventClassReader {
    IWbemServices* services_;
    Microsoft::WRL::ComPtr<IWbemCallResult> result_;
public:
    explicit WindowsEventClassReader(IWbemServices* services):services_(services){}
    int32_t begin(){
        if(!services_)return int32_t(E_POINTER);
        BSTR name=SysAllocString(L"MSI_Event");if(!name)return int32_t(E_OUTOFMEMORY);
        const auto hr=services_->GetObject(name,WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,nullptr,result_.GetAddressOf());
        SysFreeString(name);return int32_t(hr);
    }
    std::pair<int32_t,bool> wait(unsigned budgetMs){
        if(!result_)return {int32_t(E_POINTER),false};
        Microsoft::WRL::ComPtr<IWbemClassObject> object;
        const auto hr=result_->GetResultObject(LONG(budgetMs),object.GetAddressOf());
        return {int32_t(hr),object!=nullptr};
    }
};
}
