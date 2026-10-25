/** Cost-aware front door. Reuses original registered adapters and executor. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {getAdapters} from '../global/adapters.mjs';
import {getSleepAdapters} from '../global/sleep.mjs';
import {executeAdapter,validateAdapters,sha256File,makeRunDirectory} from '../global/runner.mjs';
import {configureCandidateAdapters} from '../global/candidate-options.mjs';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools} from '../global/bundled-node-tool-authority.mjs';
import {fingerprintCase,inspectCachedResult,deduplicateCases} from './identity-cache.mjs';
const REPO=path.resolve(import.meta.dirname,'../../..');
const CORE=path.resolve(REPO,'../../..','Testing/YMCC');
const CONFIG=JSON.parse(fs.readFileSync(new URL('./modules.json',import.meta.url),'utf8'));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const pin=file=>({path:path.resolve(file),sha256:sha256File(file)});
const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
export function parseOptions(args){
 const o={mode:'plan',tier:'minimal',profile:'desktop',modules:[],tests:[],fullAcceptance:false,rerun:false};
 const aliases={'out-dir':'outDir','payload-root':'payloadRoot','runtime-root':'runtimeRoot','shared-runtime-root':'sharedRuntimeRoot','lighting-payload-root':'lightingPayloadRoot','expected-helper-sha256':'expectedHelperSha256','hc-profile':'hcProfile','hc-version':'hcVersion','legacy-host-root':'legacyHostRoot','fan-tool-load-plan':'fanToolLoadPlan','fan-tool-load-plan-sha256':'fanToolLoadPlanSha256'};
 for(let i=0;i<args.length;i++){
  const a=args[i];if(a==='--full-acceptance'){o.fullAcceptance=true;continue;}if(a==='--rerun'){o.rerun=true;continue;}
  if(a==='--help'){o.help=true;continue;}
  const key=a.slice(2);if(!a.startsWith('--')||!['mode','tier','profile','module','test','powershell',...Object.keys(aliases)].includes(key)||!args[i+1]||args[i+1].startsWith('--'))throw Error('INVALID_OPTIMIZATION_ARGUMENT:'+a);
  const value=args[++i];if(key==='module'||key==='test')o[key==='module'?'modules':'tests'].push(...value.split(',').filter(Boolean));else o[aliases[key]??key]=value;
 }
 if(!['plan','run','full'].includes(o.mode)||!['minimal','module'].includes(o.tier)||!['desktop','full','sleep'].includes(o.profile))throw Error('INVALID_OPTIMIZATION_MODE');
 if(o.mode==='full'&&!o.fullAcceptance)throw Error('FULL_ACCEPTANCE_EXPLICIT_REQUIRED');
 if(o.mode==='full'){o.profile='full';o.tier='module';}
 o.modules=o.modules.map(x=>CONFIG.aliases[x]??x);
 for(const m of o.modules)if(!CONFIG.modules[m])throw Error('UNKNOWN_MODULE:'+m);
 if(!o.help&&!o.modules.length&&!o.tests.length&&o.mode!=='plan'&&o.mode!=='full')throw Error('EXPLICIT_MODULE_OR_TEST_REQUIRED');
 return o;
}
export async function selectCases(o,repoRoot=REPO){
 const core=[...getAdapters(repoRoot),...getSleepAdapters(repoRoot,{identityOnly:!(o.mode==='run'&&o.rerun)})];const idx=read(path.join(repoRoot,'tools/ymcc_sim/global/extra-adapter-index.json'));
 const extraRows=idx.groups.flatMap(g=>g.rows);const known=[...core,...extraRows];
 const wanted=[...o.tests];
 for(const m of o.modules){const c=CONFIG.modules[m];wanted.push(...(o.tier==='minimal'?c.minimum:known.filter(a=>(c.domains??[m]).includes(a.domain)).map(a=>a.id)));}
 if(o.mode==='full'&&!wanted.length)wanted.push(...known.map(a=>a.id));
 const unknown=wanted.filter(id=>!known.some(a=>a.id===id));if(unknown.length)throw Error('UNKNOWN_TEST:'+unknown.join(','));
 const selectedExtra=[...new Set(wanted.filter(id=>extraRows.some(a=>a.id===id)))];
 // Planning never materializes legacy factories/tool graphs. Explicit replay may.
 const deferExtra=o.mode!=='run'||!o.rerun;
 const deferred=deferExtra?selectedExtra.map(id=>extraRows.find(a=>a.id===id)).filter(a=>a.profiles.includes(o.profile)):[];
 const extra=selectedExtra.length&&!deferExtra?(await import('../global/extra-adapters.mjs')).getExtraAdapters(repoRoot,{selectedIds:selectedExtra}):[];
 const byId=new Map([...core,...extra].map(a=>[a.id,a]));
 const selected=wanted.map(id=>byId.get(id)).filter(a=>a&&a.profiles.includes(o.profile));
 const unique=deduplicateCases(selected);validateAdapters(unique.canonical,repoRoot);
 return {adapters:configureCandidateAdapters(unique.canonical,o),duplicateReferences:unique.deduplicated,deferred,selectedIds:[...new Set([...unique.canonical.map(a=>a.id),...deferred.map(a=>a.id)])],knownCount:known.length,requested:wanted.length};
}
// Only these reviewed, pure source contracts have no process/compiler dependency.
// A changed test script needs review; product input changes remain automatic.
const PURE_NODE_REVIEWS={"steam.shared-mode-source": "fcf406055e38eb36ac80c58ffbec472b75e45685300e55797f868fd3dd2316cc", "steam.contact-source": "58b9763d4defe366cf4f18fd49fab6a2da24fd6fffd72bc31eabd8981d11047b", "performance.snapshot-bounds-source": "2ca7820d92435738ec5c01d3a0be8ec4d8d4a3183edb2511bf3e9368c0ee8d4a"};
function nodeReview(a){try{return PURE_NODE_REVIEWS[a.id]===sha256File(path.join(REPO,a.path));}catch{return false;}}
const implementation=['tools/ymcc_sim/optimization/cli.mjs','tools/ymcc_sim/optimization/modules.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'];
const PURE_NODE_INPUTS={
 'steam.shared-mode-source':['native/main.cpp','native/steam_ui_mode_observer.h'],
 'steam.contact-source':['src/components/SteamRecoveryDiagnostics.vue','src/views/SettingsView.vue','src/views/SteamView.vue','src/views/SleepGuardView.vue','src/bridge/steamGameLink.ts'],
 'performance.snapshot-bounds-source':['native/main.cpp','native/monitor_snapshot_contract.h','native/monitor_detail_log.h']
};
function resolutionMembership(paths){
 const dirs=new Set(paths.map(p=>path.dirname(path.resolve(REPO,p))));
 return [...dirs].filter(d=>inside(REPO,d)).sort().map(d=>({path:path.relative(REPO,d),entries:fs.readdirSync(d,{withFileTypes:true}).map(e=>({name:e.name,type:e.isSymbolicLink()?'link':e.isDirectory()?'dir':'file'})).sort((a,b)=>a.name.localeCompare(b.name))}));
}
function environmentIdentity(){return hash(JSON.stringify(Object.entries(process.env).filter(([k])=>/^(PATH|NODE_OPTIONS|ESBUILD_BINARY_PATH|YMCC|YEMAN)/i.test(k)).sort()));}
function identityFor(a,previous,o,authority){
 const typed=a.kind==='typescript';const pure=a.kind==='node'&&nodeReview(a);
 const tools=typed?(authority?.tools??[]):[];
 return fingerprintCase({repoRoot:REPO,adapter:a,toolIdentities:tools,options:{closureComplete:(typed&&Boolean(authority))||pure,toolClosureComplete:(typed&&Boolean(authority))||pure,
  resolvedInputs:[...implementation,...(typed?[...BUNDLED_AUTHORITY_SOURCES,'package.json','pnpm-lock.yaml','tsconfig.json','tsconfig.node.json']:[]),...(PURE_NODE_INPUTS[a.id]??[]),...(previous?.dependencyPaths??[])],
  resolutionMembership:typed?resolutionMembership([a.path,...a.evidenceInputs,...(previous?.dependencyPaths??[])]):[],
  profile:o.profile,payloadRoot:o.payloadRoot??null,environmentIdentity:environmentIdentity(),execArgv:process.execArgv,
  closureReview:typed?'registered-reviewed-graph-plus-actual-bundle-inputs':pure?'pinned-pure-node-contract':'legacy-dynamic-closure-not-certified'}});
}
function evidencePins(result,a){
 const dir=result.evidenceDir;const files=[path.join(dir,'result.json'),path.join(dir,'stdout.log'),path.join(dir,'stderr.log'),...(a.resultFiles??[]).map(f=>path.join(dir,f))];
 for(const field of ['structuredEvidence','bundledOutputs','candidateArtifacts'])for(const r of result[field]??[])files.push(path.resolve(REPO,r.path));
 if(result.bundle?.path)files.push(result.bundle.path);
 return [...new Set(files)].filter(f=>fs.existsSync(f)&&fs.statSync(f).isFile()).map(pin);
}
function cacheFile(a){const root=path.join(CORE,'State/ExecutionCache');fs.mkdirSync(root,{recursive:true});return path.join(root,a.id+'.json');}
const cached=file=>{try{return read(file);}catch{return null;}};
function summarize(rows){return {selected:rows.length,executionAttemptsThisRun:rows.filter(r=>r.attemptedThisRun).length,executedThisRun:rows.filter(r=>r.executedThisRun).length,
 actualPassedThisRun:rows.filter(r=>r.actualPassedThisRun).length,cachedReferences:rows.filter(r=>r.status==='CACHED_REFERENCE').length,
 retainedFailureReferences:rows.filter(r=>r.status==='UNCHANGED_FAILURE_REFERENCE').length,notRun:rows.filter(r=>!r.executedThisRun).length};}
export async function main(args=process.argv.slice(2)){
 const start=performance.now(),o=parseOptions(args);
 if(o.help){console.log('Run.ps1 -Optimized [-Mode plan|run|full] -Modules fan,controller [-Tier minimal|module] [-Rerun] [-FullAcceptance]. No module: plan lists minima; full requires explicit acceptance.');return 0;}
 const parent=path.resolve(o.outDir??path.join(CORE,'Evidence/Build/optimized'));
 if(!inside(CORE,parent))throw Error('OPTIMIZATION_EVIDENCE_MUST_STAY_IN_CORE');
 // Reject an existing reparse/symlink ancestor that redirects evidence outside core.
 let cursor=parent;while(!fs.existsSync(cursor))cursor=path.dirname(cursor);if(!inside(fs.realpathSync(CORE),fs.realpathSync(cursor)))throw Error('OPTIMIZATION_EVIDENCE_ESCAPE');
 const {runDir}=makeRunDirectory(parent);const selectionStart=performance.now();
 if(o.mode==='plan'&&!o.modules.length&&!o.tests.length){write(path.join(runDir,'plan.json'),{schemaVersion:1,status:'PLAN_ONLY',modules:CONFIG.modules,executedThisRun:0,fullAcceptance:false});console.log(JSON.stringify({status:'PLAN_ONLY',runDir,modules:Object.keys(CONFIG.modules),wallMs:performance.now()-start}));return 0;}
 const selection=await selectCases(o);if(!selection.adapters.length&&!selection.deferred.length)throw Error('EMPTY_PROFILE_SELECTION');const selectionMs=performance.now()-selectionStart;
 const hasTyped=selection.adapters.some(a=>a.kind==='typescript');let authority=null,toolError=null;
 const identityStart=performance.now();if(hasTyped)try{authority=lockBundledNodeTools(REPO);}catch(e){toolError=String(e.message);}
 const plan=[];for(const a of selection.adapters){const file=cacheFile(a),prior=cached(file);const identity=identityFor(a,prior,o,authority);const inspection=inspectCachedResult({identity,cacheEntry:prior});
  plan.push({id:a.id,domain:a.domain,scope:a.scope,source:a.path,timeoutMs:a.timeoutMs,cacheable:identity.cacheable,reasons:inspection.reasons,decision:o.mode==='full'||o.rerun?'RUN_EXPLICIT':inspection.reusable?inspection.status:'RUN_IF_REQUESTED',identity:identity.identity});}
 for(const a of selection.deferred)plan.push({id:a.id,domain:a.domain,scope:null,source:null,decision:'NOT_RUN_UNPROVEN_CLOSURE',registrationOnly:true,reasons:['Legacy descriptor and tool preparation deferred; use explicit single-case Rerun or full acceptance'],executedThisRun:false});
 const identityMs=performance.now()-identityStart;
 write(path.join(runDir,'plan.json'),{schemaVersion:1,mode:o.mode,tier:o.tier,modules:o.modules,moduleCoverage:o.modules.map(m=>({module:m,description:CONFIG.modules[m].description})),knownCount:selection.knownCount,requested:selection.requested,deduplicated:selection.duplicateReferences,tests:plan,toolError,selectionMs,identityMs,noTestsExecuted:true,fullProductAcceptance:false});
 if(o.mode==='plan'){console.log(JSON.stringify({status:'PLAN_ONLY',runDir,planned:plan.length,duplicateReferences:selection.duplicateReferences.length,selectionMs,identityMs,wallMs:performance.now()-start}));return 0;}
 if(o.mode==='full'){
  // Full acceptance deliberately retains the original lifecycle and coverage gates.
  const legacy=await import('../global/cli.mjs');const argv=['--mode','run','--profile','full','--require-all','--out-dir',path.join(runDir,'full-acceptance')];
  if(o.modules.length||o.tests.length)argv.push('--test',selection.selectedIds.join(','));
  for(const [key,flag]of Object.entries({'payloadRoot':'payload-root','runtimeRoot':'runtime-root','sharedRuntimeRoot':'shared-runtime-root','lightingPayloadRoot':'lighting-payload-root','expectedHelperSha256':'expected-helper-sha256','hcProfile':'hc-profile','hcVersion':'hc-version','legacyHostRoot':'legacy-host-root','fanToolLoadPlan':'fan-tool-load-plan','fanToolLoadPlanSha256':'fan-tool-load-plan-sha256','powershell':'powershell'}))if(o[key])argv.push('--'+flag,o[key]);
  const exitCode=await legacy.main(argv);write(path.join(runDir,'result.json'),{schemaVersion:1,status:'EXPLICIT_FULL_DELEGATED',exitCode,cacheUsed:false,originalLifecycleAndCoverageGates:true,wallMs:performance.now()-start,fullProductAcceptance:false});return exitCode;
 }
 const rows=selection.deferred.map(a=>({id:a.id,status:'NOT_RUN_UNPROVEN_CLOSURE',registrationOnly:true,executedThisRun:false,actualPassedThisRun:false}));for(const a of selection.adapters){const file=cacheFile(a);let lock;
  try{lock=fs.openSync(file+'.lock','wx');fs.writeFileSync(lock,JSON.stringify({pid:process.pid,at:new Date().toISOString(),id:a.id}));}
  catch(e){rows.push({id:a.id,status:'NOT_RUN_BUSY',reason:e.code,executedThisRun:false,actualPassedThisRun:false});continue;}
  try{
   const prior=cached(file);let identity=identityFor(a,prior,o,authority);const inspection=inspectCachedResult({identity,cacheEntry:prior});
   if(inspection.reusable&&!o.rerun){rows.push({id:a.id,status:inspection.status,previousStatus:inspection.previousStatus,reference:prior.result,executedThisRun:false,actualPassedThisRun:false,durationMs:0});continue;}
   if(!identity.cacheable&&!o.rerun){rows.push({id:a.id,status:'NOT_RUN_UNPROVEN_CLOSURE',reason:identity.reasons,executedThisRun:false,actualPassedThisRun:false});continue;}
   const t=performance.now();const result=await executeAdapter(a,{repoRoot:REPO,runDir,powershell:o.powershell??'pwsh.exe',payloadRoot:o.payloadRoot});
   const row={id:a.id,status:result.status,attemptedThisRun:true,executedThisRun:Number.isInteger(result.pid),actualPassedThisRun:result.status==='PASS'&&Number.isInteger(result.pid),durationMs:performance.now()-t,reference:pin(path.join(result.evidenceDir,'result.json'))};rows.push(row);
   const dependencyPaths=(result.dependencyInputs??[]).map(r=>r.path);
   const after=identityFor(a,{dependencyPaths},o,authority);
   // Source/tool drift in the original result remains a failure and is not reusable.
   const entry={schemaVersion:1,identity:after.identity,result:row.reference,evidencePins:evidencePins(result,a),dependencyPaths,recordedAt:new Date().toISOString()};
   const currentPins=new Map([...after.manifest.inputs,...after.manifest.tools].map(r=>[r.role+'|'+r.path,r.sha256]));
   const identityDrift=[...identity.manifest.inputs,...identity.manifest.tools].some(r=>currentPins.get(r.role+'|'+r.path)!==r.sha256);
   if(identityDrift){row.status='INCOMPLETE_IDENTITY_DRIFT';row.actualPassedThisRun=false;row.identityChangedDuringExecution=true;}
   if(after.cacheable&&!identityDrift){const temp=file+'.tmp-'+process.pid;write(temp,entry);fs.renameSync(temp,file);}
   row.cacheable=after.cacheable;row.cacheReasons=after.reasons;
  }finally{fs.closeSync(lock);fs.unlinkSync(file+'.lock');}
 }
 const summary=summarize(rows);const failure=rows.some(r=>['FAIL','TIMEOUT','UNCHANGED_FAILURE_REFERENCE'].includes(r.status));
 const blocked=rows.some(r=>!['PASS','CACHED_REFERENCE'].includes(r.status));
 const result={schemaVersion:1,status:failure?'FAIL':blocked?'INCOMPLETE':summary.cachedReferences?'COMPLETE_WITH_CACHED_REFERENCES':'PASS_SELECTED_SCOPE',runDir,tier:o.tier,modules:o.modules,summary,rows,
  duplicateReferences:selection.duplicateReferences,selectionMs,identityMs,executionMs:rows.reduce((n,r)=>n+(r.durationMs??0),0),wallMs:performance.now()-start,
  sourceCoverage:'Selected registered adapters only; full lifecycle/hardware acceptance is separate',fullAcceptance:false,fullProductAcceptance:false};
 write(path.join(runDir,'result.json'),result);console.log(JSON.stringify({status:result.status,runDir,...summary,selectionMs,identityMs,wallMs:result.wallMs}));return failure?2:blocked?10:0;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().then(code=>process.exitCode=code).catch(error=>{console.error('YMCC_OPTIMIZATION_ERROR '+error.message);process.exitCode=3;});
