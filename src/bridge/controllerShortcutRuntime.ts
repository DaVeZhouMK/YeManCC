import { on, invoke } from './ipc';
import { powerLifecycle, shell, windowApi } from './api';
import { readSettingsSection } from './settingsRepository';
import { createControllerShortcutActions, type ShortcutActionOutcome, type ShortcutExecutionContext } from './controllerShortcutActions';
import { tryAcquireQuickAction } from './quickActionLock';
import { updateMonitorOutputEnabled, monitorOutputCoordinator } from './monitorOutput';
import { getContinuousPerformanceSnapshot, updateContinuousPerformanceControls } from './performanceSchedule';
import { frameRateSetting } from './frameRateModel';
import { effectiveFrameRates } from './frameRateLimits';
import { getPolicyGame, gamePolicyKey } from './gamePolicyTarget';
import { powerSourceMode } from './powerSource';
import { createFanMirrorActions } from './deckyFanActions';
import { fanHostLifecycle } from './fanHost';
import { fanFeatureEnabled, fanControlActive, getFanSelectedPreset, normalizeFanPresetCurves, saveFanPresetSelection, setFanControlActive, setFanNavigationDuty, recordFanHandshake, type FanPreset } from './fanFeature';
import { observeFanMirrorDisplay, isFanMirrorUiBusy } from './deckyFanDisplay';

const SHORTCUT_RECEIPT_TIMEOUT_MS = 3000;
const fanLabels:Record<FanPreset,string>={soft:'轻柔转速',balanced:'均衡转速',aggressive:'暴力转速'};
/** Main renderer only. Mounting registers listeners; it never changes a saved setting. */
export function startControllerShortcutRuntime() {
  let ended=false;
  let fanSource:Record<string,unknown>={};
  const fanActions=createFanMirrorActions({
    owner:fanHostLifecycle,enabled:()=>fanControlActive.value,currentPreset:getFanSelectedPreset,
    curve:preset=>normalizeFanPresetCurves(fanSource.presetCurves,fanSource.nodes,
      ['soft','balanced','aggressive'].includes(String(fanSource.preset))?fanSource.preset as FanPreset:'balanced')[preset],
    save:(_nodes,preset)=>saveFanPresetSelection(preset,fanSource),
    updateActive:setFanControlActive,updateDuty:setFanNavigationDuty,power:()=>powerLifecycle.get(),
    featureAllowed:()=>!ended&&fanFeatureEnabled.value,uiBusy:isFanMirrorUiBusy,display:observeFanMirrorDisplay,
    observeCapability:allowed=>recordFanHandshake(allowed),
  });
  const ready=async(context:ShortcutExecutionContext)=>{
    await context.verify();const power=await powerLifecycle.get();context.checkpoint();
    if(power.generation!==context.request.powerGeneration||power.phase!=='ready'||!power.hardwareWritesAllowed)throw Error('SHORTCUT_POWER_CHANGED');
  };
  const locked=async(context:ShortcutExecutionContext,run:()=>Promise<ShortcutActionOutcome>)=>{
    const release=tryAcquireQuickAction('controller-shortcut:'+context.request.actionId);
    if(!release)throw Error('SHORTCUT_BUSY');
    try{await ready(context);return await run()}finally{release()}
  };
  const actions=createControllerShortcutActions({
    capture: request => {
      if (request.actionId !== 'display.frameRateControl') return () => true;
      const identity = () => { const game=getPolicyGame(); return game ? `${game.pid}:${game.processCreated}:${gamePolicyKey(game)}` : ''; };
      const original=identity(),side=powerSourceMode.value;
      return () => identity()===original && (side===null || powerSourceMode.value===side);
    },
    claim:async request=>(await invoke<{ok:boolean}>('input.shortcutAction.claim',{id:request.id},{timeoutMs:SHORTCUT_RECEIPT_TIMEOUT_MS})).ok===true,
    complete:async(request,outcome)=>{await invoke('input.shortcutAction.complete',{id:request.id,outcome},{timeoutMs:SHORTCUT_RECEIPT_TIMEOUT_MS});},
    execute:{
      'os.switchProgram':(_params,context)=>locked(context,async()=>{
        if(!await shell.taskView())throw Error('SHORTCUT_TASK_VIEW_FAILED');
        context.checkpoint();const state=await windowApi.getState();context.checkpoint();
        if(state.visible&&!state.minimized&&!await windowApi.minimize())return {state:'failed',title:'切换程序',message:'已请求切换程序，但 YMCC 未能隐藏。'};
        return {state:'applied',title:'切换程序',message:'已请求打开任务视图。'};
      }),
      'monitor.toggle':(params,context)=>locked(context,async()=>{
        await context.verify();
        const selected=await updateMonitorOutputEnabled(params.operation as 'toggle'|'show'|'hide');context.checkpoint();
        await ready(context);const result=await monitorOutputCoordinator.apply(selected);context.checkpoint();
        return {state:result.ok?'applied':'saved',title:'监控显示',
          message:result.ok?(selected.enabled?'已显示监控。':'已隐藏监控。'):'监控显示选择已保存，输出尚未确认。',
          value:{text:selected.enabled?'显示':'隐藏'}};
      }),
      'display.frameRateControl':(params,context)=>locked(context,async()=>{
        const effective=await effectiveFrameRates();context.checkpoint();
        const current=await getContinuousPerformanceSnapshot(effective.dedicated?'dedicated':'global');context.checkpoint();
        if (effective.identity !== (current.identity.pid ? `${current.identity.pid}:${current.identity.processCreated}` : '')) throw Error('SHORTCUT_SOURCE_CHANGED');
        if(!current.side||current.actualSide!==current.side)throw Error('SHORTCUT_POWER_CHANGED');
        const previous=current.frameRates[current.side],target=Number(params.fps);
        const next=frameRateSetting({...previous,fps:params.operation==='toggle'&&previous.fps>0?0:target,lastFps:previous.fps>0?previous.fps:previous.lastFps},previous);
        await ready(context);const result=await updateContinuousPerformanceControls(current.identity,{frameRate:next});context.checkpoint();
        const fps=result.frameRates[current.side].fps,applied=result.frameRateApplied===true;
        return {state:applied?'applied':'saved',title:'锁帧控制',
          message:applied?(fps?'已设置 '+fps+' FPS 锁帧。':'已关闭锁帧。'):'锁帧选择已保存，应用尚未确认。',
          value:{text:fps?fps+' FPS':'关闭',number:fps,unit:'FPS'}};
      }),
      'fan.switchPreset':(params,context)=>locked(context,async()=>{
        if(!fanFeatureEnabled.value)throw Error('SHORTCUT_FAN_NOT_READY');
        fanSource=await readSettingsSection('fan');context.checkpoint();
        const presets:FanPreset[]=['soft','balanced','aggressive'];
        const current=presets.includes(fanSource.preset as FanPreset)?fanSource.preset as FanPreset:'balanced';
        const preset=params.preset==='next'?presets[(presets.indexOf(current)+1)%presets.length]:params.preset as FanPreset;
        await ready(context);
        const result=await fanActions.execute('fan.setPreset',{preset},{generation:context.request.powerGeneration,checkpoint:context.checkpoint});context.checkpoint();
        return {state:result.applied?'applied':result.pending?'pending':'saved',title:'风扇预设',
          message:result.applied?'已切换为'+fanLabels[preset]+'。':result.pending?'已选择'+fanLabels[preset]+'，等待风扇确认。':'已保存'+fanLabels[preset]+'；风扇控制状态保留。',
          value:{text:fanLabels[preset]}};
      }),
      'app.launchQuickApp':(params,context)=>locked(context,async()=>{
        const saved=await readSettingsSection('quickApps');context.checkpoint();
        const apps=Array.isArray(saved.apps)?saved.apps:[];
        const app=apps.find(item=>item&&typeof item==='object'&&item.path===params.appPath);
        if(!app)throw Error(apps.length?'SHORTCUT_APP_REMOVED':'SHORTCUT_NO_APPS');
        await ready(context);if(!await shell.execute(params.appPath,[]))throw Error('SHORTCUT_APP_START_FAILED');context.checkpoint();
        const name=typeof app.name==='string'&&app.name?app.name.slice(0,64):'快捷应用';
        return {state:'applied',title:'快捷应用',message:'已请求启动 '+name+'。',value:{text:name}};
      }),
    },
  });
  const stops=[on('input.shortcutAction',actions.enqueue),on('power.pending',()=>actions.invalidate()),on('power.suspending',()=>actions.invalidate()),on('power.resumed',()=>actions.invalidate())];
  return {stop(){ended=true;actions.stop();fanActions.stop();for(const stop of stops)stop()},idle:actions.idle};
}
