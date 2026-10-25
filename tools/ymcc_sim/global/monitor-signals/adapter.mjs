import {approvedProductFiles,tests} from './common.mjs';

// Reviewed names are static so missing/broken fixture metadata cannot break inventory.
export const CLOCK_BEFORE_ROOT='tools/ymcc_sim/global/monitor-signals/fixtures/clock-before';
export const CLOCK_BEFORE_RELATIVE_FILES=Object.freeze([
 "src/bridge/monitorData.ts",
 "src/bridge/monitorDisplayFeed.ts",
 "src/bridge/monitorHudSampleSource.ts",
 "src/bridge/monitorHudFreshness.ts",
 "src/bridge/monitorHudModel.ts",
 "src/bridge/monitorFrameTime.ts",
 "src/bridge/monitorCoreTiles.ts",
 "src/bridge/monitorHudFpsVisibility.ts",
 "src/bridge/monitorPayloadDecoder.ts",
 "src/bridge/monitorSnapshotBudget.ts",
 "src/bridge/monitorTopProjection.ts",
 "src/bridge/monitorHudPreview.ts",
 "src/bridge/monitorHudHost.ts",
 "src/bridge/steamHudRelay.ts",
 "src/bridge/monitorHudProtocol.ts",
]);
export function getMonitorAdapters(){return [{

 id:'sensor.monitor-consumer-signals',domain:'sensor',title:'Actual monitor consumer lifecycle, demand-aware clock and bounded identity',

 kind:'node',path:'tools/ymcc_sim/global/monitor-signals/index.mjs',args:['--evidence-dir','{outDir}/monitor'],timeoutMs:180000,

 profiles:['desktop','full'],blockedExitCodes:[10],scope:'behavioral-mock',resultFiles:['monitor/result.json'],

 evidenceInputs:[...tests.map(t=>t.source),...approvedProductFiles,...CLOCK_BEFORE_RELATIVE_FILES.map(file=>CLOCK_BEFORE_ROOT+'/'+file),

  ...['common.mjs','adapt.mjs','audit-runtime.mjs','worker.mjs','adapter.mjs','clock-baseline.json','review-lock.json','receipt-probe.mjs','process.mjs','runner-checks.mjs'].map(f=>'tools/ymcc_sim/global/monitor-signals/'+f)],

 referenceInputs:['src/bridge/ipc.ts','src/bridge/api.ts','src/bridge/monitorNoticeHost.ts','src/bridge/gamePolicyTarget.ts',

  'src/bridge/monitorOutput.ts','src/bridge/desktopHudOwner.ts','src/bridge/monitorHudCarrierRouter.ts','src/bridge/monitorHudClock.ts','src/bridge/monitorGameLogo.ts'],

 referenceMeaning:'IO/carrier/settings/game ports are injected. Source references are provenance only; these modules do not become executed coverage.',

 boundary:'Retained real TS factories; all IPC, collection, time, preview windows/carriers and preferences are inert inputs. Filesystem only reviewed source/baseline and owned evidence; no App, real Host, device, user settings or live game identity.',

 },
{
  "id": "sensor.hwinfo-connection",
  "domain": "sensor",
  "title": "HWiNFO collector and recovery proof select a live mapping",
  "kind": "node",
  "path": "tools/monitor_hwinfo_connection_selftest.mjs",
  "args": [
    "--out",
    "{outDir}/hwinfo"
  ],
  "timeoutMs": 120000,
  "profiles": [
    "desktop",
    "full"
  ],
  "scope": "behavioral-mock",
  "resultFiles": [
    "hwinfo/verification.json",
    "hwinfo/native-result.json"
  ],
  "evidenceInputs": [
    "native/main.cpp",
    "native/monitor_hwinfo_manager.h",
    "native/monitor_hwinfo_config.h",
    "native/monitor_hwinfo_transaction.h",
    "native/monitor_hwinfo_config_commit.h",
    "native/monitor_freshness.h",
    "native/monitor_hwinfo_recovery.h",
    "native/monitor_frame_time.h",
    "native/monitor_gpu_selection.h",
    "native/monitor_memory_network.h",
    "native/monitor_voltage.h",
    "native/monitor_text_fold.h",
    "native/monitor_low_slots.h",
    "native/monitor_parse_reuse.h",
    "deps/json/json.hpp",
    "tools/fixtures/hc_reviewed_calibration/toolchain.mjs",
    "tools/ymcc_sim/global/monitor-signals/adapter.mjs",
    "tools/monitor_hwinfo_connection_selftest.mjs",
    "tools/monitor_hwinfo_connection_selftest.cpp"
  ],
  "boundary": "Real product parser/selector or worker/collector fragments. Mapping/queue/clock/lifecycle ports are inert. No installed app, HW process, configuration, sleep or physical Xbox operation."
},
{
  "id": "sensor.hwinfo-recovery-epoch",
  "domain": "sensor",
  "title": "HWiNFO slow ongoing recovery, cancellation and shell permission admission",
  "kind": "node",
  "path": "tools/monitor_hwinfo_recovery_epoch_selftest.mjs",
  "args": [
    "--out",
    "{outDir}/hwinfo"
  ],
  "timeoutMs": 120000,
  "profiles": [
    "desktop",
    "full"
  ],
  "scope": "behavioral-mock",
  "resultFiles": [
    "hwinfo/verification.json",
    "hwinfo/native-result.json"
  ],
  "evidenceInputs": [
    "native/main.cpp",
    "native/monitor_hwinfo_manager.h",
    "native/monitor_hwinfo_config.h",
    "native/monitor_hwinfo_transaction.h",
    "native/monitor_hwinfo_config_commit.h",
    "native/monitor_freshness.h",
    "native/monitor_hwinfo_recovery.h",
    "native/monitor_frame_time.h",
    "native/monitor_gpu_selection.h",
    "native/monitor_memory_network.h",
    "native/monitor_voltage.h",
    "native/monitor_text_fold.h",
    "native/monitor_low_slots.h",
    "native/monitor_parse_reuse.h",
    "deps/json/json.hpp",
    "tools/fixtures/hc_reviewed_calibration/toolchain.mjs",
    "tools/ymcc_sim/global/monitor-signals/adapter.mjs",
    "tools/monitor_hwinfo_recovery_epoch_selftest.mjs",
    "tools/monitor_hwinfo_recovery_epoch_selftest.cpp"
  ],
  "boundary": "Real product parser/selector or worker/collector fragments. Mapping/queue/clock/lifecycle ports are inert. No installed app, HW process, configuration, sleep or physical Xbox operation."
}];}
