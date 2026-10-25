#pragma once
// Generated from decky_sidebar_frontend_update.js; run its embed tool.
namespace ymcc::deckymirror {
inline const char* frontendUpdateScript(){return R"YMCCUPDATE(// One version-specific YMCC reload through Decky's existing plugin lifecycle.
// Called only by the authenticated native bootstrap; no global polling.
(value => {
  const host = window, expected = value.frontendBuildId;
  if (!/^[a-f0-9]{64}$/.test(expected || '')) return;
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
    let timer = null, deadline = null, onReady = null;
    const current = () => host[key] === state && host.__YMCC_DECKY_MIRROR__?.runId === value.runId && host.__YMCC_DECKY_MIRROR__?.frontendBuildId === expected;
    const clear = () => { if(timer!==null)host.clearTimeout(timer);if(deadline!==null)host.clearTimeout(deadline);timer=deadline=null;if(onReady)host.removeEventListener('ymcc-decky-frontend-ready',onReady);onReady=null;host.removeEventListener('ymcc-decky-health-stop',state.cancel); };
    state.cancel = () => { clear();state.phase='canceled'; };
    host.addEventListener('ymcc-decky-health-stop',state.cancel);
    const stop = phase => { clear();state.phase=phase; };
    onReady = () => { if(current()&&fresh())stop('current'); };
    host.addEventListener('ymcc-decky-frontend-ready',onReady);
    const retry = () => {if(current()&&state.phase!=='canceled'&&!state.dispatched&&state.probes<3)timer=host.setTimeout(()=>{void probe();},state.probes===1?4000:8000);else stop('failed');};
    const probe = async () => {
      timer=null;
      if(!current()||state.phase==='canceled')return stop('canceled');
      if(fresh())return stop('current');
      state.probes++;
      const loader=host.DeckyPluginLoader, backend=host.DeckyBackend;
      const valid = () => current() && host.DeckyPluginLoader===loader && host.DeckyBackend===backend && backend?.ws?.readyState===1 &&
        loader?.deckyState?.publicState?.()?.versionInfo?.current===value.loaderVersion && Array.isArray(loader?.plugins);
      if(!valid()||typeof loader.getPluginsFromBackend!=='function'||typeof loader.importPlugin!=='function')return retry();
      if(loader.plugins.some(p=>p.name==='ymcc-sidebar') || loader.reloadLock || loader.pluginReloadQueue?.some(p=>p.name==='ymcc-sidebar'))return stop('official-load-in-progress');
      try {
        state.phase='reading-enabled-plugin';
        const rows=await Promise.race([Promise.resolve().then(()=>loader.getPluginsFromBackend()),new Promise((_,reject)=>{deadline=host.setTimeout(()=>reject(new Error('plugin-list-timeout')),3000);})]);
        if(deadline!==null)host.clearTimeout(deadline);deadline=null;
        if(!valid()||state.phase==='canceled')return stop('canceled');
        if(fresh())return stop('current');
        if(loader.plugins.some(p=>p.name==='ymcc-sidebar') || loader.reloadLock || loader.pluginReloadQueue?.some(p=>p.name==='ymcc-sidebar'))return stop('official-load-in-progress');
        const own=Array.isArray(rows)?rows.filter(p=>p?.name==='ymcc-sidebar'):[];
        if(own.length!==1||own[0].disabled!==false)return stop('absent-or-disabled');
        const plugin=own[0];
        if(typeof plugin.version!=='string'||!plugin.version||plugin.version.length>64||![0,1].includes(plugin.load_type))return stop('invalid-plugin-contract');
        state.dispatched=true;state.phase='waiting-receipt';
        deadline=host.setTimeout(()=>{if(current())stop(fresh()?'current':'failed');},10000);
        // One request, including queued/uncertain results. Never replay an import
        // merely because it returned before the ready event.
        Promise.resolve(loader.importPlugin(plugin.name,plugin.version,plugin.load_type,true,10000)).catch(()=>{if(current())stop(fresh()?'current':'failed');});
        if(fresh())stop('current');
      } catch (_) {if(deadline!==null)host.clearTimeout(deadline);deadline=null;if(state.dispatched)stop('failed');else retry();}
    };
    // Bounded startup grace allows normal official loading to finish first.
    timer=host.setTimeout(()=>{void probe();},12000);
  }
  const key = '__YMCC_DECKY_FRONTEND_UPDATE__';
  const fresh = () => host.__YMCC_DECKY_FRONTEND__?.active === true && host.__YMCC_DECKY_FRONTEND__.buildId === expected;
  const previous = host[key];
  if (fresh()) { previous?.cancel?.(); return; }
  const boot = host.__YMCC_DECKY_FRONTEND_BOOT__;
  if (boot?.stamp === value.runId + ':' + expected && boot.dispatched) return;
  const loader = host.DeckyPluginLoader;
  const plugin = loader?.plugins?.find(item => item.name === 'ymcc-sidebar');
  // Native-only first-load recovery checks the official enabled list first.
  if (!plugin) { recoverFirstLoad(); return; }
  if (typeof loader.importPlugin !== 'function') return;
  const stamp = value.runId + ':' + expected;
  if (previous?.stamp === stamp) return;
  previous?.cancel?.();
  const state = host[key] = { stamp, buildId: expected, phase: 'pending', attempts: 0, pending: false };
  let retry = null, deadline = null, onReady = null, rejectReady = null;
  const current = () => host[key] === state && host.__YMCC_DECKY_MIRROR__?.runId === value.runId && host.__YMCC_DECKY_MIRROR__?.frontendBuildId === expected;
  const clearWait = () => {
    if (deadline !== null) host.clearTimeout(deadline);
    deadline = null;
    if (onReady) host.removeEventListener('ymcc-decky-frontend-ready', onReady);
    onReady = null; rejectReady = null;
  };
  state.cancel = () => {
    if (retry !== null) host.clearTimeout(retry);
    retry = null;
    const reject = rejectReady; clearWait(); reject?.(new Error('frontend-update-superseded'));
  };
  const run = async () => {
    retry = null;
    if (!current() || state.pending) return;
    if (fresh()) { state.phase = 'current'; return; }
    state.pending = true; state.attempts++; state.phase = 'pending';
    try {
      const ready = new Promise((resolve, reject) => {
        rejectReady = reject;
        onReady = () => { if (fresh()) resolve(); };
        host.addEventListener('ymcc-decky-frontend-ready', onReady);
        deadline = host.setTimeout(() => reject(new Error('frontend-ready-timeout')), 10000);
      });
      // importPlugin may return before a queued reload starts. The receipt event
      // remains mandatory, so queue admission never masquerades as loaded code.
      await Promise.all([Promise.resolve().then(() => loader.importPlugin(plugin.name, plugin.version, plugin.loadType, true, 10000)), ready]);
      if (current()) state.phase = fresh() ? 'current' : 'failed';
    } catch (_) {
      if (current()) {
        state.phase = fresh() ? 'current' : 'failed';
        if (!fresh() && state.attempts < 3) retry = host.setTimeout(() => { void run(); }, state.attempts === 1 ? 5000 : 15000);
      }
    } finally { clearWait(); state.pending = false; }
  };
  void run();
})
)YMCCUPDATE";}
}
