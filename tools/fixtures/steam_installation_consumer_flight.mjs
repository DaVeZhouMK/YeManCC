import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
export async function installationFlightCases(root){
 const {transformSync}=createRequire(root+'/package.json')('esbuild'),cases=[],checks=[];let activeCase='';
 const load=(text,modules={})=>{const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console,Uint8Array,TextEncoder,Date,require:name=>{assert(Object.hasOwn(modules,name),'unbound actual consumer import '+name);return modules[name];}});return module.exports;};
 const check=(name,ok)=>{checks.push({name,pass:!!ok});assert(ok,name);};const run=async(name,body)=>{activeCase=name;await body();cases.push(name);};const flush=async()=>{for(let n=0;n<20;n++)await Promise.resolve();};
 const helper=load(fs.readFileSync(root+'/src/bridge/steamInstallationFacts.ts','utf8')),api=fs.readFileSync(root+'/src/bridge/api.ts','utf8'),start=api.indexOf('export const steamInformation = {'),end=api.indexOf('export const registry = {',start);assert(start>=0&&end>start);
 const binding="import {invoke} from './ipc';import {createSteamInstallationReader} from './steamInstallationFacts';\n"+api.slice(start,end);
 const quickSource=fs.readFileSync(root+'/src/bridge/quickapp.ts','utf8'),mediaSource=fs.readFileSync(root+'/src/bridge/steamMediaSource.ts','utf8'),profiles=load(fs.readFileSync(root+'/src/bridge/losslessProfiles.ts','utf8'));
 const steam='S:\\Steam',library='D:\\SharedLibrary',ls=library+'\\steamapps\\common\\Lossless Scaling\\LosslessScaling.exe';
 const facts=roots=>({available:true,version:1,requested:1,installations:roots.map(path=>({path,error:0}))});
 function fixture(){
  let receipt=facts([steam]),held=null,error=null,queries=0,actions=0;const files=new Map([[steam+'\\steamapps\\libraryfolders.vdf','"libraryfolders" {"1" {"path" "D:\\SharedLibrary"}}'],[ls,''],[library+'\\steamapps\\appmanifest_570.acf','"AppState" {"appid" "570" "name" "Dota 2" "installdir" "Dota 2"}']]);
  const reads=[],dirs=[],forbidden=async()=>{actions++;throw Error('No actions allowed in installation read');};
  const invoke=async(command,args,options)=>{assert.equal(command,'steamInformation.get');assert.equal(args.mask,1);assert.equal(Object.keys(args).length,1);assert.equal(options.timeoutMs,5000);queries++;const wait=held;if(wait)await wait;if(error)throw error;return receipt;};
  const shared=load(binding,{'./ipc':{invoke},'./steamInstallationFacts':helper}).steamInformation;
  const apiPort={steamInformation:shared,app:{},shell:{run:forbidden},proc:{},optiClient:{},losslessRuntime:{},
   registry:{read:forbidden},fs:{exists:async p=>{reads.push(p);return files.has(p);},readTextFile:async p=>{reads.push(p);if(!files.has(p))throw Error('inert file absent');return files.get(p);},readDir:async p=>{dirs.push(p);return [...files.keys()].filter(k=>k.startsWith(p+'\\')&&!k.slice(p.length+1).includes('\\')).map(k=>({name:k.slice(p.length+1),isFile:true}));}}};
  const quick=load(quickSource,{'./api':apiPort,'./losslessProfiles':profiles,'./losslessRuntimeState':{readLosslessRuntime:async()=>({known:true,running:false,reason:''}),invalidateLosslessRuntime:()=>{},losslessRuntimeRevision:()=>0,subscribeLosslessRuntime:()=>()=>{}},'./powerLifecycleState':{sharedPowerWriteScope:forbidden,getSharedPowerLifecycle:forbidden,onSharedPowerLifecycle:forbidden},'./gamedetect':{},'./optiscalerClientSync':{}});
  const media=load(mediaSource,{'./api':apiPort,'./ipc':{invoke},'./steamHttp':{requestSteamHttp:forbidden,requestSteamJson:forbidden},'./uiSettings':{getUiSetting:()=>false,setUiSettings:forbidden}});
  return {quick,media,shared,files,reads,dirs,queries:()=>queries,actions:()=>actions,setReceipt:v=>{receipt=v;},setError:e=>{error=e;},hold:()=>{let release;held=new Promise(r=>{release=r;});return()=>{held=null;release();};}};
 }
 try{
  await run('cross-consumer-native-request-once',async()=>{
   const f=fixture(),release=f.hold(),a=f.quick.resolveLs(),b=f.media.installedSteamWallpaperTargets();await flush();release();const [quick,media]=await Promise.all([a,b]);
   check('actual-consumers-keep-LS-and-manifest-results',quick.exe===ls&&quick.source==='steam'&&media.length===1&&media[0].appId===570&&media[0].verified===true);
   check('two-actual-consumers-share-single-native-flight',f.queries()===1);check('read-only-no-network-process-or-settings-actions',f.actions()===0);
  });
  await run('settled-flight-keeps-next-root-fresh',async()=>{
   const f=fixture();await f.media.installedSteamWallpaperTargets();const moved='T:\\MovedSteam',newLs=moved+'\\steamapps\\common\\Lossless Scaling\\LosslessScaling.exe';f.files.clear();f.files.set(newLs,'');f.files.set(moved+'\\steamapps\\appmanifest_570.acf','"AppState" {"appid" "570" "name" "Dota 2" "installdir" "NewGame"}');f.setReceipt(facts([moved]));
   const release=f.hold(),a=f.quick.resolveLs(),b=f.media.installedSteamWallpaperTargets();await flush();release();const [quick,media]=await Promise.all([a,b]);check('next-flight-uses-moved-root-in-both-consumers',quick.exe===newLs&&media.length===1&&media[0].gameDirectory===moved+'\\steamapps\\common\\NewGame');check('settled-read-does-not-cache-and-new-concurrency-is-one',f.queries()===2);
  });
  await run('failed-flight-keeps-consumer-specific-unknown',async()=>{
   const f=fixture();f.setError(Error('inert IPC unavailable'));const release=f.hold(),a=f.quick.resolveLs().then(value=>({value}),error=>({error})),b=f.media.installedSteamWallpaperTargets();await flush();release();const [quick,media]=await Promise.all([a,b]);
   check('same-failed-request-shared',f.queries()===1);check('quickapp-keeps-unconfirmed-error-media-keeps-empty',String(quick.error?.message).includes('Steam 安装信息尚未确认')&&media.length===0);check('failed-metadata-does-not-open-library-or-legacy-registry',f.dirs.length===0&&f.actions()===0);
   f.setError(null);check('later-real-consumer-can-recover-with-fresh-query',(await f.media.installedSteamWallpaperTargets()).length===1&&f.queries()===2);
  });
  await run('library-roots-and-manifest-admission-preserved',async()=>{
   const f=fixture();f.setReceipt({...facts([]),installations:[{path:steam,error:0},{path:'s:/steam/',error:0},{path:'relative',error:0},{path:'Z:\\Denied',error:5}]});f.files.set(library+'\\steamapps\\appmanifest_999.acf','"AppState" {"appid" "570" "name" "bad" "installdir" "wrong"}');f.files.set(library+'\\steamapps\\appmanifest_571.acf','"AppState" {"appid" "571" "name" "bad" "installdir" "../escape"}');
   const values=await f.media.installedSteamWallpaperTargets();check('media-still-expands-original-libraryfolders',values.length===1&&values[0].gameDirectory===library+'\\steamapps\\common\\Dota 2');check('duplicate-invalid-denied-root-never-scanned',JSON.stringify(f.dirs)===JSON.stringify([steam+'\\steamapps',library+'\\steamapps']));check('manifest-mismatch-and-parent-directory-still-rejected',values.every(x=>x.appId===570));check('no-extra-identity-presence-network-or-action',f.queries()===1&&f.actions()===0);
  });
  return {pass:true,selection:'installation-flight',cases,checks,defaultParent:false,fullOriginalQuickappAndMediaModules:true,actualPublicApiBinding:true,realSteamOperations:0,wholeCpuBenefitMeasured:false,wholeGoalComplete:false};
 }catch(error){return {pass:false,selection:'installation-flight',cases,checks,failedCase:activeCase,error:String(error.stack??error),defaultParent:false,realSteamOperations:0,wholeGoalComplete:false};}
}
