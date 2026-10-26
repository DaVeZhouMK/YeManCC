import {getSignalEvidenceAdapters} from './controller-signal-evidence/adapter.mjs';


import {getMusicPlayerToolScopeAdapter} from './music-player-tool-scope/adapter.mjs';


import {knownSettingsCompletionContract} from './execution-completion/index.mjs';


/**


 * Reviewed allowlist of existing tests for desktop/full runs. No glob discovery.


 * All native, filesystem, Audio, launch and network boundaries exercised by


 * behavior tests are either inert fixtures or Node IPC that rejects outside


 * WebView2. Only explicit {outDir} arguments may write test evidence.


 *


 * Node tests keep their original location. TypeScript tests are relocatable


 * esbuild -> entry.mjs bundles with repository cwd. No in-place output is used.


 * Missing files/dependencies remain visible to the runner: never filter here.


 * Source-contract suites are deliberately not counted as behavioral coverage.


 *


 * Dependency input lists are captured from the reviewed esbuild graphs. When


 * imports change, adapters.test.mjs requires review of the new source inputs.


 */


const gyroXYZLayoutInputs = [


  "package.json",


  "src/assets.d.ts",


  "src/assets/mouse-classic.png",


  "src/views/GyroMotionView.vue",


  "src/components/GyroStatusPreview.vue",


  "src/bridge/gyroAxisFormat.ts",


  "tools/gyro_presentation_selftest.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/hcInputUtils.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/quickAppDefaults.ts",


  "src/bridge/inputStartupPolicy.ts",


  "src/bridge/settingsSnapshot.ts",


  "src/bridge/inputConfigHash.ts",


  "src/components/GyroInputOutputPreview.vue",


  "src/components/GameProfileRemoveDialog.vue",


  "src/bridge/quickActionLock.ts",


  "src/components/GameProfileModeBanner.vue",


  "src/components/Slider.vue",


  "src/components/SegButton.vue",


  "src/components/InlineIcon.vue",


  "src/bridge/inputContracts.ts",


  "src/gamepad/focus.ts",


  "src/bridge/uiLifecycle.ts",


  "src/bridge/gyroPresentation.ts",


  "src/components/Dropdown.vue",


  "src/components/GyroSimulationWindow.vue",


  "src/components/Toggle.vue",


  "src/components/IntegratedPage.vue",


  "src/components/OverflowGameName.vue",


  "src/components/ControllerDeviceDiagram.vue",


  "src/components/AppIcon.vue",


  "src/components/SwitchControl.vue",


  "src/gamepad/sliderRepeat.ts",


  "src/icons.ts",


  "src/bridge/controllerDeviceDiagram.ts",


  "src/bridge/controllerShortcutRules.ts",


  "src/components/ControllerVectorArtwork.ts",


  "src/bridge/controllerOemPresentation.ts",


  "src/gyroSimulation/model.ts",


  "src/gyroSimulation/renderer.ts",


  "src/gyroSimulation/range3d.ts",


  "src/gyroSimulation/haptics.ts",


  "src/icons.json",


  "src/bridge/controllerDiagramData.json",


  "src/gyroSimulation/motorcycle.ts",


  "src/gyroSimulation/motorcycleRenderer.ts",


  "src/components/MotorcycleHandlebar.vue",


  "src/bridge/gyroPresetSave.ts",


  "src/radial/testingState.ts",


  "src/radial/availability.ts",


  "src/styles/tokens.css",


  "src/styles/integratedConsole.css"


];





// Reviewed actual bundled imports of the explicit gyro save consumer.


const gyroPresetSaveInputs = [


  "package.json",


  "tsconfig.json",


  "src/bridge/settingsSnapshot.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/ipc.ts",


  "src/bridge/api.ts",


  "src/bridge/quickAppDefaults.ts",


  "src/bridge/inputStartupPolicy.ts",


  "src/bridge/inputConfigHash.ts",


  "src/bridge/hcInputUtils.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/tdpRuntimeState.ts",


  "src/bridge/frameRateModel.ts",


  "src/bridge/rtssOverlay.ts",


  "src/robust/repairModel.ts",


  "src/bridge/tdpCircuitBreaker.ts",


  "src/bridge/tdpStaticDiagnostics.ts",


  "src/radial/availability.ts",


  "src/radial/testingState.ts",


  "src/bridge/controllerShortcutRules.ts",


  "src/bridge/gamingHomePolicy.ts",


  "PowerControl/GameTitlePolicy.json",


  "src/bridge/uiLifecycle.ts",


  "src/scheduler.ts",


  "src/bridge/gamedetect.ts",


  "src/bridge/gamePolicyHysteresis.ts",


  "src/bridge/gamePolicyTarget.ts",


  "src/bridge/performanceControls.ts",


  "src/bridge/monitorFpsHardware.ts",


  "src/bridge/monitorFrameTime.ts",


  "src/bridge/monitorCoreTiles.ts",


  "src/bridge/monitorSnapshotSignals.ts",


  "src/bridge/monitorNativeDemand.ts",


  "src/bridge/monitorDisplayFeed.ts",


  "src/bridge/monitorAppliedTdp.ts",


  "src/bridge/monitorPayloadDecoder.ts",


  "src/bridge/monitorSnapshotBudget.ts",


  "src/bridge/monitorHudFreshness.ts",


  "src/bridge/monitorData.ts",


  "src/bridge/autofloat.ts",


  "src/bridge/frameRateLimits.ts",


  "src/bridge/yeman.ts",


  "src/bridge/powerSource.ts",


  "src/bridge/gameCorePolicy.ts",


  "src/bridge/cpuProfiles.ts",


  "src/bridge/performanceSchedule.ts",


  "src/bridge/gyroPresetModel.ts",


  "src/bridge/gameInputOverride.ts",


  "src/bridge/gameInputEditor.ts",


  "src/bridge/gyroPresetSave.ts"


];





const performanceOwnershipInputs = [


  'package.json', 'src/App.vue', 'src/bridge/controllerPersona.ts',


  'src/bridge/frameRateModel.ts', 'src/bridge/gamePolicyRuntime.ts',


  'src/bridge/performanceControls.ts', 'src/bridge/performanceSchedule.ts',


  'tools/fixtures/performance_memory_fixture.mjs',


];


const performanceEntryParityInputs = [...performanceOwnershipInputs,


  'decky-plugin/src/index.tsx', 'decky-plugin/src/PerformanceControls.tsx',


  'decky-plugin/src/performanceIntentQueue.ts', 'decky-plugin/src/performanceModel.ts',


  'src/bridge/deckyGameActions.ts', 'src/bridge/deckyMirrorHost.ts',


  'src/bridge/deckyPerformanceActions.ts', 'src/bridge/deckyProfileActions.ts',


  'src/bridge/gamePolicyHysteresis.ts', 'src/bridge/quickActionLock.ts',


  'src/bridge/virtualTargetPolicy.ts', 'src/components/ContinuousPerformancePanel.vue',


  'src/components/GameCustomProfilePanel.vue', 'src/views/PerformanceScheduleView.vue',


];





const performanceSliderDraftInputs = [


  'package.json', 'tools/lib/continuousPerformancePreview.mjs',


  'src/components/ContinuousPerformancePanel.vue', 'src/components/Slider.vue',


  'src/components/Dropdown.vue', 'src/components/AppIcon.vue', 'src/components/InlineIcon.vue',


  'src/components/OverflowGameName.vue', 'src/components/GameProfileModeBanner.vue',


  'src/components/GameProfileRemoveDialog.vue', 'src/components/PerformanceSimulationWindow.vue',


  'src/views/PerformanceScheduleView.vue', 'src/bridge/performanceControls.ts',


  'src/bridge/frameRateModel.ts', 'src/bridge/performanceSimulation.ts',


  'src/bridge/performancePanelSource.ts', 'src/bridge/quickActionLock.ts',


  'src/bridge/api.ts', 'src/bridge/ipc.ts', 'src/bridge/monitorAppliedTdp.ts', 'src/icons.ts', 'src/assets.d.ts', 'src/assets/mouse-classic.png',


  'src/gamepad/engine.ts', 'src/gamepad/sliderRepeat.ts', 'src/gamepad/performanceNavigation.ts',


  'src/gamepad/activationEvidence.ts', 'src/gamepad/focus.ts', 'src/gamepad/spatial.ts',


  'src/styles/tokens.css', 'src/styles/gamepad.css',


  'public/performance-simulation/index.html',


  'public/performance-simulation/app.js',


  'public/performance-simulation/prototype.html',


  'public/performance-simulation/style.css',


  'public/performance-simulation/schedule-controls.css',


  'public/performance-simulation/frame-work.js',


  'public/performance-simulation/frame-pipeline.js',


  'public/performance-simulation/gpu-capability-library.js',


  'public/performance-simulation/optimization-curve.js',


  'public/performance-simulation/ts-power-curve.js',


  'public/performance-simulation/gpu-capability-chart.js',


  'public/performance-simulation/gpu-capability-chart.css',


  'public/performance-simulation/monitor-hud-base.css',


  'public/performance-simulation/simulation-monitor.css',


  'public/performance-simulation/split-layout.js',


  'public/performance-simulation/split-layout.css',


];





const performanceCpuStateInputs = [


  "package.json",


  "src/bridge/monitorCoreTiles.ts",


  "src/bridge/monitorFrameTime.ts",


  "src/bridge/monitorHudFpsVisibility.ts",


  "src/bridge/monitorHudFreshness.ts",


  "src/bridge/monitorHudModel.ts",


  "src/bridge/monitorNativeDemand.ts",


  "src/bridge/monitorSnapshotSignals.ts",


  "tsconfig.json",


];





const performanceCoreTilesInputs = [


  "package.json",


  "src/bridge/api.ts",


  "src/bridge/ipc.ts",


  "src/bridge/monitorAppliedTdp.ts",


  "src/bridge/monitorCoreTiles.ts",


  "src/bridge/monitorData.ts",


  "src/bridge/monitorDisplayFeed.ts",


  "src/bridge/monitorFpsHardware.ts",


  "src/bridge/monitorFrameTime.ts",


  "src/bridge/monitorHudFpsVisibility.ts",


  "src/bridge/monitorHudFreshness.ts",


  "src/bridge/monitorHudModel.ts",


  "src/bridge/monitorNativeDemand.ts",


  "src/bridge/monitorPayloadDecoder.ts",


  "src/bridge/monitorSnapshotBudget.ts",


  "src/bridge/monitorSnapshotSignals.ts",


  "src/bridge/tdpRuntimeState.ts",


  "tsconfig.json",


];





const audioMusicPlayerInputs = [


  "public/icons/folder.svg",


  "public/icons/music.svg",


  "public/icons/mute.svg",


  "public/icons/next.svg",


  "public/icons/pause.svg",


  "public/icons/play.svg",


  "public/icons/prev.svg",


  "public/icons/refresh.svg",


  "public/icons/volume.svg",


  "src/bridge/music.ts",


  "src/icons.json",


  "src/views/QuickAppView.vue",


  "tools/gen-icons.mjs",


];





const sensorInputContractsInputs = [


  "package.json",


  "src/bridge/api.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/hcInputUtils.ts",


  "src/bridge/inputConfigHash.ts",


  "src/bridge/inputContracts.ts",


  "src/bridge/inputStartupPolicy.ts",


  "src/bridge/ipc.ts",


  "src/bridge/quickAppDefaults.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/settingsSnapshot.ts",


  "tsconfig.json",


];





const settingsCasInputs = [


  "package.json",


  "src/bridge/api.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/hcInputUtils.ts",


  "src/bridge/inputConfigHash.ts",


  "src/bridge/inputStartupPolicy.ts",


  "src/bridge/ipc.ts",


  "src/bridge/quickAppDefaults.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/settingsSnapshot.ts",


  "tsconfig.json",


];





const settingsReactivityInputs = [


  "PowerControl/GameTitlePolicy.json",


  "package.json",


  "src/bridge/api.ts",


  "src/bridge/autofloat.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/controllerShortcutRules.ts",


  "src/bridge/cpuProfiles.ts",


  "src/bridge/frameRateLimits.ts",


  "src/bridge/frameRateModel.ts",


  "src/bridge/gameCorePolicy.ts",


  "src/bridge/gamePolicyHysteresis.ts",


  "src/bridge/gamePolicyTarget.ts",


  "src/bridge/gamedetect.ts",


  "src/bridge/gamingHomePolicy.ts",


  "src/bridge/hcInputUtils.ts",


  "src/bridge/inputConfigHash.ts",


  "src/bridge/inputStartupPolicy.ts",


  "src/bridge/ipc.ts",


  "src/bridge/monitorAppliedTdp.ts",


  "src/bridge/monitorCoreTiles.ts",


  "src/bridge/monitorData.ts",


  "src/bridge/monitorDisplayFeed.ts",


  "src/bridge/monitorFpsHardware.ts",


  "src/bridge/monitorFrameTime.ts",


  "src/bridge/monitorHudFreshness.ts",


  "src/bridge/monitorNativeDemand.ts",


  "src/bridge/monitorPayloadDecoder.ts",


  "src/bridge/monitorSnapshotBudget.ts",


  "src/bridge/monitorSnapshotSignals.ts",


  "src/bridge/performanceControls.ts",


  "src/bridge/performanceSchedule.ts",


  "src/bridge/powerSource.ts",


  "src/bridge/quickAppDefaults.ts",


  "src/bridge/rtssOverlay.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/settingsSnapshot.ts",


  "src/bridge/tdpCircuitBreaker.ts",


  "src/bridge/tdpRuntimeState.ts",


  "src/bridge/tdpStaticDiagnostics.ts",


  "src/bridge/uiLifecycle.ts",


  "src/bridge/yeman.ts",


  "src/robust/repairModel.ts",


  "src/scheduler.ts",


  "tools/mock-shell.ts",


  "tsconfig.json",


];





const pluginGameActionsInputs = [


  "PowerControl/GameTitlePolicy.json",


  "decky-plugin/src/performanceModel.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/deckyGameActions.ts",


  "src/bridge/frameRateModel.ts",


  "src/bridge/gamePolicyHysteresis.ts",


  "src/bridge/gamedetect.ts",


  "src/bridge/performanceControls.ts",


  "src/bridge/performanceSchedule.ts",


  "src/bridge/quickActionLock.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/settingsSnapshot.ts",


  "src/bridge/virtualTargetPolicy.ts",


  "tools/fixtures/decky_game_memory_store.mjs",


];





const gameLibraryMediaShortcutsInputs = [


  "PowerControl/GameTitlePolicy.json",


  "package.json",


  "src/bridge/api.ts",


  "src/bridge/backgroundPosition.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/gameMediaDownloads.ts",


  "src/bridge/gameMediaPreparationPolicy.ts",


  "src/bridge/gamePolicyHysteresis.ts",


  "src/bridge/gamePolicyTarget.ts",


  "src/bridge/gamedetect.ts",


  "src/bridge/hcInputUtils.ts",


  "src/bridge/inputConfigHash.ts",


  "src/bridge/inputStartupPolicy.ts",


  "src/bridge/ipc.ts",


  "src/bridge/quickAppDefaults.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/settingsSnapshot.ts",


  "src/bridge/sharedGameMedia.ts",


  "src/bridge/steamHttp.ts",


  "src/bridge/steamMediaPlaylist.ts",


  "src/bridge/steamMediaSource.ts",


  "src/bridge/uiLifecycle.ts",


  "src/bridge/uiSettings.ts",


  "src/scheduler.ts",


  "tsconfig.json",


];





const gameLibraryMediaDownloadsInputs = [


  "package.json",


  "src/bridge/api.ts",


  "src/bridge/backgroundPosition.ts",


  "src/bridge/controllerPersona.ts",


  "src/bridge/gameMediaDownloads.ts",


  "src/bridge/hcInputUtils.ts",


  "src/bridge/inputConfigHash.ts",


  "src/bridge/inputStartupPolicy.ts",


  "src/bridge/ipc.ts",


  "src/bridge/quickAppDefaults.ts",


  "src/bridge/settingsRepository.ts",


  "src/bridge/settingsSnapshot.ts",


  "src/bridge/uiSettings.ts",


  "tsconfig.json",


];





const performanceGameProfileEditorInputs = [


  "src/bridge/controllerPersona.ts",


  "src/bridge/frameRateModel.ts",


  "src/bridge/gameInputEditor.ts",


  "src/bridge/gameInputOverride.ts",


  "src/bridge/gyroPresetModel.ts",


  "src/bridge/performanceControls.ts",


  "src/bridge/performanceSchedule.ts",


  "src/bridge/settingsSnapshot.ts",


  "src/bridge/virtualTargetPolicy.ts",


  "src/bridge/virtualTargetPolicyState.ts",


];





const reviewedImportAdditions = Object.freeze({


  'controller.persona': ['src/radial/availability.ts','src/radial/testingState.ts'],


  'controller.mixed-inputs': ['src/radial/availability.ts','src/radial/testingState.ts'],


  'settings.reactivity': ['src/radial/availability.ts','src/radial/testingState.ts'],


});





const make = (id, domain, title, kind, path, evidenceInputs, options = {}) => {


  const completionContract=knownSettingsCompletionContract(id);


  return {id,domain,title,kind,path,args:[],timeoutMs:30000,


    profiles:['desktop','full'],scope:'behavioral-mock',


    evidenceInputs:[...evidenceInputs,...(reviewedImportAdditions[id]??[]),...(completionContract?['tools/ymcc_sim/global/execution-completion/index.mjs','tools/ymcc_sim/global/runner.mjs']:[])],


    ...(completionContract?{completionContract}:{}),...options};


};





/** repoRoot is runner-owned; inventory is stable even if required files go missing. */


export function getAdapters(_repoRoot) {


  return [


    make('controller.usbip-upgrade-observation','controller','Native original upgrade observation final-exit race, cached UI latency, coalescing and stop gates','node',


      'tools/usbip_upgrade_robustness_native_selftest.mjs',['native/main.cpp','native/gamepad_driver_setup.h','native/gamepad_driver_setup_notice.h','native/process_efficiency.h','native/process_efficiency_inventory.h','tools/usbip_upgrade_robustness_native_selftest.cpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'],


      {args:['{outDir}'],resultFiles:['usbip-robustness-native.json'],timeoutMs:120000}),


    make('controller.usbip-version-upgrade','controller','Exact HIDM paired USBIP newer-version upgrade, refusal and original metadata consumer','node',


      'tools/usbip_upgrade_selftest.mjs',['GamepadPrerequisites/UsbipUpgrade.cs','GamepadPrerequisites/UsbipUpgradeSelfTest.cs','GamepadPrerequisites/AutoSetup.cs','GamepadPrerequisites/ModulePayloadBootstrap.cs','GamepadPrerequisites/Program.cs','GamepadPrerequisites/HidMaestroPackagePreparation.cs','GamepadPrerequisites/YeManGamepadPrerequisites.csproj','native/ProcessEfficiency.cs','native/UsbipMaintenanceState.cs','native/ModuleMaintenanceGate.cs','InputHost/UsbipMaintenanceGate.cs','deps/hidmaestro/HIDMaestro.Core.dll'],


      {args:['{outDir}'],resultFiles:['usbip-upgrade-result.json'],timeoutMs:120000}),


    ...getSignalEvidenceAdapters(_repoRoot),


    make('settings.boot-mode-startup-boundary', 'settings', 'Windows boot identity preserves current input across YMCC restart, retires dead owners and delays next-scene input; actual native JSON consumer and process-replay negative', 'node',


      'tools/boot_mode_startup_selftest.mjs', ['native/main.cpp','native/boot_mode_identity.h','native/input_startup_policy.h','native/controller_persona_settings.h','native/gpu_dedicated_memory_runtime.h','tools/input_startup_policy_native_selftest.cpp','tools/boot_modes_fixture/harness.mjs'],


      {args:['{outDir}'],resultFiles:['boot-mode-startup-native.json'],timeoutMs:60000}),


    make('settings.boot-modes-runtime', 'settings', 'Startup-only boot runtime: reject hot apply, native Steam guard, deferred mouse event handoff, live shortcut cache, CAS and rollback through memory native owners', 'node',


      'tools/boot_modes_selftest.mjs', [


        "native/main.cpp",


        "package.json",


        "src/bridge/gameproc.ts",


        "src/bridge/ipc.ts",


        "src/bridge/yeman.ts",
        "src/bridge/gamingHomePolicy.ts",


        "src/bridge/steamUi.ts",


        "src/bridge/bootModeModel.ts",


        "src/bridge/screenTouchpadDefaults.ts",


        "src/bridge/bootModes.ts",


        "src/bridge/desktopModeStartup.ts",


        "src/bridge/controllerPersona.ts",


        "src/bridge/controllerShortcutRules.ts",


        "src/bridge/frameRateModel.ts",


        "src/bridge/inputStartupPolicy.ts",


        "src/bridge/monitorCoreTiles.ts",


        "src/bridge/monitorFrameTime.ts",


        "src/bridge/monitorHudFpsVisibility.ts",


        "src/bridge/monitorHudFreshness.ts",


        "src/bridge/monitorHudModel.ts",


        "src/bridge/monitorNativeDemand.ts",


        "src/bridge/monitorSnapshotSignals.ts",


        "src/bridge/performanceControls.ts",


        "src/radial/availability.ts",


        "src/radial/testingState.ts",


        "tools/boot_modes_fixture/harness.mjs",


        "tools/boot_modes_fixture/runtimePorts.ts"


      ],


      {args:['{outDir}'],resultFiles:['boot-modes-runtime.json'],timeoutMs:60000}),


    make('settings.boot-modes-browser', 'settings', 'Startup-only actual PowerView boot settings and original gamepad engine at 1280x720/800: reboot-only saves, revisions, renderer remount and modal focus replay', 'node',


      'tools/boot_modes_browser_selftest.mjs', [


        "package.json",


        "public/boot-modes/steam.jpg",


        "public/boot-modes/xbox.jpg",


        "public/boot-modes/xbox.svg",


        "src/App.vue",


        "src/assets/mouse-classic.png",


        "src/bridge/bootModeModel.ts",


        "src/bridge/screenTouchpadDefaults.ts",


        "src/bridge/bootModes.ts",


        "src/bridge/desktopModeStartup.ts",


        "src/bridge/controllerDeviceDiagram.ts",


        "src/bridge/controllerDiagramData.json",


        "src/bridge/controllerOemPresentation.ts",


        "src/bridge/controllerPersona.ts",


        "src/bridge/controllerPressFeedback.ts",


        "src/bridge/controllerShortcutRules.ts",


        "src/bridge/controllerShortcutStorage.ts",


        "src/bridge/frameRateModel.ts",


        "src/bridge/gameQuickSession.ts",


        "src/bridge/gamepadPresentation.ts",


        "src/bridge/gyroPresetModel.ts",


        "src/bridge/inputOwnerRuntime.ts",


        "src/bridge/inputStartupPolicy.ts",


        "src/bridge/keyboardGlyphAssets.ts",


        "src/bridge/keyboardKeyLayout.ts",


        "src/bridge/keyboardPreviewAsset.ts",


        "src/bridge/monitorCoreTiles.ts",


        "src/bridge/monitorFrameTime.ts",


        "src/bridge/monitorHudFpsVisibility.ts",


        "src/bridge/monitorHudFreshness.ts",


        "src/bridge/monitorHudModel.ts",


        "src/bridge/monitorLayout.ts",


        "src/bridge/monitorNativeDemand.ts",


        "src/bridge/monitorSnapshotSignals.ts",


        "src/bridge/mouseModePresentation.ts",


        "src/bridge/mouseModeState.ts",


        "src/bridge/performanceControls.ts",


        "src/bridge/residentNavigation.ts",


        "src/bridge/screenTouchpads.ts",


        "src/bridge/settingsSnapshot.ts",


        "src/bridge/steamAppearance.ts",


        "src/components/AppIcon.vue",


        "src/components/BootModeFields.vue",


        "src/components/BootModeWorkbench.vue",


        "src/components/BootModesPanel.vue",


        "src/components/ControllerActionControls.vue",


        "src/components/ControllerComboInput.vue",


        "src/components/ControllerDeviceDiagram.vue",


        "src/components/ControllerEditorGlobalActions.vue",


        "src/components/ControllerKeyboardPreview.vue",


        "src/components/ControllerOutputAction.vue",


        "src/components/ControllerOutputKeyboard.vue",


        "src/components/ControllerVectorArtwork.ts",


        "src/components/Dropdown.vue",


        "src/components/GameRecognitionControl.vue",


        "src/components/GamepadVisualizer.vue",


        "src/components/InlineIcon.vue",


        "src/components/IntegratedPage.vue",


        "src/components/KeyboardKeyGlyph.vue",


        "src/components/NavRail.vue",


        "src/components/OverflowGameName.vue",


        "src/components/ScreenTouchpadsSettings.vue",


        "src/components/SegButton.vue",


        "src/components/SettingsSectionIcon.vue",


        "src/components/SettingsSections.vue",


        "src/components/Slider.vue",


        "src/components/SwitchControl.vue",


        "src/components/Toggle.vue",


        "src/components/TopMonitorBar.vue",


        "src/composables/useKeyboardPressFeedback.ts",


        "src/composables/useShortcutPreviewFocus.ts",


        "src/gamepad/activationEvidence.ts",


        "src/gamepad/engine.ts",


        "src/gamepad/focus.ts",


        "src/gamepad/performanceNavigation.ts",


        "src/gamepad/serial.ts",


        "src/gamepad/sliderRepeat.ts",


        "src/gamepad/spatial.ts",


        "src/icons.json",


        "src/icons.ts",


        "src/radial/availability.ts",


        "src/radial/testingState.ts",


        "src/robust/frontendDiagnostics.ts",


        "src/robust/repairModel.ts",


        "src/router.ts",


        "src/styles/gamepad.css",


        "src/styles/integratedConsole.css",


        "src/styles/tokens.css",


        "src/version.ts",


        "src/views/ControllerShortcutEditorView.vue",


        "src/views/PowerView.vue",


        "tools/boot_modes_fixture/browserEntry.ts",


        "tools/boot_modes_fixture/browserNative.ts",


        "tools/boot_modes_fixture/browserPorts.ts",


        "tools/boot_modes_fixture/buildBrowser.mjs",


        "tools/boot_modes_fixture/harness.mjs",


        "tools/boot_modes_fixture/runtimePorts.ts"


      ],


      {args:['{outDir}'],resultFiles:['boot-modes-browser.json'],timeoutMs:60000}),





    make('settings.boot-modes-file-ui', 'settings', 'Four-mode actual UI -> native real-file configuration and first-next-boot consumer at 720/800; absent, legacy, all 16 transitions, custom, failure and focus-return requirements', 'node',


      'tools/boot_modes_file_ui_selftest.mjs', [


        "PowerControl/settings-baseline.json",


        "PowerControl/settings-defaults-v1.json",
        "tools/boot_modes_fixture/fixtures/win5-unconfigured-20261025.json",


        "deps/json/json.hpp",


        "native/controller_persona.h",


        "native/controller_persona_settings.h",


        "native/input_startup_policy.h",


        "native/main.cpp",


        "native/settings_baseline.h",


        "package.json",


        "public/boot-modes/steam.jpg",


        "public/boot-modes/xbox-steam.jpg",


        "public/boot-modes/xbox.jpg",


        "public/boot-modes/xbox.svg",


        "src/App.vue",


        "src/assets/mouse-classic.png",


        "src/bridge/bootModeModel.ts",


        "src/bridge/bootModes.ts",


        "src/bridge/desktopModeStartup.ts",


        "src/bridge/controllerDeviceDiagram.ts",


        "src/bridge/controllerDiagramData.json",


        "src/bridge/controllerOemPresentation.ts",


        "src/bridge/controllerPersona.ts",


        "src/bridge/controllerPressFeedback.ts",


        "src/bridge/controllerShortcutRules.ts",


        "src/bridge/controllerShortcutStorage.ts",


        "src/bridge/frameRateModel.ts",


        "src/bridge/fxSound.ts",


        "src/bridge/gameQuickSession.ts",


        "src/bridge/gamepadPresentation.ts",


        "src/bridge/gyroPresetModel.ts",


        "src/bridge/hcInputUtils.ts",


        "src/bridge/inputConfigHash.ts",


        "src/bridge/inputOwnerRuntime.ts",


        "src/bridge/inputStartupPolicy.ts",


        "src/bridge/keyboardGlyphAssets.ts",


        "src/bridge/keyboardKeyLayout.ts",


        "src/bridge/keyboardPreviewAsset.ts",


        "src/bridge/monitorCoreTiles.ts",


        "src/bridge/monitorFrameTime.ts",


        "src/bridge/monitorHudFpsVisibility.ts",


        "src/bridge/monitorHudFreshness.ts",


        "src/bridge/monitorHudModel.ts",


        "src/bridge/monitorLayout.ts",


        "src/bridge/monitorNativeDemand.ts",


        "src/bridge/monitorSnapshotSignals.ts",


        "src/bridge/mouseModePresentation.ts",


        "src/bridge/mouseModeState.ts",


        "src/bridge/performanceControls.ts",


        "src/bridge/quickAppDefaults.ts",


        "src/bridge/residentNavigation.ts",


        "src/bridge/screenTouchpadDefaults.ts",


        "src/bridge/screenTouchpads.ts",


        "src/bridge/settingsBaseline.ts",


        "src/bridge/settingsRepository.ts",


        "src/bridge/settingsSnapshot.ts",


        "src/bridge/steamAppearance.ts",


        "src/components/AppIcon.vue",


        "src/components/BootModeFields.vue",


        "src/components/BootModeWorkbench.vue",


        "src/components/BootModesPanel.vue",


        "src/components/ControllerActionControls.vue",


        "src/components/ControllerComboInput.vue",


        "src/components/ControllerDeviceDiagram.vue",


        "src/components/ControllerEditorGlobalActions.vue",


        "src/components/ControllerKeyboardPreview.vue",


        "src/components/ControllerOutputAction.vue",


        "src/components/ControllerOutputKeyboard.vue",


        "src/components/ControllerVectorArtwork.ts",


        "src/components/Dropdown.vue",


        "src/components/GameRecognitionControl.vue",


        "src/components/GamepadVisualizer.vue",


        "src/components/InlineIcon.vue",


        "src/components/IntegratedPage.vue",


        "src/components/KeyboardKeyGlyph.vue",


        "src/components/NavRail.vue",


        "src/components/OverflowGameName.vue",


        "src/components/ScreenTouchpadsSettings.vue",


        "src/components/SegButton.vue",


        "src/components/Slider.vue",


        "src/components/SwitchControl.vue",


        "src/components/Toggle.vue",


        "src/components/TopMonitorBar.vue",


        "src/composables/useKeyboardPressFeedback.ts",


        "src/composables/useShortcutPreviewFocus.ts",


        "src/gamepad/activationEvidence.ts",


        "src/gamepad/engine.ts",


        "src/gamepad/focus.ts",


        "src/gamepad/performanceNavigation.ts",


        "src/gamepad/serial.ts",


        "src/gamepad/sliderRepeat.ts",


        "src/gamepad/spatial.ts",


        "src/icons.json",


        "src/icons.ts",


        "src/radial/availability.ts",


        "src/radial/testingState.ts",


        "src/robust/controllerPreviewTrace.ts",


        "src/robust/frontendDiagnostics.ts",


        "src/robust/repairModel.ts",


        "src/router.ts",


        "src/styles/gamepad.css",


        "src/styles/integratedConsole.css",


        "src/styles/tokens.css",


        "src/version.ts",


        "src/views/ControllerShortcutEditorView.vue",


        "src/views/PowerView.vue",


        "tools/boot_modes_browser_selftest.mjs",


        "tools/boot_modes_file_ui_selftest.mjs",


        "tools/boot_modes_fixture/browserEntry.ts",


        "tools/boot_modes_fixture/browserNative.ts",


        "tools/boot_modes_fixture/buildBrowser.mjs",


        "tools/boot_modes_fixture/diskApiPort.ts",


        "tools/boot_modes_fixture/diskSettings.mjs",


        "tools/boot_modes_fixture/diskSettingsNative.cpp.in",


        "tools/boot_modes_fixture/fileBrowserPorts.ts",


        "tools/boot_modes_fixture/harness.mjs",


        "tools/boot_modes_fixture/runtimePorts.ts"


],


      {args:['{outDir}'],resultFiles:['boot-modes-file-ui.json'],timeoutMs:60000}),





    make('lifecycle.ui-power-transport','lifecycle','Production API and WebView IPC recover a missed Ready, timeout or renderer rejection without native writes','node',


      'tools/ui_power_transport_selftest.mjs',['package.json','src/bridge/uiLifecycle.ts','src/bridge/api.ts','src/bridge/ipc.ts'],


      {args:['--out-dir','{outDir}'],resultFiles:['ui-power-transport.json']}),


    make('lifecycle.ui-power-recovery','lifecycle','UI power mirror recovers a missed commit without hardware or input restart','node',


      'tools/ui_power_recovery_selftest.mjs',['package.json','src/bridge/uiLifecycle.ts','src/bridge/api.ts','src/App.vue'],


      {args:['--out-dir','{outDir}'],resultFiles:['ui-power-recovery.json']}),


    make("sleep.gyro-power", "sleep", "Actual sensor STA power callback, display pause and deferred resume",


      "node", "tools/ymcc_sim/global/sensor-power-consumer/run.mjs",


      ["native/main.cpp", "native/sensor_thread_mailbox.h", "deps/json/json.hpp",


       "tools/ymcc_sim/global/sensor-power-consumer/fixture.cpp.in",


       "tools/fixtures/legion_motion_consumer/extract_main.mjs", "tools/lib/hc_controller_motion_fixture.mjs",


       "tools/fixtures/hc_reviewed_calibration/toolchain.mjs"],


      { args: ["{outDir}"], resultFiles: ["consumer-result.json"], timeoutMs: 120000, profiles: ["desktop", "full", "sleep"] }),


    make("controller.mouse-auto", "controller", "Native mouse automatic foreground policy and stale-enable withdrawal", "node",


      "tools/mouse_mode_policy_selftest.mjs", ["native/mouse_mode_policy.h", "native/mouse_mode_foreground.h", "native/mouse_mode_foreground_rules.h", "deps/json/json.hpp", "tools/mouse_mode_policy_selftest.cpp", "tools/fixtures/hc_reviewed_calibration/toolchain.mjs"],


      {args:["{outDir}"],resultFiles:["mouse-policy-result.json"],timeoutMs:60000}),


    make("controller.steam-desktop-mouse", "controller", "Retired ordinary Steam mouse refuses all IO; SteamDeck-only sensitivity retained", "node",


      "tools/steam_desktop_mouse_selftest.mjs", ["native/main.cpp", "native/steam_live.js", "native/steam_live_script.h", "src/components/ControllerShortcutsCard.vue"],


      {args:["{outDir}"],resultFiles:["steam-desktop-mouse-result.json"],timeoutMs:60000}),


    make("controller.mouse-native", "controller", "Mouse backend direct process control and SteamDeck admission", "node",


      "tools/mouse_mode_native_selftest.mjs", ["src/bridge/ipc.ts", "src/bridge/steamDeckMouse.ts", "src/bridge/gameproc.ts", "native/main.cpp", "native/mouse_mode_com_apartment.h", "native/mouse_mode_policy.h", "native/mouse_mode_foreground.h", "native/mouse_mode_foreground_rules.h", "tools/mouse_mode_policy_selftest.cpp", "tools/mouse_mode_policy_selftest.mjs", "native/mouse_backend_process.h", "native/process_efficiency.h", "native/process_efficiency_inventory.h", "native/shortcut_feedback_native.h", "tools/mouse_mode_control_fixture.cpp", "tools/mouse_backend_fixture.cpp", "tools/mouse_backend_process_selftest.cpp", "tools/mouse_backend_settings_show_selftest.cpp", "tools/mouse_backend_settings_show_selftest.mjs", "tools/mouse_mode_bridge_selftest.mjs", "tools/mouse_status_hint_selftest.mjs", "src/bridge/mouseStatusHint.ts", "src/bridge/mouseModeState.ts", "src/bridge/desktopHudProtocol.ts", "src/bridge/mouseModePresentation.ts", "src/bridge/monitorHudModel.ts", "src/bridge/shortcutActionFeedback.ts", "tools/fixtures/legion_motion_consumer/extract_main.mjs", "tools/fixtures/hc_reviewed_calibration/toolchain.mjs"],


      {args:["{outDir}"],resultFiles:["mouse-mode-native-result.json"],timeoutMs:60000}),


    make("controller.persona", "controller", "Controller persona admission and persisted migration", "typescript",


      "tools/controller_persona_selftest.ts", ["package.json", "src/bridge/controllerPersona.ts", "src/bridge/controllerShortcutRules.ts", "tsconfig.json"]),


    make("controller.feedback-capabilities", "controller", "Controller feedback capability admission", "typescript",


      "tools/controller_feedback_capabilities_selftest.ts", ["package.json", "src/bridge/controllerFeedbackCapabilities.ts", "tsconfig.json"]),


    make("controller.mixed-inputs", "controller", "Mixed controller, OEM and keyboard shortcut rules", "typescript",


      "tools/controller_mixed_inputs_model_selftest.ts", ["package.json", "src/bridge/controllerPersona.ts", "src/bridge/controllerShortcutRules.ts", "src/bridge/keyboardKeyLayout.ts", "tsconfig.json"]),


    make("controller.display", "controller", "Controller axis presentation and visualizer geometry", "typescript",


      "tools/controller_display_selftest.ts", ["native/main.cpp", "package.json", "src/bridge/gamepadPresentation.ts", "src/components/GamepadVisualizer.vue", "src/views/GyroMotionView.vue", "tsconfig.json"]),


    make("controller.capability-gate", "controller", "Input capability fail-closed admission", "typescript",


      "tools/input_capability_gate_selftest.ts", ["package.json", "src/bridge/inputCapabilityGate.ts", "tsconfig.json"]),


    make("gyro.presentation", "gyro", "Latest-frame gyro presentation, visibility and disposal", "typescript",


      "tools/gyro_presentation_selftest.ts", ["package.json", "src/bridge/controllerPersona.ts", "src/bridge/gyroPresentation.ts", "src/bridge/gyroAxisFormat.ts", "src/bridge/inputContracts.ts", "tsconfig.json"]),


    make('gyro.xyz-fixed-layout', 'gyro', 'Actual XYZ display holds signed fixed-width decimal slots at 720/800', 'node',


      'tools/gyro_xyz_format_browser_selftest.mjs', gyroXYZLayoutInputs,


      {args:['{outDir}'],resultFiles:['results.json'],timeoutMs:60000}),


    make('controller.virtual-output-status', 'controller', 'Observed output progress, real failures and ordered request receipts', 'typescript',


      'tools/virtual_output_status_selftest.ts', ['package.json','tsconfig.json','src/bridge/virtualOutputStatus.ts','src/bridge/virtualTargetPolicy.ts']),


    make('controller.virtual-output-observed-ui', 'controller', 'Actual output status and native-request ordering plus physical gyro/back-key display without virtual output', 'node',


      'tools/virtual_output_status_selftest.mjs', ['package.json','tools/virtual_output_status_selftest.ts','src/bridge/virtualOutputStatus.ts','src/bridge/virtualTargetPolicy.ts','src/bridge/virtualTargetPolicyState.ts','src/bridge/virtualStartProtectionView.ts','src/bridge/controllerPersona.ts','src/bridge/controllerShortcutRules.ts','src/bridge/inputContracts.ts','src/radial/testingState.ts','src/radial/availability.ts','src/components/ControllerChannelStatus.vue','src/bridge/controllerFeatureStatus.ts','src/views/ButtonMappingView.vue'],


      {args:['{outDir}'],resultFiles:['virtual-output-status-report.json'],timeoutMs:60000}),


    make('controller.lossless-decky-state', 'controller', 'Decky action admission and process-only lamp metadata; runtime unknown and bounded observations preserved', 'node',


      'tools/lossless_decky_state_selftest.mjs', ['src/bridge/losslessProgramState.ts','src/bridge/losslessRuntimeState.ts','src/bridge/deckyLosslessState.ts','src/bridge/losslessScaling.ts','src/bridge/deckyMirrorHost.ts','decky-plugin/src/mirrorClient.ts','decky-plugin/src/index.tsx','tools/decky_sidebar_lossless_resilience_selftest.mjs','tools/fixtures/decky_task_paths.mjs'],


      {args:['{outDir}'],resultFiles:['lossless-decky-result.json'],timeoutMs:60000}),


    make('controller.lossless-return-focus', 'controller', 'Exact original game return after LS with three attempts inside ten seconds and bounded teardown', 'node',


      'tools/lossless_return_focus_selftest.mjs', ['native/main.cpp','native/lossless_return_focus.h','native/resume_focus.h','native/monitor_identity.h','tools/lossless_return_focus_selftest.cpp','tools/lossless_return_focus_selftest.py','deps/json/json.hpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'],


      {args:['{outDir}'],resultFiles:['lossless-return-focus-result.json'],timeoutMs:60000}),


    make('controller.lossless-toggle-owner', 'controller', 'Shared per-game Lossless configuration switch and exact stop-before-profile-removal under Xbox UIA failure', 'node',


      'tools/lossless_toggle_owner_selftest.mjs', ['src/bridge/quickapp.ts','src/bridge/api.ts','src/bridge/ipc.ts','src/bridge/losslessProfiles.ts','src/bridge/losslessScaling.ts','src/bridge/deckyLosslessState.ts','native/lossless_launch.h','native/lossless_launch_contract.h','native/lossless_scale.h','native/lossless_scale_contract.h','native/lossless_window_refresh.h','native/decky_sidebar_diagnostics.h','native/lossless_runtime.h','src/bridge/losslessRuntimeState.ts','native/process_efficiency.h','native/main.cpp','tools/lossless_launch_selftest.cpp','tools/lossless_scaling_selftest.mjs','tools/fixtures/lossless_close_probe.ps1','tools/fixtures/lossless_close_owned.cs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/decky_task_paths.mjs'],


      {args:['{outDir}'],resultFiles:['lossless-owner-result.json'],timeoutMs:60000}),


    make('controller.task-view-foreground', 'controller', 'TaskView grants only its ready worker through the owning UI thread with bounded cancellation', 'node',


      'tools/task_view_foreground_grant_selftest.mjs', ['native/task_view_foreground_grant.h','tools/task_view_foreground_grant_selftest.cpp','deps/json/json.hpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'],


      {args:['{outDir}'],resultFiles:['foreground-grant-result.json'],timeoutMs:60000}),


    make('controller.task-view-native-wiring', 'controller', 'Actual native TaskView producer preserves Windows grant outcome across disabled and three virtual personas', 'node',


      'tools/task_view_native_wiring_selftest.mjs', ['native/main.cpp','native/task_view_dispatch.h','native/task_view_foreground_grant.h','tools/task_view_api_wiring_selftest.py','tools/virtual_sample_tree_perf_selftest.py','deps/json/json.hpp'],


      {args:['{outDir}'],resultFiles:['native-wiring-result.json'],timeoutMs:60000}),


    make('controller.task-view-actions', 'controller', 'Real Y-menu activation preserves failure focus and minimizes only after accepted TaskView request', 'node',


      'tools/task_view_action_frontend_selftest.mjs', ['src/components/GameQuickActions.vue','src/bridge/api.ts','src/gamepad/engine.ts','src/gamepad/activationEvidence.ts'],


      {args:['{outDir}'],resultFiles:['task-view-action-result.json']}),


    make('touch.button-abort-recovery', 'touch', 'Dedicated touchpad buttons recover bounded aborted sync and preserve exit withdrawal', 'node',


      'tools/ymcc_sim/global/touchpad-button-recovery/run.mjs', [


        'native/main.cpp', 'native/controller_persona.h', 'native/controller_persona_settings.h',


        'native/screen_touchpads.h', 'native/screen_button_overlay.h', 'native/screen_control_glyphs.h',


        'native/screen_standalone_special.h', 'native/screen_touchpad_auto_hide.h',


        'native/screen_touchpad_contact_visual.h', 'native/screen_touchpad_cursor.h',


        'native/screen_touchpad_cursor_visibility.h', 'native/screen_touchpad_display_policy.h',


        'native/screen_touchpad_input_surface.h', 'native/screen_touchpad_owner.h',


        'native/screen_touchpad_press_visual.h', 'native/touch_haptic_dispatch.h', 'deps/json/json.hpp',


        'tools/screen_touchpad_settings_reveal_selftest.py', 'tools/screen_touchpad_settings_reveal_selftest.cpp',


        'tools/screen_touchpad_button_abort_native_selftest.ps1', 'tools/screen_touchpad_button_abort_native_selftest.cpp',


        'tools/screen_touchpad_exit_release_selftest.ps1', 'tools/screen_touchpad_exit_release_selftest.cpp',


        'tools/ymcc_sim/global/touchpad-button-recovery/field-record.json',


        'tools/ymcc_sim/global/touchpad-button-recovery/README.md'],


      {args:['{outDir}'],resultFiles:['button-recovery-result.json'],timeoutMs:60000}),


    make("touch.screen-touchpads", "touch", "Screen touchpad presets, bounds and validation", "typescript",


      "tools/screen_touchpads_selftest.ts", ["package.json", "src/bridge/ipc.ts", "src/bridge/screenTouchpads.ts", "src/bridge/screenTouchpadDefaults.ts", "tsconfig.json"]),


    make("fan.curve", "fan", "Fan curve normalization and safety bounds", "typescript",


      "tools/fan_curve_selftest.ts", ["package.json", "src/bridge/fanCurve.ts", "tsconfig.json"]),


    make("lifecycle.input", "lifecycle", "Input owner release and generation lifecycle model", "typescript",


      "tools/input_lifecycle_mock_selftest.ts", ["package.json", "src/bridge/inputLifecycleMock.ts", "tsconfig.json"]),


    make("steam.shared-mode-source", "steam", "Shared Steam UI observer source wiring contract", "node",


      "tools/steam_ui_mode_shared_source_selftest.mjs", ["native/main.cpp", "native/steam_ui_mode_observer.h"], {"scope":"source-contract"}),


    make('steam.boot-focus', 'steam', 'Existing BAT focus/flow consumers: sixty-second Steam-only startup, bounded self-activation requests and actual foreground readback; inert terminals only', 'node',

      'tools/ymcc_sim/global/steam-boot-bat/run.mjs', ['PowerControl/YeManSteam.bat','tools/ymcc_sim/global/steam-boot-bat/selftest.ps1','tools/ymcc_sim/global/steam-boot-bat/flow-selftest.ps1','tools/ymcc_sim/global/steam-boot-bat/focus-selftest.ps1','tools/ymcc_sim/global/steam-boot-bat/cleanup-selftest.ps1'],

      {args:['--focus-only','--out','{outDir}'],resultFiles:['boot-focus-result.json'],timeoutMs:90000}),

    make("steam.contact-source", "steam", "Read-only Steam contact UI source contract", "node",


      "tools/steam_contact_ui_selftest.mjs", ["src/bridge/steamGameLink.ts", "src/views/SteamView.vue"], {"scope":"source-contract"}),


    make("gyro.motion-mapper", "gyro", "HC gyro motion mapper and safe-zero boundaries", "typescript",


      "tools/gyro_motion_mapper_mock_selftest.ts", ["package.json", "src/bridge/gyroMotionMapperMock.ts", "src/bridge/hcInputUtils.ts", "tsconfig.json"]),


    make("gyro.simulation-model", "gyro", "Production gyro simulation input and physics model", "typescript",


      "tools/gyro_simulation_model_selftest.ts", ["package.json", "src/gyroSimulation/model.ts", "src/gyroSimulation/motorcycle.ts", "tsconfig.json"]),


    make("gyro.simulation-presentation", "gyro", "Simulation target poses, weapon projection and camera", "typescript",


      "tools/gyro_simulation_presentation_selftest.ts", ["package.json", "src/gyroSimulation/model.ts", "src/gyroSimulation/motorcycle.ts", "src/gyroSimulation/renderer.ts", "tsconfig.json"]),


    make("gyro.game-haptics", "gyro", "Original game haptics memory-port pulses, stop and late-reply neutrality", "typescript",


      "tools/gyro_game_haptics_selftest.ts", ["package.json", "src/bridge/ipc.ts", "src/gyroSimulation/haptics.ts", "tsconfig.json"], {


        completionContract: {schemaVersion:1,rules:[{line:'Game haptics selftest: 8/8 passed'}]},


        boundary: 'Original eight GameHaptics producer cases with injected memory ports only. Does not prove GyroSimulationWindow SFC exit or downstream physical neutrality; no device, App/Host/Steam or OS sleep.',


      }),


    make("gyro.preset-save", "gyro", "Explicit preset save rebases one conflict inside the same editor owner", "typescript",


      "tools/gyro_preset_save_selftest.ts", gyroPresetSaveInputs),


    make("gyro.motorcycle", "gyro", "Motorcycle steering, combat, collisions and frame pacing", "typescript",


      "tools/gyro_motorcycle_selftest.ts", ["package.json", "src/gyroSimulation/motorcycle.ts", "tsconfig.json"]),


    make("performance.cpu-state", "performance", "Normalized CPU and HUD render-state equivalence", "typescript",


      "tools/monitor_cpu_state_selftest.ts", performanceCpuStateInputs),


    make("performance.core-tiles", "performance", "CPU core parser, presentation and display coalescing", "typescript",


      "tools/monitor_core_tiles_selftest.ts", performanceCoreTilesInputs),


    make("sensor.extended-metrics", "sensor", "Sensor packet GPU temperature, RAM and VRAM validity", "typescript",


      "tools/monitor_extended_metrics_consumer_selftest.ts", performanceCoreTilesInputs),


    make("battery.publication-consumer", "battery", "Battery power, lifetime, freshness and unavailable display", "typescript",


      "tools/monitor_battery_hide_selftest.ts", performanceCoreTilesInputs),


    make('performance.ownership-chain', 'performance', 'Global/dedicated auto/manual ownership, wake selection and resident recovery', 'node',


      'tools/performance_manual_idle_selftest.mjs', performanceOwnershipInputs, {args:['{outDir}'],resultFiles:['performance-ownership.json']}),


    make('performance.entry-parity', 'performance', 'Actual Vue/Decky controls converge on one owner and identical CPU/TDP calls', 'node',


      'tools/performance_entry_parity_selftest.mjs', performanceEntryParityInputs, {args:['{outDir}'],resultFiles:['performance-entry-parity.json']}),


    make('performance.slider-repeat', 'performance', 'One-step performance/fan edits with linearly shorter repeat intervals', 'node',


      'tools/performance_slider_repeat_selftest.mjs', ['package.json', 'src/gamepad/sliderRepeat.ts', 'src/gamepad/engine.ts', 'src/views/FanView.vue', 'src/bridge/fanCurve.ts'],


      {args:['{outDir}'],resultFiles:['performance-slider-repeat.json']}),


    make('performance.slider-draft', 'performance', 'Actual performance sliders resist stale refreshes and late save replies until confirmation', 'node',


      'tools/performance_slider_draft_browser_selftest.mjs', performanceSliderDraftInputs,


      {args:['{outDir}'],resultFiles:['results.json'],timeoutMs:60000}),


    make('performance.slider-native-repeat', 'performance', 'Actual native performance/fan interval acceleration, edit ownership and focus recovery', 'node',


      'tools/performance_slider_native_repeat_selftest.mjs', ['native/main.cpp', 'deps/json/json.hpp', 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'],


      {args:['{outDir}'],resultFiles:['summary.json'],timeoutMs:60000}),


    make("performance.profile-handoff", "performance", "AC/DC continuous controls and independent SMT profile transfer", "node",


      "tools/performance_profile_handoff_selftest.mjs", ["src/bridge/deckyGameActions.ts", "src/bridge/performanceControls.ts", "src/components/GameCustomProfilePanel.vue", "src/views/PerformanceScheduleView.vue"]),


    make("performance.float-runtime", "performance", "Resident float CPU/TDP controller with virtual time", "node",


      "tools/continuous_float_runtime_selftest.mjs", ["src/bridge/autofloat.ts", "src/bridge/monitorCoreTiles.ts", "src/bridge/monitorFpsHardware.ts", "src/bridge/monitorFrameTime.ts", "src/bridge/monitorSnapshotSignals.ts", "src/bridge/performanceControls.ts"]),


    getMusicPlayerToolScopeAdapter(_repoRoot),


    make("steam.ui-mode-service", "steam", "Steam UI service and shipped JS operation in isolated VM", "node",


      "tools/steam_ui_mode_service_selftest.mjs", ["native/steam_live.js", "native/steam_live_script.h", "src/bridge/steamUi.ts"]),


    make("game-library.account-status", "game-library", "Production library account discovery, coalescing and retries", "node",


      "tools/custom_steam_library_account_status_selftest.mjs", ["CustomSteamLibrary/workspace-ui/app.js"]),


    make("sensor.input-contracts", "sensor", "Gyro and controller telemetry schema, provenance and rejection", "typescript",


      "tools/input_contracts_selftest.ts", sensorInputContractsInputs),


    make("settings.cas", "settings", "Settings normalization, migrations and compare-and-swap", "typescript",


      "tools/settings_cas_selftest.ts", settingsCasInputs),


    make("settings.reactivity", "settings", "Real settings write queue with fail-closed memory-only IPC", "typescript",


      "tools/settings_reactivity_selftest.ts", settingsReactivityInputs),


    make("plugin.game-actions", "plugins", "Decky game actions through the production memory-backed store", "node",


      "tools/decky_sidebar_game_actions_selftest.mjs", pluginGameActionsInputs),


    make("game-library.media-shortcuts", "game-library", "Steam shortcut identity, source precedence and media queue", "typescript",


      "tools/shared_game_media_shortcuts_selftest.ts", gameLibraryMediaShortcutsInputs),


    make('plugins.wallpaper-host','plugins','Production wallpaper host acknowledgement, visible-only scans, metadata recovery and lifecycle fences','typescript',


      'tools/steam_wallpaper_host_selftest.ts',[...gameLibraryMediaShortcutsInputs,'src/bridge/deckyMirrorExtensions.ts','src/bridge/background.ts','src/bridge/steamWallpaperHost.ts','src/bridge/steamWallpaperDelivery.ts']),


    make('plugins.wallpaper-background','plugins','Production wallpaper player background admission, late events and release','typescript',


      'tools/steam_wallpaper_playback_selftest.ts',['package.json','decky-plugin/src/wallpaperPlayers.ts','decky-plugin/src/wallpaperForeground.ts','decky-plugin/src/wallpaperEntrance.ts','src/bridge/gameMediaPlayback.ts']),


    make("game-library.media-downloads", "game-library", "Media download coordinator with synthetic settings and IPC", "typescript",


      "tools/shared_game_media_downloads_selftest.ts", gameLibraryMediaDownloadsInputs),


    make("game-library.media-recovery", "game-library", "Media coordinator cancellation, retry and stale response handling", "typescript",


      "tools/shared_game_media_downloads_reverse_selftest.ts", gameLibraryMediaShortcutsInputs),


    make("performance.gpu-library", "performance", "Shipped GPU capability curves and model admission", "node",


      "tools/gpu_capability_library_selftest.mjs", ["public/performance-simulation/frame-pipeline.js", "public/performance-simulation/frame-work.js", "public/performance-simulation/gpu-capability-library.js", "public/performance-simulation/index.html", "public/performance-simulation/optimization-curve.js", "public/performance-simulation/ts-power-curve.js", "tools/lib/simulationModelHarness.mjs"], {"args":["{outDir}"],"resultFiles":["gpu-capability-verification.json"]}),


    make("screen.standalone-actions", "display", "Screen standalone Steam menu actions in an isolated receiver VM", "node",


      "tools/screen_standalone_special_actions_selftest.mjs", ["native/main.cpp", "native/steam_live.js"]),


    make("game-tools.trainer-foreground", "game-library", "Decky interactive desktop Shell launch, no replay after uncertain dispatch", "node",


      "tools/decky_trainer_foreground_selftest.mjs", ["src/bridge/gameTrainer.ts","src/bridge/deckyTrainerActions.ts","src/bridge/deckyMirrorHost.ts","decky-plugin/src/index.tsx","decky-plugin/src/mirrorClient.ts","PowerControl/YeManTrainerLaunch.ps1","PowerControl/Trainer/TrainerForeground.ps1","PowerControl/Trainer/TrainerLaunch.ps1","PowerControl/Trainer/TrainerBackend.psm1","tools/game_trainer_foreground_selftest.ps1"], {args:["{outDir}"],resultFiles:["result.json","trainer-foreground-unit.json"],timeoutMs:60000}),


    make("game-tools.trainer-background", "game-library", "Production trainer preparation, ownership and launch state with inert IPC", "node",


      "tools/game_trainer_background_selftest.mjs", ["src/bridge/gameTrainer.ts", "src/bridge/gameTrainerAuto.ts", "tsconfig.json"], {"timeoutMs":60000}),


    make("performance.game-profile-editor", "performance", "Game profile performance/input isolation and stale edits", "node",


      "tools/game_profile_editor_selftest.mjs", performanceGameProfileEditorInputs),


    make("performance.simulation-work", "performance", "Production CPU/GPU frame work simulation model", "node",


      "tools/performance_simulation_work_selftest.mjs", ["public/performance-simulation/frame-pipeline.js", "public/performance-simulation/frame-work.js", "public/performance-simulation/gpu-capability-library.js", "public/performance-simulation/index.html", "public/performance-simulation/optimization-curve.js", "public/performance-simulation/ts-power-curve.js", "tools/lib/simulationModelHarness.mjs"], {"timeoutMs":60000,"args":["{outDir}"],"resultFiles":["model-verification.json"]}),


    make("performance.simulation-pipeline", "performance", "CPU/GPU frame pipeline simulation and boundaries", "node",


      "tools/performance_simulation_pipeline_selftest.mjs", ["public/performance-simulation/frame-pipeline.js", "public/performance-simulation/frame-work.js", "public/performance-simulation/gpu-capability-library.js", "public/performance-simulation/index.html", "public/performance-simulation/optimization-curve.js", "public/performance-simulation/ts-power-curve.js", "tools/lib/simulationModelHarness.mjs"], {"args":["{outDir}"],"resultFiles":["pipeline-verification.json"]}),


    make("performance.simulation-optimization", "performance", "Continuous optimization curves in the shipped simulation", "node",


      "tools/performance_simulation_optimization_selftest.mjs", ["public/performance-simulation/frame-pipeline.js", "public/performance-simulation/frame-work.js", "public/performance-simulation/gpu-capability-library.js", "public/performance-simulation/index.html", "public/performance-simulation/optimization-curve.js", "public/performance-simulation/ts-power-curve.js", "tools/lib/simulationModelHarness.mjs"], {"args":["{outDir}"],"resultFiles":["optimization-curve-verification.json"]}),


    make("performance.simulation-power", "performance", "Monotone power/latency curves across device models", "node",


      "tools/performance_simulation_monotone_power_selftest.mjs", ["public/performance-simulation/frame-pipeline.js", "public/performance-simulation/frame-work.js", "public/performance-simulation/gpu-capability-library.js", "public/performance-simulation/index.html", "public/performance-simulation/optimization-curve.js", "public/performance-simulation/ts-power-curve.js", "tools/lib/simulationModelHarness.mjs"], {"args":["{outDir}"],"resultFiles":["monotone-power-verification.json"]}),


    make("performance.snapshot-bounds-source", "performance", "Native monitor snapshot bounds source contract", "node",


      "tools/monitor_snapshot_bounds_selftest.mjs", ["native/main.cpp", "native/monitor_detail_log.h", "native/monitor_snapshot_contract.h"], {"scope":"source-contract"}),


    make("lifecycle.updater-notice", "lifecycle", "Update-ready presentation, IPC rejection and notice disposal", "typescript",


      "tools/updater_notice_selftest.ts", ["package.json", "src/bridge/frameRateModel.ts", "src/bridge/ipc.ts", "src/bridge/monitorNoticeHost.ts", "src/bridge/monitorShortcutNotice.ts", "src/bridge/shortcutActionFeedback.ts", "src/bridge/updateReadyNotice.ts", "tsconfig.json"]),


  ];


}





/**


 * Reviewed exclusions, not runnable adapters. These are limitations, not PASSes.


 * Browser/UI fixtures that launch Chromium are not part of this Node-only lane.


 * sleep.task-policy-source is owned by sleep.mjs to avoid duplicate registration.


 */


export const ADAPTER_EXCLUSIONS = Object.freeze([


  { path: 'tools/ai_fan_runtime_selftest.ts', reason: 'Starts a real mock Host, PowerShell helpers and a fixed 8765 listener; writes shared Build evidence.' },


  { path: 'tools/fan_bridge_real_host_smoke_selftest.ts', reason: 'Requires an exported FanHost payload, starts its executable and uses real HTTP transport plus a fixed Build directory.' },


  { path: 'tools/battery_charge_capability_selftest.mjs', reason: 'Audits research-only routing and requires hashed source snapshots outside the repository; not desktop battery hardware evidence.' },


  { path: 'tools/sensorapi_source_contract_selftest.mjs', reason: 'Starts cmd/MSVC and generated native executables; requires the reviewed external toolchain and shared Build evidence.' },


  { path: 'tools/monitor_sensor_semantics_selftest.mjs', reason: 'Starts another generator and MSVC/native executables with a shared evidence location.' },


  { path: 'tools/gyro_calibration_session_selftest.ts', reason: 'Writes a fixed Build/Validation receipt with no argument to redirect it into this case directory.' },


  { path: 'tools/gyro_config_activation_selftest.ts', reason: 'Evidence is redirected only by an ambient environment override, not the adapter argument contract; default writes shared Build state.' },


  { path: 'tools/game_value_scan_bridge_selftest.mjs', reason: 'VM boundary is inert, but report output is controlled only by ambient YMCC_CE_VALIDATION_ROOT with a shared Build default.' },


  { path: 'tools/game_trainer_five_frontend_selftest.mjs', reason: 'Requires existing five-game backend receipts; no self-contained repository fixture.' },


  { path: 'tools/game_trainer_lifecycle_frontend_selftest.mjs', reason: 'Requires pre-existing backend lifecycle JSON evidence and writes into its supplied directory.' },


  { path: 'tools/gpu_dedicated_memory_browser_selftest.mjs', reason: 'Requires Playwright/browser execution and local HTTP UI infrastructure; this lane covers GPU curves and injected metric consumers.' },


  { path: 'tools/screen_touchpads_ui_selftest.mjs', reason: 'Creates Vite SSR infrastructure and writes its legacy UI artifact location; use the pure touchpad validation suite in this lane.' },


  { path: 'tools/steam_installed_menu_keyboard_selftest.mjs', reason: 'Read-only VM test, but requires an installed Valve client outside the repository and absolute Steam/report arguments.' },


  { path: 'tools/performance_simulation_2d_demand_selftest.mjs', reason: 'Unconditionally requires a before snapshot inside the evidence directory; a fresh case directory cannot satisfy it.' },


  { path: 'tools/tdp_frontend_static_selftest.ts', reason: 'Bare TS uses runtime require(path/os) and remains excluded from ESM relocation. The unchanged original is executed through the reviewed CommonJS adapter performance.tdp-frontend-consumer in tdp-consumer-signals/.' },


  { path: 'tools/decky_sidebar_host_selftest.mjs', reason: 'Host fixture permits ambient YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE to execute an alternate source; only the fail-closed game-action store fixture is admitted here.' },


  { path: 'tools/decky_sidebar_readonly_selftest.mjs', reason: 'Shares the host fixture alternate-source environment override; not admitted without a runner-owned environment contract.' },


].map(item => Object.freeze(item)));


