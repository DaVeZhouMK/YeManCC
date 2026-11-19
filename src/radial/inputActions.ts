import { createGlobalMirrorActions, type GlobalMirrorRead } from '@/bridge/deckyGlobalActions';
import type { GameMirrorDeps, GameMirrorGyroDefaults } from '@/bridge/deckyGameActions';
import type { GameCustomProfile, PerformanceScheduleConfig } from '@/bridge/performanceSchedule';
import type { DetectedGame } from '@/bridge/gamedetect';
import type { MirrorMutationContext } from '@/bridge/deckyFanActions';
import type { VirtualTargetPolicy } from '@/bridge/virtualTargetPolicy';
import { RADIAL_INPUT_OPTIONS, emptyRadialInput, inputFieldsFor, type RadialInputFrame, type RadialInputGroup, type RadialInputIntent, type RadialInputField } from './input';
export type RadialInputRead={policy:VirtualTargetPolicy|null;locked:boolean;busy:boolean}&(
 {scope:'global';global:GlobalMirrorRead}|{scope:'game';game:DetectedGame;entry:GameCustomProfile|undefined;schedule:PerformanceScheduleConfig;gyro:GameMirrorGyroDefaults;label:string});
export interface RadialInputDeps {read:()=>Promise<RadialInputRead>;global:Parameters<typeof createGlobalMirrorActions>[0];game?:GameMirrorDeps;nextRevision:()=>string;}
export function createRadialInputActions(deps: RadialInputDeps) {
 const global = createGlobalMirrorActions({ ...deps.global, contextCurrent: deps.global.contextCurrent ?? (() => true) });
 let stopped = false, applying = false, fingerprint = '', frame = emptyRadialInput();
 async function read(checkpoint: () => void = () => {}) {
  checkpoint(); if (stopped) throw Error('轮盘输入会话已结束');
  const raw = await deps.read(); checkpoint();
  // Older readers may still include game metadata. It never selects an input owner.
  const source = raw.scope === 'global' ? raw.global : await deps.global.read(); checkpoint();
  const fields = source.fields, available = !raw.locked, busy = raw.busy;
  const controls = (['padPersona', 'gyroEnabled', 'gyroPreset'] as RadialInputField[]).map(key => {
   const field = fields[key];
   return { key, value: String(field?.value ?? ''), enabled: available && !busy && field?.supported === true && (key !== 'gyroPreset' || fields.gyroEnabled?.value === 'on'),
    choices: RADIAL_INPUT_OPTIONS[key].map(option => { const actual = field?.choices.find(choice => choice.data === option.data); return { value: option.data, label: actual?.label ?? option.label, enabled: !!actual && !actual.disabled }; }) };
  });
  const pending = source.input.applyStatus !== 'active';
  const value: RadialInputFrame = { sourceRevision: '', scope: 'global', label: '全局输入', available, busy, pending, controls,
   notice: !available ? '当前输入操作尚未完成，请稍后重试' : busy ? '请先完成当前输入操作' : pending ? '全局输入已保存；等待输入服务确认' : '全局输入状态已由 YMCC 输入服务确认' };
  const next = JSON.stringify([source.source, raw.policy, raw.locked, value]);
  if (next !== fingerprint) { fingerprint = next; frame = { ...value, sourceRevision: deps.nextRevision() }; }
  return { frame: { ...frame, controls: frame.controls.map(control => ({ ...control, choices: control.choices.map(choice => ({ ...choice })) })) }, source };
 }
 return {
  async read(checkpoint?: () => void) { return (await read(checkpoint)).frame; },
  async apply(group: RadialInputGroup, intent: RadialInputIntent, context: MirrorMutationContext) {
   if (stopped || applying) throw Error('输入操作正在处理，请稍后重试');
   if (!intent || !inputFieldsFor(group).includes(intent.field) || typeof intent.value !== 'string' || typeof intent.sourceRevision !== 'string') throw Error('输入选项请求无效');
   applying = true;
   try {
    const current = await read(context.checkpoint), control = current.frame.controls.find(control => control.key === intent.field);
    if (!current.frame.available || current.frame.busy) throw Error(current.frame.notice);
    if (!intent.sourceRevision || intent.sourceRevision !== current.frame.sourceRevision) throw Error('设备或输入设置已变化，请重新选择');
    if (!control?.enabled || !control.choices.some(choice => choice.value === intent.value && choice.enabled)) throw Error('当前输入选项不可用');
    const relayContext = { ...context, revision: 0, gameIdentity: '', controlAdmission: { global: current.source.source } };
    const outcome = await global.execute({ field: intent.field, value: intent.value }, relayContext);
    const refreshed = await read(context.checkpoint); return { ...outcome, frame: refreshed.frame };
   } finally { applying = false; }
  },
  stop() { stopped = true; },
 };
}
