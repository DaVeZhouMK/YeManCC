import {lockBundledNodeTools,classifyBundledInput,verifyBundledToolScope,bundledArtifacts,verifyBundledImplementation} from '../bundled-node-tool-authority.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
const [repoRoot,evidenceDir]=process.argv.slice(2);const require=createRequire(path.join(repoRoot,'package.json'));let esbuild,authority;const loadedTools=new Map();
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),inputs=new Map(),tool='tools/ymcc_sim/global/update-download-signals';
const result={schemaVersion:1,status:'FAIL',stage:'build',originalTests:[],stabilityTests:[],checks:[],executedProductModules:[],compiledInputs:[],bundles:[],remainingGaps:['Native downloader/HTTP/Range/hash/filesystem/install behavior is not executed; memory IPC only.','Actual update UI component rendering is not exercised; provider reactive feedback is.','No actual network, installation, service startup, user configuration/token/session or C-install file operation.']};
const gameSource="const f=globalThis.__updaterGameFixture;export const getPolicyGame=()=>f.game;export const isPolicyGameInitialized=()=>f.known;export const subscribePolicyGameStatus=fn=>{f.listeners.add(fn);return()=>f.listeners.delete(fn);};export const isPolicyTargetCurrent=()=>false;export const detectPolicyGame=async()=>f.game;export const gamePolicyKey=()=>'';export const samePolicyGame=(a,b)=>a===b;";
const plugin={name:'reviewed-game-memory-boundary-and-exact-read-hashes',setup(build){
 build.onResolve({filter:/gamePolicyTarget$/},()=>({path:'gamePolicyTarget',namespace:'fixture'}));build.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:gameSource,loader:'js'}));
 build.onLoad({filter:/\.(?:[cm]?js|tsx?|json)$/},args=>{const bytes=fs.readFileSync(args.path),observed=classifyBundledInput(repoRoot,args.path,bytes,authority),target=observed.role==='tool'?loadedTools:inputs;const previous=target.get(observed.row.path),prior=typeof previous==='string'?previous:previous?.sha256;if(prior&&prior!==observed.row.sha256)throw Error('SOURCE_CHANGED_DURING_COMPILE:'+observed.row.path);target.set(observed.row.path,observed.role==='tool'?observed.row:observed.row.sha256);return {contents:bytes,loader:path.extname(args.path)==='.ts'?'ts':path.extname(args.path)==='.json'?'json':'js'};});
}};
function guardBanner(id){return `import {writeFileSync as __writeGuard} from 'node:fs';const __updateIO={fetch:0,userStorage:0};globalThis.fetch=()=>{__updateIO.fetch++;throw Error('REAL_NETWORK_FORBIDDEN');};globalThis.localStorage={getItem(){__updateIO.userStorage++;throw Error('USER_SETTINGS_READ_FORBIDDEN');},setItem(){__updateIO.userStorage++;throw Error('USER_SETTINGS_WRITE_FORBIDDEN');},removeItem(){__updateIO.userStorage++;throw Error('USER_SETTINGS_WRITE_FORBIDDEN');}};process.on('exit',()=>{__writeGuard(${JSON.stringify(path.join(evidenceDir,id+'.io-guard.json'))},JSON.stringify(__updateIO));if(__updateIO.fetch||__updateIO.userStorage)process.exitCode=1;});`;}

const gameIOPlugin={name:'actual-game-policy-memory-io',setup(build){
 // The recovery case now supplies native game.detect IPC directly; compile
 // the real shared detector, scheduler and policy facade unchanged.
 build.onLoad({filter:/\.(?:[cm]?js|tsx?|json)$/},args=>{const bytes=fs.readFileSync(args.path),observed=classifyBundledInput(repoRoot,args.path,bytes,authority),target=observed.role==='tool'?loadedTools:inputs;const previous=target.get(observed.row.path),prior=typeof previous==='string'?previous:previous?.sha256;if(prior&&prior!==observed.row.sha256)throw Error('SOURCE_CHANGED_DURING_COMPILE:'+observed.row.path);target.set(observed.row.path,observed.role==='tool'?observed.row:observed.row.sha256);return {contents:bytes,loader:path.extname(args.path)==='.ts'?'ts':path.extname(args.path)==='.json'?'json':'js'};});
}};
async function bundle(id,entry,usedPlugin=plugin){const output=path.join(evidenceDir,id+'.mjs');const built=await esbuild.build({absWorkingDir:repoRoot,entryPoints:[entry],outfile:output,bundle:true,platform:'node',format:'esm',target:'node22',alias:{'@':path.join(repoRoot,'src')},metafile:true,logLevel:'silent',plugins:[usedPlugin],banner:{js:guardBanner(id)}});fs.writeFileSync(path.join(evidenceDir,id+'.metafile.json'),JSON.stringify(built.metafile,null,2));result.bundles.push({id,path:path.basename(output),sha256:sha(fs.readFileSync(output)),esbuildVersion:esbuild.version});return output;}
function execute(id,file,args=[]){const child=spawnSync(process.execPath,[file,...args],{cwd:repoRoot,encoding:'utf8',windowsHide:true,timeout:15000,maxBuffer:2*1024*1024});const text=(child.stdout??'')+(child.stderr??'');fs.writeFileSync(path.join(evidenceDir,id+'.log'),text);return {exitCode:child.status,errorCode:child.error?.code??null,stdout:child.stdout??''};}
try{
 verifyBundledImplementation(repoRoot);authority=lockBundledNodeTools(repoRoot);result.bundledToolAuthority=authority;esbuild=require(authority.compilerEntry);
 const originals=[['scheduler-original','tools/updater_predownload_selftest.ts','UPDATER_PREDOWNLOAD_SELFTEST_OK'],['integration-original','tools/updater_predownload_integration_selftest.ts','UPDATER_PREDOWNLOAD_INTEGRATION_SELFTEST_OK'],['service-original','tools/updater_service_game_selftest.ts','UPDATER_SERVICE_GAME_SELFTEST_OK'],['successor-original','tools/updater_successor_selftest.ts','UPDATER_SUCCESSOR_SELFTEST_OK']];
 for(const[id,entry,marker]of originals){const file=await bundle(id,entry),ran=execute(id,file);const count=new RegExp(marker+' checks=([0-9]+)').exec(ran.stdout);result.originalTests.push({id,path:entry,status:ran.exitCode===0&&Number(count?.[1])>0?'PASS':'FAIL',checks:Number(count?.[1]??0),exitCode:ran.exitCode,errorCode:ran.errorCode,scope:'behavioral-mock',safety:'memory WebView IPC/fake clock only; install command in integration is inert memory feedback, no native handler'});}
 for(const[id,entry,args]of [['scheduler',`${tool}/scheduler-fixture.ts`,[]],['activity',`${tool}/memory-fixture.ts`,['activity-rejected']],['idle-resync',`${tool}/memory-fixture.ts`,['idle-resync']],['unknown-post',`${tool}/memory-fixture.ts`,['unknown-post']],['version',`${tool}/memory-fixture.ts`,['version-fence']]]){
  const file=await bundle(id,entry),ran=execute(id,file,args);const line=ran.stdout.split(/\r?\n/).find(value=>value.startsWith('UPDATE_DOWNLOAD_RECEIPT '));if(!line)throw Object.assign(Error('CONSUMER_RECEIPT_MISSING:'+id),{code:'CONSUMER_RECEIPT_MISSING'});
  const receipt=JSON.parse(line.slice('UPDATE_DOWNLOAD_RECEIPT '.length));result.checks.push(...receipt.checks);result.executedProductModules.push(...receipt.executedProductModules);if(ran.exitCode!==0||receipt.status!=='PASS')result.failure={code:'FORMAL_CONSUMER_FAILED'};
 }
 const stabilityFile=await bundle('stability','tools/updater_stability_selftest.ts');
 for(const mode of ['missing-start','active-stall','transport-reset','slow-progress','cleanup-running','cleanup-probe','cleanup-no-proof','cleanup-error','cleanup-busy','cleanup-idle','late-check','late-initialize','manual-protection','scheduler-quiet']){
  const ran=execute('stability-'+mode,stabilityFile,[mode]);let receipt;try{receipt=JSON.parse(ran.stdout.trim());}catch{}
  result.stabilityTests.push({id:mode,status:ran.exitCode===0&&receipt?.status==='PASS'&&receipt.checks>0?'PASS':'FAIL',exitCode:ran.exitCode,receipt});
 }
 const gameFile=await bundle('game-recovery','tools/updater_game_recovery_selftest.ts',gameIOPlugin),gameRan=execute('game-recovery',gameFile);let gameReceipt;try{gameReceipt=JSON.parse(gameRan.stdout.trim());}catch{}
 result.stabilityTests.push({id:'game-recovery',status:gameRan.exitCode===0&&gameReceipt?.status==='PASS'&&gameReceipt.checks===10?'PASS':'FAIL',exitCode:gameRan.exitCode,receipt:gameReceipt});
 if(gameRan.exitCode===0&&gameReceipt?.status==='PASS')result.executedProductModules.push('src/bridge/gamePolicyTarget.ts','src/bridge/gamePolicyHysteresis.ts');
 result.ioGuards=result.bundles.map(item=>({id:item.id,...JSON.parse(fs.readFileSync(path.join(evidenceDir,item.id+'.io-guard.json'),'utf8'))}));
 result.compiledInputs=[...inputs].map(([path,sha256])=>({path,sha256})).sort((a,b)=>a.path.localeCompare(b.path));result.executedProductModules=[...new Set(result.executedProductModules)];
 result.changedCompiledInputs=result.compiledInputs.filter(item=>sha(fs.readFileSync(path.join(repoRoot,item.path)))!==item.sha256);
 result.compiledToolInputs=[...loadedTools.values()].sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
 result.toolAuthorityAfter=verifyBundledToolScope(authority,repoRoot);
 const ownedBundles=result.bundles.flatMap(row=>[path.join(evidenceDir,row.path),path.join(evidenceDir,row.path.slice(0,-4)+'.metafile.json')]).map(file=>({path:file,sha256:sha(fs.readFileSync(file)),status:'OK'}));
 result.toolArtifacts=bundledArtifacts(authority,[],ownedBundles);result.sourceHashes=result.compiledInputs;

 result.inputConsumerStateFeedback=result.checks.map(item=>({id:item.id,...item.inputConsumerStateFeedback}));
 result.injectedBoundary={module:'gamePolicyTarget',sourceSha256:sha(Buffer.from(gameSource)),role:'memory game-admission IO; source modules are otherwise formal'};
 result.compatibility={schemaVersion:1,hcRuntimeRequired:false,requiredModules:['src/bridge/updatePredownloadService.ts','src/bridge/updatePredownloadScheduler.ts','src/bridge/updatePredownload.ts','src/bridge/updateManager.ts','src/bridge/yeman.ts','src/bridge/ipc.ts','src/version.ts'],originalAssertionsPreserved:true,missingInterfaceBehavior:'actual source compile or required receipt fails'};
 result.status=result.stabilityTests.length===15&&result.stabilityTests.every(item=>item.status==='PASS')&&result.originalTests.every(item=>item.status==='PASS')&&result.checks.length===23&&result.checks.every(item=>item.status==='PASS')&&result.ioGuards.every(item=>item.fetch===0&&item.userStorage===0)&&!result.changedCompiledInputs.length?'PASS':'FAIL';result.stage='complete';
}catch(error){result.failure={code:error.code??error.message,exceptionType:error.name};result.compiledInputs=[...inputs].map(([path,sha256])=>({path,sha256}));}
fs.writeFileSync(path.join(evidenceDir,'worker-result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({status:result.status,checks:result.checks.length,originals:result.originalTests.map(item=>({id:item.id,status:item.status,checks:item.checks})),failure:result.failure??null}));process.exitCode=result.status==='PASS'?0:1;

