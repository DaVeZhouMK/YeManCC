// Offline consumer composition: exact main bodies + real headers, no product IO.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {freeze, verify} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {freeze as freezeUi} from './fixtures/legion_motion_calibration_consumer/extract_main.mjs';
const runner = fileURLToPath(import.meta.url), repo = path.resolve(path.dirname(runner), '..');
const workspace = path.resolve(repo, '../../..');
const roots = {
  runtime: path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionCalibration/Consumer'),
};
const options = {};
for (let i=2; i<process.argv.length; ++i) {
  const arg=process.argv[i];
  assert(['--out','--vcvars','--root'].includes(arg) && process.argv[i+1], `Invalid option ${arg}`);
  options[arg.slice(2)]=process.argv[++i];
}
assert(options.root === undefined || Object.hasOwn(roots, options.root), 'Use --root runtime');
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
  scope:'Verbatim main calibration registry/request/status/store/advance/abort/restore consumer functions including queue clock wrapper and epoch cutover with real session/processor/lease/frame/clock headers; memory-only callback effects and controlled globals/clock/current seam; actual isolated calibration JSON files',
  executionBoundaries:{queueClockWrapper:'actual extracted function, controlled GetMessageTime/GetTickCount64 memory stubs',
    epochCutover:'actual extracted revoke/side/registration/publish/snapshot/submission/issue/request functions',
    sampleTimestampTransfer:'fixture orchestration transfers wrapper optional into Sample; not the complete production front door',
    WM_INPUT:'source-only exact block and call-order checks; not executed',
    physicalHidCaptureRawInput:'source-only whole function, optional/null/queue-timestamp checks; not executed',
    ambientFrameCurrent:'controlled seam; real frame decide and issue/request guards execute'},
  clockLimitations:['OS queue message uptime is not physical sensor sample time','32-bit full-ring aliases cannot be uniquely recovered','Large max-age windows can admit ambiguous future timestamps; actual bounded production window is used','Same-millisecond cutover ordering is indistinguishable; equality is accepted','No real concurrent interleaving, PBT delivery, system clock or scheduling proof'],
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
  const fixture=path.join(repo,'tools/fixtures/legion_motion_calibration_consumer');
  const headers=['controller_motion_bank_sessions.h','controller_motion_session.h','controller_motion_processor.h','legion_motion_source.h',
    'input_frame_submission.h','windows_motion_submission.h','virtual_output_status.h','legion_input_protocol.h','legion_motion_message_clock.h','controller_motion_calibration.h'];
  const files=[runner,...walk(fixture),...walk(path.join(repo,'tools/fixtures/legion_motion_consumer')),...headers.map(n=>path.join(repo,'native',n)),path.join(repo,'deps/json/json.hpp')];
  for(const file of files){
    const p=path.relative(repo,file);const bytes=fs.readFileSync(file);const snapshot=path.join(out,'source',p);
    fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes);
    receipt.inputs.push({path:p.replaceAll('\\','/'),snapshot:relative(snapshot),bytes:bytes.length,sha256:sha(bytes)});
  }
  receipt.executionBoundaries={actualMain:'request/status/store/write-atomic/advance/abort/restore-trust and actual supporting lease/session/registry functions',
    controlled:'native callbacks, uptime clock, event/log effects, Win32 file API boundary',
    persistence:'actual JSON file reads/merge/serialization into evidence-private paths; simulated final platform replacement',
    sourceOnly:'whole Vue/IPC/onGamepad/Host/shutdown/OS lifecycle callers',
    pureHeader:'actual controller_motion_calibration.h; no handwritten Advance or Transaction'};
  const binding=freeze(repo,out);
  const uiOut=path.join(out,'ui-bindings');fs.mkdirSync(uiOut);const ui=freezeUi(repo,uiOut);
  assert.equal(ui.binding.sourceSha256,binding.binding.sourceSha256,'Production changed while binding UI evidence');
  receipt.uiExpressionChecks=0; // UI helper/timing verification is independently owned.
  receipt.windowsMarkerSourceIsolation=binding.entries.filter(e=>e.kind==='function'&&e.symbol.startsWith('inputCaptureControllerCalibration')).every(e=>! /\bg_(?:gyroCalib\w*|calibUiStatus|autoCalib\w*|calibSourceAuto)\b/.test(e.code));
  assert(receipt.windowsMarkerSourceIsolation,'Calibration methods reference Windows calibration markers');


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
  const harness=path.join(out,'source/tools/fixtures/legion_motion_calibration_consumer/harness.cpp');
  const harnessText=fs.readFileSync(harness,'utf8');
  const arrayStart=harnessText.indexOf('static const Scenario scenarios[]');
  const names=[...harnessText.slice(arrayStart,harnessText.indexOf('\n};',arrayStart)+3).matchAll(/\{"([-a-z0-9]+)",[A-Za-z0-9_]+\}/g)].map(m=>m[1]);
  assert(names.length>=24);receipt.scenarios=names;
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
    const artifacts=path.join(dir,'fx');fs.mkdirSync(artifacts);
    const result=run(exe,[artifacts,path.join(out,'source/tools/fixtures/legion_motion_calibration_consumer/family-topology.json')],`${relative(dir)}/run.log`,dir,env);
    const parsed=parse(result.stdout);return {buildArguments:args,executableSha256:sha(fs.readFileSync(exe)),exitCode:result.status,...parsed};
  };
  for(const [name,flags] of configs){
    const result=build(path.join(out,name),flags,out);receipt.configurations.push({name,...result});save();
    assert.equal(result.exitCode,0,name+' baseline assertions');assert.equal(result.failures,0);
    console.log(`${name}: PASS (${result.cases} scenarios, ${result.checks} checks)`);
  }
  const body=name=>{const e=binding.entries.find(e=>e.symbol===name);assert(e);return e.code;};
  const advance=body('inputCaptureControllerCalibrationAdvance');
  const complete=body('inputCaptureControllerCalibrationComplete');
  const store=body('inputCaptureControllerCalibrationStore');
  const consumer=body('inputCaptureLegionMotion');
  const missingOwner=/if \(!selected\) \{[\s\S]*?return std::nullopt;\s*\}/.exec(consumer);assert(missingOwner);
  const mutations=[
    {name:'pending-without-owner-cleanup-omitted',from:consumer,to:replaceOnce(consumer,/if \(g_legionMotionSessionKey \|\| reset[\s\S]*?\) \{/.exec(consumer)[0],'if (g_legionMotionSessionKey || reset) {'),witness:'pending-source-retirement'},
    {name:'pre-request-receipt-trained',from:advance,to:replaceOnce(advance,'sample.capturedAt>=request->requestedAt ? sample.capturedAt : 0','sample.capturedAt'),witness:'pre-request-receipt-no-training'},
    {name:'completion-id-fence-omitted',from:complete,to:replaceOnce(complete,'g_controllerCalibrationRequest->id==request.id','request.id!=0'),witness:'request-replaced-ui-id-fence'},
    {name:'persistent-sensor-aliased-left',from:store,to:replaceOnce(store,'std::to_string(key.sensorIndex)','"0"'),witness:'left-right-persistence-preserves-windows'},
    {name:'write-failure-claimed-success',from:store,to:replaceOnce(store,'return inputCaptureControllerCalibrationWriteAtomic(path.wstring(),root.dump(2));','(void)inputCaptureControllerCalibrationWriteAtomic(path.wstring(),root.dump(2)); return true;'),witness:'write-failure-rollback-not-saved'},
    {name:'missing-owner-pending-not-aborted',from:consumer,to:replaceOnce(consumer,missingOwner[0],'if (!selected) return std::nullopt;'),witness:'create-failure-cancels-request'},
    {name:'restored-trust-discarded',from:body('inputCaptureControllerCalibrationRestoreTrust'),to:replaceOnce(body('inputCaptureControllerCalibrationRestoreTrust'),'if (trusted)','if (trusted && key.power==0)'),witness:'saved-trust-restored-new-epoch'},
    {name:'busy-same-key-accepted',from:body('inputCaptureControllerCalibrationRequest'),to:replaceOnce(body('inputCaptureControllerCalibrationRequest'),'g_controllerCalibrationRequest->key==key','!(g_controllerCalibrationRequest->key==key)'),witness:'request-route-busy'},
  ];
  for(const mutation of mutations){
    const dir=path.join(out,'mutations',String(mutations.indexOf(mutation)));fs.mkdirSync(dir,{recursive:true});
    const altered=replaceOnce(binding.generated,mutation.from,mutation.to);
    fs.writeFileSync(path.join(dir,'production_functions.generated.h'),altered);
    for(const name of ['production_types.generated.h','production_scopes.generated.h'])
      fs.copyFileSync(path.join(out,name),path.join(dir,name));
    const row={name:mutation.name,evidenceDirectory:relative(dir),from:mutation.from,to:mutation.to,witness:mutation.witness,sha256:sha(altered),configurations:[]};
    receipt.mutations.push(row);
    for(const [name,flags] of configs){const result=build(path.join(dir,name),flags,dir);row.configurations.push({name,...result});save();
      assert.equal(result.exitCode,1,mutation.name+'/'+name+': require semantic failure, no crash');
      assert(result.failures>0);assert(result.scenarios.some(s=>s.name===mutation.witness&&s.failures>0),'Missing named witness');}
    console.log('Mutant compiled and killed in both configurations: '+mutation.name);
  }
  Object.assign(receipt.production,verify(repo,binding));
  const uiAfter=fs.readFileSync(path.join(repo,'src/views/GyroMotionView.vue'),'utf8');
  receipt.uiSourceStable=sha(uiAfter)===ui.binding.uiSourceSha256;
  receipt.uiScope='source-only snapshot; native JSON event assertions execute, UI helper/timing is independently owned';
  assert(receipt.production.selectedMethodsStable&&receipt.production.anchorsStable,'Selected main methods/anchors changed; use fresh run');
  receipt.ownedSourcesStable=receipt.inputs.every(i=>sha(fs.readFileSync(path.join(repo,i.path)))===i.sha256);
  assert(receipt.ownedSourcesStable,'Frozen header/fixture input changed during proof');
  receipt.status='passed';receipt.completedAt=new Date().toISOString();save();
  fs.writeFileSync(path.join(out,'REPORT.md'),'# Controller calibration actual-main composition\n\nStatus: PASSED.\n\n'+
    receipt.configurations.map(c=>'- '+c.name+': '+c.cases+' scenarios, '+c.checks+' checks, '+c.failures+' failures.').join('\n')+
    '\n- '+mutations.length+' semantic mutants compiled and killed with named assertion witnesses in both configurations. No compilation error, runner failure, exception, crash or timeout counts.\n\n'+
    'Actual main request/status/ops/source-key/outcome/complete/write-atomic/store/abort/advance/restore-trust and Session consumer bodies are compiled verbatim with actual registry declarations, session, transaction, lease and other real headers. The existing 31-scenario fixture is reused only for topology/globals/effect boundaries; its scenarios are not run by this entry point (a separate regression run does that).\n\n'+
    'Tests cover requests before handle creation, duplicate busy, missing/stale samples, pre-request samples refused for training, repeated receipt deduplication, timeout, four exact models, independent side persistence, unrelated/Windows record preservation, invalid files/values, all six file-boundary failures, save-before-apply, failed save rollback and explicit saved=false, confidence/offset refusal, source retirement matrix, old handle retirement, request replacement ID/UI fencing, logging exception during Abort, missing callbacks/create failure, stop cancellation and source retirement after persistence. Stored offset and trusted status restore on a fresh capture epoch/Session from the same single JSON read; old records without confidence and mismatched weights restore offset without claiming trust.\n\n'+
    'GetTickCount64, native C callbacks, event/log sinks and Win32 file APIs are controlled boundaries. Native callbacks forbid the Windows handle sentinel. File IO uses real JSON reads/writes only inside fresh evidence-private directories; MoveFileExW is a simulated replacement boundary, not a proof of real Windows atomicity or crash durability. The actual live product path provider never executes. A controlled log sink exception is tested; C ABI callbacks never throw. No product, DLL, HID, registry, real configuration, UI timer, Host, PBT or thread operation executes.\n\n'+
    'UI helper/timing is independently owned and not executed here. Native event fields are checked directly. Complete Vue UI, gyro IPC registration, full onGamepad, shutdown/emergency and OS sleep delivery are source-only anchors, not executed. Session revoke and controlled generation/lifecycle retirement execute. No full-chain or hardware acceptance is claimed.\n\n'+
    'Binding: '+JSON.stringify(receipt.production)+'. Frozen input stability='+receipt.ownedSourcesStable+'. Exact source bytes/hashes, build arguments, native effects, JSON artifacts and named mutant witnesses are retained.\n');
  const inventory=walk(out).filter(f=>path.basename(f)!=='FILES.json').map(file=>({path:relative(file),bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))}));
  json('FILES.json',{note:'Self inventory excluded',files:inventory});
  console.log(JSON.stringify({status:receipt.status,evidence:out,configurations:receipt.configurations.map(({name,cases,checks,failures})=>({name,cases,checks,failures})),mutations:receipt.mutations.length},null,2));
}catch(error){receipt.status='failed';receipt.error=error.stack||String(error);receipt.finishedAt=new Date().toISOString();save();console.error(receipt.error);console.error('Evidence: '+out);process.exitCode=1;}
