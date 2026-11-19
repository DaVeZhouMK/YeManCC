import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.argv[2]||'');
const options=process.argv.slice(3);
if(!process.argv[2]||options.length>1||(options.length===1&&options[0]!=='--wait-only'))throw Error('usage: node tools/steam_game_link_wait_policy_selftest.mjs <evidence-dir> [--wait-only]');
fs.mkdirSync(out,{recursive:true});
const file='native/steam_game_link_runtime.h',src=fs.readFileSync(file,'utf8').replace(/\r\n/g,'\n');
const section=(a,b)=>{const p=src.indexOf(a),q=src.indexOf(b,p);if(p<0||q<0)throw Error('production section missing '+a);return src.slice(p,q);};
const waitConsumer=section('        HANDLE waits[5]','    }catch(...){').replace('break;','return false;')+'\nreturn true;\n';
fs.writeFileSync(path.join(out,'runtime_wait.inc'),waitConsumer);
fs.writeFileSync(path.join(out,'runtime_directory.inc'),section('        if(waitPolicy.directoryReady(now))','        revokedOriginals.erase'));
fs.writeFileSync(path.join(out,'runtime_observe.inc'),'return '+section('if(state.active()&&(','            forceObserve=false;').replace(/^if\(/,'').replace(/\)\{\s*$/,'')+';\n');
const retirement=section('            if(!s.active){currentRecordPre=', '        }\n        HANDLE waits[5]');
const successor='pending=e.enabled;idleDue=e.tick;';
if(!retirement.includes(successor))throw Error('Missing production successor scheduling');
fs.writeFileSync(path.join(out,'runtime_link_retirement.inc'),retirement);
const startupScopeInit=section('    std::optional<std::array<unsigned long long,2>> startupCaptureScope;', '    unsigned long long startupCapturePower');
const startupScope=section('        // The window hook may already have retained evidence in the first scope.', '        // Coalesce only after a directory signal.');
const startupWatch=section('                if(observed.pid&&observed.created&&!observed.root.empty()&&observed.root!=watchedRoot){','                if(startupCapturePending&&');
const startupIncludes={
    'runtime_startup_scope_init.inc':startupScopeInit,
    'runtime_startup_scope.inc':startupScope,
    'runtime_startup_watch.inc':startupWatch,
};
for(const [name,body] of Object.entries(startupIncludes))fs.writeFileSync(path.join(out,name),body);
const idleDiscovery=section('static void discoverIdleGame(', 'static void run(');
fs.writeFileSync(path.join(out,'runtime_idle_discovery.inc'),idleDiscovery);
const baseIncludes=['runtime_wait.inc','runtime_directory.inc','runtime_observe.inc','runtime_link_retirement.inc','runtime_idle_discovery.inc',...Object.keys(startupIncludes)];
const vc='C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
const tests=['steam_game_link_wait_policy_selftest'];
if(!options.length)tests.push('steam_game_link_state_selftest');
const commands=['@echo off',`call "${vc}" >nul`,'if errorlevel 1 exit /b 1'];
const sdk='C:/Program Files (x86)/Windows Kits/10';
for(const t of tests){commands.push(`cl /nologo /std:c++17 /EHsc /utf-8 /W4 /I"${sdk}/Include/10.0.26100.0/um" /I"${sdk}/Include/10.0.26100.0/shared" /I"${sdk}/Include/10.0.26100.0/ucrt" /I"${out}" "${path.join(root,'tools',t+'.cpp')}" /Fo"${path.join(out,t+'.obj')}" /Fe"${path.join(out,t+'.exe')}" /link /LIBPATH:"${sdk}/Lib/10.0.26100.0/um/x64" /LIBPATH:"${sdk}/Lib/10.0.26100.0/ucrt/x64"`,'if errorlevel 1 exit /b 1',`"${path.join(out,t+'.exe')}"`,'if errorlevel 1 exit /b 1');}
fs.writeFileSync(path.join(out,'run-tests.cmd'),commands.join('\r\n')+'\r\n');
const p=spawnSync('cmd.exe',['/d','/c',path.join(out,'run-tests.cmd')],{cwd:out,encoding:'utf8',windowsHide:true});
fs.writeFileSync(path.join(out,'tests.log'),(p.stdout||'')+(p.stderr||''));
const negativeDir=path.join(out,'negative-console-urgent');fs.mkdirSync(negativeDir,{recursive:true});
for(const name of baseIncludes.filter(name=>name!=='runtime_directory.inc'))fs.copyFileSync(path.join(out,name),path.join(negativeDir,name));
const directoryConsumer=fs.readFileSync(path.join(out,'runtime_directory.inc'),'utf8');
if(!directoryConsumer.includes('else if(consoleChanged)waitPolicy.backgroundChanged(now);'))throw Error('Missing production console background dispatch');
fs.writeFileSync(path.join(negativeDir,'runtime_directory.inc'),directoryConsumer.replace('else if(consoleChanged)waitPolicy.backgroundChanged(now);','else if(consoleChanged)waitPolicy.relevantChanged(now);'));
const negativeCommands=[...commands.slice(0,3),commands[3].replaceAll(out,negativeDir),'if errorlevel 1 exit /b 2',`"${path.join(negativeDir,'steam_game_link_wait_policy_selftest.exe')}"`,'exit /b %errorlevel%'];
fs.writeFileSync(path.join(negativeDir,'run-negative.cmd'),negativeCommands.join('\r\n')+'\r\n');
const negative=spawnSync('cmd.exe',['/d','/c',path.join(negativeDir,'run-negative.cmd')],{cwd:negativeDir,encoding:'utf8',windowsHide:true});
const negativeLog=(negative.stdout||'')+(negative.stderr||'');fs.writeFileSync(path.join(negativeDir,'tests.log'),negativeLog);
const negativeDetected=negative.status===1&&negativeLog.includes('FAIL console-only read interval at least 10s');
const droppedDir=path.join(out,'negative-drop-successor');fs.mkdirSync(droppedDir,{recursive:true});
for(const name of baseIncludes.filter(name=>name!=='runtime_link_retirement.inc'))fs.copyFileSync(path.join(out,name),path.join(droppedDir,name));
fs.writeFileSync(path.join(droppedDir,'runtime_link_retirement.inc'),retirement.replace(successor,'pending=false;'));
const droppedCommands=[...commands.slice(0,3),commands[3].replaceAll(out,droppedDir),'if errorlevel 1 exit /b 2',`"${path.join(droppedDir,'steam_game_link_wait_policy_selftest.exe')}" --retirement-only`,'exit /b %errorlevel%'];
fs.writeFileSync(path.join(droppedDir,'run-negative.cmd'),droppedCommands.join('\r\n')+'\r\n');
const dropped=spawnSync('cmd.exe',['/d','/c',path.join(droppedDir,'run-negative.cmd')],{cwd:droppedDir,encoding:'utf8',windowsHide:true});
const droppedLog=(dropped.stdout||'')+(dropped.stderr||'');fs.writeFileSync(path.join(droppedDir,'tests.log'),droppedLog);
const droppedDetected=dropped.status===1&&droppedLog.includes('FAIL retirement must preserve one discovery without another hint');
function excerptControl(name,file,source,from,to,mode,expected){
    const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});
    for(const include of baseIncludes)fs.copyFileSync(path.join(out,include),path.join(dir,include));
    if(!source.includes(from))throw Error('Missing control source '+name);
    fs.writeFileSync(path.join(dir,file),source.replace(from,to));
    const run=[...commands.slice(0,3),commands[3].replaceAll(out,dir),'if errorlevel 1 exit /b 2',
        `"${path.join(dir,'steam_game_link_wait_policy_selftest.exe')}" ${mode}`,'exit /b %errorlevel%'];
    fs.writeFileSync(path.join(dir,'run-negative.cmd'),run.join('\r\n')+'\r\n');
    const result=spawnSync('cmd.exe',['/d','/c',path.join(dir,'run-negative.cmd')],{cwd:dir,encoding:'utf8',windowsHide:true});
    const log=(result.stdout||'')+(result.stderr||'');fs.writeFileSync(path.join(dir,'tests.log'),log);
    const detected=result.status===1&&log.includes('FAIL '+expected);
    console.log(detected?'PASS negative '+name:log);
    return {status:result.status,detected};
}
const negativeInitialScope=excerptControl('negative-first-scope-retire','runtime_startup_scope.inc',startupScope,
    'if(startupCaptureScope&&captureScope!=*startupCaptureScope)', 'if(!startupCaptureScope||captureScope!=*startupCaptureScope)',
    '--startup-only','first startup scope does not retire early window evidence');
const negativeInitialWatch=excerptControl('negative-first-watch-retire','runtime_startup_watch.inc',startupWatch,
    'if(directory.handle())closeDirectory();','closeDirectory();','--startup-only','first watch enrollment does not retire early window evidence');
const negativeIdleWindow=excerptControl('negative-immediate-idle-window','runtime_wait.inc',waitConsumer,
    'if(pendingHints==waitpolicy::Window&&','if(false&&pendingHints==waitpolicy::Window&&',
    '--idle-window-only','qualified idle window storm allows one audit and forwards all hints');
const negativeIdleDiscovery=excerptControl('negative-ungated-idle-discovery','runtime_idle_discovery.inc',idleDiscovery,
    'qualifiedEmpty||!nativeValveReadCachedSelection(&targetHint)','qualifiedEmpty||(!nativeValveReadCachedSelection(&targetHint)&&false)',
    '--idle-discovery-only','no selected target skips Steam round trip without proving absence');
const status=p.status===0&&negativeDetected&&droppedDetected&&negativeInitialScope.detected&&negativeInitialWatch.detected&&negativeIdleWindow.detected&&negativeIdleDiscovery.detected?0:1;
const inputs=[file,'native/steam_game_link_state.h','native/steam_game_link_wait_policy.h','native/steam_game_link_log_watch.h',...tests.map(t=>'tools/'+t+'.cpp'),'tools/steam_game_link_wait_policy_selftest.mjs'].map(f=>({path:path.join(root,f),sha256:crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')}));
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({status,positiveStatus:p.status,tests,negativeConsoleUrgent:{status:negative.status,detected:negativeDetected},negativeDropSuccessor:{status:dropped.status,detected:droppedDetected},negativeInitialScope,negativeInitialWatch,negativeIdleWindow,negativeIdleDiscovery,inputs,method:'Production wait/directory/predicate/link retirement/startup enrollment and idle discovery excerpts; break converted to return at function boundary; virtual clock, mocked discovery and directory stamps, real Windows event handles. Negative controls restore prior behaviors individually. No application/Steam/game/sleep run.'},null,2));
console.log(p.stdout||'');console.error(p.stderr||'');console.log(negativeDetected?'PASS negative console-urgent control rejected by 10s interval assertion':negativeLog);console.log(droppedDetected?'PASS negative old link retirement loses discovery':droppedLog);process.exit(status);
