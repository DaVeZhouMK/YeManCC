import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {runProcess} from '../runner.mjs';
import {motionConsumerInputs,REPO_ROOT} from './inputs.mjs';
import {PRODUCER_TIMEOUT_MS,ADAPTER_TIMEOUT_MS} from './contracts.mjs';
import {runMatrixSupplement} from './matrix-runner.mjs';
import {runOwnerSupplement} from './owner-runner.mjs';
import {validateOwnerStage} from './owner-evidence.mjs';
import {verifyMotionOriginalCompilerTraceParallel} from './original-compiler-trace.mjs';
import {validateMotionReport} from './validation.mjs';
import {hashFile,readJson,validateOutputRoot,verifyMotionEvidence,ownedTree} from './evidence.mjs';
export function fingerprintRequired(files){return files.map(file=>{try{if(!fs.statSync(file).isFile())throw Error();return{path:file,sha256:hashFile(file),status:'OK'};}catch{return{path:file,sha256:null,status:'MISSING_OR_NOT_FILE'};}});}
const changes=rows=>rows.filter(row=>{try{return hashFile(row.path)!==row.sha256;}catch{return true;}});
function childEnvironment(out){const result={};for(const name of ['SystemRoot','WINDIR','ComSpec','PATH','PATHEXT','ProgramFiles','ProgramFiles(x86)','ProgramData','USERPROFILE','NUMBER_OF_PROCESSORS','PROCESSOR_ARCHITECTURE'])if(process.env[name])result[name]=process.env[name];for(const [name,relative]of [['TEMP','process-temp'],['TMP','process-temp'],['LOCALAPPDATA','local-app-data'],['APPDATA','app-data']]){result[name]=path.join(out,relative);fs.mkdirSync(result[name],{recursive:true});}return result;}
function reportInputClosure(report,sourceInputs,externalInputs){const expected=new Map([...sourceInputs,...externalInputs].map(row=>[path.resolve(row.path).toLowerCase(),row.sha256]));for(const row of ['source','before','historical','capabilityBefore','legacyCatalog','legacyOracle','catalog','msi','runner'].map(name=>report[name]).concat(report.fixtures??[])){if(!row||typeof row.path!=='string'||expected.get(path.resolve(row.path).toLowerCase())!==row.sha256)throw Error('MOTION_UNRECORDED_PRODUCER_INPUT');}}
/** The public adapter runs the original producer; no replay-only acceptance mode. */
export async function runMotionConsumers({evidenceDir}={}) {
 const out=validateOutputRoot(evidenceDir,REPO_ROOT);fs.mkdirSync(out,{recursive:false});
 const result={schemaVersion:1,kind:'motion-consumer-simulation',status:'BLOCKED',startedAt:new Date().toISOString(),sourceHashes:[],sourceChanges:[],externalInputs:[],externalInputChanges:[],cases:[],claims:{physicalHardwareValidated:false,operatingSystemSleepPerformed:false,installedApplicationExercised:false,fullProductAcceptance:false},limits:['Extracted original C++ consumers and memory sinks; not full BUS, physical axes, driver lifetime, Host or actual OS sleep.','All original consumers, current capability matrix, fixed historical HC oracle and 39 Release/Checked semantic faults, original matrix/four controls and primary-owner/five dual-configuration faults remain mandatory.']};
 let sourceInputs=[],externalInputs=[];
 try {
  if(process.env.YMCC_SIM_GLOBAL_RUN!=='1')throw Error('GLOBAL_OWNED_JOB_REQUIRED');
  const inputs=motionConsumerInputs(REPO_ROOT);sourceInputs=fingerprintRequired(inputs.sources.map(file=>path.join(REPO_ROOT,file)));externalInputs=fingerprintRequired(inputs.artifacts);
  result.sourceHashes=sourceInputs.map(row=>({path:path.relative(REPO_ROOT,row.path).replaceAll('\\','/'),sha256:row.sha256}));result.externalInputs=externalInputs;
  const missing=[...sourceInputs,...externalInputs].filter(row=>row.status!=='OK');if(missing.length){result.missingInputs=missing.map(row=>row.path);throw Error('MOTION_REQUIRED_INPUT_MISSING');}
  if(inputs.oracleProblem||!inputs.expected)throw Error('MOTION_ORACLE_INTEGRITY_REJECTED');
  if(inputs.toolchainProblem||!inputs.tools)throw Error('MOTION_MATRIX_TOOL_ALLOWLIST_REJECTED');
  for(const pin of inputs.tools.files)if(externalInputs.find(row=>path.resolve(row.path).toLowerCase()===path.resolve(pin.path).toLowerCase())?.sha256!==pin.sha256)throw Error('MOTION_MATRIX_TOOL_PIN_MISMATCH');
  const capturedMain=fs.readFileSync(path.join(REPO_ROOT,'native/main.cpp'));
  const raw=path.join(out,'r'),args=[path.join(REPO_ROOT,'tools/hc_model_motion_consumer_selftest.mjs'),'--mutation-checks','--mutation-jobs','4','--source',path.join(REPO_ROOT,'native/main.cpp'),'--before',path.join(inputs.workspace,'Build/Validation/HC-ModelCapabilities-20261017/LegacyMotion/main.before.cpp'),'--historical-before',path.join(inputs.workspace,'Build/Validation/HC-ModelCapabilities-20261017/MotionConsumer/main.before.cpp'),'--capability-before',path.join(inputs.workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/main.before.cpp'),'--out',raw];
  const call=await runProcess(process.execPath,args,{cwd:REPO_ROOT,env:childEnvironment(out),timeoutMs:PRODUCER_TIMEOUT_MS,outputLimit:2*1024*1024});
  fs.writeFileSync(path.join(out,'producer.stdout.log'),call.stdout);fs.writeFileSync(path.join(out,'producer.stderr.log'),call.stderr);
  const {stdout,stderr,...processProof}=call;result.process=processProof;result.producer={executable:process.execPath,args,timeoutMs:PRODUCER_TIMEOUT_MS};
  if(call.exitCode!==0||call.timedOut||call.signal||call.launchError||call.outputTruncated)throw Error('MOTION_PRODUCER_EXECUTION_FAILED');
  const reportFile=path.join(raw,'source-bindings.json'),report=readJson(reportFile),expectations=readJson(path.join(raw,'expectations.json'));
  if(path.resolve(report.out)!==path.resolve(raw))throw Error('MOTION_PRODUCER_OUTPUT_ROOT_DIFFERS');
  reportInputClosure(report,sourceInputs,externalInputs);result.validation=validateMotionReport(report,expectations);if(result.validation.status!=='PASS')throw Error('MOTION_STRUCTURED_REPORT_REJECTED');
  result.executionEvidence=verifyMotionEvidence(raw,report,inputs.expected);
  result.originalCompilerTrace=await verifyMotionOriginalCompilerTraceParallel({repo:REPO_ROOT,rawRoot:raw,report,sourceInputs,externalInputs,tools:inputs.tools},{timeoutMs:Math.max(1,ADAPTER_TIMEOUT_MS-(Date.now()-Date.parse(result.startedAt))-10000)});
  const matrix=await runMatrixSupplement({repo:REPO_ROOT,out:path.join(out,'q'),generated:raw,source:path.join(REPO_ROOT,'native/main.cpp'),sourceBytes:capturedMain,sourceInputs,externalInputs,tools:inputs.tools,generatedClosure:result.executionEvidence.closure,env:childEnvironment(out),adapterStartedAt:Date.parse(result.startedAt)});
  result.matrixEvidence={path:path.join(out,'q/result.json'),sha256:hashFile(path.join(out,'q/result.json')),status:matrix.status,positiveConfigurations:matrix.positive?.configurations.map(c=>c.profile)??[],semanticSourceFaultsDetected:matrix.semanticSourceFaultsDetected??0,logicalLookupRegressionDetected:matrix.logicalLookupRegressionDetected??0,budget:matrix.budget,failure:matrix.failure};
  if(matrix.status!=='PASS')throw Object.assign(Error('MOTION_MATRIX_SUPPLEMENT_REJECTED'),{evidence:{stage:'matrix',resultFile:path.join(out,'q/result.json'),failure:matrix.failure,secondaryFailures:matrix.secondaryFailures??[]}});
  result.sourceHashes.push(...matrix.closure);
  const owner=await runOwnerSupplement({repo:REPO_ROOT,out:path.join(out,'o'),generated:raw,source:path.join(REPO_ROOT,'native/main.cpp'),sourceBytes:capturedMain,sourceInputs,externalInputs,tools:inputs.tools,generatedClosure:result.executionEvidence.closure,env:childEnvironment(out),adapterStartedAt:Date.parse(result.startedAt)});
  result.ownerEvidence={path:path.join(out,'o/result.json'),sha256:hashFile(path.join(out,'o/result.json')),status:owner.status,positiveConfigurations:owner.positive?.configurations.map(c=>c.mode)??[],semanticSourceFaultsDetected:owner.semanticSourceFaultsDetected??0,logicalLookupRegressionDetected:owner.logicalLookupRegressionDetected??0,budget:owner.budget,failure:owner.failure};
  if(owner.status!=='PASS')throw Object.assign(Error('MOTION_PRIMARY_OWNER_SUPPLEMENT_REJECTED'),{evidence:{stage:'primary-owner',resultFile:path.join(out,'o/result.json'),failure:owner.failure,closureFailures:owner.closureFailures??[],finalizationFailures:owner.finalizationFailures??[]}});
  result.ownerValidation=validateOwnerStage(owner);result.sourceHashes.push(...owner.closure);
  result.sourceChanges=changes(sourceInputs);result.externalInputChanges=changes(externalInputs);if(result.sourceChanges.length||result.externalInputChanges.length)throw Error('MOTION_SOURCE_OR_REFERENCE_CHANGED');
  result.sourceHashes.push(...result.executionEvidence.closure,...['producer.stdout.log','producer.stderr.log'].map(name=>({path:path.join(out,name),sha256:hashFile(path.join(out,name))})));
  result.cases=report.configurations.map(config=>({id:config.name,status:'PASS',checks:config.report.checks,originalConsumerAssertions:result.validation.subtotals.find(row=>row.name===config.name).originalConsumerAssertions,executableSha256:config.executableSha256}));
  result.cases.push(...report.mutations.map(mutation=>({id:'mutation.'+mutation.name,status:'PASS',actualSemanticDetection:true,configurations:mutation.configurations})));
  result.summary={positiveConfigurations:2,checksPerConfiguration:report.configurations.map(config=>({name:config.name,total:config.report.checks,consumer:config.report.consumerChecks,originalConsumerAssertions:result.validation.subtotals.find(row=>row.name===config.name).originalConsumerAssertions})),compiledSemanticFaults:report.mutations.length,originalReportsAndFiles:result.executionEvidence.closure.length,externalOracleInputs:externalInputs.length,primaryOwnerPositiveConfigurations:2,primaryOwnerSemanticSourceFaults:4,primaryOwnerLogicalLookupRegressions:1,primaryOwnerFaultConfigurations:10};
  result.originalEvidence={path:reportFile,sha256:hashFile(reportFile),oracleSha256:report.oracleSha256,generatedCppSha256:report.generatedCppSha256};result.status='PASS';
 }catch(error){
  // A producer can fail before the success-only final check. Preserve actual source drift on that path too.
  result.sourceChanges=changes(sourceInputs);result.externalInputChanges=changes(externalInputs);
  result.inputSnapshotCaptured=sourceInputs.length>0;
  const code=/^[A-Z_]{1,100}$/.test(error.message)?error.message:'MOTION_EVIDENCE_OR_VALIDATION_FAILED';
  result.status=['GLOBAL_OWNED_JOB_REQUIRED','MOTION_REQUIRED_INPUT_MISSING'].includes(code)?'BLOCKED':'FAIL';result.failure={code,detail:String(error.message).slice(0,400),...(error.code?{systemCode:error.code}:{}),...(error.evidence?{evidence:error.evidence}:{})};
 }
 result.finishedAt=new Date().toISOString();fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;
}
export function parseArguments(args){if(args.length!==2||args[0]!=='--evidence-dir'||!args[1]||args[1].startsWith('--'))throw Error('MOTION_ARGUMENTS_INVALID');return{evidenceDir:args[1]};}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const result=await runMotionConsumers(parseArguments(process.argv.slice(2)));console.log(JSON.stringify({status:result.status,summary:result.summary,failure:result.failure}));process.exitCode=result.status==='PASS'?0:result.status==='BLOCKED'?10:2;}
 catch(error){console.error(/^[A-Z_]{1,100}$/.test(error.message)?error.message:'MOTION_ARGUMENTS_OR_OUTPUT_INVALID');process.exitCode=3;}
}
