<script setup lang="ts">
import { nextTick, onDeactivated, ref } from 'vue';
import IntegratedPage from '@/components/IntegratedPage.vue';
import MonitorOutputSettings from '@/components/MonitorOutputSettings.vue';
import MonitorEditorWindow from '@/components/MonitorEditorWindow.vue';
import { focusGamepadElement } from '@/gamepad/focus';
const editorOpen=ref(false),page=ref<HTMLElement|null>(null);
let restoreEntry=false;
onDeactivated(()=>{restoreEntry=false;editorOpen.value=false;});
function closeEditor(){restoreEntry=true;editorOpen.value=false;}
async function ready(){if(!restoreEntry)return;restoreEntry=false;await nextTick();focusGamepadElement(page.value?.querySelector<HTMLElement>('[data-monitor-edit]')??null);}
</script>
<template>
 <IntegratedPage title="监控" hide-heading><div ref="page" class="page" :inert="editorOpen||undefined"><MonitorOutputSettings v-if="!editorOpen" overview-only @ready="ready" @edit="editorOpen=true"/></div></IntegratedPage>
 <MonitorEditorWindow v-if="editorOpen" @close="closeEditor"/>
</template>
