/** Real battery controller/providers with injected transports and real Vue feedback. */
export function getBatterySignalAdapters(){
 return [{id:'battery.charge-workflow',domain:'battery',title:'Charging policy, ownership recovery and actual Controller JSON consumed by the settings component',
  kind:'powershell',path:'tools/ymcc_sim/global/battery-signals/Run-Battery.ps1',args:['-EvidenceDir','{outDir}/battery'],timeoutMs:240000,
  profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['battery/result.json','battery/controller-boundary.json','battery/ui/result.json','battery/ui/live-controller/result.json','battery/dispatch-source.json'],
  referenceInputs:['native/main.cpp'],referenceMeaning:'Native IPC dispatcher provenance; this test injects bridge IPC and does not execute the native transport or production Main',
  evidenceInputs:[
   'capabilities/HcDeviceCapabilityCatalog.cs','capabilities/HcDeviceCapabilityCatalog.generated.cs','capabilities/HcBatteryCapabilityCatalog.cs','capabilities/HcBatteryCapabilityCatalog.generated.cs','capabilities/hc-device-catalog.json','capabilities/hc-battery-policy.json','docs/battery-charge-capabilities.json','tools/battery-charge-generate-routes.mjs','tools/lib/hc_battery_catalog.mjs',
   'tools/ymcc_sim/global/battery-signals/adapter.mjs',
   'tools/ymcc_sim/global/battery-signals/ControllerBridge.cs','tools/ymcc_sim/global/battery-signals/extract-dispatch.mjs','tools/ymcc_sim/global/battery-signals/controller-proxy.mjs','tools/ymcc_sim/global/battery-signals/live-browser-cases.mjs',
   'tools/ymcc_sim/global/battery-signals/BatteryHostSimulation.csproj','tools/ymcc_sim/global/battery-signals/NativeBoundary.cs',
   'native/BatteryHostTests/Program.cs','native/BatteryHostTests/MsiFixture.cs',
   'native/BatteryHost/YeManBatteryHost.csproj','native/BatteryHost/Controller.cs','native/BatteryHost/Policy.cs',
   'native/BatteryHost/Providers.cs','native/BatteryHost/MsiWmi.cs','native/BatteryHost/MsiApProvider.cs','native/BatteryHost/TraceLog.cs',
   'native/BatteryHost/RuntimeContract.cs','native/BatteryHost/AssemblyInfo.cs','native/BatteryHost/Program.cs',
   'native/BatteryHost/battery-routes.json','native/BatteryHost/runtime-contract.json','native/BatteryHost/payload-files.json','native/BatteryHost/msi-ap-route-review.json',
   'native/ProcessEfficiency.cs','native/battery_charge_client.h','native/battery_host_contract.h',
   'tools/battery_charge_contract_selftest.mjs','tools/battery-charge-contract.mjs','tools/battery_charge_browser_selftest.mjs',
   'src/bridge/batteryCharge.ts','src/bridge/uiLifecycle.ts','src/components/BatteryChargeSettings.vue','src/components/Dropdown.vue','src/components/InlineIcon.vue','src/components/AppIcon.vue','src/assets/mouse-classic.png','src/icons.ts','src/icons.json','src/styles/tokens.css',
   'deps/ymcc-shared-runtime/bin/LibreHardwareMonitorLib.dll','deps/ymcc-shared-runtime/bin/RAMSPDToolkit-NDD.dll',
  ],boundary:'Unmodified existing managed tests and real product component; vendor IO injected; target native imports blocked; no physical threshold/OS sleep acceptance'}];
}