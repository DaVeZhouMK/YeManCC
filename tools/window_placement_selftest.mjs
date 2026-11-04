import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(root,'package.json'));
const { transformSync }=require('esbuild');
const cases=[];
async function check(name,fn){await fn();cases.push(name);}
function evaluate(source,globals={},imports={}){
 const module={exports:{}};
 vm.runInNewContext(transformSync(source,{loader:'ts',format:'cjs',target:'es2020'}).code,{module,exports:module.exports,console,structuredClone,CustomEvent,Error,require(name){if(name in imports)return imports[name];throw Error('Unexpected import '+name);},...globals});
 return module.exports;
}
const read=file=>fs.readFileSync(path.join(root,file),'utf8').replaceAll('\r\n','\n');
const app=read('src/App.vue'),uiSource=read('src/bridge/uiSettings.ts'),native=read('native/main.cpp');
const monitorLayout=evaluate(read('src/bridge/monitorLayout.ts'));
function block(source,anchor){
 const start=source.indexOf(anchor);assert(start>=0,`Missing anchor: ${anchor}`);
 const open=source.indexOf('{',start);assert(open>=0);
 let depth=0,quote=null,lineComment=false,blockComment=false;
 for(let i=open;i<source.length;i++){
  const ch=source[i],next=source[i+1];
  if(lineComment){if(ch==='\n')lineComment=false;continue;}
  if(blockComment){if(ch==='*'&&next==='/'){blockComment=false;i++;}continue;}
  if(quote){if(ch==='\\'){i++;continue;}if(ch===quote)quote=null;continue;}
  if(ch==='/'&&next==='/'){lineComment=true;i++;continue;}
  if(ch==='/'&&next==='*'){blockComment=true;i++;continue;}
  if(ch==='"'||ch==="'"||ch==='`'){quote=ch;continue;}
  if(ch==='{')depth++;
  if(ch==='}'&&--depth===0)return source.slice(start,i+1);
 }
 throw Error('Unclosed block: '+anchor);
}
async function fixture(initial='right'){
 let disk={windowPlacement:initial,theme:'blue-black',unknownUiField:{keep:1}},failWrite=false,placementResult=true;
 const target=new EventTarget(),placements=[],errors=[];
 const ui=evaluate(uiSource,{window:target,localStorage:{getItem(){return null;},removeItem(){}}},{
  './backgroundPosition':evaluate(read('src/bridge/backgroundPosition.ts')),
  './settingsSnapshot':{snapshotSettingsData:value=>structuredClone(value)},
  './settingsRepository':{getSettingsGeneration:()=>1,assertSettingsGeneration:()=>{},readSettingsSection:async()=>structuredClone(disk),saveSettingsSection:async(_,patch)=>{if(failWrite)throw Error('simulated storage failure');disk={...disk,...structuredClone(patch)};}}
 });
 await ui.loadUiSettings();
 const start=app.indexOf('const windowPlacement = ref<'),end=app.indexOf('onMounted(async () => {',start);
 assert(start>=0&&end>start);
 const exports=evaluate(app.slice(start,end)+'\nexport { windowPlacement, applyWindowPlacement, onWindowPlacementSettingsChanged, onWindowPlacementSummoned };',{
  ref:value=>({value}),getUiSetting:ui.getUiSetting,windowApi:{place:async side=>{placements.push(side);if(placementResult instanceof Error)throw placementResult;return placementResult;}},console:{warn(...args){errors.push(args);}}
 });
 const registration=[...app.matchAll(/window\.addEventListener\('[^']+', onWindowPlacement(?:SettingsChanged|Summoned)\);/g)].map(m=>m[0]);
 const removal=[...app.matchAll(/window\.removeEventListener\('[^']+', onWindowPlacement(?:SettingsChanged|Summoned)\);/g)].map(m=>m[0]);
 assert.equal(registration.length,3);assert.equal(removal.length,3);
 evaluate(registration.join('\n'),{window:target,...exports});
 const settle=()=>new Promise(resolve=>setImmediate(resolve));
 return {ui,...exports,target,placements,errors,settle,get disk(){return disk;},set failWrite(value){failWrite=value;},set placementResult(value){placementResult=value;},setDisk(value){disk={...disk,...value};},dispose(){evaluate(removal.join('\n'),{window:target,...exports});}};
}
await check('startup waits for saved placement and does not place the default first',async()=>{
 assert(!app.slice(app.indexOf('onMounted(async'),app.indexOf('await loadUiSettings();')).includes('applyWindowPlacement()'));
 assert.match(app,/await loadUiSettings\(\);[\s\S]*?windowPlacement\.value = getUiSetting\('windowPlacement'\);[\s\S]*?await applyWindowPlacement\(\)\.catch\(reportWindowPlacementError\);/);
 const f=await fixture('left');await f.applyWindowPlacement();assert.deepEqual(f.placements,['left']);f.dispose();
});
await check('theme, opacity and blur changes do not move the window',async()=>{
 const f=await fixture();await f.ui.setUiSettings({theme:'red-black'});await f.ui.setUiSettings({backgroundOpacity:0.5});await f.ui.setUiSettings({backgroundBlur:4});await f.settle();assert.equal(f.placements.length,0);f.dispose();
});
await check('left/right persistence updates navigation and only moves on a side change',async()=>{
 const f=await fixture();await f.ui.setUiSettings({windowPlacement:'left'});await f.settle();assert.equal(f.disk.windowPlacement,'left');assert.equal(f.windowPlacement.value,'left');assert.deepEqual(f.placements,['left']);await f.ui.setUiSettings({windowPlacement:'left'});await f.ui.setUiSettings({windowPlacement:'right'});await f.settle();assert.deepEqual(f.placements,['left','right']);assert.equal(f.disk.unknownUiField.keep,1);f.dispose();
});
await check('settings reload/reset synchronizes placement without duplicate moves',async()=>{
 const f=await fixture();f.setDisk({windowPlacement:'left'});await f.ui.loadUiSettings();await f.settle();assert.deepEqual(f.placements,['left']);await f.ui.loadUiSettings();await f.settle();assert.deepEqual(f.placements,['left']);f.dispose();
});
await check('summon reapplies the selected side, and unmount removes all placement listeners',async()=>{
 const f=await fixture('left');f.target.dispatchEvent(new CustomEvent('ipc:window.summoned'));await f.settle();assert.deepEqual(f.placements,['left']);f.dispose();await f.ui.setUiSettings({windowPlacement:'right'});f.target.dispatchEvent(new CustomEvent('ipc:window.summoned'));await f.settle();assert.deepEqual(f.placements,['left']);
});
await check('missing/invalid placement normalizes to right',async()=>{
 for(const initial of [undefined,null,'center',42]){const f=await fixture(initial);assert.equal(f.ui.getUiSetting('windowPlacement'),'right');f.dispose();}
});
await check('native false/rejection is surfaced to the diagnostic handler',async()=>{
 const f=await fixture();f.placementResult=false;await f.ui.setUiSettings({windowPlacement:'left'});await f.settle();assert.equal(f.errors.length,1);f.placementResult=Error('simulated IPC failure');f.target.dispatchEvent(new CustomEvent('ipc:window.summoned'));await f.settle();assert.equal(f.errors.length,2);f.dispose();
});
await check('failed settings save keeps the saved side and shows a visible error',async()=>{
 const f=await fixture();const errMsg={value:''},windowPlacement={value:'right'};
 const handler=evaluate(block(read('src/views/SettingsView.vue'),'async function onWindowPlacementChange(')+'\nexport {onWindowPlacementChange};',{getUiSetting:f.ui.getUiSetting,setUiSettings:f.ui.setUiSettings,errMsg,windowPlacement});
 f.failWrite=true;await handler.onWindowPlacementChange('left');assert.equal(windowPlacement.value,'right');assert.equal(f.disk.windowPlacement,'right');assert.match(errMsg.value,/窗口位置保存失败.*simulated storage failure/);assert.equal(f.placements.length,0);f.failWrite=false;await handler.onWindowPlacementChange('left');await f.settle();assert.equal(windowPlacement.value,'left');assert.equal(errMsg.value,'');f.dispose();
});
await check('settings handler rejects invalid choices and rapid writes retain their order',async()=>{
 const f=await fixture();const errMsg={value:''},windowPlacement={value:'right'};
 const handler=evaluate(block(read('src/views/SettingsView.vue'),'async function onWindowPlacementChange(')+'\nexport {onWindowPlacementChange};',{getUiSetting:f.ui.getUiSetting,setUiSettings:f.ui.setUiSettings,errMsg,windowPlacement});
 await handler.onWindowPlacementChange('center');assert.equal(f.placements.length,0);await Promise.all([handler.onWindowPlacementChange('left'),handler.onWindowPlacementChange('right')]);await f.settle();assert.equal(windowPlacement.value,'right');assert.equal(f.disk.windowPlacement,'right');assert.deepEqual(f.placements,['left','right']);f.dispose();
});
await check('native lifecycle reflows all share the saved-side default policy',()=>{
 assert.match(native,/static bool applyFullHeightLayout\(HMONITOR preferredMonitor = nullptr, bool dockLeft = windowPlacementIsLeft\(\)\)/);
 for(const anchor of ['static void focusReflowMainWindow()', 'case WM_DPICHANGED:']){
  const begin=native.indexOf(anchor);assert(begin>=0);assert.match(native.slice(begin,begin+650),/applyFullHeightLayout\(focusCurrentTargetMonitor\(\)\)/);
 }
 assert.match(native,/if \(g_fullHeight\) \{\s*applyFullHeightLayout\(\);/);
 assert.match(app,/'app-body--nav-right': windowPlacement === 'left'/);
});

function scaleFixture(standalone = false, dpr = 1) {
 const target = new EventTarget(), vv = new EventTarget(), frames = new Map();
 Object.assign(target, { innerWidth: 640, innerHeight: 720, devicePixelRatio: dpr, visualViewport: vv });
 Object.assign(vv, { width: 640, height: 720 });
 const root = { clientWidth: 640, clientHeight: 720, style: { writes:0, getPropertyValue() { return this.scale || ''; }, setProperty(_, value) { this.scale = value; this.writes++; } } };
 const isStandaloneEditor = { value: standalone }, mounted = [], unmounted = []; let frameId = 0;
 const start = app.indexOf('const layoutBaseWidth ='), end = app.indexOf('function onGamepadRefresh()', start);
 const lifecycle = app.slice(app.indexOf('let stageResizeObserver:'), app.indexOf('</script>'));
 const exports = evaluate(app.slice(start, end) + lifecycle + '\nexport { nativeViewport, viewportSize, scalerStyle, uiScale, settleScale, nativeViewportFrames };', {
  window: target, document: { documentElement: root, querySelector(){return null;}, visibilityState:'visible',addEventListener(){},removeEventListener(){} }, isStandaloneEditor,
  router: { currentRoute: { value: { path: '/' } } }, ...monitorLayout, watch() {},
  ref: value => ({ value }), computed: get => ({ get value() { return get(); } }),
  requestAnimationFrame(fn) { const id = ++frameId; frames.set(id, fn); return id; },
  cancelAnimationFrame(id) { frames.delete(id); },
  onMounted: fn => mounted.push(fn), onUnmounted: fn => unmounted.push(fn),
 });
 mounted.forEach(fn => fn());
 return { ...exports, isStandaloneEditor, root, frames,
  resizeNative(w, h) { target.dispatchEvent(new CustomEvent('ipc:window.resized', { detail: { w, h } })); },
  resizeDpr(value){target.devicePixelRatio=value;target.dispatchEvent(new Event('resize'));},
  resizeDom(w, h) { target.innerWidth = root.clientWidth = vv.width = w; target.innerHeight = root.clientHeight = vv.height = h; target.dispatchEvent(new Event('resize')); },
  settle() { let count = 0; while(frames.size) { assert(count++ < 100, 'settling did not stop'); const current = [...frames]; frames.clear(); current.forEach(([, fn]) => fn()); } return count; },
  dispose() { unmounted.forEach(fn => fn()); },
 };
}
await check('standalone editor ignores main-window native size broadcasts', () => {
 const f = scaleFixture(true); const before = f.uiScale.value;
 f.resizeNative(1071, 1440); f.settle(); assert.equal(f.nativeViewport.value, null); assert.equal(f.uiScale.value, before);
 assert.equal(f.scalerStyle.value.zoom, 1); assert.equal(Number(f.root.style.scale), before); f.dispose();
});
await check('standalone editor follows its own shrinking DOM viewport', () => {
 const f = scaleFixture(true); f.resizeNative(1071, 1440); f.resizeDom(480, 540); f.settle();
 assert.equal(f.viewportSize().w, 480); assert.equal(f.viewportSize().h, 540); assert.equal(f.uiScale.value, 480 / 580); f.dispose();
});
await check('standalone route never consumes a previously cached main viewport', () => {
 const f = scaleFixture(); f.resizeNative(1071, 1440); f.isStandaloneEditor.value = true; f.resizeDom(480, 540); f.settle();
 assert.equal(f.viewportSize().w, 480); assert.equal(f.uiScale.value, 480 / 580); f.dispose();
});
await check('main renderer uses DPI-converted native size only during bounded DOM recovery', () => {
 const f = scaleFixture(false, 2); f.resizeNative(1600, 2160);
 assert.equal(f.viewportSize().w, 800); assert.equal(f.viewportSize().h, 1080); assert.equal(f.uiScale.value, 800 / 580);
 assert(Number.parseFloat(f.scalerStyle.value.height) * f.uiScale.value >= 1080-1e-6);
 f.settle();assert.equal(f.viewportSize().w,640);assert.equal(f.viewportSize().h,720);f.dispose();
});
await check('main renderer accepts a smaller native size after reflow', () => {
 const f = scaleFixture(); f.resizeNative(1071, 1440); f.resizeDom(480, 540); f.resizeNative(480, 540); f.settle();
 assert.equal(f.viewportSize().w, 480); assert.equal(f.uiScale.value, 480 / 580); f.dispose();
});
await check('old large native sample expires without a replacement event',()=>{
 const f=scaleFixture();f.resizeNative(1920,1200);f.resizeDom(595,800);f.settle();assert.equal(f.viewportSize().w,595);assert.equal(f.viewportSize().h,800);f.dispose();
});
await check('rotations use consistent DOM axes and settle in both directions',()=>{
 const f=scaleFixture(false,1.5);for(const [w,h]of[[595,800],[800,1076],[595,800],[300,400],[595,800]]){f.resizeNative(2400,3840);f.resizeDom(w,h);f.settle();assert.equal(f.viewportSize().w,w);assert.equal(f.viewportSize().h,h);assert(Math.abs(Number.parseFloat(f.scalerStyle.value.height)*f.uiScale.value-h)<1e-6);}f.dispose();
});
await check('large DPR changes do not reinterpret a cached physical sample',()=>{
 const f=scaleFixture(false,1.5);f.resizeNative(892,1200);for(const dpr of[5,4,3,2,1.5,1]){f.resizeDpr(dpr);const w=Math.round(892/dpr),h=Math.round(1200/dpr);f.resizeDom(w,h);f.settle();assert.equal(f.viewportSize().w,w);assert.equal(f.viewportSize().h,h);}f.dispose();
});
await check('zero DOM observations preserve a valid fallback and recover when visible',()=>{
 const f=scaleFixture();f.resizeNative(595,800);f.resizeDom(0,0);f.settle();assert(f.viewportSize().w>0&&f.viewportSize().h>0);f.resizeDom(595,800);f.settle();assert.equal(f.viewportSize().w,595);assert.equal(f.viewportSize().h,800);f.dispose();
});
await check('scale unmount cancels settling and removes resize listeners', () => {
 const f = scaleFixture(); assert(f.frames.size > 0); f.dispose(); assert.equal(f.frames.size, 0);
 const before = f.uiScale.value; f.resizeNative(1600, 2160); f.resizeDom(480, 540); assert.equal(f.uiScale.value, before); assert.equal(f.frames.size, 0);
});

await check('stable viewport stops its frame checks early and never rewrites equal CSS scale',()=>{
 const f=scaleFixture();assert(f.settle()<=8);const writes=f.root.style.writes;f.settleScale();assert(f.settle()<=8);assert.equal(f.root.style.writes,writes);assert.equal(f.frames.size,0);
 f.resizeNative(640,720);assert(f.settle()<=10);assert.equal(f.root.style.writes,writes);f.resizeDom(800,760);assert(f.uiScale.value>1.2);assert.equal(f.root.style.writes,writes+1);f.dispose();
});
await check('native summon/maximize paths resample and cancel bounded taskbar checks', () => {
 const show = block(native, 'static void showWindowAnimated(HWND hwnd, int showCmd, bool activate) {');
 const showing = show.indexOf('if (showCmd == SW_MINIMIZE || showCmd == SW_MAXIMIZE');
 assert(show.indexOf('focusReflowMainWindow();') < showing, 'missing pre-show sample');
 assert(show.indexOf('focusReflowMainWindow();', showing) > showing, 'missing post-show sample');
 assert(show.includes('scheduleTaskbarLayoutRecheck();'));
 assert(block(native, 'static void bringToFront(HWND hwnd, bool maximizeWindow = false) {').includes('showWindowAnimated(hwnd,'));
 assert(block(native, 'static void hideWindowAnimated(HWND hwnd) {').includes('stopTaskbarLayoutObserver();'));
 const wnd = block(native, 'static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {');
 assert(wnd.includes('if (w == TASKBAR_LAYOUT_RECHECK_TIMER_ID)'));
 assert(wnd.includes('case WM_SYSCOMMAND:') && wnd.includes('scheduleTaskbarLayoutRecheck();'));
 assert(wnd.slice(wnd.indexOf('case WM_DESTROY:')).includes('stopTaskbarLayoutObserver();'));
 assert(!block(native, 'static bool applyFullHeightLayout(').includes('ABS_AUTOHIDE'));
 assert(wnd.includes('setting == "ConvertibleSlateMode" || setting == "SystemDockMode"'));
 assert(block(native,'static void focusScheduleDisplayReflow() {').includes('cancelTaskbarLayoutRecheck();'));
 assert(!block(native,'static void focusScheduleDisplayReflow() {').includes('scheduleTaskbarLayoutRecheck();'));
 assert(wnd.slice(wnd.indexOf('case WM_NCCALCSIZE:')).includes('safeMonitorArea(mon, area)'));
});

const outputArg=process.argv.indexOf('--output-root');
const configured=outputArg>=0?process.argv[outputArg+1]:path.join(root,'Build','Validation','WindowPlacement');
const output=path.resolve(configured),run=path.join(output,`${Date.now()}-${process.pid}`);
assert(!path.relative(output,run).startsWith('..'));fs.mkdirSync(run,{recursive:true});
let nativeCases=0;
if(process.argv.includes('--native')){
 assert.equal(process.platform,'win32','Native regression requires Windows/MSVC');
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vcvars),'MSVC x64 tools missing');
 const helper=block(native,'static bool windowPlacementIsLeft() {');
 const layout=block(native,'static bool applyFullHeightLayout(');
 const editorWidth=read('native/controller_editor_width.h').replace('#include <windows.h>','');
 const covers=block(native,'static bool focusWindowCoversMonitor(HWND hwnd, RECT monitorRect) {');
 const looksFullscreen=block(native,'static bool focusWindowLooksFullscreen(HWND hwnd, RECT monitorRect) {');
 const ipc=block(native,'ipc_on("window.place",')+');';
 const reflow=block(native,'static void focusReflowMainWindow() {');
 const cancel=block(native,'static void cancelTaskbarLayoutRecheck() {');
 const schedule=block(native,'static void scheduleTaskbarLayoutRecheck() {');
 const tick=block(native,'static void taskbarLayoutRecheckTick() {');
 const observed=block(native,'static void taskbarLayoutObserved() {');
 const stopObserver=block(native,'static void stopTaskbarLayoutObserver() {');
 const displayTick=block(native,'static void taskbarDisplayReflowTick() {');
 const displaySchedule=block(native,'static void focusScheduleDisplayReflow() {');
 const cpp=`#include <algorithm>
#include <cmath>
#include <cwchar>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
using HWND=void*; using HMONITOR=void*; using BOOL=int;using HWINEVENTHOOK=void*;
#define CALLBACK
constexpr int TRUE=1;using LONG=long;using UINT_PTR=unsigned long long;
constexpr int FALSE=0,MONITOR_DEFAULTTONEAREST=2,SWP_NOSIZE=1,SWP_NOZORDER=4,SWP_NOACTIVATE=16;
struct RECT{long left=0,top=0,right=0,bottom=0;};
struct MONITORINFO{size_t size;RECT rcMonitor,rcWork;};
using DWORD=unsigned long;using UINT=unsigned int;using LPARAM=long long;
constexpr DWORD ABM_GETSTATE=4,ABS_AUTOHIDE=1,ABM_GETAUTOHIDEBAREX=11;constexpr UINT ABE_LEFT=0,ABE_TOP=1,ABE_RIGHT=2,ABE_BOTTOM=3;
struct APPBARDATA{DWORD cbSize;HWND hWnd;UINT uCallbackMessage;UINT uEdge;RECT rc;LPARAM lParam;};
HWND g_hwnd=reinterpret_cast<HWND>(1);bool g_frameless=true,mainVisible=true,g_fullHeight=true,zoomed=false,iconic=false,monitorOk=true,rectOk=true,posOk=true,settingsOk=true;
bool trayExists=false,trayVisible=true,trayGeometryOk=true,enumOk=true,secondaryExists=false,secondaryVisible=true;
RECT trayRect{0,1040,1920,1080},secondaryRect{0,1040,1920,1080};
HMONITOR trayMonitor=reinterpret_cast<HMONITOR>(1),secondaryMonitor=reinterpret_cast<HMONITOR>(2);
UINT autoHideEdge=ABE_BOTTOM;bool secondaryAutoHide=false;HMONITOR queriedMonitor=nullptr;
DWORD appbarState=0;RECT monitor{0,0,1920,1080};
int g_baseW=580,g_baseH=780,moves=0,lastFlags=0;HMONITOR currentMonitor=reinterpret_cast<HMONITOR>(1),targetMonitor=currentMonitor;
RECT work{0,0,1920,1080},windowRect{0,0,580,780};json settings=json::object();
json ymSettingsSection(const char*){if(!settingsOk)throw std::runtime_error("settings unavailable");return settings;}
BOOL IsWindow(HWND h){return h!=nullptr;}BOOL IsZoomed(HWND){return zoomed;}BOOL IsIconic(HWND){return iconic;}
HWND FindWindowW(const wchar_t*,const wchar_t*){return trayExists?reinterpret_cast<HWND>(2):nullptr;}
BOOL IsWindowVisible(HWND h){return h==g_hwnd?mainVisible:h==reinterpret_cast<HWND>(5)?secondaryVisible:trayVisible;}
BOOL EnumWindows(BOOL(CALLBACK* cb)(HWND,LPARAM),LPARAM value){if(!enumOk)return FALSE;if(trayExists)cb(reinterpret_cast<HWND>(2),value);if(secondaryExists)cb(reinterpret_cast<HWND>(5),value);return TRUE;}
int GetClassNameW(HWND h,wchar_t* name,int){wcscpy_s(name,64,h==reinterpret_cast<HWND>(9)?L"UnrelatedWindow":h==reinterpret_cast<HWND>(5)?L"Shell_SecondaryTrayWnd":L"Shell_TrayWnd");return 1;}
constexpr LONG OBJID_WINDOW=0,CHILDID_SELF=0;constexpr DWORD WINEVENT_OUTOFCONTEXT=0,WINEVENT_SKIPOWNPROCESS=2,EVENT_OBJECT_SHOW=0x8002,EVENT_OBJECT_HIDE=0x8003,EVENT_OBJECT_LOCATIONCHANGE=0x800B;
using EventCallback=void(CALLBACK*)(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD);
bool hooksOk=false,failLocationHook=false;int hookArms=0,hookStops=0;
DWORD GetWindowThreadProcessId(HWND,DWORD* process){*process=123;return 456;}
HWINEVENTHOOK SetWinEventHook(DWORD event,DWORD,void*,EventCallback,DWORD process,DWORD,DWORD){if(!hooksOk||(failLocationHook&&event==EVENT_OBJECT_LOCATIONCHANGE))return nullptr;if(process!=123)throw std::runtime_error("hook not process scoped");return reinterpret_cast<HWINEVENTHOOK>(static_cast<UINT_PTR>(100+(++hookArms)));}
BOOL UnhookWinEvent(HWINEVENTHOOK){++hookStops;return TRUE;}
UINT_PTR SHAppBarMessage(DWORD message,APPBARDATA* data){if(message==ABM_GETSTATE)return appbarState;if(message!=ABM_GETAUTOHIDEBAREX||!data||data->uEdge!=autoHideEdge)return 0;if(trayExists&&trayMonitor==queriedMonitor&&(appbarState&ABS_AUTOHIDE))return reinterpret_cast<UINT_PTR>(reinterpret_cast<HWND>(2));if(secondaryExists&&secondaryMonitor==queriedMonitor&&secondaryAutoHide)return reinterpret_cast<UINT_PTR>(reinterpret_cast<HWND>(5));return 0;}
using QUERY_USER_NOTIFICATION_STATE=int;constexpr int QUNS_NOT_PRESENT=1,QUNS_BUSY=2,QUNS_RUNNING_D3D_FULL_SCREEN=3;int qunsState=QUNS_NOT_PRESENT;
bool SUCCEEDED(long value){return value>=0;}
long SHQueryUserNotificationState(QUERY_USER_NOTIFICATION_STATE* state){*state=qunsState;return 0;}
struct FocusTargetSnapshot{HWND hwnd=nullptr;bool valid=false,fullscreen=false;};
struct FocusSessionState{FocusTargetSnapshot target;};
FocusSessionState g_focusSession;
HWND g_foregroundWindow=nullptr;RECT gameRect{0,0,1920,1080};bool frameOk=true,gameRectOk=true;
using LONG_PTR=long long;constexpr int GWL_STYLE=-16,DWMWA_EXTENDED_FRAME_BOUNDS=9;
constexpr LONG_PTR WS_POPUP=0x80000000LL,WS_CAPTION=0x00C00000LL;LONG_PTR gameStyle=WS_POPUP;
bool FAILED(long value){return value<0;}
long DwmGetWindowAttribute(HWND,int,RECT* rect,size_t){*rect=gameRect;return frameOk?0:-1;}
UINT GetDpiForWindow(HWND){return 96;}
int MulDiv(int a,int b,int c){return a*b/c;}
LONG_PTR GetWindowLongPtrW(HWND,int){return gameStyle;}
HWND GetForegroundWindow(){return g_foregroundWindow;}
HMONITOR focusResolveMonitor(const FocusTargetSnapshot&){return targetMonitor;}
HMONITOR MonitorFromWindow(HWND h,int){return h==reinterpret_cast<HWND>(2)?trayMonitor:h==reinterpret_cast<HWND>(5)?secondaryMonitor:currentMonitor;}HMONITOR focusCurrentTargetMonitor(){return targetMonitor;}
BOOL GetMonitorInfoW(HMONITOR mon,MONITORINFO* mi){queriedMonitor=mon;mi->rcWork=work;mi->rcMonitor=monitor;return monitorOk;}
BOOL GetWindowRect(HWND hwnd,RECT* rect){if(hwnd==reinterpret_cast<HWND>(2)){*rect=trayRect;return trayGeometryOk;}if(hwnd==reinterpret_cast<HWND>(5)){*rect=secondaryRect;return true;}if(hwnd!=g_hwnd){*rect=gameRect;return gameRectOk;}*rect=windowRect;return rectOk;}
BOOL SetWindowPos(HWND,void*,int x,int y,int w,int h,int flags){++moves;lastFlags=flags;if(!posOk)return FALSE;int width=windowRect.right-windowRect.left,height=windowRect.bottom-windowRect.top;if(!(flags&SWP_NOSIZE)){width=w;height=h;}windowRect={x,y,x+width,y+height};return 1;}
std::function<json(const json&)> place;
template<class F> void ipc_on(const char*,F fn){place=fn;}
${helper}
${editorWidth}
ymcc::editorwidth::Session g_controllerEditorWidth;
#include "${path.join(root,'native/window_taskbar_layout.h').replaceAll('\\','/')}"
constexpr UINT_PTR TASKBAR_LAYOUT_RECHECK_TIMER_ID=0x5A36,FOCUS_DISPLAY_TIMER_ID=0x5A34;
ymcc::windowlayout::TaskbarRecheck g_taskbarLayoutRecheck;
ymcc::windowlayout::TaskbarEvents g_taskbarLayoutEvents;
bool timerOk=true;int timerArms=0,timerKills=0;UINT timerInterval=0;
UINT_PTR SetTimer(HWND,UINT_PTR id,UINT delay,void*){++timerArms;timerInterval=delay;return timerOk?id:0;}
BOOL KillTimer(HWND,UINT_PTR){++timerKills;return TRUE;}
${covers}
${looksFullscreen}
${layout}
${reflow}
${cancel}
static void focusScheduleDisplayReflow();
${observed}
${stopObserver}
${schedule}
${tick}
${displayTick}
${displaySchedule}
void registerPlace(){${ipc}}
void expect(bool condition,const char* name){if(!condition)throw std::runtime_error(name);}
int main(){try{registerPlace();int tests=0;
 auto run=[&](const char* name,auto fn){g_taskbarLayoutEvents.stop();hooksOk=false;failLocationHook=false;trayExists=false;trayVisible=true;trayGeometryOk=true;enumOk=true;secondaryExists=false;secondaryVisible=true;secondaryRect={0,1040,1920,1080};secondaryAutoHide=false;appbarState=0;autoHideEdge=ABE_BOTTOM;trayMonitor=currentMonitor;trayRect={0,1040,1920,1080};fn();++tests;std::cout<<"PASS "<<name<<"\\n";};
 run("default right startup",[&]{expect(applyFullHeightLayout(),"layout result");expect(windowRect.left==1117,"default right coordinate");});
 run("saved left startup",[&]{settings["windowPlacement"]="left";expect(applyFullHeightLayout(),"left layout");expect(windowRect.left==0,"left coordinate");});
 run("repeated DPI/display/wake reflow retains left",[&]{for(int i=0;i<5;++i)expect(applyFullHeightLayout(targetMonitor),"reflow");expect(windowRect.left==0,"left reflow");});
 run("taskbar work area left offset",[&]{work={48,0,1920,1040};expect(applyFullHeightLayout(),"work area");expect(windowRect.left==48&&windowRect.top==0,"work offset");});
 run("negative-origin secondary monitor",[&]{work={-1920,-200,0,880};expect(applyFullHeightLayout(reinterpret_cast<HMONITOR>(2)),"secondary");expect(windowRect.left==-1920&&windowRect.top==-200,"secondary coordinates");});
 run("physical DPI-sized work area",[&]{work={0,0,2560,1440};expect(applyFullHeightLayout(),"dpi area");expect(windowRect.left==0&&windowRect.right==1071&&windowRect.bottom==1440,"physical dimensions");});
 run("portrait narrow-screen protection",[&]{work={0,0,900,1600};expect(applyFullHeightLayout(),"portrait");expect(windowRect.left==0&&windowRect.right==900&&windowRect.top==195,"portrait dimensions");});
 run("hidden taskbar extends the window to the monitor bottom",[&]{settings["windowPlacement"]="left";trayExists=true;trayVisible=false;appbarState=0;work={0,0,1920,1040};monitor={0,0,1920,1080};expect(applyFullHeightLayout(),"hidden tray");expect(windowRect.top==0&&windowRect.bottom==1080,"screen bottom");trayVisible=true;});
 run("auto-hidden taskbar also fills the screen",[&]{settings["windowPlacement"]="left";trayExists=true;trayRect={0,1078,1920,1120};appbarState=ABS_AUTOHIDE;work={0,0,1920,1040};monitor={0,0,1920,1080};expect(applyFullHeightLayout(),"autohide tray");expect(windowRect.top==0&&windowRect.bottom==1080,"autohide bottom");appbarState=0;work={0,0,1920,1080};});
 run("fullscreen notification never overrides a visible taskbar",[&]{settings["windowPlacement"]="left";trayExists=true;trayVisible=true;appbarState=0;work={0,0,1920,1040};monitor={0,0,1920,1080};qunsState=QUNS_NOT_PRESENT;expect(applyFullHeightLayout(),"windowed");expect(windowRect.top==0&&windowRect.bottom==1040,"windowed respects taskbar");qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;g_foregroundWindow=reinterpret_cast<HWND>(4);gameRect=monitor;expect(applyFullHeightLayout(),"fullscreen");expect(windowRect.top==0&&windowRect.bottom==1040,"visible taskbar wins");g_foregroundWindow=nullptr;qunsState=QUNS_NOT_PRESENT;work={0,0,1920,1080};});
 run("pre-summon fullscreen snapshots cannot override the live taskbar",[&]{settings["windowPlacement"]="right";trayExists=true;trayVisible=true;appbarState=0;work={0,0,1920,1040};monitor={0,0,1920,1080};qunsState=QUNS_NOT_PRESENT;g_focusSession.target.valid=true;g_focusSession.target.fullscreen=true;g_focusSession.target.hwnd=reinterpret_cast<HWND>(3);expect(applyFullHeightLayout(),"summon snapshot");expect(windowRect.top==0&&windowRect.bottom==1040,"snapshot cannot hide taskbar");g_focusSession={};g_foregroundWindow=reinterpret_cast<HWND>(4);gameRect=monitor;expect(applyFullHeightLayout(),"live foreground");expect(windowRect.top==0&&windowRect.bottom==1040,"foreground cannot hide taskbar");g_foregroundWindow=nullptr;gameRect={0,0,1920,1080};work={0,0,1920,1080};});

 run("busy notifications without local fullscreen respect the taskbar",[&]{work={0,0,1920,1040};monitor={0,0,1920,1080};qunsState=QUNS_BUSY;expect(applyFullHeightLayout(),"busy layout");expect(windowRect.bottom==1040,"busy is not fullscreen");qunsState=QUNS_NOT_PRESENT;work=monitor;});
 run("global D3D notification state alone does not hide the local taskbar",[&]{work={0,0,1920,1040};qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;expect(applyFullHeightLayout(),"global D3D");expect(windowRect.bottom==1040,"global state is not monitor-bound");qunsState=QUNS_NOT_PRESENT;work=monitor;});
 run("fullscreen foreground on another monitor preserves the local work area",[&]{work={0,0,1920,1040};g_foregroundWindow=reinterpret_cast<HWND>(4);gameRect={1920,0,3840,1080};qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;expect(applyFullHeightLayout(),"other foreground monitor");expect(windowRect.bottom==1040,"other monitor foreground");g_foregroundWindow=nullptr;gameRect=monitor;qunsState=QUNS_NOT_PRESENT;work=monitor;});
 run("fullscreen snapshot is scoped to its resolved monitor",[&]{work={0,0,1920,1040};g_focusSession.target={reinterpret_cast<HWND>(3),true,true};targetMonitor=reinterpret_cast<HMONITOR>(2);expect(applyFullHeightLayout(currentMonitor),"other snapshot monitor");expect(windowRect.bottom==1040,"other monitor snapshot");g_focusSession={};targetMonitor=currentMonitor;work=monitor;});
 run("maximized desktop app on a high-resolution monitor is not fullscreen",[&]{monitor={0,0,3840,2560};work={0,0,3840,2512};gameRect=work;gameStyle=WS_CAPTION;g_foregroundWindow=reinterpret_cast<HWND>(4);expect(!focusWindowCoversMonitor(g_foregroundWindow,monitor),"98 percent is not full coverage");expect(!focusWindowLooksFullscreen(g_foregroundWindow,monitor),"maximized classification");expect(applyFullHeightLayout(),"maximized foreground");expect(windowRect.bottom==2512,"visible taskbar remains excluded");qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;expect(!focusWindowLooksFullscreen(g_foregroundWindow,monitor),"global D3D cannot override incomplete edges");qunsState=QUNS_NOT_PRESENT;g_foregroundWindow=nullptr;gameStyle=WS_POPUP;monitor={0,0,1920,1080};work=monitor;gameRect=monitor;});
 run("borderless fullscreen without a taskbar observation keeps the work area",[&]{work={0,0,1920,1040};gameRect=monitor;g_foregroundWindow=reinterpret_cast<HWND>(4);expect(focusWindowLooksFullscreen(g_foregroundWindow,monitor),"borderless fullscreen");expect(applyFullHeightLayout(),"borderless layout");expect(windowRect.bottom==1040,"unknown taskbar stays conservative");g_foregroundWindow=nullptr;work=monitor;});
 run("fullscreen on a negative-origin monitor uses that monitor edges",[&]{monitor={-1920,-200,0,880};work={-1920,-200,0,840};gameRect=monitor;g_foregroundWindow=reinterpret_cast<HWND>(4);expect(applyFullHeightLayout(),"negative-origin fullscreen");expect(windowRect.top==-200&&windowRect.bottom==840,"negative-origin work-area bounds");g_foregroundWindow=nullptr;monitor={0,0,1920,1080};work=monitor;gameRect=monitor;});
 run("fullscreen frame rounding and spanning-monitor windows are accepted",[&]{HWND game=reinterpret_cast<HWND>(4);gameRect={2,2,1918,1078};expect(focusWindowCoversMonitor(game,monitor),"frame rounding");gameRect={-1920,0,1920,1080};expect(focusWindowCoversMonitor(game,monitor),"spanning monitor");gameRect={0,0,1920,1040};expect(!focusWindowCoversMonitor(game,monitor),"missing taskbar edge");expect(!focusWindowCoversMonitor(game,RECT{}),"empty monitor");gameRect=monitor;});
 run("fullscreen geometry failure falls back safely",[&]{HWND game=reinterpret_cast<HWND>(4);gameRect=monitor;frameOk=false;expect(focusWindowCoversMonitor(game,monitor),"window rect fallback");gameRectOk=false;expect(!focusWindowCoversMonitor(game,monitor),"both geometry calls failed");frameOk=true;gameRectOk=true;});

 run("Windows 11 transparent taskbar padding does not shrink the reserved bottom work area",[&]{trayExists=true;monitor={0,0,3840,2160};work={0,0,3840,2064};trayRect={0,2016,3840,2160};for(const char* side:{"left","right"}){settings["windowPlacement"]=side;expect(applyFullHeightLayout(),"padded bottom taskbar");expect(windowRect.top==0&&windowRect.bottom==2064,"transparent 48-pixel gap");expect(windowRect.right-windowRect.left==1535,"work-area aspect ratio");expect(side==std::string("left")?windowRect.left==0:windowRect.right==3840,"saved side");}int before=moves;for(int i=0;i<6;++i)focusReflowMainWindow();expect(moves==before,"padded taskbar resize storm");monitor={0,0,1920,1080};work=monitor;});
 run("transparent taskbar padding respects reserved work-area edges on all four sides",[&]{trayExists=true;monitor={0,0,1920,1080};const RECT workAreas[]={{0,0,1920,1040},{0,40,1920,1080},{40,0,1920,1080},{0,0,1880,1080}};const RECT trays[]={{0,1020,1920,1080},{0,0,1920,60},{0,0,60,1080},{1860,0,1920,1080}};for(int i=0;i<4;++i){work=workAreas[i];trayRect=trays[i];RECT area{};expect(ymcc::windowlayout::safeMonitorArea(currentMonitor,area),"padded edge");expect(ymcc::windowlayout::sameRect(area,work),"transparent padding changed work-area edge");}work=monitor;});
 run("secondary monitor transparent taskbar padding keeps negative physical work-area coordinates",[&]{monitor={-3840,-200,0,1960};work={-3840,-200,0,1864};secondaryExists=true;secondaryVisible=true;secondaryMonitor=currentMonitor;secondaryRect={-3840,1816,0,1960};expect(applyFullHeightLayout(),"secondary padded bar");expect(windowRect.top==-200&&windowRect.bottom==1864,"secondary transparent gap");monitor={0,0,1920,1080};work=monitor;secondaryMonitor=reinterpret_cast<HMONITOR>(2);secondaryRect={0,1040,1920,1080};});
 run("maximized frameless windows also ignore transparent taskbar padding",[&]{trayExists=true;work={0,0,1920,1040};trayRect={0,1020,1920,1080};g_fullHeight=false;zoomed=true;focusReflowMainWindow();expect(ymcc::windowlayout::sameRect(windowRect,work),"maximized transparent gap");zoomed=false;g_fullHeight=true;work=monitor;});
 run("auto-hide activation-only work-area reservations still avoid an expanded taskbar",[&]{trayExists=true;const RECT workAreas[]={{0,0,1920,1078},{0,2,1920,1080},{2,0,1920,1080},{0,0,1918,1080}};const RECT trays[]={{0,1040,1920,1080},{0,0,1920,40},{0,0,40,1080},{1880,0,1920,1080}};const RECT expected[]={{0,0,1920,1040},{0,40,1920,1080},{40,0,1920,1080},{0,0,1880,1080}};for(int i=0;i<4;++i){work=workAreas[i];trayRect=trays[i];RECT area{};expect(ymcc::windowlayout::safeMonitorArea(currentMonitor,area),"activation strip");expect(ymcc::windowlayout::sameRect(area,expected[i]),"expanded taskbar covered");}work=monitor;});
 run("a reservation on another edge does not suppress live taskbar clipping",[&]{trayExists=true;work={48,0,1920,1080};trayRect={0,1040,1920,1080};RECT area{};expect(ymcc::windowlayout::safeMonitorArea(currentMonitor,area),"different edge");expect(area.left==48&&area.bottom==1040,"different-edge reservation");work=monitor;});
 run("visible auto-hide taskbar overlays final full-height size without shrinking",[&]{trayExists=true;appbarState=ABS_AUTOHIDE;work=monitor;expect(applyFullHeightLayout(),"open auto-hide");expect(windowRect.bottom==1080,"open auto-hide must not resize");appbarState=0;});
 run("registered auto-hide uses one final size through every animation frame",[&]{trayExists=true;appbarState=ABS_AUTOHIDE;work={0,0,1920,1040};windowRect={0,0,580,780};settings["windowPlacement"]="right";int before=moves;for(int y:{1040,1045,1055,1065,1078,1080,1060,1040}){trayRect={0,y,1920,y+40};expect(applyFullHeightLayout(),"animated auto-hide");expect(windowRect.bottom==1080,"animated target changed");}expect(moves==before+1,"animated auto-hide moved more than once");work=monitor;});
 run("auto-hide reclaims its edge only on all four sides",[&]{trayExists=true;appbarState=ABS_AUTOHIDE;monitor={0,0,1920,1080};for(UINT edge=0;edge<4;++edge){autoHideEdge=edge;const RECT edges[]={{0,0,40,1080},{0,0,1920,40},{1880,0,1920,1080},{0,1040,1920,1080}};trayRect=edges[edge];work={9,10,1911,1070};RECT expected=work;if(edge==ABE_LEFT)expected.left=0;else if(edge==ABE_TOP)expected.top=0;else if(edge==ABE_RIGHT)expected.right=1920;else expected.bottom=1080;RECT area{};expect(ymcc::windowlayout::safeMonitorArea(currentMonitor,area),"auto-hide edge");expect(ymcc::windowlayout::sameRect(area,expected),"unrelated appbar edge changed");}work=monitor;});
 run("secondary auto-hide is scoped to its own negative-origin monitor",[&]{secondaryExists=true;secondaryAutoHide=true;secondaryMonitor=currentMonitor;monitor={-1920,-200,0,880};work={-1920,-200,0,840};secondaryRect={-1920,840,0,880};expect(applyFullHeightLayout(),"secondary auto-hide");expect(windowRect.bottom==880&&windowRect.top==-200,"secondary animation reserved");monitor={0,0,1920,1080};work=monitor;secondaryMonitor=reinterpret_cast<HMONITOR>(2);});
 run("auto-hide on another monitor and a global flag cannot hide local normal bar",[&]{trayExists=true;trayMonitor=reinterpret_cast<HMONITOR>(2);appbarState=ABS_AUTOHIDE;secondaryExists=true;secondaryMonitor=currentMonitor;secondaryVisible=true;work=monitor;expect(applyFullHeightLayout(),"local bar");expect(windowRect.bottom==1040,"remote auto-hide leaked");secondaryMonitor=reinterpret_cast<HMONITOR>(2);});
 run("turning auto-hide off restores normal taskbar reservation once",[&]{trayExists=true;appbarState=ABS_AUTOHIDE;work={0,0,1920,1040};expect(applyFullHeightLayout(),"auto-hide on");int before=moves;appbarState=0;expect(applyFullHeightLayout(),"auto-hide off");expect(windowRect.bottom==1040&&moves==before+1,"disabled auto-hide wrong target");work=monitor;});
 run("stale work area is clipped by live taskbar bounds",[&]{trayExists=true;work=monitor;trayRect={0,1020,1920,1080};expect(applyFullHeightLayout(),"stale work");expect(windowRect.bottom==1020,"actual bar geometry");});
 run("hidden primary taskbar cannot hide a visible secondary taskbar",[&]{trayExists=true;trayVisible=false;secondaryExists=true;targetMonitor=secondaryMonitor;work=monitor;expect(applyFullHeightLayout(targetMonitor),"secondary layout");expect(windowRect.bottom==1040,"secondary taskbar reserved");targetMonitor=currentMonitor;});
 run("primary taskbar on another monitor does not change local work area",[&]{trayExists=true;trayVisible=false;trayMonitor=secondaryMonitor;work={0,0,1920,1040};expect(applyFullHeightLayout(),"other primary");expect(windowRect.bottom==1040,"other primary ignored");});
 run("offscreen taskbar retains visible style but is actually hidden",[&]{trayExists=true;trayRect={0,1080,1920,1120};work={0,0,1920,1040};expect(applyFullHeightLayout(),"offscreen");expect(windowRect.bottom==1080,"offscreen reclaim");});
 run("top taskbar is avoided with stale work area",[&]{trayExists=true;trayRect={0,0,1920,48};work=monitor;expect(applyFullHeightLayout(),"top bar");expect(windowRect.top==48&&windowRect.bottom==1080,"top bar reserved");});
 run("left taskbar is avoided with stale work area",[&]{trayExists=true;trayRect={0,0,48,1080};work=monitor;settings["windowPlacement"]="left";expect(applyFullHeightLayout(),"left bar");expect(windowRect.left==48,"left bar reserved");});
 run("right taskbar is avoided with stale work area",[&]{trayExists=true;trayRect={1872,0,1920,1080};work=monitor;settings["windowPlacement"]="right";expect(applyFullHeightLayout(),"right bar");expect(windowRect.right==1872,"right bar reserved");});
 run("live taskbar bounds on a negative-origin screen stay in physical coordinates",[&]{trayExists=true;monitor={-1920,-200,0,880};work=monitor;trayRect={-1920,840,0,880};expect(applyFullHeightLayout(),"negative tray");expect(windowRect.top==-200&&windowRect.bottom==840,"negative visible bar");monitor={0,0,1920,1080};work=monitor;});
 run("unknown taskbar and enumeration failure keep conservative work area",[&]{work={0,0,1920,1040};expect(applyFullHeightLayout(),"missing bar");expect(windowRect.bottom==1040,"missing bar fallback");trayExists=true;trayVisible=false;enumOk=false;expect(applyFullHeightLayout(),"failed enum");expect(windowRect.bottom==1040,"failed enum fallback");});
 run("failed taskbar geometry does not reclaim reserved space",[&]{trayExists=true;trayGeometryOk=false;work={0,0,1920,1040};expect(applyFullHeightLayout(),"failed rect");expect(windowRect.bottom==1040,"failed rect fallback");});
 run("rechecking unchanged bounds does not generate resize storms",[&]{work=monitor;expect(applyFullHeightLayout(),"initial layout");int before=moves;for(int i=0;i<6;++i)expect(applyFullHeightLayout(),"no-op layout");expect(moves==before,"unchanged layout resized");});
 run("bounded post-summon recheck catches a taskbar that appears later",[&]{trayExists=true;trayVisible=false;work={0,0,1920,1040};mainVisible=true;g_fullHeight=true;scheduleTaskbarLayoutRecheck();taskbarLayoutRecheckTick();expect(windowRect.bottom==1080,"initial hidden");trayVisible=true;taskbarLayoutRecheckTick();expect(windowRect.bottom==1040,"late visible correction");for(int i=0;i<4;++i)taskbarLayoutRecheckTick();expect(!g_taskbarLayoutRecheck.active(),"recheck ends after six passes");expect(timerInterval==200,"bounded interval");int before=moves;taskbarLayoutRecheckTick();expect(moves==before,"stale timer moved window");});
 run("hide/minimize/restore cancel stale taskbar corrections",[&]{for(int kind=0;kind<3;++kind){mainVisible=true;iconic=false;g_fullHeight=true;scheduleTaskbarLayoutRecheck();if(kind==0)mainVisible=false;if(kind==1)iconic=true;if(kind==2){g_fullHeight=false;zoomed=false;}int before=moves;taskbarLayoutRecheckTick();expect(!g_taskbarLayoutRecheck.active()&&moves==before,"inactive window corrected");}mainVisible=true;iconic=false;g_fullHeight=true;});
 run("ordinary frameless maximize also corrects live taskbar bounds",[&]{trayExists=true;work=monitor;mainVisible=true;g_fullHeight=false;zoomed=true;scheduleTaskbarLayoutRecheck();taskbarLayoutRecheckTick();expect(windowRect.left==0&&windowRect.right==1920&&windowRect.bottom==1040,"maximized work area");cancelTaskbarLayoutRecheck();zoomed=false;g_fullHeight=true;});
 run("a new summon resets the finite recheck budget",[&]{scheduleTaskbarLayoutRecheck();taskbarLayoutRecheckTick();expect(g_taskbarLayoutRecheck.remaining==5,"first pass");scheduleTaskbarLayoutRecheck();expect(g_taskbarLayoutRecheck.remaining==6,"new summon restart");cancelTaskbarLayoutRecheck();});
 run("timer creation failure does not leave a pending recheck",[&]{timerOk=false;scheduleTaskbarLayoutRecheck();expect(!g_taskbarLayoutRecheck.active(),"failed timer pending");timerOk=true;});
 run("visible observer replaces repeated sampling with one post-show fallback",[&]{trayExists=true;hooksOk=true;scheduleTaskbarLayoutRecheck();expect(g_taskbarLayoutRecheck.remaining==1,"observer still polls six times");taskbarLayoutRecheckTick();expect(!g_taskbarLayoutRecheck.active(),"fallback did not stop");});
 run("tablet expansion events defer layout until final work area and resize once",[&]{trayExists=true;hooksOk=true;work={0,0,1920,1040};expect(applyFullHeightLayout(),"tablet start");scheduleTaskbarLayoutRecheck();int before=moves;for(int bottom:{1035,1025,1015,1000,984}){work.bottom=bottom;trayRect.top=bottom;ymcc::windowlayout::TaskbarEvents::observed(g_taskbarLayoutEvents.location,EVENT_OBJECT_LOCATIONCHANGE,reinterpret_cast<HWND>(2),OBJID_WINDOW,CHILDID_SELF,0,0);expect(moves==before,"tablet animation resized early");expect(timerInterval==250,"tablet event not coalesced");}taskbarDisplayReflowTick();expect(windowRect.bottom==984&&moves==before+1,"tablet did not apply final work area once");taskbarDisplayReflowTick();expect(moves==before+1,"stable tablet moved repeatedly");work=monitor;});
 run("tablet collapse and posture settings use one-shot event reflow",[&]{trayExists=true;hooksOk=true;work={0,0,1920,984};expect(applyFullHeightLayout(),"expanded tablet");work.bottom=1040;trayRect.top=1040;int before=moves;focusScheduleDisplayReflow();expect(moves==before,"posture event resized synchronously");taskbarDisplayReflowTick();expect(windowRect.bottom==1040&&moves==before+1,"collapsed tablet target");work=monitor;});
 run("observer filters non-taskbar and child events and retires old callbacks",[&]{trayExists=true;hooksOk=true;scheduleTaskbarLayoutRecheck();const auto hook=g_taskbarLayoutEvents.location;int arms=timerArms;ymcc::windowlayout::TaskbarEvents::observed(hook,EVENT_OBJECT_LOCATIONCHANGE,reinterpret_cast<HWND>(9),OBJID_WINDOW,CHILDID_SELF,0,0);ymcc::windowlayout::TaskbarEvents::observed(hook,EVENT_OBJECT_LOCATIONCHANGE,reinterpret_cast<HWND>(2),1,CHILDID_SELF,0,0);expect(timerArms==arms,"foreign event armed timer");stopTaskbarLayoutObserver();expect(!g_taskbarLayoutRecheck.active()&&!g_taskbarLayoutEvents.owner,"stop retained owner");ymcc::windowlayout::TaskbarEvents::observed(hook,EVENT_OBJECT_LOCATIONCHANGE,reinterpret_cast<HWND>(2),OBJID_WINDOW,CHILDID_SELF,0,0);expect(timerArms==arms,"old callback rearmed timer");});
 run("partial observer failure removes the first hook and preserves bounded fallback",[&]{trayExists=true;hooksOk=true;failLocationHook=true;int stopped=hookStops;scheduleTaskbarLayoutRecheck();expect(g_taskbarLayoutRecheck.remaining==6,"observer failure has no fallback");expect(!g_taskbarLayoutEvents.owner&&hookStops==stopped+1,"partial hook leaked");cancelTaskbarLayoutRecheck();});
 run("restarted Explorer registration rejects callbacks from the previous hook",[&]{trayExists=true;hooksOk=true;scheduleTaskbarLayoutRecheck();const auto old=g_taskbarLayoutEvents.location;stopTaskbarLayoutObserver();scheduleTaskbarLayoutRecheck();int before=timerArms;ymcc::windowlayout::TaskbarEvents::observed(old,EVENT_OBJECT_LOCATIONCHANGE,reinterpret_cast<HWND>(2),OBJID_WINDOW,CHILDID_SELF,0,0);expect(timerArms==before,"old Explorer hook changed new owner");});
 run("hidden window does not reflow from a queued display timer",[&]{trayExists=true;work={0,0,1920,1040};mainVisible=false;int before=moves;taskbarDisplayReflowTick();expect(moves==before,"hidden window reflow");mainVisible=true;work=monitor;});
 run("explicit IPC right uses full-height layout",[&]{work={0,0,1920,1080};settings["windowPlacement"]="right";expect(place({{"side","right"}})==true,"IPC result");expect(windowRect.left==1117&&windowRect.bottom==1080,"IPC full height");});
 run("invalid IPC side rejected",[&]{bool rejected=false;try{place({{"side","center"}});}catch(...){rejected=true;}expect(rejected,"invalid side");});
 run("maximized/minimized window is not moved",[&]{int before=moves;zoomed=true;expect(place({{"side","left"}})==true,"max result");zoomed=false;iconic=true;expect(place({{"side","left"}})==true,"min result");iconic=false;expect(moves==before,"max/min moved");});
 run("normal left positioning uses work-area physical coordinates",[&]{g_fullHeight=false;work={-1600,40,0,1040};windowRect={0,0,500,700};expect(place({{"side","left"}})==true,"normal left");expect(windowRect.left==-1600&&windowRect.top==190&&windowRect.right==-1100,"normal left coordinates");});
 run("normal right positioning does not activate",[&]{expect(place({{"side","right"}})==true,"normal right");expect(windowRect.left==-500,"normal right coordinate");expect((lastFlags&SWP_NOACTIVATE)!=0,"activation changed");});
 run("Win32 failures return false",[&]{monitorOk=false;expect(place({{"side","left"}})==false,"monitor failure");monitorOk=true;rectOk=false;expect(place({{"side","left"}})==false,"rect failure");rectOk=true;posOk=false;expect(place({{"side","left"}})==false,"position failure");g_fullHeight=true;expect(place({{"side","left"}})==false,"layout failure");posOk=true;});
 run("broken/invalid settings default right without throwing",[&]{settingsOk=false;expect(!windowPlacementIsLeft(),"settings failure");settingsOk=true;settings["windowPlacement"]=42;expect(!windowPlacementIsLeft(),"invalid saved side");});
 run("invalid work area and zero design dimensions rejected",[&]{work={0,0,0,0};expect(!applyFullHeightLayout(),"empty area");work={0,0,1920,1080};g_baseH=0;expect(!applyFullHeightLayout(),"zero design");});
 std::cout<<"NATIVE_CASES="<<tests<<"\\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<"\\n";return 1;}}
`;
 const cppPath=path.join(run,'window-placement-source-selftest.cpp'),exe=path.join(run,'window-placement-source-selftest.exe'),cmd=path.join(run,'compile-and-test.cmd');
 fs.writeFileSync(cppPath,cpp);
 fs.writeFileSync(cmd,`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /I"${path.join(root,'deps/json')}" "${cppPath}" /Fo"${path.join(run,'selftest.obj')}" /Fe"${exe}"\r\nif errorlevel 1 exit /b 1\r\n"${exe}"\r\n`);
 const msvcBase='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC',sdk='C:/Program Files (x86)/Windows Kits/10';
 const newest=directory=>fs.readdirSync(directory).filter(name=>/^\d+\./.test(name)).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}))[0];
 const msvc=path.join(msvcBase,newest(msvcBase)),sdkVersion=newest(path.join(sdk,'Include')),compiler=path.join(msvc,'bin/Hostx64/x64/cl.exe');
 const env={...process.env,INCLUDE:[path.join(msvc,'include'),...['um','shared','ucrt'].map(name=>path.join(sdk,'Include',sdkVersion,name))].join(';'),LIB:[path.join(msvc,'lib/x64'),...['um','ucrt'].map(name=>path.join(sdk,'Lib',sdkVersion,name,'x64'))].join(';'),PATH:[path.join(msvc,'bin/Hostx64/x64'),process.env.PATH].join(';')};
 const build=spawnSync(compiler,['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/I'+path.join(root,'deps/json'),cppPath,'/Fo'+path.join(run,'selftest.obj'),'/Fe'+exe],{cwd:run,env,encoding:'utf8',maxBuffer:8*1024*1024,timeout:45000,windowsHide:true});
 const compiled=build.status===0?spawnSync(exe,[],{cwd:run,encoding:'utf8',maxBuffer:8*1024*1024,timeout:10000,windowsHide:true}):build;
 fs.writeFileSync(path.join(run,'compiler.json'),JSON.stringify({compiler,sdkVersion,buildExit:build.status,compileLog:build.stdout+build.stderr},null,2));
 fs.writeFileSync(path.join(run,'native.log'),(compiled.stdout||'')+(compiled.stderr||''));
 if(compiled.status!==0)throw Error(`Native regression failed (${compiled.status}):\n${compiled.stdout}\n${compiled.stderr}`);
 const match=compiled.stdout.match(/NATIVE_CASES=(\d+)/);assert(match);nativeCases=Number(match[1]);console.log(compiled.stdout.trim());
}
const receipt={ok:true,javascriptCases:cases,nativeCases,scope:'Actual renderer/settings functions replayed with in-memory persistence; native production functions compiled with Win32 fixture stubs. No live desktop/device or installed-product acceptance.'};
fs.writeFileSync(path.join(run,'result.json'),JSON.stringify(receipt,null,2)+'\n');
console.log(JSON.stringify({ok:true,javascriptCases:cases.length,nativeCases,receipt:path.join(run,'result.json')},null,2));
