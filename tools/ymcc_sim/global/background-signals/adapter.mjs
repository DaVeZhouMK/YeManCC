/** Formal bridge/policy/dynamic consumers and isolated extracted native functions. */
export function getBackgroundAdapters(){return [{
 id:'display.background-signals',domain:'display',title:'Formal fixed/dynamic background consumers, power policy and exact isolated native read/clear',
 kind:'node',path:'tools/ymcc_sim/global/background-signals/index.mjs',args:['--evidence-dir','{outDir}/background'],timeoutMs:90000,
 profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['background/result.json','background/worker-result.json'],
 evidenceInputs:[
  'tools/ymcc_sim/global/background-signals/index.mjs','tools/ymcc_sim/global/background-signals/adapter.mjs',
  'tools/ymcc_sim/global/background-signals/worker.mjs','tools/ymcc_sim/global/background-signals/Run-Background.ps1',
  'tools/ymcc_sim/global/background-signals/behavior-fixture.ts','tools/ymcc_sim/global/background-signals/io-boundary.ts',
  'tools/ymcc_sim/global/host-process.ps1','tools/dynamic_background_selftest.ts','tools/default_background_selftest.mjs',
  'src/bridge/background.ts','src/bridge/backgroundPolicy.ts','src/bridge/dynamicBackground.ts','src/bridge/sharedGameMedia.ts',
  'src/bridge/gameMediaRevisionFence.ts','src/bridge/steamMediaSource.ts','src/bridge/gamedetect.ts','src/bridge/gameMediaDownloads.ts',
  'src/bridge/gameMediaPreparationPolicy.ts','src/bridge/gamePolicyHysteresis.ts','src/bridge/gamePolicyTarget.ts',
  'src/bridge/steamMediaPlaylist.ts','src/bridge/uiLifecycle.ts','src/scheduler.ts','PowerControl/GameTitlePolicy.json',
  'native/main.cpp','src/views/SettingsView.vue','deps/json/json.hpp','package.json',
 ],referenceInputs:['src/App.vue'],referenceMeaning:'App rendering/visibility and dynamic-background event consumer provenance only; full App/media DOM not executed',
 boundary:'Real product TS and real SharedGameMedia/revision fence; IPC/settings/network IO injected; extracted C++ functions use only fresh fixture files/settings. No native App/Host, user configuration, desktop wallpaper or physical sleep.',
}];}
