// R124 bounded local cost comparison. New output roots only, diagnostic off.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {freeze,bindCurrent,load,sha} from './fixtures/msi_canonical_proof_cost_r124/source-binding.mjs';
import {extract} from './fixtures/owned_return_settlement_r120/source-binding.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const opts={out:path.join(workspace,'Build/Validation/VirtualInput-MsiPump-R124-20261017/cost-sidecar')};
for(let i=2;i<process.argv.length;++i){
 if(process.argv[i]==='--help'){console.log('node tools/msi_canonical_proof_cost_r124_selftest.mjs [--out NEW_DIRECTORY]');process.exit(0);}
 assert.equal(process.argv[i],'--out');assert(process.argv[i+1]);opts.out=path.resolve(process.argv[++i]);
}
assert(!fs.existsSync(opts.out),'Never overwrite evidence; use --out with a new directory');
const root=opts.out;
const {binding,frozen}=freeze(repo,root);
const codeDir=path.join(repo,'tools/fixtures/msi_canonical_proof_cost_r124');
const script=load(fileURLToPath(import.meta.url));
const harness=fs.readdirSync(codeDir).map(n=>load(path.join(codeDir,n)));
const existingExtractor=load(path.join(repo,'tools/fixtures/owned_return_settlement_r120/source-binding.mjs'));
const run=path.join(root,'run');fs.mkdirSync(run);
for(const h of harness)if(h.file.endsWith('.cpp')||h.file.endsWith('.hpp'))fs.writeFileSync(path.join(run,path.basename(h.file)),h.bytes);
const write=(name,obj)=>fs.writeFileSync(path.join(root,name),JSON.stringify(obj,null,2)+'\n');
write('harness-binding.json',{script:{file:script.file,sha256:script.sha256},harness:harness.map(({file,sha256})=>({file,sha256})),existingExtractor:{file:existingExtractor.file,sha256:existingExtractor.sha256},sourceCodeSubstitutions:[],compiler:'MSVC x64 /O2 /Ob2 /GL- /std:c++20 /W3 /WX; three separate TUs, no LTCG',timing:'QueryThreadCycleTime primary; GetThreadTimes and QPC secondary. Process-name snapshots are outside measured loops, at batch boundaries only.',measurementPolicy:'12 AB pairs per stage, alternating AB/BA. All samples retained; no selection based on speed or observed competing compiler. CPU cycles do not include descheduled waits but remain sensitive to frequency, migration, thermal/cache/shared resource interference.'});
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vcvars));
const q=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
const flags='/nologo /std:c++20 /utf-8 /EHsc /MT /W3 /WX /O2 /Ob2 /GL-';
let batch='@echo off\r\ncall '+q(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\n';
for(const name of ['baseline','current','benchmark']){
 batch+='cl '+flags+' /c '+q(path.join(run,name+'.cpp'))+' /I'+q(run)+' /I'+q(frozen)+' /Fo'+q(path.join(run,name+'.obj'))+(name!=='benchmark'?' /FAs /Fa'+q(path.join(run,name+'.asm')):'')+'\r\nif errorlevel 1 exit /b 1\r\n';
}
batch+='cl /nologo '+['baseline','current','benchmark'].map(n=>q(path.join(run,n+'.obj'))).join(' ')+' /Fe'+q(path.join(run,'cost.exe'))+'\r\nexit /b %errorlevel%\r\n';
fs.writeFileSync(path.join(run,'compile.cmd'),batch);
function command(label,exe,args,expected){
 const r=spawnSync(exe,args,{cwd:run,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(run,label+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(run,label+'.stderr.log'),r.stderr??'');
 write(label+'.command.json',{exe,args,cwd:run,expected,status:r.status,error:r.error?String(r.error):null});assert(!r.error&&r.status===expected,label+' failed; logs in '+run);return r.stdout;
}
command('build','cmd.exe',['/d','/c',path.join(run,'compile.cmd')],0);
const result=JSON.parse(command('measure',path.join(run,'cost.exe'),[],0));
write('results.json',result);
assert(result.passed&&!result.diagnostic&&result.singleMeasurementThread);assert.equal(result.batches.length,84);
const quantile=(v,p)=>{const a=[...v].sort((x,y)=>x-y),i=(a.length-1)*p;return a[Math.floor(i)]+(a[Math.ceil(i)]-a[Math.floor(i)])*(i%1);};
const median=v=>quantile(v,0.5);
const comparisons={};
for(const stage of ['publish','snapshot-with-requested-proof','pipeline']){
 const rows=result.batches.filter(b=>b.stage===stage),pairs=[];
 for(let p=0;p<12;++p){const a=rows.find(b=>b.pair===p&&b.variant==='r123-baseline'),b=rows.find(b=>b.pair===p&&b.variant==='current-proof');assert(a&&b&&a.digest===b.digest&&a.iterations===b.iterations);assert.equal(a.order,p%2);assert.equal(b.order,1-p%2);pairs.push({pair:p,baselineCycles:a.cyclesPerOperation,currentCycles:b.cyclesPerOperation,deltaCycles:b.cyclesPerOperation-a.cyclesPerOperation,relativePercent:(b.cyclesPerOperation/a.cyclesPerOperation-1)*100,wallDeltaNs:b.wallNsPerOperation-a.wallNsPerOperation});}
 comparisons[stage]={pairs:12,iterationsPerBatch:rows[0].iterations,baselineMedianCycles:median(pairs.map(p=>p.baselineCycles)),currentMedianCycles:median(pairs.map(p=>p.currentCycles)),pairedMedianDeltaCycles:median(pairs.map(p=>p.deltaCycles)),pairedDeltaCyclesP10:quantile(pairs.map(p=>p.deltaCycles),0.1),pairedDeltaCyclesP90:quantile(pairs.map(p=>p.deltaCycles),0.9),pairedMedianRelativePercent:median(pairs.map(p=>p.relativePercent)),pairedMedianWallDeltaNs:median(pairs.map(p=>p.wallDeltaNs)),positiveDeltaPairs:pairs.filter(p=>p.deltaCycles>0).length,pairDetails:pairs};
}
const applyRows=result.batches.filter(b=>b.stage==='apply-current-only');
const compilerMap=new Map();let observedCompilerBatches=0,unknownCompilerBatches=0;
for(const b of result.batches){let observed=false,unknown=false;for(const snapshot of [b.compilerProcessesBefore,b.compilerProcessesAfter]){if(!Array.isArray(snapshot)){unknown=true;continue;}for(const p of snapshot){observed=true;compilerMap.set(p.name+':'+p.pid,p);}}observedCompilerBatches+=observed;unknownCompilerBatches+=unknown;}
const functionCode=(file,name)=>extract(fs.readFileSync(path.join(frozen,file),'utf8'),'function',name,path.join(frozen,file),[]);
const off=t=>t.replace(/#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC[\s\S]*?#endif/g,'');
const counts=t=>({fetchAdd:(off(t).match(/\.fetch_add\(/g)??[]).length,stores:(off(t).match(/\.store\(/g)??[]).length,loads:(off(t).match(/\.load\(/g)??[]).length});
const operations={baselinePublish:counts(functionCode('baseline-functions.inc','msiDinputPublishCanonicalFrame')),currentPublish:counts(functionCode('current-functions.inc','msiDinputPublishCanonicalFrame')),baselineSnapshot:counts(functionCode('baseline-functions.inc','msiDinputCanonicalSnapshot')),currentRequestedProofSnapshot:counts(functionCode('current-functions.inc','msiDinputCanonicalSnapshot')),currentApplyDirect:counts(functionCode('current-functions.inc','msiDinputApplyCanonicalReadProof')),currentPowerAccessor:counts(functionCode('current-functions.inc','currentPowerGeneration')),note:'Source-level atomic operations on stable successful diagnostic-off path, current proof pointers nonnull and four contexts matching. Not an instruction count or a whole-pump operation count; snapshots retry under contention are excluded.'};
assert.deepEqual(operations.baselinePublish,{fetchAdd:2,stores:3,loads:0});assert.deepEqual(operations.currentPublish,{fetchAdd:3,stores:9,loads:0});assert.equal(operations.currentRequestedProofSnapshot.loads-operations.baselineSnapshot.loads,7);
for(const f of binding.frozenFiles)assert.equal(sha(fs.readFileSync(path.join(frozen,f.name))),f.sha256,'Frozen input changed');
for(const h of [script,...harness,existingExtractor])assert.equal(load(h.file).sha256,h.sha256,'Harness changed');
const end=load(binding.currentSourceFile);let sourceStability;
try{const now=bindCurrent(end).ranges;const changed=binding.currentRanges.filter(r=>r.symbol!=='MsiDinputState'&&!now.some(n=>n.symbol===r.symbol&&n.sha256===r.sha256));sourceStability={relatedSlicesStable:changed.length===0,changedSymbols:changed.map(r=>r.symbol),fullFileChanged:end.sha256!==binding.currentSourceSHA};}catch(error){sourceStability={relatedSlicesStable:false,error:String(error),fullFileChanged:end.sha256!==binding.currentSourceSHA};}
const measuredWallSeconds=result.batches.reduce((s,b)=>s+b.wallNs,0)/1e9;
const summary={passed:true,baselineSourceSHA:binding.baselineSourceSHA,currentSourceSHA:binding.currentSourceSHA,currentSourceSHAAtEnd:end.sha256,sourceStability,diagnostic:false,checks:result.checks,measurementBatches:result.batches.length,calibrationBatches:result.calibration.length,measuredLoopWallSeconds:measuredWallSeconds,maxMeasuredBatchMs:Math.max(...result.batches.map(b=>b.wallNs))/1e6,comparisons,
 currentApplyOnly:{samples:applyRows.length,iterationsPerBatch:applyRows[0].iterations,medianCycles:median(applyRows.map(b=>b.cyclesPerOperation)),medianWallNs:median(applyRows.map(b=>b.wallNsPerOperation)),noBaselineEquivalent:true},
 interference:{observedCompilerBatches,totalBatches:result.batches.length,unknownCompilerBatches,observedProcesses:[...compilerMap.values()],endpointCoreChanges:result.batches.filter(b=>b.coreBefore!==b.coreAfter).length,boundarySamplingCannotExcludeShortLivedCompilersOrOtherWork:true,retainedAllSamples:true},
 fieldSubsetBytes:{baseline:result.baselineFieldSubsetBytes,current:result.currentFieldSubsetBytes,difference:result.currentFieldSubsetBytes-result.baselineFieldSubsetBytes,scope:'compiled required-field shim only; not complete production object'},operations,productionWritten:[],oldTestsWritten:[],productHostDeviceSteamServiceStarted:false,scope:'Local single-thread cost microbenchmark only. No product CPU, field-cause, complete pump, safety-policy or proof-removal claim.',root};
write('summary.json',summary);
const num=n=>n.toFixed(2);
const rows=Object.entries(comparisons).map(([stage,c])=>`| ${stage} | ${num(c.baselineMedianCycles)} | ${num(c.currentMedianCycles)} | ${num(c.pairedMedianDeltaCycles)} | ${num(c.pairedDeltaCyclesP10)} … ${num(c.pairedDeltaCyclesP90)} | ${num(c.pairedMedianRelativePercent)}% |`).join('\n');
const hashes=rs=>rs.map(r=>`- ${r.symbol}, original lines ${r.startLine}–${r.endLine}: \`${r.sha256}\``).join('\n');
const report=`# R124 local canonical proof cost sidecar\n\n只测限定函数的局部单线程成本；不据此改生产、删安全证明或推断整机 CPU/实机根因。CtrlAlt 排除，无权限扩大。\n\nR123 baseline sourceSHA: \`${binding.baselineSourceSHA}\`\n\nCurrent frozen sourceSHA: \`${binding.currentSourceSHA}\`\n\nCurrent sourceSHA at end: \`${end.sha256}\`; related slices stable: ${sourceStability.relatedSlicesStable}; unrelated full-file drift: ${sourceStability.fullFileChanged}. 函数与字段原文及完整来源范围/UTF-8 偏移均已冻结，见 source-binding.json 与 frozen/。\n\n## 测量事实\n\n标准 diagnostic-off，MSVC x64 /O2 /Ob2 /GL- /W3 /WX；三个独立 TU，没有跨 TU LTO。每组 warmup 5000 次，每个 A/B stage 12 paired batches，AB/BA 交替，业务输入/输出校验和逐 pair 相同。proof 输出由跨 TU 指针逃逸并在批末观测，volatile sink 保留相同业务累加器，未手写替代 publish/snapshot/apply。${result.checks} checks 通过。\n\n${result.batches.length} 个正式 batch，另 ${result.calibration.length} 个校准 batch。正式循环墙钟合计 ${num(measuredWallSeconds)} 秒，最长单批 ${num(summary.maxMeasuredBatchMs)}ms；每批目标 20ms，超过 1000ms 拒收。主要指标 QueryThreadCycleTime/thread cycles/op，另记录 GetThreadTimes 与 QPC。线程 cycles 排除被调度出时的等待，但仍受频率/核心迁移/热状态/共享缓存影响；不能换算为固定机器 CPU 收益。\n\n| stage | A median cycles/op | B median cycles/op | paired median B−A | paired delta P10…P90 | paired relative median |\n| --- | ---: | ---: | ---: | ---: | ---: |\n${rows}\n\nApply 仅当前版本：median ${num(summary.currentApplyOnly.medianCycles)} cycles/op，${num(summary.currentApplyOnly.medianWallNs)} wall ns/op，无虚构 baseline。成对统计保留全部批次；分位数只描述本次样本，不是置信区间。独立阶段和组合的编译优化不同，不把各阶段差值相加。\n\n## 干扰\n\n${observedCompilerBatches}/${result.batches.length} 正式批次在边界看到编译/构建候选进程；未知快照 ${unknownCompilerBatches}，核心端点变化 ${summary.interference.endpointCoreChanges}。观察到：${[...compilerMap.values()].map(p=>p.name+' pid='+p.pid).join(', ')||'none at sampled boundaries'}。父级可能同时构建完整 TU；结果必须带此干扰标记。只在边界读进程名，可能漏掉批中短进程及同核迁出再迁回；空采样也不证明环境无干扰。未停止进程、未改优先级/亲和性或电源设置，未按速度或干扰挑样。\n\n## 操作新增与可比性\n\n- 成功发布：在旧版 2 个 fetch_add、3 个 store 上，新增 1 个 canonicalReadSequence fetch_add 和 6 个 proof atomic store。\n- 请求 proof 的成功稳定 snapshot：比旧版多 7 个 proof atomic load，另有证明清零与结果复制。\n- actual Apply：7 个来源字段赋值 + stable 布尔更新；本匹配路径读取 4 个当前代际原子（含 actual currentPowerGeneration accessor），均为 acquire。源级操作计数不等同机器指令数。\n- required-field subset 大小 ${result.baselineFieldSubsetBytes}→${result.currentFieldSubsetBytes}（+${summary.fieldSubsetBytes.difference}）字节，仅隔离字段容器；未编译完整生产 MsiDinputState，不推断整对象/整进程内存收益。\n- A pipeline 是实际旧 publish+snapshot；B 是当前 publish（给 producer proof）+snapshot（请求 proof）+actual Apply。相同的是业务负载，B 多做证明是测量目标，不声称安全语义相同。两侧均用当前 recipient 类型仅作为公共输出容器，不伪造旧证明实现。\n- 输入表含静态零及非零按钮/轴/扳机；标准 DLL XINPUT_GAMEPAD 类型来自 SDK，但不调用/链接 XInput 设备函数。snapshot-only 在循环外预发布一次，循环重复读取稳定有效缓存；publish/组合每次有真实生产写；Apply 只测已有 matching proof。\n- 计时含共同工作表索引、函数指针 wrapper 边界与 observable digest，未扣除这些成本。原函数可在 wrapper 内按 /O2 自动内联，生产 TU 的 LTO/内联/布局/缓存行为可能不同；单线程隔离、无竞争，不是实际频率或整机占用量测。\n\n## 完整来源哈希\n\n### R123 baseline\n\n${hashes(binding.baselineRanges)}\n\n### Current\n\n${hashes(binding.currentRanges)}\n\n## 未覆盖\n\n完整 msiDinputPump、InputLost/失败/跨代、pause/permit/fallback/BUS/Host 调用和锁、真实线程竞争及 seqlock 重试、设备/驱动/服务/Steam、生产采样频率、现场根因与整机 CPU 均不覆盖，由父级对应审计处理。只有编译器与新离线测试子进程执行；生产和旧测试无写入，无安全证明或全局授权变更。全部新增路径与哈希见 FILES.json。完成后停写。\n`;
fs.writeFileSync(path.join(root,'REPORT.md'),report);
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(d=>d.isDirectory()?walk(path.join(dir,d.name)):[path.join(dir,d.name)]);}
const inventory=path.join(root,'FILES.json');
write('FILES.json',{files:[script.file,...harness.map(h=>h.file),...walk(root),inventory].sort().map(file=>file===inventory?{path:file,sha256:null,note:'self inventory'}:{path:file,bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))})});
console.log(JSON.stringify({...summary,comparisons:Object.fromEntries(Object.entries(comparisons).map(([k,{pairDetails,...v}])=>[k,v]))},null,2));