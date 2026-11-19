// Explicit original recovery consumer only; all settings/build ports are memory.
#pragma once
#include <map>
#include <new>
#include <cstdlib>
#include "settings_baseline.h"
#include "decky_loader_identity.h"
#ifndef YMCC_DECKY_BUDGET_PORTS_ONLY
static std::atomic<bool> budgetFailAllocation{false};
void* operator new(std::size_t size){if(budgetFailAllocation.exchange(false))throw std::bad_alloc();if(auto* value=std::malloc(size?size:1))return value;throw std::bad_alloc();}
void operator delete(void* value) noexcept{std::free(value);}
void operator delete(void* value,std::size_t) noexcept{std::free(value);}
#endif
namespace budgetfixture {
using json=nlohmann::json;
struct Ports {
 json primary,backup;unsigned reads=0,unlockedReads=0,writes=0,preserves=0,currentCalls=0,imageReads=0,upgradeCalls=0,buildReads=0;
 unsigned cancelAt=0,failWrite=0,badRead=0,lockDepth=0;
 bool compatible=true,preserve=true,grant=false,backupPresent=true,buildValid=true;
 std::string readFault;
};
static Ports ports;
static const std::wstring YM_SETTINGS_FILE=L"MEMORY-PRIMARY",YM_SETTINGS_BACKUP_FILE=L"MEMORY-BACKUP",POWER_CONTROL_DIR=L"MEMORY-DECKY";
namespace privatefs {inline bool exists(const std::wstring&){return ports.backupPresent;}}
namespace fspath=privatefs;
struct SettingsFileGuard {SettingsFileGuard(){++ports.lockDepth;}~SettingsFileGuard(){--ports.lockDepth;}};
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static std::string ymSettingsReadRaw(const std::wstring& file){
 require(ports.lockDepth==1,"raw read outside original lock");++ports.reads;
 auto value=file==YM_SETTINGS_FILE?ports.primary:ports.backup;
 if(file==YM_SETTINGS_BACKUP_FILE&&!ports.backupPresent)return {};
 if(ports.badRead==ports.reads){if(ports.readFault=="schema")value["schemaVersion"]=99;else value["deckyContextCleanup"]["document"]="different-document";}
 return value.dump();
}
static json ymSettingsReadUnlocked(){require(ports.lockDepth==1,"settings read outside original lock");++ports.unlockedReads;return ports.primary;}
static bool ymSettingsHasCompatibleWriteBase(const std::string&,const json&){return ports.compatible;}
static bool ymSettingsPreserveMainBeforeWrite(const std::string& raw){require(ports.lockDepth==1,"backup outside original lock");++ports.preserves;if(!ports.preserve)return false;ports.backup=json::parse(raw);ports.backupPresent=true;return true;}
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& raw){require(ports.lockDepth==1,"write outside original lock");++ports.writes;if(ports.failWrite==ports.writes)return false;(file==YM_SETTINGS_FILE?ports.primary:ports.backup)=json::parse(raw);return true;}
static std::string fixtureFrontendBuildId(const std::wstring&){++ports.buildReads;return ports.buildValid?std::string(64,'a'):"invalid-build";}
static DWORD fixtureImagePath(HMODULE,wchar_t* output,DWORD capacity){++ports.imageReads;const std::wstring text=L"C:\\INERT\\main.exe";require(capacity>text.size(),"image port buffer");std::copy(text.begin(),text.end(),output);output[text.size()]=0;return static_cast<DWORD>(text.size());}
static std::string sha256FileCancellable(const std::wstring&,const std::function<bool()>&){throw std::runtime_error("test build store must not read real images");}
template<class Digest>static bool fixtureUpgradeReserve(const std::wstring&,const std::string&,bool,const std::string&,const json&,const json&,Digest&&,const std::function<bool()>&,const char** stage=nullptr){require(ports.lockDepth==1,"upgrade reservation outside original lock");++ports.upgradeCalls;if(stage)*stage=ports.grant?"reserved":"grant";return ports.grant;}
}
#ifndef APP_VER_STR
#define APP_VER_STR "fixture-only"
#endif
#include YMCC_DECKY_BUDGET_SOURCE
#ifndef YMCC_DECKY_BUDGET_PORTS_ONLY
static int recoveryBudgetReservationCase(bool finalReadbackOnly=false){
 using namespace budgetfixture;using Json=nlohmann::json;
 const auto check=[](bool ok,const char* name){if(!ok)throw std::runtime_error(name);};
 const ymcc::steamsession::Session session{L"C:\\INERT\\Steam",1234,9,10001};
 const std::vector<std::string> allNames={"ordinary","reopen","off","incompatible-main","cancel-initial","malformed-backup","null-ledger","upgrade-granted","upgrade-refused","history-mismatch","upgrade-backup-missing","invalid-build","preserve-failed","primary-write-failed","backup-before-write-invalid","backup-write-failed","primary-readback-mismatch","backup-readback-mismatch","cancel-settings","cancel-history","cancel-after-backup","cancel-final","cancel-final-readback"};
 const std::vector<std::string> names=finalReadbackOnly?std::vector<std::string>{"cancel-final-readback"}:allNames;
 Json results=Json::array(),controls=Json::array();unsigned failed=0;
 for(const auto& name:names){
  ports=Ports{};ports.primary={{"schemaVersion",1},{"businessOpaque","preserve"},{"extensions",{{"deckySidebar",{{"enabled",true}}}}}};ports.backup=ports.primary;
  const bool reopen=name=="reopen";
  if(name=="off")ports.primary["extensions"]["deckySidebar"]["enabled"]=false;
  if(name=="incompatible-main")ports.compatible=false;
  if(name=="cancel-initial")ports.cancelAt=1;
  if(name=="malformed-backup")ports.backup["schemaVersion"]=99;
  if(name=="null-ledger")ports.primary["deckyContextCleanup"]=ports.backup["deckyContextCleanup"]=nullptr;
  if(name=="upgrade-granted"||name=="upgrade-refused"||name=="history-mismatch"||name=="upgrade-backup-missing"){
   const auto prior=ymcc::deckyupdate::managed::record(session,"original-document","501:10001",std::string(64,'b'),ymcc::deckyidentity::version,"reserved");
   check(prior.is_object(),"test prior reservation invalid");ports.primary["deckyContextCleanup"]=prior;ports.backup=ports.primary;
  }
  if(name=="upgrade-granted")ports.grant=true;
  if(name=="history-mismatch")ports.backup["deckyContextCleanup"]["document"]="other-document";
  if(name=="upgrade-backup-missing")ports.backupPresent=false;
  if(name=="invalid-build")ports.buildValid=false;
  if(name=="preserve-failed")ports.preserve=false;
  if(name=="primary-write-failed")ports.failWrite=1;
  if(name=="backup-before-write-invalid"){ports.badRead=3;ports.readFault="schema";}
  if(name=="backup-write-failed")ports.failWrite=2;
  if(name=="primary-readback-mismatch")ports.badRead=4;
  if(name=="backup-readback-mismatch")ports.badRead=5;
  if(name=="cancel-settings")ports.cancelAt=2;
  if(name=="cancel-history")ports.cancelAt=3;
  if(name=="cancel-after-backup")ports.cancelAt=4;
  if(name=="cancel-final")ports.cancelAt=5;
  if(name=="cancel-final-readback")ports.cancelAt=6;
  const auto current=[] {++ports.currentCalls;return !ports.cancelAt||ports.currentCalls<ports.cancelAt;};
  bool accepted=false;Json detail;
#ifdef YMCC_DECKY_BUDGET_BASELINE
  accepted=deckySidebarReserveContextCleanup(session,"current-document","502:10002",current,reopen);
  detail={{"ok",accepted},{"mutated",false},{"uncertain",false}};
#else
  ymcc::deckydiag::RecoveryBudgetFacts facts;
  accepted=deckySidebarReserveContextCleanup(session,"current-document","502:10002",current,reopen,&facts);
  detail={{"ok",accepted},{"mutated",false},{"uncertain",false}};
  check(ports.lockDepth==0,"original guard must release before diagnostic allocation");
  ymcc::deckydiag::attachRecoveryBudget(detail,facts);
#endif
  const bool expected=name=="ordinary"||name=="reopen"||name=="upgrade-granted";
  check(accepted==expected,"original reservation result changed");check(ports.lockDepth==0,"reservation lock remained held");
  const Json business={{"accepted",accepted},{"reads",ports.reads},{"unlockedReads",ports.unlockedReads},{"writes",ports.writes},{"preserves",ports.preserves},{"currentCalls",ports.currentCalls},{"imageReads",ports.imageReads},{"upgradeCalls",ports.upgradeCalls},{"buildReads",ports.buildReads},{"primary",ports.primary},{"backup",ports.backup}};
  ymcc::steamfailure::FailureContext context;context.actionId=77;context.recoveryEpisode=3;context.generation=9;context.powerGeneration=11;context.steamPid=session.pid;context.steamBirth=session.created;context.phase=ymcc::steamfailure::Phase::Finished;context.operation=reopen?ymcc::steamfailure::Operation::ReopenBigPicture:ymcc::steamfailure::Operation::RebuildDocument;
  const auto row=ymcc::deckydiag::failureRecord(detail,context);const bool observed=row.contains("recoveryBudget");
  if(!observed)++failed;
  check(row.at("actionId")=="77"&&row.at("recoveryEpisode")==3&&row.at("nativePowerGeneration")==11&&row.at("steamPid")==session.pid&&row.at("steamBirth")==std::to_string(session.created),"diagnostic action identity changed");
  check(detail.at("ok")==accepted&&!detail.value("mutated",true)&&!detail.value("uncertain",true),"metadata changed business reply");
#ifndef YMCC_DECKY_BUDGET_BASELINE
  const auto budget=row.at("recoveryBudget");check(budget.at("operation")==ymcc::steamfailure::name(context.operation),"budget operation context mismatch");
  check(budget.at("source")== (name=="upgrade-granted"?"verified-upgrade":facts.ordinaryKnown&&facts.ordinaryAvailable?"ordinary":"unknown"),"wrong budget origin");
  check(budget.at("outcome")==facts.outcome&&budget.at("primaryWriteAccepted")==facts.primaryWriteAccepted&&budget.at("backupWriteAccepted")==facts.backupWriteAccepted&&budget.at("copiesConfirmed")==facts.copiesConfirmed,"lost already-read reservation facts");
  if(accepted)check(budget.at("outcome")=="reserved"&&budget.at("copiesConfirmed")==true,"reservation falsely incomplete");
  if(name=="cancel-final-readback")check(!accepted&&ports.currentCalls==6&&ports.writes==2&&budget.at("copiesConfirmed")==true&&budget.at("stage")=="readback"&&budget.at("outcome")=="uncertain","final owner loss falsely certified reservation");
  check(row.dump().size()<2304&&row.dump().find("C:\\")==std::string::npos&&!budget.contains("steamSession")&&!budget.contains("host")&&!budget.contains("account"),"unbounded/private reservation data leaked");
  if(name=="ordinary"){
   Json original={{"ok",true},{"mutated",false},{"opaque","keep"}},reply=original;budgetFailAllocation=true;
   ymcc::deckydiag::attachRecoveryBudget(reply,facts);const bool unused=budgetFailAllocation.exchange(false);check(!unused&&reply==original,"allocation failure changed completed business result");controls.push_back("post-lock-diagnostic-allocation-failure-isolated");
   for(unsigned fault=0;fault<11;++fault){auto malformed=budget;
    if(fault==0)malformed["source"]="secret-source";if(fault==1)malformed["operation"]="launch-steam";if(fault==2)malformed["stage"]=std::string(5000,'z');if(fault==3)malformed["outcome"]="actually-dispatched";
    if(fault==4)malformed["schema"]=1.0;if(fault==5)malformed["schema"]=true;if(fault==6)malformed["ordinaryAvailable"]="true";if(fault==7)malformed["frontendBuildId"]="private-path";
    if(fault==8)malformed["privateAccount"]=123;if(fault==9)malformed=Json::array();if(fault==10)malformed["upgradeReserved"]=Json::object();
    auto input=detail;input["recoveryBudget"]=malformed;const auto ignored=ymcc::deckydiag::failureRecord(input,context);check(!ignored.contains("recoveryBudget")&&ignored.at("ok")==true&&!ignored.value("mutated",true),"malformed diagnostics changed acceptance or leaked");
   }controls.push_back("eleven-malformed-metadata-edges-drop-only-optional-facts");
   auto foreign=detail;foreign["recoveryBudget"]["operation"]="reopen-bigpicture";check(!ymcc::deckydiag::failureRecord(foreign,context).contains("recoveryBudget"),"foreign action inherited budget facts");
   auto binding=context;binding.operation=ymcc::steamfailure::Operation::Binding;check(!ymcc::deckydiag::failureRecord(detail,binding).contains("recoveryBudget"),"non-recovery action inherited reservation facts");controls.push_back("foreign-operation-and-binding-do-not-inherit-budget-facts");
  }
#endif
  results.push_back({{"name",name},{"business",business},{"observed",observed},{"row",row}});
 }
 const Json evidence={{"kind","actual-reservation-and-parent-projector"},{"results",results},{"controls",controls},{"failed",failed},{"realSettingsWrites",0},{"realSteamActions",0},{"realRunningStoreReads",0},{"defaultSuiteRun",false}};
 std::cout<<"RECOVERY_BUDGET_EVIDENCE "<<evidence.dump()<<'\n';
 if(failed){std::cerr<<"original recovery budget source facts missing\n";return 1;}
 std::cout<<"Recovery budget diagnostics tests passed: "<<results.size()<<"; controls="<<controls.size()<<"; realSteam=0\n";return 0;
}
#endif
