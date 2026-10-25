/** Adapter for the isolated simulator. Only explicit Save reaches shared production APIs. */
import { getContinuousPerformanceSnapshot, updateContinuousPerformanceControls } from './performanceSchedule';
import { patchContinuousBank, toPowerCoreMode, type ContinuousPerformanceSnapshot, type ContinuousSidePatch } from './performanceControls';
import { getPolicyGame } from './gamePolicyTarget';
const coreIds = { bigFirst: 0, bigOnly: 1, littleFirst: 3, littleOnly: 2 } as const;
const fromId = ['bigFirst', 'bigOnly', 'littleOnly', 'littleFirst'] as const;
const fromKey = { big: 'bigFirst', 'only-big': 'bigOnly', small: 'littleFirst', 'only-small': 'littleOnly' } as const;
export function simulationSourceSnapshot(source: ContinuousPerformanceSnapshot, revision: number): Record<string, any> {
  const side = source.side;
  if (!side || !source.bank) throw new Error('正在确认供电状态，请稍后进入模拟测试');
  const game = getPolicyGame(), a = source.controls.ac, d = source.controls.dc;
  const rule = (v: typeof a) => ({ splitWatts: v.auto.coreSplit, lowerMode: toPowerCoreMode(v.auto.coreLow), upperMode: toPowerCoreMode(v.auto.coreHigh) });
  const manual = (v: typeof a) => ({ freq: v.manual.frequencyMHz / 1000, aggr: v.manual.aggressiveness, turbo: v.manual.turbo, unlimited: v.manual.unlimited, minOverride: null, core: coreIds[v.manual.coreMode] });
  return { schema: 'ymcc-performance-prototype-v29', exampleOnly: true, fixture: 'unknown', mode: source.bank.mode, gameMode: source.bank.mode,
    gameThreads: source.identity.exe ? { [source.identity.exe]: { hyperThreadMode: source.hyperThreadMode === 'off' ? 'off' : 'on' } } : {},
    machine: { maxTdp: Math.min(150, Math.max(a.tdpCeiling, d.tdpCeiling)) }, simulationProfile: 'z2e',
    auto: { side, floatSearchMode: 'baseline', coreRules: { ac: rule(a), dc: rule(d) }, bias: { ac: a.auto.bias, dc: d.auto.bias },
      energySaving: { ac: a.auto.saving, dc: d.auto.saving }, tdp: { ac: a.tdp, dc: d.tdp } },
    manual: { side, controls: { ac: manual(a), dc: manual(d) } }, frames: source.frameRates,
    syncRevision: revision, power: { actualSide: side },
    previewRuntime: { topGame: game ? { name: game.name, path: game.path || game.name, pid: game.pid, processCreated: String(game.processCreated) } : null,
      coreTopologyScenario: source.hybrid ? 'hybrid2' : 'uniform' } };
}
export function simulationDraftPatch(base: Record<string, any>, draft: Record<string, any>, source: ContinuousPerformanceSnapshot): ContinuousSidePatch {
  const side = source.side;
  if (!side || draft?.schema !== 'ymcc-performance-prototype-v29' || draft.auto?.side !== side || draft.manual?.side !== side) throw new Error('供电侧或模拟参数已变化，请恢复当前设置');
  const m = draft.manual.controls[side], a = draft.auto, rule = a.coreRules[side], frames = draft.frames[side];
  const finite = (v: unknown) => { if (typeof v !== 'number' || !Number.isFinite(v)) throw new Error('无效的模拟参数'); return v; };
  const coreLow = fromKey[rule.lowerMode as keyof typeof fromKey], coreHigh = fromKey[rule.upperMode as keyof typeof fromKey];
  if (!coreLow || !coreHigh || !Number.isInteger(m.core) || !fromId[m.core]) throw new Error('无效的核心方案');
  if (typeof m.turbo !== 'boolean' || typeof m.unlimited !== 'boolean') throw new Error('无效的 CPU 开关');
  const patch: ContinuousSidePatch = { mode: draft.mode, tdp: finite(a.tdp[side]),
    auto: { bias: finite(a.bias[side]), saving: a.energySaving[side], coreSplit: finite(rule.splitWatts), coreLow, coreHigh },
    manual: { frequencyMHz: finite(m.freq) * 1000, aggressiveness: finite(m.aggr), turbo: m.turbo, unlimited: m.unlimited, coreMode: fromId[m.core] },
    frameRate: { fps: finite(frames.fps), ceiling: finite(frames.ceiling), lastFps: finite(frames.lastFps) } };
  // A shared draft envelope must not change an untouched per-side remembered ceiling.
  if (draft.machine.maxTdp !== base.machine.maxTdp) patch.tdpCeiling = finite(draft.machine.maxTdp);
  const before = base.gameThreads?.[source.identity.exe]?.hyperThreadMode;
  const after = draft.gameThreads?.[source.identity.exe]?.hyperThreadMode;
  if (after !== before) {
    if (!source.smtAvailable || !['on', 'off'].includes(after)) throw new Error('当前游戏不支持超线程策略');
    patch.hyperThreadMode = after;
  }
  return patch;
}
/** Project the same cached draft back into the production editor's typed view. */
export function simulationPanelSnapshot(base: Record<string, any>, draft: Record<string, any>, source: ContinuousPerformanceSnapshot): ContinuousPerformanceSnapshot {
  const patch = simulationDraftPatch(base, draft, source), side = source.side!;
  const value = structuredClone(source);
  value.controls[side] = patchContinuousBank(value.controls[side], patch, side);
  value.bank = value.controls[side];
  value.actualSide = draft.power?.actualSide ?? source.actualSide;
  value.frameRates = structuredClone(draft.frames);
  value.hyperThreadMode = draft.gameThreads?.[source.identity.exe]?.hyperThreadMode ?? source.hyperThreadMode;
  return value;
}
/** Translate typed production controls into the existing isolated draft command protocol. */
export function simulationPanelCommands(patch: ContinuousSidePatch, current: ContinuousPerformanceSnapshot): {key: string; value: any}[] {
  if (!current.side || !current.bank) throw new Error('模拟参数尚未就绪');
  const bank = patchContinuousBank(current.bank, patch, current.side), out: {key:string;value:any}[] = [];
  const add = (key: string, value: any) => out.push({key,value});
  if (patch.tdpCeiling !== undefined) add('machineMax',bank.tdpCeiling);
  if (patch.tdp !== undefined) add('simulationBudget',bank.tdp);
  if (patch.frameRate) { add('ceiling',patch.frameRate.ceiling); add('target',patch.frameRate.fps); }
  for (const [field,key] of [['bias','bias'],['saving','saving'],['coreSplit','autoCoreThreshold'],['coreLow','autoCoreLow'],['coreHigh','autoCoreHigh']] as const)
    if (patch.auto?.[field] !== undefined) add(key,bank.auto[field]);
  if (patch.manual?.frequencyMHz !== undefined || patch.manual?.unlimited !== undefined) add('manualMHz',bank.manual.unlimited?0:bank.manual.frequencyMHz);
  if (patch.manual?.aggressiveness !== undefined) add('manualAggressiveness',bank.manual.aggressiveness);
  if (patch.manual?.turbo !== undefined) add('manualTurbo',bank.manual.turbo);
  if (patch.manual?.coreMode !== undefined) add('manualCoreMode',bank.manual.coreMode);
  if (patch.hyperThreadMode !== undefined) add('manualSmt',patch.hyperThreadMode !== 'off');
  if (patch.mode !== undefined) add('mode',bank.mode);
  return out;
}
export function createPerformanceSimulationSession() {
  let revision = 0;
  const bases = new Map<number, ContinuousPerformanceSnapshot>();
  async function read() {
    const source = await getContinuousPerformanceSnapshot('effective');
    bases.set(++revision, source);
    if (bases.size > 64) bases.delete(bases.keys().next().value!);
    return simulationSourceSnapshot(source, revision);
  }
  return { read, panel(base: Record<string, any>, draft: Record<string, any>) {
    const source = bases.get(base?.syncRevision);
    if (!source) throw new Error('正式参数已变化，请恢复当前设置');
    return simulationPanelSnapshot(base,draft,source);
  }, async save(base: Record<string, any>, draft: Record<string, any>) {
    const source = bases.get(base?.syncRevision);
    if (!source) return { ok: false, reason: 'conflict', snapshot: await read() };
    try {
      const patch = simulationDraftPatch(base, draft, source);
      await updateContinuousPerformanceControls(source.identity, patch);
      return { ok: true, snapshot: await read() };
    } catch (error) { return { ok: false, reason: (error as Error).message, snapshot: await read() }; }
  } };
}
