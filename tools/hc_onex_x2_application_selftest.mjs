// Application composition test: real owner/ledger/UI/serial admission, memory
// Win32 mocks. Does not launch YMCC/Host, enumerate hardware or execute real IO.
// node tools/hc_onex_x2_application_selftest.mjs [--out <OneXApplication/Tests child>] [--vcvars <batch>] [--no-mutations]
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawn,spawnSync} from 'node:child_process';
import {extract,generated} from './fixtures/hc_onex_x2_application/extract.mjs';
import {writeHandoff} from './fixtures/hc_onex_x2_application/handoff.mjs';
const runner=fileURLToPath(import.meta.url), repo=path.resolve(path.dirname(runner),'..');
const fixtures=path.join(repo,'tools/fixtures/hc_onex_x2_application');
let workspace=repo;
while(!fs.existsSync(path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))){const next=path.dirname(workspace);if(next===workspace)throw Error('Workspace marker missing');workspace=next;}
const options={},args=process.argv.slice(2);
for(let i=0;i<args.length;++i){if(['--out','--vcvars'].includes(args[i])&&args[i+1])options[args[i].slice(2)]=args[++i];else if(args[i]==='--no-mutations')options.noMutations=true;else throw Error(`Unknown/incomplete argument: ${args[i]}`);}
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/OneXApplication/Tests');
const out=path.resolve(options.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-')));
const inside=(base,target)=>{const rel=path.relative(base,target);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith(`..${path.sep}`));};
if(!inside(root,out)||root===out)throw Error('Use a fresh child of OneXApplication/Tests');
fs.mkdirSync(root,{recursive:true});const realRoot=fs.realpathSync(root);if(path.resolve(root)!==realRoot)throw Error('Validation root redirected');
let parent=out;while(!fs.existsSync(parent))parent=path.dirname(parent);if(!inside(realRoot,fs.realpathSync(parent)))throw Error('Output parent redirected');
fs.mkdirSync(out,{recursive:true});if(fs.readdirSync(out).length)throw Error('Output directory must be empty');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const receipt={schemaVersion:1,status:'running',startedAt:new Date().toISOString(),scope:'actual application owner, ledger, Mini keyboard, Home UI and queued serial guards composed with real headers and memory Win32 boundary',realDeviceApiCalls:0,hardwareAccess:false,network:false,deployment:false,configurations:[],mutations:[]};
const save=()=>fs.writeFileSync(path.join(out,'source-bindings.json'),JSON.stringify(receipt,null,2)+'\n');save();
const record=(log,result)=>{fs.writeFileSync(path.join(out,log),(result.stdout||'')+(result.stderr||''));if(result.error)throw result.error;return result;};
const run=(exe,argv,log,cwd=out,env=process.env)=>{const r=record(log,spawnSync(exe,argv,{cwd,env,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:64*1024*1024}));if(r.status!==0)throw Error(`${log}: process exit ${r.status}`);return r.stdout;};
async function compile(argv,log,cwd,env){const r=await new Promise((resolve,reject)=>{const child=spawn('cl.exe',argv,{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='';const timer=setTimeout(()=>{child.kill();reject(Error(`${log}: compiler timeout`));},120000);child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);child.on('error',e=>{clearTimeout(timer);reject(e);});child.on('close',status=>{clearTimeout(timer);resolve({status,stdout,stderr});});});record(log,r);if(r.status!==0)throw Error(`${log}: compiler exit ${r.status}`);}
try{
  const headers=['onex_x2_windows.h','onex_x2_session.h','onex_x2_protocol.h'];
  const fixtureNames=['extract.mjs','handoff.mjs','boundary.h','harness.cpp','stubs.cpp.txt','README.md'];
  const mainPath=path.join(repo,'native/main.cpp'),beforePath=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/OneXApplication/main.before-application.cpp');
  const oldMock=path.join(repo,'tools/fixtures/hc_onex_x2_windows/mock_win32.h'),jsonPath=path.join(repo,'deps/json/json.hpp');
  receipt.inputs=[runner,mainPath,beforePath,oldMock,jsonPath,...headers.map(x=>path.join(repo,'native',x)),...fixtureNames.map(x=>path.join(fixtures,x))].map(p=>({path:p,sha256:sha(fs.readFileSync(p))}));
  for(const name of headers)fs.copyFileSync(path.join(repo,'native',name),path.join(out,name));
  for(const name of fixtureNames)fs.copyFileSync(path.join(fixtures,name),path.join(out,name));
  const main=fs.readFileSync(mainPath,'utf8'),before=fs.readFileSync(beforePath,'utf8');
  fs.writeFileSync(path.join(out,'main.snapshot.cpp'),main);fs.copyFileSync(oldMock,path.join(out,'old_mock_win32.h'));fs.copyFileSync(jsonPath,path.join(out,'json.hpp'));
  const parts=extract(main,before),stubText=fs.readFileSync(path.join(fixtures,'stubs.cpp.txt'),'utf8');
  const slots={};for(const marker of ['environment','forward','action','ui','observation','ownerObservation']){const match=stubText.match(new RegExp(`// @${marker}\\r?\\n([\\s\\S]*?)(?=// @|$)`));if(!match)throw Error(`Missing stub boundary ${marker}`);slots[marker]=match[1];}
  const production=generated(parts,slots);fs.writeFileSync(path.join(out,'production.generated.h'),production);
  receipt.application={...parts.evidence,productionSha256:sha(production),backendBoundary:'fake ready/power/topology inputs; action side effects, serial queue storage, old ROG consumer, refresh and unrelated keyboard maintenance/policy side effects mocked; actual rear evaluator, owner, producer ledger, Mini chord evaluation/recompute, masks, Home dispatch, drain and serial job guards execute; rear observation wrapper forwards to the actual evaluator',realDeviceIo:false};
  const source=fs.readFileSync(path.join(out,'onex_x2_windows.h'),'utf8');
  const mockedApis=[...fs.readFileSync(path.join(out,'old_mock_win32.h'),'utf8').matchAll(/^#define (\w+) mock::/gm)].map(m=>m[1]).concat(['GetTickCount64','PostMessageW','RegisterDeviceNotificationW','UnregisterDeviceNotification','CompareStringOrdinal','GetAsyncKeyState']);
  const calls=[...new Set([...source.matchAll(/\b([A-Z][A-Za-z0-9_]*)\s*\(/g)].map(m=>m[1]))];
  const applicationCode=production.replace(/\/\*[\s\S]*?\*\/|\/\/[^\r\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,' ');
  const applicationCalls=[...new Set([...applicationCode.matchAll(/\b([A-Z][A-Za-z0-9_]*)\s*\(/g)].map(m=>m[1]))];
  const unreviewed=applicationCalls.filter(c=>!mockedApis.includes(c)&&c!=='IsEqualGUID');
  if(unreviewed.length)throw Error(`Unmocked application API boundary: ${unreviewed}`);
  const extra=calls.filter(c=>!mockedApis.includes(c)&&!['WindowsTransport','HasOverlappedIoCompleted'].includes(c));
  if(extra.length)throw Error(`Unreviewed production API boundary: ${extra}`);
  receipt.apiBoundary={mockedApis,productionCalls:calls,applicationCalls,sdkMemoryHelpers:['IsEqualGUID'],unmockedSdkCalls:[],sdkMacro:'HasOverlappedIoCompleted (real OVERLAPPED::Internal)',allWaitTimeoutsAssertedZero:true};
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
  const common=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/wd4189','/wd4505'];
  const configurations=[['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]];
  const forbidden=new RegExp(`UNDEF[^\\r\\n]*(?:__imp_)?(?:${mockedApis.join('|')}|DeviceIoControl|LoadLibrary[AW]?|GetProcAddress)(?:$|[\\s@])`,'m');
  for(const [name,flags] of configurations){
    const dir=path.join(out,name);fs.mkdirSync(dir);const exe=path.join(dir,'onex_application.exe'),obj=path.join(dir,'harness.obj');
    const argv=[...common,...flags,path.join(out,'harness.cpp'),`/Fo${obj}`,`/Fd${path.join(dir,'harness.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO','/NODEFAULTLIB:hid.lib','/NODEFAULTLIB:setupapi.lib'];
    await compile(argv,`${name}/compile.log`,dir,env);
    const symbols=run('dumpbin.exe',['/symbols',obj],`${name}/object-symbols.log`,dir,env);if(forbidden.test(symbols))throw Error(`${name}: unmocked device API external reference`);
    const imports=run('dumpbin.exe',['/imports',exe],`${name}/imports.log`,dir,env);
    const hazardous=['SendInput','ShellExecuteW','CreateProcessW','SetupDiGetClassDevsW','SetupDiEnumDeviceInterfaces','SetupDiGetDeviceInterfaceDetailW','HidD_GetHidGuid','HidD_GetAttributes','HidP_GetCaps','CreateFileW','ReadFile','WriteFile','CancelIoEx','GetOverlappedResult','RegisterDeviceNotificationW','UnregisterDeviceNotification','CompareStringOrdinal','GetAsyncKeyState'];
    for(const api of hazardous)if(new RegExp(`\\b${api}\\b`).test(imports))throw Error(`${name}: real device API import ${api}`);
    const config={name,status:'running',compileArguments:argv,executableSha256:sha(fs.readFileSync(exe)),objectSha256:sha(fs.readFileSync(obj)),deviceApiUndefinedSymbols:0,deviceApiImports:0,checks:0,cases:[]};receipt.configurations.push(config);save();
    for(const scenario of cases){const result=record(`${name}/${scenario}.log`,spawnSync(exe,[scenario],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:15000}));let report;try{report=JSON.parse(result.stdout.trim());}catch{report={status:'invalid-report',stdout:result.stdout,stderr:result.stderr};}config.cases.push({...report,exitCode:result.status});config.checks+=report.checks||0;save();console.log(`${name} ${scenario}: ${report.status} (${report.checks||0})`);}
    config.status=config.cases.every(c=>c.status==='pass'&&c.exitCode===0)?'pass':'fail';save();
  }
  const failures=receipt.configurations.flatMap(c=>c.cases.filter(x=>x.status!=='pass').map(x=>({configuration:c.name,...x})));
  if(failures.length){receipt.failures=failures;throw Error(`${failures.length} compiled semantic case failures; see per-case logs (production headers untouched)`);}
  if(!options.noMutations){
    const controls=[
      {name:'current-without-context',from:'&& oemProducerContextCurrent(source);',to:'&& true;',scenario:'context-fences',section:'context-fence'},
      {name:'vendor-keyboard-duplicated',from:'event->kind == EventKind::Click && event->button == Button::Home',to:'event->kind == EventKind::Click',scenario:'keyboard-owned',section:'keyboard-owner'},
      {name:'mini-vendor-rear-admitted',from:'&& machine != Model::X2MiniPro',to:'',scenario:'mini-keyboard',section:'mini-rear'},
      {name:'owner-stop-forgets-revoke',from:'g_onexX2Transport.stop(GetTickCount64()); onexX2RevokeSources();',to:'g_onexX2Transport.stop(GetTickCount64()); /* fault: no revoke */',occurrence:2,scenario:'stop-retention',section:'owner-stop'},
      {name:'home-routed-to-rog',from:'if (receipt.source == OemProducerId::OneXX2Home) nativeOnexX2HomeDispatch(receipt);',to:'if (receipt.source == OemProducerId::OneXX2Home) nativeRogClickDispatch(receipt);',scenario:'home-delayed',section:'home-ui'},
      {name:'fresh-rear-loses-established-neutral',from:'rule.needsNeutral = !freshOnexEdge;',to:'rule.needsNeutral = true;',scenario:'real-first-rear',section:'real-first-rear'},
      {name:'edited-rule-skips-required-up',from:'&& !rule.needsNeutral;',to:'&& true;',scenario:'real-rule-edit-rebind',section:'real-rule-edit'},
      {name:'held-fence-not-enforced',from:'armedMask &= ~g_onexX2RearReleaseFence;',to:'armedMask |= g_onexX2RearReleaseFence;',scenario:'real-held-rear',section:'real-held-rear'},
      {name:'second-write-permission-check-removed',header:'onex_x2_windows.h',from:'if (writePermission && !writePermission()) {',to:'if (false) {',scenario:'write-gate',section:'write-gate'},
      {name:'opposite-up-arms-unobserved-rear',from:'source == OemProducerId::OneXX2Rear ? observedMask : ~uint64_t{0}',to:'source == OemProducerId::OneXX2Rear ? ~uint64_t{0} : ~uint64_t{0}',scenario:'symmetric-real-up',section:'symmetric-real-up'},
      {name:'unknown-candidate-journal-not-recorded',from:'if (g_onexX2BindingObservation) {',to:'if (false) {',scenario:'bind-races',section:'bind-race'},
      {name:'candidate-path-not-published-before-read',from:'if (!current || !onexX2SameDevicePath(*current, path))',to:'if (false)',scenario:'bind-races',section:'bind-race'},
      {name:'owner-open-bind-recheck-removed',header:'onex_x2_windows.h',from:'if (bindPermission && !bindPermission(selected)) { lastError_ = ERROR_OPERATION_ABORTED; closeQuiescent(); return; }',to:'/* fault: no post-owner bind permission check */',scenario:'bind-races',section:'bind-race'},
      {name:'binding-journal-overflow-admitted',from:'!g_onexX2BindingObservation || g_onexX2BindingOverflow || path.empty() ||',to:'!g_onexX2BindingObservation || path.empty() ||',scenario:'binding-journal',section:'bind-journal'},
      {name:'posttick-revoke-clears-candidate-path',from:'onexX2RevokeSources(state.phase != Phase::Live);',to:'onexX2RevokeSources(true);',scenario:'posttick-source-revoke-race',section:'posttick-source-race'},
      {name:'home-queuefull-not-stopped',from:'else { g_onexX2Transport.stop(now); onexX2RevokeSources(); break; }',to:'else { /* fault: refusal leaves source live */ }',scenario:'queue-full',section:'queue-full'},
    ].map(c=>({header:'production.generated.h',...c}));
    for(const c of controls){const dir=path.join(out,'mutations',c.name);fs.mkdirSync(dir,{recursive:true});for(const name of [...headers,...fixtureNames,'old_mock_win32.h','json.hpp','production.generated.h'])fs.copyFileSync(path.join(out,name),path.join(dir,name));
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
  receipt.drift=receipt.inputs.filter(i=>sha(fs.readFileSync(i.path))!==i.sha256);
  const material=receipt.drift.filter(i=>i.path!==mainPath);
  if(receipt.drift.some(i=>i.path===mainPath)){const current=extract(fs.readFileSync(mainPath,'utf8'),fs.readFileSync(beforePath,'utf8'));const stable=JSON.stringify(current.evidence.ranges.map(r=>[r.name,r.sha256]))===JSON.stringify(parts.evidence.ranges.map(r=>[r.name,r.sha256]));receipt.application.unrelatedMainFileDrift={observed:true,allBoundRangesStable:stable};if(!stable)material.push({path:mainPath});}
  if(material.length)throw Error('Bound source or fixture changed since snapshot; rerun with fresh output');
  receipt.status='pass';receipt.finishedAt=new Date().toISOString();save();writeHandoff(out,receipt);console.log(`Evidence: ${path.join(out,'source-bindings.json')}`);
}catch(error){receipt.status='fail';receipt.error=error.message;receipt.finishedAt=new Date().toISOString();save();console.error(error.stack||error.message);process.exitCode=1;}
