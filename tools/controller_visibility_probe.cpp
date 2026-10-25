// Independent read-only controller observer. Enumerating an object is NOT
// treated as input delivery. No HID writes, rumble, injection, device restart,
// service control, settings, application exemptions or power requests.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include "../native/process_efficiency.h"
#include <dinput.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <json.hpp>
#include "../native/msi_controller_visibility_diagnostics.h"
#include "../native/msi_external_raw_receipts_windows.h"
#include "../native/msi_usb_name_origin_windows.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <vector>
#include <array>
#include <thread>
#include <stdexcept>
using nlohmann::json;
namespace {
std::ofstream out;HWND window=nullptr;unsigned inventoryId=0;
ymcc::msi::external::ControlComparisons comparisons;
ymcc::msi::external::RawReceiptCounters rawCounters;
bool rawReceiptsActive=false;
ymcc::msi::external::QueueTime dispatchedQueueTime;
std::string u8(const std::wstring& text){int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);std::string s(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),s.data(),n,nullptr,nullptr);return s;}
std::string guid(const GUID& value){wchar_t s[64]{};StringFromGUID2(value,s,64);return u8(s);}
void emit(json row){FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER t{};t.LowPart=ft.dwLowDateTime;t.HighPart=ft.dwHighDateTime;row["utcEpochMs"]=(t.QuadPart-116444736000000000ull)/10000;row["tick"]=GetTickCount64();row["observerPid"]=GetCurrentProcessId();row["inventoryId"]=inventoryId;out<<row.dump()<<"\n";out.flush();}
void noteComparison(json& row,const std::string& key,const json& controls){const auto c=comparisons.observe(key,controls,inventoryId,GetTickCount64());row["decodedContentChanged"]=c.changed;row["havePriorReading"]=c.havePrior;row["comparisonSeries"]=c.series;row["previousReadingInventoryId"]=c.priorInventory;row["comparisonGapMs"]=c.gapMs;row["comparisonScope"]="recorded controls only; consecutive inventory API snapshots within 3000ms; not hardware sample freshness";}
LRESULT CALLBACK probeWindowProc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
    if(rawReceiptsActive&&message==WM_INPUT)ymcc::msi::external::observeRawReceipt(reinterpret_cast<HRAWINPUT>(lparam),rawCounters,GetTickCount64(),dispatchedQueueTime);
    if(rawReceiptsActive&&message==WM_INPUT_DEVICE_CHANGE)rawCounters.removed(reinterpret_cast<uintptr_t>(reinterpret_cast<HANDLE>(lparam)));
    return DefWindowProcW(hwnd,message,wparam,lparam);
}
// Metadata and live PnP ancestry only. A PnP interface inventory is NOT a
// successful controller open, nor a claim that a hidden interface is visible.
void interfaceMetadata(json& row,const wchar_t* path){
    HANDLE h=CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    row["freshMetadataHandleOpen"]=h!=INVALID_HANDLE_VALUE;row["freshMetadataHandleError"]=h==INVALID_HANDLE_VALUE?GetLastError():0;
    if(h!=INVALID_HANDLE_VALUE){
        HIDD_ATTRIBUTES attr{};attr.Size=sizeof(attr);const bool attrOk=HidD_GetAttributes(h,&attr)!=FALSE;row["hidAttributesKnown"]=attrOk;
        if(attrOk){row["hidVid"]=attr.VendorID;row["hidPid"]=attr.ProductID;}
        PHIDP_PREPARSED_DATA data=nullptr;HIDP_CAPS caps{};const bool capsOk=HidD_GetPreparsedData(h,&data)!=FALSE && HidP_GetCaps(data,&caps)==HIDP_STATUS_SUCCESS;
        if(data)HidD_FreePreparsedData(data);row["hidCapabilitiesKnown"]=capsOk;if(capsOk){row["hidUsagePage"]=caps.UsagePage;row["hidUsage"]=caps.Usage;}
        wchar_t name[128]{};const bool nameOk=HidD_GetProductString(h,name,sizeof(name))!=FALSE;row["hidProductStringKnown"]=nameOk;
        if(nameOk){row["hidProductString"]=u8(name);json units=json::array();for(size_t i=0;i<128&&name[i];++i)units.push_back(unsigned(name[i]));row["hidProductUtf16"]=units;}
        CloseHandle(h);
    }
    wchar_t id[512]{};DEVPROPTYPE type=0;ULONG size=sizeof(id);const auto instanceRc=CM_Get_Device_Interface_PropertyW(path,&DEVPKEY_Device_InstanceId,&type,reinterpret_cast<PBYTE>(id),&size,0);
    row["pnpInstanceConfigRet"]=instanceRc;if(instanceRc!=CR_SUCCESS || type!=DEVPROP_TYPE_STRING || !id[0])return;
    row["pnpInstanceId"]=u8(id);DEVINST node=0;const auto locateRc=CM_Locate_DevNodeW(&node,id,CM_LOCATE_DEVNODE_NORMAL);row["pnpLocateConfigRet"]=locateRc;if(locateRc!=CR_SUCCESS)return;
    ULONG status=0,problem=0;const auto statusRc=CM_Get_DevNode_Status(&status,&problem,node,0);row["pnpStatusConfigRet"]=statusRc;
    if(statusRc==CR_SUCCESS){row["pnpStatus"]=status;row["pnpProblem"]=problem;row["pnpStarted"]=(status&DN_STARTED)!=0;}
    for(unsigned depth=0;depth<8;++depth){wchar_t parentId[512]{};if(CM_Get_Device_IDW(node,parentId,512,0)!=CR_SUCCESS)break;
        const auto text=u8(parentId);if(text.starts_with("USB\\")&&text.find("&MI_")==std::string::npos){row["usbRootInstance"]=text;return;}
        DEVINST parent=0;if(CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)break;node=parent;
    }
    row["usbRootInstanceKnown"]=false;
}
bool mainReadProbeEligible(const json& row){
    if(!row.value("hidAttributesKnown",false)||!row.value("hidCapabilitiesKnown",false)||
       row.value("hidVid",0)!=0x0db0||row.value("hidPid",0)!=0x1902||row.value("hidUsagePage",0)!=1||
       (row.value("hidUsage",0)!=4&&row.value("hidUsage",0)!=5))return false;
    auto p=row.value("interfacePath",std::string{});for(auto& c:p)if(c>='A'&&c<='Z')c=char(c+'a'-'A');
    return p.starts_with("\\\\?\\hid#vid_0db0&pid_1902&mi_00&col01#");
}
void mainReadAccess(json& row,const wchar_t* path){
    const bool eligible=mainReadProbeEligible(row);row["freshMainHidReadProbeAttempted"]=eligible;
    if(!eligible)return;
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    const DWORD error=h==INVALID_HANDLE_VALUE?GetLastError():0;
    row["freshMainHidReadHandleOpen"]=h!=INVALID_HANDLE_VALUE;row["freshMainHidReadHandleError"]=error;
    if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    row["freshMainHidReadScope"]="new read-access handle only, immediately closed; no ReadFile/Acquire/output; not Steam input or filter-attachment proof";
}
void msiPnp(){
    GUID hid{};HidD_GetHidGuid(&hid);HDEVINFO set=SetupDiGetClassDevsW(&hid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE){emit({{"event","msi-pnp-inventory"},{"ok",false},{"error",GetLastError()}});return;}
    unsigned count=0;for(DWORD index=0;index<512;++index){SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);if(!SetupDiEnumDeviceInterfaces(set,nullptr,&hid,index,&iface))break;
        DWORD size=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&size,nullptr);if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||size>65536)continue;
        std::vector<BYTE> bytes(size);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data());detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,size,nullptr,nullptr))continue;
        auto path=u8(detail->DevicePath),lower=path;for(auto& c:lower)if(c>='A'&&c<='Z')c=char(c+'a'-'A');
        if(!lower.starts_with("\\\\?\\hid#vid_0db0&pid_1901&") && !lower.starts_with("\\\\?\\hid#vid_0db0&pid_1902&"))continue;
        json row={{"event","msi-pnp-interface"},{"interfacePath",path},{"enumeratedPnpInterface",true},{"scope","PnP metadata plus qualified MSI main fresh read-access handle probe; not external input delivery/Steam suppression proof"}};
        interfaceMetadata(row,detail->DevicePath);mainReadAccess(row,detail->DevicePath);emit(row);++count;
    }
    SetupDiDestroyDeviceInfoList(set);emit({{"event","msi-pnp-inventory"},{"count",count},{"ok",true},{"limit",512}});
}
struct EnumContext {IDirectInput8W* di=nullptr;unsigned count=0;};
BOOL CALLBACK enumerate(const DIDEVICEINSTANCEW* instance,void* param){auto& ctx=*static_cast<EnumContext*>(param);if(++ctx.count>128)return DIENUM_STOP;
    json row={{"event","directinput-device"},{"instanceGuid",guid(instance->guidInstance)},{"name",u8(instance->tszProductName)},{"deviceType",instance->dwDevType},{"enumerated",true},{"interfaceMapping","unknown"}};
    json nameUnits=json::array();for(size_t i=0;i<MAX_PATH&&instance->tszProductName[i];++i)nameUnits.push_back(unsigned(instance->tszProductName[i]));row["productUtf16"]=nameUnits;
    IDirectInputDevice8W* device=nullptr;HRESULT hr=ctx.di->CreateDevice(instance->guidInstance,&device,nullptr);row["createHresult"]=int64_t(hr);
    if(SUCCEEDED(hr)&&device){DIPROPGUIDANDPATH p{};p.diph.dwSize=sizeof(p);p.diph.dwHeaderSize=sizeof(p.diph);p.diph.dwHow=DIPH_DEVICE;auto pathHr=device->GetProperty(DIPROP_GUIDANDPATH,&p.diph);row["pathHresult"]=int64_t(pathHr);
        if(SUCCEEDED(pathHr)){row["interfacePath"]=u8(p.wszPath);row["interfaceMapping"]="directinput-guidandpath";interfaceMetadata(row,p.wszPath);HANDLE h=CreateFileW(p.wszPath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);row["freshHidReadHandleOpen"]=h!=INVALID_HANDLE_VALUE;row["freshHidReadHandleError"]=h==INVALID_HANDLE_VALUE?GetLastError():0;if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
        DIPROPDWORD id{};id.diph.dwSize=sizeof(id);id.diph.dwHeaderSize=sizeof(id.diph);id.diph.dwHow=DIPH_DEVICE;auto idHr=device->GetProperty(DIPROP_VIDPID,&id.diph);row["vidPidHresult"]=int64_t(idHr);if(SUCCEEDED(idHr)){row["vid"]=LOWORD(id.dwData);row["pid"]=HIWORD(id.dwData);}
        DIDEVCAPS caps{};caps.dwSize=sizeof(caps);auto capsHr=device->GetCapabilities(&caps);row["capabilitiesHresult"]=int64_t(capsHr);if(SUCCEEDED(capsHr)){row["axes"]=caps.dwAxes;row["buttons"]=caps.dwButtons;row["povs"]=caps.dwPOVs;row["capabilityFlags"]=caps.dwFlags;row["forceFeedbackCapabilityAdvertised"]=(caps.dwFlags&DIDC_FORCEFEEDBACK)!=0;row["forceFeedbackActuallyUsed"]="unknown";}
        auto formatHr=device->SetDataFormat(&c_dfDIJoystick2);row["dataFormatHresult"]=int64_t(formatHr);auto coopHr=device->SetCooperativeLevel(window,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND);row["cooperativeHresult"]=int64_t(coopHr);
        if(SUCCEEDED(formatHr)&&SUCCEEDED(coopHr)){auto acquireHr=device->Acquire();row["acquireHresult"]=int64_t(acquireHr);if(SUCCEEDED(acquireHr)){DIJOYSTATE2 state{};row["pollHresult"]=int64_t(device->Poll());auto stateHr=device->GetDeviceState(sizeof(state),&state);row["stateHresult"]=int64_t(stateHr);row["stateApiSucceeded"]=SUCCEEDED(stateHr);
                if(SUCCEEDED(stateHr)){unsigned down=0;for(BYTE b:state.rgbButtons)down+=(b&0x80)!=0;row["buttonsDownCount"]=down;const long axes[8]={state.lX,state.lY,state.lZ,state.lRx,state.lRy,state.lRz,state.rglSlider[0],state.rglSlider[1]};row["controllerState"]=ymcc::msi::visibility::controllerState(axes,state.rgdwPOV,state.rgbButtons);noteComparison(row,"di:"+guid(instance->guidInstance)+":"+row.value("interfacePath",std::string()),row["controllerState"]);}}
            device->Unacquire();}
        device->Release();}
    row["scope"]="Enum/Create/Acquire/GetState are distinct; successful unchanged API values alone may be neutral/cached; independent observer exemption must be checked";emit(row);return DIENUM_CONTINUE;
}
void dinput(IDirectInput8W* di){if(!di)return;EnumContext context{di};auto hr=di->EnumDevices(DI8DEVCLASS_GAMECTRL,enumerate,&context,DIEDFL_ATTACHEDONLY);emit({{"event","directinput-inventory"},{"hresult",int64_t(hr)},{"count",context.count},{"limit",128}});}
void wgi(){try{using namespace winrt::Windows::Gaming::Input;unsigned seen=0;for(const auto& c:RawGameController::RawGameControllers()){if(++seen>128)break;json row={{"event","wgi-device"},{"enumerated",true}};
        try{const auto name=u8(c.DisplayName().c_str()),id=u8(c.NonRoamableId().c_str());row["name"]=name;row["nonRoamableId"]=id;row["vid"]=c.HardwareVendorId();row["pid"]=c.HardwareProductId();row["axes"]=c.AxisCount();row["buttons"]=c.ButtonCount();row["switches"]=c.SwitchCount();
            // These bounds refuse suspicious/broken descriptors, not gamepads.
            if(c.AxisCount()<0||c.AxisCount()>512||c.ButtonCount()<0||c.ButtonCount()>512||c.SwitchCount()<0||c.SwitchCount()>128){row["readSkipped"]="descriptor-size-limit";}else{
                std::unique_ptr<bool[]> buttons(new bool[size_t(c.ButtonCount())+1]{});std::vector<GameControllerSwitchPosition> switches(c.SwitchCount());std::vector<double> axes(c.AxisCount());
                auto timestamp=c.GetCurrentReading(winrt::array_view<bool>(buttons.get(),buttons.get()+c.ButtonCount()),switches,axes);row["readingTimestamp"]=timestamp;row["stateApiSucceeded"]=true;
                unsigned down=0;for(int i=0;i<c.ButtonCount();i++)down+=buttons[i];row["buttonsDownCount"]=down;json pressed=json::array();for(int i=0;i<c.ButtonCount();i++)if(buttons[i])pressed.push_back(i);row["buttonsDown"]=std::move(pressed);row["axisValues"]=axes;row["switchValues"]=json::array();for(const auto v:switches)row["switchValues"].push_back(static_cast<int>(v));noteComparison(row,"wgi:"+id,{{"buttonsDown",row["buttonsDown"]},{"axisValues",row["axisValues"]},{"switchValues",row["switchValues"]}});
            }}catch(const winrt::hresult_error& e){row["readHresult"]=int64_t(e.code().value);row["stateApiSucceeded"]=false;}
        row["scope"]="WGI enumeration/API values are not physical delivery proof; map against DirectInput paths, fresh HID access and operator button action; zero/unchanged remains unproven";emit(row);}
        emit({{"event","wgi-inventory"},{"count",seen},{"ok",true}});
    }catch(const winrt::hresult_error& e){emit({{"event","wgi-inventory"},{"ok",false},{"hresult",int64_t(e.code().value)}});}}
}
// Independent supervisor: a blocked WGI/DirectInput/HID metadata call must not
// leave this diagnostic observer alive indefinitely after the owner exits.
// It terminates ONLY this observer; never a controller, service or other app.
class ObserverOwnerDeadline {
    HANDLE done=nullptr;std::thread supervisor;
public:
    ObserverOwnerDeadline(HANDLE parent,unsigned seconds) {
        done=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!done)throw std::runtime_error("watchdog-event-unavailable");
        try {supervisor=std::thread([parent,seconds,event=done](){
            HANDLE waits[2]={event,parent};const DWORD rc=WaitForMultipleObjects(2,waits,FALSE,seconds*1000);
            if(rc==WAIT_OBJECT_0)return;
            // Allow ordinary main-loop cleanup on parent loss. The deadline
            // has only 100ms cleanup grace; a stuck controller call is cut off.
            const DWORD grace=rc==WAIT_OBJECT_0+1?1500:rc==WAIT_TIMEOUT?100:0;
            if(grace&&WaitForSingleObject(event,grace)==WAIT_OBJECT_0)return;
            const UINT exitCode=rc==WAIT_OBJECT_0+1?81:rc==WAIT_TIMEOUT?82:83;
            TerminateProcess(GetCurrentProcess(),exitCode);
            // If kernel teardown is delayed, this supervisor still performs no
            // more reads. Do not use ExitProcess (DLL-detach lock deadlocks).
            Sleep(INFINITE);
        });}catch(...){CloseHandle(done);done=nullptr;throw;}
    }
    ~ObserverOwnerDeadline(){if(done){SetEvent(done);if(supervisor.joinable())supervisor.join();CloseHandle(done);}}
    ObserverOwnerDeadline(const ObserverOwnerDeadline&)=delete;
};
static bool wakeEvidenceIdentity(const std::wstring& generationText,const std::wstring& episodeText,uint64_t& generation,unsigned& episode) {
    auto decimal=[](const std::wstring& s){if(s.empty())return false;for(wchar_t c:s)if(c<L'0'||c>L'9')return false;return true;};
    if(!decimal(generationText)||!decimal(episodeText))return false;
    try{size_t used=0;const auto g=std::stoull(generationText,&used);if(!g||used!=generationText.size())return false;
        used=0;const auto e=std::stoull(episodeText,&used);if(!e||e>2||used!=episodeText.size())return false;
        generation=g;episode=static_cast<unsigned>(e);return true;
    }catch(...){return false;}
}
// --selftest exercises synthetic decoding only: no DirectInput/WGI/HID/parent I/O.
int wmain(int argc,wchar_t** argv){
    (void)ymcc::processEfficiency::applyCurrent();
    if(argc==2 && std::wstring(argv[1])==L"--selftest") {
        const long axes[8]={-32768,32767,0,0,0,0,0,0};const unsigned long povs[4]={0xffffffff,9000,0xffffffff,0xffffffff};unsigned char buttons[128]{};buttons[0]=0x80;buttons[127]=0x80;buttons[1]=1;
        const auto j=ymcc::msi::visibility::controllerState(axes,povs,buttons);
        const bool ok=j["buttonsDown"]==json::array({0,127}) && j["axisValues"][0]==-32768 && j["povValues"][0]==0xffffffffu &&
            !ymcc::msi::visibility::observerEligible(false,true,true) && !ymcc::msi::visibility::observerEligible(true,false,true) && !ymcc::msi::visibility::observerEligible(true,true,false) && ymcc::msi::visibility::observerEligible(true,true,true) &&
            std::string(ymcc::msi::visibility::inverseState(""))=="unknown" && std::string(ymcc::msi::visibility::inverseState("--inv-off"))=="off" &&
            ymcc::msi::visibility::applications(false,{"steam.exe"},"--inv-off")["paths"].is_null();
        bool expanded=ok;
        const auto inv=[](const char* x){return std::string(ymcc::msi::visibility::inverseState(x));};
        expanded=expanded&&inv("--inv-off\r\n")=="off"&&inv(" \t--inv-on\r\n ")=="on"&&inv("\r\n ")=="unknown"&&
            inv("--inv-off extra")=="unknown"&&inv("--inv-OFF")=="unknown"&&inv("--inv-on\r\n--inv-off")=="unknown"&&
            ymcc::msi::visibility::applications(true,{},"--inv-off\r\n")["inverseRaw"]=="--inv-off\r\n"&&
            ymcc::msi::visibility::applications(false,{},"--inv-off\r\n")["inverseState"]=="unknown";
        json main={{"hidAttributesKnown",true},{"hidCapabilitiesKnown",true},{"hidVid",0x0db0},{"hidPid",0x1902},{"hidUsagePage",1},{"hidUsage",5},{"interfacePath","\\\\?\\HID#VID_0DB0&PID_1902&MI_00&COL01#fixture"}};
        expanded=expanded&&mainReadProbeEligible(main);
        for(const auto& p:{std::pair{"hidVid",0x045e},std::pair{"hidPid",0x1901},std::pair{"hidUsagePage",12},std::pair{"hidUsage",1}}){auto v=main;v[p.first]=p.second;expanded=expanded&&!mainReadProbeEligible(v);}
        for(const auto key:{"hidAttributesKnown","hidCapabilitiesKnown"}){auto v=main;v[key]=false;expanded=expanded&&!mainReadProbeEligible(v);}
        auto vendor=main;vendor["interfacePath"]="\\\\?\\HID#VID_0DB0&PID_1902&MI_00&COL02#fixture";expanded=expanded&&!mainReadProbeEligible(vendor);
        uint64_t wakeGeneration=0;unsigned wakeEpisode=0;
        expanded=expanded&&wakeEvidenceIdentity(L"2",L"1",wakeGeneration,wakeEpisode)&&wakeGeneration==2&&wakeEpisode==1&&
            wakeEvidenceIdentity(L"18446744073709551615",L"2",wakeGeneration,wakeEpisode)&&wakeGeneration==UINT64_MAX&&wakeEpisode==2;
        for(const auto& bad:{std::pair{L"0",L"1"},std::pair{L"2",L"0"},std::pair{L"2",L"3"},std::pair{L"+2",L"1"},std::pair{L"2x",L"1"},std::pair{L"2",L" 1"},std::pair{L"18446744073709551616",L"1"}})
            expanded=expanded&&!wakeEvidenceIdentity(bad.first,bad.second,wakeGeneration,wakeEpisode);
        return expanded?0:6;
    }
    const auto options=ymcc::msi::usbName::observerOptions(std::vector<std::wstring>(argv,argv+argc));
    if(!options.valid)return 2;const bool rawRequested=options.rawRequested;const bool nameRequested=!options.contextName.empty();argc=static_cast<int>(options.positionalCount);
    if(argc!=4 && argc!=6 && argc!=8)return 2;unsigned seconds=0;try{size_t used=0;seconds=std::stoul(argv[3],&used);if(used!=wcslen(argv[3])||seconds<1||seconds>900)return 2;}catch(...){return 2;}
    const std::wstring phase=argv[2];const bool fixture=phase==L"watchdog-fixture"||phase==L"watchdog-fixture-wake",wake=phase==L"msi-evidence-wake"||phase==L"watchdog-fixture-wake";
    const bool managed=phase==L"msi-evidence"||fixture||wake;
    uint64_t wakeGeneration=0,parentCreationTime=0;unsigned wakeEpisode=0;
    if(wake&&(argc!=8||!wakeEvidenceIdentity(argv[6],argv[7],wakeGeneration,wakeEpisode)))return 2;
    if(phase!=L"off"&&phase!=L"on"&&phase!=L"off-repeat"&&phase!=L"smoke"&&phase!=L"transition"&&!managed)return 2;
    if((wake&&argc!=8)||(!wake&&managed&&argc!=6)||(!managed&&argc!=4))return 2;
    if(nameRequested&&(!managed||fixture))return 2;
    HANDLE parent=nullptr;DWORD parentPid=0;
    if(managed){try{size_t used=0;const auto pid=std::stoull(argv[4],&used);if(!pid||pid>MAXDWORD||used!=wcslen(argv[4]))return 2;parentPid=static_cast<DWORD>(pid);used=0;const auto expected=std::stoull(argv[5],&used);if(!expected||used!=wcslen(argv[5]))return 2;parentCreationTime=expected;
        parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parentPid);if(!parent)return 7;
        FILETIME created{},exited{},kernel{},user{};ULARGE_INTEGER stamp{};const bool timeOk=GetProcessTimes(parent,&created,&exited,&kernel,&user)!=FALSE;stamp.LowPart=created.dwLowDateTime;stamp.HighPart=created.dwHighDateTime;
        if(!timeOk||stamp.QuadPart!=expected||WaitForSingleObject(parent,0)!=WAIT_TIMEOUT){CloseHandle(parent);return 7;}
    }catch(...){if(parent)CloseHandle(parent);return 2;}}
    // Only this observer's own fixed output and .1 are rotated; no device/settings changes.
    const std::wstring log=argv[1];const DWORD logAttributes=GetFileAttributesW(log.c_str());
    if(log.empty()||(logAttributes!=INVALID_FILE_ATTRIBUTES&&(logAttributes&FILE_ATTRIBUTE_DIRECTORY))){if(parent)CloseHandle(parent);return 3;}
    // Never move a directory or truncate the prior session after a failed
    // rotation (e.g. a previous observer still owns its file).
    if(managed&&logAttributes!=INVALID_FILE_ATTRIBUTES&&!MoveFileExW(log.c_str(),(log+L".1").c_str(),MOVEFILE_REPLACE_EXISTING)){if(parent)CloseHandle(parent);return 3;}
    out.open(std::filesystem::path(log),std::ios::binary|std::ios::trunc);if(!out){if(parent)CloseHandle(parent);return 3;}
    std::unique_ptr<ObserverOwnerDeadline> watchdog;
    if(managed)try{watchdog=std::make_unique<ObserverOwnerDeadline>(parent,seconds);}catch(...){emit({{"event","supervisor-unavailable"},{"readsSkipped",true}});if(parent)CloseHandle(parent);return 9;}
    if(fixture){emit({{"event","watchdog-fixture-start"},{"parentPid",parentPid},{"parentCreationTime",parentCreationTime},{"wakeGeneration",wakeGeneration},{"wakeEpisode",wakeEpisode},{"durationSec",seconds},{"fixtureDoesNoControllerIO",true}});Sleep(INFINITE);return 10;}
    std::unique_ptr<ymcc::msi::usbName::NameProvenance> nameProvenance;bool nameContextOpened=false;
    if(nameRequested)try{nameProvenance=std::make_unique<ymcc::msi::usbName::NameProvenance>();nameContextOpened=nameProvenance->open(options.contextName,parentPid,parentCreationTime,parent);if(!nameContextOpened){nameProvenance.reset();emit({{"event","msi-usb-name-origin"},{"version",1},{"state","parent-bound-context-unavailable"},{"writesHardware",false}});}}catch(...){nameProvenance.reset();emit({{"event","msi-usb-name-origin"},{"version",1},{"state","context-initialization-failed"},{"writesHardware",false}});}
    try{winrt::init_apartment(winrt::apartment_type::multi_threaded);}catch(const winrt::hresult_error& e){emit({{"event","apartment-error"},{"hresult",int64_t(e.code().value)}});watchdog.reset();if(parent)CloseHandle(parent);return 4;}
    const wchar_t* windowClass=L"STATIC";
    if(rawRequested){WNDCLASSW wc{};wc.lpfnWndProc=probeWindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YeManCCIndependentMsiRawReceiptObserver";if(!RegisterClassW(&wc)){emit({{"event","rawinput-registration"},{"registered",false},{"win32Error",GetLastError()},{"scope","observer window class registration failed"}});winrt::uninit_apartment();watchdog.reset();if(parent)CloseHandle(parent);return 4;}windowClass=wc.lpszClassName;}
    window=CreateWindowExW(0,windowClass,L"YeManCC read-only controller probe",0,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window){winrt::uninit_apartment();watchdog.reset();if(parent)CloseHandle(parent);return 4;}
    DWORD rawError=ERROR_SUCCESS;
    if(rawRequested){rawReceiptsActive=ymcc::msi::external::registerRawReceipts(window,false,rawError);rawCounters.begin(GetTickCount64());}
    IDirectInput8W* di=nullptr;auto hr=DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&di),nullptr);
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);emit({{"event","start"},{"formatVersion",6},{"msiUsbNameOriginSupported",true},{"msiUsbNameOriginVersion",1},{"msiUsbNameOriginRequested",nameRequested},{"msiUsbNameContextOpened",nameContextOpened},{"msiGamepadSemanticWitnessVersion",1},{"rawInputReceiptsRequested",rawRequested},{"rawInputReceiptsRegistered",rawReceiptsActive},{"rawInputRegistrationError",rawError},{"phase",u8(phase)},{"parentPid",parentPid},{"parentCreationTime",parentCreationTime},{"wakeGeneration",wakeGeneration},{"wakeEpisode",wakeEpisode},{"durationSec",seconds},{"directInputHresult",int64_t(hr)},{"observerExecutable",u8(self)},{"readOnly",true},{"writesHardware",false},{"blockingReadSupervised",managed},{"parentExitCleanupGraceMs",1500},{"deadlineCleanupGraceMs",100},{"forcedExitCodes",{{"parentExit",81},{"deadline",82},{"supervisorError",83}}},{"hidhideExemption","not-assumed; compare configuration applications and inverse-state"},{"privacy","game-controller API values plus optional qualified MSI raw receipt counts and bounded descriptor-decoded main-gamepad controls; no raw report bytes, keyboard text or mouse positions"}});
    const auto deadline=GetTickCount64()+uint64_t(seconds)*1000;std::string stopReason="bounded-session-complete";
    while(GetTickCount64()<deadline){if(parent){const DWORD rc=WaitForSingleObject(parent,0);if(rc!=WAIT_TIMEOUT){stopReason=rc==WAIT_OBJECT_0?"parent-exited":"parent-wait-failed";break;}}
        ++inventoryId;dinput(di);wgi();msiPnp();if(nameProvenance)try{nameProvenance->observe([](json row){emit(std::move(row));});}catch(...){emit({{"event","msi-usb-name-origin"},{"version",1},{"state","capture-exception-unknown"},{"writesHardware",false}});nameProvenance.reset();}const auto until=GetTickCount64()+1000;while(GetTickCount64()<until&&GetTickCount64()<deadline){unsigned pumped=0;const auto pumpBegin=GetTickCount64();for(MSG m{};pumped<256&&GetTickCount64()-pumpBegin<10&&PeekMessageW(&m,nullptr,0,0,PM_REMOVE);){++pumped;dispatchedQueueTime=ymcc::msi::external::resolveQueueTime(GetTickCount64(),m.time);TranslateMessage(&m);DispatchMessageW(&m);dispatchedQueueTime={};}if(rawReceiptsActive&&(pumped>=256||GetTickCount64()-pumpBegin>=10))rawCounters.pumpLimited();Sleep(20);}if(rawRequested){auto receipts=rawCounters.flush(GetTickCount64());receipts["rawInputReceiptsRequested"]=true;receipts["rawInputReceiptsRegistered"]=rawReceiptsActive;receipts["rawInputRegistrationError"]=rawError;emit(std::move(receipts));}}
    if(rawReceiptsActive){DWORD cleanupError=0;const bool cleaned=ymcc::msi::external::registerRawReceipts(window,true,cleanupError);emit({{"event","rawinput-registration-cleanup"},{"removed",cleaned},{"win32Error",cleanupError},{"scope","this observer process registrations only"}});rawReceiptsActive=false;}
    if(di)di->Release();DestroyWindow(window);if(rawRequested)UnregisterClassW(windowClass,GetModuleHandleW(nullptr));winrt::uninit_apartment();emit({{"event","stop"},{"phase",u8(phase)},{"parentPid",parentPid},{"parentCreationTime",parentCreationTime},{"wakeGeneration",wakeGeneration},{"wakeEpisode",wakeEpisode},{"reason",stopReason},{"readOnly",true}});watchdog.reset();if(parent)CloseHandle(parent);return out.good()?0:5;
}
