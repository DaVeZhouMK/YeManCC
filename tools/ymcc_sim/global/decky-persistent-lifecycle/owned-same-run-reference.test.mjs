// Existing sharing boundary controls. Parent/child envelopes below are synthetic;
// the real leaf evidence stays untouched and is never a new global-run PASS.
import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
import {sameRunOriginal,validateOriginalResult} from './owned-run.mjs';
import {OWNED_INPUTS} from './owned-adapter.mjs';
const repo=fileURLToPath(new URL('../../../../',import.meta.url)),core=path.resolve(repo,'../../../Testing/YMCC'),out=path.join(core,'Evidence/SameRunTypedSharing-20261105'),privateParent=path.join(out,'Private');
fs.mkdirSync(privateParent,{recursive:true});const privateRoot=fs.mkdtempSync(path.join(privateParent,'run-'));
const actual=path.join(core,'Evidence/TypedLeafCost-20261105'),original=path.join(actual,'o'),read=file=>JSON.parse(fs.readFileSync(file,'utf8')),sha=b=>crypto.createHash('sha256').update(b).digest('hex'),pin=file=>({path:file,sha256:sha(fs.readFileSync(file))});
const testedPins=read(path.join(original,'SOURCE-PINS.json')).files,sourceView=path.join(privateRoot,'source-view'),originalSources=new Map(testedPins.map(row=>[row.path,row]));
for(const relative of OWNED_INPUTS){const frozen=originalSources.has(relative)?path.join(original,'source',relative):path.join(repo,relative),bytes=fs.readFileSync(frozen);if(originalSources.has(relative))assert.equal(sha(bytes),originalSources.get(relative).sha256);const target=path.join(sourceView,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});}
const sourceHashes=Object.fromEntries(OWNED_INPUTS.map(relative=>[relative,pin(path.join(sourceView,relative)).sha256]));
const process=read(path.join(actual,'ORIGINAL-LEAF-PROCESS.json')),proof={deferredRaw:pin(path.join(original,'runtime-fixtures/deferred-resource-branch/result.json')),raw:pin(path.join(original,'typed-runtime-result.json')),result:pin(path.join(original,'RESULT.json')),sourcePins:pin(path.join(original,'SOURCE-PINS.json')),exe:pin(path.join(original,'FixtureSteam/steam.exe'))};
const actualRawPins=[...Object.values(proof),pin(path.join(actual,'original-typed.stdout.log')),pin(path.join(actual,'original-typed.stderr.log'))];
let serial=0;
function fixture(){const root=path.join(privateRoot,'control-'+serial++),cases=path.join(root,'cases'),parentDir=path.join(cases,'plugins.decky-persistent-lifecycle/decky-persistent-lifecycle'),childDir=path.join(root,'control-child');fs.mkdirSync(parentDir,{recursive:true});fs.mkdirSync(childDir);
 const childFile=path.join(childDir,'result.json'),execution={id:'original-typed',program:pin(process.program),arguments:[path.join(sourceView,'tools/decky_binding_runtime_selftest.mjs'),original],cwd:process.cwd,timeoutMs:135000,exitCode:process.exitCode,signal:process.signal,error:process.error,elapsedMs:process.elapsedMs,stdout:pin(path.join(actual,'original-typed.stdout.log')),stderr:pin(path.join(actual,'original-typed.stderr.log'))};
 const child={id:'plugins.decky-current-parent-migration',sourceUnchanged:true,toolsUnchanged:true,sourceInputs:OWNED_INPUTS.map(p=>({path:p,sha256:sourceHashes[p]})),originalProducer:structuredClone(proof),executions:[execution],controlEnvelopeOnly:true};
 const names=['SOURCE-PINS.json','TOOL-PINS.json','REGRESSION-COVERAGE.json','LEGACY-TYPED-MIGRATION.json','RAW-EVIDENCE.json','HISTORY.json'],metadata=names.map(name=>{const file=path.join(parentDir,name);fs.writeFileSync(file,JSON.stringify({controlOnly:true,name}),{flag:'wx'});return pin(file);});
 const parent={id:'plugins.decky-persistent-lifecycle',status:'INCOMPLETE',passed:false,fullPersistentAcceptance:false,metadataIntegrity:true,metadataEvidence:metadata,sourceHashes:structuredClone(sourceHashes),childEvidence:childDir,childWork:actual,rawEvidence:[],controlEnvelopeOnly:true};
 const parentFile=path.join(parentDir,'result.json'),target=path.join(cases,'plugins.decky-owned-runtime-lifecycle/decky-owned-runtime-lifecycle');
 function save(){fs.writeFileSync(childFile,JSON.stringify(child));parent.rawEvidence=[pin(childFile)];fs.writeFileSync(parentFile,JSON.stringify(parent));}save();return {root,cases,parentDir,parentFile,childFile,target,parent,child,save,run:()=>sameRunOriginal(target,sourceView)};
}
test('same-run typed sharing returns existing complete original without producer or compile',()=>{const f=fixture(),r=f.run();assert.equal(r.original,original);assert.equal(r.execution.timeoutMs,135000);assert.equal(validateOriginalResult(read(proof.raw.path),read(proof.result.path)).cases,42);assert(!fs.existsSync(f.target));assert.equal(read(r.parent.path).fullPersistentAcceptance,false);assert.equal(read(r.parent.path).status,'INCOMPLETE');assert.equal(r.execution.controlEnvelopeOnly,undefined);});
test('standalone or missing-sibling calls retain original fresh-run selection',()=>{const f=fixture();assert.equal(sameRunOriginal(path.join(f.root,'standalone'),sourceView),null);assert.equal(sameRunOriginal(path.join(f.root,'another/cases/plugins.decky-owned-runtime-lifecycle/decky-owned-runtime-lifecycle'),sourceView),null);});
for(const [name,mutate,expected]of [
 ['duplicate metadata',f=>f.parent.metadataEvidence[1]=f.parent.metadataEvidence[0],/SAME_RUN_METADATA_IDENTITY/],
 ['parent source drift',f=>f.parent.sourceHashes[OWNED_INPUTS[0]]='0'.repeat(64),/SAME_RUN_PARENT_SOURCE_CHANGED/],
 ['child source drift',f=>f.child.sourceInputs[0].sha256='0'.repeat(64),/SAME_RUN_CHILD_SOURCE_CHANGED/],
 ['changed original proof',f=>f.child.originalProducer.raw.sha256='0'.repeat(64),/SAME_RUN_ORIGINAL_CHANGED/],
 ['failed original process',f=>f.child.executions[0].exitCode=1,/0/],
 ['expanded original budget',f=>f.child.executions[0].timeoutMs=135001,/135000/],
 ['selected-only original command',f=>f.child.executions[0].arguments.push('native.typed-ready-rebind'),/SAME_RUN_COMPLETE_COMMAND_REQUIRED/],
 ['original deadline overrun',f=>f.child.executions[0].elapsedMs=135001,undefined]
])test('same-run typed sharing rejects '+name,()=>{const f=fixture();mutate(f);f.save();assert.throws(f.run,expected);});
test('same-run typed sharing refuses changed metadata and bound child bytes',()=>{const f=fixture();const file=f.parent.metadataEvidence[0].path;fs.appendFileSync(file,'changed');assert.throws(f.run,/SAME_RUN_METADATA_CHANGED/);const g=fixture();fs.appendFileSync(g.childFile,' ');assert.throws(g.run,/SAME_RUN_CURRENT_CHILD_CHANGED/);});
test('same-run controls leave all real original raw and logs unchanged',()=>{for(const row of actualRawPins)assert.equal(pin(row.path).sha256,row.sha256);});
