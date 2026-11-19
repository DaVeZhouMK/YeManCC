import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {CASE_ID,REPO,INPUTS,INFRASTRUCTURE,lifecycleCompletionTools} from './adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const writeFresh=(file,bytes)=>{const fd=fs.openSync(file,'wx');try{fs.writeFileSync(fd,bytes);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}};
const write=(file,value)=>writeFresh(file,JSON.stringify(value,null,2)+'\n');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
function inside(root,file){const rel=path.relative(root,file);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));}
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'OUTPUT_LINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
const ORIGINAL_LIFECYCLE_CASE_NAMES=Object.freeze(['ordinary-owned-work','actual-runid-copy-allocation-failure','dequeued-observation-failure','recovery-observer-enter-is-optional','recovery-observer-leave-is-optional','post-dispatch-failure-does-not-replay-device-work','business-failure-keeps-existing-no-replay-policy','suppression-observation-failure']);
export const LIFECYCLE_FAULTS=[
   {id:'claim-guard-omitted',caseName:'actual-runid-copy-allocation-failure',pattern:/    ymcc::inputHost::LifecycleClaimGuard completion\(g_inputHostLifecycleWorkRequested,\r?\n        g_inputHostLifecycleWorkInFlight\);/,replacement:'    /* negative control: omit completion guard */',expectedFailures:['failed pre-dequeue snapshot releases the lifecycle claim','pending physical return remains scheduled','next owner pass completes original cleanup after transient memory failure'],
    // A stranded claim also prevents later passes in the default eight-case matrix.
    additionalFailures:{
     'ordinary-owned-work':['normal work completes its claim'],
     'dequeued-observation-failure':['dequeue diagnostic releases claim','original cleanup actually dispatches on next pass','later work remains ordered'],
     'recovery-observer-enter-is-optional':['observer failure releases claim'],
     'recovery-observer-leave-is-optional':['leave failure preserves completed operation and balanced claim'],
     'post-dispatch-failure-does-not-replay-device-work':['remaining cleanup can acquire next claim','cleanup still executes after observer failure'],
     'business-failure-keeps-existing-no-replay-policy':['remaining lifecycle cleanup runs'],
     'suppression-observation-failure':['suppression cannot strand queue claim','external-priority suppression never blocks cleanup']
    }},
   {id:'unattempted-cleanup-retention-omitted',caseName:'dequeued-observation-failure',pattern:/retainUnattemptedWork\(\);/g,replacement:'(void)retainUnattemptedWork;',expectedFailures:['unattempted cleanup returns ahead of rearm','original cleanup actually dispatches on next pass','later work remains ordered'],
    additionalFailures:{'suppression-observation-failure':['recovered external-priority request stays suppressed']}}
  ];
export function lifecycleCompletionRunSelection(options={}){
 const configs=options.configurations===undefined?['Release','Checked']:options.configurations;
 need(Array.isArray(configs)&&configs.length>0&&configs.every(c=>['Release','Checked'].includes(c))&&new Set(configs).size===configs.length,'INVALID_CONFIGURATION_SELECTION');
 const phases=options.phase===undefined?['positive',...LIFECYCLE_FAULTS.map(f=>f.id)]:[options.phase];
 need(phases.every(p=>p==='positive'||LIFECYCLE_FAULTS.some(f=>f.id===p)),'UNKNOWN_LIFECYCLE_PHASE');
 return {configurations:[...configs],phases,directed:options.phase!==undefined||options.configurations!==undefined};
}
export function validateLifecycleConfiguration(c,{fault,name='receipt',caseNames=ORIGINAL_LIFECYCLE_CASE_NAMES}={}){
 need(Array.isArray(caseNames)&&caseNames.length>0&&caseNames.every(n=>ORIGINAL_LIFECYCLE_CASE_NAMES.includes(n))&&new Set(caseNames).size===caseNames.length,'UNREVIEWED_SELECTED_CASES:'+name);
 need(c.results.length===caseNames.length&&c.results.every(r=>r.checks>0&&r.cases.length===1&&[0,1].includes(r.exitCode)),'ORIGINAL_CASE_INCOMPLETE:'+name);
 const seen=new Set(),failures=[];
 for(const r of c.results){
  need(caseNames.includes(r.caseName)&&!seen.has(r.caseName),'ORIGINAL_CASE_SET_MISMATCH:'+name);seen.add(r.caseName);
  need(r.cases[0].name===r.caseName,'ORIGINAL_CASE_NAME_MISMATCH:'+name);
  const passed=r.status==='pass';
  need((passed||r.status==='fail')&&r.exitCode===(passed?0:1)&&r.cases[0].passed===passed&&Array.isArray(r.failures)&&(r.failures.length===0)===passed,'ORIGINAL_CASE_TERMINAL_MISMATCH:'+name);
  need(r.failures.every(f=>f.case===r.caseName),'ORIGINAL_FAILURE_CASE_MISMATCH:'+name);
  failures.push(...r.failures);
 }
 need(seen.size===caseNames.length,'ORIGINAL_CASE_SET_MISMATCH:'+name);
 const failureKeys=rows=>rows.map(f=>JSON.stringify([f.case,f.assertion])).sort();
 need(Array.isArray(c.failures)&&JSON.stringify(failureKeys(c.failures))===JSON.stringify(failureKeys(failures)),'ORIGINAL_FAILURES_MISMATCH:'+name);
 const passed=failures.length===0;
 need(c.status===(passed?'pass':'fail')&&c.exitCode===(passed?0:1)&&c.checks===c.results.reduce((n,r)=>n+r.checks,0),'ORIGINAL_CONFIGURATION_TERMINAL_MISMATCH:'+name);
 if(fault){
  const expected=caseNames.flatMap(name=>(name===fault.caseName?fault.expectedFailures:fault.additionalFailures?.[name]??[]).map(assertion=>({case:name,assertion})));
  need(c.status==='fail'&&JSON.stringify(failureKeys(c.failures))===JSON.stringify(failureKeys(expected)),'WRONG_NEGATIVE_ASSERTION_SET:'+name+':'+c.configuration);
 }
 else need(c.status==='pass'&&(caseNames.length!==ORIGINAL_LIFECYCLE_CASE_NAMES.length||c.checks===31)&&c.failures.length===0,'ORIGINAL_ASSERTIONS_FAILED:'+c.configuration);
 return true;
}
export function validateLifecycleConfigurationSet(configurations,name='receipt',expected=['Release','Checked']){
 need(Array.isArray(configurations)&&Array.isArray(expected)&&expected.length>0&&expected.every(c=>['Release','Checked'].includes(c))&&new Set(expected).size===expected.length&&configurations.length===expected.length&&configurations.every(c=>c&&expected.includes(c.configuration))&&new Set(configurations.map(c=>c.configuration)).size===expected.length,'CONFIGURATIONS_MISSING:'+name);
 return true;
}
export async function execute(root,options={}){
 const selection=lifecycleCompletionRunSelection(options);
 root=path.resolve(root);fs.mkdirSync(path.dirname(root),{recursive:true});fs.mkdirSync(root);
 const sources=[...new Set([...INPUTS,...INFRASTRUCTURE])].map(p=>({path:p,sha256:hash(sourcePath(REPO,p))}));
 const {pins:tools}=lifecycleCompletionTools({capturePins:true});
 const steps=[];
 async function suite(name,source,{fault,caseNames=ORIGINAL_LIFECYCLE_CASE_NAMES}={}){
  const negative=Boolean(fault);
  const out=path.join(root,name);const args=[sourcePath(REPO,'tools/input_host_lifecycle_completion_selftest.mjs'),'--out',out,'--source',source,...(selection.directed?['--configs',selection.configurations.join(','),'--cases',caseNames.join(',')]:[])];
  const proc=await runProcess(process.execPath,args,{cwd:REPO,timeoutMs:60000,env:{...process.env,YMCC_VCVARS64:'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'}});
  steps.push({name,...proc,args});writeFresh(path.join(root,name+'.stdout.log'),proc.stdout);writeFresh(path.join(root,name+'.stderr.log'),proc.stderr);
  need(!proc.timedOut&&!proc.launchError&&!proc.signal&&!proc.outputTruncated,'RUNNER_NOT_COMPLETED:'+name);
  need(proc.exitCode===(negative?1:0),'ORIGINAL_PROCESS_EXIT_FAILED:'+name+':'+proc.exitCode);
  const receipt=read(path.join(out,'RESULT.json'));
  need(receipt.selection&&receipt.selection.directed===selection.directed&&JSON.stringify(receipt.selection.configurations)===JSON.stringify(selection.configurations)&&JSON.stringify(receipt.selection.caseNames)===JSON.stringify(caseNames),'ORIGINAL_SELECTION_MISMATCH:'+name);
  need(receipt.sourceUnchanged===true&&receipt.changedInputs.length===0,'ORIGINAL_INPUT_CHANGED:'+name);
  const declaredInputs=[...new Set([source,...INPUTS.filter(p=>p!=='native/main.cpp').map(p=>sourcePath(REPO,p)),...INFRASTRUCTURE.map(p=>sourcePath(REPO,p))])];
  const identity=p=>path.resolve(p).toLowerCase();
  need(receipt.source===source&&receipt.sourceSha256===hash(source)&&Array.isArray(receipt.inputs),'ORIGINAL_SOURCE_BINDING_REQUIRED:'+name);
  need(receipt.inputs.length===declaredInputs.length&&new Set(receipt.inputs.map(p=>identity(p.path))).size===declaredInputs.length&&receipt.inputs.every(p=>declaredInputs.some(file=>identity(file)===identity(p.path))&&hash(p.path)===p.sha256),'ORIGINAL_INPUT_SET_MISMATCH:'+name);
  validateLifecycleConfigurationSet(receipt.configurations,name,selection.configurations);
  for(const c of receipt.configurations){
   validateLifecycleConfiguration(c,{fault,name,caseNames});
   need(c.compilerInputs.length>20,'INCLUDE_CLOSURE_MISSING:'+name);
   need(Array.isArray(c.linkerInputs)&&c.linkerInputs.length>0,'LINK_CLOSURE_MISSING:'+name);
   for(const p of c.linkerInputs)need(tools.some(t=>path.resolve(t.path).toLowerCase()===path.resolve(p).toLowerCase()),'UNPINNED_ACTUAL_LINK_INPUT:'+p);
   need(c.sourceDependencies&&inside(out,c.sourceDependencies.path)&&hash(c.sourceDependencies.path)===c.sourceDependencies.sha256,'SOURCE_DEPENDENCY_RAW_CHANGED:'+name);
   for(const p of c.compilerInputs)need(inside(out,p)||tools.some(t=>t.path.toLowerCase()===p.toLowerCase()),'UNPINNED_ACTUAL_COMPILER_INPUT:'+p);


  }
  need(negative?proc.exitCode===1&&receipt.status==='fail':proc.exitCode===0&&receipt.status==='pass','WRONG_PROCESS_RESULT:'+name);
  return {path:path.join(out,'RESULT.json'),sha256:hash(path.join(out,'RESULT.json')),configurations:receipt.configurations.map(c=>({configuration:c.configuration,status:c.status,checks:c.checks,failures:c.failures})),expectedRejection:negative};
 }
 try{
  write(path.join(root,'inputs-before.json'),{sources,tools});
  const main=fs.readFileSync(sourcePath(REPO,'native/main.cpp'),'utf8');
  const positive=selection.phases.includes('positive')?await suite('positive',sourcePath(REPO,'native/main.cpp')):null;

  const negatives=[];
  for(const fault of LIFECYCLE_FAULTS.filter(f=>selection.phases.includes(f.id))){
   const matches=[...main.matchAll(new RegExp(fault.pattern.source,'g'))];need(matches.length===(fault.id==='claim-guard-omitted'?1:2),'FAULT_ANCHOR_CHANGED:'+fault.id);
   const source=path.join(root,fault.id+'.cpp');writeFresh(source,main.replace(fault.pattern,fault.replacement));
   negatives.push({id:fault.id,mutationOf:'native/main.cpp',assertionsUnmodified:true,proof:await suite('negative-'+fault.id,source,{fault,caseNames:selection.directed?[fault.caseName]:ORIGINAL_LIFECYCLE_CASE_NAMES})});
  }
  need(sources.every(r=>hash(sourcePath(REPO,r.path))===r.sha256),'SOURCE_CHANGED_DURING_RUN');
  need(tools.every(r=>hash(r.path)===r.sha256),'TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});
  const result={selection,fullOriginalModulePassed:!selection.directed,schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,positive,negatives,sourceHashes:[...sources,...walk(root).map(p=>({path:p,sha256:hash(p)}))],toolchain:tools,boundary:{productionQueueAndScopeExecuted:true,deviceDispatchExecuted:false,productOrHostStarted:false,hardwareIo:false,physicalReturnValidated:false,fullLifecycleAcceptance:false,cpuReductionMeasured:false}};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){
  try{write(path.join(root,'failure.json'),{status:'FAIL',error:error.message,steps,originalFailurePreserved:true});}
  catch(recordingError){
   try{
    const output=value=>({utf8Bytes:Buffer.byteLength(value),sha256:crypto.createHash('sha256').update(value).digest('hex')});
    console.error('YMCC_SIM_LIFECYCLE_FAILURE_RECORDING_FAILED '+JSON.stringify({
     schemaVersion:1,status:'INCOMPLETE_RECORDING',file:path.join(root,'failure.json'),durableFallbackClaimed:false,
     originalError:{code:error?.code??null,message:String(error?.message??error).slice(0,1600)},
     recordingError:{code:recordingError?.code??null,message:String(recordingError?.message??recordingError).slice(0,1600)},
     steps:steps.map(step=>({name:step.name,pid:step.pid,exitCode:step.exitCode,signal:step.signal,timedOut:step.timedOut,
      launchError:step.launchError,outputTruncated:step.outputTruncated,durationMs:step.durationMs,
      stdout:output(step.stdout),stderr:output(step.stderr)}))
    }));
   }catch{}
  }
  throw error;
 }
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const options={},allowed=['--evidence-dir','--configs','--phase'];for(let i=2;i<process.argv.length;i++){const k=process.argv[i];need(allowed.includes(k)&&process.argv[i+1]&&!Object.hasOwn(options,k),'UNKNOWN_OR_DUPLICATE_LIFECYCLE_OPTION');options[k]=process.argv[++i];}need(options['--evidence-dir'],'EVIDENCE_REQUIRED');const requested={...(options['--configs']===undefined?{}:{configurations:options['--configs'].split(',').map(s=>s.trim())}),...(options['--phase']===undefined?{}:{phase:options['--phase']})};lifecycleCompletionRunSelection(requested);const result=await execute(options['--evidence-dir'],requested);console.log(JSON.stringify({id:CASE_ID,status:result.status,selection:result.selection,fullOriginalModulePassed:result.fullOriginalModulePassed,hardwareIo:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=1;}
}
