import test from 'node:test';import assert from 'node:assert/strict';
import {originalCaseNames,parseArgs,verifyOriginalOutput} from './run.mjs';
const policyFaults=[
 ['ready rejects every missing evidence field and invalid slot','!ep::ready(rejected)'],
 ['no candidate and mixed unknown virtual nonfresh stay idle','!r.available'],
 ['selected nonfresh virtual unknown count as absence','!r.available'],
];
const runtimeFaults=[['R2 source publication blocks on consumer mutex and rechecks context inside lock','accepted == (scenario == 0)']];
function evidence(kind,negative=false){
 const runtime=kind==='runtime',prefix=`EXTERNALPAD_${kind.toUpperCase()}`;
 const faults=new Map(runtime?runtimeFaults:policyFaults),caseNames=['untouched assertion',...faults.keys()];
 const rows=caseNames.map(name=>negative&&faults.has(name)?`FAIL ${name}: line 230: ${faults.get(name)}`:`PASS ${name}`);
 const total=rows.length,passed=negative?total-faults.size:total;
 const summary=`${prefix}_SUMMARY passed=${passed} total=${total} assertions=42 hardwareAccess=false productRun=false productionHeader=true${runtime?' operations=faked-external-effects ledgerReads=private-temp-fixtures':''}`;
 // Runtime raw concatenates stdout and stderr, so failed cases can follow the summary.
 const log=(runtime?[...rows.filter(row=>row.startsWith('PASS ')),summary,...rows.filter(row=>row.startsWith('FAIL '))]:[...rows,summary]).join('\n')+'\n';
 const names=negative&&!runtime?['release']:['release','checked'];
 const profiles=new Map(names.map((name,i)=>[name,{log,binarySha256:(i?'b':'a').repeat(64)}]));
 const [script,fixture,header]=['ps1','cpp','h'].map((ext,i)=>({path:`C:/fixture/original.${ext}`,sha256:String(i+1).repeat(64)}));
 const sourcePins=[script,fixture,header],receiptFile='C:/fixture/verification-summary.txt';
 const profileReceipt=[...profiles].map(([name,entry])=>`profile=${name}${runtime?` exitCode=${negative?1:0}`:''} ${summary} binarySHA256=${entry.binarySha256.toUpperCase()}`);
 const receipt=runtime?[...sourcePins.map(row=>`sourceSHA256=${row.sha256} path=${row.path}`),...profileReceipt]
  :negative?null:[`productionHeaderSHA256=${header.sha256} path=${header.path}`,...profileReceipt,`fixtureSHA256=${fixture.sha256}`];
 const stdout='YMCC_SIM_OWNED_JOB childPid=42\n'+log.repeat(names.length)
  +(runtime?`${prefix}_RECEIPT ${receiptFile}\n`:'')+(!negative?`${prefix}_PASSED ${receiptFile}\n`:'');
 const expectedError=runtime?'Runtime regressions failed or production changed during verification. Failures remain failures; see the receipt.':'Policy selftest failed: release (exit 1)';
 const stderr=negative?`Exception: ${script.path}:230\n     | ${expectedError}\n`:'';
 return{caseNames,sourcePins,profiles,receiptFile,receipt:receipt&&receipt.join('\n')+'\n',stdout,stderr,negative,exitCode:negative?1:0};
}
test('reads unique original scene declarations without a second full scene catalog',()=>{
 const cpp='    run("first", [] { });\r\n    run("second", [] { });\r\n';
 assert.deepEqual(originalCaseNames(cpp),['first','second']);
 assert.throws(()=>originalCaseNames(cpp+cpp),/ORIGINAL_CASE_DECLARATIONS_INVALID/);
 assert.throws(()=>originalCaseNames('void run() {}'),/ORIGINAL_CASE_DECLARATIONS_INVALID/);
});
for(const kind of ['policy','runtime']){
 test(`${kind}: binds exact original case identity to each configuration`,()=>{
  const good=evidence(kind),result=verifyOriginalOutput(kind,good);
  assert.equal(result.checks,good.caseNames.length*2);
  assert.deepEqual(result.rows.map(row=>row.profile),['release','checked']);
  for(const error of [
   text=>text.replace('PASS untouched assertion\n',''),
   text=>text.replace('PASS untouched assertion\n','PASS different name\n'),
   text=>text.replace('PASS untouched assertion\n',`PASS ${good.caseNames[1]}\n`),
  ]){
   const input=evidence(kind),checked=input.profiles.get('checked');
   checked.log=error(checked.log);
   assert.throws(()=>verifyOriginalOutput(kind,input),/ORIGINAL_CASE_SET_MISMATCH:checked/);
  }
  const reordered=evidence(kind),checked=reordered.profiles.get('checked');
  const [first,second,...rest]=checked.log.split('\n'),original=checked.log;
  checked.log=[second,first,...rest].join('\n');
  reordered.stdout=reordered.stdout.replace(original+original,original+checked.log);
  assert.equal(verifyOriginalOutput(kind,reordered).status,'PASS');
  const wrongSummary=evidence(kind);
  wrongSummary.profiles.get('checked').log=checked.log.replace(/total=\d+/, 'total=999');
  assert.throws(()=>verifyOriginalOutput(kind,wrongSummary),/ORIGINAL_SUMMARY_MISSING_OR_INVALID:checked/);
 });
 test(`${kind}: completion receipt and raw must belong to the actual configuration`,()=>{
  const input=evidence(kind);
  input.profiles.delete('checked');
  assert.throws(()=>verifyOriginalOutput(kind,input),/ORIGINAL_PROFILE_SET_MISMATCH/);
  const borrowed=evidence(kind);
  borrowed.profiles.set('checked',borrowed.profiles.get('release'));
  assert.throws(()=>verifyOriginalOutput(kind,borrowed),/ORIGINAL_RECEIPT_MISMATCH/);
  const good=evidence(kind);
  assert.throws(()=>verifyOriginalOutput(kind,{...good,receipt:null}),/ORIGINAL_RECEIPT_MISMATCH/);
  assert.throws(()=>verifyOriginalOutput(kind,{...good,stdout:good.stdout.replace(/.*_PASSED .+\n/,'')}),/ORIGINAL_STDOUT_MISMATCH/);
  assert.throws(()=>verifyOriginalOutput(kind,{...good,exitCode:3}),/ORIGINAL_POSITIVE_NOT_PASS/);
 });
 test(`${kind}: only the specified assertion failures satisfy the source mutation`,()=>{
  const good=evidence(kind,true),result=verifyOriginalOutput(kind,good);
  assert.equal(result.status,'EXPECTED_REJECTION');
  assert.equal(result.profiles,kind==='policy'?1:2);
  const wrongFailure=evidence(kind,true),release=wrongFailure.profiles.get('release');
  release.log=release.log.replace(/(FAIL .*: line \d+: ).+/, '$1unrelatedAssertion()');
  assert.throws(()=>verifyOriginalOutput(kind,wrongFailure),/ORIGINAL_CASE_OUTCOME_MISMATCH:release/);
  assert.throws(()=>verifyOriginalOutput(kind,{...good,exitCode:2}),/ORIGINAL_FAULT_TERMINAL_MISMATCH/);
  assert.throws(()=>verifyOriginalOutput(kind,{...good,stderr:'Compilation failed'}),/ORIGINAL_FAULT_TERMINAL_MISMATCH/);
  if(kind==='runtime'){
   const timeout=good.receipt.replace('profile=checked exitCode=1','profile=checked FIXTURE_TIMEOUT seconds=30 exitCode=1');
   assert.throws(()=>verifyOriginalOutput(kind,{...good,receipt:timeout}),/ORIGINAL_RECEIPT_MISMATCH/);
  }
 });
}
test('strict selection refuses unknown, incomplete and duplicate arguments',()=>{
 assert.equal(parseArgs(['--test','controller.external-priority-policy','--evidence-dir','owned']).id,'controller.external-priority-policy');
 for(const args of [[],['--test','unknown','--evidence-dir','x'],['--test','controller.external-priority-policy'],['--test','controller.external-priority-policy','--evidence-dir','x','--evidence-dir','y']])assert.throws(()=>parseArgs(args));
});
