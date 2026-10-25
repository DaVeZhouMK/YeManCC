import test from 'node:test';
import assert from 'node:assert/strict';
import {LEGACY_MANAGED_CONTRACT,TERMINAL_MANAGED_CONTRACT,managedCompileContract,validateManagedCompileRows} from './r3-managed-input-contract.mjs';
import {requireCurrentCompileBatch} from './r3-current-compile-batch.mjs';
const rows=kind=>managedCompileContract(kind).names.map(relative=>({relative,sha256:'a'.repeat(64)}));
test('legacy21 remains the default and does not contain terminal22',()=>{
 const contract=managedCompileContract();assert.equal(contract.id,LEGACY_MANAGED_CONTRACT);assert.equal(contract.count,21);
 assert.equal(contract.names.includes('InputHost/HostTerminalCleanup.cs'),false);assert.equal(validateManagedCompileRows(rows(LEGACY_MANAGED_CONTRACT)).id,contract.id);
});
test('terminal22 explicitly requires the production terminal module',()=>{
 const contract=managedCompileContract(TERMINAL_MANAGED_CONTRACT);assert.equal(contract.count,22);
 assert.equal(contract.names.filter(n=>n==='InputHost/HostTerminalCleanup.cs').length,1);
 assert.equal(validateManagedCompileRows(rows(TERMINAL_MANAGED_CONTRACT),contract.id),contract);
});
test('a terminal22 member list cannot be relabeled legacy21',()=>assert.throws(()=>validateManagedCompileRows(rows(TERMINAL_MANAGED_CONTRACT)),/EXACT21/));
test('legacy21 cannot pass a terminal22 contract',()=>assert.throws(()=>validateManagedCompileRows(rows(LEGACY_MANAGED_CONTRACT),TERMINAL_MANAGED_CONTRACT),/EXACT22/));
test('missing duplicate foreign malformed and extra terminal members are refused',()=>{
 const good=rows(TERMINAL_MANAGED_CONTRACT);
 for(const bad of [good.slice(1),[...good,good[0]],good.map((r,i)=>i===3?good[0]:r),good.map((r,i)=>i===3?{...r,relative:'InputHost/FakeTerminal.cs'}:r),good.map((r,i)=>i===3?{...r,sha256:'broken'}:r)])
  assert.throws(()=>validateManagedCompileRows(bad,TERMINAL_MANAGED_CONTRACT));
});
test('unknown or guessed protocol versions cannot grant membership',()=>{
 for(const kind of ['terminal22',22,null,'current105-exact22-v1',{},''])assert.throws(()=>managedCompileContract(kind),/UNKNOWN/);
});
test('membership contracts are immutable and never compile/runtime capability tokens',()=>{
 const contract=managedCompileContract(TERMINAL_MANAGED_CONTRACT);assert(Object.isFrozen(contract));assert(Object.isFrozen(contract.names));
 for(const fake of [contract,{kind:'CURRENT_CAPTURE_COMPILE_ONLY_IN_PROCESS_CAPABILITY'},JSON.parse(JSON.stringify(contract))])
  assert.throws(()=>requireCurrentCompileBatch(fake),/LIVE_BRANDED_BATCH_REQUIRED/);
});
