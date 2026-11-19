// Narrow mode of the existing native mouse consumer: all transport/storage is memory-only.
#include <iostream>
#include <memory>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include <algorithm>
#include <vector>
#include <string>
#include <cwctype>
#include <stdexcept>
#include <optional>
#ifdef YMCC_SDM_READ_OBSERVATION_ONLY
#include <memory>
#endif
#include "json.hpp"
#include "steamdeck_mouse_vdf.h"
using json=nlohmann::json;namespace fspath=std::filesystem;using DWORD=unsigned long;
constexpr int HKEY_CURRENT_USER=1,FALSE=0;static int g_hwnd=1;
static struct{bool isolated=false;}g_aiFanMockSession;static std::map<std::wstring,std::string>disk;static json receipts,inputConfig;
static unsigned writes=0,liveMutations=0;static std::string liveMode="ok";
static std::atomic<bool>g_sdmEventPending{false};
static std::function<void()> applySleepHook,applyBackupHook;
static bool applySetReturned=false,applyLivePersist=true,applySetOwner=false,applyRejectPhaseWrite=false;
static unsigned applyBackups=0;
static std::function<void()> applyAfterSet;
static std::function<void(const std::wstring&)> applyFileReadHook;
static std::function<void()> probeBeforeOwner;static unsigned probeDiscoveryReads=0;static bool probeOwnerSupplied=false;
static void applyPostSetCheckpoint(){if(applySetReturned&&applyAfterSet){auto action=std::move(applyAfterSet);applyAfterSet={};action();}}
static void Sleep(unsigned){if(applySleepHook){auto action=std::move(applySleepHook);applySleepHook={};action();}}
using ULONGLONG=unsigned long long;using LSTATUS=long;
constexpr LSTATUS ERROR_SUCCESS=0,ERROR_NOT_READY=21;
//__PUBLIC_IDENTITY_TYPES__
static unsigned sharedSnapshotReads=0,sharedRefreshRequests=0;
static unsigned mouseEnqueueQueueCalls=0,mouseEnqueueBeginWindows=0;static bool mouseEnqueueSaveRejected=false;
static bool reconcileGetReturned=false,reconcileOwnerSupplied=false;
static unsigned reconcileGetCalls=0,reconcileSettingsWrites=0;static DWORD reconcileRegistryPid=1234;
static std::function<void()> reconcileAfterGet;
static void reconcilePostGetCheckpoint(){if(reconcileGetReturned&&reconcileAfterGet){auto action=std::move(reconcileAfterGet);reconcileAfterGet={};action();}}

static std::function<std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>()> fixtureIdentityRead;
static std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> fixtureMouseIdentity(){
 ++sharedSnapshotReads;return fixtureIdentityRead?fixtureIdentityRead():nullptr;
}
static struct{void beginWindow(){++mouseEnqueueBeginWindows;}
 using IdentitySnapshot=ymcc::steamsession::IdentitySnapshot;
 std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> identitySnapshot()const{return fixtureMouseIdentity();}
 void requestIdentityRefresh(){++sharedRefreshRequests;}
 //__PUBLIC_IDENTITY_CURRENT__
}g_steamSettingsObserver;
static std::string W2U(const std::wstring& value){return {value.begin(),value.end()};}static std::wstring U2W(const std::string& value){return {value.begin(),value.end()};}
#ifdef YMCC_SDM_READ_OBSERVATION_ONLY
//__READ_OBSERVATION_PORTS__
#else
static std::wstring sofRegSZ(int,const wchar_t*,const wchar_t*){return L"S:\\Steam";}
static bool sofRegDWORD(int,const wchar_t*,const wchar_t* name,DWORD* out){*out=std::wstring(name)==L"pid"?1234u:42u;return true;}
static bool sofFindLocalConfig(std::wstring* path,std::string* account){*path=L"S:\\Steam\\localconfig.vdf";*account="42";return true;}
static bool sofFindLocalConfigAt(const std::wstring&,DWORD,std::wstring* p,std::string* a){return sofFindLocalConfig(p,a);}
static bool sofSteamRunning(){return true;}
#endif
static std::string sgReadFile(const std::wstring& path){reconcilePostGetCheckpoint();applyPostSetCheckpoint();if(applyFileReadHook)applyFileReadHook(path);
#ifdef YMCC_SDM_READ_OBSERVATION_ONLY
 if(readSourceHook)readSourceHook();
#endif
 return disk[path];}
static bool CopyFileW(const wchar_t* from,const wchar_t* to,int){++applyBackups;disk[to]=disk[from];if(applyBackupHook){auto action=std::move(applyBackupHook);applyBackupHook={};action();}return true;}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& value){++writes;disk[path]=value;return true;}
static json ymSettingsSection(const char*){return receipts;}static bool ymSettingsWriteSection(const char*,const json& value){++reconcileSettingsWrites;if(mouseEnqueueSaveRejected)return false;if(applyRejectPhaseWrite&&value.value("phase",std::string())=="live-inflight")return false;receipts=value;return true;}
static json effectiveRuntimeInputSettings(){return inputConfig;}
static void ipc_emit(const char*,const json&){}static void appendNativeLifecycleLog(const char*,const json&){}
static bool steamLiveCanDefer(const json&){return false;}
static void steamSettingsQueue(const char*){++mouseEnqueueQueueCalls;}
static json steamLiveRequest(DWORD account,json request,const std::function<bool()>& admitted={},const std::function<bool()>& currentOwner={}){
 const bool probeGet=request.value("operation",std::string{})=="mouse.get";
 if(probeGet){probeOwnerSupplied=bool(currentOwner);if(probeBeforeOwner){auto hook=std::move(probeBeforeOwner);probeBeforeOwner={};hook();}}
 if(currentOwner&&!currentOwner())return {{"ok",false},{"reason","live-action-cancelled"}};
 if(probeGet)++probeDiscoveryReads; // Controlled discovery leaf follows the original public early owner gate.
 if(account!=42)return {{"ok",false},{"reason","steam-account-changed"}};
 const auto file=U2W(request.value("path",std::string{}));const auto current=ymcc::steamdeck::desktopLayout(disk[file]).sensitivity;
 if(request.value("operation",std::string{})=="mouse.get"){
  if(currentOwner&&!currentOwner())return {{"ok",false},{"reason","live-action-cancelled"}};
  ++reconcileGetCalls;reconcileOwnerSupplied=bool(currentOwner);reconcileGetReturned=true;
  return {{"ok",true},{"percent",current}};
 }
 if(request.value("baseline",-1)!=current)return {{"ok",false},{"reason","desktop-sensitivity-changed"}};
 ++liveMutations;applySetOwner=bool(currentOwner);if(applyLivePersist)disk[file]=ymcc::steamdeck::replaceSensitivity(disk[file],request.at("percent").get<int>());applySetReturned=true;return {{"ok",true},{"percent",request.at("percent")},{"saved",true}};
}
//__PRODUCTION_RUNTIME__
static const std::wstring path=L"S:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\413080\\controller_neptune.vdf";
static const std::wstring selection=L"S:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\configset_controller_neptune.vdf";
static const std::string fixture=R"VDF("controller_mappings" { "controller_type" "controller_neptune" "group" { "id" "77" "mode" "joystick_mouse" "settings" { "sensitivity" "145" } } "preset" { "id" "0" "name" "Default" "group_source_bindings" { "77" "right_joystick active" } } })VDF";
static void reset(){sharedSnapshotReads=sharedRefreshRequests=0;probeBeforeOwner={};probeDiscoveryReads=0;probeOwnerSupplied=false;reconcileGetCalls=0;fixtureIdentityRead=[] {
 auto receipt=std::make_shared<ymcc::steamsession::IdentitySnapshot>();receipt->registryRoot=L"S:\\Steam";receipt->rootReadError=receipt->accountReadError=receipt->registeredPidReadError=ERROR_SUCCESS;
 receipt->presence=1;receipt->session={L"S:\\Steam",1234,42,100};receipt->registeredPid=1234;receipt->processEpoch=receipt->accountEpoch=1;return receipt;};disk.clear();disk[path]=fixture;disk[selection]="\"controller_config\" { \"413080\" { \"autosave\" \"1\" } }";receipts=json::object();inputConfig={{"outputTarget",{{"buttonMappingEnabled",true},{"persona","steamdeck"}}}};writes=liveMutations=0;g_sdmEventPending=false;g_sdmReceiptError.clear();}
static void expect(bool ok,const char* label){if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
int main(){try{
#ifdef YMCC_SDM_READ_OBSERVATION_ONLY
//__READ_OBSERVATION_CASES__
std::cout<<"STEAMDECK_MOUSE_READ_OBSERVATION_OK hardware=0 services=0 disk=memory\n";
#else
//__RECEIPT_STORM__
std::cout<<"STEAMDECK_MOUSE_RECEIPT_ONLY_OK hardware=0 services=0 disk=memory\n";
#endif
return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
