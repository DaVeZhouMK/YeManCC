// Exercise the production LUID binding with fake KMT, SetupAPI and registry transports.
#include <windows.h>
#include <dxgi1_2.h>
#include <dxcore.h>
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
namespace fixture {
static int cases=0;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);++cases;}
static const std::wstring ref=LR"({4d36e968-e325-11ce-bfc1-08002be10318}\0003)";
static const std::wstring driverPath=LR"(SYSTEM\CurrentControlSet\Control\Class\)"+ref;
static const HKEY driverKey=reinterpret_cast<HKEY>(std::uintptr_t(0x1001));
static const HKEY selectKey=reinterpret_cast<HKEY>(std::uintptr_t(0x1002));
static const HDEVINFO deviceSet=reinterpret_cast<HDEVINFO>(std::uintptr_t(0x2001));
struct Node {std::wstring reference=ref,pnp=LR"(PCI\VEN_8086&DEV_FIXTURE\G3E)";bool pci=true;DWORD bus=0,slot=2u<<16;};
struct State {
    bool open=true,countQuery=true,keyQuery=true,addressQuery=true,enumComplete=true,driverOpen=true,driverVersion=true,selectRead=true;
    UINT count=1;DWORD current=1;int badBuffer=0;unsigned closes=0,destroys=0;
    std::wstring path=LR"(\Registry\Machine\SYSTEM\ControlSet001\Control\Class\)"+ref;
    std::vector<Node> nodes{Node{}};
} state;
static NTSTATUS APIENTRY open(const D3DKMT_OPENADAPTERFROMLUID* p){if(!state.open)return static_cast<NTSTATUS>(0xc0000001);const_cast<D3DKMT_OPENADAPTERFROMLUID*>(p)->hAdapter=7;return 0;}
static NTSTATUS APIENTRY close(const D3DKMT_CLOSEADAPTER* p){if(p->hAdapter!=7)throw std::runtime_error("unbound close");++state.closes;return 0;}
static NTSTATUS APIENTRY query(const D3DKMT_QUERYADAPTERINFO* p){
    if(p->hAdapter!=7)throw std::runtime_error("unbound KMT query");
    const auto failure=static_cast<NTSTATUS>(0xc0000001);
    if(p->Type==KMTQAITYPE_PHYSICALADAPTERCOUNT){if(!state.countQuery)return failure;static_cast<D3DKMT_PHYSICAL_ADAPTER_COUNT*>(p->pPrivateDriverData)->Count=state.count;return 0;}
    if(p->Type==KMTQAITYPE_PHYSICALADAPTERPNPKEY){
        if(!state.keyQuery)return failure;
        auto k=static_cast<D3DKMT_QUERY_PHYSICAL_ADAPTER_PNP_KEY*>(p->pPrivateDriverData);
        if(k->PhysicalAdapterIndex!=0||k->PnPKeyType!=D3DKMT_PNP_KEY_SOFTWARE)throw std::runtime_error("unexpected physical key query");
        if(state.badBuffer==1){*k->pCchDest=5000;return 0;}
        if(state.badBuffer==2){*k->pCchDest=1;return 0;}
        const auto size=static_cast<UINT>(state.path.size()+1);if(size>*k->pCchDest)throw std::runtime_error("fixture too long");
        memcpy(k->pDest,state.path.c_str(),size*sizeof(wchar_t));*k->pCchDest=size;
        if(state.badBuffer==3)k->pDest[size-1]=L'x';return 0;
    }
    if(p->Type==KMTQAITYPE_ADAPTERADDRESS){if(!state.addressQuery)return failure;auto a=static_cast<D3DKMT_ADAPTERADDRESS*>(p->pPrivateDriverData);a->BusNumber=0;a->DeviceNumber=2;a->FunctionNumber=0;return 0;}
    throw std::runtime_error("unexpected KMT query");
}
static HDEVINFO WINAPI devices(const GUID*,PCWSTR,HWND,DWORD flags){if(flags!=DIGCF_PRESENT)throw std::runtime_error("non-present enumeration");return deviceSet;}
static BOOL WINAPI enumerate(HDEVINFO set,DWORD index,PSP_DEVINFO_DATA node){
    if(set!=deviceSet)throw std::runtime_error("wrong set");
    if(index>=state.nodes.size()){SetLastError(state.enumComplete?ERROR_NO_MORE_ITEMS:ERROR_ACCESS_DENIED);return FALSE;}
    node->DevInst=index;return TRUE;
}
static BOOL WINAPI destroy(HDEVINFO set){if(set!=deviceSet)throw std::runtime_error("wrong destroy");++state.destroys;return TRUE;}
static BOOL WINAPI property(HDEVINFO,PSP_DEVINFO_DATA node,DWORD prop,PDWORD type,PBYTE data,DWORD bytes,PDWORD required){
    const auto& n=state.nodes.at(node->DevInst);
    if(prop==SPDRP_DRIVER){const auto size=static_cast<DWORD>((n.reference.size()+1)*sizeof(wchar_t));if(bytes<size)return FALSE;*type=REG_SZ;memcpy(data,n.reference.c_str(),size);if(required)*required=size;return TRUE;}
    if(!n.pci)return FALSE;
    DWORD number=prop==SPDRP_BUSNUMBER?n.bus:n.slot;if(prop!=SPDRP_BUSNUMBER&&prop!=SPDRP_ADDRESS)throw std::runtime_error("unexpected property");
    if(bytes<sizeof(number))return FALSE;*type=REG_DWORD;memcpy(data,&number,sizeof(number));if(required)*required=sizeof(number);return TRUE;
}
static BOOL WINAPI instance(HDEVINFO,PSP_DEVINFO_DATA node,PWSTR text,DWORD chars,PDWORD required){const auto& p=state.nodes.at(node->DevInst).pnp;const auto size=static_cast<DWORD>(p.size()+1);if(chars<size)return FALSE;memcpy(text,p.c_str(),size*sizeof(wchar_t));if(required)*required=size;return TRUE;}
static LSTATUS WINAPI openKey(HKEY parent,LPCWSTR path,DWORD,REGSAM access,PHKEY result){
    if(parent!=HKEY_LOCAL_MACHINE||access!=(KEY_READ|KEY_WOW64_64KEY))throw std::runtime_error("registry write or unexpected view");
    if(path==driverPath){if(!state.driverOpen)return ERROR_ACCESS_DENIED;*result=driverKey;return ERROR_SUCCESS;}
    if(std::wstring(path)==LR"(SYSTEM\Select)"){if(!state.selectRead)return ERROR_ACCESS_DENIED;*result=selectKey;return ERROR_SUCCESS;}
    throw std::runtime_error("unexpected registry path");
}
static LSTATUS WINAPI queryValue(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD bytes){
    if(key==selectKey&&std::wstring(name)==L"Current"){*type=REG_DWORD;if(*bytes<sizeof(DWORD))return ERROR_MORE_DATA;memcpy(data,&state.current,sizeof(DWORD));*bytes=sizeof(DWORD);return ERROR_SUCCESS;}
    if(key==driverKey&&std::wstring(name)==L"DriverVersion"){
        if(!state.driverVersion)return ERROR_FILE_NOT_FOUND;const std::wstring version=L"32.0.fixture";const auto size=static_cast<DWORD>((version.size()+1)*sizeof(wchar_t));*type=REG_SZ;
        if(!data){*bytes=size;return ERROR_SUCCESS;}if(*bytes<size)return ERROR_MORE_DATA;memcpy(data,version.c_str(),size);*bytes=size;return ERROR_SUCCESS;
    }
    throw std::runtime_error("unexpected registry query");
}
static LSTATUS WINAPI closeKey(HKEY key){if(key!=driverKey&&key!=selectKey)throw std::runtime_error("unbound registry handle");return ERROR_SUCCESS;}
}
#define D3DKMTOpenAdapterFromLuid fixture::open
#define D3DKMTQueryAdapterInfo fixture::query
#define D3DKMTCloseAdapter fixture::close
#define SetupDiGetClassDevsW fixture::devices
#define SetupDiEnumDeviceInfo fixture::enumerate
#define SetupDiDestroyDeviceInfoList fixture::destroy
#define SetupDiGetDeviceRegistryPropertyW fixture::property
#define SetupDiGetDeviceInstanceIdW fixture::instance
#define RegOpenKeyExW fixture::openKey
#define RegQueryValueExW fixture::queryValue
#define RegCloseKey fixture::closeKey
#include "../native/gpu_dedicated_memory_runtime.h"
#undef D3DKMTOpenAdapterFromLuid
#undef D3DKMTQueryAdapterInfo
#undef D3DKMTCloseAdapter
#undef SetupDiGetClassDevsW
#undef SetupDiEnumDeviceInfo
#undef SetupDiDestroyDeviceInfoList
#undef SetupDiGetDeviceRegistryPropertyW
#undef SetupDiGetDeviceInstanceIdW
#undef RegOpenKeyExW
#undef RegQueryValueExW
#undef RegCloseKey
using namespace ymcc::dedicatedmemory;
int main(){try{
    using namespace fixture;const LUID luid{123,0};
    const auto reset=[](){state=State{};};
    {reset();const auto b=bindingFor(luid);check(b&&b->source=="kernel-pnp"&&b->registryKey==driverPath,"kernel ControlSet binding resolves exact present driver");check(state.closes==1&&state.destroys==1,"handles closed after success");}
    {reset();state.nodes[0].pci=false;const auto b=bindingFor(luid);check(b&&b->source=="kernel-pnp","missing optional PCI properties does not hide confirmed driver");}
    {reset();state.addressQuery=false;check(bindingFor(luid).has_value(),"missing PCI query does not hide confirmed driver");}
    {reset();state.path=LR"(\Registry\Machine\SYSTEM\CurrentControlSet\Control\Class\)"+ref;check(bindingFor(luid).has_value(),"CurrentControlSet kernel path supported");}
    {reset();state.keyQuery=false;const auto b=bindingFor(luid);check(b&&b->source=="pci-address","unavailable kernel key retains exact PCI fallback");}
    {reset();state.countQuery=false;const auto b=bindingFor(luid);check(b&&b->source=="pci-address","unavailable physical count retains exact PCI fallback");}
    for(const UINT count:{0u,2u}){reset();state.count=count;check(!bindingFor(luid),"invalid physical count cannot fall back");}
    {reset();state.path=LR"(\Registry\Machine\SYSTEM\ControlSet002\Control\Class\)"+ref;check(!bindingFor(luid),"inactive ControlSet cannot fall back");}
    {reset();state.selectRead=false;check(!bindingFor(luid),"unknown current ControlSet cannot fall back");}
    {reset();state.path=LR"(\Registry\User\SYSTEM\ControlSet001\Control\Class\)"+ref;check(!bindingFor(luid),"wrong hive cannot fall back");}
    {reset();state.path+=L"\\GMM";check(!bindingFor(luid),"unexpected key child cannot fall back");}
    for(const int bad:{1,2,3}){reset();state.badBuffer=bad;check(!bindingFor(luid),"malformed successful kernel buffer cannot fall back");}
    {reset();state.nodes[0].slot=3u<<16;std::string reason;check(!bindingFor(luid,&reason)&&reason.find("identity-conflict")!=std::string::npos,"kernel and PCI disagreement hides control");check(state.closes==1&&state.destroys==1,"handles closed after conflict");}
    {reset();state.nodes.push_back(state.nodes[0]);check(!bindingFor(luid),"duplicate present driver keys rejected");}
    {reset();state.nodes[0].reference=LR"({4d36e968-e325-11ce-bfc1-08002be10318}\0004)";check(!bindingFor(luid),"unmatched kernel key cannot choose first Intel or fall back");}
    {reset();state.enumComplete=false;check(!bindingFor(luid),"incomplete present enumeration rejected");}
    {reset();state.driverOpen=false;check(!bindingFor(luid),"unreadable driver key rejected");}
    {reset();state.driverVersion=false;check(!bindingFor(luid),"unreadable version rejected");}
    {reset();state.open=false;check(!bindingFor(luid)&&state.closes==0,"failed open has no handle close");}
    {reset();state.keyQuery=false;state.nodes[0].pci=false;check(!bindingFor(luid),"both binding routes unavailable remains hidden");}
    {reset();state.nodes[0].pnp=LR"(ROOT\FIXTURE)";check(!bindingFor(luid),"non-PCI software identity rejected");}
    std::cout<<"GPU_DEDICATED_MEMORY_BINDING_SELFTEST_OK cases="<<cases<<" hardware=fake registry=fake\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
