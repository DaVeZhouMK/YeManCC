import { CONTROLLER_PERSONA_OPTIONS, migrateControllerPersona } from './controllerPersona';
import { isRadialTestingEnabled } from '../radial/testingState';
/**
 * Persisted controller shortcut schema. It contains no device reads; native
 * owns admission and executes the supported keyboard.* actions from it.
 */
export type ControllerButtonCode =
  | 'a' | 'b' | 'x' | 'y' | 'lb' | 'rb' | 'lt' | 'rt'
  | 'back' | 'start' | 'leftThumb' | 'rightThumb'
  | 'dpadUp' | 'dpadDown' | 'dpadLeft' | 'dpadRight';
export type ControllerTrigger = 'press' | 'hold' | 'release' | 'double' | 'repeat';
export type ControllerShortcutScope = 'native-global' | 'frontend' | 'hybrid';
export type ControllerShortcutInputSource = 'controller' | 'keyboard' | 'mouse' | 'oem';
export type ControllerShortcutInput = {
  source: ControllerShortcutInputSource;
  code: string;
};
export type ControllerShortcutRecordMode = 'controller' | 'keyboard-mouse' | 'oem';

/**
 * 本机专用键（掌机背部/专用按键）清单，由 native oem.keys.get 提供。
 * 机型库提供界面顺序和正面透视图背键位置；物理排列可与 HC OEM 编号序不同。
 * keyId 保持来源语义，不能按界面左右重编号或改写已保存的规则。
 */
export type OemTriggerCapability = 'full' | 'click-only'; // click-only：WMI 一次性事件（MSI CLAW/QS），仅 press/double
export type OemKeyPhysicalForm = 'keyboard-vk' | 'hid-button' | 'wmi-event' | 'embedded';
export interface OemSpecialKey {
  keyId: string;                 // 机型库语义键名（m1/m2/claw/qs/back1...）
  label: string;                 // 真实键名（M1/CLAW/QS/L4...）
  backIndex: number | null;      // 1-4 背部键位（背部键才有；非背部如 CLAW/QS 为 null）
  triggerCapability: OemTriggerCapability;
  physicalForm?: OemKeyPhysicalForm; // 采集形态（键盘 VK / HID 按钮 / WMI 事件 / 嵌入式）
  captureImplemented?: boolean; // 缺省沿用旧机型行为；false 仅保留目录展示
  captureReason?: string; // 内部能力原因，不直接展示给用户
}
export interface OemKeysSnapshot {
  familyId: string;
  supported: boolean;            // 本机是否有机型库适配
  keys: OemSpecialKey[];         // 机型库的界面顺序，非输入/输出位编号
}
export const OEM_EMPTY_SNAPSHOT: OemKeysSnapshot = { familyId: 'unknown', supported: false, keys: [] };

/**
 * 规范化 native oem.keys.get 的原始 JSON（纯函数，无 ipc 依赖，可单测）。
 * 未知/损坏字段 fail-closed：键缺少 keyId/label 丢弃；supported 仅在清单非空且
 * 声明 supported=true 时成立；triggerCapability 非 click-only 一律按 full 处理。
 * captureImplemented 缺省不补字段，存在但非 boolean 时按 false；reason 只保留 string。
 */
export function normalizeOemKeysSnapshot(raw: unknown): OemKeysSnapshot {
  if (!raw || typeof raw !== 'object') return OEM_EMPTY_SNAPSHOT;
  const r = raw as Record<string, unknown>;
  const keys = Array.isArray(r.keys)
    ? r.keys
        .filter((k): k is Record<string, unknown> => !!k && typeof k === 'object')
        .filter((k) => typeof k.keyId === 'string' && k.keyId.length > 0 && typeof k.label === 'string' && k.label.length > 0)
        .map((k) => ({
          keyId: String(k.keyId),
          label: String(k.label),
          ...(Object.prototype.hasOwnProperty.call(k, 'captureImplemented')
            ? { captureImplemented: typeof k.captureImplemented === 'boolean' ? k.captureImplemented : false }
            : {}),
          ...(typeof k.captureReason === 'string' ? { captureReason: k.captureReason } : {}),
          // native 用 0 表示非背部；统一归一为 null（背部卡牌仅认 1-4）
          backIndex: Number.isFinite(k.backIndex) && Number(k.backIndex) >= 1 && Number(k.backIndex) <= 4 ? Number(k.backIndex) : null,
          triggerCapability: k.triggerCapability === 'click-only' ? 'click-only' as const : 'full' as const,
          ...(k.physicalForm === 'keyboard-vk' || k.physicalForm === 'hid-button' || k.physicalForm === 'wmi-event' || k.physicalForm === 'embedded'
            ? { physicalForm: k.physicalForm as OemKeyPhysicalForm }
            : {}),
        }))
    : [];
  return {
    familyId: typeof r.familyId === 'string' ? r.familyId : 'unknown',
    supported: r.supported === true && keys.length > 0,
    keys,
  };
}
export type ControllerShortcutRuleOrigin = 'native-default' | 'custom';

/**
 * Actions already owned by the original YMCC shortcut route.
 * 2026-09-28 用户裁决：默认定义由此表维护，作为**隐藏模板**——
 *   · 无配置时自动生成初始规则（自动配置）；
 *   · “恢复默认快捷键”用它整体复写当前配置；
 *   · 不再作为占用预留拦截自定义规则（自定义可以改一切）。
 * 运行时执行层未变：默认动作仍由 native 既有路由承担（受原生开关门控），
 * keyboard.* 规则由规则表执行。
 */
export const NATIVE_DEFAULT_SHORTCUTS: ReadonlyArray<{
  defaultKey: 'enabled' | 'bDoubleMinimize' | 'tdpShortcut' | 'fpsShortcut' | 'killGame' | 'openKeyboard' | 'returnDesktop' | 'mouseToggle';
  actionId: string;
}> = [
  { defaultKey: 'enabled', actionId: 'window.summon' },
  { defaultKey: 'bDoubleMinimize', actionId: 'window.hideToTray' },
  { defaultKey: 'tdpShortcut', actionId: 'power.tdpAdjust' },
  { defaultKey: 'fpsShortcut', actionId: 'display.brightnessAdjust' },
  { defaultKey: 'killGame', actionId: 'game.killCurrent' },
  { defaultKey: 'openKeyboard', actionId: 'os.openTouchKeyboard' },
  { defaultKey: 'returnDesktop', actionId: 'os.returnDesktop' },
  { defaultKey: 'mouseToggle', actionId: 'mouse.toggle' },
];
/** A shortcut is recorded into four ordered slots; the first slot is required. */
export const CONTROLLER_SHORTCUT_MIN_INPUTS = 1;
export const CONTROLLER_SHORTCUT_MAX_INPUTS = 4;
export type ControllerShortcutRule = {
  id: string;
  /** Native defaults are the existing YMCC shortcuts; custom entries are user-created. */
  origin?: ControllerShortcutRuleOrigin;
  /** Stable native default key used to reconcile UI rules with GamepadSettings. */
  defaultKey?: string;
  enabled: boolean;
  actionId: string;
  inputs: ControllerShortcutInput[];
  trigger: ControllerTrigger;
  holdMs: number;
  doubleWindowMs: number;
  intervalMs: number;
  params: Record<string, string | number | boolean>;
};
export type ControllerShortcutAction = {
  id: string;
  label: string;
  category: string;
  scope: ControllerShortcutScope;
  available: boolean;
  description?: string;
  parameters?: Array<{
    id: string;
    label: string;
    type: 'select' | 'number' | 'boolean';
    presentation?: 'key-grid';
    gridColumns?: number;
    options?: Array<{ value: string; label: string }>;
    min?: number;
    max?: number;
    step?: number;
    default: string | number | boolean;
  }>;
};

export const CONTROLLER_BUTTONS: Array<{ value: ControllerButtonCode; label: string; gamepadIndex: number }> = [
  { value: 'a', label: 'A', gamepadIndex: 0 }, { value: 'b', label: 'B', gamepadIndex: 1 },
  { value: 'x', label: 'X', gamepadIndex: 2 }, { value: 'y', label: 'Y', gamepadIndex: 3 },
  { value: 'lb', label: 'LB', gamepadIndex: 4 }, { value: 'rb', label: 'RB', gamepadIndex: 5 },
  { value: 'lt', label: 'LT', gamepadIndex: 6 }, { value: 'rt', label: 'RT', gamepadIndex: 7 },
  { value: 'back', label: 'Back', gamepadIndex: 8 }, { value: 'start', label: 'Start', gamepadIndex: 9 },
  { value: 'leftThumb', label: '左摇杆按下', gamepadIndex: 10 }, { value: 'rightThumb', label: '右摇杆按下', gamepadIndex: 11 },
  { value: 'dpadUp', label: '↑', gamepadIndex: 12 }, { value: 'dpadDown', label: '↓', gamepadIndex: 13 },
  { value: 'dpadLeft', label: '←', gamepadIndex: 14 }, { value: 'dpadRight', label: '→', gamepadIndex: 15 },
];
export const CONTROLLER_TRIGGER_OPTIONS = [
  { value: 'press', label: '按下' }, { value: 'hold', label: '长按' }, { value: 'release', label: '松开' },
  { value: 'double', label: '双击' }, { value: 'repeat', label: '连发' },
];
const amountOptions = (unit: string) => Array.from({ length: 20 }, (_, index) => ({ value: String(index + 1), label: `${index + 1}${unit}` }));
const numberKeyboardOptions = [
  ...Array.from({ length: 10 }, (_, index) => ({ value: `Digit${index}`, label: String(index) })),
  ...Array.from({ length: 24 }, (_, index) => ({ value: `F${index + 1}`, label: `F${index + 1}` })),
  ...Array.from({ length: 10 }, (_, index) => ({ value: `Numpad${index}`, label: `数字区 ${index}` })),
  ...[{ value: 'NumLock', label: 'Num' }, { value: 'NumpadDivide', label: '数字区 /' }, { value: 'NumpadMultiply', label: '数字区 *' }, { value: 'NumpadSubtract', label: '数字区 -' }, { value: 'NumpadAdd', label: '数字区 +' }, { value: 'NumpadDecimal', label: '数字区 .' }, { value: 'NumpadEnter', label: '数字区 Enter' }],
];
const symbolKeyboardOptions = [
  { value: 'Backquote', label: '~' }, { value: 'Digit1', label: '!' }, { value: 'Digit2', label: '@' },
  { value: 'Digit3', label: '#' }, { value: 'Digit4', label: '$' }, { value: 'Digit5', label: '%' },
  { value: 'Digit6', label: '^' }, { value: 'Digit7', label: '&' }, { value: 'Digit8', label: '*' },
  { value: 'Digit9', label: '(' }, { value: 'Digit0', label: ')' }, { value: 'Minus', label: '_' },
  { value: 'Equal', label: '+' },
  { value: 'BracketLeft', label: '{' }, { value: 'BracketRight', label: '}' }, { value: 'Backslash', label: '|' }, { value: 'Semicolon', label: ':' }, { value: 'Quote', label: '"' }, { value: 'Comma', label: '<' }, { value: 'Period', label: '>' }, { value: 'Slash', label: '?' },
];
const letterKeyboardOptions = Array.from({ length: 26 }, (_, index) => {
  const letter = String.fromCharCode(65 + index);
  return { value: `Key${letter}`, label: letter };
});
const leftKeyboardFunctionOptions = [
  { value: 'Tab', label: 'Tab' }, { value: 'Space', label: 'Space' },
  { value: 'Backquote', label: '`' }, { value: 'Minus', label: '-' }, { value: 'Equal', label: '=' }, { value: 'BracketLeft', label: '[' }, { value: 'BracketRight', label: ']' }, { value: 'Backslash', label: '\\' }, { value: 'Semicolon', label: ';' }, { value: 'Quote', label: "'" }, { value: 'Comma', label: ',' }, { value: 'Period', label: '.' }, { value: 'Slash', label: '/' },
  { value: 'ShiftLeft', label: 'Shift' }, { value: 'AltLeft', label: 'Alt' },
  { value: 'Enter', label: 'Enter' }, { value: 'MetaLeft', label: 'Win' },
  { value: 'CapsLock', label: 'Caps' }, { value: 'Escape', label: 'Esc' },
  { value: 'ControlLeft', label: 'Ctrl' }, { value: 'Backspace', label: '退格' },
];
const rightKeyboardFunctionOptions = [
  { value: 'ShiftRight', label: '右 Shift' }, { value: 'AltRight', label: '右 Alt' }, { value: 'ControlRight', label: '右 Ctrl' }, { value: 'MetaRight', label: '右 Win' }, { value: 'ContextMenu', label: '菜单' },
  { value: 'ArrowUp', label: '↑' }, { value: 'ArrowDown', label: '↓' }, { value: 'ArrowLeft', label: '←' }, { value: 'ArrowRight', label: '→' },
  { value: 'PrintScreen', label: '截图' }, { value: 'ScrollLock', label: 'Scroll' }, { value: 'Pause', label: 'Pause' },
  { value: 'Insert', label: 'Insert' }, { value: 'Home', label: 'Home' }, { value: 'PageUp', label: 'PgUp' },
  { value: 'Delete', label: 'Delete' }, { value: 'End', label: 'End' }, { value: 'PageDown', label: 'PgDn' },
];

export const CONTROLLER_SHORTCUT_ACTIONS: ControllerShortcutAction[] = [
  { id: 'radial.summon', label: '呼出触摸轮盘', category: '窗口', scope: 'native-global', get available() { return isRadialTestingEnabled(); } },
  { id: 'window.summon', label: '呼出 YMCC', category: '窗口', scope: 'native-global', available: true },
  { id: 'window.hideToTray', label: '隐藏 YMCC', category: '窗口', scope: 'native-global', available: true },
  { id: 'os.switchProgram', label: '切换程序', category: '系统', scope: 'hybrid', available: true },
  { id: 'monitor.toggle', label: '监控显示开关', category: '系统', scope: 'hybrid', available: true,
    parameters: [{ id: 'operation', label: '操作', type: 'select', default: 'toggle', options: [{value:'toggle',label:'显示 / 隐藏切换'},{value:'show',label:'显示监控'},{value:'hide',label:'隐藏监控'}] }] },
  { id: 'display.frameRateControl', label: '锁帧控制', category: '性能', scope: 'hybrid', available: true,
    description: '沿用当前供电侧与现有游戏专属配置的独立锁帧，不改变性能调度档位。',
    parameters: [{ id:'operation',label:'操作',type:'select',default:'toggle',options:[{value:'toggle',label:'关闭锁帧 / 指定帧率切换'},{value:'set',label:'设置指定帧率'}]},
      {id:'fps',label:'指定帧率',type:'select',default:'60',options:[15,20,25,30,40,45,50,60,72,90,120,144,165,200,240,300].map(fps=>({value:String(fps),label:fps+' FPS'}))}] },
  { id: 'fan.switchPreset', label: '风扇预设切换', category: '性能', scope: 'hybrid', available: true,
    description: '选择已有预设曲线；风扇控制关闭时只保存选择，不自动开启控制。',
    parameters: [{id:'preset',label:'预设',type:'select',default:'next',options:[{value:'next',label:'循环切换已有预设'},{value:'soft',label:'轻柔转速'},{value:'balanced',label:'均衡转速'},{value:'aggressive',label:'暴力转速'}]}] },
  { id: 'app.launchQuickApp', label: '启动快捷应用', category: '系统', scope: 'hybrid', available: true,
    description: '选择当前快捷应用列表中的项目，按保存路径识别，不因列表重新排序启动其他应用。',
    parameters: [{id:'appPath',label:'快捷应用',type:'select',default:'',options:[]}] },
  { id: 'app.exit', label: '退出软件', category: '窗口', scope: 'native-global', available: false },
  { id: 'os.returnDesktop', label: '返回桌面', category: '系统', scope: 'native-global', available: true },
  { id: 'game.killCurrent', label: '结束当前游戏', category: '游戏', scope: 'native-global', available: true },
  { id: 'os.openTouchKeyboard', label: '打开触控键盘', category: '系统', scope: 'native-global', available: true },
  { id: 'mouse.toggle', label: '模拟鼠标', category: '输入', scope: 'native-global', available: true },
  {
    id: 'input.frontendButton', label: '前端呼出按钮', category: '输入', scope: 'native-global', available: true,
    description: 'Steam 键和三点键根据实际虚拟手柄路由：运行中的 Steam Deck 分别发送对应专用键；无虚拟手柄时按 Ctrl+1 回退。PS / Xbox 需对应虚拟手柄；Gamebar 发送 Win+G。不切换预设。',
    parameters: [{ id: 'button', label: '呼出按钮', type: 'select', default: 'steam', options: [
      { value: 'steam', label: 'Steam 键' }, { value: 'steamQuickAccess', label: 'Steam 三点键' }, { value: 'ps', label: 'PS' },
      { value: 'xbox', label: 'Xbox' }, { value: 'gamebar', label: 'Gamebar【Win+G】' },
    ] }],
  },
  {
    id: 'input.gyroToggle', label: '陀螺仪开关', category: '输入', scope: 'native-global', available: true,
    description: '已开启则关闭，已关闭则按所选预设开启。仅本次运行生效，不记忆开关或改写预设；全局与游戏专用配置都可使用。',
    parameters: [{ id: 'preset', label: '开启预设', type: 'select', default: 'fps', options: [
      { value: 'fps', label: 'FPS射击' }, { value: 'racing', label: '赛车' },
      { value: 'custom', label: '自定义' }, { value: 'steam', label: 'Steam' },
    ] }],
  },
  {
    id: 'input.virtualGamepadToggle', label: '虚拟手柄开关', category: '输入', scope: 'native-global', available: true,
    description: '已开启则关闭，已关闭则按所选预设开启。仅本次运行生效，不记忆开关或改写全局/游戏专用配置。',
    parameters: [{ id: 'preset', label: '开启预设', type: 'select', default: 'steamdeck', options: [
      ...CONTROLLER_PERSONA_OPTIONS.filter(option => option.value !== 'disabled'),
    ] }],
  },
  {
    id: 'keyboard.numeric', label: '数字键盘', category: '键盘', scope: 'native-global', available: true,
    parameters: [{ id: 'key', label: '按键', type: 'select', presentation: 'key-grid', gridColumns: 11, default: 'Digit1', options: numberKeyboardOptions }],
  },
  {
    id: 'keyboard.symbol', label: '符号键盘', category: '键盘', scope: 'native-global', available: true,
    parameters: [{ id: 'key', label: '按键', type: 'select', presentation: 'key-grid', gridColumns: 7, default: 'Backquote', options: symbolKeyboardOptions }],
  },
  {
    id: 'keyboard.letter', label: '字母键盘', category: '键盘', scope: 'native-global', available: true,
    parameters: [{ id: 'key', label: '按键', type: 'select', presentation: 'key-grid', gridColumns: 13, default: 'KeyA', options: letterKeyboardOptions }],
  },
  {
    id: 'keyboard.leftFunction', label: '左侧键盘功能', category: '键盘', scope: 'native-global', available: true,
    parameters: [{ id: 'key', label: '按键', type: 'select', presentation: 'key-grid', gridColumns: 4, default: 'Enter', options: leftKeyboardFunctionOptions }],
  },
  {
    id: 'keyboard.rightFunction', label: '右侧键盘功能', category: '键盘', scope: 'native-global', available: true,
    parameters: [{ id: 'key', label: '按键', type: 'select', presentation: 'key-grid', gridColumns: 3, default: 'PrintScreen', options: rightKeyboardFunctionOptions }],
  },
  { id: 'performance.autoAdjust', label: '自动调节', category: '性能', scope: 'hybrid', available: false },
  { id: 'performance.editMode', label: '编辑性能档位', category: '性能', scope: 'frontend', available: false },
  {
    id: 'power.tdpAdjust', label: '调节 TDP', category: '性能', scope: 'hybrid', available: true,
    parameters: [
      { id: 'direction', label: '增加方向', type: 'select', default: 'up', options: [{ value: 'up', label: '方向上增加' }, { value: 'down', label: '方向下增加' }] },
      { id: 'amount', label: '每次调节', type: 'select', default: '1', options: amountOptions('W') },
      { id: 'linearAcceleration', label: '线性加速', type: 'boolean', default: true },
    ],
  },
  {
    id: 'display.brightnessAdjust', label: '调节亮度', category: '性能', scope: 'hybrid', available: true,
    parameters: [
      { id: 'direction', label: '增加方向', type: 'select', default: 'right', options: [{ value: 'right', label: '方向右增加' }, { value: 'left', label: '方向左增加' }] },
      { id: 'amount', label: '每次调节', type: 'select', default: '5', options: amountOptions('%') },
      { id: 'linearAcceleration', label: '线性加速', type: 'boolean', default: true },
    ],
  },
  {
    id: 'performance.switchMode', label: '切换性能档位', category: '性能', scope: 'hybrid', available: false,
    parameters: [{ id: 'direction', label: '下一个档位方向', type: 'select', default: 'up', options: [{ value: 'up', label: '方向上' }, { value: 'down', label: '方向下' }] }],
  },
];

const buttonSet = new Set(CONTROLLER_BUTTONS.map((item) => item.value));
const triggerSet = new Set(CONTROLLER_TRIGGER_OPTIONS.map((item) => item.value));
const actionMap = new Map(CONTROLLER_SHORTCUT_ACTIONS.map((item) => [item.id, item]));
export const keyboardCodePattern = /^(?:Key[A-Z]|Digit[0-9]|F(?:[1-9]|1[0-9]|2[0-4])|BracketLeft|BracketRight|Backslash|Semicolon|Quote|Comma|Period|Slash|Numpad(?:[0-9]|Add|Subtract|Multiply|Divide|Decimal|Enter)|Backquote|Minus|Equal|Arrow(?:Up|Down|Left|Right)|(?:Control|Shift|Alt|Meta)(?:Left|Right)?|Tab|Enter|Escape|Space|Backspace|Delete|Insert|Home|End|PageUp|PageDown|CapsLock|NumLock|ScrollLock|PrintScreen|Pause|ContextMenu)$/;
const mouseCodeSet = new Set(['MouseLeft', 'MouseMiddle', 'MouseRight', 'MouseBack', 'MouseForward']);
function finite(value: unknown, fallback: number, min: number, max: number): number {
  const number = Number(value);
  return Number.isFinite(number) ? Math.min(max, Math.max(min, Math.round(number))) : fallback;
}
function inputKey(input: ControllerShortcutInput): string { return `${input.source}:${input.code}`; }
function controllerInput(code: ControllerButtonCode): ControllerShortcutInput { return { source: 'controller', code }; }

export function formatControllerShortcutInput(input: ControllerShortcutInput): string {
  if (input.source === 'controller') return CONTROLLER_BUTTONS.find((item) => item.value === input.code)?.label || input.code;
  if (input.source === 'oem') return input.code; // keyId 由编辑器按机型清单映射为显示名
  if (input.source === 'mouse') return ({ MouseLeft: '鼠标左键', MouseMiddle: '鼠标中键', MouseRight: '鼠标右键', MouseBack: '鼠标后退键', MouseForward: '鼠标前进键' } as Record<string, string>)[input.code] || input.code;
  const labels: Record<string, string> = { Space: '空格', Enter: 'Enter', Escape: 'Esc', Tab: 'Tab', Backspace: '退格', CapsLock: 'Caps', Delete: 'Delete', ArrowUp: '↑', ArrowDown: '↓', ArrowLeft: '←', ArrowRight: '→', ControlLeft: 'Ctrl', ControlRight: 'Ctrl', ShiftLeft: 'Shift', ShiftRight: 'Shift', AltLeft: 'Alt', AltRight: 'Alt', MetaLeft: 'Win', MetaRight: 'Win' };
  if (labels[input.code]) return labels[input.code];
  if (input.code.startsWith('Key')) return input.code.slice(3);
  if (input.code.startsWith('Digit')) return input.code.slice(5);
  return input.code;
}

export function formatControllerShortcutInputs(inputs: readonly ControllerShortcutInput[]): string {
  return inputs.map(formatControllerShortcutInput).join(' + ');
}

/** Presentation only: extended function codes retain their real keyboard identity. */
export function isHandheldExtendedKey(input: ControllerShortcutInput): boolean {
  return input.source === 'keyboard' && /^F(?:1[3-9]|2[0-4])$/.test(input.code);
}
export function shortcutPreviewKind(inputs: readonly ControllerShortcutInput[]): 'controller' | 'keyboard' {
  return !inputs.length || inputs.some(input => input.source === 'controller' || input.source === 'oem' || isHandheldExtendedKey(input)) ? 'controller' : 'keyboard';
}
export function shortcutRecordMode(inputs: readonly ControllerShortcutInput[]): ControllerShortcutRecordMode {
  return shortcutPreviewKind(inputs) === 'keyboard' ? 'keyboard-mouse' : 'controller';
}

function normalizeInput(value: unknown): ControllerShortcutInput | null {
  // Legacy snapshots stored controller codes as strings. Keep them readable and
  // editable while persisting all new recordings as typed source/code pairs.
  if (typeof value === 'string' && buttonSet.has(value as ControllerButtonCode)) return controllerInput(value as ControllerButtonCode);
  if (!value || typeof value !== 'object') return null;
  const raw = value as Partial<ControllerShortcutInput>;
  if (raw.source === 'controller' && typeof raw.code === 'string' && buttonSet.has(raw.code as ControllerButtonCode)) return controllerInput(raw.code as ControllerButtonCode);
  if (raw.source === 'keyboard' && typeof raw.code === 'string' && keyboardCodePattern.test(raw.code)) return { source: 'keyboard', code: raw.code };
  if (raw.source === 'mouse' && typeof raw.code === 'string' && mouseCodeSet.has(raw.code)) return { source: 'mouse', code: raw.code };
  // OEM key ids come from the machine library. Existence against the current
  // machine is validated at editor time; persisting an unknown id keeps the
  // rule readable on machines where that key is not present (fail-closed at
  // runtime, not silently dropped from config).
  if (raw.source === 'oem' && typeof raw.code === 'string' && raw.code.trim().length > 0) return { source: 'oem', code: raw.code.trim() };
  return null;
}

export function actionById(actionId: string): ControllerShortcutAction | undefined { return actionMap.get(actionId); }
/** Presentation only: hidden actions remain in the schema and saved rules. */
export function isControllerShortcutActionVisible(actionId: string): boolean { return actionById(actionId)?.available === true; }
export function nativeDefaultRule(defaultKey: (typeof NATIVE_DEFAULT_SHORTCUTS)[number]['defaultKey'], enabled = true): ControllerShortcutRule {
  const definition = NATIVE_DEFAULT_SHORTCUTS.find((item) => item.defaultKey === defaultKey)!;
  const base = newControllerShortcutRule(`native-${defaultKey}`, definition.actionId);
  const byKey: Record<string, { inputs: ControllerShortcutInput[]; trigger: ControllerTrigger; holdMs?: number; doubleWindowMs?: number; intervalMs?: number; params?: Record<string, string | number | boolean> }> = {
    enabled: { inputs: [controllerInput('lb'), controllerInput('rb')], trigger: 'hold', holdMs: 500 },
    bDoubleMinimize: { inputs: [controllerInput('b')], trigger: 'double', doubleWindowMs: 500 },
    tdpShortcut: { inputs: [controllerInput('start'), controllerInput('dpadUp')], trigger: 'repeat', holdMs: 0, intervalMs: 150, params: { direction: 'up', amount: '1', linearAcceleration: true } },
    fpsShortcut: { inputs: [controllerInput('start'), controllerInput('dpadRight')], trigger: 'repeat', holdMs: 0, intervalMs: 150, params: { direction: 'right', amount: '5', linearAcceleration: true } },
    killGame: { inputs: [controllerInput('back'), controllerInput('b')], trigger: 'hold', holdMs: 500 },
    openKeyboard: { inputs: [controllerInput('back'), controllerInput('x')], trigger: 'hold', holdMs: 500 },
    returnDesktop: { inputs: [controllerInput('back'), controllerInput('a')], trigger: 'press', holdMs: 500 },
    mouseToggle: { inputs: [controllerInput('back'), controllerInput('y')], trigger: 'hold', holdMs: 500 },
  };
  const spec = byKey[defaultKey];
  return {
    ...base,
    id: `native-${defaultKey}`,
    origin: 'native-default',
    defaultKey,
    enabled,
    inputs: spec.inputs,
    trigger: spec.trigger,
    holdMs: spec.holdMs ?? base.holdMs,
    doubleWindowMs: spec.doubleWindowMs ?? base.doubleWindowMs,
    intervalMs: spec.intervalMs ?? base.intervalMs,
    params: spec.params ?? defaultParameters(definition.actionId),
  };
}

export function newControllerShortcutRule(seed = `${Date.now()}`, actionId = 'window.summon'): ControllerShortcutRule {
  const keyboardAction = actionId.startsWith('keyboard.');
  return {
    id: `rule-${seed}`,
    origin: 'custom',
    enabled: true,
    actionId,
    inputs: [controllerInput('lb'), controllerInput('rb')],
    trigger: keyboardAction ? 'press' : 'hold',
    holdMs: keyboardAction ? 500 : 2000,
    doubleWindowMs: 500,
    intervalMs: 150,
    params: defaultParameters(actionId),
  };
}
export function normalizeControllerShortcutRule(value: unknown, index: number): ControllerShortcutRule | null {
  if (!value || typeof value !== 'object') return null;
  const raw = value as Partial<ControllerShortcutRule>;
  const actionId = typeof raw.actionId === 'string' && raw.actionId ? raw.actionId : 'window.summon';
  const inputs = Array.isArray(raw.inputs)
    ? raw.inputs.map(normalizeInput).filter((item): item is ControllerShortcutInput => item !== null).filter((item, position, all) => all.findIndex((candidate) => inputKey(candidate) === inputKey(item)) === position).slice(0, CONTROLLER_SHORTCUT_MAX_INPUTS)
    : [];
  const trigger = triggerSet.has(raw.trigger as ControllerTrigger) ? raw.trigger as ControllerTrigger : 'press';
  const { defaultKey: _storedDefaultKey, ...preserved } = raw;
  const params = { ...defaultParameters(actionId),
    ...(raw.params && typeof raw.params === 'object' ? raw.params as Record<string, string | number | boolean> : {}),
  };
  if (actionId === 'input.virtualGamepadToggle') {
    const persona = migrateControllerPersona(params.preset);
    if (persona !== 'disabled') params.preset = persona;
  }
  return {
    ...preserved,
    id: typeof raw.id === 'string' && raw.id ? raw.id : `rule-import-${index}`,
    origin: raw.origin === 'native-default' ? 'native-default' : 'custom',
    // Optional JSON fields must be absent, not undefined: durable input hashes are strict.
    ...(typeof raw.defaultKey === 'string' && raw.defaultKey ? { defaultKey: raw.defaultKey } : {}),
    enabled: raw.enabled !== false,
    actionId,
    inputs,
    trigger,
    holdMs: finite(raw.holdMs, 500, trigger === 'repeat' ? 0 : 100, 10000),
    doubleWindowMs: finite(raw.doubleWindowMs, 500, 200, 2000),
    intervalMs: finite(raw.intervalMs, 150, 40, 2000),
    params,
  };
}
export function readControllerShortcutRules(value: unknown): ControllerShortcutRule[] {
  const raw = value && typeof value === 'object' ? value as { shortcutRules?: unknown } : {};
  const list = Array.isArray(raw.shortcutRules) ? raw.shortcutRules : [];
  return list.map((item, index) => normalizeControllerShortcutRule(item, index)).filter((item): item is ControllerShortcutRule => item !== null);
}

/**
 * Merge the persisted rules with the hidden default template.
 * Start → F7 retired on 2026-10-19: never seed it; tagged old defaults are
 * excluded by the current template list. User-created F7 rules remain custom.
 * 2026-09-28 用户裁决（默认模板化）：
 *   · 空配置 → 用默认模板自动生成全部默认项（首次运行自动配置）；
 *   · 已有配置 → 已保存的默认项原样保留（enabled 以原生开关为准），
 *     仅在“原生开关仍为开”时补种缺失的默认项（覆盖版本升级新增默认项）；
 *     用户删除并停用过的默认项**不再自动复活**——配置即唯一真相。
 *   · 自定义规则一律保留（不再因与默认项重名/同组合而被删除）。
 * 输出顺序固定为“默认在前，自定义在后”。
 */
export function reconcileControllerShortcutRules(
  rules: readonly ControllerShortcutRule[],
  nativeEnabled: Partial<Record<(typeof NATIVE_DEFAULT_SHORTCUTS)[number]['defaultKey'], boolean>>,
): { rules: ControllerShortcutRule[]; seeded: number } {
  const persistedDefaults = new Map(
    rules.filter((rule) => rule.origin === 'native-default' && rule.defaultKey)
      .map((rule) => [rule.defaultKey!, rule] as const),
  );
  const native: ControllerShortcutRule[] = [];
  let seeded = 0;
  for (const definition of NATIVE_DEFAULT_SHORTCUTS) {
    const persisted = persistedDefaults.get(definition.defaultKey);
    const enabled = nativeEnabled[definition.defaultKey] !== false;
    if (persisted) {
      // GamepadSettings 是原生开关的权威来源；已保存的默认项保留可编辑参数，
      // 但绝不让过期行复活一条被原生关闭的快捷。
      native.push({ ...persisted, enabled });
      continue;
    }
    if (rules.length > 0 && !enabled) continue;
    native.push(nativeDefaultRule(definition.defaultKey, enabled));
    seeded += 1;
  }
  // 自定义规则不再做占用去重（2026-09-28 用户裁决：可以改一切，不再做限制）。
  const kept: ControllerShortcutRule[] = rules
    .filter((rule) => rule.origin !== 'native-default')
    .map((rule) => ({ ...rule, origin: 'custom' as const }));
  return { rules: [...native, ...kept], seeded };
}
export type ControllerShortcutDraftMerge =
  | { ok: true; rules: ControllerShortcutRule[] }
  | { ok: false; conflicts: string[] };

/** Editor has add/delete/field controls, not a reorder control. Keep the latest
 * saved order and append new local rules. Never overwrite a differently edited
 * same-id rule; the unapplied draft remains owned by the editor on conflict. */
export function mergeControllerShortcutDraft(
  baseline: readonly ControllerShortcutRule[],
  draft: readonly ControllerShortcutRule[],
  latest: readonly ControllerShortcutRule[],
): ControllerShortcutDraftMerge {
  const stable = (value: unknown): string => {
    if (Array.isArray(value)) return `[${value.map(stable).join(',')}]`;
    if (value && typeof value === 'object') return `{${Object.entries(value)
      .filter(([, item]) => item !== undefined).sort(([a], [b]) => a.localeCompare(b))
      .map(([key, item]) => `${JSON.stringify(key)}:${stable(item)}`).join(',')}}`;
    return JSON.stringify(value) ?? 'undefined';
  };
  const maps = [baseline, draft, latest].map((list) => new Map(list.map((rule) => [rule.id, rule])));
  const conflicts = new Set<string>();
  [baseline, draft, latest].forEach((list, index) => {
    if (list.length !== maps[index].size) {
      const seen = new Set<string>();
      for (const rule of list) { if (seen.has(rule.id)) conflicts.add(rule.id); seen.add(rule.id); }
    }
  });
  const merged = new Map<string, ControllerShortcutRule>();
  const ids = new Set([...maps[0].keys(), ...maps[1].keys(), ...maps[2].keys()]);
  for (const id of ids) {
    const base = maps[0].get(id), local = maps[1].get(id), remote = maps[2].get(id);
    const baseText = stable(base), localText = stable(local), remoteText = stable(remote);
    let chosen: ControllerShortcutRule | undefined;
    if (localText === baseText) chosen = remote;
    else if (remoteText === baseText || remoteText === localText) chosen = local;
    else { conflicts.add(id); continue; }
    if (chosen) merged.set(id, JSON.parse(JSON.stringify(chosen)) as ControllerShortcutRule);
  }
  if (conflicts.size) return { ok: false, conflicts: [...conflicts] };
  const ordered: ControllerShortcutRule[] = [];
  for (const rule of [...latest, ...draft]) {
    const chosen = merged.get(rule.id);
    if (chosen) { ordered.push(chosen); merged.delete(rule.id); }
  }
  return { ok: true, rules: ordered };
}

export function defaultParameters(actionId: string): Record<string, string | number | boolean> {
  const action = actionById(actionId);
  return Object.fromEntries((action?.parameters || []).map((parameter) => [parameter.id, parameter.default]));
}
export function validateControllerShortcutRules(rules: ControllerShortcutRule[], oemKeys?: OemKeysSnapshot): string[] {
  const problems: string[] = [];
  const seenIds = new Set<string>();
  const oemKnown = oemKeys && oemKeys.supported
    ? new Set(oemKeys.keys.map((key) => key.keyId))
    : null; // 无机型库上下文时不校验存在性（保持可编辑），运行时 fail-closed
  const oemUnavailable = new Set(oemKeys?.keys.filter((key) => key.captureImplemented === false).map((key) => key.keyId));
  for (const rule of rules) {
    if (seenIds.has(rule.id)) problems.push('动作编号重复，请重新新建动作。');
    seenIds.add(rule.id);
    const action = actionMap.get(rule.actionId);
    if (!action) problems.push('请选择一个有效的输出动作。');
    else {
      for (const parameter of action.parameters || []) {
        const inputToggle = action.id === 'input.gyroToggle' || action.id === 'input.virtualGamepadToggle' || action.id === 'input.frontendButton';
        const shortcutControl = ['monitor.toggle','display.frameRateControl','fan.switchPreset'].includes(action.id);
        if (parameter.presentation !== 'key-grid' && !((inputToggle || shortcutControl) && parameter.type === 'select')) continue;
        const value = rule.params[parameter.id] ?? parameter.default;
        if (typeof value !== 'string' || !parameter.options?.some((option) => option.value === value)) {
          problems.push(inputToggle ? '请重新选择有效的输出预设。' : '请重新选择有效的输出按键。');
        }
      }
    }
    // Native defaults mirror existing routes; custom rules require at least
    // one input and may use up to four ordered inputs.
    if (rule.origin !== 'native-default' && (rule.inputs.length < CONTROLLER_SHORTCUT_MIN_INPUTS || rule.inputs.length > CONTROLLER_SHORTCUT_MAX_INPUTS)) problems.push('请录制 1 至 4 个输入按键。');
    if (action?.id === 'app.launchQuickApp' && (typeof rule.params.appPath !== 'string' || !rule.params.appPath || rule.params.appPath.length > 2048)) problems.push('请选择要启动的快捷应用。');
    if (new Set(rule.inputs.map(inputKey)).size !== rule.inputs.length) problems.push('输入中有重复按键，请重新录制。');
    // Mixed physical controller/OEM/keyboard/mouse chords are intentional.
    // Persist real source/code pairs; only the preview classifies F13–F24 as handheld.
    for (const input of rule.inputs) {
      if (input.source !== 'oem') continue;
      if (oemUnavailable.has(input.code)) {
        problems.push('此专用按键的输入暂不可用，请更换输入。');
      } else if (oemKnown && !oemKnown.has(input.code)) {
        problems.push('未识别此专用按键，请在当前设备上重新录制。');
      }
    }
  }
  // 只保留结构性校验（组合数量、重复按键、录制源混用、未知动作/键位）；
  // 动作不独占、组合可重复由用户自行负责（同一组按键可绑定多个动作）。
  return [...new Set(problems)];
}
export function highlightedGamepadButtons(rule: ControllerShortcutRule | null): number[] {
  if (!rule) return [];
  return rule.inputs.filter((input) => input.source === 'controller').map((input) => CONTROLLER_BUTTONS.find((item) => item.value === input.code)?.gamepadIndex).filter((value): value is number => value !== undefined);
}
