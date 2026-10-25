<script setup lang="ts">
// [AI:UI:PAGE] UI最低布局检查场景：1280×720、1280×800；计入导航、边距、弹窗后的实际可用空间。
// 启用的交互项须有手柄到达/返回路径；按视觉同行/相邻行自然移动，滚动后焦点完整可见。
// 页面显示/隐藏不代表功能开关；用户动作约定看对应 [AI:USER:*]，导航实现看 src/gamepad/engine.ts。
defineProps<{title:string;hideHeading?:boolean}>();
</script>
<template>
 <div class="integrated-page" role="region" :aria-label="title">
  <header v-if="!hideHeading" class="console-heading"><h1>{{title}}</h1><slot name="status" /></header>
  <div class="integrated-page-body"><slot/></div>
 </div>
</template>
<style scoped>
.integrated-page{width:100%;min-width:0;color:var(--text);font-size:14px}.console-heading{display:flex;align-items:center;gap:10px;padding:3px 1px 10px}.console-heading h1{font-size:20px;line-height:1.2;margin:0;font-weight:650;letter-spacing:.2px}.integrated-page-body{min-width:0}
</style>
