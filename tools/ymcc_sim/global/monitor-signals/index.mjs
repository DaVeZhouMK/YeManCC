import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {runBoundedNode,assessRun} from './process.mjs';
import {runRunnerChecks} from './runner-checks.mjs';
import {CLOCK_BEFORE_ROOT,CLOCK_BEFORE_RELATIVE_FILES} from './adapter.mjs';
export {assessRun} from './process.mjs';
import {repoRoot,approvedProductFiles,tests,sourceHashes,sha256,readJson,assertCurrent,inside,auditRegistration} from './common.mjs';
const here=path.dirname(fileURLToPath(import.meta.url));
const ownFiles=['index.mjs','common.mjs','adapt.mjs','audit-runtime.mjs','worker.mjs','adapter.mjs','clock-baseline.json','review-lock.json','receipt-probe.mjs','process.mjs','runner-checks.mjs'];
export const schema='ymcc.monitor-signals.v1';
export async function executeWorker(configFile,{timeoutMs,env=process.env}={}){
 const cleanEnv={...env};for(const key of Object.keys(cleanEnv))if(/^YMCC_(MONITOR_|CLOCK_DEMAND_|BOUNDED_IDENTITY_)/.test(key))delete cleanEnv[key];
 return runBoundedNode(path.join(here,'worker.mjs'),[configFile],{cwd:repoRoot,timeoutMs,env:cleanEnv});
}
export function readClockBaseline(){
 let manifest;
 try{manifest=readJson(path.join(here,'clock-baseline.json'));}
 catch(error){throw Error((error.code==='ENOENT'?'CLOCK_BASELINE_MANIFEST_MISSING:':'CLOCK_BASELINE_MANIFEST_INVALID:')+error.message);}
 const names=manifest?.files&&typeof manifest.files==='object'?Object.keys(manifest.files):[];
 if(manifest?.schema!=='ymcc.monitor-clock-baseline.v1'||manifest.fixtureRoot!==CLOCK_BEFORE_ROOT||
  names.length!==CLOCK_BEFORE_RELATIVE_FILES.length||names.some(file=>!CLOCK_BEFORE_RELATIVE_FILES.includes(file))||
  names.some(file=>!/^[a-f0-9]{64}$/.test(manifest.files[file])))throw Error('CLOCK_BASELINE_MANIFEST_INVALID:reviewed file/hash contract');
 return manifest;
}
function stageBaseline(caseDir,requested){
 const manifest=readClockBaseline();
 const root=path.resolve(requested??path.join(repoRoot,manifest.fixtureRoot)),target=path.join(caseDir,'before');
 for(const [relative,pin]of Object.entries(manifest.files)){
  const source=path.join(root,relative);if(!fs.existsSync(source))throw Error('CLOCK_BASELINE_MISSING:'+relative);
  if(!inside(root,fs.realpathSync(source)))throw Error('CLOCK_BASELINE_PATH_ESCAPE');
  const bytes=fs.readFileSync(source);if(sha256(bytes)!==pin)throw Error('CLOCK_BASELINE_PIN_MISMATCH:'+relative);
  const dest=path.join(target,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes);
 }
 fs.writeFileSync(path.join(caseDir,'before-provenance.json'),JSON.stringify({...manifest,providedRoot:root,stagedRoot:target,sourceKind:'source-derived-fixture'},null,2));
 return {beforeRoot:target,beforeHashes:manifest.files};
}
export async function runMonitorSignals(options={}){
 if(!options.evidenceDir)throw Error('evidenceDir is required');
 const out=path.resolve(options.evidenceDir);if(inside(repoRoot,out))throw Error('EVIDENCE_MUST_BE_OUTSIDE_SOURCE_CHECKOUT');
 fs.mkdirSync(out,{recursive:true});if(fs.existsSync(path.join(out,'result.json')))throw Error('FRESH_EVIDENCE_DIRECTORY_REQUIRED');
 const selected=options.test?tests.filter(t=>t.id===options.test):tests;
 if(selected.length===0)throw Error('UNREVIEWED_TEST_SELECTION');
 let pins,review,inputs;
 try{
  readClockBaseline();
  const fixtureInputs=CLOCK_BEFORE_RELATIVE_FILES.map(file=>CLOCK_BEFORE_ROOT+'/'+file);
  inputs=[...approvedProductFiles,...tests.map(t=>t.source),...fixtureInputs,...ownFiles.map(name=>path.relative(repoRoot,path.join(here,name)).replaceAll('\\','/'))];
  pins=sourceHashes(inputs);review=readJson(path.join(here,'review-lock.json'));
 }catch(error){
  const failure={code:error.message.split(':')[0],message:error.message};
  const result={schema,ok:false,status:'BLOCKED',reason:'MONITOR_REQUIRED_INPUT_UNAVAILABLE',sourceChanges:[],sourceHashes:{},executedProductModules:[],
   inputConsumerStateFeedback:[],cases:selected.map(spec=>({id:spec.id,status:'BLOCKED',failure,executedProductModules:[],execution:null})),
   compatibility:{baselineInputs:'BLOCKED',failure},remainingGaps:[{id:'required-fixture-input',status:'BLOCKED',detail:error.message}],
   hardwareIo:false,nativeAppStarted:false,realHostStarted:false,physicalMonitoringVerified:false};
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;
 }
 const registration=await auditRegistration();
 fs.writeFileSync(path.join(out,'registration-audit.json'),JSON.stringify(registration,null,2));
 const runnerChecks=await runRunnerChecks(path.join(out,'runner-checks'));
 const cases=[];
 for(const spec of selected){
  const caseDir=path.join(out,spec.id);fs.mkdirSync(caseDir,{recursive:true});
  let launched=null,receipt=null,failure=null;
  try{
   if(registration.targets.find(target=>target.source===spec.source).directAdapterIds.length){
    cases.push({id:spec.id,status:'ALREADY_DIRECTLY_REGISTERED',reason:'Original assertions have a direct adapter. Do not duplicate execution.',
      adapterIds:registration.targets.find(target=>target.source===spec.source).directAdapterIds});continue;
   }
   if(pins[spec.source]!==review.tests[spec.source])throw Error('ORIGINAL_TEST_CHANGED_REVIEW_REQUIRED:'+spec.source);
   const config={schema,id:spec.id,caseDir,evidenceRoot:out,repoRoot,currentHashes:sourceHashes(),testSha256:pins[spec.source],
    ...(spec.id==='clock-demand'?stageBaseline(caseDir,options.clockBeforeRoot):{})};
   const file=path.join(caseDir,'worker-config.json');fs.writeFileSync(file,JSON.stringify(config,null,2));
   launched=await executeWorker(file,{timeoutMs:spec.timeoutMs});
   fs.writeFileSync(path.join(caseDir,'stdout.log'),launched.stdout);fs.writeFileSync(path.join(caseDir,'stderr.log'),launched.stderr);
   const receiptFile=path.join(caseDir,'case-result.json');if(fs.existsSync(receiptFile))receipt=readJson(receiptFile);
   const assessed=assessRun({...launched,receipt,expectedSourceHashes:pins,currentSourceHashes:sourceHashes(inputs)});
   cases.push({id:spec.id,...assessed,execution:launched,receipt:receiptFile,executedProductModules:receipt?.executedProductModules??[],
     executionBoundaries:receipt?.executionBoundaries??[],compiledProductSources:receipt?.compiledProductSources??[],inputConsumerStateFeedback:receipt?.inputConsumerStateFeedback??[],originalReceipt:receipt?.originalReceipt,error:receipt?.error,assertionContract:receipt?.originalTest,adaptations:receipt?.adaptations??[]});
  }catch(error){failure={code:error.message.split(':')[0],message:error.message,differences:error.differences};cases.push({id:spec.id,status:'BLOCKED',failure,execution:launched});}
 }
 const current=sourceHashes(inputs),drift=Object.keys(pins).filter(file=>pins[file]!==current[file]);
 const status=cases.every(item=>item.status==='PASS')&&drift.length===0?'PASS':cases.some(item=>item.status==='FAIL')||drift.length?'FAIL':'BLOCKED';
 const executed=[...new Set(cases.flatMap(item=>item.executedProductModules??[]))];
 const result={schema,ok:status==='PASS',status,scope:'actual retained consumer/clock-demand/identity regressions with injected ports; not complete monitor or physical acceptance',
 sourceChanges:[],sourceHashes:pins,executedProductModules:executed,executionMeaning:'Only modules with named function calls in executionBoundaries; compiled dependencies and referenced ports do not imply full behavior coverage.',cases,
 inputConsumerStateFeedback:cases.flatMap(item=>item.inputConsumerStateFeedback??[]),registrationAudit:registration,
 runnerChecks,compatibility:{reviewedTestHashPins:review.tests,clockBaseline:'exact retained source file pins; source-derived fixture, not a hardware receipt',sourceDrift:drift,
  newModulePolicy:'unreviewed .ts source imports are rejected before code execution; new external ports need explicit review',allOriginalAssertionsPreserved:cases.every(item=>item.assertionContract?.allOriginalAssertionsPreserved===true)},
 remainingGaps:[
  {id:'complete-monitor-path',status:'NOT_EXECUTED',detail:'Actual monitor consumers run; real sensor/native publisher, renderer/dragging/full display integration are not exercised by these scripts.'},
  {id:'native-runtime-protocol',status:'NOT_EXECUTED',detail:'Only injected monitor.start/stop/snapshot/takeover receipts; no App/Host, installed runtime or actual native receipt.'},
  {id:'measured-performance',status:'NOT_EXECUTED',detail:'Clock formatting/demand counts and virtual timer bounds are behavioral evidence, not measured CPU/FPS/latency.'},
  {id:'historical-clock-snapshot',status:'SOURCE_FIXTURE_ONLY',detail:'Pinned retained source demonstrates old/current projection difference; never archived device data or a past hardware acceptance.'},
 ],hardwareIo:false,nativeAppStarted:false,realHostStarted:false,physicalMonitoringVerified:false};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));
 return result;
}
function parse(args){const options={};const allowed=new Set(['evidence-dir','clock-before-root','test']);for(let i=0;i<args.length;i++){
 const name=args[i].slice(2);if(!args[i].startsWith('--')||!allowed.has(name)||!args[i+1]||args[i+1].startsWith('--'))throw Error('INVALID_ARGUMENT:'+args[i]);
 const key=name.replace(/-([a-z])/g,(_,c)=>c.toUpperCase());if(options[key])throw Error('DUPLICATE_ARGUMENT:'+name);options[key]=args[++i];}return options;}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const result=await runMonitorSignals(parse(process.argv.slice(2)));console.log(JSON.stringify({schema,status:result.status,
  cases:result.cases.map(c=>({id:c.id,status:c.status,steps:c.inputConsumerStateFeedback?.length??0,error:c.error?.message??c.failure?.message})),
  steps:result.inputConsumerStateFeedback.length,hardwareIo:false}));process.exitCode=result.status==='PASS'?0:result.status==='BLOCKED'?10:1;
 }catch(error){console.error(error.stack);process.exitCode=1;}
}
