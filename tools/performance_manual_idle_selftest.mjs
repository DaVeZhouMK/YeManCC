import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import {createPerformanceMemoryFixture,models,frames} from './fixtures/performance_memory_fixture.mjs';
import {compileSettingsConsumer,createSettingsFixture} from './fixtures/settings_baseline.mjs';
const require=createRequire(path.resolve('package.json'));
const ts=require('typescript');
const clone=x=>structuredClone(x);
const arg=name=>{const i=process.argv.indexOf(name);return i<0?null:process.argv[i+1]};
const baseline=arg('--baseline');
const source=file=>fs.readFileSync(baseline?path.join(baseline,path.basename(file)):file,'utf8');
const fixture=options=>createPerformanceMemoryFixture({...options,source});
const results=[],notRun=[];
const filter=arg('--filter');
async function check(name,run){if(filter&&!name.includes(filter)){notRun.push(name);return;}try{await run();results.push({name,pass:true})}catch(error){results.push({name,pass:false,error:error.message})}}
let freshSettingsCode,freshSettingsCompilation;
async function initialRepositorySettings(initial) {
 freshSettingsCode??=await compileSettingsConsumer(process.cwd(),{onCompiled:result=>{
  const pin=file=>{const bytes=fs.readFileSync(file);return {bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')}};
  freshSettingsCompilation={inputs:Object.fromEntries(Object.keys(result.metafile.inputs).filter(file=>file!=='<stdin>').map(file=>[file,pin(file)])),bundleSha256:crypto.createHash('sha256').update(result.outputFiles[0].text).digest('hex'),node:pin(process.execPath)};
 }});
 const repository=createSettingsFixture(freshSettingsCode,initial);
 return clone(await repository.repo.loadSettings());
}
for(const template of ['desktop','portable'])await check('TDP fresh: missing configuration '+template+' applies the same defaults shown by the panel',async()=>{
 const settings=await initialRepositorySettings(),f=fixture({ports:{initialSettings:settings,template}});
 const shown=await f.schedule.getContinuousPerformanceSnapshot('global'),watts=template==='desktop'?120:55;
 assert.equal(shown.bank.tdp,watts);assert.equal(shown.bank.tdpValid,undefined,'fresh repository defaults were mistaken for a damaged saved target');
 assert.equal(shown.bank.cpuValid,undefined);assert.equal(f.hardwareWrites.length,0,'reading the panel wrote hardware');
 const receipt=await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{restoreRemembered:true,withReceipt:true});
 assert.equal(receipt.applied,true);assert.equal(receipt.application.tdp.requested,shown.bank.tdp);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,shown.bank.tdp);
});
await check('TDP fresh: old untouched initial document resolves desktop120 after a cold restart',async()=>{
 const settings=await initialRepositorySettings({performanceSchedule:{version:2,configured:true,enabled:true,active:{ac:'elite',dc:'medium'},profiles:{}}});
 for(let restart=0;restart<2;restart++){
  const f=fixture({ports:{initialSettings:settings,template:'desktop'}}),shown=await f.schedule.getContinuousPerformanceSnapshot('global');
  assert.equal(shown.bank.tdp,120);assert.notEqual(shown.bank.tdpValid,false);
  assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),true);assert.equal(f.hardwareWrites.at(-1)?.watts,120);
 }
});
await check('TDP fresh: missing or malformed saved values remain paused and explicit120 repairs the displayed target',async()=>{
 for(const raw of [{configured:true,enabled:true},{configured:true,enabled:true,controls:{ac:{tdp:null}}},{version:2,configured:true,enabled:true,active:{ac:'elite',dc:'medium'},profiles:{},controls:null}]){
  const settings=await initialRepositorySettings({performanceSchedule:raw}),f=fixture({ports:{initialSettings:settings,template:'desktop'}});
  const shown=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(shown.bank.tdpValid,false);
  assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),false);assert.equal(f.hardwareWrites.length,0);
  const repaired=await f.schedule.updateContinuousPerformanceControls(shown.identity,{tdp:120,tdpCeiling:120});
  assert.equal(repaired.bank.tdpValid,undefined);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,120);
 }
});
await check('TDP fresh: saved55 stays55 on a desktop and saved OFF stays closed',async()=>{
 const raw={version:2,configured:true,enabled:true,controls:models.normalizeContinuousControls(undefined)};
 for(const suspended of [false,true]){
  const settings=await initialRepositorySettings({performanceSchedule:{...raw,suspended}}),f=fixture({ports:{initialSettings:settings,template:'desktop'}});
  const shown=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(shown.bank.tdp,55);
  assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),!suspended);
  if(suspended)assert.equal(f.hardwareWrites.length,0);else assert.equal(f.hardwareWrites.at(-1)?.watts,55);
 }
});
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
for(const dedicated of [false,true])await check((dedicated?'dedicated':'global')+' resident restores remembered manual policy once per side event; idle retries do not replay',async()=>{
 const f=fixture({dedicated}),flush=async()=>{for(let i=0;i<1500;i++)await Promise.resolve();for(const run of f.tasks.values())await run();for(let i=0;i<1500;i++)await Promise.resolve()};
 f.runtime.startGamePolicyRuntime();await flush();assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);
 for(const side of ['dc','ac','dc']){const begin=f.hardwareWrites.filter(w=>w.kind==='cpu').length;f.sourceMode.value=side;for(const callback of f.powerListeners)callback();await flush();const writes=f.hardwareWrites.filter(w=>w.kind==='cpu');assert.equal(writes.length,begin+1);assert.deepEqual(writes.at(-1).params.sides,[side]);}
 const count=f.hardwareWrites.length;for(let i=0;i<20;i++)for(const run of f.tasks.values())await run();await flush();assert.equal(f.hardwareWrites.length,count);
 f.runtime.stopGamePolicyRuntime();assert.equal(f.tasks.size,0);
});

const flushRuntime=async f=>{for(let i=0;i<1000;i++)await Promise.resolve();for(let attempt=0;attempt<2;attempt++){for(const run of f.tasks.values())await run();for(let i=0;i<200;i++)await Promise.resolve()}};
await check('TDP invalid slow: bad source flag still restores displayed120 using cached owner without full settings reads',async()=>{
 const controls=models.normalizeContinuousControls(undefined);controls.ac.tdp=120;controls.ac.tdpCeiling=120;controls.ac.tdpValid=false;controls.ac.cpuValid=false;
 const ports={now:100000,initialSettings:{performanceSchedule:{version:2,configured:true,enabled:true,controls},rtss:{frameRates:frames.frameRatePair({})}}},f=fixture({ports});
 const shown=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(shown.bank.tdp,120);assert.equal(shown.bank.tdpValid,false);
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);assert.equal(f.hardwareWrites.length,0);
 f.settingsRepository.loadSettings=async()=>{throw Error('slow fallback reran full settings loading')};
 ports.now+=6000;await flushRuntime(f);assert.deepEqual(f.hardwareWrites.filter(w=>w.kind==='tdp').map(w=>w.watts),[120]);
 ports.now+=60000;await flushRuntime(f);assert.equal(f.hardwareWrites.length,1,'60second cached check rewrote hardware');
 ports.now+=60000;await flushRuntime(f);assert.deepEqual(f.hardwareWrites.map(w=>w.watts),[120,120]);
 f.runtime.stopGamePolicyRuntime();assert.equal(f.tasks.size,0);
});
await check('TDP invalid slow: explicit30 cancels old120 recovery and repaired data ends the slow loop',async()=>{
 const ports={now:100000},f=fixture({mode:'manual',ports});f.settings.performanceSchedule.controls.ac.tdp=120;f.settings.performanceSchedule.controls.ac.tdpCeiling=120;f.settings.performanceSchedule.controls.ac.tdpValid=false;
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);let shown=await f.schedule.getContinuousPerformanceSnapshot('global');
 await f.schedule.updateContinuousPerformanceControls(shown.identity,{tdp:30});await flushRuntime(f);
 ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);assert.equal(f.tasks.size,0);
 f.hardwareWrites.length=0;ports.now+=180000;await flushRuntime(f);assert.equal(f.hardwareWrites.length,0);
 f.runtime.stopGamePolicyRuntime();assert.equal(f.tasks.size,0);
 assert.equal((await f.schedule.getContinuousPerformanceSnapshot('global')).bank.tdp,30);
});
await check('TDP invalid slow: dedicated bad flag uses its own22 rather than global55 and new manual owner cancels queued recovery',async()=>{
 const ports={now:100000},f=fixture({mode:'manual',dedicated:true,ports});
 const custom=await f.schedule.loadGameCustomConfig();custom.entries['game.exe'].controls.ac.tdp=22;custom.entries['game.exe'].controls.ac.tdpCeiling=30;custom.entries['game.exe'].controls.ac.tdpValid=false;custom.entries['game.exe'].controls.ac.cpuValid=false;
 await f.schedule.saveGameCustomConfig(custom);const shown=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(shown.bank.tdp,22);
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);ports.now+=6000;await flushRuntime(f);assert.deepEqual(f.hardwareWrites.map(w=>w.watts),[22]);
 ports.beforeTdpWrite=async()=>{ports.beforeTdpWrite=undefined;const next=await f.schedule.loadGameCustomConfig();next.entries['game.exe'].controls.ac.tdp=15;delete next.entries['game.exe'].controls.ac.tdpValid;await f.schedule.saveGameCustomConfig(next)};
 ports.now+=120000;await flushRuntime(f);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp'&&w.watts===22).length,1,'queued old22 was dispatched after a newer15 owner');f.runtime.stopGamePolicyRuntime();
});
await check('TDP invalid slow: unavailable cached range waits cheaply without raising3 to hardware8 or reloading settings',async()=>{
 const ports={now:100000,tdpMinimum:8},f=fixture({ports});Object.assign(f.settings.performanceSchedule.controls.ac,{tdp:3,tdpCeiling:30,tdpValid:false,cpuValid:false});
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);f.settingsRepository.loadSettings=async()=>{throw Error('unavailable cache caused full reload')};
 ports.now+=6000;await flushRuntime(f);for(let n=0;n<5;n++){ports.now+=60000;await flushRuntime(f)}assert.equal(f.hardwareWrites.length,0);f.runtime.stopGamePolicyRuntime();assert.equal(f.tasks.size,0);
});
await check('TDP template: late no-battery confirmation reconciles a displayed120 instead of retaining initial55',async()=>{
 const settings=await initialRepositorySettings(),ports={initialSettings:settings,template:'portable',now:100000},f=fixture({ports});
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);assert.equal(f.tasks.size,0);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,55);f.hardwareWrites.length=0;
 f.setTemplate('desktop');const shown=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(shown.bank.tdp,120);
 await flushRuntime(f);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,shown.bank.tdp,'panel changed to120 without a new scheduler write');
 ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);assert.equal(f.tasks.size,0);
 f.runtime.stopGamePolicyRuntime();
});
await check('TDP template: battery confirmation cannot replay a saved55 owner',async()=>{
 for(const dedicated of [false,true]){
  const ports={template:'portable',now:100000},f=fixture({mode:'manual',dedicated,ports});
  f.runtime.startGamePolicyRuntime();await flushRuntime(f);ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);assert.equal(f.tasks.size,0);
  f.hardwareWrites.length=0;f.setTemplate('desktop');await flushRuntime(f);
  assert.equal(f.hardwareWrites.length,0);assert.equal(f.tasks.size,0);assert.equal((await f.schedule.getContinuousPerformanceSnapshot()).bank.tdp,55);
  f.runtime.stopGamePolicyRuntime();
 }
});
await check('TDP provenance: malformed template markers cannot authorize missing data or override saved legacy31',async()=>{
 for(const patch of [{templateDefaults:'true'},{templateDefaults:false},{templateDefaults:true,profiles:9},{templateDefaults:true,controls:null}]){
  const raw={version:2,configured:true,enabled:true,active:{ac:'elite',dc:'medium'},profiles:{},...patch};
  const settings=await initialRepositorySettings({performanceSchedule:raw}),f=fixture({ports:{initialSettings:settings,template:'desktop'}});
  assert.equal((await f.schedule.getContinuousPerformanceSnapshot('global')).bank.tdpValid,false);
  assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),false);assert.equal(f.hardwareWrites.length,0);
 }
 const raw={version:2,configured:true,enabled:true,templateDefaults:true,active:{ac:'elite',dc:'medium'},profiles:{ac:{elite:{tdpMax:31}}}};
 const settings=await initialRepositorySettings({performanceSchedule:raw}),f=fixture({ports:{initialSettings:settings,template:'desktop'}});
 const shown=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(shown.bank.tdp,31);
 assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),true);assert.equal(f.hardwareWrites.at(-1)?.watts,shown.bank.tdp);
});
await check('TDP fresh: first120W write failure retries only TDP and settles without a permanent poller',async()=>{
 const settings=await initialRepositorySettings(),ports={initialSettings:settings,template:'desktop',now:100000,beforeTdpWrite:()=>{throw Error('first transport unavailable')}},f=fixture({ports});
 const shown=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(shown.bank.tdp,120);
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,0);
 ports.beforeTdpWrite=undefined;ports.now+=30000;await flushRuntime(f);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,shown.bank.tdp);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1,'TDP retry replayed CPU');
 ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);assert.equal(f.tasks.size,0);
 const before=f.hardwareWrites.length;
 f.settingsRepository.loadSettings=async()=>{throw Error('healthy monitor caused a new settings read')};
 for(let i=0;i<1000;i++){ports.now+=3000;f.publishMonitor({hwDown:false,top:{tdpW:55,cpuPowerValid:true,ts:ports.now},fps:{hardwareObservedAt:ports.now}});}
 await flushRuntime(f);assert.equal(f.tasks.size,0);assert.equal(f.hardwareWrites.length,before,'low measured consumption caused a forced write');
 f.runtime.stopGamePolicyRuntime();
});
await check('TDP fresh: startup and wake settle checks keep the desktop120 target without replaying CPU',async()=>{
 const settings=await initialRepositorySettings(),ports={initialSettings:settings,template:'desktop',now:100000},f=fixture({ports});
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);
 assert.equal(f.tasks.size,0);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);
 assert.deepEqual(f.hardwareWrites.filter(w=>w.kind==='tdp').map(w=>w.watts),[120,120,120]);
 f.hardwareWrites.length=0;f.runtime.requestGamePolicyTdpReconciliation(2);await flushRuntime(f);ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);
 assert.deepEqual(f.hardwareWrites.filter(w=>w.kind==='tdp').map(w=>w.watts),[120,120,120]);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,0);
 assert.equal(f.tasks.size,0);f.runtime.stopGamePolicyRuntime();
});
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
 await f.setEntry({displayName:'Manual',enabled:true,controls});await flushRuntime(f);assert.equal(f.floating,false);assert(f.effects.includes('tdp-ac'));assert(f.effects.includes('cpu-ac'));assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').at(-1).params.acFreq,controls.ac.manual.frequencyMHz);
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
for (const mode of ['auto','manual']) await check('SMT '+mode+' 同构ROG/无游戏可设置，全局偏好跨游戏与ACDC稳定',async()=>{
 const f=fixture({mode,ports:{smtAvailable:true,heterogeneous:false,topologyDetected:false}});
 let snap=await f.schedule.getContinuousPerformanceSnapshot('effective');
 assert.equal(snap.hybrid,false);assert.equal(snap.smtAvailable,true);assert.equal(snap.hyperThreadMode,'default');
 const configured=f.settings.performanceSchedule.configured,enabled=f.settings.performanceSchedule.enabled;
 f.effects.length=0;f.hardwareWrites.length=0;
 await f.schedule.updateContinuousPerformanceControls(snap.identity,{hyperThreadMode:'off'});
 assert.equal(f.settings.performanceSchedule.hyperThreadPolicy,'off');assert.deepEqual(f.hardwareWrites,[]);assert.deepEqual(f.corePolicyCalls,[]);
 assert.equal(f.settings.performanceSchedule.configured,configured);assert.equal(f.settings.performanceSchedule.enabled,enabled);
 assert.equal(Object.keys((await f.schedule.loadGameCustomConfig()).entries).length,0);
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 const games=[{pid:101,processCreated:'born101',name:'a.exe',path:'C:/a.exe'},null,{pid:102,processCreated:'born102',name:'b.exe',path:'C:/b.exe'}];
 for(const game of games){f.setGame(game);await flushRuntime(f);snap=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(snap.hyperThreadMode,'off');assert.equal(f.settings.performanceSchedule.hyperThreadPolicy,'off');}
 assert(f.corePolicyCalls.some(c=>c.kind==='apply'&&c.args[0].pid===101&&c.args[2]==='off'));
 assert(f.corePolicyCalls.some(c=>c.kind==='apply'&&c.args[0].pid===102&&c.args[2]==='off'));
 f.sourceMode.value='dc';for(const callback of f.powerListeners)callback();await flushRuntime(f);
 snap=await f.schedule.getContinuousPerformanceSnapshot('global');assert.equal(snap.hyperThreadMode,'off');
 f.hardwareWrites.length=0;f.corePolicyCalls.length=0;
 await f.schedule.updateContinuousPerformanceControls(snap.identity,{hyperThreadMode:'on'});
 assert.equal(f.settings.performanceSchedule.hyperThreadPolicy,'default');assert.equal(f.corePolicyCalls.filter(c=>c.kind==='apply').length,0);
 assert(f.corePolicyCalls.some(c=>c.kind==='clear'));assert.deepEqual(f.hardwareWrites,[]);
 f.runtime.stopGamePolicyRuntime();
});
await check('SMT专属独立于全局，开启恢复默认；主性能页只显示全局偏好',async()=>{
 const f=fixture({mode:'manual',dedicated:true,ports:{smtAvailable:true,heterogeneous:false}});
 let global=await f.schedule.getContinuousPerformanceSnapshot('global');await f.schedule.updateContinuousPerformanceControls(global.identity,{hyperThreadMode:'off'});
 assert.equal(f.corePolicyCalls.length,0,'Global editor cannot override an enabled dedicated profile');
 assert.equal((await f.schedule.getContinuousPerformanceSnapshot('effective')).hyperThreadMode,'off');
 let dedicated=await f.schedule.getContinuousPerformanceSnapshot('dedicated');assert.equal(dedicated.hyperThreadMode,'default');
 await f.schedule.updateContinuousPerformanceControls(dedicated.identity,{hyperThreadMode:'off'});assert.equal(f.corePolicyCalls.at(-1).args[2],'off');
 dedicated=await f.schedule.getContinuousPerformanceSnapshot('dedicated');await f.schedule.updateContinuousPerformanceControls(dedicated.identity,{hyperThreadMode:'default'});
 assert.equal(f.corePolicyCalls.at(-1).kind,'clear');assert.equal((await f.schedule.getContinuousPerformanceSnapshot('global')).hyperThreadMode,'off');
 assert.equal((await f.schedule.getContinuousPerformanceSnapshot('dedicated')).hyperThreadMode,'default');
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);f.corePolicyCalls.length=0;
 const entry=(await f.schedule.loadGameCustomConfig()).entries['game.exe'];await f.setEntry({...entry,hyperThreadPolicyEnabled:true,hyperThreadPolicy:'off'});await flushRuntime(f);
 assert(f.corePolicyCalls.some(c=>c.kind==='apply'&&c.args[2]==='off'));
 f.setGame(null);await flushRuntime(f);assert.equal(f.settings.performanceSchedule.hyperThreadPolicy,'off');
 f.setGame({pid:33,processCreated:'born33',name:'other.exe',path:'C:/other.exe'});await flushRuntime(f);
 assert(f.corePolicyCalls.some(c=>c.kind==='apply'&&c.args[0].pid===33&&c.args[2]==='off'));f.runtime.stopGamePolicyRuntime();
});
await check('旧EXE独立记录不再反向改变全局开关；默认开启不施加线程限制',async()=>{
 const f=fixture({mode:'manual',configured:false,gameActive:true,ports:{smtAvailable:true}});
 await f.setEntry({displayName:'Legacy',enabled:false,smtIndependent:true,hyperThreadPolicyEnabled:true,hyperThreadPolicy:'off'});
 assert.equal((await f.schedule.getContinuousPerformanceSnapshot('global')).hyperThreadMode,'default');
 assert.equal((await f.schedule.getContinuousPerformanceSnapshot('effective')).hyperThreadMode,'default');
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 assert.equal(f.corePolicyCalls.filter(c=>c.kind==='apply').length,0);assert.deepEqual(f.hardwareWrites,[]);
 assert.equal(f.settings.performanceSchedule.configured,false);f.runtime.stopGamePolicyRuntime();
});
await check('SMT不支持时不制造能力；保存失败不改持久偏好或当前游戏限制',async()=>{
 const unavailable=fixture({gameActive:true,ports:{smtAvailable:false}}),bad=await unavailable.schedule.getContinuousPerformanceSnapshot('global');
 assert.equal(bad.smtAvailable,false);await assert.rejects(unavailable.schedule.updateContinuousPerformanceControls(bad.identity,{hyperThreadMode:'off'}));assert.deepEqual(unavailable.corePolicyCalls,[]);
 const f=fixture({gameActive:true,ports:{smtAvailable:true,beforeSettingsCommit:()=>{throw Error('disk full')}}});const s=await f.schedule.getContinuousPerformanceSnapshot('global');
 await assert.rejects(f.schedule.updateContinuousPerformanceControls(s.identity,{hyperThreadMode:'off'}),/disk full/);
 assert.notEqual(f.settings.performanceSchedule.hyperThreadPolicy,'off');assert.deepEqual(f.corePolicyCalls,[]);
});
await check('正式编辑的空值/零/负TDP不能被钳成最低瓦数，原配置与硬件均保留',async()=>{
 for(const dedicated of [false,true])for(const mode of ['manual','auto'])for(const bad of [null,undefined,'',NaN,Infinity,-1,0,1]){
  const f=fixture({dedicated,mode}),snapshot=await f.schedule.getContinuousPerformanceSnapshot('effective'),before=JSON.stringify(f.settings);
  await assert.rejects(f.schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:bad}));
  assert.equal(JSON.stringify(f.settings),before);assert.deepEqual(f.hardwareWrites,[]);
 }
});
await check('新TDP操作排队即撤销旧bank尚未派发的高瓦数，正常新操作仍可写入',async()=>{
 let release,waiting=false;const f=fixture({mode:'auto',ports:{beforeTdpWrite:async()=>{if(!waiting){waiting=true;await new Promise(r=>release=r)}}}});
 f.settings.performanceSchedule.controls.ac.tdp=30;
 const restoring=f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});
 for(let n=0;n<300&&!release;n++)await Promise.resolve();assert.equal(typeof release,'function');
 const snapshot=await f.schedule.getContinuousPerformanceSnapshot('effective');const editing=f.schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:15});
 release();await Promise.allSettled([restoring,editing]);
 const writes=f.hardwareWrites.filter(w=>w.kind==='tdp');assert.deepEqual(writes.map(w=>w.watts),[15]);
});
await check('配置TDP缺失或损坏不再补成默认55/30W或最低2W，CPU独立生效且有效编辑可恢复',async()=>{
 for(const side of ['ac','dc'])for(const bad of [null,undefined,'',NaN,Infinity,0,-1]){
  const f=fixture({side,mode:'auto'});f.settings.performanceSchedule.controls[side].tdp=15;
  await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});const begin=f.hardwareWrites.length;
  f.settings.performanceSchedule.controls[side].tdp=bad;await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});
  const delta=f.hardwareWrites.slice(begin);assert.deepEqual(delta.filter(w=>w.kind==='tdp'),[]);assert.ok(delta.some(w=>w.kind==='cpu'));assert.equal(f.floating,false);
  const current=await f.schedule.getContinuousPerformanceSnapshot('effective');assert.equal(current.bank.tdp,15);
  await f.schedule.updateContinuousPerformanceControls(current.identity,{tdp:16});assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1).watts,16);assert.equal(f.floating,true);
 }
});
await check('当前游戏专属文件读取失败不会自动升到全局TDP；恢复数据只撤销旧完成缓存',async()=>{
 for(const dedicated of [false,true]){
  const ports={},f=fixture({mode:'auto',gameActive:true,dedicated,ports});const initial=await f.schedule.getContinuousPerformanceSnapshot('effective');
  await f.schedule.updateContinuousPerformanceControls(initial.identity,{tdp:15});let invalidated=0;const off=f.schedule.onContinuousPerformanceApplied(()=>{},()=>{invalidated++});
  ports.customReadFailure=true;const begin=f.hardwareWrites.length;await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});
  assert.deepEqual(f.hardwareWrites.slice(begin).filter(w=>w.kind==='tdp'),[]);assert.equal(f.floating,false);
  const count=invalidated;ports.customReadFailure=false;await f.schedule.loadGameCustomConfig();assert.ok(invalidated>count);
  const recovery=f.hardwareWrites.length;await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});
  assert.equal(f.hardwareWrites.slice(recovery).filter(w=>w.kind==='tdp').at(-1).watts,15);assert.equal(f.floating,true);off();
 }
});
await check('旧档位迁移也保留TDP无效状态，不能把空值或负数变成55W或2W',async()=>{
 for(const side of ['ac','dc'])for(const bad of [null,undefined,'',0,-1]){
  const f=fixture({side,mode:'auto'}),base=f.schedule.defaultPerformanceScheduleConfig();delete f.settings.performanceSchedule.controls;
  f.settings.performanceSchedule.profiles=base.profiles;f.settings.performanceSchedule.active={ac:'elite',dc:'medium'};f.settings.performanceSchedule.enabled=true;
  f.settings.performanceSchedule.profiles[side][base.active[side]].tdpMax=bad;await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});
  assert.deepEqual(f.hardwareWrites.filter(w=>w.kind==='tdp'),[]);assert.equal(f.floating,false);assert.ok(f.hardwareWrites.some(w=>w.kind==='cpu'));
 }
});
await check('已运行的连续配置整块缺失或结构错误，沿用15W缓存而不切回默认或旧档位',async()=>{
 for(const side of ['ac','dc'])for(const bad of [null,undefined,{},[],'bad']){
  const f=fixture({side,mode:'auto'});f.settings.performanceSchedule.controls[side].tdp=15;await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});
  const begin=f.hardwareWrites.length;f.settings.performanceSchedule.controls=bad;await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});
  assert.deepEqual(f.hardwareWrites.slice(begin).filter(w=>w.kind==='tdp'),[]);assert.equal(f.floating,false);assert.equal((await f.schedule.getContinuousPerformanceSnapshot('effective')).bank.tdp,15);
 }
});
await check('同内容专属缓存重读不误撤销TDP发送权；实际TDP变更仍立即撤销',async()=>{
 const f=fixture({mode:'auto',dedicated:true});await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});const lease=f.ownerLease;
 assert.equal(typeof lease.isCurrent,'function');assert.equal(lease.isCurrent(),true);await f.schedule.loadGameCustomConfig();assert.equal(lease.isCurrent(),true);
 const config=await f.schedule.loadGameCustomConfig();config.entries['game.exe'].controls.ac.tdp=14;await f.schedule.saveGameCustomConfig(config);assert.equal(lease.isCurrent(),false);
});
await check('专属中无关扩展字段不阻断已确认的TDP与CPU消费',async()=>{
 const f=fixture({mode:'auto',dedicated:true});const config=await f.schedule.loadGameCustomConfig();config.entries['game.exe'].controls.ac.futureBank={keep:true};await f.schedule.saveGameCustomConfig(config);
 await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,1);assert.ok(f.hardwareWrites.some(w=>w.kind==='cpu'));
});

for(const dedicated of [false,true])for(const side of ['ac','dc'])await check((dedicated?'dedicated':'global')+'/'+side+' remembered manual CPU restored on lifecycle without read replay',async()=>{
 const f=fixture({dedicated,side,mode:'manual'}),snapshot=await f.schedule.getContinuousPerformanceSnapshot();
 await f.schedule.updateContinuousPerformanceControls(snapshot.identity,{manual:{frequencyMHz:1800,turbo:true}});f.hardwareWrites.length=0;
 await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true,restoreRemembered:true,forceRemembered:true});
 const writes=f.hardwareWrites.filter(w=>w.kind==='cpu');assert.equal(writes.length,1);assert.equal(writes[0].params[side+'Freq'],1800);
 const count=f.hardwareWrites.length;for(let i=0;i<10;i++){await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true,restoreRemembered:true});}assert.equal(f.hardwareWrites.length,count);
});
for(const dedicated of [false,true])await check((dedicated?'dedicated':'global')+' superseded high CPU batch is cancelled before dispatch',async()=>{
 let release,entered;const waiting=new Promise(r=>entered=r),gate=new Promise(r=>release=r);let first=true;
 const f=fixture({mode:'manual',dedicated,ports:{beforeCpuWrite:async()=>{if(first){first=false;entered();await gate}}}});
 const old=f.schedule.restorePerformanceScheduleIfConfigured().catch(()=>null);await waiting;
 const current=await f.schedule.getContinuousPerformanceSnapshot();const low=f.schedule.updateContinuousPerformanceControls(current.identity,{manual:{frequencyMHz:1800}});
 release();await old;await low;const writes=f.hardwareWrites.filter(w=>w.kind==='cpu');assert.equal(writes.length,1);assert.equal(writes[0].params.acFreq,1800);
});
await check("global edits update extra boot baseline without mutating scenes 3",async()=>{
 const f=fixture({mode:'manual'}),base=await f.schedule.getContinuousPerformanceSnapshot();
 f.settings.extensions={bootModes:{schema:1,revision:7,current:'steam',boot:'steam',bootEnabled:true,global:{performance:{controls:clone(base.controls),frames:clone(base.frameRates)}},scenes:{steam:{overrides:{performance:{controls:{ac:{tdp:25}}}}}}}};
 const extra=JSON.stringify(f.settings.extensions.bootModes.scenes);await f.schedule.updateContinuousPerformanceControls(base.identity,{tdp:15,manual:{frequencyMHz:1800}});
 const doc=f.settings.extensions.bootModes;assert.equal(doc.global.performance.controls.ac.tdp,15);assert.equal(doc.global.performance.controls.ac.manual.frequencyMHz,1800);assert.equal(JSON.stringify(doc.scenes),extra);assert(doc.revision>7);
});
await check("dedicated edits retain global and extra presets 4",async()=>{
 const f=fixture({mode:'manual',dedicated:true});f.settings.extensions={bootModes:{schema:1,revision:2,global:{performance:{retained:true}},scenes:{steam:{overrides:{performance:{own:true}}}}}};
 const global=JSON.stringify(f.settings.performanceSchedule),boot=JSON.stringify(f.settings.extensions);const current=await f.schedule.getContinuousPerformanceSnapshot();
 await f.schedule.updateContinuousPerformanceControls(current.identity,{tdp:15,manual:{frequencyMHz:1800}});assert.equal(JSON.stringify(f.settings.performanceSchedule),global);assert.equal(JSON.stringify(f.settings.extensions),boot);
 const next=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(next.bank.tdp,15);assert.equal(next.bank.manual.frequencyMHz,1800);
});
await check('bad CPU frequency/bias retains memory but never authorizes a higher default; valid CPU edit recovers',async()=>{
 for(const mode of ['auto','manual'])for(const bad of mode==='auto'?[null,-1,NaN,Infinity]:[null,0,-1,NaN,Infinity]){
  const f=fixture({mode}),current=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(current.identity,mode==='auto'?{auto:{bias:75}}:{manual:{frequencyMHz:1800}});
  const raw=f.settings.performanceSchedule.controls.ac;if(mode==='auto')raw.auto.bias=bad;else raw.manual.frequencyMHz=bad;
  const before=f.hardwareWrites.length;await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{restoreRemembered:true});assert.equal(f.hardwareWrites.slice(before).filter(w=>w.kind==='cpu').length,0);assert.equal(f.floating,false);
  const snapshot=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(snapshot.identity,mode==='auto'?{auto:{bias:80}}:{manual:{frequencyMHz:1700}});assert(f.hardwareWrites.slice(before).some(w=>w.kind==='cpu'));
 }
});

for(const dedicated of [false,true])await check((dedicated?'dedicated':'global')+' all TDP commands save sole bank and require their own write receipt',async()=>{
 const ports={},f=fixture({mode:'manual',dedicated,gameActive:true,ports});let snap=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(snap.identity,{tdp:18});
 const global=JSON.stringify(f.settings.performanceSchedule);const count=f.hardwareWrites.filter(w=>w.kind==='tdp').length;
 const result=await f.schedule.adjustContinuousPerformanceTdp(-1);assert.equal(result.applied,true);assert.equal(result.tdp,17);snap=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(snap.bank.tdp,17);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,count+1);
 assert.equal((await f.schedule.setContinuousPerformanceTdp(16)).applied,true);assert.equal((await f.schedule.getContinuousPerformanceSnapshot()).bank.tdp,16);
 if(dedicated)assert.equal(JSON.stringify(f.settings.performanceSchedule),global);
});

await check('dedicated CPU-only lower selection immediately revokes old floating dispatch without file IO on checks',async()=>{
 const f=fixture({dedicated:true,mode:'auto'});await f.schedule.restorePerformanceScheduleIfConfigured();const lease=f.ownerLease;assert.equal(lease.isCurrent(),true);
 const config=await f.schedule.loadGameCustomConfig();config.entries['game.exe'].controls.ac.auto.bias=90;await f.schedule.saveGameCustomConfig(config);const before=f.effects.length;
 for(let i=0;i<1000;i++)assert.equal(lease.isCurrent(),false);assert.equal(f.effects.length,before);
});

for(const dedicated of [false,true])await check((dedicated?'dedicated':'global')+' fresh process restores persisted manual CPU and TDP once rather than new defaults',async()=>{
 const f=fixture({mode:'manual',dedicated,gameActive:true}),snapshot=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:15,manual:{frequencyMHz:1800}});
 const restarted=fixture({dedicated,gameActive:true,ports:{initialSettings:clone(f.settings),initialCustom:clone(await f.schedule.loadGameCustomConfig())}});
 restarted.runtime.startGamePolicyRuntime();await flushRuntime(restarted);const cpu=restarted.hardwareWrites.filter(w=>w.kind==='cpu'),tdp=restarted.hardwareWrites.filter(w=>w.kind==='tdp');assert.equal(cpu.length,1);assert.equal(cpu[0].params.acFreq,1800);assert.equal(tdp.length,1);assert.equal(tdp[0].watts,15);
 const count=restarted.hardwareWrites.length;await flushRuntime(restarted);assert.equal(restarted.hardwareWrites.length,count);restarted.runtime.stopGamePolicyRuntime();
});
await check('manual CPU invalid small/fractional edits never round upward or write; zero bias still explicitly permits unlimited',async()=>{
 const f=fixture({mode:'manual'}),snapshot=await f.schedule.getContinuousPerformanceSnapshot();
 for(const frequencyMHz of [0,100,799,1850,7300])await assert.rejects(f.schedule.updateContinuousPerformanceControls(snapshot.identity,{manual:{frequencyMHz}}));assert.equal(f.hardwareWrites.length,0);
 const current=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(current.identity,{mode:'auto',auto:{bias:0}});assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').at(-1).params.acFreq,0);
});

await check('hardware minimum cannot silently increase chosen TDP; CPU-only edits retain low invalid budget',async()=>{
 const ports={tdpMinimum:2},f=fixture({mode:'manual',ports});let snap=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(snap.identity,{tdp:5});ports.tdpMinimum=8;snap=await f.schedule.getContinuousPerformanceSnapshot();
 const before=f.hardwareWrites.length;await assert.rejects(f.schedule.setContinuousPerformanceTdp(5));await assert.rejects(f.schedule.updateContinuousPerformanceControls(snap.identity,{tdp:5}));assert.equal(f.hardwareWrites.length,before);
 const next=await f.schedule.updateContinuousPerformanceControls(snap.identity,{manual:{frequencyMHz:1800}});assert.equal(next.bank.tdp,5);assert.equal(next.bank.tdpValid,false);assert(!f.hardwareWrites.slice(before).some(w=>w.kind==='tdp'));assert(f.hardwareWrites.slice(before).some(w=>w.kind==='cpu'&&w.params.acFreq===1800));
});
await check('TDP safety: saved invalid CPU cannot suppress 55W startup or replay the whole bank on retries',async()=>{
 const ports={now:100000},f=fixture({mode:'auto',ports});f.settings.performanceSchedule.controls.ac.cpuValid=false;
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,55);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,0);
 const before=f.hardwareWrites.length;await flushRuntime(f);assert.equal(f.hardwareWrites.length,before,'unchanged invalid CPU must not cause repeated complete restore');
 ports.now+=4000;await flushRuntime(f);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,2,'one delayed TDP-only correction');
 ports.now+=8000;await flushRuntime(f);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,3,'last bounded TDP-only correction');
 assert.equal(f.hardwareWrites.filter(w=>w.kind!=='tdp').length,0);assert.equal(f.tasks.size,0,'healthy settled owner must stop recovery wakeups');f.runtime.stopGamePolicyRuntime();
});
await check('TDP safety: unknown game readiness with empty dedicated bank still restores valid global 55W',async()=>{
 const ports={gameReady:false},f=fixture({mode:'auto',ports});assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),true);
 assert.deepEqual(f.hardwareWrites.map(w=>[w.kind,w.watts]),[['tdp',55]]);assert.equal(f.floating,false);
 const dedicated=fixture({dedicated:true,mode:'auto',ports:{gameReady:false}});assert.equal(await dedicated.schedule.reconcilePerformanceScheduleTdp(),false);assert.deepEqual(dedicated.hardwareWrites,[]);
});
await check('TDP safety: wake and late OEM override recover without FPS/GPU or valid CPU',async()=>{
 const ports={now:100000},f=fixture({mode:'auto',ports});f.settings.performanceSchedule.controls.ac.cpuValid=false;f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 ports.now+=12000;await flushRuntime(f);ports.now+=4000;await flushRuntime(f);f.hardwareWrites.length=0;
 f.runtime.requestGamePolicyTdpReconciliation(2);await flushRuntime(f);assert.equal(f.hardwareWrites.at(-1)?.watts,55);
 ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);assert.equal(f.hardwareWrites.length,3);
 f.hardwareWrites.length=0;
 for(let n=0;n<5;n++){ports.now+=3000;f.publishMonitor({hwDown:false,top:{tdpW:70,cpuPowerValid:true,ts:ports.now},fps:{hardwareObservedAt:ports.now}});await flushRuntime(f);}
 assert.equal(f.hardwareWrites.length,1);assert.equal(f.hardwareWrites[0].watts,55);assert.equal(f.hardwareWrites[0].kind,'tdp');
 f.runtime.stopGamePolicyRuntime();assert.equal(f.tasks.size,0);ports.now+=30000;f.publishMonitor({top:{tdpW:70,cpuPowerValid:true,ts:ports.now}});assert.equal(f.tasks.size,0);
});
await check('TDP safety: invalid targets, explicit OFF, unknown source and superseded cached owner cannot dispatch',async()=>{
 for(const state of ['invalid','off','source','power']){const ports={},f=fixture({mode:'auto',ports});if(state==='invalid')f.settings.performanceSchedule.controls.ac.tdpValid=false;if(state==='off')f.settings.performanceSchedule.suspended=true;if(state==='source')f.sourceMode.value=null;if(state==='power')ports.lifecycle={phase:'resuming',hardwareWritesAllowed:false,generation:2};assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),false);assert.equal(f.hardwareWrites.length,0,state);}
 const ports={},f=fixture({mode:'auto',ports});ports.beforeTdpWrite=async()=>{const c=await f.schedule.loadPerformanceSchedule();c.controls.ac.tdp=15;await f.schedule.savePerformanceSchedule(c)};assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),false);assert.equal(f.hardwareWrites.length,0);
 const rawPorts={},raw=fixture({mode:'auto',ports:rawPorts});rawPorts.beforeTdpWrite=async()=>{raw.settings.performanceSchedule.controls.ac.tdp=15};assert.equal(await raw.schedule.reconcilePerformanceScheduleTdp(),false);assert.equal(raw.hardwareWrites.length,0,'direct settings commit invalidates the old cached ceiling without a full reload');
});
await check('TDP safety: missing, stale, repeated and short-peak monitor data never manufacture a correction',async()=>{
 const ports={now:100000},f=fixture({mode:'auto',ports});f.settings.performanceSchedule.controls.ac.cpuValid=false;f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);f.hardwareWrites.length=0;
 for(const invalid of ['missing','stale','repeated','peak']){
  const stamp=ports.now;
  for(let n=0;n<5;n++){ports.now+=3000;f.publishMonitor({hwDown:false,top:{tdpW:invalid==='peak'&&n%2?55:70,cpuPowerValid:invalid!=='missing',ts:ports.now},fps:{hardwareObservedAt:invalid==='stale'?ports.now-7000:invalid==='repeated'?stamp:ports.now}});await flushRuntime(f);}
  f.publishMonitor(null);
 }
 assert.equal(f.hardwareWrites.length,0);assert.equal(f.tasks.size,0);f.runtime.stopGamePolicyRuntime();
});
await check('TDP safety: partial TDP failure retries only TDP, never replays an acknowledged CPU policy',async()=>{
 const ports={now:100000,beforeTdpWrite:()=>{throw Error('fixture transport offline')}},f=fixture({mode:'manual',ports});f.runtime.startGamePolicyRuntime();await flushRuntime(f);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);
 await flushRuntime(f);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);
 ports.beforeTdpWrite=undefined;ports.now+=30000;await flushRuntime(f);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,55);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);f.runtime.stopGamePolicyRuntime();
});
await check('TDP safety: duplicate wake, OFF and a source change cancel delayed corrections of the previous owner',async()=>{
 const ports={now:100000},f=fixture({mode:'manual',ports});f.runtime.startGamePolicyRuntime();await flushRuntime(f);f.hardwareWrites.length=0;
 f.runtime.requestGamePolicyTdpReconciliation(2);await flushRuntime(f);assert.equal(f.hardwareWrites.length,1);
 for(let n=0;n<10;n++)f.runtime.requestGamePolicyTdpReconciliation(2);await flushRuntime(f);assert.equal(f.hardwareWrites.length,1);
 f.sourceMode.value='dc';for(const cb of f.powerListeners)cb();await flushRuntime(f);f.hardwareWrites.length=0;ports.now+=4000;await flushRuntime(f);assert.ok(f.hardwareWrites.every(w=>w.kind==='tdp'&&w.side==='dc'&&w.watts===30));
 await f.schedule.stopPerformanceScheduleAndUseWindowsBalanced();await flushRuntime(f);f.hardwareWrites.length=0;ports.now+=30000;await flushRuntime(f);assert.equal(f.hardwareWrites.length,0);f.runtime.stopGamePolicyRuntime();
});
await check('TDP safety: queued manual slider edit has priority over a background correction',async()=>{
 const f=fixture({mode:'manual'}),snapshot=await f.schedule.getContinuousPerformanceSnapshot();
 const editing=f.schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:15});
 const [edit,recovery]=await Promise.allSettled([editing,f.schedule.reconcilePerformanceScheduleTdp()]);
 assert.equal(recovery.status,'fulfilled');assert.equal(recovery.value,false,'background correction must not supersede an already queued player operation');
 assert.equal(edit.status,'fulfilled');assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,15);
});
await check('TDP recovery: enabled float with expired CPU lease yields only TDP to current saved30 and passive44W correction',async()=>{
 const ports={now:100000},f=fixture({mode:'auto',ports});
 let s=await f.schedule.getContinuousPerformanceSnapshot();await f.schedule.updateContinuousPerformanceControls(s.identity,{tdp:30});assert.equal(f.floating,true);const old=f.ownerLease;
 f.settings.performanceSchedule.controls.ac.cpuValid=false;await f.schedule.loadPerformanceSchedule();assert.equal(old.isCurrent(),false);
 f.hardwareWrites.length=0;assert.equal(await f.schedule.reconcilePerformanceScheduleTdp(),true);
 assert.deepEqual(f.hardwareWrites,[{kind:'tdp',side:'ac',watts:30}]);assert.equal(f.ownerLease,old,'TDP recovery must not restart float or CPU');
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);f.hardwareWrites.length=0;
 for(let i=0;i<4;i++){ports.now+=3000;f.publishMonitor({hwDown:false,top:{tdpW:44,cpuPowerValid:true,ts:ports.now}});await flushRuntime(f);}
 assert.deepEqual(f.hardwareWrites,[{kind:'tdp',side:'ac',watts:30}]);assert.equal(f.ownerLease,old);f.runtime.stopGamePolicyRuntime();
});
await check('TDP recovery: first30W write failure retains saved target and recovers independently without CPU/FPS/GPU',async()=>{
 const ports={now:100000,beforeTdpWrite:()=>{throw Error('fixture first write failed')}},f=fixture({mode:'auto',ports});
 f.settings.performanceSchedule.controls.ac.cpuValid=false;f.settings.performanceSchedule.controls.ac.tdpValid=false;
 const s=await f.schedule.getContinuousPerformanceSnapshot(),saved=await f.schedule.updateContinuousPerformanceControls(s.identity,{tdpCeiling:30});
 assert.equal(saved.bank.tdp,30);assert.equal(saved.tdpWriteReceipt.ok,false);assert.equal(saved.bank.cpuValid,false);assert.notEqual(saved.bank.tdpValid,false);
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,0);
 ports.beforeTdpWrite=undefined;ports.now+=30000;await flushRuntime(f);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,30);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,0);f.runtime.stopGamePolicyRuntime();
});
for(const dedicated of [false,true])for(const side of ['ac','dc'])for(const mode of ['auto','manual']) {
 await check(`TDP dropdown: ${dedicated?'dedicated':'global'}/${side}/${mode} explicit ceiling repairs invalid target and leaves CPU invalid`,async()=>{
  const controls=models.normalizeContinuousControls(undefined),other=side==='ac'?'dc':'ac';
  controls[side]={...controls[side],mode,tdp:55,tdpCeiling:55,tdpValid:false,cpuValid:false};
  const original=clone(controls),settings={performanceSchedule:{version:2,configured:true,enabled:true,controls},rtss:{frameRates:frames.frameRatePair({})}};
  const custom={version:1,entries:dedicated?{'game.exe':{enabled:true,displayName:'Game',controls:clone(controls)}}:{},rtss:{}};
  const f=fixture({side,mode,dedicated,ports:{initialSettings:settings,initialCustom:custom}});
  const before=await f.schedule.getContinuousPerformanceSnapshot(),result=await f.schedule.updateContinuousPerformanceControls(before.identity,{tdpCeiling:30});
  assert.equal(result.bank.tdp,30);assert.equal(result.bank.tdpCeiling,30);assert.notEqual(result.bank.tdpValid,false);
  assert.equal(result.bank.cpuValid,false,'TDP selection cannot validate unknown CPU controls');
  assert.deepEqual(f.hardwareWrites.filter(w=>w.kind==='tdp'),[{kind:'tdp',side,watts:30}]);
  assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,0);
  assert.deepEqual(clone(result.controls[other]),original[other]);
  const reopened=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(reopened.bank.tdp,30);assert.notEqual(reopened.bank.tdpValid,false);
  if(dedicated)assert.deepEqual(clone(f.settings.performanceSchedule.controls),original,'dedicated edit must not replace global');
  const count=f.hardwareWrites.length;await f.schedule.updateContinuousPerformanceControls(reopened.identity,{tdpCeiling:30});assert.equal(f.hardwareWrites.length,count,'same selection is idempotent');
 });
}
await check('TDP dropdown: original factory-shaped export accepts30 without a slider touch and recovers44W with no FPS/GPU',async()=>{
 const settings={performanceSchedule:{version:2,configured:true,enabled:true,active:{ac:'elite',dc:'medium'},profiles:{}},rtss:{frameRates:frames.frameRatePair({})}},ports={initialSettings:settings,now:100000};
 const f=fixture({side:'ac',ports}),before=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(before.bank.tdpValid,false);
 const saved=await f.schedule.updateContinuousPerformanceControls(before.identity,{tdpCeiling:30});
 assert.notEqual(saved.bank.tdpValid,false);assert.equal(saved.bank.tdp,30);assert.equal(saved.bank.cpuValid,false);
 assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1)?.watts,30);
 f.runtime.startGamePolicyRuntime();await flushRuntime(f);ports.now+=4000;await flushRuntime(f);ports.now+=8000;await flushRuntime(f);f.hardwareWrites.length=0;
 for(let i=0;i<4;i++){ports.now+=3000;f.publishMonitor({hwDown:false,top:{tdpW:44,cpuPowerValid:true,ts:ports.now}});await flushRuntime(f);}
 assert.deepEqual(f.hardwareWrites.filter(w=>w.kind==='tdp'),[{kind:'tdp',side:'ac',watts:30}]);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,0);
 f.runtime.stopGamePolicyRuntime();
});
await check('TDP dropdown: valid lower target remains lower; invalid display is replaced by explicit ceiling, unrelated edits do not validate',()=>{
 const base=models.defaultContinuousBank('ac');base.tdp=18;
 const capped=models.patchContinuousBank(base,{tdpCeiling:30},'ac');assert.equal(capped.tdp,18);
 const invalid={...base,tdpValid:false,cpuValid:false};
 const recovered=models.patchContinuousBank(invalid,{tdpCeiling:30},'ac');assert.equal(recovered.tdp,30);assert.notEqual(recovered.tdpValid,false);assert.equal(recovered.cpuValid,false);
 for(const patch of [{auto:{saving:'none'}},{auto:{coreSplit:10}},{mode:'manual'},{frameRate:{fps:30,ceiling:60,lastFps:30}}])assert.equal(models.patchContinuousBank(invalid,patch,'ac').tdpValid,false);
 for(const value of [NaN,Infinity,-Infinity,null,'30',undefined,0,1])assert.throws(()=>models.patchContinuousBank(invalid,{tdpCeiling:value},'ac'));
});
await check('TDP dropdown: ceiling below hardware minimum refuses without saving or silently raising watts',async()=>{
 const f=fixture({mode:'manual',ports:{tdpMinimum:8}}),s=await f.schedule.getContinuousPerformanceSnapshot(),before=JSON.stringify(f.settings);
 await assert.rejects(f.schedule.updateContinuousPerformanceControls(s.identity,{tdpCeiling:3}));
 assert.equal(JSON.stringify(f.settings),before);assert.equal(f.hardwareWrites.length,0);
});
await check('TDP safety: shell starts its sole resident writer before optional fan and media awaits',()=>{
 const app=source('src/App.vue'),start=app.indexOf('  startGamePolicyRuntime();');
 assert(start>app.indexOf('nativePowerControlDir = dir || null;'));assert(start<app.indexOf('  await initializeFanFeature();'));assert(start<app.indexOf('  const initialBackgroundState = await backgroundGet()'));
 assert.equal((app.match(/  startGamePolicyRuntime\(\);/g)||[]).length,1);
 assert(app.includes('requestGamePolicyTdpReconciliation(targetGeneration)'),'committed wake must schedule TDP-only settle checks');
});
assert(results.length>0,'No selected performance ownership case');
const report={pass:results.every(x=>x.pass),count:results.length,failed:results.filter(x=>!x.pass).length,cases:results,selection:filter??'all',notRun,hardwareCalls:0,scope:'Actual performanceSchedule.ts with in-memory external boundaries; AST checks passive callsite wiring. No YMCC process, OS power, service or device execution.',sources:Object.fromEntries(['src/bridge/performanceSchedule.ts','src/bridge/performanceControls.ts','src/bridge/gamePolicyRuntime.ts','src/App.vue','src/bridge/bootModeGlobalPreferences.ts','src/bridge/inputConfigHash.ts','src/bridge/settingsBaseline.ts','PowerControl/settings-baseline.json','tools/fixtures/performance_memory_fixture.mjs','tools/performance_manual_idle_selftest.mjs'].map(file=>[file,crypto.createHash('sha256').update(source(file)).digest('hex')]))};
if(freshSettingsCompilation)report.repositoryCompilation=freshSettingsCompilation;
const out=process.argv[2]&&!process.argv[2].startsWith('--')?process.argv[2]:null;
if(out)fs.mkdirSync(out,{recursive:true});
const reportPath=arg('--report')||(out?path.join(out,'performance-ownership.json'):null);
if(reportPath)fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify(report,null,2));if(!report.pass)process.exitCode=1;
