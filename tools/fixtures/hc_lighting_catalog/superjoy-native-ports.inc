// Memory discovery/notification ports for the ORIGINAL native lighting
// fixture. Exact native lifecycle/parent and real OS owner/process/pipe stay intact.
static HWND g_hwnd = reinterpret_cast<HWND>(1);
static std::atomic<bool> g_exitRequested{false};
static std::atomic<std::uint64_t> g_sjFixturePower{7};
static std::atomic<int> g_sjFixturePowerReads{0};
static std::uint64_t currentPowerGeneration(){++g_sjFixturePowerReads;return g_sjFixturePower.load();}
static constexpr GUID kYeManGuidDeviceInterfaceHid = {0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};
struct SuperJoyFixtureCandidate {
    std::wstring path;
    USHORT vendor=0x1C4F,product=2,page=0xFF00,usage=1,input=65,output=65;
    bool readable=true,descriptor=true;
};
static std::vector<SuperJoyFixtureCandidate> g_sjCandidates;
static int g_sjProbes=0,g_sjResumeCalls=0,g_sjSetsDestroyed=0;
static std::function<void()> g_sjDiscoveryMutation;
static HDEVNOTIFY SuperJoyFixtureRegister(HWND,LPVOID,DWORD){return reinterpret_cast<HDEVNOTIFY>(1);}
static BOOL SuperJoyFixtureUnregister(HDEVNOTIFY){return TRUE;}
static HDEVINFO SuperJoyFixtureSet(const GUID*,PCWSTR,HWND,DWORD){return reinterpret_cast<HDEVINFO>(1);}
static BOOL SuperJoyFixtureEnum(HDEVINFO,PSP_DEVINFO_DATA,const GUID*,DWORD index,PSP_DEVICE_INTERFACE_DATA info){
    if(index>=g_sjCandidates.size()){SetLastError(ERROR_NO_MORE_ITEMS);return FALSE;}
    info->Flags=index;return TRUE;
}
static BOOL SuperJoyFixtureDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA info,PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,DWORD size,PDWORD required,PSP_DEVINFO_DATA){
    const auto& c=g_sjCandidates.at(info->Flags);
    const auto bytes=static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath)+(c.path.size()+1)*sizeof(wchar_t));
    if(required)*required=bytes;
    if(!detail||size<bytes){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    std::copy(c.path.begin(),c.path.end(),detail->DevicePath);detail->DevicePath[c.path.size()]=L'\0';
    if(g_sjDiscoveryMutation){auto callback=std::move(g_sjDiscoveryMutation);g_sjDiscoveryMutation={};callback();}
    return TRUE;
}
static BOOL SuperJoyFixtureDestroy(HDEVINFO){++g_sjSetsDestroyed;return TRUE;}
static HANDLE SuperJoyFixtureProbe(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES,DWORD disposition,DWORD,HANDLE){
    ++g_sjProbes;
    if(access!=0||share!=(FILE_SHARE_READ|FILE_SHARE_WRITE)||disposition!=OPEN_EXISTING)throw std::runtime_error("Non-metadata probe");
    for(size_t i=0;i<g_sjCandidates.size();++i)if(g_sjCandidates[i].path==path){
        if(!g_sjCandidates[i].readable){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
        return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x10000+i));
    }
    return INVALID_HANDLE_VALUE;
}
static BOOL SuperJoyFixtureClose(HANDLE handle){
    const auto index=reinterpret_cast<std::uintptr_t>(handle);
    if(index>=0x10000&&index<0x11000)return TRUE;
    return CloseHandle(handle);
}
static BOOL SuperJoyFixtureAttributes(HANDLE h,PHIDD_ATTRIBUTES attrs){
    const auto& c=g_sjCandidates.at(reinterpret_cast<std::uintptr_t>(h)-0x10000);
    attrs->VendorID=c.vendor;attrs->ProductID=c.product;return TRUE;
}
static BOOL SuperJoyFixturePreparsed(HANDLE h,PHIDP_PREPARSED_DATA* data){
    const auto index=reinterpret_cast<std::uintptr_t>(h)-0x10000;
    if(!g_sjCandidates.at(index).descriptor)return FALSE;
    *data=reinterpret_cast<PHIDP_PREPARSED_DATA>(0x20000+index);return TRUE;
}
static NTSTATUS SuperJoyFixtureCaps(PHIDP_PREPARSED_DATA data,PHIDP_CAPS caps){
    const auto& c=g_sjCandidates.at(reinterpret_cast<std::uintptr_t>(data)-0x20000);
    caps->UsagePage=c.page;caps->Usage=c.usage;caps->InputReportByteLength=c.input;caps->OutputReportByteLength=c.output;return HIDP_STATUS_SUCCESS;
}
static BOOL SuperJoyFixtureFree(PHIDP_PREPARSED_DATA){return TRUE;}
static DWORD SuperJoyFixtureResume(HANDLE thread){++g_sjResumeCalls;if(g_sjResumeFail){SetLastError(ERROR_ACCESS_DENIED);return static_cast<DWORD>(-1);}return ResumeThread(thread);}
#define RegisterDeviceNotificationW SuperJoyFixtureRegister
#define UnregisterDeviceNotification SuperJoyFixtureUnregister
#define SetupDiGetClassDevsW SuperJoyFixtureSet
#define SetupDiEnumDeviceInterfaces SuperJoyFixtureEnum
#define SetupDiGetDeviceInterfaceDetailW SuperJoyFixtureDetail
#define SetupDiDestroyDeviceInfoList SuperJoyFixtureDestroy
#define CreateFileW SuperJoyFixtureProbe
#define CloseHandle SuperJoyFixtureClose
#define HidD_GetAttributes SuperJoyFixtureAttributes
#define HidD_GetPreparsedData SuperJoyFixturePreparsed
#define HidD_FreePreparsedData SuperJoyFixtureFree
#define HidP_GetCaps SuperJoyFixtureCaps
