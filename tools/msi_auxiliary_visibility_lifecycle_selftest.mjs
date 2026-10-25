// Actual production lifecycle excerpts, standalone fakes only. No product/Host/Windows device API.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const evidence=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Validation/MSI-Controller-R9-20261013/msi-auxiliary-cloak');
const out=path.join(evidence,'lifecycle-integration-tests');
assert.equal(path.basename(evidence),'msi-auxiliary-cloak');
assert.ok(evidence.toLowerCase().startsWith('g:\\yemancc-work\\build\\validation\\msi-controller-r9-20261013\\'));
fs.mkdirSync(out,{recursive:true});
const stage=process.argv.includes('--candidate')?'candidate':'baseline';
const candidateIndex=process.argv.indexOf('--candidate');
const sourcePath=stage==='candidate'?path.resolve(process.argv[candidateIndex+1]):path.join(out,'review-baseline-main.cpp');
const equivalentIndex=process.argv.indexOf('--equivalent-source');
const equivalentPath=equivalentIndex>=0?path.resolve(process.argv[equivalentIndex+1]):null;
const hash=b=>createHash('sha256').update(b).digest('hex').toUpperCase();
const sourceBytes=fs.readFileSync(sourcePath),text=sourceBytes.toString('utf8').replaceAll('\r\n','\n');
if(stage==='baseline'){
 const review=JSON.parse(fs.readFileSync(path.join(evidence,'independent-review/SOURCE-BINDING.json'),'utf8'));
 assert.equal(hash(sourceBytes),review.main.beforeSha256,'baseline is the independently reviewed initial snapshot');
}
let suppliedBinding=null,suppliedBindingPath=null;
if(stage==='candidate'){
 suppliedBindingPath=sourcePath.replace(/-main\.cpp$/,'-binding.json');
 assert.notEqual(suppliedBindingPath,sourcePath);
 suppliedBinding=JSON.parse(fs.readFileSync(suppliedBindingPath,'utf8'));
 assert.equal(hash(sourceBytes),suppliedBinding.candidateSha256,'immutable parent candidate binding');
}
// Keep every selected function/callsite verbatim. This only reads/extracts source, never imports test tools.
function extent(s,brace){let depth=0,mode='code';for(let i=brace;i<s.length;i++){
 const c=s[i],n=s[i+1];if(mode==='line'){if(c==='\n')mode='code';continue;}
 if(mode==='block'){if(c==='*'&&n==='/'){mode='code';i++;}continue;}
 if(mode==='string'||mode==='char'){if(c==='\\'){i++;continue;}if(c===(mode==='string'?'"':"'"))mode='code';continue;}
 if(c==='/'&&n==='/'){mode='line';i++;continue;}if(c==='/'&&n==='*'){mode='block';i++;continue;}
 if(c==='"'){mode='string';continue;}if(c==="'"){mode='char';continue;}
 if(c==='{')depth++;else if(c==='}'&&--depth===0)return i+1;
 }throw Error('unbalanced production block');}
function selected(name,sourceText=text){
 const text=sourceText;
 const rx=new RegExp('^static[^;{]*\\b'+name+'\\s*\\(','gm'),found=[];
 for(const m of text.matchAll(rx)){
  let i=m.index+m[0].length,depth=1;
  for(;i<text.length&&depth;i++){if(text[i]==='(')depth++;if(text[i]===')')depth--;}
  const brace=text.indexOf('{',i),semi=text.indexOf(';',i);
  if(brace<0||(semi>=0&&semi<brace))continue;
  found.push({name,line:text.slice(0,m.index).split('\n').length,body:text.slice(m.index,extent(text,brace))});
 }
 assert.equal(found.length,1,'one definition '+name);return found[0];
}
function type(name,kind='struct',sourceText=text){const text=sourceText;const start=text.indexOf(kind+' '+name+' ');assert.ok(start>=0,name);return text.slice(start,extent(text,text.indexOf('{',start)))+';';}
function statementIn(body,needle){const start=body.indexOf(needle);assert.ok(start>=0,needle);const brace=body.indexOf('{',start);return body.slice(start,extent(body,brace));}
const functionNames=['ascii_lower','trim_ascii','hidHideUpper','hidHideIsVirtualIdentity','hidHideContainsPath','hidHideAppendTarget','hidHidePhysicalTargets','hidHideInverseApplicationListState','hidHideVirtualPhysicalTargets','hidHideRefreshPhysicalHide','hidHideReapplyReconnectedSource','inputHostReconcilePhysicalSourceOnLifecycleThread','inputHostLateHideEpisodeEqual','inputHostLateHideEpisodeAlreadyCompleted','inputHostLateHideMarkEpisodeCompleted','inputHostLateHideEnqueueEpisode'];
const actual=Object.fromEntries(functionNames.map(n=>[n,selected(n)]));
const sourceApply=selected('hidHideApplyPhysicalTransaction');
const activeApply=statementIn(sourceApply.body,'if (g_hidHideVisibilityJournal.active)');
const lateStart=text.indexOf('                if (!lateHideRefused && !kVirtualTestKeepPhysicalVisible &&');assert.ok(lateStart>0);
const lateBlock=text.slice(lateStart,extent(text,text.indexOf('{',lateStart)));
const types=['HidHidePhysicalIdentity','HidHideVisibilitySnapshot','HidHideVisibilityJournal','HidHideMsiMainSourceRecord','HidHideInterfaceRecord','HidHidePeriodicState','InputHostLateHideEpisodeKey','VirtualOutputSuppressionObservation'];
const constantNames=['kInputHostReapplyHideCooldownMs','kInputHostReapplyGuardMaxRepairs'];
const actualConstants=Object.fromEntries(constantNames.map(n=>{const m=text.match(new RegExp('^static constexpr[^;]*\\b'+n+'\\s*=[^;]*;','m'));assert.ok(m,'actual constant '+n);return [n,m[0]];}));
const fixedHeaderBytes=fs.readFileSync(path.join(repo,'native/msi_virtual_auxiliary_cloak_plan.h'));
const headerPath=path.join(out,stage+'-msi_virtual_auxiliary_cloak_plan.h');fs.writeFileSync(headerPath,fixedHeaderBytes);
const fixturePath=path.join(evidence,'tests/log-fixture.json');
const deps=path.join(evidence,'tests/deps');
const extraPath=path.join(out,'candidate-extraction-spec.json');
let extra={types:[],functionsBeforeWrapper:[],functionsAfterRefresh:[],functionsBeforeReapply:[],resetCpp:'',testsCpp:''};
if(stage==='candidate'){
 assert.ok(fs.existsSync(extraPath),'parent candidate helper/state extraction spec required');
 extra={...extra,...JSON.parse(fs.readFileSync(extraPath,'utf8'))};
 for(const name of [...extra.functionsBeforeWrapper,...extra.functionsAfterRefresh,...extra.functionsBeforeReapply])actual[name]=selected(name);
}
const cpp=String.raw`
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cctype>
#include "json.hpp"
#include "${stage}-msi_virtual_auxiliary_cloak_plan.h"
using json=nlohmann::json;using DWORD=std::uint32_t;using USHORT=std::uint16_t;using USAGE=std::uint16_t;using ULONGLONG=std::uint64_t;
${type('HidHideIfaceRole','enum class')}
${types.map(n=>type(n)).join('\n')}
${extra.types.map(n=>type(n)).join('\n')}
${actual.ascii_lower.body}
${actual.trim_ascii.body}
${actual.hidHideUpper.body}
${actual.hidHideIsVirtualIdentity.body}
${actual.hidHideContainsPath.body}
${actual.hidHideAppendTarget.body}
${actual.hidHidePhysicalTargets.body}
${actual.hidHideInverseApplicationListState.body}
static std::wstring U2W(const std::string&s){return std::wstring(s.begin(),s.end());}
static std::string W2U(const std::wstring&s){std::string r;for(wchar_t c:s)r.push_back(static_cast<char>(c));return r;}
static unsigned cases=0,checks=0;static void check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
static HidHideVisibilityJournal g_hidHideVisibilityJournal;
static std::mutex g_inputHostMx,g_virtualOutputObservationMx,g_lateHideStateMx;
static VirtualOutputSuppressionObservation g_virtualOutputSuppression;
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
enum class YmccFamily{MsiClaw,Other};enum class YmccInterfacePolicy{MsiClawDinput,Other};
static struct{YmccFamily family=YmccFamily::MsiClaw;}g_machineIdentity;
static YmccInterfacePolicy ymccFamilyInterfacePolicy(YmccFamily f){return f==YmccFamily::MsiClaw?YmccInterfacePolicy::MsiClawDinput:YmccInterfacePolicy::Other;}
static std::atomic<unsigned long long>g_msiFeedbackModeGeneration{7},g_xinputTopologyEpoch{9},g_msiFeedbackSamplePower{1},g_msiFeedbackSampleMode{7},g_msiFeedbackSampleTopology{9};
static std::atomic<bool>g_inputHostPowerAdmissionRevoked{false};
static bool g_inputHostHello=true,g_inputHostPrepared=true,g_inputHostActive=true,g_inputHostTransportFault=false,g_inputHostStartBlocked=false,g_inputHostReleaseQuarantined=false;
static std::atomic<bool>g_inputHostFaultHoldsStart{false},g_inputHostPowerReleaseRequested{false};
static DWORD g_xinputPhysicalSourceMask=1,g_xinputConnectedMask=1;
static unsigned long long g_inputHostEpoch=4;static std::string g_inputHostRunId="test-run";
static ULONGLONG g_inputHostLastPhysicalIdentityCheckTick=0,g_inputHostReapplyHideLastTick=0;
static unsigned g_inputHostReapplyHideRepairs=0,g_inputHostReapplyHideRepairsEpoch=0;
static bool g_inputHostReapplyHideExhausted=false;
static std::string g_inputHostReapplyHideEpisodeKey,g_inputHostSourceSuppressLoggedRunId;
${Object.values(actualConstants).join("\n")}
static constexpr bool kVirtualTestKeepPhysicalVisible=false;
static bool g_lateHideCompletedValid=false;static InputHostLateHideEpisodeKey g_lateHideCompletedEpisode;
static ULONGLONG g_lateHideCompletedAtTick=0;
struct FakeContext{
 bool existingHostAlive=true,authority=true,exit=false,eligible=true,sourceKnown=true,inventoryComplete=true,fullReadback=true,periodicReadback=true,sourceResolved=true,pipeAccepted=true,cliAccepted=true,window=false;
 unsigned long long power=1;ULONGLONG now=10000;unsigned hostAliveCalls=0,sourceReads=0,inventoryReads=0,fullReads=0,slimReads=0,queueCalls=0,pipeCalls=0,cliCalls=0,registryCalls=0,cycleCalls=0,modeWrites=0,targetRestarts=0;
 HidHideMsiMainSourceRecord source;std::vector<HidHidePhysicalIdentity>physical;std::vector<HidHideInterfaceRecord>inventory;HidHideVisibilitySnapshot visibility;
 std::vector<std::wstring>hideWrites;std::vector<json>logs;std::function<void()>onInventory,onHide;
}cfg;
static ULONGLONG GetTickCount64(){return cfg.now;}
static unsigned long long currentPowerGeneration(){return cfg.power;}
static bool inputHostExitFenceRaised(){return cfg.exit;}
static bool msiClawKnownBoardAuthority(){return cfg.authority;}
static bool msiDinputMainSourceEligible(){return cfg.eligible;}
static bool msiDinputManagedMainSourceActive(HidHideMsiMainSourceRecord&r){++cfg.sourceReads;r=cfg.source;return cfg.sourceKnown&&cfg.authority;}
static bool physicalReleaseWindowActive(){return cfg.window;}
static bool inputHostProcessAliveLocked(){++cfg.hostAliveCalls;return cfg.existingHostAlive;}
static std::vector<HidHideInterfaceRecord>hidHideBuildSourceInterfaceInventory(const std::string&,const std::string&,bool*complete=nullptr){++cfg.inventoryReads;if(complete)*complete=cfg.inventoryComplete;if(cfg.onInventory)cfg.onInventory();return cfg.inventory;}
static bool hidHideGetPhysicalIdentities(std::vector<HidHidePhysicalIdentity>&r){r=cfg.physical;return cfg.sourceResolved;}
static void hidHideServiceHealth(const char*){}
static bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot&r){++cfg.fullReads;r=cfg.visibility;return cfg.fullReadback;}
static bool hidHideCapturePeriodicVisibilityState(HidHidePeriodicState&r){++cfg.slimReads;r.hidden=cfg.visibility.hidden;r.cloaking=cfg.visibility.cloaking;return cfg.periodicReadback;}
static std::wstring hidHideCurrentModulePath(){return L"test-module.exe";}
static void appendNativeLifecycleLog(const char*event,const json&row){auto x=row;x["event"]=event;cfg.logs.push_back(x);}
static void inputHostExitFenceLogRefusal(const char*,const char*){}
static bool msiControllerTraceEnabled(){return false;}
static void msiControllerTraceLog(const char*,const json&,bool){}
static json msiControllerVisibilityApplications(const HidHideVisibilitySnapshot&,bool){return json::array();}
static bool fakeHide(const std::wstring&target,bool accepted){if(!accepted)return false;cfg.hideWrites.push_back(target);if(!hidHideContainsPath(cfg.visibility.hidden,W2U(target)))cfg.visibility.hidden.push_back(target);if(cfg.onHide)cfg.onHide();return true;}
static bool inputHostHidHideApplyViaPipeLocked(const std::wstring&op,const std::wstring&target){++cfg.pipeCalls;check(op==L"hide","only hide reaches fake pipe");return fakeHide(target,cfg.pipeAccepted);}
static bool hidHideRun(const std::wstring&command){++cfg.cliCalls;const auto first=command.find(L'"'),last=command.rfind(L'"');check(command.rfind(L"--dev-hide ",0)==0&&first!=std::wstring::npos&&last>first,"only dev-hide reaches fake CLI");return fakeHide(command.substr(first+1,last-first-1),cfg.cliAccepted);}
static void virtualOutputNoteSuppression(unsigned long long epoch,ULONGLONG now,int verdict){g_virtualOutputSuppression={epoch,now,verdict};}
static std::vector<std::string>hidHidePhysicalIdentityKeys(const std::vector<HidHidePhysicalIdentity>&p){std::vector<std::string>x;for(const auto&r:p)x.push_back(r.deviceInstanceId);return x;}
static unsigned long long inputHostLateHideIdentityHash(const std::string&){return 1;}
static void inputCaptureLogVisibilitySnapshotGated(const char*,bool){}
static bool inputHostQueueLifecycleWorkInternal(const std::string&,bool*coalesced){++cfg.queueCalls;if(coalesced)*coalesced=false;return true;}
static unsigned long long inputHostLateHidePendingCommit(const InputHostLateHideEpisodeKey&,const std::string&,bool){return 42;}
static bool hidHideRefreshPhysicalHide(const std::string&,const char*);
static bool hidHideApplyPhysicalTransaction(const std::string&transactionId,bool powerCycle=true){${activeApply}return false;}
${type('InputHostLateHideEnqueueOutcome','enum class')}
${actual.inputHostLateHideEpisodeEqual.body}
${actual.inputHostLateHideEpisodeAlreadyCompleted.body}
${actual.inputHostLateHideMarkEpisodeCompleted.body}
${actual.inputHostLateHideEnqueueEpisode.body}
${extra.functionsBeforeWrapper.map(n=>actual[n].body).join('\n')}
${actual.hidHideVirtualPhysicalTargets.body}
${actual.hidHideRefreshPhysicalHide.body}
${extra.functionsAfterRefresh.map(n=>actual[n].body).join('\n')}
${extra.functionsBeforeReapply.map(n=>actual[n].body).join('\n')}
${actual.hidHideReapplyReconnectedSource.body}
${actual.inputHostReconcilePhysicalSourceOnLifecycleThread.body}
static bool replayActualLateHideCompletion(const InputHostLateHideEpisodeKey&episode){
 const bool lateHideRefused=false,lateHideRecordActive=true;const auto lateHideEpisode=episode;const unsigned long long lateHideToken=42,lateHideHostGen=4;const ULONGLONG lateHideAgeMs=0;HidHideMsiMainSourceRecord lateHideRecord=cfg.source;
 ${lateBlock}
 return g_lateHideCompletedValid;
}
static std::string rootId,mainId,auxA,auxB;
static void reset(const json&fixture){cfg=FakeContext{};g_realStickTest={true,true};g_machineIdentity.family=YmccFamily::MsiClaw;
 g_inputHostHello=true;g_inputHostPrepared=true;g_inputHostActive=true;g_inputHostTransportFault=false;g_inputHostStartBlocked=false;g_inputHostReleaseQuarantined=false;g_inputHostFaultHoldsStart=false;g_inputHostPowerReleaseRequested=false;g_msiFeedbackModeGeneration=7;g_xinputTopologyEpoch=9;g_msiFeedbackSamplePower=1;g_msiFeedbackSampleMode=7;g_msiFeedbackSampleTopology=9;g_inputHostPowerAdmissionRevoked=false;
 g_inputHostLastPhysicalIdentityCheckTick=0;g_inputHostReapplyHideLastTick=0;g_inputHostReapplyHideRepairs=0;g_inputHostReapplyHideRepairsEpoch=0;g_inputHostReapplyHideExhausted=false;g_inputHostReapplyHideEpisodeKey.clear();g_inputHostSourceSuppressLoggedRunId.clear();g_virtualOutputSuppression={};
 g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_inputHostEpoch=4;g_inputHostRunId="test-run";g_lateHideCompletedValid=false;g_lateHideCompletedEpisode={};g_lateHideCompletedAtTick=0;
 const auto&f=fixture.at("facts");const auto&s=f.at("before");rootId=s.at("baseInstanceId");mainId=s.at("instanceId");auxA=fixture.at("expectedIds")[0];auxB=fixture.at("expectedIds")[1];
 cfg.source.valid=true;cfg.source.generation=s.at("bindGeneration");cfg.source.interfacePath=s.at("sourceToken");cfg.source.usbInstance=rootId;cfg.source.vendorId=s.at("vid");cfg.source.productId=s.at("pid");
 HidHidePhysicalIdentity p;p.resolved=true;p.sourcePath=cfg.source.interfacePath;p.deviceInstanceId=mainId;p.baseContainerDeviceInstanceId=rootId;p.baseContainerDevicePath=rootId;p.vendorId=0x0DB0;p.productId=0x1902;p.usagePage=1;p.usage=5;cfg.physical={p};
 for(const auto&r:f.at("inventory")){HidHideInterfaceRecord x;x.instanceId=r.at("instanceId");x.baseContainerDeviceInstanceId=r.at("baseInstanceId");x.miCol=r.at("miCol");x.vendorId=r.at("vid");x.productId=r.at("pid");x.usagePage=r.at("usagePage");x.usage=r.at("usage");cfg.inventory.push_back(x);}
 cfg.visibility.valid=true;cfg.visibility.cloaking=true;cfg.visibility.inverseRaw="off";cfg.visibility.applications={hidHideCurrentModulePath()};
 g_hidHideVisibilityJournal={};g_hidHideVisibilityJournal.active=true;g_hidHideVisibilityJournal.transactionId=g_inputHostRunId;g_hidHideVisibilityJournal.physical=p;g_hidHideVisibilityJournal.physicalSources={p};g_hidHideVisibilityJournal.admittedTick=1;
 ${extra.resetCpp}
}
static bool owned(const std::string&id){return hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,id);}
static bool hidden(const std::string&id){return hidHideContainsPath(cfg.visibility.hidden,id);}
static void ownFour(){g_hidHideVisibilityJournal.hiddenByUs={U2W(rootId),U2W(mainId),U2W(auxA),U2W(auxB)};cfg.visibility.hidden=g_hidHideVisibilityJournal.hiddenByUs;}
static void tick(ULONGLONG delta=2000){cfg.now+=delta;inputHostReconcilePhysicalSourceOnLifecycleThread();}
static void noUnrelatedWrites(){check(cfg.registryCalls==0&&cfg.cycleCalls==0&&cfg.modeWrites==0&&cfg.targetRestarts==0,"no registry/cycle/mode/target restart side effect");}
int main(int argc,char**argv){try{std::ifstream in(argv[1]);json fixture;in>>fixture;json witnesses=json::object();
 if(std::string("${stage}")=="baseline"){
 ++cases;reset(fixture);ownFour();cfg.visibility.hidden.erase(cfg.visibility.hidden.begin());const auto prior=g_hidHideVisibilityJournal;HidHidePeriodicState slim;slim.cloaking=true;slim.hidden=cfg.visibility.hidden;bool issued=false;
 check(hidHideReapplyReconnectedSource(prior,slim,&issued)&&issued,"F1 baseline actual reapply succeeds");check(hidden(auxA)&&hidden(auxB),"F1 aux remain hidden");check(!owned(auxA)&&!owned(auxB),"F1 actual reapply discards aux ownership");
 auto targets=hidHideVirtualPhysicalTargets(cfg.physical[0],cfg.visibility,1,"post-reapply");check(std::find(targets.begin(),targets.end(),auxA)==targets.end()&&std::find(targets.begin(),targets.end(),auxB)==targets.end(),"F1 actual wrapper treats discarded aux as third party");witnesses["F1"]=true;noUnrelatedWrites();
 ++cases;reset(fixture);ownFour();cfg.visibility.hidden.erase(cfg.visibility.hidden.begin()+2);tick();check(!hidden(auxA)&&cfg.hideWrites.empty(),"F2 actual reconcile notices drift but never repairs aux-only absence");check(g_virtualOutputSuppression.verdict==3,"F2 actual slim drift diagnosis");witnesses["F2"]=true;noUnrelatedWrites();
 ++cases;reset(fixture);cfg.visibility.hidden={U2W(rootId),U2W(mainId)};g_hidHideVisibilityJournal.hiddenByUs=cfg.visibility.hidden;cfg.inventoryComplete=false;
 const InputHostLateHideEpisodeKey ep{g_inputHostRunId,4,9,true,1};check(replayActualLateHideCompletion(ep),"F3 actual worker marks baseline refresh complete");check(!owned(auxA)&&!owned(auxB),"F3 aux not admitted");cfg.inventoryComplete=true;unsigned long long token=0;bool coalesced=false;
 check(inputHostLateHideEnqueueEpisode(ep,"",false,&token,&coalesced)==InputHostLateHideEnqueueOutcome::AlreadyCompleted&&cfg.queueCalls==0,"F3 actual enqueue refuses same recovered episode");tick();check(!owned(auxA)&&!hidden(auxA)&&cfg.hideWrites.empty(),"F3 actual healthy reconcile cannot discover never-owned aux");witnesses["F3"]=true;noUnrelatedWrites();
 }else{
 ${extra.testsCpp}
 }
 std::cout<<json{{"passed",true},{"stage","${stage}"},{"cases",cases},{"assertions",checks},{"baselineRegressionWitnesses",witnesses},{"realApiCalls",0},{"productExecuted",false},{"hostExecuted",false},{"driverAcceptanceProven",false},{"scope","Actual production reapply/reconcile/wrapper/refresh and late-hide completion/queue excerpts; external device/pipe/CLI/clock/registry boundaries are fakes"}}.dump()<<'\n';return 0;
 }catch(const std::exception&e){std::cout<<json{{"passed",false},{"stage","${stage}"},{"cases",cases},{"assertions",checks},{"failure",e.what()},{"realApiCalls",0}}.dump()<<'\n';return 1;}}
`;
// Add declarations required by actual helpers whose order in the product differs from the harness.
let generated=cpp;
const definitionTypes=[['HidHideIfaceRole','enum class'],...types.map(n=>[n,'struct']),...extra.types.map(n=>[n,'struct']),['InputHostLateHideEnqueueOutcome','enum class']];
const actualTypes=Object.fromEntries(definitionTypes.map(([name,kind])=>[name,{kind,line:text.slice(0,text.indexOf(kind+' '+name+' ')).split('\n').length,sha256:hash(type(name,kind))}]));
let equivalence=null;
if(equivalentPath){
 const equivalentBytes=fs.readFileSync(equivalentPath),equivalentText=equivalentBytes.toString('utf8').replaceAll('\r\n','\n');
 const functions={...actual,[sourceApply.name]:sourceApply};
 const functionMatches=Object.fromEntries(Object.entries(functions).map(([name,item])=>[name,{sha256:hash(item.body),equivalentSha256:hash(selected(name,equivalentText).body),equal:item.body===selected(name,equivalentText).body}]));
 const typeMatches=Object.fromEntries(definitionTypes.map(([name,kind])=>[name,{sha256:hash(type(name,kind)),equivalentSha256:hash(type(name,kind,equivalentText)),equal:type(name,kind)===type(name,kind,equivalentText)}]));
 const constantMatches=Object.fromEntries(Object.entries(actualConstants).map(([name,value])=>{const m=equivalentText.match(new RegExp('^static constexpr[^;]*\\b'+name+'\\s*=[^;]*;','m'));return [name,{equal:!!m&&m[0]===value,sha256:hash(value)}];}));
 const lateOtherStart=equivalentText.indexOf('                if (!lateHideRefused && !kVirtualTestKeepPhysicalVisible &&');
 assert.ok(lateOtherStart>=0);
 const lateOther=equivalentText.slice(lateOtherStart,extent(equivalentText,equivalentText.indexOf('{',lateOtherStart)));
 equivalence={path:equivalentPath,sha256:hash(equivalentBytes),sourceStableAfterComparison:hash(equivalentBytes)===hash(fs.readFileSync(equivalentPath)),functionMatches,typeMatches,constantMatches,actualLateHideCompletionEqual:lateOther===lateBlock,fullInitialApplyComparedButNotFullyExecuted:true};
 equivalence.allEqual=Object.values(functionMatches).every(x=>x.equal)&&Object.values(typeMatches).every(x=>x.equal)&&Object.values(constantMatches).every(x=>x.equal)&&lateOther===lateBlock;
 assert.ok(equivalence.allEqual,'tested production definitions are equivalent in export snapshot');
}

// W2U and U2W are externals for the actual early helpers; declarations precede them.
generated=generated.replace(actual.ascii_lower.body,'static std::wstring U2W(const std::string&);\nstatic std::string W2U(const std::wstring&);\n'+actual.ascii_lower.body);
if(extra.declarationsCpp)generated=generated.replace('static HidHideVisibilityJournal g_hidHideVisibilityJournal;', 'static HidHideVisibilityJournal g_hidHideVisibilityJournal;\n'+extra.declarationsCpp);
const cppPath=path.join(out,stage+'-actual-lifecycle.cpp'),exePath=path.join(out,stage+'-actual-lifecycle.exe');fs.writeFileSync(cppPath,generated);
const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert.ok(fs.existsSync(vcvars));
const bat=path.join(out,stage+'-compile.cmd');const quote=s=>'"'+s.replaceAll('"','')+'"';
fs.writeFileSync(bat,'@echo off\r\ncall '+quote(vcvars)+' >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I'+quote(out)+' /I'+quote(deps)+' '+quote(cppPath)+' /Fo'+quote(path.join(out,stage+'-actual-lifecycle.obj'))+' /Fe'+quote(exePath)+'\r\nexit /b %errorlevel%\r\n');
const build=spawnSync('cmd.exe',['/d','/c',bat],{encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:6*1024*1024,cwd:out});fs.writeFileSync(path.join(out,stage+'-build.log'),(build.stdout||'')+(build.stderr||''));
const binding={stage,sourcePath,providedParentBindingPath:suppliedBindingPath,providedParentBindingSha256:suppliedBindingPath?hash(fs.readFileSync(suppliedBindingPath)):null,actualTypes,equivalence,extractionSpecSha256:stage==='candidate'?hash(fs.readFileSync(extraPath)):null,sourceSha256:hash(sourceBytes),sourceHashAfterRead:hash(fs.readFileSync(sourcePath)),sourceStableDuringRead:hash(sourceBytes)===hash(fs.readFileSync(sourcePath)),selftestSha256:hash(fs.readFileSync(fileURLToPath(import.meta.url))),headerSha256:hash(fixedHeaderBytes),fixtureSha256:hash(fs.readFileSync(fixturePath)),jsonHeaderSha256:hash(fs.readFileSync(path.join(deps,'json.hpp'))),actualFunctions:Object.fromEntries(Object.entries(actual).map(([n,v])=>[n,{line:v.line,sha256:hash(v.body)}])),actualConstants:Object.fromEntries(Object.entries(actualConstants).map(([n,v])=>[n,{source:v,sha256:hash(v)}])),actualApplyActiveBranchSha256:hash(activeApply),actualLateHideCompletion:{line:text.slice(0,lateStart).split('\n').length,sha256:hash(lateBlock)},generatedCppSha256:hash(Buffer.from(generated)),compileSucceeded:build.status===0,realApiCalls:0,productExecuted:false,hostExecuted:false,notTested:['Real driver/SetupAPI ancestry/handle behavior','Full initial apply and restore orchestration','OS mutex wait/control interleaving (tests exercise actual locked rechecks with supplied flags)','Full product worker queue timing and real Steam consumption']};
fs.writeFileSync(path.join(out,stage+'-source-test-binding.json'),JSON.stringify(binding,null,2)+'\n');
if(build.status!==0){console.error((build.stdout||'')+(build.stderr||''));throw Error('Standalone actual-excerpt compile failed; see '+stage+'-build.log');}
const run=spawnSync(exePath,[fixturePath],{encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:2*1024*1024,cwd:out});fs.writeFileSync(path.join(out,stage+'-run.log'),(run.stdout||'')+(run.stderr||''));
const result=JSON.parse(run.stdout.trim());binding.exeSha256=hash(fs.readFileSync(exePath));binding.resultSha256=hash(Buffer.from(JSON.stringify(result,null,2)+'\n'));binding.sourceHashAfterExecution=hash(fs.readFileSync(sourcePath));binding.sourceStableThroughExecution=hash(sourceBytes)===binding.sourceHashAfterExecution;binding.passed=run.status===0&&result.passed&&binding.sourceStableThroughExecution;assert.ok(binding.sourceStableThroughExecution,'immutable source stays stable through standalone execution');
fs.writeFileSync(path.join(out,stage+'-result.json'),JSON.stringify(result,null,2)+'\n');fs.writeFileSync(path.join(out,stage+'-source-test-binding.json'),JSON.stringify(binding,null,2)+'\n');
console.log(JSON.stringify(result));assert.equal(run.status,0,'standalone actual production excerpt execution');assert.ok(result.passed);
