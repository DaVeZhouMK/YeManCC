// Standalone source-extracted test. No product/Host/SDK/device API is linked.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const root=path.resolve(process.argv[2]||'');
assert.ok(root.toLowerCase().startsWith('g:\\yemancc-work\\build\\validation\\msi-controller-r9-20261013\\reacquire-cancel-evidence'),'authorized R9 output only');
const beforePath=path.resolve(process.argv[3]||root+'/before/native/main.cpp');
const afterPath=path.resolve(process.argv[4]||repo+'/native/main.cpp');
const before=fs.readFileSync(beforePath,'utf8').replaceAll('\r\n','\n'),after=fs.readFileSync(afterPath,'utf8').replaceAll('\r\n','\n');
function fn(s,name){const match=new RegExp('static (?:void|bool|unsigned long long) '+name+'\\(').exec(s);assert.ok(match,name);const a=match.index,b=s.indexOf('{',a);let depth=1,state='code';for(let i=b+1;i<s.length;i++){let c=s[i],n=s[i+1];if(state==='line'){if(c==='\n')state='code';continue}if(state==='block'){if(c==='*'&&n==='/'){state='code';i++}continue}if(state==='str'||state==='char'){if(c==='\\'){i++;continue}if(c===(state==='str'?'"':"'"))state='code';continue}if(c==='/'&&n==='/'){state='line';i++;continue}if(c==='/'&&n==='*'){state='block';i++;continue}if(c==='"'){state='str';continue}if(c==="'"){state='char';continue}if(c==='{')depth++;if(c==='}'&&--depth===0)return s.slice(a,i+1);}throw Error('unbalanced '+name);}
function guard(s){const a=s.indexOf('    if(g_msiDinput.inputLostRecovery.active){'),b=s.indexOf('    const auto readPower=',a);assert.ok(a>0&&b>a);return s.slice(a,b);}
const oldRelease=fn(before,'msiDinputReleasePad'),newRelease=fn(after,'msiDinputReleasePad'),oldGuard=guard(before),newGuard=guard(after);
const hardware=fn(before,'hardwareWriteAllowed').replace('hardwareWriteAllowed()','hardwareWriteAllowedBody()');
const power=fn(before,'currentPowerGeneration');
const out=root+'/tests';fs.mkdirSync(out,{recursive:true});
const cpp=String.raw`
#define NOMINMAX
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include "json.hpp"
#include "msi_reacquire_cancel_evidence.h"
using Json=nlohmann::json;
static std::vector<std::string> trace;
static std::vector<Json> rows;
static bool diagnosticThrows=false;
struct FaultJson:Json{
 using Json::Json;
 Json& operator[](const char* key){if(diagnosticThrows&&std::strcmp(key,"reacquireCancel")==0)throw std::runtime_error("synthetic diagnostic failure");return Json::operator[](key);}
};
using json=FaultJson;
static void appendNativeLifecycleLog(const char* event,json detail){trace.push_back(std::string("log:")+event+":"+detail.at("phase").get<std::string>());rows.push_back(static_cast<const Json&>(detail));}
static unsigned stores=0;
template<class T>struct Atomic{
 std::string name;T value{};std::vector<T> reads;unsigned count=0;
 T load(std::memory_order=std::memory_order_seq_cst){trace.push_back("read:"+name);const auto index=count++;return reads.empty()?value:reads[std::min<std::size_t>(index,reads.size()-1)];}
 void store(T v,std::memory_order){trace.push_back("store:"+name);value=v;++stores;}
};
enum class PowerLifecycle{Ready,Other};
static struct{bool isolated=false;}g_aiFanMockSession;
static Atomic<bool>g_hardwareWriteGate;
static Atomic<PowerLifecycle>g_powerLifecycle;
static Atomic<std::uint64_t>g_powerGeneration,g_xinputTopologyEpoch,g_msiFeedbackModeGeneration;
${hardware}
static unsigned hardwareCalls=0;
static bool hardwareWriteAllowed(){++hardwareCalls;trace.push_back("hardwareWriteAllowed");return hardwareWriteAllowedBody();}
${power}
static unsigned staleCalls=0,exhaustedCalls=0,dueCalls=0;
struct Recovery:ymcc::msi::AcquireRecovery{
 bool stale(std::uint64_t p,std::uint64_t t,std::uint64_t m)const noexcept{++staleCalls;trace.push_back("stale");return AcquireRecovery::stale(p,t,m);}
 bool exhausted()const noexcept{++exhaustedCalls;trace.push_back("exhausted");return AcquireRecovery::exhausted();}
 bool due(std::uint64_t n,std::uint64_t p,std::uint64_t t,std::uint64_t m)const noexcept{++dueCalls;trace.push_back("due");return AcquireRecovery::due(n,p,t,m);}
 void attempted(std::uint64_t n)noexcept{trace.push_back("attempted");AcquireRecovery::attempted(n);}
 void reset()noexcept{trace.push_back("reset");AcquireRecovery::reset();}
};
using HRESULT=long;static bool FAILED(HRESULT h){return h<0;}
static std::uint64_t fakeNow=5000;
static constexpr unsigned kMsiDinputRebindBackoffMs=765;
struct Pad{unsigned acquire=0,unacquire=0,release=0;HRESULT result=0;HRESULT Acquire(){trace.push_back("Acquire");acquire++;return result;}HRESULT Unacquire(){trace.push_back("Unacquire");unacquire++;return 0;}void Release(){trace.push_back("Release");release++;}}pad;
struct State{Recovery inputLostRecovery;Pad* pad=nullptr;Atomic<unsigned>productId;Atomic<bool>mainInterfaceMatched;unsigned bindCount=42,notAcquiredRetries=3;bool notAcquiredLogged=true,openTried=false;std::uint64_t lastFailAt=11;unsigned backoffMs=12;}g_msiDinput;
static void msiSampleClose(const char* reason){trace.push_back(std::string("sampleClose:")+reason);}
static void msiDinputFeedbackStop(const char* reason){trace.push_back(std::string("feedbackStop:")+reason);}
static void msiDinputRevokeMainSourceIdentity(const char* reason){trace.push_back(std::string("revoke:")+reason);}
namespace Original{
${oldRelease}
static void guard(){const std::uint64_t now=fakeNow;
${oldGuard}
 trace.push_back("fallthrough");}
}
namespace Changed{
${newRelease}
static void guard(){const std::uint64_t now=fakeNow;
${newGuard}
 trace.push_back("fallthrough");}
}
struct Config{unsigned mask=0,attempts=0;bool gate=true,ready=true,mock=false,active=true,hasPad=true,due=false,acquireFails=false,diagFail=false;bool mutateBetweenReads=false;};
static void seed(const Config& c){
 trace.clear();rows.clear();stores=hardwareCalls=staleCalls=exhaustedCalls=dueCalls=0;diagnosticThrows=c.diagFail;
 g_aiFanMockSession.isolated=c.mock;
 g_hardwareWriteGate={"hardwareGate",c.gate};g_powerLifecycle={"lifecycle",c.ready?PowerLifecycle::Ready:PowerLifecycle::Other};
 g_powerGeneration={"power",10+(c.mask&1?1ULL:0ULL)};g_xinputTopologyEpoch={"topology",20+(c.mask&2?1ULL:0ULL)};g_msiFeedbackModeGeneration={"mode",30+(c.mask&4?1ULL:0ULL)};
 if(c.mutateBetweenReads){g_powerGeneration.reads={10,11};g_xinputTopologyEpoch.reads={20,21};g_msiFeedbackModeGeneration.reads={30,31};}
 pad={};pad.result=c.acquireFails?-1:0;g_msiDinput={};g_msiDinput.pad=c.hasPad?&pad:nullptr;g_msiDinput.productId={"product",0x1901};g_msiDinput.mainInterfaceMatched={"matched",true};
 g_msiDinput.inputLostRecovery.begin(fakeNow,10,20,30);g_msiDinput.inputLostRecovery.active=c.active;g_msiDinput.inputLostRecovery.attempts=c.attempts;g_msiDinput.inputLostRecovery.nextDue=c.due?4999:5999;
}
static Json business(){std::vector<Json> ordinary=rows;for(auto& row:ordinary)row.erase("reacquireCancel");const auto&r=g_msiDinput.inputLostRecovery;
 return {{"trace",trace},{"rows",ordinary},{"staleCalls",staleCalls},{"exhaustedCalls",exhaustedCalls},{"dueCalls",dueCalls},{"hardwareCalls",hardwareCalls},
 {"powerReads",g_powerGeneration.count},{"topologyReads",g_xinputTopologyEpoch.count},{"modeReads",g_msiFeedbackModeGeneration.count},
 {"gateReads",g_hardwareWriteGate.count},{"lifecycleReads",g_powerLifecycle.count},{"stores",stores},
 {"padPresent",g_msiDinput.pad!=nullptr},{"acquire",pad.acquire},{"unacquire",pad.unacquire},{"release",pad.release},
 {"product",g_msiDinput.productId.value},{"matched",g_msiDinput.mainInterfaceMatched.value},{"openTried",g_msiDinput.openTried},{"lastFailAt",g_msiDinput.lastFailAt},{"backoffMs",g_msiDinput.backoffMs},
 {"notAcquiredRetries",g_msiDinput.notAcquiredRetries},{"notAcquiredLogged",g_msiDinput.notAcquiredLogged},{"episode",{{"active",r.active},{"attempts",r.attempts},{"nextDue",r.nextDue},{"power",r.power},{"topology",r.topology},{"mode",r.mode}}}};}
static unsigned cases=0,checks=0;
static void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
static void compare(const Config& c){
 ++cases;seed(c);Original::guard();const auto original=business();seed(c);Changed::guard();const auto changed=business();
 if(original!=changed)throw std::runtime_error("original/changed actual snippet differs:\n"+original.dump()+"\n"+changed.dump());++checks;
 const bool stale=c.mask!=0,hw=!c.mock&&c.gate&&c.ready,exhausted=c.attempts>=ymcc::msi::AcquireRecovery::budget;
 const bool cancelled=c.active&&(stale||!hw||exhausted);
 check(g_powerGeneration.count==(!c.active?0:cancelled?1:2),"original generation read count retained");
 if(!cancelled){for(const auto&row:rows)check(!row.contains("reacquireCancel"),"non-cancel does not invent cancel evidence");return;}
 check(staleCalls==1&&dueCalls==0&&pad.acquire==0,"cancel stays before due/Acquire");
 check(hardwareCalls==(stale?0u:1u),"hardware outer short circuit");
 check(exhaustedCalls==((!stale&&hw)?2u:1u),"guard exhaustion and separate legacy label call counts");
 check(g_msiDinput.openTried&&g_msiDinput.lastFailAt==fakeNow&&g_msiDinput.backoffMs==kMsiDinputRebindBackoffMs,"legacy backoff and timing retained");
 check(!g_msiDinput.inputLostRecovery.active&&g_msiDinput.inputLostRecovery.attempts==0,"actual release still resets episode");
 if(!c.hasPad){check(rows.empty(),"no-pad release still has no log");return;}
 check(rows.size()==1&&rows[0]["phase"]=="pad-released","no extra log I/O row");
 check(rows[0]["reason"]==(exhausted?"input-lost-reacquire-exhausted":"input-lost-reacquire-power-changed"),"legacy reason unchanged");
 check(rows[0]["bindCount"]==42&&rows[0]["openTried"]==false,"ordinary row fields still observed at same release point");
 if(c.diagFail){check(!rows[0].contains("reacquireCancel"),"diagnostic failure caught and ordinary row preserved");return;}
 const auto&e=rows[0].at("reacquireCancel");
 check(e["reasonIsLegacyLabel"]==true&&e["controlAuthority"]==false,"legacy label not cause/control");
 check(e["generationReadsAreAtomicTogether"]==false,"generation reads explicitly non-atomic snapshot");
 check(e["mismatchMask"]==c.mask,"all generation mismatches recorded including multiple changes");
 check(e["staleComparisonReachedMask"]==((c.mask&1)?1:(c.mask&2)?3:7),"derived stale comparison reach matches unchanged internal short circuit");
 check(e["stale"]["checked"]==true&&e["stale"]["result"]==stale,"actual stale result");
 check(e["hardwareWriteAllowed"]["checked"]==!stale,"hardware checked state");
 check(stale?e["hardwareWriteAllowed"]["result"].is_null():e["hardwareWriteAllowed"]["result"]==hw,"unchecked hardware is unknown");
 const bool guardChecked=!stale&&hw;
 check(e["guardExhaustion"]["checked"]==guardChecked,"guard exhaustion checked state");
 check(guardChecked?e["guardExhaustion"]["result"]==exhausted:e["guardExhaustion"]["result"].is_null(),"unchecked guard exhaustion unknown even when legacy label evaluates it");
 check(e["legacyLabelExhaustion"]["checked"]==true&&e["legacyLabelExhaustion"]["result"]==exhausted,"legacy label recheck independently recorded");
 check(e["guardFirstTrue"]==(stale?"stale":!hw?"hardware-write-not-allowed":"exhaustion"),"observed first true guard branch");
 check(e["episodeBeforeReleaseReset"]["attempts"]==c.attempts&&e["episodeBeforeReleaseReset"]["power"]==10&&e["episodeBeforeReleaseReset"]["topology"]==20&&e["episodeBeforeReleaseReset"]["mode"]==30&&e["episodeBeforeReleaseReset"]["bindCount"]==42&&e["episodeBeforeReleaseReset"]["nextDue"]==5999,"episode before release reset captured");
 unsigned ordinal=0;
 for(const auto&call:trace){if(call=="stale")break;for(const auto*key:{"power","topology","mode"})if(call==std::string("read:")+key){++ordinal;const auto&g=e["generationReads"][key];check(g["checked"]==true&&g["readOrdinal"]==ordinal,"recorded argument-read order actual, not imposed");const auto value=std::strcmp(key,"power")==0?10+(c.mask&1?1:0):std::strcmp(key,"topology")==0?20+(c.mask&2?1:0):30+(c.mask&4?1:0);check(g["value"]==value,"recorded generation is original argument value");}}
 check(ordinal==3,"exactly three guard generation reads");
}
static Json example;
int main(){try{
 for(unsigned mask=0;mask<8;++mask)for(unsigned attempts=0;attempts<=10;++attempts)for(unsigned hardware=0;hardware<6;++hardware)for(bool diagnostic:{false,true}){
  Config c;c.mask=mask;c.attempts=attempts;c.gate=hardware!=1&&hardware!=5;c.ready=hardware!=2&&hardware!=5;c.mock=hardware==3||hardware==4;c.diagFail=diagnostic;compare(c);
  if(mask==2&&attempts==0&&hardware==0&&!diagnostic)example=rows[0];
 }
 for(unsigned mask=0;mask<8;++mask){Config c;c.mask=mask;c.active=false;compare(c);}
 for(bool acquireFail:{false,true}){Config c;c.due=true;c.acquireFails=acquireFail;compare(c);}
 {Config c;c.mutateBetweenReads=true;compare(c);check(pad.acquire==0&&dueCalls==1,"actual due recheck handles generation drift without extra diagnostic reads");}
 for(unsigned mask:{1,2,4,7}){Config c;c.mask=mask;c.hasPad=false;compare(c);}
 for(const char* reason:{"manual-release","disabled","input-host-stop","reacquire-generation-changed"})for(bool hasPad:{false,true}){
  ++cases;Config c;c.hasPad=hasPad;seed(c);Original::msiDinputReleasePad(reason);auto original=business();seed(c);Changed::msiDinputReleasePad(reason);check(original==business(),"ordinary release fields/path/calls identical");for(const auto&row:rows)check(!row.contains("reacquireCancel"),"ordinary release no new evidence");}
 ymcc::msi::AcquireRecovery r;r.begin(1,1,2,3);ymcc::msi::ReacquireCancelEvidence empty(r,9);const auto nulls=empty.fields();
 check(nulls["mismatchMask"].is_null()&&nulls["stale"]["result"].is_null()&&nulls["guardExhaustion"]["result"].is_null()&&nulls["generationReads"]["power"]["value"].is_null(),"unobserved defaults remain unknown");
 std::cout<<Json{{"passed",true},{"cases",cases},{"assertions",checks},{"topologyOnlyExample",example},{"productExecuted",false},{"realApiCalls",0},{"scope","Actual original/changed cancel guard and entire release function extracted; unchanged AcquireRecovery methods delegated through trace wrappers; fake atomic reads, pad and memory log sink"}}.dump()<<'\n';return 0;
 }catch(const std::exception&ex){std::cout<<Json{{"passed",false},{"cases",cases},{"assertions",checks},{"failure",ex.what()}}.dump()<<'\n';return 1;}}
`;
fs.writeFileSync(out+'/actual-guard-release.cpp',cpp);
const dependency=root+'/deps/json';fs.mkdirSync(dependency,{recursive:true});if(!fs.existsSync(dependency+'/json.hpp'))fs.copyFileSync('G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/deps/json/json.hpp',dependency+'/json.hpp');
const command='@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"'+repo+'/native" /I"'+dependency+'" "'+out+'/actual-guard-release.cpp" /Fo"'+out+'/actual-guard-release.obj" /Fe"'+out+'/actual-guard-release-synthetic.exe"\r\nexit /b %errorlevel%\r\n';
fs.writeFileSync(out+'/compile-synthetic.cmd',command);
const opts={encoding:'utf8',timeout:60000,windowsHide:true};const build=spawnSync('cmd.exe',['/d','/c',out+'/compile-synthetic.cmd'],opts);fs.writeFileSync(out+'/build.log',(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,'synthetic compile; inspect '+out+'/build.log');
const run=spawnSync(out+'/actual-guard-release-synthetic.exe',[],opts);fs.writeFileSync(out+'/result.json',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,'synthetic test; inspect '+out+'/result.json');
const hash=b=>createHash('sha256').update(b).digest('hex').toUpperCase();const binding={originalSource:beforePath,originalSha256:hash(fs.readFileSync(beforePath)),changedSource:afterPath,changedSha256:hash(fs.readFileSync(afterPath)),originalGuardSha256:hash(oldGuard),changedGuardSha256:hash(newGuard),originalReleaseSha256:hash(oldRelease),changedReleaseSha256:hash(newRelease),helperSha256:hash(fs.readFileSync(repo+'/native/msi_reacquire_cancel_evidence.h')),recoverySha256:hash(fs.readFileSync(repo+'/native/msi_dinput_acquire_recovery.h')),generatedFixtureSha256:hash(cpp),realApiCalls:0,productExecuted:false};
fs.writeFileSync(out+'/source-test-binding.json',JSON.stringify(binding,null,2));
const result=JSON.parse(run.stdout.trim());console.log(JSON.stringify({passed:result.passed,cases:result.cases,assertions:result.assertions,realApiCalls:0}));