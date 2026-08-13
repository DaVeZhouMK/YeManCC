<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, onActivated, nextTick, inject, watch, type Ref } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
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
  applyManualPowerParams,
  applyThrottleLink,
  type CoreMode,
  setCoreModeAc,
  setCoreModeDc,
  readCoreMode,
  setActiveCoreCount,
  readActiveCoreCount,
  readPhysicalCores,
  runResetProfile,
  rebuildYemanScheme,
  setActiveScheme,
  readPowerRaw,
  detectCpuName,
  detectCoreArchitecture,
  type CoreArchitectureInfo,
  readSmt,
  setSmt,
  type SmtInfo,
} from '@/bridge/yeman';
import {
  type FloatInfo,
  getFloatInfo,
  onFloatUpdate,
} from '@/bridge/autofloat';
import { on } from '@/bridge/ipc';
import {
  CPU_PROFILE_META,
  type CpuProfileId,
  type CpuProfilesConfig,
  cpuProfilesFileExists,
  loadCpuProfiles,
  saveCpuProfiles,
  getCpuProfileMeta,
} from '@/bridge/cpuProfiles';
// ── 基准频率：根据实际 CPU 动态检测，默认 AMD Zen5 9950X = 4300 MHz ──
// 核心原则（写入 MD）：打开页面 / 切换页面只读取状态，绝不执行任何设置操作！
const BASE_MHZ_DEFAULT = 4300; // 9950X 基准频率 4.3GHz
const TURBO_MAX = 7200;
// 基准频率必须是响应式：detectCpuName 检测完成后更新，Slider 上限即时跟随
// （此前普通 let 变量导致检测成功后 computed 不重算、主频上限卡在默认 4300 —— 2026-08-05 修复）
const baseMhz = ref(BASE_MHZ_DEFAULT);

// 扩展 SchemeKey：unknown/rog 仅用于显示，绝不作为可选项写回系统。
type ExtendedSchemeKey = SchemeKey | 'unknown' | 'rog';
const schemeKey = ref<ExtendedSchemeKey>('yeman');
const isYemanScheme = computed(() => schemeKey.value === 'yeman');
// 只读检测：野蛮系统电源方案是否存在（缺失时点一下“野蛮系统电源”会自动从 YM.pow 恢复，绝不删除）
const yemanMissing = ref(false);

const acFreq = ref(0); // MHz，0 = 不限制
const dcFreq = ref(0);
// ⚠️ 默认值必须是有利/安全的：睿频默认开启（true），避免"打开就关睿频"
const acTurbo = ref(true);
const dcTurbo = ref(true);
const acAggr = ref(100);
const dcAggr = ref(100);

const cpuProfiles = ref<CpuProfilesConfig | null>(null);
const activeCpuProfile = ref<CpuProfileId>('balanced');
const activeCpuProfileLabel = computed(() => getCpuProfileMeta(activeCpuProfile.value).label);

const coreModeAc = ref<CoreMode>('big');
const coreModeDc = ref<CoreMode>('big');
const coreArchitecture = ref<CoreArchitectureInfo | null>(null);
const isHeterogeneousCpu = computed(() => coreArchitecture.value?.heterogeneous === true);
// ROG 奥创专用电源 GUID（识别到则顶部提示特别标注）
const ROG_GUIDS = ['6fecc5ae-f350-48a5-b669-b472cb895ccf', '27fa6203-3987-4dcc-918d-748559d549ec', '64a64f24-65b9-4b56-befd-5ec1eaced9b3'].map((g) => g.toLowerCase());
const activeGuid = ref('');
const isRogScheme = computed(() => schemeKey.value === 'rog');
const schemeDisplayName = computed(() => {
  const m: Record<string, string> = { yeman: '野蛮系统电源', besteff: '最佳能效', bestperf: '最佳性能', bal: '平衡', rog: '奥创', unknown: '未知电源' };
  return m[schemeKey.value] ?? '未知电源';
});
let refreshInProgress = false;
function onCpuViewActivated(): void {
  // 页面激活只做一次只读刷新，不主动切换电源方案。
  void refresh();
}
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

const acMax = computed(() => (acTurbo.value ? TURBO_MAX : baseMhz.value));
const dcMax = computed(() => (dcTurbo.value ? TURBO_MAX : baseMhz.value));
function freqText(mhz: number): string {
  return mhz === 0 ? '不限制' : (mhz / 1000).toFixed(1) + 'GHz';
}

async function refreshScheme() {
  // 读取当前电源方案并回显（guid → 名称映射）
  const guid = (await getActiveScheme()).toLowerCase();
  activeGuid.value = guid;
  const raw = await readPowerRaw();
  if (guid === PW.YEMAN.toLowerCase()) schemeKey.value = 'yeman';
  else if (ROG_GUIDS.includes(guid)) schemeKey.value = 'rog';
  else if (guid === PW.WIN_SAVER.toLowerCase()) schemeKey.value = 'besteff';
  else if (guid === PW.WIN_HIGH.toLowerCase()) schemeKey.value = 'bestperf';
  else if (guid === PW.WIN_BAL.toLowerCase())
    schemeKey.value = raw.includes('BestEfficiency') ? 'besteff' : raw.includes('BestPerformance') ? 'bestperf' : 'bal';
  else schemeKey.value = 'unknown';
}

async function refresh() {
  if (refreshInProgress) return;
  refreshInProgress = true;
  errMsg.value = '';
  // 并行异步加载所有数据（不串行等待，不阻塞渲染）
  const [cpuRes, schemeRes, paramsRes, yemanRes, coreRes, smtRes, architectureRes] = await Promise.allSettled([
    detectCpuName().then((name) => {
      // AMD Zen5 (9000系列): 9950X=4300, 9900X=4200, 9700X=3900
      if (/9950X|9985HX/i.test(name)) baseMhz.value = 4300;
      else if (/9900X/i.test(name)) baseMhz.value = 4200;
      else if (/9700X/i.test(name)) baseMhz.value = 3900;
      else if (/79[05]0/i.test(name)) baseMhz.value = 3800; // Zen4/4c
      else if (/Ryzen\s*\d+\s*series/i.test(name) || /Ryzen \d+ \d+H[XS]/i.test(name)) baseMhz.value = 3400;
      else if (/Intel.*[iI][579]/i.test(name)) baseMhz.value = 3000;
    }),
    refreshScheme(),
    readPowerParams(),
    schemeExists(PW.YEMAN), // 只读：野蛮系统电源是否存在（缺失则点“野蛮系统电源”自动恢复，绝不删除）
    readPhysicalCores(), // 只读：物理核心总数（去 SMT；检测失败→0，滑块禁用）
    readSmt(), // 只读：超线程/SMT 实时检测（native GLPI，免提权、毫秒级）
    detectCoreArchitecture(), // 只读：仅用于判断是否显示大小核心调度
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
  coreArchitecture.value = architectureRes.status === 'fulfilled' ? architectureRes.value : null;
  if (isHeterogeneousCpu.value) {
    const [coreAcRes, coreDcRes] = await Promise.allSettled([
      readCoreMode(true), // 只读：AC 大小核心调度
      readCoreMode(false), // 只读：DC 大小核心调度
    ]);
    if (coreAcRes.status === 'fulfilled' && coreAcRes.value) coreModeAc.value = coreAcRes.value;
    if (coreDcRes.status === 'fulfilled' && coreDcRes.value) coreModeDc.value = coreDcRes.value;
  }
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
  refreshInProgress = false;
}

async function applyAll(): Promise<boolean> {
  // 非野蛮方案时禁用所有写入操作（只有重制电源可用）
  if (!isYemanScheme.value) return false;
  // 检测失败时不写回任何值（防止默认值覆盖真实设置）——程序不做自主操作
  if (!paramsOk.value) return false;
  errMsg.value = '';
  try {
    await applyManualPowerParams({
      acFreq: acFreq.value,
      dcFreq: dcFreq.value,
      acTurbo: acTurbo.value,
      dcTurbo: dcTurbo.value,
      acAggr: acAggr.value,
      dcAggr: dcAggr.value,
    });
    return true;
  } catch (e) {
    errMsg.value = 'CPU 参数下发失败：' + (e as Error).message;
    return false;
  }
}

function currentProfileValues() {
  return {
    acFreq: acFreq.value,
    dcFreq: dcFreq.value,
    acTurbo: acTurbo.value,
    dcTurbo: dcTurbo.value,
    acAggr: acAggr.value,
    dcAggr: dcAggr.value,
  };
}

async function persistCurrentProfile(): Promise<void> {
  const config = cpuProfiles.value ?? await loadCpuProfiles();
  config.active = activeCpuProfile.value;
  config.profiles[activeCpuProfile.value] = currentProfileValues();
  await saveCpuProfiles(config);
  cpuProfiles.value = config;
}

function loadProfileToEditor(id: CpuProfileId): void {
  const profile = cpuProfiles.value?.profiles[id];
  if (!profile) return;
  acFreq.value = profile.acFreq;
  dcFreq.value = profile.dcFreq;
  acTurbo.value = profile.acTurbo;
  dcTurbo.value = profile.dcTurbo;
  acAggr.value = profile.acAggr;
  dcAggr.value = profile.dcAggr;
}

async function applyEditorAndSave(side?: 'ac' | 'dc'): Promise<void> {
  if (busy.value) return;
  busy.value = true;
  try {
    const applied = await applyAll();
    if (!applied) return;
    await persistCurrentProfile();
    await activateCpuSettings();
  } catch (e) {
    errMsg.value = 'CPU 挡位保存失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function onCpuProfile(id: CpuProfileId): Promise<void> {
  if (!isYemanScheme.value || !paramsOk.value || busy.value || cpuControlOwnedByFloat.value) return;
  const previous = activeCpuProfile.value;
  const config = cpuProfiles.value ?? await loadCpuProfiles();
  busy.value = true;
  activeCpuProfile.value = id;
  loadProfileToEditor(id);
  try {
    const applied = await applyAll();
    if (!applied) throw new Error('CPU 挡位下发失败');
    config.active = id;
    await saveCpuProfiles(config);
    cpuProfiles.value = config;
    await activateCpuSettings();
    resetInfo.value = `当前挡位：${getCpuProfileMeta(id).label}`;
  } catch (e) {
    activeCpuProfile.value = previous;
    loadProfileToEditor(previous);
    errMsg.value = 'CPU 挡位切换失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

function onFreqCommit(side: 'ac' | 'dc') {
  void applyEditorAndSave(side);
}
function onAcTurbo(v: boolean) {
  acTurbo.value = v;
  if (acFreq.value > acMax.value) acFreq.value = acMax.value;
  void applyEditorAndSave('ac');
}
function onDcTurbo(v: boolean) {
  dcTurbo.value = v;
  if (dcFreq.value > dcMax.value) dcFreq.value = dcMax.value;
  void applyEditorAndSave('dc');
}
function onAggrCommit(side: 'ac' | 'dc') {
  void applyEditorAndSave(side);
}
// 拖动滑块后延迟"激活方案"：写值只改注册表，必须重新激活方案( powercfg /setactive <YEMAN> )
// 操作系统才会真正套用——这正是 Windows 命令切换电源起效的关键，且不回读 UI，零闪烁。
// 若 2 秒内再次拖动，定时器重置，等最后一次拖动结束后 2 秒再激活——避免频繁拖动反复激活卡死。
async function activateCpuSettings(): Promise<void> {
  // Manual CPU writes must be followed by a real policy reload. Without this
  // Windows can keep using the previous active-policy cache.
  await setActiveScheme(PW.YEMAN);
}
/*
function scheduleActivate() {
  if (activateTimer !== null) window.clearTimeout(activateTimer);
  activateTimer = window.setTimeout(() => {
    activateTimer = null;
    // 重新激活野蛮方案，使刚写入的所有电源参数生效（AC/DC 全覆盖）
    setActiveScheme(PW.YEMAN).catch(() => {});
  }, 2000);
}
*/

async function onScheme(key: SchemeKey) {
  if (key === 'other' as SchemeKey) return; // 不允许切换到 'other'
  errMsg.value = '';
  busy.value = true;
  try {
    schemeKey.value = key;
    await switchScheme(key);
    await refresh();
    if (globalRefreshKey) globalRefreshKey.value++;
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
    await activateCpuSettings();
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
    await activateCpuSettings();
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
    .then(() => activateCpuSettings())
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

const resetInfo = ref('');
async function resetActiveCpuProfile() {
  if (!isYemanScheme.value || busy.value) return;
  const meta = getCpuProfileMeta(activeCpuProfile.value);
  errMsg.value = '';
  resetInfo.value = '';
  busy.value = true;
  try {
    await rebuildYemanScheme('yeman');
    await runResetProfile(meta.path);
    const resetValues = await readPowerParams();
    if (!resetValues) throw new Error('复位完成，但无法读取复位后的 CPU 参数');
    await applyThrottleLink(resetValues);

    const config = cpuProfiles.value ?? await loadCpuProfiles();
    config.active = activeCpuProfile.value;
    config.profiles[activeCpuProfile.value] = {
      acFreq: resetValues.acFreq,
      dcFreq: resetValues.dcFreq,
      acTurbo: resetValues.acTurbo,
      dcTurbo: resetValues.dcTurbo,
      acAggr: resetValues.acAggr,
      dcAggr: resetValues.dcAggr,
    };
    await saveCpuProfiles(config);
    cpuProfiles.value = config;
    loadProfileToEditor(activeCpuProfile.value);

    await activateCpuSettings();
    resetInfo.value = `已重置挡位：${meta.label}`;
  } catch (e) {
    errMsg.value = '重置挡位失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// 异步加载数据（nextTick 确保先渲染默认值，避免切页卡顿）
// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => refresh());
}

// ── 帧数目标浮动TDP+CPU优化：只读状态（控件已迁移至「快速切换 TDP」页）──
// 浮动接管期间，主频/积极性由浮动动态写入，本页滑块禁用避免用户误操作被覆盖。
const floatInfo = ref<FloatInfo>(getFloatInfo());
const floatOn = computed(() => floatInfo.value.enabled);
const cpuControlOwnedByFloat = computed(() => floatOn.value && floatInfo.value.profile !== 'none');
const offFloatUpdate = onFloatUpdate((info) => {
  const wasOn = floatInfo.value.enabled;
  floatInfo.value = info;
  // 浮动接管/释放瞬间：电源方案必须与实际同步。
  // 接管时自动浮动只对 YEMAN(野蛮系统电源) 写参数，且激活有 1.5s 延迟——
  // 这里补一次即时激活 + 刷新显示，保证第二页 UI 与 Windows 实际方案一致；
  // 释放时也刷新一次，让 UI 回显真实方案。
  if (info.enabled !== wasOn) {
    if (info.enabled) {
      void refreshScheme();
    } else {
      void refreshScheme();
    }
  }
});
onUnmounted(offFloatUpdate);

// 活动电源方案变化由 native Windows 广播驱动；没有广播不做自愈检查。
onActivated(onCpuViewActivated);

onMounted(() =>
  nextTick(async () => {
    let profilesCreated = false;
    try {
      const profilesFileExists = await cpuProfilesFileExists();
      const profiles = await loadCpuProfiles();
      cpuProfiles.value = profiles;
      activeCpuProfile.value = profiles.active;
      profilesCreated = !profilesFileExists;
    } catch (e) {
      errMsg.value = 'CPU 挡位配置读取失败：' + (e as Error).message;
    }
    await refresh();
    if (cpuProfiles.value) {
      if (profilesCreated && paramsOk.value) {
        cpuProfiles.value.profiles[activeCpuProfile.value] = currentProfileValues();
        await saveCpuProfiles(cpuProfiles.value).catch((e) => {
          errMsg.value = 'CPU 当前参数迁移到挡位失败：' + (e as Error).message;
        });
      } else if (!getFloatInfo().enabled) {
        loadProfileToEditor(activeCpuProfile.value);
      }
    }
  })
);
</script>

<template>
  <div class="page">
    <div v-if="!isYemanScheme" class="warn-bar">
      <template v-if="isRogScheme">识别到「奥创」电源方案——当前电源方案不是「野蛮系统电源」，请切换为「野蛮系统电源」</template>
      <template v-else>当前电源方案：{{ schemeDisplayName }}，请切换为「野蛮系统电源」<br />「野蛮系统电源」不会修改原版系统电源调度机制。</template>
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

    <section class="card profile-card" :class="{ disabled: !isYemanScheme }">
      <div class="profile-head">
        <div>
          <div class="profile-title">CPU挡位选择和编辑</div>
          <div class="profile-current">当前挡位：<strong>{{ activeCpuProfileLabel }}</strong></div>
        </div>
        <button
          type="button"
          class="profile-reset-button"
          :disabled="!isYemanScheme || busy || cpuControlOwnedByFloat"
          @click="resetActiveCpuProfile"
        >重置挡位</button>
      </div>
      <div class="profile-buttons">
        <button
          v-for="item in CPU_PROFILE_META"
          :key="item.id"
          type="button"
          class="profile-button"
          :class="{ active: activeCpuProfile === item.id }"
          :disabled="!isYemanScheme || !paramsOk || busy || cpuControlOwnedByFloat"
          @click="onCpuProfile(item.id)"
        >{{ item.label }}</button>
      </div>
      <div class="profile-hint">选择挡位后，下方 AC / DC 参数即为该挡位的独立配置；修改后自动保存。</div>
    </section>

    <section class="card" :class="{ disabled: !isYemanScheme }">
      <div class="freq-head">
        <span class="freq-label"><InlineIcon name="plug" /> AC CPU最大主频</span>
        <div class="freq-right">
          <Toggle v-model="acTurbo" :label="'睿频'" :description="acTurbo ? '已开启' : '已关闭'" color="ac" :disabled="!isYemanScheme || !paramsOk" @update:model-value="onAcTurbo" compact />
          <span class="freq-val">{{ freqText(acFreq) }}</span>
        </div>
      </div>
      <Slider v-model="acFreq" :min="0" :max="acMax" :step="100" :label="''" :unit="'MHz'" color="ac" :disabled="!isYemanScheme || !paramsOk || cpuControlOwnedByFloat" @commit="onFreqCommit('ac')" />
      <Slider v-model="acAggr" :min="0" :max="100" :step="1" :label="'CPU主频调度积极性 (越大CPU主频越高)'" icon="plug" color="ac" :disabled="!isYemanScheme || !paramsOk || cpuControlOwnedByFloat" @commit="onAggrCommit('ac')" />
      <div class="profile-editing">正在编辑：{{ activeCpuProfileLabel }}挡位 · AC</div>
    </section>

    <section class="card" :class="{ disabled: !isYemanScheme }">
      <div class="freq-head">
        <span class="freq-label"><InlineIcon name="battery" /> DC CPU最大主频</span>
        <div class="freq-right">
          <Toggle v-model="dcTurbo" :label="'睿频'" :description="dcTurbo ? '已开启' : '已关闭'" color="dc" :disabled="!isYemanScheme || !paramsOk" @update:model-value="onDcTurbo" compact />
          <span class="freq-val">{{ freqText(dcFreq) }}</span>
        </div>
      </div>
      <Slider v-model="dcFreq" :min="0" :max="dcMax" :step="100" :label="''" :unit="'MHz'" color="dc" :disabled="!isYemanScheme || !paramsOk || cpuControlOwnedByFloat" @commit="onFreqCommit('dc')" />
      <Slider v-model="dcAggr" :min="0" :max="100" :step="1" :label="'CPU主频调度积极性 (越大CPU主频越高)'" icon="battery" color="dc" :disabled="!isYemanScheme || !paramsOk || cpuControlOwnedByFloat" @commit="onAggrCommit('dc')" />
      <div class="profile-editing">正在编辑：{{ activeCpuProfileLabel }}挡位 · DC</div>
    </section>

    <section v-if="isHeterogeneousCpu" class="card core-mode-card" :class="{ disabled: !isYemanScheme }">
      <div class="core-mode-title"><InlineIcon name="wrench" /> 大小核心调度</div>
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
      <div class="profile-hint">大小核心调度独立生效，不参与 CPU 挡位记忆和复位。</div>
    </section>

    <section class="card core-sched-card" :class="{ disabled: !isYemanScheme }">
      <div class="core-head">
        <span class="core-title"><InlineIcon name="wrench" /> 全局核心调度</span>
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
.profile-card,
.core-mode-card {
  margin-top: 0;
}
.profile-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 10px;
}
.profile-title,
.core-mode-title {
  font-size: 12px;
  font-weight: 700;
  color: var(--text);
}
.profile-current {
  margin-top: 4px;
  color: #8a97a8;
  font-size: 10px;
}
.profile-current strong {
  color: var(--accent);
  font-size: 11px;
}
.profile-buttons {
  display: flex;
  gap: 6px;
  width: 100%;
}
.profile-button,
.profile-reset-button {
  min-width: 0;
  border: 0;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text);
  padding: var(--btn-py) 8px;
  min-height: var(--btn-min-h);
  font: inherit;
  font-size: var(--btn-font-size);
  font-weight: 600;
  white-space: nowrap;
  cursor: pointer;
  transition: background 0.12s, color 0.12s, transform 0.1s;
}
.profile-button {
  flex: 1 1 0;
}
.profile-reset-button {
  flex: 0 0 auto;
  color: var(--accent);
  background: color-mix(in srgb, var(--accent) 12%, transparent);
}
.profile-button.active {
  background: var(--accent);
  color: #06121d;
}
.profile-button:hover:not(:disabled),
.profile-reset-button:hover:not(:disabled) {
  background: #263447;
}
.profile-button.active:hover:not(:disabled) {
  background: var(--accent);
}
.profile-button:active:not(:disabled),
.profile-reset-button:active:not(:disabled) {
  transform: scale(0.97);
}
.profile-button:focus-visible,
.profile-reset-button:focus-visible {
  outline: none;
  box-shadow: var(--focus-ring);
}
.profile-button:disabled,
.profile-reset-button:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.profile-hint,
.profile-editing {
  color: #8a97a8;
  font-size: 10px;
  line-height: 1.4;
}
.profile-hint {
  margin-top: 8px;
}
.profile-editing {
  margin-top: 5px;
  text-align: right;
}
.core-mode-card .core-mode-block {
  margin-top: 10px;
}
.reset-card {
  margin-top: 0;
}
.reset-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 8px;
}
.reset-hint {
  color: #8a97a8;
  font-size: 10px;
  white-space: nowrap;
}
.reset-buttons {
  display: flex;
  gap: 6px;
  width: 100%;
}
.reset-button {
  flex: 1 1 0;
  min-width: 0;
  border: 0;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text);
  padding: var(--btn-py) 4px;
  min-height: var(--btn-min-h);
  font: inherit;
  font-size: var(--btn-font-size);
  font-weight: 600;
  white-space: nowrap;
  cursor: pointer;
  transition: background 0.12s, color 0.12s, transform 0.1s;
}
.reset-button:hover:not(:disabled) {
  background: #263447;
}
.reset-button:active:not(:disabled) {
  transform: scale(0.97);
}
.reset-button:focus-visible {
  outline: none;
  box-shadow: var(--focus-ring);
}
.reset-button:disabled {
  opacity: 0.45;
  cursor: not-allowed;
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
  padding: var(--btn-py) var(--btn-px);
  min-height: var(--btn-min-h);
  font-weight: 700;
  font-size: var(--btn-font-size);
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
  max-width: 440px;
  background: #121a26;
  border: 1px solid #2a3342;
  border-radius: 12px;
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.5);
  padding: 24px;
}
.smt-modal-title {
  font-size: 18px;
  font-weight: 700;
  color: var(--text);
  margin-bottom: 14px;
}
.smt-modal-msg {
  font-size: 16px;
  color: var(--text);
  line-height: 1.5;
}
.smt-modal-sub {
  font-size: 14px;
  color: #8a97a8;
  margin-top: 8px;
  line-height: 1.55;
}
.smt-modal-actions {
  display: flex;
  gap: 12px;
  margin-top: 22px;
}
.smt-btn {
  flex: 1;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: 11px;
  min-height: 44px;
  font-size: 14px;
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

/* 帧数目标浮动TDP+CPU优化 */
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

</style>
