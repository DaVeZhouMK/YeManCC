#!/usr/bin/env python3

"""Static and behavioral guard for the two-attempt log archive export contract.



This test does not start YMCC, tar.exe, FanHost, or any hardware path. It checks

that the native export implementation uses isolated temporary ZIPs, validates

before promotion, caps retries at two, and returns an explicit failure result.

"""

from __future__ import annotations



import argparse

import hashlib

import json

import pathlib

import re

from dataclasses import dataclass



ROOT = pathlib.Path(__file__).resolve().parents[1]

SOURCE = ROOT / "native" / "main.cpp"





@dataclass

class SimResult:

    ok: bool

    attempts: int

    final_content: str

    temp_cleaned: bool

    reason: str





def simulate_export(outcomes: list[str], old_final: str = "old-zip") -> SimResult:

    """Model the production contract without touching the filesystem.



    outcomes entries are: launch-failed, tar-failed, empty, success, promote-failed.

    The model intentionally stops at two attempts and never changes the final

    archive until a successful candidate is promoted.

    """

    final = old_final

    cleaned = True

    attempts = 0

    last_reason = "archive-failed-after-2-attempts"

    for outcome in outcomes[:2]:

        attempts += 1

        temp_exists = True

        if outcome == "success":

            # Candidate is non-empty/readable in this model.

            if "promote" not in outcomes[attempts - 1]:

                final = f"new-zip-attempt-{attempts}"

                temp_exists = False

                return SimResult(True, attempts, final, cleaned and not temp_exists, "ok")

        if outcome == "promote-failed":

            last_reason = "promote-failed"

        elif outcome == "launch-failed":

            last_reason = "tar-launch-failed"

        elif outcome == "tar-failed":

            last_reason = "tar-exit-nonzero"

        elif outcome == "empty":

            last_reason = "archive-empty-or-unreadable"

        else:

            last_reason = "promote-failed"

        temp_exists = False

        cleaned = cleaned and not temp_exists

    return SimResult(False, attempts, final, cleaned, last_reason)





def main() -> int:

    parser = argparse.ArgumentParser()

    parser.add_argument("--output", type=pathlib.Path)

    args = parser.parse_args()



    source_bytes = SOURCE.read_bytes()

    source = source_bytes.decode("utf-8-sig")

    failures: list[str] = []

    checks: dict[str, bool] = {}



    def check(name: str, condition: bool) -> None:

        checks[name] = bool(condition)

        if not condition:

            failures.append(name)



    helper_start = source.index("static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS")

    helper_end = source.index("// 进程内实时检测 SMT", helper_start)

    helper = source[helper_start:helper_end]

    command_path = ROOT / "native" / "log_archive_command.h"

    command_bytes = command_path.read_bytes()

    command_source = command_bytes.decode("utf-8-sig")

    capture_start = source.index("static RunOut runCapture(")

    capture_end = source.index("// 日志导出不能因为第一次", capture_start)

    capture = source[capture_start:capture_end]

    compact_helper = re.sub(r"\s+", "", helper)

    export_start = source.index('ipc_on("logs.exportAll"')

    export_all = source[export_start:]

    fan_start = source.index('ipc_on("fanLog.export"')

    fan_end = source.index('});', fan_start) + 3

    fan_export = source[fan_start:fan_end]



    check("max-attempts-is-two", "LOG_ARCHIVE_MAX_ATTEMPTS = 2" in helper)

    check("bounded-attempt-loop", "attempt <= LOG_ARCHIVE_MAX_ATTEMPTS" in helper)

    check("unique-temp-per-attempt", 'L".attempt-"' in helper and 'std::to_wstring(attempt)' in helper)

    check("per-attempt-staging-root", 'L".staging-"' in helper and "remove_all(stagingRoot" in helper)

    check("snapshot-copy-before-tar", "snapshotSharedLog(source, staged, copyError)" in helper and helper.index("snapshotSharedLog(source, staged, copyError)") < helper.index("runCapture(command"))

    check("tar-reads-staged-entries", "buildArchiveCommand(tempPath,stagingRoot,stagedEntries)" in compact_helper and (bool(re.search(r"for\s*\(const\s+auto&\s*entry\s*:\s*entries\)", command_source)) or ("autoordered=entries;orderArchiveEntries(ordered);" in re.sub(r"\s+", "", command_source) and bool(re.search(r"for\s*\(const\s+auto&\s*entry\s*:\s*ordered\)", command_source)))))

    check("archive-command-header-bound", '#include "log_archive_command.h"' in helper)

    # The real native consumer excludes this unrelated IPC include explicitly;
    # its placement in main is not an archive or cancellation behavior contract.
    consumer = (ROOT / "tools" / "steam_log_export_selftest.py").read_text(encoding="utf-8-sig")
    sleep_include = '#include "sleep_export_diagnostics_native.inc"'
    exclusion = 'helper=text[start:end].replace(' + repr(sleep_include) + "," + repr('// Unrelated sleep IPC is outside this archive consumer.') + ')'
    fixture_helper = helper.replace(sleep_include, '// Unrelated sleep IPC is outside this archive consumer.')
    check("archive-helper-excludes-unrelated-sleep-globals", exclusion in consumer and sleep_include not in fixture_helper and "static TarArchiveRetryResult createTarArchiveWithRetry(" in fixture_helper)

    check("list-uses-local-acp-with-strict-roundtrip", all(token in command_source for token in ("GetACP()", "WC_NO_BEST_FIT_CHARS", "WC_ERR_INVALID_CHARS", "MB_ERR_INVALID_CHARS", "roundtrip==text")))

    check("list-file-opened-by-wide-owned-handle", "CreateFileW(commandPlan.inputPath.c_str(),GENERIC_READ" in compact_helper and "OPEN_EXISTING" in helper and "SECURITY_ATTRIBUTES inputSecurity" in helper)

    check("tar-reads-list-from-inherited-stdin", " -T -" in command_source and "si.hStdInput = stdinHandle" in capture)

    check("tar-zip-stream-uses-owned-output-handle", "--format=zip -c -f -" in command_source and "archiveOutput=CreateFileW(" in compact_helper and "fspath::path(tempPath)" in helper and "si.hStdOutput = stdoutHandle ? stdoutHandle : hOutW" in capture)

    check("tar-has-exact-handles-wide-cwd-and-original-budget", "runCapture(command,120000,{},archiveInput,archiveOutput,stagingRoot.c_str())" in compact_helper and "nullptr, workingDirectory, &si, &pi" in capture)

    before_validation = compact_helper[:compact_helper.index("constboolarchiveReady")]

    check("caller-closes-archive-handles-before-validation", "CloseHandle(archiveInput)" in before_validation and "CloseHandle(archiveOutput)" in before_validation)

    check("capture-does-not-close-caller-owned-archive-handles", "CloseHandle(stdinHandle)" not in capture and "CloseHandle(stdoutHandle)" not in capture)

    check("archive-result-retains-additive-command-evidence", all(field in helper for field in ("commandCharacters", "listEntries", "wideArgumentEntries", "completion")))

    check("archive-result-retains-native-completion", "result.completion=run.completion" in compact_helper)

    check("launch-error-comes-from-run-win32-not-last-error", "result.win32Error=run.win32Error" in compact_helper)

    check("child-failure-keeps-original-exit-code", "result.win32Error=run.exitCode" in compact_helper)

    check("list-control-files-stay-in-owned-staging", "plan.inputPath=(root/" in re.sub(r"\s+", "", command_source) and "CREATE_NEW" in command_source and "remove_all(stagingRoot" in helper)

    check("zip-member-basename-not-prefixed-with-dot-slash", 'L"./"+' not in command_source and 'L"./" +' not in command_source)

    check("cleanup-before-attempt", "fspath::remove(tempPath, staleEc)" in helper)

    check("cleanup-after-failure", "fspath::remove(tempPath, removeEc)" in helper)

    check("candidate-validated-before-promote", "archiveOutputReadableAndNonEmpty(tempPath)" in helper)

    check("final-validated-after-promote", "archiveOutputReadableAndNonEmpty(finalPath)" in helper)

    check("atomic-promotion", "MoveFileExW" in helper and "MOVEFILE_REPLACE_EXISTING" in helper)

    check("explicit-failure-result", '"reason", "archive-failed-after-2-attempts"' in export_all)

    check("failure-reports-attempts", '"attempts", packed.attempts' in export_all)

    check("failure-reports-cleanup", '"cleanupOk", packed.cleanupOk' in export_all)

    check("fan-export-uses-same-retry", "createTarArchiveWithRetry(zipPath, files)" in fan_export)

    check("no-direct-final-tar", 'tar.exe\\\" -a -c -f " + quote(zipPath)' not in export_all)



    one_then_two = simulate_export(["tar-failed", "success"])

    check("first-failure-second-success", one_then_two.ok and one_then_two.attempts == 2 and one_then_two.final_content != "old-zip")

    both_fail = simulate_export(["tar-failed", "empty", "success"])

    check("two-failures-stop-at-two", not both_fail.ok and both_fail.attempts == 2)

    check("failed-export-preserves-old-final", both_fail.final_content == "old-zip")

    immediate_success = simulate_export(["success", "success"])

    check("success-does-not-retry", immediate_success.ok and immediate_success.attempts == 1)



    launch_recovered = simulate_export(["launch-failed", "success"])

    check("launch-failure-retries-once-then-succeeds", launch_recovered.ok and launch_recovered.attempts == 2 and launch_recovered.temp_cleaned)

    for failure in ("launch-failed", "tar-failed", "empty", "promote-failed"):

        result = simulate_export([failure, failure, "success"])

        check("bounded-failure-preserves-final-and-cleans:" + failure, not result.ok and result.attempts == 2 and result.final_content == "old-zip" and result.temp_cleaned)

    promote_recovered = simulate_export(["promote-failed", "success"])

    check("promotion-failure-does-not-consume-extra-attempt", promote_recovered.ok and promote_recovered.attempts == 2 and promote_recovered.temp_cleaned)



    check("source-and-command-header-stable", SOURCE.read_bytes() == source_bytes and command_path.read_bytes() == command_bytes)

    report = {

        "ok": not failures,

        "failures": failures,

        "checks": checks,

        "sourceSha256": hashlib.sha256(source_bytes).hexdigest().upper(),

        "archiveCommandHeaderSha256": hashlib.sha256(command_bytes).hexdigest().upper(),

        "boundArchiveHelperSha256": hashlib.sha256(helper.encode("utf-8")).hexdigest().upper(),

        "sourceStable": SOURCE.read_bytes() == source_bytes and command_path.read_bytes() == command_bytes,

        "realProductOrHardwareExecuted": False,

        "executionPolicyChanged": False,

    }

    text = json.dumps(report, ensure_ascii=False, indent=2) + "\n"

    if args.output:

        args.output.parent.mkdir(parents=True, exist_ok=True)

        args.output.write_text(text, encoding="utf-8")

    print(text, end="")

    return 0 if not failures else 1





if __name__ == "__main__":

    raise SystemExit(main())

