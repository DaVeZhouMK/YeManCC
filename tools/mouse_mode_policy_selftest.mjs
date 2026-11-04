import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv.at(-1));fs.mkdirSync(out,{recursive:true});
const inputs=['native/main.cpp','native/game_application_catalog.h','native/game_library_blacklist_entries.h','native/mouse_mode_application_rules.h','tools/fixtures/legion_motion_consumer/extract_main.mjs','native/mouse_mode_policy.h','native/mouse_mode_foreground.h','native/mouse_mode_foreground_rules.h','tools/mouse_mode_policy_selftest.cpp','deps/json/json.hpp'];const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');const hashes=Object.fromEntries(inputs.map(f=>[f,hash(path.join(root,f))]));
const prefix=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');const results=[];
for(const variant of ['Checked','Optimized','desktop-guard-removed','old-shell-class-filter','stale-off-publication','old-desktop-only','category-guards-removed']){
 const dir=path.join(out,variant);fs.mkdirSync(dir,{recursive:true});
 let header=fs.readFileSync(path.join(root,'native/mouse_mode_policy.h'),'utf8');if(variant==='desktop-guard-removed')header=header.replace('if(!c.window)return {false,"unknown","foreground_unconfirmed"};','if(!c.window)return {true,"desktop","foreground_unconfirmed"};');
 if(variant==='stale-off-publication')header=header.replace('if(backendEpoch.load()!=expectedEpoch)return 0;','/* removed readback epoch fence */');
 if(variant==='old-desktop-only')header=header.replace('if(c.identityKnown&&!c.foregroundImage.empty())return {true,"desktop","non_game_foreground"};','/* old desktop-only admission */');
 if(variant==='category-guards-removed')header=header.replace('if(c.emulator)return {false,"emulator","emulator_foreground"};','').replace('if(c.gameLibrary)return {false,"game-library","game_library_foreground"};','');
 fs.writeFileSync(path.join(dir,'mouse_mode_policy.h'),header);
 let rules=fs.readFileSync(path.join(root,'native/mouse_mode_foreground_rules.h'),'utf8');
 if(variant==='old-shell-class-filter')rules=rules.replace('if(image==windows+L"\\\\explorer.exe")return true;','if(image==windows+L"\\\\explorer.exe")return windowClass==L"Progman"||windowClass==L"Shell_TrayWnd";');
 fs.writeFileSync(path.join(dir,'mouse_mode_foreground_rules.h'),rules);
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 fs.writeFileSync(path.join(dir,'mouse_mode_application_consumer.inc'),extract(main,'mouseModeObserveForeground').code);
 fs.writeFileSync(path.join(dir,'mouse_policy_set_consumer.inc'),['mousePolicySet','mousePolicyCycle'].map(name=>extract(main,name).code).join('\n'));
 fs.writeFileSync(path.join(dir,'mouse_auto_rules_consumer.inc'),['mouseAutoRulesParse','mouseAutoRulesSnapshot','mouseAutoRulesSet'].map(name=>extract(main,name).code).join('\n'));
 fs.copyFileSync(path.join(root,'native/mouse_mode_application_rules.h'),path.join(dir,'mouse_mode_application_rules.h'));
 const cmd=prefix+'set "CL="\r\nset "_CL_="\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 '+(variant==='Checked'?'/Od /RTC1 /MDd':'/O2 /MD')+' /I"'+dir+'" /I"'+root+'/native" /I"'+root+'/deps/json" "'+root+'/tools/mouse_mode_policy_selftest.cpp" /Fo"'+dir+'/test.obj" /Fe"'+dir+'/test.exe"\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(path.join(dir,'compile.cmd'),cmd);
 const build=spawnSync('cmd.exe',['/d','/c',path.join(dir,'compile.cmd')],{encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(dir,'build.log'),build.stdout+'\n'+build.stderr);assert.equal(build.status,0,'Policy compilation failed: '+dir);
 const run=spawnSync(path.join(dir,'test.exe'),[],{encoding:'utf8',windowsHide:true,timeout:25000});fs.writeFileSync(path.join(dir,'execution.log'),run.stdout+'\n'+run.stderr);const report=JSON.parse(run.stdout);
 if(['desktop-guard-removed','old-shell-class-filter','stale-off-publication','old-desktop-only','category-guards-removed'].includes(variant)){assert.equal(run.status,1);assert.equal(report.error,variant==='old-desktop-only'?'ordinary-browser-with-no-game-enables':variant==='category-guards-removed'?'shared-emulator-profile-blocked-before-game-threshold':variant==='stale-off-publication'?'inflight-retained-off-must-not-overwrite-new-readback':variant==='old-shell-class-filter'?'logged-explorer-popup-remains-desktop':'null-foreground-never-desktop');}else {assert.equal(run.status,0,JSON.stringify(report));assert.equal(report.status,'PASS');}
 results.push({variant,expectedRejection:['desktop-guard-removed','old-shell-class-filter','stale-off-publication','old-desktop-only','category-guards-removed'].includes(variant),...report});
}
for(const [f,h]of Object.entries(hashes))assert.equal(hash(path.join(root,f)),h,'Input changed: '+f);
fs.writeFileSync(path.join(out,'mouse-policy-result.json'),JSON.stringify({status:'PASS',sourceHashes:hashes,results},null,2));console.log(JSON.stringify({status:'PASS',results},null,2));
