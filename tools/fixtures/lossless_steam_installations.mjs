import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
export async function steamInstallationCases({root,source,load}){
 const cases=[],checks=[];let activeCase='';const check=(name,ok)=>{checks.push({name,pass:!!ok});assert.ok(ok,name);};
 const helper=load(fs.readFileSync(path.join(root,'src/bridge/steamInstallationFacts.ts'),'utf8'));
 const apiSource=fs.readFileSync(path.join(root,'src/bridge/api.ts'),'utf8'),start=apiSource.indexOf('export const steamInformation = {'),end=apiSource.indexOf('export const registry = {',start);assert(start>=0&&end>start);
 const apiBinding="import {invoke} from './ipc';import {createSteamInstallationReader} from './steamInstallationFacts';\n"+apiSource.slice(start,end);
 const profiles=load(fs.readFileSync(path.join(root,'src/bridge/losslessProfiles.ts'),'utf8'));
 const primary='C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe',steam='S:\\Steam',library='D:\\SharedLibrary',target=library+'\\steamapps\\common\\Lossless Scaling\\LosslessScaling.exe';
 const facts=roots=>({available:true,version:1,requested:1,installations:roots.map(path=>({path,error:0,source:'inert-main-producer'}))});
 const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
 function fixture(){
  let receipt=facts([steam]),held=null,queryError=null,legacyCalls=0,queries=0;
  const state={known:true,running:false,path:'R:\\Existing\\LosslessScaling.exe',reason:''},files=new Map([[steam+'\\steamapps\\libraryfolders.vdf','"libraryfolders" {"1" {"path" "D:\\SharedLibrary"}}'],[target,'']]),reads=[];
  const invoke=async(command,args,options)=>{assert.equal(command,'steamInformation.get');assert.equal(args.mask,1);assert.equal(Object.keys(args).length,1);assert.equal(options.timeoutMs,5000);queries++;if(held)await held;if(queryError)throw queryError;return receipt;};
  const shared=load(apiBinding,{'./ipc':{invoke},'./steamInstallationFacts':helper}).steamInformation;
  const noAction=async()=>{throw Error('Unexpected action from installation observation');};
  const api={app:{},shell:{run:noAction},proc:{},optiClient:{},losslessRuntime:{},steamInformation:shared,
    registry:{read:async()=>{legacyCalls++;return 'X:\\PrivateSteam';}},
    fs:{exists:async p=>{reads.push(['exists',p]);return files.has(p);},readTextFile:async p=>{reads.push(['read',p]);assert(files.has(p));return files.get(p);}}};
  const runtime={readLosslessRuntime:async()=>({...state}),invalidateLosslessRuntime:()=>{},losslessRuntimeRevision:()=>0,subscribeLosslessRuntime:()=>()=>{}};
  const owner=load(source,{'./api':api,'./losslessProfiles':profiles,'./losslessRuntimeState':runtime,'./gamedetect':{},'./optiscalerClientSync':{}});
  return {owner,shared,state,files,reads,queries:()=>queries,legacyCalls:()=>legacyCalls,setReceipt:v=>{receipt=v;},setError:e=>{queryError=e;},hold:()=>{let release;held=new Promise(r=>{release=r;});return()=>{held=null;release();};}};
 }
 const run=async(name,fn)=>{activeCase=name;await fn();cases.push(name);};
 try{
  await run('LS Steam fallback consumes one shared in-flight installation request',async()=>{
   const f=fixture(),release=f.hold(),pending=[f.owner.resolveLs(),f.owner.resolveLs(),f.owner.resolveLs()];await flush();release();const values=await Promise.all(pending);
   check('canonical-public-root-selects-existing-library',values.every(x=>x.exe===target&&x.source==='steam'));
   check('three-concurrent-resolvers-share-one-native-query',f.queries()===1);
   check('consumer-never-reads-private-Steam-registry',f.legacyCalls()===0);
  });
  await run('existing runtime and primary path keep priority without Steam queries',async()=>{
   const f=fixture();f.state.running=true;const running=await f.owner.resolveLs();check('runtime-keeps-exact-current-path',running.exe===f.state.path&&running.source==='runtime');
   f.state.running=false;f.files.set(primary,'');const fixed=await f.owner.resolveLs();check('fixed-primary-keeps-priority',fixed.exe===primary&&fixed.source==='primary');check('both-fast-paths-do-no-Steam-query',f.queries()===0&&f.legacyCalls()===0);
  });
  await run('settled installation request is fresh on the next resolver call',async()=>{
   const f=fixture();await f.owner.resolveLs();const moved='T:\\MovedSteam',next=moved+'\\steamapps\\common\\Lossless Scaling\\LosslessScaling.exe';f.setReceipt(facts([moved]));f.files.set(next,'');
   const r=await f.owner.resolveLs();check('next-request-uses-moved-root',r.exe===next&&r.source==='steam');check('settled-facts-are-not-cached',f.queries()===2&&f.legacyCalls()===0);
  });
  await run('unknown and failed producer stay unknown with no private fallback',async()=>{
   for(const value of [null,{},[],{available:false},facts([steam])]){
    const f=fixture();f.setReceipt(value);if(value?.available===true)f.setError(Error('inert IPC failure'));let error;try{await f.owner.resolveLs();}catch(e){error=e;}
    check('unknown-producer-refuses-resolution',String(error?.message).includes('Steam 安装信息尚未确认'));
    check('unknown-does-not-fall-back-to-registry-or-library',f.legacyCalls()===0&&f.reads.every(x=>x[1]===primary));
    f.setError(null);f.setReceipt(facts([steam]));check('next-real-read-can-recover', (await f.owner.resolveLs()).exe===target&&f.queries()===2);
   }
  });
  await run('installation envelope and paths remain bounded metadata only',async()=>{
   for(const value of [{...facts([steam]),version:2},{...facts([steam]),requested:3},{...facts([steam]),installations:Array(8).fill({path:steam,error:0})},{...facts([steam]),installations:{path:steam}}]){
    const f=fixture();f.setReceipt(value);check('invalid-envelope-is-unknown',await f.shared.installationRoots()===null);
   }
   const f=fixture();f.setReceipt({...facts([]),installations:[{path:'relative',error:0},{path:'C:\\bad\0path',error:0},{path:steam,error:5},{path:steam,error:0},{path:'s:/Steam',error:0},{path:'\\\\server\\share\\Steam',error:0},{path:17,error:0}]});
   const roots=await f.shared.installationRoots();check('only-successful-absolute-unique-roots-consumed',JSON.stringify(roots)===JSON.stringify([steam,'\\\\server\\share\\Steam']));check('shared-receipt-is-immutable',Object.isFrozen(roots));check('metadata-only-query-does-no-files-or-registry',f.legacyCalls()===0&&f.reads.length===0);
  });
  return {pass:true,selection:'steam-installations',cases,checks,businessCasesExecuted:cases.length,defaultParent:false,realSteamOperations:0,physicalLosslessActions:0,wholeGoalComplete:false};
 }catch(error){return {pass:false,selection:'steam-installations',cases,checks,failedCase:activeCase,error:String(error.stack??error),defaultParent:false,realSteamOperations:0,physicalLosslessActions:0,wholeGoalComplete:false};}
}
