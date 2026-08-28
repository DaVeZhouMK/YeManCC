<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { dialog, keyboard } from '@/bridge/api';
import AppIcon from '@/components/AppIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { gameRuleNameFromPath, getGameRules, setGameRuleList, type GameRuleList, type GameRulesSnapshot } from '@/bridge/gameRules';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';

const props = defineProps<{ game: DetectedGame | null; open: boolean }>();
const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'toggle'): void;
  (e: 'status', value: { message?: string; error?: string }): void;
  (e: 'changed', value: { kind: GameRuleList; value?: string; currentBlacklisted?: boolean }): void;
}>();

type EditorKind = 'blacklist' | 'whitelist';

const editorOpen = ref<EditorKind | null>(null);
const rules = ref<GameRulesSnapshot>({ blacklist: [], whitelist: [] });
const draft = ref('');
const busy = ref(false);
const error = ref('');
const message = ref('');

const currentRule = computed(() => {
  const raw = props.game?.path || props.game?.name || '';
  return raw ? gameRuleNameFromPath(raw) : '';
});
const currentLabel = computed(() => detectedGameName(props.game) || props.game?.name || '当前游戏');

function displayRuleName(value: string): string {
  return value.includes('*') ? value : `${value}.exe`;
}

function status(messageText = '', errorText = ''): void {
  message.value = messageText;
  error.value = errorText;
  emit('status', { message: messageText, error: errorText });
}

async function loadRules(): Promise<void> {
  if (!props.open) return;
  try {
    rules.value = await getGameRules();
  } catch (e) {
    status('', `读取黑白名单失败：${(e as Error).message}`);
  }
}

function openEditor(kind: EditorKind): void {
  editorOpen.value = editorOpen.value === kind ? null : kind;
  draft.value = '';
  error.value = '';
  void loadRules();
}

async function saveRule(kind: GameRuleList, rawValue: string, allowPath = false): Promise<void> {
  const value = rawValue.trim();
  if (!value || busy.value) return;
  const release = tryAcquireQuickAction(`top-game-rule-${kind}`);
  if (!release) {
    status('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  busy.value = true;
  try {
    const trimmed = value.trim();
    // 手动输入只接受进程名；文件选择器会传入完整路径，先取文件名再规范化。
    if (!trimmed || (!allowPath && /[\\/]/.test(trimmed)) || /[*?]/.test(trimmed)) {
      status('', '请输入有效的进程名');
      return;
    }
    const baseName = trimmed.split(/[\\/]/).pop() || trimmed;
    const rule = gameRuleNameFromPath(baseName);
    if (!rule || rule.includes('*')) return;
    const values = rules.value[kind].includes(rule)
      ? rules.value[kind]
      : [...rules.value[kind], rule];
    rules.value = await setGameRuleList(kind, values);
    draft.value = '';
    status(`已添加${kind === 'blacklist' ? '黑名单' : '白名单'}：${displayRuleName(rule)}`);
    emit('changed', {
      kind,
      value: rule,
      currentBlacklisted: kind === 'blacklist' && rule === currentRule.value,
    });
  } catch (e) {
    status('', `保存${kind === 'blacklist' ? '黑名单' : '白名单'}失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

async function addCurrentToBlacklist(): Promise<void> {
  if (!currentRule.value || busy.value) return;
  await saveRule('blacklist', currentRule.value);
}

async function addCurrentToWhitelist(): Promise<void> {
  if (!currentRule.value || busy.value) return;
  await saveRule('whitelist', currentRule.value);
}

async function summonTouchKeyboard(): Promise<void> {
  // 点击/聚焦输入框时调用原生可见启动路径，确保 TabTip 真正出现在
  // WebView2 上层；shell.hidden 会把键盘启动到隐藏窗口，不能用于这里。
  await keyboard.open().catch(() => undefined);
}

async function chooseRuleExecutable(): Promise<void> {
  if (!editorOpen.value || busy.value) return;
  const kind = editorOpen.value;
  try {
    const picked = await dialog.openFile([
      { name: '可执行程序', extensions: ['exe'] },
    ]);
    if (!picked) return;
    await saveRule(kind, picked, true);
  } catch (e) {
    status('', `打开程序选择器失败：${(e as Error).message}`);
  }
}

async function removeRule(kind: GameRuleList, value: string): Promise<void> {
  if (busy.value) return;
  const removedIndex = rules.value[kind].indexOf(value);
  const release = tryAcquireQuickAction(`top-game-rule-remove-${kind}`);
  if (!release) {
    status('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  busy.value = true;
  try {
    rules.value = await setGameRuleList(kind, rules.value[kind].filter((item) => item !== value));
    nextTick(() => {
      const editor = document.querySelector<HTMLElement>(`[data-gp-rule-editor-kind="${kind}"]`);
      const items = editor
        ? Array.from(editor.querySelectorAll<HTMLElement>('[data-gp-rule-focus="rule-item"]'))
        : [];
      const next = items[Math.min(Math.max(removedIndex, 0), Math.max(items.length - 1, 0))]
        || editor?.querySelector<HTMLElement>('[data-gp-rule-focus="manual-input"]');
      if (next) focusGamepadElement(next);
    });
    status(`已移除${kind === 'blacklist' ? '黑名单' : '白名单'}：${displayRuleName(value)}`);
    emit('changed', { kind, value });
  } catch (e) {
    status('', `移除名单失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

function focusOpenedBody(): void {
  nextTick(() => {
    const first = document.querySelector<HTMLElement>(
      '[data-gp-game-rules-body] button:not(:disabled), [data-gp-game-rules-body] select:not(:disabled), [data-gp-game-rules-body] input:not(:disabled), [data-gp-game-rules-body] [tabindex]:not([tabindex="-1"])',
    );
    if (first) focusGamepadElement(first);
  });
}

function restoreRulesEntry(): void {
  nextTick(() => {
    const entry = document.querySelector<HTMLElement>('[data-gp-game-rules-entry]');
    if (entry) focusGamepadElement(entry);
  });
}

function disableLeavingBody(el: Element): void {
  if (!(el instanceof HTMLElement)) return;
  el.inert = true;
  el.setAttribute('aria-hidden', 'true');
}

function onBack(): void {
  if (!props.open) return;
  if (editorOpen.value) {
    editorOpen.value = null;
    draft.value = '';
    error.value = '';
    message.value = '';
    return;
  }
  emit('close');
}

watch(() => [props.open, props.game?.pid, props.game?.processCreated], (next, previous) => {
  if (props.open) {
    editorOpen.value = null;
    draft.value = '';
    void loadRules();
    // The head is removed from the focus list while open. Move focus into the
    // body immediately so the next D-pad press cannot restart from page[0].
    focusOpenedBody();
  } else if (previous?.[0] === true) {
    restoreRulesEntry();
  }
});

onMounted(() => {
  window.addEventListener('game-quick-rules-back', onBack);
});

onBeforeUnmount(() => {
  window.removeEventListener('game-quick-rules-back', onBack);
});
</script>

<template>
  <div
    class="game-rules-panel game-submenu-bubble"
    :class="{ expanded: open }"
    data-gp-game-rules
    :data-gp-expanded="open"
  >
    <button
      type="button"
      class="game-rules-head game-menu-steam-row"
      :class="{ active: open }"
      data-gp-game-rules-entry
      data-gp-game-row="rules-entry"
      tabindex="0"
      :aria-expanded="open"
      @click.stop="emit('toggle')"
    >
      <div>
        <AppIcon name="list" class="game-rules-head-icon" />
        <strong>游戏黑 / 白名单</strong>
        <small>{{ open ? '正在编辑识别名单' : (currentRule ? currentLabel : '编辑识别游戏') }}</small>
      </div>
      <span class="game-rules-chevron" aria-hidden="true">{{ open ? '⌃' : '⌄' }}</span>
    </button>

    <!-- 与专属配置使用同一套二级气泡展开动画，避免打开时突然跳出。 -->
    <Transition name="custom-submenu-pop" @before-leave="disableLeavingBody">
      <div v-if="open" class="game-rules-body" data-gp-game-rules-body>
      <div v-if="currentRule" class="game-rule-actions game-rule-current-actions" data-gp-group="game-rule-current-actions">
        <button type="button" class="danger" data-gp-game-row="rules-current" data-gp-rule-focus="current-blacklist" :disabled="busy || !currentRule" @click="addCurrentToBlacklist">
          <AppIcon name="close" />排除到黑名单
        </button>
        <button type="button" class="allow" data-gp-game-row="rules-current" data-gp-rule-focus="current-whitelist" :disabled="busy || !currentRule" @click="addCurrentToWhitelist">
          <AppIcon name="check" />添加到白名单
        </button>
      </div>

      <div class="game-rule-actions game-rule-editor-actions" data-gp-group="game-rule-editor-actions">
        <button type="button" data-gp-game-row="rules-editor" data-gp-rule-focus="editor-blacklist" :class="{ active: editorOpen === 'blacklist' }" :disabled="busy" @click="openEditor('blacklist')">
          <AppIcon name="list" />编辑黑名单
        </button>
        <button type="button" data-gp-game-row="rules-editor" data-gp-rule-focus="editor-whitelist" :class="{ active: editorOpen === 'whitelist' }" :disabled="busy" @click="openEditor('whitelist')">
          <AppIcon name="list" />编辑白名单
        </button>
      </div>

      <div v-if="editorOpen" class="game-rule-dropdown" data-gp-group="game-rule-dropdown" data-gp-game-row="rules-dropdown" :data-gp-rule-editor-kind="editorOpen">
        <div class="game-rule-dropdown-head">
          <strong>编辑名单</strong>
          <small>{{ rules[editorOpen].length }} 项</small>
        </div>
        <div v-if="rules[editorOpen].length" class="game-rule-list">
          <button v-for="item in rules[editorOpen]" :key="`${editorOpen}-${item}`" type="button" class="game-rule-item" data-gp-game-row="rules-dropdown" data-gp-rule-focus="rule-item" :aria-label="`移除${editorOpen === 'blacklist' ? '黑名单' : '白名单'}规则 ${displayRuleName(item)}`" :disabled="busy" @click="removeRule(editorOpen!, item)">
            <span>{{ displayRuleName(item) }}</span>
            <AppIcon name="trash" />
          </button>
        </div>
        <div v-else class="game-rule-empty">暂无{{ editorOpen === 'blacklist' ? '用户黑名单' : '白名单' }}规则</div>
        <div class="game-rule-add-row">
          <input
            v-model="draft"
            data-gp-rule-focus="manual-input"
            type="text"
            placeholder="手动输入进程名按回车确认"
            @focus="void summonTouchKeyboard()"
            @keydown.enter.prevent="saveRule(editorOpen!, draft)"
          />
          <button type="button" class="game-rule-confirm" data-gp-rule-focus="manual-confirm" :disabled="busy" @click="saveRule(editorOpen!, draft)">确认</button>
        </div>
        <button type="button" class="game-rule-pick" data-gp-rule-focus="file-add" :disabled="busy" @click="chooseRuleExecutable"><AppIcon name="plus" />添加</button>
      </div>

      <div v-if="message || error" class="game-rules-message" :class="{ error: !!error }">{{ error || message }}</div>

      <div class="game-rules-bottom-actions" data-gp-group="game-rules-bottom-actions" data-gp-game-row="rules-footer">
        <button type="button" class="rules-close-bottom" data-gp-rule-focus="rules-close" @click="emit('close')"><strong>B</strong>关闭页面</button>
      </div>
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.game-submenu-bubble {
  display: block;
}
.game-rules-panel {
  display: block;
}
.game-rules-panel.expanded {
  display: block;
}
.game-rules-head {
  width: 100%;
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  text-align: left;
  box-sizing: border-box;
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 48px;
  padding: 7px 9px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  box-shadow: 0 12px 30px rgba(0,0,0,.28);
  cursor: pointer;
}
.game-rules-head.game-menu-steam-row {
  display: flex;
  align-items: center;
  width: 100%;
  min-height: 54px;
  gap: 10px;
  padding: 10px 12px;
  border: 0;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
}
.game-rules-head.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
}
/* 手柄焦点：父气泡 overflow:hidden 会裁掉全局 .focused 的外发光，
 * 这里改用内描边，保证入口按钮选中态始终可见。 */
.game-rules-head.focused {
  box-shadow: inset 0 0 0 2px var(--accent), inset 0 0 10px color-mix(in srgb, var(--accent) 35%, transparent);
}
.game-rules-head > div { display: flex; align-items: center; gap: 7px; min-width: 0; }
.game-rules-head-icon { width: 15px; height: 15px; color: var(--accent); flex: 0 0 auto; }
.game-rules-chevron { width: 22px; color: var(--accent); font-size: 16px; line-height: 1; text-align: center; }
.custom-submenu-pop-enter-active, .custom-submenu-pop-leave-active {
  transition: opacity .16s ease, transform .16s ease;
  transform-origin: top center;
}
.custom-submenu-pop-enter-from, .custom-submenu-pop-leave-to {
  opacity: 0;
  transform: translateY(-5px) scale(.985);
}
.game-rules-body {
  display: grid;
  gap: 8px;
  margin: 8px 0 0;
  padding: 8px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  box-shadow: 0 16px 40px rgba(0,0,0,.4);
}
.game-rules-head strong { font-size: 13px; }
.game-rules-head small, .game-rule-dropdown-head small, .game-rule-empty { color: var(--text-dim); font-size: 10px; }
.game-rules-bottom-actions { display: flex; justify-content: flex-end; margin-top: 1px; }
.rules-close-bottom { min-height: 30px; padding: 5px 9px; border: 1px solid rgba(255,255,255,.08); border-radius: 7px; background: var(--bg-input); color: var(--text); font-size: 10px; cursor: pointer; }
.rules-close-bottom strong { margin-right: 4px; color: var(--danger); font-weight: 800; }
.rules-close, .game-rule-actions button, .game-rule-add-row button { min-height: 30px; padding: 5px 8px; border: 1px solid rgba(255,255,255,.08); border-radius: 7px; background: var(--bg-input); color: var(--text); font-size: 10px; cursor: pointer; }
.rules-close { flex: 0 0 auto; }
.game-rule-actions {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
  align-items: stretch;
}
.game-rule-actions button { width: 100%; min-width: 0; box-sizing: border-box; }
.game-rule-current-actions button,
.game-rule-editor-actions button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  min-height: 36px;
  padding: 6px 10px;
  font-size: 11px;
  white-space: nowrap;
}
.game-rule-actions button.danger { color: var(--danger); }
.game-rule-actions button.allow { color: var(--ok); }
.game-rule-actions button.active { color: var(--accent); border-color: color-mix(in srgb, var(--accent) 45%, transparent); }
.game-rule-actions button :deep(svg) { width: 14px; height: 14px; flex: 0 0 auto; margin: 0; vertical-align: initial; }
.game-rule-dropdown { display: grid; gap: 9px; padding: 13px; border: 1px solid rgba(255,255,255,.10); border-radius: 13px; background: color-mix(in srgb, var(--bg-solid) 34%, var(--bg-input)); box-shadow: inset 0 1px 0 rgba(255,255,255,.035); }
.game-rule-dropdown-head { display: flex; align-items: baseline; justify-content: space-between; gap: 8px; }
.game-rule-dropdown-head strong { font-size: 14px; font-weight: 800; letter-spacing: .01em; }
.game-rule-list { display: grid; gap: 3px; max-height: 170px; overflow-y: auto; padding: 2px 0; }
.game-rule-item { display: flex; align-items: center; justify-content: space-between; gap: 8px; width: 100%; min-height: 37px; padding: 6px 11px; border: 1px solid transparent; border-radius: 8px; background: color-mix(in srgb, var(--bg-input) 72%, transparent); color: var(--text); font: inherit; font-size: 11px; text-align: left; cursor: pointer; }
.game-rule-item span { min-width: 0; flex: 1; overflow-wrap: anywhere; }
.game-rule-item :deep(svg) { width: 14px; height: 14px; color: var(--danger); flex: 0 0 auto; }
.game-rule-add-row { display: grid; grid-template-columns: minmax(0, 1fr) 66px; gap: 6px; align-items: stretch; }
.game-rule-add-row input { min-width: 0; width: 100%; height: 38px; min-height: 38px; box-sizing: border-box; padding: 7px 11px; border: 1px solid #2a3342; border-radius: 9px; background: var(--bg-input); color: var(--text); font-size: 11px; }
.game-rule-add-row button { display: inline-flex; align-items: center; justify-content: center; gap: 4px; box-sizing: border-box; height: 38px; min-height: 38px; }
.game-rule-confirm { width: 66px; min-width: 66px; height: 38px; min-height: 38px; border-radius: 9px; font-size: 11px; font-weight: 700; }
.game-rule-pick { display: inline-flex; align-items: center; justify-content: center; gap: 7px; width: 100%; min-height: 38px; padding: 7px 11px; border: 1px solid color-mix(in srgb, var(--accent) 48%, rgba(255,255,255,.10)); border-radius: 9px; background: color-mix(in srgb, var(--accent) 16%, var(--bg-input)); color: var(--accent); font: inherit; font-size: 12px; font-weight: 800; cursor: pointer; }
.game-rule-pick :deep(svg) { width: 15px; height: 15px; }
.game-rule-item.focused { border-color: color-mix(in srgb, var(--accent) 78%, transparent); background: color-mix(in srgb, var(--accent) 12%, var(--bg-input)); box-shadow: inset 0 2px 0 var(--accent), inset 0 -2px 0 var(--accent); }
.game-rule-item:focus-visible, .game-rule-pick:focus-visible, .game-rule-confirm:focus-visible { outline: none; box-shadow: var(--focus-ring); }
.game-rules-message { color: var(--ok); font-size: 10px; line-height: 1.35; white-space: pre-line; }
.game-rules-message.error { color: var(--danger); }
button:disabled { opacity: .45; cursor: default; }
</style>
