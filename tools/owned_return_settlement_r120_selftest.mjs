import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { extract, sha } from './fixtures/owned_return_settlement_r120/source-binding.mjs';
import { reportText } from './fixtures/owned_return_settlement_r120/report.mjs';

// Bounded R120 settlement sidecar. The only executable is the inert fixture.
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const fixture=path.join(root,'tools/fixtures/owned_return_settlement_r120');
const output=path.resolve('G:/YeManCC-Work/Build/Validation/VirtualInput-ReturnClosure-R120-20261016/settlement-sidecar');
assert.equal(process.argv.length,2,'No arguments: output/source bounds are fixed');
const run=path.join(output,new Date().toISOString().replace(/[:.]/g,'-'));
fs.mkdirSync(path.join(run,'logs'),{recursive:true});
const save=(name,value)=>fs.writeFileSync(path.join(run,name),JSON.stringify(value,null,2)+'\n');
function command(label,exe,args,cwd=run) {
  const result=spawnSync(exe,args,{cwd,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});
  fs.writeFileSync(path.join(run,'logs',label+'.stdout.txt'),result.stdout??'');
  fs.writeFileSync(path.join(run,'logs',label+'.stderr.txt'),result.stderr??'');
  save('logs/'+label+'.command.json',{exe,args,cwd,status:result.status,error:result.error?String(result.error):null});
  return result;
}
const audit=command('steam-integration-audit',process.execPath,['tools/steam-integration-audit.mjs','--json'],root);
let inventory;try { inventory=JSON.parse(audit.stdout); } catch { inventory={parseFailed:true}; }
save('steam-integration-audit.json',inventory);
command('git-status-before','git',['status','--short'],root);
const production=['native/main.cpp','native/msi_owned_return_runtime.h','native/msi_owned_return_recovery.h',
  'native/msi_crash_recovery_runtime.h','native/msi_crash_recovery_journal.h','deps/json/json.hpp'];
const bytes=new Map(production.map(name=>[name,fs.readFileSync(path.join(root,name))]));
const snapshot=path.join(run,'snapshot');
for(const [name,content] of bytes) { const dest=path.join(snapshot,name);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,content); }
const fixtureFiles=['source-binding.mjs','inert.cpp.inc','signal-boundary.cpp.inc','cases.cpp.inc','report.mjs'];
const fixtureBytes=new Map(fixtureFiles.map(name=>[name,fs.readFileSync(path.join(fixture,name))]));
for(const [name,content] of fixtureBytes) { const dest=path.join(run,'fixture',name);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,content); }
fs.copyFileSync(fileURLToPath(import.meta.url),path.join(run,'fixture','owned_return_settlement_r120_selftest.mjs'));
const ranges=[],provenance=[];
const productionCode=(kind,name,file)=>{
  const code=extract(bytes.get(file).toString('utf8'),kind,name,file,ranges),binding=ranges.at(-1);
  return '\n#line '+binding.startLine+' "'+file+'"\n'+code+'\n';
};
const ownCode=name=>'\n#line 1 "fixture/'+name+'"\n'+fixtureBytes.get(name).toString('utf8').replace(/^\uFEFF/,'')+'\n';
let code=ownCode('inert.cpp.inc');
code+=productionCode('struct','InputHostVirtualIntentMutation','native/main.cpp');
code+=productionCode('enum','MsiReturnDebtBasis','native/main.cpp');
for(const name of ['msiReturnDebtBasisName','msiReturnDebtDischarge','inputHostExitFenceRaised'])code+=productionCode('function',name,'native/main.cpp');
for(const name of ['msiOwnedReturnPublish','msiOwnedReturnContext','msiOwnedReturnRecordCurrent'])code+=productionCode('function',name,'native/msi_owned_return_runtime.h');
for(const name of ['msiCrashStoreLocked','msiCrashRecoveryAfterModeDischarge'])code+=productionCode('function',name,'native/msi_crash_recovery_runtime.h');
code+=productionCode('function','msiDinputTryDischargeReturnDebt','native/main.cpp');
code+=productionCode('struct','MsiOwnedReturnSignal','native/msi_owned_return_runtime.h');
code+=ownCode('signal-boundary.cpp.inc');
code+=productionCode('function','msiOwnedReturnMaintenance','native/msi_owned_return_runtime.h');
code+=ownCode('cases.cpp.inc');
// Provenance only: these definitions establish that event mutations do not
// require the four held settlement locks. They are NOT executed or edited.
for(const name of ['inputHostRaiseExitFence','xinputSlotsMarkDiscoveryDirty','msiDinputPublishMainSourceIdentity','msiDinputRevokeMainSourceIdentity']) {
  const text=extract(bytes.get('native/main.cpp').toString('utf8'),'function',name,'native/main.cpp',provenance);
  fs.writeFileSync(path.join(run,'fixture',name+'.provenance.cpp.txt'),text+'\n');
}
const source=path.join(run,'settlement.cpp');fs.writeFileSync(source,code);
const sourceSHA=Object.fromEntries([...bytes].map(([name,value])=>[name,sha(value)]));
const binding={sourceRoot:root,sourceSHA,compiledDefinitions:ranges,mutationProvenance:provenance,
  fixtureSHA:Object.fromEntries([...fixtureBytes].map(([name,value])=>[name,sha(value)])),
  runnerSHA:sha(fs.readFileSync(fileURLToPath(import.meta.url))),translationUnitSHA:sha(code),
  extractionEdits:[],productionSettlementDefinitionsUnmodified:true,productionRecoveryPolicyAndJournalUnmodified:true,
  signalBoundary:'qualified signal and distinct sequences supplied; production ReadSignal deliberately excluded',
  persistenceBoundary:'sgWriteFileAtomic inert actor and in-memory serialized bytes; StoreLocked and AfterModeDischarge are real',
  limitations:['Atomic token changes alone do not establish a prior-debt contract violation','No OS file IO/fsync fault coverage','No ReadSignal identity/read-window coverage',
    'Atomics-only revocations; no impossible journal/episode replacement under held locks',
    'No product, Host, device, service, Steam, sleep, or power action']};
save('source-binding.json',binding);
const profiles={};
for(const profile of ['Release','Checked']) {
  const dir=path.join(run,profile);fs.mkdirSync(dir);
  const exe=path.join(dir,'settlement-selftest.exe');
  const quote=s=>{assert(!/["\r\n%]/.test(s));return '"'+s+'"';};
  const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
  assert(fs.existsSync(vcvars),'MSVC x64 toolchain unavailable');
  const flags=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-','/sdl','/Zc:__cplusplus',
    ...(profile==='Release'?['/O2','/MT','/DNDEBUG']:['/Od','/MTd','/RTC1','/Zi','/D_DEBUG']),
    '/external:I'+quote(path.join(snapshot,'deps/json')),'/external:W0','/I'+quote(path.join(snapshot,'native')),
    quote(source),'/Fo'+quote(path.join(dir,'fixture.obj')),'/Fe'+quote(exe),'/Fd'+quote(path.join(dir,'fixture.pdb')),
    '/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64','/INCREMENTAL:NO'];
  const batch=path.join(dir,'build.cmd');
  fs.writeFileSync(batch,'@echo off\r\ncall '+quote(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl '+flags.join(' ')+'\r\nexit /b %errorlevel%\r\n');
  const build=command(profile+'-build','cmd.exe',['/d','/c',batch],dir);
  if(build.status!==0) { profiles[profile]={buildPassed:false,passed:false};console.error(build.stdout,build.stderr);continue; }
  const execution=command(profile+'-execute',exe,[],dir);
  let result;try {result=JSON.parse(execution.stdout);} catch {result={passed:false,output:execution.stdout};}
  save(profile+'/results.json',result);
  profiles[profile]={buildPassed:true,passed:execution.status===0&&result.harnessPassed===true,caseCount:result.caseCount,
    assertions:result.assertions,duringStoreObservations:result.duringStoreObservations,verdict:result.verdict,settlementSafetyPassed:result.settlementSafetyPassed,
    executableSHA:sha(fs.readFileSync(exe))};
  console.log(profile+': '+JSON.stringify(profiles[profile]));
}
const changedFiles=production.filter(name=>sha(fs.readFileSync(path.join(root,name)))!==sourceSHA[name]);
const relevantChanged=[];
for(const item of ranges) {
  try {const current=extract(fs.readFileSync(path.join(root,item.file),'utf8'),item.kind,item.symbol,item.file,[]);
    if(sha(current)!==item.sha256)relevantChanged.push(item.file+'::'+item.symbol);
  }catch {relevantChanged.push(item.file+'::'+item.symbol);}
}
for(const file of ['native/msi_owned_return_recovery.h','native/msi_crash_recovery_journal.h','deps/json/json.hpp'])
  if(changedFiles.includes(file))relevantChanged.push(file);
const result={interpretation:"Historical prior-debt settlement observed; no persistent-contract violation proven. See FINAL-REVIEW.md.",createdAt:new Date().toISOString(),run,passed:Object.values(profiles).length===2&&Object.values(profiles).every(p=>p.passed)&&relevantChanged.length===0,
  verdict:Object.values(profiles).length===2&&Object.values(profiles).every(p=>p.passed&&p.duringStoreObservations===9)?'OBSERVED_PRIOR_DEBT_SETTLEMENT_DURING_STORE_EVENTS':'VALIDATION_INCOMPLETE',
  settlementSafetyPassed:null,sourceSHA,profiles,changedFilesDuringRun:changedFiles,
  relevantDefinitionsChangedDuringRun:relevantChanged,audit:{status:audit.status,missingAnchors:inventory.missingAnchors,unmappedCandidates:inventory.unmappedCandidates},
  writes:{production:[],existingTests:[],newSource:['tools/owned_return_settlement_r120_selftest.mjs',...fixtureFiles.map(name=>'tools/fixtures/owned_return_settlement_r120/'+name)],evidenceRoot:output},
  limitations:binding.limitations};
save('RESULT.json',result);
const report=reportText(result,binding);
fs.writeFileSync(path.join(run,'REPORT.md'),report);
fs.writeFileSync(path.join(output,'REPORT.md'),report);
if(profiles.Release?.buildPassed) {
  const cases=JSON.parse(fs.readFileSync(path.join(run,'Release/results.json'),'utf8')).cases;
  save('MINIMAL-REPRO.json',cases.filter(c=>['observation/durable-success/topology','control/durable-failure/topology','control/before-settlement/topology'].includes(c.name)));
}
command('git-status-after','git',['status','--short'],root);
fs.writeFileSync(path.join(output,'LATEST.json'),JSON.stringify(result,null,2)+'\n');
console.log('Evidence: '+run);
process.exitCode=result.passed?0:1;


