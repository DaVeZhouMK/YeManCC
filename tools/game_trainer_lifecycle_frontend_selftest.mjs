import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const validation=process.argv[2];
if(!validation)throw Error('Pass the lifecycle validation directory');
const input=JSON.parse(fs.readFileSync(path.join(validation,(process.argv[3]||'verified')+'.json'),'utf8').replace(/^\uFEFF/,''));
const mocks={
 'frontendDiagnostics.ts':`export const reportFrontendError=()=>{};`,
 'api.ts':`const f=globalThis.fixture;export const app={powerControlDir:async()=> 'R:/Backend',dataDir:async()=> 'R:/Cache',exeDir:async()=> 'R:/App'};export const fs={exists:async p=>f.files.has(f.key(p)),mkdir:async()=>{},readTextFile:async p=>f.files.get(f.key(p)),writeTextFileAtomic:async(p,v)=>f.files.set(f.key(p),v),remove:async p=>f.files.delete(f.key(p)),sha256File:async()=>f.receipt.sha256};export const shell={hidden:async(p,a)=>f.hidden(p,a)};`,
 'gamedetect.ts':`export const cleanGameTitle=s=>s.trim();`,
 'settingsRepository.ts':`export const getSettingsGeneration=()=>0;`,
};
const bundled=await build({entryPoints:[path.join(root,'src/bridge/gameTrainer.ts')],bundle:true,platform:'node',format:'cjs',write:false,plugins:[{name:'offline-owner',setup(b){b.onLoad({filter:/[\\/](api|gamedetect|settingsRepository|frontendDiagnostics)\.ts$/},a=>({contents:mocks[path.basename(a.path)],loader:'ts'}));}}]});
const key=p=>p.replaceAll('/',String.fromCharCode(92)).toLowerCase();
function load(receipt,gameExe){
 const f={receipt,files:new Map(),key,preparations:0,launches:0};
 for(const p of ['R:/Backend/YeManTrainerPrepare.ps1','R:/Backend/YeManTrainerLaunch.ps1',gameExe,receipt.trainerPath].filter(Boolean))f.files.set(key(p),'offline-placeholder');
 const arg=(a,n)=>a[a.indexOf(n)+1];
 f.hidden=async(_p,a)=>{const output=arg(a,'-ResultPath'),requestId=arg(a,'-RequestId');
  if(arg(a,'-File').endsWith('YeManTrainerPrepare.ps1')){f.preparations++;f.files.set(key(output),JSON.stringify({...receipt,gameName:arg(a,'-GameName'),requestId}));}
  else{assert(arg(a,'-File').endsWith('YeManTrainerLaunch.ps1'));assert.equal(key(arg(a,'-GameExecutablePath')),key(gameExe));assert.equal(arg(a,'-ExpectedSha256'),receipt.sha256);assert.equal(key(arg(a,'-TrainerPath')),key(receipt.trainerPath));if(!f.files.has(key(receipt.trainerPath))){f.files.set(key(output),JSON.stringify({requestId,ok:false,code:'trainer-unavailable',error:'修改器文件已被移除'}));}else{f.launches++;f.files.set(key(output),JSON.stringify({requestId,ok:true}));}}
  return{ok:true};
 };
 const module={exports:{}};vm.runInNewContext(bundled.outputFiles[0].text,{module,exports:module.exports,fixture:f,console,setTimeout,clearTimeout});return{owner:module.exports,f};
}
const results=[];
for(const game of input.games){
 for(const [kind,receipt]of Object.entries(game.receipts)){
  if(game.expectedBlocked){
   assert.equal(receipt.state,'failed',`${game.appid}/${kind} identity refusal`);
   const gameExe=game.gameExe||'R:/Blocked/Game.exe';const {owner:t,f}=load(receipt,gameExe);
   const result=await t.prepareGameTrainer(receipt.gameName,{processPath:gameExe,automatic:true});assert.equal(result.state,'failed');
   assert.equal(t.getReadyGameTrainer(receipt.gameName,gameExe),null);
   await assert.rejects(t.launchReadyGameTrainer(receipt.gameName,gameExe,{isTargetCurrent:async()=>true}),/未找到/);
   assert.equal(f.launches,0);results.push({appid:game.appid,kind,confirmed:false,identityConflictRejected:true,actualLaunches:0});continue;
  }
  assert.equal(receipt.state,'ready',`${game.appid}/${kind} backend ready`);
  const gameExe=kind==='unbound'?game.unboundExe:game.gameExe;
  const {owner:t,f}=load(receipt,gameExe);await t.prepareGameTrainer(receipt.gameName,{processPath:gameExe,automatic:true});
  const confirmed=kind!=='unbound';assert.equal(!!t.getReadyGameTrainer(receipt.gameName,gameExe),confirmed);
  assert.equal(t.getReadyGameTrainer(receipt.gameName,gameExe+'.other.exe'),null);
  if(!confirmed){await assert.rejects(t.launchReadyGameTrainer(receipt.gameName,gameExe,{isTargetCurrent:async()=>true}),/未找到/);assert.equal(f.launches,0);}
  else{
   await assert.rejects(t.launchReadyGameTrainer(receipt.gameName,gameExe,{isTargetCurrent:async()=>false}),/退出或更换/);assert.equal(f.launches,0);
   const options={isTargetCurrent:async()=>true},a=t.launchReadyGameTrainer(receipt.gameName,gameExe,options),b=t.launchReadyGameTrainer(receipt.gameName,gameExe,options);
   assert.equal(a,b);await Promise.all([a,b]);assert.equal(f.launches,1);
   f.files.delete(key(receipt.trainerPath));await assert.rejects(t.launchReadyGameTrainer(receipt.gameName,gameExe,options),/已被移除/);assert.equal(t.getReadyGameTrainer(receipt.gameName,gameExe),null);
  }
  assert.equal(f.preparations,1,'Ready-only clicks never start another preparation worker');
  results.push({appid:game.appid,kind,confirmed,wrongPathRejected:true,targetSwitchRejected:confirmed,duplicateClicksMerged:confirmed,removedFileRetired:confirmed,newPreparationsFromClicks:0,actualLaunches:0});
 }
}
const report={suite:'Production backend receipts consumed by production frontend owner',samples:input.sampleCount,receiptFlows:results.length,confirmed:results.filter(x=>x.confirmed).length,unconfirmedRejected:results.filter(x=>!x.confirmed).length,realNetworkRequests:0,realDownloads:0,realLaunches:0,results};
fs.writeFileSync(path.join(validation,'frontend-receipts.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({...report,results:undefined},null,2));
