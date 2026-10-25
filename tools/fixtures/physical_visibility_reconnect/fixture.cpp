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
bool readbackFails=false,cloak=true,unknownReadback=false,transactionDrift=false,knownMsi=false;
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
std::function<void()> atPlanReceipt;
void appendNativeLifecycleLog(const char*event,const json&){if(std::string(event)=="hidhide.owned-game-reconnect-plan"&&atPlanReceipt)atPlanReceipt();}
bool msiClawKnownBoardAuthority(){return knownMsi;}
bool hidHideMsiAuxiliaryOwnedInstance(const std::string&s){return s.find("MI_00&COL02")!=std::string::npos||s.find("MI_01&COL03")!=std::string::npos;}
bool fakeHide(const std::wstring&p,const char*route){++hideCalls;calls.push_back(std::string(route)+":"+W2U(p));if(W2U(p)==failTarget)return false;if(!hidHideContainsPath(fakeHidden,W2U(p)))fakeHidden.push_back(p);return true;}
bool inputHostHidHideApplyViaPipeLocked(const std::wstring&op,const std::wstring&p){if(op!=L"hide")throw std::runtime_error("unexpected op");return fakeHide(p,"pipe");}
bool hidHideRun(const std::wstring&cmd){const auto a=cmd.find(L'"'),b=cmd.rfind(L'"');if(a==std::wstring::npos||b<=a)throw std::runtime_error("unexpected cli");return fakeHide(cmd.substr(a+1,b-a-1),"cli");}
bool hidHideCapturePeriodicVisibilityState(HidHidePeriodicState&out){++readbackCalls;if(transactionDrift)g_hidHideVisibilityJournal.transactionId="other-tx";if(readbackFails)return false;out.cloaking=cloak;out.hidden=fakeHidden;if(unknownReadback&&!out.hidden.empty())out.hidden.pop_back();return true;}
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
void reset(){atPlanReceipt={};resetOwnedDiscovery();fixtureProfile=Profile::Unsupported;g_inputHostEpoch=7;g_powerGeneration=3;g_xinputTopologyEpoch=2;g_realStickTest={};g_inputReady=true;exitFence=releaseWindow=false;g_inputHostPowerAdmissionRevoked=false;g_powerLifecycle=PowerLifecycle::Ready;currentInventory.clear();currentXusb.clear();inventoryComplete=xusbComplete=fullValid=fullCloak=fullInverse=true;locationChanged=fullReadFails=retireDuringPlan=false;fullReads=inventoryReads=xusbReads=locationReads=0;nodes.clear();enumError=ERROR_NO_MORE_ITEMS;lastError=0;setFails=false;enumCalls=detailCalls=probeCalls=destroyCalls=hideCalls=readbackCalls=0;scope=managed=drift=false;eligible=true;managedReads=0;record={};fixtureNow=10000;g_inputHostReapplyHideLastTick=0;g_inputHostReapplyHideRepairs=0;g_inputHostReapplyHideExhausted=false;g_inputHostReapplyHideEpisodeKey="7|tx-A";g_xinputPhysicalSourceMask=g_xinputConnectedMask=1;g_hidHidePhysicalIdentity={};g_hidHideVisibilityJournal={};fakeHidden.clear();calls.clear();failTarget.clear();readbackFails=unknownReadback=transactionDrift=knownMsi=false;cloak=true;}
std::vector<std::string> narrow(const std::vector<std::wstring>&v){std::vector<std::string>r;for(auto&p:v)r.push_back(W2U(p));return r;}
const std::wstring extra=L"HID\\VID_0B05&PID_1ABE&IG_01\\A_GAME_SIBLING",xusb=L"USB\\VID_0B05&PID_1ABE\\A_XUSB",foreign=L"HID\\FOREIGN_OWNER\\KEEP",aux=L"HID\\VID_0DB0&PID_1902&MI_00&COL02\\A";
HidHideVisibilityJournal prepare(const HidHidePhysicalIdentity&p){nodes={{p}};g_hidHidePhysicalIdentity=p;auto&j=g_hidHideVisibilityJournal;j.active=true;j.transactionId="tx-A";j.physical=p;j.physicalSources={p};j.hiddenByUs={U2W(p.baseContainerDeviceInstanceId),U2W(p.deviceInstanceId),extra,xusb};j.before.hidden={foreign};return j;}
void observe(const char*name,bool ok,bool issued){scenarios.push_back({{"name",name},{"returned",ok},{"issued",issued},{"hideCalls",hideCalls},{"readbackCalls",readbackCalls},{"owned",narrow(g_hidHideVisibilityJournal.hiddenByUs)},{"selectedRoot",g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId},{"calls",calls}});}
int main(){try{
 {std::vector<std::string> unusedPlan;nativeForegroundAppendVirtualTargets({}, {}, 0, "closed-fixture-link", unusedPlan);}
 std::vector<HidHidePhysicalIdentity>out;bool issued=false,ok=false;
 // E: old A absent, a different root B of the same model is the only observation.
 reset();auto a=identity(),b=identity("B");g_hidHidePhysicalIdentity=a;nodes={{b}};ok=hidHideGetPhysicalIdentities(out);check("E-same-model-different-root-is-deferred",!ok&&out.empty());check("E-old-selection-not-overwritten",g_hidHidePhysicalIdentity.physicalIdentity==a.physicalIdentity);observe("E-A-removed-external-B-same-model",ok,false);
 reset();a=identity();b=identity("B");auto j=prepare(a);nodes={{b}};const auto oldOwned=j.hiddenByUs;HidHidePeriodicState snap;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("E-journal-reconnect-no-hide-no-budget",!ok&&!issued&&hideCalls==0);check("E-journal-A-responsibility-retained",g_hidHideVisibilityJournal.hiddenByUs==oldOwned);check("E-journal-attempt-does-not-select-B",g_hidHidePhysicalIdentity.physicalIdentity==a.physicalIdentity);
 // Explicit selection is the actual RawInput receipt state, independently of previous journal.
 reset();g_hidHidePhysicalIdentity=b;nodes={{b}};ok=hidHideGetPhysicalIdentities(out);check("E-explicit-new-source-B-remains-admitted",ok&&out.size()==1&&out[0].physicalIdentity==b.physicalIdentity);
 reset();a=identity();auto reconnect=identity("A","NEWPAD");g_hidHidePhysicalIdentity=a;nodes={{reconnect}};ok=hidHideGetPhysicalIdentities(out);check("E-complete-same-root-new-child-admitted",ok&&out.size()==1&&out[0].physicalIdentity==reconnect.physicalIdentity);
 reset();nodes={{a}};ok=hidHideGetPhysicalIdentities(out);check("F-complete-single-source-initial-selection",ok&&out.size()==1);
 // F: first source succeeds, the very next Enum returns an error, not end-of-list.
 reset();nodes={{a}};enumError=5;ok=hidHideGetPhysicalIdentities(out);check("F-partial-single-source-cannot-prove-unique",!ok&&out.empty()&&!g_hidHidePhysicalIdentity.resolved);observe("F-first-source-success-second-enum-error5",ok,false);check("F-enum-error-actually-reached",enumCalls==2&&probeCalls==1&&destroyCalls==1);
 reset();nodes={{a},{b,true}};ok=hidHideGetPhysicalIdentities(out);check("F-size-query-failure-not-unique",!ok&&out.empty());
 reset();nodes={{a},{b,false,true}};ok=hidHideGetPhysicalIdentities(out);check("F-detail-read-failure-not-unique",!ok&&out.empty());
 for(auto reject:{HidResolveReject::OpenFailed,HidResolveReject::ProbeFailed}){reset();nodes={{a},{b,false,false,reject}};ok=hidHideGetPhysicalIdentities(out);check(reject==HidResolveReject::OpenFailed?"F-open-failure-not-unique":"F-probe-failure-not-unique",!ok&&out.empty());}
 for(auto reject:{HidResolveReject::NotGamepad,HidResolveReject::VirtualMarker,HidResolveReject::NoBaseContainer}){reset();nodes={{a},{b,false,false,reject}};ok=hidHideGetPhysicalIdentities(out);check("F-definitive-excluded-interface-does-not-block",ok&&out.size()==1);}
 reset();g_hidHidePhysicalIdentity=a;nodes={{a}};enumError=5;ok=hidHideGetPhysicalIdentities(out);check("F-exact-selected-witness-survives-partial-enum",ok&&out.size()==1&&out[0].physicalIdentity==a.physicalIdentity);observe("F-partial-enum-with-exact-source-witness",ok,false);
 reset();g_hidHidePhysicalIdentity=a;nodes={{reconnect}};enumError=5;ok=hidHideGetPhysicalIdentities(out);check("F-partial-same-root-new-child-needs-confirmation",!ok&&out.empty());
 reset();nodes={{a},{b}};ok=hidHideGetPhysicalIdentities(out);check("F-complete-two-sources-unselected-deferred",!ok&&out.empty());
 reset();setFails=true;ok=hidHideGetPhysicalIdentities(out);check("F-class-set-failure-unresolved",!ok&&out.empty()&&destroyCalls==0);
 reset();ok=hidHideGetPhysicalIdentities(out);check("F-complete-empty-unresolved",!ok&&out.empty()&&destroyCalls==1);
 // D: successful reapply must retain actual owned game siblings, not acquire third party entries.
 reset();j=prepare(a);snap={};snap.hidden={foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-reapply-success-and-budget-receipt",ok&&issued&&hideCalls==2&&readbackCalls==1);check("D-success-retains-ROG-game-sibling",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(extra)));check("D-success-retains-XUSB-return-duty",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(xusb)));check("D-unrelated-third-party-not-acquired",!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(foreign)));observe("D-success-before-sibling-readback-unknown",ok,issued);
 // Synthetic generic-accounting probe only: 2F24:0135 is auxiliary in real GPD evidence; forced game caps here are not a claimed GPD game topology.
 reset();a=identity("A","PAD",0x2F24,0x0135);j=prepare(a);const std::wstring gpdExtra=L"HID\\VID_2F24&PID_0135&IG_01\\A_GAME_SIBLING",gpdXusb=L"USB\\VID_2F24&PID_0135\\A_XUSB";j.hiddenByUs={U2W(a.baseContainerDeviceInstanceId),U2W(a.deviceInstanceId),gpdExtra,gpdXusb};g_hidHideVisibilityJournal=j;snap={};snap.hidden={gpdExtra,gpdXusb,foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-synthetic-aux-VID-with-game-caps-retains-owned-extensions",ok&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(gpdExtra))&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(gpdXusb)));
 reset();a=identity();j=prepare(a);j.hiddenByUs={extra};g_hidHideVisibilityJournal=j;snap={};snap.hidden={U2W(a.baseContainerDeviceInstanceId),foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-prehidden-root-third-party-not-acquired",ok&&!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,a.baseContainerDeviceInstanceId));check("D-newly-fakeHidden-selected-is-owned",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,a.deviceInstanceId));observe("D-partly-prehidden-root-other-owner",ok,issued);
 reset();j=prepare(a);j.before.hidden.push_back(U2W(a.baseContainerDeviceInstanceId));j.hiddenByUs={extra};g_hidHideVisibilityJournal=j;snap={};fakeHidden={};ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-original-baseline-third-party-root-not-acquired",ok&&!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,a.baseContainerDeviceInstanceId));
 reset();j=prepare(a);snap={};snap.hidden={U2W(a.baseContainerDeviceInstanceId),U2W(a.deviceInstanceId),foreign};fakeHidden=snap.hidden;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-root-intact-extension-missing-noIO-no-budget",!ok&&!issued&&hideCalls==0&&readbackCalls==0);check("D-root-intact-keeps-unresolved-extension-duty",g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);observe("D-core-intact-extension-absent",ok,issued);
 reset();j=prepare(a);snap={};fakeHidden={};failTarget=a.deviceInstanceId;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-hide-failure-keeps-old-duty-and-issued",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs&&readbackCalls==0);observe("D-hide-failed",ok,issued);
 reset();j=prepare(a);snap={};fakeHidden={};readbackFails=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-readback-failure-keeps-old-duty-and-issued",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);observe("D-readback-failed",ok,issued);
 reset();j=prepare(a);snap={};fakeHidden={};unknownReadback=true;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-missing-target-readback-keeps-old-duty",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);
 reset();j=prepare(a);snap={};fakeHidden={};cloak=false;ok=hidHideReapplyReconnectedSource(j,snap,&issued);check("D-cloak-off-readback-keeps-old-duty",!ok&&issued&&g_hidHideVisibilityJournal.hiddenByUs==j.hiddenByUs);
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
 const bool pass=std::all_of(checks.begin(),checks.end(),[](const json&v){return v.at("pass").get<bool>();});std::cout<<json({{"passed",pass},{"checks",checks},{"scenarios",scenarios},{"realDeviceOperations",0},{"realTransportOperations",0},{"cycles",0}}).dump(2)<<"\n";return pass?0:1;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 2;}}
