// stores/wakeTask.ts — 唤醒后恢复 TDP + 电源预设
// TDP 页与睡眠优化页共享「唤醒后-执行任务」同一任务计划。两个页面的开关都绑定到这里的
// 单一真相 store.on，任一页面切换会立即同步到另一页面（KeepAlive 下页面不会随路由切换重新读取）。
import { defineStore } from 'pinia';
import { ref } from 'vue';
import { taskExists, toggleTask } from '@/bridge/yeman';

const TASK = '唤醒后-执行任务';

export const useWakeTaskStore = defineStore('wakeTask', () => {
  const on = ref(false); // 任务当前是否存在（两页共用）
  const busy = ref(false); // 切换中，防止重复点击
  let initialized = false;

  // 首次调用时从系统读取真实状态；之后幂等跳过（store.on 作为唯一真相，由各页切换维护）
  async function init() {
    if (initialized) return;
    initialized = true;
    try {
      on.value = await taskExists(TASK);
    } catch {
      on.value = false;
    }
  }

  // 切换任务并返回最终是否存在；失败时回滚 store.on 并抛出，交由调用页显示错误
  async function set(v: boolean): Promise<boolean> {
    busy.value = true;
    try {
      const result = await toggleTask(TASK, v);
      on.value = result;
      return result;
    } catch (e) {
      on.value = !v; // 回滚
      throw e;
    } finally {
      busy.value = false;
    }
  }

  return { on, busy, init, set };
});
