import { normalizeOemKeysSnapshot, type OemKeysSnapshot, type OemSpecialKey } from './controllerShortcutRules';

export interface OemInputUsage extends OemKeysSnapshot {
  deviceClass: string;
  runtime: {
    keyboardActive: boolean; rawKeyboardActive: boolean;
    msiWmiState: number; msiWmiCurrent: boolean;
    nativeRearAutomaticMapping: 'held' | 'experimental' | 'not-applicable' | 'unknown';
  };
  officialFrontend: { requested: boolean; applied: boolean; pending: boolean; failed: boolean };
}
/** Presentation only: catalog registration and collector state never prove a key
 * press, action, OEM suppression, or game consumption. Unknown receipts stay unknown. */
export function normalizeOemInputUsage(raw: unknown): OemInputUsage | null {
  if (!raw || typeof raw !== 'object') return null;
  const value = raw as Record<string, unknown>;
  if (value.schemaVersion !== 1 || typeof value.deviceClass !== 'string') return null;
  const r = value.runtime && typeof value.runtime === 'object' ? value.runtime as Record<string, unknown> : {};
  const f = value.officialFrontend && typeof value.officialFrontend === 'object' ? value.officialFrontend as Record<string, unknown> : {};
  const requested = f.shortcutOwnerActive === true || f.effective === true;
  return {
    ...normalizeOemKeysSnapshot(value), deviceClass: value.deviceClass,
    runtime: {
      keyboardActive: r.keyboardActive === true, rawKeyboardActive: r.rawKeyboardActive === true,
      msiWmiState: Number.isInteger(r.msiWmiState) ? Number(r.msiWmiState) : 0,
      msiWmiCurrent: r.msiWmiCurrent === true,
      nativeRearAutomaticMapping: r.nativeRearAutomaticMapping === 'held' || r.nativeRearAutomaticMapping === 'experimental' || r.nativeRearAutomaticMapping === 'not-applicable' ? r.nativeRearAutomaticMapping : 'unknown',
    },
    officialFrontend: { requested, applied: requested && f.applied === true,
      pending: requested && f.pending === true,
      failed: requested && f.pending === false && f.lastOutcomeKnown === true && f.lastOutcomeOk === false },
  };
}
export interface ControllerFeatureStatus {
  id: 'back' | 'dedicated' | 'gyro'; name: string; value: string; hint: string;
  tone: 'on' | 'off' | 'warning' | 'pending'; detail: string;
}
export interface ControllerFeatureContext {
  usage?: OemInputUsage | null;
  virtualEnabled: boolean; persona: string; gyroEnabled: boolean; gyroSupported: boolean;
  physicalGyroSupported?: boolean | null;
  observedKeys?: readonly string[];
}
function frontendDetail(usage: OemInputUsage): string {
  const f = usage.officialFrontend;
  if (f.failed) return '官方软件隔离未完成，可能保留默认动作或覆盖映射；当前回执不能确认它已阻断输入。';
  if (f.pending) return '官方软件隔离状态待确认；不能据此认定按键被阻断。';
  if (f.applied) return '已有官方软件隔离操作回执；仍需确认原厂默认动作是否被屏蔽。';
  return '官方软件的映射和自动切换可能影响输入，目前没有确认它正在阻断；原厂默认动作是否被屏蔽需单独确认。';
}
function keysStatus(id: 'back' | 'dedicated', context: ControllerFeatureContext): ControllerFeatureStatus {
  const name = id === 'back' ? '背部按键' : '专用按键';
  const base = { id, name, tone: 'pending' as const, value: '待确认', hint: '尚未取得机型信息', detail: '尚未取得能力总库和当前采集路线回执；这不等于硬件没有此功能。' };
  const usage = context.usage;
  if (!usage || !usage.supported) return base;
  const keys = usage.keys.filter(key => (key.backIndex !== null) === (id === 'back'));
  if (!keys.length) return { ...base, tone: 'off', value: '无适配', hint: '本机总库未登记', detail: '当前机型总库没有登记这类按键，不能据此断言硬件不存在。' };
  const implemented = keys.filter(key => key.captureImplemented !== false);
  const observed = implemented.filter(key => context.observedKeys?.includes(key.keyId));
  const names = (list: OemSpecialKey[]) => list.map(key => key.label).join('、');
  const output = id !== 'back' ? '专用按键的YMCC快捷动作不要求虚拟手柄。'
    : !context.virtualEnabled ? '本机模式使用现有物理／键盘路线执行YMCC快捷动作；独立背键如何进入游戏仍取决于固件和游戏。'
    : context.persona === 'elite' ? 'Xbox虚拟输出没有独立背键通道；背键仍可用于已有的YMCC快捷动作。'
    : '现有Steam Deck／PS5虚拟输出提供两个背键通道，实际映射与游戏消费仍需确认。';
  const keyboardReady = usage.runtime.keyboardActive || usage.runtime.rawKeyboardActive;
  // Existing input remains usable independently of the optional firmware writer.
  // Writer state and official-frontend receipts are advice, never input admission.
  const mappingDetail = usage.familyId === 'msi-claw' && id === 'back' && !context.virtualEnabled
    ? '本机模式使用固件现有的F11／F14背键映射；若按键无信号，请核对官方前端中的映射。' +
      (usage.runtime.nativeRearAutomaticMapping === 'held' ? '当前不会自动改写该固件映射；这不限制已有键盘输入。' : '') +
      '虚拟模式保留已有DInput背键路线。' : '';
  const wmiMissing = usage.familyId === 'msi-claw' && id === 'dedicated' && !usage.runtime.msiWmiCurrent;
  const qsFallback = wmiMissing && keyboardReady && implemented.some(key => key.keyId === 'qs');
  const wmiDetail = wmiMissing
    ? (usage.runtime.msiWmiState === 3 ? '当前WMI事件订阅失败。' : '当前WMI事件订阅尚未有效。') +
      (qsFallback ? 'QS仍可尝试已有Win+G／Win+Tab兼容采集，是否由本机固件输出需按键确认；CLAW仍需WMI事件。' : '') +
      '是否受官方软件影响需结合日志确认。' : '';
  const detail = '总库登记：' + names(keys) + '。' + output + mappingDetail + wmiDetail + frontendDetail(usage) + '收到按键信号不等于快捷动作已执行或游戏已收到。';
  if (!implemented.length) return { ...base, tone: 'warning', value: '采集未接入', hint: '仅有机型登记', detail };
  if (observed.length) return { ...base, tone: 'on', value: '已收到 ' + observed.length + '/' + keys.length,
    hint: '本页已收到按键信号', detail: '本页当前模式曾收到：' + names(observed) + '。' + detail };
  if (wmiMissing) {
    if (qsFallback) return { ...base, tone: 'warning', value: '待按键确认', hint: 'QS有兼容采集路线', detail };
    return { ...base, tone: usage.runtime.msiWmiState === 3 ? 'warning' : 'pending',
      value: usage.runtime.msiWmiState === 3 ? '采集受阻' : '等待采集',
      hint: usage.runtime.msiWmiState === 3 ? '官方事件订阅失败' : '等待官方按键事件', detail };
  }
  if (implemented.every(key => key.physicalForm === 'keyboard-vk') && !keyboardReady) {
    return { ...base, value: '等待采集', hint: '键盘采集尚未就绪', detail };
  }
  return { ...base, tone: implemented.length < keys.length ? 'warning' : 'pending', value: '待按键确认',
    hint: implemented.length < keys.length ? implemented.length + '/' + keys.length + ' 键已接入' : '采集路线已接入', detail };
}
export function controllerFeatureStatuses(context: ControllerFeatureContext): ControllerFeatureStatus[] {
  const gyro: ControllerFeatureStatus = { id: 'gyro', name: '陀螺仪', value: '需虚拟手柄', hint: '本机模式未接入', tone: 'warning',
    detail: '本机手柄模式暂未接入体感，需要开启虚拟手柄后使用体感功能。' };
  if (context.virtualEnabled) {
    gyro.value = context.gyroEnabled ? (context.physicalGyroSupported === true ? '已识别体感源' : '等待输入') : '未开启';
    gyro.hint = context.gyroEnabled ? '沿用现有虚拟体感' : '可在体感设置开启';
    gyro.tone = context.gyroEnabled && context.physicalGyroSupported === true ? 'on' : 'pending';
    gyro.detail = '保留现有虚拟手柄陀螺仪采集、处理及摇杆映射。' +
      (context.gyroSupported ? '当前模式也有原生体感报告通道。' : 'Xbox没有原生体感报告通道，但现有体感到摇杆映射仍保留。') +
      (context.physicalGyroSupported === true ? '已有传感器绑定或体感处理状态；仍需确认新鲜输入、虚拟提交和游戏响应。' : '尚未收到当前页面的体感来源状态。');
  }
  return [keysStatus('back', context), keysStatus('dedicated', context), gyro];
}
