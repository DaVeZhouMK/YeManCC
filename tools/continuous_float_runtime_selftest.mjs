// Real float module + controller, with an in-memory hardware boundary and a virtual clock.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const require=createRequire(path.resolve('package.json'));
const {transformSync}=require('esbuild');
let now=100000,side='ac',game={pid:101,processCreated:'birth',name:'fixture.exe'},fps=60,low=60,cpuBusy=20,cpuValid=true,gpu=null,frameAt=null,packetPid=101,cpuFailure=false,tdpFailure=false;
let fileWrites=[],fileRemoves=[],markerCost=null;
let savedTdp=120,acceptedDelta=0,holdTdpDispatch=false,releaseTdpDispatch=null,rejectTdp=null,deferVendorOnce=false,releaseVendor=null;
let packagePower=0,powerValid=true,hardwareAt=null,monitorDown=false,ownerValid=true;
let sideProbes=0;let acceptedCeiling=35,parameterWrites=[],initialParameterWrites=[],tdpWrites=[],holdTdp=false,releaseTdp=null;
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
const yeman={PW:{YEMAN:'fixture'},readTdp:async()=>{if(savedTdp==='throw')throw Error('settings unavailable');return savedTdp},detectPowerMode:async()=>side,detectPowerModeReliable:async()=>{sideProbes++;return side},
 getActiveScheme:async()=> 'fixture',setActiveScheme:async()=>{},detectVendor:async()=>{if(deferVendorOnce){deferVendorOnce=false;return new Promise(resolve=>{releaseVendor=resolve})}return 'amd'},getTdpHardwareMinimum:()=>2,getTdpHardwareMaximum:()=>150,
 getTdpExecutionCapability:async()=>capability,resolveTdpTarget:w=>({intentW:w,effectiveW:Math.max(2,Math.min(150,w))}),clampTdp:w=>w,
 readPowerParams:async()=>({}),readRtssLimit:async()=>60,setRtssLimit:async()=>{},ensureTdpDaemon:async()=>{},
 applyFloatPowerParams:async value=>{parameterWrites.push(structuredClone(value));return !cpuFailure},
 setTdp:async(s,w,opts)=>{if(holdTdpDispatch)await new Promise(resolve=>{releaseTdpDispatch=resolve});if(opts.isCurrent&&!opts.isCurrent())return false;tdpWrites.push({side:s,requested:w,at:now});if(holdTdp)await new Promise((resolve,reject)=>{releaseTdp=resolve;rejectTdp=reject});if(tdpFailure==='throw')throw Error('transport rejected');if(tdpFailure===true)return false;if(tdpFailure==='no-receipt')return true;opts.onAccepted?.(Math.min(w,acceptedCeiling)-acceptedDelta);return true}};
const runtime=evaluate('src/bridge/autofloat.ts',{
 './gamePolicyTarget':{getPolicyGame:()=>game},'./performanceControls':controls,'./monitorFpsHardware':fpsHardware,'./monitorFrameTime':frameTime,'./monitorSnapshotSignals':snapshotSignals,'./monitorControlFramerate':evaluate('src/bridge/monitorControlFramerate.ts'),
 './api':{fs:{writeTextFile:async(file)=>{fileWrites.push(file)},remove:async(file)=>{fileRemoves.push(file)}}},'./ipc':{invoke:async()=>true},'./yeman':yeman,
 './settingsRepository':{getSettingsGeneration:()=>1,assertSettingsGeneration(){},readSettingsSection:async()=>({}),saveSettingsSection:async()=>{}},
 './monitorData':{acquireMonitorDemand:()=>()=>{},monitorDemandConfirmed:()=>({fps:true}),monitorSnapshotEpoch:()=>1,setMonitorDataPowerControlDir(){},
 readMonitorSnapshot:async()=>({collectedAt:now,heartbeatAt:now,hwinfoOkAt:now,hwDown:false,top:{tdpW:packagePower,cpuPowerValid:powerValid,hwDown:monitorDown,displayedFramerate:{source:'hwinfo-displayed',fps,fps1:low,pid:packetPid,processCreated:'birth',game:'fixture.exe',observedAt:frameAt??now,validUntil:(frameAt??now)+5000,pollPeriodMs:1000},cpuUsageValid:cpuValid,cpuUsageTs:now,ts:hardwareAt??now,cpuUsage:cpuBusy,freqMhz:5000,cpuClockValid:true,gpuSelectionPresent:gpu!==null,gpuName:'Selected iGPU',gpuUsagePct:gpu},
 fps:{ts:frameAt??now,fps,fps1:low,gpu:95,game:'fixture.exe',pid:packetPid,processCreated:'birth',fpsSource:'hwinfo-presented',fpsObservedAt:frameAt??now,fpsValidUntil:now+5000,fpsPollPeriodMs:1000,fps1Source:'hwinfo-presented-1percent-low',fps1ObservedAt:frameAt??now,fps1ValidUntil:now+5000,fps1PollPeriodMs:1000,hardwareObservedAt:now,hardwareValidUntil:now+5000,hardwarePollPeriodMs:1000}})},
},'export const __test={tick,applyTdpLimit,corruptBank:value=>{if(continuousOwner)continuousOwner.bank.tdp=value},settled:async()=>{await peripheralInit},bank:()=>continuousOwner?.bank,state:()=>continuousOwner?.state,vendor:()=>tdpVendor,force:state=>{if(continuousOwner){Object.assign(continuousOwner.state,state);if(state.watts!==undefined)tdpLimit=state.watts}}};');
const cases=[];
async function check(name,run){try{await run();cases.push({name,pass:true})}catch(error){cases.push({name,pass:false,error:error.stack})}}
const step=async()=>{now+=1000;await runtime.__test.tick()};
async function start(bias=50,watts=35,saving='aggressive',initial=true,saved=120){
 await runtime.disableFloat();fileWrites=[];fileRemoves=[];parameterWrites=[];tdpWrites=[];side='ac';packagePower=0;powerValid=true;hardwareAt=null;monitorDown=false;ownerValid=true;game={pid:101,processCreated:'birth',name:'fixture.exe'};fps=60;low=60;cpuBusy=20;cpuValid=true;gpu=null;frameAt=null;packetPid=101;cpuFailure=false;tdpFailure=false;holdTdp=false;releaseTdp=null;rejectTdp=null;acceptedCeiling=35;savedTdp=saved;acceptedDelta=0;holdTdpDispatch=false;releaseTdpDispatch=null;now+=20000;
 const bank=controls.defaultContinuousBank('ac');bank.tdp=watts;bank.auto.bias=bias;bank.auto.saving=saving;
 await runtime.enableContinuousFloat(bank,'ac',60,Object.assign(async()=>side==='ac'&&ownerValid,{isCurrent:()=>side==='ac'&&ownerValid}),async()=>{}, initial?{requested:120,accepted:watts}:undefined);await runtime.__test.settled();initialParameterWrites=structuredClone(parameterWrites);parameterWrites=[];tdpWrites=[];return bank;
}
await check('OEM回执直接作为启动基准，不再发一次120W请求',async()=>{await start();const i=runtime.getFloatInfo();assert.equal(i.tdpRequestedMax,120);assert.equal(i.tdpMax,35);assert.equal(i.tdpApplied,35);assert.equal(tdpWrites.length,0)});
await check('浮动CPU参数实际变化也不另发电源探测',async()=>{await start();sideProbes=0;for(let n=0;n<12;n++)await step();assert.ok(parameterWrites.length>0);assert.equal(sideProbes,0)});
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
await check('TDP失败不记成功，后续较小回执只更新当前值，不重定义已确认基线',async()=>{await start(0);tdpFailure=true;for(let n=0;n<5;n++)await step();assert.equal(runtime.__test.state().watts,35);assert.equal(runtime.getFloatInfo().tdpApplied,0);assert.equal(tdpWrites.length,1);tdpFailure=false;acceptedCeiling=20;for(let n=0;n<3;n++)await step();assert.equal(runtime.getFloatInfo().tdpApplied,20);assert.equal(runtime.__test.bank().tdp,35);assert.equal(runtime.getFloatInfo().tdpMax,35);assert.equal(runtime.getFloatInfo().tdpTarget,17)});
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
await check('ACDC事件撤销旧浮动，循环不检测电源，也不因同侧重复事件重建',async()=>{
 await start();sideProbes=0;runtime.invalidateFloatPowerSource('ac');for(let n=0;n<7;n++)await step();assert.equal(sideProbes,0);
 runtime.invalidateFloatPowerSource('dc');const cpu=parameterWrites.length,tdp=tdpWrites.length;
 for(let n=0;n<20;n++)await step();assert.equal(parameterWrites.length,cpu);assert.equal(tdpWrites.length,tdp);assert.equal(sideProbes,0);
 runtime.invalidateFloatPowerSource('ac');await step();assert.equal(parameterWrites.length,cpu);assert.equal(tdpWrites.length,tdp);
});
await check('电源事件发生在TDP迟到响应期间，旧结果不能派生CPU或核心动作',async()=>{
 await start(0);for(let n=0;n<4;n++)await step();holdTdp=true;const pending=step();
 for(let n=0;n<100&&!releaseTdp;n++)await Promise.resolve();assert.equal(typeof releaseTdp,'function');
 runtime.invalidateFloatPowerSource('dc');const count=parameterWrites.length;holdTdp=false;releaseTdp();await pending;
 for(let n=0;n<10;n++)await step();assert.equal(parameterWrites.length,count);
});
await check('TDP编辑暂停保留CPU状态、等待当前tick，不重启浮动且暂停时不采样写入',async()=>{
 await start();for(let n=0;n<5;n++)await step();const frequency=runtime.__test.state().frequencyMHz;
 const edit=await runtime.pauseContinuousFloatForTdpEdit();assert.ok(edit);const cpu=parameterWrites.length,tdp=tdpWrites.length;
 for(let n=0;n<15;n++)await step();assert.equal(parameterWrites.length,cpu);assert.equal(tdpWrites.length,tdp);
 edit.accept(40,30);assert.equal(runtime.getFloatInfo().tdpRequestedMax,40);assert.equal(runtime.getFloatInfo().tdpApplied,30);assert.equal(runtime.getFloatInfo().tdpMax,30);assert.equal(runtime.__test.state().frequencyMHz,frequency);assert.equal(runtime.__test.state().trial,null);assert.equal(runtime.getFloatInfo().enabled,true);edit.release();await step();assert.equal(runtime.__test.state().frequencyMHz,frequency);
});
await check('TDP编辑先等旧TDP完成再返回暂停权，不把迟到结果压到新目标之后',async()=>{
 await start(0);for(let n=0;n<4;n++)await step();holdTdp=true;const tick=step();for(let n=0;n<100&&!releaseTdp;n++)await Promise.resolve();assert.equal(typeof releaseTdp,'function');
 let acquired=false;const preparing=runtime.pauseContinuousFloatForTdpEdit().then(value=>{acquired=true;return value});for(let n=0;n<20;n++)await Promise.resolve();assert.equal(acquired,false);
 holdTdp=false;releaseTdp();await tick;const edit=await preparing;assert.ok(edit);edit.accept(40,35);assert.equal(runtime.getFloatInfo().tdpApplied,35);assert.equal(runtime.getFloatInfo().tdpRequestedMax,40);edit.release();
});
await check('TDP编辑保存失败仅释放暂停，原状态与调度资源完整保留',async()=>{
 await start();const prior=JSON.stringify(runtime.__test.state()),info=runtime.getFloatInfo(),edit=await runtime.pauseContinuousFloatForTdpEdit();assert.ok(edit);edit.release();
 assert.equal(JSON.stringify(runtime.__test.state()),prior);assert.equal(runtime.getFloatInfo().tdpApplied,info.tdpApplied);assert.equal(runtime.getFloatInfo().enabled,true);await step();
});
await check('TDP编辑暂停中电源事件撤销，旧编辑不能确认新值或恢复旧循环',async()=>{
 await start();const edit=await runtime.pauseContinuousFloatForTdpEdit();assert.ok(edit);const previous=runtime.getFloatInfo().tdpApplied,cpu=parameterWrites.length,tdp=tdpWrites.length;
 runtime.invalidateFloatPowerSource('dc');assert.equal(edit.current(),false);edit.accept(50,40);edit.release();assert.equal(runtime.getFloatInfo().tdpApplied,previous);for(let n=0;n<10;n++)await step();assert.equal(parameterWrites.length,cpu);assert.equal(tdpWrites.length,tdp);
});
await check('TDP编辑暂停中关闭后，旧接受与释放均不能重开控制或写入',async()=>{
 await start();const edit=await runtime.pauseContinuousFloatForTdpEdit();assert.ok(edit);await runtime.disableFloat();assert.equal(edit.current(),false);edit.accept(50,40);edit.release();assert.equal(runtime.getFloatInfo().enabled,false);
});
await check('TDP编辑未确认回执保持未知0，目标保留且不在原三秒退避内重放',async()=>{
 await start();const edit=await runtime.pauseContinuousFloatForTdpEdit();assert.ok(edit);edit.accept(40,null);edit.release();assert.equal(runtime.getFloatInfo().tdpApplied,0);assert.equal(runtime.getFloatInfo().tdpRequestedMax,40);const count=tdpWrites.length;await step();await step();assert.equal(tdpWrites.length,count);
});
await check('无启动回执时不拿配置读取失败/零/旧低值覆盖35W，首个请求仍为35W',async()=>{
 for(const saved of [null,0,4,'throw']){
  await start(0,35,'aggressive',false,saved);fps=0;low=0;
  assert.equal(runtime.__test.bank().tdp,35,'The committed owner target survives missing or unrelated storage');
  assert.equal(runtime.getFloatInfo().tdpApplied,0,'No receipt is not an applied saved value');
  await step();assert.equal(tdpWrites[0]?.requested,35);assert.equal(runtime.__test.bank().tdp,35);
 }
});
await check('临时试降低回执不缩恢复基线；越过原节能下限则撤回，不递降至2W',async()=>{
 await start(0);for(let n=0;n<4;n++)await step();acceptedCeiling=8;await step();
 assert.equal(tdpWrites[0]?.requested,30);assert.equal(runtime.getFloatInfo().tdpApplied,8);
 assert.equal(runtime.__test.bank().tdp,35);assert.equal(runtime.getFloatInfo().tdpMax,35);assert.equal(runtime.getFloatInfo().tdpTarget,17);
 acceptedCeiling=35;fps=0;low=0;await step();assert.equal(tdpWrites.at(-1).requested,35);assert.equal(runtime.__test.state().trial,null);
 assert.ok(tdpWrites.every(x=>x.requested>=17));
});
await check('较小回执连续出现也不递归改节能参照：35W激进档请求不低于17W',async()=>{
 await start(0);acceptedDelta=1;for(let n=0;n<120;n++)await step();
 assert.equal(runtime.__test.bank().tdp,35);assert.equal(runtime.getFloatInfo().tdpTarget,17);
 assert.deepEqual(tdpWrites.map(x=>x.requested),[30,24,18]);assert.equal(runtime.getFloatInfo().tdpApplied,17);assert.equal(runtime.getFloatInfo().enabled,true);assert.ok(tdpWrites.every(x=>Number.isInteger(x.requested)&&x.requested>=17&&x.requested<=35));
});
await check('无启动回执首次真实全额写入的OEM接受值仍可建立35→20W基线',async()=>{
 await start(0,35,'aggressive',false,null);acceptedCeiling=20;await step();
 assert.equal(tdpWrites[0]?.requested,35);assert.equal(runtime.__test.bank().tdp,20);assert.equal(runtime.getFloatInfo().tdpMax,20);assert.equal(runtime.getFloatInfo().tdpTarget,10);
});
await check('TDP试降后FPS失联只撤回该试探，已有10秒观察超时不永久停留低瓦',async()=>{
 await start(0);for(let n=0;n<5;n++)await step();assert.equal(runtime.__test.state().watts,30);
 fps=0;low=0;for(let n=0;n<10;n++)await step();
 assert.equal(runtime.__test.state().watts,35);assert.equal(runtime.__test.state().trial,null);assert.equal(tdpWrites.at(-1).requested,35);
});
await check('浮动低瓦请求排队时ACDC撤销，端口前复用内存isCurrent不再派发',async()=>{
 await start(0);for(let n=0;n<4;n++)await step();holdTdpDispatch=true;const pending=step();
 for(let n=0;n<100&&!releaseTdpDispatch;n++)await Promise.resolve();assert.equal(typeof releaseTdpDispatch,'function');
 runtime.invalidateFloatPowerSource('dc');holdTdpDispatch=false;releaseTdpDispatch();await pending;assert.equal(tdpWrites.length,0);
});
await check('重复旧FPS也按原10秒超时撤回TDP试降，不停在低瓦',async()=>{
 await start(0);for(let n=0;n<5;n++)await step();assert.equal(runtime.__test.state().watts,30);
 frameAt=now;for(let n=0;n<10;n++)await step();assert.equal(runtime.__test.state().watts,35);assert.equal(runtime.__test.state().trial,null);assert.equal(tdpWrites.at(-1).requested,35);
});
await check('旧run的TDP异常回执不能清零新owner的35W确认值',async()=>{
 await start(0);for(let n=0;n<4;n++)await step();holdTdp=true;const pending=step();for(let n=0;n<30&&!rejectTdp;n++)await Promise.resolve();assert.equal(typeof rejectTdp,'function');const rejectOld=rejectTdp;
 runtime.invalidateFloatPowerSource('dc');assert.equal(runtime.getFloatInfo().tdpApplied,35);rejectOld(Error('old request rejected'));await pending;
 assert.equal(runtime.getFloatInfo().tdpApplied,35);await start(0);assert.equal(runtime.getFloatInfo().tdpApplied,35);assert.equal(runtime.__test.bank().tdp,35);assert.equal(runtime.getFloatInfo().enabled,true);
});
await check('旧初始化vendor迟到不得覆盖新owner的厂商和基线',async()=>{
 await runtime.disableFloat();deferVendorOnce=true;releaseVendor=null;const bank=controls.defaultContinuousBank('ac');bank.tdp=35;
 await runtime.enableContinuousFloat(bank,'ac',60,async()=>true,async()=>{},{requested:35,accepted:35});for(let n=0;n<30&&!releaseVendor;n++)await Promise.resolve();assert.equal(typeof releaseVendor,'function');const releaseOld=releaseVendor,oldSettled=runtime.__test.settled();
 await start(0,22);assert.equal(runtime.__test.vendor(),'amd');releaseOld('intel');await oldSettled;
 assert.equal(runtime.__test.vendor(),'amd');assert.equal(runtime.getFloatInfo().tdpApplied,22);assert.equal(runtime.__test.bank().tdp,22);
});
await check('15W回执后实测持续30W且无Displayed帧率，复用快照纠正到15W',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=30;
 for(let n=0;n<14;n++)await step();assert.equal(tdpWrites.length,1);assert.equal(tdpWrites[0].requested,15);
 assert.equal(runtime.__test.bank().tdp,15);assert.equal(runtime.getFloatInfo().tdpMax,15);
});
await check('功耗正常三分钟零额外TDP写入，仍无每tick电源检测',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=15;sideProbes=0;
 for(let n=0;n<180;n++)await step();assert.equal(tdpWrites.length,0);assert.equal(sideProbes,0);
});
await check('短峰值、恢复低功耗和重复旧硬件样本不触发异常纠正',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=30;for(let n=0;n<4;n++)await step();
 packagePower=15;await step();assert.equal(tdpWrites.length,0);
 packagePower=30;hardwareAt=now;for(let n=0;n<20;n++)await step();assert.equal(tdpWrites.length,0);
});
await check('无功耗数据直接轮空，保留已有TDP不制造0和盲目补写',async()=>{
 for(const invalid of ['missing','down']){
  await start(0,15,'medium');fps=0;low=0;packagePower=30;powerValid=invalid!=='missing';monitorDown=invalid==='down';
  for(let n=0;n<59;n++)await step();assert.equal(tdpWrites.length,0);
  for(let n=0;n<90;n++)await step();assert.equal(tdpWrites.length,0);assert.equal(runtime.__test.bank().tdp,15);
 }
});
await check('持续超额最多每30秒一次纠正，实测恢复后立即停止额外写入',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=30;
 for(let n=0;n<75;n++)await step();assert.ok(tdpWrites.length>=2&&tdpWrites.length<=3);
 for(let n=1;n<tdpWrites.length;n++)assert.ok(tdpWrites[n].at-tdpWrites[n-1].at>=30000);
 assert.ok(tdpWrites.every(x=>x.requested===15));const count=tdpWrites.length;
 packagePower=15;for(let n=0;n<90;n++)await step();assert.equal(tdpWrites.length,count);
});
await check('纠正失败保留未知与30秒退避，后续有效请求可恢复',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=30;tdpFailure=true;
 for(let n=0;n<14;n++)await step();assert.equal(tdpWrites.length,1);assert.equal(runtime.getFloatInfo().tdpApplied,0);
 const failedAt=tdpWrites[0].at;tdpFailure=false;while(now<failedAt+29000)await step();assert.equal(tdpWrites.length,1);
 for(let n=0;n<3;n++)await step();assert.equal(tdpWrites.length,2);assert.equal(runtime.getFloatInfo().tdpApplied,15);
});
await check('没有当前游戏或已切供电，不做旧目标的异常纠正及保底重申',async()=>{
 for(const boundary of ['game','power']){
  await start(0,15,'medium');fps=0;low=0;packagePower=30;if(boundary==='game')game=null;else runtime.invalidateFloatPowerSource('dc');
  for(let n=0;n<90;n++)await step();assert.equal(tdpWrites.length,0);
 }
});
await check('手动改成10W后只纠正新目标，不恢复旧15W基线',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=30;for(let n=0;n<4;n++)await step();
 const edit=await runtime.pauseContinuousFloatForTdpEdit();assert.ok(edit);edit.accept(10,10);edit.release();
 for(let n=0;n<14;n++)await step();assert.equal(tdpWrites.length,1);assert.equal(tdpWrites[0].requested,10);assert.equal(runtime.__test.bank().tdp,10);
});
await check('异常纠正排队期间撤销owner，仍由端口前isCurrent取消旧请求',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=30;for(let n=0;n<6;n++)await step();
 holdTdpDispatch=true;const pending=step();for(let n=0;n<100&&!releaseTdpDispatch;n++)await Promise.resolve();
 assert.equal(typeof releaseTdpDispatch,'function');runtime.invalidateFloatPowerSource('dc');holdTdpDispatch=false;releaseTdpDispatch();await pending;
 assert.equal(tdpWrites.length,0);
});
await check('同PID会话失效与暂停编辑时，不能重申旧TDP',async()=>{
 await start(0,15,'medium');fps=0;low=0;packagePower=30;ownerValid=false;
 for(let n=0;n<90;n++)await step();assert.equal(tdpWrites.length,0);
 await start(0,15,'medium');fps=0;low=0;packagePower=30;const edit=await runtime.pauseContinuousFloatForTdpEdit();assert.ok(edit);
 for(let n=0;n<90;n++)await step();assert.equal(tdpWrites.length,0);edit.release();
});
await check('ROG日志功耗序列回放：15W目标遇持续高功耗会纠正，指令不升为30W',async()=>{
 // 2026-11-04 ROG export monitor-detail.log lines141..189, every third row.
 const observed=[15,15,22.7,22.7,17.4,21.2,21.8,22.7,22.2,21.3,24.5,25.5,25.5,26,26,24.2,21.9];
 await start(0,15,'medium');fps=0;low=0;
 for(const watts of observed){packagePower=watts;for(let n=0;n<3;n++)await step();}
 assert.ok(tdpWrites.length>=1);assert.ok(tdpWrites.every(x=>x.requested===15));assert.equal(runtime.__test.bank().tdp,15);
});
await check('空值、非有限值、上下越界目标均轮空，保持15W并暂停直到有效样本恢复',async()=>{
 for(const value of [null,undefined,'',NaN,Infinity,-1,0,2,9,16,30]){
  await start(0,15,'medium');fps=0;low=0;packagePower=15;
  await runtime.__test.applyTdpLimit(value);assert.equal(tdpWrites.length,0);assert.equal(runtime.getFloatInfo().tdpApplied,15);
  assert.equal(runtime.__test.bank().tdp,15);assert.ok(runtime.getFloatInfo().tdpSafetyReason);
  for(let n=0;n<5;n++)await step();assert.ok(runtime.getFloatInfo().tdpSafetyReason);assert.equal(tdpWrites.length,0);
  fps=60;low=60;await step();assert.equal(runtime.getFloatInfo().tdpSafetyReason,undefined);assert.equal(runtime.__test.bank().tdp,15);
 }
});
await check('错误bank不会成为新TDP或最低瓦数；合法请求始终在当前节能区间',async()=>{
 await start(0,15,'medium');runtime.__test.corruptBank(null);await runtime.__test.applyTdpLimit(15);
 assert.equal(tdpWrites.length,0);assert.equal(runtime.__test.bank().tdp,15);assert.ok(runtime.getFloatInfo().tdpSafetyReason);
 fps=60;low=60;await step();await runtime.__test.applyTdpLimit(10);assert.equal(tdpWrites.at(-1).requested,10);
 assert.ok(tdpWrites.every(x=>Number.isFinite(x.requested)&&x.requested>=10&&x.requested<=15));
});
await check('无效启动目标不拆掉当前有效15W，继续暂停等有效数值',async()=>{
 await start(0,15,'medium');const bad=controls.defaultContinuousBank('ac');bad.tdp=NaN;
 await runtime.enableContinuousFloat(bad,'ac',60,async()=>true,async()=>{});
 assert.equal(runtime.getFloatInfo().enabled,true);assert.equal(runtime.__test.bank().tdp,15);assert.equal(tdpWrites.length,0);assert.ok(runtime.getFloatInfo().tdpSafetyReason);
});
await check('兼容入口读取空值或错误TDP也不创建零瓦基线、不替换有效owner',async()=>{
 for(const value of [null,undefined,'',NaN,Infinity,0,-1,201,'throw']){
  await start(0,15,'medium');savedTdp=value;await runtime.enableFloat(60,'bal','medium');
  assert.equal(runtime.__test.bank().tdp,15);assert.equal(runtime.getFloatInfo().tdpApplied,15);assert.ok(runtime.getFloatInfo().tdpSafetyReason);assert.equal(tdpWrites.length,0);
 }
 await runtime.disableFloat();savedTdp=null;const cpu=parameterWrites.length;await runtime.enableFloat(60,'bal','medium');
 assert.equal(runtime.getFloatInfo().enabled,false);assert.equal(parameterWrites.length,cpu);assert.equal(tdpWrites.length,0);
});
await check('用户把35W改15W时，排队中的30W在实际发送前受新缓存上限约束',async()=>{
 await start(0,35,'aggressive');for(let n=0;n<4;n++)await step();holdTdpDispatch=true;const pending=step();
 for(let n=0;n<100&&!releaseTdpDispatch;n++)await Promise.resolve();assert.equal(typeof releaseTdpDispatch,'function');
 const editing=runtime.pauseContinuousFloatForTdpEdit(15);holdTdpDispatch=false;releaseTdpDispatch();await pending;
 const edit=await editing;assert.ok(edit);assert.equal(tdpWrites.length,0);edit.accept(15,15);edit.release();
 assert.equal(runtime.__test.bank().tdp,15);assert.equal(runtime.getFloatInfo().tdpApplied,15);
});
for(const boundary of ['game-pid','game-birth','owner-revoked'])await check('排队后'+boundary+'变化，旧浮动TDP不得进入发送口',async()=>{
 await start(0,35,'aggressive');for(let n=0;n<4;n++)await step();holdTdpDispatch=true;const pending=step();
 for(let n=0;n<100&&!releaseTdpDispatch;n++)await Promise.resolve();assert.equal(typeof releaseTdpDispatch,'function');
 if(boundary==='game-pid')game={...game,pid:202};else if(boundary==='game-birth')game={...game,processCreated:'new-birth'};else ownerValid=false;
 holdTdpDispatch=false;releaseTdpDispatch();await pending;assert.equal(tdpWrites.length,0);
});
await check('原生无标志消费者，60次真实浮动tick没有float-active文件IPC',async()=>{
 await start();for(let n=0;n<60;n++)await step();await runtime.disableFloat();
 markerCost={ticks:60,writes:fileWrites.filter(p=>p.endsWith('float-active')).length,removes:fileRemoves.filter(p=>p.endsWith('float-active')).length};
 assert.deepEqual(markerCost,{ticks:60,writes:0,removes:0});
});
await runtime.disableFloat();
const record={pass:cases.every(c=>c.pass),count:cases.length,cases,markerCost,scope:'Actual controller and resident tick; virtual clock and memory-only hardware; no现场 changes'};
console.log(JSON.stringify(record,null,2));if(!record.pass)process.exitCode=1;
