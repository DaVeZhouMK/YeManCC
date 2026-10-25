import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import os from 'node:os';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
import {validateMotionReport} from './validation.mjs';import {verifyMotionEvidence,validateOutputRoot,readJson,longestCompilerInputPath} from './evidence.mjs';import {parseArguments,fingerprintRequired} from './run.mjs';import {REPO_ROOT,motionConsumerInputs} from './inputs.mjs';import {getMotionConsumerAdapters} from './adapter.mjs';import {EXPECTED_MUTATIONS,needsCapabilityGrid,MAX_RAW_REPORT_JSON_BYTES,MAX_COMPILER_INPUT_PATH_CHARS} from './contracts.mjs';import {reportFixture,fileFixture} from './test-fixtures.mjs';
test('complete original dual configurations and fixed 39 faults preserve the separate legacy subtotal',()=>{const f=reportFixture(),r=validateMotionReport(f.report,f.expected);assert.equal(r.status,'PASS');assert.equal(r.structuralValidationOnly,true);assert.deepEqual(r.subtotals.map(c=>c.originalConsumerAssertions),[114560,114560]);assert.equal(EXPECTED_MUTATIONS.length,39);});
const negative=[
 ['nonterminal prepared report',f=>f.report.status='prepared'],['generation-only report',f=>f.report.status='generated-only'],['baseline expected failure',f=>f.report.mode='legacy-before-expected-failure'],['consumer-only positive',f=>f.report.consumerOnlyControl=true],
 ['missing Checked configuration',f=>f.report.configurations.pop()],['duplicate Release configuration',f=>f.report.configurations[1].name='Release'],['launch/compile failure promoted to positive',f=>f.report.configurations[0].exitCode=3],['missing positive binary identity',f=>delete f.report.configurations[0].executableSha256],['positive run has assertion failures',f=>f.report.configurations[0].report.failures=[{group:'real-failure'}]],
 ['legacy assertions silently removed',f=>f.report.configurations[0].report.consumerChecks--],['counts do not match groups',f=>f.report.configurations[0].report.groups['legacy-consumer']--],['capability matrix skipped',f=>f.report.configurations[0].report.capabilityCoverage={skipped:true}],['unexecuted diagnostics',f=>f.report.configurations[0].report.evidenceGenerationCoverage.pureCases=0],['physical scope promoted',f=>f.report.boundaries.realDevicesAccessed=true],
 ['unified binding suite skipped',f=>delete f.report.configurations[0].report.unifiedBindingCoverage],['controller route silently claimed complete',f=>f.report.configurations[0].report.unifiedBindingCoverage.completeMotionDomain=true],['missing actual binding consumer',f=>f.report.configurations[0].report.unifiedBindingCoverage.actualSelectedConsumers--],
 ['one semantic fault omitted',f=>f.report.mutations.pop()],['duplicate fault hides a missing fault',f=>f.report.mutations[34].name=f.report.mutations[0].name],['unreviewed replacement fault',f=>f.report.mutations[34].name='something-else'],['missing checked fault execution',f=>f.report.mutations[0].configurations.pop()],['setup rejection called detected',f=>f.report.mutations[0].childExitCode=3],['compile rejection called detected',f=>f.report.mutations[0].configurations[0].exitCode=3],['no failed executable assertions',f=>f.report.mutations[0].configurations[0].failedAssertions=0],
 ['generic wrong consumer-only mode concealed by detected flag',f=>{for(const c of f.report.mutations.find(m=>m.name==='generic-claims-exact-machine').configurations)c.checks=114949;}],['source changed during run',f=>f.report.sourceAtFinish.runnerUnchanged=false],['source closure absent',f=>delete f.report.sourceAtFinish],['current hash does not match initial source',f=>f.report.sourceAtFinish.sha256='0'.repeat(64)],['unapproved historical refactor',f=>{f.report.sourceChecks.capabilityControlChecks[0].unchanged=false;f.report.sourceChecks.capabilityControlChecks[0].controlledRefactorVerified=true;}],['writer ownership check missing',f=>f.report.sourceChecks.evidenceWriterChecks.powerAssignments=3],['wrong fixed HC oracle',f=>f.expected.hcCommit='wrong'],['legacy case omitted',f=>f.expected.legacy.cases.pop()],['generic motion recipe claims exact machine',f=>f.expected.genericFallbackPolicy.machineIdentityMutable=true],
];for(const [name,change]of negative)test('reject '+name,()=>{const f=reportFixture();change(f);assert.equal(validateMotionReport(f.report,f.expected).status,'FAIL');});
test('malformed report structures return a rejection, never throw or create an acceptance',()=>{for(const report of [null,{},'pass',{configurations:[null],mutations:[null]}])assert.doesNotThrow(()=>assert.equal(validateMotionReport(report,null).status,'FAIL'));});
test('all real log/executable files and every generated artifact are bound in an owned tree',t=>{const f=fileFixture(t);const r=verifyMotionEvidence(f.raw,f.report,f.expected);assert.equal(r.positive.length,2);assert.equal(r.faults.length,39);assert(r.closure.length>300);assert(r.faults.every(row=>row.configurations.length===2));});
const fileNegatives=[
 ['changed positive binary',f=>f.write('Release/motion_consumer.exe','different')],['changed successful native log',f=>f.write('Release/run.log','{"status":"pass"}')],['missing child compile evidence',f=>fs.unlinkSync(path.join(f.raw,'mutants/catalog-only-no-entry/evidence/Checked/compile.log'))],
 ['child setup failure disguised as semantic detection',f=>{const c=f.readChild(EXPECTED_MUTATIONS[0]);c.error='Compiler setup rejected';f.saveChild(EXPECTED_MUTATIONS[0],c);}],['child source refusal disguised as semantic detection',f=>{const c=f.readChild(EXPECTED_MUTATIONS[0]);c.sourceAtFinish.runnerUnchanged=false;f.saveChild(EXPECTED_MUTATIONS[0],c);}],
 ['generic capability consumer was actually skipped',f=>{const name='generic-claims-exact-machine',c=f.readChild(name);c.consumerOnlyControl=true;f.saveChild(name,c);}],['child output points to another run',f=>{const c=f.readChild(EXPECTED_MUTATIONS[0]);c.out=f.root;f.saveChild(EXPECTED_MUTATIONS[0],c);}],['missing child binary hash',f=>{const c=f.readChild(EXPECTED_MUTATIONS[0]);delete c.configurations[0].executableSha256;f.saveChild(EXPECTED_MUTATIONS[0],c);}],
];for(const [name,change]of fileNegatives)test('filesystem rejects '+name,t=>{const f=fileFixture(t);change(f);assert.throws(()=>verifyMotionEvidence(f.raw,f.report,f.expected));});
test('repository output rejection happens before any evidence directory or result file is created',()=>{const name=path.join(REPO_ROOT,'.motion-output-reject-'+crypto.randomUUID());assert(!fs.existsSync(name));const child=spawnSync(process.execPath,[path.join(REPO_ROOT,'tools/ymcc_sim/global/motion-consumer-signals/run.mjs'),'--evidence-dir',name],{cwd:REPO_ROOT,encoding:'utf8',windowsHide:true,timeout:10000});assert.equal(child.status,3);assert(!fs.existsSync(name));});
test('unknown, missing and duplicate CLI options fail before execution',()=>{for(const args of [[],['--evidence-dir'],['--other','x'],['--evidence-dir','x','--evidence-dir','y'],['--evidence-dir','--other']])assert.throws(()=>parseArguments(args));});
test('missing required bytes stay missing rather than being signed using a default or another file',()=>{const result=fingerprintRequired([path.join(os.tmpdir(),'missing-motion-'+crypto.randomUUID())]);assert.equal(result[0].status,'MISSING_OR_NOT_FILE');assert.equal(result[0].sha256,null);});
test('new descriptor has its own bounded compiled budget, fixed arguments, and all external inputs',()=>{const d=getMotionConsumerAdapters()[0];assert.equal(d.id,'gyro.machine-motion-consumers');assert.equal(d.kind,'powershell');assert.equal(d.timeoutMs,900000);assert.deepEqual(d.args,['-EvidenceDir','{outDir}/m']);assert(d.artifactInputs.length>=744&&d.artifactInputs.length<=3072);assert(d.evidenceInputs.includes('tools/hc_model_motion_consumer_selftest.mjs'));const inputs=motionConsumerInputs();assert.equal(inputs.oracleProblem,null);assert.equal(inputs.expected.legacy.sourceChecks.length,77);});

 test('reviewed historical matrix inputs remain explicit even if the oracle cannot load',()=>{
 const fakeRepo=path.join(os.tmpdir(),'missing-motion-repository-'+crypto.randomUUID());const inputs=motionConsumerInputs(fakeRepo);
 assert(inputs.oracleProblem);assert(inputs.artifacts.some(file=>file.endsWith('EFFECTIVE-HC-MATRICES.json')));
 assert(inputs.artifacts.some(file=>file.includes('CurrentCatalog')&&file.endsWith('RESULT.json')));assert(!fs.existsSync(fakeRepo));
 });
 test('all three generic faults require the full capability consumer',()=>{
 const names=EXPECTED_MUTATIONS.filter(name=>name.startsWith('generic-'));assert.equal(names.length,3);assert(names.every(needsCapabilityGrid));
 });

 test('the selected motion case closes all relative repository module imports and the owned-job helper',()=>{
 const inputs=motionConsumerInputs(),declared=new Set(inputs.sources.map(file=>path.resolve(REPO_ROOT,file).toLowerCase())),missing=[];
 for(const source of inputs.sources.filter(file=>file.endsWith('.mjs'))){const absolute=path.resolve(REPO_ROOT,source),text=fs.readFileSync(absolute,'utf8');
  for(const match of text.matchAll(/\bfrom\s*['"](\.[^'"]+)['"]/g)){
   const imported=path.resolve(path.dirname(absolute),match[1]);assert(fs.existsSync(imported),'missing repository module '+imported);
   if(!declared.has(imported.toLowerCase()))missing.push({importer:source,dependency:path.relative(REPO_ROOT,imported)});
  }
 }
 assert.deepEqual(missing,[]);assert(inputs.sources.includes('tools/ymcc_sim/global/host-process.ps1'));
 assert(inputs.sources.includes('tools/ymcc_sim/global/machine-capability-signals/adapter.mjs'));
 assert(inputs.sources.includes('tools/ymcc_sim/global/runner.mjs'));
 });

 test('the fixed real oracle keeps 215 historical inputs distinct from 168 executed legacy consumer cases',()=>{
 const inputs=motionConsumerInputs();assert.equal(inputs.oracleProblem,null);assert.equal(inputs.expected.legacy.cases.length,215);
 const {report}=reportFixture();assert.equal(report.configurations[0].report.legacyConsumerCases,168);
 const checked=validateMotionReport(report,inputs.expected);assert.equal(checked.status,'PASS',checked.issues.join(','));
 });

 test('combined raw reports have a measured bounded allowance and reject oversized files before JSON parsing',t=>{
 const f=fileFixture(t),file=f.write('oversized.json','{}');assert.equal(MAX_RAW_REPORT_JSON_BYTES,64*1024*1024);
 fs.truncateSync(file,MAX_RAW_REPORT_JSON_BYTES+1);assert.throws(()=>readJson(file),/MOTION_JSON_SIZE_LIMIT/);
 });

 test('compiler paths are rejected before creation rather than miscounted as semantic fault detection',()=>{
 const output=path.join(os.tmpdir(),'motion-'+crypto.randomUUID()+'-'+'x'.repeat(130));assert(longestCompilerInputPath(output).length>MAX_COMPILER_INPUT_PATH_CHARS);
 assert(!fs.existsSync(output));assert.throws(()=>validateOutputRoot(output,REPO_ROOT),/MOTION_COMPILER_OUTPUT_PATH_TOO_LONG/);assert(!fs.existsSync(output));
 });
 test('short per-case m/r outputs retain the complete longest original fault name below the compiler limit',()=>{
 const output='D:/Codex/2026-10-19/mo/work/all-simulation-tests/motion-proof-run/2026-10-19T01-30-00-000Z-12345678/cases/gyro.machine-motion-consumers/m';
 const target=longestCompilerInputPath(output);assert(target.includes('capability-observation-promotes-controller'));assert(target.length<=MAX_COMPILER_INPUT_PATH_CHARS);
 const d=getMotionConsumerAdapters()[0];assert.deepEqual(d.args,['-EvidenceDir','{outDir}/m']);assert.deepEqual(d.resultFiles,['m/result.json','m/q/result.json','m/q/p/e/result.json',...Array.from({length:4},(_,i)=>'m/q/f'+i+'/e/result.json'),'m/o/result.json','m/o/p/e/result.json',...Array.from({length:5},(_,i)=>'m/o/f'+i+'/e/result.json')]);
 });
