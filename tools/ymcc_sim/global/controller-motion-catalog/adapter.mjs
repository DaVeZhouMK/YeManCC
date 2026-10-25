import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {compilerArtifacts} from '../controller-priority/adapter.mjs';
export function getControllerMotionCatalogAdapters(repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..')){
 const fixture='tools/fixtures/legion_motion_consumer';
 return [{id:'controller.unified-motion-providers',domain:'controller',title:'Total physical-motion providers through original decoder, source lease/banks, selected session and actual final pipe guard',
 kind:'powershell',path:'tools/ymcc_sim/global/controller-motion-catalog/Run-ControllerMotion.ps1',args:['-EvidenceDir','{outDir}/controller-motion','-NodePath',process.execPath],
 timeoutMs:240000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['controller-motion/result.json'],
 completionContract:{schemaVersion:1,rules:[{line:'CONTROLLER_MOTION_BINDINGS_COMPLETED configurations=2 faults=8'}]},
 evidenceInputs:[
 'tools/ymcc_sim/global/controller-motion-catalog/adapter.mjs','tools/ymcc_sim/global/controller-motion-catalog/Run-ControllerMotion.ps1',
 'tools/hc_controller_motion_consumer_selftest.mjs','tools/fixtures/unified_controller_motion/extra.cpp.inc',
 'tools/lib/hc_controller_motion_fixture.mjs','tools/lib/hc_controller_motion_catalog.mjs',
 'tools/fixtures/legion_motion_consumer/extract_main.mjs',...fs.readdirSync(path.join(repo,fixture)).filter(name=>fs.statSync(path.join(repo,fixture,name)).isFile()).map(name=>fixture+'/'+name),
 'capabilities/hc-controller-motion-policy.json','capabilities/HcControllerMotionCapabilityCatalog.h','capabilities/HcControllerMotionCapabilityCatalog.generated.h','capabilities/hc-device-catalog.json','capabilities/hc-device-catalog.generated.h',
 ...['legion_motion_source.h','legion_motion_bank_evidence.h','legion_motion_message_clock.h','legion_input_protocol.h','controller_motion_session.h','controller_motion_bank_sessions.h','controller_motion_processor.h','controller_motion_calibration.h','input_frame_submission.h','windows_motion_submission.h','virtual_output_status.h','input_host_pipe_transport.h'].map(name=>'native/'+name),
 'tools/input_frame_pipe_issue_selftest.mjs','tools/lib/hc_controller_input_fixture.mjs','capabilities/HcControllerInputCapabilityCatalog.h','capabilities/HcControllerInputCapabilityCatalog.generated.h','tools/fixtures/input_frame_pipe_issue/fixture.cpp','tools/fixtures/input_frame_pipe_issue/windows.h',
 'tools/fixtures/legion_motion_calibration_consumer/platform_boundary.h','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs','tools/ymcc_sim/global/controller-priority/adapter.mjs','native/main.cpp','deps/json/json.hpp',
 ],artifactInputs:[...compilerArtifacts(),process.execPath,'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe','C:/Windows/System32/cmd.exe','C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'],
 }];
}
