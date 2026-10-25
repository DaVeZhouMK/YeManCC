import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {nativeCompilerInputClosure} from './compiler_inputs.mjs';
const root=path.resolve('Build-fixture-compiler-closure/source');
const inert=path.join(root,'native/main.inert.cpp');
const row=rel=>({path:rel,snapshot:path.join(root,rel),sha256:'a'.repeat(64)});
function input(){return {records:['native/main.cpp','native/dsu_server.cpp','native/virtual_start_protection.h','native/custom-steam-library/steam_artwork_lab.cpp','tools/input_coordinator_lifecycle_selftest.mjs'].map(row),buildLog:['注意: 包含文件: '+inert,'Note: including file: '+path.join(root,'native/virtual_start_protection.h'),'注意: 包含文件: C:\\WindowsSdk\\shared.h'].join('\r\n'),snapshotRoot:root,generatedFiles:[inert],requiredSources:['native/main.cpp','native/dsu_server.cpp','tools/input_coordinator_lifecycle_selftest.mjs']};}
test('actual headers, two primary sources and generator stay bound; unrelated TU stays archived',()=>{const j=nativeCompilerInputClosure(input());assert.deepEqual(j.records.map(x=>x.path),['native/main.cpp','native/dsu_server.cpp','native/virtual_start_protection.h','tools/input_coordinator_lifecycle_selftest.mjs']);assert.deepEqual(j.copiedButUncompiled.map(x=>x.path),['native/custom-steam-library/steam_artwork_lab.cpp']);});
test('an additional actually included TU cannot disappear through suffix filtering',()=>{const i=input();i.buildLog+='\nNote: including file: '+path.join(root,'native/custom-steam-library/steam_artwork_lab.cpp');assert.equal(nativeCompilerInputClosure(i).copiedButUncompiled.length,0);});
test('missing consumed header record rejects',()=>{const i=input();i.records=i.records.filter(x=>!x.path.endsWith('.h'));assert.throws(()=>nativeCompilerInputClosure(i),/UNBOUND_SNAPSHOT_COMPILER_INPUT/);});
test('missing primary source rejects',()=>{const i=input();i.records=i.records.filter(x=>x.path!=='native/main.cpp');assert.throws(()=>nativeCompilerInputClosure(i),/REQUIRED_NATIVE_SOURCE_MISSING/);});
test('missing compiler observations rejects',()=>{const i=input();i.buildLog='compile succeeded';assert.throws(()=>nativeCompilerInputClosure(i),/COMPILER_INCLUDE_OBSERVATIONS_REQUIRED/);});
test('missing transformed native body rejects',()=>{const i=input();i.buildLog='Note: including file: C:\\WindowsSdk\\shared.h';assert.throws(()=>nativeCompilerInputClosure(i),/COMPILED_NATIVE_BODY_NOT_OBSERVED/);});
test('unbound generated input rejects',()=>{const i=input();i.buildLog+='\nNote: including file: '+path.resolve('unowned-generated.inc');assert.throws(()=>nativeCompilerInputClosure(i),/UNBOUND_EXTERNAL_COMPILER_INPUT/);});
test('duplicate snapshot record rejects',()=>{const i=input();i.records.push(i.records[0]);assert.throws(()=>nativeCompilerInputClosure(i),/DUPLICATE_SNAPSHOT_RECORD/);});
