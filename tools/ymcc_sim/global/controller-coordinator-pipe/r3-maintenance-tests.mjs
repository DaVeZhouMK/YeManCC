// Runs maintenance tests against existing compiled receipts; never rebuilds or runs the five leaves.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {MAINTENANCE_REPO,resolveEquivalenceContext,resolveHistoricalContext} from './r3-maintenance-context.mjs';
const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
const files=['r3-reviewed-contract.test.mjs','r3-source-equivalence.test.mjs','registration-metadata.test.mjs','r3-contract.test.mjs','r3-maintenance-context.test.mjs'];
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const write=(out,name,value)=>fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const inside=(root,file)=>{const relative=path.relative(root,file);return relative===''||(!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative));};
export function runMaintenanceTests({evidenceRoot,historyConfig,manifestPath=process.env.VC_R3_EQUIVALENCE_MANIFEST}={}){
 if(typeof evidenceRoot!=='string'||!path.isAbsolute(evidenceRoot))throw Error('R3_MAINTENANCE_ABSOLUTE_FRESH_EVIDENCE_REQUIRED');
 const out=path.resolve(evidenceRoot),repo=MAINTENANCE_REPO;
 if(inside(repo,out)||fs.existsSync(out))throw Error('R3_MAINTENANCE_FRESH_OUTPUT_OUTSIDE_SOURCE_REQUIRED');
 fs.mkdirSync(out,{recursive:true});
 try{
  if(!historyConfig)throw Error('R3_MAINTENANCE_HISTORY_CONFIG_REQUIRED: use --history-config ABS_JSON to execute all five historical tests');
  const history=resolveHistoricalContext(historyConfig),context=resolveEquivalenceContext({manifestPath});
  const temp=path.join(out,'temp');fs.mkdirSync(temp);
  const args=['--test',...files.map(name=>path.join(repo,MODULE,name))];
  const tracked=[...files.map(name=>path.join(repo,MODULE,name)),path.join(repo,MODULE,'r3-maintenance-context.mjs'),path.join(repo,MODULE,'r3-maintenance-tests.mjs'),path.join(repo,MODULE,'r3-contract.mjs'),path.join(repo,MODULE,'r3-prerequisite-root.json'),path.join(repo,MODULE,'r3-reviewed-contract.json'),path.join(repo,MODULE,'r3-reviewed-scenarios.json'),context.manifest,history.config.path,...history.identities.map(row=>row.path)];
  const before=tracked.map(file=>({path:file,sha256:sha(file)}));
  const env={...process.env,...history.env,VC_R3_EQUIVALENCE_MANIFEST:context.manifest,VC_R3_MAINTENANCE_HISTORY:history.config.path,VC_R3_EQUIVALENCE_REPO:context.foreign,TEMP:temp,TMP:temp};
  const start=new Date().toISOString(),processResult=spawnSync(process.execPath,args,{cwd:repo,env,encoding:'utf8',maxBuffer:8*1024*1024,timeout:60000,windowsHide:true});
  const stdout=processResult.stdout??'',stderr=processResult.stderr??'';
  fs.writeFileSync(path.join(out,'maintenance.tap'),stdout,{flag:'wx'});fs.writeFileSync(path.join(out,'maintenance.stderr.log'),stderr,{flag:'wx'});
  const count=name=>Number(new RegExp('^# '+name+' ([0-9]+)$','m').exec(stdout)?.[1]??NaN);
  const counts={tests:count('tests'),pass:count('pass'),fail:count('fail'),skipped:count('skipped'),cancelled:count('cancelled')};
  const identity=before.map(row=>({...row,afterSha256:sha(row.path)}));
  const passed=processResult.status===0&&counts.tests===86&&counts.pass===counts.tests&&counts.fail===0&&counts.skipped===0&&counts.cancelled===0&&identity.every(row=>row.sha256===row.afterSha256);
  const result={schemaVersion:1,status:passed?'PASS':'FAIL',startedAt:start,completedAt:new Date().toISOString(),executable:process.execPath,args,sourceRoot:repo,exitCode:processResult.status,signal:processResult.signal,error:processResult.error?.message??null,counts,equivalenceContext:context.summary,historicalScope:history.identities,identity,compilerExecuted:false,nativeOrManagedLeafExecuted:false,originalFiveLeafRerun:false,publicAcceptanceClaimed:false};
  write(out,'RESULT.json',result);return result;
 }catch(error){write(out,'FAILURE.json',{schemaVersion:1,status:'FAIL_PRECONDITION',error:error.message,compilerExecuted:false,nativeOrManagedLeafExecuted:false,publicAcceptanceClaimed:false});throw error;}
}
const help='Usage: node tools/ymcc_sim/global/controller-coordinator-pipe/r3-maintenance-tests.mjs --evidence-root ABS_NEW_OUTSIDE_SOURCE --history-config ABS_JSON\nThe declared positive managed manifest is selected and strictly verified by default. VC_R3_EQUIVALENCE_MANIFEST may explicitly name that same manifest. History JSON schemaVersion=1 requires receipts.current/old/normal with absolute path and sha256. All original67, legacy contract/history and context tests execute with zero skips; no compile or five-leaf run occurs.\n';
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{
  const args=process.argv.slice(2);if(args.length===1&&args[0]==='--help'){console.log(help);}else{
   if(args.length%2)throw Error('R3_MAINTENANCE_ARGUMENT_PAIRS_REQUIRED');const options={};
   for(let index=0;index<args.length;index+=2){const key=args[index];if(!['--evidence-root','--history-config'].includes(key)||options[key]||!args[index+1])throw Error('R3_MAINTENANCE_ARGUMENT_INVALID');options[key]=args[index+1];}
   const result=runMaintenanceTests({evidenceRoot:options['--evidence-root'],historyConfig:options['--history-config']});console.log(JSON.stringify({status:result.status,counts:result.counts}));if(result.status!=='PASS')process.exitCode=1;
  }
 }catch(error){console.error(error.message);process.exitCode=1;}
}