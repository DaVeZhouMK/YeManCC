// Generated from steam_live.js; do not edit.
#pragma once
namespace ymcc::steamlive {
inline constexpr const char kScript[] = R"YMSTEAM(// Embedded in steam_live_script.h. Regenerate/check with tools/steam_live_script_embed.mjs.
(async function ymccSteamLive(request) {
  let mutated = false, stage = 'preflight';
  const token = `${Date.now()}-${Math.random()}`;
  const fail = reason => { throw Object.assign(new Error(reason), { reason }); };
  const bounded = async promise => {
    let timer;
    try { return await Promise.race([Promise.resolve(promise), new Promise((_, reject) => {
      timer = setTimeout(() => reject(Object.assign(new Error('live-timeout'), { reason: 'live-timeout' })), 4000);
    })]); } finally { clearTimeout(timer); }
  };
  const waitFor = async test => {
    const end = performance.now() + 3000;
    while (!test()) { if (performance.now() >= end) fail('live-readback-failed'); await new Promise(r => setTimeout(r, 25)); }
  };
  if (window.__ymccSteamLiveBusy) return { ok: false, reason: 'live-busy', mutated: false, stage };
  window.__ymccSteamLiveBusy = token;
  try {
    const checkAccount = () => {
      const id = window.settingsStore?.m_CMInterface?.steamid;
      if (!id || typeof id.GetAccountID !== 'function') fail('live-api-unavailable');
      if (!Number.isInteger(request.account) || id.GetAccountID() !== request.account) fail('steam-account-changed');
    };
    checkAccount();
    if (request.operation === 'menu.toggle') {
      if (!['main', 'quick-access'].includes(request.menu)) fail('invalid-steam-menu');
      const s = window.SteamUIStore;
      const w = s?.GetFocusedWindowInstance?.() || s?.WindowStore?.GamepadUIMainWindowInstance;
      const m = w?.MenuStore;
      if (!s || !w || !m || typeof w.BHasMenus !== 'function' || !w.BHasMenus() ||
          typeof m.GetOpenSideMenu !== 'function' || typeof m.ToggleSideMenu !== 'function') fail('live-api-unavailable');
      if (s.GetShowingLockScreen?.() || s.BHomeAndQuickAccessButtonsEnabled?.() === false ||
          s.WindowStore?.BHasStandaloneKeyboard?.() || w.IsAnyVRWindow?.() || m.m_cSuppressRequests > 0) fail('steam-menu-blocked');
      // Steam MenuStore: 0=None, 1=Main, 2=QuickAccess. Verify its state/API
      // before using the enum; never depend on Decky or a connected controller.
      const before = m.GetOpenSideMenu(), menu = request.menu === 'main' ? 1 : 2;
      if (![0, 1, 2].includes(before)) fail('live-api-unavailable');
      const desired = before === menu ? 0 : menu;
      checkAccount(); stage = 'menu'; mutated = true;
      await bounded(m.ToggleSideMenu(menu, desired !== 0));
      await waitFor(() => m.GetOpenSideMenu() === desired);
      checkAccount();
      return { ok: true, before, menu: desired, runtimeAccepted: true, via: 'live' };
    }
    if (request.operation === 'settings.get' || request.operation === 'settings.set') {
      const s = window.settingsStore;
      if (!s || typeof s.GetClientSetting !== 'function' || typeof SteamClient?.Settings?.SetSetting !== 'function') fail('live-api-unavailable');
      const specs = {
        enable_overlay: ['boolean'], overlay_fps_counter_corner: ['number',0,6,true],
        overlay_fps_counter_detail_level: ['number',1,4,true], overlay_fps_counter_scale_factor: ['number',.2,1.4],
        overlay_fps_counter_saturation_factor: ['number',0,1], overlay_fps_counter_bgopacity: ['number',0,1],
      };
      const keys = request.operation === 'settings.get' ? request.keys : Object.keys(request.values || {});
      if (!Array.isArray(keys) || !keys.length || keys.length>6 || new Set(keys).size!==keys.length) fail('invalid-steam-settings');
      const before = {}, desired = {};
      for (const key of keys) {
        const spec=specs[key];if(!spec)fail('invalid-steam-settings');
        const pair=s.GetClientSetting(key);
        if(typeof pair?.[0]!==spec[0] || typeof pair?.[1]!=='function')fail('live-api-unavailable');
        before[key]=pair[0];
        if(request.operation==='settings.set'){
          const value=request.values[key];
          if(typeof value!==spec[0] || (spec[0]==='number' && (!Number.isFinite(value) || value<spec[1]-1e-6 || value>spec[2]+1e-6 || (spec[3] ? !Number.isInteger(value) : Math.abs(value*10-Math.round(value*10))>1e-5))))fail('invalid-steam-settings');
          desired[key]=value;
        }
      }
      if(request.operation==='settings.get')return {ok:true,values:before,via:'live'};
      // Preflight every field before the first mutation. One request/one Steam context,
      // setters remain Steam-owned; never write its live VDF from YMCC.
      for(const key of keys){
        checkAccount();const value=desired[key];
        if(before[key]!==value){stage='setting';mutated=true;await bounded(s.GetClientSetting(key)[1](value));}
      }
      await waitFor(()=>keys.every(key=>{const value=s.GetClientSetting(key)[0];return typeof value==='number'?Math.abs(value-desired[key])<1e-5:value===desired[key]}));
      checkAccount();return {ok:true,values:desired,before,runtimeAccepted:true,via:'live'};
    }
    if (request.operation === 'overlay.get' || request.operation === 'overlay.set') {
      const s = window.settingsStore;
      if (!s || typeof s.GetClientSetting !== 'function' || typeof SteamClient?.Settings?.SetSetting !== 'function') fail('live-api-unavailable');
      const pair = s.GetClientSetting('enable_overlay');
      if (typeof pair?.[0] !== 'boolean' || typeof pair?.[1] !== 'function') fail('live-api-unavailable');
      if (request.operation === 'overlay.get') return { ok: true, value: pair[0] ? 1 : 0, via: 'live' };
      if (typeof request.enabled !== 'boolean') fail('invalid-overlay-value');
      const before = pair[0];
      if (before !== request.enabled) {
        stage = 'setting'; mutated = true;
        await bounded(pair[1](request.enabled));
        await waitFor(() => s.GetClientSetting('enable_overlay')[0] === request.enabled);
      }
      checkAccount();
      return { ok: true, value: request.enabled ? 1 : 0, before: before ? 1 : 0, runtimeAccepted: true, via: 'live' };
    }
    if (request.operation !== 'mouse.get' && request.operation !== 'mouse.set') fail('invalid-live-operation');
    if (!['controller_neptune', 'controller_generic'].includes(request.controllerType)) fail('invalid-controller-type');
    const s = window.controllerConfiguratorStore, c = window.ControllerStore;
    if (!s || !c || !['EnsureEditingConfiguration','SetControllerSourceMode','SaveEditingConfiguration'].every(k => typeof s[k] === 'function') || typeof c.GetControllers !== 'function' || typeof c.GetControllerTypeString !== 'function') fail('live-api-unavailable');
    const controllers = c.GetControllers();
    if (!Array.isArray(controllers)) fail('live-controller-not-connected');
    const matches = controllers.filter(x => c.GetControllerTypeString(x.eControllerType) === request.controllerType);
    if (matches.length !== 1) fail(matches.length ? 'live-controller-ambiguous' : 'live-controller-not-connected');
    const controller = matches[0], index = controller.nControllerIndex;
    if (!Number.isInteger(index) || index < 0) fail('live-controller-ambiguous');
    const checkIdle = () => {
      if (typeof s.m_nEditNumber !== 'number' || typeof s.m_nLastSavedEditNumber !== 'number') fail('live-api-unavailable');
      if (s.m_nEditNumber !== s.m_nLastSavedEditNumber || s.IsUpdatingEditingConfiguration || s.PreviewedConfiguration) fail('live-editor-busy');
      if (s.EditingConfigurationAppId != null && s.EditingConfigurationAppId !== -1 && s.EditingConfigurationAppId !== 413080) fail('live-editor-busy');
      if (s.EditingConfigurationAppId === 413080 && s.EditingConfigurationControllerIndex != null && s.EditingConfigurationControllerIndex !== index) fail('live-editor-busy');
    };
    checkIdle();
    stage = 'load';
    s.EnsureEditingConfiguration(413080, index);
    await bounded(s.m_updatingEditingConfigurationPromise);
    checkAccount();
    checkIdle();
    const normalize = x => String(x).replaceAll('\\', '/').toLowerCase();
    const read = (afterSave = false) => {
      const cfg = JSON.parse(JSON.stringify(s.EditedConfiguration));
      const appMatches = s.EditingConfigurationAppId === 413080 || (afterSave && s.EditingConfigurationAppId === -1 && s.StableAppId === 413080);
      if (!appMatches || s.EditingConfigurationControllerIndex !== index || cfg.controller_type !== controller.eControllerType || normalize(cfg.url) !== normalize('autosave://' + request.path)) fail('live-layout-mismatch');
      if (!Array.isArray(cfg.sets)) fail('live-api-unavailable');
      const sets = cfg.sets.filter(x => x.key === 'Default');
      if (sets.length !== 1 || !Array.isArray(sets[0].source_bindings)) fail('live-binding-mismatch');
      const sources = sets[0].source_bindings.filter(x => x.key === 12);
      const group = sources[0]?.active_group;
      if (sources.length !== 1 || group?.mode !== 7 || group.mode_shift || !Array.isArray(group.settings)) fail('live-binding-mismatch');
      const settings = group.settings.filter(x => x.key === 30);
      if (settings.length !== 1 || !Number.isInteger(settings[0].int_value)) fail('live-binding-mismatch');
      return { group, setting: settings[0] };
    };
    let { group, setting } = read();
    if (request.operation === 'mouse.get') return { ok: true, percent: setting.int_value, controllerIndex: index, via: 'live' };
    if (!Number.isInteger(request.percent) || request.percent < 1 || request.percent > 300) fail('invalid-sensitivity');
    if (setting.int_value !== request.baseline) fail('desktop-sensitivity-changed');
    const before = setting.int_value;
    if (before !== request.percent) {
      stage = 'edit'; mutated = true;
      s.SetControllerSourceMode(413080, { action_set_key: 'Default', source_binding_key: 12,
        modeid: group.modeid, mode_shift: false, new_setting: { key: 30, int_value: request.percent } });
      await bounded(s.m_updatingEditingConfigurationPromise);
      if (read().setting.int_value !== request.percent) fail('live-value-not-accepted');
      // If another UI/native client changed an additional field, do not save its edits for it.
      if (s.m_nEditNumber !== s.m_nLastSavedEditNumber + 1) fail('live-editor-changed');
      stage = 'save';
      await bounded(new Promise(resolve => s.SaveEditingConfiguration(413080, false, resolve)));
      await waitFor(() => s.m_nEditNumber === s.m_nLastSavedEditNumber);
    }
    if (read(true).setting.int_value !== request.percent) fail('live-readback-failed');
    stage = 'verify';
    checkAccount();
    const current = (c.GetControllers() || []).filter(x => c.GetControllerTypeString(x.eControllerType) === request.controllerType);
    if (current.length !== 1 || current[0].nControllerIndex !== index) fail('live-controller-changed');
    return { ok: true, percent: request.percent, before, controllerIndex: index, runtimeAccepted: true, saved: true, via: 'live' };
  } catch (error) {
    return { ok: false, reason: error.reason || 'live-api-error', stage, mutated };
  } finally {
    if (window.__ymccSteamLiveBusy === token) delete window.__ymccSteamLiveBusy;
  }
}))YMSTEAM";
}
