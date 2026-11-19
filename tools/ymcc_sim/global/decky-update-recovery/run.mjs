import {getBaselineSourceFiles} from './baseline-source-inputs.mjs';
import {prepareNativeSource,uniqueAnchor,cppBlock} from './native-source-admission.mjs';
import {executeBaselineResources} from './resource-execution.mjs';
import {observeOnlyHarness,validateObserveOnlyFailure} from './observe-only-negative.mjs';
import {packageMaintenanceEnvironment,resourceFixtureEnvironment} from '../decky-persistent-lifecycle/baseline-environment.mjs';
import {trainerRecoveryTests} from '../../../decky_trainer_recovery_selftest.mjs';
import {wakeChainTests} from '../../../decky_wake_chain_selftest.mjs';
import {frontendBootTests} from '../../../decky_sidebar_frontend_boot_selftest.mjs';
import {trainerForegroundTests} from '../../../decky_trainer_foreground_selftest.mjs';
import {manualRepairTests} from '../../../decky_manual_repair_selftest.mjs';
import {retainedSessionRepairTests} from '../../../decky_retained_session_repair_selftest.mjs';
import {sliderStabilityTests} from '../../../decky_slider_stability_selftest.mjs';
// Actual native callbacks/Runtime and exact Steam read; no installed products are contacted.
import {nativeLaunchTests} from '../../../steam_native_launch_regression.mjs';
import {prepareNativeLaunchNoIoFixture,NATIVE_LAUNCH_NOIO_SOURCE_INPUTS} from './native-launch-noio/index.mjs';
import fs from 'node:fs';
import os from 'node:os';
import {startNativeLaunchWorker,NATIVE_LAUNCH_WORKER_SOURCE_INPUTS} from './native-launch-worker.mjs';
import {startCssAppearanceWorker,CSS_APPEARANCE_WORKER_SOURCE_INPUTS} from './css-appearance-worker.mjs';
import {getCssAppearanceInputs} from './css-appearance-sequence.mjs';
import {forwardCssRequiredReceipts} from './css-required-forwarding.mjs';
const baselineStartedAt=Date.now();
import {verifyPackageFixtureTools} from './package-fixture-tools.mjs';import path from 'node:path';import crypto from 'node:crypto';import {execFileSync,spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';import {readTests} from './read-selftest.mjs';import {clientTests} from './client-selftest.mjs';import {noRepeatContextTests} from '../../../decky_no_repeat_context_selftest.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(here,'../../../..'),args=process.argv.slice(2),at=args.indexOf('--evidence-dir'),out=path.resolve(at<0?process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-update-recovery':args[at+1]);fs.mkdirSync(out,{recursive:true});
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

if(args.includes('--committed-delivery-only')||args.includes('--shared-power-revocation-only')||args.includes('--shared-power-projection-only')||args.includes('--recovery-shared-power-only')){
 const mode=args.includes('--recovery-shared-power-only')?'--recovery-shared-power-only':args.includes('--shared-power-projection-only')?'--shared-power-projection-only':args.includes('--shared-power-revocation-only')?'--shared-power-revocation-only':'--committed-delivery-only';
 if(at<0||args.filter(arg=>arg===mode).length!==1||args.filter(arg=>arg==='--case').length>1||args.filter(arg=>arg==='--evidence-dir').length!==1)throw Error('INVALID_COMMITTED_SELECTION_SHAPE');
 const selected=args.indexOf('--case');
 const permitted=new Set(['--evidence-dir',mode,'--case']);
 for(let i=0;i<args.length;i++){if(!permitted.has(args[i]))throw Error('UNKNOWN_COMMITTED_SELECTION_ARGUMENT');if(args[i]!==mode){if(!args[i+1]||args[i+1].startsWith('--'))throw Error('MISSING_COMMITTED_SELECTION_VALUE');i++;}}
 if(mode==='--recovery-shared-power-only'){const {runRecoverySharedPower}=await import('../../../fixtures/recovery_shared_power.mjs');runRecoverySharedPower({repo:root,out:path.join(out,'committed-delivery'),selectedCase:selected<0?null:args[selected+1]});}else{
 const {runCommittedDelivery}=await import('./committed-delivery.mjs');
 runCommittedDelivery({repo:root,out:path.join(out,'committed-delivery'),selectedCase:selected<0?null:args[selected+1],sharedPowerRevocation:mode==='--shared-power-revocation-only',sharedPowerProjection:mode==='--shared-power-projection-only'});
 }
 process.exit(process.exitCode??0);
}
const files=getBaselineSourceFiles(root);
const inputs=files.map(file=>({path:path.relative(root,file).replaceAll('\\','/'),sha256:hash(file)}));const report={passed:false,fieldRegression:'decky-update-disconnect-20261020',additionalFieldRegressions:['decky-native-prewarm-startup-orders-20261022','decky-null-window-20261021','decky-tdp-qam-reentry-20261021','decky-multi-qam-visibility-20261021','decky-reentrant-transport-fence-20261021'],realSteamActions:0,userSettingsWrites:0,hardwareWrites:0,sourceInputs:inputs,sourceHashes:inputs};
let nativeLaunchWorker=null,cssAppearanceWorker=null;
try{
 const admissionStartedAt=performance.now();
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 const nativeSource=prepareNativeSource({main,template:fs.readFileSync(path.join(here,'native-fixture.cpp'),'utf8'),repairHeader:fs.readFileSync(path.join(root,'native/decky_sidebar_manual_repair.h'),'utf8'),automaticHeader:fs.readFileSync(path.join(root,'native/decky_sidebar_automatic_repair.h'),'utf8'),mainSha256:hash(path.join(root,'native/main.cpp'))});
 report.nativeCallbackExtraction=nativeSource.extraction;
 report.nativeSourceAdmission={beforeHeavyProducers:true,elapsedMs:performance.now()-admissionStartedAt,preparedFiles:nativeSource.outputs.size,compilations:0,businessRuns:0};
 nativeLaunchWorker=startNativeLaunchWorker({repo:root,callerEvidence:out,deadlineMs:baselineStartedAt+240000});
 cssAppearanceWorker=startCssAppearanceWorker({repo:root,callerEvidence:out,deadlineMs:baselineStartedAt+240000});
 report.communityRegressionSources=JSON.parse(fs.readFileSync(path.join(here,'field-record-community-maintenance-20261023.json'),'utf8'));
 const packageOut=path.join(out,'package-maintenance');
 // Original package stage stays30000ms. Runtime/preparation <=25000ms;
 // at most5000ms remains for this test's own child retirement, never added.
 const packageFile=path.join(root,'tools/decky_package_maintenance_selftest.ps1');
 const packageShell=path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe');
 const packageArgs=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',packageFile,'-EvidenceDir',packageOut,'-TotalBudgetMs','30000','-RunBudgetMs','25000','-CleanupReserveMs','5000'];
 const fixtureCompilerAuthority=verifyPackageFixtureTools();
 const packageSourceBefore={path:packageFile,sha256:hash(packageFile)},packageToolBefore={path:packageShell,sha256:hash(packageShell)};
 const packageStartedAt=new Date().toISOString(),packageStart=performance.now();
 const packageRun=spawnSync(packageShell,packageArgs,{windowsHide:true,timeout:30000,maxBuffer:16*1024*1024});
 const packageHashAfter=file=>{try{return{sha256:hash(file),error:null};}catch(error){return{sha256:null,error:{name:error.name,code:error.code,message:error.message}};}};
 const sourceAfter=packageHashAfter(packageFile),toolAfter=packageHashAfter(packageShell);
 verifyPackageFixtureTools();
 const packageResult={startedAt:packageStartedAt,finishedAt:new Date().toISOString(),elapsedMs:performance.now()-packageStart,cwd:process.cwd(),environment:packageMaintenanceEnvironment(),
   executable:packageToolBefore,source:packageSourceBefore,sourceSha256After:sourceAfter.sha256,toolSha256After:toolAfter.sha256,sourceReadErrorAfter:sourceAfter.error,toolReadErrorAfter:toolAfter.error,args:packageArgs,
   pid:packageRun.pid??null,status:packageRun.status,signal:packageRun.signal,
   error:packageRun.error?{name:packageRun.error.name,code:packageRun.error.code,errno:packageRun.error.errno,message:packageRun.error.message,stack:packageRun.error.stack}:null,
   fixtureCompilerAuthority:{manifestSha256:fixtureCompilerAuthority.manifestSha256,compiler:fixtureCompilerAuthority.compiler,inputs:fixtureCompilerAuthority.files.length,files:fixtureCompilerAuthority.files,osLoadedModulesComplete:false},
   stageBudgetMs:30000,innerTotalBudgetMs:30000,runBudgetMs:25000,cleanupReserveMs:5000,registeredAdapterBudgetMs:240000,
   timedOut:packageRun.error?.code==='ETIMEDOUT',outputLimitBytes:16*1024*1024,stdoutBytes:packageRun.stdout?.length??0,stderrBytes:packageRun.stderr?.length??0,
   descendantExitProven:false,scope:'Original30s gate; any incomplete mutation/retirement stays non-PASS.'};
 fs.writeFileSync(path.join(out,'package-maintenance-process.json'),JSON.stringify(packageResult,null,2)+'\n');
 fs.writeFileSync(path.join(out,'package-maintenance.stdout.raw'),packageRun.stdout||Buffer.alloc(0));
 fs.writeFileSync(path.join(out,'package-maintenance.stderr.raw'),packageRun.stderr||Buffer.alloc(0));
 fs.writeFileSync(path.join(out,'package-maintenance.log'),Buffer.concat([packageRun.stdout||Buffer.alloc(0),packageRun.stderr||Buffer.alloc(0)]));
 if(packageResult.timedOut||packageResult.elapsedMs>30000)throw Error('Decky package-maintenance TIMEOUT at original30000ms; INCOMPLETE');
 if(packageRun.error)throw Error('Decky package-maintenance process error: '+packageRun.error.code);
 if(packageRun.signal)throw Error('Decky package-maintenance process terminated: '+packageRun.signal);
 if(packageRun.status!==0)throw Error('Decky package-maintenance consumer nonzero exit: '+packageRun.status);
 if(packageResult.sourceSha256After!==packageSourceBefore.sha256||packageResult.toolSha256After!==packageToolBefore.sha256)throw Error('Decky package-maintenance source/tool changed during execution');
 report.packageMaintenance=JSON.parse(fs.readFileSync(path.join(packageOut,'result.json'),'utf8').replace(/^\uFEFF/,''));
 if(report.packageMaintenance.pass!==true||report.packageMaintenance.status!=='PASS'||report.packageMaintenance.complete!==true)throw Error('Decky package-maintenance TIMEOUT/INCOMPLETE or original assertion failure');
 const packagePin=file=>({path:path.resolve(file),sha256:hash(file),bytes:fs.statSync(file).size});
 report.packageMaintenanceExecution={schemaVersion:1,process:packagePin(path.join(out,'package-maintenance-process.json')),result:packagePin(path.join(packageOut,'result.json')),stdout:packagePin(path.join(out,'package-maintenance.stdout.raw')),stderr:packagePin(path.join(out,'package-maintenance.stderr.raw')),negativeResults:report.packageMaintenance.mutationRetirement.map(row=>({name:row.name,result:packagePin(path.join(row.independentRoot,'evidence/result.json'))}))};
 report.additionalFieldRegressions.push('decky-automatic-managed-package-20261023');
 // Each original complete resource producer runs once; all receipts remain under this child.
 Object.assign(report,await executeBaselineResources({repo:root,out,sourceInputs:inputs,deadlineEpochMs:baselineStartedAt+240000}));
 report.additionalFieldRegressions.push('decky-local-resource-repair-and-owned-worker-20261023');
 report.retainedSessionRepair=await retainedSessionRepairTests(root,path.join(out,'retained-session-repair'));
 report.additionalFieldRegressions.push('decky-retained-session-repair-rogx-20261023');
 report.trainerForeground=await trainerForegroundTests(path.join(out,'trainer-foreground'));
 report.trainerRecovery=await trainerRecoveryTests(path.join(out,'trainer-recovery'));
 report.manualRepairCases=await manualRepairTests(root,out,{upgradeActionsFile:path.join(out,'retained-session-repair/native.json')});const boot=await frontendBootTests(root,out);report.frontendBootCases=boot.cases;report.frontendBootNegativeDetected=boot.negativeDetected;
 for(const [name,text] of nativeSource.outputs)fs.writeFileSync(path.join(out,name),text);
 const vc=process.env.VCToolsInstallDir||'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.44.35207\\',sdk=process.env.WindowsSdkDir||'C:\\Program Files (x86)\\Windows Kits\\10\\',version=process.env.WindowsSDKVersion||'10.0.26100.0\\';
 const environment=['@echo off','set "SystemRoot=C:\\Windows"','set "WINDIR=C:\\Windows"',`set "PATH=${path.join(vc,'bin/Hostx64/x64')};C:\\Windows\\System32;C:\\Windows"`,`set "INCLUDE=${path.join(vc,'include')};${['ucrt','shared','um','winrt','cppwinrt'].map(n=>path.join(sdk,'Include',version,n)).join(';')}"`,`set "LIB=${path.join(vc,'lib/x64')};${path.join(sdk,'Lib',version,'ucrt/x64')};${path.join(sdk,'Lib',version,'um/x64')}"`];
 const modes=['fixed','prewarm-negative','wake-preparation-negative','legacy','retained-negative','repair-uncertain-negative','repair-retained-negative','repair-auto-repeat-negative','repair-auto-ui-negative','repair-stale-mode-negative','wake-power-guard-negative','wake-binding-negative','wake-ready-event-negative'];
 // Preserve every original variant and mutation; precompile only their exact
 // whole-line common preamble, including the original observation macro order.
 const originals=modes.map(mode=>({mode,file:path.join(out,mode+'.cpp'),text:fs.readFileSync(path.join(out,mode+'.cpp'),'utf8')}));
 let commonLength=originals[0].text.length;
 for(const original of originals.slice(1)){let n=0;while(n<commonLength&&n<original.text.length&&original.text[n]===originals[0].text[n])n++;commonLength=n;}
 commonLength=originals[0].text.lastIndexOf('\n',commonLength-1)+1;
 const commonPrefix=originals[0].text.slice(0,commonLength);
 if(!commonPrefix.includes('#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1')||!commonPrefix.includes('#define deckylaunch fixtureStageLaunch')||!commonPrefix.includes('#include "decky_sidebar_runtime.h"')||!commonPrefix.includes('#undef deckylaunch')||commonPrefix.includes('#include "decky_sidebar_manual_repair.h"')||commonPrefix.includes('#include "decky_sidebar_automatic_repair.h"'))throw Error('Review native common preamble mutation/macro boundary');
 const prefixFile=path.join(out,'native-shared-prefix.inc');fs.writeFileSync(prefixFile,commonPrefix);
 const nativeNegativeAssertions={"wake-power-guard-negative":"uncommitted power phase cannot emit Ready or renew Decky binding","prewarm-negative":"native Runtime prewarms resources without requiring a running Steam process","wake-preparation-negative":"actual committed Ready retries native prewarm while Steam remains absent","wake-binding-negative":"actual committed wake reaches production Runtime binding refresh","wake-ready-event-negative":"actual committed wake emits current generation Ready event","repair-auto-ui-negative":"automatic retained repair cannot rebuild Steam UI or consume manual launch consent","repair-stale-mode-negative":"explicit owned repair must read current frontend instead of stale pre-load cache","repair-auto-repeat-negative":"one automatic attempt per document survives duplicate events","repair-uncertain-negative":"exception after dispatch preserves uncertain mutation","repair-retained-negative":"retained bridge with absent owned backend permits explicit repair","retained-negative":"retained frontend must restore broker without official Loader","legacy":"field regression: global recovery cannot require a special origin"};
 // A negative exits naturally at its original assertion. Its dead suffix
 // never ran in the original case; retain every preceding byte and the catch.
 function selectNegativeDriver(driver,structural,mode){
  if(mode==='fixed')return {source:driver,selection:{kind:'full-normal',originalBodyBytePreserved:true}};
  const expected=nativeNegativeAssertions[mode];if(!expected)throw Error('Unmapped original native negative');
  const target=uniqueAnchor(driver,expected,'negative assertion'),after=driver.slice(target+expected.length),ending=/^"\s*\)\s*;/.exec(after);
  if(!ending)throw Error('Review original negative assertion boundary: '+mode);
  const end=target+expected.length+ending[0].length,tryAt=uniqueAnchor(structural,'\n try{','scenario try'),block=cppBlock(structural,tryAt+5,'scenario try');
  if(target<=block.open||end>=block.end)throw Error('Negative assertion left original scenario try');
  const opens=[];let state='code',escaped=false;
  for(let i=block.open+1;i<end;i++){
   const c=structural[i],next=structural[i+1];
   if(state==='line'){if(c==='\n')state='code';continue;}
   if(state==='block'){if(c==='*'&&next==='/'){state='code';i++;}continue;}
   if(state==='string'||state==='character'){if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}if(c===(state==='string'?'"':"'"))state='code';continue;}
   if(c==='/'&&next==='/'){state='line';i++;continue;}if(c==='/'&&next==='*'){state='block';i++;continue;}
   if(c==='"'){state='string';continue;}if(c==="'"){state='character';continue;}
   if(c==='{')opens.push(i);else if(c==='}'){if(!opens.length)throw Error('Negative selection escaped try');opens.pop();}
  }
  if(state!=='code')throw Error('Incomplete original negative statement');
  for(const open of opens){if(/\b(try|do)\s*$/.test(structural.slice(Math.max(block.open,open-24),open)))throw Error('Review required suffix of nested try/do');}
  const prefix=driver.slice(0,end),suffix=driver.slice(block.end-1),cleanup='\n g_steamModeObserver.shutdown();g_deckySidebarRuntime.shutdown();return 0;\n';
  const source=prefix+cleanup+'}\n'.repeat(opens.length)+suffix;
  if(source.slice(0,end)!==prefix)throw Error('Original negative prefix changed');
  return {source,selection:{kind:'original-prefix-through-expected-failure',expectedAssertion:expected,prefixBytes:Buffer.byteLength(prefix),prefixSha256:crypto.createHash('sha256').update(prefix).digest('hex'),originalBodyBytePreserved:false,originalExecutedPrefixBytePreserved:true,catchBytePreserved:true,omittedOriginalBytes:Buffer.byteLength(driver.slice(end,block.end-1)),successPath:'original target unexpectedly passes -> explicit shutdown -> natural zero; existing negative validator rejects zero',closedFixtureScopes:opens.length}};
 }
 const derivedSources=originals.map(original=>{
  const tail=original.text.slice(commonLength);if(!Buffer.from(commonPrefix+tail).equals(fs.readFileSync(original.file)))throw Error('Native preamble transform changed original bytes: '+original.mode);
  const file=path.join(out,original.mode+'.pch.cpp'),line=commonPrefix.split('\n').length;
  // Optimize production headers and extracted callbacks with the unchanged /O2.
  // Only the generated scenario/assertion driver avoids repeated optimization.
  const entry='int wmain(int argc,wchar_t** argv){',entryAt=tail.indexOf(entry);
  if(entryAt<0||tail.indexOf(entry,entryAt+entry.length)>=0)throw Error('Review unique native fixture driver');
  const structuralTail=tail.replace(/R"([^()\s\\]{0,16})\([\s\S]*?\)\1"/g,text=>text.replace(/[^\r\n]/g,' '));
  const driverEnd=cppBlock(structuralTail,tail.indexOf('{',entryAt),'fixture driver').end;
  if(tail.slice(driverEnd).trim())throw Error('Review declarations after native fixture driver');
  const driver=tail.slice(entryAt,driverEnd),entryLine=(commonPrefix+tail.slice(0,entryAt)).split('\n').length;
  const driverSha256=crypto.createHash('sha256').update(driver).digest('hex');
  const selectedDriver=selectNegativeDriver(driver,structuralTail.slice(entryAt,driverEnd),original.mode);
  const fileLabel=JSON.stringify(original.file.replaceAll('\\','/'));
  const driverSource=tail.slice(0,entryAt)+'#pragma optimize("", off)\n#line '+entryLine+' '+fileLabel+'\n'+selectedDriver.source+'\n#pragma optimize("", on)\n'+tail.slice(driverEnd);
  fs.writeFileSync(file,'#include "native-pch.h"\n#line '+line+' '+fileLabel+'\n'+driverSource);
  return {mode:original.mode,original:{path:original.file,sha256:hash(original.file)},derived:{path:file,sha256:hash(file)},prefixBytes:Buffer.byteLength(commonPrefix),tailBytes:Buffer.byteLength(tail),originalReconstructionByteEqual:true,fixtureDriver:{entry,originalLine:entryLine,sha256:driverSha256,bodyBytePreserved:original.mode==='fixed',selection:selectedDriver.selection,compiledDriverSha256:crypto.createHash('sha256').update(selectedDriver.source).digest('hex'),optimization:'disabled only for generated fixture driver; production definitions retain /O2'}};
 });
 const capacity={availableProcessors:os.availableParallelism(),totalMemoryBytes:os.totalmem(),freeMemoryBytes:os.freemem(),perWorkerMemoryReserveBytes:1024*1024*1024,maxWorkers:4};
 const compileWorkers=Math.max(1,Math.min(capacity.maxWorkers,capacity.availableProcessors,Math.floor(capacity.freeMemoryBytes/capacity.perWorkerMemoryReserveBytes)));
 const transformFile=path.join(out,'native-compile-sources.json');fs.writeFileSync(transformFile,JSON.stringify({schemaVersion:1,commonPrefix:{path:prefixFile,sha256:hash(prefixFile)},originalVariantsPreserved:true,independentVariantObjects:true,derivedSources},null,2)+'\n');
 const objectRoot=out.replaceAll('\\','/')+'/';
 const pchHeader=path.join(here,'native-pch.h'),pchSource=path.join(out,'native-pch.cpp'),pchImage=path.join(out,'native.pch');
 fs.writeFileSync(pchSource,'#include "native-pch.h"\n');
 const commonFlags='cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNOMINMAX /DUNICODE /D_UNICODE /I "'+path.join(root,'native')+'" /I "'+path.join(root,'deps/json')+'" /I "'+here+'" /I "'+out+'"';
 const cmd=[...environment,'echo YMCC_NATIVE_COMPILE_STAGE pch %time%',commonFlags+' /Yc"native-pch.h" /Fp"'+pchImage+'" /Fo"'+path.join(out,'native-pch.obj')+'" /c "'+pchSource+'"','if errorlevel 1 exit /b 1','echo YMCC_NATIVE_COMPILE_STAGE variants %time%',commonFlags+' /MP'+compileWorkers+' /Yu"native-pch.h" /FI"native-pch.h" /Fp"'+pchImage+'" /Fo"'+objectRoot+'" /c '+derivedSources.map(row=>'"'+row.derived.path+'"').join(' '),'if errorlevel 1 exit /b 1','echo YMCC_NATIVE_COMPILE_STAGE link %time%'];
 for(const mode of modes)cmd.push(`link /nologo /OUT:"${path.join(out,mode+'.exe')}" "${path.join(out,mode+'.pch.obj')}" "${path.join(out,'native-pch.obj')}" advapi32.lib user32.lib userenv.lib winhttp.lib ws2_32.lib bcrypt.lib`,'if errorlevel 1 exit /b 1');
 cmd.push('echo YMCC_NATIVE_COMPILE_STAGE done %time%');
 fs.writeFileSync(path.join(out,'build.cmd'),cmd.join('\r\n')+'\r\n');const nativeBuildStarted=performance.now();const build=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',path.join(out,'build.cmd')],{encoding:'utf8',windowsHide:true,timeout:90000,cwd:out});const buildText=(build.stdout||'')+(build.stderr||'');fs.writeFileSync(path.join(out,'build.log'),buildText);
 const stageMarkers=[...buildText.matchAll(/YMCC_NATIVE_COMPILE_STAGE (pch|variants|link|done)\s+(\d{1,2}):(\d{2}):(\d{2})[.,](\d{2})/g)].map(([,stage,h,m,s,cs])=>({stage,timeMs:Number(h)*3600000+Number(m)*60000+Number(s)*1000+Number(cs)*10}));
 const stageDurations=stageMarkers.slice(1).map((row,index)=>({stage:stageMarkers[index].stage,elapsedMs:(row.timeMs-stageMarkers[index].timeMs+86400000)%86400000}));
 report.nativeCompilation={status:build.status,error:build.error?.message??null,signal:build.signal,durationMs:performance.now()-nativeBuildStarted,originalBudgetMs:90000,commonHeader:{path:pchHeader,sha256:hash(pchHeader)},commonPrefix:{path:prefixFile,sha256:hash(prefixFile),bytes:Buffer.byteLength(commonPrefix)},derivedSources:{path:transformFile,sha256:hash(transformFile)},capacity:{...capacity,compileWorkers},stageMarkers,stageDurations,compilerRecipe:{path:path.join(out,'build.cmd'),sha256:hash(path.join(out,'build.cmd'))},modes:modes.length,precompiledHeaderOnly:false,sharedPreamblePrecompiled:true,fixtureDriverOptimization:"disabled for generated wmain only",independentVariantObjects:true};fs.writeFileSync(path.join(out,'native-compile-process.json'),JSON.stringify(report.nativeCompilation,null,2)+'\n');if(build.status!==0)throw Error('Native test compile failed: '+(build.error?.message??'')+' '+buildText.slice(-1800));
 // Actual Win32 LoaderTemporaryRun paths require a short owned home. Keep
 // each exact compiled image and all caller evidence, execute its byte-identical
 // copy in a separate task-owned directory, and retain the original20s budget.
 const nativeWorkParent=path.resolve(root,'../../../Testing/YMCC/Workspace/Dn');
 fs.mkdirSync(nativeWorkParent,{recursive:true});
 if(fs.lstatSync(nativeWorkParent).isSymbolicLink()||path.resolve(fs.realpathSync(nativeWorkParent)).toLowerCase()!==nativeWorkParent.toLowerCase())throw Error('Native core workspace ancestry changed');
 const nativeWork=fs.mkdtempSync(path.join(nativeWorkParent,'r'));
 report.nativeExecutionPaths={schemaVersion:1,work:nativeWork,callerEvidence:out,exactCompiledImages:[],originalNativeBudgetMs:20000};
 fs.writeFileSync(path.join(nativeWork,'OWNER.json'),JSON.stringify({kind:'decky-update-recovery-owned-native-fixtures',callerEvidence:out,work:nativeWork,parentPid:process.pid},null,2)+'\n');
 for(const mode of ['fixed','prewarm-negative','wake-preparation-negative','legacy','retained-negative','repair-uncertain-negative','repair-retained-negative','repair-auto-repeat-negative','repair-auto-ui-negative','repair-stale-mode-negative','wake-power-guard-negative','wake-binding-negative','wake-ready-event-negative']){const modeWork=path.join(nativeWork,String(report.nativeExecutionPaths.exactCompiledImages.length));fs.mkdirSync(modeWork);const compiled=path.join(out,mode+'.exe'),executable=path.join(modeWork,'fixture.exe');fs.copyFileSync(compiled,executable,fs.constants.COPYFILE_EXCL);const compiledSha256=hash(compiled);if(hash(executable)!==compiledSha256)throw Error('Native compiled image copy identity changed');report.nativeExecutionPaths.exactCompiledImages.push({mode,compiled,executable,sha256:compiledSha256,fixture:path.join(modeWork,'recovery-fixture')});fs.writeFileSync(path.join(out,'native-execution-paths.json'),JSON.stringify(report.nativeExecutionPaths,null,2)+'\n');const nativeRunStarted=performance.now();const run=spawnSync(executable,[path.join(modeWork,'recovery-fixture'),path.join(here,'field-record.json')],{encoding:'utf8',windowsHide:true,timeout:20000,cwd:modeWork});if(hash(compiled)!==compiledSha256||hash(executable)!==compiledSha256)throw Error('Native compiled image changed during execution');const text=(run.stdout||'')+(run.stderr||'');fs.writeFileSync(path.join(out,mode+'.log'),text);fs.writeFileSync(path.join(out,mode+'-native-exit.json'),JSON.stringify({schemaVersion:1,mode,executable,binarySha256:compiledSha256,pid:run.pid,exitCode:run.status,signal:run.signal,error:run.error?.message??null,elapsedMs:performance.now()-nativeRunStarted,logSha256:hash(path.join(out,mode+'.log')),originalNativeBudgetMs:20000},null,2)+'\n');
 const observed=path.join(modeWork,'native-stage-observation.json');
 const stageReceipt={mode,binarySha256:compiledSha256,ownedDirectory:modeWork,status:'NOT_OBSERVED',reason:'UNKNOWN'};
 try{if(fs.existsSync(observed)){
  if(path.resolve(fs.realpathSync(observed)).toLowerCase()!==observed.toLowerCase())throw Error('Native stage receipt path identity changed');
  const observationBytes=fs.readFileSync(observed),observation=JSON.parse(observationBytes.toString('utf8'));
  if(observation.schemaVersion!==1||observation.observationOnly!==true||path.resolve(observation.ownedDirectory).toLowerCase()!==path.resolve(modeWork).toLowerCase())throw Error('Native stage receipt owner/schema changed');
  const digest=crypto.createHash('sha256').update(observationBytes).digest('hex'),destination=path.join(out,mode+'-native-stage-observation.json');
  fs.copyFileSync(observed,destination,fs.constants.COPYFILE_EXCL);
  if(hash(destination)!==digest||hash(observed)!==digest)throw Error('Native stage receipt copy changed');
  Object.assign(stageReceipt,{status:observation.dropped>0||observation.checkpoint!=='RETAINED_PHASE_COMPLETED'?'PARTIAL':'OBSERVED',reason:null,source:observed,destination,sha256:digest,checkpoint:observation.checkpoint,dropped:observation.dropped,eventCount:observation.eventCount});
 }
 }catch(error){Object.assign(stageReceipt,{status:'INVALID',error:{name:error.name,message:error.message}});}
 (report.nativeExecutionPaths.stageObservations??=[]).push(stageReceipt);
 fs.writeFileSync(path.join(out,'native-execution-paths.json'),JSON.stringify(report.nativeExecutionPaths,null,2)+'\n');
 if(mode==='fixed'){if(run.status!==0||!text.includes('DECKY_UPDATE_RECOVERY_OK'))throw Error('Fixed native consumer failed: '+text);report.nativeExecutionPaths.outputs=[];for(const name of ['native-wake-commit.json','xbox-fullscreen-admission.json','native-prewarm-startup-matrix.json']){const source=path.join(modeWork,name),destination=path.join(out,name);if(path.resolve(fs.realpathSync(source)).toLowerCase()!==source.toLowerCase())throw Error('Native result path identity changed');const digest=hash(source);fs.copyFileSync(source,destination,fs.constants.COPYFILE_EXCL);if(hash(destination)!==digest||hash(source)!==digest)throw Error('Native result copy changed');report.nativeExecutionPaths.outputs.push({source,destination,sha256:digest});}fs.writeFileSync(path.join(out,'native-execution-paths.json'),JSON.stringify(report.nativeExecutionPaths,null,2)+'\n');report.nativeChecks=Number(/checks=(\d+)/.exec(text)[1]); }else if(mode==='wake-power-guard-negative'){if(run.status!==1||!text.includes('uncommitted power phase cannot emit Ready or renew Decky binding'))throw Error('Missing power guard negative must fail at actual Ready admission: '+text);report.wakePowerGuardNegativeDetected=true;
 }else if(mode==='prewarm-negative'){if(run.status!==1||!text.includes('native Runtime prewarms resources without requiring a running Steam process'))throw Error('Steam-dependent prewarm negative must fail at the actual early resource boundary: '+text);report.nativePrewarmRegressionDetected=true;
 }else if(mode==='wake-preparation-negative'){if(run.status!==1||!text.includes('actual committed Ready retries native prewarm while Steam remains absent'))throw Error('Missing resume preparation negative must fail at the actual committed Ready boundary: '+text);report.nativeWakePreparationRegressionDetected=true;
 }else if(mode==='wake-binding-negative'){if(run.status!==1||!text.includes('actual committed wake reaches production Runtime binding refresh'))throw Error('Missing native wake binding negative must fail its actual Runtime call: '+text);report.wakeBindingNegativeDetected=true;
 }else if(mode==='wake-ready-event-negative'){if(run.status!==1||!text.includes('actual committed wake emits current generation Ready event'))throw Error('Missing Ready event negative must fail its actual emitted event: '+text);report.wakeReadyEventNegativeDetected=true;
}else if(mode==='repair-auto-ui-negative'){if(run.status!==1||!text.includes('automatic retained repair cannot rebuild Steam UI or consume manual launch consent'))throw Error('Removed automatic UI fence must fail at actual dispatch: '+text);report.automaticUiRefreshNegativeDetected=true;}else if(mode==='repair-stale-mode-negative'){if(run.status!==1||!text.includes('explicit owned repair must read current frontend instead of stale pre-load cache'))throw Error('Stale frontend cache negative must fail its actual explicit repair assertion: '+text);report.manualFreshModeRegressionDetected=true;}else if(mode==='repair-auto-repeat-negative'){if(run.status!==1||!text.includes('one automatic attempt per document survives duplicate events'))throw Error('Automatic repeat negative must fail its original assertion: '+text);report.automaticRepeatRegressionDetected=true;}else if(mode==='repair-uncertain-negative'){if(run.status!==1||!text.includes('exception after dispatch preserves uncertain mutation'))throw Error('Uncertain dispatch negative must fail its original assertion: '+text);report.manualRepairUncertaintyRegressionDetected=true;}else if(mode==='repair-retained-negative'){if(run.status!==1||!text.includes('retained bridge with absent owned backend permits explicit repair'))throw Error('Retained backend repair negative must fail its original assertion: '+text);report.manualRepairRetainedRegressionDetected=true;}else if(mode==='retained-negative'){if(run.status!==1||!text.includes('retained frontend must restore broker without official Loader'))throw Error('Removed retained path must fail at real broker restore: '+text);report.retainedRegressionDetected=true;}else{if(run.status!==1||!text.includes('field regression: global recovery cannot require a special origin'))throw Error('Legacy must fail for the actual stranded global admission: '+text);report.legacyRegressionDetected=true;}}
 // Observation integrity is separate from each original native semantic verdict.
 // Earlier designated assertion failures keep their original error and raw exit.
 const observedNative=report.nativeExecutionPaths.stageObservations??[];
 const invalidNative=observedNative.filter(row=>row.status==='INVALID'||row.dropped>0);
 const missingRequiredNative=['fixed','wake-binding-negative','wake-power-guard-negative','wake-ready-event-negative'].filter(mode=>!observedNative.some(row=>row.mode===mode&&row.status==='OBSERVED'));
 if(invalidNative.length||missingRequiredNative.length)throw Error('Native stage evidence incomplete: '+JSON.stringify({invalid:invalidNative,missing:missingRequiredNative}));
 report.modeReadCases=await readTests(root,out);report.clientCases=await clientTests(root,out);report.sliderStability=sliderStabilityTests(root,path.join(out,'slider-stability'));
 // Exercise the exact generated updater launch expression in PowerShell with Start-Process inert.
 const line=main.split('\n').find(l=>l.includes('$newProcess = Start-Process')&&l.includes('$deckyResumeArgs')),argLine=main.split('\n').find(l=>l.includes('$deckyResumeArgs = @();'));if(!line||!argLine)throw Error('Updater must forward scoped Decky handoff');
 const decode=line=>JSON.parse(line.slice(line.indexOf('"'),line.lastIndexOf('"')+1));
 const ps=`$ErrorActionPreference='Stop'\nfunction Start-Process {param($FilePath,$WorkingDirectory,$ArgumentList,[switch]$PassThru) $script:seen=@($ArgumentList);return @{Id=42}}\n$exePath='fixture.exe';$exeDir='fixture';$handshakePath='C:\\fixture only\\handshake.json';$handshakeToken='test-only'\nforeach($ticket in @('45640:134368675855384222','')){\n$deckyResumeTicket=$ticket\n${decode(argLine)}${decode(line)}if($ticket -and ($seen[-2] -ne '--decky-update-resume' -or $seen[-1] -ne $ticket)){throw 'handoff lost'}\nif(!$ticket -and $seen -contains '--decky-update-resume'){throw 'unconnected updater manufactured handoff'}\n}\nWrite-Output 'UPDATE_HANDOFF_ARGUMENTS_OK cases=2'\n`;
 fs.writeFileSync(path.join(out,'updater-args.ps1'),ps);const powershell=process.env.YMCC_SIM_POWERSHELL||'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe';const psResult=execFileSync(powershell,['-NoProfile','-NonInteractive','-Command',ps],{encoding:'utf8',windowsHide:true,timeout:15000});fs.writeFileSync(path.join(out,'updater-args.log'),psResult);if(!psResult.includes('UPDATE_HANDOFF_ARGUMENTS_OK'))throw Error('Updater handoff fixture failed');report.updaterArgumentCases=2;
 const wake=await wakeChainTests(root,out);report.wakeChainChecks=wake.checks;report.wakeHostEventNegativeDetected=wake.committedEventNegativeDetected;
 report.noRepeatContextCases=noRepeatContextTests(root,out);
 const cssReceipt=await cssAppearanceWorker.finish();
 const cssReceiptFile=path.join(cssAppearanceWorker.evidence,'result.json');
 report.cssParallelExecution={parallel:true,status:cssReceipt.status,resourcesDestroyed:cssReceipt.resourcesDestroyed,cssSuiteExecuted:cssReceipt.cssSuiteExecuted,deadlineMs:cssReceipt.deadlineMs,workerReceipt:{path:cssReceiptFile,sha256:hash(cssReceiptFile)},work:cssReceipt.work,raw:cssReceipt.raw?.originalRaw??null,originalPublicBudgetMs:240000};
 if(cssReceipt.ok!==true||cssReceipt.status!=='PASS'||cssReceipt.cssSuiteExecuted!==true||cssReceipt.resourcesDestroyed!==true||!cssReceipt.originalReport||Date.now()>baselineStartedAt+240000)throw Error('Parallel original CSS chain failed or retirement not proven: '+cssReceipt.status);
 for(const reserved of ['passed','sourceInputs','sourceHashes','fieldRegression','nativeChecks','nativeExecutionPaths'])if(Object.hasOwn(cssReceipt.originalReport,reserved))throw Error('CSS report attempted to replace main verdict/source fields');
 Object.assign(report,cssReceipt.originalReport);
 const cssRaw=cssReceipt.raw?.originalRaw;
 if(!cssRaw||!Array.isArray(cssRaw.files)||path.resolve(cssRaw.canonical).toLowerCase()!==path.resolve(cssAppearanceWorker.evidence,'css').toLowerCase())throw Error('Original CSS raw map missing or owner changed');
 if(hash(cssRaw.map.path)!==cssRaw.map.sha256)throw Error('Original CSS raw map changed');
 // Raw worker receipts stay byte-exact. The public validation alias is explicitly
 // derived only to rebind its three pinned fixture-source keys into this case.
 report.cssCanonicalForwarding=forwardCssRequiredReceipts({cssRaw,out});
 fs.writeFileSync(path.join(out,'CSS-PARALLEL-CONSUMED.json'),JSON.stringify({schemaVersion:1,originalCssBlock:cssReceipt.bootstrap?.originalCssBlock,execution:report.cssParallelExecution,forwarding:report.cssCanonicalForwarding,sourceBound:true,originalAssertionsUnchanged:true},null,2)+'\n');
 const nativeLaunchReceipt=await nativeLaunchWorker.finish();
 report.nativeManualLaunchExecution={parallel:true,workerReceipt:{path:path.join(nativeLaunchWorker.evidence,'result.json'),sha256:hash(path.join(nativeLaunchWorker.evidence,'result.json'))},work:nativeLaunchReceipt.work,mainParent:nativeLaunchReceipt.mainParent,helper:nativeLaunchReceipt.helper,deadlineMs:nativeLaunchReceipt.deadlineMs,resourcesDestroyed:nativeLaunchReceipt.resourcesDestroyed,status:nativeLaunchReceipt.status,originalParentBudgetMs:240000};
 if(nativeLaunchReceipt.ok!==true||nativeLaunchReceipt.status!=='PASS'||nativeLaunchReceipt.resourcesDestroyed!==true||nativeLaunchReceipt.noIoSuiteExecuted!==true)throw Error('Parallel original NoIO launch regression incomplete: '+nativeLaunchReceipt.status);
 const nativeLaunchRaw=nativeLaunchReceipt.raw.originalRaw.result;
 if(hash(nativeLaunchRaw.path)!==nativeLaunchRaw.sha256)throw Error('Parallel native launch raw result identity changed');
 report.nativeManualLaunch=nativeLaunchReceipt.originalResult;
 const nativeLaunchCopy=path.join(out,'native-manual-launch/result.json');fs.mkdirSync(path.dirname(nativeLaunchCopy),{recursive:true});fs.copyFileSync(nativeLaunchRaw.path,nativeLaunchCopy,fs.constants.COPYFILE_EXCL);
 if(hash(nativeLaunchCopy)!==nativeLaunchRaw.sha256)throw Error('Parallel native launch result copy changed');
 report.nativeManualLaunchExecution.originalRaw=nativeLaunchRaw;report.nativeManualLaunchExecution.callerRaw={path:nativeLaunchCopy,sha256:hash(nativeLaunchCopy)};
 fs.writeFileSync(path.join(out,'native-manual-launch/ORIGINAL-RUN.json'),JSON.stringify(report.nativeManualLaunchExecution,null,2)+'\n');
 const unchanged=inputs.every(i=>hash(path.join(root,i.path))===i.sha256);if(!unchanged)throw Error('Sources changed while running fixture');report.inputsUnchanged=true;report.passed=true;
 }catch(e){report.error=e.stack;process.exitCode=1;}
finally{
 const cssRetirement=cssAppearanceWorker?cssAppearanceWorker.stop().then(receipt=>({ok:true,receipt}),error=>({ok:false,error})):null;
 if(nativeLaunchWorker){
  try{const retired=await nativeLaunchWorker.stop();report.nativeLaunchWorkerCleanup={status:retired.status,resourcesDestroyed:retired.resourcesDestroyed,receipt:{path:path.join(nativeLaunchWorker.evidence,'result.json'),sha256:hash(path.join(nativeLaunchWorker.evidence,'result.json'))}};if(retired.resourcesDestroyed!==true){report.passed=false;report.error=(report.error||'')+'\nOwned NoIO worker retirement unproven';process.exitCode=1;}}
  catch(error){report.passed=false;report.error=(report.error||'')+'\nOwned NoIO worker cleanup: '+String(error.stack??error);process.exitCode=1;}
 }
 if(cssRetirement){try{const resolved=await cssRetirement;if(!resolved.ok)throw resolved.error;const retired=resolved.receipt;report.cssAppearanceWorkerCleanup={status:retired.status,resourcesDestroyed:retired.resourcesDestroyed,receipt:{path:path.join(cssAppearanceWorker.evidence,'result.json'),sha256:hash(path.join(cssAppearanceWorker.evidence,'result.json'))}};if(retired.resourcesDestroyed!==true){report.passed=false;report.error=(report.error||'')+'\nOwned CSS worker retirement unproven';process.exitCode=1;}}catch(error){report.passed=false;report.error=(report.error||'')+'\nCSS worker cleanup: '+error.stack;process.exitCode=1;}}
 if(report.passed&&!inputs.every(row=>hash(path.join(root,row.path))===row.sha256)){report.passed=false;report.error='Sources changed during final owned-worker cleanup';process.exitCode=1;}
}
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,nativeChecks:report.nativeChecks,modeReadCases:report.modeReadCases,automaticAppearanceCases:report.automaticAppearanceCases,nativeAppearanceCases:report.nativeAppearanceCases,noRepeatContextCases:report.noRepeatContextCases,retiredLinkCases:report.retiredLinkCases,retiredAddonCases:report.retiredAddonCases,legacyRegressionDetected:report.legacyRegressionDetected,error:report.error}));
