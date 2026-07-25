// stores/debug.ts — 桥日志总线 + 手柄动作日志
// main.ts 启动时调用 installLogSink() 把 ipc.setLogSink 接到这里，
// 这样每条原生 API 调用的原始返回都会实时出现在调试面板。
import { defineStore } from 'pinia';
import { ref } from 'vue';
import { setLogSink, type LogInput, type LogEntry } from '@/bridge/ipc';

export const useDebugStore = defineStore('debug', () => {
  const logs = ref<LogEntry[]>([]);
  const gamepad = ref<string[]>([]);
  const MAX = 600;

  let seq = 0;
  function pushLog(e: LogInput) {
    logs.value.push({ id: ++seq, ts: Date.now(), ...e });
    if (logs.value.length > MAX) logs.value.splice(0, logs.value.length - MAX);
  }
  function pushGamepad(action: string) {
    const t = new Date().toLocaleTimeString('zh-CN', { hour12: false });
    gamepad.value.unshift(`[${t}] ${action}`);
    if (gamepad.value.length > 60) gamepad.value.length = 60;
  }
  function clear() {
    logs.value = [];
    gamepad.value = [];
  }

  function installLogSink() {
    setLogSink((e: LogInput) => pushLog(e));
  }

  return { logs, gamepad, pushLog, pushGamepad, clear, installLogSink };
});
