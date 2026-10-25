// Actual pinned GamepadMotion.dll, private Windows x64 child process, synthetic input only.
// node tools/controller_motion_binary_session_selftest.mjs [--out <fresh BinarySession child>]
// No YMCC app, HID/WinRT/Host, product config, registry/service/network or sleep operation.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash, randomBytes} from 'node:crypto';
import {spawn} from 'node:child_process';

const runner=fileURLToPath(import.meta.url), repo=path.resolve(path.dirname(runner),'..');
const fixtures=path.join(repo,'tools/fixtures/controller_motion_binary_session');
let workspace=repo;
while (!fs.existsSync(path.join(workspace,'Mainline/YeManCC-source/YeManCC/native'))) {
  const parent=path.dirname(workspace); if (parent===workspace) throw Error('Shared workspace root not found'); workspace=parent;
}
const allowed=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/BinarySession');
const args=process.argv.slice(2); let requested;
if (args.length) { if (args.length!==2 || args[0]!=='--out') throw Error('Usage: node tools/controller_motion_binary_session_selftest.mjs [--out <fresh child>]'); requested=args[1]; }
const out=path.resolve(requested || path.join(allowed,new Date().toISOString().replace(/[:.]/g,'-')+'-'+randomBytes(3).toString('hex')));
const inside=(base,target)=>{const rel=path.relative(base,target);return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
if (!inside(allowed,out)) throw Error('Output must be a fresh BinarySession child');
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
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),out,scope:'Actual production Session/Owner plus pinned native DLL; synthetic keys, clocks and samples',targetSha256:'e11d59f84b4a4a12e99b15ea998e79de06c6dfb8f1e9c637e973abd4d3d088a9',targetExecuted:false,targetExecutionAuthorized:true,targetExecutedOnlyInPrivateChild:true,productionEdited:false,normalYmccAppStarted:false,hardwareIo:false,network:false,registryServiceSettingsOrSleepOperations:false,completeInitializationSideEffectAbsenceProven:false,configurations:[],commands:[],processes:[]};
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
  if(pins.dll.sha256!==receipt.targetSha256||pins.dll.bytes!==25600)throw Error('Pinned binary identity mismatch');
  for(const ref of pins.references)if(sha(fs.readFileSync(path.join(fixtures,ref.file)))!==ref.sha256)throw Error('Fixed wrapper hash mismatch');
  const productionHeaders=['controller_motion_processor.h','controller_motion_session.h'].map(name=>path.join(repo,'native',name));
  const priorFixtures=path.join(repo,'tools/fixtures/gamepad_motion_binary');
  receipt.protectedInputs=[path.join(repo,'tools/gamepad_motion_binary_selftest.mjs'),...allFiles(priorFixtures)].map(file=>({path:file,sha256:sha(fs.readFileSync(file))}));
  receipt.inputs=[runner,...allFiles(fixtures),...productionHeaders].map(file=>({path:file,sha256:sha(fs.readFileSync(file)),bytes:fs.statSync(file).size}));
  for(const input of receipt.inputs){const rel=input.path===runner?'sources/controller_motion_binary_session_selftest.mjs':productionHeaders.includes(input.path)?'sources/native/'+path.basename(input.path):'sources/fixtures/'+path.relative(fixtures,input.path);save(rel,fs.readFileSync(input.path));}
  receipt.productionHeaders=productionHeaders.map(file=>({path:file,sha256:sha(fs.readFileSync(file)),compiledDirectlyInBaseline:true,snapshot:'sources/native/'+path.basename(file)}));
  receipt.realComponents=['Current production Session/Owner headers','Actual pinned DLL allocation, processing, mode and readback'];receipt.syntheticComponents=['Key tokens and timestamps','Raw samples and in-memory Calibration values','Manual borrower actions and callback address substitution'];receipt.scenarios=JSON.parse(fs.readFileSync(path.join(fixtures,'scenarios.json'),'utf8'));receipt.mutations=[];receipt.fullNativeStateOrAutoConvergenceProven=false;receipt.actualPbtPerformed=false;receipt.mainConsumerExecuted=false;
  const bytes=fs.readFileSync(pins.dll.defaultSource);if(sha(bytes)!==receipt.targetSha256||bytes.length!==25600)throw Error('Audited DLL changed');save('binary/GamepadMotion.dll',bytes);receipt.dll={origin:pins.dll.defaultSource,privatePath:path.join(out,'binary/GamepadMotion.dll'),sha256:sha(bytes),bytes:bytes.length};
  const msvc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207',sdk='C:/Program Files (x86)/Windows Kits/10',sdkVersion='10.0.26100.0';const bin=path.join(msvc,'bin/Hostx64/x64'),compiler=path.join(bin,'cl.exe'),linker=path.join(bin,'link.exe');
  const includes=[path.join(msvc,'include'),...['ucrt','shared','um','winrt','cppwinrt'].map(n=>path.join(sdk,'Include',sdkVersion,n))];const libs=[path.join(msvc,'lib/x64'),path.join(sdk,'Lib',sdkVersion,'ucrt/x64'),path.join(sdk,'Lib',sdkVersion,'um/x64')];
  for(const component of [compiler,linker,...includes,...libs])if(!fs.existsSync(component))throw Error('Missing existing toolchain component '+component);
  receipt.toolchain={compiler,compilerSha256:sha(fs.readFileSync(compiler)),linker,linkerSha256:sha(fs.readFileSync(linker)),sdkVersion,includeDirectories:includes,libraryDirectories:libs,environmentDumped:false};
  const temp=path.join(out,'temp');fs.mkdirSync(temp);const env={...process.env,PATH:bin+';'+process.env.PATH,INCLUDE:includes.join(';'),LIB:libs.join(';'),TEMP:temp,TMP:temp};const source=path.join(out,'sources/fixtures/helper.cpp');
  const configurations=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/Zi','/RTC1','/MTd']]];
  async function execute(name,flags,headerRoot,caseFilter,semanticExpectation,record){
    const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const exe=path.join(dir,'controller_motion_binary_session_helper.exe');
    const argv=['/nologo','/std:c++17','/utf-8','/EHsc','/W4','/WX','/fp:precise',...flags,source,'/I'+headerRoot,'/I'+path.dirname(source),'/Fo'+path.join(dir,'helper.obj'),'/Fd'+path.join(dir,'compiler.pdb'),'/Fe'+exe,'/link','/MACHINE:X64','/INCREMENTAL:NO','/PDB:'+path.join(dir,'helper.pdb'),'kernel32.lib','bcrypt.lib','psapi.lib'];
    record.name=name;record.status='compiling';record.headersCompiledFrom=headerRoot;record.compileArguments=argv;flush();
    const build=await bounded(compiler,argv,{cwd:dir,env,timeoutMs:90000,log:name+'/compile'});completed(build,name+' compile');record.compiled=true;record.executableSha256=sha(fs.readFileSync(exe));record.status='running';flush();
    const childArgs=[receipt.dll.privatePath,out,...(caseFilter?['--case',caseFilter]:[])];const run=await bounded(exe,childArgs,{cwd:dir,env,timeoutMs:30000,log:name+'/run'});
    let events=[];try{events=run.stdout.split(/\r?\n/).filter(Boolean).map(line=>JSON.parse(line));}catch(error){record.eventParseError=error.message;}
    save(name+'/events.json',events);receipt.targetExecuted ||= events.some(e=>e.type==='loaded');record.summary=events.findLast(e=>e.type==='result')??null;record.exitCode=run.exitCode;record.timedOut=run.timedOut;record.cases=events.filter(e=>e.type==='case-result');record.modules=events.filter(e=>e.type==='modules');record.assertionFailures=events.filter(e=>e.type==='assertion-failed');flush();
    if(run.timedOut||run.overflow||run.spawnError||record.eventParseError||!record.summary)throw Error(name+' runner/timeout/parse failure; not semantic detection');
    const s=record.summary;if(!s.cleanupComplete||!s.moduleUnloaded||s.created!==s.deleted||s.callbackViolations||s.fatal)throw Error(name+' cleanup/ABI/runtime gap; not semantic detection');
    const hashEvent=events.find(e=>e.type==='hash-verified'),loaded=events.find(e=>e.type==='loaded'),bindings=events.filter(e=>e.type==='binding');
    if(hashEvent?.sha256!==receipt.targetSha256||hashEvent.lockedAgainstWriteAndDelete!==true||!loaded||path.resolve(loaded.path).toLowerCase()!==path.resolve(receipt.dll.privatePath).toLowerCase())throw Error('Private DLL pin/load mismatch');
    if(bindings.length!==12||bindings.some(e=>e.rva!==e.expectedRva))throw Error('ABI bindings differ');
    save(name+'/modules.json',record.modules);
    if(semanticExpectation){
      if(run.exitCode!==1||s.status!=='fail'||!record.assertionFailures.some(e=>e.case===caseFilter&&e.label===semanticExpectation))throw Error(name+' lacks exact expected semantic failure');
      record.status='detected';record.semanticDetection={case:caseFilter,label:semanticExpectation,normalExitOne:true,cleanupComplete:true};console.log(name+': semantic mutant detected');
    }else{completed(run,name+' baseline');if(s.status!=='pass'||s.failures||s.cases!==9)throw Error(name+' numerical baseline failed');record.status='pass';console.log(name+': PASS '+s.cases+' cases / '+s.checks+' checks, '+s.created+'/'+s.deleted+' real handles cleaned');}
    flush();
  }
  for(const [name,flags]of configurations){const record={};receipt.configurations.push(record);await execute(name,flags,path.join(repo,'native'),null,null,record);}
  const sessionSource=fs.readFileSync(path.join(out,'sources/native/controller_motion_session.h'),'utf8'),ownerSource=fs.readFileSync(path.join(out,'sources/native/controller_motion_processor.h'),'utf8');
  function replaceOnce(source,from,to){if(source.split(from).length!==2)throw Error('Mutant anchor not unique: '+from);return source.replace(from,to);}
  const mutants=[
    {name:'same-key-resets-delta',file:'controller_motion_session.h',case:'same-key-stability',label:'same-key delta continuity',from:'if (!unchanged) {',to:'if (!unchanged || previousSeconds_ != nowSeconds) {'},
    {name:'borrow-invalidation-noop',file:'controller_motion_session.h',case:'manual-borrow-invalidation',label:'manual borrow restores software Auto preference via native readback',from:'void invalidateCalibrationMode() noexcept { calibrationMode_.reset(); }',to:'void invalidateCalibrationMode() noexcept { }'},
    {name:'Owner-bypasses-valid-wrong-key',file:'controller_motion_processor.h',case:'Owner-key-gate',label:'Owner wrong key has zero native process calls',from:'void* current = handleFor(key);',to:'void* current = key.valid() ? handle_ : nullptr;'}
  ];
  for(const mutant of mutants){
    const rel='mutations/'+mutant.name,headerRoot=path.join(out,rel,'native');fs.mkdirSync(headerRoot,{recursive:true});
    const session=mutant.file==='controller_motion_session.h'?replaceOnce(sessionSource,mutant.from,mutant.to):sessionSource;
    const owner=mutant.file==='controller_motion_processor.h'?replaceOnce(ownerSource,mutant.from,mutant.to):ownerSource;
    save(rel+'/native/controller_motion_session.h',session);save(rel+'/native/controller_motion_processor.h',owner);
    const m={name:mutant.name,status:'running',case:mutant.case,expectedSemanticLabel:mutant.label,mutatedHeader:mutant.file,mutatedSha256:sha(Buffer.from(mutant.file==='controller_motion_session.h'?session:owner)),configurations:[]};receipt.mutations.push(m);flush();
    for(const [name,flags]of configurations){const record={};m.configurations.push(record);await execute(rel+'/'+name,flags,headerRoot,mutant.case,mutant.label,record);}m.status='detected';flush();
  }
  for(const item of [...receipt.inputs,...receipt.protectedInputs])if(sha(fs.readFileSync(item.path))!==item.sha256)throw Error('Source/protected file changed: '+item.path);
  if(sha(fs.readFileSync(receipt.dll.privatePath))!==receipt.targetSha256)throw Error('Private DLL changed');
  receipt.status='pass';receipt.finishedAt=new Date().toISOString();receipt.runningChildProcesses=receipt.processes.filter(x=>!x.finished).map(x=>x.pid);receipt.protectedInputsUnchanged=true;flush();
  const report=['# Actual Controller Session/Owner + native binary sidecar','',
    'PASS: current production Session/Owner headers were included directly from native/ in both baselines, using the private SHA-pinned DLL. Ops/read thunks record calls and forward unchanged arguments to real native exports; no fake numerical engine is used.',
    '', 'DLL SHA256: '+receipt.targetSha256,'',
    '| Configuration | Cases | Checks | ProcessMotion | Player / world | Created / deleted |',
    '| --- | --- | --- | --- | --- | --- |',
    ...receipt.configurations.map(c=>'| '+c.name+' | '+c.summary.cases+' | '+c.summary.checks+' | '+c.summary.processCalls+' | '+c.summary.playerCalls+' / '+c.summary.worldCalls+' | '+c.summary.created+' / '+c.summary.deleted+' |'),
    '', 'Real vs synthetic boundary per scenario:',...receipt.scenarios.map(s=>'- '+s.name+': '+s.executed),
    '', 'Player/world ABI: fixed historical HC GamepadMotion.cs lines323/326 declare (IntPtr, out float, out float, float); x64 main GmGetSpace and pinned PE exports corroborate it. Session requests use1.41f/0.125f; returned values are checked against direct actual-DLL reads. No nonexistent getter is called.',
    '', 'Mode verification uses actual GetCalibrationMode. Session calibrationModeFor is used only as software-intent metadata. Manual borrower actions are synthetic actual-DLL mode/offset writes plus real Session invalidation/reconcile; the main CalibrationTransaction/registry path is not executed.',
    '', 'Generation changes and revoke are synthetic sleep-like sequences. They are not actual PBT, firmware time, full native-state restoration, main sleep/exit integration or disk persistence proof. Only the supplied in-memory Calibration value, resource retirement, current-token fencing, first/continuous dt and tested output fields are asserted.',
    '', 'Three evidence-only header mutants were compiled and normally exited1 with the exact expected assertion in Release and Checked: same-key time reset, lost manual invalidation, and Owner wrong-key bypass. Crash, timeout, build error, ABI gap or failed cleanup cannot count as semantic detection.',
    '', 'Every Session/Owner scope is revoked/destructed before FreeLibrary; actual create/delete forwarding counts balance, callback violations are zero, and the target is absent from the post-unload current-process module list. DLL/CRT initialization still executes; absence of all initialization/transitive effects is not claimed.',
    '', 'Reproducibility: RESULT.json records original production header hashes, source snapshots, toolchain/compiler commands, module/dependency paths, executable hashes, fixed C# references and original-runner/fixture protected hashes. Both helper and compile commands have timeouts. SOURCES and mutations exist only in this run.',
    '', 'Not executed: YMCC normal app, HID/WinRT/Host, device input, real sleep, settings/registry/services/network or main integration. Full native auto state and convergence remain unproven; only finite observed software reads are used. Neptune remains deferred.',
    '', 'All child/compiler processes have finished. Original binary runner/fixtures and production headers were hash-checked unchanged.'];
  save('REPORT.md',report.join('\n')+'\n');
  const files=allFiles(out).filter(f=>!f.endsWith(path.sep+'FILES-SHA256.json')).map(file=>({file:path.relative(out,file).replaceAll('\\','/'),bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))}));save('FILES-SHA256.json',{schema:1,createdAt:new Date().toISOString(),selfExcluded:'FILES-SHA256.json',files});
  console.log('PASS. Evidence: '+out);
}catch(error){
  for(const config of receipt.configurations)if(config.status==='running'||config.status==='compiling'){config.status='fail';config.error=error.message;}
  receipt.status='fail';receipt.error=error.message;receipt.finishedAt=new Date().toISOString();receipt.runningChildProcesses=receipt.processes.filter(x=>!x.finished).map(x=>x.pid);flush();save('FAILURE.md','# Retained failed Session/binary run\n\n'+error.message+'\n\nNo crash, timeout or compiler failure is a semantic mutant detection. Production and earlier tests are outside this write scope.\n');console.error(error.stack||error.message);process.exitCode=1;
}
