// Generate a differential fixture from actual production definitions. No product
// entrypoint, Windows libraries or device APIs are linked into this runner.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const [referencePath,candidatePath,outputPath]=process.argv.slice(2);
if(!referencePath||!candidatePath||!outputPath)throw Error('usage: node verify-msi-terminal-return-evidence.cjs reference-main.cpp candidate-main.cpp output-directory');
const out=path.resolve(outputPath),repo=path.resolve(__dirname,'..');fs.mkdirSync(out,{recursive:true});
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function endBrace(t,start){let depth=0,mode='code';for(let i=start;i<t.length;i++){const c=t[i],n=t[i+1];if(mode==='line'){if(c==='\n')mode='code';continue;}if(mode==='block'){if(c==='*'&&n==='/'){mode='code';i++;}continue;}if(mode==='string'||mode==='char'){if(c==='\\'){i++;continue;}if(c===(mode==='string'?'"':"'"))mode='code';continue;}if(c==='/'&&n==='/'){mode='line';i++;continue;}if(c==='/'&&n==='*'){mode='block';i++;continue;}if(c==='"'||c==="'"){mode=c==='"'?'string':'char';continue;}if(c==='{')depth++;else if(c==='}'&&--depth===0)return i+1;}throw Error('unclosed production function');}
function extract(t,name){const re=new RegExp('^static[^\\n;{]*\\b'+name+'\\s*\\(','mg');let m;while((m=re.exec(t))){const start=m.index,brace=t.indexOf('{',start),semi=t.indexOf(';',start);if(brace<0||(semi>=0&&semi<brace))continue;const end=endBrace(t,brace);return{name,line:t.slice(0,start).split('\n').length,body:t.slice(start,end),sha256:hash(Buffer.from(t.slice(start,end)))};}throw Error('missing definition '+name);}
const names=['msiTerminalReturnMissingTransport','msiTerminalReturnFacts','msiTerminalReturnNote','msiTerminalReturnNow','msiTerminalReturnSchedule'];
const frozen={};let cpp=`#include <atomic>\n#include <mutex>\n#include <iostream>\n#include <fstream>\n#include <stdexcept>\n#include <vector>\n#include "json.hpp"\n#include "msi_terminal_return_plan.h"\nusing json=nlohmann::json;using ULONGLONG=unsigned long long;\n`;
const common=`
enum class MsiClawIoState{NoDevice,StartFailed,Completed};
struct Diag{bool enumerationComplete=false,deviceFound=false;unsigned enumerationError=0,vendorMatches=0,openFailures=0,lastError=0;};
struct Write{bool attempted=false,ok=false,timedOut=false,handleRetained=false;unsigned written=0,lastError=0;std::uint64_t txSeq=0;MsiClawIoState state=MsiClawIoState::NoDevice;};
struct MsiClawCommandOutcome{Diag diag;Write write;};
std::atomic<bool> g_msiDinputRouteDinputArmed=false,g_msiDinputReturnOwed=true,g_msiTerminalReturnPending=false;
std::atomic<std::uint64_t> g_msiFeedbackModeGeneration=9;
struct Target{bool enabled=true,virtualTargetEnabled=false;}g_realStickTest;
void* g_inputHostProcess=nullptr;bool g_inputHostPrepared=false;
bool knownBoard=true,exitFence=false,powerReady=true;std::uint64_t power=7;
ymcc::msi::terminalReturn::Plan g_msiTerminalReturnPlan;std::mutex g_msiTerminalReturnMx;
std::string g_msiTerminalReturnOwner="previous-plan-owner";unsigned long long g_msiTerminalReturnEpoch=2;
std::vector<json> logs;
bool msiClawKnownBoardAuthority(){return knownBoard;}bool inputHostExitFenceRaised(){return exitFence;}bool hardwareWriteAllowed(){return powerReady;}
std::uint64_t currentPowerGeneration(){return power;}std::string inputHostOwnerToken(){return "current-call-owner";}unsigned long long inputHostEpochToken(){return 4;}
ULONGLONG GetTickCount64(){return 100000;}
const char* msiClawIoStateName(MsiClawIoState s){return s==MsiClawIoState::NoDevice?"no-device":s==MsiClawIoState::StartFailed?"start-failed":"completed";}
void appendNativeLifecycleLog(const char* event,const json& row){json r=row;r["event"]=event;logs.push_back(r);}
`;
const support=`
void seed(unsigned context){g_msiTerminalReturnPlan=ymcc::msi::terminalReturn::Plan{};g_msiTerminalReturnOwner="previous-plan-owner";g_msiTerminalReturnEpoch=2;logs.clear();g_msiTerminalReturnPending=false;
 knownBoard=true;exitFence=false;powerReady=true;power=7;g_msiFeedbackModeGeneration=9;g_msiDinputReturnOwed=true;g_msiDinputRouteDinputArmed=false;g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=false;g_inputHostProcess=nullptr;g_inputHostPrepared=false;
 switch(context){case 1:knownBoard=false;break;case 2:exitFence=true;break;case 3:powerReady=false;break;case 4:g_realStickTest.virtualTargetEnabled=true;break;case 5:g_msiDinputRouteDinputArmed=true;break;case 6:g_msiDinputReturnOwed=false;break;case 7:power=0;break;case 8:g_msiFeedbackModeGeneration=0;break;case 9:g_inputHostPrepared=true;break;case 10:g_msiTerminalReturnPlan.begin(99000,msiTerminalReturnFacts());g_msiTerminalReturnPending=true;break;}
}
json state(){return{{"active",g_msiTerminalReturnPlan.active()},{"pending",g_msiTerminalReturnPending.load()},{"deadline",g_msiTerminalReturnPlan.deadline()},{"started",g_msiTerminalReturnPlan.startedAt()},{"next",g_msiTerminalReturnPlan.nextProbeAt()},{"probes",g_msiTerminalReturnPlan.probes()},{"power",g_msiTerminalReturnPlan.power()},{"mode",g_msiTerminalReturnPlan.mode()},{"owner",g_msiTerminalReturnOwner},{"epoch",g_msiTerminalReturnEpoch},{"returnOwed",g_msiDinputReturnOwed.load()},{"routeArmed",g_msiDinputRouteDinputArmed.load()}};}
`;
for(const [label,file] of [['reference',referencePath],['candidate',candidatePath]]){const buffer=fs.readFileSync(file),text=buffer.toString('utf8'),definitions=names.map(n=>extract(text,n));if(label==='candidate')definitions.splice(3,0,extract(text,'msiTerminalReturnEligibilityNote'));frozen[label]={path:path.resolve(file),sha256:hash(buffer),functions:definitions.map(({body,...metadata})=>metadata)};const inc=common+definitions.map(x=>x.body).join('\n\n')+support;fs.writeFileSync(path.join(out,label+'-production.inc'),inc);cpp+=`namespace ${label}{\n#include "${label}-production.inc"\n}\n`;}
const plan=fs.readFileSync(path.join(repo,'native/msi_terminal_return_plan.h')),jsonHeader=fs.readFileSync(path.join(repo,'deps/json/json.hpp'));
fs.writeFileSync(path.join(out,'msi_terminal_return_plan.h'),plan);fs.writeFileSync(path.join(out,'json.hpp'),jsonHeader);
cpp+=`
unsigned checks=0;void check(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
int main(int argc,char**argv){try{
 // Each combination is compared against the pre-change actual production function.
 unsigned comparisons=0;
 for(unsigned mask=0;mask<1024;++mask)for(unsigned state=0;state<3;++state)for(unsigned context=0;context<11;++context)for(unsigned reasonCase=0;reasonCase<3;++reasonCase){
  reference::seed(context);candidate::seed(context);reference::MsiClawCommandOutcome a;candidate::MsiClawCommandOutcome b;
  a.diag.enumerationComplete=(mask&1)!=0;a.diag.deviceFound=(mask&2)!=0;a.diag.vendorMatches=mask&4?2:0;a.diag.openFailures=mask&8?1:0;a.diag.lastError=mask&16?5:0;a.diag.enumerationError=mask&32?5:0;a.write.attempted=(mask&64)!=0;a.write.ok=(mask&128)!=0;a.write.timedOut=(mask&256)!=0;a.write.handleRetained=(mask&512)!=0;a.write.written=a.write.ok?64:0;a.write.lastError=state==1?433:0;a.write.state=static_cast<reference::MsiClawIoState>(state);a.write.txSeq=123;
  b.diag.enumerationComplete=a.diag.enumerationComplete;b.diag.deviceFound=a.diag.deviceFound;b.diag.vendorMatches=a.diag.vendorMatches;b.diag.openFailures=a.diag.openFailures;b.diag.lastError=a.diag.lastError;b.diag.enumerationError=a.diag.enumerationError;b.write.attempted=a.write.attempted;b.write.ok=a.write.ok;b.write.timedOut=a.write.timedOut;b.write.handleRetained=a.write.handleRetained;b.write.written=a.write.written;b.write.lastError=a.write.lastError;b.write.state=static_cast<candidate::MsiClawIoState>(state);b.write.txSeq=123;
  const char* reason=reasonCase==0?"input-host-stop":reasonCase==1?"dinput-disabled":nullptr;
  reference::msiTerminalReturnSchedule(reason,a);candidate::msiTerminalReturnSchedule(reason,b);check(reference::state()==candidate::state(),"return plan/control state diverged");++comparisons;
 }
 candidate::seed(0);candidate::MsiClawCommandOutcome actual;actual.diag.deviceFound=true;actual.diag.vendorMatches=2;actual.write.attempted=true;actual.write.lastError=433;actual.write.txSeq=172;actual.write.state=candidate::MsiClawIoState::StartFailed;
 candidate::msiTerminalReturnSchedule("input-host-stop",actual);check(!candidate::g_msiTerminalReturnPlan.active(),"433 unexpectedly scheduled retry");check(candidate::logs.size()==1,"433 lacks one eligibility receipt");const auto actualRow=candidate::logs.front();check(actualRow["transportEligibleForDeferredDiscovery"]==false&&actualRow["eligibilityBasis"]=="actual-write-already-attempted", "433 lacks reason for rejection");check(actualRow["writeError"]==433&&actualRow["txSeq"]==172,"433 error or transaction lost");check(actualRow["physicalReturnVerifiedKnown"]==false,"unknown physical return reported as observed");check(actualRow["identityScope"]=="current-call-context-not-saved-return-plan-or-original-stop","stale plan owner attributed to actual result");check(candidate::g_msiDinputReturnOwed.load(),"433 clears return debt");
 candidate::seed(0);candidate::MsiClawCommandOutcome absent;absent.diag.enumerationComplete=true;candidate::msiTerminalReturnSchedule("input-host-stop",absent);check(candidate::logs.size()==2,"eligible case lost plan receipt");const auto eligibleRow=candidate::logs.front(),planRow=candidate::logs.back();check(eligibleRow["transportEligibleForDeferredDiscovery"]==true&&planRow["phase"]=="scheduled","missing pre-write transport cannot schedule");check(planRow["physicalReturnVerified"]==false&&planRow["physicalReturnVerifiedKnown"]==false&&planRow["physicalReturnObservation"]=="not-observed-in-transport-plan-row","literal false telemetry lacks unknown marker");check(planRow["writeError"]==0&&planRow["txSeq"]==0,"plan receipt lacks write identity");
 candidate::seed(0);actual.write.timedOut=true;actual.write.handleRetained=true;candidate::msiTerminalReturnNote("write-finished","fixture-only",&actual);check(candidate::logs.back()["ioTimedOut"]==true&&candidate::logs.back()["ioHandleRetained"]==true,"ambiguous write completion telemetry lost");
 json result={{"passed",true},{"differentialCases",comparisons},{"assertions",checks},{"actual433Receipt",actualRow},{"eligibleReceipt",eligibleRow},{"planReceipt",planRow},{"productionEntrypointExecuted",false},{"hostExecuted",false},{"deviceApisLinkedOrCalled",false},{"limitations","Only extracted scheduling/telemetry functions with real Plan and fake globals/log sink;not full stop lifecycle,write completion safety,hardware recovery or Steam consumption."}};
 if(argc>1)std::ofstream(argv[1])<<result.dump(2)<<"\\n";std::cout<<result.dump()<<"\\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\\n";return 1;}}
`;
fs.writeFileSync(path.join(out,'terminal-return-differential.cpp'),cpp);
fs.writeFileSync(path.join(out,'fixture-source-binding.json'),JSON.stringify({createdAt:new Date().toISOString(),...frozen,planSha256:hash(plan),jsonHeaderSha256:hash(jsonHeader),runnerSha256:hash(Buffer.from(cpp)),scope:'actual production functions extracted; fake globals/log sink; real pure Plan; no product/Host/device APIs'},null,2));
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
fs.writeFileSync(path.join(out,'compile-differential.cmd'),`@echo off\r\ncall "${vc}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 terminal-return-differential.cpp /Foterminal-return-differential.obj /Feterminal-return-differential.exe /Fdterminal-return-differential.pdb\r\nexit /b %errorlevel%\r\n`);
console.log(JSON.stringify({generated:out,reference:frozen.reference.sha256,candidate:frozen.candidate.sha256}));
