#!/usr/bin/env node
// Read-only source audit. The only child processes compile/run isolated value-only
// fixtures. Never loads HC/HIDMaestro, Steam, HID/DeviceIO, OEM or power APIs.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { lex, definition, declaration, lambdaForIpc, paired, classify, queryVerdict } from './fixtures/hc_steamdeck_physical_capability/source-graph.mjs';
import { runClassificationTests } from './fixtures/hc_steamdeck_physical_capability/classification-tests.mjs';
const script=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(script),'..');
const fixture=path.join(repo,'tools/fixtures/hc_steamdeck_physical_capability');
let workspace=repo;
while(!fs.existsSync(path.join(workspace,'AGENTS.md'))||!fs.existsSync(path.join(workspace,'Mainline/YeManCC-source/YeManCC'))) {
  const p=path.dirname(workspace);if(p===workspace)throw Error('Workspace root unavailable');workspace=p;
}
const evidence=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/SteamDeck');
const options={};
for(let i=2;i<process.argv.length;i++) {
  const arg=process.argv[i];
  if(arg==='--selftest-only')options.selftest=true;
  else if(arg==='--out'&&process.argv[i+1])options.out=process.argv[++i];
  else if(arg==='--allow-query-gap')options.allowQueryGap=true;
  else throw Error(`Unknown or incomplete option: ${arg}`);
}
if(options.selftest){console.log(JSON.stringify(runClassificationTests(),null,2));process.exit(0);}
const out=path.resolve(options.out||path.join(evidence,`run-${new Date().toISOString().replace(/[:.]/g,'-')}`));
function within(root,p){const rel=path.relative(root,p);return rel!==''&&!path.isAbsolute(rel)&&!rel.startsWith(`..${path.sep}`)&&rel!=='..';}
if(!within(evidence,out))throw Error('--out must be a new child of the authorized SteamDeck evidence directory');
// Reject symlink/junction escapes in existing parents before writing.
for(let p=out;p!==path.dirname(p);p=path.dirname(p))if(fs.existsSync(p)&&fs.lstatSync(p).isSymbolicLink())throw Error(`Symlink output ancestor: ${p}`);
if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('--out must be empty to prevent mixing evidence');
fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex');
const save=(name,data)=>fs.writeFileSync(path.join(out,name),typeof data==='string'?data:JSON.stringify(data,null,2)+'\n');
const inputs=new Map(),bindings=[];
function read(relative){if(!inputs.has(relative)){const b=fs.readFileSync(path.join(repo,relative));inputs.set(relative,b);}return inputs.get(relative).toString('utf8').replace(/^\uFEFF/,'');}
function bind(relative,name,{optional=false}={}){const f=definition(read(relative),name,{optional});if(!f)return null;bindings.push({path:relative,name,line:f.line,sha256:sha(f.code),start:f.start,end:f.end,kind:'syntactic-definition',offsetUnit:'UTF-16 in BOM-stripped snapshot text',code:f.code,calls:f.calls});return f;}
function hc(p){return `deps/handheldcompanion-runtime/source/${p}`;}
function run(command,args,log,env=process.env){const r=spawnSync(command,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:55000,maxBuffer:8*1024*1024,env});save(log,(r.stdout||'')+(r.stderr||'')+(r.error?.message||''));if(r.error||r.status!==0)throw Error(`${log}: exit ${r.status}; ${r.error?.message||''}`);return r.stdout;}
function walk(root,accept){if(!fs.existsSync(root))return [];return fs.readdirSync(root,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(d=>{if(d.isSymbolicLink())throw Error(`Unexpected source symlink ${d.name}`);const p=path.join(root,d.name);return d.isDirectory()?(['bin','obj','.git','node_modules'].includes(d.name)?[]:walk(p,accept)):accept(p)?[p]:[];});}
function baseline() {
  const lock=JSON.parse(read('tools/fixtures/hc_steamdeck_physical_capability/source-lock.json'));
  const bp='deps/handheldcompanion-runtime/HC-BASELINE.json',ip='deps/handheldcompanion-runtime/source-files.sha256';
  const b=JSON.parse(read(bp)),index=read(ip);
  if(sha(inputs.get(bp))!==lock.baselineSha256||sha(inputs.get(ip))!==lock.indexSha256||sha(inputs.get(ip)).toUpperCase()!==b.sourceFilesIndexSha256)throw Error('Pinned baseline/index mismatch');
  if(b.schemaVersion!==1||b.sourceDirectory!=='source'||b.sourceFilesIndex!=='source-files.sha256')throw Error('Unexpected HC baseline layout');
  const parseIndex=text=>{
    const map=new Map();for(const line of text.split(/\r?\n/).filter(x=>x.trim())){
      const m=/^([0-9a-fA-F]{64})  (.+)$/.exec(line);if(!m)throw Error('Bad hash index syntax');const p=m[2];
      if(path.isAbsolute(p)||p.includes('\\')||p.split('/').some(s=>s==='..'||s==='.')||map.has(p))throw Error(`Unsafe/duplicate index path: ${p}`);map.set(p,m[1].toLowerCase());
    }return map;
  };
  const expected=parseIndex(index),actual=walk(path.join(repo,'deps/handheldcompanion-runtime/source'),()=>true);
  if(expected.size!==b.sourceFileCount||actual.length!==expected.size)throw Error('Baseline file-count mismatch');
  const verified=[];for(const abs of actual){const p=path.relative(path.join(repo,'deps/handheldcompanion-runtime/source'),abs).replaceAll('\\','/');const hash=sha(fs.readFileSync(abs));if(hash!==expected.get(p))throw Error(`HC hash mismatch: ${p}`);verified.push({path:p,sha256:hash});}
  for(const [p,hash] of Object.entries(lock.sourceFiles))if(expected.get(p)!==hash||(read(hc(p)),sha(inputs.get(hc(p))))!==hash)throw Error(`Pinned primary source mismatch: ${p}`);
  for(const [p,hash] of Object.entries(b.coreFiles))if(expected.get(p)!==hash.toLowerCase())throw Error(`Core hash mismatch ${p}`);
  if(expected.get(b.projectFile)!==b.projectFileSha256.toLowerCase())throw Error('Project hash mismatch');
  const omitted=b.omittedFromFrozenReference,op=`deps/handheldcompanion-runtime/${omitted.index}`;const omittedMap=parseIndex(read(op));
  if(sha(inputs.get(op)).toUpperCase()!==omitted.indexSha256||omittedMap.size!==omitted.fileCount||[...omittedMap.keys()].some(p=>expected.has(p)))throw Error('Omitted index mismatch');
  const project=read(hc(b.projectFile));if(!project.includes(`<Version>${b.upstreamProjectVersion}</Version>`))throw Error('Project version mismatch');
  save('baseline-files-verified.json',verified);
  return {pass:true,baselineId:b.baselineId,version:b.upstreamProjectVersion,verifiedFiles:verified.length,baselineSha256:sha(inputs.get(bp)),indexSha256:sha(inputs.get(ip)),omittedIndexVerified:true,primaryPins:lock.sourceFiles,upstreamGitRevision:'UNKNOWN (fixed freeze manifest); not replaced by a newer HC checkout'};
}
function compileMapping() {
  const rel=hc('Controllers/Steam/NeptuneController.cs'),names=['Tick','HandleControllerInput','Open','Close','Gone'];
  const methods=names.map(name=>bind(rel,name));
  const tokens=lex(methods[0].code),members=prefix=>[...new Set(tokens.flatMap((t,i)=>t.value===prefix&&tokens[i+1]?.value==='.'?[tokens[i+2]?.value]:[]))].sort();
  // These enum names only label already-decoded values; numeric HID masks are UNKNOWN.
  const enums=`public enum NeptuneControllerButton { ${members('NeptuneControllerButton').join(',')} }\npublic enum NeptuneControllerAxis { ${members('NeptuneControllerAxis').join(',')} }\n`;
  const code=read('tools/fixtures/hc_steamdeck_physical_capability/MappingHarness.cs').replace('// @@PRODUCTION_METHODS@@',methods.map(x=>x.code).join('\n'))+'\n'+enums;
  save('Mapping.generated.cs',code);
  const dotnet=process.env.DOTNET_ROOT||'C:/Program Files/dotnet',sdk=fs.readdirSync(path.join(dotnet,'sdk')).sort((a,b)=>a.localeCompare(b,undefined,{numeric:true})).at(-1);
  const refVersion=fs.readdirSync(path.join(dotnet,'packs/Microsoft.NETCore.App.Ref')).filter(v=>v.startsWith('10.')).sort((a,b)=>a.localeCompare(b,undefined,{numeric:true})).at(-1);
  if(!sdk||!refVersion)throw Error('Offline .NET 10 compiler/ref pack unavailable');
  const refs=path.join(dotnet,'packs/Microsoft.NETCore.App.Ref',refVersion,'ref/net10.0');
  const copied=[];for(const name of ['ButtonFlags.cs','AxisFlags.cs']){const rel=hc(`Inputs/${name}`),dest=path.join(out,name);fs.writeFileSync(dest,inputs.get(rel)||Buffer.from(read(rel)));copied.push(dest);}
  const dll=path.join(out,'Mapping.dll');
  run(path.join(dotnet,'dotnet.exe'),[path.join(dotnet,'sdk',sdk,'Roslyn/bincore/csc.dll'),'/nologo','/target:exe','/langversion:preview','/nullable:enable',`/out:${dll}`,...fs.readdirSync(refs).filter(n=>n.endsWith('.dll')).map(n=>`/r:${path.join(refs,n)}`),path.join(out,'Mapping.generated.cs'),...copied],'mapping-compile.log');
  save('Mapping.runtimeconfig.json',{runtimeOptions:{tfm:'net10.0',framework:{name:'Microsoft.NETCore.App',version:'10.0.0'}}});
  const result=JSON.parse(run(path.join(dotnet,'dotnet.exe'),[dll],'mapping-run.log'));
  if(result.pass!==true)throw Error('Mapping tests failed');
  return {...result,compilerSdk:sdk,referencePack:refVersion,packetDecodeTested:false,assemblySha256:sha(fs.readFileSync(dll))};
}
function compileQuery() {
  const source=read('native/main.cpp'),code=[];
  for(const name of ['YmccFamily','OemKeyForm','OemKeyDef','OemCatalogRef'])code.push(declaration(source,name));
  const t=lex(source);
  for(let i=2;i<t.length-5;i++)if(t[i].value==='OemKeyDef'&&t[i-1].value==='const'&&t[i-2].value==='static'&&t[i+2]?.value==='[') {
    let b=i;while(t[b]?.value!=='{')b++;const e=paired(t,b);if(t[e+1]?.value!==';')throw Error('OEM table boundary changed');code.push(source.slice(t[i-2].start,t[e+1].end));
  }
  for(const name of ['ymccFamilyName','oemCatalogOf','oemCatalogForFamily','oemKeyCaptureImplemented','oemKeyCaptureRoute']) {const f=bind('native/main.cpp',name,{optional:name==='oemKeyCaptureRoute'});if(f)code.push(f.code);}
  code.push('struct { YmccFamily family; std::string deviceClass; } g_machineIdentity;\nstatic bool diagnosticLogEnabled=true; static json loggedKeys;\nstatic bool domainLogEnabled(const char*){return diagnosticLogEnabled;}\nstatic void appendNativeLifecycleLog(const char*,const json& j){loggedKeys=j.at("keys");}');
  const lambda=lambdaForIpc(source,'oem.keys.get');bindings.push({path:'native/main.cpp',name:'oem.keys.get',line:lambda.line,sha256:sha(lambda.code),start:lambda.start,end:lambda.end,kind:'compiled-production-lambda'});
  code.push(`static json actualQuery(const json& a){auto fn=${lambda.code};return fn(a);}`);
  const cpp=read('tools/fixtures/hc_steamdeck_physical_capability/QueryHarness.cpp').replace('// @@PRODUCTION@@',code.join('\n\n'));
  save('Query.generated.cpp',cpp);
  const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
  const installation=run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'query-toolchain.log').trim();
  const vcvars=path.join(installation,'VC/Auxiliary/Build/vcvars64.bat');
  if(!fs.existsSync(vcvars)||/[\r\n"%]/.test(vcvars))throw Error('MSVC path unavailable/unsupported');
  const envFile=path.join(out,'compiler-environment.cmd');
  fs.writeFileSync(envFile,`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  const r=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envFile],{encoding:'utf8',windowsHide:true,timeout:30000});
  if(r.error||r.status!==0)throw Error('MSVC environment unavailable');
  const env={...process.env};for(const line of r.stdout.split(/\r?\n/)){const p=line.indexOf('=');if(p>0)env[line.slice(0,p)]=line.slice(p+1);}
  save('query-compiler-environment.log',`VCToolsVersion=${env.VCToolsVersion}\nWindowsSDKVersion=${env.WindowsSDKVersion}\n`);
  read('deps/json/json.hpp');const exe=path.join(out,'Query.exe');
  run('cl.exe',['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',`/I${path.join(repo,'deps/json')}`,path.join(out,'Query.generated.cpp'),`/Fo${path.join(out,'Query.obj')}`,`/Fd${path.join(out,'Query.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'],'query-compile.log',env);
  const result=JSON.parse(run(exe,[],'query-run.log',env));
  const verdict=queryVerdict(result.deck),checks={realLambdaExecuted:true,logOffSameResult:JSON.stringify(result.deck)===JSON.stringify(result.deckWithoutLog),logMatchesReturn:JSON.stringify(result.deck.keys)===JSON.stringify(result.loggedKeys),legacyControlRetainsNoCaptureFields:result.legacyControl.keys.every(k=>!Object.hasOwn(k,'captureImplemented')&&!Object.hasOwn(k,'captureRoute'))};
  if(Object.values(checks).some(v=>v!==true))throw Error('Real query logging/control behavior changed');
  return {pass:verdict.pass,...checks,contractVerdict:verdict,result,compiler:env.VCToolsVersion,exeSha256:sha(fs.readFileSync(exe)),scope:'Exact production oem.keys.get lambda and catalog helpers; controlled identity and in-memory log stub only'};
}
function collectSourceGraph() {
  const hcNeptune=hc('Controllers/Steam/NeptuneController.cs'),manager=hc('Managers/ControllerManager.cs');
  for(const name of ['AttachDetails','InitializeInputOutput','Plug','Unplug','Hide','Unhide'])bind(hcNeptune,name);
  for(const name of ['HidDeviceArrived','Resume','Suspend','ClearTargetController','ClearTargetControllerInternal','PickTargetController','PickTimer_Elapsed','SetTargetController','ScenarioTimer_Elapsed'])bind(manager,name);
  for(const name of ['Open','Close'])bind(hc('Devices/Valve/SteamDeck.cs'),name);
  bind(hc('Views/Windows/MainWindow.xaml.cs'),'SystemManager_SystemStatusChanged');
  for(const name of ['ymccResolveFamily','ymccFamilyInterfacePolicy','gamepadReadStateUnlocked','ymccXInputGetState14','ymccOpenXInputGetDevicePath','physicalHidCaptureRawInput','rogParseReport','gamepadOemPressedKeys','nativeKeyboardShortcutMaskBit','gamepadRegisterRawInput','WndProc','onexBackParse'])bind('native/main.cpp',name);
  for(const name of ['BuildSteamDeckState','BuildSteamDeckStateFields'])bind('InputHost/Program.cs',name);
  bind('InputHost/ScreenTouchpadReport.cs','SubmitSteamDeckWithScreenPads');
  const sources=walk(path.join(repo,'native'),p=>/\.(h|cpp)$/.test(p)).concat(walk(path.join(repo,'InputHost'),p=>/\.(cs|csproj)$/.test(p)));
  const scan=[],unknownNeptune=[];
  // Examine the whole native/InputHost source superset, not just name matches in
  // one file. Comments and strings do not count as constructor/event/decoder code.
  const markers=/neptune|steam_hidapi|RequestLizardMode|OnControllerInputReceived/i;
  for(const abs of sources) {
    const relative=path.relative(repo,abs).replaceAll('\\','/'),text=read(relative),tokens=lex(text);
    const ids=tokens.filter(t=>t.kind==='id'&&markers.test(t.value));
    const refs=relative.endsWith('.csproj')?[...text.matchAll(/<(?:ProjectReference|Reference)[^>]*Include="([^"]+)"/g)].map(m=>m[1]):[];
    const special=tokens.filter(t=>t.kind==='id'&&['kOemBitSpecial','kOemBitSpecial2'].includes(t.value)).map(t=>({symbol:t.value,line:t.line,context:text.split(/\r?\n/)[t.line-1].trim()}));
    scan.push({path:relative,sha256:sha(inputs.get(relative)),identifiers:ids.map(t=>({symbol:t.value,line:t.line})),references:refs,oemSymbolOccurrences:special});
    if(ids.length||refs.some(x=>markers.test(x)))unknownNeptune.push({path:relative,identifiers:ids.map(t=>({symbol:t.value,line:t.line})),references:refs});
  }
  const project=read(hc('HandheldCompanion.csproj'));
  const dependencies=[...project.matchAll(/<ProjectReference\s+Include="([^"]+)"/g)].map(m=>{const absolute=path.resolve(path.dirname(path.join(repo,hc('HandheldCompanion.csproj'))),m[1].replaceAll('\\','/'));return {declared:m[1],path:path.relative(repo,absolute).replaceAll('\\','/'),exists:fs.existsSync(absolute),kind:'project-reference'};});
  const transport=dependencies.find(x=>x.declared.includes('steam-hidapi.net'));
  if(!transport)throw Error('Fixed HC no longer declares steam-hidapi dependency');
  const wanted=['oem2keys','rearButtons','touchpads','imu'];
  const node=(id,role,realm,protocol,capabilities,extra={})=>({id,role,realm,protocol,capabilities,available:true,...extra});
  const graph={scope:'Conservative source graph, selected semantic routes; syntactic call inventory is separate. Conditional source union, not runtime reachability proof.',nodes:[
    node('hc.attach','discover','hc-reference','neptune',wanted,{entry:true,evidence:'AttachDetails'}),
    node('hc.open','open','hc-reference','neptune',wanted,{evidence:'Open'}),
    node('hc.transport','physicalRead','hc-reference','neptune',wanted,{origin:'physical',available:transport.exists,evidence:transport.path}),
    node('hc.decode','decode','hc-reference','neptune',wanted,{available:false,evidence:'steam_hidapi.net.NeptuneControllerInputState source absent in fixed dependency closure'}),
    node('hc.callback','transform','hc-reference','neptune',wanted,{evidence:'HandleControllerInput'}),
    node('hc.tick','publish','hc-reference','neptune',wanted,{evidence:'Tick'}),
    node('ymcc.xinput','open','ymcc','xinput',['baseGamepad'],{entry:true,evidence:'gamepadReadStateUnlocked'}),
    node('ymcc.raw','physicalRead','ymcc','hid-usage',['baseGamepad'],{origin:'physical',entry:true,evidence:'physicalHidCaptureRawInput'}),
    node('ymcc.generic','decode','ymcc','xinput',['baseGamepad'],{evidence:'rogParseReport -> XINPUT_GAMEPAD'}),
    node('ymcc.base','publish','ymcc','xinput',['baseGamepad'],{evidence:'g_physicalHidPad, gamepadReadStateUnlocked'}),
    node('ymcc.catalog','declaration','ymcc','neptune',['oem2keys'],{entry:true,evidence:'kSteamDeckKeys'}),
    node('host.build','targetBuild','ymcc','neptune',wanted,{entry:true,origin:'virtual',evidence:'BuildSteamDeckStateFields'}),
    node('host.submit','targetSubmit','ymcc','neptune',wanted,{origin:'virtual',evidence:'SubmitSteamDeckWithScreenPads'}),
  ],edges:[
    ['hc.attach','hc.open','call'],['hc.open','hc.transport','call'],['hc.transport','hc.decode','data'],['hc.decode','hc.callback','event'],['hc.callback','hc.tick','data'],
    ['ymcc.raw','ymcc.generic','call'],['ymcc.generic','ymcc.base','data'],['ymcc.xinput','ymcc.base','data'],['host.build','host.submit','data'],
  ].map(([from,to,kind])=>({from,to,kind}))};
  const classified=Object.fromEntries(wanted.map(cap=>[cap,classify(graph,cap)]));
  save('production-source-scan.json',{files:scan.length,scope:'All native .h/.cpp and InputHost .cs/.csproj excluding bin/obj; syntactic superset, not a build configuration proof',filesScanned:scan,unreviewedNeptuneCode:unknownNeptune});
  save('source-graph.json',graph);
  save('source-bindings.json',bindings);
  const reviewed=JSON.parse(read('tools/fixtures/hc_steamdeck_physical_capability/reviewed-physical-roots.json'));
  const changedRoots=reviewed.roots.filter(root=>!bindings.some(b=>b.path===root.path&&b.name===root.name&&b.sha256===root.sha256));
  save('physical-root-review.json',{pass:changedRoots.length===0,scope:reviewed.scope,changedRoots});
  if(changedRoots.length)throw Error('Reviewed physical roots changed: re-review source graph before claiming absence; see physical-root-review.json');
  if(unknownNeptune.length)throw Error('Unreviewed Neptune protocol code appeared; source-graph absence is UNKNOWN until reviewed');
  return {classified,dependencies,physicalProtocol:{primaryConsumer:hcNeptune,transportProject:transport,packetDecoderSourceAvailable:false,packetDecodeOracleImplemented:false,missing:'Fixed HC project references ../steam-hidapi.net; raw packet input state parser, HID feature reports and transport init/close implementation are outside the fixed baseline. No target bytes used to fill this gap.'},unreviewedNeptuneCode:unknownNeptune,filesScanned:scan.length};
}
function makeContract(baselineResult,graph,mapping,query,classificationTests) {
  const test=(scope,pass,checks)=>({status:pass?'passed':'failed',scope,checks});
  const base={declared:true,softwareRoutePresent:false,actualConsumer:false,hardwareVerified:false};
  const items=[
    {...base,id:'oem2keys',meaning:'Two device keys; not ButtonFlags.OEM2',members:[{ymccKey:'special',hcFlag:'Special',decodedField:'BtnSteam'},{ymccKey:'special2',hcFlag:'Special2',decodedField:'BtnQuickAccess'}],declarationSources:['Devices/Valve/SteamDeck.cs:constructor DeviceHotkeys','native/main.cpp:kSteamDeckKeys'],hcWrapperRoutePresent:true,ymccCatalogDeclared:true,offlineTest:test('Exact HC post-decode mapping; graph fault controls; actual YMCC oem.keys.get return/log contract',mapping.pass&&query.pass&&classificationTests.pass,{mapping:mapping.checks,classification:classificationTests.tests,query:query.contractVerdict}),gap:'No physical Neptune event/packet reader publishes canonical special(bit39) / special2(bit41) for SteamDeck. Existing special producer belongs to old OneX; default capture gate is not a reader.'},
    {...base,id:'rearButtons',members:['L4','R4','L5','R5'],hcWrapperRoutePresent:true,ymccPhysicalDeclaration:false,offlineTest:test('Exact HC Tick with isolated one-hot ALREADY-DECODED rear states',mapping.pass,mapping.checks),gap:'No Neptune four-rear-key producer; configurable two-bit virtual rear output is a distinct output contract.'},
    {...base,id:'touchpads',members:['LeftPadX','LeftPadY','RightPadX','RightPadY','touch','press','derived cardinal press >= +/-21844'],hcWrapperRoutePresent:true,ymccPhysicalDeclaration:false,offlineTest:test('Exact HC Tick touch gate, release and directional boundary; not HID packets',mapping.pass,mapping.checks),gap:'ScreenTouchpadReport consumes screen/envelope input and builds target reports; generic RawInput returns XINPUT_GAMEPAD, not physical Neptune pad state.'},
    {...base,id:'imu',members:['accelerometer','gyroscope'],units:{acceleration:'g',angularVelocity:'degrees/second'},decodedConversion:{accel:['AccelX / 32767 * 2','AccelZ / 32767 * 2','-AccelY / 32767 * 2'],gyro:['Pitch / 32767 * 2000','Roll / 32767 * 2000','-Yaw / 32767 * 2000']},hcWrapperRoutePresent:true,ymccPhysicalDeclaration:false,offlineTest:test('Exact HC Tick orientation, numeric scale and readiness guards, already decoded samples',mapping.pass,mapping.checks),gap:'No Neptune IMU data source in YMCC; Legion physical parser and device/sensor motion routes do not supply this controller protocol.'},
    {...base,id:'hardwareModeInitLizard',hcWrapperRoutePresent:true,offlineTest:test('Exact HC Open/Close against fake transport; recorded requests only',mapping.pass,mapping.checks),known:{attach:'Physical details VID/PID/GetMI/devicePath; try 65 then 64 buffers; IsVirtual returns before reader construction',open:['subscribe OnControllerInputReceived','Controller.Open()','RequestLizardMode(false)'],close:['unsubscribe','RequestLizardMode(true)','Controller.Close()']},unknown:['Raw HID feature reports/opcodes, read-loop/init preconditions and acknowledgement','Original mode ownership, conflict arbitration with Steam and other readers','Dependency Open/Close idempotence; buffer retry cleanup'],gap:'No YMCC Neptune mode ownership/init/release session. SteamDeck device Open uses InpOut firmware/memory and is not controller HID initialization.'},
    {...base,id:'sleepRestoreClose',hcWrapperRoutePresent:true,offlineTest:test('Compiled wrapper Open/Close/Gone; sleep orchestration source graph only',mapping.pass,mapping.checks),sources:{suspend:'MainWindow SystemPending -> ControllerManager.Suspend -> ClearInputState + StopWatchdog + ClearTargetController -> ClearTargetControllerInternal -> targetController.Unplug -> Neptune.Unplug -> Close',resume:'MainWindow SystemReady (prev Pending) -> ControllerManager.Resume -> PickTargetController (timer) -> SetTargetController -> Plug -> Open',removal:'Neptune.Gone -> unsubscribe + EndRead + null; no RequestLizardMode(true) in this method'},gap:'No YMCC Neptune lifecycle owner. Source-level reference sequence proves neither fresh post-wake input nor successful hardware restore; Gone differs from Close.'},
  ];
  return {schemaVersion:1,kind:'physical-steamdeck-capability-contract',generatedAt:new Date().toISOString(),authority:'Offline local fixed source audit, never hardware acceptance',dimensions:{declared:'Declared in fixed HC source. See ymcc* fields for YMCC declaration scope.',softwareRoutePresent:'Complete physical Neptune software route in YMCC executable source, excluding HC reference and virtual targets',actualConsumer:'Connected open -> physical read -> decode -> publication in YMCC, per source graph; not a hardware observation',offlineTest:'Executed offline checks and their explicit boundaries',hardwareVerified:'Physical device/Steam/sleep acceptance; always false in this audit'},baseline:baselineResult,identity:{ymccFamily:'ValveSteamDeck',deviceClass:'HandheldCompanion.Devices.SteamDeck',manufacturer:'VALVE-containing normalized manufacturer',products:['JUPITER','GALILEO'],inputHidKey:'valve-steam-deck:hid-controller',hcControllerManagerIds:{vid:'28DE',pids:['1205','12F0'],interface:'passes actual GetMI; Neptune branch does not impose Gordon MI==2 gate'}},capabilities:items,physicalProtocol:graph.physicalProtocol,referenceDependencyProjects:graph.dependencies,ymccClassification:graph.classified,queryContract:query.contractVerdict,packetDecoder:{status:'unknown',reason:'Primary raw packet decoder/feature-report implementation unavailable in the fixed HC dependency closure',targetDecoderReused:false,rawPacketTestsRun:false},hardwareActionsRun:[],neededForWiring:['Obtain and pin primary steam-hidapi.net implementation compatible with this fixed wrapper; validate packet framing, 64/65 firmware variants, VID/PID/MI/path and physical vs virtual origin','Implement an owned physical reader/init/close session and publish OEM keys, four rear buttons, pads, IMU into actual YMCC consumers','Define mode ownership/release and device-removal/suspend/resume generation, clear cached state and require fresh post-wake input','Validate on a physical SteamDeck under the target Windows driver/firmware with Steam coexistence and actual key/pad/motion/release/reconnect/sleep observations'],limits:['Source scan is a syntactic superset; it is not runtime control-flow, alias analysis or a build/deployment acceptance','No raw Neptune decoder copied, synthesized or reverse-inferred from SteamDeckTarget / BuildSteamDeckState','Exact extracted wrapper compiles against explicitly fake transport and already-decoded values; no binary HC/HID dependency is loaded','Source/consumer evidence is not physical acceptance']};
}
function report(contract,tests) {
  const rows=contract.capabilities.map(c=>`| ${c.id} | ${c.declared} | ${c.softwareRoutePresent} | ${c.actualConsumer} | ${c.offlineTest.status} | ${c.hardwareVerified} |`).join('\n');
  return `# SteamDeck 物理控制器能力缺口审计\n\n当前 YMCC 没有物理 Neptune 输入链。两个 Embedded 键目录可保留；应明确 captureImplemented=false、captureRoute=physical-neptune-reader-unimplemented。固定 HC 包装层是参考输入源，InputHost SteamDeck 构造/提交函数是虚拟输出。源码或 consumer 证据不代表验收。\n\n| 能力 | declared (HC) | softwareRoutePresent (YMCC) | actualConsumer (YMCC) | offlineTest | hardwareVerified |\n|---|---|---|---|---|---|\n${rows}\n\n固定基线 ${contract.baseline.baselineId} / ${contract.baseline.version}：${contract.baseline.verifiedFiles} 个文件真实 SHA 通过；index=${contract.baseline.indexSha256}。主文件 SHA 和 baseline、遗漏 index 校验见 contract.json / baseline-files-verified.json。冻结 Git revision 为 UNKNOWN，不替换为候选 checkout。\n\n来源和调用链见 source-graph.json 与 source-bindings.json；全 native/InputHost 扫描及 canonical OEM 位出现位置见 production-source-scan.json。真实生产 oem.keys.get lambda 已离线编译执行，返回和日志合同=${tests.query.pass ? 'PASS' : 'FAIL / missing explicit fields'}；图分类 ${tests.classification.tests} 项反例/正例 PASS；精确 HC Tick/Open/Close/Gone 编译执行 ${tests.mapping.checks} 检查 PASS。详细结果见 tests.json / query-run.log / mapping-run.log。\n\n固定 HC 项目引用 ../steam-hidapi.net，当前该路径缺源码；底层原始 HID packet decoder、feature report、init/read/close 实现为 unknown。本轮只编译包装层及已解码状态 seam，不伪造 packet oracle，也不借 target 字节反推物理输入。SteamDeck.cs 的 Open 是 InpOut 固件/内存通路，不能记为 Neptune HID 初始化。\n\nHC Open 请求 Lizard=false；Close 请求 Lizard=true 后关闭；Gone 仅 EndRead/null，不声称恢复 Lizard。睡眠源链：SystemPending -> ControllerManager.Suspend -> ClearTargetController -> Unplug -> Close；恢复链：SystemReady -> Resume -> PickTargetController/SetTargetController -> Plug/Open。上述是来源图，没有真实睡眠或硬件确认。重复 Open 的假 transport seam 观察：订阅数=${tests.mapping.observations.repeatedOpenSubscriptions}，一次 Close 后=${tests.mapping.observations.remainingAfterOneClose}；这只说明包装层没有自证幂等，不能直接归因为真实依赖故障。\n\n可接线的现成组件：PC 身份识别与两键目录、受选物理设备身份/代际检查、XInput 基础采集、通用 HID usage -> XINPUT_GAMEPAD、OEM canonical 发布接口、现有 motion/虚拟输出下游。仍缺：兼容固定源的原始 Neptune dependency/协议、物理 reader 和字段 publisher、mode ownership/init/release、fresh post-wake/reconnect 状态合同。四背键和双物理触控板不能用现有两位虚拟背键/屏幕触摸板代替。\n\n验收所需：真实 SteamDeck 的目标 Windows 驱动/固件、确认的物理 VID/PID/MI/path 与虚拟设备排除、原始输入和模式回执、Steam 并存/退出及设备移除恢复观测、四背键/双触控板/IMU/睡眠唤醒后新帧实测。本轮没有启动 Steam、DeviceIO/HID/OEM 模式、PBT 或 sleep；无网络、无 production 编辑。\n\n复跑（在源码根目录）：\n\n\`node tools/hc_steamdeck_physical_capability_audit.mjs\`\n\n仅算法反例：\`node tools/hc_steamdeck_physical_capability_audit.mjs --selftest-only\`。默认新建授权 SteamDeck 证据子目录；--out 也只能指向其中的空子目录。旧 query 缺明确字段时可用 --allow-query-gap 保留差异证据，默认检查会失败。\n`;
}
try {
  for(const p of walk(fixture,()=>true))read(path.relative(repo,p).replaceAll('\\','/'));
  const fixed=baseline();save('baseline.json',fixed);
  const classification=runClassificationTests();save('classification-tests.json',classification);
  const mapping=compileMapping();save('mapping-tests.json',mapping);
  const query=compileQuery();save('query-tests.json',query);
  const graph=collectSourceGraph();
  const contract=makeContract(fixed,graph,mapping,query,classification);
  save('contract.json',contract);
  const tests={classification,mapping,query,unreviewedNeptuneCode:graph.unreviewedNeptuneCode};save('tests.json',tests);
  const manifest=[...inputs].map(([relative,bytes])=>({path:relative,sha256:sha(bytes),bytes:bytes.length,currentAtFinish:sha(fs.readFileSync(path.join(repo,relative)))===sha(bytes)}));
  for(const name of ['hc_steamdeck_physical_capability_audit.mjs']){const relative=`tools/${name}`,bytes=fs.readFileSync(path.join(repo,relative));manifest.push({path:relative,sha256:sha(bytes),bytes:bytes.length,currentAtFinish:true});}
  save('native-main.snapshot.cpp',inputs.get('native/main.cpp'));
  save('InputHost-Program.snapshot.cs',inputs.get('InputHost/Program.cs'));
  save('InputHost-ScreenTouchpadReport.snapshot.cs',inputs.get('InputHost/ScreenTouchpadReport.cs'));
  save('source-hashes.json',manifest);
  save('REPORT.md',report(contract,tests));
  const drift=manifest.filter(x=>!x.currentAtFinish);
  const status={pass:!drift.length&&!graph.unreviewedNeptuneCode.length&&(query.pass||!!options.allowQueryGap),sourceStable:!drift.length,drift,unreviewedNeptuneCode:graph.unreviewedNeptuneCode.length,classificationTests:classification.tests,mappingChecks:mapping.checks,queryContract:query.pass,physicalCapabilitiesImplemented:false,hardwareVerified:false,evidence:out};
  save('summary.json',status);console.log(JSON.stringify(status,null,2));if(!status.pass)process.exitCode=2;
} catch(error) {
  save('failure.json',{pass:false,error:error.stack});console.error(error.stack);process.exitCode=1;
}

