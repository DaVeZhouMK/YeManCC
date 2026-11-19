import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {execFile} from 'node:child_process';import {promisify} from 'node:util';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const selection=process.argv.slice(3);if(!([[],['--prepare-only'],['--steam-only'],['--steam-only','--prepare-only']].some(x=>JSON.stringify(x)===JSON.stringify(selection))))throw Error('Invalid radial native selection');
const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2]&&path.resolve(process.argv[2]),run=promisify(execFile);if(!out||fs.existsSync(out))throw Error('Fresh isolated evidence directory required');fs.mkdirSync(out,{recursive:true});
const inputs=['src/radial/availability.ts','native/radial_release.h','native/steam_ui_mode.h','native/sleep_power_state.h','native/main.cpp','tools/radial_utilities_native_selftest.mjs','native/steam_session_observer.h','native/steam_source_owner.h','native/steam_command_coordinator.h','native/steam_command_disposition.h'],sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),sourceHashes=Object.fromEntries(inputs.map(file=>[file,sha(path.join(root,file))]));
const header=fs.readFileSync(path.join(root,'native/steam_ui_mode.h'),'utf8'),main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const common=header.slice(header.indexOf('enum class ClientCommand'),header.indexOf('inline Json set('));assert(common.includes('dispatchClientCommand'));
const stop=header.match(/inline Json stop\([\s\S]*?\n\}/)?.[0],registration=main.match(/    ipc_on\("steam\.ui\.stop",[\s\S]*?\n    \}\);/)?.[0];assert.ok(stop&&registration);assert.match(fs.readFileSync(path.join(root,'native/radial_release.h'),'utf8'),/kUserAvailable\s*=\s*true/);assert.ok(main.includes('cmd == "steam.ui.stop"'));assert.ok(!/taskkill|TerminateProcess/.test(stop));
assert.match(fs.readFileSync(path.join(root,'src/radial/availability.ts'),'utf8'),/RADIAL_USER_AVAILABLE\s*=\s*true/);
const report={pass:false,sourceHashes,realSteamOperations:0,processesLaunched:0,hardwareWrites:0,scope:'Exact production graceful-stop function and actual IPC registrar; process/session and ShellExecute ports are inert C++ functions. No Steam or application process is opened.'};
const powerDefinitions=['captureSharedPowerLifecycle','sharedPowerLifecycleCurrent'].map(name=>{const value=main.match(new RegExp('\\[\\[maybe_unused\\]\\] static bool '+name+'\\([\\s\\S]*?\\n\\}'))?.[0];assert(value,'actual power helper '+name);return value;}).join('\n');
const observerHeader=fs.readFileSync(path.join(root,'native/steam_session_observer.h'),'utf8');
const publicTypes=observerHeader.slice(observerHeader.indexOf('struct Session {'),observerHeader.indexOf('using IdentityPublished'));
const comparator=observerHeader.match(/    bool sameIdentityEpoch\([\s\S]*?\n    \}/)?.[0];assert(publicTypes.includes('struct IdentitySnapshot')&&comparator);
const cpp=String.raw`#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <map>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <vector>
#include <tuple>
#include <sddl.h>
static HANDLE fixtureSourceSlot(LPSECURITY_ATTRIBUTES,BOOL,LPCWSTR){const auto h=CreateEventW(nullptr,TRUE,FALSE,nullptr);SetLastError(h?ERROR_SUCCESS:ERROR_NOT_READY);return h;}
#define CreateMutexW fixtureSourceSlot
#include ${JSON.stringify(path.join(root,'native/steam_source_owner.h').replaceAll('\\','/'))}
#undef CreateMutexW
#include ${JSON.stringify(path.join(root,'native/steam_command_coordinator.h').replaceAll('\\','/'))}
#include ${JSON.stringify(path.join(root,'deps/json/json.hpp').replaceAll('\\','/'))}
using json=nlohmann::json;
#include ${JSON.stringify(path.join(root,'native/sleep_power_state.h').replaceAll('\\','/'))}
#include ${JSON.stringify(path.join(root,'native/radial_release.h').replaceAll('\\','/'))}
namespace fixture {int snapshots=0,presenceReads=0,lookups=0,waits=0,shells=0,closes=0,refreshes=0;int presence=1,replaceAt=0;unsigned epoch=0;bool sourceAllowed=true,tokenKnown=true,tokenSameUser=true,tokenSameSession=true;HANDLE targetToken=nullptr;bool lookup=true,birth=true,live=true,shell=true;std::wstring file,parameters,directory;int show=0;DWORD mask=0;
void reset(){++epoch;sourceAllowed=tokenKnown=tokenSameUser=tokenSameSession=true;targetToken=nullptr;snapshots=presenceReads=lookups=waits=shells=closes=refreshes=0;presence=1;replaceAt=0;lookup=birth=live=shell=true;file=parameters=directory=L"";show=0;mask=0;}}
namespace ymcc::steamsession {
${publicTypes}
// Controlled publication/source-active leaves; public types/comparator and SourceOwner are original.
struct Observer {
 mutable SourceOwner owner;mutable unsigned epoch=~0u;mutable std::shared_ptr<const IdentitySnapshot> publication;
 Observer(){if(!owner.claim())throw std::runtime_error("controlled source slot failed");owner.resumeReads();}
 void publish()const{auto p=std::make_shared<IdentitySnapshot>();p->session.root=L"X:\\inert-steam";p->session.pid=123;p->session.created=456;p->presence=fixture::presence;p->processEpoch=1;p->accountEpoch=1;publication=p;}
 std::shared_ptr<const IdentitySnapshot> identitySnapshot()const{const int n=++fixture::snapshots;if(epoch!=fixture::epoch){epoch=fixture::epoch;owner.resumeReads();publish();}if(fixture::replaceAt==n){auto p=std::make_shared<IdentitySnapshot>(*publication);p->session.created=789;++p->processEpoch;publication=p;}return publication;}
 Session sessionSnapshot(){auto p=identitySnapshot();return p?p->session:Session{};}
 SourceOwner::Read sourceRead(){if(!fixture::sourceAllowed)return {};return owner.read();}
${comparator}
};
inline HANDLE verifiedProcess(DWORD,const std::wstring&,ULONGLONG* birth){++fixture::lookups;*birth=fixture::birth?456:999;return fixture::lookup?reinterpret_cast<HANDLE>(0x555):nullptr;}
}
static BOOL fixtureShell(SHELLEXECUTEINFOW* info){++fixture::shells;fixture::file=info->lpFile;fixture::parameters=info->lpParameters;fixture::directory=info->lpDirectory;fixture::show=info->nShow;fixture::mask=info->fMask;return fixture::shell?TRUE:FALSE;}
static DWORD fixtureWait(HANDLE,DWORD){++fixture::waits;return fixture::live?WAIT_TIMEOUT:WAIT_OBJECT_0;}
static BOOL fixtureClose(HANDLE h){if(h==reinterpret_cast<HANDLE>(0x555)){++fixture::closes;return TRUE;}return CloseHandle(h);}
static BOOL fixtureToken(HANDLE process,DWORD access,PHANDLE token){if(!fixture::tokenKnown)return FALSE;const auto ok=OpenProcessToken(GetCurrentProcess(),access,token);if(process==reinterpret_cast<HANDLE>(0x555))fixture::targetToken=*token;return ok;}
static BOOL fixtureTokenInfo(HANDLE token,TOKEN_INFORMATION_CLASS kind,LPVOID out,DWORD bytes,PDWORD used){
 const auto ok=GetTokenInformation(token,kind,out,bytes,used);if(!ok||token!=fixture::targetToken)return ok;
 if(kind==TokenSessionId&&!fixture::tokenSameSession)++*static_cast<DWORD*>(out);
 if(kind==TokenUser&&!fixture::tokenSameUser){auto* user=static_cast<TOKEN_USER*>(out);DWORD size=bytes-sizeof(TOKEN_USER);auto* sid=static_cast<BYTE*>(out)+sizeof(TOKEN_USER);if(!CreateWellKnownSid(WinWorldSid,nullptr,sid,&size))return FALSE;user->User.Sid=sid;}
 return ok;
}
#define OpenProcessToken fixtureToken
#define GetTokenInformation fixtureTokenInfo
#define ShellExecuteExW fixtureShell
#define WaitForSingleObject fixtureWait
#define CloseHandle fixtureClose
namespace ymcc::steamui {using Json=nlohmann::json;
${common}
${stop}
}
#undef OpenProcessToken
#undef GetTokenInformation
#undef ShellExecuteExW
#undef WaitForSingleObject
#undef CloseHandle
static std::atomic<bool> g_exitRequested{false};static struct {bool isolated=false;} g_aiFanMockSession;
static ymcc::steamsession::Observer g_steamSettingsObserver;
static uint64_t fixturePowerGeneration=1,fixturePowerPublication=1,fixturePowerSerial=0,fixturePowerSequence=0;
static bool fixturePowerReady=true,fixturePowerKnown=true,fixturePowerPending=false,fixturePowerThrow=false;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,uint32_t bytes){
    if(fixturePowerThrow)throw std::runtime_error("power port failure");
    if(bytes!=sizeof(*out)||!fixturePowerKnown)return 0;
    *out=ymcc::sleeppower::SharedPowerV1{};out->generation=fixturePowerGeneration;out->publicationRevision=fixturePowerPublication;
    out->suspendSerial=fixturePowerSerial;out->observedNotificationSequence=fixturePowerSequence;out->phase=fixturePowerReady?0:3;
    out->flags=ymcc::sleeppower::kKnown|(fixturePowerReady?ymcc::sleeppower::kHardwareAllowed:0)|(fixturePowerPending?ymcc::sleeppower::kNotificationPending:0);return 1;
}
${powerDefinitions}
static struct {void requestRefresh(const ymcc::steamsession::Session&){++fixture::refreshes;}} g_steamModeObserver;
static std::map<std::string,std::function<json(const json&)>> handlers;
static void ipc_on(const char* command,std::function<json(const json&)> fn){handlers[command]=std::move(fn);}
int main(int argc,char** argv){if(argc!=2||std::string(argv[1])!="--steam-only")return 4;ymcc::steamcommand::Coordinator coordinator;ymcc::steamcommand::Provider provider(coordinator);if(!provider)return 3;json checks=json::array();const auto need=[&](bool pass,const std::string& name){checks.push_back({{"name",name},{"pass",pass}});if(!pass)throw std::runtime_error(name);};
try{
need(ymcc::radial::kUserAvailable,"actual release policy remains available");
need(!ymcc::radial::testingEnabled.load()&&!ymcc::radial::userAvailable(),"actual runtime testing admission begins closed");
need(!ymcc::radial::userActionAvailable("radial.summon")&&ymcc::radial::userActionAvailable("window.summon"),"closed testing gate refuses radial only and preserves normal summon");
${registration}
json closedValid={{"pid",uint32_t(123)},{"birth","456"},{"userInitiated",true}};fixture::reset();const auto closed=handlers.at("steam.ui.stop")(closedValid);need(closed["ok"]==false&&closed["available"]==false&&fixture::snapshots==0&&fixture::shells==0,"actual product-disabled Steam endpoint refuses before all native ports");
handlers.clear();
// Explicit in-process opt-in; the actual registrar and policy are used unchanged.
ymcc::radial::setTestingEnabled(true);
need(ymcc::radial::userAvailable()&&ymcc::radial::userActionAvailable("radial.summon"),"public in-process test setter admits the inert original registrar");
${registration}
json valid={{"pid",uint32_t(123)},{"birth","456"},{"userInitiated",true}};ymcc::steamsession::Observer observer;
for(const auto& marker:std::vector<json>{nullptr,false,"true",1,json::object()}){fixture::reset();auto request=valid;request["userInitiated"]=marker;bool rejected=false;try{ymcc::steamui::stop(observer,request,[]{return true;});}catch(const std::runtime_error&){rejected=true;}need(rejected&&fixture::snapshots==0&&fixture::shells==0,"malformed manual marker stops before all native session/process/Shell ports");}
{fixture::reset();auto request=valid;request.erase("userInitiated");bool rejected=false;try{ymcc::steamui::stop(observer,request,[]{return true;});}catch(...){rejected=true;}need(rejected&&fixture::snapshots==0,"legacy stop request never gains an implicit manual marker");}
for(const auto& change:std::vector<json>{json{{"pid",-1}},json{{"pid",123.5}},json{{"pid","123"}},json{{"birth",456}},json{{"exe","other.exe"}},json{{"uri","steam://evil"}}}){fixture::reset();auto request=valid;request.update(change);bool rejected=false;try{ymcc::steamui::stop(observer,request,[]{return true;});}catch(...){rejected=true;}need(rejected&&fixture::snapshots==0&&fixture::shells==0,"malformed or arbitrary target rejected before native ports");}
for(const auto& [name,pid,birth]:std::vector<std::tuple<std::string,uint64_t,std::string>>{{"other PID",124,"456"},{"PID reuse",123,"789"},{"uint32 wraparound",uint64_t{0x100000000ULL+123},"456"}}){fixture::reset();auto request=valid;request["pid"]=pid;request["birth"]=birth;auto result=ymcc::steamui::stop(observer,request,[]{return true;});need(result["ok"]==false&&fixture::lookups==0&&fixture::shells==0,name+" cannot target the current client");}
for(const int presence:{-1,0}){fixture::reset();fixture::presence=presence;auto result=ymcc::steamui::stop(observer,valid,[]{return true;});need(result["ok"]==false&&fixture::lookups==0&&fixture::shells==0,"unknown/stopped presence never launches anything");}
{fixture::reset();fixture::lookup=false;auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(r["ok"]==false&&fixture::shells==0&&fixture::closes==0,"unverified image/session cannot dispatch");}
{fixture::reset();fixture::birth=false;auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(r["ok"]==false&&fixture::shells==0&&fixture::closes==1,"process-birth mismatch closes only the inert admitted handle");}
{fixture::reset();fixture::replaceAt=2;auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(r["ok"]==false&&fixture::shells==0&&fixture::closes==1,"changed observer identity is checked again immediately before Shell");}
{fixture::reset();fixture::live=false;auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(r["ok"]==false&&fixture::shells==0&&fixture::closes==1,"already exited original process cannot authorize a dispatch");}
{fixture::reset();auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(r["ok"]==true&&fixture::shells==1&&fixture::closes==1,"one original verified-session command is submitted");need(fixture::file==L"X:\\inert-steam\\steam.exe"&&fixture::parameters==L"-shutdown"&&fixture::directory==L"X:\\inert-steam","only the fixed verified-client executable and graceful shutdown flag reach Shell");need(fixture::show==SW_HIDE&&(fixture::mask&SEE_MASK_FLAG_NO_UI),"request has no new visible console/error UI");need(r["requestedState"]=="stopped"&&!r.contains("exited")&&!r.value("applied",false),"submission never claims an actual process exit receipt");}
{fixture::reset();fixture::shell=false;auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(r["ok"]==false&&r["reason"]=="steam-exit-command-failed"&&fixture::closes==1,"failed Shell acceptance is retained without kill fallback");}
{fixture::reset();g_exitRequested=true;bool rejected=false;try{handlers.at("steam.ui.stop")(valid);}catch(...){rejected=true;}g_exitRequested=false;need(rejected&&fixture::snapshots==0,"actual registrar rejects during application exit");}
{fixture::reset();g_aiFanMockSession.isolated=true;bool rejected=false;try{handlers.at("steam.ui.stop")(valid);}catch(...){rejected=true;}g_aiFanMockSession.isolated=false;need(rejected&&fixture::snapshots==0,"actual registrar refuses isolated hardware-test mode");}
{fixture::reset();const auto r=handlers.at("steam.ui.stop")(valid);need(r["ok"]==true&&fixture::shells==1&&fixture::refreshes==1,"actual IPC registrar forwards once and requests observation only");}
// Directed Steam-only continuation of the original permission assertions.
for(int kind=0;kind<3;++kind){fixture::reset();fixture::tokenKnown=kind!=0;fixture::tokenSameUser=kind!=1;fixture::tokenSameSession=kind!=2;const auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(!r.value("ok",false)&&fixture::shells==0&&fixture::closes==1,"unknown or mismatched user/logon token cannot authorize Steam stop");}
{fixture::reset();fixture::sourceAllowed=false;const auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(!r.value("ok",false)&&fixture::lookups==0&&fixture::shells==0,"unavailable original SourceRead refuses before process query");}
{fixture::reset();int calls=0;const auto r=ymcc::steamui::stop(observer,valid,[&]{if(++calls==2)observer.owner.suspendReads();return true;});need(!r.value("ok",false)&&fixture::shells==0,"SourceOwner view revoked before final dispatch cannot authorize Shell");}
{fixture::reset();const auto r=ymcc::steamui::stop(observer,valid,[]{return false;});need(!r.value("ok",false)&&fixture::lookups==0&&fixture::shells==0,"retired original current predicate refuses process and Shell");}
for(int kind=0;kind<4;++kind){fixture::reset();fixturePowerKnown=kind!=0;fixturePowerReady=kind!=1;fixturePowerPending=kind==2;fixturePowerThrow=kind==3;const auto r=handlers.at("steam.ui.stop")(valid);need(!r.value("ok",false)&&fixture::snapshots==0&&fixture::shells==0,"actual registrar refuses unknown suspended pending or throwing power read");fixturePowerKnown=fixturePowerReady=true;fixturePowerPending=fixturePowerThrow=false;}
{fixture::reset();ymcc::steamcommand::Scope held({123,456,L"X:\\inert-steam"});const auto r=ymcc::steamui::stop(observer,valid,[]{return true;});need(!r.value("ok",false)&&r.value("reason",std::string{})=="steam-command-busy"&&fixture::lookups==0&&fixture::shells==0,"ordinary parent Provider preserves typed busy without a second process read");}
ymcc::radial::setTestingEnabled(false);need(!ymcc::radial::userAvailable(),"public test setter restores closed runtime admission");
handlers.clear();

${registration}
{fixture::reset();const auto r=handlers.at("steam.ui.stop")(valid);need(r["ok"]==false&&r["available"]==false&&fixture::snapshots==0&&fixture::lookups==0&&fixture::shells==0,"revoked testing admission refuses before all original native ports");}
std::cout<<json{{"pass",true},{"checks",checks},{"count",checks.size()},{"realSteamOperations",0}}.dump();return 0;
}catch(const std::exception& error){std::cout<<json{{"pass",false},{"checks",checks},{"error",error.what()}}.dump();return 2;}}
`;
if(selection.includes('--prepare-only')){fs.writeFileSync(path.join(out,'native-consumer.cpp'),cpp,{flag:'wx'});fs.writeFileSync(path.join(out,'PREPARED.json'),JSON.stringify({status:'PREPARED_ONLY',sourceHashes,commonExtracted:true,assertionsPreserved:true,observerReplacementMovedAfterAdditionalPurePrecheck:true},null,2),{flag:'wx'});process.exit(0);}
try{
 fs.writeFileSync(path.join(out,'native-consumer.cpp'),cpp);const q=value=>'"'+value+'"';
 fs.writeFileSync(path.join(out,'compile.rsp'),['/nologo','/std:c++20','/utf-8','/EHsc','/O1','/MT',q(path.join(out,'native-consumer.cpp')),`/Fo${q(path.join(out,'native.obj'))}`,`/Fe${q(path.join(out,'native-consumer.exe'))}`].join('\r\n'));
 fs.writeFileSync(path.join(out,'compile.cmd'),calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'cl.exe @compile.rsp\r\nexit /b %errorlevel%\r\n');
 try{const result=await run(process.env.ComSpec||'cmd.exe',['/d','/c','compile.cmd'],{cwd:out,windowsHide:true,timeout:70000,maxBuffer:2000000});fs.writeFileSync(path.join(out,'compile.log'),result.stdout+result.stderr);}catch(error){fs.writeFileSync(path.join(out,'compile.log'),String(error.stdout||'')+String(error.stderr||''));throw error;}
 let executed;try{executed=await run(path.join(out,'native-consumer.exe'),['--steam-only'],{cwd:out,windowsHide:true,timeout:10000,maxBuffer:2000000});}catch(error){fs.writeFileSync(path.join(out,'native-result.json'),String(error.stdout||''));fs.writeFileSync(path.join(out,'native-stderr.log'),String(error.stderr||''));throw error;}fs.writeFileSync(path.join(out,'native-result.json'),executed.stdout);fs.writeFileSync(path.join(out,'native-stderr.log'),executed.stderr);const result=JSON.parse(executed.stdout);assert.ok(result.pass);report.checks=result.count;report.caseNames=result.checks.map(item=>item.name);report.pass=true;
}catch(error){report.error=error.stack;process.exitCode=2;}
finally{const receipt=path.join(out,'native-result.json');if(fs.existsSync(receipt)){try{report.nativeResult=JSON.parse(fs.readFileSync(receipt,'utf8'));}catch{}}fs.writeFileSync(path.join(out,'verification.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({pass:report.pass,checks:report.checks,error:report.error,out}));}
