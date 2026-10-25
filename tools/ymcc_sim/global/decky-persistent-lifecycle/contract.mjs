export const lifecycleContract={
 schemaVersion:1,
 features:[{
  id:'plugins.decky',persistence:'persistent',
  rationale:'Production Runtime retains an owned process/Job/worker, mirror host retains subscriptions, and client/relay retain recovery timers across Steam, power and transport events. Actual native and host/client tests measure these resources. Typed async ownership-v2 requires callback release before exact worker natural exit, original cancellation deadlines and strict parent resources; the separate v1 direct-library diagnostic retains its own verdict.',
  sourceInputs:['native/main.cpp','native/decky_sidebar_runtime.h','native/decky_sidebar_recovery.h','native/decky_sidebar_launch.h','native/decky_sidebar_health.h','src/bridge/deckyMirrorHost.ts','src/bridge/deckyMirrorRelay.ts','decky-plugin/src/mirrorClient.ts','decky-plugin/vendor/decky-loader.lock.json'],
  resources:['processes','handles','workers','timers','subscriptions'],
  evidenceFile:'decky-persistent-lifecycle/result.json',
  requiredComponents:['plugins.decky-owned-runtime-lifecycle']
 }]
};
