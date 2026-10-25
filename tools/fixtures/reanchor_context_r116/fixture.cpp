#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "input_host_transaction_mutex.h"
using json=nlohmann::json;using DWORD=unsigned long;using BYTE=unsigned char;
using USHORT=unsigned short;using USAGE=unsigned short;
using InputHostMutex=ymcc::inputHost::TransactionMutex<>;
/*ACTUAL_TYPES*/
static InputHostMutex g_inputHostMx;static std::mutex g_hidHidePhysicalIdentityMx;
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static struct {bool active=true;std::string transactionId="TX-A";HidHidePhysicalIdentity physical;}g_hidHideVisibilityJournal;
static unsigned long long g_inputHostEpoch=7;static std::string g_inputHostRunId="RUN-A",fixturePersona="elite";
static DWORD g_xinputPrimarySlot=0,g_xinputSemanticSlot=0,g_xinputPhysicalSourceMask=1,g_xinputPreTargetSlots=1;
static DWORD g_xinputConnectedMask=3,g_xinputVirtualEndpointMask=1,g_xinputSlotUnknownMask=0;
static bool g_xinputPhysicalSourceLocked=true,exitFence=false;
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool>g_inputReady{true},g_inputHostPowerAdmissionRevoked{false};
enum class PowerLifecycle {Ready,Suspended};static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned>g_xinputTopologyEpoch{2};static std::atomic<unsigned long long>g_powerGeneration{3};
static const char*inputHostPersona(){return fixturePersona.c_str();}static bool inputHostExitFenceRaised(){return exitFence;}
static std::wstring U2W(const std::string&s){return {s.begin(),s.end()};}static std::string W2U(const std::wstring&s){std::string out;for(const auto c:s)out.push_back(static_cast<char>(c));return out;}
static int indexQueries=0,routeQueries=0,pathQueries=0;static bool recovered=false,routeAllowed=true;
static std::function<void()>afterIndex,afterRoute;static json logs=json::array();
static void appendNativeLifecycleLog(const char*event,const json&row){auto r=row;r["event"]=event;logs.push_back(r);}
static BYTE ymccOpenXInputGetUserIndex(const std::wstring&p){++indexQueries;const BYTE result=(p==L"SAVED-A"&&!recovered)||p==L"RECOVERED-A"?1:0xff;if(afterIndex){auto hook=std::move(afterIndex);afterIndex={};hook();}return result;}
static std::wstring ymccOpenXInputGetDevicePath(DWORD slot){++pathQueries;return slot==1?L"RECOVERED-A":L"";}
static bool ymccXboxReanchorRouteMatchesSource(const std::wstring&,const HidHidePhysicalIdentity&,DWORD){++routeQueries;if(afterRoute){auto hook=std::move(afterRoute);afterRoute={};hook();}return routeAllowed;}
/*ACTUAL_HELPERS*/
/*ACTUAL_REANCHOR*/
static json cases=json::array(),checks=json::array();
static void check(const char*name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
static void reset(){g_inputHostEpoch=7;g_inputHostRunId="RUN-A";fixturePersona="elite";g_xinputPrimarySlot=g_xinputSemanticSlot=0;g_xinputPhysicalSourceMask=g_xinputPreTargetSlots=1;g_xinputConnectedMask=3;g_xinputVirtualEndpointMask=1;g_xinputSlotUnknownMask=0;g_xinputPhysicalSourceLocked=true;exitFence=false;g_realStickTest={};g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_powerLifecycle=PowerLifecycle::Ready;g_xinputTopologyEpoch=2;g_powerGeneration=3;indexQueries=routeQueries=pathQueries=0;recovered=false;routeAllowed=true;afterIndex={};afterRoute={};logs=json::array();g_hidHidePhysicalIdentity={};auto&s=g_hidHidePhysicalIdentity;s.resolved=true;s.sourcePath="HID-PATH-A";s.deviceInstanceId="HID-A";s.baseContainerDeviceInstanceId="ROOT-A";s.baseContainerDevicePath="SAVED-A";s.physicalIdentity="IDENTITY-A";g_hidHideVisibilityJournal.active=true;g_hidHideVisibilityJournal.transactionId="TX-A";g_hidHideVisibilityJournal.physical=s;}
static bool originalAccounting(){return g_xinputPrimarySlot==0&&g_xinputSemanticSlot==0&&g_xinputPhysicalSourceMask==1&&g_xinputPreTargetSlots==1;}
static void finish(const std::string&name){bool pass=std::all_of(checks.begin(),checks.end(),[](const json&c){return c["pass"].get<bool>();});cases.push_back({{"name",name},{"pass",pass},{"checks",checks},{"indexQueries",indexQueries},{"routeQueries",routeQueries},{"pathQueries",pathQueries},{"logs",logs}});checks=json::array();}
int main(){try{
 (void)xinputPhysicalConnectedMask();
 for(int variant=0;variant<3;++variant){reset();if(variant==1)fixturePersona="xbox360";if(variant==2)g_hidHideVisibilityJournal.active=false;ymccOpenXInputReanchorPhysicalSource();check("healthy original source follows slot",g_xinputPrimarySlot==1&&g_xinputSemanticSlot==1&&g_xinputPhysicalSourceMask==2&&g_xinputPreTargetSlots==2);check("saved path needs no PnP route recovery",routeQueries==0&&pathQueries==0&&indexQueries==1);finish("healthy-saved-route-"+std::to_string(variant));}
 reset();recovered=true;ymccOpenXInputReanchorPhysicalSource();check("healthy recovered exact route follows slot",g_xinputPrimarySlot==1&&g_xinputPhysicalSourceMask==2);check("retains existing bounded route checks",routeQueries==1&&pathQueries==1&&indexQueries==2);finish("healthy-recovered-route");
 const std::vector<std::string> modes={"exit","off","disabled","not-ready","suspend","revoked","host-epoch","run-id","transaction","journal-ended","source-root","source-route","source-path","source-id","topology","power-generation","primary-moved","unlocked","occupancy-changed","virtual-slot0-lost","persona-changed","unknown-slot","unresolved-source"};
 auto mutate=[&](const std::string&m){if(m=="exit")exitFence=true;else if(m=="off")g_realStickTest.virtualTargetEnabled=false;else if(m=="disabled")g_realStickTest.enabled=false;else if(m=="not-ready")g_inputReady=false;else if(m=="suspend")g_powerLifecycle=PowerLifecycle::Suspended;else if(m=="revoked")g_inputHostPowerAdmissionRevoked=true;else if(m=="host-epoch")++g_inputHostEpoch;else if(m=="run-id")g_inputHostRunId="RUN-B";else if(m=="transaction")g_hidHideVisibilityJournal.transactionId="TX-B";else if(m=="journal-ended")g_hidHideVisibilityJournal.active=false;else if(m=="source-root")g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId="ROOT-B";else if(m=="source-route")g_hidHideVisibilityJournal.physical.baseContainerDevicePath="SAVED-B";else if(m=="source-path")g_hidHideVisibilityJournal.physical.sourcePath="HID-PATH-B";else if(m=="source-id")g_hidHideVisibilityJournal.physical.physicalIdentity="IDENTITY-B";else if(m=="topology")++g_xinputTopologyEpoch;else if(m=="power-generation")++g_powerGeneration;else if(m=="primary-moved")g_xinputPrimarySlot=2;else if(m=="unlocked")g_xinputPhysicalSourceLocked=false;else if(m=="occupancy-changed")g_xinputConnectedMask=7;else if(m=="virtual-slot0-lost")g_xinputVirtualEndpointMask=2;else if(m=="persona-changed")fixturePersona="steamdeck";else if(m=="unknown-slot")g_xinputSlotUnknownMask=2;else if(m=="unresolved-source")g_hidHideVisibilityJournal.physical.resolved=false;};
 for(const bool recover:{false,true})for(const auto&mode:modes){reset();recovered=recover;auto callback=[&,mode]{mutate(mode);};if(recover)afterRoute=callback;else afterIndex=callback;ymccOpenXInputReanchorPhysicalSource();check("retired context cannot update physical sampling masks",g_xinputSemanticSlot==0&&g_xinputPhysicalSourceMask==1&&g_xinputPreTargetSlots==1);check("retired context cannot publish success",std::none_of(logs.begin(),logs.end(),[](const json&r){return r.value("policy","")=="physical-slot-follows-handoff";}));finish(std::string(recover?"recovered-query/":"saved-query/")+mode);}
 for(const auto&mode:{"exit","off","disabled","not-ready","suspend","revoked","unknown-slot","unresolved-source"}){reset();mutate(mode);ymccOpenXInputReanchorPhysicalSource();check("closed admission has no accounting change",originalAccounting());check("closed admission performs no route/index query",indexQueries==0&&routeQueries==0&&pathQueries==0);finish(std::string("before-query/")+mode);}
 for(const auto&mode:{"exit","off","not-ready","suspend","revoked","topology","power-generation","primary-moved","occupancy-changed","virtual-slot0-lost","persona-changed","unknown-slot"}){reset();recovered=true;afterRoute=[&,mode]{afterIndex=[&,mode]{mutate(mode);};};ymccOpenXInputReanchorPhysicalSource();check("final mapping query cannot retire context then publish sampling masks",g_xinputSemanticSlot==0&&g_xinputPhysicalSourceMask==1&&g_xinputPreTargetSlots==1);finish(std::string("final-recovered-query/")+mode);}
 reset();g_hidHideVisibilityJournal.active=false;afterIndex=[]{g_hidHidePhysicalIdentity.baseContainerDevicePath="SAVED-B";};ymccOpenXInputReanchorPhysicalSource();check("fallback selected source replacement cancels accounting",originalAccounting());finish("fallback-source-replaced");
 reset();g_hidHideVisibilityJournal.active=false;afterIndex=[]{g_hidHideVisibilityJournal.active=true;};ymccOpenXInputReanchorPhysicalSource();check("new journal cannot adopt fallback query",originalAccounting());finish("fallback-new-journal");
 reset();{std::lock_guard<InputHostMutex>owner(g_inputHostMx);ymccOpenXInputReanchorPhysicalSource();check("same-thread busy skip is nonblocking",originalAccounting()&&indexQueries==0);}finish("owned-host-lock-no-recursion");
 for(const bool recover:{false,true}){reset();recovered=recover;std::unique_lock<InputHostMutex>busy;const auto occupy=[&]{busy=std::unique_lock<InputHostMutex>(g_inputHostMx);};if(recover)afterRoute=occupy;else afterIndex=occupy;ymccOpenXInputReanchorPhysicalSource();check("busy commit skips without recursion or accounting mutation",originalAccounting());busy.unlock();finish(recover?"recovered-commit-busy":"saved-commit-busy");}
 reset();g_xinputConnectedMask=7;ymccOpenXInputReanchorPhysicalSource();check("ambiguous occupancy no query or publication",originalAccounting()&&indexQueries==0);finish("ambiguous-occupancy");
 reset();recovered=true;routeAllowed=false;ymccOpenXInputReanchorPhysicalSource();check("unproven new route does not replace source",originalAccounting());finish("unproven-recovered-route");
 const unsigned failed=unsigned(std::count_if(cases.begin(),cases.end(),[](const json&r){return !r["pass"].get<bool>();}));unsigned assertions=0,failedChecks=0;for(const auto&r:cases)for(const auto&c:r["checks"]){++assertions;if(!c["pass"].get<bool>())++failedChecks;}
 std::cout<<json({{"passed",failed==0},{"caseCount",cases.size()},{"assertions",assertions},{"failedCases",failed},{"failedChecks",failedChecks},{"cases",cases},{"deviceOperations",0},{"productionStarted",false}}).dump(2)<<"\n";return failed?1:0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 2;}}
