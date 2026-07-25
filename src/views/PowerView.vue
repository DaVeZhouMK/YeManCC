<script setup lang="ts">
import { ref, reactive, computed, onMounted, nextTick, inject } from 'vue';
import Toggle from '@/components/Toggle.vue';
import WarnBar from '@/components/WarnBar.vue';
import {
  taskExists,
  toggleTask,
  fxExists,
  fxSet,
  joyExists,
  joySet,
  threeMarkExists,
  searchState,
  openSearchFolder,
} from '@/bridge/yeman';

const tasks = reactive({ desktopMode: false, xboxMode: false, energyStar: false, cleanMem: false, amd395: false });
type TaskKey = keyof typeof tasks;
// 双判定：桌面模式 或 Xbox 游戏模式 任一开启 → 屏幕自动旋转失效
const conflict = computed(() => tasks.desktopMode || tasks.xboxMode);

const audioOpt = ref(false);
const joyMouse = ref(false);
const searchSt = ref<'hidden' | 'trimmed' | 'full'>('full');

const busy = ref(false);
const errMsg = ref('');

async function safeExists(name: string): Promise<boolean> {
  try {
    return await taskExists(name);
  } catch {
    return false;
  }
}

async function refresh() {
  errMsg.value = '';
  // 并行异步加载（不串行等待，不阻塞渲染）
  const [dmRes, xbRes, esRes, cmRes, amdRes, fxRes, joyRes, searchRes] = await Promise.allSettled([
    safeExists('桌面模式-开机设置为桌面模式'),
    safeExists('Xbox大屏游戏模式'),
    safeExists('节能-能源之星'),
    safeExists('内存-开机自动内存清理并关闭'),
    safeExists('Bug修复-AMD-395'),
    fxExists(),
    joyExists(),
    searchState(),
  ]);
  if (dmRes.status === 'fulfilled') tasks.desktopMode = dmRes.value;
  if (xbRes.status === 'fulfilled') tasks.xboxMode = xbRes.value;
  if (esRes.status === 'fulfilled') tasks.energyStar = esRes.value;
  if (cmRes.status === 'fulfilled') tasks.cleanMem = cmRes.value;
  if (amdRes.status === 'fulfilled') tasks.amd395 = amdRes.value;
  if (fxRes.status === 'fulfilled') audioOpt.value = fxRes.value;
  if (joyRes.status === 'fulfilled') joyMouse.value = joyRes.value;
  if (searchRes.status === 'fulfilled') searchSt.value = searchRes.value;
}

async function toggleTaskSafe(name: string, on: boolean, key: TaskKey) {
  errMsg.value = '';
  busy.value = true;
  try {
    await toggleTask(name, on);
    // 参考其他正确操作：切换后重新检测真实状态，而非信任返回值（避免创建失败却显示已开）
    tasks[key] = await safeExists(name);
  } catch (e) {
    tasks[key] = await safeExists(name).catch(() => !on); // 回滚到真实状态
    errMsg.value = '任务计划操作失败：' + (e as Error).message + '（需管理员权限）';
  } finally {
    busy.value = false;
  }
}

async function onFx(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await fxSet(v);
    audioOpt.value = v;
  } catch (e) {
    audioOpt.value = !v;
    errMsg.value = 'FxSound 设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}
async function onJoy(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await joySet(v);
    joyMouse.value = v;
  } catch (e) {
    joyMouse.value = !v;
    errMsg.value = 'Joyxoff 设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}
async function onAmd395(v: boolean) {
  if (v) {
    let ok = true;
    try {
      ok = await threeMarkExists();
    } catch {
      ok = false;
    }
    if (!ok) {
      errMsg.value = '未检测到 3DMark.exe，无法启用 AMD395 修复。';
      return;
    }
  }
  await toggleTaskSafe('Bug修复-AMD-395', v, 'amd395');
}
const searchText = computed(() => {
  if (searchSt.value === 'hidden') return '不可用（系统版本差异）';
  if (searchSt.value === 'trimmed') return '任务栏搜索已精简 [点击恢复]';
  return '任务栏搜索存在多进程 [点击精简]';
});
async function onSearch() {
  try {
    await openSearchFolder();
  } catch {
    /* ignore */
  }
}

const rotText = computed(() => (conflict.value ? '屏幕自动旋转：不可用' : '屏幕自动旋转：可用'));

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
      <h3 class="card-title">🚀 启动模式</h3>
      <p class="muted small">{{ rotText }}</p>
      <Toggle v-model="tasks.desktopMode" label="桌面模式" description="开机设置为桌面模式" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('桌面模式-开机设置为桌面模式', v, 'desktopMode')" />
      <Toggle v-model="tasks.xboxMode" label="Xbox 游戏模式" description="大屏游戏模式" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('Xbox大屏游戏模式', v, 'xboxMode')" />
      <WarnBar v-if="conflict" text="Windows 屏幕自动旋转失效：桌面模式与 Xbox 模式互斥，请关闭其一。" />
    </section>

    <section class="card">
      <h3 class="card-title">⭐ 开机启动项</h3>
      <Toggle v-model="tasks.energyStar" label="开机能源之星自动优化" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('节能-能源之星', v, 'energyStar')" />
      <Toggle v-model="tasks.cleanMem" label="开机清理一次内存" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('内存-开机自动内存清理并关闭', v, 'cleanMem')" />
      <Toggle v-model="audioOpt" label="开机启动音质优化 (FxSound)" color="accent" :disabled="busy" @update:model-value="onFx" />
      <Toggle v-model="joyMouse" label="开机启动手柄模拟鼠标 (Joyxoff)" color="accent" :disabled="busy" @update:model-value="onJoy" />
      <Toggle v-model="tasks.amd395" label="AMD395 专门修复 (需 3DMark)" description="精简版 3DMark" color="accent" :disabled="busy" @update:model-value="onAmd395" />
      <div class="row">
        <span>🔍 Windows 任务栏搜索</span>
        <button class="mini-btn" @click="onSearch">{{ searchText }}</button>
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
.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 8px;
}
.small {
  font-size: 11px;
  margin: 0 0 8px;
}
.mini-btn {
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: 6px;
  padding: 5px 8px;
  font-size: 11px;
  cursor: pointer;
  max-width: 200px;
  text-align: right;
}
.mini-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
</style>
