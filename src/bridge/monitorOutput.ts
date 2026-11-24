import { admitHudDragPlacement, type HudDragPlacement } from './monitorHudDrag';
import { deckySidebar } from './deckySidebar';
import { readSettingsSection, saveSettingsSection, updateSettings, loadSettings, cachedBootStartupReceiptOwner, type UnifiedSettings } from './settingsRepository';
import { captureBootPreferenceWrite, assertBootPreferenceOwner, bootStartupReceiptOwner, projectBootPreferenceExtensions, type BootStartupReceiptOwner, type BootPreferenceWrite } from './bootModeGlobalPreferences';
import { normalizeBootStartupResult } from './bootModeModel';
import { equalSettingsDocuments } from './settingsBaseline';
import { steamSettings, type SteamMonitorState } from './steamSettings';
import { rtssRunning, toggleRtss, readOverlayLayout, setOverlayLayout, readRtssZoom } from './yeman';
import { createMonitorOutputCoordinator, type AppliedMonitorOutput, type OutputAck, type MonitorOutputCurrent } from './monitorOutputCoordinator';
import { normalizeMonitorOutput, switchMonitorHudTemplate, type HudTemplateId, selectableMonitorOutput, type RtssMonitorTemplate, type MonitorOutputPreferences } from './monitorHudModel';
export interface YmccHudOutputAdapter {
  readonly global?:boolean;
  globalFor?(preferences:MonitorOutputPreferences):Promise<boolean>;
  available(preferences?: MonitorOutputPreferences): Promise<boolean>;
  visible(): Promise<boolean>;
  state?():Promise<'shown'|'armed'|'hidden'|'unknown'>;
  setVisible(visible: boolean, preferences: MonitorOutputPreferences, current?:MonitorOutputCurrent): Promise<OutputAck>;
}
// Product relay can register ONE adapter after its game-bound carrier is ready.
// The test-only __YMCC_STEAM_HUD_PROBE__ is intentionally never used as a product API.
let adapter: YmccHudOutputAdapter | null = null;
export interface MonitorOutputIntent {readonly id:number;readonly preferences:MonitorOutputPreferences;readonly write:BootPreferenceWrite;readonly recovery?:boolean;readonly startupReceiptOwner?:BootStartupReceiptOwner;}
let nextIntentId=0;
const copyMonitorIntent=(intent:MonitorOutputIntent):MonitorOutputIntent=>({id:intent.id,preferences:structuredClone(intent.preferences),write:captureBootPreferenceWrite(intent.write),...(intent.recovery?{recovery:true}:{}),...(intent.startupReceiptOwner?{startupReceiptOwner:{...intent.startupReceiptOwner}}:{})});
const preferenceListeners=new Set<(prefs:MonitorOutputPreferences,write?:BootPreferenceWrite)=>void>();
const applyingListeners=new Set<(applying:boolean,intent?:MonitorOutputIntent)=>void>();
let outputApplyCount=0;
export function onMonitorOutputApplying(listener:(applying:boolean,intent?:MonitorOutputIntent)=>void):()=>void {
  applyingListeners.add(listener);listener(outputApplyCount>0);return()=>{applyingListeners.delete(listener);};
}
function publishApplying(intent?:MonitorOutputIntent){for(const listener of applyingListeners){try{listener(outputApplyCount>0,intent?copyMonitorIntent(intent):undefined);}catch{}}}
const applyListeners=new Set<(prefs:MonitorOutputPreferences,result:{ok:boolean;applied:AppliedMonitorOutput},intent?:MonitorOutputIntent)=>void>();
export function onMonitorOutputApplied(listener:(prefs:MonitorOutputPreferences,result:{ok:boolean;applied:AppliedMonitorOutput},intent?:MonitorOutputIntent)=>void):()=>void {
  applyListeners.add(listener);return()=>{applyListeners.delete(listener);};
}
let hudAvailable=false;
const availabilityListeners=new Set<(available:boolean)=>void>();
export function publishMonitorHudAvailability(available:boolean):void {
  if(available===hudAvailable)return;hudAvailable=available;
  for(const listener of availabilityListeners){try{listener(available);}catch{}}
}
export function onMonitorHudAvailability(listener:(available:boolean)=>void):()=>void {
  availabilityListeners.add(listener);listener(hudAvailable);return()=>{availabilityListeners.delete(listener);};
}
export function onMonitorOutputPreferencesChanged(listener:(prefs:MonitorOutputPreferences,write?:BootPreferenceWrite)=>void):()=>void {
  preferenceListeners.add(listener);return()=>{preferenceListeners.delete(listener);};
}
export function registerYmccHudOutputAdapter(next: YmccHudOutputAdapter): () => void {
  if (adapter) throw Error('YMCC HUD output already has an owner');
  adapter = next; return () => { if (adapter === next) adapter = null; };
}
// Only the caller's current display transaction may share this read. Actions
// omit it and obtain their original fresh preferences before applying.
export async function readMonitorOutputPreferences(readRtss:()=>Promise<UnifiedSettings['rtss']>=()=>readSettingsSection('rtss')): Promise<MonitorOutputPreferences> {
  const section = await readRtss(); return selectableMonitorOutput(section.monitorOutput);
}
function mergeMonitorPreferences(previous:unknown,next:MonitorOutputPreferences){
  const raw=previous&&typeof previous==='object'&&!Array.isArray(previous)?previous as Record<string,unknown>:{};
  const child=raw.ymcc&&typeof raw.ymcc==='object'&&!Array.isArray(raw.ymcc)?raw.ymcc as Record<string,unknown>:{};
  const ymcc={...child,...next.ymcc};
  if(next.ymcc.dragX===undefined||next.ymcc.dragY===undefined){delete ymcc.dragX;delete ymcc.dragY;}
  return {...raw,...next,ymcc};
}
export async function saveMonitorOutputPreferences(prefs: MonitorOutputPreferences, options: BootPreferenceWrite = {}): Promise<void> {
  const submitted=captureBootPreferenceWrite(options);
  // Existing repository merges this nested display-only section. Frame limits,
  // startup choices, RTSS files and Steam account settings are not overwritten.
  let normalized=normalizeMonitorOutput(prefs);
  await updateSettings(settings=>{
    const saved=normalizeMonitorOutput(settings.rtss.monitorOutput);
    // Save only the edited bank; queued work must not restore an old active
    // template or overwrite edits to the other bank.
    const edited=normalized.ymccTemplate??'A';
    const banks={...saved.ymccTemplates,[edited]:structuredClone(normalized.ymcc)};
    // A reset remains authoritative even after selecting the other bank.
    if((saved.ymccTemplateResets?.[edited]??0)>(normalized.ymccTemplateResets?.[edited]??0))banks[edited]=structuredClone(saved.ymccTemplates![edited]!);
    const resets:Partial<Record<HudTemplateId,number>>={};
    for(const bank of ['A','B'] as const){const value=Math.max(saved.ymccTemplateResets?.[bank]??0,normalized.ymccTemplateResets?.[bank]??0);if(value>0)resets[bank]=value;}
    if(Object.keys(resets).length)normalized={...normalized,ymccTemplateResets:resets};
    if((saved.ymccTemplateRevision??0)>(normalized.ymccTemplateRevision??0)){
      const selected=saved.ymccTemplate??'A';
      normalized={...normalized,ymccTemplate:selected,ymccTemplateRevision:saved.ymccTemplateRevision,ymcc:banks[selected]!,ymccTemplates:banks};
    }else normalized={...normalized,ymcc:banks[edited]!,ymccTemplates:banks};
    // A queued slider edit cannot move the HUD back over a newer native drag.
    if((saved.ymcc.dragRevision??0)>(normalized.ymcc.dragRevision??0)){
      const p=admitHudDragPlacement(saved.ymcc);if(p)normalized={...normalized,ymcc:{...normalized.ymcc,...p}};
    }
    normalized=normalizeMonitorOutput(normalized);
    settings.rtss.monitorOutput=mergeMonitorPreferences(settings.rtss.monitorOutput,normalized);
  }, submitted);
  if(submitted.bootContext)assertBootPreferenceOwner(await loadSettings(),submitted);
  for(const listener of preferenceListeners){try{listener(normalized,captureBootPreferenceWrite(submitted));}catch{}}
}
export async function saveMonitorHudPlacement(raw:HudDragPlacement):Promise<void>{
  const p=admitHudDragPlacement(raw);if(!p||!p.dragRevision)return;
  let normalized:MonitorOutputPreferences|null=null;
  await updateSettings(settings=>{
    const saved=normalizeMonitorOutput(settings.rtss.monitorOutput);
    if(p.dragRevision<=(saved.ymcc.dragRevision??0))return;
    normalized={...saved,ymcc:{...saved.ymcc,...p}};settings.rtss.monitorOutput=mergeMonitorPreferences(settings.rtss.monitorOutput,normalized);
  });
  if(normalized)for(const listener of preferenceListeners){try{listener(normalized,{origin:'ordinary'});}catch{}}
}
export async function saveMonitorHudTemplate(template:HudTemplateId,revision:number,admit?:(current:MonitorOutputPreferences)=>boolean):Promise<void>{
  if((template!=='A'&&template!=='B')||!Number.isSafeInteger(revision)||revision<1)return;
  let normalized:MonitorOutputPreferences|null=null;
  await updateSettings(settings=>{
    const saved=normalizeMonitorOutput(settings.rtss.monitorOutput);
    if(admit&&!admit(saved))throw Error('MONITOR_TEMPLATE_CONTEXT_CHANGED');
    if(revision<=(saved.ymccTemplateRevision??0))return;
    normalized=switchMonitorHudTemplate(saved,template,revision);
    settings.rtss.monitorOutput=mergeMonitorPreferences(settings.rtss.monitorOutput,normalized);
  });
  if(normalized)for(const listener of preferenceListeners){try{listener(normalized,{origin:'ordinary'});}catch{}}
}
export async function readMonitorOutputRuntime(preferences?: MonitorOutputPreferences): Promise<{ applied: AppliedMonitorOutput; backend: boolean; ymccAvailable: boolean; rtssScale: number; rtssTemplate: RtssMonitorTemplate | null; steam: SteamMonitorState }> {
  const prefs=preferences ? normalizeMonitorOutput(preferences) : await readMonitorOutputPreferences();
  const globalScope=prefs.output==='ymcc'&&(adapter?.globalFor?await adapter.globalFor(prefs):adapter?.global===true);
  const [steam, layout, backend, zoom, ymccAvailable] = await Promise.all([
    globalScope?Promise.resolve({ok:false,available:false,pending:false,liveConfirmed:false,reason:'global-desktop-scope',position:0,detail:1,scale:1,saturation:1,opacity:1} as SteamMonitorState):steamSettings.get<SteamMonitorState>('monitor'), readOverlayLayout(), rtssRunning(), readRtssZoom(), adapter?.available(prefs) ?? false,
  ]);
  const visibility=adapter?.state?await adapter.state():adapter&&await adapter.visible()?'shown':'hidden';
  const visible=visibility==='shown';
  const rtssTemplate: RtssMonitorTemplate | null = layout === 'YeManOBS-W-1.ovl' ? 'W' : layout === 'YeManOBS-L-1.ovl' ? 'L' : layout === 'YeManOBS-JJ-1.ovl' ? 'J' : null;
  const outputs: AppliedMonitorOutput[] = [];
  const session=globalScope?null:await deckySidebar.get().catch(()=>null);
  const steamAbsent=session?.steamKnown===true&&session.steamRunning===false;
  if (visibility==='unknown'||!globalScope&&!steamAbsent&&(!steam.available || !steam.ok || steam.via!=='live' || steam.liveConfirmed!==true)) return { applied: 'unknown', backend, ymccAvailable, rtssScale: zoom * 20, rtssTemplate, steam };
  if (!globalScope&&!steamAbsent&&steam.position !== 0) outputs.push('steam');
  if (!globalScope && backend && ['YeManOBS-W-1.ovl', 'YeManOBS-L-1.ovl', 'YeManOBS-JJ-1.ovl'].includes(layout)) outputs.push('rtss');
  if (visible || visibility === 'armed') outputs.push('ymcc');
  return { applied: outputs.length === 1 ? outputs[0] : outputs.length ? 'unknown' : 'off', backend, ymccAvailable, rtssScale: zoom * 20, rtssTemplate, steam };
}
async function requireMonitorCurrent(current?:MonitorOutputCurrent){if(current&&!await current())throw Error('MONITOR_OUTPUT_REQUEST_EXPIRED');}
const outputCoordinator = createMonitorOutputCoordinator({
  ownsYmcc:()=>adapter!==null,
  async capabilities(prefs) {
    if(prefs.output==='ymcc'&&adapter&&(adapter.globalFor?await adapter.globalFor(prefs):adapter.global===true))return {steam:false,ymccGlobal:true,ymccOwned:true,ymcc:await adapter.available(prefs)};
    const [state,session]=await Promise.all([steamSettings.get<SteamMonitorState>('monitor').catch(()=>null),deckySidebar.get().catch(()=>null)]);
    const steamAbsent=session?.steamKnown===true&&session.steamRunning===false;
    return { steamAbsent,ymccOwned:!!adapter,steam: state?.ok === true && state.available === true && state.via === 'live' && state.liveConfirmed === true, ymcc: await (adapter?.available(prefs) ?? false) };
  },
  async ensureRtss(current) {
    const running=await rtssRunning();if(current)await requireMonitorCurrent(current);
    if(!running){await toggleRtss(true,current);if(current)await requireMonitorCurrent(current);}
    const confirmed=await rtssRunning();if(current)await requireMonitorCurrent(current);
    if(!confirmed)throw Error('RTSS 后台未运行');
  },
  async setSteam(position,current) {
    if(current)await requireMonitorCurrent(current);
    const state = await steamSettings.applyMonitor<SteamMonitorState>({ position },current?.bootContext?{origin:'boot-mode',bootContext:current.bootContext}:{});
    if(current)await requireMonitorCurrent(current);
    return { confirmed: state.ok === true && state.available === true && !state.pending && state.applicationConfirmed === true && state.via === 'live' && state.liveConfirmed === true && state.position === position,
      reason: state.pending ? 'Steam 设置仍排队，需先确认实时应用；未启用另一路。' : 'Steam 未回执实时目标位置。' };
  },
  async setRtss(show, prefs,current) {
    if(current)await requireMonitorCurrent(current);
    const layout = show ? prefs.rtssTemplate : 'off'; const state = await setOverlayLayout(layout, show ? Math.round(prefs.rtssScale / 20) : undefined,current);
    if(current)await requireMonitorCurrent(current);
    const target = show ? ({ W: 'YeManOBS-W-1.ovl', L: 'YeManOBS-L-1.ovl', J: 'YeManOBS-JJ-1.ovl' })[prefs.rtssTemplate] : 'Empty.ovl';
    // A stopped RTSS cannot draw; its OFF setting was saved and checked for
    // the next start. Showing an output still requires a live template ACK.
    const loaded = state.mode === 'live' ? await readOverlayLayout() : undefined;
    if(current)await requireMonitorCurrent(current);
    return { confirmed: state.mode === 'live' ? loaded === target : !show, reason: 'RTSS 模板加载/读回未确认；未继续开启目标。' };
  },
  async setYmcc(show, prefs,current) {
    if(current)await requireMonitorCurrent(current);
    const result=adapter ? await adapter.setVisible(show, prefs,current) : { confirmed: false, reason: 'YMCC 常驻输出适配器不可用' };
    if(current)await requireMonitorCurrent(current);return result;
  },
});
export const monitorOutputCoordinator={async apply(prefs:MonitorOutputPreferences,options:BootPreferenceWrite={},recoveryCurrent?:()=>boolean|Promise<boolean>){
  let submitted=captureBootPreferenceWrite(options);
  const originalBoot=submitted.bootContext;
  const startupReceiptOwner=originalBoot?bootStartupReceiptOwner(originalBoot):cachedBootStartupReceiptOwner();
  const value=normalizeMonitorOutput(prefs);
  const bootHudRecovery=!!(originalBoot&&recoveryCurrent&&value.enabled&&value.output==='ymcc');
  const savedMonitorMatches=(settings:UnifiedSettings)=>equalSettingsDocuments(normalizeMonitorOutput(settings.rtss.monitorOutput),value);
  if(originalBoot&&recoveryCurrent&&bootHudRecovery){
    if(!await recoveryCurrent())throw Error('MONITOR_OUTPUT_REQUEST_EXPIRED');
    const settings=await loadSettings();
    if(!savedMonitorMatches(settings))throw Error('MONITOR_OUTPUT_REQUEST_EXPIRED');
    try{assertBootPreferenceOwner(settings,submitted);}
    catch(error){
      // The same startup publishes its completed snapshot and provisional
      // receipt after the first attempt. Rebind only to that durable claim.
      const boot=startupReceiptOwner?.boot,owner=bootStartupReceiptOwner(settings);
      const prior=normalizeBootStartupResult(originalBoot.extensions.bootModesStartupResult,boot);
      const receipt=normalizeBootStartupResult(settings.extensions?.bootModesStartupResult,boot);
      const mode=settings.extensions?.bootModes as {schema?:number;revision?:number;current?:string}|undefined;
      const monitor=receipt?.modules?.monitor;
      if(!startupReceiptOwner?.writeId||!owner||owner.boot!==boot||owner.writeId!==startupReceiptOwner.writeId||
         settings.extensions?.bootModesActiveBoot!==boot||!prior||prior.status==='failed'||!receipt||receipt.status==='failed'||
         receipt.mode!==prior.mode||receipt.sourceRevision!==prior.sourceRevision||receipt.revision<prior.revision||
         mode?.schema!==1||mode.current!==receipt.mode||!Number.isSafeInteger(mode.revision)||Number(mode.revision)<receipt.revision||
         !monitor?.requested||!['waiting','requested','failed','active'].includes(monitor.state)||
         !equalSettingsDocuments(normalizeMonitorOutput(monitor.requested),value))throw error;
      submitted=captureBootPreferenceWrite({origin:'boot-mode',bootContext:{input:settings.input,extensions:projectBootPreferenceExtensions(settings.extensions)}});
    }
  }
  const current:MonitorOutputCurrent|undefined=submitted.bootContext||recoveryCurrent?async()=>{
    if(recoveryCurrent&&!await recoveryCurrent())return false;
    if(submitted.bootContext){
      const settings=await loadSettings();
      assertBootPreferenceOwner(settings,submitted);
      if(bootHudRecovery&&!savedMonitorMatches(settings))return false;
    }
    return !recoveryCurrent||await recoveryCurrent();
  }:undefined;
  if(current&&submitted.bootContext)Object.defineProperty(current,'bootContext',{value:submitted.bootContext,enumerable:false});
  if(current)await requireMonitorCurrent(current);
  const intent:MonitorOutputIntent={id:++nextIntentId,preferences:structuredClone(value),write:captureBootPreferenceWrite(submitted),...(recoveryCurrent?{recovery:true}:{}),...(startupReceiptOwner?{startupReceiptOwner}:{})};++outputApplyCount;publishApplying(intent);
  try{
    const result=await outputCoordinator.apply(value,current);
    if(current)await requireMonitorCurrent(current);
    for(const listener of applyListeners){try{listener(value,result,copyMonitorIntent(intent));}catch{}}
    return result;
  }finally{--outputApplyCount;publishApplying();}
}};

/** Atomic enabled-only shortcut edit; retain the current output, appearance and unknown fields. */
export async function updateMonitorOutputEnabled(operation: 'toggle' | 'show' | 'hide'): Promise<MonitorOutputPreferences> {
  let selected: MonitorOutputPreferences | null = null;
  await updateSettings(document => {
    const raw = document.rtss.monitorOutput;
    const previous = selectableMonitorOutput(raw);
    const enabled = operation === 'toggle' ? !previous.enabled : operation === 'show';
    const retained = raw && typeof raw === 'object' && !Array.isArray(raw) ? raw : {};
    document.rtss = { ...document.rtss, monitorOutput: { ...retained, enabled } };
    selected = selectableMonitorOutput(document.rtss.monitorOutput);
  });
  const value = selected as MonitorOutputPreferences | null;
  if (!value) throw new Error('SHORTCUT_SOURCE_CHANGED');
  for (const listener of preferenceListeners) { try { listener(structuredClone(value),{origin:'ordinary'}); } catch { /* observer isolation */ } }
  return value;
}
