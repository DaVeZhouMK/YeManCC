/** Cost-aware front door. Reuses original registered adapters and executor. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {getAdapters} from '../global/adapters.mjs';
import {getSleepAdapters} from '../global/sleep.mjs';
import {executeAdapter,validateAdapters,sha256File,makeRunDirectory} from '../global/runner.mjs';
import {configureCandidateAdapters,validateCommonHidSelection,validateInputResumeSelection} from '../global/candidate-options.mjs';
import {INPUT_RESUME_ID} from '../global/input-resume-options.mjs';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools} from '../global/bundled-node-tool-authority.mjs';
import {fingerprintCase,inspectCachedResult,deduplicateCases} from './identity-cache.mjs';
import {simulationCorePath} from '../global/release-host-environment.mjs';
import {WIRE_ID,wireDescriptorContract} from '../global/controller-lifecycle-pipe/wire-contract.mjs';
import {PREHOST_ID,isPreHostCacheAdapter,preHostDescriptorContract,preHostDependencyPaths,preHostEvidenceFiles} from '../global/controller-foreground-latency/prehost-cache-contract.mjs';
import {HOST_PROTOCOL_ID,HOST_PARENT_ID,isHostLifecycleCacheAdapter,hostLifecycleCacheAdapter,hostLifecycleDescriptorContract,hostLifecycleEvidenceFiles,captureHostProtocolLookupGuards} from '../global/controller-lifecycle-completion/host-cache-contract.mjs';
import {selectReleaseBugs,RELEASE_CANDIDATE_PATH_KEYS} from './release-bugs.mjs';
import {validateSelectionPolicy} from './selection-policy.mjs';
import {configureUiPowerCache,uiPowerCacheStillQualified,uiPowerResolutionMembership,verifyUiPowerBuildReceipt,verifyUiPowerParentReturn} from './ui-power-cache-contract.mjs';
import {runWithWallBudget,readBudgetWorker,validateWallBudget,MAX_WALL_BUDGET_MS} from './execution-budget.mjs';
const REPO=path.resolve(import.meta.dirname,'../../..');
const CORE=simulationCorePath();
const CONFIG_FILE=fileURLToPath(new URL('./modules.json',import.meta.url));
const CONFIG=JSON.parse(fs.readFileSync(CONFIG_FILE,'utf8'));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const pin=file=>({path:path.resolve(file),sha256:sha256File(file)});
const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const write=(file,value)=>{
 const data=JSON.stringify(value,null,2)+'\n',fd=fs.openSync(file,'wx');
 try{fs.writeFileSync(fd,data);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
};
export {write as writeFreshJson};
export function parseOptions(args){
 const o={mode:'plan',tier:'minimal',profile:'desktop',profileExplicit:false,modules:[],tests:[],fullAcceptance:false,rerun:false,referenceOnly:false};
 const aliases={'coordinator-case':'coordinatorCase','coordinator-tool-plan':'coordinatorToolPlan','complete-host-contract':'completeHostContract','bug-plan':'bugPlan','budget-ms':'budgetMs','out-dir':'outDir','payload-root':'payloadRoot','runtime-root':'runtimeRoot','shared-runtime-root':'sharedRuntimeRoot','lighting-payload-root':'lightingPayloadRoot','expected-helper-sha256':'expectedHelperSha256','hc-profile':'hcProfile','hc-version':'hcVersion','legacy-host-root':'legacyHostRoot','fan-tool-load-plan':'fanToolLoadPlan','fan-tool-load-plan-sha256':'fanToolLoadPlanSha256','common-hid-runtime-case':'commonHidRuntimeCase','input-resume-case':'inputResumeCase','expected-main-sha256':'expectedMainSha256','MainFile':'MainFile','HeaderRoot':'HeaderRoot'};
 aliases['coordinator-managed-dll']='coordinatorManagedDll';
 aliases['coordinator-dotnet']='coordinatorDotnet';
 for(let i=0;i<args.length;i++){
  const a=args[i];if(a==='--full-acceptance'){o.fullAcceptance=true;continue;}if(a==='--rerun'){o.rerun=true;continue;}
  if(a==='--common-hid-runtime-only'){if(o.commonHidRuntimeOnly)throw Error('DUPLICATE_COMMON_HID_RUNTIME_ONLY');o.commonHidRuntimeOnly=true;continue;}
   if(a==='--input-resume-only'){if(o.inputResumeOnly)throw Error('DUPLICATE_INPUT_RESUME_ONLY');o.inputResumeOnly=true;continue;}
  if(a==='--reference-only'){o.referenceOnly=true;continue;}
  if(a==='--help'){o.help=true;continue;}
  const key=a.slice(2);if(!a.startsWith('--')||!['mode','tier','profile','module','test','powershell',...Object.keys(aliases)].includes(key)||!args[i+1]||args[i+1].startsWith('--'))throw Error('INVALID_OPTIMIZATION_ARGUMENT:'+a);
   const value=args[++i];if(['input-resume-case','expected-main-sha256'].includes(key)&&o[aliases[key]]!==undefined)throw Error('DUPLICATE_INPUT_RESUME_ARGUMENT:'+key);if(['common-hid-runtime-case','MainFile','HeaderRoot'].includes(key)&&o[aliases[key]]!==undefined)throw Error('DUPLICATE_COMMON_HID_ARGUMENT:'+key);if(key==='coordinator-case'&&o.coordinatorCase!==undefined)throw Error('DUPLICATE_COORDINATOR_CASE');if(key==='coordinator-tool-plan'&&o.coordinatorToolPlan!==undefined)throw Error('DUPLICATE_COORDINATOR_TOOL_PLAN');if(key==='profile')o.profileExplicit=true;
  if(['coordinator-managed-dll','coordinator-dotnet'].includes(key)&&o[aliases[key]]!==undefined)throw Error('DUPLICATE_COORDINATOR_PREREQUISITE:'+key);
  if(key==='module'||key==='test'){
   const ids=value.split(',');if(ids.some(id=>!id.trim()))throw Error('EMPTY_OPTIMIZATION_SELECTION:'+key);
   o[key==='module'?'modules':'tests'].push(...ids);
  }else o[aliases[key]??key]=value;
 }
 o.budgetMs=validateWallBudget(o.budgetMs===undefined?MAX_WALL_BUDGET_MS:Number(o.budgetMs));
 if(!['plan','run','full'].includes(o.mode)||!['minimal','module'].includes(o.tier)||!['desktop','full','sleep'].includes(o.profile))throw Error('INVALID_OPTIMIZATION_MODE');
 if(o.mode==='full'&&!o.fullAcceptance)throw Error('FULL_ACCEPTANCE_EXPLICIT_REQUIRED');
 if(o.referenceOnly&&(o.mode!=='run'||o.rerun||o.fullAcceptance))throw Error('REFERENCE_ONLY_REQUIRES_RUN_WITHOUT_RERUN_OR_FULL');
 if(o.mode==='full'){o.profile='full';o.tier='module';}
 if(o.bugPlan&&(o.modules.length||o.tests.length||o.mode==='full'))throw Error('BUG_PLAN_REQUIRES_EXACT_BUG_SELECTION');
 o.modules=o.modules.map(x=>CONFIG.aliases[x]??x);
 for(const m of o.modules)if(!CONFIG.modules[m])throw Error('UNKNOWN_MODULE:'+m);
 if(o.coordinatorCase!==undefined&&(o.coordinatorCase.trim()!==o.coordinatorCase||!o.coordinatorCase||o.modules.length||o.bugPlan||o.fullAcceptance||o.mode==='full'||o.tests.length!==1||o.tests[0]!=='controller.coordinator-lifecycle'))throw Error('COORDINATOR_CASE_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST');
 if(o.coordinatorToolPlan!==undefined&&o.coordinatorCase!=='onex-diagnostic-tls-startup')throw Error('COORDINATOR_TOOL_PLAN_REQUIRES_ONEX_TLS_CASE');
 if((o.coordinatorManagedDll!==undefined||o.coordinatorDotnet!==undefined)&&!['prepare-rejection-cold','gpd-expired-first-on-full-cycle','gpd-settled-first-on-full-cycle','gpd-win5-elite-quarantine-then-on','wake-kept-source:GPD:expired-then-current','ps-exit-held-ownership-receipt','ps-exit-held-ownership-receipt:pair'].includes(o.coordinatorCase))throw Error('COORDINATOR_PREREQUISITES_REQUIRE_MANAGED_CASE');
 if(!o.help&&!o.bugPlan&&!o.modules.length&&!o.tests.length&&o.mode!=='plan'&&o.mode!=='full')throw Error('EXPLICIT_MODULE_OR_TEST_REQUIRED');
 validateInputResumeSelection(o);validateCommonHidSelection(o);
 return o;
}
export async function selectCases(o,repoRoot=REPO){
 const win5ReturnExit2=process.env.YMCC_FOREGROUND_SHIELD_MODE==='win5-return-exit2';
 if(win5ReturnExit2&&(o.mode==='full'||o.fullAcceptance||o.bugPlan||o.modules.length||o.tests.length!==1||o.tests[0]!==PREHOST_ID))throw Error('WIN5_RETURN_EXIT2_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST');
 const catalog=[...getAdapters(repoRoot,{identityOnly:true}),...getSleepAdapters(repoRoot,{identityOnly:true})];const idx=read(path.join(repoRoot,'tools/ymcc_sim/global/extra-adapter-index.json'));
 const extraRows=idx.groups.flatMap(g=>g.rows);const known=[...catalog,...extraRows];
 const selectionPolicy={...validateSelectionPolicy(known,CONFIG),configuration:pin(path.join(repoRoot,'tools/ymcc_sim/optimization/modules.json'))};
 const bugPlan=o.bugPlan?pin(path.resolve(o.bugPlan)):null;
 const bugSelection=bugPlan?selectReleaseBugs(read(bugPlan.path),{knownIds:known.map(row=>row.id)}):null;
 if(bugSelection && !bugSelection.emptySelection){
  if(bugSelection.profile){
   if(o.profileExplicit && o.profile!==bugSelection.profile)throw Error('BUG_PLAN_PROFILE_CONFLICT');
   o.profile=bugSelection.profile;
  }
  for(const [key,value] of Object.entries(bugSelection.candidateInputs)){
   const normalize=v=>RELEASE_CANDIDATE_PATH_KEYS.has(key)?(process.platform==='win32'?path.resolve(v).toLowerCase():path.resolve(v)):v;
   if(o[key]!==undefined && normalize(o[key])!==normalize(value))throw Error('BUG_PLAN_CANDIDATE_CONFLICT:'+key);
   o[key]=value;
  }
  // Reuse existing consumer validation without reading a contract or loading a factory.
  configureCandidateAdapters([],o);
  bugSelection.profile=o.profile;
 }
 const wanted=bugSelection?[...bugSelection.selectedIds]:[...o.tests];
 for(const m of o.modules){const c=CONFIG.modules[m];wanted.push(...(o.tier==='minimal'?c.minimum:known.filter(a=>(c.domains??[m]).includes(a.domain)).map(a=>a.id)));}
 if(o.mode==='full'&&!wanted.length)wanted.push(...known.map(a=>a.id));
 const unknown=wanted.filter(id=>!known.some(a=>a.id===id));if(unknown.length)throw Error('UNKNOWN_TEST:'+unknown.join(','));
 const excludedExplicit=o.tests.filter(id=>!known.find(a=>a.id===id).profiles.includes(o.profile));
 if(excludedExplicit.length)throw Error('PROFILE_EXCLUDES_EXPLICIT_TEST:'+excludedExplicit.join(','));
 if(bugSelection){
  const excluded=wanted.filter(id=>!known.find(a=>a.id===id)?.profiles.includes(o.profile));
  if(excluded.length)throw Error('BUG_PLAN_PROFILE_EXCLUDES_REQUIRED_TEST:'+excluded.join(','));
 }
 const selectedExtra=[...new Set(wanted.filter(id=>extraRows.some(a=>a.id===id)))];
 const selectedCore=[...new Set(wanted.filter(id=>catalog.some(a=>a.id===id)))];
 const materialize=o.mode==='run'&&o.rerun;
 const core=materialize&&selectedCore.length?[...getAdapters(repoRoot,{selectedIds:selectedCore}),...getSleepAdapters(repoRoot,{selectedIds:selectedCore})]:catalog;
 // Planning never materializes legacy factories/tool graphs. Explicit replay may.
 const deferExtra=o.mode!=='run'||!o.rerun;
 const directWire=o.mode==='run'&&selectedExtra.includes(WIRE_ID);
 const directCoordinator=o.coordinatorCase!==undefined&&selectedExtra.includes('controller.coordinator-lifecycle');
 const preparedCoordinator=!directCoordinator&&o.mode!=='full'&&o.tests.includes('controller.coordinator-lifecycle')&&
  selectedExtra.includes('controller.coordinator-lifecycle')&&Object.hasOwn(process.env,'YMCC_SIM_CURRENT29_LIFECYCLE_PREPARATIONS');
 const directCommonHid=o.commonHidRuntimeOnly===true&&selectedExtra.includes('controller.unified-standard-input');
  const directInputResume=o.inputResumeOnly===true&&selectedExtra.includes(INPUT_RESUME_ID);
 const directHostIds=o.mode==='run'?selectedExtra.filter(id=>[HOST_PROTOCOL_ID,HOST_PARENT_ID].includes(id)):[];
 const directPreHost=selectedExtra.includes(PREHOST_ID)&&(win5ReturnExit2||(o.mode==='run'&&process.env.YMCC_FOREGROUND_SHIELD_MODE==='prehost-writer'));
  const generalExtra=selectedExtra.filter(id=>(!directWire||id!==WIRE_ID)&&!directHostIds.includes(id)&&(!directPreHost||id!==PREHOST_ID)&&(!(directCoordinator||preparedCoordinator)||id!=='controller.coordinator-lifecycle')&&(!directCommonHid||id!=='controller.unified-standard-input')&&(!directInputResume||id!==INPUT_RESUME_ID));
  const deferred=deferExtra?generalExtra.map(id=>extraRows.find(a=>a.id===id)).filter(a=>a.profiles.includes(o.profile)):[];
 const coordinator=directCoordinator?(await import('../global/controller-coordinator-lifecycle/supplemental.mjs')).getSupplementalAdapter({caseName:o.coordinatorCase,deadlineEpochMs:o.coordinatorDeadlineEpochMs,planning:o.mode==='plan',toolPlan:o.coordinatorToolPlan,managedDll:o.coordinatorManagedDll,dotnet:o.coordinatorDotnet}):null;
 const prepared=preparedCoordinator?(await import('../global/extra-adapters.mjs')).getExtraAdapters(repoRoot,{selectedIds:['controller.coordinator-lifecycle']}).find(a=>a.id==='controller.coordinator-lifecycle'):null;
 const commonHid=directCommonHid?(await import('../global/controller-catalog/adapter.mjs')).getControllerCatalogAdapters(repoRoot).find(a=>a.id==='controller.unified-standard-input'):null;
  // A view of the existing indexed ID, not another registration or parent factory.
  const inputResume=directInputResume?{...extraRows.find(a=>a.id===INPUT_RESUME_ID),title:'Original input resume selection',kind:'node',path:'tools/input_power_gate_r122_selftest.mjs',args:[],timeoutMs:180000,scope:'behavioral-mock',evidenceInputs:['tools/input_power_gate_r122_selftest.mjs']}:null;
 const extra=[...directHostIds.map(id=>hostLifecycleCacheAdapter(id)),...(commonHid?[commonHid]:[]),...(inputResume?[inputResume]:[]),...(coordinator?[coordinator]:[]),...(prepared?[prepared]:[]),...(generalExtra.length&&!deferExtra?(await import('../global/extra-adapters.mjs')).getExtraAdapters(repoRoot,{selectedIds:generalExtra}):[]),...(directWire?(await import('../global/controller-lifecycle-pipe/adapter.mjs')).getControllerLifecyclePipeAdapters():[]),...(directPreHost?(await import('../global/controller-foreground-latency/adapter.mjs')).getControllerForegroundLatencyAdapters({win5ReturnExit2}).filter(a=>a.id===PREHOST_ID):[])];
 const byId=new Map([...core,...extra].map(a=>[a.id,a]));
 const selected=wanted.map(id=>byId.get(id)).filter(a=>a&&a.profiles.includes(o.profile));
 if(bugSelection){
  const present=new Set([...selected,...deferred].map(a=>a.id));
  const missing=wanted.filter(id=>!present.has(id));
  if(missing.length)throw Error('BUG_PLAN_REQUIRED_TEST_NOT_MATERIALIZED:'+missing.join(','));
 }
 const unique=deduplicateCases(selected);validateAdapters(unique.canonical,repoRoot);
 const configured=configureCandidateAdapters(unique.canonical,o,repoRoot).map(a=>configureUiPowerCache(a,repoRoot));validateAdapters(configured,repoRoot);
 const commonHidAdapter=directCommonHid?configured.find(a=>a.id==='controller.unified-standard-input'):null;
  const inputResumeAdapter=directInputResume?configured.find(a=>a.id===INPUT_RESUME_ID):null;
  return {adapters:configured,duplicateReferences:unique.deduplicated,deferred,selectedIds:[...new Set([...unique.canonical.map(a=>a.id),...deferred.map(a=>a.id)])],knownCount:known.length,requested:wanted.length,selectionPolicy,...(commonHidAdapter?{commonHidSelection:{...commonHidAdapter.originalProjection,entry:commonHidAdapter.path,arguments:commonHidAdapter.args}}:{}),...(inputResumeAdapter?{inputResumeSelection:{...inputResumeAdapter.originalProjection,entry:inputResumeAdapter.path,arguments:inputResumeAdapter.args}}:{}),...(directCoordinator?{coordinatorSelection:{canonicalTestId:'controller.coordinator-lifecycle',caseName:o.coordinatorCase,defaultCoordinatorDispatched:false,...coordinator.selectionDetails}}:prepared?{coordinatorSelection:{canonicalTestId:prepared.id,route:'prepared-current29',entry:prepared.path,arguments:prepared.args,blockedReason:prepared.blockedReason??null}}:{}),...(bugSelection?{bugSelection,bugPlan}:{})};
}
// Only these reviewed, pure source contracts have no process/compiler dependency.
// A changed test script needs review; product input changes remain automatic.
const PURE_NODE_REVIEWS={"steam.shared-mode-source": "fcf406055e38eb36ac80c58ffbec472b75e45685300e55797f868fd3dd2316cc", "steam.contact-source": "7a6a4077933780bd057522156a732dbf542bc67f7b7facc030e20df6b041083d", "performance.snapshot-bounds-source": "2ca7820d92435738ec5c01d3a0be8ec4d8d4a3183edb2511bf3e9368c0ee8d4a"};
function nodeReview(a){try{return PURE_NODE_REVIEWS[a.id]===sha256File(path.join(REPO,a.path));}catch{return false;}}
const implementation=['tools/ymcc_sim/optimization/cli.mjs','tools/ymcc_sim/optimization/execution-budget.mjs','tools/ymcc_sim/optimization/release-bugs.mjs','tools/ymcc_sim/optimization/selection-policy.mjs','tools/ymcc_sim/global/candidate-options.mjs','tools/ymcc_sim/global/input-resume-options.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/release-host-environment.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'];
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
function identityFor(a,previous,o,authority,runDir){
 let wire=null,wireError=null,preHost=null,preHostError=null,hostProtocol=null,hostProtocolError=null;
 if(isHostLifecycleCacheAdapter(a))try{hostProtocol=hostLifecycleDescriptorContract(a,REPO);}catch(error){hostProtocolError=error.message;}
 if(isPreHostCacheAdapter(a))try{preHost=preHostDescriptorContract(a,REPO);}catch(error){preHostError=error.message;}
 if(a.id===WIRE_ID)try{wire=wireDescriptorContract(a,path.join(runDir,'cases',a.id,'lifecycle-pipe'),REPO);}catch(error){wireError=error.message;}
 const typed=a.kind==='typescript';const pure=a.kind==='node'&&nodeReview(a);
 const uiPower=uiPowerCacheStillQualified(a,REPO);
 const tools=typed?(authority?.tools??[]):preHost?.tools??hostProtocol?.tools??[];
 const closure=(typed&&Boolean(authority))||pure||Boolean(wire)||Boolean(preHost)||Boolean(hostProtocol)||uiPower;
 return fingerprintCase({repoRoot:REPO,adapter:a,toolIdentities:tools,options:{closureComplete:closure,toolClosureComplete:closure,wireFixedContract:wire,wireContractError:wireError,preHostFixedContract:preHost?.contract??null,preHostContractError:preHostError,hostProtocolFixedContract:hostProtocol?.contract??null,hostProtocolContractError:hostProtocolError,
  resolvedInputs:[...implementation,...(typed?[...BUNDLED_AUTHORITY_SOURCES,'package.json','pnpm-lock.yaml','tsconfig.json','tsconfig.node.json']:[]),...(PURE_NODE_INPUTS[a.id]??[]),...(preHost?.sourceInputs??[]),...(hostProtocol?.sourceInputs??[]),...(previous?.dependencyPaths??[])],
  resolutionMembership:uiPower?uiPowerResolutionMembership(a,REPO):typed||preHost||hostProtocol?resolutionMembership([a.path,...a.evidenceInputs,...(previous?.dependencyPaths??[])]):[],
  profile:o.profile,payloadRoot:o.payloadRoot??null,environmentIdentity:preHost?.environmentIdentity??hostProtocol?.environmentIdentity??environmentIdentity(),execArgv:process.execArgv,
  closureReview:typed?'registered-reviewed-graph-plus-actual-bundle-inputs':pure?'pinned-pure-node-contract':wire?'fixed-direct-native-wire-tool-environment-and-path-contract':preHost?'fixed-prehost-original-native-input-output-and-parent-job-contract':hostProtocol?'fixed-managed-original-host-protocol-and-parent-return-contract':uiPower?'reviewed-ui-power-esbuild-graphs':'legacy-dynamic-closure-not-certified'}});
}
export function inspectSelectedCache(a,identity,cacheEntry,{pendingParentReturn=false}={}){
 const inspection=inspectCachedResult({identity,cacheEntry});
 if(inspection.reusable&&a.uiPowerCacheContract)try{
  verifyUiPowerBuildReceipt(a,inspection.reference,REPO);
  if(!pendingParentReturn)verifyUiPowerParentReturn(inspection.reference,cacheEntry);
 }
 catch(error){return {...inspection,hit:false,reusable:false,status:'CACHE_MISS',reference:null,reasons:[{code:'UI_POWER_CACHE_RECEIPT_REJECTED',detail:String(error.message)}]};}
 return inspection;
}
function evidencePins(result,a,{includeParent=true,hostProtocolEvidence}={}){
 const dir=result.evidenceDir;const files=[path.join(dir,'result.json'),path.join(dir,'stdout.log'),path.join(dir,'stderr.log'),...(a.resultFiles??[]).map(f=>path.join(dir,f))];
 for(const field of ['structuredEvidence','bundledOutputs','candidateArtifacts'])for(const r of result[field]??[])files.push(path.resolve(REPO,r.path));
 if(a.cacheOutputManifest&&fs.existsSync(path.resolve(dir,a.cacheOutputManifest))){
  const manifestFile=path.resolve(dir,a.cacheOutputManifest);
  if(!inside(dir,manifestFile))throw Error('CACHE_OUTPUT_MANIFEST_OUTSIDE_CASE');
  const manifest=read(manifestFile);
  if(!Array.isArray(manifest.sourceHashes)||manifest.sourceHashes.length>10000)throw Error('CACHE_OUTPUT_GRAPH_INVALID');
  for(const row of manifest.sourceHashes){const file=path.resolve(REPO,row.path);if(inside(dir,file)){if(sha256File(file)!==row.sha256)throw Error('CACHE_NESTED_OUTPUT_CHANGED:'+file);files.push(file);}}
 }
 if(a.cacheOutputManifest&&result.status==='PASS'&&!fs.existsSync(path.resolve(dir,a.cacheOutputManifest)))throw Error('CACHE_PASS_OUTPUT_MANIFEST_MISSING');
 files.push(...preHostEvidenceFiles(result,a,{includeParent}));
 files.push(...hostLifecycleEvidenceFiles(result,a,{includeParent,proof:hostProtocolEvidence,repoRoot:REPO}));
 if(result.bundle?.path)files.push(result.bundle.path);
 return [...new Set(files)].filter(f=>fs.existsSync(f)&&fs.statSync(f).isFile()).map(pin);
}
function cacheFile(a){const root=path.join(CORE,'State/ExecutionCache');fs.mkdirSync(root,{recursive:true});const suffix=a.id===PREHOST_ID&&a.args?.includes('--win5-return-exit2')?'.win5-return-exit2':'';return path.join(root,a.id+suffix+'.json');}
// Reuse the exact read-only identity/evidence consumers for explicit intake.
export {identityFor as fingerprintSelectedCase,evidencePins as collectCacheEvidencePins};
const cached=file=>{try{return read(file);}catch{return null;}};
function summarize(rows){return {selected:rows.length,executionAttemptsThisRun:rows.filter(r=>r.attemptedThisRun).length,executedThisRun:rows.filter(r=>r.executedThisRun).length,
 actualPassedThisRun:rows.filter(r=>r.actualPassedThisRun).length,cachedReferences:rows.filter(r=>r.status==='CACHED_REFERENCE').length,
 retainedFailureReferences:rows.filter(r=>r.status==='UNCHANGED_FAILURE_REFERENCE').length,notRun:rows.filter(r=>!r.executedThisRun).length};}
async function executePlan(args=process.argv.slice(2),context){
 const start=performance.now(),o=parseOptions(args);
 if(o.help){console.log('Run.ps1 -Optimized [-Mode plan|run|full] -Modules fan,controller [-Tier minimal|module] [-Rerun] [-FullAcceptance]. No module: plan lists minima; full requires explicit acceptance.');return 0;}
 const parent=path.resolve(o.outDir??path.join(CORE,'Evidence/Build/optimized'));
 if(!inside(CORE,parent))throw Error('OPTIMIZATION_EVIDENCE_MUST_STAY_IN_CORE');
 // Reject an existing reparse/symlink ancestor that redirects evidence outside core.
 let cursor=parent;while(!fs.existsSync(cursor))cursor=path.dirname(cursor);if(!inside(fs.realpathSync(CORE),fs.realpathSync(cursor)))throw Error('OPTIMIZATION_EVIDENCE_ESCAPE');
 const runDir=context?.runDir??makeRunDirectory(parent).runDir;const selectionStart=performance.now();
 if(o.coordinatorCase!==undefined&&context)o.coordinatorDeadlineEpochMs=context.deadlineEpochMs-context.cleanupReserveMs;
 const selection=await selectCases(o);
 if(o.mode==='plan'&&!o.bugPlan&&!o.modules.length&&!o.tests.length){write(path.join(runDir,'plan.json'),{schemaVersion:1,status:'PLAN_ONLY',modules:CONFIG.modules,selectionPolicy:selection.selectionPolicy,executedThisRun:0,fullAcceptance:false});console.log(JSON.stringify({status:'PLAN_ONLY',runDir,modules:Object.keys(CONFIG.modules),selectionPolicy:selection.selectionPolicy,wallMs:performance.now()-start}));return 0;}

 if(selection.bugSelection?.emptySelection){
  const result={schemaVersion:1,status:'NOT_RUN_NO_RELEASE_BUGS',bugSelection:selection.bugSelection,bugPlan:selection.bugPlan,selectionPolicy:selection.selectionPolicy,noTestsExecuted:true,summary:{selected:0,executedThisRun:0,actualPassedThisRun:0},fullAcceptance:false,fullProductAcceptance:false,wallMs:performance.now()-start};
  write(path.join(runDir,'plan.json'),{...result,tests:[]});write(path.join(runDir,'result.json'),result);console.log(JSON.stringify({status:result.status,runDir,executedThisRun:0}));return 0;
 }
 if(!selection.adapters.length&&!selection.deferred.length)throw Error('EMPTY_PROFILE_SELECTION');const selectionMs=performance.now()-selectionStart;
 const hasTyped=selection.adapters.some(a=>a.kind==='typescript');let authority=null,toolError=null;
 const identityStart=performance.now();if(hasTyped)try{authority=lockBundledNodeTools(REPO);}catch(e){toolError=String(e.message);}
 const plan=[];for(const a of selection.adapters){if(o.mode==='run'){plan.push({id:a.id,domain:a.domain,scope:a.scope,source:a.path,timeoutMs:a.timeoutMs,cacheable:null,reasons:[],decision:o.rerun?'RUN_EXPLICIT':'CHECK_REFERENCE_UNDER_LOCK',identity:null,identityDeferredToLockedExecution:true});continue;}const file=cacheFile(a),prior=cached(file);const identity=identityFor(a,prior,o,authority,runDir);const inspection=inspectSelectedCache(a,identity,prior);
  plan.push({id:a.id,domain:a.domain,scope:a.scope,source:a.path,timeoutMs:a.timeoutMs,cacheable:identity.cacheable,reasons:inspection.reasons,decision:o.mode==='full'||o.rerun?'RUN_EXPLICIT':inspection.reusable?inspection.status:'RUN_IF_REQUESTED',identity:identity.identity});}
 for(const a of selection.deferred)plan.push({id:a.id,domain:a.domain,scope:null,source:null,decision:o.mode==='full'?'RUN_EXPLICIT':'NOT_RUN_UNPROVEN_CLOSURE',registrationOnly:true,reasons:[o.mode==='full'?'Full delegates this registered ID to global; closure materialization and execution verdict remain unverified':'Legacy descriptor and tool preparation deferred; use explicit single-case Rerun or full acceptance'],executedThisRun:false});
 for(const a of selection.adapters){if(a.id===PREHOST_ID&&a.args.includes('--win5-return-exit2'))plan.find(row=>row.id===a.id).arguments=a.args;}
 const identityMs=performance.now()-identityStart;
 write(path.join(runDir,'plan.json'),{schemaVersion:1,mode:o.mode,tier:o.tier,modules:o.modules,moduleCoverage:o.modules.map(m=>({module:m,description:CONFIG.modules[m].description})),knownCount:selection.knownCount,requested:selection.requested,selectionPolicy:selection.selectionPolicy,deduplicated:selection.duplicateReferences,tests:plan,...(selection.commonHidSelection?{commonHidSelection:selection.commonHidSelection}:{}),...(selection.inputResumeSelection?{inputResumeSelection:selection.inputResumeSelection}:{}),...(selection.bugSelection?{bugSelection:selection.bugSelection,bugPlan:selection.bugPlan}:{}),...(selection.coordinatorSelection?{coordinatorSelection:selection.coordinatorSelection}:{}),toolError,selectionMs,identityMs,noTestsExecuted:true,fullProductAcceptance:false});
 if(o.mode==='plan'){console.log(JSON.stringify({status:'PLAN_ONLY',runDir,planned:plan.length,duplicateReferences:selection.duplicateReferences.length,selectionMs,identityMs,wallMs:performance.now()-start}));return 0;}
 if(o.mode==='full'){
  // Full acceptance deliberately retains the original lifecycle and coverage gates.
  const legacy=await import('../global/cli.mjs');const argv=['--mode','run','--profile','full','--require-all','--out-dir',path.join(runDir,'full-acceptance')];
  if(o.modules.length||o.tests.length)argv.push('--test',selection.selectedIds.join(','));
  for(const [key,flag]of Object.entries({'payloadRoot':'payload-root','runtimeRoot':'runtime-root','sharedRuntimeRoot':'shared-runtime-root','lightingPayloadRoot':'lighting-payload-root','expectedHelperSha256':'expected-helper-sha256','hcProfile':'hc-profile','hcVersion':'hc-version','legacyHostRoot':'legacy-host-root','fanToolLoadPlan':'fan-tool-load-plan','fanToolLoadPlanSha256':'fan-tool-load-plan-sha256','powershell':'powershell','completeHostContract':'complete-host-contract'}))if(o[key])argv.push('--'+flag,o[key]);
  const exitCode=await legacy.main(argv);write(path.join(runDir,'result.json'),{schemaVersion:1,status:'EXPLICIT_FULL_DELEGATED',exitCode,cacheUsed:false,originalLifecycleAndCoverageGates:true,wallMs:performance.now()-start,fullProductAcceptance:false});return exitCode;
 }
 const rows=selection.deferred.map(a=>({id:a.id,status:'NOT_RUN_UNPROVEN_CLOSURE',registrationOnly:true,executedThisRun:false,actualPassedThisRun:false}));let failedBug=null,lockedIdentityMs=0;for(const a of selection.adapters){
  if(failedBug){rows.push({id:a.id,status:'NOT_RUN_AFTER_FAILURE',reason:'REQUIRED_RELEASE_BUG_FAILED',blockedBy:failedBug.id,blockingStatus:failedBug.status,attemptedThisRun:false,executedThisRun:false,actualPassedThisRun:false,durationMs:0});continue;}
  if(context&&context.remainingMs()<=0){rows.push({id:a.id,status:'NOT_RUN_BUDGET',executedThisRun:false,actualPassedThisRun:false});continue;}
  const file=cacheFile(a);let lock;
  try{lock=fs.openSync(file+'.lock','wx');}
  catch(e){rows.push({id:a.id,status:'NOT_RUN_BUSY',reason:e.code,executedThisRun:false,actualPassedThisRun:false});continue;}
  try{
   try{fs.writeFileSync(lock,JSON.stringify({pid:process.pid,at:new Date().toISOString(),id:a.id}));}
   catch(e){rows.push({id:a.id,status:'NOT_RUN_LOCK_RECORDING_FAILED',reason:e.code??e.name,lockRecordingError:String(e.message??e).slice(0,1600),attemptedThisRun:false,executedThisRun:false,actualPassedThisRun:false});continue;}
   const lockedIdentityStart=performance.now();
   const prior=cached(file);const identity=identityFor(a,prior,o,authority,runDir);const inspection=inspectSelectedCache(a,identity,prior);
   lockedIdentityMs+=performance.now()-lockedIdentityStart;
   if(inspection.reusable&&!o.rerun){rows.push({id:a.id,status:inspection.status,previousStatus:inspection.previousStatus,reference:prior.result,executedThisRun:false,actualPassedThisRun:false,durationMs:0});if(o.bugPlan&&inspection.status==='UNCHANGED_FAILURE_REFERENCE')failedBug={id:a.id,status:inspection.status};continue;}
   if(o.referenceOnly){rows.push({id:a.id,status:'NOT_RUN_NO_VALID_REFERENCE',reason:inspection.reasons,executedThisRun:false,actualPassedThisRun:false,durationMs:0});continue;}
   if(!identity.cacheable&&!o.rerun){rows.push({id:a.id,status:'NOT_RUN_UNPROVEN_CLOSURE',reason:identity.reasons,executedThisRun:false,actualPassedThisRun:false});continue;}
   let hostProtocolEvidence;
   if(isHostLifecycleCacheAdapter(a)){
    const before=path.join(runDir,a.id===HOST_PARENT_ID?'host-parent-lookups-before.json':'host-protocol-lookups-before.json'),caseDir=path.join(runDir,'cases',a.id);
    write(before,{schemaVersion:1,stage:'before',atUtc:new Date().toISOString(),snapshot:captureHostProtocolLookupGuards(REPO,caseDir,{negativeGroup:a.id===HOST_PARENT_ID?'negative-cancellation-omitted':'negative-dispose-retention-omitted'})});
    hostProtocolEvidence={kind:'optimized',result:path.join(runDir,'result.json'),guards:[runDir],lookupGuards:{before:pin(before)}};
   }
   const t=performance.now();const result=await executeAdapter(a,{repoRoot:REPO,runDir,powershell:o.powershell??'pwsh.exe',payloadRoot:o.payloadRoot,remainingMs:context?.remainingMs});
   if(hostProtocolEvidence){
    const after=path.join(runDir,a.id===HOST_PARENT_ID?'host-parent-lookups-after.json':'host-protocol-lookups-after.json');
    write(after,{schemaVersion:1,stage:'after',atUtc:new Date().toISOString(),snapshot:captureHostProtocolLookupGuards(REPO,path.join(runDir,'cases',a.id),{negativeGroup:a.id===HOST_PARENT_ID?'negative-cancellation-omitted':'negative-dispose-retention-omitted'})});
    hostProtocolEvidence.lookupGuards.after=pin(after);
   }
   const row={id:a.id,status:result.status,attemptedThisRun:true,executedThisRun:Number.isInteger(result.pid),actualPassedThisRun:result.status==='PASS'&&Number.isInteger(result.pid),durationMs:performance.now()-t,reference:pin(path.join(result.evidenceDir,'result.json'))};rows.push(row);
   const dependencyPaths=[...new Set([...(result.dependencyInputs??[]).map(r=>r.path),...preHostDependencyPaths(result,a)])];
   const after=identityFor(a,{dependencyPaths},o,authority,runDir);
   const currentPins=new Map([...after.manifest.inputs,...after.manifest.tools].map(r=>[r.role+'|'+r.path,r.sha256]));
   const identityDrift=[...identity.manifest.inputs,...identity.manifest.tools].some(r=>currentPins.get(r.role+'|'+r.path)!==r.sha256)||(isPreHostCacheAdapter(a)&&identity.manifest.options.preHostFixedContract?.inputFingerprint!==after.manifest.options.preHostFixedContract?.inputFingerprint)||(isHostLifecycleCacheAdapter(a)&&JSON.stringify(identity.manifest.options.hostProtocolFixedContract)!==JSON.stringify(after.manifest.options.hostProtocolFixedContract))||(a.uiPowerCacheContract&&identity.identity!==after.identity);
   if(identityDrift){row.status='INCOMPLETE_IDENTITY_DRIFT';row.actualPassedThisRun=false;row.identityChangedDuringExecution=true;}
   const cacheCandidate=after.cacheable&&!identityDrift;
   const needsEvidenceValidation=a.cacheOutputManifest||a.uiPowerCacheContract||isPreHostCacheAdapter(a)||isHostLifecycleCacheAdapter(a);
   // Keep dedicated output/proof validation even when a cache entry cannot be stored.
   const evidence=cacheCandidate||needsEvidenceValidation?evidencePins(result,a,{includeParent:!isPreHostCacheAdapter(a)&&!isHostLifecycleCacheAdapter(a),hostProtocolEvidence}):null;
   if(cacheCandidate){
    const entry={schemaVersion:1,identity:after.identity,result:row.reference,evidencePins:evidence,dependencyPaths,...(hostProtocolEvidence?{hostProtocolEvidence}:{}),recordedAt:new Date().toISOString()};
    if(isPreHostCacheAdapter(a)||isHostLifecycleCacheAdapter(a)){if(result.status==='PASS'){write(path.join(runDir,isHostLifecycleCacheAdapter(a)?(a.id===HOST_PARENT_ID?'host-parent-cache-pending.json':'host-protocol-cache-pending.json'):'prehost-cache-pending.json'),{schemaVersion:1,adapter:a,entry});row.cacheWrite='PENDING_PARENT_RETURN';}else row.cacheWrite='NOT_STORED_SUCCESS_GRAPH_UNPROVEN';}else if(a.uiPowerCacheContract){
      const proof=inspectSelectedCache(a,after,entry,{pendingParentReturn:true});
      if(proof.status==='CACHED_REFERENCE'){write(path.join(runDir,a.id+'-cache-pending.json'),{adapter:a,entry});row.cacheWrite='PENDING_PARENT_RETURN';}
      else{row.cacheWrite='NOT_STORED_UI_POWER_GRAPH_UNPROVEN';row.cacheQualification=proof.reasons;}
     }else{const temp=file+'.tmp-'+process.pid;write(temp,entry);fs.renameSync(temp,file);}}
   row.cacheable=after.cacheable;row.cacheReasons=after.reasons;
   if(o.bugPlan&&['FAIL','TIMEOUT','INCOMPLETE_IDENTITY_DRIFT'].includes(row.status))failedBug={id:a.id,status:row.status};
  }finally{fs.closeSync(lock);fs.unlinkSync(file+'.lock');}
 }
 const summary=summarize(rows);const failure=rows.some(r=>['FAIL','TIMEOUT','UNCHANGED_FAILURE_REFERENCE'].includes(r.status));
 const blocked=rows.some(r=>!['PASS','CACHED_REFERENCE'].includes(r.status));
 const result={schemaVersion:1,status:failure?'FAIL':blocked?'INCOMPLETE':summary.cachedReferences?'COMPLETE_WITH_CACHED_REFERENCES':'PASS_SELECTED_SCOPE',runDir,tier:o.tier,modules:o.modules,summary,rows,
  duplicateReferences:selection.duplicateReferences,selectionPolicy:selection.selectionPolicy,...(selection.commonHidSelection?{commonHidSelection:selection.commonHidSelection}:{}),...(selection.inputResumeSelection?{inputResumeSelection:selection.inputResumeSelection}:{}),...(selection.coordinatorSelection?{coordinatorSelection:selection.coordinatorSelection}:{}),...(selection.bugSelection?{bugSelection:selection.bugSelection,bugPlan:selection.bugPlan}:{}),selectionMs,identityMs,lockedIdentityMs,executionMs:rows.reduce((n,r)=>n+(r.durationMs??0),0),wallMs:performance.now()-start,
  sourceCoverage:'Selected registered adapters only; full lifecycle/hardware acceptance is separate',fullAcceptance:false,fullProductAcceptance:false};
 write(path.join(runDir,'result.json'),result);console.log(JSON.stringify({status:result.status,runDir,...summary,selectionMs,identityMs,lockedIdentityMs,wallMs:result.wallMs}));return failure?2:blocked?10:0;
}
function finalizeCompletedParentCache(runDir,o,budget,kind='prehost'){
 const host=kind==='host-protocol'||kind==='host-parent',prefix=host?kind:'prehost';
 const pendingFile=path.join(runDir,prefix+'-cache-pending.json');
 if(!fs.existsSync(pendingFile))return;
 const receipt=path.join(runDir,prefix+'-cache-finalization.json'),started=performance.now();let lock=null,file=null;
 const deadlineEpochMs=read(path.join(runDir,'budget-context.json')).deadlineEpochMs;
 const checkDeadline=()=>{if(!Number.isSafeInteger(deadlineEpochMs)||Date.now()>=deadlineEpochMs)throw Error('PREHOST_CACHE_FINALIZATION_DEADLINE');};
 try{
  checkDeadline();
  if(!budget.resourceZeroVerified||budget.exitCode!==0||budget.timedOut)throw Error('PREHOST_PARENT_NOT_CLOSED_SUCCESSFULLY');
  const pending=read(pendingFile),a=pending.adapter;validateAdapters([a],REPO);
  if(!(host?(isHostLifecycleCacheAdapter(a)&&a.id===(kind==='host-parent'?HOST_PARENT_ID:HOST_PROTOCOL_ID)):isPreHostCacheAdapter(a)))throw Error('COMPLETED_PARENT_PENDING_DESCRIPTOR_INVALID');
  file=cacheFile(a);lock=fs.openSync(file+'.lock','wx');
  fs.writeFileSync(lock,JSON.stringify({pid:process.pid,id:a.id,phase:'completed-parent-cache-admission'}));
  const identity=identityFor(a,{dependencyPaths:pending.entry.dependencyPaths},o,null,runDir);
  if(!identity.cacheable||identity.identity!==pending.entry.identity)throw Error('PREHOST_COMPLETED_INPUT_IDENTITY_CHANGED');
  checkDeadline();
  const raw=read(pending.entry.result.path),entry={...pending.entry,evidencePins:evidencePins(raw,a,{hostProtocolEvidence:pending.entry.hostProtocolEvidence}),recordedAt:new Date().toISOString()};
  checkDeadline();
  const inspection=inspectCachedResult({identity,cacheEntry:entry});
  if(inspection.status!=='CACHED_REFERENCE')throw Error('PREHOST_COMPLETED_CACHE_REJECTED:'+JSON.stringify(inspection.reasons));
  checkDeadline();
  const temp=file+'.tmp-'+process.pid;write(temp,entry);fs.renameSync(temp,file);
  write(receipt,{status:'CACHE_STORED_AFTER_PARENT_RETURN',index:pin(file),result:entry.result,executedThisRun:0,actualPassedThisRun:0,elapsedMs:performance.now()-started,resourceObservation:host?inspection.hostLifecycleReference?.parentReturn??null:inspection.preHostReference?.outerCleanup??null});
 }catch(error){
  write(receipt,{status:'CACHE_NOT_STORED',error:error.code??error.message,executedThisRun:0,actualPassedThisRun:0,elapsedMs:performance.now()-started});
 }finally{if(lock!==null){fs.closeSync(lock);fs.unlinkSync(file+'.lock');}}
}
function finalizeUiPowerCaches(o,budget){
 const runDir=budget.runDir,names=['lifecycle.ui-power-transport','lifecycle.ui-power-recovery'];
 for(const id of names){
  const pending=path.join(runDir,id+'-cache-pending.json');
  if(!fs.existsSync(pending))continue;
  const receipt=path.join(runDir,id+'-cache-finalization.json');let lock=null,file=null,outcome;
  try{
   if(!budget.resourceZeroVerified||budget.sourceIdentity.status!=='MATCH'||budget.exitCode!==0||budget.timedOut||budget.ownedJob?.terminationRequested!==false)throw Error('UI_POWER_PARENT_NOT_CLOSED_NATURALLY');
   const deadline=read(path.join(runDir,'budget-context.json')).deadlineEpochMs;
   if(!Number.isSafeInteger(deadline)||Date.now()>=deadline)throw Error('UI_POWER_CACHE_DEADLINE');
   const {adapter,entry}=read(pending);
   if(adapter.id!==id||!adapter.uiPowerCacheContract||path.resolve(entry.result.path)!==path.join(runDir,'cases',id,'result.json'))throw Error('UI_POWER_PENDING_DESCRIPTOR_INVALID');
   file=cacheFile(adapter);lock=fs.openSync(file+'.lock','wx');fs.writeFileSync(lock,JSON.stringify({pid:process.pid,id,phase:'completed-parent-cache-admission'}));
   const fresh=identityFor(adapter,{dependencyPaths:entry.dependencyPaths},o,null,runDir);
   entry.evidencePins.push(...['result.json','execution-budget.json','owned-job.json'].map(name=>pin(path.join(runDir,name))));
   const proof=inspectSelectedCache(adapter,fresh,entry);
   if(proof.status!=='CACHED_REFERENCE')throw Error('UI_POWER_CACHE_FINAL_ADMISSION:'+JSON.stringify(proof.reasons));
   if(Date.now()>=deadline)throw Error('UI_POWER_CACHE_DEADLINE');
   const temp=file+'.tmp-'+process.pid;write(temp,entry);fs.renameSync(temp,file);
   outcome={status:'CACHE_STORED_AFTER_PARENT_RETURN',index:pin(file),result:entry.result};
  }catch(error){outcome={status:'CACHE_NOT_STORED',error:String(error?.message??error)};}
  finally{if(lock!==null){fs.closeSync(lock);fs.unlinkSync(file+'.lock');}}
  write(receipt,{...outcome,executedThisRun:0,actualPassedThisRun:0});
 }
}
export async function main(args=process.argv.slice(2)){
 const o=parseOptions(args);if(o.help)return executePlan(args);
 const invocation=await runWithWallBudget(args,{repoRoot:REPO,coreRoot:CORE,workerEntry:fileURLToPath(import.meta.url),budgetMs:o.budgetMs,powershell:o.powershell??'pwsh.exe',outDir:o.outDir,additionalInputs:[CONFIG_FILE,fileURLToPath(new URL('./selection-policy.mjs',import.meta.url)),...(o.bugPlan?[path.resolve(o.bugPlan)]:[])]});
 finalizeCompletedParentCache(invocation.result.runDir,o,invocation.result);
 finalizeCompletedParentCache(invocation.result.runDir,o,invocation.result,'host-protocol');
 finalizeCompletedParentCache(invocation.result.runDir,o,invocation.result,'host-parent');
 finalizeUiPowerCaches(o,invocation.result);
 return invocation.exitCode;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2);
 const run=args[0]==='--budget-worker'?Promise.resolve().then(()=>{if(args.length!==3)throw Error('INVALID_BUDGET_WORKER_ARGUMENTS');const context=readBudgetWorker(args[1],args[2],{coreRoot:CORE,repoRoot:REPO});return executePlan(context.args,context);}):main(args);
 run.then(code=>process.exitCode=code).catch(error=>{console.error('YMCC_OPTIMIZATION_ERROR '+error.message);process.exitCode=3;});
}
