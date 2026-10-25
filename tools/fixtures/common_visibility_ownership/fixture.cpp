#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;using USAGE=USHORT;
#include "types.inc"
#include "paths.inc"
enum class YmccInterfacePolicy { Other,MsiClawDinput };
struct Machine {int family=0;};static Machine g_machineIdentity;
static YmccInterfacePolicy ymccFamilyInterfacePolicy(int value){return value==3?YmccInterfacePolicy::MsiClawDinput:YmccInterfacePolicy::Other;}
static HidHideVisibilityJournal g_hidHideVisibilityJournal;
static std::vector<std::string> targets;static std::set<std::string> world;
static std::vector<json> events,operations;static bool fence=false,identityOk=true,snapshotOk=true,reservationOk=true;
static bool pipeOk=true,cliOk=true,pipeApplies=true,cliApplies=true,virtualTarget=false;static unsigned snapshots=0,health=0,applyFallback=0;
static bool inputHostExitFenceRaised(){return fence;}
static void inputHostExitFenceLogRefusal(const char*,const char*){}
static void hidHideServiceHealth(const char*){++health;}
static bool hidHideGetPhysicalIdentities(std::vector<HidHidePhysicalIdentity>& result){if(!identityOk)return false;HidHidePhysicalIdentity p{};p.resolved=true;p.deviceInstanceId="HID\\VID_1234&PID_5678\\PHYSICAL";p.baseContainerDeviceInstanceId="USB\\VID_1234&PID_5678\\PHYSICAL";result={p};return true;}
static bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot& value){++snapshots;if(!snapshotOk)return false;value.valid=true;value.cloaking=true;for(const auto& p:world)value.hidden.push_back(U2W(p));return true;}
static bool hidHideApplyPhysicalTransaction(const std::string&,bool){++applyFallback;return false;}
static std::vector<std::string> hidHideVirtualPhysicalTargets(const HidHidePhysicalIdentity&,const HidHideVisibilitySnapshot&,size_t,const char*){return targets;}
static bool msiCrashRecoveryBeforeHide(const HidHidePhysicalIdentity&,const std::vector<std::string>&,const HidHideVisibilitySnapshot&,const std::string&){return reservationOk;}
static bool hidHideIsVirtualIdentity(const std::string&,const std::string&){return virtualTarget;}
static bool inputHostHidHideApplyViaPipeLocked(const std::wstring& action,const std::wstring& path){
 if(action!=L"hide")throw std::logic_error("non-hide operation");const auto p=W2U(path);operations.push_back({{"leaf","pipe"},{"path",p},{"ownedAtDispatch",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,p)}});if(pipeApplies)world.insert(p);return pipeOk;
}
static bool hidHideRun(const std::wstring& args){const auto start=std::wstring(L"--dev-hide \"");if(args.rfind(start,0)!=0||args.back()!=L'"')throw std::logic_error("unadmitted-CLI-operation");auto p=W2U(args.substr(start.size(),args.size()-start.size()-1));operations.push_back({{"leaf","cli"},{"path",p},{"ownedAtDispatch",hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,p)}});if(cliApplies)world.insert(p);return cliOk;}
static void appendNativeLifecycleLog(const char* event,json facts){events.push_back({{"event",event},{"facts",std::move(facts)}});}
static std::wstring hidHideCurrentModulePath(){return {};}
static bool msiControllerTraceEnabled(){return false;}
static json msiControllerVisibilityApplications(const HidHideVisibilitySnapshot&,bool){return json::object();}
static void msiControllerTraceLog(const char*,json,bool){}
// Focused local-ownership consumer only. This external persistence boundary
// does not execute Session, ancestry verification, file IO or durable commit.
// The real BeforeHide algorithm belongs to its separate runtime fixture.
static bool inputHostTargetReturnBeforeHideLocked(
 const std::vector<HidHidePhysicalIdentity>&,const HidHideVisibilitySnapshot&,
 const HidHideVisibilitySnapshot&,const std::string&,const std::string&,
 bool* responsibilityReserved=nullptr){
 if(responsibilityReserved)*responsibilityReserved=true;
 return true;
}
#include "consumer.inc"
static unsigned checks=0;static std::vector<std::string> failures;static std::string current;
static void check(bool value,const char* assertion){++checks;if(!value)failures.push_back(current+" :: "+assertion);}
static void reset(int family=1){g_machineIdentity.family=family;g_hidHideVisibilityJournal={};g_hidHideVisibilityJournal.active=true;targets={"HID\\VID_1234&PID_5678\\A"};world={};events={};operations={};fence=false;identityOk=snapshotOk=reservationOk=true;pipeOk=cliOk=pipeApplies=cliApplies=true;virtualTarget=false;snapshots=health=applyFallback=0;}
static bool run(){return hidHideRefreshPhysicalHide("owned-fixture","late-source");}
int main(){
 for(int family=0;family<4;++family){reset(family);current="prehidden-unowned/family"+std::to_string(family);world.insert(targets[0]);check(run(),"existing hidden configuration retains refresh result");check(operations.empty(),"prehidden path emits no new hide");check(g_hidHideVisibilityJournal.hiddenByUs.empty(),"prehidden foreign path cannot become this transaction's responsibility");check(snapshots==(family==3?2u:1u),"existing per-family observation count unchanged");}
 for(int family=0;family<4;++family){reset(family);current="lost-hide-receipts/family"+std::to_string(family);pipeOk=cliOk=false;check(!run(),"failed receipts preserve failed execution result");check(world.count(targets[0])==1,"fake driver applied a hide despite lost receipts");check(hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,targets[0]),"attempted hide remains owed even when both receipts fail");check(operations.size()==2,"original bounded pipe then CLI fallback order retained");for(const auto& op:operations)check(op.at("ownedAtDispatch").get<bool>(),"responsibility exists before each external dispatch");}
 for(bool fallback:{false,true}){reset();current=fallback?"CLI-success":"pipe-success";pipeOk=!fallback;check(run(),"successful hide retains refresh success");check(hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,targets[0]),"newly attempted path retained");check(operations.size()==(fallback?2u:1u),"no new driver calls");for(const auto& op:operations)check(op.at("ownedAtDispatch").get<bool>(),"ownership reserved before successful dispatch");}
 reset();current="already-owned-prehidden";world.insert(targets[0]);g_hidHideVisibilityJournal.hiddenByUs={U2W(targets[0])};check(run(),"owned prehidden refresh stays usable");check(g_hidHideVisibilityJournal.hiddenByUs.size()==1,"existing responsibility preserved exactly once");check(operations.empty(),"already hidden own path never rehidden");
 reset();current="case-equivalent-existing-obligation";g_hidHideVisibilityJournal.hiddenByUs={U2W(ascii_lower(targets[0]))};check(run(),"case-equivalent existing obligation permits original hide");check(g_hidHideVisibilityJournal.hiddenByUs.size()==1,"Windows instance casing cannot duplicate an existing obligation");
 reset();current="failed-first-batch-tail-never-attempted";targets.push_back("HID\\VID_1234&PID_5678\\B");pipeOk=cliOk=false;check(!run(),"failed first dispatch stops batch");check(g_hidHideVisibilityJournal.hiddenByUs.size()==1,"only attempted first target reserved");check(!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,targets[1]),"unattempted tail is not claimed");
 for(int gate=0;gate<5;++gate){reset();current="existing-gate/"+std::to_string(gate);if(gate==0)fence=true;else if(gate==1)identityOk=false;else if(gate==2)snapshotOk=false;else if(gate==3)reservationOk=false;else virtualTarget=true;check(!run(),"existing refusal result preserved");check(operations.empty(),"refusal never dispatches hide");check(g_hidHideVisibilityJournal.hiddenByUs.empty(),"refusal never invents an attempted path");}
 reset();current="inactive-journal";g_hidHideVisibilityJournal.active=false;check(!run()&&applyFallback==1,"inactive journal delegates to existing initial apply");check(operations.empty(),"refresh does not add a second dispatch route");
 json result={{"status",failures.empty()?"PASS":"FAIL"},{"checks",checks},{"failures",failures},{"realDeviceIo",false},{"scope","actual production refresh local-ownership consumer and path helpers; explicit fake discovery/driver/log and BeforeHide persistence leaves"},{"physicalPersistenceAlgorithmExecuted",false},{"physicalPersistenceBoundary","inputHostTargetReturnBeforeHideLocked: return true and out responsibilityReserved=true; no true Session/CM/durable IO claim"}};std::cout<<result.dump(2)<<std::endl;return failures.empty()?0:1;
}
