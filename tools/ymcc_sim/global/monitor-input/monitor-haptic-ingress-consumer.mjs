// Executes the existing no-device early-return haptics test in a reviewed native
// candidate. Never launches normal YMCC startup or an installed application.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const args=process.argv.slice(2),arg=(name)=>args.includes(name)?args[args.indexOf(name)+1]:'';
assert(arg('--candidate-native')&&arg('--evidence-dir'),'Explicit candidate and fresh evidence directory required');
const candidate=path.resolve(arg('--candidate-native')),out=path.resolve(arg('--evidence-dir')),expected=args.includes('--expect-blocking-baseline');fs.mkdirSync(out,{recursive:true});
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),exe=path.join(candidate,'YeManCC.exe');
const build=JSON.parse(fs.readFileSync(path.join(candidate,'verification.json'),'utf8'));assert.equal(build.compilerExitCode,0,'Candidate must be built, not an installed application');assert.equal(hash(exe),build.exeSha256,'Candidate EXE identity changed');
assert.equal(hash(path.join(candidate,'source/native/main.cpp')),build.sourceHashes['native/main.cpp'],'Frozen main source identity changed');
assert(fs.readFileSync(exe).includes(Buffer.from('actual-monitor-haptic-ingress-never-waits-for-physical-sampling')),'Candidate lacks the actual monitor haptic lock regression');
const reportPath=path.join(candidate,'screen-touchpads-haptics-output-selftest.json');assert(!fs.existsSync(reportPath),'Use a fresh native candidate; do not overwrite earlier receipts');
let knownFailures=[],knownBaselineProof=null;
if(arg('--known-suite-baseline-native')){const base=path.resolve(arg('--known-suite-baseline-native')),identity=JSON.parse(fs.readFileSync(path.join(base,'verification.json'),'utf8')),nativeResult=JSON.parse(fs.readFileSync(path.join(base,'screen-touchpads-haptics-output-selftest.json'),'utf8'));
 assert.equal(hash(path.join(base,'YeManCC.exe')),identity.exeSha256,'Known baseline EXE changed');assert.equal(nativeResult.hardwareWrites,0);assert.equal(nativeResult.virtualTargetStarted,false);assert.equal(nativeResult.configurationWrites,0);
 assert(!nativeResult.cases.some(c=>c.name==='actual-monitor-haptic-ingress-never-waits-for-physical-sampling'),'Known baseline must predate the new lock test');
 knownFailures=nativeResult.cases.filter(c=>!c.ok).map(c=>c.name);knownBaselineProof={exeSha256:identity.exeSha256,resultSha256:hash(path.join(base,'screen-touchpads-haptics-output-selftest.json')),failures:knownFailures};}
const executableBefore=hash(exe),environment={...process.env,__COMPAT_LAYER:'RunAsInvoker'};
const run=spawnSync(exe,['--screen-touchpads-haptics-output-selftest'],{encoding:'utf8',windowsHide:true,timeout:20000,env:environment});fs.writeFileSync(path.join(out,'process.log'),(run.stdout??'')+(run.stderr??''));
const result=fs.existsSync(reportPath)?JSON.parse(fs.readFileSync(reportPath,'utf8')):null;if(result)fs.copyFileSync(reportPath,path.join(out,'native-result.json'));
const failures=result?.cases.filter(c=>!c.ok).map(c=>c.name)??[];const blockingCase='actual-monitor-haptic-ingress-never-waits-for-physical-sampling';
const identityIntact=hash(exe)===executableBefore,contextIntact=result?.hardwareWrites===0&&result?.virtualTargetStarted===false&&result?.configurationWrites===0;
const unexpectedFailures=failures.filter(name=>!knownFailures.includes(name)&&!(expected&&name===blockingCase));
const target=result?.cases.find(c=>c.name===blockingCase),fixture=result?.cases.find(c=>c.name==='monitor-feedback-lock-fixture-has-physical-source'),release=result?.cases.find(c=>c.name==='busy-monitor-haptic-release-keeps-no-contact-or-worker');
const ok=identityIntact&&contextIntact&&!run.error&&unexpectedFailures.length===0&&fixture?.ok===true&&release?.ok===true&&target?.ok===!expected&&run.status===(expected||failures.length?3:0);
const receipt={ok,fullHapticsSuitePassed:result?.ok===true,knownBaselineProof,unexpectedFailures,verdict:!ok?'FAIL':expected?'EXPECTED_MONITOR_BLOCKING_REPRODUCED':failures.length?'PASS_MONITOR_INGRESS_WITH_EXISTING_SUITE_FAILURES':'PASS',expectedBlockingBaseline:expected,exitCode:run.status,error:run.error?.message??null,exeSha256:executableBefore,sourceMainSha256:build.sourceHashes['native/main.cpp'],consumerSha256:hash(import.meta.filename),cases:result?.cases.length,failures,executedExistingEarlyReturnSelftest:true,normalApplicationStarted:false,installedProductOperated:false,systemInputInjected:false,physicalHardwareOutputClaimed:false,hardwareWrites:result?.hardwareWrites,virtualTargetStarted:result?.virtualTargetStarted,configurationWrites:result?.configurationWrites,scope:'Actual production monitorHaptic -> existing haptic sink -> nativePhysicalFeedbackReady. A fixture holds the existing sampling mutex; callback must return while that mutex is still owned. No CPU benchmark.'};
fs.writeFileSync(path.join(out,'verification.json'),JSON.stringify(receipt,null,2));console.log(JSON.stringify(receipt,null,2));if(!ok)process.exitCode=2;
