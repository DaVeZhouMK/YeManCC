import {observeOnlyHarness,validateObserveOnlyFailure} from './observe-only-negative.mjs';
import {trainerRecoveryTests} from '../../../decky_trainer_recovery_selftest.mjs';
import {wakeChainTests} from '../../../decky_wake_chain_selftest.mjs';
import {frontendBootTests} from '../../../decky_sidebar_frontend_boot_selftest.mjs';
import {trainerForegroundTests} from '../../../decky_trainer_foreground_selftest.mjs';
import {manualRepairTests} from '../../../decky_manual_repair_selftest.mjs';
import {retainedSessionRepairTests} from '../../../decky_retained_session_repair_selftest.mjs';
import {resourceCacheTests} from '../../../decky_resource_cache_selftest.mjs';
import {resourceWorkerTests} from '../../../decky_resource_worker_selftest.mjs';
import {sliderStabilityTests} from '../../../decky_slider_stability_selftest.mjs';
// Actual native callbacks/Runtime and exact Steam read; no installed products are contacted.
import {nativeLaunchTests} from '../../../steam_native_launch_regression.mjs';
import {prepareNativeLaunchNoIoFixture,NATIVE_LAUNCH_NOIO_SOURCE_INPUTS} from './native-launch-noio/index.mjs';
import fs from 'node:fs';
import {startNativeLaunchWorker,NATIVE_LAUNCH_WORKER_SOURCE_INPUTS} from './native-launch-worker.mjs';
import {startCssAppearanceWorker,CSS_APPEARANCE_WORKER_SOURCE_INPUTS} from './css-appearance-worker.mjs';
import {getCssAppearanceInputs} from './css-appearance-sequence.mjs';
import {forwardCssRequiredReceipts} from './css-required-forwarding.mjs';
const baselineStartedAt=Date.now();
import {verifyPackageFixtureTools} from './package-fixture-tools.mjs';import path from 'node:path';import crypto from 'node:crypto';import {execFileSync,spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';import {readTests} from './read-selftest.mjs';import {clientTests} from './client-selftest.mjs';import {noRepeatContextTests} from '../../../decky_no_repeat_context_selftest.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(here,'../../../..'),args=process.argv.slice(2),at=args.indexOf('--evidence-dir'),out=path.resolve(at<0?process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-update-recovery':args[at+1]);fs.mkdirSync(out,{recursive:true});
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
// Exact current C++ blocks; neighboring config fields never define a body boundary.
function uniqueAnchor(source,needle,label){const at=source.indexOf(needle);if(at<0||source.indexOf(needle,at+needle.length)>=0)throw Error('Review unique native '+label+' anchor');return at;}
function cppBlock(source,open,label){
 if(source[open]!=='{')throw Error('Native '+label+' body missing');
 let depth=0,state='code',escaped=false;
 for(let i=open;i<source.length;i++){
  const c=source[i],next=source[i+1];
  if(state==='line'){if(c==='\n')state='code';continue;}
  if(state==='block'){if(c==='*'&&next==='/'){state='code';i++;}continue;}
  if(state==='string'||state==='character'){
   if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}
   if(c===(state==='string'?'"':"'"))state='code';continue;
  }
  if(c==='/'&&next==='/'){state='line';i++;continue;}
  if(c==='/'&&next==='*'){state='block';i++;continue;}
  if(c==='R'&&next==='"')throw Error('Review raw literal in native '+label);
  if(c==='"'){state='string';continue;}if(c==="'"){state='character';continue;}
  if(c==='{')depth++;else if(c==='}'&&--depth===0)return {open,end:i+1,body:source.slice(open+1,i)};
 }
 throw Error('Unterminated native '+label+' body');
}

function closure(file,seen=new Set()) {if(seen.has(file)||!fs.existsSync(file))return seen;seen.add(file);for(const m of fs.readFileSync(file,'utf8').matchAll(/^\s*#include\s+"([^"\n]+)"/gm)){const candidate=[path.resolve(path.dirname(file),m[1]),path.join(root,'native',m[1]),path.join(root,'deps/json',m[1])].find(fs.existsSync);if(candidate&&candidate.startsWith(root+path.sep))closure(candidate,seen);}return seen;}
const files=[path.join(root,'tools/decky_wake_chain_selftest.mjs'),path.join(root,'tools/fixtures/decky_mirror_host_fixture.mjs'),path.join(root,'src/bridge/deckyMirrorRelay.ts'),path.join(root,'tools/decky_sidebar_frontend_boot_selftest.mjs'),path.join(root,'native/decky_sidebar_frontend_update.js'),path.join(root,'native/decky_sidebar_frontend_update_script.h'),path.join(root,'tools/decky_manual_repair_selftest.mjs'),path.join(root,'src/bridge/deckySidebar.ts'),path.join(root,'native/decky_sidebar_manual_repair.h'),...new Set([...closure(path.join(here,'native-fixture.cpp')),...closure(path.join(root,'native/decky_update_recovery.h')),path.join(root,'native/main.cpp'),path.join(root,'native/steam_live.js'),path.join(root,'native/steam_live_script.h'),path.join(root,'native/steam_ui_mode.h'),...fs.readdirSync(here).filter(f=>!f.endsWith('.md')&&fs.statSync(path.join(here,f)).isFile()).map(f=>path.join(here,f))])];
for(const relative of ["native/steam_native_launch.h","native/steam_native_launch_windows.h","tools/steam_native_launch_selftest.cpp","tools/steam_native_launch_selftest.ps1","tools/steam_native_launch_regression.mjs","tools/steam_launch_admission_selftest.mjs","tools/steam_linked_launch_selftest.mjs","src/views/PowerView.vue",'tools/decky_no_repeat_context_selftest.mjs','decky-plugin/vendor/decky-loader.lock.json','tools/steam_appearance_automatic_refresh_selftest.ps1','tools/ymcc_sim/global/decky-update-recovery/appearance-library-fixture.ps1','tools/steam_startup_observation_selftest.ps1','tools/steam_startup_observation_extract.mjs','tools/steam_startup_observation_selftest.cpp','tools/steam_cssloader_use_selftest.ps1','tools/steam_cssloader_resume_selftest.ps1','tools/steam_cssloader_resume_selftest.cpp','tools/steam_cssloader_resume_extract.mjs','tools/steam_appearance_render_plan_selftest.ps1','native/steam_cssloader_legacy_links.h','tools/steam_cssloader_legacy_links_selftest.cpp','tools/steam_cssloader_legacy_links_selftest.ps1','tools/steam_css_addon_retired_selftest.mjs','src/bridge/yeman.ts','src/views/SteamView.vue','PowerControl/YeManSteam.vbs','native/steam_cssloader_mode.h','native/steam_cssloader_guard.h','PowerControl/steam-appearance.ps1','PowerControl/steam-appearance/preset.ps1','PowerControl/steam-appearance/idle.ps1','PowerControl/steam-appearance/resident.ps1','PowerControl/steam-appearance/manifest.json'])files.push(path.join(root,relative));
for(const relative of ["native/decky_sidebar_setup.h","tools/decky_sidebar_setup_selftest.cpp","tools/decky_sidebar_runtime_preparation_selftest.cpp","tools/decky_sidebar_runtime_regression.mjs","tools/decky_sidebar_runtime_lifecycle_probe.mjs","tools/ymcc_sim/global/decky-update-recovery/field-record-native-startup-20261022.json"]){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
for(const relative of NATIVE_LAUNCH_NOIO_SOURCE_INPUTS){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
for(const relative of ["decky-plugin/src/index.tsx","decky-plugin/src/PerformanceControls.tsx","decky-plugin/src/performanceIntentQueue.ts","decky-plugin/src/performanceModel.ts","decky-plugin/src/fanChoiceDebounce.ts","tools/decky_slider_stability_selftest.mjs","tools/decky_sidebar_client_selftest.mjs","tools/fixtures/decky_game_memory_store.mjs","tools/decky_performance_intent_selftest.mjs","tools/decky_sidebar_slider_drag_selftest.mjs","tools/fixtures/decky_task_paths.mjs"]){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
for(const relative of ['tools/decky_trainer_foreground_selftest.mjs','tools/game_trainer_foreground_selftest.ps1','src/bridge/gameTrainer.ts','src/bridge/deckyTrainerActions.ts','src/bridge/deckyMirrorHost.ts','PowerControl/Trainer/TrainerForeground.ps1','PowerControl/Trainer/TrainerLaunch.ps1','PowerControl/Trainer/TrainerBackend.psm1','PowerControl/YeManTrainerLaunch.ps1']){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
for(const relative of ["tools/decky_trainer_recovery_selftest.mjs","tools/fixtures/game_trainer_recovery_fixture.mjs","tools/fixtures/trainer_ready_tmnt_20261022.json","src/bridge/gameTrainerRecovery.ts","src/bridge/gameTrainerAuto.ts"]){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
// The unchanged foreground consumer imports the Trainer module and copies all
// of its PowerShell files into a negative fixture. These are runtime inputs too.
for(const relative of [...fs.readdirSync(path.join(root,'PowerControl/Trainer')).filter(name=>/\.(?:ps1|psm1)$/.test(name)).map(name=>'PowerControl/Trainer/'+name),'PowerControl/GameTitlePolicy.ps1','PowerControl/GameTitlePolicy.json']){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
for(const relative of ['tools/steam_cssloader_exit_selftest.ps1','tools/steam_cssloader_exit_selftest.cpp','tools/steam_cssloader_exit_extract.mjs']){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
for(const relative of ["native/steam_cssloader_launch.h","tools/steam_cssloader_manual_observation_selftest.cpp","tools/steam_cssloader_manual_observation_selftest.ps1","tools/steam_cssloader_manual_observation_extract.mjs","tools/steam_appearance_manual_observation_selftest.ps1","tools/steam_appearance_content_update.mjs","tools/steam_appearance_use_service_selftest.mjs"]){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
// Original resource consumers freeze their reviewed compiler authority as data.
files.push(path.join(root,'tools/ymcc_sim/global/decky-persistent-lifecycle/owned-tools.json'));
// Bundler alias configuration is an input even for wholly inert consumers.
for(const relative of ['tsconfig.json','tsconfig.node.json','package.json']){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
// The unchanged memory-store consumer reads these source modules directly.
// Declare them as inputs so selected-source runs and drift checks cover them.
for(const relative of ['src/bridge/performanceControls.ts','src/bridge/controllerPersona.ts','src/bridge/virtualTargetPolicy.ts','src/bridge/settingsSnapshot.ts','src/bridge/settingsRepository.ts','src/bridge/performanceSchedule.ts','src/bridge/quickActionLock.ts','src/bridge/gamePolicyHysteresis.ts','PowerControl/GameTitlePolicy.json','src/bridge/gamedetect.ts','src/bridge/frameRateModel.ts','PowerControl/decky/PluginLoader_noconsole.exe']){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
const appearancePack=JSON.parse(fs.readFileSync(path.join(root,'PowerControl/steam-appearance/manifest.json'),'utf8'));for(const file of appearancePack.files)files.push(path.join(root,'PowerControl/steam-appearance/themes',file.path));
files.push(path.join(root,'tools/decky_retained_session_repair_selftest.mjs'),path.join(root,'native/decky_package_maintenance.ps1'),path.join(root,'native/decky_package_maintenance_script.h'),path.join(root,'tools/decky_package_maintenance_embed.mjs'),path.join(root,'tools/decky_package_maintenance_selftest.ps1'));
files.push(...NATIVE_LAUNCH_WORKER_SOURCE_INPUTS.map(relative=>path.join(root,relative)));
for(const relative of ['tools/ymcc_sim/global/decky-update-recovery/css-required-forwarding.mjs',...CSS_APPEARANCE_WORKER_SOURCE_INPUTS,...getCssAppearanceInputs(root)]){const file=path.join(root,relative);if(!files.includes(file))files.push(file);}
files.push(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/resource-fixture-tools.json'),path.join(root,'tools/ymcc_sim/global/decky-update-recovery/resource-fixture-tools.mjs'));
files.push(...['tools/decky_resource_cache_selftest.mjs','tools/decky_resource_cache_selftest.cpp','tools/decky_resource_worker_selftest.mjs','tools/decky_resource_worker_selftest.cpp','native/decky_resource_worker.h','native/decky_sidebar_resource_cache.h','native/decky_binding_worker_entry.h','native/decky_binding_process_owner.h','native/decky_binding_client.h'].map(relative=>path.join(root,relative)));
// Close repository-local literal reads and static local module imports. This
// includes source modules bundled by nested consumers; assertions stay unchanged.
const literalSeen=new Set();
const resolveLocalInput=candidate=>[candidate,...['.mjs','.js','.ts','.tsx','.vue','.json','.css','/index.mjs','/index.js','/index.ts'].map(ext=>candidate+ext)].find(file=>fs.existsSync(file)&&fs.statSync(file).isFile());
for(let index=0;index<files.length;index++){
 const owner=files[index];if(!/\.(?:mjs|js|ts|tsx|vue)$/.test(owner)||literalSeen.has(owner))continue;literalSeen.add(owner);
 const source=fs.readFileSync(owner,'utf8'),candidates=[];
 for(const match of source.matchAll(/(?:path\.join\(root,\s*|functions\(\s*)['"]([^'"\r\n]+)['"]/g)){
  const relative=match[1];if(path.isAbsolute(relative)||relative.split(/[\\/]/).includes('..'))throw Error('Literal source input escaped repository: '+relative);
  candidates.push(path.resolve(root,relative));
 }
 for(const match of source.matchAll(/(?:from\s*|import\s*(?:\(\s*)?|require\(\s*|new URL\(\s*)['"]((?:\.|@\/)[^'"\r\n]+)['"]/g)){
  const specifier=match[1].split('?')[0];candidates.push(specifier.startsWith('@/')?path.resolve(root,'src',specifier.slice(2)):path.resolve(path.dirname(owner),specifier));
 }
 for(const candidate of candidates){if(!candidate.toLowerCase().startsWith((root+path.sep).toLowerCase()))throw Error('Local module input escaped repository: '+candidate);const resolved=resolveLocalInput(candidate);if(resolved&&!files.includes(resolved))files.push(resolved);}
}
// Nested native consumers compile their own source trees, not only the main
// recovery fixture. Close every declared native translation unit/header too.
const nestedNative=new Set();for(const file of [...files])if(/\.(?:cpp|h)$/.test(file)&&file!==path.join(root,'native/main.cpp'))closure(file,nestedNative);
for(const file of nestedNative)if(!files.includes(file))files.push(file);
const inputs=files.map(file=>({path:path.relative(root,file).replaceAll('\\','/'),sha256:hash(file)}));const report={passed:false,fieldRegression:'decky-update-disconnect-20261020',additionalFieldRegressions:['decky-native-prewarm-startup-orders-20261022','decky-null-window-20261021','decky-tdp-qam-reentry-20261021','decky-multi-qam-visibility-20261021','decky-reentrant-transport-fence-20261021'],realSteamActions:0,userSettingsWrites:0,hardwareWrites:0,sourceInputs:inputs,sourceHashes:inputs};
let nativeLaunchWorker=null,cssAppearanceWorker=null;
try{
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
 const packageResult={startedAt:packageStartedAt,finishedAt:new Date().toISOString(),elapsedMs:performance.now()-packageStart,
   executable:packageToolBefore,source:packageSourceBefore,sourceSha256After:sourceAfter.sha256,toolSha256After:toolAfter.sha256,sourceReadErrorAfter:sourceAfter.error,toolReadErrorAfter:toolAfter.error,args:packageArgs,
   pid:packageRun.pid??null,status:packageRun.status,signal:packageRun.signal,
   error:packageRun.error?{name:packageRun.error.name,code:packageRun.error.code,errno:packageRun.error.errno,message:packageRun.error.message,stack:packageRun.error.stack}:null,
   fixtureCompilerAuthority:{manifestSha256:fixtureCompilerAuthority.manifestSha256,compiler:fixtureCompilerAuthority.compiler,inputs:fixtureCompilerAuthority.files.length,osLoadedModulesComplete:false},
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
 report.additionalFieldRegressions.push('decky-automatic-managed-package-20261023');
 report.resourceCache=await resourceCacheTests(root,path.join(out,'resource-cache'));
 report.resourceWorker=await resourceWorkerTests(root,path.join(out,'resource-worker'));
 report.additionalFieldRegressions.push('decky-local-resource-repair-and-owned-worker-20261023');
 report.retainedSessionRepair=await retainedSessionRepairTests(root,path.join(out,'retained-session-repair'));
 report.additionalFieldRegressions.push('decky-retained-session-repair-rogx-20261023');
 report.trainerForeground=await trainerForegroundTests(path.join(out,'trainer-foreground'));
 report.trainerRecovery=await trainerRecoveryTests(path.join(out,'trainer-recovery'));
 report.manualRepairCases=await manualRepairTests(root,out,{upgradeActionsFile:path.join(out,'retained-session-repair/native.json')});const boot=await frontendBootTests(root,out);report.frontendBootCases=boot.cases;report.frontendBootNegativeDetected=boot.negativeDetected;
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 const a=uniqueAnchor(main,'[]() -> ymcc::deckysidebar::LaunchAdmission {','launch admission'),m=uniqueAnchor(main,'modeObserver.changed = []','mode observer');
 const admissionBlock=cppBlock(main,main.indexOf('{',a),'launch admission'),changedBlock=cppBlock(main,main.indexOf('{',m),'mode observer');
 const admission=admissionBlock.body,changed=changedBlock.body,template=fs.readFileSync(path.join(here,'native-fixture.cpp'),'utf8');
 report.nativeCallbackExtraction={method:'unique anchor and lexical balanced C++ body',mainSha256:hash(path.join(root,'native/main.cpp')),admission:{begin:admissionBlock.open,end:admissionBlock.end,sha256:crypto.createHash('sha256').update(admission).digest('hex')},modeChanged:{begin:changedBlock.open,end:changedBlock.end,sha256:crypto.createHash('sha256').update(changed).digest('hex')}};
 const wakeBegin=main.indexOf('    case WM_POWER_RESUME_COMMIT:'),wakeEnd=main.indexOf('    case WM_POWER_INPUT_RECOVER:',wakeBegin);
 if(wakeBegin<0||wakeEnd<0)throw Error('Review native committed wake source boundary');
 const wakeBody=main.slice(wakeBegin+'    case WM_POWER_RESUME_COMMIT:'.length,wakeEnd),wakeMessage=main.split('\n').find(l=>l.startsWith('#define WM_POWER_INPUT_RECOVER '));
 if(!wakeMessage||!wakeBody.includes('return 0;'))throw Error('Missing native wake completion contract');
 const gamingStart=uniqueAnchor(main,'static bool deckySidebarGamingFullscreenConsent(','gaming full-screen consent'),gamingBlock=cppBlock(main,main.indexOf('{',gamingStart),'gaming full-screen consent'),gamingEnd=gamingBlock.end;
 const prewarmStart=uniqueAnchor(main,'static std::string deckySidebarPrepareEnvironment(','native environment preparation'),prewarmBlock=cppBlock(main,main.indexOf('{',prewarmStart),'native environment preparation'),prewarmEnd=prewarmBlock.end;
 const diagnosticStart=uniqueAnchor(main,'static void deckySidebarSessionDiagnostic(','session diagnostic'),diagnosticBlock=cppBlock(main,main.indexOf('{',diagnosticStart),'session diagnostic');
 const fixed=template.replace('/*__SESSION_DIAGNOSTIC__*/',main.slice(diagnosticStart,diagnosticBlock.end)).replace('/*__PREPARE_ENVIRONMENT__*/',main.slice(prewarmStart,prewarmEnd)).replace('/*__GAMING_CONSENT__*/',main.slice(gamingStart,gamingEnd)).replace('/*__ADMISSION__*/',admission).replace('/*__MODE_CHANGED__*/',changed).replace('/*__WAKE_COMMIT__*/',wakeBody).replace('/*__WAKE_MESSAGE__*/',wakeMessage);fs.writeFileSync(path.join(out,'fixed.cpp'),fixed);
 const legacy=fixed.replace('&&!g_deckyUpdateRecovery.allows(session)','');if(legacy===fixed)throw Error('Legacy missing-recovery mutation anchor changed');fs.writeFileSync(path.join(out,'legacy.cpp'),legacy);
 const retainedNegative=fixed.replace('return {true,"retained-frontend-rebind",false,{},true};','return {false,"decky-context-already-loaded",false};');if(retainedNegative===fixed)throw Error('Retained frontend admission mutation missing');fs.writeFileSync(path.join(out,'retained-negative.cpp'),retainedNegative);
 const repairHeader=fs.readFileSync(path.join(root,'native/decky_sidebar_manual_repair.h'),'utf8');
 const uncertainHeader=repairHeader.replace('dispatchEntered=true;','dispatchEntered=false;');
 if(uncertainHeader===repairHeader)throw Error('Uncertain-dispatch mutation anchor missing');
 fs.writeFileSync(path.join(out,'uncertain-manual.h'),uncertainHeader);fs.writeFileSync(path.join(out,'uncertain-auto.h'),fs.readFileSync(path.join(root,'native/decky_sidebar_automatic_repair.h'),'utf8').replace('#include "decky_sidebar_manual_repair.h"','#include "uncertain-manual.h"'));
 fs.writeFileSync(path.join(out,'repair-uncertain-negative.cpp'),fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "uncertain-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "uncertain-auto.h"'));
 const retainedRepairHeader=repairHeader.replace('(runtime.at("phase")=="frontend-attached"&&runtime.at("reason")=="retained-frontend-connected")','false');
 if(retainedRepairHeader===repairHeader)throw Error('Retained backend repair mutation anchor missing');
 fs.writeFileSync(path.join(out,'retained-manual.h'),retainedRepairHeader);fs.writeFileSync(path.join(out,'retained-auto.h'),fs.readFileSync(path.join(root,'native/decky_sidebar_automatic_repair.h'),'utf8').replace('#include "decky_sidebar_manual_repair.h"','#include "retained-manual.h"'));
 fs.writeFileSync(path.join(out,'repair-retained-negative.cpp'),fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "retained-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "retained-auto.h"'));
 const staleHeader=repairHeader.replace('origin==Origin::Manual&&deps.freshMode?deps.freshMode(session):deps.mode(session)','deps.mode(session)');if(staleHeader===repairHeader)throw Error('Manual stale-cache mutation anchor missing');
 fs.writeFileSync(path.join(out,'stale-manual.h'),staleHeader);fs.writeFileSync(path.join(out,'stale-auto.h'),fs.readFileSync(path.join(root,'native/decky_sidebar_automatic_repair.h'),'utf8').replace('#include "decky_sidebar_manual_repair.h"','#include "stale-manual.h"'));
 fs.writeFileSync(path.join(out,'repair-stale-mode-negative.cpp'),fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "stale-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "stale-auto.h"'));
 const noPowerGate=fixed.replace('if (g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready)', 'if (true)');if(noPowerGate===fixed)throw Error('Native wake power guard mutation missing');fs.writeFileSync(path.join(out,'wake-power-guard-negative.cpp'),noPowerGate);
 const noWakeBinding=fixed.replace('const auto rebound=g_deckySidebarRuntime.requestBindingRefresh(currentPowerGeneration());','const auto rebound=false;');if(noWakeBinding===fixed)throw Error('Native wake binding mutation missing');fs.writeFileSync(path.join(out,'wake-binding-negative.cpp'),noWakeBinding);
 const noReadyEvent=fixed.replace('ipc_emit("power.resumed",','ipc_emit("power.pending",');if(noReadyEvent===fixed)throw Error('Native wake ready event mutation missing');fs.writeFileSync(path.join(out,'wake-ready-event-negative.cpp'),noReadyEvent);
 const noAutoGuard=repairHeader.replace('if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user");','');if(noAutoGuard===repairHeader)throw Error('Automatic UI guard anchor missing');
 fs.writeFileSync(path.join(out,'unsafe-automatic-manual.h'),noAutoGuard);fs.writeFileSync(path.join(out,'unsafe-automatic.h'),fs.readFileSync(path.join(root,'native/decky_sidebar_automatic_repair.h'),'utf8').replace('#include "decky_sidebar_manual_repair.h"','#include "unsafe-automatic-manual.h"'));
 fs.writeFileSync(path.join(out,'repair-auto-ui-negative.cpp'),fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "unsafe-automatic-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "unsafe-automatic.h"'));
 const automaticHeader=fs.readFileSync(path.join(root,'native/decky_sidebar_automatic_repair.h'),'utf8'),repeatHeader=automaticHeader.replace('if(spent_||','if(false||');if(automaticHeader===repeatHeader)throw Error('Automatic budget mutation anchor missing');
 fs.writeFileSync(path.join(out,'repeat-auto.h'),repeatHeader);fs.writeFileSync(path.join(out,'repair-auto-repeat-negative.cpp'),fixed.replace('#include "decky_sidebar_automatic_repair.h"','#include "repeat-auto.h"'));
 const prewarmBlocked=fixed.replace('static std::string deckySidebarPrepareEnvironment(const std::function<bool()>& current) {','static std::string deckySidebarPrepareEnvironment(const std::function<bool()>& current) {\n    if(g_steamSettingsObserver.steamPresence()!=1)return "preparation-deferred";');if(prewarmBlocked===fixed)throw Error('Native prewarm negative anchor missing');fs.writeFileSync(path.join(out,'prewarm-negative.cpp'),prewarmBlocked);
 const wakePreparationMissing=fixed.replace('g_deckySidebarRuntime.requestPreparationRefresh(currentPowerGeneration());','');if(wakePreparationMissing===fixed)throw Error('Committed preparation negative anchor missing');fs.writeFileSync(path.join(out,'wake-preparation-negative.cpp'),wakePreparationMissing);
 const vc=process.env.VCToolsInstallDir||'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.44.35207\\',sdk=process.env.WindowsSdkDir||'C:\\Program Files (x86)\\Windows Kits\\10\\',version=process.env.WindowsSDKVersion||'10.0.26100.0\\';
 const environment=['@echo off','set "SystemRoot=C:\\Windows"','set "WINDIR=C:\\Windows"',`set "PATH=${path.join(vc,'bin/Hostx64/x64')};C:\\Windows\\System32;C:\\Windows"`,`set "INCLUDE=${path.join(vc,'include')};${['ucrt','shared','um','winrt','cppwinrt'].map(n=>path.join(sdk,'Include',version,n)).join(';')}"`,`set "LIB=${path.join(vc,'lib/x64')};${path.join(sdk,'Lib',version,'ucrt/x64')};${path.join(sdk,'Lib',version,'um/x64')}"`];
 const modes=['fixed','prewarm-negative','wake-preparation-negative','legacy','retained-negative','repair-uncertain-negative','repair-retained-negative','repair-auto-repeat-negative','repair-auto-ui-negative','repair-stale-mode-negative','wake-power-guard-negative','wake-binding-negative','wake-ready-event-negative'];
 // Independent consumers retain the original flags; compile four at a time, then link each fixture.
 const objectRoot=out.replaceAll('\\','/')+'/';
 const pchHeader=path.join(here,'native-pch.h'),pchSource=path.join(out,'native-pch.cpp'),pchImage=path.join(out,'native.pch');
 fs.writeFileSync(pchSource,'#include "native-pch.h"\n');
 const commonFlags='cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNOMINMAX /DUNICODE /D_UNICODE /I "'+path.join(root,'native')+'" /I "'+path.join(root,'deps/json')+'" /I "'+here+'"';
 const cmd=[...environment,commonFlags+' /Yc"native-pch.h" /Fp"'+pchImage+'" /Fo"'+path.join(out,'native-pch.obj')+'" /c "'+pchSource+'"','if errorlevel 1 exit /b 1',commonFlags+' /MP4 /Yu"native-pch.h" /FI"native-pch.h" /Fp"'+pchImage+'" /Fo"'+objectRoot+'" /c '+modes.map(mode=>'"'+path.join(out,mode+'.cpp')+'"').join(' '),'if errorlevel 1 exit /b 1'];
 for(const mode of modes)cmd.push(`link /nologo /OUT:"${path.join(out,mode+'.exe')}" "${path.join(out,mode+'.obj')}" "${path.join(out,'native-pch.obj')}" advapi32.lib user32.lib userenv.lib winhttp.lib ws2_32.lib bcrypt.lib`,'if errorlevel 1 exit /b 1');
 fs.writeFileSync(path.join(out,'build.cmd'),cmd.join('\r\n')+'\r\n');const nativeBuildStarted=performance.now();const build=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',path.join(out,'build.cmd')],{encoding:'utf8',windowsHide:true,timeout:90000,cwd:out});fs.writeFileSync(path.join(out,'build.log'),(build.stdout||'')+(build.stderr||''));report.nativeCompilation={status:build.status,error:build.error?.message??null,durationMs:performance.now()-nativeBuildStarted,originalBudgetMs:90000,commonHeader:{path:pchHeader,sha256:hash(pchHeader)},compilerRecipe:{path:path.join(out,'build.cmd'),sha256:hash(path.join(out,'build.cmd'))},modes:modes.length,precompiledHeaderOnly:true,independentVariantObjects:true};fs.writeFileSync(path.join(out,'native-compile-process.json'),JSON.stringify(report.nativeCompilation,null,2)+'\n');if(build.status!==0)throw Error('Native test compile failed: '+(build.stderr||build.stdout).slice(-1800));
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
}else if(mode==='repair-auto-ui-negative'){if(run.status!==1||!text.includes('automatic retained repair cannot rebuild Steam UI or consume manual launch consent'))throw Error('Removed automatic UI fence must fail at actual dispatch: '+text);report.automaticUiRefreshNegativeDetected=true;}else if(mode==='repair-stale-mode-negative'){if(run.status!==1||!text.includes('explicit owned repair must read current frontend instead of stale pre-load cache'))throw Error('Stale frontend cache negative must fail its actual explicit repair assertion: '+text);report.manualFreshModeRegressionDetected=true;}else if(mode==='repair-auto-repeat-negative'){if(run.status!==1||!text.includes('one automatic attempt per document survives duplicate events'))throw Error('Automatic repeat negative must fail its original assertion: '+text);report.automaticRepeatRegressionDetected=true;}else if(mode==='repair-uncertain-negative'){if(run.status!==1||!text.includes('exception after dispatch preserves uncertain mutation'))throw Error('Uncertain dispatch negative must fail its original assertion: '+text);report.manualRepairUncertaintyRegressionDetected=true;}else if(mode==='repair-retained-negative'){if(run.status!==1||!text.includes('retained bridge with absent owned backend permits explicit repair'))throw Error('Retained backend repair negative must fail its original assertion: '+text);report.manualRepairRetainedRegressionDetected=true;}else if(mode==='retained-negative'){if(run.status!==1||!text.includes('retained frontend must restore broker without official Loader'))throw Error('Removed retained path must fail at real broker restore: '+text);report.retainedRegressionDetected=true;}else{if(run.status!==1||!text.includes('field regression: recovery grant cannot reinject the loaded Steam context'))throw Error('Legacy must fail for the actual missing continuation: '+text);report.legacyRegressionDetected=true;}}
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
