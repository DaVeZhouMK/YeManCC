#pragma once
#include "background_controller_ui.h"
#include <algorithm>
#include <hidsdi.h>
#include <hidpi.h>
#include <cfgmgr32.h>
// The native owner provides devpkey.h (INITGUID deliberately makes that
// Windows header unsafe to include twice in the same translation unit).

// Included beside gamepadEval after the original physical source reader.
// All objects below belong to the existing UI thread. No polling thread,
// registration owner, device writer or virtual-output publisher is added.
static void gamepadEval();
namespace ymccBackgroundUi {
using ymcc::backgroundui::Epoch;
using ymcc::backgroundui::Descriptor;
static ymcc::backgroundui::Lane lane;
static std::uint64_t sequence=0;
struct Device {
    bool ready=false;
    ULONGLONG retryAt=0;
    std::vector<BYTE> preparsed;
    ULONG maxData=0;
};
static Epoch cacheEpoch;
static ULONGLONG epochStarted=0;
static ULONGLONG registrationFloor=0;
static std::unordered_map<HANDLE,Device> devices;
static HANDLE boundDevice=nullptr;

static bool context(Epoch& e,std::wstring* path=nullptr) {
    if(g_exitRequested.load(std::memory_order_acquire) ||
       g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
       !g_padConnected || (g_inputCaptureGamepadSnapshot.inputSource!="xinput" &&
        !(g_inputCaptureGamepadSnapshot.inputSource=="xinput-hid" && g_inputCaptureGamepadSnapshot.commonHidReportSequence)) ||
       !g_inputCaptureGamepadSnapshot.readContextStable)return false;
    const DWORD slot=g_xinputPrimarySlot;
    if(slot>=4 || !(g_xinputConnectedMask&(1u<<slot)) ||
       (g_xinputVirtualEndpointMask&(1u<<slot)) || (g_xinputSlotUnknownMask&(1u<<slot)) ||
       (g_xinputSlotReprobeMask.load(std::memory_order_acquire)&(1u<<slot)) ||
       (g_xinputPhysicalSourceLocked && !(g_xinputPhysicalSourceMask&(1u<<slot))))return false;
    e={currentPowerGeneration(),g_xinputTopologyEpoch.load(std::memory_order_acquire),
        g_xinputSlotIdentityEpoch.load(std::memory_order_acquire),0,slot};
    if(g_inputCaptureGamepadSnapshot.readPowerGeneration!=e.power ||
       g_inputCaptureGamepadSnapshot.readTopologyGeneration!=e.topology ||
       g_inputCaptureGamepadSnapshot.readIdentityGeneration!=e.identity)return false;
    if(g_xinputSlotIdentityResolvedEpoch[slot]!=e.identity || !g_axisCalibrationXinputKeys[slot])return false;
    const auto proof=g_xinputFeedbackIdentity[slot].capture(e.identity,e.power);
    if(!proof.valid || proof.legacyFallback)return false;
    e.episode=proof.episode;
    if(path)*path=U2W(*g_axisCalibrationXinputKeys[slot]);
    return e.power==currentPowerGeneration() && e.identity==g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) &&
        e.topology==g_xinputTopologyEpoch.load(std::memory_order_acquire);
}
static bool propertyString(DEVINST node,const DEVPROPKEY& key,std::wstring& out) {
    std::array<wchar_t,512> text{};DEVPROPTYPE type=0;ULONG bytes=sizeof(text);
    if(CM_Get_DevNode_PropertyW(node,&key,&type,reinterpret_cast<PBYTE>(text.data()),&bytes,0)!=CR_SUCCESS ||
       type!=DEVPROP_TYPE_STRING || bytes<sizeof(wchar_t) || bytes>sizeof(text) ||
       bytes%sizeof(wchar_t) || text[bytes/sizeof(wchar_t)-1]!=L'\0')return false;
    out.assign(text.data());return !out.empty();
}
static bool interfaceNode(const std::wstring& path,DEVINST& node,std::wstring& id) {
    if(path.empty())return false;
    std::array<wchar_t,512> text{};DEVPROPTYPE type=0;ULONG bytes=sizeof(text);
    if(CM_Get_Device_Interface_PropertyW(path.c_str(),&DEVPKEY_Device_InstanceId,&type,
       reinterpret_cast<PBYTE>(text.data()),&bytes,0)!=CR_SUCCESS || type!=DEVPROP_TYPE_STRING ||
       bytes<sizeof(wchar_t) || bytes>sizeof(text) || bytes%sizeof(wchar_t) ||
       text[bytes/sizeof(wchar_t)-1]!=L'\0')return false;
    id.assign(text.data());return !id.empty() && CM_Locate_DevNodeW(&node,text.data(),CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS;
}
static bool sameXusbSource(const std::wstring& rawPath,const std::wstring& slotPath) {
    DEVINST source=0,node=0;std::wstring sourceId,rawId,service;
    if(!interfaceNode(slotPath,source,sourceId) || sourceId.size()<8 ||
       _wcsnicmp(sourceId.c_str(),L"USB\\VID_",8)!=0 ||
       !propertyString(source,DEVPKEY_Device_Service,service) || _wcsicmp(service.c_str(),L"xusb22")!=0 ||
       !interfaceNode(rawPath,node,rawId))return false;
    // Match the exact XUSB leaf in this HID's own ancestry. Shared container
    // sentinels, hubs, VID/PID matches and arbitrary common parents are not proof.
    for(unsigned depth=0;depth<8;++depth) {
        std::array<wchar_t,512> id{};
        if(CM_Get_Device_IDW(node,id.data(),static_cast<ULONG>(id.size()),0)!=CR_SUCCESS)return false;
        if(_wcsicmp(id.data(),sourceId.c_str())==0)return true;
        DEVINST parent=0;
        if(CM_Get_Parent(&parent,node,0)!=CR_SUCCESS || parent==node)return false;
        node=parent;
    }
    return false;
}
static Device inspect(HANDLE handle,const std::wstring& slotPath) {
    Device d;RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT bytes=sizeof(info);
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICEINFO,&info,&bytes)==~0u || info.dwType!=RIM_TYPEHID ||
       info.hid.dwVendorId!=0x045e || info.hid.dwProductId!=0x028e ||
       info.hid.usUsagePage!=1 || info.hid.usUsage!=5)return d;
    UINT chars=0;
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,nullptr,&chars)==~0u || !chars || chars>32768)return d;
    std::vector<wchar_t> path(static_cast<std::size_t>(chars)+1,L'\0');
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,path.data(),&chars)==~0u ||
       !sameXusbSource(path.data(),slotPath))return d;
    bytes=0;
    if(GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,nullptr,&bytes)==~0u || !bytes || bytes>65536)return d;
    d.preparsed.resize(bytes);
    if(GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,d.preparsed.data(),&bytes)==~0u)return Device{};
    const auto prep=reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsed.data());
    HIDP_CAPS caps{};
    if(HidP_GetCaps(prep,&caps)!=HIDP_STATUS_SUCCESS || caps.NumberInputButtonCaps!=1 || caps.NumberInputValueCaps!=6)return Device{};
    HIDP_BUTTON_CAPS bc{};USHORT count=1;
    if(HidP_GetButtonCaps(HidP_Input,&bc,&count,prep)!=HIDP_STATUS_SUCCESS || count!=1 ||
       !bc.IsRange || bc.UsagePage!=9)return Device{};
    Descriptor desc;
    desc.vendor=info.hid.dwVendorId;desc.product=info.hid.dwProductId;desc.page=caps.UsagePage;desc.usage=caps.Usage;
    desc.reportBytes=caps.InputReportByteLength;desc.dataIndices=caps.NumberInputDataIndices;
    desc.buttonCaps=count;desc.buttonFirst=bc.Range.UsageMin;desc.buttonLast=bc.Range.UsageMax;
    desc.buttonIndex=bc.Range.DataIndexMin;desc.buttonEndIndex=bc.Range.DataIndexMax;
    desc.buttonReport=bc.ReportID;desc.buttonLink=bc.LinkCollection;
    std::array<HIDP_VALUE_CAPS,6> vc{};count=static_cast<USHORT>(vc.size());
    if(HidP_GetValueCaps(HidP_Input,vc.data(),&count,prep)!=HIDP_STATUS_SUCCESS || count!=6)return Device{};
    desc.valueCount=count;
    for(unsigned i=0;i<count;++i) {
        const auto& v=vc[i];
        if(v.IsRange || v.UsagePage!=1 || v.ReportCount!=1)return Device{};
        desc.values[i]={v.NotRange.DataIndex,v.NotRange.Usage,v.LinkCollection,v.BitSize,v.ReportID,v.LogicalMin,v.LogicalMax};
    }
    if(!ymcc::backgroundui::qualified(desc))return Device{};
    d.maxData=HidP_MaxDataListLength(HidP_Input,prep);
    d.ready=d.maxData>=16 && d.maxData<=64;
    return d;
}
static bool fseActive() {
    // Shared by the existing UI lane and the common BUS data reader.
    // This lock is independent of g_gamepadReadMx and never acquires it.
    static std::mutex cacheMutex;
    std::lock_guard cacheLock(cacheMutex);
    static ULONGLONG last=0;static bool active=false;
    const auto now=GetTickCount64();
    if(!last || now<last || now-last>=1000) {
        const auto state=ymcc::bootmode::readPlatform();active=state.activeKnown && state.active;last=now;
    }
    return active;
}
static bool eligible() {return gameOverlayEnabled() && (g_gameOverlay.active() || fseActive());}
static void invalidate() {lane.clear();devices.clear();boundDevice=nullptr;cacheEpoch={};epochStarted=0;registrationFloor=GetTickCount64();}
} // namespace ymccBackgroundUi

// Scoped UI view: the original reader, source receipt and BUS snapshot remain
// untouched. Restore before releasing the original sampling mutex, including
// early returns in recording, isolation, custom-library and summon handling.
class BackgroundControllerUiScope {
    XINPUT_GAMEPAD originalPad_=g_curPad;
    WORD originalButtons_=g_curW;
public:
    explicit BackgroundControllerUiScope(bool allow=true) {
        using namespace ymccBackgroundUi;
        Epoch e;XINPUT_GAMEPAD ui{};
        const bool current=allow && lane.hasSample() && context(e);
        const bool routeAllowed=current && eligible();
        const bool used=lane.apply(e,routeAllowed,g_curPad,ui);
        if(used){g_curPad=inputCanonicalizeGamepad(ui);g_curW=g_curPad.wButtons;}
        static bool previous=false;static WORD previousButtons=0;
        if(previous!=used || (used && previousButtons!=g_curW)) {
            appendNativeLifecycleLog("gamepad-background-ui",{{"source",used?"xusb-hid-ui":"original"},
                {"active",used},{"slot",e.slot},{"buttons",used?g_curW:0},{"rawSequence",sequence},
                {"power",e.power},{"topology",e.topology},{"identity",e.identity},
                {"triggerSource","original-xinput"},{"busChanged",false}});
        }
        if(previous && !used) {
            // Reuse the existing neutral handoff guards when the source lane
            // retires; a delayed native press cannot become a second action.
            g_inputReleaseRequired.store(true,std::memory_order_release);
            g_gamepadAxisNeutralAdmissionRequired=true;
        }
        previous=used;previousButtons=g_curW;
    }
    ~BackgroundControllerUiScope(){g_curPad=originalPad_;g_curW=originalButtons_;}
    BackgroundControllerUiScope(const BackgroundControllerUiScope&)=delete;
    BackgroundControllerUiScope& operator=(const BackgroundControllerUiScope&)=delete;
};

static bool backgroundControllerUiCapture(RAWINPUT* raw,UINT packetBytes) {
    using namespace ymccBackgroundUi;
    if(!raw || raw->header.dwType!=RIM_TYPEHID)return false;
    constexpr auto offset=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
    if(packetBytes<offset || raw->header.dwSize!=packetBytes || raw->data.hid.dwSizeHid!=15 ||
       !raw->data.hid.dwCount || raw->data.hid.dwCount>256 ||
       raw->data.hid.dwCount>(packetBytes-offset)/15) {
        if(raw->header.hDevice==boundDevice && boundDevice)lane.clear();
        return false;
    }
    const auto now=GetTickCount64();
    const DWORD queueAge=static_cast<DWORD>(now)-static_cast<DWORD>(GetMessageTime());
    Epoch e;
    {std::lock_guard<std::mutex> lock(g_gamepadReadMx);if(!context(e)){lane.clear();return false;}}
    if(!(cacheEpoch==e)) {
        const bool replaced=cacheEpoch.power!=0;
        lane.clear();devices.clear();boundDevice=nullptr;cacheEpoch=e;
        epochStarted=replaced?now:registrationFloor;
    }
    if(boundDevice && raw->header.hDevice!=boundDevice)return false;
    auto it=devices.find(raw->header.hDevice);
    if(it==devices.end() || (!it->second.ready && now>=it->second.retryAt)) {
        if(it==devices.end() && devices.size()>=8) {
            const auto unused=std::find_if(devices.begin(),devices.end(),[](const auto& item){return !item.second.ready;});
            if(unused==devices.end())return false;
            devices.erase(unused);
        }
        std::wstring slotPath;Epoch current;
        {std::lock_guard<std::mutex> lock(g_gamepadReadMx);
            if(!context(current,&slotPath) || !(current==e))return false;}
        Device candidate=inspect(raw->header.hDevice,slotPath);
        candidate.retryAt=now+2000; // Existing reports drive bounded recovery after transient PnP/HID failure.
        it=devices.insert_or_assign(raw->header.hDevice,std::move(candidate)).first;
        if(it->second.ready)appendNativeLifecycleLog("gamepad-background-ui-device",{
            {"source","xusb-hid-ui"},{"qualified",true},{"slot",e.slot},
            {"slotPath",W2U(slotPath)},{"binding","exact-xusb-leaf-in-hid-ancestry"},
            {"descriptor","win5-xusb-15-byte-report"},{"busChanged",false}});
    }
    auto& device=it->second;if(!device.ready)return false;
    if(queueAge>250 || now<epochStarted || queueAge>now-epochStarted){lane.clear();return false;}
    if(!eligible()){lane.clear();return false;}
    std::array<HIDP_DATA,64> data{};std::array<ymcc::backgroundui::Datum,64> decoded{};
    std::array<XINPUT_GAMEPAD,256> pads{};std::array<bool,256> validReports{};
    const DWORD reports=raw->data.hid.dwCount;
    for(DWORD report=0;report<reports;++report) {
        ULONG count=device.maxData;
        auto bytes=reinterpret_cast<PCHAR>(raw->data.hid.bRawData+report*15);
        XINPUT_GAMEPAD pad{};
        bool valid=HidP_GetData(HidP_Input,data.data(),&count,
            reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsed.data()),bytes,15)==HIDP_STATUS_SUCCESS && count<=64;
        if(valid)for(ULONG i=0;i<count;++i)decoded[i]={data[i].DataIndex,data[i].RawValue,data[i].On!=FALSE};
        valid=valid && ymcc::backgroundui::decode({decoded.data(),count},pad);
        pads[report]=pad;validReports[report]=valid;
    }
    // No reference into the device cache survives a semantic callback.
    for(DWORD report=0;report<reports;++report) {
        Epoch current;
        bool same=false;
        {std::lock_guard<std::mutex> lock(g_gamepadReadMx);same=context(current) && current==e;}
        if(same && validReports[report]) {
            if(!boundDevice)boundDevice=raw->header.hDevice;
            if(boundDevice==raw->header.hDevice){lane.observe(e,pads[report]);++sequence;}
            else lane.clear();
        }else lane.clear();
        // Preserve every report edge, including a press and release bundled
        // into the same WM_INPUT. This is the existing semantic evaluator.
        gamepadEval();
    }
    return true;
}
