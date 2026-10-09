// stores/debug.ts — 桥日志总线 + 手柄动作日志
// main.ts 启动时调用 installLogSink() 把 ipc.setLogSink 接到这里，
// 这样每条原生 API 调用的原始返回都会实时出现在调试面板。
//
// 桥日志仍完整保留最近 600 条（顺序/id/ts/cmd/args/result/error 不变），
// 但改用有界环形存储 + 惰性快照：达到 600 后每条追加不再移动整段响应式数组，
// 且只有消费者实际读取 logs 时才生成按时间顺序的快照。
import { defineStore } from 'pinia';
import { computed, ref } from 'vue';
import { setLogSink, type LogInput, type LogEntry } from '@/bridge/ipc';
import { createLogRing } from '@/stores/debugLogBuffer';

export const useDebugStore = defineStore('debug', () => {
  const buffer = createLogRing(600);
  const logs = computed<LogEntry[]>(() => buffer.snapshot());
  const gamepad = ref<string[]>([]);

  let seq = 0;
  function pushLog(e: LogInput) {
    buffer.append({ id: ++seq, ts: Date.now(), ...e });
  }
  function pushGamepad(action: string) {
    const t = new Date().toLocaleTimeString('zh-CN', { hour12: false });
    gamepad.value.unshift(`[${t}] ${action}`);
    if (gamepad.value.length > 60) gamepad.value.length = 60;
  }
  function clear() {
    buffer.clear();
    gamepad.value = [];
  }

  function installLogSink() {
    setLogSink((e: LogInput) => pushLog(e));
  }

  return { logs, gamepad, pushLog, pushGamepad, clear, installLogSink };
});
