import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import os from 'node:os';
import {verifyOriginal,parseArgs} from './run.mjs';import {getTdpProtocolConsumerAdapters,CASE_ID} from './adapter.mjs';
const spec=JSON.parse(fs.readFileSync(new URL('./spec.json',import.meta.url),'utf8'));
const sha='a'.repeat(64);
function raw(){return{schemaVersion:1,kind:'tdp-original-python-protocol',assertOptimization:0,isolated:true,noSite:true,noBytecodeWrite:true,helperMainInvoked:false,physicalHardwareAccepted:false,exitCode:0,failure:null,stdout:'tdp protocol self-test: PASS\n',observation:{forbidden:[],groups:Object.fromEntries(spec.groups.map(name=>[name,{calls:1,returns:1}])),assertLineVisits:{'42':12}},compiledCode:[{path:'original-test.marshal',sha256:sha,sourceSha256:sha},{path:'original-helper.marshal',sha256:sha,sourceSha256:sha}]};}
function fixture(t){const base=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(base,'ymcc-tdp-protocol-contract-'));t.after(()=>{const actual=fs.realpathSync(root);assert.equal(path.dirname(actual),base);assert(path.basename(actual).startsWith('ymcc-tdp-protocol-contract-'));fs.rmSync(actual,{recursive:true,force:true});});return root;}
test('descriptor binds original protocol script/helper and guarded Python job',()=>{
 const[a]=getTdpProtocolConsumerAdapters();assert.equal(a.id,CASE_ID);assert.equal(a.scope,'behavioral-mock');
 for(const source of ['tools/tdp_protocol_selftest.py','PowerControl/pawnio/YeManTdpCtl.py','tools/ymcc_sim/global/host-process.ps1'])assert(a.evidenceInputs.includes(source));
 assert(a.artifactInputs.length>5&&a.artifactInputs.length<640);assert.deepEqual(a.resultFiles,['tdp-protocol/result.json']);
});
test('all five original groups and unoptimized isolated mode are required',()=>{
 assert.equal(verifyOriginal(raw(),spec).groups.length,5);
 for(const field of ['isolated','noSite','noBytecodeWrite']){const value=raw();value[field]=false;assert.throws(()=>verifyOriginal(value,spec),/ASSERTION_OR_RUNTIME_MODE/);}
 const optimized=raw();optimized.assertOptimization=1;assert.throws(()=>verifyOriginal(optimized,spec),/ASSERTION_OR_RUNTIME_MODE/);
});
test('missing and repeated group execution cannot substitute for complete original invocation',()=>{
 const absent=raw();delete absent.observation.groups[spec.groups[4]];assert.throws(()=>verifyOriginal(absent,spec),/GROUP_NOT_EXECUTED/);
 const repeated=raw();repeated.observation.groups[spec.groups[0]].calls=2;assert.throws(()=>verifyOriginal(repeated,spec),/GROUP_NOT_EXECUTED/);
 const unfinished=raw();unfinished.observation.groups[spec.groups[2]].returns=0;assert.throws(()=>verifyOriginal(unfinished,spec),/GROUP_NOT_EXECUTED/);
});
test('both actually observed code objects must be captured',()=>{
 for(const mode of ['missing','bad-hash']){const value=raw();if(mode==='missing')value.compiledCode.pop();else value.compiledCode[0].sha256='bad';assert.throws(()=>verifyOriginal(value,spec),/EXECUTED_CODE_MISSING/);}
});
test('physical claims, helper Main and forbidden side effects cannot accompany a green result',()=>{
 for(const mode of ['main','physical','forbidden']){const value=raw();if(mode==='main')value.helperMainInvoked=true;else if(mode==='physical')value.physicalHardwareAccepted=true;else value.observation.forbidden.push('ctypes.dlopen');assert.throws(()=>verifyOriginal(value,spec),/BOUNDARY_VIOLATION/);}
});
test('a precise original AssertionError distinguishes a real routing fault from runtime failure',()=>{
 const value=raw();value.exitCode=1;value.failure={type:'AssertionError',function:spec.expectedFailedGroup,line:109,sourceLine:'assert calls[-1] == ("cli", *expected, False)'};
 assert.equal(verifyOriginal(value,spec,{negative:true,processExitCode:1}).failureDetected.line,109);
 for(const error of ['RuntimeError','TimeoutError','SyntaxError']){value.failure.type=error;assert.throws(()=>verifyOriginal(value,spec,{negative:true,processExitCode:1}),/ORIGINAL_ASSERTION_NOT_CAUGHT/);}
});
test('wrong original group, zero process exit and missing PASS marker reject fabricated evidence',()=>{
 const value=raw();value.exitCode=1;value.failure={type:'AssertionError',function:'other-group',sourceLine:'assert calls[-1]'};assert.throws(()=>verifyOriginal(value,spec,{negative:true,processExitCode:1}),/ORIGINAL_ASSERTION_NOT_CAUGHT/);
 value.failure.function=spec.expectedFailedGroup;assert.throws(()=>verifyOriginal(value,spec,{negative:true,processExitCode:0}),/ORIGINAL_ASSERTION_NOT_CAUGHT/);
 const positive=raw();positive.stdout='summary only';assert.throws(()=>verifyOriginal(positive,spec),/ORIGINAL_NOT_PASS/);
});
test('CLI refuses reused/relative outputs and extra or unknown options',t=>{
 const root=fixture(t);for(const args of [[],['--unknown',root],['--evidence-dir','relative'],['--evidence-dir',root],['--evidence-dir',root,'--extra','x']])assert.throws(()=>parseArgs(args));
 assert.equal(parseArgs(['--evidence-dir',path.join(root,'fresh')]).root,path.join(root,'fresh'));
});
