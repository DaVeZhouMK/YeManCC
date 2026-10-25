import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {controllerInputFixtureInputs,snapshotControllerInputFixture} from './lib/hc_controller_input_fixture.mjs';
// R122 bounded sidecar. Offline exact source slices; no product/MSI/Host/device run.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract,sha} from './fixtures/owned_return_settlement_r120/source-binding.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const opts={out:path.join(workspace,'Build/Validation/VirtualInput-CaptureProof-R122-20261016/xinput-sidecar')};
for(let i=2;i<process.argv.length;++i){
 if(process.argv[i]==='--help'){console.log('node tools/xinput_capture_freshness_r122_selftest.mjs [--out NEW_EVIDENCE_DIR]');process.exit(0);}
 assert.equal(process.argv[i],'--out');assert(process.argv[i+1]);opts.out=path.resolve(process.argv[++i]);
}
assert(!fs.existsSync(path.join(opts.out,'summary.json')),'Preserve completed evidence; use a new --out.');
fs.mkdirSync(opts.out,{recursive:true});
const evidenceRoot=opts.out;
const out=path.join(evidenceRoot,'runs',new Date().toISOString().replace(/[:.]/g,'-'));fs.mkdirSync(out,{recursive:true});
const sourceFile=path.join(repo,'native/main.cpp');
const fixtureFile=path.join(repo,'tools/fixtures/xinput_capture_freshness_r122/fixture.cpp');
const extractorFile=path.join(repo,'tools/fixtures/owned_return_settlement_r120/source-binding.mjs');
const scriptFile=fileURLToPath(import.meta.url);
const load=file=>{const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:sha(bytes)};};
const json=(name,value)=>fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
const source=load(sourceFile),fixture=load(fixtureFile),extractor=load(extractorFile),script=load(scriptFile);
function bind(s){
 const text=s.text,bindings=[];
 const readStart=text.indexOf('static void gamepadReadStateUnlocked(bool publishUi) {');assert(readStart>=0);
 const readEnd=text.indexOf('\nstatic ',readStart+10);assert(readEnd>readStart);
 function fragment(name,start,end){
  const a=text.indexOf(start,readStart),b=text.indexOf(end,a);assert(a>readStart&&b>a&&b<readEnd,'Missing bounded source anchors: '+name);
  assert.equal(text.indexOf(start,a+start.length)<readEnd&&text.indexOf(start,a+start.length)>=0,false,'Ambiguous start: '+name);
  const code=text.slice(a,b);
  bindings.push({file:s.file,kind:'exact-contiguous-block',symbol:name,startLine:text.slice(0,a).split('\n').length,endLine:text.slice(0,b).split('\n').length,sha256:sha(code),startUtf8Byte:Buffer.byteLength(text.slice(0,a)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,b))});
  return code;
 }
 const definition=(kind,name)=>{
  if(kind!=='enum')return extract(text,kind,name,s.file,bindings);
  // The existing extractor does not accept enum underlying types. Capture the
  // actual single-line declaration including that type, without rewriting it.
  const matches=[...text.matchAll(new RegExp('^enum class '+name+'\\b[^\\r\\n]*;','gm'))];assert.equal(matches.length,1,name);
  const m=matches[0],a=m.index,b=a+m[0].length;
  bindings.push({file:s.file,kind:'exact-enum-declaration',symbol:name,startLine:text.slice(0,a).split('\n').length,endLine:text.slice(0,b).split('\n').length,sha256:sha(m[0]),startUtf8Byte:Buffer.byteLength(text.slice(0,a)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,b))});return m[0];
 };
 const constant=name=>{
  const matches=[...text.matchAll(new RegExp('^static constexpr [^\\r\\n;]*\\b'+name+'\\s*=[^;]+;','gm'))];assert.equal(matches.length,1,name);
  const m=matches[0],a=m.index,b=a+m[0].length;
  bindings.push({file:s.file,kind:'constant',symbol:name,startLine:text.slice(0,a).split('\n').length,endLine:text.slice(0,b).split('\n').length,sha256:sha(m[0]),startUtf8Byte:Buffer.byteLength(text.slice(0,a)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,b))});return m[0];
 };
 const parts={
  TYPES:[definition('enum','PowerLifecycle'),definition('enum','XInputSlotIdentity'),definition('struct','InputCaptureGamepadSnapshot'),...(/^struct MsiCanonicalReadProof\s*\{/m.test(text)?[definition('struct','MsiCanonicalReadProof')]:[])].join('\n'),
  CONSTANTS:['kXInputSlotReprobeBackoffMs','kXInputSlotDiscoveryIntervalMs','kXInputPermissiveAnyConnectedInput','kInputStickCenterDeadzone'].map(constant).join('\n'),
  METHODS:['currentPowerGeneration','sgVirtualWriterAllowedNow','xinputNonVirtualConnectedMask','xinputPhysicalConnectedMask','xinputSlotIdentityVerdict','xinputSlotsMarkDiscoveryDirty','hidHideUpper','hidHideIsVirtualIdentity','xinputFeedbackIdentityProbe','inputCanonicalizeStickAxis','inputCanonicalizeGamepad','msiControllerNeutralCleanupAllowed','inputHostPhysicalPublicationSourceAdmitted','inputHostFrameSubmissionSample','inputHostFrameSubmissionCurrent',...(/^static void msiDinputApplyCanonicalReadProof/m.test(text)?['msiDinputApplyCanonicalReadProof']:[])].map(name=>definition('function',name)).join('\n\n'),
  READ_PREFIX:fragment('XInput read-start + clear + four-slot GetState loop + discovery bookkeeping','    GpReadScope gpReadCost;','    // L3（需求单）'),
  ADMISSION:fragment('XInput physical source admission','    const DWORD physicalMask = xinputPhysicalConnectedMask();','    // R10-R2 M3'),
  SELECTION:fragment('XInput single-source canonical selection','    bool multipleActive = false;','    // GP-MSIG3E-3（Layer B）'),
  STAMP:fragment('snapshot assignment + read-start provenance + readContextStable','        g_inputCaptureGamepadSnapshot = {live, pad, g_xinputPrimarySlot, admittedMask, inputSource};','        g_inputCaptureGamepadSnapshot.readContinuityGeneration =')
 };
 assert(parts.READ_PREFIX.includes('g_xinputPads.fill(XINPUT_GAMEPAD{});'));
 assert(!parts.READ_PREFIX.includes('msiDinputCanonicalSnapshot'));
 assert(!parts.SELECTION.includes('msiDinputCanonicalSnapshot'));
 return {parts,bindings};
}
const extracted=bind(source);
const dependencyNames=[...controllerInputFixtureInputs,'native/gamepad_backend_cost.h','native/xinput_identity_refresh_policy.h','native/input_frame_submission.h','native/touch_haptic_dispatch.h','native/virtual_output_status.h','deps/json/json.hpp'];
const dependencies=dependencyNames.map(name=>load(path.join(repo,name)));
const inc=path.join(out,'include');fs.mkdirSync(inc);
for(const d of dependencies)fs.writeFileSync(path.join(inc,path.basename(d.file)),d.bytes);
const controllerFixture=snapshotControllerInputFixture(repo,path.join(out,'controller-input'));
let code=controllerFixture.preamble+fixture.text;
for(const [name,part]of Object.entries(extracted.parts)){
 const marker='@@'+name+'@@';assert.equal(code.split(marker).length,2,'Fixture marker '+name);
 code=code.replace(marker,()=>part);
}
assert(!/@@\w+@@/.test(code));
fs.writeFileSync(path.join(out,'fixture.generated.cpp'),code);
const sourceBinding={
 sourceSHA:source.sha256,sourceFile,sourceRanges:extracted.bindings,
 extractor:{file:extractorFile,sha256:extractor.sha256},fixture:{file:fixtureFile,sha256:fixture.sha256},selftest:{file:scriptFile,sha256:script.sha256},
 dependencies:dependencies.map(({file,sha256})=>({file,sha256})),sourceSubstitutions:[],fullReadStateExtracted:false,
 exactness:'Complete named helper definitions use the existing R120 extractor. Four contiguous ReadState blocks use unique exact anchors; original bytes and statements are inserted unchanged. No handwritten replacement sampling, freshness, lifecycle or ticket algorithm.',
 exclusions:['MSI canonical/fallback/permit/cache','ReadState physical-drop/reconfirmation and other OEM fallbacks','snapshot mutex/continuity/diagnostic/UI/consumer paths','real Win32 XInput and device-internal freshness','Host transport/device/Steam/services/system sleep and field logs'],
 standins:['Fake GetState returns include deliberately unchanged packet and failure with stale output bytes. No Windows/XInput import is linked.','Fake clock and external lifecycle/power/topology/identity/Host-ready facts. Actual sgVirtualWriterAllowedNow, frame Current/Sample and production header decide/source execute.','Standby policy decision input is stubbed, not the actual machine-family/platform detector.','Log sink inert. Actual identity observe/disconnect runs; feedback scheduler is a throwing guard.','Feedback source-stamping callback is inert; actual SourceToken layout is included, no physical feedback writer is admitted.','Capture/virtual requested and persona-match facts are explicit shim inputs.','Single thread, no production lock. Static read-sequence storage in shim; actual assignment and stable predicate unchanged.','tickCostEnabled=false throughout; backend spans cannot record elapsed time. No benchmark.'],
 driftPolicy:'Compare named slices and dependencies at end; unrelated main.cpp full-file drift is recorded without claiming whole-file acceptance.'
};
json('source-binding.json',sourceBinding);
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars),'Set YMCC_VCVARS64 to vcvars64.bat');
const quote=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
const batch=path.join(out,'compile.cmd');
fs.writeFileSync(batch,calibrationToolchainPreamble(vcvars)+'set VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /WX /O2 '+quote(path.join(out,'fixture.generated.cpp'))+' /I'+quote(inc)+' /Fo'+quote(path.join(out,'fixture.obj'))+' /Fe'+quote(path.join(out,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');
function command(label,exe,args,expected){
 const r=spawnSync(exe,args,{cwd:out,encoding:'utf8',timeout:60000});
 fs.writeFileSync(path.join(out,label+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(out,label+'.stderr.log'),r.stderr??'');
 json(label+'.command.json',{exe,args,cwd:out,expected,status:r.status,error:r.error?String(r.error):null});
 assert(!r.error&&r.status===expected,label+' failed; see '+out);return r.stdout;
}
command('build','cmd.exe',['/d','/c',batch],0);
const results=JSON.parse(command('execute',path.join(out,'fixture.exe'),[],0));
assert.equal(results.caseCount,24);assert.equal(results.failedCases,0);assert(results.checks>=85);
json('results.json',results);
const end=load(sourceFile),atEnd=bind(end);
const changed=extracted.bindings.filter(b=>!atEnd.bindings.some(v=>v.symbol===b.symbol&&v.sha256===b.sha256));
const changedDependencies=dependencies.filter(d=>load(d.file).sha256!==d.sha256);
assert.equal(changed.length,0,'Related source slices changed during run: '+changed.map(b=>b.symbol).join(', '));
assert.equal(changedDependencies.length,0,'Dependencies changed during run');
assert.equal(load(fixtureFile).sha256,fixture.sha256);assert.equal(load(extractorFile).sha256,extractor.sha256);assert.equal(load(scriptFile).sha256,script.sha256);
const summary={passed:true,conclusion:'no-production-change: XInput cached-button reuse suspicion not reproduced in the executed scope',sourceSHA:source.sha256,sourceSHAAtEnd:end.sha256,relatedSlicesStable:true,fullSourceChanged:source.sha256!==end.sha256,caseCount:results.caseCount,checks:results.checks,failedCases:results.failedCases,readCount:results.readCount,canonicalCalls:results.canonicalCalls,ordinalCalls:results.ordinalCalls,productionFilesWritten:[],oldTestsWritten:[],productHostDeviceSteamServiceLaunched:false,cpuBenchmark:false,output:out};
json('summary.json',summary);fs.writeFileSync(path.join(evidenceRoot,'summary.json'),JSON.stringify(summary,null,2)+'\n');
const line=name=>{const r=extracted.bindings.find(b=>b.symbol===name);assert(r);return r.startLine+'–'+r.endLine;};
const report=`# R122 XInput bounded sidecar — no production change\n\n当前冻结源码的隔离结果：${results.caseCount} cases / ${results.checks} checks，${results.failedCases} failures；${results.readCount} 次片段读取、${results.canonicalCalls} 次 canonical GetState、${results.ordinalCalls} 次独立 ordinal 调用。未复现把旧 XInput 按钮缓存签成新读。不是完整产品验收。\n\nsourceSHA (native/main.cpp, start): \`${source.sha256}\`\n\nsourceSHA at end: \`${end.sha256}\`; selected slices/dependencies stable: true; unrelated full-file drift: ${summary.fullSourceChanged}.\n\n## 实际来源\n\n- native/main.cpp:${line('XInput read-start + clear + four-slot GetState loop + discovery bookkeeping')}：读起点 power/topology/identity 快照；每轮清 connectedMask/pads/special；known 每拍 canonical GetState，unknown 仅 discovery；成功才采纳 Gamepad，不比较 packet；失败清 known/身份并标脏。\n- native/main.cpp:${line('XInput physical source admission')} 与 ${line('XInput single-source canonical selection')}：实际物理掩码与单源选择；未解析身份不因 API 成功自动取得授权。\n- native/main.cpp:${line('snapshot assignment + read-start provenance + readContextStable')}：本轮 readAttemptSequence/起止 tick/起始三代保留；读后比较 power/topology/identity，跨代 readContextStable=false。\n- native/main.cpp:${line('sgVirtualWriterAllowedNow')}、${line('inputHostFrameSubmissionSample')}、${line('inputHostFrameSubmissionCurrent')}，以及本轮冻结的 native/input_frame_submission.h、native/virtual_output_status.h：实际 phase/power revocation/输入 readiness 与 read-window 二次准入。精确每段字节偏移、行号和 SHA 见 source-binding.json。\n\n## 覆盖\n\n${results.cases.map(r=>'- '+r.name+': '+r.checks+' checks, '+r.reads+' reads.').join('\n')}\n\n稳定 packet（包括 packet=7 不变而按钮改变）仍逐拍读；全零成功帧保留 live 与有效源，不能作为无信号/断线。失败 API 故意写回旧非零字节，仍被实际失败分支丢弃。缓存注毒覆盖四槽，unknown 未发现时没有 API 调用且不得继承。未知槽 discovery 计时验证 99ms/100ms，两种事件触发也执行实际分支。\n\n读中 power/topology/identity 分别变化：保留读起始代际，readContextStable=false；即使为当前 Host/power 新造 ticket，旧 read 仍 SourceRetired。读后到准入前变化：历史 stable 标记可以保留 true，但实际 decide/source 复核代际，旧票据/新票据均不允许旧读冒用新上下文。新代际真实再读的恢复控制通过。\n\n状态与授权分开：同代 stable/live 的读取在 Suspending/Suspended 或 Resuming 且无 keep-writer 时仍 PowerUnavailable；Resuming + 现存 keep-writer 政策 + 当前代际新读可以 Allowed。revoked/input-not-ready 仍关闭准入。readContextStable 本身只证明读取上下文三代相等，不代表设备有信号或当前处于人工唤醒后。full neutral 清理允许源已退休，但不能越过 power/target/intent 屏障，也不等同 live 源授权。年龄边界 1500ms 通过，1501ms 退休；该边界来自现有生产窗口，未引入新节拍。\n\n## 现场关联与边界\n\n父级提供的 G3E184736 线索将 2026-10-16 18:44:10.364 的 code18 自动恢复与 18:44:33.584 的 Kernel507 reason1 分开。前者之后 keep-writer/shared-ready/代2帧应按已有 Resuming/Ready 授权政策核对，不能自动计为人工唤醒恢复，也不能仅因全零或相同 packet 认定停采。本 sidecar 不读现场日志，时间线未独立核验。父级说明 ROG/Win5 未留系统 sleep/wake 边界，因此本轮不把它们认定为睡眠故障。\n\n未覆盖：MSI canonical cache/permit/票据与 ABI；整 ReadState/OEM 回退/物理源重确认；真实睡眠/唤醒的事件生产与时序；真实 XInput DLL/驱动内部是否返回旧状态；真实线程并发/锁与 OS memory ordering；Host 传输/目标提交/USB/HID/Steam 消费；现场根因；CPU 或整机收益。输入 phase/generation/standby-policy/Host facts 为可控 stand-ins，消费者谓词为实际源码。只按实际观察边界给 no-change，不把单个片段测试扩成整个采集到 Host 的证据。\n\n未启动产品/Host/设备/Steam/服务，未改生产或旧测试，未部署；只启动编译器和无 Win32/XInput 链接的隔离夹具。全部新增文件路径见 FILES.json。完成后停止写入。\n`;
fs.writeFileSync(path.join(out,'REPORT.md'),report);fs.writeFileSync(path.join(evidenceRoot,'REPORT.md'),report);
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(d=>d.isDirectory()?walk(path.join(dir,d.name)):[path.join(dir,d.name)]);}
const inventoryFile=path.join(evidenceRoot,'FILES.json');
const files=[scriptFile,fixtureFile,...walk(evidenceRoot),inventoryFile].sort().map(file=>file===inventoryFile?{path:file,sha256:null,note:'self inventory'}:{path:file,bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))});
fs.writeFileSync(inventoryFile,JSON.stringify({files},null,2)+'\n');
console.log(JSON.stringify(summary,null,2));