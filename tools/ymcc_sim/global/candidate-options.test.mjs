import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { configureCandidateAdapters } from './candidate-options.mjs';
import { parseArgs } from './cli.mjs';
import { getExtraAdapters } from './extra-adapters.mjs';
import { runProcess, executeAdapter, validateAdapters, MAX_ARTIFACT_INPUTS } from './runner.mjs';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const parent = fs.realpathSync(os.tmpdir());
const temp = fs.mkdtempSync(path.join(parent, 'ymcc-candidate-options-'));
process.on('exit', () => {
  const actual = fs.realpathSync(temp);
  assert.equal(path.dirname(actual), parent);
  assert(path.basename(actual).startsWith('ymcc-candidate-options-'));
  fs.rmSync(actual, {recursive:true,force:true});
});
const pin = 'a'.repeat(64);
const options = { payloadRoot:'D:/candidate fan', lightingPayloadRoot:'D:/candidate light', expectedHelperSha256:pin,
  runtimeRoot:'D:/candidate runtime', sharedRuntimeRoot:'D:/candidate shared', hcProfile:'D:/profile.json', hcVersion:'future-candidate' };
const byId = rows => Object.fromEntries(rows.map(row => [row.id,row]));
test('source-only regressions retain their original arguments and do not mutate registrations', () => {
  const registered=getExtraAdapters(), before=structuredClone(registered);
  const configured=byId(configureCandidateAdapters(registered));
  for(const id of ['fan.runtime-connection','lighting.signal-consumer']) {
    assert.deepEqual(configured[id].args, byId(before)[id].args);
    assert(configured[id].evidenceInputs.includes('tools/ymcc_sim/global/candidate-options.mjs'));
  }
  assert.deepEqual(registered,before);
});
test('Host and lighting candidates reach their distinct public consumer flags', () => {
  const original=byId(getExtraAdapters());const rows=byId(configureCandidateAdapters(Object.values(original),options));
  assert.deepEqual(rows['fan.runtime-connection'].args.slice(original['fan.runtime-connection'].args.length),
    ['-RuntimeRoot',options.runtimeRoot,'-SharedRuntimeRoot',options.sharedRuntimeRoot,'-HostPayloadRoot',options.payloadRoot]);
  assert.deepEqual(rows['lighting.signal-consumer'].args.slice(original['lighting.signal-consumer'].args.length),
    ['--runtime-root',options.runtimeRoot,'--shared-runtime-root',options.sharedRuntimeRoot,'--hc-version',options.hcVersion,'--hc-profile',options.hcProfile,'--host-payload-root',options.lightingPayloadRoot,'--expected-helper-sha256',pin]);
  assert(!rows['lighting.signal-consumer'].args.includes(options.payloadRoot));
});
test('a lighting payload requires a reviewed pin, and a pin cannot be silently ignored', () => {
  for(const item of [{lightingPayloadRoot:'D:/light'},{expectedHelperSha256:pin},{lightingPayloadRoot:'D:/light',expectedHelperSha256:'invalid'}])
    assert.throws(()=>configureCandidateAdapters(getExtraAdapters(),item),/CANDIDATE|PIN/);
});
test('candidate inputs reject control characters without interpreting path characters', () => {
  for(const key of ['runtimeRoot','sharedRuntimeRoot','payloadRoot','hcVersion','hcProfile'])
    for(const value of ['',null,[], 'D:/candidate\n--unsafe','D:/candidate\0'])
      assert.throws(()=>configureCandidateAdapters(getExtraAdapters(),{[key]:value}),/INVALID_CANDIDATE_INPUT/);
});
test('CLI parses explicit candidates without pinning a version or rewriting roots', () => {
 const parsed=parseArgs(['--runtime-root',options.runtimeRoot,'--shared-runtime-root',options.sharedRuntimeRoot,
 '--lighting-payload-root',options.lightingPayloadRoot,'--expected-helper-sha256',pin,'--hc-profile',options.hcProfile,'--hc-version',options.hcVersion]);
 for(const key of ['runtimeRoot','sharedRuntimeRoot','lightingPayloadRoot','expectedHelperSha256','hcProfile','hcVersion'])assert.equal(parsed[key],options[key]);
 assert.throws(()=>parseArgs(['--lighting-payload-root','one','--lighting-payload-root','two']),/DUPLICATE_ARGUMENT/);
});
test('actual CLI plan persists both candidate argument lists and explicit identities', () => {
 const output=path.join(temp,'plan');
 const args=['--mode','plan','--profile','full','--test','fan.runtime-connection,lighting.signal-consumer','--out-dir',output,
 '--payload-root',options.payloadRoot,'--runtime-root',options.runtimeRoot,'--shared-runtime-root',options.sharedRuntimeRoot,
 '--lighting-payload-root',options.lightingPayloadRoot,'--expected-helper-sha256',pin,'--hc-profile',options.hcProfile,'--hc-version',options.hcVersion];
 const child=spawnSync(process.execPath,[path.join(repo,'tools/ymcc_sim/global/cli.mjs'),...args],{cwd:repo,encoding:'utf8',windowsHide:true,timeout:30000});
 assert.equal(child.status,12,child.stderr);
 const resultFile=child.stdout.match(/^RESULT (.+)$/m)?.[1].trim();assert(resultFile,child.stdout);
 const result=JSON.parse(fs.readFileSync(resultFile,'utf8'));
 assert.deepEqual(result.candidateInputs,options);
 const plan=JSON.parse(fs.readFileSync(path.join(path.dirname(resultFile),'plan.json'),'utf8'));
 const rows=byId(plan.adapters);
 assert(rows['lighting.signal-consumer'].args.includes(options.lightingPayloadRoot));
 assert(rows['lighting.signal-consumer'].args.includes(pin));
 assert(rows['fan.runtime-connection'].args.includes(options.runtimeRoot));
 assert.equal(result.results.length,0);assert.equal(plan.noTestsExecuted,true);
});
test('the actual child process receives spaces, dollar signs and shell syntax as literal arguments', async () => {
 const value='D:/candidate space/$() `literal` & no-shell';
 const rows=byId(configureCandidateAdapters(getExtraAdapters(),{lightingPayloadRoot:value,expectedHelperSha256:pin}));
 const args=rows['lighting.signal-consumer'].args.slice(2);
 const file=path.join(temp,'capture.mjs');fs.writeFileSync(file,'console.log(JSON.stringify(process.argv.slice(2)));');
 const result=await runProcess(process.execPath,[file,...args],{cwd:repo,env:{SystemRoot:process.env.SystemRoot},timeoutMs:3000});
 assert.equal(result.exitCode,0,result.stderr);assert.deepEqual(JSON.parse(result.stdout),['--host-payload-root',value,'--expected-helper-sha256',pin]);
});

function artifactFixture(id,body) {
 const root=path.join(temp,id);fs.mkdirSync(root);const entry=path.join(root,'case.mjs');fs.writeFileSync(entry,body);
 const candidate=path.join(root,'candidate.dll');
 const adapter={id,domain:'fixture',title:id,kind:'node',scope:'behavioral-mock',path:'case.mjs',args:[],profiles:['desktop'],timeoutMs:3000,evidenceInputs:[],artifactInputs:[candidate]};
 return {root,candidate,adapter,runDir:path.join(root,'results')};
}
test('missing explicit candidate blocks before its consumer is launched', async () => {
 const f=artifactFixture('missing-candidate',"throw Error('must not launch');");
 const result=await executeAdapter(f.adapter,{repoRoot:f.root,runDir:f.runDir});
 assert.equal(result.status,'BLOCKED');assert.equal(result.reason,'EXPLICIT_CANDIDATE_ARTIFACT_MISSING');assert.equal(result.pid,undefined);
});
test('unchanged explicit candidate bytes are captured by the actual runner', async () => {
 const f=artifactFixture('stable-candidate',"console.log('consumer executed');");fs.writeFileSync(f.candidate,'original');
 const result=await executeAdapter(f.adapter,{repoRoot:f.root,runDir:f.runDir});
 assert.equal(result.status,'PASS');assert.equal(result.candidateArtifactChanged,false);
 assert.equal(result.candidateArtifacts[0].path,f.candidate);assert.match(result.candidateArtifacts[0].sha256,/^[a-f0-9]{64}$/);
});
test('a candidate changed during the real process cannot pass or hide behind opt-in BLOCKED', async () => {
 for(const exit of [0,10]){
  const id=`changed-candidate-${exit}`;const root=path.join(temp,id);const target=path.join(root,'candidate.dll');
  const f=artifactFixture(id,`import fs from 'node:fs';fs.writeFileSync(${JSON.stringify(target)},'changed');process.exitCode=${exit};`);
  fs.writeFileSync(f.candidate,'original');f.adapter.blockedExitCodes=[10];
  const result=await executeAdapter(f.adapter,{repoRoot:f.root,runDir:f.runDir});
  assert.equal(result.status,'FAIL');assert.equal(result.candidateArtifactChanged,true);assert.equal(result.sourceChanged,false);
 }
});
test('artifact paths must be explicit absolute files with a bounded schema', () => {
 const f=artifactFixture('schema-candidate','');
 for(const artifactInputs of [['relative.dll'],['x\ninvalid'],[null],Array(MAX_ARTIFACT_INPUTS+1).fill(f.candidate),'not-an-array'])
   assert.throws(()=>validateAdapters([{...f.adapter,artifactInputs}],f.root),/ARTIFACT_INPUTS_INVALID/);
});

test('a newly installed candidate never replaces the pinned old Host failure control', () => {
 const source=getExtraAdapters();const oldRoot='D:/immutable old baseline';
 const rows=byId(configureCandidateAdapters(source,{...options,legacyHostRoot:oldRoot}));
 const args=rows['fan.open-side-effect-isolation'].args;
 assert(args.includes(oldRoot));assert(!args.includes(options.payloadRoot));
 assert.equal(args[args.indexOf('--hc-assembly')+1],path.join(options.runtimeRoot,'HandheldCompanion.dll'));
 const noOverride=byId(configureCandidateAdapters(source,options))['fan.open-side-effect-isolation'];
 assert(!noOverride.args.includes('--legacy-host-root'));assert(!noOverride.args.includes(options.payloadRoot));
});
test('the CLR compatibility consumer receives the same exact runtime, shared and new Host roots', () => {
 const rows=byId(configureCandidateAdapters(getExtraAdapters(),options));
 const args=rows['lifecycle.runtime-connectivity'].args;
 for(const [flag,value] of [['--runtime-root',options.runtimeRoot],['--shared-runtime-root',options.sharedRuntimeRoot],['--host-payload-root',options.payloadRoot]])
  assert.equal(args[args.indexOf(flag)+1],value);
 assert(rows['lifecycle.runtime-connectivity'].artifactInputs.some(file=>file.endsWith('YeManFanHost.dll')));
});

test('OneX coexistence consumes the explicit current candidate and never reuses the MSI pre-fix override',()=>{
 const result=byId(configureCandidateAdapters(getExtraAdapters(),{...options,legacyHostRoot:'D:/MSI-old-5e'}))['fan.onex-lighting-coexistence'];
 assert.equal(result.args[result.args.indexOf('--candidate-host-root')+1],options.payloadRoot);
 assert.equal(result.args[result.args.indexOf('--hc-assembly')+1],path.join(options.runtimeRoot,'HandheldCompanion.dll'));
 assert(!result.args.includes('D:/MSI-old-5e'));assert(!result.args.includes('--legacy-root'));
 assert(result.artifactInputs.some(file=>file===path.resolve(options.payloadRoot,'YeManFanHost.dll')));
});

function wholeHostSelection(){
 const contractPath=path.join(temp,'complete-host-reviewed.json');
 const contract=JSON.parse(fs.readFileSync(path.join(repo,'tools/ymcc_sim/global/complete-host-selftest/fixtures/98-success-contract.json'),'utf8'));
 fs.writeFileSync(contractPath,JSON.stringify(contract));return {contractPath,contract};
}
test('whole Host stays blocked until a complete reviewed contract is explicitly selected',()=>{
 const a=getExtraAdapters().find(x=>x.id==='fan.complete-host-selftest');assert(a);
 const current=configureCandidateAdapters([a],{payloadRoot:'D:/reviewed candidate'})[0];
 assert.equal(current.blockedReason,'EXPLICIT_COMPLETE_HOST_CONTRACT_REQUIRED');assert.deepEqual(current.args,[]);
});
test('whole Host candidate binds all payload members and actual Node from its explicit contract',()=>{
 const {contractPath,contract}=wholeHostSelection(),payloadRoot=path.join(temp,'whole payload');
 const a=configureCandidateAdapters(getExtraAdapters(),{payloadRoot,completeHostContract:contractPath}).find(x=>x.id==='fan.complete-host-selftest');
 assert.equal(a.blockedReason,undefined);assert.equal(a.requiresExclusiveHost,true);assert.equal(a.requiresPayloadRoot,true);
 assert.deepEqual(a.args,['-EvidenceDir','{outDir}','-ContractPath',contractPath,'-NodeExe',process.execPath]);
 assert.equal(a.artifactInputs.length,contract.payloadFiles.length+2);
 assert(contract.payloadFiles.every(f=>a.artifactInputs.includes(path.join(payloadRoot,f.name))));
 assert(a.artifactInputs.includes(contractPath));assert(a.artifactInputs.includes(process.execPath));
});
test('whole Host cannot infer its candidate or accept relative, unsafe, or non-selftest contracts',()=>{
 const {contractPath,contract}=wholeHostSelection();
 assert.throws(()=>configureCandidateAdapters(getExtraAdapters(),{completeHostContract:contractPath}),/REQUIRES_PAYLOAD_ROOT/);
 for(const completeHostContract of ['relative.json','D:/contract\ninvalid'])assert.throws(()=>configureCandidateAdapters(getExtraAdapters(),{completeHostContract,payloadRoot:temp}),/EXPLICIT_COMPLETE_HOST_CONTRACT_REQUIRED/);
 contract.hostArguments.push('--real-backend');fs.writeFileSync(contractPath,JSON.stringify(contract));
 assert.throws(()=>configureCandidateAdapters(getExtraAdapters(),{completeHostContract:contractPath,payloadRoot:temp}),/ONLY_COMPLETE_SELFTEST_ARGUMENT_ALLOWED/);
});
test('whole Host contract CLI option is explicit and cannot be provided twice',()=>{
 const p=path.join(temp,'contract.json');assert.equal(parseArgs(['--complete-host-contract',p]).completeHostContract,p);
 assert.throws(()=>parseArgs(['--complete-host-contract',p,'--complete-host-contract',p]),/DUPLICATE_ARGUMENT/);
});

test('actual registered audit rejects an explicitly malformed Host contract before any execution',()=>{
 const contract=path.join(temp,'malformed-audit-contract.json');fs.writeFileSync(contract,JSON.stringify({schemaVersion:1}));
 const child=spawnSync(process.execPath,[path.join(repo,'tools/ymcc_sim/global/cli.mjs'),'--mode','audit','--profile','full','--test','fan.complete-host-selftest','--audit-registered','--receipt',path.join(temp,'no-execution-receipt.json'),'--complete-host-contract',contract,'--payload-root',path.join(temp,'unused-payload'),'--out-dir',path.join(temp,'audit-malformed-contract')],{cwd:repo,encoding:'utf8',windowsHide:true,timeout:30000});
 assert.equal(child.status,3,child.stdout+child.stderr);assert.match(child.stderr,/CONTRACT_SCHEMA_INVALID/);
 assert(!child.stdout.includes('RECEIPT_AUDIT '),'invalid explicit contract must not be silently replaced by the default registry');
});

test('actual registered audit requires the explicit payload paired with a valid Host contract',()=>{
 const {contractPath}=wholeHostSelection();
 const child=spawnSync(process.execPath,[path.join(repo,'tools/ymcc_sim/global/cli.mjs'),'--mode','audit','--profile','full','--test','fan.complete-host-selftest','--audit-registered','--receipt',path.join(temp,'no-execution-receipt.json'),'--complete-host-contract',contractPath,'--out-dir',path.join(temp,'audit-missing-payload')],{cwd:repo,encoding:'utf8',windowsHide:true,timeout:30000});
 assert.equal(child.status,3,child.stdout+child.stderr);assert.match(child.stderr,/COMPLETE_HOST_CONTRACT_REQUIRES_PAYLOAD_ROOT/);
});

test('actual registered audit preserves reviewed lighting candidate pin validation',()=>{
 const child=spawnSync(process.execPath,[path.join(repo,'tools/ymcc_sim/global/cli.mjs'),'--mode','audit','--profile','full','--test','lighting.signal-consumer','--audit-registered','--receipt',path.join(temp,'no-execution-receipt.json'),'--lighting-payload-root',path.join(temp,'unused-lighting'),'--expected-helper-sha256','not-a-reviewed-hash','--out-dir',path.join(temp,'audit-invalid-lighting-pin')],{cwd:repo,encoding:'utf8',windowsHide:true,timeout:30000});
 assert.equal(child.status,3,child.stdout+child.stderr);assert.match(child.stderr,/INVALID_HELPER_CANDIDATE_PIN/);
});