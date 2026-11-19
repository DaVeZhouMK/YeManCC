// One version-specific YMCC reload through Decky's existing plugin lifecycle.
// Called only by the authenticated native bootstrap; no global polling.
(value => {
  const host = window, expected = value.frontendBuildId;
  if (!/^[a-f0-9]{64}$/.test(expected || '')) return;
  // Official import scans the same registry before replacing a plugin row.
  const completeRegistry = rows => Array.isArray(rows) && rows.findIndex(row => row == null) === -1;
  // Official v3.3.0 loadPlugins waits for a future UI-mode callback. After a
  // SharedJSContext replacement that callback may already have fired. Consume
  // the public backend list/import API once; leave official Loader bytes alone.
  function recoverFirstLoad() {
    if (value.recoverFrontend !== true || !/^v[0-9]+\.[0-9]+\.[0-9]+$/.test(value.loaderVersion || '')) return;
    const key = '__YMCC_DECKY_FRONTEND_BOOT__', stamp = value.runId + ':' + expected;
    const previous = host[key];
    if (previous?.stamp === stamp) return;
    previous?.cancel?.();
    const state = host[key] = {stamp, phase:'waiting-loader', probes:0, dispatched:false};
    let timer = null, deadline = null, onReady = null, rejectRead = null, receiptOwner = null;
    const current = () => state.phase!=='canceled' && host[key] === state && host.__YMCC_DECKY_MIRROR__?.runId === value.runId && host.__YMCC_DECKY_MIRROR__?.revoked !== true && host.__YMCC_DECKY_MIRROR__?.frontendBuildId === expected;
    const onBootstrap = () => { if(!current())state.cancel(); };
    const clear = () => { if(timer!==null)host.clearTimeout(timer);if(deadline!==null)host.clearTimeout(deadline);timer=deadline=null;const reject=rejectRead;rejectRead=null;reject?.(new Error('frontend-read-retired'));if(onReady)host.removeEventListener('ymcc-decky-frontend-ready',onReady);onReady=receiptOwner=null;host.removeEventListener('ymcc-decky-bootstrap',onBootstrap);host.removeEventListener('ymcc-decky-health-stop',state.cancel);host.removeEventListener('pagehide',state.cancel); };
    state.cancel = () => { clear();state.phase='canceled'; };
    // The account watcher revokes the mirror before this event. An ordinary
    // same-grant refresh does not retire an in-flight first load.
    host.addEventListener('ymcc-decky-bootstrap',onBootstrap);
    host.addEventListener('ymcc-decky-health-stop',state.cancel);
    host.addEventListener('pagehide',state.cancel);
    const stop = phase => { clear();if(state.phase!=='canceled')state.phase=phase; };
    const reconcileReady = () => {
      if(!onReady||!current()||!fresh())return;
      if(receiptOwner){
        if(!receiptOwner())return stop('canceled');
        const rows=host.DeckyPluginLoader?.plugins;
        if(!Array.isArray(rows)||rows.filter(p=>p?.name==='ymcc-sidebar').length!==1)return;
      }
      refreshActivePanel();stop('current');
    };
    onReady = () => {
      // Decky calls the module before publishing the row, even for queued loads.
      if(receiptOwner)void Promise.resolve().then(reconcileReady).catch(()=>{});
      else reconcileReady();
    };
    host.addEventListener('ymcc-decky-frontend-ready',onReady);
    const retry = (owner = current) => {
      if(!owner())return stop('canceled');
      if(state.dispatched||state.probes>=3)return stop('failed');
      timer=host.setTimeout(()=>{void probe(owner);},state.probes===1?4000:8000);
    };
    const probe = async (owner = current) => {
      timer=null;
      if(!owner())return stop('canceled');
      if(fresh())return stop('current');
      state.probes++;
      const loader=host.DeckyPluginLoader, backend=host.DeckyBackend, socket=backend?.ws;
      // The official router may reconnect in place, replacing only its socket.
      const samePageOwner = () => current() && host.DeckyPluginLoader===loader && host.DeckyBackend===backend;
      const sameOwner = () => samePageOwner() && backend?.ws===socket && socket?.readyState===1;
      const valid = () => sameOwner() &&
        loader?.deckyState?.publicState?.()?.versionInfo?.current===value.loaderVersion && completeRegistry(loader?.plugins);
      // A retired socket cannot authorize an import. Only the same page and
      // router may use a remaining probe to read the enabled list again.
      const waitForSocket = () => {state.phase='waiting-loader';retry(samePageOwner);};
      const officialOwns = () => loader.plugins.some(p=>p?.name==='ymcc-sidebar') || loader.pluginReloadQueue?.some(p=>p?.name==='ymcc-sidebar');
      // The single official reload lock can belong to any plugin. It blocks
      // dispatch, but only a row for YMCC lets us hand off the attempt entirely.
      const waitForOfficialLock = () => {state.phase='official-load-in-progress';retry(owner);};
      try {
        // A transient official-state read failure is still before any import.
        if(!valid()||typeof loader.getPluginsFromBackend!=='function'||typeof loader.importPlugin!=='function')return retry(owner);
        if(officialOwns())return stop('official-load-in-progress');
        if(loader.reloadLock)return waitForOfficialLock();
        state.phase='reading-enabled-plugin';
        const rows=await Promise.race([Promise.resolve().then(()=>{if(!valid()||state.phase!=='reading-enabled-plugin')return;return loader.getPluginsFromBackend();}),new Promise((_,reject)=>{rejectRead=reject;deadline=host.setTimeout(()=>reject(new Error('plugin-list-timeout')),3000);})]);
        if(deadline!==null)host.clearTimeout(deadline);deadline=null;rejectRead=null;
        if(state.phase==='current')return;
        if(!samePageOwner())return stop('canceled');
        if(!sameOwner())return waitForSocket();
        if(!valid())return stop('canceled');
        if(fresh())return stop('current');
        if(officialOwns())return stop('official-load-in-progress');
        if(loader.reloadLock)return waitForOfficialLock();
        const own=Array.isArray(rows)?rows.filter(p=>p?.name==='ymcc-sidebar'):[];
        if(own.length!==1||own[0].disabled!==false)return stop('absent-or-disabled');
        const plugin=own[0];
        if(typeof plugin.version!=='string'||!plugin.version||plugin.version.length>64||![0,1].includes(plugin.load_type))return stop('invalid-plugin-contract');
        state.dispatched=true;state.phase='waiting-receipt';receiptOwner=sameOwner;
        const finish = () => {
          if(!current()||state.phase!=='waiting-receipt')return;
          if(!sameOwner())return stop('canceled');
          try { reconcileReady(); } catch {} // No verified row: retire the existing wait.
          if(state.phase==='waiting-receipt')stop('failed');
        };
        deadline=host.setTimeout(finish,10000);
        // One request, including queued/uncertain results. Never replay an import
        // merely because it returned before the ready event.
        Promise.resolve(loader.importPlugin(plugin.name,plugin.version,plugin.load_type,true,10000)).then(()=>onReady?.()).catch(finish);
        onReady?.();
        // This proves submission, not readiness. Health sampling can miss this edge.
        try { host.console?.info('[YMCC:DeckyHealth] '+JSON.stringify({schema:1,stage:'frontend-fallback-import-submitted',probe:state.probes})); } catch {}
      } catch (_) {
        if(deadline!==null)host.clearTimeout(deadline);deadline=null;rejectRead=null;
        if(!current())return stop('canceled');
        if(state.phase==='current')return;
        if(state.dispatched)return stop('failed');
        if(!samePageOwner())return stop('canceled');
        if(!sameOwner())return waitForSocket();
        retry(owner);
      }
    };
    // Bounded startup grace allows normal official loading to finish first.
    timer=host.setTimeout(()=>{void probe();},12000);
  }
  const key = '__YMCC_DECKY_FRONTEND_UPDATE__';
  const fresh = () => host.__YMCC_DECKY_FRONTEND__?.active === true && host.__YMCC_DECKY_FRONTEND__.buildId === expected;
  // Upstream #976 retains an old activePlugin object after setPlugins(). Once
  // this exact YMCC build is ready, follow the new object through the public
  // setter only if the user is already viewing YMCC; never navigate another tab.
  const refreshActivePanel = () => {
    if(!fresh()||host.__YMCC_DECKY_MIRROR__?.revoked===true||host.__YMCC_DECKY_MIRROR__?.runId!==value.runId)return;
    try {
      const loader=host.DeckyPluginLoader,state=loader?.deckyState,active=state?.publicState?.()?.activePlugin;
      if(active?.name!=='ymcc-sidebar'||typeof state.setActivePlugin!=='function'||!Array.isArray(loader.plugins))return;
      const own=loader.plugins.filter(p=>p?.name==='ymcc-sidebar');
      if(own.length===1&&active!==own[0])state.setActivePlugin('ymcc-sidebar');
    } catch {} // Optional presentation repair never changes load/retry authority.
  };

  const previous = host[key], boot = host.__YMCC_DECKY_FRONTEND_BOOT__;
  const stamp = value.runId + ':' + expected;
  // Once dispatched, only the owning wait can reconcile this grant's ready
  // event with the official row. Re-injections cannot finish a retired wait.
  if(previous?.stamp===stamp||(boot?.stamp===stamp&&(boot.phase==='canceled'||(boot.dispatched&&boot.phase!=='current'))))return;
  if (fresh()) { refreshActivePanel();previous?.cancel?.(); return; }
  if (boot?.stamp === stamp && boot.dispatched) return;
  const loader = host.DeckyPluginLoader;
  const plugins = loader?.plugins;
  const plugin = completeRegistry(plugins) ? plugins.find(item => item.name === 'ymcc-sidebar') : undefined;
  // Native-only first-load recovery checks the official enabled list first.
  if (!plugin) { recoverFirstLoad(); return; }
  if (typeof loader.importPlugin !== 'function') return;
  if (boot?.stamp === stamp) boot.cancel(); // Only one path may own this grant.
  previous?.cancel?.();
  const state = host[key] = { stamp, buildId: expected, phase: 'pending', attempts: 0, pending: false };
  let retry = null, deadline = null, onReady = null, rejectReady = null, canceled = false;
  const current = () => !canceled && host[key] === state && host.__YMCC_DECKY_MIRROR__?.runId === value.runId && host.__YMCC_DECKY_MIRROR__?.revoked !== true && host.__YMCC_DECKY_MIRROR__?.frontendBuildId === expected;
  const sameLoader = () => host.DeckyPluginLoader === loader;
  const replacementReady = () => {
    const registered=loader.plugins;
    if(!sameLoader()||!fresh()||!completeRegistry(registered))return false;
    const rows=registered.filter(row=>row.name===plugin.name);
    return rows.length===1&&rows[0]!==plugin;
  };
  const reconcile = () => {
    if(!replacementReady())return false;
    refreshActivePanel();state.phase='current';return true;
  };
  const clearWait = () => {
    if (deadline !== null) host.clearTimeout(deadline);
    deadline = null;
    if (onReady) host.removeEventListener('ymcc-decky-frontend-ready', onReady);
    onReady = null; rejectReady = null;
  };
  const onBootstrap = () => { if(!current())state.cancel(); };
  const removeStops = () => {
    host.removeEventListener('ymcc-decky-bootstrap',onBootstrap);
    host.removeEventListener('ymcc-decky-health-stop',state.cancel);
    host.removeEventListener('pagehide',state.cancel);
  };
  state.cancel = () => {
    if(canceled)return;
    canceled=true;if(state.phase!=='current')state.phase='canceled';state.pending=false;
    removeStops();
    if (retry !== null) host.clearTimeout(retry);
    retry = null;
    const reject = rejectReady; clearWait(); reject?.(new Error('frontend-update-superseded'));
  };
  // Retire page-owned waits and retries before the old view can dispatch again.
  host.addEventListener('ymcc-decky-bootstrap',onBootstrap);
  host.addEventListener('ymcc-decky-health-stop',state.cancel);
  host.addEventListener('pagehide',state.cancel);
  const run = async () => {
    retry = null;
    if (!current()) { state.cancel(); return; }
    if (state.pending) return;
    if (!sameLoader()) { state.cancel();return; }
    if (reconcile()) { removeStops();return; }
    state.pending = true; state.attempts++; state.phase = 'pending';
    let slotBusy = false;
    try {
      const ready = new Promise((resolve, reject) => {
        rejectReady = reject;
        // Decky calls the plugin's entry point before inserting the new row.
        // Let that synchronous registration finish even for an official queued load.
        onReady = () => { void Promise.resolve().then(()=>{
          if(!current())return;
          if(!sameLoader()){state.cancel();return;}
          if(replacementReady())resolve();
        }); };
        host.addEventListener('ymcc-decky-frontend-ready', onReady);
        deadline = host.setTimeout(() => reject(new Error('frontend-ready-timeout')), 10000);
      });
      await Promise.race([Promise.resolve().then(() => {
        if(!current()||!sameLoader()){state.cancel();return;}
        if(fresh()){onReady?.();return ready;}
        // A queued official load is already owned. Its return is not a ready receipt.
        if(loader.pluginReloadQueue?.some?.(p=>p?.name===plugin.name))return ready;
        if(loader.reloadLock){slotBusy=true;return;}
        const registered=loader.plugins;
        if(!completeRegistry(registered)||!registered.includes(plugin)){state.cancel();return;}
        return Promise.resolve(loader.importPlugin(plugin.name, plugin.version, plugin.loadType, true, 10000)).then(()=>{onReady?.();return ready;});
      }), ready]);
      if (current()) {
        if(!sameLoader())state.cancel();
        else if(!reconcile()&&slotBusy&&state.attempts<3){
          state.phase='waiting-official-slot';
          retry=host.setTimeout(()=>{void run();},state.attempts===1?5000:15000);
        }else if(state.phase!=='current')state.phase='failed';
      }
    } catch (_) {
      if (current()) {
        if(!sameLoader())state.cancel();
        else if(!reconcile())state.phase='failed';
        // After importPlugin was called, even rejection cannot prove no mutation.
      }
    } finally { clearWait(); state.pending = false;if(retry===null)removeStops(); }
  };
  void run();
})
