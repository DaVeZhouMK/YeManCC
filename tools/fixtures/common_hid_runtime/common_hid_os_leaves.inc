// SDK structures are real. Only the OS calls are fixture ports.
#include <hidsdi.h>
#include <hidpi.h>
#include <cfgmgr32.h>
struct CommonHidFakeOs {
    RID_DEVICE_INFO info{};
    HIDP_CAPS caps{};
    std::array<HIDP_BUTTON_CAPS,8> buttons{};
    std::array<HIDP_VALUE_CAPS,16> values{};
    ULONG maxData=0;
    unsigned rawCalls=0,hidCalls=0,cfgCalls=0,bindingCalls=0,clockCalls=0;
    std::vector<std::string> apiTrace;
    ULONGLONG tick=10000;
    std::string fault;
    std::wstring rawPath=L"\\\\?\\hid#fixture-selected";
};
static CommonHidFakeOs fakeOs;
static unsigned registrationQueries=0,windowQueries=0,windowOwnerQueries=0;
static std::string registrationFault;
static UINT WINAPI fakeRegisteredRawInput(PRAWINPUTDEVICE output,PUINT count,UINT bytes) {
    ++registrationQueries;SetLastError(2468);
    if(bytes!=sizeof(RAWINPUTDEVICE)||*count!=32)throw std::runtime_error("registration capacity contract");
    if(registrationFault=="capacity"){*count=33;SetLastError(ERROR_INSUFFICIENT_BUFFER);return ~0u;}
    if(registrationFault=="failure"){*count=32;SetLastError(ERROR_ACCESS_DENIED);return ~0u;}
    const HWND target=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(registrationFault=="wrong-target"?1706610:1706608));
    for(UINT i=0;i<3;++i)output[i]={1,static_cast<USHORT>(i==0?4:i==1?5:8),registrationFault=="no-inputsink"?0u:RIDEV_INPUTSINK,target};
    *count=3;return 3;
}
static BOOL WINAPI fakeRegistrationIsWindow(HWND){++windowQueries;SetLastError(2469);return registrationFault!="dead-sink";}
static DWORD WINAPI fakeRegistrationWindowOwner(HWND,LPDWORD pid){++windowOwnerQueries;*pid=2488;SetLastError(2470);return 4321;}
#define GetRegisteredRawInputDevices fakeRegisteredRawInput
#define IsWindow fakeRegistrationIsWindow
#define GetWindowThreadProcessId fakeRegistrationWindowOwner
static ULONGLONG fakeTick(){++fakeOs.clockCalls;return fakeOs.tick;}
static UINT WINAPI fakeRawInfo(HANDLE,UINT command,LPVOID output,PUINT bytes) {
    ++fakeOs.rawCalls;
    fakeOs.apiTrace.push_back(command==RIDI_DEVICEINFO?"info":command==RIDI_DEVICENAME?(output?"name-read":"name-size"):(output?"preparsed-read":"preparsed-size"));
    if(command==RIDI_DEVICEINFO) {
        if(fakeOs.fault=="device-info-read")return ~0u;
        auto value=fakeOs.info;
        if(fakeOs.fault=="not-gamepad-collection")value.hid.usUsage=2;
        if(!output||*bytes<sizeof(value))return ~0u;
        *static_cast<RID_DEVICE_INFO*>(output)=value;*bytes=sizeof(value);return sizeof(value);
    }
    if(command==RIDI_DEVICENAME) {
        if(!output){*bytes=fakeOs.fault=="device-name-size"?0:static_cast<UINT>(fakeOs.rawPath.size()+1);return 0;}
        if(fakeOs.fault=="device-name-read")return ~0u;
        if(*bytes<fakeOs.rawPath.size()+1)return ~0u;
        std::copy(fakeOs.rawPath.begin(),fakeOs.rawPath.end(),static_cast<wchar_t*>(output));
        static_cast<wchar_t*>(output)[fakeOs.rawPath.size()]=L'\0';
        *bytes=static_cast<UINT>(fakeOs.rawPath.size()+1);return *bytes;
    }
    if(command==RIDI_PREPARSEDDATA) {
        if(!output){*bytes=fakeOs.fault=="preparsed-size"?0:8;return 0;}
        if(fakeOs.fault=="preparsed-read"||*bytes<8)return ~0u;
        std::fill_n(static_cast<BYTE*>(output),8,BYTE{0});*bytes=8;return 8;
    }
    return ~0u;
}
static NTSTATUS __stdcall fakeHidCaps(PHIDP_PREPARSED_DATA,PHIDP_CAPS output) {
    ++fakeOs.hidCalls;
    fakeOs.apiTrace.push_back("caps");
    if(fakeOs.fault=="hid-caps-read")return HIDP_STATUS_INVALID_PREPARSED_DATA;
    *output=fakeOs.caps;
    if(fakeOs.fault=="hid-caps-bounds")output->NumberInputValueCaps=17;
    return HIDP_STATUS_SUCCESS;
}
static NTSTATUS __stdcall fakeButtonCaps(HIDP_REPORT_TYPE,PHIDP_BUTTON_CAPS output,PUSHORT count,PHIDP_PREPARSED_DATA) {
    ++fakeOs.hidCalls;
    fakeOs.apiTrace.push_back("button-caps");
    if(fakeOs.fault=="button-caps-read")return HIDP_STATUS_INVALID_PREPARSED_DATA;
    if(*count<fakeOs.caps.NumberInputButtonCaps)return HIDP_STATUS_BUFFER_TOO_SMALL;
    *count=fakeOs.caps.NumberInputButtonCaps;std::copy_n(fakeOs.buttons.begin(),*count,output);return HIDP_STATUS_SUCCESS;
}
static NTSTATUS __stdcall fakeValueCaps(HIDP_REPORT_TYPE,PHIDP_VALUE_CAPS output,PUSHORT count,PHIDP_PREPARSED_DATA) {
    ++fakeOs.hidCalls;
    fakeOs.apiTrace.push_back("value-caps");
    if(fakeOs.fault=="value-caps-read")return HIDP_STATUS_INVALID_PREPARSED_DATA;
    if(*count<fakeOs.caps.NumberInputValueCaps)return HIDP_STATUS_BUFFER_TOO_SMALL;
    *count=fakeOs.caps.NumberInputValueCaps;std::copy_n(fakeOs.values.begin(),*count,output);
    if(fakeOs.fault=="descriptor-unsupported")output[0].LogicalMax=19;
    return HIDP_STATUS_SUCCESS;
}
static ULONG __stdcall fakeMaxData(HIDP_REPORT_TYPE,PHIDP_PREPARSED_DATA) {
    ++fakeOs.hidCalls;fakeOs.apiTrace.push_back("max-data");return fakeOs.fault=="data-capacity"?1:fakeOs.maxData;
}
#if !defined(YMCC_COMMON_HID_SOURCE_BINDING_FIXTURE)
template<class... Args> static CONFIGRET fakeCfgLeaf(Args&&...) {
    ++fakeOs.cfgCalls;return CR_NO_SUCH_DEVNODE;
}
static bool commonHidFakeSameXusbSource(const std::wstring& raw,const std::wstring& selected) {
    ++fakeOs.bindingCalls;
    fakeOs.apiTrace.push_back("source-binding");
    return fakeOs.fault!="source-binding-unproven"&&raw==fakeOs.rawPath&&selected==U2W(*g_axisCalibrationXinputKeys[0]);
}
#else
// Typed memory ports for the actual interfaceNode/propertyString/source-binding
// functions. No device, registry, SetupAPI or CFG manager is queried.
struct CommonCfgInterface {
    std::wstring path,instance;DEVINST node=0;
    CONFIGRET propertyResult=CR_SUCCESS,locateResult=CR_SUCCESS;
    DEVPROPTYPE type=DEVPROP_TYPE_STRING;
};
struct CommonCfgNode {
    std::wstring instance,service;DEVINST parent=0;
    bool idKnown=true,serviceKnown=true,parentKnown=true;
};
struct CommonCfgModel {
    std::vector<CommonCfgInterface> interfaces;
    std::unordered_map<DEVINST,CommonCfgNode> nodes;
    std::vector<std::string> trace;
    const CommonCfgInterface* lastInterface=nullptr;
};
static CommonCfgModel cfgModel;
static bool cfgKey(const DEVPROPKEY* key,const DEVPROPKEY& expected) {
    return key&&key->pid==expected.pid&&IsEqualGUID(key->fmtid,expected.fmtid);
}
static CONFIGRET cfgText(const std::wstring& value,DEVPROPTYPE* type,PBYTE output,PULONG bytes,DEVPROPTYPE actualType=DEVPROP_TYPE_STRING) {
    const auto required=static_cast<ULONG>((value.size()+1)*sizeof(wchar_t));
    if(!type||!bytes)return CR_INVALID_POINTER;
    *type=actualType;
    if(!output||*bytes<required){*bytes=required;return CR_BUFFER_SMALL;}
    std::copy_n(reinterpret_cast<const BYTE*>(value.c_str()),required,output);*bytes=required;return CR_SUCCESS;
}
static CONFIGRET WINAPI bindingInterfaceProperty(LPCWSTR interfacePath,const DEVPROPKEY* key,
    DEVPROPTYPE* type,PBYTE output,PULONG bytes,ULONG flags) {
    ++fakeOs.cfgCalls;cfgModel.trace.push_back("interface-instance");cfgModel.lastInterface=nullptr;
    if(!interfacePath||flags||!cfgKey(key,DEVPKEY_Device_InstanceId))return CR_INVALID_DATA;
    for(const auto& entry:cfgModel.interfaces)if(entry.path==interfacePath){
        if(entry.propertyResult!=CR_SUCCESS)return entry.propertyResult;
        cfgModel.lastInterface=&entry;return cfgText(entry.instance,type,output,bytes,entry.type);
    }
    return CR_NO_SUCH_DEVNODE;
}
static CONFIGRET WINAPI bindingLocate(PDEVINST node,DEVINSTID_W instance,ULONG flags) {
    ++fakeOs.cfgCalls;cfgModel.trace.push_back("locate");
    const auto* selected=cfgModel.lastInterface;
    if(!node||!instance||flags!=CM_LOCATE_DEVNODE_NORMAL||!selected||
        _wcsicmp(instance,selected->instance.c_str())!=0)return CR_NO_SUCH_DEVNODE;
    if(selected->locateResult!=CR_SUCCESS)return selected->locateResult;
    *node=selected->node;return CR_SUCCESS;
}
static CONFIGRET WINAPI bindingNodeProperty(DEVINST node,const DEVPROPKEY* key,
    DEVPROPTYPE* type,PBYTE output,PULONG bytes,ULONG flags) {
    ++fakeOs.cfgCalls;cfgModel.trace.push_back("service");
    const auto found=cfgModel.nodes.find(node);
    if(flags||!cfgKey(key,DEVPKEY_Device_Service)||found==cfgModel.nodes.end()||!found->second.serviceKnown)return CR_NO_SUCH_DEVNODE;
    return cfgText(found->second.service,type,output,bytes);
}
static CONFIGRET WINAPI bindingDeviceId(DEVINST node,PWSTR output,ULONG capacity,ULONG flags) {
    ++fakeOs.cfgCalls;cfgModel.trace.push_back("device-id");
    const auto found=cfgModel.nodes.find(node);
    if(flags||!output||found==cfgModel.nodes.end()||!found->second.idKnown)return CR_NO_SUCH_DEVNODE;
    const auto& value=found->second.instance;
    if(capacity<=value.size())return CR_BUFFER_SMALL;
    std::copy(value.begin(),value.end(),output);output[value.size()]=L'\0';return CR_SUCCESS;
}
static CONFIGRET WINAPI bindingParent(PDEVINST parent,DEVINST node,ULONG flags) {
    ++fakeOs.cfgCalls;cfgModel.trace.push_back("parent");
    const auto found=cfgModel.nodes.find(node);
    if(flags||!parent||found==cfgModel.nodes.end()||!found->second.parentKnown||!found->second.parent)return CR_NO_SUCH_DEVNODE;
    *parent=found->second.parent;return CR_SUCCESS;
}
#endif
static DWORD WINAPI fakeXinputCapabilities(DWORD slot,DWORD flags,XINPUT_CAPABILITIES* output) {
    fakeOs.apiTrace.push_back("xinput-capabilities");
    if(slot!=0||flags!=XINPUT_FLAG_GAMEPAD||fakeOs.fault=="xinput-capabilities-unavailable")return ERROR_DEVICE_NOT_CONNECTED;
    *output={};output->Type=XINPUT_DEVTYPE_GAMEPAD;
    output->SubType=fakeOs.fault=="xinput-subtype-unsupported"?XINPUT_DEVSUBTYPE_WHEEL:XINPUT_DEVSUBTYPE_GAMEPAD;
    return ERROR_SUCCESS;
}
#define GetRawInputDeviceInfoW fakeRawInfo
#define HidP_GetCaps fakeHidCaps
#define HidP_GetButtonCaps fakeButtonCaps
#define HidP_GetValueCaps fakeValueCaps
#define HidP_MaxDataListLength fakeMaxData
#if defined(YMCC_COMMON_HID_SOURCE_BINDING_FIXTURE)
#define CM_Get_DevNode_PropertyW bindingNodeProperty
#define CM_Get_Device_Interface_PropertyW bindingInterfaceProperty
#define CM_Locate_DevNodeW bindingLocate
#define CM_Get_Device_IDW bindingDeviceId
#define CM_Get_Parent bindingParent
#else
#define CM_Get_DevNode_PropertyW fakeCfgLeaf
#define CM_Get_Device_Interface_PropertyW fakeCfgLeaf
#define CM_Locate_DevNodeW fakeCfgLeaf
#define CM_Get_Device_IDW fakeCfgLeaf
#define CM_Get_Parent fakeCfgLeaf
#endif
#define GetTickCount64 fakeTick

// Observe the existing FSE platform leaf without adding a platform read.
static unsigned commonHidPlatformQueries=0;
namespace ymcc::bootmode {
static Platform commonHidObservedReadPlatform(){++commonHidPlatformQueries;return readPlatform();}
}
#define readPlatform commonHidObservedReadPlatform
