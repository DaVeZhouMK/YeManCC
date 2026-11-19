import {getCapabilityCatalogAdapters} from '../capability-catalog/adapter.mjs';
import {virtualControllerRuntimeHeaders} from '../../../virtual_controller_runtime_source.mjs';
export const controllerConsumerSources=[
 'tools/hc_controller_input_consumer_selftest.mjs','tools/lib/hc_controller_input_fixture.mjs',
 'tools/input_frame_submission_selftest.mjs','tools/input_frame_pipe_issue_selftest.mjs','tools/input_power_gate_r122_selftest.mjs','tools/msi_canonical_submission_r123_selftest.mjs',
 'tools/fixtures/input_frame_submission_fixture.cpp','tools/fixtures/unified_controller_input/before.json','tools/fixtures/unified_controller_input/fixture.inc',
 'deps/json/json.hpp','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/input_frame_pipe_issue/windows.h',
 'tools/fixtures/input_frame_pipe_issue/fixture.cpp','tools/fixtures/input_power_gate_r122/power.cpp','tools/fixtures/input_power_gate_r122/resume.cpp','tools/fixtures/input_power_gate_r122/resume.mjs','native/sleep_power_dispatch.h','native/sleep_wake_admission.h','tools/fixtures/msi_canonical_submission_r123/fixture.cpp',
 'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
 'capabilities/HcControllerMotionCapabilityCatalog.h','capabilities/HcControllerMotionCapabilityCatalog.generated.h','capabilities/hc-device-catalog.generated.h',
 'tools/virtual_controller_runtime_source.mjs',...virtualControllerRuntimeHeaders.map(name=>'native/'+name),
 'native/controller_persona.h','native/canonical_frame_fingerprint_memo.h',
 'native/msi_virtual_report_diagnostic.h','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_motion_matrix_guard.mjs',
 'native/input_host_shared_power.h','native/sleep_power_state.h','native/sleep_power_observation.h','native/sleep_power_revocation.h','native/msi_power_rearm_work_runtime.h',
 'tools/fixtures/input_power_gate_r122/shared-power-prepare.mjs','tools/fixtures/input_frame_pipe_issue/request_cases.inc',
 'native/input_frame_submission.h','native/touch_haptic_dispatch.h','native/game_feedback_receipt.h','native/input_host_pipe_transport.h','native/virtual_output_status.h','native/windows_motion_submission.h','native/legion_motion_source.h','native/legion_motion_message_clock.h',
 'tools/ymcc_sim/global/controller-catalog/adapter.mjs','tools/ymcc_sim/global/controller-catalog/inputs.mjs',
];
export function controllerInputTestDefinition(repo){const shared=getCapabilityCatalogAdapters(repo)[0];return {sources:[...new Set([...shared.evidenceInputs,...controllerConsumerSources])],artifacts:shared.artifactInputs};}

