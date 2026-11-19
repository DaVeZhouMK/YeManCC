import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath,pathToFileURL} from 'node:url';
import {runProcess} from '../runner.mjs';
import {lockBundledNodeTools,verifyBundledToolScope,BUNDLED_AUTHORITY_SOURCES} from '../bundled-node-tool-authority.mjs';
export const MODULE='tools/ymcc_sim/global/monitor-owned-close-consumer',moduleRoot=path.dirname(fileURLToPath(import.meta.url)),repoRoot=path.resolve(moduleRoot,'../../../..');
export const ownFiles=['adapter.mjs','run.mjs','runtime.mjs','spec.json','README.md'];
export const spec=JSON.parse(fs.readFileSync(path.join(moduleRoot,'spec.json'),'utf8'));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),digest=p=>hash(fs.readFileSync(p)),need=(ok,code)=>{if(!ok)throw Error(code);};
export function declaredSources(){return [...new Set(['package.json','tsconfig.json',...Object.keys(spec.syntheticPorts),...BUNDLED_AUTHORITY_SOURCES,...ownFiles.map(f=>MODULE+'/'+f),...spec.productSources,...spec.suites.map(s=>s.script)])];}
export function validateReport(suite,report){
 need(report&&JSON.stringify(report.cases)===JSON.stringify(suite.cases),'ORIGINAL_CASE_MEMBERSHIP_CHANGED');
 const empty=value=>Array.isArray(value)&&value.length===0;
 // The host publishes an explicit verdict and failure list; the carrier omits both.
 const host=suite.name==='host';
 need(report.passed===suite.cases.length&&
  (host||Object.hasOwn(report,'ok')?report.ok===true:true)&&
  (host||Object.hasOwn(report,'failed')?report.failed===0:true)&&
  (host||Object.hasOwn(report,'failures')?empty(report.failures):true),'ORIGINAL_REPORT_FAILED');
}
export function validateStartupReport(suite,report){
 const expected=JSON.stringify(suite.startupCases);
 need(Array.isArray(suite.startupCases)&&suite.startupCases.length>0&&new Set(suite.startupCases).size===suite.startupCases.length&&
  Array.isArray(report?.cases)&&JSON.stringify(report.selected)===expected&&JSON.stringify(report.cases.map(row=>row?.name))===expected,
  'ORIGINAL_STARTUP_CASE_MEMBERSHIP_CHANGED');
 need(report.ok===true&&report.actualAppChanged===false&&report.cases.every(row=>row?.ok===true&&row.error===null),'ORIGINAL_STARTUP_REPORT_FAILED');
}
export function validateCompileTrace(suite,trace,receipt,authority){
 need(trace?.schemaVersion===1&&trace.status==='PASS'&&trace.producer===suite.script&&
  Number.isSafeInteger(receipt.pid)&&receipt.pid>0&&trace.pid===receipt.pid&&trace.beforeExit===true&&trace.exitCode===receipt.exitCode,
  'COMPILE_RUNTIME_AUDIT_FAILED');
 need(Array.isArray(trace.violations)&&trace.violations.length===0&&Array.isArray(trace.inputs)&&trace.inputs.length>0&&
  Array.isArray(trace.builds)&&trace.builds.length>0&&trace.builds.every((row,i)=>row?.ordinal===i+1&&row.status==='PASS'),
  'COMPILE_RUNTIME_AUDIT_FAILED');
 const compilerArgs=JSON.stringify(['--service='+authority.compilerVersion,'--ping']);
 need(Array.isArray(trace.toolChildren)&&trace.toolChildren.length>0&&trace.toolChildren.every(row=>
  Number.isSafeInteger(row?.pid)&&row.pid>0&&row.closed===true&&typeof row.program==='string'&&path.isAbsolute(row.program)&&
  path.resolve(row.program).toLowerCase()===path.resolve(authority.compilerBinary).toLowerCase()&&JSON.stringify(row.args)===compilerArgs),
  'COMPILE_RUNTIME_AUDIT_FAILED');
}
export async function runOwnedClose(out){
 const started=Date.now(),deadline=started+spec.budgetMs;need(path.isAbsolute(out),'ABSOLUTE_EVIDENCE_REQUIRED');need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH,'AMBIENT_NODE_FORWARDING_REJECTED');out=path.resolve(out);need(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');need(path.isAbsolute(out)&&!out.startsWith(repoRoot+path.sep),'OWNED_EXTERNAL_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const result={schemaVersion:1,id:spec.id,status:'FAIL',scope:'behavioral-mock',startedAt:new Date(started).toISOString(),originalSuites:[],productEdits:0,producerEdits:0,fullMonitorOrProductAcceptance:false};
 let sourceRows=[],authority;
 try{
  sourceRows=declaredSources().map(relative=>({path:relative,sha256:digest(path.join(repoRoot,relative))}));authority=lockBundledNodeTools(repoRoot);fs.writeFileSync(path.join(out,'tools.before.json'),JSON.stringify(authority,null,2));
  for(const suite of spec.suites){
   const dir=path.join(out,suite.name);fs.mkdirSync(dir);const config={repoRoot,producer:suite.script,authority,productSources:spec.productSources,syntheticPorts:spec.syntheticPorts,traceFile:path.join(dir,'runtime.json')},configPath=path.join(dir,'config.json');fs.writeFileSync(configPath,JSON.stringify(config,null,2));
   const env={...process.env};for(const name of Object.keys(env))if(/^YMCC_HUD_.*_SOURCE$/.test(name)||['NODE_OPTIONS','NODE_PATH','ESBUILD_BINARY_PATH','YMCC_MONITOR_MODEL_OUT','YMCC_HUD_OPTIONAL_OUT','YMCC_MONITOR_ROUTING_OUT'].includes(name))delete env[name];env[suite.outputEnv]=dir;env.YMCC_MONITOR_CLOSE_CONFIG=configPath;
   const remaining=deadline-Date.now();need(remaining>0,'OWNED_CLOSE_SHARED_BUDGET_EXHAUSTED');const args=['--import',pathToFileURL(path.join(moduleRoot,'runtime.mjs')).href,path.join(repoRoot,suite.script),...(suite.startupCases?['--with-startup','--startup-case='+suite.startupCases.join(',')]:[])];
   const processResult=await runProcess(process.execPath,args,{cwd:repoRoot,env,timeoutMs:Math.min(spec.childBudgetMs,remaining)});const {stdout,stderr,...receipt}=processResult;fs.writeFileSync(path.join(dir,'stdout.log'),stdout);fs.writeFileSync(path.join(dir,'stderr.log'),stderr);fs.writeFileSync(path.join(dir,'process.json'),JSON.stringify({...receipt,executable:process.execPath,args,cwd:repoRoot},null,2));
   need(receipt.exitCode===0&&!receipt.timedOut&&!receipt.signal&&!receipt.launchError&&!receipt.outputTruncated,'ORIGINAL_PRODUCER_NOT_NATURAL_SUCCESS');const trace=JSON.parse(fs.readFileSync(config.traceFile,'utf8'));validateCompileTrace(suite,trace,receipt,authority);const report=JSON.parse(fs.readFileSync(path.join(dir,suite.report),'utf8'));validateReport(suite,report);
   let startup={};if(suite.startupCases){const startupReport=path.join(dir,suite.startupReport);validateStartupReport(suite,JSON.parse(fs.readFileSync(startupReport,'utf8')));startup={startupReport,startupRows:suite.startupCases.length};}
   result.originalSuites.push({name:suite.name,status:'PASS',originalRows:suite.cases.length,process:receipt,report:path.join(dir,suite.report),trace:config.traceFile,compiledInputs:trace.inputs.length,toolChildren:trace.toolChildren,...startup});
  }
  verifyBundledToolScope(authority,repoRoot);for(const row of sourceRows)need(digest(path.join(repoRoot,row.path))===row.sha256,'SOURCE_CHANGED_DURING_RUN');result.status='PASS';result.originalPositiveCases=result.originalSuites.reduce((n,s)=>n+s.originalRows,0);result.startupPositiveCases=result.originalSuites.reduce((n,s)=>n+(s.startupRows??0),0);result.actualInputClosure=true;result.toolAuthorityVerified=true;result.compiledCodeOnlyNotPhysicalWindowAcceptance=true;
 }catch(error){result.reason=String(error.message);result.errorStack=error.stack;}
 result.finishedAt=new Date().toISOString();result.elapsedMs=Date.now()-started;result.budgetMs=spec.budgetMs;result.childBudgetMs=spec.childBudgetMs;result.sourceHashes=sourceRows;result.toolAuthorityFile=authority?path.join(out,'tools.before.json'):null;result.claims={installedApplicationExercised:false,fullMonitorParentRun:false,cpuPowerMeasured:false,normalReleaseRequired:false};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{need(process.argv.length===4&&process.argv[2]==='--evidence-dir','EXPECTED_EVIDENCE_DIR');const result=await runOwnedClose(process.argv[3]);console.log(JSON.stringify({id:result.id,status:result.status,reason:result.reason,originalPositiveCases:result.originalPositiveCases,elapsedMs:result.elapsedMs}));process.exitCode=result.status==='PASS'?0:1;}catch(error){console.error(error.stack);process.exitCode=1;}}
