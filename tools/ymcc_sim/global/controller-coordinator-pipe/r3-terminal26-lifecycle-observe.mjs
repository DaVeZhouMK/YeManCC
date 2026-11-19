// Explicit current26 lifecycle consumer; reuses the public pipe observer's unchanged helpers.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';import {fileURLToPath,pathToFileURL} from 'node:url';
import {terminal22LifecyclePreparationPins,validateTerminal22LifecycleLayout,verifyTerminal22LifecycleCapture,terminal22LifecycleManagedDllPin,validateTerminalLifecycleNativeReceipt,terminalPreparedRuntimeDirectories,terminal26CompiledSwitchSource,terminal26CompiledTupleSource,terminal22ExecutionFromProcess} from './r3-terminal22-qualified-observe.mjs';
import {currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode} from './r3-current-run.mjs';
import {terminal26SwitchFunction} from './r3-terminal26-switch-contract.mjs';
import {runProcess as trustedLifecycleRunProcess} from '../runner.mjs';
import {validateManagedCompileRows as validateTrustedLifecycleManagedRows,requireTerminalManagedContract,terminalManagedStageStatus,terminalManagedContractFromDeclaration,verifyManagedDecoderCompileEvidence} from './r3-managed-input-contract.mjs';
import {isolateCurrentFixtureSettingsMutex} from '../../../fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs';
import {terminal26CurrentCaptureTupleSource} from './r3-terminal26-lifecycle-consumer-successor.mjs';
const lifecycleSame=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const lifecycleInside=(root,file)=>{const relative=path.relative(root,file);return relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
function lifecyclePlainPath(file){
 assert(typeof file==='string'&&path.isAbsolute(file)&&!/[\0\r\n]/.test(file),'absolute lifecycle path required');
 for(let cursor=path.resolve(file);;cursor=path.dirname(cursor)){
  let st;try{st=fs.lstatSync(cursor);}catch(error){if(error.code!=='ENOENT')throw error;}
  if(st){assert(!st.isSymbolicLink(),'lifecycle reparse path rejected');assert(lifecycleSame(fs.realpathSync(cursor),cursor),'lifecycle path redirection rejected');}
  if(path.dirname(cursor)===cursor)break;
 }
}


const lifecycleNegativeNeed=(ok,code)=>{if(!ok)throw Error('NEGATIVE_TRACE_'+code);};
function lifecycleNegativePaths(nodes,grammar){let variants=[[]];for(const node of nodes){let choices;if(Number.isInteger(node)){lifecycleNegativeNeed(typeof grammar.labels[node]==='string','KNOWN_LABEL_REQUIRED');choices=[[grammar.labels[node]]];}else{lifecycleNegativeNeed(node&&Object.keys(node).sort().join(',')==='else,fact,then'&&node.fact==='cleanupAwaiting'&&Array.isArray(node.then)&&Array.isArray(node.else),'UNKNOWN_SOURCE_GRAMMAR_NODE');choices=[...lifecycleNegativePaths(node.then,grammar),...lifecycleNegativePaths(node.else,grammar)];}variants=variants.flatMap(prefix=>choices.map(suffix=>[...prefix,...suffix]));lifecycleNegativeNeed(variants.length<=8,'SOURCE_GRAMMAR_ALTERNATIVE_LIMIT');}return variants;}
export function verifyTerminal26LifecycleNegativeShape(raw,grammar,switchSource){lifecycleNegativeNeed(grammar?.schemaVersion===1&&grammar.kind==='EXPLICIT26_SOURCE_DERIVED_SWITCH_ASSERTION_GRAMMAR_V1'&&grammar.managedInputContract==='current105-terminal26-sdk-lifetime-compact-v1'&&grammar.cases?.length===98,'EXACT_SOURCE_GRAMMAR_REQUIRED');lifecycleNegativeNeed(switchSource&&typeof switchSource.path==='string'&&path.isAbsolute(switchSource.path)&&path.basename(switchSource.path)==='fixture.cpp'&&Buffer.isBuffer(switchSource.bytes)&&createHash('sha256').update(switchSource.bytes).digest('hex')===switchSource.sha256,'EXACT_COMPILED_SOURCE_PIN_REQUIRED');const switchBody=terminal26SwitchFunction(switchSource.bytes),compiledSwitchSha256=createHash('sha256').update(switchBody).digest('hex');lifecycleNegativeNeed(switchBody.length===grammar.sourceBindings?.switchFunctionBytes&&compiledSwitchSha256===grammar.sourceBindings?.exactSwitchSourceSha256,'COMPILED_SOURCE_GRAMMAR_REQUIRED');lifecycleNegativeNeed(Array.isArray(raw?.cases)&&raw.cases.length===98,'EXACT98_CASES_REQUIRED');lifecycleNegativeNeed(Array.isArray(raw.assertionTrace)&&Number.isSafeInteger(raw.checks)&&raw.checks===raw.assertionTrace.length,'ACTUAL_TRACE_REQUIRED');const occurrences=new Map(),casePaths=[],byOrdinal=Array.from({length:98},()=>[]);raw.cases.forEach((c,i)=>{const expected=grammar.cases[i],occurrence=(occurrences.get(expected.name)??0)+1;occurrences.set(expected.name,occurrence);lifecycleNegativeNeed(expected.sourceOrdinal===i+1&&c?.sourceOrdinal===i+1&&c.name===expected.name&&c.nameOccurrence===occurrence,'EXACT_CASE_IDENTITY_REQUIRED');});raw.assertionTrace.forEach((row,i)=>{lifecycleNegativeNeed(row&&row.checkIndex===i+1&&Number.isSafeInteger(row.sourceOrdinal)&&row.sourceOrdinal>=1&&row.sourceOrdinal<=98&&typeof row.assertion==='string'&&typeof row.passed==='boolean','TRACE_ROW_REQUIRED');const c=raw.cases[row.sourceOrdinal-1];lifecycleNegativeNeed(row.case===c.name&&row.nameOccurrence===c.nameOccurrence,'TRACE_OWNER_REQUIRED');byOrdinal[row.sourceOrdinal-1].push(row);if(i)lifecycleNegativeNeed(row.sourceOrdinal>=raw.assertionTrace[i-1].sourceOrdinal,'SOURCE_CASE_ORDER_REQUIRED');});let acceptedChecks=0;for(let i=0;i<98;i++){const alternatives=lifecycleNegativePaths(grammar.cases[i].grammar,grammar),actual=byOrdinal[i].map(row=>row.assertion),matches=alternatives.filter(labels=>labels.length===actual.length&&labels.every((label,j)=>label===actual[j]));lifecycleNegativeNeed(matches.length>0,'EXACT_SOURCE_ASSERTION_LANGUAGE_REQUIRED:'+String(i+1));acceptedChecks+=actual.length;casePaths.push({sourceOrdinal:i+1,checks:actual.length,matchingSourcePaths:matches.length,branchTruthIndependentlyProven:false});}lifecycleNegativeNeed(acceptedChecks===raw.checks&&raw.checks>=grammar.checkBounds.min&&raw.checks<=grammar.checkBounds.max,'EXACT_SOURCE_TRACE_BOUNDS_REQUIRED');return{scope:'SOURCE_DERIVED_CASE_AND_ASSERTION_LANGUAGE_ONLY',compiledSwitchSha256,cases:98,checks:raw.checks,casePaths,completeFailureMaskVerified:false,sourceBranchTruthIndependentlyProven:false,runtimeAuthorityGranted:false};}

export function selectTerminal26LifecycleModes(selection){
 const plan=currentLifecycleModePlan();if(selection===undefined)return plan;
 assert(typeof selection==='string'&&selection.length>0,'nonempty exact lifecycle mode selection required');
 const requested=selection.split(','),known=new Set(plan.map(currentLifecycleModeLabel));
 assert(requested.every(label=>known.has(label)),'unknown lifecycle mode selection');
 assert(new Set(requested).size===requested.length,'duplicate lifecycle mode selection');
 const selected=new Set(requested);return plan.filter(test=>selected.has(currentLifecycleModeLabel(test)));
}

export function parseTerminal22ObservationArguments(args){
 assert(Array.isArray(args),'strict observer options required');const options={};
 for(let i=0;i<args.length;i++){
  const key=args[i];assert(['--native-preparation','--lifecycle-preparations','--out','--native-budget-ms','--consumer-predecessors','--lifecycle-budget-ms','--lifecycle-modes'].includes(key)&&!Object.hasOwn(options,key),'strict observer options required');
  assert(typeof args[i+1]==='string'&&args[i+1]&&!args[i+1].startsWith('--'),'option value required');options[key]=args[++i];
 }
 assert(options['--out']&&!!options['--native-preparation']!==!!options['--lifecycle-preparations'],'exactly one explicit native or lifecycle preparation input required');
 if(options['--lifecycle-preparations'])assert(options['--native-budget-ms']===undefined,'native watchdog option is not a lifecycle mode budget');
 if(options['--lifecycle-budget-ms']!==undefined){
  assert(options['--lifecycle-preparations']&&!options['--native-preparation'],'lifecycle group budget requires lifecycle preparations');
  assert(/^[1-9][0-9]{4,6}$/.test(options['--lifecycle-budget-ms'])&&Number.isSafeInteger(Number(options['--lifecycle-budget-ms']))&&Number(options['--lifecycle-budget-ms'])>=90000&&Number(options['--lifecycle-budget-ms'])<=1500000,'bounded integer lifecycle group budget required');
 }
 if(options['--lifecycle-modes']!==undefined){assert(options['--lifecycle-preparations']&&!options['--native-preparation'],'lifecycle mode selection requires lifecycle preparations');selectTerminal26LifecycleModes(options['--lifecycle-modes']);}
 if(options['--native-budget-ms']!==undefined)assert(/^[1-9][0-9]{4,5}$/.test(options['--native-budget-ms'])&&Number(options['--native-budget-ms'])>=60000&&Number(options['--native-budget-ms'])<=180000,'bounded integer native fixture watchdog required');
 // TERMINAL26_CONSUMER_SUCCESSOR_BEGIN parser
 if(options['--consumer-predecessors']!==undefined){
  if(options['--native-preparation']){
  assert(options['--native-preparation']&&!options['--lifecycle-preparations'],'consumer predecessor proof requires one native preparation');
  assert(path.isAbsolute(options['--native-preparation'])&&!/[\0\r\n]/.test(options['--native-preparation']),'absolute native preparation required for consumer predecessor proof');
  }else assert(options['--lifecycle-preparations']&&path.isAbsolute(options['--lifecycle-preparations'])&&!/[\0\r\n]/.test(options['--lifecycle-preparations']),'absolute lifecycle preparation required for lifecycle predecessor proof');
  assert(path.isAbsolute(options['--consumer-predecessors'])&&!/[\0\r\n]/.test(options['--consumer-predecessors']),'absolute consumer predecessor declaration required');
 }
 // TERMINAL26_CONSUMER_SUCCESSOR_END parser
 if(options['--lifecycle-preparations'])for(const [key,value] of Object.entries(options))if(key!=='--lifecycle-budget-ms'&&key!=='--lifecycle-modes')assert(path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'absolute observer input paths required');
 return options;
}

function terminal22LifecycleGroupBudget(groupBudgetMs){
 assert(Number.isSafeInteger(groupBudgetMs)&&groupBudgetMs>=90000&&groupBudgetMs<=1500000,'bounded integer lifecycle group budget required');return groupBudgetMs;
}
const terminal22LifecycleBudgetScope=groupBudgetMs=>groupBudgetMs===90000?'ORIGINAL_GROUP90_ALL_PHASES':'EXPLICIT_LIFECYCLE_GROUP_BUDGET_ALL_PHASES';
export function terminal22LifecycleRemainingBudget(started,groupBudgetMs=90000,now=Date.now()){
 terminal22LifecycleGroupBudget(groupBudgetMs);
 if(groupBudgetMs===90000)return currentLifecycleRemainingBudget(started,now);
 const groupWallMs=now-started;assert(Number.isFinite(groupWallMs)&&groupWallMs>=0&&groupWallMs<groupBudgetMs,'effective lifecycle group budget exceeded');
 return {originalModeBudgetMs:45000,originalGroupBudgetMs:90000,effectiveGroupBudgetMs:groupBudgetMs,groupWallMs,remainingGroupMs:groupBudgetMs-groupWallMs,timeoutMs:Math.min(45000,groupBudgetMs-groupWallMs)};
}
export function finalizeTerminal22LifecycleExecution({started,ended=Date.now(),complete,failed=false,modeResults=[],groupBudgetMs=90000,modeSelection}){
 terminal22LifecycleGroupBudget(groupBudgetMs);
 const groupWallMs=ended-started;assert(Number.isFinite(groupWallMs)&&groupWallMs>=0,'actual final lifecycle clock required');
 const deadlineExceeded=groupWallMs>groupBudgetMs,directed=modeSelection!==undefined,plan=selectTerminal26LifecycleModes(modeSelection),planComplete=modeResults.length===plan.length&&modeResults.every((row,index)=>row.label===currentLifecycleModeLabel(plan[index])&&row.semanticAccepted===true);
 const accepted=complete===true&&!failed&&!deadlineExceeded&&planComplete,terminals=modeResults.map(row=>row.nativeProcess),facts=terminals.length>0&&terminals.every(row=>row&&typeof row.timedOut==='boolean'&&(row.signal===null||typeof row.signal==='string'));
 return {status:accepted&&directed?'BOUNDED_SELECTED_LIFECYCLE_DIAGNOSTIC_MATCHED_ORIGINAL_PARENT_NOT_ACCEPTED':accepted?(groupBudgetMs===90000?'BOUNDED_ORIGINAL16_LIFECYCLE_MATCHED_PUBLIC_RUNTIME_PENDING':'BOUNDED_ORIGINAL16_LIFECYCLE_MATCHED_EXPLICIT_GROUP_BUDGET_PUBLIC_RUNTIME_PENDING'):'INCOMPLETE',deadlineExceeded,...(directed?{diagnosticOnly:true,selectedModeLabels:plan.map(currentLifecycleModeLabel),originalPlanComplete:false}:{}),finishedAt:new Date(ended).toISOString(),
  originalGroupBudgetMs:90000,effectiveGroupBudgetMs:groupBudgetMs,lifecycleBudgetScope:terminal22LifecycleBudgetScope(groupBudgetMs),withinOriginalGroupBudget:groupWallMs<=90000,
  execution:{groupWallMs,exitCode:accepted?0:1,signal:facts?(terminals.find(row=>row.signal!==null)?.signal??null):undefined,outerTimedOut:deadlineExceeded?true:facts?terminals.some(row=>row.timedOut):undefined,supervisionScope:'actual-lifecycle-observer-control-flow-and-native-mode-deadlines',observerCliExitObserved:false},qualificationAccepted:false};
}



export function measureTerminal26LifecycleSourceCheck(check,phase,timings){
 const started=Date.now();let completed=false;
 try{const proof=check();completed=true;return proof;}
 finally{timings.push({phase,status:completed?'COMPLETED':'FAILED',wallMs:Date.now()-started});}
}

async function observeTerminal22Lifecycle(inputFile,out,predecessorDeclarationFile,groupBudgetMs=90000,modeSelection){
 const groupStarted=Date.now();terminal22LifecycleGroupBudget(groupBudgetMs);
 const runPlan=selectTerminal26LifecycleModes(modeSelection),directed=modeSelection!==undefined;
 const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const pin=file=>({path:path.resolve(file),sha256:hash(file),bytes:fs.statSync(file).size});
 const inputs=new Map(),sourceCheckTimings=[];
 function add(row,scope){
  assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'exact actual lifecycle input pin required');
  const file=path.resolve(row.path);if(scope)assert(lifecycleInside(scope,file),'lifecycle input escaped original authority');
  const key=file.toLowerCase(),previous=inputs.get(key);assert(!previous||previous.sha256===row.sha256,'conflicting lifecycle input pin');if(previous)return file;
  lifecyclePlainPath(file);assert(fs.lstatSync(file).isFile(),'regular lifecycle input required');assert.equal(hash(file),row.sha256,'actual lifecycle input changed: '+file);inputs.set(key,{path:file,sha256:row.sha256});return file;
 }
 const negativeReportGrammarFile=fileURLToPath(new URL('./r3-terminal26-switch-expectations.json',import.meta.url));
 if(runPlan.some(test=>test.mutation?.startsWith('rollback-switch-'))){add(pin(negativeReportGrammarFile));add(pin(fileURLToPath(new URL('./r3-terminal26-switch-contract.mjs',import.meta.url))));}
 lifecyclePlainPath(inputFile);const inputPin=pin(inputFile);add(inputPin);const declaration=read(inputFile),declaredContract=terminalManagedContractFromDeclaration(declaration.kind),selected=terminal22LifecyclePreparationPins(declaration),plan=currentLifecycleModePlan(),prepared=new Map();
 const postCompileConsumers=predecessorDeclarationFile===undefined?null:(await import('./r3-terminal26-lifecycle-consumer-successor.mjs')).terminal26LifecycleConsumerSuccessor(inputFile,path.resolve(predecessorDeclarationFile));
 if(postCompileConsumers){assert(declaredContract.signalEvidence===true&&declaredContract.count===26,'explicit26 lifecycle successor required');for(const row of postCompileConsumers.sourceInputs)add(row);}
 let common,shared,sourceCheck;
 for(const selectedPin of selected){
  const nativeFile=add(selectedPin),n=read(nativeFile);assert(n.schemaVersion===1&&n.status===terminalManagedStageStatus('native',declaredContract.id)&&n.nativeExecuted===false&&n.publicRuntimeAccepted===false,'actual compiled lifecycle preparation required');
  const rolePlan=plan.find(row=>row.role===selectedPin.role);assert(n.role===rolePlan.role&&n.mutation===rolePlan.mutation,'exact original lifecycle native polarity required');
  const preparationFile=add(n.preparation),preparation=read(preparationFile);assert(preparation.status===terminalManagedStageStatus('managed',declaredContract.id)&&preparation.publicRuntimeAccepted===false,'actual managed22 preparation required');
  const layoutFile=add(preparation.layout),layout=read(layoutFile);assert(layout.schemaVersion===1&&layout.sourceAuthority?.mode==='EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'&&layout.managedInputContract===declaredContract.id,'explicit original terminal22 compile layout required');
     if(n.managedInputContract!==undefined)assert.equal(n.managedInputContract,declaredContract.id);if(preparation.managedInputContract!==undefined)assert.equal(preparation.managedInputContract,declaredContract.id);
   if(declaredContract.steamDeckPadDecoder)assert(n.managedInputContract===declaredContract.id&&preparation.managedInputContract===declaredContract.id,'explicit decoder lifecycle preparation required');
validateTerminal22LifecycleLayout(layout);
  assert(lifecycleInside(layout.authorizedBuildRoot,nativeFile)&&lifecycleInside(layout.authorizedBuildRoot,layoutFile)&&(lifecycleInside(layout.authorizedBuildRoot,preparationFile)||postCompileConsumers&&declaredContract.count===26&&lifecycleSame(preparationFile,postCompileConsumers.managedPreparation.path)&&n.preparation.sha256===postCompileConsumers.managedPreparation.sha256),'lifecycle preparations escaped authority');
  assert(lifecycleSame(n.sourceRoot,layout.sourceRoot)&&lifecycleSame(preparation.sourceRoot,layout.sourceRoot),'one captured lifecycle source required');
  const managedFile=add({path:preparation.managed.manifestPath,sha256:preparation.managed.manifestSha256},layout.authorizedBuildRoot),managed=read(managedFile);
  assert(managed.status==='BUILT'&&managed.managedInputContract===layout.managedInputContract&&managed.managedInputCount===declaredContract.count&&managed.terminalCleanupCompiled===true&&managed.realSdkLoaded===false&&managed.hardwareIo===false&&managed.semanticMutation==null,'actual unmutated SDK-free managed22 build required');
  const decoder=verifyManagedDecoderCompileEvidence(managed,declaredContract.id,layout.authorizedBuildRoot);if(decoder){add(decoder.generated,layout.authorizedBuildRoot);add(decoder.compiled,layout.authorizedBuildRoot);}
   add(terminal22LifecycleManagedDllPin(managed,n.native?.managedProof?.dll,layout.authorizedBuildRoot),layout.authorizedBuildRoot);
  const identity=JSON.stringify([layoutFile,preparation.layout.sha256,preparationFile,n.preparation.sha256,layout.sourceRoot,layout.primaryRoot,layout.workspaceRoot,layout.authorizedBuildRoot,layout.productMainSha256,layout.manifestSha256,managedFile,preparation.managed.manifestSha256]);
  if(!common){
   common={identity,layout,preparation,managed,managedContract:declaredContract};const moduleRoot=path.join(layout.sourceRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
   // Trust local original implementations before reading executable captured modules.
   sourceCheck=()=>verifyTerminal22LifecycleCapture(layout);
   const sharedFile=path.join(moduleRoot,'r3-current-run.mjs'),localSharedFile=fileURLToPath(new URL('./r3-current-run.mjs',import.meta.url));
    const pairing=postCompileConsumers?.kind==='TERMINAL26_FIXED_OWNED_CPP_LIFECYCLE_SOURCE_V2'?postCompileConsumers.compiledSourceScope?.consumerSourceSuccessorProof:null;
    if(pairing){
     assert(pairing.method==='EXACT_THREE_REPORT_CONSISTENCY_HUNKS_FULL_INVERSE'&&pairing.hunks===3&&pairing.wholeInverseByteEqual===true&&pairing.completeFailureMaskVerified===false&&pairing.nativeRuntimeAccepted===false&&lifecycleSame(pairing.compiledOriginal?.path,sharedFile)&&lifecycleSame(pairing.executingCurrent?.path,localSharedFile),'exact verified compiled/executing consumer pairing required');
     add(pairing.compiledOriginal);add(pairing.executingCurrent);
    }else add({path:sharedFile,sha256:hash(localSharedFile)});
   shared={currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode};
   const localContract=fileURLToPath(new URL('./r3-managed-input-contract.mjs',import.meta.url)),contractFile=path.join(moduleRoot,'r3-managed-input-contract.mjs');assert.equal(layout.managedContractCodeSha256,hash(localContract),'lifecycle managed contract differs from trusted code');add({path:contractFile,sha256:hash(localContract)});validateTrustedLifecycleManagedRows(layout.expectedManagedSources,layout.managedInputContract);validateTrustedLifecycleManagedRows(managed.inputBindings.sources,layout.managedInputContract);
   assert.deepEqual(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),layout.expectedManagedSources,'actual managed22 rows differ from compile layout');
   add({path:path.join(layout.sourceRoot,'native/main.cpp'),sha256:layout.productMainSha256});add({path:path.join(layout.sourceRoot,'.ymcc-source-snapshot/manifest.json'),sha256:layout.manifestSha256});add({path:path.join(layout.sourceRoot,'.ymcc-source-snapshot/node-tools-binding.json'),sha256:layout.nodeBindingSha256});
   const verifierFile=path.join(layout.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs');add({path:verifierFile,sha256:layout.verifierCodeSha256});add(pin(fileURLToPath(new URL('../source-snapshot.mjs',import.meta.url))));add(pin(fileURLToPath(new URL('../runner.mjs',import.meta.url))));add(pin(fileURLToPath(new URL('./r3-current-run.mjs',import.meta.url))));add(pin(localContract));
   for(const row of managed.inputBindings.sources){assert(lifecycleSame(row.path,path.join(layout.sourceRoot,row.relative))&&lifecycleInside(layout.authorizedBuildRoot,row.snapshot),'managed22 source/snapshot escaped original lifecycle roots');add({path:row.path,sha256:row.sha256});add({path:row.snapshot,sha256:row.sha256},layout.authorizedBuildRoot);}
   for(const row of [...managed.inputBindings.generatedInputs,...managed.runtimeArtifacts])add(row,layout.authorizedBuildRoot);for(const row of managed.evaluatedCompilerInputs)add(row);
  }else assert.equal(identity,common.identity,'all lifecycle preparations must share exact source layout and managed build');
  assert(n.batchAfter?.status==='AUTHORITY_STABLE'&&n.batchAfter.source?.snapshotIntegrityVerified===true&&n.batchAfter.source.creationStatus==='PASS'&&n.batchAfter.fullSourceVerificationCalls===2&&lifecycleSame(n.batchAfter.source.snapshotRoot,layout.sourceRoot)&&n.batchAfter.source.manifestSha256===layout.manifestSha256,'actual stable compile authority required');
  for(const row of postCompileConsumers?postCompileConsumers.currentToolsByRole[n.role]:n.batchAfter.actualTools)add(row);if(n.runner)add(n.runner);if(n.toolchainManifest)add(n.toolchainManifest);
  validateTerminalLifecycleNativeReceipt(n,declaredContract.id,rolePlan.mutation);
  const compileFile=path.join(path.dirname(nativeFile),'native','COMPILE-ONLY.json');add(pin(compileFile),layout.authorizedBuildRoot);assert.deepEqual(read(compileFile),n.native,'actual native compile receipt differs from preparation');
  add(n.native.exe,layout.authorizedBuildRoot);add(n.native.buildScript,layout.authorizedBuildRoot);const sourceFile=add(n.native.sourceManifest,layout.authorizedBuildRoot),source=read(sourceFile);
  assert(lifecycleSame(n.native.managedProof?.dll?.path,managed.fixtureDll)&&n.native.managedProof.dll.sha256===hash(managed.fixtureDll)&&lifecycleSame(n.native.managedProof.manifest,managedFile)&&n.native.managedProof.manifestSha256===preparation.managed.manifestSha256,'native was not compiled with the exact lifecycle managed22');
  for(const row of source.records){const actualTool=n.batchAfter.actualTools.some(tool=>lifecycleSame(tool.path,row.original)&&tool.sha256===row.sha256);assert(lifecycleInside(layout.sourceRoot,row.original)||row.inputRole==='actual-current-compile-tool'&&actualTool,'native lifecycle source escaped capture or actual tool authority');add({path:row.original,sha256:row.sha256});add({path:row.snapshot,sha256:row.sha256},layout.authorizedBuildRoot);}for(const row of source.compiledGenerated)add(row,layout.authorizedBuildRoot);
  if(n.compilerLedger){const ledgerFile=add(n.compilerLedger,layout.authorizedBuildRoot),ledger=read(ledgerFile);for(const row of ledger.inputs)add(row);}
  assert(lifecycleSame(n.runtimeRoot,path.join(path.dirname(nativeFile),'qualification-runtime'))&&lifecycleInside(layout.authorizedBuildRoot,n.runtimeRoot),'original compiled runtime owner required');lifecyclePlainPath(n.runtimeRoot);
  const runtimeLayout=terminalPreparedRuntimeDirectories(n,layout);
  if(runtimeLayout){for(const test of runPlan.filter(row=>row.role===n.role)){const row=runtimeLayout.lifecycle.find(row=>row.label===currentLifecycleModeLabel(test));assert(row&&!fs.existsSync(row.path),'fresh exact lifecycle mode directory required');}}else assert(!fs.existsSync(n.runtimeRoot),'fresh original lifecycle runtime required');
  prepared.set(n.role,{n,nativeFile,managed,runtimeLayout,source});
 }
 const {layout}=common;assert(lifecycleInside(layout.authorizedBuildRoot,inputFile),'lifecycle declaration escaped original authority');lifecyclePlainPath(out);const root=path.resolve(out);assert(lifecycleInside(layout.authorizedBuildRoot,root)&&!fs.existsSync(root),'fresh observation root under original lifecycle authority required');
 add(pin(fileURLToPath(import.meta.url)));fs.mkdirSync(root,{recursive:true});
 const result={schemaVersion:1,status:'VERIFYING_LIFECYCLE_INPUTS',kind:'TERMINAL'+declaredContract.count+'_ORIGINAL_LIFECYCLE_OBSERVATION',managedInputContract:declaredContract.id,inputDeclaration:inputPin,sourceRoot:layout.sourceRoot,originalModeBudgetMs:45000,originalGroupBudgetMs:90000,effectiveGroupBudgetMs:groupBudgetMs,lifecycleBudgetScope:terminal22LifecycleBudgetScope(groupBudgetMs),postCompileConsumerSuccessor:postCompileConsumers?{kind:postCompileConsumers.kind,...(postCompileConsumers.compiledSourceScope?{compiledSourceScope:postCompileConsumers.compiledSourceScope}:{}),sourceReadCosts:postCompileConsumers.sourceReadCosts,legacyConsumers:postCompileConsumers.legacyConsumers,currentConsumers:postCompileConsumers.currentConsumers,sourceProofs:postCompileConsumers.sourceProofs,compilerReceiptsUnmodified:true}:undefined,modeResults:[],...(directed?{diagnosticOnly:true,selectedModeLabels:runPlan.map(currentLifecycleModeLabel)}:{}),sourceCheckTimings,preflight:{managedDll:pin(common.managed.fixtureDll),managedSources:declaredContract.count,nativePreparations:selected},qualificationAccepted:false,publicRuntimeAccepted:false,currentPrimaryAccepted:false,fullCoordinatorRuntimeAccepted:false,sourceApprovalGranted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,compilationFailureCountedAsNegative:false,errors:[]};
 const resultFile=path.join(root,'OBSERVATION.json'),save=()=>fs.writeFileSync(resultFile,JSON.stringify(result,null,2)+'\n');let started=groupStarted;
 try{
  result.sourceBefore=measureTerminal26LifecycleSourceCheck(sourceCheck,'before-modes',sourceCheckTimings);for(const row of inputs.values()){lifecyclePlainPath(row.path);assert.equal(hash(row.path),row.sha256,'lifecycle pre-run input changed');}fs.writeFileSync(path.join(root,'INPUTS.json'),JSON.stringify([...inputs.values()],null,2)+'\n');
  const runnerFile=path.join(layout.sourceRoot,'tools/ymcc_sim/global/runner.mjs');add({path:runnerFile,sha256:hash(fileURLToPath(new URL('../runner.mjs',import.meta.url)))});const runProcess=trustedLifecycleRunProcess;
  for(const {n,runtimeLayout}of prepared.values()){if(runtimeLayout)fs.mkdirSync(n.runtimeRoot,{recursive:true});else fs.mkdirSync(n.runtimeRoot);}result.startedAt=new Date(started).toISOString();result.status=directed?'RUNNING_SELECTED_ORIGINAL_LIFECYCLE_DIAGNOSTIC':'RUNNING_ORIGINAL16_LIFECYCLE_MODES';save();
  for(const test of runPlan){
   const budget=terminal22LifecycleRemainingBudget(started,groupBudgetMs);
   const {n,runtimeLayout,source}=prepared.get(test.role),label=shared.currentLifecycleModeLabel(test),dir=runtimeLayout?runtimeLayout.lifecycle.find(row=>row.label===label).path:path.join(n.runtimeRoot,label);assert(!fs.existsSync(dir),'fresh lifecycle mode evidence required');fs.mkdirSync(dir);
   const args=[dir,test.mode],processResult=await runProcess(n.native.exe.path,args,{cwd:dir,env:{...process.env,YEMAN_POWER_CONTROL_DIR:dir},timeoutMs:budget.timeoutMs});
   const {stdout,stderr,...terminal}=processResult;fs.writeFileSync(path.join(dir,'stdout.log'),stdout);fs.writeFileSync(path.join(dir,'stderr.log'),stderr);
   const row={label,role:test.role,mode:test.mode,mutation:test.mutation,expectedNegative:!!test.mutation||test.mode.startsWith('mutation'),expectedNativeExit:test.expectedExit,nativeProcess:{exe:n.native.exe.path,args,...terminal,originalModeBudgetMs:45000,effectiveTimeoutMs:budget.timeoutMs},execution:terminal22ExecutionFromProcess(terminal),semanticAccepted:false};result.modeResults.push(row);save();
   assert(terminal.timedOut===false&&terminal.launchError===null&&terminal.signal===null&&terminal.outputTruncated===false&&Number.isFinite(terminal.durationMs)&&terminal.durationMs>=0&&terminal.durationMs<=budget.timeoutMs&&terminal.exitCode===test.expectedExit,'original natural lifecycle45 exit required: '+label);
   const rawFile=path.join(dir,'fixture-result.json');assert(fs.existsSync(rawFile),'complete lifecycle native raw required');row.raw=pin(rawFile);const raw=read(rawFile),entryIndex=test.mode==='entry'?0:test.mode==='mutation1'?1:2;
   const entryText=!test.mutation&&(test.mode==='entry'||test.mode.startsWith('mutation'))?fs.readFileSync(path.join(dir,'msi-start-order-entry-selftest-m'+entryIndex+'.txt'),'utf8'):undefined;
   const tupleSource=declaredContract.signalEvidence&&test.role==='positive'&&test.mode==='tuple-protection'&&!test.mutation?(postCompileConsumers?(postCompileConsumers.kind==='TERMINAL26_FIXED_OWNED_CPP_LIFECYCLE_SOURCE_V2'?postCompileConsumers.readOwnedTupleSource(n,source):terminal26CurrentCaptureTupleSource(n,source)):terminal26CompiledTupleSource(n,source)):undefined;
   const switchSource=declaredContract.signalEvidence&&test.role==='positive'&&test.mode==='switch-recovery'&&!test.mutation?terminal26CompiledSwitchSource(n,source):undefined;
   const negativeReportShape=declaredContract.signalEvidence&&test.mutation?.startsWith('rollback-switch-')?verifyTerminal26LifecycleNegativeShape(raw,read(negativeReportGrammarFile),terminal26CompiledSwitchSource(n,source)):undefined;
   const semantic=shared.verifyCurrentLifecycleMode(test,raw,{entryText,managedInputContract:declaredContract.id,tupleSource,switchSource,requireMsiReturnObservation:test.mode==='recovery-protection'&&Boolean(source.policyFixture?.returnObservation)});row.msiReturnObservation=semantic.msiReturnObservation;row.negativeReportShape=negativeReportShape;row.semanticAccepted=true;row.exitCode=terminal.exitCode;row.checks=raw.checks;row.originalOrderChecks=['entry','mutation1','mutation2'].includes(test.mode)?56:undefined;row.caseNames=raw.cases.map(item=>item.name);row.rawSummary={status:raw.status,checks:raw.checks,failures:raw.failures};save();  }
  assert(Date.now()-started<=groupBudgetMs,'effective lifecycle group budget exceeded');for(const row of inputs.values()){lifecyclePlainPath(row.path);assert.equal(hash(row.path),row.sha256,'lifecycle post-run input changed');}result.sourceAfter=measureTerminal26LifecycleSourceCheck(sourceCheck,'after-modes',sourceCheckTimings);assert(Date.now()-started<=groupBudgetMs,'effective lifecycle group budget exceeded after input verification');result.postInputsStable=true;result.selectedPlanComplete=result.modeResults.length===runPlan.length;result.originalPlanComplete=!directed&&result.selectedPlanComplete;assert(result.selectedPlanComplete,'all selected lifecycle modes required');
  result.qualificationSteps=Object.fromEntries(result.modeResults.filter(row=>row.mutation).map(row=>[row.role,{stepLabel:row.label,raw:row.raw}]));result.status='ORIGINAL16_VERIFIED_PENDING_FINAL_DEADLINE';
 }catch(error){result.status='INCOMPLETE';result.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
 // One final measured wall sample controls status, exit and deadline facts together.
 const final=finalizeTerminal22LifecycleExecution({started,complete:result.selectedPlanComplete===true&&result.postInputsStable===true,failed:result.errors.length>0,modeResults:result.modeResults,groupBudgetMs,modeSelection});
 result.status=final.status;result.execution=final.execution;result.finishedAt=final.finishedAt;result.withinOriginalGroupBudget=final.withinOriginalGroupBudget;
 if(final.deadlineExceeded)result.errors.push({message:'effective lifecycle final group budget exceeded'});if(final.execution.exitCode!==0)process.exitCode=1;
 save();console.log(JSON.stringify({status:result.status,modeResults:result.modeResults.length,resultFile,errors:result.errors.map(row=>row.message)}));return result;
}


export async function observeTerminal26Lifecycle(args=process.argv.slice(2)){
 const options=parseTerminal22ObservationArguments(args);
 assert(options['--lifecycle-preparations']&&!options['--native-preparation'],'dedicated original lifecycle entry only');
 return observeTerminal22Lifecycle(options['--lifecycle-preparations'],options['--out'],options['--consumer-predecessors'],options['--lifecycle-budget-ms']===undefined?90000:Number(options['--lifecycle-budget-ms']),options['--lifecycle-modes']);
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await observeTerminal26Lifecycle();
