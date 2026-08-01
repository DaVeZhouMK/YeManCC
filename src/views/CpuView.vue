<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick, inject, type Ref } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import SegButton from '@/components/SegButton.vue';
import CcdCard from '@/components/CcdCard.vue';
import UndervoltCard from '@/components/UndervoltCard.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import {
  PW,
  SCHEMES,
  type SchemeKey,
  switchScheme,
  getActiveScheme,
  schemeExists,
  readPowerParams,
  applyPowerParams,
  setActiveScheme,
  type CoreMode,
  setCoreModeAc,
  setCoreModeDc,
  readCoreMode,
  setActiveCoreCount,
  readActiveCoreCount,
  readPhysicalCores,
  runResetProfile,
  RESET_PROFILES,
  readPowerRaw,
  detectCpuName,
  readSmt,
  setSmt,
  type SmtInfo,
  readRtssLimit,
  setRtssLimit,
  saveFps,
} from '@/bridge/yeman';
import { fs } from '@/bridge/api';
import {
  FLOAT_PROFILES,
  type FloatProfile,
  type FloatInfo,
  type FpsTarget,
  enableFloat,
  disableFloat,
  setFloatProfile,
  setFloatTarget,
  getFloatInfo,
  onFloatUpdate,
  getFloatSnapshot,
  applyCpuAutoEnable,
} from '@/bridge/autofloat';
import {
  isCpuLocked,
  loadCpuLock,
  setCpuLock,
  clearCpuLock,
  updateCpuLock,
  getCpuLock,
  onCpuLockUpdate,
} from '@/bridge/cpulock';

// ── 基准频率：根据实际 CPU 动态检测，默认 AMD Zen5 9950X = 4300 MHz ──
// 核心原则（写入 MD）：打开页面 / 切换页面只读取状态，绝不执行任何设置操作！
const BASE_MHZ_DEFAULT = 4300; // 9950X 基准频率 4.3GHz
const TURBO_MAX = 7200;
let baseMhz = BASE_MHZ_DEFAULT;

// 扩展 SchemeKey 支持 'other'（非野蛮/非标准方案标记）
type ExtendedSchemeKey = SchemeKey | 'other';
const schemeKey = ref<ExtendedSchemeKey>('yeman');
const isYemanScheme = computed(() => schemeKey.value === 'yeman');
const schemeName = computed(() => SCHEMES.find((s) => s.key === schemeKey.value)?.name ?? (schemeKey.value === 'other' ? '其他电源方案' : '野蛮系统电源'));
// 只读检测：野蛮系统电源方案是否存在（缺失时点一下“野蛮系统电源”会自动从 YM.pow 恢复，绝不删除）
const yemanMissing = ref(false);

const acFreq = ref(0); // MHz，0 = 不限制
const dcFreq = ref(0);
// ⚠️ 默认值必须是有利/安全的：睿频默认开启（true），避免"打开就关睿频"
const acTurbo = ref(true);
const dcTurbo = ref(true);
const acAggr = ref(100);
const dcAggr = ref(100);

const coreModeAc = ref<CoreMode>('big');
const coreModeDc = ref<CoreMode>('big');
// ROG 奥创专用电源 GUID（识别到则顶部提示特别标注）
const ROG_GUIDS = ['6fecc5ae-f350-48a5-b669-b472cb895ccf', '27fa6203-3987-4dcc-918d-748559d549ec', '64a64f24-65b9-4b56-befd-5ec1eaced9b3'].map((g) => g.toLowerCase());
const activeGuid = ref('');
const isRogScheme = computed(() => ROG_GUIDS.includes(activeGuid.value.toLowerCase()));
const schemeDisplayName = computed(() => {
  const m: Record<string, string> = { besteff: '最佳能效', bestperf: '最佳性能', bal: '平衡', other: '其他电源方案' };
  return m[schemeKey.value] ?? '其他电源方案';
});
const CORE_MODE_OPTS = [
  { value: 'big', label: '大核为主(推荐)' },
  { value: 'only-big', label: '仅大核' },
  { value: 'only-small', label: '仅小核' },
];
// 活动核心数（Core Parking）：totalCores=物理核心总数（去 SMT；检测失败=0→禁用滑块）；activeCores=当前锁定的活动核心数
const totalCores = ref(0);
const activeCores = ref(0);
const busy = ref(false);
const errMsg = ref('');
// 参数是否成功检测：检测失败时禁用全部写入，避免"检测不到→默认值→误写回"的自主操作
const paramsOk = ref(true);

// 超线程 / SMT（与「活动核心数」Core Parking 正交：SMT 是启动层 bcdedit numproc，需重启生效）
const smtOn = ref(false); // 当前运行态（真实检测）
const smtCores = ref(0); // 物理核心数
const smtLogical = ref(0); // 逻辑处理器数
const smtConfigOn = ref<boolean | null>(null); // 下次启动态（null=未知/需管理员）
const smtBusy = ref(false);
const smtPending = ref<'' | 'on' | 'off'>(''); // 已预约但尚未重启生效的变更
const smtErr = ref('');
const smtInfo = ref(''); // 非错误提示（如"已处于关闭状态"）
// 点击超线程 Toggle 后弹出的"需重启生效"确认框
const smtDialogOpen = ref(false);
const smtDialogTarget = ref(false); // 待应用的目标值（true=开 / false=关）

// 活动核心数滑块右侧显示：当前物理核 + 对应线程数（SMT on 时 ×2，off 时 ×1）
const activeCoreValueText = computed(() => {
  if (totalCores.value < 1 || activeCores.value < 1) return undefined;
  if (smtCores.value < 1 || smtLogical.value < 1) return `${activeCores.value}核`;
  const threadsPerCore = smtLogical.value / smtCores.value;
  const threads = Math.round(activeCores.value * threadsPerCore);
  return `${activeCores.value}核 / ${threads}线程`;
});

// 弹窗文案用：基于检测到的物理核数动态演算，绝不再写死 16
const smtThreadsPerCore = computed(() => {
  if (smtCores.value > 0 && smtLogical.value > 0 && smtOn.value) {
    return Math.max(1, Math.round(smtLogical.value / smtCores.value));
  }
  return 2; // SMT 关闭时无法反推真实每核线程数，按桌面 x86 通用 2 路假设
});
const smtOnTargetText = computed(() => `${smtCores.value} 核 / ${smtCores.value * smtThreadsPerCore.value} 线程`);
const smtOffTargetText = computed(() => `${smtCores.value} 核 / ${smtCores.value} 线程`);

const acMax = computed(() => (acTurbo.value ? TURBO_MAX : baseMhz));
const dcMax = computed(() => (dcTurbo.value ? TURBO_MAX : baseMhz));
function freqText(mhz: number): string {
  return mhz === 0 ? '不限制' : (mhz / 1000).toFixed(1) + 'GHz';
}

async function refresh() {
  errMsg.value = '';
  // 并行异步加载所有数据（不串行等待，不阻塞渲染）
  const [cpuRes, schemeRes, paramsRes, yemanRes, coreRes, coreAcRes, coreDcRes, smtRes] = await Promise.allSettled([
    detectCpuName().then((name) => {
      // AMD Zen5 (9000系列): 9950X=4300, 9900X=4200, 9700X=3900
      if (/9950X|9985HX/i.test(name)) baseMhz = 4300;
      else if (/9900X/i.test(name)) baseMhz = 4200;
      else if (/9700X/i.test(name)) baseMhz = 3900;
      else if (/79[05]0/i.test(name)) baseMhz = 3800; // Zen4/4c
      else if (/Ryzen\s*\d+\s*series/i.test(name) || /Ryzen \d+ \d+H[XS]/i.test(name)) baseMhz = 3400;
      else if (/Intel.*[iI][579]/i.test(name)) baseMhz = 3000;
    }),
    (async () => {
      const guid = (await getActiveScheme()).toLowerCase();
      activeGuid.value = guid;
      const raw = await readPowerRaw();
      if (guid === PW.YEMAN.toLowerCase()) schemeKey.value = 'yeman';
      else if (guid === PW.WIN_SAVER.toLowerCase()) schemeKey.value = 'besteff';
      else if (guid === PW.WIN_HIGH.toLowerCase()) schemeKey.value = 'bestperf';
      else if (guid === PW.WIN_BAL.toLowerCase())
        schemeKey.value = raw.includes('BestEfficiency') ? 'besteff' : raw.includes('BestPerformance') ? 'bestperf' : 'bal';
      else schemeKey.value = 'other';
    })(),
    readPowerParams(),
    schemeExists(PW.YEMAN), // 只读：野蛮系统电源是否存在（缺失则点“野蛮系统电源”自动恢复，绝不删除）
    readPhysicalCores(), // 只读：物理核心总数（去 SMT；检测失败→0，滑块禁用）
    readCoreMode(true), // 只读：AC 大小核心调度
    readCoreMode(false), // 只读：DC 大小核心调度
    readSmt(), // 只读：超线程/SMT 实时检测（native GLPI，免提权、毫秒级）
  ]);
  if (yemanRes.status === 'fulfilled') yemanMissing.value = !yemanRes.value;
  // 物理核心总数 → 读回当前活动核心数（不阻塞其它数据）
  if (coreRes.status === 'fulfilled') {
    totalCores.value = coreRes.value;
    if (totalCores.value >= 1) {
      const c = await readActiveCoreCount(totalCores.value);
      if (c != null) activeCores.value = c;
    } else {
      activeCores.value = 0;
    }
  }
  if (coreAcRes.status === 'fulfilled' && coreAcRes.value) coreModeAc.value = coreAcRes.value;
  if (coreDcRes.status === 'fulfilled' && coreDcRes.value) coreModeDc.value = coreDcRes.value;
  // 超线程 / SMT：真实检测映射；预约态在重启后实装（live 与预约目标一致即清除提示）
  if (smtRes.status === 'fulfilled' && smtRes.value) {
    const s: SmtInfo = smtRes.value;
    smtOn.value = s.liveOn;
    smtCores.value = s.physicalCores;
    smtLogical.value = s.logicalProcs;
    smtConfigOn.value = s.configOn;
    if (smtPending.value === 'off' && !s.liveOn) smtPending.value = '';
    if (smtPending.value === 'on' && s.liveOn) smtPending.value = '';
  }
  // CPU 名称 → baseMhz（不阻塞其他数据）
  // 电源方案（已在上面的 Promise 中赋值）
  if (paramsRes.status === 'fulfilled' && paramsRes.value) {
    const p = paramsRes.value;
    acFreq.value = p.acFreq;
    dcFreq.value = p.dcFreq;
    acTurbo.value = p.acTurbo;
    dcTurbo.value = p.dcTurbo;
    acAggr.value = p.acAggr;
    dcAggr.value = p.dcAggr;
    paramsOk.value = true;
  } else {
    // 检测失败：禁用全部写入，绝不拿默认值去改写系统设置
    paramsOk.value = false;
    errMsg.value = 'CPU 调度参数检测失败，已禁用全部写入以防误操作。请点击「刷新」重试。';
  }
}

async function applyAll() {
  // 非野蛮方案时禁用所有写入操作（只有重制电源可用）
  if (!isYemanScheme.value) return;
  // 检测失败时不写回任何值（防止默认值覆盖真实设置）——程序不做自主操作
  if (!paramsOk.value) return;
  errMsg.value = '';
  busy.value = true;
  try {
    await applyPowerParams({
      acFreq: acFreq.value,
      dcFreq: dcFreq.value,
      acTurbo: acTurbo.value,
      dcTurbo: dcTurbo.value,
      acAggr: acAggr.value,
      dcAggr: dcAggr.value,
    });
  } catch (e) {
    errMsg.value = 'CPU 参数下发失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

function onFreqCommit() {
  void applyAll();
  void syncLock(); // 锁定态：把新值同步进 cpu_lock.json，保持"锁住的就是眼前这套"
  scheduleActivate(); // 拖动结束后 2 秒用 Windows 命令重新激活方案，使设置真正生效（无回读、无闪烁）
}
function onAcTurbo(v: boolean) {
  acTurbo.value = v;
  if (acFreq.value > acMax.value) acFreq.value = acMax.value;
  void applyAll();
  void syncLock();
  scheduleActivate(); // 拖动/切换后 2 秒用 Windows 命令重新激活方案，使设置真正生效（无回读、无闪烁）
}
function onDcTurbo(v: boolean) {
  dcTurbo.value = v;
  if (dcFreq.value > dcMax.value) dcFreq.value = dcMax.value;
  void applyAll();
  void syncLock();
  scheduleActivate(); // 拖动/切换后 2 秒用 Windows 命令重新激活方案，使设置真正生效（无回读、无闪烁）
}
function onAggrCommit() {
  void applyAll();
  void syncLock();
  scheduleActivate(); // 拖动结束后 2 秒用 Windows 命令重新激活方案，使设置真正生效（无回读、无闪烁）
}
// 锁定态下改动滑块/睿频 → 同步刷新 cpu_lock.json（未锁定时为空操作）
function syncLock() {
  if (!cpuLocked.value) return Promise.resolve();
  return updateCpuLock(currentLockPayload()).catch(() => {});
}

// 拖动滑块后延迟"激活方案"：写值只改注册表，必须重新激活方案( powercfg /setactive <YEMAN> )
// 操作系统才会真正套用——这正是 Windows 命令切换电源起效的关键，且不回读 UI，零闪烁。
// 若 2 秒内再次拖动，定时器重置，等最后一次拖动结束后 2 秒再激活——避免频繁拖动反复激活卡死。
let activateTimer: number | null = null;
function scheduleActivate() {
  if (activateTimer !== null) window.clearTimeout(activateTimer);
  activateTimer = window.setTimeout(() => {
    activateTimer = null;
    // 重新激活野蛮方案，使刚写入的所有电源参数生效（AC/DC 全覆盖）
    setActiveScheme(PW.YEMAN).catch(() => {});
  }, 2000);
}

async function onScheme(key: SchemeKey) {
  if (key === 'other' as SchemeKey) return; // 不允许切换到 'other'
  errMsg.value = '';
  busy.value = true;
  try {
    schemeKey.value = key;
    await switchScheme(key);
  } catch (e) {
    errMsg.value = '切换电源方案失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function onCoreAc(mode: CoreMode) {
  if (!isYemanScheme.value) return;
  if (!paramsOk.value) return;
  errMsg.value = '';
  busy.value = true;
  try {
    coreModeAc.value = mode;
    await setCoreModeAc(mode);
    scheduleActivate();
  } catch (e) {
    errMsg.value = 'AC 大小核调度失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}
async function onCoreDc(mode: CoreMode) {
  if (!isYemanScheme.value) return;
  if (!paramsOk.value) return;
  errMsg.value = '';
  busy.value = true;
  try {
    coreModeDc.value = mode;
    await setCoreModeDc(mode);
    scheduleActivate();
  } catch (e) {
    errMsg.value = 'DC 大小核调度失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// 活动核心数滑块提交：统一 AC/DC 写入最小/最大核心数%（锁死活动核心数），2 秒后重新激活方案生效
function onCoreCountCommit() {
  if (!isYemanScheme.value || !paramsOk.value || totalCores.value < 1) return;
  errMsg.value = '';
  busy.value = true;
  setActiveCoreCount(activeCores.value, totalCores.value)
    .then(() => scheduleActivate())
    .catch((e) => { errMsg.value = '活动核心数设置失败：' + (e as Error).message; })
    .finally(() => { busy.value = false; });
}
// 超线程 / SMT：点击 Toggle 先弹窗提示"需重启生效"，确认后才预约（注册表 FeatureSettingsOverride 0x40 位，需重启生效）
function onSmt(v: boolean) {
  if (!isYemanScheme.value || !paramsOk.value) return;
  smtDialogTarget.value = v;
  smtDialogOpen.value = true;
}
function cancelSmt() {
  smtDialogOpen.value = false;
  smtDialogTarget.value = false;
  // smtOn 不变，Toggle 视觉自动回到原状态
}
async function confirmSmt() {
  const v = smtDialogTarget.value;
  smtDialogOpen.value = false;
  smtDialogTarget.value = false;
  if (smtBusy.value) return;
  smtBusy.value = true;
  smtErr.value = '';
  try {
    const res = await setSmt(v);
    if (res.ok) {
      smtOn.value = v; // 立即反映预约态（live 需重启才变）
      smtPending.value = v ? 'on' : 'off';
      smtConfigOn.value = v;
      smtErr.value = '';
      smtInfo.value = res.info ? String(res.info) : '';
    } else {
      smtErr.value = '超线程设置失败：' + (res.error || '未知错误（可能需管理员权限）');
    }
  } catch (e: any) {
    smtErr.value = '超线程设置失败：' + (e?.message ?? '');
  } finally {
    smtBusy.value = false;
  }
}

const resetPath = ref('');
const resetInfo = ref('');
// 重制方案下拉项（含占位禁用项；对齐原 <select> 的 placeholder）
const resetOpts = computed(() => [
  { value: '', label: '选择重制方案', disabled: true },
  ...RESET_PROFILES.map((p) => ({ value: p.path, label: p.name, sub: p.sub })),
]);
// 选中预设即直接应用（UAC 弹窗由 bat 自提权产生，无需额外的"应用"按钮）
function onResetPick(v: string) {
  if (!v || busy.value) return; // 占位禁用项 / 正在应用时忽略
  resetPath.value = v;
  resetInfo.value = ''; // 清除上一次的"已应用"提示
  applyReset(v);
}
async function applyReset(path: string) {
  const prof = RESET_PROFILES.find((p) => p.path === path);
  errMsg.value = '';
  resetInfo.value = '';
  busy.value = true;
  try {
    await runResetProfile(path);
    scheduleActivate(); // 重制电源后 2 秒重新激活方案，使重制后的参数真正生效（无回读、无闪烁）
    await refresh();
    resetInfo.value = `已应用并重制：${prof?.name ?? ''}`;
  } catch (e) {
    errMsg.value = '重制电源失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// 异步加载数据（nextTick 确保先渲染默认值，避免切页卡顿）
// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  import('vue').then(({ watch }) => watch(globalRefreshKey, () => refresh()));
}

// ── 主频/积极性锁定（AC 卡片右上角锁图标）──
// 锁定后：AC/DC 最大主频 + 调度积极性 写入 cpu_lock.json，浮动优化不再联动，
// 关闭自动优化 / 拔插电源时自动套用锁定值。
const cpuLocked = ref(isCpuLocked());
const lockBusy = ref(false);
// 锁定态变化且浮动未启用时，把滑块直接回显锁定值（用户能直观看到「已恢复到配置」）
function reflectLockToSliders() {
  if (!cpuLocked.value || floatOn.value) return;
  const c = getCpuLock();
  acFreq.value = c.acFreq;
  dcFreq.value = c.dcFreq;
  acAggr.value = c.acAggr;
  dcAggr.value = c.dcAggr;
  acTurbo.value = c.acTurbo;
  dcTurbo.value = c.dcTurbo;
}
const offCpuLock = onCpuLockUpdate((c) => {
  cpuLocked.value = c.locked;
  reflectLockToSliders();
});
function currentLockPayload() {
  return {
    acFreq: acFreq.value,
    dcFreq: dcFreq.value,
    acAggr: acAggr.value,
    dcAggr: dcAggr.value,
    acTurbo: acTurbo.value,
    dcTurbo: dcTurbo.value,
  };
}
async function onToggleLock() {
  if (!isYemanScheme.value || !paramsOk.value || lockBusy.value) return;
  lockBusy.value = true;
  try {
    if (cpuLocked.value) {
      // 解锁：清掉锁定位。浮动开着时它本来就在联动（锁定不影响），无需额外动作。
      await clearCpuLock();
    } else {
      // 锁定 = 写入「当前（非自动优化）」的 AC/DC 主频+积极性：
      // 浮动开着时滑块显示的是浮动动态值 → 必须取开启前的用户快照；浮动关着直接取面板值
      const snap = floatOn.value ? getFloatSnapshot() : null;
      const payload = snap
        ? { acFreq: snap.acFreq, dcFreq: snap.dcFreq, acAggr: snap.acAggr, dcAggr: snap.dcAggr, acTurbo: snap.acTurbo, dcTurbo: snap.dcTurbo }
        : currentLockPayload();
      // 浮动优先：开着时只写配置（浮动继续接管），关闭状态才立即套用一次锁定值
      await setCpuLock(payload, !floatOn.value);
      // 浮动关闭时滑块立即显示锁定值；浮动开着则继续跟随浮动，不覆盖
      if (!floatOn.value) {
        acFreq.value = payload.acFreq;
        dcFreq.value = payload.dcFreq;
        acAggr.value = payload.acAggr;
        dcAggr.value = payload.dcAggr;
        acTurbo.value = payload.acTurbo;
        dcTurbo.value = payload.dcTurbo;
      }
    }
  } finally {
    lockBusy.value = false;
  }
}

// ── 自动CPU浮动优化（控制循环在 autofloat.ts 模块单例上，切页不中断）──
const floatInfo = ref<FloatInfo>(getFloatInfo());
const floatOn = computed(() => floatInfo.value.enabled);
const offFloatUpdate = onFloatUpdate((info) => {
  floatInfo.value = info;
  if (info.enabled) {
    // 优先级：自动CPU浮动优化开启 = 最高。无论是否锁定，滑块都跟随浮动动态值回显。
    acFreq.value = info.state.freq;
    dcFreq.value = info.state.freq;
    acAggr.value = info.state.aggr;
    dcAggr.value = info.state.aggr;
    if (info.hwinfoDown && errMsg.value === '') {
      errMsg.value = 'HWiNFO 未开启或共享内存不可用 —— 已尝试自动修复，请手动打开 HWiNFO 并在「传感器设置」中启用「共享内存支持」。';
    }
  }
});
onUnmounted(offFloatUpdate);
onUnmounted(offCpuLock);

const FLOAT_TARGET_OPTS = [
  { value: 30, label: '30' },
  { value: 45, label: '45' },
  { value: 60, label: '60' },
  { value: 90, label: '90' },
];
const FLOAT_PROFILE_OPTS = (Object.keys(FLOAT_PROFILES) as FloatProfile[]).map((k) => ({
  value: k,
  label: FLOAT_PROFILES[k].name,
}));

async function syncRtssFps(target: number) {
  // 同步 RTSS 锁帧数字：只下发实时 RTSS 限制，不写入 FPS-ac.txt。
  // FPS-ac.txt 是用户在「监控/锁帧」页手动设的 AC 锁帧持久记忆，自动浮动改 RTSS 时绝不能覆盖它
  // （否则自动浮动关闭后用户手动值就丢了，与 B1 DC 修复同理）。
  try {
    const rtssEnabled = (await readRtssLimit()) > 0;
    if (rtssEnabled) await setRtssLimit(target);
  } catch { /* 忽略 */ }
}

function refreshRtssAfterFloatChange() {
  // RTSS 页面通过全局刷新触发器重新读取实时锁帧值，避免必须重启应用才能看到新数字。
  if (globalRefreshKey) globalRefreshKey.value++;
}

function onFloatTarget(v: number) {
  if (!isYemanScheme.value || !paramsOk.value) return;
  const t = v as FpsTarget;
  const wasOn = floatOn.value; // 先捕获切换前状态，避免乐观更新后误判
  // 乐观更新 UI，后台异步执行实际开关/切目标，避免 SegButton 等待
  floatInfo.value = { ...floatInfo.value, target: t, enabled: true };
  if (wasOn) {
    setFloatTarget(t);
    void syncRtssFps(t).finally(refreshRtssAfterFloatChange);
  } else {
    void enableFloat(t, floatInfo.value.profile)
      .then(() => syncRtssFps(t))
      .finally(refreshRtssAfterFloatChange);
  }
}
function onFloatProfile(v: string) {
  if (!isYemanScheme.value) return;
  const p = v as FloatProfile;
  floatInfo.value = { ...floatInfo.value, profile: p };
  void setFloatProfile(p);
}

const FLOAT_ACTION_TEXT: Record<string, string> = {
  'down-aggr': '↓ 降积极性',
  'down-freq': '↓ 降主频',
  'up-freq': '↑ 升主频',
  'up-aggr': '↑ 升积极性',
  hold: '稳定',
  idle: '待机',
  wait: '监控启动中…',
};
// 监控显示：两排、居中
//   第一排：游戏名 - FPS 实际（1%Low） GPU 占用
//   第二排：上限 X.XGHz  积极性 XX  ✳动作
const floatLine1 = computed(() => {
  const i = floatInfo.value;
  if (!i.enabled) return '';
  if (i.hwinfoDown) return 'HWiNFO 共享内存不可用';
  const s = i.status;
  if (!s) return '监控启动中…';
  if (!s.game || s.fps <= 0) return `待机 · 未检测到游戏 · GPU ${s.gpu}%`;
  const low = s.fps1 > 0 && s.fps > 0 && s.fps1 < s.fps * 0.6 ? '' : '';
  return `${s.game} - FPS ${s.fps}（${s.fps1}）${low} GPU ${s.gpu}%`;
});
const floatLine2 = computed(() => {
  const i = floatInfo.value;
  if (!i.enabled) return '';
  if (i.hwinfoDown) return '请启动 HWiNFO 并开启「共享内存支持」';
  const act = FLOAT_ACTION_TEXT[i.lastAction] ?? '';
  // 浮动优先：开启时第二排始终显示浮动实际控制量；锁定只是待命（关闭浮动后才套用）
  return `上限 ${(i.state.freq / 1000).toFixed(1)}GHz  积极性 ${i.state.aggr}  · ${act}`;
});

// ── 自动启用（CPU 优化按电源状态自动激活）──
const AUTO_ENABLE_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_auto_enable.json';
const AUTO_ENABLE_OPTS = [
  { value: 'never', label: '从不' },
  { value: 'always', label: '总是' },
  { value: 'ac', label: '接通电源' },
  { value: 'dc', label: '使用电池' },
];
const autoEnable = ref<string>('never');
async function loadAutoEnable() {
  try {
    const raw = await fs.readTextFile(AUTO_ENABLE_FILE, 64);
    const j = JSON.parse(raw);
    if (typeof j.mode === 'string') autoEnable.value = j.mode;
  } catch { /* 文件不存在或格式无效，就用默认 never */ }
}
async function saveAutoEnable(v: string) {
  autoEnable.value = v;
  try {
    await fs.writeTextFile(AUTO_ENABLE_FILE, JSON.stringify({ mode: v }));
  } catch { /* 忽略写入失败 */ }
  // 变更自动启用模式后，立即按新模式+当前电源状态启用/禁用浮动优化
  await applyCpuAutoEnable().catch(() => {});
  // 模块状态变更后回显
  floatInfo.value = getFloatInfo();
  // 关闭浮动后若已锁定，把滑块回显锁定值（恢复到配置文件）
  reflectLockToSliders();
  // 联动刷新：让「监控/锁帧」页的 AC/DC 锁帧任务锁定状态立即跟随本次下拉选择
  // （从不=两边都可调；总是=两边都锁；接通电源=锁 AC；使用电池=锁 DC）
  refreshRtssAfterFloatChange();
}

onMounted(() =>
  nextTick(async () => {
    await refresh();
    await loadAutoEnable();
    // 载入锁定配置（锁定态下滑块保持可用，浮动优化不联动）
    try {
      const lk = await loadCpuLock();
      cpuLocked.value = lk.locked;
      if (lk.locked) {
        acFreq.value = lk.acFreq;
        dcFreq.value = lk.dcFreq;
        acAggr.value = lk.acAggr;
        dcAggr.value = lk.dcAggr;
      }
    } catch { /* 忽略 */ }
    // 按「自动启用」设置 + 当前电源状态，自动启用/禁用浮动优化
    try {
      await applyCpuAutoEnable();
      floatInfo.value = getFloatInfo();
    } catch {
      /* 忽略：恢复失败不影响其它功能 */
    }
  })
);
</script>

<template>
  <div class="page">
    <div v-if="!isYemanScheme" class="warn-bar">
      <template v-if="isRogScheme">识别到「ROG奥创专用电源」——当前电源方案不是「野蛮系统电源」，请切换为「野蛮系统电源」</template>
      <template v-else>当前电源方案：{{ schemeDisplayName }}（非「野蛮系统电源」），请切换为「野蛮系统电源」<br />「野蛮系统电源」不会修改原版系统电源调度机制。</template>
    </div>
    <div v-if="yemanMissing" class="warn-bar"><InlineIcon name="warning" /> 未检测到「野蛮系统电源」方案，点击下方「野蛮系统电源」将自动从备份（YM.pow）恢复，不会删除任何现有电源方案。</div>
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div v-if="resetInfo" class="ok-bar">{{ resetInfo }}</div>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="settings" /> 当前电源方案</h3>
      <div class="seg-wrap">
        <SegButton
          :model-value="schemeKey"
          :options="SCHEMES.map((s) => ({ value: s.key, label: s.name }))"
          color="accent"
          full
          :disabled="busy"
          @update:model-value="(v: string) => onScheme(v as SchemeKey)"
        />
      </div>
    </section>

    <section class="card" :class="{ disabled: !isYemanScheme }">
      <div class="float-head">
        <span class="float-title"><InlineIcon name="cpufloat" /> 自动CPU浮动优化</span>
        <div class="float-head-right">
          <Dropdown
            :model-value="autoEnable"
            :options="AUTO_ENABLE_OPTS"
            color="accent"
            width="120px"
            @update:model-value="(v: string) => saveAutoEnable(v)"
          />
        </div>
      </div>
      <template v-if="autoEnable !== 'never'">
        <div class="float-row">
          <span class="row-label">帧数目标</span>
          <SegButton
            :model-value="floatInfo.target"
            :options="FLOAT_TARGET_OPTS"
            color="accent"
            full
            :disabled="!isYemanScheme || !paramsOk"
            @update:model-value="(v: string | number) => onFloatTarget(Number(v))"
          />
        </div>
        <div class="float-row">
          <span class="row-label">调度档位（CPU最大主频浮动范围 GHz）</span>
          <SegButton
            :model-value="floatInfo.profile"
            :options="FLOAT_PROFILE_OPTS"
            color="accent"
            full
            :disabled="!isYemanScheme"
            @update:model-value="(v: string | number) => onFloatProfile(String(v))"
          />
        </div>
        <div v-if="floatOn" class="float-status">
          <div class="float-status-line">{{ floatLine1 }}</div>
          <div class="float-status-line float-status-line2">{{ floatLine2 }}</div>
        </div>
        <div v-if="floatOn" class="float-hint">
          {{ cpuLocked
            ? '自动模式优先接管中：滑块跟随浮动显示；关闭自动优化后套用锁定值。'
            : '自动模式接管中：下方主频/积极性滑块跟随显示，手动拖动已锁定；关闭后恢复开启前设置。' }}
        </div>
        <div v-else class="float-hint">
          {{ autoEnable === 'always' ? '等待游戏中自动调节 CPU' : autoEnable === 'ac' ? '接通电源后自动启用' : '使用电池时自动启用' }}
        </div>
      </template>
    </section>

    <section class="card" :class="{ disabled: !isYemanScheme }">
      <div class="freq-head">
        <span class="freq-label"><InlineIcon name="plug" /> AC CPU最大主频</span>
        <div class="freq-right">
          <Toggle v-model="acTurbo" :label="'睿频'" :description="acTurbo ? '已开启' : '已关闭'" color="ac" :disabled="!isYemanScheme || !paramsOk" @update:model-value="onAcTurbo" compact />
          <span class="freq-val">{{ freqText(acFreq) }}</span>
          <button
            type="button"
            class="lock-btn"
            :class="{ 'is-locked': cpuLocked }"
            :disabled="!isYemanScheme || !paramsOk || lockBusy"
            :title="cpuLocked ? '已锁定 AC/DC 主频与积极性，点击解锁' : '锁定当前 AC/DC 主频与积极性（写入配置；自动CPU浮动优化开启时以浮动优先）'"
            :aria-label="cpuLocked ? '解锁主频与积极性' : '锁定主频与积极性'"
            @click="onToggleLock"
          ><InlineIcon :name="cpuLocked ? 'lock' : 'unlock'" /></button>
        </div>
      </div>
      <Slider v-model="acFreq" :min="0" :max="acMax" :step="100" :label="''" :unit="'MHz'" color="ac" :disabled="!isYemanScheme || !paramsOk || floatOn" @commit="onFreqCommit" />
      <Slider v-model="acAggr" :min="0" :max="100" :step="1" :label="'AC CPU主频调度积极性'" color="ac" :disabled="!isYemanScheme || !paramsOk || floatOn" @commit="onAggrCommit" />
    </section>

    <section class="card" :class="{ disabled: !isYemanScheme }">
      <div class="freq-head">
        <span class="freq-label"><InlineIcon name="battery" /> DC CPU最大主频</span>
        <div class="freq-right">
          <Toggle v-model="dcTurbo" :label="'睿频'" :description="dcTurbo ? '已开启' : '已关闭'" color="dc" :disabled="!isYemanScheme || !paramsOk" @update:model-value="onDcTurbo" compact />
          <span class="freq-val">{{ freqText(dcFreq) }}</span>
          <button
            type="button"
            class="lock-btn"
            :class="{ 'is-locked': cpuLocked }"
            :disabled="!isYemanScheme || !paramsOk || lockBusy"
            :title="cpuLocked ? '已锁定 AC/DC 主频与积极性，点击解锁（交回浮动优化）' : '锁定当前 AC/DC 主频与积极性（写入配置；自动CPU浮动优化开启时以浮动优先）'"
            :aria-label="cpuLocked ? '解锁主频与积极性' : '锁定主频与积极性'"
            @click="onToggleLock"
          ><InlineIcon :name="cpuLocked ? 'lock' : 'unlock'" /></button>
        </div>
      </div>
      <Slider v-model="dcFreq" :min="0" :max="dcMax" :step="100" :label="''" :unit="'MHz'" color="dc" :disabled="!isYemanScheme || !paramsOk || floatOn" @commit="onFreqCommit" />
      <Slider v-model="dcAggr" :min="0" :max="100" :step="1" :label="'DC CPU主频调度积极性'" color="dc" :disabled="!isYemanScheme || !paramsOk || floatOn" @commit="onAggrCommit" />
    </section>

    <section class="card core-sched-card" :class="{ disabled: !isYemanScheme }">
      <div class="core-head">
        <span class="core-title"><InlineIcon name="wrench" /> 核心调度</span>
        <div class="core-head-right">
          <Toggle
            :model-value="smtOn"
            :label="'超线程'"
            :description="smtOn ? '已开启' : '已关闭'"
            color="accent"
            :disabled="!isYemanScheme || !paramsOk || smtBusy"
            @update:model-value="onSmt"
            compact
          />
        </div>
      </div>

      <template v-if="totalCores >= 1">
        <Slider
          v-model="activeCores"
          :min="1"
          :max="totalCores"
          :step="1"
          label="活动核心数"
          :valueText="activeCoreValueText"
          color="accent"
          :disabled="!isYemanScheme || !paramsOk"
          @commit="onCoreCountCommit"
        />
      </template>
      <div v-else class="hint">活动核心数检测失败，滑块已禁用。</div>

      <div class="core-mode-block">
        <div class="core-mode-row">
          <span class="row-label"><InlineIcon name="plug" /> AC 大小核心调度</span>
          <SegButton
            v-model="coreModeAc"
            :options="CORE_MODE_OPTS"
            color="ac"
            full
            :disabled="!isYemanScheme || !paramsOk"
            @update:model-value="(v: string) => onCoreAc(v as CoreMode)"
          />
        </div>
        <div class="core-mode-row">
          <span class="row-label"><InlineIcon name="battery" /> DC 大小核心调度</span>
          <SegButton
            v-model="coreModeDc"
            :options="CORE_MODE_OPTS"
            color="dc"
            full
            :disabled="!isYemanScheme || !paramsOk"
            @update:model-value="(v: string) => onCoreDc(v as CoreMode)"
          />
        </div>
      </div>

      <div v-if="smtPending" class="core-smt-hint"><InlineIcon name="warning" /> 已预约{{ smtPending === 'off' ? '关闭' : '开启' }}，重启后生效</div>
      <div v-if="smtInfo" class="core-smt-info">{{ smtInfo }}</div>
      <div v-if="smtErr" class="core-smt-err">{{ smtErr }}</div>
    </section>

    <!-- 超线程 / SMT 重启提示弹窗 -->
    <Teleport to="body">
      <Transition name="smt-fade">
        <div v-if="smtDialogOpen" class="smt-modal-mask" @click.self="cancelSmt">
          <div class="smt-modal" role="dialog" aria-modal="true">
            <div class="smt-modal-title">超线程 / SMT</div>
            <div class="smt-modal-body">
              <div class="smt-modal-msg">超线程修改后需重启生效</div>
              <div class="smt-modal-sub">
                <template v-if="smtDialogTarget">
                  开启超线程：重启后恢复每物理核 {{ smtThreadsPerCore }} 线程（{{ smtOnTargetText }}）。
                </template>
                <template v-else>
                  关闭超线程：通过注册表 FeatureSettingsOverride 的缓解位实现，重启后系统会为每个物理核心只启用 1 个线程（{{ smtOffTargetText }}），是真正关闭超线程。<br />
                  此方式不按固件枚举顺序砍核，比 bcdedit numproc 更可靠；但仍需以管理员身份运行本程序，且更改需重启生效。
                </template>
              </div>
            </div>
            <div class="smt-modal-actions">
              <button class="smt-btn smt-btn-cancel" type="button" @click="cancelSmt">取消</button>
              <button class="smt-btn smt-btn-ok" type="button" @click="confirmSmt">确定</button>
            </div>
          </div>
        </div>
      </Transition>
    </Teleport>

    <!-- CPU 核心控制（CCD / 省电停驻）：自动验证支持，否则整块隐藏 -->
    <CcdCard />

    <!-- CPU 降压（Undervolt）：自动验证支持，否则整块隐藏 -->
    <UndervoltCard />

    <section class="card" :class="{ disabled: !isYemanScheme }">
      <div class="row reset-row">
        <span class="reset-label">重制电源方案</span>
        <Dropdown
          :model-value="resetPath"
          :options="resetOpts"
          color="accent"
          aria-label="重制电源方案"
          placeholder="选择重制方案"
          @update:model-value="(v: string) => onResetPick(v)"
        />
      </div>
    </section>
  </div>
</template>

<style scoped>
.page {
  padding-bottom: 20px;
}
.warn-bar {
  background: rgba(245,185,61,.12);
  border: 1px solid rgba(245,185,61,.4);
  color: #f5b93d;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.err-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.ok-bar {
  background: rgba(46, 166, 255, 0.12);
  border: 1px solid rgba(46, 166, 255, 0.4);
  color: #7dd3fc;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 8px;
}
.scheme-name {
  font-size: 13px;
  font-weight: 600;
}
.seg-wrap {
  margin-top: 6px;
}
.freq-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
}
.freq-right {
  display: flex;
  align-items: center;
  gap: 8px;
}
.freq-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
}
.freq-val {
  font-size: 13px;
  font-weight: 600;
  color: var(--accent);
}
.reset-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 8px;
}
.reset-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
  white-space: nowrap;
}
.reset-select {
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: 6px;
  padding: 5px 8px;
  font-size: 12px;
  flex: 1;
  min-width: 0;
}
.action-btn {
  width: 100%;
  background: var(--accent);
  color: #06121d;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: 9px;
  font-weight: 700;
  font-size: 12px;
  cursor: pointer;
  margin-top: 8px;
}
.action-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.action-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.core-mode-block {
  margin-top: 14px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.core-mode-row {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.row-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
}
.core-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
}
.core-head-right {
  display: flex;
  align-items: center;
  gap: 8px;
}
.core-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
}
.core-smt-hint {
  font-size: 11px;
  color: #f5b93d;
  margin-top: 12px;
  line-height: 1.4;
}
.core-smt-info {
  font-size: 11px;
  color: #8a97a8;
  margin-top: 12px;
  line-height: 1.4;
}
.core-smt-err {
  font-size: 11px;
  color: #ff9ea1;
  margin-top: 12px;
  line-height: 1.4;
}
.hint {
  font-size: 11px;
  color: #f5b93d;
  margin-top: 8px;
}
.card.disabled {
  opacity: 0.5;
  pointer-events: none;
}
/* 超线程重启提示弹窗 */
.smt-modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  backdrop-filter: blur(2px);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  padding: 16px;
}
.smt-modal {
  width: 100%;
  max-width: 300px;
  background: #121a26;
  border: 1px solid #2a3342;
  border-radius: 10px;
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.5);
  padding: 16px;
}
.smt-modal-title {
  font-size: 13px;
  font-weight: 700;
  color: var(--text);
  margin-bottom: 10px;
}
.smt-modal-msg {
  font-size: 13px;
  color: var(--text);
  line-height: 1.5;
}
.smt-modal-sub {
  font-size: 11px;
  color: #8a97a8;
  margin-top: 6px;
  line-height: 1.4;
}
.smt-modal-actions {
  display: flex;
  gap: 10px;
  margin-top: 16px;
}
.smt-btn {
  flex: 1;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: 9px;
  font-size: 12px;
  font-weight: 700;
  cursor: pointer;
}
.smt-btn-cancel {
  background: #1f2937;
  color: var(--text);
  border: 1px solid #2a3342;
}
.smt-btn-ok {
  background: var(--accent);
  color: #06121d;
}
.smt-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.smt-fade-enter-active,
.smt-fade-leave-active {
  transition: opacity 0.15s ease;
}
.smt-fade-enter-from,
.smt-fade-leave-to {
  opacity: 0;
}

/* 自动CPU浮动优化 */
.float-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
}
.float-title {
  font-size: 13px;
  font-weight: 700;
  color: var(--text);
}
.float-head-right {
  display: flex;
  align-items: center;
  gap: 8px;
}
.float-sub { font-size: 10px; font-weight: 400; color: #8a97a8; }
.float-row { display: flex; flex-direction: column; gap: 6px; margin-top: 10px; }
.float-status {
  margin-top: 10px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}
.float-status-line {
  font-size: 12px;
  font-weight: 600;
  color: #7dd3fc;
  text-align: center;
  line-height: 1.45;
  word-break: break-all;
}
.float-status-line2 {
  font-size: 11px;
  font-weight: 500;
  color: #9fb3c8;
}
.float-hint { font-size: 10px; color: #8a97a8; margin-top: 6px; line-height: 1.4; text-align: center; }

/* 主频/积极性锁定按钮 */
.lock-btn {
  width: 26px;
  height: 26px;
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  line-height: 1;
  border-radius: 7px;
  border: 1px solid #2a3342;
  background: #182231;
  color: var(--text);
  cursor: pointer;
  font-family: inherit;
  transition: background 0.12s, border-color 0.12s, transform 0.1s;
}
.lock-btn:hover:not(:disabled) { background: #1f2b3d; border-color: #3a4a5e; }
.lock-btn:active:not(:disabled) { transform: scale(0.92); }
.lock-btn:focus-visible { outline: none; box-shadow: var(--focus-ring); border-color: var(--accent); }
.lock-btn:disabled { opacity: 0.45; cursor: not-allowed; }
.lock-btn.is-locked {
  background: rgba(245, 185, 61, 0.16);
  border-color: rgba(245, 185, 61, 0.55);
}
</style>
