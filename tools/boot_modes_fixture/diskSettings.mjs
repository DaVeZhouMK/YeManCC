/** Supporting backend for settings.boot-modes-file-ui; no new parent or global suite. */
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync,spawn} from 'node:child_process';import {createInterface} from 'node:readline';
import {repo,fixture,runtimeBundle,hash} from './harness.mjs';
export const nativeInputs=['native/main.cpp','native/gyro_telemetry_record.h','native/settings_baseline.h','native/input_startup_policy.h','native/controller_persona_settings.h','native/controller_persona.h','native/boot_mode_shortcuts.h','deps/json/json.hpp','tools/boot_modes_fixture/diskSettingsNative.cpp.in'];
export function prepareDiskSettings(out,options={}){
 fs.mkdirSync(out,{recursive:true});const mainSource=options.mainSource?path.resolve(options.mainSource):repo+'/native/main.cpp',main=fs.readFileSync(mainSource,'utf8');
 const slice=(a,b)=>{assert.equal(main.split(a).length,2,'production anchor '+a);const start=main.indexOf(a),end=main.indexOf(b,start+a.length);assert(end>start,'production end '+b);return main.slice(start,end);};
 const groups=[slice('bool sgWriteFileAtomicFlags(','static bool sgWriteFileAtomicTransient('),slice('static void ymSettingsReadRawInto(','static void ymSettingsApplyDelta('),slice('static void ymSettingsApplyDelta(','static json ymSettingsReadUnlocked()'),slice('static json ymSettingsReadUnlocked()','class SettingsFileGuard'),slice('static bool ymSettingsFileExceedsLimit(','static bool ymSettingsWriteDocument('),slice('static json ymSettingsReadSnapshot(','static json ymSettingsResetApplication(')];
 // This disk-only consumer isolates CSS; the full Runtime wake is tested by the original appearance case.
 const pendingDeclaration='static thread_local bool g_settingsBootOwnerChanged = false;',hasOwnerNotification=main.includes(pendingDeclaration);
 const notificationPort=hasOwnerNotification?pendingDeclaration+'\nstatic struct {bool notifyRequestChanged() noexcept {return false;}} g_steamCssUse;\n':'';
 if(hasOwnerNotification){assert.equal(groups[3].split(pendingDeclaration).length,2);groups[3]=groups[3].replace(pendingDeclaration,'');}
 const serialFence=groups.some(body=>body.includes('GamepadSerialEvent::'))
  ?slice('struct GamepadSerialEvent {','static thread_local unsigned long long g_gamepadSerialActionPower=0;'):'';
 let guard='struct SettingsFileGuard {};',ioPorts='',touchHeaders='',touchHandler='',touchDispatch='';
 let includeRoot=repo,extraInputs=[];
 assert(!(options.settingsPreparation&&options.touchProfiles),'Choose settings-only or original touch preparation');
 if(options.settingsPreparation){
  ({guard,ioPorts,touchHeaders,touchHandler,touchDispatch,includeRoot,extraInputs}=options.settingsPreparation({out,main,slice,groups}));
 }else if(options.touchProfiles){
  const {prepareTouchProfilesNative}=options.touchProfilePreparation;
  const prepared=prepareTouchProfilesNative({out,main,slice,groups});
  ({guard,ioPorts,touchHeaders,touchHandler,touchDispatch,includeRoot,extraInputs}=prepared);
 }
 if(options.mainSource)extraInputs.push(mainSource);
 const source=fs.readFileSync(fixture+'/diskSettingsNative.cpp.in','utf8')
  .replace('// FILE_SETTINGS_GUARD',notificationPort+(serialFence?'#include '+JSON.stringify(path.join(repo,'native/gyro_telemetry_record.h').replaceAll('\\','/'))+'\n':'')+serialFence+'\n'+guard).replace('// FILE_SETTINGS_IO_PORTS',ioPorts)
  .replace('// FILE_SETTINGS_TOUCH_HEADERS',touchHeaders).replace('// FILE_SETTINGS_TOUCH_HANDLER',touchHandler)
  .replace('// FILE_SETTINGS_TOUCH_DISPATCH',touchDispatch).replace('// FILE_SETTINGS_PRODUCTION_BODIES',groups.join('\n'));
 if(options.sourceOnly===true)return {source,includeRoot,extraInputs,sourceOnly:true};
 const cpp=out+'/file-settings.cpp',exe=out+'/file-settings.exe';fs.writeFileSync(cpp,source);
 const msvc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207',sdk='C:/Program Files (x86)/Windows Kits/10',version='10.0.26100.0';
 const compiler=msvc+'/bin/Hostx64/x64/cl.exe',env={...process.env,INCLUDE:[msvc+'/include',...['um','shared','ucrt','winrt','cppwinrt'].map(part=>sdk+'/Include/'+version+'/'+part),includeRoot+'/deps/json',includeRoot+'/native'].join(';'),LIB:[msvc+'/lib/x64',sdk+'/Lib/'+version+'/um/x64',sdk+'/Lib/'+version+'/ucrt/x64'].join(';'),PATH:[msvc+'/bin/Hostx64/x64','C:/Windows/System32',process.env.PATH].join(';')};
 const compileArgs=['/nologo','/EHsc','/std:c++20','/utf-8',...(options.touchProfiles?['/DUNICODE','/D_UNICODE','/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1']:[]),
  '/sourceDependencies',out+'/source-dependencies.json',cpp,'/Fo'+out+'/file-settings.obj','/Fe'+exe,'/link','/VERBOSE:LIB',...(options.touchProfiles?['user32.lib','gdi32.lib','ole32.lib']:[])];
 const compileBegan=Date.now(),build=spawnSync(compiler,compileArgs,{cwd:out,env,windowsHide:true,encoding:'utf8',timeout:45000});
 fs.writeFileSync(out+'/compile.log',(build.stdout||'')+(build.stderr||''));
 fs.writeFileSync(out+'/compile-process.json',JSON.stringify({program:compiler,args:compileArgs,elapsedMs:Date.now()-compileBegan,timeoutMs:45000,exitCode:build.status,signal:build.signal,error:build.error?.message??null},null,2));
 assert(!build.error&&!build.signal,'native file compiler did not finish naturally');assert.equal(build.status,0,build.stdout+build.stderr);
 const root=path.resolve(options.storageRoot||out),safe=value=>{const file=path.resolve(value),rel=path.relative(root,file);assert(rel&&!rel.startsWith('..')&&!path.isAbsolute(rel),'outside file evidence '+file);return file;};
 const child=spawn(exe,[],{cwd:out,windowsHide:true,stdio:['pipe','pipe','pipe']}),pending=[];let helperError='',calls=0;
 child.stderr.on('data',data=>helperError=(helperError+data.toString()).slice(-16384));
 const lines=createInterface({input:child.stdout});lines.on('line',line=>{const task=pending.shift();if(!task)return;clearTimeout(task.timer);try{const value=JSON.parse(line);if(value.error)task.reject(Error(value.error));else task.resolve(value);}catch(error){task.reject(error);}});
 child.on('error',error=>{for(const task of pending.splice(0)){clearTimeout(task.timer);task.reject(error);}});child.on('exit',code=>{for(const task of pending.splice(0)){clearTimeout(task.timer);task.reject(Error('native file helper exited '+code+' '+helperError));}});
 const invoke=(file,request)=>{const started=performance.now(),queueDepth=pending.length;const operationPromise=new Promise((resolve,reject)=>{const task={resolve,reject,timer:null};task.timer=setTimeout(()=>{const index=pending.indexOf(task);if(index>=0)pending.splice(index,1);reject(Error('native file request deadline'));child.kill();},6000);pending.push(task);calls++;child.stdin.write(JSON.stringify({...request,file:safe(file),root})+'\n');});
  const observe=error=>fs.appendFileSync(out+'/request-timings.jsonl',JSON.stringify({operation:request.operation,elapsedMs:performance.now()-started,queueDepth,contentBytes:typeof request.content==='string'?Buffer.byteLength(request.content):0,...(error?{error:String(error.message??error)}:{})})+'\n');
  void operationPromise.then(()=>observe(),error=>observe(error));return operationPromise;
 };
 const close=async()=>{child.stdin.end();if(child.exitCode===null)await new Promise(resolve=>child.once('exit',resolve));lines.close();fs.writeFileSync(out+'/native-request-count.json',JSON.stringify({helperProcesses:1,helperPid:child.pid,requests:calls,perRequestBudgetMs:6000,pending:pending.length,exitCode:child.exitCode,signalCode:child.signalCode},null,2));};
 const evidence={compiledThisRun:true,...(options.mainSource?{mainSource,mainSourceSha256:hash(mainSource)}:{}),compilerSha256:hash(compiler),exeSha256:hash(exe),sourceSha256:hash(cpp),productionGroups:groups.length,serialActionFence:serialFence?{source:'native/main.cpp',symbol:'GamepadSerialEvent',verbatim:true,recordHeader:'native/gyro_telemetry_record.h',recordHeaderSha256:hash(repo+'/native/gyro_telemetry_record.h'),jsonHeader:'deps/json/json.hpp',jsonHeaderSha256:hash(includeRoot+'/deps/json/json.hpp')}:null,realFileIo:true,extraInputs,systemMutexAndInputHostAdmission:options.touchProfiles||options.settingsPreparation?'original settings guard with owned process mutex; InputHost isolated':'isolated; not hardware acceptance'};
 fs.writeFileSync(out+'/native-build.json',JSON.stringify(evidence,null,2));return {invoke,safe,root,evidence,close};
}
export async function createDiskRuntime(dir,native,options={}){
 fs.mkdirSync(dir,{recursive:true});const config=options.storageDirectory||dir+'/PowerControl';fs.mkdirSync(config,{recursive:true});const file=config+'/yeman-settings.json',trace=[],faults={failWrite:false,failRead:false,dropWriteAck:false,holdWrite:false,releaseWrite:null},store={file,config,trace,faults};
 const raw=p=>fs.readFileSync(native.safe(p),'utf8');
 const api={location:async()=>({directory:config,file,backup:file+'.bak'}),read:async(p,stamp)=>{trace.push({op:'read',path:native.safe(p)});if(faults.failRead)throw Error('isolated settings read failure');return native.invoke(p,{operation:'read',stamp:stamp||''});},exists:async p=>fs.existsSync(native.safe(p)),readTextFile:async(p,max=4194304)=>{const content=raw(p);assert(Buffer.byteLength(content)<=max,'maxBytes');return content;},rename:async(a,b)=>{fs.renameSync(native.safe(a),native.safe(b));return true;},write:async(p,content,transaction={})=>{assert.equal(path.resolve(p),path.resolve(file));trace.push({op:'write',initialize:transaction.initialize===true,expectedInputRevision:transaction.expectedInputRevision});if(faults.holdWrite){faults.holdWrite=false;await new Promise(resolve=>faults.releaseWrite=resolve);faults.releaseWrite=null;}if(faults.failWrite){faults.failWrite=false;return false;}const result=await native.invoke(p,{operation:'write',content,...transaction});if(faults.dropWriteAck){faults.dropWriteAck=false;throw Error('isolated commit acknowledgement lost');}return result;}};
 const bind=()=>globalThis.__bootDiskPort=api;
 const moduleAt=async name=>{bind();const folder=options.moduleDirectory||dir+'/'+name;fs.mkdirSync(folder,{recursive:true});const a=await runtimeBundle(folder,{bootModesSource:options.bootModesSource,settingsSources:options.settingsSources,diskSettingsPort:fixture+'/diskApiPort.ts',reuseCompiled:!!options.moduleDirectory});a.repository.setSettingsDirectory(config);
 // The simulated native owner reads its own current file facts, never the
 // request's supplied context or a stale renderer-era memory mirror.
 const refreshMouseFacts=async()=>{bind();Object.assign(a.ports.settings,await a.repository.loadSettings());};
 a.ports.hooks.mouseRead=refreshMouseFacts;a.ports.hooks.mouseWrite=refreshMouseFacts;a.ports.hooks.mouseBackend=refreshMouseFacts;
 a.ports.hooks.steamStartupOwnerRead=refreshMouseFacts;
 a.ports.hooks.legacyStartupMutation=refreshMouseFacts;
 return a;};
 const a=await moduleAt('initial');
 return {...store,a,bind,moduleAt,read:()=>JSON.parse(raw(file)),writeRaw:value=>fs.writeFileSync(file,typeof value==='string'?value:JSON.stringify(value,null,2)),startup:(boot,session)=>native.invoke(file,{operation:'startup',boot,session}),flush:()=>{fs.writeFileSync(dir+'/file-trace.json',JSON.stringify(trace,null,2));const evidence=dir+'/persisted';fs.mkdirSync(evidence,{recursive:true});for(const name of fs.readdirSync(config))if(name.startsWith('yeman-settings.json'))fs.copyFileSync(config+'/'+name,evidence+'/'+name);}};
}


/** Original VM repository terminal backed by the same owned native file helper.
 * Caller owns helper lifetime; no app directory or device lookup is allowed. */
export function createNativeSettingsFixtureStorage(native,directory){
 const root=path.resolve(directory);fs.mkdirSync(root,{recursive:true});
 const safe=file=>{const target=path.resolve(file),relative=path.relative(root,target);assert(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative)&&path.dirname(target)===root,'Native fixture storage escaped owned directory');assert(path.basename(target).startsWith('yeman-settings.json'),'Unexpected native fixture filename');return native.safe(target);};
 return {directory:root,
  snapshot:()=>new Map(fs.readdirSync(root).filter(name=>name.startsWith('yeman-settings.json')).map(name=>{const file=safe(path.join(root,name));return [file,fs.readFileSync(file,'utf8')];})),
  seed:(file,raw)=>{fs.writeFileSync(safe(file),raw);},
  read:args=>native.invoke(safe(args.path),{operation:'read',stamp:args.stamp||''}),
  write:args=>native.invoke(safe(args.path),{operation:'write',content:args.content,baseline:args.baseline,initialize:args.initialize===true,...(args.expectedInputRevision===undefined?{}:{expectedInputRevision:args.expectedInputRevision}),...(args.guardBootMode?{guardBootMode:true}:{})})
 };
}
