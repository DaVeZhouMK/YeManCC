import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {CASE_ID,REPO,INPUTS,INFRASTRUCTURE,installedTools} from './adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
function inside(root,file){const rel=path.relative(root,file);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));}
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'OUTPUT_LINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
export async function execute(root){
 root=path.resolve(root);fs.mkdirSync(root,{recursive:true});
 const sources=[...new Set([...INPUTS,...INFRASTRUCTURE])].map(p=>({path:p,sha256:hash(sourcePath(REPO,p))}));
 const tools=installedTools().map(p=>({path:p,sha256:hash(p)}));
 const steps=[];
 async function suite(name,source,{negative=false,expected}={}){
  const out=path.join(root,name);const args=[sourcePath(REPO,'tools/input_host_lifecycle_completion_selftest.mjs'),'--out',out,'--source',source];
  const proc=await runProcess(process.execPath,args,{cwd:REPO,timeoutMs:60000,env:{...process.env,YMCC_VCVARS64:'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'}});
  fs.writeFileSync(path.join(root,name+'.stdout.log'),proc.stdout);fs.writeFileSync(path.join(root,name+'.stderr.log'),proc.stderr);steps.push({name,...proc,args});
  need(!proc.timedOut&&!proc.launchError&&!proc.signal&&!proc.outputTruncated,'RUNNER_NOT_COMPLETED:'+name);
  const receipt=read(path.join(out,'RESULT.json'));
  need(receipt.sourceUnchanged===true&&receipt.changedInputs.length===0,'ORIGINAL_INPUT_CHANGED:'+name);
  need(receipt.configurations.length===2,'CONFIGURATIONS_MISSING:'+name);
  for(const c of receipt.configurations){
   need(c.results.length===8&&c.results.every(r=>r.checks>0&&r.cases.length===1&&[0,1].includes(r.exitCode)),'ORIGINAL_CASE_INCOMPLETE:'+name);
   need(c.compilerInputs.length>20,'INCLUDE_CLOSURE_MISSING:'+name);
   for(const p of c.compilerInputs)need(inside(out,p)||tools.some(t=>t.path.toLowerCase()===p.toLowerCase()),'UNPINNED_ACTUAL_COMPILER_INPUT:'+p);
   if(negative)need(c.status==='fail'&&c.failures.some(f=>f.assertion===expected),'WRONG_NEGATIVE_ASSERTION:'+name+':'+c.configuration);
   else need(c.status==='pass'&&c.checks===31&&c.failures.length===0,'ORIGINAL_ASSERTIONS_FAILED:'+c.configuration);
  }
  need(negative?proc.exitCode===1&&receipt.status==='fail':proc.exitCode===0&&receipt.status==='pass','WRONG_PROCESS_RESULT:'+name);
  return {path:path.join(out,'RESULT.json'),sha256:hash(path.join(out,'RESULT.json')),configurations:receipt.configurations.map(c=>({configuration:c.configuration,status:c.status,checks:c.checks,failures:c.failures})),expectedRejection:negative};
 }
 try{
  write(path.join(root,'inputs-before.json'),{sources,tools});
  const main=fs.readFileSync(sourcePath(REPO,'native/main.cpp'),'utf8');
  const positive=await suite('positive',sourcePath(REPO,'native/main.cpp'));
  const faults=[
   {id:'claim-guard-omitted',pattern:/    ymcc::inputHost::LifecycleClaimGuard completion\(g_inputHostLifecycleWorkRequested,\r?\n        g_inputHostLifecycleWorkInFlight\);/,replacement:'    /* negative control: omit completion guard */',expected:'failed pre-dequeue snapshot releases the lifecycle claim'},
   {id:'unattempted-cleanup-retention-omitted',pattern:/retainUnattemptedWork\(\);/g,replacement:'(void)retainUnattemptedWork;',expected:'unattempted cleanup returns ahead of rearm'}
  ];
  const negatives=[];
  for(const fault of faults){
   const matches=[...main.matchAll(new RegExp(fault.pattern.source,'g'))];need(matches.length===(fault.id==='claim-guard-omitted'?1:2),'FAULT_ANCHOR_CHANGED:'+fault.id);
   const source=path.join(root,fault.id+'.cpp');fs.writeFileSync(source,main.replace(fault.pattern,fault.replacement));
   negatives.push({id:fault.id,mutationOf:'native/main.cpp',assertionsUnmodified:true,proof:await suite('negative-'+fault.id,source,{negative:true,expected:fault.expected})});
  }
  need(sources.every(r=>hash(sourcePath(REPO,r.path))===r.sha256),'SOURCE_CHANGED_DURING_RUN');
  need(tools.every(r=>hash(r.path)===r.sha256),'TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});
  const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,positive,negatives,sourceHashes:[...sources,...walk(root).map(p=>({path:p,sha256:hash(p)}))],toolchain:tools,boundary:{productionQueueAndScopeExecuted:true,deviceDispatchExecuted:false,productOrHostStarted:false,hardwareIo:false,physicalReturnValidated:false,fullLifecycleAcceptance:false,cpuReductionMeasured:false}};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',error:error.message,steps,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{need(process.argv.length===4&&process.argv[2]==='--evidence-dir'&&process.argv[3],'EVIDENCE_REQUIRED');const result=await execute(process.argv[3]);console.log(JSON.stringify({id:CASE_ID,status:result.status,checks:62,semanticFaultsRejected:2,hardwareIo:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=1;}
}
