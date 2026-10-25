import {compilerArtifacts} from '../controller-priority/adapter.mjs';
export function getOemConsumerAdapters(){return [{
 id:'controller.unified-oem-input-consumers',domain:'controller',
 title:'Complete production OEM gate, canonical query, pressed keys, hook publication and source return with compiled faults',
 kind:'powershell',path:'tools/ymcc_sim/global/oem-consumer-signals/Run-Consumers.ps1',args:['-EvidenceDir','{outDir}/oem-consumers','-NodePath',process.execPath],
 completionContract:{schemaVersion:1,rules:[{line:'OEM_INPUT_CONSUMERS_COMPLETED configurations=2 faults=6'}]},
 timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['oem-consumers/result.json'],
 evidenceInputs:[
  'tools/ymcc_sim/global/oem-consumer-signals/adapter.mjs','tools/ymcc_sim/global/oem-consumer-signals/Run-Consumers.ps1',
  'tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs',
  'tools/hc_oem_input_consumer_selftest.mjs','tools/fixtures/unified_oem_input/generate.mjs','tools/fixtures/unified_oem_input/harness.cpp',
  'tools/lib/hc_oem_input_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs',
  'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/controller-priority/adapter.mjs','native/main.cpp','deps/json/json.hpp',
  'capabilities/HcOemInputCapabilityBinding.h','capabilities/hc-device-catalog.generated.h','capabilities/hc-device-catalog.json',
 ],artifactInputs:[...compilerArtifacts(),process.execPath,'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe','C:/Windows/System32/cmd.exe','C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'],
}];}
