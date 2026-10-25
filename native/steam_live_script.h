// Generated from steam_live.js; do not edit. Adjacent literals avoid MSVC C2026.
#pragma once
namespace ymcc::steamlive {
inline constexpr const char kScript[] =
R"YMSTEAM(// Embedded in steam_live_script.h. Regenerate/check with tools/steam_live_script_embed.mjs.
(async function ymccSteamLive(request) {
  let mutated = false, stage = 'preflight', menuTarget = null, keyDelivered = false, settledNativeReturn = null;
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
      const id = window.settingsStore?.m_CMInterface?.steamid;
      if (!id || typeof id.GetAccountID !== 'function') fail('live-api-unavailable');
      const activeAccount = id.GetAccountID();
      if (!Number.isInteger(activeAccount) || activeAccount <= 0) fail('steam-session-starting');
      if (!Number.isInteger(request.account) || activeAccount !== request.account) fail('steam-account-changed');
    };
    // Approved bounded repair: a retained document with a dead old backend must
    // not trap the product behind the no-repeat launch gate. Only an explicit repair
    // click can rebuild UI; automatic callers fail closed before any side effect.
    if (request.operation === 'decky.repairConnection' || request.operation === 'decky.recoverConnection') {
      stage = 'decky-manual-repair';
      const hex = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
      const automatic = request.operation === 'decky.recoverConnection';
      // RestartJSContext recreates the Big Picture window and may steal foreground.
      // Fail before any mode read, timer, marker or UI mutation, including stale native callers.
      if (automatic) fail('steam-ui-refresh-requires-user');
      if (Object.keys(request).length !== 5 || (automatic ? request.automatic !== true : request.userInitiated !== true) ||
          !(hex(request.expectedFrontendBuild) || request.expectedFrontendBuild === null) || !hex(request.installedFrontendBuild) ||
          !/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(request.loaderVersion || '')) fail('invalid-decky-repair-request');
      const loader = window.DeckyPluginLoader, frontend = window.__YMCC_DECKY_FRONTEND__;
      // Null is an explicit absence contract, never a fabricated module hash.
      const missing = request.expectedFrontendBuild === null;
      const ownEmpty = () => frontend == null && Array.isArray(loader?.plugins) && loader.plugins.length === 0 &&
        window.__YMCC_DECKY_MIRROR__?.frontendBuildId === request.installedFrontendBuild &&
        /^[a-f0-9]{32}$/.test(window.__YMCC_DECKY_MIRROR__?.runId || '') &&
        /^[a-f0-9]{64}$/.test(window.__YMCC_DECKY_MIRROR__?.token || '') &&
        new RegExp('^ws://127[.]0[.]0[.]1:[1-9][0-9]{0,4}/mirror$').test(window.__YMCC_DECKY_MIRROR__?.endpoint || '');
      const current = () => window === window.top && location.origin === 'https://steamloopback.host' &&
        window.DeckyPluginLoade)YMSTEAM"
R"YMSTEAM(r === loader && window.__YMCC_DECKY_FRONTEND__ === frontend &&
        (missing ? ownEmpty() : frontend?.active === true && frontend.buildId === request.expectedFrontendBuild &&
          Array.isArray(loader?.plugins) && loader.plugins.some(item => item.name === 'ymcc-sidebar')) &&
        loader.deckyState?.publicState?.()?.versionInfo?.current === request.loaderVersion;
      if (!current() || typeof SteamClient?.UI?.GetUIMode !== 'function' ||
          typeof SteamClient?.Browser?.RestartJSContext !== 'function') fail('decky-repair-context-changed');
      const mode = await bounded(SteamClient.UI.GetUIMode());
      if ((mode !== 1 && mode !== 4) || !current()) fail('decky-repair-context-changed');
      if (window.__ymccDeckyManualRepair) fail('decky-repair-already-requested');
      window.__ymccDeckyManualRepair = true;
      // A scheduled/uncertain dispatch is not a successful handshake. The native
      // owner and actual snapshot remain the only completion evidence.
      setTimeout(() => { if (current()) SteamClient.Browser.RestartJSContext(); }, 0);
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
        if(window===window.top&&location.origin==='https://steamloopback.host'&&window.DeckyPluginLoader&&prior?.active===true&&/^[a-f0-9]{64}$/.test(prior.buildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&/^[a-f0-9]{64}$/.test(bridge?.token||'')&&/^ws:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}\/mirror$/.test(bridge?.endpoint||''))
          deckyUpdateResidue={protocol:1,active:true,bridgePresent:true,frontendBuildId:prior.buildId};
        const emptyLoader=window.DeckyPluginLoader, emptyVersion=emptyLoader?.deckyState?.publicState?.()?.versionInfo?.current;
        if(window===window.top&&location.origin==='https://steamloopback.host'&&prior==null&&Array.isArray(emptyLoader?.plugins)&&emptyLoader.plugins.length===0&&/^[a-f0-9]{64}$/.test(bridge?.frontendBuildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&/^[a-f0-9]{64}$/.test(bridge?.token||'')&&new RegExp('^ws://127[.]0[.]0[.]1:[1-9][0-9]{0,4}/mirror$').test(bridge?.endpoint||'')&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(emptyVersion||''))
          deckyEmptyFrontend={protocol:1,frontendBuildId:bridge.frontendBuildId,loaderVersion:emptyVersion};
        if(deckyUpdateResidue){try{const loaded=window.DeckyPluginLoader?.deckyState?.publicState?.()?.versionInfo?.current;if(typeof loaded==='string'&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(loaded)&&loaded.length<=32)deckyUpdateResidue.loaderVersion=loaded;}catch{}}
      } catch {} // Recovery evidence is optional; a read failure never grants launch.
      return { ok: true, mode, rawMode: Number.isInteger(raw) ? raw : null, modeEpoch: watch.active ? watch.id + ':' + watch.entries : '', ...(deckyUpdateResidue?{deckyUpdateResidue}:{}), ...(deckyEmptyFrontend?{deckyEmptyFrontend}:{}), ...(typeof deckyFrontendLoaded === 'boolean'?{deckyFrontendLoaded}:{}), mutated: false, via: 'live' };
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
    if (request.operation === 'diagnostics.apps.api.read') {
      stage = 'apps-api-surface-read';
      // Read property names and descriptor kinds only. Never evaluate getters or
      // call a Steam method while discovering a possible supported contact API.
      const surface = (label, object, pattern) => {
        const rows = [], seen = new Set();
        for (let cursor = object, depth = 0; cursor && depth < 4; cursor = Object.getPrototypeOf(cursor), depth++) {
          let names = [];
          try { names = Object.getOwnPropertyNames(cursor); } catch { continue; }
          for (const name of names) {
            if (seen.has(name) || !pattern.test(name)) continue;
            seen.add(name);
            let kind = 'unknown';
            try {
              const descriptor = Object.getOwnPropertyDescriptor(cursor, name);
              kind = descriptor?.get || descriptor?.set ? 'accessor' : typeof descriptor?.value;
            } catch { kind = 'unreadable'; }
            rows.push({ name, kind, depth });
          }
        }
        rows.sort((a, b) => a.name.localeCompare(b.name));
        return { label, rows: rows.slice(0, 128), truncated: rows.length > 128 };
      };
      const pattern = /app|game|running|focus|resume|scope|launch|window|notify|register/i;
      return { ok:true, mutated:false, stage,
        surfaces: [
          surface('SteamClient.Apps', globalThis.SteamClient?.Apps, pattern),
          surface('SteamClient.System.UI', globalThis.SteamClient?.System?.UI, pattern),
          surface('SteamUIStore', window.SteamUIStore, pattern),
          surface()YMSTEAM"
R"YMSTEAM('FocusedAppWindowStore', window.FocusedAppWindowStore, pattern),
          surface('ControllerStore', window.ControllerStore, pattern),
        ],
        apiValuesNotRead:true,
      };
    }
    if (request.operation === 'diagnostics.apps.contract.read') {
      stage = 'apps-api-contract-read';
      const targets = [
        ['SteamClient.Apps', globalThis.SteamClient?.Apps, ['LaunchNonSteamApp', 'RunGame', 'RaiseWindowForGame', 'RegisterForAppOverviewChanges']],
        ['SteamClient.GameSessions', globalThis.SteamClient?.GameSessions, ['RegisterForAppLifetimeNotifications']],
        ['SteamClient.System.UI', globalThis.SteamClient?.System?.UI, ['RegisterForFocusChangeEvents', 'RegisterForOverlayGameWindowFocusChanged']],
        ['SteamUIStore', window.SteamUIStore, ['SetRunningApp', 'OnGameRunStateChanged', 'ScopeRunningApps', 'NavigateToRunningApp', 'GetWindowForRunningAppNavigation']],
      ];
      const findDescriptor = (object, name) => {
        for (let cursor = object, depth = 0; cursor && depth < 4; cursor = Object.getPrototypeOf(cursor), depth++) {
          try {
            const descriptor = Object.getOwnPropertyDescriptor(cursor, name);
            if (descriptor) return { descriptor, depth };
          } catch { return null; }
        }
        return null;
      };
      const fnv1a = text => { let hash = 2166136261; for (let i = 0; i < text.length; i++) hash = Math.imul(hash ^ text.charCodeAt(i), 16777619) >>> 0; return hash >>> 0; };
      const rows = [];
      for (const [label, object, names] of targets) for (const name of names) {
        const found = findDescriptor(object, name);
        if (!found) { rows.push({ label, name, present:false }); continue; }
        const descriptor = found.descriptor;
        if (typeof descriptor.value !== 'function') { rows.push({ label, name, present:true, depth:found.depth, descriptorKind:descriptor.get || descriptor.set ? 'accessor' : typeof descriptor.value }); continue; }
        let source = '', length = null;
        try { source = Function.prototype.toString.call(descriptor.value); length = descriptor.value.length; } catch { source = ''; }
        rows.push({ label, name, present:true, depth:found.depth, descriptorKind:'function', arity:length, sourceLength:source.length, sourceFnv1a32:fnv1a(source), sourceHead:source.slice(0,160) });
      }
      return { ok:true, mutated:false, stage, apiValuesNotRead:true, methodsNotCalled:true, rows };
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
        // Reading DesktopOverlay would lazily construct the store. Observe only
        // an existing store; never create it or register Overlay callbacks.
        const desktop=probe(()=>active?.m_DesktopOverlay);
        const gameid=probe(()=>params?.gameid),pid=probe(()=>params?.browserInfo?.m_unPID);
        return {authority:'diagnostic-only-no-admission',source:'valve-navigation-cac)YMSTEAM"
R"YMSTEAM(he-and-browser-dom',
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
                  return {appid:ids[0],index:c.index,status:info?'available':'unavailable',urlIdentity:hash,urlKind:url===null?'unknown':/(?:^|[\\/])empty\.vdf$/i.test(url)?'empty-or-passthrough':'configured',selectionType:num(info?.eSelectionType)};
                }catch{return {appid:ids[0],index:c.index,status:'unavailable'}}
 )YMSTEAM"
R"YMSTEAM(             });
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
          for(let i=0;i<5&&owner;i++,owner=Object.getPrototypeOf(owner)){
            const d=Object.getOwnPropertyDescriptor(owner,'GetOverlayInstances');
            if(d){if('value' in d&&typeof d.value==='function')method=d.value;break;}
          }
          const expected='GetOverlayInstances(Zt){let Nt=Array.from(this.m_mapOverla)YMSTEAM"
R"YMSTEAM(yPopupByPID.values()).reverse();return Nt=Nt.filter(Er=>Er.params.browserInfo.m_unAppID==Zt),Nt}';
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
            if(Map.prototype.get.call(map,targetKey)!==target.row)reject('cache-changed');
            for(let i=0;i<names.length;i++){
              const d=Object.getOwnPropertyDescriptor(target.row,names[i]);
              if(!d||!('value' in d)||!Object.is(d.value,target.raw[i]))reject('cache-changed');
            }
          };
          const take)YMSTEAM"
R"YMSTEAM(=()=>{
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
        result.observerCreatedAtUnixMs=diagnosticCount(watch.createdAtUnixMs);
        result.diagnosticCountersAvailable=watch.diagnosticCountersVersion===1&&
          [watch.rawCallbacksEntered,watch.accountUnavailableCallbacks,watch.accountMismatchCallbacks,watch.schemaInvalidEvents].every(value=>diagnosticCount(value)!==null);
  )YMSTEAM"
R"YMSTEAM(      if(result.diagnosticCountersAvailable){
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
        const nativeReturn=settledNativeReturn={source:'SteamClient.Apps.RaiseWindowForGame',kind:nativeNumber?'uint32':nativeBoolean?'boolean':typeof nativeValue==='undefined'?'undefined':'unsupported',
          observed:nativeNumber||nativeBoolean,code:nativeNumber?nativeValue:null,booleanValue:nativeBoolean?nativeValue:null,
         )YMSTEAM"
R"YMSTEAM( authority:'diagnostic-only-no-admission',nativeMeaningVerified:false,gameInputVerified:false,continueVerified:false};
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
          // Manifest-verified Valve Windows sources, inspected 2026-10-13.
          // Both derive membership from native local state; unknown bodies stay blocked.
          const knownScopes=[
            [998,2265414143,'valve-win64-1788652215'],
            [1038,4095097876,'valve-win64-1791856936']
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
          refreshed:missing,runtimeAccepted:accepted,rescopeContract,contactRecovery:{requested:contactRequested,proofAccepted:contactTargetValid,accepted:contactAccepted,readOnly:true},before,after,gameInput:'unknown',healthVerified:false};
      }
      let overlayEnabled=null;try{const v=window.settingsStore?.GetClientSetting('enable_overlay')?.[0];overlayEnabled=typeof v==='boolean'?v:null}catch{}
      // Persistent association re)YMSTEAM"
R"YMSTEAM(ads do not spend their independent 2s lane on
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
    // Passive receiver bookkeeping only. Native owns the click-only 750ms
    // deadlines; this bridge never sleeps, starts a timer or sends a second toggle
    // from an asynchronous JS callback. A lost receiver edge is not permission.
    const qamFollowup = (w, phase, ticket) => {
      const s=window.SteamUIStore, m=w?.MenuStore, now=Date.now();
      const result=(status,terminal=true,extra={})=>({status,terminal,...extra});
      const read=()=>{try{const v=m?.GetOpenSideMenu?.();return [0,1,2].includes(v)?v:null;}catch{return null;}};
      if(!w||!m||typeof w.OnQuickAccessButtonPressed!=='function')return result('receiver-unavailable');
      let state=window.__ymccQamFollowup;
      if(state?.version!==2||state.account!==request.account||state.store!==s){
        state?.dispose?.();
        state={version:2,account:request.account,store:s,entries:[],ticket:null};
        state.dispose=()=>{for(const e of state.entries)try{if(e.window.OnQuickAccessButtonPressed===e.wrapper){if(e.own)Object.defineProperty(e.window,'OnQuickAccessButtonPressed',e.own);else delete e.window.OnQuickAccessButtonPressed;}}catch{}state.entries=[];state.ticket=null;};
        window.__ymccQamFollowup=state;
      }
      let entry=state.entries.find(e=>e.window===w&&e.menu===m);
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
      }
      if(phase==='prepare')return result('receiver-prepared');
      if(!ticket||typeof ticket.id!=='string'||ticket.id.length>96||!Number.isSafeInteger(ticket.startedAt)||
          ticket.startedAt>now||now-ticket.startedAt>=3000)return result('expired');
      checkAccount();
      if(window.SteamUIStore!==s||s.GetShowingLockScreen?.()||w.IsAnyVRWindow?.()||
          (s.ActiveWindowInstance&&s.ActiveWindowInstance!==w&&
            typeof s.ActiveWindowInstance.OnQuickAccessButtonPressed==='function')||w.MenuStore!==m||
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
        if(entry.previous?.at>=ticket.startedAt)t.event=entry.previous;
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
    const passiv)YMSTEAM"
R"YMSTEAM(eQamFollowup=(w,phase,ticket)=>{
      try{return qamFollowup(w,phase,ticket);}catch{return {status:'recorder-unavailable',terminal:true};}
    };
    if(request.operation==='menu.qam.dispose'){
      checkAccount();window.__ymccQamFollowup?.dispose?.();delete window.__ymccQamFollowup;
      return {ok:true,mutated:false,qamRecovery:{status:'disposed',terminal:true},via:'live'};
    }
    if (['menu.qam.prepare','menu.qam.observe','menu.qam.retry'].includes(request.operation)) {
      checkAccount();
      const s=window.SteamUIStore,active=s?.ActiveWindowInstance;
      const w=typeof active?.OnQuickAccessButtonPressed==='function'?active:s?.WindowStore?.GamepadUIMainWindowInstance;
      const phase=request.operation.slice('menu.qam.'.length);
      stage='qam-followup';
      const recovery=qamFollowup(w,phase,request.qamFollowup);
      mutated=recovery.supplementSent===true;
      return {ok:true,mutated,qamRecovery:recovery,via:'live'};
    }
    if (['menu.button', 'menu.toggle', 'menu.inspect'].includes(request.operation)) {
      if (!['main', 'quick-access'].includes(request.menu)) fail('invalid-steam-menu');
      const s = window.SteamUIStore;
      // GLOBAL functional-key semantics. Steam, not an OS HWND/PID chosen by
      // YMCC, supplies its own routing context. Never focus or activate a window.
      const handler = request.menu === 'main' ? 'OnHomeButtonPressed' : 'OnQuickAccessButtonPressed';
      const active = s?.ActiveWindowInstance;
      const w = typeof active?.[handler] === 'function' ? active : s?.WindowStore?.GamepadUIMainWindowInstance;
      const m = w?.MenuStore;
      menuTarget = { source: w && w === active ? 'steam-active-context' : 'steam-big-picture',
        windowType: Number.isInteger(w?.WindowType) ? w.WindowType : null, isGameOverlay: !!w?.IsOverlayWindow?.(),
        handler, keyboardFallback: false, runtimeAccepted: false };
      if (!s || !w || typeof w[handler] !== 'function') fail('live-api-unavailable');
      // Keep Steam's lock/VR boundaries, but do not treat keyboard modifiers,
      // OS foreground ownership or a hidden main window as key-admission gates.
      if (s.GetShowingLockScreen?.() || w.IsAnyVRWindow?.()) fail('steam-menu-blocked');
      const readMenu = () => {
        try { const value = m?.GetOpenSideMenu?.(); return [0, 1, 2].includes(value) ? value : null; }
        catch { return null; }
      };
      // Menu observation is optional: a failed read-back is NOT a key gate.
      const before = readMenu(), menu = request.menu === 'main' ? 1 : 2;
      checkAccount();
      if (request.operation === 'menu.inspect') return { ok: true, mutated: false, before, menuTarget, via: 'live' };
      if(request.menu==='quick-access'&&request.qamFollowup)passiveQamFollowup(w,'prepare',null);
      stage = 'button'; mutated = true;
      // These are Steam's actual Guide/QAM key receivers, including its own
      // debounce, suppression and navigation behavior. Do not set menu flags.
      await bounded(w[handler]()); keyDelivered = true;
      checkAccount();
      const after = readMenu(), known = before !== null && after !== null;
      const expected = before === menu ? 0 : menu, confirmed = known && after === expected;
      menuTarget.runtimeAccepted = confirmed;
      const qamRecovery=request.menu==='quick-access'&&request.qamFollowup?passiveQamFollowup(w,'observe',request.qamFollowup):undefined;
      return { ok: true, before, menu: after, menuTarget, keyDelivered: true, ...(qamRecovery?{qamRecovery}:{}),
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
    if (ma)YMSTEAM"
R"YMSTEAM(tches.length !== 1) fail(matches.length ? 'live-controller-ambiguous' : 'live-controller-not-connected');
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
    // A later account/interactive veto must retain a settled bounded native
    // return for diagnosis. It grants no acknowledgment, replay or acceptance.
    return { ok: false, reason: error.reason || 'live-api-error', stage, mutated,
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
          deckyFrontendLoaded = !!window.deckyHasLoaded || !!window.DeckyPluginLoader;
      } catch {} // Unknown is never permissio)YMSTEAM"
R"YMSTEAM(n to risk restarting Steam's UI.
      let deckyUpdateResidue, deckyEmptyFrontend;
      try {
        const prior=window.__YMCC_DECKY_FRONTEND__,bridge=window.__YMCC_DECKY_MIRROR__;
        if(window===window.top&&location.origin==='https://steamloopback.host'&&window.DeckyPluginLoader&&prior?.active===true&&/^[a-f0-9]{64}$/.test(prior.buildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&/^[a-f0-9]{64}$/.test(bridge?.token||'')&&/^ws:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}\/mirror$/.test(bridge?.endpoint||''))
          deckyUpdateResidue={protocol:1,active:true,bridgePresent:true,frontendBuildId:prior.buildId};
        const emptyLoader=window.DeckyPluginLoader, emptyVersion=emptyLoader?.deckyState?.publicState?.()?.versionInfo?.current;
        if(window===window.top&&location.origin==='https://steamloopback.host'&&prior==null&&Array.isArray(emptyLoader?.plugins)&&emptyLoader.plugins.length===0&&/^[a-f0-9]{64}$/.test(bridge?.frontendBuildId||'')&&/^[a-f0-9]{32}$/.test(bridge?.runId||'')&&/^[a-f0-9]{64}$/.test(bridge?.token||'')&&new RegExp('^ws://127[.]0[.]0[.]1:[1-9][0-9]{0,4}/mirror$').test(bridge?.endpoint||'')&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(emptyVersion||''))
          deckyEmptyFrontend={protocol:1,frontendBuildId:bridge.frontendBuildId,loaderVersion:emptyVersion};
        if(deckyUpdateResidue){try{const loaded=window.DeckyPluginLoader?.deckyState?.publicState?.()?.versionInfo?.current;if(typeof loaded==='string'&&/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(loaded)&&loaded.length<=32)deckyUpdateResidue.loaderVersion=loaded;}catch{}}
      } catch {} // Recovery evidence is optional; a read failure never grants launch.
      return { ok: true, mode, rawMode: Number.isInteger(raw) ? raw : null, modeEpoch: watch.active ? watch.id + ':' + watch.entries : '', ...(deckyUpdateResidue?{deckyUpdateResidue}:{}), ...(deckyEmptyFrontend?{deckyEmptyFrontend}:{}), ...(typeof deckyFrontendLoaded === 'boolean'?{deckyFrontendLoaded}:{}), mutated: false, via: 'live' };
    }
  } catch (error) {
    // A later account/interactive veto must retain a settled bounded native
    // return for diagnosis. It grants no acknowledgment, replay or acceptance.
    return { ok: false, reason: error.reason || 'live-api-error', stage, mutated,
      ...(settledNativeReturn ? { nativeReturn: settledNativeReturn } : {}), ...(menuTarget ? { menuTarget, keyDelivered } : {}) };
  } finally {
    if (window.__ymccSteamLiveBusy === token) delete window.__ymccSteamLiveBusy;
  }
}))YMSTEAM";
}
