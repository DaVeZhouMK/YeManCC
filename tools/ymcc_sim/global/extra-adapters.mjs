import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';
import {assembleExtraAdapters,registrationIndexCurrent} from './extra-adapter-assembly.mjs';
import {getRouteFallbackConsumerAdapters} from './route-fallback-consumer/adapter.mjs';
import {getBatteryHostProcessOwnerAdapters} from './battery-host-process-owner/adapter.mjs';
import {getMonitorNoticeLifecycleAdapter} from './monitor-notice-lifecycle/adapter.mjs';
import {getMonitorNoticeNativeLeaseAdapter} from './monitor-notice-lifecycle/native-adapter.mjs';
import {getMonitorNoticeRealWebViewAdapter} from './monitor-notice-lifecycle/real-webview-adapter.mjs';
import {getMonitorNoticeRealRendererAdapter} from './monitor-notice-lifecycle/real-renderer-adapter.mjs';
import {getDeckyOwnedRuntimeLifecycleAdapter} from './decky-persistent-lifecycle/owned-adapter.mjs';
import {getDeckyPersistentLifecycleAdapters} from './decky-persistent-lifecycle/adapter.mjs';
import {getTypedAsyncRequestAdapters} from './decky-persistent-lifecycle/typed-async-adapter.mjs';
import { getCpuLockConsumerAdapters } from './cpulock-consumer/adapter.mjs';
import {getControllerFeatureStatusAdapters} from './controller-feature-status/adapter.mjs';
import {getQuickAppFileIconAdapters} from './quick-app-file-icons-consumer/adapter.mjs';
import {getControllerForegroundLatencyAdapters} from './controller-foreground-latency/adapter.mjs';
import {getWorkspaceNativeCommandAdapters} from './workspace-native-command-consumer/adapter.mjs';
import {getPerformanceSimulationConsumerAdapters} from './performance-simulation-consumer/adapter.mjs';
import {getTrayResidentConsumerAdapters} from './tray-resident-consumer/adapter.mjs';
import {getAdapter as getLibraryWorkspaceDatachainAdapter} from './library-workspace-datachain-consumer/adapter.mjs';
import {getSteamAppearanceDraftConsumerAdapters} from './steam-appearance-draft-consumer/adapter.mjs';
import {getSettingsDiagnosticsAdapters} from './settings-diagnostics-consumer/adapter.mjs';
import {getGyroPreviewLifecycleConsumerAdapters} from './gyro-preview-lifecycle-consumer/adapter.mjs';
import {getControllerDeviceDiagramConsumerAdapters} from './controller-device-diagram-consumer/adapter.mjs';
import {getDeckyMirrorHostConsumerAdapters} from './decky-mirror-host-consumer/adapter.mjs';
import {getGameLibraryUiConsumerAdapters} from './game-library-ui-consumers/adapter.mjs';
import {getDeckyUpdateRecoveryAdapters} from './decky-update-recovery/adapter.mjs';
import {getSleepExitInflightConsumerAdapters} from './sleep-exit-inflight-consumer/adapter.mjs';
import {getSleepSessionMarkerFileConsumerAdapters} from './sleep-session-marker-file-consumer/adapter.mjs';
import {getSleepStartupExitConsumerAdapters} from './sleep-startup-exit-consumer/adapter.mjs';
import {getBatteryUiNativeRecoveryConsumerAdapters} from './battery-ui-native-recovery-consumer/adapter.mjs';
import {getControllerCoordinatorPipeAdapters} from './controller-coordinator-pipe/adapter.mjs';
import {getXinputFeedbackLifecycleAdapters} from './xinput-feedback-lifecycle/adapter.mjs';
import {getControllerCoordinatorLifecycleAdapters} from './controller-coordinator-lifecycle/adapter.mjs';
import {getControllerStartProtectionAdapters} from './controller-start-protection/adapter.mjs';
import {getHostSdkStopAdapters} from './host-sdk-stop/adapter.mjs';
import {getControllerStartupReturnAdapters} from './controller-startup-return/adapter.mjs';
import {getSleepGamePauseConsumerAdapters} from './sleep-game-pause-consumer/adapter.mjs';
import {getHostGameOutputObservationAdapters} from './controller-host-game-output-observation/adapter.mjs';
import {getSleepSinglePidFreezeConsumerAdapters} from './sleep-single-pid-freeze-consumer/adapter.mjs';
import {getFanFailedReadIdentityAdapters} from './fan-failed-read-identity-consumer/adapter.mjs';
import {getFanAdoptedStartRefusalAdapters} from './fan-adopted-start-refusal-consumer/adapter.mjs';
import {getMarkerOwnershipConsumerAdapters} from './marker-ownership-consumer/adapter.mjs';
import {getFanCloseObservationRefusalAdapters} from './fan-close-observation-refusal-consumer/adapter.mjs';
import {getLightingUiFeedbackOrderingConsumerAdapters} from './lighting-ui-feedback-ordering-consumer/adapter.mjs';
import {getControllerGameMotorPipeConsumerAdapters} from './controller-game-motor-pipe-consumer/adapter.mjs';
import {getFanExplicitOffHostChainAdapters} from './fan-explicit-off-host-chain-consumer/adapter.mjs';
import {getSleepDirectoryResumeConsumerAdapters} from './sleep-directory-resume-consumer/adapter.mjs';
import {getControllerGameFeedbackWriterConsumerAdapters} from './controller-game-feedback-writer-consumer/adapter.mjs';
import {getBatteryNativeHostRoundtripConsumerAdapters} from './battery-native-host-roundtrip-consumer/adapter.mjs';
import {getLightingBuildDeliveryAdapters} from './lighting-component-continuity/v4/adapter.mjs';
import {getSleepRecoveryUiAdapters} from './sleep-recovery-ui-consumer/adapter.mjs';
import {getSleepNonUserWakeConsumerAdapters} from './sleep-non-user-wake-consumer/adapter.mjs';
import {getSleepNativeRecoveryConsumerAdapters} from './sleep-native-recovery-consumer/adapter.mjs';
import {getFanUnconfirmedResumeAdapters} from './fan-unconfirmed-resume-consumer/adapter.mjs';
import {getFanPendingAcquisitionAdapters} from './fan-pending-acquisition-consumer/adapter.mjs';
import {getControllerLifecyclePipeAdapters} from './controller-lifecycle-pipe/adapter.mjs';
import {getControllerRecoveryHealthAdapters} from './controller-recovery-health/adapter.mjs';
import {getMonitorDisplayFeedAdapters} from './monitor-display-feed-consumer/adapter.mjs';
import {getFanApiResponseConsumerAdapters} from './fan-api-response-consumer/adapter.mjs';
import {getControllerHostExitAdapters} from './controller-host-exit/adapter.mjs';
import {getControllerHostLifecycleAdapters} from './controller-lifecycle-completion/host-adapter.mjs';
import {getControllerLifecycleCompletionAdapters} from './controller-lifecycle-completion/adapter.mjs';
import {getControllerMotionCatalogAdapters} from './controller-motion-catalog/adapter.mjs';
import {getFanMonitorBrowserAdapters} from './fan-monitor-browser-consumer/adapter.mjs';
import {getControllerPriorityLedgerAdapters} from './controller-priority-ledger/adapter.mjs';
import {getSharedRuntimeNoHcAdapters} from './shared-runtime-no-hc/adapter.mjs';
import {getGyroCalibrationSessionAdapters} from './gyro-calibration-session/adapter.mjs';
import {getOemConsumerAdapters} from './oem-consumer-signals/adapter.mjs';
import {getOemCapabilityBindingAdapters} from './oem-capability-binding/adapter.mjs';
import {getLightingComponentContinuityAdapters} from './lighting-component-continuity/adapter.mjs';
import {getFanMonitorConsumerAdapters} from './fan-monitor-consumer/adapter.mjs';
import {getTdpRuntimeDisplayAdapters} from './tdp-runtime-display-consumer/adapter.mjs';
import {getFanApiZipRegressionAdapters} from './fan-api-zip-regression/adapter.mjs';
import {getTdpScheduleConsumerAdapters} from './tdp-schedule-consumer/adapter.mjs';
import {getTdpProtocolConsumerAdapters} from './tdp-protocol-consumer/adapter.mjs';
import {getControllerPriorityUiAdapters} from './controller-priority-ui/adapter.mjs';
import {getTdpConsumerAdapters} from './tdp-consumer-signals/adapter.mjs';
import {getControllerCatalogAdapters} from './controller-catalog/adapter.mjs';
import {getControllerPortCycleAdapters} from './controller-port-cycle/adapter.mjs';
import {getUsbipSetupAdapters} from './controller-usbip-setup/adapter.mjs';
import {getMsiSourceWindowAdapters} from './controller-msi-source-window/adapter.mjs';
import {getPnpAbsenceAdapters} from './controller-pnp-absence/adapter.mjs';
import {getMsiLateHidePendingAdapters} from './controller-msi-late-hide-pending/adapter.mjs';
import {getColdMsiArchiveAdapters} from './controller-cold-msi-archive/adapter.mjs';
import {getFocusRecoveryAdapters} from './controller-focus-recovery/adapter.mjs';
import {getPrerequisiteEnvironmentAdapters} from './controller-prerequisite-environment/adapter.mjs';
import {getControllerInputEntryAdapters} from './controller-input-entry-lifecycle/adapter.mjs';
import {getControllerPriorityAdapters} from './controller-priority/adapter.mjs';
import {getFanRuntimeMaintenanceAdapters} from './fan-runtime-maintenance/adapter.mjs';
import {getCompleteHostSelfTestAdapters} from './complete-host-selftest/adapter.mjs';
import {getCapabilityHotPathAdapters} from './capability-hotpaths/adapter.mjs';

import {getInputHostProtocolAdapters} from './inputhost-protocol/adapter.mjs';

import {getPluginUpdateConsumerAdapters} from './plugin-update-consumers/adapter.mjs';

import {getLightingCatalogAdapters} from './lighting-catalog/adapter.mjs';

import { getCapabilityCatalogAdapters } from './capability-catalog/adapter.mjs';
import {getCapabilityMaintenanceAdapters} from './capability-maintenance/adapter.mjs';

import { getFanPackageSourceAdapters } from './fan-package-source/adapter.mjs';

import {getMsiSdRearConsumerAdapters} from './msi-sd-rear-consumer/adapter.mjs';
import {getRadialNativeConsumerAdapters} from './radial-native-consumer/adapter.mjs';
import { getMotionConsumerAdapters } from './motion-consumer-signals/adapter.mjs';

import { getMachineModelAdapters } from './machine-capability-signals/adapter.mjs';

import { getMonitorInputAdapter, getMonitorFeedbackAdapter } from './monitor-input/adapter.mjs';

import { getSignalAdapters } from './signal-adapters.mjs';

import { getBatterySignalAdapters } from './battery-signals/adapter.mjs';

import { getFanGlobalAdapter } from './fan-global-adapter.mjs';

import { getInputFollowupAdapter } from './input-signals/input-followup.mjs';

import { getBackgroundAdapters } from './background-signals/adapter.mjs';

import { getMonitorAdapters } from './monitor-signals/adapter.mjs';

import { getFanOpenAdapters } from './fan-open-signals/adapter.mjs';

import { getGpuMemoryAdapters } from './gpu-memory-signals/adapter.mjs';

import { getUpdateDownloadAdapters } from './update-download-signals/adapter.mjs';

import { getMonitorOutputAdapters } from './monitor-output-signals/adapter.mjs';

import { getTouchSurfaceAdapters } from './touch-surface-signals/adapters.mjs';

import { getOneXFanCoexistenceAdapters } from './onex-fan-coexistence/adapter.mjs';

/** Additional reviewed runtime-initialization and memory-transport adapters.

 * Each adapter records its own boundary; no real device Open or OS sleep is authorized. */

export function getExtraFactories(_repoRoot,{selectedIds}={}) {

 return [()=>[...getCpuLockConsumerAdapters(_repoRoot)],
()=>[...getControllerFeatureStatusAdapters()],
()=>[getLibraryWorkspaceDatachainAdapter()],
()=>[...getSteamAppearanceDraftConsumerAdapters()],
()=>[...getSettingsDiagnosticsAdapters()],
()=>[...getGyroPreviewLifecycleConsumerAdapters()],
()=>[...getControllerDeviceDiagramConsumerAdapters()],
()=>[...[...getDeckyMirrorHostConsumerAdapters(),...getGameLibraryUiConsumerAdapters()].map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getSleepExitInflightConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getSleepSessionMarkerFileConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getSleepStartupExitConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getBatteryUiNativeRecoveryConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getBatteryNativeHostRoundtripConsumerAdapters()],
()=>[...getControllerGameFeedbackWriterConsumerAdapters({selectedIds})],
()=>[...getSleepDirectoryResumeConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getFanExplicitOffHostChainAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getControllerGameMotorPipeConsumerAdapters()],
()=>[...getLightingUiFeedbackOrderingConsumerAdapters()],
()=>[...getFanCloseObservationRefusalAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getMarkerOwnershipConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getFanAdoptedStartRefusalAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getFanFailedReadIdentityAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getSleepSinglePidFreezeConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getControllerCoordinatorPipeAdapters({selectedIds})],
()=>[...getHostGameOutputObservationAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getSleepGamePauseConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getLightingBuildDeliveryAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getSleepRecoveryUiAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getSleepNonUserWakeConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getXinputFeedbackLifecycleAdapters()],
()=>[...getControllerCoordinatorLifecycleAdapters()],
()=>[...getControllerStartProtectionAdapters()],
()=>[...getHostSdkStopAdapters()],
()=>[...getControllerStartupReturnAdapters()],
()=>[...getSleepNativeRecoveryConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getFanUnconfirmedResumeAdapters()],
()=>[...getFanPendingAcquisitionAdapters()],
()=>[...getControllerLifecyclePipeAdapters()],
()=>[...getControllerRecoveryHealthAdapters()],
()=>[...getMonitorDisplayFeedAdapters()],
()=>[...getFanApiResponseConsumerAdapters()],
()=>[...getControllerHostLifecycleAdapters()],
()=>[...getControllerHostExitAdapters()],
()=>[...getControllerLifecycleCompletionAdapters()],
()=>[...getControllerMotionCatalogAdapters()],
()=>[...getFanMonitorBrowserAdapters()],
()=>[...getControllerPriorityLedgerAdapters()],
()=>[...getControllerForegroundLatencyAdapters()],
()=>[...getSharedRuntimeNoHcAdapters()],
()=>[...getGyroCalibrationSessionAdapters()],
()=>[...getOemConsumerAdapters()],
()=>[...getOemCapabilityBindingAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getLightingComponentContinuityAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getFanMonitorConsumerAdapters()],
()=>[...getWorkspaceNativeCommandAdapters()],
()=>[...getQuickAppFileIconAdapters()],
()=>[...getTdpRuntimeDisplayAdapters()],
()=>[...getFanApiZipRegressionAdapters()],
()=>[...getTdpScheduleConsumerAdapters()],
()=>[...getTdpProtocolConsumerAdapters()],
()=>[...getControllerPriorityUiAdapters()],
()=>[...getTdpConsumerAdapters()],
()=>[...getControllerCatalogAdapters()],
()=>[...getControllerPortCycleAdapters()],
()=>[...getUsbipSetupAdapters(_repoRoot)],
()=>[...getMsiSourceWindowAdapters(_repoRoot)],
()=>[...getPnpAbsenceAdapters(_repoRoot)],
()=>[...getMsiLateHidePendingAdapters(_repoRoot)],
()=>[...getColdMsiArchiveAdapters(_repoRoot)],
()=>[...getFocusRecoveryAdapters(_repoRoot)],
()=>[...getPrerequisiteEnvironmentAdapters(_repoRoot)],
()=>[...getControllerInputEntryAdapters(_repoRoot)],
()=>[...getControllerPriorityAdapters()],
()=>[...getFanRuntimeMaintenanceAdapters()],
()=>[...getCompleteHostSelfTestAdapters()],
()=>[...getInputHostProtocolAdapters()],
()=>[...getPluginUpdateConsumerAdapters()],
()=>[...getCapabilityHotPathAdapters()],
()=>[...getLightingCatalogAdapters()],
()=>[...getCapabilityCatalogAdapters()],
()=>[...getCapabilityMaintenanceAdapters(_repoRoot)],
()=>[...getFanPackageSourceAdapters()],
()=>[...getMotionConsumerAdapters()],
()=>[...getMsiSdRearConsumerAdapters()],
()=>[...getRadialNativeConsumerAdapters().map(adapter=>({...adapter,evidenceInputs:[...new Set([...adapter.evidenceInputs,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])]}))],
()=>[...getMachineModelAdapters()],
()=>[{

  id:'fan.runtime-connection',domain:'fan',title:'Real HC DLL initialization, COM ABI and public mock fan lifecycle with broken-DLL rejection',

  kind:'powershell',path:'tools/ymcc_sim/domains/fan/fan_domain.ps1',args:['-Mode','connect','-EvidenceDir','{outDir}/fan-connection'],timeoutMs:300000,

  profiles:['desktop','full'],scope:'host-mock',

  resultFiles:['fan-connection/verification.json','fan-connection/integrity/verification.json','fan-connection/interop/result.json','fan-connection/sharpdx/result.json','fan-connection/manager-initialization/verification.json','fan-connection/connection/verification.json','fan-connection/sharpdx/result.json'],

  evidenceInputs:['tools/ymcc_sim/lib/sim_core.ps1','tools/SharpDxCallbackSelfTest/Run-SelfTest.ps1','tools/SharpDxCallbackSelfTest/SharpDxCallbackSelfTest.csproj','tools/SharpDxCallbackSelfTest/Program.cs','tools/SharpDxCallbackSelfTest/Metadata.cs','tools/SharpDxCallbackSelfTest/ManagedProbe.cs','tools/SharpDxCallbackSelfTest/Fixtures.cs','tools/SharpDxCallbackSelfTest/NuGet.Config','tools/HcManagerInitializationSelfTest/Program.cs','tools/HcManagerInitializationSelfTest/Run-SelfTest.ps1','tools/HcManagerInitializationSelfTest/HcManagerInitializationSelfTest.csproj','tools/hc-device-runtime/verify-runtime.ps1','tools/hc-device-runtime/runtime-selftest.ps1','deps/ymcc-device-core-runtime/profile.json','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Core.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Wasapi.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.DirectInput.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.Direct3D9.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/manifest.json','tools/hc-application-dependency-trim/Program.cs','tools/hc-application-dependency-trim/ComGateSelfTest.cs','tools/hc-application-dependency-trim/ApplicationTrim.csproj','tools/hc-application-dependency-trim/create-overlay.ps1','tools/hc-application-dependency-trim/test-com-gate.ps1','tools/SharpDxCallbackSelfTest/Run-SelfTest.ps1','tools/SharpDxCallbackSelfTest/Program.cs','tools/SharpDxCallbackSelfTest/Metadata.cs','tools/SharpDxCallbackSelfTest/ManagedProbe.cs','tools/SharpDxCallbackSelfTest/Fixtures.cs','tools/SharpDxCallbackSelfTest/SharpDxCallbackSelfTest.csproj','tools/SharpDxCallbackSelfTest/NuGet.Config','FanLab/real-host/Program.cs','tools/ymcc_sim/tests/fan_runtime_connection.ps1','FanLab/RuntimeDependencyContract.cs','FanLab/DeviceCoreSharedDependencies.cs','FanLab/real-host/YeManFanHost.csproj','tools/hc-device-runtime/runtime-selftest.ps1','tools/hc-application-dependency-trim/test-com-gate.ps1','tools/hc-application-dependency-trim/Program.cs','tools/hc-application-dependency-trim/ComGateSelfTest.cs','tools/hc-application-dependency-trim/ApplicationTrim.csproj'],

 }],
()=>[{

  id:'fan.parent-exit-contract',domain:'fan',title:'Actual supplied Host bounded recovery, honest exhausted budget and natural parent exit',

  kind:'powershell',path:'tools/ymcc_sim/tests/parent_exit_contract.ps1',args:['-EvidenceDir','{outDir}/parent-exit'],timeoutMs:120000,

  profiles:['desktop','full'],scope:'host-mock',

  resultFiles:['parent-exit/verification.json','parent-exit/engine/verification.json','parent-exit/process/verification.json'],

  evidenceInputs:['tools/FanFinalizeContractSelfTest/MutationFixture.cs','tools/FanFinalizeContractSelfTest/Program.cs','tools/FanFinalizeContractSelfTest/FanFinalizeContractSelfTest.csproj','tools/FanFinalizeContractSelfTest/Run-SelfTest.ps1','tools/fan_host_parent_watchdog_selftest.ps1','tools/ymcc_sim/tests/invariants.ps1','tools/ymcc_sim/lib/sim_core.ps1','FanLab/real-host/Program.cs','FanLab/real-host/HostProcessLifetime.cs'],

 }],
()=>[{

  id:'fan.hc-init-unavailable',domain:'fan',title:'Log-derived HC-unavailable refusal, post-denial 12029 and explicit retry',

  kind:'node',path:'tools/ymcc_sim/global/fan-log-regression.mjs',args:['--output','{outDir}/fan-log-result.json'],timeoutMs:30000,

  profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['fan-log-result.json'],

  evidenceInputs:['src/bridge/fanHost/startupConfiguration.ts','src/bridge/fanHost/deployment.ts','src/bridge/fanHost/capabilityAdmission.ts','src/bridge/fanHost.ts','src/bridge/fanApi.ts','src/bridge/fanDiagnostics.ts','src/bridge/fanSessionEvidence.ts','src/bridge/hardwareCoordinator.ts','src/bridge/aiFanMock.ts','src/bridge/fanHostInstallerRepair.ts','src/bridge/fanHostInstallerAsset.json','tools/fixtures/fan_no_io_host.ts'],

 }],
()=>[{

  id:'network.http-deadline',domain:'network',title:'Production HTTP/IPC deadlines and unknown POST results with NoIoHost',

  kind:'typescript',path:'tools/http_timeout_contract_selftest.ts',args:[],timeoutMs:30000,

  profiles:['desktop','full'],scope:'behavioral-mock',

  evidenceInputs:['src/bridge/api.ts','src/bridge/ipc.ts','src/bridge/fanApi.ts','src/bridge/fanHost.ts','tools/fixtures/fan_no_io_host.ts'],

 }],
()=>[{id:'lighting.promotion-acl',domain:'lighting',title:'Actual lighting promotion and rollback preserve the exact NTFS descriptor; inert owned files',kind:'powershell',path:'tools/lighting-build-candidate-selftest.ps1',args:['-ProjectRoot','.','-WorkspaceRoot','.','-PromotionAcl','-EvidenceDir','{outDir}/promotion-acl'],timeoutMs:30000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['promotion-acl/result.json'],evidenceInputs:['tools/build-lightsetter-payload.ps1','tools/lighting-build-contract.ps1']}],
()=>[getMonitorInputAdapter()],
()=>[getMonitorFeedbackAdapter()],
()=>[...getSignalAdapters(undefined,{selectedIds})],
()=>[...getBatterySignalAdapters()],
()=>[getFanGlobalAdapter()],
()=>[getInputFollowupAdapter()],
()=>[...getBackgroundAdapters()],
()=>[...getMonitorAdapters()],
()=>[...getFanOpenAdapters()],
()=>[...getGpuMemoryAdapters(undefined,{selectedIds})],
()=>[...getUpdateDownloadAdapters()],
()=>[...getDeckyUpdateRecoveryAdapters()],
()=>[...getMonitorOutputAdapters()],
()=>[...getTouchSurfaceAdapters()],
()=>[...getOneXFanCoexistenceAdapters()],
()=>[...getTrayResidentConsumerAdapters()],
()=>[...getPerformanceSimulationConsumerAdapters()],
()=>[getMonitorNoticeLifecycleAdapter()],
()=>[...getDeckyPersistentLifecycleAdapters(_repoRoot)],
()=>[getMonitorNoticeNativeLeaseAdapter(_repoRoot)],
()=>[getDeckyOwnedRuntimeLifecycleAdapter(_repoRoot)],
()=>[getMonitorNoticeRealWebViewAdapter(_repoRoot)],
()=>[getMonitorNoticeRealRendererAdapter(_repoRoot)],
()=>[...getBatteryHostProcessOwnerAdapters(_repoRoot)],
()=>[...getRouteFallbackConsumerAdapters(_repoRoot)],
()=>[...getTypedAsyncRequestAdapters(_repoRoot)]];

}

const extraIndexBytes=fs.readFileSync(new URL('./extra-adapter-index.json',import.meta.url));
if(createHash('sha256').update(extraIndexBytes).digest('hex')!=='b787bedee4fe1a5a9db155a61c207c549bdce46e13c7bb7546570f070b4ad89a')throw Error('EXTRA_REGISTRATION_INDEX_IDENTITY_CHANGED');
const extraIndex=JSON.parse(extraIndexBytes);
export function getExtraAdapters(repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..'),{selectedIds,auditReceiptIds}={}){
 const current=registrationIndexCurrent(extraIndex,repoRoot);
 return assembleExtraAdapters(getExtraFactories(repoRoot,{selectedIds}),extraIndex,{repoRoot,selectedIds,auditReceiptIds,indexCurrent:current});
}

