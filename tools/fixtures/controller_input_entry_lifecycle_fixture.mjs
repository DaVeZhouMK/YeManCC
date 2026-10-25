// Actual caller -> repository -> api -> ipc over an inert WebView boundary.
// The native admission algorithm is NOT modeled here. Tests supply its explicit
// observed reply kinds; the existing native full-TU suite proves admission itself.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {verifyEntryToolchain,entryProductSources} from './controller_input_entry_toolchain.mjs';
const pinnedTools=verifyEntryToolchain();
export const repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const require=createRequire(path.join(repoRoot,'package.json'));
const {build}=require('esbuild');
export const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const fileHash=p=>sha(fs.readFileSync(p));
const mockPaths=new Set(['src/bridge/gamePolicyTarget.ts','src/bridge/performanceSchedule.ts','src/scheduler/index.ts']);
const mockModules={
 'src/bridge/gamePolicyTarget.ts':`const s=globalThis.__entryState;export const getPolicyGame=()=>s.game;export const isPolicyGameInitialized=()=>true;export const gamePolicyKey=g=>String(g.path).replaceAll('\\\\','/').split('/').at(-1).toLowerCase();export function subscribePolicyGameStatus(cb){s.gameListeners.add(cb);return()=>s.gameListeners.delete(cb)}`,
 'src/bridge/performanceSchedule.ts':`const s=globalThis.__entryState;export const loadGameCustomConfig=async()=>structuredClone(s.config);export function onGameCustomConfigChanged(cb){s.configListeners.add(cb);return()=>s.configListeners.delete(cb)}export async function clearGameInputRestoreSnapshot(expected){if(JSON.stringify(expected)!==JSON.stringify(s.config.inputRestore))throw Error('restore identity changed');s.restoreClears++;delete s.config.inputRestore}`,
 'src/scheduler/index.ts':`const s=globalThis.__entryState;export function registerScheduledTask(name,ms,run,opts){if(s.scheduled.has(name))throw Error('duplicate scheduled owner');s.scheduled.set(name,{ms,run,opts});return()=>s.scheduled.delete(name)}`,
};
const mutationRules={
 'protected-nack-as-commit':{file:'src/bridge/settingsRepository.ts',from:'throw new VirtualStartProtectionActive(normalize(JSON.parse(result.content)), result.startProtection);',to:'return next;'},
 'overlay-publishes-rejected-save':{file:'src/bridge/gameInputOverride.ts',from:'if (!result.ok) continue;',to:'if (false) continue;'},
 'mirror-ignores-rejected-save':{file:'src/bridge/deckyGlobalActions.ts',from:"if(!result.ok)throw Error('GAME_MIRROR_SOURCE_CHANGED');",to:'if(false)throw Error(\'GAME_MIRROR_SOURCE_CHANGED\');',count:2},
};
export async function compileEntryBundle(mutation=null){
 const records=new Map(),modified=[];const rule=mutation&&mutationRules[mutation];if(mutation&&!rule)throw Error('Unknown entry mutation');
 const plugin={name:'declared-entry-os-boundaries',setup(b){
  b.onResolve({filter:/.*/},args=>{
   const normalized=args.path.replaceAll('\\','/');
   let rel=normalized.startsWith('@/')?'src/'+normalized.slice(2):normalized==='@/scheduler'?'src/scheduler/index.ts':args.path.startsWith('.')?path.relative(repoRoot,path.resolve(args.resolveDir,args.path)).replaceAll('\\','/'):null;
   if(rel==='src/scheduler')rel+='/index.ts';else if(rel&&!path.extname(rel))rel+='.ts';
   if(rel&&mockPaths.has(rel))return{path:rel,namespace:'entry-boundary'};
   return null;
  });
  b.onLoad({filter:/.*/,namespace:'entry-boundary'},args=>({contents:mockModules[args.path],loader:'ts',resolveDir:repoRoot}));
  b.onLoad({filter:/\.(?:[cm]?[jt]sx?|json)$/},args=>{
   const rel=path.relative(repoRoot,args.path).replaceAll('\\','/');
   if(rel.startsWith('node_modules/')||rel.startsWith('..'))return null;
   const bytes=fs.readFileSync(args.path);records.set(rel,{relative:rel,path:args.path,sha256:sha(bytes)});let text=bytes.toString('utf8');
   if(rule&&rel===rule.file){assert.equal(text.split(rule.from).length-1,rule.count??1,'Mutation source anchor');text=text.split(rule.from).join(rule.to);modified.push({relative:rel,beforeSha256:sha(bytes),afterSha256:sha(text),from:rule.from,to:rule.to});}
   return{contents:text,loader:rel.endsWith('.json')?'json':rel.endsWith('.ts')?'ts':'js',resolveDir:path.dirname(args.path)};
  });
 }};
 const result=await build({stdin:{contents:`export * as baseline from './src/bridge/settingsBaseline';export * as repo from './src/bridge/settingsRepository';export * as globalActions from './src/bridge/deckyGlobalActions';export * as overlay from './src/bridge/gameInputOverride';`,resolveDir:repoRoot,loader:'ts'},bundle:true,write:false,metafile:true,format:'cjs',platform:'browser',alias:{'@':path.join(repoRoot,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[plugin]});
 assert.deepEqual([...records.keys()].sort(),[...entryProductSources].sort(),'Product import graph changed; update the existing adapter and fixture together');
 const vendorFiles=new Set(pinnedTools.map(r=>path.resolve(r.path).toLowerCase()));
 for(const input of Object.keys(result.metafile.inputs)){
  if(input==='<stdin>')continue;
  if(input.startsWith('entry-boundary:')){assert(mockPaths.has(input.slice('entry-boundary:'.length)),'Undeclared runtime seam');continue;}
  const absolute=path.resolve(repoRoot,input),relative=path.relative(repoRoot,absolute).replaceAll('\\','/');
  assert(records.has(relative)||vendorFiles.has(absolute.toLowerCase()),'Unpinned compiled input: '+input);
 }
 if(rule)assert.equal(modified.length,1,'Mutation must change exactly one consumer source');
 return{code:result.outputFiles[0].text,sha256:sha(result.outputFiles[0].contents),sources:[...records.values()],modified,declaredSeams:[...mockPaths],metafile:result.metafile};
}
export async function createEntryFixture(compiled){
 const state={game:null,gameListeners:new Set(),configListeners:new Set(),scheduled:new Map(),config:{version:1,entries:{}},restoreClears:0};
 const fx={state,protectedReply:false,writeFailure:false,beforeWrite:null,beforeReadText:null,requests:[],writeAttempts:[],commits:[],events:[],forbidden:[],clearShortcut:0,closeJoyxoff:0,leases:0,disk:null,revisionConflicts:0,changeRevisionBeforeWrite:false};
 const key=p=>String(p).replaceAll('/','\\').toLowerCase();const dir='Q:\\YMCC-ENTRY-MEMORY',file=dir+'\\yeman-settings.json';let receiver;
 const webview={addEventListener(type,fn){assert.equal(type,'message');receiver=fn;},postMessage(message){fx.requests.push(message);queueMicrotask(async()=>{
  try{const {cmd,args,id}=message;let value;
   if(cmd==='settings.location')value={directory:dir,file,backup:file+'.bak'};
   else if(cmd==='settings.read'){assert.equal(key(args.path),key(file));value={stamp:fx.disk,unchanged:args.stamp===fx.disk,content:fx.disk};}
   else if(cmd==='fs.readTextFile'){assert.equal(key(args.path),key(file));if(fx.beforeReadText)await fx.beforeReadText();value=fx.disk;}
   else if(cmd==='fs.exists'){assert.equal(key(args.path),key(file));value=true;}
   else if(cmd==='settings.write'){
    assert.equal(key(args.path),key(file));fx.writeAttempts.push(structuredClone(args));if(fx.beforeWrite)await fx.beforeWrite(args);
    if(fx.writeFailure)value=false;
    else if(fx.protectedReply)value={ok:false,reason:'virtual-start-protection-active',content:fx.disk,startProtection:{locked:true,requestId:41,phase:'verifying',persona:'steamdeck',remainingMs:18000,verificationPending:true,verificationRemainingMs:3000}};
    else {
     let current=JSON.parse(fx.disk);
     if(fx.changeRevisionBeforeWrite){fx.changeRevisionBeforeWrite=false;current.input.revision++;current.input.futureNativeValue='retained';fx.disk=JSON.stringify(current);fx.revisionConflicts++;}
     if(args.expectedInputRevision!==undefined&&args.expectedInputRevision!==current.input.revision)value={ok:false,reason:'revision-conflict',content:fx.disk};
     else{fx.disk=args.content;fx.commits.push(structuredClone(args));value={ok:true,content:fx.disk};}
    }
   }else if(cmd==='input.gameOverride.session')value='memory-native-session';
   else{fx.forbidden.push(cmd);throw Error('FORBIDDEN_NATIVE_REQUEST '+cmd);}
   receiver({data:{id,result:value}});
  }catch(e){receiver({data:{id:message.id,error:String(e.message??e)}});}
 });}};
 const module={exports:{}};const sandbox={module,exports:module.exports,console,setTimeout,clearTimeout,queueMicrotask,structuredClone,TextEncoder,TextDecoder,AbortController,performance,__entryState:state,process:{env:{NODE_ENV:'production'}},window:{chrome:{webview},dispatchEvent(e){fx.events.push({type:e.type,detail:structuredClone(e.detail)});return true;},addEventListener(){},removeEventListener(){}},CustomEvent:class{constructor(type,init){this.type=type;this.detail=init?.detail;}},document:{}};
 vm.runInNewContext(compiled.code,sandbox,{filename:'actual-controller-entry.cjs'});
 fx.repo=module.exports.repo;fx.actions=module.exports.globalActions;fx.overlay=module.exports.overlay;
 fx.repo.setSettingsDirectory(dir);
 fx.initial=fx.repo.normalizeSettings({input:{revision:10,outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false},gyroMotion:{enabled:false,virtualPadLink:false}},futureDocument:{retained:true}});module.exports.baseline.stampSettingsBaseline(fx.initial);fx.disk=JSON.stringify(fx.initial);
 fx.document=()=>JSON.parse(fx.disk);
 fx.flush=async()=>{for(let i=0;i<120;i++)await Promise.resolve();};
 fx.setGame=async game=>{state.game=game;for(const cb of state.gameListeners)cb(game);await fx.flush();};
 fx.tick=async()=>{for(const task of [...state.scheduled.values()])await task.run();await fx.flush();};
 fx.startOverlay=()=>fx.overlay.startGameInputOverrideWatch();
 fx.stop=()=>{fx.overlay.stopGameInputOverrideWatch();assert.equal(state.scheduled.size,0,'No live retry owner after stop');assert.equal(fx.forbidden.length,0,'Unexpected native ports were used');};
 fx.mirror=async(value,controls={})=>{
  const snapshot=fx.actions.globalMirrorRead({},(await fx.repo.loadSettings()).input,null);let valid=true;
  const context={generation:1,gameIdentity:'',controlAdmission:{global:snapshot.source},checkpoint(){if(!valid)throw Error('retired-mirror-context');}};
  const api=fx.actions.createGlobalMirrorActions({allowed:()=>true,game:()=>state.game,locked:()=>fx.overlay.getGameInputOverrideState().locked,targetPolicy:()=>null,side:()=> 'ac',read:async()=>snapshot,power:async()=>({generation:1,phase:'ready',hardwareWritesAllowed:true}),acquire:()=>{if(fx.leases)return null;fx.leases++;return()=>{fx.leases--;};},cas:async(revision,patch,isCurrent)=>{if(controls.beforeCas)await controls.beforeCas(()=>{valid=false;});return fx.repo.compareAndSwapInputSettings(revision,patch,isCurrent);},closeJoyxoff:async()=>{fx.closeJoyxoff++;return true;},clearShortcut:async()=>{fx.clearShortcut++;return true;}});
  try{return{ok:true,result:await api.execute({field:'padPersona',value},context)};}catch(error){return{ok:false,error:String(error.message??error)};}
 };
 return fx;
}