#pragma once
#include "msi_external_raw_receipts.h"
#include "msi_external_gamepad_decode_windows.h"
#include <cstddef>
// This header is included ONLY by the independent observer executable/test.
// No calls to CreateFile, ReadFile, WriteFile, HID feature/output or SendInput.
namespace ymcc::msi::external {
inline bool registerRawReceipts(HWND target,bool remove,DWORD& error) {
    const DWORD flags=remove?RIDEV_REMOVE:RIDEV_INPUTSINK|RIDEV_DEVNOTIFY;
    const HWND window=remove?nullptr:target;
    RAWINPUTDEVICE devices[]={{1,4,flags,window},{1,5,flags,window},{0xfff0,0x40,flags,window},{0xffa0,1,flags,window},{12,1,flags,window}};
    const bool ok=RegisterRawInputDevices(devices,static_cast<UINT>(std::size(devices)),sizeof(RAWINPUTDEVICE))!=FALSE;
    error=ok?ERROR_SUCCESS:GetLastError();return ok;
}
inline RawIdentity queryRawIdentity(HANDLE device) {
    RawIdentity result;RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT bytes=sizeof(info);
    if(!device||GetRawInputDeviceInfoW(device,RIDI_DEVICEINFO,&info,&bytes)==UINT(-1)||info.dwType!=RIM_TYPEHID)return result;
    result.hid=true;result.vid=info.hid.dwVendorId;result.pid=info.hid.dwProductId;result.usagePage=info.hid.usUsagePage;result.usage=info.hid.usUsage;
    // Do not request paths for unrelated devices, much less capture their data.
    if(result.vid!=0x0db0||(result.pid!=0x1901&&result.pid!=0x1902))return result;
    UINT chars=0;if(GetRawInputDeviceInfoW(device,RIDI_DEVICENAME,nullptr,&chars)==UINT(-1)||!chars||chars>4096)return result;
    std::vector<wchar_t> path(chars+1,0);UINT capacity=static_cast<UINT>(path.size());
    const UINT copied=GetRawInputDeviceInfoW(device,RIDI_DEVICENAME,path.data(),&capacity);
    if(copied==UINT(-1)||copied>=path.size())return result;
    path.back()=0;const std::wstring text(path.data());
    const int utf8Bytes=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(utf8Bytes<=0)return result;result.path.resize(utf8Bytes);
    if(WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.path.data(),utf8Bytes,nullptr,nullptr)!=utf8Bytes){result.path.clear();return result;}
    result.known=true;return result;
}
inline void observeRawReceipt(HRAWINPUT input,RawReceiptCounters& counters,uint64_t now,QueueTime queue={}) {
    RAWINPUTHEADER header{};UINT bytes=sizeof(header);
    if(GetRawInputData(input,RID_HEADER,&header,&bytes,sizeof(RAWINPUTHEADER))!=sizeof(header)){counters.error();return;}
    if(header.dwType!=RIM_TYPEHID)return;
    const auto identity=queryRawIdentity(header.hDevice);
    if(std::string(rawRole(identity))=="unqualified"){
        // Count a non-authorizing receipt without decoding its payload.
        const unsigned char none=0;counters.observe(reinterpret_cast<uintptr_t>(header.hDevice),identity,&none,1,1,1,now,queue);return;
    }
    bytes=0;
    if(GetRawInputData(input,RID_INPUT,nullptr,&bytes,sizeof(RAWINPUTHEADER))==UINT(-1)){counters.error();return;}
    constexpr size_t payloadOffset=offsetof(RAWINPUT,data.hid.bRawData);
    if(bytes<payloadOffset||bytes>payloadOffset+65536){counters.invalid();return;}
    std::vector<BYTE> buffer(bytes);UINT capacity=bytes;
    const UINT copied=GetRawInputData(input,RID_INPUT,buffer.data(),&capacity,sizeof(RAWINPUTHEADER));
    if(copied==UINT(-1)||copied!=bytes||capacity!=bytes){counters.error();return;}
    const auto* raw=reinterpret_cast<const RAWINPUT*>(buffer.data());
    if(raw->header.dwType!=RIM_TYPEHID||raw->header.hDevice!=header.hDevice||raw->header.dwSize!=bytes){counters.invalid();return;}
    counters.observeWithDecoder(reinterpret_cast<uintptr_t>(header.hDevice),identity,raw->data.hid.bRawData,
        raw->data.hid.dwSizeHid,raw->data.hid.dwCount,buffer.size()-payloadOffset,now,queue,
        [&](const unsigned char* report,unsigned size){return decodeGamepadReceipt(header.hDevice,identity,report,size);});
}
}
