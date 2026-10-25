import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||'');
if(!process.argv[2]||out===root)throw Error('A separate evidence directory is required');
fs.mkdirSync(out,{recursive:true});
const require=createRequire(path.join(root,'package.json')),{build}=require('esbuild'),run=promisify(execFile);
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const inputs=['src/radial/utilities.ts','src/radial/utilityActions.ts','src/radial/utilitiesHost.ts','src/bridge/monitorOutput.ts','src/bridge/steamUi.ts','native/steam_ui_mode.h','src/radial/input.ts','src/radial/inputActions.ts','src/radial/inputHost.ts','src/bridge/deckyGlobalActions.ts','src/bridge/deckyGameActions.ts','src/radial/fan.ts','src/radial/fanActions.ts','src/radial/fanHost.ts','src/bridge/deckyFanActions.ts','src/radial/performance.ts','src/bridge/deckyPerformanceActions.ts','src/bridge/performanceControls.ts','decky-plugin/src/performanceModel.ts','src/radial/model.ts','src/radial/availability.ts','src/radial/testingState.ts','src/radial/testing.ts','src/bridge/uiSettings.ts','src/bridge/residentNavigation.ts','src/components/NavRail.vue','src/views/SettingsView.vue','native/radial_release.h','src/router.ts','src/App.vue','src/radial/host.ts','src/bridge/controllerShortcutRules.ts','src/bridge/controllerShortcutStorage.ts','src/radialOutputEntry.ts','vite.config.ts','native/main.cpp','tools/radial_release_selftest.mjs'];
const sourceHashes=Object.fromEntries(inputs.map(file=>[file,sha(path.join(root,file))]));
const report={pass:false,scope:'Production user-entry policy; consumers executed with memory routing/IPC boundaries. Native endpoints and extracted actual summon/action predicates run in an ordinary console fixture.',sourceHashes,hardwareWrites:0,inputInjected:false,ordinaryYmccLaunched:false,checks:[]};
const check=(name,value)=>{assert.ok(value,name);report.checks.push(name);};
async function load(entry,modules={}){
 const result=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},write:false,bundle:true,platform:'node',format:'cjs',alias:{'@':path.join(root,'src')},define:{'import.meta.url':JSON.stringify(pathToFileURL(path.join(root,'vite.config.ts')).href)},plugins:[{name:'radial-release-memory-boundaries',setup(b){
  b.onResolve({filter:/.*/},a=>{if(a.path in modules)return{path:a.path,namespace:'memory'};if(a.path.endsWith('.vue'))return{path:a.path,namespace:'component'};});
  b.onLoad({filter:/.*/,namespace:'memory'},a=>({contents:modules[a.path],loader:'ts'}));
  b.onLoad({filter:/.*/,namespace:'component'},a=>({contents:`globalThis.__radialRelease.loads.push(${JSON.stringify(a.path)});export default {};`,loader:'ts'}));
 }}]});
 const module={exports:{}};new Function('require','module','exports',result.outputFiles[0].text)(require,module,module.exports);return module.exports;
}
try{
 globalThis.__radialRelease={loads:[],guards:[],calls:0};
 const state=globalThis.__radialRelease;
 const routing=await load(`export * from './src/router';export * from './src/radial/availability';export * from './src/radial/testingState';export {residentNavigationRoutes} from './src/bridge/residentNavigation';export {defaultRadialSettings,normalizeRadialSettings} from './src/radial/model';`,{
  'vue-router':`export const createWebHashHistory=()=>({});export const createRouter=options=>({options,beforeEach:guard=>globalThis.__radialRelease.guards.push(guard)});`,
  './bridge/customSteamLibrary':`export const CUSTOM_STEAM_LIBRARY_ROUTE='/custom-steam-library';export const CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED=false;`,
 });
 check('feature ships behind explicit runtime testing opt-in',routing.RADIAL_USER_AVAILABLE===true&&!routing.isRadialTestingEnabled());
 const channels=['enabled','monitorHold','fullScreenHold','feedback'];
 check('new configuration defaults every wheel enable channel to off',channels.every(key=>routing.defaultRadialSettings()[key]===false));
 for(const raw of [undefined,null,{}, {enabled:'true',monitorHold:1,fullScreenHold:'true',feedback:'true'}])check('missing or invalid channel fields default to off',channels.every(key=>routing.normalizeRadialSettings(raw)[key]===false));

 check('default sidebar/controller route list excludes wheel',routing.residentNavigationRoutes(routing.ROUTES,false).every(route=>route.path!=='/radial'));
 check('testing opt-in exposes sidebar/controller route',routing.residentNavigationRoutes(routing.ROUTES,false,true).some(route=>route.path==='/radial'));
 check('router retains test-only editor route',routing.router.options.routes.some(route=>route.path==='/radial'));
 for(const route of ['/radial','/radial/','/radial/editor'])check('direct route refuses '+route,state.guards.some(guard=>guard({path:route})==='/schedule'));
 check('performance route remains allowed',state.guards.every(guard=>guard({path:'/schedule'})===true));
 routing.radialTestingEnabled.value=true;check('explicit testing admits direct editor route',state.guards.every(guard=>guard({path:'/radial'})===true));
 routing.radialTestingEnabled.value=false;check('disabling testing rejects route again',state.guards.some(guard=>guard({path:'/radial'})==='/schedule'));
 await routing.preloadStartupRoutes();
 check('startup never loads radial editor/page chunks',state.loads.every(file=>!file.includes('Radial')));
 const bad=`()=>{globalThis.__radialRelease.calls++;throw Error('Hidden host performed IO')}`;
 const host=await load(`export {startRadialHost} from './src/radial/host';`,{
  './utilitiesHost':`export const createLocalRadialUtilityActions=${bad};`,
  './inputHost':`export const createLocalRadialInputActions=${bad};`,
  './fanHost':`export const createLocalRadialFanActions=${bad};`,
  '@/bridge/deckyFanDisplay':`export const onFanMirrorDisplay=${bad};export const onFanMirrorUiGate=${bad};`,
  '@/bridge/ipc':`export const isNativeRuntime=true;export const invoke=${bad};export const on=${bad};`,
  '@/bridge/api':`export const powerLifecycle={get:${bad}};export const shell={};export const windowApi={};`,
  '@/bridge/settingsRepository':`export const getSettingsGeneration=${bad};`,
  '@/bridge/performanceSchedule':`export const getContinuousPerformanceSnapshot=${bad};export const updateContinuousPerformanceControls=${bad};`,
  '@/bridge/yeman':`export const getTdpExecutionCapability=${bad};`,
  './settings':`export const loadRadialSettings=${bad};`,
 });
 const oldWindow=globalThis.window,oldInterval=globalThis.setInterval;
 globalThis.window={addEventListener(){state.calls++;throw Error('Hidden host registered event listener')}};
 globalThis.setInterval=()=>{state.calls++;throw Error('Hidden host started timer')};
 try{const owner=host.startRadialHost({push(){state.calls++}});owner.stop();owner.stop();check('host starts no IPC, settings, listeners or timers',state.calls===0);}finally{globalThis.window=oldWindow;globalThis.setInterval=oldInterval;}
 const rules=await load(`export * from './src/bridge/controllerShortcutRules';export * from './src/bridge/controllerShortcutStorage';`);
 check('recording choices hide summon action',rules.actionById('radial.summon')?.available===false&&!rules.isControllerShortcutActionVisible('radial.summon'));
 check('normal YMCC summon remains available',rules.isControllerShortcutActionVisible('window.summon'));
 const saved={...rules.newControllerShortcutRule('kept-wheel','radial.summon'),inputs:[{source:'oem',code:'rear-a'}],futureMetadata:{kept:true}};
 const stored={shortcutRules:[saved]},normalized=rules.readControllerShortcutRules(stored);
 check('old wheel rule is retained in settings model',normalized.length===1&&normalized[0].actionId==='radial.summon'&&normalized[0].enabled===true);
 assert.deepEqual(rules.preserveControllerShortcutRecords(stored,normalized,normalized),[saved]);report.checks.push('saving unrelated rules preserves old wheel record bytes/data');
 const config=await load(`export {default as config} from './vite.config';`,{'vite':'export const defineConfig=x=>x;','@vitejs/plugin-vue':'export default ()=>({name:"fixture-vue"});'});
 check('explicit testing ships its output page without starting it','radial' in config.config.build.rollupOptions.input);
 check('main and normal monitor output stay packaged',['main','hud','notice'].every(key=>key in config.config.build.rollupOptions.input));
 const nativeSource=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replaceAll('\r\n','\n');
 const summon=nativeSource.match(/static void radialSummonRequest\(const char\* source,int x,int y\)\{[^\n]+\}/)?.[0];
 const nativeDefaults=nativeSource.match(/static bool g_radialEnabled=[^\n]+/)?.[0];
 check('native defaults are extracted from current production source',!!nativeDefaults);
 check('missing native monitor-hold field defaults off',nativeSource.includes('g_radialMonitorHold=a.value("monitorHold",false);'));
 const predicate=nativeSource.match(/static bool nativeYmccShortcutAction\(const std::string& action\) \{[\s\S]*?\n\}/)?.[0];
 check('actual native summon and action predicate extracted',!!summon&&!!predicate);
 check('saved native rules are rejected before input chord admission',nativeSource.includes('if (!ymcc::radial::userActionAvailable(actionId)) continue;\n                        NativeKeyboardShortcutRule rule;'));
 check('monitor long-hold predicate includes product policy',nativeSource.includes('[]{return ymcc::radial::userAvailable()&&g_radialEnabled&&g_radialMonitorHold;}'));
 const testingHandler=nativeSource.slice(nativeSource.indexOf('    ipc_on("radial.testing.configure"'),nativeSource.indexOf('    const auto radialInitialize=',nativeSource.indexOf('    ipc_on("radial.testing.configure"')));
 check('native diagnostic testing handler extracted',testingHandler.includes('g_radialOverlay.shutdown()'));
 const endpointGuards=['configure','status','request','create','frame','keepAlive','result'].map(command=>{
  const start=nativeSource.indexOf('    ipc_on("radial.'+command+'"');const body=nativeSource.indexOf(')->json{',start)+8;
  const guard=nativeSource.slice(body).match(/^if\(!ymcc::radial::userAvailable\(\)\)return ymcc::radial::unavailableResponse<json>\(\);/)?.[0];
  check('actual native '+command+' refuses before parsing/configuring',!!guard);return [command,guard];
 });
 const appSource=fs.readFileSync(path.join(root,'src/App.vue'),'utf8');
 check('normal startup uses only diagnostic admission coordinator',appSource.includes('radialHost = startRadialTestingHost(router);')&&!appSource.includes('radialHost = startRadialHost(router)'));
 const refreshStart=nativeSource.indexOf('static void nativeKeyboardShortcutsRefresh(ULONGLONG now) {');
 const refreshEnd=nativeSource.indexOf('    std::vector<NativeKeyboardShortcutRule> parsed;',refreshStart);
 const refreshPrefix=nativeSource.slice(refreshStart,refreshEnd);
 check('native shortcut refresh tracks testing admission without changing saved revision',refreshPrefix.includes('radialTestingSeen == radialTesting'));
 const cpp=`#include <functional>
#include <map>
#include <string>
#include <iostream>
#include ${JSON.stringify(path.join(root,'deps/json/json.hpp').replaceAll('\\','/'))}
#include ${JSON.stringify(path.join(root,'native/radial_release.h').replaceAll('\\','/'))}
using json=nlohmann::json;
static int eventCalls=0,metadataReads=0,shutdownCalls=0,bodyEntries=0;
namespace ymcc::desktophud {static long long wallNow(){++metadataReads;return 1;}}
static unsigned currentPowerGeneration(){++metadataReads;return 1;}
static void gamepadSerialPostEvent(const char*,const json&){++eventCalls;}
${nativeDefaults}
${summon}
${predicate}
#define YMCC_SHORTCUT_FIXTURE
using ULONGLONG=unsigned long long;
static ULONGLONG g_nativeKeyboardShortcutLastRefresh=0;
static long long g_nativeKeyboardShortcutRevision=-1;
static constexpr ULONGLONG NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS=1;
static int refreshParses=0;
static json ymSettingsSection(const char*){return {{"revision",7}};}
${refreshPrefix}++refreshParses;g_nativeKeyboardShortcutRevision=revision;}
struct Observer {bool ok=true;bool shutdown(){++shutdownCalls;return ok;}};
static Observer g_radialTouchObserver,g_radialRawTouchObserver,g_radialOverlay;
static std::map<std::string,std::function<json(const json&)>> handlers;
template<class Fn> void ipc_on(const char* name,Fn fn){handlers.emplace(name,fn);}
static void reg(){${testingHandler}
${endpointGuards.map(([command,guard])=>'ipc_on("radial.'+command+'",[](const json&)->json{'+guard+'++bodyEntries;return true;});').join('\n')}}
int main(){int checks=0;auto need=[&](bool value){if(!value)throw std::runtime_error("testing gate failed");++checks;};
need(ymcc::radial::kUserAvailable);need(!ymcc::radial::userAvailable());need(!g_radialEnabled);need(!g_radialMonitorHold);need(!g_radialFullScreenRequested);reg();
auto refuse=[&]{for(const auto& [name,handler]:handlers)if(name!="radial.testing.configure")for(const auto& payload:std::array<json,3>{json(),json::array({1,2}),json{{"enabled",true},{"monitorHold",true},{"fullScreenHold",true}}}){const auto r=handler(payload);for(const auto* key:{"ok","available","enabled","monitorHold","fullScreenHold","feedback","fullScreenInstalled","rawRegistered"})need(r.at(key)==false);}need(bodyEntries==0);};
auto summonAll=[&]{for(const auto* source:{"monitor","shortcut","settings","fullscreen","fullscreen-raw"})radialSummonRequest(source,1,2);};
nativeKeyboardShortcutsRefresh(10);need(refreshParses==1);nativeKeyboardShortcutsRefresh(12);need(refreshParses==1);
refuse();summonAll();need(eventCalls==0&&metadataReads==0&&shutdownCalls==0);need(!nativeYmccShortcutAction("radial.summon"));
for(const auto& invalid:std::array<json,4>{json(),json::array(),json{{"enabled",1}},json{{"enabled",true},{"old",true}}}){bool rejected=false;try{handlers.at("radial.testing.configure")(invalid);}catch(...){rejected=true;}need(rejected);need(!ymcc::radial::userAvailable());}
need(handlers.at("radial.testing.configure")({{"enabled",true}}).at("ok")==true);need(ymcc::radial::userAvailable());nativeKeyboardShortcutsRefresh(14);need(refreshParses==2);need(nativeYmccShortcutAction("radial.summon"));summonAll();need(eventCalls==5&&shutdownCalls==0);
need(!g_radialEnabled&&!g_radialMonitorHold&&!g_radialFullScreenRequested);g_radialEnabled=true;g_radialMonitorHold=true;g_radialFullScreenRequested=true;
need(handlers.at("radial.testing.configure")({{"enabled",false}}).at("ok")==true);need(shutdownCalls==3);nativeKeyboardShortcutsRefresh(16);need(refreshParses==3);nativeKeyboardShortcutsRefresh(18);need(refreshParses==3);need(!ymcc::radial::userAvailable()&&!g_radialEnabled&&!g_radialMonitorHold&&!g_radialFullScreenRequested);refuse();summonAll();need(eventCalls==5);
g_radialRawTouchObserver.ok=false;need(handlers.at("radial.testing.configure")({{"enabled",false}}).at("ok")==false);need(!ymcc::radial::userAvailable());
for(const auto* action:{"window.summon","window.hideToTray","os.returnDesktop","power.tdpAdjust","input.frontendButton"})need(nativeYmccShortcutAction(action));
std::cout<<"{\\\"pass\\\":true,\\\"checks\\\":"<<checks<<"}";
}
`;
 fs.writeFileSync(path.join(out,'native-release-consumer.cpp'),cpp);
 const exe=path.join(out,'native-release-consumer.exe'),quote=v=>'"'+v+'"';
 fs.writeFileSync(path.join(out,'compile.rsp'),['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O1',quote(path.join(out,'native-release-consumer.cpp')),`/Fo${quote(path.join(out,'native-release-consumer.obj'))}`,`/Fe${quote(exe)}`].join('\r\n'));
 fs.writeFileSync(path.join(out,'compile.cmd'),calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'cl.exe @compile.rsp\r\nexit /b %errorlevel%\r\n');
 try{const compiled=await run(process.env.ComSpec||'cmd.exe',['/d','/c','compile.cmd'],{cwd:out,windowsHide:true,timeout:70000,maxBuffer:2e6});fs.writeFileSync(path.join(out,'compile.log'),compiled.stdout+compiled.stderr);}catch(error){fs.writeFileSync(path.join(out,'compile.log'),String(error.stdout||'')+String(error.stderr||''));throw Error('Native release consumer failed to compile');}
 const native=await run(exe,[],{cwd:out,windowsHide:true,timeout:10000,maxBuffer:2e6});fs.writeFileSync(path.join(out,'native-result.json'),native.stdout);
 report.native=JSON.parse(native.stdout);check('native entry consumers refuse all requests',report.native.pass===true);
 for(const [file,digest]of Object.entries(sourceHashes))assert.equal(sha(path.join(root,file)),digest,'Source changed: '+file);
 report.pass=true;
}catch(error){report.error=error.stack;process.exitCode=2;}finally{
 delete globalThis.__radialRelease;fs.writeFileSync(path.join(out,'verification.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({pass:report.pass,checks:report.checks.length,nativeChecks:report.native?.checks,error:report.error,out}));
}
