import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const args=process.argv.slice(2),positional=[];let selected=null;
for(let i=0;i<args.length;++i){
 if(args[i]==='--selected'){assert.equal(selected,null,'Duplicate selection');selected=args[++i];assert.equal(selected,'release-retry','Unknown mouse policy selection');}
 else {assert.ok(!args[i].startsWith('--'),'Unknown mouse policy option: '+args[i]);positional.push(args[i]);}
}
assert.equal(positional.length,1,'Expected one evidence directory');
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(positional[0]);fs.mkdirSync(out,{recursive:true});
const inputs=['native/main.cpp','native/game_application_catalog.h','native/game_library_blacklist_entries.h','native/mouse_mode_application_rules.h','tools/fixtures/legion_motion_consumer/extract_main.mjs','native/mouse_mode_policy.h','native/mouse_mode_foreground.h','native/mouse_mode_foreground_rules.h','tools/mouse_mode_policy_selftest.cpp','deps/json/json.hpp'];const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');const hashes=Object.fromEntries(inputs.map(f=>[f,hash(path.join(root,f))]));
const prefix=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');const results=[];
const variants=selected?['Checked','Optimized','release-retry-disabled','release-retry-no-backoff','release-retry-no-context-check','stale-off-publication']:['Checked','Optimized','desktop-guard-removed','old-shell-class-filter','stale-off-publication','old-desktop-only','category-guards-removed'];
for(const variant of variants){
 const dir=path.join(out,variant);fs.mkdirSync(dir,{recursive:true});
 let header=fs.readFileSync(path.join(root,'native/mouse_mode_policy.h'),'utf8');if(variant==='desktop-guard-removed')header=header.replace('if(!c.window)return {false,"unknown","foreground_unconfirmed"};','if(!c.window)return {true,"desktop","foreground_unconfirmed"};');
 if(variant==='stale-off-publication')header=header.replace('if(backendEpoch.load()!=expectedEpoch)return 0;','/* removed readback epoch fence */');
 if(variant==='old-desktop-only')header=header.replace('if(c.identityKnown&&!c.foregroundImage.empty())return {true,"desktop","non_game_foreground"};','/* old desktop-only admission */');
 if(variant==='category-guards-removed')header=header.replace('if(c.emulator)return {false,"emulator","emulator_foreground"};','').replace('if(c.gameLibrary)return {false,"game-library","game_library_foreground"};','');
 const mutate=(before,after)=>{assert.equal(header.split(before).length,2,'Missing or ambiguous mutation anchor: '+variant);header=header.replace(before,after);};
 if(variant==='release-retry-disabled')mutate('if(!refresh&&!first&&context==previous&&m==priorMode&&!releasePending)','if(!refresh&&!first&&context==previous&&m==priorMode)');
 if(variant==='release-retry-no-backoff')mutate('if(releasePending&&retryNow()<releaseDue)','if(false)');
 if(variant==='release-retry-no-context-check')mutate('if(!current()||backendEpoch.load()!=readEpoch)','if(backendEpoch.load()!=readEpoch)');
 fs.writeFileSync(path.join(dir,'mouse_mode_policy.h'),header);
 let rules=fs.readFileSync(path.join(root,'native/mouse_mode_foreground_rules.h'),'utf8');
 if(variant==='old-shell-class-filter')rules=rules.replace('if(image==windows+L"\\\\explorer.exe")return true;','if(image==windows+L"\\\\explorer.exe")return windowClass==L"Progman"||windowClass==L"Shell_TrayWnd";');
 fs.writeFileSync(path.join(dir,'mouse_mode_foreground_rules.h'),rules);
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 fs.writeFileSync(path.join(dir,'mouse_mode_application_consumer.inc'),extract(main,'mouseModeObserveForeground').code);
 fs.writeFileSync(path.join(dir,'mouse_policy_set_consumer.inc'),['mouseModeRequestExpired','mousePolicySet','mousePolicyCycle'].map(name=>extract(main,name).code).join('\n'));
 fs.writeFileSync(path.join(dir,'mouse_auto_rules_consumer.inc'),['mouseAutoRulesParse','mouseAutoRulesSnapshot','mouseAutoRulesSet'].map(name=>extract(main,name).code).join('\n'));
 fs.copyFileSync(path.join(root,'native/mouse_mode_application_rules.h'),path.join(dir,'mouse_mode_application_rules.h'));
 const cmd=prefix+'set "CL="\r\nset "_CL_="\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 '+(variant==='Checked'?'/Od /RTC1 /MDd':'/O2 /MD')+' /I"'+dir+'" /I"'+root+'/native" /I"'+root+'/deps/json" "'+root+'/tools/mouse_mode_policy_selftest.cpp" /Fo"'+dir+'/test.obj" /Fe"'+dir+'/test.exe"\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(path.join(dir,'compile.cmd'),cmd);
 const build=spawnSync('cmd.exe',['/d','/c',path.join(dir,'compile.cmd')],{encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(dir,'build.log'),build.stdout+'\n'+build.stderr);assert.equal(build.status,0,'Policy compilation failed: '+dir);
 const scenario=variant==='stale-off-publication'?'readback-epoch-fence':variant==='release-retry-no-context-check'?'current-context-and-mode':'stable-game-provider-recovers';
 const selection=selected?(variant==='Checked'||variant==='Optimized'?selected:'release-retry:'+scenario):null;
 const runArgs=selection?['--selected',selection]:[];
 const run=spawnSync(path.join(dir,'test.exe'),runArgs,{encoding:'utf8',windowsHide:true,timeout:25000});fs.writeFileSync(path.join(dir,'execution.log'),run.stdout+'\n'+run.stderr);const report=JSON.parse(run.stdout);
 const rejection=selected?{'release-retry-disabled':'release-retry-stable-game-recovers-without-context-change','release-retry-no-backoff':'release-retry-never-runs-before-backoff','release-retry-no-context-check':'release-retry-reobserves-before-off-dispatch','stale-off-publication':'release-retry-old-result-cannot-overwrite-new-readback-epoch'}:{'desktop-guard-removed':'null-foreground-never-desktop','old-shell-class-filter':'logged-explorer-popup-remains-desktop','stale-off-publication':'inflight-retained-off-must-not-overwrite-new-readback','old-desktop-only':'ordinary-browser-with-no-game-enables','category-guards-removed':'shared-emulator-profile-blocked-before-game-threshold'};
 if(rejection[variant]){assert.equal(run.status,1);assert.equal(report.error,rejection[variant]);}else {assert.equal(run.status,0,JSON.stringify(report));assert.equal(report.status,'PASS');if(selection)assert.equal(report.selection,selection);}
 results.push({variant,selection,expectedRejection:!!rejection[variant],...report});
}
for(const [f,h]of Object.entries(hashes))assert.equal(hash(path.join(root,f)),h,'Input changed: '+f);
fs.writeFileSync(path.join(out,'mouse-policy-result.json'),JSON.stringify({status:'PASS',selection:selected,sourceHashes:hashes,results},null,2));console.log(JSON.stringify({status:'PASS',selection:selected,results},null,2));
