// Actual native origin adapter/export; all CM, clocks, cache and saves replaced.
#define NOMINMAX
#define INITGUID
#include <windows.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
unsigned checks=0,rawReads=0,cm=0,writes=0,collections=0,observations=0,snapshots=0,clocks=0;
DWORD error=12345;bool detailed=true,saveOkay=true,throwCollect=false,throwWrite=false,throwCm=false;
CONFIGRET interfaceError=CR_SUCCESS,locateError=CR_SUCCESS,instanceError=CR_SUCCESS,propertyError=CR_SUCCESS,parentError=CR_SUCCESS;
DEVPROPTYPE interfaceType=DEVPROP_TYPE_STRING,propertyType=DEVPROP_TYPE_STRING;ULONG returnedBytes=0;
bool terminated=true,oversize=false,rawFailed=false;std::wstring lastInterface,lastLocate,lastWrite,observedPath;std::string payloadBytes;
std::uintptr_t observedDevice=0;std::uint64_t observedGeneration=0,observedEventTick=0,observedQueryTick=0;
void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
DWORD getError(){return error;}void setError(DWORD value){error=value;}
ULONGLONG tick(){++clocks;error=678;return 555;}
void touch(){++cm;error=777;if(throwCm)throw std::runtime_error("fake CM");}
UINT rawName(HANDLE device,UINT kind,void* data,UINT* chars){++rawReads;check(device==reinterpret_cast<HANDLE>(65605)&&kind==RIDI_DEVICENAME,"raw name scope");if(rawFailed)return UINT(-1);const std::wstring path=L"\\\\?\\HID#SYNTHETIC";if(!data){*chars=static_cast<UINT>(path.size()+1);return 0;}check(*chars>=path.size()+1,"raw output capacity");std::memcpy(data,path.c_str(),(path.size()+1)*sizeof(wchar_t));return static_cast<UINT>(path.size());}

CONFIGRET copyString(PBYTE out,ULONG* size,const std::wstring& value){
 const auto original=*size;const auto needed=static_cast<ULONG>((value.size()+1)*sizeof(wchar_t));
 if(oversize){*size=original+2;return CR_SUCCESS;}
 if(original<needed)return CR_BUFFER_SMALL;
 std::memcpy(out,value.data(),value.size()*sizeof(wchar_t));
 if(terminated)reinterpret_cast<wchar_t*>(out)[value.size()]=0;
 *size=returnedBytes?returnedBytes:needed;return CR_SUCCESS;
}
CONFIGRET interfaceProperty(LPCWSTR path,const DEVPROPKEY* key,DEVPROPTYPE* type,PBYTE buffer,ULONG* bytes,ULONG flags){touch();lastInterface=path;check(key==&DEVPKEY_Device_InstanceId&&flags==0,"interface key/flags");*type=interfaceType;if(interfaceError!=CR_SUCCESS)return interfaceError;return copyString(buffer,bytes,L"HID\\SYNTHETIC");}
CONFIGRET locate(PDEVINST node,DEVINSTID_W id,ULONG flags){touch();check(flags==CM_LOCATE_DEVNODE_NORMAL,"locate mutation flags");lastLocate=id;*node=42;return locateError;}
CONFIGRET instance(DEVINST node,PWSTR buffer,ULONG chars,ULONG flags){touch();check(node==42&&chars==1024&&flags==0,"instance node/limit/flags");if(instanceError!=CR_SUCCESS)return instanceError;ULONG bytes=chars*sizeof(wchar_t);return copyString(reinterpret_cast<PBYTE>(buffer),&bytes,L"HID\\SYNTHETIC");}
CONFIGRET property(DEVINST node,const DEVPROPKEY* key,DEVPROPTYPE* type,PBYTE buffer,ULONG* bytes,ULONG flags){touch();check(node==42&&flags==0,"property node/flags");*type=propertyType;if(propertyError!=CR_SUCCESS)return propertyError;
 const std::wstring value=key==&DEVPKEY_Device_Service?L"service-example":key==&DEVPKEY_Device_DriverInfPath?L"oem124.inf":key==&DEVPKEY_Device_DriverProvider?L"Netease":key==&DEVPKEY_Device_Manufacturer?L"maker":key==&DEVPKEY_Device_DeviceDesc?L"description":key==&DEVPKEY_Device_EnumeratorName?L"HID":L"INVALID";
 check(value!=L"INVALID","arbitrary property queried");return copyString(buffer,bytes,value);
}
CONFIGRET parent(PDEVINST parent,DEVINST node,ULONG flags){touch();check(node==42&&flags==0,"parent node/flags");*parent=99;return parentError;}
}
namespace ymcc::keyboardEvents {struct Event {std::uintptr_t device=0;std::uint64_t deviceGeneration=0,tick=0;};}
namespace ymcc::keyboardOrigin {
struct ResolveResult{bool known;std::uint32_t node,error;};struct StringResult{bool known;std::uint32_t error;std::string value;};struct ParentResult{bool known,root;std::uint32_t node,error;};
struct Snapshot{bool known=true;};
struct Cache{
 void observe(const std::wstring& path,std::uintptr_t device,std::uint64_t gen,std::uint64_t event,std::uint64_t query)noexcept{++fake::observations;fake::observedPath=path;fake::observedDevice=device;fake::observedGeneration=gen;fake::observedEventTick=event;fake::observedQueryTick=query;}
 Snapshot snapshot(){++fake::snapshots;return {};}
};
template<class Ops> json collect(bool enabled,Ops& ops){
 ++fake::collections;if(fake::throwCollect)throw std::runtime_error("fake collector");json j={{"enabled",enabled},{"injectorProcessProven",false}};
 if(enabled){(void)ops.snapshot();(void)ops.now();fake::check(ops.utf8(L"abc")=="abc","utf8 forwarding");auto resolved=ops.resolve(L"\\\\?\\HID#SYNTHETIC");fake::check(resolved.known&&resolved.node==42,"resolve forwarding");auto id=ops.instance(resolved.node);fake::check(id.known&&id.value=="HID\\SYNTHETIC","instance forwarding");for(const char* name:{"service","driverInf","driverProvider","manufacturer","description","enumerator"})fake::check(ops.property(42,name).known,"property forwarding");auto p=ops.parent(42);fake::check(p.known&&!p.root&&p.node==99,"parent forwarding");}
 return j;
}
}
static std::string W2U(const std::wstring& text){std::string out;for(wchar_t c:text)out.push_back(static_cast<char>(c));return out;}
static std::string W2U(const wchar_t* text,int chars){return W2U(std::wstring(text,static_cast<std::size_t>(chars)));}
static std::atomic<std::uint64_t> g_keyboardModifierDeviceGeneration{18};
static bool domainLogEnabled(const char* name){fake::check(std::string(name)=="virtual","gate domain");return fake::detailed;}
static std::wstring SG_DIR=L"X:\\NOT-LIVE";
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& bytes){++fake::writes;fake::lastWrite=path;fake::payloadBytes=bytes;fake::error=666;if(fake::throwWrite)throw std::runtime_error("save");return fake::saveOkay;}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::tick
#define GetRawInputDeviceInfoW fake::rawName
#define CM_Get_Device_Interface_PropertyW fake::interfaceProperty
#define CM_Locate_DevNodeW fake::locate
#define CM_Get_Device_IDW fake::instance
#define CM_Get_DevNode_PropertyW fake::property
#define CM_Get_Parent fake::parent
static ymcc::keyboardOrigin::Cache g_keyboardDeviceOriginCache;
static std::atomic<std::uint64_t> g_keyboardDeviceOriginObserveFailures{0};
#include "actual_export.inc"
static std::vector<std::wstring> added;static json availability;
static void addFile(const std::wstring& path){added.push_back(path);}
static void attach(){
#include "actual_attach.inc"
}
static void reset(){fake::rawReads=fake::cm=fake::writes=fake::collections=fake::observations=fake::snapshots=fake::clocks=0;fake::error=12345;fake::detailed=fake::saveOkay=fake::terminated=true;fake::throwCollect=fake::throwWrite=fake::throwCm=fake::oversize=fake::rawFailed=false;fake::interfaceError=fake::locateError=fake::instanceError=fake::propertyError=fake::parentError=CR_SUCCESS;fake::interfaceType=fake::propertyType=DEVPROP_TYPE_STRING;fake::returnedBytes=0;fake::payloadBytes.clear();added.clear();availability={{"files",json::array()}};}
int main(){try{
 reset();auto path=exportKeyboardDeviceOriginEvidence();auto j=json::parse(fake::payloadBytes);fake::check(!path.empty()&&fake::cm==10&&j["nativeCmCalls"]==10&&j["nativeCmCallLimit"]==160&&j["nativeCmBudgetExhausted"]==false,"native read-only forwarding/count");fake::check(j["nativeEvidenceVersion"]==1&&j["exportEntry"]=="logs.exportAll"&&fake::error==12345,"export identity/error");
 reset();fake::detailed=false;path=exportKeyboardDeviceOriginEvidence();j=json::parse(fake::payloadBytes);fake::check(!path.empty()&&j["enabled"]==false&&fake::cm==0&&fake::snapshots==0&&fake::clocks==0,"disabled collector queried device/cache/clock");
 for(unsigned n=0;n<4;++n){reset();fake::saveOkay=n!=0;fake::throwCollect=n==1;fake::throwWrite=n==2;fake::throwCm=n==3;fake::check(exportKeyboardDeviceOriginEvidence().empty()&&fake::error==12345,"failed export stale path/error");}
 reset();KeyboardDeviceOriginEvidenceOps ops;for(unsigned i=0;i<160;++i)fake::check(ops.instance(42).known,"native cap prematurely refused");fake::check(!ops.instance(42).known&&!ops.resolve(L"ignored").known&&!ops.property(42,"service").known&&!ops.parent(42).known&&fake::cm==160&&ops.cmBudgetExhausted,"native cap called CM after limit");
 reset();ops={};ops.cmCalls=159;fake::check(!ops.resolve(L"\\\\?\\HID#SYNTHETIC").known&&fake::cm==1&&ops.cmCalls==160&&ops.cmBudgetExhausted,"second resolve call exceeded cap");
 reset();ops={};fake::check(!ops.property(42,"arbitrary").known&&fake::cm==0&&ops.cmCalls==0,"unlisted property read");
 reset();ops={};fake::parentError=CR_NO_SUCH_DEVNODE;auto parent=ops.parent(42);fake::check(!parent.known&&!parent.root&&parent.node==0&&parent.error==CR_NO_SUCH_DEVNODE,"missing node incorrectly certified a root");
 reset();ops={};fake::parentError=CR_ACCESS_DENIED;parent=ops.parent(42);fake::check(!parent.known&&!parent.root&&parent.node==0&&parent.error==CR_ACCESS_DENIED,"parent failure leaked output node");
 for(unsigned mode=0;mode<5;++mode){reset();ops={};if(mode==0)fake::interfaceError=CR_ACCESS_DENIED;if(mode==1)fake::interfaceType=DEVPROP_TYPE_UINT32;if(mode==2)fake::terminated=false;if(mode==3)fake::oversize=true;if(mode==4)fake::returnedBytes=3;fake::check(!ops.resolve(L"\\\\?\\HID#SYNTHETIC").known&&fake::cm==1,"invalid interface buffer/type used for locate");}
 reset();ops={};fake::locateError=CR_NO_SUCH_DEVNODE;fake::check(!ops.resolve(L"\\\\?\\HID#SYNTHETIC").known&&fake::cm==2,"locate failure ignored");
 for(unsigned mode=0;mode<5;++mode){reset();ops={};if(mode==0)fake::propertyError=CR_ACCESS_DENIED;if(mode==1)fake::propertyType=DEVPROP_TYPE_UINT32;if(mode==2)fake::terminated=false;if(mode==3)fake::oversize=true;if(mode==4)fake::returnedBytes=3;fake::check(!ops.property(42,"service").known,"invalid property exported");}
 reset();fake::terminated=false;ops={};fake::check(!ops.instance(42).known,"unterminated instance accepted");
 reset();ymcc::keyboardEvents::Event e{65605,18,1074921};const std::wstring keyboard=L"\\\\?\\HID#EXAMPLE";keyboardDeviceOriginObserveAtDrain(e,keyboard.c_str(),keyboard.size());fake::check(fake::observations==1&&fake::observedPath==keyboard&&fake::observedDevice==65605&&fake::observedGeneration==18&&fake::observedEventTick==1074921&&fake::observedQueryTick==555&&fake::error==12345,"existing-drain observation identity/error");
 reset();fake::detailed=false;keyboardDeviceOriginObserveAtDrain(e,keyboard.c_str(),keyboard.size());fake::check(fake::observations==0&&fake::clocks==0&&fake::error==12345,"disabled observation work");
 reset();unsigned rawBudget=4;auto raw=keyboardModifierRawNameAtDrain(e,rawBudget);fake::check(raw["status"]=="name-observed-at-drain"&&fake::observations==1&&fake::rawReads==2&&rawBudget==3&&fake::error==12345,"actual successful raw drain did not remember origin");
 reset();rawBudget=4;fake::rawFailed=true;raw=keyboardModifierRawNameAtDrain(e,rawBudget);fake::check(raw["status"]=="size-query-failed"&&fake::observations==0&&fake::error==12345,"failed raw query remembered origin");
 reset();rawBudget=0;raw=keyboardModifierRawNameAtDrain(e,rawBudget);fake::check(raw["status"]=="query-budget-exhausted"&&fake::observations==0&&fake::rawReads==0,"raw name cap bypassed");
 reset();rawBudget=4;e.deviceGeneration=17;raw=keyboardModifierRawNameAtDrain(e,rawBudget);fake::check(raw["status"]=="device-generation-changed"&&fake::observations==0&&fake::rawReads==0,"stale raw generation remembered");
 reset();attach();fake::check(added.size()==1&&availability["files"].empty(),"attachment missing");reset();fake::saveOkay=false;attach();fake::check(added.empty()&&availability["files"].size()==1&&availability["files"][0]["missingReason"]=="current-keyboard-origins-not-saved","stale attachment copied");
 std::cout<<json{{"pass",true},{"checks",fake::checks},{"actualCmCalls",0},{"actualInput",false},{"collectorReplaced",true}}.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
