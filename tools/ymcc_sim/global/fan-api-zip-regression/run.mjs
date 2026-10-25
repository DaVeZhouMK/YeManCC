/** Reuse the original ordinary-ZIP CLI and existing NoIO product fixture. */
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {MODULE,SOURCE_INPUTS} from './adapter.mjs';
import {discoverTools,toolRow,loadedCommonJsTools,verifyToolRows,hashFile} from './toolchain.mjs';
import {assertCliCompletion,assertScope,assertPositiveReceipt,assertConflictReceipt,assertMissingSchemaReceipt} from './assertions.mjs';
import {generateFanApiFixture} from '../fan-api-log-consumer/fixture.mjs';
import {runProcess,checkStructuredEvidence,sourcePath} from '../runner.mjs';
const ownRoot=path.dirname(fileURLToPath(import.meta.url));
const defaultRepo=path.resolve(ownRoot,'../../../..');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const relative=(root,file)=>path.relative(root,file).replaceAll('\\','/');
const inside=(root,file)=>{const r=path.relative(root,file);return r!== '..'&&!r.startsWith('..'+path.sep)&&!path.isAbsolute(r);};
function filesIn(root) {
 const files=[];
 const visit=dir=>{for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);if(fs.lstatSync(file).isSymbolicLink())throw Error('OWN_EVIDENCE_REPARSE_PATH');if(entry.isDirectory())visit(file);else if(entry.isFile())files.push(file);else throw Error('OWN_EVIDENCE_NOT_FILE');}};
 visit(root);return files.sort();
}
function pythonProcess(tools,args,input) {
 const child=spawnSync(tools.python,['-I','-B',...args],{input,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:1024*1024});
 return {pid:child.pid??null,exitCode:child.status,signal:child.signal,launchError:child.error?.code??null,timedOut:child.error?.code==='ETIMEDOUT',stdout:child.stdout??'',stderr:child.stderr??''};
}
function assertPythonSuccess(row) {assert.equal(row.exitCode,0);assert.equal(row.signal,null);assert.equal(row.launchError,null);assert.equal(row.timedOut,false);}
export async function runFanApiZipRegression({evidenceDir,repoRoot=defaultRepo,pythonPath}={}) {
 if(typeof evidenceDir!=='string'||!path.isAbsolute(evidenceDir)||/[\0\r\n]/.test(evidenceDir)||fs.existsSync(evidenceDir))throw Error('FRESH_ABSOLUTE_EVIDENCE_DIR_REQUIRED');
 if(fs.realpathSync(repoRoot)!==fs.realpathSync(defaultRepo))throw Error('MODULE_REPOSITORY_MUST_MATCH');
 fs.mkdirSync(evidenceDir,{recursive:true});
 const cases=[],executions=[],originalFiles=[],start=new Date().toISOString();let tools=null,sourceBefore=[],toolBefore=[],producer=null,blockedReason=null,failure=null;
 const attempt=async(id,fn)=>{try{const details=await fn();cases.push({id,passed:true,...details});return true;}catch(error){cases.push({id,passed:false,assertionCode:error.code??'REGRESSION_ASSERTION_FAILED',assertion:error.message});return false;}};
 try{tools=discoverTools(repoRoot,pythonPath);}catch(error){blockedReason=error.message;}
 if(!blockedReason) try {
  const sourceFiles=[...new Set([...SOURCE_INPUTS,'tools/ymcc_sim/global/extra-adapters.mjs','tools/ymcc_sim/global/candidate-options.mjs'])];
  sourceBefore=sourceFiles.map(file=>({path:file,sha256:hashFile(sourcePath(repoRoot,file))}));
  // The production fixtures use these same CJS packages. Warm them before the
  // input fence, so the CLI observer can be checked against pre-execution bytes.
  const require=createRequire(path.join(repoRoot,'package.json'));require('typescript');require('vue');
  toolBefore=[...new Map([...tools.rows,...loadedCommonJsTools(repoRoot)].map(row=>[row.path,row])).values()];
  write(path.join(evidenceDir,'input-fence-before.json'),{sourceHashes:sourceBefore,toolInputs:toolBefore});
  const version=pythonProcess(tools,['-c','import json,sys; print(json.dumps({"executable":sys.executable,"version":sys.version,"isolated":sys.flags.isolated}))']);
  write(path.join(evidenceDir,'python-version-execution.json'),version);assertPythonSuccess(version);tools.pythonRuntime=JSON.parse(version.stdout);assert.equal(tools.pythonRuntime.isolated,1);assert.equal(fs.realpathSync(tools.pythonRuntime.executable),tools.python);
  const sample=await generateFanApiFixture({repoRoot});producer=sample.producer;
  assert.equal(producer.startRejected,false);assert.equal(producer.sourceUnchanged,true);assert.equal(producer.boundaryViolations,0);assert.equal(producer.actualHostStarted,false);assert.equal(producer.hardwareIo,false);
  fs.writeFileSync(path.join(evidenceDir,'original-source-derived-projection.jsonl'),sample.jsonl,{flag:'wx'});
  write(path.join(evidenceDir,'producer-proof.json'),{...producer,sourceIdentity:sample.sourceIdentity,syntheticNativeEnvelope:true,nativeProducerExecuted:false});
  const epoch=Date.parse('2026-10-19T00:00:00.000Z');
  const envelope=records=>records.map(row=>({time:new Date(epoch+row.atMs).toISOString(),event:row.event,details:row.details}));
  const jsonl=records=>envelope(records).map(row=>JSON.stringify(row)).join('\n')+'\n';
  const good=structuredClone(sample.records),conflict=structuredClone(good),legacy=structuredClone(good.slice(0,1));
  conflict[0].details.receipt.ok=!conflict[0].details.response.ok;
  delete legacy[0].details.observationSchemaVersion;
  const variants=[
   {id:'source-derived-ordinary-zip',entries:[{name:'logs/fan-api.log',data:jsonl(good)}],exit:0,status:'PASS',verify:assertPositiveReceipt},
   {id:'root-response-contradiction',entries:[{name:'logs/fan-api.log',data:jsonl(conflict)}],exit:2,status:'BLOCKED',verify:assertConflictReceipt},
   {id:'missing-schema-isolated-batch',entries:[{name:'logs/fan-api.log',data:jsonl(legacy)},{name:'logs/fan-api.log.1',data:jsonl(good)}],exit:10,status:'INCOMPLETE',verify:assertMissingSchemaReceipt},
  ];
  const completed=new Map();
  for(const variant of variants) await attempt(variant.id,async()=>{
   const dir=path.join(evidenceDir,variant.id);fs.mkdirSync(dir);
   const request={root:dir,name:'source-derived-ordinary-log.zip',entries:variant.entries};write(path.join(dir,'synthetic-zip-request.json'),request);
   const created=pythonProcess(tools,[path.join(ownRoot,'write-fixture.py')],JSON.stringify(request));write(path.join(dir,'zip-writer-execution.json'),created);assertPythonSuccess(created);assert.equal(JSON.parse(created.stdout).status,'SYNTHETIC_ZIP_WRITTEN');
   const archive=path.join(dir,request.name),archiveSha=hashFile(archive),cliOut=path.join(dir,'original-cli'),toolReceipt=path.join(dir,'actual-cli-commonjs-tools.json');
   const args=['--require',path.join(ownRoot,'observe-tools.cjs'),path.join(repoRoot,'tools/ymcc_sim/global/cli.mjs'),'--mode','signal-check','--domain','fan','--archive',archive,'--python',tools.python,'--out-dir',cliOut];
   const env={...process.env,YMCC_FAN_ZIP_TOOL_RECEIPT:toolReceipt};delete env.NODE_OPTIONS;delete env.NODE_PATH;
   const row=await runProcess(tools.node,args,{cwd:repoRoot,env,timeoutMs:45000});
   const {stdout,stderr,...processResult}=row;fs.writeFileSync(path.join(dir,'stdout.log'),stdout,{flag:'wx'});fs.writeFileSync(path.join(dir,'stderr.log'),stderr,{flag:'wx'});
   const execution={id:variant.id,executable:tools.node,argv:args,...processResult,expectedExitCode:variant.exit,expectedTerminalStatus:variant.status};executions.push(execution);write(path.join(dir,'cli-execution.json'),execution);
   const resultLines=stdout.split(/\r?\n/).filter(line=>line.startsWith('RESULT '));assert.equal(resultLines.length,1,'ACTUAL_CLI_RESULT_REQUIRED');
   const signalFile=path.resolve(resultLines[0].slice(7));assert(inside(cliOut,signalFile),'ACTUAL_RESULT_MUST_BELONG_TO_THIS_CLI_RUN');
   assertCliCompletion(row,stdout,variant.exit,variant.status,signalFile);
   const planFile=path.join(path.dirname(signalFile),'log-replay-plan.json'),plan=read(planFile),signal=read(signalFile);
   assertScope(plan,signal,archiveSha);const details=variant.verify(plan,signal);
   assert.equal(hashFile(archive),archiveSha,'ARCHIVE_CHANGED_AFTER_CONSUMPTION');
   const observed=read(toolReceipt);assert.equal(observed.exitCode,row.exitCode);assert.equal(observed.nodeExecutable,tools.node);assert.equal(observed.nodeVersion,tools.nodeVersion);
   const approved=new Map(toolBefore.map(item=>[item.path,item.sha256]));
   for(const item of observed.files){assert.equal(approved.get(item.path),item.sha256,'CJS_TOOL_NOT_BOUND_BEFORE_CLI');assert.equal(hashFile(item.path),item.sha256,'CJS_TOOL_CHANGED_AFTER_CLI');}
   if(variant.id!=='root-response-contradiction'){assert(observed.files.some(item=>item.path===tools.packages.typescript.main));assert(observed.files.some(item=>item.path===tools.packages.vue.main));}
   assert(!observed.files.some(item=>/[\\/]esbuild[\\/]lib[\\/]main\.js$/i.test(item.path)),'ESBUILD_MUST_NOT_BE_CLAIMED_AS_CONSUMED');
   for(const input of [...plan.sourceHashes,...signal.results.flatMap(row=>row.result?.sourceHashes??[])]){assert.equal(hashFile(sourcePath(repoRoot,input.path)),input.sha256,'ORIGINAL_CONSUMER_SOURCE_STALE');assert.equal(sourceBefore.find(row=>row.path===input.path)?.sha256,input.sha256,'ORIGINAL_CONSUMER_INPUT_NOT_BOUND');}
   originalFiles.push({path:archive,sha256:archiveSha},{path:planFile,sha256:hashFile(planFile)},{path:signalFile,sha256:hashFile(signalFile)});
   completed.set(variant.id,{plan,signal,signalFile,planFile,archive,archiveSha});
   return {observedPlanStatus:plan.status,observedSignalStatus:signal.status,originalExitCode:row.exitCode,originalPid:row.pid,originalResult:signalFile,...details};
  });
  await attempt('public-reader-actual-output-byte-guard',async()=>{
   const positive=completed.get('source-derived-ordinary-zip');assert(positive,'ACTUAL_POSITIVE_RESULT_REQUIRED');
   const dir=path.join(evidenceDir,'public-byte-guard');fs.mkdirSync(dir);const copy=path.join(dir,'copy-of-actual-signal-result.json'),proof=path.join(dir,'proof.json'),original=fs.readFileSync(positive.signalFile),originalSha=hashFile(positive.signalFile);fs.writeFileSync(copy,original,{flag:'wx'});
   write(proof,{schemaVersion:1,status:'PASS',sourceHashes:[{path:copy,sha256:originalSha}],originalSource:positive.signalFile});
   const readProof=()=>checkStructuredEvidence(evidenceDir,{resultFiles:['public-byte-guard/proof.json']},repoRoot);
   const accepted=readProof();assert.equal(accepted.missing,false);assert.equal(accepted.failed,false);
   let rejected;try{fs.appendFileSync(copy,' ');rejected=readProof();assert.equal(rejected.missing,false);assert.equal(rejected.failed,true);assert.deepEqual(rejected.paths[0].sourceMismatches,[copy]);}finally{fs.writeFileSync(copy,original);}
   const restored=readProof();assert.equal(restored.failed,false);assert.equal(restored.missing,false);assert.equal(hashFile(positive.signalFile),originalSha);assert.equal(hashFile(copy),originalSha);
   write(path.join(dir,'byte-guard-result.json'),{status:'PASS',accepted,rejected,restored,originalUnchanged:true,mutation:'one ASCII space in ordinary output copy',originalSource:positive.signalFile,originalSha256:originalSha});
   return {assertionMeaning:'actual output bytes bound through unchanged public reader; original signal output unchanged'};
  });
 } catch(error) {failure={code:error.code??'ADAPTER_EXECUTION_FAILED',message:error.message};}
 const sourceAfter=sourceBefore.map(row=>{try{return {path:row.path,sha256:hashFile(sourcePath(repoRoot,row.path))};}catch{return {path:row.path,sha256:null};}});
 const sourceChanges=sourceBefore.filter((row,index)=>row.sha256!==sourceAfter[index]?.sha256).map(row=>row.path),toolChanges=verifyToolRows(toolBefore);
 if(!blockedReason)write(path.join(evidenceDir,'input-fence-after.json'),{sourceHashes:sourceAfter,sourceChanges,toolChanges});
 const toolSources=toolBefore.filter(row=>inside(repoRoot,row.path)).map(row=>({path:relative(repoRoot,row.path),sha256:row.sha256}));
 const artifacts=filesIn(evidenceDir).map(file=>({path:file,sha256:hashFile(file)}));
 const status=blockedReason?'BLOCKED':failure||sourceChanges.length||toolChanges.length||cases.length!==4||cases.some(row=>!row.passed)?'FAIL':'PASS';
 const report={schemaVersion:1,kind:'fan-api-ordinary-zip-regression',status,startedAt:start,finishedAt:new Date().toISOString(),cases,executions,producer,tools,
  sourceHashes:[...new Map([...sourceBefore,...toolSources,...artifacts].map(row=>[row.path,row])).values()],sourceChanges,toolChanges,toolClosureBefore:toolBefore,
  originalArtifacts:originalFiles,blockedReason,failure,
  scope:{producer:'unchanged product fanApi/fanDiagnostics with inert NoIO server',envelope:'synthetic native-shaped timestamps, not native producer execution',entry:'original signal-check CLI',ordinaryZipConnected:cases.some(row=>row.id==='source-derived-ordinary-zip'&&row.passed),actualHostStarted:false,userArchiveRead:false,installedApplicationMutated:false,externalNetwork:false,hardwareIo:false,realOperatingSystemSleep:false,physicalReceipts:0,compiledProductArtifacts:'VM transpilation only; no DLL/PDB/disk JS bundle',esbuildExecuted:false,fullGoalComplete:false}};
 write(path.join(evidenceDir,'fan-api-zip-regression-result.json'),report);return report;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
 const options={},keys={'--evidence-dir':'evidenceDir','--python':'pythonPath'};
 try{for(let i=2;i<process.argv.length;i++){const key=keys[process.argv[i]];if(!key||options[key]!==undefined||!process.argv[i+1])throw Error('EXPLICIT_UNIQUE_ARGUMENTS_REQUIRED');options[key]=process.argv[++i];}
  const report=await runFanApiZipRegression(options);console.log('FAN_API_ZIP_REGRESSION '+report.status);console.log('RESULT '+path.join(options.evidenceDir,'fan-api-zip-regression-result.json'));process.exitCode=report.status==='PASS'?0:report.status==='BLOCKED'?10:2;
 }catch(error){console.error('FAN_API_ZIP_REGRESSION FAIL '+error.message);process.exitCode=2;}
}
