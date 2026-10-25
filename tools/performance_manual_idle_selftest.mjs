import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import {createPerformanceMemoryFixture,models,frames} from './fixtures/performance_memory_fixture.mjs';
const require=createRequire(path.resolve('package.json'));
const ts=require('typescript');
const clone=x=>structuredClone(x);
const arg=name=>{const i=process.argv.indexOf(name);return i<0?null:process.argv[i+1]};
const baseline=arg('--baseline');
const source=file=>fs.readFileSync(baseline?path.join(baseline,path.basename(file)):file,'utf8');
const fixture=options=>createPerformanceMemoryFixture({...options,source});
const results=[];
async function check(name,run){try{await run();results.push({name,pass:true})}catch(error){results.push({name,pass:false,error:error.message})}}
for (const dedicated of [false,true]) {
 await check((dedicated?'专属':'全局')+' 全局OFF保留预设，阻断AC/DC/换游戏与恢复',async()=>{
  const f=fixture({dedicated,mode:'auto'}),before=JSON.stringify(f.settings.performanceSchedule.controls);
  await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});const oldLease=f.ownerLease;
  await f.schedule.stopPerformanceScheduleAndUseWindowsBalanced();
  assert.equal(f.floating,false);assert.equal(f.settings.performanceSchedule.suspended,true);
  assert.equal(JSON.stringify(f.settings.performanceSchedule.controls),before);assert.equal(await oldLease(),false);
  f.effects.length=0;f.hardwareWrites.length=0;
  for(const side of ['ac','dc','ac']){f.sourceMode.value=side;assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true}),'none');assert.equal(await f.schedule.getPerformanceScheduleOwnership(),'none');assert.equal(await f.schedule.refreshPerformanceScheduleCoreMode(),false);}
  assert.deepEqual(f.hardwareWrites,[]);assert.deepEqual(f.effects,[]);
  const current=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(current.identity,{mode:'auto'});
  assert.equal(f.settings.performanceSchedule.suspended,false);assert.ok(f.effects.includes('start-float'));
 });
 await check((dedicated?'专属':'全局')+' 核心分界提交只改核心规则，不重放TDP/CPU/浮动',async()=>{
  const f=fixture({dedicated,mode:'auto'});await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});
  f.hardwareWrites.length=0;f.effects.length=0;const snap=await f.schedule.getContinuousPerformanceSnapshot();
  await f.schedule.updateContinuousPerformanceControls(snap.identity,{auto:{coreSplit:60,coreHigh:'littleFirst',coreLow:'bigOnly'}});
  assert.ok(f.effects.includes('update-core-rules'));assert.ok(!f.effects.includes('stop-float'));assert.ok(!f.effects.includes('start-float'));
  assert.ok(f.hardwareWrites.every(w=>['core','active-scheme'].includes(w.kind)));assert.ok(f.hardwareWrites.some(w=>w.kind==='core'));
 });
}
await check('平衡切换失败仍保持OFF，重试不重新启用；暂停时核心编辑只保存',async()=>{
 const f=fixture({mode:'auto'});f.setSwitchFailure(true);await assert.rejects(f.schedule.stopPerformanceScheduleAndUseWindowsBalanced(),/overlay rejected/);
 assert.equal(f.settings.performanceSchedule.suspended,true);assert.equal(await f.schedule.getPerformanceScheduleOwnership(),'none');
 const s=await f.schedule.getContinuousPerformanceSnapshot();f.hardwareWrites.length=0;await f.schedule.updateContinuousPerformanceControls(s.identity,{auto:{coreSplit:11}});assert.deepEqual(f.hardwareWrites,[]);
 f.setSwitchFailure(false);await f.schedule.stopPerformanceScheduleAndUseWindowsBalanced();assert.equal(f.settings.performanceSchedule.suspended,true);
});
await check('OFF立即拒绝迟到的旧bank，不等待保存完成才关门',async()=>{
 const f=fixture({mode:'auto'});await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});const lease=f.ownerLease;
 const stopping=f.schedule.stopPerformanceScheduleAndUseWindowsBalanced();assert.equal(await lease(),false);await stopping;
});
for(const dedicated of [false,true])for(const side of ['ac','dc']) {
  const label=(dedicated?'专属':'全局')+'/'+side;
  await check(label+' 手动读取与被动恢复重复20次不写硬件或设置',async()=>{
    const f=fixture({dedicated,side});const before=JSON.stringify(f.settings);
    await f.schedule.getContinuousPerformanceSnapshot();
    for(let i=0;i<20;i++)assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true}),'manual');
    assert.deepEqual(f.effects,[]);assert.equal(JSON.stringify(f.settings),before);
  });
  await check(label+' 手动无变化提交不重复下发',async()=>{
    const f=fixture({dedicated,side});let snap=await f.schedule.getContinuousPerformanceSnapshot();
    for(const patch of [{mode:'manual'},{tdp:snap.bank.tdp},{manual:{frequencyMHz:snap.bank.manual.frequencyMHz}},{}])snap=await f.schedule.updateContinuousPerformanceControls(snap.identity,patch);
    assert.deepEqual(f.effects,[]);
  });
  await check(label+' 用户实际调节一次仍下发且不启动浮动',async()=>{
    const f=fixture({dedicated,side});const snap=await f.schedule.getContinuousPerformanceSnapshot();
    const next=await f.schedule.updateContinuousPerformanceControls(snap.identity,{tdp:snap.bank.tdp-1});
    assert.equal(next.bank.tdp,snap.bank.tdp-1);assert.equal(f.effects.filter(x=>x==='tdp-'+side).length,1);
    assert.equal(f.effects.filter(x=>x==='cpu-'+side).length,1);assert(!f.effects.includes('start-float'));
    f.effects.length=0;await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});assert.deepEqual(f.effects,[]);
  });
  await check(label+' 自动模式被动恢复继续正常执行',async()=>{
    const f=fixture({dedicated,side,mode:'auto'});
    assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true}),'auto');
    assert(f.effects.includes('tdp-'+side));assert(f.effects.includes('cpu-'+side));assert(f.effects.includes('start-float'));
  });
}
await check('手动被动恢复只停止遗留自动浮动一次，不重设CPU/TDP',async()=>{
  const f=fixture({float:true});for(let i=0;i<5;i++)await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});
  assert.deepEqual(f.effects,['stop-float']);assert.equal(f.floating,false);
});
await check('AC自动/DC手动的恢复与归属按当前电源侧判断',async()=>{
  const f=fixture({mode:'auto'});f.settings.performanceSchedule.controls.dc.mode='manual';
  assert.equal(await f.schedule.getPerformanceScheduleOwnership(),'auto');f.sourceMode.value='dc';
  assert.equal(await f.schedule.getPerformanceScheduleOwnership(),'manual');
  await f.schedule.restorePerformanceScheduleIfConfigured('dc',{automaticOnly:true});assert.deepEqual(f.effects,[]);
});
await check('旧版手动配置被动恢复仍保持静态',async()=>{
  const f=fixture({legacy:true});assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true}),'manual');assert.deepEqual(f.effects,[]);
});
await check('显式启用/删除专属的原有恢复入口仍可应用手动参数',async()=>{
  const f=fixture();assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured(),'manual');assert(f.effects.includes('tdp-ac'));assert(f.effects.includes('cpu-ac'));
});
await check('dedicatedOnly在无专属时不接管全局',async()=>{
  const f=fixture({mode:'auto'});assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured('ac',{dedicatedOnly:true,automaticOnly:true}),'none');assert.deepEqual(f.effects,[]);
});
await check('启动/唤醒/方案变动/ACDC和常驻游戏恢复均显式限制自动模式',async()=>{
  const counts={};
  for(const file of ['src/App.vue','src/bridge/gamePolicyRuntime.ts']){
    const text=source(file),body=file.endsWith('.vue')?/<script\b[^>]*>([\s\S]*?)<\/script>/.exec(text)[1]:text;
    const ast=ts.createSourceFile(file+'.ts',body,ts.ScriptTarget.Latest,true);let count=0;
    function visit(node){if(ts.isCallExpression(node)&&node.expression.getText(ast)==='restorePerformanceScheduleIfConfigured'){
      count++;const options=node.arguments[1];assert(options&&ts.isObjectLiteralExpression(options),file+' missing passive options');
      assert(options.properties.some(p=>ts.isPropertyAssignment(p)&&p.name.getText(ast)==='automaticOnly'&&p.initializer.kind===ts.SyntaxKind.TrueKeyword),file+' missing automaticOnly:true');
    }ts.forEachChild(node,visit)}visit(ast);counts[file]=count;
  }
  assert.equal(counts['src/App.vue'],4);assert.equal(counts['src/bridge/gamePolicyRuntime.ts'],1);
});
for(const dedicated of [false,true])await check((dedicated?'专属':'全局')+' 实际常驻消费者启动/供电事件/多次重试保持手动静态',async()=>{
 const f=fixture({dedicated});const flush=async()=>{for(let i=0;i<180;i++)await Promise.resolve()};
 f.runtime.startGamePolicyRuntime();await flush();assert.deepEqual(f.effects,[]);
 for(const side of ['dc','ac','dc']){f.sourceMode.value=side;for(const callback of f.powerListeners)callback();await flush();assert.deepEqual(f.effects,[])}
 for(let i=0;i<20;i++)for(const run of f.tasks.values())await run();await flush();assert.deepEqual(f.effects,[]);
 f.runtime.stopGamePolicyRuntime();assert.equal(f.tasks.size,0);
});

const flushRuntime=async f=>{for(let i=0;i<1000;i++)await Promise.resolve();for(let attempt=0;attempt<2;attempt++){for(const run of f.tasks.values())await run();for(let i=0;i<200;i++)await Promise.resolve()}};
await check('自动专属的唤醒归属独立于全局手动/未配置状态',async()=>{
 for(const configured of [false,true]){const f=fixture({mode:'auto',dedicated:true,configured});f.settings.performanceSchedule.enabled=false;f.settings.performanceSchedule.controls.ac.mode='manual';assert.equal(await f.schedule.getPerformanceScheduleOwnership(),'auto')}
});
await check('手动专属覆盖全局自动时唤醒归属必须为手动',async()=>{
 const f=fixture({mode:'manual',dedicated:true});f.settings.performanceSchedule.controls.ac.mode='auto';assert.equal(await f.schedule.getPerformanceScheduleOwnership(),'manual');
});
await check('完全相同自动参数启用/删除专属仍重建有效owner，不丢失自动链路',async()=>{
 const f=fixture({mode:'auto',gameActive:true});f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 assert(f.ownerLease);const globalLease=f.ownerLease;assert.equal(await globalLease(),true);const controls=clone(f.settings.performanceSchedule.controls);
 await f.setEntry({displayName:'Game',enabled:true,controls});await flushRuntime(f);
 assert.equal(await globalLease(),false);assert.notEqual(f.ownerLease,globalLease);assert.equal(await f.ownerLease(),true);
 const dedicatedLease=f.ownerLease;await f.setEntry(null);await flushRuntime(f);assert.equal(await dedicatedLease(),false);assert.notEqual(f.ownerLease,dedicatedLease);assert.equal(await f.ownerLease(),true);
 f.runtime.stopGamePolicyRuntime();
});
await check('旧专属无controls时修改实际AC档位会自动重新下发',async()=>{
 const f=fixture({mode:'auto',gameActive:true});const global=await f.schedule.loadPerformanceSchedule();const ac=clone(global.profiles.ac.performance),dc=clone(global.profiles.dc.performance);
 await f.setEntry({displayName:'Legacy',enabled:true,ac,dc});f.effects.length=0;f.runtime.startGamePolicyRuntime();await flushRuntime(f);assert(f.ownerLease);const before=f.ownerLease;
 ac.tdpMax-=1;await f.setEntry({displayName:'Legacy',enabled:true,ac,dc});await flushRuntime(f);assert.notEqual(f.ownerLease,before);assert.equal(await f.ownerLease(),true);f.runtime.stopGamePolicyRuntime();
});
await check('自动AC/DC切换保留自动控制且旧供电owner失效',async()=>{
 const f=fixture({mode:'auto'});f.runtime.startGamePolicyRuntime();await flushRuntime(f);const ac=f.ownerLease;assert.equal(f.ownerSide,'ac');
 f.sourceMode.value='dc';for(const callback of f.powerListeners)callback();await flushRuntime(f);assert.equal(await ac(),false);assert.equal(f.ownerSide,'dc');assert.equal(await f.ownerLease(),true);
 f.runtime.stopGamePolicyRuntime();
});
await check('全局自动→专属手动停止自动写入，删除专属后恢复全局自动',async()=>{
 const f=fixture({mode:'auto',gameActive:true});f.runtime.startGamePolicyRuntime();await flushRuntime(f);const controls=clone(f.settings.performanceSchedule.controls);controls.ac.mode='manual';f.effects.length=0;
 await f.setEntry({displayName:'Manual',enabled:true,controls});await flushRuntime(f);assert.equal(f.floating,false);assert(!f.effects.includes('tdp-ac'));assert(!f.effects.includes('cpu-ac'));
 await f.setEntry(null);await flushRuntime(f);assert.equal(f.floating,true);assert.equal(await f.ownerLease(),true);f.runtime.stopGamePolicyRuntime();
});
await check('已生效专属的输入/名称和无关全局编辑不重启自动调度',async()=>{
 const f=fixture({mode:'auto',dedicated:true});f.runtime.startGamePolicyRuntime();await flushRuntime(f);const lease=f.ownerLease;assert(lease);
 const value=await f.schedule.loadGameCustomConfig();const entry=value.entries['game.exe'];await f.setEntry({...entry,displayName:'Renamed',padPersona:'elite'});await flushRuntime(f);assert.equal(f.ownerLease,lease);
 const global=await f.schedule.loadPerformanceSchedule();global.controls.ac.tdp-=1;await f.schedule.savePerformanceSchedule(global);await flushRuntime(f);assert.equal(f.ownerLease,lease);assert.equal(await lease(),true);f.runtime.stopGamePolicyRuntime();
});
await check('旧自动专属的正帧率修改不重放CPU，AC/DC切换仍重新建立调度',async()=>{
 const f=fixture({mode:'auto',gameActive:true});const global=await f.schedule.loadPerformanceSchedule();await f.setEntry({displayName:'Legacy',enabled:true,ac:clone(global.profiles.ac.performance),dc:clone(global.profiles.dc.performance)});
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);const lease=f.ownerLease;assert(lease);
 const custom=await f.schedule.loadGameCustomConfig();custom.rtss['game.exe']=frames.dedicatedFrameRateRecord(frames.frameRatePair({},100,60));await f.schedule.saveGameCustomConfig(custom);await flushRuntime(f);assert.equal(f.ownerLease,lease);
 f.sourceMode.value='dc';for(const callback of f.powerListeners)callback();await flushRuntime(f);assert.notEqual(f.ownerLease,lease);assert.equal(f.ownerSide,'dc');assert.equal(await lease(),false);assert.equal(await f.ownerLease(),true);f.runtime.stopGamePolicyRuntime();
});
const report={pass:results.every(x=>x.pass),count:results.length,failed:results.filter(x=>!x.pass).length,cases:results,hardwareCalls:0,scope:'Actual performanceSchedule.ts with in-memory external boundaries; AST checks passive callsite wiring. No YMCC process, OS power, service or device execution.',sources:Object.fromEntries(['src/bridge/performanceSchedule.ts','src/bridge/gamePolicyRuntime.ts','src/App.vue'].map(file=>[file,crypto.createHash('sha256').update(source(file)).digest('hex')]))};
const out=process.argv[2]&&!process.argv[2].startsWith('--')?process.argv[2]:null;
if(out)fs.mkdirSync(out,{recursive:true});
const reportPath=arg('--report')||(out?path.join(out,'performance-ownership.json'):null);
if(reportPath)fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report,null,2));if(!report.pass)process.exitCode=1;
