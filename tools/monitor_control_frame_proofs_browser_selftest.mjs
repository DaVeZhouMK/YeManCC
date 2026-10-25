// Extend the real internal-consumer/visibility browser fixture without changing
// its original cases. All native/control IO remains inert.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.env.YMCC_CONTROL_FRAME_PROOF_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/control-frame-proofs-20261017'),dir=out+'/browser';fs.mkdirSync(dir,{recursive:true});let source=fs.readFileSync('tools/monitor_topbar_demand_visibility_selftest.mjs','utf8');
source=source.replace("getPolicyGame(){return {pid:2077}}","getPolicyGame(){return {pid:2077,processCreated:'99'}}");
const probe=String.raw`
export async function __controlFrameTickProbe(){
 const bank=legacyContinuousBank('bal','small',30,'ac'),cpu=automaticCpuControls(bank);
 const state={watts:bank.tdp,frequencyMHz:cpu.maxMHz,aggressiveness:cpu.aggressiveness,stable:0,lastDown:0,recoverUntil:0};
 continuousOwner={bank,side:'ac',current:async()=>true,applyCore:async()=>{throw Error('Unexpected TDP actuation')},state,targetPid:2077};
 lastFloatActiveTs=Date.now();hwinfoPollTs=Date.now();curTarget=60;
 try{await tick();await Promise.resolve();await Promise.resolve();return {info:getFloatInfo(),state:{...continuousOwner.state}};}finally{continuousOwner=null;}
}
`;
source=source.replaceAll('__unifiedDataContinuousProbe}', '__unifiedDataContinuousProbe,__controlFrameTickProbe}');
const marker='const floatProbe = `';assert.equal(source.split(marker).length,2);source=source.replace(marker,marker+probe);
const hook='window.__native.cpuObservations.push(cpuBusy);';assert.equal(source.split(hook).length,2);source=source.replace(hook,hook+'window.__native.frameInputs.push({fps,low,pid,cpuBusy});');
source=source.replace('cpuObservations:[],data:false','cpuObservations:[],frameInputs:[],data:false');
source=source.replace('hwDown:false,topActive:this.flags.top','hwDown:this.hwDownOverride===true,topActive:this.flags.top');
const raw="JSON.stringify({ts:now,fps:117,fps1:85,gpu:80,packagePower:30,pid:2077,game:'fixture'})";assert.equal(source.split(raw).length,2);source=source.replace(raw,"JSON.stringify(this.fpsOverride?this.fpsOverride(now):{ts:now,fps:117,fps1:85,gpu:80,packagePower:30,pid:2077,game:'fixture'})");
const insert=String.raw`
    if(side==='after'){
      const controlCases=[];
      for(const mode of ['expired-low','expired-main','zero-low','recovery','rtss-missing-low','rtss-down']){
        const observed=await run(
          'const mode='+JSON.stringify(mode)+';'+
          "__native.hwDownOverride=mode==='rtss-down';__native.frameInputs=[];__native.cpuObservations=[];"+
          "__native.fpsOverride=now=>{const rtss=mode.startsWith('rtss'),fpsAt=now-(mode==='expired-main'?6000:1000),lowAt=now-(mode==='expired-low'||mode==='rtss-missing-low'?6000:1000);return {ts:now,fps:117,fps1:mode==='zero-low'?0:85,gpu:80,packagePower:30,pid:2077,processCreated:'99',game:'fixture',fpsSource:rtss?'rtss':'hwinfo-presented',fpsObservedAt:fpsAt,fpsValidUntil:fpsAt+5000,fpsPollPeriodMs:rtss?0:1000,fps1Source:'hwinfo-presented-1percent-low',fps1ObservedAt:lowAt,fps1ValidUntil:lowAt+5000,fps1PollPeriodMs:1000};};"+
          "const reads=__native.calls.filter(c=>c.name==='monitor.snapshot').length;const result=await __app.__controlFrameTickProbe();const value={mode,result,inputs:__native.frameInputs,io:__native.blockedIo,reads:__native.calls.filter(c=>c.name==='monitor.snapshot').length-reads};__native.fpsOverride=null;__native.hwDownOverride=false;return value;"
        );
        assert.equal(observed.reads,1,'status/control proof check adds no extra snapshot read');
        assert.deepStrictEqual(observed.io,[],'proof validation must not introduce a control/file operation');
        assert.equal(observed.inputs.length,1);
        const expectedLow=['expired-low','rtss-missing-low','rtss-down'].includes(mode)?-1:mode==='zero-low'||mode==='expired-main'?0:85;
        assert.equal(observed.inputs[0].low,expectedLow);
        assert.equal(observed.inputs[0].fps,mode==='expired-main'?0:117);
        assert.equal(observed.inputs[0].pid,mode==='expired-main'?0:2077);
        assert.equal(observed.result.state.watts,30);
        controlCases.push(observed);cases.push('current full control tick proof: '+mode);
      }
      traces[side]['control-frame-proofs']=controlCases;
    }
`;
const anchor="    traces[side]['current-continuous-consumer']=continuous;";assert.equal(source.split(anchor).length,2);source=source.replace(anchor,anchor+insert);const file=out+'/browser-current.mjs';fs.writeFileSync(file,source);
const run=spawnSync(process.execPath,[file],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6,env:{...process.env,YMCC_TOPBAR_VISIBILITY_OUT:dir}});fs.writeFileSync(out+'/BROWSER-RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,(run.error?.message||'')+run.stdout+run.stderr);const result=JSON.parse(fs.readFileSync(dir+'/RESULT.json','utf8'));assert(result.ok);assert.equal(result.traces.after['control-frame-proofs'].length,6);assert.deepEqual(result.pageErrors,[]);console.log(run.stdout.trim());
