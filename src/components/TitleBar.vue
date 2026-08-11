<script setup lang="ts">
import InlineIcon from './InlineIcon.vue';
</script>

<template>
  <header class="titlebar app-region-drag">
    <div class="tb-brand">
      <span class="tb-bolt"><InlineIcon name="yeman" /></span>
      <div class="tb-titles">
        <span class="tb-title">YeMan Control Center</span>
        <span class="tb-sub">野蛮系统控制中心</span>
      </div>
    </div>
  </header>
</template>

<style scoped>
.titlebar {
  height: 38px;
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 8px 0 12px;
  /* 磨砂玻璃：Steam 大屏黑蓝色，82% 可见 */
  background: linear-gradient(180deg, rgba(11, 16, 24, 0.82), rgba(11, 16, 24, 0.82));
  border-bottom: 1px solid #1c2533;
}
.tb-brand {
  display: flex;
  align-items: center;
  gap: 7px;
}
.tb-titles {
  display: flex;
  flex-direction: column;
  line-height: 1.15;
}
.tb-sub {
  font-size: 9px;
  font-weight: 500;
  letter-spacing: 0.5px;
  color: var(--text-dim);
}
.tb-bolt {
  color: var(--accent-2);
  font-size: 15px;
}
.tb-title {
  font-size: 13px;
  font-weight: 700;
  letter-spacing: 0.5px;
  color: var(--text);
}
</style>
