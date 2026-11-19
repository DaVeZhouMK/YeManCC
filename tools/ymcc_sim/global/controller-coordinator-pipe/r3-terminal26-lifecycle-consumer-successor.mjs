// Strictly post-compile, read-only lifecycle consumer successor. No producer or runtime work.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {currentLifecycleModePlan} from './r3-current-run.mjs';

const HERE=path.dirname(fileURLToPath(import.meta.url));
const PRIMARY=path.resolve(HERE,'../../../..');
const PROJECT=path.resolve(PRIMARY,'../../..');
const EVIDENCE=path.join(PROJECT,'Testing/YMCC/Evidence');
const OWNER=path.join(PROJECT,'Testing/YMCC/Workspace/R105CP-20261117A');
const CAPTURE=path.join(OWNER,'Mainline/YeManCC-source/YeManCC');
const BUILD=path.join(OWNER,'Build/Validation/CurrentCoordinatorTerminal22');
const RELATIVE='tools/ymcc_sim/global/controller-coordinator-pipe/';
const OBSERVER='r3-terminal22-qualified-observe.mjs';
const HELPER='r3-terminal26-lifecycle-consumer-successor.mjs';
const CONTRACT='current105-terminal26-sdk-lifetime-compact-v1';
const PROFILE='current105-terminal26-current-capture-20261117a';
const OLD={bytes:57457,sha256:'e04db556441df3448be8d66b1f8102bbba90b0f3f25f183758a35878c3c5620a'};
const INTERMEDIATE={bytes:61009,sha256:'28791901de032d5cbec3db1dd4ec5487ffa62f25138bd1812f46715ce0c2e288'};
const ENTRY="r3-terminal26-lifecycle-observe.mjs";
const ENTRY_EXPECTED={"bytes":33348,"sha256":"61cc26264ae02a1be4219137a28a1ef7e0887c8a9ae1c8c6dd7bbacd74b9b95c"};
const ENTRY_CURRENT_TUPLE_PATCH={"before":"const tupleSource=declaredContract.signalEvidence&&test.role==='positive'&&test.mode==='tuple-protection'&&!test.mutation?terminal26CompiledTupleSource(n,source):undefined;","after":"const tupleSource=declaredContract.signalEvidence&&test.role==='positive'&&test.mode==='tuple-protection'&&!test.mutation?(postCompileConsumers?terminal26CurrentCaptureTupleSource(n,source):terminal26CompiledTupleSource(n,source)):undefined;"};
const ENTRY_SOURCE_PHASE_PATCHES=[
  {
    "label": "time-full-source-check-without-replacing-result-or-error",
    "before": "async function observeTerminal22Lifecycle(",
    "after": "export function measureTerminal26LifecycleSourceCheck(check,phase,timings){\n const started=Date.now();let completed=false;\n try{const proof=check();completed=true;return proof;}\n finally{timings.push({phase,status:completed?'COMPLETED':'FAILED',wallMs:Date.now()-started});}\n}\n\nasync function observeTerminal22Lifecycle("
  },
  {
    "label": "one-call-local-cost-list",
    "before": "const inputs=new Map();",
    "after": "const inputs=new Map(),sourceCheckTimings=[];"
  },
  {
    "label": "defer-complete-source-check-until-before-modes",
    "before": "sourceCheck=()=>verifyTerminal22LifecycleCapture(layout);sourceCheck();",
    "after": "sourceCheck=()=>verifyTerminal22LifecycleCapture(layout);"
  },
  {
    "label": "include-actual-source-check-phase-costs",
    "before": "modeResults:[],preflight:",
    "after": "modeResults:[],sourceCheckTimings,preflight:"
  },
  {
    "label": "retain-complete-source-check-before-any-native-launch",
    "before": "result.sourceBefore=sourceCheck();",
    "after": "result.sourceBefore=measureTerminal26LifecycleSourceCheck(sourceCheck,'before-modes',sourceCheckTimings);"
  },
  {
    "label": "retain-complete-source-check-after-modes",
    "before": "result.sourceAfter=sourceCheck();",
    "after": "result.sourceAfter=measureTerminal26LifecycleSourceCheck(sourceCheck,'after-modes',sourceCheckTimings);"
  }
];
const ENTRY_OWNED_TUPLE_PATCH={"before":"postCompileConsumers?terminal26CurrentCaptureTupleSource(n,source):terminal26CompiledTupleSource(n,source)","after":"postCompileConsumers?(postCompileConsumers.kind==='TERMINAL26_FIXED_OWNED_CPP_LIFECYCLE_SOURCE_V2'?postCompileConsumers.readOwnedTupleSource(n,source):terminal26CurrentCaptureTupleSource(n,source)):terminal26CompiledTupleSource(n,source)"};
const ENTRY_REPORT_METADATA_PATCH={"before":"kind:postCompileConsumers.kind,legacyConsumers:","after":"kind:postCompileConsumers.kind,...(postCompileConsumers.compiledSourceScope?{compiledSourceScope:postCompileConsumers.compiledSourceScope}:{}),sourceReadCosts:postCompileConsumers.sourceReadCosts,legacyConsumers:"};
const ENTRY_CONSUMER_SOURCE_PATCH={"before":"const sharedFile=path.join(moduleRoot,'r3-current-run.mjs');add({path:sharedFile,sha256:hash(fileURLToPath(new URL('./r3-current-run.mjs',import.meta.url)))});","after":"const sharedFile=path.join(moduleRoot,'r3-current-run.mjs'),localSharedFile=fileURLToPath(new URL('./r3-current-run.mjs',import.meta.url));\n    const pairing=postCompileConsumers?.kind==='TERMINAL26_FIXED_OWNED_CPP_LIFECYCLE_SOURCE_V2'?postCompileConsumers.compiledSourceScope?.consumerSourceSuccessorProof:null;\n    if(pairing){\n     assert(pairing.method==='EXACT_THREE_REPORT_CONSISTENCY_HUNKS_FULL_INVERSE'&&pairing.hunks===3&&pairing.wholeInverseByteEqual===true&&pairing.completeFailureMaskVerified===false&&pairing.nativeRuntimeAccepted===false&&lifecycleSame(pairing.compiledOriginal?.path,sharedFile)&&lifecycleSame(pairing.executingCurrent?.path,localSharedFile),'exact verified compiled/executing consumer pairing required');\n     add(pairing.compiledOriginal);add(pairing.executingCurrent);\n    }else add({path:sharedFile,sha256:hash(localSharedFile)});"};
const ENTRY_PREFIX="// Explicit current26 lifecycle consumer; reuses the public pipe observer's unchanged helpers.\nimport fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';\nimport {createHash} from 'node:crypto';import {fileURLToPath,pathToFileURL} from 'node:url';\nimport {terminal22LifecyclePreparationPins,validateTerminal22LifecycleLayout,verifyTerminal22LifecycleCapture,terminal22LifecycleManagedDllPin,validateTerminalLifecycleNativeReceipt,terminalPreparedRuntimeDirectories,terminal26CompiledSwitchSource,terminal26CompiledTupleSource,terminal22ExecutionFromProcess} from './r3-terminal22-qualified-observe.mjs';\nimport {currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode} from './r3-current-run.mjs';\nimport {runProcess as trustedLifecycleRunProcess} from '../runner.mjs';\nimport {validateManagedCompileRows as validateTrustedLifecycleManagedRows,requireTerminalManagedContract,terminalManagedStageStatus,terminalManagedContractFromDeclaration,verifyManagedDecoderCompileEvidence} from './r3-managed-input-contract.mjs';\nimport {isolateCurrentFixtureSettingsMutex} from '../../../fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs';\nimport {terminal26CurrentCaptureTupleSource} from './r3-terminal26-lifecycle-consumer-successor.mjs';\n";
const ENTRY_SUFFIX="\nexport async function observeTerminal26Lifecycle(args=process.argv.slice(2)){\n const options=parseTerminal22ObservationArguments(args);\n assert(options['--lifecycle-preparations']&&!options['--native-preparation'],'dedicated original lifecycle entry only');\n return observeTerminal22Lifecycle(options['--lifecycle-preparations'],options['--out'],options['--consumer-predecessors'],options['--lifecycle-budget-ms']===undefined?90000:Number(options['--lifecycle-budget-ms']));\n}\nif(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await observeTerminal26Lifecycle();\n";
const ENTRY_DIRECTED_PATCHES=[
  {
    "label": "exact-original-mode-selection",
    "before": "export function parseTerminal22ObservationArguments(args){",
    "after": "export function selectTerminal26LifecycleModes(selection){\n const plan=currentLifecycleModePlan();if(selection===undefined)return plan;\n assert(typeof selection==='string'&&selection.length>0,'nonempty exact lifecycle mode selection required');\n const requested=selection.split(','),known=new Set(plan.map(currentLifecycleModeLabel));\n assert(requested.every(label=>known.has(label)),'unknown lifecycle mode selection');\n assert(new Set(requested).size===requested.length,'duplicate lifecycle mode selection');\n const selected=new Set(requested);return plan.filter(test=>selected.has(currentLifecycleModeLabel(test)));\n}\n\nexport function parseTerminal22ObservationArguments(args){"
  },
  {
    "label": "strict-mode-selection-option",
    "before": "'--consumer-predecessors','--lifecycle-budget-ms'].includes(key)",
    "after": "'--consumer-predecessors','--lifecycle-budget-ms','--lifecycle-modes'].includes(key)"
  },
  {
    "label": "reject-native-or-unknown-mode-selection",
    "before": "if(options['--native-budget-ms']!==undefined)",
    "after": "if(options['--lifecycle-modes']!==undefined){assert(options['--lifecycle-preparations']&&!options['--native-preparation'],'lifecycle mode selection requires lifecycle preparations');selectTerminal26LifecycleModes(options['--lifecycle-modes']);}\n if(options['--native-budget-ms']!==undefined)"
  },
  {
    "label": "mode-selection-is-not-a-path",
    "before": "if(key!=='--lifecycle-budget-ms')assert(path.isAbsolute(value)",
    "after": "if(key!=='--lifecycle-budget-ms'&&key!=='--lifecycle-modes')assert(path.isAbsolute(value)"
  },
  {
    "label": "selected-finalizer-input",
    "before": "complete,failed=false,modeResults=[],groupBudgetMs=90000}){",
    "after": "complete,failed=false,modeResults=[],groupBudgetMs=90000,modeSelection}){"
  },
  {
    "label": "selected-finalizer-exact-plan",
    "before": "const deadlineExceeded=groupWallMs>groupBudgetMs,plan=currentLifecycleModePlan(),planComplete=",
    "after": "const deadlineExceeded=groupWallMs>groupBudgetMs,directed=modeSelection!==undefined,plan=selectTerminal26LifecycleModes(modeSelection),planComplete="
  },
  {
    "label": "diagnostic-status-never-original-parent-pass",
    "before": "return {status:accepted?(groupBudgetMs===90000?",
    "after": "return {status:accepted&&directed?'BOUNDED_SELECTED_LIFECYCLE_DIAGNOSTIC_MATCHED_ORIGINAL_PARENT_NOT_ACCEPTED':accepted?(groupBudgetMs===90000?"
  },
  {
    "label": "diagnostic-finalizer-scope",
    "before": "'INCOMPLETE',deadlineExceeded,finishedAt:",
    "after": "'INCOMPLETE',deadlineExceeded,...(directed?{diagnosticOnly:true,selectedModeLabels:plan.map(currentLifecycleModeLabel),originalPlanComplete:false}:{}),finishedAt:"
  },
  {
    "label": "selected-observer-input",
    "before": "async function observeTerminal22Lifecycle(inputFile,out,predecessorDeclarationFile,groupBudgetMs=90000){",
    "after": "async function observeTerminal22Lifecycle(inputFile,out,predecessorDeclarationFile,groupBudgetMs=90000,modeSelection){"
  },
  {
    "label": "selected-observer-original-plan",
    "before": "const groupStarted=Date.now();terminal22LifecycleGroupBudget(groupBudgetMs);",
    "after": "const groupStarted=Date.now();terminal22LifecycleGroupBudget(groupBudgetMs);\n const runPlan=selectTerminal26LifecycleModes(modeSelection),directed=modeSelection!==undefined;"
  },
  {
    "label": "one-invocation-register-each-input-once",
    "before": "  assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'exact actual lifecycle input pin required');lifecyclePlainPath(row.path);\n  const file=path.resolve(row.path);assert(fs.lstatSync(file).isFile(),'regular lifecycle input required');if(scope)assert(lifecycleInside(scope,file),'lifecycle input escaped original authority');\n  const key=file.toLowerCase(),previous=inputs.get(key);assert(!previous||previous.sha256===row.sha256,'conflicting lifecycle input pin');assert.equal(hash(file),row.sha256,'actual lifecycle input changed: '+file);inputs.set(key,{path:file,sha256:row.sha256});return file;",
    "after": "  assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'exact actual lifecycle input pin required');\n  const file=path.resolve(row.path);if(scope)assert(lifecycleInside(scope,file),'lifecycle input escaped original authority');\n  const key=file.toLowerCase(),previous=inputs.get(key);assert(!previous||previous.sha256===row.sha256,'conflicting lifecycle input pin');if(previous)return file;\n  lifecyclePlainPath(file);assert(fs.lstatSync(file).isFile(),'regular lifecycle input required');assert.equal(hash(file),row.sha256,'actual lifecycle input changed: '+file);inputs.set(key,{path:file,sha256:row.sha256});return file;"
  },
  {
    "label": "freshness-only-selected-modes",
    "before": "for(const test of plan.filter(row=>row.role===n.role)){",
    "after": "for(const test of runPlan.filter(row=>row.role===n.role)){"
  },
  {
    "label": "declare-selected-diagnostic-only",
    "before": "modeResults:[],sourceCheckTimings,preflight:",
    "after": "modeResults:[],...(directed?{diagnosticOnly:true,selectedModeLabels:runPlan.map(currentLifecycleModeLabel)}:{}),sourceCheckTimings,preflight:"
  },
  {
    "label": "selected-running-scope",
    "before": "result.status='RUNNING_ORIGINAL16_LIFECYCLE_MODES';",
    "after": "result.status=directed?'RUNNING_SELECTED_ORIGINAL_LIFECYCLE_DIAGNOSTIC':'RUNNING_ORIGINAL16_LIFECYCLE_MODES';"
  },
  {
    "label": "run-only-exact-selected-original-modes",
    "before": "for(const test of shared.currentLifecycleModePlan())",
    "after": "for(const test of runPlan)"
  },
  {
    "label": "selected-complete-not-original-parent",
    "before": "result.postInputsStable=true;result.originalPlanComplete=result.modeResults.length===plan.length;assert(result.originalPlanComplete,'all original16 modes required');",
    "after": "result.postInputsStable=true;result.selectedPlanComplete=result.modeResults.length===runPlan.length;result.originalPlanComplete=!directed&&result.selectedPlanComplete;assert(result.selectedPlanComplete,'all selected lifecycle modes required');"
  },
  {
    "label": "selected-final-deadline-preserved",
    "before": "complete:result.originalPlanComplete===true&&result.postInputsStable===true,failed:result.errors.length>0,modeResults:result.modeResults,groupBudgetMs}",
    "after": "complete:result.selectedPlanComplete===true&&result.postInputsStable===true,failed:result.errors.length>0,modeResults:result.modeResults,groupBudgetMs,modeSelection}"
  },
  {
    "label": "forward-selection-through-original-cli",
    "before": "options['--lifecycle-budget-ms']===undefined?90000:Number(options['--lifecycle-budget-ms']));",
    "after": "options['--lifecycle-budget-ms']===undefined?90000:Number(options['--lifecycle-budget-ms']),options['--lifecycle-modes']);"
  },
  {
    "label": "reuse-existing-switch-function-source-binding",
    "before": "import {currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode} from './r3-current-run.mjs';",
    "after": "import {currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode} from './r3-current-run.mjs';\nimport {terminal26SwitchFunction} from './r3-terminal26-switch-contract.mjs';"
  },
  {
    "label": "source-derived-negative-report-completeness-only",
    "before": "export function selectTerminal26LifecycleModes(selection){",
    "after": "const lifecycleNegativeNeed=(ok,code)=>{if(!ok)throw Error('NEGATIVE_TRACE_'+code);};\nfunction lifecycleNegativePaths(nodes,grammar){let variants=[[]];for(const node of nodes){let choices;if(Number.isInteger(node)){lifecycleNegativeNeed(typeof grammar.labels[node]==='string','KNOWN_LABEL_REQUIRED');choices=[[grammar.labels[node]]];}else{lifecycleNegativeNeed(node&&Object.keys(node).sort().join(',')==='else,fact,then'&&node.fact==='cleanupAwaiting'&&Array.isArray(node.then)&&Array.isArray(node.else),'UNKNOWN_SOURCE_GRAMMAR_NODE');choices=[...lifecycleNegativePaths(node.then,grammar),...lifecycleNegativePaths(node.else,grammar)];}variants=variants.flatMap(prefix=>choices.map(suffix=>[...prefix,...suffix]));lifecycleNegativeNeed(variants.length<=8,'SOURCE_GRAMMAR_ALTERNATIVE_LIMIT');}return variants;}\nexport function verifyTerminal26LifecycleNegativeShape(raw,grammar,switchSource){lifecycleNegativeNeed(grammar?.schemaVersion===1&&grammar.kind==='EXPLICIT26_SOURCE_DERIVED_SWITCH_ASSERTION_GRAMMAR_V1'&&grammar.managedInputContract==='current105-terminal26-sdk-lifetime-compact-v1'&&grammar.cases?.length===98,'EXACT_SOURCE_GRAMMAR_REQUIRED');lifecycleNegativeNeed(switchSource&&typeof switchSource.path==='string'&&path.isAbsolute(switchSource.path)&&path.basename(switchSource.path)==='fixture.cpp'&&Buffer.isBuffer(switchSource.bytes)&&createHash('sha256').update(switchSource.bytes).digest('hex')===switchSource.sha256,'EXACT_COMPILED_SOURCE_PIN_REQUIRED');const switchBody=terminal26SwitchFunction(switchSource.bytes),compiledSwitchSha256=createHash('sha256').update(switchBody).digest('hex');lifecycleNegativeNeed(switchBody.length===grammar.sourceBindings?.switchFunctionBytes&&compiledSwitchSha256===grammar.sourceBindings?.exactSwitchSourceSha256,'COMPILED_SOURCE_GRAMMAR_REQUIRED');lifecycleNegativeNeed(Array.isArray(raw?.cases)&&raw.cases.length===98,'EXACT98_CASES_REQUIRED');lifecycleNegativeNeed(Array.isArray(raw.assertionTrace)&&Number.isSafeInteger(raw.checks)&&raw.checks===raw.assertionTrace.length,'ACTUAL_TRACE_REQUIRED');const occurrences=new Map(),casePaths=[],byOrdinal=Array.from({length:98},()=>[]);raw.cases.forEach((c,i)=>{const expected=grammar.cases[i],occurrence=(occurrences.get(expected.name)??0)+1;occurrences.set(expected.name,occurrence);lifecycleNegativeNeed(expected.sourceOrdinal===i+1&&c?.sourceOrdinal===i+1&&c.name===expected.name&&c.nameOccurrence===occurrence,'EXACT_CASE_IDENTITY_REQUIRED');});raw.assertionTrace.forEach((row,i)=>{lifecycleNegativeNeed(row&&row.checkIndex===i+1&&Number.isSafeInteger(row.sourceOrdinal)&&row.sourceOrdinal>=1&&row.sourceOrdinal<=98&&typeof row.assertion==='string'&&typeof row.passed==='boolean','TRACE_ROW_REQUIRED');const c=raw.cases[row.sourceOrdinal-1];lifecycleNegativeNeed(row.case===c.name&&row.nameOccurrence===c.nameOccurrence,'TRACE_OWNER_REQUIRED');byOrdinal[row.sourceOrdinal-1].push(row);if(i)lifecycleNegativeNeed(row.sourceOrdinal>=raw.assertionTrace[i-1].sourceOrdinal,'SOURCE_CASE_ORDER_REQUIRED');});let acceptedChecks=0;for(let i=0;i<98;i++){const alternatives=lifecycleNegativePaths(grammar.cases[i].grammar,grammar),actual=byOrdinal[i].map(row=>row.assertion),matches=alternatives.filter(labels=>labels.length===actual.length&&labels.every((label,j)=>label===actual[j]));lifecycleNegativeNeed(matches.length>0,'EXACT_SOURCE_ASSERTION_LANGUAGE_REQUIRED:'+String(i+1));acceptedChecks+=actual.length;casePaths.push({sourceOrdinal:i+1,checks:actual.length,matchingSourcePaths:matches.length,branchTruthIndependentlyProven:false});}lifecycleNegativeNeed(acceptedChecks===raw.checks&&raw.checks>=grammar.checkBounds.min&&raw.checks<=grammar.checkBounds.max,'EXACT_SOURCE_TRACE_BOUNDS_REQUIRED');return{scope:'SOURCE_DERIVED_CASE_AND_ASSERTION_LANGUAGE_ONLY',compiledSwitchSha256,cases:98,checks:raw.checks,casePaths,completeFailureMaskVerified:false,sourceBranchTruthIndependentlyProven:false,runtimeAuthorityGranted:false};}\n\nexport function selectTerminal26LifecycleModes(selection){"
  },
  {
    "label": "bind-actual-negative-grammar-and-source-parser-inputs",
    "before": " lifecyclePlainPath(inputFile);const inputPin=pin(inputFile);",
    "after": " const negativeReportGrammarFile=fileURLToPath(new URL('./r3-terminal26-switch-expectations.json',import.meta.url));\n if(runPlan.some(test=>test.mutation?.startsWith('rollback-switch-'))){add(pin(negativeReportGrammarFile));add(pin(fileURLToPath(new URL('./r3-terminal26-switch-contract.mjs',import.meta.url))));}\n lifecyclePlainPath(inputFile);const inputPin=pin(inputFile);"
  },
  {
    "label": "negative-report-shape-before-original-semantic-check",
    "before": "const semantic=shared.verifyCurrentLifecycleMode(test,raw,",
    "after": "const negativeReportShape=declaredContract.signalEvidence&&test.mutation?.startsWith('rollback-switch-')?verifyTerminal26LifecycleNegativeShape(raw,read(negativeReportGrammarFile),terminal26CompiledSwitchSource(n,source)):undefined;\n   const semantic=shared.verifyCurrentLifecycleMode(test,raw,"
  },
  {
    "label": "record-only-limited-report-proof-not-full-mask",
    "before": "row.msiReturnObservation=semantic.msiReturnObservation;row.semanticAccepted=true;",
    "after": "row.msiReturnObservation=semantic.msiReturnObservation;row.negativeReportShape=negativeReportShape;row.semanticAccepted=true;"
  }
];
const ENTRY_SPANS=[["const lifecycleSame=","const trustedLifecyclePrimaryRoot="],["export function parseTerminal22ObservationArguments(","export function terminal22LifecyclePreparationPins("],["function terminal22LifecycleGroupBudget(","// Finite current26 paths are proved"],["async function observeTerminal22Lifecycle(","export async function observeTerminal22("]];
const SHARED_METADATA={
  "batch": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\BATCH-PREPARATION-r1.json",
    "bytes": 443629,
    "sha256": "2b7afdf0df8826e6222756246079db275a7ee9a8d77b481ef47fe457b004d222"
  },
  "execution": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\BATCH-EXECUTION-RESULT-r1.json",
    "bytes": 1225089,
    "sha256": "212e91a484fb5b088c594005acedaf790b0e8c81c26e1ff321405f970e392637"
  },
  "entry": {
    "path": "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-terminal22-native-prepare.mjs",
    "bytes": 27739,
    "sha256": "26bd627b19404e438bc969b4b8a364e6a3e175f747e634b1363218e0037a2c6d"
  },
  "runner": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\build-original-remaining-roles-r1.mjs",
    "bytes": 9281,
    "sha256": "90024de6e1dad3ea887978dfba95471f706ca46b6d3bdeb20023748f1c03c168"
  }
};
const OWNED_SHARED_METADATA={
  "batch": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\owned-runtime-batch-r9\\BATCH-PREPARATION-r10.json",
    "bytes": 458423,
    "sha256": "c7037445d8fd70fccaf0d9de3c15649e412990771c6b9ef9a846192e8eb9b3b2"
  },
  "execution": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\owned-runtime-batch-r9\\BATCH-EXECUTION-RESULT-r10.json",
    "bytes": 1264519,
    "sha256": "abe677cdf4b095cc56675849cf35946a83fc28da5716f89554c762057de39e54"
  },
  "entry": {
    "path": "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-terminal22-native-prepare.mjs",
    "bytes": 28198,
    "sha256": "686de1eebd2ee6db9ee9c81812f3e5028b9f4c69ee0f0ec78d254ee1f41c5ed1"
  },
  "runner": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\owned-runtime-batch-r9\\build-original-remaining-roles-r10.mjs",
    "bytes": 18715,
    "sha256": "0d3c5f91910865a6443f9fdbedb68a14ca658b505a61a8b072b246627d03adb2"
  },
  "executionStatus": "ORIGINAL_SEVEN_R10_RUNNER_R9_ROLES_COMPILED_WITH_SHARED_SOURCE_AUTHORITY",
  "runtimeReusePolicy": "owned-direct-child-v1"
};
const OWNED_LIFECYCLE_V2={
  "kind": "TERMINAL26_FIXED_OWNED_CPP_LIFECYCLE_PREDECESSORS_V2",
  "resultKind": "TERMINAL26_FIXED_OWNED_CPP_LIFECYCLE_SOURCE_V2",
  "cohortIntake": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\formal-owned-runtime-r9\\OWNED-COHORT-INTAKE-r9.json",
    "bytes": 9313,
    "sha256": "0f519bae0b8c7c7eb52561d45efcf8f6bba2cdf09babb9c72ebb3e5590dc6099"
  },
  "positiveCompilation": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\OWNED-RUNTIME-NATIVE-RESULT-r10.json",
    "bytes": 386856,
    "sha256": "0fd654a8a55a9f2fa8622e849e612e776951ad52330ce70b6fe55192d4419624"
  },
  "descriptor": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\formal-owned-runtime-r9\\generated-r13\\FIXED-SOURCE-DESCRIPTOR-owned-r13.json",
    "bytes": 17330112,
    "sha256": "f8e5b407c9e1e93fb21af6e7b4011779d7a69224e1ffb931c960a3da6f1bc793"
  },
  "expectations": {
    "path": "G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\shared-role-batch\\formal-owned-runtime-r9\\generated-r13\\SOURCE-EXPECTATIONS-current26-owned-r13.json",
    "bytes": 4509852,
    "sha256": "a1f2b703005d321230672a86d6039ae0eccb8771bd33ebec0ce96179ed230b1f"
  },
  "cohortId": "current26-owned-public-runtime-r9-tools-r10-cohort-np2-nb3",
  "tupleSpec": {
    "path": "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-terminal26-tuple-expectations.json",
    "bytes": 44357,
    "sha256": "3a8018f4c76c2ad70522b0cdeda1cf6f7e1fffd4aa561983968523a6edd0a185"
  }
};
const HISTORICAL_HOST_INPUT={
  "relative": "tools/ymcc_sim/global/host-process.ps1",
  "logicalPath": "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\host-process.ps1",
  "physicalPath": "G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\host-process.ps1",
  "bytes": 52633,
  "sha256": "7e714655f8a8ab2b7537165daff816a22238ddea250abe04a516718520cb0102"
};
const HISTORICAL_CONSUMER_INPUT={
  "relative": "tools/ymcc_sim/global/controller-coordinator-pipe/r3-current-run.mjs",
  "logicalPath": "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-current-run.mjs",
  "physicalPath": "G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-current-run.mjs",
  "bytes": 23097,
  "sha256": "c3bcb61a0558c21863240af39982eeb1e8b7482f836c2229c984a07da829950d"
};
const EXECUTING_CONSUMER_PIN={
  "path": "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-current-run.mjs",
  "bytes": 25125,
  "sha256": "731d393ad3ad24e7b38c55995094de08dcac644053ef74a6822a404fc1542a93"
};
const CONSUMER_REPORT_SUCCESSOR_HUNKS=[
  {
    "before": "export function verifyCurrentLifecycleMode(",
    "after": "// Validate the actual terminal26 check() report format; this is not a failure-mask oracle.\nfunction verifyTerminal26SwitchNegativeReport(raw){\n need(Array.isArray(raw.cases)&&raw.cases.length>0,'SWITCH_NEGATIVE_CASES_REQUIRED');\n const occurrences=new Map();\n raw.cases.forEach((row,index)=>{need(row&&typeof row.name==='string'&&row.name.length>0,'SWITCH_NEGATIVE_CASE_NAME_REQUIRED');const occurrence=(occurrences.get(row.name)??0)+1;occurrences.set(row.name,occurrence);need(row.sourceOrdinal===index+1&&row.nameOccurrence===occurrence,'SWITCH_NEGATIVE_CASE_OCCURRENCE_REQUIRED');});\n need(tupleInteger(raw.checks)&&raw.checks>0&&Array.isArray(raw.assertionTrace)&&raw.assertionTrace.length===raw.checks,'SWITCH_NEGATIVE_TRACE_REQUIRED');\n const expectedFailures=[];\n raw.assertionTrace.forEach((row,index)=>{need(row&&row.checkIndex===index+1&&typeof row.passed==='boolean'&&typeof row.assertion==='string'&&row.assertion.length>0&&Number.isSafeInteger(row.sourceOrdinal)&&row.sourceOrdinal>0,'SWITCH_NEGATIVE_TRACE_ROW_REQUIRED');const owner=raw.cases[row.sourceOrdinal-1];need(owner&&row.case===owner.name&&row.nameOccurrence===owner.nameOccurrence,'SWITCH_NEGATIVE_TRACE_OWNER_REQUIRED');if(!row.passed)expectedFailures.push(row);});\n const signature=row=>row&&[row.case,row.assertion,row.sourceOrdinal,row.nameOccurrence,row.checkIndex];\n need(Array.isArray(raw.failures)&&raw.failures.length===expectedFailures.length&&raw.failures.every((row,index)=>JSON.stringify(signature(row))===JSON.stringify(signature(expectedFailures[index]))),'SWITCH_NEGATIVE_FAILURE_TRACE_CONFLICT');\n return {scope:'ORIGINAL_NECESSARY_ASSERTION_AND_REPORT_CONSISTENCY_ONLY',reportConsistencyVerified:true,cases:raw.cases.length,checks:raw.checks,failures:expectedFailures.length,completeFailureMaskVerified:false};\n}\n\nexport function verifyCurrentLifecycleMode("
  },
  {
    "before": "   need(raw.forbiddenIo===0&&raw.fullTranslationUnit===true&&raw.normalProductMainExecuted===false&&raw.realDeviceIo===false,'OFFLINE_COMPLETE_NATIVE_MODE_REQUIRED');",
    "after": "   need(raw.forbiddenIo===0&&raw.fullTranslationUnit===true&&raw.normalProductMainExecuted===false&&raw.realDeviceIo===false,'OFFLINE_COMPLETE_NATIVE_MODE_REQUIRED');\n   const negativeSemanticEvidence=managedInputContract===tuple26Id&&test.mutation&&test.mode==='switch-recovery'?verifyTerminal26SwitchNegativeReport(raw):null;"
  },
  {
    "before": "return {originalSemanticChecksVerified:true,preparedContextGranted:false,runtimeAuthorityGranted:false,",
    "after": "return {originalSemanticChecksVerified:true,preparedContextGranted:false,runtimeAuthorityGranted:false,...(negativeSemanticEvidence?{negativeSemanticEvidence}:{}),"
  }
];
const CAPTURE_MANIFEST_SHA='03bc36a4c6546e3a1d0ba505aa12aa8cd2c22da0249291ef4b7b66f58897eee9';
// Fixed literals, never supplied by the caller or a serialized proof.
const OBSERVER_HUNKS=[
  {
    "label": "parser-allow-explicit-lifecycle-budget",
    "before": "['--native-preparation','--lifecycle-preparations','--out','--native-budget-ms','--consumer-predecessors']",
    "after": "['--native-preparation','--lifecycle-preparations','--out','--native-budget-ms','--consumer-predecessors','--lifecycle-budget-ms']"
  },
  {
    "label": "parser-lifecycle-budget-only",
    "before": " if(options['--native-budget-ms']!==undefined)assert(",
    "after": " if(options['--lifecycle-budget-ms']!==undefined){\n  assert(options['--lifecycle-preparations']&&!options['--native-preparation'],'lifecycle group budget requires lifecycle preparations');\n  assert(/^[1-9][0-9]{4,6}$/.test(options['--lifecycle-budget-ms'])&&Number.isSafeInteger(Number(options['--lifecycle-budget-ms']))&&Number(options['--lifecycle-budget-ms'])>=90000&&Number(options['--lifecycle-budget-ms'])<=1500000,'bounded integer lifecycle group budget required');\n }\n if(options['--native-budget-ms']!==undefined)assert("
  },
  {
    "label": "parser-dispatch-proof-by-existing-input",
    "before": "  assert(options['--native-preparation']&&!options['--lifecycle-preparations'],'consumer predecessor proof requires one native preparation');\n  assert(path.isAbsolute(options['--native-preparation'])&&!/[\\0\\r\\n]/.test(options['--native-preparation']),'absolute native preparation required for consumer predecessor proof');",
    "after": "  if(options['--native-preparation']){\n  assert(options['--native-preparation']&&!options['--lifecycle-preparations'],'consumer predecessor proof requires one native preparation');\n  assert(path.isAbsolute(options['--native-preparation'])&&!/[\\0\\r\\n]/.test(options['--native-preparation']),'absolute native preparation required for consumer predecessor proof');\n  }else assert(options['--lifecycle-preparations']&&path.isAbsolute(options['--lifecycle-preparations'])&&!/[\\0\\r\\n]/.test(options['--lifecycle-preparations']),'absolute lifecycle preparation required for lifecycle predecessor proof');"
  },
  {
    "label": "parser-budget-is-not-path",
    "before": " if(options['--lifecycle-preparations'])for(const value of Object.values(options))assert(",
    "after": " if(options['--lifecycle-preparations'])for(const [key,value] of Object.entries(options))if(key!=='--lifecycle-budget-ms')assert("
  },
  {
    "label": "bounded-group-budget-helper-and-finalizer-argument",
    "before": "export function finalizeTerminal22LifecycleExecution({started,ended=Date.now(),complete,failed=false,modeResults=[]}){",
    "after": "function terminal22LifecycleGroupBudget(groupBudgetMs){\n assert(Number.isSafeInteger(groupBudgetMs)&&groupBudgetMs>=90000&&groupBudgetMs<=1500000,'bounded integer lifecycle group budget required');return groupBudgetMs;\n}\nconst terminal22LifecycleBudgetScope=groupBudgetMs=>groupBudgetMs===90000?'ORIGINAL_GROUP90_ALL_PHASES':'EXPLICIT_LIFECYCLE_GROUP_BUDGET_ALL_PHASES';\nexport function terminal22LifecycleRemainingBudget(started,groupBudgetMs=90000,now=Date.now()){\n terminal22LifecycleGroupBudget(groupBudgetMs);\n if(groupBudgetMs===90000)return currentLifecycleRemainingBudget(started,now);\n const groupWallMs=now-started;assert(Number.isFinite(groupWallMs)&&groupWallMs>=0&&groupWallMs<groupBudgetMs,'effective lifecycle group budget exceeded');\n return {originalModeBudgetMs:45000,originalGroupBudgetMs:90000,effectiveGroupBudgetMs:groupBudgetMs,groupWallMs,remainingGroupMs:groupBudgetMs-groupWallMs,timeoutMs:Math.min(45000,groupBudgetMs-groupWallMs)};\n}\nexport function finalizeTerminal22LifecycleExecution({started,ended=Date.now(),complete,failed=false,modeResults=[],groupBudgetMs=90000}){\n terminal22LifecycleGroupBudget(groupBudgetMs);"
  },
  {
    "label": "finalizer-single-effective-deadline",
    "before": " const deadlineExceeded=groupWallMs>90000,plan=currentLifecycleModePlan()",
    "after": " const deadlineExceeded=groupWallMs>groupBudgetMs,plan=currentLifecycleModePlan()"
  },
  {
    "label": "finalizer-explicit-budget-status-and-scope",
    "before": " return {status:accepted?'BOUNDED_ORIGINAL16_LIFECYCLE_MATCHED_PUBLIC_RUNTIME_PENDING':'INCOMPLETE',deadlineExceeded,finishedAt:new Date(ended).toISOString(),",
    "after": " return {status:accepted?(groupBudgetMs===90000?'BOUNDED_ORIGINAL16_LIFECYCLE_MATCHED_PUBLIC_RUNTIME_PENDING':'BOUNDED_ORIGINAL16_LIFECYCLE_MATCHED_EXPLICIT_GROUP_BUDGET_PUBLIC_RUNTIME_PENDING'):'INCOMPLETE',deadlineExceeded,finishedAt:new Date(ended).toISOString(),\n  originalGroupBudgetMs:90000,effectiveGroupBudgetMs:groupBudgetMs,lifecycleBudgetScope:terminal22LifecycleBudgetScope(groupBudgetMs),withinOriginalGroupBudget:groupWallMs<=90000,"
  },
  {
    "label": "lifecycle-reader-arguments",
    "before": "async function observeTerminal22Lifecycle(inputFile,out){",
    "after": "async function observeTerminal22Lifecycle(inputFile,out,predecessorDeclarationFile,groupBudgetMs=90000){"
  },
  {
    "label": "validate-group-without-resetting-start",
    "before": " const groupStarted=Date.now();",
    "after": " const groupStarted=Date.now();terminal22LifecycleGroupBudget(groupBudgetMs);"
  },
  {
    "label": "read-proof-before-original-five-preflights",
    "before": " let common,shared,sourceCheck;\n for(const selectedPin of selected){",
    "after": " const postCompileConsumers=predecessorDeclarationFile===undefined?null:(await import('./r3-terminal26-lifecycle-consumer-successor.mjs')).terminal26LifecycleConsumerSuccessor(inputFile,path.resolve(predecessorDeclarationFile));\n if(postCompileConsumers){assert(declaredContract.signalEvidence===true&&declaredContract.count===26,'explicit26 lifecycle successor required');for(const row of postCompileConsumers.sourceInputs)add(row);}\n let common,shared,sourceCheck;\n for(const selectedPin of selected){"
  },
  {
    "label": "only-verified-managed-preparation-may-use-evidence",
    "before": "assert(lifecycleInside(layout.authorizedBuildRoot,nativeFile)&&lifecycleInside(layout.authorizedBuildRoot,preparationFile)&&lifecycleInside(layout.authorizedBuildRoot,layoutFile),'lifecycle preparations escaped authority');",
    "after": "assert(lifecycleInside(layout.authorizedBuildRoot,nativeFile)&&lifecycleInside(layout.authorizedBuildRoot,layoutFile)&&(lifecycleInside(layout.authorizedBuildRoot,preparationFile)||postCompileConsumers&&declaredContract.count===26&&lifecycleSame(preparationFile,postCompileConsumers.managedPreparation.path)&&n.preparation.sha256===postCompileConsumers.managedPreparation.sha256),'lifecycle preparations escaped authority');"
  },
  {
    "label": "only-fixed-observer-tool-row-mapped",
    "before": "for(const row of n.batchAfter.actualTools)add(row);if(n.runner)add(n.runner);",
    "after": "for(const row of postCompileConsumers?postCompileConsumers.currentToolsByRole[n.role]:n.batchAfter.actualTools)add(row);if(n.runner)add(n.runner);"
  },
  {
    "label": "record-effective-budget-and-both-epochs",
    "before": "originalGroupBudgetMs:90000,modeResults:[],preflight:",
    "after": "originalGroupBudgetMs:90000,effectiveGroupBudgetMs:groupBudgetMs,lifecycleBudgetScope:terminal22LifecycleBudgetScope(groupBudgetMs),postCompileConsumerSuccessor:postCompileConsumers?{kind:postCompileConsumers.kind,legacyConsumers:postCompileConsumers.legacyConsumers,currentConsumers:postCompileConsumers.currentConsumers,sourceProofs:postCompileConsumers.sourceProofs,compilerReceiptsUnmodified:true}:undefined,modeResults:[],preflight:"
  },
  {
    "label": "remaining-budget-retains-mode45",
    "before": "const budget=shared.currentLifecycleRemainingBudget(started);",
    "after": "const budget=terminal22LifecycleRemainingBudget(started,groupBudgetMs);"
  },
  {
    "label": "first-closing-deadline",
    "before": "assert(Date.now()-started<=90000,'original lifecycle group90 budget exceeded');",
    "after": "assert(Date.now()-started<=groupBudgetMs,'effective lifecycle group budget exceeded');"
  },
  {
    "label": "second-closing-deadline",
    "before": "assert(Date.now()-started<=90000,'original lifecycle group90 budget exceeded after input verification');",
    "after": "assert(Date.now()-started<=groupBudgetMs,'effective lifecycle group budget exceeded after input verification');"
  },
  {
    "label": "finalizer-uses-selected-budget",
    "before": "failed:result.errors.length>0,modeResults:result.modeResults});",
    "after": "failed:result.errors.length>0,modeResults:result.modeResults,groupBudgetMs});"
  },
  {
    "label": "record-final-original90-scope",
    "before": "result.status=final.status;result.execution=final.execution;result.finishedAt=final.finishedAt;",
    "after": "result.status=final.status;result.execution=final.execution;result.finishedAt=final.finishedAt;result.withinOriginalGroupBudget=final.withinOriginalGroupBudget;"
  },
  {
    "label": "deadline-error-matches-effective-scope",
    "before": "'original lifecycle final group90 budget exceeded'",
    "after": "'effective lifecycle final group budget exceeded'"
  },
  {
    "label": "dispatcher-only-lifecycle-arguments",
    "before": "return observeTerminal22Lifecycle(options['--lifecycle-preparations'],options['--out']);",
    "after": "return observeTerminal22Lifecycle(options['--lifecycle-preparations'],options['--out'],options['--consumer-predecessors'],options['--lifecycle-budget-ms']===undefined?90000:Number(options['--lifecycle-budget-ms']));"
  }
];

const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&key(a)===key(b);
const inside=(root,file)=>{const r=path.relative(root,file);return !!r&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
function exactKeys(value,names,label){
 assert(value&&typeof value==='object'&&!Array.isArray(value),label);
 assert.deepEqual(Object.keys(value).sort(),names.slice().sort(),label);
}
function plain(file,regular=true){
 assert(typeof file==='string'&&path.isAbsolute(file)&&!/[\0\r\n]/.test(file),'absolute plain lifecycle successor path required');
 // No missing ancestors or symbolic links/junctions; canonical path must stay identical.
 for(let cursor=path.resolve(file);;cursor=path.dirname(cursor)){
  const st=fs.lstatSync(cursor);assert(!st.isSymbolicLink(),'lifecycle successor reparse input rejected');
  assert(same(fs.realpathSync(cursor),cursor),'lifecycle successor redirected input rejected');
  if(path.dirname(cursor)===cursor)break;
 }
 if(regular)assert(fs.lstatSync(file).isFile(),'regular lifecycle successor file required');
 return path.resolve(file);
}
function replaceOnce(text,before,after,label){
 const at=text.indexOf(before);
 assert(at>=0&&text.indexOf(before,at+before.length)===-1,'unique fixed lifecycle patch span required: '+label);
 return text.slice(0,at)+after+text.slice(at+before.length);
}
function observerProof(before,publicCurrent,entry){
 assert.equal(before.length,OLD.bytes);assert.equal(digest(before),OLD.sha256);
 assert(publicCurrent.equals(before),'public pipe observer must remain exact predecessor bytes');
 let intermediate=before.toString('utf8');for(const h of OBSERVER_HUNKS)intermediate=replaceOnce(intermediate,h.before,h.after,h.label);
 assert.equal(Buffer.byteLength(intermediate),INTERMEDIATE.bytes);assert.equal(digest(intermediate),INTERMEDIATE.sha256);
 const piece=(start,end)=>{const a=intermediate.indexOf(start),b=intermediate.indexOf(end,a+start.length);assert(a>=0&&b>a&&intermediate.indexOf(start,a+start.length)===-1,'unique lifecycle source extraction required');return intermediate.slice(a,b);};
 const originalEntry=ENTRY_PREFIX+ENTRY_SPANS.map(([a,b])=>piece(a,b)).join('\n')+ENTRY_SUFFIX;
 let exactEntry=replaceOnce(originalEntry,ENTRY_CURRENT_TUPLE_PATCH.before,ENTRY_CURRENT_TUPLE_PATCH.after,'exact current tuple source dispatch');
 for(const h of ENTRY_SOURCE_PHASE_PATCHES)exactEntry=replaceOnce(exactEntry,h.before,h.after,h.label);
 exactEntry=replaceOnce(exactEntry,ENTRY_OWNED_TUPLE_PATCH.before,ENTRY_OWNED_TUPLE_PATCH.after,'exact owned tuple source dispatch');
 exactEntry=replaceOnce(exactEntry,ENTRY_REPORT_METADATA_PATCH.before,ENTRY_REPORT_METADATA_PATCH.after,'retain original source scope and measured read costs in report');
 exactEntry=replaceOnce(exactEntry,ENTRY_CONSUMER_SOURCE_PATCH.before,ENTRY_CONSUMER_SOURCE_PATCH.after,'separate compiled and executing report consumer source');
 for(const h of ENTRY_DIRECTED_PATCHES)exactEntry=replaceOnce(exactEntry,h.before,h.after,h.label);
 const expected=Buffer.from(exactEntry);
 assert.equal(entry.length,ENTRY_EXPECTED.bytes);assert.equal(digest(entry),ENTRY_EXPECTED.sha256);assert(expected.equals(entry),'standalone entry differs outside exact original lifecycle extraction');
 let inverse=intermediate;for(const h of OBSERVER_HUNKS.slice().reverse())inverse=replaceOnce(inverse,h.after,h.before,h.label);
 assert(Buffer.from(inverse).equals(before),'complete lifecycle intermediate inverse differs');
 return {method:'EXACT_ORIGINAL_LIFECYCLE_EXTRACTION_WITH_FIXED_IMPORTS_AND_BUDGET_PATCH',publicPipeObserverUnchanged:true,old:OLD,reviewedIntermediate:INTERMEDIATE,currentEntry:ENTRY_EXPECTED,wholeEntryBytesEqual:true,wholeIntermediateInverseByteEqual:true,sourceVerificationPhases:{completeCallsBefore:3,completeCallsAfter:2,required:['before-modes','after-modes'],removed:'redundant-preflight-complete-scan',nativeLaunchAfterCompleteBeforeCheck:true,requiredFullBeforeAndAfterChecksPreserved:true,sameInvocationInputRegistrationDeduplicated:true,directedModeSelection:'EXACT_ORIGINAL_LABELS_DIAGNOSTIC_ONLY',negativeReportCompleteness:'COMPILED_SOURCE_BOUND_CASE_AND_ASSERTION_LANGUAGE_ONLY',completeFailureMaskVerified:false},eolNormalization:'NONE',sourceSpans:ENTRY_SPANS,currentTupleSourceDispatch:{beforeSha256:digest(ENTRY_CURRENT_TUPLE_PATCH.before),afterSha256:digest(ENTRY_CURRENT_TUPLE_PATCH.after),singleExactReplacement:true}};
}
export function terminal26LifecycleConsumerSuccessor(lifecycleDeclarationFile,predecessorDeclarationFile){
 assert(same(PRIMARY,'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC'),'trusted current G project implementation required');
 const inputs=new Map(),currentToolsByRole=Object.create(null),nativeBindings=[],inputBytes=new Map();
 let closingRead=false,historicalHostEnabled=false,historicalConsumerEnabled=false,consumerSourceSuccessorProof=null;
 function sourceFile(file,expected){
  if(historicalHostEnabled&&same(file,HISTORICAL_HOST_INPUT.logicalPath)){
   assert(expected&&expected.sha256===HISTORICAL_HOST_INPUT.sha256&&(expected.bytes===undefined||expected.bytes===HISTORICAL_HOST_INPUT.bytes),'historical host request conflicts with original tool pin');
   return HISTORICAL_HOST_INPUT.physicalPath;
  }
  if(historicalConsumerEnabled&&same(file,HISTORICAL_CONSUMER_INPUT.logicalPath)){
   assert(expected,'selected consumer source requires an explicit original or executing pin');
   if(expected.sha256===HISTORICAL_CONSUMER_INPUT.sha256&&(expected.bytes===undefined||expected.bytes===HISTORICAL_CONSUMER_INPUT.bytes))return HISTORICAL_CONSUMER_INPUT.physicalPath;
   assert(expected.sha256===EXECUTING_CONSUMER_PIN.sha256&&expected.bytes===EXECUTING_CONSUMER_PIN.bytes,'consumer source request conflicts with fixed compiled/executing identities');
  }
  return file;
 }
 const sourceReadCosts={loadRequests:0,cacheHits:0,initialByteReads:0,initialBytesRead:0,closingByteReads:0,closingBytesRead:0,logicalBytesReturned:0};
 function load(file,expected,requiresBytes=true){
  sourceReadCosts.loadRequests++;file=plain(sourceFile(file,expected));const fileKey=key(file),cached=inputBytes.get(fileKey),bound=inputs.get(fileKey);
  if(!closingRead&&bound&&(!requiresBytes||cached)){
   if(expected){assert.equal(bound.sha256,expected.sha256,'lifecycle successor input SHA mismatch: '+file);if(expected.bytes!==undefined)assert.equal(bound.bytes,expected.bytes,'lifecycle successor input size mismatch: '+file);}
   sourceReadCosts.cacheHits++;sourceReadCosts.logicalBytesReturned+=bound.bytes;return requiresBytes?Buffer.from(cached):undefined;
  }
  const fd=fs.openSync(file,'r');let bytes;
  try{
   const a=fs.fstatSync(fd);bytes=fs.readFileSync(fd);const b=fs.fstatSync(fd);
   assert(a.isFile()&&b.isFile()&&a.ino===b.ino&&a.dev===b.dev&&a.size===b.size&&a.mtimeMs===b.mtimeMs&&a.ctimeMs===b.ctimeMs&&bytes.length===b.size,'lifecycle successor file changed while reading');
  }finally{fs.closeSync(fd);}
  if(closingRead){sourceReadCosts.closingByteReads++;sourceReadCosts.closingBytesRead+=bytes.length;}
  else{sourceReadCosts.initialByteReads++;sourceReadCosts.initialBytesRead+=bytes.length;}
  plain(file);const pin={path:file,bytes:bytes.length,sha256:digest(bytes)};
  if(expected){
   assert.equal(pin.sha256,expected.sha256,'lifecycle successor input SHA mismatch: '+file);
   if(expected.bytes!==undefined)assert.equal(pin.bytes,expected.bytes,'lifecycle successor input size mismatch: '+file);
  }
  const previous=inputs.get(fileKey);
  assert(!previous||previous.sha256===pin.sha256&&previous.bytes===pin.bytes,'lifecycle successor conflicting input pin');
  inputs.set(fileKey,pin);if(!closingRead&&requiresBytes)inputBytes.set(fileKey,Buffer.from(bytes));
  sourceReadCosts.logicalBytesReturned+=bytes.length;return requiresBytes?bytes:undefined;
 }
 const observe=(file,pin)=>load(file,pin,false);
 const read=(file,pin)=>JSON.parse(load(file,pin).toString('utf8').replace(/^\uFEFF/,''));
 function exactPin(row,label){
  exactKeys(row,['path','bytes','sha256'],label);
  assert(typeof row.path==='string'&&path.isAbsolute(row.path)&&!/[\0\r\n]/.test(row.path)&&Number.isSafeInteger(row.bytes)&&row.bytes>0&&/^[a-f0-9]{64}$/.test(row.sha256),label);
 }
 plain(PRIMARY,false);plain(CAPTURE,false);plain(BUILD,false);plain(EVIDENCE,false);
 assert(inside(EVIDENCE,plain(predecessorDeclarationFile)),'lifecycle predecessor proof outside trusted project evidence');
 const proof=read(predecessorDeclarationFile);
 const ownedProof=proof.kind===OWNED_LIFECYCLE_V2.kind;
 const useHistoricalHost=ownedProof&&Object.hasOwn(proof,'historicalHost');
 const useConsumerSourcePairing=ownedProof&&Object.hasOwn(proof,'consumerSourcePairing');
 const ownedFields=["cohortIntake","positiveCompilation","descriptor","expectations"];
 exactKeys(proof,['schemaVersion','kind','managedInputContract','nativeSourceProfile','lifecycleDeclaration','sourceCaptureManifest','managedPreparation','layout','managedManifest','capturedObserver',...(ownedProof?ownedFields:[]),...(useHistoricalHost?['historicalHost']:[]),...(useConsumerSourcePairing?['consumerSourcePairing']:[])],'strict lifecycle predecessor proof required');
 assert(proof.schemaVersion===1&&(proof.kind==='TERMINAL26_POST_COMPILE_LIFECYCLE_PREDECESSORS_V1'||ownedProof)&&proof.managedInputContract===CONTRACT&&proof.nativeSourceProfile===PROFILE,'unknown lifecycle predecessor proof or source contract');
 for(const name of ['lifecycleDeclaration','sourceCaptureManifest','managedPreparation','layout','managedManifest','capturedObserver'])exactPin(proof[name],'exact lifecycle proof pin required: '+name);
 if(ownedProof)for(const name of ownedFields){exactPin(proof[name],'exact V2 evidence pin required: '+name);assert(same(proof[name].path,OWNED_LIFECYCLE_V2[name].path)&&proof[name].bytes===OWNED_LIFECYCLE_V2[name].bytes&&proof[name].sha256===OWNED_LIFECYCLE_V2[name].sha256,'V2 evidence differs from fixed reviewed cohort: '+name);}
 assert(same(lifecycleDeclarationFile,proof.lifecycleDeclaration.path)&&inside(BUILD,lifecycleDeclarationFile),'proof must bind exact original lifecycle declaration');
 assert(same(proof.sourceCaptureManifest.path,path.join(CAPTURE,'.ymcc-source-snapshot/manifest.json'))&&proof.sourceCaptureManifest.sha256===CAPTURE_MANIFEST_SHA,'exact fixed source capture manifest required');
 assert(same(proof.capturedObserver.path,path.join(CAPTURE,RELATIVE,OBSERVER))&&proof.capturedObserver.bytes===OLD.bytes&&proof.capturedObserver.sha256===OLD.sha256,'exact captured predecessor observer required');
 assert(inside(BUILD,proof.layout.path)&&inside(BUILD,proof.managedManifest.path),'layout and managed manifest must remain inside original Buildroot');
 assert(inside(EVIDENCE,proof.managedPreparation.path)||inside(BUILD,proof.managedPreparation.path),'only managed preparation metadata may use trusted evidence');
 if(useHistoricalHost){
  exactPin(proof.historicalHost,'exact historical host pin required');
  assert(same(proof.historicalHost.path,HISTORICAL_HOST_INPUT.physicalPath)&&proof.historicalHost.bytes===HISTORICAL_HOST_INPUT.bytes&&proof.historicalHost.sha256===HISTORICAL_HOST_INPUT.sha256,'historical host differs from exact frozen original');
  const captured=read(proof.sourceCaptureManifest.path,proof.sourceCaptureManifest),members=captured.files.filter(row=>row.path===HISTORICAL_HOST_INPUT.relative);
  assert(captured.status==='PASS'&&same(captured.snapshotRoot,CAPTURE)&&members.length===1&&members[0].sha256===HISTORICAL_HOST_INPUT.sha256&&members[0].size===HISTORICAL_HOST_INPUT.bytes,'historical host is not the exact captured manifest member');
  historicalHostEnabled=true;observe(proof.historicalHost.path,proof.historicalHost);
 }
 if(useConsumerSourcePairing){
  const pairing=proof.consumerSourcePairing;exactKeys(pairing,['compiled','executing'],'exact consumer source pairing required');
  exactPin(pairing.compiled,'exact compiled consumer source pin required');exactPin(pairing.executing,'exact executing consumer source pin required');
  assert(same(pairing.compiled.path,HISTORICAL_CONSUMER_INPUT.physicalPath)&&pairing.compiled.bytes===HISTORICAL_CONSUMER_INPUT.bytes&&pairing.compiled.sha256===HISTORICAL_CONSUMER_INPUT.sha256,'compiled consumer differs from exact frozen input');
  assert(same(pairing.executing.path,EXECUTING_CONSUMER_PIN.path)&&pairing.executing.bytes===EXECUTING_CONSUMER_PIN.bytes&&pairing.executing.sha256===EXECUTING_CONSUMER_PIN.sha256,'executing consumer differs from fixed reviewed successor');
  const captured=read(proof.sourceCaptureManifest.path,proof.sourceCaptureManifest),members=captured.files.filter(row=>row.path===HISTORICAL_CONSUMER_INPUT.relative);
  assert(captured.status==='PASS'&&same(captured.snapshotRoot,CAPTURE)&&members.length===1&&members[0].sha256===HISTORICAL_CONSUMER_INPUT.sha256&&members[0].size===HISTORICAL_CONSUMER_INPUT.bytes,'compiled consumer is not the exact captured manifest member');
  const originalConsumer=load(pairing.compiled.path,pairing.compiled),executingConsumer=load(pairing.executing.path,pairing.executing);let inverse=executingConsumer.toString('utf8');
  for(const h of CONSUMER_REPORT_SUCCESSOR_HUNKS.slice().reverse())inverse=replaceOnce(inverse,h.after,h.before,'fixed report consistency consumer successor');
  assert(Buffer.from(inverse).equals(originalConsumer),'whole executing consumer inverse differs from original captured input');
  historicalConsumerEnabled=true;consumerSourceSuccessorProof={method:'EXACT_THREE_REPORT_CONSISTENCY_HUNKS_FULL_INVERSE',compiledOriginal:inputs.get(key(pairing.compiled.path)),executingCurrent:inputs.get(key(pairing.executing.path)),hunks:CONSUMER_REPORT_SUCCESSOR_HUNKS.length,wholeInverseByteEqual:true,completeFailureMaskVerified:false,nativeRuntimeAccepted:false};
 }
 const declaration=read(lifecycleDeclarationFile,proof.lifecycleDeclaration);
 exactKeys(declaration,['schemaVersion','kind','preparations'],'original lifecycle declaration shape required');
 assert(declaration.schemaVersion===1&&declaration.kind==='TERMINAL26_LIFECYCLE_COMPILED_INPUTS','explicit26 lifecycle declaration required');
 const plan=currentLifecycleModePlan(),roles=[...new Set(plan.map(row=>row.role))];
 assert.equal(plan.length,16,'original16 lifecycle plan required');assert.equal(roles.length,5,'original five native roles required');
 assert(Array.isArray(declaration.preparations)&&declaration.preparations.length===roles.length,'all original five lifecycle selections required');
 assert.deepEqual(declaration.preparations.map(row=>row?.role),roles,'exact ordered original five lifecycle roles required');
 let ownedData=null,ownedPositive=null;
 if(ownedProof){
  const intake=read(proof.cohortIntake.path,proof.cohortIntake),positive=read(proof.positiveCompilation.path,proof.positiveCompilation),descriptor=read(proof.descriptor.path,proof.descriptor),expectations=read(proof.expectations.path,proof.expectations);
  assert(intake.kind==='CURRENT26_OWNED_RUNTIME_COMPILED_COHORT_INTAKE_R9'&&intake.cohortId===OWNED_LIFECYCLE_V2.cohortId&&intake.runtimeReusePolicy==='owned-direct-child-v1','fixed owned cohort intake required');
  assert(positive.status==='CURRENT_NATIVE26_POSITIVE_COMPILED_NOT_RUNTIME_ACCEPTED'&&positive.newNativeBuildInvocations===1&&positive.newOriginalBusinessRuns===0&&positive.currentPrimaryAccepted===false,'fixed compiled-only positive receipt required');
  assert(descriptor.kind==='CURRENT26_FIXED_CAPTURE_OWNED_COMPILED_SOURCE_DESCRIPTOR_R13'&&expectations.kind==='CURRENT26_FIXED_CAPTURE_OWNED_SOURCE_EXPECTATIONS_R13','exact fixed compiled descriptor kinds required');
  for(const data of [descriptor,expectations])assert(data.id===PROFILE&&data.managedInputContract===CONTRACT&&data.cohortId===OWNED_LIFECYCLE_V2.cohortId&&data.runtimeReusePolicy==='owned-direct-child-v1'&&data.qualificationScope==='CPP_AND_USED_FIXTURE_SOURCE_ONLY'&&data.toolClosureComplete===false&&data.javascriptHostModuleClosureComplete===false&&data.cppCompilerInputClosureComplete===true&&data.usedFixtureSourceReconstructionComplete===true&&data.sourceReady===false&&data.runtimeQualified===false&&data.currentPrimaryAccepted===false&&data.installedRuntimeAccepted===false,'fixed CPP/used-fixture-only scope required');
  assert(same(descriptor.sourceRoot,CAPTURE)&&same(descriptor.consumerRepository,PRIMARY)&&descriptor.roles.length===8&&new Set(descriptor.roles.map(row=>row.role)).size===8,'fixed eight-role descriptor identity required');
  assert(descriptor.sourceExpectation.name===path.basename(proof.expectations.path)&&descriptor.sourceExpectation.bytes===proof.expectations.bytes&&descriptor.sourceExpectation.sha256===proof.expectations.sha256,'descriptor expectation source binding required');
  samePin(descriptor.captureManifest,proof.sourceCaptureManifest,'fixed descriptor capture mismatch');samePin(descriptor.layout,proof.layout,'fixed descriptor layout mismatch');samePin(descriptor.managedPreparation,proof.managedPreparation,'fixed descriptor managed preparation mismatch');samePin(descriptor.managedManifest,proof.managedManifest,'fixed descriptor managed manifest mismatch');
  assert(intake.roles.length===8&&new Set(intake.roles.map(row=>row.role)).size===8,'exact eight-role intake required');samePin(intake.batch,OWNED_SHARED_METADATA.batch,'owned intake batch mismatch');samePin(intake.execution,OWNED_SHARED_METADATA.execution,'owned intake execution mismatch');
  for(const selected of declaration.preparations){
   const matches=descriptor.roles.filter(row=>row.role===selected.role),received=intake.roles.filter(row=>row.role===selected.role);assert(matches.length===1&&received.length===1,'fixed owned lifecycle role missing');const row=matches[0];
   samePin(selected,row.nativePreparation,'selected lifecycle role differs from fixed compiled cohort');samePin(received[0].nativePreparation,row.nativePreparation,'intake native role mismatch');samePin(received[0].sourceManifest,row.sourceManifest,'intake source manifest mismatch');samePin(received[0].compilerLedger,row.compilerLedger,'intake compiler ledger mismatch');
   for(const name of ['sourceInputIds','artifactInputIds','compilerInputIds']){assert(Array.isArray(row[name])&&new Set(row[name]).size===row[name].length,'duplicate fixed owned input ID');for(const id of row[name]){const item=descriptor.inputs[id];exactKeys(item,['path','bytes','sha256'],'fixed owned input pin keys required');assert(typeof item.path==='string'&&path.isAbsolute(item.path)&&!/[\0\r\n]/.test(item.path)&&Number.isSafeInteger(item.bytes)&&item.bytes>=0&&/^[a-f0-9]{64}$/.test(item.sha256),'fixed owned input pin required');assert.equal(digest(JSON.stringify({path:path.resolve(item.path),bytes:item.bytes,sha256:item.sha256})),id,'fixed owned input ID digest differs');observe(item.path,item);}}
  }
  samePin(positive.preparation,descriptor.roles.find(row=>row.role==='positive').nativePreparation,'owned positive compilation metadata mismatch');
  ownedData={descriptor,expectations,intake};
 }
 const captureManifest=read(proof.sourceCaptureManifest.path,proof.sourceCaptureManifest);
 assert(captureManifest.schemaVersion===1&&captureManifest.status==='PASS'&&same(captureManifest.sourceRoot,PRIMARY)&&same(captureManifest.snapshotRoot,CAPTURE)&&Array.isArray(captureManifest.files),'actual complete capture manifest required');
 const observerRows=captureManifest.files.filter(row=>row.path===RELATIVE+OBSERVER);
 assert.equal(observerRows.length,1,'one captured observer manifest row required');
 assert(observerRows[0].sha256===OLD.sha256&&observerRows[0].size===OLD.bytes,'capture manifest predecessor pin differs');
 assert(!captureManifest.files.some(row=>path.basename(row.path).toLowerCase()===HELPER.toLowerCase()),'new helper unexpectedly belongs to old capture');
 const old=load(proof.capturedObserver.path,proof.capturedObserver);
 const currentFile=path.join(HERE,OBSERVER),current=load(currentFile,OLD),entryFile=path.join(HERE,ENTRY),entry=load(entryFile,ENTRY_EXPECTED);
 const sourceProof=observerProof(old,current,entry);
 const helperFile=fileURLToPath(import.meta.url),helper=load(helperFile),helperPin=inputs.get(key(helperFile));
 const forbiddenNames=new Set([OBSERVER.toLowerCase(),HELPER.toLowerCase(),ENTRY.toLowerCase()]);
 const forbiddenHashes=new Set([OLD.sha256,INTERMEDIATE.sha256,ENTRY_EXPECTED.sha256,digest(helper)]);
 function noConsumer(value,label){
  if(typeof value==='string')assert(!forbiddenNames.has(path.basename(value).toLowerCase())&&!forbiddenHashes.has(value.toLowerCase()),'post-compile observer/helper belongs to actual compiled inputs: '+label);
  else if(Array.isArray(value))for(const row of value)noConsumer(row,label);
  else if(value&&typeof value==='object')for(const row of Object.values(value))noConsumer(row,label);
 }
 function compiledArray(rows,label){assert(Array.isArray(rows),'actual compile array required: '+label);noConsumer(rows,label);}
 const preparation=read(proof.managedPreparation.path,proof.managedPreparation);
 assert(preparation.schemaVersion===1&&preparation.status==='MANAGED26_COMPILED_NOT_RUNTIME_ACCEPTED'&&preparation.managedInputContract===CONTRACT&&preparation.managedMemberCount===26&&same(preparation.sourceRoot,CAPTURE)&&preparation.publicRuntimeAccepted===false,'exact shared managed26 preparation required');
 assert(preparation.managed?.semanticMutation===null&&preparation.managed.sourceStable===true&&preparation.managed.toolchainStable===true,'unmutated stable managed preparation required');
 assert(same(preparation.layout?.path,proof.layout.path)&&preparation.layout.sha256===proof.layout.sha256,'managed layout proof differs');
 const layout=read(proof.layout.path,proof.layout);
 assert(layout.schemaVersion===1&&layout.managedInputContract===CONTRACT&&layout.sourceAuthority?.mode==='EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'&&same(layout.primaryRoot,PRIMARY)&&same(layout.sourceRoot,CAPTURE)&&same(layout.workspaceRoot,OWNER)&&same(layout.authorizedBuildRoot,BUILD)&&layout.manifestSha256===proof.sourceCaptureManifest.sha256,'fixed explicit26 current layout required');
 assert(same(preparation.managed.manifestPath,proof.managedManifest.path)&&preparation.managed.manifestSha256===proof.managedManifest.sha256,'managed manifest proof differs');
 const managed=read(proof.managedManifest.path,proof.managedManifest);
 assert(managed.status==='BUILT'&&managed.managedInputContract===CONTRACT&&managed.managedInputCount===26&&managed.terminalCleanupCompiled===true&&managed.semanticMutation===null&&managed.realSdkLoaded===false&&managed.hardwareIo===false,'actual unmutated managed26 build required');
 assert(Array.isArray(managed.inputBindings?.sources)&&managed.inputBindings.sources.length===26,'actual26 managed inputs required');
 assert.deepEqual(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),layout.expectedManagedSources,'managed source membership differs');
 for(const [label,rows] of [['managed.sources',managed.inputBindings.sources],['managed.generated',managed.inputBindings.generatedInputs],['managed.compiler',managed.evaluatedCompilerInputs]])compiledArray(rows,label);
 for(const row of managed.runtimeArtifacts){assert(inside(BUILD,row.path),'managed artifact escaped original Buildroot');observe(row.path,row);}
 assert(same(preparation.managed.fixtureDll,managed.fixtureDll)&&inside(BUILD,managed.fixtureDll),'shared actual managed DLL required');
 let sharedMetadataProof,sharedRuntimeEpoch;
 function samePin(a,b,label){assert(a&&b&&same(a.path,b.path)&&a.sha256===b.sha256&&(a.bytes===undefined||b.bytes===undefined||a.bytes===b.bytes),label);}
 function metadataRows(n,selected){
  const runtimeEpoch=n.runtimeReusePolicy??'ORIGINAL_FIXED_PATHS';
  assert(runtimeEpoch===(ownedProof?'owned-direct-child-v1':'ORIGINAL_FIXED_PATHS'),'proof kind does not authorize this runtime cohort');
  assert(['ORIGINAL_FIXED_PATHS','owned-direct-child-v1'].includes(runtimeEpoch),'unknown lifecycle runtime epoch');
  if(sharedRuntimeEpoch===undefined)sharedRuntimeEpoch=runtimeEpoch;else assert.equal(runtimeEpoch,sharedRuntimeEpoch,'mixed lifecycle runtime epochs rejected');
  const sharedMetadata=runtimeEpoch==='owned-direct-child-v1'?OWNED_SHARED_METADATA:SHARED_METADATA;
  if(Object.hasOwn(n,'fixtureToolImplementation')){
   assert(!Object.hasOwn(n,'sharedBatchFile')&&!Object.hasOwn(n,'sharedBatchClosed')&&!Object.hasOwn(n,'originalNativePreparationImplementation'),'mixed single/shared metadata rejected');
   assert(n.fixtureToolImplementation?.sourceCaptureModified===false&&same(n.fixtureToolImplementation.root,PRIMARY),'actual reviewed tool implementation root differs');
   assert(Array.isArray(n.fixtureToolImplementation.localReviewedToolFiles),'single preparation reviewed tools required');
   return {shape:'ORIGINAL_SINGLE_PREPARATION',rows:[n.batchAfter.actualTools,n.fixtureToolImplementation.localReviewedToolFiles]};
  }
  assert(n.sharedBatchClosed===true&&same(n.sharedBatchFile,sharedMetadata.batch.path),'strict closed original shared batch metadata required');
  exactPin(n.runner,'shared runner pin required');exactPin(n.originalNativePreparationImplementation,'original native implementation pin required');
  samePin(n.runner,sharedMetadata.runner,'shared runner differs from fixed original');
  samePin(n.originalNativePreparationImplementation,sharedMetadata.entry,'original native implementation differs from fixed original');
  assert(same(n.originalNativePreparationImplementation.path,path.join(HERE,'r3-terminal22-native-prepare.mjs')),'shared original implementation must be exact primary entry');
  for(const pinned of [n.runner,n.originalNativePreparationImplementation]){
   const matches=n.batchAfter.actualTools.filter(row=>same(row.path,pinned.path));
   assert.equal(matches.length,1,'shared runner/entry must be unique actual tool');
   samePin(matches[0],pinned,'shared runner/entry actual tool pin differs');observe(pinned.path,pinned);
  }
  if(!sharedMetadataProof){
   const b=read(sharedMetadata.batch.path,sharedMetadata.batch),e=read(sharedMetadata.execution.path,sharedMetadata.execution);
   assert(b.schemaVersion===1&&b.status==='ALL_ORIGINAL_REMAINING_NATIVE_ROLES_COMPILED_NOT_RUNTIME_ACCEPTED'&&b.sharedSourceBatch===true&&b.successfulNativeCompiles===7&&b.actualNativeBuildAttempts===7&&b.fullSourceChecksPlanned===2&&b.managedAdmissions===26&&b.newNativeRuntimeInvocations===0&&Array.isArray(b.errors)&&b.errors.length===0&&Array.isArray(b.unreachedRoles)&&b.unreachedRoles.length===0,'actual all-seven closed batch required');
   assert(same(b.sourceRoot,CAPTURE),'shared batch source capture differs');
   assert((b.runtimeReusePolicy??'ORIGINAL_FIXED_PATHS')===runtimeEpoch,'shared batch runtime epoch differs');
   samePin(b.layout,proof.layout,'shared batch layout differs');samePin(b.managedPreparation,proof.managedPreparation,'shared batch managed preparation differs');samePin(b.actualRunner,sharedMetadata.runner,'shared batch runner differs');
   observe(b.layout.path,b.layout);observe(b.managedPreparation.path,b.managedPreparation);observe(b.actualRunner.path,b.actualRunner);
   assert(Array.isArray(b.roles)&&Array.isArray(b.selectedRoles)&&b.roles.length===7&&b.selectedRoles.length===7&&new Set(b.roles.map(row=>row.role)).size===7,'exact complete shared producer roles required');
   assert.deepEqual(b.roles.map(row=>row.role),b.selectedRoles.map(row=>row.role),'shared batch actual producer selection differs');
   for(const row of b.roles){assert(row.status==='NATIVE26_COMPILED_NOT_RUNTIME_ACCEPTED'&&row.compilerStarted===true&&row.preparation&&same(row.path,row.preparation.path)&&inside(BUILD,row.path),'unfinished or escaped batch role');observe(row.preparation.path,row.preparation);}
   assert(e.status===(sharedMetadata.executionStatus??'ORIGINAL_SEVEN_ROLES_COMPILED_WITH_SHARED_SOURCE_AUTHORITY')&&e.successfullyCompiled===7&&e.attempts===7&&e.fullSourceVerificationCalls===2&&e.newNativeRuntimeInvocations===0&&Array.isArray(e.unreached)&&e.unreached.length===0,'actual complete shared execution receipt required');
   samePin(e.batch,sharedMetadata.batch,'execution does not bind exact closed batch');observe(e.batch.path,e.batch);
   const t=e.terminal,x=t?.rawExecution;
   assert(t&&t.exitCode===0&&t.signal===null&&t.error===null&&t.timedOut===false&&t.outputTruncated===false&&same(t.cwd,PRIMARY)&&Array.isArray(t.arguments)&&t.arguments.length===1&&same(t.arguments[0],b.actualRunner.path),'actual successful shared runner terminal required');
   assert(x&&x.collectionComplete===true&&x.cancelled===false&&x.ownerLost===false&&x.residualDescendants===false&&x.completion?.kind==='NATURAL'&&x.completion.forcedToolFinalization===false&&x.helperExitCode===0&&x.helperSignal===null,'actual complete natural raw execution required');
   const raw=read(x.terminal.path,x.terminal),request=read(x.request.path,x.request),args=read(x.arguments.path,x.arguments),plan=read(e.plan.path,e.plan);
   assert(raw.schemaVersion===1&&raw.status==='EXITED'&&raw.collectionComplete===true&&raw.businessSucceeded===true&&raw.businessExitCode===0&&raw.signal===null&&raw.timedOut===false&&raw.cancelled===false&&raw.ownerLost===false&&raw.residualDescendants===false&&raw.declaredPinsUnchanged===true&&raw.logsClosedAndFlushed===true&&raw.outputLimitExceeded===false&&raw.error===null&&raw.helperExitCode===0,'original raw terminal is not complete natural success');
   assert(raw.job?.zeroObservedBeforeDispose===true&&raw.job.activeProcessesFinal===0&&raw.job.disposed===true&&raw.job.cleanupError===null&&raw.job.terminateRequested===false,'original shared job close proof required');
   assert.deepEqual(x.job,raw.job,'shared outer/raw job differs');assert.deepEqual(x.actualBusiness,raw.business,'shared outer/raw actual business differs');
   assert(raw.business.pid===t.pid&&same(raw.business.cwd,PRIMARY)&&raw.business.atomicJobAdmission===true,'actual shared runner identity differs');
   samePin(raw.business.program,t.program,'raw shared process program differs');samePin(plan.program,t.program,'plan shared program differs');observe(t.program.path,t.program);
   assert.deepEqual(args,t.arguments,'original shared argv differs');assert.deepEqual(plan.arguments,t.arguments,'shared plan argv differs');
   samePin(raw.request,x.request,'raw request pin differs');samePin(raw.argumentsPin,x.arguments,'raw arguments pin differs');samePin(request.arguments,x.arguments,'request arguments pin differs');
   assert(Array.isArray(plan.sourceInputs)&&Array.isArray(plan.toolArtifacts)&&Array.isArray(request.sources)&&Array.isArray(request.tools),'complete planned/requested source graph required');
   assert.deepEqual(request.sources,plan.sourceInputs,'request source graph differs from pinned plan');assert.deepEqual(request.tools,plan.toolArtifacts,'request tool graph differs from pinned plan');
   assert.deepEqual(x.sourceBefore,raw.declaredInputsBefore,'outer source-before differs from raw');assert.deepEqual(x.sourceAfter,raw.declaredInputsAfter,'outer source-after differs from raw');
   const projectPin=row=>({path:key(row.path),bytes:row.bytes,sha256:row.sha256});
   const planned=[...plan.sourceInputs,...plan.toolArtifacts],before=raw.declaredInputsBefore,after=raw.declaredInputsAfter;
   assert(Array.isArray(before)&&Array.isArray(after)&&before.length===planned.length&&after.length===planned.length&&planned.length>0,'full raw source observation coverage required');
   assert.deepEqual(before.map(projectPin),planned.map(projectPin),'raw source-before differs from exact planned closure');assert.deepEqual(after.map(projectPin),planned.map(projectPin),'raw source-after differs from exact planned closure');
   before.forEach((row,index)=>assert.equal(row.role,index<plan.sourceInputs.length?'sources':'tools','original raw source role differs'));
   // The sole observer epoch is proved above. Every other original source/tool pin
   // remains exact and enters the ordinary input recheck closure.
   for(const row of planned){
    if(same(row.path,currentFile)){assert(row.sha256===OLD.sha256&&row.bytes===OLD.bytes,'raw observer source epoch differs');observe(currentFile,OLD);}
    else observe(row.path,row);
   }
   for(const row of b.batchAfter.actualTools){assert(before.some(pin=>same(pin.path,row.path)&&pin.sha256===row.sha256&&pin.bytes===row.bytes),'batch tool missing from complete raw source proof');}
   for(const stream of ['stdout','stderr']){
    const rawPin=raw.logs[stream],outerPin=t[stream],wrapped=x.logs[stream];assert(rawPin&&outerPin&&rawPin.sha256===outerPin.sha256&&(outerPin.bytes===undefined||rawPin.bytes===outerPin.bytes),'outer/raw log content differs');observe(rawPin.path,rawPin);observe(outerPin.path,outerPin);if(wrapped?.path)observe(wrapped.path,wrapped);
   }
   if(x.helperSource)observe(x.helperSource.path,x.helperSource);
   sharedMetadataProof={batch:b,execution:e,sourceCount:planned.length,executionPin:inputs.get(key(sharedMetadata.execution.path)),rawTerminal:inputs.get(key(x.terminal.path))};
  }
  const b=sharedMetadataProof.batch;
  assert.equal(JSON.stringify(b.batchAfter),JSON.stringify(n.batchAfter),'closed shared batchAfter must match native JSON exactly');
  samePin(n.toolchainManifest,b.toolchainManifest,'shared native toolchain differs');
  const rows=b.roles.filter(row=>row.role===n.role),selectedRows=b.selectedRoles.filter(row=>row.role===n.role);
  assert(rows.length===1&&selectedRows.length===1&&selectedRows[0].mutation===n.mutation&&same(selectedRows[0].directory,path.dirname(selected.path)),'shared native role/mutation/directory differs');
  samePin(rows[0].preparation,selected,'shared batch must pin exact selected native receipt');observe(selected.path,rows[0].preparation);
  return {shape:'ORIGINAL_CLOSED_SHARED_BATCH',rows:[n.batchAfter.actualTools],batch:sharedMetadata.batch,execution:sharedMetadataProof.executionPin,rawTerminal:sharedMetadataProof.rawTerminal,fullRawSourceCount:sharedMetadataProof.sourceCount};
 }
 for(const selected of declaration.preparations){
  exactKeys(selected,['role','path','sha256'],'exact original lifecycle native selection pin required');
  assert(path.isAbsolute(selected.path)&&!/[\0\r\n]/.test(selected.path)&&/^[a-f0-9]{64}$/.test(selected.sha256)&&inside(BUILD,selected.path)&&path.basename(selected.path)==='NATIVE-PREPARATION.json','selected native preparation escaped original authority');
  const n=read(selected.path,selected),original=structuredClone(n),rolePlan=plan.find(row=>row.role===selected.role);
  assert(n.schemaVersion===1&&n.status==='NATIVE26_COMPILED_NOT_RUNTIME_ACCEPTED'&&n.managedInputContract===CONTRACT&&n.managedAdmissions===26&&n.nativeSourceProfile===PROFILE&&n.generationPreflight?.nativeSourceProfile===PROFILE&&n.role===rolePlan.role&&n.mutation===rolePlan.mutation&&same(n.sourceRoot,CAPTURE),'exact current-profile lifecycle native role required');
  assert(n.nativeExecuted===false&&n.publicRuntimeAccepted===false&&n.normalProductMainExecuted===false&&n.realSdkLoaded===false&&n.deviceIo===false&&Array.isArray(n.errors)&&n.errors.length===0,'unexecuted native lifecycle preparation required');
  assert(same(n.preparation?.path,proof.managedPreparation.path)&&n.preparation.sha256===proof.managedPreparation.sha256,'mixed lifecycle managed preparation rejected');
  observe(n.preparation.path,n.preparation);
  assert(n.native?.status==='COMPILED_NOT_EXECUTED'&&n.native.nativeSourceProfile===PROFILE&&n.native.sourceFullTranslationUnit===true&&Array.isArray(n.native.changed)&&n.native.changed.length===0&&n.native.compileOnly===true&&n.native.compilerExitCode===0&&n.native.compilerSignal===null&&n.native.compilerError===null&&n.native.mutation===rolePlan.mutation&&n.native.runBranchReached===false&&n.native.fixtureExecuted===false&&n.native.nativeExecuted===false&&n.native.normalProductMainExecuted===false&&n.native.realDeviceIo===false,'actual successful compile-only lifecycle role required');
  assert(n.batchAfter?.status==='AUTHORITY_STABLE'&&n.batchAfter.fullSourceVerificationCalls===2&&n.batchAfter.source?.status==='FROZEN_SOURCE_VERIFIED'&&n.batchAfter.source.snapshotIntegrityVerified===true&&n.batchAfter.source.creationStatus==='PASS'&&same(n.batchAfter.source.snapshotRoot,CAPTURE)&&same(n.batchAfter.source.sourceRoot,PRIMARY)&&n.batchAfter.source.manifestSha256===proof.sourceCaptureManifest.sha256,'closed stable original source batch required');
  if(ownedProof){const row=ownedData.descriptor.roles.find(row=>row.role===n.role);samePin(selected,row.nativePreparation,'actual owned role metadata differs');samePin(n.native.sourceManifest,row.sourceManifest,'actual owned source manifest differs');samePin(n.compilerLedger,row.compilerLedger,'actual owned compiler ledger differs');samePin(n.native.exe,row.nativeExe,'actual owned executable differs');}
  const metadata=metadataRows(n,selected);
  const compileFile=path.join(path.dirname(selected.path),'native/COMPILE-ONLY.json');
  assert.deepEqual(read(compileFile),n.native,'actual compile receipt differs');
  for(const row of [n.native.exe,n.native.buildScript,n.native.sourceManifest,n.compilerLedger]){assert(row&&inside(BUILD,row.path),'compiled artifact escaped original Buildroot');observe(row.path,row);}
  assert(same(n.native.exe.path,path.join(path.dirname(selected.path),'native',path.basename(n.native.exe.path))),'native executable owner differs');
  assert(n.native.compilerProcess&&inside(BUILD,n.native.compilerProcess.path),'compiler process receipt escaped original Buildroot');
  const compilerProcess=read(n.native.compilerProcess.path,n.native.compilerProcess);assert(compilerProcess.exitCode===0,'actual successful compiler process required');
  const source=read(n.native.sourceManifest.path,n.native.sourceManifest),ledger=read(n.compilerLedger.path,n.compilerLedger);
  if(ownedProof&&n.role==='positive')ownedPositive={n:structuredClone(n),source:structuredClone(source)};
  assert(source.nativeSourceProfile===PROFILE,'mixed actual compiled source profile rejected');
  assert(ledger.schemaVersion===1&&ledger.status==='ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED'&&ledger.method==='ORIGINAL_MSVC_SHOWINCLUDES_AND_EXPLICIT_THREE_TU_RECIPE'&&Array.isArray(ledger.inputs)&&ledger.inputs.length===n.actualObservedCompilerInputs&&ledger.inputs.length>0,'actual native compiler ledger required');
  assert(same(ledger.sourceManifest.path,n.native.sourceManifest.path)&&ledger.sourceManifest.sha256===n.native.sourceManifest.sha256&&same(ledger.buildScript.path,n.native.buildScript.path)&&ledger.buildScript.sha256===n.native.buildScript.sha256,'compiler ledger binding differs');
  assert(Array.isArray(ledger.explicitTranslationUnits)&&ledger.explicitTranslationUnits.length===3,'original actual three TU recipe required');
  for(const row of [ledger.buildScript,ledger.buildLog]){assert(row&&inside(BUILD,row.path),'compiler ledger raw input escaped original Buildroot');observe(row.path,row);}
  for(const [label,rows] of [['source.records',source.records],['source.compiledGenerated',source.compiledGenerated],['source.observedIncludes',source.observedIncludePaths],['ledger.inputs',ledger.inputs],['ledger.includes',ledger.observedIncludePaths],['ledger.translationUnits',ledger.explicitTranslationUnits]])compiledArray(rows,selected.role+':'+label);
  assert(same(n.native.managedProof?.manifest,proof.managedManifest.path)&&n.native.managedProof.manifestSha256===proof.managedManifest.sha256&&same(n.native.managedProof.dll.path,managed.fixtureDll),'native/managed manifest or DLL binding differs');
  const dllRows=managed.runtimeArtifacts.filter(row=>same(row.path,managed.fixtureDll));
  assert(dllRows.length===1&&dllRows[0].sha256===n.native.managedProof.dll.sha256,'native must bind exact shared managed DLL');
  observe(n.native.managedProof.dll.path,n.native.managedProof.dll);
  for(const rows of metadata.rows){
   assert(Array.isArray(rows),'actual original lifecycle tool rows required');
   const matches=rows.filter(row=>same(row.path,currentFile));
   assert(matches.length===1&&matches[0].sha256===OLD.sha256&&matches[0].bytes===OLD.bytes,'each original lifecycle native must pin exact old observer');
   assert.equal(rows.filter(row=>path.basename(row.path).toLowerCase()===OBSERVER.toLowerCase()).length,1,'ambiguous original observer tool role');
   assert(!rows.some(row=>path.basename(row.path).toLowerCase()===HELPER.toLowerCase()||row.sha256===helperPin.sha256),'new helper unexpectedly belongs to old compile tool closure');
  }
  assert(new Set(n.batchAfter.actualTools.map(row=>key(row.path))).size===n.batchAfter.actualTools.length,'duplicate original batch tool paths rejected');
  const mapped=n.batchAfter.actualTools.map(row=>structuredClone(row));
  assert.deepEqual(mapped,n.batchAfter.actualTools,'all original compiled tool pins remain unchanged');
  // Every other actual tool is still read and checked, never skipped or supplied by a caller.
  for(const row of mapped)observe(row.path,row);
  assert.deepEqual(n,original,'native receipt mutated during lifecycle source proof');
  currentToolsByRole[selected.role]=mapped.map(row=>{const physical=sourceFile(row.path,row);return same(physical,row.path)?row:{...row,path:physical};});
  nativeBindings.push({role:selected.role,metadataShape:metadata.shape,sharedBatch:metadata.batch,sharedExecution:metadata.execution,sharedRawTerminal:metadata.rawTerminal,fullRawSourceCount:metadata.fullRawSourceCount,nativePreparation:inputs.get(key(selected.path)),sourceManifest:inputs.get(key(n.native.sourceManifest.path)),compilerLedger:inputs.get(key(n.compilerLedger.path)),actualCompilerInputs:ledger.inputs.length,compiledConsumersAbsent:true,observerOldPinVerified:true});
 }
 // Recheck this reader and all proof/raw/tool inputs at the end. The observer adds
 // these pins to its existing pre-run and post-run rehash closure.
 let readOwnedTupleSource;
 if(ownedProof){
  const spec=read(OWNED_LIFECYCLE_V2.tupleSpec.path,OWNED_LIFECYCLE_V2.tupleSpec);assert(ownedPositive,'actual owned positive tuple metadata required');
  const root=path.dirname(ownedPositive.n.native.exe.path),file=path.join(root,'tuple_recovery_cases.inc'),rows=ownedPositive.source.compiledGenerated.filter(row=>same(row.path,file));assert.equal(rows.length,1,'one actual owned tuple generated input required');assert.equal(rows[0].sha256,spec.sourceBindings.compiledTupleSha256,'owned tuple source differs from existing oracle');
  const expected={path:file,bytes:spec.sourceBindings.compiledTupleBytes,sha256:spec.sourceBindings.compiledTupleSha256};observe(file,expected);
  readOwnedTupleSource=(native,source)=>{assert.deepEqual(native,ownedPositive.n,'owned tuple native metadata mismatch');assert.deepEqual(source,ownedPositive.source,'owned tuple compiled source metadata mismatch');const bytes=load(file,expected);return {path:file,sha256:expected.sha256,bytes};};
 }
 const historicalToolBindings=[];
 if(historicalHostEnabled)historicalToolBindings.push({logicalPin:{path:HISTORICAL_HOST_INPUT.logicalPath,bytes:HISTORICAL_HOST_INPUT.bytes,sha256:HISTORICAL_HOST_INPUT.sha256},physicalPin:proof.historicalHost,captureManifest:proof.sourceCaptureManifest,scope:'EXACT_FROZEN_HISTORICAL_INPUT_ONLY',currentLogicalPathQualified:false,currentRuntimeGuardianQualified:false});
 if(historicalConsumerEnabled)historicalToolBindings.push({logicalPin:{path:HISTORICAL_CONSUMER_INPUT.logicalPath,bytes:HISTORICAL_CONSUMER_INPUT.bytes,sha256:HISTORICAL_CONSUMER_INPUT.sha256},physicalPin:proof.consumerSourcePairing.compiled,captureManifest:proof.sourceCaptureManifest,scope:'EXACT_FROZEN_COMPILED_CONSUMER_INPUT_ONLY',currentLogicalPathQualified:false,currentConsumerSourceSeparatelyBound:true,currentRuntimeGuardianQualified:false});
 closingRead=true;for(const pin of [...inputs.values()])observe(pin.path,pin);
 return {kind:ownedProof?OWNED_LIFECYCLE_V2.resultKind:'TERMINAL26_EXACT_POST_COMPILE_LIFECYCLE_CONSUMER_SUCCESSOR_V1',
  ...(ownedProof?{readOwnedTupleSource,compiledSourceScope:{qualificationScope:'CPP_AND_USED_FIXTURE_SOURCE_ONLY',fullToolClosure:false,cacheAcceptance:false,currentPrimaryAccepted:false,descriptor:proof.descriptor,expectations:proof.expectations,cohortIntake:proof.cohortIntake,...(historicalToolBindings.length?{historicalToolBindings}:{}),...(consumerSourceSuccessorProof?{consumerSourceSuccessorProof}:{})}}:{}),
  managedPreparation:inputs.get(key(proof.managedPreparation.path)),currentToolsByRole,sourceInputs:[...inputs.values()],
  legacyConsumers:[{name:OBSERVER,...inputs.get(key(proof.capturedObserver.path)),originalToolPath:currentFile}],
  currentConsumers:[{name:OBSERVER,...inputs.get(key(currentFile)),unchanged:true},{name:ENTRY,...inputs.get(key(entryFile))},{name:HELPER,...helperPin},...(historicalConsumerEnabled?[{name:path.basename(EXECUTING_CONSUMER_PIN.path),...inputs.get(key(EXECUTING_CONSUMER_PIN.path)),scope:'EXACT_REPORT_CONSISTENCY_SOURCE_SUCCESSOR_ONLY'}]:[])],
  sourceProofs:{observer:sourceProof,lifecycleDeclaration:inputs.get(key(lifecycleDeclarationFile)),sourceCaptureManifest:inputs.get(key(proof.sourceCaptureManifest.path)),layout:inputs.get(key(proof.layout.path)),managedPreparation:inputs.get(key(proof.managedPreparation.path)),managedManifest:inputs.get(key(proof.managedManifest.path)),nativeSourceProfile:PROFILE,managedInputContract:CONTRACT,nativeSelections:nativeBindings,allInputsRechecked:true,compilerReceiptsUnmodified:true,sourceCaptureModified:false,nativeRuntimeAccepted:false},
  sourceReadCosts:{...sourceReadCosts,retainedContentBytes:[...inputBytes.values()].reduce((sum,b)=>sum+b.length,0),hashOnlyReadsRetainNoContent:true,scope:'ONE_INVOCATION_SNAPSHOT_THEN_FRESH_COMPLETE_CLOSING',crossInvocationCache:false},
  wholeGoalComplete:false,runtimeAuthorityGranted:false};
}

// The original Evidence metadata is the same exact six-pin proof exception
// already consumed above. Reopen its immutable original pin; do not embed a copy.
function deepFreezeTupleBinding(value){if(value&&typeof value==='object'){for(const child of Object.values(value))deepFreezeTupleBinding(child);Object.freeze(value);}return value;}
export const TERMINAL26_CURRENT_CAPTURE_TUPLE_CONSTANTS=deepFreezeTupleBinding({"profileId":"current105-terminal26-current-capture-20261117a","managedContract":"current105-terminal26-sdk-lifetime-compact-v1","captureRoot":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Mainline\\YeManCC-source\\YeManCC","buildRoot":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3","pins":{"nativePreparation":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3\\NATIVE-PREPARATION.json","bytes":492096,"sha256":"1102e09239e58ba93d833cb9ddc7d30ad5b7cd282a2b51dcc854b07305b04066"},"sourceManifest":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3\\native\\source-manifest.json","bytes":1532470,"sha256":"d0e58fef5622c080fc3bcdc50568804a23a77728483e80c7319dbc037c27a7d2"},"compilerLedger":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3\\COMPILER-INPUTS.json","bytes":619429,"sha256":"7f17c15ed3f23527b15b3de1bf8eb82bd1fe0c6e6a88b660835d7b10ad6b4b63"},"exe":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3\\native\\CoordinatorFixture.exe","bytes":15750656,"sha256":"538bc11b00e3bb8054cae70d193296e1db2b8e0c15286df7ee7a44a5ccb3b70c"},"compileReceipt":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3\\native\\COMPILE-ONLY.json","bytes":12177,"sha256":"236ecdd07b7d0d315964157f3490959cb2d1ffe56cdbfca87c322c5d0bfb8e5d"},"buildScript":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3\\native\\build.cmd","bytes":3980,"sha256":"c58d522f62d5ece96a0c2d04a9f75410b893a7ff0250dc2ae45ce972829e7036"},"compilerProcess":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\native26-current-positive-r3\\native\\build-process.json","bytes":855,"sha256":"d91f2c0f0360c9bfa98205b56ee5fbe80cc94cc00a5d22cc4a2122f4331814e4"},"layout":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\LAYOUT-terminal26.json","bytes":5588,"sha256":"8230cedde97e4506b76e9ffc8f04095577cc3fd268baeaef3e80d085b145f27a"},"managedManifest":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\managed26-current-r1\\bin\\Release\\net10.0\\fixture-manifest.json","bytes":233004,"sha256":"a1577534779a01f71aef41ec53c347f758001353fe2c4ccc29bdd742b1990a40"},"managedDll":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Build\\Validation\\CurrentCoordinatorTerminal22\\managed26-current-r1\\bin\\Release\\net10.0\\Fixture.dll","bytes":508928,"sha256":"4d9dc94730def44e4f13a7cc7b8c63e98e616fed3d2a3885272b7212015ba170"},"captureManifest":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Mainline\\YeManCC-source\\YeManCC\\.ymcc-source-snapshot\\manifest.json","bytes":2384744,"sha256":"03bc36a4c6546e3a1d0ba505aa12aa8cd2c22da0249291ef4b7b66f58897eee9"},"nodeBinding":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Workspace\\R105CP-20261117A\\Mainline\\YeManCC-source\\YeManCC\\.ymcc-source-snapshot\\node-tools-binding.json","bytes":291056,"sha256":"51bd53eb3662cb68a7025e4dfef38bfc0ecd7051e8280d34bda9277e4802a908"},"profile":{"path":"G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-terminal26-compile-source-profile-current-20261117a.json","bytes":16442,"sha256":"66c18e7ebed3f4ae0bae8725daa4ee97e9ff04bde3162dc7b9bd56d57fca66fe"},"tupleSpec":{"path":"G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-terminal26-tuple-expectations.json","bytes":44357,"sha256":"3a8018f4c76c2ad70522b0cdeda1cf6f7e1fffd4aa561983968523a6edd0a185"},"extractor":{"path":"G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\fixtures\\legion_motion_consumer\\extract_main.mjs","bytes":20795,"sha256":"8ad6f96a549afc7d1ee6c4d2ef8c68ee853250444ed96fb9abaf36102de367c3"},"legacyObserver":{"path":"G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-terminal22-qualified-observe.mjs","bytes":57457,"sha256":"e04db556441df3448be8d66b1f8102bbba90b0f3f25f183758a35878c3c5620a"},"tupleConsumer":{"path":"G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\tools\\ymcc_sim\\global\\controller-coordinator-pipe\\r3-current-run.mjs","bytes":23097,"sha256":"c3bcb61a0558c21863240af39982eeb1e8b7482f836c2229c984a07da829950d"}},"historicalPreparationPin":{"path":"G:\\YeManCC-Work\\Testing\\YMCC\\Evidence\\BacklogAssist-20261102\\Runtime105CurrentMembership-20261114\\native26-routing\\lifecycle-runtime-layout\\real-store-original-consumer-20261116\\r4-intake-20261117\\current-primary\\MANAGED26-CURRENT-PREPARATION-r1.json","sha256":"ec742e5dff0d7a439c04eb35c29936a9c24d50a70366e6155b0afe9882b58118","bytes":192675},"counts":{"core":81,"records":739,"compilerInputs":1513,"captureFiles":11389},"tuple":{"relative":"tuple_recovery_cases.inc","bytes":63272,"sha256":"98a0f8f817c5c48399272b308fe79c3290b7b5c9531ff8b8f948914dfab99104"},"fixtureSha256":"bca9e9744c71b635906e02f5b9150e104660860b5c013727a97d3de4dc04c572","inertSha256":"fe973bca3281d0c8aa9ff342d924b96b3fc8ce092ab17097e4baf7e84a4a12fc"});
const currentTupleSourceReader=await(async()=>{
const F=TERMINAL26_CURRENT_CAPTURE_TUPLE_CONSTANTS,sha=b=>createHash('sha256').update(b).digest('hex'),key=p=>path.resolve(p).toLowerCase(),inside=(root,p)=>{const r=path.relative(root,p);return r!==''&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);},read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
function plain(file){assert(typeof file==='string'&&path.isAbsolute(file)&&!/[\0\r\n]/.test(file),'ABSOLUTE_PLAIN_PATH_REQUIRED');for(let p=path.resolve(file);;p=path.dirname(p)){const st=fs.lstatSync(p);assert(!st.isSymbolicLink()&&key(fs.realpathSync(p))===key(p),'PATH_REDIRECTION_REJECTED');if(path.dirname(p)===p)break;}assert(fs.lstatSync(file).isFile(),'REGULAR_FILE_REQUIRED');}
function exact(row,seen){assert(row&&/^[a-f0-9]{64}$/.test(row.sha256),'EXACT_PIN_REQUIRED');plain(row.path);const b=fs.readFileSync(row.path);assert.equal(sha(b),row.sha256,'FIXED_INPUT_CHANGED:'+row.path);if(row.bytes!==undefined)assert.equal(b.length,row.bytes,'FIXED_INPUT_SIZE_CHANGED');if(seen){const k=key(row.path),old=seen.get(k);assert(!old||old.sha256===row.sha256,'CONFLICTING_INPUT_PIN');seen.set(k,{path:path.resolve(row.path),sha256:row.sha256,bytes:b.length});}return b;}
exact(F.pins.extractor);const{extract}=await import(pathToFileURL(F.pins.extractor.path));
function terminal26CurrentCaptureTupleSource(n,source){
 assert(n?.nativeSourceProfile===F.profileId,'EXPLICIT_CURRENT_CAPTURE_TUPLE_PROFILE_REQUIRED');assert.equal(n.managedInputContract,F.managedContract);assert(n.role==='positive'&&n.mutation===null&&n.native?.mutation===null,'UNMUTATED_POSITIVE_TUPLE_REQUIRED');
 const seen=new Map();const actualN=JSON.parse(exact(F.pins.nativePreparation,seen).toString('utf8'));assert.deepEqual(n,actualN,'ACTUAL_NATIVE_PREPARATION_REQUIRED');const actualSource=JSON.parse(exact(F.pins.sourceManifest,seen).toString('utf8'));assert.deepEqual(source,actualSource,'ACTUAL_SOURCE_MANIFEST_REQUIRED');
 assert.equal(n.status,'NATIVE26_COMPILED_NOT_RUNTIME_ACCEPTED');assert(n.nativeExecuted===false&&n.publicRuntimeAccepted===false&&n.normalProductMainExecuted===false&&n.realSdkLoaded===false&&n.deviceIo===false&&n.errors.length===0,'COMPILE_ONLY_SCOPE_REQUIRED');assert.equal(n.native.status,'COMPILED_NOT_EXECUTED');assert(n.native.compileOnly===true&&n.native.compilerExitCode===0&&n.native.runBranchReached===false&&n.native.results.length===0);assert.equal(key(n.sourceRoot),key(F.captureRoot));
 for(const v of [n.productSourceSuccessor,n.productHeaderSuccessor,n.productPolicySuccessor,source.productSourceSuccessor,source.productHeaderSuccessor,source.productPolicySuccessor])assert(v==null,'PRIVATE_SUCCESSOR_MIX_REJECTED');assert.equal(source.nativeSourceProfile,F.profileId);assert.equal(n.native.nativeSourceProfile,F.profileId);assert.equal(n.generationPreflight.nativeSourceProfile,F.profileId);
 const metaBytes=exact(F.historicalPreparationPin,seen);assert.equal(sha(metaBytes),F.historicalPreparationPin.sha256);assert.equal(metaBytes.length,F.historicalPreparationPin.bytes);assert.deepEqual(n.preparation,F.historicalPreparationPin);const preparation=JSON.parse(metaBytes.toString('utf8'));assert.equal(preparation.status,'MANAGED26_COMPILED_NOT_RUNTIME_ACCEPTED');assert.equal(preparation.managedInputContract,F.managedContract);assert.equal(key(preparation.sourceRoot),key(F.captureRoot));
 for(const name of ['exe','compileReceipt','buildScript','compilerProcess','layout','managedManifest','managedDll','captureManifest','nodeBinding','profile','tupleSpec'])exact(F.pins[name],seen);
 assert.deepEqual(read(F.pins.compileReceipt.path),n.native,'ACTUAL_COMPILE_RECEIPT_REQUIRED');assert.equal(n.native.sourceManifest.sha256,F.pins.sourceManifest.sha256);assert.equal(key(n.native.sourceManifest.path),key(F.pins.sourceManifest.path));assert.equal(n.native.exe.sha256,F.pins.exe.sha256);assert.equal(key(n.native.exe.path),key(F.pins.exe.path));assert.equal(n.native.buildScript.sha256,F.pins.buildScript.sha256);assert.equal(n.native.compilerProcess.sha256,F.pins.compilerProcess.sha256);assert.equal(read(F.pins.compilerProcess.path).exitCode,0);
 assert.equal(preparation.layout.sha256,F.pins.layout.sha256);assert.equal(key(preparation.layout.path),key(F.pins.layout.path));assert.equal(preparation.managed.manifestSha256,F.pins.managedManifest.sha256);assert.equal(key(preparation.managed.manifestPath),key(F.pins.managedManifest.path));assert.equal(n.native.managedProof.manifestSha256,F.pins.managedManifest.sha256);assert.equal(key(n.native.managedProof.manifest),key(F.pins.managedManifest.path));assert.equal(n.native.managedProof.dll.sha256,F.pins.managedDll.sha256);assert.equal(key(n.native.managedProof.dll.path),key(F.pins.managedDll.path));
 const layout=read(F.pins.layout.path),managed=read(F.pins.managedManifest.path),capture=read(F.pins.captureManifest.path),profile=read(F.pins.profile.path),spec=read(F.pins.tupleSpec.path);assert.equal(layout.managedInputContract,F.managedContract);assert.equal(layout.manifestSha256,F.pins.captureManifest.sha256);assert.equal(key(layout.sourceRoot),key(F.captureRoot));assert.equal(capture.status,'PASS');assert.equal(capture.files.length,F.counts.captureFiles);assert.equal(key(capture.snapshotRoot),key(F.captureRoot));assert.equal(profile.id,F.profileId);assert.equal(profile.sourceCapture.manifest.sha256,F.pins.captureManifest.sha256);assert.equal(profile.sourceCapture.nodeToolsBinding.sha256,F.pins.nodeBinding.sha256);assert.equal(key(profile.sourceCapture.root),key(F.captureRoot));
 assert.equal(managed.status,'BUILT');assert.equal(managed.managedInputContract,F.managedContract);assert.equal(managed.managedInputCount,26);assert(managed.realSdkLoaded===false&&managed.hardwareIo===false&&managed.semanticMutation===null);assert.deepEqual(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),layout.expectedManagedSources);assert.equal(managed.inputBindings.sources.length,26);for(const r of managed.inputBindings.sources){assert(inside(F.captureRoot,r.path)&&inside(layout.authorizedBuildRoot,r.snapshot));exact({path:r.path,sha256:r.sha256},seen);exact({path:r.snapshot,sha256:r.sha256},seen);}for(const r of [...managed.inputBindings.generatedInputs,...managed.runtimeArtifacts]){assert(inside(layout.authorizedBuildRoot,r.path));exact(r,seen);}
 assert(n.batchAfter.status==='AUTHORITY_STABLE'&&n.batchAfter.fullSourceVerificationCalls===2&&n.batchAfter.source.snapshotIntegrityVerified===true&&n.batchAfter.source.creationStatus==='PASS');assert.equal(n.batchAfter.source.manifestSha256,F.pins.captureManifest.sha256);
 assert.equal(source.core.length,F.counts.core);assert.equal(source.records.length,F.counts.records);assert.equal(new Set(source.records.map(r=>r.path)).size,F.counts.records);const members=new Map(capture.files.map(r=>[r.path.replaceAll('\\','/'),r.sha256])),tools=new Map(n.batchAfter.actualTools.map(r=>[key(r.path),r.sha256]));
 for(const r of source.records){assert(inside(F.captureRoot,r.original)||r.inputRole==='actual-current-compile-tool'&&tools.get(key(r.original))===r.sha256,'UNBOUND_SOURCE_OR_TOOL');assert(inside(path.join(F.buildRoot,'native'),r.snapshot));if(inside(F.captureRoot,r.original)){assert.equal(key(r.original),key(path.join(F.captureRoot,r.path)));assert.equal(members.get(r.path),r.sha256,'CAPTURE_MEMBER_CHANGED');}exact({path:r.original,sha256:r.sha256},seen);exact({path:r.snapshot,sha256:r.sha256},seen);}
 for(const r of source.compiledGenerated){assert(inside(path.join(F.buildRoot,'native'),r.path));exact(r,seen);}const gen=relative=>{const file=path.join(F.buildRoot,'native',relative),rows=source.compiledGenerated.filter(r=>key(r.path)===key(file));assert.equal(rows.length,1,'UNIQUE_COMPILED_MEMBER_REQUIRED:'+relative);return rows[0];};
 const inertRow=gen('source/native/main.inert.cpp');assert.equal(inertRow.sha256,F.inertSha256);const inert=fs.readFileSync(inertRow.path,'utf8');for(const row of source.core){const record=row.source?source.records.find(r=>r.path===row.source):null;const body=extract(record?fs.readFileSync(record.snapshot,'utf8'):inert,row.symbol);assert.equal(body.sha256,row.executedSha256,'EXECUTED_CORE_CHANGED:'+row.symbol);assert(row.expectedSemanticNegative!==true);}
 for(const row of source.structCore){const record=row.source?source.records.find(r=>r.path===row.source):null;assert.equal(extract(record?fs.readFileSync(record.snapshot,'utf8'):inert,row.symbol,row.kind).sha256,row.executedSha256,'STRUCT_CORE_CHANGED');}for(const row of source.compilerHeaderBindings){const r=source.records.find(r=>r.path===row.source);assert(r&&r.sha256===row.sha256&&source.observedIncludePaths.some(p=>key(p)===key(r.snapshot)),'REQUIRED_COMPILER_HEADER_NOT_OBSERVED');}
 const fixture=gen('fixture.cpp'),pipe=gen('pipe_cases.inc'),tuple=gen(F.tuple.relative);assert.equal(fixture.sha256,F.fixtureSha256);assert.equal(fixture.sha256,n.generationPreflight.fixtureSha256);assert.equal(pipe.sha256,spec.sourceBindings.pipeSha256);assert.equal(tuple.sha256,F.tuple.sha256);assert.equal(tuple.sha256,spec.sourceBindings.compiledTupleSha256);assert.equal(spec.sourceBindings.compiledTupleBytes,F.tuple.bytes);
 const ledger=JSON.parse(exact(F.pins.compilerLedger,seen).toString('utf8'));assert.equal(n.compilerLedger.sha256,F.pins.compilerLedger.sha256);assert.equal(key(n.compilerLedger.path),key(F.pins.compilerLedger.path));assert.equal(ledger.status,'ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED');assert.equal(ledger.method,'ORIGINAL_MSVC_SHOWINCLUDES_AND_EXPLICIT_THREE_TU_RECIPE');assert.equal(ledger.inputs.length,F.counts.compilerInputs);assert.equal(n.actualObservedCompilerInputs,F.counts.compilerInputs);assert.equal(new Set(ledger.inputs.map(r=>key(r.path))).size,F.counts.compilerInputs);assert.equal(ledger.sourceManifest.sha256,F.pins.sourceManifest.sha256);assert.equal(ledger.buildScript.sha256,F.pins.buildScript.sha256);exact(ledger.buildLog,seen);
 const allowed=new Map([...n.batchAfter.actualTools,...source.records.map(r=>({path:r.snapshot,sha256:r.sha256})),...source.compiledGenerated,F.pins.buildScript].map(r=>[key(r.path),r.sha256]));const ids=new Map(ledger.inputs.map(r=>[key(r.path),r.sha256]));for(const r of ledger.inputs){assert.equal(allowed.get(key(r.path)),r.sha256,'UNBOUND_COMPILER_INPUT');exact(r,seen);}const tus=[fixture.path,...['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp'].map(p=>source.records.find(r=>r.path===p).snapshot)];assert.deepEqual(ledger.explicitTranslationUnits.map(key),tus.map(key));assert.deepEqual([...new Set(ledger.observedIncludePaths.map(key))].sort(),[...new Set(source.observedIncludePaths.map(key))].sort());const observedSet=new Set([...source.observedIncludePaths,...tus,F.pins.buildScript.path].map(key));assert.deepEqual([...observedSet].sort(),[...ids.keys()].sort(),'COMPLETE_OBSERVED_CLOSURE_REQUIRED');
 const bytes=fs.readFileSync(tuple.path);assert.equal(bytes.length,F.tuple.bytes);assert.equal(sha(bytes),F.tuple.sha256);for(const row of seen.values())exact(row);return {path:tuple.path,sha256:tuple.sha256,bytes};
}

return terminal26CurrentCaptureTupleSource;
})();
export function terminal26CurrentCaptureTupleSource(n,source){return currentTupleSourceReader(n,source);}
