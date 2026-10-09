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
 const root = { clientWidth: 640, clientHeight: 720, style: { setProperty(_, value) { this.scale = value; } } };
 const isStandaloneEditor = { value: standalone }, mounted = [], unmounted = []; let frameId = 0;
 const start = app.indexOf('const BASE_W ='), end = app.indexOf('// M8: Start button', start);
 const lifecycle = app.slice(app.indexOf('let stageResizeObserver:'), app.indexOf('</script>'));
 const exports = evaluate(app.slice(start, end) + lifecycle + '\nexport { nativeViewport, viewportSize, scalerStyle, uiScale, settleScale };', {
  window: target, document: { documentElement: root }, isStandaloneEditor,
  ref: value => ({ value }), computed: get => ({ get value() { return get(); } }),
  requestAnimationFrame(fn) { const id = ++frameId; frames.set(id, fn); return id; },
  cancelAnimationFrame(id) { frames.delete(id); },
  onMounted: fn => mounted.push(fn), onUnmounted: fn => unmounted.push(fn),
 });
 mounted.forEach(fn => fn());
 return { ...exports, isStandaloneEditor, root, frames,
  resizeNative(w, h) { target.dispatchEvent(new CustomEvent('ipc:window.resized', { detail: { w, h } })); },
  resizeDom(w, h) { target.innerWidth = root.clientWidth = vv.width = w; target.innerHeight = root.clientHeight = vv.height = h; target.dispatchEvent(new Event('resize')); },
  settle() { let count = 0; while(frames.size) { assert(count++ < 100, 'settling did not stop'); const current = [...frames]; frames.clear(); current.forEach(([, fn]) => fn()); } },
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
await check('main renderer uses DPI-converted native size during delayed DOM recovery', () => {
 const f = scaleFixture(false, 2); f.resizeNative(1600, 2160); f.settle();
 assert.equal(f.viewportSize().w, 800); assert.equal(f.viewportSize().h, 1080); assert.equal(f.uiScale.value, 800 / 580);
 assert(Number.parseFloat(f.scalerStyle.value.height) * f.uiScale.value >= 1080); f.dispose();
});
await check('main renderer accepts a smaller native size after reflow', () => {
 const f = scaleFixture(); f.resizeNative(1071, 1440); f.resizeDom(480, 540); f.resizeNative(480, 540); f.settle();
 assert.equal(f.viewportSize().w, 480); assert.equal(f.uiScale.value, 480 / 580); f.dispose();
});
await check('scale unmount cancels settling and removes resize listeners', () => {
 const f = scaleFixture(); assert(f.frames.size > 0); f.dispose(); assert.equal(f.frames.size, 0);
 const before = f.uiScale.value; f.resizeNative(1600, 2160); f.resizeDom(480, 540); assert.equal(f.uiScale.value, before); assert.equal(f.frames.size, 0);
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
 const covers=block(native,'static bool focusWindowCoversMonitor(HWND hwnd, RECT monitorRect) {');
 const looksFullscreen=block(native,'static bool focusWindowLooksFullscreen(HWND hwnd, RECT monitorRect) {');
 const ipc=block(native,'ipc_on("window.place",')+');';
 const cpp=`#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
using HWND=void*; using HMONITOR=void*; using BOOL=int;
constexpr int FALSE=0,MONITOR_DEFAULTTONEAREST=2,SWP_NOSIZE=1,SWP_NOZORDER=4,SWP_NOACTIVATE=16;
struct RECT{long left=0,top=0,right=0,bottom=0;};
struct MONITORINFO{size_t size;RECT rcMonitor,rcWork;};
using DWORD=unsigned long;using UINT=unsigned int;using LPARAM=long long;
constexpr DWORD ABM_GETSTATE=4,ABS_AUTOHIDE=1;
struct APPBARDATA{DWORD cbSize;HWND hWnd;UINT uCallbackMessage;UINT uEdge;RECT rc;LPARAM lParam;};
HWND g_hwnd=reinterpret_cast<HWND>(1);bool g_fullHeight=true,zoomed=false,iconic=false,monitorOk=true,rectOk=true,posOk=true,settingsOk=true;
bool trayExists=true,trayVisible=true;DWORD appbarState=0;RECT monitor{0,0,1920,1080};
int g_baseW=580,g_baseH=780,moves=0,lastFlags=0;HMONITOR currentMonitor=reinterpret_cast<HMONITOR>(1),targetMonitor=currentMonitor;
RECT work{0,0,1920,1080},windowRect{0,0,580,780};json settings=json::object();
json ymSettingsSection(const char*){if(!settingsOk)throw std::runtime_error("settings unavailable");return settings;}
BOOL IsWindow(HWND h){return h!=nullptr;}BOOL IsZoomed(HWND){return zoomed;}BOOL IsIconic(HWND){return iconic;}
HWND FindWindowW(const wchar_t*,const wchar_t*){return trayExists?reinterpret_cast<HWND>(2):nullptr;}
BOOL IsWindowVisible(HWND){return trayVisible;}
DWORD SHAppBarMessage(DWORD,APPBARDATA*){return appbarState;}
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
HMONITOR MonitorFromWindow(HWND,int){return currentMonitor;}HMONITOR focusCurrentTargetMonitor(){return targetMonitor;}
BOOL GetMonitorInfoW(HMONITOR,MONITORINFO* mi){mi->rcWork=work;mi->rcMonitor=monitor;return monitorOk;}
BOOL GetWindowRect(HWND hwnd,RECT* rect){if(hwnd!=g_hwnd){*rect=gameRect;return gameRectOk;}*rect=windowRect;return rectOk;}
BOOL SetWindowPos(HWND,void*,int x,int y,int w,int h,int flags){++moves;lastFlags=flags;if(!posOk)return FALSE;int width=windowRect.right-windowRect.left,height=windowRect.bottom-windowRect.top;if(!(flags&SWP_NOSIZE)){width=w;height=h;}windowRect={x,y,x+width,y+height};return 1;}
std::function<json(const json&)> place;
template<class F> void ipc_on(const char*,F fn){place=fn;}
${helper}
${covers}
${looksFullscreen}
${layout}
void registerPlace(){${ipc}}
void expect(bool condition,const char* name){if(!condition)throw std::runtime_error(name);}
int main(){try{registerPlace();int tests=0;
 auto run=[&](const char* name,auto fn){fn();++tests;std::cout<<"PASS "<<name<<"\\n";};
 run("default right startup",[&]{expect(applyFullHeightLayout(),"layout result");expect(windowRect.left==1117,"default right coordinate");});
 run("saved left startup",[&]{settings["windowPlacement"]="left";expect(applyFullHeightLayout(),"left layout");expect(windowRect.left==0,"left coordinate");});
 run("repeated DPI/display/wake reflow retains left",[&]{for(int i=0;i<5;++i)expect(applyFullHeightLayout(targetMonitor),"reflow");expect(windowRect.left==0,"left reflow");});
 run("taskbar work area left offset",[&]{work={48,0,1920,1040};expect(applyFullHeightLayout(),"work area");expect(windowRect.left==48&&windowRect.top==0,"work offset");});
 run("negative-origin secondary monitor",[&]{work={-1920,-200,0,880};expect(applyFullHeightLayout(reinterpret_cast<HMONITOR>(2)),"secondary");expect(windowRect.left==-1920&&windowRect.top==-200,"secondary coordinates");});
 run("physical DPI-sized work area",[&]{work={0,0,2560,1440};expect(applyFullHeightLayout(),"dpi area");expect(windowRect.left==0&&windowRect.right==1071&&windowRect.bottom==1440,"physical dimensions");});
 run("portrait narrow-screen protection",[&]{work={0,0,900,1600};expect(applyFullHeightLayout(),"portrait");expect(windowRect.left==0&&windowRect.right==900&&windowRect.top==195,"portrait dimensions");});
 run("hidden taskbar extends the window to the monitor bottom",[&]{settings["windowPlacement"]="left";trayVisible=false;appbarState=0;work={0,0,1920,1040};monitor={0,0,1920,1080};expect(applyFullHeightLayout(),"hidden tray");expect(windowRect.top==0&&windowRect.bottom==1080,"screen bottom");trayVisible=true;});
 run("auto-hidden taskbar also fills the screen",[&]{settings["windowPlacement"]="left";appbarState=ABS_AUTOHIDE;work={0,0,1920,1040};monitor={0,0,1920,1080};expect(applyFullHeightLayout(),"autohide tray");expect(windowRect.top==0&&windowRect.bottom==1080,"autohide bottom");appbarState=0;work={0,0,1920,1080};});
 run("fullscreen game occluding the taskbar fills to the monitor bottom",[&]{settings["windowPlacement"]="left";trayVisible=true;appbarState=0;work={0,0,1920,1040};monitor={0,0,1920,1080};qunsState=QUNS_NOT_PRESENT;expect(applyFullHeightLayout(),"windowed");expect(windowRect.top==0&&windowRect.bottom==1040,"windowed respects taskbar");qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;g_foregroundWindow=reinterpret_cast<HWND>(4);gameRect=monitor;expect(applyFullHeightLayout(),"fullscreen");expect(windowRect.top==0&&windowRect.bottom==1080,"fullscreen bottom");g_foregroundWindow=nullptr;qunsState=QUNS_NOT_PRESENT;work={0,0,1920,1080};});
 run("fullscreen game snapshots fill to the monitor bottom",[&]{settings["windowPlacement"]="right";trayVisible=true;appbarState=0;work={0,0,1920,1040};monitor={0,0,1920,1080};qunsState=QUNS_NOT_PRESENT;g_focusSession.target.valid=true;g_focusSession.target.fullscreen=true;g_focusSession.target.hwnd=reinterpret_cast<HWND>(3);expect(applyFullHeightLayout(),"summon snapshot");expect(windowRect.top==0&&windowRect.bottom==1080,"snapshot bottom");g_focusSession={};g_foregroundWindow=reinterpret_cast<HWND>(4);gameRect=monitor;expect(applyFullHeightLayout(),"live foreground");expect(windowRect.top==0&&windowRect.bottom==1080,"foreground bottom");g_foregroundWindow=nullptr;gameRect={0,0,1920,1080};work={0,0,1920,1080};});

 run("busy notifications without local fullscreen respect the taskbar",[&]{work={0,0,1920,1040};monitor={0,0,1920,1080};qunsState=QUNS_BUSY;expect(applyFullHeightLayout(),"busy layout");expect(windowRect.bottom==1040,"busy is not fullscreen");qunsState=QUNS_NOT_PRESENT;work=monitor;});
 run("global D3D notification state alone does not hide the local taskbar",[&]{work={0,0,1920,1040};qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;expect(applyFullHeightLayout(),"global D3D");expect(windowRect.bottom==1040,"global state is not monitor-bound");qunsState=QUNS_NOT_PRESENT;work=monitor;});
 run("fullscreen foreground on another monitor preserves the local work area",[&]{work={0,0,1920,1040};g_foregroundWindow=reinterpret_cast<HWND>(4);gameRect={1920,0,3840,1080};qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;expect(applyFullHeightLayout(),"other foreground monitor");expect(windowRect.bottom==1040,"other monitor foreground");g_foregroundWindow=nullptr;gameRect=monitor;qunsState=QUNS_NOT_PRESENT;work=monitor;});
 run("fullscreen snapshot is scoped to its resolved monitor",[&]{work={0,0,1920,1040};g_focusSession.target={reinterpret_cast<HWND>(3),true,true};targetMonitor=reinterpret_cast<HMONITOR>(2);expect(applyFullHeightLayout(currentMonitor),"other snapshot monitor");expect(windowRect.bottom==1040,"other monitor snapshot");g_focusSession={};targetMonitor=currentMonitor;work=monitor;});
 run("maximized desktop app on a high-resolution monitor is not fullscreen",[&]{monitor={0,0,3840,2560};work={0,0,3840,2512};gameRect=work;gameStyle=WS_CAPTION;g_foregroundWindow=reinterpret_cast<HWND>(4);expect(!focusWindowCoversMonitor(g_foregroundWindow,monitor),"98 percent is not full coverage");expect(!focusWindowLooksFullscreen(g_foregroundWindow,monitor),"maximized classification");expect(applyFullHeightLayout(),"maximized foreground");expect(windowRect.bottom==2512,"visible taskbar remains excluded");qunsState=QUNS_RUNNING_D3D_FULL_SCREEN;expect(!focusWindowLooksFullscreen(g_foregroundWindow,monitor),"global D3D cannot override incomplete edges");qunsState=QUNS_NOT_PRESENT;g_foregroundWindow=nullptr;gameStyle=WS_POPUP;monitor={0,0,1920,1080};work=monitor;gameRect=monitor;});
 run("borderless fullscreen fills the monitor without notification hints",[&]{work={0,0,1920,1040};gameRect=monitor;g_foregroundWindow=reinterpret_cast<HWND>(4);expect(focusWindowLooksFullscreen(g_foregroundWindow,monitor),"borderless fullscreen");expect(applyFullHeightLayout(),"borderless layout");expect(windowRect.bottom==1080,"borderless bottom");g_foregroundWindow=nullptr;work=monitor;});
 run("fullscreen on a negative-origin monitor uses that monitor edges",[&]{monitor={-1920,-200,0,880};work={-1920,-200,0,840};gameRect=monitor;g_foregroundWindow=reinterpret_cast<HWND>(4);expect(applyFullHeightLayout(),"negative-origin fullscreen");expect(windowRect.top==-200&&windowRect.bottom==880,"negative-origin monitor bounds");g_foregroundWindow=nullptr;monitor={0,0,1920,1080};work=monitor;gameRect=monitor;});
 run("fullscreen frame rounding and spanning-monitor windows are accepted",[&]{HWND game=reinterpret_cast<HWND>(4);gameRect={2,2,1918,1078};expect(focusWindowCoversMonitor(game,monitor),"frame rounding");gameRect={-1920,0,1920,1080};expect(focusWindowCoversMonitor(game,monitor),"spanning monitor");gameRect={0,0,1920,1040};expect(!focusWindowCoversMonitor(game,monitor),"missing taskbar edge");expect(!focusWindowCoversMonitor(game,RECT{}),"empty monitor");gameRect=monitor;});
 run("fullscreen geometry failure falls back safely",[&]{HWND game=reinterpret_cast<HWND>(4);gameRect=monitor;frameOk=false;expect(focusWindowCoversMonitor(game,monitor),"window rect fallback");gameRectOk=false;expect(!focusWindowCoversMonitor(game,monitor),"both geometry calls failed");frameOk=true;gameRectOk=true;});

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
 const compiled=spawnSync('cmd.exe',['/d','/c',cmd],{cwd:run,encoding:'utf8',maxBuffer:8*1024*1024});
 fs.writeFileSync(path.join(run,'native.log'),(compiled.stdout||'')+(compiled.stderr||''));
 if(compiled.status!==0)throw Error(`Native regression failed (${compiled.status}):\n${compiled.stdout}\n${compiled.stderr}`);
 const match=compiled.stdout.match(/NATIVE_CASES=(\d+)/);assert(match);nativeCases=Number(match[1]);console.log(compiled.stdout.trim());
}
const receipt={ok:true,javascriptCases:cases,nativeCases,scope:'Actual renderer/settings functions replayed with in-memory persistence; native production functions compiled with Win32 fixture stubs. No live desktop/device or installed-product acceptance.'};
fs.writeFileSync(path.join(run,'result.json'),JSON.stringify(receipt,null,2)+'\n');
console.log(JSON.stringify({ok:true,javascriptCases:cases.length,nativeCases,receipt:path.join(run,'result.json')},null,2));
