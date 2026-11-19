/** Supporting lane of settings.boot-modes-file-ui. Original IPC and owned IO. */
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
import {repo} from './harness.mjs';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export function prepareTouchProfilesNative({out,main,slice,groups}){
 const root=path.join(out,'production'),seen=new Map();
 const visit=file=>{
  file=path.resolve(file);if(seen.has(file))return;
  const relative=path.relative(repo,file);if(relative.startsWith('..')||path.isAbsolute(relative))throw Error('touch header escaped source root');
  const bytes=fs.readFileSync(file);seen.set(file,bytes);
  for(const match of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"]+)"/gm)){
   const candidates=[path.resolve(path.dirname(file),match[1]),path.resolve(repo,'native',match[1]),path.resolve(repo,'deps/json',match[1])];
   const dependency=candidates.find(candidate=>fs.existsSync(candidate)&&fs.statSync(candidate).isFile());
   if(!dependency)throw Error('Missing quoted touch dependency '+match[1]+' in '+relative);visit(dependency);
  }
 };
 for(const file of ['native/settings_baseline.h','native/input_startup_policy.h','native/controller_persona_settings.h','native/controller_persona.h','native/boot_mode_shortcuts.h','native/screen_touchpad_profile_store.h','native/boot_mode_preferences.h'])visit(path.join(repo,file));
 for(const [file,bytes]of seen){const target=path.join(root,path.relative(repo,file));fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});}
 const guardBody=slice('class SettingsFileGuard {','// Defined beside the effective input consumer.');
 const writer=slice('static bool ymSettingsWriteSection(','// Native MSI shield writes');
 const handler=slice('    ipc_on("screenTouchpads.saveProfiles",','    ipc_on("screenTouchpads.set",');
 groups.push(writer);
 const inputs=[...seen].map(([file,bytes])=>({path:path.relative(repo,file).replaceAll('\\','/'),sha256:sha(bytes)}));
 fs.writeFileSync(out+'/touch-source.json',JSON.stringify({mainSha256:sha(Buffer.from(main)),fragments:{guard:{sha256:sha(Buffer.from(guardBody)),text:guardBody},writer:{sha256:sha(Buffer.from(writer)),text:writer},ipc:{sha256:sha(Buffer.from(handler)),text:handler}},headers:inputs},null,2),{flag:'wx'});
 const guard=String.raw`
static std::timed_mutex g_settingsFileMtx;
static unsigned fixtureSettingsLocks=0,fixtureMainCommits=0;
static HANDLE WINAPI fixtureMutex(LPSECURITY_ATTRIBUTES security,BOOL owned,LPCWSTR){return ::CreateMutexW(security,owned,(L"Local\\YMCC-BootTouchFixture-"+std::to_wstring(GetCurrentProcessId())).c_str());}
static DWORD WINAPI fixtureWait(HANDLE handle,DWORD timeout){const auto result=::WaitForSingleObject(handle,timeout);if(result==WAIT_OBJECT_0||result==WAIT_ABANDONED)++fixtureSettingsLocks;return result;}
#define CreateMutexW fixtureMutex
#define WaitForSingleObject fixtureWait
`+guardBody+String.raw`
#undef CreateMutexW
#undef WaitForSingleObject
`;
 const ioPorts=String.raw`
static BOOL WINAPI fixtureMove(LPCWSTR from,LPCWSTR to,DWORD flags){const auto result=::MoveFileExW(from,to,flags);if(result&&to&&_wcsicmp(to,YM_SETTINGS_FILE.c_str())==0)++fixtureMainCommits;return result;}
static BOOL WINAPI fixtureReplace(LPCWSTR replaced,LPCWSTR replacement,LPCWSTR backup,DWORD flags,LPVOID exclude,LPVOID reserved){const auto result=::ReplaceFileW(replaced,replacement,backup,flags,exclude,reserved);if(result&&replaced&&_wcsicmp(replaced,YM_SETTINGS_FILE.c_str())==0)++fixtureMainCommits;return result;}
#define MoveFileExW fixtureMove
#define ReplaceFileW fixtureReplace
`;
 const touchHandler=String.raw`
#undef MoveFileExW
#undef ReplaceFileW
static std::function<json(const json&)> touchProfilesHandler;
static json fixtureTouchEvents=json::array();
static json fixtureTouchControls=json::object();
static std::string fixtureObservedBoot="file-fixture-boot-1";
static std::string bootModeStartupIdentity(){return fixtureObservedBoot;}
static void ipc_on(const char* name,const std::function<json(const json&)>& callback){if(std::string(name)!="screenTouchpads.saveProfiles")throw std::runtime_error("unexpected IPC registration");touchProfilesHandler=callback;}
static void ipc_emit(const char* event,const json& data){fixtureTouchEvents.push_back({{"event",event},{"data",data}});}
static void logScreenTouchpadObservation(const char*,const char*,const json&){}
static void registerTouchProfileHandler(){
`+handler+String.raw`
}
static json executeTouchProfileHandler(const json& request){
 using namespace ymcc::screenpads;
 const auto controls=request.value("controls",json::object());
 dryRun=true;inputTeardown=controls.value("teardown",false);windowError=controls.value("windowError",0u);inputError=controls.value("inputError",0u);
 activeProfile=controls.value("active",-1);appliedProfile=-1;
 // Active configuration tests need a live settings owner. Presentation remains
 // paused and every HWND creation is denied/counted by the private syscall port.
 initialized=controls.value("ownerInitialized",activeProfile.load()>=0);
 presentationSuspended.store(true);fixtureTouchWindowCreates=0;
 available=controls.value("steamDeckAvailable",false);psAvailable=controls.value("psAvailable",false);
 fixtureObservedBoot=controls.value("observedBoot",std::string("file-fixture-boot-1"));
 fixtureTouchEvents=json::array();fixtureSettingsLocks=0;fixtureMainCommits=0;
 fixtureTouchControls=controls;
 quickAccessContextObserver=[](){
  // Original configure can clear a previous window fault during refresh. The
  // explicit failure controls model its existing post-configure observation.
  if(const auto error=fixtureTouchControls.value("windowError",0u))windowError=error;
  if(const auto error=fixtureTouchControls.value("inputError",0u))inputError=error;
  const auto change=fixtureTouchControls.value("afterConfigure",std::string{});
  if(change=="active-profile")activeProfile=3;
  else if(change=="teardown")inputTeardown=true;
  else if(change=="configuration"){std::lock_guard<std::mutex> lock(mutex);core.config.transparency=64;}
  else if(change=="deck-output")available=false;
 };
 registerTouchProfileHandler();
 HANDLE held=INVALID_HANDLE_VALUE;
 if(controls.value("denyReplace",false)){held=::CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(held==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot create owned replacement denial");}
 json result;
 try{result=touchProfilesHandler(request.at("args"));}catch(...){quickAccessContextObserver=nullptr;if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);throw;}
 quickAccessContextObserver=nullptr;
 if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
 return {{"result",result},{"events",fixtureTouchEvents},{"settingsLocks",fixtureSettingsLocks},{"mainCommits",fixtureMainCommits},
  {"runtime",{{"initialized",initialized},{"appliedProfile",appliedProfile.load()},{"activeProfile",activeProfile.load()},{"config",configJson(getConfig())},{"injectedInputs",state().value("injectedInputs",0u)},
    {"touchWindows",bool(windows[0]||windows[1]||outlines[0]||outlines[1]||buttonsVisible())},
    {"presentationSuspended",presentationSuspended.load()},{"windowCreateAttempts",fixtureTouchWindowCreates}}}};
}
`;
 const touchDispatch=String.raw`
 if(operation=="touchDefaults")return ymcc::screenpads::ProfileBank(ymSettingsReadUnlocked().value("screenTouchpads",json::object())).json();
 if(operation=="touchProfiles")return executeTouchProfileHandler(request);
`;
 const touchHeaders=String.raw`
static unsigned fixtureTouchWindowCreates=0;
static HWND WINAPI fixtureTouchNoWindow(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID){++fixtureTouchWindowCreates;::SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
#define CreateWindowExW fixtureTouchNoWindow
#include "screen_touchpad_profile_store.h"
#include "boot_mode_preferences.h"
#undef CreateWindowExW
`;
 return {guard,ioPorts,touchHeaders,touchHandler,touchDispatch,includeRoot:root,extraInputs:inputs.map(row=>row.path)};
}
