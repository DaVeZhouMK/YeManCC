<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { useDebugStore } from '@/stores/debug';
import { fs, shell } from '@/bridge/api';
import { isNativeRuntime } from '@/bridge/ipc';
import * as yeman from '@/bridge/yeman';

const emit = defineEmits<{ (e: 'close'): void }>();
const store = useDebugStore();

type Tab = 'log' | 'txt' | 'task' | 'pad';
const tab = ref<Tab>('log');

// ── txt 读写测试器 ──
const txtPath = ref('C:\\SOFT\\YeMan\\PowerControl\\tdp-ac.txt');
const txtContent = ref('');
const txtMsg = ref('');
async function txtRead() {
  txtMsg.value = '';
  try {
    if (!(await fs.exists(txtPath.value))) {
      txtContent.value = '';
      txtMsg.value = '文件不存在';
      return;
    }
    txtContent.value = await fs.readTextFile(txtPath.value);
    txtMsg.value = '读取成功';
  } catch (e) {
    txtMsg.value = '读取失败: ' + (e as Error).message;
  }
}
async function txtWrite() {
  txtMsg.value = '';
  try {
    await fs.writeTextFile(txtPath.value, txtContent.value);
    txtMsg.value = '写入成功';
  } catch (e) {
    txtMsg.value = '写入失败: ' + (e as Error).message;
  }
}

// ── schtasks 测试器 ──
const taskName = ref('TDP-插电AC模式TDP调节');
const taskResult = ref('');
async function taskQuery() {
  taskResult.value = '查询中...';
  try {
    const exists = await yeman.taskExists(taskName.value);
    taskResult.value = exists ? '✅ 任务存在' : '⬜ 任务不存在';
  } catch (e) {
    taskResult.value = '查询失败: ' + (e as Error).message;
  }
}
async function taskDelete() {
  taskResult.value = '删除中...';
  try {
    const ok = await yeman.deleteTask(taskName.value);
    taskResult.value = ok ? '🗑 已删除' : '删除失败';
  } catch (e) {
    taskResult.value = '删除失败: ' + (e as Error).message;
  }
}

const logsText = computed(() =>
  store.logs
    .slice()
    .reverse()
    .map((l) => {
      const t = new Date(l.ts).toLocaleTimeString('zh-CN', { hour12: false });
      const ok = l.ok ? '✓' : '✗';
      const res = l.ok ? JSON.stringify(l.result) : l.error;
      return `${t} ${ok} ${l.cmd} ${JSON.stringify(l.args)} => ${res}`;
    })
    .join('\n')
);

onMounted(() => {
  store.pushGamepad('调试面板已打开');
});
</script>

<template>
  <div class="debug-overlay">
    <div class="debug-panel">
      <header class="db-head app-region-drag">
        <span>🛠 调试面板</span>
        <span class="db-runtime" :class="isNativeRuntime ? 'ok' : 'warn'">
          {{ isNativeRuntime ? '原生运行时' : '非原生(浏览器)' }}
        </span>
        <button class="db-close app-region-no-drag" @click="emit('close')">✕</button>
      </header>

      <nav class="db-tabs app-region-no-drag">
        <button :class="{ active: tab === 'log' }" @click="tab = 'log'">桥日志</button>
        <button :class="{ active: tab === 'txt' }" @click="tab = 'txt'">txt 测试</button>
        <button :class="{ active: tab === 'task' }" @click="tab = 'task'">schtasks</button>
        <button :class="{ active: tab === 'pad' }" @click="tab = 'pad'">手柄动作</button>
        <button class="db-clear" @click="store.clear()">清空</button>
      </nav>

      <section class="db-body">
        <div v-show="tab === 'log'" class="db-log">{{ logsText || '（暂无桥调用）' }}</div>

        <div v-show="tab === 'txt'" class="db-form">
          <label>路径</label>
          <input v-model="txtPath" class="db-input" />
          <label>内容</label>
          <textarea v-model="txtContent" class="db-area" rows="3"></textarea>
          <div class="db-row">
            <button @click="txtRead">读取</button>
            <button @click="txtWrite">写入</button>
            <span class="db-msg">{{ txtMsg }}</span>
          </div>
        </div>

        <div v-show="tab === 'task'" class="db-form">
          <label>任务名</label>
          <input v-model="taskName" class="db-input" />
          <div class="db-row">
            <button @click="taskQuery">查询</button>
            <button @click="taskDelete">删除</button>
          </div>
          <div class="db-msg">{{ taskResult }}</div>
          <p class="muted small">提示：创建需对应 XML 模板（M3+ 接入）。</p>
        </div>

        <div v-show="tab === 'pad'" class="db-log">{{ store.gamepad.join('\n') || '（暂无手柄动作）' }}</div>
      </section>
    </div>
  </div>
</template>

<style scoped>
.debug-overlay {
  position: fixed;
  inset: 0;
  z-index: 50;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
}
.debug-panel {
  width: 92%;
  height: 80%;
  background: #0e131c;
  border: 1px solid #2a3342;
  border-radius: var(--radius);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  box-shadow: 0 12px 48px rgba(0, 0, 0, 0.6);
}
.db-head {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 12px;
  background: #141b27;
  border-bottom: 1px solid #1c2533;
  font-weight: 600;
}
.db-runtime {
  font-size: 11px;
  margin-left: auto;
}
.db-runtime.ok {
  color: var(--ok);
}
.db-runtime.warn {
  color: var(--accent-2);
}
.db-close {
  background: transparent;
  border: none;
  color: var(--text-dim);
  cursor: pointer;
  font-size: 14px;
}
.db-tabs {
  display: flex;
  gap: 4px;
  padding: 6px 8px;
  border-bottom: 1px solid #1c2533;
}
.db-tabs button {
  background: transparent;
  border: 1px solid transparent;
  color: var(--text-dim);
  font-size: 12px;
  padding: 5px 10px;
  border-radius: 6px;
  cursor: pointer;
}
.db-tabs button.active {
  background: #162434;
  color: var(--accent);
  border-color: rgba(46, 166, 255, 0.35);
}
.db-clear {
  margin-left: auto !important;
  color: var(--text-dim) !important;
}
.db-body {
  flex: 1 1 auto;
  overflow: auto;
  padding: 10px;
}
.db-log {
  font-family: 'Cascadia Code', 'Consolas', monospace;
  font-size: 11px;
  line-height: 1.5;
  white-space: pre-wrap;
  color: #b9c4d4;
}
.db-form label {
  display: block;
  font-size: 11px;
  color: var(--text-dim);
  margin: 8px 0 4px;
}
.db-input,
.db-area {
  width: 100%;
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: 6px;
  padding: 6px 8px;
  font-size: 12px;
  font-family: inherit;
}
.db-area {
  resize: vertical;
}
.db-row {
  display: flex;
  gap: 8px;
  align-items: center;
  margin-top: 8px;
}
.db-row button {
  background: var(--accent);
  color: #06121d;
  border: none;
  border-radius: 6px;
  padding: 6px 14px;
  font-weight: 600;
  font-size: 12px;
  cursor: pointer;
}
.db-msg {
  font-size: 11px;
  color: var(--ok);
}
.small {
  font-size: 11px;
  margin: 8px 0 0;
}
</style>
