// Real float module + controller, with an in-memory hardware boundary and a virtual clock.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const require=createRequire(path.resolve('package.json'));
const {transformSync}=require('esbuild');
let now=100000,side='ac',game={pid:101,processCreated:'birth',name:'fixture.exe'},fps=60,low=60,cpuBusy=20,cpuValid=true,gpu=null,frameAt=null,packetPid=101,cpuFailure=false,tdpFailure=false;
let acceptedCeiling=35,parameterWrites=[],initialParameterWrites=[],tdpWrites=[],holdTdp=false,releaseTdp=null;
class Clock extends Date {static now(){return now}}
function evaluate(file,imports={},suffix=''){
 const module={exports:{}};
 vm.runInNewContext(transformSync(fs.readFileSync(file,'utf8')+'\n'+suffix,{loader:'ts',format:'cjs'}).code,
 {module,exports:module.exports,require:key=>{if(key in imports)return imports[key];throw Error('Unexpected dependency '+key)},console,structuredClone,Date:Clock,performance:{now:()=>now},
 setInterval:()=>1,clearInterval(){},setTimeout:()=>1,clearTimeout(){},window:{setTimeout:()=>1,clearTimeout(){}}});
 return module.exports;
}
const controls=evaluate('src/bridge/performanceControls.ts');
const frameTime=evaluate('src/bridge/monitorFrameTime.ts');
const fpsHardware=evaluate('src/bridge/monitorFpsHardware.ts');
const snapshotSignals=fs.existsSync('src/bridge/monitorSnapshotSignals.ts')
 ? evaluate('src/bridge/monitorSnapshotSignals.ts',{'./monitorFrameTime':frameTime,'./monitorCoreTiles':evaluate('src/bridge/monitorCoreTiles.ts')}) : null;
const capability={ready:true,selected:true,minPL1:2,maxPL1:150};
const yeman={PW:{YEMAN:'fixture'},readTdp:async()=>120,detectPowerMode:async()=>side,detectPowerModeReliable:async()=>side,
 getActiveScheme:async()=> 'fixture',setActiveScheme:async()=>{},detectVendor:async()=> 'amd',getTdpHardwareMinimum:()=>2,getTdpHardwareMaximum:()=>150,
 getTdpExecutionCapability:async()=>capability,resolveTdpTarget:w=>({intentW:w,effectiveW:Math.max(2,Math.min(150,w))}),clampTdp:w=>w,
 readPowerParams:async()=>({}),readRtssLimit:async()=>60,setRtssLimit:async()=>{},ensureTdpDaemon:async()=>{},
 applyFloatPowerParams:async value=>{parameterWrites.push(structuredClone(value));return !cpuFailure},
 setTdp:async(s,w,opts)=>{tdpWrites.push({side:s,requested:w});if(holdTdp)await new Promise(resolve=>{releaseTdp=resolve});if(tdpFailure==='throw')throw Error('transport rejected');if(tdpFailure===true)return false;if(tdpFailure==='no-receipt')return true;opts.onAccepted?.(Math.min(w,acceptedCeiling));return true}};
const runtime=evaluate('src/bridge/autofloat.ts',{
 './gamePolicyTarget':{getPolicyGame:()=>game},'./performanceControls':controls,'./monitorFpsHardware':fpsHardware,'./monitorFrameTime':frameTime,'./monitorSnapshotSignals':snapshotSignals,
 './api':{fs:{writeTextFile:async()=>{},remove:async()=>{}}},'./ipc':{invoke:async()=>true},'./yeman':yeman,
 './settingsRepository':{getSettingsGeneration:()=>1,assertSettingsGeneration(){},readSettingsSection:async()=>({}),saveSettingsSection:async()=>{}},
 './monitorData':{acquireMonitorDemand:()=>()=>{},monitorDemandConfirmed:()=>({fps:true}),monitorSnapshotEpoch:()=>1,setMonitorDataPowerControlDir(){},
 readMonitorSnapshot:async()=>({collectedAt:now,heartbeatAt:now,hwinfoOkAt:now,hwDown:false,top:{cpuUsageValid:cpuValid,cpuUsageTs:now,ts:now,cpuUsage:cpuBusy,freqMhz:5000,cpuClockValid:true,gpuSelectionPresent:gpu!==null,gpuName:'Selected iGPU',gpuUsagePct:gpu},
 fps:{ts:frameAt??now,fps,fps1:low,gpu:95,game:'fixture.exe',pid:packetPid,processCreated:'birth',fpsSource:'hwinfo-presented',fpsObservedAt:frameAt??now,fpsValidUntil:now+5000,fpsPollPeriodMs:1000,fps1Source:'hwinfo-presented-1percent-low',fps1ObservedAt:frameAt??now,fps1ValidUntil:now+5000,fps1PollPeriodMs:1000,hardwareObservedAt:now,hardwareValidUntil:now+5000,hardwarePollPeriodMs:1000}})},
},'export const __test={tick,settled:async()=>{await peripheralInit},bank:()=>continuousOwner?.bank,state:()=>continuousOwner?.state,force:state=>{if(continuousOwner){Object.assign(continuousOwner.state,state);if(state.watts!==undefined)tdpLimit=state.watts}}};');
const cases=[];
async function check(name,run){try{await run();cases.push({name,pass:true})}catch(error){cases.push({name,pass:false,error:error.stack})}}
const step=async()=>{now+=1000;await runtime.__test.tick()};
async function start(bias=50,watts=35,saving='aggressive'){
 await runtime.disableFloat();side='ac';game={pid:101,processCreated:'birth',name:'fixture.exe'};fps=60;low=60;cpuBusy=20;cpuValid=true;gpu=null;frameAt=null;packetPid=101;cpuFailure=false;tdpFailure=false;holdTdp=false;releaseTdp=null;acceptedCeiling=35;now+=20000;
 const bank=controls.defaultContinuousBank('ac');bank.tdp=watts;bank.auto.bias=bias;bank.auto.saving=saving;
 await runtime.enableContinuousFloat(bank,'ac',60,async()=>side==='ac',async()=>{}, {requested:120,accepted:watts});await runtime.__test.settled();initialParameterWrites=structuredClone(parameterWrites);parameterWrites=[];tdpWrites=[];return bank;
}
await check('OEM回执直接作为启动基准，不再发一次120W请求',async()=>{await start();const i=runtime.getFloatInfo();assert.equal(i.tdpRequestedMax,120);assert.equal(i.tdpMax,35);assert.equal(i.tdpApplied,35);assert.equal(tdpWrites.length,0)});
await check('全21档手动倾向直接决定CPU上限/积极性/睿频；不再全部不限频',()=>{
 const expected=[[0,0,100,true],[25,4000,78,true],[50,2900,55,true],[75,1900,33,false],[100,1200,10,false]];
 for(const [bias,maxMHz,aggressiveness,turbo] of expected){const b=controls.defaultContinuousBank('ac');b.auto.bias=bias;const c=controls.automaticCpuControls(b);assert.equal(c.maxMHz,maxMHz);assert.equal(c.aggressiveness,aggressiveness);assert.equal(c.turbo,turbo)}
 let prior=Infinity;for(let bias=0;bias<=100;bias+=5){const b=controls.defaultContinuousBank('ac');b.auto.bias=bias;const c=controls.automaticCpuControls(b);assert.ok((c.maxMHz||Infinity)<=prior);prior=c.maxMHz||Infinity}
});
await check('原掌机/台式AC与DC默认参数恢复，已有手动倾向不被默认覆盖',()=>{
 for(const [side,template,bias,watts] of [['ac','portable',25,55],['ac','desktop',0,120],['dc','portable',50,30],['dc','desktop',50,30]]){
  const b=controls.defaultContinuousBank(side,template);assert.equal(b.auto.bias,bias);assert.equal(b.tdp,watts);assert.equal(b.tdpCeiling,watts);
  assert.equal(controls.normalizeContinuousBank({...b,auto:{...b.auto,bias:90}},side).auto.bias,90);
 }
});
await check('无浮动/小中大激进按原节能档分别用0/3/5/8/12W试探，不改玩家CPU基线',()=>{
 for(const [saving,watts] of [['none',100],['small',97],['medium',95],['large',92],['aggressive',88]]){
  const b=controls.defaultContinuousBank('ac');b.tdp=100;b.tdpCeiling=100;b.auto.bias=0;b.auto.saving=saving;
  let s={watts:100,frequencyMHz:0,aggressiveness:100,stable:0,lastDown:0,recoverUntil:0};
  for(const now of [100000,101000])s=controls.stepContinuousFloat(b,s,{now,fps:60,low:60,target:60,minimum:2,cpuBusy:20,gpuBusy:null});
  assert.equal(s.watts,watts);assert.equal(s.frequencyMHz,0);assert.equal(s.aggressiveness,100);
 }
});
await check('CPU/GPU倾向只改变内部后续试探幅度100/200/300MHz，策略不暴露调度步长',()=>{
 for(const [bias,limit] of [[0,100],[20,100],[25,200],[50,200],[70,200],[75,300],[100,300]]){
  const b=controls.defaultContinuousBank('ac');b.auto.bias=bias;b.auto.saving='aggressive';const c=controls.automaticCpuControls(b);assert.equal('probeMHz' in c,false);
  const s={watts:b.tdp,frequencyMHz:c.maxMHz,aggressiveness:c.aggressiveness,stable:0,lastDown:0,recoverUntil:0,probeMHz:300,
   trial:{at:1000,axis:'cpu',watts:b.tdp,frequencyMHz:c.maxMHz,aggressiveness:c.aggressiveness,fps:60,low:60,needsGain:false}};
  const n=controls.stepContinuousFloat(b,s,{now:6000,fps:60,low:60,target:60,minimum:2,cpuBusy:20,gpuBusy:99});assert.equal(n.trial,null);assert.equal(n.probeMHz,limit);
 }
});
await check('无GPU/无帧率且无浮动，最右GPU优先仍立即下发CPU限制',async()=>{
 await start(100,35,'none');fps=0;low=0;cpuValid=false;gpu=null;
 assert.ok(initialParameterWrites.length>0);for(const p of initialParameterWrites){assert.equal(p.acFreq,1200);assert.equal(p.acAggr,10);assert.equal(p.acTurbo,false);assert.deepEqual(Array.from(p.sides),['ac'])}
 for(let n=0;n<12;n++)await step();assert.equal(parameterWrites.length+tdpWrites.length,0);
 fps=40;low=35;cpuValid=true;cpuBusy=92;gpu=99;for(let n=0;n<12;n++)await step();assert.equal(parameterWrites.length+tdpWrites.length,0);assert.equal(runtime.__test.state().frequencyMHz,1200);
});
await check('换手动倾向替代旧试探，旧trial不能覆盖新滑块边界',async()=>{
 await start();fps=40;gpu=99;await step();assert.ok(runtime.__test.state().trial);
 await start(100,35,'none');assert.equal(runtime.__test.state().trial,undefined);assert.equal(runtime.__test.state().frequencyMHz,1200);
 fps=40;low=35;cpuBusy=92;gpu=99;for(let n=0;n<12;n++)await step();assert.equal(parameterWrites.length+tdpWrites.length,0);assert.equal(runtime.__test.state().frequencyMHz,1200);
});
await check('全部倾向下低帧恢复不越过用户CPU上限及积极性',()=>{
 for(let bias=0;bias<=100;bias+=5){const b=controls.defaultContinuousBank('ac');b.auto.bias=bias;b.auto.saving='aggressive';const c=controls.automaticCpuControls(b);
 const st={watts:b.tdp,frequencyMHz:c.maxMHz?Math.max(800,c.maxMHz-100):4200,aggressiveness:Math.max(0,c.aggressiveness-5),stable:0,lastDown:0,recoverUntil:0};
 for(const gpuBusy of [null,99]){const n=controls.stepContinuousFloat(b,st,{now:100000,fps:40,low:35,target:60,minimum:2,cpuBusy:92,gpuBusy});if(c.maxMHz>0)assert.ok(n.frequencyMHz>0&&n.frequencyMHz<=c.maxMHz);assert.ok(n.aggressiveness<=c.aggressiveness)}
 }
});
await check('真实tick单项试降CPU100MHz，只写当前AC；五秒观察期不再试降',async()=>{await start();for(let n=0;n<5;n++)await step();assert.equal(runtime.__test.state().frequencyMHz,2800);assert.equal(tdpWrites.length,0);assert.equal(parameterWrites.length,1);assert.deepEqual(Array.from(parameterWrites[0].sides),['ac']);for(let n=0;n<4;n++)await step();assert.equal(parameterWrites.length,1);await step();assert.equal(runtime.__test.state().trial,null)});
await check('GPU有效参与主决策：固定预算低帧时试降CPU；缺GPU不冒认核显瓶颈',async()=>{await start();fps=40;gpu=99;await step();assert.equal(runtime.__test.state().frequencyMHz,2800);assert.equal(tdpWrites.length,0);await start();fps=40;gpu=null;await step();assert.equal(parameterWrites.length,0);assert.equal(tdpWrites.length,0)});
await check('试探恶化立即撤回，低帧无改善五秒后同样撤回',async()=>{await start();fps=40;gpu=99;await step();fps=35;await step();assert.equal(runtime.__test.state().frequencyMHz,2900);assert.equal(runtime.__test.state().trial,null);await start();fps=40;gpu=99;await step();for(let n=0;n<5;n++)await step();assert.equal(runtime.__test.state().frequencyMHz,2900);assert.equal(runtime.__test.state().trial,null)});
await check('无GPU仍可先恢复TDP，再在CPU压力证据下恢复CPU；不同时写两个轴',async()=>{await start();runtime.__test.force({watts:25,frequencyMHz:2600});fps=40;cpuBusy=92;await step();assert.equal(tdpWrites.at(-1).requested,35);assert.equal(parameterWrites.length,0);await step();assert.equal(parameterWrites.at(-1).acFreq,2900);assert.equal(tdpWrites.length,1)});
await check('CPU无效不是0%，不允许新试降；Low缺失/滞后同样不放行',async()=>{await start();cpuValid=false;for(let n=0;n<12;n++)await step();assert.equal(tdpWrites.length+parameterWrites.length,0);cpuValid=true;low=-1;for(let n=0;n<8;n++)await step();assert.equal(tdpWrites.length+parameterWrites.length,0);low=40;for(let n=0;n<8;n++)await step();assert.equal(tdpWrites.length+parameterWrites.length,0)});
await check('同一个FPS观察不能累计稳定计数，快包不续旧帧',async()=>{await start();frameAt=now;for(let n=0;n<12;n++)await step();assert.equal(tdpWrites.length+parameterWrites.length,0)});
await check('外来游戏帧不恢复旧bank；AC/DC边界拒绝旧owner，关闭不重放TDP',async()=>{await start();packetPid=202;for(let n=0;n<10;n++)await step();assert.equal(tdpWrites.length+parameterWrites.length,0);packetPid=101;side='dc';await step();await runtime.disableFloat();assert.equal(tdpWrites.length+parameterWrites.length,0)});
await check('CPU返回false不能成为成功；三秒退避，保持旧状态',async()=>{await start();cpuFailure=true;for(let n=0;n<5;n++)await step();assert.equal(parameterWrites.length,1);assert.equal(runtime.__test.state().frequencyMHz,2900);await step();await step();assert.equal(parameterWrites.length,1);cpuFailure=false;await step();assert.equal(runtime.__test.state().frequencyMHz,2800)});
await check('TDP失败不记成功，回执较小则更新恢复基线与节能下限',async()=>{await start(0);tdpFailure=true;for(let n=0;n<5;n++)await step();assert.equal(runtime.__test.state().watts,35);assert.equal(runtime.getFloatInfo().tdpApplied,0);assert.equal(tdpWrites.length,1);tdpFailure=false;acceptedCeiling=20;for(let n=0;n<3;n++)await step();assert.equal(runtime.__test.bank().tdp,20);assert.equal(runtime.getFloatInfo().tdpMax,20);assert.equal(runtime.getFloatInfo().tdpTarget,10)});
await check('激进档仍遵守OEM基线50%/硬件2W下限，单步观察避免2秒连写',async()=>{await start(0,4);for(let n=0;n<45;n++)await step();assert.equal(runtime.getFloatInfo().tdpApplied,2);assert.equal(runtime.getFloatInfo().tdpTarget,2);assert.ok(tdpWrites.every(w=>w.requested>=2&&w.requested<=4));const count=tdpWrites.length;for(let n=0;n<12;n++)await step();assert.equal(tdpWrites.length,count)});
await check('缺少本次TDP回执/传输抛错均未确认，三秒内不重试',async()=>{for(const failure of ['no-receipt','throw']){await start(0);tdpFailure=failure;for(let n=0;n<7;n++)await step();assert.equal(tdpWrites.length,1);assert.equal(runtime.getFloatInfo().tdpApplied,0);assert.equal(runtime.__test.state().watts,35);await step();assert.equal(tdpWrites.length,2)}});
await check('CPU/GPU都忙时只小步验证CPU，不无条件拉满；无收益回退',async()=>{await start();runtime.__test.force({frequencyMHz:2600});gpu=99;cpuBusy=92;fps=40;await step();assert.equal(runtime.__test.state().frequencyMHz,2700);assert.equal(tdpWrites.length,0);for(let n=0;n<5;n++)await step();assert.equal(runtime.__test.state().frequencyMHz,2600)});
await check('关闭等待已发出的TDP请求，迟到完成不再派生CPU或核心写入',async()=>{
 await start(0);for(let n=0;n<4;n++)await step();holdTdp=true;const inFlight=step();
 for(let n=0;n<100&&!releaseTdp;n++)await Promise.resolve();assert.equal(typeof releaseTdp,'function');
 let closed=false;const stopping=runtime.disableFloat().then(()=>{closed=true});for(let n=0;n<20;n++)await Promise.resolve();assert.equal(closed,false);
 const count=parameterWrites.length;holdTdp=false;releaseTdp();await inFlight;await stopping;assert.equal(closed,true);assert.equal(parameterWrites.length,count);assert.equal(runtime.getFloatInfo().enabled,false);
});
await check('核心切换滞回/确认/保持，边缘瓦数不频繁翻转',()=>{const b=controls.defaultContinuousBank('ac');b.auto.coreSplit=15;let s={mode:b.auto.coreLow,changedAt:0};for(let t=10000;t<20000;t+=1000)s=controls.stepContinuousCore(b,s,t%2000?14:16,t);assert.equal(s.mode,b.auto.coreLow);for(let t=20000;t<=23000;t+=1000)s=controls.stepContinuousCore(b,s,17,t);assert.equal(s.mode,b.auto.coreHigh);for(let t=24000;t<=32000;t+=1000)s=controls.stepContinuousCore(b,s,13,t);assert.equal(s.mode,b.auto.coreHigh);s=controls.stepContinuousCore(b,s,13,33000);assert.equal(s.mode,b.auto.coreLow)});
await runtime.disableFloat();
const record={pass:cases.every(c=>c.pass),count:cases.length,cases,scope:'Actual controller and resident tick; virtual clock and memory-only hardware; no现场 changes'};
console.log(JSON.stringify(record,null,2));if(!record.pass)process.exitCode=1;
