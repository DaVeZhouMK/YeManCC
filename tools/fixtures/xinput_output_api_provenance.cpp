// Optional case of the existing manual feedback consumer producer. Real files,
// production SHA256/export functions; module APIs are memory-only and no API
// function pointer is invoked. Never loads a real XInput DLL or opens a device.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincrypt.h>
#include <Xinput.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
static std::string W2U(const std::wstring& value){
 const int n=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
 std::string out(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),out.data(),n,nullptr,nullptr);return out;
}
static std::wstring U2W(const std::string& value){
 const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
 std::wstring out(n,0);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n);return out;
}
/*ACTUAL-SHA-CONSUMER*/
static unsigned hashCalls=0;static int hashFault=0;
static std::string sha256File(const std::wstring& path){
 ++hashCalls;if(hashFault==1)return {};if(hashFault==2)return "wrong-digest";
 if(hashFault==3)throw std::runtime_error("injected hash failure");return sha256FileCancellable(path,{});
}
enum class YmccFamily{OneXPlayer,Aokzoe,AsusRogAlly,MsiClaw,Unknown};
static struct {YmccFamily family=YmccFamily::OneXPlayer;}g_machineIdentity;
using YmccXInputGetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
using YmccXInputSetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
static unsigned outputCalls=0,loadCalls=0,procCalls=0,moduleCalls=0,pathCalls=0;
static DWORD WINAPI fixtureRead(DWORD,XINPUT_STATE*){++outputCalls;return ERROR_INVALID_PARAMETER;}
static DWORD WINAPI fixtureWrite(DWORD,XINPUT_VIBRATION*){++outputCalls;return ERROR_ACCESS_DENIED;}
static HMODULE g_ymccXInput14Module=nullptr;
static YmccXInputGetStateFn g_ymccXInputGetState14=fixtureRead;
static YmccXInputSetStateFn g_ymccXInputSetState14=nullptr;
using YmccOpenXInputGetDevicePathFn=DWORD(WINAPI*)(DWORD,WCHAR*,DWORD*);
static YmccOpenXInputGetDevicePathFn g_ymccOpenXInputGetDevicePath=nullptr;
static std::atomic<bool> g_ymccOpenXInputAvailable{false};
static std::once_flag g_ymccXInputSetState14Once;
static bool splitOwners=false,ownerFails=false,pathFails=false,pathTruncated=false;
static std::wstring readPath,writePath;static json apiLogs=json::array();
static HMODULE fixtureLoad(LPCWSTR){++loadCalls;return reinterpret_cast<HMODULE>(uintptr_t(11));}
static unsigned capsCalls=0;static bool capsFail=false;
static DWORD WINAPI fixtureCaps(DWORD,DWORD,XINPUT_CAPABILITIES* caps){++capsCalls;if(capsFail)return ERROR_ACCESS_DENIED;caps->Type=1;caps->SubType=1;caps->Vibration.wLeftMotorSpeed=caps->Vibration.wRightMotorSpeed=255;return ERROR_SUCCESS;}
static FARPROC fixtureProc(HMODULE,LPCSTR name){++procCalls;if(std::strcmp(name,"XInputGetCapabilities")==0)return reinterpret_cast<FARPROC>(fixtureCaps);return std::strcmp(name,"XInputSetState")==0?reinterpret_cast<FARPROC>(fixtureWrite):reinterpret_cast<FARPROC>(fixtureRead);}
static BOOL fixtureModule(DWORD,LPCWSTR address,HMODULE* owner){
 ++moduleCalls;if(ownerFails)return FALSE;
 const auto function=reinterpret_cast<FARPROC>(const_cast<wchar_t*>(address));
 *owner=reinterpret_cast<HMODULE>(uintptr_t(splitOwners&&function==reinterpret_cast<FARPROC>(fixtureWrite)?22:11));return TRUE;
}
static DWORD fixtureModulePath(HMODULE owner,LPWSTR path,DWORD capacity){
 ++pathCalls;if(pathFails)return 0;if(pathTruncated)return capacity;
 const auto& value=reinterpret_cast<uintptr_t>(owner)==22?writePath:readPath;
 std::copy_n(value.data(),std::min<size_t>(value.size(),capacity),path);return DWORD(value.size());
}
static DWORD fixtureLastError(){return ERROR_ACCESS_DENIED;}
static void appendNativeLifecycleLog(const char* event,const json& row){apiLogs.push_back({{"event",event},{"row",row}});}
#define LoadLibraryW fixtureLoad
#define GetProcAddress fixtureProc
#define GetModuleHandleExW fixtureModule
#define GetModuleFileNameW fixtureModulePath
#define GetLastError fixtureLastError
/*ACTUAL-PROVENANCE-CONSUMERS*/
/*ACTUAL-TARGET-CONSUMER*/
#undef LoadLibraryW
#undef GetProcAddress
#undef GetModuleHandleExW
#undef GetModuleFileNameW
#undef GetLastError
static std::string ordinaryManifest(){std::ostringstream manifest;
/*ACTUAL-MANIFEST-CONSUMER*/
 return manifest.str();
}
static unsigned checks=0;static json observations=json::array();
static void check(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
static json exported(){
 const auto text=ordinaryManifest();const std::string prefix="xinputOutputApiFileEvidence=";
 check(text.starts_with(prefix)&&text.back()=='\n',"actual ordinary manifest contains one JSON field");
 const auto row=json::parse(text.substr(prefix.size()));
 check(!row.at("physicalEffectVerified").get<bool>(),"export cannot claim a motor response");
 if(row.value("status",std::string{})!="unavailable"){
  check(row.at("hardwareWriteIssued")==false&&row.at("communicationEnableStateObserved")==false,"file identity never grants output or communication proof");
  check(row.at("scope")=="export-time-disk;not-loaded-image-proof;not-atomic-with-entry-point-observation","disk identity retains its exact time/proof boundary");
 }
 observations.push_back(row);return row;
}
static void storeObservation(const json& value){g_xinputOutputApiEvidence.store(std::make_shared<const json>(value),std::memory_order_release);}
static unsigned pathSizeCalls=0,pathReadCalls=0;static int pathFault=0;
static std::wstring mappedPath=L"\\\\?\\usb#vid_045e&pid_028e#physical";
static DWORD WINAPI fixtureTargetPath(DWORD,WCHAR* buffer,DWORD* count){
 if(!buffer){++pathSizeCalls;*count=DWORD(mappedPath.size()+1);if(pathFault==1)return ERROR_ACCESS_DENIED;if(pathFault==2)*count=0;if(pathFault==3)*count=0xffffffffu;return ERROR_SUCCESS;}
 ++pathReadCalls;const DWORD capacity=*count;
 if(pathFault==4)return ERROR_ACCESS_DENIED;
 if(pathFault==5){*count=capacity+1;return ERROR_SUCCESS;}
 if(pathFault==6){std::fill_n(buffer,capacity,L'X');return ERROR_SUCCESS;}
 if(pathFault==7){buffer[0]=0;return ERROR_SUCCESS;}
 if(pathFault==10){buffer[0]=L'P';return ERROR_SUCCESS;}
 std::copy(mappedPath.begin(),mappedPath.end(),buffer);buffer[mappedPath.size()]=0;
 *count=pathFault==8?DWORD(mappedPath.size()):pathFault==9?0:capacity;return ERROR_SUCCESS;
}
static int targetProbeMain(){try{
 g_ymccXInput14Module=reinterpret_cast<HMODULE>(uintptr_t(11));readPath=L"C:\\memory\\proxy.dll";writePath=L"C:\\memory\\writer.dll";
 auto row=nativeManualXInputTargetEvidence(0);observations.push_back(row);
 check(row.at("status")=="unavailable"&&row.at("reason")=="path-provider-not-published","unpublished resolver does not expose non-atomic pointers");
 check(loadCalls==0&&procCalls==0&&pathSizeCalls==0&&pathReadCalls==0,"unpublished resolver has no module or path calls");
 g_ymccOpenXInputAvailable.store(true,std::memory_order_release);
 row=nativeManualXInputTargetEvidence(0);observations.push_back(row);
 check(row.at("status")=="unavailable"&&row.at("attempted")==false,"missing resolved export is an unknown observation");
 check(loadCalls==0&&procCalls==0&&pathSizeCalls==0&&pathReadCalls==0,"unknown export cannot load or query a provider");
 g_ymccOpenXInputGetDevicePath=fixtureTargetPath;
 const auto inspect=[&](const json& value){
  observations.push_back(value);check(value.at("writeTargetBindingProven")==false&&value.at("physicalMotorEffectObserved")==false,"path observation never proves SetState target or motor effect");
  check(value.at("scope")=="request-time-provider-slot-path;not-setstate-delivery-or-motor-proof","target query retains provider-namespace boundary");
  check(value.at("completedTick").get<ULONGLONG>()>=value.at("startedTick").get<ULONGLONG>(),"request-time window is retained");
 };
 for(DWORD slot:{0u,3u}){
  const auto sizeBefore=pathSizeCalls,readBefore=pathReadCalls;row=nativeManualXInputTargetEvidence(slot);inspect(row);
  check(row.at("status")=="observed"&&row.at("path")==W2U(mappedPath),"valid current provider path observed");
  check(pathSizeCalls==sizeBefore+1&&pathReadCalls==readBefore+1,"successful observation has exactly two bounded metadata calls");
 }
 const auto beforeSize=pathSizeCalls,beforeRead=pathReadCalls;row=nativeManualXInputTargetEvidence(4);inspect(row);
 check(row.at("attempted")==false&&pathSizeCalls==beforeSize&&pathReadCalls==beforeRead,"out-of-range slot has no provider call");
 for(int fault=1;fault<=10;++fault){
  pathFault=fault;const auto sizeBefore=pathSizeCalls,readBefore=pathReadCalls;
  row=nativeManualXInputTargetEvidence(0);inspect(row);
  check(row.at("status")== (fault==8?"observed":"unavailable"),"size/read/growth/termination/count faults cannot create a confirmed path");
  check(pathSizeCalls==sizeBefore+1&&pathReadCalls==readBefore+(fault<=3?0:1),"failure path performs no retry or extra metadata read");
  if(fault!=8)check(row.at("path").is_null(),"failed path remains null without partial or invented identity");
 }pathFault=0;
 mappedPath=std::wstring(1023,L'P');row=nativeManualXInputTargetEvidence(0);inspect(row);check(row.at("status")=="observed","maximum declared character bound admitted");
 mappedPath+=L'Q';const auto readBefore=pathReadCalls;row=nativeManualXInputTargetEvidence(0);inspect(row);check(row.at("status")=="unavailable"&&pathReadCalls==readBefore,"oversized path cannot allocate/read a provider buffer");
 mappedPath=L"\\\\?\\hid#hidmaestro_vid_045e_pid_0b00#virtual";row=nativeManualXInputTargetEvidence(0);inspect(row);
 check(row.at("path")==W2U(mappedPath)&&row.at("writeTargetBindingProven")==false,"virtual-looking provider path is retained as evidence without reassigning an output");
 splitOwners=true;row=nativeManualXInputTargetEvidence(0);inspect(row);check(row.at("sameFunctionModuleKnown")==true&&row.at("sameFunctionModule")==false,"different query/output function modules remain different");splitOwners=false;
 ownerFails=true;row=nativeManualXInputTargetEvidence(0);inspect(row);check(row.at("status")=="observed"&&row.at("sameFunctionModuleKnown")==false&&row.at("sameFunctionModule").is_null(),"unknown module provenance does not become a namespace or output proof");ownerFails=false;
 capsFail=false;auto caps=nativeManualXInputCapabilitiesEvidence(0);check(caps.at("rc")==0&&caps.at("leftMotorMaximum")==255,"existing capability fields retained");
 check(caps.at("targetIdentity").at("path")==W2U(mappedPath),"actual manual capabilities consumer carries target identity into existing receipt");
 capsFail=true;caps=nativeManualXInputCapabilitiesEvidence(0);check(caps.at("rc")==ERROR_ACCESS_DENIED&&caps.at("targetIdentity").at("status")=="observed","failed capability query does not erase independent target observation");
 check(outputCalls==0&&loadCalls==0&&hashCalls==0,"target evidence never invokes output, loads DLLs or hashes files");
 std::ofstream("PROVENANCE-RESULT.json")<<json{{"status","PASS"},{"checks",checks},{"observations",observations},{"actualTargetProbe",true},{"actualManualCapsConsumer",true},{"pathSizeCalls",pathSizeCalls},{"pathReadCalls",pathReadCalls},{"hardwareIo",false},{"sourceSelectionChanged",false},{"physicalAccepted",false}}.dump(2);
 std::cout<<"XINPUT_TARGET_PROVENANCE_OK checks="<<checks<<" hardware_io=none\n";return 0;
 }catch(const std::exception& e){std::ofstream("PROVENANCE-RESULT.json")<<json{{"status","FAIL"},{"checks",checks},{"reason",e.what()},{"observations",observations}}.dump(2);std::cerr<<e.what()<<'\n';return 1;}}
int main(int argc,char** argv){
#ifdef YMCC_XINPUT_TARGET_PROBE
 (void)argc;(void)argv;return targetProbeMain();
#else
try{
 check(reinterpret_cast<FARPROC>(fixtureRead)!=reinterpret_cast<FARPROC>(fixtureWrite),"fixture entry points must have distinct addresses under release folding");
 if(argc!=2)throw std::runtime_error("fresh private output root required");
 const auto root=std::filesystem::path(U2W(argv[1]));
 check(!std::filesystem::exists(root)&&std::filesystem::create_directory(root),"private file root must be fresh");
 const auto a=root/L"proxy-a.bin",b=root/L"proxy-b.bin";
 {std::ofstream f(a,std::ios::binary);f<<"sample-A";}{std::ofstream f(b,std::ios::binary);f<<"sample-B";}
 readPath=a.wstring();writePath=b.wstring();
 auto row=exported();check(row.at("status")=="not-observed"&&row.at("files").empty(),"export before API use preserves absence");
 check(loadCalls==0&&procCalls==0&&moduleCalls==0&&pathCalls==0&&hashCalls==0,"export does not resolve or probe an API");
 const auto writer=ymccResolveXInputSetState14();check(writer==fixtureWrite,"actual resolver retains original resolved writer");
 check(ymccResolveXInputSetState14()==writer&&loadCalls==1&&apiLogs.size()==1,"existing once-only API resolve and log retained");
 check(hashCalls==0&&outputCalls==0,"API resolution performs no file hash or controller operation");
 const auto original=g_xinputOutputApiEvidence.load();
 check(original&&*original==apiLogs[0].at("row"),"ordinary API log and immutable cached observation are identical");
 const auto resolverCalls=std::make_tuple(loadCalls,procCalls,moduleCalls,pathCalls);
 row=exported();check(hashCalls==1,"same module is hashed once per export");
 for(const char* role:{"canonicalFunction","outputFunction"}){
  check(row["files"][role]["status"]=="observed"&&row["files"][role]["bytes"]==8,"actual file metadata reaches both roles");
  check(row["files"][role]["sha256"]==/*SHA-A*/,"production CryptoAPI SHA256 matches independent Node digest");
 }
 check(std::make_tuple(loadCalls,procCalls,moduleCalls,pathCalls)==resolverCalls&&outputCalls==0,"export never re-resolves or invokes any controller API");
 {std::ofstream f(a,std::ios::binary|std::ios::trunc);f<<"sample-A-replaced";}
 row=exported();check(hashCalls==2&&row["files"]["outputFunction"]["sha256"]==/*SHA-A2*/,"later export records current disk bytes without rebranding loaded image");
 check(*g_xinputOutputApiEvidence.load()==*original,"export cannot mutate historical entry-point observation");
 splitOwners=true;HMODULE ignored=nullptr;auto split=*original;
 split["canonicalFunction"]=nativeXInputFunctionModuleEvidence(reinterpret_cast<FARPROC>(fixtureRead),ignored);
 split["outputFunction"]=nativeXInputFunctionModuleEvidence(reinterpret_cast<FARPROC>(fixtureWrite),ignored);split["sameFunctionModule"]=false;storeObservation(split);
 unsigned before=hashCalls;row=exported();check(hashCalls==before+2,"distinct observed module paths retain two separate files");
 check(row["files"]["canonicalFunction"]["sha256"]==/*SHA-A2*/&&row["files"]["outputFunction"]["sha256"]==/*SHA-B*/,"different read/write binaries cannot collapse into one identity");
 for(int fault=1;fault<=3;++fault){
  hashFault=fault;row=exported();
  for(const char* role:{"canonicalFunction","outputFunction"})check(row["files"][role]["status"]=="unavailable"&&row["files"][role]["sha256"].is_null(),"empty/invalid/throwing hash cannot become file confirmation");
 }hashFault=0;
 for(int fault=0;fault<3;++fault){
  ownerFails=fault==0;pathFails=fault==1;pathTruncated=fault==2;
  auto missing=*original;missing["canonicalFunction"]=nativeXInputFunctionModuleEvidence(reinterpret_cast<FARPROC>(fixtureRead),ignored);missing["outputFunction"]=nativeXInputFunctionModuleEvidence(nullptr,ignored);storeObservation(missing);
  before=hashCalls;row=exported();check(hashCalls==before,"missing/truncated/unresolved module metadata performs no file lookup");
  check(row["files"]["canonicalFunction"]["status"]=="unavailable"&&row["files"]["outputFunction"]["status"]=="unavailable","missing module metadata stays unavailable");
 }ownerFails=pathFails=pathTruncated=false;
 for(const auto& path:{std::filesystem::path{},root,root/L"missing.bin"}){
  auto missing=*original;for(const char* role:{"canonicalFunction","outputFunction"})missing[role]["modulePath"]=W2U(path.wstring());storeObservation(missing);
  before=hashCalls;row=exported();check(hashCalls==before,"empty/directory/missing module path never reaches hash reader");
  check(row["files"]["outputFunction"]["status"]=="unavailable","missing file cannot be reported as a binary match");
 }
 auto malformed=*original;malformed["canonicalFunction"]["modulePathKnown"]="malformed";storeObservation(malformed);row=exported();
 check(row.at("status")=="unavailable"&&row.at("reason")=="export-evidence-exception","evidence exception is contained by actual ordinary export consumer");
 storeObservation(*original);before=hashCalls;
 for(auto family:{YmccFamily::AsusRogAlly,YmccFamily::MsiClaw,YmccFamily::Unknown}){g_machineIdentity.family=family;check(ordinaryManifest().empty(),"OneX evidence does not add work to other family exports");}
 check(hashCalls==before&&outputCalls==0,"unaffected family exports perform no new file/device operation");
 // The reported failure also occurs in AOKZOE sessions without a wrapper
 // output call. Preserve the reader's provider bytes without inventing a write.
 g_machineIdentity.family=YmccFamily::Aokzoe;g_xinputOutputApiEvidence.store(nullptr);
 const auto beforeReadOnly=std::make_tuple(loadCalls,procCalls,outputCalls);
 row=exported();check(row.at("status")=="read-module-only"&&row.at("entryPointObservation").is_null(),"AOKZOE input-only export does not invent an output resolver");
 check(row.at("files").at("canonicalFunction").at("sha256")==/*SHA-A2*/,"AOKZOE input-only export carries the actual reader module file identity");
 check(row.at("providerInternalWritesObserved")==false,"provider lifecycle writes remain unobserved");
 check(std::make_tuple(loadCalls,procCalls,outputCalls)==beforeReadOnly,"input-only export cannot load or invoke a provider");
 storeObservation(*original);row=exported();check(row.at("status")=="observed","AOKZOE retains ordinary output evidence when it exists");
 std::ofstream("PROVENANCE-RESULT.json")<<json{{"status","PASS"},{"checks",checks},{"observations",observations},{"actualProductionSha256",true},{"actualResolver",true},{"actualExportConsumer",true},{"realOwnedFiles",true},{"moduleApis","memory only"},{"hardwareIo",false},{"physicalAccepted",false}}.dump(2);
 std::cout<<"XINPUT_OUTPUT_PROVENANCE_OK checks="<<checks<<" observations="<<observations.size()<<" hardware_io=none\n";return 0;
 }catch(const std::exception& e){std::ofstream("PROVENANCE-RESULT.json")<<json{{"status","FAIL"},{"checks",checks},{"reason",e.what()},{"observations",observations}}.dump(2);std::cerr<<"XINPUT_OUTPUT_PROVENANCE_FAIL "<<e.what()<<'\n';return 1;}
#endif
}
