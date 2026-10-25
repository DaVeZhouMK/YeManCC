import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const args=process.argv.slice(2);
assert(args.length===2&&args[0]==='--out'&&path.isAbsolute(args[1]),'Use --out ABS_NEW_DIRECTORY');
const modulePath=path.join(repo,'tools/ymcc_sim/global/controller-prerequisite-environment');
const spec=JSON.parse(fs.readFileSync(path.join(modulePath,'toolchain-inputs.json'),'utf8'));
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
assert(sha(spec.python)===spec.sha256,'Python interpreter differs from evidence toolchain');
const legacy=path.join(repo,'tools/fixtures/gamepad_package_preparation/legacy');
const provenance=JSON.parse(fs.readFileSync(path.join(legacy,'provenance.json'),'utf8'));
for(const row of provenance.files)
 assert(sha(path.join(legacy,row.file))===row.sha256,'Historical consumer source changed');
const command=[path.join(repo,'tools/gamepad_package_preparation_selftest.py'),
 '--out',path.resolve(args[1]),'--legacy-source',path.join(legacy,'Program.cs.txt'),
 '--legacy-auto-source',path.join(legacy,'AutoSetup.cs.txt'),'--configuration','Checked'];
const result=spawnSync(spec.python,command,{cwd:repo,encoding:'utf8',windowsHide:true,
 timeout:165000,maxBuffer:2*1024*1024});
if(result.stdout)process.stdout.write(result.stdout);
if(result.stderr)process.stderr.write(result.stderr);
if(result.error)throw result.error;
assert(sha(spec.python)===spec.sha256,'Python changed during run');
if(result.status!==0){process.exitCode=result.status??1;}
else {
 const detailedPath=path.join(path.resolve(args[1]),'RESULT.json');
 const detailed=JSON.parse(fs.readFileSync(detailedPath,'utf8'));
 assert(detailed.status==='PASS_ACTUAL_PREREQUISITE_CONSUMER_OLD_FAULT_AND_CONTROLS');
 assert(detailed.sourceDrift.length===0 && detailed.sdkMethodsInvoked===0 &&
  detailed.deviceDriverRegistryCertificateOperations===false && detailed.normalSetupExecuted===false);
 const historical=detailed.cases.find(c=>c.variant==='old-policy-red');
 const positive=detailed.cases.find(c=>c.variant==='actual-consumer-green');
 assert(historical?.exit===1 && historical.status==='EXPECTED_OLD_CODE_FAILURE_REPRODUCED');
 assert(positive?.exit===0 && positive.status==='PASS_ACTUAL_PRODUCTION_CONSUMER' && positive.receipt.status==='PASS');
 const names=['mutation-legacy-as-current','mutation-raw-CAT-required','mutation-old-dispatch-restored',
  'mutation-postcondition-bypassed','mutation-observed-pnputil-schema-rejected',
  'mutation-complete-pair-vetoed-by-extra-version','mutation-unverified-extra-allows-maintenance',
  'mutation-deferred-collapsed-to-dependency-failure','mutation-deferred-without-current-pair',
  'mutation-controller-client-guard-bypassed'];
 assert(detailed.cases.length===names.length+2,'Unexpected production/control consumer set');
 const controls=names.map(name=>{
  const row=detailed.cases.find(c=>c.variant===name);
  assert(row?.exit===1 && row.status==='COMPILED_PRODUCTION_MUTATION_CAUGHT' &&
   row.receipt.status==='FAIL' && row.receipt.failures.length>0,'Expected compiled control not caught: '+name);
  return {name,caught:true,expectedAssertionFailures:row.receipt.failures.length};
 });
 const output={status:'PASS',configuration:detailed.configuration,
  actualCases:positive.receipt.cases,actualChecks:positive.receipt.checks,
  historicalReadinessFailureReproduced:true,controls,
  detail:{path:detailedPath,sha256:sha(detailedPath)},
  sourceDrift:[],sdkMethodsInvoked:0,deviceOperations:false,normalSetupExecuted:false,
  physicalHardwareValidated:false,gameConsumptionProven:false};
 fs.writeFileSync(path.join(path.resolve(args[1]),'global-result.json'),JSON.stringify(output,null,2)+'\n',{flag:'wx'});
 console.log(JSON.stringify({status:output.status,actualCases:output.actualCases,actualChecks:output.actualChecks,expectedControls:controls.length}));
}
