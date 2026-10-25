// Actual pinned GamepadMotion.dll, private Windows x64 child process, synthetic input only.
// node tools/gamepad_motion_binary_selftest.mjs [--out <fresh NativeBinaryExecution child>]
// No YMCC app, HID/WinRT/Host, product config, registry/service/network or sleep operation.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash, randomBytes} from 'node:crypto';
import {spawn} from 'node:child_process';

const runner=fileURLToPath(import.meta.url), repo=path.resolve(path.dirname(runner),'..');
const fixtures=path.join(repo,'tools/fixtures/gamepad_motion_binary');
let workspace=repo;
while (!fs.existsSync(path.join(workspace,'Mainline/YeManCC-source/YeManCC/native'))) {
  const parent=path.dirname(workspace); if (parent===workspace) throw Error('Shared workspace root not found'); workspace=parent;
}
const allowed=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/AutomaticCalibration/NativeBinaryExecution');
const args=process.argv.slice(2); let requested;
if (args.length) { if (args.length!==2 || args[0]!=='--out') throw Error('Usage: node tools/gamepad_motion_binary_selftest.mjs [--out <fresh child>]'); requested=args[1]; }
const out=path.resolve(requested || path.join(allowed,new Date().toISOString().replace(/[:.]/g,'-')+'-'+randomBytes(3).toString('hex')));
const inside=(base,target)=>{const rel=path.relative(base,target);return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
if (!inside(allowed,out)) throw Error('Output must be a fresh NativeBinaryExecution child');
if (process.platform!=='win32'||process.arch!=='x64') throw Error('Requires actual Windows x64 Node/MSVC');
fs.mkdirSync(allowed,{recursive:true});
if (fs.realpathSync(allowed).toLowerCase()!==path.resolve(allowed).toLowerCase()) throw Error('Evidence root is redirected');
let ancestor=path.dirname(out);while(!fs.existsSync(ancestor)) ancestor=path.dirname(ancestor);
const realAncestor=fs.realpathSync(ancestor);
if(realAncestor.toLowerCase()!==path.resolve(ancestor).toLowerCase()) throw Error('Evidence parent is redirected');
if (fs.existsSync(out)) throw Error('Choose fresh output; previous runs must be preserved');
fs.mkdirSync(out,{recursive:true});
const sha=value=>createHash('sha256').update(value).digest('hex');
const save=(rel,value)=>{const p=path.resolve(out,rel);if(!inside(out,p))throw Error('Write outside execution evidence');fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,typeof value==='string'||Buffer.isBuffer(value)?value:JSON.stringify(value,null,2)+'\n');};
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),out,scope:'Pinned native DLL CPU synthetic input in private helper only',targetSha256:'e11d59f84b4a4a12e99b15ea998e79de06c6dfb8f1e9c637e973abd4d3d088a9',targetExecuted:false,targetExecutionAuthorized:true,targetExecutedOnlyInPrivateChild:true,productionEdited:false,normalYmccAppStarted:false,hardwareIo:false,network:false,registryServiceSettingsOrSleepOperations:false,completeInitializationSideEffectAbsenceProven:false,configurations:[],commands:[],processes:[]};
const flush=()=>save('RESULT.json',receipt); flush();
function bounded(executable,argv,{cwd,env,timeoutMs,log}) {
  return new Promise(resolve=>{
    const start=Date.now(),child=spawn(executable,argv,{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});
    const record={pid:child.pid??null,executable,args:argv,startedAt:new Date().toISOString(),finished:false};receipt.processes.push(record);flush();
    let stdout='',stderr='',timedOut=false,overflow=false,spawnError=null;
    const timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutMs);
    const append=(channel,data)=>{if(channel==='out')stdout+=data.toString('utf8');else stderr+=data.toString('utf8');if(Buffer.byteLength(stdout)+Buffer.byteLength(stderr)>4*1024*1024){overflow=true;child.kill();}};
    child.stdout.on('data',b=>append('out',b));child.stderr.on('data',b=>append('err',b));child.on('error',error=>{spawnError=error.message;});
    child.on('close',(exitCode,signal)=>{
      clearTimeout(timer);record.finished=true;record.finishedAt=new Date().toISOString();
      const r={exitCode,signal,timedOut,overflow,spawnError,elapsedMs:Date.now()-start,stdout,stderr};
      save(log+'.stdout.txt',stdout);save(log+'.stderr.txt',stderr);
      receipt.commands.push({executable,args:argv,cwd,timeoutMs,log,exitCode,signal,timedOut,overflow,spawnError,elapsedMs:r.elapsedMs});flush();resolve(r);
    });
  });
}
function completed(r,label){if(r.exitCode!==0||r.timedOut||r.overflow||r.spawnError)throw Error(label+': '+JSON.stringify({exitCode:r.exitCode,signal:r.signal,timedOut:r.timedOut,spawnError:r.spawnError,overflow:r.overflow}));}
function allFiles(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(d=>d.isDirectory()?allFiles(path.join(dir,d.name)):[path.join(dir,d.name)]).sort();}
try {
  const pins=JSON.parse(fs.readFileSync(path.join(fixtures,'abi-pins.json'),'utf8'));
  if(pins.dll.sha256!==receipt.targetSha256||pins.dll.bytes!==25600)throw Error('Fixture binary pin changed unexpectedly');
  for(const ref of pins.references)if(sha(fs.readFileSync(path.join(fixtures,ref.file)))!==ref.sha256)throw Error('Fixed C# wrapper hash mismatch: '+ref.file);
  receipt.retainedPriorFailure=JSON.parse(fs.readFileSync(path.join(fixtures,'retained-failure.json'),'utf8'));
  receipt.inputs=[runner,...allFiles(fixtures)].map(file=>({path:file,sha256:sha(fs.readFileSync(file)),bytes:fs.statSync(file).size}));
  for(const input of receipt.inputs){const rel=input.path===runner?'sources/gamepad_motion_binary_selftest.mjs':'sources/fixtures/'+path.relative(fixtures,input.path);save(rel,fs.readFileSync(input.path));}
  const dllBytes=fs.readFileSync(pins.dll.defaultSource);if(dllBytes.length!==pins.dll.bytes||sha(dllBytes)!==receipt.targetSha256)throw Error('Audited DLL source hash/size mismatch');
  save('binary/GamepadMotion.dll',dllBytes);receipt.dll={origin:pins.dll.defaultSource,privatePath:path.join(out,'binary/GamepadMotion.dll'),bytes:dllBytes.length,sha256:sha(dllBytes),fixedHcCommits:pins.dll.fixedHcCommits};
  const msvc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207';
  const sdk='C:/Program Files (x86)/Windows Kits/10',sdkVersion='10.0.26100.0';
  const bin=path.join(msvc,'bin/Hostx64/x64'),compiler=path.join(bin,'cl.exe'),linker=path.join(bin,'link.exe');
  const includes=[path.join(msvc,'include'),...['ucrt','shared','um','winrt','cppwinrt'].map(n=>path.join(sdk,'Include',sdkVersion,n))];
  const libs=[path.join(msvc,'lib/x64'),path.join(sdk,'Lib',sdkVersion,'ucrt/x64'),path.join(sdk,'Lib',sdkVersion,'um/x64')];
  for(const item of [compiler,linker,...includes,...libs])if(!fs.existsSync(item))throw Error('Existing MSVC/SDK component missing: '+item);
  receipt.toolchain={compiler,compilerSha256:sha(fs.readFileSync(compiler)),linker,linkerSha256:sha(fs.readFileSync(linker)),sdkVersion,includeDirectories:includes,libraryDirectories:libs,fullEnvironmentDumped:false};
  const temp=path.join(out,'temp');fs.mkdirSync(temp);
  const env={...process.env,PATH:bin+';'+process.env.PATH,INCLUDE:includes.join(';'),LIB:libs.join(';'),TEMP:temp,TMP:temp};
  const source=path.join(out,'sources/fixtures/helper.cpp');flush();
  for(const [name,flags] of [['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/Zi','/RTC1','/MTd']]]) {
    const dir=path.join(out,name);fs.mkdirSync(dir);const executable=path.join(dir,'gamepad_motion_binary_helper.exe');
    const argv=['/nologo','/std:c++17','/utf-8','/EHsc','/W4','/WX','/fp:precise',...flags,source,'/I'+path.dirname(source),'/Fo'+path.join(dir,'helper.obj'),'/Fd'+path.join(dir,'compiler.pdb'),'/Fe'+executable,'/link','/MACHINE:X64','/INCREMENTAL:NO','/PDB:'+path.join(dir,'helper.pdb'),'kernel32.lib','bcrypt.lib','psapi.lib'];
    const config={name,status:'compiling',compileArguments:argv,helperTimeoutMs:30000};receipt.configurations.push(config);flush();
    const build=await bounded(compiler,argv,{cwd:dir,env,timeoutMs:90000,log:name+'/compile'});completed(build,name+' compile');
    config.compiled=true;config.executableSha256=sha(fs.readFileSync(executable));config.status='running';flush();
    const run=await bounded(executable,[receipt.dll.privatePath,out],{cwd:dir,env,timeoutMs:30000,log:name+'/run'});
    let events=[];try{events=run.stdout.split(/\r?\n/).filter(Boolean).map(line=>JSON.parse(line));}catch(error){config.eventParseError=error.message;}
    save(name+'/events.json',events);
    receipt.targetExecuted ||= events.some(x=>x.type==='loaded');
    const summary=events.findLast(x=>x.type==='result');config.summary=summary??null;config.exitCode=run.exitCode;config.timedOut=run.timedOut;flush();
    completed(run,name+' helper');if(!summary||summary.status!=='pass'||!summary.cleanupComplete||!summary.moduleUnloaded||summary.created!==summary.deleted||summary.autoConvergenceThresholdAsserted!==false)throw Error(name+' helper missing successful cleanup/result');
    const loaded=events.find(x=>x.type==='loaded');if(!loaded||path.resolve(loaded.path).toLowerCase()!==path.resolve(receipt.dll.privatePath).toLowerCase())throw Error(name+' actual loaded path differs from private DLL');
    const bindingEvents=events.filter(x=>x.type==='binding');if(bindingEvents.length!==10||bindingEvents.some(x=>x.rva!==x.expectedRva))throw Error(name+' ABI export binding mismatch');
    const actualHash=events.find(x=>x.type==='hash-verified');if(actualHash?.sha256!==receipt.targetSha256||actualHash.lockedAgainstWriteAndDelete!==true)throw Error(name+' helper did not verify/lock pinned bytes');
    config.modules=events.filter(x=>x.type==='modules');config.cases=events.filter(x=>x.type==='case-result');
    const observations=events.filter(x=>x.type==='auto-observation');if(observations.length<2)throw Error('No auto observations');
    config.autoObservation={checkpoints:observations.length,initial:observations[0],final:observations.at(-1),confidenceMin:Math.min(...observations.map(x=>x.confidence)),confidenceMax:Math.max(...observations.map(x=>x.confidence)),offsetComponentMin:[0,1,2].map(i=>Math.min(...observations.map(x=>x.offset[i]))),offsetComponentMax:[0,1,2].map(i=>Math.max(...observations.map(x=>x.offset[i]))),thresholdOrConvergenceDeadlineAsserted:false};
    save(name+'/auto-observations.json',observations);save(name+'/modules.json',config.modules);
    config.manualLiveBaseline=events.find(x=>x.type==='manual-live-baseline');config.status='pass';flush();
    console.log(name+': PASS '+summary.cases+' cases / '+summary.checks+' checks, '+summary.processed+' ProcessMotion calls, '+summary.created+'/'+summary.deleted+' handles cleaned; evidence '+dir);
  }
  for(const input of receipt.inputs)if(sha(fs.readFileSync(input.path))!==input.sha256)throw Error('Owned test input changed during run: '+input.path);
  if(sha(fs.readFileSync(receipt.dll.privatePath))!==receipt.targetSha256)throw Error('Private DLL bytes changed');
  receipt.status='pass';receipt.finishedAt=new Date().toISOString();receipt.runningChildProcesses=receipt.processes.filter(x=>!x.finished).map(x=>x.pid);flush();
  const lines=['# GamepadMotion binary execution sidecar','',
    'Result: '+receipt.status+'. Only the private pinned x64 GamepadMotion DLL was loaded by the evidence helper. No normal YMCC app or device transport was used.',
    '', 'DLL SHA256: '+receipt.targetSha256, '',
    '| Configuration | Cases | Checks | ProcessMotion calls | Created / deleted | Auto checkpoints |',
    '| --- | --- | --- | --- | --- | --- |',
    ...receipt.configurations.map(c=>'| '+c.name+' | '+c.summary.cases+' | '+c.summary.checks+' | '+c.summary.processed+' | '+c.summary.created+' / '+c.summary.deleted+' | '+c.autoObservation.checkpoints+' |'),
    '', 'Assertions: actual mode0/3 GetCalibrationMode readback; positive-weight offset roundtrip; legal zero calibration and zero samples/dt; Manual offset retention and gyro-minus-offset compensation; two simultaneous instances; retired/new instance state; Auto finite readbacks; Auto->Manual live offset retention; all handles deleted before FreeLibrary and target absent afterwards.',
    '', 'Auto input: 4096 biased-still + 2048 synthetic-changing-motion + 4096 biased-still-again samples, dt=0.004f. Synthetic time is supplied by the fixture; no sleep or physical sampling time is involved. No convergence confidence threshold/deadline or hardware accuracy claim is asserted.',
    '', ...receipt.configurations.map(c=>c.name+' observed Auto: confidence '+c.autoObservation.initial.confidence+' -> '+c.autoObservation.final.confidence+'; offset '+JSON.stringify(c.autoObservation.initial.offset)+' -> '+JSON.stringify(c.autoObservation.final.offset)+'. This is an observation, not a convergence gate.'),
    '', 'Reproducibility: sources/ snapshots, input source hashes, helper executable hashes, command lines, compiler/SDK identities, private DLL hash and fixed HC wrapper references are recorded in RESULT.json. Compile and helper timeouts are enforced separately; an abnormal exit/timeout is a failure, never counted as a semantic mutant result.',
    '', 'Module/dependency observations: Release/modules.json and Checked/modules.json contain current-helper module paths at helper start, before target load, after target load, and after target unload. API-set contracts may resolve into already-loaded ucrtbase.dll; the module list does not by itself map every transitive edge.',
    '', 'Boundary: LoadLibraryExW uses the absolute private path and LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32. The helper hashes and holds the DLL open without write/delete sharing before load. DLL/CRT initialization, BCrypt hashing and normal process/runtime operations have OS effects; complete absence of initialization or transitive effects is not claimed.',
    '', 'Retained first failure: '+receipt.retainedPriorFailure.run+'; six assertions overclaimed immediate raw-minus-offset for all-six-axis-zero input. Pinned native code explicitly returns early for all-zero axes. Corrected coverage verifies retained finite state/offset, plus compensation with zero gyro and nonzero gravity. This is a fixture correction, not a production fix or mutant detection.',
    '', 'Not executed: production Main/Session integration, hardware drift/accuracy, HID/WinRT/Host, settings/persistence, registry/services/network, deployment or real sleep. Native C++ source identity remains unknown; the exact distributed binary matches both fixed HC trees. Neptune remains deferred.',
    '', 'All helper/compiler processes started by this runner have completed. Previous failed runs, if any, are retained.'];
  save('REPORT.md',lines.join('\n')+'\n');
  const files=allFiles(out).filter(f=>!f.endsWith(path.sep+'FILES-SHA256.json')).map(file=>({file:path.relative(out,file).replaceAll('\\','/'),bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))}));
  save('FILES-SHA256.json',{schema:1,createdAt:new Date().toISOString(),selfExcluded:'FILES-SHA256.json',files});
  console.log('PASS. Evidence: '+out);
} catch(error) {
  for(const config of receipt.configurations) if(config.status==='running'||config.status==='compiling') {config.status='fail';config.error=error.message;}
  receipt.status='fail';receipt.error=error.message;receipt.finishedAt=new Date().toISOString();receipt.runningChildProcesses=receipt.processes.filter(x=>!x.finished).map(x=>x.pid);flush();
  save('FAILURE.md','# Retained failed binary selftest run\n\n'+error.message+'\n\nCompiler, runner or ABI/numerical failures are not semantic mutant detections. See RESULT.json and logs; no baseline expectation was silently lowered.\n');
  console.error(error.stack||error.message);process.exitCode=1;
}
