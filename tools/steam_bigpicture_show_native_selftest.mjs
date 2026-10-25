import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {execFileSync} from 'node:child_process';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=process.cwd(),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const header=fs.readFileSync('native/steam_ui_mode.h','utf8'),main=fs.readFileSync('native/main.cpp','utf8');const extract=name=>{const value=header.match(new RegExp('inline (?:Json|const wchar_t\\*) '+name+'\\([\\s\\S]*?\\n\\}'))?.[0];assert(value,'production '+name);return value};const registration=main.match(/    ipc_on\("steam\.ui\.showBigPicture",[\s\S]*?\n    \}\);/)?.[0];assert(registration);assert(main.includes('cmd == "steam.ui.showBigPicture"'));
const cpp=`#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <iostream>
#include <functional>
#include <atomic>
#include <map>
#include "${path.join(root,'deps/json/json.hpp').replaceAll('\\','/')}"
using json=nlohmann::json;
namespace fixture {int reads=0,shells=0,lookups=0,snapshots=0;bool live=true,birth=true,current=true,accepted=true;std::string mode="bigpicture";json state;void reset(){reads=shells=lookups=snapshots=0;live=birth=current=accepted=true;mode="bigpicture";state={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",uint32_t(123)},{"birth","456"}};}}
namespace ymcc::steamsession {struct Session {std::wstring root=LR"(X:\\InertSteam)";DWORD pid=123;ULONGLONG created=456;bool operator==(const Session&)const=default;};struct Observer {Session sessionSnapshot(){++fixture::snapshots;Session s;if(!fixture::current)s.created=999;return s;}int steamPresence(){return 1;}};HANDLE verifiedProcess(DWORD,const std::wstring&,ULONGLONG* birth){++fixture::lookups;*birth=fixture::birth?456:999;return reinterpret_cast<HANDLE>(555);}}
static BOOL fakeShell(SHELLEXECUTEINFOW* info){if(std::wstring(info->lpFile)!=LR"(X:\\InertSteam\\steam.exe)"||std::wstring(info->lpParameters)!=L"-url \\\"steam://open/bigpicture\\\"")throw std::runtime_error("fixed existing-client command");++fixture::shells;return fixture::accepted;}
static DWORD fakeWait(HANDLE,DWORD){return fixture::live?WAIT_TIMEOUT:WAIT_OBJECT_0;}static BOOL fakeClose(HANDLE){return TRUE;}
#define ShellExecuteExW fakeShell
#define WaitForSingleObject fakeWait
#define CloseHandle fakeClose
namespace ymcc::steamui {using Json=nlohmann::json;inline Json read(steamsession::Observer&,bool force){if(!force)throw std::runtime_error("fresh mode required");++fixture::reads;return fixture::state;}
${extract('uriFor')}
${extract('set')}
${extract('showBigPicture')}
}
#undef ShellExecuteExW
#undef WaitForSingleObject
#undef CloseHandle
static std::atomic<bool> g_exitRequested{false};static struct {bool isolated=false;} g_aiFanMockSession;static ymcc::steamsession::Observer g_steamSettingsObserver;static std::map<std::string,std::function<json(const json&)>> handlers;void ipc_on(const char* name,std::function<json(const json&)> value){handlers[name]=value;}
int main(){int count=0;const auto need=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++count;};try{
${registration}
const json valid={{"pid",uint32_t(123)},{"birth","456"},{"userInitiated",true}};
for(const auto& intent:std::vector<json>{nullptr,false,1,"true",json::object()}){fixture::reset();auto request=valid;request["userInitiated"]=intent;bool refused=false;try{handlers.at("steam.ui.showBigPicture")(request);}catch(...){refused=true;}need(refused&&fixture::reads==0&&fixture::shells==0,"invalid intent before any reads");}
for(const auto& patch:std::vector<json>{{{"pid",-1}},{{"birth",123}},{{"extra",true}}}){fixture::reset();auto request=valid;request.update(patch);bool refused=false;try{handlers.at("steam.ui.showBigPicture")(request);}catch(...){refused=true;}need(refused&&fixture::reads==0,"invalid identity before any reads");}
fixture::reset();auto r=handlers.at("steam.ui.showBigPicture")(valid);need(r["ok"]==true&&fixture::reads==1&&fixture::shells==1,"one explicit show uses exact production mode command");
for(const auto* mode:{"desktop","unknown","stopped"}){fixture::reset();fixture::state["mode"]=mode;auto result=handlers.at("steam.ui.showBigPicture")(valid);need(result["ok"]==false&&fixture::shells==0,"other modes are never switched by status");}
for(const auto& patch:std::vector<json>{{{"known",false}},{{"running",false}},{{"pid",uint32_t(124)}},{{"birth","999"}}}){fixture::reset();fixture::state.update(patch);auto result=handlers.at("steam.ui.showBigPicture")(valid);need(result["ok"]==false&&fixture::shells==0,"stale read refuses show");}
for(int failure=0;failure<4;++failure){fixture::reset();if(failure==0)fixture::live=false;if(failure==1)fixture::birth=false;if(failure==2)fixture::current=false;if(failure==3)fixture::accepted=false;auto result=handlers.at("steam.ui.showBigPicture")(valid);need(result["ok"]==false&&fixture::shells==(failure==3?1:0),"final identity and process check has no fallback");}
fixture::reset();g_exitRequested=true;bool refused=false;try{handlers.at("steam.ui.showBigPicture")(valid);}catch(...){refused=true;}g_exitRequested=false;need(refused&&fixture::reads==0,"exit gate before native reads");
fixture::reset();g_aiFanMockSession.isolated=true;refused=false;try{handlers.at("steam.ui.showBigPicture")(valid);}catch(...){refused=true;}need(refused&&fixture::reads==0,"isolated gate before native reads");
std::cout<<json{{"ok",true},{"checks",count},{"realSteamActions",0}}.dump();return 0;
}catch(const std::exception& error){std::cout<<json{{"ok",false},{"checks",count},{"error",error.what()}}.dump();return 23;}}
`;
const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
function run(name,source){const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'consumer.cpp'),source);fs.writeFileSync(path.join(dir,'compile.cmd'),preamble+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O1 consumer.cpp /Foconsumer.obj /Feconsumer.exe\r\nexit /b %errorlevel%\r\n');const compiled=execFileSync('cmd.exe',['/d','/c','compile.cmd'],{cwd:dir,windowsHide:true,timeout:60000,encoding:'utf8'});fs.writeFileSync(path.join(dir,'compile.log'),compiled);try{return {code:0,result:JSON.parse(execFileSync(path.join(dir,'consumer.exe'),[],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:10000}))};}catch(error){return {code:error.status,result:JSON.parse(error.stdout)}}}
const positive=run('positive',cpp);assert.equal(positive.code,0,JSON.stringify(positive));const bad=cpp.replace('if (state.value("mode", std::string{}) != "bigpicture")','if (false)');assert.notEqual(bad,cpp);const negative=run('desktop-negative',bad);assert.equal(negative.code,23);assert.equal(negative.result.error,'other modes are never switched by status');assert(!/g_steamCss|g_decky|g_steamLinkAdmission|launchSteam|wscript/.test(registration));
const report={ok:true,checks:positive.result.checks,negative:negative.result,sourceHashes:Object.fromEntries(['native/steam_ui_mode.h','native/main.cpp','tools/steam_bigpicture_show_native_selftest.mjs'].map(p=>[p,hash(p)])),actualProductionDefinitions:['uriFor','set','showBigPicture','IPC registrar'],realSteamActions:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
