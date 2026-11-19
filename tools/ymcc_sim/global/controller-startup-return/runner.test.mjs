import test from 'node:test';import assert from 'node:assert/strict';import {verifyRuntime,verifyLedger} from './provenance.mjs';
import {startupToolchainPreamble,startupToolchainEnvironment} from './toolchain.mjs';
test('failed compilation is never a startup-return semantic negative',()=>{
 for(const code of [1,null,undefined])assert.throws(()=>verifyRuntime(null,{}, {buildExitCode:code,executeExitCode:1,negative:true}),/COMPILE_FAILURE/);
});
test('missing original runtime reports cannot pass via generic nonzero exit',()=>{
 assert.throws(()=>verifyRuntime({tests:75,assertions:374,cases:[]},{tests:75,assertions:374,caseNames:[]},{buildExitCode:0,executeExitCode:1,negative:true}),/ORIGINAL_TEST/);
});
test('ledger rejects missing or duplicate summary-only profile evidence',()=>{
 const line='EXTERNALPAD_LEDGER_SUMMARY passed=13 total=13 assertions=8729 hardwareAccess=false productRun=false filesystem=private-temp-only\n';
 assert.throws(()=>verifyLedger('Build succeeded.','',{exitCode:0}),/TERMINATION_INCOMPLETE/);
 assert.throws(()=>verifyLedger(line,'',{exitCode:0}),/TERMINATION_INCOMPLETE/);
 assert.throws(()=>verifyLedger(line+line+'EXTERNALPAD_LEDGER_PASSED fixture\n','',{exitCode:0}),/ROOTS_REQUIRED/);
});
test('fixed startup compiler recipe binds declared roots and rejects unknown or malformed selections',()=>{
 const fixed={kind:'fixed-startup-return-x64-v1',msvcRoot:'C:/fixture/vc/14.44.35207',sdkRoot:'C:/fixture/sdk',sdkVersion:'10.0.26100.0',windowsRoot:'C:/Windows'};
 const text=startupToolchainPreamble(fixed);
 assert(text.includes('14.44.35207'));assert(text.includes('10.0.26100.0'));assert(text.includes('set "VSLANG=1033"'));
 assert(!text.includes('%PATH%')&&!text.includes('%INCLUDE%')&&!text.includes('%LIB%')&&!text.includes('vcvars')&&!text.includes('call '));
 const environment=startupToolchainEnvironment(fixed,{Path:'C:/unrelated/compiler',include:'C:/unrelated/include',Lib:'C:/unrelated/lib',KEEP:'retained'});
 assert(!Object.values(environment).some(value=>value.includes('unrelated')));assert(environment.PATH.includes('14.44.35207'));assert(environment.COMSPEC.endsWith('System32\\cmd.exe'));assert.equal(environment.KEEP,'retained');assert(!Object.hasOwn(environment,'Path'));
 for(const changed of [{kind:'unreviewed'},{sdkVersion:'latest'},{sdkRoot:'relative'},{msvcRoot:'C:/fixture/"bad'},{windowsRoot:'C:/%ambient%'}])assert.throws(()=>startupToolchainPreamble({...fixed,...changed}));
});
