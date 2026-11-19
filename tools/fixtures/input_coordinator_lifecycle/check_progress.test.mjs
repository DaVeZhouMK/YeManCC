import fs from 'node:fs';import test from 'node:test';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {observeCurrent105Progress} from './check_progress.mjs';import {observeCurrent105Fixture} from '../../input_coordinator_current105_source_adapters.mjs';import {buildCurrentCompileOnly} from '../../input_coordinator_current_compile_only.mjs';
const profile=JSON.parse(fs.readFileSync(new URL('../../ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-compile-source-profile.json',import.meta.url),'utf8'));
const source=observeCurrent105Fixture(fs.readFileSync(profile.sourceCapture.root+'/'+profile.fixture.relative,'utf8'),{sourceRoot:profile.sourceCapture.root,managedInputContract:profile.managedInputContract}).text,sha=x=>createHash('sha256').update(x).digest('hex');
test('real current105 fixture gains progress observation while retaining original check records',()=>{const out=observeCurrent105Progress(source);assert(out.wholeOriginalRestored&&out.originalAssertionsUnchanged);assert.equal(out.originalSha256,sha(source));assert.equal(out.observedSha256,sha(out.text));assert(out.text.includes('fixtureRecordCheckProgress(assertionTrace.back());'));assert(out.text.includes('if(!good)failures.push_back'));});
test('duplicate observation and changed assertion endpoint are refused',()=>{assert.throws(()=>observeCurrent105Progress(observeCurrent105Progress(source).text),/ALREADY_PRESENT/);assert.throws(()=>observeCurrent105Progress(source.replace('if(!good)failures.push_back','if(!good)other.push_back')),/EXACT_CURRENT105_FAILURE_RECORD/);});
test('compile producer refuses invalid diagnostic options before any compile authority or output',()=>{assert.throws(()=>buildCurrentCompileOnly({checkProgress:'true'}),/BOOLEAN_CHECK_PROGRESS/);assert.throws(()=>buildCurrentCompileOnly({runtimeRoot:'D:/outside/current105'}),/RUNTIME_ROOT_OUTSIDE_CORE_EVIDENCE/);});

test('explicit functional optimization never bypasses live compile authority and unknown options refuse early',()=>{
 for(const nativeOptimization of ['O2','Od'])
  assert.throws(()=>buildCurrentCompileOnly({argv:['--out','G:/unit-native-output-never-created','--compile-only'],nativeOptimization}),/LIVE_BRANDED_BATCH_REQUIRED/);
 for(const nativeOptimization of ['O3','fast',null,2])
  assert.throws(()=>buildCurrentCompileOnly({nativeOptimization}),/CURRENT_NATIVE_OPTIMIZATION_REQUIRED/);
});
