// Offline consumer composition: exact main bodies + real headers, no product IO.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {freeze, verify} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const runner = fileURLToPath(import.meta.url), repo = path.resolve(path.dirname(runner), '..');
const workspace = path.resolve(repo, '../../..');
const roots = {
  runtime: path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionRuntime/Consumer'),
  families: path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionFamilies/ConsumerTests'),
  consumer: path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionConsumer/Tests'),
};
const options = {};
for (let i=2; i<process.argv.length; ++i) {
  const arg=process.argv[i];
  assert(['--out','--vcvars','--root'].includes(arg) && process.argv[i+1], `Invalid option ${arg}`);
  options[arg.slice(2)]=process.argv[++i];
}
assert(options.root === undefined || Object.hasOwn(roots, options.root), 'Use --root runtime, families or consumer');
const inferred = options.out && Object.entries(roots).find(([,p]) => path.dirname(path.resolve(options.out)).toLowerCase() === p.toLowerCase());
const root = roots[options.root || inferred?.[0] || 'runtime'];
const out=path.resolve(options.out || path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomBytes(3).toString('hex')));
assert.equal(path.dirname(out).toLowerCase(),root.toLowerCase(),'Use a fresh direct child of the selected approved evidence root');
fs.mkdirSync(root,{recursive:true}); assert.equal(fs.realpathSync(root).toLowerCase(),path.resolve(root).toLowerCase());
fs.mkdirSync(out);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const relative=f=>path.relative(out,f).replaceAll('\\','/');
const json=(name,value)=>fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),
  scope:'Verbatim main consumer functions/types including queue clock wrapper and epoch cutover with real session/processor/lease/frame/clock headers; memory-only callback effects and controlled globals/clock/current seam; actual isolated calibration JSON files',
  executionBoundaries:{queueClockWrapper:'actual extracted function, controlled GetMessageTime/GetTickCount64 memory stubs',
    epochCutover:'actual extracted revoke/side/registration/publish/snapshot/submission/issue/request functions',
    sampleTimestampTransfer:'fixture orchestration transfers wrapper optional into Sample; not the complete production front door',
    WM_INPUT:'source-only exact block and call-order checks; not executed',
    physicalHidCaptureRawInput:'source-only whole function, optional/null/queue-timestamp checks; not executed',
    ambientFrameCurrent:'controlled seam; real frame decide and issue/request guards execute'},
  clockLimitations:['OS queue message uptime is not physical sensor sample time','32-bit full-ring aliases cannot be uniquely recovered','Large max-age windows can admit ambiguous future timestamps; actual bounded production window is used','Same-millisecond cutover ordering is indistinguishable; equal queue ticks are rejected','No real concurrent interleaving, PBT delivery, system clock or scheduling proof'],
  actualHardware:false,actualPbt:false,rawReaderExecuted:false,hostExecuted:false,actualWindowsSensor:false,
  auditedDllExecuted:false,deployment:false,evidenceRoot:root,inputs:[],configurations:[],mutations:[]};
const save=()=>json('summary.json',receipt);
const run=(exe,args,log,cwd=out,env=process.env)=>{
  const r=spawnSync(exe,args,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:16*1024*1024});
  fs.mkdirSync(path.dirname(path.join(out,log)),{recursive:true});
  fs.writeFileSync(path.join(out,log),(r.stdout||'')+(r.stderr||''));
  if(r.error)throw r.error; assert.equal(r.signal,null,log); return r;
};
const success=(r,label)=>{assert.equal(r.status,0,`${label}: exit ${r.status}; ${out}`);return r.stdout;};
const replaceOnce=(s,a,b)=>{assert.equal(s.split(a).length-1,1,'Mutation anchor ambiguous/missing: '+a);return s.replace(a,b);};
const walk=dir=>fs.readdirSync(dir,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(e=>
  e.isDirectory()?walk(path.join(dir,e.name)):e.isFile()?[path.join(dir,e.name)]:[]);
save();
try {
  const fixture=path.join(repo,'tools/fixtures/legion_motion_consumer');
  const headers=['controller_motion_bank_sessions.h','controller_motion_session.h','controller_motion_processor.h','legion_motion_source.h',
    'input_frame_submission.h','windows_motion_submission.h','virtual_output_status.h','legion_input_protocol.h','legion_motion_message_clock.h','controller_motion_calibration.h'];
  const files=[runner,...walk(fixture),path.join(repo,'tools/fixtures/legion_motion_calibration_consumer/platform_boundary.h'),...headers.map(n=>path.join(repo,'native',n)),path.join(repo,'deps/json/json.hpp')];
  for(const file of files){
    const p=path.relative(repo,file);const bytes=fs.readFileSync(file);const snapshot=path.join(out,'source',p);
    fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes);
    receipt.inputs.push({path:p.replaceAll('\\','/'),snapshot:relative(snapshot),bytes:bytes.length,sha256:sha(bytes)});
  }
  const binding=freeze(repo,out);
  receipt.production={sourceSha256:binding.binding.sourceSha256,
    methods:binding.entries.filter(e=>e.kind==='function').length,structures:binding.entries.filter(e=>e.kind==='struct').length,
    sourceOnlyAnchors:binding.anchors.length,sourceOnlyChecks:binding.binding.checks.length,
    sourceBinding:'main-source-bindings.json',mainSourceStable:null};
  json('source-bindings.json',{inputs:receipt.inputs});
  let vcvars=options.vcvars;
  if(!vcvars){
    const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
    const install=success(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],
      'toolchain-discovery.log'),'MSVC discovery').trim();assert(install&&!/[\r\n]/.test(install));
    vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');
  }
  assert(fs.existsSync(vcvars)&&!/[\r\n"%&|<>^!]/.test(vcvars));
  const setup=path.join(out,'compiler-environment.cmd');assert(!/[\r\n"%&|<>^!]/.test(setup));
  fs.writeFileSync(setup,calibrationToolchainPreamble(vcvars)+'set\r\n');
  const setupResult=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',setup],{encoding:'utf8',windowsHide:true,timeout:30000});
  if(setupResult.error)throw setupResult.error;assert.equal(setupResult.status,0,'MSVC environment');
  const env={...process.env};
  for(const line of setupResult.stdout.split(/\r?\n/)){const at=line.indexOf('=');if(at<=0)continue;
    const key=line.slice(0,at),old=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[old||key]=line.slice(at+1);}
  for(const key of Object.keys(env))if(['cl','_cl_','link','_link_'].includes(key.toLowerCase()))delete env[key];
  const compiler=path.join(env.VCToolsInstallDir,'bin/Hostx64/x64/cl.exe');
  receipt.compiler={compiler,sha256:sha(fs.readFileSync(compiler)),vcvars,vcToolsVersion:env.VCToolsVersion,windowsSdkVersion:env.WindowsSDKVersion};
  const configs=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']]];
  const harness=path.join(out,'source/tools/fixtures/legion_motion_consumer/harness.cpp');
  const harnessText=fs.readFileSync(harness,'utf8');
  const names=[...harnessText.slice(harnessText.indexOf('static const Scenario scenarios[]')).matchAll(/\{"([-a-z0-9]+)",[A-Za-z0-9_]+\}/g)].map(m=>m[1]);
  assert(names.length>=31);receipt.scenarios=names;
  const parse=text=>{
    const scenarios=[...text.matchAll(/^CASE ([-a-z0-9]+) checks=(\d+) failures=(\d+)\r?$/gm)].map(m=>({name:m[1],checks:+m[2],failures:+m[3]}));
    const m=/^SUMMARY cases=(\d+) checks=(\d+) failures=(\d+) failedCases=(\d+)\r?$/m.exec(text);assert(m,'No semantic summary');
    const parsed={cases:+m[1],checks:+m[2],failures:+m[3],failedCases:+m[4],scenarios};
    assert.deepEqual(scenarios.map(s=>s.name),names);assert.equal(scenarios.length,parsed.cases);
    assert.equal(scenarios.reduce((n,s)=>n+s.checks,0),parsed.checks);assert.equal(scenarios.reduce((n,s)=>n+s.failures,0),parsed.failures);
    assert(scenarios.every(s=>s.checks>0));return parsed;
  };
  const build=(dir,flags,generatedDir)=>{
    fs.mkdirSync(dir,{recursive:true});const exe=path.join(dir,'legion_motion_consumer.exe');
    const args=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise','/showIncludes',...flags,
      `/I${generatedDir}`,`/I${path.join(out,'source/native')}`,`/I${path.join(out,'source/deps/json')}`,
      harness,`/Fo${path.join(dir,'harness.obj')}`,`/Fd${path.join(dir,'compiler.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
    const compile=success(run(compiler,args,`${relative(dir)}/compile.log`,dir,env),relative(dir)+' compilation');
    for(const header of headers)assert(compile.includes(path.join(out,'source/native',header)),'Missing actual header include '+header);
    const artifacts=path.join(dir,'calibration-and-effects');fs.mkdirSync(artifacts);
    const result=run(exe,[artifacts,path.join(out,'source/tools/fixtures/legion_motion_consumer/family-topology.json')],`${relative(dir)}/run.log`,dir,env);
    const parsed=parse(result.stdout);return {buildArguments:args,executableSha256:sha(fs.readFileSync(exe)),exitCode:result.status,...parsed};
  };
  for(const [name,flags] of configs){
    const result=build(path.join(out,name),flags,out);receipt.configurations.push({name,...result});save();
    assert.equal(result.exitCode,0,name+' baseline assertions');assert.equal(result.failures,0);
    console.log(`${name}: PASS (${result.cases} scenarios, ${result.checks} checks)`);
  }
  const selectedGetter=binding.entries.find(e=>e.symbol==='legionMotionSelectedSensorIndex').code;
  const sideSetter=binding.entries.find(e=>e.symbol==='legionMotionSelectSensorIndex').code;
  const revisionComparison=/\brevision\s*<\s*g_legionMotionSelectionRevision/.exec(sideSetter);
  assert(revisionComparison,'Missing actual revision comparison');
  const functionBody=name=>{const found=binding.entries.find(e=>e.symbol===name&&e.kind==='function');assert(found,'Missing actual function '+name);return found.code;};
  const omitCutover=name=>{const body=functionBody(name);
    const term=/ymcc::legion_motion_clock::belongsToEpoch\(sample\.capturedAt,\s*g_legionMotionEpochStartedAt\.load\(std::memory_order_acquire\)\) &&\s*/.exec(body);
    assert(term,'Cutover predicate missing in '+name);return replaceOnce(body,term[0],'');};
  const snapshotBody=functionBody('legionMotionSnapshot');
  const snapshotTerm=/ \|\| !ymcc::legion_motion_clock::belongsToEpoch\(sample->capturedAt,\s*g_legionMotionEpochStartedAt\.load\(std::memory_order_acquire\)\)/.exec(snapshotBody);
  assert(snapshotTerm,'Snapshot cutover predicate missing');
  const wrapper=functionBody('legionMotionMessageReceiptTick');
  const processingWrapper=replaceOnce(replaceOnce(wrapper,'return ymcc::legion_motion_clock::messageReceiptTick(',
    '(void)queuedAt;\n    const auto processingNow = GetTickCount64();\n    return ymcc::legion_motion_clock::messageReceiptTick('),
    'GetTickCount64(), queuedAt,','processingNow, static_cast<uint32_t>(processingNow),');
  const mutations=[
    {name:'publish-cutover-omitted',from:functionBody('legionMotionPublish'),
      to:omitCutover('legionMotionPublish'),witness:'queued-message-publish-cutover'},
    {name:'snapshot-cutover-omitted',from:functionBody('legionMotionSnapshot'),
      to:replaceOnce(snapshotBody,snapshotTerm[0],''),witness:'cutover-only-snapshot-and-final-guards'},
    {name:'submission-cutover-omitted',from:functionBody('legionMotionSubmissionCurrent'),
      to:omitCutover('legionMotionSubmissionCurrent'),witness:'cutover-only-snapshot-and-final-guards'},
    {name:'clock-route-gate-bypassed',from:wrapper,
      to:replaceOnce(wrapper,'if (!inputCaptureLegionControllerMotionRoute()) return std::nullopt;','(void)inputCaptureLegionControllerMotionRoute();'),witness:'queue-clock-wrapper-route-scope'},
    {name:'clock-processing-time-for-queue-time',from:wrapper,
      to:processingWrapper,witness:'queue-clock-age-window'},
    {name:'sensor-getter-pinned-left',from:selectedGetter,
      to:selectedGetter.slice(0,selectedGetter.indexOf('{')+1)+'\n    return 0;\n}',witness:'tablet-side-switch-calibration'},
    {name:'selection-revision-comparison-inverted',from:sideSetter,
      to:replaceOnce(sideSetter,revisionComparison[0],revisionComparison[0].replace('<','>')),witness:'sensor-selection-revision-fence'},
    {name:'selected-identity-retirement-inverted',from:'legionMotionStableIdentity(previous) != legionMotionStableIdentity(next)',
      to:'legionMotionStableIdentity(previous) == legionMotionStableIdentity(next)',witness:'source-identity-and-real-route'},
    {name:'calibration-sensor-alias',from:'device->find(std::to_string(key.sensorIndex))',to:'device->find("1")',witness:'actual-sample-and-independent-calibration'},
    {name:'repeat-calibration-read',from:'const bool changed=!g_legionMotionSessionKey || !(*g_legionMotionSessionKey==key);',
      to:'const bool changed=true;',witness:'repeat-tick-no-calibration-or-create'},
    {name:'issue-controller-guard-neutral-inverted',from:'!frame->ticket.neutral && frame->controllerMotion &&',
      to:'frame->ticket.neutral && frame->controllerMotion &&',witness:'issue-request-guard-retirement'},
    {name:'request-controller-guard-neutral-inverted',from:'!frame.ticket.neutral && frame.controllerMotion &&',
      to:'frame.ticket.neutral && frame.controllerMotion &&',witness:'issue-request-guard-retirement'},
  ];
  for(const mutation of mutations){
    const dir=path.join(out,'mutations',mutation.name);fs.mkdirSync(dir,{recursive:true});
    const altered=replaceOnce(binding.generated,mutation.from,mutation.to);
    fs.writeFileSync(path.join(dir,'production_functions.generated.h'),altered);
    for(const name of ['production_types.generated.h','production_scopes.generated.h'])
      fs.copyFileSync(path.join(out,name),path.join(dir,name));
    const row={name:mutation.name,from:mutation.from,to:mutation.to,witness:mutation.witness,sha256:sha(altered),configurations:[]};
    receipt.mutations.push(row);
    for(const [name,flags] of configs){const result=build(path.join(dir,name),flags,dir);row.configurations.push({name,...result});save();
      assert.equal(result.exitCode,1,mutation.name+'/'+name+': require semantic failure, no crash');
      assert(result.failures>0);assert(result.scenarios.some(s=>s.name===mutation.witness&&s.failures>0),'Missing named witness');}
    console.log('Mutant compiled and killed in both configurations: '+mutation.name);
  }
  Object.assign(receipt.production,verify(repo,binding));
  assert(receipt.production.selectedMethodsStable&&receipt.production.anchorsStable,'Selected main methods/anchors changed; use fresh run');
  receipt.ownedSourcesStable=receipt.inputs.every(i=>sha(fs.readFileSync(path.join(repo,i.path)))===i.sha256);
  assert(receipt.ownedSourcesStable,'Frozen header/fixture input changed during proof');
  receipt.status='passed';receipt.completedAt=new Date().toISOString();save();
  fs.writeFileSync(path.join(out,'REPORT.md'),`# Legion consumer isolated composition\n\nStatus: PASSED.\n\n`+
    `Compiled ${receipt.production.methods} verbatim main functions, ${receipt.production.structures} exact structs/scopes, actual PID/sensor/cutover declarations, real session/processor/source/frame/status/protocol/message-clock headers and bundled JSON library. Function UTF-8 byte offsets and SHA256, main bytes and source-only anchor bytes are frozen.\n\n`+
    receipt.configurations.map(c=>`- ${c.name}: ${c.cases} scenarios, ${c.checks} checks, ${c.failures} failures.`).join('\n')+
    `\n- ${mutations.length} finite semantic mutants compiled and killed in both configurations; compile errors or crashes cannot count.\n\n`+
    `This existing 31-scenario regression compiles the new calibration registry and actual Advance dependency. It runs the idle Advance path with no calibration request. Request/status/persistence/active calibration are validated by the separate calibration consumer fixture; they are not claimed executed here.\n\n`+
    `Four exact Legion models, model-specific VID/PID/base-container identity, strict read-only settings parsing, revision-fenced left/right sensor selection and independent side calibration, actual sample publication/snapshot, optional Session ownership, Manual mode/offset/process/read callbacks, structured physical identity+sensor calibration JSON, repeated tick cache/handle reuse, source/epoch/sensor changes, A-B-A, Ready/power/raw retirement, expiry/future/absent recovery, failed-create non-revival, nonfinite and stationary raw axes, player/world requests, nested scopes and memory-simulated revoke-before-unload are exercised.\n\n`+
    `The actual legionMotionMessageReceiptTick wrapper executes with counted GetMessageTime/GetTickCount64 memory stubs: non-target routes call neither clock; all four exact model routes call each once in order. Tests cover age equality/expiry, future/pre-boot/zero uptime, signed message bits, and 32-bit wrap. Wrapper optional-to-Sample transfer is fixture orchestration.\n\n`+
    `Actual revoke/side/registration cutovers, publish, snapshot and submission predicates execute. Queued messages at or before a cutover are refused even with the current epoch and source stamp; only provably later queue ticks are accepted. A controlled cutover-only interleave holds epoch/context/lease fixed to isolate snapshot and final issue/request timestamp guards. Side and re-registration transitions, failure/recovery and wrap are covered.\n\n`+
    `The real final issue and request guards execute against real frame decide() with a controlled Current-global seam. Tests retire power/raw/capture/age after frame formation, preserve full-neutral release with unchanged target/power/intent, and verify the null controller pointer path retains canonical admission. These guards are not a real pipe/Host run.\n\n`+
    `GamepadMotion callbacks are an effect sink; the Windows handle sentinel is forbidden on every callback. Both message and uptime clocks and globals are controlled; calibration lookup uses actual isolated JSON files. No audited DLL, HID, OEM, DeviceIO, sensor, product, Host, Steam or sleep operation is executed.\n\n`+
    `${receipt.production.sourceOnlyChecks} labelled source-only checks cover the complete WM_INPUT block and physicalHidCaptureRawInput optional/null/timestamp wiring, raw provenance, onGamepad consumption/finite readback/wire orientation, Host controller scope, both selected setters, shutdown and emergency revoke-before-unload. Those whole functions are not executed. The real raw report normalization/decoder executes separately, but full WM_INPUT and full physicalHidCaptureRawInput do not execute. Physical input, actual PBT/OS lifecycle delivery, real clocks/threads/locking, complete main/Host/Steam integration and hardware acceptance remain unproved.\n\n`+
    `Message time is OS queue uptime, not physical sensor sample time. Full 32-bit-ring aliases are not uniquely recoverable; large-window future ambiguity and same-millisecond cutover ordering remain helper contract limits (equal queue ticks are conservatively refused). The actual bounded production age window is used.\n\n`+
    `Binding stability: full source changed=${receipt.production.fullSourceChanged}; selected extracted methods/constants stable=${receipt.production.selectedMethodsStable}; source-only anchors stable=${receipt.production.anchorsStable}; all frozen runner/fixture/header inputs stable=${receipt.ownedSourcesStable}. See main-source-bindings.json and summary.json for exact hashes and boundaries.\n`);
  const inventory=walk(out).filter(f=>path.basename(f)!=='FILES.json').map(file=>({path:relative(file),bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))}));
  json('FILES.json',{note:'Self inventory excluded',files:inventory});
  console.log(JSON.stringify({status:receipt.status,evidence:out,configurations:receipt.configurations.map(({name,cases,checks,failures})=>({name,cases,checks,failures})),mutations:receipt.mutations.length},null,2));
}catch(error){receipt.status='failed';receipt.error=error.stack||String(error);receipt.finishedAt=new Date().toISOString();save();console.error(receipt.error);console.error('Evidence: '+out);process.exitCode=1;}
