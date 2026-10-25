// Execute production channel helpers with fake Windows transport. Never probe the host.
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
static int cases = 0;
static void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); ++cases; }
static const std::wstring adapterPath = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0003";
static const HKEY adapterKey = reinterpret_cast<HKEY>(std::uintptr_t(0x1001));
static const HKEY gmmKey = reinterpret_cast<HKEY>(std::uintptr_t(0x1002));
struct Registry {
    std::wstring version = L"32.0.fixture";
    bool parentMissing=false,childMissing=false,valueMissing=false,denyWrite=false;
    DWORD raw=4096,type=REG_DWORD;
    unsigned mutations=0,parentCreates=0,childCreates=0,deletes=0;
    std::vector<std::wstring> paths;
    std::vector<REGSAM> access;
} reg;
static LSTATUS WINAPI openKey(HKEY parent,LPCWSTR path,DWORD,REGSAM access,PHKEY result) {
    reg.paths.emplace_back(path);reg.access.push_back(access);
    if ((access & KEY_WOW64_64KEY)==0) throw std::runtime_error("registry view not explicit");
    if (parent==HKEY_LOCAL_MACHINE && path==adapterPath) {
        if(reg.parentMissing)return ERROR_FILE_NOT_FOUND;*result=adapterKey;return ERROR_SUCCESS;
    }
    if (parent==adapterKey && std::wstring(path)==L"GMM") {
        if(reg.childMissing)return ERROR_FILE_NOT_FOUND;
        if(reg.denyWrite && (access & KEY_SET_VALUE))return ERROR_ACCESS_DENIED;
        *result=gmmKey;return ERROR_SUCCESS;
    }
    throw std::runtime_error("unbound registry path");
}
static LSTATUS WINAPI queryValue(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD size) {
    if(key==adapterKey && std::wstring(name)==L"DriverVersion") {
        *type=REG_SZ;const auto bytes=static_cast<DWORD>((reg.version.size()+1)*sizeof(wchar_t));
        if(data && *size<bytes)return ERROR_MORE_DATA;
        if(data)memcpy(data,reg.version.c_str(),bytes);*size=bytes;return ERROR_SUCCESS;
    }
    if(key==gmmKey && std::wstring(name)==L"DedicatedSegmentSize") {
        if(reg.valueMissing)return ERROR_FILE_NOT_FOUND;*type=reg.type;
        if(!data || *size<sizeof(DWORD))return ERROR_MORE_DATA;
        memcpy(data,&reg.raw,sizeof(DWORD));*size=sizeof(DWORD);return ERROR_SUCCESS;
    }
    throw std::runtime_error("unexpected registry query");
}
static LSTATUS WINAPI createKey(HKEY parent,LPCWSTR name,DWORD,LPWSTR,DWORD,REGSAM access,
                                const LPSECURITY_ATTRIBUTES,PHKEY result,LPDWORD) {
    if(parent!=adapterKey){++reg.parentCreates;throw std::runtime_error("cannot recreate adapter parent");}
    if(std::wstring(name)!=L"GMM" || !(access & KEY_SET_VALUE) || !(access & KEY_WOW64_64KEY))throw std::runtime_error("unexpected child creation");
    if(reg.denyWrite)return ERROR_ACCESS_DENIED;
    ++reg.childCreates;reg.childMissing=false;*result=gmmKey;return ERROR_SUCCESS;
}
static LSTATUS WINAPI setValue(HKEY key,LPCWSTR name,DWORD,DWORD type,const BYTE* data,DWORD size) {
    if(key!=gmmKey || std::wstring(name)!=L"DedicatedSegmentSize" || type!=REG_DWORD || size!=sizeof(DWORD))throw std::runtime_error("unexpected registry mutation");
    if(reg.denyWrite)return ERROR_ACCESS_DENIED;
    memcpy(&reg.raw,data,size);reg.valueMissing=false;++reg.mutations;return ERROR_SUCCESS;
}
static LSTATUS WINAPI deleteValue(HKEY key,LPCWSTR name) {
    if(key!=gmmKey || std::wstring(name)!=L"DedicatedSegmentSize")throw std::runtime_error("unexpected registry deletion");
    if(reg.denyWrite)return ERROR_ACCESS_DENIED;
    if(reg.valueMissing)return ERROR_FILE_NOT_FOUND;reg.valueMissing=true;++reg.mutations;++reg.deletes;return ERROR_SUCCESS;
}
static LSTATUS WINAPI closeKey(HKEY key) {
    if(key!=adapterKey && key!=gmmKey)throw std::runtime_error("unknown registry handle");return ERROR_SUCCESS;
}
struct Request { std::vector<std::uint32_t> words;DWORD outputSize=0; };
struct Response { std::vector<std::uint8_t> bytes;bool success=true;DWORD returned=0,error=ERROR_GEN_FAILURE; };
struct Acpi { bool denyOpen=false;unsigned closed=0;std::vector<Request> requests;std::vector<Response> replies; } acpi;
static const HANDLE acpiHandle = reinterpret_cast<HANDLE>(std::uintptr_t(0x2001));
static HANDLE WINAPI openFile(LPCWSTR name,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES,DWORD disposition,DWORD,HANDLE) {
    if(std::wstring(name)!=L"\\\\.\\ATKACPI" || access!=(GENERIC_READ|GENERIC_WRITE) ||
       sharing!=(FILE_SHARE_READ|FILE_SHARE_WRITE) || disposition!=OPEN_EXISTING)throw std::runtime_error("unexpected device open");
    if(acpi.denyOpen){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}return acpiHandle;
}
static BOOL WINAPI control(HANDLE handle,DWORD code,LPVOID input,DWORD inputSize,LPVOID output,DWORD outputSize,LPDWORD returned,LPOVERLAPPED) {
    if(handle!=acpiHandle || code!=0x0022240c || inputSize%4)throw std::runtime_error("unexpected IOCTL");
    const auto words=static_cast<const std::uint32_t*>(input);
    acpi.requests.push_back({{words,words+inputSize/4},outputSize});
    if(acpi.replies.empty())throw std::runtime_error("unplanned firmware request");
    const auto reply=acpi.replies.front();acpi.replies.erase(acpi.replies.begin());
    if(reply.bytes.size()>outputSize)throw std::runtime_error("test response exceeds buffer");
    memcpy(output,reply.bytes.data(),reply.bytes.size());
    *returned=reply.returned ? reply.returned : static_cast<DWORD>(reply.bytes.size());
    if(!reply.success)SetLastError(reply.error);return reply.success ? TRUE : FALSE;
}
static BOOL WINAPI closeFile(HANDLE handle) {
    if(handle!=acpiHandle)throw std::runtime_error("unknown device handle");++acpi.closed;return TRUE;
}
static Response scalar(std::uint32_t raw) {
    return {{{static_cast<std::uint8_t>(raw),static_cast<std::uint8_t>(raw>>8),static_cast<std::uint8_t>(raw>>16),static_cast<std::uint8_t>(raw>>24)}},true,0};
}
static Response vram() { Response r;r.bytes.assign(16,0);r.bytes[0]=3;r.bytes[2]=3;r.bytes[4]=4;r.bytes[8]=4;r.bytes[10]=8;return r; }
}
#define RegOpenKeyExW fixture::openKey
#define RegQueryValueExW fixture::queryValue
#define RegCreateKeyExW fixture::createKey
#define RegSetValueExW fixture::setValue
#define RegDeleteValueW fixture::deleteValue
#define RegCloseKey fixture::closeKey
#define CreateFileW fixture::openFile
#define DeviceIoControl fixture::control
#define CloseHandle fixture::closeFile
#include "../native/gpu_dedicated_memory_runtime.h"
#undef RegOpenKeyExW
#undef RegQueryValueExW
#undef RegCreateKeyExW
#undef RegSetValueExW
#undef RegDeleteValueW
#undef RegCloseKey
#undef CreateFileW
#undef DeviceIoControl
#undef CloseHandle
using namespace ymcc::dedicatedmemory;
static Probe intel() {
    Probe p;p.hardware=p.integrated=p.identityConfirmed=true;p.provider="intel-gmm";
    p.pnpId="PCI\\VEN_8086&DEV_TEST";p.driverVersion="32.0.fixture:123";p.deviceClass="G3E";
    p.bootId="boot-fixture";p.adapterName="Intel fixture";p.luid="luid-fixture";
    p.registryKey=utf8(fixture::adapterPath);p.installedBytes=32768*mib;p.capacityRead=true;p.dedicatedBytes=4096*mib;
    p.configuredRaw=4096;p.options=intelOptions(p.installedBytes);return p;
}
static const Option& option(const Probe& p,const char* id) {
    const auto it=std::find_if(p.options.begin(),p.options.end(),[&](const Option& o){return o.id==id;});
    if(it==p.options.end())throw std::runtime_error("test option missing");return *it;
}
int main() {
 using namespace fixture;
 try {
    {reg={};auto p=intel();check(readIntelSetting(p)&&p.configuredRaw==4096u,"read actual matched Intel node");
     check(reg.paths==std::vector<std::wstring>{adapterPath,L"GMM"}&&reg.mutations==0,"reader uses 0003 without writes or index scan");}
    {reg={};auto p=intel();check(writeIntelSetting(p,option(p,"mib:8192"))&&reg.raw==8192,"write Intel MiB to the same node");
     check(reg.paths==std::vector<std::wstring>{adapterPath}&&reg.parentCreates==0&&reg.childCreates==1&&reg.mutations==1,"writer creates only matched GMM child");
     check(readIntelSetting(p)&&p.configuredRaw==8192u,"independent configuration read uses identical parent");}
    {reg={};auto p=intel();check(writeIntelSetting(p,option(p,"auto"))&&reg.deletes==1&&reg.childCreates==0,"Auto deletes only matched value");
     check(readIntelSetting(p)&&!p.configuredRaw,"deleted value is read as absence");}
    {reg={};reg.parentMissing=true;auto p=intel();check(!readIntelSetting(p)&&!writeIntelSetting(p,option(p,"auto"))&&reg.mutations==0,"missing parent cannot masquerade as Auto");}
    {reg={};reg.version=L"new-driver";auto p=intel();check(!readIntelSetting(p)&&!writeIntelSetting(p,option(p,"auto"))&&!writeIntelSetting(p,option(p,"mib:8192"))&&reg.childCreates==0&&reg.mutations==0,"both write modes reject changed driver");}
    {reg={};reg.childMissing=true;auto p=intel();check(readIntelSetting(p)&&!p.configuredRaw&&reg.mutations==0,"missing GMM on a valid parent is readable Auto");
     check(writeIntelSetting(p,option(p,"auto"))&&reg.childCreates==0&&reg.mutations==0,"Auto does not create GMM");}
    {reg={};reg.type=REG_SZ;auto p=intel();check(!readIntelSetting(p),"wrong registry value type rejected");}
    {reg={};reg.raw=123;auto p=intel();check(!readIntelSetting(p),"unknown Intel value has no default");}
    {reg={};reg.denyWrite=true;auto p=intel();check(!writeIntelSetting(p,option(p,"mib:8192"))&&reg.mutations==0,"write denial is not success");}
    {reg={};auto p=intel();auto bad=option(p,"mib:8192");bad.raw=1024;
     check(!writeIntelSetting(p,bad)&&reg.paths.empty(),"unoffered payload never enters registry channel");}
    {acpi={};acpi.replies={vram()};auto p=intel();p.provider.clear();p.deviceClass="HandheldCompanion.Devices.ROGAllyX";
     check(readRogSetting(p)&&p.provider=="rog-vram"&&p.configuredRaw==4u,"ROG buffer selects new VRAM route");
     check(acpi.requests[0].words==std::vector<std::uint32_t>{0x53545344,12,0x000600c4,0,0}&&acpi.requests[0].outputSize==64,"new read emits DSTS c4 large packet");
     acpi.replies={scalar(1)};check(writeRogSetting(p,option(p,"mib:4096")),"ROG new route accepts firmware ack");
     check(acpi.requests[1].words==std::vector<std::uint32_t>{0x53564544,8,0x000600c4,8}&&acpi.requests[1].outputSize==16,"new write emits DEVS c4 with raw units");
     auto updated=vram();updated.bytes[4]=8;acpi.replies={updated};check(readRogSetting(p)&&p.configuredRaw==8u&&acpi.requests[2].words[2]==0x000600c4,"new configuration is read back on the same c4 route");
     check(acpi.closed==3,"each transport handle closes");}
    {acpi={};acpi.replies={scalar(0),scalar(0x10105)};auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAlly";
     check(readRogSetting(p)&&p.provider=="rog-apu"&&p.configuredRaw==0x105u,"explicit unsupported new endpoint allows known legacy route");
     check(acpi.requests[1].words==std::vector<std::uint32_t>{0x53545344,8,0x000600c1,0},"legacy read emits DSTS c1");
     acpi.replies={scalar(1)};check(writeRogSetting(p,option(p,"mib:8192")),"legacy setter uses offered encoding");
     check(acpi.requests[2].words==std::vector<std::uint32_t>{0x53564544,8,0x000600c1,0x106},"legacy write uses the same c1 endpoint");}
    {acpi={};acpi.replies={scalar(0x42)};auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAlly";
     check(!readRogSetting(p)&&acpi.requests.size()==1,"unknown c4 reply cannot open a legacy route");}
    {acpi={};acpi.replies={scalar(0)};auto p=intel();p.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
     check(!readRogSetting(p)&&acpi.requests.size()==1,"Xbox model cannot fall back to classic endpoint");}
    {acpi={};auto malformed=vram();malformed.bytes[0]=32;acpi.replies={malformed};auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAlly";
     check(!readRogSetting(p)&&acpi.requests.size()==1,"truncated option list cannot fall back");}
    {acpi={};acpi.replies={{{1,0},true,2}};auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAlly";
     check(!readRogSetting(p)&&acpi.requests.size()==1,"short IOCTL response rejected");}
    {acpi={};acpi.denyOpen=true;auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAlly";
     check(!readRogSetting(p)&&acpi.requests.empty(),"unopenable firmware channel has no fallback");}
    {acpi={};auto p=intel();p.provider="rog-apu";p.deviceClass="HandheldCompanion.Devices.ROGAlly";p.options=rogApuOptions(p.installedBytes);p.configuredRaw=0x105;
     acpi.replies={scalar(0)};check(!writeRogSetting(p,option(p,"mib:8192")),"non-success firmware ack rejected");
     p.deviceClass="unsupported";acpi.requests.clear();check(!writeRogSetting(p,option(p,"mib:8192"))&&acpi.requests.empty(),"unsupported machine cannot enter firmware write channel");}

    {acpi={};acpi.replies={vram()};auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAllyX";Json details;
     check(readRogSetting(p,&details)&&details["asusMemory"]["reads"][0]["deviceId"]==0x000600c4&&details["asusMemory"]["optionsSource"]=="firmware-reply","new ROG read records actual endpoint and option source");
     acpi.replies={scalar(1)};Json dispatch;
     check(writeRogSetting(p,option(p,"mib:4096"),&dispatch)&&dispatch["acknowledged"]==true&&dispatch["firmwareReturn"]==1,"ROG write preserves firmware method return");
     check(dispatch["mechanism"]=="asus-acpi-memory"&&dispatch["acknowledgementScope"]=="firmware-method-return"&&dispatch["driverAcceptance"]=="not_reported","ROG firmware acknowledgement never claims driver adoption");
     check(dispatch["api"]=="DeviceIoControl"&&dispatch["win32Error"]==0&&dispatch["valueWriteAttempted"]==true&&dispatch["requestedRaw"]==8,"ROG dispatch identifies actual API, raw encoding and attempted mutation");}
    {acpi={};auto p=intel();p.provider="rog-apu";p.deviceClass="HandheldCompanion.Devices.ROGAlly";p.options=rogApuOptions(p.installedBytes);p.configuredRaw=0x105;Json dispatch;
     acpi.denyOpen=true;check(!writeRogSetting(p,option(p,"mib:8192"),&dispatch)&&dispatch["stage"]=="asus.device.open"&&dispatch["api"]=="CreateFileW"&&dispatch["win32Error"]==ERROR_ACCESS_DENIED,"ROG access denial records real CreateFile error");
     check(dispatch["valueWriteAttempted"]==false&&dispatch["acknowledged"]==false&&acpi.requests.empty(),"ROG open refusal is not a write acknowledgement");
     acpi.denyOpen=false;auto rejected=scalar(1);rejected.success=false;rejected.error=ERROR_NOT_SUPPORTED;acpi.replies={rejected};
     check(!writeRogSetting(p,option(p,"mib:8192"),&dispatch)&&dispatch["api"]=="DeviceIoControl"&&dispatch["win32Error"]==ERROR_NOT_SUPPORTED&&dispatch["valueWriteAttempted"]==true,"ROG IOCTL refusal preserves real error without claiming acceptance");
     check(dispatch["acknowledged"]==false&&!dispatch.contains("firmwareReturn"),"failed ROG transport has no invented firmware reply");
     acpi.replies={{{1,0},true,2}};
     check(!writeRogSetting(p,option(p,"mib:8192"),&dispatch)&&dispatch["transportOk"]==true&&dispatch["returnedBytes"]==2&&dispatch["acknowledged"]==false,"successful IOCTL with short reply is not a setting acknowledgement");
     acpi.replies={scalar(0x42)};
     check(!writeRogSetting(p,option(p,"mib:8192"),&dispatch)&&dispatch["firmwareReturn"]==0x42&&dispatch["win32Error"]==0&&dispatch["acknowledged"]==false,"unknown firmware reply remains distinct from Win32 success");}
    {acpi={};acpi.denyOpen=true;auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAlly";Json details;
     check(!readRogSetting(p,&details)&&details["asusMemory"]["reads"][0]["win32Error"]==ERROR_ACCESS_DENIED&&acpi.requests.empty(),"read failure preserves ASUS device error and does not fall back");}
    {acpi={};acpi.replies={scalar(0),scalar(0x10105)};auto p=intel();p.deviceClass="HandheldCompanion.Devices.ROGAlly";Json details;
     check(readRogSetting(p,&details)&&details["asusMemory"]["reads"].size()==2&&details["asusMemory"]["reads"][1]["deviceId"]==0x000600c1,"legacy ROG diagnostics trace c4 then c1");
     check(details["asusMemory"]["optionsSource"]=="model-specific-protocol-codes","legacy ROG encodings are not misreported as enumerated firmware choices");}
    {acpi={};auto p=intel();p.deviceClass="HandheldCompanion.Devices.ClawBZ2EM";Json details;
     check(!readRogSetting(p,&details)&&acpi.requests.empty()&&details["asusMemory"]["reads"].empty(),"AMD MSI identity never inherits ASUS memory protocol");}
    std::cout<<"GPU_DEDICATED_MEMORY_CHANNEL_SELFTEST_OK cases="<<cases<<" hardware=fake registry=fake\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
