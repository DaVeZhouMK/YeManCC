import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {execFile} from 'node:child_process';import {promisify} from 'node:util';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2]&&path.resolve(process.argv[2]),run=promisify(execFile);if(!out||fs.existsSync(out))throw Error('Fresh isolated evidence directory required');fs.mkdirSync(out,{recursive:true});
const inputs=['src/radial/availability.ts','native/radial_release.h','native/steam_ui_mode.h','native/main.cpp','tools/radial_utilities_native_selftest.mjs'],sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),sourceHashes=Object.fromEntries(inputs.map(file=>[file,sha(path.join(root,file))]));
const header=fs.readFileSync(path.join(root,'native/steam_ui_mode.h'),'utf8'),main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const stop=header.match(/inline Json stop\([\s\S]*?\n\}/)?.[0],registration=main.match(/    ipc_on\("steam\.ui\.stop",[\s\S]*?\n    \}\);/)?.[0];assert.ok(stop&&registration);assert.match(fs.readFileSync(path.join(root,'native/radial_release.h'),'utf8'),/kUserAvailable\s*=\s*true/);assert.ok(main.includes('cmd == "steam.ui.stop"'));assert.ok(!/taskkill|TerminateProcess/.test(stop));
assert.match(fs.readFileSync(path.join(root,'src/radial/availability.ts'),'utf8'),/RADIAL_USER_AVAILABLE\s*=\s*true/);
const report={pass:false,sourceHashes,realSteamOperations:0,processesLaunched:0,hardwareWrites:0,scope:'Exact production graceful-stop function and actual IPC registrar; process/session and ShellExecute ports are inert C++ functions. No Steam or application process is opened.'};
const cpp=String.raw`#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <map>
#include <functional>
#include <iostream>
#include <stdexcept>
#include ${JSON.stringify(path.join(root,'deps/json/json.hpp').replaceAll('\\','/'))}
using json=nlohmann::json;
#include ${JSON.stringify(path.join(root,'native/radial_release.h').replaceAll('\\','/'))}
namespace fixture {int snapshots=0,presenceReads=0,lookups=0,waits=0,shells=0,closes=0,refreshes=0;int presence=1,replaceAt=0;bool lookup=true,birth=true,live=true,shell=true;std::wstring file,parameters,directory;int show=0;DWORD mask=0;
void reset(){snapshots=presenceReads=lookups=waits=shells=closes=refreshes=0;presence=1;replaceAt=0;lookup=birth=live=shell=true;file=parameters=directory=L"";show=0;mask=0;}}
namespace ymcc::steamsession {
struct Session {std::wstring root;DWORD pid;ULONGLONG created;bool operator==(const Session&)const=default;};
struct Observer {Session sessionSnapshot(){const int n=++fixture::snapshots;return {L"X:\\inert-steam",123,fixture::replaceAt==n?789ULL:456ULL};}int steamPresence(){++fixture::presenceReads;return fixture::presence;}};
inline HANDLE verifiedProcess(DWORD,const std::wstring&,ULONGLONG* birth){++fixture::lookups;*birth=fixture::birth?456:999;return fixture::lookup?reinterpret_cast<HANDLE>(0x555):nullptr;}
}
static BOOL fixtureShell(SHELLEXECUTEINFOW* info){++fixture::shells;fixture::file=info->lpFile;fixture::parameters=info->lpParameters;fixture::directory=info->lpDirectory;fixture::show=info->nShow;fixture::mask=info->fMask;return fixture::shell?TRUE:FALSE;}
static DWORD fixtureWait(HANDLE,DWORD){++fixture::waits;return fixture::live?WAIT_TIMEOUT:WAIT_OBJECT_0;}
static BOOL fixtureClose(HANDLE){++fixture::closes;return TRUE;}
#define ShellExecuteExW fixtureShell
#define WaitForSingleObject fixtureWait
#define CloseHandle fixtureClose
namespace ymcc::steamui {using Json=nlohmann::json;
${stop}
}
#undef ShellExecuteExW
#undef WaitForSingleObject
#undef CloseHandle
static std::atomic<bool> g_exitRequested{false};static struct {bool isolated=false;} g_aiFanMockSession;
static ymcc::steamsession::Observer g_steamSettingsObserver;
static struct {void requestRefresh(const ymcc::steamsession::Session&){++fixture::refreshes;}} g_steamModeObserver;
static std::map<std::string,std::function<json(const json&)>> handlers;
static void ipc_on(const char* command,std::function<json(const json&)> fn){handlers[command]=std::move(fn);}
int main(){json checks=json::array();const auto need=[&](bool pass,const std::string& name){checks.push_back({{"name",name},{"pass",pass}});if(!pass)throw std::runtime_error(name);};
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
for(const auto& marker:std::vector<json>{nullptr,false,"true",1,json::object()}){fixture::reset();auto request=valid;request["userInitiated"]=marker;bool rejected=false;try{ymcc::steamui::stop(observer,request);}catch(const std::runtime_error&){rejected=true;}need(rejected&&fixture::snapshots==0&&fixture::shells==0,"malformed manual marker stops before all native session/process/Shell ports");}
{fixture::reset();auto request=valid;request.erase("userInitiated");bool rejected=false;try{ymcc::steamui::stop(observer,request);}catch(...){rejected=true;}need(rejected&&fixture::snapshots==0,"legacy stop request never gains an implicit manual marker");}
for(const auto& change:std::vector<json>{json{{"pid",-1}},json{{"pid",123.5}},json{{"pid","123"}},json{{"birth",456}},json{{"exe","other.exe"}},json{{"uri","steam://evil"}}}){fixture::reset();auto request=valid;request.update(change);bool rejected=false;try{ymcc::steamui::stop(observer,request);}catch(...){rejected=true;}need(rejected&&fixture::snapshots==0&&fixture::shells==0,"malformed or arbitrary target rejected before native ports");}
for(const auto& [name,pid,birth]:std::vector<std::tuple<std::string,uint64_t,std::string>>{{"other PID",124,"456"},{"PID reuse",123,"789"},{"uint32 wraparound",uint64_t{0x100000000ULL+123},"456"}}){fixture::reset();auto request=valid;request["pid"]=pid;request["birth"]=birth;auto result=ymcc::steamui::stop(observer,request);need(result["ok"]==false&&fixture::lookups==0&&fixture::shells==0,name+" cannot target the current client");}
for(const int presence:{-1,0}){fixture::reset();fixture::presence=presence;auto result=ymcc::steamui::stop(observer,valid);need(result["ok"]==false&&fixture::lookups==0&&fixture::shells==0,"unknown/stopped presence never launches anything");}
{fixture::reset();fixture::lookup=false;auto r=ymcc::steamui::stop(observer,valid);need(r["ok"]==false&&fixture::shells==0&&fixture::closes==0,"unverified image/session cannot dispatch");}
{fixture::reset();fixture::birth=false;auto r=ymcc::steamui::stop(observer,valid);need(r["ok"]==false&&fixture::shells==0&&fixture::closes==1,"process-birth mismatch closes only the inert admitted handle");}
{fixture::reset();fixture::replaceAt=2;auto r=ymcc::steamui::stop(observer,valid);need(r["ok"]==false&&fixture::shells==0&&fixture::closes==1,"changed observer identity is checked again immediately before Shell");}
{fixture::reset();fixture::live=false;auto r=ymcc::steamui::stop(observer,valid);need(r["ok"]==false&&fixture::shells==0&&fixture::closes==1,"already exited original process cannot authorize a dispatch");}
{fixture::reset();auto r=ymcc::steamui::stop(observer,valid);need(r["ok"]==true&&fixture::shells==1&&fixture::closes==1,"one original verified-session command is submitted");need(fixture::file==L"X:\\inert-steam\\steam.exe"&&fixture::parameters==L"-shutdown"&&fixture::directory==L"X:\\inert-steam","only the fixed verified-client executable and graceful shutdown flag reach Shell");need(fixture::show==SW_HIDE&&(fixture::mask&SEE_MASK_FLAG_NO_UI),"request has no new visible console/error UI");need(r["requestedState"]=="stopped"&&!r.contains("exited")&&!r.value("applied",false),"submission never claims an actual process exit receipt");}
{fixture::reset();fixture::shell=false;auto r=ymcc::steamui::stop(observer,valid);need(r["ok"]==false&&r["reason"]=="steam-exit-command-failed"&&fixture::closes==1,"failed Shell acceptance is retained without kill fallback");}
{fixture::reset();g_exitRequested=true;bool rejected=false;try{handlers.at("steam.ui.stop")(valid);}catch(...){rejected=true;}g_exitRequested=false;need(rejected&&fixture::snapshots==0,"actual registrar rejects during application exit");}
{fixture::reset();g_aiFanMockSession.isolated=true;bool rejected=false;try{handlers.at("steam.ui.stop")(valid);}catch(...){rejected=true;}g_aiFanMockSession.isolated=false;need(rejected&&fixture::snapshots==0,"actual registrar refuses isolated hardware-test mode");}
{fixture::reset();const auto r=handlers.at("steam.ui.stop")(valid);need(r["ok"]==true&&fixture::shells==1&&fixture::refreshes==1,"actual IPC registrar forwards once and requests observation only");}
ymcc::radial::setTestingEnabled(false);need(!ymcc::radial::userAvailable(),"public test setter restores closed runtime admission");
handlers.clear();

${registration}
{fixture::reset();const auto r=handlers.at("steam.ui.stop")(valid);need(r["ok"]==false&&r["available"]==false&&fixture::snapshots==0&&fixture::lookups==0&&fixture::shells==0,"revoked testing admission refuses before all original native ports");}
std::cout<<json{{"pass",true},{"checks",checks},{"count",checks.size()},{"realSteamOperations",0}}.dump();return 0;
}catch(const std::exception& error){std::cout<<json{{"pass",false},{"checks",checks},{"error",error.what()}}.dump();return 2;}}
`;
try{
 fs.writeFileSync(path.join(out,'native-consumer.cpp'),cpp);const q=value=>'"'+value+'"';
 fs.writeFileSync(path.join(out,'compile.rsp'),['/nologo','/std:c++20','/utf-8','/EHsc','/O1','/MT',q(path.join(out,'native-consumer.cpp')),`/Fo${q(path.join(out,'native.obj'))}`,`/Fe${q(path.join(out,'native-consumer.exe'))}`].join('\r\n'));
 fs.writeFileSync(path.join(out,'compile.cmd'),calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'cl.exe @compile.rsp\r\nexit /b %errorlevel%\r\n');
 try{const result=await run(process.env.ComSpec||'cmd.exe',['/d','/c','compile.cmd'],{cwd:out,windowsHide:true,timeout:70000,maxBuffer:2000000});fs.writeFileSync(path.join(out,'compile.log'),result.stdout+result.stderr);}catch(error){fs.writeFileSync(path.join(out,'compile.log'),String(error.stdout||'')+String(error.stderr||''));throw error;}
 let executed;try{executed=await run(path.join(out,'native-consumer.exe'),[],{cwd:out,windowsHide:true,timeout:10000,maxBuffer:2000000});}catch(error){fs.writeFileSync(path.join(out,'native-result.json'),String(error.stdout||''));fs.writeFileSync(path.join(out,'native-stderr.log'),String(error.stderr||''));throw error;}fs.writeFileSync(path.join(out,'native-result.json'),executed.stdout);fs.writeFileSync(path.join(out,'native-stderr.log'),executed.stderr);const result=JSON.parse(executed.stdout);assert.ok(result.pass);report.checks=result.count;report.caseNames=result.checks.map(item=>item.name);report.pass=true;
}catch(error){report.error=error.stack;process.exitCode=2;}
finally{const receipt=path.join(out,'native-result.json');if(fs.existsSync(receipt)){try{report.nativeResult=JSON.parse(fs.readFileSync(receipt,'utf8'));}catch{}}fs.writeFileSync(path.join(out,'verification.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({pass:report.pass,checks:report.checks,error:report.error,out}));}
