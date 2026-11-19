import fs from'node:fs';import path from'node:path';import assert from'node:assert/strict';import{spawnSync}from'node:child_process';import{fileURLToPath}from'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
function prepareForegroundSources(io){
 const main=io.readFileSync(path.join(root,'src/bridge/deckyMirrorHost.ts'),'utf8'),trainer=io.readFileSync(path.join(root,'src/bridge/gameTrainer.ts'),'utf8');assert(main.includes('isTargetCurrent,processTarget:game,interactiveShell:true'));assert(trainer.includes("if (interactiveShell) args.push('-InteractiveShell')"));
 io.readFileSync(path.join(root,'tools/game_trainer_foreground_selftest.ps1'));
 const files=io.readdirSync(path.join(root,'PowerControl/Trainer')).filter(name=>name.endsWith('.ps1')||name.endsWith('.psm1')).map(name=>({relative:'Trainer/'+name,bytes:io.readFileSync(path.join(root,'PowerControl/Trainer',name))}));
 for(const name of ['TrainerBackend.psm1','TrainerGcmFallback.ps1','TrainerCatalog.ps1','TrainerIdentity.ps1','TrainerLaunchPaths.ps1','TrainerLaunch.ps1','TrainerForeground.ps1'])assert(files.some(file=>file.relative==='Trainer/'+name),'Trainer foreground helper missing: '+name);
 for(const name of ['GameTitlePolicy.ps1','GameTitlePolicy.json'])files.push({relative:name,bytes:io.readFileSync(path.join(root,'PowerControl',name))});
 const launch=files.find(file=>file.relative==='Trainer/TrainerLaunch.ps1').bytes.toString('utf8'),anchor='try{Assert-TrainerLaunchContinuation $Plan;return Invoke-TrainerShellPlan $Plan $desktop}finally';assert(launch.includes(anchor));
 return {files,negativeLaunch:launch.replace(anchor,anchor.replace('}finally','}catch{}finally'))};
}
export async function trainerForegroundTests(out,{io=fs,producer=spawnSync}={}){
 const prepared=prepareForegroundSources(io);
 io.mkdirSync(out,{recursive:true});const powershell=path.join(process.env.WINDIR||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe');
 const run=producer(powershell,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools/game_trainer_foreground_selftest.ps1'),'-ValidationRoot',out],{cwd:root,windowsHide:true,encoding:'utf8',timeout:20000});io.writeFileSync(path.join(out,'powershell.log'),(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,run.stdout+run.stderr);
 const native=JSON.parse(io.readFileSync(path.join(out,'trainer-foreground-unit.json'),'utf8').replace(/^\uFEFF/,''));assert.equal(native.failed,0);
 const negative=path.join(out,'uncertain-fallback-negative'),pc=path.join(negative,'PowerControl');
 io.mkdirSync(path.join(pc,'Trainer'),{recursive:true});
 for(const file of prepared.files)io.writeFileSync(path.join(pc,file.relative),file.bytes);
 io.writeFileSync(path.join(pc,'Trainer/TrainerLaunch.ps1'),prepared.negativeLaunch);
 const neg=producer(powershell,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools/game_trainer_foreground_selftest.ps1'),'-ValidationRoot',negative,'-BackendPath',path.join(pc,'Trainer/TrainerBackend.psm1')],{cwd:root,windowsHide:true,encoding:'utf8',timeout:20000});io.writeFileSync(path.join(out,'uncertain-negative.log'),(neg.stdout||'')+(neg.stderr||''));assert.equal(neg.status,1,'uncertain fallback negative must fail');const negReport=JSON.parse(io.readFileSync(path.join(negative,'trainer-foreground-unit.json'),'utf8').replace(/^\uFEFF/,''));assert(negReport.checks.some(c=>c.name==='uncertain Shell reply never falls through to a second Process.Start'&&!c.ok),'original uncertain dispatch assertion must detect replay fallback');
 const report={passed:true,uncertainFallbackNegativeDetected:true,nativeShellCases:native.passedChecks,sourceWiringChecks:2,realProcessLaunches:0,realRegistryWrites:0,realSteamActions:0,physicalFseAcceptance:false};io.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));return report;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){const i=process.argv.indexOf('--out');console.log(JSON.stringify(await trainerForegroundTests(i<0?(process.argv[2]?path.resolve(process.argv[2]):path.join(root,'Build/Validation/TrainerForeground')):path.resolve(process.argv[i+1]))));}
