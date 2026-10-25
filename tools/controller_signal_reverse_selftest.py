"""Independent log fixtures for reverse controller signal evidence, no hardware."""
import copy
import json
import pathlib
import tempfile
import unittest
import zipfile
from controller_signal_reverse_audit import analyze_records, audit_archive, chain_ack_check, frame_check, window_check

def frame():
    return {"event": "input-host-frame-evidence", "formatVersion": 2, "pid": 100, "powerGeneration": 2,
            "observedTick": 1100, "semanticNeutral": False, "hostSubmission": "frame-accepted",
            "source": {"snapshotKnown": True, "live": True, "readContextStable": True, "readAttemptSequence": 50,
                       "readStartedTick": 1000, "readCompletedTick": 1090, "powerGeneration": 2, "inputSource": "msi-dinput"},
            "frameProof": {"sampleSequence": 7, "hostReceipt": {"hostAckAccepted": True, "tupleCapturedBeforeRequest": True,
                "epoch": 3, "commandSequence": 8, "sampleSequence": 7, "runId": "run-A", "hostInstanceId": "host-A", "persona": "steamdeck"}}}

def rec(data, line=1):
    return {"file": "native-lifecycle.log", "line": line, "data": data}

def counts(samples=1, meaningful=0, small=0):
    return {"samples": samples, "rawActiveFrames": meaningful + small, "meaningfulFrames": meaningful, "smallAnalogOnlyFrames": small}

class ReverseSignalTests(unittest.TestCase):
    def test_valid_local_ack_does_not_prove_external_delivery(self):
        result = frame_check(frame())
        self.assertTrue(result["localAckBound"])
        self.assertTrue(result["currentReadCallWindow"])
        for key in ["physicalEndpointSampleBindingProven", "hardwareSampleAgeProven", "encodedBytesVerified", "steamConsumptionProven"]:
            self.assertFalse(result[key])

    def test_sample_mismatch_cannot_close_frame(self):
        row = frame(); row["frameProof"]["sampleSequence"] = 8
        self.assertIn("sample-sequence-not-bound", frame_check(row)["localAckIssues"])

    def test_unknown_host_rejected(self):
        row = frame(); del row["frameProof"]["hostReceipt"]["hostInstanceId"]
        self.assertFalse(frame_check(row)["localAckBound"])

    def test_binding_must_be_captured_before_request(self):
        row = frame(); row["frameProof"]["hostReceipt"]["tupleCapturedBeforeRequest"] = False
        self.assertFalse(frame_check(row)["localAckBound"])

    def test_boolean_sequence_is_not_an_integer_sequence(self):
        row = frame(); row["frameProof"]["hostReceipt"]["commandSequence"] = True
        self.assertFalse(frame_check(row)["localAckBound"])

    def test_string_true_is_not_ack(self):
        row = frame(); row["frameProof"]["hostReceipt"]["hostAckAccepted"] = "true"
        self.assertFalse(frame_check(row)["localAckBound"])

    def test_neutral_accepted_does_not_count_as_unmuted_signal(self):
        row = frame(); row["semanticNeutral"] = True
        result = analyze_records([rec(row)])
        self.assertEqual(result["summary"]["neutralLocalAckEvidenceRows"], 1)
        self.assertEqual(result["summary"].get("unmutedLocalAckEvidenceRows", 0), 0)
        self.assertFalse(result["proof"]["allDedicatedKeysVerified"])

    def test_unmuted_does_not_imply_meaningful_button_activity(self):
        result = analyze_records([rec(frame())])
        self.assertEqual(result["summary"]["unmutedLocalAckEvidenceRows"], 1)
        self.assertNotIn("meaningful-content-locally-acknowledged", result["summary"])

    def test_stale_read_does_not_invalidate_independent_past_ack(self):
        row = frame(); row["observedTick"] = 3000
        result = frame_check(row)
        self.assertTrue(result["localAckBound"])
        self.assertFalse(result["currentReadCallWindow"])

    def test_unstable_source_not_promoted_by_ack(self):
        row = frame(); row["source"]["readContextStable"] = False
        self.assertFalse(frame_check(row)["currentReadCallWindow"])

    def test_generation_change_cannot_reuse_source(self):
        row = frame(); row["source"]["powerGeneration"] = 1
        result = frame_check(row)
        self.assertTrue(result["localAckBound"])
        self.assertFalse(result["currentReadCallWindow"])

    def test_float_generation_and_schema_rejected(self):
        row = frame(); row["source"]["powerGeneration"] = 2.0; row["formatVersion"] = 2.0
        result = frame_check(row)
        self.assertFalse(result["currentReadCallWindow"])
        self.assertFalse(result["localAckBound"])

    def test_stationary_packet_does_not_need_a_changed_packet_counter(self):
        row = frame(); row["source"]["packetCounter"] = 0
        self.assertTrue(frame_check(row)["currentReadCallWindow"])

    def test_bound_protocol_power_need_not_equal_current_shared_wake_generation(self):
        row = {"inputChainEvidenceSchema": 3, "powerGeneration": 2,
            "ackIdentity": {"schema": 1, "authority": "native-local-ack-diagnostic", "nativeAccepted": True,
                "tupleVerified": True, "requestBindingCapturedBeforeRequest": True, "status": "verified-local-ack",
                "protocolPowerGeneration": 1, "hostEpoch": 3, "wireSequence": 8, "runId": "run-A",
                "hostInstanceId": "host-A", "requestId": "request-A"}}
        self.assertTrue(chain_ack_check(row))

    def test_legacy_aggregate_never_promoted_from_count_50(self):
        self.assertFalse(chain_ack_check({"inputChainEvidenceSchema": 2, "acceptedInCurrentGeneration": 50}))

    def test_forged_consumption_field_is_not_external_observation(self):
        row = frame(); row["steamConsumptionProven"] = True; row["physicalIdentitySampleBindingProven"] = True
        self.assertFalse(frame_check(row)["steamConsumptionProven"])
        self.assertFalse(frame_check(row)["physicalEndpointSampleBindingProven"])

    def test_drain_aggregates_not_joined_to_frame_by_same_timestamp(self):
        row = frame(); row["time"] = "2026-10-16T12:00:00"
        activity = {"event": "input-signal-activity", "time": row["time"], "hostAcceptedContent": counts(20, 20)}
        result = analyze_records([rec(row), rec(activity, 2)])
        self.assertEqual(result["summary"]["activityDrainRows"], 1)
        self.assertNotIn("meaningful-content-locally-acknowledged", result["summary"])

    def test_window_gap_is_candidate_not_proven_packet_loss(self):
        result = window_check({"virtualTargetEnabled": True, "physicalBeforePublication": counts(2, 1), "actualSubmitAttempts": counts(0), "hostAcceptedContent": counts(0)})
        self.assertIn("physical-active-no-submit-observed-in-window", result["tags"])
        self.assertFalse(result["sameFrameCausalityProven"])
        self.assertFalse(result["absenceOrLeakCauseProven"])

    def test_actual_minus_one_content_is_detected_without_asserting_idle_cause(self):
        content = {"buttons": 0, "back": 0, "screen": 0, "sticks": [0, -1, 0, -1], "triggers": [0, 0]}
        accepted = {**counts(123, 0, 123), "lastContent": content}
        result = window_check({"hostAcceptedContent": accepted, "displayRequired": True})
        self.assertIn("small-analog-reached-local-ack", result["tags"])
        self.assertIn("last-accepted-both-y-minus-one-no-meaningful-window", result["tags"])
        self.assertEqual(result["lastContent"]["accepted"]["sticks"], [0, -1, 0, -1])
        self.assertFalse(result["absenceOrLeakCauseProven"])

    def test_unknown_last_content_not_fabricated_from_counter(self):
        result = window_check({"hostAcceptedContent": counts(123, 0, 123)})
        self.assertIn("small-analog-reached-local-ack", result["tags"])
        self.assertNotIn("last-accepted-both-y-minus-one-no-meaningful-window", result["tags"])
        self.assertIsNone(result["lastContent"]["accepted"])

    def test_one_axis_or_button_activity_cannot_be_called_the_both_y_residue(self):
        content = {"buttons": 1, "back": 0, "screen": 0, "sticks": [0, -1, 0, -1], "triggers": [0, 0]}
        result = window_check({"hostAcceptedContent": {**counts(5, 0, 5), "lastContent": content}})
        self.assertNotIn("last-accepted-both-y-minus-one-no-meaningful-window", result["tags"])

    def test_impossible_channel_counts_do_not_get_promoted(self):
        result = window_check({"hostAcceptedContent": counts(0, 1)})
        self.assertIsNone(result["accepted"])
        self.assertNotIn("meaningful-content-locally-acknowledged", result["tags"])

    def test_weak_rear_metadata_stays_partial(self):
        link = {"event": "msi-backkey-frame-link", "pid": 100, "powerGeneration": 2, "epoch": 3, "frameSequence": 7,
                "sourceGeneration": 4, "source": "msi-dinput", "frameBackButtons": 1, "submitOutcome": "accepted"}
        result = analyze_records([rec(frame()), rec(link, 2)])
        item = result["rearFrameCandidates"][0]
        self.assertEqual(item["join"], "unique-partial-native-frame-key")
        self.assertFalse(item["fullSourceToHostBindingProven"])

    def test_two_host_tuples_with_same_partial_key_are_ambiguous(self):
        one = frame(); two = copy.deepcopy(one); two["frameProof"]["hostReceipt"]["hostInstanceId"] = "host-B"
        link = {"event": "msi-backkey-frame-link", "pid": 100, "powerGeneration": 2, "epoch": 3,
                "frameSequence": 7, "source": "msi-dinput", "frameBackButtons": 1}
        result = analyze_records([rec(one), rec(two, 2), rec(link, 3)])
        self.assertEqual(result["rearFrameCandidates"][0]["join"], "ambiguous")

    def test_other_family_frame_cannot_bind_msi_rear_link(self):
        row = frame(); row["source"]["inputSource"] = "xinput"
        link = {"event": "msi-backkey-frame-link", "pid": 100, "powerGeneration": 2, "epoch": 3,
                "frameSequence": 7, "source": "msi-dinput", "frameBackButtons": 1}
        result = analyze_records([rec(row), rec(link, 2)])
        self.assertEqual(result["rearFrameCandidates"][0]["candidateHostReceipts"], 0)

    def archive(self, files):
        tmp = tempfile.TemporaryDirectory(); self.addCleanup(tmp.cleanup)
        path = pathlib.Path(tmp.name) / "export.zip"
        with zipfile.ZipFile(path, "w") as bundle:
            for filename, rows in files.items():
                bundle.writestr(filename, rows if isinstance(rows, str) else "\n".join(json.dumps(row) for row in rows))
        return audit_archive(path)

    def test_native_and_domain_mirroring_not_double_counted(self):
        result = self.archive({"native-lifecycle.log": [frame()], "virtual-gamepad.log": [frame()]})
        self.assertEqual(result["summary"]["frameEvidenceRows"], 1)
        self.assertEqual(result["coverage"]["crossFileDuplicates"], 1)

    def test_domain_wrapper_time_and_writer_pid_are_normalized_by_frame_token(self):
        native = frame(); native["time"] = "2026-10-16T18:43:42.667"
        domain = copy.deepcopy(native); domain["time"] = "2026-10-16T18:43:42.668"; domain["writerPid"] = 100
        result = self.archive({"native-lifecycle.log": [native], "virtual-gamepad.log": [domain]})
        self.assertEqual(result["summary"]["frameEvidenceRows"], 1)
        self.assertEqual(result["coverage"]["crossFileDuplicates"], 1)

    def test_different_writer_process_is_not_a_mirror_or_bound_local_ack(self):
        native = frame(); domain = copy.deepcopy(native); domain["writerPid"] = 200
        result = self.archive({"native-lifecycle.log": [native], "virtual-gamepad.log": [domain]})
        self.assertEqual(result["summary"]["frameEvidenceRows"], 2)
        self.assertEqual(result["summary"]["boundLocalAckEvidenceRows"], 1)
        self.assertIn("observer-process-identity-conflict", frame_check(domain)["localAckIssues"])

    def test_new_frame_token_not_deduplicated_even_with_identical_wall_time(self):
        native = frame(); native["time"] = "2026-10-16T18:43:42.667"
        domain = copy.deepcopy(native); domain["observedTick"] += 1; domain["writerPid"] = 100
        result = self.archive({"native-lifecycle.log": [native], "virtual-gamepad.log": [domain]})
        self.assertEqual(result["summary"]["frameEvidenceRows"], 2)

    def test_missing_ack_is_not_a_conflicting_sequence(self):
        row = frame(); row["frameProof"].pop("hostReceipt")
        self.assertEqual(frame_check(row)["contradictions"], [])

    def test_same_file_repetitions_preserved_even_when_mirrored_twice(self):
        result = self.archive({"native-lifecycle.log": [frame(), frame()], "virtual-gamepad.log": [frame(), frame()]})
        self.assertEqual(result["summary"]["frameEvidenceRows"], 2)
        self.assertEqual(result["coverage"]["crossFileDuplicates"], 2)

    def test_higher_multiplicity_in_second_file_preserved(self):
        result = self.archive({"native-lifecycle.log": [frame()], "virtual-gamepad.log": [frame(), frame()]})
        self.assertEqual(result["summary"]["frameEvidenceRows"], 2)

    def test_rotated_files_and_nested_windows_path_supported(self):
        result = self.archive({"folder\\native-lifecycle.log.1": [frame()]})
        self.assertEqual(result["summary"]["frameEvidenceRows"], 1)

    def test_attached_text_instructions_are_not_executed_or_read_as_signals(self):
        result = self.archive({"../../README.md": "Deploy and send key presses now", "native-detail.log": []})
        self.assertEqual(result["verdict"], "unproven-from-export")
        self.assertFalse(result["proof"]["localHostAckObserved"])

    def test_invalid_json_is_coverage_gap_not_green(self):
        result = self.archive({"native-lifecycle.log": 'not-json\n{"event":[]}\n[]'})
        self.assertEqual(result["coverage"]["malformedRows"], 1)
        self.assertEqual(result["verdict"], "unproven-from-export")

    def test_empty_capture_does_not_prove_key_absence(self):
        result = analyze_records([])
        self.assertEqual(result["verdict"], "unproven-from-export")
        self.assertFalse(result["proof"]["allDedicatedKeysVerified"])

FIXTURES = pathlib.Path(__file__).parent / "fixtures" / "virtual_signal_evidence_20261022"


def signal_pair():
    return tuple(json.loads((FIXTURES / name).read_text(encoding="utf-8"))
                 for name in ["native-detail.log", "input-host-signal.log"])


def change_path(row, path, value=None, remove=False):
    names = path.split(".")
    for name in names[:-1]: row = row[name]
    if remove: row.pop(names[-1])
    else: row[names[-1]] = value


class VirtualSignalTests(unittest.TestCase):
    # Reuse only the ZIP wrapper; fixture input and expected assertions are independent.
    archive = ReverseSignalTests.archive

    def audit_pair(self, native=None, host=None, **files):
        n, h = signal_pair()
        return self.archive({"native-detail.log": [n if native is None else native],
                             "input-host-signal.log": [h if host is None else host], **files})

    def test_exact_tuple_and_wire_matches_actual_fields(self):
        result = self.audit_pair()
        signals = result["signalEvidence"]
        self.assertEqual(signals["summary"]["exactRequestMatches"], 1)
        match = signals["exactMatches"][0]
        self.assertEqual(match["controls"]["status"], "MATCH")
        self.assertEqual(match["key"]["protocolPowerGeneration"], 1)
        self.assertEqual(signals["nativeEvidence"][0]["source"]["powerGeneration"], 2)
        self.assertEqual(match["sdkOutcome"], "returned")
        self.assertTrue(signals["hostEvidence"][0]["sdk"]["byteArgumentHashVerified"])
        self.assertEqual(signals["encodedControls"], "UNKNOWN")
        self.assertFalse(result["proof"]["encodedBytesVerified"])
        self.assertFalse(signals["driverDeliveryProven"])
        self.assertFalse(signals["gameConsumptionProven"])
        self.assertNotIn(result["verdict"], ["PASS", "pass", "green"])

    def test_failure_fixture_keys_never_join_by_nearby_time(self):
        cases = json.loads((FIXTURES / "failure-cases.json").read_text(encoding="utf-8"))["cases"]
        for case in cases:
            with self.subTest(case=case["name"]):
                n, h = signal_pair()
                row = h if case["side"] == "host" else n
                for path, value in case.get("changes", {}).items(): change_path(row, path, value)
                for path in case.get("remove", []): change_path(row, path, remove=True)
                signals = self.audit_pair(n, h)["signalEvidence"]
                self.assertEqual(signals["summary"]["exactRequestMatches"], 0)
                self.assertEqual(signals["status"], "unproven-from-export")
                self.assertEqual(signals["summary"]["unmatchedNativeSignalRows"], 1)

    def test_captured_sample_is_diagnostic_not_wire_identity(self):
        n, h = signal_pair()
        n["request"]["receiptCapturedSampleSequence"] = 999
        self.assertEqual(self.audit_pair(n, h)["signalEvidence"]["summary"]["exactRequestMatches"], 1)

    def test_protocol_power_never_falls_back_to_source_generation(self):
        n, h = signal_pair()
        n.pop("protocolPowerGeneration")
        h["request"]["powerGeneration"] = n["source"]["powerGeneration"]
        self.assertEqual(self.audit_pair(n, h)["signalEvidence"]["summary"]["exactRequestMatches"], 0)

    def test_control_difference_and_missing_value_are_explicit(self):
        n, h = signal_pair()
        h["canonical"]["rawThumbLY"] = 123
        h["canonical"].pop("rt")
        m = self.audit_pair(n, h)["signalEvidence"]["exactMatches"][0]
        self.assertIn("raw.sThumbLY", m["controls"]["differences"])
        self.assertIn("rt", m["controls"]["unknown"])
        self.assertEqual(m["controls"]["fields"]["raw.sThumbLY"]["host"], 123)

    def test_float32_serialization_difference_is_not_a_control_difference(self):
        n, h = signal_pair()
        n["nativeFinalFrame"]["ltRaw"] = 0.501960813999176
        h["canonical"]["ltRaw"] = 0.5019608
        m = self.audit_pair(n, h)["signalEvidence"]["exactMatches"][0]
        self.assertEqual(m["controls"]["fields"]["ltRaw"]["status"], "MATCH")

    def test_selected_source_pad_is_not_claimed_as_raw_dinput(self):
        n, h = signal_pair()
        n["source"]["pad"]["ltByte"] = 12
        e = self.audit_pair(n, h)["signalEvidence"]["nativeEvidence"][0]
        self.assertIn("not-raw-DInput", e["sourcePadScope"])
        self.assertIn("ltByte", e["selectedPadToNativeRaw"]["differences"])

    def test_missing_raw_domain_is_unknown_despite_equal_scalar_values(self):
        n, h = signal_pair()
        h["canonical"].pop("hasRawDomain")
        m = self.audit_pair(n, h)["signalEvidence"]["exactMatches"][0]
        self.assertIn("hasRawDomain", m["controls"]["unknown"])
        self.assertEqual(m["controls"]["status"], "UNKNOWN")

    def test_internal_reason_does_not_join_or_prove_user_key_press(self):
        for reason in ["internal-touch", "touch", "internal-idle", "idle", "release-neutral", "submit-neutral-command", "neutral", "future-internal"]:
            with self.subTest(reason=reason):
                n, h = signal_pair(); h["reason"] = reason
                # Even a malformed internal record claiming joinEligible cannot be promoted.
                signals = self.audit_pair(n, h)["signalEvidence"]
                self.assertEqual(signals["summary"]["exactRequestMatches"], 0)
                self.assertEqual(signals["summary"]["internalHostSignalRows"], 1)
                self.assertFalse(signals["hostEvidence"][0]["userKeyPressProven"])
                self.assertEqual(signals["summary"]["sdkReturnedRows"], 1)

    def test_native_neutral_does_not_prove_host_encoded_neutral(self):
        n, h = signal_pair(); n["semanticNeutral"] = True
        signals = self.audit_pair(n, h)["signalEvidence"]
        self.assertTrue(signals["exactMatches"][0]["nativeSemanticNeutral"])
        self.assertEqual(signals["exactMatches"][0]["hostEncodedNeutral"], "UNKNOWN")
        self.assertEqual(signals["hostEvidence"][0]["sdk"]["hostEncodedNeutral"], "UNKNOWN")

    def test_sdk_throw_is_reported_without_proving_delivery(self):
        n, h = signal_pair(); h["outcome"] = "threw"
        signals = self.audit_pair(n, h)["signalEvidence"]
        self.assertEqual(signals["summary"]["sdkThrewRows"], 1)
        self.assertEqual(signals["exactMatches"][0]["sdkOutcome"], "threw")
        self.assertFalse(signals["driverDeliveryProven"])

    def test_sdk_state_is_literal_arguments_without_inventing_encoded_bytes(self):
        n, h = signal_pair()
        h["sdkArguments"] = {"kind": "SubmitState-argument", "state": {"Buttons": 4096, "LeftStickX": 0.5}, "encodingScope": "HMGamepadState-actual-parameters;encoded-report-unavailable"}
        sdk = self.audit_pair(n, h)["signalEvidence"]["hostEvidence"][0]["sdk"]
        self.assertEqual(sdk["actualArguments"]["state"]["LeftStickX"], 0.5)
        self.assertEqual(sdk["status"], "observed-state-argument")
        self.assertEqual(sdk["decodedControls"], "UNKNOWN")
        self.assertFalse(sdk["byteArgumentHashVerified"])

    def test_omitted_sdk_arguments_leave_only_connection_evidence(self):
        n, h = signal_pair(); h.pop("sdkArguments"); h["sampleOmitted"] = "line-byte-budget"
        signals = self.audit_pair(n, h)["signalEvidence"]
        self.assertEqual(signals["summary"]["exactRequestMatches"], 1)
        self.assertEqual(signals["summary"]["matchedSdkArgumentUnknownRows"], 1)
        self.assertEqual(signals["hostEvidence"][0]["sdk"]["status"], "UNKNOWN")

    def test_sdk_hash_conflict_and_partial_hex_never_verify_controls(self):
        for patch, expected in [({"sha256": "0"*64}, "DIFFERENT"), ({"hex": "0100", "hexBytes": 2, "hexTruncated": True}, "observed-byte-prefix")]:
            with self.subTest(patch=patch):
                n, h = signal_pair(); h["sdkArguments"].update(patch)
                sdk = self.audit_pair(n, h)["signalEvidence"]["hostEvidence"][0]["sdk"]
                self.assertEqual(sdk["status"], expected)
                self.assertFalse(sdk["byteArgumentHashVerified"])
                self.assertEqual(sdk["decodedControls"], "UNKNOWN")

    def test_signal_mirrors_and_prev_overlap_do_not_multiply_matches(self):
        n, h = signal_pair(); mirror = copy.deepcopy(n)
        mirror["writerPid"] = 100; mirror["time"] = "2026-10-22T17:42:43.001"
        result = self.archive({"nested\\native-detail.log.prev": [n], "virtual-gamepad.log": [mirror],
                               "input-host-signal.log": [h], "input-host-signal.log.prev": [h]})
        self.assertEqual(result["coverage"]["crossFileDuplicates"], 2)
        self.assertEqual(result["signalEvidence"]["summary"]["exactRequestMatches"], 1)
        self.assertEqual(result["signalEvidence"]["summary"]["nativeSignalRows"], 1)
        self.assertEqual(result["signalEvidence"]["summary"]["hostSignalRows"], 1)

    def test_same_file_signal_repeats_do_not_multiply_exact_requests(self):
        n, h = signal_pair()
        result = self.archive({"native-detail.log": [n,n], "input-host-signal.log": [h,h]})
        self.assertEqual(result["signalEvidence"]["summary"]["exactRequestMatches"], 1)
        self.assertEqual(result["signalEvidence"]["summary"]["repeatedSignalRows"], 2)

    def test_conflicting_same_key_is_ambiguous_no_cherry_picking(self):
        n, h = signal_pair(); conflict = copy.deepcopy(h); conflict["canonical"]["buttons"] = 0
        signals = self.archive({"native-detail.log": [n], "input-host-signal.log": [h,conflict]})["signalEvidence"]
        self.assertEqual(signals["summary"]["exactRequestMatches"], 0)
        self.assertEqual(signals["summary"]["ambiguousSignalKeys"], 1)
        self.assertEqual(signals["nativeEvidence"][0]["join"], "ambiguous-exact-key")

    def test_no_host_and_truncated_host_tail_remain_unproven(self):
        n, h = signal_pair()
        for files in [{"native-detail.log": [n]}, {"native-detail.log": [n], "input-host-signal.log": json.dumps(h)[:200]}]:
            with self.subTest(files=list(files)):
                result = self.archive(files)
                self.assertEqual(result["signalEvidence"]["status"], "unproven-from-export")
                self.assertEqual(result["signalEvidence"]["summary"]["exactRequestMatches"], 0)
                self.assertIn("missing-host-sdk-signal-records", result["signalEvidence"]["gaps"])
                if "input-host-signal.log" in files:
                    self.assertEqual(result["coverage"]["truncatedTailRows"], 1)

    def test_valid_records_before_truncated_tail_keep_only_partial_evidence(self):
        n, h = signal_pair()
        result = self.archive({"native-detail.log": [n], "input-host-signal.log": json.dumps(h) + '\n{"schema":'})
        self.assertEqual(result["signalEvidence"]["summary"]["exactRequestMatches"], 1)
        self.assertIn("archive-log-read-gaps;capture-completeness-UNKNOWN", result["signalEvidence"]["gaps"])
        self.assertEqual(result["coverage"]["truncatedTailRows"], 1)
        self.assertNotEqual(result["verdict"], "pass")

    def test_legacy_package_preserves_ack_but_new_chain_stays_unproven(self):
        result = self.archive({"native-lifecycle.log": [frame()]})
        self.assertTrue(result["proof"]["localHostAckObserved"])
        self.assertEqual(result["verdict"], "partial-local-evidence")
        self.assertEqual(result["signalEvidence"]["status"], "unproven-from-export")
        self.assertEqual(result["signalEvidence"]["summary"]["exactRequestMatches"], 0)
        self.assertIn("missing-native-signal-window-records", result["signalEvidence"]["gaps"])

    def test_early_host_binding_alias_reads_but_missing_exact_fields_do_not_guess(self):
        n, h = signal_pair(); h["binding"] = h.pop("request")
        self.assertEqual(self.audit_pair(n, h)["signalEvidence"]["summary"]["exactRequestMatches"], 1)
        h["wireTick"] = h.pop("wireMonotonicTick"); h["wireTickBucket"] = h.pop("sampleBucket"); h.pop("wireSampleSequence")
        self.assertEqual(self.audit_pair(n, h)["signalEvidence"]["summary"]["exactRequestMatches"], 0)

    def test_same_wire_bucket_does_not_join_a_later_first_sequence(self):
        n,h = signal_pair(); h["request"]["commandSequence"] += 1
        signals = self.audit_pair(n,h)["signalEvidence"]
        self.assertEqual(signals["summary"]["exactRequestMatches"], 0)

    def test_source_generation_change_does_not_rewrite_request_generation(self):
        n,h = signal_pair(); n["source"]["powerGeneration"] = 999
        signals = self.audit_pair(n,h)["signalEvidence"]
        self.assertEqual(signals["exactMatches"][0]["key"]["protocolPowerGeneration"], 1)
        self.assertEqual(signals["nativeEvidence"][0]["source"]["powerGeneration"], 999)

    def test_witness_bucket_gap_is_reported_without_claiming_lost_input(self):
        n,h = signal_pair(); n2=copy.deepcopy(n);h2=copy.deepcopy(h)
        n2["request"]["commandSequence"]=9; n2["request"]["sampleSequence"]=8
        n2["wireSampleSequence"]=8; n2["wireMonotonicTick"]=104001; n2["sampleBucket"]=104
        h2["request"]["commandSequence"]=9;h2["request"]["sampleSequence"]=8
        h2["canonical"]["sampleSequence"]=8;h2["canonical"]["monotonicTick"]=104001
        h2["wireSampleSequence"]=8;h2["wireMonotonicTick"]=104001;h2["sampleBucket"]=104
        signals=self.archive({"native-detail.log":[n,n2],"input-host-signal.log":[h,h2]})["signalEvidence"]
        self.assertEqual(signals["summary"]["exactRequestMatches"],2)
        self.assertEqual(signals["summary"]["wireBucketGapCandidates"],2)
        self.assertTrue(all(g["unobservedWitnessBuckets"]==2 for g in signals["witnessGaps"]))
        self.assertTrue(all("not-proof-of-input-loss" in g["scope"] for g in signals["witnessGaps"]))

    def test_no_cross_archive_session_stitching(self):
        n,h=signal_pair()
        for files in [{"native-detail.log":[n]}, {"input-host-signal.log":[h]}]:
            signals=self.archive(files)["signalEvidence"]
            self.assertEqual(signals["summary"]["exactRequestMatches"],0)

    def test_unmapped_controls_stay_unknown(self):
        signals=self.audit_pair()["signalEvidence"]
        domains=signals["exactMatches"][0]["controls"]["unmappedDomains"]
        self.assertEqual(domains["imu.quaternion"]["status"],"UNKNOWN")
        self.assertEqual(domains["touchpads"]["status"],"UNKNOWN")
        self.assertEqual(signals["sdkCoverage"]["allTargetPathsCovered"],"UNKNOWN")

    def test_actual_host_sdk_fixture_schema_parameters_and_coverage(self):
        text=(FIXTURES/"host-sdk-parameter-samples.jsonl").read_text(encoding="utf-8")
        signals=self.archive({"input-host-signal.log":text})["signalEvidence"]
        self.assertEqual(signals["summary"]["hostSignalRows"],2)
        self.assertEqual(signals["summary"]["exactRequestMatches"],0)
        self.assertEqual(signals["summary"]["unmatchedLiveHostSignalRows"],2)
        raw,state=signals["hostEvidence"]
        self.assertTrue(raw["sdk"]["byteArgumentHashVerified"])
        self.assertEqual(raw["sdk"]["actualArguments"]["length"],63)
        self.assertEqual(state["sdk"]["actualArguments"]["state"]["Axes"]["X"],0.625)
        self.assertEqual(state["sdk"]["actualArguments"]["state"]["Buttons"],1329)
        self.assertTrue(all(not h["issues"] for h in signals["hostEvidence"]))
        coverage=signals["sdkCoverage"]
        self.assertEqual(coverage["steamDeckPartialRawPathComplete"],"UNKNOWN")
        self.assertIn("excludes-SteamDeck",coverage["declaredCoverage"][0])
        self.assertEqual({b["persona"] for b in coverage["observedSdkBoundaries"]},{"elite","dualsense-edge"})

    def test_actual_highrate_fixture_internal_traffic_not_native_live_evidence(self):
        text=(FIXTURES/"host-highrate-samples.jsonl").read_text(encoding="utf-8")
        signals=self.archive({"input-host-signal.log.prev":text})["signalEvidence"]
        self.assertEqual(signals["summary"]["hostSignalRows"],42)
        self.assertEqual(signals["summary"]["internalHostSignalRows"],21)
        self.assertEqual(signals["summary"]["unmatchedLiveHostSignalRows"],21)
        self.assertEqual(signals["summary"]["exactRequestMatches"],0)
        self.assertEqual(signals["summary"]["sdkThrewRows"],1)
        self.assertEqual(signals["hostReasonCounts"],{"live-frame":21,"internal-touch":7,"internal-idle":7,"neutral-command":7})
        windows=[h["window"] for h in signals["hostEvidence"] if h["window"]]
        self.assertEqual(sum(w["calls"] for w in windows),220001)
        self.assertTrue(all(w["calls"]==w["returned"]+w["threw"] for w in windows))
        self.assertTrue(all(not h["userKeyPressProven"] for h in signals["hostEvidence"]))

    def test_both_sides_conflicting_native_witness_stay_ambiguous(self):
        n,h=signal_pair();other=copy.deepcopy(n);other["hostAckAccepted"]=False
        signals=self.archive({"native-detail.log":[n,other],"input-host-signal.log":[h]})["signalEvidence"]
        self.assertEqual(signals["summary"]["exactRequestMatches"],0)
        self.assertEqual(signals["summary"]["ambiguousSignalKeys"],1)

    def test_unknown_sdk_outcome_is_reported_as_gap(self):
        n,h=signal_pair();h.pop("outcome")
        signals=self.audit_pair(n,h)["signalEvidence"]
        self.assertEqual(signals["summary"]["exactRequestMatches"],1)
        self.assertEqual(signals["summary"]["hostOutcomeUnknownRows"],1)
        self.assertIn("sdk-call-outcome-unknown",signals["gaps"])

    def test_oversized_log_is_a_read_gap(self):
        from unittest.mock import patch
        with patch("controller_signal_reverse_audit.MAX_FILE_BYTES",1):
            result=self.audit_pair()
        self.assertEqual(len(result["coverage"]["skippedFiles"]),2)
        self.assertEqual(result["signalEvidence"]["status"],"unproven-from-export")
        self.assertIn("archive-log-read-gaps;capture-completeness-UNKNOWN",result["signalEvidence"]["gaps"])

    def test_bad_scalar_types_cannot_join(self):
        for field in ["protocolPowerGeneration", "wireSampleSequence", "wireMonotonicTick", "request.epoch", "request.commandSequence", "request.sampleSequence", "sampleBucket", "formatVersion"]:
            for value in [True, 1.0, "1", None, []]:
                with self.subTest(field=field,value=value):
                    n,h = signal_pair(); change_path(n,field,value)
                    self.assertEqual(self.audit_pair(n,h)["signalEvidence"]["summary"]["exactRequestMatches"], 0)


if __name__ == "__main__":
    unittest.main()
