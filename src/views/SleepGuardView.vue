<script setup lang="ts">
import { ref, reactive, computed, inject, watch, type Ref } from 'vue';
import { fs, shell } from '@/bridge/api';
import Toggle from '@/components/Toggle.vue';
import Slider from '@/components/Slider.vue';
import SegButton from '@/components/SegButton.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import {
  sleepGuardGet,
  sleepGuardSet,
  sleepGuardSetConfig,
  getPowerBtnIdx,
  setPowerBtnIdx,
  reactivateCurrentScheme,
  isHibernateOff,
  setHibernate,
  readHibernateSize,
  setHibernateSize,
  readTotalMemoryGB,
  type SleepGuardStatus,
  type PowerBtnIdx,
} from '@/bridge/yeman';

const cfg = reactive<SleepGuardStatus>({
  enabled: false,
  mode: 'off',
  suspended: 0,
  pauseResume: true,
  killListEnabled: false,
  resleepEnabled: false,
  overheatSleepEnabled: false,
  overheatTempC: 95,
});
const SLEEP_KILL_LIST = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\睡眠击杀名单.txt';

// 电源按钮：0=S3睡眠, 1=S4休眠, 2=不操作
const acBtnIdx = ref<PowerBtnIdx>(2);
const dcBtnIdx = ref<PowerBtnIdx>(2);

// 系统休眠
const hibernateOn = ref<boolean | null>(null); // null=未检测（不强制设默认值）
const hibLoading = ref(true); // 仅加载中显示"检测中"，加载完必为真实布尔
const hibSize = ref(50);
const memGB = ref<number | null>(null); // 物理内存总量（GB），用于预估休眠文件大小
const confirmOff = ref(false); // 关闭休眠二次确认（避免整行误触关闭休眠这种销毁性操作）

const busy = ref(false);
const msg = ref('');

async function refresh() {
  try {
    const s = await sleepGuardGet();
    cfg.enabled = s.enabled;
    cfg.mode = s.mode;
    cfg.suspended = s.suspended;
    cfg.pauseResume = s.pauseResume;
    cfg.killListEnabled = !!s.killListEnabled;
    cfg.resleepEnabled = !!s.resleepEnabled;
    cfg.overheatSleepEnabled = !!s.overheatSleepEnabled;
    cfg.overheatTempC = Math.max(85, Math.min(100, Number(s.overheatTempC) || 95));
  } catch {
    /* 检测不到保持现状，绝不假定关闭 */
  }

  // 并行加载电源按钮、系统休眠状态
  const [acBtnRes, dcBtnRes, hibRes, hibSizeRes, memRes] = await Promise.allSettled([
    getPowerBtnIdx(true),
    getPowerBtnIdx(false),
    isHibernateOff().then((off) => (off === null ? null : !off)), // true=开启；null=未知
    readHibernateSize(),
    readTotalMemoryGB(),
  ]);
  if (acBtnRes.status === 'fulfilled') acBtnIdx.value = acBtnRes.value;
  if (dcBtnRes.status === 'fulfilled') dcBtnIdx.value = dcBtnRes.value;
  if (hibRes.status === 'fulfilled') hibernateOn.value = hibRes.value;
  if (hibSizeRes.status === 'fulfilled') hibSize.value = hibSizeRes.value;
  if (memRes.status === 'fulfilled') memGB.value = memRes.value;
  hibLoading.value = false;
}

// 睡眠时暂停游戏（与唤醒自动恢复绑定）。
async function onPauseResume(v: boolean) {
  cfg.pauseResume = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({ mode: 'custom', pauseResume: v, killListEnabled: cfg.killListEnabled, resleepEnabled: cfg.resleepEnabled, overheatSleepEnabled: cfg.overheatSleepEnabled, overheatTempC: cfg.overheatTempC });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onKillList(v: boolean) {
  cfg.killListEnabled = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({ mode: 'custom', pauseResume: cfg.pauseResume, killListEnabled: v, resleepEnabled: cfg.resleepEnabled, overheatSleepEnabled: cfg.overheatSleepEnabled, overheatTempC: cfg.overheatTempC });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onResleep(v: boolean) {
  cfg.resleepEnabled = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({
      mode: 'custom',
      pauseResume: cfg.pauseResume,
      killListEnabled: cfg.killListEnabled,
      resleepEnabled: v,
      overheatSleepEnabled: cfg.overheatSleepEnabled,
      overheatTempC: cfg.overheatTempC,
    });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onOverheatSleep(v: boolean) {
  cfg.overheatSleepEnabled = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({
      mode: 'custom',
      pauseResume: cfg.pauseResume,
      killListEnabled: cfg.killListEnabled,
      resleepEnabled: cfg.resleepEnabled,
      overheatSleepEnabled: v,
      overheatTempC: cfg.overheatTempC,
    });
    await syncGuardEnabled();
  } catch {
    msg.value = '璁剧疆淇濆瓨澶辫触';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onOverheatTemp(v: number) {
  cfg.overheatTempC = Math.max(85, Math.min(100, Math.round(v)));
  try {
    await sleepGuardSetConfig({ overheatTempC: cfg.overheatTempC });
  } catch {
    msg.value = '璁剧疆淇濆瓨澶辫触';
    await refresh();
  }
}

async function openKillList() {
  try {
    if (!(await fs.exists(SLEEP_KILL_LIST))) {
      await fs.writeTextFile(SLEEP_KILL_LIST, '# 每行一个 exe 名称，可带或不带 .exe\n# 例如：SomeTool.exe\n');
    }
    await shell.open(SLEEP_KILL_LIST);
  } catch {
    msg.value = '无法打开睡眠击杀名单';
  }
}

// 总闸（Enable.txt）：任一子功能生效即开启，全部关闭即关闭
async function syncGuardEnabled() {
  const active = cfg.pauseResume || cfg.killListEnabled || cfg.resleepEnabled || cfg.overheatSleepEnabled;
  if (active === cfg.enabled) return;
  const previous = cfg.enabled;
  try {
    await sleepGuardSet(active);
    cfg.enabled = active;
  } catch {
    cfg.enabled = previous;
    msg.value = '守护总闸设置失败';
    throw new Error('守护总闸设置失败');
  }
}

// ── 电源按钮按下操作 ──
async function onPwrBtn(ac: boolean, idx: PowerBtnIdx) {
  msg.value = '';
  busy.value = true;
  try {
    await setPowerBtnIdx(ac, idx);
    if (ac) acBtnIdx.value = idx; else dcBtnIdx.value = idx;
    // 联动：电源按钮选「S4 休眠到硬盘」时，若系统休眠当前为关闭，则自动开启休眠——否则 S4 不会生效。
    // 仅"开启"，绝不在选择其它按钮时关闭休眠；检测未知时不乱写。
    if (idx === 1) {
      if (hibernateOn.value === false) {
        await setHibernate(true);
        hibernateOn.value = true;
        await setHibernateSize(hibSize.value);
      } else if (hibernateOn.value === null) {
        msg.value = '休眠状态未知（检测失败），本次未自动开启休眠。请点击「刷新」重新检测后重试。';
      }
    }
    schedulePwrActivate();
  } catch (e) {
    msg.value = '电源按钮设置失败：' + (e as Error).message + '（需要管理员权限修改电源方案）';
  } finally {
    busy.value = false;
  }
}

let pwrActivateTimer: number | null = null;
function schedulePwrActivate() {
  if (pwrActivateTimer !== null) window.clearTimeout(pwrActivateTimer);
  pwrActivateTimer = window.setTimeout(() => {
    pwrActivateTimer = null;
    reactivateCurrentScheme().catch(() => {});
  }, 2000);
}

// ── 系统休眠 ──
async function onHibernate(v: boolean) {
  if (hibernateOn.value === null) {
    msg.value = '休眠状态未知，无法切换。请点击「刷新」重新检测后重试。';
    return;
  }
  if (v) {
    msg.value = '';
    busy.value = true;
    try {
      await setHibernate(true);
      hibernateOn.value = true;
      await setHibernateSize(hibSize.value);
    } catch (e) {
      hibernateOn.value = false;
      msg.value = '休眠开启失败：' + (e as Error).message + '（可能需要管理员权限）';
    } finally {
      busy.value = false;
    }
    return;
  }
  confirmOff.value = true;
}
async function confirmHibernateOff() {
  msg.value = '';
  busy.value = true;
  try {
    await setHibernate(false);
    hibernateOn.value = false;
  } catch (e) {
    hibernateOn.value = true;
    msg.value = '休眠关闭失败：' + (e as Error).message + '（可能需要管理员权限）';
  } finally {
    confirmOff.value = false;
    busy.value = false;
  }
}
function cancelHibernateOff() {
  confirmOff.value = false;
}
async function onHibSize(v: number) {
  msg.value = '';
  try {
    await setHibernateSize(v);
  } catch (e) {
    msg.value = '休眠大小设置失败：' + (e as Error).message;
  }
}

const hibDesc = computed(() => {
  if (hibLoading.value) return '检测中…';
  if (hibernateOn.value === null) return '检测失败（未知）';
  if (memGB.value !== null) {
    const est = (memGB.value * hibSize.value) / 100;
    return `休眠文件大小预估为 ${est.toFixed(1)} G`;
  }
  return hibernateOn.value ? '休眠已开启' : '休眠已关闭';
});

const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => refresh());
}
refresh();
</script>

<template>
  <div class="sleep-guard">
    <!-- 顶部：电源按钮与系统休眠 合并为一个气泡 -->
    <div class="card">
      <h3 class="card-title"><InlineIcon name="plug" /> 电源按钮与系统休眠</h3>
      <div class="pwr-row">
        <span class="pwr-label"><InlineIcon name="bolt" /> AC 插电</span>
        <SegButton
          :model-value="acBtnIdx"
          :options="[
            { value: 0, label: 'S3 睡眠' },
            { value: 1, label: 'S4 休眠' },
            { value: 2, label: '不操作' },
            { value: 3, label: '关闭显示器' },
          ]"
          color="ac"
          @update:model-value="(v: number) => onPwrBtn(true, v as PowerBtnIdx)"
        />
      </div>
      <div class="pwr-row">
        <span class="pwr-label"><InlineIcon name="battery" /> DC 离电</span>
        <SegButton
          :model-value="dcBtnIdx"
          :options="[
            { value: 0, label: 'S3 睡眠' },
            { value: 1, label: 'S4 休眠' },
            { value: 2, label: '不操作' },
            { value: 3, label: '关闭显示器' },
          ]"
          color="dc"
          @update:model-value="(v: number) => onPwrBtn(false, v as PowerBtnIdx)"
        />
      </div>
      <p v-if="acBtnIdx === 0 || dcBtnIdx === 0" class="pwr-warn">S3 状态插入 USB4 供电可能导致唤醒！</p>
      <Toggle
        :model-value="hibernateOn"
        label="系统休眠开关"
        :description="hibDesc"
        color="accent"
        :disabled="busy || hibLoading || hibernateOn === null"
        @update:model-value="onHibernate"
      />
      <div v-if="confirmOff" class="confirm-bar">
        <span class="confirm-text">确认关闭系统休眠？将删除 hiberfil.sys 休眠文件（S4 电源按钮会失效）。</span>
        <div class="confirm-actions">
          <button class="mini-btn danger" @click="confirmHibernateOff">确认关闭</button>
          <button class="mini-btn" @click="cancelHibernateOff">取消</button>
        </div>
      </div>
      <Slider
        v-model="hibSize"
        :min="30"
        :max="100"
        :step="5"
        label="休眠文件大小百分比"
        unit="%"
        color="accent"
        :disabled="!hibernateOn"
        @commit="onHibSize"
      />
    </div>

    <!-- 睡眠操作（总开关已移除，子项直接驱动 guard，始终显示） -->
    <div class="card">
      <h3 class="card-title"><InlineIcon name="sleep" /> 睡眠操作</h3>
      <Toggle
        :model-value="cfg.pauseResume"
        label="睡眠时暂停游戏"
        description="入睡冻结当前最大内存进程，唤醒自动恢复"
        :disabled="busy"
        @update:model-value="onPauseResume"
      />
      <Toggle
        :model-value="cfg.resleepEnabled"
        label="入睡失败重睡"
        description="触发睡眠后 30 秒内，连续 10 秒无手柄/键盘输入时重新睡眠；重睡后 5 分钟内不重复触发"
        :disabled="busy"
        @update:model-value="onResleep"
      />
      <Toggle
        :model-value="cfg.overheatSleepEnabled"
        label="过热自动睡眠"
        description="电池设备温度超过阈值后，10秒无手柄、键盘、鼠标输入自动睡眠"
        :disabled="busy"
        @update:model-value="onOverheatSleep"
      />
      <Slider
        v-model="cfg.overheatTempC"
        :min="85"
        :max="100"
        :step="1"
        label="过热温度"
        unit="°C"
        color="accent"
        :disabled="busy || !cfg.overheatSleepEnabled"
        @commit="onOverheatTemp"
      />
      <div class="kill-list-row">
        <div class="kill-list-text">
          <div class="kill-list-label">睡眠时清除指定程序</div>
          <div class="kill-list-hint">入睡前结束名单中的进程防止影响睡眠</div>
        </div>
        <div class="kill-list-actions">
          <button class="kill-list-edit" :disabled="busy" @click="openKillList">编辑名单</button>
          <Toggle
            :model-value="cfg.killListEnabled"
            :disabled="busy"
            @update:model-value="onKillList"
          />
        </div>
      </div>
    </div>

    <div v-if="msg" class="msg" :class="{ 'msg-ok': /已启用|已关闭|已恢复|已暂停/.test(msg) }">
      {{ msg }}
    </div>
  </div>
</template>

<style scoped>
.sleep-guard { padding: 2px; }
.muted { color: var(--text-dim); }

.card {
  background: color-mix(in srgb, var(--bg-panel) 72%, transparent);
  border-radius: var(--radius);
  padding: 12px 14px;
  margin-bottom: 10px;
}
.card-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
  margin: 0 0 10px;
  display: flex;
  align-items: center;
  gap: 6px;
}

.kill-list-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 8px 0; }
.kill-list-text { min-width: 0; flex: 1; text-align: left; }
.kill-list-label { font-size: 13px; }
.kill-list-hint { font-size: 11px; color: var(--text-dim); line-height: 1.3; }
.kill-list-actions { display: flex; align-items: center; gap: 8px; flex: 0 0 auto; }
.kill-list-edit {
  min-height: var(--btn-min-h);
  padding: var(--btn-py) var(--btn-px);
  border: 1px solid #2a3342;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text);
  font-size: var(--btn-font-size);
  cursor: pointer;
}
.kill-list-edit:disabled { opacity: 0.4; cursor: not-allowed; }
.kill-list-edit:focus-visible { box-shadow: var(--focus-ring); }

.msg {
  font-size: 12px;
  color: var(--danger);
  margin-top: 4px;
  padding: 8px 10px;
  border-radius: var(--radius-ctrl);
  background: rgba(255, 90, 90, 0.08);
  border: 1px solid rgba(255, 90, 90, 0.25);
}
.msg-ok { color: var(--ok); background: rgba(80, 200, 120, 0.08); border-color: rgba(80, 200, 120, 0.25); }

/* 电源按钮 / 系统休眠 */
.pwr-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 8px;
}
.pwr-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
}
.pwr-warn {
  margin-top: 4px;
  font-size: 11px;
  color: var(--accent-2, #f5b93d);
  line-height: 1.3;
}
.confirm-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  margin-bottom: 8px;
  font-size: 11px;
  line-height: 1.4;
}
.confirm-text {
  color: #ff9ea1;
  display: block;
  margin-bottom: 8px;
}
.confirm-actions {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
}
.mini-btn {
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: 6px;
  padding: 5px 8px;
  font-size: 11px;
  cursor: pointer;
}
.mini-btn.danger {
  background: rgba(229, 72, 77, 0.85);
  color: #fff;
  border-color: rgba(229, 72, 77, 0.85);
}
</style>
