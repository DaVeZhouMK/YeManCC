// Standalone NON-exempt observer. No HID writes, SendInput, power requests,
// sleep/brightness changes, service control or device disable/refresh commands.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "../native/input_idle_windows_json.h"
#include <Xinput.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <map>
#include <iostream>
using nlohmann::json;
namespace {
std::ofstream output;
struct Device {USHORT usagePage=0,usage=0;DWORD vid=0,pid=0;uint64_t reports=0,changes=0,lastHash=0;bool haveHash=false;
    DWORD type=RIM_TYPEHID;uint64_t mouseMoves=0,mouseButtonsOrWheel=0,keyboardReports=0;LONG lastX=0,lastY=0;bool haveAbsolute=false;};
std::map<HANDLE,Device> devices;
int displayState=-1;
uint64_t lastInputTick=0;
void write(json row){
    FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER time{};time.LowPart=ft.dwLowDateTime;time.HighPart=ft.dwHighDateTime;
    row["utcEpochMs"]=(time.QuadPart-116444736000000000ull)/10000;row["tick"]=GetTickCount64();row["observerPid"]=GetCurrentProcessId();
    output<<row.dump()<<"\n";output.flush();
}
void inventory(HANDLE handle) {
    if(devices.contains(handle)||devices.size()>=256)return;
    RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT bytes=sizeof(info);
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICEINFO,&info,&bytes)==UINT(-1))return;
    Device d;d.type=info.dwType;
    if(info.dwType==RIM_TYPEHID){const auto& h=info.hid;
        if(h.usUsagePage!=1||(h.usUsage!=4&&h.usUsage!=5))return;
        d.usagePage=h.usUsagePage;d.usage=h.usUsage;d.vid=h.dwVendorId;d.pid=h.dwProductId;
    }else if(info.dwType==RIM_TYPEKEYBOARD||info.dwType==RIM_TYPEMOUSE){
        d.usagePage=1;d.usage=info.dwType==RIM_TYPEMOUSE?2:6;
        // Resolve VID/PID only. Do NOT write path, serial, key codes or coordinates.
        UINT chars=0;GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,nullptr,&chars);
        if(chars&&chars<32768){std::vector<wchar_t> name(chars+1);
            if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,name.data(),&chars)!=UINT(-1)){
                std::wstring text=name.data();for(auto& c:text)c=static_cast<wchar_t>(towupper(c));
                for(auto [token,target]:{std::pair{L"VID_",&d.vid},std::pair{L"PID_",&d.pid}}){
                    auto at=text.find(token);if(at!=std::wstring::npos&&at+8<=text.size()){
                        auto hex=text.substr(at+4,4);wchar_t* end=nullptr;auto value=wcstoul(hex.c_str(),&end,16);
                        if(end==hex.c_str()+4)*target=value;
                    }
                }
            }
        }
    }else return;
    devices[handle]=d;
    write({{"event","raw-input-device"},{"deviceKey",reinterpret_cast<uintptr_t>(handle)},
        {"vid",d.vid},{"pid",d.pid},{"usagePage",d.usagePage},{"usage",d.usage},{"rawType",d.type},
        {"scope","enumeration only; delivered event counts are separate; no serial, keys, text or coordinates saved"}});
}

LRESULT CALLBACK proc(HWND window,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_INPUT){
        UINT size=0;if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))==0&&size<=65536){
            std::vector<BYTE> bytes(size);
            if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,bytes.data(),&size,sizeof(RAWINPUTHEADER))==size&&size>=sizeof(RAWINPUTHEADER)){
                auto* raw=reinterpret_cast<RAWINPUT*>(bytes.data());
                if(raw->header.dwType==RIM_TYPEMOUSE||raw->header.dwType==RIM_TYPEKEYBOARD){
                    inventory(raw->header.hDevice);auto it=devices.find(raw->header.hDevice);
                    if(it!=devices.end()){
                        auto& d=it->second;++d.reports;
                        if(raw->header.dwType==RIM_TYPEKEYBOARD && size>=offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD))++d.keyboardReports;
                        else if(raw->header.dwType==RIM_TYPEMOUSE && size>=offsetof(RAWINPUT,data)+sizeof(RAWMOUSE)){
                            const auto& mouse=raw->data.mouse;
                            if(mouse.usFlags&MOUSE_MOVE_ABSOLUTE){if(d.haveAbsolute&&(d.lastX!=mouse.lLastX||d.lastY!=mouse.lLastY))++d.mouseMoves;
                                d.haveAbsolute=true;d.lastX=mouse.lLastX;d.lastY=mouse.lLastY;}
                            else if(mouse.lLastX||mouse.lLastY)++d.mouseMoves;
                            if(mouse.usButtonFlags)++d.mouseButtonsOrWheel;
                        }
                    }
                }
                if(raw->header.dwType==RIM_TYPEHID){inventory(raw->header.hDevice);auto it=devices.find(raw->header.hDevice);
                    if(it!=devices.end()&&size>=offsetof(RAWINPUT,data.hid.bRawData)){
                        auto& d=it->second;const auto& hid=raw->data.hid;
                        const uint64_t payload=uint64_t(hid.dwSizeHid)*hid.dwCount;
                        if(payload<=size-offsetof(RAWINPUT,data.hid.bRawData)){
                            for(DWORD i=0;i<hid.dwCount;++i){uint64_t hash=1469598103934665603ull;
                                for(DWORD b=0;b<hid.dwSizeHid;++b){hash^=hid.bRawData[uint64_t(i)*hid.dwSizeHid+b];hash*=1099511628211ull;}
                                ++d.reports;if(d.haveHash&&d.lastHash!=hash)++d.changes;d.lastHash=hash;d.haveHash=true;
                            }
                        }
                    }
                }
            }
        }
        // Required foreground cleanup. No key codes, text, coordinates or raw bytes persisted.
        return DefWindowProcW(window,msg,w,l);
    }
    if(msg==WM_INPUT_DEVICE_CHANGE){
        auto handle=reinterpret_cast<HANDLE>(l);
        if(w==GIDC_ARRIVAL)inventory(handle);
        else if(w==GIDC_REMOVAL&&devices.contains(handle)){
            write({{"event","raw-gamepad-removed"},{"deviceKey",reinterpret_cast<uintptr_t>(handle)}});devices.erase(handle);
        }
    }
    if(msg==WM_POWERBROADCAST&&w==PBT_POWERSETTINGCHANGE&&l){
        const auto* setting=reinterpret_cast<const POWERBROADCAST_SETTING*>(l);
        if(IsEqualGUID(setting->PowerSetting,ymcc::idle::sessionDisplay)&&setting->DataLength==sizeof(DWORD)){
            DWORD state=0;memcpy(&state,setting->Data,sizeof(state));if(state<=2){displayState=int(state);write({{"event","display-state"},{"state",state}});}
        }
    }
    return DefWindowProcW(window,msg,w,l);
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4){std::cerr<<"Usage: probe output.jsonl phase seconds\n";return 2;}
    const std::wstring phaseWide=argv[2];std::string phase;for(wchar_t c:phaseWide){if(c>127)return 2;phase.push_back(static_cast<char>(c));}
    if(phase!="off"&&phase!="on"&&phase!="off-repeat"&&phase!="smoke")return 2;
    unsigned seconds=0;try{size_t used=0;seconds=std::stoul(argv[3],&used);if(used!=wcslen(argv[3])||seconds<1||seconds>900)return 2;}catch(...){return 2;}
    output.open(std::filesystem::path(argv[1]),std::ios::binary|std::ios::trunc);if(!output)return 3;
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);std::wstring widePath=self;std::string asciiPath;
    const int chars=WideCharToMultiByte(CP_UTF8,0,widePath.c_str(),int(widePath.size()),nullptr,0,nullptr,nullptr);
    asciiPath.resize(chars);WideCharToMultiByte(CP_UTF8,0,widePath.c_str(),int(widePath.size()),asciiPath.data(),chars,nullptr,nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.lpszClassName=L"YeManCCReadOnlyIdleProbe";wc.hInstance=GetModuleHandleW(nullptr);
    if(!RegisterClassW(&wc))return 4;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!window)return 4;
    RAWINPUTDEVICE input[]={{1,4,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window},{1,5,RIDEV_INPUTSINK,window}};
    const bool rawOk=RegisterRawInputDevices(input,4,sizeof(RAWINPUTDEVICE))!=FALSE;
    const DWORD rawError=rawOk?0:GetLastError();
    auto notification=RegisterPowerSettingNotification(window,&ymcc::idle::sessionDisplay,DEVICE_NOTIFY_WINDOW_HANDLE);
    HMODULE xinput=LoadLibraryExW(L"xinput1_4.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto get=xinput?reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(xinput,"XInputGetState")):nullptr;
    write({{"event","start"},{"phase",phase},{"durationSec",seconds},{"observerExecutable",asciiPath},
        {"rawRegistrationOk",rawOk},{"rawRegistrationError",rawError},{"xinputAvailable",get!=nullptr},
        {"displayNotificationRegistered",notification!=nullptr},{"hiddenWindow",true},
        {"hidhideExemption","not-assumed; inspect wrapper app-list and inverse-state"},
        {"readOnly",true},{"keyboardMouseCapture","aggregate counts per VID/PID only; no key codes, typed text or coordinates"},{"inputInjection",false},{"installed",false}});
    UINT deviceCount=0;
    const bool countOk=GetRawInputDeviceList(nullptr,&deviceCount,sizeof(RAWINPUTDEVICELIST))!=UINT(-1);
    if(countOk&&deviceCount<=4096){
        std::vector<RAWINPUTDEVICELIST> list(deviceCount);UINT capacity=deviceCount;
        const UINT found=GetRawInputDeviceList(list.data(),&capacity,sizeof(RAWINPUTDEVICELIST));
        if(found!=UINT(-1)){for(UINT i=0;i<found;++i)inventory(list[i].hDevice);}
        else write({{"event","inventory-error"},{"error",GetLastError()}});
    }else write({{"event","inventory-error"},{"countQueryOk",countOk},{"deviceCount",deviceCount}});
    const ULONGLONG start=GetTickCount64(),deadline=start+uint64_t(seconds)*1000;ULONGLONG next=start;
    std::array<XINPUT_STATE,4> previous{};unsigned previousMask=0;
    while(GetTickCount64()<deadline){
        for(MSG message{};PeekMessageW(&message,nullptr,0,0,PM_REMOVE);){TranslateMessage(&message);DispatchMessageW(&message);}
        auto now=GetTickCount64();if(now>=next){next=now+1000;
            const auto env=ymcc::idle::readEnvironment();json slots=json::array();unsigned mask=0;
            for(DWORD i=0;i<4;++i){XINPUT_STATE state{};DWORD rc=get?get(i,&state):ERROR_PROC_NOT_FOUND;
                if(rc==ERROR_SUCCESS){mask|=1u<<i;auto& p=state.Gamepad;
                    ymcc::idle::Pad pad{p.wButtons,0,0,{p.sThumbLX,p.sThumbLY,p.sThumbRX,p.sThumbRY},{p.bLeftTrigger,p.bRightTrigger}};
                    const auto& old=previous[i].Gamepad;ymcc::idle::Pad before{old.wButtons,0,0,{old.sThumbLX,old.sThumbLY,old.sThumbRX,old.sThumbRY},{old.bLeftTrigger,old.bRightTrigger}};
                    slots.push_back({{"slot",i},{"packet",state.dwPacketNumber},{"packetChanged",(previousMask&(1u<<i))&&previous[i].dwPacketNumber!=state.dwPacketNumber},
                        {"contentChanged",(previousMask&(1u<<i))&&pad!=before},{"rawActive",ymcc::idle::rawActive(pad)},
                        {"meaningful",ymcc::idle::meaningful(pad)},{"pad",ymcc::idle::padJson(pad)},{"identity","unknown; slot alone is not physical or virtual identity"}});
                    previous[i]=state;
                }else slots.push_back({{"slot",i},{"status",rc}});
            }
            previousMask=mask;json raw=json::array();for(auto& [key,d]:devices){raw.push_back({{"deviceKey",reinterpret_cast<uintptr_t>(key)},{"vid",d.vid},{"pid",d.pid},
                {"rawType",d.type},{"reportsDelta",d.reports},{"rawBytesChangedDelta",d.changes},{"mouseMovementDelta",d.mouseMoves},{"mouseButtonOrWheelDelta",d.mouseButtonsOrWheel},{"keyboardReportDelta",d.keyboardReports},
                {"scope","HID changed bytes may include timestamps; keyboard/mouse counts only, no codes/text/coordinates"}});d.reports=d.changes=d.mouseMoves=d.mouseButtonsOrWheel=d.keyboardReports=0;}
            auto row=ymcc::idle::environmentJson(env);row["event"]="sample";row["phase"]=phase;row["displayState"]=displayState;row["connectedMask"]=mask;
            row["xinputSlots"]=slots;row["rawInputDelivery"]=raw;
            row["lastInputTickChanged"]=lastInputTick&&env.lastInputOk&&lastInputTick!=env.lastInputTick;lastInputTick=env.lastInputTick;
            write(std::move(row));
        }
        // Message-aware wait, no busy polling, no display or system power request.
        MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    if(notification)UnregisterPowerSettingNotification(notification);
    RAWINPUTDEVICE remove[]={{1,4,RIDEV_REMOVE,nullptr},{1,5,RIDEV_REMOVE,nullptr},{1,2,RIDEV_REMOVE,nullptr},{1,6,RIDEV_REMOVE,nullptr}};if(rawOk)RegisterRawInputDevices(remove,4,sizeof(RAWINPUTDEVICE));
    if(xinput)FreeLibrary(xinput);DestroyWindow(window);
    write({{"event","stop"},{"phase",phase},{"elapsedMs",GetTickCount64()-start},{"readOnly",true}});
    return output.good()?0:5;
}
