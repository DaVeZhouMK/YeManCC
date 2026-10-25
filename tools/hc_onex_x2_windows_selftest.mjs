// Windows boundary test: actual production Session/WindowsTransport, only memory
// Win32 mocks. Does not launch YMCC/Host, enumerate hardware or execute real IO.
// node tools/hc_onex_x2_windows_selftest.mjs [--out <WindowsTests or Testing/YMCC/Evidence child>] [--vcvars <batch>] [--no-mutations]
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawn,spawnSync} from 'node:child_process';
const runner=fileURLToPath(import.meta.url), repo=path.resolve(path.dirname(runner),'..');
const fixtures=path.join(repo,'tools/fixtures/hc_onex_x2_windows');
let workspace=repo;
while(!fs.existsSync(path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))){const next=path.dirname(workspace);if(next===workspace)throw Error('Workspace marker missing');workspace=next;}
const options={},args=process.argv.slice(2);
for(let i=0;i<args.length;++i){if(['--out','--vcvars'].includes(args[i])&&args[i+1])options[args[i].slice(2)]=args[++i];else if(args[i]==='--no-mutations')options.noMutations=true;else throw Error(`Unknown/incomplete argument: ${args[i]}`);}
const legacyRoot=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/OneXRuntime/WindowsTests');
const evidenceRoot=path.join(workspace,'Testing/YMCC/Evidence');
const out=path.resolve(options.out||path.join(legacyRoot,new Date().toISOString().replace(/[:.]/g,'-')));
const inside=(base,target)=>{const rel=path.relative(base,target);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith(`..${path.sep}`));};
const root=[legacyRoot,evidenceRoot].find(base=>inside(base,out)&&base!==out);
if(!root)throw Error('Use a fresh child of OneXRuntime/WindowsTests or Testing/YMCC/Evidence');
fs.mkdirSync(root,{recursive:true});const realRoot=fs.realpathSync(root);if(path.resolve(root)!==realRoot)throw Error('Validation root redirected');
let parent=out;while(!fs.existsSync(parent))parent=path.dirname(parent);if(!inside(realRoot,fs.realpathSync(parent)))throw Error('Output parent redirected');
fs.mkdirSync(out,{recursive:true});if(fs.readdirSync(out).length)throw Error('Output directory must be empty');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const receipt={schemaVersion:1,status:'running',startedAt:new Date().toISOString(),scope:'real production headers, SDK types and all device Win32 APIs memory mocked',realDeviceApiCalls:0,hardwareAccess:false,network:false,deployment:false,configurations:[],mutations:[]};
const save=()=>fs.writeFileSync(path.join(out,'source-bindings.json'),JSON.stringify(receipt,null,2)+'\n');save();
const record=(log,result)=>{fs.writeFileSync(path.join(out,log),(result.stdout||'')+(result.stderr||''));if(result.error)throw result.error;return result;};
const run=(exe,argv,log,cwd=out,env=process.env)=>{const r=record(log,spawnSync(exe,argv,{cwd,env,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:8*1024*1024}));if(r.status!==0)throw Error(`${log}: process exit ${r.status}`);return r.stdout;};
async function compile(argv,log,cwd,env){const r=await new Promise((resolve,reject)=>{const child=spawn('cl.exe',argv,{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='';const timer=setTimeout(()=>{child.kill();reject(Error(`${log}: compiler timeout`));},120000);child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);child.on('error',e=>{clearTimeout(timer);reject(e);});child.on('close',status=>{clearTimeout(timer);resolve({status,stdout,stderr});});});record(log,r);if(r.status!==0)throw Error(`${log}: compiler exit ${r.status}`);}
try{
  const headers=['onex_x2_windows.h','onex_x2_session.h','onex_x2_protocol.h'];
  const fixtureNames=['mock_win32.h','harness.cpp','layout.generated.h','README.md'];
  receipt.inputs=[runner,...headers.map(x=>path.join(repo,'native',x)),...fixtureNames.map(x=>path.join(fixtures,x))].map(p=>({path:p,sha256:sha(fs.readFileSync(p))}));
  for(const name of headers)fs.copyFileSync(path.join(repo,'native',name),path.join(out,name));
  for(const name of fixtureNames)fs.copyFileSync(path.join(fixtures,name),path.join(out,name));
  const source=fs.readFileSync(path.join(out,'onex_x2_windows.h'),'utf8');
  const mockedApis=[...fs.readFileSync(path.join(out,'mock_win32.h'),'utf8').matchAll(/^#define (\w+) mock::/gm)].map(m=>m[1]);
  const calls=[...new Set([...source.matchAll(/\b([A-Z][A-Za-z0-9_]*)\s*\(/g)].map(m=>m[1]))];
  const extra=calls.filter(c=>!mockedApis.includes(c)&&!['WindowsTransport','HasOverlappedIoCompleted'].includes(c));
  if(extra.length)throw Error(`Unreviewed production API boundary: ${extra}`);
  receipt.apiBoundary={mockedApis,productionCalls:calls,unmockedSdkCalls:[],sdkMacro:'HasOverlappedIoCompleted (real OVERLAPPED::Internal)',allWaitTimeoutsAssertedZero:true};
  const inventory=fs.readFileSync(path.join(out,'harness.cpp'),'utf8').match(/static const Case cases\[\]=\{([\s\S]*?)\n\};/);
  if(!inventory)throw Error('Scenario table missing');
  const tuple=/\{\s*"([^"]+)"\s*,\s*\w+\s*\}/g;
  const cases=[...inventory[1].matchAll(tuple)].map(m=>m[1]);
  if(cases.length<19||new Set(cases).size!==cases.length||cases.some(c=>!/^[-a-z0-9]+$/.test(c))||inventory[1].replace(tuple,'').replace(/[,\s]/g,''))throw Error('Scenario inventory incomplete/duplicated');receipt.scenarios=cases;
  let vcvars=options.vcvars;
  if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');const install=run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log').trim();vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
  if(!fs.existsSync(vcvars)||/[\r\n"%]/.test(vcvars))throw Error('MSVC environment batch unavailable/unsupported');
  const envCmd=path.join(out,'compiler-environment.cmd');if(/[&|<>^%\r\n"]/.test(envCmd))throw Error('Unsupported batch path');
  fs.writeFileSync(envCmd,`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  const er=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envCmd],{windowsHide:true,encoding:'utf8',timeout:30000});if(er.error||er.status!==0)throw Error('MSVC setup failed');
  const env={...process.env};for(const line of er.stdout.split(/\r?\n/)){const at=line.indexOf('=');if(at<=0)continue;const key=line.slice(0,at),old=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[old||key]=line.slice(at+1);}
  const temp=path.join(out,'compiler-temp');fs.mkdirSync(temp);for(const key of ['TEMP','TMP']){const old=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[old||key]=temp;}
  receipt.compiler={vcvars,vcToolsVersion:env.VCToolsVersion,windowsSdkVersion:env.WindowsSDKVersion};save();
  const common=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-'];
  const configurations=[['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]];
  const forbidden=new RegExp(`UNDEF[^\\r\\n]*(?:__imp_)?(?:${mockedApis.join('|')}|DeviceIoControl|LoadLibrary[AW]?|GetProcAddress)(?:$|[\\s@])`,'m');
  for(const [name,flags] of configurations){
    const dir=path.join(out,name);fs.mkdirSync(dir);const exe=path.join(dir,'onex_windows.exe'),obj=path.join(dir,'harness.obj');
    const argv=[...common,...flags,path.join(out,'harness.cpp'),`/Fo${obj}`,`/Fd${path.join(dir,'harness.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO','/NODEFAULTLIB:hid.lib','/NODEFAULTLIB:setupapi.lib'];
    await compile(argv,`${name}/compile.log`,dir,env);
    const symbols=run('dumpbin.exe',['/symbols',obj],`${name}/object-symbols.log`,dir,env);if(forbidden.test(symbols))throw Error(`${name}: unmocked device API external reference`);
    const imports=run('dumpbin.exe',['/imports',exe],`${name}/imports.log`,dir,env);
    const hazardous=['SetupDiGetClassDevsW','SetupDiEnumDeviceInterfaces','SetupDiGetDeviceInterfaceDetailW','HidD_GetHidGuid','HidD_GetAttributes','HidP_GetCaps','CreateFileW','ReadFile','WriteFile','CancelIoEx','GetOverlappedResult','CloseHandle','CreateEventW','ResetEvent','WaitForSingleObject'];
    for(const api of hazardous)if(new RegExp(`\\b${api}\\b`).test(imports))throw Error(`${name}: real device API import ${api}`);
    const config={name,status:'running',compileArguments:argv,executableSha256:sha(fs.readFileSync(exe)),objectSha256:sha(fs.readFileSync(obj)),deviceApiUndefinedSymbols:0,deviceApiImports:0,checks:0,cases:[]};receipt.configurations.push(config);save();
    for(const scenario of cases){const result=record(`${name}/${scenario}.log`,spawnSync(exe,[scenario],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:15000}));let report;try{report=JSON.parse(result.stdout.trim());}catch{report={status:'invalid-report',stdout:result.stdout,stderr:result.stderr};}config.cases.push({...report,exitCode:result.status});config.checks+=report.checks||0;save();console.log(`${name} ${scenario}: ${report.status} (${report.checks||0})`);}
    config.status=config.cases.every(c=>c.status==='pass'&&c.exitCode===0)?'pass':'fail';save();
  }
  const failures=receipt.configurations.flatMap(c=>c.cases.filter(x=>x.status!=='pass').map(x=>({configuration:c.name,...x})));
  if(failures.length){receipt.failures=failures;throw Error(`${failures.length} compiled semantic case failures; see per-case logs (production headers untouched)`);}
  if(!options.noMutations){
    const controls=[
      {name:'completion-phase-only',header:'onex_x2_windows.h',from:'return session_.snapshot().phase == Phase::Detached && !read_ && !write_ &&\n            device_ == INVALID_HANDLE_VALUE && !readEvent_ && !writeEvent_;',to:'return session_.snapshot().phase == Phase::Detached;',scenario:'completion-close-device',section:'completion-close'},
      {name:'completion-misses-read-event',header:'onex_x2_windows.h',from:'device_ == INVALID_HANDLE_VALUE && !readEvent_ && !writeEvent_;',to:'device_ == INVALID_HANDLE_VALUE && !writeEvent_;',scenario:'completion-close-read-event',section:'completion-close'},
      {name:'completion-misses-write-event',header:'onex_x2_windows.h',from:'device_ == INVALID_HANDLE_VALUE && !readEvent_ && !writeEvent_;',to:'device_ == INVALID_HANDLE_VALUE && !readEvent_;',scenario:'completion-close-write-event',section:'completion-close'},
      {name:'failed-device-close-drops-owner',header:'onex_x2_windows.h',from:'if (CloseHandle(device_)) device_ = INVALID_HANDLE_VALUE;',to:'if (CloseHandle(device_) || true) device_ = INVALID_HANDLE_VALUE;',scenario:'completion-close-device',section:'completion-close'},
      {name:'failed-read-event-close-drops-owner',header:'onex_x2_windows.h',from:'if (CloseHandle(readEvent_)) readEvent_ = nullptr;',to:'if (CloseHandle(readEvent_) || true) readEvent_ = nullptr;',scenario:'completion-close-read-event',section:'completion-close'},
      {name:'failed-write-event-close-drops-owner',header:'onex_x2_windows.h',from:'if (CloseHandle(writeEvent_)) writeEvent_ = nullptr;',to:'if (CloseHandle(writeEvent_) || true) writeEvent_ = nullptr;',scenario:'completion-close-write-event',section:'completion-close'},
      {name:'retirement-poll-calls-tick',header:'onex_x2_windows.h',from:'void pollRetirement(std::uint64_t now) noexcept {',to:'void pollRetirement(std::uint64_t now) noexcept { tick(now);',scenario:'completion-observation',section:'completion-observation'},
      {name:'retirement-poll-services-live',header:'onex_x2_windows.h',from:'if (session_.snapshot().phase == Phase::Live) return;',to:'/* fault: retirement poll services live IO */',scenario:'completion-live-poll',section:'completion-live-poll'},
      {name:'retained-event-allows-rebind',header:'onex_x2_windows.h',from:'state.phase == Phase::Detached && stopped() && session_.canBind(now)',to:'state.phase == Phase::Detached && session_.canBind(now)',scenario:'completion-blocks-rebind',section:'completion-rebind'},
      {name:'failed-probe-close-drops-owner',header:'onex_x2_windows.h',from:'device_ = probe; // Retain the failed probe close in the same owner slot.',to:'/* fault: failed probe owner lost */',scenario:'completion-close-probe',section:'completion-probe'},

      {name:'binding-permission-before-open-bypass',header:'onex_x2_windows.h',from:'        if (bindPermission && !bindPermission(selected)) { lastError_ = ERROR_OPERATION_ABORTED; return; }',to:'        /* fault: no candidate registration before open */',scenario:'binding-permission',section:'binding-permission'},
      {name:'binding-permission-after-open-bypass',header:'onex_x2_windows.h',from:'        if (bindPermission && !bindPermission(selected)) { lastError_ = ERROR_OPERATION_ABORTED; closeQuiescent(); return; }',to:'        /* fault: no recheck after owner open */',scenario:'binding-permission',section:'binding-permission'},

      {name:'write-gate-final-check-bypass',header:'onex_x2_windows.h',from:'        if (writePermission && !writePermission()) {',to:'        if (false) {',scenario:'write-permission-withdraw',section:'write-permission-withdraw'},
      {name:'write-withdraw-ticket-retained',header:'onex_x2_windows.h',from:'(void)session_.discardPreparedWrite(withdrawn);',to:'/* fault: unsubmitted ticket remains pending */',scenario:'write-permission-withdraw',section:'write-permission-withdraw'},
      {name:'closed-path-retained',header:'onex_x2_windows.h',from:'devicePath_.clear();',to:'/* fault: stale bound path retained */',scenario:'bound-path',section:'bound-path'},

      {name:'gamepad-collection-admitted',header:'onex_x2_protocol.h',from:'usagePage != 0xFF00 || usage != 0x0001',to:'(usagePage == 0xFFFF || usage == 0xFFFF)',scenario:'collection-filtering',section:'filtering'},
      {name:'submission-pretends-terminal',header:'onex_x2_windows.h',from:'} else if (GetLastError() != ERROR_IO_PENDING) {',to:'} else if (true) {',scenario:'initialization',section:'initialization',occurrence:2},
      {name:'pending-error-pretends-terminal',header:'onex_x2_windows.h',from:'error == ERROR_IO_INCOMPLETE || !HasOverlappedIoCompleted(&overlap)',to:'error == ERROR_IO_INCOMPLETE',scenario:'incomplete-result',section:'result-pending'},
      {name:'valid-layout-change-retains-old-contract',header:'onex_x2_windows.h',from:'if (layout != layout_) session_.stop(StopReason::SelectionChanged, now);',to:'/* fault: valid layout changes keep the old session */',scenario:'valid-layout-change',section:'layout-change'},
      {name:'quiescent-stop-defers-release',header:'onex_x2_windows.h',from:'cancel(); finalize(); }',to:'cancel(); }',scenario:'stop-immediate-release',section:'stop-quiescent'},
      {name:'status-misses-due-init-x2',header:'onex_x2_session.h',from:'settleInitialization(now); // A status read may precede advance on the exact final-page due tick.',to:'/* fault: due initialization is not settled by status */',scenario:'status-init-boundary-x2',section:'initialization-status-boundary'},
      {name:'status-misses-due-init-mini',header:'onex_x2_session.h',from:'settleInitialization(now); // A status read may precede advance on the exact final-page due tick.',to:'/* fault: due initialization is not settled by status */',scenario:'status-init-boundary-mini',section:'initialization-status-boundary'},
      {name:'status-runs-watchdog-before-write-terminal',header:'onex_x2_session.h',from:'settleInitialization(now); // A status read may precede advance on the exact final-page due tick.',to:'advance(now); // fault: full watchdog before transport write service',scenario:'status-write-watchdog-order',section:'status-watchdog-order'},
      {name:'blocking-wait',header:'onex_x2_windows.h',from:'WaitForSingleObject(event, 0)',to:'WaitForSingleObject(event, 1)',scenario:'initialization',section:'initialization'},
    ];
    for(const c of controls){const dir=path.join(out,'mutations',c.name);fs.mkdirSync(dir,{recursive:true});for(const name of [...headers,...fixtureNames])fs.copyFileSync(path.join(out,name),path.join(dir,name));
      const original=fs.readFileSync(path.join(dir,c.header),'utf8');let mutated;
      if(c.occurrence){const chunks=original.split(c.from);if(chunks.length!==c.occurrence+1)throw Error(`${c.name}: mutation anchor drift`);mutated=chunks.slice(0,-1).join(c.from)+c.to+chunks.at(-1);}
      else{if(original.split(c.from).length!==2)throw Error(`${c.name}: mutation anchor drift`);mutated=original.replace(c.from,c.to);}
      fs.writeFileSync(path.join(dir,c.header),mutated);
      const exe=path.join(dir,'mutant.exe'),argv=[...common,'/O2','/MD',path.join(dir,'harness.cpp'),`/Fo${path.join(dir,'harness.obj')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO','/NODEFAULTLIB:hid.lib','/NODEFAULTLIB:setupapi.lib'];
      await compile(argv,`mutations/${c.name}/compile.log`,dir,env);const r=record(`mutations/${c.name}/run.log`,spawnSync(exe,[c.scenario],{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:15000}));let report;try{report=JSON.parse(r.stdout.trim());}catch{throw Error(`${c.name}: not a structured semantic rejection`);}
      const detected=r.status===1&&report.status==='fail'&&report.section===c.section;
      receipt.mutations.push({name:c.name,status:detected?'detected':'invalid-control',compileArguments:argv,headerSha256:sha(mutated),executableSha256:sha(fs.readFileSync(exe)),report});save();
      if(!detected)throw Error(`${c.name}: wrong/absent semantic rejection`);console.log(`Mutation ${c.name}: rejected (${report.message})`);
    }
  }
  receipt.drift=receipt.inputs.filter(i=>sha(fs.readFileSync(i.path))!==i.sha256);if(receipt.drift.length)throw Error('Inputs changed since snapshot; use a fresh output and rerun');
  receipt.status='pass';receipt.finishedAt=new Date().toISOString();save();console.log(`Evidence: ${path.join(out,'source-bindings.json')}`);
}catch(error){receipt.status='fail';receipt.error=error.message;receipt.finishedAt=new Date().toISOString();save();console.error(error.stack||error.message);process.exitCode=1;}
