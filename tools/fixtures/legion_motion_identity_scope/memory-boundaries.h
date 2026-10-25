#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// NO Windows headers or libraries: every Win32/HID/CM/SetupAPI name below is a
// fixture function over memory. This is not an ABI layout or live device test.
using BYTE=unsigned char;using USHORT=unsigned short;using USAGE=unsigned short;
using DWORD=std::uint32_t;using UINT=unsigned;using ULONG=std::uint32_t;using ULONGLONG=std::uint64_t;
using CONFIGRET=unsigned;using DEVINST=std::uint32_t;using WCHAR=wchar_t;
using HANDLE=void*;using HDEVINFO=void*;
#define INVALID_HANDLE_VALUE reinterpret_cast<void*>(static_cast<std::intptr_t>(-1))
constexpr int FALSE=0,TRUE=1;constexpr DWORD ERROR_SUCCESS=0,ERROR_NO_MORE_ITEMS=259,ERROR_INSUFFICIENT_BUFFER=122;
constexpr DWORD FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=3,FILE_ATTRIBUTE_NORMAL=128;
constexpr DWORD DIGCF_PRESENT=2,DIGCF_DEVICEINTERFACE=16,RIDI_DEVICENAME=0x20000007;
constexpr CONFIGRET CR_SUCCESS=0,CR_NO_SUCH_DEVNODE=13,CR_INVALID_DEVINST=5;
constexpr ULONG DN_WILL_BE_REMOVED=0x40000,DN_NO_SHOW_IN_DM=0x40000000;
constexpr UINT MAX_DEVICE_ID_LEN=200;constexpr int HIDP_STATUS_SUCCESS=0x110000;
struct SP_DEVINFO_DATA{DWORD cbSize=0;DEVINST DevInst=0;};
struct SP_DEVICE_INTERFACE_DATA{DWORD cbSize=0;DWORD index=0;};
struct SP_DEVICE_INTERFACE_DETAIL_DATA_W{DWORD cbSize;wchar_t DevicePath[1];};
struct HIDD_ATTRIBUTES{DWORD Size=0;USHORT VendorID=0,ProductID=0;};
struct HIDP_CAPS{USAGE UsagePage=0,Usage=0;};
struct GUID{};static GUID kYeManGuidDeviceInterfaceHid;
enum class YmccFamily{Unknown,LenovoLegionGo,Other};
struct MachineIdentity{YmccFamily family=YmccFamily::LenovoLegionGo;std::string deviceClass="HandheldCompanion.Devices.LegionGoTablet";};
static MachineIdentity g_machineIdentity;
namespace memory {
struct Node{std::string id;DEVINST parent=0;CONFIGRET idResult=CR_SUCCESS,parentResult=CR_SUCCESS,statusResult=CR_SUCCESS;ULONG status=0;};
struct Device{std::wstring path;DEVINST node=0;USHORT vid=0x17EF,pid=0x6182;USAGE page=0xFFA0,usage=1;bool open=true,attributes=true,preparsed=true,caps=true;};
inline std::map<DEVINST,Node> nodes;inline std::vector<Device> devices;inline std::map<HANDLE,std::wstring> rawPaths;
inline DWORD error=ERROR_SUCCESS;inline ULONGLONG now=100;
inline unsigned opens=0,closes=0,preps=0,frees=0,xusb=0,hit=0,discards=0,routeRefreshes=0,enumerations=0,selectedTransitions=0,msiNotifications=0;
inline std::wstring route=L"\\?\\usb#xusb-test";inline std::function<void()> routeHook;
inline HDEVINFO set(){return reinterpret_cast<void*>(std::uintptr_t{0x1234});}
inline Device* find(const wchar_t* path){for(auto&d:devices)if(d.path==path)return &d;return nullptr;}
}
using PHIDP_PREPARSED_DATA=memory::Device*;
static std::string W2U(const std::wstring& s){std::string out;for(wchar_t c:s){if(c<0||c>127)throw std::runtime_error("ASCII-only fixture boundary");out.push_back(static_cast<char>(c));}return out;}
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
static std::string ascii_lower(std::string s){for(auto&c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
static DWORD GetLastError(){return memory::error;}
static ULONGLONG GetTickCount64(){return memory::now;}
static HANDLE CreateFileW(const wchar_t*p,DWORD,DWORD,void*,DWORD,DWORD,void*){++memory::opens;auto*d=memory::find(p);if(!d||!d->open){memory::error=5;return INVALID_HANDLE_VALUE;}return d;}
static int CloseHandle(HANDLE){++memory::closes;return TRUE;}
static int HidD_GetAttributes(HANDLE p,HIDD_ATTRIBUTES*a){auto*d=static_cast<memory::Device*>(p);if(!d->attributes)return FALSE;a->VendorID=d->vid;a->ProductID=d->pid;return TRUE;}
static int HidD_GetPreparsedData(HANDLE p,PHIDP_PREPARSED_DATA*out){auto*d=static_cast<memory::Device*>(p);if(!d->preparsed)return FALSE;*out=d;++memory::preps;return TRUE;}
static int HidP_GetCaps(PHIDP_PREPARSED_DATA p,HIDP_CAPS*c){if(!p->caps)return -1;c->UsagePage=p->page;c->Usage=p->usage;return HIDP_STATUS_SUCCESS;}
static int HidD_FreePreparsedData(PHIDP_PREPARSED_DATA){++memory::frees;return TRUE;}
static int SetupDiGetDeviceInstanceIdW(HDEVINFO,SP_DEVINFO_DATA*data,wchar_t*buffer,DWORD chars,DWORD*required){
    auto it=memory::nodes.find(data->DevInst);if(it==memory::nodes.end()||it->second.idResult!=CR_SUCCESS)return FALSE;
    auto id=U2W(it->second.id);if(required)*required=static_cast<DWORD>(id.size()+1);if(!buffer||chars<id.size()+1){memory::error=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
    std::copy(id.begin(),id.end(),buffer);buffer[id.size()]=0;return TRUE;}
static CONFIGRET CM_Get_Device_IDW(DEVINST node,wchar_t*buffer,ULONG chars,ULONG){
    auto it=memory::nodes.find(node);if(it==memory::nodes.end())return CR_INVALID_DEVINST;if(it->second.idResult!=CR_SUCCESS)return it->second.idResult;
    const auto id=U2W(it->second.id);if(id.size()+1>chars)return CR_INVALID_DEVINST;std::copy(id.begin(),id.end(),buffer);buffer[id.size()]=0;return CR_SUCCESS;}
static CONFIGRET CM_Get_Parent(DEVINST*out,DEVINST node,ULONG){auto it=memory::nodes.find(node);if(it==memory::nodes.end())return CR_INVALID_DEVINST;if(it->second.parentResult!=CR_SUCCESS)return it->second.parentResult;if(!it->second.parent)return CR_NO_SUCH_DEVNODE;*out=it->second.parent;return CR_SUCCESS;}
static CONFIGRET CM_Get_DevNode_Status(ULONG*status,ULONG*problem,DEVINST node,ULONG){auto it=memory::nodes.find(node);if(it==memory::nodes.end())return CR_INVALID_DEVINST;*status=it->second.status;*problem=0;return it->second.statusResult;}
static HDEVINFO SetupDiGetClassDevsW(const GUID*,const wchar_t*,void*,DWORD){++memory::enumerations;return memory::set();}
static int SetupDiEnumDeviceInterfaces(HDEVINFO,void*,const GUID*,DWORD index,SP_DEVICE_INTERFACE_DATA*out){if(index>=memory::devices.size()){memory::error=ERROR_NO_MORE_ITEMS;return FALSE;}out->index=index;return TRUE;}
static int SetupDiGetDeviceInterfaceDetailW(HDEVINFO,SP_DEVICE_INTERFACE_DATA*iface,SP_DEVICE_INTERFACE_DETAIL_DATA_W*out,DWORD bytes,DWORD*required,SP_DEVINFO_DATA*data){
    auto&d=memory::devices.at(iface->index);const DWORD need=static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath)+(d.path.size()+1)*sizeof(wchar_t));
    if(required)*required=need;if(!out||bytes<need){memory::error=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
    std::copy(d.path.begin(),d.path.end(),out->DevicePath);out->DevicePath[d.path.size()]=0;if(data)data->DevInst=d.node;return TRUE;}
static int SetupDiDestroyDeviceInfoList(HDEVINFO){return TRUE;}
static UINT GetRawInputDeviceInfoW(HANDLE h,UINT,void*buffer,UINT*chars){auto it=memory::rawPaths.find(h);if(it==memory::rawPaths.end())return static_cast<UINT>(-1);const UINT need=static_cast<UINT>(it->second.size()+1);if(!buffer){*chars=need;return 0;}if(*chars<need)return static_cast<UINT>(-1);auto*p=static_cast<wchar_t*>(buffer);std::copy(it->second.begin(),it->second.end(),p);p[it->second.size()]=0;*chars=need;return need;}
static std::wstring hidHideResolveXusbInterfacePath(const std::string&){++memory::xusb;auto hook=std::move(memory::routeHook);memory::routeHook={};if(hook)hook();return memory::route;}
struct GpHidResolveScope{void noteCacheHit(){++memory::hit;}void noteRouteRefresh(bool){++memory::routeRefreshes;}void noteGenerationDiscard(){++memory::discards;}};
struct GpHidResolveCallerScope{explicit GpHidResolveCallerScope(int){}};
struct LogField{template<class T>LogField(const char*,const T&){}};
static void appendNativeLifecycleLog(const char*,std::initializer_list<LogField>){}
static bool tickCostEnabled(){return false;}
static std::array<std::atomic<unsigned>,2> g_gpSelectedGenerationRejects{};
template<class A,class B>void legionMotionSelectedIdentityChanging(const A&,const B&){++memory::selectedTransitions;}
template<class A>bool msiDinputObservePhysicalXinputReturn(const A&,ULONGLONG){++memory::msiNotifications;return false;}
template<class A>void msiForegroundIsolationObserveNative(const A&){++memory::msiNotifications;}
template<class A>void msiNativeForegroundObserveSource(const A&,std::uint64_t){++memory::msiNotifications;}
