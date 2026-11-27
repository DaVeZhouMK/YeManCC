import {fileURLToPath} from 'node:url';
export function getControllerForegroundLatencyAdapters({win5ReturnExit2=false}={}){
    const mode=process.env.YMCC_NATIVE_FOREGROUND_MODE??'latency';
    if(!['latency','power-order','helper-exit-query'].includes(mode))throw Error('INVALID_NATIVE_FOREGROUND_MODE:'+mode);
    const powerOrder=mode==='power-order',helperQuery=mode==='helper-exit-query';
    const shieldMode=win5ReturnExit2?'prehost-writer':process.env.YMCC_FOREGROUND_SHIELD_MODE??'default';
    if(!['default','native-visibility','handoff-cycle','prehost-writer','empty-tail'].includes(shieldMode))throw Error('INVALID_FOREGROUND_SHIELD_MODE:'+shieldMode);
    const coldOnly=shieldMode==='empty-tail';
    const visibilityOnly=shieldMode==='native-visibility';
    const handoffOnly=shieldMode==='handoff-cycle';
    const preHostOnly=shieldMode==='prehost-writer';
    return [{id:'controller.native-foreground-latency',domain:'controller',
        title:helperQuery?'Original helper exit query provenance with inert process ports':powerOrder?'Native foreground power-order consumers and recorded Win5 sleep sequence':'Actual native foreground snapshot and nonblocking one-shot ownership with original latency counterexamples',
        kind:'node',path:'tools/native_foreground_latency_selftest.mjs',args:['--output','{outDir}/foreground-latency',...(powerOrder?['--power-order']:helperQuery?['--case','helper-exit-query']:[])],
        timeoutMs:240000,profiles:['desktop','full'],scope:'behavioral-mock',
        resultFiles:['foreground-latency/result.json'],evidenceInputs:[
            ...(helperQuery?[]:['native/main.cpp']),'native/msi_native_foreground_isolation_runtime.h',
            ...(powerOrder?['native/sleep_power_state.h']:[]),
            'tools/native_foreground_latency_selftest.mjs','tools/fixtures/native_foreground_snapshot/fixture.cpp',
            'tools/fixtures/native_foreground_snapshot/helper-exit-query.cpp.in',
            'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
            'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs',
            'tools/ymcc_sim/global/controller-foreground-latency/adapter.mjs',
            powerOrder?'tools/ymcc_sim/global/controller-foreground-latency/win5-power-field.json':'tools/ymcc_sim/global/controller-foreground-latency/field-record.json',
            'tools/ymcc_sim/global/controller-foreground-latency/README.md',
        ],artifactInputs:[fileURLToPath(new URL('../../../../deps/json/json.hpp',import.meta.url))],
        boundary:helperQuery?'Original poll and source-exact immediate query,reset and receipt fields;inert process/writer ports;not full Cycle or resource acceptance.':powerOrder?'Actual power consumers and lifecycle queue handoff with controlled shared facts, recorded notification order and gate counterexamples. No full maintenance, downstream writer, real sleep or device proof.':'Source-exact production consumers in Release/Checked plus the original split-query and blocking-wait failures. CLI, process launch, wait, PnP and device IO are inert. No product Main, driver, registry, Steam or sleep operation; physical continuity remains unverified.'},
        {id:'controller.foreground-shield-field',domain:'controller',title:'ROG family native-OFF root isolation, preserved HID-leaf quarantine, shared-node dedup and MSI journal continuation',
        kind:'node',path:'tools/shield_foreground_field_selftest.mjs',args:['--output','{outDir}/foreground-field',...(coldOnly?['--cold-visibility','empty-tail']:preHostOnly?[win5ReturnExit2?'--win5-return-exit2':'--prehost-writer']:handoffOnly?['--handoff-cycle']:visibilityOnly?['--native-visibility']:[])],
        timeoutMs:720000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['foreground-field/result.json'],
        lifecycleComponent:{featureId:'controller.virtual-input',parentAdapterId:'controller.signal-evidence',
            persistence:'persistent',role:'foreground-visibility-component',
            rationale:'Foreground visibility leases and pending journal jobs survive asynchronous source and power callbacks under the existing controller lifecycle owner.',
            sourceInputs:['native/main.cpp','native/native_foreground_source_policy.h','native/msi_foreground_isolation_runtime.h',
                'native/msi_native_foreground_isolation_runtime.h','native/native_visibility_cold_runtime.h','native/one_shot_helper_diagnostic.h','InputHost/MsiNativePortCycle.cs'],
            resources:['visibility-leases','pending-journal-jobs','owned-helper-handles'],
            evidenceFile:'foreground-field/result.json',fullPersistentAcceptance:false},
        evidenceInputs:['native/main.cpp','native/native_foreground_source_policy.h','native/msi_foreground_isolation_runtime.h',
            'native/msi_foreground_isolation.h','native/msi_native_foreground_isolation.h','native/msi_native_foreground_isolation_runtime.h',
            'native/one_shot_helper_diagnostic.h','InputHost/MsiNativePortCycle.cs','tools/msi_foreground_isolation_native_regression.cpp','tools/msi_native_foreground_regression.h',
            'tools/verify-msi-foreground-isolation.ps1','tools/shield_foreground_field_selftest.mjs',
            'tools/native_visibility_write_checks.h','native/native_visibility_cold_runtime.h','native/legacy_native_visibility_parser.h','native/virtual_controller_start_runtime.h','native/virtual_runtime_recovery_runtime.h',
            'tools/gpd_native_handoff_cycle_checks.h','tools/gpd_native_handoff_cycle_legacy.h',
            'tools/gpd_native_handoff_cycle_partial_before.h',
            'tools/gpd_native_handoff_cycle_legacy.source.json','tools/gpd_native_handoff_cycle_partial_before.source.json',
            'native/native_refresh_writer_runtime.h','native/native_refresh_writer_store.h','native/virtual_target_return_physical_path.h',
            'native/native_refresh_writer_record.h','native/virtual_target_return_record.h','native/virtual_target_return_store.h','native/virtual_target_return_process.h','native/hidhide_cli_job_guard.h',
            'native/virtual_controller_queued_start_runtime.h','native/virtual_start_protection.h','native/virtual_recovery_intent.h',
            ...(preHostOnly?['tools/gpd_prehost_writer_cases.ps1','tools/ymcc_sim/global/host-process.ps1',
                'tools/ymcc_sim/global/controller-foreground-latency/prehost-toolchain-inputs.json',
                'tools/ymcc_sim/global/controller-foreground-latency/prehost-header-inputs.json']:[]),
            'tools/ymcc_sim/global/controller-foreground-latency/prehost-toolchain.mjs',
            'tools/ymcc_sim/global/controller-foreground-latency/prehost-output-provenance.mjs',
            'tools/ymcc_sim/global/controller-foreground-latency/prehost-input-provenance.mjs',
            'tools/fixtures/native_visibility_cold/fixture.cpp','tools/fixtures/native_visibility_cold/run.mjs',
            'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs',
            'tools/native_game_hid_caps_selftest.mjs','tools/fixtures/native_game_hid_caps/Fixture.cs',
            'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
            'tools/ymcc_sim/global/capability-lifecycle/index.mjs',
            'tools/ymcc_sim/global/controller-foreground-latency/adapter.mjs',
            'tools/ymcc_sim/global/controller-foreground-latency/shield-field-record.json',
            'tools/ymcc_sim/global/controller-foreground-latency/README.md'],
        artifactInputs:[fileURLToPath(new URL('../../../../deps/json/json.hpp',import.meta.url))],
        boundary:win5ReturnExit2?'One Win5 pre-Host exit-2 case on the unoptimized production TU, using the original Priority probes and atomic Job. Default nine not executed; no Host, physical or full lifecycle acceptance.':preHostOnly?'Explicit unoptimized production-TU pre-Host writer selections through original Priority probes; one compile and sequential exact cases with individual atomic Jobs. Original release/helper/cold/handoff groups and old-source semantic control are not executed by this scope. No complete BUS, Host or physical acceptance.':'Actual full native TU and actual helper consumer with inert device/driver/process/registry boundaries. Original USB-only and lost-journal failures plus shared-node duplicate-refresh mutation must fail. Existing lifetime/OFF/power/identity cases retained; this component does not establish full persistent or physical acceptance.'}];
}
