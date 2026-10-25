import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {CASES,REPO,INFRASTRUCTURE} from './host-adapter.mjs';
import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
const need=(v,m)=>{if(!v)throw Error(m);};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'OUTPUT_LINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
export async function execute(id,root){
 const c=CASES[id];need(c,'UNREVIEWED_CASE');root=path.resolve(root);fs.mkdirSync(root,{recursive:true});
 const sources=[...new Set([...c.inputs,...INFRASTRUCTURE])].map(p=>({path:p,sha256:hash(sourcePath(REPO,p))}));
 const tools=toolchainFiles().map(p=>({path:p,sha256:hash(p)}));
 const spec=read(sourcePath(REPO,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'));
 const steps=[];
 async function suite(name,negative=false){
  const out=path.join(root,name);const args=[sourcePath(REPO,c.runner),'--out',out,...(negative?['--fault',c.mode==='parent'?'ignore-parent-cancellation':'drop-dispose-retention']:[])];
  const proc=await runProcess(process.execPath,args,{cwd:REPO,timeoutMs:120000,env:{...process.env,PATH:spec.dotnetRoot+path.delimiter+process.env.PATH,DOTNET_ROOT:spec.dotnetRoot}});
  fs.writeFileSync(path.join(root,name+'.stdout.log'),proc.stdout);fs.writeFileSync(path.join(root,name+'.stderr.log'),proc.stderr);steps.push({name,args,...proc});
  need(!proc.timedOut&&!proc.launchError&&!proc.signal&&!proc.outputTruncated,'RUNNER_NOT_COMPLETED:'+name);
  const r=read(path.join(out,'result.json'));need(r.sourceStable&&r.profiles.length===2,'SOURCE_OR_PROFILES_INVALID:'+name);
  need(r.sources.every(s=>s.sha256===sources.find(row=>path.resolve(REPO,row.path).toLowerCase()===path.resolve(s.path).toLowerCase())?.sha256),'PRODUCER_INPUT_NOT_BOUND:'+name);
  for(const p of r.profiles){
   need(p.result&&p.result.hardwareIo===false&&p.result.cases.length===(c.mode==='parent'?5:19),'CASE_RECEIPTS_INCOMPLETE:'+name);
   if(negative){
    const failed=p.result.cases.filter(t=>!t.pass).map(t=>t.name).sort();need(p.exit===1&&JSON.stringify(failed)===JSON.stringify(c.mode==='parent'?['parent-loss-during-connect','parent-loss-during-read','parent-loss-during-write']:['SDK Dispose context exception survives second recreate without false release','SDK Dispose controller exception survives second recreate without false release']),'WRONG_PARENT_NEGATIVE_FAILURES:'+p.config);
   }else{
    need(p.exit===0&&p.result.passed&&p.result.cases.every(t=>t.pass===true),'CURRENT_CASE_FAILED:'+p.config);
    if(c.mode==='protocol'){
     const old=p.result.legacyFailures.map(t=>t.name).sort();
     need(p.result.legacyCases.length===57&&JSON.stringify(old)===JSON.stringify(['flag rejected wrong persona/profile: (dualshock4, dualshock-4-v2)','flag rejected wrong persona/profile: (steamdeck, wrong-profile)','flag rejected wrong persona/profile: (xbox360, xbox-360-wired)']),'UNREVIEWED_LEGACY_FAILURE_CHANGE');
    }
   }
  }
  need(proc.exitCode===(negative?1:0)&&r.status===(negative?'FAIL':'PASS'),'WRONG_FINAL_STATUS:'+name);
  return {path:path.join(out,'result.json'),sha256:hash(path.join(out,'result.json')),profiles:r.profiles.map(p=>({config:p.config,exit:p.exit,cases:p.result.cases,legacyFailures:p.result.legacyFailures??null})),expectedRejection:negative};
 }
 try{
  write(path.join(root,'inputs-before.json'),{sources,tools});
  const positive=await suite('positive');const negative=await suite(c.mode==='parent'?'negative-cancellation-omitted':'negative-dispose-retention-omitted',true);
  need(sources.every(s=>hash(sourcePath(REPO,s.path))===s.sha256),'SOURCE_CHANGED_DURING_RUN');need(tools.every(t=>hash(t.path)===t.sha256),'TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});
  const result={schemaVersion:1,id,status:'PASS',passed:true,positive,negative,legacyWholeFixtureStatus:c.mode==='protocol'?'FAIL_RETAINED_PROTOCOL_REJECTION_DETAIL_DRIFT':null,sourceHashes:[...sources,...walk(root).map(p=>({path:p,sha256:hash(p)}))],toolchain:tools,boundary:{productMainExecuted:false,sdkLoaded:false,hardwareIo:false,realBclPipes:c.mode==='parent',ownedChildProcesses:c.mode==='parent',physicalReturnValidated:false,fullLifecycleAcceptance:false,cpuReductionMeasured:false}};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',error:error.message,steps,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{need(process.argv.length===6&&process.argv[2]==='--case'&&process.argv[4]==='--evidence-dir','CASE_AND_EVIDENCE_REQUIRED');const r=await execute(process.argv[3],process.argv[5]);console.log(JSON.stringify({id:r.id,status:r.status,profiles:2,groupsPerProfile:CASES[r.id].mode==='parent'?5:19,hardwareIo:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=1;}
}
