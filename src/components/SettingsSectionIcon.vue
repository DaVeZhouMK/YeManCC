<script setup lang="ts">
defineProps<{name?:string}>();
const paths:Record<string,string>={sleep:'M20 14.5A8 8 0 1 1 10.5 4 6.5 6.5 0 0 0 20 14.5Z',power:'M12 3v9M6.4 5.6a8 8 0 1 0 11.2 0',monitor:'M3 4h18v13H3zM8 21h8M12 17v4M6 13l3-4 3 2 5-5',gamepad:'M7 7h10c3 0 5 10 3 11-2 1-4-3-5-3H9c-1 0-3 4-5 3-2-1 0-11 3-11ZM6 11h4M8 9v4M16 10h.1M18 12h.1',gyro:'M12 2v20M2 12h20M5 5l14 14M4 16c-2-8 8-15 15-9M20 8l-1-3-3 1',layers:'M12 3 2 8l10 5 10-5ZM2 12l10 5 10-5M2 16l10 5 10-5',tools:'m15 4 1 4 4 1a6 6 0 0 1-7 7l-6 6-4-4 6-6a6 6 0 0 1 6-8Z',grid:'M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z',check:'m5 12 4 4L19 6',back:'M5 3h14v18H5zM8 7h2v5H8zM14 7h2v5h-2z',signal:'M4 20v-4M9 20v-8M14 20V8M19 20V3',arrow:'m9 5 7 7-7 7'};
</script>
<template><svg class="settings-section-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path :d="paths[name||'grid']||paths.grid"/></svg></template>

<style scoped>.settings-section-icon{width:21px;height:21px;flex:0 0 auto;vertical-align:middle}</style>
