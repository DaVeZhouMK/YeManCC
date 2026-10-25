// Replay recorded unified output and explicit outages through the real output
// page, admission, Vue renderer and layout queue. No runtime/hardware changes.
import fs from 'node:fs';import path from 'node:path';import cp from 'node:child_process';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {createServer} from 'node:http';import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),out=path.resolve(process.env.YMCC_DROPOUT_REPLAY_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/dropout-replay-20261013'),req=createRequire(root+'/package.json'),{build}=req('esbuild');fs.mkdirSync(out,{recursive:true});const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const native=`#include <windows.h>
#include <fstream>
#include "${root}/deps/json/json.hpp"
using json=nlohmann::json;
#include "${root}/native/monitor_frame_output.h"
int main(int argc,char**argv){if(argc!=2)return 2;const uint64_t n=1700000000000ULL;json all=json::array();
 for(const std::string name:{"short-low-gap","expired-low","stopped-producer","pid-reuse"}){
  ymcc::monitorframe::Output o;o.bind(77,99);o.observeRtss({true,8.333,120,n-400});o.observeRtss({true,8.333,120,n-200});json rows=json::array();
  for(uint64_t dt=0;dt<=10000;dt+=200){const auto at=n+dt;
   if(name=="pid-reuse"&&dt==4000)o.bind(77,100);
   const bool stopped=name=="stopped-producer"&&dt>0&&dt<7000;
   if(dt%1000==0&&!stopped){double low=89;bool missing=false;
    if(name=="short-low-gap"){missing=dt>=2000&&dt<4000;if(dt>=4000)low=73;}
    if(name=="expired-low"){missing=dt>=1000&&dt<7000;if(dt>=7000)low=73;}
    if(name=="pid-reuse")missing=dt>=4000&&dt<6000;
    o.hardware(at/1000,120,missing?-1:low,20,30,"synthetic-case",at,8.333,1000);
   }
   if(!stopped)o.observeRtss({true,8.333,120,at});
   rows.push_back({{"at",at},{"fps",o.packet(at)}});
  }
  all.push_back({{"name",name},{"rows",rows}});
 }
 std::ofstream(argv[1])<<all.dump();return 0;
}`;
fs.writeFileSync(out+'/native-outages.cpp',native);fs.writeFileSync(out+'/build-native-outages.cmd',['@echo off','call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul',`cl /nologo /std:c++20 /utf-8 /EHsc "${out}/native-outages.cpp" /Fo"${out}/native-outages.obj" /Fe"${out}/native-outages.exe"`,'if errorlevel 1 exit /b 1',`"${out}/native-outages.exe" "${out}/NATIVE-OUTAGES.json"`,'exit /b %errorlevel%'].join('\r\n'));
cp.execFileSync('cmd.exe',['/d','/c',out+'/build-native-outages.cmd'],{cwd:root,windowsHide:true,stdio:'pipe'});const scenarios=read(out+'/NATIVE-OUTAGES.json');
const recordedPath=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/low-grace-memory-20261013/live-before.json'),recorded=read(recordedPath).rows.filter(x=>x.type==='snapshot'&&x.fps);
// A schema adaptation reflects this patch's 5s RTSS deadline. Original reading,
// source, producer timestamps and frame samples are preserved; no observations added.
const realRows=recorded.map(x=>{const fps=structuredClone(x.fps);if(fps.fpsSource==='rtss')fps.fpsValidUntil=fps.fpsObservedAt+5000;if(fps.frameTimeSource==='rtss-current-frame')fps.frameTimeValidUntil=fps.frameTimeTs+5000;return {at:x.at,fps,hwDown:x.hwDown}});assert(realRows.length>100);scenarios.unshift({name:'recorded-unified-cyberpunk2077',rows:realRows});
const nativeDeps={clock:false,disk:false,process:false};
const ipc=`export const isNativeRuntime=true;export function on(name,fn){globalThis.__events[name]=fn;return()=>{delete globalThis.__events[name]}}export async function invoke(cmd,args){const h=globalThis.__output;if(cmd==='desktopHud.ready')return {id:23,width:1920,height:1080};if(cmd==='desktopHud.layout'){h.layouts.push({...args,at:globalThis.__time});return true}if(cmd==='desktopHud.close'){h.closes.push(args);return true}throw Error('unexpected output IPC '+cmd)}`;
const entry=`import {normalizeMonitorOutput,hudFromUniformSnapshot,HUD_METRICS} from './src/bridge/monitorHudModel';import {createMonitorDataHub} from './src/bridge/monitorData';import {nextTick} from 'vue';import './src/monitorHudOutputEntry';
let seq=0,prefs=normalizeMonitorOutput();const hub=createMonitorDataHub({now:()=>globalThis.__time,invoke:async()=>{throw Error('replay must not acquire')},readTextFile:async()=>{throw Error('replay must not read files')},setTimer:()=>1,clearTimer:()=>{}});
window.__reset=(layout,scale,hide)=>{prefs=normalizeMonitorOutput({ymcc:{layout,scale,hideUnavailable:hide,underlayPreset:'tifa',fields:Object.fromEntries(HUD_METRICS.map(m=>[m.id,['fps','fps1','frametimeGraph'].includes(m.id)]))}})};
window.__feed=async row=>{window.__time=row.at;const publication={schema:1,available:true,session:1,generation:1,seq:++seq,relayEpoch:0,collectedAt:row.at,heartbeatAt:row.at,hwinfoOkAt:row.hwDown?0:row.at,topActive:false,fpsActive:true,hwDown:row.hwDown===true,top:null,fps:row.fps?JSON.stringify(row.fps):null};const view=hub.admitDisplayPublication(publication);if(!view)throw Error('production admission rejected replay row');const data=hudFromUniformSnapshot(view,row.at);window.__events['desktopHud.frame']({type:'desktop-hud',protocol:1,hudId:23,nonce:'replay-valid-nonce-123456',seq,expiresAt:row.at+6000,show:true,preferences:prefs,data});await nextTick();return {values:data.values,freshUntil:data.freshUntil}};
window.__shape=()=>{const panel=document.querySelector('[data-monitor-hud]'),r=panel?.getBoundingClientRect(),img=panel?.querySelector('.hud-image-underlay'),im=img?.getBoundingClientRect(),low=panel?.querySelector('[data-hud-metric="fps1"]'),curve=panel?.querySelector('svg polyline');return {width:r?.width,height:r?.height,present:!!low,low:low?.innerText??null,imageCount:panel?.querySelectorAll('.hud-image-underlay').length??0,image:im?{width:im.width,height:im.height}:null,curve:!!curve,closes:window.__output.closes.length,layouts:window.__output.layouts.length,visibility:document.querySelector('#hud').style.visibility}};
`;
const built=await build({stdin:{contents:entry,loader:'ts',resolveDir:root},bundle:true,platform:'browser',format:'iife',alias:{'@':root+'/src'},write:false,plugins:[{name:'output-ipc-only',setup(b){b.onResolve({filter:/\/bridge\/ipc$/},x=>x.importer.endsWith('monitorHudOutputEntry.ts')?{path:'ipc',namespace:'replay'}:null);b.onLoad({filter:/.*/,namespace:'replay'},()=>({contents:ipc,loader:'ts',resolveDir:root}));b.onLoad({filter:/\.css$/},x=>({contents:'const style=document.createElement("style");style.textContent='+JSON.stringify(fs.readFileSync(x.path,'utf8'))+';document.head.appendChild(style);',loader:'js'}))}},monitorHudVuePlugin(root)]});
fs.writeFileSync(out+'/fixture.js',built.outputFiles[0].text);
const html='<meta charset="utf-8"><style>html,body{margin:0;background:#17212d}#hud{width:1920px}</style><div id="hud"></div><script>window.__time=1700000000000;window.__events={};window.__output={layouts:[],closes:[]};Date.now=()=>window.__time;</script><script src="/fixture.js"></script>';
const server=createServer((q,s)=>{s.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript':'text/html');s.end(q.url==='/fixture.js'?built.outputFiles[0].text:html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));const url='http://127.0.0.1:'+server.address().port+'/?hudId=23&nonce=replay-valid-nonce-123456';const {chromium}=req('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');let browser,page;const errors=[],measurements=[],checks=[];
const stable=(a,b,ctx)=>{for(const k of ['width','height'])assert(Math.abs(a[k]-b[k])<.1,JSON.stringify({ctx,key:k,a,b}));assert.equal(b.imageCount,1);assert(b.image&&Math.abs(b.image.width-b.width)<.15&&Math.abs(b.image.height-b.height)<.15,JSON.stringify({ctx,b}));assert.equal(b.closes,0);assert.equal(b.visibility,'visible')};
try{browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});page=await browser.newPage({viewport:{width:1920,height:1080}});page.on('pageerror',e=>errors.push(String(e)));

 for(const layout of ['corner','bar','capsule'])for(const scale of [1.4,2])for(const scenario of scenarios){
  await page.goto(url);await page.waitForFunction(()=>window.__feed&&window.__events['desktopHud.frame']);await page.evaluate(v=>window.__reset(...v),[layout,scale,true]);
  let baseline=null,previousLow=null,lowChanges=0,entrance=null;const shapes=[];
  for(const [i,row]of scenario.rows.entries()){
   const consumed=await page.evaluate(x=>window.__feed(x),row),f=row.fps;
   // Independent public requirement: one target must survive three seconds
   // with advancing genuine observations. LOW keeps its own shorter deadline.
   const valid=typeof consumed.values.fps==='number'&&consumed.values.fps>=1;
   const key=f?[f.pid,f.processCreated].join('|'):'';
   if(!valid)entrance=null;
   else if(!entrance||entrance.key!==key||row.at>=entrance.end||f.fpsObservedAt<entrance.observed)entrance={key,start:row.at,first:f.fpsObservedAt,observed:f.fpsObservedAt,end:f.fpsValidUntil,ready:false};
   if(entrance&&valid){entrance.ready ||= row.at-entrance.start>=3000&&f.fpsObservedAt-entrance.first>=3000;entrance.observed=f.fpsObservedAt;entrance.end=f.fpsValidUntil;}
   const expected=!!entrance?.ready&&consumed.values.fps1!=null;
   if(i===0||i%15===0||scenario.name!=='recorded-unified-cyberpunk2077'){
    await page.waitForTimeout(previousLow!==expected?45:16);const current=await page.evaluate(()=>window.__shape());shapes.push({at:row.at,expected,...current});
    assert.equal(current.present,expected,JSON.stringify({scenario:scenario.name,layout,scale,i,current,expected,consumed,fps:f}));assert.equal(current.closes,0);
    if(previousLow!==null&&previousLow!==current.present)lowChanges++;previousLow=current.present;
    if((scenario.name==='recorded-unified-cyberpunk2077'||scenario.name==='short-low-gap')&&expected){if(baseline)stable(baseline,current,{layout,scale,name:scenario.name,i});else baseline=current;}
   }
  }
  if(scenario.name==='short-low-gap')assert.equal(lowChanges,1,'Only the initial 3s entrance is visible; short LOW misses do not flicker');
  if(scenario.name==='expired-low'||scenario.name==='pid-reuse')assert.equal(lowChanges,3,'Initial admission, genuine exit, genuine readmission');
  if(scenario.name==='stopped-producer')assert.equal(lowChanges,1,'A stopped initial proof never satisfies entrance; restarted producer re-enters after 3s');
  measurements.push({layout,scale,name:scenario.name,rows:scenario.rows.length,lowChanges,shapes});checks.push(layout+' '+scale+' '+scenario.name);
 }
 // Silence starts after a genuine three-second admission, not an instant
 // bypass. LOW expires at 5s, FPS at 8s, transport at 9s after the final frame.
 await page.clock.install({time:new Date(1700000000000)});await page.goto(url);await page.waitForFunction(()=>window.__feed&&window.__events['desktopHud.frame']);await page.evaluate(()=>window.__reset('corner',1.4,true));
 const silent=scenarios.find(x=>x.name==='expired-low');await page.evaluate(x=>window.__feed(x),silent.rows[0]);await page.clock.runFor(40);
 await page.evaluate(()=>window.__time=1700000003000);await page.clock.runFor(2960);await page.evaluate(x=>window.__feed(x),silent.rows.find(x=>x.at===1700000003000));await page.clock.runFor(40);let held=await page.evaluate(()=>window.__shape());assert(held.present);assert.equal(held.closes,0);
 await page.evaluate(()=>window.__time=1700000004999);await page.clock.runFor(1959);held=await page.evaluate(()=>window.__shape());assert(held.present);assert.equal(held.closes,0);
 await page.evaluate(()=>window.__time=1700000005000);await page.clock.runFor(1);await page.evaluate(()=>Promise.resolve());held=await page.evaluate(()=>window.__shape());assert.equal(held.present,false);assert.equal(held.imageCount,1);assert.equal(held.closes,0);checks.push('silent transport: genuine LOW clears at 5s while the independent live FPS lease remains');
 await page.evaluate(()=>window.__time=1700000008000);await page.clock.runFor(3000);held=await page.evaluate(()=>window.__shape());assert.equal(held.imageCount,0);assert.equal(held.closes,0);checks.push('silent transport: FPS/curve age out at 8s without pretending the transport already closed');
 await page.evaluate(()=>window.__time=1700000009000);await page.clock.runFor(1000);held=await page.evaluate(()=>window.__shape());assert.equal(held.visibility,'hidden');assert.equal(held.closes,1);checks.push('silent transport: final frame at 3s closes exactly once at its independent 9s transport deadline');
 assert.deepEqual(errors,[]);await page.screenshot({path:out+'/replay-final.png'});fs.writeFileSync(out+'/REPLAY-RESULT.json',JSON.stringify({ok:true,checks,measurements,errors,recordedRows:realRows.length,nativeDeps,scope:'Actual output entry + snapshot admission + shared model/renderer + layout queue. Recorded unified packets adapted only to current RTSS deadline schema; outages are explicit synthetic production Output cases. Browser IPC receipt stubs; no live game/native-HWND acceptance, sensor acquisition, installed writes or CPU benchmark.'},null,2));console.log('Output dropout replay PASS',checks.length,'recorded rows',realRows.length);
}catch(e){await page?.screenshot({path:out+'/replay-failure.png'});fs.writeFileSync(out+'/REPLAY-RESULT.json',JSON.stringify({ok:false,checks,errors,error:String(e),measurements},null,2));throw e}finally{await browser?.close();await new Promise(r=>server.close(r))}
