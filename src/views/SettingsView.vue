<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, computed } from 'vue';
import Toggle from '@/components/Toggle.vue';
import GamepadVisualizer from '@/components/GamepadVisualizer.vue';
import { summonGet, summonSet, toggleTask, taskExists, autocloseGet, autocloseSet, updateAccelGet, updateAccelToggle, type GamepadSettings, type AutoCloseConfig, type UpdateAccelState, checkUpdate, downloadUpdate, installUpdate, compareVersions, UPDATE_MANIFEST_URL, updatePackageUrl } from '@/bridge/yeman';
import { shell } from '@/bridge/api';
import { APP_VERSION } from '@/version';

const BOOT_TASK = '野蛮控制中心-开机启动';

const bootMinOn = ref(false);
const busy = ref(false);
const gp = ref<GamepadSettings>({
  enabled: true,
  bDoubleMinimize: true,
  tdpShortcut: false,
  fpsShortcut: false,
  killGame: false,
  openKeyboard: false,
});
const errMsg = ref('');

// ── 掌机前端自动关闭：总开关 + 可编辑进程名列表 ──
const autoClose = ref<AutoCloseConfig>({ enabled: false, procs: [] });
const acBusy = ref(false);

// ── 更新加速器：手动按钮 + 文件/运行状态 ──
const updateAccel = ref<UpdateAccelState>({
  exists: false,
  running: false,
});
const uaBusy = ref(false);
const uaStatusText = computed(() => {
  if (uaBusy.value) return '处理中…';
  if (!updateAccel.value.exists) return '文件未找到';
  if (updateAccel.value.running) return '运行中';
  return '已关闭';
});
const uaTagClass = computed(() => {
  if (uaBusy.value) return '';
  if (updateAccel.value.running) return 'ok';
  if (!updateAccel.value.exists) return 'err';
  return '';
});

// 版本号：构建期由 version.json 注入（scripts/write-version.mjs → src/version.ts）
const appVersion = APP_VERSION;

// ── 自动更新（检查 / 下载 / 安装）──
type UpdateState = 'idle' | 'checking' | 'has' | 'latest' | 'downloading' | 'error';
const updateState = ref<UpdateState>('idle');
const updateInfo = ref<{ version: string; notes?: string } | null>(null);
const updateErr = ref('');
async function onCheckUpdate() {
  updateErr.value = '';
  updateState.value = 'checking';
  try {
    const info = await checkUpdate(UPDATE_MANIFEST_URL);
    if (compareVersions(info.version, appVersion) > 0) {
      updateInfo.value = { version: info.version, notes: info.notes };
      updateState.value = 'has';
    } else {
      updateState.value = 'latest';
    }
  } catch (e) {
    updateErr.value = '检查失败：' + (e as Error).message;
    updateState.value = 'error';
  }
}
async function onUpdate() {
  if (!updateInfo.value) return;
  updateErr.value = '';
  updateState.value = 'downloading';
  try {
    await downloadUpdate(updatePackageUrl(updateInfo.value.version));
    // 下载完成后请求 native 安装（会解压、合并覆盖、重启，本进程随后退出）
    await installUpdate();
  } catch (e) {
    updateErr.value = '更新失败：' + (e as Error).message;
    updateState.value = 'error';
  }
}

// 快捷键预览：鼠标悬浮 或 真实手柄按键按下 → 对应手柄图标蓝色闪烁 + 提示
type GpKey = keyof GamepadSettings;
const shortcuts: { key: GpKey; label: string; hint: string; buttons: number[] }[] = [
  { key: 'enabled', label: '呼出软件', hint: 'LB + RB 长按 2 秒 → 呼出窗口', buttons: [4, 5] },
  { key: 'bDoubleMinimize', label: '关闭软件', hint: '双击 B → 最小化到托盘', buttons: [1] },
  { key: 'tdpShortcut', label: '调节TDP', hint: '按住 Start + 方向键 上/下 → TDP ±1W', buttons: [9, 12, 13] },
  { key: 'fpsShortcut', label: '调节锁帧', hint: '按住 Start + 方向键 左/右 → 锁帧 ±5', buttons: [9, 14, 15] },
  { key: 'killGame', label: '关闭游戏', hint: '按住 选择(≡) + B 长按 0.5 秒 → 关闭游戏', buttons: [8, 1] },
  { key: 'openKeyboard', label: '打开键盘', hint: '按住 选择(≡) + X 长按 0.5 秒 → 打开键盘', buttons: [8, 2] },
];
// 快捷键预览：鼠标悬浮 或 手柄焦点选中（移动选择）→ 对应手柄图标蓝色闪烁 + 提示
const hoverKey = ref<GpKey | ''>('');
const focusKey = ref<GpKey | ''>('');
const activeKey = computed<GpKey | ''>(() => hoverKey.value || focusKey.value);
const activeShortcut = computed(() => shortcuts.find((s) => s.key === activeKey.value) || null);

// 把手柄提示里的物理按键名（LB/RB/B/X/Start/选择(≡)/方向键…）单独标黄加粗放大
const KEY_TOKENS = ['选择(≡)', '方向键 上/下', '方向键 左/右', 'Start', 'LB', 'RB', 'X', 'Y', 'A', 'B'];
function segmentHint(hint: string): { text: string; key: boolean }[] {
  const segs: { text: string; key: boolean }[] = [];
  let i = 0;
  while (i < hint.length) {
    let matched = false;
    for (const t of KEY_TOKENS) {
      if (hint.startsWith(t, i)) {
        segs.push({ text: t, key: true });
        i += t.length;
        matched = true;
        break;
      }
    }
    if (!matched) {
      let j = i + 1;
      let found = -1;
      while (j < hint.length) {
        let hit = false;
        for (const t of KEY_TOKENS) {
          if (hint.startsWith(t, j)) { found = j; hit = true; break; }
        }
        if (hit) break;
        j++;
      }
      const end = found === -1 ? hint.length : found;
      segs.push({ text: hint.slice(i, end), key: false });
      i = end;
    }
  }
  return segs;
}
const hintSegments = computed(() => (activeShortcut.value ? segmentHint(activeShortcut.value.hint) : []));

async function load() {
  errMsg.value = '';
  try {
    bootMinOn.value = await taskExists(BOOT_TASK);
  } catch (e) {
    errMsg.value = '读取开机启动状态失败：' + (e as Error).message;
  }
  try {
    gp.value = await summonGet();
  } catch {
    /* 保持默认值 */
  }
  try {
    autoClose.value = await autocloseGet();
  } catch {
    /* 保持默认值 */
  }
  try {
    updateAccel.value = await updateAccelGet();
  } catch {
    /* 保持默认值 */
  }
}

async function saveAutoClose(opts: { procsOnly?: boolean } = {}) {
  acBusy.value = true;
  const snapshot = { enabled: autoClose.value.enabled, procs: [...autoClose.value.procs] };
  try {
    const next = await autocloseSet(snapshot);
    autoClose.value = next;
  } catch (e) {
    errMsg.value = '自动关闭设置失败：' + (e as Error).message;
  } finally {
    acBusy.value = false;
  }
}

async function onAutoCloseToggle(v: boolean) {
  errMsg.value = '';
  const prev = autoClose.value.enabled;
  autoClose.value.enabled = v; // 乐观更新
  try {
    const next = await autocloseSet({ enabled: v, procs: [...autoClose.value.procs] });
    autoClose.value = next;
  } catch (e) {
    autoClose.value.enabled = prev;
    errMsg.value = '自动关闭开关失败：' + (e as Error).message;
  }
}

function onAcProcAdd() {
  autoClose.value.procs.push('');
}

function onAcProcRemove(idx: number) {
  autoClose.value.procs.splice(idx, 1);
  saveAutoClose();
}

async function onAcProcInput(idx: number, val: string) {
  autoClose.value.procs[idx] = val;
  // 输入即存（失焦/回车时触发 @change），避免退出页面丢失
  await saveAutoClose();
}

async function onUpdateAccelToggle() {
  errMsg.value = '';
  uaBusy.value = true;
  try {
    const next = await updateAccelToggle();
    updateAccel.value = next;
    if (!next.ok) {
      errMsg.value = next.running
        ? '停止加速失败'
        : next.exists
          ? '启动加速失败'
          : '加速器文件未找到';
    }
  } catch (e) {
    errMsg.value = '更新加速操作失败：' + (e as Error).message;
  } finally {
    uaBusy.value = false;
  }
}

async function onBootMin(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  const prev = bootMinOn.value;
  bootMinOn.value = v; // 乐观更新
  try {
    await toggleTask(BOOT_TASK, v);
  } catch (e) {
    bootMinOn.value = prev; // 失败回滚
    errMsg.value = '设置失败：' + (e as Error).message + '（创建开机任务需管理员权限，请右键以管理员身份运行 YeManCC）';
  } finally {
    busy.value = false;
  }
}

async function onGpSetting<K extends keyof GamepadSettings>(key: K, v: GamepadSettings[K]) {
  errMsg.value = '';
  const prev = gp.value[key];
  gp.value[key] = v; // 乐观更新
  try {
    const next = await summonSet({ [key]: v });
    gp.value = next;
    // 实时同步给手柄引擎（避免重启才生效）
    window.dispatchEvent(new CustomEvent('ipc:gamepad.settings', { detail: next }));
  } catch (e) {
    gp.value[key] = prev; // 失败回滚
    errMsg.value = '设置失败：' + (e as Error).message;
  }
}

// ── 以下原属「支持」页 ──
async function openSupport() {
  try {
    await shell.open('C:\\SOFT\\YeMan\\YeMan-Support.html');
  } catch {
    /* ignore */
  }
}

async function openHome() {
  try {
    await shell.open('https://link3.cc/yeman');
  } catch {
    /* ignore */
  }
}

onMounted(() => {
  load();
});
onBeforeUnmount(() => {});
</script>

<template>
  <div class="page">
    <section class="card">
      <h3 class="card-title">⚙ 开机启动</h3>
      <Toggle
        v-model="bootMinOn"
        label="开机最小化启动"
        description="登录系统后自动以最小化方式启动控制中心"
        color="accent"
        :disabled="busy"
        @update:model-value="onBootMin"
      />
      <p v-if="errMsg" class="err-bar">{{ errMsg }}</p>
    </section>

    <section class="card">
      <h3 class="card-title">🔒 掌机前端自动关闭</h3>
      <Toggle
        v-model="autoClose.enabled"
        label="自动关闭掌机前端"
        description="开启后每 5 秒检测，发现列表中的进程即温和关闭（发关闭信号，非强杀）"
        color="accent"
        :disabled="acBusy"
        @update:model-value="onAutoCloseToggle"
      />
      <p v-if="!autoClose.enabled" class="muted body ac-collapsed">
        已关闭 —— 开启开关后展开下方进程名列表
      </p>
      <template v-else>
        <p class="muted body ac-sub">
          目标进程名（可带或不带 .exe；支持 <code>*</code> 前缀通配，如 <code>AYA*</code>）。
          默认预填 一号本/AYA/GPD，KO助手、NewKO 等请在此自行补充。
        </p>
        <div class="ac-list">
          <div v-for="(p, i) in autoClose.procs" :key="i" class="ac-row">
            <input
              class="ac-input"
              type="text"
              :placeholder="i === 0 ? 'OneXConsole' : '进程名，如 AYASpace'"
              :value="p"
              :disabled="acBusy"
              @input="onAcProcInput(i, ($event.target as HTMLInputElement).value)"
              @change="saveAutoClose()"
            />
            <button class="ac-del" :disabled="acBusy" title="移除" @click="onAcProcRemove(i)">✕</button>
          </div>
          <button class="ac-add" :disabled="acBusy" @click="onAcProcAdd">＋ 添加进程名</button>
        </div>
      </template>
    </section>

    <section class="card">
      <h3 class="card-title">⌨ 快捷键</h3>
      <div class="gp-toggles">
        <button
          v-for="s in shortcuts"
          :key="s.key"
          class="gp-toggle"
          :class="{ on: gp[s.key], sel: activeKey === s.key }"
          @mouseenter="hoverKey = s.key"
          @mouseleave="hoverKey = ''"
          @focus="focusKey = s.key"
          @blur="focusKey = ''"
          @click="onGpSetting(s.key, !gp[s.key])"
        >
          <span class="gp-toggle-label">{{ s.label }}</span>
          <span class="gp-toggle-state">{{ gp[s.key] ? '开' : '关' }}</span>
        </button>
      </div>
      <p class="gp-hint">
        <template v-if="activeShortcut">
          <b class="gp-hint-name">{{ activeShortcut.label }}</b> · <span v-for="(seg, i) in hintSegments" :key="i" :class="{ 'gp-key': seg.key }">{{ seg.text }}</span>
        </template>
        <template v-else>鼠标悬停 或 用手柄移动选择对应按钮，查看组合与操作方式</template>
      </p>
      <GamepadVisualizer :settings="gp" :highlight-buttons="activeShortcut ? activeShortcut.buttons : []" />
    </section>

    <!-- ── 以下内容原属「支持」页面，已合并至设置下方 ── -->
    <section class="card">
      <h3 class="card-title">🌐 野蛮系统支持</h3>
      <p class="muted body">此控制台处于早期测试阶段</p>
      <button class="action-btn outline" @click="openSupport">🚀 支持和软件官网 ↗</button>
      <button class="action-btn outline" @click="openHome">🏠 野蛮系统主页 ↗</button>
    </section>

    <section class="card">
      <h3 class="card-title">📦 版本和更新</h3>
      <p class="muted body">当前版本：<b class="ac-name">{{ appVersion }}</b></p>
      <div class="upd-row">
        <button class="ac-add" :disabled="updateState === 'checking' || updateState === 'downloading'" @click="onCheckUpdate">
          {{ updateState === 'checking' ? '检查中…' : '检查更新' }}
        </button>
        <span v-if="updateState === 'latest'" class="upd-tag ok">已是最新</span>
        <span v-else-if="updateState === 'error'" class="upd-tag err">{{ updateErr }}</span>
        <span v-else-if="updateState === 'checking'" class="upd-tag">连接更新服务器…</span>
        <span class="upd-divider" />
        <button class="ac-add ua-btn" :disabled="uaBusy || !updateAccel.exists" @click="onUpdateAccelToggle">
          {{ updateAccel.running ? '停止更新加速' : '启动更新加速' }}
        </button>
        <span class="upd-tag" :class="uaTagClass">{{ uaStatusText }}</span>
      </div>
      <div v-if="updateState === 'has' && updateInfo" class="upd-has">
        <p class="upd-line">发现新版本 <b class="ac-name">{{ updateInfo.version }}</b></p>
        <p v-if="updateInfo.notes" class="muted body upd-notes">{{ updateInfo.notes }}</p>
        <button class="ac-add upd-install" :disabled="updateState === 'downloading'" @click="onUpdate">
          {{ updateState === 'downloading' ? '下载并安装中…' : '下载并安装' }}
        </button>
      </div>
    </section>
  </div>
</template>

<style scoped>
.page {
  padding-bottom: 20px;
}
.body {
  font-size: 12px;
  line-height: 1.6;
  margin: 0 0 10px;
}
.err-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-top: 10px;
  line-height: 1.4;
}
.tips {
  margin: 0;
  padding-left: 18px;
  font-size: 11px;
  line-height: 1.7;
}
.action-btn {
  width: 100%;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: 9px;
  font-weight: 700;
  font-size: 12px;
  cursor: pointer;
  margin-bottom: 8px;
}
.action-btn.outline {
  background: transparent;
  color: var(--accent);
  border: 1px solid var(--accent);
}
.action-btn.outline:hover {
  background: rgba(46, 166, 255, 0.1);
}
.action-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.gp-toggles {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin: 4px 0 2px;
}
.gp-toggle {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: rgba(255, 255, 255, 0.04);
  border-radius: 10px;
  padding: 9px 11px;
  cursor: pointer;
  color: var(--text);
  font-size: 12px;
  font-weight: 600;
  transition: border-color 0.12s, background 0.12s, transform 0.08s;
}
.gp-toggle:active {
  transform: scale(0.98);
}
.gp-toggle.on {
  border-color: var(--accent);
  background: rgba(46, 166, 255, 0.14);
  color: #cfe6ff;
}
.gp-toggle-state {
  font-size: 10px;
  font-weight: 700;
  padding: 2px 7px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.1);
  color: var(--text-dim);
}
.gp-toggle.on .gp-toggle-state {
  background: var(--accent);
  color: #fff;
}
.gp-hint {
  font-size: 12px;
  color: var(--text-dim);
  margin: 4px 0 2px;
  line-height: 1.5;
}
.gp-key {
  font-size: 1.3em;
  color: var(--accent-2);
  font-weight: 700;
}
.gp-hint-name {
  color: var(--accent);
}
.gp-toggle.sel,
.gp-toggle.focused {
  border-color: var(--accent);
  box-shadow: 0 0 0 1px var(--accent), 0 0 8px rgba(46, 166, 255, 0.4);
}
.gp-toggle.on.sel,
.gp-toggle.on.focused {
  background: rgba(46, 166, 255, 0.2);
  color: #cfe6ff;
}
.gp-toggle.sel .gp-toggle-state,
.gp-toggle.focused .gp-toggle-state {
  background: var(--accent);
  color: #06203a;
}
.ac-sub {
  margin: 8px 0 6px;
}
.ac-sub code {
  background: rgba(255, 255, 255, 0.08);
  border-radius: 4px;
  padding: 1px 5px;
  font-size: 11px;
  color: var(--accent-2);
}
.ac-collapsed {
  margin: 8px 0 2px;
  opacity: 0.7;
}
.ac-list {
  display: flex;
  flex-direction: column;
  gap: 7px;
  margin-top: 4px;
}
.ac-row {
  display: flex;
  align-items: center;
  gap: 7px;
}
.ac-input {
  flex: 1 1 auto;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: var(--radius-ctrl);
  color: var(--text);
  font-size: 12px;
  padding: 8px 10px;
  outline: none;
  transition: border-color 0.12s, box-shadow 0.12s;
}
.ac-input:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 1px var(--accent);
}
.ac-input:disabled {
  opacity: 0.55;
}
.ac-del {
  flex: 0 0 auto;
  width: 30px;
  height: 30px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: rgba(229, 72, 77, 0.1);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  cursor: pointer;
  font-size: 13px;
  line-height: 1;
  transition: background 0.12s;
}
.ac-del:hover:not(:disabled) {
  background: rgba(229, 72, 77, 0.22);
}
.ac-del:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.ac-add {
  align-self: flex-start;
  border: 1px dashed rgba(46, 166, 255, 0.5);
  background: transparent;
  color: var(--accent);
  border-radius: var(--radius-ctrl);
  padding: 7px 12px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.12s;
}
.ac-add:hover:not(:disabled) {
  background: rgba(46, 166, 255, 0.1);
}
.ac-add:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.ac-name {
  color: var(--accent-2);
}
.upd-row {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 8px;
}
.upd-tag {
  font-size: 12px;
  color: var(--text-dim, #9aa4b2);
}
.upd-tag.ok {
  color: #4ec98b;
}
.upd-tag.err {
  color: #ff9ea1;
}
.upd-divider {
  width: 1px;
  height: 18px;
  background: rgba(255, 255, 255, 0.12);
  margin: 0 4px;
}
.ua-btn {
  border-style: dashed;
}
.ua-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.upd-has {
  margin-top: 10px;
  padding: 10px 12px;
  border: 1px solid rgba(46, 166, 255, 0.35);
  border-radius: var(--radius-card, 12px);
  background: rgba(46, 166, 255, 0.06);
}
.upd-line {
  font-size: 13px;
  margin: 0 0 4px;
}
.upd-notes {
  white-space: pre-wrap;
  line-height: 1.5;
  margin: 0 0 10px;
  opacity: 0.85;
}
.upd-install {
  border-style: solid;
  background: rgba(46, 166, 255, 0.14);
}
</style>
