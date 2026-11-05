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
const HOST_CASE_NAMES=Object.freeze({"parent":["healthy-idle","pipe-break","parent-loss-during-read","parent-loss-during-write","parent-loss-during-connect"],"protocol":["HELLO PREPARE NEUTRAL FRAME QUIESCE RELEASE SHUTDOWN actual dispatcher","dualsense-edge current target lifecycle and barrier retry","elite current target lifecycle and barrier retry","current parser rejects all three obsolete wrong-pair inputs","output stop failure retains resources and RELEASE retry disposes","output stop failure rejects SHUTDOWN until successful retry","output stop failure prevents RECREATE from creating second target","idle stop failure prevents output stop and destruction","partial PREPARE retains objects when release barrier fails; retry gates create","frame backend failure releases and makes next frame terminal","frame backend failure with stalled output keeps resources for retry","target tuple change cannot replace retained target or release pins","idempotent PREPARE replay creates once; changed replay rejected","idle CTS capture failure keeps controller and blocks SDK Dispose","idle CTS disposal follows successful SDK return and retries same pending resource","SDK Dispose exception never authorizes idle CTS explicit disposal","SDK Dispose controller exception survives second recreate without false release","SDK Dispose context exception survives second recreate without false release","RECREATE preserves MSI owned absence requirement"]});
const HOST_NEGATIVE_NAMES=Object.freeze({"parent":["parent-loss-during-connect","parent-loss-during-read","parent-loss-during-write"],"protocol":["SDK Dispose context exception survives second recreate without false release","SDK Dispose controller exception survives second recreate without false release"]});
const HOST_LEGACY_FAILURE_NAMES=Object.freeze(["flag rejected wrong persona/profile: (dualshock4, dualshock-4-v2)","flag rejected wrong persona/profile: (steamdeck, wrong-profile)","flag rejected wrong persona/profile: (xbox360, xbox-360-wired)"]);
export function validateHostLifecycleReceipt(r,mode,{negative=false,name='receipt'}={}){
 const wanted=HOST_CASE_NAMES[mode];need(wanted,'UNREVIEWED_CASE');
 need(r.sourceStable===true&&Array.isArray(r.profiles)&&r.profiles.length===2&&r.profiles.every(p=>p&&['Release','Debug'].includes(p.config))&&new Set(r.profiles.map(p=>p.config)).size===2,'SOURCE_OR_PROFILES_INVALID:'+name);
 for(const p of r.profiles){
  const result=p.result;
  need(result&&result.hardwareIo===false&&Array.isArray(result.cases)&&result.cases.length===wanted.length,'CASE_RECEIPTS_INCOMPLETE:'+name);
  need(result.cases.every(t=>t&&wanted.includes(t.name)&&typeof t.pass==='boolean')&&new Set(result.cases.map(t=>t.name)).size===wanted.length,'CASE_RECEIPT_SET_MISMATCH:'+name);
  const failed=result.cases.filter(t=>!t.pass).map(t=>t.name).sort();
  if(negative)need(p.exit===1&&result.passed===false&&JSON.stringify(failed)===JSON.stringify([...HOST_NEGATIVE_NAMES[mode]].sort()),'WRONG_PARENT_NEGATIVE_FAILURES:'+p.config);
  else{
   need(p.exit===0&&result.passed===true&&failed.length===0,'CURRENT_CASE_FAILED:'+p.config);
   if(mode==='protocol'){
    const old=result.legacyFailures?.map(t=>t.name).sort();
    need(Array.isArray(result.legacyCases)&&result.legacyCases.length===57&&JSON.stringify(old)===JSON.stringify([...HOST_LEGACY_FAILURE_NAMES].sort()),'UNREVIEWED_LEGACY_FAILURE_CHANGE');
    need(result.legacyExit===0&&result.legacyCases.every(t=>t&&typeof t.pass==='boolean')&&JSON.stringify(result.legacyCases.filter(t=>!t.pass).map(t=>t.name).sort())===JSON.stringify(old),'LEGACY_FAILURE_RECEIPTS_MISMATCH:'+p.config);
   }
  }
 }
 return true;
}
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
  const r=read(path.join(out,'result.json'));
  need(r.sources.every(s=>s.sha256===sources.find(row=>path.resolve(REPO,row.path).toLowerCase()===path.resolve(s.path).toLowerCase())?.sha256),'PRODUCER_INPUT_NOT_BOUND:'+name);
  validateHostLifecycleReceipt(r,c.mode,{negative,name});
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
