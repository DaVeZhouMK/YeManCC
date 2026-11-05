const pending = new Map();
let requestId = 1;
let snapshot = { config: {}, manualOverrides: { items: {} }, library: { games: [] }, steamPlan: { items: [], summary: {} } };
let activeTab = 'not-in-steam';
let selectedSteamTarget = { accountId: '', steamRoot: '' };
let steamRuntimeState = { running: null, steamPath: '', checking: false };
let steamRuntimeRequest = null;
// Cached userdata discovery is not proof of the current Steam login state.
// Keep fresh account detection independent of slow game scans and snapshots.
let steamAccountDiscovery = null;
let steamAccountRequest = null;
let steamAccountLastCheck = 0;
const STEAM_ACCOUNT_RECHECK_MS = 5000;
let steamRuntimeLastCheck = 0;
let steamRuntimeWatchTimer = 0;
let steamRuntimeWatchClosed = false;
let steamRuntimeWatchForeground = false;
let selectedGameDirectories = null;
let selectedSteamDirectories = null;
let selectedCategoryDirectories = { unclassified: new Set(), 'non-game': new Set() };
let selectedActionGame = null;
let editPresentationEpoch=0;
let editorArtworkRenderPending=false;
let nativeArtworkRefreshOwner=null;
let pendingSteamCommitArguments = null;
let steamCommitRequestInFlight = false;
let editDraft = null;
let confirmResolver = null;
let confirmReturnFocus = null;
let busyReturnFocus = null;
let artworkDraft = { cover: null, long: null, wallpaper: null, dirty: { cover: false, long: false, wallpaper: false } };
let artworkPageBase = { cover: null, long: null, wallpaper: null };
let artworkSearchType = "cover";
let artworkSearchSlot = "cover";
let artworkActionType = "cover";
let artworkCandidates = [];
const artworkUnavailableUrls = new Set();
let downloadingCandidateUrl = "";
let artworkTaskToken = 0;
let artworkTaskInFlight = false;
const ARTWORK_PREFETCH_TTL_MS = 5 * 60 * 1000;
const ARTWORK_PREFETCH_MAX_PRELOADS = 16;
const ARTWORK_PREFETCH_MAX_SETTLED = 96;
const ARTWORK_PREFETCH_RETRY_BASE_MS = 30 * 1000;
const ARTWORK_SEARCH_TIMEOUT_MS = 185 * 1000;
const artworkPrefetchCache = new Map();
const artworkPreviewWarmups = new Map();
let artworkPrefetchPendingCount = 0;
let cancelInFlight = false;
let searchReturnToEdit = false;
let identitySearchInFlight = false;
let steamIdClearPending = false;
let identitySearchCancelled = false;
let editSaveInFlight = false;
const editorSaveTasks = new Map();
const steamCommitTasks = new Map();
function activeCardOperation(game) {
  const key=typeof game==='string'?game:keyFor(game||{});
  return editorSaveTasks.get(key)||steamCommitTasks.get(key);
}
function renderEditorOperationState() {
  if(!selectedActionGame)return;
  const key=keyFor(selectedActionGame),saving=editorSaveTasks.has(key),steam=steamCommitTasks.get(key);
  const save=$('#edit-save-content');if(save){save.disabled=saving;save.textContent=saving?'正在保存…':'保存内容';}
  const join=$('#edit-add-to-steam');if(join){join.disabled=Boolean(selectedActionGame.mediaOnly)||isNativeSteamGame(selectedActionGame)||!selectedActionGame.primaryExecutable||!key||['queued','running'].includes(steam?.phase);join.textContent=steam?.phase==='queued'?'排队加入中…':steam?.phase==='running'?'正在加入…':'加入 Steam';}
  decorateLibrarySteamButton(join);
  const operation=activeCardOperation(key);if(operation){const badge=$('#edit-status');badge.textContent=operation.label;badge.className='badge '+(operation.phase==='failed'?'warning':'');badge.title=operation.label;}
}
let cardOperationFrame=0,cardOperationPending=false,cardOperationEditorPending=false;
function suspendCardOperationState(){
  if(cardOperationFrame)cancelAnimationFrame(cardOperationFrame);
  cardOperationFrame=0;
}
function refreshCardOperationState(includeEditor=true) {
  cardOperationPending=true;cardOperationEditorPending||=includeEditor;
  suspendCardOperationState();
  if(!libraryRenderAllowed())return;
  cardOperationPending=false;
  syncScrapeCardStates();
  if(cardOperationEditorPending){
    cardOperationEditorPending=false;
    if(selectedActionGame&&!$('#edit-modal').classList.contains('hidden'))renderEditDraft(selectedActionGame);
  }
}
function scheduleCardOperationState(includeEditor=false){
  cardOperationPending=true;cardOperationEditorPending||=includeEditor;
  if(!libraryRenderAllowed()||cardOperationFrame)return;
  cardOperationFrame=requestAnimationFrame(()=>{cardOperationFrame=0;refreshCardOperationState(false);});
}
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
let identitySearchLoadingWanted=false,identitySearchOwner=null,identitySearchFocusFrame=0;
let currentCardDirectory = '';
let editReturnFocusFrame=0;
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
let scrapeProgressToastActive = false;
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
document.querySelectorAll('.app-heading > div, .library-heading > div').forEach(heading => {
  if (heading.querySelector('.app-brand-subtitle')) return;
  const subtitle = document.createElement('small');
  subtitle.className = 'app-brand-subtitle';
  subtitle.textContent = 'YeManCC附带程序';
  const title = heading.querySelector('h1');
  if (title) title.insertAdjacentElement('afterend', subtitle);
});
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
document.addEventListener('pointerdown', event => { closeWorkspaceDropdownAtBoundary(event.target); }, true);
window.addEventListener('resize', () => { if (openWorkspaceDropdownState) workspaceDropdownSetPosition(openWorkspaceDropdownState); });
window.addEventListener('scroll', () => { if (openWorkspaceDropdownState) workspaceDropdownSetPosition(openWorkspaceDropdownState); }, true);

// The editor is a fixed 840px design box; this ratio is its reference viewport
// (1080p host: 1040px work area - 100px window margin - 46px titlebar). The
// editor scales as one unit, so 1080p renders 1:1 and a 720p host scales to
// 0.64 instead of clipping or scrolling. Taller viewports stay at 1:1.
const EDITOR_DESIGN_VIEWPORT_HEIGHT = 932;
function syncEditorScale() {
  const zoom = Math.min(1, window.innerHeight / EDITOR_DESIGN_VIEWPORT_HEIGHT);
  document.documentElement.style.setProperty('--edit-zoom', String(zoom));
}
syncEditorScale();
window.addEventListener('resize', syncEditorScale);

// Host JSON is persisted/user data, not an instruction or a trusted shape.
function isWorkspaceRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function isBuiltinExcludedSteamTool(row, depth = 0) {
  if (!isWorkspaceRecord(row)) return false;
  const toolName = text => {
    if (typeof text !== 'string') return false;
    const key = text.toLowerCase().replace(/[^\p{L}\p{N}]/gu, '');
    if (['steamworkscommonredistributables', 'steamworksshared', 'unrealengine', 'unrealengine4', 'unrealengine5', 'unityhub', 'unityeditor', 'godotengine', 'godot', 'gamemaker', 'gamemakerstudio', 'rpgmaker', 'steamcmd', 'steamvr', 'steamlink', 'steamworkssdk', 'steamsdk', 'sourcesdk', 'sourcesdkbase', 'sourcefilmmaker', 'epicgameslauncher', 'proton', 'steamlinuxruntime'].includes(key)) return true;
    return (/^unrealengine[45]\d{1,3}$/.test(key) || /^ue[45]\d{1,3}$/.test(key) || /^steamlinuxruntime\d{1,3}$/.test(key) || /^proton\d{1,3}$/.test(key));
  };
  const toolId = value => typeof value === 'number' ? value === 228980 :
    typeof value === 'string' && /^0*228980$/.test(value.trim());
  for (const key of ['nativeSteamAppId', 'steamStoreAppId', 'storefrontAppId', 'appId', 'steamAppId', 'steamId', 'steam_appid']) if (toolId(row[key])) return true;
  for (const key of ['directoryName', 'formalName', 'displayName', 'name', 'primaryProductName', 'manifestName', 'manualName', 'query']) if (toolName(row[key])) return true;
  for (const key of ['gameDirectory', 'primaryExecutable', 'nativeSteamGameDirectory', 'nativeSteamExecutable', 'executable']) {
    if (typeof row[key] === 'string' && row[key].split(/[\\/]/).some(toolName)) return true;
  }
  return depth < 3 && ['nativeSteam', 'steamNativeRecord', 'steam', 'match', 'resolvedIdentity'].some(key => isBuiltinExcludedSteamTool(row[key], depth + 1));
}
function deduplicateWorkspaceExecutables(rows) {
  const pathKey = value => typeof value === 'string' ? value.trim().replace(/^"|"$/g, '').replaceAll('/', '\\').replace(/[\\]+$/, '').toLocaleLowerCase() : '';
  const rank = row => row.steamNative === true || row.contentType === 'steam-native' ? 100
    : ['already-in-steam', 'added-to-steam'].includes(row.status) ? 90
    : row.manualGameDirectoryLocked === true || row.discoverySource === 'manual' ? 80
    : row.steamVerificationStatus === 'steam-verified' ? 70
    : ['ready', 'ready-to-add'].includes(row.status) ? 60 : 0;
  const result = [], positions = new Map();
  for (const row of rows) {
    if (isBuiltinExcludedSteamTool(row)) continue;
    const key = pathKey(row.primaryExecutable);
    if (!key || !positions.has(key)) {
      if (key) positions.set(key, result.length);
      result.push(row); continue;
    }
    const index = positions.get(key), prior = result[index];
    const nextDir = pathKey(row.gameDirectory), oldDir = pathKey(prior.gameDirectory);
    if (rank(row) > rank(prior) || (rank(row) === rank(prior) &&
      (nextDir.length < oldDir.length || (nextDir.length === oldDir.length && nextDir < oldDir)))) result[index] = row;
  }
  return result;
}
function normalizeWorkspaceSnapshot(value, previous = {}) {
  if (!isWorkspaceRecord(value)) throw new Error('游戏库返回的数据格式无效；已保留当前列表。');
  const next = { ...value };
  const warnings = [];
  const record = (input, fallback, label) => {
    if (isWorkspaceRecord(input)) return { ...input };
    if (input !== undefined) warnings.push(label);
    return isWorkspaceRecord(fallback) ? { ...fallback } : {};
  };
  const rows = (input, fallback, label, validate) => {
    if (!Array.isArray(input)) {
      if (input !== undefined) warnings.push(label);
      return Array.isArray(fallback) ? fallback.map(row => ({ ...row })) : [];
    }
    const valid = input.filter(row => isWorkspaceRecord(row) && validate(row)).map(row => ({ ...row }));
    if (valid.length !== input.length) warnings.push(label);
    // An intentionally empty list is authoritative. If any row is malformed,
    // retain the last complete list instead of silently dropping that record.
    return valid.length !== input.length && Array.isArray(fallback) ? fallback.map(row => ({ ...row })) : valid;
  };
  next.config = record(value.config, previous.config, 'config');
  for (const key of ['roots', 'trainerRoots']) {
    if (next.config[key] !== undefined && !Array.isArray(next.config[key])) {
      warnings.push('config.' + key);
      next.config[key] = Array.isArray(previous.config?.[key]) ? previous.config[key] : [];
    } else if (Array.isArray(next.config[key])) next.config[key] = next.config[key].filter(item => typeof item === 'string');
  }
  next.library = record(value.library, previous.library, 'library');
  next.library.games = rows(next.library.games, previous.library?.games, 'library.games', row => typeof row.gameDirectory === 'string' && row.gameDirectory.length > 0).map(game => {
    for (const key of ['primaryExecutable', 'directoryName', 'status', 'contentType', 'classification']) {
      if (game[key] !== undefined && game[key] !== null && typeof game[key] !== 'string') {
        warnings.push('library.games.' + key); delete game[key]; game.status = 'data-invalid-review-required';
      }
    }
    return game;
  });
  next.steamPlan = record(value.steamPlan, previous.steamPlan, 'steamPlan');
  next.steamPlan.items = rows(next.steamPlan.items, previous.steamPlan?.items, 'steamPlan.items', row =>
    (typeof row.gameDirectory === 'string' && row.gameDirectory.trim().length > 0) ||
    (typeof row.primaryExecutable === 'string' && row.primaryExecutable.trim().length > 0)).map(item => {
    for (const key of ['gameDirectory', 'primaryExecutable', 'status', 'formalName', 'steamVerificationStatus']) {
      if (item[key] !== undefined && item[key] !== null && typeof item[key] !== 'string') {
        warnings.push('steamPlan.items.' + key); delete item[key]; item.status = 'data-invalid-review-required';
      }
    }
    return item;
  });
  next.library.games = deduplicateWorkspaceExecutables(next.library.games);
  for (const key of ['unscannedGames', 'missingGames']) {
    if (Array.isArray(next.library[key])) next.library[key] = next.library[key].filter(row => !isBuiltinExcludedSteamTool(row));
  }
  next.steamPlan.items = deduplicateWorkspaceExecutables(next.steamPlan.items);
  next.steamPlan.summary = record(next.steamPlan.summary, previous.steamPlan?.summary, 'steamPlan.summary');
  next.manualOverrides = record(value.manualOverrides, previous.manualOverrides, 'manualOverrides');
  next.manualOverrides.items = record(next.manualOverrides.items, previous.manualOverrides?.items, 'manualOverrides.items');
  next.resolvedIdentities = record(value.resolvedIdentities, previous.resolvedIdentities, 'resolvedIdentities');
  next.steamAccounts = record(value.steamAccounts, previous.steamAccounts, 'steamAccounts');
  next.steamAccounts.accounts = rows(next.steamAccounts.accounts, previous.steamAccounts?.accounts, 'steamAccounts.accounts', row => typeof row.accountId === 'string' && row.accountId.trim().length > 0);
  if (warnings.length) next.dataValidationWarnings = [...new Set(warnings)];
  return next;
}
function workspaceBridgeResult(command, result) {
  const snapshotCommands = new Set(['state', 'scan', 'prepare', 'openLibrary', 'plan', 'commit', 'saveEditor', 'deleteFromSteam', 'refreshSteamArtwork']);
  const isSnapshot = isWorkspaceRecord(result) && ('library' in result || 'steamPlan' in result || 'config' in result);
  if (!isSnapshot && snapshotCommands.has(command)) throw new Error('游戏库没有返回有效快照；已保留当前数据。');
  return isSnapshot ? normalizeWorkspaceSnapshot(result, snapshot) : result;
}

function invoke(command, arguments_ = {}, timeoutMs = 0) {
  if (command === 'cancelArtwork') retireArtworkSearchReads();
  else if (command === 'downloadArtworkCandidate' || command === 'mediaSearchVideos') retireArtworkSearchReads(arguments_);
  const id = requestId++;
  return new Promise((resolve, reject) => {
    const timer = timeoutMs > 0 ? window.setTimeout(() => {
      if (!pending.delete(id)) return;
      reject(new Error(command==='searchIdentity'?"搜索任务暂未响应，请稍后重试。":"下载接口暂未响应，状态会自动复查；请稍后重试。"));
    }, timeoutMs) : 0;
    pending.set(id, { resolve, reject, command, timer,
      artworkWorkerKey: command === 'searchArtwork' ? artworkWorkerKey(arguments_) : '' });
    try {
      window.chrome.webview.postMessage({ id, command, arguments: arguments_ });
    } catch (error) {
      pending.delete(id);
      if(timer) clearTimeout(timer);
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
function setBusy(visible, title = '正在处理', detail = '弱网环境下可能需要几分钟，请勿关闭窗口。') {
  const wasVisible = !busy.classList.contains('hidden');
  if (visible && !wasVisible) busyReturnFocus = document.activeElement;
  busy.classList.toggle('hidden', !visible);
  $('#busy-title').textContent = title;
  $('#busy-detail').textContent = detail;
  const cancel = $('#busy-cancel');
  if (cancel) { cancel.classList.toggle('hidden', !visible); cancel.disabled = cancelInFlight; cancel.textContent = '取消'; }
  if (visible && !wasVisible) focusWorkspaceElement(visibleFocusables(busy)[0], false);
  else if (!visible && wasVisible) {
    const trigger = busyReturnFocus;
    busyReturnFocus = null;
    if (trigger?.isConnected && !trigger.disabled && !trigger.closest('.hidden')) focusWorkspaceElement(trigger, false);
  }
}
async function cancelActiveTask() { if (identitySearchInFlight) return cancelIdentitySearch(); if (cancelInFlight || busy.classList.contains('hidden')) return; cancelInFlight = true; setBusy(true, '正在取消当前任务', '正在终止隔离 worker，不会删除游戏文件或真实 Steam 数据。'); try { await invoke('cancelTask'); } catch (error) { showError(error, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); cancelInFlight = false; setBusy(true, '正在处理', '取消请求失败，请稍后重试。'); } }
function pauseIdentitySearchLoading(){
  if(identitySearchEllipsisTimer)clearInterval(identitySearchEllipsisTimer);
  identitySearchEllipsisTimer=0;
}
function identitySearchOwnerCurrent(owner=identitySearchOwner){
  const modal=$('#identity-search-modal');
  return Boolean(modal?.isConnected&&owner&&owner===identitySearchOwner&&owner.token===identitySearchToken&&owner.epoch===silentRefreshEpoch&&!silentRefreshClosed&&selectedActionGame&&keyFor(selectedActionGame)===owner.key&&normalizedUiPath(selectedActionGame.primaryExecutable)===owner.executable&&!modal.classList.contains('hidden'));
}
function syncIdentitySearchLoading(){
  const owner=identitySearchOwner;let handle=0;
  const allowed=()=>identitySearchLoadingWanted&&identitySearchOwnerCurrent(owner)&&!owner.terminal&&libraryRenderAllowed();
  if(!allowed()){pauseIdentitySearchLoading();return;}
  if(identitySearchEllipsisTimer)return;
  const paint=()=>{if(identitySearchEllipsisTimer!==handle)return;if(!allowed()){pauseIdentitySearchLoading();return;}const dots=$('#identity-search-loading-dots'),text='。'.repeat((identitySearchEllipsisStep++%3)+1);if(dots&&dots.textContent!==text)dots.textContent=text;};
  paint();handle=window.setInterval(paint,450);identitySearchEllipsisTimer=handle;
}
function startIdentitySearchLoading(){
  identitySearchJobId='';identitySearchLoadingWanted=true;identitySearchEllipsisStep=0;pauseIdentitySearchLoading();
  $('#identity-search-loading')?.classList.remove('hidden');syncIdentitySearchLoading();
}
function stopIdentitySearchLoading(){
  identitySearchLoadingWanted=false;pauseIdentitySearchLoading();const loading=$('#identity-search-loading');if(loading&&!loading.classList.contains('hidden'))loading.classList.add('hidden');
}
function cancelIdentitySearchFocus(){if(identitySearchFocusFrame)cancelAnimationFrame(identitySearchFocusFrame);identitySearchFocusFrame=0;}
function requestIdentitySearchFocus(target,owner=identitySearchOwner){
  if(!owner||!target)return;owner.focusTarget=target;cancelIdentitySearchFocus();
  if(!libraryRenderAllowed()||!identitySearchOwnerCurrent(owner))return;
  identitySearchFocusFrame=requestAnimationFrame(()=>{
    identitySearchFocusFrame=0;if(!identitySearchOwnerCurrent(owner)||!libraryRenderAllowed()||activeWorkspaceModal()!==$('#identity-search-modal')||owner.focusTarget!==target||!target.isConnected||target.closest('.hidden'))return;
    owner.focusTarget=null;target.focus({preventScroll:true});
  });
}
function retireIdentitySearch(cancelNative=true){
  const active=identitySearchInFlight,executable=identitySearchOwner?.executable||identitySearchExecutable;
  if(identitySearchOwner?.deadline)clearTimeout(identitySearchOwner.deadline);
  identitySearchOwner=null;identitySearchToken++;identitySearchCancelled=true;identitySearchInFlight=false;identitySearchJobId='';searchReturnToEdit=false;identitySearchManualCandidate=null;
  cancelIdentitySearchFocus();stopIdentitySearchLoading();$('#identity-search-modal')?.classList.add('hidden');
  if(cancelNative&&active&&executable)invoke('cancelIdentitySearch',{executable}).catch(()=>{});
}
async function cancelIdentitySearch(){retireIdentitySearch();if(selectedActionGame)openEdit(selectedActionGame);}
function cancelEditorBackgroundTasks() {
  cancelArtworkPreviewWarmups();retireArtworkSearchPresentation(true);
  if(identitySearchOwner||identitySearchInFlight)retireIdentitySearch();
  if (artworkTaskInFlight || artworkPrefetchPendingCount > 0) {
    artworkTaskToken++;
    artworkTaskInFlight = false;
    downloadingCandidateUrl = '';
    invoke('cancelArtwork').catch(() => {});
  }
}
function presentIdentitySearch(owner){
  if(!identitySearchOwnerCurrent(owner)||!libraryRenderAllowed()||!owner.presentation)return;
  const next=owner.presentation;owner.presentation=null;
  stopIdentitySearchLoading();
  if(next.kind==='candidates'){
    if(owner.terminal)searchReturnToEdit=false;
    renderSearchCandidates({...selectedActionGame,directoryName:owner.query,_searchQuery:owner.query},next.candidates,owner.query);
    if(next.notice)showNotice(next.notice,false,libraryNotice);
  }else if(next.cancelled){
    retireIdentitySearch(false);if(selectedActionGame)openEdit(selectedActionGame);
  }else{
    searchReturnToEdit=false;$('#identity-search-list').innerHTML='<div class="identity-search-empty">候选名称暂未返回，可再次点击自动识别。</div>';
    if(next.error)showError(next.error,libraryNotice);else showNotice('自动识别暂未完成，原页面保持不变，可稍后重试。',false,libraryNotice);
  }
}
function finishIdentitySearch(owner,presentation){
  if(!identitySearchOwnerCurrent(owner))return;
  if(owner.deadline)clearTimeout(owner.deadline);owner.deadline=0;
  owner.terminal=true;owner.presentation=presentation;identitySearchInFlight=false;identitySearchJobId='';identitySearchLoadingWanted=false;pauseIdentitySearchLoading();
  presentIdentitySearch(owner);
}
function consumeIdentitySearchEvent(message){
  if(!identityEventMatches(message))return;
  const owner=identitySearchOwner;
  if(message.event==='identity-search-started'){
    if(libraryRenderAllowed())showNotice('候选名称正在后台搜索；结果会自动填入下拉菜单。',false,libraryNotice);
  }else if(message.event==='identity-search-complete'){
    finishIdentitySearch(owner,{kind:'candidates',candidates:Array.isArray(message.candidates)?message.candidates:[]});
  }else finishIdentitySearch(owner,{kind:'failed',cancelled:message.cancelled===true});
}
function syncEditorSearchActivity(){
  if(!libraryRenderAllowed()){
    pauseIdentitySearchLoading();cancelIdentitySearchFocus();cancelArtworkSearchFocus();cancelArtworkPreviewWarmups();return;
  }
  const owner=identitySearchOwner;
  if(identitySearchOwnerCurrent(owner)){
    presentIdentitySearch(owner);syncIdentitySearchLoading();
    if(owner.focusTarget)requestIdentitySearchFocus(owner.focusTarget,owner);
  }else pauseIdentitySearchLoading();
  presentArtworkSearch();const artOwner=artworkSearchPresentationOwner;if(artworkSearchPresentationCurrent(artOwner)&&artOwner.focusTarget)requestArtworkSearchFocus(artOwner.focusTarget,artOwner);
  resumeArtworkPreviewWarmups();
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
function setScrapeProgressToast(message, error = false) {
  const text = String(message ?? '').trim();
  if (!text) return;
  noticeQueue.length = 0;
  noticeSequence++;
  if (noticeTimer) window.clearTimeout(noticeTimer);
  noticeTimer = 0;
  noticeActive = true;
  scrapeProgressToastActive = true;
  toastNotice.textContent = text;
  toastNotice.classList.remove('hidden', 'error');
  if (error) toastNotice.classList.add('error');
}
function finishScrapeProgressToast(message, error = false) {
  if (!scrapeProgressToastActive) {
    showNotice(message, error, libraryNotice);
    return;
  }
  const text = String(message ?? '').trim();
  if (text) toastNotice.textContent = text;
  toastNotice.classList.toggle('error', Boolean(error));
  scrapeProgressToastActive = false;
  const sequence = ++noticeSequence;
  if (noticeTimer) window.clearTimeout(noticeTimer);
  noticeTimer = window.setTimeout(() => {
    if (sequence !== noticeSequence) return;
    toastNotice.classList.add('hidden');
    toastNotice.classList.remove('error');
    noticeActive = false;
    noticeTimer = 0;
    showNextNotice();
  }, NOTICE_DURATION_MS);
}
function clearNotice(target = notice) {
  if (target) target.classList.add('hidden');
  noticeQueue.length = 0;
  noticeSequence++;
  if (noticeTimer) window.clearTimeout(noticeTimer);
  noticeTimer = 0;
  noticeActive = false;
  scrapeProgressToastActive = false;
  toastNotice.classList.add('hidden');
  toastNotice.classList.remove('error');
}
function showError(error, target = notice) { showNotice(error?.message || String(error), true, target); }
function askConfirm(title, message, confirmLabel = '确认') { return new Promise(resolve => { confirmResolver = resolve; confirmReturnFocus = document.activeElement; $('#confirm-title').textContent = title; $('#confirm-message').textContent = message; $('#confirm-confirm').textContent = confirmLabel; $('#confirm-modal').classList.remove('hidden'); focusModalItem($('#confirm-modal')); }); }
function closeConfirm(result) { $('#confirm-modal').classList.add('hidden'); const resolve = confirmResolver; confirmResolver = null; const trigger = confirmReturnFocus; confirmReturnFocus = null; if (trigger?.isConnected && !trigger.disabled && !trigger.closest('.hidden')) focusWorkspaceElement(trigger); if (resolve) resolve(result); }
function steamAccounts() { return (steamAccountDiscovery || snapshot.steamAccounts)?.accounts || []; }
function ensureSteamTarget() {
  const accounts = steamAccounts();
  const current = accounts.find(item => item.accountId === selectedSteamTarget.accountId && (!selectedSteamTarget.steamRoot || item.steamRoot === selectedSteamTarget.steamRoot));
  if (current) selectedSteamTarget = { accountId: current.accountId, steamRoot: current.steamRoot || selectedSteamTarget.steamRoot };
  else if (accounts.length === 1) selectedSteamTarget = { accountId: accounts[0].accountId, steamRoot: accounts[0].steamRoot || '' };
  else if (accounts.length > 1) selectedSteamTarget = { accountId: '', steamRoot: '' };
}
function defaultSelectedDirectories() { return mergedGames().filter(game => isSteamImportReady(game) && category(game) === 'not-in-steam' && librarySidebarMatches(game)).map(keyFor); }
function defaultSelectedSteamDirectories() { return mergedGames().filter(game => category(game) === 'in-steam' && librarySidebarMatches(game)).map(keyFor); }
function selectedCommitDirectories() {
  if (activeTab === 'not-in-steam') {
    const selected = selectedGameDirectories === null ? new Set(defaultSelectedDirectories()) : selectedGameDirectories;
    return mergedGames().filter(game => category(game) === 'not-in-steam' && selected.has(keyFor(game)) && librarySidebarMatches(game)).map(keyFor);
  }
  if (activeTab === 'unclassified' || activeTab === 'non-game') {
    // Selection sets survive a background scan so a user does not lose an
    // in-progress choice. They must still be intersected with the current
    // inventory; otherwise a removed/renamed directory is sent to the native
    // worker as an unmatched selection and the whole commit is rejected.
    const available = new Set(mergedGames().filter(game => category(game) === activeTab && librarySidebarMatches(game)).map(keyFor));
    return [...(selectedCategoryDirectories[activeTab] || [])].filter(key => available.has(keyFor({ gameDirectory: key })));
  }
  return [];
}
function selectedTargetArguments(directories = selectedCommitDirectories()) { return { accountId: '', steamRoot: '', selectionMode: 'all-steam-accounts', selectedGameDirectories: [...directories] }; }

const steamReadyStatuses = new Set(['ready-to-add', 'ready-not-selected']);
const CATEGORY_ORDER = Object.freeze(['not-in-steam', 'in-steam', 'native-steam', 'unclassified', 'non-game']);
const CATEGORY_DEFINITIONS = Object.freeze({
  'not-in-steam': Object.freeze({ label: '等待加入', tone: 'good' }),
  'in-steam': Object.freeze({ label: '已加入', tone: 'steam' }),
  'native-steam': Object.freeze({ label: '原生Steam', tone: 'native-steam' }),
  unclassified: Object.freeze({ label: '需处理', tone: 'warn' }),
  'non-game': Object.freeze({ label: '不加入', tone: 'muted-badge' })
});
function ensureNativeSteamTab() {
  const tabs = document.querySelector('.page-tabs');
  if (!tabs || tabs.querySelector('[data-tab="native-steam"]')) return;
  const anchor = tabs.querySelector('[data-tab="in-steam"]');
  if (!anchor) return;
  const tab = document.createElement('button');
  tab.className = 'page-tab';
  tab.dataset.tab = 'native-steam';
  tab.type = 'button';
  tab.innerHTML = '原生Steam <strong id="count-native-steam">[0个]</strong>';
  tab.addEventListener('click', () => { closeLibraryMenuPanels(); activeTab = 'native-steam'; renderLibrary(); });
  anchor.after(tab);
}
function isSteamReady(game) {
  const steam = game?.steam || {};
  const id = String(steam.steamStoreAppId || steam.storefrontAppId || steam.appId || '');
  // Artwork readiness is separate from an officially verified Steam identity.
  // A numeric candidate alone still cannot classify the game as identified.
  return steamReadyStatuses.has(steam.status) || (steam.identityStatus === 'verified' &&
    steam.steamVerificationStatus === 'steam-verified' && /^[1-9]\d*$/.test(id) && Number(id) <= 2147483647);
}
function isSteamImportReady(game) {
  // Waiting-to-add is an explicit user queue. SteamID and artwork are
  // optional metadata and must never prevent selecting an otherwise scanned
  // game for import.
  return category(game) === 'not-in-steam';
}
function isNonGame(game) { return game.contentType === 'non-game' || game.status === 'unrecognized-tool' || game.unrecognizedTool; }
function keyFor(game) { return game?.mediaOnly ? `media:${game.mediaKey}` : normalizedUiPath(game?.gameDirectory || ''); }
function planMap() {
  const result = new Map();
  for (const item of snapshot.steamPlan?.items || []) {
    // Host and Worker persist the same Windows path through different JSON
    // generations (slash direction, case, and trailing separators can vary).
    // Use the same canonical UI key on both sides and index both identifiers;
    // an executable update must not hide a still-valid directory match.
    const executable = normalizedUiPath(item.primaryExecutable);
    const directory = normalizedUiPath(item.gameDirectory);
    if (executable) result.set(executable, item);
    if (directory) result.set(`dir:${directory}`, item);
  }
  return result;
}
function normalizedUiPath(value) { return String(value || '').replaceAll('/', '\\').replace(/[\\]+$/, '').toLocaleLowerCase(); }
// These indexes belong to one synchronous merge/render only. Source changes,
// nested snapshot views and exceptions cannot leave a long-lived stale cache.
let libraryPathLookups = null;
function withLibraryPathLookups(action){
  if(libraryPathLookups?.snapshot===snapshot)return action();
  const previous=libraryPathLookups;
  libraryPathLookups={snapshot,indexes:new Map()};
  try{return action();}finally{libraryPathLookups=previous;}
}
function libraryPathIndex(kind,items){
  if(libraryPathLookups?.snapshot!==snapshot)return null;
  const prior=libraryPathLookups.indexes.get(kind);
  if(prior?.source===items)return prior.values;
  const values=new Map();let order=0;
  for(const [stored,value] of Object.entries(items)){
    const keys=kind==='identities'?[normalizedUiPath(stored),normalizedUiPath(value?.executable)]:[normalizedUiPath(stored)];
    if(kind!=='buckets'||value?.bucket)for(const key of keys)if(!values.has(key))values.set(key,{value,order});
    ++order;
  }
  libraryPathLookups.indexes.set(kind,{source:items,values});return values;
}
function manualOverride(executable) {
  const key=normalizedUiPath(executable);if(!key)return {};
  const items=snapshot.manualOverrides?.items||{};if(items[key])return items[key];
  const index=libraryPathIndex('overrides',items);
  if(index)return index.get(key)?.value||{};
  const found=Object.entries(items).find(([stored])=>normalizedUiPath(stored)===key);return found?.[1]||{};
}
function resolvedIdentity(executable) {
  const key=normalizedUiPath(executable);if(!key)return {};
  const items=snapshot.resolvedIdentities||{};if(items[key])return items[key];
  const index=libraryPathIndex('identities',items);
  if(index)return index.get(key)?.value||{};
  const found=Object.entries(items).find(([stored,value])=>normalizedUiPath(stored)===key||normalizedUiPath(value?.executable)===key);return found?.[1]||{};
}
function executableLikeName(value) { return /\\.(exe|dll|bin|bat|cmd)$/i.test(String(value || '').trim()) || /^(setup|unins|launcher|start|game)\\d*$/i.test(String(value || '').trim()); }
function gameDisplayName(game) { const overrideName = game.override?.name; if (overrideName) return overrideName; const formal = game.resolvedIdentity?.formalName || game.steam?.formalName; if (formal && !executableLikeName(formal)) return formal; return game.primaryProductName || game.primaryFileDescription || game.steam?.suggestedName || game.directoryName || formal || '未命名项目'; }
const MANUAL_BUCKETS = new Set(['auto', 'not-in-steam', 'in-steam', 'native-steam', 'unclassified', 'non-game']);
function validManualBucket(value) { return MANUAL_BUCKETS.has(String(value || '')) ? String(value) : 'auto'; }
function manualBucket(game) {
  const buckets = snapshot.config?.manualBuckets || {};
  const directoryKey = keyFor(game);
  const executableKey = normalizedUiPath(game.primaryExecutable);
  const direct = buckets[directoryKey];
  if (direct?.bucket) return validManualBucket(direct.bucket);
  const executable = buckets[executableKey];
  if (executable?.bucket) return validManualBucket(executable.bucket);
  const index=libraryPathIndex('buckets',buckets);
  if(index){
    const directory=index.get(directoryKey),executable=index.get(executableKey);
    const first=directory&&executable?(directory.order<=executable.order?directory:executable):directory||executable;
    return validManualBucket(first?.value?.bucket);
  }
  const found = Object.entries(buckets).find(([stored, value]) =>
    (normalizedUiPath(stored) === directoryKey || normalizedUiPath(stored) === executableKey) && value?.bucket);
  return validManualBucket(found?.[1]?.bucket);
}
let libraryRenderGames = null;
let libraryRenderSnapshot = null;
function mergedGames() {
  if(libraryRenderGames&&libraryRenderSnapshot===snapshot)return libraryRenderGames;
  return withLibraryPathLookups(()=>{
  const plans = planMap();
  const games=(snapshot.library?.games || []).map(game => {
    const key = normalizedUiPath(game.primaryExecutable);
    const directoryPlan=plans.get(`dir:${keyFor(game)}`);
    // A directory match cannot lend the old EXE's identity after an explicit
    // primary change. Legacy plans with no EXE keep their display fallback.
    const steam=plans.get(key)||(directoryPlan&&(!directoryPlan.primaryExecutable||normalizedUiPath(directoryPlan.primaryExecutable)===key)?directoryPlan:null)||{};
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
  // Preserve Array.find's first match across both EXE and verified AppID,
  // including their original sorted order, without scanning every game again.
  const byExecutable=new Map(),byAppId=new Map();
  const indexGame=(game,index)=>{
    const row={game,index},exe=normalizedUiPath(game.primaryExecutable),appId=String(confirmedMediaAppId(game));
    if(!byExecutable.has(exe))byExecutable.set(exe,row);
    if(!byAppId.has(appId))byAppId.set(appId,row);
  };
  games.forEach(indexGame);
  for(const record of Object.values(snapshot.gameMedia?.entries||{})){
    const exe=record.executable?byExecutable.get(normalizedUiPath(record.executable)):null;
    const app=record.appId?byAppId.get(String(record.appId)):null;
    const match=exe&&app?(exe.index<=app.index?exe:app):exe||app;
    if(match){match.game.gameMedia=record;continue;}
    const game={mediaOnly:true,mediaKey:record.key,directoryName:record.name||'动态壁纸',primaryProductName:record.name||'动态壁纸',gameMedia:record,steam:{},override:{}};
    games.push(game);indexGame(game,games.length-1);
  }return games;
  });
}
function isNativeSteamGame(game) {
  return game?.steamNative === true || game?.contentType === 'steam-native' ||
    game?.steam?.steamNative === true || game?.steam?.steamOwnership === 'native-steam' ||
    game?.steam?.status === 'steam-native';
}
function category(game) {
  if (isNativeSteamGame(game)) return 'native-steam';
  if (game.steam?.status === 'already-in-steam' || game.steam?.status === 'added-to-steam') return 'in-steam';
  const bucket = manualBucket(game);
  if (bucket !== 'auto') return bucket;
  if (isNonGame(game)) return 'non-game';
  if (isSteamReady(game) || game.steam?.status === 'waiting-steam-verification') return 'not-in-steam';
  if (shouldPreserveWaitingCard(game)) return 'not-in-steam';
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
const WAITING_KEEP_STATUSES = new Set([
  'needs-minimum-artwork',
  'needs-identity-and-artwork',
  'needs-identity-confirmation',
  'needs-steam-verification',
  'waiting-steam-verification',
  'ready-to-add',
  'ready-not-selected',
  'needs-primary-confirmation'
]);
function snapshotLooksDegraded(next) {
  if (!next || typeof next !== 'object') return false;
  const source = String(next.steamPlanSource || '');
  if (source === 'local-scrape-fallback') return true;
  if (next.steamPlanError) return true;
  const items = Array.isArray(next.steamPlan?.items) ? next.steamPlan.items : [];
  return items.some(item => String(item?.steamVerificationStatus || '') === 'network-pending' ||
    String(item?.reason || '') === 'steam-verification-pending-network');
}
function shouldPreserveWaitingCard(game) {
  const keys = snapshot._preservedWaitingKeys;
  if (!Array.isArray(keys) || !keys.length) return false;
  if (!keys.includes(keyFor(game))) return false;
  const status = String(game.steam?.status || '');
  return !status || WAITING_KEEP_STATUSES.has(status);
}
function firstPopulatedTab(games) {
  const counts = categoryCounts(games);
  for (const tab of [activeTab, 'not-in-steam', 'in-steam', 'native-steam', 'unclassified', 'non-game']) {
    if (counts[tab] > 0) return tab;
  }
  return activeTab;
}
function tabContainingKeys(games, keys) {
  if (!keys || !keys.size) return '';
  const hits = {};
  for (const game of games) {
    if (!keys.has(keyFor(game))) continue;
    const code = category(game);
    hits[code] = (hits[code] || 0) + 1;
  }
  let best = '';
  let count = 0;
  for (const [code, value] of Object.entries(hits)) {
    if (value > count) {
      best = code;
      count = value;
    }
  }
  return best;
}
let lastMediaDownloadDisplay = null;
let mediaManualBusy=false;
let mediaRecoveryTimer=0;
let mediaRecoveryRequest=null;
let mediaPageClosed=false;
let mediaDownloadPaintPending=false;
let mediaDownloadPaintFrame=0;
function cancelMediaDownloadPaint(){
  if(mediaDownloadPaintFrame)cancelAnimationFrame(mediaDownloadPaintFrame);
  mediaDownloadPaintFrame=0;
}
function suspendMediaDownloadPaint(){
  cancelMediaDownloadPaint();
  const bar=document.getElementById('media-download-bar');
  if(bar&&!bar.hasAttribute('value')){mediaDownloadPaintPending=true;bar.value=0;}
  const wallpaperBar=document.querySelector('#shared-wallpaper-download-status progress');
  if(wallpaperBar&&!wallpaperBar.hasAttribute('value')){mediaDownloadPaintPending=true;wallpaperBar.value=0;}
}
function requestMediaDownloadPaint(){
  if(!mediaDownloadPaintPending||mediaDownloadPaintFrame||mediaPageClosed||!libraryRenderAllowed())return;
  mediaDownloadPaintFrame=requestAnimationFrame(()=>{mediaDownloadPaintFrame=0;if(mediaDownloadPaintPending)paintMediaDownloads();});
}
function syncMediaDownloadDisplayActivity(){
  scheduleMediaDownloadRecovery();
  if(mediaPageClosed||!libraryRenderAllowed())suspendMediaDownloadPaint();else requestMediaDownloadPaint();
}
function scheduleMediaDownloadRecovery(state=snapshot.mediaDownloads) {
  const retained=state&&((Number(state.remaining||0)>0)||state.catalogPending||Number(state.activeCount||0)>0);
  const enabled=state&&(state.enabled!==false||Number(state.manualUntil||0)>Date.now());
  if(mediaPageClosed||document.hidden||!document.hasFocus()||!retained||!enabled) {
    if(mediaRecoveryTimer)clearTimeout(mediaRecoveryTimer);mediaRecoveryTimer=0;return;
  }
  if(!mediaRecoveryTimer&&!mediaRecoveryRequest)mediaRecoveryTimer=window.setTimeout(()=>{mediaRecoveryTimer=0;void recheckMediaDownloads();},20000);
}
function recheckMediaDownloads() {
  if(mediaRecoveryRequest||mediaPageClosed)return mediaRecoveryRequest||Promise.resolve();
  const request=invoke('mediaDownloadStatus',{},5000).then(state=>{
    if(!mediaPageClosed)renderMediaDownloads(state);
  }).catch(()=>{}).finally(()=>{
    if(mediaRecoveryRequest===request)mediaRecoveryRequest=null;
    scheduleMediaDownloadRecovery();
  });
  mediaRecoveryRequest=request;return request;
}
window.addEventListener('pagehide',()=>{mediaPageClosed=true;if(mediaRecoveryTimer)clearTimeout(mediaRecoveryTimer);mediaRecoveryTimer=0;suspendMediaDownloadPaint();});
window.addEventListener('pageshow',()=>{mediaPageClosed=false;syncMediaDownloadDisplayActivity();});
document.addEventListener('visibilitychange',syncMediaDownloadDisplayActivity);
window.addEventListener('focus',syncMediaDownloadDisplayActivity);
window.addEventListener('blur',syncMediaDownloadDisplayActivity);
document.getElementById('media-download-now')?.addEventListener('click',async()=>{if(mediaManualBusy)return;mediaManualBusy=true;const button=document.getElementById('media-download-now');button.disabled=true;try{const manual=Number(snapshot.mediaDownloads?.manualUntil||0)>Date.now();renderMediaDownloads(await invoke(manual?'mediaDownloadCancel':'mediaDownloadNow',{},8000));}catch(error){showError(error,libraryNotice);}finally{mediaManualBusy=false;button.disabled=false;void recheckMediaDownloads();}});
function renderMediaDownloads(state = snapshot.mediaDownloads, options = {}) {
  if (state && lastMediaDownloadDisplay && Number(state.statusRevision || 0) < Number(lastMediaDownloadDisplay.statusRevision || 0)) state = lastMediaDownloadDisplay;
  const previousCompleted=lastMediaDownloadDisplay?.completed;
  if (state) {lastMediaDownloadDisplay=state;snapshot.mediaDownloads=state;}
  scheduleMediaDownloadRecovery(state);
  if(previousCompleted!=null&&Number(state?.completed||0)!==Number(previousCompleted))scheduleSilentRefresh();
  mediaDownloadPaintPending=true;
  if(mediaPageClosed||!libraryRenderAllowed()){suspendMediaDownloadPaint();return;}
  if(options.defer===true){requestMediaDownloadPaint();return;}
  cancelMediaDownloadPaint();paintMediaDownloads();
}
function paintMediaDownloads(){
  if(mediaPageClosed||!libraryRenderAllowed())return;
  mediaDownloadPaintPending=false;
  const state=snapshot.mediaDownloads;
  renderSharedWallpaperDownloadStatus();
  const wrap = document.getElementById('media-download-progress');
  if (!wrap) return;
  const known = state && typeof state === 'object';
  if(wrap.classList.contains('hidden')===Boolean(known))wrap.classList.toggle('hidden', !known);
  if (!known) return;
  const pauses = {'game-running':'游戏运行中，后台下载已暂停','battery-saver':'节电模式下暂停后台下载','low-battery':'低电量，后台下载已暂停','capacity-unavailable':'正在检查下载条件','media-storage-unavailable':'存储暂不可用，等待恢复后继续下载','media-disk-space-low':'磁盘余量不足，后台下载已暂停','media-cache-budget-full':'壁纸缓存已满，后台下载已暂停'};
  const resources={'startup-protection':'启动保护：前 30 秒不自动下载','on-battery':'电池模式，自动下载已停止','power-unknown':'正在确认供电，暂不自动下载','cpu-unknown':'正在检测 CPU 负载','cpu-busy':`CPU 占用超过 ${state.cpuThreshold || 15}%，自动下载已暂停`,'cpu-recovering':'CPU 负载下降，稳定后继续下载'};
  const manual=Number(state.manualUntil||0)>Date.now();const nowButton=document.getElementById('media-download-now');if(nowButton){const text=manual?'停止本轮补齐':'立即补齐壁纸',title='本轮忽略 CPU 和游戏暂停；电池、启动与磁盘保护保留';if(nowButton.textContent!==text)nowButton.textContent=text;if(nowButton.title!==title)nowButton.title=title;}
  const active = ['starting','identifying','resolving','downloading','publishing'].includes(state.phase);
  const count = manual&&state.manualTotal>0?`${state.manualCompleted||0} / ${state.manualTotal}`:`${state.completed || 0} / ${state.total || 0}`;
  const reason=state.pendingReason||state.reason||'';
  const failureReason=/automatic-.*budget-exhausted/.test(reason)?'该游戏已用尽自动尝试预算，已跳过；可手动重试':/media-size-invalid/.test(reason)?'视频超过单文件限制，已尝试较低清晰度':/store-unavailable/.test(reason)?'商店资料暂不可用，本轮已跳过':/not-game-or-no-video|no-dynamic-video/.test(reason)?'该条目暂无动态素材，本轮已跳过':/cache-owned-or-deferred/.test(reason)?'素材正由其他任务处理，等待归还后继续':/video-selection-expired/.test(reason)?'所选视频已失效，请重新搜索；原壁纸已保留':/staging-budget-full/.test(reason)?'下载暂存空间已满，等待腾出空间后继续':/storage-unavailable|data-root-unwritable|media-write|media-publish/.test(reason)?'存储暂不可用，等待恢复后继续下载':/video-incomplete|video-structure|slot-container/.test(reason)?'素材格式异常，保留原壁纸并等待重试':/upgrade-required/.test(reason)?'游戏库下载组件待更新':/worker-stalled/.test(reason)?'下载无进展，已退出并等待重试':/task-timeout|task-time-budget/.test(reason)?'单项下载超时，已退出并等待重试':/session-timeout/.test(reason)?'本批下载已达时限，保存队列后继续':/network|http|truncated|download-failed/.test(reason)?'网络下载中断，等待自动重试':/worker-exited/.test(reason)?'下载进程异常退出，等待自动恢复':/launch|job-|resume-failed/.test(reason)?'下载进程启动失败，等待自动恢复':'';
  const waiting=Number(state.nextDueAt||0)>Date.now()&&!state.workerPid&&!state.activeCount;
  const preparing=Number(state.inventoryAwaitUntil||0)>Date.now()&&state.inventoryReadyId!==state.inventoryRequestId;
  const waitingLabel=`${manual?'手动补齐':'后台下载'}等待重试 · ${failureReason||'本轮剩余任务暂不可用'} · 约 ${Math.max(1,Math.ceil((Number(state.nextDueAt)-Date.now())/1000))} 秒后复查 · ${count}`;
  const label = state.enabled === false&&!manual ? '壁纸后台下载已关闭'+(failureReason?' · '+failureReason:'') : state.resourcePauseReason ? resources[state.resourcePauseReason] || '后台下载等待恢复' : state.pauseReason&&!(manual&&state.pauseReason==='game-running') ? (pauses[state.pauseReason] || '壁纸后台下载已暂停，条件恢复后继续')+(/video-selection-expired/.test(state.reason||'')?' · '+failureReason:'') : waiting ? waitingLabel : manual ? `手动补齐中 · ${state.current?.name||(preparing?'正在准备本轮队列':failureReason||'等待下载进程接入')} · ${count}` : active ? `${state.activeCount > 1 ? `正在下载 ${state.activeCount} 个` : state.current?.name || '正在准备壁纸'}${state.resumedBytes ? ' · 断点续传' : ''} · ${count}${state.totalBytes ? ` · ${Math.min(100,Math.floor((state.bytes || 0)*100/state.totalBytes))}%` : ''}` : ['deferred','paused'].includes(state.phase) ? `${failureReason || '壁纸后台下载等待恢复'} · ${count}` : state.total ? `本轮已处理 · ${count}${state.unavailable ? ` · ${state.unavailable} 个已跳过，不影响其他游戏` : ''}` : '等待补齐动态壁纸';
  const labelNode=document.getElementById('media-download-label');
  if(labelNode.textContent!==label)labelNode.textContent=label;
  const bar = document.getElementById('media-download-bar');
  const maximum=state.totalBytes||state.total||1;
  if(bar.max!==Number(maximum))bar.max=maximum;
  if (active && !state.totalBytes) {if(bar.hasAttribute('value'))bar.removeAttribute('value');}
  else {const value=state.totalBytes?state.bytes||0:state.completed||0;if(!bar.hasAttribute('value')||bar.value!==Number(value))bar.value=value;}
}

function applyLibrarySnapshot(next, options = {}) {
  if (!next || typeof next !== 'object') return;
  const preserveWaiting = options.preserveWaiting === true || snapshotLooksDegraded(next);
  const previousTab = activeTab;
  const previousGames = mergedGames();
  const previousVisibleKeys = new Set(previousGames.filter(game => category(game) === previousTab).map(keyFor));
  const previousWaitingKeys = previousGames.filter(game => category(game) === 'not-in-steam').map(keyFor);
  snapshot = options.normalized === true ? next : normalizeWorkspaceSnapshot(next, snapshot);
  if (next.mediaDownloads) snapshot.mediaDownloads=next.mediaDownloads;
  if (options.deferRender !== true) renderMediaDownloads();
  if (preserveWaiting && previousWaitingKeys.length) snapshot._preservedWaitingKeys = previousWaitingKeys;
  const games = mergedGames();
  const visible = games.filter(game => category(game) === activeTab && librarySidebarMatches(game));
  if (visible.length === 0 && games.length > 0) {
    const switched = tabContainingKeys(games, previousVisibleKeys) || firstPopulatedTab(games);
    if (switched && switched !== activeTab) {
      activeTab = switched;
      const label = CATEGORY_DEFINITIONS[switched]?.label || switched;
      if (options.deferRender !== true && !$('#library-view').classList.contains('hidden')) {
        showNotice(`当前页暂无项目，已切换到「${label}」。`, false, libraryNotice);
      }
    }
  }
  if (options.deferRender !== true) {
    renderEntry();
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
  }
}
function selectedReadyCount() { const selected = new Set(selectedCommitDirectories()); return mergedGames().filter(game => category(game) === activeTab && selected.has(keyFor(game))).length; }
function selectedSteamCount() { return selectedVisibleSteamDirectories().length; }
function formatCount(value) { const count = Number.isFinite(Number(value)) ? Math.max(0, Number(value)) : 0; return `[${count}个]`; }
function formatSelectedCount(value) { const count = Number.isFinite(Number(value)) ? Math.max(0, Number(value)) : 0; return `[已选${count}个]`; }
function pageSelectable(game, page = activeTab) { if(game?.mediaOnly)return false; const itemCategory = category(game); if (itemCategory !== page || page === 'native-steam') return false; return true; }
function pageSelectionKeys(page = activeTab) {
  if (page === 'not-in-steam') return new Set(selectedGameDirectories === null ? defaultSelectedDirectories() : selectedGameDirectories);
  if (page === 'in-steam') return new Set(selectedSteamDirectories === null ? defaultSelectedSteamDirectories() : selectedSteamDirectories);
  return new Set(selectedCategoryDirectories[page] || []);
}
function defaultPageKeys(page = activeTab) { return mergedGames().filter(game => pageSelectable(game, page) && librarySidebarMatches(game)).map(keyFor); }
function storePageSelection(page, selected, all) {
  if (page === 'not-in-steam') selectedGameDirectories = selected.size === all.length ? null : selected;
  else if (page === 'in-steam') selectedSteamDirectories = selected.size === all.length ? null : selected;
  else selectedCategoryDirectories[page] = selected;
}
function selectedPageCount(page = activeTab) { const selected = pageSelectionKeys(page); return mergedGames().filter(game => pageSelectable(game, page) && selected.has(keyFor(game)) && librarySidebarMatches(game)).length; }
function isPageSelected(game) { if(!pageSelectable(game))return false;const selected=activeTab==='not-in-steam'?selectedGameDirectories:activeTab==='in-steam'?selectedSteamDirectories:selectedCategoryDirectories[activeTab];return selected===null||selected?.has(keyFor(game))===true; }

function statusLabel(game) {
  const definition = categoryDefinition(game);
  return [definition.label, definition.tone];
}
function escapeHtml(value) { return String(value ?? '').replace(/[&<>'"]/g, char => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', "'":'&#39;', '"':'&quot;' })[char]); }
function initials(name) { const parts = String(name || '?').trim().split(/[\s\-_]+/).filter(Boolean); return (parts.length > 1 ? parts.slice(0, 2).map(x => x[0]).join('') : parts[0]?.slice(0, 2) || '?').toUpperCase(); }
function manualArtworkPriority(game) { const override = game?.override || {}; const policy = override.artworkProtection || {}; return Boolean(override.cover || override.long || override.wallpaper || policy.cover === 'manual' || policy.cover === 'deleted' || policy.long === 'manual' || policy.long === 'deleted' || policy.wallpaper === 'manual' || policy.wallpaper === 'deleted'); }
function artworkMarkup(game, name) { const previewUrl = artworkPreviewUrl(artworkEntry(game, 'cover')); return previewUrl ? `<img src="${escapeHtml(previewUrl)}" alt="${escapeHtml(name)} 封面" loading="lazy">` : escapeHtml(initials(name)); }
function scrapeStatusFor(game) {
  if (isNativeSteamGame(game)) return null;
  const executable = normalizedUiPath(game?.primaryExecutable || '');
  if (!executable) return null;
  const items = snapshot.scrape?.items;
  if(!items)return null;
  const direct = items[executable] || items[String(game.primaryExecutable || '').toLocaleLowerCase()];
  if (direct) return direct;
  const index=libraryPathIndex('scrape',items);
  if(index)return index.get(executable)?.value||null;
  const found = Object.entries(items).find(([key, value]) => normalizedUiPath(key) === executable);
  return found?.[1] || null;
}
function scrapeBadgeMarkup(game) { const item = scrapeStatusFor(game); if (!item) return ''; if (item.status === 'loading') return '<span class="scrape-inline loading"><i></i>正在联网补齐</span>'; if (item.status === 'queued') return '<span class="scrape-inline queued">排队中</span>'; if (item.status === 'failed') return '<span class="scrape-inline failed">网络失败，可稍后重试</span>'; return ''; }
function applyScrapeCardState(card, gameOrExecutable) {
  const game = typeof gameOrExecutable === 'string'
    ? { primaryExecutable: gameOrExecutable }
    : gameOrExecutable;
  const operation=activeCardOperation(card.dataset.directory||keyFor(game));
  const item = operation?{status:['queued','running'].includes(operation.phase)?operation.phase==='running'?'loading':'queued':operation.phase}:scrapeStatusFor(game);
  const active = item?.status === 'queued' || item?.status === 'loading';
  const loading = item?.status === 'loading';
  if(card.classList.contains('scrape-active')!==active)card.classList.toggle('scrape-active',active);
  if(card.classList.contains('scrape-loading')!==loading)card.classList.toggle('scrape-loading',loading);
  const busy=active?'true':'false',status=item?.status||'';
  if(card.getAttribute('aria-busy')!==busy)card.setAttribute('aria-busy',busy);
  if(card.dataset.scrapeStatus!==status)card.dataset.scrapeStatus=status;
  const overlay = card.querySelector('.game-card-scrape-overlay');
  if (overlay) {
    const label = overlay.querySelector('.game-card-scrape-label');
    const text=operation?.label||(loading?'正在刮削':'等待刮削'),title=operation?.label||'';
    if(label&&label.textContent!==text)label.textContent=text;
    if(overlay.title!==title)overlay.title=title;
    const terminal=operation?.phase==='failed'||operation?.phase==='completed';
    const style=(node,key,value)=>{if(node&&node.style[key]!==value)node.style[key]=value;};
    style(overlay,'display',terminal?'grid':'');style(overlay,'opacity',terminal?'1':'');
    style(overlay,'visibility',terminal?'visible':'');style(overlay,'pointerEvents',terminal?'none':'');
    style(overlay.querySelector('.spinner'),'display',terminal?'none':'');
  }
}
function syncScrapeCardStates() {
  return withLibraryPathLookups(()=>libraryCards().forEach(card => applyScrapeCardState(card, card.dataset.executable || '')));
}

function renderEntry() {
  return withLibraryPathLookups(()=>{
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
  });
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
  for (const root of roots) { const row = document.createElement('div'); row.className = 'root-row'; row.innerHTML = `<code>${escapeHtml(root)}</code><button class="mini-button danger">移除</button>`; row.querySelector('button').addEventListener('click', async () => { if (!await askConfirm('移除扫描目录', `停止扫描此目录？\n${root}\n\n不会删除游戏文件。`, '移除')) return; await runBusy('removeRoot', '正在移除扫描目录', root, { path: root }); }); list.appendChild(row); }
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
let steamInstallPromptStatus = '';
function offerSteamWebsite(status) {
  // A missing userdata directory does not mean Steam is signed out, and
  // reinstalling an already installed client cannot fix a stale account scan.
  if (status !== 'not-installed' || steamInstallPromptStatus === status) return;
  steamInstallPromptStatus = status;
  window.setTimeout(async () => {
    if (steamAccountRequest || steamAccountDiscovery?.steamStatus !== 'not-installed') return;
    if (!await askConfirm('未检测到 Steam', '当前设备未检测到 Steam。打开 Steam 官方页面下载安装后，再返回这里检测。', '打开 Steam 官网')) return;
    try { await invoke('openSteamWebsite'); }
    catch (error) { showError(error, libraryNotice); }
  }, 0);
}
function validateSteamAccountDiscovery(result) {
  if (!isWorkspaceRecord(result) || !Array.isArray(result.accounts)
      || !['ready', 'not-installed', 'installed-no-account', 'error'].includes(result.steamStatus)
      || result.accounts.some(account => !isWorkspaceRecord(account)
        || typeof account.accountId !== 'string' || !account.accountId.trim())
      || (result.steamStatus === 'ready') !== (result.accounts.length > 0)) {
    throw new Error('Steam 账户检测结果无效，请重新检测。');
  }
  return result;
}
function refreshSteamAccountDiscovery(force = false) {
  if (steamAccountRequest) return steamAccountRequest;
  if (!force && steamAccountDiscovery && Date.now() - steamAccountLastCheck < STEAM_ACCOUNT_RECHECK_MS) {
    return Promise.resolve(steamAccountDiscovery);
  }
  steamAccountRequest = Promise.resolve().then(() => invoke('steamAccounts')).then(result => {
    steamAccountDiscovery = validateSteamAccountDiscovery(result);
    snapshot.steamAccounts = steamAccountDiscovery;
    ensureSteamTarget();
    return steamAccountDiscovery;
  }).catch(error => {
    steamAccountDiscovery = {
      accounts: [], steamStatus: 'error',
      error: error?.message || 'Steam 账户检测失败，请重新检测。'
    };
    return steamAccountDiscovery;
  }).finally(() => {
    steamAccountLastCheck = Date.now();
    steamAccountRequest = null;
    renderSteamTarget();
  });
  renderSteamTarget();
  return steamAccountRequest;
}
function applySteamRuntimeState(result) {
  steamRuntimeState = {
    running: result?.steamRunning === true ? true : result?.steamRunning === false ? false : null,
    steamPath: String(result?.steamPath || ''),
    checking: false
  };
  renderSteamRuntimeState();
}
function renderSteamRuntimeState() {
  const state = $('#steam-runtime-state');
  if (state) {
    const running = steamRuntimeState.running;
    state.className = `button steam-runtime-state ${running === true ? 'running' : running === false ? 'closed' : 'checking'}`;
    state.textContent = running === true ? 'Steam 运行中' : running === false ? 'Steam 已关闭，可以导入' : 'Steam 检测中';
    state.title = running === true ? '点击请求 Steam 温和退出' : running === false ? 'Steam 已关闭，可以导入；点击重新检测' : '正在检查 Steam 状态';
    state.disabled = running === null;
  }
  const button = $('#commit-button');
  if (!button || activeTab === 'in-steam' || activeTab === 'native-steam') return;
  const running = steamRuntimeState.running;
  const label = '点击加入';
  button.innerHTML = `${librarySidebarState()?'加入 Steam':label} <span class="count-label">${formatSelectedCount(selectedPageCount())}</span>`;decorateLibrarySteamButton(button);
  // Closing Steam is an independent safety action; it must remain available
  // even when the current page has no selected game.
  button.disabled = running === null || (running !== true && selectedReadyCount() === 0);
  button.title = running === true
    ? 'Steam 正在运行，点击后关闭 Steam；关闭后再点击加入 Steam'
    : running === false ? '加入当前页中已选中的项目（SteamID、封面和壁纸可为空）' : '正在检测 Steam 状态';
}
function refreshSteamRuntimeState(force = false) {
  if (!force && !steamRuntimeWatchAllowed()) return Promise.resolve(steamRuntimeState);
  const now = Date.now();
  if (!force && steamRuntimeRequest) return steamRuntimeRequest;
  if (!force && now - steamRuntimeLastCheck < 1800) return Promise.resolve(steamRuntimeState);
  if (steamRuntimeRequest) return steamRuntimeRequest;
  cancelSteamRuntimeWatchTimer();
  steamRuntimeState.checking = true;
  renderSteamRuntimeState();
  steamRuntimeRequest = invoke('steamCommitPreflight').then(result => {
    steamRuntimeLastCheck = Date.now();
    applySteamRuntimeState(result);
    return result;
  }).catch(() => {
    steamRuntimeLastCheck = Date.now();
    steamRuntimeState = { running: null, steamPath: '', checking: false };
    renderSteamRuntimeState();
    return null;
  }).finally(() => { steamRuntimeRequest = null; syncSteamRuntimeWatch(); });
  return steamRuntimeRequest;
}
// One visible, focused library owns this timer. Background state changes can
// still update the UI, but must not restart a periodic system-process scan.
function steamRuntimeWatchAllowed() {
  const library = $('#library-view');
  return !steamRuntimeWatchClosed && !!library && !library.classList.contains('hidden') &&
    !document.hidden && document.hasFocus();
}
function cancelSteamRuntimeWatchTimer() {
  if (steamRuntimeWatchTimer) window.clearTimeout(steamRuntimeWatchTimer);
  steamRuntimeWatchTimer = 0;
}
function syncSteamRuntimeWatch(refresh = false) {
  const active = steamRuntimeWatchAllowed();
  const activated = active && !steamRuntimeWatchForeground;
  steamRuntimeWatchForeground = active;
  if (!active) { cancelSteamRuntimeWatchTimer(); return; }
  if (steamRuntimeRequest) { cancelSteamRuntimeWatchTimer(); return; }
  if (refresh || activated) {
    cancelSteamRuntimeWatchTimer();
    void refreshSteamRuntimeState(true);
    return;
  }
  if (!steamRuntimeWatchTimer) steamRuntimeWatchTimer = window.setTimeout(() => {
    steamRuntimeWatchTimer = 0;
    if (steamRuntimeWatchAllowed()) void refreshSteamRuntimeState(true);
    else syncSteamRuntimeWatch();
  }, 3000);
}
function startSteamRuntimeWatch() {
  const resume = () => syncSteamRuntimeWatch();
  const pause = () => syncSteamRuntimeWatch();
  window.addEventListener('focus', resume);
  window.addEventListener('blur', pause);
  document.addEventListener('visibilitychange', resume);
  window.addEventListener('pagehide', () => { steamRuntimeWatchClosed = true; pause(); });
  window.addEventListener('pageshow', () => { steamRuntimeWatchClosed = false; resume(); });
  syncSteamRuntimeWatch();
}
function renderSteamTarget() {
  // Do not show yesterday's negative cache as a live login warning. A fresh
  // result also survives older game-scan snapshots arriving out of order.
  const checking = steamAccountRequest !== null || steamAccountDiscovery === null;
  const accounts = steamAccounts();
  const status = checking ? '' : steamAccountDiscovery.steamStatus;
  const copy = $('#steam-target-status');
  if (copy) copy.textContent = checking ? '正在检测 Steam 账户'
    : accounts.length ? `已发现 ${accounts.length} 个 Steam 大屏用户，将自动同步全部用户`
    : status === 'installed-no-account' ? 'Steam 已安装，暂未检测到可用的账户目录'
    : status === 'not-installed' ? '未检测到 Steam，请先安装 Steam'
    : status === 'error' ? 'Steam 账户检测失败，可重新检测'
    : '正在检测 Steam 账户';
  const notice = $('#in-steam-limitation');
  if (!notice) return;
  if (status === 'not-installed') {
    notice.classList.remove('hidden');
    notice.innerHTML = '未检测到 Steam。请先安装 Steam，再返回此窗口重新检测。<br><a href="https://store.steampowered.com/about/" target="_blank" rel="noreferrer">打开 Steam 官方下载页面</a>';
    notice.querySelector('a')?.addEventListener('click', event => { event.preventDefault(); invoke('openSteamWebsite').catch(error => showError(error, libraryNotice)); });
    offerSteamWebsite(status);
  } else if (status === 'installed-no-account') {
    notice.classList.remove('hidden');
    notice.textContent = '暂未检测到可用的 Steam 账户目录。若已登录 Steam，请返回此窗口或重新打开游戏库以重新检测；这不代表 Steam 当前未登录。';
  } else if (status === 'error') {
    notice.classList.remove('hidden');
    notice.textContent = steamAccountDiscovery.error || 'Steam 账户检测失败，可重新检测。';
  } else {
    notice.classList.add('hidden');
    notice.textContent = '';
  }
  renderSteamRuntimeState();
  syncSteamRuntimeWatch();
}
function renderArtworkRefreshControl() {
  const button = $('#refresh-artwork-button');
  if (!button) return;
  const state = manualRefreshProgress;
  let label = '手动刷新';
  if (state.phase === 'starting') label = '手动刷新（准备中）';
  else if (state.phase === 'queued') label = '手动刷新（排队中）';
  else if (state.phase === 'running') label = state.total > 0
    ? '手动刷新（' + (state.roundLabel || '第一轮') + ' ' + state.roundCurrent + '/' + state.roundTotal + '）'
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
function scrapeProgressText(state) {
  const roundLabel = state.roundLabel || (Number(state.round) === 2 ? '第二轮' : '第一轮');
  const current = Number.isFinite(Number(state.roundCurrent)) ? Number(state.roundCurrent) : 0;
  const total = Number.isFinite(Number(state.roundTotal)) ? Number(state.roundTotal) : 0;
  return total > 0 ? `后台刷新：${roundLabel} ${current}/${total}` : `后台刷新：${roundLabel} 准备中`;
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
  const card = document.createElement("article"); card.className = "game-card"; card.tabIndex = 0; card.setAttribute("role", "button"); card.dataset.directory = keyFor(game);
  card.dataset.executable = game.primaryExecutable || "";
  // Reuse the DOM for metadata-only changes, while event handlers always act
  // on the latest game record (especially its selected executable).
  card._setGame = next => {
    game = next;
    card.dataset.directory = keyFor(game);
    card.dataset.executable = game.primaryExecutable || '';
  };
  const name = gameDisplayName(game); const checked = isPageSelected(game); const canSelect = pageSelectable(game, activeTab);
  // Every waiting card is selectable. SteamID and artwork are optional and
  // can be completed later from the editor or background scraper.
  const steamIdPending = activeTab === 'not-in-steam' && !canSelect;
  card.classList.toggle("selection-enabled", canSelect); card.classList.toggle("selected-for-steam", checked); card.classList.toggle("unselected-for-steam", canSelect && !checked);
  card.classList.toggle("steam-id-pending", steamIdPending);
  card.classList.toggle("current-card", currentCardDirectory === keyFor(game));
  card.setAttribute("aria-pressed", canSelect ? (checked ? "true" : "false") : "false");
  card.setAttribute("aria-disabled", steamIdPending ? "true" : "false");
  card.setAttribute("aria-label", `${name}，${canSelect ? (checked ? "已选中，点击或按 A 取消选择；按 X 编辑" : "未选中，点击或按 A 选择；按 X 编辑") : steamIdPending ? "Steam AppID 尚未正式验证，不能加入；双击或按 X 编辑" : "按 X 编辑"}`);
  card.title = steamIdPending ? "Steam AppID 尚未正式验证，不能加入；双击或按 X 编辑" : "双击编辑";
  card.innerHTML = `<span class="card-art-glow" aria-hidden="true"></span><div class="game-main"><div class="cover-placeholder">${artworkMarkup(game, name)}</div><div><h2 class="game-card-name">${escapeHtml(name)}</h2></div></div><span class="game-card-scrape-overlay" aria-hidden="true"><span class="spinner"></span><span class="game-card-scrape-label">正在刮削</span></span>`;
  // Keep the glow on the exact same priority/deletion path as the visible
  // card image. The old shortcut bypassed artworkEntry(), so an explicitly
  // deleted manual cover still left the stale automatic image as a glow.
  const cardArtwork = artworkEntry(game, 'cover');
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
  card.addEventListener("click", event => { if (event.target.closest("button,input,select")) return; if (canSelect) toggleGameSelection(game, event.isTrusted && card.matches(":hover")); else if (!steamIdPending) openEdit(game); });
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
      if (canSelect) toggleGameSelection(game); else if (!steamIdPending) openEdit(game);
    }
  });
  applyScrapeCardState(card, game);
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
function libraryHeaderItems() { return [...document.querySelectorAll('#library-view:not(.hidden) #steam-runtime-state, #library-view:not(.hidden) #back-entry-button')].filter(isVisuallyPresent); }
function libraryFocusItems() { if(librarySidebarState())return visibleFocusables($('#library-view'));return [...libraryHeaderItems(), ...libraryPageTabs(), ...libraryToolbarItems(), ...libraryPopupItems(), ...libraryCards().filter(item => !item.classList.contains('hidden'))]; }
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
  if(librarySidebarState())return librarySidebarMove(key);
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
  const long = $('#artwork-long-preview');
  const wallpaper = $('#artwork-wallpaper-preview');
  if (!cover || !long || !wallpaper || ![cover, long, wallpaper].includes(active)) return null;
  if (key === 'ArrowUp') {
    if (active === long) return cover;
    return active;
  }
  if (key === 'ArrowDown') {
    if (active === cover) return long;
    return workspaceDropdownTriggerForSelect($('#edit-primary-select')) ||
      workspaceDropdownTriggerForSelect($('#edit-bucket-select')) || null;
  }
  if (key === 'ArrowRight' && (active === cover || active === long)) return wallpaper;
  if (key === 'ArrowLeft' && active === wallpaper) return cover;
  return active;
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
  const active = document.activeElement;
  if (['ArrowLeft', 'ArrowRight'].includes(key) && active?.closest('.edit-actions')) {
    const bounds = active.getBoundingClientRect();
    const row = items.filter(item => {
      if (!item.closest('.edit-actions')) return false;
      const rect = item.getBoundingClientRect();
      return Math.min(bounds.bottom, rect.bottom) - Math.max(bounds.top, rect.top) > .5;
    });
    return moveSpatialFocus(row, key) || true;
  }
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
    // This modal is a single visual column. Do not let horizontal movement
    // jump from the candidate control to the close button in the header.
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
    if (modal.id === 'edit-modal' && active?.matches?.('.artwork-dropzone[data-artwork-editable="true"]')) {
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
// Controller navigation selects text fields without making them editable.
// Unlock only on A/Enter or a pointer gesture; blur/B restores navigation mode.
function configureControllerTextInput(target, editable = true) {
  if (!target?.matches?.('[data-controller-input]')) return;
  target.dataset.controllerEditable = String(editable);
  finishControllerTextInput(target);
}
function finishControllerTextInput(target) {
  if (!target?.matches?.('[data-controller-input]')) return false;
  const wasEditing = target.dataset.controllerEditing === 'true';
  delete target.dataset.controllerEditing;
  target.readOnly = true;
  return wasEditing;
}
function beginControllerTextInput(target) {
  if (!target?.matches?.('[data-controller-input]') || target.disabled ||
      target.dataset.controllerEditable === 'false') return false;
  target.dataset.controllerEditing = 'true';
  target.readOnly = false;
  target.focus({ preventScroll: false });
  return true;
}
document.querySelectorAll('[data-controller-input]').forEach(target => {
  configureControllerTextInput(target);
  target.addEventListener('pointerdown', event => {
    if (event.button === 0) beginControllerTextInput(target);
  });
  target.addEventListener('blur', () => finishControllerTextInput(target));
});
function requestGamepadKeyboard(target = document.activeElement) {
  if (target?.matches?.('[data-controller-input]') && !beginControllerTextInput(target)) return false;
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
  if(librarySidebarState()&&!libraryModalOpen()&&event.key==='Enter'&&document.activeElement?.id==='library-search'){event.preventDefault();requestGamepadKeyboard(document.activeElement);return true;}
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
    const game = mergedGames().find(item => keyFor(item) === card.dataset.directory);
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
function pageSelectableGames() { return mergedGames().filter(game => pageSelectable(game, activeTab) && librarySidebarMatches(game)); }
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
// Use visible cards as anchors: inserting/removing games above the viewport
// should keep the same game at the same screen position, not just the same Y.
function libraryCardViewportTop(card) {
  // Selection scales a card visually; anchor its layout position so toggling
  // that animation cannot move the page by a few pixels on every click.
  let top = 0;
  for (let item = card; item; item = item.offsetParent) top += item.offsetTop;
  return top - window.scrollY;
}
function captureLibraryViewport() {
  if (library._renderedTab !== activeTab) return null;
  const scroll=$('#library-sidebar-scroll');if(scroll){const top=scroll.getBoundingClientRect().top;return {sidebar:true,x:scroll.scrollLeft,y:scroll.scrollTop,anchors:libraryCards().filter(card=>{const r=card.getBoundingClientRect();return r.bottom>top&&r.top<top+scroll.clientHeight;}).map(card=>({directory:card.dataset.directory,top:card.getBoundingClientRect().top-top}))};}
  return { x: window.scrollX, y: window.scrollY,
    anchors: libraryCards().filter(card => {
      const rect = card.getBoundingClientRect();
      return rect.bottom > 0 && rect.top < window.innerHeight;
    }).map(card => ({ directory: card.dataset.directory, top: libraryCardViewportTop(card) })) };
}
function restoreLibraryViewport(viewport) {
  if (!viewport) return;
  if(viewport.sidebar){const scroll=$('#library-sidebar-scroll');if(!scroll)return;const cards=new Map(libraryCards().map(card=>[card.dataset.directory,card])),anchor=viewport.anchors.find(item=>cards.has(item.directory));const y=viewport.y<=0?0:anchor?scroll.scrollTop+cards.get(anchor.directory).getBoundingClientRect().top-scroll.getBoundingClientRect().top-anchor.top:viewport.y;scroll.scrollTo({left:viewport.x,top:Math.max(0,y),behavior:'instant'});return;}
  const cards = new Map(libraryCards().map(card => [card.dataset.directory, card]));
  const anchor = viewport.anchors.find(item => cards.has(item.directory));
  const y = viewport.y <= 0 ? 0 : anchor ? window.scrollY + libraryCardViewportTop(cards.get(anchor.directory)) - anchor.top : viewport.y;
  window.scrollTo({ left: viewport.x, top: Math.max(0, y), behavior: 'instant' });
}
function updateLibraryCards(games) {
  const existing = new Map(libraryCards().map(card => [normalizedUiPath(card.dataset.directory), card]));
  const wanted = new Set(games.map(game => keyFor(game)));
  // Keep the grid attached while patching. Clearing it first clamps the page
  // scroll height and reloads every image even when only one game changed.
  for (const [key, card] of existing) if (!wanted.has(key)) card.remove();
  games.forEach((game, index) => {
    const key = keyFor(game);
    const signature = JSON.stringify([activeTab, gameDisplayName(game), artworkPreviewUrl(artworkEntry(game, 'cover')),
      isPageSelected(game), pageSelectable(game, activeTab)]);
    let card = existing.get(key);
    if (!card || card._renderSignature !== signature) {
      const replacement = createCard(game);
      replacement._renderSignature = signature;
      if (card?.isConnected) card.replaceWith(replacement);
      card = replacement;
    } else {
      card._setGame(game);
      applyScrapeCardState(card, game);
      card.classList.toggle('current-card', currentCardDirectory === key);
    }
    if (library.children[index] !== card) library.insertBefore(card, library.children[index] || null);
  });
  library._renderedTab = activeTab;
}
function renderLibrary() {
  return withLibraryPathLookups(()=>{
  const previousGames=libraryRenderGames,previousSnapshot=libraryRenderSnapshot;
  libraryRenderGames=mergedGames();libraryRenderSnapshot=snapshot;
  try {
  renderMediaDownloads();
  const viewport = captureLibraryViewport();
  const focusedDescriptor = libraryFocusDescriptor(document.activeElement) || libraryFocusMemory;
  const games = mergedGames(); const counts = categoryCounts(games);
  ensureNativeSteamTab();
  $('#count-not-in-steam').textContent = formatCount(counts['not-in-steam']); $('#count-in-steam').textContent = formatCount(counts['in-steam']); $('#count-native-steam').textContent = formatCount(counts['native-steam']); $('#count-unclassified').textContent = formatCount(counts.unclassified); $('#count-non-game').textContent = formatCount(counts['non-game']); $('#library-subtitle').textContent = `${formatCount(games.length)} 项目 · 已确认 ${formatCount(games.filter(game => game.status === 'ready').length)} 主程序`;
  document.querySelectorAll('.page-tab').forEach(tab => tab.classList.toggle('active', tab.dataset.tab === activeTab)); const inSteam = activeTab === 'in-steam'; const nativeSteam = activeTab === 'native-steam'; const notInSteam = activeTab === 'not-in-steam'; document.querySelector('.steam-target-copy')?.classList.add('hidden'); document.querySelector('.steam-bar')?.classList.remove('hidden'); $('#commit-button').classList.toggle('hidden', inSteam || nativeSteam); $('#delete-button').classList.toggle('hidden', !inSteam); $('#commit-button').innerHTML = `点击加入 <span class="count-label">${formatSelectedCount(selectedPageCount())}</span>`; $('#delete-button').innerHTML = `点击删除 <span class="count-label">${formatSelectedCount(selectedSteamCount())}</span>`; $('#commit-button').disabled = inSteam || nativeSteam || selectedReadyCount() === 0; $('#commit-button').title = nativeSteam ? '原生 Steam 游戏不能通过自定义库重复导入' : '加入当前页中已选中的项目（SteamID、封面和壁纸可为空）'; $('#delete-button').disabled = selectedSteamCount() === 0; $("#in-steam-limitation").classList.add("hidden"); syncPageSelectionControls(); renderRoots(); renderSteamRuntimeState();
  renderArtworkRefreshControl();
  renderSteamTarget();
  const visible = games.filter(game => category(game) === activeTab && librarySidebarMatches(game));
  updateLibraryCards(visible);
  $('#empty').classList.toggle('hidden', visible.length !== 0); library.classList.toggle('hidden', visible.length === 0);
  if (visible.length > 0 && !libraryModalOpen()) {
    const remembered = resolveLibraryFocusDescriptor(focusedDescriptor);
    if (remembered && remembered !== document.activeElement) focusWorkspaceElement(remembered, false);
    else if (!$('#library-view').contains(document.activeElement)) focusWorkspaceElement(libraryCards()[0], false);
  }
  // Restore synchronously before paint. An older animation-frame callback
  // must never move the view after the user has already scrolled again.
  restoreLibraryViewport(viewport);
  syncUpdateSteamArtworkButton();
  syncLibrarySidebar();
  } finally {libraryRenderGames=previousGames;libraryRenderSnapshot=previousSnapshot;}
  });
}
function candidateDataFor(game) {
  const key = normalizedUiPath(game.primaryExecutable);
  const direct = snapshot.identityCandidates?.[key];
  if (direct?.candidates) return direct.candidates;
  const normalized = Object.entries(snapshot.identityCandidates || {}).find(([candidateKey, value]) =>
    normalizedUiPath(candidateKey) === key || normalizedUiPath(value.executable) === key);
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
function editorIdentityName(game) { return String(game?.override?.name || game?.steam?.suggestedName || game?.primaryProductName || game?.directoryName || ''); }
function draftFor(game) {
  const cloned = cloneEditGame(game);
  cloned.override = { ...(cloned.override || {}) };
  cloned.override.artwork = { ...(cloned.override.artwork || {}) };
  return { game: cloned, original: cloneEditGame(game), originalBucket: manualBucket(game), bucket: manualBucket(game), artworkDirty: false, artworkChanged: { cover: false, long: false, wallpaper: false }, identityDirty: false, primaryDirty: false,
    identityBaseline: {name:editorIdentityName(game).trim(),steamId:String(formalSteamAppId(game)||'')},nameEdits:0,steamIdEdits:0 };
}
function draftName() { return $('#identity-name')?.value.trim() || ''; }
function draftSteamId() { return $('#identity-steam-id')?.value.trim() || ''; }
function syncIdentityDraft(field = '') {
  if(!editDraft?.game)return;
  if(field==='name')++editDraft.nameEdits;
  if(field==='steamId')++editDraft.steamIdEdits;
  const game=editDraft.game,name=draftName(),steamId=draftSteamId(),baseline=editDraft.identityBaseline;
  const nameChanged=name!==baseline.name||editDraft.nameEdits>0;
  const idChanged=steamId!==baseline.steamId||editDraft.steamIdEdits>0;
  game.override={...(game.override||{})};
  // Displayed defaults are not a manual override. A real input/candidate
  // action can still explicitly accept exactly the displayed value.
  if(nameChanged){if(name)game.override.name=name;else delete game.override.name;}
  if(isNativeSteamGame(game)){
    delete game.override.steamId;delete game.override.igdbId;delete game.override.idCleared;
  }else if(steamId&&idChanged){game.override.steamId=Number(steamId);delete game.override.igdbId;delete game.override.idCleared;}
  else if(!steamId&&(steamIdClearPending||game.override.idCleared===true)){delete game.override.steamId;game.override.idCleared=true;}
  editDraft.identityDirty=nameChanged||idChanged||steamIdClearPending;
  selectedActionGame=game;
}
function renderEditDraft(game = selectedActionGame) {
  if (!game) return;
  renderIdentityCandidates(game); renderIdentityNameCandidates(game);
  const [label, tone] = statusLabel(game);
  $('#edit-title').textContent = gameDisplayName(game); $('#edit-path').textContent = game.primaryExecutable || game.gameDirectory || '';
  $('#edit-status').className = `badge ${tone}`; $('#edit-status').textContent = label;
  setBucketOptions(editDraft?.bucket || manualBucket(game));
  renderArtworkDraft();
  renderEditorOperationState();
}
function cancelEditReturnFocus(){
  if(editReturnFocusFrame)cancelAnimationFrame(editReturnFocusFrame);
  editReturnFocusFrame=0;
}
function restoreEditReturnFocus(directory) {
  cancelEditReturnFocus();
  if (!directory || $('#library-view')?.classList.contains('hidden')) return false;
  currentCardDirectory=directory;libraryFocusMemory={type:'card',directory};
  if(!libraryRenderAllowed())return false;
  const card = libraryCards().find(item => String(item.dataset.directory || '').toLocaleLowerCase() === directory);
  if (!card) return false;
  const highlight=target=>libraryCards().forEach(item=>{const active=item===target;if(item.classList.contains('current-card')!==active)item.classList.toggle('current-card',active);});
  highlight(card);
  const epoch=silentRefreshEpoch;
  editReturnFocusFrame=requestAnimationFrame(() => {
    editReturnFocusFrame=0;
    if (libraryRenderAllowed()&&epoch===silentRefreshEpoch&&currentCardDirectory===directory&&$('#edit-modal')?.classList.contains('hidden')&&!libraryModalOpen()) {
      const latest = libraryCards().find(item => String(item.dataset.directory || '').toLocaleLowerCase() === directory);
      if (latest) {highlight(latest);focusLibraryCard(latest);}
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
function refreshNativeArtworkForEdit(game) {
  if (!isNativeSteamGame(game) || !game.primaryExecutable) return;
  const owner={epoch:editPresentationEpoch,key:keyFor(game),executable:normalizedUiPath(game.primaryExecutable),appId:formalSteamAppId(game)};
  nativeArtworkRefreshOwner=owner;
  const current=()=>nativeArtworkRefreshOwner===owner&&owner.epoch===editPresentationEpoch&&selectedActionGame&&keyFor(selectedActionGame)===owner.key&&normalizedUiPath(selectedActionGame.primaryExecutable)===owner.executable;
  return invoke('refreshNativeArtwork', { executable: game.primaryExecutable }).then(result => {
    if(!current()||normalizedUiPath(result?.dataRoot||'')!==normalizedUiPath(snapshot?.dataRoot||''))return;
    const belongs=item=>{
      if(normalizedUiPath(item.primaryExecutable)!==owner.executable)return false;
      if(typeof item.gameDirectory==='string'&&item.gameDirectory&&keyFor(item)!==owner.key)return false;
      const appId=String(item.nativeSteamAppId||item.steamStoreAppId||item.storefrontAppId||item.appId||'');
      return !owner.appId||!appId||appId===owner.appId;
    };
    const refreshed=Array.isArray(result?.steamPlan?.items)?result.steamPlan.items.find(belongs):null;
    if(!refreshed)return;
    // This reply owns native artwork only. Never replace a newer whole model,
    // media preference or unsaved editor fields with its older snapshot.
    const previews=isWorkspaceRecord(refreshed.artworkPreview)?structuredClone(refreshed.artworkPreview):{};
    const items=(snapshot.steamPlan?.items||[]).map(item=>belongs(item)?{...item,artworkPreview:previews}:item);
    snapshot={...snapshot,steamPlan:{...snapshot.steamPlan,items}};
    selectedActionGame.steam={...selectedActionGame.steam,artworkPreview:previews};
    if(editDraft?.original)editDraft.original.steam={...editDraft.original.steam,artworkPreview:previews};
    const inherited=artworkDraftFromGame(selectedActionGame);
    for(const type of ['cover','long','wallpaper'])if(!artworkDraft.dirty[type]&&!editDraft?.artworkChanged?.[type]){
      artworkDraft[type]=inherited[type];artworkPageBase[type]=inherited[type]?{...inherited[type]}:null;
      // Native cache files can be overwritten at an unchanged URL. A direct
      // refresh invalidates inherited image presentation, never a shared video.
      const shared=type==='wallpaper'&&(sharedWallpaperMode()==='dynamic'||selectedActionGame.gameMedia?.staticPreviewUrl&&(!inherited[type]?.asset||selectedActionGame.mediaOnly));
      if(inherited[type]?.state==='inherit'&&!shared){const target=document.getElementById('artwork-'+type+'-preview');if(target)target._editorArtworkProjection=null;}
    }
    renderArtworkDraft();
    if(libraryRenderAllowed())renderLibrary();else silentRenderPending=true;
  }).catch(error=>{if(current())showError(error,libraryNotice);})
    .finally(()=>{if(nativeArtworkRefreshOwner===owner)nativeArtworkRefreshOwner=null;});
}
function openEdit(game) {
  if (!game) return;
  cancelEditReturnFocus();
  if ($('#edit-modal').classList.contains('hidden') || keyFor(selectedActionGame || {}) !== keyFor(game) || normalizedUiPath(selectedActionGame?.primaryExecutable||'')!==normalizedUiPath(game.primaryExecutable||'')) {
    ++editPresentationEpoch;nativeArtworkRefreshOwner=null;editorArtworkRenderPending=false;sharedWallpaperPreviewChoice=null;
  }
    try {
    const previousExecutable = normalizedUiPath(selectedActionGame?.primaryExecutable || '');
    const nextExecutable = normalizedUiPath(game.primaryExecutable || '');
    if (previousExecutable && nextExecutable && previousExecutable !== nextExecutable) cancelEditorBackgroundTasks();
    const returnDirectory = keyFor(game);
    if (returnDirectory) editReturnCardDirectory = returnDirectory;
    if (!editDraft || (game.mediaOnly?keyFor(editDraft.game)!==keyFor(game):normalizedUiPath(editDraft.game?.primaryExecutable)!==normalizedUiPath(game.primaryExecutable))) editDraft = draftFor(game);
    selectedActionGame = editDraft.game;
    artworkDraft = artworkDraftFromGame(selectedActionGame);
    artworkPageBase = { cover: artworkDraft.cover ? { ...artworkDraft.cover } : null, long: artworkDraft.long ? { ...artworkDraft.long } : null, wallpaper: artworkDraft.wallpaper ? { ...artworkDraft.wallpaper } : null };
    steamIdClearPending = selectedActionGame.override?.idCleared === true;
    renderEditDraft(selectedActionGame);
    const override = selectedActionGame.override || {};
    $('#identity-name').value = editorIdentityName(selectedActionGame);
    $('#identity-steam-id').value = formalSteamAppId(selectedActionGame);
    const nativeSteam = isNativeSteamGame(selectedActionGame);
    configureControllerTextInput($('#identity-name'));
    configureControllerTextInput($('#identity-steam-id'), !nativeSteam);
    $('#identity-steam-id').title = nativeSteam ? '原生 Steam AppID 来自 appmanifest，不允许手动改写' : '手柄按 A 输入，按 B 返回导航';
    $('#identity-steam-id-clear').disabled = nativeSteam;
    if (nativeSteam) $('#artwork-search-hint').textContent = `原生 Steam AppID ${formalSteamAppId(selectedActionGame)}；素材启动时读取缓存，进入手动编辑时刷新 Steam 本地素材`;
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
    renderEditorOperationState();
    $('#identity-name').disabled=Boolean(game.mediaOnly);$('#identity-steam-id').disabled=Boolean(game.mediaOnly)||nativeSteam;$('#identity-steam-id-clear').disabled=Boolean(game.mediaOnly)||nativeSteam;
    $('#edit-modal').classList.remove('hidden'); requestAnimationFrame(focusEditModal);
    void prepareSharedWallpaperPreview(selectedActionGame);
    if(!selectedActionGame.mediaOnly)startArtworkPrefetch(selectedActionGame);
    if (nativeSteam) refreshNativeArtworkForEdit(selectedActionGame);
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
      (candidate.igdbId ? `IGDB ID ${candidate.igdbId}（仅辅助，不能确认 SteamID）` : '无直接 ID');
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
  requestIdentitySearchFocus(workspaceDropdownTriggerForSelect(select));
}
async function identifyCandidate(candidate) {
  if (!selectedActionGame || !candidate?.name) return;
  const game = selectedActionGame;
  retireIdentitySearch();
  identitySearchManualCandidate = null;
  searchReturnToEdit = false;
  game.override = { ...(game.override || {}) };
  game.override.name = candidate.name;
  ++editDraft.nameEdits;
  if(candidate.steamAppId)++editDraft.steamIdEdits;
  if (candidate.igdbId && !candidate.steamAppId) {
    // Keep IGDB visible as a lower-priority search result, but never persist
    // it as the game's identity. Only a Steam AppID may move the card toward
    // the Steam import queue; IGDB artwork enrichment happens after that.
    delete game.override.igdbId;
    editDraft.identityDirty = true;
    selectedActionGame = game;
    $('#identity-name').value = candidate.name;
    $('#identity-steam-id').value = formalSteamAppId(game) ? String(formalSteamAppId(game)) : '';
    renderEditDraft(game);
    renderArtworkDraft();
    $('#edit-modal').classList.remove('hidden');
    showNotice('IGDB ID 仅作辅助候选，未确认 SteamID；名称已暂存，请选择 Steam AppID 后保存。', false, libraryNotice);
    return;
  }
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
  const owner=identitySearchOwner;
  if(!identitySearchOwnerCurrent(owner)||owner.terminal||normalizedUiPath(message?.executable)!==owner.executable||String(message?.query||'').trim()!==owner.query||typeof message?.jobId!=='string'||!message.jobId)return false;
  if(!owner.acknowledged){
    // The native generation is monotonic for this EXE. Retain one newest
    // terminal event until the request receipt binds its exact job ID.
    if(message.event!=='identity-search-started'&&(!owner.earlyTerminal||Number(message.generation||0)>=Number(owner.earlyTerminal.generation||0)))owner.earlyTerminal=message;
    return false;
  }
  return message.jobId===owner.jobId&&(!message.generation||!owner.generation||Number(message.generation)===owner.generation);
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
  const game=selectedActionGame,steamId=$('#identity-steam-id').value.trim(),name=$('#identity-name').value.trim()||artworkSearchQuery(game);
  if(steamId&&!/^[1-9]\d*$/.test(steamId))return showNotice('Steam AppID 必须是正整数。',true,libraryNotice);
  const owner={token:++identitySearchToken,epoch:silentRefreshEpoch,key:keyFor(game),executable:normalizedUiPath(game.primaryExecutable),query:name,acknowledged:false,jobId:'',generation:0,terminal:false,presentation:null,earlyTerminal:null,focusTarget:null};
  identitySearchOwner=owner;identitySearchInFlight=true;identitySearchCancelled=false;searchReturnToEdit=true;identitySearchExecutable=game.primaryExecutable;identitySearchQuery=name;
  identitySearchManualCandidate=steamId?{name,steamAppId:Number(steamId),provider:'当前输入',manualSelection:true,score:1,year:0}:null;
  $('#identity-search-query').textContent=name;$('#identity-search-summary').textContent=`名称：${name} · 正在后台搜索候选，不会阻塞编辑页面`;
  $('#identity-search-list').replaceChildren();$('#identity-search-modal').classList.remove('hidden');startIdentitySearchLoading();requestIdentitySearchFocus($('#identity-search-cancel'),owner);
  try{
    const result=await invoke('searchIdentity',{executable:game.primaryExecutable,query:name},8000);
    if(!identitySearchOwnerCurrent(owner))return;
    owner.acknowledged=true;owner.jobId=typeof result?.jobId==='string'?result.jobId:'';owner.generation=Number(result?.generation||0);
    if(result?.pending&&!owner.jobId)throw new Error('搜索任务未返回有效回执，请重新搜索。');
    identitySearchJobId=owner.jobId;
    if(result?.pending)owner.deadline=window.setTimeout(()=>{
      if(!identitySearchOwnerCurrent(owner)||owner.terminal)return;
      finishIdentitySearch(owner,{kind:'failed',error:new Error('搜索候选等待超时，请重新搜索。')});invoke('cancelIdentitySearch',{executable:owner.executable}).catch(()=>{});
    },95000);
    const early=owner.earlyTerminal;owner.earlyTerminal=null;
    if(early&&early.jobId===owner.jobId&&(!early.generation||!owner.generation||Number(early.generation)===owner.generation)){consumeIdentitySearchEvent(early);return;}
    const candidates=Array.isArray(result?.candidates)?result.candidates:[];
    if(!result?.pending){finishIdentitySearch(owner,{kind:'candidates',candidates});return;}
    if(candidates.length||identitySearchManualCandidate){owner.presentation={kind:'candidates',candidates,notice:result.cached?'已显示缓存候选，网络结果会继续更新。':'自动识别已在后台搜索，请选择名称和 ID。'};presentIdentitySearch(owner);}
  }catch(error){
    if(!identitySearchOwnerCurrent(owner)||owner.terminal)return;
    const text=error?.message||String(error);finishIdentitySearch(owner,{kind:'failed',cancelled:text.includes('TASK_CANCELLED')||text.includes('ERROR_CANCELLED'),error});
    if(!owner.jobId)invoke('cancelIdentitySearch',{executable:owner.executable}).catch(()=>{});
  }
}
function artworkOverride(game) {
  const direct = game?.override && typeof game.override === "object" ? game.override : {};
  const legacy = direct.artwork && typeof direct.artwork === "object" ? direct.artwork : {};
  return { ...legacy, ...direct };
}
function artworkLabel(type) { return type === 'cover' ? '封面' : type === 'long' ? '横版封面' : '壁纸'; }
function artworkSearchTypeForSlot(type) { return type; }
function artworkEntry(game, type) {
  const override = artworkOverride(game);
  const protection = override.artworkProtection && typeof override.artworkProtection === "object" ? override.artworkProtection : {};
  if (protection[type] === "deleted") return null;
  const entry = override[type];
  if (entry?.available !== false && artworkPreviewUrl(entry)) return { ...entry, source: "manual", priority: "manual" };
  const preview = type === "cover" ? game?.steam?.artworkPreview?.tall
    : type === "long" ? game?.steam?.artworkPreview?.long
    : game?.steam?.artworkPreview?.hero;
  return preview ? { ...preview, source: preview.source === "manual" ? "manual" : "automatic", priority: preview.source === "manual" ? "manual" : "automatic" } : null;
}
function artworkPreviewUrl(entry) {
  if (!entry) return '';
  const cacheBust = value => {
    const updated = Number(entry.updatedAt || entry.updated_at || 0);
    if (!updated || !/^https?:\/\//i.test(value)) return value;
    return `${value}${value.includes('?') ? '&' : '?'}v=${encodeURIComponent(String(updated))}`;
  };
  if (entry.available === false) return '';
  const raw = String(entry.portableFile || entry.path || entry.file || '').trim();
  const root = normalizedUiPath(snapshot.dataRoot || '');
  const normalized = normalizedUiPath(raw);
  if (root && (normalized === root || normalized.startsWith(root + '\\'))) {
    const relative = normalized.slice(root.length).replace(/^\\+/, '');
    const mapped = `https://steam-library-data.localhost/${relative.split('\\').map(part => encodeURIComponent(part)).join('/')}`;
    return cacheBust(mapped);
  }
  if (entry.url) return cacheBust(String(entry.url));
  return /^https?:\/\//i.test(raw) ? raw : '';
}
function artworkDropMarkup(entry, label) { const previewUrl = artworkPreviewUrl(entry); if (previewUrl) return `<img src="${escapeHtml(previewUrl)}" alt="${escapeHtml(label)}" loading="lazy">`; const raw = entry?.path || entry?.file || entry?.portableFile; if (raw) return `<span class="artwork-path">${escapeHtml(raw)}</span>`; return `<span class="artwork-empty">暂无${escapeHtml(label)}</span>`; }
function artworkSlotIsSteamPreview(type) {
  const slot = artworkDraft[type];
  if (!selectedActionGame || slot?.state !== 'inherit') return false;
  const entry = slot.asset || {};
  const raw = String(entry.url || entry.file || entry.path || entry.portableFile || '');
  return artworkCandidateIsSteam({ provider: entry.provider, url: raw }) || Boolean(formalSteamAppId(selectedActionGame));
}
async function downloadIgdbArtworkFallback(target, label, type) {
  const game=selectedActionGame,slot=artworkDraft[type],epoch=editPresentationEpoch,key=keyFor(selectedActionGame),executable=normalizedUiPath(selectedActionGame.primaryExecutable||'');
  const token = ++artworkTaskToken;target._artworkFallbackToken=token;
  const current=()=>token===artworkTaskToken&&epoch===editPresentationEpoch&&selectedActionGame===game&&keyFor(selectedActionGame)===key&&normalizedUiPath(selectedActionGame.primaryExecutable||'')===executable&&artworkDraft[type]===slot;
  artworkTaskInFlight = true;
  try {
    const requestType = artworkSearchTypeForSlot(type);
    const result = await ensureArtworkSearch(game, requestType, { automatic: true });
    if(!current())return;
    const candidates = Array.isArray(result?.candidates) ? result.candidates : [];
    const fallback = candidates.find(candidate => String(candidate?.provider || '').toLocaleLowerCase() === 'playnite-igdb' && candidate?.url) ||
      candidates.find(candidate => String(candidate?.provider || '').toLocaleLowerCase() === 'baidu-image' && candidate?.url);
    if (!fallback) throw new Error('没有可用的 IGDB 备用图片');
    downloadingCandidateUrl = fallback.url;
    const downloaded = await invoke('downloadArtworkCandidate', { executable: game.primaryExecutable, type: requestType, url: fallback.url });
    if(!current())return;
    artworkDraft[type] = { state: 'set', asset: {
      path: downloaded.path, file: downloaded.path, width: downloaded.width, height: downloaded.height,
      provider: fallback.provider || 'playnite-igdb', source: 'automatic-fallback'
    }};
    artworkDraft.dirty[type] = true;
    renderArtworkDraft();
    showNotice(`${label}的 Steam 图片预览失败，已自动切换为 ${fallback.provider === 'playnite-igdb' ? 'IGDB' : '备用'} 图片；点击“保存内容”应用。`, false, libraryNotice);
  } catch (error) {
    if(current()){
      slot.previewError=label+'加载失败，可重新搜索或指定图片';renderArtworkDraft();
      if (!String(error?.message || error).includes('CANCELLED')) showNotice(`${label}的 Steam 图片不可用，IGDB 备用图片也未获取成功。`, true, libraryNotice);
    }
  } finally {
    if(token===artworkTaskToken){
      artworkTaskInFlight=false;downloadingCandidateUrl='';
      if(target._artworkFallbackToken===token){
        target._artworkFallbackToken=null;target._editorArtworkProjection=null;
        if(epoch===editPresentationEpoch&&selectedActionGame&&keyFor(selectedActionGame)===key&&normalizedUiPath(selectedActionGame.primaryExecutable||'')===executable)renderArtworkDraft();
      }
    }
  }
}
function bindArtworkImageFallback(target, label, type) {
  const image=target.querySelector('img');if(!image)return;
  const epoch=editPresentationEpoch,key=keyFor(selectedActionGame||{}),executable=normalizedUiPath(selectedActionGame?.primaryExecutable||'');
  const fail=()=>{
    if(epoch!==editPresentationEpoch||!selectedActionGame||keyFor(selectedActionGame)!==key||normalizedUiPath(selectedActionGame.primaryExecutable||'')!==executable||!image.isConnected||target.querySelector('img')!==image)return;
    if(!libraryRenderAllowed()){target._deferredArtworkError=fail;editorArtworkRenderPending=true;return;}
    target._deferredArtworkError=null;
    if(artworkSlotIsSteamPreview(type)){
      target.innerHTML=`<span class="artwork-empty">${escapeHtml(label)}预览失败，正在切换 IGDB</span>`;
      void downloadIgdbArtworkFallback(target,label,type);
    }else{
      artworkDraft[type].previewError=label+'加载失败，可重新指定';renderArtworkDraft();
    }
  };
  image.addEventListener('error',fail,{once:true});
}
function editorArtworkProjection(kind,value){
  return JSON.stringify([editPresentationEpoch,keyFor(selectedActionGame||{}),normalizedUiPath(selectedActionGame?.primaryExecutable||''),kind,value]);
}
function replaceEditorArtworkMarkup(target,kind,markup,type){
  const projection=editorArtworkProjection(kind,markup);
  if(target._artworkFallbackToken!=null&&target._artworkFallbackToken!==artworkTaskToken)target._editorArtworkProjection=null;
  if(target._editorArtworkProjection!==projection){
    const video=target.querySelector('video');if(video){video.pause();video.removeAttribute('src');video.load();}
    target._editorArtworkProjection=projection;target._deferredArtworkError=null;target._artworkFallbackToken=null;
    target.innerHTML=markup;
    if(type&&artworkDraft[type]?.state!=='deleted')bindArtworkImageFallback(target,artworkLabel(type),type);
  }
  const deferred=target._deferredArtworkError;
  if(deferred&&libraryRenderAllowed()){target._deferredArtworkError=null;deferred();}
}
function artworkSlotMarkup(slot, type) {
  const label = artworkLabel(type);
  if (slot?.state === "deleted") return `<span class="artwork-empty artwork-deleted">${label}已删除，保存后不自动补回</span>`;
  if(slot?.previewError)return `<span class="artwork-empty">${escapeHtml(slot.previewError)}</span>`;
  return artworkDropMarkup(slot?.asset || null, label);
}
function renderArtworkDraft() {
  if(!selectedActionGame)return;
  if(!libraryRenderAllowed()){editorArtworkRenderPending=true;return;}
  editorArtworkRenderPending=false;
  for(const type of ['cover','long','wallpaper']){
    const target=$(`#artwork-${type}-preview`);
    if(!target){reportUiError('artwork-preview-missing',new Error(`缺少素材预览节点：${type}`));continue;}
    if(target.dataset.artworkType!==type)target.dataset.artworkType=type;
    if(target.dataset.editRow!==`artwork-${type}`)target.dataset.editRow=`artwork-${type}`;
    const label=`${artworkLabel(type)}操作，按 A 打开`;if(target.getAttribute('aria-label')!==label)target.setAttribute('aria-label',label);
    if(type!=='wallpaper')replaceEditorArtworkMarkup(target,'slot:'+type,artworkSlotMarkup(artworkDraft[type],type),type);
  }
  renderSharedWallpaperPreview();
}
function resumeEditorArtworkPresentation(){
  if(editorArtworkRenderPending&&selectedActionGame&&!$('#edit-modal').classList.contains('hidden')&&libraryRenderAllowed())renderArtworkDraft();
  syncSharedWallpaperPreviewActivity();
}
function openArtworkActionMenu(type) {
  if (!selectedActionGame) return false;
  artworkActionType = ['cover', 'long', 'wallpaper'].includes(type) ? type : 'cover';
  const label = artworkLabel(artworkActionType);
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
  if(type==='wallpaper'&&(sharedWallpaperMode()==='dynamic'||selectedActionGame?.mediaOnly))return actOnSharedWallpaper(action);
  if (action === 'search') return searchArtwork(type);
  if (action === 'replace') return promptArtworkPath(type);
  if (action === 'delete') return clearArtwork(type);
  return false;
}
function artworkSearchQuery(game) { return gameDisplayName(game) || String(game.primaryExecutable || '').split(/[\\/]/).pop().replace(/\.exe$/i, ''); }
function formalSteamAppId(game) { if(game?.mediaOnly)return String(game.gameMedia?.appId||''); if (isNativeSteamGame(game)) { const native = game?.steam?.nativeSteamAppId || game?.nativeSteamAppId || game?.steam?.steamStoreAppId || ''; const nativeText = String(native ?? '').trim(); return /^[1-9]\d*$/.test(nativeText) ? nativeText : ''; } if (game?.override?.idCleared === true) return ''; const value = game.override?.steamId || game.steam?.steamStoreAppId || game.steam?.storefrontAppId || game.steam?.appId || game.steam?.canonicalAppId || game.steam?.localAppId || game.steam?.requestedAppId || ''; const text = String(value ?? '').trim(); return /^[1-9]\d*$/.test(text) ? text : ''; }
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
function artworkPreviewWarmupCurrent(key,result){
  return libraryRenderAllowed()&&selectedActionGame&&!$('#edit-modal').classList.contains('hidden')&&artworkPrefetchCache.get(key)?.result===result&&['cover','long','wallpaper'].some(slot=>artworkSearchCacheKey(selectedActionGame,artworkSearchTypeForSlot(slot))===key);
}
function cancelArtworkPreviewWarmups(){
  for(const task of artworkPreviewWarmups.values()){
    if(task.idle&&typeof window.cancelIdleCallback==='function')window.cancelIdleCallback(task.handle);else clearTimeout(task.handle);
  }
  artworkPreviewWarmups.clear();
}
function warmArtworkPreviewCache(result,key){
  const entry=artworkPrefetchCache.get(key);
  if(!entry||entry.warmed||artworkPreviewWarmups.has(key)||!artworkPreviewWarmupCurrent(key,result))return;
  const candidates=Array.isArray(result?.candidates)?result.candidates:[],urls=[...new Set(candidates.slice(0,ARTWORK_PREFETCH_MAX_PRELOADS).map(candidate=>candidate?.previewUrl||candidate?.url).filter(url=>typeof url==='string'&&/^https?:/i.test(url)))];
  if(!urls.length){entry.warmed=true;return;}
  const task={handle:0,idle:typeof window.requestIdleCallback==='function'};
  const preload=()=>{
    if(artworkPreviewWarmups.get(key)!==task)return;artworkPreviewWarmups.delete(key);
    if(!artworkPreviewWarmupCurrent(key,result)||artworkPrefetchCache.get(key)!==entry)return;
    entry.warmed=true;for(const url of urls){const image=new Image();image.decoding='async';image.loading='eager';image.src=url;}
  };
  artworkPreviewWarmups.set(key,task);
  task.handle=task.idle?window.requestIdleCallback(preload,{timeout:1200}):window.setTimeout(preload,0);
}
function resumeArtworkPreviewWarmups(){
  if(!selectedActionGame||$('#edit-modal').classList.contains('hidden'))return;
  for(const slot of ['cover','long','wallpaper']){const key=artworkSearchCacheKey(selectedActionGame,artworkSearchTypeForSlot(slot)),entry=artworkPrefetchCache.get(key);if(artworkPrefetchEntryFresh(entry)&&entry.result)warmArtworkPreviewCache(entry.result,key);}
}
function discardArtworkPrefetchEntry(key) {
  const task = artworkPreviewWarmups.get(key);
  if (task) {
    if (task.idle && typeof window.cancelIdleCallback === 'function') window.cancelIdleCallback(task.handle);
    else clearTimeout(task.handle);
    artworkPreviewWarmups.delete(key);
  }
  artworkPrefetchCache.delete(key);
}
function touchArtworkPrefetchEntry(key, entry) {
  artworkPrefetchCache.delete(key);
  artworkPrefetchCache.set(key, entry);
}
function pruneArtworkPrefetchCache() {
  const now = Date.now();
  let settled = 0;
  for (const [key, entry] of artworkPrefetchCache) {
    if (entry.pending) continue;
    if (now - entry.completedAt > ARTWORK_PREFETCH_TTL_MS) discardArtworkPrefetchEntry(key);
    else settled++;
  }
  if (settled <= ARTWORK_PREFETCH_MAX_SETTLED) return;
  const protectedKeys = new Set(selectedActionGame && !$('#edit-modal')?.classList.contains('hidden')
    ? ['cover', 'long', 'wallpaper'].map(slot => artworkSearchCacheKey(selectedActionGame, artworkSearchTypeForSlot(slot))) : []);
  // Pending promises retain their waiters and same-key deduplication until settlement.
  for (const [key, entry] of artworkPrefetchCache) {
    if (settled <= ARTWORK_PREFETCH_MAX_SETTLED) break;
    if (entry.pending || protectedKeys.has(key)) continue;
    discardArtworkPrefetchEntry(key);
    settled--;
  }
}
function artworkPrefetchBackoffActive(entry) {
  return Boolean(entry?.error && Date.now() < entry.retryAt);
}
function artworkWorkerKey(request) {
  return `${normalizedUiPath(request.executable) || 'query:' + String(request.query || '')}|${request.type || 'cover'}`;
}
function retireArtworkSearchReads(request = null) {
  const workerKey = request ? artworkWorkerKey(request) : null;
  // Native jobs replace reads by EXE/type, and cancelArtwork cancels all artwork jobs.
  // Retire only read receipts; accepted downloads, saves and Steam commits keep their owners.
  for (const [key, entry] of artworkPrefetchCache) {
    if (entry.pending && (workerKey === null || entry.workerKey === workerKey)) discardArtworkPrefetchEntry(key);
  }
  for (const [id, item] of pending) {
    if (item.command !== 'searchArtwork' || (workerKey !== null && item.artworkWorkerKey !== workerKey)) continue;
    pending.delete(id);
    if (item.timer) clearTimeout(item.timer);
    item.reject(new Error('ERROR_CANCELLED: 素材任务已取消或被新请求替代'));
  }
}
function ensureArtworkSearch(game, type, { automatic = false } = {}) {
  pruneArtworkPrefetchCache();
  const key = artworkSearchCacheKey(game, type);
  const existing = artworkPrefetchCache.get(key);
  if (artworkPrefetchEntryFresh(existing)) {
    touchArtworkPrefetchEntry(key, existing);
    if (existing.result) warmArtworkPreviewCache(existing.result, key);
    return existing.pending || Promise.resolve(existing.result);
  }
  if (automatic && artworkPrefetchBackoffActive(existing)) return Promise.reject(new Error(existing.error));
  if (existing) discardArtworkPrefetchEntry(key);
  const searchRequest = artworkSearchRequest(game, type);
  retireArtworkSearchReads(searchRequest);
  const entry = { key, workerKey: artworkWorkerKey(searchRequest), startedAt: Date.now(), completedAt: 0, result: null, error: null, pending: null,
    failureCount: existing?.failureCount || 0, retryAt: 0 };
  // The native read worker has a 180s budget. Release a lost IPC receipt without replaying it.
  const request = invoke('searchArtwork', searchRequest, ARTWORK_SEARCH_TIMEOUT_MS);
  entry.pending = request.then(result => {
    if (artworkPrefetchCache.get(key) === entry) {
      entry.result = result;
      entry.completedAt = Date.now();
      entry.pending = null;
      entry.error = null;
      entry.failureCount = 0;
      touchArtworkPrefetchEntry(key, entry);
      pruneArtworkPrefetchCache();
      warmArtworkPreviewCache(result, key);
    }
    return result;
  }).catch(error => {
    if (artworkPrefetchCache.get(key) === entry) {
      entry.pending = null;
      entry.completedAt = Date.now();
      entry.error = String(error?.message || error || '素材搜索失败');
      const cancelled = /CANCELLED|取消/i.test(entry.error);
      entry.failureCount = cancelled ? 0 : Math.min(5, entry.failureCount + 1);
      entry.retryAt = cancelled ? 0 : entry.completedAt + Math.min(ARTWORK_PREFETCH_TTL_MS,
        ARTWORK_PREFETCH_RETRY_BASE_MS * 2 ** (entry.failureCount - 1));
      touchArtworkPrefetchEntry(key, entry);
      pruneArtworkPrefetchCache();
    }
    throw error;
  });
  artworkPrefetchCache.set(key, entry);
  return entry.pending;
}
function startArtworkPrefetch(game) {
  if (!game) return;
  pruneArtworkPrefetchCache();
  for (const slot of ['cover', 'long', 'wallpaper']) {
    const type = artworkSearchTypeForSlot(slot);
    const key = artworkSearchCacheKey(game, type);
    const cached = artworkPrefetchCache.get(key);
    if (artworkPrefetchEntryFresh(cached)) {
      touchArtworkPrefetchEntry(key, cached);
      if (cached.result) warmArtworkPreviewCache(cached.result, key);
      continue;
    }
    if (artworkPrefetchBackoffActive(cached)) continue;
    artworkPrefetchPendingCount++;
    ensureArtworkSearch(game, type, { automatic: true }).catch(() => {}).finally(() => {
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
  return { cover: slotFor("cover"), long: slotFor("long"), wallpaper: slotFor("wallpaper"), dirty: { cover: false, long: false, wallpaper: false } };
}
function openArtworkEditor() { if (!selectedActionGame) return; const artworkId = artworkSearchSteamId(selectedActionGame); $('#artwork-search-hint').textContent = artworkId ? `已确认 Steam AppID ${artworkId}` : `搜索词：${artworkSearchQuery(selectedActionGame)}（优先产品名称，其次 EXE）`; renderArtworkDraft(); $('#artwork-status').classList.add('hidden'); $('#edit-modal').classList.remove('hidden'); startArtworkPrefetch(selectedActionGame); }
async function promptArtworkPath(type) { if (!selectedActionGame) return; try { const result = await invoke('pickArtwork', { type }); if (result?.cancelled || !result?.path) return; artworkDraft[type] = { state: "set", asset: { path: result.path, file: result.path } }; artworkDraft.dirty[type] = true; renderArtworkDraft(); } catch (error) { showError(error, libraryNotice); } }
function clearArtwork(type) { artworkDraft[type] = { state: "deleted", asset: null }; artworkDraft.dirty[type] = true; renderArtworkDraft(); showNotice(`${artworkLabel(type)}已标记删除；点击“保存内容”后应用。`, false, libraryNotice); }
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
function artworkCandidateKey(candidate) { return `${candidate?.provider || ''}|${candidate?.url || candidate?.previewUrl || ''}`; }
function artworkCandidateIsSteam(candidate) { return ['steam-store', 'steam-cdn', 'steam'].includes(String(candidate?.provider || '').toLocaleLowerCase()); }
function artworkCandidateFallback(candidate) {
  const failedProvider = String(candidate?.provider || '').toLocaleLowerCase();
  const candidates = artworkCandidates.filter(item => {
    if (!item?.url || artworkUnavailableUrls.has(artworkCandidateKey(item))) return false;
    const provider = String(item.provider || '').toLocaleLowerCase();
    if (artworkCandidateIsSteam(candidate)) return provider === 'playnite-igdb' || provider === 'baidu-image';
    if (failedProvider === 'playnite-igdb') return provider === 'baidu-image';
    return false;
  });
  const rank = item => String(item?.provider || '').toLocaleLowerCase() === 'playnite-igdb' ? 0 : 1;
  return candidates.sort((left, right) => rank(left) - rank(right) || Number(right?.preferred) - Number(left?.preferred) || Number(right?.score || 0) - Number(left?.score || 0))[0] || null;
}
function artworkCandidateCard(candidate) {
  const key = artworkCandidateKey(candidate);
  return [...document.querySelectorAll('#artwork-search-list .artwork-candidate')]
    .find(item => item.dataset.candidateKey === key) || null;
}
function artworkSearchContext() {
  return { token: artworkTaskToken, epoch:silentRefreshEpoch, executable: selectedActionGame?.primaryExecutable || '', slot: artworkSearchSlot, type: artworkSearchType };
}
function artworkSearchContextCurrent(context) {
  const modal=$('#artwork-search-modal');
  return Boolean(modal?.isConnected && context && !silentRefreshClosed && context.epoch===silentRefreshEpoch && context.token === artworkTaskToken && context.executable === (selectedActionGame?.primaryExecutable || '') &&
    context.slot === artworkSearchSlot && context.type === artworkSearchType && !modal.classList.contains("hidden"));
}
let artworkSearchPresentationOwner=null,artworkSearchFocusFrame=0;
function cancelArtworkSearchFocus(){if(artworkSearchFocusFrame)cancelAnimationFrame(artworkSearchFocusFrame);artworkSearchFocusFrame=0;}
function retireArtworkSearchPresentation(hide=false){
  artworkSearchPresentationOwner=null;cancelArtworkSearchFocus();
  if(hide){artworkCandidates=[];$('#artwork-search-modal')?.classList.add('hidden');}
}
function artworkSearchPresentationCurrent(owner){return Boolean(owner&&owner===artworkSearchPresentationOwner&&!silentRefreshClosed&&owner.epoch===silentRefreshEpoch&&owner.current());}
function beginArtworkSearchPresentation(current){retireArtworkSearchPresentation();return artworkSearchPresentationOwner={epoch:silentRefreshEpoch,current,render:null,focusTarget:null,onError:null};}
function presentArtworkSearch(owner=artworkSearchPresentationOwner){
  if(!artworkSearchPresentationCurrent(owner)||!libraryRenderAllowed()||!owner.render)return;
  const render=owner.render;owner.render=null;
  try{render();}catch(error){const fallback=owner.onError;owner.onError=null;if(fallback)fallback(error);else showError(error,libraryNotice);}
}
function queueArtworkSearchPresentation(owner,render){if(!artworkSearchPresentationCurrent(owner))return false;owner.render=render;presentArtworkSearch(owner);return true;}
function requestArtworkSearchFocus(target,owner=artworkSearchPresentationOwner,context=artworkSearchContext()){
  if(!target)return;if(owner)owner.focusTarget=target;cancelArtworkSearchFocus();
  const current=()=>owner?artworkSearchPresentationCurrent(owner)&&owner.focusTarget===target:artworkSearchContextCurrent(context);
  if(!current()||!libraryRenderAllowed())return;
  artworkSearchFocusFrame=requestAnimationFrame(()=>{artworkSearchFocusFrame=0;if(!current()||!libraryRenderAllowed()||activeWorkspaceModal()!==$('#artwork-search-modal')||!target.isConnected)return;if(owner)owner.focusTarget=null;target.focus({preventScroll:true});});
}
async function fallbackArtworkCandidate(candidate, card, reason = 'preview', context = artworkSearchContext()) {
  // Loading a thumbnail is not user consent to download or apply another image.
  // Old cards may dispatch errors after a new game/slot has already been opened.
  if (!artworkSearchContextCurrent(context) || !artworkCandidates.includes(candidate) || (card && !card.isConnected)) return false;
  const key = artworkCandidateKey(candidate);
  if (!key || artworkUnavailableUrls.has(key)) return false;
  const distinctPreview = reason === 'preview' && candidate.previewUrl && candidate.previewUrl !== candidate.url;
  if (!distinctPreview) artworkUnavailableUrls.add(key);
  if (card) {
    card.disabled = !distinctPreview;
    card.classList.add('unavailable');
    card.setAttribute('aria-disabled', String(!distinctPreview));
    const preview = card.querySelector('.artwork-candidate-preview');
    if (preview) preview.textContent = distinctPreview ? '预览失败，仍可点击下载原图' : artworkCandidateIsSteam(candidate) ? 'Steam 图片不可用' : '图片候选不可用';
  }
  const fallback = artworkCandidateFallback(candidate);
  if (!fallback) {
    showNotice('当前图片不可用；可选择其他候选，或返回后指定图片。', true, libraryNotice);
    return false;
  }
  const label = artworkProviderLabel(fallback.provider);
  const fallbackCard = artworkCandidateCard(fallback);
  if (fallbackCard && !fallbackCard.disabled) fallbackCard.focus({ preventScroll: true });
  showNotice(`${artworkCandidateIsSteam(candidate) ? 'Steam' : '当前'}图片${reason === 'preview' ? '预览' : '下载'}失败，已切换到${label}候选，请点击选择。`, false, libraryNotice);
  return true;
}
function renderArtworkCandidates(result,owner=artworkSearchPresentationOwner) {
  const context = artworkSearchContext();
  artworkCandidates = Array.isArray(result?.candidates) ? result.candidates.filter(candidate => candidate?.url || candidate?.previewUrl) : [];
  artworkUnavailableUrls.clear();
  $("#artwork-search-title").textContent = `选择${artworkLabel(artworkSearchSlot)}`;
  $("#artwork-search-summary").textContent = `${result?.query || artworkSearchQuery(selectedActionGame)} · ${artworkCandidates.length} 个候选 · ${result?.filterInstruction || "已过滤过小图片"}`;
  const list = $("#artwork-search-list"); list.dataset.artworkType = artworkSearchSlot; list.replaceChildren();
  $("#artwork-search-empty").classList.toggle("hidden", artworkCandidates.length !== 0);
  for (const candidate of artworkCandidates) {
    const card = document.createElement("button"); card.type = "button"; card.className = "artwork-candidate"; card.tabIndex = 0;
    card.dataset.candidateKey = artworkCandidateKey(candidate);
    const dimensions = `${Number(candidate.width || 0)}×${Number(candidate.height || 0)}`;
    card.innerHTML = `<span class="artwork-candidate-preview"><img src="${escapeHtml(candidate.previewUrl || candidate.url)}" alt="候选图片" loading="lazy"></span><strong class="artwork-candidate-title">${escapeHtml(candidate.title || artworkCandidateFallbackTitle(candidate))}</strong><span class="artwork-candidate-meta"><span>${escapeHtml(artworkProviderLabel(candidate.provider))}</span><span>${dimensions}</span><span>${candidate.preferred ? "优选" : "合格"}</span></span>`;
    card.addEventListener("click", () => applyArtworkCandidate(candidate, card, context));
     card.addEventListener("keydown", event => {
       if (event.target !== card) return;
       if (event.key === "Enter" || event.key === " ") {
         event.preventDefault();
         event.stopPropagation();
         applyArtworkCandidate(candidate, card, context);
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
     const image = card.querySelector("img");
     image?.addEventListener("load", () => {
       // A Steam "_2x" filename is not proof of doubled pixel dimensions.
       if (!artworkSearchContextCurrent(context) || (candidate.previewUrl && candidate.previewUrl !== candidate.url)) return;
       const dimensionsLabel = card.querySelector(".artwork-candidate-meta")?.children[1];
       if (dimensionsLabel && image.naturalWidth > 0 && image.naturalHeight > 0) dimensionsLabel.textContent = `${image.naturalWidth}×${image.naturalHeight}`;
     }, { once: true });
     image?.addEventListener("error", () => { fallbackArtworkCandidate(candidate, card, 'preview', context).catch(error => { if (artworkSearchContextCurrent(context)) showError(error, libraryNotice); }); }, { once: true });
    list.appendChild(card);
  }
  requestArtworkSearchFocus(list.querySelector(".artwork-candidate"),owner,context);
}
async function applyArtworkCandidate(candidate, card, galleryContext) {
  if (!selectedActionGame || !candidate?.url || downloadingCandidateUrl || !card?.isConnected ||
    !artworkCandidates.includes(candidate) || artworkUnavailableUrls.has(artworkCandidateKey(candidate))) return false;
  // A failed download advances the write token; other cards in this same gallery remain selectable.
  if (!galleryContext || !artworkSearchContextCurrent({ ...galleryContext, token: artworkTaskToken })) return false;
  const context = artworkSearchContext();
  const token = ++artworkTaskToken;
  context.token = token;
  artworkTaskInFlight = true;
  downloadingCandidateUrl = candidate.url; card?.classList.add("loading"); card?.setAttribute("aria-busy", "true");
  try {
    const result = await invoke("downloadArtworkCandidate", { executable: context.executable, type: context.type, url: candidate.url });
    if (!artworkSearchContextCurrent(context)) return false;
    if (!result?.path) throw new Error('图片下载没有返回有效文件，请选择其他候选。');
    artworkDraft[context.slot] = { state: "set", asset: { path: result.path, file: result.path, width: result.width, height: result.height, provider: candidate.provider || "baidu-image", source: "search" } };
    artworkDraft.dirty[context.slot] = true;
    $("#artwork-search-modal").classList.add("hidden"); renderArtworkDraft();
    showNotice(artworkLabel(context.slot) + "已下载，请点击“保存内容”。", false, libraryNotice);
    return true;
  } catch (error) {
    if (artworkSearchContextCurrent(context) && !String(error?.message || error).includes("CANCELLED")) {
      const switched = await fallbackArtworkCandidate(candidate, card, 'download', context);
      if (!switched) showError(error, libraryNotice);
    }
    return false;
  } finally {
    // An older/cancelled request must not clear a newer request's busy guard.
    if (token === artworkTaskToken) { artworkTaskInFlight = false; downloadingCandidateUrl = ""; }
    card?.classList.remove("loading"); card?.removeAttribute("aria-busy");
  }
}
async function searchArtwork(type) {
  document.getElementById('artwork-video-quality-row')?.classList.add('hidden');
  if (!selectedActionGame) { showNotice("请先打开一个游戏卡片的编辑页面。", true, libraryNotice); return; }
  const token = ++artworkTaskToken;
  artworkTaskInFlight = true;
  artworkSearchSlot = ['cover', 'long', 'wallpaper'].includes(type) ? type : 'cover';
  artworkSearchType = artworkSearchTypeForSlot(artworkSearchSlot);
  document.querySelector('#artwork-search-modal .eyebrow').textContent='图片候选';
  $("#artwork-search-title").textContent = `选择${artworkLabel(artworkSearchSlot)}`;
  $("#artwork-search-summary").textContent = "正在搜索：" + artworkSearchQuery(selectedActionGame) + " · 弱网下仍可返回";
  $("#artwork-search-list").dataset.artworkType = artworkSearchSlot;
  $("#artwork-search-list").innerHTML = '<div class="search-loading"><strong>正在获取候选图片</strong><span>搜索完成后可直接选择素材</span></div>';
  $("#artwork-search-empty").classList.add("hidden");
  $("#artwork-search-modal").classList.remove("hidden");
  const game=selectedActionGame,context=artworkSearchContext(),key=artworkSearchCacheKey(game,artworkSearchType),owner=beginArtworkSearchPresentation(()=>artworkSearchContextCurrent(context)&&artworkSearchCacheKey(selectedActionGame,context.type)===key);
  try { const result = await ensureArtworkSearch(game, artworkSearchType); queueArtworkSearchPresentation(owner,()=>renderArtworkCandidates(result,owner)); }
  catch (error) { if (!String(error?.message || error).includes("CANCELLED")) queueArtworkSearchPresentation(owner,()=>{ $("#artwork-search-list").innerHTML = '<div class="search-loading error">搜索失败，可返回后改用“指定图片”</div>'; showError(error, libraryNotice); }); }
  finally { if (token === artworkTaskToken) artworkTaskInFlight = false; }
}
function cancelArtworkTask() { retireArtworkSearchPresentation();artworkTaskToken++; artworkTaskInFlight = false; downloadingCandidateUrl = ""; $("#artwork-search-modal")?.classList.add("hidden"); invoke("cancelArtwork").catch(() => {}); if(artworkSearchType==='dynamic'&&selectedActionGame)renderSharedWallpaperPreview(); }
function syncArtworkDraftToEdit() {
  if (!selectedActionGame || !editDraft?.game) return;
  selectedActionGame.override = { ...(selectedActionGame.override || {}) };
  selectedActionGame.override.artworkProtection = { ...(selectedActionGame.override.artworkProtection || {}) };
  for (const type of ["cover", "long", "wallpaper"]) {
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
  editDraft.artworkDirty = editDraft.artworkDirty || artworkDraft.dirty.cover || artworkDraft.dirty.long || artworkDraft.dirty.wallpaper;
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
function preserveNewerEditorMedia(next) {
  if(normalizedUiPath(next?.dataRoot)!==normalizedUiPath(snapshot?.dataRoot))return next;
  const incoming=next?.gameMedia?.entries,current=snapshot?.gameMedia?.entries;
  if(!isWorkspaceRecord(incoming)||!isWorkspaceRecord(current))return next;
  let merged=null;
  for(const [key,entry] of Object.entries(incoming)){
    const prior=current[key];
    if(entry?.key===key&&prior?.key===key&&Number(prior.bindingRevision||0)>Number(entry.bindingRevision||0)){
      merged||={...incoming};merged[key]=prior;
    }
  }
  return merged?{...next,gameMedia:{...next.gameMedia,entries:merged}}:next;
}
function rebaseSavedEditor(submitted,saved,refreshed,fields) {
  if(editDraft!==submitted||$('#edit-modal').classList.contains('hidden'))return;
  const nameLater=draftName()!==fields.name||submitted.nameEdits!==saved.nameEdits;
  const idLater=draftSteamId()!==fields.steamId||submitted.steamIdEdits!==saved.steamIdEdits;
  syncIdentityDraft();syncArtworkDraftToEdit();
  const primaryLater=normalizedUiPath(submitted.game.primaryExecutable)!==normalizedUiPath(saved.game.primaryExecutable);
  const bucketLater=submitted.bucket!==saved.bucket;
  const changed={};
  for(const type of ['cover','long','wallpaper'])changed[type]=
    JSON.stringify(submitted.game.override?.[type])!==JSON.stringify(saved.game.override?.[type])||
    submitted.game.override?.artworkProtection?.[type]!==saved.game.override?.artworkProtection?.[type]||
    submitted.artworkChanged[type]&&!saved.artworkChanged[type];
  const media=submitted.game.gameMedia;
  if(media?.key&&refreshed.gameMedia?.key===media.key&&Number(media.bindingRevision||0)>Number(refreshed.gameMedia.bindingRevision||0)){
    // Preferences saved independently while the editor batch was pending
    // remain authoritative over its older whole-library snapshot.
    refreshed={...refreshed,gameMedia:structuredClone(media)};
    snapshot.gameMedia.entries[media.key]=structuredClone(media);
  }
  const base=draftFor(refreshed);
  if(!nameLater&&!idLater&&!primaryLater&&!bucketLater&&!Object.values(changed).some(Boolean))editDraft=base;
  else{
    const copy=(target,source,key)=>{if(Object.prototype.hasOwnProperty.call(source||{},key))target[key]=structuredClone(source[key]);else delete target[key];};
    submitted.original=base.original;submitted.originalBucket=base.originalBucket;submitted.identityBaseline=base.identityBaseline;
    submitted.nameEdits=nameLater?1:0;submitted.steamIdEdits=idLater?1:0;
    if(!nameLater)copy(submitted.game.override,refreshed.override,'name');
    if(!idLater)for(const field of ['steamId','igdbId','idCleared'])copy(submitted.game.override,refreshed.override,field);
    if(!primaryLater){submitted.game.primaryExecutable=refreshed.primaryExecutable;submitted.game.steam=structuredClone(refreshed.steam||{});}
    if(!bucketLater)submitted.bucket=base.bucket;
    submitted.game.override.artworkProtection||={};
    for(const type of ['cover','long','wallpaper']){
      if(!changed[type]){copy(submitted.game.override,refreshed.override,type);copy(submitted.game.override.artworkProtection,refreshed.override?.artworkProtection,type);}
      submitted.artworkChanged[type]=changed[type];
    }
    if(!Object.keys(submitted.game.override.artworkProtection).length)delete submitted.game.override.artworkProtection;
    submitted.artworkDirty=Object.values(changed).some(Boolean);
    submitted.primaryDirty=normalizedUiPath(submitted.game.primaryExecutable)!==normalizedUiPath(refreshed.primaryExecutable);
    submitted.identityDirty=nameLater||idLater;
  }
  selectedActionGame=editDraft.game;
  if(!nameLater)$('#identity-name').value=editorIdentityName(selectedActionGame);
  if(!idLater)$('#identity-steam-id').value=formalSteamAppId(selectedActionGame);
  steamIdClearPending=selectedActionGame.override?.idCleared===true;
  artworkDraft=artworkDraftFromGame(selectedActionGame);renderEditDraft(selectedActionGame);
}
async function persistEditDraft() {
  if(!editDraft?.game)return false;
  const submitted=editDraft,key=keyFor(submitted.game);
  if(editorSaveTasks.has(key))return false;
  syncIdentityDraft();syncArtworkDraftToEdit();
  // The accepted submission owns its values even after Back clears the form.
  const saved=structuredClone(submitted),game=saved.game,original=saved.original;
  const submittedFields={name:draftName(),steamId:draftSteamId()};
  const operations=[],add=(command,args)=>operations.push({command,arguments:args});
  const primaryChanged=normalizedUiPath(game.primaryExecutable)!==normalizedUiPath(original.primaryExecutable);
  if(primaryChanged)add('setPrimary',{gameDirectory:game.gameDirectory,executable:game.primaryExecutable,previousExecutable:original.primaryExecutable||''});
  const originalName=original.override?.name||'',name=game.override?.name||'';
  if(name&&name!==originalName)add('saveManualName',{executable:game.primaryExecutable,name});
  const originalId=String(formalSteamAppId(original)||''),id=game.override?.idCleared?'':String(game.override?.steamId||'');
  const igdbId=game.override?.idCleared?Number(game.override?.igdbId||0):0;
  if(game.override?.idCleared&&originalId)add('clearManualSteamId',{executable:game.primaryExecutable});
  if(id&&(id!==originalId||saved.steamIdEdits>0&&id!==String(original.override?.steamId||'')))add('identifyOne',{executable:game.primaryExecutable,manualName:name,steamId:id});
  else if(igdbId&&igdbId!==Number(original.override?.igdbId||0))add('identifyOne',{executable:game.primaryExecutable,manualName:name,igdbId:String(igdbId)});
  if(saved.artworkDirty){
    const args={executable:game.primaryExecutable};
    for(const type of ['cover','long','wallpaper'])if(saved.artworkChanged[type]){
      const value=game.override?.[type]?.path||game.override?.[type]?.file||game.override?.[type]?.portableFile||game.override?.[type]?.url||'';
      args[type+'Path']=value;args['clear'+type[0].toUpperCase()+type.slice(1)]=!value;
    }
    add('saveArtworkOverride',args);
  }
  if(saved.bucket!==saved.originalBucket)add('setBucket',{gameDirectory:game.gameDirectory,bucket:saved.bucket});
  if(!operations.length)return true;
  editorSaveTasks.set(key,{phase:'running',label:'正在保存，可返回后继续处理'});editSaveInFlight=true;refreshCardOperationState();
  try{
    snapshot=preserveNewerEditorMedia(await invoke('saveEditor',{operations,refreshNativeArtwork:Boolean(saved.artworkDirty&&isNativeSteamGame(game))}));
    if(document.hasFocus()&&!document.hidden&&!$('#library-view').classList.contains('hidden'))renderLibrary();else silentRenderPending=true;
    const refreshed=mergedGames().find(item=>normalizedUiPath(item.primaryExecutable)===normalizedUiPath(game.primaryExecutable));
    if(refreshed)rebaseSavedEditor(submitted,saved,refreshed,submittedFields);
    return true;
  }catch(error){showError(error,libraryNotice);return false;}
  finally{editorSaveTasks.delete(key);editSaveInFlight=editorSaveTasks.size>0;refreshCardOperationState();}
}
function setBucketOptions(value) { const select = $('#edit-bucket-select'); if (select) { select.value = value || 'auto'; refreshWorkspaceDropdown(select); } }
function closeEdit() {
  ++editPresentationEpoch;nativeArtworkRefreshOwner=null;editorArtworkRenderPending=false;
  stopSharedWallpaperPreview();
  document.querySelectorAll('#edit-modal [data-controller-input]').forEach(finishControllerTextInput);
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
  artworkDraft = { cover: null, long: null, wallpaper: null, dirty: { cover: false, long: false, wallpaper: false } };
  editReturnCardDirectory = '';
  restoreEditReturnFocus(returnDirectory);
}
async function refreshSteamPlan() { try { snapshot = await invoke('plan', selectedTargetArguments()); renderEntry(); renderLibrary(); } catch (error) { showError(error, libraryNotice); } }
async function runBusy(command, title, detail, args = {}, success = '') { clearNotice(); setBusy(true, title, detail); try { const result = await invoke(command, args); if (command === 'closeSteam') { applySteamRuntimeState(result); } else { snapshot = result; renderEntry(); if (!$('#library-view').classList.contains('hidden')) renderLibrary(); } if (success) showNotice(success, false, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); return true; } catch (error) { const text = error?.message || String(error); const cancelled = text.includes('TASK_CANCELLED') || text.includes('ERROR_CANCELLED'); if (cancelled) { showNotice(/搜索游戏|搜索候选|识别真实游戏/.test(title) ? '已取消搜索，返回当前游戏。' : '已取消当前任务。', false, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); if (searchReturnToEdit && selectedActionGame) { searchReturnToEdit = false; $('#identity-search-modal')?.classList.add('hidden'); openEdit(selectedActionGame); } } else { if (['commit', 'deleteFromSteam', 'refreshSteamArtwork'].includes(command)) { try { applyLibrarySnapshot(await invoke('state')); } catch (refreshError) { reportUiError('refresh-after-steam-write-error', refreshError, { command }); } } showError(error, $('#library-view').classList.contains('hidden') ? notice : libraryNotice); } return false; } finally { cancelInFlight = false; setBusy(false); } }
async function startNonBlockingRefresh() {
  if (['starting', 'queued', 'running'].includes(manualRefreshProgress.phase)) return;
  clearNotice();
  manualRefreshProgress = {
    phase: 'starting', current: 0, total: 0,
    round: 1, roundLabel: '第一轮', roundCurrent: 0, roundTotal: 0
  };
  renderArtworkRefreshControl();
  setBusy(true, '正在启动手动刷新', '识别将在后台合并执行，页面保持可操作。');
  try {
    snapshot = await invoke('prepare');
    const request = snapshot?.scrapeRequest || {};
    manualRefreshProgress = {
      phase: request.queued ? 'queued' : 'starting',
      current: 0,
      total: Number(request.total || snapshot?.scrape?.total || 0),
      round: 1, roundLabel: '第一轮', roundCurrent: 0,
      roundTotal: Number(snapshot?.scrape?.roundTotal || request.total || 0)
    };
    renderArtworkRefreshControl();
    renderEntry();
    if (!$('#library-view').classList.contains('hidden')) renderLibrary();
    showNotice('手动刷新已启动，完成后自动更新页面。', false, libraryNotice);
  } catch (error) {
    manualRefreshProgress = { phase: 'failed', current: 0, total: 0, round: 1, roundLabel: '第一轮', roundCurrent: 0, roundTotal: 0 };
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
    // Open the page first; this bounded read-only query must not wait for the
    // background game scan or block navigation behind the busy overlay.
    void refreshSteamAccountDiscovery();
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
$('#directory-settings-toggle').addEventListener('click', () => { if(librarySidebarState()){librarySidebarPage('sources');return;}const panel = $('#directory-settings-panel'); if (!panel) return; const open = panel.classList.toggle('hidden') === false; if (open) closeLibraryMenuPanels('directory-settings-panel'); $('#directory-settings-toggle').setAttribute('aria-expanded', String(open)); });
$('#manual-game-settings-toggle').addEventListener('click', () => { if(librarySidebarState()){librarySidebarPage('manual');return;}const panel = $('#manual-game-panel'); if (!panel) return; const open = panel.classList.toggle('hidden') === false; if (open) closeLibraryMenuPanels('manual-game-panel'); $('#manual-game-settings-toggle').setAttribute('aria-expanded', String(open)); });
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
    void refreshSteamAccountDiscovery(true);
  } catch (error) {
    showError(error, libraryNotice);
  } finally {
    scanLibraryRequestInFlight = false;
    scanLibraryButton.disabled = false;
  }
});
$('#pick-root-button').addEventListener('click', () => pickAndAddLibraryPath('pickDirectory', 'addRoot', '正在添加扫描目录', '正在保存所选文件夹并扫描游戏库。', '扫描目录已保存。'));
$('#pick-game-button').addEventListener('click', () => pickAndAddLibraryPath('pickExecutable', 'addGame', '正在手动加入游戏', '正在读取所选 EXE 所在的游戏目录。', '游戏已加入，自动规则不会覆盖。'));
async function commitSelectedGames(target) {
  await runBusy('commit', '正在事务写入 Steam',
    '逐账户备份、暂存、写入并重新解析 shortcuts.vdf。', target,
    '已加入 Steam；快捷方式和图片已校验。需要时请手动启动 Steam。');
}
async function closeSteamOnly() {
  await runBusy('closeSteam', '正在关闭 Steam', '正在请求 Steam 保存数据并温和退出。', {}, 'Steam 已关闭，可以导入。');
  await refreshSteamRuntimeState(true);
}
function closeSteamCommit() {
  $('#steam-commit-modal').classList.add('hidden');
  pendingSteamCommitArguments = null;
  const trigger = $('#edit-modal').classList.contains('hidden') ? $('#commit-button') : $('#edit-add-to-steam');
  if (!trigger.disabled) focusWorkspaceElement(trigger);
}
async function requestSteamCommit(directories = selectedCommitDirectories()) {
  if (steamCommitRequestInFlight || !$('#steam-commit-modal').classList.contains('hidden')) return;
  steamCommitRequestInFlight = true;
  // Capture the exact selection before preflight; background scans and page
  // selection must not broaden a single-card request while it is confirmed.
  const target = selectedTargetArguments(directories);
  try {
    const preflight = await invoke('steamCommitPreflight');
    applySteamRuntimeState(preflight);
    if (preflight?.steamRunning) {
      const accepted = await askConfirm('Steam 正在运行',
        'Steam 正在运行，确认后只请求 Steam 温和退出，不会启动游戏；关闭完成后请再次点击加入按钮。',
        '关闭 Steam');
      if (accepted) await closeSteamOnly();
      return;
    }
    const count = target.selectedGameDirectories.length;
    if (!count) return showNotice('当前页面没有已选中的项目。', true, libraryNotice);
    $('#commit-summary').textContent = `将把 ${count} 个已选中项目同步到全部 Steam 大屏用户。`;
    $('#commit-target').textContent = '目标：所有已发现的 Steam 用户（逐账户事务写入）';
    pendingSteamCommitArguments = target;
    $('#steam-commit-modal').classList.remove('hidden');
    focusModalItem($('#steam-commit-modal'));
  } catch (error) { showError(error, libraryNotice); }
  finally { steamCommitRequestInFlight = false; }
}
$('#commit-button').addEventListener('click', () => requestSteamCommit());
async function joinEditedGameToSteam() {
  const game=selectedActionGame;
  if(!game||game.mediaOnly||isNativeSteamGame(game)||!game.primaryExecutable||!keyFor(game))return false;
  const key=keyFor(game);if(['queued','running'].includes(steamCommitTasks.get(key)?.phase))return false;
  const target=selectedTargetArguments([key]);
  steamCommitTasks.set(key,{phase:'queued',label:'排队加入 Steam'});refreshCardOperationState();
  try{
    // The native FIFO performs admission again at execution time. A running
    // Steam instance is reported on this card; it is never silently closed.
    snapshot=await invoke('commit',target);
    steamCommitTasks.set(key,{phase:'completed',label:'已加入 Steam'});
    if(document.hasFocus()&&!document.hidden&&!$('#library-view').classList.contains('hidden'))renderLibrary();else silentRenderPending=true;
    return true;
  }catch(error){steamCommitTasks.set(key,{phase:'failed',label:'加入失败：'+(error?.message||String(error))});return false;}
  finally{refreshCardOperationState();}
}
$('#edit-add-to-steam').addEventListener('click',joinEditedGameToSteam);
$('#steam-runtime-state')?.addEventListener('click', async () => {
  if (steamRuntimeState.running === true) await closeSteamOnly();
  else if (steamRuntimeState.running === false) await refreshSteamRuntimeState(true);
});
$('#steam-commit-close').addEventListener('click', closeSteamCommit); $('#steam-commit-cancel').addEventListener('click', closeSteamCommit); $('#steam-commit-confirm').addEventListener('click', async () => {
  const target = pendingSteamCommitArguments;
  closeSteamCommit();
  if (!target?.selectedGameDirectories.length) return;
  try {
    const preflight = await invoke('steamCommitPreflight');
    applySteamRuntimeState(preflight);
    if (preflight?.steamRunning) {
      const accepted = await askConfirm('Steam 正在运行',
        'Steam 在确认期间重新启动了，当前不能导入。确认后只关闭 Steam，关闭完成后请再次点击加入按钮。',
        '关闭 Steam');
      if (!accepted) return;
      await closeSteamOnly();
    } else await commitSelectedGames(target);
  } catch (error) { showError(error, libraryNotice); }
});
$('#delete-button').addEventListener('click', () => { const count = selectedSteamCount(); if (!count) return showNotice('当前页面没有已选中的 Steam 快捷方式。', true, libraryNotice); $('#delete-summary').textContent = `将从全部 Steam 大屏用户中删除 ${count} 个明确匹配的快捷方式。`; $('#steam-delete-modal').classList.remove('hidden'); });
$('#steam-delete-close').addEventListener('click', () => $('#steam-delete-modal').classList.add('hidden')); $('#steam-delete-cancel').addEventListener('click', () => $('#steam-delete-modal').classList.add('hidden')); $('#steam-delete-confirm').addEventListener('click', async () => { $('#steam-delete-modal').classList.add('hidden'); await runBusy('deleteFromSteam', '正在事务删除 Steam 快捷方式', '逐账户备份、暂存、删除并校验 shortcuts.vdf。', { ...selectedTargetArguments(), selectionMode: 'all-steam-accounts', selectedGameDirectories: selectedVisibleSteamDirectories() }); });
document.querySelectorAll('.page-tab').forEach(tab => tab.addEventListener('click', () => { closeLibraryMenuPanels(); activeTab = tab.dataset.tab; renderLibrary(); }));
$('#edit-back-button').addEventListener('click', closeEdit); $('#edit-folder').addEventListener('click', () => selectedActionGame && invoke('openFolder', { path: selectedActionGame.gameDirectory }).catch(error => showError(error, libraryNotice))); $('#artwork-cancel')?.addEventListener('click', cancelArtworkEditor); $('#artwork-save')?.addEventListener('click', confirmArtworkDraft); document.querySelectorAll('.artwork-pane[data-artwork-editable="true"]').forEach(pane => { const type = pane.dataset.artworkType; const preview = pane.querySelector('.artwork-dropzone'); preview?.addEventListener('click', () => openArtworkActionMenu(type)); preview?.addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); event.stopPropagation(); openArtworkActionMenu(type); } }); });
$('#artwork-action-close').addEventListener('click', () => closeArtworkActionMenu(true)); $('#artwork-action-search').addEventListener('click', () => artworkActionForCurrentType('search')); $('#artwork-action-replace').addEventListener('click', () => artworkActionForCurrentType('replace')); $('#artwork-action-delete').addEventListener('click', () => artworkActionForCurrentType('delete'));
$('#artwork-search-close').addEventListener('click', cancelArtworkTask); $('#artwork-search-cancel').addEventListener('click', cancelArtworkTask); $('#identity-search-cancel').addEventListener('click', cancelIdentitySearch);
$('#edit-primary-select').addEventListener('change', event => { if (!selectedActionGame || !event.target.value) return; selectedActionGame.primaryExecutable = event.target.value; editDraft.primaryDirty = true; renderEditDraft(selectedActionGame); showNotice('主程序已暂存；点击“保存内容”才会写入配置。', false, libraryNotice); });
$('#edit-bucket-select').addEventListener('change', event => { if (!selectedActionGame) return; editDraft.bucket = event.target.value; setBucketOptions(editDraft.bucket); renderEditDraft(selectedActionGame); showNotice('页面归类已暂存；点击“保存内容”才会写入配置。', false, libraryNotice); });
$('#identity-form').addEventListener('submit', async event => { event.preventDefault(); syncIdentityDraft(); await searchGame(); }); $('#identity-name-candidates').addEventListener('change', event => { const selected = event.target.selectedIndex > 0 ? event.target._nameRows?.[event.target.selectedIndex - 1] : null; if (selected?.name) { $('#identity-name').value = selected.name; if (selected.steamAppId) { $('#identity-steam-id').value = String(selected.steamAppId); ++editDraft.steamIdEdits; } syncIdentityDraft('name'); renderEditDraft(selectedActionGame); $('#identity-name').focus(); } event.target.value = ''; }); $('#identity-steam-id-clear').addEventListener('click', async () => { if (!selectedActionGame?.primaryExecutable) return; if (editDraft) { steamIdClearPending = true; $('#identity-steam-id').value = ''; syncIdentityDraft('steamId'); renderEditDraft(selectedActionGame); showNotice('Steam AppID 已清空并暂存；点击“保存内容”才会写入配置。', false, libraryNotice); return; } steamIdClearPending = true; $('#identity-steam-id').value = ''; try { snapshot = await invoke('clearManualSteamId', { executable: selectedActionGame.primaryExecutable }); const refreshed = mergedGames().find(item => normalizedUiPath(item.primaryExecutable) === normalizedUiPath(selectedActionGame.primaryExecutable)); if (refreshed) { selectedActionGame = refreshed; openEdit(refreshed); } else { renderEntry(); if (!$('#library-view').classList.contains('hidden')) renderLibrary(); } showNotice('Steam AppID 已清空；已锁定为不自动恢复，点击保存内容可继续保存其他修改。', false, libraryNotice); } catch (error) { steamIdClearPending = false; showError(error, libraryNotice); } }); $('#identity-name').addEventListener('input', () => { syncIdentityDraft('name'); renderEditDraft(selectedActionGame); }); $('#identity-steam-id').addEventListener('input', () => { syncIdentityDraft('steamId'); renderEditDraft(selectedActionGame); }); $('#edit-save-content').addEventListener('click', async () => { if (!selectedActionGame?.primaryExecutable) return; if (editDraft) { await persistEditDraft(); return; } });
$('#confirm-cancel').addEventListener('click', () => closeConfirm(false)); $('#confirm-confirm').addEventListener('click', () => closeConfirm(true)); $('#confirm-close').addEventListener('click', () => closeConfirm(false));
function routeWorkspaceKey(event) {
  // Native form controls must keep Enter/Space/arrows for text entry,
  const isBack = event.key === 'Escape' || event.key === 'Esc' || event.code === 'Escape';
  // selection and browser control behavior. Escape/B remains a deliberate
  // workspace back/cancel action even while an input has focus. Also stop any
  // event already consumed by a card or modal control from reaching the global
  // router.
  const semanticWorkspaceAction = event.workspaceAction === true;
  const activeInput = event.target || document.activeElement;
  const lockedControllerInput = activeInput?.matches?.('[data-controller-input]') && activeInput.readOnly;
  if (event.defaultPrevented || (!isBack && !semanticWorkspaceAction && isFormInputTarget(activeInput) && !lockedControllerInput)) return;
  if (lockedControllerInput && !semanticWorkspaceAction && (event.key === 'Enter' || event.key === ' ')) {
    event.preventDefault();
    beginControllerTextInput(activeInput);
    return;
  }
  if (isBack && document.activeElement?.dataset?.controllerEditing === 'true') {
    event.preventDefault();
    controllerBackLastAt = 0;
    if (controllerBackResetTimer) window.clearTimeout(controllerBackResetTimer);
    controllerBackResetTimer = 0;
    const input = document.activeElement;
    finishControllerTextInput(input);
    // Remove the editable DOM focus before selecting the locked field again.
    // This ends text entry without closing the editor or arming double-B exit.
    input.blur();
    focusWorkspaceElement(input, false);
    return;
  }
  if (semanticWorkspaceAction && ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(event.key)) {
    finishControllerTextInput(document.activeElement);
  }
  if(isBack&&librarySidebarState()?.page!=='library'&&librarySidebarState()&&!activeWorkspaceModal()){event.preventDefault();controllerBackLastAt=0;if(controllerBackResetTimer)window.clearTimeout(controllerBackResetTimer);controllerBackResetTimer=0;librarySidebarPage();return;}
  if (isBack) {
    event.preventDefault();
    const isControllerBack = semanticWorkspaceAction;
    if (isControllerBack && !activeWorkspaceModal()) {
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
    if (!$('#confirm-modal').classList.contains('hidden')) return closeConfirm(false);
    if (!$('#steam-commit-modal').classList.contains('hidden')) return closeSteamCommit();
    if (!$('#edit-modal').classList.contains('hidden')) return closeEdit();
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
let silentRefreshPending = false;
let silentRefreshAdvisory = false, silentRefreshPreserveWaiting = false;
let silentRefreshInFlight = null;
let silentRefreshClosed = false;
let silentRefreshEpoch = 0;
let silentRenderPending = false;
let silentRefreshDeferredRender = false;
function libraryRenderAllowed(){return !silentRefreshClosed&&!document.hidden&&document.hasFocus();}
function syncLibraryAnimationActivity(){
  const paused=!libraryRenderAllowed(),root=document.documentElement;
  if(root.classList.contains('workspace-background')!==paused)root.classList.toggle('workspace-background',paused);
}
syncLibraryAnimationActivity();
function renderSilentSnapshot(){
  if(!libraryRenderAllowed()){silentRenderPending=true;return;}
  silentRenderPending=false;
  renderEntry();
  if(!$('#library-view').classList.contains('hidden'))renderLibrary();
}
function scheduleSilentRefresh(options = {}) {
  if(silentRefreshClosed)return;
  if(options.advisory===true)silentRefreshAdvisory=true;
  if(options.preserveWaiting===true)silentRefreshPreserveWaiting=true;
  silentRefreshPending=true;
  if(!libraryRenderAllowed()||refreshAfterScrapeTimer||silentRefreshInFlight)return;
  refreshAfterScrapeTimer=window.setTimeout(()=>{
    refreshAfterScrapeTimer=0;
    if(!libraryRenderAllowed())return;
    silentRefreshPending=false;
    const epoch=silentRefreshEpoch;let succeeded=false;
    const request=invoke('state',{},5000).then(next=>{
      if(silentRefreshClosed||epoch!==silentRefreshEpoch)return;
      succeeded=true;
      if(silentRefreshAdvisory){
        applyLibrarySnapshot(next,{normalized:true,deferRender:true,preserveWaiting:silentRefreshPreserveWaiting});
        if(!silentRefreshPending){silentRefreshAdvisory=false;silentRefreshPreserveWaiting=false;}
      }else snapshot=next;
      silentRenderPending=true;
      // A change during the read needs one follow-up. Do not render an
      // intermediate tree that would be rebuilt again a moment later.
      if(!silentRefreshPending||silentRefreshDeferredRender){silentRefreshDeferredRender=false;renderSilentSnapshot();}
      else silentRefreshDeferredRender=true;
    }).catch(error=>{
      if(silentRefreshClosed||epoch!==silentRefreshEpoch)return;
      silentRefreshPending=true;
      if(libraryRenderAllowed())showNotice(error?.message||String(error),true,libraryNotice);
    }).finally(()=>{
      // A previous page lifecycle cannot clear a newer page's request owner.
      if(silentRefreshInFlight!==request)return;
      silentRefreshInFlight=null;
      if(succeeded&&silentRefreshPending)scheduleSilentRefresh();
    });
    silentRefreshInFlight=request;
  },250);
}
function resumeSilentRefresh(){
  syncLibraryAnimationActivity();
  syncEditorSearchActivity();
  if(!libraryRenderAllowed())return;
  if(cardOperationPending)scheduleCardOperationState();
  if(silentRefreshPending)scheduleSilentRefresh();
  else if(silentRenderPending)renderSilentSnapshot();
}
window.addEventListener('focus',resumeSilentRefresh);
document.addEventListener('visibilitychange',()=>{if(document.hidden){suspendCardOperationState();cancelEditReturnFocus();}resumeSilentRefresh();});
window.addEventListener('blur',()=>{
  syncLibraryAnimationActivity();
  syncEditorSearchActivity();
  suspendCardOperationState();cancelEditReturnFocus();
  if(refreshAfterScrapeTimer)clearTimeout(refreshAfterScrapeTimer);
  refreshAfterScrapeTimer=0;
});
window.addEventListener('pagehide',()=>{
  suspendCardOperationState();cancelEditReturnFocus();
  silentRefreshClosed=true;++silentRefreshEpoch;silentRefreshPending=true;
  syncLibraryAnimationActivity();
  if(identitySearchOwner||identitySearchInFlight)retireIdentitySearch();
  cancelArtworkPreviewWarmups();
  if(artworkSearchPresentationOwner){retireArtworkSearchPresentation();$('#artwork-search-modal')?.classList.add('hidden');}
  silentRenderPending=false;silentRefreshDeferredRender=false;silentRefreshInFlight=null;
  if(refreshAfterScrapeTimer)clearTimeout(refreshAfterScrapeTimer);
  refreshAfterScrapeTimer=0;
  syncSharedWallpaperPreviewActivity();
});
window.addEventListener('pageshow',()=>{silentRefreshClosed=false;resumeSilentRefresh();});
function workspaceRefreshRequested(message) {
  return Boolean(message.snapshot) || (message.refreshRequested === true && message.refreshProtocol === 1);
}

window.chrome.webview.addEventListener('message', event => {
  const message = isWorkspaceRecord(event.data) ? event.data : {};
  if (message.kind === 'response') {
    if (message.id === 0) {
      if (message.ok && message.result) {
        applyLibrarySnapshot(message.result);
      } else if (!message.ok) showError(message.error);
      return;
    }
    const item = pending.get(message.id);
    if (!item) return;
    pending.delete(message.id);
    if(item.timer) clearTimeout(item.timer);
    if (!message.ok) item.reject(new Error(message.error || '操作失败'));
    else {
      try { item.resolve(workspaceBridgeResult(item.command, message.result)); }
      catch (error) { reportUiError('invalid-host-snapshot', error, { command: item.command }); item.reject(error); }
    }
    return;
  }
  if (message.scrape) {
    snapshot.scrape = message.scrape;
    // Keep every data update, but paint card statuses once per visible frame.
    // Background events retain only the latest data until focus returns.
    scheduleCardOperationState();
  }
  if(message.event==='steam-commit-progress'){
    const labels={queued:message.position>1?'排队加入 Steam（第 '+message.position+' 位）':'排队加入 Steam',running:'正在加入 Steam',completed:'已加入 Steam',failed:'加入失败：'+(message.error||'请重试')};
    for(const directory of Array.isArray(message.directories)?message.directories:[]){if(typeof directory!=='string')continue;const key=normalizedUiPath(directory);if(key)steamCommitTasks.set(key,{phase:message.phase,label:labels[message.phase]||'正在处理'});}
    scheduleCardOperationState(true);return;
  }
  if(message.event==='media-download-progress'){renderMediaDownloads(message.mediaDownloads,{defer:true});return;}
  if(message.event==='game-media-changed'){scheduleSilentRefresh();}else if(message.event==='game-media-sync-error'){showNotice('静态壁纸已保存，共享缓存同步待重试：'+(message.error||'未知错误'),true,libraryNotice);}
  else if (message.event === 'library-first-run-started') {
    showNotice('正在自动扫描游戏库；仅首次打开时执行，页面保持可操作。', false, libraryNotice);
  // Detached workers can publish a snapshot sampled before a completed user write.
  // Coalesce their notifications into the current FIFO state read, never apply that old payload.
  } else if (message.event === 'library-first-run-scan-complete' || message.event === 'library-first-run-complete') {
    if (workspaceRefreshRequested(message)) scheduleSilentRefresh({advisory:true});
    showNotice(`首次目录扫描完成${Number.isFinite(Number(message.rootCount)) ? `，发现 ${Number(message.rootCount)} 个游戏库目录` : ''}；SteamID、封面和壁纸仍在后台识别。`, false, libraryNotice);
  } else if (message.event === 'library-first-run-error') {
    showNotice(message.error || '首次自动扫描未完成，可手动点击“扫描游戏库”重试。', true, libraryNotice);
  } else if (message.event === 'library-scan-complete') {
    if (workspaceRefreshRequested(message)) scheduleSilentRefresh({advisory:true});
    showNotice('目录扫描已完成；页面保持可操作。', false, libraryNotice);
  } else if (message.event === 'library-plan-refreshed') {
    // The local scan and Steam plan are intentionally separate phases.  A
    // slow/disconnected Steam install may delay this event, but must never
    // delay the library page or consume foreground input.
    if (workspaceRefreshRequested(message)) scheduleSilentRefresh({advisory:true,preserveWaiting:true});
  } else if (message.event === 'scrape-queue-started') {
    clearNotice(libraryNotice);
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      manualRefreshProgress = {
        phase: 'running',
        current: 0,
        total: Number(message.total || message.scrape?.total || 0),
        round: Number(message.round || message.scrape?.round || 1),
        roundLabel: message.roundLabel || message.scrape?.roundLabel || '第一轮',
        roundCurrent: Number(message.roundCurrent || message.scrape?.roundCurrent || 0),
        roundTotal: Number(message.roundTotal || message.scrape?.roundTotal || message.total || 0)
      };
      renderArtworkRefreshControl();
      setScrapeProgressToast(scrapeProgressText(manualRefreshProgress));
    }
  } else if (message.event === 'scrape-round-started') {
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      manualRefreshProgress = {
        ...manualRefreshProgress,
        phase: 'running',
        round: Number(message.round || 1),
        roundLabel: message.roundLabel || (Number(message.round) === 2 ? '第二轮' : '第一轮'),
        roundCurrent: Number(message.roundCurrent || 0),
        roundTotal: Number(message.roundTotal || 0)
      };
      renderArtworkRefreshControl();
      setScrapeProgressToast(scrapeProgressText(manualRefreshProgress));
    }
  } else if (message.event === 'scrape-progress') {
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      const current = Number(message.current);
      const total = Number(message.total);
      manualRefreshProgress = {
        phase: message.status === 'paused' ? 'paused' : 'running',
        current: Number.isFinite(current) ? Math.max(manualRefreshProgress.current, current) : manualRefreshProgress.current,
        total: Number.isFinite(total) ? total : manualRefreshProgress.total,
        round: Number(message.round || manualRefreshProgress.round || 1),
        roundLabel: message.roundLabel || (Number(message.round) === 2 ? '第二轮' : (manualRefreshProgress.roundLabel || '第一轮')),
        roundCurrent: Number.isFinite(Number(message.roundCurrent)) ? Number(message.roundCurrent) : manualRefreshProgress.roundCurrent,
        roundTotal: Number.isFinite(Number(message.roundTotal)) ? Number(message.roundTotal) : manualRefreshProgress.roundTotal
      };
      renderArtworkRefreshControl();
      setScrapeProgressToast(scrapeProgressText(manualRefreshProgress));
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
        total: Number.isFinite(total) ? total : manualRefreshProgress.total,
        round: manualRefreshProgress.round || 1,
        roundLabel: manualRefreshProgress.roundLabel || '第一轮',
        roundCurrent: manualRefreshProgress.roundCurrent || 0,
        roundTotal: manualRefreshProgress.roundTotal || 0
      };
      renderArtworkRefreshControl();
    }
    scheduleSilentRefresh();
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      const items = Object.values(message.scrape?.items || {});
      const success = items.filter(item => item?.status === 'ready').length;
      const unresolved = items.filter(item => ['identity-failed', 'failed', 'not-ready', 'deferred-manual', 'skipped-already-attempted', 'artwork-fallback-ready'].includes(item?.status)).length;
      const total = Number(message.total || items.length || 0);
      const detail = unresolved > 0
        ? `手动刷新结束：成功 ${success}/${total}，仍需处理 ${unresolved} 个`
        : (total > 0 ? `手动刷新完成：${success}/${total} 个已补齐 SteamID、封面和壁纸` : '手动刷新：当前没有需要处理的项目');
      finishScrapeProgressToast(detail, unresolved > 0);
    } else {
      showNotice('后台更新已完成。', false, libraryNotice);
    }
  } else if (message.event === 'scrape-paused') {
    setBusy(false);
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      manualRefreshProgress = {
        phase: 'paused',
        current: Number.isFinite(Number(message.current)) ? Number(message.current) : manualRefreshProgress.current,
        total: Number.isFinite(Number(message.total)) ? Number(message.total) : manualRefreshProgress.total,
        round: Number(message.round || manualRefreshProgress.round || 1),
        roundLabel: message.roundLabel || manualRefreshProgress.roundLabel || '第一轮',
        roundCurrent: Number.isFinite(Number(message.roundCurrent)) ? Number(message.roundCurrent) : manualRefreshProgress.roundCurrent,
        roundTotal: Number.isFinite(Number(message.roundTotal)) ? Number(message.roundTotal) : manualRefreshProgress.roundTotal
      };
      renderArtworkRefreshControl();
    }
    scheduleSilentRefresh();
    if (String(message.scrape?.mode || '') === 'manual-refresh') {
      finishScrapeProgressToast('手动刷新已暂停；已完成项目保留。', false);
    } else {
      showNotice('识别任务已停止；已完成项目保留。', false, libraryNotice);
    }
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
  } else if (['identity-search-started','identity-search-complete','identity-search-failed'].includes(message.event)) {
    consumeIdentitySearchEvent(message);
  } else if (message.event === 'scrape-error' || message.event === 'library-open-error') {
    if (message.event === 'scrape-error') setBusy(false);
    if (message.event === 'scrape-error' && String(message.scrape?.mode || '') === 'manual-refresh') {
      manualRefreshProgress = {
        ...manualRefreshProgress,
        phase: 'failed',
        current: manualRefreshProgress.current,
        total: manualRefreshProgress.total
      };
      renderArtworkRefreshControl();
      finishScrapeProgressToast(message.error || '后台任务失败', true);
    } else {
      showNotice(message.error || '后台任务失败', true, libraryNotice);
    }
  } else if (message.event === 'network-wake-started') {
    showNotice('网络恢复任务已后台启动，页面保持可操作。', false, libraryNotice);
  } else if (message.event === 'network-wake-complete') {
    scheduleSilentRefresh();
    showNotice(message.planPending ? '网络恢复已完成，计划正在后台刷新。' : '网络恢复任务已完成。', false, libraryNotice);
  } else if (message.event === 'network-plan-refreshed') {
    if (workspaceRefreshRequested(message)) scheduleSilentRefresh({advisory:true,preserveWaiting:true});
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
updateSteamArtworkButton.addEventListener('click', async () => {
  const selected = selectedVisibleSteamDirectories();
  if (!selected.length) return showNotice('请先选择已加入 Steam 的游戏。', true, libraryNotice);
  try {
    const preflight = await invoke('steamCommitPreflight');
    let forceCloseSteam = false;
    if (preflight?.steamRunning) {
      const accepted = await askConfirm('Steam 正在运行',
        '更新 Steam 图标需要暂时关闭 Steam，才能让新的封面和壁纸被可靠读取。\n\n确认后只请求 Steam 温和退出，再更新并校验素材文件；完成后由你手动启动 Steam。',
        '关闭 Steam 并更新');
      if (!accepted) return;
      forceCloseSteam = true;
    }
    const target = { selectionMode: 'all-steam-accounts', selectedGameDirectories: selected };
    if (forceCloseSteam) target.forceCloseSteam = true;
    await runBusy('refreshSteamArtwork', '正在更新 Steam 图标', forceCloseSteam
      ? '正在关闭 Steam、备份并更新封面/壁纸；完成后由你手动启动 Steam。'
      : '正在备份并更新已选择游戏的 Steam 封面/壁纸。', target,
      forceCloseSteam ? 'Steam 图标已更新并校验；需要时请手动启动 Steam。' : 'Steam 图标已更新并校验。');
  } catch (error) { showError(error, libraryNotice); }
});

// Build the visible editor dropdowns before the first startup snapshot so a
// fast controller A/方向键 press cannot land on a native select during the
// initial render race.
ensureNativeSteamTab();
upgradeEditDropdowns();

startSteamRuntimeWatch();

// Returning from Steam after signing in rechecks accounts once. Coalesce
// requests and rate-limit focus churn instead of adding another polling loop.
window.addEventListener('focus', () => {
  if (steamAccountDiscovery || steamAccountRequest) void refreshSteamAccountDiscovery();
});

// Startup is a fast local snapshot refresh, not a user-cancellable task. Keep
// it invisible and non-blocking; only explicit refresh/add/search operations
// use the busy overlay and B/Escape cancellation path.
invoke('startup', {advisoryRefreshProtocol:1}).then(async result => {
  snapshot = result;
  renderEntry();
  await openLibraryView();
}).catch(showError);








// Static and dynamic are two variants of the existing wallpaper slot.
let sharedWallpaperBusy=false;
let sharedWallpaperPreviewChoice=null;
function sharedWallpaperMode(){
  if(sharedWallpaperPreviewChoice?.key===keyFor(selectedActionGame||{}))return sharedWallpaperPreviewChoice.mode;
  const entry=selectedActionGame?.gameMedia;
  if(entry?.wallpaperMode==='static'||entry?.wallpaperMode==='dynamic')return entry.wallpaperMode;
  return entry?.dynamicPreviewUrl||(entry?.asset?.kind==='video'&&entry.previewUrl)?'dynamic':'static';
}
function confirmedMediaAppId(game){return isNativeSteamGame(game)?formalSteamAppId(game):(isSteamReady(game)?String(game?.steam?.steamStoreAppId||game?.steam?.storefrontAppId||game?.steam?.appId||''):'');}
function sharedWallpaperTarget(game){return game?.gameMedia||{appId:Number(confirmedMediaAppId(game))||0,name:gameDisplayName(game),executable:game.primaryExecutable||'',verified:Boolean(confirmedMediaAppId(game))};}
async function prepareSharedWallpaperPreview(game){
  if(!game)return;const key=keyFor(game),executable=normalizedUiPath(game.primaryExecutable||''),originalKey=game.gameMedia?.key,epoch=editPresentationEpoch;
  try{
    const entry=await invoke('mediaCreate',{...sharedWallpaperTarget(game)});
    if(epoch!==editPresentationEpoch||!selectedActionGame||keyFor(selectedActionGame)!==key||normalizedUiPath(selectedActionGame.primaryExecutable||'')!==executable)return;
    const current=selectedActionGame.gameMedia;
    if(current?.key===entry.key&&Number(current.bindingRevision)>Number(entry.bindingRevision))return;
    if(current?.key&&current.key!==originalKey&&current.key!==entry.key)return;
    selectedActionGame.gameMedia={...(current||{}),...entry};renderSharedWallpaperPreview();
  }catch(error){showError(error,libraryNotice);}
}
function syncSharedWallpaperPreviewActivity(){
  const video=document.querySelector('#artwork-wallpaper-preview video');if(!video)return;
  if(!libraryRenderAllowed()||$('#edit-modal').classList.contains('hidden')){if(!video.paused)video.pause();}
  else if(video.paused)void video.play().catch(()=>{});
}
window.addEventListener('blur',syncSharedWallpaperPreviewActivity);
window.addEventListener('focus',resumeEditorArtworkPresentation);
window.addEventListener('pageshow',resumeEditorArtworkPresentation);
document.addEventListener('visibilitychange',resumeEditorArtworkPresentation);
function stopSharedWallpaperPreview(){const video=document.querySelector('#artwork-wallpaper-preview video');if(video){video.pause();video.removeAttribute('src');video.load();}}
const WALLPAPER_DEFAULT_PLAYBACK={start:8,end:38,fit:'cover'};
const sharedWallpaperPendingPlayback=new Map();
let sharedWallpaperActivePlayback=null;
// Old hosts return a durable media receipt without mapped preview URLs.
// Reuse a preview only when its exact immutable asset descriptor is unchanged.
function retainSharedWallpaperPreviews(owner,receipt){
  const previous=owner.entry;
  if(!previous||!receipt||previous.key!==receipt.key||Number(previous.appId||0)!==Number(receipt.appId||0))return receipt;
  const next={...receipt};
  const inherit=(field,prior,asset)=>{if(!next[field]&&previous[field]&&prior?.id&&asset?.id===prior.id&&JSON.stringify(prior)===JSON.stringify(asset))next[field]=previous[field];};
  for(const slot of ['dynamic','static'])inherit(slot+'PreviewUrl',previous.assets?.[slot],next.assets?.[slot]);
  inherit('previewUrl',previous.asset,next.asset);
  return next;
}
function captureSharedWallpaperWrite(game=selectedActionGame){
  if(!game)return null;
  return {key:keyFor(game),executable:normalizedUiPath(game.primaryExecutable||''),target:{...sharedWallpaperTarget(game)},entry:game.gameMedia?{...game.gameMedia}:null};
}
function pendingSharedWallpaperPlayback(game=selectedActionGame){
  const key=keyFor(game||{});
  return sharedWallpaperPendingPlayback.get(key)?.playback||(sharedWallpaperActivePlayback?.owner.key===key?sharedWallpaperActivePlayback.playback:null);
}
function applySharedWallpaperWrite(owner,entry){
  owner.entry=entry;
  const queued=sharedWallpaperPendingPlayback.get(owner.key);if(queued)queued.owner.entry=entry;
  if(selectedActionGame&&keyFor(selectedActionGame)===owner.key&&normalizedUiPath(selectedActionGame.primaryExecutable||'')===owner.executable){selectedActionGame.gameMedia=entry;renderArtworkDraft();}
}
function wallpaperEffectiveRange(video,playback){
  const duration=Number(video.duration);if(!Number.isFinite(duration)||duration<=0)return {start:0,end:0};
  const requested=Math.max(0,Number(playback?.start)||0),length=Number(playback?.end)>requested?Number(playback.end)-requested:0;
  const start=requested<duration-1?requested:0;return {start,end:length?Math.min(duration,start+length):duration};
}
function applyWallpaperPreviewRange(video){
  if(!selectedActionGame||!video.isConnected||document.querySelector('#artwork-wallpaper-preview video')!==video)return;
  if(!libraryRenderAllowed()){editorArtworkRenderPending=true;return;}
  const requested=pendingSharedWallpaperPlayback()||selectedActionGame?.gameMedia?.playback||WALLPAPER_DEFAULT_PLAYBACK;
  const range=wallpaperEffectiveRange(video,requested);if(video.readyState>=1&&(video.currentTime<range.start||range.end>range.start&&video.currentTime>=range.end))video.currentTime=range.start;
  const hint=document.getElementById('shared-wallpaper-duration-hint');if(hint&&Number.isFinite(video.duration)){const text='本片播放约 '+Math.max(1,Math.round(range.end-range.start))+' 秒';if(hint.textContent!==text)hint.textContent=text;const trigger=workspaceDropdownForSelect(document.getElementById('shared-wallpaper-length'))?.trigger;const title='播放时长 · '+text;if(trigger&&trigger.title!==title)trigger.title=title;}
}
function setWallpaperSelect(select,value,label){
  if(!select)return;const text=String(value);let added=false;
  if(![...select.options].some(option=>option.value===text)){const option=document.createElement('option');option.value=text;option.textContent=label;option.dataset.custom='true';select.appendChild(option);added=true;}
  if(select.value!==text||added){select.value=text;refreshWorkspaceDropdown(select);}
}
function renderSharedWallpaperPreview(){
  renderSharedWallpaperDownloadStatus();
  if(!selectedActionGame)return;
  if(!libraryRenderAllowed()){editorArtworkRenderPending=true;return;}
  const target=document.getElementById('artwork-wallpaper-preview');if(!target)return;
  const entry=selectedActionGame.gameMedia||{},mode=sharedWallpaperMode(),actualHeight=Number(entry.assets?.dynamic?.selectedHeight||0);
  const title=mode==='dynamic'&&actualHeight>0?'当前动态壁纸：'+actualHeight+'p':'';if(target.title!==title)target.title=title;
  for(const button of document.querySelectorAll('[data-shared-wallpaper-mode]')){const pressed=String(button.dataset.sharedWallpaperMode===mode);if(button.getAttribute('aria-pressed')!==pressed)button.setAttribute('aria-pressed',pressed);}
  const playback=document.getElementById('shared-wallpaper-playback'),dynamic=document.querySelector('.shared-wallpaper-dynamic');
  if(playback&&playback.classList.contains('hidden')!==(mode!=='dynamic'))playback.classList.toggle('hidden',mode!=='dynamic');
  if(dynamic&&dynamic.classList.contains('is-selected')!==(mode==='dynamic'))dynamic.classList.toggle('is-selected',mode==='dynamic');
  const requested=pendingSharedWallpaperPlayback()||entry.playback||WALLPAPER_DEFAULT_PLAYBACK;
  const start=Number(requested.start)||0,length=Number(requested.end)>start?Number(requested.end)-start:0;
  setWallpaperSelect(document.getElementById('shared-wallpaper-skip'),start,start+' 秒');
  setWallpaperSelect(document.getElementById('shared-wallpaper-length'),length,length?length+' 秒':'完整播放');
  if(mode==='dynamic'){
    const url=entry.dynamicPreviewUrl||(entry.asset?.kind==='video'?entry.previewUrl:'');
    if(url){
      const projection=editorArtworkProjection('video',url),old=target.querySelector('video');
      if(target._editorArtworkProjection===projection&&old?.getAttribute('src')===url){const fit=entry.playback?.fit||'cover';if(old.style.objectFit!==fit)old.style.objectFit=fit;applyWallpaperPreviewRange(old);syncSharedWallpaperPreviewActivity();return;}
      if(old){old.pause();old.removeAttribute('src');old.load();}
      target._editorArtworkProjection=projection;target._deferredArtworkError=null;target._artworkFallbackToken=null;
      const video=document.createElement('video');video.src=url;video.muted=true;video.loop=true;video.playsInline=true;video.autoplay=libraryRenderAllowed();video.preload='metadata';video.setAttribute('aria-label','动态壁纸预览');video.style.objectFit=entry.playback?.fit||'cover';video.addEventListener('loadedmetadata',()=>applyWallpaperPreviewRange(video));video.addEventListener('timeupdate',()=>applyWallpaperPreviewRange(video));target.replaceChildren(video);
    }else replaceEditorArtworkMarkup(target,'dynamic-empty','<span class="artwork-empty">暂无动态壁纸，按 A 可替换或重新下载</span>');
  }else if(artworkDraft.dirty.wallpaper&&!selectedActionGame.mediaOnly){
    replaceEditorArtworkMarkup(target,'slot:wallpaper',artworkSlotMarkup(artworkDraft.wallpaper,'wallpaper'),'wallpaper');
  }else if(entry.deletedSlots?.static){
    replaceEditorArtworkMarkup(target,'shared-static-deleted',artworkSlotMarkup({state:'deleted'},'wallpaper'));
  }else if(entry.staticPreviewUrl&&(!artworkDraft.wallpaper?.asset||selectedActionGame.mediaOnly||entry.manualSlots?.static)){
    replaceEditorArtworkMarkup(target,'shared-image',`<img src="${escapeHtml(entry.staticPreviewUrl)}" alt="静态壁纸预览">`);
  }else replaceEditorArtworkMarkup(target,'slot:wallpaper',artworkSlotMarkup(artworkDraft.wallpaper,'wallpaper'),'wallpaper');
}
function queueSharedWallpaperPlayback(){
  const owner=captureSharedWallpaperWrite();if(!owner)return;
  const start=Number(document.getElementById('shared-wallpaper-skip').value),length=Number(document.getElementById('shared-wallpaper-length').value);
  sharedWallpaperPendingPlayback.set(owner.key,{owner,playback:{start,end:length?start+length:0,fit:'cover'}});
  void flushSharedWallpaperPlayback();
}
async function flushSharedWallpaperPlayback(){
  if(sharedWallpaperBusy||!sharedWallpaperPendingPlayback.size)return;
  const [key,pending]=sharedWallpaperPendingPlayback.entries().next().value;sharedWallpaperPendingPlayback.delete(key);
  sharedWallpaperActivePlayback=pending;
  try{await updateSharedWallpaper('playback',pending.playback,pending.owner);}
  finally{if(sharedWallpaperActivePlayback===pending)sharedWallpaperActivePlayback=null;}
}
async function updateSharedWallpaper(action,extra={},owner=captureSharedWallpaperWrite()){
  if(sharedWallpaperBusy||!owner)return false;sharedWallpaperBusy=true;
  try{
    const entry=owner.entry||await invoke('mediaCreate',owner.target);
    const next=await invoke('mediaEdit',{...entry,...extra,action});
    snapshot.gameMedia||={entries:{}};snapshot.gameMedia.entries||={};snapshot.gameMedia.entries[next.key||entry.key]=next;const state=snapshot;
    applySharedWallpaperWrite(owner,state.gameMedia?.entries?.[next.key||entry.key]||next);if(libraryRenderAllowed())renderLibrary();else silentRenderPending=true;return true;
  }catch(error){showError(error,libraryNotice);return false;}
  finally{sharedWallpaperBusy=false;void flushSharedWallpaperPlayback();}
}
const sharedWallpaperDownloadRequests=new Map();
function setSharedWallpaperDownloadRequest(owner,request){
  sharedWallpaperDownloadRequests.delete(owner.key);
  sharedWallpaperDownloadRequests.set(owner.key,{mediaKey:owner.entry?.key,...request});
  while(sharedWallpaperDownloadRequests.size>64)sharedWallpaperDownloadRequests.delete(sharedWallpaperDownloadRequests.keys().next().value);
  renderSharedWallpaperDownloadStatus();
}
function sharedWallpaperDownloadFeedback(entry={},state={},request){
  if(entry.deleted||entry.deletedSlots?.dynamic)return null;
  if(request?.mediaKey&&request.mediaKey!==entry.key)request=null;
  const key=entry.key,active=state.active?.[key]||(state.current?.key===key?state.current:null);
  const rawItem=state.manualItems?.[key];
  const item=rawItem&&(!request?.revision||Number(rawItem.requestBindingRevision||0)>=request.revision)?rawItem:null;
  const pending=entry.refreshRequested&&entry.refreshSlot!=='static'||entry.selectedVideo&&entry.taskSlots?.includes('dynamic');
  if(request?.phase==='submitting')return {status:'pending',text:'正在提交下载请求…',indeterminate:true};
  if(request?.phase==='error')return {status:'error',text:'下载请求未完成：'+request.error,canSearch:true};
  const replaced=request?.assetId&&entry.assets?.dynamic?.id&&entry.assets.dynamic.id!==request.assetId;
  if(replaced||item?.state==='done'&&!pending)return request||item?{status:'done',text:'动态壁纸已下载并应用',value:1}:null;
  if(!request&&!pending&&!active&&!item)return null;
  const error=item?.lastError||(pending?entry.error:''),stopped=item?.autoStopped||item?.state==='unavailable';
  // A terminal per-game receipt is not resumable merely because another
  // policy currently pauses automatic downloads.
  if(state.manualState==='cancelled'&&item&&!active)return {status:'waiting',text:'本轮下载已停止，所选视频已保留',canSearch:true};
  if(!active&&(stopped||error==='media-video-selection-expired')){
    const reason=item?.stopReason||error;
    const detail=error==='media-video-selection-expired'?'官方视频候选已更新，请重新选择':reason==='automatic-time-budget-exhausted'||error==='media-task-time-budget'?'本轮下载时间预算已用完，可重新选择视频重试':reason==='automatic-attempt-budget-exhausted'?'本轮下载重试次数已用完，可重新选择视频重试':/network|http-429|http-5|truncated|empty/.test(error)?'网络连接持续失败':String(error||reason||'所选视频暂不可用');
    return {status:'error',text:'下载未完成：'+detail+'；当前壁纸保留',canSearch:true};
  }
  const pauses={'on-battery':'电池模式，等待接通电源后继续','wallpaper-on-battery':'已启用离电关闭，等待接通电源后继续','wallpaper-power-unknown':'正在确认供电状态','power-unknown':'正在确认供电状态','startup-protection':'启动保护结束后继续','media-disk-space-low':'磁盘余量不足，腾出空间后继续','media-cache-budget-full':'壁纸缓存已满，清理后继续','media-storage-unavailable':'存储暂不可用，恢复后继续','low-battery':'低电量，等待充电后继续','battery-saver':'节电模式下暂停','cpu-busy':'后台负载较高，等待空闲后继续','cpu-unknown':'正在检测后台负载','cpu-recovering':'等待后台负载稳定后继续','game-running':'游戏运行中，等待后台下载恢复'};
  const now=Date.now(),manual=Number(state.manualUntil||0)>now&&Number(state.manualUntil)<=now+900000&&(!Array.isArray(state.manualTargets)||state.manualTargets.includes(key));
  const softPauses=['game-running','cpu-busy','cpu-unknown','cpu-recovering'];
  const pause=[state.resourcePauseReason,state.pauseReason].find(reason=>reason&&!(manual&&softPauses.includes(reason)));
  if(pauses[pause])return {status:'waiting',text:pauses[pause]+'；当前壁纸保留'};
  if(active){
    if(active.phase==='retrying')return {status:'retrying',text:'网络波动，正在按弱网策略重试…',indeterminate:true};
    if(active.phase==='publishing')return {status:'pending',text:'下载完成，正在应用动态壁纸…',indeterminate:true};
    const total=Number(active.totalBytes||0),bytes=Math.max(0,Number(active.bytes||0));
    if(total>0)return {status:'downloading',text:'正在下载动态壁纸 · '+Math.min(100,Math.floor(bytes*100/total))+'%',value:Math.min(1,bytes/total)};
    const mb=bytes>0?' · 已下载 '+(bytes/1048576).toFixed(1)+' MB':'';
    return {status:'downloading',text:(active.phase==='downloading'?'正在下载动态壁纸':'正在连接视频来源')+mb,indeterminate:true};
  }
  if(error){
    if(error==='media-video-selection-expired')return {status:'error',text:'官方视频候选已更新，请重新选择；当前壁纸保留',canSearch:true};
    const network=/network|http-429|http-5|truncated|empty/.test(error),retryAt=Number(item?.retryAt||entry.retryAt||0);
    if(!stopped&&(retryAt>0||state.manualUntil>Date.now()))return {status:'retrying',text:(network?'网络暂不稳定':'下载暂未完成')+'，已保留所选视频，等待重试；当前壁纸保留',canSearch:true};
    const detail=network?'网络连接持续失败':error==='media-video-selection-expired'?'官方视频候选已更新，请重新选择':pauses[error]||String(error);
    return {status:'error',text:'下载未完成：'+detail+'；当前壁纸保留',canSearch:true};
  }
  if(request?.phase==='queued'||item)return {status:'pending',text:'已加入下载队列，等待处理；当前壁纸保留',indeterminate:true};
  if(pending)return {status:'waiting',text:'壁纸更新尚未完成，等待下载安排；可重新选择视频重试',canSearch:true};
  return null;
}
function renderSharedWallpaperDownloadStatus(){
  const panel=document.getElementById('shared-wallpaper-download-status');if(!panel||!libraryRenderAllowed())return;
  const game=selectedActionGame;if(!game||sharedWallpaperMode()!=='dynamic'){panel.classList.add('hidden');return;}
  const bound=game.gameMedia||{},fresh=snapshot.gameMedia?.entries?.[bound.key];
  const entry=fresh&&Number(fresh.bindingRevision||0)>=Number(bound.bindingRevision||0)?fresh:bound;
  const feedback=sharedWallpaperDownloadFeedback(entry,snapshot.mediaDownloads||{},sharedWallpaperDownloadRequests.get(keyFor(game)));
  panel.classList.toggle('hidden',!feedback);if(!feedback)return;
  if(panel.dataset.status!==feedback.status)panel.dataset.status=feedback.status;
  const text=panel.querySelector('[data-wallpaper-download-label]');if(text.textContent!==feedback.text)text.textContent=feedback.text;
  const bar=panel.querySelector('progress');
  if(feedback.indeterminate){if(bar.hasAttribute('value'))bar.removeAttribute('value');}
  else if(!bar.hasAttribute('value')||bar.value!==Number(feedback.value||0))bar.value=Number(feedback.value||0);
  panel.querySelector('button').classList.toggle('hidden',!feedback.canSearch);
}
async function searchSharedWallpaperVideos(owner){
  const qualitySelect=document.getElementById('artwork-video-quality');qualitySelect.value=String([480,720,1080].includes(owner.entry?.selectedVideo?.preferredHeight)?owner.entry.selectedVideo.preferredHeight:720);document.getElementById('artwork-video-quality-row').classList.remove('hidden');const qualityDropdown=workspaceDropdownForSelect(qualitySelect);refreshWorkspaceDropdown(qualitySelect);qualitySelect.onchange=()=>{for(const item of document.querySelectorAll('[data-video-candidate] .artwork-candidate-meta'))item.textContent=qualitySelect.value+'p 优先';};
  const token=++artworkTaskToken,epoch=silentRefreshEpoch;artworkTaskInFlight=true;artworkSearchSlot='wallpaper';artworkSearchType='dynamic';stopSharedWallpaperPreview();document.querySelector('#artwork-search-modal .eyebrow').textContent='视频候选';
  const modal=document.getElementById('artwork-search-modal'),list=document.getElementById('artwork-search-list');
  document.getElementById('artwork-search-title').textContent='选择动态壁纸';document.getElementById('artwork-search-summary').textContent='正在读取 Steam 官方预告片…';
  list.replaceChildren();list.dataset.artworkType='wallpaper';document.getElementById('artwork-search-empty').classList.add('hidden');modal.classList.remove('hidden');
  const ownerCurrent=()=>selectedActionGame&&keyFor(selectedActionGame)===owner.key&&normalizedUiPath(selectedActionGame.primaryExecutable||'')===owner.executable&&(!owner.entry?.key||selectedActionGame.gameMedia?.key===owner.entry.key&&Number(selectedActionGame.gameMedia.bindingRevision||0)===Number(owner.entry.bindingRevision||0));
  const current=()=>modal.isConnected&&!silentRefreshClosed&&epoch===silentRefreshEpoch&&token===artworkTaskToken&&!modal.classList.contains('hidden')&&ownerCurrent();
  const lease=beginArtworkSearchPresentation(current);
  const searchError=error=>queueArtworkSearchPresentation(lease,()=>{if(!current())return;document.getElementById('artwork-search-summary').textContent='动态壁纸搜索未完成：'+String(error?.message||error);document.getElementById('artwork-search-empty').classList.add('hidden');const retry=document.createElement('button');retry.type='button';retry.className='button';retry.textContent='重试搜索';retry.addEventListener('click',()=>{if(current())void searchSharedWallpaperVideos(captureSharedWallpaperWrite());});list.replaceChildren(retry);requestArtworkSearchFocus(retry,lease);});
  const selectionError=error=>queueArtworkSearchPresentation(lease,()=>{
    if(!current())return;
    document.getElementById('artwork-search-summary').textContent='动态壁纸选择或下载请求未完成：'+String(error?.message||error)+'；可重试候选或重新搜索，完成前保留当前壁纸。';
    if(!list.querySelector('[data-video-search-retry]')){const retry=document.createElement('button');retry.type='button';retry.className='button';retry.dataset.videoSearchRetry='true';retry.textContent='重新搜索';retry.addEventListener('click',()=>{if(current()&&!sharedWallpaperBusy)void searchSharedWallpaperVideos(captureSharedWallpaperWrite());});list.appendChild(retry);}
  });
  lease.onError=searchError;
  try{
    const result=await invoke('mediaSearchVideos',owner.entry||owner.target);if(!current())return false;
    const entryKey=owner.entry?.key||selectedActionGame.gameMedia?.key;
    if(result?.entryKey&&entryKey&&result.entryKey!==entryKey)throw new Error('动态壁纸游戏绑定已变化，请重新搜索');
    return queueArtworkSearchPresentation(lease,()=>{
    const candidates=Array.isArray(result?.candidates)?result.candidates:[];
    document.getElementById('artwork-search-summary').textContent=(result.query||'当前游戏')+' · '+candidates.length+' 个动态壁纸候选 · 选择后后台下载';
    document.getElementById('artwork-search-empty').textContent=candidates.length?'':'该游戏暂无可用的官方预告片，可返回后指定本地视频。';document.getElementById('artwork-search-empty').classList.toggle('hidden',candidates.length!==0);
    for(const candidate of candidates){const card=document.createElement('button');card.type='button';card.className='artwork-candidate';card.dataset.videoCandidate=candidate.id;
      const preview=document.createElement('span');preview.className='artwork-candidate-preview';
      if(candidate.previewUrl){const img=document.createElement('img');img.src=candidate.previewUrl;img.alt=candidate.title||'动态壁纸';img.loading='lazy';img.addEventListener('error',()=>{preview.textContent='Steam 官方预告片';},{once:true});preview.appendChild(img);}else preview.textContent='Steam 官方预告片';
      const title=document.createElement('strong');title.className='artwork-candidate-title';title.textContent=candidate.title||'Steam 预告片';const meta=document.createElement('span');meta.className='artwork-candidate-meta';meta.textContent=qualitySelect.value+'p 优先';card.append(preview,title,meta);
      card.addEventListener('click',async()=>{if(!current()||sharedWallpaperBusy)return;sharedWallpaperBusy=true;card.disabled=true;setSharedWallpaperDownloadRequest(owner,{phase:'submitting',token,assetId:owner.entry?.assets?.dynamic?.id});meta.textContent='正在加入下载队列…';
        try{const target=owner.entry||owner.target;const received=await invoke('mediaSelectVideo',{...target,bindingRevision:result.bindingRevision,searchToken:result.searchToken,candidateId:candidate.id,preferredHeight:Number(qualitySelect.value)||720});if(!current())return;const next=retainSharedWallpaperPreviews(owner,received);
          if(!next?.key||entryKey&&next.key!==entryKey)throw new Error('选择或下载请求未返回有效的游戏回执，请重试');
          // applySharedWallpaperWrite advances owner.entry to the accepted revision before the state refresh fence.
          snapshot.gameMedia={...snapshot.gameMedia,entries:{...snapshot.gameMedia?.entries,[next.key]:next}};
          setSharedWallpaperDownloadRequest(owner,{phase:'queued',token,revision:Number(next.bindingRevision||0),assetId:owner.entry?.assets?.dynamic?.id});
          applySharedWallpaperWrite(owner,next);closeWorkspaceDropdown(false);modal.classList.add('hidden');renderLibrary();showNotice('已选择“'+candidate.title+'”，已加入下载队列；完成前保留当前壁纸。',false,libraryNotice);
          try{
            const downloadsBefore=snapshot.mediaDownloads;
            const state=await invoke('mediaState',{key:next.key,entryOnly:true});if(token!==artworkTaskToken||!ownerCurrent())return;
            const entry=state.gameMedia?.entries?.[next.key];
            const refreshed=entry?.key===next.key&&Number(entry.bindingRevision||0)>=Number(next.bindingRevision||0)?retainSharedWallpaperPreviews(owner,entry):next;
            snapshot.gameMedia={...snapshot.gameMedia,entries:{...snapshot.gameMedia?.entries,[next.key]:refreshed}};
            if(state.mediaDownloads&&snapshot.mediaDownloads===downloadsBefore)renderMediaDownloads(state.mediaDownloads);
            applySharedWallpaperWrite(owner,refreshed);renderLibrary();
          }
          catch(error){if(token===artworkTaskToken&&ownerCurrent())showNotice('动态壁纸选择请求已完成，下载状态刷新未完成：'+String(error?.message||error)+'；完成前保留当前壁纸。',true,libraryNotice);}
        }catch(error){if(current())setSharedWallpaperDownloadRequest(owner,{phase:'error',token,error:String(error?.message||error)});selectionError(error);}finally{sharedWallpaperBusy=false;card.disabled=false;meta.textContent=qualitySelect.value+'p 优先';if(sharedWallpaperDownloadRequests.get(owner.key)?.phase==='submitting'&&sharedWallpaperDownloadRequests.get(owner.key)?.token===token)sharedWallpaperDownloadRequests.delete(owner.key);if(current()&&libraryRenderAllowed())card.focus();renderSharedWallpaperDownloadStatus();void flushSharedWallpaperPlayback();}
      });list.appendChild(card);
    }
    requestArtworkSearchFocus(list.querySelector('button'),lease);});
  }catch(error){searchError(error);return false;}
  finally{if(token===artworkTaskToken)artworkTaskInFlight=false;}
}
async function actOnSharedWallpaper(action){
  const owner=captureSharedWallpaperWrite();if(!owner)return false;const slot=sharedWallpaperMode();
  if(action==='search'){if(slot==='dynamic')return searchSharedWallpaperVideos(owner);return searchArtwork('wallpaper');}
  if(action==='delete'){await updateSharedWallpaper('delete',{slot},owner);return true;}
  if(action!=='replace'||sharedWallpaperBusy)return false;sharedWallpaperBusy=true;
  try{
    const picked=await invoke(slot==='dynamic'?'pickVideo':'pickArtwork');if(picked?.cancelled||!picked?.path)return true;
    const entry=owner.entry||await invoke('mediaCreate',owner.target);
    const imported=await invoke('mediaImport',{...entry,source:picked.path,slot});
    if(!imported?.key)throw new Error('素材导入没有返回有效的游戏回执，请刷新后确认。');
    // The accepted receipt includes mapped previews and advances queued edits
    // before any independent refresh. A full-library read is not part of this write.
    snapshot.gameMedia||={entries:{}};snapshot.gameMedia.entries||={};snapshot.gameMedia.entries[imported.key]=imported;
    applySharedWallpaperWrite(owner,imported);if(libraryRenderAllowed())renderLibrary();else silentRenderPending=true;return true;
  }catch(error){showError(error,libraryNotice);return false;}
  finally{sharedWallpaperBusy=false;void flushSharedWallpaperPlayback();}
}
(function installSharedWallpaperPreview(){
  const preview=document.getElementById('artwork-wallpaper-preview'),pane=preview?.closest('.artwork-pane');if(!preview||!pane)return;
  const status=document.createElement('div');status.id='shared-wallpaper-download-status';status.className='shared-wallpaper-download-status hidden';status.setAttribute('role','status');status.setAttribute('aria-live','polite');status.innerHTML='<span data-wallpaper-download-label></span><progress max="1" value="0" aria-label="当前动态壁纸下载进度"></progress><button type="button" class="button hidden" data-edit-row="wallpaper-download-retry">重新选择视频</button>';status.querySelector('button').addEventListener('click',()=>{if(!sharedWallpaperBusy)void actOnSharedWallpaper('search');});preview.after(status);
  const aside=document.createElement('aside');aside.className='shared-wallpaper-settings';aside.setAttribute('aria-label','壁纸设置');
  const choice=document.createElement('div');choice.className='shared-wallpaper-modes';choice.setAttribute('aria-label','壁纸类型');
  const dynamicGroup=document.createElement('div');dynamicGroup.className='shared-wallpaper-dynamic';dynamicGroup.setAttribute('role','group');dynamicGroup.setAttribute('aria-label','动态壁纸与播放设置');choice.appendChild(dynamicGroup);
  for(const mode of ['dynamic','static']){const button=document.createElement('button');button.type='button';button.className='button';button.dataset.sharedWallpaperMode=mode;button.textContent=mode==='static'?'静态壁纸':'动态壁纸';button.addEventListener('click',()=>{if(sharedWallpaperBusy||!selectedActionGame)return;sharedWallpaperPreviewChoice={key:keyFor(selectedActionGame),mode};renderArtworkDraft();void updateSharedWallpaper('mode',{mode});});(mode==='dynamic'?dynamicGroup:choice).appendChild(button);}aside.appendChild(choice);
  const tools=document.createElement('div');tools.id='shared-wallpaper-playback';tools.className='shared-wallpaper-playback';
  tools.innerHTML='<label><span class="wallpaper-control-label">跳过片头</span><select id="shared-wallpaper-skip" aria-label="跳过片头" data-edit-row="wallpaper-skip"></select></label><label><span class="wallpaper-control-label">播放时长</span><select id="shared-wallpaper-length" aria-label="播放时长" data-edit-row="wallpaper-length"></select></label><p id="shared-wallpaper-duration-hint" class="form-help">选择后自动保存</p>';
  const skips=tools.querySelector('#shared-wallpaper-skip'),lengths=tools.querySelector('#shared-wallpaper-length');
  for(const value of [0,3,5,8,10,15,20,30]){const option=document.createElement('option');option.value=String(value);option.textContent=value===0?'不跳过':value===8?'8 秒（推荐）':value+' 秒';skips.appendChild(option);}
  for(const value of [30,60,90,120,0]){const option=document.createElement('option');option.value=String(value);option.textContent=value?value+' 秒':'完整播放';lengths.appendChild(option);}
  skips.value='8';lengths.value='30';skips.addEventListener('change',queueSharedWallpaperPlayback);lengths.addEventListener('change',queueSharedWallpaperPlayback);dynamicGroup.appendChild(tools);const heading=document.querySelector('#edit-modal .edit-heading-status');heading.insertBefore(aside,heading.firstChild);for(const select of [skips,lengths]){const dropdown=upgradeWorkspaceSelect(select);const caption=document.createElement('span');caption.className='wallpaper-control-caption';caption.textContent=select.getAttribute('aria-label');caption.setAttribute('aria-hidden','true');dropdown.trigger.prepend(caption);if(select===lengths)dropdown.trigger.setAttribute('aria-describedby','shared-wallpaper-duration-hint');}
})();

// Left library presentation reuses the existing snapshot, native IPC and write owners.
function librarySidebarState(){return document.getElementById('library-view')?._sidebarState||null;}
function librarySidebarMatches(game){const query=librarySidebarState()?.query||'';return !query||String(gameDisplayName(game)||'').toLocaleLowerCase().includes(query);}
function librarySidebarIcon(name){const icons=LIBRARY_SIDEBAR_ICONS;return `<svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">${icons[name]||icons.grid}</svg>`;}
function librarySidebarCurrentGame(){const key=activeLibraryCard()?.dataset.directory||librarySidebarState()?.lastCard||currentCardDirectory;return mergedGames().find(game=>keyFor(game)===key)||mergedGames().find(game=>category(game)===activeTab&&librarySidebarMatches(game));}
function librarySidebarPage(page='library',restore=true){
 const state=librarySidebarState();if(!state)return;
 const previous=state.page;state.page=page;
 const management=$('#library-management-page'),main=$('#library-main-page');
 main.classList.toggle('hidden',page!=='library');management.classList.toggle('hidden',page==='library');
 for(const [id,kind] of [['directory-settings-panel','sources'],['manual-game-panel','manual']])$('#'+id).classList.toggle('hidden',page!==kind);
 $('#directory-settings-toggle').setAttribute('aria-expanded',String(page==='sources'));$('#manual-game-settings-toggle').setAttribute('aria-expanded',String(page==='manual'));
 $('#library-management-title').textContent=page==='sources'?'游戏库来源':'单独添加游戏';
 $('#library-management-description').textContent=page==='sources'?'管理需要扫描的游戏目录。移除来源不会删除游戏文件。':'选择一个游戏的主程序，沿用原有识别与加入流程。';
 $('#library-view').dataset.libraryPage=page;
 syncLibrarySidebarDock();
 if(restore){const target=page==='library'?$('#'+(previous==='sources'?'directory-settings-toggle':'manual-game-settings-toggle')):page==='sources'?$('#pick-root-button'):$('#pick-game-button');focusWorkspaceElement(target);}
}
function syncLibrarySidebarDock(){
 const state=librarySidebarState();if(!state)return;
 const management=state.page!=='library';$('#library-dock-select').classList.toggle('hidden',management);$('#library-dock-edit').classList.toggle('hidden',management);
 $('#library-dock-back').querySelector('[data-library-back-label]').textContent=management?'返回游戏库':'返回';
 const game=librarySidebarCurrentGame();$('#library-dock-name').textContent=management?(state.page==='sources'?'游戏库来源':'单独添加游戏'):game?gameDisplayName(game):'当前分类没有游戏';
 $('#library-dock-select').disabled=!game||!pageSelectable(game);$('#library-dock-edit').disabled=!game;
}
function fitLibrarySidebarCards(){
 const state=librarySidebarState(),scroll=$('#library-sidebar-scroll');if(!state||!scroll||state.page!=='library'||!libraryRenderAllowed())return;
 const width=library.clientWidth,height=scroll.clientHeight-20,gap=state.density==='compact'?12:innerWidth<=1400?13:16;
 let columns=innerWidth<760?Math.max(2,Math.floor((width+gap)/125)):innerWidth<=1400?Math.max(3,Math.min(7,Math.floor((width+gap)/125))):8;
 if(innerWidth>=760)while(columns<11&&2*((width-gap*(columns-1))/columns*1.5+44)+18>height&&(width-gap*columns)/(columns+1)>92)columns++;
 if(state.density==='compact')columns=innerWidth<760?columns+1:Math.ceil(columns*1.4);
 library.style.setProperty('--library-columns',String(columns));library.classList.toggle('small-library-cards',(width-gap*(columns-1))/columns<110);
 const card=libraryCards()[0],rows=card?Math.max(1,Math.floor((height+18)/(card.getBoundingClientRect().height+18))):0;
 $('#library-density-count').textContent=card?`${columns} 列 · 一屏 ${columns*rows} 张`:'';
}
function syncLibrarySidebar(){
 const state=librarySidebarState();if(!state)return;
 const title=CATEGORY_DEFINITIONS[activeTab]?.label||'游戏库';$('#library-main-title').textContent=title;
 for(const tab of document.querySelectorAll('.page-tab')){
  if(!tab.querySelector('.sidebar-tab-icon')){const icon=document.createElement('i');icon.className='sidebar-tab-icon';icon.innerHTML=librarySidebarIcon(tab.dataset.tab==='native-steam'?'steam':tab.dataset.tab==='in-steam'?'checkCircle':tab.dataset.tab==='unclassified'?'alert':tab.dataset.tab==='non-game'?'ban':'clock');tab.prepend(icon);}
 }
 for(const id of ['commit-button','edit-add-to-steam']){const button=$('#'+id);if(button&&!button.querySelector('.sidebar-steam-icon')){const icon=document.createElement('i');icon.className='sidebar-steam-icon';icon.innerHTML=librarySidebarIcon('steam');button.prepend(icon);}}
 for(const card of libraryCards()){
  const image=card.querySelector('.cover-placeholder');if(!image||card.querySelector('.sidebar-card-check'))continue;
  const check=document.createElement('span');check.className='sidebar-card-check';check.setAttribute('aria-hidden','true');check.innerHTML=librarySidebarIcon('check');image.appendChild(check);
  const hint=document.createElement('span');hint.className='sidebar-card-edit';hint.setAttribute('aria-hidden','true');hint.innerHTML='<span class="controller-key controller-key--small" data-controller-key="X">X</span><span>编辑</span>';image.appendChild(hint);
 }
 $('#library-result-count').textContent=`${libraryCards().length} 个游戏`;
 syncLibrarySidebarDock();fitLibrarySidebarCards();
}
function librarySidebarMove(key){
 const state=librarySidebarState();if(!state)return false;
 const root=$('#library-view'),active=document.activeElement,all=visibleFocusables(root).filter(item=>!item.matches('.workspace-native-select')&&!item.closest('.workspace-dropdown-menu'));
 const card=activeLibraryCard(),aside=$('#library-sidebar');
 if(card&&state.page==='library'){
  const cards=navigableLibraryCards(),index=cards.indexOf(card),columns=Number(library.style.getPropertyValue('--library-columns'))||1,column=index%columns;let next=null;
  if(key==='ArrowLeft'){if(column)next=cards[index-1];else next=aside.querySelector('.page-tab.active');}
  else if(key==='ArrowRight'){if(column<columns-1)next=cards[index+1];}
  else if(key==='ArrowUp'){next=index>=columns?cards[index-columns]:$('#library-search');}
  else if(key==='ArrowDown'){next=cards[index+columns]||$('#library-dock-edit');}
  return focusWorkspaceElement(next)||true;
 }
 if(aside.contains(active)){
  if(key==='ArrowRight'){const from=active.getBoundingClientRect(),sameRow=visibleFocusables(aside).find(item=>{const box=item.getBoundingClientRect();return box.left>from.left+8&&Math.abs(box.top-from.top)<8;});if(sameRow)return focusWorkspaceElement(sameRow)||true;const target=state.page==='library'?libraryCards().find(item=>item.dataset.directory===state.lastCard)||libraryCards()[0]||$('#library-search'):visibleFocusables($('#library-management-page'))[0];return focusWorkspaceElement(target)||true;}
  return moveSpatialFocus(visibleFocusables(aside),key,active)||true;
 }
 if(active?.closest('#library-sidebar-dock')&&key==='ArrowUp'&&state.page==='library')return focusWorkspaceElement(libraryCards().find(item=>item.dataset.directory===state.lastCard)||libraryCards()[0]||$('#library-search'))||true;
 if(!all.includes(active))return focusWorkspaceElement(state.page==='library'?libraryCards()[0]||aside.querySelector('.page-tab.active'):visibleFocusables($('#library-management-page'))[0])||true;
 const next=spatialNavigationTarget(all.filter(item=>!aside.contains(item)),active,key);
 return focusWorkspaceElement(next||(key==='ArrowLeft'?aside.querySelector('.page-tab.active'):null))||true;
}
function installLibrarySidebar(){
 const root=$('#library-view');if(!root||root._sidebarState)return;
 root._sidebarState={page:'library',density:'standard',query:'',lastCard:''};root.dataset.libraryDensity='standard';
 document.body.classList.add('library-sidebar-shell');
 const heading=root.querySelector('.library-heading'),tabs=root.querySelector('.page-tabs'),menu=root.querySelector('.library-menu-row'),bar=root.querySelector('.steam-bar');
 const aside=document.createElement('aside');aside.id='library-sidebar';aside.setAttribute('aria-label','游戏库导航');aside.innerHTML='<div class="sidebar-library-brand">YeMan自定义游戏库</div><div class="sidebar-caption">游戏</div>';
 root.prepend(aside);aside.appendChild(tabs);
 const manage=document.createElement('div');manage.className='sidebar-management-actions';manage.innerHTML='<div class="sidebar-caption">管理</div>';
 for(const id of ['directory-settings','manual-game-settings'])manage.appendChild($('#'+id));
 const refresh=$('#refresh-artwork-button');manage.appendChild(refresh);aside.appendChild(manage);aside.appendChild(bar);
 const media=$('#media-download-progress');aside.appendChild(media);
 const workspace=document.createElement('div');workspace.className='library-sidebar-workspace';
 const main=document.createElement('div');main.id='library-main-page';main.className='library-sidebar-page';
 heading.querySelector('h1').id='library-main-title';heading.querySelector('.eyebrow').textContent='YeMan自定义游戏库';$('#back-entry-button').textContent='返回主程序';main.appendChild(heading);
 const tools=document.createElement('div');tools.className='library-sidebar-tools';tools.innerHTML='<label class="library-sidebar-search"><span>'+librarySidebarIcon('search')+'</span><input id="library-search" type="search" placeholder="搜索游戏名称…" aria-label="搜索游戏名称" autocomplete="off"></label><div class="library-sidebar-density" role="group" aria-label="封面密度"><button type="button" data-library-density="standard" class="active" aria-pressed="true">标准</button><button type="button" data-library-density="compact" aria-pressed="false">紧凑</button></div>';
 main.appendChild(tools);
 const meta=document.createElement('div');meta.className='library-sidebar-meta';meta.innerHTML='<span id="library-result-count"></span><span id="library-density-count"></span>';main.appendChild(meta);
 for(const id of ['in-steam-limitation','library-notice'])main.appendChild($('#'+id));
 const scroll=document.createElement('div');scroll.id='library-sidebar-scroll';scroll.append(library,$('#empty'));main.appendChild(scroll);workspace.appendChild(main);
 const management=document.createElement('section');management.id='library-management-page';management.className='library-sidebar-page hidden';management.innerHTML='<header class="library-management-heading"><button id="library-management-back" type="button" class="button ghost"><span class="controller-key sidebar-key key-b" data-controller-key="B">B</span>返回</button><div><div class="eyebrow">YeMan自定义游戏库 / 管理</div><h1 id="library-management-title">游戏库来源</h1></div></header><p id="library-management-description"></p><div id="library-management-content"></div>';
 for(const id of ['directory-settings-panel','manual-game-panel']){const panel=$('#'+id);panel.classList.remove('menu-popover-panel');panel.classList.add('library-management-panel');management.querySelector('#library-management-content').appendChild(panel);}
 workspace.appendChild(management);
 const dock=document.createElement('footer');dock.id='library-sidebar-dock';dock.innerHTML='<div id="library-dock-name"></div><div class="library-dock-actions"><button type="button" id="library-dock-select" class="button ghost"><span class="controller-key sidebar-key key-a" data-controller-key="A">A</span>选择</button><button type="button" id="library-dock-edit" class="button ghost"><span class="controller-key sidebar-key key-x" data-controller-key="X">X</span>编辑 / 单独加入</button><button type="button" id="library-dock-back" class="button ghost"><span class="controller-key sidebar-key key-b" data-controller-key="B">B</span><span data-library-back-label>返回</span></button></div>';workspace.appendChild(dock);
 root.append(aside,workspace);menu.remove();
 $('#directory-settings-toggle').setAttribute('aria-label','游戏库来源');$('#directory-settings-toggle').firstChild.textContent='游戏库来源 ';
 $('#manual-game-settings-toggle').setAttribute('aria-label','单独添加游戏');$('#manual-game-settings-toggle').firstChild.textContent='单独添加游戏 ';
 $('#pick-root-button').textContent='选择游戏库目录';$('#pick-game-button').textContent='选择游戏主程序';
 $('#library-search').addEventListener('input',event=>{root._sidebarState.query=event.target.value.trim().toLocaleLowerCase();renderLibrary();});
 $('#library-search').addEventListener('keydown',event=>{if(event.key==='ArrowDown'||event.key==='Enter'){event.preventDefault();event.stopPropagation();focusWorkspaceElement(libraryCards()[0]);}});
 tools.querySelectorAll('[data-library-density]').forEach(button=>button.addEventListener('click',()=>{const density=button.dataset.libraryDensity;root._sidebarState.density=density;root.dataset.libraryDensity=density;tools.querySelectorAll('[data-library-density]').forEach(item=>{item.classList.toggle('active',item===button);item.setAttribute('aria-pressed',String(item===button));});fitLibrarySidebarCards();}));
 $('#library-management-back').addEventListener('click',()=>librarySidebarPage());
 $('#library-dock-select').addEventListener('click',()=>{const game=librarySidebarCurrentGame();if(game)toggleGameSelection(game,true);});
 $('#library-dock-edit').addEventListener('click',()=>{const game=librarySidebarCurrentGame();if(game)openEdit(game);});
 $('#library-dock-back').addEventListener('click',()=>{if(root._sidebarState.page!=='library')librarySidebarPage();else focusWorkspaceElement(aside.querySelector('.page-tab.active'));});
 root.addEventListener('focusin',event=>{const card=event.target.closest('.game-card');if(card){root._sidebarState.lastCard=card.dataset.directory;syncLibrarySidebarDock();}});
 tabs.addEventListener('click',event=>{if(event.target.closest('.page-tab')&&root._sidebarState.page!=='library')librarySidebarPage('library',false);});
 const observer=new ResizeObserver(()=>fitLibrarySidebarCards());observer.observe(scroll);
 window.addEventListener('pagehide',()=>observer.disconnect(),{once:true});
 syncLibrarySidebar();
}

function selectedVisibleSteamDirectories(){const selected=selectedSteamDirectories===null?new Set(defaultSelectedSteamDirectories()):selectedSteamDirectories;return mergedGames().filter(game=>category(game)==='in-steam'&&selected.has(keyFor(game))&&librarySidebarMatches(game)).map(keyFor);}
function decorateLibrarySteamButton(button){if(!librarySidebarState()||!button||button.querySelector('.sidebar-steam-icon'))return;const icon=document.createElement('i');icon.className='sidebar-steam-icon';icon.innerHTML=librarySidebarIcon('steam');button.prepend(icon);}
const LIBRARY_SIDEBAR_ICONS={"grid": "<rect x=\"3\" y=\"3\" width=\"7\" height=\"7\" rx=\"1\"/><rect x=\"14\" y=\"3\" width=\"7\" height=\"7\" rx=\"1\"/><rect x=\"3\" y=\"14\" width=\"7\" height=\"7\" rx=\"1\"/><rect x=\"14\" y=\"14\" width=\"7\" height=\"7\" rx=\"1\"/>", "clock": "<circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M12 7v5l3 2\"/>", "checkCircle": "<circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"m7.5 12 3 3 6-6\"/>", "check": "<path d=\"m5 12 4 4L19 6\"/>", "alert": "<path d=\"M12 3 2 21h20L12 3Z\"/><path d=\"M12 9v5m0 3h.01\"/>", "ban": "<circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"m6 6 12 12\"/>", "search": "<circle cx=\"10.5\" cy=\"10.5\" r=\"6.5\"/><path d=\"m16 16 5 5\"/>", "steam": "<path fill=\"currentColor\" stroke=\"none\" opacity=\"0.66\" fill-rule=\"evenodd\" d=\"M11.7857 0.378L11.4429 0.4271 10.8429 0.4321 10.6714 0.5083 10.0714 0.5212 9.9857 0.5787 9.3 0.7033 9.2143 0.764 9.0429 0.7755 8.6143 0.9385 8.4429 0.9503 8.1 1.1065 7.9286 1.123 7.8429 1.1917 7.5 1.2942 6.4714 1.8083 5.9571 2.1346 5.8714 2.1512 5.7 2.3044 5.6143 2.3242 5.1 2.7384 4.9286 2.833 4.5857 3.1659 4.4143 3.261 3.3438 4.3286 2.4951 5.4429 2.214 5.9571 2.0654 6.1286 1.9631 6.3857 1.7303 6.7286 1.5343 7.2429 1.3802 7.5 1.3456 7.6714 1.2116 7.9286 1.1894 8.1 1.1227 8.1857 1.1056 8.3571 1.0373 8.4429 1.0098 8.6143 0.9498 8.7 0.9341 8.9571 0.8659 9.0429 0.8522 9.2143 0.7755 9.3857 0.757 9.6429 0.6874 9.8143 0.6819 10.0714 0.6052 10.2429 0.5978 10.7571 0.5163 11.0143 0.5145 12.6429 0.5879 12.9 0.6015 13.4143 0.6819 13.6714 0.6929 14.0143 0.7548 14.1 0.7805 14.4429 0.8346 14.5286 0.8659 14.7857 0.9341 14.8714 0.9516 15.1286 1.019 15.2143 1.0373 15.3857 1.1056 15.4714 1.2081 15.9 1.2744 15.9857 1.3793 16.3286 1.5341 16.5857 1.7228 17.1 2.1346 17.7857 2.1512 17.8714 2.9188 18.9857 3.3857 19.5422 4.4602 20.6143 5.8714 21.6774 7.3286 22.4484 7.5 22.4659 7.9286 22.706 8.2714 22.8085 8.3571 22.877 8.8714 22.9815 8.9571 23.0487 9.2143 23.0612 9.3857 23.1385 9.5571 23.1544 9.6429 23.2202 9.9857 23.2373 10.0714 23.3056 10.5 23.3266 10.7571 23.3975 11.4429 23.4101 11.7 23.4835 12.3 23.485 12.6429 23.4033 13.2429 23.3975 13.5857 23.3163 13.9286 23.3074 14.1 23.2306 14.4429 23.2188 14.5286 23.1544 14.7857 23.1396 14.9571 23.0623 15.4714 22.9627 15.5571 22.8944 15.7286 22.8672 15.9857 22.7326 16.3286 22.6216 16.5857 22.4723 16.7571 22.4484 17.6143 22.0203 18.3 21.593 18.4714 21.4369 18.5571 21.4203 18.8143 21.2501 18.9857 21.0881 19.1571 20.994 19.7571 20.4295 19.8429 20.3939 20.3131 19.9286 20.9953 19.1571 21.0904 18.9857 21.3371 18.7286 21.5872 18.3857 21.9512 17.7 22.1889 17.3571 22.5289 16.6714 22.6369 16.3286 22.7058 16.2429 23.1382 14.9571 23.1483 14.7857 23.2251 14.6143 23.2389 14.3571 23.3115 14.1857 23.3173 14.0143 23.3945 13.8429 23.4007 13.4143 23.4847 13.1571 23.4875 12.6429 23.5662 12.2143 23.5707 11.8714 23.4923 11.3571 23.4832 10.7571 23.4066 10.5857 23.3945 9.9857 23.2338 9.4714 23.2242 9.2143 23.1472 9.0429 23.1374 8.8714 22.7056 7.5857 22.5557 7.3286 22.3629 6.8143 21.9512 6.1286 21.9346 6.0429 21.7826 5.8714 21.4915 5.3571 20.9945 4.6714 20.6629 4.3286 20.4833 4.0714 20.0568 3.6429 19.7571 3.4219 19.2429 2.9198 19.0714 2.825 18.5571 2.4119 18.0429 2.131 17.8714 1.9797 17.2714 1.6864 17.1 1.5623 16.9286 1.5211 16.1571 1.1479 15.0429 0.7761 14.4429 0.6691 14.3571 0.6091 14.1 0.5956 13.9286 0.5184 13.4143 0.5091 13.2429 0.4318ZM10.8429 2.233L10.6714 2.3099 10.4143 2.3305 10.2429 2.3967 9.9857 2.402 9.8143 2.4786 9.5571 2.4944 9.4714 2.5627 8.9571 2.6789 8.5286 2.8371 8.4429 2.9058 8.2714 2.9412 7.7571 3.1802 6.9857 3.6075 6.4714 3.9489 6.3 4.106 6.2143 4.1226 5.7 4.5438 4.9739 5.2714 4.3821 5.9571 3.7787 6.8143 3.0944 8.1 3.0695 8.2714 2.923 8.5286 2.9056 8.7 2.8373 8.7857 2.7478 9.0429 2.5627 9.9 2.4953 9.9857 2.483 10.4143 2.4121 10.5857 2.3951 11.1 2.3326 11.2714 2.4429 11.3917 2.6143 11.4747 2.9571 11.5843 3.3857 11.8198 3.5571 11.8373 3.9857 12.077 4.1571 12.0944 4.4143 12.2484 4.5857 12.2659 5.0143 12.5058 5.3571 12.6083 5.7857 12.8484 5.9571 12.8659 6.2143 13.0197 6.5571 13.124 6.8143 13.277 6.9857 13.277 7.1571 13.1226 7.6714 12.9346 7.7571 12.8659 8.1857 12.8549 8.6143 12.7357 11.1309 9.1286 11.1516 8.7 11.2128 8.6143 11.2332 8.3571 11.3897 8.0143 11.4087 7.8429 11.5797 7.5 12.0033 6.9 12.3857 6.5193 12.5571 6.4247 12.7286 6.2654 13.4143 5.923 13.5857 5.9036 13.6714 5.837 13.8429 5.8256 14.0143 5.7523 14.3571 5.7351 14.6143 5.6571 14.8714 5.6691 15.0429 5.7393 15.3 5.7451 15.4714 5.8155 15.7286 5.8462 16.3286 6.094 16.6714 6.3353 16.7571 6.3572 17.1857 6.7562 17.5631 7.1571 17.5797 7.2429 17.8203 7.5857 18.0085 8.1 18.077 8.1857 18.0964 8.4429 18.163 8.5286 18.1742 8.7857 18.2549 9.0429 18.2567 9.5571 18.1752 9.8143 18.1659 9.9857 18.0892 10.1571 18.0802 10.3286 17.7346 11.1 17.4827 11.4429 16.9286 12 16.5 12.331 15.9857 12.5913 15.8143 12.6052 15.4714 12.7544 14.8714 12.7943 14.3571 13.1187 14.1857 13.2797 13.7571 13.5489 12.9857 14.1374 12.6429 14.3327 12.2143 14.6499 12.1286 14.6725 11.3571 15.2491 11.2714 15.2698 11.1535 15.3857 11.1406 15.9 11.0638 16.0714 11.029 16.3286 10.7917 16.8429 10.6226 17.1 10.2429 17.4836 9.6429 17.888 9.4714 17.9228 9.3857 17.9917 9.1286 18.0819 8.9571 18.0893 8.7857 18.1627 8.2714 18.17 8.0143 18.0887 7.8429 18.0788 7.4143 17.9096 6.8143 17.5659 6.4356 17.1857 6.1045 16.6714 6.077 16.5 6.0087 16.4143 5.9198 16.1571 5.9024 15.9 5.7857 15.7214 5.0143 15.421 4.7571 15.2659 4.5857 15.2484 4.3286 15.0946 3.9857 14.9915 3.7286 14.8373 3.5571 14.8198 3.1286 14.5799 2.7 14.4244 2.6685 14.5286 2.7346 14.6143 2.7483 14.7857 2.8373 15.0429 2.9056 15.1286 2.923 15.3 3.0605 15.5571 3.0944 15.7286 3.3329 16.1571 3.3599 16.3286 3.4974 16.5 3.7009 16.9286 4.2709 17.7 4.2932 17.7857 4.717 18.3 5.6581 19.2429 6.4714 19.8797 7.4143 20.4774 8.2714 20.9036 8.4429 20.923 8.7 21.077 8.8714 21.0941 8.9571 21.1608 9.5571 21.3377 9.8143 21.3519 9.9857 21.4261 10.1571 21.4373 10.2429 21.5056 10.7571 21.5291 10.8429 21.5925 10.9286 21.5995 12.0429 21.6336 13.2429 21.5894 13.3286 21.5227 13.7571 21.5056 13.8429 21.4373 14.1 21.4229 14.2714 21.3495 14.5286 21.3341 14.6143 21.2659 14.8714 21.2386 15.1286 21.1027 15.3857 21.0622 16.6714 20.4774 16.8429 20.3226 17.1 20.2159 17.5286 19.894 17.6143 19.8774 17.8714 19.6369 17.9571 19.6203 18.4714 19.1882 19.1134 18.5571 19.6957 17.8714 19.7213 17.7857 20.1364 17.2714 20.8201 16.0714 21.1681 15.2143 21.2765 14.7857 21.3359 14.7 21.3516 14.5286 21.4198 14.4429 21.4326 14.1857 21.5957 13.6714 21.6066 13.2429 21.6832 13.0714 21.6842 10.8429 21.6156 10.6714 21.5957 10.1571 21.4335 9.6429 21.4211 9.3857 21.3582 9.3 21.2517 8.8714 20.9942 8.1857 20.8426 7.9286 20.8169 7.7571 20.5619 7.2429 19.9797 6.3857 19.9631 6.3 19.8083 6.1286 19.7571 6.0016 18.7714 4.9286 18.0429 4.2902 16.9286 3.5226 16.8429 3.506 16.6714 3.3567 15.9857 3.0774 15.7286 2.9228 15.3857 2.8176 15.3 2.7509 15.1286 2.7341 15.0429 2.6659 14.6143 2.563 14.5286 2.4951 14.1857 2.4788 14.1 2.4208 13.8429 2.3937 13.6714 2.3165 13.3286 2.312 13.1571 2.2349ZM14.4429 6.6097L14.3571 6.6763 14.0143 6.7047 13.5 6.8851 12.9857 7.2227 12.6915 7.5 12.3512 8.0143 12.1854 8.3571 12.1659 8.5286 12.0885 8.7 12.0887 9.8143 12.1659 9.9857 12.1814 10.1571 12.4338 10.6714 12.6869 11.0143 12.9857 11.3104 13.5 11.6486 13.8429 11.7512 13.9286 11.8198 14.2714 11.8357 14.4429 11.9123 14.9571 11.908 15.1286 11.8308 15.3857 11.8237 15.9857 11.5631 16.2844 11.3571 16.8765 10.7571 17.1341 10.2429 17.2358 9.8143 17.2973 9.7286 17.3056 8.8714 17.2382 8.7857 17.1341 8.2714 16.9631 7.9286 16.7571 7.6295 16.2429 7.1327 15.8143 6.8679 15.3 6.6944 15.0429 6.6788 14.8714 6.602ZM14.5286 7.4687L14.3571 7.5409 14.1 7.5516 13.7571 7.7226 13.3841 8.0143 13.0373 8.5286 13.0198 8.7 12.9516 8.7857 12.8839 9.3857 13.0373 9.9857 13.2771 10.3286 13.2969 10.4143 13.5 10.6247 14.1 10.9627 14.5286 11.0552 14.8714 11.0528 15.4714 10.8774 15.7705 10.6714 16.1814 10.2429 16.3516 9.9 16.3802 9.7286 16.4484 9.6429 16.4549 8.9571 16.3427 8.6143 16.1093 8.1857 15.7286 7.806 15.2143 7.5464 14.9571 7.5263 14.7857 7.4584ZM8.1857 13.7351L8.1857 13.7935 8.2714 13.8783 8.9105 14.1857 9.1654 14.4429 9.3195 14.7 9.4283 15.0429 9.4273 15.5571 9.3346 15.8143 9.2143 16.0269 8.7857 16.4488 8.3571 16.6185 7.8429 16.6163 7.3286 16.4286 7.1571 16.3103 7.0714 16.291 6.9663 16.3286 7.3286 16.7662 7.5857 16.9631 7.8477 17.1 8.2714 17.2268 8.7857 17.2198 8.8714 17.1516 9.1286 17.1221 9.6429 16.7948 10.0226 16.3286 10.1988 15.9 10.19 14.9571 9.9346 14.4429 9.4714 13.9797 9.1286 13.8044 8.9571 13.7916 8.8714 13.7237Z\"/>"};
installLibrarySidebar();
