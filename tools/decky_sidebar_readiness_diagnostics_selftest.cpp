#if defined(YMCC_DECKY_SHORTCUT_READ_ONLY)
#include "fixtures/decky_shortcut_read_diagnostics.inc"
#else
// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Actual shared observer / Decky admission worker with inert readers and rejected
// preflight, plus the production telemetry allowlist. No real Steam or loader.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_session_observer.h"
#include <atomic>
static std::atomic<unsigned> registryDiagnosticReads{0};
static LSTATUS WINAPI diagnosticRegistryRead(HKEY key,LPCWSTR subkey,LPCWSTR name,DWORD flags,LPDWORD type,PVOID data,LPDWORD size){
 ++registryDiagnosticReads;return RegGetValueW(key,subkey,name,flags,type,data,size);
}
#define RegGetValueW diagnosticRegistryRead
#include "decky_sidebar_session.h"
#undef RegGetValueW
#include "steam_ui_mode_observer.h"
#include "decky_sidebar_runtime.h"
#include "decky_sidebar_diagnostics.h"
#include <iostream>
#include <stdexcept>
using namespace ymcc;
static unsigned passed=0;
static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++passed;std::cout<<"PASS "<<name<<'\n';}
template<class F>bool until(F f,DWORD ms=800){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static steamui::ModeJson mode(const steamsession::Session& s,const char* name){return {{"known",true},{"running",true},{"mode",name},{"modeEpoch","same-renderer"},{"pid",s.pid},{"birth",std::to_string(s.created)}};}

#ifndef YMCC_READINESS_BASELINE
static void sharedIdentityDiagnosticCases(const steamsession::Session& session,const steamui::ModeJson& proofMode){
 auto online=session;online.account=43;steamui::ModeJson value;
 // Only a private HKCU subtree is written. The original API performs each
 // actual DWORD/type/error read; the shim counts calls without replacing IO.
 struct RegistryScope{std::wstring path;~RegistryScope(){RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());}} registry{
   L"Software\\YMCCTests\\DeckyAccountDiagnostic-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())};
 HKEY key=nullptr;
 check(RegCreateKeyExW(HKEY_CURRENT_USER,(registry.path+L"\\ActiveProcess").c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"Private diagnostic registry key created");
 struct KeyScope{HKEY value;~KeyScope(){if(value)RegCloseKey(value);}} keyScope{key};
 const auto writeAccount=[&](DWORD account){check(RegSetValueExW(key,L"ActiveUser",0,REG_DWORD,reinterpret_cast<const BYTE*>(&account),sizeof(account))==ERROR_SUCCESS,"Private diagnostic DWORD written");};
 const auto inspect=[&](const auto& who,const auto& selected){
   const auto before=selected.dump();const auto admitted=deckysession::bigPicture(who,selected);
   // Consume real shared registry readers into the actual public receipt type.
   // Producer/lifecycle behavior is independently covered by the original Observer
   // suite; this original consumer verifies pure telemetry and actual zero IO.
   auto identity=std::make_shared<steamsession::IdentitySnapshot>();
   identity->session=who;identity->presence=1;identity->rootReadError=ERROR_SUCCESS;
   identity->registeredPid=steamsession::regNumber(registry.path,L"pid",&identity->registeredPidReadError);
   identity->session.account=steamsession::regNumber(registry.path,L"ActiveUser",&identity->accountReadError);
   const auto count=registryDiagnosticReads.load();
   const auto detail=deckysession::diagnosticWithIdentity(who,selected,identity);
   check(registryDiagnosticReads==count&&detail.value("registryReadFromSharedSnapshot",true),"Shared identity projection never performs a consumer registry read");
   check(selected.dump()==before&&deckysession::bigPicture(who,selected)==admitted,"Fresh registry diagnostics do not change the source or admission");
   return deckydiag::record("steam-mode-read",detail);
 };
 online.account=43;
 for(const DWORD current:{43u,42u,44u,0u}){
   writeAccount(current);const auto count=registryDiagnosticReads.load();const auto result=inspect(online,proofMode);
   check(registryDiagnosticReads==count&&result.value("registryReadKnown",false)&&
     result.value("registryReadMatchesSession",true)==(current==43)&&result.value("registryReadMatchesUi",true)==(current==42)&&
     result.value("registryReadAccountPresent",true)==(current!=0),"One shared receipt distinguishes session/UI/neither/zero with zero consumer registry reads");
   check(!result.contains("observedAccount")&&!result.contains("account")&&!result.contains("current")&&!result.contains("registryPath"),"Fresh registry diagnostics omit raw identities and paths");
 }
 const wchar_t wrong[]=L"private-not-an-account";
 check(RegSetValueExW(key,L"ActiveUser",0,REG_SZ,reinterpret_cast<const BYTE*>(wrong),sizeof(wrong))==ERROR_SUCCESS,"Private malformed registry value written");
 const auto wrongType=inspect(online,proofMode);
 check(!wrongType.value("registryReadKnown",true)&&wrongType.contains("registryReadError")&&!wrongType.contains("registryReadMatchesSession")&&!wrongType.contains("registryReadMatchesUi"),"Wrong registry type stays unknown and cannot imply either account matches");
 check(RegDeleteValueW(key,L"ActiveUser")==ERROR_SUCCESS,"Private diagnostic registry value removed");
 const auto absent=inspect(online,proofMode);
 check(!absent.value("registryReadKnown",true)&&absent.value("registryReadError",0u)==ERROR_FILE_NOT_FOUND&&!absent.contains("registryReadAccountPresent"),"Missing registry read is distinct from a verified zero account");
 // Process relation is diagnostic only, independent of the account value.
 // A matching PID does not verify the image/birth or authorize a conflicting account.
 writeAccount(43u);
 for(const DWORD pid:std::array<DWORD,3>{online.pid,online.pid+1,0u}){
   check(RegSetValueExW(key,L"pid",0,REG_DWORD,reinterpret_cast<const BYTE*>(&pid),sizeof(pid))==ERROR_SUCCESS,"Private diagnostic PID written");
   const auto count=registryDiagnosticReads.load();const auto result=inspect(online,proofMode);
   check(registryDiagnosticReads==count&&result.value("registryReadPidKnown",false)&&
     result.value("registryReadPidPresent",true)==(pid!=0)&&result.value("registryReadPidMatchesSession",true)==(pid==online.pid),
     "Fresh PID relation distinguishes current/other/zero with zero consumer DWORD reads");
   check(deckysession::bigPicture(online,proofMode)&&result.value("uiAccountSelected",false)&&result.value("registryMismatchIgnored",false)&&!result.contains("registeredPid")&&!result.contains("registryPid"),"Current UI independently authorizes Decky despite stale account; PID telemetry never logs its raw registry PID");
 }
 check(RegSetValueExW(key,L"pid",0,REG_SZ,reinterpret_cast<const BYTE*>(wrong),sizeof(wrong))==ERROR_SUCCESS,"Private malformed PID written");
 const auto wrongPid=inspect(online,proofMode);
 check(!wrongPid.value("registryReadPidKnown",true)&&wrongPid.contains("registryReadPidError")&&!wrongPid.contains("registryReadPidMatchesSession")&&wrongPid.value("registryReadKnown",false),"Malformed PID stays unknown independently of a successful account read");
 check(RegDeleteValueW(key,L"pid")==ERROR_SUCCESS,"Private diagnostic PID removed");
 const auto missingPid=inspect(online,proofMode);
 check(!missingPid.value("registryReadPidKnown",true)&&missingPid.value("registryReadPidError",0u)==ERROR_FILE_NOT_FOUND&&!missingPid.contains("registryReadPidPresent"),"Missing PID is distinct from a verified zero PID");
 const auto beforeHealthy=registryDiagnosticReads.load();
 auto healthy=online;healthy.account=42;inspect(healthy,proofMode);
 value=proofMode;value["mode"]="desktop";inspect(online,value);
 value=proofMode;value["mode"]="unknown";inspect(online,value);
 value=proofMode;value["pid"]=session.pid+1;inspect(online,value);
 inspect(session,proofMode);
 check(registryDiagnosticReads==beforeHealthy,"Healthy/offline/desktop/unknown/stale-process diagnostics add zero registry reads");
 const auto rejectedFresh=deckydiag::record("steam-mode-read",{{"registryReadKnown",1},{"registryReadMatchesUi","true"},{"registryReadError",-1},{"ActiveUser",43u},{"registryPath","SECRET"}});
 check(rejectedFresh.size()==1,"Fresh registry telemetry rejects wrong types and private values");
 check(!deckydiag::record("runtime",{{"registryReadKnown",true},{"registryReadError",5u}}).contains("registryReadKnown")&&
   !deckydiag::record("runtime",{{"registryReadError",5u}}).contains("registryReadError"),"Fresh registry telemetry cannot widen unrelated events");
 check(deckydiag::record("steam-mode-read",{{"registryReadPidKnown",1},{"registryReadPidPresent","true"},{"registryReadPidMatchesSession",0},{"registryReadPidError",-1},{"registeredPid",123u}}).size()==1,"PID telemetry rejects wrong types, signed errors and raw registry identities");
 check(deckydiag::record("runtime",{{"registryReadPidKnown",true},{"registryReadPidPresent",true},{"registryReadPidMatchesSession",true},{"registryReadPidError",5u}}).size()==1,"PID diagnostic fields cannot widen unrelated events");
 const auto invalid=deckysession::diagnostic(session,deckydiag::Json::array());
 check(invalid.value("proofStage",std::string{})=="mode-unconfirmed"&&!invalid.value("modeKnown",true),"Malformed snapshot remains safely unconfirmed in diagnostics");

 const auto beforeMissing=registryDiagnosticReads.load();
 const auto missingReceipt=deckydiag::record("steam-mode-read",deckysession::diagnosticWithIdentity(online,proofMode,{}));
 check(!missingReceipt.value("registrySnapshotAvailable",true)&&!missingReceipt.value("registryReadKnown",true)&&!missingReceipt.contains("registryReadMatchesUi"),"Missing shared identity remains unknown without a registry fallback");
 auto foreign=std::make_shared<steamsession::IdentitySnapshot>();foreign->session=online;foreign->session.created++;
 foreign->presence=1;foreign->accountReadError=ERROR_SUCCESS;foreign->registeredPidReadError=ERROR_SUCCESS;
 const auto stale=deckydiag::record("steam-mode-read",deckysession::diagnosticWithIdentity(online,proofMode,foreign));
 check(stale.value("registrySnapshotAvailable",false)&&!stale.value("registrySnapshotMatchesProcess",true)&&!stale.value("registryReadKnown",true)&&!stale.contains("registryReadMatchesUi"),"A receipt from another process birth cannot diagnose the current UI account");
 check(registryDiagnosticReads==beforeMissing,"Unknown or foreign shared identity never triggers consumer IO");
 check(!deckydiag::record("runtime",{{"registryReadFromSharedSnapshot",true},{"registrySnapshotAvailable",true}}).contains("registryReadFromSharedSnapshot"),"Shared identity telemetry stays within the original mode event");
}
#endif
#ifndef YMCC_READINESS_BASELINE

static void clientStateDiagnosticCases(){
 const steamsession::Session who{L"X:\\NeverRealSteam",123,42,456};
 const std::vector<std::string> states={"disposed","native-run-missing","binding-run-missing","run-mismatch","socket-missing","socket-connecting","socket-closing","socket-closed","socket-unavailable","transport-unconfirmed","opening-reply-missing","connected","module-dismounted","frontend-absent","frontend-inactive","read-failed"};
 for(const auto& label:states){
  auto value=mode(who,"bigpicture");value["observedAccount"]=42u;value["deckyClientConnected"]=false;value["deckyClientState"]=label;
  const auto detail=deckysession::diagnostic(who,value),record=deckydiag::record("steam-mode-read",detail);
  check(record.value("deckyClientState",std::string{})==label,"Finite client state reaches original mode diagnostic log");
  check(deckysession::bigPicture(who,value)&&!record.value("deckyClientConnected",true),"Observation label changes no original account or connection proof");
 }
 for(const auto& value:std::vector<deckydiag::Json>{nullptr,17,true,deckydiag::Json::object(),deckydiag::Json::array(),"https://private/token","not-a-known-state",std::string(10000,'x')}){
  auto m=mode(who,"bigpicture");m["deckyClientState"]=value;
  check(!deckysession::diagnostic(who,m).contains("deckyClientState")&&!deckydiag::record("steam-mode-read",{{"deckyClientState",value}}).contains("deckyClientState"),"Unknown or private client state cannot enter logs");
 }
 check(!deckydiag::record("runtime",{{"deckyClientState","socket-closed"}}).contains("deckyClientState"),"Client state stays within original mode event");
 for(const auto* label:{"unobserved","committed","native-ready-commit"}){
  auto value=mode(who,"bigpicture");value["deckyQamPanelState"]=label;
  const auto record=deckydiag::record("steam-mode-read",deckysession::diagnostic(who,value));
  check(record.value("deckyQamPanelState",std::string{})==label&&deckysession::bigPicture(who,value),"Panel commit evidence is carried without changing admission");
  value["birth"]="987";
  check(!deckysession::diagnostic(who,value).contains("deckyQamPanelState"),"A different process birth cannot claim the observed panel");
 }
 for(const auto& bad:std::vector<deckydiag::Json>{nullptr,true,23,"private-token"}){
  auto value=mode(who,"bigpicture");value["deckyQamPanelState"]=bad;
  check(!deckysession::diagnostic(who,value).contains("deckyQamPanelState")&&!deckydiag::record("steam-mode-read",{{"deckyQamPanelState",bad}}).contains("deckyQamPanelState"),"Only the fixed panel labels are logged");
 }
 auto desktop=mode(who,"desktop");desktop["deckyQamPanelState"]="committed";
 check(!deckysession::diagnostic(who,desktop).contains("deckyQamPanelState")&&!deckydiag::record("runtime",desktop).contains("deckyQamPanelState"),"Panel diagnostic stays within matching Big Picture mode read");
 const std::vector<std::string> backendStates={"socket-missing","socket-connecting","socket-open","socket-closing","socket-closed","socket-unavailable"};
 for(const auto& label:backendStates){
  auto value=mode(who,"bigpicture");value["observedAccount"]=42u;value["deckyBackendConnected"]=false;value["deckyBackendState"]=label;
  const auto before=value.dump();const auto record=deckydiag::record("steam-mode-read",deckysession::diagnostic(who,value));
  check(record.value("deckyBackendState",std::string{})==label,"Finite official backend state reaches original diagnostic projection and log");
  check(value.dump()==before&&deckysession::bigPicture(who,value)&&!record.value("deckyBackendConnected",true),"Backend state changes no admission, source or existing boolean proof");
 }
 for(const auto& bad:std::vector<deckydiag::Json>{nullptr,17,true,deckydiag::Json::object(),deckydiag::Json::array(),"https://private/token","connected",std::string(10000,'x')}){
  auto value=mode(who,"bigpicture");value["deckyBackendState"]=bad;
  check(!deckysession::diagnostic(who,value).contains("deckyBackendState")&&!deckydiag::record("steam-mode-read",{{"deckyBackendState",bad}}).contains("deckyBackendState"),"Malformed official backend state cannot leak through either native diagnostic boundary");
 }
 check(!deckydiag::record("runtime",{{"deckyBackendState","socket-closed"}}).contains("deckyBackendState"),"Official backend explanation stays within original mode log event");
 const auto reads=registryDiagnosticReads.load();for(unsigned i=0;i<1000;++i){auto m=mode(who,"bigpicture");m["deckyClientState"]="connected";deckydiag::record("steam-mode-read",deckysession::diagnostic(who,m));}
 check(registryDiagnosticReads==reads,"Repeated finite diagnostic projection has zero additional registry reads");
}
#endif
#ifndef YMCC_READINESS_BASELINE
static void tdpOwnerSettlementDiagnosticCases(){
 using deckydiag::Json;
 const std::string event="tdp-owner-settled";
 const auto valid=[&](const Json& row){return deckydiag::frontendRecord(row)==row&&deckydiag::record(event,row)==row;};
 const auto invalid=[&](const Json& row){return deckydiag::frontendRecord(row).is_null()&&deckydiag::record(event,row)==Json{{"event",event}};};
 const Json confirmed={{"event",event},{"generation",42u},{"requestedW",23u},{"outcome","write-ack"}};
 check(valid(confirmed),"Owner write acknowledgment survives both native filters");
 auto saved=confirmed;saved["outcome"]="saved-unconfirmed";saved["requestedW"]=22.5;
 check(valid(saved),"Saved-only report survives without an apply claim");
 for(const auto* field:{"event","generation","requestedW","outcome"}){
  auto row=confirmed;row.erase(field);check(invalid(row),"Incomplete TDP settlement is rejected");
 }
 for(const auto* field:{"requestId","account","path","error"}){
  auto row=confirmed;row[field]="PRIVATE";check(invalid(row),"Private field cannot enter a TDP settlement");
 }
 for(const auto& value:std::vector<Json>{0u,151u,true,"55",Json::object()}){
  auto row=confirmed;row["requestedW"]=value;check(invalid(row),"TDP wattage must be finite and within the diagnostic range");
 }
 for(const auto& value:std::vector<Json>{-1,9007199254740992ull,"42",true}){
  auto row=confirmed;row["generation"]=value;check(invalid(row),"TDP generation must be a bounded unsigned integer");
 }
 auto unknown=confirmed;unknown["outcome"]="readback";check(invalid(unknown),"An owner settlement cannot claim hardware readback");
 check(deckydiag::record("runtime",confirmed)==deckydiag::record("runtime",{{"generation",42u}}),"TDP-only fields do not widen other events or suppress the existing generation");
}
static void sourceChangeDiagnosticCases(){
 const std::vector<std::string> labels={"audio","monitor","input","touchpads","frames","schedule","profile","game","power","fan","controller","steam","trainer","speed","lossless","action","unspecified","multiple"};
 for(const auto& label:labels){
  const deckydiag::Json raw={{"event","snapshot-read-finished"},{"invalidatedBy",label},{"discarded",true},{"discardReason","source-changed"}};
  const auto clean=deckydiag::frontendRecord(raw),logged=deckydiag::record("snapshot-read-finished",clean);
  check(clean.value("invalidatedBy",std::string{})==label&&logged.value("invalidatedBy",std::string{})==label,"Fixed invalidation category survives both original native filters");
 }
 for(const auto& bad:std::vector<deckydiag::Json>{"C:/PRIVATE",true,42,deckydiag::Json::object(),deckydiag::Json::array()}){
  const deckydiag::Json raw={{"event","snapshot-read-finished"},{"invalidatedBy",bad}};
  check(!deckydiag::frontendRecord(raw).contains("invalidatedBy")&&!deckydiag::record("snapshot-read-finished",raw).contains("invalidatedBy"),"Unknown or wrongly typed source cannot enter diagnostics");
 }
 const deckydiag::Json other={{"event","snapshot-read-start"},{"invalidatedBy","monitor"}};
 check(!deckydiag::frontendRecord(other).contains("invalidatedBy")&&!deckydiag::record("runtime",other).contains("invalidatedBy"),"Source category cannot widen unrelated event scopes");
 const deckydiag::Json sources={"monitor","power","snapshot-request"};
 auto multiple=deckydiag::Json{{"event","snapshot-read-finished"},{"invalidatedBy","multiple"},{"discarded",true},{"discardReason","source-changed"},{"invalidatedBySources",sources}};
 const auto sanitized=deckydiag::frontendRecord(multiple);
 check(sanitized==multiple&&deckydiag::record("snapshot-read-finished",sanitized)==multiple,"Multiple distinct categories flow through both original native filters");
 for(const auto& bad:std::vector<deckydiag::Json>{"monitor",{{"power",true}},{"power"},{"power","power"},{"power","C:/PRIVATE"},{"power","multiple"},{"power",true}}){
  auto row=multiple;row["invalidatedBySources"]=bad;
  check(!deckydiag::frontendRecord(row).contains("invalidatedBySources")&&!deckydiag::record("snapshot-read-finished",row).contains("invalidatedBySources"),"Only distinct fixed diagnostic categories are accepted");
 }
 for(const auto* unrelated:{"snapshot-read-start","runtime"}){
  auto row=multiple;row["event"]=unrelated;
  check(!deckydiag::frontendRecord(row).contains("invalidatedBySources")&&!deckydiag::record(unrelated,row).contains("invalidatedBySources"),"Category list cannot widen unrelated event scopes");
 }
 for(const auto* reason:{"peer-changed","demand-changed"}){
  auto row=multiple;row["discardReason"]=reason;
  check(!deckydiag::frontendRecord(row).contains("invalidatedBySources")&&!deckydiag::record("snapshot-read-finished",row).contains("invalidatedBySources"),"A retired peer or demand has priority over the source list");
 }
 auto full=deckydiag::Json::parse(R"({"event":"snapshot-read-finished","readId":1,"demandEpoch":2,"elapsedMs":5,"ok":false,"stage":"partial","ready":true,"discarded":true,"failureKind":"owner-read","failureStage":"monitor","discardReason":"source-changed"})");
 for(const auto* field:{"powerMs","powerSourceMs","scheduleMs","performanceMs","topologyMs","gyroMs","framesMs","touchpadsMs","globalMs","monitorMs","inputMs","audioMs","profileMs","performanceScheduleMs","performanceGameConfigMs","performancePowerModeMs","performanceGameMs","performanceCoreMs","performanceTdpCapabilityMs","performanceCpuProfilesMs","performanceFrameRatesMs"})full[field]=1u;
 check(full.size()==32&&deckydiag::frontendRecord(full).size()==32,"Original32-field complete timing envelope remains accepted");
 full["extra"]=1u;check(deckydiag::frontendRecord(full).is_null(),"Original unqualified33-field extra envelope remains rejected");full.erase("extra");
 full["invalidatedBy"]="monitor";const auto all=deckydiag::frontendRecord(full),logged=deckydiag::record("snapshot-read-finished",all);
 check(full.size()==33&&all.size()==33&&logged.size()==33&&logged.value("invalidatedBy",std::string{})=="monitor","Exactly one qualified fixed source extends full timing envelope");
 full["extra"]=1u;check(deckydiag::frontendRecord(full).is_null(),"Oversized34-field source envelope is rejected");full.erase("extra");
 full["invalidatedBy"]="C:/PRIVATE";check(deckydiag::frontendRecord(full).is_null(),"An invalid source cannot grant the additional field slot");
 full["invalidatedBy"]="profile";
 const deckydiag::Json timings={{"queue",155u},{"settings",0u},{"main",35u},{"backup",5u}};
 full["profileReadMs"]=timings;
 const auto withTimings=deckydiag::frontendRecord(full);
 check(full.size()==34&&withTimings==full&&deckydiag::record("snapshot-read-finished",withTimings)==full,"Qualified profile timing survives both filters with the maximum old envelope");
 full["extra"]=1u;check(deckydiag::frontendRecord(full).is_null(),"The profile timing grants only one extra field");full.erase("extra");
 auto withSources=full;withSources["invalidatedBy"]="multiple";withSources["invalidatedBySources"]=sources;
 check(deckydiag::frontendRecord(withSources)==withSources,"The validated source list adds only one field to the old full envelope");
 withSources["extra"]=1u;check(deckydiag::frontendRecord(withSources).is_null(),"The source list does not allow unrelated extra fields");withSources.erase("extra");
 withSources["invalidatedBy"]="monitor";check(deckydiag::frontendRecord(withSources).is_null(),"A single source cannot grant the additional list slot");
 const deckydiag::Json part={{"queue",155u},{"main",2000u}};
 check(deckydiag::frontendRecord({{"event","snapshot-read-finished"},{"profileReadMs",part}}).at("profileReadMs")==part,"Unreached profile stages stay absent");
 for(const auto& bad:std::vector<deckydiag::Json>{deckydiag::Json::object(),{{"queue",0u},{"path","PRIVATE"}},{{"main",-1}},{{"main",true}},{{"backup",3600001u}}}){
  full["profileReadMs"]=bad;
  check(deckydiag::frontendRecord(full).is_null()&&!deckydiag::record("snapshot-read-finished",full).contains("profileReadMs"),"Invalid nested data cannot enlarge the envelope or reach the log");
 }
 check(!deckydiag::frontendRecord({{"event","snapshot-read-start"},{"profileReadMs",timings}}).contains("profileReadMs")&&!deckydiag::record("runtime",{{"profileReadMs",timings}}).contains("profileReadMs"),"Profile timing is confined to snapshot finish");
}
#endif

static void startupPhaseDiagnosticCases(){
 using Json=deckydiag::Json;
 for(const auto* phase:{"registered","native-context","fan-settings","ui-settings","placement","background-state","power-source","background-media","wake-state","fan-owner","boot-mode","performance","tray","ready"}){
  const Json input{{"event","startup-phase"},{"stage",phase},{"elapsedMs",120u},{"previousStage","registered"},{"previousMs",20u}};
  const auto first=deckydiag::frontendRecord(input);check(first==input,"Fixed startup phase survives renderer filter");
  check(deckydiag::record("startup-phase",first)==input,"Fixed startup phase survives native log projection");
 }
 const Json simple{{"event","startup-phase"},{"stage","registered"},{"elapsedMs",0u}};
 check(deckydiag::frontendRecord(simple)==simple,"First registration has no invented previous phase");
 for(const auto& change:std::vector<std::pair<const char*,Json>>{{"stage","C:/PRIVATE"},{"elapsedMs",-1},{"elapsedMs",true},{"elapsedMs",0.5},{"previousStage","SECRET"},{"previousMs",-1},{"previousMs",true}}){
  auto input=simple;input[change.first]=change.second;
  check(deckydiag::frontendRecord(input).is_null(),"Malformed or private startup fact is rejected");
 }
 auto bounded=simple;bounded["elapsedMs"]=999999999u;check(deckydiag::frontendRecord(bounded)["elapsedMs"]==3600000u,"Startup durations are capped");
 auto overflow=simple;overflow["previousStage"]="registered";overflow["previousMs"]=2u;overflow["extra"]=true;check(deckydiag::frontendRecord(overflow).is_null(),"Startup envelope is limited to five fields");
 check(!deckydiag::record("runtime",{{"previousStage","tray"},{"previousMs",4u}}).contains("previousStage"),"Startup facts cannot widen other event schemas");
}

static void modeBusyDiagnosticCases(){
 using Json=deckydiag::Json;const steamsession::Session who{L"X:\\NeverRealSteam",123,42,456};
 const Json local={{"known",true},{"running",true},{"mode","unknown"},{"pid",who.pid},{"birth",std::to_string(who.created)},{"reason","live-busy"},{"localReadBusy",true}};
 const auto inspect=[&](const Json& value,const char* expected){
  const auto original=value.dump();const auto account=deckysession::resolve(who,value);const auto admission=deckysession::bigPicture(who,value);const auto reads=registryDiagnosticReads.load();
  const auto detail=deckysession::diagnosticWithIdentity(who,value,{});const auto log=deckydiag::record("steam-mode-read",detail);
  check(detail.value("modeReadBusyKind",std::string{})==expected&&log.value("modeReadBusyKind",std::string{})==expected,"Exact busy classification survives original diagnostic and log filter");
  const auto after=deckysession::resolve(who,value);check(value.dump()==original&&account.value==after.value&&account.fromUi==after.fromUi&&admission==deckysession::bigPicture(who,value)&&reads==registryDiagnosticReads,"Busy diagnostic preserves input, business selection and zero registry reads");
  check(!log.contains("localReadBusy")&&!log.contains("observedAccount")&&!log.contains("token")&&!log.contains("path"),"Busy log contains only the fixed label and original safe fields");
 };
 inspect(local,"native-read-lock");auto remote=local;remote.erase("localReadBusy");inspect(remote,"shared-live-channel");
 for(unsigned variant=0;variant<11;++variant){auto value=local;switch(variant){case 0:value["localReadBusy"]=false;break;case 1:value["localReadBusy"]=1;break;case 2:value["localReadBusy"]="true";break;case 3:value["pid"]=who.pid+1;break;case 4:value["pid"]=double(who.pid);break;case 5:value["birth"]="wrong";break;case 6:value["known"]=false;break;case 7:value["running"]=false;break;case 8:value["mode"]="desktop";break;case 9:value["token"]="PRIVATE";break;case 10:value["known"]="true";break;}inspect(value,"unknown");}
 for(const Json& value:std::vector<Json>{mode(who,"desktop"),mode(who,"bigpicture"),Json(),Json::array(),Json(1)}){
  const auto detail=deckysession::diagnosticWithIdentity(who,value,{});check(!detail.contains("modeReadBusyKind")&&!deckydiag::record("steam-mode-read",detail).contains("modeReadBusyKind"),"Healthy and unavailable observations add no busy label");
 }
 for(const auto& invalid:std::vector<Json>{"C:/PRIVATE",1,true,Json(),Json::object(),"native-read-lock PRIVATE"})check(!deckydiag::record("steam-mode-read",{{"modeReadBusyKind",invalid}}).contains("modeReadBusyKind"),"Private, malformed or unknown busy labels are filtered");
 for(const char* event:{"runtime","connection-recovery","steam-css-read"})check(!deckydiag::record(event,{{"modeReadBusyKind","native-read-lock"}}).contains("modeReadBusyKind"),"Busy telemetry does not widen unrelated event filters");
}

static void cssReadyScopeDiagnosticCases(){
 using Json=deckydiag::Json;
 const Json input={{"reason","public-ready-scope"},{"generation",12055675u},{"suspendSerial",3u},{"notificationSequence",12u},{"steamPid",42u},{"steamBirth","100"},{"token","PRIVATE"}};
 const auto kept=deckydiag::record("steam-css-resume",input);
 check(kept.contains("suspendSerial")&&kept["suspendSerial"]==3u&&kept.contains("notificationSequence")&&kept["notificationSequence"]==12u,
       "Complete public Ready scope keeps both exact sequence counters");
 check(!kept.contains("token")&&kept["steamPid"]==42u&&kept["steamBirth"]=="100","Public scope preserves existing identity and no credentials");
 const auto checkPair=[&](const Json& invalid,const char* name){
  const auto row=deckydiag::record("steam-css-resume",invalid);
  check(!row.contains("suspendSerial")&&!row.contains("notificationSequence"),name);
 };
 for(const char* key:{"suspendSerial","notificationSequence"}){
  auto missing=input;missing.erase(key);checkPair(missing,"Missing counter cannot publish an incomplete scope");
  for(const auto& bad:std::vector<Json>{-1,true,"12",12.0,Json(),Json::object()}){
   auto malformed=input;malformed[key]=bad;checkPair(malformed,"Wrong counter type cannot publish an incomplete scope");
  }
 }
 auto baseline=input;baseline["suspendSerial"]=0u;baseline["notificationSequence"]=0u;checkPair(baseline,"Initial zero scope does not claim a wake");
 baseline["notificationSequence"]=UINT64_MAX;
 const auto exact=deckydiag::record("steam-css-resume",baseline);
 check(exact["suspendSerial"]==0u&&exact["notificationSequence"]==UINT64_MAX,"Notification-only scope and full unsigned counter remain lossless");
 auto other=input;other["reason"]="resume-css-recheck";checkPair(other,"Legacy resume diagnostics do not gain public counters");
 for(const char* event:{"runtime","steam-css-use","steam-css-guard","steam-mode-read"}){
  const auto unrelated=deckydiag::record(event,input);
  check(!unrelated.contains("suspendSerial")&&!unrelated.contains("notificationSequence"),"Public scope counters are not projected on unrelated events");
 }
}

static void modeCancelDiagnosticCases(){
 using Json=deckydiag::Json;
 const Json observed={{"reason","shared-mode-read-cancelled"},{"modeReadCancelCheck","power-scope"},
    {"modeReadStartPowerGeneration",37u},{"modeReadStartSuspendSerial",4u},{"observedAccount",42u}};
 const auto row=deckydiag::record("steam-mode-read",observed);
 check(row.value("modeReadCancelCheck",std::string{})=="power-scope"&&row.value("modeReadStartPowerGeneration",0u)==37u&&
    row.value("modeReadStartSuspendSerial",0u)==4u&&!row.contains("observedAccount"),"Cancelled read retains only the fixed check and its start scope");
#ifndef YMCC_MODE_CANCEL_BASELINE
 const auto omitted=[](const Json& item){return !item.contains("modeReadCancelCheck")&&!item.contains("modeReadStartPowerGeneration")&&!item.contains("modeReadStartSuspendSerial");};
 const std::array labels={"observer-fence","owner-exit","power-scope","identity-epoch","check-error"};
 for(unsigned denied=0;denied<labels.size();++denied){
  deckydiag::ModeReadCancelNote note;
  unsigned calls=0;
  const auto gate=[&](unsigned step){return [&,step]{
   if(++calls!=step+1)throw std::logic_error("order");
   if(denied==4&&step==1)throw std::runtime_error("private exception");
   return denied!=step;
  };};
  const bool refused=!note.current(gate(0),gate(1),gate(2),gate(3));
  check(refused&&calls==(denied==4?2:denied+1),"Checks stay in order and stop at first refusal");
  const auto no=[] {return false;};note.current(no,no,no,no);
  Json detail={{"reason","shared-mode-read-cancelled"}};note.attach(detail,37,0);
  const auto kept=deckydiag::record("steam-mode-read",detail);
  check(kept.value("modeReadCancelCheck",std::string{})==labels[denied]&&kept.contains("modeReadStartSuspendSerial")&&
        kept["modeReadStartSuspendSerial"]==0u,"First refusal is stable and initial serial zero is valid");
  detail["reason"]="live-busy";
  check(omitted(deckydiag::record("steam-mode-read",detail)),"Busy observation does not receive cancellation evidence");
 }
 deckydiag::ModeReadCancelNote note;const auto yes=[] {return true;};
 Json unknown={{"reason","shared-mode-read-cancelled"}};
 check(note.current(yes,yes,yes,yes),"Healthy checks keep their original result");
 note.attach(unknown,37,4);check(omitted(unknown),"Without an observed refusal the cause remains unknown");
 auto missing=observed;missing.erase("modeReadStartSuspendSerial");
 check(omitted(deckydiag::record("steam-mode-read",missing)),"Incomplete scope is omitted as a unit");
 for(const Json& bad:std::vector<Json>{-1,1.0,true,"4",Json(),Json::object()}){
  auto badSerial=observed;badSerial["modeReadStartSuspendSerial"]=bad;
  check(omitted(deckydiag::record("steam-mode-read",badSerial)),"Malformed scope cannot enter the log");
 }
 auto raw=observed;raw["modeReadCancelCheck"]="C:/private-or-unknown";
 check(omitted(deckydiag::record("steam-mode-read",raw))&&omitted(deckydiag::record("runtime",observed)),"Private labels and unrelated events remain filtered");
#endif
}

int main(int argc,char** argv){try{
 if(argc==2&&std::string(argv[1])=="--mode-cancel-diagnostics-only"){modeCancelDiagnosticCases();std::cout<<"DECKY_MODE_CANCEL_DIAGNOSTICS_OK passed="<<passed<<" realSteam=0\n";return 0;}
 if(argc==2&&std::string(argv[1])=="--css-ready-scope-diagnostics-only"){cssReadyScopeDiagnosticCases();std::cout<<"CSS_READY_SCOPE_DIAGNOSTICS_OK passed="<<passed<<" realSteam=0\n";return 0;}
 if(argc==2&&std::string(argv[1])=="--mode-busy-diagnostics-only"){modeBusyDiagnosticCases();std::cout<<"DECKY_MODE_BUSY_DIAGNOSTICS_OK passed="<<passed<<" realSteam=0\n";return 0;}
 if(argc==2&&std::string(argv[1])=="--startup-phase-diagnostics-only"){
   startupPhaseDiagnosticCases();
   using Json=deckydiag::Json;
   const auto raw=Json::parse(R"({"schema":1,"elapsedMs":4524,"bootstrapReadyMs":4250,"socketOpenMs":274,"bootstrapMisses":4,"socketAttempts":1,"retryCount":4,"scheduledRetryMs":14250})");
   bool recorded=false;Json detail=Json::object();const Json request={{"command","snapshot"},{"clientStartup",raw},{"token","PRIVATE"}};
   deckydiag::attachClientStartup(detail,request,recorded);
   check(recorded&&detail["clientStartup"]==raw,"First client timing is attached once without copying the request");
   check(deckydiag::record("mirror-snapshot-request",detail)["clientStartup"]==raw,"First client timing reaches the actual diagnostic allowlist");
   detail=Json::object();deckydiag::attachClientStartup(detail,request,recorded);
   check(detail.empty(),"Repeated snapshot cannot refill the native timing budget");
   for(const auto* key:{"elapsedMs","bootstrapReadyMs","socketOpenMs","scheduledRetryMs","bootstrapMisses","socketAttempts","retryCount"}){
     for(const auto& invalid:std::vector<Json>{-1,true,0.5,"PRIVATE",Json()}){
       auto bad=raw;bad[key]=invalid;bool used=false;Json observed=Json::object();deckydiag::attachClientStartup(observed,{{"clientStartup",bad}},used);
       check(observed.empty()&&!used&&!deckydiag::record("mirror-snapshot-request",{{"clientStartup",bad}}).contains("clientStartup"),"Malformed timing is omitted and does not consume the valid-record allowance");
     }
   }
   for(const auto* key:{"schema","elapsedMs","bootstrapReadyMs","socketOpenMs","scheduledRetryMs","bootstrapMisses","socketAttempts","retryCount"}){
     auto bad=raw;bad.erase(key);check(deckydiag::clientStartupRecord(bad).is_null(),"Incomplete first-client timing is omitted");
   }
   auto extra=raw;extra["token"]="PRIVATE";check(deckydiag::clientStartupRecord(extra).is_null(),"Extra private fields cannot enter the timing envelope");
   auto future=raw;future["socketOpenMs"]=4525;check(deckydiag::clientStartupRecord(future).is_null(),"Impossible socket duration is not a timing fact");
   auto excessive=raw;excessive["retryCount"]=1000001;check(deckydiag::clientStartupRecord(excessive).is_null(),"Timing counters are bounded");
   check(!deckydiag::record("runtime",{{"clientStartup",raw}}).contains("clientStartup"),"Client timing does not widen unrelated event schemas");
   for(const auto* source:{"socket-open","socket-open-probe","demand-start","demand-stop","connection-probe","hud-peer-ready","hidden-snapshot","operation-timeout"}){
     const Json packet={{"command","snapshot"},{"requestSource",source},{"probeId","probe:PRIVATE:1"},{"token","PRIVATE"}};
     Json fields=Json::object();deckydiag::attachSnapshotRequestSource(fields,packet);
     const Json expected={{"requestSource",source}};
     check(fields==expected&&deckydiag::record("mirror-snapshot-request",fields)==Json({{"event","mirror-snapshot-request"},{"requestSource",source}}),"Only fixed snapshot request provenance is attached and logged");
     check(!deckydiag::record("snapshot-read-start",fields).contains("requestSource"),"Request provenance does not leak into non-request events");
   }
   for(const Json& packet:std::vector<Json>{Json::object(),Json(),{{"requestSource","C:/PRIVATE"}},{{"requestSource","socket-open-extra"}},{{"requestSource",true}},{{"requestSource",1}},{{"requestSource",Json::array({"socket-open"})}},{{"requestSource",Json::object()}}}){
     Json fields=Json::object();deckydiag::attachSnapshotRequestSource(fields,packet);
     check(fields.empty()&&!deckydiag::record("mirror-snapshot-request",packet).contains("requestSource"),"Legacy, unknown and malformed provenance is omitted at both boundaries");
   }
   std::cout<<"DECKY_STARTUP_PHASE_DIAGNOSTICS_OK passed="<<passed<<" realSteam=0\n";return 0;
 }
#ifndef YMCC_READINESS_BASELINE
 if(argc==2&&std::string(argv[1])=="--tdp-owner-settlement-only"){tdpOwnerSettlementDiagnosticCases();std::cout<<"DECKY_TDP_OWNER_SETTLEMENT_OK passed="<<passed<<" realSteam=0\n";return 0;}
 if(argc==2&&std::string(argv[1])=="--source-change-diagnostics-only"){sourceChangeDiagnosticCases();std::cout<<"DECKY_SOURCE_CHANGE_DIAGNOSTICS_OK passed="<<passed<<" realSteam=0\n";return 0;}
#endif

#ifndef YMCC_READINESS_BASELINE
 if(argc==2&&std::string(argv[1])=="--client-state-diagnostics-only"){clientStateDiagnosticCases();std::cout<<"DECKY_CLIENT_STATE_DIAGNOSTICS_PASS checks="<<passed<<"\n";return 0;}
#endif
#ifndef YMCC_READINESS_BASELINE
 if(argc==2&&std::string(argv[1])=="--shared-identity-diagnostics-only"){
   steamsession::Session session{L"S:\\YMCC-private-diagnostic",123,0,456};
   auto proofMode=mode(session,"bigpicture");proofMode["observedAccount"]=42u;proofMode["deckyAccountWatchAvailable"]=true;
   sharedIdentityDiagnosticCases(session,proofMode);
   std::cout<<"DECKY_SHARED_IDENTITY_DIAGNOSTICS_PASS checks="<<passed<<"\n";return 0;
 }
#endif
 if(argc!=1)throw std::runtime_error("Unknown readiness diagnostic selection");

 const steamsession::Session session{L"X:\\NeverRealSteam",123,0,100};
 steamui::ModeObserver observer;deckysidebar::Runtime decky;
 std::atomic<unsigned> reads{0},changed{0},stable{0},preflight{0};std::atomic<bool> desktop{false};
 steamui::ModeObserverConfig c;c.debounceMs=15;c.minimumGapMs=180;c.failureGapMs=300;
 c.read=[&](const auto& s){++reads;return mode(s,desktop?"desktop":"bigpicture");};
 c.changed=[&](const auto&,const auto&){++changed;};
#ifndef YMCC_READINESS_BASELINE
 c.stableBigPicture=[&](const auto& s){++stable;if(s==session)decky.requestLaunchAdmissionRefresh();};
#endif
 check(observer.initialize(c),"Shared observer initialized with inert verified reader");
 observer.setDemand(steamui::ModeConsumer::Decky,true);observer.observeSteam(session);
 check(until([&]{return changed==1;}),"Initial genuine Big Picture change is delivered once");
 check(reads==1&&stable==0,"Mode edge does not also generate an unchanged-mode callback");
 check(observer.requestRefresh(session),"A new window hint invalidates the cached mode");
 check(observer.snapshot(session).value("mode",std::string{})=="unknown","Decky cannot consume the invalidated confirmation");
 deckysidebar::Config d;d.executable=L"NeverLaunch.exe";
 d.preflight=[&]{++preflight;return std::string("inert-preflight-stop");};
 d.launchAdmission=[&]{const auto state=observer.snapshot(session);return deckysidebar::LaunchAdmission{state.value("mode",std::string{})=="bigpicture","steam-ui-state-pending",true};};
 decky.initialize(d);check(true,"Real Decky worker initialized; all launch attempts stop at preflight");
 decky.observeSteam(1,true);decky.setEnabled(true);
 check(until([&]{return decky.snapshot().phase=="waiting-manual-steam";},100),"Decky is waiting while the shared mode is pending");
 const auto began=GetTickCount64();
 const bool prompt=until([&]{return preflight==1;},650);
 std::cout<<"UNCHANGED_MODE_ADMISSION elapsedMs="<<(GetTickCount64()-began)<<" reads="<<reads<<" stable="<<stable<<'\n';
 check(prompt,"Unchanged verified Big Picture completion wakes waiting Decky before its 1s/30s retry");
 check(changed==1&&stable==1&&reads==2,"One original read and one stable completion suffice; no duplicate reader");
 check(until([&]{return decky.snapshot().phase=="unavailable";}),"Original resource preflight remains authoritative");
 check(decky.snapshot().pid==0,"No real loader process was created");
 check(!decky.requestLaunchAdmissionRefresh(),"A completed/nonwaiting admission rejects extra wakes");
 observer.requestRefresh(session);check(until([&]{return stable==2;}),"A later genuine read may deliver another stable result");
 Sleep(60);check(preflight==1,"Nonwaiting Decky cannot restart from unchanged-mode notifications");
 desktop=true;observer.requestRefresh(session);check(until([&]{return observer.snapshot(session).value("mode",std::string{})=="desktop";}),"Desktop mode is observed normally");
 const auto old=stable.load();observer.requestRefresh(session);Sleep(260);check(stable==old,"Unchanged desktop mode cannot authorize Decky startup");
 auto stale=session;++stale.created;check(!observer.requestRefresh(stale),"A mismatched process birth cannot request current-session observation");
 observer.setDemand(steamui::ModeConsumer::Decky,false);const auto atStop=reads.load();Sleep(240);check(reads==atStop,"Disabled demand leaves no recurring UI reader");decky.shutdown();observer.shutdown();
#ifndef YMCC_READINESS_BASELINE
 sourceChangeDiagnosticCases();
 tdpOwnerSettlementDiagnosticCases();
 const auto input=deckydiag::Json::parse(R"({"event":"snapshot-read-finished","readId":4,"demandEpoch":2,"elapsedMs":500,"powerMs":2,"monitorMs":490,"ok":true,"stage":"settled","discarded":false,"token":"SECRET","game":"PRIVATE"})");
 const auto clean=deckydiag::frontendRecord(input),record=deckydiag::record("snapshot-read-finished",clean);
 check(clean.value("elapsedMs",0)==500&&record.value("monitorMs",0)==490,"Numeric stage timings survive both native allowlists");
 check(!clean.contains("token")&&!record.contains("game"),"Paths, tokens and game content never enter the telemetry");
  const auto recovered=deckydiag::frontendRecord(deckydiag::Json::parse(R"({"event":"snapshot-read-finished","ready":true,"performanceMs":123})"));
  check(deckydiag::record("snapshot-read-finished",recovered).value("ready",false)&&deckydiag::record("snapshot-read-finished",recovered).value("performanceMs",0)==123,"Read readiness and performance timing survive both filters");
  const auto held=deckydiag::frontendRecord(deckydiag::Json::parse(R"({"event":"snapshot-read-finished","ready":false,"performanceMs":-1})"));
  check(held.contains("ready")&&!held["ready"].get<bool>()&&!held.contains("performanceMs"),"Not-ready remains explicit and invalid timing cannot enter logs");
 const auto failureFacts=deckydiag::Json::parse(R"({"event":"snapshot-read-finished","failureKind":"source-changed","failureStage":"input","discardReason":"source-changed","token":"PRIVATE"})");
 const auto failureClean=deckydiag::frontendRecord(failureFacts),failureLog=deckydiag::record("snapshot-read-finished",failureClean);
 check(failureLog.value("failureKind",std::string{})=="source-changed"&&failureLog.value("failureStage",std::string{})=="input"&&failureLog.value("discardReason",std::string{})=="source-changed"&&!failureLog.contains("token"),"Fixed failure stage and discard reason survive both actual native filters without raw errors");
 const auto badFailure=deckydiag::Json::parse(R"({"event":"snapshot-read-finished","failureKind":"C:/PRIVATE","failureStage":true,"discardReason":"native-secret"})");
 check(deckydiag::frontendRecord(badFailure).size()==1&&deckydiag::record("snapshot-read-finished",badFailure).size()==1,"Arbitrary failure strings and wrong types are rejected by both filters");
 const auto unrelatedFailure=deckydiag::Json::parse(R"({"event":"snapshot-read-start","failureKind":"read-error","failureStage":"monitor","discardReason":"disposed"})");
 check(deckydiag::frontendRecord(unrelatedFailure).size()==1&&deckydiag::record("runtime",unrelatedFailure).size()==1,"Snapshot failure fields cannot widen unrelated native events");
 auto fullFailure=deckydiag::Json::parse(R"({"event":"snapshot-read-finished","readId":1,"demandEpoch":2,"elapsedMs":5,"ok":false,"stage":"partial","ready":true,"discarded":true,"failureKind":"owner-read","failureStage":"monitor","discardReason":"demand-changed"})");
 for(const auto* field:{"powerMs","powerSourceMs","scheduleMs","performanceMs","topologyMs","gyroMs","framesMs","touchpadsMs","globalMs","monitorMs","inputMs"})fullFailure[field]=1u;
 check(fullFailure.size()==22&&deckydiag::frontendRecord(fullFailure).size()==22&&deckydiag::record("snapshot-read-finished",deckydiag::frontendRecord(fullFailure)).size()==22,"All existing stage timings plus new bounded failure facts fit original22-field admission cap");
 for(const auto* field:{"audioMs","profileMs","performanceScheduleMs","performanceGameConfigMs","performancePowerModeMs","performanceGameMs","performanceCoreMs","performanceTdpCapabilityMs","performanceCpuProfilesMs","performanceFrameRatesMs"}){
  const auto cleanTiming=deckydiag::frontendRecord({{"event","snapshot-read-finished"},{field,37u}});
  check(cleanTiming.value(field,0)==37&&deckydiag::record("snapshot-read-finished",cleanTiming).value(field,0)==37,"Nested performance timing survives both actual filters");
  for(const auto& bad:std::vector<deckydiag::Json>{-1,true,"PRIVATE",deckydiag::Json::object()})check(!deckydiag::frontendRecord({{"event","snapshot-read-finished"},{field,bad}}).contains(field)&&!deckydiag::record("snapshot-read-finished",{{field,bad}}).contains(field),"Malformed nested timings do not widen telemetry");
  check(deckydiag::frontendRecord({{"event","snapshot-read-finished"},{field,999999999u}}).value(field,0)==3600000,"Nested timing remains bounded");
  check(!deckydiag::frontendRecord({{"event","snapshot-read-start"},{field,1u}}).contains(field)&&!deckydiag::record("runtime",{{field,1u}}).contains(field),"Nested timing has only its original finish event scope");
  fullFailure[field]=1u;
 }
 check(fullFailure.size()==32&&deckydiag::frontendRecord(fullFailure).size()==32&&deckydiag::record("snapshot-read-finished",deckydiag::frontendRecord(fullFailure)).size()==32,"Full fixed32-field timing envelope is preserved");
 fullFailure["extra"]=1u;check(deckydiag::frontendRecord(fullFailure).is_null(),"Oversized33-field timing envelope still rejected");
 check(deckydiag::frontendRecord({{"event","arbitrary-log"}}).is_null(),"Unknown diagnostic events are rejected");
 check(deckydiag::frontendRecord(deckydiag::Json::parse(R"({"event":"snapshot-read-start","readId":"42","elapsedMs":-1,"stage":"C:/secret"})")).size()==1,"Wrong scalar types and arbitrary text are omitted");
 check(deckydiag::frontendRecord(deckydiag::Json::parse(R"({"event":"snapshot-read-finished","elapsedMs":999999999})")).value("elapsedMs",0)==3600000,"Telemetry timings are capped");
 check(deckydiag::record("snapshot-demand",{{"subscribe",false},{"demandEpoch",3}}).contains("subscribe"),"Pause demand is distinguishable from an active snapshot request");
 check(deckydiag::record("mirror-snapshot-request",{{"subscribe",true}}).value("subscribe",false),"Native request logs preserve the true subscription direction");
 const std::string id(64,'a');check(deckydiag::validBuildId(id),"Fixed-size lower-case hex build fingerprint is accepted");
 check(!deckydiag::validBuildId("path/secret")&&!deckydiag::validBuildId(std::string(65,'a'))&&!deckydiag::validBuildId(std::string(64,'G')),"Arbitrary or overlong build labels are rejected");
 check(deckydiag::record("plugin-build",{{"buildId",id},{"token","SECRET"}}).size()==2,"Build receipt includes only its fingerprint and event");
 check(!deckydiag::record("snapshot-read-finished",{{"buildId",id}}).contains("buildId"),"Build identity has its own dedicated event scope");
 const auto sessionDiagnostic=deckydiag::record("steam-session-check",{{"stage","binding-identity-incomplete"},{"steamPid",123u},{"steamBirth","456"},{"pidPresent",true},{"birthPresent",true},{"accountPresent",false},{"rootPresent",true},{"error",0u},{"account",12345u},{"root","C:/PRIVATE"},{"token","SECRET"}});
 check(sessionDiagnostic.value("stage",std::string{})=="binding-identity-incomplete"&&sessionDiagnostic.value("steamPid",0u)==123&&sessionDiagnostic.contains("accountPresent")&&!sessionDiagnostic["accountPresent"].get<bool>(),"Session failure reports its missing condition with the observed process lease");
 check(!sessionDiagnostic.contains("account")&&!sessionDiagnostic.contains("root")&&!sessionDiagnostic.contains("token"),"Session diagnostic never exposes account values, paths or tokens");
 const auto invalidSessionDiagnostic=deckydiag::record("steam-session-check",{{"stage","C:/PRIVATE"},{"steamPid","123"},{"steamBirth","birth/secret"},{"pidPresent",1},{"error",-1},{"enabled",true}});
 check(invalidSessionDiagnostic.size()==1,"Session diagnostic rejects arbitrary stages, wrong types and generic payload fields");
 check(!deckydiag::record("runtime",{{"accountPresent",true}}).contains("accountPresent"),"Session presence flags cannot widen unrelated events");
 // Historical Win5: an account can exist while PID/birth are missing. These
 // are projections of the already-read snapshot, not extra reads or authority.
 auto proofMode=mode(session,"bigpicture");proofMode["observedAccount"]=42u;proofMode["deckyAccountWatchAvailable"]=true;
 const auto projected=[&](const steamsession::Session& who,const steamui::ModeJson& value){
     const auto sourceBefore=value.dump();const auto accountBefore=deckysession::resolve(who,value);
     auto detail=deckysession::diagnostic(who,value);detail["steamPid"]=who.pid;detail["steamBirth"]=std::to_string(who.created);
     const auto result=deckydiag::record("steam-mode-read",detail);const auto accountAfter=deckysession::resolve(who,value);
     check(value.dump()==sourceBefore&&accountBefore.value==accountAfter.value&&accountBefore.fromUi==accountAfter.fromUi,"Diagnostic projection leaves the source and original account admission unchanged");
     return result;
 };
 auto proof=projected(session,proofMode);
 check(proof.value("proofStage",std::string{})=="verified-steam-ui"&&proof.value("modeIdentityConfirmed",false)&&proof.value("uiAccountWatchAvailable",false),"Verified offline UI proof survives the existing native log filter");
 check(!proof.value("deckyBackendKnown",true)&&!proof.contains("deckyBackendConnected")&&
   !proof.value("deckyHooksKnown",true)&&!proof.contains("deckyHooksInstalled"),"Absent official layer evidence stays unknown, not false");
 for(const bool backend:{false,true})for(const bool hooks:{false,true})for(const bool client:{false,true}){
   auto observed=proofMode;observed["deckyBackendConnected"]=backend;observed["deckyHooksInstalled"]=hooks;observed["deckyClientConnected"]=client;
   const auto count=registryDiagnosticReads.load();const auto layers=projected(session,observed);
   check(layers.value("deckyBackendKnown",false)&&layers.at("deckyBackendConnected")==backend&&
     layers.value("deckyHooksKnown",false)&&layers.at("deckyHooksInstalled")==hooks&&layers.value("deckyClientKnown",false)&&layers.at("deckyClientConnected")==client&&registryDiagnosticReads==count,
     "Official backend and hooks survive the actual projection and filter independently, without registry IO");
 }
 auto malformedLayers=proofMode;malformedLayers["deckyBackendConnected"]="true";malformedLayers["deckyHooksInstalled"]=1;malformedLayers["deckyClientConnected"]="true";
 const auto unknownLayers=projected(session,malformedLayers);
 check(!unknownLayers.value("deckyBackendKnown",true)&&!unknownLayers.contains("deckyBackendConnected")&&
   !unknownLayers.value("deckyHooksKnown",true)&&!unknownLayers.contains("deckyHooksInstalled")&&!unknownLayers.value("deckyClientKnown",true)&&!unknownLayers.contains("deckyClientConnected"),"Malformed official layer fields cannot imply connected or disconnected");
 check(deckydiag::record("runtime",{{"deckyBackendKnown",true},{"deckyBackendConnected",true},{"deckyHooksKnown",true},{"deckyHooksInstalled",true}}).size()==1&&
   deckydiag::record("steam-mode-read",{{"deckyBackendKnown",1},{"deckyBackendConnected","true"},{"deckyHooksKnown",0},{"deckyHooksInstalled",1}}).size()==1,
   "Official status fields reject wrong types and cannot widen unrelated events");
 auto missing=session;missing.pid=0;missing.created=0;missing.account=42;
 proof=projected(missing,proofMode);
 check(proof.value("proofStage",std::string{})=="session-pid-missing"&&proof.value("registryAccountPresent",false)&&!proof.value("sessionPidPresent",true),"Win5 missing process is distinguishable from an absent account");
 missing=session;missing.created=0;check(projected(missing,proofMode).value("proofStage",std::string{})=="session-birth-missing","Missing birth has its own diagnostic branch");
 missing=session;missing.root.clear();check(projected(missing,proofMode).value("proofStage",std::string{})=="session-root-missing","Missing root has its own diagnostic branch without logging a path");
 auto value=proofMode;value["mode"]="unknown";check(projected(session,value).value("proofStage",std::string{})=="mode-unconfirmed","Unknown UI does not masquerade as account rejection");
 value=proofMode;value["pid"]=session.pid+1;check(projected(session,value).value("proofStage",std::string{})=="mode-identity-mismatch","Stale PID proof is diagnosed without changing admission");
 value=proofMode;value["birth"]="999";check(projected(session,value).value("proofStage",std::string{})=="mode-identity-mismatch","Stale birth proof is diagnosed without changing admission");
 value=proofMode;value["mode"]="desktop";check(projected(session,value).value("proofStage",std::string{})=="desktop","Confirmed desktop is separate from failed account evidence");
 value=proofMode;value.erase("observedAccount");check(projected(session,value).value("proofStage",std::string{})=="ui-account-missing","Missing UI account is explicit");
 value=proofMode;value.erase("deckyAccountWatchAvailable");proof=projected(session,value);
 check(proof.value("proofStage",std::string{})=="ui-account-watch-unconfirmed"&&!proof.value("uiAccountWatchKnown",true),"Absent watch result remains unknown rather than a proven false callback");
 value=proofMode;value["deckyAccountWatchAvailable"]=false;proof=projected(session,value);
 check(proof.value("proofStage",std::string{})=="ui-account-watch-unconfirmed"&&proof.value("uiAccountWatchKnown",false)&&!proof.value("uiAccountWatchAvailable",true),"Known false account watch is distinguishable from a missing field");
 auto online=session;online.account=42;check(projected(online,proofMode).value("proofStage",std::string{})=="registry-account","Ordinary registry account provenance remains explicit");
 online.account=43;proof=projected(online,proofMode);
 check(proof.value("proofStage",std::string{})=="account-mismatch"&&proof.value("uiRegistryAccountComparable",false)&&!proof.value("uiRegistryAccountMatches",true),"A genuine account conflict is explicit without disclosing either account value");
 check(!proof.contains("observedAccount")&&!proof.contains("account")&&!proof.contains("root")&&!proof.contains("url"),"Account-proof telemetry has no account numbers, paths or URLs");
 check(deckydiag::record("steam-mode-read",{{"proofStage","C:/SECRET"},{"modeKnown",1},{"uiAccountWatchAvailable","true"},{"steamPid",-1},{"steamBirth","private/birth"},{"observedAccount",42u},{"token","SECRET"}}).size()==1,"Arbitrary proof labels and wrong types cannot enter proof telemetry");
 const auto other=deckydiag::record("runtime",deckysession::diagnostic(session,proofMode));
 check(!other.contains("proofStage")&&!other.contains("uiAccountPresent")&&!other.contains("modeIdentityConfirmed"),"Proof fields do not widen unrelated event logging");
 sharedIdentityDiagnosticCases(session,proofMode);clientStateDiagnosticCases();
#endif
 std::cout<<"DECKY_READINESS_DIAGNOSTICS_OK passed="<<passed<<" realSteam=0 realLoader=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
#endif

