import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {execFileSync} from 'node:child_process';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const argv=process.argv.slice(2);const options=argv.slice(1);if(!argv[0]||options.some(x=>!['--command-outlet','--prepare-only'].includes(x))||new Set(options).size!==options.length){console.error('Invalid Steam UI consumer selection');process.exit(2);}const outlet=options.includes('--command-outlet'),prepareOnly=options.includes('--prepare-only');
const root=process.cwd(),out=path.resolve(argv[0]);fs.mkdirSync(out,{recursive:true});const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const actualHeaderPath=process.env.YMCC_STEAM_UI_MODE_HEADER_OVERRIDE||path.join(root,'native/steam_ui_mode.h');const header=fs.readFileSync(actualHeaderPath,'utf8'),mainPath=process.env.YMCC_STEAM_UI_MAIN_OVERRIDE||path.join(root,'native/main.cpp'),main=fs.readFileSync(mainPath,'utf8');const extract=name=>{const value=header.match(new RegExp('inline (?:Json|bool|ClientDispatch|const wchar_t\\*) '+name+'\\([\\s\\S]*?\\n\\}'))?.[0];assert(value,'production '+name);return value};const registration=main.match(/    ipc_on\("steam\.ui\.showBigPicture",[\s\S]*?\n    \}\);/)?.[0];assert(registration);assert(main.includes('cmd == "steam.ui.showBigPicture"'));
const sharedOutlet=header.includes('enum class ClientCommand');
const coordinatedCommands=header.includes('steam_command_coordinator.h');
// Use the actual public types and pure epoch comparator. Only publication reads
// are controlled here; no private producer algorithm or registry scan is added.
const observerPath=path.join(root,'native/steam_session_observer.h'),observerText=fs.readFileSync(observerPath,'utf8').replaceAll('\r\n','\n');
const observerTypes=observerText.slice(observerText.indexOf('struct Session {'),observerText.indexOf('using IdentityPublished'));
assert(observerTypes.startsWith('struct Session {')&&observerTypes.includes('struct IdentitySnapshot {'));
const identityComparator=observerText.match(/    bool sameIdentityEpoch\([\s\S]*?\n    \}/)?.[0];assert(identityComparator,'actual public identity comparator');
const common=sharedOutlet?header.slice(header.indexOf('enum class ClientCommand'),header.indexOf('inline Json set(')):'';
const registrations=outlet?['steam.ui.showBigPicture','steam.ui.stop','steam.uiMode.set'].map(name=>{const escaped=name.replaceAll('.','\\.');const value=main.match(new RegExp('    ipc_on\\("'+escaped+'",[\\s\\S]*?\\n    \\}\\);'))?.[0];assert(value,'production '+name);return value;}):[registration];
let commandCases=outlet?fs.readFileSync(new URL('./steam_ui_command_outlet_cases.inc',import.meta.url),'utf8'):'';
commandCases=commandCases.replace("        {\"recovery-admit-throws-closes-handle\",[]{", '#pragma warning(push)\n#pragma warning(disable:4702)\n'+"        {\"recovery-admit-throws-closes-handle\",[]{").replace("        {\"recovery-pure-owner-throws\",[]{", '#pragma warning(pop)\n'+"        {\"recovery-pure-owner-throws\",[]{");
commandCases=commandCases.replace('#include "steam_command_availability.h"', '#include "'+path.join(root,'native/steam_command_availability.h').replaceAll('\\','/')+'"');
const commandNames=[...commandCases.matchAll(/\{"([^"]+)",\[\]/g)].map(match=>match[1]);assert.equal(new Set(commandNames).size,commandNames.length,'Duplicate original command case');assert(!commandNames.includes('manual-identity'),'Aggregate selector cannot shadow an original case');
const recover=(session,mode,admit)=>'ymcc::steamui::dispatchRecoveryMode('+session+','+mode+','+admit+(sharedOutlet?',[]{return fixture::owner;}':'')+')';
const powerDefinitions=['captureSharedPowerLifecycle','sharedPowerLifecycleCurrent'].map(name=>{const value=main.match(new RegExp('\\[\\[maybe_unused\\]\\] static bool '+name+'\\([\\s\\S]*?\\n\\}'))?.[0];assert(value,'actual shared power consumer '+name);return value;}).join('\n');
let cpp=`#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <iostream>
#include <functional>
#include <memory>
#include <cstdint>
#include <atomic>
#include <map>
#include <vector>
#include <algorithm>
#include <stdexcept>
${coordinatedCommands?'#include "'+path.join(root,'native/steam_command_coordinator.h').replaceAll('\\','/')+'"\n':''}#include "${path.join(root,'native/sleep_power_state.h').replaceAll('\\','/')}"
#define YMCC_COMMAND_SHARED ${sharedOutlet?1:0}
#include "${path.join(root,'deps/json/json.hpp').replaceAll('\\','/')}"

// The original command fixture keeps its controlled publication/process/shell
// leaves. Only command-source selections use the actual SourceOwner read/drain.
static HANDLE WINAPI fixtureCommandSourceMutex(LPSECURITY_ATTRIBUTES sa,BOOL owner,LPCWSTR name){
 if(!name||std::wstring_view(name).find(L"Local\\\\YeManCC.SteamSource.v1.")!=0){SetLastError(ERROR_INVALID_NAME);return nullptr;}
 const auto scope=L"Local\\\\YMCCFixture.CommandSource."+std::to_wstring(GetCurrentProcessId());
 return ::CreateMutexW(sa,owner,scope.c_str());
}
#define CreateMutexW fixtureCommandSourceMutex
#include "${path.join(root,'native/steam_source_owner.h').replaceAll('\\\\','/')}"
#undef CreateMutexW
#include <optional>
#include <thread>
#include <exception>

using json=nlohmann::json;
namespace fixture {
int shown=-1;int reads=0,shells=0,lookups=0,snapshots=0;
ymcc::steamsession::SourceOwner* commandSource=nullptr;
bool live=true,birth=true,current=true,accepted=true,owner=true,found=true;
int closes=0,admissions=0,identitySnapshots=0,legacySnapshots=0,presenceReads=0;
bool identityKnown=true;int presence=1;DWORD account=42;
std::uint64_t processEpoch=7,accountEpoch=9,revision=1;
std::function<void()> onRead,onLookup,onWait,onSnapshot,onShell;std::string mode="bigpicture";json state;
enum class TokenFault { None, DifferentUser, DifferentSession, Unavailable, QueryFailure, QueryThrow };
TokenFault tokenFault=TokenFault::None;
// Only real token handles opened by this consumer are tracked. The bool marks
// the inert target (555), whose queries normally read our own process token.
std::map<HANDLE,bool> tokens;
int tokenOpens=0,tokenCloses=0,tokenCloseFailures=0,targetTokenOpenAttempts=0,targetTokenQueries=0,targetUserProbes=0,targetUserFills=0,sidComparisons=0;
void tokensReturned(){
    if(!tokens.empty()||tokenOpens!=tokenCloses||tokenCloseFailures)
        throw std::runtime_error("owned Windows token handles must all be returned");
}
void reset(){
    tokensReturned(); // Never erase a previous case's outstanding token debt.
    tokenFault=TokenFault::None;
    tokenOpens=tokenCloses=tokenCloseFailures=targetTokenOpenAttempts=targetTokenQueries=targetUserProbes=targetUserFills=sidComparisons=0;
    shown=-1;reads=shells=lookups=snapshots=0;live=birth=current=accepted=owner=found=true;closes=admissions=0;
    onRead=onLookup=onWait=onSnapshot=onShell={};mode="bigpicture";
    identitySnapshots=legacySnapshots=presenceReads=0;identityKnown=true;presence=1;account=42;processEpoch=7;accountEpoch=9;revision=1;
    state={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",uint32_t(123)},{"birth","456"}};
}}
namespace ymcc::steamsession {
${observerTypes}
struct Observer {
 struct SourceReadPort {
  std::optional<SourceOwner::Read> actual;
  bool real=false;
  explicit operator bool()const noexcept{return !real||(actual&&static_cast<bool>(*actual));}
  struct View {SourceOwner::View actual;bool real=false;bool current()const noexcept{return !real||actual.current();}};
  View view()const noexcept{return {actual?actual->view():SourceOwner::View{},real};}
 };
 SourceReadPort sourceRead(){SourceReadPort value;if(fixture::commandSource){value.real=true;value.actual.emplace(fixture::commandSource->read());}return value;}

 Session sessionSnapshot(){++fixture::snapshots;++fixture::legacySnapshots;if(fixture::onSnapshot)fixture::onSnapshot();Session value{LR"(X:\\InertSteam)",123,fixture::account,456};if(!fixture::current)value.created=999;return value;}
 int steamPresence(){++fixture::presenceReads;return fixture::presence;}
 std::shared_ptr<const IdentitySnapshot> identitySnapshot() const {
  ++fixture::snapshots;++fixture::identitySnapshots;if(fixture::onSnapshot)fixture::onSnapshot();if(!fixture::identityKnown)return {};
  auto value=std::make_shared<IdentitySnapshot>();value->session={LR"(X:\\InertSteam)",123,fixture::account,fixture::current?456ull:999ull};
  value->presence=fixture::presence;value->processEpoch=fixture::processEpoch;value->accountEpoch=fixture::accountEpoch;value->revision=fixture::revision;
  return value; // Explicit immutable terminal facts; not actual Observer publication.
 }
${identityComparator}
};
HANDLE verifiedProcess(DWORD,const std::wstring&,ULONGLONG* birth){++fixture::lookups;if(fixture::onLookup)fixture::onLookup();*birth=fixture::birth?456:999;return fixture::found?reinterpret_cast<HANDLE>(555):nullptr;}}
static BOOL fakeShell(SHELLEXECUTEINFOW* info){const auto command=std::wstring(info->lpParameters);if(std::wstring(info->lpFile)!=LR"(X:\\InertSteam\\steam.exe)"||(command!=LR"ARGS(-url "steam://open/bigpicture")ARGS"&&command!=LR"ARGS(-url "steam://close/bigpicture")ARGS"&&command!=L"-shutdown"))throw std::runtime_error("fixed existing-client command");++fixture::shells;fixture::shown=info->nShow;if(fixture::onShell)fixture::onShell();return fixture::accepted;}
static DWORD fakeWait(HANDLE,DWORD){if(fixture::onWait)fixture::onWait();return fixture::live?WAIT_TIMEOUT:WAIT_OBJECT_0;}
// These local token queries are real Windows calls, including TokenSessionId
// and the TokenUser size probe/fill contract. Foreign user/session identities
// below are injected leaves; they do not prove real cross-session behavior.
static BOOL fakeOpenProcessToken(HANDLE process,DWORD access,PHANDLE token){
    if(!token){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    *token=nullptr;
    const bool target=process==reinterpret_cast<HANDLE>(555);
    if(target){
        ++fixture::targetTokenOpenAttempts;
        if(fixture::tokenFault==fixture::TokenFault::Unavailable){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    }
    HANDLE opened=nullptr;
    if(!::OpenProcessToken(target?::GetCurrentProcess():process,access,&opened))return FALSE;
    try{fixture::tokens.emplace(opened,target);}catch(...){
        if(!::CloseHandle(opened))++fixture::tokenCloseFailures;
        throw;
    }
    ++fixture::tokenOpens;*token=opened;return TRUE;
}
static BOOL fakeGetTokenInformation(HANDLE token,TOKEN_INFORMATION_CLASS kind,LPVOID buffer,DWORD bytes,PDWORD returned){
    const auto entry=fixture::tokens.find(token);
    if(entry==fixture::tokens.end()){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    const bool target=entry->second;
    if(target){
        ++fixture::targetTokenQueries;
        if(kind==TokenUser){
            if(!buffer)++fixture::targetUserProbes;
            else{
                ++fixture::targetUserFills;
                // The probe still uses the real insufficient-buffer/length
                // contract. Inject failure only after the target fill begins.
                if(fixture::tokenFault==fixture::TokenFault::QueryThrow)throw std::runtime_error("injected target TokenUser fill exception");
                if(fixture::tokenFault==fixture::TokenFault::QueryFailure){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
            }
        }
    }
    const BOOL ok=::GetTokenInformation(token,kind,buffer,bytes,returned);
    if(ok&&target&&kind==TokenSessionId&&buffer&&bytes>=sizeof(DWORD)&&fixture::tokenFault==fixture::TokenFault::DifferentSession){
        auto& session=*static_cast<DWORD*>(buffer);session=session==MAXDWORD?0:session+1;
    }
    return ok;
}
static BOOL fakeEqualSid(PSID left,PSID right){
    ++fixture::sidComparisons;
    const BOOL equal=::EqualSid(left,right);
    return fixture::tokenFault==fixture::TokenFault::DifferentUser?FALSE:equal;
}
static BOOL fakeClose(HANDLE value){
    if(value==reinterpret_cast<HANDLE>(555)){++fixture::closes;return TRUE;}
    const auto entry=fixture::tokens.find(value);
    if(entry==fixture::tokens.end()){++fixture::tokenCloseFailures;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    const BOOL closed=::CloseHandle(value);
    if(closed){fixture::tokens.erase(entry);++fixture::tokenCloses;}else ++fixture::tokenCloseFailures;
    return closed;
}
#define ShellExecuteExW fakeShell
#define WaitForSingleObject fakeWait
#define CloseHandle fakeClose
#define OpenProcessToken fakeOpenProcessToken
#define GetTokenInformation fakeGetTokenInformation
#define EqualSid fakeEqualSid
namespace ymcc::steamui {using Json=nlohmann::json;inline Json read(steamsession::Observer&,bool force,const std::function<bool()>& = {}){if(!force)throw std::runtime_error("fresh mode required");++fixture::reads;if(fixture::onRead)fixture::onRead();return fixture::state;}
${extract('uriFor')}
${common}
${extract('set')}
${extract('showBigPicture')}
${extract('stop')}
${extract('dispatchRecoveryMode')}
}
#undef ShellExecuteExW
#undef WaitForSingleObject
#undef CloseHandle
#undef OpenProcessToken
#undef GetTokenInformation
#undef EqualSid
static std::atomic<bool> g_exitRequested{false};static struct {bool isolated=false;} g_aiFanMockSession;static ymcc::steamsession::Observer g_steamSettingsObserver;static std::map<std::string,std::function<json(const json&)>> handlers;void ipc_on(const char* name,std::function<json(const json&)> value){handlers[name]=value;}
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
namespace ymcc::radial{static bool userAvailable(){return true;}}
static struct {void requestRefresh(const ymcc::steamsession::Session&){} } g_steamModeObserver;
static struct {bool manual(const ymcc::steamsession::Session&){return true;}void desktop(const ymcc::steamsession::Session&){} } g_steamLinkAdmission;
static struct {void observeSteam(int,bool){} } g_deckySidebarRuntime;
static struct {void requestManualRecovery(){} } g_steamCssGuard;
static void deckySidebarDiagnostic(const char*,const json&,bool){}
static void installCommandHandlers(){${registrations.join("\n")}}
${commandCases}
int main(int argc,char** argv){${coordinatedCommands?'ymcc::steamcommand::Coordinator commandCoordinator;ymcc::steamcommand::Provider commandProvider(commandCoordinator);if(!commandProvider)return 2;':''}${outlet?'return runCommandOutlet(argc,argv);':'(void)argc;(void)argv;'}int count=0;const auto need=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++count;};try{
${registrations.join("\n")}
const json valid={{"pid",uint32_t(123)},{"birth","456"},{"userInitiated",true}};
for(const auto& intent:std::vector<json>{nullptr,false,1,"true",json::object()}){fixture::reset();auto request=valid;request["userInitiated"]=intent;bool refused=false;try{handlers.at("steam.ui.showBigPicture")(request);}catch(...){refused=true;}need(refused&&fixture::reads==0&&fixture::shells==0,"invalid intent before any reads");}
for(const auto& patch:std::vector<json>{{{"pid",-1}},{{"birth",123}},{{"extra",true}}}){fixture::reset();auto request=valid;request.update(patch);bool refused=false;try{handlers.at("steam.ui.showBigPicture")(request);}catch(...){refused=true;}need(refused&&fixture::reads==0,"invalid identity before any reads");}
fixture::reset();auto r=handlers.at("steam.ui.showBigPicture")(valid);need(r["ok"]==true&&fixture::reads==1&&fixture::shells==1,"one explicit show uses exact production mode command");
for(const auto* mode:{"desktop","unknown","stopped"}){fixture::reset();fixture::state["mode"]=mode;auto result=handlers.at("steam.ui.showBigPicture")(valid);need(result["ok"]==false&&fixture::shells==0,"other modes are never switched by status");}
for(const auto& patch:std::vector<json>{{{"known",false}},{{"running",false}},{{"pid",uint32_t(124)}},{{"birth","999"}}}){fixture::reset();fixture::state.update(patch);auto result=handlers.at("steam.ui.showBigPicture")(valid);need(result["ok"]==false&&fixture::shells==0,"stale read refuses show");}
for(int failure=0;failure<4;++failure){fixture::reset();if(failure==0)fixture::live=false;if(failure==1)fixture::birth=false;if(failure==2)fixture::current=false;if(failure==3)fixture::accepted=false;auto result=handlers.at("steam.ui.showBigPicture")(valid);need(result["ok"]==false&&fixture::shells==(failure==3?1:0),"final identity and process check has no fallback");}
fixture::reset();g_exitRequested=true;bool refused=false;try{handlers.at("steam.ui.showBigPicture")(valid);}catch(...){refused=true;}g_exitRequested=false;need(refused&&fixture::reads==0,"exit gate before native reads");
fixture::reset();g_aiFanMockSession.isolated=true;refused=false;try{handlers.at("steam.ui.showBigPicture")(valid);}catch(...){refused=true;}need(refused&&fixture::reads==0,"isolated gate before native reads");

fixture::reset();const ymcc::steamsession::Session recoverySession{LR"(X:\\InertSteam)",123,42,456};
need(${recover('recoverySession','"bigpicture"','[]{return fixture::current;}')},"recovery command submission");
need(fixture::shells==1&&fixture::shown==SW_SHOWNORMAL,"reopening Big Picture must request a displayed target, not SW_HIDE");
fixture::reset();need(${recover('recoverySession','"desktop"','[]{return fixture::current;}')}&&fixture::shells==1&&fixture::shown==SW_HIDE,"desktop close keeps background dispatch");
for(int failure=0;failure<4;++failure){fixture::reset();if(failure==0)fixture::live=false;if(failure==1)fixture::birth=false;if(failure==2)fixture::current=false;if(failure==3)fixture::accepted=false;need(!${recover('recoverySession','"bigpicture"','[]{return fixture::current;}')}&&fixture::shells==(failure==3?1:0),"recovery display intent cannot bypass identity, current or dispatch failure");}
fixture::reset();bool invalidRecovery=false;try{${recover('recoverySession','"shutdown"','[]{return true;}')};}catch(const std::runtime_error&){invalidRecovery=true;}need(invalidRecovery&&fixture::shells==0&&fixture::lookups==0,"recovery fixed mode allowlist precedes process lookup");
fixture::tokensReturned();std::cout<<json{{"ok",true},{"checks",count},{"realSteamActions",0}}.dump();return 0;
}catch(const std::exception& error){std::cout<<json{{"ok",false},{"checks",count},{"error",error.what()}}.dump();return 23;}}
`;
if(outlet){const marker='return runCommandOutlet(argc,argv);int count=0;';const at=cpp.indexOf(marker);assert(at>=0,'exact original outlet tail');cpp=cpp.slice(0,at)+'return runCommandOutlet(argc,argv);'+String.fromCharCode(125,10);}
if(prepareOnly){const target=path.join(out,'consumer.cpp');fs.writeFileSync(target,cpp,{flag:'wx'});const report={status:'PREPARED_ONLY_NO_COMPILE_NO_BUSINESS',commandOutlet:outlet,defaultGroupExecuted:false,actualHeader:{path:actualHeaderPath,sha256:hash(actualHeaderPath)},actualMain:{path:mainPath,sha256:hash(mainPath)},publicIdentitySource:{path:observerPath,sha256:hash(observerPath)},identityPort:'Controlled immutable receipt; actual public Session/IdentitySnapshot types and sameIdentityEpoch comparator, not Observer publication',source:target,definitions:sharedOutlet?['ClientCommand','ClientDispatch',...(header.includes('inline bool clientCommandProcessOwner(')?['clientCommandProcessOwner']:[]),'clientCommandCurrent','dispatchClientCommand',...(header.includes('inline ClientDispatch dispatchClientCommandOwned(')?['dispatchClientCommandOwned']:[]),'uriFor','set','stop','showBigPicture','dispatchRecoveryMode']:['uriFor','set','stop','showBigPicture','dispatchRecoveryMode'],registrars:outlet?['steam.ui.showBigPicture','steam.ui.stop','steam.uiMode.set']:['steam.ui.showBigPicture']};fs.writeFileSync(path.join(out,'PREPARED.json'),JSON.stringify(report,null,2),{flag:'wx'});console.log(JSON.stringify(report));process.exit(0);}
const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
function run(name,source){const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'consumer.cpp'),source);fs.writeFileSync(path.join(dir,'compile.cmd'),preamble+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O1 consumer.cpp /Foconsumer.obj /Feconsumer.exe /link advapi32.lib\r\nexit /b %errorlevel%\r\n');const compiled=execFileSync('cmd.exe',['/d','/c','compile.cmd'],{cwd:dir,windowsHide:true,timeout:60000,encoding:'utf8'});fs.writeFileSync(path.join(dir,'compile.log'),compiled);try{return {code:0,result:JSON.parse(execFileSync(path.join(dir,'consumer.exe'),[],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:10000}))};}catch(error){return {code:error.status,result:JSON.parse(error.stdout)}}}
if(outlet){const positive=run('command-outlet',cpp);fs.writeFileSync(path.join(out,'command-result.json'),JSON.stringify(positive,null,2));assert.equal(positive.code,0,JSON.stringify(positive));assert(positive.result.ok&&!positive.result.defaultGroupExecuted);const report={...positive.result,actualHeader:{path:actualHeaderPath,sha256:hash(actualHeaderPath)},actualMain:{path:mainPath,sha256:hash(mainPath)}};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));process.exit(0);}
const positive=run('positive',cpp);fs.writeFileSync(path.join(out,'positive-result.json'),JSON.stringify(positive,null,2));assert.equal(positive.code,0,JSON.stringify(positive));const bad=cpp.replace('if (state.value("mode", std::string{}) != "bigpicture")','if (false)');assert.notEqual(bad,cpp);const negative=run('desktop-negative',bad);assert.equal(negative.code,23);assert.equal(negative.result.error,'other modes are never switched by status');assert(!/g_steamCss|g_decky|g_steamLinkAdmission|launchSteam|wscript/.test(registration));
const report={ok:true,checks:positive.result.checks,negative:negative.result,sourceHashes:Object.fromEntries(['native/steam_ui_mode.h','native/sleep_power_state.h','native/main.cpp','tools/steam_bigpicture_show_native_selftest.mjs'].map(p=>[p,hash(p)])),actualHeader:{path:actualHeaderPath,sha256:hash(actualHeaderPath),override:!!process.env.YMCC_STEAM_UI_MODE_HEADER_OVERRIDE},actualProductionDefinitions:['uriFor','set','showBigPicture','dispatchRecoveryMode','IPC registrar'],realSteamActions:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
