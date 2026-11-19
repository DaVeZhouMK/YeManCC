// Reproducible isolated current-native configuration baseline consumer.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { performance } from 'node:perf_hooks';
import {pathToFileURL} from 'node:url';
const root=process.cwd(), evidence=process.env.YMCC_BASELINE_EVIDENCE;
const virtualStartWriteFailure=process.argv.includes('--virtual-start-write-failure');
if(virtualStartWriteFailure&&process.argv.length!==3)throw Error('virtual-start-write-failure requires its own exclusive scope');
const inputRuntimePower=process.argv.includes('--input-runtime-power');
const upgradeBoundaries=process.argv.includes('--upgrade-boundaries');
const {calibrationToolchainPreamble}=await import(pathToFileURL(path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')).href);
const serialConfigOnly=process.argv.includes('--serial-config'),sourceMainCandidate=process.argv.includes('--source-main');
const cleanupOnly=process.argv.includes('--cleanup-history'),sectionCacheOnly=process.argv.includes('--section-cache'),caseAt=process.argv.indexOf('--case');
if(serialConfigOnly&&(cleanupOnly||sectionCacheOnly))throw Error('serial-config requires its own exclusive scope');
if(cleanupOnly&&sectionCacheOnly)throw Error('section-cache and cleanup-history scopes are exclusive');
const candidateAt=process.argv.indexOf('--section-cache-candidate');
if(candidateAt>=0&&(!sectionCacheOnly||!process.argv[candidateAt+1]||process.argv[candidateAt+1].startsWith('--')))throw Error('section-cache-candidate requires explicit --section-cache and a file');
if(upgradeBoundaries&&(cleanupOnly||sectionCacheOnly||serialConfigOnly||sourceMainCandidate))throw Error('upgrade-boundaries requires its own exclusive scope');
if(inputRuntimePower&&[cleanupOnly,sectionCacheOnly,serialConfigOnly,upgradeBoundaries,sourceMainCandidate].some(Boolean))throw Error('input-runtime-power requires exclusive scope');
if(caseAt>=0&&(!(cleanupOnly||upgradeBoundaries||inputRuntimePower)||!process.argv[caseAt+1]))throw Error('Exact case requires explicit original scope');
const selectedCase=caseAt>=0?process.argv[caseAt+1]:null;
if(!evidence)throw Error('Set YMCC_BASELINE_EVIDENCE to an owned isolated evidence directory');
const out=path.resolve(evidence), sourceRoot=path.resolve(root);
if(out===sourceRoot||out.toLowerCase().startsWith(path.join(sourceRoot,'PowerControl').toLowerCase()+path.sep))throw Error('Evidence directory cannot overwrite configuration sources');
const setup=process.env.YMCC_BASELINE_ENV_CMD||'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
if(!fs.existsSync(setup)||/["%\r\n]/.test(setup))throw Error('Invalid explicit compiler environment');
for(const value of [root,out])if(/["%\r\n]/.test(value))throw Error('Unsupported shell path');
// A referenced raw result is immutable. New attempts require a fresh output.
if(virtualStartWriteFailure){fs.mkdirSync(out);}
else if((sourceMainCandidate||serialConfigOnly)&&fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Explicit candidate evidence directory must be empty');
fs.mkdirSync(out,{recursive:true});
// Keep future compiler/native temporary files under this explicit Evidence.
const runtimeTemp=path.join(out,'runtime-temp');fs.mkdirSync(runtimeTemp,{recursive:true});
const processEnvironment={...process.env,TEMP:runtimeTemp,TMP:runtimeTemp};
const runRecorded=(name,program,args,options)=>{
  const startedAtUtc=new Date().toISOString(),started=performance.now();
  const value=spawnSync(program,args,{...options,env:processEnvironment});
  fs.writeFileSync(path.join(out,name+'-process.json'),JSON.stringify({program,args,cwd:options.cwd,startedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-started,pid:value.pid??null,exitCode:value.status,signal:value.signal??null,error:value.error?.message??null,budgetMs:options.timeout,temporaryEnvironment:{TEMP:runtimeTemp,TMP:runtimeTemp}},null,2)+'\n');
  return value;
};
await import('./settings_baseline_native_prepare.mjs');
const inputs=JSON.parse(fs.readFileSync(path.join(out,'native-source-inputs.json'),'utf8'));
const before=fs.readFileSync(path.join(out,'settings-baseline-native.cpp'));
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
const environmentInputs=[setup,path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')].map(file=>({path:path.resolve(file),sha256:sha(fs.readFileSync(file))}));
const verifyInputs=()=>{
 for(const input of [...inputs,...environmentInputs])if(sha(fs.readFileSync(path.isAbsolute(input.path)?input.path:path.join(root,input.path)))!==input.sha256)throw Error('NATIVE_BASELINE_INPUT_CHANGED '+input.path);
 if(sha(fs.readFileSync(path.join(out,'settings-baseline-native.cpp')))!==sha(before))throw Error('NATIVE_BASELINE_GENERATED_INPUT_CHANGED');
};
// Explicit private candidates must retain the bytes selected by prepare, both
// before compilation and after execution, including an expected semantic FAIL.
if(sectionCacheOnly||sourceMainCandidate||serialConfigOnly||virtualStartWriteFailure)verifyInputs();
const commandPreamble=process.env.YMCC_BASELINE_ENV_CMD?`@echo off\r\ncall "${setup}" >nul\r\nif errorlevel 1 exit /b 1\r\n`:calibrationToolchainPreamble(setup);
const command=`${commandPreamble}cl /nologo /EHsc /std:c++20 /utf-8 /O1 /sourceDependencies "${path.join(out,'source-dependencies.json')}" /I"${path.join(root,'native')}" /I"${path.join(root,'deps/json')}" "${path.join(out,'settings-baseline-native.cpp')}" /Fe:"${path.join(out,'settings-baseline-native.exe')}" /Fo:"${path.join(out,'settings-baseline-native.obj')}"\r\nexit /b %errorlevel%\r\n`;
const driver=path.join(out,'compile.cmd');fs.writeFileSync(driver,command);
const built=runRecorded('compile',process.env.ComSpec||'cmd.exe',['/d','/c',driver],{cwd:out,encoding:'utf8',timeout:120000,windowsHide:true});
fs.writeFileSync(path.join(out,'compile.log'),(built.stdout||'')+(built.stderr||''));
if(built.status!==0)throw Error('NATIVE_BASELINE_COMPILE_FAILED '+(built.error?.message||built.status));
const nativeArgs=[path.join(root,'PowerControl/settings-defaults-v1.json')];
if(virtualStartWriteFailure)nativeArgs.push('--virtual-start-write-failure');
if(inputRuntimePower)nativeArgs.push('--input-runtime-power',...(selectedCase?[selectedCase]:[]));
if(upgradeBoundaries)nativeArgs.push('--upgrade-boundaries',path.join(out,'upgrade-boundaries-owned'),...(selectedCase?[selectedCase]:[]));
if(cleanupOnly)nativeArgs.push('--cleanup-history',path.join(out,'cleanup-history-owned'),...(selectedCase?[selectedCase]:[]));
if(sectionCacheOnly)nativeArgs.push('--section-cache',path.join(out,'section-cache-owned'));
if(serialConfigOnly)nativeArgs.push('--serial-config',path.join(out,'serial-config-owned'));
fs.writeFileSync(path.join(out,'native-execution-identity.json'),JSON.stringify({generatedConsumerSha256:crypto.createHash('sha256').update(before).digest('hex'),executableSha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(out,'settings-baseline-native.exe'))).digest('hex'),args:nativeArgs,scope:virtualStartWriteFailure?'virtual-start-write-failure':inputRuntimePower?'input-runtime-power':upgradeBoundaries?'upgrade-boundaries-real-win32-io':serialConfigOnly?'serial-config-storage':sectionCacheOnly?'section-cache-real-win32-io':cleanupOnly?'cleanup-history-only':'original-default'},null,2)+'\n');
const ran=runRecorded('native',path.join(out,'settings-baseline-native.exe'),nativeArgs,{cwd:out,encoding:'utf8',timeout:60000,windowsHide:true});
fs.writeFileSync(path.join(out,'result.log'),(ran.stdout||'')+(ran.stderr||''));
if(sectionCacheOnly||sourceMainCandidate||serialConfigOnly||virtualStartWriteFailure)verifyInputs();
if(ran.status!==0)throw Error('NATIVE_BASELINE_RUN_FAILED '+(ran.error?.message||ran.status));
if(!sectionCacheOnly&&!sourceMainCandidate&&!serialConfigOnly&&!virtualStartWriteFailure)verifyInputs();
const result=JSON.parse(ran.stdout.trim().split(/\r?\n/).at(-1));
if(virtualStartWriteFailure){
 const rows=result.caseRecords;
 if(result.status!=='PASS'||result.scope!=='virtual-start-write-failure'||result.cases!==1||!Array.isArray(rows)||rows.length!==1||rows[0].name!=='admitted-write-failure-and-explicit-retry'||rows[0].pass!==true||!Number.isInteger(rows[0].assertions)||rows[0].assertions<1||result.installedSettingsWritten!==false||result.hardwareValidated!==false)throw Error('NATIVE_VIRTUAL_START_WRITE_FAILURE_SCOPE_MISMATCH');
}
if(upgradeBoundaries){
 const fixture=fs.readFileSync(path.join(root,'tools/fixtures/settings_baseline_native.cpp'),'utf8');
 const declaration=fixture.match(/const std::vector<std::string> names=\{([^\n]+)\};/);
 if(!declaration)throw Error('Upgrade boundary fixture selection missing');
 const names=selectedCase?[selectedCase]:[...declaration[1].matchAll(/"([^"\\]+)"/g)].map(match=>match[1]);
 if(result.status!=='PASS'||result.scope!=='upgrade-boundaries-real-win32-io'||!names.length||result.cases!==names.length||!Array.isArray(result.caseRecords)||result.caseRecords.length!==names.length||result.caseRecords.some((row,index)=>row.name!==names[index]||row.pass!==true||row.assertions<1)||result.ownedIoRemoved!==true||result.installedSettingsWritten!==false)throw Error('NATIVE_UPGRADE_BOUNDARIES_SCOPE_MISMATCH');
}
if(sectionCacheOnly&&(result.status!=='PASS'||result.memorySectionStubUsed!==false||result.scope!=='section-cache-real-win32-io'||result.consumerNamespace!=='settingsIoFixture'||result.actualProductionSection!==true||result.cases!==8||result.caseRecords?.length!==8||result.caseRecords.some(row=>row.pass!==true||row.assertions<1)||result.ownedIoRemoved!==true))throw Error('NATIVE_SECTION_CACHE_SCOPE_MISMATCH');
if(serialConfigOnly){
 const selected=JSON.parse(fs.readFileSync(path.join(out,'source-segments.json'),'utf8')).serialConfigSelection;
 const actual=result.caseRecords;
 if(result.status!=='PASS'||result.scope!=='serial-config-storage'||!selected||!Array.isArray(actual)||result.cases!==selected.names.length||actual.length!==selected.names.length||new Set(actual.map(row=>row.name)).size!==actual.length||actual.some((row,index)=>row.name!==selected.names[index]||row.pass!==true||!Number.isInteger(row.assertions)||row.assertions<1)||result.ownedIoRemoved!==true||result.installedSettingsWritten!==false||result.hardwareValidated!==false)throw Error('NATIVE_SERIAL_CONFIG_SCOPE_MISMATCH');
}
result.inputIdentity='UNCHANGED';result.generatedConsumerSha256=sha(before);result.compilerEnvironment={path:setup,sha256:sha(fs.readFileSync(setup)),selection:process.env.YMCC_BASELINE_ENV_CMD?'explicit-override':'complete-installed-sdk',preambleSha256:sha(Buffer.from(commandPreamble)),inputs:environmentInputs};
result.sourceSegmentsExtracted=JSON.parse(fs.readFileSync(path.join(out,'source-segments.json'),'utf8'));result.admissionAndFilesystem=virtualStartWriteFailure?'production guard, settings handler and writer; owned memory storage and explicit external ports':'production bodies; inert memory transactions and host callbacks plus isolated owned Win32 files and locks';
if(result.sourceSegmentsExtracted.cleanupUpgradePort)result.upgradeRecoveryBoundary={
 route:'verified-marked-running-build',policyNamespace:'ymcc::deckyupgrade::running',
 protectedInstallationIO:'DECLARED_MEMORY_PORT',imageDigest:'DECLARED_READ_ONLY_PORT',privilegedAclPositive:'NOT_EXERCISED',
 versionMacro:'APP_VER_STR (test version string only)',
 originalSettingsPair:'EXTRACTED_WIN32_IO_GUARD_FLUSH_REPLACE_READBACK',
 extendedOriginalCaseExecuted:Array.isArray(result.caseRecords)&&result.caseRecords.some(row=>row.name==='cleanup pair-commit fallback and duplicate'),
 purePolicyInput:result.sourceSegmentsExtracted.cleanupUpgradePort.purePolicy,
 purePolicyTransitiveInputs:result.sourceSegmentsExtracted.cleanupUpgradePort.purePolicyTransitive
};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
