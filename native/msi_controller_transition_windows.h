#pragma once
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <string>
#include <vector>
#include <json.hpp>
// Diagnostic metadata only. No Acquire/Poll/GetState, HID writes, device
// restart, HidHide command, power request or application exemption mutation.
namespace ymcc::msi::trace {
inline std::string utf8(const std::wstring& s){
    if(s.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};std::string out(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
inline std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c=char(c+'a'-'A');return s;}
struct InfoSetOwner {HDEVINFO handle;~InfoSetOwner(){if(handle!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(handle);}};
struct MetadataHandleOwner {HANDLE handle;~MetadataHandleOwner(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
inline nlohmann::json physicalNamespace(){
    using nlohmann::json;const auto begin=GetTickCount64();json items=json::array();bool complete=false;std::string limit;DWORD enumError=0;
    GUID hid{};HidD_GetHidGuid(&hid);const auto set=SetupDiGetClassDevsW(&hid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return {{"known",false},{"complete",false},{"error",GetLastError()},{"interfaces",items}};
    InfoSetOwner ownSet{set};
    for(DWORD i=0;i<128;++i){
        if(GetTickCount64()-begin>150){limit="soft-time-budget-between-metadata-calls";break;}
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&hid,i,&iface)){enumError=GetLastError();complete=enumError==ERROR_NO_MORE_ITEMS;break;}
        DWORD size=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&size,nullptr);if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||size>65536)continue;
        std::vector<BYTE> bytes(size);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data());detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA dev{};dev.cbSize=sizeof(dev);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,size,nullptr,&dev))continue;
        const auto path=utf8(detail->DevicePath),key=lower(path);
        if(!key.starts_with("\\\\?\\hid#vid_0db0&pid_1901")&&!key.starts_with("\\\\?\\hid#vid_0db0&pid_1902"))continue;
        json item={{"interfacePath",path},{"pnpInterfaceEnumerated",true},{"pathVidPidMeaning","candidate filter only"}};
        wchar_t id[512]{};const bool idOk=SetupDiGetDeviceInstanceIdW(set,&dev,id,512,nullptr)!=FALSE;item["instanceKnown"]=idOk;if(idOk)item["instanceId"]=utf8(id);
        ULONG status=0,problem=0;const auto statusRc=CM_Get_DevNode_Status(&status,&problem,dev.DevInst,0);item["pnpStatusKnown"]=statusRc==CR_SUCCESS;
        if(statusRc==CR_SUCCESS){item["pnpStatus"]=status;item["pnpProblem"]=problem;item["pnpStarted"]=(status&DN_STARTED)!=0;}
        DEVINST node=dev.DevInst;bool usbKnown=false;
        for(unsigned depth=0;depth<8;++depth){wchar_t parentId[512]{};if(CM_Get_Device_IDW(node,parentId,512,0)!=CR_SUCCESS)break;const auto parent=utf8(parentId),fold=lower(parent);
            if(fold.starts_with("usb\\")&&fold.find("&mi_")==std::string::npos){item["usbRootInstance"]=parent;usbKnown=true;break;}
            DEVINST p=0;if(CM_Get_Parent(&p,node,0)!=CR_SUCCESS)break;node=p;}
        item["usbRootKnown"]=usbKnown;
        HANDLE handle=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        MetadataHandleOwner ownHandle{handle};
        item["metadataHandleOpened"]=handle!=INVALID_HANDLE_VALUE;
        if(handle==INVALID_HANDLE_VALUE)item["metadataOpenError"]=GetLastError();else{
            HIDD_ATTRIBUTES attr{};attr.Size=sizeof(attr);const bool attrOk=HidD_GetAttributes(handle,&attr)!=FALSE;item["hidAttributesKnown"]=attrOk;
            if(attrOk){item["vendorId"]=attr.VendorID;item["productId"]=attr.ProductID;}
            PHIDP_PREPARSED_DATA prep=nullptr;HIDP_CAPS caps{};const bool capsOk=HidD_GetPreparsedData(handle,&prep)!=FALSE&&HidP_GetCaps(prep,&caps)==HIDP_STATUS_SUCCESS;
            if(prep)HidD_FreePreparsedData(prep);item["hidCapabilitiesKnown"]=capsOk;if(capsOk){item["usagePage"]=caps.UsagePage;item["usage"]=caps.Usage;}
            wchar_t name[128]{};const bool productOk=HidD_GetProductString(handle,name,sizeof(name))!=FALSE;item["productKnown"]=productOk;
            if(productOk){std::wstring product;json units=json::array();for(size_t k=0;k<128&&name[k];++k){product.push_back(name[k]);units.push_back(unsigned(name[k]));}item["product"]=utf8(product);item["productUtf16"]=units;}
        }
        items.push_back(std::move(item));if(items.size()==16){limit="candidate-count-limit";break;}
        if(i==127)limit="interface-scan-limit";
    }
    return {{"known",true},{"complete",complete},{"limit",limit},{"enumerationError",enumError},
        {"beginTick",begin},{"endTick",GetTickCount64()},{"elapsedMs",GetTickCount64()-begin},{"budgetMeaning","soft budget; in-flight metadata API is not cancelled"},
        {"scope","PnP and HID metadata only; not active-mode or external input-delivery proof"},{"interfaces",items}};
}
}
