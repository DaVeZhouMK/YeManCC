// Executes the actual native IPC selector and actual ROG/MSI packet consumers at memory IO terminals.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {spawnSync} from 'node:child_process';import {createHash,randomUUID} from 'node:crypto';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const entryStarted=Date.now();
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
let prepareOnly=false,selectedCase=null;
if(args.length<2||args[0]!=='--out'||!args[1]||args[1].startsWith('--'))throw Error('Usage --out isolated-evidence-directory [--case x2mini-owner] [--prepare-only]');
for(let i=2;i<args.length;i++){
 if(args[i]==='--prepare-only'&&!prepareOnly)prepareOnly=true;
 else if(args[i]==='--case'&&selectedCase===null){selectedCase=args[++i];if(!['x2mini-owner','apex-discovery'].includes(selectedCase))throw Error('Unknown or empty --case');}
 else throw Error('Unknown, repeated or unsupported native lighting selection');
}
const out=path.resolve(args[1]);if(selectedCase&&fs.existsSync(path.join(out,'selection.json')))throw Error('Existing directed attempt/evidence must not be overwritten');fs.mkdirSync(out,{recursive:true});const mainBytes=fs.readFileSync(path.join(repo,'native/main.cpp')),main=mainBytes.toString('utf8').replace(/\r\n/g,'\n');
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),sourceHash=createHash('sha256').update(mainBytes).digest('hex');
const extractedFunctions=new Map(),includedSources=new Map();
const compilerInputs=new Map(),dependencyReports=new Set();
function captureCompilerInputs(){
 for(const name of fs.readdirSync(out).filter(n=>n.endsWith('.dependencies.json'))){
  const file=path.join(out,name);if(dependencyReports.has(file))continue;
  const report=JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
  if(!report.Data||!report.Data.Source||!Array.isArray(report.Data.Includes))throw Error('Malformed actual MSVC dependency report: '+file);
  for(const input of [report.Data.Source,...report.Data.Includes]){
   const full=path.resolve(input),hash=sha(full),key=full.toLowerCase(),old=compilerInputs.get(key);
   if(old&&old.sha256!==hash)throw Error('Native compiler input changed between builds: '+full);
   compilerInputs.set(key,{path:full,sha256:hash,bytes:fs.statSync(full).size});
  }
  dependencyReports.add(file);
 }
}

const fragmentHash=text=>createHash('sha256').update(text).digest('hex');
function extract(text,signature){const a=text.indexOf(signature),b=text.indexOf('\n}',a);if(a<0||b<0)throw Error('Missing function '+signature);return text.slice(a,b+2);}
function block(signature){const text=extract(main,signature);extractedFunctions.set(signature,fragmentHash(text));return text;}

if(selectedCase==='apex-discovery'){
 const selected=['no-matching-interface','exact-current-interface','wrong-descriptor','ambiguous-interface','metadata-open-failure','descriptor-unavailable','topology-changed','candidate-report-bound','unrelated-interface-not-probed','context-blocked','aya-receives-no-onex-log','diagnostic-failure-keeps-selection'];
 const selector=block('static bool superJoyLightingSelectInterface('),context=block('static ymcc::superjoy_owner::Context superJoyLightingContext()'),baseEvent=block('static bool nativeLifecycleEventIsBase(const char* event) {');
 const globalsStart=main.indexOf('static std::atomic<std::uint64_t> g_superJoyLightingTopology{1};'),globalsEnd=main.indexOf('static ymcc::superjoy_owner::Owner& superJoyLightingOwner()',globalsStart);
 if(globalsStart<0||globalsEnd<0)throw Error('Actual discovery globals absent');const globals=main.slice(globalsStart,globalsEnd);
 const ownerHeader=path.join(repo,'native/superjoy_lighting_owner.h'),portsFile=path.join(repo,'tools/fixtures/hc_lighting_catalog/superjoy-native-ports.inc');includedSources.set('native/superjoy_lighting_owner.h',sha(ownerHeader));includedSources.set('tools/fixtures/hc_lighting_catalog/superjoy-native-ports.inc',sha(portsFile));
 const prefix=String.raw`#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <dbt.h>
#include <atomic>
#include <functional>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
static bool powerAllowed=true,g_sjResumeFail=false,logThrows=false;
static bool hardwareWriteAllowed(){return powerAllowed;}
static struct {std::string deviceClass;} g_machineIdentity;
static json logs=json::array();
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
static bool nativeLifecycleEventIsBase(const char*);
static void appendNativeLifecycleLog(const char* name,const json& body){if(logThrows)throw std::runtime_error("fixture-log-write-error");if(nativeLifecycleEventIsBase(name))logs.push_back({{"event",name},{"body",body}});}
`+'#include '+JSON.stringify(ownerHeader.replaceAll('\\','/'))+'\n'+fs.readFileSync(portsFile,'utf8')+'\n'+globals+'\n'+baseEvent+'\n'+context+'\n';
 const tests=String.raw`
int main(){int checks=0;std::vector<std::string> actual,failures;
 auto check=[&](bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);};
 const std::wstring path=L"\\\\?\\hid#vid_1a2c&pid_b001#memory-rgb";
 SuperJoyFixtureCandidate good{path,0x1A2C,0xB001,0xFF01,1,65,65};
 auto reset=[&]{g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";powerAllowed=true;g_exitRequested=false;g_superJoyLightingStopped=false;g_superJoyLightingNotifyReady=true;g_sjFixturePower=7;g_superJoyLightingTopology=1;g_sjCandidates.clear();g_sjProbes=g_sjSetsDestroyed=0;g_sjDiscoveryMutation={};logs=json::array();logThrows=false;};
 auto receipt=[&]() -> json {check(logs.size()==1,"ordinary-log-receipt-required");const auto row=logs[0]["body"];check(logs[0]["event"]=="onex-lighting-discovery","wrong-event");check(!row["hardwareWriteProven"].get<bool>()&&!row["physicalEffectProven"].get<bool>(),"diagnostics-claimed-hardware-success");check(row["candidates"].size()<=4,"candidate-report-unbounded");return row;};
 auto run=[&](const char* name,auto test){actual.push_back(name);reset();try{test();}catch(const std::exception& e){failures.push_back(std::string(name)+":"+e.what());}};
 std::wstring selected;std::string reason;
 run("no-matching-interface",[&]{check(!superJoyLightingSelectInterface(selected,reason)&&selected.empty(),"missing-endpoint-admitted");const auto r=receipt();check(r["reason"]=="superjoy-endpoint-unavailable"&&r["candidateCount"]==0&&r["enumeratedInterfaces"]==0,"missing-endpoint-evidence");check(g_sjProbes==0&&g_sjSetsDestroyed==1,"enumeration-not-reused");});
 run("exact-current-interface",[&]{g_sjCandidates={good};check(superJoyLightingSelectInterface(selected,reason)&&selected==path,"matching-endpoint-rejected");const auto r=receipt(),c=r["candidates"][0];check(r["selected"]==true&&c["descriptorMatched"]==true,"matching-result-missing");check(c["vid"]==0x1A2C&&c["pid"]==0xB001&&c["usagePage"]==0xFF01&&c["usage"]==1&&c["outputReportBytes"]==65,"descriptor-not-actual");check(g_sjProbes==1&&g_sjSetsDestroyed==1,"extra-hardware-probe");});
 run("wrong-descriptor",[&]{auto bad=good;bad.page=1;g_sjCandidates={bad};check(!superJoyLightingSelectInterface(selected,reason),"wrong-descriptor-admitted");const auto r=receipt();check(r["candidates"][0]["usagePage"]==1&&r["candidates"][0]["descriptorMatched"]==false,"wrong-descriptor-hidden");});
 run("ambiguous-interface",[&]{auto second=good;second.path+=L"-other";g_sjCandidates={good,second};check(!superJoyLightingSelectInterface(selected,reason)&&selected.empty(),"ambiguous-admitted");const auto r=receipt();check(r["reason"]=="superjoy-endpoint-ambiguous"&&r["candidateCount"]==2,"ambiguous-evidence");});
 run("metadata-open-failure",[&]{auto bad=good;bad.readable=false;g_sjCandidates={bad};check(!superJoyLightingSelectInterface(selected,reason),"failed-open-admitted");const auto r=receipt(),c=r["candidates"][0];check(r["reason"]=="superjoy-endpoint-observation-failed"&&c["openedForMetadata"]==false&&c["vid"].is_null()&&c["usage"].is_null(),"unknown-descriptor-fabricated");});
 run("descriptor-unavailable",[&]{auto bad=good;bad.descriptor=false;g_sjCandidates={bad};check(!superJoyLightingSelectInterface(selected,reason),"missing-caps-admitted");const auto r=receipt(),c=r["candidates"][0];check(r["reason"]=="superjoy-endpoint-descriptor-unavailable"&&c["vid"]==0x1A2C&&c["capsObserved"]==false&&c["outputReportBytes"].is_null(),"missing-caps-not-unknown");});
 run("topology-changed",[&]{g_sjCandidates={good};g_sjDiscoveryMutation=[]{g_superJoyLightingTopology.fetch_add(1);};check(!superJoyLightingSelectInterface(selected,reason)&&selected.empty(),"changed-context-admitted");check(receipt()["reason"]=="superjoy-owner-context-changed","changed-context-not-logged");});
 run("candidate-report-bound",[&]{for(int i=0;i<7;i++){auto bad=good;bad.page=1;bad.path+=std::to_wstring(i);g_sjCandidates.push_back(bad);}check(!superJoyLightingSelectInterface(selected,reason),"invalid-candidates-admitted");const auto r=receipt();check(r["candidateCount"]==7&&r["candidates"].size()==4&&r["candidatesOmitted"]==3,"candidate-bound-accounting");check(g_sjProbes==7,"diagnostics-added-probes");});
 run("unrelated-interface-not-probed",[&]{auto other=good;other.path=L"\\\\?\\hid#vid_1234&pid_5678#other";g_sjCandidates={other};check(!superJoyLightingSelectInterface(selected,reason)&&g_sjProbes==0,"unrelated-interface-probed");const auto r=receipt();check(r["enumeratedInterfaces"]==1&&r["candidateCount"]==0,"unrelated-count-incorrect");});
 run("context-blocked",[&]{g_superJoyLightingNotifyReady=false;g_sjCandidates={good};check(!superJoyLightingSelectInterface(selected,reason)&&g_sjProbes==0&&g_sjSetsDestroyed==0,"blocked-context-enumerated");check(receipt()["reason"]=="superjoy-owner-context-unavailable","blocked-context-not-reported");});
 run("aya-receives-no-onex-log",[&]{g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEO3";auto aya=good;aya.path=L"\\\\?\\hid#vid_1c4f&pid_0002#aya";aya.vendor=0x1C4F;aya.product=2;aya.page=0xFF00;g_sjCandidates={aya};check(superJoyLightingSelectInterface(selected,reason)&&selected==aya.path,"aya-selection-changed");check(logs.empty()&&g_sjProbes==1,"aya-polluted-by-onex-diagnostics");});
 run("diagnostic-failure-keeps-selection",[&]{g_sjCandidates={good};logThrows=true;check(superJoyLightingSelectInterface(selected,reason)&&selected==path,"diagnostic-failure-altered-selection");});
 json result={{"status",failures.empty()?"PASS":"FAIL"},{"cases",actual.size()},{"actual",actual},{"checks",checks},{"failures",failures},{"hardwareIo",false},{"fullProcessHostExecuted",false}};std::cout<<result.dump();return failures.empty()?0:1;}
`;
 const code=prefix+selector+'\n'+tests;
 const begin=selector.indexOf('appendNativeLifecycleLog("onex-lighting-discovery", {'),end=selector.indexOf('});',begin)+3;if(begin<0||end<3)throw Error('Exact diagnostic fault anchor missing');
 const fault=selector.slice(0,begin)+'throw std::runtime_error("fixture-disabled-discovery-evidence");'+selector.slice(end);
 const programs=[['candidate',code],['missing-evidence-negative',prefix+fault+'\n'+tests]];const steps=[];
 const run=(exe,argv,label,expected=0)=>{const started=Date.now(),r=spawnSync(exe,argv,{cwd:out,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:2*1024*1024});fs.writeFileSync(path.join(out,label+'.stdout.log'),r.stdout??'',{flag:'wx'});fs.writeFileSync(path.join(out,label+'.stderr.log'),r.stderr??'',{flag:'wx'});steps.push({label,exitCode:r.status,signal:r.signal,error:r.error?String(r.error):null,elapsedMs:Date.now()-started});if(r.status!==expected||r.error)throw Error('Apex discovery step failed: '+label+' '+JSON.stringify(steps.at(-1)));return r.stdout;};
 fs.writeFileSync(path.join(out,'selection.json'),JSON.stringify({selector:selectedCase,selected,defaultExecuted:false,functions:[...extractedFunctions],includes:[...includedSources],hardwareIo:false},null,2),{flag:'wx'});
 try{
  for(const [name,cpp] of programs){fs.writeFileSync(path.join(out,name+'.cpp'),cpp,{flag:'wx'});const recipe=path.join(out,name+'.cmd');fs.writeFileSync(recipe,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+`cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "${out}/${name}.dependencies.json" "${out}/${name}.cpp" /I"${repo}/deps/json" /Fo"${out}/${name}.obj" /Fe"${out}/${name}.exe" /link Setupapi.lib Hid.lib User32.lib\r\nexit /b %errorlevel%\r\n`);run('cmd.exe',['/d','/c',recipe],name+'-compile');captureCompilerInputs();}
  let result={status:'NOT_RUN_PREPARE_ONLY',actual:[],checks:0};
  if(!prepareOnly){result=JSON.parse(run(path.join(out,'candidate.exe'),[],'candidate-execute'));if(result.status!=='PASS'||JSON.stringify(result.actual)!==JSON.stringify(selected))throw Error('Apex discovery actual selection differs');const negative=JSON.parse(run(path.join(out,'missing-evidence-negative.exe'),[],'missing-evidence-negative-execute',1));if(!negative.failures.some(s=>s.includes('ordinary-log-receipt-required')))throw Error('Missing-evidence fault did not fail the expected assertion');result.semanticNegative={compiled:true,exitCode:1,detected:true,expectedAssertion:'ordinary-log-receipt-required'};}
  const current=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replaceAll('\r\n','\n');for(const [signature,expected] of extractedFunctions)if(fragmentHash(extract(current,signature))!==expected)throw Error('Actual discovery source changed during test: '+signature);if(!current.includes(globals))throw Error('Discovery state changed during test');for(const [relative,expected] of includedSources)if(sha(path.join(repo,relative))!==expected)throw Error('Discovery included source changed: '+relative);for(const input of compilerInputs.values())if(sha(input.path)!==input.sha256)throw Error('Actual compiler dependency changed: '+input.path);
  fs.writeFileSync(path.join(out,'native-compiler-inputs.json'),JSON.stringify({files:[...compilerInputs.values()],reports:[...dependencyReports]},null,2),{flag:'wx'});
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({...result,selector:selectedCase,sourceHash,defaultExecuted:false,steps,elapsedMs:Date.now()-entryStarted,hardwareIo:false,fullProcessHostExecuted:false},null,2),{flag:'wx'});console.log(JSON.stringify({status:result.status,selector:selectedCase,cases:result.cases,checks:result.checks,semanticNegative:result.semanticNegative}));process.exit(0);
 }catch(error){fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify({status:'FAIL',error:String(error.stack??error),steps,defaultExecuted:false,hardwareIo:false},null,2),{flag:'wx'});throw error;}
}

const start=main.indexOf('    ipc_on("gamepad.setLed"'),end=main.indexOf('\n    });',start)+8;if(start<0||end<=start)throw Error('Missing actual setLed IPC');const handler=main.slice(start,end);
const enums=main.slice(main.indexOf('enum class RogAuraMode : BYTE'),main.indexOf('// （g_rogAuraMx'));
const msiStart=main.indexOf('static constexpr int kMsiLedZoneCount'),msiEnd=main.indexOf('// MSI Claw 灯光应用：',msiStart);if(msiStart<0||msiEnd<0)throw Error('Missing MSI packet builders');
const include=p=>{includedSources.set(p,sha(path.join(repo,p)));return '#include '+JSON.stringify(path.join(repo,p).replaceAll('\\','/'))+'\n';};
const ownerFakes=String.raw`
static std::function<bool()> ownerGetterLockProbe;
static int ownerGettersOutsideLock=0,ownerGettersInsideLock=0;
static void observeOwnerGetter(){if(ownerGetterLockProbe){if(ownerGetterLockProbe())++ownerGettersInsideLock;else ++ownerGettersOutsideLock;}}
namespace ymcc { namespace onex_x2 {
enum class Phase {Detached,Requested,Live};
struct Snapshot {Phase phase;std::uint64_t generation;bool writePending;Model requested=Model::OneXPlayer3,bound=Model::OneXPlayer3;bool initialized=true;};
class WindowsTransport {
public: Snapshot state{Phase::Live,7,true};bool has=true;bool clearPending=true;int pendingTicks=0;std::wstring path=L"path";int ticks=0;std::function<void(int)> afterTick;
Snapshot snapshot()const{observeOwnerGetter();return state;}bool hasDevice()const{observeOwnerGetter();return has;}const std::wstring& devicePath()const{observeOwnerGetter();return path;}
void tick(std::uint64_t,bool(*)(),bool(*)(const std::wstring&)){++ticks;if(afterTick)afterTick(ticks);if(clearPending)state.writePending=false;else if(pendingTicks>0&&--pendingTicks==0)state.writePending=false;}
};}}
static std::atomic<std::uint64_t> fakeNow{0};static std::uint64_t GetTickCount64(){return ++fakeNow;}
static void Sleep(int ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms>0?ms:1));}
static int eventCreates=0,eventResets=0,eventSets=0;static std::wstring lastEventName;
static HANDLE CreateEventW(void*,int,int,const wchar_t* name){++eventCreates;lastEventName=name;return 77;}
static int ResetEvent(HANDLE){++eventResets;return 1;}static int SetEvent(HANDLE){++eventSets;return 1;}
static ymcc::oxp3_owner::HandoffRecord fakeHandoffRecord{};
static HANDLE CreateFileMappingW(HANDLE,void*,DWORD,DWORD,DWORD,const wchar_t*){return 55;}
static void* MapViewOfFile(HANDLE,DWORD,DWORD,DWORD,size_t){return &fakeHandoffRecord;}
static std::atomic<int> fakeUnmapCalls{0};
static constexpr DWORD PAGE_READWRITE=0x04;static constexpr DWORD FILE_MAP_ALL_ACCESS=0xF001F;
static bool onexX2OwnerWriteAllowed();static bool onexX2OwnerBindAllowed(const std::wstring&);
static std::string W2U(const std::wstring& w){return std::string(w.begin(),w.end());}
static bool hardwareWriteAllowed();
static bool onexX2BindingPathAllowed(const std::wstring&){return true;}
static ymcc::onex_x2::WindowsTransport g_onexX2Transport;static std::mutex g_onexX2OwnerMx;
static std::atomic<std::uint64_t> g_onexX2DeviceEpoch{9};static ymcc::oxp3_owner::Lease g_oxp3OwnerLease;
static ymcc::oxp3_owner::OwnerState g_oxp3OwnerState=ymcc::oxp3_owner::OwnerState::Idle;
static std::mutex g_oxp3OwnerStateMx;
static HANDLE g_oxp3OwnerRevokedEvent=0;static std::wstring g_oxp3OwnerEventName,g_oxp3OwnerInterfacePath;
static HANDLE g_oxp3OwnerHandoffMapping=0;static ymcc::oxp3_owner::HandoffRecord* g_oxp3OwnerHandoff=&fakeHandoffRecord;
static std::atomic<bool> g_oxp3OwnerPendingDebt{false},g_oxp3OwnerStopTimedOut{false};
static std::atomic<bool> g_oxp3OwnerApplyOutstanding{false},g_oxp3OwnerHelperAlive{false};
static HANDLE g_oxp3OwnerRetiredProcess=0;static std::string g_oxp3OwnerRetiredProcessToken;
static std::atomic<bool> g_oxp3OwnerChildReturnObserved{false};
static std::atomic<std::uint64_t> g_oxp3OwnerAdmissionSequence{0};
static std::mutex g_legacyLightSetterDebtMx;static HANDLE g_legacyLightSetterRetiredProcess=0;
static bool fakeRetiredSignaled=false;static int WaitForSingleObject(HANDLE,DWORD){return fakeRetiredSignaled?0:258;}
static bool oxp3OwnerDenyWrite(){return false;}static bool oxp3OwnerDenyBind(const std::wstring&){return false;}
`;
const cpp=String.raw`#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;using BYTE=unsigned char;using DWORD=std::uint32_t;using ULONG=std::uint32_t;using HANDLE=std::intptr_t;using ULONGLONG=std::uint64_t;
constexpr HANDLE INVALID_HANDLE_VALUE=-1;constexpr int FALSE=0;constexpr int TRUE=1;constexpr int WAIT_OBJECT_0=0;
enum class YmccFamily {Unknown,OneXPlayer,MsiClaw,AsusRogAllyX,AsusRogAlly,AsusRogAllyClassic,AsusRogAllyXClassic};
struct {std::string deviceClass;YmccFamily family=YmccFamily::Unknown;}g_machineIdentity;
struct {HANDLE auraControl=-1;ULONG auraFeatureLen=64;}g_rog;
static std::mutex g_rogAuraMx;static constexpr BYTE kRogAuraReportId=0x5d;
static std::vector<std::vector<BYTE>> rogPackets,rogFeatures,msiPackets;static std::vector<json> logs;
static bool powerAllowed=true,writeOk=true,shortWrite=false,openOk=true;static int helperCalls=0,msiOpens=0,msiCloses=0,retiredCloses=0;
static bool WriteFile(HANDLE,const void*data,DWORD size,DWORD*written,void*){const auto*p=static_cast<const BYTE*>(data);rogPackets.emplace_back(p,p+size);*written=writeOk?(shortWrite?size/2:size):0;return writeOk;}
static bool HidD_SetFeature(HANDLE,void*data,ULONG size){const auto*p=static_cast<BYTE*>(data);rogFeatures.emplace_back(p,p+size);return writeOk&&!shortWrite;}
struct MsiClawHidDiag {bool deviceFound=false;int devicesScanned=0,vendorMatches=0,openFailures=0;DWORD lastError=0;ULONG firmware=0x230;};
struct MsiClawWriteResult {bool ok=false;DWORD written=0,lastError=0;};
static HANDLE msiClawOpenCommandHid(MsiClawHidDiag& d){++msiOpens;d.deviceFound=true;return openOk?55:INVALID_HANDLE_VALUE;}
static MsiClawWriteResult msiClawWriteFrame(HANDLE,const BYTE*data,DWORD size){msiPackets.emplace_back(data,data+size);return {writeOk&&!shortWrite,writeOk?(shortWrite?size/2:size):0,writeOk?0u:5u};}
static void CloseHandle(HANDLE handle){if(handle==88)++retiredCloses;if(handle==55){++msiCloses;return;}if(handle!=77&&handle!=88)throw std::runtime_error("unexpected close");}
static void appendNativeLifecycleLog(const char*name,const json& row){logs.push_back({{"event",name},{"body",row}});}
static const char* ymccFamilyName(YmccFamily){return "memory-identity";}
static bool hardwareWriteAllowed(){return powerAllowed;}
static json ymccLightSetterApply(bool,const std::string&,BYTE,BYTE,BYTE,BYTE,BYTE,BYTE,int,int,const std::string&){++helperCalls;return {{"ok",true},{"backend","hc-light-setter"},{"effectApplied",true},{"brightnessApplied",true}};}
`+include('native/oem_lighting_operations.h')+include('native/oxp3_lighting_owner.h')+include('native/onex_x2_protocol.h')+ownerFakes+[
 'static bool legacyLightSetterDebtClass(', 'static bool legacyLightSetterReapReturned()', 'static bool legacyLightSetterHasDebt()',
 'static bool onexX2OwnerWriteAllowed()','static bool onexX2OwnerBindAllowed('
].map(signature=>{const code=block(signature);return signature.startsWith('static bool legacyLightSetter')?code.replaceAll('nullptr','0'):code;}).join('\n')+'\n'+
[
 'static std::wstring oxp3OwnerEventNameFor(',
 'static bool oxp3OwnerEnsureHandoff()',
 'static void oxp3OwnerPublishHandoff(',
 'static void oxp3OwnerPublishLeaseState(',
 'static void oxp3OwnerHeartbeat(',
 'static void oxp3OwnerRetainRetiredProcess(',
 'static bool oxp3OwnerRetiredProcessReturned()',
 'static bool oxp3OwnerCompleteReturnedChild()',
 'static void oxp3OwnerRetainDebtRecord(',
 'static void oxp3OwnerAbandonReservation()',
 'static bool oxp3OwnerSettleDebt()',
 'static bool oxp3OwnerAdmit(',
 'static bool oxp3OwnerSequenceCurrent()',
 'static bool oxp3OwnerRelease(',
 'static void oxp3OwnerMarkDebt()',
 'static bool oxp3OwnerFinishApply(',
 'static bool oxp3OwnerParentTransition(',
 'static bool oxp3OwnerReceiptAccepted(',
 'static bool oxp3OwnerStopRetire(',
 'static void oxp3OwnerSettleFromTick('
].map(block).join('\n')+'\n'+enums+[
 'static bool machineIsRogAllyFamily() {',
 'static const std::vector<std::string>& ymccDeviceClassLightingSet()',
 'static bool ymccDeviceClassSupportsLighting(',
 'static std::vector<BYTE> rogAuraMessage(',
 'static bool rogAuraWriteLocked(',
 'static RogAuraSpeed rogAuraSpeedFromPercent(',
 'static bool rogAuraApplyColor(',
 'static bool rogAuraApplyAmbilight(',
 'static bool rogAuraSetBrightness(',
 'static bool parseRgbHex('
].map(block).join('\n')+'\n'+main.slice(msiStart,msiEnd)+'\n'+block('static json msiClawApplyLed(')+String.raw`
static json execute(const json& a){auto ipc_on=[](const char*,auto handler){return handler;};auto callback=
`+handler+String.raw`
;return callback(a);}
static void reset(){rogPackets.clear();rogFeatures.clear();msiPackets.clear();logs.clear();helperCalls=0;msiOpens=0;msiCloses=0;powerAllowed=writeOk=openOk=true;shortWrite=false;}
int main(int argc,char**argv){std::ifstream f(argv[1]);json c;f>>c;int checks=0,models=0;std::vector<std::string>failures;
auto check=[&](bool ok,const std::string&id){++checks;if(!ok)failures.push_back(id);};
auto quiet=[&]{return rogPackets.empty()&&rogFeatures.empty()&&msiPackets.empty()&&helperCalls==0&&msiOpens==0;};
try{
{ymcc::oxp3_owner::Lease lease;check(ymcc::oxp3_owner::writeAllowed(true,lease)&&ymcc::oxp3_owner::bindAllowed(true,lease),"owner:initial-allows");lease.active.store(true);check(!ymcc::oxp3_owner::writeAllowed(true,lease)&&!ymcc::oxp3_owner::bindAllowed(true,lease),"owner:active-blocks-init-and-bind");lease.generation.store(7);lease.topology.store(9);lease.interfacePath="path";check(ymcc::oxp3_owner::sequenceCurrent(lease,7,9,"path"),"owner:sequence-current");check(!ymcc::oxp3_owner::sequenceCurrent(lease,8,9,"path"),"owner:generation-change-denied");lease.revoked.store(true);check(!ymcc::oxp3_owner::sequenceCurrent(lease,7,9,"path"),"owner:revoked-denied");}
g_machineIdentity.family=YmccFamily::OneXPlayer;g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayer3";
auto resetOwner=[&]{g_oxp3OwnerState=ymcc::oxp3_owner::OwnerState::Idle;g_oxp3OwnerLease.active=false;g_oxp3OwnerLease.revoked=true;g_oxp3OwnerLease.generation=0;g_oxp3OwnerLease.topology=0;g_oxp3OwnerLease.modelTag=0;g_oxp3OwnerLease.generationToken.clear();g_oxp3OwnerLease.interfacePath.clear();g_oxp3OwnerInterfacePath.clear();g_oxp3OwnerEventName.clear();g_oxp3OwnerRevokedEvent=0;g_oxp3OwnerPendingDebt=false;g_oxp3OwnerStopTimedOut=false;g_oxp3OwnerApplyOutstanding=false;g_oxp3OwnerHelperAlive=false;g_oxp3OwnerRetiredProcess=0;g_oxp3OwnerRetiredProcessToken.clear();g_oxp3OwnerChildReturnObserved=false;g_oxp3OwnerLease.admission=0;g_oxp3OwnerAdmissionSequence=0;fakeRetiredSignaled=false;g_onexX2Transport.state={ymcc::onex_x2::Phase::Live,7,true};g_onexX2Transport.state.requested=g_onexX2Transport.state.bound=ymcc::onex_x2::modelForClass(g_machineIdentity.deviceClass);g_onexX2Transport.afterTick={};g_onexX2Transport.ticks=0;retiredCloses=0;g_onexX2Transport.has=true;g_onexX2Transport.path=L"path";g_onexX2Transport.clearPending=true;g_onexX2Transport.pendingTicks=0;fakeHandoffRecord={};eventCreates=eventResets=eventSets=0;lastEventName.clear();fakeNow=0;};
std::string ownerReason;
resetOwner();check(oxp3OwnerAdmit(100,ownerReason)&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerLease.generationToken=="7:9:1"&&g_oxp3OwnerLease.interfacePath=="path"&&!onexX2OwnerWriteAllowed()&&!onexX2OwnerBindAllowed(L"path"),"owner:admit-quiesce-generation-and-write-gate");
check(fakeHandoffRecord.magic==ymcc::oxp3_owner::kHandoffMagic&&fakeHandoffRecord.version==ymcc::oxp3_owner::kHandoffVersion&&fakeHandoffRecord.state==2&&std::string(fakeHandoffRecord.token)=="7:9:1"&&fakeHandoffRecord.generation==7&&fakeHandoffRecord.topology==9&&fakeHandoffRecord.admission==1&&(fakeHandoffRecord.sequence%2)==0,"owner:authoritative-handoff-published");
check(oxp3OwnerSequenceCurrent(),"owner:sequence-current");g_onexX2Transport.state.generation=8;check(!oxp3OwnerSequenceCurrent(),"owner:fresh-generation-consumes-old-sequence");
g_onexX2Transport.state.generation=7;g_onexX2DeviceEpoch=10;check(!oxp3OwnerSequenceCurrent(),"owner:topology-change-consumes-old-sequence");g_onexX2DeviceEpoch=9;check(oxp3OwnerSequenceCurrent(),"owner:sequence-restored");
check(oxp3OwnerRelease("7:9:1",GetTickCount64()+50,false)&&!g_oxp3OwnerLease.active.load()&&onexX2OwnerWriteAllowed()&&fakeHandoffRecord.state==0,"owner:release-resumes-initialization-writes");
resetOwner();g_onexX2Transport.clearPending=false;g_onexX2Transport.pendingTicks=20;
std::atomic<bool> admitDone{false};bool admittedWait=false;std::string reasonWait;
std::thread admitThread([&]{admittedWait=oxp3OwnerAdmit(GetTickCount64()+1000,reasonWait);admitDone.store(true);});
bool lockObserved=false;while(!admitDone.load()){if(g_onexX2OwnerMx.try_lock()){g_onexX2OwnerMx.unlock();lockObserved=true;}Sleep(1);}admitThread.join();
check(lockObserved&&admittedWait&&g_oxp3OwnerLease.generationToken=="7:9:1","owner:pending-wait-never-holds-input-lock");
check(oxp3OwnerRelease("7:9:1",GetTickCount64()+50,false),"owner:wait-admission-release");
resetOwner();g_onexX2Transport.state.writePending=true;
std::atomic<int> ready{0};bool okLeft=false,okRight=false;std::string reasonLeft,reasonRight;
auto attempt=[&](bool& ok,std::string& reason){ready.fetch_add(1);while(ready.load()<2){}ok=oxp3OwnerAdmit(GetTickCount64()+1000,reason);};
std::thread left([&]{attempt(okLeft,reasonLeft);}),right([&]{attempt(okRight,reasonRight);});left.join();right.join();
check((okLeft?1:0)+(okRight?1:0)==1,"owner:interleaved-admission-single-winner");
check((okLeft?reasonRight:reasonLeft)=="owner-lease-busy","owner:loser-keeps-winner-lease");
check(g_oxp3OwnerLease.generationToken=="7:9:1"&&eventCreates==1&&eventResets==1,"owner:winner-event-token-path-preserved");
check(oxp3OwnerRelease("7:9:1",GetTickCount64()+50,false),"owner:interleaved-winner-release");
resetOwner();check(oxp3OwnerAdmit(100,ownerReason)&&g_oxp3OwnerLease.active.load(),"owner:admit-before-stop-timeout");
g_oxp3OwnerApplyOutstanding=true;g_oxp3OwnerHelperAlive=true;
check(!oxp3OwnerStopRetire(GetTickCount64()+20)&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring,"owner:stop-timeout-keeps-retiring-debt");
check(!oxp3OwnerAdmit(GetTickCount64()+100,ownerReason)&&ownerReason=="owner-retiring-debt"&&!onexX2OwnerWriteAllowed()&&!onexX2OwnerBindAllowed(L"path"),"owner:stop-timeout-denies-admission-and-rebind");
check(oxp3OwnerRelease("7:9:1",GetTickCount64()+100,true)&&!g_oxp3OwnerLease.active.load()&&!g_oxp3OwnerPendingDebt.load()&&onexX2OwnerWriteAllowed(),"owner:actual-return-clears-debt");
g_oxp3OwnerApplyOutstanding=false;g_oxp3OwnerHelperAlive=false;
check(oxp3OwnerAdmit(GetTickCount64()+100,ownerReason)&&g_oxp3OwnerLease.generationToken=="7:9:2"&&oxp3OwnerRelease("7:9:2",GetTickCount64()+100,false),"owner:admission-after-actual-return");
resetOwner();check(oxp3OwnerAdmit(100,ownerReason)&&oxp3OwnerRelease("7:9:1",GetTickCount64()+50,false),"owner:first-owner-return");
g_onexX2Transport.state.generation=8;check(oxp3OwnerAdmit(100,ownerReason)&&g_oxp3OwnerLease.generationToken=="8:9:2","owner:newer-owner-admitted");
check(!oxp3OwnerRelease("7:9:1",GetTickCount64()+50,true)&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerLease.generationToken=="8:9:2"&&!onexX2OwnerWriteAllowed(),"owner:late-old-release-cannot-free-newer-lease");
check(oxp3OwnerRelease("8:9:2",GetTickCount64()+50,false),"owner:newer-owner-return");
resetOwner();check(oxp3OwnerAdmit(100,ownerReason),"owner:apply-admit");
g_oxp3OwnerApplyOutstanding=true;g_oxp3OwnerHelperAlive=true;
oxp3OwnerRetainRetiredProcess(88,"7:9:1");fakeRetiredSignaled=false;
check(!oxp3OwnerFinishApply("7:9:1",true,false)&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerRetiredProcess==88,"owner:unreturned-apply-keeps-debt-and-process");
check(!oxp3OwnerAdmit(GetTickCount64()+100,ownerReason)&&ownerReason=="owner-retiring-debt","owner:unreturned-apply-denies-new-writer");
check(!oxp3OwnerSettleDebt()&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerHelperAlive.load(),"owner:n03-unsignaled-settle-denied");
oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});
check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerHelperAlive.load()&&g_oxp3OwnerRetiredProcess==88,"owner:n03-unsignaled-tick-keeps-debt");
check(!oxp3OwnerStopRetire(GetTickCount64()+20)&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerRetiredProcess==88,"owner:n03-unsignaled-stop-keeps-debt");
fakeRetiredSignaled=true;
check(oxp3OwnerRetiredProcessReturned()&&g_oxp3OwnerRetiredProcess==0&&g_oxp3OwnerChildReturnObserved.load(),"owner:n03-exact-child-signaled");
oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Idle&&!g_oxp3OwnerLease.active.load()&&!g_oxp3OwnerPendingDebt.load()&&!g_oxp3OwnerHelperAlive.load()&&!g_oxp3OwnerApplyOutstanding.load(),"owner:n03-single-permitted-retirement");
check(oxp3OwnerAdmit(GetTickCount64()+100,ownerReason)&&g_oxp3OwnerLease.generationToken=="7:9:2"&&oxp3OwnerRelease("7:9:2",GetTickCount64()+100,false),"owner:apply-admission-after-return");
resetOwner();check(oxp3OwnerAdmit(100,ownerReason)&&oxp3OwnerRelease("7:9:1",GetTickCount64()+50,false),"owner:n03-first-return");
g_onexX2Transport.state.generation=8;check(oxp3OwnerAdmit(100,ownerReason)&&g_oxp3OwnerLease.generationToken=="8:9:2","owner:n03-newer-owner");
oxp3OwnerRetainRetiredProcess(88,"7:9:1");fakeRetiredSignaled=true;
check(!oxp3OwnerRetiredProcessReturned()&&g_oxp3OwnerRetiredProcess==88,"owner:n03-unrelated-signaled-child-cannot-retire-new-owner");
fakeRetiredSignaled=false;g_oxp3OwnerRetiredProcess=0;g_oxp3OwnerRetiredProcessToken.clear();
check(oxp3OwnerRelease("8:9:2",GetTickCount64()+50,false),"owner:n03-newer-owner-return");
resetOwner();check(oxp3OwnerAdmit(100,ownerReason)&&g_oxp3OwnerLease.generationToken=="7:9:1"&&oxp3OwnerRelease("7:9:1",GetTickCount64()+50,false),"owner:n04-unique-first-return");
check(oxp3OwnerAdmit(100,ownerReason)&&g_oxp3OwnerLease.generationToken=="7:9:2"&&eventCreates==2,"owner:n04-unique-second-admission-new-event");
check(!oxp3OwnerRelease("7:9:1",GetTickCount64()+50,true)&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerLease.generationToken=="7:9:2","owner:n04-old-token-cannot-free-second-admission");
check(oxp3OwnerRelease("7:9:2",GetTickCount64()+50,false),"owner:n04-unique-second-return");
resetOwner();check(oxp3OwnerAdmit(100,ownerReason)&&g_oxp3OwnerLease.generationToken=="7:9:1","owner:n04-parent-admit");
g_oxp3OwnerApplyOutstanding=true;g_oxp3OwnerHelperAlive=true;
check(!oxp3OwnerParentTransition(88,true,false,"7:9:1",true)&&g_oxp3OwnerRetiredProcess==88&&g_oxp3OwnerHelperAlive.load()&&g_oxp3OwnerApplyOutstanding.load()&&g_oxp3OwnerPendingDebt.load(),"owner:n04-production-parent-transition-retains-exact-child");
check(!oxp3OwnerAdmit(GetTickCount64()+100,ownerReason)&&ownerReason=="owner-retiring-debt"&&!onexX2OwnerBindAllowed(L"path"),"owner:n04-parent-transition-denies-admission-and-rebind");
oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});
check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerHelperAlive.load()&&g_oxp3OwnerRetiredProcess==88,"owner:n04-parent-transition-tick-keeps-debt");
fakeRetiredSignaled=true;check(oxp3OwnerRetiredProcessReturned()&&g_oxp3OwnerRetiredProcess==0,"owner:n04-parent-exact-child-return");
oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});
check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Idle&&onexX2OwnerBindAllowed(L"path")&&g_oxp3OwnerLease.active.load()==false,"owner:n04-parent-settlement-restores-rebind");
check(oxp3OwnerAdmit(GetTickCount64()+100,ownerReason)&&g_oxp3OwnerLease.generationToken=="7:9:2"&&oxp3OwnerRelease("7:9:2",GetTickCount64()+100,false),"owner:n04-parent-readmit-after-return");
check(oxp3OwnerReceiptAccepted(false,false,false,false,0,true,true,true,true,true,true,true),"owner:receipt-gate-accepts-exact-return");
check(!oxp3OwnerReceiptAccepted(false,false,false,false,0,true,true,true,true,true,false,true),"owner:receipt-gate-rejects-unreturned-child");
check(!oxp3OwnerReceiptAccepted(false,false,false,false,0,true,true,true,true,true,true,false),"owner:receipt-gate-rejects-unreturned-lease");
check(!oxp3OwnerReceiptAccepted(false,false,false,false,0,true,true,true,true,false,true,true),"owner:receipt-gate-rejects-stale-generation");
check(!oxp3OwnerReceiptAccepted(false,false,false,false,2,true,true,true,true,true,true,true),"owner:receipt-gate-rejects-exit-2");
resetOwner();g_onexX2Transport.clearPending=false;g_onexX2Transport.pendingTicks=0;
check(!oxp3OwnerAdmit(100,ownerReason)&&ownerReason=="owner-pending-write-debt"&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerLease.generationToken=="7:9:1","owner:pending-write-debt-retains-lease");
g_onexX2Transport.state.writePending=false;check(oxp3OwnerSettleDebt()&&!g_oxp3OwnerLease.active.load()&&!g_oxp3OwnerPendingDebt.load()&&onexX2OwnerWriteAllowed(),"owner:debt-settle-reopens-gate");
check(oxp3OwnerAdmit(GetTickCount64()+100,ownerReason)&&g_oxp3OwnerLease.generationToken=="7:9:2"&&oxp3OwnerRelease("7:9:2",GetTickCount64()+100,false),"owner:admission-after-debt-settle");
std::vector<json> lightingRows=c["domains"]["lighting"]["rows"].get<std::vector<json>>();for(const auto& row:c["domains"]["lighting"]["extensionRows"])lightingRows.push_back(row);
for(const auto& row:lightingRows){++models;reset();const std::string cls=row["deviceClass"],id=row["deviceId"];g_machineIdentity.deviceClass=cls;
const bool rog=ymcc::lighting::usesRogAura(cls),msi=ymcc::lighting::usesMsiNativeHid(cls);g_machineIdentity.family=rog?YmccFamily::AsusRogAlly:msi?YmccFamily::MsiClaw:YmccFamily::Unknown;g_rog.auraControl=rog?17:-1;
auto result=execute({{"mode","solid"},{"color","#123456"},{"brightness",65}});
check(result.at("ok")==true&&result.at("capabilityId")==id&&result.at("physicalEffectVerified")==false,id+":unified-result");
if(rog){check(rogPackets.size()==3&&rogFeatures.size()==1&&msiPackets.empty()&&helperCalls==0,id+":actual-ROG-writer");check(rogPackets[0].size()==64&&rogPackets[0][0]==0x5d&&rogPackets[0][4]==0x12&&rogPackets[1][1]==0xb4&&rogPackets[2][1]==0xb5,id+":real-ROG-packets");check(rogFeatures[0].size()==64&&rogFeatures[0][1]==0xba,id+":real-brightness-feature");}
else if(msi){check(msiPackets.size()==4&&msiOpens==1&&msiCloses==1&&rogPackets.empty()&&helperCalls==0,id+":actual-MSI-writer");check(msiPackets[0].size()==64&&msiPackets[0][0]==15&&msiPackets[0][6]==2&&msiPackets[0][7]==0x4a&&msiPackets[0][13]==65&&msiPackets[0][14]==0x12,id+":real-MSI-packets");}
else check(helperCalls==1&&rogPackets.empty()&&msiPackets.empty(),id+":actual-helper-call");
reset();powerAllowed=false;check(execute({{"mode","solid"}}).at("ok")==false&&quiet(),id+":power-refusal-zero-io");
reset();check(execute({{"mode","unreviewed-effect"}}).at("ok")==false&&quiet(),id+":mode-refusal-zero-io");
reset();if(rog||msi){g_machineIdentity.family=YmccFamily::Unknown;check(execute({{"mode","solid"}}).at("reason")=="lighting-identity-mismatch"&&quiet(),id+":family-mismatch-zero-io");}
else {g_rog.auraControl=17;check(execute({{"mode","solid"}}).at("ok")==true&&rogPackets.empty()&&helperCalls==1,id+":cached-Aura-handle-does-not-steal-route");}
}
for(const std::string cls:{"HandheldCompanion.Devices.ROGAlly","HandheldCompanion.Devices.ClawCG3EM"}){g_machineIdentity.deviceClass=cls;g_machineIdentity.family=ymcc::lighting::usesRogAura(cls)?YmccFamily::AsusRogAlly:YmccFamily::MsiClaw;g_rog.auraControl=17;
reset();writeOk=false;auto failed=execute({{"mode","solid"}});check(failed.at("ok")==false&&failed.at("physicalEffectVerified")==false,cls+":write-failure-preserved");
reset();shortWrite=true;check(execute({{"mode","solid"}}).at("ok")==false,cls+":short-write-not-success");
reset();auto off=execute({{"enabled",false}});check(off.at("ok")==true,cls+":off-consumed");if(ymcc::lighting::usesRogAura(cls))check(rogFeatures[0][4]==0&&rogPackets[0][4]==0,cls+":off-black-and-zero-brightness");else check(msiPackets[0][13]==0&&msiPackets[0][14]==0,cls+":off-black-and-zero-brightness");
reset();auto ambient=execute({{"mode","ambilight"},{"color","#123456"},{"color2","#abcdef"}});check(ambient.at("ok")==true,cls+":ambilight-consumed");if(ymcc::lighting::usesRogAura(cls))check(rogPackets.size()==4&&rogPackets[0][2]==1&&rogPackets[3][2]==4,cls+":actual-four-zone-packets");else check(msiPackets[0][14]==0xab&&msiPackets[0][26]==0x12,cls+":actual-dual-color-zones");
}
reset();g_machineIdentity.deviceClass="Unreviewed.Vendor.ROGAlly";g_machineIdentity.family=YmccFamily::AsusRogAlly;g_rog.auraControl=17;check(execute({{"mode","solid"}}).at("ok")==false&&quiet(),"unregistered-model-with-cached-Aura-zero-io");
}catch(const std::exception&e){failures.push_back(e.what());}
json result={{"status",failures.empty()?"PASS":"FAIL"},{"checks",checks},{"models",models},{"failures",failures},{"actualRogAndMsiPacketConsumers",true},{"hardwareIo",false}};std::cout<<result.dump();return failures.empty()?0:1;}
`;
// Exact directed extension of the original native consumer. The default path
// below remains intact, including its full process/pipe/managed-host controls.
if(selectedCase==='x2mini-owner'){
 try {
 const begin=cpp.indexOf('{ymcc::oxp3_owner::Lease lease;'),finish=cpp.indexOf('std::vector<json> lightingRows=');
 if(begin<0||finish<=begin)throw Error('Original owner assertion boundaries changed');
 const legacyOwner=cpp.slice(begin,finish).replace('g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayer3";','g_machineIdentity.deviceClass=ownerModel;');
 const extraOwner=String.raw`
 auto modelReset=[&]{g_machineIdentity.family=YmccFamily::OneXPlayer;g_machineIdentity.deviceClass=ownerModel;resetOwner();};
 modelReset();
 check(sizeof(ymcc::oxp3_owner::HandoffRecord)==1136&&offsetof(ymcc::oxp3_owner::HandoffRecord,modelTag)==52&&ymcc::oxp3_owner::kHandoffVersion==2,"x2mini:actual-v2-record-layout-offset-and-size");
 check(oxp3OwnerAdmit(100,ownerReason)&&fakeHandoffRecord.modelTag==static_cast<std::uint32_t>(ymcc::onex_x2::modelForClass(ownerModel))&&g_oxp3OwnerLease.modelTag.load()==fakeHandoffRecord.modelTag,"x2mini:actual-active-record-and-lease-model-tag");
 const auto actualModelTag=g_oxp3OwnerLease.modelTag.load();g_oxp3OwnerLease.modelTag=0;
 check(!oxp3OwnerSequenceCurrent(),"x2mini:sequence-missing-model-tag-rejected");
 g_oxp3OwnerLease.modelTag=actualModelTag==ymcc::oxp3_owner::kOneXPlayer3ModelTag?ymcc::oxp3_owner::kX2MiniProModelTag:ymcc::oxp3_owner::kOneXPlayer3ModelTag;
 check(!oxp3OwnerSequenceCurrent(),"x2mini:sequence-wrong-allowed-model-tag-rejected");
 g_oxp3OwnerLease.modelTag=actualModelTag;
 check(oxp3OwnerRelease("7:9:1",GetTickCount64()+50,true),"x2mini:model-tag-test-actual-return");
 modelReset();g_onexX2Transport.state.initialized=false;g_onexX2Transport.state.writePending=false;
 check(!oxp3OwnerAdmit(100,ownerReason)&&ownerReason=="owner-initialization-not-complete"&&!g_oxp3OwnerLease.active.load()&&fakeHandoffRecord.state==0,"x2mini:uninitialized-no-debt-abandons-reservation");
 modelReset();g_onexX2Transport.state.initialized=false;g_onexX2Transport.clearPending=false;
 check(!oxp3OwnerAdmit(100,ownerReason)&&ownerReason=="owner-pending-write-debt"&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerPendingDebt.load(),"x2mini:uninitialized-pending-keeps-original-debt");
 modelReset();g_onexX2Transport.state.requested=ymcc::onex_x2::Model::X2;
 check(!oxp3OwnerAdmit(100,ownerReason)&&!g_oxp3OwnerLease.active.load()&&eventCreates==0,"x2mini:requested-model-mismatch-no-publish");
 modelReset();g_onexX2Transport.state.bound=ymcc::onex_x2::Model::X2;
 check(!oxp3OwnerAdmit(100,ownerReason)&&!g_oxp3OwnerLease.active.load()&&eventCreates==0,"x2mini:bound-model-mismatch-no-publish");
 modelReset();g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX2";
 check(!oxp3OwnerAdmit(100,ownerReason)&&ownerReason=="owner-model-unsupported"&&eventCreates==0,"x2mini:x2-not-admitted-by-shared-owner");
 modelReset();g_machineIdentity.family=YmccFamily::Unknown;
 check(!oxp3OwnerAdmit(100,ownerReason)&&eventCreates==0,"x2mini:wrong-family-no-admit");
 modelReset();g_onexX2Transport.afterTick=[](int count){if(count==2)g_onexX2Transport.state.initialized=false;};
 check(!oxp3OwnerAdmit(100,ownerReason)&&!g_oxp3OwnerLease.active.load()&&fakeHandoffRecord.state!=2,"x2mini:final-publish-rechecks-initialization");
 modelReset();g_onexX2Transport.afterTick=[](int count){if(count==2)g_onexX2Transport.state.bound=ymcc::onex_x2::Model::X2;};
 check(!oxp3OwnerAdmit(100,ownerReason)&&!g_oxp3OwnerLease.active.load()&&fakeHandoffRecord.state!=2,"x2mini:final-publish-rechecks-bound-model");
 modelReset();g_onexX2Transport.afterTick=[](int count){if(count==2)g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX2";};
 check(!oxp3OwnerAdmit(100,ownerReason)&&!g_oxp3OwnerLease.active.load()&&fakeHandoffRecord.state!=2,"x2mini:final-publish-rechecks-machine-identity");
 modelReset();check(oxp3OwnerAdmit(100,ownerReason),"x2mini:sequence-test-admit");
 g_onexX2Transport.state.initialized=false;check(!oxp3OwnerSequenceCurrent(),"x2mini:sequence-loses-initialization");g_onexX2Transport.state.initialized=true;
 g_onexX2Transport.state.requested=ymcc::onex_x2::Model::X2;check(!oxp3OwnerSequenceCurrent(),"x2mini:sequence-requested-model-revokes");g_onexX2Transport.state.requested=ymcc::onex_x2::modelForClass(ownerModel);
 g_onexX2Transport.state.bound=ymcc::onex_x2::Model::X2;check(!oxp3OwnerSequenceCurrent(),"x2mini:sequence-bound-model-revokes");g_onexX2Transport.state.bound=ymcc::onex_x2::modelForClass(ownerModel);
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX2";check(!oxp3OwnerSequenceCurrent(),"x2mini:sequence-machine-identity-revokes");g_machineIdentity.deviceClass=ownerModel;
 g_onexX2Transport.state.phase=ymcc::onex_x2::Phase::Detached;check(!oxp3OwnerSequenceCurrent(),"x2mini:sequence-disconnect-revokes");g_onexX2Transport.state.phase=ymcc::onex_x2::Phase::Live;
 check(oxp3OwnerRelease("7:9:1",GetTickCount64()+50,true),"x2mini:sequence-test-owned-return");
 modelReset();check(oxp3OwnerAdmit(100,ownerReason),"x2mini:exact-child-test-admit");g_oxp3OwnerApplyOutstanding=true;g_oxp3OwnerHelperAlive=true;
 check(!oxp3OwnerParentTransition(88,true,false,"7:9:1",true)&&retiredCloses==0&&!onexX2OwnerBindAllowed(L"path"),"x2mini:unreturned-child-blocks-rebind-no-close");
 fakeRetiredSignaled=true;check(oxp3OwnerRetiredProcessReturned()&&retiredCloses==1,"x2mini:exact-child-return-closes-once");
 oxp3OwnerSettleFromTick(g_onexX2Transport.snapshot());
 check(!g_oxp3OwnerLease.active.load()&&!g_oxp3OwnerPendingDebt.load()&&onexX2OwnerBindAllowed(L"path")&&retiredCloses==1,"x2mini:debt-settle-after-child-handle-return");
 modelReset();
`;
 const actualApply=extract(main,'static json ymccLightSetterApply(');
 const bodyStart=actualApply.indexOf('    const bool superJoyOwnerRoute =');
 const commandEnd=actualApply.indexOf('    const std::wstring cmdText = cmd.str();')+'    const std::wstring cmdText = cmd.str();'.length;
 if(bodyStart<0||commandEnd<bodyStart||actualApply.indexOf('std::wostringstream cmd;')<bodyStart)throw Error('Actual command/admission fragment boundaries changed');
 const admissionAndCommand=actualApply.slice(bodyStart,commandEnd);
 const applyOffset=main.indexOf('static json ymccLightSetterApply(');
 const fragmentStart=applyOffset+bodyStart;
 const fragmentIdentity={source:'native/main.cpp',sourceSha256:sourceHash,startLine:main.slice(0,fragmentStart).split('\n').length,endLine:main.slice(0,applyOffset+commandEnd).split('\n').length,sha256:fragmentHash(admissionAndCommand),first:'const bool superJoyOwnerRoute =',last:'const std::wstring cmdText = cmd.str();',executed:'actual admission gates + sole command construction',remaining:'CreateProcess/pipe/receipt/child host below cmd.str() NOT_EXECUTED in this selector; original default parent-full remains'};
 const inlineFragments=[['setLed IPC',handler],['ROG enums',enums],['MSI packet definitions',main.slice(msiStart,msiEnd)],['actual admission and command',admissionAndCommand]];
 const sourceFragments=inlineFragments.map(([name,text])=>{const offset=main.indexOf(text);if(!text||offset<0)throw Error('Missing actual directed fragment: '+name);return {name,source:'native/main.cpp',startLine:main.slice(0,offset).split('\n').length,endLine:main.slice(0,offset+text.length).split('\n').length,sha256:fragmentHash(text)};});
 const commandPorts=include('native/lighting_device_core_binding.h')+String.raw`
#include <optional>
#include <sstream>
static std::wstring POWER_CONTROL_DIR=L"C:\\offline",kLightSetterExe=L"C:\\offline\\YeManLightSetter.exe",kLightSetterHcRuntimeRoot=L"C:\\offline\\hc";
static std::vector<std::wstring> queriedFiles;static bool missingCore=false;
static constexpr DWORD INVALID_FILE_ATTRIBUTES=0xFFFFFFFF;
static DWORD GetFileAttributesW(const wchar_t* path){queriedFiles.emplace_back(path);return missingCore&&queriedFiles.back().find(L"YeManLightDeviceCore.dll")!=std::wstring::npos?INVALID_FILE_ATTRIBUTES:0;}
static std::wstring U2W(const std::string& text){return {text.begin(),text.end()};}
namespace ymcc::superjoy_owner {struct Admission{std::string token;std::wstring path;};static bool sourceModel(const std::string&){return false;}}
static bool superJoyLightingRouteAdmitted(){return false;}
static std::optional<ymcc::superjoy_owner::Admission> superJoyLightingAdmit(std::string&){return std::nullopt;}
static json actualAdmissionAndCommand(bool enabled,const std::string& mode,BYTE r1,BYTE g1,BYTE b1,BYTE r2,BYTE g2,BYTE b2,int speed,int brightness,const std::string& presetId={}){
`+admissionAndCommand+String.raw`
 return json{{"preparedCommand",W2U(cmdText)},{"ownerAdmitted",ownerAdmitted},{"action",action},{"independentCore",independentCore}};
}
`;
 const commandChecks=String.raw`
 if(ownerModel=="HandheldCompanion.Devices.OneXPlayerX2MiniPro"){
  modelReset();queriedFiles.clear();auto solid=actualAdmissionAndCommand(true,"solid",10,20,30,99,88,77,23,50);
  const auto command=solid.value("preparedCommand",std::string{});
  check(solid.value("independentCore",false)&&solid.value("ownerAdmitted",false)&&command.find(" --device-core-assembly ")!=std::string::npos&&command.find(" --device-core-sha256 "+std::string(ymcc::lighting::kIndependentDeviceCoreSha256))!=std::string::npos,"native-args:x2-actual-independent-core-and-pin");
  check(command.find(" --expected-device-class \"HandheldCompanion.Devices.OneXPlayerX2MiniPro\"")!=std::string::npos&&command.find(" --owner-interface-path \"path\"")!=std::string::npos&&command.find(" --owner-generation 7:9:1")!=std::string::npos,"native-args:x2-exact-model-owner-path-token");
  check(command.find(" --r 10 --g 20 --b 30 --r2 99 --g2 88 --b2 77 --speed 23 --brightness 50")!=std::string::npos&&command.find(" --action color --level solid")!=std::string::npos,"native-args:x2-dual-color-speed-brightness-forwarded");
  check(command.find("--hc-assembly")==std::string::npos&&command.find("--hc-runtime-root")==std::string::npos,"native-args:x2-no-frozen-hc-fallback");
  modelReset();auto breathing=actualAdmissionAndCommand(true,"breathing",10,20,30,99,88,77,255,67);
  check(breathing.value("preparedCommand",std::string{}).find("--action color --level breathing")!=std::string::npos&&breathing.value("ownerAdmitted",false),"native-args:x2-breathing-owned-core");
  modelReset();auto off=actualAdmissionAndCommand(false,"solid",10,20,30,99,88,77,23,50);const auto offCommand=off.value("preparedCommand",std::string{});
  check(off.value("action",std::string{})=="off"&&off.value("independentCore",false)&&offCommand.find(" --action off")!=std::string::npos&&offCommand.find("--status")==std::string::npos,"native-args:x2-off-preserved-no-status");
  modelReset();queriedFiles.clear();auto rainbow=actualAdmissionAndCommand(true,"rainbow",10,20,30,99,88,77,23,50);
  check(rainbow.value("reason",std::string{})=="x2mini-lighting-operation-unavailable"&&eventCreates==0&&!rainbow.contains("preparedCommand")&&queriedFiles.size()==1,"native-args:x2-rainbow-rejected-before-core-owner-command");
  modelReset();queriedFiles.clear();auto preset=actualAdmissionAndCommand(true,"preset",10,20,30,99,88,77,23,50,"not-admitted");
  check(preset.value("reason",std::string{})=="x2mini-lighting-operation-unavailable"&&eventCreates==0&&!preset.contains("preparedCommand")&&queriedFiles.size()==1,"native-args:x2-preset-rejected-without-legacy");
  modelReset();missingCore=true;auto absent=actualAdmissionAndCommand(true,"solid",10,20,30,99,88,77,23,50);missingCore=false;
  check(absent.value("reason",std::string{})=="light-device-core-missing"&&eventCreates==0,"native-args:x2-missing-core-no-owner-or-fallback");
  modelReset();reset();g_machineIdentity.family=YmccFamily::OneXPlayer;auto wrong=execute({{"mode","rainbow"}});
  check(!wrong.value("ok",true)&&helperCalls==0&&rogPackets.empty()&&msiPackets.empty(),"native-args:x2-actual-ipc-wrong-mode-zero-helper");
  modelReset();g_oxp3OwnerApplyOutstanding=false;
 }
`;
 const snapshotChecks=String.raw`
 modelReset();ownerGettersOutsideLock=ownerGettersInsideLock=0;
 ownerGetterLockProbe=[]{return OwnerTrackedMutex::ownerDepth>0;};
 const bool lockedAdmission=oxp3OwnerAdmit(100,ownerReason);ownerGetterLockProbe={};
 check(lockedAdmission&&ownerGettersOutsideLock==0&&ownerGettersInsideLock>=6,"x2mini:snapshot-getters-use-owner-lock");
 check(lockedAdmission&&g_oxp3OwnerLease.interfacePath=="path"&&g_oxp3OwnerInterfacePath==L"path","x2mini:snapshot-path-value-copy-published");
 modelReset();g_onexX2Transport.afterTick=[](int tick){if(tick==2)g_onexX2Transport.path=L"changed-path";};
 check(!oxp3OwnerAdmit(100,ownerReason)&&ownerReason=="owner-quiesce-not-live"&&!g_oxp3OwnerLease.active.load(),"x2mini:snapshot-final-path-change-rejected");
 modelReset();g_onexX2Transport.afterTick=[](int tick){if(tick==2){g_onexX2Transport.state.generation=8;g_onexX2Transport.path=L"pending-new-path";g_onexX2DeviceEpoch=10;g_onexX2Transport.clearPending=false;g_onexX2Transport.state.writePending=true;}};
 const bool pendingAdmitted=oxp3OwnerAdmit(100,ownerReason);
 check(!pendingAdmitted&&ownerReason=="owner-pending-write-debt"&&g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring&&g_oxp3OwnerLease.generation.load()==8&&g_oxp3OwnerLease.topology.load()==10&&g_oxp3OwnerLease.interfacePath=="pending-new-path"&&g_oxp3OwnerLease.generationToken=="8:10:1","x2mini:snapshot-final-pending-debt-uses-observed-identity");
 g_onexX2DeviceEpoch=9;modelReset();g_onexX2Transport.state.phase=ymcc::onex_x2::Phase::Detached;
 check(!oxp3OwnerAdmit(100,ownerReason)&&g_onexX2Transport.ticks==0&&ownerReason=="owner-quiesce-not-live","x2mini:snapshot-detached-admission-does-not-discover");
`;
 const labels=text=>[...text.matchAll(/"((?:owner:|x2mini:|native-args:)[^"]+)"/g)].map(m=>m[1]);
 const originals=labels(legacyOwner),extra=labels(extraOwner),commands=labels(commandChecks),snapshotLabels=labels(snapshotChecks);
 if(originals.length<40||extra.length<15||commands.length<8)throw Error('Missing original/new native assertion selection');
 const nativeSelected=['oxp3','x2mini'].flatMap((group,index)=>[...originals,...extra,...(index===1?commands:[]),...snapshotLabels].map((label,i)=>`${group}:${i}:${label}`));
 const selectorControls=[['unknown',['--case','not-a-real-case']],['empty',['--case','']],['duplicate',['--case','x2mini-owner','--case','x2mini-owner']]];
 const selected=[...nativeSelected,...selectorControls.map(([name])=>'selector-reject-'+name)];
 const prefix=cpp.slice(0,cpp.indexOf('int main(int argc,char**argv)'));
 let directedCpp=prefix+commandPorts+String.raw`
int main(){int checks=0;std::vector<std::string> failures,actual;std::string group;int index=0;
 auto check=[&](bool ok,const std::string& label){auto name=group+":"+std::to_string(index++)+":"+label;++checks;actual.push_back(name);if(!ok)failures.push_back(name);};
 try {for(const std::string ownerModel:{"HandheldCompanion.Devices.OneXPlayer3","HandheldCompanion.Devices.OneXPlayerX2MiniPro"}){
 group=ownerModel=="HandheldCompanion.Devices.OneXPlayer3"?"oxp3":"x2mini";index=0;
`+legacyOwner+extraOwner+commandChecks+snapshotChecks+String.raw`
 }}catch(const std::exception& error){failures.push_back(error.what());}
 json result={{"status",failures.empty()?"PASS":"FAIL"},{"selector","x2mini-owner"},{"checks",checks},{"actual",actual},{"failures",failures},{"hardwareIo",false},{"fullProcessHostExecuted",false}};std::cout<<result.dump();return failures.empty()?0:1;}
`;
 // Selected source instrumentation forwards every lock to real std::mutex.
 directedCpp=directedCpp.replaceAll('std::mutex','OwnerTrackedMutex').replace('using json=nlohmann::json;',"struct OwnerTrackedMutex {\n std::mutex underlying;\n static inline const void* ownerAddress=nullptr;\n static inline thread_local int ownerDepth=0;\n void lock(){underlying.lock();if(this==ownerAddress)++ownerDepth;}\n bool try_lock(){if(!underlying.try_lock())return false;if(this==ownerAddress)++ownerDepth;return true;}\n void unlock(){if(this==ownerAddress)--ownerDepth;underlying.unlock();}\n};\n"+'using json=nlohmann::json;').replace('int main(){','int main(){OwnerTrackedMutex::ownerAddress=&g_onexX2OwnerMx;');
 const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
 const source=path.join(out,'x2mini-owner.cpp'),recipe=path.join(out,'x2mini-owner-build.cmd'),exe=path.join(out,'x2mini-owner.exe');
 const sessionFile=path.join(repo,'native/onex_x2_session.h'),sessionText=fs.readFileSync(sessionFile,'utf8').replace(/\r\n/g,'\n');
 const snapshotStart=sessionText.indexOf('struct Snapshot {'),snapshotEnd=sessionText.indexOf('\n};',snapshotStart);
 if(snapshotStart<0||snapshotEnd<snapshotStart)throw Error('Actual Snapshot provenance boundary changed');
 const snapshotText=sessionText.slice(snapshotStart,snapshotEnd+3);
 const declared={selector:selectedCase,selected,defaultExecuted:false,fragment:fragmentIdentity,sourceFragments,functions:[...extractedFunctions].map(([signature,sha256])=>({signature,sha256})),ownerFunctions:[...extractedFunctions].filter(([s])=>s.includes('Owner')).map(([signature,sha256])=>({signature,sha256})),lockInstrumentation:'Selected mutex wrapper forwards actual std::mutex and records only owner-held getter sampling; production function control flow unchanged.',actualSnapshot:{file:'native/onex_x2_session.h',sha256:sha(sessionFile),fragmentSha256:fragmentHash(snapshotText)},includes:[...includedSources].map(([file,sha256])=>({file,sha256})),estimateSeconds:[35,100],compilerTimeoutMs:120000,compilerIdentity:'original calibrationToolchainPreamble/vcvars64; sourceDependencies retained',originalDefaultFullHost:'NOT_RUN',hardware:'NOT_RUN'};
 fs.writeFileSync(path.join(out,'selection.json'),JSON.stringify(declared,null,2)+'\n');fs.writeFileSync(source,directedCpp);
 fs.writeFileSync(recipe,preamble+`cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "${out}/x2mini-owner.dependencies.json" "${source}" /I"${repo}/deps/json" /Fo"${out}/x2mini-owner.obj" /Fe"${exe}"\r\nexit /b %errorlevel%\r\n`);
 const started=entryStarted,steps=[];
 function directedRun(command,argv,log,expectedExit=0){
  const remaining=240000-(Date.now()-started);if(remaining<=0)throw Error('Original lighting 240s scope exhausted');
  const timeout=Math.min(remaining,120000),at=Date.now();
  const r=spawnSync(command,argv,{cwd:out,encoding:'utf8',windowsHide:true,timeout,maxBuffer:2*1024*1024});
  const step={argv:[command,...argv],elapsedMs:Date.now()-at,timeoutMs:timeout,exitCode:r.status,signal:r.signal,error:r.error?{code:r.error.code,message:r.error.message}:null};steps.push(step);
  fs.writeFileSync(path.join(out,log),(r.stdout??'')+(r.stderr??''));fs.writeFileSync(path.join(out,'steps.json'),JSON.stringify(steps,null,2)+'\n');
  if(r.status!==expectedExit||r.error)throw Error('Directed actual native consumer failed: '+log);return r.stdout;
 }
 directedRun('cmd.exe',['/d','/c',recipe],'x2mini-owner-compile.log');captureCompilerInputs();
 let result;
 if(prepareOnly)result={status:'NOT_RUN_PREPARE_ONLY',selector:selectedCase,selected,actual:[],checks:0,failures:[]};
 else {
  result=JSON.parse(directedRun(exe,[],'x2mini-owner-execute.log'));result.selected=selected;
  fs.writeFileSync(path.join(out,'result.raw.json'),JSON.stringify(result,null,2)+'\n');
  if(result.status!=='PASS'||JSON.stringify(result.actual)!==JSON.stringify(nativeSelected))throw Error('Actual native owner scenarios differ from selected exact set');
  result.selectorControls=[];
  for(const [name,caseArgs] of selectorControls){
   const rejected=path.join(out,'must-not-exist-'+name);
   directedRun(process.execPath,[fileURLToPath(import.meta.url),'--out',rejected,...caseArgs],'selector-'+name+'.log',1);
   if(fs.existsSync(rejected))throw Error('Invalid native selector entered preparation/default execution: '+name);
   const row={name:'selector-reject-'+name,expectedExit:1,preparedOrExecutedScenarios:0};result.selectorControls.push(row);result.actual.push(row.name);result.checks++;
  }
  if(JSON.stringify(result.actual)!==JSON.stringify(selected))throw Error('Final native actual set differs from declared selection');
 }
 const current=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
 const drift=[...extractedFunctions].filter(([signature,expected])=>fragmentHash(extract(current,signature))!==expected).map(([signature])=>signature);
 for(const [name,text] of inlineFragments)if(!current.includes(text))drift.push(name);
 if(sha(sessionFile)!==declared.actualSnapshot.sha256)drift.push('actual Snapshot provenance');
 for(const [file,expected] of includedSources)if(sha(path.join(repo,file))!==expected)drift.push(file);
 for(const row of compilerInputs.values())if(sha(row.path)!==row.sha256)drift.push(row.path);
 if(drift.length)throw Error('Directed native source drift: '+drift.join(','));
 result={...result,sourceHash,sourceScope:declared,sourceDrift:drift,steps,elapsedMs:Date.now()-started,defaultExecuted:false,fullProcessHostExecuted:false};
 fs.writeFileSync(path.join(out,'native-compiler-inputs.json'),JSON.stringify({files:[...compilerInputs.values()],reports:[...dependencyReports]},null,2)+'\n');
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));process.exit(0);
 } catch(error) {
  const failure={status:'FAIL',selector:selectedCase,defaultExecuted:false,error:String(error?.stack??error),hardwareIo:false,fullProcessHostExecuted:false};
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(failure,null,2)+'\n');throw error;
 }
}

fs.writeFileSync(path.join(out,'actual.cpp'),cpp);const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
fs.writeFileSync(path.join(out,'build.cmd'),preamble+`cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "${out}/actual.dependencies.json" "${out}/actual.cpp" /I"${repo}/deps/json" /Fo"${out}/actual.obj" /Fe"${out}/actual.exe"\r\nexit /b %errorlevel%\r\n`);
function run(exe,args,log){if(prepareOnly&&['actual.exe','parent-wait.exe','parent-full.exe'].includes(path.basename(exe))){const skipped=JSON.stringify({status:'NOT_RUN_PREPARE_ONLY',checks:0,failures:[]});fs.writeFileSync(path.join(out,log),skipped);return skipped;}const r=spawnSync(exe,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:2*1024*1024});fs.writeFileSync(path.join(out,log),(r.stdout??'')+(r.stderr??''));if(r.status!==0)throw Error('Native lighting consumption failed: '+log);if(path.basename(exe).toLowerCase()==='cmd.exe')captureCompilerInputs();return r.stdout;}
run('cmd.exe',['/d','/c',path.join(out,'build.cmd')],'compile.log');const result=JSON.parse(run(path.join(out,'actual.exe'),[path.join(repo,'capabilities/hc-device-catalog.json')],'execute.log'));result.sourceHash=sourceHash;
const parentWaitFn=block('static bool oxp3OwnerWaitChild(');
const parentWaitCpp=String.raw`#include <windows.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
`+parentWaitFn+String.raw`
int main(){
  int checks=0;std::vector<std::string> failures;
  auto check=[&](bool ok,const char*id){++checks;if(!ok)failures.push_back(id);};
  STARTUPINFOW si{};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
  auto spawn=[&](const wchar_t* cmd,PROCESS_INFORMATION& pi){std::wstring line=cmd;return CreateProcessW(nullptr,const_cast<wchar_t*>(line.c_str()),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;};
  PROCESS_INFORMATION slow{};
  wchar_t winDir[MAX_PATH]{};GetWindowsDirectoryW(winDir,MAX_PATH);
  const std::wstring slowCmd=std::wstring(L"cmd.exe /c ")+winDir+L"\\System32\\ping.exe -n 20 127.0.0.1 >nul";
  if(!spawn(slowCmd.c_str(),slow)){std::cout<<"{\"status\":\"FAIL\",\"checks\":0,\"failures\":[\"spawn\"]}";return 1;}
  bool timedOut=false;DWORD exitCode=0;
  bool returned=oxp3OwnerWaitChild(slow.hProcess,200,5000,timedOut,exitCode);
  const bool slowTimedOut=timedOut,slowReturned=returned;const DWORD slowExit=exitCode;
  check(timedOut&&returned,"parent-wait-timeout-terminate-return");
  CloseHandle(slow.hThread);CloseHandle(slow.hProcess);
  PROCESS_INFORMATION quick{};
  if(!spawn(L"cmd.exe /c exit 0",quick)){std::cout<<"{\"status\":\"FAIL\",\"checks\":1,\"failures\":[\"spawn-quick\"]}";return 1;}
  returned=oxp3OwnerWaitChild(quick.hProcess,5000,5000,timedOut,exitCode);
  check(!timedOut&&returned&&exitCode==0,"parent-wait-normal-return");
  CloseHandle(quick.hThread);CloseHandle(quick.hProcess);
  std::cout<<"{\"status\":\""<<(failures.empty()?"PASS":"FAIL")<<"\",\"checks\":"<<checks<<",\"slowTimedOut\":"<<(slowTimedOut?"true":"false")<<",\"slowReturned\":"<<(slowReturned?"true":"false")<<",\"slowExit\":"<<slowExit<<",\"failures\":[";
  for(size_t i=0;i<failures.size();++i){if(i)std::cout<<",";std::cout<<"\""<<failures[i]<<"\"";}
  std::cout<<"]}";
  return failures.empty()?0:1;
}
`;
fs.writeFileSync(path.join(out,'parent-wait.cpp'),parentWaitCpp);
fs.writeFileSync(path.join(out,'parent-wait.cmd'),preamble+`cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "${out}/parent-wait.dependencies.json" "${out}/parent-wait.cpp" /Fo"${out}/parent-wait.obj" /Fe"${out}/parent-wait.exe"\r\nexit /b %errorlevel%\r\n`);
run('cmd.exe',['/d','/c',path.join(out,'parent-wait.cmd')],'parent-wait-compile.log');
const parentWait=JSON.parse(run(path.join(out,'parent-wait.exe'),[],'parent-wait.log'));
result.parentWait=parentWait;result.checks+=parentWait.checks;result.failures.push(...parentWait.failures.map(f=>'parent-wait:'+f));result.status=result.failures.length?'FAIL':'PASS';
const catalogSha=fs.readFileSync(path.join(repo,'native/oem_lighting_operations.h'),'utf8').match(/kCatalogSha256\[\] = "([a-f0-9]{64})"/)[1];
const inertHolderCpp=String.raw`#include <windows.h>
int main(){ Sleep(8000); return 0; }
`;
const inertHelperCpp=String.raw`#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
int main(){
  char mode[64]{}; GetEnvironmentVariableA("HC011_CHILD_MODE",mode,64);
  std::string m=mode;
  if(m=="partial-hold"){
    std::fputs("{",stdout); std::fflush(stdout);
    wchar_t holder[4096]{}; GetEnvironmentVariableW(L"HC011_HOLDER",holder,4096);
    std::wstring exe(holder);
    std::wstring cmd=L"\""+exe+L"\"";
    STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE); si.hStdError=si.hStdOutput; si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    CreateProcessW(nullptr,const_cast<wchar_t*>(cmd.c_str()),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
    if(pi.hProcess) CloseHandle(pi.hProcess); if(pi.hThread) CloseHandle(pi.hThread);
    return 0;
  }
  if(m=="sleep"){ Sleep(40000); return 0; }
  if(m=="sleep-unreturned"){ Sleep(45000); return 0; }
  if(m=="nojson"){ std::fputs("not json\n",stdout); return 0; }
  if(m=="truncate"){ for(int i=0;i<65536;i++) std::fputc('x',stdout); std::fflush(stdout); return 0; }
  const bool declared=m!="not-ok";
  const char* id=m=="wrong-identity"?"hc.device.ROGAlly":"ymcc.device.OneXPlayer3";
  const char* cat=m=="wrong-catalog"?"0000000000000000000000000000000000000000000000000000000000000000":"${catalogSha}";
  const char* op=m=="wrong-operation"?"lighting.color.breathing":"lighting.color.solid";
  std::printf("{\"ok\":%s,\"result\":{\"deviceClass\":\"HandheldCompanion.Devices.OneXPlayer3\",\"capabilityId\":\"%s\",\"capabilityOperation\":\"%s\",\"capabilityCatalogSha256\":\"%s\",\"action\":\"color\",\"note\":\"ok\"}}\n",declared?"true":"false",id,op,cat);
  return m=="nonzero"?3:0;
}
`;
const ownerParentBlocks=[
 'static bool onexX2OwnerWriteAllowed()','static bool onexX2OwnerBindAllowed(',
 'static std::wstring oxp3OwnerEventNameFor(','static bool oxp3OwnerEnsureHandoff()',
 'static void oxp3OwnerPublishHandoff(','static void oxp3OwnerPublishLeaseState(',
 'static void oxp3OwnerHeartbeat(','static void oxp3OwnerRetainRetiredProcess(',
 'static bool oxp3OwnerRetiredProcessReturned()','static bool oxp3OwnerCompleteReturnedChild()',
 'static void oxp3OwnerRetainDebtRecord(','static void oxp3OwnerAbandonReservation()',
 'static bool oxp3OwnerSettleDebt()','static bool oxp3OwnerAdmit(',
 'static bool oxp3OwnerSequenceCurrent()','static bool oxp3OwnerRelease(',
 'static void oxp3OwnerMarkDebt()','static bool oxp3OwnerFinishApply(',
 'static bool oxp3OwnerWaitChild(','static bool oxp3OwnerParentTransition(',
 'static bool oxp3OwnerReceiptAccepted(','static bool oxp3OwnerStopRetire(',
 'static void oxp3OwnerSettleFromTick('
].map(block).join('\n');
const identityType=signature=>{const a=main.indexOf(signature),b=main.indexOf('\n};',a);if(a<0||b<0)throw Error('Missing actual identity type');return main.slice(a,b+3);};
const machineIdentityEnum=identityType('enum class YmccFamily : int {'),machineIdentityStruct=identityType('struct MachineIdentity {');
const machineIdentityTypes=machineIdentityEnum+'\n'+machineIdentityStruct;
const machineIdentityResolver=block('static void ymccResolveFamily(MachineIdentity& id) {');
const superJoyBegin=main.indexOf('// BEGIN SuperJoy lighting native lifecycle'),superJoyEnd=main.indexOf('// END SuperJoy lighting native lifecycle',superJoyBegin);
if(superJoyBegin<0||superJoyEnd<0)throw Error('Missing actual SuperJoy lifecycle block');
const superJoyLifecycle=main.slice(superJoyBegin,superJoyEnd);
const superJoyPorts=fs.readFileSync(path.join(repo,'tools/fixtures/hc_lighting_catalog/superjoy-native-ports.inc'),'utf8');
includedSources.set('tools/fixtures/hc_lighting_catalog/superjoy-native-ports.inc',sha(path.join(repo,'tools/fixtures/hc_lighting_catalog/superjoy-native-ports.inc')));
const superJoyCases=fs.readFileSync(path.join(repo,'tools/fixtures/hc_lighting_catalog/superjoy-native-cases.inc'),'utf8');
includedSources.set('tools/fixtures/hc_lighting_catalog/superjoy-native-cases.inc',sha(path.join(repo,'tools/fixtures/hc_lighting_catalog/superjoy-native-cases.inc')));
const parentApplyFn=block('static json ymccLightSetterApply(');
// Original full host keeps every assertion, but its actual Windows owner API
// terminals receive private fixture names through a reversible header copy.
let privateOwnerHeaderScope=null;
function isolatedFullOwnerHeader(){
 const relative='native/oxp3_lighting_owner.h',file=path.join(repo,relative),original=fs.readFileSync(file,'utf8');
 const namespace='YMCC-NativeFullReplay-'+randomUUID();
 const replacements=[['YeManCC-OXP3-VendorRevoked-',namespace+'-Revoked-'],['YeManCC-OXP3-OwnerLease',namespace+'-OwnerLease']];
 let generated=original;
 for(const [oldName,newName] of replacements){if(generated.split(oldName).length!==2)throw Error('Full owner private-name seam changed: '+oldName);generated=generated.replace(oldName,newName);}
 let restored=generated;for(const [oldName,newName] of replacements)restored=restored.replace(newName,oldName);
 if(restored!==original)throw Error('Full owner source adapter changed non-name text');
 const header=path.join(out,'private-full-owner.h');fs.writeFileSync(header,generated);includedSources.set(relative,sha(file));
 const before=original.replace(/\r\n/g,'\n').split('\n'),after=generated.replace(/\r\n/g,'\n').split('\n');
 const changed=before.map((line,i)=>line===after[i]?-1:i).filter(i=>i>=0),start=Math.max(0,changed[0]-3),end=Math.min(before.length,changed.at(-1)+4);
 const patch=['--- actual/oxp3_lighting_owner.h','+++ scratch/private-full-owner.h',`@@ -${start+1},${end-start} +${start+1},${end-start} @@`];
 for(let i=start;i<end;i++){if(before[i]===after[i])patch.push(' '+before[i]);else patch.push('-'+before[i],'+'+after[i]);}
 const patchFile=path.join(out,'private-full-owner-names.patch');fs.writeFileSync(patchFile,patch.join('\n')+'\n');
 privateOwnerHeaderScope={source:relative,sourceSha256:sha(file),generated:header,generatedSha256:sha(header),privateNamespace:namespace,replacements,singleHunk:true,reverseOriginalTextEqual:true,patchSha256:sha(patchFile),fixedProductObjectsOpenedOrWritten:false};
 fs.writeFileSync(path.join(out,'private-full-owner-source.json'),JSON.stringify(privateOwnerHeaderScope,null,2)+'\n');
 return '#include '+JSON.stringify(header.replaceAll('\\','/'))+'\n';
}
const parentFullCpp=String.raw`#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
`+isolatedFullOwnerHeader()+include('native/superjoy_lighting_owner.h')+include('native/oem_lighting_operations.h')+include('native/lighting_device_core_binding.h')+include('native/onex_x2_protocol.h')+String.raw`
#include <dbt.h>
#include <setupapi.h>
#include <hidsdi.h>

using json=nlohmann::json;
static std::wstring kLightSetterExe,kLightSetterHcRuntimeRoot,POWER_CONTROL_DIR;
static constexpr DWORD kLightSetterReadBudgetMs=5000;
static constexpr size_t kLightSetterMaxOutputBytes=64*1024;
`+machineIdentityTypes+String.raw`
static MachineIdentity g_machineIdentity;
`+machineIdentityResolver+String.raw`
static std::string W2U(const std::wstring& w){if(w.empty())return{};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);std::string s((size_t)n,'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr);return s;}
static std::wstring U2W(const std::string& s){if(s.empty())return{};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring w((size_t)n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
namespace ymcc{namespace onex_x2{
enum class Phase{Detached,Requested,Live};
struct Snapshot{Phase phase;std::uint64_t generation;bool writePending;Model requested=Model::OneXPlayer3,bound=Model::OneXPlayer3;bool initialized=true;};
class WindowsTransport{public:Snapshot state{Phase::Live,7,false};bool has=true;bool clearPending=true;int pendingTicks=0;std::wstring path=L"path";
 Snapshot snapshot()const{return state;}bool hasDevice()const{return has;}const std::wstring& devicePath()const{return path;}
 void tick(std::uint64_t,bool(*)(),bool(*)(const std::wstring&)){if(clearPending)state.writePending=false;else if(pendingTicks>0&&--pendingTicks==0)state.writePending=false;}};
}}
static ymcc::onex_x2::WindowsTransport g_onexX2Transport;static std::mutex g_onexX2OwnerMx;static std::atomic<std::uint64_t> g_onexX2DeviceEpoch{9};
static bool hardwareWriteAllowed(){return true;}
static bool onexX2BindingPathAllowed(const std::wstring& path){return !path.empty();}
static bool oxp3OwnerDenyWrite(){return false;}
static bool oxp3OwnerDenyBind(const std::wstring&){return false;}
static ymcc::oxp3_owner::Lease g_oxp3OwnerLease;
static ymcc::oxp3_owner::OwnerState g_oxp3OwnerState=ymcc::oxp3_owner::OwnerState::Idle;
static std::mutex g_oxp3OwnerStateMx;
static HANDLE g_oxp3OwnerRevokedEvent=0;static std::wstring g_oxp3OwnerEventName,g_oxp3OwnerInterfacePath;
static HANDLE g_oxp3OwnerHandoffMapping=0;static ymcc::oxp3_owner::HandoffRecord* g_oxp3OwnerHandoff=nullptr;
static std::atomic<bool> g_oxp3OwnerPendingDebt{false},g_oxp3OwnerStopTimedOut{false};
static std::atomic<bool> g_oxp3OwnerApplyOutstanding{false},g_oxp3OwnerHelperAlive{false};
static HANDLE g_oxp3OwnerRetiredProcess=0;static std::string g_oxp3OwnerRetiredProcessToken;
static std::atomic<bool> g_oxp3OwnerChildReturnObserved{false};
static std::atomic<std::uint64_t> g_oxp3OwnerAdmissionSequence{0};
static bool g_limitChildRights=false;
static bool g_sjMetadataFail=false,g_sjResumeFail=false;
namespace ymcc{namespace processEfficiency{
inline BOOL createProcess(LPCWSTR application,LPWSTR command,LPSECURITY_ATTRIBUTES processAttributes,LPSECURITY_ATTRIBUTES threadAttributes,BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION info)noexcept{
    constexpr DWORD priorityFlags=IDLE_PRIORITY_CLASS|BELOW_NORMAL_PRIORITY_CLASS|NORMAL_PRIORITY_CLASS|ABOVE_NORMAL_PRIORITY_CLASS|HIGH_PRIORITY_CLASS|REALTIME_PRIORITY_CLASS;
    if(!(flags&priorityFlags))flags|=NORMAL_PRIORITY_CLASS;
    BOOL created=CreateProcessW(application,command,processAttributes,threadAttributes,inherit,flags,environment,directory,startup,info);
    if(created&&info&&info->hProcess&&g_limitChildRights){
        HANDLE limited=nullptr;
        if(DuplicateHandle(GetCurrentProcess(),info->hProcess,GetCurrentProcess(),&limited,
                           SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0)){
            CloseHandle(info->hProcess);info->hProcess=limited;
        }
    }
    if(created&&info&&info->hProcess&&g_sjMetadataFail){
        HANDLE limited=nullptr;
        if(!DuplicateHandle(GetCurrentProcess(),info->hProcess,GetCurrentProcess(),&limited,
                            SYNCHRONIZE|PROCESS_TERMINATE,FALSE,0)){
            TerminateProcess(info->hProcess,1);WaitForSingleObject(info->hProcess,1000);
            CloseHandle(info->hThread);CloseHandle(info->hProcess);*info={};return FALSE;
        }
        CloseHandle(info->hProcess);info->hProcess=limited;
    }
    return created;
}
}}
`+ownerParentBlocks+'\n'+superJoyPorts+'\n'+superJoyLifecycle+String.raw`
#undef RegisterDeviceNotificationW
#undef UnregisterDeviceNotification
#undef SetupDiGetClassDevsW
#undef SetupDiEnumDeviceInterfaces
#undef SetupDiGetDeviceInterfaceDetailW
#undef SetupDiDestroyDeviceInfoList
#undef CreateFileW
#undef CloseHandle
#undef HidD_GetAttributes
#undef HidD_GetPreparsedData
#undef HidD_FreePreparsedData
#undef HidP_GetCaps
#define ResumeThread SuperJoyFixtureResume

static bool g_forceReadError=false;
static bool ParentHarnessReadFile(HANDLE h,LPVOID buf,DWORD n,LPDWORD read,LPOVERLAPPED ov){
    if(g_forceReadError){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    return ReadFile(h,buf,n,read,ov)!=FALSE;
}
#define ReadFile ParentHarnessReadFile
`+parentApplyFn+String.raw`
#undef ReadFile
#undef ResumeThread
static void resetOwner(){
    if(g_oxp3OwnerRevokedEvent){CloseHandle(g_oxp3OwnerRevokedEvent);g_oxp3OwnerRevokedEvent=0;}
    g_oxp3OwnerEventName.clear();g_oxp3OwnerInterfacePath.clear();
    if(g_oxp3OwnerRetiredProcess){WaitForSingleObject(g_oxp3OwnerRetiredProcess,3000);CloseHandle(g_oxp3OwnerRetiredProcess);g_oxp3OwnerRetiredProcess=0;}
    g_oxp3OwnerRetiredProcessToken.clear();
    g_oxp3OwnerState=ymcc::oxp3_owner::OwnerState::Idle;
    g_oxp3OwnerLease.active.store(false);g_oxp3OwnerLease.revoked.store(true);
    g_oxp3OwnerLease.generation.store(0);g_oxp3OwnerLease.topology.store(0);g_oxp3OwnerLease.admission.store(0);g_oxp3OwnerLease.modelTag.store(0);
    g_oxp3OwnerLease.generationToken.clear();g_oxp3OwnerLease.interfacePath.clear();
    g_oxp3OwnerPendingDebt.store(false);g_oxp3OwnerStopTimedOut.store(false);
    g_oxp3OwnerApplyOutstanding.store(false);g_oxp3OwnerHelperAlive.store(false);
    g_oxp3OwnerChildReturnObserved.store(false);g_oxp3OwnerAdmissionSequence.store(0);
    g_onexX2Transport.state={ymcc::onex_x2::Phase::Live,7,false};g_onexX2Transport.has=true;
    g_onexX2Transport.clearPending=true;g_onexX2Transport.pendingTicks=0;g_onexX2Transport.path=L"path";
    g_onexX2DeviceEpoch.store(9);
    if(g_oxp3OwnerHandoff)oxp3OwnerPublishLeaseState(ymcc::oxp3_owner::OwnerState::Idle);
}
int main(int argc,char**argv){
    if(argc<5){std::cout<<"{\"status\":\"FAIL\",\"checks\":0,\"failures\":[\"argv\"]}";return 1;}
    // A failed fixture must reclaim only this fixture's own process subtree.
    HANDLE fixtureJob=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION fixtureLimits{};
    fixtureLimits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!fixtureJob||!SetInformationJobObject(fixtureJob,JobObjectExtendedLimitInformation,&fixtureLimits,sizeof(fixtureLimits))||
       !AssignProcessToJobObject(fixtureJob,GetCurrentProcess())){
        std::cout<<"{\"status\":\"FAIL\",\"checks\":0,\"failures\":[\"fixture-job-assignment\"]}";return 1;
    }
    std::string dirN=argv[1],runtimeN=argv[2],powerN=argv[3];
    const std::wstring superJoyManagedFixture=U2W(argv[4]);
    int wn=MultiByteToWideChar(CP_UTF8,0,dirN.c_str(),-1,nullptr,0);
    std::wstring dir(wn?wn-1:0,L'\0');MultiByteToWideChar(CP_UTF8,0,dirN.c_str(),-1,dir.data(),wn);
    int wn2=MultiByteToWideChar(CP_UTF8,0,runtimeN.c_str(),-1,nullptr,0);
    std::wstring runtimeRoot(wn2?wn2-1:0,L'\0');MultiByteToWideChar(CP_UTF8,0,runtimeN.c_str(),-1,runtimeRoot.data(),wn2);
    int wn3=MultiByteToWideChar(CP_UTF8,0,powerN.c_str(),-1,nullptr,0);
    std::wstring powerRoot(wn3?wn3-1:0,L'\0');MultiByteToWideChar(CP_UTF8,0,powerN.c_str(),-1,powerRoot.data(),wn3);
    kLightSetterExe=dir+L"\\inert-helper.exe";
    kLightSetterHcRuntimeRoot=runtimeRoot;
    POWER_CONTROL_DIR=powerRoot;
    {std::ofstream f((dirN+"\\bad-helper.exe").c_str());f<<"not an executable";}
    SetEnvironmentVariableW(L"HC011_HOLDER",(dir+L"\\inert-holder.exe").c_str());
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayer3";
    g_machineIdentity.family=YmccFamily::OneXPlayer;
    int checks=0;std::vector<std::string> failures;json receipts=json::object();
    auto check=[&](bool ok,const std::string& id){++checks;if(!ok)failures.push_back(id);};
    auto record=[&](const char* name,const json& r){
        receipts[name]={{"reason",r.value("reason",std::string())},{"ok",r.value("ok",false)},
          {"ownerAdmitted",r.value("ownerAdmitted",false)},{"ownerChildReturned",r.value("ownerChildReturned",false)},
          {"ownerLeaseReleased",r.value("ownerLeaseReleased",false)},{"ownerPendingDebt",r.value("ownerPendingDebt",false)},
          {"exitCode",r.value("exitCode",0)},{"timedOut",r.value("timedOut",false)},
          {"outputTruncated",r.value("outputTruncated",false)},{"outputReadTimedOut",r.value("outputReadTimedOut",false)},
          {"outputReadError",r.value("outputReadError",false)}};
    };
    auto ownedIdle=[&]{return !g_oxp3OwnerLease.active.load()&&!g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Idle;};
    auto invoke=[&](const char* mode,json& out){SetEnvironmentVariableW(L"HC011_CHILD_MODE",U2W(mode).c_str());resetOwner();out=ymccLightSetterApply(true,"solid",10,20,30,40,50,60,50,75);};
    json r;
    for(const auto* cls:{"HandheldCompanion.Devices.AYANEO3","HandheldCompanion.Devices.AYANEONEXT2"}){
        resetOwner();g_machineIdentity.deviceClass=cls;
        const auto actualHelper=kLightSetterExe;kLightSetterExe=dir+L"\\must-not-open-legacy-helper.exe";
        const auto probesBefore=g_sjProbes, resumesBefore=g_sjResumeCalls;
        r=ymccLightSetterApply(true,"solid",10,20,30,40,50,60,50,75);
        check(!r.value("ok",true)&&r.value("reason",std::string())=="light-setter-missing"&&
            !r.value("ownerAdmitted",false)&&ownedIdle()&&g_sjProbes==probesBefore&&g_sjResumeCalls==resumesBefore,
            std::string("superjoy-typed-route-missing-helper-before-discovery:")+cls);
        kLightSetterExe=actualHelper;
    }
`+superJoyCases+String.raw`
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayer3";
    invoke("ok",r);record("ok",r);
    check(r.value("ok",false)&&r.value("reason",std::string())=="ok"&&r.value("resultAccepted",false)&&r.value("ownerChildReturned",false)&&r.value("ownerLeaseReleased",false)&&r.value("exitCode",-1)==0,"parent-full-ok-receipt");
    check(ownedIdle(),"parent-full-ok-owner-idle");
    kLightSetterExe=dir+L"\\bad-helper.exe";invoke("ok",r);record("spawn-failure",r);kLightSetterExe=dir+L"\\inert-helper.exe";
    check(!r.value("ok",true)&&r.value("reason",std::string())=="light-setter-spawn-failed"&&r.contains("error")&&ownedIdle(),"parent-full-spawn-failure-releases-owner");
    invoke("truncate",r);record("truncate",r);
    check(r.value("outputTruncated",false)&&r.value("reason",std::string())=="light-setter-output-truncated","parent-full-read-truncation");
    g_forceReadError=true;invoke("ok",r);g_forceReadError=false;record("read-error",r);
    check(r.value("outputReadError",false)&&r.value("reason",std::string())=="light-setter-output-read-error","parent-full-read-error");
    invoke("partial-hold",r);record("read-timeout",r);
    check(r.value("outputReadTimedOut",false)&&r.value("reason",std::string())=="light-setter-output-read-timeout","parent-full-read-timeout");
    invoke("nojson",r);record("no-json",r);
    check(r.value("reason",std::string())=="light-setter-no-json","parent-full-no-json");
    invoke("nonzero",r);record("nonzero",r);
    check(r.value("reason",std::string())=="light-setter-exit-3"&&r.value("exitCode",0)==3,"parent-full-nonzero-exit");
    invoke("not-ok",r);record("not-ok",r);
    check(r.value("reason",std::string())=="light-setter-result-not-ok","parent-full-child-not-ok");
    invoke("wrong-identity",r);record("wrong-identity",r);
    check(r.value("reason",std::string())=="light-setter-identity-mismatch","parent-full-wrong-identity");
    invoke("wrong-catalog",r);record("wrong-catalog",r);
    check(r.value("reason",std::string())=="light-setter-catalog-mismatch","parent-full-wrong-catalog");
    invoke("wrong-operation",r);record("wrong-operation",r);
    check(r.value("reason",std::string())=="light-setter-operation-mismatch","parent-full-wrong-operation");
    invoke("sleep",r);record("timeout",r);
    check(r.value("timedOut",false)&&r.value("reason",std::string())=="light-setter-timeout"&&r.value("ownerChildReturned",false)&&r.value("ownerLeaseReleased",false)&&ownedIdle(),"parent-full-timeout-observed-return");
    check(r.value("exitCode",0)==1,"parent-full-timeout-exitcode-initializer");
    g_limitChildRights=true;
    invoke("sleep-unreturned",r);record("timeout-unreturned",r);
    g_limitChildRights=false;
    check(r.value("timedOut",false)&&r.value("reason",std::string())=="light-setter-timeout"&&!r.value("ownerChildReturned",true)&&!r.value("ownerLeaseReleased",true)&&r.value("ownerPendingDebt",true)&&g_oxp3OwnerRetiredProcess!=0&&g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring,"parent-full-timeout-unreturned-retains-exact-child");
    std::string blockedReason;
    check(!oxp3OwnerAdmit(GetTickCount64()+100,blockedReason)&&blockedReason=="owner-retiring-debt"&&!onexX2OwnerWriteAllowed()&&!onexX2OwnerBindAllowed(L"path"),"parent-full-unreturned-blocks-admission-write-bind");
    oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});
    check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerRetiredProcess!=0,"parent-full-unreturned-tick-keeps-debt");
    bool unreturnedReturned=false;
    for(int i=0;i<1500;i++){if(oxp3OwnerRetiredProcessReturned()){unreturnedReturned=true;break;}Sleep(10);}
    check(unreturnedReturned&&g_oxp3OwnerRetiredProcess==0&&g_oxp3OwnerChildReturnObserved.load(),"parent-full-unreturned-observed-return-once");
    oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});
    check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Idle&&!g_oxp3OwnerPendingDebt.load()&&onexX2OwnerWriteAllowed()&&onexX2OwnerBindAllowed(L"path"),"parent-full-unreturned-single-settlement-reopens");
    std::string readmitReason;
    check(oxp3OwnerAdmit(GetTickCount64()+100,readmitReason)&&g_oxp3OwnerLease.generationToken=="7:9:2","parent-full-unreturned-readmit");
    check(!oxp3OwnerRelease("7:9:1",GetTickCount64()+100,true)&&g_oxp3OwnerLease.active.load()&&g_oxp3OwnerLease.generationToken=="7:9:2","parent-full-unreturned-old-token-rejected");
    check(oxp3OwnerRelease("7:9:2",GetTickCount64()+100,false),"parent-full-unreturned-release");
    resetOwner();
    std::string reason;
    check(oxp3OwnerAdmit(GetTickCount64()+100,reason)&&g_oxp3OwnerLease.generationToken=="7:9:1","parent-debt-admit");
    g_oxp3OwnerApplyOutstanding.store(true);g_oxp3OwnerHelperAlive.store(true);
    wchar_t win[MAX_PATH]{};GetWindowsDirectoryW(win,MAX_PATH);
    std::wstring ping=L"cmd.exe /c \""+std::wstring(win)+L"\\System32\\ping.exe\" -n 9 127.0.0.1 >nul";
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};const BOOL spawned=CreateProcessW(nullptr,const_cast<wchar_t*>(ping.c_str()),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
    if(spawned)CloseHandle(pi.hThread);
    check(spawned!=FALSE&&pi.hProcess!=0,"parent-debt-real-child-spawned");
    check(!oxp3OwnerParentTransition(pi.hProcess,true,false,"7:9:1",true)&&g_oxp3OwnerRetiredProcess==pi.hProcess&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring,"parent-debt-retains-exact-child");
    std::string reason2;
    check(!oxp3OwnerAdmit(GetTickCount64()+100,reason2)&&reason2=="owner-retiring-debt"&&!onexX2OwnerWriteAllowed()&&!onexX2OwnerBindAllowed(L"path"),"parent-debt-blocks-admission-write-bind");
    oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});
    check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Retiring&&g_oxp3OwnerPendingDebt.load()&&g_oxp3OwnerRetiredProcess==pi.hProcess,"parent-debt-tick-keeps-debt");
    bool returned=false;
    for(int i=0;i<1200;i++){if(oxp3OwnerRetiredProcessReturned()){returned=true;break;}Sleep(10);}
    check(returned&&g_oxp3OwnerRetiredProcess==0&&g_oxp3OwnerChildReturnObserved.load(),"parent-debt-observed-return-once");
    oxp3OwnerSettleFromTick(ymcc::onex_x2::Snapshot{ymcc::onex_x2::Phase::Live,7,false});
    check(g_oxp3OwnerState==ymcc::oxp3_owner::OwnerState::Idle&&!g_oxp3OwnerPendingDebt.load()&&onexX2OwnerWriteAllowed()&&onexX2OwnerBindAllowed(L"path"),"parent-debt-single-settlement-reopens");
    check(oxp3OwnerAdmit(GetTickCount64()+100,reason)&&g_oxp3OwnerLease.generationToken=="7:9:2"&&oxp3OwnerRelease("7:9:2",GetTickCount64()+100,false),"parent-debt-readmit-after-return");
    std::cout<<"{\"status\":"<<(failures.empty()?"\"PASS\"":"\"FAIL\"")<<",\"checks\":"<<checks<<",\"receipts\":"<<receipts.dump()<<",\"failures\":[";
    for(size_t i=0;i<failures.size();++i){if(i)std::cout<<",";std::cout<<"\""<<failures[i]<<"\"";}
    std::cout<<"]}";
    return failures.empty()?0:1;
}
`;
// Compile the original managed owner fixture once; only its explicit software child is launched.
const managedArtifacts=path.join(out,'managed-build'),managedIsolation=path.join(out,'managed-isolation.targets');
fs.writeFileSync(managedIsolation,'<Project><Target Name="ExcludeWorkspaceOutputs" BeforeTargets="CoreCompile"><ItemGroup><Compile Remove="$(MSBuildProjectDirectory)/bin/**/*.cs"/><Compile Remove="$(MSBuildProjectDirectory)/obj/**/*.cs"/></ItemGroup></Target></Project>');
run('C:/Program Files/dotnet/dotnet.exe',['build',path.join(repo,'tools/LightSetterSelfTest/LightSetterSelfTest.csproj'),'-c','Release','--artifacts-path',managedArtifacts,'-p:StartupObject=SuperJoyOwnerLeaseTests','-p:CustomAfterMicrosoftCommonTargets='+managedIsolation,'--nologo','-v:q'],'managed-fixture-build.log');
const managedFixture=path.join(managedArtifacts,'bin/LightSetterSelfTest/release/LightSetterSelfTest.exe');
if(!fs.existsSync(managedFixture))throw Error('Missing original managed owner fixture apphost');
const powerControl=process.env.YEMAN_POWER_CONTROL_DIR||'C:\\SOFT\\YeMan\\PowerControl';
const runtimeRoot=path.join(powerControl,'handheldcompanion-runtime','YMCC-DEVICECORE-20261018-v1');
const runtimeDll=path.join(runtimeRoot,'HandheldCompanion.dll');
if(!fs.existsSync(runtimeDll))throw Error('Fixed HC runtime missing: '+runtimeDll);
const runtimeDllSha=sha(runtimeDll);
const runtimeManifest=path.join(runtimeRoot,'HandheldCompanion.runtime.json');
const runtimeManifestSha=fs.existsSync(runtimeManifest)?sha(runtimeManifest):null;
fs.writeFileSync(path.join(out,'inert-holder.cpp'),inertHolderCpp);
fs.writeFileSync(path.join(out,'inert-helper.cpp'),inertHelperCpp);
fs.writeFileSync(path.join(out,'parent-full.cpp'),parentFullCpp);
fs.writeFileSync(path.join(out,'parent-full-build.cmd'),preamble+`cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "${out}/inert-holder.dependencies.json" "${out}/inert-holder.cpp" /Fo"${out}/inert-holder.obj" /Fe"${out}/inert-holder.exe"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "${out}/inert-helper.dependencies.json" "${out}/inert-helper.cpp" /Fo"${out}/inert-helper.obj" /Fe"${out}/inert-helper.exe"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "${out}/parent-full.dependencies.json" "${out}/parent-full.cpp" /I"${repo}/deps/json" /I"${repo}/native" /I"${repo}/capabilities" /Fo"${out}/parent-full.obj" /Fe"${out}/parent-full.exe"
exit /b %errorlevel%
`);
run('cmd.exe',['/d','/c',path.join(out,'parent-full-build.cmd')],'parent-full-build.log');
const parentFull=JSON.parse(run(path.join(out,'parent-full.exe'),[out,runtimeRoot,powerControl,managedFixture],'parent-full.log'));
parentFull.superJoyManagedFixture={apphost:managedFixture,apphostSha256:sha(managedFixture),dllSha256:sha(managedFixture.replace(/\.exe$/i,'.dll')),testPolicyPort:false,productRouteAdmitted:true,actualMachineSelector:true,shippingParserAndCatalog:true};
parentFull.runtime={root:runtimeRoot,dll:{path:runtimeDll,sha256:runtimeDllSha},manifest:{path:runtimeManifest,sha256:runtimeManifestSha}};
parentFull.privateOwnerNamespace=privateOwnerHeaderScope;
result.parentFull=parentFull;result.checks+=parentFull.checks;result.failures.push(...parentFull.failures.map(f=>'parent-full:'+f));result.status=result.failures.length?'FAIL':'PASS';
const freshMain=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
const compilerInputDrift=[...compilerInputs.values()].filter(row=>sha(row.path)!==row.sha256).map(row=>row.path);
if(compilerInputDrift.length)throw Error('Actual native compiler inputs changed: '+compilerInputDrift.join(','));
fs.writeFileSync(path.join(out,'native-compiler-inputs.json'),JSON.stringify({status:'PASS_INPUT_IDENTITY',files:[...compilerInputs.values()],reports:[...dependencyReports],inputDrift:compilerInputDrift},null,2)+'\n');
const scopeDrift=[...extractedFunctions].filter(([signature,expected])=>fragmentHash(extract(freshMain,signature))!==expected).map(([signature])=>signature);
for(const [name,text] of [['setLed IPC',handler],['ROG enums',enums],['MSI packet definitions',main.slice(msiStart,msiEnd)],['SuperJoy native lifecycle',superJoyLifecycle],['actual machine enum',machineIdentityEnum],['actual machine struct',machineIdentityStruct]])if(!text||!freshMain.includes(text))scopeDrift.push(name);
for(const [file,expected] of includedSources)if(sha(path.join(repo,file))!==expected)scopeDrift.push(file);
result.sourceScope={functions:[...extractedFunctions].map(([signature,sha256])=>({signature,sha256})),includes:[...includedSources].map(([file,sha256])=>({file,sha256})),scopeDrift,currentWholeSourceHash:sha(path.join(repo,'native/main.cpp')),wholeSourceChanged:sha(path.join(repo,'native/main.cpp'))!==sourceHash,wholeApplicationPassed:false};
if(scopeDrift.length)throw Error('Compiled native consumption scope changed: '+scopeDrift.join(','));
if(prepareOnly)result.status='NOT_RUN_PREPARE_ONLY';
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
