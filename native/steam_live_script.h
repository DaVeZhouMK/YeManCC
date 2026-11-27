// Generated from steam_live.js; do not edit. Adjacent literals avoid MSVC C2026.
#pragma once
namespace ymcc::steamlive {
inline constexpr const char kScript[] =
R"YMSTEAM(// Embedded in steam_live_script.h. Regenerate/check with tools/steam_live_script_embed.mjs.
(async function ymccSteamLive(request) {
  let mutated = false, stage = 'preflight', menuTarget = null, keyDelivered = false, settledNativeReturn = null;
  const token = `${Date.now()}-${Math.random()}`;
  const lightGamepadLease = request.operation === 'input.gamepadLease';
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
  if (!lightGamepadLease && window.__ymccSteamLiveBusy) return { ok: false, reason: 'live-busy', mutated: false, stage };
  if (!lightGamepadLease) window.__ymccSteamLiveBusy = token;
  try {
    // Only the independent, account/process-bound running-link read may own a
    // transient native subscription. This flag never grants a setter authority.
    if (request.nativeFocusFeedbackRead !== undefined &&
        (typeof request.nativeFocusFeedbackRead !== 'boolean' || request.operation !== 'sleep.inspect' || request.runningLinkRead !== true))
      fail('invalid-steam-native-focus-read');
    if (request.nativeAppLifetimeFeedbackRead !== undefined &&
        (typeof request.nativeAppLifetimeFeedbackRead !== 'boolean' || request.operation !== 'sleep.inspect' || request.runningLinkRead !== true))
      fail('invalid-steam-app-lifetime-read');
    if (request.nativeRecordIdentityRead !== undefined &&
        (typeof request.nativeRecordIdentityRead !== 'boolean' || !['sleep.session.inspect','sleep.inspect'].includes(request.operation)))
      fail('invalid-steam-native-record-read');
    // A private native diagnostic identity channel. The native caller strips
    // this reply before logging/export; no SteamID is persisted in JS state.
    const readNativeRecordIdentity = () => {
      const unknown=reason=>({status:'unavailable',reason});
      if(request.nativeRecordIdentityRead!==true)return null;
      try {
        if(!Number.isInteger(request.account)||request.account<=0||request.account>0xffffffff)return unknown('native-record-ui-account-unavailable');
        const ids=request.appids;
        if(!Array.isArray(ids)||ids.length!==1||!Number.isInteger(ids[0])||ids[0]<=0||ids[0]>0xffffffff)return unknown('native-record-ui-game-unavailable');
        const id=window.settingsStore?.m_CMInterface?.steamid;
        if(!id||typeof id.GetAccountID!=='function'||typeof id.ConvertTo64BitString!=='function')return unknown('native-record-ui-account-unavailable');
        if(Function.prototype.toString.call(id.ConvertTo64BitString)!=='ConvertTo64BitString(){return this.m_ulSteamID.toString()}')return unknown('native-record-ui-identity-schema-changed');
        const account=id.GetAccountID();
        if(account!==request.account)return unknown('native-record-ui-account-unavailable');
        const full=id.ConvertTo64BitString();
        if(typeof full!=='string'||!/^[1-9][0-9]{0,19}$/.test(full)||(BigInt(full)>0xffffffffffffffffn||(BigInt(full)&0xffffffffn)!==BigInt(account)))return unknown('native-record-ui-account-unavailable');
        const map=window.appStore?.m_mapApps;
        if(!map||typeof map.get!=='function')return unknown('native-record-ui-game-unavailable');
        const unsigned=map.get(ids[0]),signed=map.get(ids[0]|0);
        if(unsigned&&signed&&unsigned!==signed)return unknown('native-record-ui-game-unavailable');
        const overview=unsigned||signed,appid=overview?.appid,gameid=overview?.gameid,type=overview?.app_type;
        if(typeof appid!=='number'||!Number.isInteger(appid)||appid< -0x80000000||appid>0xffffffff||(appid>>>0)!==ids[0]||
          typeof gameid!=='string'||!/^[1-9][0-9]{0,19}$/.test(gameid)||!Number.isInteger(type)||type<0||type>0xffffffff)return unknown('native-record-ui-game-unavailable');
        const raw=BigInt(gameid),shortcut=(type&0x40000000)!==0;
        if(raw>0xffffffffffffffffn||!((!shortcut&&raw===BigInt(ids[0])&&raw<=0xffffffn)||(shortcut&&raw>>32n===BigInt(ids[0])&&(raw&0xffffffffn)===0x02000000n)))return unknown('native-record-ui-game-unavailable');
        if(window.settingsStore?.m_CMInterface?.steamid!==id||id.GetAccountID()!==account||id.ConvertTo64BitString()!==full||overview.gameid!==gameid)return unknown('native-record-ui-identity-raced');
        return {status:'observed',identity:id,overview,steamid64:full,gameid64:gameid,appid:ids[0]};
      } catch {return unknown('native-record-ui-identity-unavailable');}
    };
    const privateRecordStart=readNativeRecordIdentity();
    const privateNativeRecordReply=()=>{
      if(request.nativeRecordIdentityRead!==true)return {};
      const end=readNativeRecordIdentity();
      if(privateRecordStart?.status!=='observed'||end?.status!=='observed')return {_ymccPrivateNativeRecord:{status:'unavailable',reason:privateRecordStart?.reason||end?.reason||'native-record-ui-identity-unavailable'}};
      if(privateRecordStart.identity!==end.identity||privateRecordStart.overview!==end.overview||privateRecordStart.steamid64!==end.steamid64||privateRecordStart.gameid64!==end.gameid64||privateRecordStart.appid!==end.appid)
        return {_ymccPrivateNativeRecord:{status:'unavailable',reason:'native-record-ui-identity-raced'}};
      return {_ymccPrivateNativeRecord:{status:'observed',source:'steam-ui-exact-native-record-identity',steamid64:end.steamid64,gameid64:end.gameid64,appid:end.appid}};
    };
    const checkAccount = () => {
      // Explicit menu keys act on Steam's current UI context, not account data.
      // This fixed operation list grants no settings, game or input-lease access.
      if (['menu.button', 'menu.toggle', 'menu.inspect', 'menu.qam.prepare',
           'menu.qam.observe', 'menu.qam.retry', 'menu.qam.dispose'].includes(request.operation)) return;
      const id = window.settingsStore?.m_CMInterface?.steamid;
      if (!id || typeof id.GetAccountID !== 'function') fail('live-api-unavailable');
      const activeAccount = id.GetAccountID();
      if (!Number.isInteger(activeAccount) || activeAccount <= 0) fail('steam-session-starting');
      if (!Number.isInteger(request.account) || activeAccount !== request.account) fail('steam-account-changed');
    };
    // Approved bounded repair: a retained document with a dead old backend must
    // not trap the product behind the no-repeat launch gate. Only an explicit repair
    // click can rebuild UI; automatic callers fail closed before any side effect.
    if (request.operation === 'decky.repairConnection' || request.operation === 'decky.recoverConnection' || request.operation === 'decky.cleanupConnection') {
      stage = 'decky-manual-repair';
      const hex = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
      const automatic = request.operation === 'decky.recoverConnection';
      const cleanup = request.operation === 'decky.cleanupConnection';
      // RestartJSContext recreates the Big Picture window and may steal foreground.
      // Fail before any mode read, timer, marker or UI mutation, including stale native callers.
      if (automatic) fail('steam-ui-refresh-requires-user');
      const emptyBridgePin = Object.prototype.hasOwnProperty.call(request, 'expectedEmptyBridgeBuild');
      if (Object.keys(request).length !== (cleanup ? 7 : 5) + (emptyBridgePin ? 1 : 0) ||
          (emptyBridgePin && (request.expectedFrontendBuild !== null || !hex(request.expectedEmptyBridgeBuild))) || (cleanup ? request.cleanupAuthorized !== true : automatic ? request.automatic !== true : request.userInitiated !== true) ||
          !(hex(request.expectedFrontendBuild) || request.expectedFrontendBuild === null) || !hex(request.installedFrontendBuild) ||
          !/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(request.loaderVe)YMSTEAM"
R"YMSTEAM(rsion || '')) fail('invalid-decky-repair-request');
      const loader = window.DeckyPluginLoader, frontend = window.__YMCC_DECKY_FRONTEND__;
      // Null is an explicit absence contract, never a fabricated module hash.
      const missing = request.expectedFrontendBuild === null;
      const ownEmpty = () => frontend == null && Array.isArray(loader?.plugins) && loader.plugins.length === 0 &&
        window.__YMCC_DECKY_MIRROR__?.frontendBuildId === (emptyBridgePin ? request.expectedEmptyBridgeBuild : request.installedFrontendBuild) &&
        /^[a-f0-9]{32}$/.test(window.__YMCC_DECKY_MIRROR__?.runId || '') &&
        /^[a-f0-9]{64}$/.test(window.__YMCC_DECKY_MIRROR__?.token || '') &&
        new RegExp('^ws://127[.]0[.]0[.]1:[1-9][0-9]{0,4}/mirror$').test(window.__YMCC_DECKY_MIRROR__?.endpoint || '');
      const cleanupCurrent = () => !cleanup || (typeof request.expectedModeEpoch === 'string' &&
        request.expectedModeEpoch.length <= 96 && Number.isInteger(request.expectedAccount) &&
        request.expectedAccount > 0 && request.expectedAccount <= 0xffffffff &&
        window.__YMCC_DECKY_ACCOUNT_WATCH__?.read?.(request.expectedAccount) === true &&
        window.__ymccSteamUiModeWatch?.mode === 'bigpicture' &&
        window.__ymccSteamUiModeWatch.id + ':' + window.__ymccSteamUiModeWatch.entries === request.expectedModeEpoch);
      const hasRegisteredFrontend = () => {
        const rows = loader?.plugins;
        // Validate sparse slots too, before a matching row can short-circuit the search.
        return Array.isArray(rows) && rows.findIndex(row => row == null) === -1 &&
          rows.some(row => row.name === 'ymcc-sidebar');
      };
      const current = () => cleanupCurrent() && window === window.top && location.origin === 'https://steamloopback.host' &&
        window.DeckyPluginLoader === loader && window.__YMCC_DECKY_FRONTEND__ === frontend &&
        (missing ? ownEmpty() : frontend?.active === true && frontend.buildId === request.expectedFrontendBuild && hasRegisteredFrontend()) &&
        loader.deckyState?.publicState?.()?.versionInfo?.current === request.loaderVersion;
      if (!current() || typeof SteamClient?.UI?.GetUIMode !== 'function' ||
          typeof SteamClient?.Browser?.RestartJSContext !== 'function') fail('decky-repair-context-changed');
      const mode = await bounded(SteamClient.UI.GetUIMode());
      if ((mode !== 1 && mode !== 4) || !current()) fail('decky-repair-context-changed');
      if (window.__ymccDeckyManualRepair) fail('decky-repair-already-requested');
      // Steam's existing export log can now distinguish a queued repair from
      // the actual restart call. Fixed facts only; observation never gates it.
      const traceRepair = phase => {
        try { console.info('[YMCC:DeckyRepair]', JSON.stringify({schema:1,origin:cleanup?'final-cleanup':'manual',phase})); } catch {}
      };
      const repairReservation = {}; let restartEntered = false;
      window.__ymccDeckyManualRepair = repairReservation;
      // A scheduled/uncertain dispatch is not a successful handshake. The native
      // owner and actual snapshot remain the only completion evidence.
      setTimeout(() => {
        if (restartEntered || window.__ymccDeckyManualRepair !== repairReservation) return;
        let stillCurrent = false;
        try { stillCurrent = current(); } catch {} // The restart API has not been entered.
        if (!stillCurrent) {
          traceRepair('cancelled-before-call');
          // A positively cancelled callback may release ONLY its own JS gate,
          // so a fresh explicit repair can proceed. This never refunds native
          // cleanup history or grants ordinary automatic UI-refresh authority.
          if (window.__ymccDeckyManualRepair === repairReservation) delete window.__ymccDeckyManualRepair;
          return;
        }
        // Keep the gate spent even when the call throws: a restart may already
        // have reached Steam. A late/duplicate callback cannot invoke it twice.
        restartEntered = true;
        traceRepair('restart-call-entered');
        SteamClient.Browser.RestartJSContext();
      }, 0);
      traceRepair('scheduled');
      mutated = true;
      return { ok: true, requestDispatched: true, mutated: true, stage, phase: 'requested' };
    }
    // UI mode is process-wide, available before account login. Do not couple it
    // to CSSLoader, controller settings or whichever window currently has focus.
    if (request.operation === 'ui.mode.get') {
      stage = 'ui-mode-read';
      if (typeof globalThis.SteamClient?.UI?.GetUIMode !== 'function') fail('live-api-unavailable');
      // An O(1) mode callback remembers quick leave/re-enter transitions between
      // native window-event reads. No timers, DOM changes or CSS/Steam launches.
      const classify = value => value === 4 || value === 1 ? 'bigpicture' : value === 7 || value === 0 ? 'desktop' : 'unknown';
      let watch = window.__ymccSteamUiModeWatch;
      if (!watch) {
        watch = { version: 1, id: Date.now().toString(36) + '-' + Math.random().toString(36).slice(2), entries: 0, mode: 'unknown', revision: 0, lastRaw: null, active: false };
        window.__ymccSteamUiModeWatch = watch;
        watch.observe = value => { const next = classify(value); if (next === 'unknown') return;
          if (next === 'bigpicture' && watch.mode !== next) watch.entries++;
          watch.mode = next; watch.lastRaw = value; watch.revision++;
        };
        try {
          if (typeof SteamClient.UI.RegisterForUIModeChanged === 'function') {
            watch.subscription = SteamClient.UI.RegisterForUIModeChanged(watch.observe); watch.active = true;
          }
        } catch {} // Older Steam retains the verified current-mode fallback.
      }
      const beforeRead = watch.revision;
      let raw = await bounded(SteamClient.UI.GetUIMode());
      if (watch.revision === beforeRead) watch.observe(raw);
      else raw = watch.lastRaw; // A real event during the async read wins over its older reply.
      // Valve's current EUIMode: VGUI=0, Tenfoot=1, Gamepad=4, Desktop=7.
      const mode = raw === 4 || raw === 1 ? 'bigpicture' : raw === 7 || raw === 0 ? 'desktop' : 'unknown';
      // Narrow update residue only: no credentials, endpoint, settings or new subscription.
      // A prior active YMCC module differing from the installed module proves an update
      // remains unconsumed in this existing Steam context. An arbitrary Steam page does not.
      let deckyFrontendLoaded;
      try {
        if (window === window.top && location.origin === 'https://steamloopback.host')
          deckyFrontendLoaded = !!window.deckyHasLoaded || !!window.DeckyPluginLoader;
      } catch {} // Unknown is never permission to risk restarting Steam's UI.
      let deckyUpdateResidue, deckyEmptyFrontend;
      try {
        const prior=window.__YMCC_DECKY_FRONTEND__,bridge=window.__YMCC_DECKY_MIRROR__;
        // Revoked bridge keeps only build/run provenance, never credentials.
        if(window===window.top&&location.origin==='https://steamloopback.host'&&window.DeckyPluginLoader&&prior?.active===true&&/^[a-f0-9]{64}$/.test(prior.buildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&((bridge?.revoked===true&&Object.keys(bridge).length===3&&bridge.frontendBuildId===prior.buildId)||(/^[a-f0-9]{64}$/.test(bridge?.token||'')&&/^ws:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}\/mirror$/.test(bridge?.endpoint||''))))
          deckyUpdateResidue={protocol:1,active:true,bridgePresent:true,frontendBuildId:prior.buildId};
        const emptyLoader=window.DeckyPluginLoader, emptyVersion=emptyLoader?.deckyState?.publicState?.()?.versionInfo?.current;
        if(window===window.top&&location.origin==='https://steamloopback.host'&&prior==null&&Array.isArray(emptyLoader?.plugins)&&emptyLoader.plugins.length===0&&/^[a-f0-9]{64}$/.test(bridge?.frontendBuildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&/^[a-f0-9]{64}$/.test(bridge?.token||'')&&new RegExp('^ws://127[.]0[.]0[.]1:[1-9][0-9]{0,4}/mirror$').test(bridge?.endpoint||'')&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(emptyVersion||''))
          deckyEmptyFrontend={protocol:1,frontendBuil)YMSTEAM"
R"YMSTEAM(dId:bridge.frontendBuildId,loaderVersion:emptyVersion};
        if(deckyUpdateResidue){try{const loaded=window.DeckyPluginLoader?.deckyState?.publicState?.()?.versionInfo?.current;if(typeof loaded==='string'&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(loaded)&&loaded.length<=32)deckyUpdateResidue.loaderVersion=loaded;}catch{}}
      } catch {} // Recovery evidence is optional; a read failure never grants launch.
      // Reuse sleep.session.inspect's current UI identity read. Account discovery
      // stays read-only and account-independent for mode/CSS; Decky alone may
      // consume this proof when ActiveUser is zero in a logged-in offline session.
      let observedAccount;
      try {
        const id=window.settingsStore?.m_CMInterface?.steamid;
        if(typeof id?.GetAccountID==='function'){
          const account=id.GetAccountID();
          if(Number.isInteger(account)&&account>0&&account<=0xffffffff&&
             window.settingsStore?.m_CMInterface?.steamid===id&&id.GetAccountID()===account)observedAccount=account;
        }
      } catch {} // Missing/racing identity never prevents a process-wide mode read.
      // One optional native registration attempt per document, shared by all
      // rebinds. A missing/throwing hook or delayed initial callback must not
      // permanently block a valid current UI getter after offline/sleep startup.
      // Upgrade contract: read/refresh/subscribe is the stable document-owned
      // interface with readbackPolicy=2. Older complete objects can retain a
      // logout veto forever when the same account returns without a callback;
      // migrate them as notifications-only, without a second native hook set. An older object without this contract supplies
      // notifications only: its cached-account predicate must not veto live UI.
      const hadAccountWatch=Object.prototype.hasOwnProperty.call(window,'__YMCC_DECKY_ACCOUNT_WATCH__');
      let accountWatch=window.__YMCC_DECKY_ACCOUNT_WATCH__;
      if(!accountWatch||accountWatch.version!==1||accountWatch.readbackPolicy!==2||typeof accountWatch.read!=='function'||
         typeof accountWatch.refresh!=='function'||typeof accountWatch.subscribe!=='function'){
        const legacy=accountWatch;
        const listeners=new Set();let loggedOut=false,lastUiAccount=observedAccount||0;
        const read=expected=>{
          if(loggedOut||!Number.isInteger(expected)||expected<=0||expected>0xffffffff)return false;
          try {const id=window.settingsStore?.m_CMInterface?.steamid;
            return typeof id?.GetAccountID==='function'&&id.GetAccountID()===expected&&
              window.settingsStore?.m_CMInterface?.steamid===id&&id.GetAccountID()===expected;
          }catch{return false;}
        };
        const changed=()=>{for(const fn of Array.from(listeners).slice(0,4)){try{fn();}catch{}}};
        // Borrow the existing mode read to catch lost account callbacks. No
        // extra timer/read or native subscription, and no healthy-state events.
        const refresh=account=>{
          if(!Number.isInteger(account)||account<0||account>0xffffffff)return;
          // Logout revokes the old run immediately. A later actual mode read
          // may confirm the SAME current account after a missed login callback;
          // an empty UI still cannot reopen it. Healthy reads stay silent.
          const returned=loggedOut&&account>0;
          if(account===lastUiAccount&&!returned)return;
          lastUiAccount=account;if(account>0)loggedOut=false;changed();
        };
        accountWatch={version:1,readbackPolicy:2,read,refresh,subscribe(fn){listeners.add(fn);return ()=>listeners.delete(fn);}};
        Object.defineProperty(window,'__YMCC_DECKY_ACCOUNT_WATCH__',{value:accountWatch,configurable:true});
        if(hadAccountWatch){
          // One forwarding listener for the lifetime of this Steam document.
          // A broken/opaque legacy observer falls back to existing UI readback;
          // do not call its read() or guess whether native hooks were installed.
          try{legacy?.subscribe?.(changed);}catch{}
        }else{
        try{
          SteamClient.User?.RegisterForCurrentUserChanges?.(user=>{
            let next=0;
            try {const id=user?.strSteamID;
              if(typeof id==='string'&&/^[1-9][0-9]{0,19}$/.test(id)){
                const full=BigInt(id);if(full<=0xffffffffffffffffn&&((full>>52n)&15n)===1n)next=Number(full&0xffffffffn);
              }
            }catch{}
            // A callback can lag the actual UI after resume/account changes.
            // It wakes validation; its cached account is not a second veto.
            if(next)loggedOut=false;changed();
          });
        }catch{} // No retries/subscription buildup; read still validates the UI.
        try{
          SteamClient.User?.RegisterForLoginStateChange?.(name=>{
            loggedOut=typeof name!=='string'||!name;
            changed();
          });
        }catch{} // Independent hooks: a failed first registration cannot skip this one.
        }
      }
      accountWatch?.refresh?.(observedAccount||0);
      const deckyAccountWatchAvailable=accountWatch?.version===1&&!!observedAccount&&accountWatch.read(observedAccount);
      // Reviewed official v3.3.1 fields. Unknown versions remain UNKNOWN;
      // installed hooks are only a structural check, never visual acceptance.
      let deckyBackendConnected,deckyBackendState,deckyHooksInstalled,deckyClientConnected,deckyClientVerified,deckyClientChecking,deckyClientState,deckyQamPanelState;
      try {
        if(window===window.top&&window.location.origin==='https://steamloopback.host'&&
           window.DeckyPluginLoader?.deckyState?.publicState?.()?.versionInfo?.current==='v3.3.1'){
          const ws=window.DeckyBackend?.ws,readyState=ws?.readyState;
          if(ws&&Number.isInteger(readyState))deckyBackendConnected=readyState===1;
          // Reuse this read only: a closed official socket must not masquerade
          // as a missing YMCC peer, nor trigger an extra probe or UI restart.
          deckyBackendState=!ws?'socket-missing':readyState===0?'socket-connecting':readyState===1?'socket-open':
            readyState===2?'socket-closing':readyState===3?'socket-closed':'socket-unavailable';
          const hooks=window.__TABS_HOOK_INSTANCE,frontend=window.__YMCC_DECKY_FRONTEND__;
          const active=patch=>!!patch&&patch.hasUnpatched===false;
          deckyHooksInstalled=!!(hooks&&hooks===window.DeckyPluginLoader.tabsHook&&active(hooks.qamBrowserViewPatch)&&
            (!hooks.qamEmbeddedPatch||active(hooks.qamEmbeddedPatch))&&frontend?.qamReady!==false&&
            Array.isArray(hooks.tabs)&&hooks.tabs.some(tab=>tab?.id===999));
          deckyClientConnected=frontend?.active===true&&frontend.connectionReady===true;
          deckyClientChecking=frontend?.active===true&&frontend.connectionChecking===true;
          // Optional native reply proof is a memory getter, not another request.
          try{const verified=frontend?.active===true&&frontend.connectionVerified;if(typeof verified==='boolean')deckyClientVerified=verified;}catch{}
          // Optional local explanation only; the original booleans remain
          // authoritative. A diagnostic getter failure cannot discard them.
          try{
            if(!frontend)deckyClientState='frontend-absent';
            else if(frontend.active!==true)deckyClientState='frontend-inactive';
            else{const state=frontend.connectionState;if(['disposed','native-run-missing','binding-run-missing','run-mismatch','socket-missing','socket-connecting','socket-closing','socket-closed','socket-unavailable','transport-unconfirmed','opening-reply-missing','connected','module-dismounted','frontend-absent','frontend-inactive','read-failed'].includes(state))deckyClientState=state;}
          }catch{deckyClientState='read-failed';}
          // Optional commit evidence. Missing/hidden and old frontends cannot
          // prove a failed mount, and this value never changes hook health.
          try{
            const state=mode==='bigpicture'&&frontend?.active===true?frontend.qamPanelState:undefined;
            if(s)YMSTEAM"
R"YMSTEAM(tate==='unobserved'||state==='committed'||state==='native-ready-commit')deckyQamPanelState=state;
          }catch{}
        }
      }catch{} // Never emit credentials, router URL, source objects or exception text.
      return { ok: true, ...(typeof deckyBackendConnected==='boolean'?{deckyBackendConnected}:{}), ...(typeof deckyBackendState==='string'?{deckyBackendState}:{}), ...(typeof deckyHooksInstalled==='boolean'?{deckyHooksInstalled}:{}), ...(typeof deckyClientConnected==='boolean'?{deckyClientConnected}:{}), ...(typeof deckyClientChecking==='boolean'?{deckyClientChecking}:{}), ...(typeof deckyClientVerified==='boolean'?{deckyClientVerified}:{}), ...(typeof deckyClientState==='string'?{deckyClientState}:{}), ...(deckyQamPanelState?{deckyQamPanelState}:{}), ...(observedAccount?{observedAccount}:{}), deckyAccountWatchAvailable, mode, rawMode: Number.isInteger(raw) ? raw : null, modeEpoch: watch.active ? watch.id + ':' + watch.entries : '', ...(deckyUpdateResidue?{deckyUpdateResidue}:{}), ...(deckyEmptyFrontend?{deckyEmptyFrontend}:{}), ...(typeof deckyFrontendLoaded === 'boolean'?{deckyFrontendLoaded}:{}), mutated: false, via: 'live' };
    }
    // Steam sleep captures bind the account only through this fixed read.
    // Native code pins the verified Steam PID/birth/root and never uses this to
    // adopt a different account after a sleep capture or to mutate settings.
    if (request.operation === 'sleep.session.inspect') {
      stage = 'sleep-session-read';
      const id = window.settingsStore?.m_CMInterface?.steamid;
      if (!id || typeof id.GetAccountID !== 'function') fail('live-api-unavailable');
      const account = id.GetAccountID();
      if (!Number.isInteger(account) || account <= 0 || account > 0xffffffff) fail('steam-session-starting');
      if (window.settingsStore?.m_CMInterface?.steamid !== id || id.GetAccountID() !== account) fail('steam-session-changed');
      return { ok:true, mutated:false, stage, observedAccount:account, source:'verified-steam-ui-account-read',...privateNativeRecordReply() };
    }
    checkAccount();
    if (request.operation === 'input.gamepadLease') {
      stage = 'input-gamepad-lease';
      const dataField = (object, key) => {
        const descriptor = object && Object.getOwnPropertyDescriptor(object, key);
        return descriptor && 'value' in descriptor ? descriptor.value : undefined;
      };
      if (typeof request.owner !== 'string' || !/^[a-f0-9]{32}$/.test(request.owner) ||
          !Number.isSafeInteger(request.serial) || request.serial <= 0 || typeof request.enabled !== 'boolean' ||
          !Number.isSafeInteger(request.expiresAt)) fail('invalid-gamepad-lease');
      const monotonicStart = performance.now(), remaining = request.expiresAt - Date.now();
      if (!Number.isFinite(monotonicStart) || remaining <= 0 || remaining > 3000) fail('gamepad-lease-expired-or-too-long');
      const previous = dataField(window, '__ymccLightGamepadLease');
      let carried = null;
      if (previous && (typeof previous.dispose !== 'function' || typeof previous.remainingMs !== 'function' || typeof previous.receipt !== 'function'))
        fail('gamepad-lease-state-unavailable');
      if (previous) {
        const live = previous.remainingMs() > 0;
        if (previous.owner !== request.owner) {
          if (!request.enabled || live) fail('gamepad-lease-owner-mismatch');
        } else {
          if (request.serial < previous.serial) fail('gamepad-lease-stale-serial');
          if (request.serial === previous.serial) {
            if (request.enabled !== previous.requestedEnabled ||
                (request.enabled && (request.expiresAt !== previous.expiresAt || !live))) fail('gamepad-lease-stale-serial');
            const receipt = previous.receipt();
            if (request.enabled && !receipt.enabled) {
              mutated = true;
              if (!previous.dispose()) fail('gamepad-lease-cleanup-failed');
              fail('gamepad-lease-expired');
            }
            if (!live && !previous.dispose()) { mutated = true; fail('gamepad-lease-cleanup-failed'); }
            return { ok: true, mutated, stage, ...previous.receipt() };
          }
          if (live) {
            if (!(previous.priorUp instanceof Set) || !(previous.blockedDown instanceof Set)) {
              mutated = true;
              previous.dispose();
              fail('gamepad-lease-state-unavailable');
            }
            carried = { priorUp: new Set(previous.priorUp), blockedDown: new Set(previous.blockedDown) };
          }
        }
        mutated = Boolean(previous.enabled || previous.releasing || previous.handle || previous.timer !== null || previous.listening);
        if (!previous.dispose()) fail('gamepad-lease-cleanup-failed');
      }
      const document = window.document, ui = dataField(window, 'SteamUIStore');
      const manager = dataField(ui, 'm_GamepadNavigationManager');
      const callbacks = dataField(manager, 'm_rgCatchAllGamepadInput');
      const settings = window.settingsStore, cm = settings?.m_CMInterface, identity = cm?.steamid, account = request.account;
      const lease = { owner: request.owner, serial: request.serial, expiresAt: request.expiresAt,
        requestedEnabled: request.enabled, enabled: false, releasing: false, deadline: monotonicStart + remaining,
        document, pageHidden: previous?.document === document && previous.pageHidden === true,
        priorUp: carried?.priorUp || new Set(), blockedDown: carried?.blockedDown || new Set(),
        timer: null, handle: null, listening: false, wrapper: null, dispatch: null };
      let setter, originalAction, actionDescriptor, actionOwner, inputSources, gamepadType;
      const sourceBindings = [];
      const dispatchKey = (source, controller, button) => `${source}:${controller}:${button}`;
      lease.remainingMs = () => {
        try {
          if (!(lease.enabled || lease.releasing) || lease.pageHidden || window.__ymccLightGamepadLease !== lease ||
              window.closed || !document || window.document !== document || document.defaultView !== window ||
              window.settingsStore !== settings || settings.m_CMInterface !== cm || cm.steamid !== identity ||
              identity.GetAccountID() !== account || dataField(window, 'SteamUIStore') !== ui ||
              dataField(ui, 'm_GamepadNavigationManager') !== manager || manager.SetCatchAllGamepadInput !== setter ||
              dataField(manager, 'm_rgCatchAllGamepadInput') !== callbacks || !callbacks.includes(lease.callback) ||
              manager.OnButtonActionInternal !== lease.wrapper ||
              dataField(manager, 'm_rgGamepadInputSources') !== inputSources || inputSources.length !== sourceBindings.length ||
              sourceBindings.some((binding, index) => inputSources[index] !== binding.source ||
                binding.source.GetSourceType !== binding.getType || binding.getType.call(binding.source) !== binding.type ||
                dataField(binding.source, 'm_rgControllers') !== binding.controllers)) return 0;
          const now = performance.now(), wallRemaining = lease.expiresAt - Date.now();
          return Number.isFinite(now) && Number.isFinite(wallRemaining) ? Math.max(0, Math.min(lease.deadline - now, wallRemaining)) : 0;
        } catch { return 0; }
      };
      lease.receipt = () => {
        const remainingMs = lease.remainingMs();
        return { enabled: remainingMs > 0 && lease.enabled, releasing: remainingMs > 0 && lease.releasing,
          pendingReleaseCount: remainingMs > 0 ? lease.blockedDown.size : 0, serial: lease.serial, remainingMs, scope: 'steam-gamepad-navigation' };
      };
      lease.dispose = () => {
        lease.enabled = false;
        lease.releasing = false;
        let clean = true;
        if (lease.timer !== null) {
          try { clearTimeout(lease.timer); lease.timer = null; } catch { clean = false; }
        }
        if (lease.listening) {
          try { window.removeEventListener('pagehide', lease.pagehide); lease.listening = false; } catch { clean = false; }
        }
        if (lease.handle) {
          try { lease.handle.Unregister(); lease.handle = nu)YMSTEAM"
R"YMSTEAM(ll; } catch { clean = false; }
        }
        if (Array.isArray(callbacks)) {
          try {
            let index;
            while ((index = callbacks.indexOf(lease.callback)) >= 0) callbacks.splice(index, 1);
          } catch { clean = false; }
        }
        try {
          const currentAction = manager && Object.getOwnPropertyDescriptor(manager, 'OnButtonActionInternal');
          if (lease.wrapper && currentAction && 'value' in currentAction && currentAction.value === lease.wrapper &&
              currentAction.writable === true && currentAction.configurable === true && currentAction.enumerable === actionDescriptor.enumerable) {
            if (actionOwner === manager && actionDescriptor) Object.defineProperty(manager, 'OnButtonActionInternal', actionDescriptor);
            else if (!delete manager.OnButtonActionInternal) clean = false;
          }
        } catch { clean = false; }
        return clean;
      };
      lease.callback = (button, down, repeat) => {
        if (lease.remainingMs() <= 0) { lease.dispose(); return false; }
        const dispatch = lease.dispatch;
        return Boolean(dispatch && dispatch.button === button && dispatch.down === down && dispatch.repeat === repeat && dispatch.block);
      };
      lease.pagehide = () => { lease.pageHidden = true; lease.dispose(); };
      window.__ymccLightGamepadLease = lease;
      mutated = true;
      if (request.enabled || lease.blockedDown.size > 0) {
        try {
          setter = manager?.SetCatchAllGamepadInput;
          for (let cursor = manager; cursor; cursor = Object.getPrototypeOf(cursor)) {
            const descriptor = Object.getOwnPropertyDescriptor(cursor, 'OnButtonActionInternal');
            if (descriptor) { actionDescriptor = descriptor; actionOwner = cursor; break; }
          }
          originalAction = manager?.OnButtonActionInternal;
          inputSources = dataField(manager, 'm_rgGamepadInputSources');
          if (!document || document.defaultView !== window || lease.pageHidden || window.closed ||
              typeof window.addEventListener !== 'function' || typeof window.removeEventListener !== 'function' ||
              !Array.isArray(callbacks) || typeof setter !== 'function' || typeof originalAction !== 'function' ||
              originalAction.length !== 10 || !actionDescriptor || !('value' in actionDescriptor) || actionDescriptor.writable !== true ||
              (actionOwner === manager && actionDescriptor.configurable !== true) || !Array.isArray(inputSources) || inputSources.length === 0)
            fail('gamepad-lease-api-unavailable');
          const controllersSeen = new Set();
          for (const source of inputSources) {
            const getType = source?.GetSourceType, controllers = dataField(source, 'm_rgControllers');
            if (typeof getType !== 'function') fail('gamepad-lease-source-unavailable');
            const type = getType.call(source);
            if (!Number.isSafeInteger(type) || type < 0) fail('gamepad-lease-source-unavailable');
            sourceBindings.push({ source, getType, type, controllers });
            if (controllers !== undefined) {
              let entries;
              try { entries = [...Map.prototype.entries.call(controllers)]; } catch { fail('gamepad-lease-source-unavailable'); }
              if (gamepadType !== undefined && gamepadType !== type) fail('gamepad-lease-source-unavailable');
              gamepadType = type;
              for (const [controller, record] of entries) {
                if (!Number.isSafeInteger(controller) || controller < -1 || controllersSeen.has(controller)) fail('gamepad-lease-source-unavailable');
                controllersSeen.add(controller);
                const activeButtons = dataField(record, 'activeButtons');
                if (!activeButtons || typeof activeButtons !== 'object') fail('gamepad-lease-source-unavailable');
                for (const button of Reflect.ownKeys(activeButtons)) {
                  const held = dataField(activeButtons, button), numericButton = Number(button);
                  if (typeof button !== 'string' || !/^[1-9][0-9]*$/.test(button) || !Number.isSafeInteger(numericButton) ||
                      typeof held !== 'boolean') fail('gamepad-lease-source-unavailable');
                  if (!carried && held) lease.priorUp.add(dispatchKey(type, controller, numericButton));
                }
              }
            }
          }
          if (gamepadType === undefined || sourceBindings.some(binding => binding.type === gamepadType && binding.controllers === undefined))
            fail('gamepad-lease-source-unavailable');
          lease.wrapper = function(...dispatchArgs) {
            if (this !== manager) return originalAction.apply(this, dispatchArgs);
            if (lease.remainingMs() <= 0) { lease.dispose(); return originalAction.apply(this, dispatchArgs); }
            const [down, button, source, controller, repeatValue] = dispatchArgs, repeat = repeatValue ?? false;
            let block = false;
            if (source === gamepadType) {
              if (typeof down !== 'boolean' || !Number.isSafeInteger(button) || button <= 0 ||
                  !Number.isSafeInteger(controller) || controller < -1 || typeof repeat !== 'boolean') {
                lease.dispose(); return originalAction.apply(this, dispatchArgs);
              }
              const key = dispatchKey(source, controller, button), prior = lease.priorUp.has(key), blocked = lease.blockedDown.has(key);
              if (lease.enabled) {
                block = true;
                if (down) {
                  if (!prior) lease.blockedDown.add(key);
                } else if (prior) {
                  block = false;
                  lease.priorUp.delete(key);
                } else if (blocked) {
                  block = true;
                  lease.blockedDown.delete(key);
                }
              } else if (lease.releasing) {
                if (down) {
                  block = blocked && repeat;
                  if (!block) {
                    if (!repeat) lease.blockedDown.delete(key);
                    lease.priorUp.add(key);
                  }
                } else {
                  if (blocked) {
                    block = true;
                    lease.blockedDown.delete(key);
                  }
                  lease.priorUp.delete(key);
                }
              }
            }
            const parentDispatch = lease.dispatch;
            lease.dispatch = { down, button, repeat, block };
            try { return originalAction.apply(this, dispatchArgs); }
            finally {
              lease.dispatch = parentDispatch;
              if (lease.releasing && lease.blockedDown.size === 0) lease.dispose();
            }
          };
          const wrapperDescriptor = { ...actionDescriptor, value: lease.wrapper, writable: true, configurable: true };
          Object.defineProperty(manager, 'OnButtonActionInternal', wrapperDescriptor);
          lease.handle = setter.call(manager, lease.callback);
          if (typeof lease.handle?.Unregister !== 'function' || callbacks.filter(callback => callback === lease.callback).length !== 1)
            fail('gamepad-lease-registration-failed');
          lease.listening = true;
          window.addEventListener('pagehide', lease.pagehide);
          lease.enabled = request.enabled;
          lease.releasing = !request.enabled;
          const delay = lease.remainingMs();
          if (delay <= 0 || request.expiresAt <= Date.now()) fail('gamepad-lease-expired');
          lease.timer = setTimeout(() => { lease.timer = null; lease.dispose(); }, delay);
          if (!Number.isSafeInteger(lease.timer) || lease.timer < 0) fail('gamepad-lease-timer-failed');
          if (lease.remainingMs() <= 0) fail('gamepad-lease-expired');
        } catch (error) {
          if (!lease.dispose()) fail('gamepad-lease-cleanup-failed');
          throw error;
        }
      }
      const receipt = lease.receipt();
      if ((request.enabled && !receipt.enabled) || (!request.enabled && lease.blockedDown.size > 0 && !receipt.releasing)) {
        if (!lease.)YMSTEAM"
R"YMSTEAM(dispose()) fail('gamepad-lease-cleanup-failed');
        fail('gamepad-lease-expired');
      }
      return { ok: true, mutated, stage, ...receipt };
    }
    if (request.operation === 'diagnostics.apps.api.read') {
      stage = 'apps-api-surface-read';
      return readFixedApiDescriptors(false);
    }
    if (request.operation === 'diagnostics.apps.contract.read') {
      stage = 'apps-api-contract-read';
      return readFixedApiDescriptors(true);
    }
    // One fixed diagnostic catalog; no caller names/paths/code or PID payload.
    // Reflection may execute Proxy traps. ownPropertyNames allocates its complete
    // returned list; only our visits/output below are bounded, not that intrinsic.
    function readFixedApiDescriptors(contractOnly) {
      const hasCursor = Object.prototype.hasOwnProperty.call(request, 'cursor');
      if (Object.keys(request).length !== (hasCursor ? 3 : 2) || !Object.prototype.hasOwnProperty.call(request, 'account') ||
          !Object.prototype.hasOwnProperty.call(request, 'operation') || (contractOnly && hasCursor)) fail('invalid-apps-api-diagnostic-request');
      const cursorStart = hasCursor ? request.cursor : 0;
      if (!Number.isInteger(cursorStart) || cursorStart < 0 || cursorStart > 255) fail('invalid-apps-api-diagnostic-cursor');
      const maxVisits = 256, maxRows = 64, maxDepth = 2, maxNamespaceVisits = 64, maxNamespaceRows = 16;
      let visits = 0, rowCount = 0, skippedPositions = 0;
      const fixed = [
        ['Apps', ['LaunchNonSteamApp', 'RunGame', 'RaiseWindowForGame', 'RegisterForAppOverviewChanges']],
        ['User', []], ['GameSessions', ['RegisterForAppLifetimeNotifications']], ['ProcessMonitor', []],
      ];
      const objectLike = value => value !== null && (typeof value === 'object' || typeof value === 'function');
      const descriptor = (object, name) => {
        let cursor = object;
        for (let depth = 0; objectLike(cursor) && depth < maxDepth; depth++) {
          let item;
          try { item = Object.getOwnPropertyDescriptor(cursor, name); }
          catch { return { status: 'uninspectable', reason: 'descriptor-threw', depth }; }
          if (item) return Object.prototype.hasOwnProperty.call(item, 'value')
            ? { status: 'data', descriptor: item, depth }
            : { status: 'uninspectable', reason: 'accessor-not-invoked', depth };
          if (depth + 1 < maxDepth) {
            try { cursor = Object.getPrototypeOf(cursor); }
            catch { return { status: 'uninspectable', reason: 'prototype-threw', depth }; }
          }
        }
        return { status: 'uninspectable', reason: 'not-found-within-depth' };
      };
      const root = descriptor(globalThis, 'SteamClient');
      const rootObject = root.status === 'data' && objectLike(root.descriptor.value) ? root.descriptor.value : null;
      const rootReason = root.status !== 'data' ? root.reason : rootObject ? '' : 'namespace-not-object';
      const describe = (label, name, item, depth) => {
        const accessor = !Object.prototype.hasOwnProperty.call(item, 'value');
        const kind = accessor ? 'accessor' : typeof item.value;
        let arity = null, arityReason = 'not-function';
        if (kind === 'function') {
          try {
            const length = Object.getOwnPropertyDescriptor(item.value, 'length');
            if (!length) arityReason = 'length-descriptor-missing';
            else if (!Object.prototype.hasOwnProperty.call(length, 'value')) arityReason = 'length-accessor-not-invoked';
            else if (!Number.isInteger(length.value) || length.value < 0 || length.value > 65535) arityReason = 'length-value-unconfirmed';
            else { arity = length.value; arityReason = 'diagnostic-only'; }
          } catch { arityReason = 'length-descriptor-threw'; }
        }
        return { label, name, present: true, depth, descriptorKind: kind,
          enumerable: item.enumerable === true, configurable: item.configurable === true,
          writable: !accessor && item.writable === true, arity, arityReason,
          nativeSignature: 'unknown', getterInvokedByReader: false };
      };
      const surfaces = [], rows = [];
      for (const [namespace, literalNames] of fixed) {
        const label = 'SteamClient.' + namespace;
        const namespaceField = rootObject ? descriptor(rootObject, namespace) : { status: 'uninspectable', reason: rootReason };
        const object = namespaceField.status === 'data' && objectLike(namespaceField.descriptor.value) ? namespaceField.descriptor.value : null;
        const summary = { label, status: object ? 'inspected' : 'uninspectable',
          reason: namespaceField.status !== 'data' ? namespaceField.reason : object ? '' : 'namespace-not-object',
          rows: [], namesVisited: 0, skippedNamePositions: 0, nextCursor: object && !contractOnly ? cursorStart : null,
          sequenceLimitReached: false, hasMore: object && !contractOnly ? false : null, truncated: false };
        let reflectionUnknown = false;
        const budgetFull = () => visits >= maxVisits || rowCount >= maxRows || summary.namesVisited >= maxNamespaceVisits || summary.rows.length >= maxNamespaceRows || (!contractOnly && cursorStart + summary.namesVisited >= 256);
        const budgetReason = () => !contractOnly && cursorStart + summary.namesVisited >= 256 ? 'sequence-limit' : visits >= maxVisits || rowCount >= maxRows ? 'catalog-budget' : 'namespace-budget';
        if (object && contractOnly) {
          if (literalNames.length === 0) { summary.status = 'uninspectable'; summary.reason = 'no-fixed-contract-methods'; }
          for (const name of literalNames) {
            if (budgetFull()) { summary.truncated = true; summary.hasMore = true; summary.status = 'partial'; summary.reason = budgetReason(); break; }
            visits++; summary.namesVisited++;
            const found = descriptor(object, name);
            const row = found.status === 'data' ? describe(label, name, found.descriptor, found.depth)
              : { label, name, present: null, descriptorKind: 'unknown', nativeSignature: 'unknown', reason: found.reason, getterInvokedByReader: false };
            rowCount++; summary.rows.push(row); rows.push(row);
          }
        } else if (object) {
          const seen = new Set(); let cursor = object, skip = cursorStart;
          for (let depth = 0; objectLike(cursor) && depth < maxDepth; depth++) {
            if (budgetFull()) { summary.truncated = true; summary.hasMore = true; summary.status = 'partial'; summary.reason = budgetReason(); break; }
            let names;
            try { names = Object.getOwnPropertyNames(cursor); }
            catch { summary.status = 'uninspectable'; summary.reason = 'own-names-threw'; break; }
            const skipped = Math.min(skip, names.length);
            skip -= skipped; summary.skippedNamePositions += skipped; skippedPositions += skipped;
            let index = skipped;
            for (; index < names.length && !budgetFull(); index++) {
              const name = names[index]; visits++; summary.namesVisited++;
              if (seen.has(name)) continue; seen.add(name);
              // Names skipped on earlier pages still shadow a prototype method.
              // Do not access their values or replay earlier-page traversal.
              if (depth > 0) {
                try { if (Object.getOwnPropertyDescriptor(object, name)) continue; }
                catch { reflectionUnknown = true; summary.status = 'partial'; summary.reason = 'shadow-descriptor-threw'; continue; }
              }
              if (!/^[A-Za-z_$][A-Za-z0-9_$]{0,95}$/.test(name) || !/attach|register|existing|process|app|game|running|launch|notify/i.test(name)) continue;
              let item;
              try { item = Object.getOwnPropertyDescriptor(cursor, name); }
              catch { reflectionUnknown = true; summary.status = 'partial'; summary.reason = 'descriptor-threw'; continue; }
              if (!item) { reflectionUnknown = true; summary.status = 'partial'; summary.reason = 'descriptor-disappeared'; continue; }
              const row = describe(label, name, item, depth); rowCount++; summary.rows.pus)YMSTEAM"
R"YMSTEAM(h(row); rows.push(row);
            }
            if (index < names.length) { summary.truncated = true; summary.hasMore = true; summary.status = 'partial'; summary.reason = budgetReason(); break; }
            if (depth + 1 < maxDepth) {
              try { cursor = Object.getPrototypeOf(cursor); }
              catch { summary.status = 'uninspectable'; summary.reason = 'prototype-threw'; break; }
            }
          }
        }
        if (object && !contractOnly) {
          // namesVisited counts every consumed sequence position, including
          // filtered/shadowed names, so row limits never create continuation gaps.
          const continuationUnknown = reflectionUnknown || summary.status === 'uninspectable' ||
            (summary.status === 'partial' && !['namespace-budget','catalog-budget','sequence-limit'].includes(summary.reason));
          if (continuationUnknown) { summary.nextCursor = null; summary.hasMore = null; summary.sequenceLimitReached = false; }
          else { summary.nextCursor = cursorStart + summary.namesVisited; summary.sequenceLimitReached = summary.nextCursor >= 256; }
        }
        surfaces.push(summary);
      }
      return { ok: true, mutated: false, stage, schema: 'ymcc.steam-fixed-api-descriptors.v1',
        surfaces, ...(contractOnly ? { rows } : {}), rootStatus: rootObject ? 'data-object' : 'uninspectable',
        rootReason, namesVisited: visits, matchedNames: rowCount, maxNamesVisited: maxVisits, maxMatchedNames: maxRows, maxPrototypeLevels: maxDepth,
        namespaceCount: fixed.length, cursor: cursorStart, maxCursor: 255, sequencePositionLimit: 256, cursorApplied: !contractOnly,
        skippedNamePositions: skippedPositions, skipCostProvenZero: false, crossPageAtomic: false,
        hasMore: surfaces.some(value => value.hasMore === true) ? true : surfaces.some(value => value.hasMore === null) ? null : false,
        hasMoreScope: 'current-two-level-sequence-or-budget-not-complete-api',
        continuationScope: 'per-namespace-nextCursor-is-exact-consumed-end-not-an-atomic-snapshot',
        maxNamespaceNamesVisited: maxNamespaceVisits, maxNamespaceMatchedNames: maxNamespaceRows,
        truncationScope: 'budget-only-not-complete-api-inventory', catalogComplete: false, truncated: surfaces.some(value => value.truncated),
        apiValuesNotRead: true, methodsNotCalled: true, discoveredMethodBodiesNotCalled: true,
        namespaceAndLengthGettersNotInvokedByReader: true, proxyTrapEffectsUnknown: true, ownNamesAllocationBounded: false,
        nativeSignaturesKnown: false, registrationAuthority: false,
        accountValidation: 'existing-GetAccountID-read-before-diagnostic', wholeRequestFunctionCallsZero: false,
        coordinationMetadata: 'existing-context-busy-marker-retained' };
    }
    if (request.operation === 'sleep.inspect' || request.operation === 'sleep.reconcile' || request.operation === 'sleep.input-resume') {
      const ids=request.appids??[];
      // IPC inputs stay unsigned32; Steam's own stores may use signed32 aliases.
      if(!Array.isArray(ids)||ids.length>16||ids.some(id=>!Number.isInteger(id)||id<=0||id>0xffffffff))fail('invalid-steam-appids');
      stage='sleep-inspect';
      // These four m_ fields are observed data properties, not the aggregate
      // window getters that can construct a simulated VR window.
      const dataField=(object,key)=>{try{const d=object&&Object.getOwnPropertyDescriptor(object,key);return d&&'value' in d?d.value:null}catch{return null}};
      const ui=window.SteamUIStore,store=window.appStore,ws=request.operation==='sleep.inspect'?dataField(ui,'m_WindowStore'):ui?.WindowStore;
      if(!ui?.m_runningAppIDs||!store?.m_mapApps||typeof store.m_mapApps.get!=='function')fail('live-api-unavailable');
      const canonical=id=>typeof id==='number'&&Number.isInteger(id)&&id>=-0x80000000&&id<=0xffffffff&&id!==0?id>>>0:null;
      const num=v=>typeof v==='number'&&Number.isFinite(v)?v:null;
      const text=v=>typeof v==='string'?v:typeof v==='number'&&Number.isSafeInteger(v)?String(v):null;
      const value=(o,k)=>{try{return o?.[k]??null}catch{return null}};
      const lookup=appid=>{
        const keys=[appid,appid|0];let found=null;
        for(const key of new Set(keys)) {
          const overview=store.m_mapApps.get(key);
          if(!overview)continue;
          // A keyed overview may be replaced concurrently; never borrow another app's state.
          if(overview.appid!=null&&canonical(overview.appid)!==appid)continue;
          if(found&&found.overview!==overview)return {conflict:true};
          found={overview,key};
        }
        return found;
      };
      const snapshot=()=>{
        const raw=Array.from(ui.m_runningAppIDs),running=[...new Set(raw.map(canonical).filter(id=>id!==null))];
        const requested=[...new Set(ids.length?ids:running.slice(0,16))];
        const overviews=requested.map(appid=>{
          const found=lookup(appid);if(!found||found.conflict)return {appid,overviewAvailable:false,aliasConflict:found?.conflict===true};
          const {overview:o,key}=found;
          let windowIDs=null;try{windowIDs=ws?.GetAppWindowIDs?.(o.appid??key)?.slice(0,16).map(text)??null}catch{}
          const local=num(value(o,'local_per_client_data')?.display_status);
          const selected=num(value(o,'selected_per_client_data')?.display_status);
          // display_status is most-available-client state, NOT proof this machine is running.
          return {appid,overviewKey:key,overviewAvailable:true,gameidString:typeof o.gameid==='string'?o.gameid:null,gameidValueType:typeof o.gameid,name:typeof o.display_name==='string'?o.display_name.slice(0,192):null,
            localDisplayStatus:local,selectedDisplayStatus:selected,mostAvailableDisplayStatus:num(value(o,'display_status')),
            overviewSource:'steam-ui-native-overview-store',appType:num(o.app_type),windowIDs,
            inRunningList:raw.includes(o.appid??key),canonicalInRunningList:running.includes(appid),
            runningAliasMismatch:running.includes(appid)&&!raw.includes(o.appid??key),localRunning:local===4||local===1};
        });
        return {runningAppIDs:running.slice(0,16),runningTruncated:running.length>16,
          rawRunningCount:raw.length,invalidRunningCount:raw.filter(id=>canonical(id)===null).length,
          signedAliasesNormalized:raw.some(id=>canonical(id)!==null&&id<0),overviews};
      };
      const exactGameId=(target,appid)=>{
        if(!target||target.appid!==appid||typeof target.gameidString!=='string'||!/^[0-9]{1,20}$/.test(target.gameidString))return false;
        let raw;try{raw=BigInt(target.gameidString)}catch{return false;}
        if(raw>0xffffffffffffffffn||raw.toString()!==target.gameidString)return false;
        const type=target.appType,knownType=Number.isInteger(type)&&type>=0&&type<=0xffffffff,shortcut=knownType&&(type&0x40000000)!==0;
        return (knownType&&!shortcut&&raw===BigInt(appid)&&raw<=0xffffffn)||
          (shortcut&&raw>>32n===BigInt(appid)&&(raw&0xffffffffn)===0x02000000n);
      };
      // FocusedAppWindowStore is auxiliary UI state, not active Steam Input.
      // Query interaction guards without registering/replacing Steam callbacks.
      // Valve's active navigation context can fall back to the last context.
      // These read-only cache/DOM details explain a veto; they never grant it.
      const windowActivitySnapshot=(active,existingWindow=false)=>{
        const probe=read=>{try{return read()}catch{return null}};
        const bool=read=>{const result=probe(read);return typeof result==='boolean'?result:null};
        const browser=probe(()=>active?.BrowserWindow),document=probe(()=>browser?.document);
        const manager=probe(()=>existingWindow?dataField(ui,'m_GamepadNavigationManager'):ui.NavigationManager);
        const nav=probe(()=>typeof manager?.GetActiveContext==='function'?manager.GetActiveContext():null);
        const root=probe(()=>nav?.RootWindow),params=probe(()=>active?.params);
        const route=probe(()=>active?.LocationPathName),visibility=probe(()=>document?.visibilityState);
        // Reading DesktopOverlay would lazily construct the )YMSTEAM"
R"YMSTEAM(store. Observe only
        // an existing store; never create it or register Overlay callbacks.
        const desktop=probe(()=>active?.m_DesktopOverlay);
        const gameid=probe(()=>params?.gameid),pid=probe(()=>params?.browserInfo?.m_unPID);
        return {authority:'diagnostic-only-no-admission',source:'valve-navigation-cache-and-browser-dom',
          navigationContextObserved:nav!=null,rootMatchesBrowser:root!=null&&browser!=null?root===browser:null,
          navigationActive:bool(()=>nav?.BIsActive?.()),browserObserved:browser!=null,browserClosed:bool(()=>browser?.closed),
          documentVisibility:visibility==='visible'||visibility==='hidden'?visibility:'unknown',documentFocused:bool(()=>document?.hasFocus?.()),
          routeKind:route==='/apprunning'?'app-running':typeof route==='string'?'other':'unknown',
          windowAppid:canonical(probe(()=>params?.appid)),browserAppid:canonical(probe(()=>params?.browserInfo?.m_unAppID)),
          browserPid:Number.isInteger(pid)&&pid>0&&pid<=0xffffffff?pid:null,gameidString:typeof gameid==='string'&&/^\d{1,20}$/.test(gameid)?gameid:null,
          desktopOverlayActive:bool(()=>desktop?.overlay_active)};
      };
      const unknownInteraction=reason=>({available:false,idle:false,reason});
      const existingWindowSelection=()=>{
        try {
          const manager=dataField(ui,'m_GamepadNavigationManager');
          if(!ws||dataField(ui,'m_WindowStore')!==ws||typeof manager?.GetActiveContext!=='function')return {reason:'steam-passive-window-cache-unavailable'};
          // Archived/current GetActiveContext only returns the existing context;
          // RootWindow and BrowserWindow only return their existing window fields.
          const context=manager.GetActiveContext(),root=context?.RootWindow;
          if(root===null||!['object','function'].includes(typeof root))return {reason:'steam-passive-window-root-unavailable'};
          const overlay=dataField(ws,'m_mapOverlayPopupByPID'),desired=dataField(ws,'m_mapDesiredWindowInstances');
          const matches=new Set(),mapSize=Object.getOwnPropertyDescriptor(Map.prototype,'size').get;
          for(const cache of [overlay,desired]){
            let data=cache,wrapped=false,size;
            try{size=mapSize.call(data)}catch{
              // Current MobX maps expose an existing native data_ Map of value_
              // wrappers. Read data descriptors, never computed map properties.
              data=dataField(cache,'data_');wrapped=true;
              try{size=mapSize.call(data)}catch{return {reason:'steam-passive-window-cache-unavailable'}}
            }
            if(size>64)return {reason:'steam-passive-window-cache-limit'};
            const iterator=Map.prototype.values.call(data);let count=0;
            if(typeof iterator?.next!=='function')return {reason:'steam-passive-window-cache-invalid'};
            for(;;){
              const item=iterator.next();
              if(!item||typeof item.done!=='boolean')return {reason:'steam-passive-window-cache-invalid'};
              if(item.done)break;
              if(++count>size||count>64)return {reason:'steam-passive-window-cache-changed'};
              const instance=wrapped?dataField(item.value,'value_'):item.value;
              if(!instance||!['object','function'].includes(typeof instance))return {reason:'steam-passive-window-cache-invalid'};
              if(instance.BrowserWindow===root)matches.add(instance);
            }
            if(count!==size||mapSize.call(data)!==size||wrapped&&dataField(cache,'data_')!==data)return {reason:'steam-passive-window-cache-changed'};
          }
          if(matches.size!==1)return {reason:matches.size?'steam-passive-window-cache-ambiguous':'steam-passive-window-cache-no-match'};
          return {active:matches.values().next().value,context,root,manager,overlay,desired};
        } catch {return {reason:'steam-passive-window-cache-read-failed'};}
      };
      const interactiveSnapshot=(existingWindow=null)=>{
        try {
          if((!existingWindow&&typeof ui.GetFocusedWindowInstance!=='function')||typeof ui.GetShowingLockScreen!=='function'||typeof ws?.BHasStandaloneKeyboard!=='function')return existingWindow?unknownInteraction('steam-passive-interactive-api-unavailable'):{available:false,idle:false};
          const active=existingWindow?existingWindow.active:ui.GetFocusedWindowInstance(),lockScreen=ui.GetShowingLockScreen(),keyboard=ws.BHasStandaloneKeyboard(),menu=active?.MenuStore;
          // Diagnostic split of the SAME existing guards, not a cache override.
          const kindProbe=method=>{if(active==null)return false;if(typeof active[method]!=='function')return null;const value=active[method]();return typeof value==='boolean'?value:null;};
          const windowKinds={vr:kindProbe('IsAnyVRWindow'),gamepadOverlay:kindProbe('IsGamepadUIOverlayWindow'),desktopOverlay:kindProbe('IsDesktopOverlayWindow'),controllerConfigurator:kindProbe('IsControllerConfiguratorWindow')};
          const overlayOrVrWindow=Object.values(windowKinds).some(v=>v===true);
          const menuOpen=menu?(typeof menu.GetOpenSideMenu==='function'?menu.GetOpenSideMenu():null):0,menuSuppressed=!!(menu?.m_cSuppressRequests>0);
          const windowKindProbesComplete=Object.values(windowKinds).every(v=>typeof v==='boolean');
          const available=windowKindProbesComplete&&typeof lockScreen==='boolean'&&typeof keyboard==='boolean'&&Number.isInteger(menuOpen)&&menuOpen>=0;
          return {available,idle:available&&!lockScreen&&!keyboard&&!overlayOrVrWindow&&menuOpen===0&&!menuSuppressed,lockScreen,keyboard,overlayOrVrWindow,menuOpen,menuSuppressed,
            windowInstanceObserved:active!=null,windowKinds,windowKindProbesComplete,windowActivity:windowActivitySnapshot(active,!!existingWindow),focusEvidence:'steam-ui-window-instance-cache-not-os-foreground',
            ...(existingWindow&&!available?{reason:'steam-passive-interactive-guard-incomplete'}:{})};
        } catch {return existingWindow?unknownInteraction('steam-passive-interactive-read-failed'):{available:false,idle:false};}
      };
      const passiveInteractiveSnapshot=()=>{
        const selected=existingWindowSelection();
        if(selected.reason)return unknownInteraction(selected.reason);
        const observed=interactiveSnapshot(selected),after=existingWindowSelection();
        if(after.reason||Object.keys(selected).some(key=>selected[key]!==after[key]))return unknownInteraction('steam-passive-window-cache-changed');
        return observed;
      };
      const inputSnapshot=(queryConfigs=true,{passiveInspect=false}={})=>{
        const focus=window.FocusedAppWindowStore, editor=window.controllerConfiguratorStore;
        let controllers=null;try{const list=window.ControllerStore?.GetControllers?.();if(Array.isArray(list))controllers=list.slice(0,16).map(c=>({index:num(c.nControllerIndex),type:num(c.eControllerType)}))}catch{}
        let focusState={available:false,appid:null,windowID:null,overlayAppid:null,overlayPid:null};
        try{const raw=focus?.GetFocusedAppID?.(),overlay=focus?.GetFocusedOverlayAppID?.();focusState={available:raw===0||canonical(raw)!==null,appid:raw===0?0:canonical(raw),windowID:text(focus?.GetFocusedWindowID?.()),overlayAppid:overlay===0?0:canonical(overlay),overlayPid:num(focus?.GetFocusedOverlayPID?.())}}catch{}
        const configQueries=[];
        const controllerListValid=Array.isArray(controllers)&&controllers.every(c=>Number.isInteger(c.index)&&c.index>=0&&Number.isInteger(c.type));
        const readConfigs=async()=>{
          if(queryConfigs&&ids.length===1&&controllerListValid&&controllers.length&&typeof SteamClient.Input?.GetConfigForAppAndController==='function'){
            const found=lookup(ids[0]);if(found&&!found.conflict){
              const queries=controllers.slice(0,2).map(async c=>{
                try{const info=await SteamClient.Input.GetConfigForAppAndController(found.overview.appid??found.key,c.index);
                  const url=typeof info?.URL==='string'?info.URL:null;let hash=null;if(url!==null){hash=2166136261;for(let n=0;n<url.length;n++)hash=Math.imul(hash^url.charCodeAt(n),16777619)>>>0;}
)YMSTEAM"
R"YMSTEAM(                  return {appid:ids[0],index:c.index,status:info?'available':'unavailable',urlIdentity:hash,urlKind:url===null?'unknown':/(?:^|[\\/])empty\.vdf$/i.test(url)?'empty-or-passthrough':'configured',selectionType:num(info?.eSelectionType)};
                }catch{return {appid:ids[0],index:c.index,status:'unavailable'}}
              });
              try{configQueries.push(...await bounded(Promise.all(queries)))}catch{configQueries.push({appid:ids[0],status:'timeout'})}
            }
          }
        };
        const finish=()=>{
          const editedApp=value(editor,'EditingConfigurationAppId');
          const edit=num(value(editor,'m_nEditNumber')),saved=num(value(editor,'m_nLastSavedEditNumber'));const dirty=edit!==null&&saved!==null?edit!==saved:null;
          // Passive inspect resolves a unique existing cached window without any
          // aggregate window getter; only proven guards may establish idle.
          const interactive=passiveInspect?passiveInteractiveSnapshot():interactiveSnapshot();
          return {focus:focusState,controllers,controllerListValid,configQueries,configQueriesTruncated:controllerListValid&&controllers.length>2,interactive,
            editor:{available:!!editor,editingAvailable:!!editor&&'EditingConfigurationAppId' in editor&&'EditedConfiguration' in editor&&'PreviewedConfiguration' in editor&&typeof value(editor,'IsUpdatingEditingConfiguration')==='boolean'&&(editedApp===null||typeof editedApp==='number'&&Number.isInteger(editedApp)),editingActive:editedApp!==null&&editedApp!==-1&&editedApp!==0||editedApp===null&&!!value(editor,'EditedConfiguration'),editingAppid:editedApp===-1?null:editedApp===0?0:canonical(editedApp),dirty,previewActive:!!value(editor,'PreviewedConfiguration'),updating:!!value(editor,'IsUpdatingEditingConfiguration')},
            gameInput:'not-tested',inputEventsObserved:false};
        };
        // No-config state reads stay synchronous for the final send guard.
        // Config inspection retains its bounded async query and fresh finish.
        return queryConfigs?readConfigs().then(finish):finish();
      };
      // Receives Valve's real native window feedback independently of UI stores.
      // A callback can be an initial cached snapshot: receipt age is NOT state age.
      const nativeFocusFeedbackRead=async()=>{
        const started=performance.now(),accountObject=window.settingsStore?.m_CMInterface?.steamid;
        const result={implementation:'steam-native-focus-feedback-v1',source:'steam-native-focus-subscription',
          available:false,reason:'native-focus-unavailable',registrationAttempted:false,callbackReceived:false,
          ownSubscriptionUnregistered:false,nativeCacheFreshnessKnown:false,fullGameIdCarried:false,processBirthCarried:false,
          gameInputVerified:false,continueVerified:false,focused:null,focusable:null};
        if(window.__ymccNativeFocusReadQuarantine){result.reason='native-focus-subscription-quarantined';return result;}
        const api=SteamClient.System?.UI;
        if(typeof api?.RegisterForFocusChangeEvents!=='function'){result.reason='native-focus-subscription-unavailable';return result;}
        const quarantine=reason=>{window.__ymccNativeFocusReadQuarantine={implementation:result.implementation,reason};};
        const tuple=value=>{
          const uint=x=>typeof x==='number'&&Number.isInteger(x)&&x>=0&&x<=0xffffffff;
          if(!value||typeof value!=='object'||Array.isArray(value)||(value.strExeName!=null&&typeof value.strExeName!=='string')||typeof value.appid!=='number'||!Number.isInteger(value.appid)||
             value.appid < -0x80000000||value.appid>0xffffffff||!uint(value.pid)||!uint(value.windowid))return null;
          return {appid:value.appid>>>0,pid:value.pid,windowid:value.windowid,exeName:typeof value.strExeName==='string'?value.strExeName.slice(0,160):null};
        };
        let subscription=null,unregister=null,timer=null,returned=false,finished=false,receivedAt=null;
        await new Promise(resolve=>{
          const complete=()=>{
            if(finished||!returned)return;
            finished=true;if(timer!==null){clearTimeout(timer);timer=null;}
            try{
              if(typeof unregister!=='function')throw new Error('owned-unregister-unavailable');
              const reply=unregister.call(subscription);
              // The verified Valve contract returns synchronously. Unknown async
              // cleanup must not allow repeated registrations or unhandled rejects.
              if(reply&&typeof reply.then==='function'){Promise.resolve(reply).catch(()=>{});throw new Error('owned-unregister-contract-changed');}
              result.ownSubscriptionUnregistered=true;
            }catch{result.reason='native-focus-unregister-failed';quarantine(result.reason);}
            resolve();
          };
          const observe=payload=>{
            if(result.callbackReceived||finished)return;
            result.callbackReceived=true;receivedAt=performance.now();
            try{
              const object=payload&&typeof payload==='object'&&!Array.isArray(payload);
              const focused=object?tuple(payload.focusedApp):null,raw=object?payload.rgFocusable:null;
              const focusable=Array.isArray(raw)&&raw.length<=128?Array.from(raw,tuple):null;
              if(focused&&focusable&&focusable.every(v=>v!==null)){result.focused=focused;result.focusable=focusable;result.reason='native-focus-snapshot-received';}
              else result.reason='native-focus-schema-invalid';
            }catch{result.reason='native-focus-schema-invalid';}
            complete();
          };
          result.registrationAttempted=true;
          try{subscription=api.RegisterForFocusChangeEvents(observe);returned=true;}
          catch{result.reason='native-focus-registration-failed';quarantine(result.reason);finished=true;resolve();return;}
          try{unregister=subscription?.unregister;}catch{}
          if(typeof unregister!=='function'){
            result.reason='native-focus-unregister-contract-unavailable';quarantine(result.reason);finished=true;resolve();return;
          }
          if(result.callbackReceived)complete();
          else timer=setTimeout(()=>{result.reason='native-focus-callback-timeout';complete();},1000);
        });
        if(window.settingsStore?.m_CMInterface?.steamid!==accountObject)fail('steam-session-changed');
        checkAccount();
        result.available=result.reason==='native-focus-snapshot-received'&&result.ownSubscriptionUnregistered;
        result.readElapsedMs=Math.max(0,performance.now()-started);
        result.callbackAgeAtReadbackMs=receivedAt===null?null:Math.max(0,performance.now()-receivedAt);
        return result;
      };
      // Keep one bounded, account-bound observation subscription in this Steam
      // JS context. The callback is intentionally diagnostic-only: it carries
      // app lifetime events, not PID/birth/path, GameID, input or a Running
      // setter ACK. A fixed ring survives the sleep/wake gap so the native
      // layer can distinguish a real bRunning edge from an overview-only read.
      const overlayPopupCacheDiagnosticRead=()=>{
        const rows=[...new Set(ids)].map(appid=>({appid,available:false,reason:'cache-read-unavailable',popupCount:null,pidCandidates:null}));
        const result={implementation:'steam-overlay-popup-cache-v1',readOnly:true,sourceMatched:false,rows,
          sourceContract:'GetOverlayInstances-179-sp5a28',normalGetterCalls:0,snapshotAtomic:false,
          steamContactVerified:false,actualInputVerified:false,actualContinueVerified:false,recoveryAdmission:false};
        const unknown=reason=>{for(const row of rows)Object.assign(row,{available:false,reason,popupCount:null,pidCandidates:null});return result;};
        if(!rows.length)return unknown('no-target');
        try{
          // Keep this on the existing data store. Aggregate window getters can
          // create VR windows; neither they nor navigation/native calls belong here.
          if(!ws||dataField(ui,'m_WindowStore')!==ws)return unknown('window-store-unavailable');
          let method=null,owner=ws;
     )YMSTEAM"
R"YMSTEAM(     for(let i=0;i<5&&owner;i++,owner=Object.getPrototypeOf(owner)){
            const d=Object.getOwnPropertyDescriptor(owner,'GetOverlayInstances');
            if(d){if('value' in d&&typeof d.value==='function')method=d.value;break;}
          }
          const expected='GetOverlayInstances(Zt){let Nt=Array.from(this.m_mapOverlayPopupByPID.values()).reverse();return Nt=Nt.filter(Er=>Er.params.browserInfo.m_unAppID==Zt),Nt}';
          if(!method||Function.prototype.toString.call(method).trim()!==expected)return unknown('getter-source-unverified');
          result.sourceMatched=true;
          const cache=dataField(ws,'m_mapOverlayPopupByPID'),sizeGet=Object.getOwnPropertyDescriptor(Map.prototype,'size').get;
          let data=cache,wrapped=false,size;
          try{size=sizeGet.call(data);}catch{
            data=dataField(cache,'data_');wrapped=true;
            try{size=sizeGet.call(data);}catch{return unknown('map-unavailable');}
          }
          if(!Number.isInteger(size)||size<0||size>64)return unknown('map-budget');
          const entries=[],iterator=Map.prototype.entries.call(data);let count=0;
          for(;;){
            const item=iterator.next();if(item.done)break;
            if(++count>size||count>64)return unknown('cache-changed');
            const [key,cell]=item.value,instance=wrapped?dataField(cell,'value_'):cell;
            if(!instance||typeof instance!=='object')return unknown('map-unavailable');
            entries.push({key,cell,instance});
          }
          if(count!==size)return unknown('cache-changed');
          const stored=new Set(entries.map(entry=>entry.instance));
          for(const row of rows){
            // This exact Valve method only filters the existing popup cache.
            // Its ordinary params accessors are data reads, not window creation.
            result.normalGetterCalls++;const first=Reflect.apply(method,ws,[row.appid]);
            if(!Array.isArray(first)||first.length>64||first.some(instance=>!stored.has(instance)))return unknown('target-entry-unknown');
            const pids=first.map(instance=>instance.params?.browserInfo?.m_unPID);
            if(pids.some(pid=>!Number.isInteger(pid)||pid<0||pid>0xffffffff)||new Set(pids).size!==pids.length)return unknown('target-entry-unknown');
            result.normalGetterCalls++;const last=Reflect.apply(method,ws,[row.appid]);
            if(!Array.isArray(last)||last.length!==first.length||last.some((instance,i)=>instance!==first[i]||instance.params?.browserInfo?.m_unPID!==pids[i]))return unknown('cache-changed');
            Object.assign(row,{available:true,reason:'source-qualified-cache-read',popupCount:first.length,pidCandidates:pids});
          }
          if(dataField(ui,'m_WindowStore')!==ws||dataField(ws,'m_mapOverlayPopupByPID')!==cache||
              (wrapped&&dataField(cache,'data_')!==data)||sizeGet.call(data)!==size)return unknown('cache-changed');
          for(const {key,cell,instance} of entries){
            const current=Map.prototype.get.call(data,key);
            if(current!==cell||(wrapped?dataField(current,'value_'):current)!==instance)return unknown('cache-changed');
          }
          return result;
        }catch{return unknown('cache-read-unavailable');}
      };
      const overlayProviderCacheDiagnosticRead=()=>{
        const result={implementation:'steam-overlay-provider-cache-v1',readOnly:true,sourceMatched:false,sourcePathQualified:false,
          available:false,reason:'cache-read-unavailable',mapEntryCount:null,passesComplete:null,rows:null,snapshotAtomic:false,
          originalProviderObjectVerified:false,steamContactVerified:false,actualInputVerified:false,actualContinueVerified:false,recoveryAdmission:false};
        const unknown=reason=>Object.assign(result,{available:false,reason,mapEntryCount:null,passesComplete:null,rows:null});
        if(!ids.length)return unknown('no-target');
        let failureReason='cache-read-unavailable';
        const reject=reason=>{failureReason=reason;throw null;};
        try{
          const started=performance.now(),limit=()=>{if(performance.now()-started>500)reject('read-window-expired');};
          // No provider methods, getters, callbacks or native overlay requests.
          // A matched method qualifies this static path, never the original
          // singleton object or a real game/native connection.
          const own=(object,key)=>{
            const d=object&&Object.getOwnPropertyDescriptor(object,key);
            if(!d||!('value' in d))reject('data-path-unqualified');
            return d.value;
          };
          // This witness is supplied by the existing pinned native caller;
          // it selects the exact tail row without any focus/new-PID fallback.
          const witness=request.overlayProviderCacheTarget;
          if(ids.length!==1||!witness||typeof witness!=='object')return unknown('original-identity-bracket-unqualified');
          const targetPid=own(witness,'pid'),targetGameId=own(witness,'gameid64');
          if(!Number.isInteger(targetPid)||targetPid<=0||targetPid>0xffffffff||typeof targetGameId!=='string'||
              !/^[1-9][0-9]{0,19}$/.test(targetGameId))return unknown('original-identity-bracket-unqualified');
          const full=BigInt(targetGameId);
          if(full>0xffffffffffffffffn||!((full<=0xffffffn&&full===BigInt(ids[0]))||
              (full>>32n===BigInt(ids[0])&&(full&0xffffffffn)===0x02000000n)))return unknown('original-identity-bracket-unqualified');
          const targetKey=targetGameId+':'+targetPid;
          const provider=own(window,'overlayStore');
          if(!provider||typeof provider!=='object')return unknown('provider-store-unavailable');
          const methodOf=(object,name)=>{
            for(let owner=object,depth=0;owner&&depth<5;owner=Object.getPrototypeOf(owner),depth++){
              const d=Object.getOwnPropertyDescriptor(owner,name);
              if(d)return 'value' in d&&typeof d.value==='function'?d.value:null;
            }
            return null;
          };
          // Callback-decorated methods can hide their original bodies.
          // These two already-frozen ordinary methods are inspected only.
          const anchors=[{"name":"RegisterForBrowserCreated","expected":"RegisterForBrowserCreated(C){return this.m_OnBrowserCreatedCallbacks.Register(C)}"},{"name":"OverlayBrowserCreated","expected":"OverlayBrowserCreated(C,P,D,z){j.Debug(`Creating overlay browser window - pid: ${C.m_unPID}, browserid: ${C.m_nBrowserID}, for app ${C.m_unAppID} at ${P}x${D}, ${z} dpi`),this.m_OnBrowserCreatedCallbacks.Dispatch(C,P,D,z)}"}];
          const methods=anchors.map(anchor=>methodOf(provider,anchor.name));
          if(methods.some((method,i)=>!method||Function.prototype.toString.call(method).trim()!==anchors[i].expected))return unknown('provider-source-unverified');
          result.sourceMatched=true;
          const map=own(provider,'m_mapBrowserInfo'),sizeGet=Object.getOwnPropertyDescriptor(Map.prototype,'size').get;
          let size;try{size=sizeGet.call(map);}catch{return unknown('map-brand-unqualified');}
          if(!Number.isInteger(size)||size<0||size>128)return unknown('map-budget');
          result.sourcePathQualified=true;
          const names=['m_unPID','m_nBrowserID','m_eBrowserType','m_unAppID','m_gameID','m_eUIMode'];
          const tag=value=>{
            const rawType=value===null?'null':typeof value;
            if(rawType==='string'){
              if(value.length>64)reject('summary-budget');
              return {rawType,value};
            }
            if(rawType==='bigint'){if(value< -0xffffffffffffffffn||value>0xffffffffffffffffn)reject('summary-budget');return {rawType,value:value.toString(10)};}
            if(rawType==='number')return {rawType,value:Number.isSafeInteger(value)&&!Object.is(value,-0)?value:null};
            return {rawType,value:rawType==='boolean'?value:null};
          };
          const tailCheck=entries=>{
            limit();const target=entries.find(entry=>entry.key===targetKey);
            if(!target){if(Map.prototype.has.call(map,targetKey))reject('cache-changed');return;}
            if(Map.prot)YMSTEAM"
R"YMSTEAM(otype.get.call(map,targetKey)!==target.row)reject('cache-changed');
            for(let i=0;i<names.length;i++){
              const d=Object.getOwnPropertyDescriptor(target.row,names[i]);
              if(!d||!('value' in d)||!Object.is(d.value,target.raw[i]))reject('cache-changed');
            }
          };
          const take=()=>{
            limit();const entries=[],iterator=Map.prototype.entries.call(map);
            const next=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(iterator),'next')?.value;
            if(typeof next!=='function'||!Function.prototype.toString.call(next).includes('[native code]'))reject('map-brand-unqualified');
            for(let count=0;;count++){
              limit();const item=Reflect.apply(next,iterator,[]);
              if(item.done)break;
              if(count>=128)reject('map-budget');
              const [key,row]=item.value;
              if(typeof key!=='string'||key.length>64||!row||typeof row!=='object')reject('field-unavailable');
              const raw=[],fields={};
              for(const name of names){
                const d=Object.getOwnPropertyDescriptor(row,name);
                if(!d||!('value' in d))reject('field-unavailable');
                raw.push(d.value);fields[name]=tag(d.value);
              }
              entries.push({key,row,raw,fields});
            }
            if(entries.length!==size||sizeGet.call(map)!==size)reject('cache-changed');
            tailCheck(entries);
            return entries;
          };
          const first=take(),last=take();limit();
          if(own(window,'overlayStore')!==provider||own(provider,'m_mapBrowserInfo')!==map||methods.some((method,i)=>methodOf(provider,anchors[i].name)!==method)||
              sizeGet.call(map)!==size||first.length!==last.length)return unknown('cache-changed');
          for(let i=0;i<first.length;i++){
            if(first[i].key!==last[i].key||first[i].row!==last[i].row||first[i].raw.some((value,j)=>!Object.is(value,last[i].raw[j])))return unknown('cache-changed');
          }
          for(const entry of last)if(Map.prototype.get.call(map,entry.key)!==entry.row)reject('cache-changed');
          tailCheck(last);
          const rows=last.map(entry=>({entryKey:entry.key,fields:entry.fields}));
          // Only locally created scalar tags are serialized, never target
          // objects/toJSON. Raw rows are removed by the native log sanitizer.
          Object.assign(result,{available:true,reason:'source-qualified-cache-read',mapEntryCount:size,passesComplete:[true,true],rows});
          let bytes=0;for(const character of JSON.stringify(result)){const code=character.codePointAt(0);bytes+=code<0x80?1:code<0x800?2:code<0x10000?3:4;}
          if(bytes>8192)return unknown('summary-budget');
          limit();return result;
        }catch{return unknown(failureReason);}
      };
      const nativeAppLifetimeFeedbackRead=()=>{
        const started=performance.now();
        const result={implementation:'steam-app-lifetime-feedback-v1',source:'steam-game-sessions-app-lifetime-subscription',
          authority:'diagnostic-only-no-admission',available:false,reason:'app-lifetime-unavailable',
          registrationAttempted:false,observerInstalled:false,subscriptionUnregisterAvailable:false,subscriptionUnregistered:false,
          observerInstanceId:null,observerCreatedAtUnixMs:null,observerIdentityScope:'local-js-watch-only-not-cef-context-proof',
          registrationEvidenceScope:'local-registration-returned-without-throwing-only',nativeSubscriptionHealth:'unknown',
          diagnosticCountersAvailable:false,rawCallbacksEntered:null,accountUnavailableCallbacks:null,accountMismatchCallbacks:null,
          accountBound:false,callbackReceived:false,events:[],droppedEvents:0,latestEventSequence:0,
          schemaInvalidEvents:null,fullGameIdCarried:false,processBirthCarried:false,gameInputVerified:false,continueVerified:false};
        if(window.__ymccSteamAppLifetimeQuarantine){result.reason='app-lifetime-subscription-quarantined';return result;}
        const api=SteamClient.GameSessions;
        if(typeof api?.RegisterForAppLifetimeNotifications!=='function'){result.reason='app-lifetime-subscription-unavailable';return result;}
        const readAccount=()=>{
          const id=window.settingsStore?.m_CMInterface?.steamid;
          if(!id||typeof id.GetAccountID!=='function')throw Error('steam-session-starting');
          const value=id.GetAccountID();
          if(!Number.isInteger(value)||value<=0)throw Error('steam-session-starting');
          return value;
        };
        let watch=window.__ymccSteamAppLifetimeWatch;
        if(watch){
          let observedAccount;try{observedAccount=readAccount()}catch{result.reason='steam-session-starting';return result;}
          if(watch.account!==request.account||watch.account!==observedAccount){
            result.reason='app-lifetime-watch-account-changed';return result;
          }
        }
        if(!watch){
          const createdAtUnixMs=Date.now();
          watch={version:1,id:createdAtUnixMs.toString(36)+'-'+Math.random().toString(36).slice(2),account:request.account,createdAtUnixMs,
            diagnosticCountersVersion:1,rawCallbacksEntered:0,accountUnavailableCallbacks:0,accountMismatchCallbacks:0,
            events:[],droppedEvents:0,nextEventSequence:0,schemaInvalidEvents:0,active:false,
            registrationAttempted:false,unregisterAvailable:false,subscription:null};
          watch.observe=payload=>{
            // Count entry before the identity gate; rejected callbacks never become events.
            watch.rawCallbacksEntered++;
            let activeAccount;try{activeAccount=readAccount()}catch{watch.accountUnavailableCallbacks++;return;}
            if(watch.account!==request.account||watch.account!==activeAccount){watch.accountMismatchCallbacks++;return;}
            const object=payload&&typeof payload==='object'&&!Array.isArray(payload);
            const appid=object?payload.unAppID:null,running=object?payload.bRunning:null,rawInstance=object?payload.nInstanceID:null;
            const instance=typeof rawInstance==='number'&&Number.isSafeInteger(rawInstance)&&rawInstance>=0?String(rawInstance):typeof rawInstance==='string'&&/^\d{1,20}$/.test(rawInstance)?rawInstance:null;
            if(!Number.isInteger(appid)||appid<0||appid>0xffffffff||typeof running!=='boolean'||instance===null){watch.schemaInvalidEvents++;return;}
            const event={sequence:++watch.nextEventSequence,receivedAtUnixMs:Date.now(),receivedAtPerformanceMs:Math.max(0,performance.now()),unAppID:appid>>>0,nInstanceID:instance,bRunning:running};
            watch.events.push(event);
            if(watch.events.length>64){watch.events.shift();watch.droppedEvents++;}
          };
          window.__ymccSteamAppLifetimeWatch=watch;
          watch.registrationAttempted=true;result.registrationAttempted=true;
          try{
            const subscription=api.RegisterForAppLifetimeNotifications(watch.observe);
            watch.subscription=subscription;
            watch.unregisterAvailable=typeof subscription?.unregister==='function';
            // Valve's own UI discards this return value and retains callbacks in
            // a native vector. Do not require an undocumented disposer: this
            // observer is installed once per JS context and never re-registered.
            watch.active=true;
          }catch{
            window.__ymccSteamAppLifetimeQuarantine={implementation:result.implementation,reason:'app-lifetime-registration-failed'};
            watch.active=false;result.reason='app-lifetime-registration-failed';
          }
        }
        let activeAccount;
        try{activeAccount=readAccount()}catch{result.reason='steam-session-starting';return result;}
        result.registrationAttempted=!!watch.registrationAttempted;
        // This is only the local registration return, never native subscription health.
        result.observerInstalled=!!watch.active;
        result.observerInstanceId=typeof watch.id==='string'?watch.id:null;
        const diagnosticCount=value=>Number.isSafeInteger(value)&&value>=0?value:null;
   )YMSTEAM"
R"YMSTEAM(     result.observerCreatedAtUnixMs=diagnosticCount(watch.createdAtUnixMs);
        result.diagnosticCountersAvailable=watch.diagnosticCountersVersion===1&&
          [watch.rawCallbacksEntered,watch.accountUnavailableCallbacks,watch.accountMismatchCallbacks,watch.schemaInvalidEvents].every(value=>diagnosticCount(value)!==null);
        if(result.diagnosticCountersAvailable){
          result.rawCallbacksEntered=watch.rawCallbacksEntered;
          result.accountUnavailableCallbacks=watch.accountUnavailableCallbacks;
          result.accountMismatchCallbacks=watch.accountMismatchCallbacks;
        }
        result.subscriptionUnregisterAvailable=!!watch.unregisterAvailable;
        result.accountBound=watch.account===request.account&&watch.account===activeAccount;
        result.events=Array.isArray(watch.events)?watch.events.slice(-64):[];
        result.droppedEvents=Number.isInteger(watch.droppedEvents)?watch.droppedEvents:0;
        result.latestEventSequence=Number.isInteger(watch.nextEventSequence)?watch.nextEventSequence:0;
        // Do not backfill legacy watches: their old callback has no raw/account counters.
        result.schemaInvalidEvents=diagnosticCount(watch.schemaInvalidEvents);
        result.callbackReceived=result.events.length>0;
        result.available=result.observerInstalled&&result.accountBound;
        result.reason=result.available?'app-lifetime-observer-installed':result.reason;
        result.readElapsedMs=Math.max(0,performance.now()-started);
        return result;
      };
      const before=snapshot();
      if(request.operation==='sleep.input-resume') {
        if(ids.length!==1)fail('steam-single-target-required');
        const target=before.overviews[0],found=lookup(ids[0]);
        if(!found||found.conflict||!target.overviewAvailable)fail('steam-target-overview-unavailable');
        const proof=request.originalTarget;
        const originalProof=['captured-original-tracker','current-association','captured-original-local-running'].includes(proof?.basis)&&proof.appid===ids[0]&&Number.isInteger(proof.pid)&&proof.pid>0&&proof.pid<=0xffffffff&&
          typeof proof.created==='string'&&/^[1-9]\d{0,19}$/.test(proof.created)&&BigInt(proof.created)<=0xffffffffffffffffn;
        const trackedOriginal=originalProof&&proof.basis==='captured-original-tracker';
        const capturedLocalRunning=originalProof&&proof.basis==='captured-original-local-running';
        if(request.originalWindowReturn!=null&&typeof request.originalWindowReturn!=='boolean')fail('invalid-steam-original-window-return');
        const originalWindowReturn=request.originalWindowReturn===true;
        if(request.preControllerOriginalWindow!=null&&typeof request.preControllerOriginalWindow!=='boolean')fail('invalid-steam-pre-controller-window');
        const windowPreparation=request.preControllerOriginalWindow===true;
        if(windowPreparation&&(!originalProof||proof.basis!=='current-association'||!originalWindowReturn||!target.localRunning||target.localDisplayStatus!==4||!target.inRunningList))fail('steam-pre-controller-window-authority-unavailable');
        if(originalWindowReturn&&!originalProof)fail('steam-original-window-return-authority-unavailable');
        const returnAllowed=row=>!!row?.overviewAvailable&&(windowPreparation?row.localRunning&&row.localDisplayStatus===4&&row.inRunningList:
          row.localRunning&&(row.inRunningList||capturedLocalRunning&&[1,4].includes(row.localDisplayStatus))||trackedOriginal&&!row.localRunning&&row.localDisplayStatus===11);
        if(!returnAllowed(target))fail('steam-original-game-not-running');
        const context=await inputSnapshot(false);
        if(!context.focus.available)fail('steam-input-focus-unavailable');
        if(!context.controllerListValid||!context.controllers.length&&!windowPreparation)fail('steam-controller-not-connected');
        if(context.focus.appid===ids[0]&&!originalWindowReturn)return {ok:true,mutated:false,runtimeAccepted:true,stage,reason:'steam-input-context-current',inputContext:context,gameInput:'not-tested',healthVerified:false};
        // Native game focus must not dismiss a deliberate overlay/configurator,
        // keyboard, side menu, VR session or another running game's context.
        if(context.focus.overlayAppid!==0)fail('steam-overlay-focus-active-or-unknown');
        if(![0,769,413080].includes(context.focus.appid)&&!(originalWindowReturn&&context.focus.appid===ids[0]))fail('steam-other-app-focused');
        // EditingConfigurationAppId is a cached loaded-config identity, not a
        // visible editor proof. A clean cache for THIS game is allowed only
        // after the interactive window/menu guards below; other/dirty edits block.
        if(!context.editor.available||!context.editor.editingAvailable||(context.editor.editingActive&&context.editor.editingAppid!==ids[0])||context.editor.dirty!==false||context.editor.previewActive||context.editor.updating)fail('steam-input-editor-busy-or-unknown');
        if(typeof ui.GetFocusedWindowInstance!=='function'||typeof ui.GetShowingLockScreen!=='function'||typeof ws?.BHasStandaloneKeyboard!=='function')fail('steam-interactive-ui-state-unavailable');
        const assertInteractiveIdle=()=>{
          const observed=interactiveSnapshot();
          if(!observed.available||!observed.idle)fail('steam-interactive-ui-active');
        };
        assertInteractiveIdle();
        // Preserve the exact native GameID string. In particular, never round a
        // 64-bit shortcut GameID through Number or borrow another application's ID.
        const gameid=value(found.overview,'gameid');
        if(typeof gameid!=='string'||!/^\d{1,20}$/.test(gameid))fail('steam-gameid-unavailable');
        const rawGameid=BigInt(gameid),expected=BigInt(ids[0]);
        const type=target.appType,knownType=Number.isInteger(type)&&type>=0&&type<=0xffffffff,shortcut=knownType&&(type&0x40000000)!==0;
        const exactNative=knownType&&!shortcut&&rawGameid===expected&&rawGameid<=0xffffffn;
        const exactShortcut=shortcut&&rawGameid>>32n===expected&&(rawGameid&0xffffffffn)===0x02000000n;
        if(rawGameid>0xffffffffffffffffn||rawGameid.toString()!==gameid||!(exactNative||exactShortcut))fail('steam-gameid-mismatch');
        if(typeof SteamClient.Apps?.RaiseWindowForGame!=='function')fail('steam-native-continue-unavailable');
        const latest=snapshot().overviews[0],latestContext=await inputSnapshot(false);
        // An await yields even for a resolved read. Re-read all mutable send
        // guards synchronously afterwards; cached samples cannot authorize it.
        const dispatchTarget=snapshot().overviews[0],dispatchContext=inputSnapshot(false);
        const sameInputState=observed=>observed.focus.available&&observed.focus.appid===context.focus.appid&&observed.focus.overlayAppid===0&&
          (windowPreparation||JSON.stringify(observed.controllers)===JSON.stringify(context.controllers))&&observed.controllerListValid&&JSON.stringify(observed.editor)===JSON.stringify(context.editor);
        if(!returnAllowed(latest)||!returnAllowed(dispatchTarget)||lookup(ids[0])?.overview!==found.overview||value(found.overview,'gameid')!==gameid||!sameInputState(latestContext)||!sameInputState(dispatchContext))fail('steam-input-state-changed');
        checkAccount();assertInteractiveIdle();if(windowPreparation&&!returnAllowed(snapshot().overviews[0]))fail('steam-pre-controller-state-changed');stage='sleep-input-resume';mutated=true;
        // This is Valve's existing Windows "return to running game" operation.
        // No launch, console override, config reset, device cycle or fake UI state.
        const nativeValue=await bounded(SteamClient.Apps.RaiseWindowForGame(gameid));
        // Preserve only bounded primitive return evidence. Current native code
        // can return while its own window thread is still running; neither a
        // numeric code nor a settled call proves original-window/input success.
        const nativeNumber=typeof nativeValue==='number'&&Number.isInteger(nativeValue)&&nativeValue>=0&&nativeValue<=0xffffffff;
        const nativeBoolean=typeof nativeValue==='boolean';
       )YMSTEAM"
R"YMSTEAM( const nativeReturn=settledNativeReturn={source:'SteamClient.Apps.RaiseWindowForGame',kind:nativeNumber?'uint32':nativeBoolean?'boolean':typeof nativeValue==='undefined'?'undefined':'unsupported',
          observed:nativeNumber||nativeBoolean,code:nativeNumber?nativeValue:null,booleanValue:nativeBoolean?nativeValue:null,
          authority:'diagnostic-only-no-admission',nativeMeaningVerified:false,gameInputVerified:false,continueVerified:false};
        // FocusedAppWindowStore is only a UI cache, not active Steam Input or
        // the original-PID contract. Do not delay native OS-PID postflight on
        // its 3s readback loop; take an auxiliary fresh snapshot immediately.
        // Delayed cache catch-up belongs to bounded read-only wake observation.
        checkAccount();assertInteractiveIdle();const after=snapshot(),afterContext=await inputSnapshot(false);
        checkAccount();assertInteractiveIdle();
        const associationCurrent=!!(after.overviews[0]?.localRunning&&after.overviews[0]?.inRunningList);
        // A genuine native overview update can replace its JS object during
        // return. Rebind exact GameID/type after the action, not object address;
        // native postflight still verifies the original OS PID + process birth.
        const returnedTarget=after.overviews[0];
        const stateCurrent=returnAllowed(returnedTarget)&&returnedTarget.gameidString===gameid&&returnedTarget.appType===target.appType&&afterContext.focus.available&&afterContext.focus.overlayAppid===0&&afterContext.controllerListValid&&
          (windowPreparation||JSON.stringify(context.controllers)===JSON.stringify(afterContext.controllers))&&JSON.stringify(context.editor)===JSON.stringify(afterContext.editor);
        const uiFocusMatched=afterContext.focus.appid===ids[0],accepted=!windowPreparation&&stateCurrent&&uiFocusMatched;
        // Only native original-instance authority can request pending OS-PID
        // verification; a bare CDP request or another game's focus cannot.
        const windowPreparationPending=!!(windowPreparation&&stateCurrent&&[0,769,413080,ids[0]].includes(afterContext.focus.appid)&&after.overviews[0]?.localDisplayStatus===4&&after.overviews[0]?.inRunningList);
        const verificationPending=windowPreparationPending||!!(originalProof&&stateCurrent&&!uiFocusMatched&&[0,769,413080].includes(afterContext.focus.appid));
        return {ok:!!accepted,mutated,stage,runtimeAccepted:!!accepted,actionAcknowledged:true,nativeReturn,verificationPending,uiFocusMatched,uiFocusReadback:'auxiliary-cache-native-pid-postflight-required',
          reason:windowPreparationPending?'steam-original-window-prepared-input-unverified':accepted?'steam-input-context-refocused':verificationPending?'steam-input-ui-focus-readback-pending':'steam-input-context-not-confirmed',windowPreparation,controllerReconnectPending:windowPreparation&&afterContext.controllers.length===0,returnBasis:trackedOriginal?'captured-original-tracker':capturedLocalRunning?'captured-original-local-running':'current-association',originalWindowReturn,
          associationVerified:associationCurrent,before,inputContextBefore:context,after,inputContext:afterContext,gameInput:'not-tested',healthVerified:false};
      }
      if(request.operation==='sleep.reconcile') {
        // Only Valve's installed production rescope method is admitted. It derives
        // membership from existing backend overviews; no set.push/splice or fake status.
        if(!ids.length)fail('steam-target-required');
        if(before.overviews.some(o=>!o.overviewAvailable))fail('steam-target-overview-unavailable');
        const contactRequested=request.contactRecovery===true;
        const contact=request.contactProof;
        const sessionProof=request.sleepSessionProcess;
        const contactRequestValid=contactRequested&&contact&&typeof contact==='object'&&contact.connected===true&&Number.isInteger(contact.appid)&&contact.appid===ids[0]&&
          Number.isInteger(contact.pid)&&contact.pid>0&&contact.pid<=0xffffffff&&typeof contact.created==='string'&&/^[1-9][0-9]*$/.test(contact.created)&&
          sessionProof&&typeof sessionProof==='object'&&Number.isInteger(sessionProof.pid)&&sessionProof.pid>0&&typeof sessionProof.created==='string'&&/^[1-9][0-9]*$/.test(sessionProof.created)&&typeof sessionProof.root==='string'&&sessionProof.root.length>0;
        const targetBefore=before.overviews.length===1?before.overviews[0]:null;
        // ScopeRunningApps is global: its verified Valve body recomputes all
        // apps and may remove unrelated list members. Never spend it while a
        // different app is currently in Steam's running list.
        const contactGlobalScopeSafe=before.rawRunningCount===0&&before.invalidRunningCount===0&&before.runningTruncated===false&&
          Array.isArray(before.runningAppIDs)&&before.runningAppIDs.length===0;
        // A contact request retains its exact identity contract even if the
        // original naturally becomes Running before this JS starts executing.
        const contactTargetValid=contactRequestValid&&targetBefore&&[11,4,1].includes(targetBefore.localDisplayStatus)&&exactGameId(targetBefore,ids[0]);
        if(contactRequested&&!contactTargetValid)return {ok:false,mutated:false,stage,reason:'steam-contact-proof-rejected',before,contactRecovery:{requested:true,proofAccepted:false,readOnly:true},gameInput:'unknown',healthVerified:false};
        if(before.overviews.some(o=>!o.localRunning)&&!contactTargetValid)return {ok:false,mutated:false,stage,reason:'steam-backend-not-running',before,contactRecovery:{requested:false,proofAccepted:false,readOnly:true},gameInput:'unknown',healthVerified:false};
        const missing=before.overviews.some(o=>!o.inRunningList);
        // Already-current contact needs no global mutation, so unrelated list
        // members cannot veto that read-only result. A real Scope needs raw emptiness.
        if(contactRequested&&missing&&!contactGlobalScopeSafe)return {ok:false,mutated:false,stage,reason:'steam-contact-global-running-unsafe',before,contactRecovery:{requested:true,proofAccepted:true,globalScopeSafe:false,readOnly:true},gameInput:'unknown',healthVerified:false};
        let rescopeContract=null;
        if(missing) {
          if(typeof ui.ScopeRunningApps!=='function')fail('steam-rescope-unavailable');
          const source=Function.prototype.toString.call(ui.ScopeRunningApps);
          let hash=2166136261;for(let n=0;n<source.length;++n)hash=Math.imul(hash^source.charCodeAt(n),16777619)>>>0;
          // Manifest-verified Valve Windows sources, inspected through 2026-11-13.
          // Both derive membership from native local state; unknown bodies stay blocked.
          const knownScopes=[
            [998,2265414143,'valve-win64-1788652215'],
            [1038,4095097876,'valve-win64-1791856936'],
            [1043,3939488289,'valve-win64-1794424036']
          ];
          const contract=knownScopes.find(([length,fingerprint])=>source.length===length&&hash===fingerprint);
          rescopeContract={profile:contract?.[2]??'unknown',sourceLength:source.length,sourceFnv1a32:hash};
          if(!contract)return {ok:false,mutated:false,stage,reason:'steam-rescope-schema-changed',rescopeContract,gameInput:'unknown',healthVerified:false};
          checkAccount();stage='sleep-rescope';mutated=true;
          await bounded(ui.ScopeRunningApps());
        }
        checkAccount();const after=snapshot();
        const contactIdentityStable=!contactRequested||(contactTargetValid&&after.overviews.length===1&&
          exactGameId(after.overviews[0],ids[0])&&after.overviews[0].gameidString===targetBefore.gameidString&&
          after.overviews[0].appType===targetBefore.appType);
        const accepted=contactIdentityStable&&after.overviews.every(o=>o.overviewAvailable&&o.localRunning&&o.inRunningList);
        const contactAccepted=contactRequested&&contactTargetValid&&accepted;
        return {ok:accepted,mutated,stage,reason:accepted?'steam-association-current':contactTargetValid?'steam-contact-rescope-not-confirmed':'steam-rescope-not-confirmed',
          refreshed:missing,runtimeAccepte)YMSTEAM"
R"YMSTEAM(d:accepted,rescopeContract,contactRecovery:{requested:contactRequested,proofAccepted:contactTargetValid,accepted:contactAccepted,readOnly:true},before,after,gameInput:'unknown',healthVerified:false};
      }
      let overlayEnabled=null;try{const v=window.settingsStore?.GetClientSetting('enable_overlay')?.[0];overlayEnabled=typeof v==='boolean'?v:null}catch{}
      // Persistent association reads do not spend their independent 2s lane on
      // controller configuration RPCs. Sleep/input inspection retains those RPCs.
      const inputContext=await inputSnapshot(request.runningLinkRead!==true,{passiveInspect:request.operation==='sleep.inspect'});const controllers=inputContext.controllers;
      // Install the lifecycle observer before the focus callback can yield; its
      // bounded ring is what preserves edges across later sleep/wake reads.
      const nativeAppLifetimeFeedback=request.nativeAppLifetimeFeedbackRead===true?nativeAppLifetimeFeedbackRead():null;
      const nativeFocusFeedback=request.nativeFocusFeedbackRead===true?await nativeFocusFeedbackRead():null;
      const overlayPopupCacheDiagnostic=request.operation==='sleep.inspect'&&request.runningLinkRead===true?overlayPopupCacheDiagnosticRead():null;
      const overlayProviderCacheDiagnostic=request.operation==='sleep.inspect'&&request.runningLinkRead===true?overlayProviderCacheDiagnosticRead():null;
      checkAccount();
      return {ok:true,mutated:false,stage,...(request.runningLinkRead===true?snapshot():before),overlayEnabled,controllers,inputContext,...privateNativeRecordReply(),
        ...(nativeAppLifetimeFeedback?{nativeAppLifetimeFeedback}:{}),
        ...(overlayPopupCacheDiagnostic?{overlayPopupCacheDiagnostic}:{}),
        ...(overlayProviderCacheDiagnostic?{overlayProviderCacheDiagnostic}:{}),
        ...(nativeFocusFeedback?{nativeFocusFeedback}:{}),gameInput:'unknown',
        appBinding:ids.length?'operator-or-native-tracker-supplied-not-proven-by-js':'no-target-supplied'};
    }
    // The game may own Steam's overlay while navigation still points at the
    // main window. Use Valve's existing focused-overlay cache and exact lookup;
    // never create an overlay, choose an OS HWND, activate or focus a window.
    // Valve uses PID 0 for app-only lookup, including real Windows overlays.
    // Retain only the branch already taken by routing. No diagnostic window
    // property reads or overlay-map inventory on a functional key request.
    // One alias owner while the original Steam menu/overlay is active. The
    // original composition code and API return values remain authoritative.
    // No timers, input synthesis, idle work, params or composition-cache writes.
    // Private candidate: one passive, event-bounded window per qualified alias.
    // Source entry is an observation point, never proof of hardware delivery.
    const createMenuGameInputTrace = (entry, receiver, proof) => {
      const profile={"OnButtonDown":"OnButtonDown(u,B,d,S){B===void 0&&(B=-1),this.SetControllerActive(B),this.m_ButtonRepeatHandler.HandleInputButtonDown(u,()=>this.DispatchButtonDown(u,!1,d,S),()=>this.DispatchButtonDown(u,!0,d,S))}","OnButtonUp":"OnButtonUp(u,B){B===void 0&&(B=-1),this.SetControllerActive(B),this.m_ButtonRepeatHandler.HandleInputButtonUp(u),this.m_ButtonUpCallbacks.Dispatch(u,this.m_eNavigationSourceType,this.m_nLastActiveControllerIndex)}","OnButtonActionInternal":"OnButtonActionInternal(Zt,sr,lr,Rr,Ai,mi,dn,qi,Ji,Gr){for(let Ti=this.m_rgCatchAllGamepadInput.length-1;Ti>=0;Ti--)if(this.m_rgCatchAllGamepadInput[Ti](sr,Zt,Ai??!1)){Zt&&he(\"Ignoring button press - gamepad input is suppressed by parent window\");return}oe(!!mi==!!dn,\"Must set both overrideContext and overrideElement or neither\");let hi=mi,li=dn;if((hi==null||li==null)&&([hi,li]=this.GetEventTarget(sr,lr,Zt)),li?.BIsGamepadInputSuppressed()&&!qi){he(`Suppressing ${n.pR[sr]} input on element ${hi?.className} because tree ${li?.m_LastActiveNavTree?.id} has it disabled`);return}this.ChangeNavigationSource(lr,Rr,Ji,Gr),Zt&&he(`Firing ${n.pR[sr]} in tree ${li?.m_LastActiveNavTree?.id} at `,hi),this.BatchedUpdate(()=>(0,w.AE)(hi,Zt?\"vgp_onbuttondown\":\"vgp_onbuttonup\",{button:sr,source:lr,is_repeat:Ai}))}","GetEventTarget":"GetEventTarget(Zt,sr,lr=!1){let Rr=this.GetActiveContext();if(this.m_nSystemKeyAppIDHint&&!Rr?.FindNavTreeInFocusedWindow()){const mi=this.m_fnSystemKeyContextResolver?.(this.m_nSystemKeyAppIDHint);mi&&mi!=Rr&&this.BCanActivateContext(mi)&&(he(`GetEventTarget: using ${mi.LogName()} for system key AppID ${this.m_nSystemKeyAppIDHint} instead of ${Rr?.LogName()??\"no context\"}`),Rr?.ActiveWindow&&Rr.OnDeactivate(Rr.ActiveWindow),mi.OnActivate(mi.m_LastActiveFocusNavTree?.Window??mi.RootWindow),Rr=mi)}!Rr&&lr&&(Rr=this.FindAnActiveContext());let Ai=Rr?.ActiveWindow?.document.activeElement;if(Rr?.m_LastActiveNavTree){if(sr||(sr=this.m_navigationSource?.Value?.eActivationSourceType),!Rr.m_LastActiveNavTree.GetLastFocusedNode()&&(0,n.UE)(sr)&&(he(`GetEventTarget: Context ${Rr.LogName()} tree ${Rr.m_LastActiveNavTree.id} has no focused node, ${lr?\"finding one\":\"will not find one\"}`),lr&&Rr.m_LastActiveNavTree.TakeFocus(w.D$.GAMEPAD,!0),!this.BGlobalGamepadButton(Zt)))return[void 0,Rr];Rr.m_LastActiveNavTree.GetLastFocusedNode()?Ai=Rr.m_LastActiveNavTree?.GetLastFocusedNode()?.Element:he(`GetEventTarget: Context ${Rr.LogName()} tree ${Rr.m_LastActiveNavTree.id} still has no focused node - will fall back to document.activeElement`)}return[Ai,Rr]}","GetActiveContext":"GetActiveContext(){return this.m_ActiveContext}","GetFocusNavContext":"GetFocusNavContext(){return this.IsVRGamepadUIOverlayWindow()?l.oy.WindowStore.VRGamepadUIMainWindowInstance.GetFocusNavContext():l.oy.NavigationManager.FindContextForRootWindow(this.m_BrowserWindow)}","sourceEnum":{"UNKNOWN":0,"GAMEPAD":1,"KEYBOARD_SIMULATOR":2,"MOUSE":3,"TOUCH":4,"LPAD":5,"RPAD":6}};
      const game=receiver.game,w=receiver.window;
      // Immutable creation-time tuple from the already-verified alias proof.
      // A diagnostic copy never becomes a current identity/admission proof.
      const identity=Object.freeze({source:'verified-install-proof-copy',diagnosticOnly:true,current:false,
        gamePid:proof.gamePid,gameCreated:proof.gameCreated,steamPid:proof.steamPid,steamCreated:proof.steamCreated,
        hostPid:proof.hostPid,hostCreated:proof.hostCreated,browserId:proof.browserId,
        logicalGameid64:proof.gameid64,registeredGameid64:proof.registeredGameid64,registeredAppid:proof.registeredAppid});
      // Original-call lookup records cannot identify their caller. A separate
      // owned passive activation observer reports its own callback primitives
      // without replacing or invoking Valve's original bound action callback.
      let nativeAttempted=false,nativeStatus='not-started',nativeReason='no-explicit-action';
      let nativeStart=null,nativeElapsed=null;
      const nativeCalls=[];
      const nativeStop=why=>{
        if(nativeStatus==='observing'){
          nativeStatus='stopped';
          const known=['alias-dispose','next-menu-request','event-limit','window-expired','clock-unavailable',
            'owner-or-menu-ended','observation-unavailable','installation-unavailable','trace-owner','trace-manager',
            'trace-profile','trace-arrays','trace-source','trace-documents','trace-clock'];
          nativeReason=known.includes(why)?why:'external-stop';
        }
        // Original-call and activation callback caps are independent.
        if(why!=='event-limit')activationStop(why);
      };
      const nativeLive=()=>{
        if(nativeStatus!=='observing')return false;
        try{
          const now=performance.now();
          if(!Number.isFinite(now)||now<nativeStart){nativeElapsed=null;nativeStop('clock-unavailable');return false;}
          nativeElapsed=Math.min(now-nativeStart,10000);
          if(now-nativeStart>=10000){nativeStop('window-expired');return false;}
          if(window.__ymccMenuGameAlias!==entry||entry.disposed){nativeStop('owner-or-menu-ended');return false;}
          return true;
        }catch{nativeStop('observation-unavailable');return false;}
     )YMSTEAM"
R"YMSTEAM( };
      const armNative=()=>{
        if(nativeAttempted||entry.disposed||window.__ymccMenuGameAlias!==entry)return;
        nativeAttempted=true;
        try{
          nativeStart=performance.now();
          if(!Number.isFinite(nativeStart)){nativeReason='clock-unavailable';nativeStatus='stopped';return;}
          nativeElapsed=0;nativeStatus='observing';nativeReason='original-call-window';
          armActivation();
        }catch{nativeStatus='stopped';nativeReason='clock-unavailable';}
      };
      const nativeCall=(kind,path,self,id,value,result,threw=false)=>{
        if(!nativeLive())return;
        const app=Number.isInteger(id)&&id>=-0x80000000&&id<=0xffffffff?id:null;
        const pid=Number.isInteger(value)&&value>=0&&value<=0xffffffff?value:null;
        const full=typeof id==='string'&&/^[1-9][0-9]{0,19}$/.test(id)&&BigInt(id)<=0xffffffffffffffffn?id:null;
        const row={ordinal:nativeCalls.length+1,elapsedMs:nativeElapsed,kind,path,receiverMatches:self,
          appid:kind==='state'?null:app,
          appidRole:kind==='state'?'not-applicable':app===null?'unknown':(app>>>0)===proof.appid?'logical':(app>>>0)===proof.registeredAppid?'registered':'other',
          pid:kind==='lookup'?pid:null,
          pidRole:kind!=='lookup'?'not-applicable':pid===null?'unknown':pid===proof.hostPid?'held-host':pid===proof.gamePid?'game':pid===0?'zero':'other',
          gameid64:kind==='state'?full:null,
          gameidRole:kind!=='state'?'not-applicable':full===null?'unknown':full===proof.gameid64?'logical':full===proof.registeredGameid64?'registered':'other',
          state:kind==='state'&&Number.isInteger(value)&&value>=0&&value<=5?value:null,
          outcome:threw?'threw':'returned',resultKind:threw?'throw':result===w?'held-window':result==null?'none':'other',
          arrayLength:null,heldOccurrences:null};
        // Inspect no getters/thenable or external object. Own array data slots
        // are optional diagnostic facts and never become routing permission.
        if(!threw&&kind==='list')try{
          if(Array.isArray(result)){
            const length=Object.getOwnPropertyDescriptor(result,'length');
            if(length&&'value'in length&&Number.isInteger(length.value)&&length.value>=0&&length.value<=16){
              let held=0,known=true;
              for(let n=0;n<length.value;++n){const item=Object.getOwnPropertyDescriptor(result,String(n));if(!item||!('value'in item)){known=false;break;}if(item.value===w)++held;}
              row.resultKind='array';row.arrayLength=length.value;row.heldOccurrences=known?held:null;
            }
          }
        }catch{} // Original return and original throw remain untouched.
        nativeCalls.push(Object.freeze(row));
        if(nativeCalls.length===16)nativeStop('event-limit');
      };
      const nativeSnapshot=()=>{
        nativeLive();
        return Object.freeze({schema:1,diagnosticOnly:true,scope:'existing-alias-original-calls',
          attempted:nativeAttempted,status:nativeStatus,reason:nativeReason,observedWindowMs:nativeElapsed,
          eventCount:nativeCalls.length,maxEvents:16,windowLimitMs:10000,callerCallback:'UNKNOWN',
          finalWrapperSelectionProven:false,nativeInputAcceptance:'UNKNOWN',physicalInputProven:false,
          calls:Object.freeze([...nativeCalls]),activation:activationSnapshot()});
      };
      const data=(object,key)=>{const d=object&&Object.getOwnPropertyDescriptor(object,key);return d&&'value'in d?d.value:undefined;};
      const method=(object,key)=>{
        for(let n=0;object&&n<8;++n,object=Object.getPrototypeOf(object)){
          const d=Object.getOwnPropertyDescriptor(object,key);
          if(d)return 'value'in d&&typeof d.value==='function'?d.value:null;
        }
        return null;
      };
      // Valve's independent useEffect subscription calls returned.unregister().
      // This observer never replaces/calls the original bound action callback.
      // Collect only exact held-Host events; no input/focus/state permission.
      const activationKey='__ymccMenuNativeActivationObservation';
      let activationAttempted=false,activationStatus='not-started',activationReason='no-explicit-action';
      let activationResource=null,activationSlot=null,activationRegister=null,activationApi=null;
      let activationCallbacks=0,activationIgnored=0,activationInvalid=0,activationCleaning=false;
      let activationElapsed=null;
      let activationCleanupReason='not-required';
      const activationEvents=[];
      const activationSlotIsOurs=()=>{
        try{const d=Object.getOwnPropertyDescriptor(window,activationKey);return !!d&&'value'in d&&d.value===activationSlot&&d.writable===true&&d.configurable===true&&d.enumerable===false;}catch{return false;}
      };
      const releaseActivation=()=>{
        const resource=activationResource;
        if(!resource||!resource.returned||resource.clean||activationCleaning||!resource.retryAllowed)return;
        if(typeof resource.unregister!=='function'){activationCleanupReason='unregister-unavailable';return;}
        activationCleaning=true;++resource.cleanupAttempts;
        try{
          const reply=Reflect.apply(resource.unregister,resource.handle,[]);
          // A changed asynchronous cleanup contract is quarantined, not awaited
          // as successful or used to allow another subscription. Absorb its own
          // rejection, retaining this returned token and its original receiver.
          if(reply&&(typeof reply==='object'||typeof reply==='function')){
            let then;try{then=reply.then;}catch{resource.retryAllowed=false;throw new Error('unregister-result-unavailable');}
            if(typeof then==='function'){
              resource.retryAllowed=false;Promise.resolve(reply).catch(()=>{});
              throw new Error('unregister-async-unconfirmed');
            }
          }
          // Unknown non-void returns cannot certify resource release.
          if(reply!==undefined){resource.retryAllowed=false;throw new Error('unregister-result-unconfirmed');}
          resource.clean=true;activationCleanupReason='returned';
          resource.handle=null;resource.unregister=null;
          if(activationSlotIsOurs())delete window[activationKey];
        }catch{activationCleanupReason=resource.retryAllowed?'unregister-threw':'unregister-contract-unknown';}
        finally{activationCleaning=false;}
      };
      const activationStop=why=>{
        if(activationStatus==='observing'||activationStatus==='registering'){
          activationStatus='stopped';
          const known=['alias-dispose','next-menu-request','event-limit','window-expired','clock-unavailable',
            'owner-or-menu-ended','observation-unavailable','callback-limit','activation-owner-changed'];
          activationReason=known.includes(why)?why:'external-stop';
        }
        // Retry only this returned resource on existing lifecycle cleanup. A
        // failure retains the slot/token and denies a future registration.
        releaseActivation();
      };
      const activationLive=()=>{
        if(activationStatus!=='observing')return false;
        try{
          const now=performance.now();
          if(!Number.isFinite(now)||now<nativeStart){activationElapsed=null;activationStop('clock-unavailable');return false;}
          activationElapsed=Math.min(now-nativeStart,10000);
          if(now-nativeStart>=10000){activationStop('window-expired');return false;}
          if(window.__ymccMenuGameAlias!==entry||entry.disposed||!entry.current()||!activationSlotIsOurs()||
             SteamClient.Overlay!==activationApi||method(activationApi,'RegisterForOverlayActivated')!==activationRegister){activationStop('activation-owner-changed');return false;}
          return true;
        }catch{activationStop('observation-unavailable');return false;}
      };
      const observeActivation=(pid,appid,active,fourth)=>{
        if(!activationLive())return;
        try{
          ++activationCallbacks;
          const validPid=Number.isInteger(pid)&&pid>=0&&pid<=0xffffffff;
          const validApp=Number.isInteger(appid)&&appid>=-0x80000000&&appid<=0xffffffff;
          const unsigned=)YMSTEAM"
R"YMSTEAM(validApp?appid>>>0:null;
          if(!validPid||!validApp||pid!==proof.hostPid||unsigned!==proof.appid&&unsigned!==proof.registeredAppid)++activationIgnored;
          else{
            const type=value=>value===null?'null':['undefined','boolean','number','string'].includes(typeof value)?typeof value:'other';
            if(typeof active!=='boolean'||typeof fourth!=='boolean')++activationInvalid;
            activationEvents.push(Object.freeze({ordinal:activationEvents.length+1,elapsedMs:activationElapsed,
              pid,appidRaw:appid,appid:unsigned,appidRole:unsigned===proof.appid?'logical':'registered',
              active:typeof active==='boolean'?active:null,fourth:typeof fourth==='boolean'?fourth:null,
              activeType:type(active),fourthType:type(fourth)}));
          }
          if(activationCallbacks===16)activationStop('callback-limit');
        }catch{try{activationStop('observation-unavailable');}catch{}}
      };
      const armActivation=()=>{
        if(activationAttempted||activationStatus!=='not-started')return;
        try{
          activationApi=SteamClient.Overlay;activationRegister=method(activationApi,'RegisterForOverlayActivated');
          if(!activationRegister){activationStatus='unavailable';activationReason='registration-api-unavailable';return;}
          if(Object.getOwnPropertyDescriptor(window,activationKey)){
            activationStatus='blocked';activationReason='prior-subscription-or-foreign-slot';return;
          }
          activationResource={handle:null,unregister:null,returned:false,clean:false,retryAllowed:true,cleanupAttempts:0};
          activationSlot=Object.freeze({kind:'ymcc-menu-native-activation-v1',resource:activationResource});
          Object.defineProperty(window,activationKey,{value:activationSlot,writable:true,configurable:true});
          if(!activationSlotIsOurs()){activationStatus='unavailable';activationReason='observation-slot-unavailable';return;}
          activationAttempted=true;activationStatus='observing';activationReason='native-event-window';
          try{activationResource.handle=Reflect.apply(activationRegister,activationApi,[observeActivation]);}
          catch{
            activationResource.returned=true;activationResource.retryAllowed=false;
            activationStatus='stopped';activationReason='registration-threw';activationCleanupReason='registration-outcome-unknown';return;
          }
          activationResource.returned=true;
          activationResource.unregister=method(activationResource.handle,'unregister');
          if(!activationResource.unregister){
            activationStatus='stopped';activationReason='unregister-contract-unavailable';
            activationCleanupReason='unregister-unavailable';activationResource.retryAllowed=false;
            // A changed registration may return a promise instead of its owned
            // token. Consume only this API's rejection; keep cleanup UNKNOWN.
            if(activationResource.handle&&(typeof activationResource.handle==='object'||typeof activationResource.handle==='function'))
              Promise.resolve(activationResource.handle).catch(()=>{});
            return;
          }
          if(activationStatus!=='observing')releaseActivation(); // synchronous callbacks may have ended the window
        }catch{
          activationStatus='stopped';activationReason='registration-unavailable';
          if(activationAttempted)activationCleanupReason='registration-outcome-unknown';
          else if(activationSlotIsOurs())try{delete window[activationKey];}catch{}
        }
      };
      const activationSnapshot=()=>{activationLive();return Object.freeze({schema:1,diagnosticOnly:true,
        source:'steam-native-overlay-activation-subscription',attempted:activationAttempted,
        status:activationStatus,reason:activationReason,callbackReceived:activationCallbacks>0,
        matchingCallbackReceived:activationEvents.length>0,
        callbacksObserved:activationCallbacks,ignoredCallbacks:activationIgnored,invalidMatchingPayloads:activationInvalid,
        eventCount:activationEvents.length,maxCallbacks:16,windowLimitMs:10000,
        expiry:'semantic-window-checked-on-callback-or-existing-snapshot',noTimer:true,
        ownSubscriptionUnregistered:activationResource?.clean===true,
        cleanupPending:activationAttempted&&activationResource?.clean!==true,
        cleanupAttempts:activationResource?.cleanupAttempts??0,cleanupReason:activationCleanupReason,
        originalActionCallbackDelivery:'UNKNOWN',callbackStateFreshnessKnown:false,
        fourthArgumentProducerMeaning:'UNKNOWN',inputCapabilityGranted:false,physicalInputProven:false,
        observedWindowMs:activationElapsed,events:Object.freeze([...activationEvents])});};

      const exact=(fn,key)=>typeof fn==='function'&&Function.prototype.toString.call(fn)===profile[key];
      const same=(a,b)=>!!a&&!!b&&a.value===b.value&&a.writable===b.writable&&
        a.enumerable===b.enumerable&&a.configurable===b.configurable&&'value'in a&&'value'in b;
      const counts={sourceDown:0,sourceUp:0,managerAction:0,managerTarget:0,targetReadOutsideAction:0,
        actionSourceGamepad:0,actionSourceKeyboard:0,actionSourceOther:0,actionSourceUnknown:0,
        documentVgpDown:0,documentVgpUp:0,documentTouchStart:0,documentTouchEnd:0,
        documentPointerDown:0,documentPointerUp:0,documentMouseDown:0,
        methodReturned:0,methodThrew:0,foreignThis:0,targetHeldContext:0,
        targetOtherContext:0,targetMenuDocument:0,targetOtherDocument:0,targetUnknown:0};
      let attempted=false,status='not-started',reason='no-explicit-open',events=0,startTime=null;
      let observedWindowMs=null,installationComplete=false;
      let ui=null,manager=null,context=null,source=null,sources=null,sourceItems=null,contexts=null;
      let getActive=null,getContext=null,tree=null,treeWindow=null,browser=null,docs=[];
      const hooks=[],listeners=[];
      let actionFrame=null,actionEpoch=0;
      const receiptOwner=Object.freeze({});let receiptSlot=null,receiptConsumed=false;
      const publishTerminal=()=>{
        if(!attempted||status!=='stopped'||nativeStatus==='observing'||receiptConsumed)return;
        try{
          const key='__ymccMenuInputTraceReceipt',d=Object.getOwnPropertyDescriptor(window,key);
          if(receiptSlot&&!d)return; // Already consumed; never resurrect an old receipt.
          if(d&&(!('value'in d)||d.value!==receiptSlot||d.writable!==true||d.configurable!==true||d.enumerable!==false))return;
          // One immutable terminal snapshot, with no manager/window/source refs.
          const value=render();Object.freeze(value.counts);Object.freeze(value);
          const next=Object.freeze({kind:'ymcc-menu-input-trace-v1',owner:receiptOwner,snapshot:value});
          Object.defineProperty(window,key,d?{...d,value:next}:{value:next,writable:true,configurable:true});
          receiptSlot=next;
        }catch{}
      };
      const nativeSource=value=>{
        if(!value||data(value,'m_NavController')!==manager)return false;
        const map=data(value,'m_rgControllers');
        // Brand check only: no controller keys, indices or device samples.
        try{Object.getOwnPropertyDescriptor(Map.prototype,'size').get.call(map);}catch{return false;}
        return ['HandleControllerInputMessages','HandleSystemKeyEvents'].every(key=>{
          const d=Object.getOwnPropertyDescriptor(value,key);
          return !!d&&'value'in d&&d.writable===false&&d.configurable===false&&
            typeof d.value==='function'&&Function.prototype.toString.call(d.value)==='function () { [native code] }';
        });
      };
      const cleanup=()=>{
        // Each successful/possibly-partial installation is retried independently.
        for(const item of listeners){
          if(item.done)continue;
          try{Reflect.apply(item.remove,item.doc,[item.type,item.listener,true]);item.done=true;}catch{}
        }
        for(let n=hooks.length-1;n>=0;--n){
          const item=hooks[n];if(item.done)continue;
          try{
            const d=Object.getOwnPropertyDescriptor(item.object,item.key);
            if(item.before?same(d,i)YMSTEAM"
R"YMSTEAM(tem.before):d===undefined){
              item.notInstalled=!item.wasInstalled;item.returned=true;item.done=true;continue;
            }
            if(!same(d,item.installed)){
              // A later owner is never overwritten, including its new flags.
              if(d&&'value'in d&&d.value===item.wrapper){item.foreign=true;continue;}
              item.foreign=true;item.done=true;continue;
            }
            item.wasInstalled=true;
            if(item.before)Object.defineProperty(item.object,item.key,item.before);
            else delete item.object[item.key];
            const after=Object.getOwnPropertyDescriptor(item.object,item.key);
            if(item.before?same(after,item.before):after===undefined){item.done=true;item.restored=true;item.returned=true;}
          }catch{} // One failed restoration must not skip the other three.
        }
      };
      const stop=why=>{
        if(['alias-dispose','next-menu-request','event-limit','window-expired','clock-unavailable','owner-or-menu-ended'].includes(why))nativeStop(why);
        if(status==='observing'||status==='installing'){
          if(startTime!==null){
            try{const now=performance.now();observedWindowMs=why!=='clock-unavailable'&&Number.isFinite(now)&&now>=startTime?Math.min(now-startTime,10000):null;}catch{observedWindowMs=null;}
          }
          const allowed=['alias-dispose','next-menu-request','event-limit','window-expired','clock-unavailable',
            'owner-or-menu-ended','observation-unavailable','installation-unavailable','trace-owner','trace-manager',
            'trace-profile','trace-arrays','trace-source','trace-documents','trace-clock'];
          status='stopped';reason=allowed.includes(why)?why:'external-stop';++actionEpoch;actionFrame=null;
        }
        // Never return early merely because already stopped/disposed.
        cleanup();
        publishTerminal();
      };
      const ownerCurrent=()=>{
        if(window.__ymccMenuGameAlias!==entry||entry.disposed||!entry.current()||!game.current()||
          window.SteamUIStore!==ui||ui.NavigationManager!==manager||data(ui,'m_GamepadNavigationManager')!==manager||
          method(w,'GetFocusNavContext')!==getContext||Reflect.apply(getContext,w,[])!==context||
          data(w,'m_FocusNavContext')!==context||data(context,'m_controller')!==manager||data(context,'m_bMounted')!==true||
          method(manager,'GetActiveContext')!==getActive||Reflect.apply(getActive,manager,[])!==context||
          data(manager,'m_rgAllContexts')!==contexts||!Array.isArray(contexts)||contexts.length>32)return false;
        let matches=0;
        for(let n=0;n<contexts.length;++n){const d=Object.getOwnPropertyDescriptor(contexts,String(n));if(!d||!('value'in d))return false;if(d.value===context)++matches;}
        if(matches!==1||data(manager,'m_rgGamepadInputSources')!==sources||!Array.isArray(sources)||
          sources.length!==sourceItems.length||sources.length>64)return false;
        for(let n=0;n<sourceItems.length;++n){const d=Object.getOwnPropertyDescriptor(sources,String(n));if(!d||!('value'in d)||d.value!==sourceItems[n])return false;}
        if(!nativeSource(source)||context.m_LastActiveNavTree!==tree||tree.Window!==treeWindow||
          w.BrowserWindow!==browser||treeWindow.document!==docs[0]||browser.document!==(docs[1]??docs[0]))return false;
        if(![1,2].includes(w.MenuStore.GetOpenSideMenu()))return false;
        return hooks.every(item=>same(Object.getOwnPropertyDescriptor(item.object,item.key),item.installed));
      };
      const live=()=>{
        if(status!=='observing'){cleanup();publishTerminal();return false;}
        try{
          const now=performance.now();
          if(!Number.isFinite(now)||now<startTime){stop('clock-unavailable');return false;}
          observedWindowMs=Math.min(now-startTime,10000);
          if(now-startTime>=10000){stop('window-expired');return false;}
          if(events>=32){stop('event-limit');return false;}
          if(!ownerCurrent()){stop('owner-or-menu-ended');return false;}
          return true;
        }catch{stop('observation-unavailable');return false;}
      };
      const note=(key,foreignThis=false)=>{
        if(!live())return false;
        ++events;if(foreignThis)++counts.foreignThis;else ++counts[key];
        // The final admitted callback still calls its original exactly once.
        if(events===32)stop('event-limit');
        return true;
      };
      const wrap=(object,key,label)=>{
        const before=Object.getOwnPropertyDescriptor(object,key),original=method(object,key);
        if(!exact(original,key)||(before&&(!('value'in before)||before.configurable!==true))||
          (!before&&!Object.isExtensible(object)))throw new Error('trace-profile');
        const item={object,key,before,original,wrapper:null,installed:null,done:false,restored:false,
          returned:false,notInstalled:false,wasInstalled:false,foreign:false};
        item.wrapper=function(...args){
          let observed=false,frame=null;
          let effectiveLabel=label;
          try{
            // A preparatory/read-only target lookup is never an input action.
            if(key==='GetEventTarget'&&!(actionFrame&&actionFrame.epoch===actionEpoch&&this===manager))
              effectiveLabel='targetReadOutsideAction';
            observed=note(effectiveLabel,this!==object);
            if(observed&&key==='OnButtonActionInternal'&&this===manager){
              const kind=args[2];
              if(kind===profile.sourceEnum.GAMEPAD)++counts.actionSourceGamepad;
              else if(kind===profile.sourceEnum.KEYBOARD_SIMULATOR)++counts.actionSourceKeyboard;
              else if([profile.sourceEnum.MOUSE,profile.sourceEnum.TOUCH,profile.sourceEnum.LPAD,profile.sourceEnum.RPAD].includes(kind))++counts.actionSourceOther;
              else ++counts.actionSourceUnknown;
              if(status==='observing'){frame={parent:actionFrame,epoch:actionEpoch};actionFrame=frame;}
            }
          }catch{try{stop('observation-unavailable');}catch{}}
          let value;
          try{
          try{value=Reflect.apply(original,this,args);}
          catch(error){try{if(observed)++counts.methodThrew;live();}catch{}throw error;}
          // Diagnostic result inspection is isolated from the original return.
          // Never await/assimilate it, read then, or replace an original throw.
          try{
            if(observed){
              ++counts.methodReturned;
              if(effectiveLabel==='managerTarget'&&this===manager){
                try{
                  if(!Array.isArray(value))++counts.targetUnknown;
                  else{
                    if(value[1]===context)++counts.targetHeldContext;else ++counts.targetOtherContext;
                    const target=value[0];
                    if(target==null)++counts.targetUnknown;
                    else if(docs.includes(target.ownerDocument))++counts.targetMenuDocument;
                    else ++counts.targetOtherDocument;
                  }
                }catch{++counts.targetUnknown;} // e.g. a throwing result getter.
              }
            }
            live();
          }catch{}
          return value;
          }finally{
            // Nested action returns/throws cannot leak a ticket into a read.
            try{if(frame&&actionFrame===frame)actionFrame=frame.epoch===actionEpoch?frame.parent:null;}catch{}
          }
        };
        item.installed={value:item.wrapper,writable:true,enumerable:before?.enumerable??false,configurable:true};
        hooks.push(item); // Record before defineProperty: even partial throws clean up.
        Object.defineProperty(object,key,item.installed);
        if(!same(Object.getOwnPropertyDescriptor(object,key),item.installed))throw new Error('trace-install');
        item.wasInstalled=true;
      };
      const start=()=>{
        if(attempted)return;
        attempted=true;status='installing';reason='qualifying';
        try{
          if(window.__ymccMenuGameAlias!==entry||!entry.current()||!game.current()||entry.disposed)
            throw new Error('trace-owner');
          ui=window.SteamUIStore;manager=ui?.NavigationManage)YMSTEAM"
R"YMSTEAM(r;
          if(!manager||data(ui,'m_GamepadNavigationManager')!==manager)throw new Error('trace-manager');
          getContext=method(w,'GetFocusNavContext');getActive=method(manager,'GetActiveContext');
          if(!exact(getContext,'GetFocusNavContext')||!exact(getActive,'GetActiveContext'))throw new Error('trace-profile');
          context=Reflect.apply(getContext,w,[]);contexts=data(manager,'m_rgAllContexts');
          sources=data(manager,'m_rgGamepadInputSources');
          if(!context||!Array.isArray(contexts)||contexts.length>32||!Array.isArray(sources)||sources.length>64)
            throw new Error('trace-arrays');
          sourceItems=[];const matches=[];
          for(let n=0;n<sources.length;++n){
            const d=Object.getOwnPropertyDescriptor(sources,String(n));
            if(!d||!('value'in d))throw new Error('trace-arrays');
            sourceItems.push(d.value);if(nativeSource(d.value))matches.push(d.value);
          }
          if(matches.length!==1)throw new Error('trace-source');source=matches[0];
          // Only existing held menu/navigation windows; no popup inventory.
          tree=context.m_LastActiveNavTree;treeWindow=tree?.Window;browser=w.BrowserWindow;
          if(!treeWindow||!browser||!treeWindow.document||!browser.document)throw new Error('trace-documents');
          docs=[treeWindow.document];if(browser.document!==docs[0])docs.push(browser.document);
          if(docs.length>2||docs.some(doc=>!doc||doc.defaultView!==treeWindow&&doc.defaultView!==browser))
            throw new Error('trace-documents');
          if(!ownerCurrent())throw new Error('trace-owner');
          wrap(source,'OnButtonDown','sourceDown');wrap(source,'OnButtonUp','sourceUp');
          wrap(manager,'OnButtonActionInternal','managerAction');wrap(manager,'GetEventTarget','managerTarget');
          const eventsByName=[['vgp_onbuttondown','documentVgpDown'],['vgp_onbuttonup','documentVgpUp'],
            ['touchstart','documentTouchStart'],['touchend','documentTouchEnd'],
            ['pointerdown','documentPointerDown'],['pointerup','documentPointerUp'],['mousedown','documentMouseDown']];
          for(const doc of docs){
            const add=method(doc,'addEventListener'),remove=method(doc,'removeEventListener');
            if(!add||!remove||!Function.prototype.toString.call(add).includes('[native code]')||
              !Function.prototype.toString.call(remove).includes('[native code]'))throw new Error('trace-documents');
            for(const [type,label]of eventsByName){
              const listener=()=>{try{note(label);}catch{try{stop('observation-unavailable');}catch{}}};
              const item={doc,type,listener,remove,done:false};listeners.push(item);
              Reflect.apply(add,doc,[type,listener,{capture:true,passive:true}]);
            }
          }
          if(!ownerCurrent())throw new Error('trace-owner');
          startTime=performance.now();if(!Number.isFinite(startTime)){startTime=null;throw new Error('trace-clock');}
          observedWindowMs=0;installationComplete=true;
          status='observing';reason='passive-window-open';
        }catch(error){
          // Only our fixed locally-created error message selects an enum.
          const known=['trace-owner','trace-manager','trace-profile','trace-arrays','trace-source','trace-documents','trace-clock'];
          let why='installation-unavailable';try{if(known.includes(error?.message))why=error.message;}catch{}
          stop(why);
        }
      };
      const render=()=>{
        const cleanupPending=hooks.some(item=>!item.done)||listeners.some(item=>!item.done);
        const observationState=!attempted?'not-attempted':status==='observing'?'installed-observation':'invalidated';
        return {schema:1,diagnosticOnly:true,scope:'qualified-alias-menu-window',identity,
          status:cleanupPending&&status!=='observing'?'cleanup-pending':observationState,observationState,reason,
          cleanupState:status==='observing'?'owned-observation':cleanupPending?'cleanup-pending':hooks.length||listeners.length?'returned':'not-installed',
          physicalInputProven:false,sourceEntryObserved:counts.sourceDown+counts.sourceUp>0,
          managerEntryObserved:counts.managerAction+counts.managerTarget>0,
          documentEventObserved:counts.documentVgpDown+counts.documentVgpUp+counts.documentTouchStart+
            counts.documentTouchEnd+counts.documentPointerDown+counts.documentPointerUp+counts.documentMouseDown>0,
          // Zero at these observation points never establishes non-delivery.
          deliveryEvidence:'UNKNOWN',hardwareDelivery:'UNKNOWN',zeroCountMeaning:'UNKNOWN',
          attempted,installationComplete,observedWindowMs,eventCount:events,maxEvents:32,windowLimitMs:10000,documentCount:docs.length,
          counts:{...counts},nativeRoute:nativeSnapshot(),methodHookCount:hooks.length,listenerCount:listeners.length,
          restoredMethodCount:hooks.filter(item=>item.restored).length,
          returnedMethodCount:hooks.filter(item=>item.returned).length,
          notInstalledMethodCount:hooks.filter(item=>item.notInstalled).length,
          newerMethodOwnerPreservedCount:hooks.filter(item=>item.foreign).length,
          methodCleanupPending:hooks.some(item=>!item.done),listenerCleanupPending:listeners.some(item=>!item.done)};
      };
      const snapshot=()=>{live();return render();};
      const acknowledge=record=>{if(record===receiptSlot&&record.owner===receiptOwner)receiptConsumed=true;};
      const ownsReceipt=record=>record===receiptSlot&&record.owner===receiptOwner;
      return Object.freeze({start,stop,snapshot,acknowledge,ownsReceipt,armNative,nativeCall});
    };
    const projectMenuGameInputTrace = (receiver, target, phase, opened=false) => {
      try{
        const dataTraceField=(object,key)=>{const d=object&&Object.getOwnPropertyDescriptor(object,key);return d&&'value'in d?d.value:undefined;};
        const entry=receiver?.game?.entry,d=entry&&Object.getOwnPropertyDescriptor(entry,'menuInputTrace');
        const trace=d&&'value'in d?d.value:null;
        // End before another request's original method-profile check. Never
        // misclassify our temporary GetEventTarget wrapper as a Valve update.
        if(phase==='before'){
          if(request.operation!=='menu.inspect'&&entry)delete entry.popupActivationPending;
          trace?.stop('next-menu-request');if(request.operation!=='menu.inspect')trace?.armNative?.();
        }
        if(opened)trace?.start();
        if(trace)target.inputTrace=trace.snapshot();
        // A native close/destroy can retire an alias before the next request.
        // Drain its one terminal primitive snapshot only on an existing receipt.
        const key='__ymccMenuInputTraceReceipt',slot=Object.getOwnPropertyDescriptor(window,key),record=slot&&'value'in slot?slot.value:null;
        if(record&&Object.isFrozen(record)&&Object.keys(record).length===3&&
          dataTraceField(record,'kind')==='ymcc-menu-input-trace-v1'){
          const last=dataTraceField(record,'snapshot');
          if(last&&Object.isFrozen(last)&&dataTraceField(last,'schema')===1&&dataTraceField(last,'physicalInputProven')===false){
            // Separate a previous alias from the current receiver's snapshot.
            if(!trace?.ownsReceipt(record))target.previousInputTrace=Object.freeze({...last,history:true,current:false});
            const currentSlot=Object.getOwnPropertyDescriptor(window,key);
            if(slot.writable===true&&slot.configurable===true&&slot.enumerable===false&&currentSlot&&
              currentSlot.value===record&&currentSlot.writable===true&&currentSlot.configurable===true&&currentSlot.enumerable===false){
              delete window[key];trace?.acknowledge(record);
            }
          }
        }
      }catch{} // No diagnostic failure may gate or replay a functional key.
    };

    const installMenuGameAlias = (receiver, proof) => {
      const old=window.__ymccMenuGameAlias;
      if(proof.gameid64===proof.registeredGameid64){if(old){mutated=true;old.dispose?.(true);}return null;}
      const w=receiver.window,game=receiver.game,api=SteamClient.O)YMSTEAM"
R"YMSTEAM(verlay,store=window.overlayStore,map=store?.m_mapBrowserInfo;
      const cacheCurrent=()=>{
        if(window.overlayStore!==store||store?.m_mapBrowserInfo!==map||typeof map?.values!=='function'||map.size>16)return false;
        const matches=Array.from(map.values()).filter(r=>r?.m_gameID===proof.registeredGameid64);
        if(matches.length!==1)return false;const r=matches[0];
        return r.m_unPID===proof.hostPid&&r.m_nBrowserID===proof.browserId&&r.m_unAppID===proof.registeredAppid&&r.m_eBrowserType===0&&r.m_eUIMode===4;
      };
      if(!cacheCurrent())fail('steam-menu-game-native-cache-changed');
      if(old?.inputLookupVersion===1 && old.window===w && old.logical===game.logical && old.registered===game.registered && old.current?.())return old;
      if(old){mutated=true;old.dispose?.(true);}
      const own=Object.getOwnPropertyDescriptor(api,'SetOverlayState'),destroyOwn=Object.getOwnPropertyDescriptor(w,'Destroy');
      if(typeof own?.value!=='function'||(!own.writable&&!own.configurable)||typeof w.Destroy!=='function'||
         (destroyOwn&&(!('value' in destroyOwn)||(!destroyOwn.writable&&!destroyOwn.configurable))))
        fail('steam-menu-game-alias-api-immutable');
      const original=api.SetOverlayState,originalDestroy=w.Destroy;
      const windows=window.SteamUIStore?.WindowStore;
      const lookupOwn=Object.getOwnPropertyDescriptor(windows,'GetOverlayInstance');
      const listOwn=Object.getOwnPropertyDescriptor(windows,'GetOverlayInstances');
      const lookup=windows.GetOverlayInstance,list=windows.GetOverlayInstances;
      const replaceable=d=>!d?Object.isExtensible(windows):'value' in d&&(d.writable||d.configurable);
      if(typeof lookup!=='function'||typeof list!=='function'||!replaceable(lookupOwn)||!replaceable(listOwn))
        fail('steam-menu-game-input-lookup-immutable');
      const entry={window:w,logical:game.logical,registered:game.registered,lastState:null,activated:w.MenuStore.GetOpenSideMenu()!==0,disposed:false,failed:false};
      entry.inputLookupVersion=1;
      entry.current=()=>!entry.disposed && SteamClient.Overlay===api && api.SetOverlayState===entry.wrapper &&
        window.SteamUIStore?.WindowStore===windows&&windows.GetOverlayInstance===entry.lookup&&
        windows.GetOverlayInstances===entry.list&&game.current()&&cacheCurrent();
      let inputTrace=null;
      try{inputTrace=createMenuGameInputTrace(entry,receiver,proof);entry.menuInputTrace=inputTrace;}catch{}
      const noteNative=(kind,path,self,id,value,result,threw=false)=>{
        try{inputTrace?.nativeCall(kind,path,self,id,value,result,threw);}catch{}
      };
      const logicalApp=id=>Number.isInteger(id)&&id>=-0x80000000&&id<=0xffffffff&&(id>>>0)===proof.appid;
      // Native-focus and system-key consumers use browser AppID/PID, not the
      // composition GameID. Preserve every original result and original throw.
      // Singular aliases only this exact held Host; never gamePID or PID0.
      entry.lookup=function(id,pid,...rest){
        let result;
        try{result=lookup.call(this,id,pid,...rest);}
        catch(error){noteNative('lookup','original',this===windows,id,pid,null,true);throw error;}
        noteNative('lookup','original',this===windows,id,pid,result);
        if(result!=null||this!==windows||!logicalApp(id)||pid!==proof.hostPid)return result;
        try{
          if(!entry.current()){entry.dispose(false);return result;}
          let registered;
          try{registered=lookup.call(this,proof.registeredAppid,pid,...rest);}
          catch(error){noteNative('lookup','registered-fallback',this===windows,proof.registeredAppid,pid,null,true);throw error;}
          noteNative('lookup','registered-fallback',this===windows,proof.registeredAppid,pid,registered);
          if(!entry.current()){entry.dispose(false);return result;}
          return registered===w?w:result;
        }catch{return result;}
      };
      // The plural original registered set must be exactly [w]. Never filter,
      // choose a first Host, cache a returned array or replace a nonempty set.
      entry.list=function(id,...rest){
        let result;
        try{result=list.call(this,id,...rest);}
        catch(error){noteNative('list','original',this===windows,id,null,null,true);throw error;}
        noteNative('list','original',this===windows,id,null,result);
        if(!Array.isArray(result)||result.length!==0||this!==windows||!logicalApp(id))return result;
        try{
          if(!entry.current()){entry.dispose(false);return result;}
          let registered;
          try{registered=list.call(this,proof.registeredAppid,...rest);}
          catch(error){noteNative('list','registered-fallback',this===windows,proof.registeredAppid,null,null,true);throw error;}
          noteNative('list','registered-fallback',this===windows,proof.registeredAppid,null,registered);
          if(!entry.current()){entry.dispose(false);return result;}
          return Array.isArray(registered)&&registered.length===1&&registered[0]===w?registered:result;
        }catch{return result;}
      };
      entry.submit=state=>{
        if(!entry.current()||![0,2].includes(state))return false;
        if(entry.failed||entry.lastState===state)return false;
        try{const result=original.call(api,entry.registered,state);noteNative('state','alias-submit',true,entry.registered,state,result);entry.lastState=state;if(state>0)entry.activated=true;return true;}
        catch{noteNative('state','alias-submit',true,entry.registered,state,null,true);entry.failed=true;return false;}
      };
      entry.dispose=release=>{
        delete entry.popupActivationPending;
        try{inputTrace?.stop('alias-dispose');}catch{} // Also retry cleanup on repeated dispose.
        if(entry.disposed)return;entry.disposed=true;
        // Release only this still-unique registered host. Overview/running may
        // already be retired; a new/duplicate host never inherits this write.
        try{
        if(release&&api.SetOverlayState===entry.wrapper&&entry.lastState!==null&&entry.lastState!==0&&cacheCurrent()&&
           game.registrationUncontested()&&
           window.settingsStore?.m_CMInterface?.steamid?.GetAccountID?.()===proof.account&&
           window.SteamUIStore?.WindowStore?.GetInstanceForPID?.(proof.hostPid)===w){
          try{original.call(api,entry.registered,0);}catch{}
        }
        }catch{} // Release diagnostics cannot prevent ownership cleanup.
        // Each restoration is independent and never replaces a newer owner.
        try{if(windows.GetOverlayInstance===entry.lookup){if(lookupOwn)Object.defineProperty(windows,'GetOverlayInstance',lookupOwn);else delete windows.GetOverlayInstance;}}catch{}
        try{if(windows.GetOverlayInstances===entry.list){if(listOwn)Object.defineProperty(windows,'GetOverlayInstances',listOwn);else delete windows.GetOverlayInstances;}}catch{}
        try{if(api.SetOverlayState===entry.wrapper)Object.defineProperty(api,'SetOverlayState',own);}catch{}
        try{if(w.Destroy===entry.destroy){if(destroyOwn)Object.defineProperty(w,'Destroy',destroyOwn);else delete w.Destroy;}}catch{}
        if(window.__ymccMenuGameAlias===entry)delete window.__ymccMenuGameAlias;
      };
      entry.wrapper=function(id,state,...rest){
        let result;
        try{result=original.call(this,id,state,...rest);}
        catch(error){noteNative('state','original',this===api,id,state,null,true);throw error;}
        noteNative('state','original',this===api,id,state,result);
        try{
          if(!entry.current()){entry.dispose(true);return result;}
          // Non-separate Steam windows share MainRunningAppID. Another game's
          // composition can therefore use our logical ID. Mirror only the
          // proven window's own current requested composition, never that
          // other window's close/keyboard/screensaver state. These are Valve's
          // pure original reads; no new subscription or composition request.
          if(id!==entry.logical&&id!==w.MainRunningApp?.gameid)return result;
          const menu=w.MenuStore.GetOpenSideMenu();
          // A shared Steam screensaver/opaque request may arrive af)YMSTEAM"
R"YMSTEAM(ter this
          // window's side menu has closed. End our menu-only ownership here;
          // never mirror that fullscreen state into the game's registration.
          if(menu===0&&entry.activated){entry.submit(0);entry.dispose(false);return result;}
          if(![1,2].includes(menu)||state!==2)return result;
          const cs=w.CompositionStateStore;
          if(typeof cs?.ComputeOverallMinimumRequestedComposition!=='function'||
             typeof cs.GetOverridingComposition!=='function')return result;
          const desired=cs.GetOverridingComposition()??cs.ComputeOverallMinimumRequestedComposition();
          if(state!==desired)return result;
          entry.submit(state);
        }catch{try{entry.dispose(false);}catch{}} // Alias bookkeeping never changes the original API result.
        return result;
      };
      entry.destroy=function(...args){try{entry.dispose(true);}catch{}return originalDestroy.apply(this,args);};
      mutated=true;
      try{
        Object.defineProperty(api,'SetOverlayState',{...own,value:entry.wrapper});
        if(destroyOwn)Object.defineProperty(w,'Destroy',{...destroyOwn,value:entry.destroy});else w.Destroy=entry.destroy;
        Object.defineProperty(windows,'GetOverlayInstance',lookupOwn?{...lookupOwn,value:entry.lookup}:{value:entry.lookup,writable:true,configurable:true});
        Object.defineProperty(windows,'GetOverlayInstances',listOwn?{...listOwn,value:entry.list}:{value:entry.list,writable:true,configurable:true});
        if(w.Destroy!==entry.destroy||!entry.current())throw new Error('alias-installation-changed');
      }catch{entry.dispose(false);fail('steam-menu-game-input-lookup-immutable');}
      window.__ymccMenuGameAlias=entry;
      return entry;
    };

    // The native caller pins the elected foreground game, Steam, and this exact
    // overlay host until the request ends. IDs alone never authorize routing.
    const menuGameReceiver = async (handler, proof, trace, restoreMissing = false) => {
      const uint32 = v => Number.isInteger(v) && v > 0 && v <= 0xffffffff;
      const decimal = v => typeof v === 'string' && /^[1-9][0-9]{0,19}$/.test(v) && BigInt(v) <= 0xffffffffffffffffn;
      if (!proof || proof.version !== 1 ||
          !['appid','account','gamePid','steamPid','hostPid','browserId','registeredAppid'].every(k=>uint32(proof[k])) ||
          !['gameid64','gameCreated','steamCreated','hostCreated','registeredGameid64'].every(k=>decimal(proof[k])) ||
          new Set([proof.gamePid,proof.steamPid,proof.hostPid]).size !== 3)
        fail('steam-menu-game-witness-invalid');
      const full=BigInt(proof.gameid64), appid=proof.appid, registered=BigInt(proof.registeredGameid64);
      const shortcut=full>>32n===BigInt(appid) && (full&0xffffffffn)===0x02000000n;
      const native=appid<=0xffffff && full===BigInt(appid);
      const exact=proof.registeredAppid===appid && registered===full;
      const short=shortcut && registered===BigInt(appid) &&
        (proof.registeredAppid===appid || proof.registeredAppid===(appid&0xffffff));
      const base=shortcut && proof.registeredAppid<=0xffffff && registered===BigInt(proof.registeredAppid);
      if ((!shortcut&&!native)||(!exact&&!short&&!base))fail('steam-menu-gameid-invalid');
      const ui=window.SteamUIStore,windows=ui?.WindowStore,apps=window.appStore?.m_mapApps;
      const account=window.settingsStore?.m_CMInterface?.steamid;
      const a=apps?.get?.(appid),b=apps?.get?.(appid|0),overview=a||b;
      // The registered browser ID may be a base app ID. Do not mirror menu
      // state to it if Steam is also running that app independently.
      const registrationUncontested=()=>!base||exact||short||(
        window.SteamUIStore===ui&&Array.isArray(ui?.m_runningAppIDs)&&
        !ui.m_runningAppIDs.some(id=>Number.isInteger(id)&&(id>>>0)===proof.registeredAppid));
      // Record only the first failed, already-evaluated preflight predicate.
      // Later ownership checks use the same reads without changing action stage.
      let contextFailure=null;
      const rejectedContext=(leaf,diagnose)=>{if(diagnose)contextFailure=leaf;return false;};
      const accountCurrent=(diagnose=false)=>(account!=null||rejectedContext('menu-game-account-object',diagnose)) &&
        (window.settingsStore?.m_CMInterface?.steamid===account||rejectedContext('menu-game-account-binding',diagnose)) &&
        (account.GetAccountID?.()===proof.account||rejectedContext('menu-game-account-id',diagnose));
      const overviewCurrent=(diagnose=false)=>(window.appStore?.m_mapApps===apps||rejectedContext('menu-game-app-store',diagnose)) &&
        ((apps?.get?.(appid)||apps?.get?.(appid|0))===overview||rejectedContext('menu-game-overview-binding',diagnose)) &&
        (!(apps?.get?.(appid)&&apps?.get?.(appid|0)&&apps.get(appid)!==apps.get(appid|0))||rejectedContext('menu-game-overview-alias',diagnose)) &&
        (overview?.gameid===proof.gameid64||rejectedContext('menu-game-full-id',diagnose)) &&
        (Number.isInteger(overview?.appid)||rejectedContext('menu-game-appid-type',diagnose)) &&
        ((overview.appid>>>0)===appid||rejectedContext('menu-game-appid',diagnose)) &&
        (Number.isInteger(overview.app_type)||rejectedContext('menu-game-app-type',diagnose)) &&
        ((((overview.app_type&0x40000000)!==0)===shortcut)||rejectedContext('menu-game-app-kind',diagnose)) &&
        ([1,4].includes(overview?.local_per_client_data?.display_status)||rejectedContext('menu-game-display-status',diagnose)) &&
        (Array.isArray(ui?.m_runningAppIDs)||rejectedContext('menu-game-running-list',diagnose)) &&
         (ui.m_runningAppIDs.some(id=>Number.isInteger(id)&&(id>>>0)===appid)||rejectedContext('menu-game-running-app',diagnose)) &&
         (registrationUncontested()||rejectedContext('menu-game-registered-app-running',diagnose));
      if(!accountCurrent(true)||!overviewCurrent(true)||(a&&b&&a!==b)){
        stage=contextFailure||'menu-game-original-alias';fail('steam-menu-game-context-changed');
      }
      const rows=await bounded(SteamClient.Overlay.GetOverlayBrowserInfo());
      if(!Array.isArray(rows)||rows.length>16)fail('steam-menu-game-registration-invalid');
      const same=rows.filter(r=>r?.gameID===proof.registeredGameid64);
      if(same.length!==1)fail('steam-menu-game-registration-ambiguous');
      const row=same[0];
      if(row.unPID!==proof.hostPid || row.nBrowserID!==proof.browserId || row.appID!==proof.registeredAppid || row.eBrowserType!==0 || row.eUIMode!==4)
        fail('steam-menu-game-registration-changed');
      const ownersCurrent=()=>window.SteamUIStore===ui && ui.WindowStore===windows && accountCurrent() && overviewCurrent();
      let w=windows?.GetInstanceForPID?.(proof.hostPid);
      if(w==null&&restoreMissing){
        // Steam can retain a native overlay registration and its cached row
        // without a WindowStore receiver. Use only the original per-browser
        // creation handler, never the global overlay or JS-context rebuild.
        const store=window.overlayStore, cache=store?.m_mapBrowserInfo, create=windows?.OnOverlayBrowserCreated;
        const main=windows?.GamepadUIMainWindowInstance;
        // Valve's constructor uses the live main mode, not the overlay cache's mode.
        if(ui.MainInstanceUIMode!==4||store?.m_eUIMode!==4||store.m_bVRMode!==false||!main||main.IsAnyVRWindow?.()!==false||
           windows.BHasVRMainWindow?.()!==false||ui.GetShowingLockScreen?.()!==false||typeof create!=='function'||
           !Number.isInteger(cache?.size)||cache.size<1||cache.size>16||typeof cache.values!=='function'||
           rows.filter(r=>r?.unPID===proof.hostPid).length!==1||
           ![row.nScreenWidth,row.nScreenHeight].every(x=>Number.isInteger(x)&&x>0&&x<=32768)||
           !Number.isFinite(row.flDisplayScale)||row.flDisplayScale<=0||row.flDisplayScale>8)
          fail('steam-menu-game-overlay-repair-unavailable');
        const cached=Array.from(cache.values()).filter(r=>r?.m_unPID===proof.hostPid);
        const matches=r=>r?.m_unPID===proof.hostPid&&r.m_nBrowserID===proof.browserId&&
          r.m_unAppID===proof.registeredAppid&&r.m_gameID===proof.registere)YMSTEAM"
R"YMSTEAM(dGameid64&&r.m_eBrowserType===0&&r.m_eUIMode===4;
        if(cached.length!==1||!matches(cached[0])||!ownersCurrent()||window.overlayStore!==store||
           store.m_mapBrowserInfo!==cache||store.m_eUIMode!==4||store.m_bVRMode!==false||
           windows.GamepadUIMainWindowInstance!==main||windows.OnOverlayBrowserCreated!==create||
           ui.GetShowingLockScreen?.()!==false||windows.GetInstanceForPID(proof.hostPid)!=null||ui.MainInstanceUIMode!==4)
          fail('steam-menu-game-overlay-repair-context-changed');
        const data={m_unPID:row.unPID,m_nBrowserID:row.nBrowserID,m_unAppID:row.appID,
          m_gameID:row.gameID,m_eBrowserType:row.eBrowserType,m_eUIMode:row.eUIMode};
        stage='menu-game-overlay-create';mutated=true;
        const created=create.call(windows,data,row.nScreenWidth,row.nScreenHeight,row.flDisplayScale);
        if(created!==undefined)fail('steam-menu-game-overlay-repair-uncertain');
        w=windows.GetInstanceForPID(proof.hostPid);
        if(!w)fail('steam-menu-game-overlay-repair-unconfirmed');
        trace.overlayRepair='created-missing-receiver';
      }
      const p=w?.params,info=p?.browserInfo;
      const current=()=>ownersCurrent() &&
        windows.GetInstanceForPID(proof.hostPid)===w && w?.IsGamepadUIOverlayWindow?.()===true && !w.IsAnyVRWindow?.() && !ui.GetShowingLockScreen?.() &&
        w.params===p && p.appid===proof.registeredAppid && p.gameid===proof.registeredGameid64 && p.browserInfo===info &&
        info?.m_unPID===proof.hostPid && info.m_nBrowserID===proof.browserId && info.m_unAppID===proof.registeredAppid && info.m_gameID===proof.registeredGameid64;
      if(!current()||typeof w[handler]!=='function')fail('steam-menu-game-window-changed');
      trace.version=3;trace.lookup='proven-game-overlay-selected';trace.identity=exact?'exact-gameid-confirmed':'native-registration-alias';
      const receiver={window:w,source:'steam-proven-game-overlay',game:{current,registrationUncontested,appid,registeredAppid:proof.registeredAppid,registered:proof.registeredGameid64,logical:proof.gameid64}};
      receiver.game.entry=installMenuGameAlias(receiver,proof);
      return receiver;
    };
    // Only an explicit QAM button may retire an exited game library route.
    // A proven unique foreground game/host remains the sole navigation owner.
    // Inspect, followup, sleep and healthy/exact registrations never enter here.
    const returnMenuGameToRunning = (receiver, target) => {
      const game=receiver?.game,w=receiver?.window,ui=window.SteamUIStore;
      if(!game||game.registered===game.logical)return;
      if(!game.current())fail("steam-menu-game-window-changed");
      const running=ui?.m_runningAppIDs,route=w.LocationPathName;
      if(!Array.isArray(running)||running.length!==1||!Number.isInteger(running[0])||
         (running[0]>>>0)!==game.appid||w.MainRunningApp?.gameid!==game.logical)return;
      const match=typeof route==="string"?/^\/library\/app\/([1-9][0-9]{0,9})$/.exec(route):null;
      if(!match)return;const previousId=Number(match[1]);
      if(previousId>0xffffffff||previousId===game.appid)return;
      const apps=window.appStore?.m_mapApps,a=apps?.get?.(previousId),b=apps?.get?.(previousId|0),previous=a||b;
      if((a&&b&&a!==b)||!previous||!Number.isInteger(previous.appid)||
         (previous.appid>>>0)!==previousId||previous.local_per_client_data?.display_status!==11)return;
      const cs=w.CompositionStateStore,navigate=w.NavigateToRunningApp;
      if(w.MenuStore?.GetOpenSideMenu?.()!==0||cs?.GetCompositionState?.()!==3||
         cs.GetOverridingComposition?.()!==null||w.GetShowingGlobalModal?.()!==false||
         w.m_VirtualKeyboardManager?.IsShowingVirtualKeyboard?.Value!==false||
         w.HeaderStore?.GetForceHeaderAfterResume?.()!==false||w.BCanNavigate?.()!==true||
         typeof navigate!=="function")return;
      if(!game.current()||w.LocationPathName!==route||window.appStore?.m_mapApps!==apps||
         (apps.get(previousId)||apps.get(previousId|0))!==previous||
         previous.local_per_client_data?.display_status!==11||ui.m_runningAppIDs!==running||
         running.length!==1||(running[0]>>>0)!==game.appid)return;
      const navigation={status:"original-running-route-requested",from:route,previousAppid:previousId};
      target.navigation=navigation;stage="menu-game-return";mutated=true;
      const result=navigate.call(w,true);
      // A dispatched but uncertain return must never replay navigation or keys.
      if(result&&typeof result.then==="function")fail("steam-menu-game-return-uncertain");
      if(!game.current()||w.LocationPathName!=="/apprunning"||w.MainRunningApp?.gameid!==game.logical)
        fail("steam-menu-game-return-not-confirmed");
      navigation.status="original-running-route-restored";navigation.to=w.LocationPathName;
    };
    // One explicit proven-alias menu may use Valve's original system-key
    // target preparation, without sending another key/down/up or choosing a
    // controller index. Unknown ownership keeps the original receiver path.
    const prepareMenuGameInputContext = (receiver, target) => {
      const game=receiver?.game,w=receiver?.window;
      if(!game||game.logical===game.registered)return;
      const result={schema:1,status:'not-attempted',prepared:false,contextChanged:false,
        source:'original-system-key-target',physicalInputProven:false};
      target.navigationPreparation=result;
      const dataField=(object,key)=>{const d=object&&Object.getOwnPropertyDescriptor(object,key);return d&&'value'in d?d.value:undefined;};
      let attempted=false,manager=null,hintSetter=null,ownedHint=false,operation=null;
      const skip=reason=>{result.reason=reason;};
      try{
        if(!game.current()||!game.entry?.current?.())return skip('menu-owner-unavailable');
        const ui=window.SteamUIStore;
        manager=ui?.NavigationManager;
        if(!manager||manager!==dataField(ui,'m_GamepadNavigationManager'))return skip('manager-unavailable');
        const expected={"GetEventTarget":"GetEventTarget(Zt,sr,lr=!1){let Rr=this.GetActiveContext();if(this.m_nSystemKeyAppIDHint&&!Rr?.FindNavTreeInFocusedWindow()){const mi=this.m_fnSystemKeyContextResolver?.(this.m_nSystemKeyAppIDHint);mi&&mi!=Rr&&this.BCanActivateContext(mi)&&(he(`GetEventTarget: using ${mi.LogName()} for system key AppID ${this.m_nSystemKeyAppIDHint} instead of ${Rr?.LogName()??\"no context\"}`),Rr?.ActiveWindow&&Rr.OnDeactivate(Rr.ActiveWindow),mi.OnActivate(mi.m_LastActiveFocusNavTree?.Window??mi.RootWindow),Rr=mi)}!Rr&&lr&&(Rr=this.FindAnActiveContext());let Ai=Rr?.ActiveWindow?.document.activeElement;if(Rr?.m_LastActiveNavTree){if(sr||(sr=this.m_navigationSource?.Value?.eActivationSourceType),!Rr.m_LastActiveNavTree.GetLastFocusedNode()&&(0,n.UE)(sr)&&(he(`GetEventTarget: Context ${Rr.LogName()} tree ${Rr.m_LastActiveNavTree.id} has no focused node, ${lr?\"finding one\":\"will not find one\"}`),lr&&Rr.m_LastActiveNavTree.TakeFocus(w.D$.GAMEPAD,!0),!this.BGlobalGamepadButton(Zt)))return[void 0,Rr];Rr.m_LastActiveNavTree.GetLastFocusedNode()?Ai=Rr.m_LastActiveNavTree?.GetLastFocusedNode()?.Element:he(`GetEventTarget: Context ${Rr.LogName()} tree ${Rr.m_LastActiveNavTree.id} still has no focused node - will fall back to document.activeElement`)}return[Ai,Rr]}","SetSystemKeyAppIDHint":"SetSystemKeyAppIDHint(Zt){this.m_nSystemKeyAppIDHint=Zt}","GetActiveContext":"GetActiveContext(){return this.m_ActiveContext}","BCanActivateContext":"BCanActivateContext(Zt){return!(!Zt||this.BHasVRGamepadNavigationContext()&&!Zt.BIsVR())}","GetFocusNavContext":"GetFocusNavContext(){return this.IsVRGamepadUIOverlayWindow()?l.oy.WindowStore.VRGamepadUIMainWindowInstance.GetFocusNavContext():l.oy.NavigationManager.FindContextForRootWindow(this.m_BrowserWindow)}","resolver":"sr=>this.WindowStore.GetOverlayInstances(sr).find(lr=>lr.IsGamepadUIOverlayWindow())?.GetFocusNavContext()"};
        for(const key of ['GetEventTarget','SetSystemKeyAppIDHint','GetActiveContext','BCanActivateContext']){
          if(typeof manager[key]!=='function'||Function.prototype.toString.call(manager[key])!==expected[key])
            return skip('manager-so)YMSTEAM"
R"YMSTEAM(urce-unverified');
        }
        const calls=Object.fromEntries(['GetEventTarget','SetSystemKeyAppIDHint','GetActiveContext','BCanActivateContext'].map(key=>[key,manager[key]]));
        const resolver=manager.m_fnSystemKeyContextResolver;
        if(typeof resolver!=='function'||Function.prototype.toString.call(resolver)!==expected.resolver||
           typeof w.GetFocusNavContext!=='function'||Function.prototype.toString.call(w.GetFocusNavContext)!==expected.GetFocusNavContext)
          return skip('context-source-unverified');
        const hint=Object.getOwnPropertyDescriptor(manager,'m_nSystemKeyAppIDHint');
        const catches=dataField(manager,'m_rgCatchAllGamepadInput'),contexts=dataField(manager,'m_rgAllContexts');
        const holder=window.__ymccMenuInputPreparation;
        const hintState=!hint?'missing':!('value'in hint)?'accessor':hint.writable!==true?'non-writable':
          hint.value===undefined?'undefined':hint.value===null?'null':'occupied';
        result.inputOwner={hint:hintState,catchAllCount:Array.isArray(catches)?Math.min(catches.length,33):null,
          contextCount:Array.isArray(contexts)?Math.min(contexts.length,33):null,preparationPresent:!!holder};
        if(hintState!=='undefined')return skip(hintState==='null'||hintState==='occupied'?'system-key-hint-in-use':'hint-descriptor-unavailable');
        if(!Array.isArray(catches))return skip('catchall-list-unavailable');
        if(!Array.isArray(contexts)||contexts.length>32)return skip('context-list-unavailable');
        if(holder)return skip('preparation-owner-present');
        const context=w.GetFocusNavContext();
        if(!context||context!==dataField(w,'m_FocusNavContext')||
           dataField(context,'m_controller')!==manager||dataField(context,'m_bMounted')!==true||
           contexts.filter(value=>value===context).length!==1||context.BIsVR?.()!==false||
           context.BIsGamepadInputSuppressed?.()!==false||manager.BCanActivateContext(context)!==true)
          return skip('held-context-unavailable');
        const active=manager.GetActiveContext();
        if(active===context&&context.IsActive?.Value===true){
          if(!game.current()||!game.entry.current())return skip('context-changed-before-prepare');
          result.status='already-current';result.prepared=true;return;
        }
        // Valve retains a VR catch-all even outside VR. Its presence does not
        // invalidate an already-current context. A context switch still needs
        // the original empty-list guard; never invoke or remove these callbacks.
        if(catches.length!==0)return skip('catchall-input-owner-unverified');
        if(active!=null&&(typeof active.FindNavTreeInFocusedWindow!=='function'||active.FindNavTreeInFocusedWindow()))
          return skip('existing-focused-tree-preserved');
        if(resolver.call(manager,game.appid)!==context)return skip('resolver-does-not-select-held-context');
        const hintCurrent=()=>{
          const d=Object.getOwnPropertyDescriptor(manager,'m_nSystemKeyAppIDHint');
          return !!d&&'value'in d&&d.writable===true&&d.value===(ownedHint?game.appid:undefined);
        };
        const current=()=>hintCurrent()&&(operation?window.__ymccMenuInputPreparation===operation:!window.__ymccMenuInputPreparation)&&
          window.SteamUIStore===ui&&ui.NavigationManager===manager&&
          manager.m_fnSystemKeyContextResolver===resolver&&
          Object.entries(calls).every(([key,value])=>manager[key]===value)&&
          dataField(manager,'m_rgCatchAllGamepadInput')===catches&&catches.length===0&&
          dataField(manager,'m_rgAllContexts')===contexts&&contexts.filter(value=>value===context).length===1&&
          w.GetFocusNavContext()===context&&dataField(w,'m_FocusNavContext')===context&&
          dataField(context,'m_controller')===manager&&dataField(context,'m_bMounted')===true&&context.BIsVR?.()===false&&
          context.BIsGamepadInputSuppressed?.()===false&&manager.BCanActivateContext(context)===true&&game.current()&&game.entry.current();
        if(!current()||manager.GetActiveContext()!==active)return skip('context-changed-before-prepare');
        operation={manager,context};
        window.__ymccMenuInputPreparation=operation;
        hintSetter=calls.SetSystemKeyAppIDHint;
        ownedHint=true;
        hintSetter.call(manager,game.appid);
        // No await, event emission or button handler occurs while the hint is
        // borrowed. This original method performs its own activation checks.
        attempted=true;mutated=true;stage='menu-game-input-context';
        const selected=calls.GetEventTarget.call(manager,request.menu==='main'?27:28,2,false);
        const endHint=Object.getOwnPropertyDescriptor(manager,'m_nSystemKeyAppIDHint');
        if(window.__ymccMenuInputPreparation!==operation||!endHint||!('value'in endHint)||endHint.value!==game.appid)
          fail('steam-menu-game-input-hint-owner-changed');
        hintSetter.call(manager,undefined);ownedHint=false;
        if(!current()||!Array.isArray(selected)||selected[1]!==context||
           manager.GetActiveContext()!==context||context.IsActive?.Value!==true)
          fail('steam-menu-game-input-context-not-confirmed');
        result.status='original-target-prepared';result.prepared=true;result.contextChanged=active!==context;
      }catch{
        if(attempted)fail('steam-menu-game-input-context-uncertain');
        skip('input-context-unavailable');
      }finally{
        if(ownedHint&&manager&&hintSetter){
          try{
            const hint=Object.getOwnPropertyDescriptor(manager,'m_nSystemKeyAppIDHint');
            if(window.__ymccMenuInputPreparation===operation&&hint&&'value'in hint&&hint.value===game.appid&&manager.SetSystemKeyAppIDHint===hintSetter)
              hintSetter.call(manager,undefined);
          }catch{}
        }
        if(operation&&window.__ymccMenuInputPreparation===operation)delete window.__ymccMenuInputPreparation;
      }
    };


    // Same explicit key, same held browser. The original store can use another
    // global MainRunningApp or cache a tuple before BrowserWindow is mounted.
    // Correct only that demonstrated mismatch; never force a healthy tuple,
    // alter Valve's cache, choose an input producer or replay a key.
    const syncMenuGameComposition = (receiver, browserBefore) => {
      const game=receiver?.game,w=receiver?.window;
      if(!game||game.registered===game.logical)return null;
      const result={schema:1,scope:'same-key-per-browser-composition',diagnosticOnly:false,nativeRequestSubmitted:false,inputCapabilityGranted:false,physicalInputProven:false};
      const skip=reason=>Object.assign(result,{status:reason});
      const appid=value=>Number.isInteger(value)&&value>=-0x80000000&&value<=0xffffffff&&value!==0;
      const windowId=value=>Number.isSafeInteger(value)&&value>=0;
      // Valve constructs this cache as plain own data. Read only primitive
      // descriptors and own array slots; an accessor/unknown tuple is not
      // authority and is never copied into the diagnostic receipt.
      const ownData=(object,key)=>{
        const d=object&&Object.getOwnPropertyDescriptor(object,key);
        return d&&'value'in d?d.value:undefined;
      };
      const cachedTuple=store=>{
        const cache=ownData(store,'m_eLastPushedToWebHelperCompositionState');
        const mode=ownData(cache,'eCompositionMode'),windowID=ownData(cache,'windowID');
        const queue=ownData(cache,'appidCompositionQueue');let values=null;
        if(Array.isArray(queue)){
          const length=ownData(queue,'length');
          if(Number.isInteger(length)&&length>=0&&length<=16){
            const copy=[];let valid=true;
            for(let i=0;i<length;++i){const value=ownData(queue,String(i));if(!appid(value)){valid=false;break;}copy.push(value);}
            if(valid)values=copy;
          }
        }
        return {cache,queue,mode,windowID,values};
      };

      try{
        if(!browserBefore?.known||!game.current()||!game.entry?.current?.())return skip('composition-owner-unavailable');
        const ui=window.SteamUIStore,windows=ui?.WindowStore,cs=w.Composition)YMSTEAM"
R"YMSTEAM(StateStore;
        const browser=w.BrowserWindow,info=w.params?.browserInfo,client=browser?.SteamClient,api=client?.Window;
        const set=api?.SetComposition,focus=windows?.GetAppFocusedWindowID;
        if(!browser||browser.browserInfo!==info||typeof set!=='function'||typeof focus!=='function'||
           cs?.m_Instance!==w)return skip('composition-browser-unconfirmed');
        if(browserBefore.browser!=null&&(browserBefore.browser!==browser||browserBefore.client!==client||
           browserBefore.api!==api||browserBefore.set!==set))return skip('composition-browser-owner-changed');
        // This pure original map lookup is not a focus setter or a new source.
        const focusProfile='GetAppFocusedWindowID(Zt){let sr=0;return Zt&&(sr=this.m_mapAppWindows.get(Zt)?.focusedWindowID),sr??0}';
        if(Function.prototype.toString.call(focus).replace(/\s+/g,'')!==focusProfile.replace(/\s+/g,''))
          return skip('composition-window-lookup-unqualified');
        const running=ui.m_runningAppIDs;
        if(!Array.isArray(running)||running.length>32)return skip('composition-running-list-unknown');
        const selected=running.filter(id=>appid(id)&&(id>>>0)===game.appid);
        if(selected.length!==1)return skip('composition-game-representation-ambiguous');
        const sourceId=selected[0],apps=window.appStore?.m_mapApps;
        const a=apps?.get?.(game.appid),b=apps?.get?.(game.appid|0),overview=a||b;
        const overviewAppid=overview?.appid,overviewType=overview?.app_type;
        if(!overview||(a&&b&&a!==b)||!appid(overviewAppid)||(overviewAppid>>>0)!==game.appid||
           overview.gameid!==game.logical||!Number.isInteger(overviewType)||
           (overviewType&0x40000000)===0)return skip('composition-game-overview-unconfirmed');
        const override=overview.shortcut_override_appid;
        if(override!=null&&override!==0&&!appid(override))return skip('composition-shortcut-override-unknown');
        const queue=[override||overviewAppid,769],focusedWindow=focus.call(windows,sourceId);
        if(!windowId(focusedWindow))return skip('composition-focused-window-unknown');
        const observed=cachedTuple(cs),cached=observed.cache,cachedQueue=observed.queue;
        const cachedValues=observed.values,cachedWindowID=observed.windowID;
        result.cachedComposition={scope:'original-js-composition-cache',diagnosticOnly:true,
          snapshot:'before-supplement',atomic:false,maxQueueLength:16,
          mode:Number.isInteger(observed.mode)&&observed.mode>=0&&observed.mode<=5?observed.mode:null,
          windowID:windowId(cachedWindowID)?cachedWindowID:null,
          queue:cachedValues===null?null:[...cachedValues]};
        if(cs.GetCompositionState?.()!==2||
           (cs.GetOverridingComposition?.()??cs.ComputeOverallMinimumRequestedComposition?.())!==2||
           observed.mode!==2||!windowId(cachedWindowID)||cachedValues===null)
          return skip('composition-original-state-unsettled');
        const matches=cachedWindowID===focusedWindow&&cachedValues.length===queue.length&&
          cachedValues.every((id,index)=>id===queue[index]);
        const lateBrowser=browserBefore.browser==null&&browserBefore.browser!==browser;
        Object.assign(result,{sourceTupleMatchesSelected:matches,browserMountedDuringKey:lateBrowser,
          originalQueueLength:cachedValues.length,selectedQueueLength:queue.length});
        // A matching JS cache does not confirm native input handoff for the
        // observed short registration form. One explicit opening may reassert
        // this same held browser tuple; exact/base aliases and multiple games
        // retain the original zero-extra path. No queue or cache is rewritten.
        // Reuse the primitive registration already bound to this held owner.
        // Observable params may use an accessor; final game.current() still
        // verifies its live value and the exact same parameter object.
        const shortSingleAlias=()=>running.length===1&&game.appid>0xffffff&&
          game.registered===String(game.appid)&&game.registeredAppid===(game.appid&0xffffff);
        const inputReassert=matches&&!lateBrowser&&shortSingleAlias();
        result.sameTupleInputReassert=inputReassert;
        if(matches&&!lateBrowser&&!inputReassert)return skip('composition-source-tuple-unchanged');
        const current=()=>{
          // The original focused-window getter is observable. Run it before
          // the ownership/value suffix, then finish with primitive cache slots.
          const currentWindow=focus.call(windows,sourceId);
          if(currentWindow!==focusedWindow||(inputReassert&&!shortSingleAlias())||!game.current()||!game.entry?.current?.()||
             window.SteamUIStore!==ui||ui.WindowStore!==windows||ui.m_runningAppIDs!==running||
             running.filter(id=>appid(id)&&(id>>>0)===game.appid).length!==1||
             !running.some(id=>id===sourceId)||window.appStore?.m_mapApps!==apps||
             (apps.get(game.appid)||apps.get(game.appid|0))!==overview||overview.gameid!==game.logical||
             overview.shortcut_override_appid!==override||w.CompositionStateStore!==cs||cs.m_Instance!==w||
             w.BrowserWindow!==browser||browser.browserInfo!==info||w.params.browserInfo!==info||
             browser.SteamClient!==client||client.Window!==api||api.SetComposition!==set||
             windows.GetAppFocusedWindowID!==focus||![1,2].includes(w.MenuStore.GetOpenSideMenu())||
             cs.GetCompositionState()!==2||
             (cs.GetOverridingComposition()??cs.ComputeOverallMinimumRequestedComposition())!==2||
             overview.appid!==overviewAppid||overview.app_type!==overviewType)return false;
          const settled=cachedTuple(cs);
          if(settled.cache!==cached||settled.queue!==cachedQueue||settled.mode!==2||
             settled.windowID!==cachedWindowID||settled.values===null||settled.values.length!==cachedValues.length)return false;
          for(let i=0;i<cachedValues.length;++i)if(settled.values[i]!==cachedValues[i])return false;
          return true;
        };
        if(!current())return skip('composition-owner-changed-before-submit');
        // A native call can be uncertain. Submit once; no retry, cache commit,
        // listener, continuation or cleanup write is created here.
        mutated=true;result.nativeRequestSubmitted=true;result.status=inputReassert?'per-browser-composition-reassert-submitted':'per-browser-composition-submitted';
        try{
          const reply=set.call(api,2,queue,focusedWindow);
          if(reply&&typeof reply.then==='function'){Promise.resolve(reply).catch(()=>{});result.status='per-browser-composition-uncertain';}
        }catch{result.status='per-browser-composition-uncertain';}
        if(!current())result.status='composition-owner-ended-after-submit';
        return result;
      }catch{return skip('composition-observation-unavailable');}
    };

    // Independent side menus use their own tree onActivated subscription to
    // focus the BrowserView (Valve module91780). This is a separate lifecycle
    // from the window-focus coordinator; only the held active QAM popup may
    // reassert that original activation after the measured no-registration case.
    // Exact current React/Portal data only binds the observer. No callback or native focus is added.
    const popupNativeFocusProfiles={"ye":"function Ye(nr){const{active:zt,embeddedView:ue,expanded:Qe,visible:mt,vr:Dt}=nr,$t=m.useRef(void 0),vr=(0,C.Dt)(nr.onFocusNavDeactivated),{onFocusNavActivated:Tr}=nr,Jr=m.useCallback(($r,ti)=>{for(let or=ti;!zt&&or;or=or.Parent)if(or===$r)return;Tr?.()},[zt,Tr]),Dr=m.useRef(void 0),Br=m.useRef(void 0),zr=(0,g.q3)(()=>Br.current?.BFocusWithin()??!1),pr=m.useCallback($r=>{zr&&ue&&Br.current.Node()?.GetLastFocusElement()?.scrollIntoView({behavior:\"auto\",inline:\"end\"})},[zr,ue,Br]),Wr=(0,ht.wY)(pr),kr=(0,C.Y$)(c.Ez.QuickAccess),xr=(0,He.fe)();m.useEffect(()=>{Dt||(zt?$t.current.Activate(!0):$t.current?.BIsActive()&&$t.current.Deactivate())},[zt,Dt]);const Ge=m.useCallback(()=>(Br.current.TakeFocus(),!0),[]),ie=m.useCallback(()=>(Dr.current.TakeFocus(),!0),[]),Re=(0,o.ch)()YMSTEAM"
R"YMSTEAM(),Vt=(0,g.q3)(()=>Re.BQuickAccessContentFocusRequested());m.useEffect(()=>{if(!zt||!Vt)return;Re.SetQuickAccessContentFocusRequested(!1);const $r=Dr.current?.Node();$r?.Tree.DeferredFocus.RequestFocus($r,{bFocusDescendant:!0})},[zt,Vt,Re]);const Ct=ot(\"quick-access-menu\");let Pt=(0,c.i4)();const wt=(0,ct.Eb)();Pt=(0,g.q3)(()=>Ct.some(ti=>ti.key===Pt&&!jt(ti))?Pt:Ct.length>0?Ct[0].key:void 0);const dr=(0,O.A)(\"BasicUI\",xr,Z().Container,nr.visible&&Z().Open,Dt&&Z().BottomTabs,Dt&&Z().VR,wt&&Z().OOBE,Qe&&\"QAMFriendExpanded\",Qe&&Z().Expanded,nr.showHeaderAndFooter&&Z().HeaderAndFooterVisible);let ir=m.useCallback($r=>{$r.stopPropagation(),$r.preventDefault()},[]);const Kt=m.useCallback(()=>(Ge(),!0),[Ge]),Ir=(0,n.K1)();let Hr=(0,d.Qu)();const pi=(0,h.W6)(),Ei=m.useCallback(()=>{pi.replace(a.BV.GamepadUI.Keyboard())},[pi]),yi=m.useCallback(()=>{Ir(),Ei()},[Ir,Ei]);m.useEffect(()=>{Ge()},[Pt,Ge]);const Qi=(0,C.on)(),Xi=(0,O.A)(Z().QuickAccessMenu,nr.showHeaderAndFooter&&Z().HeaderAndFooterVisible),cn=(0,O.A)(Z().Tabs,nr.showHeaderAndFooter&&Z().TabsWithFooter),ln=(0,Xt.uw)(\"frame\"),Qr=(0,e.jsxs)(K.SR,{children:[(0,e.jsx)(st,{bQuickAccessMenuVisible:mt}),(0,e.jsxs)(M.g,{children:[(0,e.jsxs)(D.Z,{id:\"QuickAccess-Menu\",className:Xi,onClick:ir,children:[(0,e.jsx)(\"div\",{className:Z().HeaderContainer,children:(0,e.jsx)(i.hB,{quickAccessHeader:!0,visible:nr.showHeaderAndFooter})}),(0,e.jsxs)(D.Z,{className:Z().Menu,children:[(0,e.jsxs)(D.Z,{className:cn,ref:Wr,navRef:Br,autoFocus:!0,focusable:!1,onCancel:Dt?void 0:Ir,onActivate:ie,onMoveRight:ie,navEntryPreferPosition:y.iU.PREFERRED_CHILD,children:[(0,e.jsx)(cr,{tabs:Ct,activeTab:Pt,vr:Dt}),Hr&&!Dt&&(0,e.jsx)(D.Z,{className:Z().KeyboardButton,focusable:!0,onActivate:yi,children:(0,e.jsx)(R.ShowKeyboard,{})})]}),(0,e.jsx)(D.Z,{className:Z().PanelOuterNav,onMoveLeft:Dt?void 0:Kt,onMoveDown:Dt?Kt:void 0,onCancelButton:Dt?Ge:void 0,navRef:Dr,children:(0,e.jsx)(tt,{tabs:Ct,activeTab:Pt,bMenuVisible:mt})})]}),nr.showHeaderAndFooter&&(0,e.jsx)(Gt.w,{className:Z().Footer,location:\"quick-access\"})]}),!Dt&&(0,e.jsx)(Je.i,{bQAMVersion:!0})]})]});return Dt?(0,e.jsx)(D.Z,{className:dr,onMoveLeft:E.ru,onMoveRight:E.ru,children:Qr}):(0,e.jsx)(f.B2,{navID:\"QuickAccess-NA\",enabled:zt||kr,navTreeRef:$t,onActivated:Jr,onDeactivated:vr,autoFocus:!0,className:dr,onCancelButton:Ge,onGlobalButtonDown:Qi,...ln,children:nr.refModalManager?(0,e.jsx)(J,{refModalManager:nr.refModalManager,refContextMenuManager:nr.refContextMenuManager,children:Qr}):Qr})}","owner":"class Id{m_viewWindow;m_rootElement;m_renderWhenReady;m_bMounted=!1;m_browserView;m_browserViewName;m_DynamicCSSObserver;constructor(){}CreateView(E,T,L){const{name:R,strVROverlayKey:P,eCreationFlags:O}=T;this.m_browserView&&this.DestroyView();let v=L?.ownerWindow?.SteamClient.Browser.GetBrowserID(),z=\"BrowserViewPopup\";R?.length>0&&(z=R),v&&(z+=\"_uid\"+v),z=Id.GetUniqueBrowserViewName(z);const p={parentPopupBrowserID:v,strVROverlayKey:P,strName:z};let{strCreateURL:C,browserView:k}=SteamClient.BrowserView.CreatePopup(p);O&&(C+=\"&createflags=\"+O);let q=window.open(C,z,\"top=0,left=0,width=540,height=700,resizable=yes,status=0,toolbar=0,menubar=0,location=0\");if(!q){console.error(`Failed to create popup, browser/CEF may be blocking popups for \"${window.location.origin}\"`);return}q.document.write('<!DOCTYPE html><html ><head><title></title></head><body  style=\"overflow: hidden\"><div id=\"browserview_target\"></div></body></html>'),q.document.title=z,q.document.close();let V=q.document.getElementById(\"browserview_target\");q.addEventListener(\"unload\",this.OnUnload),this.m_viewWindow=q,this.m_rootElement=V,this.m_browserView=k,this.m_browserViewName=z,o.Oe(this.m_viewWindow,o.yU()),this.m_renderWhenReady=new n.Q(q.document,V),this.m_renderWhenReady.SetTarget(E)}DestroyView(){this.m_browserView&&(Id.DelayedReleaseBrowserViewName(this.m_browserViewName),SteamClient.BrowserView.Destroy(this.m_browserView),this.m_browserView=void 0),this.m_DynamicCSSObserver&&(this.m_DynamicCSSObserver.disconnect(),this.m_DynamicCSSObserver=void 0)}GetBrowserView(){return this.m_browserView}GetRenderElement(){return this.m_rootElement}GetViewWindow(){return this.m_viewWindow}OnUnload(){}static m_setActiveBrowserViewNames=new Set;static GetUniqueBrowserViewName(E){let T=E,L=0;for(;Id.m_setActiveBrowserViewNames.has(T);)T=`${E}_${L++}`;return Id.m_setActiveBrowserViewNames.add(T),T}static DelayedReleaseBrowserViewName(E){window.setTimeout(()=>Id.m_setActiveBrowserViewNames.delete(E),100)}}","focus":"()=>{j?.GetBrowserView().SetFocus(!0);const T=j?.GetViewWindow();T&&y?.OnActivate(T),I(),f.ToggleSideMenu(F,!0)}","subscriber":"($r,ti)=>{for(let or=ti;!zt&&or;or=or.Parent)if(or===$r)return;Tr?.()}","parent":"function(zt){const{active:ue}=zt,[Qe,mt]=m.useState(ue),Dt=Qe||ue;m.useEffect(()=>mt(ue),[ue]);const $t=(0,z.OU)(a.BV.GamepadUI.AppRunning());(0,i.iW)(Dt&&!$t,\"QuickAccessMenuBrowserView\");const vr=(0,L.Jo)({name:\"QuickAccess\"});(0,re.kV)(vr?.GetRenderElement()?.ownerDocument.defaultView);const Jr=(0,c.i4)()==c.qE.Friends,Dr=(0,g.q3)(()=>s.LN.qamFriendsExpanded)&&Jr,[Br,zr]=m.useState(null),[pr,Wr]=m.useState(null),kr=W(pr??void 0),xr=Dr||kr,{fnOnFocusNavActivated:Ge,fnOnFocusNavDeactivated:ie}=(0,C.k6)(c.Ez.QuickAccess,vr,Br??zt.ModalManager,xr&&pr||void 0);m.useEffect(()=>{ue||(pr?.ActiveMenu?.OnCancel(),Br?.CloseLegacyModals())},[ue,Br,pr]);const Re=(0,n.K1)();(0,s.PG)(vr?.GetViewWindow());const Vt=(0,pe.u6)(),Ct=(0,g.q3)(()=>lt.O.BKeyboardIMEVisible);if((0,C.Vs)(vr,ue),!vr)return null;const Pt={nDuration:300,fnStep:Lt},wt={nDuration:200,fnStep:vt};let dr=$t?\"transparent\":\"darkblur\",ir=(0,O.A)(Z().ViewPlaceholder,$t&&Z().FullHeight,xr&&Z().Expanded,Vt&&Z().OSKActive,Ct&&Z().IMEVisible,$t&&Z().HeaderAndFooterVisible);return(0,e.jsxs)(r.H,{appearance:dr,visible:Dt,onClick:Re,children:[(0,e.jsx)(L.m4,{className:ir,browser:vr.GetBrowserView(),visible:Dt,animateIn:Pt,animateOut:wt}),(0,e.jsx)(b.b5,{ownerWindow:vr.GetViewWindow(),children:w.createPortal((0,e.jsx)(Ye,{active:ue,visible:Dt,expanded:xr,showHeaderAndFooter:$t,onFocusNavActivated:Ge,onFocusNavDeactivated:ie,refModalManager:zr,refContextMenuManager:Wr}),vr.GetRenderElement())})]})}","b2":"function B(w){const{navID:h,onActivated:F,onDeactivated:j,navTreeRef:A,enabled:b=!0,modal:f=!1,virtualFocus:M=!1,historyMode:D,children:I,parentEmbeddedNavTree:y,onGlobalButtonDown:x,disableFocusClasses:E=!1,disabledRoot:T=!1,excludeFromScrollRegionSearch:L=!1,\"flow-children\":R,...P}=w,{elemProps:O,navOptions:v,gamepadEvents:z}=(0,i.sl)(P),{refDiv:p,tree:C}=d({navID:h,parentEmbeddedNavTree:y,disabledRoot:T,enabled:b,onGlobalButtonDown:x,navTreeRef:A,virtualFocus:M,modal:f,historyMode:D,excludeFromScrollRegionSearch:L});(0,a.hL)(C.OnActivateCallbacks,F),(0,a.hL)(C.OnDeactivateCallbacks,j),(0,l.KF)(z,p,C),o.useLayoutEffect(()=>{const H=(0,n.O)(R);C.Root.SetProperties({...v,layout:H})});const k=(0,r.Ue)(p,I.props.ref);return(0,e.jsx)(g,{tree:T?null:C,disableFocusClasses:E||T,children:(0,e.jsx)(i.TJ.Provider,{value:T?null:C.Root,children:o.cloneElement(I,{id:h,\"data-react-nav-root\":h,ref:k})})})}","deckyAfter":"function (...args) {\n        let ret = patch.original.call(this, ...args);\n        ret = handler.call(this, args, ret);\n        if (options.singleShot) {\n            patch.unpatch();\n        }\n        return ret;\n    }"};
    const captureCommittedPopupActivation=(element,tree,popup,context,menuStore,callbacks,profiles)=>{
      const note={schema:1,scope:'committed-current-qam-native-focus',status:'unavailable',
        capturedActive:null,currentActive:null,nativeRequestSubmitted:false,
        inputCapabilityGranted:false,physicalInputProven:false};
      try{
        const stringify=Function.prototype.toString,own=Object.getOwnPropertyDescriptor;
        const text=fn=>typeof fn==='function'?Reflect.apply(stringify,fn,[]):null;
        let reads=0;const rows=[],byObject=new WeakMap(),protos=[];
        const data=(object,key,optional=false)=>{
          if(++reads>4096||!object||!['object','function'].includes(typeof object))throw Error('data-budget-or-shape');
          const d=own(object,key);i)YMSTEAM"
R"YMSTEAM(f(!d||!('value'in d)){if(optional&&!d)return undefined;throw Error('own-data-required');}
          let map=byObject.get(object);if(!map){map=new Map;byObject.set(object,map);}
          const previous=map.get(key);
          if(previous){if(previous.value!==d.value||previous.writable!==d.writable||previous.enumerable!==d.enumerable||previous.configurable!==d.configurable)throw Error('data-changed');}
          else{if(rows.length>=2048)throw Error('data-snapshot-budget');const r={object,key,...d};map.set(key,r);rows.push(r);}
          return d.value;
        };
        const fail=status=>{note.status=status;note.reads=reads;return {note};};
        const names=Object.getOwnPropertyNames(element);if(names.length>256)return fail('element-key-budget');
        const keys=names.filter(k=>k.startsWith('__reactFiber$'));if(keys.length!==1)return fail('element-fiber-unconfirmed');
        const leaf=data(element,keys[0]),other=data(leaf,'alternate');
        let sharedParentTransitions=0,sharedReturnTransitions=0;
        const maxFiberDepth=256;
        const rootFrom=start=>{
          const seen=new Set;let at=start;
          note.rootWalkSteps=0;note.rootWalkLimit=maxFiberDepth;note.rootWalkStop='unavailable';
          for(let n=0;at&&n<maxFiberDepth;++n){
            if(seen.has(at)){note.rootWalkStop='cycle';return null;}
            seen.add(at);++note.rootWalkSteps;
            const parent=data(at,'return');
            if(parent===null){
              if(data(at,'tag')!==3){note.rootWalkStop='non-host-root';return null;}
              const owner=data(at,'stateNode'),peer=data(at,'alternate');
              if(peer&&(peer===at||data(peer,'alternate')!==at||data(peer,'tag')!==3||data(peer,'stateNode')!==owner)){
                note.rootWalkStop='root-pair-unconfirmed';return null;
              }
              const elected=data(owner,'current');
              const matched=elected===at||peer&&elected===peer;
              note.rootWalkStop=matched?'host-root-confirmed':'root-current-unconfirmed';
              return matched?owner:null;
            }
            at=parent;
          }
          note.rootWalkStop=at?'depth-budget':'missing-parent';
          return null;
        };
        const heldRoot=rootFrom(leaf);
        if(!heldRoot)return fail('held-react-root-unconfirmed');
        let pathSearchIncomplete=false;
        const pathFrom=start=>{
          if(!start||data(start,'stateNode')!==element)return [];
          const found=[];
          const visit=(at,path,seen)=>{
            if(!at||found.length>1)return;
            if(seen.has(at)||path.length>=maxFiberDepth){pathSearchIncomplete=true;return;}
            const nextPath=[...path,at],nextSeen=new Set(seen);nextSeen.add(at);
            const parent=data(at,'return');
            if(parent===null){
              if(data(at,'tag')===3&&data(at,'stateNode')===heldRoot&&data(heldRoot,'current')===at)found.push(nextPath);
              return;
            }
            const other=data(at,'alternate'),child=data(parent,'child');
            const scan=head=>{
              let item=head,matches=0,otherMatches=0;const siblings=new Set;
              for(let n=0;item&&n<128;++n){
                if(siblings.has(item)){pathSearchIncomplete=true;return null;}siblings.add(item);
                if(item===at)++matches;if(other&&item===other)++otherMatches;
                item=data(item,'sibling');
              }
              if(item){pathSearchIncomplete=true;return null;}
              return {matches,otherMatches};
            };
            const original=scan(child);if(!original)return;
            const parents=original.matches===1?[parent]:[],peer=data(parent,'alternate');
            if(peer&&peer!==parent&&data(peer,'alternate')===parent){
              const peerChild=data(peer,'child');
              // A shared list may leave child.return naming the old parent.
              if(child!==null&&peerChild===child&&original.matches===1){
                parents.push(peer);++sharedParentTransitions;
              }else if(child!==peerChild&&other&&other!==at&&
                       data(other,'alternate')===at&&data(other,'return')===parent){
                // Common-return pairs must have exclusive complementary
                // child lists. Both seed orientations use the same rule.
                const alternate=scan(peerChild);
                if(!alternate)return;
                const originalOwns=original.matches===1&&original.otherMatches===0&&alternate.matches===0&&alternate.otherMatches===1;
                const peerOwns=original.matches===0&&original.otherMatches===1&&alternate.matches===1&&alternate.otherMatches===0;
                if(!originalOwns&&!peerOwns)return;
                if(peerOwns){parents.push(peer);++sharedReturnTransitions;}
              }
            }
            for(const value of parents)visit(value,nextPath,nextSeen);
          };
          visit(start,[],new Set);return found;
        };
        const paths=[];
        for(const value of [leaf,other])for(const path of pathFrom(value)){
          if(!paths.some(previous=>previous.length===path.length&&previous.every((node,i)=>node===path[i])))paths.push(path);
        }
        note.currentPathCount=Math.min(3,paths.length);
        note.currentPathDepth=paths.length===1?paths[0].length:null;
        note.sharedParentTransitions=Math.min(maxFiberDepth,sharedParentTransitions);
        note.sharedReturnTransitions=Math.min(maxFiberDepth,sharedReturnTransitions);
        note.pathSearchIncomplete=pathSearchIncomplete;
        if(pathSearchIncomplete||paths.length!==1)return fail('committed-component-unconfirmed');
        // Only a fixed official afterPatch function with a reciprocal own-data
        // Patch record may lead to the exact official component. Never call
        // custom toString, __deckyOrig, a render function or a patch handler.
        const componentDepth=(node,expected)=>{
          let type=data(node,'type');const seen=new Set,head=type;
          for(let depth=0;typeof type==='function'&&depth<=4;++depth){
            if(seen.has(type))return null;seen.add(type);
            const source=text(type);if(source===expected)return depth;
            if(depth===4||source!==profiles.deckyAfter)return null;
            const patch=data(type,'__deckyPatch',true);
            if(!patch||data(patch,'patchedFunction')!==type||data(patch,'hasUnpatched')!==false||
               data(patch,'property')!=='type')return null;
            const object=data(patch,'object'),slot=data(object,'type');
            if(slot!==type&&slot!==head)return null;
            const original=data(patch,'original');
            if(typeof original!=='function'||original===type)return null;
            type=original;
          }
          return null;
        };
        const choices=[];
        for(const path of paths)for(let index=0;index<path.length;++index){
          const depth=componentDepth(path[index],profiles.ye);
          if(depth!==null&&!choices.some(x=>x.fiber===path[index]))choices.push({fiber:path[index],path,index,depth});
        }
        note.componentMatchCount=Math.min(3,choices.length);
        if(choices.length!==1)return fail('committed-component-unconfirmed');
        const {fiber,path,index,depth}=choices[0],props=data(fiber,'memoizedProps');
        note.componentWrapperDepth=depth;
        const active=data(props,'active'),focus=data(props,'onFocusNavActivated');note.currentActive=typeof active==='boolean'?active:null;
        if(active!==true||text(focus)!==profiles.focus)return fail('committed-component-not-active');
        const hookRows=node=>{
          const found=[],seen=new Set;let hook=data(node,'memoizedState');
          for(let n=0;hook&&n<128;++n){
            if(seen.has(hook))throw Error('hook-cycle');seen.add(hook);
            const value=data(hook,'memoizedState');
            if(Array.isArray(value)&&data(value,'length')===2){
              const fn=data(value,'0'),deps=data(value,'1');
              if(typeof fn==='function'&&Array.isArray(deps))found.push({fn,deps});
            }
            hook=data(hook,'next');
          }
        )YMSTEAM"
R"YMSTEAM(  if(hook)throw Error('hook-budget');return found;
        };
        const currentHooks=hookRows(fiber).filter(h=>text(h.fn)===profiles.subscriber&&data(h.deps,'length')===2&&data(h.deps,'0')===true&&data(h.deps,'1')===focus);
        if(currentHooks.length!==1)return fail('committed-subscriber-unconfirmed');
        const currentCallback=currentHooks[0].fn,list=data(callbacks,'m_vecCallbacks');
        if(!Array.isArray(list)||data(list,'length')>64)return fail('registered-list-unconfirmed');
        const registered=[];for(let i=0;i<list.length;++i){const fn=data(list,String(i));if(typeof fn!=='function')return fail('registered-entry-unconfirmed');if(text(fn)===profiles.subscriber)registered.push(fn);}
        if(registered.length!==1)return fail('registered-subscriber-ambiguous');
        note.registeredIsCurrent=registered[0]===currentCallback;
        const b2=path.slice(0,index).filter(node=>typeof data(node,'type')==='function'&&text(data(node,'type'))===profiles.b2);
        if(b2.length!==1)return fail('current-navigation-component-unconfirmed');
        const b2Props=data(b2[0],'memoizedProps');
        if(data(b2Props,'onActivated')!==currentCallback||data(b2Props,'navID')!=='QuickAccess-NA'||data(data(b2Props,'navTreeRef'),'current')!==tree)return fail('current-navigation-binding-unconfirmed');
        const portals=path.slice(index+1).filter(node=>data(node,'tag')===4);if(portals.length!==1)return fail('popup-portal-unconfirmed');
        const portal=portals[0],portalState=data(portal,'stateNode'),container=data(portalState,'containerInfo');
        const bindings=[];
        for(const node of path.slice(path.indexOf(portal)+1)){
          if(componentDepth(node,profiles.parent)===null||![0,15].includes(data(node,'tag')))continue;
          for(const h of hookRows(node))if(h.fn===focus&&data(h.deps,'length')===5)bindings.push(h);
        }
        if(bindings.length!==1)return fail('focus-hook-owner-unconfirmed');
        const deps=bindings[0].deps,store=data(deps,'0'),menu=data(deps,'1'),owner=data(deps,'2'),tail=data(deps,'3'),nav=data(deps,'4');
        if(store!==menuStore||menu!==2||nav!==context||typeof tail!=='function'||!owner)return fail('focus-hook-context-mismatch');
        const proto=Object.getPrototypeOf(owner);protos.push([owner,proto]);
        const ctor=data(proto,'constructor');
        if(typeof ctor!=='function'||text(ctor)!==profiles.owner||data(ctor,'prototype')!==proto)return fail('popup-holder-profile-unconfirmed');
        for(const [name,expected]of [['GetViewWindow','GetViewWindow(){return this.m_viewWindow}'],['GetRenderElement','GetRenderElement(){return this.m_rootElement}'],['GetBrowserView','GetBrowserView(){return this.m_browserView}']]){
          if(own(owner,name)||text(data(proto,name))!==expected)return fail('popup-holder-method-unconfirmed');
        }
        const native=data(owner,'m_browserView');let nativeFocus,methodOwner=native;
        for(let i=0;methodOwner&&i<4;++i){
          const descriptor=own(methodOwner,'SetFocus');
          if(descriptor){if(!('value'in descriptor))return fail('native-focus-accessor-unconfirmed');nativeFocus=data(methodOwner,'SetFocus');break;}
          const parent=Object.getPrototypeOf(methodOwner);protos.push([methodOwner,parent]);methodOwner=parent;
        }
        if(data(owner,'m_viewWindow')!==popup||data(owner,'m_rootElement')!==container||typeof nativeFocus!=='function'||!text(nativeFocus).includes('[native code]'))return fail('popup-holder-target-unconfirmed');
        const nodeProto=popup.Node?.prototype,contains=own(nodeProto,'contains')?.value,documentGet=own(nodeProto,'ownerDocument')?.get,connected=own(nodeProto,'isConnected')?.get;
        if([contains,documentGet,connected].some(fn=>typeof fn!=='function'||!text(fn).includes('[native code]')))return fail('native-dom-source-unconfirmed');
        const document=popup.document;
        const dom=()=>Reflect.apply(documentGet,container,[])===document&&Reflect.apply(documentGet,element,[])===document&&Reflect.apply(connected,container,[])===true&&Reflect.apply(connected,element,[])===true&&Reflect.apply(contains,container,[element])===true;
        if(!dom())return fail('popup-dom-unconfirmed');
        note.status='committed-popup-holder-confirmed';note.reads=reads;
        const current=()=>{
          try{
            if(Function.prototype.toString!==stringify||Object.getOwnPropertyNames(element).filter(k=>k.startsWith('__reactFiber$')).length!==1)return false;
            for(const [object,proto]of protos)if(Object.getPrototypeOf(object)!==proto)return false;
            for(const r of rows){const d=own(r.object,r.key);if(!d||!('value'in d)||d.value!==r.value||d.writable!==r.writable||d.enumerable!==r.enumerable||d.configurable!==r.configurable)return false;}
            return true;
          }catch{return false;}
        };
        return {note,current,refresh:dom,view:native,methodOwner,nativeFocus};
      }catch{return {note:{...note,status:'source-unconfirmed'}};}
    };
    
    const observeMenuPopupNativeFocus=binding=>{
      const note={status:'unavailable',calls:0,trueCalls:0,falseCalls:0,foreignCalls:0,returned:0,threw:0,restored:false,
        nativeInputAcceptance:'UNKNOWN',inputCapabilityGranted:false,physicalInputProven:false};
      binding.note.nativeFocus=note;
      try{
        if(typeof binding.current!=='function'||!binding.current())return ()=>{};
        const object=binding.view,original=binding.nativeFocus,own=Object.getOwnPropertyDescriptor(object,'SetFocus');
        const descriptor=own??Object.getOwnPropertyDescriptor(binding.methodOwner,'SetFocus');
        if(!descriptor||!('value'in descriptor)||descriptor.value!==original||
           (own?!(own.configurable||own.writable):!Object.isExtensible(object))){note.status='slot-not-reversible';return ()=>{};}
        const state={active:true,object,note};
        const observer=function(){
          'use strict';
          const args=arguments;
          if(!state.active)return Reflect.apply(original,this,args);
          const n=state.note,matched=this===state.object&&args.length===1&&typeof args[0]==='boolean';
          if(matched){n.calls=Math.min(16,n.calls+1);const key=args[0]?'trueCalls':'falseCalls';n[key]=Math.min(16,n[key]+1);}
          else n.foreignCalls=Math.min(16,n.foreignCalls+1);
          try{const value=Reflect.apply(original,this,args);if(matched&&state.active)n.returned=Math.min(16,n.returned+1);return value;}
          catch(error){if(matched&&state.active)n.threw=Math.min(16,n.threw+1);throw error;}
        };
        const installed=own?{...own,value:observer}:{value:observer,writable:true,configurable:true,enumerable:false};
        Object.defineProperty(object,'SetFocus',installed);note.status='observing-original-native-focus';
        return ()=>{
          state.active=false;state.object=null;state.note=null;
          try{
            const now=Object.getOwnPropertyDescriptor(object,'SetFocus');
            if(!now||!('value'in now)||now.value!==observer){note.status='slot-replaced-during-callback';return;}
            if(now.writable!==installed.writable||now.enumerable!==installed.enumerable||now.configurable!==installed.configurable){note.status='slot-descriptor-changed-during-callback';return;}
            if(own)Object.defineProperty(object,'SetFocus',own);
            else if(!Reflect.deleteProperty(object,'SetFocus')){note.status='slot-restore-failed';return;}
            note.restored=true;note.status=binding.current()?'original-native-focus-scope-observed':'holder-changed-after-observation';
          }catch{note.status='slot-restore-failed';}
        };
      }catch{note.status='observation-unavailable';return ()=>{};}
    };

    const reassertMenuGamePopupActivation = (receiver, target, focusResult, pending=null) => {
      const game=receiver?.game,w=receiver?.window;
      if(request.menu!=='quick-access')return;
      const result={schema:1,scope:'held-active-qam-popup-original-activation',
        status:'not-attempted',callbackSubmitted:false,inputCapabilityGranted:false,physicalInputProven:false};
      focusResult.menuTreeActivation=result;
      co)YMSTEAM"
R"YMSTEAM(nst skip=reason=>{result.reason=reason;};
      const data=(object,key)=>{const d=object&&Object.getOwnPropertyDescriptor(object,key);return d&&'value'in d?d.value:undefined;};
      const text=fn=>typeof fn==='function'?Function.prototype.toString.call(fn):null;
      const methods={"OnActivate":"OnActivate(Zt){this.m_tsLastActivated=performance.now(),this.m_onActivateCallbacks.Dispatch(this,Zt),this.m_onActiveFocusStateChangedCallbacks.Dispatch(!0,this);for(let sr=this.m_lastFocusNode;sr;sr=sr.Parent)sr.SetFocusWithin(!0)}","BIsActive":"BIsActive(){return this.m_valueIsMounted.Value&&(this.m_Controller.IsActiveNavTree(this)||this.m_Controller.IsActiveFocusNavTree(this))}","BIsContextActive":"BIsContextActive(){return this.m_Controller.BIsInActiveContext(this)}","BIsEnabled":"BIsEnabled(){return this.m_bIsEnabled}"};
      try{
        const ui=window.SteamUIStore,manager=ui?.NavigationManager,browser=w.BrowserWindow,info=w.params?.browserInfo;
        const context=data(w,'m_FocusNavContext'),tree=data(context,'m_LastActiveFocusNavTree');
        const id=data(tree,'m_ID');result.treeId=typeof id==='string'&&id.length<=96?id:null;
        if(pending){
          result.activationPhase='native-followup';
          const now=performance.now();
          if(!Number.isFinite(now)||!Number.isFinite(pending.startedAt)||now<pending.startedAt||now-pending.startedAt>=4000)
            return skip('popup-task-window-expired');
          if(pending.tree!==tree||pending.context!==context||pending.browser!==browser||pending.popup!==data(tree,'m_window'))
            return skip('popup-ticket-tree-changed');
        }
        if(id!=='QuickAccess-NA')return skip('current-tree-is-not-qam-popup');
        const proto=Object.getPrototypeOf(tree),activate=Object.getOwnPropertyDescriptor(proto,'OnActivate')?.value;
        const treeProfile=()=>{
          for(const [name,expected]of Object.entries(methods)){
            const descriptor=Object.getOwnPropertyDescriptor(proto,name);
            if(Object.getOwnPropertyDescriptor(tree,name)||!descriptor||!('value'in descriptor)||text(descriptor.value)!==expected)return false;
          }
          return true;
        };
        if(!treeProfile())return skip('popup-tree-profile-unconfirmed');
        const popup=data(tree,'m_window'),root=data(tree,'m_Root'),element=data(root,'m_element');
        const document=popup?.document,trees=data(context,'m_rgGamepadNavigationTrees');
        const mounted=data(tree,'m_valueIsMounted'),nodeProto=popup?.Node?.prototype;
        const connectedDescriptor=nodeProto&&Object.getOwnPropertyDescriptor(nodeProto,'isConnected');
        const nativeConnected=connectedDescriptor?.get;
        const callbacks=data(tree,'m_onActivateCallbacks'),stateCallbacks=data(tree,'m_onActiveFocusStateChangedCallbacks');
        if(!context||!manager||!browser||browser.browserInfo!==info||!popup||popup===browser||
           !document||document.defaultView!==popup||!element||element.ownerDocument!==document||
           data(root,'m_Tree')!==tree||data(element,'__nav_tree')!==tree||
           !Array.isArray(trees)||trees.length>128||trees.filter(value=>value===tree).length!==1||
           !mounted||typeof nativeConnected!=='function'||
           !callbacks||typeof callbacks.Dispatch!=='function'||!stateCallbacks||typeof stateCallbacks.Dispatch!=='function')
          return skip('held-popup-tree-mount-unconfirmed');
        const dispatch=callbacks.Dispatch,stateDispatch=stateCallbacks.Dispatch;
        const current=()=>{
          // Complete original observable calls before the held mount/owner
          // suffix. A late composition or game-current read may revoke it.
          const isActive=tree.BIsActive(),contextActive=tree.BIsContextActive(),enabled=tree.BIsEnabled();
          const connected=element.isConnected,activeContext=manager.GetActiveContext();
          const rootWindow=context.RootWindow,activeWindow=context.ActiveWindow;
          const menu=w.MenuStore.GetOpenSideMenu(),composition=w.CompositionStateStore?.GetCompositionState?.();
          const validGame=game.current()&&game.entry?.current?.(),running=ui.m_runningAppIDs;
          let failed=null;
          const guard=(code,value)=>{if(!value&&failed===null)failed=code;return value;};
          const accepted=guard('game-current',validGame)&&
            guard('tree-active',isActive===true)&&
            guard('context-active',contextActive===true)&&
            guard('tree-enabled',enabled===true)&&
            guard('dom-connected',connected===true)&&
            guard('menu-open',menu===2)&&
            guard('composition-open',composition===2)&&
            guard('active-context',activeContext===context)&&
            guard('root-window',rootWindow===browser)&&
            guard('active-popup',activeWindow===popup)&&
            guard('tree-profile',treeProfile())&&
            guard('activate-method',tree.OnActivate===activate)&&
            guard('ui-owner',window.SteamUIStore===ui)&&
            guard('manager-owner',ui.NavigationManager===manager)&&
            guard('context-slot',data(w,'m_FocusNavContext')===context)&&
            guard('root-slot',data(context,'m_rootWindow')===browser)&&
            guard('active-slot',data(context,'m_activeWindow')===popup)&&
            guard('focus-tree-slot',data(context,'m_LastActiveFocusNavTree')===tree)&&
            guard('nav-tree-slot',data(context,'m_LastActiveNavTree')===tree)&&
            guard('tree-list-owner',data(context,'m_rgGamepadNavigationTrees')===trees)&&
            guard('unique-tree',trees.filter(value=>value===tree).length===1)&&
            guard('tree-id',data(tree,'m_ID')===id)&&
            guard('tree-controller',data(tree,'m_Controller')===manager)&&
            guard('tree-context',data(tree,'m_context')===context)&&
            guard('tree-popup',data(tree,'m_window')===popup)&&
            guard('tree-root',data(tree,'m_Root')===root)&&
            guard('root-element',data(root,'m_element')===element)&&
            guard('root-tree',data(root,'m_Tree')===tree)&&
            guard('element-tree',data(element,'__nav_tree')===tree)&&
            guard('element-document',element.ownerDocument===document)&&
            guard('popup-document',popup.document===document)&&
            guard('document-window',document.defaultView===popup)&&
            guard('activation-listeners',data(tree,'m_onActivateCallbacks')===callbacks)&&
            guard('activation-dispatch',callbacks.Dispatch===dispatch)&&
            guard('state-listeners',data(tree,'m_onActiveFocusStateChangedCallbacks')===stateCallbacks)&&
            guard('state-dispatch',stateCallbacks.Dispatch===stateDispatch)&&
            guard('browser-owner',w.BrowserWindow===browser)&&
            guard('browser-info',browser.browserInfo===info)&&
            guard('params-browser-info',w.params.browserInfo===info)&&
            guard('running-list-owner',ui.m_runningAppIDs===running)&&
            guard('running-list-array',Array.isArray(running))&&
            guard('single-game',running.length===1)&&
            guard('running-game',(running[0]>>>0)===game.appid)&&
            guard('registered-game',game.registered===String(game.appid))&&
            guard('registered-app',game.registeredAppid===(game.appid&0xffffff))&&
            guard('enabled-slot',data(tree,'m_bIsEnabled')===true)&&
            guard('mounted-owner',data(tree,'m_valueIsMounted')===mounted)&&
            guard('mounted-value',mounted.Value===true)&&
            guard('node-prototype',popup.Node?.prototype===nodeProto)&&
            guard('connected-getter',Object.getOwnPropertyDescriptor(nodeProto,'isConnected')?.get===nativeConnected)&&
            guard('final-connected',nativeConnected.call(element)===true);
          if(failed!==null)result.currentFailure=failed;
          return accepted;
        };
        if(!current())return skip('popup-tree-owner-changed-before-activation');
        if(!pending){
          const entry=game.entry,startedAt=performance.now();
          if(!entry||entry.disposed||window.__ymccMenuGameAlias!==entry||!Number.isFinite(startedAt))
            return skip('popup-ticke)YMSTEAM"
R"YMSTEAM(t-owner-unavailable');
          const heldReceiver=Object.freeze({window:w,source:receiver.source,game:Object.freeze({...game})});
          entry.popupActivationPending=Object.freeze({id:token,receiver:heldReceiver,
            proof:Object.freeze({...request.menuGame}),tree,context,browser,popup,startedAt});
          result.status='pending-browser-task';result.ticket=token;result.activationPhase='native-pending';
          return;
        }
        // Observe only the held context's original synchronous suffix. This is
        // not an acknowledgement of BrowserView focus or physical input.
        const nativeBinding=captureCommittedPopupActivation(element,tree,popup,context,w.MenuStore,callbacks,popupNativeFocusProfiles);
        result.nativeFocusObservation=nativeBinding.note;
        try{if(nativeBinding.refresh&&!nativeBinding.refresh())nativeBinding.current=()=>false;}catch{nativeBinding.current=()=>false;}
        if(!current())return skip('popup-tree-owner-changed-before-native-observation');
        const suffix={status:'unknown',calls:0,returned:0,threw:0,foreignCalls:0,restored:false};
        result.contextActivationObservation=suffix;

        suffix.subscriptionStatus='unknown';suffix.callbackCount=null;suffix.originalQamCallbacks=null;
        try{
          const dispatcherPrototype=Object.getPrototypeOf(callbacks),d=Object.getOwnPropertyDescriptor(dispatcherPrototype,'Dispatch');
          const list=data(callbacks,'m_vecCallbacks');
          if(!Object.getOwnPropertyDescriptor(callbacks,'Dispatch')&&d&&'value'in d&&text(d.value)==="Dispatch(...s){for(const c of Array.from(this.m_vecCallbacks))c(...s)}"&&Array.isArray(list)&&list.length<=64){
            let matches=0,known=true;
            for(let index=0;index<list.length;++index){
              const item=Object.getOwnPropertyDescriptor(list,String(index));
              if(!item||!('value'in item)||typeof item.value!=='function'){known=false;break;}
              if(text(item.value)==="($r,ti)=>{for(let or=ti;!zt&&or;or=or.Parent)if(or===$r)return;Tr?.()}")++matches;
            }
            if(known){suffix.subscriptionStatus='profiled-held-list';suffix.callbackCount=list.length;suffix.originalQamCallbacks=matches;}
          }
        }catch{}
        // Phase observation wraps only these held original Dispatch methods.
        // Callback entries and their registration identities stay untouched.
        const phaseState={active:true,phase:'unclassified'};
        suffix.contextCallPhases={'activation-dispatch':0,'active-state-dispatch':0,'outside-dispatch':0,unclassified:0};
        suffix.dispatchScopes={};
        const phaseRestores=[];
        const observeDispatch=(object,label,first,second)=>{
          const note={status:'unknown',calls:0,returned:0,threw:0,foreignCalls:0,restored:false};
          suffix.dispatchScopes[label]=note;
          try{
            const own=Object.getOwnPropertyDescriptor(object,'Dispatch'),owner=own?object:Object.getPrototypeOf(object);
            const descriptor=own??(owner&&Object.getOwnPropertyDescriptor(owner,'Dispatch'));
            if(!descriptor||!('value'in descriptor)||text(descriptor.value)!=="Dispatch(...s){for(const c of Array.from(this.m_vecCallbacks))c(...s)}"){
              note.status='method-profile-unknown';return false;
            }
            if(own?!(own.configurable||own.writable):!Object.isExtensible(object)){
              note.status='slot-not-reversible';return false;
            }
            const original=descriptor.value,state={active:true,object,first,second,note,phaseState,label};
            const observer=function(){
              'use strict';
              const args=arguments;
              if(!state.active||!state.phaseState.active)return Reflect.apply(original,this,args);
              const observation=state.note,phase=state.phaseState,previous=phase.phase;
              const matched=this===state.object&&args.length===2&&args[0]===state.first&&args[1]===state.second;
              if(matched)observation.calls=Math.min(16,observation.calls+1);
              else observation.foreignCalls=Math.min(16,observation.foreignCalls+1);
              phase.phase=matched?state.label:'unclassified';
              try{
                const value=Reflect.apply(original,this,args);
                if(matched&&state.active)observation.returned=Math.min(16,observation.returned+1);
                return value;
              }catch(error){if(matched&&state.active)observation.threw=Math.min(16,observation.threw+1);throw error;}
              finally{if(phase.active)phase.phase=previous;}
            };
            const installed=own?{...own,value:observer}:{value:observer,writable:true,configurable:true,enumerable:false};
            Object.defineProperty(object,'Dispatch',installed);
            note.status='observing-original-dispatch';
            phaseRestores.push(()=>{
              state.active=false;state.object=null;state.first=null;state.second=null;state.note=null;state.phaseState=null;
              try{
                const now=Object.getOwnPropertyDescriptor(object,'Dispatch');
                if(!now||!('value'in now)||now.value!==observer){note.status='slot-replaced-during-callback';return;}
                if(now.writable!==installed.writable||now.enumerable!==installed.enumerable||now.configurable!==installed.configurable){
                  note.status='slot-descriptor-changed-during-callback';return;
                }
                if(own)Object.defineProperty(object,'Dispatch',own);
                else if(!Reflect.deleteProperty(object,'Dispatch')){note.status='slot-restore-failed';return;}
                note.restored=true;
                note.status=!own&&(Object.getPrototypeOf(object)!==owner||Object.getOwnPropertyDescriptor(owner,'Dispatch')?.value!==original)?
                  'prototype-source-changed-after-observation':'original-dispatch-scope-observed';
              }catch{note.status='slot-restore-failed';}
            });
            return true;
          }catch{note.status='observation-unavailable';return false;}
        };
        const activationObserved=observeDispatch(callbacks,'activation-dispatch',tree,tree);
        const stateObserved=observeDispatch(stateCallbacks,'active-state-dispatch',true,tree);
        if(activationObserved&&stateObserved)phaseState.phase='outside-dispatch';
        let restoreSuffix=()=>{};
        try{
          const expected="OnActivate(Zt){this.BIsActive()&&this.m_activeWindow==Zt&&this.m_activeBrowserView===void 0||(H(`${this.LogName(Zt)} Activating context, there are ${this.m_rgGamepadNavigationTrees.length} trees in this context`),this.SetActive(!0,Zt))}";
          const own=Object.getOwnPropertyDescriptor(context,'OnActivate');
          const owner=own?context:Object.getPrototypeOf(context);
          const descriptor=own??(owner&&Object.getOwnPropertyDescriptor(owner,'OnActivate'));
          if(!descriptor||!('value'in descriptor)||text(descriptor.value)!==expected)suffix.status='method-profile-unknown';
          else if(own?!(own.configurable||own.writable):!Object.isExtensible(context))suffix.status='slot-not-reversible';
          else{
            const original=descriptor.value,state={active:true,context,popup,suffix};
            const observer=function(){
              'use strict';
              const args=arguments;
              if(!state.active)return Reflect.apply(original,this,args);
              const note=state.suffix,matched=this===state.context&&args.length===1&&args[0]===state.popup;
              if(matched){
                note.calls=Math.min(16,note.calls+1);
                const phase=phaseState.active?phaseState.phase:'unclassified';
                if(Object.prototype.hasOwnProperty.call(note.contextCallPhases,phase))note.contextCallPhases[phase]=Math.min(16,note.contextCallPhases[phase]+1);
              }
              else note.foreignCalls=Math.min(16,note.foreignCalls+1);
              try{
                const value=Reflect.apply(original,this,args);
                if(matched&&state.active)note.returned=Math.min(16,note.returned+1);
                return value;
        )YMSTEAM"
R"YMSTEAM(      }catch(error){if(matched&&state.active)note.threw=Math.min(16,note.threw+1);throw error;}
            };
            const installed=own?{...own,value:observer}:{value:observer,writable:true,configurable:true,enumerable:false};
            Object.defineProperty(context,'OnActivate',installed);
            suffix.status='observing-original-context-suffix';
            restoreSuffix=()=>{
              state.active=false;state.context=null;state.popup=null;state.suffix=null;
              try{
                const now=Object.getOwnPropertyDescriptor(context,'OnActivate');
                if(!now||!('value'in now)||now.value!==observer){suffix.status='slot-replaced-during-callback';return;}
                if(now.writable!==installed.writable||now.enumerable!==installed.enumerable||now.configurable!==installed.configurable){suffix.status='slot-descriptor-changed-during-callback';return;}
                if(own)Object.defineProperty(context,'OnActivate',own);
                else if(!Reflect.deleteProperty(context,'OnActivate')){suffix.status='slot-restore-failed';return;}
                suffix.restored=true;suffix.status=!own&&(Object.getPrototypeOf(context)!==owner||Object.getOwnPropertyDescriptor(owner,'OnActivate')?.value!==original)?'prototype-source-changed-after-observation':'original-context-suffix-observed';
              }catch{suffix.status='slot-restore-failed';}
            };
          }
        }catch{suffix.status='observation-unavailable';}
        const restoreNativeFocus=observeMenuPopupNativeFocus(nativeBinding);
        mutated=true;result.callbackSubmitted=true;result.status='original-popup-tree-activation-submitted';
        try{
          // This tree is already the active tree; preserve that previous-tree
          // argument so an inactive React closure keeps its original ancestry filter.
          const reply=activate.call(tree,tree);
          if(reply&&typeof reply.then==='function'){
            Promise.resolve(reply).catch(()=>{});result.status='original-popup-tree-activation-uncertain';
          }
        }catch{result.status='original-popup-tree-activation-uncertain';}
        finally{
          phaseState.active=false;phaseState.phase='ended';
          restoreNativeFocus();
          restoreSuffix();
          for(let index=phaseRestores.length-1;index>=0;--index)phaseRestores[index]();
        }
        if(!current())result.status='popup-tree-owner-ended-after-activation';
      }catch{result.reason='popup-tree-observation-unavailable';}
    };

    // Resolve only this explicit action's held root and active tree window.
    // Steam may register a popup tree in a different window's original module
    // singleton. A source profile alone never authorizes another tree or owner.
    const reassertMenuGameWindowFocus = (receiver, target) => {
      const game=receiver?.game,w=receiver?.window,composition=target.composition;
      if(!game||game.registered===game.logical||composition?.sameTupleInputReassert!==true||
         composition.status!=='per-browser-composition-reassert-submitted')return;
      const result={schema:2,scope:'selected-registered-tree-original-focus',status:'not-attempted',
        callbackSubmitted:false,inputCapabilityGranted:false,physicalInputProven:false};
      target.windowFocus=result;
      const skip=reason=>{result.reason=reason;};
      const data=(object,key)=>{const d=object&&Object.getOwnPropertyDescriptor(object,key);return d&&'value'in d?d.value:undefined;};
      const text=fn=>typeof fn==='function'?Function.prototype.toString.call(fn):null;
      const expectedCoordinator="class m{m_rgTrees=[];m_mapChildTreeCleanup=new Map;FindTree(M){return this.m_rgTrees.find(D=>D.tree==M)}FindTreeByName(M){return this.m_rgTrees.find(D=>D.name==M)}FindTreeByBrowser(M){return this.m_rgTrees.find(D=>D.browser==M)}RegisterNavTree(M,D,I){(0,n.wT)(!this.FindTreeByName(M),`Duplicate focus nav tree ${M}`),S(`Registering tree ${M}`);const y=new s.e0,x={name:M,tree:D,browserContext:I};return this.m_rgTrees.push(x),y.Push(()=>i.x9(this.m_rgTrees,x)),y.Push(D.OnActivateCallbacks.Register(this.OnTreeActivated).Unregister),y.Push(D.OnDeactivateCallbacks.Register(this.OnTreeDeactivated).Unregister),y.Push(D.OnChildTreesChangedCallbacks.Register((E,T)=>this.OnChildTreesChanged(M,I,E,T)).Unregister),y.Push(()=>D.ChildTrees.forEach(E=>this.UnregisterChildTree(E))),D.BIsActive()&&this.OnTreeActivated(D),y.GetUnregisterFunc()}OnChildTreesChanged(M,D,I,y){I==\"add\"?this.RegisterChildTree(M,D,y):I==\"remove\"&&this.UnregisterChildTree(y)}RegisterChildTree(M,D,I){const y=`${M}/${I.id}`;this.m_mapChildTreeCleanup.set(I,this.RegisterNavTree(y,I,D)),I.ChildTrees.forEach(x=>this.RegisterChildTree(y,D,x))}UnregisterChildTree(M){const D=this.m_mapChildTreeCleanup.get(M);D&&D(),this.m_mapChildTreeCleanup.delete(M)}SetBrowserViewFocus(M,D){const I=this.FindTree(D);g(I?.browserContext,`CBrowserViewWindowFocusCoordinator Focusing browser view in ${I?.name}, ${I?\"blurring main window\":\"not changing main window focus\"}`),I&&(I.browser=M,A(I.browserContext)),M.SetFocus(!0)}SetBrowserViewBlurred(M,D){const I=this.FindTree(D);g(I?.browserContext,`CBrowserViewWindowFocusCoordinator Blurring browser view in ${I?.name}, ${I?\"giving main window focus\":\"not changing main window focus\"}`),M.SetFocus(!1),I&&(delete I.browser,j(I.browserContext))}GetActiveBrowserViewWithinTree(M){return this.FindTree(M)?.browser}OnTreeActivated(M){const D=this.FindTree(M);D.browser?(g(D?.browserContext,`CBrowserViewWindowFocusCoordinator Activating tree ${D?.name} but browser view has focus`),D.browser.SetFocus(!0)):(b(D?.browserContext),M.BIsContextActive()?(g(D?.browserContext,`CBrowserViewWindowFocusCoordinator Activating tree ${D?.name} and giving main window focus`),j(D.browserContext)):g(D?.browserContext,`CBrowserViewWindowFocusCoordinator tree ${D?.name} was activated, but ignoring because the window is not active`))}OnTreeDeactivated(M,D){const I=this.FindTree(M),y=this.FindTree(D);if(y){g(I?.browserContext,`CBrowserViewWindowFocusCoordinator Focus transfer from ${I?.name} to ${y?.name}, both within main window`);return}I.browser?(g(I?.browserContext,\"CBrowserViewWindowFocusCoordinator From tree had browser, blurring the browser\"),I.browser.SetFocus(!1)):(g(I?.browserContext,\"CBrowserViewWindowFocusCoordinator Blurring main window\"),A(I.browserContext))}}";
      const bindGetter='get(){let c=s.value.bind(this);return Object.prototype.hasOwnProperty.call(this,e)||Object.defineProperty(this,e,{value:c}),c}';
      const boundShape=d=>!!d&&'value'in d&&typeof d.value==='function'&&d.writable===false&&
        d.enumerable===false&&d.configurable===false&&d.value.name==='bound OnTreeActivated'&&
        d.value.length===1&&text(d.value)==='function () { [native code] }';
      try{
        const ui=window.SteamUIStore,manager=ui?.NavigationManager,browser=w.BrowserWindow,info=w.params?.browserInfo;
        const context=data(w,'m_FocusNavContext'),tree=context?.m_LastActiveFocusNavTree;
        result.heldContextPresent=!!context;result.focusTreePresent=!!tree;
        if(!context||!tree||!manager||!browser||browser.browserInfo!==info||
           context.RootWindow!==browser||manager.GetActiveContext?.()!==context||tree.WindowContext!==context)
          return skip('held-active-context-unavailable');
        const treeWindow=tree.Window;
        if(!treeWindow||context.ActiveWindow!==treeWindow)return skip('active-tree-window-unconfirmed');
        const windows=[],records=[],observations=[];let incomplete=false;
        result.coordinatorSelection={schema:1,scope:'held-root-active-tree-and-evaluation-windows',
          maximumWindows:3,diagnosticOnly:true,candidates:observations};
        const choices=[[browser,'receiver-root'],[treeWindow,'active-tree-window'],[window,'evaluation-window']];
        for(const [sourceWindow,origin]of choices){
          const seen=windows.find(value=>value.window===sourceWindow);
          if(seen){seen.observation.origins.push(origin);continue;}
          const observation={origins:[origin],present:false,profileVerified:false,registeredTreeCount:null,
            findReturne)YMSTEAM"
R"YMSTEAM(dRow:null,matchingDataRows:null,rowTreeSlot:null};
          observations.push(observation);
          const slot=Object.getOwnPropertyDescriptor(sourceWindow,'g_WindowFocusCoordinator');
          const coordinator=slot&&'value'in slot?slot.value:undefined;
          const holder={window:sourceWindow,slot,coordinator,observation};windows.push(holder);
          if(!coordinator){observation.status=slot?'coordinator-slot-unverified':'coordinator-unavailable';if(slot)incomplete=true;continue;}
          observation.present=true;
          const duplicate=records.find(value=>value.coordinator===coordinator);
          if(duplicate){observation.status='same-coordinator';observation.sameAsCandidate=observations.indexOf(duplicate.observation);continue;}
          const proto=Object.getPrototypeOf(coordinator),ctor=data(proto,'constructor');
          const rows=data(coordinator,'m_rgTrees'),find=coordinator.FindTree,viewLookup=coordinator.GetActiveBrowserViewWithinTree;
          const descriptor=proto&&Object.getOwnPropertyDescriptor(proto,'OnTreeActivated');
          observation.registeredTreeCount=Array.isArray(rows)?Math.min(rows.length,129):null;
          const record={coordinator,proto,ctor,rows,find,viewLookup,descriptor,holder,observation,qualified:false};records.push(record);
          if(!Array.isArray(rows)||rows.length>128||ctor?.prototype!==proto||text(ctor)!==expectedCoordinator||
             text(find)!=='FindTree(M){return this.m_rgTrees.find(D=>D.tree==M)}'||
             text(viewLookup)!=='GetActiveBrowserViewWithinTree(M){return this.FindTree(M)?.browser}'||
             !descriptor||text(descriptor.get)!==bindGetter){observation.status='coordinator-profile-unverified';incomplete=true;continue;}
          observation.profileVerified=true;
          const unverifiedRows=rows.filter(value=>{const d=value&&Object.getOwnPropertyDescriptor(value,'tree');return !d||!('value'in d);}).length;
          observation.unverifiedRowCount=unverifiedRows;
          if(unverifiedRows){observation.status='registration-tree-slot-unverified';incomplete=true;continue;}
          const row=find.call(coordinator,tree),matches=rows.filter(value=>data(value,'tree')===tree);
          record.row=row;record.matches=matches.length;record.qualified=true;
          observation.findReturnedRow=!!row;observation.matchingDataRows=matches.length;
          const rowSlot=row&&Object.getOwnPropertyDescriptor(row,'tree');
          observation.rowTreeSlot=rowSlot?'value'in rowSlot?'data':'accessor':row?'missing':null;
          observation.status=!row?'tree-not-registered':matches.length!==1||matches[0]!==row?'registered-row-unconfirmed':'registered-tree-matched';
          record.matched=!!row&&matches.length===1&&matches[0]===row;
        }
        result.coordinatorPresent=observations.some(value=>value.present);
        result.coordinatorSelection.complete=!incomplete;
        if(incomplete)return skip('coordinator-scope-incomplete');
        const selected=records.filter(value=>value.matched);
        result.coordinatorSelection.uniqueRegisteredCoordinators=selected.length;
        if(selected.length!==1){
          skip(selected.length?'registered-tree-coordinator-ambiguous':'registered-tree-unconfirmed');
          if(selected.length===0)reassertMenuGamePopupActivation(receiver,target,result);
          return;
        }
        const selectedRecord=selected[0],{coordinator,proto,ctor,rows,find,viewLookup,descriptor,row,holder}=selectedRecord;
        result.coordinatorSelection.selectedOrigins=[...holder.observation.origins];
        result.registeredTreeCount=rows.length;
        const browserContext=data(row,'browserContext'),owner=data(browserContext,'ownerWindow'),view=data(row,'browser');
        const noView=()=>{const d=Object.getOwnPropertyDescriptor(row,'browser');return d?'value'in d&&d.value==null:!('browser'in row);};
        const treeActive=tree.BIsActive?.(),contextActive=tree.BIsContextActive?.();
        Object.assign(result,{registeredTreeConfirmed:true,ownerWindowPresent:!!owner,
          ownerIsRoot:owner===browser,ownerIsTreeWindow:!!owner&&owner===treeWindow,
          ownerIsActiveWindow:!!owner&&owner===context.ActiveWindow,treeContextIsHeld:tree.WindowContext===context,
          treeIsActive:typeof treeActive==='boolean'?treeActive:null,
          treeContextIsActive:typeof contextActive==='boolean'?contextActive:null,browserViewPresent:view!=null});
        if(!owner||!noView()||owner!==browser||context.ActiveWindow!==treeWindow||tree.WindowContext!==context||
           treeActive!==true||contextActive!==true)return skip('registered-tree-owner-unconfirmed');
        const client=owner.SteamClient,api=client?.Window,mark=api?.MarkLastFocused,key=api?.SetKeyFocus;
        if(typeof mark!=='function'||typeof key!=='function')return skip('registered-owner-native-api-unavailable');
        const own=Object.getOwnPropertyDescriptor(coordinator,'OnTreeActivated');
        result.callbackRepresentation=boundShape(own)?'original-bind-shape':'unknown';
        if(!boundShape(own))return skip('coordinator-bound-method-unverified');
        const callback=own.value,running=ui.m_runningAppIDs;
        const slotCurrent=holder=>{
          const d=Object.getOwnPropertyDescriptor(holder.window,'g_WindowFocusCoordinator'),before=holder.slot;
          return !before?!d:!!d&&('value'in d)===('value'in before)&&
            ('value'in before?d.value===before.value:d.get===before.get&&d.set===before.set);
        };
        const registrationsCurrent=()=>{
          if(!windows.every(slotCurrent))return false;
          for(const record of records){
            if(!record.qualified)continue;
            const c=record.coordinator;
            if(Object.getPrototypeOf(c)!==record.proto||data(record.proto,'constructor')!==record.ctor||
               record.ctor.prototype!==record.proto||Object.getOwnPropertyDescriptor(record.proto,'OnTreeActivated')?.get!==record.descriptor.get||
               data(c,'m_rgTrees')!==record.rows||record.rows.length>128||c.FindTree!==record.find||
               c.GetActiveBrowserViewWithinTree!==record.viewLookup)return false;
            // Reject changed/accessor rows before invoking original FindTree.
            const matches=[];
            for(const value of record.rows){
              const d=value&&Object.getOwnPropertyDescriptor(value,'tree');
              if(!d||!('value'in d))return false;
              if(d.value===tree)matches.push(value);
            }
            if(matches.length!==record.matches||(record.matched&&
               (data(record.row,'tree')!==tree||matches.length!==1||matches[0]!==record.row)))return false;
            if(record.find.call(c,tree)!==record.row)return false;
          }
          return true;
        };
        const current=()=>game.current()&&game.entry?.current?.()&&window.SteamUIStore===ui&&
          ui.NavigationManager===manager&&ui.m_runningAppIDs===running&&Array.isArray(running)&&
          running.length===1&&(running[0]>>>0)===game.appid&&game.registered===String(game.appid)&&
          game.registeredAppid===(game.appid&0xffffff)&&w.BrowserWindow===browser&&browser.browserInfo===info&&
          w.params.browserInfo===info&&w.MenuStore.GetOpenSideMenu()===(request.menu==='main'?1:2)&&
          w.CompositionStateStore?.GetCompositionState?.()===2&&data(w,'m_FocusNavContext')===context&&
          context.RootWindow===browser&&manager.GetActiveContext()===context&&context.m_LastActiveFocusNavTree===tree&&
          context.ActiveWindow===treeWindow&&tree.WindowContext===context&&tree.Window===treeWindow&&
          tree.BIsActive()===true&&tree.BIsContextActive()===true&&registrationsCurrent()&&
          data(row,'browserContext')===browserContext&&data(browserContext,'ownerWindow')===owner&&noView()&&
          data(coordinator,'OnTreeActivated')===callback&&owner.SteamClient===client&&client.Window===api&&
          api.MarkLastFocused===mark&&api.SetKeyFocus===key&&game.current()&&game.entry?.current?.()&&
          ui.m_runningAppIDs===running&&running.length===1&&(running[0]>>>0)===game.appid;
        if(!current())return skip('registered-focu)YMSTEAM"
R"YMSTEAM(s-owner-changed-before-callback');
        mutated=true;result.callbackSubmitted=true;result.status='original-focus-callback-submitted';
        try{
          const reply=callback.call(coordinator,tree);
          if(reply&&typeof reply.then==='function'){
            Promise.resolve(reply).catch(()=>{});result.status='original-focus-callback-uncertain';
          }
        }catch{result.status='original-focus-callback-uncertain';}
        if(!current())result.status='registered-focus-owner-ended-after-callback';
      }catch{result.reason='registered-focus-observation-unavailable';}
    };

    const syncMenuGameState = receiver => {
      const game=receiver?.game;
      if(!game)return null;
      if(!game.current())fail('steam-menu-game-window-changed');
      if(game.registered===game.logical)return {status:'exact-identity',stateWritten:false};
      const w=receiver.window,menu=w.MenuStore.GetOpenSideMenu(),state=w.CompositionStateStore?.GetCompositionState?.();
      const entry=game.entry;
      if(menu===0&&entry?.activated&&entry.current?.()){
        mutated=true;const written=entry.submit(0);entry.dispose(false);
        return {status:'native-registration-menu-ended',stateWritten:written,composition:state};
      }
      // Use the settled original composition only. A screensaver, keyboard,
      // modal or unknown state does not authorize forcing an overlay visible.
      if(!((menu===0&&state===0)||([1,2].includes(menu)&&state===2)))
        return {status:'composition-not-settled',stateWritten:false};
      if(!game.current())fail('steam-menu-game-window-changed');
      if(entry?.lastState===state)return {status:'native-registration-state-synchronized',stateWritten:false,composition:state};
      if(!entry?.current?.())return {status:'native-registration-owner-ended',stateWritten:false};
      mutated=true;const written=entry.submit(state);
      return {status:written?'native-registration-state-submitted':'native-registration-state-uncertain',stateWritten:written,composition:state};
    };

    const menuReceiver = (handler, trace=null) => {
      const s=window.SteamUIStore, active=s?.ActiveWindowInstance;
      if(trace){trace.version=3;trace.lookup='not-needed-active-overlay';}
      try {
        if(active?.IsGamepadUIOverlayWindow?.()===true && typeof active[handler]==='function')
          return {window:active,source:'steam-active-context'};
        const focus=window.FocusedAppWindowStore, store=s?.WindowStore;
        const app=focus?.GetFocusedOverlayAppID?.(), pid=focus?.GetFocusedOverlayPID?.();
        if(trace){
          trace.lookup=!Number.isInteger(app)||app<=0||app>0xffffffff?'focused-app-invalid':
            !Number.isInteger(pid)||pid<0||pid>0xffffffff?'focused-pid-invalid':
            'lookup-api-unavailable';
          // Classify only the failed lookup's already-read primitive value.
          // Do not export an AppID/PID, scan overlays or add diagnostic reads.
          if(trace.lookup==='focused-app-invalid'||trace.lookup==='focused-pid-invalid'){
            const value=trace.lookup==='focused-app-invalid'?app:pid;
            trace.focusValueKind=value==null?'missing':typeof value!=='number'?'other-type':
              value===0?'zero':Number.isInteger(value)&&value>=-0x80000000&&value<0?'signed':
              'invalid-number';
          }
        }
        if(Number.isInteger(app)&&app>0&&app<=0xffffffff && Number.isInteger(pid)&&pid>=0&&pid<=0xffffffff &&
            typeof store?.GetOverlayInstance==='function') {
          const overlay=store.GetOverlayInstance(app,pid);
          if(trace)trace.lookup=overlay?'not-gamepad-or-handler-unavailable':'overlay-not-found';
          if(overlay?.IsGamepadUIOverlayWindow?.()===true && typeof overlay[handler]==='function')
            {if(trace)trace.lookup='game-overlay-selected';return {window:overlay,source:'steam-focused-game-overlay'};}
        }
      } catch {if(trace)trace.lookup='optional-route-threw';} // Preserve original key fallback.
      return typeof active?.[handler]==='function'?{window:active,source:'steam-active-context'}:
        {window:s?.WindowStore?.GamepadUIMainWindowInstance,source:'steam-big-picture'};
    };
    // Passive receiver bookkeeping only. Native owns the click-only 750ms
    // deadlines; this bridge never sleeps, starts a timer or sends a second toggle
    // from an asynchronous JS callback. A lost receiver edge is not permission.
    const qamFollowup = (w, phase, ticket, game=null) => {
      const s=window.SteamUIStore, m=w?.MenuStore, now=Date.now();
      const result=(status,terminal=true,extra={})=>({status,terminal,...extra});
      const read=()=>{try{const v=m?.GetOpenSideMenu?.();return [0,1,2].includes(v)?v:null;}catch{return null;}};
      if(!w||!m||typeof w.OnQuickAccessButtonPressed!=='function')return result('receiver-unavailable');
      let state=window.__ymccQamFollowup;
      if(state?.version!==3||state.store!==s){
        state?.dispose?.();
        state={version:3,store:s,entries:[],ticket:null,gameGesture:null};
        state.dispose=()=>{for(const e of state.entries)try{if(e.window.OnQuickAccessButtonPressed===e.wrapper){if(e.own)Object.defineProperty(e.window,'OnQuickAccessButtonPressed',e.own);else delete e.window.OnQuickAccessButtonPressed;}}catch{}state.entries=[];state.ticket=null;state.gameGesture=null;};
        window.__ymccQamFollowup=state;
      }
      const validTicket=()=>ticket&&typeof ticket.id==='string'&&ticket.id.length<=96&&Number.isSafeInteger(ticket.startedAt)&&
        ticket.startedAt<=now&&now-ticket.startedAt<3000;
      const gameKey=game?JSON.stringify(['account','appid','gameid64','gamePid','gameCreated','steamPid','steamCreated',
        'hostPid','hostCreated','browserId','registeredAppid','registeredGameid64'].map(k=>request.menuGame[k])):null;
      const gesture=state.gameGesture;
      let entry=state.entries.find(e=>e.window===w&&e.menu===m);
      if(phase!=='prepare'){
        if(!validTicket())return result('expired');
        // Only the original game's registration, receiver, and button edge can retry.
        if(game&&(!gesture||gesture.id!==ticket.id||gesture.startedAt!==ticket.startedAt||
                  gesture.key!==gameKey||!entry||gesture.entry!==entry))return result('context-changed');
        if(!game&&gesture?.id===ticket.id)return result('context-changed');
      }
      if(!entry){
        // At most the current main/overlay contexts; restore an evicted wrapper.
        if(state.entries.length===2){const e=state.entries.shift();try{if(e.window.OnQuickAccessButtonPressed===e.wrapper){if(e.own)Object.defineProperty(e.window,'OnQuickAccessButtonPressed',e.own);else delete e.window.OnQuickAccessButtonPressed;}}catch{}}
        const own=Object.getOwnPropertyDescriptor(w,'OnQuickAccessButtonPressed');
        if(own&&(!('value'in own)||!own.configurable&&!own.writable))return result('receiver-not-writable');
        entry={window:w,menu:m,original:w.OnQuickAccessButtonPressed,own,serial:0,event:null,previous:null};
        entry.wrapper=function(...args){
          let event=null;
          try {event={serial:++entry.serial,at:Date.now(),before:read(),pending:false,failed:false,
            history:w.m_history?.location?.pathname,home:w.m_flLastHomePressMS};entry.previous=entry.event;entry.event=event;}catch{entry.event=null;}
          // Bookkeeping failures must never block Steam's original receiver.
          let value;try{value=entry.original.apply(this,args);}catch(error){if(event)event.failed=true;throw error;}
          if(event)try {
            event.opened=read()===2;
            if(value&&typeof value.then==='function'){
              event.pending=true;Promise.resolve(value).then(()=>{event.pending=false;event.opened=event.opened||read()===2;},()=>{event.pending=false;event.failed=true;});
            }
          }catch{event.failed=true;}
          return value;
        };
        try{w.OnQuickAccessButtonPressed=entry.wrapper;}catch{return result('receiver-not-writable');}
        if(w.OnQuickAccessButtonPressed!==entry.wrapper)return result('receiver-not-writable');
        state.entries.push(entry);
      })YMSTEAM"
R"YMSTEAM(
      if(phase==='prepare'){
        if(game&&validTicket()&&game.current())
          state.gameGesture={id:ticket.id,startedAt:ticket.startedAt,key:gameKey,entry,serial:entry.serial+1};
        return result('receiver-prepared');
      }
      checkAccount();
      if(window.SteamUIStore!==s||s.GetShowingLockScreen?.()||w.IsAnyVRWindow?.()||
          !(game?game.current():menuReceiver('OnQuickAccessButtonPressed').window===w)||w.MenuStore!==m||
          w.OnQuickAccessButtonPressed!==entry.wrapper)return result('context-changed');
      // An evaluation already sent before cancellation may return late. It
      // cannot replace a newer ticket, even when callbacks share a millisecond.
      if(state.ticket&&state.ticket.id!==ticket.id&&state.ticket.startedAt>=ticket.startedAt)return result('superseded');
      if(!state.ticket||state.ticket.id!==ticket.id){
        state.ticket={id:ticket.id,startedAt:ticket.startedAt,entry,event:null,attempts:0,lastSlot:0,lastAttemptAt:0};
      }
      const t=state.ticket;
      if(t.startedAt!==ticket.startedAt||t.entry!==entry)return result('context-changed');
      if(!t.event){
        // Retain two receiver edges. A delayed first observation must detect
        // a later physical/user press rather than adopt it as the original.
        if(game){
          if(entry.serial<gesture.serial)return result('waiting-receiver',false,{menu:read()});
          if(entry.event?.serial!==gesture.serial)return result('newer-user-input');
          t.event=entry.event;
        }else if(entry.previous?.at>=ticket.startedAt)t.event=entry.previous;
        else if(entry.event?.at>=ticket.startedAt)t.event=entry.event;
        else return result('waiting-receiver',false,{menu:read()});
      }
      if(entry.event!==t.event)return result('newer-user-input');
      // An existing open QAM means this press was a close request. Never reopen it.
      if(t.event.before===2)return result('close-request');
      if(t.event.before!==0&&t.event.before!==1)return result('menu-state-unknown');
      if(t.event.failed||t.handlerFailed)return result('receiver-failed');
      if(t.event.opened)return result('menu-was-opened');
      if(t.event.history!==w.m_history?.location?.pathname||t.event.home!==w.m_flLastHomePressMS)return result('other-navigation');
      const menu=read();
      if(menu===2)return result('menu-open',true,{menu,attempts:t.attempts});
      if(menu===null)return result('menu-state-unknown');
      if(menu!==t.event.before)return result('other-navigation');
      if(phase!=='retry')return result('awaiting-menu',false,{menu});
      const elapsed=now-ticket.startedAt;
      // Native schedules at 750/1500/2250ms. Late workers cannot bunch retries.
      const slot=Math.floor(elapsed/750);
      if(slot<1||slot>3||slot<=t.lastSlot||t.attempts>=3)return result('waiting-slot',false,{menu,attempts:t.attempts});
      t.lastSlot=slot;
      if(m.m_cSuppressRequests>0||t.handlerPending||t.event.pending)return result('steam-suppressed',false,{menu});
      if(t.lastAttemptAt&&now-t.lastAttemptAt<750)return result('waiting-spacing',false,{menu});
      ++t.attempts;t.lastAttemptAt=now;mutated=true;
      const value=entry.original.call(w); // Same receiver, exactly once for this slot.
      if(value&&typeof value.then==='function'){
        t.handlerPending=true;Promise.resolve(value).then(()=>{t.handlerPending=false;},()=>{t.handlerPending=false;t.handlerFailed=true;});
      }
      checkAccount();const after=read();
      return result(after===2?'menu-open':'supplement-sent',after===2||t.attempts===3,{menu:after,attempts:t.attempts,supplementSent:true});
    };
    const passiveQamFollowup=(w,phase,ticket,game)=>{
      try{return qamFollowup(w,phase,ticket,game);}catch{return {status:'recorder-unavailable',terminal:true};}
    };
    // Second evaluation of the SAME explicit QAM action. Native retains its
    // original power/process/host admission. This branch sends no second key,
    // performs no new discovery and consumes only the original alias ticket.
    if(request.menuGamePopupActivation!==undefined){
      const output={schema:1,scope:'held-active-qam-popup-original-activation',status:'not-attempted',
        callbackSubmitted:false,inputCapabilityGranted:false,physicalInputProven:false,activationPhase:'native-followup'};
      const finish=reason=>{if(reason)output.reason=reason;return {ok:true,mutated,menuPopupActivation:output,via:'live'};};
      if(request.operation!=='menu.qam.observe'||request.menu!=='quick-access'||
         typeof request.menuGamePopupActivation!=='string'||!/^[A-Za-z0-9._:-]{1,96}$/.test(request.menuGamePopupActivation)||
         !request.menuGame||typeof request.menuGame!=='object'||Array.isArray(request.menuGame))
        fail('invalid-steam-menu-popup-continuation');
      const entry=window.__ymccMenuGameAlias,descriptor=entry&&Object.getOwnPropertyDescriptor(entry,'popupActivationPending');
      const pending=descriptor&&'value'in descriptor?descriptor.value:null;
      if(!pending||!Object.isFrozen(pending)||pending.id!==request.menuGamePopupActivation||
         pending.receiver?.game?.entry!==entry)return finish('popup-ticket-not-current');
      for(const key of ['version','appid','gameid64','account','gamePid','gameCreated','steamPid','steamCreated',
                       'hostPid','hostCreated','browserId','registeredAppid','registeredGameid64'])
        if(request.menuGame[key]!==pending.proof[key])return finish('popup-ticket-identity-changed');
      // Consume before any observable source read; rejected and uncertain
      // continuations are never retried or made available to a newer request.
      delete entry.popupActivationPending;
      if(entry.disposed||window.__ymccMenuGameAlias!==entry)return finish('popup-ticket-owner-ended');
      const target={},focus={menuTreeActivation:output};
      reassertMenuGamePopupActivation(pending.receiver,target,focus,pending);
      return {ok:true,mutated,menuPopupActivation:focus.menuTreeActivation,via:'live'};
    }
    if(request.operation==='menu.qam.dispose'&&request.menuGameDisposeOnly===true){
      const alias=window.__ymccMenuGameAlias,owned=!!alias,traceTarget={};
      alias?.dispose?.(true);
      projectMenuGameInputTrace({game:{entry:alias}},traceTarget,'dispose');
      return {ok:true,mutated:owned,menuGameState:{status:'disposed',...traceTarget},via:'live'};
    }
    if(request.operation==='menu.inspect'&&request.menuGameRead===true){
      const account=window.settingsStore?.m_CMInterface?.steamid?.GetAccountID?.();
      if(!Number.isInteger(account)||account<=0||window.SteamUIStore?.GetShowingLockScreen?.())fail('steam-menu-game-read-unavailable');
      const rows=await bounded(SteamClient.Overlay.GetOverlayBrowserInfo());
      if(!Array.isArray(rows)||rows.length>16)fail('steam-menu-game-registration-invalid');
      return {ok:true,mutated:false,observedAccount:account,menuGameBrowsers:rows.map(r=>({hostPid:r.unPID,browserId:r.nBrowserID,appid:r.appID,gameid64:r.gameID,browserType:r.eBrowserType,uiMode:r.eUIMode})),via:'live'};
    }
    if(request.operation==='menu.qam.dispose'){
      checkAccount();window.__ymccQamFollowup?.dispose?.();delete window.__ymccQamFollowup;
      return {ok:true,mutated:false,qamRecovery:{status:'disposed',terminal:true},via:'live'};
    }
    if (['menu.qam.prepare','menu.qam.observe','menu.qam.retry'].includes(request.operation)) {
      checkAccount();
      const receiver=request.menuGame?await menuGameReceiver('OnQuickAccessButtonPressed',request.menuGame,{}):menuReceiver('OnQuickAccessButtonPressed');
      const w=receiver.window;
      const phase=request.operation.slice('menu.qam.'.length);
      stage='qam-followup';
      const recovery=qamFollowup(w,phase,request.qamFollowup,receiver.game);
      mutated=recovery.supplementSent===true;
      const menuGameState=phase==='prepare'?null:syncMenuGameState(receiver);
      if(menuGameState)recovery.menuGameState=menuGameState;
      projectMenuGameInputTrace(receiver,recovery,'receipt');
      return {ok:true,mutated,qamRecovery:recovery,via:'live'};
    }
    if (['menu.button', 'menu.toggle', )YMSTEAM"
R"YMSTEAM('menu.inspect'].includes(request.operation)) {
      if (!['main', 'quick-access'].includes(request.menu)) fail('invalid-steam-menu');
      const s = window.SteamUIStore;
      // GLOBAL functional-key semantics. Steam, not an OS HWND/PID chosen by
      // YMCC, supplies its own routing context. Never focus or activate a window.
      const handler = request.menu === 'main' ? 'OnHomeButtonPressed' : 'OnQuickAccessButtonPressed';
      const routeTrace={}, receiver=request.menuGame?await menuGameReceiver(handler,request.menuGame,routeTrace,
        request.operation==='menu.button'&&request.menu==='quick-access'):menuReceiver(handler,routeTrace), w=receiver.window;
      const m = w?.MenuStore;
      menuTarget = { source: receiver.source,
        windowType: Number.isInteger(w?.WindowType) ? w.WindowType : null, isGameOverlay: !!w?.IsOverlayWindow?.(),
        handler, keyboardFallback: false, runtimeAccepted: false,
        routing:routeTrace };
      if (!s || !w || typeof w[handler] !== 'function') fail('live-api-unavailable');
      // Keep Steam's lock/VR boundaries, but do not treat keyboard modifiers,
      // OS foreground ownership or a hidden main window as key-admission gates.
      if (s.GetShowingLockScreen?.() || w.IsAnyVRWindow?.()) fail('steam-menu-blocked');
      const readMenu = () => {
        try { const value = m?.GetOpenSideMenu?.(); return [0, 1, 2].includes(value) ? value : null; }
        catch { return null; }
      };
      // Explicit proven-alias menu diagnostics only. These original Steam
      // getters read existing navigation caches; no focus/input setter, event,
      // subscription, timer or alternate receiver is installed here.
      const observeMenuInputContext = phase => {
        if(!receiver.game||receiver.game.registered===receiver.game.logical)return;
        try {
          const probe=read=>{try{return read();}catch{return undefined;}};
          const focused=probe(()=>typeof s.GetFocusedWindowInstance==='function'?s.GetFocusedWindowInstance():undefined);
          const active=probe(()=>s.ActiveWindowInstance);
          const navigationActive=probe(()=>w.FocusNavActiveSubscribableValue?.Value);
          const overlayAppid=probe(()=>window.FocusedAppWindowStore?.GetFocusedOverlayAppID?.());
          const overlayPid=probe(()=>window.FocusedAppWindowStore?.GetFocusedOverlayPID?.());
          // Inspect only existing caches. An inherited context may have a
          // different RootWindow; identity and tree ownership stay separate.
          const manager=probe(()=>s.NavigationManager);
          const activeContext=probe(()=>manager?.GetActiveContext?.());
          const receiverContext=probe(()=>w.m_FocusNavContext);
          const browser=probe(()=>w.BrowserWindow);
          const tree=probe(()=>receiverContext?.m_LastActiveNavTree);
          const focusTree=probe(()=>receiverContext?.m_LastActiveFocusNavTree);
          const objectKnown=v=>v!==null&&(typeof v==='object'||typeof v==='function');
          const contextKnown=objectKnown(receiverContext),activeKnown=objectKnown(activeContext);
          const related=read=>{const value=probe(read);return typeof value==='boolean'?value:null;};
          const navigation={
            receiverContextPresent:receiverContext===undefined?null:receiverContext!==null,
            activeContextPresent:activeContext===undefined?null:activeContext!==null,
            receiverContextIsActive:contextKnown&&activeKnown?receiverContext===activeContext:null,
            receiverInputSuppressed:contextKnown?related(()=>receiverContext.BIsGamepadInputSuppressed?.()):null,
            activeInputSuppressed:activeKnown?related(()=>activeContext.BIsGamepadInputSuppressed?.()):null,
            receiverRootIsBrowser:contextKnown&&objectKnown(browser)?related(()=>receiverContext.RootWindow===browser):null,
            receiverActiveWindowIsBrowser:contextKnown&&objectKnown(browser)?related(()=>receiverContext.ActiveWindow===browser):null,
            receiverTreePresent:contextKnown?tree===undefined?null:tree!==null:null,
            receiverTreeWindowIsBrowser:objectKnown(tree)&&objectKnown(browser)?related(()=>tree.Window===browser):null,
            receiverFocusTreePresent:contextKnown?focusTree===undefined?null:focusTree!==null:null,
            receiverFocusTreeWindowIsBrowser:objectKnown(focusTree)&&objectKnown(browser)?related(()=>focusTree.Window===browser):null,
          };
          const appidKnown=Number.isInteger(overlayAppid)&&overlayAppid>=-0x80000000&&overlayAppid<=0xffffffff;
          const pidKnown=Number.isInteger(overlayPid)&&overlayPid>=0&&overlayPid<=0xffffffff;
          const appid=appidKnown?overlayAppid>>>0:null;
          // Bounded reads of existing Steam caches only; never input authority.
          const integer=(v,min=0,max=0xffffffff)=>Number.isSafeInteger(v)&&v>=min&&v<=max?v:null;
          const ownInteger=(object,key,min=0,max=0xffffffff)=>{
            try{const d=object&&Object.getOwnPropertyDescriptor(object,key);return d&&'value'in d?integer(d.value,min,max):null;}catch{return null;}
          };
          const originalCacheRead=(object,key,expected,getter=false,field=null)=>{
            try{
            const fieldDescriptor=object&&Object.getOwnPropertyDescriptor(object,field);
            if(!fieldDescriptor||'value'in fieldDescriptor||typeof fieldDescriptor.get!=='function'||
               Function.prototype.toString.call(fieldDescriptor.get)!=='function(){return this[W].getObservablePropValue_(g)}')return null;
              let owner=object;
              for(let depth=0;owner&&depth<2;++depth,owner=Object.getPrototypeOf(owner)){
                const d=Object.getOwnPropertyDescriptor(owner,key);if(!d)continue;
                const fn=getter?d.get:'value'in d?d.value:null;
                if(typeof fn!=='function'||Function.prototype.toString.call(fn)!==expected)return null;
                return Reflect.apply(fn,object,[]);
              }
            }catch{}return null;
          };
          const focusSlot=probe(()=>Object.getOwnPropertyDescriptor(window,'FocusedAppWindowStore'));
          const focusCache=focusSlot&&'value'in focusSlot?focusSlot.value:null;
          // Observable fields can be accessors. Only these exact shipped pure
          // reads and the exact MobX field getter may observe them; other getters stay unknown.
          const mainMode=ownInteger(s,'m_mainInstanceUIMode')??integer(originalCacheRead(s,'MainInstanceUIMode','get MainInstanceUIMode(){return this.m_mainInstanceUIMode}',true,'m_mainInstanceUIMode'));
          const cachedAppid=ownInteger(focusCache,'m_unFocusedAppID',-0x80000000)??integer(originalCacheRead(focusCache,'GetFocusedAppID','GetFocusedAppID(){return this.m_unFocusedAppID}',false,'m_unFocusedAppID'),-0x80000000);
          const cachedWindow=ownInteger(focusCache,'m_nFocusedWindowID',0,Number.MAX_SAFE_INTEGER)??integer(originalCacheRead(focusCache,'GetFocusedWindowID','GetFocusedWindowID(){return this.m_nFocusedWindowID}',false,'m_nFocusedWindowID'),0,Number.MAX_SAFE_INTEGER);
          const observed={
            steamFocusCache:{scope:'existing-qualified-cache-read',diagnosticOnly:true,cacheFreshnessKnown:false,
              mainInstanceUIMode:mainMode,focusedAppid:cachedAppid,
              focusedAppidUnsigned:cachedAppid===null?null:cachedAppid>>>0,focusedWindowID:cachedWindow,
              activationCallbackDelivery:'UNKNOWN',inputCapabilityGranted:false},
            navigationWindowPresent:focused===undefined?null:focused!==null,
            navigationWindowIsReceiver:focused===undefined?null:focused===w,
            activeWindowPresent:active===undefined?null:active!==null,
            activeWindowIsReceiver:active===undefined?null:active===w,
            receiverNavigationActive:typeof navigationActive==='boolean'?navigationActive:null,
            focusedOverlayAppid:appidKnown?overlayAppid:null,
            focusedOverlayAppidUnsigned:appid,
            focusedOverlayPid:pidKnown?overlayPid:null,
            focusedAppidMatchesRegistered:appidKnown?appid===request.menuGame.registeredAppid:null,
            focusedAppidMatchesLogical:a)YMSTEAM"
R"YMSTEAM(ppidKnown?appid===receiver.game.appid:null,
            focusedPidMatchesHeldHost:pidKnown?overlayPid===request.menuGame.hostPid:null,
            navigation,
          };
          (menuTarget.inputContext??={schema:1,scope:'selected-overlay-cached-navigation',diagnosticOnly:true,atomic:false})[phase]=observed;
        } catch {} // Missing/throwing diagnostics never change the original key.
      };
      // Menu observation is optional: a failed read-back is NOT a key gate.
      projectMenuGameInputTrace(receiver,menuTarget,'before');
      const before = readMenu(), menu = request.menu === 'main' ? 1 : 2;
      checkAccount();
      observeMenuInputContext('before');
      if (request.operation === 'menu.inspect') {
        const gameState=syncMenuGameState(receiver);if(gameState)menuTarget.gameState=gameState;
        return {ok:true,mutated,before,menuTarget,via:'live'};
      }
      if(request.operation==='menu.button'&&request.menu==='quick-access')returnMenuGameToRunning(receiver,menuTarget);
      prepareMenuGameInputContext(receiver,menuTarget);
      if(request.operation==='menu.button'&&request.menu==='quick-access'&&request.qamFollowup)
        passiveQamFollowup(w,'prepare',request.qamFollowup,receiver.game);
      let compositionBrowserBefore={known:false,browser:null};
      if(receiver.game){try{
        const browser=w.BrowserWindow,client=browser?.SteamClient,api=client?.Window,set=api?.SetComposition;
        compositionBrowserBefore={known:browser==null||!!client&&!!api&&typeof set==='function',browser,client,api,set};
      }catch{}}
      if(receiver.game&&!receiver.game.current())fail('steam-menu-game-window-changed');
      stage = 'button'; mutated = true;
      // These are Steam's actual Guide/QAM key receivers, including its own
      // debounce, suppression and navigation behavior. Do not set menu flags.
      await bounded(w[handler]()); keyDelivered = true;
      checkAccount();
      const compositionMenu=readMenu();
      if(before!==null&&before!==menu&&compositionMenu===menu){
        const composition=syncMenuGameComposition(receiver,compositionBrowserBefore);
        if(composition)menuTarget.composition=composition;
        reassertMenuGameWindowFocus(receiver,menuTarget);
      }
      const menuGameState=syncMenuGameState(receiver);
      if(menuGameState)menuTarget.gameState=menuGameState;
      const after = readMenu(), known = before !== null && after !== null;
      observeMenuInputContext('after');
      const expected = before === menu ? 0 : menu, confirmed = known && after === expected;
      projectMenuGameInputTrace(receiver,menuTarget,'after',confirmed&&after===menu);
      menuTarget.runtimeAccepted = confirmed;
      const qamRecovery=request.menu==='quick-access'&&request.qamFollowup?passiveQamFollowup(w,'observe',request.qamFollowup,receiver.game):undefined;
      return { ok: true, before, menu: after, menuTarget, keyDelivered: true,
        ...(routeTrace.overlayRepair?{mutated:true}:{}), ...(qamRecovery?{qamRecovery}:{}),
        menuStateChanged: known ? before !== after : null, runtimeAccepted: confirmed,
        response: !known ? 'key-delivered-menu-state-unknown' : confirmed ? 'menu-state-confirmed' :
          before !== after ? 'key-delivered-other-menu-response' : 'key-delivered-no-menu-state-change', via: 'live' };
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
    // Retired: standard controllers use YMCC's realtime mouse providers.
    if (request.operation === 'desktopMouse.get' || request.operation === 'desktopMouse.set') fail('steam-desktop-mouse-retired');
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
      if (s.m_nEditNumber !== s.m_nLastSavedEditNumber || s.IsUpdatingEditingConfigura)YMSTEAM"
R"YMSTEAM(tion || s.PreviewedConfiguration) fail('live-editor-busy');
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
    // A later account/interactive veto must retain a settled bounded native
    // return for diagnosis. It grants no acknowledgment, replay or acceptance.
    let leaseFields = {};
    if (request.operation === 'input.gamepadLease') {
      leaseFields = { enabled: false, releasing: false, pendingReleaseCount: 0,
        serial: Number.isSafeInteger(request.serial) && request.serial > 0 ? request.serial : 0, remainingMs: 0, scope: 'steam-gamepad-navigation' };
      try { leaseFields = window.__ymccLightGamepadLease?.receipt() || leaseFields; } catch {}
    }
    // Preserve a caught Steam exception class before the generic business reason
    // erases it. Never inspect/log its message, stack, URL, or request contents.
    let steamFailure;
    try {
      const name = error?.name;
      const known = typeof name === 'string' && ['Error','TypeError','ReferenceError','SyntaxError','RangeError','EvalError','URIError','AggregateError','DOMException'].includes(name);
      steamFailure = { schema: 1, domain: 'javascript', sourceStage: 'evaluate', codeKnown: false,
        exceptionClassKnown: known, ...(known ? { exceptionClass: name } : {}) };
    } catch {} // Diagnostic access cannot replace the original failure.
    return { ok: false, reason: error.reason || 'live-api-error', stage, mutated, ...leaseFields,
      ...(steamFailure ? { steamFailure } : {}),
      ...(settledNativeReturn ? { nativeReturn: settledNativeReturn } : {}), ...(menuTarget ? { menuTarget, keyDelivered } : {}) };
  } finally {
    if (window.__ymccSteamLiveBusy === token) delete window.__ymccSteamLiveBusy;
  }
}))YMSTEAM";
inline constexpr const char kUiModeScript[] =
R"YMSTEAM(// Embedded in steam_live_script.h. Regenerate/check with tools/steam_live_script_embed.mjs.
(async function ymccSteamLive(request) {
  let mutated = false, stage = 'preflight', menuTarget = null, keyDelivered = false, settledNativeReturn = null;
  const token = `${Date.now()}-${Math.random()}`;
  const lightGamepadLease = request.operation === 'input.gamepadLease';
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
  if (!lightGamepadLease && window.__ymccSteamLiveBusy) return { ok: false, reason: 'live-busy', mutated: false, stage };
  if (!lightGamepadLease) window.__ymccSteamLiveBusy = token;
  try {
    // Only the independent, account/process-bound running-link read may own a
    // transient native subscription. This flag never grants a setter authority.
    if (request.nativeFocusFeedbackRead !== undefined &&
        (typeof request.nativeFocusFeedbackRead !== 'boolean' || request.operation !== 'sleep.inspect' || request.runningLinkRead !== true))
      fail('invalid-steam-native-focus-read');
    if (request.nativeAppLifetimeFeedbackRead !== undefined &&
        (typeof request.nativeAppLifetimeFeedbackRead !== 'boolean' || request.operation !== 'sleep.inspect' || request.runningLinkRead !== true))
      fail('invalid-steam-app-lifetime-read');
    if (request.nativeRecordIdentityRead !== undefined &&
        (typeof request.nativeRecordIdentityRead !== 'boolean' || !['sleep.session.inspect','sleep.inspect'].includes(request.operation)))
      fail('invalid-steam-native-record-read');
    // A private native diagnostic identity channel. The native caller strips
    // this reply before logging/export; no SteamID is persisted in JS state.
    const readNativeRecordIdentity = () => {
      const unknown=reason=>({status:'unavailable',reason});
      if(request.nativeRecordIdentityRead!==true)return null;
      try {
        if(!Number.isInteger(request.account)||request.account<=0||request.account>0xffffffff)return unknown('native-record-ui-account-unavailable');
        const ids=request.appids;
        if(!Array.isArray(ids)||ids.length!==1||!Number.isInteger(ids[0])||ids[0]<=0||ids[0]>0xffffffff)return unknown('native-record-ui-game-unavailable');
        const id=window.settingsStore?.m_CMInterface?.steamid;
        if(!id||typeof id.GetAccountID!=='function'||typeof id.ConvertTo64BitString!=='function')return unknown('native-record-ui-account-unavailable');
        if(Function.prototype.toString.call(id.ConvertTo64BitString)!=='ConvertTo64BitString(){return this.m_ulSteamID.toString()}')return unknown('native-record-ui-identity-schema-changed');
        const account=id.GetAccountID();
        if(account!==request.account)return unknown('native-record-ui-account-unavailable');
        const full=id.ConvertTo64BitString();
        if(typeof full!=='string'||!/^[1-9][0-9]{0,19}$/.test(full)||(BigInt(full)>0xffffffffffffffffn||(BigInt(full)&0xffffffffn)!==BigInt(account)))return unknown('native-record-ui-account-unavailable');
        const map=window.appStore?.m_mapApps;
        if(!map||typeof map.get!=='function')return unknown('native-record-ui-game-unavailable');
        const unsigned=map.get(ids[0]),signed=map.get(ids[0]|0);
        if(unsigned&&signed&&unsigned!==signed)return unknown('native-record-ui-game-unavailable');
        const overview=unsigned||signed,appid=overview?.appid,gameid=overview?.gameid,type=overview?.app_type;
        if(typeof appid!=='number'||!Number.isInteger(appid)||appid< -0x80000000||appid>0xffffffff||(appid>>>0)!==ids[0]||
          typeof gameid!=='string'||!/^[1-9][0-9]{0,19}$/.test(gameid)||!Number.isInteger(type)||type<0||type>0xffffffff)return unknown('native-record-ui-game-unavailable');
        const raw=BigInt(gameid),shortcut=(type&0x40000000)!==0;
        if(raw>0xffffffffffffffffn||!((!shortcut&&raw===BigInt(ids[0])&&raw<=0xffffffn)||(shortcut&&raw>>32n===BigInt(ids[0])&&(raw&0xffffffffn)===0x02000000n)))return unknown('native-record-ui-game-unavailable');
        if(window.settingsStore?.m_CMInterface?.steamid!==id||id.GetAccountID()!==account||id.ConvertTo64BitString()!==full||overview.gameid!==gameid)return unknown('native-record-ui-identity-raced');
        return {status:'observed',identity:id,overview,steamid64:full,gameid64:gameid,appid:ids[0]};
      } catch {return unknown('native-record-ui-identity-unavailable');}
    };
    const privateRecordStart=readNativeRecordIdentity();
    const privateNativeRecordReply=()=>{
      if(request.nativeRecordIdentityRead!==true)return {};
      const end=readNativeRecordIdentity();
      if(privateRecordStart?.status!=='observed'||end?.status!=='observed')return {_ymccPrivateNativeRecord:{status:'unavailable',reason:privateRecordStart?.reason||end?.reason||'native-record-ui-identity-unavailable'}};
      if(privateRecordStart.identity!==end.identity||privateRecordStart.overview!==end.overview||privateRecordStart.steamid64!==end.steamid64||privateRecordStart.gameid64!==end.gameid64||privateRecordStart.appid!==end.appid)
        return {_ymccPrivateNativeRecord:{status:'unavailable',reason:'native-record-ui-identity-raced'}};
      return {_ymccPrivateNativeRecord:{status:'observed',source:'steam-ui-exact-native-record-identity',steamid64:end.steamid64,gameid64:end.gameid64,appid:end.appid}};
    };
    // UI mode is process-wide, available before account login. Do not couple it
    // to CSSLoader, controller settings or whichever window currently has focus.
    if (request.operation === 'ui.mode.get') {
      stage = 'ui-mode-read';
      if (typeof globalThis.SteamClient?.UI?.GetUIMode !== 'function') fail('live-api-unavailable');
      // An O(1) mode callback remembers quick leave/re-enter transitions between
      // native window-event reads. No timers, DOM changes or CSS/Steam launches.
      const classify = value => value === 4 || value === 1 ? 'bigpicture' : value === 7 || value === 0 ? 'desktop' : 'unknown';
      let watch = window.__ymccSteamUiModeWatch;
      if (!watch) {
        watch = { version: 1, id: Date.now().toString(36) + '-' + Math.random().toString(36).slice(2), entries: 0, mode: 'unknown', revision: 0, lastRaw: null, active: false };
        window.__ymccSteamUiModeWatch = watch;
        watch.observe = value => { const next = classify(value); if (next === 'unknown') return;
          if (next === 'bigpicture' && watch.mode !== next) watch.entries++;
          watch.mode = next; watch.lastRaw = value; watch.revision++;
        };
        try {
          if (typeof SteamClient.UI.RegisterForUIModeChanged === 'function') {
            watch.subscription = SteamClient.UI.RegisterForUIModeChanged(watch.observe); watch.active = true;
          }
        } catch {} // Older Steam retains the verified current-mode fallback.
      }
      const beforeRead = watch.revision;
      let raw = await bounded(SteamClient.UI.GetUIMode());
      if (watch.revision === beforeRead) watch.observe(raw);
      else raw = watch.lastRaw; // A real event during the async read wins over its older reply.
      // Valve's current EUIMode: VGUI=0, Tenfoot=1, Gamepad=4, Desktop=7.
      const mode = raw === 4 || raw === 1 ? 'bigpicture' : raw === 7 || raw === 0 ? 'desktop' : 'unknown';
      // Narrow update residue only: no credentials, endpoint, settings or new subscription.
      // A prior active YMCC module differing from the installed module proves an update
      // remains unconsumed in this existing Steam context. An arbitrary Steam page does not.
      let deckyFrontendLoaded;
      try {
        if (window === window.top && location.origin === 'https://steamloopback.host')
          decky)YMSTEAM"
R"YMSTEAM(FrontendLoaded = !!window.deckyHasLoaded || !!window.DeckyPluginLoader;
      } catch {} // Unknown is never permission to risk restarting Steam's UI.
      let deckyUpdateResidue, deckyEmptyFrontend;
      try {
        const prior=window.__YMCC_DECKY_FRONTEND__,bridge=window.__YMCC_DECKY_MIRROR__;
        // Revoked bridge keeps only build/run provenance, never credentials.
        if(window===window.top&&location.origin==='https://steamloopback.host'&&window.DeckyPluginLoader&&prior?.active===true&&/^[a-f0-9]{64}$/.test(prior.buildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&((bridge?.revoked===true&&Object.keys(bridge).length===3&&bridge.frontendBuildId===prior.buildId)||(/^[a-f0-9]{64}$/.test(bridge?.token||'')&&/^ws:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}\/mirror$/.test(bridge?.endpoint||''))))
          deckyUpdateResidue={protocol:1,active:true,bridgePresent:true,frontendBuildId:prior.buildId};
        const emptyLoader=window.DeckyPluginLoader, emptyVersion=emptyLoader?.deckyState?.publicState?.()?.versionInfo?.current;
        if(window===window.top&&location.origin==='https://steamloopback.host'&&prior==null&&Array.isArray(emptyLoader?.plugins)&&emptyLoader.plugins.length===0&&/^[a-f0-9]{64}$/.test(bridge?.frontendBuildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&/^[a-f0-9]{64}$/.test(bridge?.token||'')&&new RegExp('^ws://127[.]0[.]0[.]1:[1-9][0-9]{0,4}/mirror$').test(bridge?.endpoint||'')&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(emptyVersion||''))
          deckyEmptyFrontend={protocol:1,frontendBuildId:bridge.frontendBuildId,loaderVersion:emptyVersion};
        if(deckyUpdateResidue){try{const loaded=window.DeckyPluginLoader?.deckyState?.publicState?.()?.versionInfo?.current;if(typeof loaded==='string'&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(loaded)&&loaded.length<=32)deckyUpdateResidue.loaderVersion=loaded;}catch{}}
      } catch {} // Recovery evidence is optional; a read failure never grants launch.
      // Reuse sleep.session.inspect's current UI identity read. Account discovery
      // stays read-only and account-independent for mode/CSS; Decky alone may
      // consume this proof when ActiveUser is zero in a logged-in offline session.
      let observedAccount;
      try {
        const id=window.settingsStore?.m_CMInterface?.steamid;
        if(typeof id?.GetAccountID==='function'){
          const account=id.GetAccountID();
          if(Number.isInteger(account)&&account>0&&account<=0xffffffff&&
             window.settingsStore?.m_CMInterface?.steamid===id&&id.GetAccountID()===account)observedAccount=account;
        }
      } catch {} // Missing/racing identity never prevents a process-wide mode read.
      // One optional native registration attempt per document, shared by all
      // rebinds. A missing/throwing hook or delayed initial callback must not
      // permanently block a valid current UI getter after offline/sleep startup.
      // Upgrade contract: read/refresh/subscribe is the stable document-owned
      // interface with readbackPolicy=2. Older complete objects can retain a
      // logout veto forever when the same account returns without a callback;
      // migrate them as notifications-only, without a second native hook set. An older object without this contract supplies
      // notifications only: its cached-account predicate must not veto live UI.
      const hadAccountWatch=Object.prototype.hasOwnProperty.call(window,'__YMCC_DECKY_ACCOUNT_WATCH__');
      let accountWatch=window.__YMCC_DECKY_ACCOUNT_WATCH__;
      if(!accountWatch||accountWatch.version!==1||accountWatch.readbackPolicy!==2||typeof accountWatch.read!=='function'||
         typeof accountWatch.refresh!=='function'||typeof accountWatch.subscribe!=='function'){
        const legacy=accountWatch;
        const listeners=new Set();let loggedOut=false,lastUiAccount=observedAccount||0;
        const read=expected=>{
          if(loggedOut||!Number.isInteger(expected)||expected<=0||expected>0xffffffff)return false;
          try {const id=window.settingsStore?.m_CMInterface?.steamid;
            return typeof id?.GetAccountID==='function'&&id.GetAccountID()===expected&&
              window.settingsStore?.m_CMInterface?.steamid===id&&id.GetAccountID()===expected;
          }catch{return false;}
        };
        const changed=()=>{for(const fn of Array.from(listeners).slice(0,4)){try{fn();}catch{}}};
        // Borrow the existing mode read to catch lost account callbacks. No
        // extra timer/read or native subscription, and no healthy-state events.
        const refresh=account=>{
          if(!Number.isInteger(account)||account<0||account>0xffffffff)return;
          // Logout revokes the old run immediately. A later actual mode read
          // may confirm the SAME current account after a missed login callback;
          // an empty UI still cannot reopen it. Healthy reads stay silent.
          const returned=loggedOut&&account>0;
          if(account===lastUiAccount&&!returned)return;
          lastUiAccount=account;if(account>0)loggedOut=false;changed();
        };
        accountWatch={version:1,readbackPolicy:2,read,refresh,subscribe(fn){listeners.add(fn);return ()=>listeners.delete(fn);}};
        Object.defineProperty(window,'__YMCC_DECKY_ACCOUNT_WATCH__',{value:accountWatch,configurable:true});
        if(hadAccountWatch){
          // One forwarding listener for the lifetime of this Steam document.
          // A broken/opaque legacy observer falls back to existing UI readback;
          // do not call its read() or guess whether native hooks were installed.
          try{legacy?.subscribe?.(changed);}catch{}
        }else{
        try{
          SteamClient.User?.RegisterForCurrentUserChanges?.(user=>{
            let next=0;
            try {const id=user?.strSteamID;
              if(typeof id==='string'&&/^[1-9][0-9]{0,19}$/.test(id)){
                const full=BigInt(id);if(full<=0xffffffffffffffffn&&((full>>52n)&15n)===1n)next=Number(full&0xffffffffn);
              }
            }catch{}
            // A callback can lag the actual UI after resume/account changes.
            // It wakes validation; its cached account is not a second veto.
            if(next)loggedOut=false;changed();
          });
        }catch{} // No retries/subscription buildup; read still validates the UI.
        try{
          SteamClient.User?.RegisterForLoginStateChange?.(name=>{
            loggedOut=typeof name!=='string'||!name;
            changed();
          });
        }catch{} // Independent hooks: a failed first registration cannot skip this one.
        }
      }
      accountWatch?.refresh?.(observedAccount||0);
      const deckyAccountWatchAvailable=accountWatch?.version===1&&!!observedAccount&&accountWatch.read(observedAccount);
      // Reviewed official v3.3.1 fields. Unknown versions remain UNKNOWN;
      // installed hooks are only a structural check, never visual acceptance.
      let deckyBackendConnected,deckyBackendState,deckyHooksInstalled,deckyClientConnected,deckyClientVerified,deckyClientChecking,deckyClientState,deckyQamPanelState;
      try {
        if(window===window.top&&window.location.origin==='https://steamloopback.host'&&
           window.DeckyPluginLoader?.deckyState?.publicState?.()?.versionInfo?.current==='v3.3.1'){
          const ws=window.DeckyBackend?.ws,readyState=ws?.readyState;
          if(ws&&Number.isInteger(readyState))deckyBackendConnected=readyState===1;
          // Reuse this read only: a closed official socket must not masquerade
          // as a missing YMCC peer, nor trigger an extra probe or UI restart.
          deckyBackendState=!ws?'socket-missing':readyState===0?'socket-connecting':readyState===1?'socket-open':
            readyState===2?'socket-closing':readyState===3?'socket-closed':'socket-unavailable';
          const hooks=window.__TABS_HOOK_INSTANCE,frontend=window.__YMCC_DECKY_FRONTEND__;
          const active=patch=>!!patch&&patch.hasUnpatched===false;
          deckyHooksInstalled=!!(hooks&&hooks===window.DeckyPluginLoader.tabsHook&&active(hooks.qamBrowserViewPatch)&&
            (!hooks.qamEmbeddedPatch||active(hooks.qamEmbeddedPatch))&&frontend?.qamReady!==fals)YMSTEAM"
R"YMSTEAM(e&&
            Array.isArray(hooks.tabs)&&hooks.tabs.some(tab=>tab?.id===999));
          deckyClientConnected=frontend?.active===true&&frontend.connectionReady===true;
          deckyClientChecking=frontend?.active===true&&frontend.connectionChecking===true;
          // Optional native reply proof is a memory getter, not another request.
          try{const verified=frontend?.active===true&&frontend.connectionVerified;if(typeof verified==='boolean')deckyClientVerified=verified;}catch{}
          // Optional local explanation only; the original booleans remain
          // authoritative. A diagnostic getter failure cannot discard them.
          try{
            if(!frontend)deckyClientState='frontend-absent';
            else if(frontend.active!==true)deckyClientState='frontend-inactive';
            else{const state=frontend.connectionState;if(['disposed','native-run-missing','binding-run-missing','run-mismatch','socket-missing','socket-connecting','socket-closing','socket-closed','socket-unavailable','transport-unconfirmed','opening-reply-missing','connected','module-dismounted','frontend-absent','frontend-inactive','read-failed'].includes(state))deckyClientState=state;}
          }catch{deckyClientState='read-failed';}
          // Optional commit evidence. Missing/hidden and old frontends cannot
          // prove a failed mount, and this value never changes hook health.
          try{
            const state=mode==='bigpicture'&&frontend?.active===true?frontend.qamPanelState:undefined;
            if(state==='unobserved'||state==='committed'||state==='native-ready-commit')deckyQamPanelState=state;
          }catch{}
        }
      }catch{} // Never emit credentials, router URL, source objects or exception text.
      return { ok: true, ...(typeof deckyBackendConnected==='boolean'?{deckyBackendConnected}:{}), ...(typeof deckyBackendState==='string'?{deckyBackendState}:{}), ...(typeof deckyHooksInstalled==='boolean'?{deckyHooksInstalled}:{}), ...(typeof deckyClientConnected==='boolean'?{deckyClientConnected}:{}), ...(typeof deckyClientChecking==='boolean'?{deckyClientChecking}:{}), ...(typeof deckyClientVerified==='boolean'?{deckyClientVerified}:{}), ...(typeof deckyClientState==='string'?{deckyClientState}:{}), ...(deckyQamPanelState?{deckyQamPanelState}:{}), ...(observedAccount?{observedAccount}:{}), deckyAccountWatchAvailable, mode, rawMode: Number.isInteger(raw) ? raw : null, modeEpoch: watch.active ? watch.id + ':' + watch.entries : '', ...(deckyUpdateResidue?{deckyUpdateResidue}:{}), ...(deckyEmptyFrontend?{deckyEmptyFrontend}:{}), ...(typeof deckyFrontendLoaded === 'boolean'?{deckyFrontendLoaded}:{}), mutated: false, via: 'live' };
    }
  } catch (error) {
    // A later account/interactive veto must retain a settled bounded native
    // return for diagnosis. It grants no acknowledgment, replay or acceptance.
    let leaseFields = {};
    if (request.operation === 'input.gamepadLease') {
      leaseFields = { enabled: false, releasing: false, pendingReleaseCount: 0,
        serial: Number.isSafeInteger(request.serial) && request.serial > 0 ? request.serial : 0, remainingMs: 0, scope: 'steam-gamepad-navigation' };
      try { leaseFields = window.__ymccLightGamepadLease?.receipt() || leaseFields; } catch {}
    }
    // Preserve a caught Steam exception class before the generic business reason
    // erases it. Never inspect/log its message, stack, URL, or request contents.
    let steamFailure;
    try {
      const name = error?.name;
      const known = typeof name === 'string' && ['Error','TypeError','ReferenceError','SyntaxError','RangeError','EvalError','URIError','AggregateError','DOMException'].includes(name);
      steamFailure = { schema: 1, domain: 'javascript', sourceStage: 'evaluate', codeKnown: false,
        exceptionClassKnown: known, ...(known ? { exceptionClass: name } : {}) };
    } catch {} // Diagnostic access cannot replace the original failure.
    return { ok: false, reason: error.reason || 'live-api-error', stage, mutated, ...leaseFields,
      ...(steamFailure ? { steamFailure } : {}),
      ...(settledNativeReturn ? { nativeReturn: settledNativeReturn } : {}), ...(menuTarget ? { menuTarget, keyDelivered } : {}) };
  } finally {
    if (window.__ymccSteamLiveBusy === token) delete window.__ymccSteamLiveBusy;
  }
}))YMSTEAM";
inline constexpr const char kLightGamepadScript[] =
R"YMSTEAM(// Embedded in steam_live_script.h. Regenerate/check with tools/steam_live_script_embed.mjs.
(async function ymccSteamLive(request) {
  let mutated = false, stage = 'preflight', menuTarget = null, keyDelivered = false, settledNativeReturn = null;
  const token = `${Date.now()}-${Math.random()}`;
  const lightGamepadLease = request.operation === 'input.gamepadLease';
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
  if (!lightGamepadLease && window.__ymccSteamLiveBusy) return { ok: false, reason: 'live-busy', mutated: false, stage };
  if (!lightGamepadLease) window.__ymccSteamLiveBusy = token;
  try {
    // Only the independent, account/process-bound running-link read may own a
    // transient native subscription. This flag never grants a setter authority.
    if (request.nativeFocusFeedbackRead !== undefined &&
        (typeof request.nativeFocusFeedbackRead !== 'boolean' || request.operation !== 'sleep.inspect' || request.runningLinkRead !== true))
      fail('invalid-steam-native-focus-read');
    if (request.nativeAppLifetimeFeedbackRead !== undefined &&
        (typeof request.nativeAppLifetimeFeedbackRead !== 'boolean' || request.operation !== 'sleep.inspect' || request.runningLinkRead !== true))
      fail('invalid-steam-app-lifetime-read');
    if (request.nativeRecordIdentityRead !== undefined &&
        (typeof request.nativeRecordIdentityRead !== 'boolean' || !['sleep.session.inspect','sleep.inspect'].includes(request.operation)))
      fail('invalid-steam-native-record-read');
    // A private native diagnostic identity channel. The native caller strips
    // this reply before logging/export; no SteamID is persisted in JS state.
    const readNativeRecordIdentity = () => {
      const unknown=reason=>({status:'unavailable',reason});
      if(request.nativeRecordIdentityRead!==true)return null;
      try {
        if(!Number.isInteger(request.account)||request.account<=0||request.account>0xffffffff)return unknown('native-record-ui-account-unavailable');
        const ids=request.appids;
        if(!Array.isArray(ids)||ids.length!==1||!Number.isInteger(ids[0])||ids[0]<=0||ids[0]>0xffffffff)return unknown('native-record-ui-game-unavailable');
        const id=window.settingsStore?.m_CMInterface?.steamid;
        if(!id||typeof id.GetAccountID!=='function'||typeof id.ConvertTo64BitString!=='function')return unknown('native-record-ui-account-unavailable');
        if(Function.prototype.toString.call(id.ConvertTo64BitString)!=='ConvertTo64BitString(){return this.m_ulSteamID.toString()}')return unknown('native-record-ui-identity-schema-changed');
        const account=id.GetAccountID();
        if(account!==request.account)return unknown('native-record-ui-account-unavailable');
        const full=id.ConvertTo64BitString();
        if(typeof full!=='string'||!/^[1-9][0-9]{0,19}$/.test(full)||(BigInt(full)>0xffffffffffffffffn||(BigInt(full)&0xffffffffn)!==BigInt(account)))return unknown('native-record-ui-account-unavailable');
        const map=window.appStore?.m_mapApps;
        if(!map||typeof map.get!=='function')return unknown('native-record-ui-game-unavailable');
        const unsigned=map.get(ids[0]),signed=map.get(ids[0]|0);
        if(unsigned&&signed&&unsigned!==signed)return unknown('native-record-ui-game-unavailable');
        const overview=unsigned||signed,appid=overview?.appid,gameid=overview?.gameid,type=overview?.app_type;
        if(typeof appid!=='number'||!Number.isInteger(appid)||appid< -0x80000000||appid>0xffffffff||(appid>>>0)!==ids[0]||
          typeof gameid!=='string'||!/^[1-9][0-9]{0,19}$/.test(gameid)||!Number.isInteger(type)||type<0||type>0xffffffff)return unknown('native-record-ui-game-unavailable');
        const raw=BigInt(gameid),shortcut=(type&0x40000000)!==0;
        if(raw>0xffffffffffffffffn||!((!shortcut&&raw===BigInt(ids[0])&&raw<=0xffffffn)||(shortcut&&raw>>32n===BigInt(ids[0])&&(raw&0xffffffffn)===0x02000000n)))return unknown('native-record-ui-game-unavailable');
        if(window.settingsStore?.m_CMInterface?.steamid!==id||id.GetAccountID()!==account||id.ConvertTo64BitString()!==full||overview.gameid!==gameid)return unknown('native-record-ui-identity-raced');
        return {status:'observed',identity:id,overview,steamid64:full,gameid64:gameid,appid:ids[0]};
      } catch {return unknown('native-record-ui-identity-unavailable');}
    };
    const privateRecordStart=readNativeRecordIdentity();
    const privateNativeRecordReply=()=>{
      if(request.nativeRecordIdentityRead!==true)return {};
      const end=readNativeRecordIdentity();
      if(privateRecordStart?.status!=='observed'||end?.status!=='observed')return {_ymccPrivateNativeRecord:{status:'unavailable',reason:privateRecordStart?.reason||end?.reason||'native-record-ui-identity-unavailable'}};
      if(privateRecordStart.identity!==end.identity||privateRecordStart.overview!==end.overview||privateRecordStart.steamid64!==end.steamid64||privateRecordStart.gameid64!==end.gameid64||privateRecordStart.appid!==end.appid)
        return {_ymccPrivateNativeRecord:{status:'unavailable',reason:'native-record-ui-identity-raced'}};
      return {_ymccPrivateNativeRecord:{status:'observed',source:'steam-ui-exact-native-record-identity',steamid64:end.steamid64,gameid64:end.gameid64,appid:end.appid}};
    };
    const checkAccount = () => {
      // Explicit menu keys act on Steam's current UI context, not account data.
      // This fixed operation list grants no settings, game or input-lease access.
      if (['menu.button', 'menu.toggle', 'menu.inspect', 'menu.qam.prepare',
           'menu.qam.observe', 'menu.qam.retry', 'menu.qam.dispose'].includes(request.operation)) return;
      const id = window.settingsStore?.m_CMInterface?.steamid;
      if (!id || typeof id.GetAccountID !== 'function') fail('live-api-unavailable');
      const activeAccount = id.GetAccountID();
      if (!Number.isInteger(activeAccount) || activeAccount <= 0) fail('steam-session-starting');
      if (!Number.isInteger(request.account) || activeAccount !== request.account) fail('steam-account-changed');
    };
    checkAccount();
    if (request.operation === 'input.gamepadLease') {
      stage = 'input-gamepad-lease';
      const dataField = (object, key) => {
        const descriptor = object && Object.getOwnPropertyDescriptor(object, key);
        return descriptor && 'value' in descriptor ? descriptor.value : undefined;
      };
      if (typeof request.owner !== 'string' || !/^[a-f0-9]{32}$/.test(request.owner) ||
          !Number.isSafeInteger(request.serial) || request.serial <= 0 || typeof request.enabled !== 'boolean' ||
          !Number.isSafeInteger(request.expiresAt)) fail('invalid-gamepad-lease');
      const monotonicStart = performance.now(), remaining = request.expiresAt - Date.now();
      if (!Number.isFinite(monotonicStart) || remaining <= 0 || remaining > 3000) fail('gamepad-lease-expired-or-too-long');
      const previous = dataField(window, '__ymccLightGamepadLease');
      let carried = null;
      if (previous && (typeof previous.dispose !== 'function' || typeof previous.remainingMs !== 'function' || typeof previous.receipt !== 'function'))
        fail('gamepad-lease-state-unavailable');
      if (previous) {
        const live = previous.remainingMs() > 0;
        if (previous.owner !== request.owner) {
          if (!request.enabled || live) fail('gamepad-lease-owner-mismatch');
        } else {
          if (request.serial < previous.serial) fail('gamepad-lease-stale-serial');
          if (request.serial === previous.serial) {
            if (request.enabled !== previous.requestedEnabled ||
      )YMSTEAM"
R"YMSTEAM(          (request.enabled && (request.expiresAt !== previous.expiresAt || !live))) fail('gamepad-lease-stale-serial');
            const receipt = previous.receipt();
            if (request.enabled && !receipt.enabled) {
              mutated = true;
              if (!previous.dispose()) fail('gamepad-lease-cleanup-failed');
              fail('gamepad-lease-expired');
            }
            if (!live && !previous.dispose()) { mutated = true; fail('gamepad-lease-cleanup-failed'); }
            return { ok: true, mutated, stage, ...previous.receipt() };
          }
          if (live) {
            if (!(previous.priorUp instanceof Set) || !(previous.blockedDown instanceof Set)) {
              mutated = true;
              previous.dispose();
              fail('gamepad-lease-state-unavailable');
            }
            carried = { priorUp: new Set(previous.priorUp), blockedDown: new Set(previous.blockedDown) };
          }
        }
        mutated = Boolean(previous.enabled || previous.releasing || previous.handle || previous.timer !== null || previous.listening);
        if (!previous.dispose()) fail('gamepad-lease-cleanup-failed');
      }
      const document = window.document, ui = dataField(window, 'SteamUIStore');
      const manager = dataField(ui, 'm_GamepadNavigationManager');
      const callbacks = dataField(manager, 'm_rgCatchAllGamepadInput');
      const settings = window.settingsStore, cm = settings?.m_CMInterface, identity = cm?.steamid, account = request.account;
      const lease = { owner: request.owner, serial: request.serial, expiresAt: request.expiresAt,
        requestedEnabled: request.enabled, enabled: false, releasing: false, deadline: monotonicStart + remaining,
        document, pageHidden: previous?.document === document && previous.pageHidden === true,
        priorUp: carried?.priorUp || new Set(), blockedDown: carried?.blockedDown || new Set(),
        timer: null, handle: null, listening: false, wrapper: null, dispatch: null };
      let setter, originalAction, actionDescriptor, actionOwner, inputSources, gamepadType;
      const sourceBindings = [];
      const dispatchKey = (source, controller, button) => `${source}:${controller}:${button}`;
      lease.remainingMs = () => {
        try {
          if (!(lease.enabled || lease.releasing) || lease.pageHidden || window.__ymccLightGamepadLease !== lease ||
              window.closed || !document || window.document !== document || document.defaultView !== window ||
              window.settingsStore !== settings || settings.m_CMInterface !== cm || cm.steamid !== identity ||
              identity.GetAccountID() !== account || dataField(window, 'SteamUIStore') !== ui ||
              dataField(ui, 'm_GamepadNavigationManager') !== manager || manager.SetCatchAllGamepadInput !== setter ||
              dataField(manager, 'm_rgCatchAllGamepadInput') !== callbacks || !callbacks.includes(lease.callback) ||
              manager.OnButtonActionInternal !== lease.wrapper ||
              dataField(manager, 'm_rgGamepadInputSources') !== inputSources || inputSources.length !== sourceBindings.length ||
              sourceBindings.some((binding, index) => inputSources[index] !== binding.source ||
                binding.source.GetSourceType !== binding.getType || binding.getType.call(binding.source) !== binding.type ||
                dataField(binding.source, 'm_rgControllers') !== binding.controllers)) return 0;
          const now = performance.now(), wallRemaining = lease.expiresAt - Date.now();
          return Number.isFinite(now) && Number.isFinite(wallRemaining) ? Math.max(0, Math.min(lease.deadline - now, wallRemaining)) : 0;
        } catch { return 0; }
      };
      lease.receipt = () => {
        const remainingMs = lease.remainingMs();
        return { enabled: remainingMs > 0 && lease.enabled, releasing: remainingMs > 0 && lease.releasing,
          pendingReleaseCount: remainingMs > 0 ? lease.blockedDown.size : 0, serial: lease.serial, remainingMs, scope: 'steam-gamepad-navigation' };
      };
      lease.dispose = () => {
        lease.enabled = false;
        lease.releasing = false;
        let clean = true;
        if (lease.timer !== null) {
          try { clearTimeout(lease.timer); lease.timer = null; } catch { clean = false; }
        }
        if (lease.listening) {
          try { window.removeEventListener('pagehide', lease.pagehide); lease.listening = false; } catch { clean = false; }
        }
        if (lease.handle) {
          try { lease.handle.Unregister(); lease.handle = null; } catch { clean = false; }
        }
        if (Array.isArray(callbacks)) {
          try {
            let index;
            while ((index = callbacks.indexOf(lease.callback)) >= 0) callbacks.splice(index, 1);
          } catch { clean = false; }
        }
        try {
          const currentAction = manager && Object.getOwnPropertyDescriptor(manager, 'OnButtonActionInternal');
          if (lease.wrapper && currentAction && 'value' in currentAction && currentAction.value === lease.wrapper &&
              currentAction.writable === true && currentAction.configurable === true && currentAction.enumerable === actionDescriptor.enumerable) {
            if (actionOwner === manager && actionDescriptor) Object.defineProperty(manager, 'OnButtonActionInternal', actionDescriptor);
            else if (!delete manager.OnButtonActionInternal) clean = false;
          }
        } catch { clean = false; }
        return clean;
      };
      lease.callback = (button, down, repeat) => {
        if (lease.remainingMs() <= 0) { lease.dispose(); return false; }
        const dispatch = lease.dispatch;
        return Boolean(dispatch && dispatch.button === button && dispatch.down === down && dispatch.repeat === repeat && dispatch.block);
      };
      lease.pagehide = () => { lease.pageHidden = true; lease.dispose(); };
      window.__ymccLightGamepadLease = lease;
      mutated = true;
      if (request.enabled || lease.blockedDown.size > 0) {
        try {
          setter = manager?.SetCatchAllGamepadInput;
          for (let cursor = manager; cursor; cursor = Object.getPrototypeOf(cursor)) {
            const descriptor = Object.getOwnPropertyDescriptor(cursor, 'OnButtonActionInternal');
            if (descriptor) { actionDescriptor = descriptor; actionOwner = cursor; break; }
          }
          originalAction = manager?.OnButtonActionInternal;
          inputSources = dataField(manager, 'm_rgGamepadInputSources');
          if (!document || document.defaultView !== window || lease.pageHidden || window.closed ||
              typeof window.addEventListener !== 'function' || typeof window.removeEventListener !== 'function' ||
              !Array.isArray(callbacks) || typeof setter !== 'function' || typeof originalAction !== 'function' ||
              originalAction.length !== 10 || !actionDescriptor || !('value' in actionDescriptor) || actionDescriptor.writable !== true ||
              (actionOwner === manager && actionDescriptor.configurable !== true) || !Array.isArray(inputSources) || inputSources.length === 0)
            fail('gamepad-lease-api-unavailable');
          const controllersSeen = new Set();
          for (const source of inputSources) {
            const getType = source?.GetSourceType, controllers = dataField(source, 'm_rgControllers');
            if (typeof getType !== 'function') fail('gamepad-lease-source-unavailable');
            const type = getType.call(source);
            if (!Number.isSafeInteger(type) || type < 0) fail('gamepad-lease-source-unavailable');
            sourceBindings.push({ source, getType, type, controllers });
            if (controllers !== undefined) {
              let entries;
              try { entries = [...Map.prototype.entries.call(controllers)]; } catch { fail('gamepad-lease-source-unavailable'); }
              if (gamepadType !== undefined && gamepadType !== type) fail('gamepad-lease-source-unavailable');
              gamepadType = type;
              for (const [controller, record] of entries) {
                if (!Number.isSafeInteger(controller) || controller < -1 || controllersSeen.has()YMSTEAM"
R"YMSTEAM(controller)) fail('gamepad-lease-source-unavailable');
                controllersSeen.add(controller);
                const activeButtons = dataField(record, 'activeButtons');
                if (!activeButtons || typeof activeButtons !== 'object') fail('gamepad-lease-source-unavailable');
                for (const button of Reflect.ownKeys(activeButtons)) {
                  const held = dataField(activeButtons, button), numericButton = Number(button);
                  if (typeof button !== 'string' || !/^[1-9][0-9]*$/.test(button) || !Number.isSafeInteger(numericButton) ||
                      typeof held !== 'boolean') fail('gamepad-lease-source-unavailable');
                  if (!carried && held) lease.priorUp.add(dispatchKey(type, controller, numericButton));
                }
              }
            }
          }
          if (gamepadType === undefined || sourceBindings.some(binding => binding.type === gamepadType && binding.controllers === undefined))
            fail('gamepad-lease-source-unavailable');
          lease.wrapper = function(...dispatchArgs) {
            if (this !== manager) return originalAction.apply(this, dispatchArgs);
            if (lease.remainingMs() <= 0) { lease.dispose(); return originalAction.apply(this, dispatchArgs); }
            const [down, button, source, controller, repeatValue] = dispatchArgs, repeat = repeatValue ?? false;
            let block = false;
            if (source === gamepadType) {
              if (typeof down !== 'boolean' || !Number.isSafeInteger(button) || button <= 0 ||
                  !Number.isSafeInteger(controller) || controller < -1 || typeof repeat !== 'boolean') {
                lease.dispose(); return originalAction.apply(this, dispatchArgs);
              }
              const key = dispatchKey(source, controller, button), prior = lease.priorUp.has(key), blocked = lease.blockedDown.has(key);
              if (lease.enabled) {
                block = true;
                if (down) {
                  if (!prior) lease.blockedDown.add(key);
                } else if (prior) {
                  block = false;
                  lease.priorUp.delete(key);
                } else if (blocked) {
                  block = true;
                  lease.blockedDown.delete(key);
                }
              } else if (lease.releasing) {
                if (down) {
                  block = blocked && repeat;
                  if (!block) {
                    if (!repeat) lease.blockedDown.delete(key);
                    lease.priorUp.add(key);
                  }
                } else {
                  if (blocked) {
                    block = true;
                    lease.blockedDown.delete(key);
                  }
                  lease.priorUp.delete(key);
                }
              }
            }
            const parentDispatch = lease.dispatch;
            lease.dispatch = { down, button, repeat, block };
            try { return originalAction.apply(this, dispatchArgs); }
            finally {
              lease.dispatch = parentDispatch;
              if (lease.releasing && lease.blockedDown.size === 0) lease.dispose();
            }
          };
          const wrapperDescriptor = { ...actionDescriptor, value: lease.wrapper, writable: true, configurable: true };
          Object.defineProperty(manager, 'OnButtonActionInternal', wrapperDescriptor);
          lease.handle = setter.call(manager, lease.callback);
          if (typeof lease.handle?.Unregister !== 'function' || callbacks.filter(callback => callback === lease.callback).length !== 1)
            fail('gamepad-lease-registration-failed');
          lease.listening = true;
          window.addEventListener('pagehide', lease.pagehide);
          lease.enabled = request.enabled;
          lease.releasing = !request.enabled;
          const delay = lease.remainingMs();
          if (delay <= 0 || request.expiresAt <= Date.now()) fail('gamepad-lease-expired');
          lease.timer = setTimeout(() => { lease.timer = null; lease.dispose(); }, delay);
          if (!Number.isSafeInteger(lease.timer) || lease.timer < 0) fail('gamepad-lease-timer-failed');
          if (lease.remainingMs() <= 0) fail('gamepad-lease-expired');
        } catch (error) {
          if (!lease.dispose()) fail('gamepad-lease-cleanup-failed');
          throw error;
        }
      }
      const receipt = lease.receipt();
      if ((request.enabled && !receipt.enabled) || (!request.enabled && lease.blockedDown.size > 0 && !receipt.releasing)) {
        if (!lease.dispose()) fail('gamepad-lease-cleanup-failed');
        fail('gamepad-lease-expired');
      }
      return { ok: true, mutated, stage, ...receipt };
    }
    fail('unsupported-light-gamepad-operation');
  } catch (error) {
    // A later account/interactive veto must retain a settled bounded native
    // return for diagnosis. It grants no acknowledgment, replay or acceptance.
    let leaseFields = {};
    if (request.operation === 'input.gamepadLease') {
      leaseFields = { enabled: false, releasing: false, pendingReleaseCount: 0,
        serial: Number.isSafeInteger(request.serial) && request.serial > 0 ? request.serial : 0, remainingMs: 0, scope: 'steam-gamepad-navigation' };
      try { leaseFields = window.__ymccLightGamepadLease?.receipt() || leaseFields; } catch {}
    }
    // Preserve a caught Steam exception class before the generic business reason
    // erases it. Never inspect/log its message, stack, URL, or request contents.
    let steamFailure;
    try {
      const name = error?.name;
      const known = typeof name === 'string' && ['Error','TypeError','ReferenceError','SyntaxError','RangeError','EvalError','URIError','AggregateError','DOMException'].includes(name);
      steamFailure = { schema: 1, domain: 'javascript', sourceStage: 'evaluate', codeKnown: false,
        exceptionClassKnown: known, ...(known ? { exceptionClass: name } : {}) };
    } catch {} // Diagnostic access cannot replace the original failure.
    return { ok: false, reason: error.reason || 'live-api-error', stage, mutated, ...leaseFields,
      ...(steamFailure ? { steamFailure } : {}),
      ...(settledNativeReturn ? { nativeReturn: settledNativeReturn } : {}), ...(menuTarget ? { menuTarget, keyDelivered } : {}) };
  } finally {
    if (window.__ymccSteamLiveBusy === token) delete window.__ymccSteamLiveBusy;
  }
}))YMSTEAM";
}
