import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {REPO,MODULE,PRODUCT_INPUTS,inputFiles,toolchainSpec} from './adapter.mjs';
export const need=(condition,message)=>{if(!condition)throw Error(message);};
export const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export const bytesHash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export function writeJson(file,value){fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');}
export function inside(root,file){const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(r!=='..'&&!r.startsWith('..'+path.sep)&&!path.isAbsolute(r));}
export function evidencePath(value){
 need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'RECOVERY_ABSOLUTE_EVIDENCE_REQUIRED');
 const root=path.resolve(value),build=path.resolve(REPO,'../../../Build');
 need(inside(build,root)&&!inside(REPO,root),'RECOVERY_EVIDENCE_OUTSIDE_BUILD');
 for(let p=root;;){if(fs.existsSync(p))need(!fs.lstatSync(p).isSymbolicLink(),'RECOVERY_EVIDENCE_REPARSE');const next=path.dirname(p);if(p===next)break;p=next;}
 need(!fs.existsSync(root),'RECOVERY_FRESH_EVIDENCE_REQUIRED');return root;
}
export function walk(root){const files=[];function visit(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){
 const p=path.join(dir,e.name);need(!fs.lstatSync(p).isSymbolicLink(),'RECOVERY_OUTPUT_REPARSE');
 if(e.isDirectory())visit(p);else{need(e.isFile(),'RECOVERY_OUTPUT_NOT_FILE');files.push(p);need(files.length<15000,'RECOVERY_OUTPUT_LIMIT');}
 }}visit(root);return files.sort();}
export function captureInputs(root){
 const sourceRows=inputFiles().map(relative=>{
  const p=path.join(REPO,relative),bytes=fs.readFileSync(p),saved=path.join(root,'input-sources',relative);
  need(fs.realpathSync(p).toLowerCase().startsWith(fs.realpathSync(REPO).toLowerCase()+path.sep),'RECOVERY_SOURCE_OUTSIDE_REPO');
  fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes,{flag:'wx'});
  return {path:relative,sha256:bytesHash(bytes),saved};
 });
 const tool=toolchainSpec();need(tool.files.length>0&&tool.files.length<=640,'RECOVERY_TOOLCHAIN_MEMBERSHIP');
 const artifacts=tool.files.map(row=>{need(path.isAbsolute(row.path)&&fs.lstatSync(row.path).isFile(),'RECOVERY_TOOLCHAIN_NOT_FILE');need(hash(row.path)===row.sha256,'RECOVERY_PINNED_TOOL_CHANGED:'+row.path);return {...row};});
 const node=artifacts.find(r=>r.roles.includes('node-runtime'));need(node&&node.path.toLowerCase()===path.resolve(process.execPath).toLowerCase(),'RECOVERY_NODE_RUNTIME_NOT_PINNED');
 const result={schemaVersion:1,createdAt:new Date().toISOString(),sources:sourceRows,artifacts,toolchain:tool,
  toolchainMeaning:'Pinned compiler/linker binaries and resources, environment scripts, evaluated headers and libraries. Actual compiler dependencies/link searches are checked per profile. Normal OS loader/kernel are stated platform prerequisites.'};
 writeJson(path.join(root,'input-snapshot.json'),result);return result;
}
export function verifyInputs(root,before){
 need(isDeepStrictEqual(inputFiles(),before.sources.map(r=>r.path)),'RECOVERY_SOURCE_MEMBERSHIP_CHANGED');
 need(isDeepStrictEqual(toolchainSpec().files,before.artifacts),'RECOVERY_TOOL_MEMBERSHIP_CHANGED');
 for(const r of before.sources){need(hash(path.join(REPO,r.path))===r.sha256,'RECOVERY_LIVE_SOURCE_CHANGED:'+r.path);need(hash(r.saved)===r.sha256,'RECOVERY_SOURCE_COPY_CHANGED:'+r.path);}
 for(const r of before.artifacts)need(hash(r.path)===r.sha256,'RECOVERY_TOOL_CHANGED:'+r.path);
 return true;
}
export function freezeProject(root,before){
 const frozen=path.join(root,'frozen-project');
 for(const relative of PRODUCT_INPUTS){const row=before.sources.find(r=>r.path===relative),target=path.join(frozen,relative);need(row,'RECOVERY_UNDECLARED_FROZEN_SOURCE');fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.saved,target,fs.constants.COPYFILE_EXCL);need(hash(target)===row.sha256,'RECOVERY_FROZEN_SOURCE_CHANGED');}
 return frozen;
}
export function verifyOriginalReport(raw,spec,{buildExitCode,executeExitCode,negative=false}={}){
 need(buildExitCode===0,'RECOVERY_COMPILE_FAILURE_IS_NOT_REJECTION');
 need(raw&&Array.isArray(raw.cases)&&raw.caseCount===spec.caseCount&&raw.cases.length===spec.caseCount,'RECOVERY_ORIGINAL_CASE_COUNT');
 need(raw.checks===spec.checkCount&&raw.deviceIO===false&&raw.newThreads===false,'RECOVERY_ORIGINAL_BOUNDARY_OR_CHECK_COUNT');
 need(isDeepStrictEqual(raw.cases.map(r=>r.name),spec.caseNames),'RECOVERY_ORIGINAL_CASE_REMOVED_OR_REORDERED');
 let checks=0;
 for(const c of raw.cases){need(Array.isArray(c.checks)&&c.checks.length>0,'RECOVERY_ORIGINAL_CASE_EMPTY');checks+=c.checks.length;
  for(const check of c.checks)need(typeof check.passed==='boolean'&&check.passed===isDeepStrictEqual(check.actual,check.expected),'RECOVERY_ORIGINAL_ASSERTION_TAMPERED');
  need(c.passed===c.checks.every(x=>x.passed),'RECOVERY_ORIGINAL_CASE_STATUS_TAMPERED');
 }
 need(checks===raw.checks,'RECOVERY_ORIGINAL_ASSERTION_COUNT');
 const failed=raw.cases.filter(c=>!c.passed),failedChecks=failed.flatMap(c=>c.checks.filter(x=>!x.passed).map(x=>({case:c.name,...x})));
 need(raw.failedCases===failed.length,'RECOVERY_FAILED_COUNT_MISMATCH');
 if(negative){
  need(executeExitCode===1,'RECOVERY_NEGATIVE_NEEDS_EXECUTED_ASSERTION_FAILURE');
  need(isDeepStrictEqual(failed.map(c=>c.name),spec.negative.failedCases),'RECOVERY_UNEXPECTED_NEGATIVE_CASES');
  need(isDeepStrictEqual(failedChecks,spec.negative.failedChecks),'RECOVERY_EXPECTED_SEMANTIC_ASSERTIONS_MISSING');
 }else need(executeExitCode===0&&failed.length===0,'RECOVERY_ORIGINAL_POSITIVE_FAILED');
 return {status:negative?'EXPECTED_SEMANTIC_FAILURE':'PASS',cases:raw.caseCount,checks:raw.checks,failedCases:failed.length,failedNames:failed.map(c=>c.name),failedChecks,buildExitCode,executeExitCode};
}
export function evaluatedInputs(root,profileDir,before){
 const depFile=path.join(profileDir,'fixture.cpp.json');need(fs.existsSync(depFile),'RECOVERY_COMPILER_DEPENDENCIES_MISSING');
 const d=readJson(depFile);need(d.Data&&Array.isArray(d.Data.Includes)&&d.Data.Includes.length>0,'RECOVERY_COMPILER_DEPENDENCY_SCHEMA');
 const permitted=new Map(before.artifacts.map(r=>[path.resolve(r.path).toLowerCase(),r]));
 const rows=[];
 function add(file,kind){const p=path.resolve(file);need(fs.existsSync(p),'RECOVERY_COMPILER_INPUT_MISSING:'+p);
  const digest=hash(p);if(!inside(root,p)){const row=permitted.get(p.toLowerCase());need(row,'RECOVERY_UNDECLARED_COMPILER_INPUT:'+p);need(row.sha256===digest,'RECOVERY_COMPILER_INPUT_CHANGED:'+p);}
  rows.push({kind,path:p,sha256:digest});
 }
 need(inside(root,d.Data.Source),'RECOVERY_COMPILE_SOURCE_OUTSIDE_CASE');add(d.Data.Source,'Compile');
 for(const p of d.Data.Includes)add(p,'Include');
 const log=fs.readFileSync(path.join(profileDir,'build.stdout.log'),'utf8'),libraries=[...new Set([...log.matchAll(/([A-Za-z]:\\[^\r\n]*?\.lib)/g)].map(m=>m[1]))];
 need(libraries.length>0,'RECOVERY_LINK_LIBRARY_LEDGER_MISSING');for(const p of libraries)add(p,'Library');
 const command=fs.readFileSync(path.join(profileDir,'compile.cmd'),'utf8');need(command.includes(before.toolchain.windowsSdkVersion),'RECOVERY_SDK_SELECTION_CHANGED');
 writeJson(path.join(profileDir,'evaluated-inputs.json'),{schemaVersion:1,sourceDependencies:{path:depFile,sha256:hash(depFile)},rows});
 return {compile:1,headers:d.Data.Includes.length,libraries:libraries.length,ledger:path.join(profileDir,'evaluated-inputs.json'),sha256:hash(path.join(profileDir,'evaluated-inputs.json'))};
}
export function verifyBindings(runDir,project,before,{negativeHeader}={}){
 const binding=readJson(path.join(runDir,'source-bindings.json'));
 const runnerRow=before.sources.find(r=>r.path==='tools/input_host_recovery_health_selftest.mjs');
 need(binding.runnerSha256===runnerRow.sha256,'RECOVERY_EXECUTED_RUNNER_NOT_BOUND');
 need(binding.fixture.sha256===hash(path.join(project,'tools/fixtures/input_host_recovery_health_fixture.cpp')),'RECOVERY_ORIGINAL_FIXTURE_CHANGED');
 need(binding.sources.current.sha256===hash(path.join(project,'native/main.cpp')),'RECOVERY_PRODUCT_MAIN_SNAPSHOT_CHANGED');
 const required=['inputHostRecoveryHealthContextLocked','inputHostRecoveryHealthSource','inputHostRecoveryHealthLatestSourceLocked','inputHostRecoveryHealthObserveLocked','inputHostRecoveryHealthAcceptedLocked','inputHostRecoveryMaintenanceTick','canonical producer previous source','canonical producer continuity commit'];
 const names=new Set(binding.sources.current.ranges.map(r=>r.name));for(const name of required)need(names.has(name),'RECOVERY_ACTUAL_SOURCE_BINDING_MISSING:'+name);
 need(binding.sources.current.fixed===true,'RECOVERY_CURRENT_WINDOW_HELPER_NOT_USED');
 const expectedDeps=PRODUCT_INPUTS.filter(p=>p!=='native/main.cpp'&&p!=='tools/input_host_recovery_health_selftest.mjs'&&p!=='tools/fixtures/input_host_recovery_health_fixture.cpp');
 need(binding.dependencies.length===expectedDeps.length,'RECOVERY_RUNNER_DEPENDENCY_MEMBERSHIP');
 for(const rel of expectedDeps){const p=rel==='native/input_host_recovery_health.h'&&negativeHeader?negativeHeader:path.join(project,rel);const row=binding.dependencies.find(r=>path.resolve(r.file).toLowerCase()===path.resolve(p).toLowerCase());need(row&&row.sha256===hash(p),'RECOVERY_DEPENDENCY_BINDING_MISSING:'+rel);}
 return {path:path.join(runDir,'source-bindings.json'),sha256:hash(path.join(runDir,'source-bindings.json')),actualFunctions:required,productionSubstitutions:binding.productionSubstitutions,scope:binding.scope};
}
