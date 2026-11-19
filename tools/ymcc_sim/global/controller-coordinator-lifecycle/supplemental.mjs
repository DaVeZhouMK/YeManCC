// Optional scope of the existing controller.coordinator-lifecycle ID.
// Uses the selected original producer without driving devices or dispatching the default suite.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {gpdPreHidePrefix,gpdPreHideParameterPath,readGpdPreHideSelection} from '../../../fixtures/input_coordinator_lifecycle/gpd_pre_hide_selection.mjs';
import {startOwnedRawProducer,rawProgramLauncherDeclaration,verifyRawProducerBudget} from '../decky-persistent-lifecycle/raw-owned-producer.mjs';

const SELF=fileURLToPath(import.meta.url),REPO=path.resolve(path.dirname(SELF),'../../../..');
const WORKSPACE=path.resolve(REPO,'../../..'),EVIDENCE=path.join(WORKSPACE,'Testing/YMCC/Evidence');
const MODULE='tools/ymcc_sim/global/controller-coordinator-lifecycle',ID='controller.coordinator-lifecycle';
const CONFIG=path.join(REPO,MODULE,'prehost-writer-fixtures.json');
const key=p=>path.resolve(p).toLowerCase(),inside=(parent,p)=>key(p).startsWith(key(parent)+path.sep);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
function plain(p){
  need(typeof p==='string'&&path.isAbsolute(p),'ABSOLUTE_PATH_REQUIRED');let cursor=path.resolve(p);
  while(!fs.existsSync(cursor)){const next=path.dirname(cursor);need(next!==cursor,'PATH_ANCESTOR_REQUIRED');cursor=next;}
  for(;;){need(!fs.lstatSync(cursor).isSymbolicLink()&&key(fs.realpathSync.native(cursor))===key(cursor),'REPARSE_PATH:'+p);const next=path.dirname(cursor);if(next===cursor)break;cursor=next;}
  return path.resolve(p);
}
function pin(p){plain(p);const b=fs.readFileSync(p);return {path:path.resolve(p),bytes:b.length,sha256:sha(b)};}
function check(row){need(row&&path.isAbsolute(row.path)&&Number.isSafeInteger(row.bytes)&&row.bytes>=0&&/^[a-f0-9]{64}$/.test(row.sha256),'FILE_PIN_REQUIRED');const actual=pin(row.path);need(actual.bytes===row.bytes&&actual.sha256===row.sha256,'INPUT_IDENTITY_CHANGED:'+row.path);return actual;}
function unique(rows){const result=new Map();for(const row of rows){const previous=result.get(key(row.path));need(!previous||(previous.sha256===row.sha256&&previous.bytes===row.bytes),'CONFLICTING_PIN:'+row.path);result.set(key(row.path),row);}return [...result.values()];}
function remaining(deadline,reserve=5000){need(Number.isSafeInteger(deadline)&&deadline-Date.now()>reserve,'ORIGINAL_DEADLINE_EXHAUSTED');return deadline-Date.now()-reserve;}

function requireSupplementalCase(caseName){
  need(caseName==='prehost-writer'||caseName==='late-terminal','UNKNOWN_COORDINATOR_CASE');
  return caseName;
}

function gpdPreHideAdapter({caseName,deadlineEpochMs,planning,toolPlan,managedDll,dotnet}){
  need(typeof planning==='boolean','SUPPLEMENTAL_PLANNING_BOOLEAN_REQUIRED');
  need(toolPlan===undefined&&managedDll===undefined&&dotnet===undefined,'GPD_PRE_HIDE_REQUIRES_NATIVE_ONLY');
  if(!planning)need(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>Date.now(),'INHERITED_FUTURE_DEADLINE_REQUIRED');
  const selection=readGpdPreHideSelection(fs.readFileSync(path.join(REPO,gpdPreHideParameterPath),'utf8'),caseName);
  const [suite]=selection.requestedModes,fixture='tools/fixtures/input_coordinator_lifecycle/';
  return {id:ID,domain:'controller',title:'GPD first ON source before hide: '+(selection.selectedShape??selection.selectedShapes.join(', ')),kind:'node',
    path:'tools/input_coordinator_lifecycle_selftest.mjs',args:['--out','{outDir}/p','--suite',suite,'--case',caseName],timeoutMs:selection.selectedShapes?330000+90000*selection.expectedCount:420000,
    profiles:['desktop','full'],scope:'behavioral-mock',selectedCoordinatorCase:caseName,
    selectionDetails:{suite,expectedCases:selection.expectedCases,nativeOnly:true,requiresSdkFreeManagedFixture:false},
    evidenceInputs:[MODULE+'/supplemental.mjs',fixture+'fixture.cpp',gpdPreHideParameterPath,fixture+'gpd_pre_hide_selection.mjs',fixture+'compiler_inputs.mjs',
      'tools/input_coordinator_current_compile_only.mjs','tools/input_coordinator_terminal26_source_profile.mjs',
      'tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'],
    artifactInputs:[],resultFiles:['p/RESULT.json'],cacheAcceptance:false,
    boundary:'Only explicit GPD pre-hide cases from the original C++ table. Compile once; each case uses its own process and runtime root, original assertions and deadlines. No hardware, managed Host, default suite or release acceptance.'};
}

function managedLifecycleAdapter({caseName,deadlineEpochMs,planning,managedDll,dotnet}){
  const original='tools/input_coordinator_lifecycle_selftest.mjs';
  const gpd=caseName==='gpd-expired-first-on-full-cycle';
  const lateWake=caseName==='wake-kept-source:GPD:expired-then-current';
  const suite=gpd||lateWake?'lifecycle-directed':'target-return';
  const pipeSource='tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc';
  const args=['--out','{outDir}/p','--suite',suite,'--case',caseName];
  if(gpd)args.push('--native-writer-return','actual');
  const bound=managedDll!==undefined||dotnet!==undefined;
  const safePath=p=>typeof p==='string'&&path.isAbsolute(p)&&!/[\0\r\n]/.test(p)&&!p.includes('{outDir}');
  if(!planning||bound)need(safePath(managedDll)&&path.extname(managedDll).toLowerCase()==='.dll'&&safePath(dotnet)&&path.basename(dotnet).toLowerCase()==='dotnet.exe',gpd||lateWake?'GPD_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED':'PREPARE_REJECTION_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED');
  const artifactInputs=[];
  if(!planning){
    need(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>Date.now(),'INHERITED_FUTURE_DEADLINE_REQUIRED');
    const dll=plain(managedDll),exe=plain(dotnet),dir=path.dirname(dll),stem=dll.slice(0,-4);
    artifactInputs.push(dll,plain(path.join(dir,'fixture-manifest.json')),plain(stem+'.deps.json'),plain(stem+'.runtimeconfig.json'),exe);
    args.push('--managed-dll',dll,'--dotnet',exe);
  }
  let expectedCases=['PREPARE-REJECT/ROG/actual-cold-create-exception'];
  if(gpd){
    const source=fs.readFileSync(path.join(REPO,pipeSource),'utf8');
    const table=/\bgpdExpiredFirstOnFullCycleTerminals\[\]\s*=\s*\{\s*"(off|exit)"\s*,\s*"(off|exit)"\s*\}\s*;/.exec(source);
    need(table&&table[1]!==table[2],'GPD_ORIGINAL_OFF_AND_EXIT_TABLE_REQUIRED');
    expectedCases=table.slice(1).map(terminal=>'GPD/first-on-expired-full-cycle/'+terminal);
  }else if(lateWake){
    const source=fs.readFileSync(path.join(REPO,pipeSource),'utf8');
    const table=/\bwakeKeptSourceFamilyControls\[\]\s*=\s*\{([\s\S]*?)\r?\n\};/.exec(source);
    need(table&&[...table[1].matchAll(/\{\s*"GPD"\s*,\s*"expired-then-current"\s*,\s*2\s*,\s*true\s*\}/g)].length===1,'GPD_LATE_WAKE_DIRECTED_PARAMETER_REQUIRED');
    expectedCases=['GPD/wake-kept-source/expired-then-current'];
  }
  return {id:ID,domain:'controller',title:gpd?'GPD native writer return through OFF and exit':lateWake?'GPD expired ON and late same-Host wake':'Cold PREPARE rejection and next ON recovery',kind:'node',path:original,args,timeoutMs:420000,
    profiles:['desktop','full'],scope:'behavioral-mock',selectedCoordinatorCase:caseName,
    selectionDetails:{suite,expectedCases,requiresSdkFreeManagedFixture:true,...(gpd?{nativeWriterReturn:true}:{} )},
    evidenceInputs:[MODULE+'/supplemental.mjs','tools/fixtures/input_coordinator_lifecycle/fixture.cpp',
      ...(gpd?[pipeSource,'tools/fixtures/input_coordinator_lifecycle/gpd_native_writer_return.inc','tools/fixtures/input_coordinator_lifecycle/gpd_stop_receipt.mjs']:
        lateWake?[pipeSource,'native/virtual_switch_verification_runtime.h','native/virtual_wake_execution_runtime.h']:['tools/fixtures/input_coordinator_lifecycle/prepare_rejection_cases.inc']),
      'tools/fixtures/input_coordinator_lifecycle/compiler_inputs.mjs',
      'tools/input_coordinator_current_compile_only.mjs','tools/input_coordinator_terminal26_source_profile.mjs',
      'tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'],
    artifactInputs,resultFiles:lateWake?['p/RESULT.json','p/source-wake-selection-observed.json','p/full-pipe-core/fixture-result.json']:['p/RESULT.json'],cacheAcceptance:false,
    boundary:(gpd?'Two original SDK-free managed full-pipe terminals with actual native writer retirement and resource return.':lateWake?
      'One SDK-free managed full-pipe case: original ON expiry followed by current-power same-Host frame and wake, with original terminal preserved.':'One original native/SDK-free managed target-return scenario.')+
      ' Original compile and mode limits under the core owned Job. No default suite, hardware or release acceptance.'};
}

// Planning does not qualify external artifacts or load the full factory.
export function getSupplementalAdapter({caseName,deadlineEpochMs,planning=false,toolPlan,managedDll,dotnet}={}){
  if(caseName?.startsWith(gpdPreHidePrefix))return gpdPreHideAdapter({caseName,deadlineEpochMs,planning,toolPlan,managedDll,dotnet});
  if(caseName==='prepare-rejection-cold'||caseName==='gpd-expired-first-on-full-cycle'||caseName==='wake-kept-source:GPD:expired-then-current')return managedLifecycleAdapter({caseName,deadlineEpochMs,planning,managedDll,dotnet});
  if(caseName==='onex-diagnostic-tls-startup'){
    need(typeof planning==='boolean','SUPPLEMENTAL_PLANNING_BOOLEAN_REQUIRED');
    need(typeof toolPlan==='string'&&path.isAbsolute(toolPlan)&&!/[\0\r\n]/.test(toolPlan),'ONEX_TLS_ABSOLUTE_TOOL_PLAN_REQUIRED');
    const args=['--evidence-dir','{outDir}/coordinator','--case',caseName,'--tool-plan',toolPlan];
    if(!planning){
      need(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>Date.now(),'INHERITED_FUTURE_DEADLINE_REQUIRED');
      args.push('--deadline-epoch-ms',String(deadlineEpochMs));
    }
    return {id:ID,domain:'controller',title:'Selected original coordinator OneX diagnostic startup boundary',
      kind:'node',path:MODULE+'/run.mjs',args,timeoutMs:120000,profiles:['desktop','full'],scope:'behavioral-mock',
      selectedCoordinatorCase:caseName,
      evidenceInputs:[MODULE+'/run.mjs',MODULE+'/supplemental.mjs',MODULE+'/onex-tls.mjs',MODULE+'/onex-tls.py',MODULE+'/onex-tls.cpp.in',
        'tools/fixtures/hc_onex_x2_application/extract.mjs','native/virtual_controller_onex_start_runtime.h','deps/json/json.hpp',
        'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs','tools/ymcc_sim/global/host-process.ps1',
        'tools/ymcc_sim/optimization/cli.mjs','tools/ymcc_sim/optimization/selection-policy.mjs'],
      artifactInputs:[toolPlan],resultFiles:['coordinator/result.json','coordinator/worker-result.json'],cacheAcceptance:false,
      boundary:'Only the existing OneX pre-CRT diagnostic counterexample and Release/Checked22 checks; no full coordinator or47-case family. '+
        'Planning constructs an inert descriptor without a deadline or tool reads; execution rematerializes it with the existing owner cutoff. '+
        'The original leaf qualifies the explicit source/tool/compile graph, retains its120-second cap and owned Job. No current whole Native, Host, device, cache or release acceptance.'};
  }
  requireSupplementalCase(caseName);need(typeof planning==='boolean','SUPPLEMENTAL_PLANNING_BOOLEAN_REQUIRED');
  need(toolPlan===undefined,'COORDINATOR_TOOL_PLAN_REQUIRES_ONEX_TLS_CASE');
  const args=['--evidence-dir','{outDir}/coordinator','--case',caseName];
  if(planning)args.push('--plan');
  else{
    need(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>Date.now(),'INHERITED_FUTURE_DEADLINE_REQUIRED');
    args.push('--deadline-epoch-ms',String(deadlineEpochMs));
  }
  return {id:ID,domain:'controller',title:'Selected original coordinator runtime cases: '+caseName,
    kind:'node',path:MODULE+'/supplemental.mjs',args,timeoutMs:90000,profiles:['desktop','full'],scope:'behavioral-mock',
    selectedCoordinatorCase:caseName,
    evidenceInputs:[MODULE+'/supplemental.mjs',MODULE+'/prehost-writer-fixtures.json',MODULE+'/README.md',
      'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs','tools/ymcc_sim/global/host-process.ps1',
      'tools/ymcc_sim/optimization/cli.mjs','tools/ymcc_sim/optimization/selection-policy.mjs'],
    artifactInputs:[],resultFiles:['coordinator/result.json'],cacheAcceptance:false,
    boundary:'Only the selected original '+caseName+' group and its original semantic negatives; full coordinator and sibling cases are not selected. '+
      'Fixed descriptor source declarations are not a cache-certified closure. Actual source/tool/artifact admission remains in executeSupplemental/runtimeSources and raw validation. '+
      'The optimizer supplies its existing owned Job and inherited absolute cutoff; no new outer session or budget. No full Native, managed Host, device or real sleep acceptance.'};
}

export function parseSupplementalArgs(args){
  const values={},flags=new Set();
  for(let i=0;i<args.length;++i){const name=args[i];need(['--evidence-dir','--case','--deadline-epoch-ms','--plan'].includes(name),'UNKNOWN_SUPPLEMENTAL_ARGUMENT:'+name);need(!flags.has(name),'DUPLICATE_SUPPLEMENTAL_ARGUMENT:'+name);flags.add(name);
    if(name==='--plan'){values.plan=true;continue;}need(i+1<args.length&&!args[i+1].startsWith('--'),'MISSING_SUPPLEMENTAL_ARGUMENT:'+name);values[name]=args[++i];}
  requireSupplementalCase(values['--case']);
  need(values['--evidence-dir']&&path.isAbsolute(values['--evidence-dir']),'ABSOLUTE_EVIDENCE_REQUIRED');
  const raw=values['--deadline-epoch-ms'];if(raw!==undefined)need(/^[1-9][0-9]*$/.test(raw)&&Number.isSafeInteger(Number(raw)),'ABSOLUTE_DEADLINE_REQUIRED');
  need(values.plan||raw!==undefined,'INHERITED_DEADLINE_REQUIRED');
  return {caseName:values['--case'],root:values['--evidence-dir'],plan:values.plan===true,deadlineEpochMs:raw===undefined?null:Number(raw)};
}

function qualifyCurrentSourceMutations(spec,root,boundary){
  const names=['positive','negative-live-writer','negative-single-attempt','negative-observation-deadline','negative-baseline-terminal-gate','negative-prior-recovery-owner'];
  need(JSON.stringify(Object.keys(spec.variants))===JSON.stringify(names),'EXACT_CURRENT_VARIANTS_REQUIRED');
  need(boundary.argumentCount===1&&JSON.stringify(boundary.allowedSelectors)===JSON.stringify(['live','duplicate','late-queue','late-terminal']),'EXACT_CURRENT_ARGUMENT_SCOPE_REQUIRED');
  const cpp=fs.readFileSync(path.join(REPO,'tools/fixtures/input_coordinator_lifecycle/prehost_writer_cases.cpp'));
  need(sha(cpp)===check(boundary.currentCpp).sha256,'CURRENT_POSITIVE_CPP_REQUIRED');
  const policy=read(check(boundary.mutationPolicy).path),runtime=fs.readFileSync(path.join(REPO,'native/virtual_runtime_recovery_runtime.h'),'utf8');
  need(policy.schemaVersion===1&&policy.kind===boundary.kind&&policy.historicalWholeHeadersReused===false,'CURRENT_MUTATION_POLICY_REQUIRED');
  need(check(policy.cppSnapshot).sha256===sha(cpp)&&check(policy.cppSource).sha256===sha(cpp)&&check(policy.runtimeSnapshot).sha256===sha(Buffer.from(runtime))&&check(policy.runtimeSource).sha256===sha(Buffer.from(runtime)),'CURRENT_MUTATION_SOURCE_REQUIRED');
  need(JSON.stringify(Object.keys(policy.mutations))===JSON.stringify(names.slice(1)),'EXACT_FIVE_CURRENT_MUTATIONS_REQUIRED');
  const artifactPins=new Map(spec.artifacts.map(r=>[key(r.path),r]));
  const qualification=read(check(boundary.compilationQualification).path);
  need(qualification.status==='CURRENT_SIX_VARIANTS_COMPILE_QUALIFIED_BUSINESS_NOT_RUN'&&qualification.businessExecutions===0,'CURRENT_COMPILE_QUALIFICATION_REQUIRED');
  for(const [i,name]of names.entries()){
    const variant=spec.variants[name];let body=runtime;
    if(i){const rules=policy.mutations[name];need(rules.length===[1,1,2,2,3][i-1]&&policy.originalSemanticAssertions[name]===variant.assertion&&policy.originalSelections[name]===variant.selection,'ORIGINAL_SEMANTIC_MUTATION_SCOPE_REQUIRED');
      for(const rule of rules){need(typeof rule.before==='string'&&rule.before.length>0&&rule.before.length<=1600&&typeof rule.after==='string'&&rule.after.length<=1600&&body.split(rule.before).length===2,'EXACT_RUNTIME_MUTATION_REQUIRED');body=body.replace(rule.before,rule.after);}
    }
    need(check(variant.cppSource).sha256===sha(cpp)&&check(variant.runtimeHeader).sha256===sha(Buffer.from(body)),'CURRENT_COMPILED_CPP_HEADER_REQUIRED:'+name);
    need(inside(root,plain(variant.compilation.path)),'CURRENT_COMPILATION_PROOF_ROOT_REQUIRED');const binding=read(check(variant.compilation).path);
    need(binding.schemaVersion===1&&binding.status==='COMPILED_CURRENT_VARIANT_NOT_BUSINESS_EXECUTED'&&binding.variant===name&&binding.compilerExitCode===0&&binding.businessExecutions===0,'CURRENT_SUCCESSFUL_COMPILATION_REQUIRED');
    for(const field of ['cppSource','runtimeHeader','exe'])need(key(binding[field].path)===key(variant[field].path)&&check(binding[field]).sha256===check(variant[field]).sha256,'COMPILED_VARIANT_BINDING_REQUIRED:'+field);
    const step=read(check(binding.compileProcess).path);need(step.exitCode===0&&!step.error&&!step.signal&&!step.timedOut&&step.businessExecutions===0&&check(step.program).sha256===check(binding.compiler).sha256,'REAL_CURRENT_COMPILER_TERMINAL_REQUIRED');
    need(step.args.includes(variant.cppSource.path)&&step.args.includes('/std:c++20')&&step.args.includes('/WX')&&step.args.includes('/W3')&&step.args[step.args.indexOf('/sourceDependencies')+1]===binding.sourceDependencies.path,'ORIGINAL_CURRENT_COMPILER_RECIPE_REQUIRED');
    need(check(step.environment).sha256===check(binding.environment).sha256,'BOUND_CURRENT_COMPILER_ENVIRONMENT_REQUIRED');
    const deps=read(check(binding.sourceDependencies).path),actual=[deps.Data.Source,...deps.Data.Includes];need(actual.length===binding.actualCompilerInputs.length,'ACTUAL_COMPILER_INPUT_ROWS_REQUIRED');
    const seen=new Map();for(const [n,row]of binding.actualCompilerInputs.entries()){
      need(key(actual[n])===key(row.path),'ACTUAL_COMPILER_INPUT_ORDER_REQUIRED');const observed=check(row),declared=artifactPins.get(key(row.path));need(declared&&check(declared).sha256===observed.sha256,'DECLARED_ACTUAL_COMPILER_INPUT_REQUIRED');
      const previous=seen.get(key(row.path));need(!previous||previous.sha256===observed.sha256&&previous.bytes===observed.bytes,'CONFLICTING_ACTUAL_COMPILER_INPUT');seen.set(key(row.path),observed);
    }
    need(binding.observedLinkedLibraries.length>0,'OBSERVED_CURRENT_LINK_LIBRARIES_REQUIRED');for(const row of binding.observedLinkedLibraries){const declared=artifactPins.get(key(row.path));need(declared&&check(row).sha256===check(declared).sha256,'DECLARED_CURRENT_LINK_LIBRARY_REQUIRED');}
    for(const rel of ['fixture.cpp','virtual_runtime_recovery_runtime.h','virtual_start_protection.h','virtual_recovery_intent.h','game_input_override.h','json.hpp'])need(binding.actualCompilerInputs.some(r=>key(r.path)===key(path.join(path.dirname(variant.exe.path),rel))),'ACTUAL_CURRENT_NATIVE_HEADER_REQUIRED:'+rel);
  }
}

function contract(caseName){
  requireSupplementalCase(caseName);
  const configBytes=fs.readFileSync(plain(CONFIG)),sourceContract={path:path.resolve(CONFIG),bytes:configBytes.length,sha256:sha(configBytes)};
  const spec=JSON.parse(configBytes.toString('utf8').replace(/^\uFEFF/,''));need(spec.schemaVersion===1&&spec.canonicalTestId===ID,'CANONICAL_FIXTURE_CONTRACT_REQUIRED');
  const root=plain(spec.artifactRoot);need(inside(path.join(WORKSPACE,'Build/Validation'),root)||(spec.negativeCppBoundary?.kind==='CURRENT_SOURCE_EXACT_SEMANTIC_MUTATIONS_V1'&&inside(EVIDENCE,root)),'PREPARED_ARTIFACT_ROOT_REQUIRED');
  need(JSON.stringify(Object.keys(spec.cases).sort())===JSON.stringify(['late-terminal','prehost-writer']),'EXACT_OPTIONAL_CASES_REQUIRED');
  const item=spec.cases[caseName],other=spec.cases[caseName==='prehost-writer'?'late-terminal':'prehost-writer'];
  need(item.selection===(caseName==='prehost-writer'?'all':'late-terminal')&&item.groups.length===(caseName==='prehost-writer'?14:3),'EXACT_LEAF_SELECTION_REQUIRED');
  need(new Set(item.groups).size===item.groups.length&&item.groups.every(x=>typeof x==='string'&&x.length&&!other.groups.includes(x)),'DISJOINT_UNIQUE_GROUPS_REQUIRED');
  need(item.positive==='positive'&&item.negatives.length===(caseName==='prehost-writer'?3:2)&&new Set(item.negatives).size===item.negatives.length,'ORIGINAL_NEGATIVE_POLARITIES_REQUIRED');
  const programs=[{name:item.positive,selection:item.selection,expectedExit:0,assertion:null},...item.negatives.map(name=>({name,...spec.variants[name]}))];
  for(const p of programs){const variant=spec.variants[p.name];need(variant&&inside(root,plain(variant.exe.path)),'BOUND_PRECOMPILED_PROGRAM_REQUIRED');if(p.name!=='positive')need(p.expectedExit===2&&typeof p.assertion==='string'&&p.assertion.length,'SEMANTIC_NEGATIVE_ASSERTION_REQUIRED');}
  const boundary=spec.negativeCppBoundary;
  if(boundary?.kind==='CURRENT_SOURCE_EXACT_SEMANTIC_MUTATIONS_V1')qualifyCurrentSourceMutations(spec,root,boundary);
  else {
  need(boundary?.kind==='EXACT_TWO_ARGUMENT_GUARDS_VALID_ARGV_ONLY'&&boundary.argumentCount===1,'HISTORICAL_NEGATIVE_CPP_BOUNDARY_REQUIRED');
  const cpp=fs.readFileSync(path.join(REPO,'tools/fixtures/input_coordinator_lifecycle/prehost_writer_cases.cpp'));
  need(sha(cpp)===check(boundary.currentCpp).sha256&&sha(cpp)===check(spec.variants.positive.cppSource).sha256,'CURRENT_POSITIVE_CPP_REQUIRED');
  const allowed=['  require(argc==1||argc==2,"expected zero or one selector argument");','  require(cases>0,"unknown or empty selector");'];
  let projected=cpp.toString('utf8');need(boundary.removedLines.length===2,'EXACT_ARGUMENT_GUARD_BOUNDARY');
  boundary.removedLines.forEach((line,i)=>{need(line.replace(/\r?\n$/,'')===allowed[i]&&projected.split(line).length===2,'UNKNOWN_CPP_PROJECTION');projected=projected.replace(line,'');});
  const old=check(boundary.compiledHistoricalCpp);need(sha(Buffer.from(projected))===old.sha256,'CPP_VALID_ARGUMENT_SCOPE_CHANGED');
  for(const p of programs)if(p.name!=='positive')need(boundary.allowedSelectors.includes(p.selection)&&check(spec.variants[p.name].cppSource).sha256===old.sha256,'ORIGINAL_NEGATIVE_CPP_SCOPE_REQUIRED');
  }
  return {spec,item,programs,sourceContract};
}

function describeSupplementalPlan(caseName,item,programs,sourceContract){
  return {schemaVersion:1,status:'PLANNED_ONLY_NOT_EXECUTED',canonicalTestId:ID,selectedCase:caseName,leafSelection:item.selection,
    groups:item.groups,programs:programs.map(p=>({name:p.name,selection:p.selection,expectedExit:p.expectedExit,assertion:p.assertion})),
    sourceContract,compilerInvocations:0,programExecutions:0,defaultCoordinatorDispatched:false,
    boundary:{supplementalRuntimeHeader:true,fullNativeCoordinatorExecuted:false,managedHostExecuted:false,hardwareIo:false,cacheAcceptance:false},
    executionAdmission:'Source/tool/artifact qualification is required again by execute; planning grants no execution acceptance.'};
}
export function planSupplemental(caseName){
  const {item,programs,sourceContract}=contract(caseName);
  return describeSupplementalPlan(caseName,item,programs,sourceContract);
}
export function verifySupplementalReceipt(stdout,groups,selection){
  const lines=stdout.trim().split(/\r?\n/),names=lines.filter(s=>s.startsWith('PASS ')).map(s=>s.slice(5)),summary=JSON.parse(lines.at(-1));
  need(JSON.stringify(names)===JSON.stringify(groups)&&summary.caseGroups===groups.length&&summary.selection===selection&&summary.hardwareIo===false&&summary.wholeNativeConsumer===false,'EXACT_SELECTED_GROUPS_REQUIRED');
  return {groups:names,selection:summary.selection};
}

export function runtimeSources(deadline,entrySource=SELF){
  need(entrySource===SELF||entrySource===path.join(REPO,MODULE,'run.mjs'),'EXACT_OPTIONAL_ENTRY_SOURCE_REQUIRED');
  const seen=new Set(),pending=[SELF,entrySource,path.join(REPO,'tools/ymcc_sim/global/host-process.ps1'),CONFIG];
  while(pending.length){remaining(deadline);const file=path.resolve(pending.pop());if(seen.has(file))continue;need(inside(REPO,file),'SOURCE_OUTSIDE_REPO');plain(file);seen.add(file);
    if(!file.endsWith('.mjs'))continue;const text=fs.readFileSync(file,'utf8');
    if(key(file)===key(path.join(REPO,'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-oracle.mjs'))){
      // This original eager reader uses root+filename instead of new URL.
      // Bind that exact caller statement and its existing immutable JSON pin.
      const digest='43f5de340a6ab0abc5fe1362fecbb27745daab7e0d32ef329d89e9107f858b78';
      const binding="const contractFile=root+'/r3-current105-expectations.json';const CONTRACT_SHA256='"+digest+"';need(fileHash(contractFile)===CONTRACT_SHA256,'CONTRACT_CHANGED');const contract=read(contractFile);";
      need(text.includes(binding),'ORIGINAL_ORACLE_BINDING_CHANGED');
      const data=path.join(path.dirname(file),'r3-current105-expectations.json');need(pin(data).sha256===digest,'ORIGINAL_ORACLE_JSON_CHANGED');pending.push(data);
    }
    for(const m of text.matchAll(/(?:\bfrom\s*|\bimport\s*\(\s*|\bimport\s*)(['"])(\.[^'"\r\n]+)\1/g))pending.push(path.resolve(path.dirname(file),m[2]));
    for(const m of text.matchAll(/new URL\(\s*(['"])(\.[^'"\r\n]+\.json)\1\s*,\s*import\.meta\.url\s*\)/g))pending.push(path.resolve(path.dirname(file),m[2]));
  }
  return [...seen].map(file=>{remaining(deadline);return pin(file);});
}
function toolMembers(spec,deadline){
  const node=check(spec.tools.node),engine=check(spec.tools.powershell);need(key(node.path)===key(process.execPath),'DECLARED_NODE_REQUIRED');
  need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH,'AMBIENT_NODE_INJECTION_REFUSED');
  const declaration=rawProgramLauncherDeclaration({YMCC_SIM_RAW_PROGRAM_PWSH:engine.path});need(declaration,'EXPLICIT_POWERSHELL_RUNTIME_REQUIRED');
  // Same original launcher graph, plus complete selected module directories.
  // Other installed modules and OS-loaded modules are not claimed as observed.
  const root=path.dirname(engine.path),observed=new Set(declaration.files.map(key)),pending=['Management','Utility','Security'].map(name=>path.join(root,'Modules','Microsoft.PowerShell.'+name));
  while(pending.length){remaining(deadline);const d=pending.pop();plain(d);for(const entry of fs.readdirSync(d,{withFileTypes:true})){need(!entry.isSymbolicLink(),'TOOL_TREE_LINK');const p=path.join(d,entry.name);if(entry.isDirectory())pending.push(p);else {need(entry.isFile(),'TOOL_TREE_FILE_REQUIRED');observed.add(key(p));}}}
  const rows=unique(spec.tools.files),declared=rows.filter(r=>inside(root,r.path)).map(r=>key(r.path));
  need(observed.size===declared.length&&[...observed].every(p=>declared.includes(p)),'POWERSHELL_SELECTED_INVENTORY_CHANGED');
  for(const file of declaration.files)need(rows.some(r=>key(r.path)===key(file)),'RAW_LAUNCHER_DEPENDENCY_NOT_DECLARED:'+file);
  for(const row of rows){remaining(deadline);check(row);}return {node,engine,rows};
}
function environment(engine,temp){return {SystemRoot:'C:\\Windows',WINDIR:'C:\\Windows',ComSpec:'C:\\Windows\\System32\\cmd.exe',
  PATH:[path.dirname(process.execPath),path.dirname(engine.path),'C:\\Windows\\System32'].join(';'),TEMP:temp,TMP:temp,
  PSModulePath:path.join(path.dirname(engine.path),'Modules'),POWERSHELL_TELEMETRY_OPTOUT:'1',POWERSHELL_UPDATECHECK:'Off'};}
export function qualifySupplemental(caseName,deadline,entrySource=SELF){
  const {spec,item,programs,sourceContract}=contract(caseName),plan=describeSupplementalPlan(caseName,item,programs,sourceContract),tools=toolMembers(spec,deadline),sources=runtimeSources(deadline,entrySource);
  const currentConfig=sources.find(row=>key(row.path)===key(CONFIG));
  need(currentConfig&&currentConfig.bytes===sourceContract.bytes&&currentConfig.sha256===sourceContract.sha256,'CONFIG_CHANGED_DURING_QUALIFICATION');
  const fixture=spec.fixtureSources.map(row=>({...row,path:path.join(REPO,row.relative)}));
  need(JSON.stringify(fixture.map(r=>r.relative).sort())===JSON.stringify((spec.negativeCppBoundary?.kind==='CURRENT_SOURCE_EXACT_SEMANTIC_MUTATIONS_V1'?['deps/json/json.hpp','native/game_input_override.h','native/virtual_recovery_intent.h','native/virtual_runtime_recovery_runtime.h','native/virtual_start_protection.h','tools/fixtures/input_coordinator_lifecycle/prehost_writer_cases.cpp']:['deps/json/json.hpp','native/virtual_runtime_recovery_runtime.h','native/virtual_start_protection.h','tools/fixtures/input_coordinator_lifecycle/prehost_writer_cases.cpp']).sort()),'EXACT_NATIVE_SOURCE_INPUTS_REQUIRED');
  const inputs=unique([...sources,...fixture,...spec.artifacts,...Object.values(spec.variants).map(v=>v.exe)]);
  for(const row of inputs){remaining(deadline);check(row);}return {spec,plan,tools,inputs};
}

export async function executeSupplemental(options){
  const began=Date.now(),deadline=Math.min(options.deadlineEpochMs,began+90000);remaining(deadline);
  const output=plain(options.root);need(inside(EVIDENCE,output)&&!fs.existsSync(output),'FRESH_CORE_EVIDENCE_REQUIRED');fs.mkdirSync(output,{recursive:true});
  let error=null,proof=null,workerResult=null,plan=null,inputs=[];const steps=[];
  try{
    const qualified=qualifySupplemental(options.caseName,deadline,options.entrySource??SELF),{spec,tools}=qualified;plan=qualified.plan;inputs=qualified.inputs;save(path.join(output,'plan.json'),plan);
    const temp=path.join(output,'temp');fs.mkdirSync(temp);const env=environment(tools.engine,temp);
    // This existing Windows metadata query creates no producer/descendant. Its
    // exact command, tool identity, elapsed and terminal are retained separately.
    const query='[Diagnostics.Process]::GetProcessById('+process.pid+').StartTime.ToUniversalTime().Ticks.ToString()';
    const started=Date.now(),birth=spawnSync(tools.engine.path,['-NoLogo','-NoProfile','-NonInteractive','-Command',query],{env,cwd:output,windowsHide:true,encoding:'utf8',timeout:Math.min(10000,remaining(deadline))});
    save(path.join(output,'owner-query.json'),{program:tools.engine,query,pid:birth.pid,exitCode:birth.status,signal:birth.signal,error:birth.error?.message??null,stdout:birth.stdout,stderr:birth.stderr,elapsedMs:Date.now()-started});
    need(!birth.error&&!birth.signal&&birth.status===0&&/^[0-9]+\s*$/.test(birth.stdout),'OWNER_CREATION_IDENTITY_REQUIRED');
    const ownerStartTicks=birth.stdout.trim(),requestFile=path.join(output,'worker-request.json'),nonce=crypto.randomBytes(24).toString('hex');
    const request={schemaVersion:1,kind:'coordinator-supplemental-precompiled-v1',caseName:options.caseName,deadlineEpochMs:deadline,output,nonce,inputs,tools:tools.rows};save(requestFile,request);
    const allocated=Date.now(),parentBudget=deadline-began;remaining(deadline);
    const owned=startOwnedRawProducer({id:ID+':'+options.caseName,program:process.execPath,args:[SELF,'--worker',requestFile,nonce],cwd:REPO,env,
      host:path.join(REPO,'tools/ymcc_sim/global/host-process.ps1'),powershell:tools.engine.path,ownerStartTicks,
      directory:path.join(output,'owned'),stdoutPath:path.join(output,'worker.stdout.raw'),stderrPath:path.join(output,'worker.stderr.raw'),
      deadlineEpochMs:deadline,allocatedAtEpochMs:allocated,parentStartedAtEpochMs:began,parentBudgetMs:parentBudget,cleanupReserveMs:5000,
      sourceInputs:unique([...inputs,pin(requestFile)]),toolArtifacts:tools.rows,
      originalBudgetMs:90000,parentRemainingBeforeMs:deadline-allocated});
    proof=await owned.wait();save(path.join(output,'process.json'),proof);verifyRawProducerBudget(proof,90000,parentBudget);
    need(proof.exitCode===0&&!proof.error&&!proof.timedOut&&proof.rawExecution.completion?.kind==='NATURAL','OWNED_SELECTED_WORKER_FAILED');
    workerResult=read(path.join(output,'worker-result.json'));need(workerResult.status==='PASS_SELECTED_SCOPE'&&workerResult.selectedCase===options.caseName,'SELECTED_WORKER_RESULT_REQUIRED');
    need(JSON.stringify(workerResult.steps.map(s=>s.name))===JSON.stringify(plan.programs.map(s=>s.name)),'EXACT_SELECTED_PROGRAMS_REQUIRED');steps.push(...workerResult.steps);
    for(const row of inputs){remaining(deadline,0);check(row);}toolMembers(spec,deadline);remaining(deadline,500);
  }catch(cause){error=cause.stack??String(cause);}
  const result={schemaVersion:1,status:error?'FAIL':'PASS_SELECTED_SUPPLEMENTAL',passed:!error,error,canonicalTestId:ID,selectedCase:options.caseName,
    plan,steps,proof,workerResult,sourceHashes:inputs.filter(row=>inside(REPO,row.path)).map(row=>({...row,path:path.relative(REPO,row.path).replace(/\\/g,'/')})),prerequisiteArtifacts:inputs.filter(row=>!inside(REPO,row.path)),elapsedMs:Date.now()-began,effectiveDeadlineEpochMs:deadline,inheritedDeadlineEpochMs:options.deadlineEpochMs,
    compilerInvocations:0,defaultCoordinatorDispatched:false,fullNativeCoordinatorExecuted:false,managedHostExecuted:false,hardwareIo:false,cacheAcceptance:false,
    scope:'Existing actual runtime/admission headers in source-bound precompiled CPP with explicit cached effect ports; selected groups and original semantic negatives only.',
    sourceBoundary:'Literal loader sources and the explicit selected branch. Computed legacy full-suite branches were not dispatched or accepted.'};
  save(path.join(output,'result.json'),result);need(!error,'SUPPLEMENTAL_CASE_FAILED');return result;
}

function worker(requestFile,nonce){
  const request=read(requestFile);need(request.kind==='coordinator-supplemental-precompiled-v1'&&request.nonce===nonce,'BOUND_WORKER_REQUEST_REQUIRED');
  const output=plain(request.output);need(inside(EVIDENCE,output),'WORKER_CORE_OUTPUT_REQUIRED');const steps=[];let error=null;
  try{
    const {spec,item,programs,sourceContract}=contract(request.caseName),expectedConfig=request.inputs.find(row=>key(row.path)===key(CONFIG));
    need(expectedConfig&&expectedConfig.bytes===sourceContract.bytes&&expectedConfig.sha256===sourceContract.sha256,'WORKER_CONFIG_SNAPSHOT_MISMATCH');
    for(const row of request.inputs){remaining(request.deadlineEpochMs);check(row);}
    for(const p of programs){remaining(request.deadlineEpochMs);const exe=check(spec.variants[p.name].exe),started=Date.now(),timeout=remaining(request.deadlineEpochMs);
      const run=spawnSync(exe.path,[p.selection],{cwd:output,env:process.env,windowsHide:true,encoding:'utf8',maxBuffer:4*1024*1024,timeout});
      const stdout=run.stdout??'',stderr=run.stderr??'',step={name:p.name,selection:p.selection,program:exe,pid:run.pid,exitCode:run.status,signal:run.signal,error:run.error?.message??null,errorCode:run.error?.code??null,timedOut:run.error?.code==='ETIMEDOUT',elapsedMs:Date.now()-started,effectiveTimeoutMs:timeout};
      fs.writeFileSync(path.join(output,p.name+'.stdout.raw'),stdout,{flag:'wx'});fs.writeFileSync(path.join(output,p.name+'.stderr.raw'),stderr,{flag:'wx'});save(path.join(output,p.name+'.process.json'),step);steps.push(step);
      need(!run.error&&!run.signal&&run.status===p.expectedExit,'UNEXPECTED_SELECTED_TERMINAL:'+p.name);
      if(p.name==='positive'){
        verifySupplementalReceipt(stdout,item.groups,item.selection);
      }else need((stdout+stderr).includes(p.assertion),'ORIGINAL_NEGATIVE_ASSERTION_MISSING:'+p.name);
    }
    for(const row of request.inputs){remaining(request.deadlineEpochMs,0);check(row);}remaining(request.deadlineEpochMs,500);
  }catch(cause){error=cause.stack??String(cause);}
  save(path.join(output,'worker-result.json'),{status:error?'FAIL':'PASS_SELECTED_SCOPE',error,selectedCase:request.caseName,steps,compilerInvocations:0,fullNativeCoordinatorExecuted:false,hardwareIo:false});
  need(!error,'SELECTED_WORKER_FAILED');
}

if(process.argv[1]&&path.resolve(process.argv[1])===SELF){
  try{
    if(process.argv[2]==='--worker'){
      need(process.argv.length===5,'EXACT_INTERNAL_WORKER_ARGUMENTS_REQUIRED');worker(process.argv[3],process.argv[4]);
    }else{
      const options=parseSupplementalArgs(process.argv.slice(2));
      const result=options.plan?planSupplemental(options.caseName):await executeSupplemental(options);
      console.log(JSON.stringify(result));
    }
  }catch(error){console.error(error.stack??error.message);process.exitCode=1;}
}
