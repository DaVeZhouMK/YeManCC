#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <map>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "native_foreground_source_policy.h"
#include "msi_foreground_isolation.h"
using json=nlohmann::json;
using BYTE=unsigned char;using DWORD=unsigned long;using USHORT=unsigned short;using USAGE=unsigned short;using ULONGLONG=unsigned long long;using HDEVINFO=void*;
struct GUID{int value=0;};constexpr GUID kYeManGuidDeviceInterfaceHid{};
#define INVALID_HANDLE_VALUE reinterpret_cast<void*>(-1)
constexpr DWORD DIGCF_PRESENT=2,DIGCF_DEVICEINTERFACE=16,ERROR_NO_MORE_ITEMS=259,ERROR_SUCCESS=0;
struct SP_DEVICE_INTERFACE_DATA{DWORD cbSize=0,index=0;};struct SP_DEVINFO_DATA{DWORD cbSize=0,DevInst=0;};struct SP_DEVICE_INTERFACE_DETAIL_DATA_W{DWORD cbSize=0;wchar_t DevicePath[256]{};};
/*ACTUAL_TYPES*/
std::string W2U(const std::wstring&s){std::string r;for(auto c:s)r.push_back(static_cast<char>(c));return r;}
std::wstring U2W(const std::string&s){return {s.begin(),s.end()};}
/*ACTUAL_HELPERS*/
struct Node{HidHidePhysicalIdentity identity;bool noSize=false,detailFails=false;HidResolveReject reject=HidResolveReject::None;};
std::vector<Node> nodes;DWORD enumError=ERROR_NO_MORE_ITEMS,lastError=0;bool setFails=false;
int enumCalls=0,detailCalls=0,probeCalls=0,destroyCalls=0,hideCalls=0,readbackCalls=0;
bool scope=false,eligible=true,managed=false,drift=false;unsigned managedReads=0;
HidHideMsiMainSourceRecord record;
std::atomic<bool> g_msiDinputRouteConfirmed{false};
std::mutex g_hidHidePhysicalIdentityMx;HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
using InputHostMutex=std::mutex;InputHostMutex g_inputHostMx;HidHideVisibilityJournal g_hidHideVisibilityJournal;
std::vector<std::wstring> fakeHidden;std::vector<std::string> calls;std::string failTarget;
bool readbackFails=false,fixtureReconnectCloak=true,unknownReadback=false,transactionDrift=false,knownMsi=false;
HDEVINFO SetupDiGetClassDevsW(const GUID*,void*,void*,DWORD){return setFails?INVALID_HANDLE_VALUE:reinterpret_cast<void*>(1);}
bool SetupDiEnumDeviceInterfaces(HDEVINFO,void*,const GUID*,DWORD index,SP_DEVICE_INTERFACE_DATA*iface){++enumCalls;if(index>=nodes.size()){lastError=enumError;return false;}iface->index=index;return true;}
DWORD GetLastError(){return lastError;}
bool SetupDiGetDeviceInterfaceDetailW(HDEVINFO,SP_DEVICE_INTERFACE_DATA*iface,SP_DEVICE_INTERFACE_DETAIL_DATA_W*detail,DWORD,DWORD*bytes,SP_DEVINFO_DATA*data){++detailCalls;auto&n=nodes.at(iface->index);if(!detail){if(bytes)*bytes=n.noSize?0:sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);lastError=122;return false;}if(n.detailFails)return false;auto p=U2W(n.identity.sourcePath);if(p.size()>=256)throw std::runtime_error("fixture path too long");wcscpy_s(detail->DevicePath,256,p.c_str());data->DevInst=iface->index;return true;}
bool SetupDiDestroyDeviceInfoList(HDEVINFO){++destroyCalls;return true;}
bool hidHideResolveInterfaceIdentity(const std::wstring&,SP_DEVINFO_DATA*data,HDEVINFO,HidHidePhysicalIdentity&out,HidResolveReject*reject=nullptr){++probeCalls;auto&n=nodes.at(data->DevInst);if(reject)*reject=n.reject;if(n.reject!=HidResolveReject::None)return false;out=n.identity;return true;}
bool msiControllerRepairScope(){return scope;}bool msiDinputMainSourceEligible(){return eligible;}
bool msiDinputManagedMainSourceActive(HidHideMsiMainSourceRecord&out){++managedReads;out=record;if(drift&&managedReads>1)++out.generation;return managed;}
bool deviceInterfaceContainerId(const wchar_t*,GUID*out){out->value=1;return true;}
std::string msiGuidToString(const GUID&){return "container-A";}
bool deviceInterfaceUsbInstance(const wchar_t*,std::wstring&out){out=U2W("usb-A");return true;}
unsigned long long inputHostEpochToken(){return 7;}
json hidHideIdentityJson(const HidHidePhysicalIdentity&p){return {{"id",p.physicalIdentity},{"root",p.baseContainerDeviceInstanceId}};}
void legionMotionSelectedIdentityChanging(const HidHidePhysicalIdentity&,const HidHidePhysicalIdentity&){}
bool inputHostTargetReturnBeforeHideLocked(const std::vector<HidHidePhysicalIdentity>&,
    const HidHideVisibilitySnapshot&,const HidHideVisibilitySnapshot& original,const std::string& target,const std::string&,
    bool* reserved,bool nativeOwnedReassertion){
 if(nativeOwnedReassertion)throw std::logic_error("unexpected native handoff");
 // Memory boundary: deny foreign baseline ownership; the durable store is not exercised.
 const bool admitted=!hidHideContainsPath(original.hidden,target);
 if(reserved)*reserved=admitted;
 return admitted;
}
std::function<void()> atPlanReceipt;
void appendNativeLifecycleLog(const char*event,const json&){if(std::string(event)=="hidhide.owned-game-reconnect-plan"&&atPlanReceipt)atPlanReceipt();}
bool msiClawKnownBoardAuthority(){return knownMsi;}
bool hidHideMsiAuxiliaryOwnedInstance(const std::string&s){return s.find("MI_00&COL02")!=std::string::npos||s.find("MI_01&COL03")!=std::string::npos;}
bool fakeHide(const std::wstring&p,const char*route){++hideCalls;calls.push_back(std::string(route)+":"+W2U(p));if(W2U(p)==failTarget)return false;if(!hidHideContainsPath(fakeHidden,W2U(p)))fakeHidden.push_back(p);return true;}
bool inputHostHidHideApplyViaPipeLocked(const std::wstring&op,const std::wstring&p){if(op!=L"hide")throw std::runtime_error("unexpected op");return fakeHide(p,"pipe");}
bool hidHideRun(const std::wstring&cmd){const auto a=cmd.find(L'"'),b=cmd.rfind(L'"');if(a==std::wstring::npos||b<=a)throw std::runtime_error("unexpected cli");return fakeHide(cmd.substr(a+1,b-a-1),"cli");}
bool hidHideCapturePeriodicVisibilityState(HidHidePeriodicState&out){++readbackCalls;if(transactionDrift)g_hidHideVisibilityJournal.transactionId="other-tx";if(readbackFails)return false;out.cloaking=fixtureReconnectCloak;out.hidden=fakeHidden;if(unknownReadback&&!out.hidden.empty())out.hidden.pop_back();return true;}
unsigned g_xinputPhysicalSourceMask=1,g_xinputConnectedMask=1;
ULONGLONG fixtureNow=10000,g_inputHostReapplyHideLastTick=0;
unsigned long long g_inputHostEpoch=7,g_inputHostReapplyHideRepairsEpoch=7;
std::atomic<unsigned long long> g_powerGeneration{3},g_xinputTopologyEpoch{2};
ULONGLONG GetTickCount64(){return fixtureNow;}
struct Config {bool enabled=true,virtualTargetEnabled=true;} g_realStickTest;
bool g_inputReady=true,exitFence=false,releaseWindow=false;
std::atomic<bool> g_inputHostPowerAdmissionRevoked{false};
enum class PowerLifecycle{Ready,Unavailable};std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
bool inputHostExitFenceRaised(){return exitFence;}bool physicalReleaseWindowActive(){return releaseWindow;}
using Profile=ymcc::nativeForeground::Profile;Profile fixtureProfile=Profile::Unsupported;
Profile nativeForegroundProfile(){return fixtureProfile;}
bool nativeForegroundRootCandidate(const std::string&root){return ymcc::nativeForeground::rootCandidate(fixtureProfile,root);}
bool nativeForegroundGameInputAdmitted(bool same,bool virt,unsigned vid,unsigned pid,unsigned page,unsigned usage){return ymcc::nativeForeground::gameInputAdmitted(fixtureProfile,same,virt,vid,pid,page,usage);}
bool nativeForegroundCurrentXusbTargetAdmitted(const std::string&root,const ymcc::msi::NativeForegroundXusbInterface&item){return ymcc::nativeForeground::xusbTargetAdmitted(fixtureProfile,root,item);}
std::vector<HidHideInterfaceRecord> currentInventory;bool inventoryComplete=true,xusbComplete=true,locationChanged=false,fullReadFails=false,fullValid=true,fullCloak=true,fullInverse=true;
int fullReads=0,inventoryReads=0,xusbReads=0,locationReads=0;bool retireDuringPlan=false;
std::vector<ymcc::msi::NativeForegroundXusbInterface> currentXusb;
std::vector<std::wstring> msiForegroundIsolationLocations(const std::string&){++locationReads;if(locationChanged&&locationReads>1)return {L"OTHER-PORT"};return {L"PINNED-PORT"};}
std::vector<HidHideInterfaceRecord> hidHideBuildSourceInterfaceInventory(const std::string&,const std::string&,bool*complete){++inventoryReads;*complete=inventoryComplete;if(retireDuringPlan)++g_powerGeneration;return currentInventory;}
ymcc::msi::NativeForegroundXusbInventory msiNativeForegroundXusbInventory(const HidHidePhysicalIdentity&){++xusbReads;return {xusbComplete,"fixture",currentXusb};}
std::string hidHideInverseApplicationListState(const std::string&raw){return raw=="off"?"off":raw=="on"?"on":"unknown";}
bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot&out){++fullReads;if(fullReadFails)return false;out.valid=fullValid;out.cloaking=fullCloak;out.hidden=fakeHidden;out.inverseRaw=fullInverse?"off":"";return true;}
/*ACTUAL_CONSTANTS*/
/*ACTUAL_FAMILY_PLANNER*/
/*ACTUAL_OWNED_PROGRESS*/
/*ACTUAL_FUNCTIONS*/
int g_inputHostReapplyHideRepairs=0;bool g_inputHostReapplyHideExhausted=false;
std::string g_inputHostReapplyHideEpisodeKey="7|tx-A",g_inputHostSourceSuppressLoggedRunId;

/*ACTUAL_BUDGET*/
json checks=json::array(),scenarios=json::array();
void check(const char*name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
HidHidePhysicalIdentity identity(const std::string&root="A",const std::string&child="PAD",USHORT vid=0x0B05,USHORT pid=0x1ABE){HidHidePhysicalIdentity p;p.resolved=true;p.sourcePath="\\\\?\\hid#"+root+"#"+child;std::ostringstream ids;ids<<std::uppercase<<std::hex<<std::setfill('0')<<"VID_"<<std::setw(4)<<vid<<"&PID_"<<std::setw(4)<<pid;p.deviceInstanceId="HID\\"+ids.str()+"\\"+root+"_"+child;p.baseContainerDeviceInstanceId="USB\\"+ids.str()+"\\"+root;p.vendorId=vid;p.productId=pid;p.usagePage=1;p.usage=5;p.physicalIdentity=p.deviceInstanceId+"|"+p.baseContainerDeviceInstanceId+"|VID_"+std::to_string(vid)+"|PID_"+std::to_string(pid);return p;}
void reset(){atPlanReceipt={};resetOwnedDiscovery();fixtureProfile=Profile::Unsupported;g_inputHostEpoch=7;g_powerGeneration=3;g_xinputTopologyEpoch=2;g_realStickTest={};g_inputReady=true;exitFence=releaseWindow=false;g_inputHostPowerAdmissionRevoked=false;g_powerLifecycle=PowerLifecycle::Ready;currentInventory.clear();currentXusb.clear();inventoryComplete=xusbComplete=fullValid=fullCloak=fullInverse=true;locationChanged=fullReadFails=retireDuringPlan=false;fullReads=inventoryReads=xusbReads=locationReads=0;nodes.clear();enumError=ERROR_NO_MORE_ITEMS;lastError=0;setFails=false;enumCalls=detailCalls=probeCalls=destroyCalls=hideCalls=readbackCalls=0;scope=managed=drift=false;eligible=true;managedReads=0;record={};fixtureNow=10000;g_inputHostReapplyHideLastTick=0;g_inputHostReapplyHideRepairs=0;g_inputHostReapplyHideExhausted=false;g_inputHostReapplyHideEpisodeKey="7|tx-A";g_xinputPhysicalSourceMask=g_xinputConnectedMask=1;g_hidHidePhysicalIdentity={};g_hidHideVisibilityJournal={};fakeHidden.clear();calls.clear();failTarget.clear();readbackFails=unknownReadback=transactionDrift=knownMsi=false;fixtureReconnectCloak=true;}
std::vector<std::string> narrow(const std::vector<std::wstring>&v){std::vector<std::string>r;for(auto&p:v)r.push_back(W2U(p));return r;}
const std::wstring extra=L"HID\\VID_0B05&PID_1ABE&IG_01\\A_GAME_SIBLING",xusb=L"USB\\VID_0B05&PID_1ABE\\A_XUSB",foreign=L"HID\\FOREIGN_OWNER\\KEEP",aux=L"HID\\VID_0DB0&PID_1902&MI_00&COL02\\A";
HidHideVisibilityJournal prepare(const HidHidePhysicalIdentity&p){nodes={{p}};g_hidHidePhysicalIdentity=p;auto&j=g_hidHideVisibilityJournal;j.active=true;j.transactionId="tx-A";j.physical=p;j.physicalSources={p};j.hiddenByUs={U2W(p.baseContainerDeviceInstanceId),U2W(p.deviceInstanceId),extra,xusb};j.before.hidden={foreign};return j;}
void observe(const char*name,bool ok,bool issued){scenarios.push_back({{"name",name},{"returned",ok},{"issued",issued},{"hideCalls",hideCalls},{"readbackCalls",readbackCalls},{"owned",narrow(g_hidHideVisibilityJournal.hiddenByUs)},{"selectedRoot",g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId},{"calls",calls}});}
// Explicit periodic-capability selection only. Existing reconnect scenarios
// keep their original readback stub and assertions. This wrapper comes from
// current production source; aliases below are private observation/I/O ports.
#include <chrono>
#include <thread>
#include "input_host_transaction_mutex.h"
static std::atomic<unsigned> fixturePeriodicLockAttempts{0};
struct FixturePeriodicBackendMutex {
    std::mutex mutex;
    void lock(){fixturePeriodicLockAttempts.fetch_add(1,std::memory_order_release);mutex.lock();}
    bool try_lock(){return mutex.try_lock();}
    void unlock(){mutex.unlock();}
};
using FixturePeriodicMutex=ymcc::inputHost::TransactionMutex<FixturePeriodicBackendMutex>;
static FixturePeriodicMutex fixturePeriodicMx;
static unsigned fixturePeriodicUnlockedReads=0,fixturePeriodicPipeCalls=0,fixturePeriodicFullCalls=0;
static unsigned fixturePeriodicCliCalls=0,fixturePeriodicUnsupportedQueries=0,fixturePeriodicQueryUnlocked=0;
static unsigned fixturePeriodicCliLocked=0,fixturePeriodicHostRevision=1,fixturePeriodicQueriedRevision=0;
static bool fixturePeriodicPipeOk=true,fixturePeriodicFullOk=true,fixturePeriodicFullValid=true,fixturePeriodicCliOk=true;
static std::function<void()> fixturePeriodicBeforeLock;
struct FixturePeriodicCapability {
    bool value=false;
    operator bool(){
        const bool observed=value;
        if(!fixturePeriodicMx.ownedByCurrentThread()){
            ++fixturePeriodicUnlockedReads;
            if(fixturePeriodicBeforeLock){auto callback=std::move(fixturePeriodicBeforeLock);fixturePeriodicBeforeLock={};callback();}
        }
        return observed;
    }
};
static FixturePeriodicCapability fixturePeriodicCapability;
struct FixturePlayerView{bool locked=false;std::string phase="success",persona="steamdeck";};
struct FixturePlayer{FixturePlayerView state;FixturePlayerView view(ULONGLONG)const{return state;}};
static FixturePlayer fixturePlayer;
static bool fixtureActive=true,fixtureHello=true,fixturePipeRetired=false,fixtureQuarantined=false,fixtureTransportFault=false,fixtureHealthAllowed=true;
static std::atomic<bool> fixtureRecoveryPending{false},fixtureSwitchPending{false},fixtureRuntimePending{false},fixtureTuplePending{false},fixtureWakePending{false};
static void* fixtureHostProcess=reinterpret_cast<void*>(1);
static std::string fixtureRunId="owned-host-run",fixtureConfigured="steamdeck",fixtureBound="steamdeck";
static unsigned long long fixtureSequence=77,fixtureEpoch=7;
static DWORD fixtureAllowance=0;static ULONGLONG fixtureQueryElapsed=0;
static bool fixtureReplyUnknown=false;static json fixtureSlowLogs=json::array();
struct FixtureHealthKey{bool allowed=true;};
static FixtureHealthKey fixtureHealthContextLocked(){return{fixtureHealthAllowed};}
static std::string fixtureConfiguredPersona(){return fixtureConfigured;}
static std::string fixtureBoundPersona(){return fixtureBound;}
static void fixturePeriodicLog(const char*,const json& detail){fixtureSlowLogs.push_back(detail);}
static bool fixtureRequestLocked(const char* command,const json&,const char* status,DWORD allowance,json* receipt){
 ++fixturePeriodicPipeCalls;fixtureAllowance=allowance;fixturePeriodicQueriedRevision=fixturePeriodicHostRevision;
 if(!fixturePeriodicMx.ownedByCurrentThread())++fixturePeriodicQueryUnlocked;
 if(std::string(command)!="HIDHIDE_QUERY_STATE"||std::string(status)!="hidhide-query-state")throw std::logic_error("unexpected-query-port");
 if(!fixturePeriodicCapability.value){++fixturePeriodicUnsupportedQueries;return false;}
 fixtureNow+=fixtureQueryElapsed;
 if(!fixturePeriodicPipeOk)return false;
 const json detail=fixtureReplyUnknown?json{{"ok",false}}:json{{"ok",true},{"cloakOn",true},{"hidden",json::array({"selected-current-root"})}};
 *receipt=json{{"detail",detail.dump()}};++fixtureSequence;return true;
}

// Existing periodic consumer, with actual production CLI parsers and snapshot bodies.
// All command execution remains an inert boundary: never execute HidHideCLI.
static bool fixtureUseActualCli=false,fixtureCliSeam=false,fixtureQueryFailByExit=false;
static bool fixtureQueryCloak=true;
static unsigned fixtureCliSeamSkips=0;
static std::wstring fixtureQueryFailOperation;
static std::vector<std::wstring> fixtureQueryHidden{L"selected-current-root"};
static std::vector<std::wstring> fixtureQueryCommands;
static unsigned fixtureQueryCommandsUnderOwner=0;
static bool ymStartSeamActive(){return fixtureCliSeam;}
static void ymStartSeamTrace(const char* event){
    if(std::string(event)!="visibility-cli-skip")throw std::logic_error("unexpected-seam-trace");
    ++fixtureCliSeamSkips;
}
static bool fixtureQueryCli(const std::wstring& command,std::string* output,DWORD* code){
    fixtureQueryCommands.push_back(command);
    if(fixturePeriodicMx.ownedByCurrentThread())++fixtureQueryCommandsUnderOwner;
    output->clear();*code=ERROR_SUCCESS;
    if(command==fixtureQueryFailOperation){*code=5;return fixtureQueryFailByExit;}
    if(command==L"--dev-list"){
        for(const auto& p:fixtureQueryHidden)*output+="--dev-hide \""+W2U(p)+"\"\n";
    }else if(command==L"--cloak-state")*output=fixtureQueryCloak?"--cloak-on\n":"--cloak-off\n";
    else if(command==L"--app-list")*output="--app-reg \"fixture-owned-app\"\n";
    else if(command==L"--inv-state")*output="--inv-off\n";
    else throw std::logic_error("unexpected-query-command");
    return true;
}

/*ACTUAL_PERIODIC_HELPERS*/
static bool fixturePeriodicFull(HidHideVisibilitySnapshot& output){
    ++fixturePeriodicFullCalls;fixturePeriodicQueriedRevision=fixturePeriodicHostRevision;
    if(fixtureUseActualCli)return fixtureActualFullCliSnapshot(output);
    if(!fixturePeriodicMx.ownedByCurrentThread())++fixturePeriodicQueryUnlocked;
    output={};output.valid=fixturePeriodicFullValid;output.cloaking=true;output.hidden={L"selected-current-root"};
    return fixturePeriodicFullOk;
}
static bool fixturePeriodicCli(HidHidePeriodicState& output){
    ++fixturePeriodicCliCalls;if(fixturePeriodicMx.ownedByCurrentThread())++fixturePeriodicCliLocked;
    output={};if(fixtureUseActualCli)return fixtureActualSlimCli(output);if(!fixturePeriodicCliOk)return false;
    output.valid=true;output.cloaking=true;output.hidden={L"selected-current-root"};return true;
}
/*ACTUAL_PERIODIC_CAPABILITY*/
static void resetPeriodicCapability(bool capable){
    fixtureUseActualCli=fixtureCliSeam=fixtureQueryFailByExit=false;fixtureQueryCloak=true;fixtureCliSeamSkips=0;
    fixtureQueryFailOperation.clear();fixtureQueryHidden={L"selected-current-root"};fixtureQueryCommands.clear();fixtureQueryCommandsUnderOwner=0;
    fixtureActive=fixtureHello=fixtureHealthAllowed=true;fixturePipeRetired=fixtureQuarantined=fixtureTransportFault=false;
    fixtureRecoveryPending=fixtureSwitchPending=fixtureRuntimePending=fixtureTuplePending=fixtureWakePending=false;
    fixtureHostProcess=reinterpret_cast<void*>(1);fixtureRunId="owned-host-run";fixtureConfigured=fixtureBound="steamdeck";
    fixtureSequence=77;fixtureEpoch=7;fixtureAllowance=0;fixtureQueryElapsed=0;fixtureReplyUnknown=false;fixtureSlowLogs=json::array();fixturePlayer.state={};
    fixturePeriodicBeforeLock={};fixturePeriodicCapability.value=capable;
    fixturePeriodicUnlockedReads=fixturePeriodicPipeCalls=fixturePeriodicFullCalls=fixturePeriodicCliCalls=0;
    fixturePeriodicUnsupportedQueries=fixturePeriodicQueryUnlocked=fixturePeriodicCliLocked=0;
    fixturePeriodicHostRevision=1;fixturePeriodicQueriedRevision=0;fixturePeriodicLockAttempts=0;
    fixturePeriodicPipeOk=fixturePeriodicFullOk=fixturePeriodicFullValid=fixturePeriodicCliOk=true;
}
static void periodicCapabilityCase(const char* name,const std::function<void()>& body){
    body();scenarios.push_back({{"name",name},{"capabilityReadsOutsideOwner",fixturePeriodicUnlockedReads},
        {"compactQueries",fixturePeriodicPipeCalls},{"fullQueries",fixturePeriodicFullCalls},{"cliQueries",fixturePeriodicCliCalls},
        {"unsupportedQueries",fixturePeriodicUnsupportedQueries},{"queryWithoutOwner",fixturePeriodicQueryUnlocked},
        {"cliUnderOwner",fixturePeriodicCliLocked},{"hostRevision",fixturePeriodicHostRevision},
        {"queriedRevision",fixturePeriodicQueriedRevision},{"lockAttempts",fixturePeriodicLockAttempts.load()},{"actualQueryCommands",[&]{json commands=json::array();for(const auto& command:fixtureQueryCommands)commands.push_back(W2U(command));return commands;}()},{"queryCommandsUnderOwner",fixtureQueryCommandsUnderOwner}});
}
static void periodicCapabilityCases(bool replacementOnly){
    if(!replacementOnly){
        periodicCapabilityCase("periodic/current-compact",[]{
            resetPeriodicCapability(true);HidHidePeriodicState out;
            check("periodic-compact-current-shape",fixtureActualPeriodicVisibilityState(out)&&out.valid&&out.cloaking&&out.hidden.size()==1);
            check("periodic-compact-one-existing-lock-and-one-query",fixturePeriodicLockAttempts==1&&fixturePeriodicPipeCalls==1&&fixturePeriodicFullCalls==0&&fixturePeriodicCliCalls==0);
            check("periodic-compact-capability-and-query-owned",fixturePeriodicUnlockedReads==0&&fixturePeriodicQueryUnlocked==0);
        });
        periodicCapabilityCase("periodic/compact-fallback-cli",[]{
            resetPeriodicCapability(true);fixturePeriodicPipeOk=false;HidHidePeriodicState out;
            check("periodic-compact-fallback-shape",fixtureActualPeriodicVisibilityState(out)&&out.valid);
            check("periodic-fallback-exactly-one-CLI-outside-owner",fixturePeriodicPipeCalls==1&&fixturePeriodicCliCalls==1&&fixturePeriodicCliLocked==0);
        });
        periodicCapabilityCase("periodic/current-legacy-full",[]{
            resetPeriodicCapability(false);HidHidePeriodicState out;
            check("periodic-legacy-current-shape",fixtureActualPeriodicVisibilityState(out)&&out.valid&&out.hidden.size()==1);
            check("periodic-legacy-one-original-query-and-lock",fixturePeriodicFullCalls==1&&fixturePeriodicPipeCalls==0&&fixturePeriodicCliCalls==0&&fixturePeriodicLockAttempts==1);
            check("periodic-legacy-capability-and-query-owned",fixturePeriodicUnlockedReads==0&&fixturePeriodicQueryUnlocked==0);
        });
        periodicCapabilityCase("periodic/invalid-full",[]{
            resetPeriodicCapability(false);fixturePeriodicFullValid=false;HidHidePeriodicState out;out.valid=true;
            check("periodic-invalid-full-not-published",!fixtureActualPeriodicVisibilityState(out)&&!out.valid&&out.hidden.empty());
            check("periodic-invalid-full-releases-owner",!fixturePeriodicMx.ownedByCurrentThread()&&fixturePeriodicCliCalls==0);
        });
    }
    periodicCapabilityCase("periodic/replacement-before-lock",[]{
        resetPeriodicCapability(true);
        fixturePeriodicBeforeLock=[]{std::lock_guard<FixturePeriodicMutex> lock(fixturePeriodicMx);fixturePeriodicCapability.value=false;++fixturePeriodicHostRevision;};
        HidHidePeriodicState out;
        check("periodic-replacement-keeps-valid-readback",fixtureActualPeriodicVisibilityState(out)&&out.valid);
        check("periodic-replacement-capability-read-is-owned",fixturePeriodicUnlockedReads==0);
        check("periodic-replacement-never-queries-new-Host-with-old-capability",fixturePeriodicUnsupportedQueries==0);
        check("periodic-replacement-query-owned-and-CLI-unlocked",fixturePeriodicQueryUnlocked==0&&fixturePeriodicCliLocked==0);
        fixturePeriodicBeforeLock={};
    });
    if(!replacementOnly){
        periodicCapabilityCase("periodic/actual-thread-owner-wait",[]{
            resetPeriodicCapability(true);fixturePeriodicMx.lock();fixturePeriodicLockAttempts=0;
            HidHidePeriodicState out;bool returned=false;std::exception_ptr error;
            std::thread caller([&]{try{returned=fixtureActualPeriodicVisibilityState(out);}catch(...){error=std::current_exception();}});
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(fixturePeriodicLockAttempts.load(std::memory_order_acquire)==0&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
            const bool attempted=fixturePeriodicLockAttempts.load(std::memory_order_acquire)>0;
            fixturePeriodicCapability.value=false;++fixturePeriodicHostRevision;
            fixturePeriodicMx.unlock();caller.join();
            check("periodic-real-caller-reached-existing-owner-wait",attempted&&!error);
            check("periodic-real-wait-selects-current-replacement-capability",returned&&out.valid&&fixturePeriodicFullCalls==1&&fixturePeriodicPipeCalls==0&&fixturePeriodicQueriedRevision==2);
            check("periodic-real-wait-capability-and-query-owned",fixturePeriodicUnlockedReads==0&&fixturePeriodicQueryUnlocked==0&&fixturePeriodicUnsupportedQueries==0);
        });
    }
}

static void periodicGraceCases(){
 periodicCapabilityCase("periodic/grace-steady-2s-cadence-4s-one-query",[]{
  resetPeriodicCapability(true);HidHidePeriodicState out;fixtureQueryElapsed=3000;
  {std::lock_guard<FixturePeriodicMutex> lock(fixturePeriodicMx);check("steady-qualified-original-predicate",fixtureSteadyObservationLocked());check("steady-cadence-is2000",fixtureObservationCadenceLocked()==2000);}
  check("steady-four-second-query-success",fixtureActualPeriodicVisibilityState(out,true)&&out.valid&&fixtureAllowance==4000);
  check("steady-single-request-noCLI",fixturePeriodicPipeCalls==1&&fixturePeriodicCliCalls==0&&fixtureSequence==78);
  check("slow-success-preserves-original-host-identity",fixtureEpoch==7&&fixtureRunId=="owned-host-run"&&fixtureHostProcess==reinterpret_cast<void*>(1));
  check("slow-success-distinguishes-cadence-and-allowance",fixtureSlowLogs.size()==1&&fixtureSlowLogs[0].value("queryAllowanceMs",0)==4000&&fixtureSlowLogs[0].value("queryElapsedMs",0)==3000&&fixtureSlowLogs[0].value("observationCadenceMs",0)==2000&&fixtureSlowLogs[0].value("sameRequest",false));
 });
 periodicCapabilityCase("periodic/grace-default-and-after-write-stay2s",[]{
  resetPeriodicCapability(true);HidHidePeriodicState out;
  check("default-strong-query-retains2s",fixtureActualPeriodicVisibilityState(out)&&fixtureAllowance==2000);
  fixtureAllowance=0;{std::lock_guard<FixturePeriodicMutex> lock(fixturePeriodicMx);check("pipe-default-explicit2s",fixturePeriodicPipe(out)&&fixtureAllowance==2000);}
  check("default-no-grace-log",fixtureSlowLogs.empty());
 });
 periodicCapabilityCase("periodic/grace-pending-or-owner-state-never-promoted",[]{
  for(unsigned edge=0;edge<15;++edge){resetPeriodicCapability(true);
   if(edge==0)fixtureActive=false;if(edge==1)fixtureHello=false;if(edge==2)fixturePipeRetired=true;if(edge==3)fixtureQuarantined=true;
   if(edge==4)fixtureRecoveryPending=true;if(edge==5)fixtureSwitchPending=true;if(edge==6)fixtureRuntimePending=true;if(edge==7)fixtureTuplePending=true;if(edge==8)fixtureWakePending=true;
   if(edge==9)fixtureHealthAllowed=false;if(edge==10)fixturePlayer.state.locked=true;if(edge==11)fixturePlayer.state.phase="failure";
   if(edge==12)fixturePlayer.state.phase="expired";if(edge==13)fixtureConfigured="elite";if(edge==14)fixtureBound="dualsense-edge";
   {std::lock_guard<FixturePeriodicMutex> lock(fixturePeriodicMx);check("unsteady-predicate-remains-false",!fixtureSteadyObservationLocked());check("unsteady-cadence-stays500",fixtureObservationCadenceLocked()==500);}
   HidHidePeriodicState out;fixtureActualPeriodicVisibilityState(out,true);
   if(!fixturePipeRetired)check("unsteady-request-retains2s",fixtureAllowance==2000);else check("retired-Host-never-queried",fixturePeriodicPipeCalls==0);
  }
 });
 periodicCapabilityCase("periodic/grace-unknown-read-not-health",[]{
  resetPeriodicCapability(true);fixtureReplyUnknown=true;fixturePeriodicCliOk=false;HidHidePeriodicState out;
  check("unknown-retains-invalid",!fixtureActualPeriodicVisibilityState(out,true)&&!out.valid&&out.hidden.empty());
  check("unknown-original-singleCLI-fallback-unlocked",fixturePeriodicPipeCalls==1&&fixturePeriodicCliCalls==1&&fixturePeriodicCliLocked==0&&fixtureSlowLogs.empty());
 });
 periodicCapabilityCase("periodic/grace-legacy-no-new-capability-query",[]{
  resetPeriodicCapability(false);HidHidePeriodicState out;
  check("legacy-preserves-full-query",fixtureActualPeriodicVisibilityState(out,true)&&out.valid&&fixturePeriodicFullCalls==1&&fixturePeriodicPipeCalls==0&&fixtureAllowance==0);
 });
 periodicCapabilityCase("periodic/grace-scope-rechecked-after-real-owner-wait",[]{
  resetPeriodicCapability(true);fixturePeriodicMx.lock();fixturePeriodicLockAttempts=0;
  HidHidePeriodicState out;bool returned=false;std::exception_ptr error;
  std::thread caller([&]{try{returned=fixtureActualPeriodicVisibilityState(out,true);}catch(...){error=std::current_exception();}});
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(fixturePeriodicLockAttempts.load(std::memory_order_acquire)==0&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
  const bool attempted=fixturePeriodicLockAttempts.load()!=0;fixturePlayer.state.locked=true;fixtureWakePending=true;
  fixturePeriodicMx.unlock();caller.join();
  check("scope-wait-actually-observed",attempted&&!error&&returned&&out.valid);
  check("scope-retired-before-lock-loses4s",fixtureAllowance==2000&&fixturePeriodicPipeCalls==1&&fixturePeriodicCliCalls==0&&fixturePeriodicQueryUnlocked==0);
 });
}

static void prepareHostless(){
    resetPeriodicCapability(false);fixtureHostProcess=nullptr;fixtureUseActualCli=true;
}
static void hostlessCheck(const std::string& name,bool passed){check(name.c_str(),passed);}
static void hostlessCommands(const char* label){
    hostlessCheck(std::string(label)+"-two-required-queries",
        fixtureQueryCommands==std::vector<std::wstring>{L"--dev-list",L"--cloak-state"});
    hostlessCheck(std::string(label)+"-cli-outside-owner",fixtureQueryCommandsUnderOwner==0&&fixturePeriodicCliLocked==0);
    hostlessCheck(std::string(label)+"-no-full-or-pipe",fixturePeriodicFullCalls==0&&fixturePeriodicPipeCalls==0&&fixturePeriodicCliCalls==1);
}
static void periodicHostlessCases(const std::string& only={}){
 auto run=[&](const char* name,const std::function<void()>& body){
    if(only.empty()||only==name)periodicCapabilityCase(name,body);
 };
 for(const std::string reason:{"no-process","transport-fault","no-run-id","retired-pipe"}){
    const std::string name="hostless/"+reason;
    run(name.c_str(),[&]{
        resetPeriodicCapability(false);fixtureUseActualCli=true;
        if(reason=="no-process")fixtureHostProcess=nullptr;
        if(reason=="transport-fault")fixtureTransportFault=true;
        if(reason=="no-run-id")fixtureRunId.clear();
        if(reason=="retired-pipe")fixturePipeRetired=true;
        HidHidePeriodicState out;out.valid=true;out.hidden={L"stale"};
        hostlessCheck(name+"-current-shape",fixtureActualPeriodicVisibilityState(out,true)&&out.valid&&out.cloaking&&out.hidden==fixtureQueryHidden);
        hostlessCommands(name.c_str());
    });
 }
 run("hostless/default-after-write-preserves-full",[]{
    for(unsigned reason=0;reason<4;++reason){
      resetPeriodicCapability(false);fixtureUseActualCli=true;
      if(reason==0)fixtureHostProcess=nullptr;
      if(reason==1)fixtureTransportFault=true;
      if(reason==2)fixtureRunId.clear();
      if(reason==3)fixturePipeRetired=true;
      HidHidePeriodicState out;
      hostlessCheck("hostless-default-full-shape",fixtureActualPeriodicVisibilityState(out)&&out.valid&&out.cloaking&&out.hidden==fixtureQueryHidden);
      hostlessCheck("hostless-default-keeps-original-four-queries",fixturePeriodicFullCalls==1&&fixturePeriodicCliCalls==0&&fixtureQueryCommands==std::vector<std::wstring>{L"--dev-list",L"--app-list",L"--cloak-state",L"--inv-state"});
    }
 });
 run("hostless/empty-hidden-cloak-off",[]{
    prepareHostless();fixtureQueryHidden.clear();fixtureQueryCloak=false;HidHidePeriodicState out;
    hostlessCheck("hostless-empty-and-off-are-known",fixtureActualPeriodicVisibilityState(out,true)&&out.valid&&!out.cloaking&&out.hidden.empty());
    hostlessCommands("hostless-empty");
 });
 for(const std::wstring operation:{L"--dev-list",L"--cloak-state"})for(const bool exitOnly:{false,true}){
    const std::string name="hostless/failure-"+W2U(operation)+(exitOnly?"-exit":"-return");
    run(name.c_str(),[&]{
        prepareHostless();fixtureQueryFailOperation=operation;fixtureQueryFailByExit=exitOnly;
        HidHidePeriodicState out;out.valid=true;out.cloaking=true;out.hidden={L"stale"};
        hostlessCheck(name+"-unknown-no-partial",!fixtureActualPeriodicVisibilityState(out,true)&&!out.valid&&!out.cloaking&&out.hidden.empty());
        hostlessCheck(name+"-bounded-failure",fixtureQueryCommands.size()==(operation==L"--dev-list"?1U:2U));
        hostlessCheck(name+"-unlocked",fixtureQueryCommandsUnderOwner==0&&!fixturePeriodicMx.ownedByCurrentThread());
    });
 }
 run("hostless/irrelevant-app-query-not-issued",[]{
    prepareHostless();fixtureQueryFailOperation=L"--app-list";HidHidePeriodicState out;
    hostlessCheck("hostless-does-not-require-unconsumed-app-list",fixtureActualPeriodicVisibilityState(out,true)&&out.valid);
    hostlessCommands("hostless-unused-app");
 });
 run("hostless/seam-no-cli",[]{
    prepareHostless();fixtureCliSeam=true;HidHidePeriodicState out;out.valid=true;out.hidden={L"stale"};
    hostlessCheck("hostless-seam-preserves-skip",!fixtureActualPeriodicVisibilityState(out,true)&&!out.valid&&out.hidden.empty());
    hostlessCheck("hostless-seam-never-runs-cli",fixtureQueryCommands.empty()&&fixtureCliSeamSkips==1&&fixturePeriodicCliCalls==0&&fixturePeriodicFullCalls==0);
 });
 run("hostless/legacy-reachable-keeps-full",[]{
    resetPeriodicCapability(false);fixtureUseActualCli=true;HidHidePeriodicState out;
    hostlessCheck("hostless-legacy-shape",fixtureActualPeriodicVisibilityState(out,true)&&out.valid&&out.cloaking&&out.hidden==fixtureQueryHidden);
    hostlessCheck("hostless-legacy-original-full",fixturePeriodicFullCalls==1&&fixturePeriodicCliCalls==0&&fixtureQueryCommands==std::vector<std::wstring>{L"--dev-list",L"--app-list",L"--cloak-state",L"--inv-state"});
 });
 run("hostless/capable-pipe-preferred",[]{
    resetPeriodicCapability(true);fixtureUseActualCli=true;HidHidePeriodicState out;
    hostlessCheck("hostless-pipe-stays-one-query",fixtureActualPeriodicVisibilityState(out,true)&&out.valid&&fixturePeriodicPipeCalls==1&&fixtureQueryCommands.empty()&&fixturePeriodicCliCalls==0&&fixturePeriodicFullCalls==0);
 });
 run("hostless/capable-offline-original-fallback",[]{
    resetPeriodicCapability(true);fixtureUseActualCli=true;fixtureHostProcess=nullptr;HidHidePeriodicState out;
    hostlessCheck("hostless-offline-capability-shape",fixtureActualPeriodicVisibilityState(out,true)&&out.valid);
    hostlessCommands("hostless-offline-capability");
 });
 for(const std::string transition:{"retire","replace-legacy","replace-capable"}){
    const std::string name="hostless/owner-wait-"+transition;
    run(name.c_str(),[&]{
        resetPeriodicCapability(false);fixtureUseActualCli=true;
        if(transition!="retire")fixtureHostProcess=nullptr;
        fixturePeriodicMx.lock();fixturePeriodicLockAttempts=0;
        HidHidePeriodicState out;bool returned=false;std::exception_ptr error;
        std::thread caller([&]{try{returned=fixtureActualPeriodicVisibilityState(out,true);}catch(...){error=std::current_exception();}});
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(fixturePeriodicLockAttempts.load(std::memory_order_acquire)==0&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
        const bool attempted=fixturePeriodicLockAttempts.load(std::memory_order_acquire)>0;
        ++fixturePeriodicHostRevision;
        if(transition=="retire")fixturePipeRetired=true;
        else{fixtureHostProcess=reinterpret_cast<void*>(1);fixturePeriodicCapability.value=transition=="replace-capable";}
        fixturePeriodicMx.unlock();caller.join();
        hostlessCheck(name+"-actual-wait",attempted&&!error&&returned&&out.valid);
        hostlessCheck(name+"-reads-owned",fixturePeriodicUnlockedReads==0&&fixturePeriodicQueryUnlocked==0);
        if(transition=="retire")hostlessCommands(name.c_str());
        else if(transition=="replace-legacy")hostlessCheck(name+"-new-legacy-full",fixturePeriodicFullCalls==1&&fixtureQueryCommands.size()==4&&fixturePeriodicCliCalls==0&&fixturePeriodicQueriedRevision==2);
        else hostlessCheck(name+"-new-capable-pipe",fixturePeriodicPipeCalls==1&&fixtureQueryCommands.empty()&&fixturePeriodicQueriedRevision==2);
    });
 }
}

static int finishPhysicalVisibilityFixture(){
    const bool pass=std::all_of(checks.begin(),checks.end(),[](const json&value){return value.at("pass").get<bool>();});
    std::cout<<json({{"passed",pass},{"checks",checks},{"scenarios",scenarios},{"realDeviceOperations",0},{"realTransportOperations",0},{"cycles",0}}).dump(2)<<"\n";
    return pass?0:1;
}

int main(int argc,char**argv){try{
 if(argc>1){if(argc!=2)return 2;const std::string selected=argv[1];if(selected!="periodic-capability"&&selected!="periodic-capability-replacement"&&selected!="periodic-grace"&&selected!="periodic-hostless"&&selected!="periodic-hostless-no-process"&&selected!="periodic-hostless-seam"&&selected!="periodic-hostless-default")return 2;if(selected=="periodic-hostless")periodicHostlessCases();else if(selected=="periodic-hostless-no-process")periodicHostlessCases("hostless/no-process");else if(selected=="periodic-hostless-seam")periodicHostlessCases("hostless/seam-no-cli");else if(selected=="periodic-hostless-default")periodicHostlessCases("hostless/default-after-write-preserves-full");else if(selected=="periodic-grace")periodicGraceCases();else periodicCapabilityCases(selected=="periodic-capability-replacement");return finishPhysicalVisibilityFixture();}
 {std::vector<std::string> unusedPlan;nativeForegroundAppendVirtualTargets({}, {}, 0, "closed-fixture-link", unusedPlan);}
 std::vector<HidHidePhysicalIdentity>out;bool issued=false,ok=false;
 // E: old A absent, a different root B of the same model is the only observation.
 const char* selectionFailure=nullptr;const auto failureIs=[&](std::string_view expected){return selectionFailure&&std::string_view(selectionFailure)==expected;};
 reset();auto a=identity(),b=identity("B");g_hidHidePhysicalIdentity=a;nodes={{b}};ok=hidHideGetPhysicalIdentities(out,&selectionFailure);check("E-same-model-different-root-is-deferred",!ok&&out.empty()&&failureIs("selected-physical-identity-no-match"));check("E-old-selection-not-overwritten",g_hidHidePhysicalIdentity.physicalIdentity==a.physicalIdentity);observe("E-A-removed-external-B-same-model",ok,false);
 reset();a=identity();b=identity("B");auto j=prepare(a);nodes={{b}};const auto oldOwned=j.hiddenByUs;HidHidePeriodicState snap;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("E-journal-reconnect-no-hide-no-budget",!ok&&!issued&&hideCalls==0);check("E-journal-A-responsibility-retained",g_hidHideVisibilityJournal.hiddenByUs==oldOwned);check("E-journal-attempt-does-not-select-B",g_hidHidePhysicalIdentity.physicalIdentity==a.physicalIdentity);
 // Explicit selection is the actual RawInput receipt state, independently of previous journal.
 reset();g_hidHidePhysicalIdentity=b;nodes={{b}};ok=hidHideGetPhysicalIdentities(out);check("E-explicit-new-source-B-remains-admitted",ok&&out.size()==1&&out[0].physicalIdentity==b.physicalIdentity);
 reset();a=identity();auto reconnect=identity("A","NEWPAD");g_hidHidePhysicalIdentity=a;nodes={{reconnect}};ok=hidHideGetPhysicalIdentities(out);check("E-complete-same-root-new-child-admitted",ok&&out.size()==1&&out[0].physicalIdentity==reconnect.physicalIdentity);
 reset();nodes={{a}};ok=hidHideGetPhysicalIdentities(out,&selectionFailure);check("F-complete-single-source-initial-selection",ok&&out.size()==1&&!selectionFailure);
 // F: first source succeeds, the very next Enum returns an error, not end-of-list.
 reset();nodes={{a}};enumError=5;ok=hidHideGetPhysicalIdentities(out);check("F-partial-single-source-cannot-prove-unique",!ok&&out.empty()&&!g_hidHidePhysicalIdentity.resolved);observe("F-first-source-success-second-enum-error5",ok,false);check("F-enum-error-actually-reached",enumCalls==2&&probeCalls==1&&destroyCalls==1);
 reset();nodes={{a},{b,true}};ok=hidHideGetPhysicalIdentities(out);check("F-size-query-failure-not-unique",!ok&&out.empty());
 reset();nodes={{a},{b,false,true}};ok=hidHideGetPhysicalIdentities(out);check("F-detail-read-failure-not-unique",!ok&&out.empty());
 for(auto reject:{HidResolveReject::OpenFailed,HidResolveReject::ProbeFailed}){reset();nodes={{a},{b,false,false,reject}};ok=hidHideGetPhysicalIdentities(out);check(reject==HidResolveReject::OpenFailed?"F-open-failure-not-unique":"F-probe-failure-not-unique",!ok&&out.empty());}
 for(auto reject:{HidResolveReject::NotGamepad,HidResolveReject::VirtualMarker,HidResolveReject::NoBaseContainer}){reset();nodes={{a},{b,false,false,reject}};ok=hidHideGetPhysicalIdentities(out);check("F-definitive-excluded-interface-does-not-block",ok&&out.size()==1);}
 reset();g_hidHidePhysicalIdentity=a;nodes={{a}};enumError=5;ok=hidHideGetPhysicalIdentities(out);check("F-exact-selected-witness-survives-partial-enum",ok&&out.size()==1&&out[0].physicalIdentity==a.physicalIdentity);observe("F-partial-enum-with-exact-source-witness",ok,false);
 reset();g_hidHidePhysicalIdentity=a;nodes={{reconnect}};enumError=5;ok=hidHideGetPhysicalIdentities(out);check("F-partial-same-root-new-child-needs-confirmation",!ok&&out.empty());
 reset();nodes={{a},{b}};ok=hidHideGetPhysicalIdentities(out,&selectionFailure);check("F-complete-two-sources-unselected-deferred",!ok&&out.empty()&&failureIs("physical-identity-unselected"));
 reset();setFails=true;ok=hidHideGetPhysicalIdentities(out,&selectionFailure);check("F-class-set-failure-unresolved",!ok&&out.empty()&&destroyCalls==0&&failureIs("physical-discovery-incomplete"));
 reset();ok=hidHideGetPhysicalIdentities(out,&selectionFailure);check("F-complete-empty-unresolved",!ok&&out.empty()&&destroyCalls==1&&failureIs("no-eligible-source-in-complete-discovery"));
 // D: successful reapply must retain actual owned game siblings, not acquire third party entries.
 reset();j=prepare(a);snap={};snap.hidden={foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-reapply-success-and-budget-receipt",ok&&issued&&hideCalls==2&&readbackCalls==1);check("D-success-retains-ROG-game-sibling",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(extra)));check("D-success-retains-XUSB-return-duty",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(xusb)));check("D-unrelated-third-party-not-acquired",!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(foreign)));observe("D-success-before-sibling-readback-unknown",ok,issued);
 // Synthetic generic-accounting probe only: 2F24:0135 is auxiliary in real GPD evidence; forced game caps here are not a claimed GPD game topology.
 reset();a=identity("A","PAD",0x2F24,0x0135);j=prepare(a);const std::wstring gpdExtra=L"HID\\VID_2F24&PID_0135&IG_01\\A_GAME_SIBLING",gpdXusb=L"USB\\VID_2F24&PID_0135\\A_XUSB";j.hiddenByUs={U2W(a.baseContainerDeviceInstanceId),U2W(a.deviceInstanceId),gpdExtra,gpdXusb};g_hidHideVisibilityJournal=j;snap={};snap.hidden={gpdExtra,gpdXusb,foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-synthetic-aux-VID-with-game-caps-retains-owned-extensions",ok&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(gpdExtra))&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(gpdXusb)));
 reset();a=identity();j=prepare(a);j.hiddenByUs={extra};g_hidHideVisibilityJournal=j;snap={};snap.hidden={U2W(a.baseContainerDeviceInstanceId),foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-prehidden-root-third-party-not-acquired",ok&&!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,a.baseContainerDeviceInstanceId));check("D-newly-fakeHidden-selected-is-owned",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,a.deviceInstanceId));observe("D-partly-prehidden-root-other-owner",ok,issued);
 reset();j=prepare(a);j.before.hidden.push_back(U2W(a.baseContainerDeviceInstanceId));j.hiddenByUs={extra};g_hidHideVisibilityJournal=j;snap={};fakeHidden={};ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-original-baseline-third-party-root-not-acquired",!ok&&!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,a.baseContainerDeviceInstanceId)&&!hidHideContainsPath(fakeHidden,a.baseContainerDeviceInstanceId));
 reset();j=prepare(a);snap={};snap.hidden={U2W(a.baseContainerDeviceInstanceId),U2W(a.deviceInstanceId),foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-root-intact-extension-missing-noIO-no-budget",!ok&&!issued&&hideCalls==0&&readbackCalls==0);check("D-root-intact-keeps-unresolved-extension-duty",g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);observe("D-core-intact-extension-absent",ok,issued);
 reset();j=prepare(a);snap={};fakeHidden={};failTarget=a.deviceInstanceId;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-hide-failure-keeps-old-duty-and-issued",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs&&readbackCalls==0);observe("D-hide-failed",ok,issued);
 reset();j=prepare(a);snap={};fakeHidden={};readbackFails=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-readback-failure-keeps-old-duty-and-issued",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);observe("D-readback-failed",ok,issued);
 reset();j=prepare(a);snap={};fakeHidden={};unknownReadback=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-missing-target-readback-keeps-old-duty",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);
 reset();j=prepare(a);snap={};fakeHidden={};fixtureReconnectCloak=false;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-fixtureReconnectCloak-off-readback-keeps-old-duty",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);
 reset();j=prepare(a);snap={};fakeHidden={};knownMsi=true;g_hidHideVisibilityJournal.hiddenByUs.push_back(aux);ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-MSI-existing-auxiliary-duty-retained",ok&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(aux)));
 reset();j=prepare(a);snap={};fakeHidden={};transactionDrift=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-transaction-drift-does-not-replace-duty",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);

 // Verbatim production caller budget block drives the actual reapply function.
 reset();a=identity();j=prepare(a);snap={};snap.hidden={U2W(a.baseContainerDeviceInstanceId),U2W(a.deviceInstanceId)};fakeHidden=snap.hidden;fixtureReconcileBudget(j,snap);check("budget-noIO-does-not-spend-or-start-cooldown",g_inputHostReapplyHideRepairs==0&&g_inputHostReapplyHideLastTick==0&&hideCalls==0);
 reset();j=prepare(a);snap={};fixtureReconcileBudget(j,snap);check("budget-success-spends-once",g_inputHostReapplyHideRepairs==1&&g_inputHostReapplyHideLastTick==fixtureNow&&hideCalls==2);
 reset();j=prepare(a);snap={};readbackFails=true;fixtureReconcileBudget(j,snap);check("budget-failed-readback-still-spends-once",g_inputHostReapplyHideRepairs==1&&g_inputHostReapplyHideLastTick==fixtureNow);
 reset();j=prepare(a);snap={};failTarget=a.deviceInstanceId;fixtureReconcileBudget(j,snap);check("budget-failed-hide-still-spends-once",g_inputHostReapplyHideRepairs==1&&g_inputHostReapplyHideLastTick==fixtureNow);
 reset();j=prepare(a);nodes={{identity("B")}};snap={};fixtureReconcileBudget(j,snap);check("budget-different-root-noIO-keeps-budget",g_inputHostReapplyHideRepairs==0&&g_inputHostReapplyHideLastTick==0&&hideCalls==0);
 reset();j=prepare(a);snap={};failTarget=a.deviceInstanceId;for(int attempt=0;attempt<kInputHostReapplyGuardMaxRepairs;attempt++){fixtureReconcileBudget(j,snap);fixtureNow+=kInputHostReapplyHideCooldownMs;}const int callsAtLimit=hideCalls;fixtureReconcileBudget(j,snap);check("budget-exhaustion-stops-fifth-attempt",g_inputHostReapplyHideRepairs==4&&g_inputHostReapplyHideExhausted&&hideCalls==callsAtLimit&&callsAtLimit==8);
 reset();j=prepare(a);snap={};g_inputHostReapplyHideLastTick=fixtureNow;fixtureReconcileBudget(j,snap);check("budget-cooldown-does-noIO",g_inputHostReapplyHideRepairs==0&&hideCalls==0&&enumCalls==0);
 reset();j=prepare(a);snap={};g_xinputConnectedMask=0;fixtureReconcileBudget(j,snap);check("budget-source-offline-does-noIO",g_inputHostReapplyHideRepairs==0&&hideCalls==0&&enumCalls==0);
 // A new re-enumerated child must retain its attempted return duty even
 // if a later target or the readback fails. No later success tail is required.
 reset();a=identity();j=prepare(a);auto freshChild=identity("A","NEWPAD");nodes={{freshChild}};snap={};failTarget=a.baseContainerDeviceInstanceId;
 ok=hidHideReapplyReconnectedSource(j,snap,&issued);
 check("D-partial-hide-new-child-return-duty-retained",!ok&&issued&&hidHideContainsPath(fakeHidden,freshChild.deviceInstanceId)&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,freshChild.deviceInstanceId));
 observe("D-partial-new-child-hidden-root-failed",ok,issued);
 reset();a=identity();j=prepare(a);freshChild=identity("A","NEWPAD");nodes={{freshChild}};snap={};readbackFails=true;
 ok=hidHideReapplyReconnectedSource(j,snap,&issued);
 check("D-new-child-unknown-readback-return-duty-retained",!ok&&issued&&hidHideContainsPath(fakeHidden,freshChild.deviceInstanceId)&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,freshChild.deviceInstanceId));
 reset();a=identity();j=prepare(a);snap={};g_hidHideVisibilityJournal.transactionId="new-owner-transaction";
 ok=hidHideReapplyReconnectedSource(j,snap,&issued);
 check("D-replaced-transaction-cannot-issue-or-spend",!ok&&!issued&&hideCalls==0);
 // B: synthetic ROG/GPD topology (GPD game VID/PID045E:028E); not a Win5 device capture.
 // Only owned game extensions drift; run the actual family planner with inert inventories.
 auto setupOwned=[&](bool gpd=false){reset();fixtureProfile=gpd?Profile::Gpd:Profile::Rog;a=gpd?identity("A","PAD",0x045E,0x028E):identity();j=prepare(a);j.hiddenByUs={U2W(a.baseContainerDeviceInstanceId),U2W(a.deviceInstanceId),extra,xusb};
  if(gpd){j.hiddenByUs[2]=L"HID\\VID_045E&PID_028E&IG_01\\A_GAME_SIBLING";j.hiddenByUs[3]=L"USB\\VID_045E&PID_028E\\A_XUSB";}g_hidHideVisibilityJournal=j;
  snap={};snap.cloaking=true;snap.hidden={U2W(a.baseContainerDeviceInstanceId),U2W(a.deviceInstanceId),foreign};fakeHidden=snap.hidden;
  for(const auto&id:{a.deviceInstanceId,W2U(j.hiddenByUs[2])}){HidHideInterfaceRecord item;item.instanceId=id;item.baseContainerDeviceInstanceId=a.baseContainerDeviceInstanceId;item.vendorId=a.vendorId;item.productId=a.productId;item.usagePage=1;item.usage=5;currentInventory.push_back(item);}
  ymcc::msi::NativeForegroundXusbInterface item;item.activeInterface=item.ancestorMatched=true;item.instanceId=W2U(j.hiddenByUs[3]);item.ancestorRoot=a.baseContainerDeviceInstanceId;currentXusb={item};};
 for(bool gpd:{false,true}){setupOwned(gpd);const auto duties=j.hiddenByUs;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check(gpd?"B-GPD-owned-game-and-XUSB-repaired":"B-ROG-owned-game-and-XUSB-repaired",ok&&issued&&hideCalls==2&&readbackCalls==1&&fullReads==1);check("B-original-owned-set-retained",g_hidHideVisibilityJournal.hiddenByUs==duties);check("B-foreign-baseline-preserved",hidHideContainsPath(fakeHidden,W2U(foreign)));}
 setupOwned();currentInventory[1].usagePage=5;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-aux-page05-never-rehidden-as-game",!hidHideContainsPath(fakeHidden,W2U(extra))&&hidHideContainsPath(fakeHidden,W2U(xusb))&&ok&&issued);
 setupOwned();currentInventory[1].baseContainerDeviceInstanceId=identity("B").baseContainerDeviceInstanceId;currentXusb[0].ancestorRoot=identity("B").baseContainerDeviceInstanceId;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-other-root-extensions-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();currentInventory[1].instanceId="HID\\VID_0B05&PID_1ABE&IG_02\\NEW_NOT_OWNED";snap.hidden.push_back(xusb);fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-no-new-unowned-extension-acquisition",!ok&&!issued&&hideCalls==0&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);
 setupOwned();inventoryComplete=false;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-partial-game-inventory-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();xusbComplete=false;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-partial-XUSB-inventory-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();locationChanged=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-location-drift-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();fullInverse=false;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-unknown-inverse-no-plan-or-write",!ok&&!issued&&inventoryReads==0&&hideCalls==0);
 setupOwned();fullReadFails=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-full-snapshot-failure-no-plan-or-write",!ok&&!issued&&inventoryReads==0&&hideCalls==0);
 setupOwned();atPlanReceipt=[](){++g_powerGeneration;};ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-retired-after-planning-before-write-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();atPlanReceipt=[](){g_hidHideVisibilityJournal.transactionId="replacement";};ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-transaction-retired-after-planning-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();atPlanReceipt=[](){g_realStickTest.virtualTargetEnabled=false;};ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-virtual-off-after-planning-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();currentInventory.erase(currentInventory.begin());ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-missing-current-main-source-cannot-prove-extensions",!ok&&!issued&&hideCalls==0);
 setupOwned();for(const auto&id:j.hiddenByUs)snap.hidden.push_back(id);fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-owned-interfaces-intact-no-extra-observation",!ok&&!issued&&fullReads==0&&inventoryReads==0&&hideCalls==0);
 setupOwned();failTarget=W2U(xusb);ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-partial-extra-write-retains-exact-duties",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs&&hidHideContainsPath(fakeHidden,W2U(extra)));
 setupOwned();retireDuringPlan=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-retired-power-plan-noIO",!ok&&!issued&&hideCalls==0);
 setupOwned();g_hidHideVisibilityJournal.transactionId="new-owner";ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-replaced-transaction-no-extra-discovery",!ok&&!issued&&fullReads==0&&hideCalls==0);
 setupOwned();releaseWindow=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-release-window-no-extra-discovery",!ok&&!issued&&fullReads==0&&hideCalls==0);
 setupOwned();g_inputHostPowerAdmissionRevoked=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-power-revoked-no-extra-discovery",!ok&&!issued&&fullReads==0&&hideCalls==0);
 setupOwned();fixtureProfile=Profile::Msi;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-MSI-stays-on-existing-private-route",!ok&&!issued&&fullReads==0&&hideCalls==0);
 setupOwned();g_realStickTest.virtualTargetEnabled=false;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-virtual-off-no-extra-discovery",!ok&&!issued&&fullReads==0&&hideCalls==0);
 setupOwned();currentInventory.pop_back();currentXusb.clear();for(int n=0;n<3;++n){fixtureNow=10000+n*500;ok=hidHideReapplyReconnectedSource(j,snap,&issued);}check("B-wait-backoff-no-write-budget",fullReads==1&&hideCalls==0&&g_inputHostReapplyHideRepairs==0);
 setupOwned();currentInventory.pop_back();currentXusb.clear();for(const auto offset:{0ULL,1500ULL,4500ULL,10500ULL,20000ULL,60000ULL}){fixtureNow=10000+offset;ok=hidHideReapplyReconnectedSource(j,snap,&issued);}check("B-incomplete-discovery-bounded-four-attempts",fullReads==4&&hideCalls==0);
 const auto readsBefore=fullReads;++g_powerGeneration;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("B-new-power-generation-rearms-discovery",fullReads==readsBefore+1&&hideCalls==0);
 setupOwned();inventoryComplete=false;fixtureReconcileBudget(j,snap);check("B-unproven-discovery-does-not-spend-driver-budget",fullReads==1&&hideCalls==0&&g_inputHostReapplyHideRepairs==0&&g_inputHostReapplyHideLastTick==0);
 setupOwned();fixtureReconcileBudget(j,snap);check("B-proven-extra-repair-spends-one-write-attempt",hideCalls==2&&g_inputHostReapplyHideRepairs==1&&g_inputHostReapplyHideLastTick==fixtureNow);
 // Existing strict MSI branch: exact path, same-source unique sibling, ambiguity and generation drift.
 auto setupMsi=[&](){reset();a=identity("A","MI_00&COL01",0x0DB0,0x1902);nodes={{a}};managed=true;record.interfacePath=a.sourcePath;record.vendorId=a.vendorId;record.productId=a.productId;record.containerGuid="container-A";record.usbInstance="usb-A";record.generation=4;};
 setupMsi();ok=hidHideGetPhysicalIdentities(out);check("MSI-complete-exact-path-admitted",ok&&out.size()==1);
 setupMsi();enumError=5;ok=hidHideGetPhysicalIdentities(out);check("MSI-partial-with-exact-path-witness-admitted",ok&&out.size()==1);
 setupMsi();record.interfacePath="old-path";ok=hidHideGetPhysicalIdentities(out);check("MSI-complete-unique-same-source-sibling-admitted",ok&&out.size()==1);
 setupMsi();record.interfacePath="old-path";enumError=5;ok=hidHideGetPhysicalIdentities(out);check("MSI-partial-sibling-observation-is-not-unique",!ok&&out.empty());
 setupMsi();record.interfacePath="old-path";record.containerGuid="other-container";record.usbInstance="other-usb";ok=hidHideGetPhysicalIdentities(out);check("MSI-different-source-same-model-deferred",!ok&&out.empty());
 setupMsi();drift=true;ok=hidHideGetPhysicalIdentities(out);check("MSI-generation-drift-deferred",!ok&&out.empty());
 setupMsi();scope=true;eligible=false;ok=hidHideGetPhysicalIdentities(out);check("MSI-pending-mode2-does-not-enumerate",!ok&&enumCalls==0&&out.empty());
 return finishPhysicalVisibilityFixture();
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 2;}}
