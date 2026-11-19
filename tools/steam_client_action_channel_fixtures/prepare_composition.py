#!/usr/bin/env python3
"""Prepare byte-exact includes only; never compile, import, or run product code.

Usage: python prepare-composition.py --source-root REPO --out NEW_EXTERNAL_DIR
Optional: --expect-main SHA256 --expect-ui SHA256 --expect-host SHA256

Offsets in manifest.json are zero-based bytes with exclusive ends. To independently
rebuild an include, verify each sourcePin against the original file, take each
source[startByte:endByteExclusive], verify sliceSha256, and concatenate the ordered
parts (literal parts use bytes.fromhex(hex)). Check every output range and output
pin as well. No source text is decoded or newline-normalized. The manifest pins
the product includes; its own pin is printed to stdout to avoid a circular self-hash.
Failures preserve partial output. Consumers require the complete manifest, all
matching pins, and the successful stdout receipt; a partial directory proves none.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from dataclasses import dataclass


STATUS = "PREPARED_NOT_COMPILED_NOT_RUN"
SOURCE_FILES = {
    "main": "native/main.cpp",
    "ui": "native/steam_ui_mode.h",
    "host": "native/custom-steam-library/workspace_host.cpp",
    "steamSessionObserver": "native/steam_session_observer.h",
    "sleepPowerState": "native/sleep_power_state.h",
    "sleepPowerDispatch": "native/sleep_power_dispatch.h",
    "sleepCancelReady": "native/sleep_cancel_ready.h",
    "steamClientActionChannel": "native/steam_client_action_channel.h",
    "steamClientActionProducer": "native/steam_client_action_producer.h",
}
WHITESPACE = b" \t\r\n\v\f"
# Consume identifiers and pp-numbers too: 10'000 is not a character literal.
LEXEME = re.compile(
    rb"//|/\*|[A-Za-z_][A-Za-z_0-9]*|"
    rb"(?:[0-9]|\.[0-9])(?:[A-Za-z_0-9.]|'[A-Za-z_0-9])*|[\"']"
)
NEWLINE = re.compile(rb"\r\n|\r|\n")
RAW_PREFIXES = {b"R", b"u8R", b"uR", b"UR", b"LR"}
STRING_PREFIXES = {b"u8", b"u", b"U", b"L"}


class PreparationError(Exception):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pin(path: Path, data: bytes) -> dict:
    return {"path": str(path), "bytes": len(data), "sha256": sha256(data)}


def inside(path: Path, root: Path) -> bool:
    return path == root or root in path.parents


@dataclass(frozen=True)
class Source:
    requested: Path
    path: Path
    data: bytes
    source_pin: dict


def read_source(requested: Path, root: Path | None = None) -> Source:
    path = requested.resolve(strict=True)
    if not path.is_file() or (root is not None and not inside(path, root)):
        raise PreparationError(f"Input is not a regular file inside source root: {requested}")
    data = path.read_bytes()
    return Source(requested, path, data, pin(path, data))


def code_projection(data: bytes, label: str) -> bytes:
    """Mask comments/literals at identical byte offsets, preserving CR and LF.

    An opaque literal keeps an '@' token so an anchor cannot span a string as if
    it were whitespace. Raw/escaped strings, character literals, digit separators,
    and backslash-continued line comments are handled without counting brackets.
    This is lexical extraction, not C++ preprocessing or a compiler acceptance.
    """
    result = bytearray(data)
    cursor = 0

    def hide(start: int, end: int, literal: bool = False) -> None:
        result[start:end] = re.sub(rb"[^\r\n]", b" ", data[start:end])
        if literal:
            result[start] = ord("@")

    while True:
        match = LEXEME.search(data, cursor)
        if match is None:
            break
        start, end = match.span()
        token = match.group()
        if token == b"//":
            cursor = end
            while True:
                newline = NEWLINE.search(data, cursor)
                if newline is None:
                    cursor = len(data)
                    break
                newline_start = newline.start()
                cursor = newline.end()
                if data[newline_start - 1:newline_start] != b"\\":
                    break
            hide(start, cursor)
        elif token == b"/*":
            close = data.find(b"*/", end)
            if close < 0:
                raise PreparationError(f"{label}: unterminated comment at byte {start}")
            cursor = close + 2
            hide(start, cursor)
        elif token in RAW_PREFIXES and data[end:end + 1] == b'"':
            opening = data.find(b"(", end + 1, end + 18)
            delimiter = data[end + 1:opening] if opening >= 0 else b""
            if opening < 0 or any(ch <= 32 or ch in b"()\\" or ch == 127 for ch in delimiter):
                raise PreparationError(f"{label}: invalid raw-string delimiter at byte {start}")
            terminator = b")" + delimiter + b'"'
            close = data.find(terminator, opening + 1)
            if close < 0:
                raise PreparationError(f"{label}: unterminated raw string at byte {start}")
            cursor = close + len(terminator)
            hide(start, cursor, True)
        elif token in (b'"', b"'") or (
            token in STRING_PREFIXES and data[end:end + 1] in (b'"', b"'")
        ):
            quote_at = start if token in (b'"', b"'") else end
            quote = data[quote_at]
            cursor = quote_at + 1
            while cursor < len(data):
                char = data[cursor]
                if char == quote:
                    cursor += 1
                    break
                if char == ord("\\"):
                    cursor += 3 if data[cursor + 1:cursor + 3] == b"\r\n" else 2
                elif char in (10, 13):
                    raise PreparationError(f"{label}: unescaped literal newline at byte {cursor}")
                else:
                    cursor += 1
            else:
                raise PreparationError(f"{label}: unterminated literal at byte {start}")
            hide(start, cursor, True)
        else:
            cursor = end
    return bytes(result)


def skip_space(code: bytes, position: int) -> int:
    while position < len(code) and code[position] in WHITESPACE:
        position += 1
    return position


def balanced_end(code: bytes, opening: int, label: str) -> int:
    pairs = {ord("("): ord(")"), ord("["): ord("]"), ord("{"): ord("}")}
    if opening >= len(code) or code[opening] not in pairs:
        raise PreparationError(f"{label}: missing opening delimiter at byte {opening}")
    stack = []
    for position in range(opening, len(code)):
        char = code[position]
        if char in pairs:
            stack.append(pairs[char])
        elif char in b")]}":
            if not stack or stack.pop() != char:
                raise PreparationError(f"{label}: mismatched delimiter at byte {position}")
            if not stack:
                return position + 1
    raise PreparationError(f"{label}: unterminated delimiter at byte {opening}")


def anchor_matches(code: bytes, expression: bytes, label: str) -> list:
    matches = list(re.finditer(rb"(?<![A-Za-z_0-9])" + expression, code))
    if not matches:
        raise PreparationError(f"{label}: missing anchor")
    for match in matches:
        line_start = max(code.rfind(b"\n", 0, match.start()), code.rfind(b"\r", 0, match.start())) + 1
        if code[line_start:match.start()].strip(WHITESPACE):
            raise PreparationError(f"{label}: anchor is not a definition/declaration line boundary")
    return matches


@dataclass(frozen=True)
class Definition:
    name: str
    start: int
    end: int
    parameter_open: int
    parameter_end: int
    body_open: int
    declarations: tuple

    def metadata(self) -> dict:
        return {
            "name": self.name, "startByte": self.start, "endByteExclusive": self.end,
            "parameterOpenByte": self.parameter_open,
            "parameterEndByteExclusive": self.parameter_end, "bodyOpenByte": self.body_open,
            "ignoredForwardDeclarations": list(self.declarations),
        }


def function_definition(code: bytes, expression: bytes, name: str, *,
                        empty_parameters: bool = False, require_attribute: bool = False) -> Definition:
    definitions = []
    declarations = []
    for match in anchor_matches(code, expression, name):
        parameter_open = match.end() - 1
        if code[parameter_open:parameter_open + 1] != b"(":
            raise PreparationError(f"{name}: anchor must end at parameter opening")
        parameter_end = balanced_end(code, parameter_open, name)
        if empty_parameters and code[parameter_open + 1:parameter_end - 1].strip(WHITESPACE):
            raise PreparationError(f"{name}: expected empty parameter list")
        following = skip_space(code, parameter_end)
        if code[following:following + 5] == b"const" and (
            following + 5 == len(code) or not re.match(rb"[A-Za-z_0-9]", code[following + 5:following + 6])
        ):
            following = skip_space(code, following + 5)
        if code[following:following + 8] == b"noexcept" and (
            following + 8 == len(code) or not re.match(rb"[A-Za-z_0-9]", code[following + 8:following + 9])
        ):
            following = skip_space(code, following + 8)
            if code[following:following + 1] == b"(":
                following = skip_space(code, balanced_end(code, following, name))
        if code[following:following + 1] == b";":
            declarations.append({"startByte": match.start(), "endByteExclusive": following + 1})
            continue
        if code[following:following + 1] != b"{":
            raise PreparationError(f"{name}: unsupported or missing definition boundary at byte {following}")
        if require_attribute and not code[match.start():match.start() + 2] == b"[[":
            raise PreparationError(f"{name}: definition lacks required [[maybe_unused]]")
        definitions.append((match.start(), balanced_end(code, following, name),
                            parameter_open, parameter_end, following))
    if len(definitions) != 1:
        raise PreparationError(f"{name}: expected one definition, found {len(definitions)}")
    return Definition(name, *definitions[0], tuple(declarations))


def single_record(code: bytes, expression: bytes, name: str) -> tuple[int, int]:
    matches = anchor_matches(code, expression, name)
    if len(matches) != 1:
        raise PreparationError(f"{name}: ambiguous anchor ({len(matches)} matches)")
    match = matches[0]
    opening = skip_space(code, match.end())
    if code[opening:opening + 1] != b"{":
        raise PreparationError(f"{name}: expected complete record definition")
    closing = skip_space(code, balanced_end(code, opening, name))
    if code[closing:closing + 1] != b";":
        raise PreparationError(f"{name}: missing record terminator")
    return match.start(), closing + 1


def require_gap(code: bytes, start: int, end: int, label: str) -> None:
    if end < start or code[start:end].strip(WHITESPACE):
        raise PreparationError(f"{label}: overlap, reordered definitions, or unexpected intervening code")


class Include:
    def __init__(self, name: str):
        self.name = name
        self.data = bytearray()
        self.parts = []

    def source(self, source: Source, start: int, end: int, name: str, members: list) -> None:
        if not 0 <= start < end <= len(source.data):
            raise PreparationError(f"{name}: invalid source slice")
        chunk = source.data[start:end]
        member_proofs = []
        for member in members:
            member_start, member_end = member["startByte"], member["endByteExclusive"]
            if not start <= member_start < member_end <= end:
                raise PreparationError(f"{name}: member extends outside source slice")
            member_data = source.data[member_start:member_end]
            member_proofs.append({
                **member, "sourcePin": source.source_pin, "bytes": len(member_data),
                "sliceSha256": sha256(member_data),
                "outputStartByte": len(self.data) + member_start - start,
                "outputEndByteExclusive": len(self.data) + member_end - start,
            })
        self.parts.append({
            "kind": "source", "name": name, "sourcePin": source.source_pin,
            "startByte": start, "endByteExclusive": end,
            "bytes": len(chunk), "sliceSha256": sha256(chunk),
            "outputStartByte": len(self.data), "outputEndByteExclusive": len(self.data) + len(chunk),
            "members": member_proofs,
        })
        self.data.extend(chunk)

    def literal(self, chunk: bytes) -> None:
        self.parts.append({
            "kind": "literal", "hex": chunk.hex(), "bytes": len(chunk),
            "sliceSha256": sha256(chunk), "outputStartByte": len(self.data),
            "outputEndByteExclusive": len(self.data) + len(chunk),
        })
        self.data.extend(chunk)

    def metadata(self, out: Path) -> dict:
        return {"name": self.name, "pin": pin(out / self.name, bytes(self.data)), "parts": self.parts}


def prepare_includes(sources: dict[str, Source], information_reader: bool = False) -> list[Include]:
    main = code_projection(sources["main"].data, SOURCE_FILES["main"])
    ui = code_projection(sources["ui"].data, SOURCE_FILES["ui"])
    host = code_projection(sources["host"].data, SOURCE_FILES["host"])
    attribute = rb"(?:\[\[\s*maybe_unused\s*\]\]\s*)?"
    power_specs = [
        ("hardwareWriteAllowed", rb"static\s+bool\s+hardwareWriteAllowed\s*\(", True, False),
        ("currentPowerGeneration", rb"static\s+unsigned\s+long\s+long\s+currentPowerGeneration\s*\(", True, False),
        ("readSharedSystemPower", rb"static\s+int\s+__cdecl\s+readSharedSystemPower\s*\(", False, False),
        ("captureSharedPowerLifecycle", attribute + rb"static\s+bool\s+captureSharedPowerLifecycle\s*\(", False, True),
        ("sharedPowerLifecycleCurrent", attribute + rb"static\s+bool\s+sharedPowerLifecycleCurrent\s*\(", False, True),
    ]
    power = Include("power-body.inc")
    previous_end = -1
    for name, expression, empty, attributed in power_specs:
        definition = function_definition(main, expression, name,
                                         empty_parameters=empty, require_attribute=attributed)
        if definition.start < previous_end:
            raise PreparationError("power-body.inc: definitions overlap or source order changed")
        if power.parts:
            power.literal(b"\n\n")
        power.source(sources["main"], definition.start, definition.end, name, [definition.metadata()])
        previous_end = definition.end
    power.literal(b"\n")

    library = Include("library-handler.inc")
    submitter = function_definition(main, rb"static\s+bool\s+submitSteamActionToPool\s*\(", "submitSteamActionToPool")
    library.source(sources["main"], submitter.start, submitter.end, submitter.name, [submitter.metadata()])
    library.literal(b"\n\n")
    handler = function_definition(main, rb"static\s+LRESULT\s+serveLibrarySteamAction\s*\(", "serveLibrarySteamAction")
    library.source(sources["main"], handler.start, handler.end, handler.name, [handler.metadata()])
    library.literal(b"\n")

    enum_start, enum_end = single_record(ui, rb"enum\s+class\s+ClientCommand\b", "ClientCommand")
    record_start, record_end = single_record(ui, rb"struct\s+ClientDispatch\b", "ClientDispatch")
    client_current = function_definition(ui, rb"inline\s+bool\s+clientCommandCurrent\s*\(", "clientCommandCurrent")
    dispatch = function_definition(ui, rb"inline\s+ClientDispatch\s+dispatchClientCommand\s*\(", "dispatchClientCommand")
    dispatch_owned = function_definition(ui, rb"template\s*<\s*class\s+Source\s*>\s*inline\s+ClientDispatch\s+dispatchClientCommandOwned\s*\(", "dispatchClientCommandOwned")
    set_function = function_definition(ui, rb"inline\s+Json\s+set\s*\(", "set")
    owner = None
    if re.search(rb"(?<![A-Za-z_0-9])clientCommandProcessOwner\s*\(", ui):
        owner = function_definition(ui, rb"inline\s+bool\s+clientCommandProcessOwner\s*\(", "clientCommandProcessOwner")
    definitions = [(enum_start, enum_end), (record_start, record_end)]
    if owner is not None:
        definitions.append((owner.start, owner.end))
    definitions.extend([(client_current.start, client_current.end), (dispatch.start, dispatch.end),
                        (dispatch_owned.start, dispatch_owned.end),
                        (set_function.start, set_function.end)])
    for left, right in zip(definitions, definitions[1:]):
        require_gap(ui, left[1], right[0], "typed-client.inc")
    typed = Include("typed-client.inc")
    typed.source(sources["ui"], enum_start, set_function.start, "ClientCommand through before set", [
        {"name": "ClientCommand", "startByte": enum_start, "endByteExclusive": enum_end},
        {"name": "ClientDispatch", "startByte": record_start, "endByteExclusive": record_end},
        *([owner.metadata()] if owner is not None else []), client_current.metadata(), dispatch.metadata(), dispatch_owned.metadata(),
    ])

    host_specs = [
        ("steamActionNumber", rb"static\s+uint64_t\s+steamActionNumber\s*\("),
        ("steamActionClient", rb"static\s+json\s+steamActionClient\s*\("),
        ("closeSteamForShortcutCommit", rb"static\s+void\s+closeSteamForShortcutCommit\s*\("),
    ]
    host_functions = [function_definition(host, expression, name) for name, expression in host_specs]
    json_integer = function_definition(host, rb"static\s+int64_t\s+jsonIntegerOr\s*\(", "jsonIntegerOr")
    for left, right in zip(host_functions, host_functions[1:] + [json_integer]):
        require_gap(host, left.end, right.start, "host-body.inc")
    host_include = Include("host-body.inc")
    host_include.source(sources["host"], host_functions[0].start, json_integer.start,
                        "steamActionNumber through before jsonIntegerOr",
                        [definition.metadata() for definition in host_functions])
    observer = code_projection(sources["steamSessionObserver"].data, SOURCE_FILES["steamSessionObserver"])
    same_identity = function_definition(observer, rb"bool\s+sameIdentityEpoch\s*\(", "sameIdentityEpoch")
    identity = Include("identity-comparator.inc")
    identity.source(sources["steamSessionObserver"], same_identity.start, same_identity.end,
                    same_identity.name, [same_identity.metadata()])
    identity.literal(b"\n")
    outputs = [power, library, typed, host_include, identity]
    if information_reader:
        matches = list(re.finditer(rb'if\s*\(\s*data\s*&&\s*data->dwData\s*==\s*ymcc::steaminfo::messageTag\s*\)\s*\{', main))
        if len(matches) != 1:
            raise PreparationError("main-information-reader.inc: original ingress must be unique")
        start = matches[0].start()
        opening = main.index(b'{', matches[0].start(), matches[0].end())
        end = balanced_end(main, opening, "main Steam information ingress")
        fragment = main[start:end]
        for anchor in [b'ymcc::steaminfo::serve', b'captureSharedPowerLifecycle', b'ymcc::steaminfo::collect', b'sharedPowerLifecycleCurrent']:
            if fragment.count(anchor) != 1:
                raise PreparationError("main-information-reader.inc: original authority/body changed")
        information = Include("main-information-reader.inc")
        information.literal(b'static LRESULT fixtureLibraryInformation(HWND h, WPARAM w, const COPYDATASTRUCT* data) {\n')
        information.source(sources["main"], start, end, "original main information WM_COPYDATA branch", [])
        information.literal(b'\nreturn FALSE;\n}\n')
        outputs.append(information)
    return outputs


def expected_sha(value: str) -> str:
    if not re.fullmatch(r"[0-9a-fA-F]{64}", value):
        raise argparse.ArgumentTypeError("expected exactly 64 hexadecimal SHA256 characters")
    return value.lower()


def validate_output(requested: Path, out: Path, root: Path) -> None:
    if os.path.lexists(requested) or os.path.lexists(out):
        raise PreparationError(f"Output already exists (including dangling links): {requested}")
    if requested.resolve(strict=False) != out:
        raise PreparationError("Output path resolution changed during preparation")
    if inside(out, root) or inside(requested, root):
        raise PreparationError(f"Output must be outside source root: {out}")


def reread_inputs(sources: dict[str, Source], generator: Source,
                  requested_root: Path, root: Path) -> None:
    if requested_root.resolve(strict=True) != root:
        raise PreparationError("Source root resolution changed during preparation")
    for role, source in list(sources.items()) + [("generator", generator)]:
        if source.requested.resolve(strict=True) != source.path:
            raise PreparationError(f"Input path resolution changed: {role}")
        if source.path.read_bytes() != source.data:
            raise PreparationError(f"Input bytes changed before output creation: {role}")


def write_exclusive(path: Path, data: bytes) -> None:
    # Python 'xb' is wx/CreateNew: O_CREAT | O_EXCL, binary without CRLF conversion.
    with path.open("xb") as stream:
        if stream.write(data) != len(data):
            raise PreparationError(f"Short write: {path}")
        stream.flush()
        os.fsync(stream.fileno())
    if path.read_bytes() != data:
        raise PreparationError(f"Output readback mismatch: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    for role in ("main", "ui", "host"):
        parser.add_argument(f"--expect-{role}", type=expected_sha)
    parser.add_argument("--information-reader", action="store_true", help="Also extract the original main Steam information ingress")
    args = parser.parse_args()
    requested_root = Path(os.path.abspath(args.source_root.expanduser()))
    root = requested_root.resolve(strict=True)
    if not root.is_dir():
        raise PreparationError(f"Source root is not a directory: {root}")
    requested_out = Path(os.path.abspath(args.out.expanduser()))
    out = requested_out.resolve(strict=False)
    if inside(requested_out, requested_root):
        raise PreparationError(f"Output must be outside the requested source tree: {requested_out}")
    validate_output(requested_out, out, root)
    generator = read_source(Path(os.path.abspath(__file__)))
    sources = {role: read_source(root / relative, root) for role, relative in SOURCE_FILES.items()}
    expected = {role: getattr(args, f"expect_{role}") for role in ("main", "ui", "host")}
    for role, wanted in expected.items():
        if wanted is not None and sources[role].source_pin["sha256"] != wanted:
            raise PreparationError(f"--expect-{role} SHA256 mismatch")
    includes = prepare_includes(sources, information_reader=args.information_reader)
    manifest = {
        "schemaVersion": 1, "status": STATUS,
        "sourceRoot": str(root), "outputRoot": str(out), "expectedSha256": expected,
        "scope": "Byte extraction only; no compiler, business, IPC, Steam, or device execution proof.",
        "compiled": False, "businessExecuted": False, "sourceReady": False,
        "inputs": {role: {"repoRelativePath": SOURCE_FILES[role], "pin": source.source_pin}
                   for role, source in sources.items()},
        "generatorPin": generator.source_pin,
        "offsetContract": "Zero-based byte offsets; all ends exclusive; original CR/LF preserved.",
        "reconstruction": "Verify sourcePin; concatenate ordered source slices and hex literals; verify slice/output pins.",
        "closingCheck": "All eight inputs and generator reread byte-for-byte immediately before directory creation.",
        "outputs": [include.metadata(out) for include in includes],
        "manifestSelfPin": "Reported on stdout, not embedded in this manifest (no circular hash).",
    }
    manifest_data = (json.dumps(manifest, ensure_ascii=True, indent=2) + "\n").encode("utf-8")
    # No filesystem mutation precedes the final complete input/self byte check.
    validate_output(requested_out, out, root)
    reread_inputs(sources, generator, requested_root, root)
    validate_output(requested_out, out, root)
    out.mkdir(parents=True, exist_ok=False)
    for include in includes:
        write_exclusive(out / include.name, bytes(include.data))
    # Commit marker last: a partial directory without manifest is not prepared.
    manifest_path = out / "manifest.json"
    write_exclusive(manifest_path, manifest_data)
    print(json.dumps({"status": STATUS, "manifestPin": pin(manifest_path, manifest_data)}, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (PreparationError, OSError, ValueError) as error:
        print(f"PREPARATION_REJECTED: {error}", file=sys.stderr)
        raise SystemExit(1)
