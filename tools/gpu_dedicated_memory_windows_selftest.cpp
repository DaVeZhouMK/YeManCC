// Run the whole production Windows reader and apply/journal path with offline transports.
// No real GPU, registry, device, system-memory or boot query is reachable in this test.
#include <windows.h>
#include <dxgi1_2.h>
#include <dxcore.h>
#include <d3d12.h>
#include <d3dkmthk.h>
#include <setupapi.h>
#include <wrl/client.h>
#include "json.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include "../native/gpu_dedicated_memory_contract.h"
namespace transport {
using Json=nlohmann::json;
using ymcc::dedicatedmemory::mib;
constexpr auto deviceClass="HandheldCompanion.Devices.ClawCG3EM";
const std::wstring defaultRef=LR"({4d36e968-e325-11ce-bfc1-08002be10318}\0003)";
std::wstring ref=defaultRef;
std::wstring driverPath(){return LR"(SYSTEM\CurrentControlSet\Control\Class\)"+ref;}
const std::wstring defaultPnp=LR"(PCI\VEN_8086&DEV_OFFLINE\G3E)";std::wstring pnp=defaultPnp;
const auto parent=reinterpret_cast<HKEY>(std::uintptr_t(0x1001));
const auto gmm=reinterpret_cast<HKEY>(std::uintptr_t(0x1002));
const auto deviceSet=reinterpret_cast<HDEVINFO>(std::uintptr_t(0x2001));
struct Adapter {
    UINT vendor=0x8086,device=0,flags=0; DWORD luid=123; LONG high=0;
    std::wstring name=L"Intel Arc B390 (offline transport)";
    std::optional<D3DKMT_ADAPTERTYPE> kernelType;
    std::uint64_t bytes=4096*mib,sharedBytes=16*1024*mib;
    HRESULT desc=S_OK,byLuid=S_OK,hardwareHr=S_OK,integratedHr=S_OK,driverHr=S_OK;
    std::uint32_t coreLuid=123;
    bool hardware=true,integrated=true;
    std::uint64_t liveVersion=0x0020000000010001ull;
};
struct State {
    std::vector<Adapter> adapters{Adapter{}};
    bool d3dModule=true,d3dEntry=true,kernelTypeRead=true,kernelRender=true,kernelSoftware=false,kernelDiscrete=false,kernelIndirect=false,kernelVirtual=false;
    HRESULT d3dCreate=S_OK,d3dFeature=S_OK,driverCheck=S_OK,d3dRemoved=S_OK;
    BOOL uma=TRUE,cacheCoherentUma=TRUE;UINT nodeCount=1;DWORD d3dLuid=0;LONG d3dHigh=0;
    bool dxgiModule=true,coreModule=true,entry=true,ramRead=true,bootRead=true,bootZero=false;
    HRESULT dxgiFactory=S_OK,coreFactory=S_OK,enumFailure=S_OK;
    DWORD boot=1,bootEnvironment=1;bool sharedBootRead=true,sharedBootShort=false;DWORD sharedMajor=10;USHORT sharedArchitecture=9;std::uint64_t ramKb=32ull*1024*1024;
    bool kmtOpen=true,kmtKey=true,kmtAddress=false,enumComplete=true,bindingConflict=false;
    UINT physicalCount=1;
    LSTATUS parentRead=ERROR_SUCCESS,childRead=ERROR_SUCCESS,valueRead=ERROR_SUCCESS;
    LSTATUS parentWrite=ERROR_SUCCESS,childWrite=ERROR_SUCCESS,createWrite=ERROR_SUCCESS,setWrite=ERROR_SUCCESS,deleteWrite=ERROR_SUCCESS;
    DWORD type=REG_DWORD,raw=4096,valueBytes=sizeof(DWORD);
    bool childMissing=false,valueMissing=false,denyWrite=false,ignoreWrite=false,versionRead=true;
    std::wstring version=L"32.0.offline";
    unsigned writes=0,registryHandles=0,kmtHandles=0,deviceSets=0,modules=0,comObjects=0;
    unsigned integratedQueries=0,d3dCreates=0,probeCalls=0,changeOnProbe=0,fileOpens=0,denyFileOpenAt=0;
    enum class Change {none,config,driver,capacity,readFailure};Change change=Change::none;
    bool asusOpen=true,asusIo=true,asusIgnore=false,asusShort=false;
    DWORD asusReply=1,asusRaw=4,asusError=ERROR_NOT_SUPPORTED;
    unsigned asusHandles=0,asusReads=0,asusWrites=0,asusOpens=0,asusDenyOpenAt=0;
} state;
std::filesystem::path root;
std::vector<std::string> cases;
Json responses=Json::object();
unsigned topologyPermutations=0,uncertainCombinations=0;
void check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);cases.emplace_back(label);}
void balanced(){if(state.registryHandles||state.kmtHandles||state.deviceSets||state.modules||state.comObjects||state.asusHandles)throw std::runtime_error("leaked transport handles");}
void reset(){balanced();state=State{};ref=defaultRef;pnp=defaultPnp;std::filesystem::remove(root/L"gpu-dedicated-memory.json");}
// Minimal IUnknown call surfaces replace only SDK interface names after SDK headers.
// COM ownership and every production method call still run through the real reader.
struct Object: IUnknown {
    ULONG refs=1;
    Object(){++state.comObjects;}
    virtual ~Object(){--state.comObjects;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** value)override{*value=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--refs;if(!left)delete this;return left;}
};
struct __declspec(uuid("94c0b001-7732-4d7e-8bad-000000000001")) DxgiAdapter final:Object {
    std::size_t index;explicit DxgiAdapter(std::size_t i):index(i){}
    HRESULT CheckInterfaceSupport(REFGUID guid,LARGE_INTEGER* version){if(!IsEqualGUID(guid,__uuidof(IDXGIDevice)))throw std::runtime_error("wrong live driver interface");if(FAILED(state.driverCheck))return state.driverCheck;version->QuadPart=static_cast<LONGLONG>(state.adapters.at(index).liveVersion);return S_OK;}
    HRESULT GetDesc1(DXGI_ADAPTER_DESC1* d){const auto& a=state.adapters.at(index);if(FAILED(a.desc))return a.desc;
        *d={};wcscpy_s(d->Description,a.name.c_str());d->VendorId=a.vendor;d->DeviceId=a.device;
        d->Flags=a.flags;d->AdapterLuid.LowPart=a.luid;d->AdapterLuid.HighPart=a.high;d->DedicatedVideoMemory=static_cast<SIZE_T>(a.bytes);
        d->SharedSystemMemory=static_cast<SIZE_T>(a.sharedBytes);return S_OK;}
};
struct __declspec(uuid("94c0b001-7732-4d7e-8bad-000000000002")) DxgiFactory final:Object {
    HRESULT EnumAdapters1(UINT i,DxgiAdapter** out){if(FAILED(state.enumFailure))return state.enumFailure;
        if(i>=state.adapters.size())return DXGI_ERROR_NOT_FOUND;*out=new DxgiAdapter(i);return S_OK;}
};
struct __declspec(uuid("94c0b001-7732-4d7e-8bad-000000000003")) CoreAdapter final:Object {
    std::size_t index;explicit CoreAdapter(std::size_t i):index(i){}
    HRESULT GetProperty(DXCoreAdapterProperty prop,std::size_t size,void* out){const auto& a=state.adapters.at(index);
        if(prop==DXCoreAdapterProperty::IsHardware){if(FAILED(a.hardwareHr))return a.hardwareHr;if(size!=sizeof(bool))throw std::runtime_error("hardware size");memcpy(out,&a.hardware,size);return S_OK;}
        if(prop==DXCoreAdapterProperty::IsIntegrated){++state.integratedQueries;if(FAILED(a.integratedHr))return a.integratedHr;if(size!=sizeof(bool))throw std::runtime_error("integrated size");memcpy(out,&a.integrated,size);return S_OK;}
        if(prop==DXCoreAdapterProperty::DriverVersion){if(FAILED(a.driverHr))return a.driverHr;if(size!=sizeof(a.liveVersion))throw std::runtime_error("version size");memcpy(out,&a.liveVersion,size);return S_OK;}
        throw std::runtime_error("unexpected DXCore property");}
};
struct __declspec(uuid("94c0b001-7732-4d7e-8bad-000000000004")) CoreFactory final:Object {
    HRESULT GetAdapterByLuid(const LUID& luid,REFIID,void** out){for(std::size_t i=0;i<state.adapters.size();++i){const auto& a=state.adapters[i];if(a.coreLuid==luid.LowPart&&a.high==luid.HighPart){if(FAILED(a.byLuid))return a.byLuid;*out=new CoreAdapter(i);return S_OK;}}return E_FAIL;}
};
struct __declspec(uuid("94c0b001-7732-4d7e-8bad-000000000006")) D3dDevice final:Object {
    std::size_t index;explicit D3dDevice(std::size_t i):index(i){}
    LUID GetAdapterLuid(){LUID value{};value.LowPart=state.d3dLuid?state.d3dLuid:state.adapters.at(index).luid;value.HighPart=state.d3dHigh;return value;}
    UINT GetNodeCount(){return state.nodeCount;}
    HRESULT GetDeviceRemovedReason(){return state.d3dRemoved;}
    HRESULT CheckFeatureSupport(D3D12_FEATURE feature,void* out,UINT bytes){if(feature!=D3D12_FEATURE_ARCHITECTURE1||bytes!=sizeof(D3D12_FEATURE_DATA_ARCHITECTURE1))throw std::runtime_error("wrong architecture query");if(FAILED(state.d3dFeature))return state.d3dFeature;
        auto a=static_cast<D3D12_FEATURE_DATA_ARCHITECTURE1*>(out);if(a->NodeIndex!=0)throw std::runtime_error("wrong node index");a->UMA=state.uma;a->CacheCoherentUMA=state.cacheCoherentUma;return S_OK;}
};
HRESULT WINAPI createD3d(IUnknown* adapter,D3D_FEATURE_LEVEL level,REFIID,void** out){
    const auto a=dynamic_cast<DxgiAdapter*>(adapter);if(!a||level!=D3D_FEATURE_LEVEL_11_0)throw std::runtime_error("D3D12 must use the actual bound DXGI adapter");
    ++state.d3dCreates;if(FAILED(state.d3dCreate))return state.d3dCreate;*out=new D3dDevice(a->index);return S_OK;
}
HRESULT WINAPI createDxgi(REFIID,void** out){
    if(++state.probeCalls==state.changeOnProbe){switch(state.change){
        case State::Change::config:state.raw=2048;break;
        case State::Change::driver:state.version=L"33.0.changed-during-save";break;
        case State::Change::capacity:state.adapters[0].bytes=2048*mib;break;
        case State::Change::readFailure:state.adapters[0].desc=E_FAIL;break;
        default:break;
    }}
    if(FAILED(state.dxgiFactory))return state.dxgiFactory;*out=new DxgiFactory;return S_OK;}
HRESULT WINAPI createCore(REFIID,void** out){if(FAILED(state.coreFactory))return state.coreFactory;*out=new CoreFactory;return S_OK;}
LONG WINAPI bootQuery(ULONG info,void* out,ULONG size,ULONG* returned){if(info!=90||size<sizeof(GUID))throw std::runtime_error("unexpected boot query");if(!state.bootRead)return -1;
    const GUID id{state.bootZero?0u:state.bootEnvironment,0,0,{}};memcpy(out,&id,sizeof(id));*returned=sizeof(id);return 0;}
HANDLE WINAPI process(){return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));}
BOOL WINAPI sharedBootMemory(HANDLE owner,LPCVOID address,LPVOID data,SIZE_T size,SIZE_T* count){
    if(owner!=process()||reinterpret_cast<std::uintptr_t>(address)!=0x7ffe0000||size!=0x2c8)throw std::runtime_error("unexpected shared boot data read");
    if(!state.sharedBootRead)return FALSE;memset(data,0,size);const DWORD minor=0;
    memcpy(static_cast<BYTE*>(data)+0x26a,&state.sharedArchitecture,sizeof(state.sharedArchitecture));
    memcpy(static_cast<BYTE*>(data)+0x26c,&state.sharedMajor,sizeof(state.sharedMajor));
    memcpy(static_cast<BYTE*>(data)+0x270,&minor,sizeof(minor));
    memcpy(static_cast<BYTE*>(data)+0x2c4,&state.boot,sizeof(state.boot));
    *count=state.sharedBootShort?size-1:size;return TRUE;
}
HMODULE WINAPI load(LPCWSTR name,HANDLE,DWORD flags){if(flags!=LOAD_LIBRARY_SEARCH_SYSTEM32)throw std::runtime_error("unsafe module search");
    const auto text=std::wstring(name);const bool dxgi=text==L"dxgi.dll",core=text==L"dxcore.dll",d3d=text==L"d3d12.dll";
    if(!dxgi&&!core&&!d3d)throw std::runtime_error("unexpected module");
    if(!(dxgi?state.dxgiModule:core?state.coreModule:state.d3dModule))return nullptr;++state.modules;return reinterpret_cast<HMODULE>(std::uintptr_t(dxgi?1:core?2:4));}
BOOL WINAPI freeModule(HMODULE){--state.modules;return TRUE;}
HMODULE WINAPI module(LPCWSTR name){if(std::wstring(name)!=L"ntdll.dll")throw std::runtime_error("unexpected module handle");return reinterpret_cast<HMODULE>(std::uintptr_t(3));}
FARPROC WINAPI proc(HMODULE m,LPCSTR name){const auto id=reinterpret_cast<std::uintptr_t>(m);
    if(id==3&&std::string(name)=="NtQuerySystemInformation")return reinterpret_cast<FARPROC>(bootQuery);
    if(id==4&&std::string(name)=="D3D12CreateDevice")return state.d3dEntry?reinterpret_cast<FARPROC>(createD3d):nullptr;
    if(!state.entry)return nullptr;
    if(id==1&&std::string(name)=="CreateDXGIFactory1")return reinterpret_cast<FARPROC>(createDxgi);
    if(id==2&&std::string(name)=="DXCoreCreateAdapterFactory")return reinterpret_cast<FARPROC>(createCore);
    throw std::runtime_error("unexpected symbol");}
BOOL WINAPI ram(PULONGLONG kb){*kb=state.ramKb;return state.ramRead?TRUE:FALSE;}
NTSTATUS APIENTRY openAdapter(const D3DKMT_OPENADAPTERFROMLUID* p){if(!state.kmtOpen)return -1;
    for(std::size_t i=0;i<state.adapters.size();++i){const auto& a=state.adapters[i];if(a.luid==p->AdapterLuid.LowPart&&a.high==p->AdapterLuid.HighPart){const_cast<D3DKMT_OPENADAPTERFROMLUID*>(p)->hAdapter=static_cast<UINT>(i+1000);++state.kmtHandles;return 0;}}return -1;}
NTSTATUS APIENTRY closeAdapter(const D3DKMT_CLOSEADAPTER*){--state.kmtHandles;return 0;}
NTSTATUS APIENTRY adapterInfo(const D3DKMT_QUERYADAPTERINFO* p){
    if(p->Type==KMTQAITYPE_ADAPTERTYPE){if(!state.kernelTypeRead)return -1;auto t=static_cast<D3DKMT_ADAPTERTYPE*>(p->pPrivateDriverData);
        const auto& a=state.adapters.at(p->hAdapter-1000);if(a.kernelType){*t=*a.kernelType;return 0;}t->RenderSupported=state.kernelRender?1u:0u;t->SoftwareDevice=state.kernelSoftware?1u:0u;t->HybridDiscrete=state.kernelDiscrete?1u:0u;t->IndirectDisplayDevice=state.kernelIndirect?1u:0u;t->Paravirtualized=state.kernelVirtual?1u:0u;return 0;}
    if(p->Type==KMTQAITYPE_PHYSICALADAPTERCOUNT){static_cast<D3DKMT_PHYSICAL_ADAPTER_COUNT*>(p->pPrivateDriverData)->Count=state.physicalCount;return 0;}
    if(p->Type==KMTQAITYPE_PHYSICALADAPTERPNPKEY){if(!state.kmtKey)return -1;
        const auto text=LR"(\Registry\Machine\SYSTEM\CurrentControlSet\Control\Class\)"+ref;
        auto k=static_cast<D3DKMT_QUERY_PHYSICAL_ADAPTER_PNP_KEY*>(p->pPrivateDriverData);const auto chars=static_cast<UINT>(text.size()+1);
        if(*k->pCchDest<chars)throw std::runtime_error("key buffer too small");memcpy(k->pDest,text.c_str(),chars*sizeof(wchar_t));*k->pCchDest=chars;return 0;}
    if(p->Type==KMTQAITYPE_ADAPTERADDRESS){if(!state.kmtAddress)return -1;auto a=static_cast<D3DKMT_ADAPTERADDRESS*>(p->pPrivateDriverData);a->BusNumber=0;a->DeviceNumber=2;a->FunctionNumber=0;return 0;}
    throw std::runtime_error("unexpected KMT request");}
HDEVINFO WINAPI devices(const GUID*,PCWSTR,HWND,DWORD flags){if(flags!=DIGCF_PRESENT)throw std::runtime_error("not present devices");++state.deviceSets;return deviceSet;}
BOOL WINAPI enumerate(HDEVINFO set,DWORD i,PSP_DEVINFO_DATA node){if(set!=deviceSet)throw std::runtime_error("wrong devices");if(i){SetLastError(state.enumComplete?ERROR_NO_MORE_ITEMS:ERROR_ACCESS_DENIED);return FALSE;}node->DevInst=0;return TRUE;}
BOOL WINAPI destroy(HDEVINFO set){if(set!=deviceSet)throw std::runtime_error("wrong destroy");--state.deviceSets;return TRUE;}
BOOL WINAPI property(HDEVINFO,PSP_DEVINFO_DATA,DWORD prop,PDWORD type,PBYTE out,DWORD bytes,PDWORD required){
    if(prop==SPDRP_DRIVER){const auto size=static_cast<DWORD>((ref.size()+1)*sizeof(wchar_t));if(bytes<size)return FALSE;memcpy(out,ref.c_str(),size);*type=REG_SZ;if(required)*required=size;return TRUE;}
    if(!state.kmtAddress)return FALSE;const DWORD n=prop==SPDRP_BUSNUMBER?0u:((state.bindingConflict?3u:2u)<<16);
    if(prop!=SPDRP_BUSNUMBER&&prop!=SPDRP_ADDRESS)throw std::runtime_error("unexpected device property");if(bytes<sizeof(n))return FALSE;memcpy(out,&n,sizeof(n));*type=REG_DWORD;if(required)*required=sizeof(n);return TRUE;}
BOOL WINAPI instance(HDEVINFO,PSP_DEVINFO_DATA,PWSTR out,DWORD chars,PDWORD required){const auto size=static_cast<DWORD>(pnp.size()+1);if(chars<size)return FALSE;memcpy(out,pnp.c_str(),size*sizeof(wchar_t));if(required)*required=size;return TRUE;}
LSTATUS WINAPI openKey(HKEY base,LPCWSTR name,DWORD,REGSAM access,PHKEY out){if(!(access&KEY_WOW64_64KEY))throw std::runtime_error("registry view");
    if(base==HKEY_LOCAL_MACHINE&&std::wstring(name)==driverPath()){if(state.parentRead!=ERROR_SUCCESS)return state.parentRead;if((access&KEY_CREATE_SUB_KEY)&&state.parentWrite!=ERROR_SUCCESS)return state.parentWrite;*out=parent;}
    else if(base==parent&&std::wstring(name)==L"GMM"){if(state.childMissing)return ERROR_FILE_NOT_FOUND;if(state.childRead!=ERROR_SUCCESS)return state.childRead;if(access&KEY_SET_VALUE){if(state.denyWrite)return ERROR_ACCESS_DENIED;if(state.childWrite!=ERROR_SUCCESS)return state.childWrite;}*out=gmm;}
    else throw std::runtime_error("wrong registry node");++state.registryHandles;return ERROR_SUCCESS;}
LSTATUS WINAPI value(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE out,LPDWORD size){
    if(key==parent&&std::wstring(name)==L"DriverVersion"){if(!state.versionRead)return ERROR_FILE_NOT_FOUND;const auto bytes=static_cast<DWORD>((state.version.size()+1)*sizeof(wchar_t));*type=REG_SZ;if(out&&*size<bytes)return ERROR_MORE_DATA;if(out)memcpy(out,state.version.c_str(),bytes);*size=bytes;return ERROR_SUCCESS;}
    if(key==gmm&&std::wstring(name)==L"DedicatedSegmentSize"){if(state.valueMissing)return ERROR_FILE_NOT_FOUND;if(state.valueRead!=ERROR_SUCCESS)return state.valueRead;*type=state.type;const auto original=*size;*size=state.valueBytes;if(!out||original<state.valueBytes)return ERROR_MORE_DATA;memcpy(out,&state.raw,std::min<DWORD>(sizeof(DWORD),state.valueBytes));return ERROR_SUCCESS;}
    throw std::runtime_error("unexpected registry read");}
LSTATUS WINAPI closeKey(HKEY key){if(key!=parent&&key!=gmm)throw std::runtime_error("wrong close");--state.registryHandles;return ERROR_SUCCESS;}
LSTATUS WINAPI createKey(HKEY base,LPCWSTR name,DWORD,LPWSTR,DWORD,REGSAM access,const LPSECURITY_ATTRIBUTES,PHKEY out,LPDWORD){
    if(base!=parent||std::wstring(name)!=L"GMM"||!(access&KEY_SET_VALUE)||!(access&KEY_WOW64_64KEY))throw std::runtime_error("wrong create");if(state.denyWrite)return ERROR_ACCESS_DENIED;if(state.createWrite!=ERROR_SUCCESS)return state.createWrite;state.childMissing=false;*out=gmm;++state.registryHandles;return ERROR_SUCCESS;}
LSTATUS WINAPI setValue(HKEY key,LPCWSTR name,DWORD,DWORD type,const BYTE* out,DWORD size){if(key!=gmm||std::wstring(name)!=L"DedicatedSegmentSize"||type!=REG_DWORD||size!=sizeof(DWORD))throw std::runtime_error("wrong write");
    if(state.denyWrite)return ERROR_ACCESS_DENIED;if(state.setWrite!=ERROR_SUCCESS)return state.setWrite;++state.writes;if(!state.ignoreWrite){memcpy(&state.raw,out,size);state.valueMissing=false;state.type=type;}return ERROR_SUCCESS;}
LSTATUS WINAPI deleteValue(HKEY key,LPCWSTR name){if(key!=gmm||std::wstring(name)!=L"DedicatedSegmentSize")throw std::runtime_error("wrong delete");if(state.denyWrite)return ERROR_ACCESS_DENIED;if(state.deleteWrite!=ERROR_SUCCESS)return state.deleteWrite;if(state.valueMissing)return ERROR_FILE_NOT_FOUND;++state.writes;if(!state.ignoreWrite)state.valueMissing=true;return ERROR_SUCCESS;}
HANDLE WINAPI file(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templ){
    if(std::wstring(name)==L"\\\\.\\ATKACPI"){
        if(++state.asusOpens==state.asusDenyOpenAt||!state.asusOpen){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
        if(access!=(GENERIC_READ|GENERIC_WRITE)||share!=(FILE_SHARE_READ|FILE_SHARE_WRITE)||creation!=OPEN_EXISTING)throw std::runtime_error("wrong ASUS open");
        ++state.asusHandles;return reinterpret_cast<HANDLE>(std::uintptr_t(0x4001));}
    const auto target=std::filesystem::path(name).lexically_normal();if(target!=root/L"gpu-dedicated-memory.json.tmp")throw std::runtime_error("unexpected file/device open");
    if(++state.fileOpens==state.denyFileOpenAt){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    return ::CreateFileW(name,access,share,security,creation,flags,templ);}
BOOL WINAPI closeFile(HANDLE handle){
    if(handle==reinterpret_cast<HANDLE>(std::uintptr_t(0x4001))){--state.asusHandles;return TRUE;}
    return ::CloseHandle(handle);}
BOOL WINAPI control(HANDLE handle,DWORD code,LPVOID input,DWORD inputBytes,LPVOID output,DWORD outputBytes,LPDWORD returned,LPOVERLAPPED){
    if(handle!=reinterpret_cast<HANDLE>(std::uintptr_t(0x4001))||code!=0x0022240c||inputBytes<16)throw std::runtime_error("unexpected hardware IOCTL");
    const auto words=static_cast<const std::uint32_t*>(input);
    if(words[2]!=0x000600c4)throw std::runtime_error("unexpected ASUS memory endpoint");
    if(!state.asusIo){SetLastError(state.asusError);return FALSE;}
    if(words[0]==0x53545344){
        ++state.asusReads;if(inputBytes!=20||outputBytes!=64)throw std::runtime_error("wrong VRAM read packet");
        std::array<std::uint8_t,16> bytes{};bytes[0]=3;bytes[2]=3;bytes[4]=static_cast<std::uint8_t>(state.asusRaw);bytes[8]=4;bytes[10]=8;
        memcpy(output,bytes.data(),bytes.size());*returned=state.asusShort?2:static_cast<DWORD>(bytes.size());return TRUE;}
    if(words[0]==0x53564544){
        ++state.asusWrites;if(inputBytes!=16||outputBytes!=16)throw std::runtime_error("wrong VRAM write packet");
        memcpy(output,&state.asusReply,sizeof(state.asusReply));*returned=state.asusShort?2:sizeof(state.asusReply);
        if(state.asusReply==1&&!state.asusIgnore&&!state.asusShort)state.asusRaw=words[3];return TRUE;}
    throw std::runtime_error("unexpected ASUS method");}
}
#define IDXGIFactory1 transport::DxgiFactory
#define IDXGIAdapter1 transport::DxgiAdapter
#define IDXCoreAdapterFactory transport::CoreFactory
#define IDXCoreAdapter transport::CoreAdapter
#define ID3D12Device transport::D3dDevice
#define LoadLibraryExW transport::load
#define FreeLibrary transport::freeModule
#define GetModuleHandleW transport::module
#define GetProcAddress transport::proc
#define GetPhysicallyInstalledSystemMemory transport::ram
#define D3DKMTOpenAdapterFromLuid transport::openAdapter
#define D3DKMTCloseAdapter transport::closeAdapter
#define D3DKMTQueryAdapterInfo transport::adapterInfo
#define SetupDiGetClassDevsW transport::devices
#define SetupDiEnumDeviceInfo transport::enumerate
#define SetupDiDestroyDeviceInfoList transport::destroy
#define SetupDiGetDeviceRegistryPropertyW transport::property
#define SetupDiGetDeviceInstanceIdW transport::instance
#define RegOpenKeyExW transport::openKey
#define RegQueryValueExW transport::value
#define RegCloseKey transport::closeKey
#define RegCreateKeyExW transport::createKey
#define RegSetValueExW transport::setValue
#define RegDeleteValueW transport::deleteValue
#define GetCurrentProcess transport::process
#define ReadProcessMemory transport::sharedBootMemory
#define CreateFileW transport::file
#define DeviceIoControl transport::control
#define CloseHandle transport::closeFile
#include "../native/gpu_dedicated_memory_runtime.h"
using namespace ymcc::dedicatedmemory;
int main(int argc,char** argv){try{
    using namespace transport;
    if(argc!=3)throw std::runtime_error("pass an isolated output directory and captured topology fixture");
    Json captured;{std::ifstream input{std::filesystem::path(wide(argv[2]))};input>>captured;}
    if(captured.at("schema")!=1||captured.at("deviceClass")!=deviceClass||captured.at("adapters").size()!=2)throw std::runtime_error("invalid captured G3E fixture");
    root=std::filesystem::absolute(wide(argv[1])).lexically_normal()/L"journal";
    std::filesystem::create_directories(root);
    // Legacy Intel transport is exercised below the public support policy. These
    // diagnostic candidate responses are deliberately inadmissible in the real UI.
    const auto readNow=[](){Json diagnostics;WindowsOps ops(root.wstring(),deviceClass,&diagnostics);auto j=resultJson(read(ops));j["readDiagnostics"]=std::move(diagnostics);balanced();return j;};
    const auto hidden=[&](const char* stage,const char* label){const auto r=readNow();check(!r.at("ok").get<bool>()&&!r.at("available").get<bool>()&&r.at("reason").get<std::string>().find(stage)!=std::string::npos,label);responses[label]=r;};
    const auto applyNow=[&](const Json& before,const char* option,bool confirmed=true){Json dispatch;WindowsOps ops(root.wstring(),deviceClass,nullptr,&dispatch);const auto applied=apply(ops,before.at("fence").get<std::string>(),option,confirmed);auto r=writeResultJson(applied,ops,std::move(dispatch));balanced();return r;};
    reset();state.raw=8192;state.adapters[0].bytes=128*mib;
    const auto publicIntel=runtimeRead(root.wstring(),deviceClass);balanced();
    check(publicIntel.at("ok")&&!publicIntel.at("available").get<bool>()&&publicIntel.at("settingRoute")=="read-only"&&publicIntel.at("dedicatedBytes")==128*mib&&publicIntel.at("selected")=="mib:8192","public G3E read retains actual capacity and configuration diagnostics but hides unconfirmed setter");responses["public-g3e-read-only"]=publicIntel;
    check(publicIntel["readDiagnostics"]["settingRoute"]["allowed"]==false&&state.writes==0,"machine capability records unsupported Intel route without mutation");
    const auto publicIntelWrite=runtimeApply(root.wstring(),deviceClass,Json{{"fence",publicIntel.at("fence")},{"option","mib:8192"},{"confirmed",true}});balanced();
    check(!publicIntelWrite.at("ok").get<bool>()&&!publicIntelWrite.at("available").get<bool>()&&state.writes==0&&state.asusWrites==0&&!std::filesystem::exists(root/L"gpu-dedicated-memory.json"),"direct public IPC cannot bypass Intel support gate or persist a write intent");responses["public-g3e-write-blocked"]=publicIntelWrite;
    reset();const auto renamedIntel=runtimeRead(root.wstring(),"HandheldCompanion.Devices.ROGAllyX");
    check(!renamedIntel.at("available").get<bool>()&&renamedIntel.at("provider")=="intel-gmm"&&state.asusReads==0,"known ROG class cannot grant Intel registry setter support");responses["public-intel-rog-class-blocked"]=renamedIntel;
    reset();const auto initial=readNow();check(initial.at("ok")&&initial.at("available")&&initial.at("dedicatedBytes")==4096*mib&&initial.at("selected")=="mib:4096","G3E candidate reader retains exact current capacity below the public setting policy");responses["initial"]=initial;
    check(initial.at("pnpId")==utf8(pnp)&&initial.at("bindingSource")=="kernel-pnp"&&state.writes==0,"reader uses bound driver 0003 and never mutates registry");
    check(initial.at("options").size()==10,"32 GiB installed RAM bounds Intel offered capacities");
    check(initial.at("readerRevision")==readerRevision,"actual runtime response carries its compiled reader revision");
    // Independently constructed prior configuration: no YMCC journal, numeric target already stored,
    // yet real capacity differs. This must not be confused with the earlier hidden-adapter bug.
    reset();state.raw=8192;state.adapters[0].bytes=128*mib;const auto configuredMismatch=readNow();
    check(configuredMismatch.at("ok")&&configuredMismatch.at("available")&&configuredMismatch.at("selected")=="mib:8192"&&configuredMismatch.at("dedicatedBytes")==128*mib&&configuredMismatch.at("verification")=="unconfirmed"&&!configuredMismatch.contains("pendingOption"),"preexisting 8 GiB registry target with real 128 MiB and no journal is explicitly unconfirmed");responses["configured8-actual128-initial"]=configuredMismatch;
    check(state.writes==0&&!std::filesystem::exists(root/L"gpu-dedicated-memory.json"),"read of preexisting discrepancy has no automatic write or journal mutation");
    const auto mismatchNoConfirm=applyNow(configuredMismatch,"mib:8192",false);
    check(!mismatchNoConfirm.at("ok").get<bool>()&&state.writes==0,"preexisting same-target discrepancy still requires confirmation");
    const auto mismatchPending=applyNow(configuredMismatch,"mib:8192");
    check(mismatchPending.at("ok")&&state.writes==1&&mismatchPending.at("selected")=="mib:8192"&&mismatchPending.at("verification")=="pending_restart"&&mismatchPending.at("dedicatedBytes")==128*mib,"explicit preexisting target reapply uses production write and independent config readback without inventing capacity");responses["configured8-actual128-pending"]=mismatchPending;
    {Json journal;std::ifstream input(root/L"gpu-dedicated-memory.json");input>>journal;
     check(journal.at("stage")=="saved"&&journal.at("originalRaw")==8192&&journal.at("beforeBytes")==128*mib,"reapplied same target saves original configuration and actual capacity in production journal");}
    const auto mismatchRepeated=applyNow(mismatchPending,"mib:8192");
    check(mismatchRepeated.at("ok")&&state.writes==1&&mismatchRepeated.at("verification")=="pending_restart","same boot pending reapply cannot create a registry write loop");
    state.boot=2;const auto mismatchIgnored=readNow();
    check(mismatchIgnored.at("verification")=="not_effective"&&mismatchIgnored.at("dedicatedBytes")==128*mib,"preexisting target unchanged after new boot remains unconfirmed");responses["configured8-actual128-ignored"]=mismatchIgnored;
    check(mismatchIgnored.at("bootComparison")=="new_boot"&&mismatchIgnored.at("bootId")!=mismatchIgnored.at("savedBootId"),"stable environment GUID and incremented loader counter provide real restart evidence");
    const auto mismatchBlocked=applyNow(mismatchIgnored,"mib:8192");
    check(!mismatchBlocked.at("ok").get<bool>()&&state.writes==1&&mismatchBlocked.at("verification")=="not_effective","ineffective same target is rejected before another production registry write");responses["configured8-actual128-blocked"]=mismatchBlocked;
    state.adapters[0].bytes=8192*mib;const auto mismatchVerified=readNow();
    check(mismatchVerified.at("verification")=="verified"&&mismatchVerified.at("dedicatedBytes")==8192*mib,"preexisting target verifies only after exact dedicated capacity on a new boot");responses["configured8-actual128-verified"]=mismatchVerified;
    const auto mismatchEffectiveNoop=applyNow(mismatchVerified,"mib:8192");
    check(mismatchEffectiveNoop.at("ok")&&state.writes==1,"already effective same target retains idempotent production behavior");
    reset();state.raw=8192;state.adapters[0].bytes=128*mib;auto mismatchBeforeFailure=readNow();state.denyFileOpenAt=1;
    check(!applyNow(mismatchBeforeFailure,"mib:8192").at("ok").get<bool>()&&state.writes==0,"preexisting same-target reapply cannot bypass original-record failure");
    reset();state.raw=8192;state.adapters[0].bytes=128*mib;mismatchBeforeFailure=readNow();state.denyWrite=true;
    check(!applyNow(mismatchBeforeFailure,"mib:8192").at("ok").get<bool>()&&state.writes==0,"matching existing registry value cannot turn denied reapply into success");
    reset();state.raw=8192;state.adapters[0].bytes=128*mib;mismatchBeforeFailure=readNow();state.changeOnProbe=3;state.change=State::Change::capacity;
    check(!applyNow(mismatchBeforeFailure,"mib:8192").at("ok").get<bool>()&&state.writes==0,"same-target reapply must still pass final live fence immediately before write");
    reset();state.adapters[0].byLuid=E_INVALIDARG;auto umaFallback=readNow();check(umaFallback.at("available")&&umaFallback.at("dedicatedBytes")==4096*mib&&umaFallback.at("classificationSource")=="d3d12-cache-coherent-uma","G3E E_INVALIDARG uses exact DXGI adapter with positive kernel hardware and coherent UMA proof");responses["intel-uma-fallback"]=umaFallback;
    check(umaFallback.at("readDiagnostics")["adapters"][0]["dxcoreLuidHr"]==static_cast<std::uint32_t>(E_INVALIDARG),"ordinary result retains direct DXCore failure alongside independent successful proof");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.d3dLuid=999;hidden("intel.uma-identity","independent device LUID low-part mismatch stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.d3dHigh=1;hidden("intel.uma-identity","independent device LUID high-part mismatch stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.nodeCount=2;hidden("intel.uma-identity","independent multi-node adapter stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.uma=FALSE;hidden("intel.uma-identity","Intel discrete architecture never enables GMM fallback");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.cacheCoherentUma=FALSE;hidden("intel.uma-identity","UMA without CPU GPU cache-coherence proof stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.kernelSoftware=true;hidden("adapter.none","kernel software adapter cannot use UMA fallback");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.kernelRender=false;hidden("adapter.none","kernel adapter without render support cannot use UMA fallback");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.kernelDiscrete=true;hidden("adapter.none","kernel discrete flag conflicts with fallback admission");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.kernelTypeRead=false;hidden("intel.uma-identity","unknown kernel hardware type cannot use UMA fallback");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.kernelIndirect=true;hidden("adapter.none","indirect display adapter cannot use physical UMA fallback");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.kernelVirtual=true;hidden("adapter.none","paravirtualized adapter cannot use physical UMA fallback");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.d3dModule=false;hidden("intel.uma-identity","unavailable system D3D12 library stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.d3dEntry=false;hidden("intel.uma-identity","unavailable D3D12 create entry stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.d3dCreate=E_FAIL;hidden("intel.uma-identity","D3D12 device creation failure stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.d3dFeature=E_FAIL;hidden("intel.uma-identity","D3D12 architecture read failure stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.driverCheck=E_FAIL;hidden("intel.uma-identity","missing live DXGI driver version stays hidden");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.adapters[0].liveVersion=0;hidden("intel.uma-identity","empty live DXGI driver version stays hidden");
    reset();state.adapters[0].byLuid=DXGI_ERROR_DEVICE_REMOVED;hidden("dxcore.luid","removed DXCore adapter never attempts an admission fallback");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.d3dRemoved=DXGI_ERROR_DEVICE_REMOVED;hidden("intel.uma-identity","removed D3D12 device cannot confirm current UMA identity");
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.adapters[0].bytes=0;auto umaZero=readNow();check(umaZero.at("available")&&umaZero.at("capacityRead")==true&&umaZero.at("dedicatedBytes")==0,"coherent UMA route preserves genuinely read zero capacity");responses["intel-uma-zero"]=umaZero;
    reset();state.adapters[0].byLuid=E_INVALIDARG;state.raw=9000;hidden("intel.config.unknown","coherent UMA route still rejects unknown DedicatedSegmentSize");
    reset();state.adapters[0].byLuid=E_INVALIDARG;auto umaBefore=readNow();
    auto umaPending=applyNow(umaBefore,"mib:8192");check(umaPending.at("ok")&&umaPending.at("verification")=="pending_restart"&&umaPending.at("dedicatedBytes")==4096*mib,"coherent UMA save independently reads configuration and keeps actual capacity pending");responses["intel-uma-pending"]=umaPending;
    state.boot=2;check(readNow().at("verification")=="not_effective","coherent UMA new boot with ignored driver setting stays unconfirmed");
    state.adapters[0].bytes=8192*mib;auto umaVerified=readNow();check(umaVerified.at("verification")=="verified","coherent UMA exact new boot capacity verifies actual driver adoption");responses["intel-uma-verified"]=umaVerified;
    reset();state.adapters[0].byLuid=E_INVALIDARG;umaBefore=readNow();state.adapters[0].byLuid=S_OK;
    auto routeDrift=applyNow(umaBefore,"mib:8192");check(!routeDrift.at("ok").get<bool>()&&state.writes==0,"classification route change invalidates prewrite snapshot fence");
    // Replay the topology that actually hid G3E controls on 2026-10-14.
    // Configuration is an explicitly valid offline substitute; the log did not expose its value.
    const auto replay=[&](bool indirectFirst){
        reset();state.adapters.clear();state.valueMissing=true;state.kmtAddress=true;
        for(const auto& row:captured.at("adapters")){
            Adapter a;a.name=wide(row.at("adapterName"));a.vendor=row.at("vendorId");a.device=row.at("deviceId");a.flags=row.at("flags");
            a.bytes=row.at("dedicatedBytes");a.sharedBytes=row.at("sharedSystemBytes");
            const auto luid=row.at("luid").get<std::string>();a.high=static_cast<LONG>(std::stoul(luid.substr(0,8),nullptr,16));a.luid=std::stoul(luid.substr(9),nullptr,16);a.coreLuid=a.luid;
            a.byLuid=static_cast<HRESULT>(row.at("dxcoreLuidHr").get<std::uint32_t>());
            if(row.contains("isHardware"))a.hardware=row.at("isHardware");if(row.contains("isIntegrated"))a.integrated=row.at("isIntegrated");
            if(row.contains("kernelAdapterType")){const auto& k=row.at("kernelAdapterType");D3DKMT_ADAPTERTYPE t{};
                t.RenderSupported=k.at("renderSupported").get<bool>();t.SoftwareDevice=k.at("softwareDevice").get<bool>();t.HybridDiscrete=k.at("hybridDiscrete").get<bool>();t.IndirectDisplayDevice=k.at("indirectDisplay").get<bool>();t.Paravirtualized=k.at("paravirtualized").get<bool>();a.kernelType=t;
            }else{const auto key=wide(row.at("kernelSoftwareKey"));const auto pos=key.find(L"{4d36e968");if(pos==std::wstring::npos)throw std::runtime_error("missing captured driver reference");ref=key.substr(pos);}
            state.adapters.push_back(a);
        }
        if(indirectFirst)std::reverse(state.adapters.begin(),state.adapters.end());
    };
    replay(false);const auto dual=readNow();
    {std::ofstream output(root.parent_path()/L"g3e-log-replay.json");output<<dual.dump(2);}
    check(dual.at("available")&&dual.at("dedicatedBytes")==128*mib&&dual.at("luid")==captured.at("adapters").at(0).at("luid"),"captured G3E physical B390 remains available after same-name indirect display");responses["g3e-indirect-after"]=dual;
    check(dual.at("fence").get<std::string>().find(utf8(driverPath()))!=std::string::npos&&dual.at("readDiagnostics").at("adapters").at(0).at("kernelSoftwareKey").get<std::string>().ends_with("0001")&&dual.at("classificationSource")=="dxcore","captured physical adapter alone supplies GMM driver identity and classification");
    check(state.d3dCreates==0&&state.writes==0,"captured nonrender indirect display needs neither D3D12 creation nor a registry mutation");
    replay(true);const auto reversed=readNow();check(reversed.at("available")&&reversed.at("fence")==dual.at("fence"),"captured indirect display before physical B390 preserves same write fence");responses["g3e-indirect-first"]=reversed;
    const auto excluded=dual.at("readDiagnostics").at("adapters").at(1);
    check(excluded.at("luid")==captured.at("adapters").at(1).at("luid")&&excluded.at("disposition")=="excluded"&&excluded.at("exclusionReason")=="kernel-adapter-type"&&excluded.at("kernelAdapterType").at("indirectDisplay")==true,"ordinary capability diagnostics retain the excluded indirect LUID and positive kernel proof");
    for(unsigned flag=0;flag<5;++flag){replay(false);D3DKMT_ADAPTERTYPE type{};type.RenderSupported=1;
        switch(flag){case 0:type.RenderSupported=0;break;case 1:type.SoftwareDevice=1;break;case 2:type.HybridDiscrete=1;break;case 3:type.IndirectDisplayDevice=1;break;default:type.Paravirtualized=1;break;}
        state.adapters[1].kernelType=type;const auto r=readNow();check(r.at("available")&&r.at("fence")==dual.at("fence")&&state.d3dCreates==0,"each confirmed kernel-ineligible adapter cannot veto the same valid physical candidate");
    }
    replay(false);state.adapters.erase(state.adapters.begin());hidden("adapter.none","indirect display alone cannot expose dedicated memory controls");
    replay(false);state.adapters[1].kernelType.reset();state.kernelTypeRead=false;hidden("intel.uma-identity","unknown second adapter type still prevents an incomplete admission");
    replay(false);state.adapters[1].byLuid=DXGI_ERROR_DEVICE_REMOVED;hidden("dxcore.luid","removed second adapter is never silently excluded");
    replay(false);state.adapters[1].kernelType.reset();state.d3dCreate=DXGI_ERROR_UNSUPPORTED;hidden("intel.uma-identity","D3D12 unsupported on a rendering second adapter is a read failure rather than proven exclusion");
    replay(false);state.adapters[1].kernelType.reset();state.cacheCoherentUma=FALSE;hidden("intel.uma-identity","uncertain second memory architecture remains unconfirmed despite a valid first candidate");
    replay(false);const auto dualBefore=readNow();const auto dualPending=applyNow(dualBefore,"mib:8192");
    check(dualPending.at("ok")&&dualPending.at("verification")=="pending_restart"&&dualPending.at("dedicatedBytes")==128*mib&&state.writes==1,"captured topology save targets physical driver only and keeps real 128 MiB until reboot");responses["g3e-indirect-pending"]=dualPending;
    state.boot=2;const auto dualIgnored=readNow();check(dualIgnored.at("verification")=="not_effective"&&dualIgnored.at("dedicatedBytes")==128*mib,"captured topology never claims 8 GiB when driver still reports 128 MiB");responses["g3e-indirect-not-effective"]=dualIgnored;
    state.adapters[0].bytes=8192*mib;const auto dualVerified=readNow();check(dualVerified.at("verification")=="verified"&&dualVerified.at("dedicatedBytes")==8192*mib,"captured topology verifies only exact physical capacity after a new boot");responses["g3e-indirect-verified"]=dualVerified;
    // A confirmed nonhardware adapter has no need to expose IsIntegrated at all.
    reset();Adapter irrelevant;irrelevant.luid=77;irrelevant.coreLuid=77;irrelevant.hardware=false;irrelevant.integratedHr=E_NOTIMPL;state.adapters.insert(state.adapters.begin(),irrelevant);
    auto nonhardware=readNow();check(nonhardware.at("available")&&nonhardware.at("luid")==initial.at("luid"),"confirmed nonhardware Intel adapter must not hide a later G3E");responses["nonhardware-first"]=nonhardware;
    reset();Adapter discrete;discrete.luid=78;discrete.coreLuid=78;discrete.integrated=false;discrete.driverHr=E_FAIL;state.adapters.insert(state.adapters.begin(),discrete);check(readNow().at("available"),"confirmed discrete Intel adapter does not block G3E");
    {
    // Design-derived combinations: these adapters are constructed independently of user logs.
    // Exhaust all orders, including same-name aliases and equal low LUID parts with different high parts.
    reset();state.adapters[0].bytes=128*mib;state.valueMissing=true;const auto matrixBaseline=readNow();
    std::vector<Adapter> topology{state.adapters[0]};
    Adapter matrixAlias=topology[0];matrixAlias.luid=123;matrixAlias.coreLuid=123;matrixAlias.high=1;topology.push_back(matrixAlias);
    Adapter software;software.luid=78;software.coreLuid=78;software.flags=DXGI_ADAPTER_FLAG_SOFTWARE;software.byLuid=E_FAIL;topology.push_back(software);
    Adapter matrixNonhardware;matrixNonhardware.luid=79;matrixNonhardware.coreLuid=79;matrixNonhardware.hardware=false;matrixNonhardware.integratedHr=E_FAIL;topology.push_back(matrixNonhardware);
    Adapter matrixDiscrete;matrixDiscrete.luid=80;matrixDiscrete.coreLuid=80;matrixDiscrete.integrated=false;matrixDiscrete.driverHr=E_FAIL;topology.push_back(matrixDiscrete);
    Adapter indirect;indirect.luid=81;indirect.coreLuid=81;indirect.byLuid=E_INVALIDARG;D3DKMT_ADAPTERTYPE indirectType{};indirectType.IndirectDisplayDevice=1;indirect.kernelType=indirectType;topology.push_back(indirect);
    Adapter otherVendor;otherVendor.luid=82;otherVendor.coreLuid=82;otherVendor.vendor=0x10de;otherVendor.byLuid=E_FAIL;topology.push_back(otherVendor);
    std::array<std::size_t,7> order{0,1,2,3,4,5,6};
    do{reset();state.valueMissing=true;state.adapters.clear();for(const auto index:order)state.adapters.push_back(topology[index]);
        const auto r=readNow();if(!r.at("available").get<bool>()||r.at("fence")!=matrixBaseline.at("fence")||r.at("dedicatedBytes")!=128*mib||state.writes||state.d3dCreates)throw std::runtime_error("independent topology permutation changed physical readback or admission");++topologyPermutations;
    }while(std::next_permutation(order.begin(),order.end()));
    check(topologyPermutations==5040,"all 5040 orders of physical aliases, software, matrixNonhardware, matrixDiscrete, indirect and unsupported-vendor adapters preserve the same real capacity and write fence");
    for(unsigned error=0;error<6;++error){for(unsigned position=0;position<3;++position){
        reset();state.valueMissing=true;Adapter uncertain;uncertain.luid=90;uncertain.coreLuid=90;
        switch(error){case 0:uncertain.desc=E_FAIL;break;case 1:uncertain.byLuid=E_FAIL;break;case 2:uncertain.hardwareHr=E_FAIL;break;case 3:uncertain.integratedHr=E_FAIL;break;case 4:uncertain.driverHr=E_FAIL;break;default:uncertain.liveVersion=0;break;}
        state.adapters={topology[0],topology[5]};state.adapters.insert(state.adapters.begin()+position,uncertain);
        const auto r=readNow();if(r.at("available").get<bool>()||r.at("ok").get<bool>()||state.writes)throw std::runtime_error("uncertain adapter combination was incorrectly admitted");++uncertainCombinations;
    }}
    check(uncertainCombinations==18,"unknown capacity, LUID, hardware, architecture or driver version veto admission before between and after valid plus excluded adapters");
    }
    reset();state.adapters[0].bytes=0;auto zero=readNow();check(zero.at("available")&&zero.at("capacityRead")==true&&zero.at("dedicatedBytes")==0,"successful GetDesc1 zero is genuine Intel data");responses["zero"]=zero;
    reset();state.adapters[0].desc=E_FAIL;hidden("dxgi.capacity","failed GetDesc1 stays hidden rather than becoming zero");
    check(readNow().at("readDiagnostics").at("adapters").empty(),"failed GetDesc1 does not fabricate partial capacity diagnostics");
    reset();state.dxgiModule=false;hidden("runtime.module","missing DXGI module fails closed");
    reset();state.coreModule=false;hidden("runtime.module","missing DXCore module fails closed");
    reset();state.entry=false;hidden("runtime.entry","missing factory entry fails closed");
    reset();state.dxgiFactory=E_FAIL;hidden("dxgi.factory","DXGI factory failure stays hidden");
    reset();state.coreFactory=E_FAIL;hidden("dxcore.factory","DXCore factory failure stays hidden");
    reset();state.ramRead=false;hidden("system.ram","installed RAM failure stays hidden");
    reset();state.ramKb=UINT64_MAX;hidden("system.ram","installed RAM overflow rejected");
    reset();state.ramKb=0;hidden("capacity-range","zero installed RAM rejected");
    reset();state.sharedBootRead=false;hidden("system.boot","loader sequence copy failure cannot expose controls");
    reset();state.sharedBootShort=true;hidden("system.boot","short loader sequence copy cannot establish boot identity");
    reset();state.sharedMajor=11;hidden("system.boot","unknown shared-data OS layout cannot establish boot identity");
    reset();state.sharedArchitecture=12;hidden("system.boot","unexpected shared-data architecture cannot establish boot identity");
    reset();state.boot=0;const auto zeroBoot=readNow();check(zeroBoot.at("available")&&zeroBoot.at("bootId").get<std::string>().ends_with("|0"),"successful zero loader sequence read remains valid");
    reset();state.raw=8192;state.adapters[0].bytes=128*mib;const auto legacyBefore=readNow();auto legacySaved=applyNow(legacyBefore,"mib:8192");
    {const auto file=root/L"gpu-dedicated-memory.json";Json j;{std::ifstream input(file);input>>j;}j["bootId"]="{00000001-0000-0000-0000-000000000000}";std::ofstream output(file);output<<j.dump();}
    state.boot=2;const auto legacyMismatch=readNow();
    check(legacyMismatch.at("available")&&legacyMismatch.at("verification")=="unconfirmed"&&legacyMismatch.at("bootComparison")=="uncomparable"&&legacyMismatch.at("dedicatedBytes")==128*mib,"r4 legacy journal cannot falsely establish a new boot or stay pending forever");responses["legacy-boot-mismatch"]=legacyMismatch;
    state.adapters[0].bytes=8192*mib;const auto legacyMatch=readNow();
    check(legacyMatch.at("verification")=="unconfirmed","exact capacity with legacy GUID history does not fabricate restart proof");responses["legacy-boot-exact-capacity"]=legacyMatch;
    state.adapters[0].bytes=128*mib;auto legacyReapply=applyNow(readNow(),"mib:8192");
    check(legacyReapply.at("ok")&&legacyReapply.at("verification")=="pending_restart"&&legacyReapply.at("bootComparison")=="same_boot"&&state.writes==2,"explicit legacy discrepancy reapply records current loader sequence through existing write path");responses["legacy-boot-reapplied"]=legacyReapply;
    state.adapters[0].luid=456;state.adapters[0].coreLuid=456;check(readNow().at("verification")=="pending_restart","adapter reenumeration and app restart cannot fabricate a loader restart");
    state.bootEnvironment=2;check(readNow().at("verification")=="unconfirmed","different loader environment namespace cannot falsely verify or classify restart");
    reset();auto restoreBefore=readNow();auto restorePending=applyNow(restoreBefore,"mib:8192");state.boot=2;
    const auto restoreIneffective=readNow();check(restoreIneffective.at("verification")=="not_effective","ineffective provider remains readable before explicit Auto recovery");
    const auto restored=applyNow(restoreIneffective,"auto");
    check(restored.at("ok")&&restored.at("selected")=="auto"&&restored.at("verification")=="pending_restart"&&state.writes==2&&state.valueMissing,"blocked same target does not prevent explicit Auto recovery");responses["ineffective-restore-auto"]=restored;
    reset();state.bootRead=false;hidden("system.boot","boot query failure stays hidden");
    reset();state.bootZero=true;hidden("system.boot","empty boot identity rejected");
    reset();state.enumFailure=E_FAIL;hidden("dxgi.enumeration","failed enumeration stays hidden");
    reset();state.adapters[0].byLuid=E_FAIL;hidden("dxcore.luid","LUID lookup failure other than E_INVALIDARG stays hidden");
    const auto partialLuidFailure=readNow();const auto partialAdapter=partialLuidFailure.at("readDiagnostics").at("adapters").at(0);
    check(partialAdapter.at("luid")=="00000000:0000007b"&&partialAdapter.at("capacityRead")==true&&partialAdapter.at("dedicatedBytes")==4096*mib&&!partialLuidFailure.at("available").get<bool>(),"failed classification preserves actual already-read LUID and capacity without enabling controls");
    reset();state.adapters[0].hardwareHr=E_FAIL;hidden("dxcore.is-hardware","unknown hardware classification fails closed");
    reset();state.adapters[0].integratedHr=E_FAIL;hidden("dxcore.is-integrated","unknown integrated classification fails closed");
    reset();state.adapters[0].bytes=33*1024*mib;hidden("capacity-range","dedicated capacity above installed RAM rejected");
    reset();state.adapters[0].vendor=0x1002;hidden("adapter.none","AMD G3E identity does not enable Intel route");
    reset();state.adapters[0].flags=DXGI_ADAPTER_FLAG_SOFTWARE;hidden("adapter.none","DXGI software adapter ignored");
    reset();state.kmtOpen=false;hidden("adapter.binding","unresolved KMT identity rejected");
    reset();state.physicalCount=2;hidden("physical-count","linked physical adapters rejected");
    reset();state.kmtKey=false;state.kmtAddress=true;auto pci=readNow();check(pci.at("available")&&pci.at("bindingSource")=="pci-address","unavailable kernel key uses exact PCI fallback");
    reset();state.kmtAddress=true;state.bindingConflict=true;hidden("identity-conflict","kernel and PCI disagreement rejected");
    reset();state.enumComplete=false;hidden("adapter.binding.enumeration","incomplete present device enumeration rejected");
    reset();state.parentRead=ERROR_FILE_NOT_FOUND;hidden("driver-open","missing matched driver parent rejected");
    reset();state.versionRead=false;hidden("driver-version","missing bound driver version rejected");
    reset();state.adapters[0].driverHr=E_FAIL;hidden("dxcore.driver-version","missing live driver version rejected");
    reset();state.adapters[0].liveVersion=0;hidden("dxcore.driver-version","successful DXCore query with zero live driver version remains unconfirmed");
    reset();state.childMissing=true;auto missingChild=readNow();check(missingChild.at("available")&&missingChild.at("selected")=="auto","missing GMM below confirmed parent is Auto");responses["auto-initial"]=missingChild;
    reset();state.valueMissing=true;check(readNow().at("selected")=="auto","missing DedicatedSegmentSize is Auto");
    reset();state.childRead=ERROR_ACCESS_DENIED;hidden("intel.config.open","GMM access denied stays hidden");
    reset();state.valueRead=ERROR_ACCESS_DENIED;hidden("intel.config.read","dedicated value access denied stays hidden");
    reset();state.type=REG_SZ;hidden("intel.config.type","wrong registry value type rejected");
    reset();state.valueBytes=2;hidden("intel.config.type","truncated registry DWORD rejected");
    reset();state.raw=9000;hidden("intel.config.unknown","unknown registry capacity is not Auto");
    reset();state.ramKb=8ull*1024*1024;state.raw=8192;hidden("intel.config.unknown","configuration beyond RAM bound rejected");
    reset();Adapter alias=state.adapters[0];alias.luid=122;alias.coreLuid=122;state.adapters.push_back(alias);auto aliases=readNow();check(aliases.at("available")&&aliases.at("luid")=="00000000:0000007a","matching adapter aliases choose stable LUID");
    reset();alias=state.adapters[0];alias.luid=124;alias.coreLuid=124;alias.bytes=8*1024*mib;state.adapters.push_back(alias);hidden("adapter.ambiguous","aliases with conflicting capacity stay hidden");
    reset();state.adapters.assign(32,Adapter{});hidden("enumeration-limit","unterminated adapter enumeration stays hidden");
    reset();auto before=readNow();auto noConfirm=applyNow(before,"mib:8192",false);check(!noConfirm.at("ok").get<bool>()&&state.writes==0,"unconfirmed apply never writes");
    state.raw=2048;auto stale=applyNow(before,"mib:8192");check(!stale.at("ok").get<bool>()&&state.writes==0,"changed configuration invalidates frontend fence");
    reset();before=readNow();auto noOption=applyNow(before,"mib:9000");check(!noOption.at("ok").get<bool>()&&state.writes==0,"unoffered frontend option never writes");
    reset();before=readNow();state.denyWrite=true;auto denied=applyNow(before,"mib:8192");check(!denied.at("ok").get<bool>()&&state.writes==0,"write permission failure cannot report saved");responses["write-denied"]=denied;
    check(denied.at("writeDiagnostics").at("stage")=="intel.config.create"&&denied.at("writeDiagnostics").at("win32Error")==ERROR_ACCESS_DENIED&&
        !denied.at("writeDiagnostics").at("valueWriteAttempted").get<bool>()&&!denied.at("writeDiagnostics").at("configurationReadback").get<bool>()&&
        denied.at("reason").get<std::string>().find("Win32=5")!=std::string::npos,"create-key permission failure preserves stage and actual error without asserting driver acceptance");
    reset();before=readNow();state.parentWrite=ERROR_ACCESS_DENIED;const auto parentDenied=applyNow(before,"mib:8192");
    check(!parentDenied.at("ok").get<bool>()&&state.writes==0&&parentDenied.at("writeDiagnostics").at("stage")=="intel.parent.open"&&
        parentDenied.at("writeDiagnostics").at("api")=="RegOpenKeyExW"&&parentDenied.at("writeDiagnostics").at("win32Error")==ERROR_ACCESS_DENIED,
        "parent ACL refusal is distinct from value setter failure");responses["dispatch-parent-denied"]=parentDenied;
    reset();before=readNow();state.setWrite=ERROR_WRITE_PROTECT;const auto valueDenied=applyNow(before,"mib:8192");
    check(!valueDenied.at("ok").get<bool>()&&state.writes==0&&valueDenied.at("writeDiagnostics").at("stage")=="intel.config.set"&&
        valueDenied.at("writeDiagnostics").at("api")=="RegSetValueExW"&&valueDenied.at("writeDiagnostics").at("win32Error")==ERROR_WRITE_PROTECT&&
        valueDenied.at("writeDiagnostics").at("valueWriteAttempted").get<bool>()&&!valueDenied.at("writeDiagnostics").at("acknowledged").get<bool>(),
        "RegSetValueExW failure returns its own code and attempted-write state");responses["dispatch-set-denied"]=valueDenied;
    reset();before=readNow();state.childWrite=ERROR_ACCESS_DENIED;const auto autoOpenDenied=applyNow(before,"auto");
    check(!autoOpenDenied.at("ok").get<bool>()&&state.writes==0&&autoOpenDenied.at("writeDiagnostics").at("stage")=="intel.config.open-write"&&
        autoOpenDenied.at("writeDiagnostics").at("win32Error")==ERROR_ACCESS_DENIED,"Auto open ACL refusal is preserved without deleting any value");responses["dispatch-auto-open-denied"]=autoOpenDenied;
    reset();before=readNow();state.deleteWrite=ERROR_ACCESS_DENIED;const auto deleteDenied=applyNow(before,"auto");
    check(!deleteDenied.at("ok").get<bool>()&&state.writes==0&&!state.valueMissing&&deleteDenied.at("writeDiagnostics").at("stage")=="intel.config.delete"&&
        deleteDenied.at("writeDiagnostics").at("api")=="RegDeleteValueW"&&deleteDenied.at("writeDiagnostics").at("win32Error")==ERROR_ACCESS_DENIED,
        "Auto deletion failure is distinct from configuration readback");responses["dispatch-delete-denied"]=deleteDenied;
    reset();before=readNow();state.ignoreWrite=true;auto ignored=applyNow(before,"mib:8192");check(!ignored.at("ok").get<bool>()&&ignored.at("selected")=="mib:4096"&&ignored.at("verification")=="unconfirmed","successful write return with unchanged registry fails independent readback");responses["write-ignored"]=ignored;
    check(ignored.at("writeDiagnostics").at("acknowledged")==true&&ignored.at("writeDiagnostics").at("win32Error")==ERROR_SUCCESS&&
        ignored.at("writeDiagnostics").at("configurationReadback")==false&&ignored.at("writeDiagnostics").at("driverAcceptance")=="not_reported",
        "registry acknowledgement and failed independent readback remain separate");
    const auto ignoredFollowup=readNow();check(ignoredFollowup.at("ok")&&ignoredFollowup.at("available")&&ignoredFollowup.at("verification")=="unconfirmed"&&ignoredFollowup.at("dedicatedBytes")==4096*mib,"successful fresh read after ignored write keeps actual capacity and prepared target unconfirmed");responses["write-ignored-followup"]=ignoredFollowup;
    for(const auto change:{State::Change::config,State::Change::driver,State::Change::capacity}){
        reset();before=readNow();state.changeOnProbe=3;state.change=change;const auto drift=applyNow(before,"mib:8192");
        const char* label=change==State::Change::config?"config drift in final prewrite production probe prevents mutation":
            change==State::Change::driver?"driver drift in final prewrite production probe prevents mutation":"capacity drift in final prewrite production probe prevents mutation";
        check(!drift.at("ok").get<bool>()&&state.writes==0,label);
    }
    reset();before=readNow();state.changeOnProbe=4;state.change=State::Change::readFailure;
    const auto lostRead=applyNow(before,"mib:8192");check(!lostRead.at("ok").get<bool>()&&!lostRead.at("available").get<bool>()&&state.writes==1,"lost read after actual mutation hides controls and never verifies");responses["postwrite-read-failed"]=lostRead;
    check(lostRead.at("writeDiagnostics").at("acknowledged")==true&&lostRead.at("writeDiagnostics").at("configurationReadback")==false&&
        lostRead.at("writeDiagnostics").at("capacityRead")==false&&lostRead.at("writeDiagnostics").at("dedicatedBytes").is_null(),
        "post-write read failure returns no invented zero capacity");
    reset();before=readNow();state.denyFileOpenAt=1;const auto noJournal=applyNow(before,"mib:8192");
    check(!noJournal.at("ok").get<bool>()&&state.writes==0&&noJournal.at("selected")=="mib:4096","prepared journal failure prevents any registry mutation");responses["journal-before-failed"]=noJournal;
    reset();before=readNow();state.denyFileOpenAt=2;const auto saveJournalFailed=applyNow(before,"mib:8192");
    check(!saveJournalFailed.at("ok").get<bool>()&&state.writes==1&&saveJournalFailed.at("verification")=="unconfirmed","final journal failure never reports configuration verified");responses["journal-after-failed"]=saveJournalFailed;
    state.boot=2;state.adapters[0].bytes=8192*mib;check(readNow().at("verification")=="unconfirmed","prepared journal remains unconfirmed even with exact new boot capacity");
    reset();before=readNow();auto pending=applyNow(before,"mib:8192");check(pending.at("ok")&&state.writes==1&&pending.at("selected")=="mib:8192"&&pending.at("verification")=="pending_restart"&&pending.at("dedicatedBytes")==4096*mib,"saved target stays pending with actual 4 GiB capacity");responses["pending"]=pending;
    check(pending.at("writeDiagnostics").at("acknowledged")==true&&pending.at("writeDiagnostics").at("configurationReadback")==true&&
        pending.at("writeDiagnostics").at("dedicatedBytes")==4096*mib&&pending.at("writeDiagnostics").at("acknowledgementScope")=="registry-only"&&
        pending.at("writeDiagnostics").at("driverAcceptance")=="not_reported","saved 8 GiB configuration preserves actual 4 GiB and explicitly unknown driver consumption");
    check(!readNow().contains("writeDiagnostics"),"ordinary read never invents a driver or setter acknowledgement");
    check(readNow().at("verification")=="pending_restart","same boot read stays pending");
    state.boot=2;auto notEffective=readNow();check(notEffective.at("ok")&&notEffective.at("verification")=="not_effective"&&notEffective.at("dedicatedBytes")==4096*mib,"new boot with ignored driver setting never verifies");responses["not-effective"]=notEffective;
    state.adapters[0].bytes=8192*mib;auto verified=readNow();check(verified.at("verification")=="verified"&&verified.at("dedicatedBytes")==8192*mib,"new boot and exact capacity verifies actual driver result");responses["verified"]=verified;
    state.version=L"33.0.offline";auto changed=readNow();check(changed.at("verification")=="driver_changed","driver upgrade invalidates previous verification");responses["driver-changed"]=changed;
    reset();before=readNow();auto autoPending=applyNow(before,"auto");check(autoPending.at("ok")&&state.valueMissing&&autoPending.at("verification")=="pending_restart"&&autoPending.at("selected")=="auto","Auto deletes only bound DedicatedSegmentSize and waits for boot");responses["auto-pending"]=autoPending;
    state.boot=2;state.adapters[0].bytes=512*mib;auto defaultRead=readNow();check(defaultRead.at("verification")=="default_read"&&defaultRead.at("dedicatedBytes")==512*mib,"Auto after reboot reports actual default capacity");responses["auto-default"]=defaultRead;
    {std::ofstream bad(root/L"gpu-dedicated-memory.json");bad<<"corrupt";}
    hidden("显存设置记录无法读取","corrupt journal cannot expose controls");
    balanced();std::filesystem::remove(root/L"gpu-dedicated-memory.json");

    const auto prepareRog=[](){reset();pnp=LR"(PCI\VEN_1002&DEV_OFFLINE\ROG)";state.adapters[0].vendor=0x1002;state.adapters[0].name=L"AMD Radeon (offline ASUS transport)";state.adapters[0].bytes=2048*mib;};
    constexpr auto rogDeviceClass="HandheldCompanion.Devices.ROGAllyX";
    const auto readRogNow=[&](){auto j=runtimeRead(root.wstring(),rogDeviceClass);balanced();return j;};
    const auto applyRogNow=[&](const Json& snap){auto j=runtimeApply(root.wstring(),rogDeviceClass,Json{{"fence",snap.at("fence")},{"option","mib:4096"},{"confirmed",true}});balanced();return j;};
    for(const auto* model:{"HandheldCompanion.Devices.ROGAlly","HandheldCompanion.Devices.ROGAllyX","HandheldCompanion.Devices.XboxROGAlly","HandheldCompanion.Devices.XboxROGAllyX"}) {
        prepareRog();const auto supported=runtimeRead(root.wstring(),model);balanced();
        check(supported.at("available")&&supported.at("settingRoute")=="known-model"&&supported.at("dedicatedBytes")==2048*mib&&supported["readDiagnostics"]["settingRoute"]["allowed"]==true,"each known ROG model needs complete firmware and capacity reads before opening the UI");responses[std::string("known-model-")+model]=supported;
        const auto written=runtimeApply(root.wstring(),model,Json{{"fence",supported.at("fence")},{"option","mib:4096"},{"confirmed",true}});balanced();
        check(written.at("ok")&&written.at("verification")=="pending_restart"&&state.asusWrites==1&&state.writes==0,"each known ROG model executes only its bound firmware setter");
    }
    prepareRog();const auto noCapacity=runtimeRead(root.wstring(),rogDeviceClass);state.adapters[0].bytes=0;
    const auto vanished=runtimeApply(root.wstring(),rogDeviceClass,Json{{"fence",noCapacity.at("fence")},{"option","mib:4096"},{"confirmed",true}});balanced();
    check(!vanished.at("available").get<bool>()&&state.asusWrites==0&&!std::filesystem::exists(root/L"gpu-dedicated-memory.json"),"failed capacity reread blocks even a previously valid known model");responses["known-model-capacity-lost"]=vanished;
    prepareRog();auto rogInitial=readRogNow();
    check(rogInitial.at("provider")=="rog-vram"&&rogInitial.at("dedicatedBytes")==2048*mib&&rogInitial.at("selected")=="mib:2048"&&state.writes==0,"ROG whole reader uses real dedicated capacity and ASUS configuration without a registry write");
    check(rogInitial["readDiagnostics"]["adapters"][0]["asusMemory"]["optionsSource"]=="firmware-reply","ROG whole reader exposes existing firmware reply diagnostics");responses["rog-initial"]=rogInitial;
    auto rogPending=applyRogNow(rogInitial);
    check(rogPending.at("ok")&&rogPending.at("selected")=="mib:4096"&&rogPending.at("dedicatedBytes")==2048*mib&&rogPending.at("verification")=="pending_restart","ROG saved target never replaces real 2 GiB capacity before restart");
    check(rogPending["writeDiagnostics"]["acknowledged"]==true&&rogPending["writeDiagnostics"]["configurationReadback"]==true&&rogPending["writeDiagnostics"]["firmwareReturn"]==1&&rogPending["writeDiagnostics"]["deviceId"]==0x000600c4,"ROG write configuration is independently re-read through the same bound provider");
    check(rogPending["writeDiagnostics"]["acknowledgementScope"]=="firmware-method-return"&&rogPending["writeDiagnostics"]["driverAcceptance"]=="not_reported"&&state.asusWrites==1&&state.writes==0,"ROG dispatch records firmware-only acceptance without Intel registry mutation");responses["rog-pending"]=rogPending;
    state.boot=2;auto rogNotEffective=readRogNow();
    check(rogNotEffective.at("verification")=="not_effective"&&rogNotEffective.at("dedicatedBytes")==2048*mib,"ROG new-boot capacity mismatch remains not effective");responses["rog-not-effective"]=rogNotEffective;
    state.adapters[0].bytes=4096*mib;auto rogVerified=readRogNow();
    check(rogVerified.at("verification")=="verified"&&rogVerified.at("dedicatedBytes")==4096*mib,"ROG exact capacity after comparable restart verifies");responses["rog-verified"]=rogVerified;
    prepareRog();rogInitial=readRogNow();state.asusIgnore=true;auto rogIgnored=applyRogNow(rogInitial);
    check(!rogIgnored.at("ok").get<bool>()&&rogIgnored["writeDiagnostics"]["acknowledged"]==true&&rogIgnored["writeDiagnostics"]["configurationReadback"]==false&&rogIgnored.at("dedicatedBytes")==2048*mib,"ROG method ACK with ignored configuration fails independent readback");responses["rog-config-ignored"]=rogIgnored;
    prepareRog();rogInitial=readRogNow();state.asusDenyOpenAt=state.asusOpens+3;auto rogDenied=applyRogNow(rogInitial);
    check(!rogDenied.at("ok").get<bool>()&&rogDenied["writeDiagnostics"]["win32Error"]==ERROR_ACCESS_DENIED&&rogDenied["writeDiagnostics"]["valueWriteAttempted"]==false&&state.asusWrites==0,"ROG write open denial is traceable and performs no mutation");responses["rog-open-denied"]=rogDenied;
    prepareRog();rogInitial=readRogNow();state.asusReply=0x42;auto rogRefused=applyRogNow(rogInitial);
    check(!rogRefused.at("ok").get<bool>()&&rogRefused["writeDiagnostics"]["firmwareReturn"]==0x42&&rogRefused["writeDiagnostics"]["win32Error"]==0&&rogRefused["writeDiagnostics"]["acknowledged"]==false,"ROG firmware refusal cannot be hidden by successful DeviceIoControl");responses["rog-firmware-refused"]=rogRefused;
    prepareRog();state.asusShort=true;auto rogShort=readRogNow();
    check(!rogShort.at("available").get<bool>()&&state.asusReads==1&&rogShort["readDiagnostics"]["adapters"][0]["asusMemory"]["reads"][0]["returnedBytes"]==2,"ROG short read hides controls and preserves exact reply length");responses["rog-short-read"]=rogShort;
    prepareRog();state.asusRaw=8;auto externalRog=readRogNow();
    check(externalRog.at("selected")=="mib:4096"&&externalRog.at("dedicatedBytes")==2048*mib&&externalRog.at("verification")=="unconfirmed"&&!externalRog.contains("pendingOption"),"existing external ROG 4 GiB configuration with actual 2 GiB is unconfirmed without a saved journal");responses["rog-configured-mismatch"]=externalRog;
    auto externalNoop=applyRogNow(externalRog);check(externalNoop.at("ok")&&externalNoop.at("verification")=="unconfirmed"&&state.asusWrites==0,"same external ROG configuration is read-only until another firmware option is explicitly selected");
    prepareRog();auto unsupportedAmd=runtimeRead(root.wstring(),deviceClass);
    check(!unsupportedAmd.at("available").get<bool>()&&state.asusReads==0&&state.asusWrites==0,"AMD vendor alone never admits the ASUS provider or probes an MSI machine");responses["amd-unsupported-model"]=unsupportedAmd;
    balanced();std::filesystem::remove(root/L"gpu-dedicated-memory.json");
    Json publicResponses=Json::object(),candidateResponses=Json::object();
    for(const auto& [name,response]:responses.items()) {
        // Only public runtime API replies carry the setting-route policy.
        (response.contains("settingRoute")?publicResponses:candidateResponses)[name]=response;
    }
    const Json result{{"ok",true},{"cases",cases},{"responses",publicResponses},{"candidateResponses",candidateResponses},{"topologyPermutations",topologyPermutations},{"uncertainCombinations",uncertainCombinations},{"productionReader",true},{"productionApply",true},{"hardwareAccess",false},{"registryAccess",false},{"journalStorage","isolated-files"}};
    std::ofstream output(root.parent_path()/L"windows-result.json");output<<result.dump(2);if(!output)throw std::runtime_error("cannot write result");
    std::cout<<"GPU_DEDICATED_MEMORY_WINDOWS_SELFTEST_OK cases="<<cases.size()<<" hardware=fake registry=fake\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}





