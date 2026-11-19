import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';

const prefix='lifecycle.ui-power-', dir='src/bridge/';
const transport=['ipc','powerLifecycleState','uiLifecycle','steamInstallationFacts','inputConfigHash','settingsBaseline','bootModeGlobalPreferences','api'].map(name=>dir+name+'.ts');
const shared=[dir+'ipc.ts',dir+'powerLifecycleState.ts'];
const cases={
 [prefix+'transport']:{entry:'tools/ui_power_transport_selftest.mjs',report:'ui-power-transport.json',checks:6,graphs:[['<stdin>','PowerControl/settings-baseline.json',...transport]]},
 [prefix+'recovery']:{entry:'tools/ui_power_recovery_selftest.mjs',report:'ui-power-recovery.json',checks:47,graphs:[['<stdin>',...shared,dir+'uiLifecycle.ts'],shared],extra:['src/App.vue','tools/fixtures/ui_shared_power_cases.mjs']},
};
const contractFile='tools/ymcc_sim/optimization/ui-power-cache-contract.mjs';
const buildReceipt='tools/fixtures/ui_power_build_receipt.mjs';
// Include exact entry bytes: a new fixture recipe needs its own graph review.
const fixtureHashes={
 'tools/ui_power_transport_selftest.mjs':'e09ea9e626b57415c656bac1951f4f220685cf29828c5c20cf9282ca765dd98a',
 'tools/ui_power_recovery_selftest.mjs':'630c2fbb4ba2956223cd3b59ca08536895d03fc67aefe2854337ae2d537c83ac',
 [buildReceipt]:'db80c5cb83d93067ccf59331def58078d0b8dbab0b0f3dbc799134731b0fdfd4',
 'tools/fixtures/ui_shared_power_cases.mjs':'bc86fe46aa7bf2c24bb745975244092c16e06015dc9caa82544e723a1fb3e7b7',
};
const esbuildJs='ac0772cb9176e4dd1d896be746b82edff699103940c8535a286493cecb69bdee';
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const json=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const key=file=>path.resolve(file).toLowerCase();
const sort=rows=>[...rows].sort();
const normalize=file=>file.replaceAll('\\','/');
const blockedEnv=/^(?:NODE_OPTIONS|NODE_PATH|ESBUILD_.+|YMCC_PUBLIC_POWER_GRAPH_DIR)$/i;

export function configureUiPowerCache(adapter,repo,env=process.env){
 const c=Object.hasOwn(cases,adapter.id)?cases[adapter.id]:null;
 if(!c||adapter.kind!=='node'||adapter.path!==c.entry||JSON.stringify(adapter.args)!=='["--out-dir","{outDir}"]'||process.platform!=='win32'||process.arch!=='x64')return adapter;
 try{
  if(Object.keys(env).some(k=>blockedEnv.test(k)&&env[k])||process.versions.pnp)return adapter;
  const fixturePaths=[c.entry,buildReceipt,...(c.extra??[]).filter(p=>Object.hasOwn(fixtureHashes,p))];
  for(const p of fixturePaths)if(hash(path.join(repo,p))!==fixtureHashes[p])return adapter;
  const compilerConfig=json(path.join(repo,'tsconfig.json'));
  if(compilerConfig.extends!==undefined||fs.existsSync(path.join(repo,'jsconfig.json')))return adapter;
  // No nested package/tsconfig can silently change the reviewed resolution.
  const prefixes=['src','src/bridge','PowerControl','tools','tools/fixtures'];
  for(const p of prefixes)for(const name of ['tsconfig.json','jsconfig.json','package.json'])if(fs.existsSync(path.join(repo,p,name)))return adapter;
  const require=createRequire(path.join(repo,'package.json'));
  const front=fs.realpathSync(require.resolve('esbuild')),backRequire=createRequire(front);
  const frontPackage=fs.realpathSync(require.resolve('esbuild/package.json'));
  const back=fs.realpathSync(backRequire.resolve('@esbuild/win32-x64/esbuild.exe'));
  const backPackage=fs.realpathSync(backRequire.resolve('@esbuild/win32-x64/package.json'));
  if(key(front)!==key(path.join(path.dirname(frontPackage),'lib/main.js'))||key(back)!==key(path.join(path.dirname(backPackage),'esbuild.exe'))||hash(front)!==esbuildJs)return adapter;
  if(json(frontPackage).version!=='0.21.5'||json(backPackage).version!=='0.21.5')return adapter;
  const sources=[...new Set([...adapter.evidenceInputs,contractFile,buildReceipt,'package.json','pnpm-lock.yaml','tsconfig.json','tsconfig.node.json',...(c.extra??[]),...c.graphs.flat().filter(p=>p!=='<stdin>')])];
  for(const p of [adapter.path,...sources])if(key(fs.realpathSync(path.join(repo,p)))!==key(path.join(repo,p)))return adapter;
  return {...adapter,evidenceInputs:sources,artifactInputs:[front,frontPackage,back,backPackage],uiPowerCacheContract:{schemaVersion:1,report:c.report,checks:c.checks,graphs:c.graphs.map(sort),compilerEntry:front,compilerExecutable:back}};
 }catch{return adapter;}
}

export function uiPowerCacheStillQualified(a,repo){
 if(!a.uiPowerCacheContract)return false;
 const {uiPowerCacheContract,artifactInputs,...base}=a;
 const fresh=configureUiPowerCache(base,repo);
 return JSON.stringify(fresh.uiPowerCacheContract)===JSON.stringify(uiPowerCacheContract)&&JSON.stringify(fresh.artifactInputs)===JSON.stringify(artifactInputs);
}

export function uiPowerResolutionMembership(a,repo){
 const dirs=new Map();
 for(const name of a.uiPowerCacheContract.graphs.flat()){
  if(name==='<stdin>')continue;
  const directory=path.dirname(name),stem=path.basename(name,path.extname(name)).toLowerCase();
  if(!dirs.has(directory))dirs.set(directory,new Set());
  dirs.get(directory).add(stem);
 }
 return sort(dirs.keys()).map(directory=>({path:directory,entries:fs.readdirSync(path.join(repo,directory),{withFileTypes:true})
  .filter(e=>[...dirs.get(directory)].some(stem=>e.name.toLowerCase()===stem||e.name.toLowerCase().startsWith(stem+'.')))
  .map(e=>({name:e.name,kind:e.isSymbolicLink()?'link':e.isDirectory()?'directory':'file'})).sort((a,b)=>a.name.localeCompare(b.name))}));
}

export function verifyUiPowerBuildReceipt(a,result,repo){
 const c=a.uiPowerCacheContract;
 assert(c&&result.id===a.id&&result.status==='PASS'&&result.exitCode===0&&result.sourceChanged===false&&result.candidateArtifactChanged===false,'UI_POWER_CACHE_RUN_NOT_QUALIFIED');
 const file=path.join(result.evidenceDir,c.report);
 const rows=result.structuredEvidence?.filter(row=>key(row.path)===key(file));
 assert(rows?.length===1&&rows[0].failed===false,'UI_POWER_CACHE_REPORT_NOT_BOUND');
 const bytes=fs.readFileSync(file);
 assert.equal(createHash('sha256').update(bytes).digest('hex'),rows[0].sha256,'UI_POWER_CACHE_REPORT_CHANGED');
 const data=JSON.parse(bytes);
 assert.equal(data.pass,true);assert.equal(data.cases,c.checks);
 assert(Array.isArray(data.checks)&&data.checks.length===c.checks&&new Set(data.checks).size===c.checks,'UI_POWER_CACHE_CASE_SET');
 const build=data.observedBuilds;
 assert.deepEqual(build?.graphs,c.graphs,'UI_POWER_CACHE_BUILD_GRAPH');
 assert.deepEqual(build.externalImports,c.graphs.map(()=>[]),'UI_POWER_CACHE_EXTERNAL_IMPORTS');
 for(const [field,expected] of [['compilerEntry',c.compilerEntry],['compilerExecutable',c.compilerExecutable],['nodeExecutable',process.execPath]]){
  assert(typeof build[field]==='string'&&path.isAbsolute(build[field])&&key(build[field])===key(expected),'UI_POWER_CACHE_TOOL:'+field);
 }
 const declared=new Map(result.sourceInputs.filter(row=>row.status==='OK').map(row=>[normalize(row.path),row.sha256]));
 for(const [name,sha] of Object.entries(data.sourceHashes))assert.equal(sha,declared.get(normalize(name)),'UI_POWER_CACHE_SOURCE_UNBOUND:'+name);
 for(const name of new Set(c.graphs.flat().filter(p=>p!=='<stdin>')))assert.equal(declared.get(name),hash(path.join(repo,name)),'UI_POWER_CACHE_SOURCE_DRIFT:'+name);
}

export function verifyUiPowerParentReturn(result,cacheEntry){
 const caseDir=path.dirname(cacheEntry.result.path),casesDir=path.dirname(caseDir),runDir=path.dirname(casesDir);
 assert.equal(key(caseDir),key(path.join(runDir,'cases',result.id)),'UI_POWER_CACHE_CASE_PATH');
 assert.equal(key(result.evidenceDir),key(caseDir),'UI_POWER_CACHE_RESULT_SCOPE');
 const readPinned=name=>{
  const file=path.join(runDir,name);
  const pins=cacheEntry.evidencePins.filter(row=>key(row.path)===key(file));
  assert.equal(pins.length,1,'UI_POWER_CACHE_PARENT_PIN:'+name);
  const bytes=fs.readFileSync(file);
  assert.equal(createHash('sha256').update(bytes).digest('hex'),pins[0].sha256,'UI_POWER_CACHE_PARENT_CHANGED:'+name);
  return JSON.parse(bytes);
 };
 const parent=readPinned('result.json'),budget=readPinned('execution-budget.json'),job=readPinned('owned-job.json');
 assert(parent.schemaVersion===1&&key(parent.runDir)===key(runDir)&&['PASS_SELECTED_SCOPE','COMPLETE_WITH_CACHED_REFERENCES'].includes(parent.status),'UI_POWER_CACHE_PARENT_RESULT');
 const rows=parent.rows.filter(row=>row.id===result.id),row=rows[0];
 assert(rows.length===1&&row.status==='PASS'&&row.attemptedThisRun===true&&row.executedThisRun===true&&row.actualPassedThisRun===true&&key(row.reference.path)===key(cacheEntry.result.path)&&row.reference.sha256===cacheEntry.result.sha256,'UI_POWER_CACHE_PARENT_CASE_BINDING');
 assert(budget.schemaVersion===1&&key(budget.runDir)===key(runDir)&&budget.scope==='optimized-adapters'&&budget.status===parent.status&&budget.workerStatus===parent.status&&budget.workerEvidenceError===null&&budget.actualPassedThisRun===parent.summary.actualPassedThisRun&&budget.actualPassedThisRun>0&&budget.exitCode===0&&budget.processExitCode===0&&budget.timedOut===false&&budget.sourceIdentity?.status==='MATCH'&&budget.resourceZeroVerified===true&&budget.ownedJobVerified===true,'UI_POWER_CACHE_PARENT_BUDGET');
 assert.deepEqual(budget.ownedJob,job,'UI_POWER_CACHE_PARENT_JOB_BINDING');
 assert(job.schemaVersion===1&&job.resourceObservationVersion===1&&job.status==='EXITED'&&job.rootExitCode===0&&job.exitCode===0&&job.deadlineExpired===false&&job.jobAssigned===true&&job.childExitedBeforeReturn===true&&job.jobDisposed===true&&job.zeroObservedBeforeDispose===true&&job.activeProcessesFinal===0&&job.terminationRequested===false&&job.cleanupError===null&&Number.isInteger(job.childPid)&&job.childPid>0&&Number.isInteger(job.jobQueryCount)&&job.jobQueryCount>0,'UI_POWER_CACHE_PARENT_NOT_NATURAL_ZERO');
}
