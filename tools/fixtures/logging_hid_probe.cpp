// Actual diagnostic function and Native envelope, with memory-only Win32/HID ports.
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "log_snapshot_state.h"
using json=nlohmann::json;
static bool failNextAllocation=false,countAllocations=false;
static unsigned long long allocations=0;
void* operator new(std::size_t size){if(failNextAllocation){failNextAllocation=false;throw std::bad_alloc();}if(countAllocations)++allocations;if(auto p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
using DWORD=unsigned long;using USHORT=unsigned short;using BYTE=unsigned char;using ULONGLONG=unsigned long long;
using HANDLE=void*;using HDEVINFO=void*;using PHIDP_PREPARSED_DATA=void*;
struct GUID{unsigned long a;unsigned short b,c;unsigned char d[8];};
struct SP_DEVICE_INTERFACE_DATA{DWORD cbSize;};
struct SP_DEVICE_INTERFACE_DETAIL_DATA_W{DWORD cbSize;wchar_t DevicePath[64];};
struct HIDD_ATTRIBUTES{DWORD Size;USHORT VendorID,ProductID;};
struct HIDP_CAPS{USHORT InputReportByteLength;};
static HANDLE INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
static constexpr int DIGCF_PRESENT=1,DIGCF_DEVICEINTERFACE=2,GENERIC_READ=4,GENERIC_WRITE=8,
    FILE_SHARE_READ=16,FILE_SHARE_WRITE=32,OPEN_EXISTING=64,FILE_ATTRIBUTE_NORMAL=128,HIDP_STATUS_SUCCESS=0;
enum class Fault{none,buffer,report,hex,logArgument};
struct Ports{unsigned clocks=0,lists=0,destroys=0,enums=0,opens=0,closes=0,attributes=0,preparses=0,prepFrees=0,reads=0;
    int infoActive=0,hidActive=0,prepActive=0;bool noList=false,noHandle=false,noAttributes=false,noPreparse=false,noReport=false;
    USHORT vid=0x045e,pid=0x028e,reportLen=8;Fault fault=Fault::none;} ports;
static ULONGLONG fixtureTick=10000;
static std::atomic<bool> g_domainLogEnabled[2]={false,false};
static ULONGLONG GetTickCount64(){++ports.clocks;return fixtureTick;}
static DWORD GetCurrentProcessId(){return 12345;}
static HDEVINFO SetupDiGetClassDevsW(const GUID*,void*,void*,int){++ports.lists;if(ports.noList)return INVALID_HANDLE_VALUE;++ports.infoActive;return reinterpret_cast<HANDLE>(1);}
static bool SetupDiEnumDeviceInterfaces(HDEVINFO,void*,const GUID*,DWORD index,SP_DEVICE_INTERFACE_DATA*){++ports.enums;return index==0;}
static bool SetupDiGetDeviceInterfaceDetailW(HDEVINFO,SP_DEVICE_INTERFACE_DATA*,SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail,DWORD,DWORD* need,void*){
    if(need)*need=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if(!detail){if(ports.fault==Fault::buffer)failNextAllocation=true;return false;}
    detail->DevicePath[0]=L'F';detail->DevicePath[1]=0;return true;}
static HANDLE CreateFileW(const wchar_t*,int,int,void*,int,int,void*){++ports.opens;if(ports.noHandle)return INVALID_HANDLE_VALUE;++ports.hidActive;return reinterpret_cast<HANDLE>(2);}
static bool HidD_GetAttributes(HANDLE,HIDD_ATTRIBUTES* attr){++ports.attributes;if(ports.noAttributes)return false;attr->VendorID=ports.vid;attr->ProductID=ports.pid;return true;}
static bool HidD_GetPreparsedData(HANDLE,PHIDP_PREPARSED_DATA* prep){++ports.preparses;if(ports.noPreparse)return false;*prep=reinterpret_cast<HANDLE>(3);++ports.prepActive;return true;}
static int HidP_GetCaps(PHIDP_PREPARSED_DATA,HIDP_CAPS* caps){caps->InputReportByteLength=ports.reportLen;if(ports.fault==Fault::report)failNextAllocation=true;return HIDP_STATUS_SUCCESS;}
static bool HidD_FreePreparsedData(PHIDP_PREPARSED_DATA){++ports.prepFrees;--ports.prepActive;return true;}
static bool HidD_GetInputReport(HANDLE,BYTE* data,USHORT length){++ports.reads;for(unsigned i=0;i<length;++i)data[i]=static_cast<BYTE>(i);if(ports.fault==Fault::hex)failNextAllocation=true;return !ports.noReport;}
static bool CloseHandle(HANDLE){++ports.closes;--ports.hidActive;return true;}
static bool SetupDiDestroyDeviceInfoList(HDEVINFO){++ports.destroys;--ports.infoActive;return true;}
static std::string W2U(const wchar_t*){if(ports.fault==Fault::logArgument)failNextAllocation=true;return "fixture-hid";}
enum class PowerLifecycle{Ready};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{7};
static HANDLE g_hwnd=nullptr;
static const char* powerLifecycleName(PowerLifecycle){return "ready";}
static std::string nativeLifecycleTimestamp(){return "fixture";}
static std::vector<json> packets;
static void modelAdaptationObserveRecord(const char*,const json&) noexcept{}
static void virtualAdaptationObserveRecord(const char*,const json&) noexcept{}
static json nativeSteamGameLinkLogDetail(const json& value){return value;}
// The source probe matches none of the production streaming event families.
static bool nativeLifecycleRecordIsStreaming(const char*,const json&){return false;}
namespace ymcc::inputHost::diagnostics{struct Scope{static bool active(){return false;}};}
static void inputHostDeferNativeDiagnostic(const char*,const json&,bool,bool=false){}
static void mirrorLifecycleToDomains(const char*,const json&){}
static void appendNativeLifecyclePrepared(const char*,const json& value,bool,const std::string*){packets.push_back(value);}
// Controlled unavailable identity for this fake Win32/HID scope. The actual
// shared cache is verified separately by the Native writer identity consumer.
namespace ymcc::sleepfactlog {
static const json& writerCreation100nsJson(){static const json value=nullptr;return value;}
}
#include "production.h"
static json checks=json::array();static unsigned failures=0;
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});if(!pass)++failures;}
static void reset(){failNextAllocation=false;countAllocations=false;ports=Ports{};packets.clear();fixtureTick+=6000;}
static bool returned(){return ports.infoActive==0&&ports.hidActive==0&&ports.prepActive==0;}
int main(){
    g_domainLogEnabled[1]=false;reset();before::inputHostProbeHidSource();
    check("before-detail-off-still-enumerates-opens-and-reads",ports.lists==1&&ports.opens==1&&ports.reads==1&&packets.empty());
    reset();allocations=0;countAllocations=true;
    for(unsigned i=0;i<200000;++i)candidate::inputHostProbeHidSource();
    countAllocations=false;
    check("candidate-detail-off-200000-zero-clock-hid-and-allocation",ports.clocks==0&&ports.lists==0&&ports.opens==0&&ports.reads==0&&allocations==0&&packets.empty());
    reset();g_domainLogEnabled[0]=true;candidate::inputHostProbeHidSource();
    check("gyro-only-does-not-enable-virtual-diagnostic",ports.clocks==0&&ports.lists==0);
    g_domainLogEnabled[1]=true;reset();before::inputHostProbeHidSource();json old=packets.at(0);const Ports prior=ports;
    reset();candidate::inputHostProbeHidSource();json now=packets.at(0);
    check("candidate-keeps-original-enabled-probe-counts",ports.lists==prior.lists&&ports.opens==prior.opens&&ports.reads==prior.reads&&ports.prepFrees==prior.prepFrees&&returned());
    check("native-writer-retains-process-pid-and-new-hid-product-id",old["pid"]==12345&&!old.contains("hidPid")&&now["pid"]==12345&&now["hidPid"]==0x028e&&now["vid"]==0x045e);
    auto equivalent=now;equivalent.erase("hidPid");check("all-other-enabled-event-fields-preserved",equivalent==old);
    reset();fixtureTick-=5000;candidate::inputHostProbeHidSource();check("original-five-second-throttle-still-applies",ports.clocks==1&&ports.lists==0);
    reset();g_domainLogEnabled[1]=false;candidate::inputHostProbeHidSource();g_domainLogEnabled[1]=true;candidate::inputHostProbeHidSource();
    check("off-does-not-spend-a-future-enabled-probe-window",ports.clocks==1&&ports.lists==1&&returned());
    reset();ports.fault=Fault::buffer;bool oldEscaped=false;try{before::inputHostProbeHidSource();}catch(const std::bad_alloc&){oldEscaped=true;}
    failNextAllocation=false;check("before-diagnostic-allocation-escapes-and-skips-info-cleanup",oldEscaped&&ports.infoActive==1);
    bool isolated=true,throttled=true;unsigned faultCases=0;
    for(auto fault:{Fault::buffer,Fault::report,Fault::hex,Fault::logArgument}){
        reset();ports.fault=fault;bool escaped=false;try{candidate::inputHostProbeHidSource();}catch(...){escaped=true;}
        failNextAllocation=false;isolated&=!escaped&&returned();++faultCases;
        const auto listCount=ports.lists;fixtureTick+=1000;ports.fault=Fault::none;candidate::inputHostProbeHidSource();throttled&=ports.lists==listCount;
    }
    check("four-allocation-boundaries-return-existing-resources-and-do-not-escape",isolated&&faultCases==4);
    check("failed-diagnostic-does-not-refund-five-second-throttle",throttled);
    bool exits=true;
    for(unsigned scenario=0;scenario<6;++scenario){reset();ports.noList=scenario==0;ports.noHandle=scenario==1;ports.noAttributes=scenario==2;
        ports.vid=scenario==3?0x9999:0x045e;ports.noPreparse=scenario==4;ports.reportLen=scenario==5?65:8;
        candidate::inputHostProbeHidSource();exits&=returned();if(scenario>=4)exits&=ports.reads==0;}
    check("existing-empty-open-attributes-other-device-and-report-limits-preserved",exits);
    reset();candidate::inputHostProbeHidSource();check("next-real-diagnostic-after-fault-can-produce-evidence",packets.size()==1&&returned());
    std::cout<<json({{"checks",checks},{"failures",failures},{"allocationFaultBoundaries",faultCases},
        {"offObservations",200000},{"offAllocations",allocations},{"normalPacket",now},
        {"scope","actual-probe-and-native-envelope;memory-only-Win32-HID-leaves;not-device-SDK-or-business-acceptance"}}).dump(2)<<std::endl;
    return failures?3:0;
}
