import {selectedNativeTools} from './ymcc_sim/global/release-host-environment.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const repo=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]??'');
let selection=null,readerRetained=false,sourceNotification=false;
for(let i=3;i<process.argv.length;i++){
 const arg=process.argv[i];
 if(arg==='--case'){assert.equal(selection,null,'Duplicate client case selection');selection=process.argv[++i];assert(selection&&!selection.startsWith('--'),'Missing client case');}
 else if(arg==='--source-notification'){assert(!sourceNotification,'Duplicate source notification parameter');sourceNotification=true;}
 else if(arg==='--reader-retained'){assert(!readerRetained,'Duplicate retained reader parameter');readerRetained=true;}
 else throw Error('Unknown client selftest parameter '+arg);
}
const knownCases=[...fs.readFileSync(path.join(repo,'tools/decky_binding_client_selftest.cpp'),'utf8').matchAll(/\btest\("([^"\r\n]+)"/g)].map(row=>row[1]);
if(selection)assert(knownCases.includes(selection),'Unknown original client case');
if(readerRetained)assert.equal(selection,'zero-budget-stop-preserves-incomplete-state-and-is-repeatable','Reader retirement parameter belongs to the original zero-budget case');
if(sourceNotification){assert.equal(selection,'refresh-and-ordinary-stop-preserve-enabled-epoch-off-does-not','Source notification belongs to the original refresh case');assert(!readerRetained,'Cannot combine source notification and retained reader ports');}
assert(process.argv[2]&&!fs.existsSync(out),'Use a fresh evidence directory.');fs.mkdirSync(out,{recursive:true});
const source=path.join(out,'source'),pins=[],seen=new Set();const sha=b=>crypto.createHash('sha256').update(b).digest('hex');const search=[path.join(repo,'native'),path.join(repo,'deps/json'),repo];
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
function freeze(file){file=path.resolve(file);if(seen.has(file.toLowerCase()))return;const relative=path.relative(repo,file);if(relative.startsWith('..')||path.isAbsolute(relative))return;seen.add(file.toLowerCase());const bytes=fs.readFileSync(file),target=path.join(source,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);assert.equal(sha(fs.readFileSync(file)),sha(bytes),'Concurrent input change: '+relative);pins.push({path:relative.replaceAll('\\','/'),bytes:bytes.length,sha256:sha(bytes)});for(const m of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){const found=[path.resolve(path.dirname(file),m[1]),...search.map(p=>path.resolve(p,m[1]))].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(found)freeze(found);}}
freeze(path.join(repo,'tools/decky_binding_client_selftest.cpp'));freeze(path.join(repo,'tools/decky_binding_client_selftest.mjs'));freeze(path.join(repo,'deps/json/json.hpp'));
const fixturePorts=[];
if(sourceNotification){
 const mainPath=path.join(repo,'native/main.cpp'),before=fs.readFileSync(mainPath),main=before.toString('utf8');
 const templatePath=path.join(repo,'tools/fixtures/decky_source_notification_diagnostic_cases.inc.in');freeze(templatePath);
 const begin='static void deckySidebarSessionDiagnostic(',middle='// Source notifications already revoke the previous binding.',end='static ymcc::deckysidebar::BindingPlan deckySidebarOwnedBindingPlan';
 for(const anchor of [begin,middle,end])assert.equal(main.split(anchor).length-1,1,'Source notification main anchor');
 const a=main.indexOf(begin),b=main.indexOf(middle,a),c=main.indexOf(end,b);
 let code=fs.readFileSync(templatePath,'utf8').replace('@@DIAGNOSTIC@@',main.slice(a,b)).replace('@@WRAPPER@@',main.slice(b,c));
 const fanoutBegin='if (const auto source=g_steamSettingsObserver.identitySnapshot()) {\n                g_steamLinkAdmission.observe(source->session);';
 const fanoutEnd='\n        }\n        if (sessionChanged){steamWallpaperForegroundSignal();';
 const fanoutMain=main.replaceAll('\r\n','\n');
 assert.equal(fanoutMain.split(fanoutBegin).length-1,1,'Source notification actual fan-out');
 const from=fanoutMain.indexOf(fanoutBegin),to=fanoutMain.indexOf(fanoutEnd,from);
 assert(to>from,'Source notification actual fan-out end');
 code=code.replace('@@SESSION_FANOUT@@',fanoutMain.slice(from,to));
 const calls=[
  ['PRESENCE_CALL','deckySidebarObserveSteamSource(source->presence,source->session,"observer-presence");'],
  ['INITIAL_CALL','deckySidebarObserveSteamSource(initial->presence,initial->session,"observer-initial");']
 ];
 for(const [slot,call] of calls){assert.equal(main.split(call).length-1,1,'Source notification actual caller');code=code.replace('@@'+slot+'@@',call);}
 assert(!code.includes('@@'),'Unfilled source notification template');
 const relative='native/main.cpp',captured=path.join(source,relative);fs.mkdirSync(path.dirname(captured),{recursive:true});fs.writeFileSync(captured,before);pins.push({path:relative,bytes:before.length,sha256:sha(before)});
 assert.deepEqual(fs.readFileSync(mainPath),before,'Source notification main changed during generation');
 const target=path.join(source,'tools/fixtures/decky_source_notification_diagnostic_cases.inc');fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,code);
 fixturePorts.push({path:target,bytes:Buffer.byteLength(code),sha256:sha(Buffer.from(code)),generator:'tools/decky_binding_client_selftest.mjs',main:{path:mainPath,bytes:before.length,sha256:sha(before)},template:{path:templatePath,bytes:fs.statSync(templatePath).size,sha256:sha(fs.readFileSync(templatePath))},scope:'Actual main helper, synchronous session fan-out and two other observation call statements; full producer callback gates are outside this test'});
}
if(readerRetained){
 const relative='native/decky_binding_client.h',target=path.join(source,relative),before=fs.readFileSync(target);
 const anchor=Buffer.from('class EventReader {'),marker=Buffer.from('\n    friend struct ReaderRetainedFixtureAccess; // Private observation only.\n');
 assert(!before.includes(marker),'Reader header is already adapted');assert.equal(before.indexOf(anchor),before.lastIndexOf(anchor));assert(before.indexOf(anchor)>=0);
 const at=before.indexOf(anchor)+anchor.length,derived=Buffer.concat([before.subarray(0,at),marker,before.subarray(at)]);
 const inverse=Buffer.concat([derived.subarray(0,at),derived.subarray(at+marker.length)]);assert.deepEqual(inverse,before,'Reader fixture exact private inverse');
 fs.writeFileSync(target,derived);
 fixturePorts.push({path:target,original:{path:relative,bytes:before.length,sha256:sha(before)},derived:{path:relative,bytes:derived.length,sha256:sha(derived)},marker:marker.toString('utf8'),transformation:'One private friend; no resource, IO, wait or cleanup replacement'});
}
fs.writeFileSync(path.join(out,'SOURCE-PINS.json'),JSON.stringify({repo,source,files:pins,selection,readerRetained,sourceNotification,fixturePorts},null,2));
const declaration=selectedNativeTools(),tools=declaration.native;
const env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));env.PATH=path.dirname(tools.compiler)+';'+process.env.SystemRoot+'/System32';env.INCLUDE=tools.include.join(';');env.LIB=tools.lib.join(';');env.CL='';env._CL_='';env.LINK='';env._LINK_='';env.VSLANG='1033';env.TEMP=path.join(out,'tmp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
const root=path.join(out,'FixtureSteam');fs.mkdirSync(root);const exe=path.join(root,'steam.exe'),object=path.join(out,'binding-client.obj');
const args=['/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/DNOMINMAX','/DUNICODE','/D_UNICODE',...(readerRetained?['/DYMCC_RETAINED_READER_FIXTURE']:[]),...(sourceNotification?['/DYMCC_SOURCE_NOTIFICATION_DIAGNOSTIC_FIXTURE']:[]),'/I'+path.join(source,'native'),'/I'+path.join(source,'deps/json'),path.join(source,'tools/decky_binding_client_selftest.cpp'),'/sourceDependencies',path.join(out,'dependencies.json'),'/Fo'+object,'/Fe'+exe,'/link','/verbose:lib','advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib','userenv.lib','shell32.lib'];
// Actual used includes/libraries are checked against this precompiler superset.
// Generated visibility is paired to the exact original product bytes above.
const declaredInputs=new Map(),visited=new Set(),includeRoots=[path.join(source,'native'),path.join(source,'deps/json'),...tools.include];
const key=p=>path.resolve(p).toLowerCase();
function pinInput(file){const p=path.resolve(file),id=key(p);if(declaredInputs.has(id))return;const b=fs.readFileSync(p);declaredInputs.set(id,{path:p,bytes:b.length,sha256:sha(b)});}
for(const row of [...declaration.files,...(declaration.additionalActualDependencies??[])]){pinInput(row.path);assert.equal(declaredInputs.get(key(row.path)).sha256,row.sha256.toLowerCase(),'Native tool changed');}
const libraries=new Set(args.filter(arg=>/\.lib$/i.test(arg)));
function visit(file){if(visited.has(key(file)))return;visited.add(key(file));pinInput(file);const text=fs.readFileSync(file,'utf8');for(const m of text.matchAll(/^\s*#\s*include\s*([<"])([^>"\r\n]+)[>"]/gm)){const found=[...(m[1]==='"'?[path.resolve(path.dirname(file),m[2])]:[]),...includeRoots.map(dir=>path.resolve(dir,m[2]))].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(found)visit(found);}for(const m of text.matchAll(/^\s*#\s*pragma\s+comment\s*\(\s*lib\s*,\s*"([^"\r\n]+)"/gim))libraries.add(m[1]);}
visit(path.join(source,'tools/decky_binding_client_selftest.cpp'));
for(const name of libraries){const file=tools.lib.map(dir=>path.join(dir,name)).find(p=>fs.existsSync(p));if(file)pinInput(file);else assert(!args.includes(name),'Missing explicit import library');}
const precompiler=[...declaredInputs.values()];fs.writeFileSync(path.join(out,'PRECOMPILE-INPUT-CLOSURE.json'),JSON.stringify({files:precompiler},null,2)+'\n');
const buildBegin=performance.now(),buildStartedAtUtc=new Date().toISOString();
const build=spawnSync(tools.compiler,args,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'compile.log'),(build.stdout??'')+(build.stderr??''));
fs.writeFileSync(path.join(out,'BUILD.json'),JSON.stringify({pid:build.pid,startedAtUtc:buildStartedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-buildBegin,timeoutMs:60000,compiler:tools.compiler,compilerSha256:sha(fs.readFileSync(tools.compiler)),args,exit:build.status,error:build.error?.message??null},null,2));assert.equal(build.status,0,'Entry fixture compile failed');
const deps=read(path.join(out,'dependencies.json')).Data,actualSource=[deps.Source,...deps.Includes];
const actualLibraries=[...new Set([...(build.stdout??'').matchAll(/^\s*Searching (.+\.lib):\s*$/gm)].map(m=>path.resolve(m[1].trim())))];
assert(actualLibraries.length>0,'Actual linker library trace missing');
const missing=[...actualSource,...actualLibraries].filter(p=>!declaredInputs.has(key(p)));
const drift=precompiler.filter(row=>{const b=fs.readFileSync(row.path);return b.length!==row.bytes||sha(b)!==row.sha256;}).map(row=>row.path);
const compilerInputClosure={complete:missing.length===0&&drift.length===0,actualSource,actualLibraries,missing,drift};
fs.writeFileSync(path.join(out,'COMPILER-INPUT-CLOSURE.json'),JSON.stringify(compilerInputClosure,null,2)+'\n');assert(compilerInputClosure.complete,'Undeclared or changed actual compiler input');
const cef=path.join(root,'bin','cef','fixture','steamwebhelper.exe');fs.mkdirSync(path.dirname(cef),{recursive:true});fs.copyFileSync(exe,cef);
const resultPath=path.join(out,'test-result.json'),runArgs=[resultPath,...(selection?['--case',selection]:[])],runBegin=performance.now(),runStartedAtUtc=new Date().toISOString();
const run=spawnSync(exe,runArgs,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'run.log'),(run.stdout??'')+(run.stderr??''));
fs.writeFileSync(path.join(out,'NATIVE-PROCESS.json'),JSON.stringify({pid:run.pid,executable:exe,args:runArgs,sha256:sha(fs.readFileSync(exe)),startedAtUtc:runStartedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-runBegin,timeoutMs:45000,exitCode:run.status,signal:run.signal,error:run.error?.message??null},null,2)+'\n');
const changed=pins.filter(p=>sha(fs.readFileSync(path.join(repo,p.path)))!==p.sha256).map(p=>p.path);
const native=fs.existsSync(resultPath)?read(resultPath):null;
const selectionMatched=!selection||(native?.tests===1&&native?.cases?.length===1&&native.cases[0].name===selection);
const retainedMatched=!readerRetained||(Array.isArray(native?.retainedReaderMeasurements)&&native.retainedReaderMeasurements.length===6);
const result={schema:'ymcc.binding-client-build-and-run.v1',at:new Date().toISOString(),status:run.status===0&&changed.length===0&&selectionMatched&&retainedMatched?'pass':'fail',compilerExit:build.status,runExit:run.status,error:run.error?.message??null,sourceInputs:pins.length,changedOriginalInputs:changed,exeSha256:sha(fs.readFileSync(exe)),objectSha256:sha(fs.readFileSync(object)),selection,selectionMatched,readerRetained,retainedMatched,sourceNotification,fixturePorts,compilerInputClosure,scope:'Real Client/Owner/EventReader with same-image private worker and owned fixtures. No Steam/device/sleep; component evidence only, not the original Runtime 3200ms gate.',fullRuntimeMigrationPassed:false,hostReleasePassed:false,installed:false,fieldActions:0,sleepActions:0};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));if(result.status!=='pass')process.exitCode=1;
