<script setup lang="ts">
import { ref, onMounted, nextTick, inject } from 'vue';
import Toggle from '@/components/Toggle.vue';
import StateCard from '@/components/StateCard.vue';
import { shell } from '@/bridge/api';
import {
  STEAM_ADDONS,
  type SteamAddonKey,
  steamAddonExists,
  steamAddonSet,
  steamExeExists,
  steamMasterOn,
  steamMasterSet,
  steamRunning,
  launchSteam,
} from '@/bridge/yeman';

const running = ref(false);
const master = ref(false);
const addonStates: Record<string, boolean> = {};
const states = ref({ ...addonStates });

const busy = ref(false);
const errMsg = ref('');

// 并行异步加载（不串行等待，不阻塞渲染；不检测绝对路径，启动均走非绝对路径的 Steam 启动代码）
async function refresh() {
  errMsg.value = '';
  const [runRes, masterRes, ...addonRes] = await Promise.allSettled([
    steamRunning(),
    steamMasterOn(),
    ...STEAM_ADDONS.map((a) => steamAddonExists(a.key).catch(() => false)),
  ]);
  if (runRes.status === 'fulfilled') running.value = runRes.value;
  if (masterRes.status === 'fulfilled') master.value = masterRes.value;
  const next: Record<string, boolean> = {};
  STEAM_ADDONS.forEach((a, i) => {
    next[a.key] = addonRes[i]?.status === 'fulfilled' ? addonRes[i].value : false;
  });
  states.value = next;
}

// 点击启动 Steam 大屏（执行 VBS：启动 Steam + 检查 RTSS 锁帧 + 界面）
async function launch() {
  errMsg.value = '';
  busy.value = true;
  try {
    await launchSteam();
    running.value = true;
    // 异步启动，20s 后重新检测运行状态
    setTimeout(() => refresh(), 20000);
  } catch (e) {
    errMsg.value = 'Steam 启动失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function onMaster(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await steamMasterSet(v);
    master.value = v;
  } catch (e) {
    master.value = !v;
    errMsg.value = '写入 .earlystart 失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function onAddon(key: SteamAddonKey, v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    if (v) {
      const exeOk = await steamExeExists(key);
      if (!exeOk) {
        const cfg = STEAM_ADDONS.find((a) => a.key === key);
        errMsg.value = '未检测到：' + (cfg?.name ?? key) + '\n请先安装对应程序。';
        states.value = { ...states.value, [key]: false };
        return;
      }
    }
    await steamAddonSet(key, v);
    states.value = { ...states.value, [key]: v };
  } catch (e) {
    states.value = { ...states.value, [key]: !v };
    errMsg.value = '写入失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function launchBigPicture() {
  // 非绝对路径：通过 steam:// 协议交给系统处理（不依赖 Steam.exe 的绝对路径）
  try {
    await shell.run('cmd', ['/c', 'start "" "steam://open/bigpicture"']);
  } catch {
    /* Steam 可能未安装 */
  }
}

// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  import('vue').then(({ watch }) => watch(globalRefreshKey, () => refresh()));
}

onMounted(() => nextTick(refresh));
</script>

<template>
  <div class="page">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>

    <section class="card">
      <h3 class="card-title">🎮 Steam 大屏</h3>
      <div class="states-row">
        <StateCard title="Steam" :state="running ? 'on' : 'off'" :text="running ? '运行中' : '未运行'" />
      </div>
      <div class="btn-row">
        <button class="action-btn" :disabled="busy || running" @click="launch">▶ 启动 Steam 大屏（联动）</button>
        <button class="action-btn ghost" @click="launchBigPicture">⛶ 仅启动 Steam 大屏模式</button>
      </div>
      <Toggle v-model="master" label="Steam 高级开机启动 (.earlystart)" description="写入用户目录 .earlystart" color="accent" :disabled="busy" @update:model-value="onMaster" />
    </section>

    <section class="card">
      <h3 class="card-title">🔗 联动启动项</h3>
      <div v-for="l in STEAM_ADDONS" :key="l.key" class="addon-row">
        <Toggle
          v-model="states[l.key]"
          :label="l.name"
          color="accent"
          :disabled="busy"
          @update:model-value="(v: boolean) => onAddon(l.key, v)"
        />
      </div>
    </section>
  </div>
</template>

<style scoped>
.page {
  padding-bottom: 20px;
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
.btn-row {
  display: flex;
  gap: 8px;
  margin: 4px 0 6px;
}
.action-btn {
  flex: 1;
  width: 100%;
  background: var(--accent);
  color: #06121d;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: 9px;
  font-weight: 700;
  font-size: 12px;
  cursor: pointer;
}
.action-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.action-btn.ghost {
  background: var(--bg-input);
  color: var(--text);
}
.states-row {
  display: flex;
  gap: 8px;
  margin-bottom: 8px;
}
.states-row .state-card {
  flex: 1;
}
.addon-row {
  display: flex;
  flex-direction: column;
}
</style>
