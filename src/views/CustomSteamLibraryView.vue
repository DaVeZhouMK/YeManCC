<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import {
  CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED,
  CUSTOM_STEAM_LIBRARY_TITLE,
  launchCustomSteamLibrary,
  stopCustomSteamLibrarySession,
} from '@/bridge/customSteamLibrary';

const router = useRouter();
const status = ref('准备启动…');
const error = ref('');

onMounted(async () => {
  if (!CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED) {
    await router.replace('/schedule');
    return;
  }
  try {
    const result = await launchCustomSteamLibrary();
    if (!result.ok) throw new Error(result.reason || '启动失败');
    status.value = `${CUSTOM_STEAM_LIBRARY_TITLE} 正在启动，父程序输入已屏蔽`;
  } catch (cause) {
    await stopCustomSteamLibrarySession();
    error.value = cause instanceof Error ? cause.message : String(cause);
    status.value = '启动失败';
  }
});

function onLibraryClosed() {
  status.value = `${CUSTOM_STEAM_LIBRARY_TITLE} 已返回，正在恢复主程序焦点`;
  void router.replace('/schedule');
}

function onLibraryConflict(event: Event) {
  const detail = (event as CustomEvent<{ inputOwner?: string }>).detail;
  status.value = `${CUSTOM_STEAM_LIBRARY_TITLE} 未接管输入，已阻止主程序重复响应`;
  error.value = `检测到现有窗口输入所有者：${detail?.inputOwner || 'unknown'}。请先关闭独立版窗口，再从主程序打开。`;
  void router.replace('/schedule');
}

onMounted(() => window.addEventListener('customSteamLibrary:closed', onLibraryClosed));
onMounted(() => window.addEventListener('customSteamLibrary:conflict', onLibraryConflict));

onBeforeUnmount(() => {
  window.removeEventListener('customSteamLibrary:closed', onLibraryClosed);
  window.removeEventListener('customSteamLibrary:conflict', onLibraryConflict);
  void stopCustomSteamLibrarySession();
});
</script>

<template>
  <section class="page custom-steam-library-staging">
    <div class="card">
      <h3 class="card-title">{{ CUSTOM_STEAM_LIBRARY_TITLE }}</h3>
      <p class="muted">{{ status }}</p>
      <p v-if="error" class="err-bar">{{ error }}</p>
    </div>
  </section>
</template>

<style scoped>
.custom-steam-library-staging {
  padding-bottom: 20px;
}
.muted {
  color: var(--text-dim);
  font-size: 12px;
}
.err-bar {
  margin-top: 10px;
  padding: 8px 10px;
  border: 1px solid rgba(229, 72, 77, 0.4);
  border-radius: var(--radius-ctrl);
  color: #ff9ea1;
  background: rgba(229, 72, 77, 0.12);
  font-size: 11px;
}
</style>
