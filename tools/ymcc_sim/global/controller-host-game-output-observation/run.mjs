import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {CASE_ID,MODULE,REPO,PRODUCER,productSources,moduleSources,infrastructureSources,toolFiles} from './adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
const need=(ok,code)=>{if(!ok)throw Error(code);};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
const key=p=>path.resolve(p).toLowerCase();
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'OUTPUT_SYMLINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
export async function execute(root){
 root=path.resolve(root);need(root.length<=180,'OUTPUT_ROOT_REQUIRES_SHORT_PATH');need(!fs.existsSync(root),'FRESH_OUTPUT_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const spec=read(sourcePath(REPO,MODULE+'/spec.json'));need(spec.id===CASE_ID&&spec.schemaVersion===1&&spec.faults.length===10,'UNREVIEWED_OBSERVATION_SPEC');
 const sources=[...new Set([...productSources(),...moduleSources(),...infrastructureSources()])].map(p=>({path:p,sha256:hash(sourcePath(REPO,p))}));
 const tools=toolFiles().map(p=>({path:p,sha256:hash(p)}));
 const originMap=new Map([...sources.map(r=>[key(path.join(REPO,r.path)),r.sha256]),...tools.map(r=>[key(r.path),r.sha256])]);
 const steps=[];
 async function suite(name,sourceRoot,fault){
  const out=path.join(root,name),args=[sourcePath(REPO,PRODUCER),'--source-root',sourceRoot,'--out',out,...(fault?['--configs',spec.faultConfiguration]:[])];
  const proc=await runProcess(process.execPath,args,{cwd:REPO,timeoutMs:120000});
  fs.writeFileSync(path.join(root,name+'.stdout.log'),proc.stdout);fs.writeFileSync(path.join(root,name+'.stderr.log'),proc.stderr);
  steps.push({name,args,...proc});
  need(!proc.timedOut&&!proc.launchError&&!proc.signal&&!proc.outputTruncated,'PRODUCER_DID_NOT_COMPLETE:'+name);
  const r=read(path.join(out,'result.json'));need(r.stable===true&&r.candidate===true&&r.targetAssemblyLoaded===false&&r.realHostStarted===false&&r.deviceIo===false,'OBSERVATION_BOUNDARY_OR_SOURCE:'+name);
  const allowed=new Map(originMap);
  if(fault)for(const f of fs.readdirSync(sourceRoot).filter(n=>n.endsWith('.cs'))){const p=path.join(sourceRoot,f);allowed.set(key(p),hash(p));}
  need(r.sources.every(row=>allowed.get(key(row.path))===row.sha256),'UNBOUND_PRODUCER_INPUT:'+name);
  const expectedProfiles=fault?[spec.faultConfiguration]:spec.positive.configurations;
  need(JSON.stringify(r.profiles.map(p=>p.config))===JSON.stringify(expectedProfiles),'PROFILE_SET_CHANGED:'+name);
  const profiles=[];
  for(const p of r.profiles){
   const game=p['game-observation'],life=p['original-lifecycle'];
   need(p.build?.exit===0&&game?.result&&life?.result,'MISSING_COMPILED_BUSINESS_RECEIPT:'+name);
   need(game.result.cases.length===spec.positive.observationCases&&life.result.cases.length===spec.positive.lifecycleCases,'BUSINESS_CASE_SET_CHANGED:'+name);
   need(game.result.deviceIo===false&&game.result.realHostStarted===false&&game.result.gameFeedbackForwardingProven===false,'FALSE_GAME_FEEDBACK_PROOF:'+name);
   need(life.result.legacyCases.length===spec.positive.legacyCases&&JSON.stringify(life.result.legacyFailures.map(c=>c.name).sort())===JSON.stringify([...spec.positive.retainedLegacyFailureNames].sort()),'ORIGINAL_LEGACY_EXPECTATIONS_CHANGED:'+name);
   const failed=game.result.cases.filter(c=>c.pass!==true);
   if(fault){need(game.exit===1&&game.result.passed===false&&failed.some(c=>c.name===fault.designatedCase),'DESIGNATED_COMPILED_FAULT_NOT_CAUGHT:'+fault.id);}
   else need(game.exit===0&&game.result.passed===true&&failed.length===0&&life.exit===0&&life.result.passed===true&&life.result.cases.every(c=>c.pass===true),'CURRENT_BUSINESS_CASE_FAILED:'+p.config);
   profiles.push({config:p.config,buildExit:p.build.exit,observationExit:game.exit,caseCount:game.result.cases.length,lifecycleExit:life.exit,lifecycleCases:life.result.cases.length,legacyCases:life.result.legacyCases.length,legacyFailures:life.result.legacyFailures,designatedAssertionFailed:!!fault&&failed.some(c=>c.name===fault.designatedCase),failedCases:failed});
  }
  need(proc.exitCode===(fault?1:0)&&r.status===(fault?'FAIL':'PASS'),'UNEXPECTED_PRODUCER_FINAL_STATUS:'+name);
  return {path:path.join(out,'result.json'),sha256:hash(path.join(out,'result.json')),profiles,expectedRejection:!!fault};
 }
 try{
  write(path.join(root,'inputs-before.json'),{sources,tools,spec});
  const positive=await suite('positive',path.join(REPO,'InputHost'));
  const negatives=[];
  for(const fault of spec.faults){
   need(['Program.cs','HostGameOutputObservation.cs'].includes(fault.file)&&/^[a-z0-9-]+$/.test(fault.id),'FAULT_WRITE_SCOPE');
   const sourceRoot=path.join(root,'fault-'+fault.id);fs.mkdirSync(sourceRoot);
   for(const row of sources.filter(r=>r.path.startsWith('InputHost/')&&r.path.endsWith('.cs'))){const p=sourcePath(REPO,row.path);need(hash(p)===row.sha256,'LIVE_HOST_SOURCE_CHANGED');fs.copyFileSync(p,path.join(sourceRoot,path.basename(p)));}
   const file=path.join(sourceRoot,fault.file),before=fs.readFileSync(file,'utf8');need(before.split(fault.from).length===2,'FAULT_ANCHOR_CHANGED:'+fault.id);
   fs.writeFileSync(file,before.replace(fault.from,fault.to));
   const receipt=await suite('negative-'+fault.id,sourceRoot,fault);negatives.push({id:fault.id,designatedCase:fault.designatedCase,...receipt});
  }
  need(sources.every(r=>hash(sourcePath(REPO,r.path))===r.sha256),'LIVE_SOURCE_CHANGED_DURING_RUN');need(tools.every(r=>hash(r.path)===r.sha256),'TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});
  const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,sourceStillCurrent:true,positive,negatives,sourceHashes:[...sources,...walk(root).map(file=>({path:file,sha256:hash(file)}))],toolchain:tools,boundary:{actualHostConsumerBodiesExecuted:true,actualObservationHelperExecuted:true,sdkEventAndPnPBoundaries:'inert matching fixtures',pinnedSdkArtifactHashedOnly:true,sdkCompileOnlyEvidenceNotIncludedInThisCase:true,realSdkConstructed:false,realHostStarted:false,physicalAcceptance:false,protocolDecoded:false,gameFeedbackForwardingProven:false,completeMotorQueue:false,wholeProcessCpuPercent:null,cpugpuConsumersChanged:false}};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{schemaVersion:1,id:CASE_ID,status:'FAIL',error:error.message,steps,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=process.argv.slice(2);need(args.length===2&&args[0]==='--evidence-dir','USE_EXPLICIT_NEW_EVIDENCE_DIR');const r=await execute(args[1]);console.log(JSON.stringify({id:r.id,status:r.status,positiveProfiles:r.positive.profiles.length,compiledSourceFaults:r.negatives.length,protocolDecoded:false,gameFeedbackForwardingProven:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
