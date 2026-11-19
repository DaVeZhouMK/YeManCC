// Actual manual vibration IPC and LED receipt closures; all writers are memory.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');if(!process.argv[2])throw Error('Output directory required');const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});const sourceFile=path.resolve(process.env.YMCC_MANUAL_FEEDBACK_MAIN_SOURCE||path.join(repo,'native/main.cpp'));const main=fs.readFileSync(sourceFile,'utf8');const ordinaryLog=process.argv[3]==='--ordinary-log',apiProvenance=process.argv[3]==='--xinput-provenance',targetProbe=process.argv[3]==='--xinput-target',writeAudit=process.argv[3]==='--xinput-write-audit';if(process.argv.length>4||process.argv[3]&&!ordinaryLog&&!apiProvenance&&!targetProbe&&!writeAudit)throw Error('Unsupported manual feedback selection');
const start=main.indexOf('    ipc_on("gamepad.vibrate",');const end=main.indexOf('\n    });',start)+8;if(start<0||end<start)throw Error('Missing manual vibration handler');const handler=main.slice(start,end);
const ledStart=main.indexOf('    ipc_on("gamepad.setLed",'),ls=main.indexOf('        const auto receipt =',ledStart),le=main.indexOf('\n        };',ls)+11;if(ls<0||le<ls)throw Error('Missing LED receipt');const led=main.slice(ls,le);
let cpp='#include '+JSON.stringify(path.join(repo,'native/oem_lighting_operations.h').replaceAll('\\','/'))+'\n'+String.raw`#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
struct {std::string deviceClass;int family=0;}g_machineIdentity;
struct {int auraControl=-1;unsigned auraFeatureLen=64;}g_rog;
constexpr int INVALID_HANDLE_VALUE=-1;
static std::string ymccFamilyName(int family){return family==1?"msi-claw":family==2?"asus-rog-ally-x":family==7?"onexplayer":family>=4?"ayanero":"gpd";}
static std::vector<json> logs;static json pulseResponse;static int pulseCalls=0,seenStrength=0,seenDuration=0;
static void appendNativeLifecycleLog(const char* name,const json& body){logs.push_back(json{{"event",name},{"body",body}});}
static json nativeGamepadFeedbackPulse(int strength,int duration){++pulseCalls;seenStrength=strength;seenDuration=duration;return pulseResponse;}
static json invokeVibrate(const json& a){auto ipc_on=[](const char*,auto callback){return callback;};auto callback=
`+handler+String.raw`
;return callback(a);}
static json invokeLedReceipt(const json& a,const json& body){
 const std::string capabilityId="hc.device.fixture";
 const bool restoreOem=false;const std::string ownership="ymcc";const bool enabled=a.value("enabled",true);const std::string mode=a.value("mode",std::string{"solid"}),presetId=a.value("presetId",std::string{});int speed=65,brightness=100;
`+led+String.raw`
 return receipt(body);}
int main(){json modelReceipts=json::object();unsigned checks=0;auto check=[&](bool ok,const char* n){++checks;if(!ok)throw std::runtime_error(n);};try{
 const std::vector<json> responses={
  {{"ok",true},{"backend","msi-dinput-hid"},{"attempted",true},{"written",2},{"strength",70},{"durationMs",125},{"fieldRumbleVerified",false}},
  {{"ok",false},{"backend","msi-dinput-hid"},{"attempted",false},{"written",0},{"reason","source-changed"}},
  {{"ok",false},{"backend","msi-dinput-hid"},{"attempted",true},{"written",1},{"reason","write-failed"}},
  {{"ok",true},{"backend","xinput-physical"},{"attempted",true},{"rc",0},{"slot",2}},
  {{"ok",false},{"backend","xinput-physical"},{"attempted",false},{"reason","no-xinput-physical-source"}},
  {{"ok",false},{"backend","none"},{"attempted",false},{"reason","power-transition"}},
  {{"ok",false},{"backend","xinput-physical"},{"attempted",true},{"reason","stop-unavailable"}},
  {{"ok",false},{"backend","xinput-physical"},{"attempted",true},{"reason","pulse-stop-unavailable"}},
  {{"ok",false},{"backend","xinput-physical"},{"attempted",true},{"reason","write-failed"},{"rc",5}},
  {{"ok",false},{"backend","xinput-physical"},{"attempted",false},{"reason","source-changed"}}
 };
 for(int family:{1,2,3,4,5,6,7})for(const auto& response:responses)for(bool defaults:{false,true}){
  if(family>=4&&response.value("backend",std::string{})=="msi-dinput-hid")continue;
  g_machineIdentity={family==1?"HandheldCompanion.Devices.ClawCG3EM":family==2?"HandheldCompanion.Devices.XboxROGAllyX":family==4?"HandheldCompanion.Devices.AYANEOAIR1S":family==5?"HandheldCompanion.Devices.AYANEO3":family==6?"HandheldCompanion.Devices.AYANEONEXT2":family==7?"HandheldCompanion.Devices.OneXPlayerApex":"HandheldCompanion.Devices.GPDWin5",family};pulseResponse=response;const auto count=logs.size();const int called=pulseCalls;
  const json args=defaults?json::object():json{{"strength",137},{"durationMs",2}};const auto result=invokeVibrate(args);
  check(result==response,"IPC response unchanged for every backend/failure");check(pulseCalls==called+1,"only one backend invocation");check(seenStrength==(defaults?70:137)&&seenDuration==(defaults?125:2),"original pulse argument/default contract retained");check(logs.size()==count+1,"exactly one request receipt on all returns");
  const auto& log=logs.back();check(log.at("event")=="gamepad-vibrate-request","request event independent from low-level XInput pulse event");const auto& body=log.at("body");
  for(auto it=response.begin();it!=response.end();++it)check(body.at(it.key())==it.value(),"actual backend result preserved in log");
  check(body.at("deviceClass")==g_machineIdentity.deviceClass&&body.at("family")==ymccFamilyName(family),"manual receipt bound to actual model identity");check(body.at("physicalEffectVerified")==false&&body.at("proofScope")=="request-and-backend-receipt","write receipt cannot claim physical effect");check(body.at("requestedStrength")==seenStrength&&body.at("requestedDurationMs")==seenDuration,"requested vs returned clamped values distinguishable");if(defaults){auto& row=modelReceipts[g_machineIdentity.deviceClass];row["family"]=ymccFamilyName(family);if(!row.contains("receipts"))row["receipts"]=json::array();row["receipts"].push_back(result);row["lastRequestLog"]=body;}
 }
 for(bool enabled:{false,true})for(const auto& result:std::vector<json>{{{"ok",false},{"reason","power-transition"}},{{"ok",false},{"reason","invalid-color"}},{{"ok",true},{"effectApplied",true},{"brightnessApplied",true},{"backend","msi-hid"}}}){
  const json a={{"enabled",enabled},{"mode","ambilight"},{"color","#ff0000"},{"color2","#0000ff"}};const auto n=logs.size();const auto returned=invokeLedReceipt(a,result);auto expected=result;expected["capabilityId"]=result.value("capabilityId",std::string{"hc.device.fixture"});expected["requestedCapabilityId"]="hc.device.fixture";expected["capabilityCatalogSha256"]=ymcc::lighting::kCatalogSha256;expected["physicalEffectVerified"]=false;check(returned==expected,"LED response retains outcome and adds unified identity/proof metadata");check(logs.size()==n+1,"one LED receipt including early failure");const auto& b=logs.back().at("body");check(b.at("requestedColor")=="#ff0000"&&b.at("requestedColor2")=="#0000ff","both requested colors are available for physical side evidence");check(b.at("physicalEffectVerified")==false,"LED software write does not claim physical effect");check(b.at("schema")==2&&b.at("deviceClass")==g_machineIdentity.deviceClass,"LED receipt version and family identity");
 }
 const json helper={{"queuedWriteManaged",true},{"queuedWritesCompleted",true},{"queuedWriteTasks",3},{"queuedWriteFailures",1}};
 auto actual=invokeLedReceipt(json{{"mode","solid"}},json{{"ok",true},{"capabilityId","hc.device.LokiMax6800U"},{"requestedCapabilityId","hc.device.AynLoki"}});check(actual.at("capabilityId")=="hc.device.LokiMax6800U"&&logs.back().at("body").at("capabilityId")=="hc.device.LokiMax6800U","validated concrete helper identity survives outer receipt and log");
 invokeLedReceipt(json{{"mode","preset"},{"presetId","LEDPreset_OneXPlayerX1_Preset09"}},json{{"ok",false},{"reason","light-setter-exit-1"},{"note","onex-write-queue-failed"},{"helperReceipt",helper}});
 check(logs.back().at("body").at("helperReceipt")==helper&&logs.back().at("body").at("backendNote")=="onex-write-queue-failed","backend queue failure evidence survives ordinary exported LED receipt");
 check(logs.back().at("body").at("physicalEffectVerified")==false&&logs.back().at("body").at("ok")==false,"queue receipt cannot promote write failure to physical success");
 std::ofstream("NATIVE-RECEIPTS.json")<<json{{"schemaVersion",1},{"models",modelReceipts},{"checks",checks},{"hardwareIo",false},{"backendWrites","memory"}}.dump(2);
 std::cout<<"MANUAL_FEEDBACK_RECEIPT_OK checks="<<checks<<" actual_ipc_closures=true backend_writes=fake hardware_io=none\n";return 0;
 }catch(const std::exception& e){std::cerr<<"MANUAL_FEEDBACK_RECEIPT_FAIL "<<e.what()<<'\n';return 1;}}
`;
if(ordinaryLog){
 const block=signature=>{const a=main.indexOf(signature),b=main.indexOf('\n}',a);if(a<0||b<a)throw Error('Missing actual logging consumer '+signature);return main.slice(a,b+2);};
 const include=file=>'#include '+JSON.stringify(path.join(repo,file).replaceAll('\\','/'))+'\n';
 const preamble=String.raw`
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string_view>
`+include('native/sleep_fact_log_record.h')+include('native/log_frequency_policy.h')+include('native/log_snapshot_state.h')+include('native/log_jsonl_boundary.h')+include('native/input_host_diagnostic_dispatch.h');
 const inert=String.raw`
namespace fspath=std::filesystem;
static std::wstring logRoot;
static std::array<std::atomic<bool>,3>g_domainLogEnabled{};
static bool domainLogEnabled(const char* name){return g_domainLogEnabled[std::strcmp(name,"virtual")==0?1:0].load();}
// Explicit IPC/logger scope: the actual diagnostic Scope remains inactive.
// Unexpected entry into a Host diagnostic queue must fail, never become a no-op.
static void inputHostDeferNativeDiagnostic(const char*,json,bool,bool=false) noexcept {std::abort();}
static std::atomic<unsigned long long>g_powerGeneration{7};static std::atomic<int>g_powerLifecycle{0};static HWND g_hwnd=nullptr;
static std::mutex g_nativeLifecycleLogMx;static std::ofstream g_nativeLifecycleLog,g_nativeDetailLog;
static uint32_t g_nativeLifecycleFrameWrites=0,g_nativeDetailWrites=0;static ULONGLONG g_nativeDetailLastFlushTick=0;
static constexpr unsigned long long kLogMaxBytes=4ull*1024*1024;
static std::wstring app_data_dir(){return logRoot;}
static const char* powerLifecycleName(int){return "ready";}
static std::string nativeLifecycleTimestamp(){return "fixture-time";}
static void modelAdaptationObserveRecord(const char*,const json&){}
static void virtualAdaptationObserveRecord(const char*,const json&){}
static json nativeSteamGameLinkLogDetail(const json& row){return row;}
static void rotateOneLogFile(const std::wstring&){throw std::runtime_error("unexpected fixture rotation");}
static void mirrorLifecycleToDomains(const char* name,const json& body){logs.push_back(json{{"event",name},{"body",body}});}
static std::vector<json> fileRows(const std::wstring& leaf){std::ifstream input(fspath::path(logRoot)/leaf);std::vector<json> rows;std::string line;while(std::getline(input,line))rows.push_back(json::parse(line));return rows;}
`;
 const memoryAppend='static void appendNativeLifecycleLog(const char* name,const json& body){logs.push_back(json{{"event",name},{"body",body}});}';
 if(cpp.split(memoryAppend).length!==2)throw Error('Actual receipt fixture append seam changed');
 const preparedSignature='static void appendNativeLifecyclePrepared(const char* event,const json& detail,bool baseEvent,const std::string* encoded) {';
 const prepared=main.includes(preparedSignature)?block(preparedSignature)+'\n':'';
 const logger=inert+'\n'+block('static bool nativeLifecycleEventIsBase(const char* event) {')+'\n'+block('static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {')+'\n'+prepared+block('static void appendNativeLifecycleLog(const char* event, const json& observed) {');
 cpp=preamble+cpp.replace('constexpr int INVALID_HANDLE_VALUE=-1;','').replace('struct {int auraControl=-1;','struct {HANDLE auraControl=INVALID_HANDLE_VALUE;').replace(memoryAppend,logger);
 cpp=cpp.replace(';return callback(a);}', ';auto request=a;request["feedbackSource"]="manual-test";return callback(request);}');
 cpp=cpp.replace('int main(){json modelReceipts=',String.raw`int main(int argc,char**argv){if(argc!=2)throw std::runtime_error("explicit fresh log root required");logRoot=fspath::path(argv[1]).wstring();if(fspath::exists(logRoot)||!fspath::create_directory(logRoot))throw std::runtime_error("fresh owned log root required");json modelReceipts=`);
 const checks=String.raw`
 g_nativeLifecycleLog.flush();
 std::vector<json> expected;for(const auto& row:logs)if(row.at("event")=="gamepad-vibrate-request")expected.push_back(row.at("body"));
 std::vector<json> recorded;for(const auto& row:fileRows(L"native-lifecycle.log"))if(row.value("event","")=="gamepad-vibrate-request")recorded.push_back(row);
 check(!expected.empty()&&recorded.size()==expected.size(),"ordinary log must retain every explicit vibration result with detailed logs OFF");
 std::vector<std::string> failedReasons;
 for(size_t i=0;i<expected.size();++i){const auto& want=expected[i];const auto& got=recorded[i];for(auto it=want.begin();it!=want.end();++it)check(got.at(it.key())==it.value(),"exact response fields reach ordinary file");check(got.at("physicalEffectVerified")==false,"disk receipt cannot prove motor vibration");if(!got.value("ok",false))failedReasons.push_back(got.value("reason",std::string{}));}
 for(const char* reason:{"no-xinput-physical-source","source-changed","write-failed","power-transition","pulse-stop-unavailable"})check(std::find(failedReasons.begin(),failedReasons.end(),reason)!=failedReasons.end(),"ordinary file loses refusal/failure reason");
 const auto classifiedBefore=fileRows(L"native-lifecycle.log").size();
 for(const char* source:{"unclassified","gyro-simulation","","manual-test-extra"})appendNativeLifecycleLog("gamepad-vibrate-request",json{{"requestSource",source},{"ok",true},{"physicalEffectVerified",false}});
 g_nativeLifecycleLog.flush();check(fileRows(L"native-lifecycle.log").size()==classifiedBefore,"unclassified/simulation requests must retain detailed lane");
 const auto ordinaryBefore=fileRows(L"native-lifecycle.log").size();appendNativeLifecycleLog("input-native-signal-window",json{{"fixture",true}});g_nativeLifecycleLog.flush();check(fileRows(L"native-lifecycle.log").size()==ordinaryBefore,"raw/periodic input must not become ordinary evidence");check(!fspath::exists(fspath::path(logRoot)/L"native-detail.log"),"detail OFF must not create the detailed file");
 g_domainLogEnabled[1]=true;const auto callsBefore=pulseCalls;const auto detailCount=fileRows(L"native-detail.log").size();invokeVibrate(json::object());g_nativeLifecycleLog.flush();check(pulseCalls==callsBefore+1&&fileRows(L"native-lifecycle.log").size()==ordinaryBefore+1,"detail ON does not move manual request out of ordinary ledger");check(fileRows(L"native-detail.log").size()==detailCount,"manual request is not duplicated into detail");
 g_domainLogEnabled[1]=false;
 const auto capsBefore=fileRows(L"native-lifecycle.log").size();
 const json capsDeclaration={{"schema",3},{"deviceClass","HandheldCompanion.Devices.AYANEOSlide"},
  {"capabilityId","hc.device.AYANEOSlide"},{"capabilityCatalogSha256",ymcc::lighting::kCatalogSha256},
  {"oemRestoreAvailable",true},{"oemOwnershipProtocol","ceii-ram"},{"sharedChargeIndicatorDeclared",true},
  {"ledReason","ok"},{"brightnessAvailable",true},{"presetImplemented",false},
  {"hardwareWriteIssued",false},{"ledVerified",false},{"vibrationVerified",false}};
 appendNativeLifecycleLog("gamepad.feedbackCaps",capsDeclaration);g_nativeLifecycleLog.flush();
 auto capsRows=fileRows(L"native-lifecycle.log");check(capsRows.size()==capsBefore+1,"ordinary log retains OEM/charge capability metadata with detailed logs off");
 for(auto it=capsDeclaration.begin();it!=capsDeclaration.end();++it)check(capsRows.back().at(it.key())==it.value(),"OEM/charge capability field survives actual logger and snapshot filter");
 appendNativeLifecycleLog("gamepad.feedbackCaps",capsDeclaration);g_nativeLifecycleLog.flush();
 check(fileRows(L"native-lifecycle.log").size()==capsBefore+1,"unchanged capability metadata keeps original snapshot deduplication");
 auto refusedDeclaration=capsDeclaration;refusedDeclaration["oemRestoreAvailable"]=false;refusedDeclaration["oemOwnershipProtocol"]="";
 appendNativeLifecycleLog("gamepad.feedbackCaps",refusedDeclaration);g_nativeLifecycleLog.flush();
 capsRows=fileRows(L"native-lifecycle.log");check(capsRows.size()==capsBefore+2&&capsRows.back().at("oemRestoreAvailable")==false&&capsRows.back().at("oemOwnershipProtocol")=="","changed unavailable ownership metadata cannot be swallowed by deduplication");
 check(capsRows.back().at("ledVerified")==false&&capsRows.back().at("hardwareWriteIssued")==false,"charging declaration must not become a physical effect or hardware write");
 std::ofstream("ORDINARY-LOG-RESULT.json")<<json{{"status","PASS"},{"checks",checks},{"ordinaryManualRows",recorded.size()+1},{"detailedOffRows",recorded.size()},{"allBackendResponsesRetained",true},{"actualLogger",true},{"actualLanePolicy",true},{"actualFileSink",true},{"hardwareIo",false},{"modelCallbacks","inert"},{"fileRoot",fspath::path(logRoot).string()}}.dump(2);
 `;
 const at=' std::ofstream("NATIVE-RECEIPTS.json")';if(cpp.split(at).length!==2)throw Error('Missing original receipt output');cpp=cpp.replace(at,checks+'\n'+at);
 fs.writeFileSync(path.join(out,'LOGGING-SOURCE.json'),JSON.stringify({sourceFile,mainSha256:crypto.createHash('sha256').update(main).digest('hex'),functions:['static bool nativeLifecycleEventIsBase(const char* event) {','static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {',...(prepared?[preparedSignature]:[]),'static void appendNativeLifecycleLog(const char* event, const json& observed) {'].map(signature=>({signature,sha256:crypto.createHash('sha256').update(block(signature)).digest('hex')})),realFileSink:true,hostDiagnosticScope:'actual Scope inactive; unexpected defer aborts',hardwareIo:false},null,2),{flag:'wx'});
}

let provenanceInputs=[];
if(writeAudit){
 const auditStart=main.indexOf('struct XInputFeedbackWriteAudit {'),auditEnd=main.indexOf('\n};',auditStart)+3;
 if(auditStart<0||auditEnd<=auditStart)throw Error('Missing actual output audit');
 const fn=name=>{const m=new RegExp(String.raw`^static [^\n;]*\b${name}\([^;]*?\)\s*\{[\s\S]*?\n\}`,'m').exec(main);if(!m)throw Error('Missing write audit consumer '+name);return m[0];};
 const actual=main.slice(auditStart,auditEnd)+'\nstatic XInputFeedbackWriteAudit g_xinputFeedbackWriteAudit;\n'+fn('nativeXInputFeedbackWriteEvidence')+'\n'+fn('ymccXInputSetState14');
 provenanceInputs.push({symbol:'actual-XInput-writer-and-read-only-audit',sha256:crypto.createHash('sha256').update(actual).digest('hex')});
 cpp=String.raw`#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;using DWORD=uint32_t;using WORD=uint16_t;
constexpr DWORD ERROR_DEVICE_NOT_CONNECTED=1167,ERROR_SUCCESS=0;
struct XINPUT_VIBRATION{WORD wLeftMotorSpeed,wRightMotorSpeed;};
static unsigned calls=0,resolves=0;static DWORD response=0;static bool resolved=true;static uint64_t tick=1;
static DWORD writer(DWORD,XINPUT_VIBRATION*){++calls;return response;}
static auto ymccResolveXInputSetState14()->DWORD(*)(DWORD,XINPUT_VIBRATION*){++resolves;return resolved?writer:nullptr;}
static uint64_t GetTickCount64(){return tick;}
`+actual+String.raw`
int main(){unsigned checks=0;auto check=[&](bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);};try{
 auto snapshot=nativeXInputFeedbackWriteEvidence();check(calls==0&&resolves==0&&snapshot.at("attempts")==0&&!snapshot.at("lastWriteKnown").get<bool>(),"idle snapshot never resolves or writes");
 check(snapshot.at("providerInternalWritesObserved")==false&&snapshot.at("allProcessOutputCovered")==false,"zero wrapper calls cannot exclude provider lifecycle output");
 check(snapshot.at("scope").get<std::string>().find("provider-internal-and-other-process-output-not-observed")!=std::string::npos,"output counter reports its actual wrapper boundary");
 XINPUT_VIBRATION pulse{17,29},zero{};
 check(ymccXInputSetState14(2,&pulse)==0&&calls==1,"original nonzero invocation unchanged");
 tick=126;response=5;check(ymccXInputSetState14(2,&zero)==5&&calls==2,"failed zero remains failed");
 tick=626;response=0;check(ymccXInputSetState14(2,&zero)==0&&calls==3,"accepted zero retains original return");
 snapshot=nativeXInputFeedbackWriteEvidence();check(snapshot.at("attempts")==3&&snapshot.at("completed")==3&&snapshot.at("nonzeroAttempts")==1&&snapshot.at("zeroAttempts")==2,"actual call counts");
 check(snapshot.at("nonzeroApiAccepted")==1&&snapshot.at("zeroApiAccepted")==1,"attempt and acceptance separate");
 check(snapshot.at("lastSlot")==2&&snapshot.at("lastLeft")==0&&snapshot.at("lastRight")==0&&snapshot.at("lastCompletedTick")==626,"last completed request retained");
 check(snapshot.at("hardwareStopObserved")==false,"API zero is not motor proof");
 auto reads=calls;auto resolveReads=resolves;for(int n=0;n<50;++n)(void)nativeXInputFeedbackWriteEvidence();check(calls==reads&&resolves==resolveReads,"repeated snapshots are passive");
 resolved=false;check(ymccXInputSetState14(0,&pulse)==1167&&calls==3,"unresolved API is not invoked");resolved=true;
 check(ymccXInputSetState14(0,nullptr)==1167&&calls==3,"null request is not invoked");
 snapshot=nativeXInputFeedbackWriteEvidence();check(snapshot.at("attempts")==3,"non-invocations never fabricate output");
 std::cout<<json{{"pass",true},{"checks",checks},{"actualWriterExecuted",true},{"hardwareIo",false},{"outputCalls",calls}}.dump();return 0;
 }catch(const std::exception&e){std::cerr<<e.what();return 1;}}
`;
}
if(apiProvenance||targetProbe){
 const fn=name=>{const m=new RegExp(String.raw`^static [^\n;]*\b${name}\([^;]*?\)\s*\{[\s\S]*?\n\}`,'m').exec(main);if(!m)throw Error('Missing feedback provenance source '+name);provenanceInputs.push({symbol:name,sha256:crypto.createHash('sha256').update(m[0]).digest('hex')});return m[0];};
 const definition=pattern=>{const m=main.match(pattern);if(!m)throw Error('Missing provenance declaration '+pattern);provenanceInputs.push({definition:m[0],sha256:crypto.createHash('sha256').update(m[0]).digest('hex')});return m[0];};
 const manifest=main.match(/                    if\(g_machineIdentity.family==YmccFamily::OneXPlayer(?: \|\| g_machineIdentity.family==YmccFamily::Aokzoe)?\) \{\r?\n[\s\S]*?\r?\n                    \}/)?.[0];if(!manifest)throw Error('Missing ordinary XInput export consumer');
 provenanceInputs.push({symbol:'ordinary-OneX-AOKZOE-manifest-consumer',sha256:crypto.createHash('sha256').update(manifest).digest('hex')});
 const harnessPath=path.join(repo,'tools/fixtures/xinput_output_api_provenance.cpp'),harness=fs.readFileSync(harnessPath,'utf8');
 provenanceInputs.push({file:harnessPath,sha256:crypto.createHash('sha256').update(harness).digest('hex')});
 cpp=harness.replace('/*ACTUAL-SHA-CONSUMER*/',fn('sha256FileCancellable'))
  .replace('/*ACTUAL-PROVENANCE-CONSUMERS*/',[
   definition(/^static std::atomic<std::shared_ptr<const json>> g_xinputOutputApiEvidence;/m),
   fn('nativeXInputFunctionModuleEvidence'),fn('nativeFeedbackModuleDiskEvidence'),fn('nativeXInputOutputApiExportEvidence'),fn('ymccResolveXInputSetState14')].join('\n\n'))
  .replace('/*ACTUAL-MANIFEST-CONSUMER*/',manifest);
 cpp=cpp.replace('/*ACTUAL-TARGET-CONSUMER*/',[
  definition(/^static constexpr DWORD kOpenXInput_ERROR_SUCCESS = [^\n]+/m),
  fn('nativeManualXInputTargetEvidence'),fn('nativeManualXInputCapabilitiesEvidence')].join('\n\n'));
 if(targetProbe)cpp='#define YMCC_XINPUT_TARGET_PROBE\n'+cpp;
 for(const [marker,value]of [['A','sample-A'],['A2','sample-A-replaced'],['B','sample-B']])cpp=cpp.replaceAll('/*SHA-'+marker+'*/',JSON.stringify(crypto.createHash('sha256').update(value).digest('hex')));
 if(cpp.includes('/*ACTUAL-'))throw Error('Unbound provenance fixture seam');
}
fs.writeFileSync(path.join(out,'actual-manual-feedback-receipt.cpp'),cpp);const sha=x=>crypto.createHash('sha256').update(x).digest('hex');fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({sourceFile,mainSha256:sha(main),vibrationHandlerSha256:sha(handler),ledClosureSha256:sha(led),fixtureSha256:sha(cpp),selection:writeAudit?'xinput-write-audit':targetProbe?'xinput-target':apiProvenance?'xinput-provenance':ordinaryLog?'ordinary-log':'receipts',provenanceInputs,hardwareIo:false,backendWrites:'memory fake'},null,2));console.log('Generated actual manual feedback consumer harness: '+(writeAudit?'xinput-write-audit':targetProbe?'xinput-target':apiProvenance?'xinput-provenance':ordinaryLog?'ordinary-log':'receipts'));
