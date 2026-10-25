<script setup lang="ts">
import { computed } from 'vue';
import { KEYBOARD_GLYPHS } from '@/bridge/keyboardGlyphAssets';
const props=defineProps<{code:string;label:string}>();
const glyph=computed(()=>{let key=props.code.replace(/^Key/,'').replace(/^Digit/,'').toLowerCase();const aliases:Record<string,string>={controlleft:'ctrl',shiftleft:'shift',altleft:'alt',metaleft:'win',capslock:'capslock',escape:'escape',backspace:'backspace',printscreen:'printscreen',scrolllock:'scrolllock',pause:'pause',pageup:'page_up',pagedown:'page_down',tilde:'tilde',leftbracket:'bracket_open',rightbracket:'bracket_close',equal:'equals',minus:'minus',plus:'plus',space:'space'};key=aliases[key]||key;return KEYBOARD_GLYPHS[key]||''});
</script>
<template><span class="keyboard-key-glyph" aria-hidden="true" v-html="glyph || label"/></template>
<style scoped>
.keyboard-key-glyph{display:flex;align-items:center;justify-content:center;width:30px;height:30px;font:inherit}.keyboard-key-glyph :deep(svg){width:100%;height:100%;display:block}.keyboard-key-glyph :deep(path),.keyboard-key-glyph :deep(polygon),.keyboard-key-glyph :deep(rect){fill:currentColor}
</style>
