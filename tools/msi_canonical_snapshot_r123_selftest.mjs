// R123 offline canonical baseline. New files only; never run product or old scripts.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {freezeBaseline,bind,load,sha} from './fixtures/msi_canonical_snapshot_r123/source-binding.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const opts={out:path.join(workspace,'Build/Validation/VirtualInput-MsiCanonical-R123-20261017/canonical-sidecar')};
for(let i=2;i<process.argv.length;++i){
 if(process.argv[i]==='--help'){console.log('node tools/msi_canonical_snapshot_r123_selftest.mjs [--out NEW_EVIDENCE_ROOT]');process.exit(0);}
 assert.equal(process.argv[i],'--out');assert(process.argv[i+1]);opts.out=path.resolve(process.argv[++i]);
}
const root=opts.out;
assert(!fs.existsSync(path.join(root,'summary.json')),'Preserve completed evidence; use a new --out.');
const bindingFile=path.join(root,'source-binding.json');
if(!fs.existsSync(bindingFile))freezeBaseline(repo,root);
const baseline=JSON.parse(fs.readFileSync(bindingFile,'utf8'));
const frozen=path.join(root,'baseline');
function verifyFrozen(){for(const f of baseline.frozenInputs)assert.equal(sha(fs.readFileSync(path.join(frozen,f.name))),f.sha256,'Frozen input changed: '+f.name);}
verifyFrozen();
const sourceStart=load(path.join(repo,'native/main.cpp'));
const script=load(fileURLToPath(import.meta.url));
const fixture=load(path.join(repo,'tools/fixtures/msi_canonical_snapshot_r123/fixture.cpp'));
const localBinder=load(path.join(repo,'tools/fixtures/msi_canonical_snapshot_r123/source-binding.mjs'));
const existingExtractor=load(path.join(repo,'tools/fixtures/owned_return_settlement_r120/source-binding.mjs'));
const reference=load(path.join(repo,'tools/msi_native_report_chain_pair_selftest.cpp'));
const run=path.join(root,'runs',new Date().toISOString().replace(/[:.]/g,'-'));
fs.mkdirSync(run,{recursive:true});
fs.writeFileSync(path.join(run,'fixture.cpp'),fixture.bytes);
const write=(dir,name,obj)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(obj,null,2)+'\n');
write(run,'test-binding.json',{
 baselineSourceSHA:baseline.baselineSourceSHA,baselineFrozenAt:baseline.frozenAt,baselineBinding:bindingFile,
 sourceSHAAtTestStart:sourceStart.sha256,
 harness:[script,fixture,localBinder,existingExtractor].map(({file,sha256})=>({file,sha256})),
 readOnlyInterfaceReference:{file:reference.file,sha256:reference.sha256,executed:false},
 sourceSubstitutions:[],
 extraction:'Existing exact extractor for three complete functions and full owner struct; selected original bindCount and contiguous canonical declarations compiled inside a field-subset shim. Original ifdefs retained. Full owner layout is NOT compiled.',
 externalInputs:'Only scalar XINPUT_GAMEPAD interface, supplied capture tick, bindCount, and controlled atomic states. No fake publish/snapshot algorithm, OS read, clock call, pump, permit, fallback or Host.',
 concurrency:'Single thread. Odd stopped seqlock is a direct state control, not a real concurrent interleaving or memory-order proof.',
 exclusions:['sleep-generation/permission policy (R122 already scoped)','full pump→pause/permit→fallback→BUS snapshot→Host order and locks','driver/OS device-internal sample age','Host/service/Steam/device execution','CtrlAlt','CPU or whole-machine performance'],
});
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars),'Set YMCC_VCVARS64 to vcvars64.bat');
const q=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
function command(dir,label,exe,args,expected){
 const result=spawnSync(exe,args,{cwd:dir,encoding:'utf8',timeout:60000});
 fs.writeFileSync(path.join(dir,label+'.stdout.log'),result.stdout??'');fs.writeFileSync(path.join(dir,label+'.stderr.log'),result.stderr??'');
 write(dir,label+'.command.json',{exe,args,cwd:dir,expected,status:result.status,error:result.error?String(result.error):null});
 assert(!result.error&&result.status===expected,label+' failed in '+dir);return result.stdout;
}
const profiles={};
for(const diagnostic of [false,true]){
 const name=diagnostic?'diagnostic-on':'standard',dir=path.join(run,name);fs.mkdirSync(dir);
 const batch=path.join(dir,'compile.cmd');
 fs.writeFileSync(batch,'@echo off\r\ncall '+q(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /WX /O2'+(diagnostic?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC=1':'')+' '+q(path.join(run,'fixture.cpp'))+' /I'+q(frozen)+' /Fo'+q(path.join(dir,'fixture.obj'))+' /Fe'+q(path.join(dir,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');
 command(dir,'build','cmd.exe',['/d','/c',batch],0);
 const result=JSON.parse(command(dir,'execute',path.join(dir,'fixture.exe'),[],0));
 assert.equal(result.failedCases,0);assert.equal(result.commonCases,12);assert.equal(result.caseCount,diagnostic?19:12);
 assert.equal(result.abi.reportTickFieldPresent,diagnostic);assert.equal(result.abi.reportSequenceFieldPresent,diagnostic);assert.equal(result.abi.reportGenerationFieldPresent,diagnostic);
 profiles[name]=result;write(dir,'results.json',result);
}
const commonRows=p=>p.cases.filter(c=>c.common).map(c=>({name:c.name,productionState:c.productionState}));
assert.deepEqual(commonRows(profiles.standard),commonRows(profiles['diagnostic-on']),'Diagnostic switch changed common production state');
verifyFrozen();
for(const h of [script,fixture,localBinder,existingExtractor])assert.equal(load(h.file).sha256,h.sha256,'Harness changed during execution: '+h.file);
const sourceEnd=load(sourceStart.file);
let currentRelated;
try{
 const endBindings=bind(sourceEnd).ranges;
 const changed=baseline.ranges.filter(b=>!endBindings.some(e=>e.symbol===b.symbol&&e.sha256===b.sha256));
 currentRelated={matchesFrozenBaseline:changed.length===0,changedSymbols:changed.map(c=>c.symbol)};
}catch(error){currentRelated={matchesFrozenBaseline:false,currentBindingError:String(error)};}
// Current parent source is allowed to evolve. Results remain explicitly bound to
// frozen inputs; a changed live source must never be presented as baseline-tested.
const summary={
 passed:true,baselineSourceSHA:baseline.baselineSourceSHA,baselineFrozenAt:baseline.frozenAt,
 sourceSHAAtTestStart:sourceStart.sha256,sourceSHAAtTestEnd:sourceEnd.sha256,currentRelated,
 profiles:Object.fromEntries(Object.entries(profiles).map(([name,r])=>[name,{cases:r.caseCount,commonCases:r.commonCases,checks:r.checks,failedCases:r.failedCases,publishCalls:r.publishCalls,snapshotCalls:r.snapshotCalls,invalidateCalls:r.invalidateCalls,abi:r.abi}])),
 totalCaseExecutions:Object.values(profiles).reduce((n,r)=>n+r.caseCount,0),totalChecks:Object.values(profiles).reduce((n,r)=>n+r.checks,0),
 commonProductionStateEquivalent:true,sourceImplementationsRewritten:false,productionWritten:[],oldTestsWritten:[],productHostDeviceSteamServiceStarted:false,benchmark:false,run,
 conclusion:'Baseline function contracts only: retained valid even cache is readable; reads neither renew producer state nor diagnostic provenance. This alone is not a pipeline bug or safe-pipeline conclusion.'
};
write(run,'summary.json',summary);write(root,'summary.json',summary);
const location=name=>{const r=baseline.ranges.find(r=>r.symbol===name);return `${r.startLine}–${r.endLine}`;};
const named=name=>baseline.ranges.find(r=>r.symbol===name).sha256;
const facts=Object.entries(summary.profiles).map(([name,r])=>`| ${name} | ${r.cases} | ${r.checks} | ${r.publishCalls} | ${r.snapshotCalls} | ${r.invalidateCalls} | ${r.failedCases} |`).join('\n');
const report=`# R123 canonical publish/snapshot baseline\n\n完成限定函数测试；不据保留缓存可读性判定完整管线有 bug，也不据本测试证明管线安全。父级正在独立核验完整 BUS 顺序、跨睡眠 wrapper、permit 与锁。本报告不放宽全局权限，CtrlAlt 排除。\n\nsourceSHA (frozen native/main.cpp): \`${baseline.baselineSourceSHA}\`\n\n冻结时刻：${baseline.frozenAt}（UTC）。精确原文已在父级可能新增 producer 证明字段前保留。当前源码与基线相关片段一致：${currentRelated.matchesFrozenBaseline}；当前 test end sourceSHA：\`${sourceEnd.sha256}\`。若后续变化，只能按此冻结 baseline 理解结果。\n\n## 精确来源与哈希\n\n- native/main.cpp:${location('MsiDinputState')} — 完整原始 MsiDinputState，SHA \`${named('MsiDinputState')}\`，保留原文但不编译完整布局。\n- native/main.cpp:${location('MsiDinputState.bindCount')} — 原始 bindCount 声明。\n- native/main.cpp:${location('MsiDinputState canonical field block (including original ifdef)')} — canonical 字段原文与原 ifdef，SHA \`${named('MsiDinputState canonical field block (including original ifdef)')}\`。\n- native/main.cpp:${location('msiDinputPublishCanonicalFrame')} — actual publish，SHA \`${named('msiDinputPublishCanonicalFrame')}\`。\n- native/main.cpp:${location('msiDinputInvalidateCanonicalFrame')} — actual invalidation 控制，SHA \`${named('msiDinputInvalidateCanonicalFrame')}\`。\n- native/main.cpp:${location('msiDinputCanonicalSnapshot')} — actual snapshot，SHA \`${named('msiDinputCanonicalSnapshot')}\`。\n- native/msi_virtual_report_diagnostic.h:${baseline.sourceStamp.startLine}–${baseline.sourceStamp.endLine} — actual SourceStamp，SHA \`${baseline.sourceStamp.sha256}\`。\n\n全部字节偏移、源码范围、完整依赖哈希见 root/source-binding.json；actual-fields.inc、actual-functions.inc、完整结构和实际诊断头均在 baseline/。所需字段原文编入 subset shim，不声称完整 MsiDinputState 或 Win32 ABI 已编译。\n\n## 实际执行\n\n两种配置均 /std:c++20 /W3 /WX 编译并运行，新测试使用相同冻结生产片段，不运行旧测试脚本。12 个共同 case 的最终生产字段逐项相等。\n\n| 配置 | cases | checks | publish | snapshot | invalidate | failures |\n| --- | ---: | ---: | ---: | ---: | ---: | ---: |\n${facts}\n\n共同覆盖：${profiles.standard.cases.map(c=>c.name).join('；')}。\n\n额外 diagnostic-on 覆盖：${profiles['diagnostic-on'].cases.filter(c=>!c.common).map(c=>c.name).join('；')}。\n\n## 函数事实\n\n1. publish 接收的静态全零与相同 payload 均有效。即使 payload 和 supplied capture tick 均不变，每次实际 publish 仍把生产 canonicalSeq 推进两次、canonicalValid=true；diagnostic ordinal 每次 publish 加一。不是按 payload 差分才发布。\n2. canonicalSeq 停在稳定偶数且 canonicalValid=true：snapshot 可重复成功读取保留 payload；观测时刻推进到 10001/100000/86410000 不改返回值。观测时刻只是夹具记录，未传给实际 snapshot；该函数没有 clock、age、当前 binding/power/permit 参数。读本身不递增 canonicalSeq、不更新 payload/valid，也不更新诊断 sequence/generation/tick。\n3. canonicalValid=false 时返回 false；actual invalidate 保留旧 payload 字节，但通过 +2 seqlock 提交 valid=false，snapshot 不再成功。奇数 seq 停写时实际最多四次尝试后 false；重新置稳定偶数是独立状态控制，读取恢复。所有失败保持调用方 out 原值，调用方必须检查 bool；diagnosticSource 若提供则入口先全部清零。\n4. standard 构建：实际字段组没有 canonicalReportSequence/Generation/Tick；publish/snapshot 函数指针均仅 1 参数。diagnostic-on 构建：这三个字段存在，两个函数指针均为 2 参数且原默认参数有效。编译期 requires/static_assert 与运行 ABI facts 双重记录。这只是本轮函数与字段范围的条件性，不是对整个产品所有 timestamp 的全局断言。\n5. canonicalSeq 是常开 uint32 seqlock：publish/invalidate +2，snapshot 不推进；测试 UINT32_MAX-1 发布后回零仍有效。它不是 64 位语义 sample ordinal。canonicalReportSequence 仅诊断编译存在，是 publish ordinal，invalidate/snapshot 不推进；canonicalReportGeneration 是发布时 bindCount，并非 Host epoch、power generation、source revision 或 seqlock。测试以 productionSeq=42 / diagnosticOrdinal=2^40+1 / binding=73 分离。\n6. SourceStamp 的七个 payload 字段来自同一稳定 bt/thumbs 读取；capturedTick 来自 publish 的 supplied argument。改变当前 bindCount 而未 publish 不会把旧 stamp 重签为新 binding。再次 publish 才更新生成代；已返回的旧 stamp 按值保留。默认 tick=0、generation=0 或诊断 ordinal=0 不阻止 snapshot 的 payload bool=true，但 stamp.valid=false；非零未来 tick 仍 stamp.valid=true，说明这里的 valid 是 provenance presence 条件，不是年龄/未来时间/当前代际授权。\n\n## 未覆盖\n\n完整 pump→pause/permit→fallback→BUS→Host 调用顺序、锁与真实并发交错，上一轮 payload 能否被新 wrapper 授权，跨睡眠代际完整场景，生产调用方是否正确处理 false，驱动/设备新鲜度与真实 Host/Steam 消费，producer 证明字段的后续修订，均不在本 sidecar 结论内。顺序 fixture 未模拟真实 seqlock 竞争或 memory ordering；没有拿 retained payload 的读取成功单独判 bug。睡眠代际沿用 R122 已核边界，没有新增放宽政策或 CtrlAlt 工作。\n\n未启动产品/Host/设备/Steam/服务，未 benchmark，未改生产或旧测试。只启动编译器与新隔离测试子进程。新增文件完整路径/哈希见 FILES.json，成功运行目录为 ${run.replaceAll('\\','/')}。完成即停止写入。\n`;
fs.writeFileSync(path.join(run,'REPORT.md'),report);fs.writeFileSync(path.join(root,'REPORT.md'),report);
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(d=>d.isDirectory()?walk(path.join(dir,d.name)):[path.join(dir,d.name)]);}
const inventoryFile=path.join(root,'FILES.json');
const files=[script.file,fixture.file,localBinder.file,...walk(root),inventoryFile].sort().map(file=>file===inventoryFile?{path:file,sha256:null,note:'self inventory'}:{path:file,bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))});
write(root,'FILES.json',{files});
console.log(JSON.stringify(summary,null,2));