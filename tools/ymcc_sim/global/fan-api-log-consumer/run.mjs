/** Explicit standalone entry. Ordinary ZIP/log-corpus registration is a later owner task. */
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';
import {consumeFanApiRecords,FAN_API_LOG_LIMITS} from './index.mjs';import {generateFanApiFixture} from './fixture.mjs';
const ownRoot=path.dirname(fileURLToPath(import.meta.url)),repoRoot=path.resolve(ownRoot,'../../../..');
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export async function runFanApiLogConsumer({evidenceDir,inputJsonl,sourceSha256,fixture=false}={}) {
 if(typeof evidenceDir!=='string'||!path.isAbsolute(evidenceDir)||fs.existsSync(evidenceDir))throw Error('FRESH_ABSOLUTE_EVIDENCE_DIR_REQUIRED');
 if(fixture===Boolean(inputJsonl))throw Error('CHOOSE_FIXTURE_OR_EXPLICIT_JSONL');
 const sources=['normalize.mjs','index.mjs','fixture.mjs','run.mjs'].map(name=>({path:'tools/ymcc_sim/global/fan-api-log-consumer/'+name,sha256:sha(path.join(ownRoot,name))}));
 fs.mkdirSync(evidenceDir,{recursive:true});let bytes,identity,producer=null;
 if(fixture){const sample=await generateFanApiFixture({repoRoot});bytes=sample.jsonl;identity=sample.sourceIdentity;producer=sample.producer;}
 else {
  if(typeof inputJsonl!=='string'||!path.isAbsolute(inputJsonl)||path.extname(inputJsonl).toLowerCase()!=='.jsonl'||!/^[a-f0-9]{64}$/.test(sourceSha256??''))throw Error('EXPLICIT_JSONL_AND_SOURCE_SHA_REQUIRED');
  if(fs.lstatSync(inputJsonl).isSymbolicLink()||!fs.statSync(inputJsonl).isFile()||fs.statSync(inputJsonl).size>FAN_API_LOG_LIMITS.maxBytes)throw Error('BOUNDED_REGULAR_JSONL_REQUIRED');
  bytes=fs.readFileSync(inputJsonl);identity={sha256:sourceSha256,kind:'synthetic-json'};
 }
 const result=await consumeFanApiRecords(bytes,{repoRoot,sourceIdentity:identity});
 // Only the self-generated fixture is archived verbatim. Generic input is never returned/copied.
 if(fixture)fs.writeFileSync(path.join(evidenceDir,'original-source-derived-projection.jsonl'),bytes);
 const sourceHashes=new Map([...sources,...(producer?.sourceHashes??[]),...(result.consumer.sourceHashes??[])].map(r=>[r.path,r]));
 const sourceChanges=[...sourceHashes.values()].filter(r=>sha(path.join(repoRoot,r.path))!==r.sha256).map(r=>r.path);
 const report={schemaVersion:1,kind:'fan-api-log-consumer',status:sourceChanges.length?'FAIL':result.status,mode:fixture?'source-derived-fixture':'explicit-bounded-jsonl',producer,result,
  sourceHashes:[...sourceHashes.values()],sourceChanges,ordinaryZipConnected:false,originalFieldActionsReplayed:false,hardwareIo:false,fullGoalComplete:false};
 if(fixture)report.sourceHashes.push({path:path.join(evidenceDir,'original-source-derived-projection.jsonl'),sha256:createHash('sha256').update(bytes).digest('hex')});
 fs.writeFileSync(path.join(evidenceDir,'fan-api-log-consumer-result.json'),JSON.stringify(report,null,2)+'\n');
 return report;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
 const args=process.argv.slice(2),options={};
 for(let i=0;i<args.length;i++){
  if(args[i]==='--fixture'){if(options.fixture)throw Error('DUPLICATE_ARGUMENT');options.fixture=true;continue;}
  const keys={'--evidence-dir':'evidenceDir','--input-jsonl':'inputJsonl','--source-sha256':'sourceSha256'};const key=keys[args[i]];
  if(!key||options[key]!==undefined||!args[i+1])throw Error('EXPLICIT_ARGUMENTS_REQUIRED');options[key]=args[++i];
 }
 const result=await runFanApiLogConsumer(options);console.log(JSON.stringify({status:result.status,events:result.result.normalization.events.length,consumerInvoked:result.result.consumer.invoked,ordinaryZipConnected:false,fullGoalComplete:false}));process.exitCode=result.status==='PASS'?0:result.status==='INCOMPLETE'?10:2;
}