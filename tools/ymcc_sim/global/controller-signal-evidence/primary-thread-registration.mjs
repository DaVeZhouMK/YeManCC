// Independent group21 entry. This never starts the other twenty consumers.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {REPO,sourceInputs,toolInputs,historicalInputs} from './adapter.mjs';
import {consumerEnvironment} from './environment.mjs';
import {createOwnedWorkspace} from './owned-workspace.mjs';
import {executePrimaryThread,validatePrimaryThread} from './primary-thread-receipts.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export async function runPrimaryRegistration({repo=REPO,out,identity,ambient=process.env,toolchainAuthorityRepo=repo}){
 assert(path.isAbsolute(out)&&!fs.existsSync(out),'PRIMARY_FRESH_OUTPUT');fs.mkdirSync(out,{recursive:true});
 const tools=toolInputs(repo),historical=historicalInputs(repo),sourceHashes=Object.fromEntries(sourceInputs.map(n=>[n,hash(path.join(repo,n))]));
 const execution={schemaVersion:1,status:'RUNNING',pass:false,scope:'independent-group21-only',sourceRepository:repo,toolchainAuthorityRepository:toolchainAuthorityRepo,runner:{path:process.argv[1],sha256:hash(process.argv[1])},parentLifecycleAcceptance:'INCOMPLETE',identity,sourceHashes,jobs:[]};
 const save=()=>fs.writeFileSync(path.join(out,'EXECUTION.json'),JSON.stringify(execution,null,2)+'\n');save();
 const primary=()=>Object.entries(tools.primary).map(([role,file])=>{const pin=tools.files.find(r=>path.resolve(r.path).toLowerCase()===path.resolve(file).toLowerCase());assert(pin&&hash(file)===pin.sha256,'PRIMARY_TOOL_IDENTITY:'+role);return {role,path:file,sha256:pin.sha256};});
 const verifyTools=()=>tools.files.map(r=>{assert.equal(hash(r.path),r.sha256,'PRIMARY_REVIEWED_TOOL_CHANGED:'+r.path);return {...r};});
 const env=consumerEnvironment({ambient,tools,out,nodeExecutable:process.execPath}),workspace=createOwnedWorkspace({repo,output:out,primary:tools.primary,sourceHashes});execution.workspace={root:workspace.root,identity:workspace.identity};
 async function run(id,executable,args,expectedExit,extra,owned){
  workspace.verify(owned);const row={id,executable,args,expectedExit,ownedWorkspace:owned,primaryToolsBefore:primary(),startedAt:new Date().toISOString(),exitCode:null,signal:null,error:null,timedOut:false};execution.jobs.push(row);save();let bytes=0;const stdout=[],stderr=[];
  await new Promise(resolve=>{const child=spawn(executable,args,{cwd:repo,windowsHide:true,env:{...env,...extra,TEMP:owned.temp,TMP:owned.temp},stdio:['ignore','pipe','pipe']});const timer=setTimeout(()=>{row.timedOut=true;child.kill();},900000);const end=()=>{clearTimeout(timer);resolve();};const collect=target=>b=>{bytes+=b.length;if(bytes<=8*1024*1024)target.push(b);else{row.error='OUTPUT_LIMIT_EXCEEDED';child.kill();}};child.stdout.on('data',collect(stdout));child.stderr.on('data',collect(stderr));child.on('error',error=>{row.error=error.message;end();});child.on('close',(code,signal)=>{row.exitCode=code;row.signal=signal;end();});});
  row.finishedAt=new Date().toISOString();row.primaryToolsAfter=primary();workspace.verify(owned);fs.writeFileSync(path.join(out,'primary-thread-entry.log'),Buffer.concat([...stdout,...stderr]));save();assert(row.exitCode===expectedExit&&row.signal===null&&row.error===null&&!row.timedOut,'PRIMARY_ACTUAL_CONSUMER_FAILED:'+JSON.stringify(row));return row;
 }
 try{
  execution.toolsBefore=verifyTools();const {job,file,context}=await executePrimaryThread({repo,sourceHashes,workspace,run,identity,python:tools.primary.python,toolchainAuthorityRepo});
  const canonical=path.join(out,'primary-thread','RESULT.json');fs.mkdirSync(path.dirname(canonical));fs.copyFileSync(file,canonical);assert.equal(hash(canonical),hash(file));job.publishedReceipts=[{actualPath:file,canonicalPath:canonical,sha256:hash(file)}];execution.context={path:path.join(path.dirname(file),'registration-context.json'),sha256:hash(path.join(path.dirname(file),'registration-context.json'))};save();
  execution.validation=validatePrimaryThread(JSON.parse(fs.readFileSync(file,'utf8')),{repo,sourceHashes,tools,identity,toolchainAuthorityRepo});execution.status='PASS';execution.pass=true;
 }catch(error){execution.status='FAIL';execution.error=error.message;execution.stack=error.stack;throw error;}
 finally{
  try{execution.toolsAfter=verifyTools();primary();workspace.verify();for(const [n,p] of Object.entries(sourceHashes))assert.equal(hash(path.join(repo,n)),p,'PRIMARY_POST_SOURCE_DRIFT:'+n);for(const [n,p] of Object.entries(historical))assert.equal(hash(p.path),p.sha256,'PRIMARY_POST_HISTORICAL_DRIFT:'+n);}
  catch(error){execution.status='FAIL';execution.pass=false;execution.postAuditError=error.message;throw error;}
  finally{execution.finishedAt=new Date().toISOString();save();}
 }
 return execution;
}
export function revalidatePrimaryExecution({executionFile,out,repo=REPO}){
 assert(path.isAbsolute(executionFile)&&path.isAbsolute(out)&&!fs.existsSync(out),'PRIMARY_FRESH_REVALIDATION_REQUIRED');
 const originalPin={path:executionFile,sha256:hash(executionFile)},e=JSON.parse(fs.readFileSync(executionFile,'utf8'));
 const validatorClosure=Object.fromEntries(fs.readdirSync(path.dirname(process.argv[1])).filter(name=>name.endsWith('.mjs')||name.endsWith('.json')).map(name=>{const file=path.join(path.dirname(process.argv[1]),name);return [file,hash(file)];}));
 const report={schemaVersion:1,status:'FAIL',pass:false,scope:'independent-group21-existing-actual-execution-revalidation',originalExecution:{...originalPin,status:e.status,pass:e.pass,error:e.error},validatorClosure,parentLifecycleAcceptance:'INCOMPLETE'};
 try{
  assert(e.scope==='independent-group21-only'&&e.jobs.length===1&&e.jobs[0].exitCode===0&&e.jobs[0].error===null&&e.jobs[0].signal===null&&!e.jobs[0].timedOut,'PRIMARY_REAL_COMPLETED_ENTRY_REQUIRED');
  assert(e.status==='PASS'||e.error==='PRIMARY_ACTUAL_LINK_LIBRARIES','PRIMARY_ONLY_LOCALIZED_LINKER_REVALIDATION');
  const tools=toolInputs(repo);for(const row of tools.files)assert.equal(hash(row.path),row.sha256,'PRIMARY_CURRENT_TOOL_PIN');
  for(const phase of ['toolsBefore','toolsAfter'])assert(e[phase]?.length===731&&e[phase].every(row=>hash(row.path)===row.sha256),'PRIMARY_ORIGINAL_TOOL_SEALS');
  const actual=e.jobs[0].publishedReceipts[0];assert.equal(hash(actual.actualPath),actual.sha256,'PRIMARY_ORIGINAL_PUBLISHED_RECEIPT');assert.equal(hash(actual.canonicalPath),actual.sha256,'PRIMARY_CANONICAL_RECEIPT');assert.equal(hash(e.context.path),e.context.sha256,'PRIMARY_ORIGINAL_CONTEXT_PIN');
  const r=JSON.parse(fs.readFileSync(actual.actualPath,'utf8'));report.validation=validatePrimaryThread(r,{repo:e.sourceRepository,toolchainAuthorityRepo:e.toolchainAuthorityRepository,sourceHashes:e.sourceHashes,tools,identity:e.identity});
  report.consumerReceipt={path:actual.actualPath,sha256:actual.sha256};report.originalContext=e.context;report.actualSourceRepository=e.sourceRepository;report.actualSourceClosure=e.sourceHashes;report.approvedIdentity=e.identity;
  assert.equal(hash(executionFile),originalPin.sha256,'PRIMARY_ORIGINAL_EXECUTION_CHANGED');for(const [file,pin] of Object.entries(validatorClosure))assert.equal(hash(file),pin,'PRIMARY_VALIDATOR_CHANGED_DURING_REVALIDATION');
  report.status='PASS';report.pass=true;
 }catch(error){report.error=error.message;report.stack=error.stack;throw error;}
 finally{report.finishedAt=new Date().toISOString();fs.writeFileSync(out,JSON.stringify(report,null,2)+'\n',{flag:'wx'});}
 return report;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
 const args=Object.fromEntries(Array.from({length:(process.argv.length-2)/2},(_,i)=>[process.argv[2+i*2],process.argv[3+i*2]]));
 if(args['--validate-execution']){
  try{const r=revalidatePrimaryExecution({executionFile:args['--validate-execution'],out:args['--out']});console.log(JSON.stringify({status:r.status,...r.validation},null,2));}catch(error){console.error(error.stack);process.exitCode=1;}
 }else{
 assert(args['--out']&&args['--identity-manifest']&&/^[a-f0-9]{64}$/.test(args['--identity-sha256']??''),'PRIMARY_EXPLICIT_ARGUMENTS_REQUIRED');
 try{const result=await runPrimaryRegistration({out:args['--out'],identity:{path:args['--identity-manifest'],sha256:args['--identity-sha256']},toolchainAuthorityRepo:args['--toolchain-authority-repo']??REPO});console.log(JSON.stringify({status:result.status,...result.validation},null,2));}catch(error){console.error(error.stack);process.exitCode=1;}
}
}
