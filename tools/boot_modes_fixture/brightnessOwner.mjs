import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {pathToFileURL} from 'node:url';
import {require} from './harness.mjs';
import {spawnSync} from 'node:child_process';
import {extract} from '../fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from '../fixtures/hc_reviewed_calibration/toolchain.mjs';

export async function runShellOwnerNative(out,args=[]){
 const root=process.cwd(),gamePower=args.includes('--game-detect-public-power'),power=args.includes('--shell-public-power')||gamePower,powerIndex=args.indexOf('--shell-power-case'),powerCase=powerIndex<0?'all':args[powerIndex+1];
 const powerNames=gamePower?["zero-sequence","current","deny-start","stale-generation","stale-sequence","scan-auto","scan-preferred","rules-failure","same-fact","publication-only","legacy-current","malformed","repeat-unavailable"]:["current","stale-dispatch","create","resume","exit","unguarded-create","read-only-create","ordinary-create","publication-create","same-fact-create","deny-start","plan-disabled-create","plan-current","manual-plan-disabled-create","plan-legacy-current","plan-invalid-setting-create","plan-unknown-create","plan-stale-create"];assert(!power||powerCase==='all'||powerCase==='invalid'||powerNames.includes(powerCase),'SHELL_PUBLIC_POWER_UNKNOWN_CASE');
 const reuseIndex=args.indexOf('--shell-power-reuse'),reuse=reuseIndex<0?null:path.resolve(args[reuseIndex+1]);assert(!reuse||power,'SHELL_PUBLIC_POWER_REUSE_REQUIRES_SCOPE');
 const dir=path.join(out,'native-shell');fs.mkdirSync(dir);
 const selection=power?'power:'+powerCase:args.includes('--shell-owner-before')?'before':'all',hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const pin=file=>({path:file,bytes:fs.statSync(file).size,sha256:hash(file)}),write=(name,value)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const main=fs.readFileSync(root+'/native/main.cpp','utf8'),names=['ymSettingsBootWriteMatches','mouseModeBootOwnerCheck'];
 const fragments=names.map(name=>extract(main,name));
 const powerFunctions=['currentPowerGeneration','hardwareWriteAllowed','readSharedSystemPower','captureSharedPowerLifecycle','sharedPowerLifecycleCurrent','observeSharedPowerRevocation','beginHardwareWriteLease','endHardwareWriteLease'].map(name=>{const row=extract(main.replaceAll('[[maybe_unused]] static','\nstatic'),name);const at=main.indexOf(row.code);assert(at>=0&&main.indexOf(row.code,at+1)<0,'exact public function body '+name);const attribute='[[maybe_unused]] ';if(main.slice(at-attribute.length,at)===attribute)row.code=attribute+row.code;assert(main.includes(row.code));return row;});
 const leaseBegin=main.indexOf('struct HardwareWriteLease {'),leaseEnd=main.indexOf('\n};',leaseBegin)+3;assert(leaseBegin>=0&&leaseEnd>leaseBegin);const lease=main.slice(leaseBegin,leaseEnd);
 const publicCode=powerFunctions.filter(row=>!['beginHardwareWriteLease','endHardwareWriteLease'].some(n=>row.code.includes(' '+n+'('))).map(row=>row.code).join('\n');
 const leaseCode=powerFunctions.filter(row=>['beginHardwareWriteLease','endHardwareWriteLease'].some(n=>row.code.includes(' '+n+'('))).map(row=>row.code).join('\n')+'\n'+lease;
 const start=main.indexOf('    ipc_on("shell.run",'),ending=/\r?\n\}\);(?=\r?\n\})/.exec(main.slice(start));assert(start>0&&ending);
 const registration=main.slice(start,start+ending.index+ending[0].length);
 const lowerHeader=fs.readFileSync(root+'/native/steam_launch_path.h','utf8'),lowerRows=lowerHeader.split(/\r?\n/).filter(line=>line.startsWith('inline std::wstring lower('));assert.equal(lowerRows.length,1);const lowerSource='namespace ymcc::steamlaunch {\n'+lowerRows[0]+'\n}';
 const gameSource=gamePower?(await import('./gameDetectionPower.mjs')).gameDetectionPowerSource(root,publicCode):null;
 const source=gameSource?gameSource.source:fs.readFileSync(root+'/tools/boot_modes_fixture/shellOwner.cpp.in','utf8')
  .replace('// ORIGINAL_STEAM_PATH_LOWER',lowerSource).replace('// ORIGINAL_SHARED_POWER_FUNCTIONS',publicCode).replace('// ORIGINAL_HARDWARE_WRITE_LEASE',leaseCode).replace('// ORIGINAL_OWNER',fragments.map(row=>row.code).join('\n')).replace('// ORIGINAL_SHELL_HANDLER',registration);
 const cpp=path.join(dir,'probe.cpp'),exe=path.join(dir,'probe.exe');fs.writeFileSync(cpp,source,{flag:'wx'});
 const inputs=['native/main.cpp','native/settings_baseline.h','native/steam_native_launch.h','native/steam_launch_path.h','native/sleep_power_dispatch.h','native/sleep_power_state.h','native/sleep_power_observation.h','native/sleep_power_revocation.h','native/sleep_cancel_ready.h','deps/json/json.hpp','PowerControl/settings-defaults-v1.json','tools/boot_modes_fixture/brightnessOwner.mjs','tools/boot_modes_fixture/shellOwner.cpp.in','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',...(gamePower?['native/monitor_identity.h','native/game_rule_literal.h','tools/fixtures/steam_original_recovery_valve_guard/concurrency-cases.cpp.in','tools/boot_modes_fixture/gameDetectionPower.mjs','tools/boot_modes_fixture/gameDetectionPower.cpp.in']:[])].map(file=>pin(path.join(root,file)));
 const command=path.join(dir,'compile.cmd'),preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
 fs.writeFileSync(command,preamble+'set "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\ncl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- /O2 /DNDEBUG /MD /I"'+root+'/native" /I"'+root+'/deps/json" "'+cpp+'" /Fo"'+dir+'/probe.obj" /Fe"'+exe+'" /sourceDependencies "'+dir+'/dependencies.json" /link user32.lib ole32.lib\r\nexit /b %errorlevel%\r\n',{flag:'wx'});
 const execute=(label,program,argv,timeout)=>{
  const begin=performance.now(),p=spawnSync(program,argv,{cwd:dir,env:process.env,windowsHide:true,encoding:'utf8',timeout,maxBuffer:16*1024*1024});
  for(const stream of ['stdout','stderr'])fs.writeFileSync(path.join(dir,label+'.'+stream+'.raw'),p[stream]??'',{flag:'wx'});
  write(label+'.process.json',{program,args:argv,cwd:dir,exitCode:p.status,signal:p.signal,error:p.error?.message??null,elapsedMs:performance.now()-begin});
  assert(!p.error&&!p.signal,'Owned shell consumer did not finish');return p;
 };
 write('TASK.json',{originalCase:'actual display platform rejects invented brightness and retains saved but inactive power-plan receipts',selection,
  expectedRows:power?(powerCase==='all'?powerNames.length:1):selection==='before'?1:128,compileLimitMs:45000,executionLimitMs:12000,inputs,generated:pin(cpp),
  actualConsumer:'Original shell.run registration/owner guard; actual pipes, suspended owned child, Job, wait/read/join/cleanup. Child image is always this owned probe; no real requested command is run.'});
 let actualExe=exe;
 if(reuse){const old=JSON.parse(fs.readFileSync(path.join(reuse,'TASK.json'),'utf8'));assert.equal(old.generated.sha256,pin(cpp).sha256,'SHELL_PUBLIC_POWER_REUSE_GENERATED_DRIFT');for(const input of old.inputs)assert.equal(hash(input.path),input.sha256);actualExe=path.join(reuse,'probe.exe');write('compile-reuse.json',{compiledTask:pin(path.join(reuse,'TASK.json')),compiledExecutable:pin(actualExe),exactGeneratedSource:true,compileExecuted:false});}
 else{const compile=execute('compile','cmd.exe',['/d','/c',command],45000);assert.equal(compile.status,0,'Original shell.run source failed compilation');}
 const raw=path.join(dir,'observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
 const run=execute('execute',actualExe,[root+'/PowerControl/settings-defaults-v1.json',selection,raw],12000);
 assert([0,1].includes(run.status),'Abnormal native exit; inspect process/raw before parsing');const actual=JSON.parse(run.stdout);
 if(power){
  const expected=powerCase==='all'?powerNames:[powerCase];assert.deepEqual(actual.rows.map(row=>row.name),run.status===0?expected:expected.slice(0,actual.rows.length));
  if(args.includes('--shell-power-expect-failure')){assert.equal(run.status,1);assert.equal(actual.rows.length,1);assert.match(actual.error,/revoked public shell request accepted|revoked public shell child resumed|revoked public game request accepted/);}
  else{assert.equal(run.status,0,actual.error);assert.equal(actual.status,'PASS');assert.equal(actual.rows.length,expected.length);}
 }
 else if(selection==='before'){assert.equal(run.status,1);assert.equal(actual.rows.length,1);assert.match(actual.error,/expired native shell request was accepted|expired native shell child was resumed/);}
 else {assert.equal(run.status,0,actual.error);assert.equal(actual.status,'PASS');assert.equal(actual.rows.length,128);assert.equal(actual.ordinaryAndPowerControls,3);assert(actual.rows.every(row=>row.jobZeroBeforeClose&&row.childExited&&row.actualOwnedImageMatched&&row.explicitHandlesAtReturn===0&&row.explicitHandlesCreated===row.explicitHandlesClosed));}
 for(const pin of inputs)assert.equal(hash(pin.path),pin.sha256,'Native shell input changed '+pin.path);
 const result={status:selection==='before'||args.includes('--shell-power-expect-failure')?'EXPECTED_SEMANTIC_REJECTION':'PASS',actualRows:actual.rows.length,ordinaryAndPowerControls:selection==='all'?3:0,selection,
  inputs,generated:pin(cpp),executable:pin(actualExe),powerScope:power,powerCase,powerFunctions:powerFunctions.map(row=>({symbol:row.symbol??null,code:row.code})),leaseSource:lease,exactSteamLowerSource:lowerRows[0],actual:pin(path.join(dir,'execute.stdout.raw')),observations:pin(raw),realPowercfgOrHardwareActions:0,fullNativeMain:'NOT_RUN'};
 write('RESULT.json',result);return result;
}


export async function runShellPublicWire(out){
 const root=process.cwd(),memory="export const calls=[];export async function invoke(command,args,options){calls.push(structuredClone({command,args,options}));return {exitCode:0,stdout:'',stderr:''};}",hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const esbuild=require('esbuild'),built=await esbuild.build({stdin:{contents:"export {shell} from './src/bridge/api.ts';export {calls} from 'shell-wire-memory';",resolveDir:root,sourcefile:'shell-public-wire-entry.ts',loader:'ts'},absWorkingDir:root,bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'existing-shell-memory-ipc',setup(build){build.onResolve({filter:/^(shell-wire-memory|\.\/ipc)$/},()=>({path:'memory',namespace:'shell-wire-memory'}));build.onLoad({filter:/.*/,namespace:'shell-wire-memory'},()=>({contents:memory,loader:'js'}));}}]});await esbuild.stop();
 const bundle=path.join(out,'shell-public-wire.mjs');fs.writeFileSync(bundle,built.outputFiles[0].text,{flag:'wx'});fs.writeFileSync(path.join(out,'shell-public-wire.metafile.json'),JSON.stringify(built.metafile,null,2)+'\n',{flag:'wx'});const api=await import(pathToFileURL(bundle));
 const rows=[];for(const sequence of [0,12,Number.MAX_SAFE_INTEGER]){await api.shell.run('powercfg',['/setacvalueindex','owned'],5000,undefined,{expectedPowerGeneration:4,expectedPowerNotificationSequence:sequence});const call=api.calls.at(-1);assert.equal(call.command,'shell.run');assert.equal(call.args.expectedPowerNotificationSequence,sequence,'actual API must forward the captured ingress ticket');assert.equal(call.args.expectedPowerGeneration,4);assert.equal(call.options.timeoutMs,10000);rows.push({sequence,forwarded:true});}
 const before=api.calls.length;for(const value of [null,true,'1',-1,0.5,NaN,Infinity,Number.MAX_SAFE_INTEGER+1])assert.throws(()=>api.shell.run('powercfg',[],5000,undefined,{expectedPowerGeneration:4,expectedPowerNotificationSequence:value}),/SHELL_INTENT_INVALID_NOTIFICATION_SEQUENCE/);assert.equal(api.calls.length,before);
 await api.shell.run('owned-read',[],5000);assert.deepEqual(api.calls.at(-1),{command:'shell.run',args:{program:'owned-read',args:[],timeoutMs:5000},options:{timeoutMs:10000}});await api.shell.run('owned-read',[],5000,undefined,{expectedPowerGeneration:4});assert(!('expectedPowerNotificationSequence'in api.calls.at(-1).args));
 const inputs=Object.keys(built.metafile.inputs).filter(p=>p!=='shell-public-wire-entry.ts'&&!p.startsWith('shell-wire-memory:')).map(p=>({path:path.resolve(root,p),sha256:hash(path.resolve(root,p))}));
 const result={status:'PASS',forwarded:rows,invalidRejectedBeforeIpc:8,legacyPayloadPreserved:true,inputs,metafile:pinWire(path.join(out,'shell-public-wire.metafile.json')),bundle:pinWire(bundle),inertInvokeSource:memory,realNativeCommands:0};function pinWire(file){return{path:file,bytes:fs.statSync(file).size,sha256:hash(file)}}fs.writeFileSync(path.join(out,'shell-public-wire.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}

export async function runBrightnessOwner(out,args=[]){
 const root=process.cwd(),hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const choose=name=>args.find(value=>value.startsWith('--brightness-owner-'+name+'='))?.split('=')[1];
 const modes=['global','xbox','steam','xbox-steam'],sides=['ac','dc'],stages=['power-read','power-write','registry-read','scheme-read','activation'];
 const kinds=['revision','hash','mode','result','observed','ack','unrelated','current'],selected={mode:choose('mode'),side:choose('side'),stage:choose('stage'),kind:choose('kind')};
 for(const [name,values]of Object.entries({mode:modes,side:sides,stage:stages,kind:kinds}))assert(!selected[name]||values.includes(selected[name]),'Unknown brightness '+name);
 const ts=require('typescript'),source=fs.readFileSync(root+'/src/bridge/yeman.ts','utf8'),ast=ts.createSourceFile('yeman.ts',source,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 const functions=['setAcValueIndex','setDcValueIndex'].map(name=>{const nodes=ast.statements.filter(node=>ts.isFunctionDeclaration(node)&&node.name?.text===name);assert.equal(nodes.length,1,name);return nodes[0].getText(ast);});
 const memory=String.raw`
 import {assertBootPreferenceOwner} from ACTUAL_OWNER;
 export const state={settings:null,side:'ac',brightness:50,effects:[],requests:[],stage:'current',change:()=>{},ownerReads:0};
 export const PW={YEMAN:'1cb8b882-a900-4b9f-9bac-99d151e64441'};
 export async function loadSettings(){state.ownerReads++;return structuredClone(state.settings);}
 export async function detectPowerModeReliable(){if(state.stage==='power-read')state.change();return state.side;}
 export async function getActiveScheme(){if(state.stage==='scheme-read')state.change();return PW.YEMAN;}
 export async function invoke(command,args){
  if(command==='registry.read'){if(state.stage==='registry-read')state.change();return state.brightness;}
  if(command!=='shell.run')throw Error('Unexpected owned native command '+command);
  state.requests.push(structuredClone(args));
  const verb=args.args[0],activation=verb==='/setactive';
  if(state.stage===(activation?'activation':'power-write'))state.change();
  if(args.bootContext)assertBootPreferenceOwner(state.settings,{origin:'boot-mode',bootContext:args.bootContext});
  state.effects.push(activation?'activation':'write');if(!activation)state.brightness=Number(args.args.at(-1));
  return {exitCode:0,stdout:'',stderr:''};
 }
 `.replace('ACTUAL_OWNER',JSON.stringify(root+'/src/bridge/bootModeGlobalPreferences.ts'));
 const power="import {shell} from "+JSON.stringify(root+'/src/bridge/api.ts')+";export {PW,detectPowerModeReliable,getActiveScheme} from 'brightness-memory';import {PW} from 'brightness-memory';\n"+functions.join('\n');
 const built=await require('esbuild').build({stdin:{contents:"export * as platform from './src/bridge/bootModePlatform.ts';export {shell} from './src/bridge/api.ts';export {state} from 'brightness-memory';",resolveDir:root,sourcefile:'boot-brightness-owner.ts',loader:'ts'},
  absWorkingDir:root,bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'owned-brightness',setup(build){
   build.onResolve({filter:/^(brightness-memory|\.\/ipc|\.\/settingsRepository)$/},()=>({path:'memory',namespace:'owned-brightness'}));
   build.onResolve({filter:/^\.\/yeman$/},()=>({path:'power',namespace:'owned-brightness'}));
   build.onLoad({filter:/.*/,namespace:'owned-brightness'},args=>({contents:args.path==='power'?power:memory,loader:'ts',resolveDir:root}));
  }}]});
 const file=path.join(out,'brightness-owner.mjs');fs.writeFileSync(file,built.outputFiles[0].text,{flag:'wx'});
 const api=await import(pathToFileURL(file)),m=api.state,defaults=JSON.parse(fs.readFileSync(root+'/PowerControl/settings-defaults-v1.json','utf8'));
 const inputFiles=[...new Set([...Object.keys(built.metafile.inputs).filter(name=>!name.startsWith('owned-brightness:')&&!name.startsWith('boot-')).map(name=>path.resolve(root,name)),root+'/src/bridge/yeman.ts',root+'/PowerControl/settings-defaults-v1.json'])];
 const pins=inputFiles.map(file=>({path:file,sha256:hash(file)})),raw=path.join(out,'brightness-owner-observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));const rows=[];
 for(const mode of modes)for(const side of sides)for(const stage of stages)for(const kind of kinds){
  if(Object.entries(selected).some(([key,value])=>value&&({mode,side,stage,kind})[key]!==value))continue;
  m.settings=structuredClone(defaults);m.settings.input.revision=7;m.settings.input.startupObservedBoot='brightness-owned-boot';
  m.settings.extensions={bootModes:{schema:1,revision:3,current:mode,boot:mode},bootModesAppliedBoot:'brightness-owned-boot'};
  const context={input:structuredClone(m.settings.input),extensions:structuredClone(m.settings.extensions)};
  m.side=side;m.stage=stage;m.brightness=50;m.effects=[];m.requests=[];m.ownerReads=0;let changed=false,at=0;
  m.change=()=>{if(changed)return;changed=true;at=m.effects.length;
   if(kind==='revision')m.settings.input.revision++;
   if(kind==='hash')m.settings.input.diagnostics.inputLoggingEnabled=!m.settings.input.diagnostics.inputLoggingEnabled;
   if(kind==='mode')m.settings.extensions.bootModes.revision++;
   if(kind==='result')m.settings.extensions.bootModesStartupResult={schema:1,revision:99};
   if(kind==='observed')m.settings.input.startupObservedBoot='new-boot';
   if(kind==='ack')m.settings.input.hostAcknowledgedRevision=7;
   if(kind==='unrelated')m.settings.extensions.peer={keep:true};
  };
  let result,error='';try{result=await api.platform.applyBootBrightness(75,{origin:'boot-mode',bootContext:context});}catch(e){error=String(e);}
  const allowed=['ack','unrelated','current'].includes(kind),late=m.effects.slice(at),row={mode,side,stage,kind,changed,allowed,error,result,lateEffects:late,requests:m.requests,ownerReads:m.ownerReads};
  fs.appendFileSync(raw,JSON.stringify(row)+'\n');assert(changed,'Original brightness boundary not reached '+JSON.stringify(row));
  if(allowed){assert(!error,JSON.stringify(row));assert.equal(result.applied,true);assert(late.length>0);assert(m.requests.every(request=>request.bootContext),'Brightness context did not reach actual API');}
  else {assert.equal(late.length,0,'expired brightness command reached an effect: '+JSON.stringify(row));assert(error,'Expired brightness reply must not be accepted');}
  rows.push(row);
 }
 m.stage='current';m.change=()=>{};m.effects=[];m.requests=[];m.ownerReads=0;
 assert.equal((await api.platform.applyBootBrightness(75)).applied,true);assert.equal(m.ownerReads,0);assert(m.requests.every(request=>!('bootContext'in request)));
 for(const pin of pins)assert.equal(hash(pin.path),pin.sha256,'Brightness source changed during execution '+pin.path);
 const result={status:'PASS',actualRows:rows.length,selected,rows,ordinaryOwnerReads:0,sourcePins:pins,
  actualPowerWriterFragments:functions.map((code,index)=>({name:['setAcValueIndex','setDcValueIndex'][index],sha256:createHash('sha256').update(code).digest('hex')})),
  scope:'Actual brightness module, exact AC/DC functions, owner assertion and API; memory native terminal',realPowercfgOrHardwareOperations:0};
 fs.writeFileSync(path.join(out,'brightness-owner-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
