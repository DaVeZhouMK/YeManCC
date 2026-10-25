<script setup lang="ts">
import Toggle from './Toggle.vue';
defineOptions({ inheritAttrs: false });
const props = defineProps<{ modelValue: boolean; label?: string; description?: string; color?: 'ac'|'dc'|'accent'|'danger'; disabled?: boolean; compact?: boolean; gpRow?: number; gpCol?: number }>();
const emit = defineEmits<{ (event: 'update:modelValue', value: boolean): void }>();
</script>
<template><Toggle v-bind="{ ...props, ...$attrs }" class="monitor-toggle" @update:model-value="value => emit('update:modelValue', value)" /></template>
