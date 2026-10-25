import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {MANIFEST_FILE,LEGACY_ARTIFACT_FLOOR,INTEGRITY_CASES,sha,validateArtifactManifest,verifyBaseline,
 copyBaseline,applyIntegrityFault,auditVariant,classifyIntegrityRun,snapshotArtifacts} from './hc_capability_catalog_integrity.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(process.argv.length!==4||process.argv[2]!=='--out')throw Error('Usage: --out isolated-evidence-directory');
const out=path.resolve(process.argv[3]),rel=path.relative(repo,out);
if(!rel||(!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel)))throw Error('Evidence must be outside production source');
if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Evidence directory must be new or empty');
fs.mkdirSync(out,{recursive:true});
const realOut=fs.realpathSync(out),realRepo=fs.realpathSync(repo),realRel=path.relative(realRepo,realOut);
if(!realRel||(!realRel.startsWith('..'+path.sep)&&!path.isAbsolute(realRel)))throw Error('Evidence resolves inside production source');
const write=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,typeof value==='string'?value:JSON.stringify(value,null,2)+'\n');};
const toolFiles=['tools/hc_capability_catalog_generate.mjs','tools/hc_capability_catalog_selftest.mjs','tools/hc_capability_catalog_integrity.mjs'];
const toolSnapshot=()=>toolFiles.map(file=>{const bytes=fs.readFileSync(path.join(repo,file));return {path:file,size:bytes.length,sha256:sha(bytes)};});
const toolBefore=toolSnapshot(),cases=[];
function runCheck(dir,fixture){
 fs.mkdirSync(dir,{recursive:true});
 const args=['tools/hc_capability_catalog_generate.mjs','--check',...(fixture?['--generated-root',fixture]:[]),'--out',path.join(dir,'extractor')];
 const startedAt=new Date().toISOString();
 const run=spawnSync(process.execPath,args,{cwd:repo,windowsHide:true,encoding:'utf8',timeout:120000,maxBuffer:4*1024*1024});
 const endedAt=new Date().toISOString(),stdout=run.stdout??'',stderr=run.stderr??'';
 write(path.join(dir,'stdout.log'),stdout);write(path.join(dir,'stderr.log'),stderr);
 write(path.join(dir,'check.log'),stdout+stderr+(run.error?String(run.error):''));
 const receipt={executable:process.execPath,args,cwd:repo,pid:run.pid??null,startedAt,endedAt,status:run.status,signal:run.signal,error:run.error?{code:run.error.code,message:run.error.message}:null,
  stdout:{path:'stdout.log',size:Buffer.byteLength(stdout),sha256:sha(stdout)},stderr:{path:'stderr.log',size:Buffer.byteLength(stderr),sha256:sha(stderr)}};
 write(path.join(dir,'process.json'),receipt);return {...run,receipt};
}
let manifest,manifestBytes,manifestPath,before,after,failure;
try{
 // Only --check of the real repository may establish a baseline; generation never repairs the input.
 const baselineRun=runCheck(path.join(out,'baseline'));
 if(baselineRun.status!==0||baselineRun.error||baselineRun.signal)throw Error('CATALOG_BASELINE_STRICT_CHECK_FAILED');
 let report;try{report=JSON.parse(baselineRun.stdout.trim());}catch{}
 if(report?.status!=='CURRENT'||report.hardwareIo!==false||report.assemblyLoaded!==false)throw Error('CATALOG_BASELINE_REPORT_INVALID');
 manifestPath=path.join(out,'baseline','extractor',MANIFEST_FILE);manifestBytes=fs.readFileSync(manifestPath);
 manifest=validateArtifactManifest(JSON.parse(manifestBytes),{requiredPaths:LEGACY_ARTIFACT_FLOOR});
 before=verifyBaseline(repo,manifest);
 write(path.join(out,'baseline','verified.json'),{status:'PASS',strictCheck:true,manifest:{path:manifestPath,sha256:sha(manifestBytes)},artifacts:before,process:baselineRun.receipt});
 for(const spec of INTEGRITY_CASES){
  const dir=path.join(out,spec.id),fixture=path.join(dir,'generated');
  try{
   copyBaseline(repo,fixture,manifest);
   const variant=applyIntegrityFault(fixture,manifest,spec);write(path.join(dir,'mutation.json'),variant);
   const run=runCheck(dir,fixture),oracle=classifyIntegrityRun(run,manifest,spec);
   const afterVariant=auditVariant(fixture,manifest,spec,{expectedSnapshot:variant.snapshot});write(path.join(dir,'mutation-after.json'),afterVariant);
   if(spec.kind==='current'&&oracle.accepted){
    const actual=JSON.parse(fs.readFileSync(path.join(dir,'extractor',MANIFEST_FILE),'utf8'));
    validateArtifactManifest(actual,{expectedManifest:manifest,requiredPaths:LEGACY_ARTIFACT_FLOOR});
   }
   cases.push({id:spec.id,status:oracle.accepted?'PASS':'FAIL',actualExitCode:run.status,expectedRejection:spec.id!=='current',...oracle,
    mutationEvidence:{before:'mutation.json',after:'mutation-after.json',unchangedArtifacts:variant.unchangedArtifacts,targetPaths:variant.targetPaths},process:'process.json'});
  }catch(error){cases.push({id:spec.id,status:'FAIL',expectedRejection:spec.id!=='current',error:error.message});}
 }
 after=verifyBaseline(repo,manifest);
 if(!fs.readFileSync(manifestPath).equals(manifestBytes))throw Error('CATALOG_BASELINE_MANIFEST_CHANGED');
 validateArtifactManifest(JSON.parse(fs.readFileSync(manifestPath)),{expectedManifest:manifest,requiredPaths:LEGACY_ARTIFACT_FLOOR});
}catch(error){failure={message:error.message};if(manifest)try{after=snapshotArtifacts(repo,manifest);}catch(snapshotError){failure.snapshotError=snapshotError.message;}}
const toolsAfter=toolSnapshot();
const productionUnchanged=Boolean(before&&after&&JSON.stringify(before)===JSON.stringify(after));
const toolSourcesUnchanged=JSON.stringify(toolBefore)===JSON.stringify(toolsAfter);
const result={status:!failure&&productionUnchanged&&toolSourcesUnchanged&&cases.length===INTEGRITY_CASES.length&&cases.every(c=>c.status==='PASS')?'PASS':'FAIL',
 cases,productionUnchanged,toolSourcesUnchanged,sourceInputs:after??before??[],sourceInputsBefore:before??[],toolSourcesBefore:toolBefore,toolSourcesAfter:toolsAfter,
 baselineManifest:manifestBytes?{path:manifestPath,sha256:sha(manifestBytes),artifactCount:manifest.artifactCount}:null,
 failure:failure??null,hardwareIo:false,assemblyLoaded:false};
write(path.join(out,'result.json'),result);console.log(JSON.stringify(result));if(result.status!=='PASS')process.exitCode=1;
