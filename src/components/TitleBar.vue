<script setup lang="ts">
</script>

<template>
  <header class="titlebar app-region-drag">
    <div class="tb-brand">
      <span class="tb-bolt">⚡</span>
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
  background: linear-gradient(180deg, #141b27, #0e131c);
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
