"""Opt-in typed preparation for the existing hostframe consumer.

Only generates inputs. Native execution requires the existing owned-Job parent,
its unspent cumulative budget, and independent parity intake. The historical
Popen runner does not prove descendant return and is deliberately not reused.
The JSON oracle is a small independent frozen schema/layout, never a full main.
"""
from __future__ import annotations

import datetime
import hashlib
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE_SHA256 = "bcdfa2d5f402e894b04106226b1e0ab13c1fe1c13b32e63dc916aaa00c137b7e"
RECORD_SHA256 = "4fd559951051b522d181fa22166db32fe8baf63f60d75b5527364403dc88b1c0"
LIFECYCLE = ["retired-power", "configuration-writer", "zero-state-notification",
             "failed-post", "window-lost-during-snapshot", "terminal-retire-pending",
             "terminal-late-producers"]
HEADERS = ("tree-support-extracted.h", "tree-wrappers-extracted.h",
           "tree-dispatch-extracted.h", "tree-methods-extracted.h")
RUNTIME_LIMITATION = (
    "Preparation does not execute or qualify the opt-in bridge. runtime.py and "
    "bridge.mjs reuse the existing owned RawProgram host around tree_invoke; "
    "execution requires an explicit fixed environment, complete declared input "
    "superset, actual cumulative ledger and remaining original budget. Fixed-cost "
    "requires same-input owned core+parity intake. SOURCE_READY remains false "
    "until the public bridge and its receipts are reviewed and qualified."
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError("expected exactly one adaptation site: " + old[:100])
    return text.replace(old, new, 1)


def project_consumer(raw: bytes, typed: bool) -> str:
    """Resolve only our named feature directives; preserve every old directive.

    This is source projection, not a general C++ preprocessor. It lets both
    public modes and dual-layout cost share the one existing assertion body.
    """
    lines = raw.decode("utf-8-sig").replace("\r\n", "\n").splitlines(True)
    output, arm = [], None
    for line in lines:
        directive = line.strip()
        if directive == "#if defined(YMCC_TYPED_TELEMETRY_BUILD)":
            if arm is not None:
                raise ValueError("nested typed feature projection")
            arm = typed
        elif directive == "#else // YMCC_TYPED_TELEMETRY_BUILD":
            if arm is None:
                raise ValueError("unmatched typed else")
            arm = not arm
        elif directive == "#endif // YMCC_TYPED_TELEMETRY_BUILD":
            if arm is None:
                raise ValueError("unmatched typed endif")
            arm = None
        elif arm is not False:
            output.append(line)
    if arm is not None:
        raise ValueError("unclosed typed feature projection")
    text = "".join(output)
    if typed:
        text = once(text, '#include "fixtures/virtual_sample_tree/telemetry_typed/lifecycle.inc"',
                    (HERE / "lifecycle.inc").read_text(encoding="utf-8").rstrip("\n"))
    return text


def oracle_headers() -> dict[str, str]:
    oracle = json.loads((HERE / "json_oracle.json").read_text(encoding="utf-8"))
    headers = oracle["headers"]
    if set(headers) != set(HEADERS):
        raise ValueError("wrong independent JSON oracle header set")
    for name, text in headers.items():
        if sha(text.encode("utf-8")) != oracle["sha256"][name]:
            raise ValueError("JSON oracle changed: " + name)
    if "gyroTelemetry" in headers["tree-support-extracted.h"]:
        raise ValueError("baseline must retain its original four-field Event")
    return headers


def typed_extract(raw: bytes, record_header: bytes, consumer: bytes, api: dict, empty_delivery_header: bytes | None = None) -> dict:
    """Return Core/Cost/negative compile inputs extracted from actual source.

    No files are written, no process is launched. `api` is the original runner's
    globals: only its brace parser, serial adapters and fixed plan are reused.
    Source/header hashes are recorded here; typed_prepare enforces caller pins.
    """
    body, close = api["function_body"], api["close_brace"]
    text = raw.decode("utf-8-sig").replace("\r\n", "\n")
    callback = body(text, "static void inputCaptureOnGamepad(")[2]
    empty_delivery = 'static std::atomic<bool> g_emptyTelemetryHiddenHint{false};' in text
    if empty_delivery and not empty_delivery_header:
        raise ValueError("actual empty-delivery helper must be supplied and pinned")
    baseline = oracle_headers()
    start = text.index("struct GamepadSerialEvent {")
    end = close(text, text.index("{", start)) + 1
    if text[end] != ";":
        raise ValueError("typed Event terminator changed")
    event = text[start:end + 1]
    if event.count("std::unique_ptr<ymcc::gyro_telemetry::Record> gyroTelemetry;") != 1:
        raise ValueError("actual typed Event must own its Record")
    root = api["serial_mailbox_binding"](text)
    power = api["serial_action_power_binding"](text)
    if not root or not power:
        raise ValueError("typed mode requires the actual mailbox and action-power fence")
    support = api["serial_support"](event, root, power) + "\n"
    immutable_host = "static std::shared_ptr<const json> inputCaptureInactiveHostFrameOwner(" in text
    if immutable_host:
        support += body(text, "static std::shared_ptr<const json> inputCaptureInactiveHostFrameOwner(")[2] + "\n"
    support += body(text, "static const json& inputCaptureInactiveHostFrame(")[2] + "\n"
    if "kSteamTrackerCompletionMessageTag" in text:
        support += '\nnamespace GameSteamContext {\nstatic void cancelCompletionNotice(){throw std::runtime_error("unselected Steam completion branch");}\nstatic void retryCompletionNoticeOnMainEvent(){}\n}\nstruct TelemetryUnselectedSteamDispatcher {\n    void retire(){throw std::runtime_error("unselected Steam completion retire");}\n    bool drain(std::uintptr_t){throw std::runtime_error("unselected Steam completion drain");}\n};\nstatic TelemetryUnselectedSteamDispatcher* g_steamTrackerCompletionDispatcher=nullptr;\nstatic constexpr WPARAM kSteamTrackerCompletionMessageTag=0x535443;\n'

    if empty_delivery:
        # Existing Core models foreground delivery. Hidden-demand behavior has
        # a separate exact emitter consumer; no real renderer identity is forged.
        support += "\nstatic std::atomic<bool> g_emptyTelemetryHiddenHint{false};\n"
        support += "static std::atomic<unsigned long long> g_webviewGeneration{1};\n"
        support += "static unsigned long long g_webviewRenderReadyNavigationId=1;\nstatic bool g_webviewReady=false;\n"
        support += "static bool IsWindowVisible(bool value){return value;}\nstatic bool IsIconic(bool){return false;}\n"

    wrappers, source_bodies = [], {}
    for marker in ("static void gamepadSerialPostOwnedEvent(",
                   "static void gamepadSerialPostOwnedGyroTelemetry(",
                   "static void ipc_emit_owned_gyro_telemetry(",
                   "static void ipc_emit_owned_gyro_record(",
                   "static void ipc_broadcast_controller_snapshot_generated(",
                   "static void ipc_broadcast_controller_snapshot("):
        current = body(text, marker)[2]
        source_bodies[marker] = sha(current.encode("utf-8"))
        if marker.endswith("_generated("):
            current = "template<class MakeUtf8>\n" + current
        adapted = api["serial_wrapper"](current, root)
        if marker == "static void gamepadSerialPostOwnedGyroTelemetry(":
            # Same synthetic member capture as the old sender. No post body,
            # ownership, gate or dispatch semantics are otherwise substituted.
            adapted = once(adapted, "[](std::uintptr_t token)", "[this](std::uintptr_t token)")
        if empty_delivery and marker == "static void ipc_emit_owned_gyro_record(":
            adapted = once(adapted, "reinterpret_cast<std::uintptr_t>(g_view.Get())", "static_cast<std::uintptr_t>(g_view)")
            adapted = once(adapted, "static ymcc::gyro_telemetry::EmptyDelivery emptyDelivery;", "static thread_local ymcc::gyro_telemetry::EmptyDelivery emptyDelivery;")
        wrappers.append(adapted)

    marker = "gamepadSerialPostOwnedGyroTelemetry([&]()"
    if callback.count(marker) != 1:
        raise ValueError("one actual typed producer call required")
    call_start = callback.index(marker)
    opening = callback.index("{", call_start)
    closing = close(callback, opening)
    if callback[closing + 1:closing + 5] != "());":
        raise ValueError("typed Record call terminator changed")
    call = callback[call_start:closing + 5]
    expression = call[len("gamepadSerialPostOwnedGyroTelemetry("):-2]
    if expression.count("inputCaptureGyroPresentSnapshot()") != 1 or ".tree()" in expression:
        raise ValueError("typed capture must snapshot once and retain a Record")
    binding = "const json& hostFrame = *hostFrameView;"
    if callback.count(binding) != 1 or callback.index(binding) >= call_start:
        raise ValueError("actual immutable Host binding required")
    plane_marker = "const auto buildGamepadMotionPlane = [&]() -> json {"
    plane_start = callback.index(plane_marker)
    plane_end = close(callback, callback.index("{", plane_start)) + 1
    zero_marker = "const auto telemetryZeroAxes = []() -> const json& {"
    zero_start = callback.index(zero_marker)
    zero_end = close(callback, callback.index("{", zero_start)) + 1
    if callback[plane_end] != ";" or callback[zero_end] != ";" or zero_end >= call_start:
        raise ValueError("actual source-local producer dependency changed")
    plane, zero = callback[plane_start:plane_end + 1], callback[zero_start:zero_end + 1]
    gate_start = callback.index("const bool uiTelemetryCostOn =")
    gate_if = callback.index("if (g_hwnd && now - g_inputCaptureLastUi >= 80)", gate_start)
    gate_end = close(callback, callback.index("{", gate_if)) + 1
    gate = callback[gate_start:gate_end]
    if gate.count(call) != 1 or gate.count("g_inputCaptureLastUi = now;") != 1:
        raise ValueError("actual 80ms gate must enclose the sole typed producer")

    aliases = "json candidateInitializer(){return beforeInitializer();}"
    old_methods = baseline["tree-methods-extracted.h"]
    methods = old_methods[:old_methods.index(aliases)]
    setup = "\n" + binding + "\n" + plane + "\n" + zero + "\n"
    before_init = body(methods, "__declspec(noinline) json beforeInitializer()")[2]
    frozen_setup = before_init[before_init.index("{") + 1:before_init.index("json result =")]
    if frozen_setup.replace("\r\n", "\n") != setup:
        raise ValueError("actual producer locals differ from the independent JSON oracle; review required")
    # The verified private core inserted the raw original locals (mixed EOLs)
    # into its LF candidate. Preserve those exact bytes after checking their
    # tokens against today's actual producer locals; no historical main needed.
    setup = frozen_setup
    if immutable_host:
        setup += "auto retainedInactiveHostFrame = retainedInactiveSource;\n"
    frozen_plane_start = setup.index(plane_marker)
    frozen_plane_end = close(setup, setup.index("{", frozen_plane_start)) + 2
    gate_plane = setup[frozen_plane_start:frozen_plane_end]
    methods += "__declspec(noinline) ymcc::gyro_telemetry::Record candidateRecord() {\n" + setup + "return " + expression + ";\n}\n"
    methods += '''__declspec(noinline) json candidateInitializer() {
json result=candidateRecord().tree();
#if defined(YMCC_TREE_BAD_VALUE)
result["schemaVersion"]=2;
#elif defined(YMCC_TREE_BAD_TYPE)
result["schemaVersion"]=1.0;
#endif
return result;
}
'''
    methods += "__declspec(noinline) void candidatePost() {\n" + setup + call + "\ncapturePosted();\n}\n"
    methods += "void candidateGated() {\n" + binding + "\n" + ("auto retainedInactiveHostFrame = retainedInactiveSource;\n" if immutable_host else "") + gate_plane + "\n" + gate + "\ncapturePosted();\n}\n"
    case_start = text.index("    case WM_GAMEPAD_SERIAL_EVENT: {")
    case_open = text.index("{", case_start)
    case_end = close(text, case_open)
    dispatch = text[case_open + 1:case_end]
    take = "        auto event = g_gamepadSerialEventMailbox->take(static_cast<std::uintptr_t>(l));"
    dispatch = once(dispatch, take, "")
    if "fixtureTimestampUtc" in consumer.decode("utf-8-sig"):
        for signature in ("__declspec(noinline) json beforeInitializer() {", "__declspec(noinline) ymcc::gyro_telemetry::Record candidateRecord() {", "__declspec(noinline) void beforePost() {", "__declspec(noinline) void candidatePost() {"):
            methods = once(methods, signature, signature + "\nconst auto& telemetryTimestampUtc=fixtureTimestampUtc;")
    headers = dict(zip(HEADERS, (support, "\n\n".join(wrappers),
                   "int dispatchSerial(std::unique_ptr<GamepadSerialEvent> event) {\n" + ("volatile WPARAM w=0;const LPARAM l=0;\n" if "kSteamTrackerCompletionMessageTag" in dispatch else "") + dispatch + "\n}\n", methods)))
    original_fixture = project_consumer(consumer, False)
    typed_fixture = project_consumer(consumer, True)
    if empty_delivery:
        typed_fixture = once(typed_fixture, '#include "gyro_telemetry_record.h"',
            '#include "gyro_telemetry_record.h"\n#include "gyro_telemetry_empty_delivery.h"')
    files = {"Core/fixture.cpp": typed_fixture, "Core/gyro_telemetry_record.h": record_header,
             **{"Core/" + name: value for name, value in headers.items()}}
    files.update(cost_inputs(original_fixture, typed_fixture, baseline, headers, api))
    if empty_delivery:
        for folder in ("Core", "Cost", "BadWire"):
            files[folder + "/gyro_telemetry_empty_delivery.h"] = empty_delivery_header
    files["BadWire/fixture.cpp"] = typed_fixture
    wire = 'text.append("},\\\"event\\\":\\\"gyro.telemetry\\\"}");'
    files["BadWire/gyro_telemetry_record.h"] = once(record_header.decode("utf-8"), wire,
        'text.append("},\\\"event\\\":\\\"gyro.telemetry.bad\\\"}");')
    files["FakeJson/fixture.cpp"] = typed_fixture
    files["FakeJson/tree-wrappers-extracted.h"] = once(headers["tree-wrappers-extracted.h"],
        "    payload->gyroTelemetry = std::make_unique<ymcc::gyro_telemetry::Record>(std::move(data));",
        "    payload->data = data.tree();")
    proof = {"sourceSha256": sha(raw), "recordHeaderSha256": sha(record_header),
             "actualSourceBodySha256": source_bodies, "actualProducerCallSha256": sha(call.encode()),
             "actualGateSha256": sha(gate.encode()), "mailboxTakeHoistedOnce": True,
             "typedPostCaptureAdapter": "[] -> [this] only for synthetic Fixture members",
             "independentJsonOracle": True, "originalEventLayoutPreservedForCost": True,
             "typedCaptureMaterializesTree": False, "allOriginalAssertionsRetained": True,
             "lifecycleCases": LIFECYCLE, "newGlobalIds": [], "compiled": False,
             "correctnessExecuted": False, "costExecuted": False, "sourceReady": False}
    if empty_delivery:
        proof["emptyDeliveryHeaderSha256"] = sha(empty_delivery_header)
        proof["emptyDeliveryAdapter"] = "Foreground-only original Core; bool view tag and thread-local state isolate synthetic independent fixture lanes. Actual hidden emitter covered separately."
    return {"files": files, "proof": proof}


def cost_inputs(original: str, typed: str, baseline: dict, after: dict, api: dict) -> dict:
    """Exact two-world adaptation of the existing fixed cost consumer."""
    marker = "namespace current_tree {"
    def world(text):
        start = text.index(marker) + len(marker)
        return text[start:api["close_brace"](text, start - 1)]
    prefix = original[:original.index(marker)]
    prefix += '\n#include "gyro_telemetry_record.h"\nextern "C" void observeWholeCapture(const void*) noexcept;\n'
    before_world, after_world = world(original), world(typed)
    for name in HEADERS:
        before_world = before_world.replace('"' + name + '"', '"Before/' + name + '"')
        after_world = after_world.replace('"' + name + '"', '"After/' + name + '"')
    line = "if(mode==0){auto value=(f.*(candidate?&Fixture::candidateInitializer:&Fixture::beforeInitializer))();sink+=value.size();}"
    before_world = once(before_world, line,
        "if(mode==0){auto value=f.beforeInitializer();observeWholeCapture(&value);sink+=value.size();}")
    after_world = once(after_world, line,
        "if(mode==0){auto value=f.candidateRecord();observeWholeCapture(&value);sink+=27;}")
    sink = "else if(f.posted)sink+=f.posted->data.size();"
    before_world = once(before_world, sink,
        "else if(f.posted){observeWholeCapture(f.posted);sink+=f.posted->data.size();}")
    after_world = once(after_world, sink,
        "else if(f.posted){observeWholeCapture(f.posted);sink+=f.posted->gyroTelemetry?27:f.posted->data.size();}")
    combined = api["function_body"](original, "static json benchmark(const json&hosts,int campaign)")[2]
    combined = once(combined, "static json benchmark(", "static json combinedBenchmark(")
    setup = "        input.costEnabled=spec.diagnostics;input.childrenMask=spec.children?3:0;"
    combined = once(combined, setup, setup + '\n        auto typedInput=spec.persona?typed_pipeline::active(hosts,spec.persona,spec.plane,spec.yaw):typed_pipeline::inactive();\n        typedInput.costEnabled=spec.diagnostics;typedInput.childrenMask=spec.children?3:0;')
    if combined.count("measure(input,spec.mode,true") != 4:
        raise ValueError("fixed cost arm call set changed")
    combined = combined.replace("measure(input,spec.mode,true", "typedMeasure(typedInput,spec.mode,true")
    metadata = '{"cycleCounter","QueryThreadCycleTime(GetCurrentThread())"},{"wholeProcessCpuAcceptance",false}'
    combined = once(combined, metadata, metadata + ',\n        {"actualBaselineEventBytes",sizeof(baseline_pipeline::GamepadSerialEvent)},{"actualTypedEventBytes",sizeof(typed_pipeline::GamepadSerialEvent)},{"typedRecordBytes",sizeof(ymcc::gyro_telemetry::Record)},{"allCaptureFieldsEscapeThroughSeparateTranslationUnit",true}')
    adapter = '''namespace baseline_pipeline {
static Measurement typedMeasure(const typed_pipeline::Input& input,int mode,bool candidate,bool allocations,unsigned batches){
    const auto v=typed_pipeline::measure(input,mode,candidate,allocations,batches);return {v.ns,v.cycles,v.calls,v.bytes};
}
''' + combined + '\n}\n'
    main = '''int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("host-inputs.json CAMPAIGN required");
        std::ifstream stream(argv[1]);if(!stream)throw std::runtime_error("host inputs missing");json hosts;stream>>hosts;
        std::cout<<baseline_pipeline::combinedBenchmark(hosts,std::stoi(argv[2])).dump(2)<<'\\n';return 0;
    }catch(const std::exception& error){countEnabled=false;std::cerr<<error.what()<<'\\n';return 1;}
}
#else
#error Explicit YMCC_CURRENT_TREE_BUILD required
#endif
'''
    driver = prefix + '\nnamespace baseline_pipeline {\n' + before_world + '\n}\nnamespace typed_pipeline {\n' + after_world + '\n}\n' + adapter + main
    return {"Cost/cost.cpp": driver, "Cost/opaque_sink.cpp": (HERE / "opaque_sink.cpp").read_bytes(),
            **{"Cost/Before/" + name: value.replace("\r\n", "\n") if name == "tree-methods-extracted.h" else value for name, value in baseline.items()},
            **{"Cost/After/" + name: value for name, value in after.items()}}


def execution_contract(api: dict) -> dict:
    """Inputs for review, not a launcher or authority to reset a spent budget."""
    variants = [
        {"name": "Record", "fixture": "Core/fixture.cpp", "defines": [], "expectedFailure": None},
        {"name": "BadValue", "fixture": "Core/fixture.cpp", "defines": ["/DYMCC_TREE_BAD_VALUE"],
         "expectedFailure": "full telemetry types/values/float-bits"},
        {"name": "BadType", "fixture": "Core/fixture.cpp", "defines": ["/DYMCC_TREE_BAD_TYPE"],
         "expectedFailure": "full telemetry types/values/float-bits"},
        {"name": "BadWire", "fixture": "BadWire/fixture.cpp", "defines": [],
         "expectedFailure": "actual broadcast UTF16 bytes/order"},
        {"name": "FakeJson", "fixture": "FakeJson/fixture.cpp", "defines": [],
         "expectedFailure": "actual typed post owns record without JSON adapter"},
    ]
    return {"prepareOnly": True, "sourceReady": False, "limitation": RUNTIME_LIMITATION,
            "core": {"profiles": api["TREE_PLAN"]["profiles"], "variants": variants,
                     "positiveCases": 758, "lifecycleCases": LIFECYCLE,
                     "command": "negative only for BadValue/BadType; correctness for Record/BadWire/FakeJson",
                     "compileDefines": ["/DYMCC_CURRENT_TREE_BUILD"], "includeDirectory": "Core",
                     "correctnessExecutionCapMs": 300000, "compileTimeoutSeconds": 180,
                     "correctnessTimeoutSeconds": 60},
            "cost": {**api["TREE_PLAN"], "budgetMs": 300000, "compileIncludedInBudget": True,
                     "compileTimeoutSeconds": 180, "campaignTimeoutSeconds": 240,
                     "compileDefines": ["/DYMCC_CURRENT_TREE_BUILD"], "includeDirectory": "Cost",
                     "compileSeparateTranslationUnits": ["Cost/cost.cpp", "Cost/opaque_sink.cpp"],
                     "forbiddenFlags": ["/GL", "/LTCG"],
                     "requireOrdinaryX64CoffBothObjectsBeforeTiming": True,
                     "evaluator": "tree_evaluate_campaign", "stopAfterAnyFailedCampaign": True,
                     "noRetimingAfterFailure": True, "oldFailedCandidatesExecuted": False},
            "ownership": {"required": "existing owned-Job parent with full descendant return receipt",
                          "cumulativeRemainingBudgetRequired": True, "budgetResetAllowed": False,
                          "pinsAndActualDependencyClosureRequired": True,
                          "rawProcessAndBudgetIntakeRequired": True,
                          "independentParityIntakeRequiredBeforeCost": True},
            "wholeProcessCpuAcceptance": False, "newRuntimeStarted": False}


def typed_prepare(output: Path, *, source: Path, record: Path, expected_source_sha256: str,
                  expected_record_sha256: str, api: dict) -> dict:
    """Create one immutable prepared bundle; no compile/run side effects."""
    output = output.resolve()
    evidence = (api["WORKSPACE"] / "Testing/YMCC/Evidence").resolve()
    if output == evidence or not output.is_relative_to(evidence):
        raise ValueError("typed output must be a fresh development Evidence directory")
    if output.exists():
        raise FileExistsError(output)
    source, record = source.resolve(), record.resolve()
    paths = [source, record, api["TOOL"], api["HARNESS"], api["JSON_HPP"],
             api["REPO"] / "native/ipc_result_mailbox.h",
             api["REPO"] / "native/controller_persona.h", Path(__file__).resolve(),
             HERE / "json_oracle.json", HERE / "lifecycle.inc", HERE / "opaque_sink.cpp"]
    empty_path = source.parent / "gyro_telemetry_empty_delivery.h"
    empty_enabled = b"static std::atomic<bool> g_emptyTelemetryHiddenHint{false};" in source.read_bytes()
    if empty_enabled:
        paths.append(empty_path)
    initial = {path: path.read_bytes() for path in paths}
    for path, expected in ((source, expected_source_sha256), (record, expected_record_sha256)):
        if not re.fullmatch(r"[a-f0-9]{64}", expected) or sha(initial[path]) != expected:
            raise ValueError("typed prepare exact source/header identity differs: " + str(path))
    result = typed_extract(initial[source], initial[record], initial[api["HARNESS"]], api, initial[empty_path] if empty_enabled else None)
    files = result["files"]
    for folder in ("Core", "Cost"):
        for name, path in (("json.hpp", api["JSON_HPP"]),
                           ("ipc_result_mailbox.h", api["REPO"] / "native/ipc_result_mailbox.h"),
                           ("controller-persona.h", api["REPO"] / "native/controller_persona.h")):
            files[folder + "/" + name] = initial[path]
        files[folder + "/host-inputs.json"] = json.dumps(api["tree_host_inputs"](), ensure_ascii=False, indent=2) + "\n"
    files["Cost/gyro_telemetry_record.h"] = initial[record]
    # Full actual candidate source may be frozen in evidence only. The public
    # oracle holds no historical main and preparation requires no baseline main.
    files["source-candidate.cpp"] = initial[source]
    contract = execution_contract(api)
    files["EXECUTION-CONTRACT.json"] = json.dumps(contract, ensure_ascii=False, indent=2) + "\n"
    files["EXTRACTION-PROOF.json"] = json.dumps(result["proof"], ensure_ascii=False, indent=2) + "\n"
    # Detect peer edits throughout extraction, before creating any output.
    for path, raw in initial.items():
        if path.read_bytes() != raw:
            raise RuntimeError("input changed during prepare: " + str(path))
    output.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        api["tree_write"](output / name, data)
    live = [api["tree_pin"](path) for path in paths]
    for row, raw in zip(live, initial.values()):
        if row["bytes"] != len(raw) or row["sha256"] != sha(raw):
            raise RuntimeError("input changed while pinning: " + row["path"])
    frozen = [api["tree_pin"](path) for path in sorted(output.rglob("*")) if path.is_file()]
    manifest = {"mode": "typed-telemetry-build", "status": "PREPARED_NOT_COMPILED_NOT_SOURCE_READY",
                "atUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "expectedSourceSha256": expected_source_sha256,
                "expectedRecordSha256": expected_record_sha256,
                "liveInputs": live, "frozenInputs": frozen, "proof": result["proof"],
                "executionContract": contract, "compilationExecuted": False,
                "correctnessExecuted": False, "costBenchmarkExecuted": False, "sourceReady": False}
    api["tree_save"](output / "PREPARATION.json", manifest)
    return manifest
