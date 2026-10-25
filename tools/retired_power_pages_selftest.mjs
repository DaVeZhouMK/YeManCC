// Actual retired routes, bridge implementations and startup guards; all device/IO calls mocked.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(root,'package.json'));
const {transformSync,build}=require('esbuild'),ts=require('typescript'),vueRouter=require('vue-router');
const out=process.env.YMCC_LEGACY_CPU_RETIRE_OUT||path.resolve(root,'../../../_scratch/legacy-power-pages-retirement-20261017');
fs.mkdirSync(out,{recursive:true});
const read=file=>fs.readFileSync(path.join(root,file),'utf8');
const normalize=value=>JSON.parse(JSON.stringify(value));
const checks=[];
async function check(name,run){await run();checks.push(name);}
function evaluate(file,imports){const module={exports:{}};vm.runInNewContext(transformSync(read(file),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,console,require:name=>{assert(name in imports,'unexpected import '+name);return imports[name];}});return module.exports;}
const policy=evaluate('src/bridge/legacyCpuFeaturePolicy.ts',{});
await check('retired CCD/UV policy is a fixed inactive flag',()=>assert.equal(policy.LEGACY_CCD_UNDERVOLT_ACTIVE,false));
const calls={native:[],shell:[],read:[],write:[]};
const raw={autostart:{ccd:{enabled:true,mode:2},uv:{enabled:true,preset:'risk',vendor:'intel'}}};
const rawBefore=JSON.stringify(raw);
const bridge=evaluate('src/bridge/cpuctl.ts',{
 './legacyCpuFeaturePolicy':policy,
 './ipc':{invoke:async(...args)=>{calls.native.push(args);return {l3Domains:2,physicalCores:16,logical:32,ccdMasks:['ffff','ffff0000']};}},
 './api':{shell:{run:async(...args)=>{calls.shell.push(args);return {exitCode:0,stdout:'{"supported":true,"vendor":"intel","current":-75}',stderr:''};}}},
});
const settings={readSettingsSection:async key=>{calls.read.push(key);return raw;},saveSettingsSection:async(...args)=>{calls.write.push(args);}};
const autostart=evaluate('src/bridge/autostart.ts',{'./legacyCpuFeaturePolicy':policy,'./cpuctl':bridge,'./settingsRepository':settings});
await check('inactive CCD probe makes no topology query',async()=>{assert.equal((await bridge.probeCcd()).supported,false);assert.equal(calls.native.length,0);});
await check('inactive undervolt probe starts no external helper',async()=>{assert.equal((await bridge.probeUndervolt()).reason,'feature_inactive');assert.equal(calls.shell.length,0);});
for(const mode of [0,1,2,-1,NaN])await check('CCD command stays inactive for mode '+mode,async()=>{assert.equal((await bridge.setCcdMode(mode)).ok,false);assert.equal(calls.native.length,0);});
for(const value of [0,-8,-25,-75])await check('undervolt command stays inactive for value '+value,async()=>{assert.equal((await bridge.setUndervolt(value)).ok,false);assert.equal(calls.shell.length,0);});
await check('historic enabled preferences cannot activate either feature',async()=>{const cfg=await autostart.readCpuAutostart();assert.equal(cfg.ccd.enabled,false);assert.equal(cfg.uv.enabled,false);await autostart.applyCpuAutostart();await autostart.applyCpuAutostart();assert.equal(calls.read.length,0);assert.equal(calls.native.length+calls.shell.length,0);});
await check('retired autostart saves do not alter stored preferences',async()=>{await autostart.writeCcdAutostart(true,2);await autostart.writeUvAutostart(true,'risk','intel');assert.equal(calls.write.length,0);assert.equal(JSON.stringify(raw),rawBefore);});
await check('pure legacy preset conversion remains available',()=>{assert.equal(bridge.uvPresetValue('intel','risk'),-75);assert.equal(bridge.uvPresetValue('amd','safe'),-8);assert.equal(bridge.uvPresetValue('amd','unknown'),0);});
// Test-only active import verifies retained implementation without enabling the production flag.
const activePolicy={LEGACY_CCD_UNDERVOLT_ACTIVE:true};
const retained=evaluate('src/bridge/cpuctl.ts',{'./legacyCpuFeaturePolicy':activePolicy,'./ipc':{invoke:async(...args)=>{calls.native.push(args);return {l3Domains:2,physicalCores:16,logical:32,ccdMasks:['ffff','ffff0000']};}},'./api':{shell:{run:async(...args)=>{calls.shell.push(args);return {exitCode:0,stdout:'{"supported":true,"vendor":"intel","current":-75}',stderr:''};}}}});
await check('future gated CCD implementation retains validation and wire command',async()=>{assert.equal((await retained.probeCcd()).supported,true);const n=calls.native.length;assert.equal((await retained.setCcdMode(-1)).ok,false);assert.equal(calls.native.length,n);assert.equal((await retained.setCcdMode(2)).ok,true);assert.deepEqual(normalize(calls.native.at(-1)),['cpu.setCcdMode',{mode:2}]);});
await check('future gated UV implementation retains probe and set arguments',async()=>{assert.equal((await retained.probeUndervolt()).supported,true);assert.equal((await retained.setUndervolt(-75)).ok,true);assert.deepEqual(normalize(calls.shell.at(-1)[1]),['uv','set','-75']);});
const nav=evaluate('src/bridge/residentNavigation.ts',{});
for(const mode of [false,true])await check('sidebar and controller policy excludes retired pages in mode '+mode,()=>{const entries=[{path:'/schedule'},{path:'/tdp'},{path:'/cpu'},{path:'/fan'},{path:'/hidden',hidden:true}];assert.deepEqual(Array.from(nav.residentNavigationRoutes(entries,mode),r=>r.path),['/schedule','/fan']);assert.equal(entries.length,5);});
const source=read('src/App.vue').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)?.[1];assert(source);
const app=ts.createSourceFile('App.ts',source,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
let guardedCalls=0;function visit(node,ancestors=[]){if(ts.isCallExpression(node)&&node.expression.getText(app)==='applyCpuAutostart'){assert(ancestors.some(p=>ts.isIfStatement(p)&&p.expression.getText(app).includes('LEGACY_CCD_UNDERVOLT_ACTIVE')),'autostart call lacks inactive guard');guardedCalls++;}ts.forEachChild(node,child=>visit(child,[...ancestors,node]));}visit(app);
await check('both boot and wake legacy activation owners are removed; resident restore and sleep optimization remain',()=>{assert.equal(guardedCalls,2);assert(source.includes('scheduleSleepPowerPlanOptimization();'));assert(!source.includes('applyCpuAutoEnable'));assert(!source.includes('applyAutoTdpIfNeeded'));assert(source.includes('restorePerformanceScheduleIfConfigured'));});
await check('retired source modules are physically absent and App has no direct TDP bypass',()=>{
 assert.equal(fs.existsSync(path.join(root,'src/views/CpuView.vue')),false);
 assert.equal(fs.existsSync(path.join(root,'src/views/TdpView.vue')),false);
 assert.equal(fs.existsSync(path.join(root,'src/bridge/tdpAutoApply.ts')),false);
 assert(!source.includes('readTdp(mode)'));
 assert(!source.includes("setTdp(mode, next"));
});
const loaded=[];
const bundled=await build({stdin:{contents:read('src/router.ts'),loader:'ts',resolveDir:path.join(root,'src')},bundle:true,platform:'node',format:'cjs',write:false,external:['vue-router'],plugins:[{name:'isolated-views',setup(b){b.onResolve({filter:/\.vue$/},args=>({path:args.path,namespace:'views'}));b.onLoad({filter:/.*/,namespace:'views'},args=>({contents:`globalThis.__loadedPages.push(${JSON.stringify(args.path)});export default {name:${JSON.stringify(path.basename(args.path,'.vue'))}};`}));b.onResolve({filter:/^\.\/bridge\/customSteamLibrary$/},()=>({path:'library-contract',namespace:'contract'}));b.onLoad({filter:/.*/,namespace:'contract'},()=>({contents:"export const CUSTOM_STEAM_LIBRARY_ROUTE='/custom-steam-library';export const CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED=true;"}));}}]});
const module={exports:{}};
vm.runInNewContext(bundled.outputFiles[0].text,{module,exports:module.exports,__loadedPages:loaded,console,require:name=>{assert.equal(name,'vue-router');return {...vueRouter,createWebHashHistory:()=>vueRouter.createMemoryHistory()};}});
const routes=module.exports;
await check('retired pages are absent from active route catalog and lazy imports',()=>{assert(!Array.from(routes.ROUTES,r=>r.path).some(p=>p==='/cpu'||p==='/tdp'));assert(!/import\(['"]\.\/views\/(?:CpuView|TdpView)\.vue/.test(read('src/router.ts')));});
for(const old of ['/cpu','/tdp'])await check('old direct URL '+old+' resolves to performance schedule without mounting legacy UI',async()=>{await routes.router.push(old+'?from=bookmark');assert.equal(routes.router.currentRoute.value.path,'/schedule');assert.equal(routes.router.currentRoute.value.query.from,'bookmark');assert.equal(routes.router.currentRoute.value.matched.at(-1).name,'schedule');});
for(const name of ['cpu','tdp'])await check('old named link '+name+' redirects safely',async()=>{await routes.router.push({name});assert.equal(routes.router.currentRoute.value.path,'/schedule');});
await check('startup preloader never loads retired CPU/TDP pages',async()=>{await routes.preloadStartupRoutes();assert(!loaded.some(p=>/\/(?:CpuView|TdpView)\.vue$/.test(p)));assert(loaded.some(p=>p.endsWith('/PerformanceScheduleView.vue')));});
const report={passed:checks.length,checks,retiredPages:['/cpu','/tdp'],nativeHardwareCalls:0,externalHelpersStarted:0,installedSettingsWritten:0,productionPolicyActive:policy.LEGACY_CCD_UNDERVOLT_ACTIVE,futureBranchTestUsedFakeServices:true,loadedViewModules:loaded,guardedAutostartCalls:guardedCalls,scope:'Actual source modules and Vue Router with memory history; OS, IPC and filesystem services mocked; no product/device run'};
fs.writeFileSync(path.join(out,'retired-power-pages-test.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
