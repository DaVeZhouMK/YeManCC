import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {extract} from '../fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from '../fixtures/hc_reviewed_calibration/toolchain.mjs';

export async function runAutomaticRecoveryTransport({root,dir,apiInputs,actualBridge,runCutoffEpochMs=Date.now()+30000,compileOnly=false}) {
 assert(Number.isSafeInteger(runCutoffEpochMs)&&runCutoffEpochMs>Date.now(),"Valid parent execution cutoff required");
 const hash=x=>createHash('sha256').update(x).digest('hex'),pin=file=>({path:fs.realpathSync.native(file),bytes:fs.statSync(file).size,sha256:hash(fs.readFileSync(file))}),write=(name,obj)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(obj,null,2)+'\n',{flag:'wx'});
 const main=fs.readFileSync(root+'/native/main.cpp','utf8'),transport=fs.readFileSync(root+'/native/steam_live_cdp.h','utf8'),recovery=fs.readFileSync(root+'/native/sleep_steam_recovery.h','utf8'),originals={};
 const get=(source,name,kind)=>{
  // Analysis only: same-length signature normalization lets the existing parser
  // locate inline functions and skip empty default callbacks. Emit original bytes.
  const normalized=source.replace(/^inline(?=\s)/gm,'static').replace(/(&\s*[A-Za-z_][A-Za-z_0-9]*\s*=\s*)\{\}/g,'$1  ');
  assert.equal(Buffer.byteLength(normalized),Buffer.byteLength(source));
  const span=extract(normalized,name,kind),text=Buffer.from(source).subarray(span.startUtf8Byte,span.endUtf8ByteExclusive).toString('utf8');
  if(!kind)assert(/\)\s*(?:const\s*)?(?:noexcept\s*)?\{/.test(span.code),'Complete function body required before compiler '+name);
  originals[name]={text,sha256:hash(text)};return text;
 };
 const socket=get(transport,'BoundedSocket','struct');
 assert.equal(socket.split('        if (h) WinHttpCloseHandle(h);').length,2);
 const leaky=socket.replaceAll('BoundedSocket','LeakyBoundedSocket').replace('        if (h) WinHttpCloseHandle(h);','        // Semantic negative: final owned socket close removed.');
 const run=get(transport,'run'),socketLine='        BoundedSocket bounded(socket, socketBudget);';assert.equal(run.split(socketLine).length,2);
 const observed=run.replace(socketLine,'        WatchdogWitness witness; BoundedSocket bounded(socket, socketBudget); witness.bind(bounded.watchdog);');
 const negative=observed.replace('inline Json run(','inline Json runLeaky(').replace('BoundedSocket bounded(','LeakyBoundedSocket bounded(');
 assert.equal(observed.replace('        WatchdogWitness witness; BoundedSocket bounded(socket, socketBudget); witness.bind(bounded.watchdog);',socketLine),run);
 const sourceBlock=(name,first,last)=>{
  const a=transport.indexOf(first),b=transport.indexOf(last,a+first.length);
  assert(a>=0&&b>a&&transport.indexOf(first,a+first.length)<0,name);
  const text=transport.slice(a,b);originals[name]={text,sha256:hash(text)};return text;
 };
 const transportParts=[
  sourceBlock('requestPolicyAndTransportHolder','inline bool isRunningLinkRead(', 'struct HttpHandle {'),
  sourceBlock('httpHandleAndPath','struct HttpHandle {','inline std::wstring processImage('),
  sourceBlock('httpTargetResolution','inline std::wstring widenAscii(','struct BoundedSocket {'),
  sourceBlock('replyAndModeHelpers','inline bool replyEnvelopeValid(','inline Json run('),
 ];
 const callers=['liveRequest','resumeInputContext','reconcile'].map(name=>get(recovery,name));
 let source=fs.readFileSync(root+'/tools/boot_modes_fixture/automaticRecoveryTransport.cpp.in','utf8');
 const replace=(from,to)=>{assert.equal(source.split(from).length,2,from);source=source.replace(from,to);};
 replace('// ORIGINAL_NATIVE_POWER',['currentPowerGeneration','sleepSteamRecoveryAllows'].map(name=>get(main,name)).join('\n'));
 replace('// ORIGINAL_TRANSPORT',[...transportParts,socket,leaky,observed,negative].join('\n'));
 for(const [anchor,name]of [['// ORIGINAL_GAME','Game'],['// ORIGINAL_SESSION','Session'],['// ORIGINAL_FOREGROUND','Foreground']])replace(anchor,get(recovery,name,'struct'));
 replace('// ORIGINAL_NATIVE_CALLERS',callers.join('\n'));
 const cpp=path.join(dir,'transport.cpp'),exe=path.join(dir,'transport.exe');fs.writeFileSync(cpp,source,{flag:'wx'});
 const explicitTransportHeaders=['steam_listener_source.h','steam_session_observer.h','steam_request_policy.h','steam_command_coordinator.h','sleep_steam_command_attempt.h','steam_failure_diagnostics.h','steam_native_record_identity.h'].map(file=>'native/'+file);
 const inputs=[...new Set(['native/main.cpp','native/sleep_steam_recovery.h','native/steam_live_cdp.h','native/steam_endpoint_discovery.h','native/steam_native_return_evidence.h','native/sleep_recovery_diagnostics.h','deps/json/json.hpp','PowerControl/settings-defaults-v1.json','src/bridge/sleepRecovery.ts','tools/boot_modes_fixture/automaticRecoveryOwner.mjs','tools/boot_modes_fixture/automaticRecoveryTransport.mjs','tools/boot_modes_fixture/automaticRecoveryTransport.cpp.in','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',...explicitTransportHeaders,...apiInputs].map(file=>path.resolve(root,file)))].map(pin);
 write('transport-source-segments.json',{originals,actualBridgeSha256:hash(actualBridge),watchdogObservation:'Only a duplicate owned thread HANDLE before BoundedSocket declaration; witness checks signaled after actual BoundedSocket destructor joins. Reversible source insertion.','sameExecutableNegative':'Actual current BoundedSocket with only its final socket close removed, plus verbatim run copied under a new name/type. Current positive uses unchanged resource class.','IOBoundary':'All WinHTTP endpoints, listener/process/identity/game/inspection/foreground leaves inert. Fixed JS symbol is an owned marker; actual JS execution and main session binder are NOT_RUN. Actual native reconcile/input callers and liveRequest envelope are compiled.','timeout':'Actual default16000ms watchdog with real std::thread/condition_variable/GetTickCount64, no accelerated clock.'});
 const command=path.join(dir,'transport-compile.cmd');fs.writeFileSync(command,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'set "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\ncl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- /Od /DNDEBUG /MD /I"'+root+'/native" /I"'+root+'/deps/json" "'+cpp+'" /Fo"'+dir+'/transport.obj" /Fe"'+exe+'" /sourceDependencies "'+dir+'/transport-dependencies.json"\r\nexit /b %errorlevel%\r\n',{flag:'wx'});
 const execute=(label,program,args,timeout)=>{const at=performance.now(),p=spawnSync(program,args,{cwd:dir,env:process.env,windowsHide:true,encoding:'utf8',timeout,maxBuffer:16*1024*1024});for(const stream of ['stdout','stderr'])fs.writeFileSync(path.join(dir,label+'.'+stream+'.raw'),p[stream]??'',{flag:'wx'});write(label+'.process.json',{program,args,exitCode:p.status,error:p.error?.message??null,signal:p.signal,elapsedMs:performance.now()-at});assert(!p.error&&!p.signal,label+' did not complete');return p;};
 write('TASK.json',{originalCase:'Decky and wake password startup writes retain boot origin and failed preference save stays visible',selection:'automatic-native-transport',expectedRows:64,expectedControls:9,expectedNegativeRows:1,defaultActualReceiveTimeoutMs:16000,compileOnly,nativeAcceptanceGranted:false,inputs,generated:pin(cpp),fullNativeMain:false,scope:'Actual native lower callers, fixed envelope and complete native CDP run/RAII/socket watchdog. All OS identity and WinHTTP endpoints inert; no actual Steam/JS/desktop/device/power action.'});
 assert.equal(execute('transport-compile','cmd.exe',['/d','/c',command],22000).status,0,'Actual native transport compilation failed');
 const dependencyReport=path.join(dir,'transport-dependencies.json'),actualIncludes=new Set(JSON.parse(fs.readFileSync(dependencyReport,'utf8')).Data.Includes.map(file=>fs.realpathSync.native(file).toLowerCase()));
 for(const file of explicitTransportHeaders)assert(actualIncludes.has(fs.realpathSync.native(path.resolve(root,file)).toLowerCase()),'Missing actual transport include '+file);
 if(compileOnly){for(const item of inputs)assert.equal(pin(item.path).sha256,item.sha256);const result={status:'COMPILED_ONLY_NATIVE_SCOPE_NOT_RUN',selection:'automatic-native-transport',actualRows:0,actualControls:0,inputs,generated:pin(cpp),executable:pin(exe),fullRequiredRows:64,fullRequiredControls:9,actualDefaultDeadlineRequiredMs:16000,nativeAcceptanceGranted:false};write('COMPILE-RESULT.json',result);return result;}
 assert(runCutoffEpochMs-Date.now()>=18000,'Original remaining cutoff cannot contain all64 rows, actual16s deadline and negative; native not started');
 const raw=path.join(dir,'transport-observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
 const p=execute('transport-execute',exe,[path.join(dir,'api-wire.json'),'transport',raw],Math.min(19000,runCutoffEpochMs-Date.now()-300)),actual=JSON.parse(p.stdout);assert.equal(p.status,0,actual.error);assert.equal(actual.rows.length,64);assert.equal(actual.controls.length,9);
 const expected=new Set();for(const mode of ['global','xbox','steam','xbox-steam'])for(const operation of ['sleep.reconcile','sleep.input-resume'])for(const at of ['pre-send','after-send'])for(const kind of ['off','power-generation','suspend','exit'])expected.add([mode,operation,at,kind].join('|'));const observedKeys=new Set(actual.rows.map(x=>[x.mode,x.operation,x.position,x.cancel].join('|')));assert.equal(observedKeys.size,64);assert.deepEqual(observedKeys,expected);
 assert(actual.rows.every(x=>x.handlesRemaining===0&&x.opened===x.closed&&x.doubleCloses===0&&x.watchdogsJoined===1&&!x.reply.accepted));
 assert.equal(actual.controls[8].name,'actual-default-16s-blocking-receive-deadline');assert(actual.controls[8].elapsedMs>=15000&&actual.controls[8].elapsedMs<17500);
 const nraw=path.join(dir,'transport-negative-observations.jsonl');fs.closeSync(fs.openSync(nraw,'wx'));
 const n=execute('transport-negative',exe,[path.join(dir,'api-wire.json'),'transport-negative',nraw],2000),negativeResult=JSON.parse(n.stdout);assert.equal(n.status,1);assert.equal(negativeResult.error,'actual transport retained owned socket handle');assert.equal(negativeResult.rows.length,1);assert.equal(negativeResult.testPortHandlesBeforeFailureCleanup,1);assert.equal(negativeResult.testPortHandlesAfterFailureCleanup,0);
 for(const item of inputs)assert.equal(pin(item.path).sha256,item.sha256,'Transport source changed during producer '+item.path);
 const result={status:'PASS_SCOPED_NATIVE_TRANSPORT_CANCELLATION_AND_DEFAULT_DEADLINE',selection:'automatic-native-transport',actualRows:64,actualControls:9,sourceNegativeRows:1,actualDefaultDeadlineMs:actual.controls[8].elapsedMs,inputs,dependencyReport:pin(dependencyReport),generated:pin(cpp),apiWire:pin(path.join(dir,'api-wire.json')),actual:pin(path.join(dir,'transport-execute.stdout.raw')),observations:pin(raw),negative:pin(path.join(dir,'transport-negative.stdout.raw')),negativeObservations:pin(nraw),executable:pin(exe),actualSteamJsNetworkDeviceOrPowerActions:0,fullNativeMain:'NOT_RUN',mainSessionBinder:'NOT_RUN'};write('RESULT.json',result);return result;
}
