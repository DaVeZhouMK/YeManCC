"""Bounded read-only ZIP reader. stdin/stdout JSON; no extraction or native I/O.

Only central-directory metadata and reviewed log basenames are read. Producer
messages, paths, identifiers, commands, sessions, credentials and free text are
never returned. All diagnostics are fixed codes, with ordinal member/line IDs.
"""
import datetime as dt
import hashlib
import json
import os
import re
import stat
import struct
import sys
import unicodedata
import zipfile

CAPS = {
    "maxArchives": 64, "maxScanEntries": 4096, "maxDepth": 8,
    "maxArchiveBytes": 256 * 1024 * 1024, "maxCentralDirectoryBytes": 8 * 1024 * 1024,
    "maxMembers": 4096, "maxTotalUncompressedBytes": 512 * 1024 * 1024,
    "maxCompressionRatio": 200, "maxMemberReadBytes": 8 * 1024 * 1024,
    "maxTotalReadBytes": 32 * 1024 * 1024, "maxLineBytes": 64 * 1024,
    "maxLinesPerMember": 20000, "maxTotalLines": 50000, "maxEvents": 20000,
}
SLEEP = set("""user-standby-cleared sleep-orphan-recovery-start sleep-orphan-recovery-complete
sleep-exit-cleanup-start sleep-exit-cleanup-complete device-change acdc-change kernel-power-506
kernel-power power-broadcast suspend-keep-writer handheld-sleep-sensors-only non-user-wake-armed
sleep-game-pause external-device-wake-suppressed handheld-input-resume pbt-resume-keep-writer
sleep-game-wake-held s0-wake-message sleep-game-recovery-queued non-user-wake-observed
external-device-wake-evaluated sleep-retry-scheduled external-device-wake-confirmed internal-sleep-506
internal-sleep-request-armed sleep-retry-request kernel-power-107 sleep-retry-suspend-confirmed
s4-wake-message internal-sleep-request-cleared unexpected-wake-cleared s4-wake-canceled-sleep-retry
s0-intent-ignored sleep-game-recovery-attempt sleep-game-recovery-complete s0-wake-classified
s0-wake-keep-writer sleep-game-pause-skipped s0-sleep-retry-canceled-user-wake user-standby
user-initiated-sleep s0-intent-keep-writer s0-intent-accepted""".split())
NATIVE = set("""gamepad-summon-emitted oem-shortcut-click domain-pad-truth system-xinput-readback
rog.hid-bound rog.xbox-face-enabled domain-xbox-face-write rog.controller-remap-noise-ignored
rog.controller-remap msi-wmi-key gamepad.feedbackCaps gamepad-setLed input-capture-bus-rate
power-device-sleep gamepad-controller-sleep hidhide.unplugged-unhide-held xinput-slot-devices
 gamepad-controller-wake input-host-late-hide-decision rog.xbox-face-skipped
msi-dinput-transition power-device-wake power-resume-notification input-host-wake-rescan
wake-input-chain-snapshot power-input-recover-light power-resume-focus-nudge hidhide.unplugged-unhide
rog.controller-remap-deferred domain-log-gate app-exit-ipc input-host-exit-fence begin-async-exit
rog.controller-remap-exit-unresolved exit-chain-order oem-vendor-stack input-host-stop-request
input-host-release-receipt input-host-release-visibility hidhide.p-hid-restore input-host-xinput-plane
input-host-stop-result input-host-switch-audit msi-wmi-gate msi-dinput-gate msi-dinput-open-failed
hidhide.sweep-all-devices session-clean-exit exit-cleanup-worker-detached oem-hid-read-lifecycle
window-destroy boot-single-instance-acquired gamepad-driver-setup window-created
recovery-service-started startup-input-state gamepad-xinput-api oem-shortcut-rules
input-capture-timer-resolution input-capture-scheduler hidhide.app-reg-resident hidhide.cloak-resident
input-host-publication-deferred-to-bus physical-controller-identity rog.hid-discovery-summary
msi-rear-source-observation msi-wmi-source-capability msi-crash-return msi-native-foreground-isolation
input-host-frame-evidence msi-oem-suppression-observation input-host.native-helper-diagnostic
oem-key-consumption oem-shortcut-edge gamepad-physical-source-admission msi-claw-m12-remap-refused
msi-rear-sd-preparation msi-claw-dinput-route msi-claw-gamepad-mode gamepad-physical-source-released
input-host-command-result input-host-msi-stage-evidence hidhide.pre-cloak msi-dinput-bind
msi-foreground-isolation msi-main-source-record msi-dinput-readable msi-backkey-source
msi-backkey-capability msi-backkey-first-sample oem-producer-edge msi-sd-release-fence
input-host-open-result input-host-started late-hide-enqueue late-hide-dequeue late-hide-exec-decision
physical-controller-identities hidhide.p-hid-admitted hidhide.visibility-snapshot hidhide-app-list
late-hide-complete hidhide.p-hid-late-attempt msi-backkey-frame-link wake-input-chain-first-frame
input-host-submit-flow input-host-recovery-episode-new wake-input-chain-sustained
hidhide.p-hid-refreshed input-host-fresh-neutral-admission input-host-frame-admission
input-host-frame-reject msi-backkey-source-release focus-isolation-submit-failed msi-owned-return-recovery""".split())
FAN_NATIVE = set("""fan-activity-boot native-lease-keepalive-renderer-epoch native-lease-keepalive-armed
fan-activity-update fan-host-credential-loaded fan-host-instance-observed native-lease-keepalive-success
native-lease-keepalive-stopped fan-exit-identity fan-exit-parent-exit fan-exit-recovery-handed-off
fan-exit-handoff-result fan-host-identity-unverified fan-host-resume-nowork
lifecycle.power-boundary-supersede fan-exit-recovery-unverified fan-host-wake-deferred fan-host-wake-ready""".split())
FAN_HOST = set("""host.starting host.identity host.instance-started auth.credential-loaded
route.catalog engine.created hc-operation.begin deployment.record-hit hc-operation.return
startup.handshake listener.ready api.request-success hc-operation.phase host.msi-preopen-original-blocks
hc.manager-factory.observed hc.external-profile-events-subscribed api.enable.begin
hc.fan-dispatch.begin hc.fan-dispatch.callback-returned enable.msi-structured-control
 enable.compat-route-callback-contract profile.watchdog-started api.enable.success host.runtime-sample
api.safety-cleanup-lease-bypassed restore.begin restore.hc-device-state restore.msi-narrow-release-dispatch
restore.compat-route-callback-contract restore.msi-structured-release restore.acpi-direct restore.success
restore.already-confirmed recovery.cycle-started parent-exit-handoff.bound parent-exit-handoff.accepted
parent-exit-handoff.recovery-begin restore.close-hc-begin hc-close.virtual-device.begin
hc-close.msi-release-settlement hc-close.virtual-device.return hc-close.manager-cleanup-confirmed
restore.close-hc-success recovery.cycle-completed parent-exit-handoff.recovery-confirmed
parent-exit-handoff.already-settled parent-watchdog.handoff host.stopped parent-watchdog.dispose-deferred-self
api.enable.hc-write-failure api.request-failure api.request-host-error hc.fan-dispatch-failure-retired
hc.temperature-dispatch-failure hc.timeout-returned hc.timeout-returned-after-completed-boundary
hc.timeout-returned-close-failure-after-oem-restore hc.timeout-returned-retired host.runtime-sample-failure
parent-watchdog.finalize-failure power.resume-auto-rebuild.failure power.transition-failure
power.transition-timeout recovery.control-intent-cancel-failed recovery.control-intent-read-failed
recovery.control-intent-write-failed recovery.cycle-read-failed recovery.cycle-write-failed
recovery.takeover-open-failed restore.close-hc-secondary-release-failed restore.deferred-hc-timeout
restore.failure startup.handshake-failure temperature.monitor-stale-oem-restore-failed""".split())
PHASES = set("""ready accepted armed before-envelope close config-cycle-accepted config-returned
confirmed deferred exhausted-reprobe hide immediate input-lost input-lost-reacquire
late-hide-after-source-return loaded neutral neutral-requested no-owned-record open pad-released
passthrough passthrough-requested prepare published raised record-committed recovery-observation
refused release reset Active Neutralized scope-begin sdk-span target-snapshot F3-controlled
close.temperature-monitor-stop close.virtual-device-close enable-curve open.capture-baseline
open.hc-device open.pre-baseline-readonly open.ready-wait""".split())
PRESETS = {"soft", "balanced", "aggressive"}  # Program.cs PresetNodes, only when explicitly logged.
ERROR_CODES = {"E_FAIL", "E_ACCESSDENIED", "E_INVALIDARG", "ERROR_ACCESS_DENIED",
               "ERROR_TIMEOUT", "INVALID_PRESET", "CONTROL_INTENT_CANCEL_FAILED"}
ISO = re.compile(r"\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?(?:Z|[+-]\d{2}:\d{2})?")
TEXT = re.compile(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(?:\.\d{1,7})?) ([+-]\d{2}:\d{2}) \[(VRB|DBG|INF|WRN|ERR|FTL)\](?: .*?)?")
# Per-producer scalar contracts. These remain observations, never commands or
# proof that an OEM/default action was suppressed. No nested/free-text values.
INPUT_FIELDS = {
    "msi-oem-suppression-observation": {
        "bool": ("configured", "actionDispatchAccepted", "takeoverApplied", "contextStableAcrossRead", "readOnly", "registryMutated", "hardwareWriteIssued", "defaultActionSuppressionVerified", "modeWriterProven"),
        "int": ("code", "sequence", "ruleEpoch", "capturedAt", "requestPowerGeneration", "vendorIntent", "beginTick", "endTick", "observationEpisodes", "episodeBudget", "snapshotBudget", "queueDrops", "expired"),
        "enum": {"origin": {"wmi-click", "vendor-takeover-result"}},
    },
    "input-host-late-hide-decision": {
        "bool": ("virtualTargetEnabled", "targetEnabled", "msiGenerationValid"),
        "enum": {"outcome": {"skipped", "applied", "refused"}, "gate": {"virtual-target-not-enabled", "msi-generation-invalid"}},
    },
    "msi-dinput-open-failed": {
        "int": ("openAttempts", "episodeOpens", "episodeBudget", "bindCount", "nextBackoffMs"),
    },
    "msi-claw-gamepad-mode": {
        "bool": ("modeWrite", "virtualTargetCreated", "configured", "accepted", "restored"),
        "enum": {"externalInputBlocked": {"unverified", "verified", "unknown"}},
    },
}
INPUT_PHASES = {"msi-oem-suppression-observation": {"immediate", "settled-1500ms"},
                "msi-claw-gamepad-mode": {"config-cycle-accepted", "config-returned", "restored"}}
SEVERITY_EVENTS = {"VRB": "fan-host.trace-observed", "DBG": "fan-host.debug-observed",
                   "INF": "fan-host.info-observed", "WRN": "fan-host.warning-observed",
                   "ERR": "fan-host.error-observed", "FTL": "fan-host.fatal-observed"}


class Rejected(Exception):
    pass


class DuplicateKey(Exception):
    pass


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKey()
        result[key] = value
    return result


def kind_for(name):
    base = re.sub(r"\.[1-3]$", "", name)
    if base == "fan-api.log":
        return "fan-api"
    if base == "sleep-facts.log":
        return "sleep"
    if base == "native-lifecycle.log":
        return "input"
    if base == "fan-lifecycle.log":
        return "fan-native"
    if base in {"fan-host.log", "fan-host.jsonl", "fan-host-fault.log", "fan-host-fault.jsonl",
                "fan-host-runtime.log", "fan-host-runtime.jsonl", "yeman-fan-host-runtime.log"}:
        return "fan-host"
    if re.fullmatch(r"yeman-fan-host\d{8}\.log", base):
        try:
            dt.datetime.strptime(base[14:22], "%Y%m%d")
        except ValueError:
            return None
        return "fan-text"
    if base == "input-host-fatal.log":
        return "input-fatal"
    if base == "input-host-xbox-sdk.log":
        return "input-xbox"
    return None


def timestamp(value):
    if type(value) is int and 946684800000 <= value <= 4102444800000:
        return value, "unix-ms", value
    if not isinstance(value, str) or not ISO.fullmatch(value):
        return None
    try:
        parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
        basis = "utc" if value.endswith("Z") or value.endswith("+00:00") else "offset" if parsed.tzinfo else "local-unqualified"
        epoch = dt.datetime(1970, 1, 1, tzinfo=dt.timezone.utc) if parsed.tzinfo else dt.datetime(1970, 1, 1)
        return value, basis, int((parsed - epoch).total_seconds() * 1000)
    except (ValueError, OverflowError):
        return None


def numeric_code(value):
    if type(value) is int and -(2 ** 31) <= value < 2 ** 32:
        return value
    if isinstance(value, str) and (re.fullmatch(r"0x[0-9a-fA-F]{1,8}", value) or value in ERROR_CODES):
        return value
    return None


def snapshot_signature(item):
    # Windows lstat can report birth time as ctime while fstat reports change
    # time. Compare the same identity/size/mtime fields across both APIs.
    return item.st_dev, item.st_ino, item.st_size, item.st_mtime_ns


class Reader:
    def __init__(self, limits, domains):
        self.limits = limits
        self.domains = set(domains or ("fan", "sleep", "input", "performance"))
        self.allowlisted = 0
        self.budgets = {}
        self.members = []
        self.events = []
        self.issues = {}
        self.bytes_read = 0
        self.lines_read = 0
        self.unsupported = 0
        self.selected = 0
        self.complete_members = 0
        self.source_archive = None

    def issue(self, status, code, member=None, line=None):
        key = status, code, member
        if key not in self.issues:
            self.issues[key] = {"status": status, "code": code, "count": 0}
            if member is not None:
                self.issues[key]["memberId"] = member
            if line is not None:
                self.issues[key]["firstLine"] = line
        self.issues[key]["count"] += 1

    def central(self, stream):
        size = os.fstat(stream.fileno()).st_size
        if size > self.limits["maxArchiveBytes"]:
            raise Rejected("ARCHIVE_SIZE_LIMIT")
        if size < 22:
            raise Rejected("INVALID_ZIP")
        stream.seek(max(0, size - 65557))
        tail = stream.read(min(size, 65557))
        offset = tail.rfind(b"PK\x05\x06")
        if offset < 0 or offset + 22 > len(tail):
            raise Rejected("INVALID_ZIP")
        _, disk, central_disk, disk_count, count, cd_size, cd_offset, comment = struct.unpack_from("<4s4H2LH", tail, offset)
        if offset + 22 + comment != len(tail):
            raise Rejected("INVALID_ZIP_END")
        if disk or central_disk or disk_count != count:
            raise Rejected("MULTIDISK_UNSUPPORTED")
        if count == 65535 or cd_size == 0xFFFFFFFF or cd_offset == 0xFFFFFFFF:
            raise Rejected("ZIP64_UNSUPPORTED")
        if count > self.limits["maxMembers"]:
            raise Rejected("MEMBER_COUNT_LIMIT")
        if cd_size > self.limits["maxCentralDirectoryBytes"]:
            raise Rejected("CENTRAL_DIRECTORY_LIMIT")
        if cd_offset + cd_size != size - len(tail) + offset:
            raise Rejected("INVALID_CENTRAL_DIRECTORY")
        # Bound the actual entry count before ZipFile allocates ZipInfo objects.
        stream.seek(cd_offset)
        central = stream.read(cd_size)
        cursor, actual = 0, 0
        while cursor < len(central):
            if central[cursor:cursor + 4] != b"PK\x01\x02" or cursor + 46 > len(central):
                raise Rejected("INVALID_CENTRAL_DIRECTORY")
            lengths = struct.unpack_from("<3H", central, cursor + 28)
            cursor += 46 + sum(lengths)
            actual += 1
            if actual > self.limits["maxMembers"]:
                raise Rejected("MEMBER_COUNT_LIMIT")
        if cursor != len(central) or actual != count:
            raise Rejected("CENTRAL_COUNT_MISMATCH")
        return count, cd_offset

    def inventory(self, archive, count, central_offset):
        infos = archive.infolist()
        if len(infos) != count:
            raise Rejected("CENTRAL_COUNT_MISMATCH")
        seen, offsets, total = set(), [], 0
        for index, info in enumerate(infos):
            name = info.orig_filename
            if not name or len(name) > 1024 or "\x00" in name:
                raise Rejected("UNSAFE_MEMBER_PATH")
            normalized = unicodedata.normalize("NFC", name.replace("\\", "/"))
            parts = normalized.rstrip("/").split("/")
            if normalized.startswith("/") or len(parts) > 32 or any(p in {"", ".", ".."} or ":" in p or p.endswith((" ", ".")) for p in parts):
                raise Rejected("UNSAFE_MEMBER_PATH")
            key = normalized.rstrip("/").casefold()
            if key in seen:
                raise Rejected("DUPLICATE_MEMBER")
            seen.add(key)
            mode = (info.external_attr >> 16) & 0xF000
            if mode not in {0, stat.S_IFREG, stat.S_IFDIR}:
                raise Rejected("NON_REGULAR_MEMBER")
            if info.flag_bits & 1:
                raise Rejected("ENCRYPTED_MEMBER")
            if info.compress_type not in {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}:
                raise Rejected("UNSUPPORTED_COMPRESSION")
            if info.file_size and (not info.compress_size or info.file_size > info.compress_size * self.limits["maxCompressionRatio"]):
                raise Rejected("COMPRESSION_RATIO_LIMIT")
            total += info.file_size
            if total > self.limits["maxTotalUncompressedBytes"]:
                raise Rejected("UNCOMPRESSED_SIZE_LIMIT")
            if info.header_offset < 0 or info.header_offset + 30 + info.compress_size > central_offset:
                raise Rejected("INVALID_MEMBER_BOUNDS")
            offsets.append((info.header_offset, info.header_offset + 30 + info.compress_size))
            basename = parts[-1]
            kind = None if info.is_dir() else kind_for(basename)
            member = {"id": "member-%04d" % (index + 1), "compressedBytes": info.compress_size,
                      "uncompressedBytes": info.file_size,
                      "policy": "ALLOWLIST_BOUNDED_READ" if kind else "METADATA_ONLY"}
            if kind:
                domain = "fan" if kind.startswith("fan-") else "sleep" if kind == "sleep" else "input"
                source_domains = {"input", "performance"} if kind == "input" else {domain}
                member.update({"basename": basename, "format": kind, "domain": domain,
                               "domains": sorted(source_domains),
                               "selectedForReplay": bool(self.domains & source_domains)})
                self.allowlisted += 1
                self.selected += int(member["selectedForReplay"])
            self.members.append(member)
        ordered = sorted(offsets)
        if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
            raise Rejected("OVERLAPPING_MEMBERS")
        by_domain = {}
        for member in self.members:
            if member.get("selectedForReplay"):
                for domain in self.domains & set(member["domains"]):
                    by_domain.setdefault(domain, []).append(member["id"])
        for domain, ids in by_domain.items():
            per_member = max(1, self.limits["maxEvents"] // len(by_domain) // len(ids))
            self.budgets.update({(member_id, domain): per_member for member_id in ids})
        return infos

    def project_fan_api(self, record, member, line):
        # This is a privacy projection only. The frozen JS normalizer owns
        # schema2, receipt/response consistency and supported consumer mapping.
        event = record.get("event")
        if event not in {"api.response", "api.transport-failure", "api.request-failure"}:
            self.unsupported += 1
            self.issue("UNSUPPORTED", "FAN_API_EVENT_NOT_CONNECTED", member, line)
            return None
        source = record.get("details")
        if not isinstance(source, dict):
            self.issue("FAIL", "FAN_API_DETAILS_INVALID", member, line)
            return None
        detail = {}
        numeric = {"observationSchemaVersion", "generation", "durationMs", "status"}
        booleans = {"ok", "receiptValid", "bodyCompleteObserved", "bodyComplete", "responseReceived"}
        enums = {"method": {"GET", "POST"}, "path": {"/api/handshake", "/api/state"},
                 "error": {"HTTP request failed (WinHTTP 12029, phase=send)"}}
        for key in ("observationSchemaVersion", "generation", "durationMs", "status", "ok", "receiptValid", "bodyCompleteObserved", "bodyComplete", "responseReceived", "method", "path", "error"):
            if key not in source:
                continue
            value = source[key]
            if key in numeric:
                detail[key] = value if type(value) in (int, float) else None
            elif key in booleans:
                detail[key] = value if type(value) is bool else None
            else:
                detail[key] = value if isinstance(value, str) and value in enums[key] else "[unreviewed]"
        receipt_bools = ("ok", "supported", "resumeAccepted", "resumeNoop", "controlIntentCancelled")
        response_bools = ("ok", "supported", "mockZeroHardwareEvidence", "hardwareCapable",
                          "hardwareWritesEnabled", "hardwareWritesObserved", "fanRouteWriteReady")
        response_enums = {"hostMode": {"source-derived-fixture", "safe-no-hardware", "real-hc", "real-hc-unavailable", "real-hc-fault-locked"},
                          "fanRoute": {"GenericDuty", "GenericRpmTarget", "ProfileCurve", "EC"},
                          "deviceClass": {"HandheldCompanion.Devices.GPDWin5", "HandheldCompanion.Devices.XboxROGAlly", "HandheldCompanion.Devices.XboxROGAllyX"}}
        for name, fields in (("receipt", receipt_bools), ("response", response_bools)):
            if name not in source:
                continue
            original = source[name]
            if not isinstance(original, dict):
                detail[name] = None
                continue
            projected = {key: original[key] if type(original[key]) is bool else None for key in fields if key in original}
            if name == "response":
                if "protocolVersion" in original:
                    value = original["protocolVersion"]
                    projected["protocolVersion"] = value if type(value) is int else None
                for key, allowed in response_enums.items():
                    if key in original:
                        value = original[key]
                        projected[key] = value if value is None or (isinstance(value, str) and value in allowed) else "[unreviewed]"
                if original.get("deviceIdentity") is not None:
                    if original["deviceIdentity"] != {}:
                        self.issue("UNSUPPORTED", "FAN_API_PRIVATE_IDENTITY_NOT_REPLAYABLE", member, line)
                        return None
                    projected["deviceIdentity"] = {}
            detail[name] = projected
        observed = timestamp(record.get("time"))
        output = {"event": "fan-api.observation", "domain": "fan",
                  "fanApiRecord": {"event": event, "details": detail},
                  "projectionPolicy": "allowlisted-scalars-only"}
        if observed:
            output["time"], output["timeBasis"], clock = observed
        else:
            clock = None
            self.issue("INCOMPLETE", "TIMESTAMP_MISSING_OR_INVALID", member, line)
        return output, clock

    def normalize(self, text, kind, member, line):
        try:
            record = json.loads(text, object_pairs_hook=unique_object,
                                parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
        except DuplicateKey:
            self.issue("FAIL", "DUPLICATE_JSON_KEY", member, line)
            return None
        except (ValueError, RecursionError):
            match = TEXT.fullmatch(text) if kind in {"fan-host", "fan-text"} else None
            if match:
                record = {"time": match[1] + match[2], "event": SEVERITY_EVENTS[match[3]]}
                kind = "fan-marker"
            else:
                self.unsupported += 1
                self.issue("UNSUPPORTED", "RECORD_FORMAT_UNSUPPORTED", member, line)
                return None
        if not isinstance(record, dict):
            self.unsupported += 1
            self.issue("UNSUPPORTED", "RECORD_FORMAT_UNSUPPORTED", member, line)
            return None
        if kind == "fan-api":
            return self.project_fan_api(record, member, line)
        if kind == "input-fatal":
            fatal_kind = record.get("kind")
            event = {"orderly": "input-host.orderly-exit-observed", "parent-lost": "input-host.parent-lost-observed",
                     "fatal": "input-host.fatal-observed"}.get(fatal_kind) if isinstance(fatal_kind, str) else None
        elif kind == "input-xbox":
            event = "input-host.xbox-sdk-evidence" if record.get("kind") == "xbox-sdk-evidence" else None
        elif kind == "input" and record.get("event") == "machine-capability":
            # appendNativeLifecycleLog flattens this producer's fields. A
            # record's capability selector cannot borrow nested proof.
            scalars = record if "capability" in record else record.get("details", {})
            if not isinstance(scalars, dict) or scalars.get("capability") != "gpu.dedicatedMemory":
                if "input" not in self.domains:
                    return None
                self.unsupported += 1
                self.issue("UNSUPPORTED", "EVENT_UNSUPPORTED", member, line)
                return None
            if "performance" not in self.domains:
                return None
            event = "machine-capability"
        else:
            if kind == "input" and "input" not in self.domains:
                return None
            event = record.get("eventName") if kind == "fan-host" else record.get("event")
            known = {"sleep": SLEEP, "input": NATIVE, "fan-native": FAN_NATIVE,
                     "fan-host": FAN_HOST, "fan-text": FAN_HOST, "fan-marker": set(SEVERITY_EVENTS.values())}[kind]
            if not isinstance(event, str) or event not in known:
                event = None
        if event is None:
            self.unsupported += 1
            self.issue("UNSUPPORTED", "EVENT_UNSUPPORTED", member, line)
            return None
        domain = "performance" if event == "machine-capability" else "sleep" if kind == "sleep" else "fan" if kind.startswith("fan-") else "input"
        output = {"event": event, "domain": domain}
        if domain == "performance":
            output["capability"] = "gpu.dedicatedMemory"
            output["signalPolicy"] = "diagnostic-observation-only"
            output["signalFieldContainer"] = "record" if scalars is record else "details"
            for field in ("ok", "available", "capacityRead"):
                if field in scalars:
                    if type(scalars[field]) is bool:
                        output[field] = scalars[field]
                    else:
                        self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            enums = {"status": {"available", "unconfirmed"}, "provider": {"intel-gmm", "rog-apu", "rog-vram"},
                     "verification": {"none", "pending_restart", "verified", "default_read", "unconfirmed", "driver_changed", "not_effective"}}
            for field, allowed in enums.items():
                if field in scalars:
                    if isinstance(scalars[field], str) and scalars[field] in allowed:
                        output[field] = scalars[field]
                    else:
                        self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            if "dedicatedBytes" in scalars:
                value = scalars["dedicatedBytes"]
                if type(value) is int and 0 <= value <= 2 ** 53 - 1:
                    output["dedicatedBytes"] = value
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            option_id = lambda value: isinstance(value, str) and bool(re.fullmatch(r"auto|mib:[1-9][0-9]{0,6}", value))
            for field in ("selected", "pendingOption"):
                if field in scalars:
                    if option_id(scalars[field]):
                        output[field] = scalars[field]
                    else:
                        self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            if "options" in scalars:
                options = scalars["options"]
                valid = isinstance(options, list) and 2 <= len(options) <= 64
                sanitized, ids = [], set()
                if valid:
                    for item in options:
                        if not isinstance(item, dict) or not option_id(item.get("id")) or item["id"] in ids or type(item.get("raw")) is not int or not 0 <= item["raw"] <= 0xffffffff or type(item.get("removeOverride")) is not bool or (item.get("capacityMiB") is not None and (type(item.get("capacityMiB")) is not int or not 1 <= item["capacityMiB"] <= 0xffffffff)) or "capacityMiB" not in item:
                            valid = False
                            break
                        ids.add(item["id"])
                        sanitized.append({key: item[key] for key in ("id", "raw", "capacityMiB", "removeOverride")})
                if valid:
                    output["options"] = sanitized
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
        ts_keys = ("timestampUtc", "atUtc", "timestampLocal") if kind == "fan-host" else ("t",) if kind.startswith("input-") else ("time",)
        observed_time = next((record[k] for k in ts_keys if k in record), None)
        ts = timestamp(observed_time)
        if ts:
            output["time"], output["timeBasis"], clock = ts
        else:
            clock = None
            self.issue("INCOMPLETE", "TIMESTAMP_MISSING_OR_INVALID", member, line)
        detail = record.get("details") if isinstance(record.get("details"), dict) else {}
        # Only known scalar fields; no recursion into unreviewed details/context.
        for field in ("phase", "generation", "powerGeneration", "sourceGeneration", "preset"):
            value = record.get(field, detail.get(field))
            if value is None or value == "not-observed":
                continue
            if field == "phase":
                valid = isinstance(value, str) and value in (PHASES | INPUT_PHASES.get(event, set()))
            elif field == "preset":
                valid = domain == "fan" and isinstance(value, str) and value in PRESETS
            else:
                valid = type(value) is int and 0 <= value <= 2 ** 53 - 1
                if isinstance(value, str) and re.fullmatch(r"\d{1,15}", value):
                    value, valid = int(value), True
            if valid:
                output[field] = value
            else:
                self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
        if kind == "input" and event in INPUT_FIELDS:
            policy = INPUT_FIELDS[event]
            # appendNativeLifecycleLog flattens its detail object into the
            # record. Older reviewed fixtures can use details; choose one
            # complete container, never assemble proof across both shapes.
            names = (*policy.get("bool", ()), *policy.get("int", ()), *policy.get("enum", {}))
            scalars = record if any(field in record for field in names) else detail
            output["signalPolicy"] = "diagnostic-observation-only"
            output["signalFieldContainer"] = "record" if scalars is record else "details"
            for field in policy.get("bool", ()):
                if field not in scalars:
                    continue
                if type(scalars[field]) is bool:
                    output[field] = scalars[field]
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            for field in policy.get("int", ()):
                if field not in scalars:
                    continue
                if type(scalars[field]) is int and 0 <= scalars[field] <= 2 ** 53 - 1:
                    output[field] = scalars[field]
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            for field, allowed in policy.get("enum", {}).items():
                if field not in scalars:
                    continue
                if isinstance(scalars[field], str) and scalars[field] in allowed:
                    output[field] = scalars[field]
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
        # HostDiagnosticLog writes the actual Engine.Handshake response into
        # details (Program.cs startup.handshake). Retain only its reviewed
        # booleans, protocol and route enum; never merge records or free text.
        if kind == "fan-host" and event == "startup.handshake":
            modes = {"real-hc-unavailable", "real-hc-authorized", "real-hc-read-only",
                     "real-hc-fault-locked", "safe-no-hardware", "mock-handshake"}
            if "hostMode" in detail:
                if isinstance(detail["hostMode"], str) and detail["hostMode"] in modes:
                    output["hostMode"] = detail["hostMode"]
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            for field in ("ok", "supported", "hardwareCapable", "hardwareWritesEnabled", "hardwareWritesObserved", "fanRouteWriteReady", "recoveryReadbackRequired", "mockZeroHardwareEvidence"):
                if field in detail:
                    if type(detail[field]) is bool:
                        output[field] = detail[field]
                    else:
                        self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            if "protocolVersion" in detail:
                if type(detail["protocolVersion"]) is int and 0 <= detail["protocolVersion"] <= 65535:
                    output["protocolVersion"] = detail["protocolVersion"]
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
            if "fanRoute" in detail:
                if detail["fanRoute"] is None or (isinstance(detail["fanRoute"], str) and detail["fanRoute"] in {"EC", "GenericDuty", "GenericRpmTarget", "ProfileCurve"}):
                    output["fanRoute"] = detail["fanRoute"]
                else:
                    self.issue("UNSUPPORTED", "FIELD_UNSUPPORTED", member, line)
        error = record.get("errorCode", detail.get("errorCode"))
        if error is None and kind == "sleep":
            # These are Win32 error fields, unlike generic `code` (often a key).
            error = next((detail[k] for k in ("requestError", "openError", "shutdownPrivilegeError") if k in detail), None)
        if error is not None:
            code = numeric_code(error)
            if code is None:
                self.issue("UNSUPPORTED", "ERROR_CODE_UNSUPPORTED", member, line)
            else:
                output["errorCode"] = code
        return output, clock

    def replay_member(self, archive, info, member):
        member_id = member["id"]
        kind = member.get("format")
        if not kind or not member.get("selectedForReplay"):
            return
        selected_domains = self.domains & set(member.get("domains", [member["domain"]]))
        event_counts = {domain: 0 for domain in selected_domains}
        read_bytes, lines, origin, last, basis, eof = 0, 0, None, None, None, False
        try:
            with archive.open(info, "r") as stream:
                while True:
                    # An exact limit at the actual member end is complete.
                    # Reading EOF also lets ZipExtFile finish its CRC check.
                    if read_bytes == info.file_size:
                        if stream.read(1):
                            self.issue("FAIL", "MEMBER_SIZE_MISMATCH", member_id)
                        else:
                            eof = True
                        break
                    if all(event_counts[domain] >= self.budgets[(member_id, domain)] for domain in selected_domains):
                        self.issue("INCOMPLETE", "MEMBER_EVENT_BUDGET", member_id)
                        break
                    if len(self.events) >= self.limits["maxEvents"]:
                        self.issue("INCOMPLETE", "EVENT_LIMIT", member_id)
                        break
                    if lines >= self.limits["maxLinesPerMember"] or self.lines_read >= self.limits["maxTotalLines"]:
                        self.issue("INCOMPLETE", "LINE_COUNT_LIMIT", member_id)
                        break
                    remaining = min(self.limits["maxMemberReadBytes"] - read_bytes,
                                    self.limits["maxTotalReadBytes"] - self.bytes_read)
                    if remaining <= 0:
                        self.issue("INCOMPLETE", "READ_BYTE_LIMIT", member_id)
                        break
                    raw = stream.readline(min(self.limits["maxLineBytes"], remaining) + 1)
                    read_bytes += len(raw)
                    self.bytes_read += len(raw)
                    if not raw:
                        eof = True
                        break
                    lines += 1
                    self.lines_read += 1
                    if len(raw) > self.limits["maxLineBytes"]:
                        self.issue("FAIL", "LINE_TOO_LONG", member_id, lines)
                        break
                    if len(raw) > remaining:
                        self.issue("INCOMPLETE", "READ_BYTE_LIMIT", member_id, lines)
                        break
                    try:
                        text = raw.decode("utf-8-sig" if lines == 1 else "utf-8").rstrip("\r\n")
                    except UnicodeError:
                        self.issue("FAIL", "INVALID_UTF8", member_id, lines)
                        break
                    if not text.strip():
                        continue
                    parsed = self.normalize(text, kind, member_id, lines)
                    if not parsed:
                        continue
                    event, clock = parsed
                    domain = event["domain"]
                    if event_counts[domain] >= self.budgets[(member_id, domain)]:
                        self.issue("INCOMPLETE", "MEMBER_EVENT_BUDGET", member_id, lines)
                        continue
                    event_counts[domain] += 1
                    offset = None
                    if clock is not None:
                        if origin is None:
                            origin, basis = clock, event["timeBasis"]
                        if event["timeBasis"] == basis and clock >= origin and (last is None or clock >= last):
                            offset, last = clock - origin, clock
                        else:
                            self.issue("INCOMPLETE", "TIME_ORDER_UNSUPPORTED", member_id, lines)
                    event.update({"id": member_id + ":" + str(lines), "memberId": member_id,
                                  "line": lines, "target": "owned-mock-only", "atMs": offset,
                                  "timingStatus": "OBSERVED_RELATIVE" if offset is not None else "UNSUPPORTED"})
                    self.events.append(event)
        except (zipfile.BadZipFile, EOFError, RuntimeError, OSError, ValueError):
            self.issue("FAIL", "MEMBER_INTEGRITY_FAILED", member_id)
        member["readBytes"] = read_bytes
        member["linesRead"] = lines
        member["integrity"] = "CRC_VERIFIED" if eof else "PARTIAL_UNVERIFIED"
        if eof:
            self.complete_members += 1
        else:
            self.issue("INCOMPLETE", "MEMBER_PARTIAL", member_id)

    def fingerprint(self, stream, initial):
        digest, total = hashlib.sha256(), 0
        stream.seek(0)
        while True:
            block = stream.read(min(65536, self.limits["maxArchiveBytes"] - total + 1))
            if not block:
                break
            total += len(block)
            if total > self.limits["maxArchiveBytes"]:
                raise Rejected("ARCHIVE_SIZE_LIMIT")
            digest.update(block)
        self.source_archive = {"bytes": total,
                               "mtimeUtc": dt.datetime.fromtimestamp(initial.st_mtime, dt.timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z"),
                               "sha256": digest.hexdigest(), "stable": False}
        if total != initial.st_size:
            raise Rejected("ARCHIVE_CHANGED")

    def changed_since(self, archive_path, initial):
        if initial is None:
            return False
        try:
            return snapshot_signature(initial) != snapshot_signature(os.lstat(archive_path))
        except OSError:
            return True

    def run(self, archive_path, mode):
        initial = None
        try:
            source_stat = os.lstat(archive_path)
            if not stat.S_ISREG(source_stat.st_mode):
                raise Rejected("ARCHIVE_NOT_REGULAR_FILE")
            with open(archive_path, "rb") as stream:
                initial = os.fstat(stream.fileno())
                if snapshot_signature(source_stat) != snapshot_signature(initial):
                    raise Rejected("ARCHIVE_CHANGED")
                count, central_offset = self.central(stream)
                self.fingerprint(stream, initial)
                with zipfile.ZipFile(stream, "r") as archive:
                    infos = self.inventory(archive, count, central_offset)
                    if mode == "replay":
                        for info, member in zip(infos, self.members):
                            self.replay_member(archive, info, member)
                final = os.fstat(stream.fileno())
                path_final = os.lstat(archive_path)
                if snapshot_signature(initial) != snapshot_signature(final) or snapshot_signature(initial) != snapshot_signature(path_final):
                    raise Rejected("ARCHIVE_CHANGED")
                self.source_archive["stable"] = True
            if not self.allowlisted:
                self.issue("INCOMPLETE", "NO_ALLOWLISTED_MEMBERS")
            elif not self.selected:
                self.issue("INCOMPLETE", "NO_SELECTED_MEMBERS")
            if mode == "replay" and not self.events:
                self.issue("INCOMPLETE", "NO_SUPPORTED_EVENTS")
        except Rejected as error:
            # Identity races preserve an explicit incomplete result, never a stable hash.
            code = str(error)  # Rejected carries fixed internal codes only.
            if code == "ARCHIVE_CHANGED" or self.changed_since(archive_path, initial):
                self.issues.clear()
                if self.source_archive:
                    self.source_archive["stable"] = False
                self.issue("INCOMPLETE", "ARCHIVE_CHANGED")
            else:
                self.issue("FAIL", code)
        except (zipfile.BadZipFile, UnicodeError, EOFError, OSError, ValueError, struct.error, NotImplementedError):
            if self.changed_since(archive_path, initial):
                self.issues.clear()
                if self.source_archive:
                    self.source_archive["stable"] = False
                self.issue("INCOMPLETE", "ARCHIVE_CHANGED")
            else:
                self.issue("FAIL", "ARCHIVE_INVALID_OR_UNREADABLE")
        diagnostics = list(self.issues.values())
        status = "FAIL" if any(d["status"] == "FAIL" for d in diagnostics) else "INCOMPLETE" if diagnostics else "READY"
        if self.source_archive and not self.source_archive["stable"]:
            self.source_archive["sha256"] = None
        if status == "FAIL" or not self.source_archive or not self.source_archive["stable"]:
            self.events = []  # Never schedule corrupt or unstable evidence.
        return {"status": status, "sourceArchive": self.source_archive,
                "members": self.members, "events": self.events, "diagnostics": diagnostics,
                "summary": {"memberCount": len(self.members), "allowlistedMemberCount": self.allowlisted,
                            "selectedMemberCount": self.selected,
                            "contentBytesRead": self.bytes_read, "linesRead": self.lines_read,
                            "completeMemberCount": self.complete_members, "unsupportedRecordCount": self.unsupported,
                            "metadataOnly": mode == "index", "crossMemberOrderInferred": False}}


def main():
    request_bytes = sys.stdin.buffer.read(32769)
    try:
        if len(request_bytes) > 32768:
            raise ValueError()
        request = json.loads(request_bytes, object_pairs_hook=unique_object)
        if request.get("mode") not in {"index", "replay"} or not isinstance(request.get("archivePath"), str):
            raise ValueError()
        limits = {**CAPS, **request.get("limits", {})}
        if any(k not in CAPS or type(v) is not int or not 1 <= v <= CAPS[k] for k, v in limits.items()):
            raise ValueError()
        domains = request.get("domains", [])
        if not isinstance(domains, list) or any(not isinstance(d, str) or d not in {"fan", "sleep", "input", "performance"} for d in domains):
            raise ValueError()
        output = Reader(limits, domains).run(request["archivePath"], request["mode"])
    except Exception:
        output = {"status": "FAIL", "members": [], "events": [], "summary": {},
                  "diagnostics": [{"status": "FAIL", "code": "READER_INPUT_INVALID"}]}
    # ASCII escaping avoids console-codepage corruption and cannot emit raw logs.
    sys.stdout.write(json.dumps(output, ensure_ascii=True, separators=(",", ":"), allow_nan=False))


if __name__ == "__main__":
    main()
