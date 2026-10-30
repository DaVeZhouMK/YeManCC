import {selectedNativeTools} from './ymcc_sim/global/release-host-environment.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const repo=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]??'');assert(process.argv[2]&&!fs.existsSync(out),'Use a fresh evidence directory.');fs.mkdirSync(out,{recursive:true});
const source=path.join(out,'source'),pins=[],seen=new Set();const sha=b=>crypto.createHash('sha256').update(b).digest('hex');const search=[path.join(repo,'native'),path.join(repo,'deps/json'),repo];
function freeze(file){file=path.resolve(file);if(seen.has(file.toLowerCase()))return;const relative=path.relative(repo,file);if(relative.startsWith('..')||path.isAbsolute(relative))return;seen.add(file.toLowerCase());const bytes=fs.readFileSync(file),target=path.join(source,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);assert.equal(sha(fs.readFileSync(file)),sha(bytes),'Concurrent input change: '+relative);pins.push({path:relative.replaceAll('\\','/'),bytes:bytes.length,sha256:sha(bytes)});for(const m of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){const found=[path.resolve(path.dirname(file),m[1]),...search.map(p=>path.resolve(p,m[1]))].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(found)freeze(found);}}
freeze(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'));freeze(path.join(repo,'tools/decky_binding_runtime_selftest.mjs'));freeze(path.join(repo,'deps/json/json.hpp'));
// Static event binding only: main.cpp is frozen for attribution, never run.
const mainFile=path.join(repo,'native/main.cpp'),mainBytes=fs.readFileSync(mainFile),mainText=mainBytes.toString('utf8');
const commit=mainText.match(/case WM_POWER_RESUME_COMMIT:[\s\S]*?(?=\n\s*case WM_POWER_INPUT_RECOVER:)/);
assert(commit,'Missing WM_POWER_RESUME_COMMIT block');
const call='g_deckySidebarRuntime.requestBindingRefresh(currentPowerGeneration())';
assert.equal(commit[0].split(call).length-1,1,'Committed resume must bind the existing production consumer exactly once');
assert(commit[0].includes('PowerLifecycle::Ready'),'Committed resume must retain the Ready guard');
const consumer={path:'native/main.cpp',sourceSha256:sha(mainBytes),event:'WM_POWER_RESUME_COMMIT',call,callLine:mainText.slice(0,commit.index+commit[0].indexOf(call)).split('\n').length,excerptSha256:sha(Buffer.from(commit[0])),verification:'static call binding; OS event dispatch not executed'};
// Execute the exact Decky-only consumer from the alternate S0 Ready path too.
const intent=mainText.match(/static bool deckySidebarResumeAfterIntent\(unsigned long long generation\) \{[\s\S]*?\n\}/);
assert(intent,'Missing alternate S0 Ready consumer');
const abort=mainText.slice(mainText.indexOf('static void sgAbortSleepIntent(const char* reason) {'),mainText.indexOf('// Idle owns no periodic wakeup.',mainText.indexOf('static void sgAbortSleepIntent(const char* reason) {')));
assert.equal(abort.split('deckySidebarResumeAfterIntent(generation)').length-1,1,'Actual S0 Ready path must notify Decky once');
assert(abort.indexOf('g_powerLifecycle.store(PowerLifecycle::Ready')<abort.indexOf('deckySidebarResumeAfterIntent(generation)'),'Decky may only consume committed Ready');
let intentText=intent[0];const override=process.env.YMCC_DECKY_WAKE_READY_OVERRIDE;
if(override){intentText=fs.readFileSync(override,'utf8');consumer.intentOverride={path:override,sha256:sha(Buffer.from(intentText)),scope:'explicit negative variant'};}
fs.writeFileSync(path.join(source,'native/decky_wake_ready_consumer.inc'),intentText+'\n');
consumer.alternateReady={path:'native/main.cpp',function:'deckySidebarResumeAfterIntent',sourceSha256:sha(Buffer.from(intent[0])),actualCallIn:'sgAbortSleepIntent after real Ready',execution:'Actual extracted function; explicit process/session IO adapters'};
fs.writeFileSync(path.join(source,'native/main.cpp'),mainBytes);pins.push({path:'native/main.cpp',bytes:mainBytes.length,sha256:sha(mainBytes),role:'static event consumer attribution; not compiled'});
fs.writeFileSync(path.join(out,'RESUME-CONSUMER.json'),JSON.stringify(consumer,null,2)+'\n');fs.writeFileSync(path.join(out,'RESUME-CONSUMER.cpp.txt'),commit[0]+'\n');
fs.writeFileSync(path.join(out,'SOURCE-PINS.json'),JSON.stringify({repo,source,files:pins},null,2));
const tools=selectedNativeTools().native;
const env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));env.PATH=path.dirname(tools.compiler)+';'+process.env.SystemRoot+'/System32';env.INCLUDE=tools.include.join(';');env.LIB=tools.lib.join(';');env.CL='';env._CL_='';env.LINK='';env._LINK_='';env.TEMP=path.join(out,'tmp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
const root=path.join(out,'FixtureSteam');fs.mkdirSync(root);const exe=path.join(root,'steam.exe'),object=path.join(out,'binding-client.obj');
const args=['/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(source,'native'),'/I'+path.join(source,'deps/json'),path.join(source,'tools/decky_binding_runtime_selftest.cpp'),'/sourceDependencies',path.join(out,'dependencies.json'),'/Fo'+object,'/Fe'+exe,'/link','advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib','userenv.lib','shell32.lib'];
const build=spawnSync(tools.compiler,args,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'compile.log'),(build.stdout??'')+(build.stderr??''));
fs.writeFileSync(path.join(out,'BUILD.json'),JSON.stringify({compiler:tools.compiler,compilerSha256:sha(fs.readFileSync(tools.compiler)),args,exit:build.status,error:build.error?.message??null},null,2));assert.equal(build.status,0,'Entry fixture compile failed');
const cef=path.join(root,'bin','cef','fixture','steamwebhelper.exe');fs.mkdirSync(path.dirname(cef),{recursive:true});fs.copyFileSync(exe,cef);
const run=spawnSync(exe,[path.join(out,'runtime-fixtures'),...process.argv.slice(3)],{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'run.log'),(run.stdout??'')+(run.stderr??''));
const changed=pins.filter(p=>sha(fs.readFileSync(path.join(repo,p.path)))!==p.sha256).map(p=>p.path);
const result={schema:'ymcc.binding-runtime-build-and-run.v1',at:new Date().toISOString(),status:run.status===0&&changed.length===0?'pass':'fail',compilerExit:build.status,runExit:run.status,error:run.error?.message??null,sourceInputs:pins.length,changedOriginalInputs:changed,exeSha256:sha(fs.readFileSync(exe)),objectSha256:sha(fs.readFileSync(object)),selection:process.argv[3]??null,resumeConsumer:consumer,scope:'Real production Runtime/Client/Owner plus source-built protocol peer, inert Loader/CEF. Original 3200ms deadline and release-after-measurement. Not real Steam bootstrap, field input or full package acceptance.',fullRuntimeMigrationPassed:false,hostReleasePassed:false,installed:false,fieldActions:0,sleepActions:0};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));if(result.status!=='pass')process.exitCode=1;


