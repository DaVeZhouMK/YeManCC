import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {REPO,toolInputs} from './adapter.mjs';
import {consumerEnvironment} from './environment.mjs';
import {createOwnedWorkspace} from './owned-workspace.mjs';
import {managedContract,managedCurrentVersion,validateManagedTerminal} from './managed-terminal-receipts.mjs';
import {qualifyManagedV2} from './managed-terminal-v2.mjs';
import {runManagedTerminalGroup} from './consumer-runner.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const out=path.resolve(process.argv[process.argv.indexOf('--out')+1]??'');assert(process.argv.includes('--out')&&path.isAbsolute(out)&&!fs.existsSync(out),'MANAGED_FRESH_ABSOLUTE_OUTPUT');fs.mkdirSync(out,{recursive:true});
const version=managedCurrentVersion(),qualifyExisting=process.argv.includes('--qualify-existing');
const c=managedContract(REPO,{version}),tools=toolInputs(REPO),sourceHashes=Object.fromEntries(c.sourceInputs.map(n=>[n,hash(path.join(REPO,n))]));
const controlSources=Object.fromEntries(['managed-terminal-registration.mjs','consumer-runner.mjs','managed-terminal-receipts.mjs','managed-terminal-v2.mjs','consumer-schema.json','owned-workspace.mjs','environment.mjs'].map(n=>{const p=path.join(REPO,'tools/ymcc_sim/global/controller-signal-evidence',n);return [p,hash(p)];}));
const workspace=qualifyExisting?null:createOwnedWorkspace({repo:REPO,output:out,primary:tools.primary,sourceHashes}),env=consumerEnvironment({ambient:process.env,tools,out,nodeExecutable:process.execPath});
const execution={schemaVersion:1,status:'RUNNING',pass:false,version,qualificationOnly:qualifyExisting,scope:'independent-managed-terminal22-v2-only',sourceHashes,controlSources,jobs:[],workspace:workspace?{root:workspace.root,identity:workspace.identity}:null,parentLifecycleAcceptance:'INCOMPLETE'};
const save=()=>fs.writeFileSync(path.join(out,'EXECUTION.json'),JSON.stringify(execution,null,2)+'\n');
const primary=()=>Object.entries(tools.primary).map(([role,p])=>{const pin=tools.files.find(r=>path.resolve(r.path).toLowerCase()===path.resolve(p).toLowerCase());assert(pin&&pin.sha256===hash(p),'MANAGED_PRIMARY_TOOL_PIN');return {role,path:p,sha256:pin.sha256};});
async function run(id,executable,args,expectedExit,extra,owned){workspace.verify(owned);const row={id,executable,args,expectedExit,ownedWorkspace:owned,primaryToolsBefore:primary(),startedAt:new Date().toISOString(),exitCode:null,signal:null,error:null,timedOut:false};execution.jobs.push(row);save();const chunks=[];let size=0;
 await new Promise(resolve=>{const child=spawn(executable,args,{cwd:REPO,windowsHide:true,env:{...env,...extra,TEMP:owned.temp,TMP:owned.temp},stdio:['ignore','pipe','pipe']});const timer=setTimeout(()=>{row.timedOut=true;child.kill();},900000);const end=()=>{clearTimeout(timer);resolve();};const collect=b=>{size+=b.length;if(size<=8*1024*1024)chunks.push(b);else{row.error='OUTPUT_LIMIT_EXCEEDED';child.kill();}};child.stdout.on('data',collect);child.stderr.on('data',collect);child.on('error',e=>{row.error=e.message;end();});child.on('close',(code,signal)=>{row.exitCode=code;row.signal=signal;end();});});
 row.finishedAt=new Date().toISOString();row.primaryToolsAfter=primary();fs.writeFileSync(path.join(out,id+'.log'),Buffer.concat(chunks));workspace.verify(owned);save();assert(row.exitCode===expectedExit&&row.signal===null&&row.error===null&&!row.timedOut,'MANAGED_CONSUMER_FAILED:'+id);return row;
}
try{for(const row of tools.files)assert.equal(hash(row.path),row.sha256,'MANAGED_ORIGINAL_731_TOOL_PIN');const {file,result}=(qualifyExisting?qualifyManagedV2({repo:REPO,tools,out:path.join(out,'managed-terminal')}):await runManagedTerminalGroup({repo:REPO,sourceHashes,tools,workspace,run,out}));execution.receipt={path:file,sha256:hash(file)};execution.validation=validateManagedTerminal(result,{repo:REPO,sourceHashes,tools,version});execution.status='PASS';execution.pass=true;}
catch(error){execution.status='FAIL';execution.error=error.message;execution.stack=error.stack;console.error(error.stack);process.exitCode=1;}
finally{for(const [p,pin] of Object.entries(controlSources))if(hash(p)!==pin){execution.pass=false;execution.status='FAIL';execution.controlDrift=p;process.exitCode=1;}for(const [name,pin] of Object.entries(sourceHashes))if(hash(path.join(REPO,name))!==pin){execution.pass=false;execution.status='FAIL';execution.sourceDrift=name;process.exitCode=1;}workspace?.verify();execution.finishedAt=new Date().toISOString();save();console.log(JSON.stringify({status:execution.status,validation:execution.validation,error:execution.error},null,2));}
