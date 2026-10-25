export const TOOL_AUTHORITY_SOURCES = Object.freeze([
  "tools/ymcc_sim/global/runner.mjs",
  "tools/ymcc_sim/global/bundled-node-tool-authority.mjs",
  "tools/ymcc_sim/global/snapshot-node-tools.mjs",
  "tools/ymcc_sim/global/source-snapshot.mjs",
  "tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs",
  "tools/ymcc_sim/global/execution-completion/index.mjs"
]);
export const PRODUCT_SOURCES = [
  'src/bridge/monitorNoticeHost.ts', 'src/bridge/ipc.ts', 'src/bridge/shortcutActionFeedback.ts',
  'src/bridge/monitorShortcutNotice.ts', 'src/bridge/frameRateModel.ts', 'src/bridge/updateReadyNotice.ts',
];
export const LEGACY_INPUTS = [
  'tools/ymcc_sim/global/monitor-input/adapter.mjs',
  'tools/ymcc_sim/global/monitor-input/feedback-consumer.mjs',
  'tools/ymcc_sim/global/monitor-input/notice-lifecycle-consumer.mjs',
  'tools/ymcc_sim/global/monitor-signals/adapter.mjs',
  'tools/ymcc_sim/global/monitor-signals/common.mjs',
];
export function getMonitorNoticeLifecycleAdapter() {
  return {
    id: 'lifecycle.monitor-notice-host', domain: 'lifecycle', kind: 'node',
    title: 'Actual monitor notice host ownership, destruction and persistent recovery',
    path: 'tools/ymcc_sim/global/monitor-notice-lifecycle/index.mjs',
    args: ['--evidence-dir', '{outDir}/monitor-notice-lifecycle'], timeoutMs: 90000,
    profiles: ['desktop', 'full'], scope: 'behavioral-mock',
    resultFiles: ['monitor-notice-lifecycle/result.json'],
    evidenceInputs: ['tools/ymcc_sim/global/capability-lifecycle/index.mjs', ...TOOL_AUTHORITY_SOURCES, ...PRODUCT_SOURCES, ...['index.mjs', 'memory-boundary.mjs', 'adapter.mjs', 'verify-delivery.mjs', 'README.md'].map(file => 'tools/ymcc_sim/global/monitor-notice-lifecycle/' + file)],
    referenceInputs: [...LEGACY_INPUTS, 'src/bridge/monitorHudHost.ts', 'src/monitorNoticeOutputEntry.ts', 'native/monitor_desktop_hud.h'],
    referenceMeaning: 'Existing fixtures remain unchanged; parent HUD integration, renderer and native Carrier are references, not executed coverage in this adapter.',
    lifecycleContract: { schemaVersion: 1, features: [{
      id: 'monitor.notice-host', persistence: 'persistent',
      rationale: 'startMonitorNoticeHost.ready with popupEnabled ON pre-owns an inert window before feedback; it retains that owner across receipt expiry, theme and power events until explicit OFF or stop. Short-lived receipt visibility does not make the owning host one-shot. This run observes ownership, actual event subscriptions and a feedback expiry timer.',
      sourceInputs: PRODUCT_SOURCES, resources: ['windows', 'timers', 'subscriptions'], evidenceFile: 'monitor-notice-lifecycle/result.json', requiredComponents: ['lifecycle.monitor-notice-native-lease', 'lifecycle.monitor-notice-real-webview', 'lifecycle.monitor-notice-real-renderer'],
    }] },
    boundary: 'Unmodified product TS, including actual IPC timeout/event code. Only window.chrome.webview transport, DOM event target, Date and timers are memory terminals. No native Carrier, WebView2, installed YMCC, device or OS sleep. Native forced destruction remains unverified.',
  };
}

