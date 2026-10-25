import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {resolveLightingRuntimeInputs,describeLightingPayload,localImportClosure,lightingArtifactInputs,lightingToolDeclaration} from './input-closure.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
function fixture(t){const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'lighting-declaration-'));t.after(()=>{const target=fs.realpathSync(root);assert.equal(path.dirname(target),parent);assert(path.basename(target).startsWith('lighting-declaration-'));fs.rmSync(target,{recursive:true,force:true});});return root;}
test('default runtime uses repository overlay plus exact profile source Shared',()=>{
 const runtime=resolveLightingRuntimeInputs(repo),profile=JSON.parse(fs.readFileSync(runtime.profilePath));
 assert.equal(runtime.hcCore,path.join(repo,'deps/ymcc-device-core-runtime/bin/HandheldCompanion.dll'));
 assert.equal(runtime.sharedDll,path.join(repo,profile.sourceDirectory,'Shared.dll'));
 assert.equal(runtime.selection,'repository-profile-overlay');
});
test('explicit runtime uses both explicit PE files without borrowing default Shared',()=>{
 const runtime=resolveLightingRuntimeInputs(repo,{runtimeRoot:path.join(os.tmpdir(),'lighting-explicit-runtime')});
 assert.equal(runtime.sharedDll,path.join(runtime.runtimeRoot,'Shared.dll'));
 assert.equal(runtime.selection,'explicit-runtime-root');
});
test('no payload request performs no external payload lookup',()=>{
 const result=describeLightingPayload({},{exists(){throw Error('unexpected lookup');}});
 assert.equal(result.requested,false);assert.equal(result.file,null);
});
test('external payload retains its own reviewed pin and distinct provenance',()=>{
 const root=path.join(os.tmpdir(),'lighting-explicit-payload'),pin='A'.repeat(64);
 const result=describeLightingPayload({hostPayloadRoot:root,expectedHelperSha256:pin},{exists:file=>file===path.join(root,'light-setter/YeManLightSetter.dll'),hashFile:()=>pin.toLowerCase()});
 assert.equal(result.expectedReviewedSha256,pin.toLowerCase());assert.equal(result.actual,pin.toLowerCase());
 assert.equal(result.origin,'explicit-reviewed-external-payload');assert.equal(result.sourceBuildAcceptanceReused,false);
});
test('missing external candidate remains missing even when a pin was supplied',()=>{
 const result=describeLightingPayload({hostPayloadRoot:os.tmpdir(),expectedHelperSha256:'b'.repeat(64)},{exists:()=>false,hashFile(){throw Error('missing candidate must not be replaced');}});
 assert.equal(result.file,null);assert.equal(result.actual,null);assert.equal(result.requested,true);
});
test('local UI import closure includes real nested components and data',()=>{
 const members=localImportClosure(repo,['src/components/ControllerFeedbackCapabilitiesCard.vue','src/bridge/api.ts']);
 for(const file of ['src/components/AppIcon.vue','src/components/SwitchControl.vue','src/icons.ts','src/icons.json'])assert(members.includes(file),file);
 assert(members.includes('src/bridge/api.ts'),'the executed lifecycle API source stays declared');
});
test('missing local imports are rejected rather than silently omitted',t=>{
 const root=fixture(t);fs.writeFileSync(path.join(root,'entry.ts'),"import './missing.ts';");
 assert.throws(()=>localImportClosure(root,['entry.ts']),/LIGHTING_SOURCE_IMPORT_MISSING/);
});
test('local import cannot escape the declared repository',t=>{
 const root=fixture(t);fs.writeFileSync(path.join(root,'entry.ts'),"import '../escape.ts';");
 assert.throws(()=>localImportClosure(root,['entry.ts']),/LIGHTING_SOURCE_IMPORT_ESCAPE/);
});
test('artifact declaration includes actual default PE bytes and preserves 3072 limit',()=>{
 const artifacts=lightingArtifactInputs(repo),runtime=resolveLightingRuntimeInputs(repo);
 assert(artifacts.includes(runtime.hcCore));assert(artifacts.includes(runtime.sharedDll));assert(artifacts.length<=3072);
 assert(!artifacts.some(file=>/[/\\]SOFT[/\\]YeMan(?:[/\\]|$)/i.test(file)));
});
test('selected interpreter/compiler tools have declared pinned members',()=>{
 const tool=lightingToolDeclaration(repo);
 for(const name of ['node','dotnet','python','cl','edge'])assert(tool.files.some(row=>path.resolve(row.path).toLowerCase()===path.resolve(tool.manifest[name]).toLowerCase()),name);
});

test('whole mocked aliases do not require a real module or imply an actual IPC consumer',t=>{const root=fixture(t);fs.writeFileSync(path.join(root,'entry.ts'),"import {invoke} from '@/bridge/ipc';");assert.deepEqual(localImportClosure(root,['entry.ts']),['entry.ts']);});
