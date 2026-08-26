const pending = new Map();
let requestId = 1;
let snapshot = { config: {}, manualOverrides: { items: {} }, library: { games: [] }, steamPlan: { items: [], summary: {} } };
let activeTab = 'not-in-steam';
let selectedSteamTarget = { accountId: '', steamRoot: '' };
let selectedGameDirectories = null;
let selectedSteamDirectories = null;
let selectedCategoryDirectories = { unclassified: new Set(), 'non-game': new Set() };
let selectedActionGame = null;
let editDraft = null;
let confirmResolver = null;
let artworkDraft = { cover: null, wallpaper: null, dirty: { cover: false, wallpaper: false } };
let artworkPageBase = { cover: null, wallpaper: null };
let artworkSearchType = "cover";
let artworkActionType = "cover";
let artworkCandidates = [];
let downloadingCandidateUrl = "";
let artworkTaskToken = 0;
let artworkTaskInFlight = false;
const ARTWORK_PREFETCH_TTL_MS = 5 * 60 * 1000;
const ARTWORK_PREFETCH_MAX_PRELOADS = 16;
const artworkPrefetchCache = new Map();
let artworkPrefetchPendingCount = 0;
let cancelInFlight = false;
let searchReturnToEdit = false;
let identitySearchInFlight = false;
let steamIdClearPending = false;
let identitySearchCancelled = false;
let editSaveInFlight = false;
let identitySearchToken = 0;
let identitySearchJobId = '';
let identitySearchExecutable = '';
let identitySearchQuery = '';
// A typed Steam AppID is still a candidate until the user confirms the
// result. Keep it in the result list while the asynchronous provider search
// updates, instead of silently taking a fast path around player confirmation.
let identitySearchManualCandidate = null;
const manualIdentifyGenerations = new Map();
let identitySearchEllipsisTimer = 0;
let identitySearchEllipsisStep = 0;
let currentCardDirectory = '';
let libraryFocusMemory = null;
let manualRefreshProgress = { phase: 'idle', current: 0, total: 0 };
let controllerBackLastAt = 0;
let controllerBackResetTimer = 0;
const CONTROLLER_B_DOUBLE_MS = 500;
let lastDirectionalAction = '';
let lastDirectionalActionAt = 0;
const DIRECTIONAL_ACTION_DEDUPE_MS = 90;
// The modal takes DOM focus away from the card. Keep the card directory
// separately so closing the editor can return the single navigation focus to
// the exact card that opened it, even after the card's blur handler ran.
let editReturnCardDirectory = '';
const NOTICE_DURATION_MS = 8000;
const noticeQueue = [];
let noticeActive = false;
let noticeTimer = 0;
let noticeSequence = 0;
const workspaceDropdownStates = new WeakMap();
let openWorkspaceDropdownState = null;

const $ = selector => document.querySelector(selector);
const library = $('#library');
const busy = $('#busy');
const notice = $('#notice');
const libraryNotice = $('#library-notice');
// Notices are status-only surfaces. Keep them outside keyboard/gamepad focus even when visible.
[notice, libraryNotice].forEach(element => { if (element) element.inert = true; });
const toastNotice = document.createElement('section');
toastNotice.id = 'global-notice';
toastNotice.className = 'notice hidden';
toastNotice.setAttribute('role', 'status');
toastNotice.setAttribute('aria-live', 'polite');
toastNotice.setAttribute('aria-atomic', 'true');
toastNotice.inert = true;
document.body.appendChild(toastNotice);

// Keep the standalone workspace visually aligned with the parent YeManCC page.
// The entry title is a product name, while the blue section eyebrow belongs to
// the old prototype layout and should not be shown here.
const entryTitle = $('.app-heading h1');
const entrySubtitle = $('.app-heading p');
const libraryTitle = $('.library-heading h1');
if (entryTitle) entryTitle.textContent = 'Steam自定义游戏库';
if (entrySubtitle) entrySubtitle.textContent = '扫描非Steam游戏加入Steam大屏';
if (libraryTitle) libraryTitle.textContent = 'Steam自定义游戏库';
// The standalone executable no longer exposes the old entry/statistics page.
// Switch the shell to the actual library view before the first async snapshot
// so the removed first-level menu cannot flash during startup.
$('#entry-view')?.classList.add('hidden');
$('#library-view')?.classList.remove('hidden');
const libraryBackButton = $('#back-entry-button');
if (libraryBackButton) {
  libraryBackButton.textContent = window.__customSteamLibraryParentMode === true ? '返回主程序' : '关闭窗口';
}

// The main YeManCC UI uses a button trigger and a teleported listbox for its
// dropdowns. Native <select> controls are not reliable recipients of semantic
// gamepad actions inside this WebView: the browser may consume the arrow/A
// event before the workspace router sees it, and their popup chrome differs by
// Windows/WebView version. Keep the native select as the data/model endpoint,
// but expose a deterministic, controller-friendly visual control beside it.
function workspaceDropdownOptionText(option) {
  return option ? String(option.textContent || option.value || '').trim() : '';
}
function workspaceDropdownTrigger(state) { return state?.trigger || null; }
function workspaceDropdownOpen(state) { return Boolean(state?.open); }
function workspaceDropdownVisibleOptions(state) {
  return [...(state?.select?.options || [])].filter(option => !option.disabled);
}
function workspaceDropdownSetPosition(state) {
  if (!state?.menu || !state.open || !state.trigger) return;
  const rect = state.trigger.getBoundingClientRect();
  const safe = 8;
  const desired = Math.min(360, Math.max(42, state.menu.children.length * 44 + 12));
  const below = Math.max(36, window.innerHeight - rect.bottom - safe);
  const above = Math.max(36, rect.top - safe);
  const abovePlacement = below < Math.min(desired, 360) && above > below;
  const maxHeight = Math.max(42, Math.min(desired, abovePlacement ? above : below));
  // Name candidates are the one editor dropdown allowed to grow leftward:
  // the name field lives near the right edge and a trigger-width menu clips
  // long titles. Keep other controls bounded to their own column.
  const isNameCandidates = state.select?.id === 'identity-name-candidates';
  const width = isNameCandidates
    ? Math.min(460, Math.max(rect.width, 360), window.innerWidth - safe * 2)
    : Math.max(160, Math.min(rect.width, window.innerWidth - safe * 2));
  const preferredLeft = isNameCandidates ? rect.right - width : rect.left;
  const left = Math.max(safe, Math.min(preferredLeft, window.innerWidth - safe - width));
  state.menu.style.position = 'fixed';
  state.menu.style.left = `${left}px`;
  state.menu.style.width = `${width}px`;
  state.menu.style.maxHeight = `${maxHeight}px`;
  if (abovePlacement) {
    state.menu.style.top = 'auto';
    state.menu.style.bottom = `${Math.max(safe, window.innerHeight - rect.top + 6)}px`;
  } else {
    state.menu.style.bottom = 'auto';
    state.menu.style.top = `${Math.min(window.innerHeight - safe - maxHeight, rect.bottom + 6)}px`;
  }
}
function closeWorkspaceDropdown(restoreFocus = false) {
  const state = openWorkspaceDropdownState;
  if (!state?.open) return false;
  state.open = false;
  openWorkspaceDropdownState = null;
  state.wrapper?.classList.remove('is-open');
  state.menu?.classList.remove('is-open');
  state.trigger?.setAttribute('aria-expanded', 'false');
  if (state.menu && state.wrapper && state.menu.parentElement !== state.wrapper) {
    state.menu.removeAttribute('style');
    state.wrapper.appendChild(state.menu);
  }
  if (restoreFocus && state.trigger) state.trigger.focus({ preventScroll: true });
  return true;
}
function closeWorkspaceDropdownAtBoundary(target) {
  const state = openWorkspaceDropdownState;
  if (!state?.open) return false;
  const element = target instanceof Element ? target : null;
  if (element && (state.wrapper?.contains(element) || state.menu?.contains(element))) return false;
  return closeWorkspaceDropdown(false);
}
function workspaceDropdownSelect(state, index) {
  if (!state?.select) return;
  const option = state.select.options[index];
  if (!option || option.disabled) return;
  state.select.selectedIndex = index;
  state.select.dispatchEvent(new Event('change', { bubbles: true }));
  closeWorkspaceDropdown(true);
}
function workspaceDropdownMove(state, direction) {
  if (!state?.open) return false;
  const options = workspaceDropdownVisibleOptions(state);
  if (!options.length) return true;
  const current = state.select.options[state.highlight];
  let index = options.indexOf(current);
  if (index < 0) index = direction > 0 ? -1 : options.length;
  index = Math.max(0, Math.min(options.length - 1, index + direction));
  state.highlight = [...state.select.options].indexOf(options[index]);
  state.menu.querySelectorAll('.workspace-dropdown-option').forEach(option => {
    option.classList.toggle('highlighted', Number(option.dataset.optionIndex) === state.highlight);
    option.setAttribute('data-highlighted', String(Number(option.dataset.optionIndex) === state.highlight));
  });
  const target = state.menu.querySelector(`[data-option-index="${state.highlight}"]`);
  if (target) focusWorkspaceElement(target);
  return true;
}
function refreshWorkspaceDropdown(select) {
  const state = workspaceDropdownStates.get(select);
  if (!state) return;
  const selected = select.options[select.selectedIndex];
  state.trigger.querySelector('.workspace-dropdown-label').textContent = workspaceDropdownOptionText(selected) || '请选择';
  state.trigger.disabled = select.disabled;
  state.trigger.setAttribute('aria-disabled', String(select.disabled));
  state.menu.replaceChildren();
  [...select.options].forEach((option, index) => {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'workspace-dropdown-option';
    item.dataset.optionIndex = String(index);
    item.setAttribute('role', 'option');
    item.setAttribute('aria-selected', String(index === select.selectedIndex));
    item.disabled = option.disabled;
    item.innerHTML = `<span>${escapeHtml(workspaceDropdownOptionText(option))}</span>${index === select.selectedIndex ? '<b aria-hidden="true">✓</b>' : ''}`;
    item.addEventListener('mouseenter', () => {
      state.highlight = index;
      state.menu.querySelectorAll('.workspace-dropdown-option').forEach(node => node.classList.remove('highlighted'));
      item.classList.add('highlighted');
    });
    item.addEventListener('click', () => workspaceDropdownSelect(state, index));
    item.addEventListener('keydown', event => {
      if (['ArrowUp', 'ArrowDown'].includes(event.key)) {
        event.preventDefault();
        event.stopPropagation();
        workspaceDropdownMove(state, event.key === 'ArrowDown' ? 1 : -1);
      } else if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        event.stopPropagation();
        workspaceDropdownSelect(state, index);
      } else if (event.key === 'Escape' || event.key === 'Esc') {
        event.preventDefault();
        event.stopPropagation();
        closeWorkspaceDropdown(true);
      }
    });
    state.menu.appendChild(item);
  });
  if (state.open) {
    state.highlight = select.selectedIndex >= 0 ? select.selectedIndex : 0;
    workspaceDropdownSetPosition(state);
  }
}
function openWorkspaceDropdown(state, fromGamepad = false, initialDirection = 0) {
  if (!state || state.select.disabled) return false;
  if (openWorkspaceDropdownState && openWorkspaceDropdownState !== state) closeWorkspaceDropdown(false);
  if (state.open) return true;
  state.open = true;
  openWorkspaceDropdownState = state;
  state.highlight = state.select.selectedIndex >= 0 ? state.select.selectedIndex : 0;
  state.wrapper.classList.add('is-open');
  state.menu.classList.add('is-open');
  state.trigger.setAttribute('aria-expanded', 'true');
  document.body.appendChild(state.menu);
  workspaceDropdownSetPosition(state);
  requestAnimationFrame(() => {
    if (!state.open) return;
    if (initialDirection) workspaceDropdownMove(state, initialDirection);
    else if (fromGamepad) state.menu.querySelector(`[data-option-index="${state.highlight}"]`)?.focus({ preventScroll: true });
  });
  return true;
}
function workspaceDropdownKey(event) {
  const state = openWorkspaceDropdownState;
  const eventTarget = event.target instanceof Element ? event.target : document.activeElement;
  if (state?.open && state.menu?.contains(eventTarget)) {
    if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
      event.preventDefault();
      event.stopPropagation();
      return workspaceDropdownMove(state, event.key === 'ArrowDown' ? 1 : -1);
    }
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      event.stopPropagation();
      workspaceDropdownSelect(state, state.highlight);
      return true;
    }
    return false;
  }
  const trigger = eventTarget?.closest?.('.workspace-dropdown-trigger') || null;
  const triggerState = trigger ? workspaceDropdownStates.get(trigger.closest('.workspace-dropdown')?.querySelector('select')) : null;
  if (!triggerState) return false;
  // Direction keys only move the focused control. Opening is an explicit
  // accept action, matching the YeManCC main-program interaction contract.
  if (event.key === 'Enter' || event.key === ' ') {
    event.preventDefault();
    event.stopPropagation();
    return openWorkspaceDropdown(triggerState, true);
  }
  return false;
}
function upgradeWorkspaceSelect(select) {
  if (!select) return null;
  const existing = workspaceDropdownStates.get(select);
  if (existing) { refreshWorkspaceDropdown(select); return existing; }
  const wrapper = document.createElement('div');
  wrapper.className = 'workspace-dropdown';
  wrapper.dataset.selectId = select.id || '';
  if (select.dataset.editRow) wrapper.dataset.editRow = select.dataset.editRow;
  const trigger = document.createElement('button');
  trigger.type = 'button';
  trigger.className = 'workspace-dropdown-trigger';
  trigger.setAttribute('aria-haspopup', 'listbox');
  trigger.setAttribute('aria-expanded', 'false');
  trigger.setAttribute('aria-label', select.getAttribute('aria-label') || '选择项目');
  trigger.innerHTML = '<span class="workspace-dropdown-label"></span><span class="workspace-dropdown-caret" aria-hidden="true">⌄</span>';
  const menu = document.createElement('div');
  menu.className = 'workspace-dropdown-menu';
  menu.dataset.selectId = select.id || '';
  menu.setAttribute('role', 'listbox');
  const state = { select, wrapper, trigger, menu, open: false, highlight: 0 };
  workspaceDropdownStates.set(select, state);
  select.classList.add('workspace-native-select');
  select.tabIndex = -1;
  select.setAttribute('aria-hidden', 'true');
  select.parentElement?.insertBefore(wrapper, select);
  wrapper.append(select, trigger, menu);
  trigger.addEventListener('click', () => state.open ? closeWorkspaceDropdown(true) : openWorkspaceDropdown(state));
  trigger.addEventListener('keydown', event => workspaceDropdownKey(event));
  select.addEventListener('change', () => refreshWorkspaceDropdown(select));
  refreshWorkspaceDropdown(select);
  return state;
}
function workspaceDropdownForSelect(select) { return select ? workspaceDropdownStates.get(select) || upgradeWorkspaceSelect(select) : null; }
function workspaceDropdownTriggerForSelect(select) { return workspaceDropdownForSelect(select)?.trigger || null; }
function upgradeEditDropdowns() {
  document.querySelectorAll('#edit-modal select, #identity-search-modal select').forEach(upgradeWorkspaceSelect);
}
function updateWorkspaceFocusMarker(target) {
  document.querySelectorAll('.workspace-focus-visible').forEach(item => item.classList.remove('workspace-focus-visible'));
  const element = target instanceof Element ? target : null;
  if (!element || element.matches('.workspace-native-select')) return;
  const inWorkspaceModal = element.closest('.modal');
  const isDropdownItem = element.matches('.workspace-dropdown-trigger, .workspace-dropdown-option');
  if (inWorkspaceModal || isDropdownItem) element.classList.add('workspace-focus-visible');
}
function libraryFocusDescriptor(target) {
  const element = target instanceof Element ? target : null;
  if (!element || !element.closest('#library-view')) return null;
  if (element.matches('.game-card')) return { type: 'card', directory: String(element.dataset.directory || '').toLocaleLowerCase() };
  if (element.matches('.page-tab')) return { type: 'tab', tab: element.dataset.tab || '' };
  if (element.id) return { type: 'id', id: element.id };
  return null;
}
function rememberLibraryFocus(target) {
  const descriptor = libraryFocusDescriptor(target);
  if (descriptor) libraryFocusMemory = descriptor;
}
function resolveLibraryFocusDescriptor(descriptor) {
  if (!descriptor) return null;
  if (descriptor.type === 'card') return libraryCards().find(card => String(card.dataset.directory || '').toLocaleLowerCase() === descriptor.directory) || null;
  if (descriptor.type === 'tab') return [...document.querySelectorAll('#library-view .page-tab')].find(tab => tab.dataset.tab === descriptor.tab) || null;
  if (descriptor.type === 'id') { const element = document.getElementById(descriptor.id); return element?.closest('#library-view') ? element : null; }
  return null;
}
document.addEventListener('focusin', event => { updateWorkspaceFocusMarker(event.target); rememberLibraryFocus(event.target); }, true);
document.addEventListener('pointerdown', event => closeWorkspaceDropdownAtBoundary(event.target), true);
window.addEventListener('resize', () => { if (openWorkspaceDropdownState) workspaceDropdownSetPosition(openWorkspaceDropdownState); });
window.addEventListener('scroll', () => { if (openWorkspaceDropdownState) workspaceDropdownSetPosition(openWorkspaceDropdownState); }, true);

function invoke(command, arguments_ = {}) {
  const id = requestId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    try {
      window.chrome.webview.postMessage({ id, command, arguments: arguments_ });
    } catch (error) {
      pending.delete(id);
      reportUiError('post-message', error, { command });
      reject(error);
    }
  });
}
function reportUiError(kind, error, extra = {}) {
  try {
    const value = error instanceof Error ? error : new Error(String(error ?? '未知错误'));
    window.chrome?.webview?.postMessage({ id: 0, command: 'uiError', arguments: {
      kind: String(kind || 'ui-error').slice(0, 80),
      message: String(value.message || value).slice(0, 2000),
      stack: String(value.stack || '').slice(0, 6000),
      ...extra
    }});
  } catch (_) {}
}
window.addEventListener('error', event => reportUiError('window-error', event.error || event.message, { source: event.filename || '', line: event.lineno || 0, column: event.colno || 0 }));
window.addEventListener('unhandledrejection', event => reportUiError('unhandled-rejection', event.reason));
function setBusy(visible, title = '正在处理', detail = '弱网环境下可能需要几分钟，请勿关闭窗口。') { busy.classList.toggle('hidden', !visible); $('#busy-title').textContent = title; $('#busy-detail').textContent = detail; const cancel = $('#busy-cancel'); if (cancel) { cancel.classList.toggle('hidden', !visible); cancel.disabled = cancelInFlight; cancel.textContent = '取消'; } }
async function cancelActiveTask() { if (identitySearchInFlight) return cancelIdentitySearch(); if (cancelInFlight || busy.classList.contains('hidden')) return; cancelInFlight = true; setBusy(true, '正在取消当前任务', '正在终止隔离 worker，不会删除游戏文件或真实 Steam 数据。'); try { await invoke('cancelTask'); } catch (error) { showError(error, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); cancelInFlight = false; setBusy(true, '正在处理', '取消请求失败，请稍后重试。'); } }
function startIdentitySearchLoading() { identitySearchJobId = ''; const loading = $('#identity-search-loading'); const dots = $('#identity-search-loading-dots'); if (!loading || !dots) return; loading.classList.remove('hidden'); if (identitySearchEllipsisTimer) clearInterval(identitySearchEllipsisTimer); identitySearchEllipsisStep = 0; const paint = () => { dots.textContent = '。'.repeat((identitySearchEllipsisStep++ % 3) + 1); }; paint(); identitySearchEllipsisTimer = window.setInterval(paint, 450); }
function stopIdentitySearchLoading() { if (identitySearchEllipsisTimer) { clearInterval(identitySearchEllipsisTimer); identitySearchEllipsisTimer = 0; } $('#identity-search-loading')?.classList.add('hidden'); }
async function cancelIdentitySearch() { identitySearchManualCandidate = null; if (!identitySearchInFlight) { $('#identity-search-modal').classList.add('hidden'); if (selectedActionGame) openEdit(selectedActionGame); return; } identitySearchCancelled = true; identitySearchToken++; identitySearchInFlight = false; stopIdentitySearchLoading(); $('#identity-search-modal').classList.add('hidden'); const executable = selectedActionGame?.primaryExecutable || identitySearchExecutable; identitySearchJobId = ''; if (selectedActionGame) openEdit(selectedActionGame); invoke('cancelIdentitySearch', { executable }).catch(() => {}); }
function cancelEditorBackgroundTasks() {
  if (identitySearchInFlight) {
    const executable = selectedActionGame?.primaryExecutable || identitySearchExecutable;
    identitySearchCancelled = true;
    identitySearchToken++;
    identitySearchInFlight = false;
    identitySearchJobId = '';
    stopIdentitySearchLoading();
    invoke('cancelIdentitySearch', { executable }).catch(() => {});
  }
  if (artworkTaskInFlight || artworkPrefetchPendingCount > 0) {
    artworkTaskToken++;
    artworkTaskInFlight = false;
    downloadingCandidateUrl = '';
    invoke('cancelArtwork').catch(() => {});
  }
}
function showNextNotice() {
  if (noticeActive || !noticeQueue.length) return;
  const item = noticeQueue.shift();
  noticeActive = true;
  const sequence = ++noticeSequence;
  toastNotice.textContent = item.message;
  toastNotice.classList.remove('hidden', 'error');
  if (item.error) toastNotice.classList.add('error');
  noticeTimer = window.setTimeout(() => {
    if (sequence !== noticeSequence) return;
    toastNotice.classList.add('hidden');
    toastNotice.classList.remove('error');
    noticeActive = false;
    noticeTimer = 0;
    showNextNotice();
  }, NOTICE_DURATION_MS);
}
function showNotice(message, error = false, target = notice) {
  const text = String(message ?? '').trim();
  if (!text) return;
  const duplicate = (noticeActive && toastNotice.textContent === text) || noticeQueue.some(item => item.message === text && item.error === error);
  if (duplicate) return;
  noticeQueue.push({ message: text, error: Boolean(error), target });
  showNextNotice();
}
function clearNotice(target = notice) {
  if (target) target.classList.add('hidden');
  noticeQueue.length = 0;
  noticeSequence++;
  if (noticeTimer) window.clearTimeout(noticeTimer);
  noticeTimer = 0;
  noticeActive = false;
  toastNotice.classList.add('hidden');
  toastNotice.classList.remove('error');
}
function showError(error, target = notice) { showNotice(error?.message || String(error), true, target); }
function askConfirm(title, message, confirmLabel = '确认') { return new Promise(resolve => { confirmResolver = resolve; $('#confirm-title').textContent = title; $('#confirm-message').textContent = message; $('#confirm-confirm').textContent = confirmLabel; $('#confirm-modal').classList.remove('hidden'); }); }
function closeConfirm(result) { $('#confirm-modal').classList.add('hidden'); const resolve = confirmResolver; confirmResolver = null; if (resolve) resolve(result); }
function steamAccounts() { return snapshot.steamAccounts?.accounts || []; }
function ensureSteamTarget() {
  const accounts = steamAccounts();
  const current = accounts.find(item => item.accountId === selectedSteamTarget.accountId && (!selectedSteamTarget.steamRoot || item.steamRoot === selectedSteamTarget.steamRoot));
  if (current) selectedSteamTarget = { accountId: current.accountId, steamRoot: current.steamRoot || selectedSteamTarget.steamRoot };
  else if (accounts.length === 1) selectedSteamTarget = { accountId: accounts[0].accountId, steamRoot: accounts[0].steamRoot || '' };
  else if (accounts.length > 1) selectedSteamTarget = { accountId: '', steamRoot: '' };
}
function defaultSelectedDirectories() { return mergedGames().filter(game => isSteamReady(game) && category(game) === 'not-in-steam').map(keyFor); }
function defaultSelectedSteamDirectories() { return mergedGames().filter(game => category(game) === 'in-steam').map(keyFor); }
function selectedCommitDirectories() {
  if (activeTab === 'not-in-steam') return selectedGameDirectories === null ? defaultSelectedDirectories() : [...selectedGameDirectories];
  if (activeTab === 'unclassified' || activeTab === 'non-game') return [...(selectedCategoryDirectories[activeTab] || [])];
  return [];
}
function selectedTargetArguments() { return { accountId: '', steamRoot: '', selectionMode: 'all-steam-accounts', selectedGameDirectories: selectedCommitDirectories() }; }

const steamReadyStatuses = new Set(['ready-to-add', 'ready-not-selected']);
const CATEGORY_ORDER = Object.freeze(['not-in-steam', 'in-steam', 'unclassified', 'non-game']);
const CATEGORY_DEFINITIONS = Object.freeze({
  'not-in-steam': Object.freeze({ label: '等待加入', tone: 'good' }),
  'in-steam': Object.freeze({ label: '已加入', tone: 'steam' }),
  unclassified: Object.freeze({ label: '需处理', tone: 'warn' }),
  'non-game': Object.freeze({ label: '不加入', tone: 'muted-badge' })
});
function isSteamReady(game) { return steamReadyStatuses.has(game.steam?.status); }
function isNonGame(game) { return game.contentType === 'non-game' || game.status === 'unrecognized-tool' || game.unrecognizedTool; }
function keyFor(game) { return normalizedUiPath(game?.gameDirectory || ''); }
function planMap() { const result = new Map(); for (const item of snapshot.steamPlan?.items || []) { if (item.primaryExecutable) result.set(item.primaryExecutable.toLocaleLowerCase(), item); else if (item.gameDirectory) result.set(`dir:${item.gameDirectory.toLocaleLowerCase()}`, item); } return result; }
function normalizedUiPath(value) { return String(value || '').replaceAll('/', '\\').replace(/[\\]+$/, '').toLocaleLowerCase(); }
function manualOverride(executable) { const key = normalizedUiPath(executable); if (!key) return {}; const items = snapshot.manualOverrides?.items || {}; if (items[key]) return items[key]; const found = Object.entries(items).find(([stored]) => normalizedUiPath(stored) === key); return found?.[1] || {}; }
function resolvedIdentity(executable) { const key = normalizedUiPath(executable); if (!key) return {}; const items = snapshot.resolvedIdentities || {}; if (items[key]) return items[key]; const found = Object.entries(items).find(([stored, value]) => normalizedUiPath(stored) === key || normalizedUiPath(value?.executable) === key); return found?.[1] || {}; }
function executableLikeName(value) { return /\\.(exe|dll|bin|bat|cmd)$/i.test(String(value || '').trim()) || /^(setup|unins|launcher|start|game)\\d*$/i.test(String(value || '').trim()); }
function gameDisplayName(game) { const overrideName = game.override?.name; if (overrideName) return overrideName; const formal = game.resolvedIdentity?.formalName || game.steam?.formalName; if (formal && !executableLikeName(formal)) return formal; return game.primaryProductName || game.primaryFileDescription || game.steam?.suggestedName || game.directoryName || formal || '未命名项目'; }
function manualBucket(game) {
  const buckets = snapshot.config?.manualBuckets || {};
  const directoryKey = keyFor(game);
  const executableKey = normalizedUiPath(game.primaryExecutable);
  const direct = buckets[directoryKey];
  if (direct?.bucket) return direct.bucket;
  const executable = buckets[executableKey];
  if (executable?.bucket) return executable.bucket;
  const found = Object.entries(buckets).find(([stored, value]) =>
    (normalizedUiPath(stored) === directoryKey || normalizedUiPath(stored) === executableKey) && value?.bucket);
  return found?.[1]?.bucket || 'auto';
}
function mergedGames() {
  const plans = planMap();
  return (snapshot.library?.games || []).map(game => {
    const key = game.primaryExecutable?.toLocaleLowerCase();
    const steam = plans.get(key) || plans.get(`dir:${keyFor(game)}`) || {};
    const override = manualOverride(game.primaryExecutable);
    const resolved = resolvedIdentity(game.primaryExecutable);
    const idCleared = override.idCleared === true;
    // Do not surface local candidates, IGDB-only IDs, or pending manual IDs
    // as a canonical Steam identity in the library list.  The editor still
    // shows the user's draft value through formalSteamAppId().
    const steamVerified = steam.steamVerificationStatus === 'steam-verified' || isSteamReady({ steam });
    const canonicalSteamId = idCleared || !steamVerified ? '' :
      (steam.steamStoreAppId || steam.storefrontAppId || steam.appId || '');
    return { ...game, steam: { ...steam,
      ...(idCleared ? { steamStoreAppId: '', storefrontAppId: '', appId: '', canonicalAppId: '', localAppId: '', requestedAppId: '' } : {}),
      ...(canonicalSteamId ? { steamStoreAppId: canonicalSteamId, storefrontAppId: steam.storefrontAppId || canonicalSteamId, formalName: steam.formalName || resolved.formalName || '' } : {})
    }, override, resolvedIdentity: resolved };
  }).sort((a, b) => String(a.directoryName || '').localeCompare(String(b.directoryName || ''), 'zh-Hans'));
}
function category(game) {
  if (game.steam?.status === 'already-in-steam' || game.steam?.status === 'added-to-steam') return 'in-steam';
  const bucket = manualBucket(game);
  if (bucket !== 'auto') return bucket;
  if (isNonGame(game)) return 'non-game';
  if (isSteamReady(game)) return 'not-in-steam';
  return 'unclassified';
}
function categoryDefinition(gameOrCategory) {
  const code = typeof gameOrCategory === 'string' ? gameOrCategory : category(gameOrCategory);
  return CATEGORY_DEFINITIONS[code] || CATEGORY_DEFINITIONS.unclassified;
}
function categoryCounts(games = mergedGames()) {
  const counts = Object.fromEntries(CATEGORY_ORDER.map(code => [code, 0]));
  for (const game of games) {
    const code = category(game);
    if (Object.prototype.hasOwnProperty.call(counts, code)) counts[code] += 1;
  }
  return counts;
}
function selectedReadyCount() { const selected = new Set(selectedCommitDirectories()); return mergedGames().filter(game => isSteamReady(game) && category(game) === activeTab && selected.has(keyFor(game))).length; }
function selectedSteamCount() { return mergedGames().filter(game => category(game) === 'in-steam' && (selectedSteamDirectories === null || selectedSteamDirectories.has(keyFor(game)))).length; }
function formatCount(value) { const count = Number.isFinite(Number(value)) ? Math.max(0, Number(value)) : 0; return `[${count}个]`; }
function formatSelectedCount(value) { const count = Number.isFinite(Number(value)) ? Math.max(0, Number(value)) : 0; return `[已选${count}个]`; }
function pageSelectable(game, page = activeTab) { const itemCategory = category(game); if (itemCategory !== page) return false; return page !== 'not-in-steam' || isSteamReady(game); }
function pageSelectionKeys(page = activeTab) {
  if (page === 'not-in-steam') return new Set(selectedGameDirectories === null ? defaultSelectedDirectories() : selectedGameDirectories);
  if (page === 'in-steam') return new Set(selectedSteamDirectories === null ? defaultSelectedSteamDirectories() : selectedSteamDirectories);
  return new Set(selectedCategoryDirectories[page] || []);
}
function defaultPageKeys(page = activeTab) { return mergedGames().filter(game => pageSelectable(game, page)).map(keyFor); }
function storePageSelection(page, selected, all) {
  if (page === 'not-in-steam') selectedGameDirectories = selected.size === all.length ? null : selected;
  else if (page === 'in-steam') selectedSteamDirectories = selected.size === all.length ? null : selected;
  else selectedCategoryDirectories[page] = selected;
}
function selectedPageCount(page = activeTab) { const selected = pageSelectionKeys(page); return mergedGames().filter(game => pageSelectable(game, page) && selected.has(keyFor(game))).length; }
function isPageSelected(game) { return pageSelectable(game) && pageSelectionKeys(activeTab).has(keyFor(game)); }

function statusLabel(game) {
  const definition = categoryDefinition(game);
  return [definition.label, definition.tone];
}
function escapeHtml(value) { return String(value ?? '').replace(/[&<>'"]/g, char => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', "'":'&#39;', '"':'&quot;' })[char]); }
function initials(name) { const parts = String(name || '?').trim().split(/[\s\-_]+/).filter(Boolean); return (parts.length > 1 ? parts.slice(0, 2).map(x => x[0]).join('') : parts[0]?.slice(0, 2) || '?').toUpperCase(); }
function manualArtworkPriority(game) { const override = game?.override || {}; const policy = override.artworkProtection || {}; return Boolean(override.cover || override.wallpaper || policy.cover === 'manual' || policy.cover === 'deleted' || policy.wallpaper === 'manual' || policy.wallpaper === 'deleted'); }
function artworkMarkup(game, name) { const manual = game.override?.cover || game.override?.artwork?.cover; const preview = manual?.url ? manual : game.steam?.artworkPreview?.tall; const previewUrl = artworkPreviewUrl(preview); return previewUrl ? `<img src="${escapeHtml(previewUrl)}" alt="${escapeHtml(name)} 封面" loading="lazy">` : escapeHtml(initials(name)); }
function scrapeStatusFor(game) { const key = (game.primaryExecutable || '').toLocaleLowerCase(); return snapshot.scrape?.items?.[key] || null; }
function scrapeBadgeMarkup(game) { const item = scrapeStatusFor(game); if (!item) return ''; if (item.status === 'loading') return '<span class="scrape-inline loading"><i></i>正在联网补齐</span>'; if (item.status === 'queued') return '<span class="scrape-inline queued">排队中</span>'; if (item.status === 'failed') return '<span class="scrape-inline failed">网络失败，可稍后重试</span>'; return ''; }

function renderEntry() {
  const games = mergedGames();
  const counts = categoryCounts(games);
  $('#entry-summary').innerHTML = CATEGORY_ORDER.map(code => {
    const definition = categoryDefinition(code);
    return '<div data-category="' + code + '"><strong>' + formatCount(counts[code]) + '</strong><span>' + definition.label + '</span></div>';
  }).join('');
  $('#entry-summary')?.setAttribute('aria-label', '游戏库分类统计：等待加入、已加入、需处理、不加入');
  if ($('#entry-view') && !$('#entry-view').classList.contains('hidden') && !document.querySelector('.modal:not(.hidden)')) {
    requestAnimationFrame(() => { if (!document.querySelector('.modal:not(.hidden)') && !$('#entry-view').contains(document.activeElement)) $('#open-library-button')?.focus(); });
  }
}
function closeLibraryMenuPanels(except = '') {
  for (const id of ['directory-settings-panel', 'manual-game-panel']) {
    if (id === except) continue;
    const panel = $('#' + id);
    if (panel) panel.classList.add('hidden');
  }
  $('#directory-settings-toggle')?.setAttribute('aria-expanded', String(!$('#directory-settings-panel')?.classList.contains('hidden')));
  $('#manual-game-settings-toggle')?.setAttribute('aria-expanded', String(!$('#manual-game-panel')?.classList.contains('hidden')));
}
function openLibraryMenuPanel(target) {
  const localPanel = target?.closest?.('.menu-popover-panel:not(.hidden)');
  return localPanel || $('#library-view .menu-popover-panel:not(.hidden)');
}
function closeLibraryMenuAtBoundary(target) {
  if ($('#library-view')?.classList.contains('hidden')) return;
  const openPanel = openLibraryMenuPanel(document);
  if (!openPanel) return;
  const targetElement = target instanceof Element ? target : null;
  const targetOwner = targetElement?.closest?.('.menu-popover');
  const openOwner = openPanel.closest('.menu-popover');
  // Keep the current menu interactive. Any pointer/click/focus that lands on
  // another toolbar item or on the page is a boundary action and must dismiss
  // the menu immediately, including synthetic clicks from the gamepad router.
  if (targetOwner !== openOwner) {
    const nextPanel = targetOwner?.querySelector?.('.menu-popover-panel');
    closeLibraryMenuPanels(nextPanel?.id || '');
  }
}
document.addEventListener('pointerdown', event => closeLibraryMenuAtBoundary(event.target), true);
document.addEventListener('click', event => closeLibraryMenuAtBoundary(event.target), true);
document.addEventListener('focusin', event => closeLibraryMenuAtBoundary(event.target), true);
function renderRoots() {
  const rawRoots = snapshot.config?.roots ?? snapshot.config?.libraryRoots ?? snapshot.library?.roots ?? [];
  const roots = Array.isArray(rawRoots) ? rawRoots : (rawRoots ? [rawRoots] : []);
  $('#root-count').innerHTML = `<b class="count-value">${formatCount(roots.length)}</b>`;
  const directoryPanel = $('#directory-settings-panel');
  const directoryToggle = $('#directory-settings-toggle');
  if (directoryPanel && directoryToggle) directoryToggle.setAttribute('aria-expanded', String(!directoryPanel.classList.contains('hidden')));
  const list = $('#root-list'); list.replaceChildren();
  for (const root of roots) { const row = document.createElement('div'); row.className = 'root-row'; row.innerHTML = `<code>${escapeHtml(root)}</code><button class="mini-button danger" ${roots.length <= 1 ? 'disabled' : ''}>移除</button>`; row.querySelector('button').addEventListener('click', async () => { if (!await askConfirm('移除扫描目录', `停止扫描此目录？\n${root}\n\n不会删除游戏文件。`, '移除')) return; await runBusy('removeRoot', '正在移除扫描目录', root, { path: root }); }); list.appendChild(row); }
  renderManualGameDirectories();
}
function renderManualGameDirectories() {
  const list = $('#manual-game-list');
  if (!list) return;
  list.replaceChildren();
  const raw = snapshot.config?.manualGameDirectories;
  const entries = Array.isArray(raw) ? raw : [];
  const directories = entries.map(item => typeof item === 'string' ? item : (item?.path || item?.gameDirectory || '')).filter(Boolean);
  $('#manual-count').innerHTML = `<b class="count-value">${formatCount(directories.length)}</b>`;
  if (!directories.length) {
    const empty = document.createElement('p'); empty.className = 'manual-game-empty'; empty.textContent = '暂无单独添加的游戏'; list.appendChild(empty); return;
  }
  for (const directory of directories) {
    const row = document.createElement('div'); row.className = 'root-row manual-game-row';
    row.innerHTML = `<code title="${escapeHtml(directory)}">${escapeHtml(directory)}</code><button class="mini-button danger" type="button">删除</button>`;
    row.querySelector('button').addEventListener('click', async () => {
      if (!await askConfirm('删除单独添加的游戏', `仅从自定义游戏库移除：\n${directory}\n\n不会删除游戏安装文件。`, '删除')) return;
      await runBusy('removeGame', '正在删除单独添加的游戏', directory, { gameDirectory: directory }, '单独添加的游戏已移除。');
    });
    list.appendChild(row);
  }
}
function renderSteamTarget() { const accounts = steamAccounts(); $("#steam-target-status").textContent = accounts.length ? `已发现 ${accounts.length} 个 Steam 大屏用户，将自动同步全部用户` : "未发现 Steam 用户，稍后可重试扫描"; }
function renderArtworkRefreshControl() {
  const button = $('#refresh-artwork-button');
  if (!button) return;
  const state = manualRefreshProgress;
  let label = '手动刷新';
  if (state.phase === 'starting') label = '手动刷新（准备中）';
  else if (state.phase === 'queued') label = '手动刷新（排队中）';
  else if (state.phase === 'running') label = state.total > 0
    ? '手动刷新（' + state.current + '/' + state.total + '）'
    : '手动刷新（准备中）';
  else if (state.phase === 'completed') label = '手动刷新（已完成）';
  else if (state.phase === 'paused') label = '手动刷新（已暂停）';
  else if (state.phase === 'failed') label = '手动刷新（失败）';
  button.classList.toggle('paused', state.phase === 'queued' || state.phase === 'starting' || state.phase === 'paused');
  button.disabled = state.phase === 'starting' || state.phase === 'queued' || state.phase === 'running';
  button.setAttribute('aria-pressed', state.phase === 'running' ? 'true' : 'false');
  button.setAttribute('aria-label', label);
  $('#refresh-artwork-label').textContent = label;
}

function toggleGameSelection(game, pointerActivated = false) {
  const page = activeTab;
  const selectable = pageSelectable(game, page);
  if (!selectable) return;
  if (pointerActivated) currentCardDirectory = keyFor(game);
  const all = defaultPageKeys(page);
  const selected = pageSelectionKeys(page);
  const key = keyFor(game);
  if (selected.has(key)) selected.delete(key); else selected.add(key);
  storePageSelection(page, selected, all);
  renderLibrary();
}

function createCard(game) {
  const card = document.createElement("article"); card.className = "game-card"; card.tabIndex = 0; card.setAttribute("role", "button"); card.dataset.directory = game.gameDirectory || "";
  const name = gameDisplayName(game); const checked = isPageSelected(game); const canSelect = pageSelectable(game, activeTab);
  card.classList.toggle("selection-enabled", canSelect); card.classList.toggle("selected-for-steam", checked); card.classList.toggle("unselected-for-steam", canSelect && !checked);
  card.classList.toggle("current-card", currentCardDirectory === keyFor(game));
  card.setAttribute("aria-pressed", canSelect ? (checked ? "true" : "false") : "false"); card.setAttribute("aria-label", `${name}，${canSelect ? (checked ? "已选中，点击或按 A 取消选择；按 X 编辑" : "未选中，点击或按 A 选择；按 X 编辑") : "按 X 编辑"}`); card.title = "双击编辑";
  card.innerHTML = `<span class="card-art-glow" aria-hidden="true"></span><div class="game-main"><div class="cover-placeholder">${artworkMarkup(game, name)}</div><h2 class="game-card-name">${escapeHtml(name)}</h2></div>`;
  const cardArtwork = game.override?.cover || game.override?.artwork?.cover || game.steam?.artworkPreview?.tall;
  const cardArtworkUrl = artworkPreviewUrl(cardArtwork);
  const glow = card.querySelector(".card-art-glow");
  if (glow && cardArtworkUrl) glow.style.backgroundImage = `url("${cardArtworkUrl.replaceAll('"', '%22')}")`;
  const image = card.querySelector("img"); if (image) image.addEventListener("error", () => { image.parentElement.textContent = initials(name); }, { once: true });
  card.addEventListener("focus", () => {
    currentCardDirectory = keyFor(game);
    libraryCards().forEach(item => item.classList.remove("current-card"));
    card.classList.add("current-card");
  });
  card.addEventListener("blur", () => {
    if (currentCardDirectory === keyFor(game) && !card.matches(":hover")) {
      currentCardDirectory = '';
      card.classList.remove("current-card");
    }
  });
  card.addEventListener("pointerenter", () => {
    currentCardDirectory = keyFor(game);
    libraryCards().forEach(item => item.classList.remove("current-card"));
    card.classList.add("current-card");
  });
  card.addEventListener("pointerdown", event => {
    if (event.isPrimary === false) return;
    currentCardDirectory = keyFor(game);
    libraryCards().forEach(item => item.classList.remove("current-card"));
    card.classList.add("current-card");
    // WebView does not consistently focus an article with tabindex on click.
    // Preserve the clicked card as the controller origin instead of leaving
    // focus on body, where the next ArrowUp used to scroll the document.
    focusWorkspaceElement(card, false);
  });
  card.addEventListener("pointerleave", () => {
    if (currentCardDirectory === keyFor(game) && document.activeElement !== card) {
      currentCardDirectory = '';
      card.classList.remove("current-card");
    }
  });
  card.addEventListener("click", event => { if (event.target.closest("button,input,select")) return; if (canSelect) toggleGameSelection(game, event.isTrusted && card.matches(":hover")); else openEdit(game); });
  card.addEventListener("dblclick", event => { if (event.target === card || event.target.closest(".game-main")) openEdit(game); });
  card.addEventListener("keydown", event => {
    if (event.target !== card) return;
    if (event.key.toLowerCase() === "x") {
      event.preventDefault();
      event.stopPropagation();
      openEdit(game);
    } else if (event.key === "Enter" || event.key === " ") {
      // The card owns activation. Stop bubbling so the window-level
      // navigation handler cannot click the same card a second time.
      event.preventDefault();
      event.stopPropagation();
      if (canSelect) toggleGameSelection(game); else openEdit(game);
    }
  });
  return card;
}
function libraryCards() { return [...library.querySelectorAll('.game-card')]; }
function navigableLibraryCards() {
  return libraryCards().filter(item => !item.classList.contains('hidden') &&
    item.getClientRects().length > 0 && getComputedStyle(item).visibility !== 'hidden');
}
function focusLibraryCard(card = navigableLibraryCards()[0]) { return focusWorkspaceElement(card); }
function moveCardFocus(card, key) {
  const rows = [];
  const visibleCards = navigableLibraryCards();
  if (!visibleCards.length) return false;
  const rowTolerance = Math.max(10, Math.min(40, (visibleCards[0].getBoundingClientRect().height || 0) * 0.2));
  for (const item of visibleCards) {
    const rect = item.getBoundingClientRect();
    let row = rows.find(candidate => Math.abs(candidate.top - rect.top) <= rowTolerance);
    if (!row) { row = { top: rect.top, items: [] }; rows.push(row); }
    row.items.push({ item, left: rect.left });
  }
  rows.sort((a, b) => a.top - b.top);
  rows.forEach(row => row.items.sort((a, b) => a.left - b.left));
  const rowIndex = rows.findIndex(row => row.items.some(entry => entry.item === card));
  if (rowIndex < 0) return false;
  const currentRow = rows[rowIndex];
  const currentIndex = currentRow.items.findIndex(entry => entry.item === card);
  if (key === 'ArrowLeft' || key === 'ArrowRight') {
    const nextIndex = currentIndex + (key === 'ArrowLeft' ? -1 : 1);
    if (nextIndex < 0 || nextIndex >= currentRow.items.length) return true;
    return focusLibraryCard(currentRow.items[nextIndex].item);
  }
  if (key !== 'ArrowUp' && key !== 'ArrowDown') return false;
  const nextRowIndex = rowIndex + (key === 'ArrowUp' ? -1 : 1);
  if (nextRowIndex < 0) return false;
  if (nextRowIndex >= rows.length) return true;
  const currentLeft = currentRow.items[currentIndex].left;
  const target = rows[nextRowIndex].items.reduce((best, entry) => {
    if (!best) return entry;
    return Math.abs(entry.left - currentLeft) < Math.abs(best.left - currentLeft) ? entry : best;
  }, null);
  return target ? focusLibraryCard(target.item) : true;
}
function activeLibraryCard() { const active = document.activeElement; return active?.classList?.contains('game-card') ? active : null; }
function isVisuallyPresent(item) {
  if (!item || item.disabled || item.closest('.hidden')) return false;
  const style = getComputedStyle(item);
  return style.visibility !== 'hidden' && item.getClientRects().length > 0;
}
function libraryPageTabs() { return [...document.querySelectorAll('#library-view:not(.hidden) .page-tab:not(.hidden):not(:disabled)')].filter(isVisuallyPresent); }
function libraryToolbarItems() { return [...document.querySelectorAll('#library-view:not(.hidden) #directory-settings-toggle, #library-view:not(.hidden) #manual-game-settings-toggle, #library-view:not(.hidden) .steam-bar .button:not(.hidden):not(:disabled)')].filter(isVisuallyPresent); }
function libraryPopupItems() { return [...document.querySelectorAll('#library-view:not(.hidden) .menu-popover-panel:not(.hidden) button:not(:disabled), #library-view:not(.hidden) .menu-popover-panel:not(.hidden) [tabindex]:not([tabindex="-1"])')].filter(isVisuallyPresent); }
function libraryHeaderItems() { return [...document.querySelectorAll('#library-view:not(.hidden) #back-entry-button')].filter(isVisuallyPresent); }
function libraryFocusItems() { return [...libraryHeaderItems(), ...libraryPageTabs(), ...libraryToolbarItems(), ...libraryPopupItems(), ...libraryCards().filter(item => !item.classList.contains('hidden'))]; }
function focusLibraryItem(item) { return focusWorkspaceElement(item); }
function moveFocusWithinRow(items, active, key) {
  if (!items.length) return false;
  let index = items.indexOf(active);
  if (index < 0) index = key === 'ArrowLeft' || key === 'ArrowUp' ? items.length - 1 : 0;
  const nextIndex = index + (key === 'ArrowLeft' || key === 'ArrowUp' ? -1 : 1);
  if (nextIndex < 0 || nextIndex >= items.length) return true;
  return focusLibraryItem(items[nextIndex]);
}
function libraryFocusZone() {
  const active = document.activeElement;
  const popup = active?.closest?.('.menu-popover-panel:not(.hidden)');
  if (popup) return { name: 'popup', item: active, popup };
  if (active?.classList?.contains('game-card')) return { name: 'cards', item: active };
  if (libraryPageTabs().includes(active)) return { name: 'tabs', item: active };
  if (libraryToolbarItems().includes(active)) return { name: 'toolbar', item: active };
  if (libraryHeaderItems().includes(active)) return { name: 'header', item: active };
  return { name: 'none', item: active };
}
function moveLibraryDirectional(key) {
  const activeCard = activeLibraryCard();
  if (activeCard) {
    currentCardDirectory = activeCard.dataset.directory.toLocaleLowerCase();
    libraryCards().forEach(item => item.classList.toggle("current-card", item === activeCard));
  }
  const zone = libraryFocusZone();
  const tabs = libraryPageTabs();
  const toolbar = libraryToolbarItems();
  const popup = libraryPopupItems();
  const header = libraryHeaderItems();
  const cards = navigableLibraryCards();
  const allItems = visibleFocusables($('#library-view')).filter(item =>
    !item.matches('.workspace-native-select') && !item.closest('.workspace-dropdown-menu'));
  if (zone.name === 'popup') {
    if (key === 'ArrowUp') {
      const trigger = zone.item.closest('.menu-popover')?.querySelector('.menu-toggle');
      return focusWorkspaceElement(trigger) || true;
    }
    return moveSpatialFocus(popup, key, zone.item) || true;
  }
  if (zone.name === 'toolbar' && (key === 'ArrowLeft' || key === 'ArrowRight')) {
    // Keep horizontal movement on the actual toolbar row. A page-wide
    // nearest-neighbour search can jump from the refresh button into a card
    // when the toolbar wraps at 720p.
    return moveSpatialFocus(toolbar, key, zone.item) || true;
  }
  if (zone.name === 'toolbar' && key === 'ArrowDown' &&
      zone.item.matches('#directory-settings-toggle, #manual-game-settings-toggle')) {
    const panel = zone.item.closest('.menu-popover')?.querySelector('.menu-popover-panel:not(.hidden)');
    if (panel) return focusWorkspaceElement(libraryPopupItems()[0]) || true;
  }
  // The old zone-specific fallback treated the card grid as a separate
  // linear list. At the first row that left ArrowUp to the browser, and the
  // editor had the same problem in reverse. Use the actual screen geometry
  // for every visible library control so cards, tabs, toolbar and headers form
  // one reachable spatial graph.
  if (zone.name === 'cards' && key === 'ArrowUp' && zone.item) {
    const cardRect = zone.item.getBoundingClientRect();
    const hasCardAbove = cards.some(card => card !== zone.item &&
      card.getBoundingClientRect().top < cardRect.top - Math.max(12, cardRect.height * .2));
    if (!hasCardAbove) {
      const targetToolbar = toolbar.slice().sort((left, right) => {
        const lx = left.getBoundingClientRect().left + left.getBoundingClientRect().width / 2;
        const rx = right.getBoundingClientRect().left + right.getBoundingClientRect().width / 2;
        const cx = cardRect.left + cardRect.width / 2;
        return Math.abs(lx - cx) - Math.abs(rx - cx);
      })[0];
      if (focusWorkspaceElement(targetToolbar)) return true;
    }
  }
  if (zone.name === 'none') {
    const fallback = (key === 'ArrowUp' || key === 'ArrowLeft')
      ? (tabs[0] || header[0] || toolbar[0] || cards[0])
      : (toolbar[0] || tabs[0] || cards[0] || header[0]);
    return focusWorkspaceElement(fallback) || true;
  }
  const moved = moveSpatialFocus(allItems, key, zone.item);
  if (moved) return true;
  if (zone.name === 'cards' && key === 'ArrowUp') {
    return focusWorkspaceElement(toolbar[0] || tabs.find(tab => tab.dataset.tab === activeTab) || header[0]) || true;
  }
  if (!zone.item) {
    return focusWorkspaceElement((key === 'ArrowUp' || key === 'ArrowLeft')
      ? (tabs[0] || header[0]) : (toolbar[0] || cards[0])) || true;
  }
  return true;
}
function ensureLibraryFocus() {
  if ($('#library-view').classList.contains('hidden') || libraryModalOpen()) return true;
  if (libraryFocusZone().name !== 'none') return true;
  const target = resolveLibraryFocusDescriptor(libraryFocusMemory) || libraryCards()[0] || libraryPageTabs()[0] || libraryToolbarItems()[0];
  return target ? focusWorkspaceElement(target, false) : false;
}
function scheduleLibraryFocusRepair() {
  const delays = [0, 32, 96, 220, 480];
  let attempt = 0;
  const repair = () => {
    if (ensureLibraryFocus() || attempt >= delays.length - 1) return;
    attempt += 1;
    window.setTimeout(repair, delays[attempt]);
  };
  window.requestAnimationFrame(repair);
}
function visibleFocusables(root) {
  if (!root) return [];
  return [...root.querySelectorAll('button,input,select,summary,[tabindex]')].filter(item => {
    if (item.disabled || item.getAttribute('aria-hidden') === 'true' || item.closest('.hidden')) return false;
    const style = getComputedStyle(item);
    return style.visibility !== 'hidden' && item.getClientRects().length > 0;
  });
}
function focusWorkspaceElement(item, scroll = true) {
  if (!item) return false;
  // A semantic controller event is not a browser key event, but physical
  // keyboard arrows still are. Focus first without allowing the browser to
  // perform its own document scroll, then reveal only the chosen target.
  item.focus({ preventScroll: true });
  if (scroll) item.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  return true;
}
function focusModalItem(root, preferred = null) {
  const items = visibleFocusables(root);
  const item = preferred && items.includes(preferred) ? preferred : items[0];
  return focusWorkspaceElement(item);
}
function spatialNavigationTarget(items, active, key) {
  if (!Array.isArray(items) || !items.length) return null;
  const current = active && items.includes(active) ? active : null;
  if (!current) {
    return (key === 'ArrowUp' || key === 'ArrowLeft') ? items[items.length - 1] : items[0];
  }
  const source = current.getBoundingClientRect();
  const sourceX = source.left + source.width / 2;
  const sourceY = source.top + source.height / 2;
  const horizontal = key === 'ArrowLeft' || key === 'ArrowRight';
  const positive = key === 'ArrowRight' || key === 'ArrowDown';
  const candidates = items.filter(item => {
    if (item === current) return false;
    const rect = item.getBoundingClientRect();
    const x = rect.left + rect.width / 2;
    const y = rect.top + rect.height / 2;
    if (horizontal) return positive ? x > sourceX + 1 : x < sourceX - 1;
    return positive ? y > sourceY + 1 : y < sourceY - 1;
  });
  if (!candidates.length) return null;
  const sourceRight = source.right;
  const sourceBottom = source.bottom;
  const scored = candidates.map(item => {
    const rect = item.getBoundingClientRect();
    const x = rect.left + rect.width / 2;
    const y = rect.top + rect.height / 2;
    const primary = horizontal
      ? (positive ? Math.max(0, rect.left - sourceRight) : Math.max(0, source.left - rect.right))
      : (positive ? Math.max(0, rect.top - sourceBottom) : Math.max(0, source.top - rect.bottom));
    const orthogonal = horizontal ? Math.abs(y - sourceY) : Math.abs(x - sourceX);
    const sourceSpan = horizontal ? source.height : source.width;
    const targetSpan = horizontal ? rect.height : rect.width;
    const overlap = horizontal
      ? Math.max(0, Math.min(source.bottom, rect.bottom) - Math.max(source.top, rect.top))
      : Math.max(0, Math.min(source.right, rect.right) - Math.max(source.left, rect.left));
    const overlapRatio = overlap / Math.max(1, Math.min(sourceSpan, targetSpan));
    // Prefer elements in the same visual row/column. A small overlap bonus
    // keeps a wide toolbar or a full-width action bar from stealing focus
    // from the card directly above/below it.
    const score = primary + orthogonal * (overlapRatio > 0 ? 1.8 : 4.2) - overlapRatio * 140;
    return { item, score, primary, orthogonal };
  });
  scored.sort((left, right) => left.score - right.score || left.primary - right.primary || left.orthogonal - right.orthogonal);
  return scored[0]?.item || null;
}
function moveSpatialFocus(items, key, active = document.activeElement) {
  const target = spatialNavigationTarget(items, active, key);
  if (!target) return false;
  return focusWorkspaceElement(target);
}
function moveContainerFocus(root, key) {
  return moveSpatialFocus(visibleFocusables(root), key);
}
function editIdentityFocusTarget(key, active) {
  const nameTargets = [$('#identity-name'), workspaceDropdownTriggerForSelect($('#identity-name-candidates'))]
    .filter(item => item && isVisuallyPresent(item));
  const idTargets = [$('#identity-steam-id'), $('#identity-steam-id-clear')]
    .filter(item => item && isVisuallyPresent(item));
  const wallpaper = $('#artwork-wallpaper-preview');
  if (key === 'ArrowUp' && idTargets.includes(active)) return nameTargets[0] || null;
  if (key === 'ArrowUp' && nameTargets.includes(active)) return wallpaper || null;
  if (key === 'ArrowDown' && nameTargets.includes(active)) return idTargets[0] || null;
  return null;
}
function editArtworkFocusTarget(key, active) {
  const cover = $('#artwork-cover-preview');
  const wallpaper = $('#artwork-wallpaper-preview');
  if (!cover || !wallpaper || ![cover, wallpaper].includes(active)) return null;
  if (key === 'ArrowLeft') return active === wallpaper ? cover : null;
  if (key === 'ArrowRight') return active === cover ? wallpaper : null;
  if (key === 'ArrowDown') {
    if (active === wallpaper) return $('#identity-name') || null;
    return workspaceDropdownTriggerForSelect($('#edit-primary-select')) ||
      workspaceDropdownTriggerForSelect($('#edit-bucket-select')) || null;
  }
  return null;
}
function editModalFocusItems() {
  const modal = $('#edit-modal');
  return visibleFocusables(modal).filter(item => !item.matches('.workspace-native-select'));
}
function editModalRowFor(item) {
  return item?.closest?.('[data-edit-row]')?.dataset.editRow || '';
}
function editModalRows(items) {
  const rows = [];
  for (const item of items) {
    const row = editModalRowFor(item);
    if (row && !rows.includes(row)) rows.push(row);
  }
  return rows;
}
function moveEditModalFocus(key) {
  const items = editModalFocusItems();
  if (!items.length) return false;
  const artworkTarget = editArtworkFocusTarget(key, document.activeElement);
  if (artworkTarget) return focusWorkspaceElement(artworkTarget);
  const identityTarget = editIdentityFocusTarget(key, document.activeElement);
  if (identityTarget) return focusWorkspaceElement(identityTarget);
  // Do not use data-edit-row here. Those values describe semantic form
  // groups, not the visual layout: cover and wallpaper are side by side, and
  // the primary/name controls can be in different columns. The controller
  // must follow the rendered rectangles exactly.
  return moveSpatialFocus(items, key) || true;
}
function activeWorkspaceModal() {
  const modalOrder = [...document.querySelectorAll('.modal')];
  return modalOrder.filter(item => !item.classList.contains('hidden')).sort((left, right) => {
    const leftZ = Number.parseInt(getComputedStyle(left).zIndex, 10) || 0;
    const rightZ = Number.parseInt(getComputedStyle(right).zIndex, 10) || 0;
    if (leftZ !== rightZ) return rightZ - leftZ;
    return modalOrder.indexOf(right) - modalOrder.indexOf(left);
  })[0] || null;
}
function artworkActionControls() {
  const modal = $('#artwork-action-modal');
  if (!modal || modal.classList.contains('hidden')) return [];
  return ['#artwork-action-search', '#artwork-action-replace', '#artwork-action-delete']
    .map(selector => $(selector))
    .filter(item => item && !item.disabled && item.offsetParent !== null);
}
function focusArtworkActionControl(index = 0) {
  const controls = artworkActionControls();
  if (!controls.length) return false;
  const target = controls[Math.max(0, Math.min(controls.length - 1, index))];
  return focusWorkspaceElement(target);
}
function handleArtworkActionGamepadKey(event) {
  const modal = $('#artwork-action-modal');
  if (!modal || modal.classList.contains('hidden')) return false;
  const controls = artworkActionControls();
  if (!controls.length) return true;
  const active = document.activeElement;
  if (['ArrowLeft', 'ArrowRight'].includes(event.key)) {
    event.preventDefault(); event.stopPropagation();
    const current = controls.indexOf(active);
    const index = Math.max(0, Math.min(controls.length - 1, current < 0 ? 0 : current + (event.key === 'ArrowRight' ? 1 : -1)));
    return focusArtworkActionControl(index);
  }
  if (['ArrowUp', 'ArrowDown'].includes(event.key)) {
    event.preventDefault(); event.stopPropagation();
    return true;
  }
  if (event.key === 'Enter' || event.key === ' ') {
    event.preventDefault(); event.stopPropagation();
    if (controls.includes(active)) { active.click(); return true; }
    focusArtworkActionControl(0);
    return true;
  }
  return false;
}
function identitySearchModalControls() {
  const modal = $('#identity-search-modal');
  if (!modal || modal.classList.contains('hidden')) return [];
  const trigger = workspaceDropdownTriggerForSelect($('#identity-search-select'));
  const confirm = modal.querySelector('#identity-search-list > .button');
  return [trigger, confirm].filter(item => item && isVisuallyPresent(item));
}
function handleIdentitySearchGamepadKey(event) {
  const modal = $('#identity-search-modal');
  if (!modal || modal.classList.contains('hidden')) return false;
  const controls = identitySearchModalControls();
  if (!controls.length) return true;
  const active = document.activeElement;
  if (['ArrowUp', 'ArrowDown'].includes(event.key)) {
    event.preventDefault();
    event.stopPropagation();
    const current = controls.indexOf(active);
    const next = event.key === 'ArrowDown' ? current + 1 : current - 1;
    if (current >= 0 && next >= 0 && next < controls.length) focusWorkspaceElement(controls[next]);
    return true;
  }
  if (['ArrowLeft', 'ArrowRight'].includes(event.key)) {
    event.preventDefault();
    event.stopPropagation();
    return true;
  }
  return false;
}
function handleModalGamepadKey(event) {
  const modal = activeWorkspaceModal();
  if (!modal) return false;
  if (workspaceDropdownKey(event)) return true;
  if (modal.id === 'artwork-action-modal' && handleArtworkActionGamepadKey(event)) return true;
  if (modal.id === 'identity-search-modal' && handleIdentitySearchGamepadKey(event)) return true;
  const key = event.key;
  if (['ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(key)) {
    event.preventDefault(); event.stopPropagation();
    const active = document.activeElement;
    if (active?.classList?.contains('artwork-candidate') && moveArtworkCandidateFocus(active, key)) return true;
    if (modal.id === 'edit-modal') return moveEditModalFocus(key);
    return moveContainerFocus(modal, key);
  }
  if (key === 'Enter' || key === ' ') {
    const active = document.activeElement;
    if (modal.id === 'edit-modal' && active?.matches?.('.artwork-dropzone')) {
      event.preventDefault(); event.stopPropagation(); return openArtworkActionMenu(active.dataset.artworkType);
    }
    if (active?.matches?.('button,summary')) { event.preventDefault(); event.stopPropagation(); active.click(); return true; }
    // A semantic action on a text field is consumed by the editor boundary;
    // it must never fall through to the library card or activate the form
    // twice. Physical keyboard typing remains untouched by routeWorkspaceKey.
    if (event.workspaceAction && isFormInputTarget(active)) {
      requestGamepadKeyboard(active);
      return true;
    }
    return false;
  }
  return false;
}
function isFormInputTarget(target = document.activeElement) {
  const element = target instanceof Element ? target : document.activeElement;
  return Boolean(element?.matches?.('input, textarea, select, [contenteditable="true"], [contenteditable=""]'));
}
function requestGamepadKeyboard(target = document.activeElement) {
  if (!isFormInputTarget(target) || target.disabled || target.readOnly ||
      !target.matches?.('input:not([readonly]), textarea:not([readonly]), [contenteditable="true"], [contenteditable=""]')) return false;
  target.focus({ preventScroll: false });
  invoke('keyboard').catch(() => {});
  return true;
}
function handleEntryGamepadKey(event) {
  if ($('#entry-view').classList.contains('hidden') || activeWorkspaceModal()) return false;
  if (['ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(event.key)) {
    event.preventDefault(); event.stopPropagation(); return moveContainerFocus($('#entry-view'), event.key);
  }
  if (event.key === 'Enter' || event.key === ' ') {
    const active = document.activeElement;
    if (active?.matches?.('button,summary')) { event.preventDefault(); event.stopPropagation(); active.click(); return true; }
    const defaultAction = $('#open-library-button');
    if (defaultAction) { event.preventDefault(); event.stopPropagation(); defaultAction.focus(); defaultAction.click(); return true; }
  }
  return false;
}
function libraryModalOpen() { return Boolean(activeWorkspaceModal()); }
function handleLibraryGamepadKey(event) {
  if ($('#library-view').classList.contains('hidden') || libraryModalOpen()) return false;
  const cards = libraryCards(); if (!cards.length && !libraryFocusItems().length) return false;
  const key = event.key;
  if (['ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(key)) {
    event.preventDefault(); event.stopPropagation();
    return moveLibraryDirectional(key);
  }
  if (key === 'Enter' || key === ' ') {
    event.preventDefault(); event.stopPropagation();
    const active = document.activeElement;
    if (active?.matches?.('button,.game-card,summary')) { active.click(); return true; }
    return focusLibraryCard(cards[0]);
  }
  if (key.toLowerCase() === 'x') {
    event.preventDefault(); event.stopPropagation();
    const card = activeLibraryCard() || cards[0];
    if (!activeLibraryCard()) return focusLibraryCard(card);
    const game = mergedGames().find(item => item.gameDirectory === card.dataset.directory);
    if (game) openEdit(game);
    return true;
  }
  return false;
}
function cycleLibraryTab(direction) {
  const tabs = [...document.querySelectorAll('.page-tab')]; if (!tabs.length) return;
  let index = tabs.findIndex(item => item.dataset.tab === activeTab); if (index < 0) index = 0;
  index = (index + direction + tabs.length) % tabs.length;
  activeTab = tabs[index].dataset.tab; renderLibrary(); focusLibraryCard(libraryCards()[0]);
}
function pageSelectableGames() { return mergedGames().filter(game => pageSelectable(game, activeTab)); }
function setPageSelection(mode) {
  const keys = pageSelectableGames().map(keyFor); const current = pageSelectionKeys(activeTab);
  if (mode === 'all') {
    if (activeTab === 'not-in-steam') selectedGameDirectories = null;
    else if (activeTab === 'in-steam') selectedSteamDirectories = null;
    else selectedCategoryDirectories[activeTab] = new Set(keys);
  } else if (mode === 'invert') storePageSelection(activeTab, new Set(keys.filter(key => !current.has(key))), keys);
  renderLibrary();
}
function syncPageSelectionControls() {
  const selectable = ['not-in-steam', 'in-steam', 'unclassified', 'non-game'].includes(activeTab);
  const enabled = selectable && pageSelectableGames().length > 0;
  const allBtn = $('#page-select-all');
  const invertBtn = $('#page-invert-selection');
  if (allBtn) { allBtn.classList.toggle('hidden', !selectable); allBtn.disabled = !enabled; }
  if (invertBtn) { invertBtn.classList.toggle('hidden', !selectable); invertBtn.disabled = !enabled; }
}
function renderLibrary() {
  const games = mergedGames(); const counts = categoryCounts(games);
  $('#count-not-in-steam').textContent = formatCount(counts['not-in-steam']); $('#count-in-steam').textContent = formatCount(counts['in-steam']); $('#count-unclassified').textContent = formatCount(counts.unclassified); $('#count-non-game').textContent = formatCount(counts['non-game']); $('#library-subtitle').textContent = `${formatCount(games.length)} 项目 · 已确认 ${formatCount(games.filter(game => game.status === 'ready').length)} 主程序`;
  document.querySelectorAll('.page-tab').forEach(tab => tab.classList.toggle('active', tab.dataset.tab === activeTab)); const inSteam = activeTab === 'in-steam'; const notInSteam = activeTab === 'not-in-steam'; document.querySelector('.steam-target-copy')?.classList.add('hidden'); document.querySelector('.steam-bar')?.classList.remove('hidden'); $('#commit-button').classList.toggle('hidden', inSteam); $('#delete-button').classList.toggle('hidden', !inSteam); $('#commit-button').innerHTML = `点击加入Steam <span class="count-label">${formatSelectedCount(selectedPageCount())}</span>`; $('#delete-button').innerHTML = `点击删除 <span class="count-label">${formatSelectedCount(selectedSteamCount())}</span>`; $('#commit-button').disabled = inSteam || selectedReadyCount() === 0; $('#commit-button').title = '只加入当前页中已验证且已选中的项目'; $('#delete-button').disabled = selectedSteamCount() === 0; $("#in-steam-limitation").classList.add("hidden"); syncPageSelectionControls(); renderRoots();
  renderArtworkRefreshControl();
  const focusedDescriptor = libraryFocusDescriptor(document.activeElement) || libraryFocusMemory;
  const focusedDir = focusedDescriptor?.type === 'card' ? focusedDescriptor.directory : (activeLibraryCard()?.dataset.directory || '');
  library.replaceChildren(); const visible = games.filter(game => category(game) === activeTab); visible.forEach(game => library.appendChild(createCard(game))); $('#empty').classList.toggle('hidden', visible.length !== 0); library.classList.toggle('hidden', visible.length === 0);
  if (visible.length > 0 && !libraryModalOpen()) {
    const restore = focusedDir ? libraryCards().find(c => c.dataset.directory === focusedDir) : null;
    const remembered = resolveLibraryFocusDescriptor(focusedDescriptor);
    const shouldFocus = restore || remembered || !$('#library-view').contains(document.activeElement);
    if (shouldFocus) requestAnimationFrame(() => { if (!libraryModalOpen()) focusWorkspaceElement(restore || remembered || libraryCards()[0]); });
    else scheduleLibraryFocusRepair();
  }
}
function candidateDataFor(game) {
  const key = (game.primaryExecutable || '').toLocaleLowerCase();
  const direct = snapshot.identityCandidates?.[key];
  if (direct?.candidates) return direct.candidates;
  const normalized = Object.entries(snapshot.identityCandidates || {}).find(([candidateKey, value]) => candidateKey === key || value.executable?.toLocaleLowerCase() === key);
  return normalized?.[1]?.candidates || [];
}
function candidatePreview(candidate) {
  if (candidate.preview) return `<img src="${escapeHtml(candidate.preview)}" alt="${escapeHtml(candidate.name || '候选')} 封面" loading="lazy">`;
  return `<span class="candidate-initials">${escapeHtml(initials(candidate.name))}</span>`;
}
function renderIdentityCandidates(game) {
  const section = $('#identity-candidates');
  const list = $('#identity-candidate-list');
  if (!section || !list) return;
  const candidates = candidateDataFor(game).filter(candidate => candidate && candidate.name).slice(0, 5);
  list.replaceChildren();
  section.classList.toggle('hidden', candidates.length === 0);
  for (const candidate of candidates) {
    const card = document.createElement('button');
    card.type = 'button';
    card.className = 'identity-candidate-card';
    const idText = candidate.steamAppId ? `Steam AppID ${candidate.steamAppId}` : (candidate.igdbId ? `IGDB ID ${candidate.igdbId}` : '无直接 ID');
    card.innerHTML = `<span class="candidate-cover">${candidatePreview(candidate)}</span><span class="candidate-copy"><strong>${escapeHtml(candidate.name)}</strong><span>${escapeHtml(idText)} · ${escapeHtml(candidate.provider || '候选')}</span><span>疑似度 ${(Number(candidate.score || 0) * 100).toFixed(0)}%</span></span><span class="candidate-use">选择</span>`;
    card.addEventListener('click', () => identifyCandidate(candidate));
    list.appendChild(card);
  }
}
function renderIdentityNameCandidates(game) {
  const select = $('#identity-name-candidates');
  if (!select) return;
  const candidates = Array.isArray(game?.candidates) ? game.candidates.filter(item => item && item.path && item.role !== 'mod-manager') : [];
  const identityCandidates = candidateDataFor(game);
  const linkedAppId = name => {
    const match = identityCandidates.find(item => item && String(item.name || '').toLocaleLowerCase() === String(name || '').toLocaleLowerCase());
    return match?.steamAppId || match?.appId || match?.storefrontAppId || match?.canonicalAppId || '';
  };
  const primary = game.primaryProductName || game.steam?.formalName || game.steam?.suggestedName || '';
  const rows = [];
  if (primary) rows.push({ name: primary, rank: 0, source: '产品名', steamAppId: linkedAppId(primary) });
  const product = candidates.filter(item => item.productName || item.fileDescription).sort((a, b) => Number(b.score || 0) - Number(a.score || 0));
  for (const item of product) {
    const name = item.productName || item.fileDescription;
    if (name && !rows.some(row => row.name.toLocaleLowerCase() === name.toLocaleLowerCase())) rows.push({ name, rank: 1, source: item.productName ? 'EXE 产品名' : 'EXE 描述', path: item.relativePath || item.path, steamAppId: linkedAppId(name) });
  }
  for (const item of candidates) {
    const name = item.relativePath || item.path;
    if (name && !rows.some(row => row.name.toLocaleLowerCase() === name.toLocaleLowerCase())) rows.push({ name, rank: 2, source: '包内 EXE', path: name, steamAppId: linkedAppId(name) });
  }
  select.replaceChildren();
  const empty = document.createElement('option'); empty.value = ''; empty.textContent = '选择名称候选…'; select.appendChild(empty);
  select._nameRows = rows.slice(0, 20);
  for (const row of select._nameRows) { const option = document.createElement('option'); option.value = row.name; option.textContent = row.name; select.appendChild(option); }
  select.value = '';
  refreshWorkspaceDropdown(select);
}
function cloneEditGame(game) { return JSON.parse(JSON.stringify(game || {})); }
function draftFor(game) {
  const cloned = cloneEditGame(game);
  cloned.override = { ...(cloned.override || {}) };
  cloned.override.artwork = { ...(cloned.override.artwork || {}) };
  return { game: cloned, original: game, originalBucket: manualBucket(game), bucket: manualBucket(game), artworkDirty: false, artworkChanged: { cover: false, wallpaper: false }, identityDirty: false, primaryDirty: false };
}
function draftName() { return $('#identity-name')?.value.trim() || ''; }
function draftSteamId() { return $('#identity-steam-id')?.value.trim() || ''; }
function syncIdentityDraft() {
  if (!editDraft?.game) return;
  const game = editDraft.game;
  const name = draftName();
  const steamId = draftSteamId();
  game.override = { ...(game.override || {}) };
  if (name) game.override.name = name; else delete game.override.name;
  if (steamId) { game.override.steamId = Number(steamId); delete game.override.igdbId; delete game.override.idCleared; }
  else if (steamIdClearPending || game.override.idCleared === true) { delete game.override.steamId; game.override.idCleared = true; }
  editDraft.identityDirty = true;
  selectedActionGame = game;
}
function renderEditDraft(game = selectedActionGame) {
  if (!game) return;
  renderIdentityCandidates(game); renderIdentityNameCandidates(game);
  const [label, tone] = statusLabel(game);
  $('#edit-title').textContent = gameDisplayName(game); $('#edit-path').textContent = game.primaryExecutable || game.gameDirectory || '';
  $('#edit-status').className = `badge ${tone}`; $('#edit-status').textContent = label;
  setBucketOptions(editDraft?.bucket || manualBucket(game));
  renderArtworkDraft();
}
function restoreEditReturnFocus(directory) {
  if (!directory || $('#library-view')?.classList.contains('hidden')) return false;
  const card = libraryCards().find(item => String(item.dataset.directory || '').toLocaleLowerCase() === directory);
  if (!card) return false;
  currentCardDirectory = directory;
  libraryCards().forEach(item => item.classList.toggle('current-card', item === card));
  requestAnimationFrame(() => {
    if ($('#edit-modal')?.classList.contains('hidden') && !libraryModalOpen()) {
      const latest = libraryCards().find(item => String(item.dataset.directory || '').toLocaleLowerCase() === directory);
      if (latest) {
        currentCardDirectory = directory;
        libraryCards().forEach(item => item.classList.toggle('current-card', item === latest));
        focusLibraryCard(latest);
      }
    }
  });
  return true;
}
function focusEditModal() {
  const modal = $('#edit-modal');
  if (!modal) return false;
  const preferred = workspaceDropdownTriggerForSelect($('#edit-primary-select')) ||
    workspaceDropdownTriggerForSelect($('#edit-bucket-select')) ||
    modal.querySelector('#artwork-cover-preview');
  return focusModalItem(modal, preferred);
}
function openEdit(game) {
  if (!game) return;
  try {
    const previousExecutable = normalizedUiPath(selectedActionGame?.primaryExecutable || '');
    const nextExecutable = normalizedUiPath(game.primaryExecutable || '');
    if (previousExecutable && nextExecutable && previousExecutable !== nextExecutable) cancelEditorBackgroundTasks();
    const returnDirectory = keyFor(game);
    if (returnDirectory) editReturnCardDirectory = returnDirectory;
    if (!editDraft || normalizedUiPath(editDraft.game?.primaryExecutable) !== normalizedUiPath(game.primaryExecutable)) editDraft = draftFor(game);
    selectedActionGame = editDraft.game;
    artworkDraft = artworkDraftFromGame(selectedActionGame);
    artworkPageBase = { cover: artworkDraft.cover ? { ...artworkDraft.cover } : null, wallpaper: artworkDraft.wallpaper ? { ...artworkDraft.wallpaper } : null };
    steamIdClearPending = selectedActionGame.override?.idCleared === true;
    renderEditDraft(selectedActionGame);
    const override = selectedActionGame.override || {};
    $('#identity-name').value = override.name || selectedActionGame.steam?.suggestedName || selectedActionGame.primaryProductName || selectedActionGame.directoryName || '';
    $('#identity-steam-id').value = formalSteamAppId(selectedActionGame);
    const rawCandidates = (selectedActionGame.candidates || []).filter(candidate => candidate.role !== 'mod-manager' && candidate.path);
    const currentPath = selectedActionGame.primaryExecutable || '';
    const candidates = currentPath && !rawCandidates.some(candidate => normalizedUiPath(candidate.path) === normalizedUiPath(currentPath)) ? [{ path: currentPath, relativePath: currentPath, current: true }, ...rawCandidates] : rawCandidates;
    const block = $('#edit-primary-block'); const select = $('#edit-primary-select');
    select.replaceChildren(); block.classList.toggle('hidden', !candidates.length && !currentPath); select.classList.remove('hidden');
    for (const candidate of candidates.slice(0, 12)) { const option = document.createElement('option'); option.value = candidate.path; option.textContent = `${candidate.current ? '当前确定 · ' : ''}${candidate.relativePath || candidate.path}`; option.selected = normalizedUiPath(candidate.path) === normalizedUiPath(currentPath); select.appendChild(option); }
    upgradeEditDropdowns();
    refreshWorkspaceDropdown(select);
    refreshWorkspaceDropdown($('#edit-bucket-select'));
    const primaryDropdown = workspaceDropdownForSelect(select);
    primaryDropdown?.wrapper.classList.toggle('hidden', candidates.length <= 1);
    $('#edit-modal').classList.remove('hidden'); requestAnimationFrame(focusEditModal);
    startArtworkPrefetch(selectedActionGame);
  } catch (error) {
    reportUiError('open-edit', error, { executable: game.primaryExecutable || '', gameDirectory: game.gameDirectory || '' });
    $('#edit-modal')?.classList.add('hidden');
    showNotice('编辑页面加载失败，已记录诊断信息；请重新打开。', true, libraryNotice);
  }
}
function candidateYear(candidate) { const raw = candidate?.year || candidate?.releaseYear || candidate?.releaseDate || candidate?.release_date || ''; const match = String(raw).match(/(19|20)\d{2}/); return match ? Number(match[0]) : 0; }
function searchCandidatesFor(game) { const list = candidateDataFor(game).filter(candidate => candidate && candidate.name).map(candidate => ({ ...candidate, year: candidateYear(candidate) })); const currentName = gameDisplayName(game); if (currentName && !list.some(candidate => String(candidate.name).toLocaleLowerCase() === String(currentName).toLocaleLowerCase())) list.push({ name: currentName, steamAppId: formalSteamAppId(game) || null, preview: game.steam?.artworkPreview?.tall?.url || null, provider: '当前识别', score: 1, year: candidateYear(game.steam?.releaseDate) }); return list.sort((left, right) => (right.year - left.year) || (Number(right.score || 0) - Number(left.score || 0)) || String(left.name).localeCompare(String(right.name), 'zh-Hans')); }
function renderSearchCandidates(game, candidatesOverride = null, queryOverride = null) {
  const list = $('#identity-search-list');
  const incoming = (candidatesOverride || searchCandidatesFor(game)).slice();
  const manual = identitySearchManualCandidate;
  if (manual && !incoming.some(candidate => String(candidate?.steamAppId || '') === String(manual.steamAppId || '') &&
      String(candidate?.name || '').toLocaleLowerCase() === String(manual.name || '').toLocaleLowerCase())) {
    incoming.unshift(manual);
  }
  const candidates = incoming.sort((left, right) => {
    const leftPriority = left.manualSelection ? 2 : (left.provider === 'steam' ? 1 : 0);
    const rightPriority = right.manualSelection ? 2 : (right.provider === 'steam' ? 1 : 0);
    return rightPriority - leftPriority || (Number(right.year || 0) - Number(left.year || 0)) ||
      (Number(right.score || 0) - Number(left.score || 0));
  });
  list.replaceChildren(); stopIdentitySearchLoading();
  const query = String(queryOverride ?? game?._searchQuery ?? artworkSearchQuery(game) ?? '').trim();
  $('#identity-search-query').textContent = query;
  $('#identity-search-summary').textContent = `名称：${query || '（未填写）'} · ${candidates.length} 个候选 · 请选择名称和 ID`;
  if (!candidates.length) { list.innerHTML = '<div class="identity-search-empty">暂无候选名称，请修改名称后再次自动识别。</div>'; return; }
  const select = document.createElement('select');
  select.id = 'identity-search-select'; select.className = 'identity-search-select';
  select.setAttribute('aria-label', '选择游戏名称和 ID');
  for (const candidate of candidates) {
    const option = document.createElement('option'); option.value = String(candidates.indexOf(candidate));
    const idText = candidate.steamAppId ? `Steam AppID ${candidate.steamAppId}` :
      (candidate.igdbId ? `IGDB ID ${candidate.igdbId}（待 Steam 验证）` : '无直接 ID');
    const sourceText = candidate.manualSelection ? '当前输入' : (candidate.provider || '候选');
    const year = candidateYear(candidate);
    const yearText = year ? `${year} · ` : '';
    option.textContent = `${yearText}${candidate.name} · ${idText} · ${sourceText}`;
    select.appendChild(option);
  }
  const confirm = document.createElement('button'); confirm.type = 'button'; confirm.className = 'button secondary';
  confirm.textContent = '选择名称和 ID';
  confirm.addEventListener('click', () => identifyCandidate(candidates[Number(select.value) || 0]));
  list.append(select, confirm); upgradeWorkspaceSelect(select);
  requestAnimationFrame(() => workspaceDropdownTriggerForSelect(select)?.focus());
}
async function identifyCandidate(candidate) {
  if (!selectedActionGame || !candidate?.name) return;
  const game = selectedActionGame;
  $('#identity-search-modal').classList.add('hidden');
  stopIdentitySearchLoading();
  identitySearchManualCandidate = null;
  searchReturnToEdit = false;
  game.override = { ...(game.override || {}) };
  game.override.name = candidate.name;
  if (candidate.steamAppId) {
    game.override.steamId = Number(candidate.steamAppId);
    delete game.override.igdbId; delete game.override.idCleared; steamIdClearPending = false;
  } else if (candidate.igdbId) {
    delete game.override.steamId;
    game.override.igdbId = Number(candidate.igdbId);
    game.override.idCleared = true; steamIdClearPending = true;
  }
  editDraft.identityDirty = true;
  selectedActionGame = game;
  $('#identity-name').value = candidate.name;
  $('#identity-steam-id').value = candidate.steamAppId ? String(candidate.steamAppId) : '';
  renderEditDraft(game);
  renderArtworkDraft();
  $('#edit-modal').classList.remove('hidden');
  showNotice('名称和 ID 已暂存，请点击“保存内容”后写入配置。', false, libraryNotice);
}
function identityEventMatches(message) {
  const executable = normalizedUiPath(message?.executable || '');
  const expectedExecutable = normalizedUiPath(selectedActionGame?.primaryExecutable || '');
  const expectedQuery = String($('#identity-search-query')?.textContent || '').trim();
  if (expectedExecutable && executable !== expectedExecutable) return false;
  if (expectedQuery && message?.query && String(message.query) !== expectedQuery) return false;
  if (message?.jobId) {
    if (identitySearchJobId && message.jobId !== identitySearchJobId) return false;
    identitySearchJobId = message.jobId;
  }
  return true;
}
function manualIdentifyEventMatches(message) {
  const executable = normalizedUiPath(message?.executable || '');
  const generation = Number(message?.generation || 0);
  if (!executable || !generation) return true;
  const current = manualIdentifyGenerations.get(executable);
  if (current && current !== generation) return false;
  manualIdentifyGenerations.set(executable, generation);
  return true;
}
async function searchGame() {
  if (!selectedActionGame?.primaryExecutable) return showNotice('当前项目还没有可验证的主程序。', true, libraryNotice);
  const game = selectedActionGame;
  const steamId = $('#identity-steam-id').value.trim();
  const name = $('#identity-name').value.trim() || artworkSearchQuery(game);
  if (steamId && !/^[1-9]\d*$/.test(steamId)) return showNotice('Steam AppID 必须是正整数。', true, libraryNotice);
  const token = ++identitySearchToken;
  identitySearchInFlight = true; identitySearchCancelled = false; searchReturnToEdit = true;
  identitySearchExecutable = game.primaryExecutable; identitySearchQuery = name;
  identitySearchManualCandidate = steamId ? {
    name, steamAppId: Number(steamId), provider: '当前输入', manualSelection: true, score: 1, year: 0
  } : null;
  $('#identity-search-query').textContent = name;
  $('#identity-search-summary').textContent = `名称：${name} · 正在后台搜索候选，不会阻塞编辑页面`;
  $('#identity-search-list').replaceChildren(); startIdentitySearchLoading();
  $('#identity-search-modal').classList.remove('hidden');
  requestAnimationFrame(() => $('#identity-search-cancel')?.focus({ preventScroll: true }));
  try {
    const result = await invoke('searchIdentity', { executable: game.primaryExecutable, query: name });
    if (token !== identitySearchToken || identitySearchCancelled) return;
    const candidates = Array.isArray(result?.candidates) ? result.candidates : [];
    if (candidates.length || identitySearchManualCandidate) {
      renderSearchCandidates({ ...game, directoryName: name, _searchQuery: name }, candidates, name);
      showNotice(result.cached ? '已显示缓存候选，网络结果会继续更新。' : '自动识别已在后台搜索，请选择名称和 ID。', false, libraryNotice);
    }
    if (result?.pending) return;
    identitySearchInFlight = false; searchReturnToEdit = false;
    if (!candidates.length && !identitySearchManualCandidate) renderSearchCandidates({ ...game, directoryName: name, _searchQuery: name }, [], name);
  } catch (error) {
    identitySearchInFlight = false; stopIdentitySearchLoading(); searchReturnToEdit = false;
    const text = error?.message || String(error);
    if (text.includes('TASK_CANCELLED') || text.includes('ERROR_CANCELLED') || identitySearchCancelled) {
      if (!identitySearchCancelled) { $('#identity-search-modal').classList.add('hidden'); openEdit(game); showNotice('已取消自动识别，返回当前游戏。', false, libraryNotice); }
    } else { $('#identity-search-list').innerHTML = '<div class="identity-search-empty">自动识别暂未完成，可稍后再次点击自动识别。</div>'; showError(error, libraryNotice); }
  }
}
function artworkOverride(game) {
  const direct = game?.override && typeof game.override === "object" ? game.override : {};
  const legacy = direct.artwork && typeof direct.artwork === "object" ? direct.artwork : {};
  return { ...legacy, ...direct };
}
function artworkEntry(game, type) {
  const override = artworkOverride(game);
  const protection = override.artworkProtection && typeof override.artworkProtection === "object" ? override.artworkProtection : {};
  if (protection[type] === "deleted") return null;
  const entry = override[type];
  if (entry?.url || entry?.portableFile || entry?.file || entry?.path) return entry;
  const preview = type === "cover" ? game.steam?.artworkPreview?.tall : game.steam?.artworkPreview?.hero;
  return preview || null;
}
function artworkPreviewUrl(entry) {
  if (!entry) return '';
  const cacheBust = value => {
    const updated = Number(entry.updatedAt || entry.updated_at || 0);
    if (!updated || !/^https?:\/\//i.test(value)) return value;
    return `${value}${value.includes('?') ? '&' : '?'}v=${encodeURIComponent(String(updated))}`;
  };
  if (entry.url) return cacheBust(String(entry.url));
  const raw = String(entry.path || entry.file || entry.portableFile || '').trim();
  if (!raw) return '';
  const root = normalizedUiPath(snapshot.dataRoot || '');
  const normalized = normalizedUiPath(raw);
  if (root && (normalized === root || normalized.startsWith(root + '\\'))) {
    const relative = normalized.slice(root.length).replace(/^\\+/, '');
    const mapped = `https://steam-library-data.local/${relative.split('\\').map(part => encodeURIComponent(part)).join('/')}`;
    return cacheBust(mapped);
  }
  return /^https?:\/\//i.test(raw) ? raw : '';
}
function artworkDropMarkup(entry, label) { const previewUrl = artworkPreviewUrl(entry); if (previewUrl) return `<img src="${escapeHtml(previewUrl)}" alt="${escapeHtml(label)}" loading="lazy">`; const raw = entry?.path || entry?.file || entry?.portableFile; if (raw) return `<span class="artwork-path">${escapeHtml(raw)}</span>`; return `<span class="artwork-empty">暂无${escapeHtml(label)}</span>`; }
function bindArtworkImageFallback(target, label) { const image = target.querySelector('img'); if (!image) return; image.addEventListener('error', () => { target.innerHTML = `<span class="artwork-empty">${escapeHtml(label)}加载失败，可重新指定</span>`; }, { once: true }); }
function artworkSlotMarkup(slot, type) {
  const label = type === "cover" ? "封面" : "壁纸";
  if (slot?.state === "deleted") return `<span class="artwork-empty artwork-deleted">${label}已删除，保存后不自动补回</span>`;
  return artworkDropMarkup(slot?.asset || null, label);
}
function renderArtworkDraft() {
  if (!selectedActionGame) return;
  for (const type of ["cover", "wallpaper"]) {
    const target = $(`#artwork-${type}-preview`);
    if (!target) {
      reportUiError('artwork-preview-missing', new Error(`缺少素材预览节点：${type}`));
      continue;
    }
    const slot = artworkDraft[type];
    target.dataset.artworkType = type;
    target.dataset.editRow = `artwork-${type}`;
    target.setAttribute('aria-label', `${type === 'cover' ? '封面' : '壁纸'}操作，按 A 打开`);
    target.innerHTML = artworkSlotMarkup(slot, type);
    if (slot?.state !== "deleted") bindArtworkImageFallback(target, type === "cover" ? "封面" : "壁纸");
  }
}
function openArtworkActionMenu(type) {
  if (!selectedActionGame) return false;
  artworkActionType = type === 'wallpaper' ? 'wallpaper' : 'cover';
  const label = artworkActionType === 'wallpaper' ? '壁纸' : '封面';
  $('#artwork-action-title').textContent = label;
  $('#artwork-action-detail').textContent = `${label}：方向键移动，按 A 确认，按 B 关闭。`;
  $('#artwork-action-modal').classList.remove('hidden');
  // The host only polls the controller while this window is foreground. A
  // mouse click can leave focus in a WebView child or another transient
  // surface, so explicitly reactivate the workspace before accepting A/B.
  invoke('activateWorkspace').catch(() => {});
  requestAnimationFrame(() => focusArtworkActionControl(0));
  return true;
}
function closeArtworkActionMenu(restoreFocus = true) {
  const modal = $('#artwork-action-modal');
  if (!modal || modal.classList.contains('hidden')) return false;
  modal.classList.add('hidden');
  if (restoreFocus) $(`#artwork-${artworkActionType}-preview`)?.focus({ preventScroll: true });
  return true;
}
function artworkActionForCurrentType(action) {
  const type = artworkActionType;
  closeArtworkActionMenu(false);
  if (action === 'search') return searchArtwork(type);
  if (action === 'replace') return promptArtworkPath(type);
  if (action === 'delete') return clearArtwork(type);
  return false;
}
function artworkSearchQuery(game) { return gameDisplayName(game) || String(game.primaryExecutable || '').split(/[\\/]/).pop().replace(/\.exe$/i, ''); }
function formalSteamAppId(game) { if (game?.override?.idCleared === true) return ''; const value = game.override?.steamId || game.steam?.steamStoreAppId || game.steam?.storefrontAppId || game.steam?.appId || game.steam?.canonicalAppId || game.steam?.localAppId || game.steam?.requestedAppId || ''; const text = String(value ?? '').trim(); return /^[1-9]\d*$/.test(text) ? text : ''; }
function artworkSearchSteamId(game) { return formalSteamAppId(game); }
function artworkSearchCacheKey(game, type) {
  const executable = normalizedUiPath(game?.primaryExecutable || '');
  const query = artworkSearchQuery(game).trim();
  const steamAppId = artworkSearchSteamId(game);
  return `${executable}\u001f${type}\u001f${query}\u001f${steamAppId}`;
}
function artworkSearchRequest(game, type) {
  return {
    executable: game?.primaryExecutable || '',
    query: artworkSearchQuery(game),
    type,
    steamAppId: artworkSearchSteamId(game)
  };
}
function artworkPrefetchEntryFresh(entry) {
  if (!entry) return false;
  if (entry.pending) return true;
  const completedAt = Number(entry.completedAt || 0);
  return Boolean(entry.result) && completedAt > 0 && Date.now() - completedAt <= ARTWORK_PREFETCH_TTL_MS;
}
function warmArtworkPreviewCache(result) {
  const candidates = Array.isArray(result?.candidates) ? result.candidates : [];
  const urls = [...new Set(candidates.slice(0, ARTWORK_PREFETCH_MAX_PRELOADS)
    .map(candidate => candidate?.previewUrl || candidate?.url)
    .filter(url => typeof url === 'string' && /^https?:/i.test(url)))];
  if (!urls.length) return;
  const preload = () => {
    for (const url of urls) {
      const image = new Image();
      image.decoding = 'async';
      image.loading = 'eager';
      image.src = url;
    }
  };
  if (typeof window.requestIdleCallback === 'function') window.requestIdleCallback(preload, { timeout: 1200 });
  else window.setTimeout(preload, 0);
}
function ensureArtworkSearch(game, type) {
  const key = artworkSearchCacheKey(game, type);
  const existing = artworkPrefetchCache.get(key);
  if (artworkPrefetchEntryFresh(existing)) {
    return existing.pending || Promise.resolve(existing.result);
  }
  if (existing) artworkPrefetchCache.delete(key);
  const entry = { key, startedAt: Date.now(), completedAt: 0, result: null, error: null, pending: null };
  const request = invoke('searchArtwork', artworkSearchRequest(game, type));
  entry.pending = request.then(result => {
    if (artworkPrefetchCache.get(key) === entry) {
      entry.result = result;
      entry.completedAt = Date.now();
      entry.pending = null;
      entry.error = null;
      warmArtworkPreviewCache(result);
    }
    return result;
  }).catch(error => {
    if (artworkPrefetchCache.get(key) === entry) {
      entry.pending = null;
      entry.completedAt = Date.now();
      entry.error = String(error?.message || error || '素材搜索失败');
    }
    throw error;
  });
  artworkPrefetchCache.set(key, entry);
  return entry.pending;
}
function startArtworkPrefetch(game) {
  if (!game) return;
  for (const type of ['cover', 'wallpaper']) {
    const key = artworkSearchCacheKey(game, type);
    const cached = artworkPrefetchCache.get(key);
    if (artworkPrefetchEntryFresh(cached)) continue;
    artworkPrefetchPendingCount++;
    ensureArtworkSearch(game, type).catch(() => {}).finally(() => {
      artworkPrefetchPendingCount = Math.max(0, artworkPrefetchPendingCount - 1);
    });
  }
}
function artworkDraftFromGame(game) {
  const override = artworkOverride(game);
  const protection = override.artworkProtection && typeof override.artworkProtection === "object" ? override.artworkProtection : {};
  const slotFor = type => {
    const raw = override[type];
    if (raw) return { state: "set", asset: { ...raw, path: raw.file || raw.path || raw.portableFile || raw.url || "" } };
    if (protection[type] === "deleted") return { state: "deleted", asset: null };
    return { state: "inherit", asset: artworkEntry(game, type) };
  };
  return { cover: slotFor("cover"), wallpaper: slotFor("wallpaper"), dirty: { cover: false, wallpaper: false } };
}
function openArtworkEditor() { if (!selectedActionGame) return; const artworkId = artworkSearchSteamId(selectedActionGame); $('#artwork-search-hint').textContent = artworkId ? `已确认 Steam AppID ${artworkId}` : `搜索词：${artworkSearchQuery(selectedActionGame)}（优先产品名称，其次 EXE）`; renderArtworkDraft(); $('#artwork-status').classList.add('hidden'); $('#edit-modal').classList.remove('hidden'); startArtworkPrefetch(selectedActionGame); }
async function promptArtworkPath(type) { if (!selectedActionGame) return; try { const result = await invoke('pickArtwork', { type }); if (result?.cancelled || !result?.path) return; artworkDraft[type] = { state: "set", asset: { path: result.path, file: result.path } }; artworkDraft.dirty[type] = true; renderArtworkDraft(); } catch (error) { showError(error, libraryNotice); } }
function clearArtwork(type) { artworkDraft[type] = { state: "deleted", asset: null }; artworkDraft.dirty[type] = true; renderArtworkDraft(); showNotice(`${type === "cover" ? "封面" : "壁纸"}已标记删除；点击“保存内容”后应用。`, false, libraryNotice); }
function moveArtworkCandidateFocus(card, key) {
  const cards = [...document.querySelectorAll("#artwork-search-list .artwork-candidate")];
  if (!cards.includes(card) || cards.length < 2) return false;
  return moveSpatialFocus(cards, key, card);
}
function artworkProviderLabel(provider) {
  if (provider === "steam-cdn" || provider === "steam-store") return "Steam 官方";
  if (provider === "playnite-igdb") return "Playnite-IGDB";
  if (provider === "baidu-image") return "百度图片";
  return provider || "图片候选";
}
function artworkCandidateFallbackTitle(candidate) {
  if (candidate?.provider === "steam-cdn") return "Steam 官方候选";
  if (candidate?.provider === "playnite-igdb") return "Playnite-IGDB 候选";
  if (candidate?.provider === "baidu-image") return "百度图片候选";
  return "图片候选";
}
function renderArtworkCandidates(result) {
  artworkCandidates = Array.isArray(result?.candidates) ? result.candidates : [];
  $("#artwork-search-title").textContent = artworkSearchType === "wallpaper" ? "选择壁纸" : "选择封面";
  $("#artwork-search-summary").textContent = `${result?.query || artworkSearchQuery(selectedActionGame)} · ${artworkCandidates.length} 个候选 · ${result?.filterInstruction || "已过滤过小图片"}`;
  const list = $("#artwork-search-list"); list.dataset.artworkType = artworkSearchType; list.replaceChildren();
  $("#artwork-search-empty").classList.toggle("hidden", artworkCandidates.length !== 0);
  for (const candidate of artworkCandidates) {
    const card = document.createElement("button"); card.type = "button"; card.className = "artwork-candidate"; card.tabIndex = 0;
    const dimensions = `${Number(candidate.width || 0)}×${Number(candidate.height || 0)}`;
    card.innerHTML = `<span class="artwork-candidate-preview"><img src="${escapeHtml(candidate.previewUrl || candidate.url)}" alt="候选图片" loading="lazy"></span><strong class="artwork-candidate-title">${escapeHtml(candidate.title || artworkCandidateFallbackTitle(candidate))}</strong><span class="artwork-candidate-meta"><span>${escapeHtml(artworkProviderLabel(candidate.provider))}</span><span>${dimensions}</span><span>${candidate.preferred ? "优选" : "合格"}</span></span>`;
    card.addEventListener("click", () => applyArtworkCandidate(candidate, card));
     card.addEventListener("keydown", event => {
       if (event.target !== card) return;
       if (event.key === "Enter" || event.key === " ") {
         event.preventDefault();
         event.stopPropagation();
         applyArtworkCandidate(candidate, card);
         return;
       }
       if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(event.key)) {
         // Candidate cards own the complete directional event, including
         // edge presses. An edge press must stay in this grid and must not
         // fall through to modal-level linear navigation.
         event.preventDefault();
         event.stopPropagation();
         moveArtworkCandidateFocus(card, event.key);
       }
     });
     const image = card.querySelector("img"); image?.addEventListener("error", () => { card.querySelector(".artwork-candidate-preview").textContent = "预览加载失败"; }, { once: true });
    list.appendChild(card);
  }
  requestAnimationFrame(() => list.querySelector(".artwork-candidate")?.focus());
}
async function applyArtworkCandidate(candidate, card) {
  if (!selectedActionGame || !candidate?.url || downloadingCandidateUrl) return;
  const token = ++artworkTaskToken;
  artworkTaskInFlight = true;
  downloadingCandidateUrl = candidate.url; card?.classList.add("loading"); card?.setAttribute("aria-busy", "true");
  try {
    const result = await invoke("downloadArtworkCandidate", { executable: selectedActionGame.primaryExecutable, type: artworkSearchType, url: candidate.url });
    if (token !== artworkTaskToken) return;
    artworkDraft[artworkSearchType] = { state: "set", asset: { path: result.path, file: result.path, width: result.width, height: result.height, provider: candidate.provider || "baidu-image", source: "search" } };
    artworkDraft.dirty[artworkSearchType] = true;
    $("#artwork-search-modal").classList.add("hidden"); renderArtworkDraft();
    if (token === artworkTaskToken) {
      renderArtworkDraft();
      showNotice((artworkSearchType === "wallpaper" ? "壁纸" : "封面") + "已下载，请点击“保存内容”。", false, libraryNotice);
    }
  } catch (error) { if (token === artworkTaskToken && !String(error?.message || error).includes("CANCELLED")) showError(error, libraryNotice); }
  finally { if (token === artworkTaskToken) artworkTaskInFlight = false; downloadingCandidateUrl = ""; card?.classList.remove("loading"); card?.removeAttribute("aria-busy"); }
}
async function searchArtwork(type) {
  if (!selectedActionGame) { showNotice("请先打开一个游戏卡片的编辑页面。", true, libraryNotice); return; }
  const token = ++artworkTaskToken;
  artworkTaskInFlight = true;
  artworkSearchType = type;
  $("#artwork-search-title").textContent = type === "wallpaper" ? "选择壁纸" : "选择封面";
  $("#artwork-search-summary").textContent = "正在搜索：" + artworkSearchQuery(selectedActionGame) + " · 弱网下仍可返回";
  $("#artwork-search-list").dataset.artworkType = type;
  $("#artwork-search-list").innerHTML = '<div class="search-loading"><strong>正在获取候选图片</strong><span>搜索完成后可直接选择素材</span></div>';
  $("#artwork-search-empty").classList.add("hidden");
  $("#artwork-search-modal").classList.remove("hidden");
  try { const result = await ensureArtworkSearch(selectedActionGame, type); if (token === artworkTaskToken) renderArtworkCandidates(result); }
  catch (error) { if (token === artworkTaskToken && !String(error?.message || error).includes("CANCELLED")) { $("#artwork-search-list").innerHTML = '<div class="search-loading error">搜索失败，可返回后改用“指定图片”</div>'; showError(error, libraryNotice); } }
  finally { if (token === artworkTaskToken) artworkTaskInFlight = false; }
}
function cancelArtworkTask() { artworkTaskToken++; artworkTaskInFlight = false; downloadingCandidateUrl = ""; $("#artwork-search-modal")?.classList.add("hidden"); invoke("cancelArtwork").catch(() => {}); }
function syncArtworkDraftToEdit() {
  if (!selectedActionGame || !editDraft?.game) return;
  selectedActionGame.override = { ...(selectedActionGame.override || {}) };
  selectedActionGame.override.artworkProtection = { ...(selectedActionGame.override.artworkProtection || {}) };
  for (const type of ["cover", "wallpaper"]) {
    const slot = artworkDraft[type];
    if (!artworkDraft.dirty[type]) continue;
    if (slot?.state === "set" && slot.asset) {
      selectedActionGame.override[type] = { ...slot.asset, path: slot.asset.path || slot.asset.file || slot.asset.portableFile || slot.asset.url || "" };
      selectedActionGame.override.artworkProtection[type] = "manual";
    } else if (slot?.state === "deleted") {
      delete selectedActionGame.override[type];
      selectedActionGame.override.artworkProtection[type] = "deleted";
    }
    editDraft.artworkChanged[type] = true;
  }
  if (!Object.keys(selectedActionGame.override.artworkProtection).length) delete selectedActionGame.override.artworkProtection;
  editDraft.artworkDirty = editDraft.artworkDirty || artworkDraft.dirty.cover || artworkDraft.dirty.wallpaper;
}
function confirmArtworkDraft() {
  syncArtworkDraftToEdit();
  renderEditDraft(selectedActionGame);
  showNotice('素材修改已更新，请点击“保存内容”。', false, libraryNotice);
}
function cancelArtworkEditor() {
  artworkDraft = artworkDraftFromGame(editDraft?.game || selectedActionGame);
  $('#edit-modal').classList.remove('hidden');
  $('#artwork-status').classList.add('hidden');
  renderArtworkDraft();
}
async function persistEditDraft() {
  if (!editDraft?.game || editSaveInFlight) return false;
  const saveButton = $('#edit-save-content');
  editSaveInFlight = true;
  if (saveButton) { saveButton.disabled = true; saveButton.textContent = '正在保存…'; }
  showNotice('正在写入隔离配置并刷新当前游戏素材…', false, libraryNotice);
  syncIdentityDraft();
  syncArtworkDraftToEdit();
  const game = editDraft.game;
  const original = editDraft.original;
  try {
    const originalName = original.override?.name || '';
    const name = game.override?.name || '';
    if (name && name !== originalName) snapshot = await invoke('saveManualName', { executable: game.primaryExecutable, name });
    const originalId = String(formalSteamAppId(original) || '');
    const id = game.override?.idCleared ? '' : String(game.override?.steamId || '');
    const originalIgdbId = Number(original.override?.igdbId || 0);
    const igdbId = game.override?.idCleared ? Number(game.override?.igdbId || 0) : 0;
    if (game.override?.idCleared && originalId) snapshot = await invoke('clearManualSteamId', { executable: game.primaryExecutable });
    if (id && id !== originalId) snapshot = await invoke('identifyOne', { executable: game.primaryExecutable, manualName: name, steamId: id });
    else if (igdbId && igdbId !== originalIgdbId) snapshot = await invoke('identifyOne', { executable: game.primaryExecutable, manualName: name, igdbId: String(igdbId) });
    if (editDraft.artworkDirty) {
      const args = { executable: game.primaryExecutable };
      for (const type of ['cover', 'wallpaper']) {
        if (!editDraft.artworkChanged[type]) continue;
        const value = game.override?.[type]?.path || game.override?.[type]?.file || game.override?.[type]?.portableFile || game.override?.[type]?.url || '';
        args[`${type}Path`] = value;
        args[`clear${type[0].toUpperCase()}${type.slice(1)}`] = !value;
      }
      snapshot = await invoke('saveArtworkOverride', args);
    }
    if (editDraft.bucket !== editDraft.originalBucket) snapshot = await invoke('setBucket', { gameDirectory: game.gameDirectory, bucket: editDraft.bucket });
    snapshot = await invoke('state');
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
    const refreshed = mergedGames().find(item => normalizedUiPath(item.primaryExecutable) === normalizedUiPath(game.primaryExecutable));
    if (refreshed) {
      // Rebuild from the committed snapshot so new artwork is visible immediately.
      editDraft = draftFor(refreshed);
      selectedActionGame = editDraft.game;
      openEdit(selectedActionGame);
      artworkDraft = artworkDraftFromGame(selectedActionGame);
      renderArtworkDraft();
    }
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
    showNotice('编辑内容已保存；封面和壁纸已立即应用，无需切换页面或重启工作台。', false, libraryNotice);
    return true;
  } catch (error) { showError(error, libraryNotice); return false; }
  finally {
    editSaveInFlight = false;
    if (saveButton) { saveButton.disabled = false; saveButton.textContent = '保存内容'; }
  }
}
function setBucketOptions(value) { const select = $('#edit-bucket-select'); if (select) { select.value = value || 'auto'; refreshWorkspaceDropdown(select); } }
function closeEdit() {
  cancelEditorBackgroundTasks();
  closeWorkspaceDropdown(false);
  closeArtworkActionMenu(false);
  const returnDirectory = editReturnCardDirectory || keyFor(selectedActionGame);
  $('#edit-modal').classList.add('hidden');
  $('#identity-search-modal')?.classList.add('hidden');
  identitySearchManualCandidate = null;
  $('#artwork-search-modal').classList.add('hidden');
  selectedActionGame = null;
  editDraft = null;
  artworkDraft = { cover: null, wallpaper: null, dirty: { cover: false, wallpaper: false } };
  editReturnCardDirectory = '';
  restoreEditReturnFocus(returnDirectory);
}
async function refreshSteamPlan() { try { snapshot = await invoke('plan', selectedTargetArguments()); renderEntry(); renderLibrary(); } catch (error) { showError(error, libraryNotice); } }
async function runBusy(command, title, detail, args = {}, success = '') { clearNotice(); setBusy(true, title, detail); try { snapshot = await invoke(command, args); renderEntry(); if (!$('#library-view').classList.contains('hidden')) renderLibrary(); if (success) showNotice(success, false, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); return true; } catch (error) { const text = error?.message || String(error); const cancelled = text.includes('TASK_CANCELLED') || text.includes('ERROR_CANCELLED'); if (cancelled) { showNotice(/搜索游戏|搜索候选|识别真实游戏/.test(title) ? '已取消搜索，返回当前游戏。' : '已取消当前任务。', false, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); if (searchReturnToEdit && selectedActionGame) { searchReturnToEdit = false; $('#identity-search-modal')?.classList.add('hidden'); openEdit(selectedActionGame); } } else showError(error, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); return false; } finally { cancelInFlight = false; setBusy(false); } }
async function startNonBlockingRefresh() {
  if (['starting', 'queued', 'running'].includes(manualRefreshProgress.phase)) return;
  clearNotice();
  manualRefreshProgress = { phase: 'starting', current: 0, total: 0 };
  renderArtworkRefreshControl();
  setBusy(true, '正在启动手动刷新', '识别将在后台合并执行，页面保持可操作。');
  try {
    snapshot = await invoke('prepare');
    const request = snapshot?.scrapeRequest || {};
    manualRefreshProgress = {
      phase: request.queued ? 'queued' : 'starting',
      current: 0,
      total: Number(request.total || snapshot?.scrape?.total || 0)
    };
    renderArtworkRefreshControl();
    renderEntry();
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
    showNotice('手动刷新已启动，完成后自动更新页面。', false, libraryNotice);
  } catch (error) {
    manualRefreshProgress = { phase: 'failed', current: 0, total: 0 };
    renderArtworkRefreshControl();
    showError(error, libraryNotice);
  } finally {
    cancelInFlight = false;
    setBusy(false);
  }
}

async function openLibraryView() {
  clearNotice();
  try {
    // openLibrary returns the cached snapshot immediately. Directory scanning
    // and weak-network recognition continue in the detached native pipeline.
    snapshot = await invoke('openLibrary');
    $('#entry-view').classList.add('hidden');
    $('#library-view').classList.remove('hidden');
    renderEntry();
    renderLibrary();
    clearNotice(libraryNotice);
    scheduleLibraryFocusRepair();
  } catch (error) {
    showError(error, libraryNotice);
  }
}

$('#busy-cancel').addEventListener('click', cancelActiveTask);
$('#open-library-button').addEventListener('click', openLibraryView);
$('#back-entry-button').addEventListener('click', () => { closeEdit(); invoke('closeWorkspace').catch(error => showError(error, libraryNotice)); });
$('#refresh-artwork-button').addEventListener('click', startNonBlockingRefresh);
$('#page-select-all').addEventListener('click', () => setPageSelection('all'));
$('#page-invert-selection').addEventListener('click', () => setPageSelection('invert'));
$('#directory-settings-toggle').addEventListener('click', () => { const panel = $('#directory-settings-panel'); if (!panel) return; const open = panel.classList.toggle('hidden') === false; if (open) closeLibraryMenuPanels('directory-settings-panel'); $('#directory-settings-toggle').setAttribute('aria-expanded', String(open)); });
$('#manual-game-settings-toggle').addEventListener('click', () => { const panel = $('#manual-game-panel'); if (!panel) return; const open = panel.classList.toggle('hidden') === false; if (open) closeLibraryMenuPanels('manual-game-panel'); $('#manual-game-settings-toggle').setAttribute('aria-expanded', String(open)); });
async function pickAndAddLibraryPath(command, addCommand, busyTitle, busyDetail, successMessage) { try { const result = await invoke(command); if (result?.cancelled || !result?.path) return; await runBusy(addCommand, busyTitle, result.path, { path: result.path }, successMessage); } catch (error) { showError(error, libraryNotice); } }
const scanLibraryButton = document.createElement('button');
scanLibraryButton.id = 'scan-library-button';
scanLibraryButton.type = 'button';
scanLibraryButton.className = 'button steam menu-panel-action';
scanLibraryButton.textContent = '扫描游戏库';
$('#pick-root-button')?.before(scanLibraryButton);
let scanLibraryRequestInFlight = false;
scanLibraryButton.addEventListener('click', async () => {
  if (scanLibraryRequestInFlight) return;
  scanLibraryRequestInFlight = true;
  scanLibraryButton.disabled = true;
  showNotice('正在扫描游戏库；页面保持可操作。', false, libraryNotice);
  try {
    snapshot = await invoke('scan');
    renderEntry();
    renderLibrary();
  } catch (error) {
    showError(error, libraryNotice);
  } finally {
    scanLibraryRequestInFlight = false;
    scanLibraryButton.disabled = false;
  }
});
$('#pick-root-button').addEventListener('click', () => pickAndAddLibraryPath('pickDirectory', 'addRoot', '正在添加扫描目录', '正在保存所选文件夹并扫描游戏库。', '扫描目录已保存。'));
$('#pick-game-button').addEventListener('click', () => pickAndAddLibraryPath('pickExecutable', 'addGame', '正在手动加入游戏', '正在读取所选 EXE 所在的游戏目录。', '游戏已加入，自动规则不会覆盖。'));
async function commitSelectedGames(forceCloseSteam = false, steamExecutable = '') {
  const target = selectedTargetArguments();
  if (forceCloseSteam) {
    target.forceCloseSteam = true;
    target.restartSteamBigPicture = true;
    if (steamExecutable) target.steamExecutable = steamExecutable;
  }
  await runBusy('commit', '正在事务写入 Steam', forceCloseSteam
    ? '正在关闭 Steam、备份并写入 shortcuts.vdf，完成后自动重启 Steam 大屏。'
    : '逐账户备份、暂存、写入并重新解析 shortcuts.vdf。', target,
    forceCloseSteam ? '已加入 Steam；快捷方式已校验，Steam 大屏正在重新启动。' : '已加入 Steam；快捷方式已校验。');
}
async function requestSteamCommit() {
  const count = selectedReadyCount();
  if (!count) return showNotice('当前页面没有已验证且已选中的项目。', true, libraryNotice);
  try {
    const preflight = await invoke('steamCommitPreflight');
    if (preflight?.steamRunning) {
      const accepted = await askConfirm('Steam 正在运行',
        '加入 Steam 需要暂时关闭 Steam 才能安全写入 shortcuts.vdf。\n\n确认后将强行关闭 Steam，完成加入、备份和校验，然后自动重启 Steam 大屏。',
        '强行关闭并加入');
      if (accepted) await commitSelectedGames(true, preflight.steamPath || '');
      return;
    }
    $('#commit-summary').textContent = `将把 ${count} 个已验证项目同步到全部 Steam 大屏用户。`;
    $('#commit-target').textContent = '目标：所有已发现的 Steam 用户（逐账户事务写入）';
    $('#steam-commit-modal').classList.remove('hidden');
  } catch (error) { showError(error, libraryNotice); }
}
$('#commit-button').addEventListener('click', requestSteamCommit);
$('#steam-commit-close').addEventListener('click', () => $('#steam-commit-modal').classList.add('hidden')); $('#steam-commit-cancel').addEventListener('click', () => $('#steam-commit-modal').classList.add('hidden')); $('#steam-commit-confirm').addEventListener('click', async () => {
  $('#steam-commit-modal').classList.add('hidden');
  try {
    const preflight = await invoke('steamCommitPreflight');
    if (preflight?.steamRunning) {
      const accepted = await askConfirm('Steam 正在运行',
        'Steam 在确认期间重新启动了。需要暂时关闭 Steam 才能继续写入；完成后会自动重启 Steam 大屏。',
        '强行关闭并加入');
      if (!accepted) return;
      await commitSelectedGames(true, preflight.steamPath || '');
    } else await commitSelectedGames(false);
  } catch (error) { showError(error, libraryNotice); }
});
$('#delete-button').addEventListener('click', () => { const count = selectedSteamCount(); if (!count) return showNotice('当前页面没有已选中的 Steam 快捷方式。', true, libraryNotice); $('#delete-summary').textContent = `将从全部 Steam 大屏用户中删除 ${count} 个明确匹配的快捷方式。`; $('#steam-delete-modal').classList.remove('hidden'); });
$('#steam-delete-close').addEventListener('click', () => $('#steam-delete-modal').classList.add('hidden')); $('#steam-delete-cancel').addEventListener('click', () => $('#steam-delete-modal').classList.add('hidden')); $('#steam-delete-confirm').addEventListener('click', async () => { $('#steam-delete-modal').classList.add('hidden'); await runBusy('deleteFromSteam', '正在事务删除 Steam 快捷方式', '逐账户备份、暂存、删除并校验 shortcuts.vdf。', { ...selectedTargetArguments(), selectionMode: 'all-steam-accounts', selectedGameDirectories: [...(selectedSteamDirectories === null ? new Set(defaultSelectedSteamDirectories()) : selectedSteamDirectories)] }); });
document.querySelectorAll('.page-tab').forEach(tab => tab.addEventListener('click', () => { closeLibraryMenuPanels(); activeTab = tab.dataset.tab; renderLibrary(); }));
$('#edit-back-button').addEventListener('click', closeEdit); $('#edit-folder').addEventListener('click', () => selectedActionGame && invoke('openFolder', { path: selectedActionGame.gameDirectory }).catch(error => showError(error, libraryNotice))); $('#artwork-cancel')?.addEventListener('click', cancelArtworkEditor); $('#artwork-save')?.addEventListener('click', confirmArtworkDraft); document.querySelectorAll('.artwork-pane').forEach(pane => { const type = pane.dataset.artworkType; const preview = pane.querySelector('.artwork-dropzone'); preview?.addEventListener('click', () => openArtworkActionMenu(type)); preview?.addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); event.stopPropagation(); openArtworkActionMenu(type); } }); });
$('#artwork-action-close').addEventListener('click', () => closeArtworkActionMenu(true)); $('#artwork-action-search').addEventListener('click', () => artworkActionForCurrentType('search')); $('#artwork-action-replace').addEventListener('click', () => artworkActionForCurrentType('replace')); $('#artwork-action-delete').addEventListener('click', () => artworkActionForCurrentType('delete'));
$('#artwork-search-close').addEventListener('click', cancelArtworkTask); $('#artwork-search-cancel').addEventListener('click', cancelArtworkTask); $('#identity-search-cancel').addEventListener('click', cancelIdentitySearch);
$('#edit-primary-select').addEventListener('change', event => { if (!selectedActionGame || !event.target.value) return; selectedActionGame.primaryExecutable = event.target.value; editDraft.primaryDirty = true; renderEditDraft(selectedActionGame); showNotice('主程序已暂存；点击“保存内容”才会写入配置。', false, libraryNotice); });
$('#edit-bucket-select').addEventListener('change', event => { if (!selectedActionGame) return; editDraft.bucket = event.target.value; setBucketOptions(editDraft.bucket); renderEditDraft(selectedActionGame); showNotice('页面归类已暂存；点击“保存内容”才会写入配置。', false, libraryNotice); });
$('#identity-form').addEventListener('submit', async event => { event.preventDefault(); syncIdentityDraft(); await searchGame(); }); $('#identity-name-candidates').addEventListener('change', event => { const selected = event.target.selectedIndex > 0 ? event.target._nameRows?.[event.target.selectedIndex - 1] : null; if (selected?.name) { $('#identity-name').value = selected.name; if (selected.steamAppId) $('#identity-steam-id').value = String(selected.steamAppId); syncIdentityDraft(); renderEditDraft(selectedActionGame); $('#identity-name').focus(); } event.target.value = ''; }); $('#identity-steam-id-clear').addEventListener('click', async () => { if (!selectedActionGame?.primaryExecutable) return; if (editDraft) { steamIdClearPending = true; $('#identity-steam-id').value = ''; syncIdentityDraft(); renderEditDraft(selectedActionGame); showNotice('Steam AppID 已清空并暂存；点击“保存内容”才会写入配置。', false, libraryNotice); return; } steamIdClearPending = true; $('#identity-steam-id').value = ''; try { snapshot = await invoke('clearManualSteamId', { executable: selectedActionGame.primaryExecutable }); const refreshed = mergedGames().find(item => normalizedUiPath(item.primaryExecutable) === normalizedUiPath(selectedActionGame.primaryExecutable)); if (refreshed) { selectedActionGame = refreshed; openEdit(refreshed); } else { renderEntry(); if (!$('#library-view').classList.contains('hidden')) renderLibrary(); } showNotice('Steam AppID 已清空；已锁定为不自动恢复，点击保存内容可继续保存其他修改。', false, libraryNotice); } catch (error) { steamIdClearPending = false; showError(error, libraryNotice); } }); $('#identity-name').addEventListener('input', () => { syncIdentityDraft(); renderEditDraft(selectedActionGame); }); $('#identity-steam-id').addEventListener('input', () => { syncIdentityDraft(); renderEditDraft(selectedActionGame); }); $('#edit-save-content').addEventListener('click', async () => { if (!selectedActionGame?.primaryExecutable) return; if (editDraft) { await persistEditDraft(); return; } });
$('#confirm-cancel').addEventListener('click', () => closeConfirm(false)); $('#confirm-confirm').addEventListener('click', () => closeConfirm(true)); $('#confirm-close').addEventListener('click', () => closeConfirm(false));
function routeWorkspaceKey(event) {
  // Native form controls must keep Enter/Space/arrows for text entry,
  const isBack = event.key === 'Escape' || event.key === 'Esc' || event.code === 'Escape';
  // selection and browser control behavior. Escape/B remains a deliberate
  // workspace back/cancel action even while an input has focus. Also stop any
  // event already consumed by a card or modal control from reaching the global
  // router.
  const semanticWorkspaceAction = event.workspaceAction === true;
  if (event.defaultPrevented || (!isBack && !semanticWorkspaceAction && isFormInputTarget(event.target || document.activeElement))) return;
  if (isBack) {
    event.preventDefault();
    const isControllerBack = semanticWorkspaceAction;
    if (isControllerBack) {
      const now = performance.now();
      const isDouble = controllerBackLastAt > 0 && now - controllerBackLastAt <= CONTROLLER_B_DOUBLE_MS;
      if (controllerBackResetTimer) window.clearTimeout(controllerBackResetTimer);
      if (isDouble) {
        controllerBackLastAt = 0;
        controllerBackResetTimer = 0;
        // closeWorkspace is the same native close path used by the header.
        // WM_CLOSE restores and focuses YeManCC in parent mode.
        invoke('closeWorkspace').catch(() => {});
        return;
      }
      controllerBackLastAt = now;
      const token = now;
      controllerBackResetTimer = window.setTimeout(() => {
        if (controllerBackLastAt === token) controllerBackLastAt = 0;
        controllerBackResetTimer = 0;
      }, CONTROLLER_B_DOUBLE_MS + 40);
    }
    if (closeWorkspaceDropdown(true)) return;
    if (closeArtworkActionMenu(true)) return;
    if (!busy.classList.contains('hidden')) return cancelActiveTask();
    if (!$('#identity-search-modal').classList.contains('hidden')) return cancelIdentitySearch();
    if (!$('#artwork-search-modal').classList.contains('hidden')) return cancelArtworkTask();
    if (!$('#edit-modal').classList.contains('hidden')) return closeEdit();
    if (!$('#confirm-modal').classList.contains('hidden')) return closeConfirm(false);
    if (!$('#steam-commit-modal').classList.contains('hidden')) return $('#steam-commit-modal').classList.add('hidden');
    if (!$('#steam-delete-modal').classList.contains('hidden')) return $('#steam-delete-modal').classList.add('hidden');
    const hadLibraryPanel = !$('#directory-settings-panel')?.classList.contains('hidden') || !$('#manual-game-panel')?.classList.contains('hidden');
    if (hadLibraryPanel) { closeLibraryMenuPanels(); return; }
    // At the library root a single controller B is deliberately a no-op.
    // The second B inside the same window is handled above as the explicit
    // close gesture; keyboard Escape keeps its existing direct-close behavior.
    if (!$('#library-view').classList.contains('hidden')) {
      closeEdit();
      if (!isControllerBack) invoke('closeWorkspace').catch(() => {});
      return;
    }
  }
  if (handleModalGamepadKey(event)) return;
  if (handleLibraryGamepadKey(event)) return;
  handleEntryGamepadKey(event);
}
function routeWorkspaceAction(action) {
  const actionName = String(action || '');
  if (['navigate-left', 'navigate-right', 'navigate-up', 'navigate-down'].includes(actionName)) {
    const now = performance.now();
    if (actionName === lastDirectionalAction && now - lastDirectionalActionAt < DIRECTIONAL_ACTION_DEDUPE_MS) return true;
    lastDirectionalAction = actionName;
    lastDirectionalActionAt = now;
  }
  const key = {
    'navigate-left': 'ArrowLeft',
    'navigate-right': 'ArrowRight',
    'navigate-up': 'ArrowUp',
    'navigate-down': 'ArrowDown',
    'accept': 'Enter',
    'back': 'Escape',
    'edit': 'x'
  }[actionName];
  if (!key) {
    if (action === 'tab-previous' || action === 'tab-next') {
      if (!$('#library-view').classList.contains('hidden') && !libraryModalOpen()) cycleLibraryTab(action === 'tab-next' ? 1 : -1);
      return true;
    }
    return false;
  }
  const event = {
    key,
    code: key === 'Escape' ? 'Escape' : key,
    workspaceAction: true,
    preventDefault() {},
    stopPropagation() {}
  };
  routeWorkspaceKey(event);
  return true;
}
window.addEventListener('workspace-action', event => routeWorkspaceAction(event.detail));
window.addEventListener('keydown', routeWorkspaceKey);
let refreshAfterScrapeTimer = 0;
function scheduleSilentRefresh() {
  if (refreshAfterScrapeTimer) return;
  refreshAfterScrapeTimer = window.setTimeout(async () => {
    refreshAfterScrapeTimer = 0;
    try {
      snapshot = await invoke('state');
      renderEntry();
      if (!$('#library-view').classList.contains('hidden')) renderLibrary();
    } catch (error) {
      showNotice(error?.message || String(error), true, libraryNotice);
    }
  }, 250);
}
window.chrome.webview.addEventListener('message', event => {
  const message = event.data || {};
  if (message.kind === 'response') {
    if (message.id === 0) {
      if (message.ok && message.result) {
        snapshot = message.result;
        renderEntry();
        if (!$('#library-view').classList.contains('hidden')) renderLibrary();
      } else if (!message.ok) showError(message.error);
      return;
    }
    const item = pending.get(message.id);
    if (!item) return;
    pending.delete(message.id);
    message.ok ? item.resolve(message.result) : item.reject(new Error(message.error || '操作失败'));
    return;
  }
  if (message.scrape) {
    snapshot.scrape = message.scrape;
    // Background progress must not rebuild the card DOM on every tick. The
    // progress controls update below, while terminal events schedule one
    // coalesced snapshot refresh; this preserves the active gamepad focus.
  }
  if (message.event === 'library-first-run-started') {
    showNotice('正在自动扫描游戏库；仅首次打开时执行，页面保持可操作。', false, libraryNotice);
  } else if (message.event === 'library-first-run-complete') {
    if (message.snapshot) snapshot = message.snapshot;
    renderEntry();
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
    showNotice(`首次自动扫描完成${Number.isFinite(Number(message.rootCount)) ? `，发现 ${Number(message.rootCount)} 个游戏库目录` : ''}。`, false, libraryNotice);
  } else if (message.event === 'library-first-run-error') {
    showNotice(message.error || '首次自动扫描未完成，可手动点击“扫描游戏库”重试。', true, libraryNotice);
  } else if (message.event === 'library-scan-complete') {
    if (message.snapshot) snapshot = message.snapshot;
    renderEntry();
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
    showNotice('目录扫描已完成；页面保持可操作。', false, libraryNotice);
  } else if (message.event === 'library-plan-refreshed') {
    // The local scan and Steam plan are intentionally separate phases.  A
    // slow/disconnected Steam install may delay this event, but must never
    // delay the library page or consume foreground input.
    if (message.snapshot) snapshot = message.snapshot;
    renderEntry();
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
  } else if (message.event === 'scrape-queue-started') {
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      manualRefreshProgress = {
        phase: 'running',
        current: 0,
        total: Number(message.total || message.scrape?.total || 0)
      };
      renderArtworkRefreshControl();
    }
    clearNotice(libraryNotice);
  } else if (message.event === 'scrape-progress') {
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      const current = Number(message.current);
      const total = Number(message.total);
      manualRefreshProgress = {
        phase: message.status === 'paused' ? 'paused' : 'running',
        current: Number.isFinite(current) ? current : manualRefreshProgress.current,
        total: Number.isFinite(total) ? total : manualRefreshProgress.total
      };
      renderArtworkRefreshControl();
    }
    if (message.status === 'ready' || message.status === 'failed') scheduleSilentRefresh();
  } else if (message.event === 'scrape-complete') {
    setBusy(false);
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      const total = Number(message.total);
      const current = Number(message.current);
      manualRefreshProgress = {
        phase: 'completed',
        current: Number.isFinite(current) ? current : manualRefreshProgress.current,
        total: Number.isFinite(total) ? total : manualRefreshProgress.total
      };
      renderArtworkRefreshControl();
    }
    scheduleSilentRefresh();
    showNotice(String(message.scrape?.mode || '') === 'manual-refresh' ? '手动刷新已完成' : '后台更新已完成。', false, libraryNotice);
  } else if (message.event === 'scrape-paused') {
    setBusy(false);
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      manualRefreshProgress = {
        phase: 'paused',
        current: Number.isFinite(Number(message.current)) ? Number(message.current) : manualRefreshProgress.current,
        total: Number.isFinite(Number(message.total)) ? Number(message.total) : manualRefreshProgress.total
      };
      renderArtworkRefreshControl();
    }
    scheduleSilentRefresh();
    showNotice(String(message.scrape?.mode || '') === 'manual-refresh' ? '手动刷新已暂停；已完成项目保留。' : '识别任务已停止；已完成项目保留。', false, libraryNotice);
  } else if (message.event === 'library-open-cancelled') {
    scheduleSilentRefresh();
    showNotice('打开游戏库的后台扫描已取消；当前页面仍可继续操作。', false, libraryNotice);
  } else if (message.event === 'manual-identify-started') {
    manualIdentifyEventMatches(message);
    showNotice('名称与 ID 已保存，正在后台补齐 Steam/IGDB 身份和素材；页面保持可操作。', false, libraryNotice);
  } else if (message.event === 'manual-identify-complete') {
    if (!manualIdentifyEventMatches(message)) return;
    scheduleSilentRefresh();
    if (!editDraft && selectedActionGame && normalizedUiPath(selectedActionGame.primaryExecutable) === normalizedUiPath(message.executable || '')) {
      const refreshed = mergedGames().find(item => normalizedUiPath(item.primaryExecutable) === normalizedUiPath(message.executable || ''));
      if (refreshed) {
        selectedActionGame = refreshed;
        $('#edit-title').textContent = gameDisplayName(refreshed);
        $('#artwork-search-hint').textContent = `搜索词：${artworkSearchQuery(refreshed)}（优先产品名称，其次 EXE）`;
        renderArtworkDraft();
      }
    }
    showNotice(`手动识别后台补齐完成（${message.attempts || 1} 次尝试）。`, false, libraryNotice);
  } else if (message.event === 'manual-identify-failed') {
    if (!manualIdentifyEventMatches(message)) return;
    scheduleSilentRefresh();
    showNotice(message.cancelled ? '已暂停后台手动识别；名称与 ID 保留，可稍后再次搜索。' : '网络补齐暂未完成；名称与 ID 已保留，可稍后再次搜索。', false, libraryNotice);
  } else if (message.event === 'manual-identify-busy') {
    showNotice('后台识别任务正在合并，当前编辑仍可继续。', false, libraryNotice);
  } else if (message.event === 'identity-search-started') {
    if (!identityEventMatches(message)) return;
    if (message.jobId) identitySearchJobId = message.jobId;
    identitySearchInFlight = true;
    showNotice('候选名称正在后台搜索；结果会自动填入下拉菜单。', false, libraryNotice);
  } else if (message.event === 'identity-search-complete') {
    if (!identityEventMatches(message)) return;
    identitySearchInFlight = false;
    identitySearchJobId = '';
    stopIdentitySearchLoading();
    if (selectedActionGame && normalizedUiPath(selectedActionGame.primaryExecutable) === normalizedUiPath(message.executable || '')) {
      searchReturnToEdit = false;
      renderSearchCandidates({ ...selectedActionGame, directoryName: message.query || gameDisplayName(selectedActionGame), _searchQuery: message.query || '' }, message.candidates || [], message.query || '');
    }
  } else if (message.event === 'identity-search-failed') {
    if (!identityEventMatches(message)) return;
    identitySearchInFlight = false;
    identitySearchJobId = '';
    stopIdentitySearchLoading();
    if (message.cancelled) {
      $('#identity-search-modal').classList.add('hidden');
      if (selectedActionGame) openEdit(selectedActionGame);
    } else {
      $('#identity-search-list').innerHTML = '<div class="identity-search-empty">候选名称暂未返回，可再次点击自动识别。</div>';
      showNotice('自动识别暂未完成，原页面保持不变，可稍后重试。', false, libraryNotice);
    }
  } else if (message.event === 'scrape-error' || message.event === 'library-open-error') {
    if (message.event === 'scrape-error') setBusy(false);
    if (message.event === 'scrape-error' && String(message.scrape?.mode || '') === 'manual-refresh') {
      manualRefreshProgress = { phase: 'failed', current: manualRefreshProgress.current, total: manualRefreshProgress.total };
      renderArtworkRefreshControl();
    }
    showNotice(message.error || '后台任务失败', true, libraryNotice);
  } else if (message.event === 'network-wake-started') {
    showNotice('网络恢复任务已后台启动，页面保持可操作。', false, libraryNotice);
  } else if (message.event === 'network-wake-complete') {
    scheduleSilentRefresh();
    showNotice(message.planPending ? '网络恢复已完成，计划正在后台刷新。' : '网络恢复任务已完成。', false, libraryNotice);
  } else if (message.event === 'network-plan-refreshed') {
    if (message.snapshot) snapshot = message.snapshot;
    renderEntry();
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
  } else if (message.event === 'network-wake-cancelled') {
    scheduleSilentRefresh();
    showNotice('网络恢复已取消；当前缓存和页面状态保留，可稍后重试。', false, libraryNotice);
  } else if (message.event === 'network-wake-error') {
    showNotice(message.error || '网络恢复任务暂未完成，可稍后重试。', false, libraryNotice);
  } else if (message.event === 'directory-change-detected') clearNotice(libraryNotice);
  else if (message.event === 'network-restored') clearNotice(libraryNotice);
  else if (message.event === 'host-error') showNotice(message.error || '宿主错误', true);
});

const updateSteamArtworkButton = (() => {
  const button = document.createElement('button');
  button.id = 'update-steam-artwork-button';
  button.type = 'button';
  button.className = 'button ghost hidden';
  button.innerHTML = `更新Steam图标 <span class="count-label">${formatSelectedCount(0)}</span>`;
  $('#commit-button')?.before(button);
  return button;
})();
function syncUpdateSteamArtworkButton() {
  if (!updateSteamArtworkButton) return;
  const visible = activeTab === 'in-steam';
  const count = selectedSteamCount();
  updateSteamArtworkButton.classList.toggle('hidden', !visible);
  updateSteamArtworkButton.disabled = !visible || count === 0;
  updateSteamArtworkButton.innerHTML = `更新Steam图标 <span class="count-label">${formatSelectedCount(count)}</span>`;
}
const baseRenderLibrary = renderLibrary;
renderLibrary = function () { baseRenderLibrary(); syncUpdateSteamArtworkButton(); };
updateSteamArtworkButton.addEventListener('click', async () => {
  const selected = [...(selectedSteamDirectories === null ? new Set(defaultSelectedSteamDirectories()) : selectedSteamDirectories)];
  if (!selected.length) return showNotice('请先选择已加入 Steam 的游戏。', true, libraryNotice);
  try {
    const preflight = await invoke('steamCommitPreflight');
    let forceCloseSteam = false;
    if (preflight?.steamRunning) {
      const accepted = await askConfirm('Steam 正在运行',
        '更新 Steam 图标需要暂时关闭 Steam，才能让新的封面和壁纸被可靠读取。\n\n确认后将强行关闭 Steam，更新并校验素材文件，然后自动重启 Steam 大屏。',
        '强行关闭并更新');
      if (!accepted) return;
      forceCloseSteam = true;
    }
    const target = { selectionMode: 'all-steam-accounts', selectedGameDirectories: selected };
    if (forceCloseSteam) { target.forceCloseSteam = true; target.restartSteamBigPicture = true; target.steamExecutable = preflight?.steamPath || ''; }
    await runBusy('refreshSteamArtwork', '正在更新 Steam 图标', forceCloseSteam
      ? '正在关闭 Steam、备份并更新封面/壁纸，完成后自动重启 Steam 大屏。'
      : '正在备份并更新已选择游戏的 Steam 封面/壁纸。', target,
      forceCloseSteam ? 'Steam 图标已更新并校验；Steam 大屏正在重新启动。' : 'Steam 图标已更新并校验。');
  } catch (error) { showError(error, libraryNotice); }
});

// Build the visible editor dropdowns before the first startup snapshot so a
// fast controller A/方向键 press cannot land on a native select during the
// initial render race.
upgradeEditDropdowns();

// Startup is a fast local snapshot refresh, not a user-cancellable task. Keep
// it invisible and non-blocking; only explicit refresh/add/search operations
// use the busy overlay and B/Escape cancellation path.
invoke('startup').then(async result => {
  snapshot = result;
  renderEntry();
  await openLibraryView();
}).catch(showError);

